// The dithered fade of the world's geometry shaders, for #include "common/dither". A draw's fade runs from 0 (solid) to
// 1 (gone): the draw keeps a matching share of its pixels, chosen by a 4x4 ordered (Bayer) pattern on the scene's pixel
// grid. A fading or half-transparent object then looks like pixel-art dithering and keeps its depth and its shadow (the
// shadow passes dither too); nothing needs sorting or blending. The renderer uses it for the draw-distance fade and the
// object's opacity; T11 draws two LODs at once, the outgoing one at fade p and the incoming one at fade 1 - p with the
// mirrored pattern (ditherInvert), so every pixel is drawn by exactly one of them.
//
// The formula (agents/WorldRuntime.md, "Visibility"; source/WorldVisibility.c is its C mirror for the tests):
//   cell      = floor(gl_FragCoord.xy / ditherPixelScale), each coordinate mod 4 (x from the left, y from the bottom row
//               of the target)
//   threshold = (DITHER_BAYER[cell.y*4 + cell.x] + 0.5) / 16
//   covered   = invert ? (1 - threshold >= fade) : (threshold > fade)
//   discard   = !covered
// Either way a fade of 0 keeps every pixel, 1 none, and k/16 keeps 16 - k pixels of every 4x4 block; a draw at fade p and
// an inverted one at fade 1 - p keep exactly complementary pixels.
//
// Test it last in a shader that takes derivatives or samples mipmapped textures (after those), so they stay defined for
// the pixels that are drawn.

uniform float ditherFade;      // this draw's fade: 0 solid .. 1 gone
uniform bool ditherInvert;     // the mirrored pattern (the incoming LOD of a cross-fade, at 1 - the outgoing's fade)
uniform vec2 ditherPixelScale; // this pass's pixels per scene pixel: 1 at the scene's resolution (and in the shadow maps,
                               // on their texel grid), window / scene in the crisp overlay

// The 4x4 Bayer matrix, row by row, the first row at the target's bottom.
const float DITHER_BAYER[16] = float[16](
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0);

// The pattern's threshold at this fragment: one of (k + 0.5) / 16, k = 0..15.
float DitherThreshold()
{
    ivec2 cell = ivec2(floor(gl_FragCoord.xy/max(ditherPixelScale, vec2(1.0)))) % 4;
    return (DITHER_BAYER[cell.y*4 + cell.x] + 0.5)/16.0;
}

// Whether this fragment is discarded for a fade, with the normal or the mirrored pattern.
bool DitherDiscard(float fade, bool invert)
{
    float threshold = DitherThreshold();
    bool isCovered = invert ? ((1.0 - threshold) >= fade) : (threshold > fade);
    return !isCovered;
}
