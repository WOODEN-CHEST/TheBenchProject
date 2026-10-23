#version 330

// The sun and moon shadows' vertex stage for skinned meshes (world_depth_skinned, T29): the position moved by the
// object's pose (common/skinning), then the light's MVP, as raylib's default vertex stage does for world_depth. The
// fragment stage is world_depth's own.

#include "common/skinning"

in vec3 vertexPosition;

uniform mat4 mvp;

void main()
{
    vec4 row0;
    vec4 row1;
    vec4 row2;
    Skinning_GetRows(row0, row1, row2);
    gl_Position = mvp*vec4(Skinning_MovePoint(row0, row1, row2, vertexPosition), 1.0);
}
