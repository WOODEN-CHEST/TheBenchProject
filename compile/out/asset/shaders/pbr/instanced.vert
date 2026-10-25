#version 330

// The PBR shader's vertex stage for instanced copies (world_pbr_instanced, B03): pbr/vertex for every copy of a cell at
// once, each placed by its record and its colour tinted by it, and dropped when this draw does not show it
// (common/instancing). The fragment stage is world_pbr's own, its dither the copy's (WORLD_INSTANCED).

#include "common/instancing"

// Vertex attributes (Raylib binds these by name from the mesh).
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// The cell's matrices (the instanced draw sets them as DrawMesh does).
uniform mat4 mvp;        // model-view-projection of the cell
uniform mat4 matModel;   // the cell's matrix (into render space: the world's orientation, the camera at the origin)
uniform mat4 matNormal;  // transpose(inverse(matModel)), for correct normal transform

// Interpolated outputs consumed by the fragment stage.
out vec3 fragPosition;   // render-space surface position (relative to the camera)
out vec2 fragTexCoord;
out vec3 fragNormal;     // world-space surface normal
out vec4 fragColor;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor*vec4(instanceTint.rgb, 1.0);
    if (!Instancing_IsShown(matModel))
    {
        fragPosition = vec3(0.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        gl_Position = INSTANCE_DROPPED_POSITION;
        return;
    }

    vec3 position = Instancing_MoveVertex(vertexPosition);
    fragPosition = vec3(matModel*vec4(position, 1.0));
    fragNormal = normalize(vec3(matNormal*vec4(Instancing_TurnNormal(vertexNormal), 0.0)));

    gl_Position = mvp*vec4(position, 1.0);
}
