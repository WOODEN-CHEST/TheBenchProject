#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"
#include "WorldModel.h"


/**
 * @file WorldModelComponent.h
 * @brief The model component: a world model, referenced by its asset name, drawn with its object's transform and tint,
 *        and the settings this placement overrides.
 *
 * The component names a WORLD MODEL (WorldModel.h: the LOD models, draw distance, render defaults and variants of a
 * placeable thing); the world renderer resolves it from the asset manager by name (under its own asset user) and
 * resolves it again when the name changes, so changing the name swaps the model and keeps the object. The component
 * itself holds no asset.
 *
 * OVERRIDES. Each setting a placement can override (WorldModelSetting) is a pair of properties: an override flag,
 * "overrides_" + the setting's key (false by default: the world model's value applies), and the placement's own value.
 * The value is kept, and saved, while its flag is off, so switching the flag back on restores it; there are no sentinel
 * values and no "inherit" states (the owner's rule, G17). WorldModelComponent_ResolveSettings turns the world model's
 * settings and the overrides into the settings that apply. The draw distance also has a plain multiplier (1 changes
 * nothing, 2 = seen twice as far), which applies to whichever distance is in effect.
 *
 * VARIANTS. With uses_variant on, the object is drawn with the world model's variant of the given name (another look of
 * the same meshes); an unknown name draws the model's own materials, and the renderer warns once.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Model, &component); reach its fields with
 * WorldModelComponent_FromComponent. Its properties are the rows of its table ("model_component"), in this order:
 * world_model, uses_variant, variant, overrides_has_outline, has_outline, overrides_omit_pixelation, omit_pixelation,
 * overrides_shadow_tier, shadow_tier ("both" / "near" / "far"), overrides_casts_shadows, casts_shadows,
 * overrides_draw_distance, draw_distance, draw_distance_multiplier, overrides_is_draw_distance_fixed and
 * is_draw_distance_fixed. Every change advances the component's revision (WorldComponent_GetRevision).
 */


// Macros.
/** @brief The most bytes a world model's asset name may have (without the terminator). */
#define WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH ((size_t)128)


// Types.
/**
 * @brief The settings a placement can override, each with its own flag (WorldModelComponent_IsOverridden).
 */
typedef enum WorldModelSettingEnum
{
    /** @brief The outline (has_outline, flag overrides_has_outline). */
    WorldModelSetting_HasOutline,
    /** @brief Drawing at full resolution (omit_pixelation, flag overrides_omit_pixelation). */
    WorldModelSetting_OmitPixelation,
    /** @brief The shadow cascades (shadow_tier, flag overrides_shadow_tier). */
    WorldModelSetting_ShadowTier,
    /** @brief Casting shadows (casts_shadows, flag overrides_casts_shadows). */
    WorldModelSetting_CastsShadows,
    /** @brief The draw distance (draw_distance, flag overrides_draw_distance). */
    WorldModelSetting_DrawDistance,
    /** @brief Keeping the draw distance fixed against the quality setting (is_draw_distance_fixed, flag
     *  overrides_is_draw_distance_fixed). */
    WorldModelSetting_IsDrawDistanceFixed,
    /** @brief The number of settings; not a setting. */
    WorldModelSetting_Count
} WorldModelSetting;

/**
 * @brief The model component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldModelComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Owned asset name of the world model to draw (at most WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH bytes); NULL
     *  draws nothing (the default). */
    unsigned char* _worldModel;
    /** @brief Whether the object is drawn with the variant named by _variant (default false: the model's own look). */
    bool _usesVariant;
    /** @brief Owned name of the variant used while _usesVariant is on (at most WORLD_MODEL_VARIANT_NAME_MAX_LENGTH
     *  bytes; default NULL); kept while it is off. */
    unsigned char* _variant;
    /** @brief Whether _hasOutline replaces the world model's outline setting (default false). */
    bool _overridesHasOutline;
    /** @brief This placement's outline setting (default true). */
    bool _hasOutline;
    /** @brief Whether _omitPixelation replaces the world model's (default false). */
    bool _overridesOmitPixelation;
    /** @brief This placement's "drawn at full resolution over the pixelated image" (default false). */
    bool _omitPixelation;
    /** @brief Whether _shadowTier replaces the world model's (default false). */
    bool _overridesShadowTier;
    /** @brief This placement's shadow cascade(s) (default WorldShadowTier_Both). */
    WorldShadowTier _shadowTier;
    /** @brief Whether _castsShadows replaces the world model's (default false). */
    bool _overridesCastsShadows;
    /** @brief This placement's "casts shadows" (default true). */
    bool _castsShadows;
    /** @brief Whether _drawDistance replaces the world model's draw distance (default false). */
    bool _overridesDrawDistance;
    /** @brief This placement's draw distance, in metres, above 0 (default WORLD_MODEL_DEFAULT_DRAW_DISTANCE). */
    float _drawDistance;
    /** @brief Multiplies the draw distance in effect (the world model's or this placement's), above 0 (default 1; 2 =
     *  seen twice as far). A plain setting, not an override. */
    float _drawDistanceMultiplier;
    /** @brief Whether _isDrawDistanceFixed replaces the world model's (default false). */
    bool _overridesIsDrawDistanceFixed;
    /** @brief This placement's "the quality setting leaves the draw distance as it is" (default false). */
    bool _isDrawDistanceFixed;
} WorldModelComponent;


// Functions.
/**
 * @brief Returns the model component's registry entry (WorldComponentType_Model, "model").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldModelComponent_GetTypeInfo(void);

/**
 * @brief Returns the model component's property table ("model_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldModelComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component to a model component.
 * @param component The component, or NULL.
 * @returns The model component, or NULL when @p component is NULL or of another type.
 */
static inline WorldModelComponent* WorldModelComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Model)) ? (WorldModelComponent*)component
        : NULL;
}

/**
 * @brief Upcasts a model component to its base.
 * @param self The model component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldModelComponent_AsComponent(WorldModelComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns the borrowed asset name of the world model (may be NULL); valid until it changes.
 * @param self The model component; must not be NULL.
 * @returns The NUL-terminated UTF-8 asset name, or NULL.
 */
static inline const unsigned char* WorldModelComponent_GetWorldModel(const WorldModelComponent* self)
{
    return self->_worldModel;
}

/**
 * @brief Sets the world model to draw (a clone of @p worldModel; NULL draws nothing). The renderer resolves the new
 *        world model at its next frame; the object keeps its id, tags, data and children.
 * @param self The model component; must not be NULL.
 * @param worldModel Borrowed asset name, or NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p worldModel is not
 *          a valid asset name of at most WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH bytes (nothing changes then).
 */
Error WorldModelComponent_SetWorldModel(WorldModelComponent* self, const unsigned char* worldModel);

/**
 * @brief Returns whether the object is drawn with a variant of its world model.
 * @param self The model component; must not be NULL.
 * @returns true while the variant named by WorldModelComponent_GetVariant is used.
 */
static inline bool WorldModelComponent_UsesVariant(const WorldModelComponent* self)
{
    return self->_usesVariant;
}

/**
 * @brief Switches the variant on or off (off: the world model's own materials). The variant's name is kept either way.
 * @param self The model component; must not be NULL.
 * @param usesVariant Whether to use the variant.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelComponent_SetUsesVariant(WorldModelComponent* self, bool usesVariant);

/**
 * @brief Returns the borrowed name of the variant (may be NULL); valid until it changes.
 * @param self The model component; must not be NULL.
 * @returns The NUL-terminated UTF-8 name, or NULL.
 */
static inline const unsigned char* WorldModelComponent_GetVariant(const WorldModelComponent* self)
{
    return self->_variant;
}

/**
 * @brief Sets the name of the variant used while WorldModelComponent_UsesVariant is on (a clone; NULL for none).
 * @param self The model component; must not be NULL.
 * @param variant Borrowed name, or NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p variant is longer
 *          than WORLD_MODEL_VARIANT_NAME_MAX_LENGTH bytes or not valid UTF-8 (nothing changes then).
 */
Error WorldModelComponent_SetVariant(WorldModelComponent* self, const unsigned char* variant);

/**
 * @brief Returns whether this placement overrides one of its world model's settings (the setting's override flag).
 * @param self The model component; must not be NULL.
 * @param setting The setting.
 * @returns true when the placement's own value applies; false when the world model's does (and for a value outside the
 *          enum).
 */
bool WorldModelComponent_IsOverridden(const WorldModelComponent* self, WorldModelSetting setting);

/**
 * @brief Sets one setting's override flag: on, the placement's own value applies; off, the world model's (the own value
 *        is kept).
 * @param self The model component; must not be NULL.
 * @param setting The setting.
 * @param isOverridden Whether the placement's own value applies.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value outside the
 *          enum.
 */
Error WorldModelComponent_SetOverridden(WorldModelComponent* self, WorldModelSetting setting, bool isOverridden);

/**
 * @brief Returns this placement's own outline setting (it applies while WorldModelSetting_HasOutline is overridden).
 * @param self The model component; must not be NULL.
 * @returns true if outlined.
 */
static inline bool WorldModelComponent_HasOutline(const WorldModelComponent* self)
{
    return self->_hasOutline;
}

/**
 * @brief Sets this placement's own outline setting (it applies while WorldModelSetting_HasOutline is overridden).
 * @param self The model component; must not be NULL.
 * @param hasOutline Whether to draw the outline.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelComponent_SetHasOutline(WorldModelComponent* self, bool hasOutline);

/**
 * @brief Returns this placement's own "drawn at full resolution over the pixelated image" (it applies while
 *        WorldModelSetting_OmitPixelation is overridden).
 * @param self The model component; must not be NULL.
 * @returns true if exempt from the pixelation.
 */
static inline bool WorldModelComponent_OmitsPixelation(const WorldModelComponent* self)
{
    return self->_omitPixelation;
}

/**
 * @brief Sets this placement's own "drawn at full resolution over the pixelated image" (it applies while
 *        WorldModelSetting_OmitPixelation is overridden).
 * @param self The model component; must not be NULL.
 * @param omitPixelation Whether to exempt it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelComponent_SetOmitsPixelation(WorldModelComponent* self, bool omitPixelation);

/**
 * @brief Returns this placement's own shadow cascade(s) (they apply while WorldModelSetting_ShadowTier is overridden).
 * @param self The model component; must not be NULL.
 * @returns The tier.
 */
static inline WorldShadowTier WorldModelComponent_GetShadowTier(const WorldModelComponent* self)
{
    return self->_shadowTier;
}

/**
 * @brief Sets this placement's own shadow cascade(s) (they apply while WorldModelSetting_ShadowTier is overridden).
 * @param self The model component; must not be NULL.
 * @param shadowTier A WorldShadowTier value.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for any other value.
 */
Error WorldModelComponent_SetShadowTier(WorldModelComponent* self, WorldShadowTier shadowTier);

/**
 * @brief Returns this placement's own "casts shadows" (it applies while WorldModelSetting_CastsShadows is overridden).
 * @param self The model component; must not be NULL.
 * @returns true if it casts shadows.
 */
static inline bool WorldModelComponent_CastsShadows(const WorldModelComponent* self)
{
    return self->_castsShadows;
}

/**
 * @brief Sets this placement's own "casts shadows" (it applies while WorldModelSetting_CastsShadows is overridden;
 *        false skips the model in every shadow pass, it still receives shadows).
 * @param self The model component; must not be NULL.
 * @param castsShadows Whether it casts shadows.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelComponent_SetCastsShadows(WorldModelComponent* self, bool castsShadows);

/**
 * @brief Returns this placement's own draw distance (it applies while WorldModelSetting_DrawDistance is overridden).
 * @param self The model component; must not be NULL.
 * @returns Metres, above 0.
 */
static inline float WorldModelComponent_GetDrawDistance(const WorldModelComponent* self)
{
    return self->_drawDistance;
}

/**
 * @brief Sets this placement's own draw distance (it applies while WorldModelSetting_DrawDistance is overridden).
 * @param self The model component; must not be NULL.
 * @param drawDistance Metres; finite and above 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value that is not
 *          finite or not above 0.
 */
Error WorldModelComponent_SetDrawDistance(WorldModelComponent* self, float drawDistance);

/**
 * @brief Returns the draw distance multiplier: it multiplies the draw distance in effect (2 = seen twice as far).
 * @param self The model component; must not be NULL.
 * @returns The multiplier, above 0 (default 1).
 */
static inline float WorldModelComponent_GetDrawDistanceMultiplier(const WorldModelComponent* self)
{
    return self->_drawDistanceMultiplier;
}

/**
 * @brief Sets the draw distance multiplier (it multiplies the world model's or the placement's draw distance, whichever
 *        is in effect).
 * @param self The model component; must not be NULL.
 * @param multiplier Finite and above 0; 1 changes nothing.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value that is not
 *          finite or not above 0.
 */
Error WorldModelComponent_SetDrawDistanceMultiplier(WorldModelComponent* self, float multiplier);

/**
 * @brief Returns this placement's own "the quality setting leaves the draw distance as it is" (it applies while
 *        WorldModelSetting_IsDrawDistanceFixed is overridden).
 * @param self The model component; must not be NULL.
 * @returns true if fixed.
 */
static inline bool WorldModelComponent_IsDrawDistanceFixed(const WorldModelComponent* self)
{
    return self->_isDrawDistanceFixed;
}

/**
 * @brief Sets this placement's own "the quality setting leaves the draw distance as it is" (it applies while
 *        WorldModelSetting_IsDrawDistanceFixed is overridden).
 * @param self The model component; must not be NULL.
 * @param isFixed Whether the player's draw_distance_scale leaves the draw distance as it is.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelComponent_SetIsDrawDistanceFixed(WorldModelComponent* self, bool isFixed);

/**
 * @brief The settings that apply to this placement: each setting from the world model, or the placement's own value
 *        while its override flag is on, and the draw distance in effect times the multiplier.
 *
 * A pure function (the renderer calls it for every model each frame).
 * @param self The model component; must not be NULL.
 * @param modelSettings The world model's settings (WorldModel_GetSettings), or WorldModelSettings_CreateDefault()
 *        while it is not resolved; must not be NULL.
 * @returns The settings in effect. Their draw distance is above 0 (+infinity when the product overflows).
 */
WorldModelSettings WorldModelComponent_ResolveSettings(const WorldModelComponent* self,
    const WorldModelSettings* modelSettings);
