#version 330

// The point-light shadow's vertex stage for skinned meshes (world_cube_depth_skinned): cube_depth/vertex with the
// position moved by the object's pose first (common/skinning). The fragment stage is world_cube_depth's own.

#include "common/skinning"

in vec3 vertexPosition;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragWorldPos;

void main()
{
    vec4 row0;
    vec4 row1;
    vec4 row2;
    Skinning_GetRows(row0, row1, row2);
    vec4 position = vec4(Skinning_MovePoint(row0, row1, row2, vertexPosition), 1.0);
    fragWorldPos = (matModel*position).xyz;
    gl_Position = mvp*position;
}
