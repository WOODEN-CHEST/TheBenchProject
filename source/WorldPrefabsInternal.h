#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "Prefab.h"
#include "PrefabInternal.h"
#include "WorldPrefabComponent.h"
#include "WorldPrefabs.h"

/*
 * Private header (source/) of the prefabs system: what its files share. WorldPrefabs.c follows the components, loads the prefabs and
 * makes and removes a copy's objects; WorldPrefabsApply.c works out what its values and overrides come to and writes it.
 */


// Macros.
/** How many distinct warnings a copy remembers having given. */
#define WORLD_PREFABS_MAX_WARNINGS ((size_t)64)
/** How many bytes copied count as one of an update's work. */
#define WORLD_PREFABS_BYTES_PER_WORK ((size_t)64)


// Types.
/* A row a copy's values or overrides write, kept so the next apply can undo it. */
typedef struct PrefabTouchedRowStruct
{
    uint32_t Object;
    int32_t Component;
    const PropertyTable* Table;
    const PropertyDescriptor* Row;
} PrefabTouchedRow;

/* What the system remembers of one component. */
typedef struct PrefabCopyStateStruct
{
    uint64_t Serial;
    uint64_t RootId;
    /* Marked while the states are matched to the components; what stays unmarked goes. */
    bool IsListed;
    /* What the last build saw. */
    bool HasBuilt;
    uint32_t SeenRevision;
    /* The prefab resolved: an owned copy of its name (NULL: none) and the prefab (held under the system's user; NULL when it did not
     * load or would nest wrongly, which is not retried until the name changes). */
    bool IsResolved;
    unsigned char* AssetName;
    const Prefab* Asset;
    /* The id of each of the prefab's objects (0: not made), owned; as many as the prefab has. NULL while the budgets leave it no room:
     * a copy past them costs nothing. */
    uint64_t* ObjectIds;
    /* How many of them exist as far as it knows, counted in the stats. */
    size_t MadeCount;
    /* The bytes it holds, counted in the stats: its prefab's file while it has objects, and what its last apply wrote. */
    size_t HeldBytes;
    size_t WrittenBytes;
    /* Whether a budget left something out: an update that looks makes it when it finds room. */
    bool IsShort;
    /* PrefabTouchedRow. */
    GenericBuffer Touched;
    /* uint64_t hashes of the warnings given. */
    GenericBuffer Warned;
} PrefabCopyState;

struct WorldPrefabsStruct
{
    World* _world;
    AssetManager* _assets;
    Logger* _logger;
    AssetUserID _assetUser;
    WorldPrefabsLimits _limits;
    WorldPrefabsStats _stats;
    /* PrefabCopyState, in the components' order, which is the order of their serials. */
    GenericBuffer _states;
    /* How many warnings all the copies gave since the last rebuild. */
    size_t _warningCount;
    /* What the last update saw of the world's prefab components. */
    bool _hasSeen;
    uint64_t _seenSetVersion;
    uint64_t _seenRevision;
    /* Whether a copy waits for a later update, and the state the next update starts on so that none starves. */
    bool _hasPending;
    size_t _nextState;
    /* The work one update may still do, and whether it finished a copy: it always finishes the first. */
    size_t _workLeft;
    bool _hasWorked;
    /* Lent to rows' readers during an update, and gone after it: a pool keeps a copy of every key it ever saw. */
    JSONObjectPool* _pool;
    /* Scratch of an apply: PrefabWrite; the bytes of the values they write; a zeroed instance. */
    GenericBuffer _writes;
    size_t _writeBytes;
    void* _scratch;
    size_t _scratchSize;
    /* Scratch of a build: which objects it removes. */
    GenericBuffer _removed;
};


// Functions: WorldPrefabs.c.
/* Logs a warning for a copy, once per text; a copy that gives very many stops at WORLD_PREFABS_MAX_WARNINGS. */
void WorldPrefabsInternal_Warn(WorldPrefabs* self, PrefabCopyState* state, const char* format, ...);

/* The pool an update lends out: made when first asked for. */
JSONObjectPool* WorldPrefabsInternal_GetPool(WorldPrefabs* self);

/* The object a copy holds under an id, NULL if it went: something saved that took the id since is not the copy's. */
WorldObject* WorldPrefabsInternal_FindHeld(const WorldPrefabs* self, uint64_t id);

/* Takes work off what the update may still do. */
void WorldPrefabsInternal_SpendWork(WorldPrefabs* self, size_t work);


// Functions: WorldPrefabsApply.c.
/* The scratch an apply reuses. */
void WorldPrefabsApply_ConstructScratch(WorldPrefabs* self);
void WorldPrefabsApply_DeconstructScratch(WorldPrefabs* self);

/* Which of the prefab's objects a copy's overrides remove: @p outRemoved holds a flag per object. What lies under one goes with it, and
 * nothing is made under an object that is not there. An override naming no object of the prefab is warned about. */
void WorldPrefabsApply_FindRemoved(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component, bool* outRemoved);

/* Works out and writes what a copy's values and overrides come to. Returns false when it waits for a later update (nothing done). */
bool WorldPrefabsApply_Run(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component);
