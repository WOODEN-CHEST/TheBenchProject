#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "PropertyDescriptor.h"

// Forward declarations (World.h, WorldObject.h): the registry's hooks receive the world and the owning object.
typedef struct WorldStruct World;
typedef struct WorldObjectStruct WorldObject;


/**
 * @file WorldComponent.h
 * @brief Components: the parts a world object is made of, and the registry that describes each component type.
 *
 * A world object (WorldObject.h) is one concrete type: an id, a name, a transform, a tint and its flags. What it IS
 * comes from its components: a model, a sprite, a light, and later a collider, a character, a screen, an ambient zone.
 * An object carries at most one component of each type; a street lamp is one object with a model and a light.
 *
 * A component is a struct that embeds WorldComponent as its FIRST member (so a pointer to it is also a valid
 * WorldComponent*), described by its own property table (PropertyDescriptor.h) and by one registry entry, a
 * WorldComponentTypeInfo: its type, its name in files and the editor ("model"), its size, its table, whether it drives
 * its object's transform, and its hooks (construct, deconstruct, attach, detach, property changed).
 *
 * LIFETIME. Components exist only inside a world: World_AddComponent constructs one, attaches it to its object and
 * returns it; World_RemoveComponent (or removing the object, or destroying the world) detaches and destroys it. The
 * world keeps a dense array of every component per type (World_GetComponents), which systems such as the renderer
 * iterate instead of walking every object.
 *
 * CHANGES. Every property change advances the component's revision (WorldComponent_GetRevision), whether it comes
 * from the component's own setters or from the table (Properties_Set, a JSON or GHDF read, Properties_CopyTo): every
 * component table's AfterSet hook advances it and then calls the type's OnPropertySet hook with the world, so a
 * component that holds runtime state for a property (the sprite's bound world sprite and its playing animation) stays
 * in step with it.
 *
 * TRANSFORM DRIVERS. A type flagged IsTransformDriver writes its object's transform every tick (later: the character
 * controller, vehicles, rigid bodies). An object carries at most one driver, so two systems never fight over where an
 * object is, and adding one makes the object movable (WorldObject_GetMobility).
 *
 * New component types mean one enum value (WorldComponentType, explicit and never renumbered: world files will save
 * it), one registry entry returned by WorldComponent_GetStandardTypeInfo, a table and the hooks.
 */


// Macros.
/**
 * @brief How many component types a world can hold: every object has one slot per type.
 *
 * Above WorldComponentType_Count to leave room for the types later tasks add (character, screen, ambient zone, ...) and
 * for test-only types.
 */
#define WORLD_MAX_COMPONENT_TYPES ((size_t)16)


// Types.
/**
 * @brief The standard component types. The value indexes the object's component slots and the world's per-type
 *        arrays; values are explicit and never renumbered (world files will save them).
 */
typedef enum WorldComponentTypeEnum
{
    /** @brief A 3D model (WorldModelComponent.h). */
    WorldComponentType_Model = 0,
    /** @brief A 2D sprite showing a world sprite, which plays a sprite animation (WorldSpriteComponent.h). */
    WorldComponentType_Sprite = 1,
    /** @brief A point light (WorldLightComponent.h). */
    WorldComponentType_Light = 2,
    /** @brief A collider: the object's part in collision (WorldColliderComponent.h, T12). */
    WorldComponentType_Collider = 3,
    /** @brief A character: the player or an NPC, a transform driver (WorldCharacterComponent.h, T13). */
    WorldComponentType_Character = 4,
    /** @brief An animator: what plays the skeletal animation of the object's skinned model (WorldAnimatorComponent.h,
     *  T29). */
    WorldComponentType_Animator = 5,
    /** @brief The number of standard types (not a type). */
    WorldComponentType_Count = 6
} WorldComponentType;

/**
 * @brief The base every component embeds as its FIRST member.
 *
 * Read-only to code outside the world modules: use the accessors. The world sets every field when it attaches the
 * component and the owner never changes afterwards.
 */
typedef struct WorldComponentStruct
{
    /** @brief The component's type; set when it is added, never changed. */
    WorldComponentType _type;
    /** @brief The object that carries the component (borrowed; the world owns both). Valid while the component exists:
     *  a component is destroyed before its object. */
    WorldObject* _owner;
    /** @brief Advances (wrapping) on every property change; see the file comment. */
    uint32_t _revision;
    /** @brief Unique among the components its world ever added (from 1); set when it is added, never changed. */
    uint64_t _serial;
} WorldComponent;

/**
 * @brief Initializes a freshly allocated component's own fields (the base is set by the world afterwards).
 *
 * Must call Properties_Construct with the type's table first (it zeroes the whole struct, then writes the defaults)
 * and then set up any runtime state the table does not describe. On failure everything it allocated is released.
 * @param component The component's memory (the type's Size bytes); never NULL.
 * @returns Success, or the error that fails World_AddComponent.
 */
typedef Error (*WorldComponentConstructFunction)(WorldComponent* component);

/**
 * @brief Releases everything a component owns (Properties_Deconstruct and its runtime state); not the memory itself.
 * @param component The component; never NULL.
 */
typedef void (*WorldComponentDeconstructFunction)(WorldComponent* component);

/**
 * @brief Called when a component joins (OnAttach) or leaves (OnDetach) its object in a world.
 *
 * OnAttach runs after the component is in its object's slot and in the world's per-type array; OnDetach runs before
 * it leaves them, while its object and the object's whole hierarchy are still in place: removing an object runs the
 * hooks of its whole subtree (children first) before unlinking or destroying any of it, and destroying a world runs
 * every hook before destroying any object. Neither can fail: a recoverable problem (a missing asset) is logged once
 * and leaves the component in a harmless state (AGENTS.md rule 7). Must not add or remove objects or components, nor
 * change the hierarchy (World_SetParent).
 * @param world The world; never NULL.
 * @param object The owning object; never NULL.
 * @param component The component; never NULL.
 */
typedef void (*WorldComponentHookFunction)(World* world, WorldObject* object, WorldComponent* component);

/**
 * @brief Called after a property of an attached component changed (after its revision advanced).
 *
 * Runs for the component's own setters and for every table write (see the file comment). Must not add or remove
 * objects or components, nor change the hierarchy (World_SetParent).
 * @param world The world; never NULL.
 * @param object The owning object; never NULL.
 * @param component The component; never NULL.
 * @param descriptor The property that changed, or NULL after a JSON or GHDF read or a Properties_CopyTo (anything may
 *        have changed).
 */
typedef void (*WorldComponentPropertySetFunction)(World* world, WorldObject* object, WorldComponent* component,
    const PropertyDescriptor* descriptor);

/**
 * @brief The registry entry of a component type: everything the world needs to create, attach, describe and destroy
 *        one. A static constant of the type's module, handed out by its GetTypeInfo function.
 */
typedef struct WorldComponentTypeInfoStruct
{
    /** @brief The type: also its slot index in objects and worlds (below WORLD_MAX_COMPONENT_TYPES). */
    WorldComponentType Type;
    /** @brief The type's name: the key of its block in world files and its label in the editor, snake_case
     *  (e.g. u8"model"); unique within a world. */
    const unsigned char* Name;
    /** @brief sizeof the concrete struct, which embeds WorldComponent first. */
    size_t Size;
    /** @brief The concrete struct's property table (its StructSize equals Size); its AfterSet must be the world's
     *  component hook (source/WorldInternal.h), so every table write advances the revision. */
    const PropertyTable* Table;
    /** @brief Whether the component writes its object's transform every tick (an object carries at most one). */
    bool IsTransformDriver;
    /** @brief Constructs a component's own fields; never NULL. */
    WorldComponentConstructFunction Construct;
    /** @brief Releases what a component owns; never NULL. */
    WorldComponentDeconstructFunction Deconstruct;
    /** @brief Called once the component is attached; may be NULL. */
    WorldComponentHookFunction OnAttach;
    /** @brief Called before the component is detached; may be NULL. */
    WorldComponentHookFunction OnDetach;
    /** @brief Called after a property changed; may be NULL. */
    WorldComponentPropertySetFunction OnPropertySet;
} WorldComponentTypeInfo;


// Functions.
/**
 * @brief Returns the component's type.
 * @param self The component; must not be NULL.
 * @returns The type.
 */
static inline WorldComponentType WorldComponent_GetType(const WorldComponent* self)
{
    return self->_type;
}

/**
 * @brief Returns the object that carries the component (borrowed; valid while the component exists).
 * @param self The component; must not be NULL.
 * @returns The owning object.
 */
static inline WorldObject* WorldComponent_GetOwner(const WorldComponent* self)
{
    return self->_owner;
}

/**
 * @brief Returns a number that changes whenever one of the component's properties changes.
 *
 * Lets a consumer that derived something from the properties (the renderer's resolved model) notice a change by
 * comparing one integer. Runtime only: not saved, and it restarts at 0 for every new component.
 * @param self The component; must not be NULL.
 * @returns The current revision.
 */
static inline uint32_t WorldComponent_GetRevision(const WorldComponent* self)
{
    return self->_revision;
}

/**
 * @brief Returns a number no other component of the same world ever had (from 1, in the order they were added).
 *
 * Tells a component from the one it replaced: a removed component's memory is often reused by the next one added, so
 * a consumer that keeps component pointers (the collision world) compares serials, not addresses. Runtime only.
 * @param self The component; must not be NULL.
 * @returns The serial.
 */
static inline uint64_t WorldComponent_GetSerial(const WorldComponent* self)
{
    return self->_serial;
}

/**
 * @brief Returns the registry entry of a standard component type.
 *
 * The registry every world uses (World_Construct1/2). Each entry passes WorldComponentTypeInfo_Check.
 * @param type A standard type (below WorldComponentType_Count).
 * @returns The entry (static), or NULL for any other value.
 */
const WorldComponentTypeInfo* WorldComponent_GetStandardTypeInfo(WorldComponentType type);

/**
 * @brief Checks a registry entry: a type below WORLD_MAX_COMPONENT_TYPES, a snake_case name, a size that holds the
 *        base, a table that passes PropertyTable_Check with the same struct size, and the required hooks.
 * @param info The entry; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument naming the type and the broken rule.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldComponentTypeInfo_Check(const WorldComponentTypeInfo* info);
