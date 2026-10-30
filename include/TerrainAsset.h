#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// CollisionLayer.h for the collision settings' layer; WorldModel.h for the haze's bound a terrain shares with models;
// TerrainMesh.h for the mesh the asset owns; TerrainMaterial.h for its palette's materials.
#include "CollisionLayer.h"
#include "TerrainMaterial.h"
#include "TerrainMesh.h"
#include "WorldModel.h"


/**
 * @file TerrainAsset.h
 * @brief The terrain asset: a terrain's mesh (TerrainMesh.h), its palette of materials and its density channels, and the
 *        default render and collision settings of its placements (a terrain lives in an asset, which a world's
 *        terrain object names, so stories set in one town share it and a story's pack overrides it).
 *
 * A terrain is an asset of the "terrain" type (JSON definitions in a resource pack's terrains/ folder,
 * references/asset_structure.md, "Terrains"; load one with AssetManager_LoadTerrain). Its mesh comes from a Blender .glb
 * imported when it loads (source "model"), or from its binary form (source "data", a .terrain.ghdf file the
 * editor will write: TerrainMesh_SaveGHDF). Unknown keys are refused everywhere in a definition and every rule is
 * checked when it is read; the mesh's own limits when it loads (the file is hostile).
 *
 * The PALETTE names 1 to TERRAIN_MAX_MATERIALS terrain materials (TerrainMaterial.h), loaded as the terrain's
 * dependencies: a vertex weighs each of them, a face material takes one outright (so a model's palette names no material
 * "hole" or as its blend slot: those slots are read first). The renderer makes one texture array per map kind, one layer
 * per material, so the materials' maps of one kind must share one size (the load refuses a mismatch, naming both). The
 * RULES (at most TERRAIN_MAX_RULES) move the weights toward a material by the ground's slope, its height and noise,
 * in order, per pixel (TerrainBlend.h is their C mirror). The DENSITY CHANNELS (at most TERRAIN_MAX_DENSITY_CHANNELS) are
 * named for the scattering (not built yet); their bytes are kept per vertex.
 *
 * A placed object names its terrain in its terrain component (WorldTerrainComponent.h) and may override each default
 * setting with an explicit flag next to the value; WorldTerrainComponent_ResolveSettings turns the two into the settings
 * that apply.
 *
 * OWNERSHIP. The asset is pure data (no GL) and owns its mesh and rules; its palette's materials are its dependencies,
 * held under its dependency user and released after it. The asset manager owns the asset. Never keep its pointer past
 * your asset user (AssetManager.md). The renderer and the collision world read the mesh and never write it; the editor
 * (later) changes it through the mesh's setters, and both rebuild what changed by the chunks' revisions.
 */


// Macros.
/** @brief The draw distance a terrain has unless its definition says, in metres: far, and fixed, so a quality setting
 *  never makes hills vanish. */
#define TERRAIN_DEFAULT_DRAW_DISTANCE 4000.0f
/** @brief The shadow distance a terrain's own starts at, in metres (unused until has_shadow_distance or a placement's
 *  override is on: by default a terrain casts as far as its chunks are drawn). */
#define TERRAIN_DEFAULT_SHADOW_DISTANCE TERRAIN_DEFAULT_DRAW_DISTANCE
/** @brief The most bytes a palette material's or density channel's name may have. */
#define TERRAIN_NAME_MAX_LENGTH ((size_t)128)
/** @brief The material slot name a definition's faces use for the painted weights unless it says otherwise. */
#define TERRAIN_DEFAULT_BLEND_SLOT ((const unsigned char*)u8"blend")
/** @brief The most rules a terrain may have: each is about five vec4 uniforms of the terrain shader's, which shares
 *  GL 3.3's guaranteed 1024 fragment uniform components with the lighting and the sixteen materials. */
#define TERRAIN_MAX_RULES 8U
/** @brief Every palette material at once, as a rule's CoveredMaterials (without "only_on"). */
#define TERRAIN_ALL_MATERIALS ((uint32_t)((1U << TERRAIN_MAX_MATERIALS) - 1U))


// Types.
/**
 * @brief The render settings a terrain's placements start from (and each may override). A plain value type.
 */
typedef struct TerrainRenderSettingsStruct
{
    /** @brief Whether the renderer outlines it (default true). */
    bool HasOutline;
    /** @brief Whether it casts shadows at all (default true; it receives them either way). */
    bool CastsShadows;
    /** @brief Whether ShadowDistance is a shadow distance of its own (default false: it casts as far as its chunks are
     *  drawn). Once resolved for a placement, whether the placement or the terrain gives one. */
    bool HasShadowDistance;
    /** @brief How far from the camera its chunks still cast, in metres, before the player's shadow_distance_scale and the
     *  zoom (above 0; default TERRAIN_DEFAULT_SHADOW_DISTANCE; meaningful while HasShadowDistance). Once resolved for a
     *  placement: its own (or the terrain's), or without one its draw distance in effect, times its
     *  shadow_distance_multiplier; without its own the renderer scales it as the draw distance before the shadow
     *  scale (WorldModelSettings.ShadowDistance's rule). */
    float ShadowDistance;
    /** @brief Whether the player's shadow_distance_scale leaves the shadow distance as it is (default false). */
    bool IsShadowDistanceFixed;
    /** @brief How far its chunks are seen, in metres (above 0; default TERRAIN_DEFAULT_DRAW_DISTANCE): each chunk fades
     *  out past it, times the zoom. Once resolved for a placement, times its multiplier (+infinity on overflow). */
    float DrawDistance;
    /** @brief Whether the player's draw_distance_scale leaves the draw distance as it is (default true). */
    bool IsDrawDistanceFixed;
    /** @brief How hazy it looks at a distance: it multiplies the haze in front of it, 1 (the default) the real air's,
     *  0 as if the air were clear. In [0, WORLD_MODEL_MAX_HAZE]. */
    float Haze;
} TerrainRenderSettings;

/**
 * @brief The collision settings a terrain's placements start from (and each may override). A plain value type.
 */
typedef struct TerrainCollisionSettingsStruct
{
    /** @brief Whether its chunks collide (default true). */
    bool IsEnabled;
    /** @brief Their layer (default world). */
    CollisionLayer Layer;
    /** @brief Whether the definition gives a mask; without one the layer in effect's default mask applies
     *  (CollisionLayer_GetDefaultMask), as for a world model's collision. */
    bool HasCollidesWith;
    /** @brief The mask while HasCollidesWith (COLLISION_LAYER_BIT per layer). */
    uint32_t CollidesWith;
} TerrainCollisionSettings;

/**
 * @brief A terrain's settings: render and collision. A plain value type; TerrainSettings_CreateDefault gives the
 *        defaults.
 */
typedef struct TerrainSettingsStruct
{
    /** @brief The render settings. */
    TerrainRenderSettings Render;
    /** @brief The collision settings. */
    TerrainCollisionSettings Collision;
} TerrainSettings;

/**
 * @brief One condition of a terrain rule on a value (the ground's slope, or its height): 1 inside [Minimum, Maximum],
 *        ramping to 0 over Blend around each bound (a smooth step from bound - Blend / 2 to bound + Blend / 2), a hard
 *        step without a blend. A bound left out is open. A plain value type.
 */
typedef struct TerrainRuleRangeStruct
{
    /** @brief Whether the rule has this condition (its block in the definition); without it the condition is 1. */
    bool IsEnabled;
    /** @brief Whether the range has a lower bound, and the bound (degrees for the slope, metres for the height). */
    bool HasMinimum;
    float Minimum;
    /** @brief Whether the range has an upper bound, and the bound. */
    bool HasMaximum;
    float Maximum;
    /** @brief The ramp's width around each bound, in the same unit (>= 0; 0 is a hard step). */
    float Blend;
} TerrainRuleRange;

/**
 * @brief A terrain rule's noise: world-space value noise (TerrainBlend_GetValueNoise) scaling the rule's strength. A plain
 *        value type.
 */
typedef struct TerrainRuleNoiseStruct
{
    /** @brief Whether the rule has noise (its block in the definition); without it the noise factor is 1. */
    bool IsEnabled;
    /** @brief Metres per noise cell (above 0): the size of its patches. */
    float Scale;
    /** @brief How much the noise takes away, 0 to 1: the factor is 1 - Strength + Strength x noise. */
    float Strength;
    /** @brief Picks the pattern: rules with different seeds vary apart. */
    uint32_t Seed;
} TerrainRuleNoise;

/**
 * @brief One of a terrain's rules: where its conditions hold, it moves the weights toward its material. A plain
 *        value type, read from the terrain's definition (its "rules").
 *
 * Per pixel (and in TerrainBlend_ApplyRules), in order: the rule's strength is Opacity x the slope's condition x the
 * height's x the noise's factor; each covered material other than its own gives up that share of its weight, which its
 * material gains. Faces with a face material are left as they are.
 */
typedef struct TerrainRuleStruct
{
    /** @brief The palette index of the material it puts down. */
    uint32_t Material;
    /** @brief How strongly it applies where its conditions hold fully, 0 to 1. */
    float Opacity;
    /** @brief A bit per palette index: the materials it may cover ("only_on"; every material without it). */
    uint32_t CoveredMaterials;
    /** @brief The slope, in degrees from flat (0) through vertical (90) to an overhang's underside (180). */
    TerrainRuleRange Slope;
    /** @brief The height, in metres: the world's Y (absolute, not the terrain's own). */
    TerrainRuleRange Height;
    /** @brief The noise. */
    TerrainRuleNoise Noise;
} TerrainRule;

/**
 * @brief A loaded terrain. Read-only, owned by the asset manager: use the accessors.
 */
typedef struct TerrainAssetStruct
{
    /** @brief The mesh (owned). */
    TerrainMesh* _mesh;
    /** @brief The palette's materials (borrowed: held under the terrain's dependency user, valid while the terrain is);
     *  NULL past the palette, and for a terrain built without loading them (headless tests). */
    const TerrainMaterial* _materials[TERRAIN_MAX_MATERIALS];
    /** @brief The rules, in order. */
    TerrainRule _rules[TERRAIN_MAX_RULES];
    uint32_t _ruleCount;
    /** @brief Its placements' defaults. */
    TerrainSettings _settings;
    /** @brief The terrain's own name (its definition's; into _names), for messages; NULL for a terrain built without a
     *  definition (headless tests). */
    const unsigned char* _name;
    /** @brief The palette's names and the density channels' names (into _names, owned). */
    const unsigned char* _materialNames[TERRAIN_MAX_MATERIALS];
    uint32_t _materialCount;
    const unsigned char* _densityChannelNames[TERRAIN_MAX_DENSITY_CHANNELS];
    uint32_t _densityChannelCount;
    /** @brief One block holding every name, NUL-terminated one after the other. */
    unsigned char* _names;
} TerrainAsset;


// Functions.
/**
 * @brief The default settings: outlined, casting shadows as far as it is drawn, seen TERRAIN_DEFAULT_DRAW_DISTANCE
 *        metres with a fixed draw distance; colliding on the world layer with that layer's default mask.
 * @returns The settings.
 */
TerrainSettings TerrainSettings_CreateDefault(void);

/** @brief The terrain's mesh (borrowed: valid while the terrain is). */
static inline TerrainMesh* TerrainAsset_GetMesh(const TerrainAsset* self)
{
    return self->_mesh;
}

/** @brief The defaults of the terrain's placements. */
static inline const TerrainSettings* TerrainAsset_GetSettings(const TerrainAsset* self)
{
    return &self->_settings;
}

/** @brief The terrain's name (its definition's: borrowed, valid while the terrain is), or NULL for one built without a
 *  definition. */
static inline const unsigned char* TerrainAsset_GetName(const TerrainAsset* self)
{
    return self->_name;
}

/** @brief The palette's size (1 to TERRAIN_MAX_MATERIALS). */
static inline uint32_t TerrainAsset_GetMaterialCount(const TerrainAsset* self)
{
    return self->_materialCount;
}

/**
 * @brief A palette material's name.
 * @param self The terrain; must not be NULL.
 * @param index The palette index.
 * @returns The name (borrowed), or NULL for an index past the palette.
 */
static inline const unsigned char* TerrainAsset_GetMaterialName(const TerrainAsset* self, uint32_t index)
{
    return (index < self->_materialCount) ? self->_materialNames[index] : NULL;
}

/**
 * @brief A palette material.
 * @param self The terrain; must not be NULL.
 * @param index The palette index.
 * @returns The material (borrowed: valid while the terrain is), or NULL for an index past the palette or a terrain built
 *          without its materials.
 */
static inline const TerrainMaterial* TerrainAsset_GetMaterial(const TerrainAsset* self, uint32_t index)
{
    return (index < self->_materialCount) ? self->_materials[index] : NULL;
}

/**
 * @brief The terrain's rules, in the order they apply.
 * @param self The terrain; must not be NULL.
 * @param outCount [out] Receives how many there are (0 to TERRAIN_MAX_RULES). Must not be NULL.
 * @returns The rules (borrowed: valid while the terrain is).
 */
static inline const TerrainRule* TerrainAsset_GetRules(const TerrainAsset* self, uint32_t* outCount)
{
    *outCount = self->_ruleCount;
    return self->_rules;
}

/** @brief The number of density channels (0 to TERRAIN_MAX_DENSITY_CHANNELS). */
static inline uint32_t TerrainAsset_GetDensityChannelCount(const TerrainAsset* self)
{
    return self->_densityChannelCount;
}

/**
 * @brief A density channel's name.
 * @param self The terrain; must not be NULL.
 * @param index The channel.
 * @returns The name (borrowed), or NULL for an index past the channels.
 */
static inline const unsigned char* TerrainAsset_GetDensityChannelName(const TerrainAsset* self, uint32_t index)
{
    return (index < self->_densityChannelCount) ? self->_densityChannelNames[index] : NULL;
}
