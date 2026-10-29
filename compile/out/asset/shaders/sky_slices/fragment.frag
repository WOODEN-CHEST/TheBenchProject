#version 330

// The sky's occupancy volume (interior lighting; common/interior reads it): everything that keeps the sky out, drawn
// from above onto the map's grid with no depth test, both faces, and a logic op, into an integer target whose 128 bits
// are height slices (slice k: word k / 32's bit k % 32, from the window's bottom up). sliceMode 0, drawn with XOR,
// toggles the slices whose centre lies below the fragment, so a slice ends up solid where an odd number of surfaces lie
// above it: inside walls under a roof, not in the air under it. sliceMode 1, drawn with OR over that, sets the slice the
// fragment passes through, so a slab thinner than a slice is not lost (the top slice for a fragment above the window, so
// nothing above it is lost either: a column solid at the window's top goes on up). The C mirror is source/WorldInterior.c
// (WorldInterior_GetSliceParityMask, WorldInterior_GetSlicePresenceMask). A fading object dithers as in the other passes,
// the same at a texel on all its faces, so its columns stay whole.

uniform float mapEyeHeight; // render-space height of the map's eye, where its depth is 0
uniform float mapDepth;     // its depth range in metres (depth 1 at its bottom)
uniform float volumeBottom; // the window's bottom, render space
uniform int sliceMode;      // 0: the slices whose centre lies below (parity); 1: the slice it passes through (presence)

out uvec4 finalColor;

#include "common/dither"

const int SLICE_COUNT = 128;
const float SLICE_HEIGHT = 0.25;

// The slices from first to last (inclusive, each within the column) as a mask.
uvec4 SliceRun(int first, int last)
{
    uvec4 mask = uvec4(0u);
    for (int word = 0; word < 4; word++)
    {
        int low = max(first - 32*word, 0);
        int high = min(last - 32*word, 31);
        if (low <= high)
        {
            uint count = uint(high - low + 1);
            uint bits = (count == 32u) ? 0xFFFFFFFFu : ((1u << count) - 1u);
            mask[word] = bits << uint(low);
        }
    }
    return mask;
}

void main()
{
    if (DitherDiscard())
    {
        discard;
    }
    // The map looks straight down with an orthographic depth: linear in height.
    float slice = (mapEyeHeight - gl_FragCoord.z*mapDepth - volumeBottom)/SLICE_HEIGHT;
    uvec4 mask = uvec4(0u);
    if (sliceMode == 0)
    {
        float below = slice - 0.5;
        int count = (below > 0.0) ? ((below < float(SLICE_COUNT)) ? int(ceil(below)) : SLICE_COUNT) : 0;
        mask = (count > 0) ? SliceRun(0, count - 1) : uvec4(0u);
    }
    else if (slice >= 0.0)
    {
        // Above the window, its top slice: what lies above keeps the sky out of the column too.
        int index = (slice < float(SLICE_COUNT)) ? int(floor(slice)) : SLICE_COUNT - 1;
        mask = SliceRun(index, index);
    }
    finalColor = mask;
}
