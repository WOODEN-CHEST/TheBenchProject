#pragma once
#include <stdbool.h>
#include <stdint.h>
// Color is a Raylib type and part of the light's public data, so this include is unavoidable here.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"
#include "WorldLightFlicker.h"


/**
 * @file WorldLightComponent.h
 * @brief The light component: a point light or a spot light at its object's world position.
 *
 * It has a colour, an intensity, a range (how far the light reaches: the renderer fades it to nothing there and only
 * lights the objects within it), whether it may cast a shadow, whether it is on, and a flicker (WorldLightFlicker.h). A
 * point shines every way; a spot shines along its object's world forward (+Z after its rotation, through its parents),
 * fully within its inner angle of that axis and fading to nothing at its outer angle. It uses its object's world
 * position and, for a spot, its forward (a lamp carries its light); the scale (beyond the forward's direction) and the
 * tint do not affect it. A hidden or disabled object's light is off, and so is one under a hidden or disabled ancestor;
 * a light switched off (is_on) keeps every other setting.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Light, &component); reach its fields with
 * WorldLightComponent_FromComponent. Its properties are the rows of its table ("light_component"): is_on, type, color,
 * intensity, range, inner_angle, outer_angle, casts_shadows, flicker, flicker_amount and flicker_speed. Every change
 * advances the component's revision (WorldComponent_GetRevision).
 *
 * One rule spans two rows, which a table cannot hold: the inner angle is at most the outer. The table checks each angle
 * on its own (a file or a script may set them in any order), WorldLightComponent_SetCone checks the pair, and
 * WorldLightComponent_GetCone gives the cone in effect: an inner angle past the outer counts as the outer (a hard edge).
 */


// Macros.
/** @brief The default intensity of a new light. */
#define WORLD_LIGHT_COMPONENT_DEFAULT_INTENSITY 1.0f
/** @brief The default range of a new light, in world units. */
#define WORLD_LIGHT_COMPONENT_DEFAULT_RANGE 1.0f
/** @brief The default inner and outer angles of a spot's cone, in radians (25 and 35 degrees from its axis). */
#define WORLD_LIGHT_COMPONENT_DEFAULT_INNER_ANGLE ((float)(25.0 * 3.14159265358979323846 / 180.0))
#define WORLD_LIGHT_COMPONENT_DEFAULT_OUTER_ANGLE ((float)(35.0 * 3.14159265358979323846 / 180.0))
/** @brief A cone's angles lie strictly between 0 and this, in radians (90 degrees: a spot never lights behind itself). */
#define WORLD_LIGHT_COMPONENT_MAX_CONE_ANGLE (3.14159265358979323846 / 2.0)
/** @brief The default depth and tempo of a new light's flicker (it flickers only once its flicker kind is set). */
#define WORLD_LIGHT_COMPONENT_DEFAULT_FLICKER_AMOUNT 0.3f
#define WORLD_LIGHT_COMPONENT_DEFAULT_FLICKER_SPEED 1.0f


// Types.
/** @brief The kinds of light. Values are saved: never renumber. */
typedef enum WorldLightTypeEnum
{
    /** @brief Shines every way from its position. */
    WorldLightType_Point = 0,
    /** @brief Shines in a cone along its object's world forward (+Z after its rotation). */
    WorldLightType_Spot = 1
} WorldLightType;

/** @brief A spot's cone as it lights: half-angles from its axis, in radians, with InnerAngle <= OuterAngle. */
typedef struct WorldLightConeStruct
{
    /** @brief Within this angle of the axis the spot lights fully. */
    float InnerAngle;
    /** @brief Past this angle it lights nothing; between the two its light fades smoothly. */
    float OuterAngle;
} WorldLightCone;

/**
 * @brief The light component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldLightComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Whether the light is on (default true): off it lights nothing and casts no shadow. */
    bool _isOn;
    /** @brief A point or a spot (default WorldLightType_Point). */
    WorldLightType _type;
    /** @brief The light's colour (default white); its alpha is stored but the brightness comes from the intensity. */
    Color _color;
    /** @brief Emission intensity; finite and >= 0 (default WORLD_LIGHT_COMPONENT_DEFAULT_INTENSITY). */
    float _intensity;
    /** @brief How far the light reaches, in world units; finite and >= 0 (default
     *  WORLD_LIGHT_COMPONENT_DEFAULT_RANGE). */
    float _range;
    /** @brief A spot's inner and outer angles from its axis, in radians, each strictly between 0 and
     *  WORLD_LIGHT_COMPONENT_MAX_CONE_ANGLE (defaults 25 and 35 degrees); kept while the light is a point. The cone in
     *  effect is WorldLightComponent_GetCone's. */
    float _innerAngle;
    float _outerAngle;
    /** @brief Whether the renderer may cast a shadow from this light (default false). */
    bool _castsShadows;
    /** @brief How it flickers (default WorldLightFlicker_None), how deep (in [0, 1], default
     *  WORLD_LIGHT_COMPONENT_DEFAULT_FLICKER_AMOUNT: its intensity times a value in [1 - amount, 1]) and how fast (in
     *  [0, WORLD_LIGHT_FLICKER_MAX_SPEED], default WORLD_LIGHT_COMPONENT_DEFAULT_FLICKER_SPEED). */
    WorldLightFlicker _flicker;
    float _flickerAmount;
    float _flickerSpeed;
} WorldLightComponent;


// Fields.
/** @brief The light types' names in files ("point", "spot"). */
extern const PropertyEnumTable WORLD_LIGHT_TYPE_ENUM;
/** @brief The flicker kinds' names in files ("none", "candle", "fluorescent", "broken"). */
extern const PropertyEnumTable WORLD_LIGHT_FLICKER_ENUM;


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
 * @brief Returns whether the light is on.
 * @param self The light component; must not be NULL.
 * @returns true if it is on.
 */
static inline bool WorldLightComponent_IsOn(const WorldLightComponent* self)
{
    return self->_isOn;
}

/**
 * @brief Switches the light on or off (a script's lamp switch): off it lights nothing and casts no shadow; its other
 *        settings stay as they are.
 * @param self The light component; must not be NULL.
 * @param isOn Whether it is on.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldLightComponent_SetIsOn(WorldLightComponent* self, bool isOn);

/**
 * @brief Returns the kind of light.
 * @param self The light component; must not be NULL.
 * @returns A point or a spot.
 */
static inline WorldLightType WorldLightComponent_GetType(const WorldLightComponent* self)
{
    return self->_type;
}

/**
 * @brief Sets the kind of light.
 * @param self The light component; must not be NULL.
 * @param type WorldLightType_Point or WorldLightType_Spot.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p type is not a
 *          WorldLightType.
 */
Error WorldLightComponent_SetType(WorldLightComponent* self, WorldLightType type);

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
 * @brief Returns a spot's inner angle as set (its row's value, which may lie past the outer angle; the cone in effect is
 *        WorldLightComponent_GetCone's).
 * @param self The light component; must not be NULL.
 * @returns The angle from the axis, in radians.
 */
static inline float WorldLightComponent_GetInnerAngle(const WorldLightComponent* self)
{
    return self->_innerAngle;
}

/**
 * @brief Returns a spot's outer angle.
 * @param self The light component; must not be NULL.
 * @returns The angle from the axis, in radians.
 */
static inline float WorldLightComponent_GetOuterAngle(const WorldLightComponent* self)
{
    return self->_outerAngle;
}

/**
 * @brief Returns the cone in effect: the outer angle, and the inner angle, or the outer where the inner lies past it (a
 *        hard edge). Pure; meaningful for a spot (a point keeps its angles unused).
 * @param self The light component; must not be NULL.
 * @returns The cone, InnerAngle <= OuterAngle.
 */
WorldLightCone WorldLightComponent_GetCone(const WorldLightComponent* self);

/**
 * @brief Sets a spot's cone: full light within @p innerAngle of its axis, fading to nothing at @p outerAngle.
 * @param self The light component; must not be NULL.
 * @param innerAngle The inner angle, in radians: finite, strictly between 0 and WORLD_LIGHT_COMPONENT_MAX_CONE_ANGLE,
 *        and at most @p outerAngle (equal: a hard edge).
 * @param outerAngle The outer angle, in radians: finite and strictly between 0 and WORLD_LIGHT_COMPONENT_MAX_CONE_ANGLE.
 * @returns Success (both set); ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if an angle
 *          breaks its rule or @p innerAngle exceeds @p outerAngle (nothing set).
 */
Error WorldLightComponent_SetCone(WorldLightComponent* self, float innerAngle, float outerAngle);

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
 * @brief Sets whether the renderer may cast a shadow from this light (a point's: the nearest such point gets the cube
 *        shadow; a spot's: the strongest few such spots around the camera get a shadow map each).
 * @param self The light component; must not be NULL.
 * @param castsShadows Whether it may.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldLightComponent_SetCastsShadows(WorldLightComponent* self, bool castsShadows);

/**
 * @brief Returns how the light flickers.
 * @param self The light component; must not be NULL.
 * @returns The kind of flicker.
 */
static inline WorldLightFlicker WorldLightComponent_GetFlicker(const WorldLightComponent* self)
{
    return self->_flicker;
}

/**
 * @brief Sets how the light flickers.
 * @param self The light component; must not be NULL.
 * @param flicker The kind of flicker.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p flicker is not a
 *          WorldLightFlicker.
 */
Error WorldLightComponent_SetFlicker(WorldLightComponent* self, WorldLightFlicker flicker);

/**
 * @brief Returns how deep the flicker goes.
 * @param self The light component; must not be NULL.
 * @returns The amount, in [0, 1].
 */
static inline float WorldLightComponent_GetFlickerAmount(const WorldLightComponent* self)
{
    return self->_flickerAmount;
}

/**
 * @brief Sets how deep the flicker goes: the intensity is multiplied by a value in [1 - @p amount, 1].
 * @param self The light component; must not be NULL.
 * @param amount The amount, in [0, 1].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p amount is not
 *          finite or lies outside [0, 1].
 */
Error WorldLightComponent_SetFlickerAmount(WorldLightComponent* self, float amount);

/**
 * @brief Returns the flicker's tempo.
 * @param self The light component; must not be NULL.
 * @returns The speed, in [0, WORLD_LIGHT_FLICKER_MAX_SPEED].
 */
static inline float WorldLightComponent_GetFlickerSpeed(const WorldLightComponent* self)
{
    return self->_flickerSpeed;
}

/**
 * @brief Sets the flicker's tempo: 1 its kind's own, 2 twice as fast, 0 held still.
 * @param self The light component; must not be NULL.
 * @param speed The speed, in [0, WORLD_LIGHT_FLICKER_MAX_SPEED].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p speed is not finite
 *          or lies outside that range.
 */
Error WorldLightComponent_SetFlickerSpeed(WorldLightComponent* self, float speed);

/**
 * @brief Returns how bright the light's flicker makes it at a moment (WorldLightFlicker_GetFactor with its settings and
 *        its object's id as the seed): at the renderer's lights' clock (WorldRenderer_GetLightSeconds), the value its
 *        last render used. Pure.
 * @param self The light component; must not be NULL.
 * @param seconds The time, in seconds.
 * @returns The multiplier of its intensity, in [1 - flicker_amount, 1] (1 without a flicker).
 */
float WorldLightComponent_GetFlickerFactor(const WorldLightComponent* self, double seconds);
