#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "AssetManager.h"
#include "Logger.h"
#include "World.h"
#include "WorldCollision.h"


/**
 * @file WorldPathObjects.h
 * @brief The path objects system: it keeps what every path object component generates up to date, once a tick and only when
 *        something changed.
 *
 * WHAT IT MAKES. Under each path object's object it keeps runtime children (WorldObject_SetPersistent false: world files never
 * save them): one per slot and model holding the rigid copies (a model_instances component, drawn as any copies are) and the
 * collision of that slot and model (a collider with placed shapes), and one per slot, model and stretch of about 32 metres
 * holding
 * bent pieces (a model component drawing a generated model, its lights and its culling its own). The children take the settings
 * in effect as ordinary rows, so the renderer, the collision world and the status know nothing of path objects.
 *
 * WHAT IS WATCHED. A rebuild comes when the component's rows change (PathObjectLayout.h, PathObjectPlacement.h), its asset's
 * name,
 * its path (a node, a setting, the path found, lost or replaced: WorldPathWatch), and, for a path object with anything snapped,
 * when the object or the path's object moved, or when the static world changed under its points
 * (WorldCollision_HasGroundChanged; what snaps to terrain alone looks only at terrain). A rebuild writes only what came out
 * different, so the renderer's and the collision world's own rebuilds follow only real changes, and it never loops on its own
 * colliders. A path object whose object is effectively disabled waits until it is enabled.
 *
 * SPACE. What a path object makes lies in its own object's space, as if the path's nodes were given there: a child with no
 * transform of its own lies exactly on its parent's path, and moving it moves everything it made along with it. The ground is
 * looked for along the world's Y, from a point's place in the world.
 *
 * PREFABS. A slot's model may name a prefab: each of its pieces is then a runtime child holding a copy of it (WorldPrefabs.h), its values and
 * picks written here and its objects made by that system. It stands as a rigid piece at an even scale, since an object has no shear.
 *
 * Tick order: after the transform drivers, before WorldCollision_Step (whose sync takes in the new colliders). At load, after a
 * first WorldCollision_Sync, so the first frame stands on the ground; a ground that comes later rebuilds it anyway. Main thread
 * only: the world models load through the asset manager, which needs the GL context outside the tests' code-built models.
 */


// Macros.
/** @brief The most pieces one path object lays out. */
#define WORLD_PATH_OBJECTS_MAX_PIECES ((size_t)262144)
/** @brief What every path object of a world makes together, at most, by default (WorldPathObjectsLimits). */
#define WORLD_PATH_OBJECTS_MAX_TOTAL_PIECES ((size_t)4000000)
#define WORLD_PATH_OBJECTS_MAX_TOTAL_VERTICES ((size_t)32000000)
#define WORLD_PATH_OBJECTS_MAX_TOTAL_SHAPES ((size_t)4000000)
#define WORLD_PATH_OBJECTS_MAX_TOTAL_CHILDREN ((size_t)65536)
/** @brief Warnings all of a system's path objects give together: past them it stays quiet, and says so once. */
#define WORLD_PATH_OBJECTS_MAX_WARNINGS ((size_t)1024)
/** @brief The most vertices one generated mesh holds. */
#define WORLD_PATH_OBJECTS_MAX_MESH_VERTICES ((size_t)1048576)
/** @brief The most vertices every generated mesh of one rebuild holds together. */
#define WORLD_PATH_OBJECTS_MAX_BUILD_VERTICES ((size_t)8388608)
/** @brief The most placed collision shapes a child holds. */
#define WORLD_PATH_OBJECTS_MAX_PLACED_SHAPES ((size_t)65536)
/** @brief The most points of a curve one rebuild's bent pieces look at, samples and slices together: past it, the rest stay
 * unbent. */
#define WORLD_PATH_OBJECTS_MAX_BENT_WORK ((size_t)8000000)
/** @brief The most samples one section gives a bent piece: a tinier spacing is coarsened to it. */
#define WORLD_PATH_OBJECTS_MAX_SECTION_SAMPLES ((size_t)65536)
/** @brief The most a rebuild's looks for the ground may cost, as rays times the path object's own placed shapes (what a ray may
 * have to
 *  pass over): past it, the rest stay off the ground. */
#define WORLD_PATH_OBJECTS_MAX_GROUND_WORK ((uint64_t)400000000)
/** @brief The stretch one generated model holds at most, in metres of path: each has its own lights and culling. */
#define WORLD_PATH_OBJECTS_PART_LENGTH 32.0


// Types.
/** @brief The system (opaque). */
typedef struct WorldPathObjectsStruct WorldPathObjects;

/** @brief The most every path object of a world makes together: past it, more of them make less or nothing (logged once per path
 *  object). Limits for hostile worlds, whose small files could otherwise ask for any amount of memory: a town's fences and
 * hedges hold
 *  far less. */
typedef struct WorldPathObjectsLimitsStruct
{
    /** @brief Pieces laid out. */
    size_t MaxPieces;
    /** @brief Vertices of generated meshes, over every LOD. */
    size_t MaxVertices;
    /** @brief Placed collision shapes. */
    size_t MaxPlacedShapes;
    /** @brief Runtime children. */
    size_t MaxChildren;
} WorldPathObjectsLimits;

/** @brief What the system holds now, and how much it did. */
typedef struct WorldPathObjectsStatsStruct
{
    /** @brief The path object components it follows. */
    size_t ComponentCount;
    /** @brief The pieces laid out now, over all of them. */
    size_t PieceCount;
    /** @brief The rigid copies and the placed collision shapes they hold. */
    size_t CopyCount;
    size_t PlacedShapeCount;
    /** @brief The generated models (each ~32 m of bent pieces), and their triangles at LOD 0. */
    size_t GeneratedModelCount;
    size_t GeneratedTriangleCount;
    /** @brief The runtime children it keeps. */
    size_t ChildCount;
    /** @brief How many rebuilds it ran, and how many of them changed nothing. */
    uint64_t RebuildCount;
    uint64_t UnchangedRebuildCount;
    /** @brief How many times a child's copies, generated model or placed shapes were written. */
    uint64_t WriteCount;
} WorldPathObjectsStats;


// Functions.
/**
 * @brief Creates the system over a world.
 * @param world The world; must outlive the system. Must not be NULL.
 * @param collision The collision world the ground is looked for in; must outlive the system. NULL: nothing snaps.
 * @param assets Loads the path objects and their world models, under the system's own asset user. Must not be NULL.
 * @param logger Receives one warning per path object for each thing that cannot be done (a missing asset, a limit); may be NULL.
 * @param outSystem [out] Receives the system. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments.
 */
Error WorldPathObjects_Create(World* world, WorldCollision* collision, AssetManager* assets, Logger* logger,
    WorldPathObjects** outSystem);

/**
 * @brief Releases the system and removes what it made from the world, syncing the collision world so none of it holds on: their
 *        collision borrows from the world models the system holds, which go with it. Called before the world and the collision
 * world go.
 * @param self The system, or NULL.
 */
void WorldPathObjects_Destroy(WorldPathObjects* self);

/**
 * @brief Brings everything up to date: once a tick. Costs a few integer comparisons per path object while nothing changed.
 * @param self The system; must not be NULL.
 */
void WorldPathObjects_Update(WorldPathObjects* self);

/**
 * @brief The default limits.
 * @returns WORLD_PATH_OBJECTS_MAX_TOTAL_PIECES and its kin.
 */
static inline WorldPathObjectsLimits WorldPathObjectsLimits_CreateDefault(void)
{
    return (WorldPathObjectsLimits){ .MaxPieces = WORLD_PATH_OBJECTS_MAX_TOTAL_PIECES,
        .MaxVertices = WORLD_PATH_OBJECTS_MAX_TOTAL_VERTICES,
        .MaxPlacedShapes = WORLD_PATH_OBJECTS_MAX_TOTAL_SHAPES, .MaxChildren = WORLD_PATH_OBJECTS_MAX_TOTAL_CHILDREN };
}

/**
 * @brief Sets what every path object of the world may make together. Later rebuilds keep to it; what is made already stays
 * until it is
 *        rebuilt, and what was left short stays so until a change rebuilds it.
 * @param self The system; must not be NULL.
 * @param limits The limits; must not be NULL.
 */
/**
 * @brief Lets go of every asset it holds and builds every path object again from the definitions as they are now, at its next update: what a
 *        changed path object says, and the picks of a changed prefab's pieces. What comes out the same stays.
 * @param self The system; must not be NULL.
 */
void WorldPathObjects_Rebuild(WorldPathObjects* self);

void WorldPathObjects_SetLimits(WorldPathObjects* self, const WorldPathObjectsLimits* limits);

/**
 * @brief What the system holds now.
 * @param self The system; must not be NULL.
 * @returns The stats.
 */
WorldPathObjectsStats WorldPathObjects_GetStats(const WorldPathObjects* self);
