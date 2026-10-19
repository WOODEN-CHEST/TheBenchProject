#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"


/**
 * @file WorldModelComponent.h
 * @brief The model component: a 3D model, referenced by its asset name, drawn with its object's transform and tint.
 *
 * The world renderer resolves the model from the asset manager by name (under its own asset user) and resolves it
 * again when the name changes; the component itself holds no asset. It adds the per-object render settings: a 1-pixel
 * outline (on by default), whether it is exempt from the pixelation pass (a readable in-world screen), which sun or
 * moon shadow cascade(s) it casts into, and whether it casts shadows at all.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Model, &component); reach its fields with
 * WorldModelComponent_FromComponent. Its properties are the rows of its table ("model_component"): model,
 * has_outline, omit_pixelation, shadow_tier ("both" / "near" / "far") and casts_shadows. Every change advances the
 * component's revision (WorldComponent_GetRevision).
 */


// Macros.
/** @brief The most bytes a model's asset name may have (without the terminator). */
#define WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH ((size_t)128)


// Types.
/**
 * @brief Which shadow cascade(s) of the sun or the moon a model casts into.
 *
 * The shadow light uses two cascaded directional shadow maps: a NEAR map (small extent, sharp, for close detail) and a
 * FAR map (large extent, coarse, for big or distant casters). A small detailed prop can cast only up close (Near: no
 * coarse shadow at a distance, and cheaper), a large object at long range (Far), or both (the default, correct
 * everywhere). Saved by its value: never renumber.
 */
typedef enum WorldShadowTierEnum
{
    /** @brief Casts into both cascades (default): a correct shadow at every distance. */
    WorldShadowTier_Both = 0,
    /** @brief Casts into the NEAR cascade only: sharp up close, no shadow past the near range (detail props). */
    WorldShadowTier_Near = 1,
    /** @brief Casts into the FAR cascade only: a long-range coarse shadow (large objects, buildings, terrain). */
    WorldShadowTier_Far = 2
} WorldShadowTier;

/**
 * @brief The model component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldModelComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Owned asset name of the model to draw (at most WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH bytes); NULL draws
     *  nothing (the default). */
    unsigned char* _model;
    /** @brief Whether the renderer draws a 1-pixel outline around the model (default true). */
    bool _hasOutline;
    /** @brief Whether the model is drawn at full resolution over the pixelated image (default false). */
    bool _omitPixelation;
    /** @brief Which shadow cascade(s) the model casts into (default WorldShadowTier_Both). */
    WorldShadowTier _shadowTier;
    /** @brief Whether the model casts shadows at all (default true); false skips it in every shadow pass. */
    bool _castsShadows;
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
 * @brief Returns the borrowed asset name of the model (may be NULL); valid until it changes.
 * @param self The model component; must not be NULL.
 * @returns The NUL-terminated UTF-8 asset name, or NULL.
 */
static inline const unsigned char* WorldModelComponent_GetModel(const WorldModelComponent* self)
{
    return self->_model;
}

/**
 * @brief Sets the model to draw (a clone of @p model; NULL draws nothing). The renderer resolves the new model at its
 *        next frame.
 * @param self The model component; must not be NULL.
 * @param model Borrowed asset name, or NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p model is not a
 *          valid asset name of at most WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH bytes (nothing changes then).
 */
Error WorldModelComponent_SetModel(WorldModelComponent* self, const unsigned char* model);

/**
 * @brief Returns whether the renderer outlines the model.
 * @param self The model component; must not be NULL.
 * @returns true if outlined.
 */
static inline bool WorldModelComponent_HasOutline(const WorldModelComponent* self)
{
    return self->_hasOutline;
}

/**
 * @brief Turns the model's outline on or off.
 * @param self The model component; must not be NULL.
 * @param hasOutline Whether to draw the outline.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelComponent_SetHasOutline(WorldModelComponent* self, bool hasOutline);

/**
 * @brief Returns whether the model is drawn at full resolution over the pixelated image.
 * @param self The model component; must not be NULL.
 * @returns true if exempt from the pixelation.
 */
static inline bool WorldModelComponent_OmitsPixelation(const WorldModelComponent* self)
{
    return self->_omitPixelation;
}

/**
 * @brief Exempts the model from the pixelation pass (drawn crisp at full resolution) or not.
 * @param self The model component; must not be NULL.
 * @param omitPixelation Whether to exempt it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelComponent_SetOmitsPixelation(WorldModelComponent* self, bool omitPixelation);

/**
 * @brief Returns which shadow cascade(s) the model casts into.
 * @param self The model component; must not be NULL.
 * @returns The tier.
 */
static inline WorldShadowTier WorldModelComponent_GetShadowTier(const WorldModelComponent* self)
{
    return self->_shadowTier;
}

/**
 * @brief Sets which shadow cascade(s) the model casts into.
 * @param self The model component; must not be NULL.
 * @param shadowTier A WorldShadowTier value.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for any other value.
 */
Error WorldModelComponent_SetShadowTier(WorldModelComponent* self, WorldShadowTier shadowTier);

/**
 * @brief Returns whether the model casts shadows.
 * @param self The model component; must not be NULL.
 * @returns true if it casts shadows.
 */
static inline bool WorldModelComponent_CastsShadows(const WorldModelComponent* self)
{
    return self->_castsShadows;
}

/**
 * @brief Sets whether the model casts shadows (false skips it in every shadow pass; it still receives shadows).
 * @param self The model component; must not be NULL.
 * @param castsShadows Whether it casts shadows.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldModelComponent_SetCastsShadows(WorldModelComponent* self, bool castsShadows);
