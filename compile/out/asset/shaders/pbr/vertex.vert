#version 330

// Vertex attributes (Raylib binds these by name from the mesh).
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// Matrices supplied automatically by Raylib's DrawMesh path.
uniform mat4 mvp;        // model-view-projection
uniform mat4 matModel;   // model (world) matrix
uniform mat4 matNormal;  // transpose(inverse(matModel)), for correct normal transform

// Interpolated outputs consumed by the fragment stage.
out vec3 fragPosition;   // world-space surface position
out vec2 fragTexCoord;
out vec3 fragNormal;     // world-space surface normal
out vec4 fragColor;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));

    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
