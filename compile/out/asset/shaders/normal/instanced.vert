#version 330

// The G-buffer's vertex stage for instanced copies (world_normal_instanced, B03): normal/vertex for every copy of a cell
// at once, each placed by its record and dropped when this draw does not show it (common/instancing). The fragment stage
// is world_normal's own, its dither the copy's (solid in the G-buffer; WORLD_INSTANCED).

#include "common/instancing"

// Vertex attributes (Raylib binds these by name from the mesh).
in vec3 vertexPosition;
in vec3 vertexNormal;

// The cell's matrices (the instanced draw sets them as DrawMesh does).
uniform mat4 mvp;        // model-view-projection of the cell
uniform mat4 matModel;   // the cell's matrix (into render space)
uniform mat4 matNormal;  // transpose(inverse(matModel)): cell -> world normal transform
uniform mat4 matView;    // world -> view (camera) space

// View-space surface normal handed to the fragment stage.
out vec3 fragNormalView;

void main()
{
    if (!Instancing_IsShown(matModel))
    {
        fragNormalView = vec3(0.0, 0.0, 1.0);
        gl_Position = INSTANCE_DROPPED_POSITION;
        return;
    }

    vec3 worldNormal = normalize(vec3(matNormal*vec4(Instancing_TurnNormal(vertexNormal), 0.0)));
    fragNormalView = normalize(vec3(matView*vec4(worldNormal, 0.0)));

    gl_Position = mvp*vec4(Instancing_MoveVertex(vertexPosition), 1.0);
}
