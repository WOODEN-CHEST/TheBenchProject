// "common/curvature": the planet's curvature in the camera's passes, for #include in their vertex stages (the lit,
// G-buffer and water programs, the terrain, the sprites, the rain) and in what reads their depth back.
//
// The world is flat: its positions, its collision, its lighting and its shadow maps all are. Only what the camera sees
// curves with the planet: every vertex the camera's passes draw drops by d^2 / 2R below where it stands, d its
// horizontal distance from the camera and R the planet's radius, so flat ground meets the far ground at the horizon at
// any distance, and the sea bends down to the true horizon. The drop is applied to the clip position alone
// (CurvedClipPosition: the clip position less the drop times the pass's clip-space image of the world's up), so the
// fragment's render-space position (fragPosition), with which it is lit, shadowed and hazed, stays the flat world's:
// it is exactly the point it would be without the curve. A position read back from the camera's depth is the dropped one;
// CurvatureFlatten gives the flat point back (its horizontal place is the same). The C mirror is
// WorldRenderSpace_GetCurvatureDrop (source/WorldRenderSpace.h).
//
// curvatureScale is 1 / (2 R) while the world curves (its environment's is_curvature_enabled) and 0 otherwise, which
// leaves every clip position exactly as it was. A program that never sets it (the shadow passes' sprite program shares
// the sprites' vertex stage) keeps its link-time 0: the shadow maps hold the flat world.

uniform float curvatureScale;  // 1 / (2 R): metres of drop per square metre of horizontal distance; 0 for a flat picture
uniform vec4 curvatureClipUp;  // the pass's view-projection of the world's up direction (w = 0)

// How far a render-space point (the camera at the origin) drops below where it stands, in metres.
float CurvatureDrop(vec3 position)
{
    return dot(position.xz, position.xz)*curvatureScale;
}

// A vertex's clip position, dropped: clip is the pass's projection of the render-space position.
vec4 CurvedClipPosition(vec4 clip, vec3 position)
{
    return clip - CurvatureDrop(position)*curvatureClipUp;
}

// The flat render-space point a dropped one was drawn for (its horizontal place is unchanged by the drop).
vec3 CurvatureFlatten(vec3 dropped)
{
    return vec3(dropped.x, dropped.y + CurvatureDrop(dropped), dropped.z);
}
