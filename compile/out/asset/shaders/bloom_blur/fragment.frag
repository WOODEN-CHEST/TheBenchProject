#version 330

// Separable Gaussian blur for bloom, run twice per iteration (once horizontal, once vertical). Uses the
// 5-sample linear-sampled Gaussian (fractional-texel taps that rely on BILINEAR filtering of the source, so
// the bloom targets must be created with TEXTURE_FILTER_BILINEAR). Weights sum to 1.0, so the blur preserves
// energy. Samples by gl_FragCoord/resolution for orientation-consistency, like the other post passes.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   // the bloom buffer to blur
uniform vec4 colDiffuse;
uniform vec2 resolution;      // this target's pixel size
uniform vec2 direction;       // (1,0) for the horizontal pass, (0,1) for the vertical pass (in texel units)

out vec4 finalColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;
    vec2 texel = direction / resolution;

    // Clamp each offset tap to [0,1]: raylib render-texture samplers default to REPEAT wrap, so an out-of-range
    // tap near a screen edge would wrap to the OPPOSITE edge and smear a bright (e.g. emissive) glow into a
    // faded line along that far edge. Clamping keeps edge taps on the edge. (The bloom targets are also set to
    // CLAMP wrap, which additionally protects the tonemap's bilinear upsample of this buffer.)
    vec3 sum = texture(texture0, uv).rgb * 0.227027;
    sum += texture(texture0, clamp(uv + texel * 1.3846153846, 0.0, 1.0)).rgb * 0.3162162162;
    sum += texture(texture0, clamp(uv - texel * 1.3846153846, 0.0, 1.0)).rgb * 0.3162162162;
    sum += texture(texture0, clamp(uv + texel * 3.2307692308, 0.0, 1.0)).rgb * 0.0702702703;
    sum += texture(texture0, clamp(uv - texel * 3.2307692308, 0.0, 1.0)).rgb * 0.0702702703;

    finalColor = vec4(sum, 1.0) * fragColor * colDiffuse;
}
