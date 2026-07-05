#version 330

// Full-screen post pass run at the LOW (pixelated) scene resolution, between the scene pass and the tonemap
// upscale. It does two stylised effects on the linear-HDR scene:
//   1. Screen-space ambient occlusion (SSAO): darkens creases and contact areas using the scene depth buffer.
//   2. Hand-drawn 1px outlines: edges (silhouettes + creases) are drawn NOT in flat black but as a darker or
//      brighter shade of the object's own colour, chosen by the local lighting, so the image reads like a
//      pixel hand-drawn drawing.
// Everything stays in linear HDR; the later tonemap post-pass maps it to display. All sampling uses
// gl_FragCoord (the true framebuffer pixel), NOT fragTexCoord, so the pass is orientation-consistent with the
// scene target regardless of the blit's source-rect flip (matching how the sky shader samples).

in vec2 fragTexCoord;   // unused (see the gl_FragCoord note above); kept so Raylib's blit still has an output
in vec4 fragColor;

uniform sampler2D texture0;      // the linear-HDR scene colour (bound by the blit)
uniform vec4 colDiffuse;         // blit tint (WHITE in normal use)
uniform sampler2D depthTexture;  // the scene depth texture (bound manually to a spare slot)

uniform vec2 resolution;         // scene target size in pixels
uniform mat4 invProjection;      // inverse of the perspective projection used by the scene pass
uniform mat4 projection;         // the perspective projection used by the scene pass (for SSAO re-projection)

uniform float aoStrength;        // effective AO strength (config x world); 0 disables SSAO
uniform float outlineStrength;   // effective outline strength; 0 disables outlines

out vec4 finalColor;

// ---- SSAO tuning ----
const int   AO_KERNEL_SIZE = 12;
const float AO_RADIUS = 0.55;    // hemisphere radius, view-space units
const float AO_BIAS = 0.025;     // depth bias to avoid self-occlusion acne
const float AO_MAX = 0.9;        // clamp so AO never fully blackens a surface

// Fixed hemisphere kernel (all +z); rotated per-pixel and oriented to the reconstructed normal.
const vec3 AO_KERNEL[12] = vec3[](
    vec3( 0.50,  0.00,  0.50), vec3(-0.40,  0.30,  0.40),
    vec3( 0.20, -0.50,  0.30), vec3(-0.30, -0.20,  0.60),
    vec3( 0.60,  0.40,  0.20), vec3(-0.50, -0.40,  0.30),
    vec3( 0.10,  0.60,  0.50), vec3( 0.35, -0.15,  0.70),
    vec3(-0.15,  0.10,  0.25), vec3( 0.00, -0.35,  0.20),
    vec3(-0.25,  0.45,  0.55), vec3( 0.45, -0.45,  0.25)
);

// ---- Outline tuning ----
const float OUTLINE_SILHOUETTE = 0.12; // relative depth jump (fraction of distance) counted as a silhouette
const float OUTLINE_CREASE = 0.35;     // relative depth curvature counted as an interior crease
const float OUTLINE_DARKEN = 0.42;     // outline shade in lit areas (fraction of the object colour)
const float OUTLINE_BRIGHTEN = 1.9;    // outline shade in dark areas (multiple of the object colour)
const float OUTLINE_LUM_LOW = 0.05;    // linear luminance below which the outline goes brighter
const float OUTLINE_LUM_HIGH = 0.5;    // linear luminance above which the outline goes darker

const float BACKGROUND_DEPTH = 0.99999; // depth at/above this is the cleared sky (no AO, no outline)

// Reconstructs the view-space position of the scene pixel at uv from its stored depth.
vec3 ViewPosFromUv(vec2 uv)
{
    float d = texture(depthTexture, uv).r;
    vec4 ndc = vec4(uv*2.0 - 1.0, d*2.0 - 1.0, 1.0);
    vec4 view = invProjection*ndc;
    return view.xyz/view.w;
}

float Hash21(vec2 p)
{
    p = fract(p*vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x*p.y);
}

// Standard hemisphere SSAO: samples the depth buffer around the reconstructed view position and counts how
// many samples are occluded by nearer geometry. Returns occlusion in [0,1] (0 = fully open).
float ComputeOcclusion(vec2 uv, vec3 viewPos, vec3 normal)
{
    // Per-pixel random rotation vector so the small fixed kernel does not band.
    float rnd = Hash21(gl_FragCoord.xy)*6.2831853;
    vec3 randomVec = vec3(cos(rnd), sin(rnd), 0.0);
    vec3 tangent = normalize(randomVec - normal*dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < AO_KERNEL_SIZE; i++)
    {
        // Bias the samples toward the origin (more near-field detail).
        float scale = float(i)/float(AO_KERNEL_SIZE);
        scale = mix(0.1, 1.0, scale*scale);
        vec3 samplePos = viewPos + (tbn*(normalize(AO_KERNEL[i])*scale))*AO_RADIUS;

        vec4 offset = projection*vec4(samplePos, 1.0);
        vec2 sampleUv = (offset.xy/offset.w)*0.5 + 0.5;
        if ((sampleUv.x < 0.0) || (sampleUv.x > 1.0) || (sampleUv.y < 0.0) || (sampleUv.y > 1.0))
        {
            continue;
        }

        float sceneZ = ViewPosFromUv(sampleUv).z; // actual geometry depth at that screen sample (negative = far)
        // Occluded when the real surface there is CLOSER to the camera than the sample point (greater z).
        float occluded = (sceneZ >= samplePos.z + AO_BIAS) ? 1.0 : 0.0;
        // Ignore occluders that are far away in depth (they belong to a different surface), so silhouettes
        // do not haze.
        float rangeCheck = smoothstep(0.0, 1.0, AO_RADIUS/max(abs(viewPos.z - sceneZ), 1e-4));
        occlusion += occluded*rangeCheck;
    }
    return occlusion/float(AO_KERNEL_SIZE);
}

void main()
{
    vec2 uv = gl_FragCoord.xy/resolution;
    vec2 texel = 1.0/resolution;

    vec3 sceneColor = texture(texture0, uv).rgb;
    float centerDepth = texture(depthTexture, uv).r;

    // Background (cleared sky) gets no AO and no outline.
    if (centerDepth >= BACKGROUND_DEPTH)
    {
        finalColor = vec4(sceneColor, 1.0)*fragColor*colDiffuse;
        return;
    }

    vec3 viewPos = ViewPosFromUv(uv);
    // Reconstruct a view-space normal from the position derivatives, forced to face the camera (+z) so the
    // SSAO hemisphere is oriented correctly regardless of any vertical flip in the blit.
    vec3 normal = normalize(cross(dFdx(viewPos), dFdy(viewPos)));
    if (normal.z < 0.0) { normal = -normal; }

    // --- Ambient occlusion ---
    vec3 color = sceneColor;
    if (aoStrength > 0.0)
    {
        float occlusion = ComputeOcclusion(uv, viewPos, normal);
        float ao = 1.0 - clamp(occlusion*aoStrength, 0.0, AO_MAX);
        color *= ao;
    }

    // --- Hand-drawn outline ---
    if (outlineStrength > 0.0)
    {
        float zC = -viewPos.z; // positive distance from camera
        float zL = -ViewPosFromUv(uv - vec2(texel.x, 0.0)).z;
        float zR = -ViewPosFromUv(uv + vec2(texel.x, 0.0)).z;
        float zU = -ViewPosFromUv(uv - vec2(0.0, texel.y)).z;
        float zD = -ViewPosFromUv(uv + vec2(0.0, texel.y)).z;

        // Silhouette: a neighbour clearly BEHIND this pixel means this is the near (foreground) edge.
        float silhouette = max(max(zL, zR), max(zU, zD)) - zC;
        float sil = smoothstep(OUTLINE_SILHOUETTE*zC, OUTLINE_SILHOUETTE*zC*2.0, silhouette);
        // Crease: depth curvature (Laplacian) picks up interior folds where the silhouette test is quiet.
        float crease = abs(zL + zR - 2.0*zC) + abs(zU + zD - 2.0*zC);
        float cr = smoothstep(OUTLINE_CREASE*zC, OUTLINE_CREASE*zC*3.0, crease);
        float edge = clamp(max(sil, cr), 0.0, 1.0);

        // Outline shade = the object's own colour, pushed darker in lit areas and brighter in dark areas so
        // the contour reads like a hand-drawn ink line lit by the same light as the surface.
        float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
        float t = smoothstep(OUTLINE_LUM_LOW, OUTLINE_LUM_HIGH, lum);
        vec3 outlineColor = mix(color*OUTLINE_BRIGHTEN, color*OUTLINE_DARKEN, t);
        color = mix(color, outlineColor, edge*clamp(outlineStrength, 0.0, 1.0));
    }

    finalColor = vec4(color, 1.0)*fragColor*colDiffuse;
}
