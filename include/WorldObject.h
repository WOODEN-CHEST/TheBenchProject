#pragma once
#include <stdint.h>
#include <stdbool.h>
// Vector3 comes from Raylib and RenderColor (the tint type) from the renderer. Both are part of a world object's public
// data, so these includes are unavoidable in this header.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRError.h"
#include "DataMap.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"


/**
 * @file WorldObject.h
 * @brief A world object: one concrete type for everything placed in a 3D world.
 *
 * Every object has the same data: a unique id, a (non-unique) name, a transform (position, Euler XYZ rotation,
 * per-axis scale), a render tint, three flags (whether it is enabled, whether it is visible, and its mobility), and its
 * tags and custom data. What an object is comes from its components (WorldComponent.h): a model, a sprite, a light, at
 * most one of each type.
 *
 * LIFETIME. Objects exist only inside a world: World_CreateObject makes one (the world assigns its id) and
 * World_RemoveObject destroys it with its components. An object points back to its world, so a World must never be
 * moved or copied once constructed.
 *
 * PROPERTIES. Every property is a row of the object's property table (WorldObject_GetPropertyTable): name, position,
 * rotation, scale, tint, is_enabled, is_visible, mobility and data. The setters below validate through the table, so
 * the rules live there only, and the table functions (Properties_Set, the JSON and GHDF readers, Properties_CopyTo)
 * work on objects too. Every change, from either side, advances the object's revision (WorldObject_GetRevision); a
 * change of the tags and data advances its data revision instead (WorldObject_GetDataRevision).
 *
 * TAGS AND DATA. Each object holds a DataMap (DataMap.h) of namespaced keys to typed values: a tag is an entry with no
 * value. It is NULL while empty. Change it through the world, which keeps its tag index, its limits and the data
 * revision: World_SetData, World_RemoveData, World_AddTag, World_RemoveTag (World.h). An object holds at most
 * WORLD_OBJECT_DATA_MAX_ENTRIES entries counting at most WORLD_OBJECT_DATA_MAX_BYTES (DataMap_GetByteSize). The data
 * row of the table is ReadOnly for Properties_Set; world files and copies still fill it.
 *
 * FLAGS.
 * - is_enabled (default true): false means every system skips the object: the renderer, and later collision and
 *   scripts' searches.
 * - is_visible (default true): false means the renderer skips it (its model, its sprite and its light), but it still
 *   exists for collision and interaction (invisible walls, hidden triggers).
 * - mobility (default static): a hint for performance, never a lock. Static is scenery that does not move in play;
 *   movable is anything that does (doors, the player, NPCs, cars). Systems that cache static objects (collision's
 *   static structure, later static shadows) rebuild when a static object moves anyway. Adding a transform driver
 *   component makes an object movable.
 *
 * This module holds no rendering logic: the world renderer reads objects and never writes them.
 */


// Macros.
/** @brief The most bytes an object's name may have (without the terminator). */
#define WORLD_OBJECT_NAME_MAX_LENGTH ((size_t)256)
/** @brief The most entries (tags included) an object's data may hold. */
#define WORLD_OBJECT_DATA_MAX_ENTRIES ((size_t)256)
/** @brief The most bytes an object's data may count (DataMap_GetByteSize): 64 KiB. */
#define WORLD_OBJECT_DATA_MAX_BYTES ((size_t)65536)


// Types.
/**
 * @brief Whether an object moves during play. Saved by its value: never renumber.
 */
typedef enum WorldMobilityEnum
{
    /** @brief Scenery that does not move in play (the default). Systems may cache it. */
    WorldMobility_Static = 0,
    /** @brief Moves in play: doors, elevators, the player, NPCs, cars, later physics objects. */
    WorldMobility_Movable = 1
} WorldMobility;

/**
 * @brief A world object. Underscore-prefixed fields are read-only to code outside the world modules; use the
 *        accessors and setters (or the property table).
 */
typedef struct WorldObjectStruct
{
    /** @brief The world that owns the object (borrowed); set at creation, never changed. */
    World* _world;
    /** @brief Unique id within the world, never 0; assigned at creation, never changed. */
    uint64_t _id;
    /** @brief Owned, NUL-terminated UTF-8 name (need not be unique, at most WORLD_OBJECT_NAME_MAX_LENGTH bytes);
     *  may be NULL. */
    unsigned char* _name;
    /** @brief Position (world space until parenting arrives). */
    Vector3 _position;
    /** @brief Euler XYZ rotation, in radians (degrees in JSON). */
    Vector3 _rotation;
    /** @brief Per-axis scale. */
    Vector3 _scale;
    /** @brief Render tint (tint colour, brightness and opacity, both in [0, 1]). */
    RenderColor _tint;
    /** @brief Whether systems process the object at all (default true). */
    bool _isEnabled;
    /** @brief Whether the renderer draws the object (default true). */
    bool _isVisible;
    /** @brief Whether the object moves in play (default static). */
    WorldMobility _mobility;
    /** @brief Advances (wrapping) on every property change. Runtime only. */
    uint32_t _revision;
    /** @brief One slot per component type: the object's component of that type (world-owned), or NULL. */
    WorldComponent* _components[WORLD_MAX_COMPONENT_TYPES];
    /** @brief The object's tags and custom data (owned); NULL while it has none. */
    DataMap* _data;
    /** @brief Advances (wrapping) on every change of the tags and data. Runtime only. */
    uint32_t _dataRevision;
    /** @brief The world's bookkeeping: the data bytes its total counts for this object. */
    size_t _countedDataBytes;
    /** @brief The world's bookkeeping: how many of the tag index's lists hold this object. */
    uint32_t _indexedTagCount;
} WorldObject;


// Functions: the table.
/**
 * @brief Returns the property table of world objects ("world_object").
 *
 * Its rows: name, position, rotation (an AngleVector3: degrees in JSON), scale, tint, is_enabled, is_visible and
 * mobility ("static" / "movable"). Its AfterSet advances the object's revision. Wire ids start at 3: 1 and 2 are left
 * for the world file's own entries per object (the id and the components).
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldObject_GetPropertyTable(void);


// Functions: identity.
/**
 * @brief Returns the object's unique id (never 0).
 * @param self The object; must not be NULL.
 * @returns The id.
 */
static inline uint64_t WorldObject_GetId(const WorldObject* self)
{
    return self->_id;
}

/**
 * @brief Returns the object's borrowed name (may be NULL); valid until the name is changed or the object removed.
 * @param self The object; must not be NULL.
 * @returns The NUL-terminated UTF-8 name, or NULL if unset.
 */
static inline const unsigned char* WorldObject_GetName(const WorldObject* self)
{
    return self->_name;
}

/**
 * @brief Replaces the object's name with an owned clone of @p name (or NULL to clear it).
 * @param self The object; must not be NULL.
 * @param name Borrowed NUL-terminated UTF-8 name to clone, or NULL to clear the name.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if the name is longer
 *          than WORLD_OBJECT_NAME_MAX_LENGTH bytes or not valid UTF-8 (nothing changes then).
 */
Error WorldObject_SetName(WorldObject* self, const unsigned char* name);

/**
 * @brief Returns a number that changes whenever one of the object's properties changes.
 *
 * Covers every row of the object's table except its tags and data (WorldObject_GetDataRevision), and not its
 * components (each has its own revision). Runtime only: not saved.
 * @param self The object; must not be NULL.
 * @returns The current revision.
 */
static inline uint32_t WorldObject_GetRevision(const WorldObject* self)
{
    return self->_revision;
}


// Functions: tags and data.
/**
 * @brief Returns the object's tags and custom data, for reading and iteration (DataMap_GetEntry).
 *
 * Change them through the world (World_SetData and the others), never through this pointer.
 * @param self The object; must not be NULL.
 * @returns The map (world-owned: valid until the object's data changes), or NULL while it has none.
 */
static inline const DataMap* WorldObject_GetData(const WorldObject* self)
{
    return self->_data;
}

/**
 * @brief Returns a number that changes whenever the object's tags or data change (and only then: a script variable
 *        changing must not make the renderer recompute anything). Runtime only: not saved.
 * @param self The object; must not be NULL.
 * @returns The current data revision.
 */
static inline uint32_t WorldObject_GetDataRevision(const WorldObject* self)
{
    return self->_dataRevision;
}

/**
 * @brief Reports whether the object carries a tag (an entry of type tag; a key holding a value is not a tag).
 * @param self The object; must not be NULL.
 * @param key The tag's key id.
 * @returns true if tagged.
 */
static inline bool WorldObject_HasTag(const WorldObject* self, uint32_t key)
{
    return DataMap_HasTag(self->_data, key);
}


// Functions: transform and tint.
/**
 * @brief Returns the object's position.
 * @param self The object; must not be NULL.
 * @returns The position.
 */
static inline Vector3 WorldObject_GetPosition(const WorldObject* self)
{
    return self->_position;
}

/**
 * @brief Sets the object's position.
 * @param self The object; must not be NULL.
 * @param position The new position; every component must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if any component is
 *          NaN or infinite.
 */
Error WorldObject_SetPosition(WorldObject* self, Vector3 position);

/**
 * @brief Returns the object's Euler XYZ rotation, in radians.
 * @param self The object; must not be NULL.
 * @returns The rotation.
 */
static inline Vector3 WorldObject_GetRotation(const WorldObject* self)
{
    return self->_rotation;
}

/**
 * @brief Sets the object's Euler XYZ rotation, in radians.
 * @param self The object; must not be NULL.
 * @param rotation The new rotation; every component must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if any component is
 *          NaN or infinite.
 */
Error WorldObject_SetRotation(WorldObject* self, Vector3 rotation);

/**
 * @brief Returns the object's per-axis scale.
 * @param self The object; must not be NULL.
 * @returns The scale.
 */
static inline Vector3 WorldObject_GetScale(const WorldObject* self)
{
    return self->_scale;
}

/**
 * @brief Sets the object's per-axis scale.
 * @param self The object; must not be NULL.
 * @param scale The new scale; every component must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if any component is
 *          NaN or infinite.
 */
Error WorldObject_SetScale(WorldObject* self, Vector3 scale);

/**
 * @brief Returns the object's render tint.
 * @param self The object; must not be NULL.
 * @returns The tint.
 */
static inline RenderColor WorldObject_GetTint(const WorldObject* self)
{
    return self->_tint;
}

/**
 * @brief Sets the object's render tint. The renderer draws its opacity as a dither (models, cutout sprites; their
 *        shadows thin with it) or as alpha (blended and additive sprites); at 0 the object is not drawn at all.
 * @param self The object; must not be NULL.
 * @param tint The new tint; Brightness and Opacity must be finite and in [0, 1] (the tint colour's alpha is not part
 *        of the value and is ignored).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if Brightness or
 *          Opacity is not finite or outside [0, 1].
 */
Error WorldObject_SetTint(WorldObject* self, RenderColor tint);


// Functions: flags.
/**
 * @brief Returns whether systems process the object (see the file comment).
 * @param self The object; must not be NULL.
 * @returns true if enabled.
 */
static inline bool WorldObject_IsEnabled(const WorldObject* self)
{
    return self->_isEnabled;
}

/**
 * @brief Enables or disables the object (advances its revision).
 * @param self The object; must not be NULL.
 * @param isEnabled false makes every system skip the object.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldObject_SetEnabled(WorldObject* self, bool isEnabled);

/**
 * @brief Returns whether the renderer draws the object (see the file comment).
 * @param self The object; must not be NULL.
 * @returns true if visible.
 */
static inline bool WorldObject_IsVisible(const WorldObject* self)
{
    return self->_isVisible;
}

/**
 * @brief Shows or hides the object (advances its revision).
 * @param self The object; must not be NULL.
 * @param isVisible false hides the object's model, sprite and light; it still exists for collision and interaction.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldObject_SetVisible(WorldObject* self, bool isVisible);

/**
 * @brief Returns whether the object moves in play (see the file comment).
 * @param self The object; must not be NULL.
 * @returns The mobility.
 */
static inline WorldMobility WorldObject_GetMobility(const WorldObject* self)
{
    return self->_mobility;
}

/**
 * @brief Sets whether the object moves in play (a performance hint, never a lock; advances its revision).
 * @param self The object; must not be NULL.
 * @param mobility WorldMobility_Static or WorldMobility_Movable.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for any other value.
 */
Error WorldObject_SetMobility(WorldObject* self, WorldMobility mobility);


// Functions: components.
/**
 * @brief Returns the object's component of a type (world-owned, borrowed until it is removed), or NULL.
 *
 * Use the concrete type's FromComponent to reach its fields, e.g.
 * `WorldModelComponent_FromComponent(WorldObject_GetComponent(object, WorldComponentType_Model))`.
 * @param self The object; must not be NULL.
 * @param type The component type.
 * @returns The component, or NULL when the object has none of that type (or @p type is out of range).
 */
static inline WorldComponent* WorldObject_GetComponent(const WorldObject* self, WorldComponentType type)
{
    return ((size_t)type < WORLD_MAX_COMPONENT_TYPES) ? self->_components[type] : NULL;
}
