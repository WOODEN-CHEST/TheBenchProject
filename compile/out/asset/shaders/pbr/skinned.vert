#version 330

// The PBR shader's vertex stage for skinned meshes (world_pbr_skinned, T29): pbr/vertex with the position and the normal
// moved by the object's pose first (common/skinning). The fragment stage is world_pbr's own.

#include "common/skinning"

// Vertex attributes (Raylib binds these by name from the mesh).
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// Matrices supplied automatically by Raylib's DrawMesh path.
uniform mat4 mvp;        // model-view-projection
uniform mat4 matModel;   // model matrix (into render space: the world's orientation, the camera at the origin)
uniform mat4 matNormal;  // transpose(inverse(matModel)), for correct normal transform

// Interpolated outputs consumed by the fragment stage.
out vec3 fragPosition;   // render-space surface position (relative to the camera)
out vec2 fragTexCoord;
out vec3 fragNormal;     // world-space surface normal
out vec4 fragColor;

void main()
{
    vec4 row0;
    vec4 row1;
    vec4 row2;
    Skinning_GetRows(row0, row1, row2);
    vec3 position = Skinning_MovePoint(row0, row1, row2, vertexPosition);
    vec3 normal = Skinning_TurnVector(row0, row1, row2, vertexNormal);

    fragPosition = vec3(matModel*vec4(position, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(normal, 0.0)));

    gl_Position = mvp*vec4(position, 1.0);
}
