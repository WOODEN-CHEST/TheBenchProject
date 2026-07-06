#version 330

// Composites the crisp (un-pixelated, full-resolution) objects over the already-tonemapped pixelated frame.
// The crisp objects were rendered at window resolution into an HDR colour+depth target; here they are tonemapped
// (matching the main tonemap) and drawn ONLY where they are the frontmost surface — depth-tested against the
// (low-res) pixelated scene's depth so nearer pixelated geometry still occludes them. Pixels with no crisp
// geometry, or where the crisp surface is behind the scene, are discarded so the pixelated frame shows through.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;     // crisp objects' linear-HDR colour (window resolution)
uniform sampler2D crispDepth;   // crisp objects' depth (window resolution)
uniform sampler2D sceneDepth;   // the pixelated scene's depth (low resolution)
uniform vec4 colDiffuse;
uniform float exposure;         // same HDR eye-adaptation exposure the main tonemap uses

out vec4 finalColor;

const float FAR_DEPTH = 0.9999;   // at/above this the crisp target has no geometry at this pixel
const float DEPTH_BIAS = 1e-5;    // tolerance for the crisp-vs-scene depth comparison

// Narkowicz ACES filmic tonemap — identical to the main tonemap pass so crisp objects match the world's look.
vec3 TonemapACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x + b))/(x*(c*x + d) + e), 0.0, 1.0);
}

void main()
{
    float cd = texture(crispDepth, fragTexCoord).r;
    if (cd >= FAR_DEPTH) { discard; }                 // no crisp object at this pixel -> keep the pixelated frame
    float sd = texture(sceneDepth, fragTexCoord).r;
    if (cd > sd + DEPTH_BIAS) { discard; }            // crisp object is behind the pixelated scene -> occluded

    vec3 hdr = max(texture(texture0, fragTexCoord).rgb, vec3(0.0));
    float ev = (exposure > 0.0) ? exposure : 1.0;
    vec3 mapped = pow(TonemapACES(hdr*ev), vec3(1.0/2.2)); // ACES + linear->sRGB, matching the main tonemap
    finalColor = vec4(mapped, 1.0) * fragColor * colDiffuse;
}
