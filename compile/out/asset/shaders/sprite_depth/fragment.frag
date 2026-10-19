#version 330

// Depth-only pass of cutout sprites for the sun's and the moon's shadow cascades: the rasterizer records the depth of
// the sprite as the light sees it (the renderer turns billboards toward the light), and texels below the sprite's alpha
// cutoff are discarded, so the shadow has the sprite's shape, not its quad's. The colour output is unused (the shadow
// framebuffer is depth-only).

in vec2 fragTexCoord;

uniform sampler2D texture0;   // the animation frame's texture
uniform float alphaCutoff;

out vec4 finalColor;

void main()
{
    if (texture(texture0, fragTexCoord).a < alphaCutoff)
    {
        discard;
    }
    finalColor = vec4(1.0);
}
