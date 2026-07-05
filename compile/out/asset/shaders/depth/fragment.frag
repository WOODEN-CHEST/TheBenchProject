#version 330

// Depth-only pass for the sun shadow map. The default vertex stage already writes gl_Position from the
// light's MVP, so the rasterizer records depth automatically; this fragment output is unused (the shadow
// framebuffer is depth-only). A dedicated trivial shader is used so the PBR shader (which SAMPLES the shadow
// map) is never bound while the shadow map is being rendered into.

out vec4 finalColor;

void main()
{
    finalColor = vec4(1.0);
}
