#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "AssetManager.h"
#include "DataMap.h"
#include "Logger.h"
#include "PropertyDescriptor.h"
#include "World.h"
#include "WorldComponent.h"
#include "WorldObject.h"


/**
 * Private header (source/) of the world data modules: World (World.c, WorldData.c for tags and data, WorldHierarchy.c
 * for parents and world matrices, WorldQuery.c), WorldObject, WorldComponent and the component types
 * (WorldModelComponent, WorldSpriteComponent, WorldLightComponent). Not public API. It holds what those modules share:
 * the object's construction (objects are made by the world only), the object lookup, the hook every component table
 * calls after a change, the world's data bookkeeping (the tag index and the byte count), the hierarchy's bookkeeping
 * (dirty world matrices, parents from reads, subtrees), the world's assets for components that load some, small
 * helpers, and seams for tests.
 */


// Types.
/** One match of a query being sorted by distance: World_Query keeps them in the world's _queryMatches scratch. */
typedef struct WorldQueryMatchStruct
{
    /** The object's id. */
    uint64_t Id;
    /** Its squared distance from the query's origin, in double (positions are). */
    double DistanceSquared;
} WorldQueryMatch;


// Functions: objects (WorldObject.c).
/**
 * Constructs an object in place for @p world: the table's defaults, the world, the id and a clone of @p name (checked
 * against the name's rules). Pair with WorldObject_Deconstruct. On failure nothing is left to release.
 */
Error WorldObject_Construct(WorldObject* self, World* world, uint64_t id, const unsigned char* name);

/** Releases what the object owns (its name). Its components must have been destroyed first; not the memory itself. */
void WorldObject_Deconstruct(WorldObject* self);

/** Checks a transform against the object table's rules for position (finite, within WORLD_POSITION_LIMIT), rotation
 *  and scale (the first broken one fails, ErrorCode_ArgumentOutOfRange), for code that writes the three fields itself
 *  (World_SetParent). */
Error WorldObject_CheckTransform(Vector3Double position, Vector3 rotation, Vector3 scale);


// Functions: the world (World.c).
/** The object with @p id, or NULL (a hash lookup; 0 finds nothing). */
WorldObject* WorldInternal_FindObject(World* world, uint64_t id);


// Functions: tags and data (WorldData.c).
/** Sets up a new world's data bookkeeping: the empty tag index, the byte count, the query scratch. */
Error WorldInternal_ConstructData(World* world);

/** Releases the tag index and the query scratch; the objects (and their data) must be gone already. */
void WorldInternal_DeconstructData(World* world);

/**
 * After an object's whole data was replaced behind the world's back (a table read or Properties_CopyTo, through the
 * object table's AfterSet): recounts its bytes and indexes its tags again. Logs once when the world's data then passes
 * WORLD_DATA_MAX_BYTES (a loader refuses such a world; World_GetDataByteSize).
 */
void WorldInternal_SyncObjectData(World* world, WorldObject* object);

/** Before an object is removed: takes its tags out of the index and its bytes out of the world's count. */
void WorldInternal_ForgetObjectData(World* world, WorldObject* object);

/** The ids of the objects carrying @p tag, ascending (borrowed: valid until the tag index changes); NULL with a count
 *  of 0 when none do. */
const uint64_t* WorldInternal_GetTagMembers(World* world, uint32_t tag, size_t* outCount);


// Functions: the hierarchy (WorldHierarchy.c).
/**
 * After an object's position, rotation, scale or parent changed: marks its cached world matrix and those of its whole
 * subtree dirty (counted for World_UpdateTransforms) and advances each one's transform revision. Walks the subtree
 * every time (the revisions must move even where the matrices were dirty already).
 */
void WorldInternal_MarkTransformChanged(World* world, WorldObject* object);

/**
 * After an object's pose changed (T30: the animation system posed or cleared its animator): marks the children hanging
 * on its sockets changed (WorldInternal_MarkTransformChanged on each), so their world matrices follow the sockets.
 * Children in the object's own space are left alone. Cheap without such children: one walk over its direct children.
 */
void WorldInternal_MarkSocketChildrenChanged(World* world, WorldObject* object);

/**
 * After a read or a copy wrote an object's parent id behind the world's back (the object table's AfterSet): attaches
 * the object under that parent, keeping its local transform, as World_SetParent would. A parent it cannot take (no such
 * object, the object itself, one of its descendants, or a hierarchy deeper than WORLD_HIERARCHY_MAX_DEPTH) is refused:
 * the object keeps its parent and the id goes back to it, the world counts the refusal (World_GetRefusedParentCount)
 * and logs the first one. The caller marks the transform changed.
 */
void WorldInternal_SyncObjectParent(World* world, WorldObject* object);

/**
 * The first object of a children-first walk of @p root's subtree (every object after all of its descendants, @p root
 * last): its deepest first child. Walk on with WorldInternal_GetNextChildrenFirst; the links must not change meanwhile.
 */
WorldObject* WorldInternal_GetFirstChildrenFirst(WorldObject* root);

/** The object after @p current in a children-first walk of @p root's subtree, or NULL after @p root. */
WorldObject* WorldInternal_GetNextChildrenFirst(WorldObject* current, const WorldObject* root);

/**
 * Appends @p object's subtree to @p outObjects (a buffer of WorldObject*) children first: every object after all of
 * its descendants, @p object last. Nothing is changed. Returns false if the buffer could not grow.
 */
bool WorldInternal_CollectSubtree(WorldObject* object, GenericBuffer* outObjects);

/** Takes @p object out of its parent's children: it becomes a root (its own children stay with it). Its local transform
 *  and its dirty flags are left alone (the caller marks what changed). */
void WorldInternal_DetachFromParent(WorldObject* object);

/** Tests only: how many world matrices World_UpdateTransforms has computed and stored since the world was made. */
static inline uint64_t WorldInternal_GetWorldMatrixRecomputeCount(const World* world)
{
    return world->_worldMatrixRecomputeCount;
}


// Functions: components (World.c).
/**
 * The AfterSet hook of every component table: advances the component's revision, then calls its type's OnPropertySet
 * hook with the world, when the component is attached. A component's own setters call it too, after writing.
 */
void WorldInternal_ComponentAfterSet(void* instance, const PropertyDescriptor* descriptor);

/**
 * Tests only: constructs a world (as World_Construct2) whose component types are @p types instead of the standard
 * registry, so tests can add types of their own (a transform driver before any real one exists). The standard types
 * come first, as their own entries (WorldComponent_GetStandardTypeInfo), then the test types; entry i must have Type i
 * and pass WorldComponentTypeInfo_Check; at most WORLD_MAX_COMPONENT_TYPES entries. The array of pointers is copied,
 * the entries are borrowed and must outlive the world.
 */
Error WorldInternal_ConstructWithTypes(World* self, KeyRegistry* keys, AssetManager* assets, Logger* logger,
    const WorldComponentTypeInfo* const* types, size_t typeCount);


// Functions: the world's assets, for components that load some under the world's user.
/** The world's asset manager (borrowed), or NULL for a world without assets. */
static inline AssetManager* WorldInternal_GetAssets(const World* world)
{
    return world->_assets;
}

/** The asset user the world's components hold their assets under (ASSET_USER_ID_INVALID without assets). */
static inline AssetUserID WorldInternal_GetAssetUser(const World* world)
{
    return world->_assetUser;
}

/** The world's logger (borrowed), or NULL. */
static inline Logger* WorldInternal_GetLogger(const World* world)
{
    return world->_logger;
}


// Functions: helpers.
/** Returns true if @p value is a real, finite float (not NaN and not +/- infinity). */
static inline bool WorldInternal_IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

/**
 * Clones a NUL-terminated UTF-8 string into an owned buffer, or gives NULL for NULL. Memory_Allocate aborts on
 * failure, so this cannot fail. Free the result with Memory_Free.
 */
static inline unsigned char* WorldInternal_CloneString(const unsigned char* source)
{
    if (source == NULL)
    {
        return NULL;
    }

    size_t Length = 0;
    while (source[Length] != (unsigned char)u8'\0')
    {
        Length++;
    }
    unsigned char* Copy = Memory_Allocate(Length + 1);
    Memory_Copy(source, Copy, Length + 1);
    return Copy;
}

/** Byte equality of two NUL-terminated strings; two NULLs are equal, NULL and a string are not. */
static inline bool WorldInternal_AreStringsEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    size_t Index = 0;
    while ((a[Index] == b[Index]) && (a[Index] != (unsigned char)u8'\0'))
    {
        Index++;
    }
    return a[Index] == b[Index];
}
