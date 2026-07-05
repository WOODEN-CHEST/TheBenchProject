#pragma once
#include <stdint.h>
#include <stdbool.h>
// Vector3 and Color come from Raylib; RenderColor (the tint type) comes from the renderer. All three
// are part of a world object's public data, so these includes are unavoidable in this header.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRError.h"


/**
 * @file WorldObject.h
 * @brief Abstract base shared by every object that lives in a 3D world.
 *
 * A world is composed of world objects: 3D model objects, 2D sprite objects, and lights (see
 * WorldModelObject, WorldSpriteObject, WorldLight). Everything they have in common — a unique id, a
 * (non-unique) name, a spatial transform (position, Euler XYZ rotation, per-axis scale) and a render
 * tint — lives here in WorldObject, the abstract base each concrete type embeds as its FIRST member.
 *
 * A concrete type provides a WorldObjectVTable (currently just a Destroy slot) and calls
 * WorldObject_Construct from its own factory, then WorldObject_Deconstruct from its Destroy. Because
 * the base is the first member, a pointer to any concrete object is also a valid WorldObject*, which is
 * how the World stores and iterates them polymorphically.
 *
 * MUTABILITY. World objects are mutable, but the transform, tint and name are reached through
 * validating setters (which reject non-finite values) and small getters, so the internal layout can
 * change and invalid data cannot be stored. The id is assigned once by the owning World and is
 * read-only thereafter.
 *
 * This module holds no rendering logic: world objects can be created, mutated and destroyed with no
 * renderer present. The world renderer consumes them separately.
 */


// Types.
/**
 * @brief Discriminates the concrete kind of a WorldObject.
 *
 * Set once at construction and never changed; used to safely downcast a WorldObject* to its concrete
 * type.
 */
typedef enum WorldObjectTypeEnum
{
    /** @brief A 3D model object (WorldModelObject). */
    WorldObjectType_Model,
    /** @brief A 2D sprite object (WorldSpriteObject). */
    WorldObjectType_Sprite,
    /** @brief A point light (WorldLight). */
    WorldObjectType_Light
} WorldObjectType;

/**
 * @brief Virtual table of behavior for a concrete world object.
 *
 * A concrete type supplies one static instance. The self parameter is the concrete object (recovered
 * from void* without a cast, per the project's OOP convention).
 */
typedef struct WorldObjectVTableStruct
{
    /**
     * @brief Frees the concrete object and everything it owns.
     *
     * Must release the concrete type's own resources, call WorldObject_Deconstruct on the embedded
     * base, and free the object allocation itself. Called by the owning World when the object is
     * removed or the world is torn down.
     * @param self The concrete world object.
     */
    void (*Destroy)(void* self);
} WorldObjectVTable;

/**
 * @brief Abstract base of every world object: identity plus spatial transform and tint.
 *
 * Embed as the FIRST member of a concrete world object. Underscore-prefixed fields are read-only to
 * code outside this module; use the WorldObject_* getters/setters to access them.
 */
typedef struct WorldObjectStruct
{
    /** @brief Behavior table for this object; set by WorldObject_Construct. Never NULL afterwards. */
    const WorldObjectVTable* VTable;
    /** @brief The concrete kind of this object; set at construction, never changed. */
    WorldObjectType Type;

    /** @brief Unique id within the owning world; 0 means "unassigned". Assigned by the World, then read-only. */
    uint64_t _id;
    /** @brief Owned, NUL-terminated UTF-8 name (need not be unique); may be NULL. */
    unsigned char* _name;

    /** @brief World-space position. */
    Vector3 _position;
    /** @brief Euler XYZ rotation, in radians. */
    Vector3 _rotation;
    /** @brief Per-axis scale. */
    Vector3 _scale;
    /** @brief Render tint (tint color + brightness + opacity). */
    RenderColor _tint;
} WorldObject;


// Base lifecycle (called by concrete types, not end users).
/**
 * @brief Initializes the abstract base of a world object.
 *
 * Sets the vtable and type, clones @p name into an owned copy, and defaults the transform to identity
 * (position 0, rotation 0, scale 1,1,1), the tint to opaque white and the id to 0 (unassigned). Call
 * first inside a concrete type's factory, then fill the concrete fields. Release with
 * WorldObject_Deconstruct.
 * @param self The base to initialize (usually &concrete->Base); must not be NULL.
 * @param vtable The concrete type's behavior table; must not be NULL with a non-NULL Destroy slot.
 * @param type The concrete kind being constructed.
 * @param name Borrowed NUL-terminated UTF-8 name to clone, or NULL for no name.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p vtable (or its Destroy) is NULL.
 */
Error WorldObject_Construct(WorldObject* self, const WorldObjectVTable* vtable, WorldObjectType type,
    const unsigned char* name);

/**
 * @brief Releases the base's owned resources (its name).
 *
 * Frees the owned name. Does NOT free @p self or any concrete resources — the concrete Destroy owns
 * those and calls this as part of its teardown. Safe on NULL.
 * @param self The base to release, or NULL.
 */
void WorldObject_Deconstruct(WorldObject* self);

/**
 * @brief Destroys a world object through its vtable (frees it and everything it owns).
 * @param self The object; must not be NULL. Must not be used afterwards.
 */
static inline void WorldObject_Destroy(WorldObject* self)
{
    self->VTable->Destroy(self);
}


// Identity.
/**
 * @brief Returns the object's unique world id (0 if not yet added to a world).
 * @param self The object; must not be NULL.
 * @returns The id.
 */
static inline uint64_t WorldObject_GetId(const WorldObject* self)
{
    return self->_id;
}

/**
 * @brief Assigns the object's world id. Intended for the owning World only.
 *
 * @param self The object; must not be NULL.
 * @param id The id to assign; must not be 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p id is 0.
 */
Error WorldObject_SetId(WorldObject* self, uint64_t id);

/**
 * @brief Returns the object's concrete kind.
 * @param self The object; must not be NULL.
 * @returns The type discriminator.
 */
static inline WorldObjectType WorldObject_GetType(const WorldObject* self)
{
    return self->Type;
}

/**
 * @brief Returns the object's borrowed name (may be NULL); valid until the name is changed or freed.
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
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldObject_SetName(WorldObject* self, const unsigned char* name);


// Transform and tint (validating setters reject non-finite values).
/**
 * @brief Returns the object's world-space position.
 * @param self The object; must not be NULL.
 * @returns The position.
 */
static inline Vector3 WorldObject_GetPosition(const WorldObject* self)
{
    return self->_position;
}

/**
 * @brief Sets the object's world-space position.
 * @param self The object; must not be NULL.
 * @param position The new position; every component must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if any
 *          component is NaN or infinite.
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
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if any
 *          component is NaN or infinite.
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
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if any
 *          component is NaN or infinite.
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
 * @brief Sets the object's render tint.
 * @param self The object; must not be NULL.
 * @param tint The new tint; its Brightness and Opacity must be finite (they are clamped to [0;1] only
 *        when the color is resolved for rendering, so out-of-range-but-finite values are accepted here).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          Brightness or Opacity is NaN or infinite.
 */
Error WorldObject_SetTint(WorldObject* self, RenderColor tint);
