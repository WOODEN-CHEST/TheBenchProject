#version 330

// The far shadows' height maps (world_far_shadow_heights): the surface's height in the maps' frame. The depth test keeps
// the highest surface over each texel (a cliff's overhang over its foot, a cave's roof over its floor).

in float fragHeight;

out vec4 finalColor;

void main()
{
    finalColor = vec4(fragHeight, 0.0, 0.0, 1.0);
}
