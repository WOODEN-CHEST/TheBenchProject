#version 330

// The star's radiance, added onto the sky (WorldRendererStars.c draws with (ONE, ONE) blending). Alpha 0 leaves the
// scene's alpha, which the tonemap passes on to the frame target, untouched.

flat in vec3 fragRadiance;

out vec4 finalColor;

void main()
{
    finalColor = vec4(fragRadiance, 0.0);
}
