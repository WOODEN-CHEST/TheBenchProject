#version 330

// Normal / mask G-buffer pass for SPRITES (billboards). Companion to the model `world_normal` shader: the
// postfx outline pass reads the same buffer, so sprites written here get the same silhouette outlines models do.
//
// Two differences from the model version:
//   * It ALPHA-DISCARDS transparent texels, so the outline follows the sprite's actual (alpha) silhouette
//     rather than the billboard's rectangle.
//   * A billboard is flat and faces the camera, so a constant (0,0,1) VIEW normal is written (no internal
//     creases on a flat sprite — only the silhouette matters). Encoded n*0.5+0.5 = (0.5,0.5,1.0).
//
// Output packing matches world_normal exactly: RGB = encoded view normal, A = surface/outline flag
// (0.5 = surface, 1.0 = surface with outlines). Drawn with colour blending disabled (RenderNormalBuffer), so
// RGB/A are written verbatim; the alpha discard (not blending) handles the sprite's transparency.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   // the sprite frame's texture (its alpha is the sprite's shape)
uniform vec4 colDiffuse;
uniform float outlineFlag;    // 1.0 if this sprite has outlines enabled, else 0.0 (set per sprite)

out vec4 finalColor;

void main()
{
    float alpha = texture(texture0, fragTexCoord).a * fragColor.a * colDiffuse.a;
    if (alpha < 0.5)
    {
        discard; // transparent sprite texel: not a surface, so the outline hugs the sprite's shape
    }
    float flag = (outlineFlag > 0.5) ? 1.0 : 0.5;
    finalColor = vec4(0.5, 0.5, 1.0, flag);
}
