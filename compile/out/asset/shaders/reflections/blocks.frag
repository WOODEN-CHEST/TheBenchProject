#version 330

// The reflection pass's blocks (world_reflection_blocks, source/WorldRendererReflections.c): for each block of
// REFLECTION_BLOCK_SIZE x REFLECTION_BLOCK_SIZE scene pixels (one pixel of this target), the depth of the nearest surface
// in it as stored (the largest reversed, the smallest standard), over its pixels on the screen; the sky's depth where it
// shows only the sky. The march skips a block whose nearest surface lies behind the ray all through it
// (WorldReflections_GetBlockNearest is its C mirror).

uniform sampler2D sceneDepth; // the scene's depth (common/depth's convention), on its unit
uniform ivec2 sceneSize;      // the scene's pixels

out vec4 finalColor;

#include "common/depth"

// WorldReflections.h's WORLD_REFLECTIONS_BLOCK_SIZE (tests/ShaderSourceTests.c pins it).
const int REFLECTION_BLOCK_SIZE = 4;

void main()
{
    ivec2 first = ivec2(gl_FragCoord.xy)*REFLECTION_BLOCK_SIZE;
    bool isReversed = depthReversed > 0.5;
    float nearest = isReversed ? 0.0 : 1.0;
    for (int row = 0; row < REFLECTION_BLOCK_SIZE; row++)
    {
        for (int column = 0; column < REFLECTION_BLOCK_SIZE; column++)
        {
            ivec2 pixel = first + ivec2(column, row);
            if ((pixel.x >= sceneSize.x) || (pixel.y >= sceneSize.y))
            {
                continue;
            }
            float depth = texelFetch(sceneDepth, pixel, 0).r;
            nearest = isReversed ? max(nearest, depth) : min(nearest, depth);
        }
    }
    finalColor = vec4(nearest, 0.0, 0.0, 1.0);
}
