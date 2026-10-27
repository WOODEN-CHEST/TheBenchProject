#version 330

// The sun and moon shadows' vertex stage for instanced copies (world_depth_instanced): the light's MVP over every
// copy of a cell at once, each placed by its record and dropped when this draw does not show it (common/instancing), as
// raylib's default vertex stage does for world_depth. The fragment stage is world_depth's own, its dither the copy's
// (WORLD_INSTANCED).

#include "common/instancing"

in vec3 vertexPosition;

uniform mat4 mvp;       // the light's model-view-projection of the cell
uniform mat4 matModel;  // the cell's matrix (into render space): the copies' distances are the camera's

void main()
{
    if (!Instancing_IsShown(matModel))
    {
        gl_Position = INSTANCE_DROPPED_POSITION;
        return;
    }
    gl_Position = mvp*vec4(Instancing_MoveVertex(vertexPosition), 1.0);
}
