#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// raylib.h is needed for Vector3 (the bounds) and MaterialMap (variants apply to a material's maps); GameModel.h for
// GameModel and GameMaterialFactors; PropertyDescriptor.h for the shadow tier's names (a PropertyEnumTable).
#include "raylib/raylib.h"
#include "GameModel.h"
#include "PropertyDescriptor.h"

/** @brief A standalone texture asset (GameTexture.h); variants name textures as dependencies. */
typedef struct GameTextureStruct GameTexture;


/**
 * @file WorldModel.h
 * @brief The world model: the reusable definition of a placeable thing, kept apart from its placements.
 *
 * A world model is an asset of the "world_model" type (JSON definitions in a resource pack's world_models/ folder,
 * references/asset_structure.md; load one with AssetManager_LoadWorldModel). It says which model to draw at which
 * distance (its LODs, 1 to WORLD_MODEL_MAX_LODS), how far it is seen (its draw distance, and whether the player's
 * draw_distance_scale may change it), the default render settings of its placements, its material variants and its
 * bounding sphere. A model asset stays a plain asset that is usable outside the world (the UI); what only matters in
 * the world lives here (the owner's decision G17).
 *
 * A placed object names its world model in its model component (WorldModelComponent.h), and may override each default
 * setting with an explicit flag next to the value; WorldModelComponent_ResolveSettings turns the two into the settings
 * that apply. Changing the component's name swaps the world model and keeps the object.
 *
 * OWNERSHIP. The asset is pure data. Its LOD models and the textures its variants name are loaded as DEPENDENCIES,
 * under the world model's dependency user: holding the world model holds them, and they unload right after it. The
 * GameModel and GameTexture pointers in it are therefore valid exactly as long as the world model is. The asset
 * manager owns the world model; never keep its pointer past your asset user (AssetManager.md).
 *
 * VARIANTS apply at draw time, never to an asset: the renderer copies a mesh's material maps for each draw and
 * WorldModel_ApplyVariant changes that copy (textures, then the factors), the same rule as the object's tint.
 *
 * LODS. Each LOD is drawn while the object is nearer than its max distance (at the default 60-degree field of view), the
 * last one until the draw distance; a placement may give its own switch distances, scale them or force a LOD
 * (WorldModelComponent_ResolveLodSettings). The renderer picks the LOD by the camera's distance with the hysteresis
 * stored here, and cross-fades a change over the fade time with a dither (T11).
 *
 * Every field is read-only (underscore-prefixed): read them through the accessors.
 */


// Macros.
/** @brief The most LODs a world model may have. */
#define WORLD_MODEL_MAX_LODS ((size_t)8)
/** @brief The most material variants a world model may have. */
#define WORLD_MODEL_MAX_VARIANTS ((size_t)64)
/** @brief The most material overrides one variant may hold (one per material slot). */
#define WORLD_MODEL_MAX_VARIANT_OVERRIDES ((size_t)32)
/** @brief The most bytes a variant's name may have (without the terminator). */
#define WORLD_MODEL_VARIANT_NAME_MAX_LENGTH ((size_t)128)
/** @brief How long a LOD cross-fade takes when the definition does not say, in seconds (T11). */
#define WORLD_MODEL_DEFAULT_LOD_FADE_SECONDS 0.3f
/** @brief The LOD hysteresis when the definition does not say: a share of each switch distance (T11). */
#define WORLD_MODEL_DEFAULT_LOD_HYSTERESIS 0.05f
/** @brief The draw distance of WorldModelSettings_CreateDefault, in metres: what a model component's own draw distance
 *  starts at (its override is off by default). A world model's own draw distance defaults to its last LOD's. */
#define WORLD_MODEL_DEFAULT_DRAW_DISTANCE 600.0f


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
 * @brief The settings a placed object can override: as a world model sets them (the defaults of its placements), or as
 *        they apply to one placement (WorldModelComponent_ResolveSettings).
 *
 * A plain value type.
 */
typedef struct WorldModelSettingsStruct
{
    /** @brief Whether the renderer draws a 1-pixel outline around the model. */
    bool HasOutline;
    /** @brief Whether the model is drawn at full resolution over the pixelated image (a readable screen). */
    bool OmitsPixelation;
    /** @brief Which shadow cascade(s) the model casts into. */
    WorldShadowTier ShadowTier;
    /** @brief Whether the model casts shadows at all (it receives them either way). */
    bool CastsShadows;
    /** @brief How far the model is seen, in metres, before the player's draw_distance_scale and the zoom; beyond it the
     *  model fades out. Above 0 in a world model; once resolved for a placement, the product with its multiplier:
     *  +infinity when it overflows (never faded by distance), 0 when two tiny factors underflow (never drawn). */
    float DrawDistance;
    /** @brief Whether the player's draw_distance_scale leaves DrawDistance as it is (landmarks, gameplay-critical
     *  props). */
    bool IsDrawDistanceFixed;
} WorldModelSettings;

/**
 * @brief How one placement chooses among its world model's LODs (WorldModelComponent_ResolveLodSettings): the switch
 *        distances in effect, the forced LOD, and the world model's hysteresis and fade time.
 *
 * A plain value type; the renderer resolves it for every model each frame.
 */
typedef struct WorldModelLodSettingsStruct
{
    /** @brief The number of LODs, 1 to WORLD_MODEL_MAX_LODS: the world model's. */
    size_t LodCount;
    /** @brief The first LodCount entries: LOD i is drawn while the object is nearer than SwitchDistances[i] metres (at
     *  the default 60-degree field of view, before the player's lod_distance_scale); past the last one the last LOD
     *  keeps drawing until the draw distance. The world model's max distances or the placement's own list, times the
     *  placement's multiplier: at least 0, +infinity where a product overflows. The other entries are 0. */
    float SwitchDistances[WORLD_MODEL_MAX_LODS];
    /** @brief The world model's hysteresis, in [0, 1): a switch to a coarser LOD waits until the switch distance
     *  x (1 + h), one back to a finer LOD until x (1 - h). */
    float Hysteresis;
    /** @brief The world model's cross-fade time, in seconds (>= 0; 0 swaps at once). */
    float FadeSeconds;
    /** @brief Whether ForcedLod is drawn whatever the distance. */
    bool IsLodForced;
    /** @brief The LOD drawn while IsLodForced, below LodCount (the placement's forced_lod, clamped to the last LOD). */
    size_t ForcedLod;
} WorldModelLodSettings;

// TODO: impostors: a world model could use a sprite (a world sprite) as its last, farthest LOD.
/** @brief One level of detail of a world model. */
typedef struct WorldModelLodStruct
{
    /** @brief The LOD's model, held under the world model's dependency user (valid while the world model is). */
    GameModel* _model;
    /** @brief The LOD is used up to this distance, in metres at the default 60-degree field of view (above 0, and above
     *  the previous LOD's). */
    float _maxDistance;
} WorldModelLod;

/** @brief What a variant changes in one material slot: maps from texture assets, and factors. */
typedef struct WorldModelMaterialOverrideStruct
{
    /** @brief The material's index in each LOD's model (a LOD without that material is not changed). */
    int32_t _slot;
    /** @brief The albedo, normal, packed occlusion/roughness/metallic and emissive maps, each NULL to keep the model's;
     *  texture assets held under the world model's dependency user. */
    const GameTexture* _albedo;
    const GameTexture* _normal;
    const GameTexture* _mra;
    const GameTexture* _emissive;
    /** @brief Tint, metallic, roughness and emissive colour and intensity (GameMaterialFactors). */
    GameMaterialFactors _factors;
} WorldModelMaterialOverride;

/** @brief A named set of material overrides: one look of the same meshes (dark wood, a red car). */
typedef struct WorldModelVariantStruct
{
    /** @brief The variant's name ([a-z][a-z0-9_.-]*, at most WORLD_MODEL_VARIANT_NAME_MAX_LENGTH bytes), unique within
     *  its world model; owned by the world model. */
    const unsigned char* _name;
    /** @brief Its overrides, one per material slot (owned by the world model), and their count. */
    const WorldModelMaterialOverride* _overrides;
    size_t _overrideCount;
} WorldModelVariant;

/**
 * @brief A loaded world model. Built and owned by the asset manager (the "world_model" type); read-only.
 */
typedef struct WorldModelStruct
{
    /** @brief The LODs, finest first; the first _lodCount entries are used. */
    WorldModelLod _lods[WORLD_MODEL_MAX_LODS];
    /** @brief The number of LODs, 1 to WORLD_MODEL_MAX_LODS. */
    size_t _lodCount;
    /** @brief The defaults of its placements: render settings and draw distance. */
    WorldModelSettings _settings;
    /** @brief How long a LOD cross-fade takes, in seconds (>= 0; 0 swaps at once). */
    float _lodFadeSeconds;
    /** @brief The LOD hysteresis, in [0, 1): a share of each switch distance. */
    float _lodHysteresis;
    /** @brief The bounding sphere, in the space _boundsTransform takes to the object's own: the definition's "bounds"
     *  (object space), or the sphere around LOD 0's mesh bounds (LOD 0's mesh space). */
    Vector3 _boundsCenter;
    float _boundsRadius;
    /** @brief From the bounding sphere's space to the object's own: identity for the definition's "bounds", LOD 0's
     *  import transform for its mesh bounds. */
    Matrix _boundsTransform;
    /** @brief The variants (owned), and their count (0 to WORLD_MODEL_MAX_VARIANTS). */
    const WorldModelVariant* _variants;
    size_t _variantCount;
} WorldModel;


// Fields.
/** @brief The names of WorldShadowTier ("both", "near", "far"): the world model definition's render.shadow_tier and the
 *  model component's shadow_tier row read them. */
extern const PropertyEnumTable WORLD_SHADOW_TIER_ENUM;


// Functions.
/**
 * @brief The settings a world model and a placement start from: an outline, pixelated, casting into both shadow
 *        cascades, casting shadows, WORLD_MODEL_DEFAULT_DRAW_DISTANCE metres, not fixed.
 * @returns The settings.
 */
WorldModelSettings WorldModelSettings_CreateDefault(void);

/**
 * @brief Returns the number of LODs.
 * @param self The world model; must not be NULL.
 * @returns 1 to WORLD_MODEL_MAX_LODS.
 */
static inline size_t WorldModel_GetLodCount(const WorldModel* self)
{
    return self->_lodCount;
}

/**
 * @brief Returns a LOD's model.
 * @param self The world model; must not be NULL.
 * @param index The LOD, 0 the finest.
 * @returns The model (borrowed: valid while the world model is loaded), or NULL for an index past the last LOD.
 */
static inline GameModel* WorldModel_GetLodModel(const WorldModel* self, size_t index)
{
    return (index < self->_lodCount) ? self->_lods[index]._model : NULL;
}

/**
 * @brief Returns the distance up to which a LOD is used, in metres at the default field of view.
 * @param self The world model; must not be NULL.
 * @param index The LOD, 0 the finest.
 * @returns The distance (above 0), or 0 for an index past the last LOD.
 */
static inline float WorldModel_GetLodMaxDistance(const WorldModel* self, size_t index)
{
    return (index < self->_lodCount) ? self->_lods[index]._maxDistance : 0.0f;
}

/**
 * @brief Returns the defaults of the world model's placements (render settings and draw distance).
 * @param self The world model; must not be NULL.
 * @returns The settings (borrowed: valid while the world model is loaded).
 */
static inline const WorldModelSettings* WorldModel_GetSettings(const WorldModel* self)
{
    return &self->_settings;
}

/**
 * @brief Returns how long a LOD cross-fade takes.
 * @param self The world model; must not be NULL.
 * @returns Seconds, >= 0 (0 swaps at once).
 */
static inline float WorldModel_GetLodFadeSeconds(const WorldModel* self)
{
    return self->_lodFadeSeconds;
}

/**
 * @brief Returns the LOD hysteresis: a LOD switches to a coarser one at its switch distance x (1 + h) and back at
 *        x (1 - h).
 * @param self The world model; must not be NULL.
 * @returns h, in [0, 1).
 */
static inline float WorldModel_GetLodHysteresis(const WorldModel* self)
{
    return self->_lodHysteresis;
}

/**
 * @brief Returns the centre of the bounding sphere, in the space WorldModel_GetBoundsTransform takes to the object's.
 * @param self The world model; must not be NULL.
 * @returns The centre.
 */
static inline Vector3 WorldModel_GetBoundsCenter(const WorldModel* self)
{
    return self->_boundsCenter;
}

/**
 * @brief Returns the radius of the bounding sphere, in the space WorldModel_GetBoundsTransform takes to the object's.
 * @param self The world model; must not be NULL.
 * @returns The radius (>= 0).
 */
static inline float WorldModel_GetBoundsRadius(const WorldModel* self)
{
    return self->_boundsRadius;
}

/**
 * @brief Returns the transform from the bounding sphere's space to the object's own space.
 *
 * Identity when the definition gives "bounds" (they are authored in the object's space); LOD 0's import transform when
 * the sphere is the one around LOD 0's mesh bounds. Compose it with the object's world matrix (MatrixMultiply(this,
 * world)) and move the sphere by the product, its radius scaled by the product's largest stretch: composing first keeps
 * the sphere as tight as one taken from the drawn model itself (separate stretch bounds of an unevenly scaled import
 * and an unevenly scaled placement would multiply up).
 * @param self The world model; must not be NULL.
 * @returns The transform.
 */
static inline Matrix WorldModel_GetBoundsTransform(const WorldModel* self)
{
    return self->_boundsTransform;
}

/**
 * @brief Returns the number of variants.
 * @param self The world model; must not be NULL.
 * @returns 0 to WORLD_MODEL_MAX_VARIANTS.
 */
static inline size_t WorldModel_GetVariantCount(const WorldModel* self)
{
    return self->_variantCount;
}

/**
 * @brief Returns a variant's name.
 * @param self The world model; must not be NULL.
 * @param index The variant.
 * @returns The name (borrowed: valid while the world model is loaded), or NULL for an index past the last variant.
 */
static inline const unsigned char* WorldModel_GetVariantName(const WorldModel* self, size_t index)
{
    return (index < self->_variantCount) ? self->_variants[index]._name : NULL;
}

/**
 * @brief Finds a variant by name.
 *
 * A linear search over the variants (a few per world model): resolve the name once and keep the index, as the
 * renderer's cache does.
 * @param self The world model; must not be NULL.
 * @param name The name, NUL-terminated UTF-8; NULL finds nothing.
 * @param outIndex [out] Receives the variant's index when found. Must not be NULL.
 * @returns true when the world model has a variant of that name.
 */
bool WorldModel_TryFindVariant(const WorldModel* self, const unsigned char* name, size_t* outIndex);

/**
 * @brief Applies a variant to one material's maps: its override for that material slot, if it has one.
 *
 * The override's textures replace their maps' textures (albedo, normal, the packed occlusion/roughness/metallic map in
 * the metalness slot, emissive), then its factors apply (GameMaterialFactors_Apply). A slot the variant does not
 * override, and an index past the last variant, change nothing. Writes only to @p maps: pass the renderer's per-draw
 * copy, never a model asset's maps.
 * @param self The world model; must not be NULL.
 * @param variantIndex The variant (from WorldModel_TryFindVariant).
 * @param materialIndex The index of the material in the LOD's model (raylib's Model.meshMaterial of the mesh drawn).
 * @param maps [in,out] A copy of that material's maps (raylib's MAX_MATERIAL_MAPS entries); must not be NULL.
 */
void WorldModel_ApplyVariant(const WorldModel* self, size_t variantIndex, int materialIndex, MaterialMap* maps);
