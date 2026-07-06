#version 330

// Full-screen post pass run at the LOW (pixelated) scene resolution, between the scene pass and the tonemap
// upscale. It does two stylised effects on the linear-HDR scene, both driven by the low-res NORMAL/MASK
// G-buffer (normalTexture) so they touch world objects only (never the sky, the debug grid, or shadows):
//   1. Screen-space ambient occlusion (SSAO): darkens creases/contact areas on solid world geometry.
//   2. Outlines, in the style of the three.js RenderPixelatedPass and the Godot 3D-pixel-art shader:
//        * SILHOUETTES (depth / surface discontinuities) darken the near object's rim.
//        * INTERIOR CREASES (view-normal discontinuities) darken on the sun-lit side and brighten on the
//          shadowed side, tying the line art to the same sun that casts the shadows.
// The G-buffer packs a VIEW-SPACE normal in RGB (encoded n*0.5+0.5) and a surface/outline flag in A:
//   A = 0.0 -> not a surface (sky / grid);  0.5 -> surface, no outline;  1.0 -> surface, outline enabled.
// Because the sky is never drawn into the G-buffer it has A = 0, so it is neither ambient-occluded nor
// outlined (outlines are never applied to the sky). Everything stays in linear HDR; the later tonemap pass
// maps it to display. All sampling uses gl_FragCoord (the true framebuffer pixel), NOT fragTexCoord, so the
// pass is orientation-consistent with the scene target regardless of the blit's source-rect flip (matching
// how the sky shader samples).

in vec2 fragTexCoord;   // unused (see the gl_FragCoord note above); kept so Raylib's blit still has an output
in vec4 fragColor;

uniform sampler2D texture0;      // the linear-HDR scene colour (bound by the blit)
uniform vec4 colDiffuse;         // blit tint (WHITE in normal use)
uniform sampler2D depthTexture;  // the scene depth texture (bound to a spare slot via SetShaderValueTexture)
uniform sampler2D normalTexture; // the normal/mask G-buffer (RGB = view normal, A = surface/outline flag)

uniform vec2 resolution;         // scene target size in pixels
uniform mat4 invProjection;      // inverse of the perspective projection used by the scene pass
uniform mat4 projection;         // the perspective projection used by the scene pass (for SSAO re-projection)
uniform vec3 sunDirectionView;   // unit direction TO the sun, in VIEW space (for sun-aware crease colouring)

uniform float aoStrength;        // effective AO strength (config x world); 0 disables SSAO
uniform float outlineStrength;   // effective outline strength (0..1); 0 disables outlines

out vec4 finalColor;

// ---- G-buffer flag thresholds (the alpha channel is 0.0 / 0.5 / 1.0) ----
const float SURFACE_ON = 0.25;   // alpha above this = a solid world-model surface (0 = sky / grid)
const float OUTLINE_ON = 0.75;   // alpha above this = surface with per-object outlines enabled

// ---- SSAO tuning ----
const int   AO_KERNEL_SIZE = 12;
const float AO_RADIUS = 0.5;     // hemisphere radius, view-space units
// Occlusion is counted only where the occluder rises ABOVE this surface's own tangent plane by more than
// AO_TANGENT_BIAS (ramping to full at AO_TANGENT_FULL), in view-space units. On a flat surface (even a steeply
// tilted floor seen grazing) all neighbours lie IN the tangent plane, so this is ~0 -> no false darkening of
// the ground at grazing angles. Only geometry that genuinely stands proud of the surface (a wall meeting the
// floor, a crease) occludes.
const float AO_TANGENT_BIAS = 0.02;
const float AO_TANGENT_FULL = 0.10;
const float AO_MAX = 0.6;        // clamp so AO never fully blackens a surface

// Fixed hemisphere kernel (all +z); oriented to the reconstructed normal via a stable (non-random) basis, so
// the AO is smooth rather than speckled at the low pixel resolution (a per-pixel random rotation needs a blur
// pass to not look like noise, and there is none here).
// (Explicit array size in the constructor: implicit-size constructors are valid GLSL 330 but some drivers
// reject them, and a failed compile silently degrades this pass to a plain copy.)
const vec3 AO_KERNEL[12] = vec3[12](
    vec3( 0.50,  0.00,  0.50), vec3(-0.40,  0.30,  0.40),
    vec3( 0.20, -0.50,  0.30), vec3(-0.30, -0.20,  0.60),
    vec3( 0.60,  0.40,  0.20), vec3(-0.50, -0.40,  0.30),
    vec3( 0.10,  0.60,  0.50), vec3( 0.35, -0.15,  0.70),
    vec3(-0.15,  0.10,  0.25), vec3( 0.00, -0.35,  0.20),
    vec3(-0.25,  0.45,  0.55), vec3( 0.45, -0.45,  0.25)
);

// ---- Outline tuning (depth + view-normal edge detection) ----
// three.js normal-edge bias: a fixed direction that decides which side of a crease is treated as the edge, so
// the line falls on a consistent side of each fold.
const vec3  NORMAL_EDGE_BIAS = vec3(1.0, 1.0, 1.0);
// Silhouette depth step, as a fraction of the centre's view distance, that begins (LO) / fully counts (HI) as
// an edge between two SURFACES. Distance-scaled so contour thickness stays consistent with depth. (The much
// larger object-vs-sky silhouette is keyed on the surface flag instead, so it is robust to sky depth.)
const float OUTLINE_DEPTH_LO = 0.04;
const float OUTLINE_DEPTH_HI = 0.12;
// How strongly each edge type recolours the pixel (all further scaled by outlineStrength):
const float OUTLINE_SILHOUETTE_DARKEN = 0.35; // silhouette rim: darken
const float OUTLINE_CREASE_DARKEN = 0.22;     // interior crease facing the sun: darken
const float OUTLINE_CREASE_BRIGHTEN = 0.55;   // interior crease facing away from the sun: brighten

// Reconstructs the view-space position of the scene pixel at uv from its stored depth.
vec3 ViewPosFromUv(vec2 uv)
{
    float d = texture(depthTexture, uv).r;
    vec4 ndc = vec4(uv*2.0 - 1.0, d*2.0 - 1.0, 1.0);
    vec4 view = invProjection*ndc;
    return view.xyz/view.w;
}

// Builds a stable orthonormal basis around a +z-ish normal (Duff et al., branchless). normal.z is forced >= 0
// by the caller, so the sign term is always +1. No randomness -> no SSAO speckle.
mat3 TangentBasis(vec3 normal)
{
    float a = -1.0/(1.0 + normal.z);
    float b = normal.x*normal.y*a;
    vec3 tangent = vec3(1.0 + normal.x*normal.x*a, b, -normal.x);
    vec3 bitangent = vec3(b, 1.0 + normal.y*normal.y*a, -normal.y);
    return mat3(tangent, bitangent, normal);
}

// Tangent-plane hemisphere SSAO: for each hemisphere sample, read the ACTUAL geometry at that screen location
// and count it as an occluder only insofar as it stands ABOVE this surface's tangent plane. Flat surfaces
// (their neighbours lie in the plane) self-occlude ~0, which is what kills the grazing-angle dark ground.
// Returns occlusion in [0,1] (0 = fully open).
float ComputeOcclusion(vec3 viewPos, vec3 normal)
{
    mat3 tbn = TangentBasis(normal);

    float occlusion = 0.0;
    for (int i = 0; i < AO_KERNEL_SIZE; i++)
    {
        float scale = float(i)/float(AO_KERNEL_SIZE);
        scale = mix(0.1, 1.0, scale*scale);
        vec3 samplePos = viewPos + (tbn*(normalize(AO_KERNEL[i])*scale))*AO_RADIUS;

        vec4 offset = projection*vec4(samplePos, 1.0);
        vec2 sampleUv = (offset.xy/offset.w)*0.5 + 0.5;
        if ((sampleUv.x < 0.0) || (sampleUv.x > 1.0) || (sampleUv.y < 0.0) || (sampleUv.y > 1.0))
        {
            continue;
        }
        // Only solid world geometry occludes (sky/grid have surface flag 0), so the sky never darkens objects.
        if (texture(normalTexture, sampleUv).a < SURFACE_ON)
        {
            continue;
        }

        vec3 occluder = ViewPosFromUv(sampleUv);   // real geometry at this screen sample
        vec3 diff = occluder - viewPos;
        float aboveTangent = smoothstep(AO_TANGENT_BIAS, AO_TANGENT_FULL, dot(diff, normal));
        float rangeCheck = smoothstep(0.0, 1.0, AO_RADIUS/max(length(diff), 1e-4));
        occlusion += aboveTangent*rangeCheck;
    }
    return occlusion/float(AO_KERNEL_SIZE);
}

// Silhouette contribution of one neighbour: 1 when the neighbour lies BEHIND the centre, i.e. part of the
// centre object's outline. That is either the neighbour being background (not a surface) or a surface stepped
// away from the centre by more than a distance-scaled margin (so one object occluding another also gets a
// contour). The background case is keyed on the surface flag, NOT on depth, so it is robust to the engine's
// unreliable sky depth; the depth term (nulled for background neighbours) only distinguishes two surfaces.
float SilhouetteNeighbour(float neighbourFlag, float neighbourDist, float centreDist)
{
    float surf = step(SURFACE_ON, neighbourFlag);
    float background = 1.0 - surf;
    float stepAway = surf*smoothstep(OUTLINE_DEPTH_LO*centreDist, OUTLINE_DEPTH_HI*centreDist, neighbourDist - centreDist);
    return clamp(background + stepAway, 0.0, 1.0);
}

// Interior-crease contribution of one neighbour (three.js normalEdgeIndicator term): fires where the neighbour
// is a surface whose view-normal differs from the centre's, and only on the nearer (shallower) side so each
// crease is drawn once. Background neighbours contribute 0 (surf = 0).
float CreaseNeighbour(vec3 centreNormal, vec4 neighbourSample, float neighbourDist, float centreDist)
{
    float surf = step(SURFACE_ON, neighbourSample.a);
    vec3 neighbourNormal = normalize(neighbourSample.rgb*2.0 - 1.0);
    float depthDiff = neighbourDist - centreDist;
    float normalDiff = dot(centreNormal - neighbourNormal, NORMAL_EDGE_BIAS);
    float normalIndicator = clamp(smoothstep(-0.01, 0.01, normalDiff), 0.0, 1.0);
    float depthIndicator = clamp(sign(depthDiff*0.25 + 0.0025), 0.0, 1.0); // only the nearer pixel draws it
    return (1.0 - dot(centreNormal, neighbourNormal))*depthIndicator*normalIndicator*surf;
}

void main()
{
    vec2 uv = gl_FragCoord.xy/resolution;
    vec2 texel = 1.0/resolution;

    vec3 sceneColor = texture(texture0, uv).rgb;
    vec4 gbuffer = texture(normalTexture, uv);
    bool isSurface = gbuffer.a > SURFACE_ON;   // a solid world model (not sky / grid)
    bool hasOutline = gbuffer.a > OUTLINE_ON;  // ... with per-object outlines enabled
    vec3 nView = normalize(gbuffer.rgb*2.0 - 1.0); // view-space surface normal (only meaningful if isSurface)

    vec3 color = sceneColor;

    // Reconstruct the centre's view position + a geometric normal once (used by the AO and, for its z, by the
    // outline's distance-scaled silhouette test). Only meaningful on surfaces.
    vec3 viewPos = vec3(0.0);
    vec3 geoNormal = vec3(0.0, 0.0, 1.0);
    if (isSurface)
    {
        viewPos = ViewPosFromUv(uv);
        geoNormal = normalize(cross(dFdx(viewPos), dFdy(viewPos)));
        if (geoNormal.z < 0.0) { geoNormal = -geoNormal; }
    }

    // --- Ambient occlusion (solid world geometry only) ---
    if ((aoStrength > 0.0) && isSurface)
    {
        float occlusion = ComputeOcclusion(viewPos, geoNormal);
        color *= 1.0 - clamp(occlusion*aoStrength, 0.0, AO_MAX);
    }

    // --- Outline (depth + view-normal edge detection; outline-enabled surfaces only, never the sky) ---
    if ((outlineStrength > 0.0) && isSurface && hasOutline)
    {
        // Neighbour tap coordinates, clamped to the texture interior: raylib render-texture samplers default
        // to REPEAT wrap, so an unclamped tap at the screen border would wrap to the OPPOSITE edge and mint a
        // false silhouette there. Clamped, a border pixel's neighbour is itself -> no edge.
        vec2 uvMin = texel*0.5;
        vec2 uvMax = vec2(1.0) - texel*0.5;
        vec2 uvL = clamp(uv - vec2(texel.x, 0.0), uvMin, uvMax);
        vec2 uvR = clamp(uv + vec2(texel.x, 0.0), uvMin, uvMax);
        vec2 uvU = clamp(uv - vec2(0.0, texel.y), uvMin, uvMax);
        vec2 uvD = clamp(uv + vec2(0.0, texel.y), uvMin, uvMax);

        vec4 sL = texture(normalTexture, uvL);
        vec4 sR = texture(normalTexture, uvR);
        vec4 sU = texture(normalTexture, uvU);
        vec4 sD = texture(normalTexture, uvD);

        float centreDist = -viewPos.z;
        float dL = -ViewPosFromUv(uvL).z;
        float dR = -ViewPosFromUv(uvR).z;
        float dU = -ViewPosFromUv(uvU).z;
        float dD = -ViewPosFromUv(uvD).z;

        // Silhouette: depth / background discontinuity -> the near object's rim darkens.
        float silhouette = SilhouetteNeighbour(sL.a, dL, centreDist) + SilhouetteNeighbour(sR.a, dR, centreDist)
                         + SilhouetteNeighbour(sU.a, dU, centreDist) + SilhouetteNeighbour(sD.a, dD, centreDist);
        float dei = clamp(silhouette, 0.0, 1.0);

        // Interior crease: view-normal discontinuity within the object -> sun-aware line.
        float crease = CreaseNeighbour(nView, sL, dL, centreDist) + CreaseNeighbour(nView, sR, dR, centreDist)
                     + CreaseNeighbour(nView, sU, dU, centreDist) + CreaseNeighbour(nView, sD, dD, centreDist);
        float nei = step(0.1, crease);

        float coeff;
        if (dei > 0.0)
        {
            coeff = 1.0 - OUTLINE_SILHOUETTE_DARKEN*dei; // silhouette always darkens (three.js depth edge)
        }
        else
        {
            // Sun-aware crease: darken the sun-lit side, brighten the shadowed side (Godot 3D-pixel-art look),
            // driven by the same sun direction that casts the shadows.
            float towardSun = dot(nView, sunDirectionView); // > 0 on the sun-facing side
            float litSide = smoothstep(-0.15, 0.15, towardSun);
            coeff = 1.0 - OUTLINE_CREASE_DARKEN*nei*litSide + OUTLINE_CREASE_BRIGHTEN*nei*(1.0 - litSide);
        }
        color *= mix(1.0, coeff, clamp(outlineStrength, 0.0, 1.0));
    }

    finalColor = vec4(color, 1.0)*fragColor*colDiffuse;
}
