#version 330

// Screen-space sun shafts (god rays): a radial blur from the sun's screen position that accumulates the sky's
// radiance where the line of sight toward the sun is UNOCCLUDED (sky pixels wrote no depth, so they sit at the
// far plane) and nothing where geometry blocks it — producing bright shafts fanning out from the sun past
// silhouette edges. Runs in linear HDR; the tonemap adds the result (scaled by the sun-shaft strength) before
// ACES. Samples by gl_FragCoord/resolution for orientation-consistency with the other post passes.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;      // linear-HDR scene colour (its sky pixels are the shaft light source)
uniform sampler2D depthTexture;  // scene depth: sky (unoccluded) sits at the far plane, geometry is closer
uniform vec4 colDiffuse;
uniform vec2 resolution;         // this target's pixel size
uniform vec2 sunScreenPos;       // sun position in UV [0,1] (same gl_FragCoord/resolution convention as here)

out vec4 finalColor;

const int SAMPLES = 48;          // steps marched from the fragment toward the sun
const float DENSITY = 0.9;       // fraction of the fragment->sun span the march covers
const float DECAY = 0.95;        // per-step attenuation, so samples nearer the fragment weigh more
const float SKY_DEPTH = 0.9999;  // depth at/above which a pixel is unoccluded sky (the shaft light source)

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;

    // March from this pixel toward the sun; accumulate sky radiance along the way, decaying each step. Where the
    // ray crosses geometry (depth < far) nothing is added, so occluders carve dark gaps into the shafts.
    vec2 delta = (sunScreenPos - uv) * (DENSITY / float(SAMPLES));
    vec2 samplePos = uv;
    float illumination = 1.0;
    vec3 shaft = vec3(0.0);
    for (int i = 0; i < SAMPLES; i++)
    {
        samplePos += delta;
        vec2 sp = clamp(samplePos, vec2(0.0), vec2(1.0));
        if (texture(depthTexture, sp).r >= SKY_DEPTH)
        {
            shaft += max(texture(texture0, sp).rgb, vec3(0.0)) * illumination;
        }
        illumination *= DECAY;
    }
    shaft /= float(SAMPLES);

    // The shaft already carries the sky's own colour (blue high up, warm near the sun) from the sampled scene,
    // so it is NOT re-tinted here — that would double-count the sun colour and over-warm the shafts at sunset.
    finalColor = vec4(shaft, 1.0) * fragColor * colDiffuse;
}
