#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
// AssetUserID: a world built with an asset manager holds its sprite animations under its own asset user.
#include "AssetManager.h"
#include "WorldObject.h"
#include "WorldEnvironment.h"


/**
 * @file World.h
 * @brief A live 3D world: the objects it contains, the shared id counter, and its environment settings.
 *
 * A World owns a flat, ordered collection of WorldObject pointers (model objects, sprite objects and
 * lights, all stored polymorphically through their common base) plus the WorldEnvironment describing its
 * sky/fog/effect settings. It mints the unique object ids (a shared uint64 counter starting at 1; 0 is
 * never a valid id) and owns every object added to it — World_Deconstruct destroys them all.
 *
 * The world is data: it can be built, mutated, saved (via WorldDTO) and torn down with no renderer present.
 * The world renderer consumes a World separately and never writes to it.
 *
 * ASSETS. A world built with World_Construct2 borrows an asset manager and owns an asset user of its own.
 * Sprite objects play their animation as world state (game logic advances it), so the world loads each
 * sprite's animation by name under that user when the sprite is added, and again when its name changes
 * through World_SetSpriteAnimationAssetName. The animations stay held until the world is destroyed. A world
 * built with World_Construct1 has no asset manager: its sprites stay empty (they draw nothing), which suits
 * tools and tests that only handle data.
 *
 * CHANGE TRACKING. The object-set version changes whenever an object is added or removed, so a consumer (the
 * renderer's per-object cache) can tell cheaply whether the object list is the one it saw last frame.
 *
 * Object lookup by id is currently a linear scan; this is fine for the expected object counts, and can be
 * backed by an index later without changing the API.
 */


// Types.
/**
 * @brief A live 3D world and everything persistently in it.
 *
 * A value type the caller holds. Construct with World_Construct1 or World_Construct2 and release with
 * World_Deconstruct.
 * Underscore-prefixed fields are internal/read-only to code outside this module.
 */
typedef struct WorldStruct
{
    /** @brief Owned buffer of WorldObject* (one pointer per contained object). */
    GenericBuffer _objects;
    /** @brief Next object id to hand out; starts at 1 and only increases. */
    uint64_t _nextId;
    /** @brief Incremented on every object addition and removal; see World_GetObjectSetVersion. */
    uint64_t _objectSetVersion;
    /** @brief Owned, NUL-terminated UTF-8 world name; may be NULL. */
    unsigned char* _name;
    /** @brief Asset manager the sprite animations load from; borrowed. NULL for a world without assets. */
    AssetManager* _assets;
    /** @brief Logger for asset failures; borrowed, may be NULL. */
    Logger* _logger;
    /** @brief The world's own asset user (holds its sprite animations); ASSET_USER_ID_INVALID without assets. */
    AssetUserID _assetUser;
    /** @brief This world's environment/graphics settings (sky, fog, effect multipliers). */
    WorldEnvironment Environment;
} World;


// Lifecycle.
/**
 * @brief Initializes an empty world without assets (no objects, default environment, id counter at 1).
 *
 * Sprite objects in such a world are never backed by an animation, so they draw nothing; use
 * World_Construct2 for a world that is rendered.
 * @param self The world to initialize; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error World_Construct1(World* self);

/**
 * @brief Initializes an empty world that loads its sprite animations through an asset manager.
 *
 * Mints an asset user for the world (retired by World_Deconstruct). Sprite animations are loaded under it
 * when sprites are added or renamed; see the file comment.
 * @param self The world to initialize; must not be NULL.
 * @param assets The asset manager; borrowed, must outlive the world. NULL gives a world without assets (as
 *        World_Construct1).
 * @param logger Receives one warning per sprite animation that fails to load; borrowed, may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_Construct2(World* self, AssetManager* assets, Logger* logger);

/**
 * @brief Destroys every object in the world and releases the world's own storage and assets.
 *
 * Calls Destroy on each contained object, frees the object buffer and the world name, releases the
 * environment, then retires the world's asset user (which releases its sprite animations). Every object is
 * destroyed before any asset is released.
 * @param self The world to release.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; otherwise the error from retiring the asset
 *          user (the world is fully released either way).
 */
Error World_Deconstruct(World* self);


// Objects.
/**
 * @brief Adds an object to the world, assigning it the next free id; the world takes ownership.
 *
 * The object must not already carry an id (its id must be 0). On success the world owns the object and
 * will destroy it on removal or teardown; on failure ownership stays with the caller. A sprite object is
 * then backed by its named animation when the world has an asset manager (replacing any animation set
 * before); an animation that fails to load is logged once and leaves the sprite empty, without failing the
 * add. Binding re-initializes the sprite's playback instance, which drops event subscriptions made on it before
 * the add: subscribe after adding.
 * @param self The world; must not be NULL.
 * @param object The object to add; must not be NULL and must have id 0 (not already added to a world).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p object is NULL; ErrorCode_InvalidOperation
 *          if the object already has an id, or the object could not be stored.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_AddObject(World* self, WorldObject* object);

/**
 * @brief Adds an object with an explicit id, used when loading a saved world; the world takes ownership.
 *
 * Intended for the persistence layer (WorldDTO) to restore objects with their saved ids. The id must be
 * non-zero and not already used by another object in the world; the world's id counter is advanced past
 * @p id so future World_AddObject calls do not collide with it. On success the world owns the object, and a
 * sprite object is bound to its animation as in World_AddObject.
 * @param self The world; must not be NULL.
 * @param object The object to add; must not be NULL and must have id 0.
 * @param id The id to assign; must not be 0 and must be unused in this world.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p object is NULL, @p id is 0, or the object
 *          already has an id; ErrorCode_InvalidOperation if @p id is already in use or the object could
 *          not be stored.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_AddObjectWithId(World* self, WorldObject* object, uint64_t id);

/**
 * @brief Removes and destroys the object with the given id.
 * @param self The world; must not be NULL.
 * @param id The id of the object to remove.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p id is 0; ErrorCode_InvalidOperation
 *          if no object with that id exists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_RemoveObjectById(World* self, uint64_t id);

/**
 * @brief Changes a sprite object's animation asset name and backs its playback with the new animation.
 *
 * This is the only way to rename the animation of a sprite that belongs to a world (the sprite's own setter
 * refuses then), so the stored name and the playing animation cannot disagree. Playback restarts from the
 * new animation's defaults, and event subscriptions on the sprite's playback instance are dropped (subscribe
 * again afterwards). When the world has an asset manager the animation is loaded under the world's
 * asset user; a failed load is logged once and leaves the sprite empty, without failing the call. The
 * previous animation stays held until the world is destroyed.
 * @param self The world; must not be NULL.
 * @param id The id of a sprite object in this world.
 * @param assetName Borrowed NUL-terminated UTF-8 asset name to clone, or NULL to clear the animation.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p id is 0; ErrorCode_InvalidOperation if
 *          no object with @p id exists or it is not a sprite object.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_SetSpriteAnimationAssetName(World* self, uint64_t id, const unsigned char* assetName);

/**
 * @brief Looks up a contained object by id.
 * @param self The world; must not be NULL.
 * @param id The id to look up.
 * @param outObject [out] Receives the borrowed object (world-owned) if found, NULL otherwise. Must not be NULL.
 * @returns true if an object with @p id was found (and written to @p outObject); false otherwise (including
 *          NULL arguments).
 */
bool World_TryGetObjectById(World* self, uint64_t id, WorldObject** outObject);

/**
 * @brief Returns the number of objects in the world.
 * @param self The world; may be NULL (returns 0).
 * @returns The object count.
 */
size_t World_GetObjectCount(World* self);

/**
 * @brief Returns the object at a given storage index (0-based), for iteration.
 *
 * The index is a position in the world's internal ordering, not an id; it is stable only while the object
 * set is unmodified.
 * @param self The world; must not be NULL.
 * @param index Index in [0, World_GetObjectCount).
 * @param outObject [out] Receives the borrowed object (world-owned). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outObject is NULL; ErrorCode_IndexOutOfBounds
 *          if @p index is out of range.
 */
Error World_GetObjectByIndex(World* self, size_t index, WorldObject** outObject);


// World properties.
/**
 * @brief Returns a mutable pointer to the world's environment settings (world-owned).
 * @param self The world; must not be NULL.
 * @returns The embedded environment.
 */
static inline WorldEnvironment* World_GetEnvironment(World* self)
{
    return &self->Environment;
}

/**
 * @brief Returns a number that changes whenever an object is added to or removed from the world.
 *
 * Equal values from the same world mean its object list (which objects, in which order) is unchanged; it
 * says nothing about the objects' properties. Values from different worlds are unrelated.
 * @param self The world; must not be NULL.
 * @returns The current object-set version.
 */
static inline uint64_t World_GetObjectSetVersion(const World* self)
{
    return self->_objectSetVersion;
}

/**
 * @brief Returns the value the id counter will next hand out (useful for persistence).
 * @param self The world; must not be NULL.
 * @returns The next object id.
 */
static inline uint64_t World_GetNextObjectId(const World* self)
{
    return self->_nextId;
}

/**
 * @brief Raises the id counter so the next id handed out is at least @p minimumNext (never lowers it).
 *
 * Used by the persistence layer to restore a saved id counter after re-adding objects, so future ids do
 * not collide with previously freed ones.
 * @param self The world; must not be NULL.
 * @param minimumNext The minimum value the next handed-out id should have.
 */
void World_EnsureNextObjectIdAtLeast(World* self, uint64_t minimumNext);

/**
 * @brief Returns the world's borrowed name (may be NULL); valid until changed or freed.
 * @param self The world; must not be NULL.
 * @returns The NUL-terminated UTF-8 world name, or NULL if unset.
 */
static inline const unsigned char* World_GetName(const World* self)
{
    return self->_name;
}

/**
 * @brief Replaces the world name with an owned clone of @p name (or NULL to clear it).
 * @param self The world; must not be NULL.
 * @param name Borrowed NUL-terminated UTF-8 name to clone, or NULL to clear.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error World_SetName(World* self, const unsigned char* name);
