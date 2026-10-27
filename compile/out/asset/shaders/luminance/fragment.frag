#version 330

// The eye's meter (source/EyeAdaptation.h): one texel per block of the linear-HDR scene, the mean log2 luminance
// of a SAMPLES x SAMPLES grid of the block's pixels, each held within the meter's window around the sky light's anchor,
// so the sun's disc, a lamp's bulb or a black corner count no further than its edges; the sky (no geometry: the far
// depth) counts as at least the night's own light, the natural night sky's glow the game lights surfaces with but does
// not draw. The renderer reads the blocks back a frame
// or two later (source/GLReadback.h) and weighs them toward the middle of the view. Samples by gl_FragCoord (a texel of
// this target is a block) and texelFetch (the scene's nearest pixels), so the draw's orientation and filtering do not
// matter.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   // the linear-HDR scene, as the tonemap reads it
uniform vec3 window;          // the lowest log2 luminance a surface's pixel counts as, the sky's, the highest any pixel's
// The scene's depth, to tell the sky apart (depthMask > 0.5; without a samplable depth every pixel is a surface's).
uniform sampler2D depthTexture;
uniform float depthMask;

out vec4 finalColor;

// EYE_ADAPTATION_METER_WIDTH and _HEIGHT (the blocks), and EYE_ADAPTATION_METER_SAMPLES (a block's samples a side).
const int METER_WIDTH = 64;
const int METER_HEIGHT = 36;
const int SAMPLES = 8;
// The sky sits at the cleared depth, every surface nearer (common/depth, as bloom_prefilter and sunshafts tell them apart).
#include "common/depth"

void main()
{
    ivec2 block = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(texture0, 0);
    // The block's pixels: the scene split evenly, every pixel in one block.
    vec2 meter = vec2(METER_WIDTH, METER_HEIGHT);
    vec2 start = vec2(block * size) / meter;
    vec2 extent = vec2((block + 1) * size) / meter - start;
    float sum = 0.0;
    for (int y = 0; y < SAMPLES; y++)
    {
        for (int x = 0; x < SAMPLES; x++)
        {
            ivec2 pixel = min(ivec2(start + (vec2(x, y) + 0.5) * extent / float(SAMPLES)), size - 1);
            float luminance = dot(texelFetch(texture0, pixel, 0).rgb, vec3(0.2126, 0.7152, 0.0722));
            bool isSky = (depthMask > 0.5) && depthIsSky(texelFetch(depthTexture, pixel, 0).r);
            float low = isSky ? window.y : window.x;
            // log2 is undefined at and below 0: such a pixel (and NaN, which fails the test) counts as the low edge; an
            // infinity clamps to the high one.
            sum += (luminance > 0.0) ? clamp(log2(luminance), low, window.z) : low;
        }
    }
    finalColor = vec4(sum / float(SAMPLES * SAMPLES), 0.0, 0.0, 1.0);
}
