#version 330

// Normal / mask G-buffer for the postfx outline pass. Only world MODEL objects are drawn through this
// (depth-tested), so the postfx pass can tell real geometry from the sky / grid / shadows, read a per-pixel
// surface normal for edge detection, and honour each object's per-object outline flag.
//
// Output packing (RGBA8):
//   RGB = view-space surface normal, encoded n*0.5 + 0.5 (decoded as rgb*2 - 1 in the postfx pass)
//   A   = surface + outline flag, in three levels:
//           0.0 = not a surface (the cleared value: sky / debug grid / anything not drawn here)
//           0.5 = surface, outlines DISABLED for this object
//           1.0 = surface, outlines ENABLED for this object
//
// The pass is drawn with colour blending DISABLED (see RenderNormalBuffer), so RGB and A are written verbatim;
// with blending on, a fragment alpha of 0.5 would blend the normal with the background and corrupt it.

// 1.0 when this object has outlines enabled, else 0.0 (set per object before each draw).
uniform float outlineFlag;

in vec3 fragNormalView;

out vec4 finalColor;

void main()
{
    vec3 n = normalize(fragNormalView);
    float flag = (outlineFlag > 0.5) ? 1.0 : 0.5; // always a surface here (sky/grid are never drawn into this)
    finalColor = vec4(n*0.5 + 0.5, flag);
}
