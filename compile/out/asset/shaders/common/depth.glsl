// The scene's depth as the camera's passes store it (source/GLDepth.h). Reversed where the GPU has
// clip control: 1 at the near plane, 0 at infinity, a 32-bit float cleared to 0, so the sky (nothing drawn) is exactly 0
// and anything at any finite distance above it. Standard otherwise (raylib's): 0 at the near plane, 1 at its 4 km far
// plane, cleared to 1. Every pass that reads the scene's, the G-buffer's or the crisp overlay's depth takes it through
// these, with the renderer's depthReversed.

uniform float depthReversed;    // 1: reversed depth, 0: standard

// Whether a depth is the sky's: nothing drawn there. Standard depth keeps a hair below 1, clear of the far plane's
// precision.
bool depthIsSky(float depth)
{
    return (depthReversed > 0.5) ? (depth <= 0.0) : (depth >= 0.9999);
}

// A depth as normalized device z, for the inverse projection: as stored with clip control's 0-to-1 depth, mapped back
// from 0-to-1 onto -1-to-1 without.
float depthToNdc(float depth)
{
    return (depthReversed > 0.5) ? depth : depth*2.0 - 1.0;
}

// Whether depth a lies behind depth b, farther from the camera, by more than a tolerance: in standard depth an absolute
// one, in reversed depth (which falls as 1 over the distance) the same share of b.
bool depthIsBehind(float a, float b, float tolerance)
{
    return (depthReversed > 0.5) ? (a < b*(1.0 - tolerance)) : (a > b + tolerance);
}
