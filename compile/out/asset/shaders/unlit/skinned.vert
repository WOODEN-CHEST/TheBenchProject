#version 330

// The unlit fallback's vertex stage for skinned meshes (world_unlit_skinned, T29). Without the PBR shader the renderer
// draws models with their materials' own shaders (raylib's default); a skinned mesh draws with this one instead, which
// shades as raylib's default does (unlit/fragment) after moving the position by the object's pose (common/skinning).

#include "common/skinning"

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    vec4 row0;
    vec4 row1;
    vec4 row2;
    Skinning_GetRows(row0, row1, row2);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(Skinning_MovePoint(row0, row1, row2, vertexPosition), 1.0);
}
