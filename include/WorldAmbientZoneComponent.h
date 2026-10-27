#pragma once
#include <stdbool.h>
#include <stdint.h>
// Color and Vector3 are raylib types and part of the zone's public data, so this include is unavoidable here.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"


/**
 * @file WorldAmbientZoneComponent.h
 * @brief The ambient zone component: a volume that sets a room's own light (the warm office, the dim storeroom, the
 *        cellar no daylight reaches).
 *
 * A zone is a box (half_extents) or a sphere (radius) in its object's own space, around its object's position: the
 * object's world placement moves, turns and scales it, so a zone that is a child of a building moves with it. Inside it
 * every lit surface takes:
 * - its own ambient light, ambient_color x ambient_intensity: the light a room's walls and fixtures give everything in it
 *   that no lamp models, in the surfaces' light units (the light a white surface shows, as the night's own light);
 * - the sky's ambient times sky_ambient_scale, on top of the sky's visibility (0: no sky light at all, a cellar);
 * - the haze of the part of a view that lies inside it times haze (0: a room's still air; 1: the outside air's).
 * While the camera is inside, the eye's exposure takes exposure_bias stops more (1: twice as bright).
 *
 * Its edge fades in over blend_distance metres inward, from nothing at its surface, so its light never leaks out of it
 * (a building's outside walls stay outside); a surface on or inside it that faces into it takes it whole. Make a room's
 * zone reach blend_distance into its walls, floor and ceiling (to the walls' middle), and everything in the room takes
 * its whole light while the walls' outside faces take none. Where zones overlap the higher priority wins; equal ones
 * blend, each by its weight.
 *
 * A hidden or disabled object's zone is off, as a light is, and so is one under a hidden or disabled ancestor, and one
 * with no inside (a zero half size or radius, or a zero scale).
 *
 * Added with World_AddComponent(world, id, WorldComponentType_AmbientZone, &component); reach its fields with
 * WorldAmbientZoneComponent_FromComponent. Its properties are the rows of its table ("ambient_zone_component"), in this
 * order: shape ("box" / "sphere"), half_extents, radius, blend_distance, ambient_color, ambient_intensity,
 * sky_ambient_scale, exposure_bias, priority and haze. Every change advances the component's revision
 * (WorldComponent_GetRevision).
 */


// Macros.
/** @brief A new zone's ambient intensity: about the light a lit office gives its surfaces, some 500 lux (in its default
 *  colour a white surface shows 0.013 of the game's light units, where a clear noon's sky gives one facing up about
 *  0.07 and the sun 0.25 more, a lamp 0.06 at 2 m, and a moonless night 0.005: a lit room glows from the street at
 *  night). */
#define WORLD_AMBIENT_ZONE_DEFAULT_INTENSITY 0.02f
/** @brief The most stops exposure_bias may add or take (the exposure's whole range is about 15 stops). */
#define WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS 8.0f
/** @brief The most a zone's haze may multiply the haze inside it by (as a placement's). */
#define WORLD_AMBIENT_ZONE_MAX_HAZE 1000.0f


// Types.
/** @brief The shape of an ambient zone, in its object's own space. Values are saved: never renumber. */
typedef enum WorldAmbientZoneShapeEnum
{
    /** @brief A box of half_extents around the object's position, along its axes. */
    WorldAmbientZoneShape_Box = 0,
    /** @brief A sphere of radius around the object's position. */
    WorldAmbientZoneShape_Sphere = 1
} WorldAmbientZoneShape;

/**
 * @brief The ambient zone component. Underscore-prefixed fields are read-only: use the accessors and setters (or the
 *        table).
 */
typedef struct WorldAmbientZoneComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Its shape (default WorldAmbientZoneShape_Box). */
    WorldAmbientZoneShape _shape;
    /** @brief A box's half sizes along the object's axes, in the object's own units, each finite and >= 0 (default
     *  (2, 1.5, 2): a 4 x 3 x 4 room); kept while the shape is a sphere. */
    Vector3 _halfExtents;
    /** @brief A sphere's radius, in the object's own units, finite and >= 0 (default 2); kept while the shape is a box. */
    float _radius;
    /** @brief How far inward from its surface it fades in, in metres, finite and >= 0 (default 0.1; 0 a hard edge). */
    float _blendDistance;
    /** @brief The colour of its own ambient light (default a warm white); its alpha is stored but unused. */
    Color _ambientColor;
    /** @brief The strength of its own ambient light, finite and >= 0 (default WORLD_AMBIENT_ZONE_DEFAULT_INTENSITY). */
    float _ambientIntensity;
    /** @brief Multiplies the sky's ambient inside it, after the sky's visibility, finite and >= 0 (default 1; 0 no sky
     *  light at all). */
    float _skyAmbientScale;
    /** @brief Stops added to the eye's exposure while the camera is inside, in
     *  [-WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS, WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS] (default 0). */
    float _exposureBias;
    /** @brief Where zones overlap the higher priority wins and equal ones blend (default 0). */
    int32_t _priority;
    /** @brief Multiplies the haze of the part of a view inside it, in [0, WORLD_AMBIENT_ZONE_MAX_HAZE] (default 1: the
     *  outside air's; 0 a room's still air). */
    float _haze;
} WorldAmbientZoneComponent;


// Fields.
/** @brief The shapes' names in files ("box", "sphere"). */
extern const PropertyEnumTable WORLD_AMBIENT_ZONE_SHAPE_ENUM;


// Functions.
/**
 * @brief Returns the ambient zone component's registry entry (WorldComponentType_AmbientZone, "ambient_zone").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldAmbientZoneComponent_GetTypeInfo(void);

/**
 * @brief Returns the ambient zone component's property table ("ambient_zone_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldAmbientZoneComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component to an ambient zone component.
 * @param component The component, or NULL.
 * @returns The ambient zone component, or NULL when @p component is NULL or of another type.
 */
static inline WorldAmbientZoneComponent* WorldAmbientZoneComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_AmbientZone))
        ? (WorldAmbientZoneComponent*)component : NULL;
}

/**
 * @brief Upcasts an ambient zone component to its base.
 * @param self The ambient zone component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldAmbientZoneComponent_AsComponent(WorldAmbientZoneComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns the zone's shape.
 * @param self The ambient zone component; must not be NULL.
 * @returns The shape.
 */
static inline WorldAmbientZoneShape WorldAmbientZoneComponent_GetShape(const WorldAmbientZoneComponent* self)
{
    return self->_shape;
}

/**
 * @brief Sets the zone's shape.
 * @param self The ambient zone component; must not be NULL.
 * @param shape A WorldAmbientZoneShape value.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for any other value.
 */
Error WorldAmbientZoneComponent_SetShape(WorldAmbientZoneComponent* self, WorldAmbientZoneShape shape);

/**
 * @brief Returns a box zone's half sizes along its object's axes, in the object's own units.
 * @param self The ambient zone component; must not be NULL.
 * @returns The half extents.
 */
static inline Vector3 WorldAmbientZoneComponent_GetHalfExtents(const WorldAmbientZoneComponent* self)
{
    return self->_halfExtents;
}

/**
 * @brief Sets a box zone's half sizes (kept while the zone is a sphere).
 * @param self The ambient zone component; must not be NULL.
 * @param halfExtents Each component finite and >= 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a negative or
 *          non-finite component.
 */
Error WorldAmbientZoneComponent_SetHalfExtents(WorldAmbientZoneComponent* self, Vector3 halfExtents);

/**
 * @brief Returns a sphere zone's radius, in the object's own units.
 * @param self The ambient zone component; must not be NULL.
 * @returns The radius.
 */
static inline float WorldAmbientZoneComponent_GetRadius(const WorldAmbientZoneComponent* self)
{
    return self->_radius;
}

/**
 * @brief Sets a sphere zone's radius (kept while the zone is a box).
 * @param self The ambient zone component; must not be NULL.
 * @param radius Finite and >= 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a negative or
 *          non-finite radius.
 */
Error WorldAmbientZoneComponent_SetRadius(WorldAmbientZoneComponent* self, float radius);

/**
 * @brief Returns how far inward from its surface the zone fades in, in metres.
 * @param self The ambient zone component; must not be NULL.
 * @returns The distance.
 */
static inline float WorldAmbientZoneComponent_GetBlendDistance(const WorldAmbientZoneComponent* self)
{
    return self->_blendDistance;
}

/**
 * @brief Sets how far inward from its surface the zone fades in.
 * @param self The ambient zone component; must not be NULL.
 * @param blendDistance In metres, finite and >= 0 (0: a hard edge).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a negative or
 *          non-finite distance.
 */
Error WorldAmbientZoneComponent_SetBlendDistance(WorldAmbientZoneComponent* self, float blendDistance);

/**
 * @brief Returns the colour of the zone's own ambient light.
 * @param self The ambient zone component; must not be NULL.
 * @returns The colour (sRGB; its alpha unused).
 */
static inline Color WorldAmbientZoneComponent_GetAmbientColor(const WorldAmbientZoneComponent* self)
{
    return self->_ambientColor;
}

/**
 * @brief Sets the colour of the zone's own ambient light.
 * @param self The ambient zone component; must not be NULL.
 * @param color The colour (sRGB; its alpha unused).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldAmbientZoneComponent_SetAmbientColor(WorldAmbientZoneComponent* self, Color color);

/**
 * @brief Returns the strength of the zone's own ambient light.
 * @param self The ambient zone component; must not be NULL.
 * @returns The intensity.
 */
static inline float WorldAmbientZoneComponent_GetAmbientIntensity(const WorldAmbientZoneComponent* self)
{
    return self->_ambientIntensity;
}

/**
 * @brief Sets the strength of the zone's own ambient light (the light a white surface shows, in the surfaces' units).
 * @param self The ambient zone component; must not be NULL.
 * @param intensity Finite and >= 0 (0: the zone adds no light).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a negative or
 *          non-finite intensity.
 */
Error WorldAmbientZoneComponent_SetAmbientIntensity(WorldAmbientZoneComponent* self, float intensity);

/**
 * @brief Returns what the sky's ambient is multiplied by inside the zone, after the sky's visibility.
 * @param self The ambient zone component; must not be NULL.
 * @returns The scale.
 */
static inline float WorldAmbientZoneComponent_GetSkyAmbientScale(const WorldAmbientZoneComponent* self)
{
    return self->_skyAmbientScale;
}

/**
 * @brief Sets what the sky's ambient is multiplied by inside the zone, after the sky's visibility.
 * @param self The ambient zone component; must not be NULL.
 * @param scale Finite and >= 0 (1 changes nothing, 0 no sky light at all: a cellar).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a negative or
 *          non-finite scale.
 */
Error WorldAmbientZoneComponent_SetSkyAmbientScale(WorldAmbientZoneComponent* self, float scale);

/**
 * @brief Returns the stops added to the eye's exposure while the camera is inside the zone.
 * @param self The ambient zone component; must not be NULL.
 * @returns The stops.
 */
static inline float WorldAmbientZoneComponent_GetExposureBias(const WorldAmbientZoneComponent* self)
{
    return self->_exposureBias;
}

/**
 * @brief Sets the stops added to the eye's exposure while the camera is inside the zone.
 * @param self The ambient zone component; must not be NULL.
 * @param stops In [-WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS, WORLD_AMBIENT_ZONE_MAX_EXPOSURE_BIAS] (1: twice as bright).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value outside the
 *          range.
 */
Error WorldAmbientZoneComponent_SetExposureBias(WorldAmbientZoneComponent* self, float stops);

/**
 * @brief Returns the zone's priority where zones overlap.
 * @param self The ambient zone component; must not be NULL.
 * @returns The priority.
 */
static inline int32_t WorldAmbientZoneComponent_GetPriority(const WorldAmbientZoneComponent* self)
{
    return self->_priority;
}

/**
 * @brief Sets the zone's priority where zones overlap (the higher wins, equal ones blend).
 * @param self The ambient zone component; must not be NULL.
 * @param priority Any value.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldAmbientZoneComponent_SetPriority(WorldAmbientZoneComponent* self, int32_t priority);

/**
 * @brief Returns what the haze of the part of a view inside the zone is multiplied by.
 * @param self The ambient zone component; must not be NULL.
 * @returns The multiplier.
 */
static inline float WorldAmbientZoneComponent_GetHaze(const WorldAmbientZoneComponent* self)
{
    return self->_haze;
}

/**
 * @brief Sets what the haze of the part of a view inside the zone is multiplied by.
 * @param self The ambient zone component; must not be NULL.
 * @param haze In [0, WORLD_AMBIENT_ZONE_MAX_HAZE]: 1 the outside air's haze, 0 a room's still air.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value outside the
 *          range.
 */
Error WorldAmbientZoneComponent_SetHaze(WorldAmbientZoneComponent* self, float haze);
