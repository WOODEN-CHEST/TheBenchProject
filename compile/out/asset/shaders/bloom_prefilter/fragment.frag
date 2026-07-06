#version 330

// Bloom bright-pass / downsample: reads the linear-HDR scene and keeps only the energy above a brightness
// threshold (with a soft knee so the bloom fades in gradually instead of a hard cut-in), writing it into a
// smaller HDR target that the blur passes then spread. Runs in linear HDR, before the tonemap. Samples by
// gl_FragCoord/resolution (not fragTexCoord) so it is orientation-consistent with the scene regardless of any
// blit flip (the same convention the sky/postfx passes use).

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   // the linear-HDR scene (or post-processed scene) to extract bright areas from
uniform vec4 colDiffuse;
uniform vec2 resolution;      // this (bloom) target's pixel size
uniform float threshold;      // brightness (max channel, linear) above which a pixel contributes to bloom
uniform float softKnee;       // 0..1 width of the soft roll-in just below the threshold

// Sky exclusion: the sky (with its sun disc + stars) is drawn as a full-screen pass that writes NO depth, so
// its pixels sit at the cleared far plane while every object writes real depth. depthMask > 0.5 masks those
// sky pixels out of the bloom so only OBJECTS glow (the bright sun no longer smears a white halo across the
// sky). depthMask <= 0.5 blooms everything (used when the scene depth is not a samplable texture).
uniform sampler2D depthTexture;
uniform float depthMask;

out vec4 finalColor;

// The sky sits exactly at the cleared far depth (1.0); any object is strictly closer, so this cleanly splits
// sky from geometry. A hair below 1.0 to stay clear of depth precision at the very far plane.
const float SKY_DEPTH = 0.9999;

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;

    // Reject sky pixels (no bloom from the sun/sky/stars) when masking is enabled.
    if (depthMask > 0.5)
    {
        if (texture(depthTexture, uv).r >= SKY_DEPTH)
        {
            finalColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    vec3 c = max(texture(texture0, uv).rgb, vec3(0.0));

    // Soft-knee threshold (the standard Unity/UE curve): a quadratic roll-in over [threshold-knee, threshold+knee]
    // then a linear region above, so bright areas bloom without a hard on/off edge at exactly the threshold.
    float brightness = max(c.r, max(c.g, c.b));
    float knee = max(threshold * softKnee, 1e-4);
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 1e-4);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);

    finalColor = vec4(c * contribution, 1.0) * fragColor * colDiffuse;
}
