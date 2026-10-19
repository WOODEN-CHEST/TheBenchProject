#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRHashMap.h"
// AssetUserID: a world built with an asset manager holds its components' assets (sprite animations) under its own user.
#include "AssetManager.h"
#include "DataMap.h"
#include "DataValue.h"
#include "NamespacedKey.h"
#include "WorldObject.h"
#include "WorldComponent.h"
#include "WorldEnvironment.h"


/**
 * @file World.h
 * @brief A live 3D world: its objects, their components, the id counter and the environment.
 *
 * A World owns every object in it (WorldObject.h) and every component of those objects (WorldComponent.h), plus the
 * WorldEnvironment describing its sky, fog and effects. It mints the unique object ids (a uint64 counter starting at
 * 1; 0 is never a valid id) and keeps:
 * - the objects in creation order, with an id -> object map for lookups;
 * - one dense array of components per component type, in the order they were added, which systems iterate
 *   (World_GetComponents): the renderer draws the models, sprites and lights from them.
 *
 * The world is data: it can be built, changed and torn down with no renderer present; the world renderer reads it and
 * never writes to it. Objects and components point back to their world, so a World must never be moved or copied once
 * constructed.
 *
 * KEYS. Every world borrows the program's key registry (NamespacedKey.h): the keys of its objects' tags and data are
 * interned there, so they compare as integers.
 *
 * TAGS AND DATA. Every object holds a map of namespaced keys to typed values (DataValue.h); a tag is an entry with no
 * value. Change them through World_SetData, World_RemoveData, World_AddTag and World_RemoveTag: they enforce the
 * limits (WORLD_OBJECT_DATA_MAX_ENTRIES and WORLD_OBJECT_DATA_MAX_BYTES per object, WORLD_DATA_MAX_BYTES per world;
 * past one the call fails with ErrorCode_InvalidOperation and the world logs it once), keep the tag index the queries
 * use (WorldQuery.h), and advance the object's data revision, never its render revision. Global and game-state
 * variables will use the same DataMap type, outside worlds.
 *
 * Values set in the editor are saved in the world file (through the object table's data row). Values scripts set at
 * runtime will be saved in game saves as differences from the world, once the save round designs them (architecture
 * 3.7); nothing here prevents that.
 *
 * BUILDING. World_CreateObject makes an object, World_AddComponent gives it components:
 * @code
 *     WorldObject* Lamp = NULL;
 *     World_CreateObject(&world, u8"lamp", &Lamp);
 *     WorldObject_SetPosition(Lamp, (Vector3){ 2.0f, 0.0f, 1.0f });
 *     WorldComponent* Light = NULL;
 *     World_AddComponent(&world, WorldObject_GetId(Lamp), WorldComponentType_Light, &Light);
 *     WorldLightComponent_SetRange(WorldLightComponent_FromComponent(Light), 6.0f);
 * @endcode
 *
 * ASSETS. A world built with World_Construct2 borrows an asset manager and owns an asset user of its own: sprite
 * components load their animations under it (they play as world state, advanced by game logic). The assets stay held
 * until the world is destroyed, after every object. A world built with World_Construct1 has no asset manager: its
 * sprites stay empty (they draw nothing), which suits tools and tests that only handle data.
 *
 * CHANGE TRACKING. The object-set version changes when an object is created or removed, the component-set version
 * when a component is added or removed (also with its object), so a consumer (the renderer's cache) can tell cheaply
 * whether the lists are the ones it saw last frame. Property changes advance the object's and the component's own
 * revisions instead.
 *
 * THE TICK. Systems that change a world run in this order each game tick, so each knows its slot:
 * 1. input; 2. scripts (later); 3. transform drivers: characters, vehicles, physics (later); 4. collision sync
 * (later); 5. queued events: trigger enter and exit, later contacts (later); 6. animation (sprite playback today);
 * 7. the renderer, which only reads.
 */


// Macros.
/** @brief The most bytes the data of all of a world's objects may count together (DataMap_GetByteSize): 16 MiB. */
#define WORLD_DATA_MAX_BYTES ((size_t)16777216)


// Types.
/**
 * @brief A live 3D world and everything in it.
 *
 * A value type the caller holds, constructed in place with World_Construct1 or World_Construct2 and released with
 * World_Deconstruct; never moved or copied in between. Underscore-prefixed fields are internal.
 */
typedef struct WorldStruct
{
    /** @brief Owned buffer of WorldObject* in creation order. */
    GenericBuffer _objects;
    /** @brief uint64 id -> WorldObject* (the objects in _objects). */
    HashMap _objectsById;
    /** @brief The registry entry of each component type this world holds (static data), the first
     *  _componentTypeCount set. */
    const WorldComponentTypeInfo* _componentTypes[WORLD_MAX_COMPONENT_TYPES];
    /** @brief How many component types the world holds. */
    size_t _componentTypeCount;
    /** @brief Per component type: an owned buffer of WorldComponent* in the order they were added (the first
     *  _componentTypeCount constructed). */
    GenericBuffer _components[WORLD_MAX_COMPONENT_TYPES];
    /** @brief The next object id to hand out; starts at 1 and only increases; 0 once every id was handed out. */
    uint64_t _nextId;
    /** @brief Changes on every object creation and removal; see World_GetObjectSetVersion. */
    uint64_t _objectSetVersion;
    /** @brief Changes on every component addition and removal; see World_GetComponentSetVersion. */
    uint64_t _componentSetVersion;
    /** @brief Owned, NUL-terminated UTF-8 world name; may be NULL. */
    unsigned char* _name;
    /** @brief The key registry the keys of the objects' tags and data come from; borrowed, never NULL. */
    KeyRegistry* _keys;
    /** @brief The tag index: tag key id -> the ids of the objects carrying it, ascending (see WorldData.c). */
    HashMap _tagIndex;
    /** @brief The bytes the data of every object counts together (see World_GetDataByteSize). */
    size_t _dataByteSize;
    /** @brief The data limits already logged (one bit each), so each is logged once per world. */
    uint32_t _loggedDataLimits;
    /** @brief Scratch World_Query reuses: candidate object ids (uint64_t). */
    GenericBuffer _queryCandidates;
    /** @brief Scratch World_Query reuses: matches with their distances, for sorting. */
    GenericBuffer _queryMatches;
    /** @brief The asset manager components load from; borrowed. NULL for a world without assets. */
    AssetManager* _assets;
    /** @brief Logger for failures components report; borrowed, may be NULL. */
    Logger* _logger;
    /** @brief The world's own asset user; ASSET_USER_ID_INVALID without assets. */
    AssetUserID _assetUser;
    /** @brief This world's environment settings (sky, sun, moon, fog, effects). */
    WorldEnvironment Environment;
} World;


// Lifecycle.
/**
 * @brief Initializes an empty world without assets (no objects, default environment, id counter at 1).
 *
 * Its sprite components never get an animation, so they draw nothing; use World_Construct2 for a world that is
 * rendered.
 * @param self The world to initialize in place; must not be NULL.
 * @param keys The program's key registry; borrowed, must outlive the world. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p keys is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_Construct1(World* self, KeyRegistry* keys);

/**
 * @brief Initializes an empty world whose components load their assets through an asset manager.
 *
 * Mints an asset user for the world (retired by World_Deconstruct).
 * @param self The world to initialize in place; must not be NULL.
 * @param keys The program's key registry; borrowed, must outlive the world. Must not be NULL.
 * @param assets The asset manager; borrowed, must outlive the world. NULL gives a world without assets (as
 *        World_Construct1).
 * @param logger Receives one warning per asset a component fails to load, and one per data limit the world's objects
 *        reach (see World_SetData); borrowed, may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p keys is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_Construct2(World* self, KeyRegistry* keys, AssetManager* assets, Logger* logger);

/**
 * @brief Destroys every object (with its components) and releases the world's storage, environment and assets.
 *
 * Every component is detached and destroyed before any asset is released (a sprite's playback points into an
 * animation the world's user holds).
 * @param self The world to release.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; otherwise the error from retiring the asset user
 *          (the world is fully released either way).
 */
Error World_Deconstruct(World* self);


// Objects.
/**
 * @brief Creates an object with the next free id (default properties, no components).
 * @param self The world; must not be NULL.
 * @param name Borrowed NUL-terminated UTF-8 name to clone, or NULL (at most WORLD_OBJECT_NAME_MAX_LENGTH bytes).
 * @param outObject [out] Receives the new object (world-owned; valid until it is removed). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outObject is NULL; ErrorCode_ArgumentOutOfRange for a
 *          name that breaks its rules; ErrorCode_InvalidOperation when every id was handed out or the object could not
 *          be stored.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_CreateObject(World* self, const unsigned char* name, WorldObject** outObject);

/**
 * @brief Creates an object with an explicit id: the loader's version (world files keep their ids).
 *
 * The id counter is advanced past @p id, so later World_CreateObject calls never collide with it.
 * @param self The world; must not be NULL.
 * @param name Borrowed NUL-terminated UTF-8 name to clone, or NULL.
 * @param id The id; must not be 0 and must be unused in this world.
 * @param outObject [out] Receives the new object (world-owned). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outObject is NULL or @p id is 0;
 *          ErrorCode_InvalidOperation if @p id is in use or the object could not be stored;
 *          ErrorCode_ArgumentOutOfRange for a name that breaks its rules.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_CreateObjectWithId(World* self, const unsigned char* name, uint64_t id, WorldObject** outObject);

/**
 * @brief Removes an object: detaches and destroys its components, then destroys it.
 * @param self The world; must not be NULL.
 * @param id The id of the object to remove.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p id is 0; ErrorCode_InvalidOperation if no
 *          object has that id.
 */
Error World_RemoveObject(World* self, uint64_t id);

/**
 * @brief Looks up an object by id (a hash lookup).
 * @param self The world; must not be NULL.
 * @param id The id to look up.
 * @param outObject [out] Receives the object (world-owned) when found, NULL otherwise. Must not be NULL.
 * @returns true if an object with @p id exists; false otherwise (including NULL arguments).
 */
bool World_TryGetObject(World* self, uint64_t id, WorldObject** outObject);

/**
 * @brief Returns the number of objects in the world.
 * @param self The world; may be NULL (returns 0).
 * @returns The object count.
 */
size_t World_GetObjectCount(const World* self);

/**
 * @brief Returns the object at a storage index (creation order), for iteration.
 *
 * The index is not an id; it is stable only while no object is created or removed.
 * @param self The world; must not be NULL.
 * @param index Index in [0, World_GetObjectCount).
 * @param outObject [out] Receives the object (world-owned). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outObject is NULL; ErrorCode_IndexOutOfBounds if
 *          @p index is out of range.
 */
Error World_GetObjectByIndex(World* self, size_t index, WorldObject** outObject);


// Components.
/**
 * @brief Constructs a component of @p type with its default properties, attaches it to the object and returns it.
 *
 * Refused when the object already has a component of that type, or when the type is a transform driver and the
 * object already has one (an object has at most one driver). Adding a driver makes the object movable. The type's
 * OnAttach hook runs last (a sprite binds its animation then).
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param type The component type; one of the world's types (World_GetComponentTypeInfo).
 * @param outComponent [out] Receives the component (world-owned; valid until it is removed). Must not be NULL. Use the
 *        concrete type's FromComponent to reach its fields.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL, @p id is 0 or @p type is not a
 *          type of this world; ErrorCode_InvalidOperation if no object has @p id, it already has a component of
 *          @p type, it already has a transform driver, or the component could not be stored.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_AddComponent(World* self, uint64_t id, WorldComponentType type, WorldComponent** outComponent);

/**
 * @brief Detaches and destroys an object's component of a type.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param type The component type.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL, @p id is 0 or @p type is not a type of this world;
 *          ErrorCode_InvalidOperation if no object has @p id or it has no component of @p type.
 */
Error World_RemoveComponent(World* self, uint64_t id, WorldComponentType type);

/**
 * @brief Looks up an object's component of a type.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param type The component type.
 * @param outComponent [out] Receives the component (world-owned) when found, NULL otherwise. Must not be NULL.
 * @returns true if the object exists and has a component of @p type; false otherwise (including NULL arguments).
 */
bool World_TryGetComponent(World* self, uint64_t id, WorldComponentType type, WorldComponent** outComponent);

/**
 * @brief Returns every component of a type in the world, in the order they were added: what systems iterate.
 *
 * The array is the world's own (borrowed): valid until a component of that type is added or removed. Systems still
 * check each owner's flags (WorldObject_IsEnabled, WorldObject_IsVisible).
 * @param self The world; must not be NULL.
 * @param type The component type.
 * @param outCount [out] Receives the number of components. Must not be NULL.
 * @returns The components; NULL (with a count of 0) when there are none or @p type is not a type of this world.
 */
WorldComponent* const* World_GetComponents(const World* self, WorldComponentType type, size_t* outCount);

/**
 * @brief Returns how many component types the world holds (WorldComponentType_Count for every world the game builds).
 * @param self The world; must not be NULL.
 * @returns The type count; the types are 0 to the count - 1.
 */
static inline size_t World_GetComponentTypeCount(const World* self)
{
    return self->_componentTypeCount;
}

/**
 * @brief Returns the registry entry of one of the world's component types (its name, table and hooks).
 * @param self The world; must not be NULL.
 * @param type The type.
 * @returns The entry (static data), or NULL when @p type is not a type of this world.
 */
const WorldComponentTypeInfo* World_GetComponentTypeInfo(const World* self, WorldComponentType type);

/**
 * @brief Finds one of the world's component types by its name (e.g. u8"model"), as world files and the editor name
 *        them.
 * @param self The world; must not be NULL.
 * @param name The name; NULL finds nothing.
 * @param outType [out] Receives the type when found. Must not be NULL.
 * @returns true if a type has that name.
 */
bool World_TryFindComponentType(const World* self, const unsigned char* name, WorldComponentType* outType);


// Tags and data.
/**
 * @brief Sets an object's data entry to a copy of a value (a tag with DataValue_CreateTag), replacing what the key
 *        held (another type included).
 *
 * The value is validated first. Setting the value an entry already holds changes nothing. Otherwise the object's data
 * revision advances (its render revision does not) and the tag index follows.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param key The key: an id of the world's key registry (World_GetKeys, KeyRegistry_Intern).
 * @param value The value; must not be NULL. The caller keeps it.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p value is NULL or @p key is not an id of the registry;
 *          ErrorCode_InvalidOperation if no object has @p id, or the object (WORLD_OBJECT_DATA_MAX_ENTRIES,
 *          WORLD_OBJECT_DATA_MAX_BYTES) or the world (WORLD_DATA_MAX_BYTES) would pass a limit (nothing changes; the
 *          world's logger hears of each limit once); the validation error (ErrorCode_ArgumentOutOfRange).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_SetData(World* self, uint64_t id, uint32_t key, const DataValue* value);

/**
 * @brief Finds an object's data entry.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param key The key id.
 * @param outValue [out] Receives the value (world-owned: valid until the object's data changes; a tag for a tag), or
 *        NULL. Must not be NULL.
 * @returns true if the object exists and holds @p key.
 */
bool World_TryGetData(World* self, uint64_t id, uint32_t key, const DataValue** outValue);

/**
 * @brief Removes an object's data entry (a tag or a value), if it has one.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param key The key id.
 * @param outWasRemoved [out] Receives whether the key was there; may be NULL.
 * @returns Success (also when the key was not there); ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_InvalidOperation if no object has @p id.
 */
Error World_RemoveData(World* self, uint64_t id, uint32_t key, bool* outWasRemoved);

/**
 * @brief Tags an object: sets @p key to a tag (World_SetData with a tag, so a value under @p key is replaced).
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param key The tag's key id.
 * @returns As World_SetData.
 */
Error World_AddTag(World* self, uint64_t id, uint32_t key);

/**
 * @brief Removes a tag from an object; a value under @p key is not a tag and stays.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param key The tag's key id.
 * @param outWasRemoved [out] Receives whether the object carried the tag; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if no object has @p id.
 */
Error World_RemoveTag(World* self, uint64_t id, uint32_t key, bool* outWasRemoved);

/**
 * @brief Reports whether an object carries a tag.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param key The tag's key id.
 * @returns true if the object exists and carries the tag.
 */
bool World_HasTag(World* self, uint64_t id, uint32_t key);

/**
 * @brief Returns the bytes the data of all of the world's objects counts together (DataMap_GetByteSize summed).
 *
 * The data functions keep it within WORLD_DATA_MAX_BYTES. Files and copies (the object table's readers and
 * Properties_CopyTo) cannot see the world while they write, so they are counted but not refused: a loader checks this
 * after reading the objects and refuses a world past the limit.
 * @param self The world; must not be NULL.
 * @returns The byte count.
 */
static inline size_t World_GetDataByteSize(const World* self)
{
    return self->_dataByteSize;
}


// World properties.
/**
 * @brief Returns the key registry the world borrows (the keys of its objects' tags and data).
 * @param self The world; must not be NULL.
 * @returns The registry (borrowed).
 */
static inline KeyRegistry* World_GetKeys(const World* self)
{
    return self->_keys;
}

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
 * @brief Returns a number that changes whenever an object is created or removed.
 *
 * Equal values from the same world mean its object list (which objects, in which order) is unchanged; it says nothing
 * about properties or components. Values from different worlds are unrelated.
 * @param self The world; must not be NULL.
 * @returns The current object-set version.
 */
static inline uint64_t World_GetObjectSetVersion(const World* self)
{
    return self->_objectSetVersion;
}

/**
 * @brief Returns a number that changes whenever a component is added or removed (also with its object).
 *
 * Equal values from the same world mean every per-type component array is unchanged (which components, in which
 * order). Values from different worlds are unrelated.
 * @param self The world; must not be NULL.
 * @returns The current component-set version.
 */
static inline uint64_t World_GetComponentSetVersion(const World* self)
{
    return self->_componentSetVersion;
}

/**
 * @brief Returns the value the id counter will hand out next (for world files); 0 once every id was handed out.
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
 * Used when loading a world file, so new ids do not reuse ids its objects had before they were removed.
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
