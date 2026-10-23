#version 330

// The unlit fallback's fragment stage (world_unlit_skinned, T29): raylib's default fragment shader, the material's
// albedo texture times its colour times the vertex colour, so a skinned mesh looks as the rigid ones drawn with
// raylib's default shader do.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    finalColor = texture(texture0, fragTexCoord)*colDiffuse*fragColor;
}
