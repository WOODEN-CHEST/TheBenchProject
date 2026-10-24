#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// CollisionLayer.h for the collision settings' layer; WorldModel.h for WorldShadowTier (a terrain casts as a model
// does); TerrainMesh.h for the mesh the asset owns.
#include "CollisionLayer.h"
#include "TerrainMesh.h"
#include "WorldModel.h"


/**
 * @file TerrainAsset.h
 * @brief The terrain asset: a terrain's mesh (TerrainMesh.h), its palette of materials and its density channels, and the
 *        default render and collision settings of its placements (decision W5: a terrain lives in an asset, which a
 *        world's terrain object names, so stories set in one town share it and a story's pack overrides it).
 *
 * A terrain is an asset of the "terrain" type (JSON definitions in a resource pack's terrains/ folder,
 * references/asset_structure.md, "Terrains"; load one with AssetManager_LoadTerrain). Its mesh comes from a Blender .glb
 * imported when it loads (source "model", decision W6), or from its binary form (source "data", a .terrain.ghdf file the
 * editor will write: TerrainMesh_SaveGHDF). Unknown keys are refused everywhere in a definition and every rule is
 * checked when it is read; the mesh's own limits when it loads (the file is hostile).
 *
 * The PALETTE names 1 to TERRAIN_MAX_MATERIALS terrain materials (B02 adds that asset type; until then the names only
 * pick a debug colour per entry): a vertex weighs each of them, a face material takes one outright (so a model's palette
 * names no material "hole" or as its blend slot: those slots are read first). The DENSITY CHANNELS
 * (at most TERRAIN_MAX_DENSITY_CHANNELS) are named for the scattering (B09); their bytes are kept per vertex.
 *
 * A placed object names its terrain in its terrain component (WorldTerrainComponent.h) and may override each default
 * setting with an explicit flag next to the value; WorldTerrainComponent_ResolveSettings turns the two into the settings
 * that apply.
 *
 * OWNERSHIP. The asset is pure data with no dependencies, and owns its mesh; the asset manager owns the asset. Never keep
 * its pointer past your asset user (AssetManager.md). The renderer and the collision world read the mesh and never write
 * it; the editor (later) changes it through the mesh's setters, and both rebuild what changed by the chunks' revisions.
 */


// Macros.
/** @brief The draw distance a terrain has unless its definition says, in metres: far, and fixed, so a quality setting
 *  never makes hills vanish. */
#define TERRAIN_DEFAULT_DRAW_DISTANCE 4000.0f
/** @brief The most bytes a palette material's or density channel's name may have. */
#define TERRAIN_NAME_MAX_LENGTH ((size_t)128)
/** @brief The material slot name a definition's faces use for the painted weights unless it says otherwise. */
#define TERRAIN_DEFAULT_BLEND_SLOT ((const unsigned char*)u8"blend")


// Types.
/**
 * @brief The render settings a terrain's placements start from (and each may override). A plain value type.
 */
typedef struct TerrainRenderSettingsStruct
{
    /** @brief Whether the renderer outlines it (default true). */
    bool HasOutline;
    /** @brief Which shadow cascade(s) it casts into (default both). */
    WorldShadowTier ShadowTier;
    /** @brief Whether it casts shadows at all (default true; it receives them either way). */
    bool CastsShadows;
    /** @brief How far its chunks are seen, in metres (above 0; default TERRAIN_DEFAULT_DRAW_DISTANCE): each chunk fades
     *  out past it, times the zoom. Once resolved for a placement, times its multiplier (+infinity on overflow). */
    float DrawDistance;
    /** @brief Whether the player's draw_distance_scale leaves the draw distance as it is (default true). */
    bool IsDrawDistanceFixed;
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
 * @brief A loaded terrain. Read-only, owned by the asset manager: use the accessors.
 */
typedef struct TerrainAssetStruct
{
    /** @brief The mesh (owned). */
    TerrainMesh* _mesh;
    /** @brief Its placements' defaults. */
    TerrainSettings _settings;
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
 * @brief The default settings: outlined, casting into both cascades, seen TERRAIN_DEFAULT_DRAW_DISTANCE metres with a
 *        fixed draw distance; colliding on the world layer with that layer's default mask.
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
