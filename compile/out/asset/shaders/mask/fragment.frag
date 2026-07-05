#version 330

// Object mask for the post pass. Only world MODEL objects are drawn through this (depth-tested), so the post
// pass can tell real geometry from the sky / grid / shadows and honour each object's per-object outline flag.
// The default vertex stage already writes gl_Position, so this fragment just tags the pixel:
//   R = outline flag (1 when this object has outlines enabled, else 0)
//   G = surface flag (always 1 here: "this pixel is a solid world object", used to gate ambient occlusion)
// Everything the mask pass does NOT draw (sky, debug grid) stays at the cleared 0, so it is neither outlined
// nor ambient-occluded.

uniform float outlineFlag;

out vec4 finalColor;

void main()
{
    finalColor = vec4(outlineFlag, 1.0, 0.0, 1.0);
}
