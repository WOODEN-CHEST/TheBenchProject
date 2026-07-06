#version 330

// World sprite shader: draws a 2D sprite (a billboard textured with an animation frame) into the LINEAR-HDR
// scene buffer. Sprite art is authored in sRGB, so it is linearized here (the tonemap pass maps the linear
// scene back to display with gamma at the end) — otherwise the sprite would be double-gamma'd and look wrong.
// Fragment-only: Raylib's default vertex stage supplies the billboard's UVs (fragTexCoord) and tint (fragColor).

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   // the animation frame's texture (source region selected by the billboard UVs)
uniform vec4 colDiffuse;      // batch tint (WHITE under BeginShaderMode)

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord) * fragColor * colDiffuse;
    // sRGB sprite art -> linear light. Alpha is left as-is (not gamma-encoded) for correct blending.
    vec3 linear = pow(max(texel.rgb, vec3(0.0)), vec3(2.2));
    finalColor = vec4(linear, texel.a);
}
