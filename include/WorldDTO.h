#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "Renderer.h"
#include "WorldObject.h"
#include "WorldEnvironment.h"


/**
 * @file WorldDTO.h
 * @brief The on-disk data-transfer representation of a world: a plain snapshot of everything persistent.
 *
 * The World and its live objects carry runtime machinery (vtables, mutation logic). The WorldDTO is the
 * flat, plain-data mirror of only the PERSISTENT parts — the world name, the environment settings, and,
 * for every object, its id, name, transform, tint and type-specific fields. It is what a save file is
 * built from and what a load produces before being applied to a live World.
 *
 * A WorldDTO owns its strings (they are cloned in), so it is a standalone snapshot independent of the
 * World it came from. Build one from a live world with WorldDTO_FromWorld and rebuild a live world from
 * one with WorldDTO_ApplyToWorld. Serializing a DTO to binary is a separate concern (see WorldEncoder,
 * which turns a DTO into a GHDF compound).
 *
 * The DTO is deliberately expandable: a per-object union carries the fields specific to each object type,
 * so new fields (or new object types) are added here alongside the live types without disturbing the rest.
 */


// Forward declarations.
/** @brief A live world; full type in World.h. Referenced here only by pointer for the conversions. */
typedef struct WorldStruct World;


// Types.
/**
 * @brief The persistent data of a single world object, tagged by type.
 *
 * Common fields (id, name, transform, tint) apply to every object; the Data union carries the fields
 * specific to the object's Type. Strings are owned by the DTO. For a light, the base rotation/scale/tint
 * are stored for lossless round-tripping even though the live light ignores them.
 */
typedef struct WorldObjectDTOStruct
{
    /** @brief The object's concrete kind; selects the active Data union member. */
    WorldObjectType Type;
    /** @brief The object's saved id (non-zero). */
    uint64_t Id;
    /** @brief Owned, NUL-terminated UTF-8 object name; may be NULL. */
    unsigned char* Name;
    /** @brief World-space position. */
    Vector3 Position;
    /** @brief Euler XYZ rotation, in radians. */
    Vector3 Rotation;
    /** @brief Per-axis scale. */
    Vector3 Scale;
    /** @brief Render tint. */
    RenderColor Tint;

    /** @brief Type-specific persistent fields; the active member is chosen by Type. */
    union
    {
        /** @brief Valid when Type is WorldObjectType_Model. */
        struct
        {
            /** @brief Owned, NUL-terminated UTF-8 model asset name; may be NULL. */
            unsigned char* ModelAssetName;
            /** @brief Whether the object draws a 1-pixel outline. */
            bool HasOutline;
            /** @brief Whether the object is exempt from the pixelation pass. */
            bool OmitPixelation;
        } Model;

        /** @brief Valid when Type is WorldObjectType_Sprite. */
        struct
        {
            /** @brief Owned, NUL-terminated UTF-8 sprite animation asset name; may be NULL. */
            unsigned char* SpriteAnimationAssetName;
            /** @brief Whether the object draws a 1-pixel outline. */
            bool HasOutline;
            /** @brief Whether the object is exempt from the pixelation pass. */
            bool OmitPixelation;
        } Sprite;

        /** @brief Valid when Type is WorldObjectType_Light. */
        struct
        {
            /** @brief Light color; alpha ignored. */
            Color Color;
            /** @brief Emission intensity. */
            float Intensity;
            /** @brief Light radius/size. */
            float Size;
            /** @brief Whether the light casts shadows. */
            bool CastsShadows;
        } Light;
    } Data;
} WorldObjectDTO;

/**
 * @brief The persistent snapshot of a whole world: name, environment, and every object's data.
 *
 * A value type the caller holds. Construct with WorldDTO_Construct (or WorldDTO_FromWorld) and release
 * with WorldDTO_Deconstruct, which frees all owned strings. Underscore-prefixed fields are internal.
 */
typedef struct WorldDTOStruct
{
    /** @brief Owned, NUL-terminated UTF-8 world name; may be NULL. */
    unsigned char* Name;
    /** @brief The saved id counter (the next object id the world would hand out); restored on apply. */
    uint64_t NextObjectId;
    /** @brief The world's environment/graphics settings (plain data). */
    WorldEnvironment Environment;
    /** @brief Owned buffer of WorldObjectDTO records (by value). */
    GenericBuffer _objects;
} WorldDTO;


// Lifecycle.
/**
 * @brief Initializes an empty world DTO (no objects, default environment, no name).
 * @param self The DTO to initialize; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldDTO_Construct(WorldDTO* self);

/**
 * @brief Releases a world DTO and all owned strings within it (including each object's strings).
 * @param self The DTO to release, or NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldDTO_Deconstruct(WorldDTO* self);


// Conversion.
/**
 * @brief Builds a DTO snapshot from a live world (initializes @p outDTO, then fills it).
 *
 * Clones the world name, copies the environment, and appends one WorldObjectDTO per world object (with
 * cloned strings). @p outDTO is initialized by this call; do not pre-construct it. On failure @p outDTO
 * is left deconstructed (owning nothing).
 * @param world The world to snapshot; must not be NULL.
 * @param outDTO [out] Receives the initialized, populated DTO. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p world or @p outDTO is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldDTO_FromWorld(World* world, WorldDTO* outDTO);

/**
 * @brief Rebuilds a live world from a DTO: sets its name/environment and re-creates every object.
 *
 * The target world should be empty; objects are added with their saved ids (WorldDTO_ApplyToWorld fails
 * if an id collides). Object transform/tint values are validated as they are applied, so a corrupt DTO
 * (e.g. non-finite values) is rejected. On failure, objects added before the failing one remain in the
 * world (the caller should discard the world).
 * @param dto The source DTO; must not be NULL.
 * @param world The target world (should be empty); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p dto or @p world is NULL, or a stored object has an
 *          unknown type; propagates validation and insertion errors.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldDTO_ApplyToWorld(const WorldDTO* dto, World* world);

/**
 * @brief Returns the number of object records in the DTO.
 * @param self The DTO; may be NULL (returns 0).
 * @returns The object record count.
 */
size_t WorldDTO_GetObjectCount(const WorldDTO* self);
