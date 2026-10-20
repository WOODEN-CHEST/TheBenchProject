#version 330

// Depth-only pass for the sun shadow map. The default vertex stage already writes gl_Position from the
// light's MVP, so the rasterizer records depth automatically; this fragment output is unused (the shadow
// framebuffer is depth-only). A dedicated trivial shader is used so the PBR shader (which SAMPLES the shadow
// map) is never bound while the shadow map is being rendered into. A fading object's depth is dithered on the shadow
// map's texel grid, so its shadow thins out with it.

out vec4 finalColor;

#include "common/dither"

void main()
{
    if (DitherDiscard())
    {
        discard;
    }
    finalColor = vec4(1.0);
}
