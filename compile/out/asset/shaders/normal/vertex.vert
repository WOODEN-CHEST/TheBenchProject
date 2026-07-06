#version 330

// Normal / mask G-buffer pass. Draws world MODEL objects only, writing a low-resolution VIEW-SPACE normal
// buffer that the postfx pass edge-detects for outlines (the three.js RenderPixelatedPass / Godot 3D-pixel-art
// technique). View-space normals are used so a single normal-difference test detects silhouettes and creases
// regardless of world orientation, and so the sun test in the postfx pass (sun direction is uploaded in the
// same view space) is a plain dot product.

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
    vec3 worldNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));
    fragNormalView = normalize(vec3(matView*vec4(worldNormal, 0.0)));

    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
