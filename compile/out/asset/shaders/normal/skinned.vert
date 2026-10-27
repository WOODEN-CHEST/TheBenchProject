#version 330

// The G-buffer's vertex stage for skinned meshes (world_normal_skinned): normal/vertex with the position and the
// normal moved by the object's pose first (common/skinning). The fragment stage is world_normal's own.

#include "common/skinning"

// Vertex attributes (Raylib binds these by name from the mesh).
in vec3 vertexPosition;
in vec3 vertexNormal;

// Matrices supplied automatically by Raylib's DrawMesh path.
uniform mat4 mvp;        // model-view-projection
uniform mat4 matNormal;  // transpose(inverse(matModel)): model -> world normal transform
uniform mat4 matView;    // world -> view (camera) space

// View-space surface normal handed to the fragment stage.
out vec3 fragNormalView;

void main()
{
    vec4 row0;
    vec4 row1;
    vec4 row2;
    Skinning_GetRows(row0, row1, row2);
    vec3 position = Skinning_MovePoint(row0, row1, row2, vertexPosition);
    vec3 normal = Skinning_TurnVector(row0, row1, row2, vertexNormal);

    vec3 worldNormal = normalize(vec3(matNormal*vec4(normal, 0.0)));
    fragNormalView = normalize(vec3(matView*vec4(worldNormal, 0.0)));

    gl_Position = mvp*vec4(position, 1.0);
}
