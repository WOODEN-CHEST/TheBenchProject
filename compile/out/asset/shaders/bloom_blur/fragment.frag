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

    vec3 sum = texture(texture0, uv).rgb * 0.227027;
    sum += texture(texture0, uv + texel * 1.3846153846).rgb * 0.3162162162;
    sum += texture(texture0, uv - texel * 1.3846153846).rgb * 0.3162162162;
    sum += texture(texture0, uv + texel * 3.2307692308).rgb * 0.0702702703;
    sum += texture(texture0, uv - texel * 3.2307692308).rgb * 0.0702702703;

    finalColor = vec4(sum, 1.0) * fragColor * colDiffuse;
}
