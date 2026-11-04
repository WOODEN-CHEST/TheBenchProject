#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"
#include "CollisionLayer.h"
#include "WorldCollision.h"


/**
 * @file PathGround.h
 * @brief Ground projection: the one function every generator shares to find the ground under a point of its path.
 *
 * A straight ray down the world's Y from a little above the point: from the point's own height plus a search height, not
 * from the sky, so a path in a tunnel finds the tunnel's floor and not the hill above it. It goes through the collision
 * world, onto terrain only or onto chosen collision layers (a railing on a bridge deck), so what it can find is what
 * collides: a terrain with its collision off is not found, a one-sided terrain is met only from above, and the collision
 * world must have synced since the ground last changed (WorldCollision_Sync; once a tick in play). A surface the ray
 * starts inside does not count: a point under a thick deck passes down through it.
 *
 * Whether a reader snaps at all is its own setting and the section's (PathCurveSection_ResolveSnapsToTerrain); this is only
 * the question "where is the ground here". When it finds nothing the reader keeps the point's own height.
 */


// Macros.
/** @brief PathGroundSettings_CreateDefault's search height, in metres: low enough for a tunnel a few metres under a hill. */
#define PATH_GROUND_DEFAULT_SEARCH_HEIGHT 5.0f
/** @brief PathGroundSettings_CreateDefault's search depth, in metres. */
#define PATH_GROUND_DEFAULT_SEARCH_DEPTH 1000.0f


// Types.
/**
 * @brief What counts as the ground. Saved by the assets that hold these settings: values are explicit and never renumbered.
 */
typedef enum PathGroundTargetEnum
{
    /** @brief Terrain only, on whichever layer it collides. */
    PathGroundTarget_Terrain = 0,
    /** @brief Anything solid on the settings' layers. */
    PathGroundTarget_Layers = 1
} PathGroundTarget;

/**
 * @brief How to look for the ground. Build with PathGroundSettings_CreateDefault.
 */
typedef struct PathGroundSettingsStruct
{
    /** @brief What counts as the ground. */
    PathGroundTarget Target;
    /** @brief The layers that count, for PathGroundTarget_Layers (COLLISION_LAYER_BIT per layer). */
    uint32_t LayerMask;
    /** @brief How far above the point the ray starts, in metres (at least 0). */
    float SearchHeight;
    /** @brief How far below the point it looks, in metres (at least 0). */
    float SearchDepth;
    /** @brief How far above the ground the result lies, in metres (negative sinks it), along the world's Y. */
    float Offset;
    /** @brief An object the ray passes through with everything under it, 0 for none: a reader's own colliders, which must
     *  not lift what they were made from. */
    uint64_t IgnoredObjectId;
} PathGroundSettings;

/**
 * @brief Where the ground was found. A plain value type.
 */
typedef struct PathGroundHitStruct
{
    /** @brief The point on the surface, in the world. */
    Vector3Double Ground;
    /** @brief Ground raised by the offset: where the reader's point goes. */
    Vector3Double Position;
    /** @brief The surface's unit normal there. */
    Vector3 Normal;
    /** @brief The shape hit: its object and, on a terrain, its chunk and triangle. */
    WorldCollisionShapeInfo Shape;
} PathGroundHit;


// Functions.
/**
 * @brief Settings with the defaults: terrain only, 5 m above, 1 km below, no offset, nothing ignored.
 * @returns The settings.
 */
static inline PathGroundSettings PathGroundSettings_CreateDefault(void)
{
    return (PathGroundSettings){ .Target = PathGroundTarget_Terrain, .LayerMask = COLLISION_LAYER_MASK_ALL,
        .SearchHeight = PATH_GROUND_DEFAULT_SEARCH_HEIGHT, .SearchDepth = PATH_GROUND_DEFAULT_SEARCH_DEPTH, .Offset = 0.0f,
        .IgnoredObjectId = 0U };
}

/**
 * @brief Finds the ground under a point.
 *
 * Allocates only in the rare case that the ray starts inside something solid.
 * @param collision The collision world; must not be NULL. Queries see it as of its last sync.
 * @param point The point in the world, in double.
 * @param settings How to look. Must not be NULL.
 * @param outHit [out] Receives where. Must not be NULL.
 * @returns true when ground was found; false otherwise (also for a search of no length), leaving @p outHit alone.
 */
bool PathGround_Project(WorldCollision* collision, Vector3Double point, const PathGroundSettings* settings,
    PathGroundHit* outHit);
