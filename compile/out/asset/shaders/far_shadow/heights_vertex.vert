#version 330

// The far shadows' height maps (world_far_shadow_heights; source/WorldFarShadows.h): a terrain chunk drawn from above into
// one of the nested maps, its model matrix relative to the maps' frame's centre. Each vertex hands on its height in the
// frame: its height above the centre's level, lowered by its drop from the centre (the planet's curve, common/curvature,
// whose scale and clip-space up the pass sets for its own view), so the light's rays stay straight in the maps.

#include "common/curvature"

in vec3 vertexPosition;

uniform mat4 mvp;       // the chunk into the map's clip space
uniform mat4 matModel;  // the chunk relative to the frame's centre

out float fragHeight;

void main()
{
    vec3 position = vec3(matModel*vec4(vertexPosition, 1.0));
    fragHeight = position.y - CurvatureDrop(position);
    gl_Position = CurvedClipPosition(mvp*vec4(vertexPosition, 1.0), position);
}
