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

out vec4 finalColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;
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
