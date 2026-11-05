#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "AssetManager.h"
#include "Logger.h"
#include "Prefab.h"
#include "World.h"


/**
 * @file WorldPrefabs.h
 * @brief The prefabs system: it keeps the objects of every copy of a prefab (WorldPrefabComponent.h) up to date, once a tick and only
 *        when something changed.
 *
 * WHAT IT MAKES. Under a copy's root, an object per object of the prefab: runtime objects, which world files never save, so a world
 * keeps only the root, the prefab's name, the values and the overrides. The prefab's root is a child of the copy's root, which stays
 * a plain object: what a copy adds of its own hangs on the root as an ordinary saved child, and the system never touches it.
 *
 * IDS. An object's id is derived from the id of its copy's root and its name in the prefab (WorldPrefabs_DeriveObjectId), so a
 * reference to "the bench in this bus stop" holds across loads and machines. A nested copy's objects derive from its own root's id.
 * Two that come out the same take the next salt, with a warning.
 *
 * WHAT IS APPLIED, in this order, each over the last: the prefab's objects as they are; each property in the order it is declared,
 * with the copy's value or its default; then the copy's overrides. It writes only rows that come out different from what the object
 * holds, so a re-apply that changes nothing costs nothing in the systems that watch those rows, and it undoes what it no longer
 * writes: an enum's last option goes when the next one is chosen. Rows no property or override touches are left as they are, whoever
 * changed them.
 *
 * NESTED COPIES are ordinary objects with this component: what the outer copy passes them lands in their values and overrides rows,
 * and their own apply follows from their revision. All of it settles within one update, as far as that update's work goes.
 *
 * WHAT IS WATCHED. A change of a component's rows, by any means. A quiet tick costs two comparisons, however many copies there are.
 * A prefab that does not load makes nothing and is not tried again until the name changes (or WorldPrefabs_Rebuild), with one warning.
 *
 * Tick order: after the transform drivers, before the path objects (a prefab may hold a fence), before WorldCollision_Step. Main thread
 * only. It goes before the world does, and takes what it made with it.
 */


// Macros.
/** @brief How many copies nest inside one another at most: past it a copy makes nothing, with a warning. */
#define WORLD_PREFABS_MAX_NESTING ((size_t)16)
/** @brief What every copy of a world makes together, at most, by default (WorldPrefabsLimits). */
#define WORLD_PREFABS_MAX_TOTAL_OBJECTS ((size_t)1048576)
/** @brief What every copy of a world holds together, in bytes at most, by default (WorldPrefabsLimits): 512 MiB. */
#define WORLD_PREFABS_MAX_TOTAL_BYTES ((size_t)536870912)
/** @brief How much work one update does, by default (WorldPrefabsLimits): what is left waits for the next. */
#define WORLD_PREFABS_MAX_UPDATE_WORK ((size_t)2097152)
/** @brief Warnings all of a system's copies give together, until the next rebuild: past them it stays quiet, and says so once. */
#define WORLD_PREFABS_MAX_TOTAL_WARNINGS ((size_t)1024)


// Types.
/** @brief The system (opaque). */
typedef struct WorldPrefabsStruct WorldPrefabs;

/** @brief Limits for hostile worlds, whose small files could otherwise ask for any number of objects: a town holds far fewer. */
typedef struct WorldPrefabsLimitsStruct
{
    /** @brief The most objects every copy makes together: past it a copy makes what it can, and the rest when an update finds room. */
    size_t MaxObjects;
    /** @brief The most bytes the copies hold together: each counts its prefab's file, whose objects it copies, and every value it writes.
     *         Past it a copy makes nothing, or writes none of its values, until an update finds room: objects alone do not bound what a small
     *         file can ask for. */
    size_t MaxBytes;
    /** @brief The most work one update does: an object looked at or a row written is one, and so are 64 bytes copied. The copies past it wait
     *         for the next update, which starts with them. An update always finishes the first copy it starts on, however large. */
    size_t MaxUpdateWork;
    /** @brief Warnings all the copies give together, until the next rebuild. */
    size_t MaxWarnings;
} WorldPrefabsLimits;

/** @brief What the system holds now, and how much it did. */
typedef struct WorldPrefabsStatsStruct
{
    /** @brief The copies it follows, nested ones too. */
    size_t CopyCount;
    /** @brief The objects they hold now. */
    size_t ObjectCount;
    /** @brief How many times a copy's values and overrides were applied. */
    uint64_t ApplyCount;
    /** @brief How many rows came out different and were written. */
    uint64_t WriteCount;
    /** @brief How many times it looked over every copy: never on a quiet update. */
    uint64_t PassCount;
    /** @brief The bytes every copy holds (WorldPrefabsLimits.MaxBytes). */
    uint64_t ByteCount;
} WorldPrefabsStats;


// Functions.
/**
 * @brief Creates the system over a world.
 * @param world The world; must outlive the system. Must not be NULL.
 * @param assets Loads the prefabs, under the system's own asset user. Must not be NULL.
 * @param logger Receives one warning for each thing a copy cannot do (a missing prefab, a stale override's name, a limit), up to
 *        WorldPrefabsLimits.MaxWarnings; may be NULL.
 * @param outSystem [out] Receives the system. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments.
 */
Error WorldPrefabs_Create(World* world, AssetManager* assets, Logger* logger, WorldPrefabs** outSystem);

/**
 * @brief Releases the system and removes what it made from the world.
 * @param self The system, or NULL.
 */
void WorldPrefabs_Destroy(WorldPrefabs* self);

/**
 * @brief Brings every copy up to date: once a tick, and at load until WorldPrefabs_IsSettled.
 * @param self The system; must not be NULL.
 */
void WorldPrefabs_Update(WorldPrefabs* self);

/**
 * @brief Whether nothing waits for a later update: false only past MaxUpdateWork.
 * @param self The system; must not be NULL.
 * @returns Whether every copy is up to date as of the last update.
 */
bool WorldPrefabs_IsSettled(const WorldPrefabs* self);

/**
 * @brief Lets go of every prefab and makes every copy again from the definitions as they are now: how a changed definition reaches
 *        copies until assets reload in place. Ids stay; what a copy made is made anew. A path object's pieces pick their enums when it
 *        builds, so a changed prefab reaches those picks through WorldPathObjects_Rebuild.
 * @param self The system; must not be NULL.
 */
void WorldPrefabs_Rebuild(WorldPrefabs* self);

/**
 * @brief The default limits.
 * @returns WORLD_PREFABS_MAX_TOTAL_OBJECTS, WORLD_PREFABS_MAX_TOTAL_BYTES, WORLD_PREFABS_MAX_UPDATE_WORK and
 *          WORLD_PREFABS_MAX_TOTAL_WARNINGS.
 */
static inline WorldPrefabsLimits WorldPrefabsLimits_CreateDefault(void)
{
    return (WorldPrefabsLimits){ .MaxObjects = WORLD_PREFABS_MAX_TOTAL_OBJECTS, .MaxBytes = WORLD_PREFABS_MAX_TOTAL_BYTES,
        .MaxUpdateWork = WORLD_PREFABS_MAX_UPDATE_WORK, .MaxWarnings = WORLD_PREFABS_MAX_TOTAL_WARNINGS };
}

/**
 * @brief Sets what the copies of the world may make together. Later updates keep to it; what is made already stays.
 * @param self The system; must not be NULL.
 * @param limits The limits; must not be NULL.
 */
void WorldPrefabs_SetLimits(WorldPrefabs* self, const WorldPrefabsLimits* limits);

/**
 * @brief What the system holds now.
 * @param self The system; must not be NULL.
 * @returns The stats.
 */
WorldPrefabsStats WorldPrefabs_GetStats(const WorldPrefabs* self);

/**
 * @brief The id an object of a copy takes: pure, the same on every machine and every load, in the derived range.
 * @param rootId The id of its copy's root.
 * @param name The object's name in the prefab; must not be NULL.
 * @param salt 0, or how many ids came out taken before it.
 * @returns The id, from WORLD_OBJECT_DERIVED_ID_FIRST to WORLD_OBJECT_ID_MAX.
 */
uint64_t WorldPrefabs_DeriveObjectId(uint64_t rootId, const unsigned char* name, uint32_t salt);


// Functions: selection. What a click needs: it walks up to a copy's root, and entering reaches the objects inside.
/**
 * @brief The root of the nearest copy an object belongs to: itself if it holds the component, else the first ancestor that does,
 *        walking through runtime objects only. An ordinary saved child a copy added of its own belongs to no copy, nor does what a
 *        saved object generates. A nested copy's root is its own copy's root.
 * @param world The world; must not be NULL.
 * @param objectId The object.
 * @param outRootId [out] Receives the root's id when found. Must not be NULL.
 * @returns Whether it belongs to a copy.
 */
bool WorldPrefabs_TryGetCopyRoot(World* world, uint64_t objectId, uint64_t* outRootId);

/**
 * @brief The root of the outermost copy an object belongs to: what a first click selects.
 * @param world The world; must not be NULL.
 * @param objectId The object.
 * @param outRootId [out] Receives the root's id when found. Must not be NULL.
 * @returns Whether it belongs to a copy.
 */
bool WorldPrefabs_TryGetOutermostCopyRoot(World* world, uint64_t objectId, uint64_t* outRootId);

/**
 * @brief Finds an object of a copy by its path of names: "bench", or "lamp/bulb" through a nested copy.
 * @param self The system; must not be NULL.
 * @param rootId The copy's root.
 * @param path The path; must not be NULL.
 * @param outObjectId [out] Receives its id when found. Must not be NULL.
 * @returns Whether the copy holds it: false for one a copy removed, and for a copy the system has not reached yet.
 */
bool WorldPrefabs_TryFindObject(const WorldPrefabs* self, uint64_t rootId, const unsigned char* path, uint64_t* outObjectId);

/**
 * @brief Appends the ids of the objects a copy holds, parents first, not those of the copies nested in it.
 * @param self The system; must not be NULL.
 * @param rootId The copy's root.
 * @param outIds Appended uint64_t ids; must not be NULL.
 * @returns Whether it is a copy the system follows.
 */
bool WorldPrefabs_GetCopyObjects(const WorldPrefabs* self, uint64_t rootId, GenericBuffer* outIds);

/**
 * @brief The prefab a copy shows, for its options panel.
 * @param self The system; must not be NULL.
 * @param rootId The copy's root.
 * @returns The prefab (borrowed: valid until its name changes or WorldPrefabs_Rebuild), or NULL when it shows none.
 */
const Prefab* WorldPrefabs_GetPrefab(const WorldPrefabs* self, uint64_t rootId);
