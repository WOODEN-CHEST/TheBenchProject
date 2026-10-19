#pragma once
#include <stdbool.h>
// Color is a Raylib type and part of the light's public data, so this include is unavoidable here.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"


/**
 * @file WorldLightComponent.h
 * @brief The light component: a point light at its object's position.
 *
 * It has a colour, an intensity, a range (how far the light reaches: the renderer fades it to nothing there and only
 * lights the objects within it) and whether it may cast a shadow. It uses its object's position; the rotation, scale
 * and tint do not affect it. A hidden or disabled object's light is off.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Light, &component); reach its fields with
 * WorldLightComponent_FromComponent. Its properties are the rows of its table ("light_component"): color, intensity,
 * range and casts_shadows. Every change advances the component's revision (WorldComponent_GetRevision).
 */


// Macros.
/** @brief The default intensity of a new light. */
#define WORLD_LIGHT_COMPONENT_DEFAULT_INTENSITY 1.0f
/** @brief The default range of a new light, in world units. */
#define WORLD_LIGHT_COMPONENT_DEFAULT_RANGE 1.0f


// Types.
/**
 * @brief The light component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldLightComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief The light's colour (default white); its alpha is stored but the brightness comes from the intensity. */
    Color _color;
    /** @brief Emission intensity; finite and >= 0 (default WORLD_LIGHT_COMPONENT_DEFAULT_INTENSITY). */
    float _intensity;
    /** @brief How far the light reaches, in world units; finite and >= 0 (default
     *  WORLD_LIGHT_COMPONENT_DEFAULT_RANGE). */
    float _range;
    /** @brief Whether the renderer may cast a shadow from this light (default false). */
    bool _castsShadows;
} WorldLightComponent;


// Functions.
/**
 * @brief Returns the light component's registry entry (WorldComponentType_Light, "light").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldLightComponent_GetTypeInfo(void);

/**
 * @brief Returns the light component's property table ("light_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldLightComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component to a light component.
 * @param component The component, or NULL.
 * @returns The light component, or NULL when @p component is NULL or of another type.
 */
static inline WorldLightComponent* WorldLightComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Light)) ? (WorldLightComponent*)component
        : NULL;
}

/**
 * @brief Upcasts a light component to its base.
 * @param self The light component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldLightComponent_AsComponent(WorldLightComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns the light's colour.
 * @param self The light component; must not be NULL.
 * @returns The colour.
 */
static inline Color WorldLightComponent_GetColor(const WorldLightComponent* self)
{
    return self->_color;
}

/**
 * @brief Sets the light's colour.
 * @param self The light component; must not be NULL.
 * @param color The colour.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldLightComponent_SetColor(WorldLightComponent* self, Color color);

/**
 * @brief Returns the light's intensity.
 * @param self The light component; must not be NULL.
 * @returns The intensity.
 */
static inline float WorldLightComponent_GetIntensity(const WorldLightComponent* self)
{
    return self->_intensity;
}

/**
 * @brief Sets the light's intensity.
 * @param self The light component; must not be NULL.
 * @param intensity The intensity; finite and >= 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p intensity is not
 *          finite or is negative.
 */
Error WorldLightComponent_SetIntensity(WorldLightComponent* self, float intensity);

/**
 * @brief Returns how far the light reaches, in world units.
 * @param self The light component; must not be NULL.
 * @returns The range.
 */
static inline float WorldLightComponent_GetRange(const WorldLightComponent* self)
{
    return self->_range;
}

/**
 * @brief Sets how far the light reaches, in world units.
 * @param self The light component; must not be NULL.
 * @param range The range; finite and >= 0 (0 lights nothing).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p range is not
 *          finite or is negative.
 */
Error WorldLightComponent_SetRange(WorldLightComponent* self, float range);

/**
 * @brief Returns whether the renderer may cast a shadow from this light.
 * @param self The light component; must not be NULL.
 * @returns true if it may.
 */
static inline bool WorldLightComponent_CastsShadows(const WorldLightComponent* self)
{
    return self->_castsShadows;
}

/**
 * @brief Sets whether the renderer may cast a shadow from this light (the nearest such light gets the point shadow).
 * @param self The light component; must not be NULL.
 * @param castsShadows Whether it may.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldLightComponent_SetCastsShadows(WorldLightComponent* self, bool castsShadows);
