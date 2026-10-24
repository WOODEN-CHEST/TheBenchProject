#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRHashMap.h"
// AssetUserID: a world built with an asset manager holds its components' assets (world sprites) under its own user.
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
 * - the hierarchy: each object's parent and children (World_SetParent), and each object's cached world matrix;
 * - one dense array of components per component type, in the order they were added, which systems iterate
 *   (World_GetComponents): the renderer draws the models, sprites and lights from them.
 *
 * The world is data: it can be built, changed and torn down with no renderer present; the world renderer reads it and
 * never writes to it. Objects and components point back to their world, so a World must never be moved or copied once
 * constructed.
 *
 * HIERARCHY. World_SetParent places an object under another: its position, rotation and scale become relative to the
 * parent, so it follows the parent (a cup on a desk, a light on a lamp, the parts of a door); World_SetParentSocket
 * places it on a socket of the parent (T30: a named place of its model, on a bone that moves with its pose: a cup in a
 * hand). It refuses unknown ids, an
 * object as its own parent, cycles (a parent inside the object's own subtree) and hierarchies of more than
 * WORLD_HIERARCHY_MAX_DEPTH levels; with keepWorldTransform it rewrites the object's local transform so it stays where it
 * is (the editor's default). Removing an object removes its whole subtree, children first. A disabled parent disables
 * its subtree and an invisible one hides it (World_IsEffectivelyEnabled, World_IsEffectivelyVisible). Each object's
 * world matrix is cached: a change of a transform or a parent marks the subtree dirty, and World_UpdateTransforms
 * (once a tick, after everything that moves objects) recomputes only the dirty ones, parents first. Reading a dirty
 * matrix computes it without storing it, so reads never change the world (the renderer reads them).
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
 *     WorldObject_SetPosition(Lamp, Vector3Double_Create(2.0, 0.0, 1.0));
 *     WorldComponent* Light = NULL;
 *     World_AddComponent(&world, WorldObject_GetId(Lamp), WorldComponentType_Light, &Light);
 *     WorldLightComponent_SetRange(WorldLightComponent_FromComponent(Light), 6.0f);
 * @endcode
 *
 * ASSETS. A world built with World_Construct2 borrows an asset manager and owns an asset user of its own: sprite
 * components load their world sprites under it, and with them the animations they play (as world state, advanced by
 * game logic). The assets stay held until the world is destroyed, after every object. A world built with
 * World_Construct1 has no asset manager: its sprites stay empty (they draw nothing), which suits tools and tests that
 * only handle data.
 *
 * CHANGE TRACKING. The object-set version changes when an object is created or removed, the component-set version
 * when a component is added or removed (also with its object), so a consumer (the renderer's cache) can tell cheaply
 * whether the lists are the ones it saw last frame. Property changes advance the object's and the component's own
 * revisions instead.
 *
 * THE TICK. Systems that change a world run in this order each game tick, so each knows its slot:
 * 1. input; 2. scripts (later); 3. transform drivers: characters, vehicles, physics (later); 4. collision sync
 * (later); 5. queued events: trigger enter and exit, later contacts (later); 6. animation (sprite playback today);
 * 7. World_UpdateTransforms: the world matrices of everything that moved this tick; 8. the renderer, which only reads.
 */


// Macros.
/** @brief The most bytes the data of all of a world's objects may count together (DataMap_GetByteSize): 16 MiB. */
#define WORLD_DATA_MAX_BYTES ((size_t)16777216)
/** @brief The longest world name, in bytes of UTF-8 without the terminator (World_SetName). */
#define WORLD_NAME_MAX_LENGTH ((size_t)256)
/**
 * @brief The largest object id: UINT64_MAX - 1. UINT64_MAX is never an id (World_CreateObjectWithId refuses it, the
 *        counter stops below it), so every object can be saved and loaded: a world file has no next id past it.
 */
#define WORLD_OBJECT_ID_MAX (UINT64_MAX - 1U)


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
    /** @brief The serial the last added component got (WorldComponent_GetSerial); 0 before the first. */
    uint64_t _lastComponentSerial;
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
    /** @brief How many objects' cached world matrices are dirty; World_UpdateTransforms brings them up to date. */
    size_t _dirtyTransformCount;
    /** @brief How many world matrices World_UpdateTransforms has computed and stored (a counter for tests). */
    uint64_t _worldMatrixRecomputeCount;
    /** @brief How many parents brought by reads and copies the world refused (World_GetRefusedParentCount). */
    uint64_t _refusedParentCount;
    /** @brief Whether a refused parent was logged (the first one is, once per world). */
    bool _hasLoggedRefusedParent;
    /** @brief Scratch World_RemoveObject reuses: the subtree being removed (WorldObject*), children first. */
    GenericBuffer _removalScratch;
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
 * Its sprite components never get a world sprite, so they draw nothing; use World_Construct2 for a world that is
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
 * Every component is detached first, while every object is alive and linked (each root's subtree children first), then
 * the components and objects are destroyed, all before any asset is released (a sprite's playback points into the
 * animation of a world sprite the world's user holds).
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
 * The id counter is advanced past @p id, so later World_CreateObject calls never collide with it; an object with
 * WORLD_OBJECT_ID_MAX uses the counter up (World_CreateObject then fails).
 * @param self The world; must not be NULL.
 * @param name Borrowed NUL-terminated UTF-8 name to clone, or NULL.
 * @param id The id; from 1 to WORLD_OBJECT_ID_MAX and unused in this world.
 * @param outObject [out] Receives the new object (world-owned). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outObject is NULL or @p id is 0;
 *          ErrorCode_InvalidOperation if @p id is in use or the object could not be stored;
 *          ErrorCode_ArgumentOutOfRange if @p id is above WORLD_OBJECT_ID_MAX or for a name that breaks its rules.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_CreateObjectWithId(World* self, const unsigned char* name, uint64_t id, WorldObject** outObject);

/**
 * @brief Removes an object with its whole subtree (its children, theirs, ...): children first, each object's
 *        components are detached (their OnDetach hooks run while the hierarchy is still in place), then the subtree is
 *        unlinked and its components and objects destroyed.
 *
 * The other objects and components keep their order. The work is one pass over the world's object list and over each
 * component array the subtree used, however large the subtree.
 * @param self The world; must not be NULL.
 * @param id The id of the object to remove.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p id is 0; ErrorCode_InvalidOperation if no
 *          object has that id or the subtree could not be listed (nothing is removed then).
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
Error World_GetObjectByIndex(const World* self, size_t index, WorldObject** outObject);


// Components.
/**
 * @brief Constructs a component of @p type with its default properties, attaches it to the object and returns it.
 *
 * Refused when the object already has a component of that type, or when the type is a transform driver and the
 * object already has one (an object has at most one driver). Adding a driver makes the object movable. The type's
 * OnAttach hook runs last (a sprite binds its world sprite then).
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
 * check each owner's flags (WorldObject_IsEffectivelyEnabled, WorldObject_IsEffectivelyVisible: a disabled or hidden
 * ancestor counts).
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


// Hierarchy (source/WorldHierarchy.c).
/**
 * @brief Places an object under a parent, or makes it a root (@p parentId 0); its subtree comes with it.
 *
 * The object becomes the parent's last child. Setting the parent it already has changes nothing. Otherwise the
 * object's revision advances, and its subtree's world matrices are marked dirty (and their transform revisions
 * advance). Refused, with nothing changed, when: the object or the parent does not exist; the parent is the object
 * itself or one of its descendants (a cycle); the hierarchy would get more than WORLD_HIERARCHY_MAX_DEPTH levels.
 * @param self The world; must not be NULL.
 * @param childId The object to move.
 * @param parentId The new parent, or 0 for none.
 * @param keepWorldTransform true: the object's position, rotation and scale are rewritten so that its world placement
 *        stays the same (the editor's default; through the one decomposition, so a placement the new parent would shear
 *        keeps its nearest unsheared form, and the values are within float precision of the old placement; the scale
 *        keeps the signs the object has in the world, less the new parent's, so a billboard flips as before); false:
 *        they are kept as they are, now relative to the new parent, so the object moves with the change.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p childId is 0; ErrorCode_InvalidOperation for
 *          an unknown object or parent, the object itself, a cycle, a hierarchy too deep, or (with
 *          @p keepWorldTransform) a new parent whose world matrix cannot be inverted (a zero scale on an axis);
 *          ErrorCode_ArgumentOutOfRange if the kept placement breaks the transform's rules (not finite).
 */
Error World_SetParent(World* self, uint64_t childId, uint64_t parentId, bool keepWorldTransform);

/**
 * @brief Places an object on a socket of a parent (T30: a cup in a hand, a hat on a head), or under it with @p socket
 *        NULL as World_SetParent does; its subtree comes with it.
 *
 * As World_SetParent, but the object's position, rotation and scale become relative to the socket's frame: a named
 * place of the parent's world model, which the parent's animator publishes with its pose (WorldAnimatorComponent.h) and
 * which may move with it. A socket the parent's pose does not have (no animator, no pose yet, another name) is the
 * parent's own space until it has. Setting the parent and the socket it already has changes nothing.
 * @param self The world; must not be NULL.
 * @param childId The object to move.
 * @param parentId The new parent; 0 makes it a root (then @p socket must be NULL).
 * @param socket The socket's name ([a-z][a-z0-9_.-]*, at most WORLD_OBJECT_SOCKET_NAME_MAX_LENGTH bytes; copied), or
 *        NULL for the parent's own space.
 * @param keepWorldTransform As World_SetParent's, relative to the socket's frame as it is now.
 * @returns As World_SetParent; ErrorCode_ArgumentOutOfRange for a socket name that breaks its rule;
 *          ErrorCode_InvalidOperation for a socket with no parent.
 */
Error World_SetParentSocket(World* self, uint64_t childId, uint64_t parentId, const unsigned char* socket,
    bool keepWorldTransform);

/**
 * @brief Gives a socket's world matrix: the socket's frame in the object's current pose (its animator's, T30) times the
 *        object's world matrix, in double. Reading never changes the world.
 * @param self The world; must not be NULL.
 * @param objectId The object (with an animator whose pose has the socket).
 * @param socket The socket's name; NULL finds nothing.
 * @param outMatrix [out] Receives the socket's space to world space. Must not be NULL.
 * @returns Whether the object exists and its pose has the socket.
 */
bool World_TryGetSocketWorldMatrix(World* self, uint64_t objectId, const unsigned char* socket,
    Matrix4Double* outMatrix);

/**
 * @brief Gives an object's parent.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param outParentId [out] Receives the parent's id, 0 for a root (and on failure). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outParentId is NULL or @p id is 0;
 *          ErrorCode_InvalidOperation if no object has @p id.
 */
Error World_GetParent(World* self, uint64_t id, uint64_t* outParentId);

/**
 * @brief Appends the ids of an object's children, in the order they were attached. @p outIds is not cleared.
 *
 * WorldObject_GetFirstChild / WorldObject_GetNextSibling walk them without copying.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param outIds A buffer of uint64_t to append the ids to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL, @p outIds does not hold uint64_t or @p id is 0;
 *          ErrorCode_InvalidOperation if no object has @p id; ErrorCode_BufferTooSmall if @p outIds could not grow.
 */
Error World_GetChildren(World* self, uint64_t id, GenericBuffer* outIds);

/**
 * @brief Gives an object's world matrix in double (WorldObject_GetWorldMatrix): its local transform times its parent's
 *        world matrix. Reading never changes the world.
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param outMatrix [out] Receives the matrix. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outMatrix is NULL or @p id is 0;
 *          ErrorCode_InvalidOperation if no object has @p id.
 */
Error World_GetWorldMatrix(World* self, uint64_t id, Matrix4Double* outMatrix);

/**
 * @brief Gives an object's position in world space, in double (WorldObject_GetWorldPosition).
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param outPosition [out] Receives the position. Must not be NULL.
 * @returns As World_GetWorldMatrix.
 */
Error World_GetWorldPosition(World* self, uint64_t id, Vector3Double* outPosition);

/**
 * @brief Gives an object's rotation in world space as Euler XYZ angles in radians (WorldObject_GetWorldTransform: a
 *        root's own rotation, a child's read from its world matrix).
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @param outRotation [out] Receives the rotation. Must not be NULL.
 * @returns As World_GetWorldMatrix.
 */
Error World_GetWorldRotation(World* self, uint64_t id, Vector3* outRotation);

/**
 * @brief Reports whether an object and every ancestor are enabled (WorldObject_IsEffectivelyEnabled).
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @returns true if the object exists and is effectively enabled.
 */
bool World_IsEffectivelyEnabled(World* self, uint64_t id);

/**
 * @brief Reports whether an object and every ancestor are visible (WorldObject_IsEffectivelyVisible).
 * @param self The world; must not be NULL.
 * @param id The object's id.
 * @returns true if the object exists and is effectively visible.
 */
bool World_IsEffectivelyVisible(World* self, uint64_t id);

/**
 * @brief Brings every dirty world matrix up to date (each computed once, its parent's first) and stores it.
 *
 * Run it once a tick after everything that moves objects (the tick's step 7, before the renderer), so the renderer and
 * the next tick read cached matrices. Nothing to do (no walk at all) while nothing is dirty; otherwise one pass over the
 * objects. Anything moved after it is still read correctly, computed on each read until the next update.
 * @param self The world; may be NULL (does nothing).
 */
void World_UpdateTransforms(World* self);

/**
 * @brief Returns how many parents brought by reads and copies (the object table's JSON and GHDF readers,
 *        Properties_CopyTo) the world refused.
 *
 * Those writes cannot fail on a parent (the tables check each value alone), so the world refuses one it cannot take (no
 * such object, the object itself, a cycle, a hierarchy too deep): the object keeps its parent, and the first refusal
 * is logged. A loader compares the count before and after reading each object and refuses a file that raised it.
 * @param self The world; must not be NULL.
 * @returns The count since the world was made.
 */
static inline uint64_t World_GetRefusedParentCount(const World* self)
{
    return self->_refusedParentCount;
}


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
 * Used when loading a world file, so new ids do not reuse ids its objects had before they were removed. A minimum
 * above WORLD_OBJECT_ID_MAX uses the counter up: no id is that large (World_GetNextObjectId returns 0).
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
 * @param name Borrowed NUL-terminated UTF-8 name to clone (at most WORLD_NAME_MAX_LENGTH bytes), or NULL to clear.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a name longer than
 *          WORLD_NAME_MAX_LENGTH bytes or not valid UTF-8 (nothing changes then).
 */
Error World_SetName(World* self, const unsigned char* name);
