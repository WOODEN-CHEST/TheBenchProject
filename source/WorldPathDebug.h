#pragma once
#include <stdbool.h>
#include "WorldCollision.h"
#include "WorldRenderer.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): what the paths' debug view draws, as pure data: a list of lines in the world, in double, for
 * WorldRenderer_SetDebugLines. It reads the world's path components and nothing else, and builds on the update side (it may
 * ask the collision world for the ground), so the renderer only draws a list.
 *
 * Every enabled path gets its curve, coloured by section (a skipped section dim); a cross at each node by its mode (smooth
 * white, corner yellow, bezier cyan) with a bezier node's handles; a tall tick at each node that starts a section; and a
 * closed path's area hatched at its middle height. In detail: its samples as small ticks, and every few metres its frame
 * (tangent blue, up green at the scale's size, right red across the width) with the ground under it as the shared projection
 * finds it (grey, a cross on the ground), or a short violet tick along a section that turns snapping off.
 */


// Macros.
/** The most lines a list holds: past it, later paths lose what does not fit. */
#define WORLD_PATH_DEBUG_MAX_LINES ((size_t)200000)


// Types.
/** What the view shows. */
typedef struct WorldPathDebugOptionsStruct
{
    /** Whether the samples, the frames and the ground are drawn too. */
    bool ShowsDetail;
    /** The collision world the ground ticks ask (it must have synced); NULL for none. */
    WorldCollision* Collision;
} WorldPathDebugOptions;


// Functions.
/** Appends the view's lines to @p outLines, a buffer of WorldDebugLine (not cleared). */
void WorldPathDebug_Build(const World* world, const WorldPathDebugOptions* options, GenericBuffer* outLines);
