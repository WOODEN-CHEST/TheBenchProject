#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// TerrainAsset.h for the settings a placement overrides (and the shadow tier and collision layer they hold);
// WorldComponent.h for the component base and the registry entry.
#include "TerrainAsset.h"
#include "WorldComponent.h"
#include "wr/WRError.h"


/**
 * @file WorldTerrainComponent.h
 * @brief The terrain component (type 6, "terrain"): what places a terrain asset (TerrainAsset.h) in a world, and the
 *        settings this placement overrides.
 *
 * The component names a terrain by asset name (NULL places nothing); changing the name swaps the terrain and keeps the
 * object. The object's transform places the terrain's space in the world (static by default: terrain does not move in
 * play; moving it works, and the collision world then moves its chunks, warning once, as for any static collider). It is
 * not a transform driver. The renderer draws its chunks and the collision world makes each chunk a mesh shape (static,
 * or a kinematic mover's when the object is movable); both resolve the asset by name under their own asset users, and
 * again when the name changes.
 *
 * OVERRIDES. Each default of the terrain asset a placement may change is two rows: an override flag
 * ("overrides_<key>", false: the asset's value applies) and the placement's own value next to it, kept (and saved) while
 * the flag is off: has_outline, shadow_tier, casts_shadows, haze, draw_distance, is_draw_distance_fixed,
 * is_collision_enabled, collision_layer and collides_with. draw_distance_multiplier (above 0, default 1) multiplies whichever draw distance
 * applies. WorldTerrainComponent_ResolveSettings is the pure function that gives the settings in effect. Its table is
 * "terrain_component", in that order with "terrain" first; every change advances the component's revision.
 */


// Macros.
/** @brief The most bytes the terrain's asset name may have. */
#define WORLD_TERRAIN_COMPONENT_NAME_MAX_LENGTH ((size_t)128)


// Types.
/**
 * @brief The settings a terrain placement can override, each with its own flag.
 */
typedef enum WorldTerrainSettingEnum
{
    /** @brief has_outline. */
    WorldTerrainSetting_HasOutline,
    /** @brief shadow_tier. */
    WorldTerrainSetting_ShadowTier,
    /** @brief casts_shadows. */
    WorldTerrainSetting_CastsShadows,
    /** @brief draw_distance. */
    WorldTerrainSetting_DrawDistance,
    /** @brief is_draw_distance_fixed. */
    WorldTerrainSetting_IsDrawDistanceFixed,
    /** @brief is_collision_enabled. */
    WorldTerrainSetting_IsCollisionEnabled,
    /** @brief collision_layer. */
    WorldTerrainSetting_CollisionLayer,
    /** @brief collides_with. */
    WorldTerrainSetting_CollidesWith,
    /** @brief haze. */
    WorldTerrainSetting_Haze,
    /** @brief The number of settings (not a setting). */
    WorldTerrainSetting_Count
} WorldTerrainSetting;

/**
 * @brief The terrain component. Underscore-prefixed fields are read-only: use the accessors and setters (or the
 *        table).
 */
typedef struct WorldTerrainComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Owned asset name of the terrain (at most WORLD_TERRAIN_COMPONENT_NAME_MAX_LENGTH bytes); NULL places
     *  nothing. */
    unsigned char* _terrain;
    /** @brief Override flags (false: the asset's value) and the placement's own values, kept while their flag is off. */
    bool _overridesHasOutline;
    bool _hasOutline;
    bool _overridesShadowTier;
    WorldShadowTier _shadowTier;
    bool _overridesCastsShadows;
    bool _castsShadows;
    bool _overridesHaze;
    /** @brief The haze multiplier, in [0, WORLD_MODEL_MAX_HAZE] (default 1: the real air's haze; 0 as if the air were
     *  clear). */
    float _haze;
    bool _overridesDrawDistance;
    /** @brief Metres, above 0 (default TERRAIN_DEFAULT_DRAW_DISTANCE). */
    float _drawDistance;
    /** @brief Multiplies the draw distance in effect; above 0 (default 1). */
    float _drawDistanceMultiplier;
    bool _overridesIsDrawDistanceFixed;
    bool _isDrawDistanceFixed;
    bool _overridesIsCollisionEnabled;
    bool _isCollisionEnabled;
    bool _overridesCollisionLayer;
    CollisionLayer _collisionLayer;
    bool _overridesCollidesWith;
    /** @brief COLLISION_LAYER_BIT per layer (default: the world layer's default mask). */
    uint32_t _collidesWith;
} WorldTerrainComponent;


// Functions.
/**
 * @brief Returns the registry entry of the terrain component type (WorldComponentType_Terrain).
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldTerrainComponent_GetTypeInfo(void);

/**
 * @brief Returns the terrain component's property table ("terrain_component").
 * @returns The table (static).
 */
const PropertyTable* WorldTerrainComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component to a terrain component.
 * @param component The component; may be NULL.
 * @returns The terrain component, or NULL for NULL or a component of another type.
 */
static inline WorldTerrainComponent* WorldTerrainComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Terrain)) ? (WorldTerrainComponent*)component
        : NULL;
}

/**
 * @brief Upcasts a terrain component to its base.
 * @param self The terrain component; must not be NULL.
 * @returns The base.
 */
static inline WorldComponent* WorldTerrainComponent_AsComponent(WorldTerrainComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns the borrowed asset name of the terrain (may be NULL); valid until it changes.
 * @param self The component; must not be NULL.
 * @returns The NUL-terminated UTF-8 asset name, or NULL.
 */
static inline const unsigned char* WorldTerrainComponent_GetTerrain(const WorldTerrainComponent* self)
{
    return self->_terrain;
}

/**
 * @brief Names the terrain to place (a copy is kept).
 * @param self The component; must not be NULL.
 * @param terrain The asset name, or NULL for none.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self; ErrorCode_ArgumentOutOfRange for a name that is not a
 *          valid asset name of at most WORLD_TERRAIN_COMPONENT_NAME_MAX_LENGTH bytes (nothing changes then).
 */
Error WorldTerrainComponent_SetTerrain(WorldTerrainComponent* self, const unsigned char* terrain);

/**
 * @brief Whether a setting is overridden by this placement.
 * @param self The component; must not be NULL.
 * @param setting The setting.
 * @returns The override flag (false for a value outside the enum).
 */
bool WorldTerrainComponent_IsOverridden(const WorldTerrainComponent* self, WorldTerrainSetting setting);

/**
 * @brief Sets a setting's override flag (its own value is kept either way).
 * @param self The component; must not be NULL.
 * @param setting The setting.
 * @param isOverridden Whether the placement's value applies.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self; ErrorCode_ArgumentOutOfRange for a value outside the enum.
 */
Error WorldTerrainComponent_SetOverridden(WorldTerrainComponent* self, WorldTerrainSetting setting, bool isOverridden);

/**
 * @brief The placement's own outline setting (it applies while WorldTerrainSetting_HasOutline is overridden).
 * @param self The component; must not be NULL.
 * @returns true if outlined.
 */
static inline bool WorldTerrainComponent_HasOutline(const WorldTerrainComponent* self)
{
    return self->_hasOutline;
}

/**
 * @brief Sets the placement's own outline setting (it applies while overridden).
 * @param self The component; must not be NULL.
 * @param hasOutline Whether the terrain is outlined.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self.
 */
Error WorldTerrainComponent_SetHasOutline(WorldTerrainComponent* self, bool hasOutline);

/**
 * @brief The placement's own shadow tier (it applies while WorldTerrainSetting_ShadowTier is overridden).
 * @param self The component; must not be NULL.
 * @returns The tier.
 */
static inline WorldShadowTier WorldTerrainComponent_GetShadowTier(const WorldTerrainComponent* self)
{
    return self->_shadowTier;
}

/**
 * @brief Sets the placement's own shadow tier (it applies while overridden).
 * @param self The component; must not be NULL.
 * @param shadowTier The cascade(s) it casts into.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self; ErrorCode_ArgumentOutOfRange for a value outside the enum.
 */
Error WorldTerrainComponent_SetShadowTier(WorldTerrainComponent* self, WorldShadowTier shadowTier);

/**
 * @brief The placement's own "casts shadows" (it applies while WorldTerrainSetting_CastsShadows is overridden).
 * @param self The component; must not be NULL.
 * @returns true if it casts shadows.
 */
static inline bool WorldTerrainComponent_CastsShadows(const WorldTerrainComponent* self)
{
    return self->_castsShadows;
}

/**
 * @brief Sets the placement's own "casts shadows" (it applies while overridden).
 * @param self The component; must not be NULL.
 * @param castsShadows Whether it casts shadows.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self.
 */
Error WorldTerrainComponent_SetCastsShadows(WorldTerrainComponent* self, bool castsShadows);

/**
 * @brief The placement's own haze multiplier (it applies while WorldTerrainSetting_Haze is overridden).
 * @param self The component; must not be NULL.
 * @returns The multiplier: 1 the real air's haze, 0 none.
 */
static inline float WorldTerrainComponent_GetHaze(const WorldTerrainComponent* self)
{
    return self->_haze;
}

/**
 * @brief Sets the placement's own haze multiplier (it applies while overridden): how hazy the terrain looks at a distance.
 * @param self The component; must not be NULL.
 * @param haze Finite, in [0, WORLD_MODEL_MAX_HAZE]: 1 the real air's haze, 0 as if the air were clear.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self; ErrorCode_ArgumentOutOfRange for a value outside the range.
 */
Error WorldTerrainComponent_SetHaze(WorldTerrainComponent* self, float haze);

/**
 * @brief The placement's own draw distance in metres (it applies while WorldTerrainSetting_DrawDistance is
 *        overridden).
 * @param self The component; must not be NULL.
 * @returns The distance.
 */
static inline float WorldTerrainComponent_GetDrawDistance(const WorldTerrainComponent* self)
{
    return self->_drawDistance;
}

/**
 * @brief Sets the placement's own draw distance (it applies while overridden).
 * @param self The component; must not be NULL.
 * @param drawDistance Metres, above 0.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self; ErrorCode_ArgumentOutOfRange for one not above 0 or not
 *          finite.
 */
Error WorldTerrainComponent_SetDrawDistance(WorldTerrainComponent* self, float drawDistance);

/**
 * @brief The multiplier of the draw distance in effect.
 * @param self The component; must not be NULL.
 * @returns The multiplier (above 0).
 */
static inline float WorldTerrainComponent_GetDrawDistanceMultiplier(const WorldTerrainComponent* self)
{
    return self->_drawDistanceMultiplier;
}

/**
 * @brief Sets the multiplier of the draw distance in effect.
 * @param self The component; must not be NULL.
 * @param multiplier Above 0.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self; ErrorCode_ArgumentOutOfRange for one not above 0 or not
 *          finite.
 */
Error WorldTerrainComponent_SetDrawDistanceMultiplier(WorldTerrainComponent* self, float multiplier);

/**
 * @brief The placement's own "fixed draw distance" (it applies while WorldTerrainSetting_IsDrawDistanceFixed is
 *        overridden).
 * @param self The component; must not be NULL.
 * @returns true if fixed.
 */
static inline bool WorldTerrainComponent_IsDrawDistanceFixed(const WorldTerrainComponent* self)
{
    return self->_isDrawDistanceFixed;
}

/**
 * @brief Sets the placement's own "fixed draw distance" (it applies while overridden).
 * @param self The component; must not be NULL.
 * @param isFixed Whether the player's draw_distance_scale leaves the draw distance as it is.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self.
 */
Error WorldTerrainComponent_SetIsDrawDistanceFixed(WorldTerrainComponent* self, bool isFixed);

/**
 * @brief The placement's own "collides" (it applies while WorldTerrainSetting_IsCollisionEnabled is overridden).
 * @param self The component; must not be NULL.
 * @returns true if its chunks collide.
 */
static inline bool WorldTerrainComponent_IsCollisionEnabled(const WorldTerrainComponent* self)
{
    return self->_isCollisionEnabled;
}

/**
 * @brief Sets the placement's own "collides" (it applies while overridden).
 * @param self The component; must not be NULL.
 * @param isEnabled Whether its chunks collide.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self.
 */
Error WorldTerrainComponent_SetIsCollisionEnabled(WorldTerrainComponent* self, bool isEnabled);

/**
 * @brief The placement's own collision layer (it applies while WorldTerrainSetting_CollisionLayer is overridden).
 * @param self The component; must not be NULL.
 * @returns The layer.
 */
static inline CollisionLayer WorldTerrainComponent_GetCollisionLayer(const WorldTerrainComponent* self)
{
    return self->_collisionLayer;
}

/**
 * @brief Sets the placement's own collision layer (it applies while overridden).
 * @param self The component; must not be NULL.
 * @param layer The layer.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self; ErrorCode_ArgumentOutOfRange for a value outside the enum.
 */
Error WorldTerrainComponent_SetCollisionLayer(WorldTerrainComponent* self, CollisionLayer layer);

/**
 * @brief The placement's own mask (it applies while WorldTerrainSetting_CollidesWith is overridden).
 * @param self The component; must not be NULL.
 * @returns COLLISION_LAYER_BIT per layer.
 */
static inline uint32_t WorldTerrainComponent_GetCollidesWith(const WorldTerrainComponent* self)
{
    return self->_collidesWith;
}

/**
 * @brief Sets the placement's own mask (it applies while overridden).
 * @param self The component; must not be NULL.
 * @param mask COLLISION_LAYER_BIT per layer.
 * @returns Success; ErrorCode_IllegalArgument for a NULL self; ErrorCode_ArgumentOutOfRange for a bit that is no layer.
 */
Error WorldTerrainComponent_SetCollidesWith(WorldTerrainComponent* self, uint32_t mask);

/**
 * @brief The settings in effect for this placement: the asset's (@p assetSettings), or the placement's own where their
 *        override flag is on; the draw distance times the multiplier; the mask the placement's, else the asset's, else
 *        the layer in effect's default mask (Collision.HasCollidesWith is then true: the mask is always resolved).
 * @param self The component; must not be NULL.
 * @param assetSettings The terrain's settings (TerrainAsset_GetSettings), or the defaults
 *        (TerrainSettings_CreateDefault) while no terrain is resolved; must not be NULL.
 * @returns The settings in effect.
 */
TerrainSettings WorldTerrainComponent_ResolveSettings(const WorldTerrainComponent* self,
    const TerrainSettings* assetSettings);
