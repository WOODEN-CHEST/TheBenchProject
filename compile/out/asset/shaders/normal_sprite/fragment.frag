#version 330

// Normal / mask G-buffer pass for cutout SPRITES. Companion to the model `world_normal` shader: the postfx outline pass
// and the AO pre-pass read the same buffer, so sprites written here get the same silhouette outlines and ambient
// occlusion models do. Decals and blended or additive sprites are not drawn here.
//
// Two differences from the model version:
//   * It discards texels below the sprite's alpha cutoff, so the outline follows the sprite's shape rather than its
//     quad's rectangle.
//   * The quad is two-sided: its view-space normal is turned toward the camera (a billboard faces the camera already).
//
// Output packing matches world_normal exactly: RGB = encoded view normal (n*0.5 + 0.5), A = surface/outline flag
// (0.5 = surface, 0.76..1.0 = surface with outlines, its haze multiplier over that range). Drawn with colour blending
// disabled (WorldRendererGBuffer), so RGB/A are written verbatim; the discard, not blending, handles the sprite's
// transparency.

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform sampler2D texture0;   // the animation frame's texture (its alpha is the sprite's shape)
uniform mat4 matView;         // world -> view (supplied by DrawMesh)
uniform float outlineFlag;    // 1.0 if this sprite has outlines enabled, else 0.0 (set per sprite)
uniform float hazeMultiplier; // its haze multiplier (its placement's haze; set per sprite)
uniform float alphaCutoff;    // the sprite's cutout threshold

out vec4 finalColor;

// An outlined surface's alpha at haze 0 (postfx/fragment.frag decodes it with the same value).
const float OUTLINE_HAZE_BASE = 0.76;

// Shared with the other geometry shaders; the renderer leaves the fade at 0 here (see world_normal).
#include "common/dither"

void main()
{
    if ((texture(texture0, fragTexCoord).a < alphaCutoff) || DitherDiscard())
    {
        discard; // not a surface, so the outline hugs the sprite's shape
    }
    vec3 n = normalize(mat3(matView)*normalize(fragNormal));
    vec3 viewPosition = (matView*vec4(fragPosition, 1.0)).xyz; // the camera sits at the view-space origin
    if (dot(n, -viewPosition) < 0.0)
    {
        n = -n;
    }
    float flag = (outlineFlag > 0.5) ? mix(OUTLINE_HAZE_BASE, 1.0, clamp(hazeMultiplier, 0.0, 1.0)) : 0.5;
    finalColor = vec4(n*0.5 + 0.5, flag);
}
