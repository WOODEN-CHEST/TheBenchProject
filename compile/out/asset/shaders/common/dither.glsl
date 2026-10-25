// The dithered fades of the world's geometry shaders, for #include "common/dither". A pixel is drawn only where two masks
// keep it, both from one 4x4 ordered (Bayer) pattern on the scene's pixel grid, so a fading or half-transparent object
// looks like pixel-art dithering and keeps its depth and its shadow (the shadow passes dither too); nothing needs sorting
// or blending:
// - the draw's fade (ditherFade: its draw distance, its opacity) from 0 (solid) to 1 (gone): the draw keeps the pixels
//   whose threshold lies above it;
// - the LOD cross-fade (T11): while an object swaps LODs both are drawn, and the incoming LOD takes the share of the
//   pixels given by the fade's progress (ditherLodProgress): it keeps the pixels whose threshold is at or below the
//   progress (ditherIsIncomingLod), the outgoing LOD the others. The two compare the same value, so every pixel is drawn
//   by exactly one of them; the draw's fade then removes its pixels from both alike. A LOD drawn alone has progress 0
//   and is not the incoming one: it keeps every pixel.
//
// The formula (agents/WorldRuntime.md, "Visibility"; source/WorldVisibility.c is its C mirror for the tests):
//   cell      = floor(gl_FragCoord.xy / ditherPixelScale), each coordinate mod 4 (x from the left, y from the bottom row
//               of the target)
//   threshold = (DITHER_BAYER[cell.y*4 + cell.x] + 0.5) / 16
//   keptByFade = threshold > fade
//   keptByLod  = isIncomingLod ? (threshold <= lodProgress) : (threshold > lodProgress)
//   discard    = !(keptByFade && keptByLod)
// A fade of 0 keeps every pixel, 1 none, and k/16 keeps 16 - k pixels of every 4x4 block; a LOD progress of k/16 gives
// the incoming LOD k pixels of every block and the outgoing one the other 16 - k.
//
// Test it last in a shader that takes derivatives or samples mipmapped textures (after those), so they stay defined for
// the pixels that are drawn.
//
// The instanced twins (B03: their definitions define WORLD_INSTANCED) take the fade and the LOD band per copy from their
// vertex stage (common/instancing) as flat varyings instead of the uniforms: the same formula with values from two
// sources.

#ifdef WORLD_INSTANCED
flat in float fragDitherFade;          // the copy's fade: 0 solid .. 1 gone
flat in float fragDitherLodProgress;   // its LOD band's progress: the share of the pixels the incoming LOD takes
flat in float fragDitherIsIncomingLod; // 1 when this draw is the copy's incoming LOD, 0 otherwise (no bool varyings)
#else
uniform float ditherFade;         // this draw's fade (its draw distance, its opacity): 0 solid .. 1 gone
uniform float ditherLodProgress;  // its LOD cross-fade's progress: the share of the pixels the incoming LOD takes
uniform bool ditherIsIncomingLod; // whether this draw is the incoming LOD (it keeps the pixels the outgoing one leaves)
#endif
uniform vec2 ditherPixelScale;    // this pass's pixels per scene pixel: 1 at the scene's resolution (and in the shadow
                                  // maps, on their texel grid), window / scene in the crisp overlay

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

// Whether this fragment is discarded for a fade and a part of a LOD cross-fade: either removes it.
bool DitherDiscardValues(float fade, float lodProgress, bool isIncomingLod)
{
    float threshold = DitherThreshold();
    bool isKeptByFade = threshold > fade;
    bool isKeptByLod = isIncomingLod ? (threshold <= lodProgress) : (threshold > lodProgress);
    return !(isKeptByFade && isKeptByLod);
}

// Whether this fragment is discarded: the draw's fade or its part of a LOD cross-fade removes it (in an instanced twin,
// its copy's).
bool DitherDiscard()
{
#ifdef WORLD_INSTANCED
    return DitherDiscardValues(fragDitherFade, fragDitherLodProgress, fragDitherIsIncomingLod > 0.5);
#else
    return DitherDiscardValues(ditherFade, ditherLodProgress, ditherIsIncomingLod);
#endif
}
