#version 330

// Full-screen post pass run at the LOW (pixelated) scene resolution, between the scene pass and the tonemap
// upscale. It does two stylised effects on the linear-HDR scene, both driven by an OBJECT MASK so they touch
// world objects only (never the sky, the debug grid, or shadows):
//   1. Screen-space ambient occlusion (SSAO): darkens creases/contact areas on solid world geometry.
//   2. Hand-drawn 1px outlines: the silhouettes/creases of outline-ENABLED objects are drawn NOT in flat black
//      but as a darker or brighter shade of the object's own colour, chosen by the local lighting, so the
//      image reads like a pixel hand-drawn drawing.
// The mask (maskTexture) is: R = per-object outline flag, G = surface flag (1 on any world model, 0 on
// sky/grid). Everything stays in linear HDR; the later tonemap post-pass maps it to display. All sampling uses
// gl_FragCoord (the true framebuffer pixel), NOT fragTexCoord, so the pass is orientation-consistent with the
// scene target regardless of the blit's source-rect flip (matching how the sky shader samples).

in vec2 fragTexCoord;   // unused (see the gl_FragCoord note above); kept so Raylib's blit still has an output
in vec4 fragColor;

uniform sampler2D texture0;      // the linear-HDR scene colour (bound by the blit)
uniform vec4 colDiffuse;         // blit tint (WHITE in normal use)
uniform sampler2D depthTexture;  // the scene depth texture (bound manually to a spare slot)
uniform sampler2D maskTexture;   // the object mask (R = outline flag, G = surface flag)

uniform vec2 resolution;         // scene target size in pixels
uniform mat4 invProjection;      // inverse of the perspective projection used by the scene pass
uniform mat4 projection;         // the perspective projection used by the scene pass (for SSAO re-projection)

uniform float aoStrength;        // effective AO strength (config x world); 0 disables SSAO
uniform float outlineStrength;   // effective outline strength; 0 disables outlines

out vec4 finalColor;

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

// ---- Outline tuning ----
// Crease = how far a neighbour sits OFF this pixel's tangent plane, as a fraction of view distance. This is
// zero for any flat surface (even a steeply tilted floor seen in perspective), so flat ground / shadows do
// NOT get false outlines; only real folds and depth steps between surfaces do.
const float OUTLINE_CREASE_LOW = 0.02;
const float OUTLINE_CREASE_HIGH = 0.06;
// Outline shade: the object's own colour pushed toward darker (in lit areas) or brighter (in shadowed areas),
// always by a clear margin so the contour never fades out at mid tones.
const float OUTLINE_MID = 0.22;        // linear luminance splitting "lit" from "shadowed"
const float OUTLINE_CONTRAST = 0.6;    // how far the outline shade moves from the base colour (0..1)
const float OUTLINE_DARK_LIFT = 0.03;  // small additive lift so a near-black edge still shows a visible line

const float MASK_ON = 0.5; // threshold for the mask flags (they are 0 or 1)

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
        if (texture(maskTexture, sampleUv).g < MASK_ON)
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

void main()
{
    vec2 uv = gl_FragCoord.xy/resolution;
    vec2 texel = 1.0/resolution;

    vec3 sceneColor = texture(texture0, uv).rgb;
    vec4 mask = texture(maskTexture, uv);
    float surface = mask.g;      // 1 on any world model, 0 on sky / grid
    float outlineFlagC = mask.r; // 1 on outline-enabled objects
    bool isSurface = surface > MASK_ON;

    vec3 color = sceneColor;

    // Reconstruct view position + a camera-facing normal once (used by both AO and the crease detector).
    vec3 viewPos = vec3(0.0);
    vec3 normal = vec3(0.0, 0.0, 1.0);
    if (isSurface)
    {
        viewPos = ViewPosFromUv(uv);
        normal = normalize(cross(dFdx(viewPos), dFdy(viewPos)));
        if (normal.z < 0.0) { normal = -normal; }
    }

    // --- Ambient occlusion (solid world geometry only) ---
    if ((aoStrength > 0.0) && isSurface)
    {
        float occlusion = ComputeOcclusion(viewPos, normal);
        color *= 1.0 - clamp(occlusion*aoStrength, 0.0, AO_MAX);
    }

    // --- Hand-drawn outline (outline-enabled objects only) ---
    if (outlineStrength > 0.0)
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
        // Diagonal taps too: an outline object's edge against the background is a staircase at this low
        // resolution; a 4-neighbour test misses the inner-corner pixels (their orthogonal neighbours are all
        // inside the object, only the DIAGONAL neighbour is outside), leaving gaps that the point-upscale turns
        // into a dashed line. The 8-neighbour test flags those corners, so the outline stays continuous.
        vec2 uvLU = clamp(uv - texel, uvMin, uvMax);
        vec2 uvRU = clamp(uv + vec2(texel.x, -texel.y), uvMin, uvMax);
        vec2 uvLD = clamp(uv + vec2(-texel.x, texel.y), uvMin, uvMax);
        vec2 uvRD = clamp(uv + texel, uvMin, uvMax);

        // Silhouette: THIS pixel is an outline object and a neighbour is not (sky/grid/non-outline object).
        // Keyed purely on the object mask, so shadows and flat ground never create a silhouette.
        float minNeighbour = min(
            min(min(texture(maskTexture, uvL).r, texture(maskTexture, uvR).r),
                min(texture(maskTexture, uvU).r, texture(maskTexture, uvD).r)),
            min(min(texture(maskTexture, uvLU).r, texture(maskTexture, uvRU).r),
                min(texture(maskTexture, uvLD).r, texture(maskTexture, uvRD).r)));
        float silhouette = outlineFlagC*(1.0 - minNeighbour);

        // Interior crease: only inside a solid outline object (all four neighbours must be surface too, so the
        // detector never reconstructs sky depth). Uses tangent-plane deviation, which is 0 for any flat/tilted
        // plane and non-zero only at real folds and depth steps -> flat ground and shadows get NO crease.
        float sL = texture(maskTexture, uvL).g;
        float sR = texture(maskTexture, uvR).g;
        float sU = texture(maskTexture, uvU).g;
        float sD = texture(maskTexture, uvD).g;
        float crease = 0.0;
        if ((outlineFlagC*surface*sL*sR*sU*sD) > MASK_ON)
        {
            vec3 pL = ViewPosFromUv(uvL);
            vec3 pR = ViewPosFromUv(uvR);
            vec3 pU = ViewPosFromUv(uvU);
            vec3 pD = ViewPosFromUv(uvD);
            float dev = abs(dot(pL - viewPos, normal)) + abs(dot(pR - viewPos, normal))
                      + abs(dot(pU - viewPos, normal)) + abs(dot(pD - viewPos, normal));
            float dist = max(-viewPos.z, 1e-3);
            crease = smoothstep(OUTLINE_CREASE_LOW*dist, OUTLINE_CREASE_HIGH*dist, dev);
        }

        float edge = clamp(max(silhouette, crease), 0.0, 1.0);
        if (edge > 0.0)
        {
            // Always contrast: darker in lit areas, brighter in shadowed areas, never a no-change mid tone.
            float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
            float lit = smoothstep(OUTLINE_MID*0.7, OUTLINE_MID*1.3, lum);
            float factor = mix(1.0 + OUTLINE_CONTRAST, 1.0 - OUTLINE_CONTRAST, lit);
            vec3 outlineColor = color*factor + vec3(OUTLINE_DARK_LIFT)*(1.0 - lit);
            color = mix(color, outlineColor, edge*clamp(outlineStrength, 0.0, 1.0));
        }
    }

    finalColor = vec4(color, 1.0)*fragColor*colDiffuse;
}
