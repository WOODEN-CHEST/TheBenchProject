#version 330

// Vertex stage for the point-light OMNIDIRECTIONAL shadow pass. Renders model geometry from a point light's
// position into one face of a cube map; the fragment stores the linear distance from the light. Raylib's
// DrawMesh supplies mvp (per face: light-view * face-projection * model) and matModel (model -> world).

in vec3 vertexPosition;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragWorldPos;

void main()
{
    fragWorldPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
