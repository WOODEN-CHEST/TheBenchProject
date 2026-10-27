#version 330

// Vertex stage of the point-light cube shadow for instanced copies (world_cube_depth_instanced): cube_depth/vertex
// over every copy of a cell at once, each placed by its record and dropped when this draw does not show it
// (common/instancing). The fragment stage is world_cube_depth's own, its dither the copy's (WORLD_INSTANCED).

#include "common/instancing"

in vec3 vertexPosition;

uniform mat4 mvp;       // per face: light-view * face-projection * the cell's matrix
uniform mat4 matModel;  // the cell's matrix (into render space, where the light's position is)

out vec3 fragWorldPos;

void main()
{
    if (!Instancing_IsShown(matModel))
    {
        fragWorldPos = vec3(0.0);
        gl_Position = INSTANCE_DROPPED_POSITION;
        return;
    }
    vec3 position = Instancing_MoveVertex(vertexPosition);
    fragWorldPos = (matModel * vec4(position, 1.0)).xyz;
    gl_Position = mvp * vec4(position, 1.0);
}
