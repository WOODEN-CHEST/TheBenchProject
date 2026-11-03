#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// PropertyDescriptor.h for the settings' property table; raylib.h for the scattered colour (Color) and the absorption
// (Vector3).
#include "PropertyDescriptor.h"
#include "raylib/raylib.h"

/** @brief A standalone texture asset (GameTexture.h): a water material's normal maps and its foam load as them. */
typedef struct GameTextureStruct GameTexture;


/**
 * @file WaterMaterial.h
 * @brief A water material: the look of a body of water (a pond, a river, a fountain's basin, the sea), for any mesh.
 *
 * A water material is an asset of the "water_material" type (JSON definitions in a resource pack's water_materials/
 * folder, references/asset_structure.md, "Water materials"; load one with AssetManager_LoadWaterMaterial). A world model
 * names water materials for some of its material slots (its "water" section, WorldModel.h): those meshes are drawn as
 * water, never with their own materials; a variant may swap a slot's water material. There is no global sea: every
 * water is placed by hand, the sea a huge flat water model, so creators give each water its own look.
 *
 * THE LOOK. The surface's waves are two normal maps scrolling along the flow, summed; the wind moves and roughens them.
 * The surface mirrors what lies around it by water's Fresnel reflectance (the screen through the reflection pass, the sky
 * where it has no answer), glints in the sun, the moon and the lamps at its roughness, and rings while it rains. Under
 * it the bottom is seen straight through (no refraction), dimmed by the absorption over the view's path through the
 * water, while the water's own colour, lit by the sun, the moon and the sky, scatters in over that path: deep water
 * shows its colour alone. Foam lies where the water is shallow.
 *
 * THE FLOW. "wind": the waves drift along the wind (lakes, the sea), anchored to the world; "fixed": along a set
 * direction at a set speed, anchored to the world; "uv": along the mesh's texture coordinates' +V at a set speed (a
 * river's coordinates run along it), the maps laid on those coordinates.
 *
 * THE SETTINGS (WaterMaterialSettings) are read through a property table (WaterMaterialSettings_GetPropertyTable), so
 * the editor, scripts and a later per-placement override read the same rows; unknown keys are refused when the
 * definition is read.
 *
 * OWNERSHIP. The asset owns its settings; its maps are texture assets loaded as its DEPENDENCIES (holding the water
 * material holds them; they unload right after it), so the GameTexture pointers in it are valid exactly as long as the
 * water material is. The asset manager owns the asset; never keep its pointer past your asset user (AssetManager.md).
 * Read-only.
 */


// Macros.
/** @brief The most bytes a texture asset name (in full) a water material names may have. */
#define WATER_MATERIAL_NAME_MAX_LENGTH ((size_t)128)
/** @brief Water's reflectance facing it (Fresnel's F0 of water against air, 2 %): a material's reflectance multiplies
 *  it. */
#define WATER_MATERIAL_WATER_REFLECTANCE 0.02f
/** @brief The longest a map's tile may be, in metres (or texture coordinates along a river). */
#define WATER_MATERIAL_MAX_TILE_SIZE 10000.0
/** @brief The fastest a flow may run, in metres a second. */
#define WATER_MATERIAL_MAX_FLOW_SPEED 100.0
/** @brief The largest absorption, per metre: the water then hides what lies a few centimetres under it. */
#define WATER_MATERIAL_MAX_ABSORPTION 100.0
/** @brief The deepest foam may reach, in metres. */
#define WATER_MATERIAL_MAX_FOAM_DEPTH 20.0


// Types.
/**
 * @brief Which way a water material's waves move. Saved by name in definitions and by value in the table's GHDF form:
 *        never renumber.
 */
typedef enum WaterFlowEnum
{
    /** @brief Along the wind, anchored to the world: lakes, the sea. Its speed is the wind's share (wind_response). */
    WaterFlow_Wind = 0,
    /** @brief Along flow_angle at flow_speed, anchored to the world: a pond with a steady drift. */
    WaterFlow_Fixed = 1,
    /** @brief Along the mesh's texture coordinates' +V at flow_speed (V in metres along the flow), the maps laid on those
     *  coordinates: a river, a stream, a gutter. */
    WaterFlow_Uv = 2
} WaterFlow;

/** @brief The flows' names in definitions ("wind", "fixed", "uv"). */
extern const PropertyEnumTable WATER_FLOW_ENUM;

/**
 * @brief The maps a water material may name, each a texture asset.
 */
typedef enum WaterMaterialMapEnum
{
    /** @brief The first normal map: the larger waves (linear; OpenGL's convention, green toward the image's top). */
    WaterMaterialMap_NormalA = 0,
    /** @brief The second normal map: the smaller waves, turned from the first. */
    WaterMaterialMap_NormalB = 1,
    /** @brief The foam's pattern (its red channel, 0 to 1): a texel shows foam while the water under it is shallower than
     *  foam_depth times its value, so foam thins out away from the shore. */
    WaterMaterialMap_Foam = 2,
    /** @brief The number of maps. */
    WaterMaterialMap_Count = 3
} WaterMaterialMap;

/**
 * @brief A water material's settings: its maps' texture names and its factors. The value its definition is read into
 *        through the "water_material" property table.
 *
 * Construct with WaterMaterialSettings_SetDefaults, copy with Properties_CopyTo on the table, release with
 * WaterMaterialSettings_Deconstruct (the names are owned strings).
 */
typedef struct WaterMaterialSettingsStruct
{
    /** @brief The first normal map's texture asset (owned), or NULL: no large waves. */
    unsigned char* NormalA;
    /** @brief Metres per repeat of the first map (texture coordinates along a river; above 0; default 5). */
    float NormalATileSize;
    /** @brief How fast the first map moves, as a share of the flow's speed (0 to 10; default 1). */
    float NormalASpeed;
    /** @brief The second normal map's texture asset (owned), or NULL: no small waves. */
    unsigned char* NormalB;
    /** @brief Metres per repeat of the second map (above 0; default 1.7). */
    float NormalBTileSize;
    /** @brief How fast the second map moves, as a share of the flow's speed (0 to 10; default 0.6). */
    float NormalBSpeed;
    /** @brief How far the second map's layout and its motion are turned from the first's, in radians (degrees in
     *  definitions; default 35): the two patterns never line up. */
    float NormalBTurn;
    /** @brief How strongly the summed maps tilt the surface (0 flat to 8; default 1), before the wind. */
    float WaveStrength;
    /** @brief How much the wind moves and roughens the waves (0 to 2; default 1): their strength follows the wind's speed
     *  as wind-driven seas' slopes do (WorldWater.h), and with the "wind" flow they drift along it at this share of
     *  WORLD_WATER_WIND_DRIFT_SHARE of its speed. 0: the wind leaves the water as it is. */
    float WindResponse;
    /** @brief Which way the waves move (default wind). */
    WaterFlow Flow;
    /** @brief With the "fixed" flow: where the water flows toward, in radians around the vertical (degrees in
     *  definitions: 0 toward +Z, 90 toward +X, as the wind's direction). */
    float FlowAngle;
    /** @brief With the "fixed" and "uv" flows: how fast the water flows, metres a second (0 to
     *  WATER_MATERIAL_MAX_FLOW_SPEED; default 0.3). */
    float FlowSpeed;
    /** @brief What the water scatters back (sRGB; its alpha is ignored; default a dark blue-green): deep water's colour,
     *  lit by the sun, the moon and the sky, so it darkens at night by itself. */
    Color ScatterColor;
    /** @brief How fast the water takes the light of what lies under it, per metre of the view's path and colour channel
     *  (0 to WATER_MATERIAL_MAX_ABSORPTION each; default 0.45, 0.09, 0.06: red goes first): low for clear water, high for
     *  murky. */
    Vector3 Absorption;
    /** @brief The surface's roughness, the glints' size (0 to 1; default 0.06; the shader keeps at least 0.045). */
    float Roughness;
    /** @brief The share of water's reflectance (WATER_MATERIAL_WATER_REFLECTANCE) it mirrors with (0 to 5; default 1). */
    float Reflectance;
    /** @brief The foam's texture asset (owned), or NULL: no foam. */
    unsigned char* Foam;
    /** @brief Metres per repeat of the foam (above 0; default 2). */
    float FoamTileSize;
    /** @brief How deep the water may be and still show foam, metres (0 to WATER_MATERIAL_MAX_FOAM_DEPTH; default 0.3): at
     *  the shore all of the foam's pattern shows, at this depth only its brightest texels. */
    float FoamDepth;
    /** @brief How much of the surface the foam covers where it shows (0 to 1; default 0.9). */
    float FoamStrength;
    /** @brief Whether the rain's rings show on it while it rains where the rain reaches (default true). */
    bool HasRipples;
    /** @brief Whether it mirrors what the screen shows around it (the reflection pass; default true); false: the sky's
     *  reflection alone. */
    bool HasReflections;
} WaterMaterialSettings;

/**
 * @brief A loaded water material. Read-only, owned by the asset manager: use the accessors.
 */
typedef struct WaterMaterialStruct
{
    /** @brief Its settings (owned strings). */
    WaterMaterialSettings _settings;
    /** @brief Its maps, indexed by WaterMaterialMap, each held under the material's dependency user (NULL without one). */
    const GameTexture* _maps[WaterMaterialMap_Count];
} WaterMaterial;


// Functions: settings.
/**
 * @brief Returns the "water_material" property table: the settings' keys, defaults and rules (WaterMaterial.c). The
 *        definition is read through it; the editor's inspector and scripts will read the same rows.
 * @returns The static table.
 */
const PropertyTable* WaterMaterialSettings_GetPropertyTable(void);

/**
 * @brief Fills @p self with the defaults (no maps; the first map 5 m at the flow's speed, the second 1.7 m at 0.6 of it
 *        turned 35 degrees; wave strength 1; wind response 1; the wind's flow, 0.3 m/s along +Z for the other flows; a
 *        dark blue-green; absorption 0.45, 0.09, 0.06 per metre; roughness 0.06; reflectance 1; foam 2 m tiles to 0.3 m
 *        deep at 0.9; ripples and reflections on).
 * @param self The settings; overwritten (not released first). Must not be NULL.
 * @returns Success, or the error Properties_Construct gives (then nothing is held).
 */
Error WaterMaterialSettings_SetDefaults(WaterMaterialSettings* self);

/**
 * @brief Releases the settings' strings and leaves them NULL. Safe on a zeroed or already deconstructed struct.
 * @param self The settings; must not be NULL.
 */
void WaterMaterialSettings_Deconstruct(WaterMaterialSettings* self);

/**
 * @brief The texture asset name a map slot of the settings holds.
 * @param self The settings; must not be NULL.
 * @param map The map.
 * @returns The name (borrowed from the settings), or NULL when the settings name none (or for a value outside the enum).
 */
const unsigned char* WaterMaterialSettings_GetMapName(const WaterMaterialSettings* self, WaterMaterialMap map);


// Functions: the material.
/** @brief The material's settings (borrowed: valid while the material is). @param self Must not be NULL. */
static inline const WaterMaterialSettings* WaterMaterial_GetSettings(const WaterMaterial* self)
{
    return &self->_settings;
}

/**
 * @brief One of the material's maps.
 * @param self The material; must not be NULL.
 * @param map The map.
 * @returns The texture (valid while the material is loaded), or NULL when the material names none (or for a value
 *          outside the enum).
 */
static inline const GameTexture* WaterMaterial_GetMap(const WaterMaterial* self, WaterMaterialMap map)
{
    return ((uint32_t)map < (uint32_t)WaterMaterialMap_Count) ? self->_maps[map] : NULL;
}
