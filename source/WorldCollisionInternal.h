#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "CollisionGeometry.h"
#include "WorldCollision.h"
#include "WorldCollisionTree.h"
#include "WorldColliderComponent.h"
#include "WorldModel.h"
#include "WorldObject.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): the collision world's state, shared by its files: source/WorldCollision.c (lifetime, sync,
 * debug list, stats), source/WorldCollisionQuery.c (the queries) and source/WorldCollisionTrigger.c (trigger pairs and
 * events).
 *
 * INSTANCES live in slots (one growable array with a free list), so a tree proxy can name its shape by (slot, shape
 * index) for as long as the instance lives. Between changes of the world's component set, _order maps each collider of
 * the world's dense collider array to its slot, so a sync is one walk in the world's order; _lookup (sorted by object
 * id) finds an object's instance for the queries. Every instance holds its shapes in world space (WorldCollisionShape:
 * the transform both ways, the box, the proxy).
 */


// Macros.
/** A slot that names nothing. */
#define WORLD_COLLISION_NO_SLOT UINT32_MAX


// Types.
/** One shape of an instance, in world space. */
typedef struct WorldCollisionShapeStruct
{
    /** The shape (borrowed from the world model, or the instance's OwnShape block). */
    const CollisionShape* Shape;
    /** Its index in its set (0 for an own shape). */
    uint32_t ShapeIndex;
    /** Shape space to world and back. */
    CollisionAffine ShapeToWorld;
    CollisionAffine WorldToShape;
    /** Whether its sizes are valid: always for a world model's shapes (checked at load), by CollisionShape_Validate for
     *  an own shape (its rows allow a cylinder of half height 0, which is flat). */
    bool IsSizeValid;
    /** False when it has no valid size or the transform flattens it (a zero scale): it takes part in nothing. */
    bool IsValid;
    /** An unbounded plane: no box and no proxy (it is in the planes list). */
    bool IsUnbounded;
    /** The world box (fat for nothing: the tree fattens its own copy). */
    Vector3 Min;
    Vector3 Max;
    /** Its proxy in the instance's tree, or COLLISION_TREE_NULL. */
    int32_t Proxy;
} WorldCollisionShape;

/** One enabled collider of an effectively enabled object. */
typedef struct WorldCollisionInstanceStruct
{
    /** Whether the slot holds an instance. */
    bool IsUsed;
    /** The next free slot while unused. */
    uint32_t NextFree;
    uint64_t ObjectId;
    /** The collider (borrowed: re-matched whenever the world's component set changes) and its serial (a replaced
     *  collider often reuses its predecessor's memory: the serial tells them apart). */
    WorldColliderComponent* Collider;
    uint64_t ColliderSerial;
    /** What the shapes were built from: the collider's and the object's transform revision, valid while HasSeen. */
    bool HasSeen;
    uint32_t SeenColliderRevision;
    uint32_t SeenTransformRevision;
    /** The world model resolved: an owned copy of its name (NULL: none), and the asset (borrowed under the collision
     *  world's user; NULL when there is none or it failed, which is not retried until the name changes). */
    unsigned char* WorldModelName;
    const WorldModel* WorldModel;
    /** Whether the shapes are in the trees, and in which. */
    bool IsActive;
    bool IsStatic;
    WorldColliderSettings Settings;
    /** The collider's own primitive, while Settings.UsesOwnShape: an owned block, allocated the first time, so its
     *  address survives the instances' buffer growing (its shape points at it); NULL before. */
    CollisionShape* OwnShape;
    /** WorldCollisionShape elements. */
    GenericBuffer Shapes;
    /** The object's world matrix both ways (local contact positions). */
    CollisionAffine ObjectToWorld;
    CollisionAffine WorldToObject;
    /** Set while the component set is re-matched: whether a collider still names this instance. */
    bool IsMatched;
    /** Warnings given once per instance. */
    bool HasWarnedNoShapes;
    bool HasWarnedSet;
    bool HasWarnedStaticMove;
    bool HasWarnedFlatShape;
} WorldCollisionInstance;

/** An overlapping trigger pair. */
typedef struct WorldCollisionTriggerPairStruct
{
    uint64_t TriggerId;
    uint64_t OtherId;
} WorldCollisionTriggerPair;

/** An object's slot, for lookups by id. */
typedef struct WorldCollisionLookupStruct
{
    uint64_t ObjectId;
    uint32_t Slot;
} WorldCollisionLookup;

struct WorldCollisionStruct
{
    World* _world;
    AssetManager* _assets;
    AssetUserID _assetUser;
    Logger* _logger;
    WorldCollisionOptions _options;
    /** WorldCollisionInstance slots and the free list's head. */
    GenericBuffer _instances;
    uint32_t _freeSlot;
    /** uint32_t: each collider's slot, in the world's collider order (valid for _componentSetVersion). */
    GenericBuffer _order;
    /** WorldCollisionLookup, sorted by object id. */
    GenericBuffer _lookup;
    bool _hasComponentSetVersion;
    uint64_t _componentSetVersion;
    CollisionTree _staticTree;
    CollisionTree _movableTree;
    /** uint64_t keys of the unbounded planes. */
    GenericBuffer _planes;
    /** WorldCollisionTriggerPair, sorted: this tick's and the next's. */
    GenericBuffer _triggerPairs;
    GenericBuffer _newTriggerPairs;
    /** WorldTriggerEvent, queued until cleared. */
    GenericBuffer _triggerEvents;
    /** uint64_t keys a query collects. */
    GenericBuffer _candidates;
    /** WorldCollisionDebugShape, while the list is enabled. */
    bool _isDebugListEnabled;
    GenericBuffer _debugShapes;
    uint64_t _tick;
    uint64_t _rebuildCount;
    uint64_t _moveCount;
};


// Functions.
static inline uint64_t WorldCollisionInternal_MakeKey(uint32_t slot, uint32_t shape)
{
    return ((uint64_t)slot << 32U) | (uint64_t)shape;
}

static inline uint32_t WorldCollisionInternal_GetKeySlot(uint64_t key)
{
    return (uint32_t)(key >> 32U);
}

static inline uint32_t WorldCollisionInternal_GetKeyShape(uint64_t key)
{
    return (uint32_t)(key & 0xFFFFFFFFULL);
}

static inline WorldCollisionInstance* WorldCollisionInternal_GetInstance(const WorldCollision* self, uint32_t slot)
{
    return &((WorldCollisionInstance*)(void*)self->_instances._data)[slot];
}

static inline WorldCollisionShape* WorldCollisionInternal_GetShape(const WorldCollisionInstance* instance,
    uint32_t index)
{
    return &((WorldCollisionShape*)(void*)instance->Shapes._data)[index];
}

/* The slot of an object's instance (used or not); false when it has none. */
bool WorldCollisionInternal_FindSlot(const WorldCollision* self, uint64_t objectId, uint32_t* outSlot);

/* Whether a query with @p filter sees an instance (active, its layer in the mask, the trigger rule, not ignored). An
 * ignored subtree is read from the live hierarchy through the world, by id: queries may run between a removal and the
 * next sync, when the instance's collider and object may be gone. */
bool WorldCollisionInternal_PassesFilter(WorldCollision* self, const WorldCollisionInstance* instance,
    const WorldCollisionFilter* filter);

/* Fills what a result says about an instance's shape (no triangle). */
void WorldCollisionInternal_FillShapeInfo(const WorldCollisionInstance* instance, const WorldCollisionShape* shape,
    WorldCollisionShapeInfo* outInfo);

/* Collects into _candidates the keys of every shape whose box overlaps [min, max] (both trees) and every unbounded
 * plane. */
void WorldCollisionInternal_CollectCandidates(WorldCollision* self, Vector3 min, Vector3 max);

/* The trigger step of a sync: this tick's pairs against the last tick's, their events queued. */
void WorldCollisionInternal_UpdateTriggers(WorldCollision* self);

/* Whether two instances' shapes overlap (touching counts), or come within @p margin of each other. */
bool WorldCollisionInternal_DoInstancesTouch(WorldCollision* self, const WorldCollisionInstance* a,
    const WorldCollisionInstance* b, float margin);
