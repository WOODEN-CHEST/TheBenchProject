#pragma once
#include <stdbool.h>
// Color is a Raylib type and part of the light's public data, so this include is unavoidable here.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "WorldObject.h"


/**
 * @file WorldLight.h
 * @brief A point light in a world: a WorldObject with light-specific intensity, color and size.
 *
 * A light uses the base object's Position (and id/name); its rotation, scale and tint are inherited but
 * unused. It adds a color, an intensity, a size (the light's radius, used by the renderer to decide how
 * far it reaches and which objects it affects), and a per-light shadow-casting toggle that lets the
 * renderer skip shadow work for lights that do not need it.
 *
 * Create with WorldLight_Create (which heap-allocates it); ownership normally passes to a World. Because
 * the WorldObject base is the first member, a WorldLight* is also a valid WorldObject*.
 */


// Macros.
/** @brief Minimum accepted light intensity (inclusive). */
#define WORLD_LIGHT_INTENSITY_MIN 0.0f
/** @brief Minimum accepted light size/radius (inclusive). */
#define WORLD_LIGHT_SIZE_MIN 0.0f

/** @brief Default light intensity applied by WorldLight_Create. */
#define WORLD_LIGHT_INTENSITY_DEFAULT 1.0f
/** @brief Default light size/radius applied by WorldLight_Create. */
#define WORLD_LIGHT_SIZE_DEFAULT 1.0f


// Types.
/**
 * @brief A point light: the base world object plus color, intensity, size and a shadow toggle.
 *
 * Underscore-prefixed fields are read-only to outside code; use the accessors. Color and the
 * CastsShadows flag are plain fields with no validation and may be set directly.
 */
typedef struct WorldLightStruct
{
    /** @brief Abstract base (must be first). The light uses its Position, id and name; rotation/scale/tint are unused. */
    WorldObject Base;

    /** @brief Light color; its alpha is ignored (brightness comes from Intensity). */
    Color Color;
    /** @brief Emission intensity; finite and >= WORLD_LIGHT_INTENSITY_MIN. */
    float _intensity;
    /** @brief Light radius/size; finite and >= WORLD_LIGHT_SIZE_MIN. Governs how far the light reaches. */
    float _size;
    /** @brief When true, the renderer may cast shadows from this light; false skips per-light shadow work. */
    bool CastsShadows;
} WorldLight;


// Functions.
/**
 * @brief Creates a heap-allocated point light with default color/intensity/size.
 *
 * Initializes the base (identity transform, id 0), clones @p name, and defaults the color to white,
 * intensity to WORLD_LIGHT_INTENSITY_DEFAULT, size to WORLD_LIGHT_SIZE_DEFAULT and CastsShadows to
 * false. Position the light and adjust its properties with the setters. Release with the light's Destroy
 * (via WorldObject_Destroy) or by handing it to a World.
 * @param name Borrowed NUL-terminated UTF-8 light name to clone, or NULL.
 * @param outLight [out] Receives the new light on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p outLight is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldLight_Create(const unsigned char* name, WorldLight** outLight);

/**
 * @brief Upcasts a light to its WorldObject base.
 * @param self The light; must not be NULL.
 * @returns The embedded base pointer.
 */
static inline WorldObject* WorldLight_AsObject(WorldLight* self)
{
    return &self->Base;
}

/**
 * @brief Returns the light's intensity.
 * @param self The light; must not be NULL.
 * @returns The intensity.
 */
static inline float WorldLight_GetIntensity(const WorldLight* self)
{
    return self->_intensity;
}

/**
 * @brief Sets the light's intensity.
 * @param self The light; must not be NULL.
 * @param intensity The new intensity; must be finite and >= WORLD_LIGHT_INTENSITY_MIN.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p intensity is not finite or is negative.
 */
Error WorldLight_SetIntensity(WorldLight* self, float intensity);

/**
 * @brief Returns the light's size/radius.
 * @param self The light; must not be NULL.
 * @returns The size.
 */
static inline float WorldLight_GetSize(const WorldLight* self)
{
    return self->_size;
}

/**
 * @brief Sets the light's size/radius.
 * @param self The light; must not be NULL.
 * @param size The new size; must be finite and >= WORLD_LIGHT_SIZE_MIN.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p size is not finite or is negative.
 */
Error WorldLight_SetSize(WorldLight* self, float size);
