#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// PropertyDescriptor.h for the settings' property table; raylib.h for the albedo tint (Color) and the mean colour.
#include "PropertyDescriptor.h"
#include "raylib/raylib.h"


/**
 * @file TerrainMaterial.h
 * @brief A terrain material: one material of a terrain's palette (grass, dirt, rock, gravel, asphalt), its maps'
 *        pixels and its settings.
 *
 * A terrain material is an asset of the "terrain_material" type (JSON definitions in a resource pack's
 * terrain_materials/ folder, references/asset_structure.md, "Terrain materials"; load one with
 * AssetManager_LoadTerrainMaterial). A terrain names up to sixteen of them as its palette and loads them as its
 * dependencies (TerrainAsset.h); every vertex weighs them, a face may take one outright, and the terrain's rules move
 * the weights by slope, height and noise. The renderer makes one texture array per map kind of a terrain, one layer per
 * palette material, so the maps of one kind must share one size across a terrain's materials (the terrain
 * refuses a mismatch, naming both sizes).
 *
 * THE MAPS are texture assets the definition names (their image files are read on the CPU through
 * AssetManager_LoadTexturePixels, never uploaded as textures of their own):
 * - albedo (required): the colour, read as sRGB; it shares one map with the height, which goes into its alpha;
 * - height (optional, flat at mid-height without it): the surface's height for height blending (where materials meet,
 *   the one standing higher wins), its red channel, the same size as the albedo;
 * - normal (optional, flat without it): a tangent-space normal map, OpenGL's convention (green toward the image's top:
 *   what Blender bakes and glTF uses); its red and green are kept, the blue is rebuilt;
 * - roughness_ao (optional, roughness 1 and no occlusion without it): red is the roughness, green the ambient
 *   occlusion.
 * Each map's slot decides how it is read, whatever the texture's color_space says; its texture_properties do not apply
 * (the arrays are mipmapped, filtered and repeat). Every map is at most TERRAIN_MATERIAL_MAX_SIDE pixels per side.
 *
 * THE SETTINGS (TerrainMaterialSettings) are read through a property table (TerrainMaterialSettings_GetPropertyTable),
 * so the editor and the scripts read the same rows; unknown keys are refused when the definition is read.
 *
 * OWNERSHIP. The asset owns its pixels and settings; it has no dependencies (the texture assets are never loaded). The
 * asset manager owns the asset; never keep its pointer past your asset user (AssetManager.md). Read-only: the renderer
 * builds its texture arrays from the pixels, the terrain's C mirror of the blend (TerrainBlend.h) samples its height.
 */


// Macros.
/** @brief The most pixels a terrain material's map may have per side: sixteen materials' maps at this size are about
 *  128 MiB of pixels, kept on the CPU and uploaded once (a hostile pack cannot ask for gigabytes). */
#define TERRAIN_MATERIAL_MAX_SIDE 1024
/** @brief The most bytes a texture asset name or the surface key of a terrain material may have. */
#define TERRAIN_MATERIAL_NAME_MAX_LENGTH ((size_t)128)
/** @brief The surface a terrain material is unless its definition says (a namespaced key: footsteps, the scattering's
 *  limits and later systems read it). */
#define TERRAIN_MATERIAL_DEFAULT_SURFACE ((const unsigned char*)u8"tbp:ground")
/** @brief The height byte of a material without a height map: mid-height, so it neither wins nor loses every
 *  transition. */
#define TERRAIN_MATERIAL_FLAT_HEIGHT ((uint8_t)128U)
/** @brief A normal map's red and green bytes for a flat normal (x and y 0: bytes decode as (byte - 128) / 127). */
#define TERRAIN_MATERIAL_FLAT_NORMAL ((uint8_t)128U)


// Types.
/**
 * @brief When a terrain material is sampled from three sides (triplanar) instead of from above. Saved by name in
 *        definitions and by value in the table's GHDF form: never renumber.
 */
typedef enum TerrainTriplanarModeEnum
{
    /** @brief Always from above (planar along the world's X and Z): cheapest, stretched on steep faces. */
    TerrainTriplanarMode_Off = 0,
    /** @brief From above on gentle ground, from three sides where the ground is steeper than the material's
     *  triplanar_slope (blended in over a few degrees, so no seam shows). */
    TerrainTriplanarMode_Steep = 1,
    /** @brief Always from three sides, blended by the normal. */
    TerrainTriplanarMode_Always = 2
} TerrainTriplanarMode;

/** @brief The triplanar modes' names in definitions ("off", "steep", "always"). */
extern const PropertyEnumTable TERRAIN_TRIPLANAR_MODE_ENUM;

/**
 * @brief The kinds of map a terrain material has; each becomes one texture array of a terrain.
 */
typedef enum TerrainMaterialMapKindEnum
{
    /** @brief 4 bytes a texel: the albedo's red, green and blue (sRGB) and the height in alpha. Every material has it. */
    TerrainMaterialMapKind_AlbedoHeight = 0,
    /** @brief 2 bytes a texel: the tangent-space normal's x and y as the normal map has them (each (byte - 128) / 127;
     *  OpenGL's convention: x toward the image's right, y toward its top row). */
    TerrainMaterialMapKind_Normal = 1,
    /** @brief 2 bytes a texel: the roughness and the ambient occlusion (each byte / 255). */
    TerrainMaterialMapKind_RoughnessAo = 2,
    /** @brief The number of map kinds. */
    TerrainMaterialMapKind_Count = 3
} TerrainMaterialMapKind;

/**
 * @brief A terrain material's settings: its maps' texture names and its factors. The value its definition is read into
 *        through the "terrain_material" property table.
 *
 * Construct with TerrainMaterialSettings_SetDefaults, copy with Properties_CopyTo on the table, release with
 * TerrainMaterialSettings_Deconstruct (the names are owned strings).
 */
typedef struct TerrainMaterialSettingsStruct
{
    /** @brief The albedo's texture asset (owned; required: NULL only in the defaults). */
    unsigned char* Albedo;
    /** @brief The height map's texture asset (owned), or NULL: flat (TERRAIN_MATERIAL_FLAT_HEIGHT). */
    unsigned char* Height;
    /** @brief The normal map's texture asset (owned), or NULL: flat. */
    unsigned char* Normal;
    /** @brief The roughness and AO map's texture asset (owned), or NULL: roughness 1 (times Roughness), no occlusion. */
    unsigned char* RoughnessAo;
    /** @brief Metres per repeat of the maps (above 0, at most 1000; default 2). */
    float TileSize;
    /** @brief Multiplies the albedo (sRGB; default white). Its alpha is ignored. */
    Color AlbedoTint;
    /** @brief The roughness, multiplied by the map's (0 to 1; default 0.9). */
    float Roughness;
    /** @brief How strongly the normal map tilts the surface (0 flat to 8; default 1). */
    float NormalStrength;
    /** @brief How strongly its height wins at a transition (0 to 10; default 1): a material's share of a pixel is decided
     *  by its weight plus its height times this. */
    float HeightBlendStrength;
    /** @brief When it is sampled from three sides (default steep). */
    TerrainTriplanarMode Triplanar;
    /** @brief The slope, in radians (degrees in definitions), from which a "steep" material is sampled from three sides
     *  (0 to 90 degrees; default 40). */
    float TriplanarSlope;
    /** @brief What the ground is, a namespaced key in its full form (owned; default TERRAIN_MATERIAL_DEFAULT_SURFACE;
     *  "ground" in a definition reads as "tbp:ground"): footsteps, the scattering's limits, later systems. */
    unsigned char* Surface;
    /** @brief How much rain's puddles may form on it, 0 to 1 (default 0; asphalt 1, grass 0). For the rain's puddles
     *  (not built yet). */
    float Puddles;
    /** @brief How much the weather's wetness darkens it, 0 to 1 (default 0.5): a soaked surface of porosity 1 is 70 %
     *  darker (the lit shaders' ApplyWetness). */
    float Porosity;
} TerrainMaterialSettings;

/**
 * @brief One map of a loaded terrain material: its pixels and size. Read-only.
 */
typedef struct TerrainMaterialMapStruct
{
    /** @brief Width x Height texels, row by row from the image's top row, of TerrainMaterial_GetMapTexelSize bytes each
     *  (borrowed from the material); NULL when the material has no such map. */
    const uint8_t* _pixels;
    /** @brief The size in pixels (0 without the map). */
    int32_t _width;
    int32_t _height;
} TerrainMaterialMap;

/**
 * @brief A loaded terrain material. Read-only, owned by the asset manager: use the accessors.
 */
typedef struct TerrainMaterialStruct
{
    /** @brief Its settings (owned strings). */
    TerrainMaterialSettings _settings;
    /** @brief Its maps, indexed by TerrainMaterialMapKind; their pixels point into _pixels. */
    TerrainMaterialMap _maps[TerrainMaterialMapKind_Count];
    /** @brief One block holding every map's pixels (owned). */
    uint8_t* _pixels;
    /** @brief The albedo's mean colour in linear light, times the tint (linear): what the material looks like from
     *  afar, and what terrains draw without texture arrays. */
    Vector3 _meanColor;
} TerrainMaterial;


// Functions: settings.
/**
 * @brief Returns the "terrain_material" property table: the settings' keys, defaults and rules (TerrainMaterial.c). The
 *        definition is read through it; the editor's inspector and scripts will read the same rows.
 * @returns The static table.
 */
const PropertyTable* TerrainMaterialSettings_GetPropertyTable(void);

/**
 * @brief Fills @p self with the defaults (no maps, tile size 2, white, roughness 0.9, normal strength 1, height blend
 *        strength 1, triplanar from 40 degrees, the ground surface, no puddles, porosity 0.5).
 * @param self The settings; overwritten (not released first). Must not be NULL.
 * @returns Success, or the error Properties_Construct gives (then nothing is held).
 */
Error TerrainMaterialSettings_SetDefaults(TerrainMaterialSettings* self);

/**
 * @brief Releases the settings' strings and leaves them NULL. Safe on a zeroed or already deconstructed struct.
 * @param self The settings; must not be NULL.
 */
void TerrainMaterialSettings_Deconstruct(TerrainMaterialSettings* self);


// Functions: the material.
/** @brief The material's settings (borrowed: valid while the material is). */
static inline const TerrainMaterialSettings* TerrainMaterial_GetSettings(const TerrainMaterial* self)
{
    return &self->_settings;
}

/**
 * @brief How many bytes one texel of a map kind has (4 for the albedo and height, 2 for the others).
 * @param kind The map kind.
 * @returns The byte count; 0 for a value outside the enum.
 */
uint32_t TerrainMaterial_GetMapTexelSize(TerrainMaterialMapKind kind);

/**
 * @brief Reports whether the material has a map of a kind (the albedo and height always; the others only when the
 *        definition names their texture).
 * @param self The material; must not be NULL.
 * @param kind The map kind.
 * @returns true when the material holds pixels of that kind.
 */
static inline bool TerrainMaterial_HasMap(const TerrainMaterial* self, TerrainMaterialMapKind kind)
{
    return ((uint32_t)kind < (uint32_t)TerrainMaterialMapKind_Count) && (self->_maps[kind]._pixels != NULL);
}

/**
 * @brief A map's pixels and size.
 * @param self The material; must not be NULL.
 * @param kind The map kind.
 * @param outWidth [out] Receives its width (0 without the map); may be NULL.
 * @param outHeight [out] Receives its height (0 without the map); may be NULL.
 * @returns The pixels (borrowed: valid while the material is; TerrainMaterial_GetMapTexelSize bytes a texel), or NULL
 *          when the material has no map of that kind.
 */
const uint8_t* TerrainMaterial_GetMap(const TerrainMaterial* self, TerrainMaterialMapKind kind, int32_t* outWidth,
    int32_t* outHeight);

/**
 * @brief The albedo's mean colour in linear light, times the tint: what the material looks like from afar.
 * @param self The material; must not be NULL.
 * @returns Red, green and blue, linear, 0 to 1.
 */
static inline Vector3 TerrainMaterial_GetMeanColor(const TerrainMaterial* self)
{
    return self->_meanColor;
}

/**
 * @brief Samples the material's height at a texture coordinate, as the GPU's base level does: bilinear between the four
 *        nearest texels, repeating at the edges.
 *
 * O(1). A material without a height map is flat (TERRAIN_MATERIAL_FLAT_HEIGHT / 255).
 * @param self The material; must not be NULL.
 * @param u The texture coordinate along the image's rows (1 is one repeat; any finite value).
 * @param v The texture coordinate down the image's columns (0 the top row).
 * @returns The height, 0 to 1.
 */
float TerrainMaterial_SampleHeight(const TerrainMaterial* self, float u, float v);
