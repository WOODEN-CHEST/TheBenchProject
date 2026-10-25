#include <math.h>
#include "TerrainMaterial.h"
#include "NamespacedKey.h"

/*
 * The terrain material's data functions (include/TerrainMaterial.h): its settings' property table and the loaded
 * material's accessors and height sampling. The asset type itself (the definition's parser and the loader that reads
 * the maps' pixels) is source/TerrainMaterialDefinition.c.
 */


// Macros.
/** The settings' inspector groups. */
#define CATEGORY_MAPS ((const unsigned char*)u8"Maps")
#define CATEGORY_LOOK ((const unsigned char*)u8"Look")
#define CATEGORY_GAMEPLAY ((const unsigned char*)u8"Gameplay")
/** The largest triplanar slope, 90 degrees, as a float's radians: the JSON's degrees round to the float just above pi / 2,
 *  which an exact pi / 2 bound would refuse. */
#define MAX_TRIPLANAR_SLOPE 1.5707964
/** The default triplanar slope: 40 degrees in radians. */
#define DEFAULT_TRIPLANAR_SLOPE 0.6981317f


// Fields.
static const PropertyEnumEntry TriplanarModeEntries[] =
{
    { u8"off", (int32_t)TerrainTriplanarMode_Off },
    { u8"steep", (int32_t)TerrainTriplanarMode_Steep },
    { u8"always", (int32_t)TerrainTriplanarMode_Always }
};

const PropertyEnumTable TERRAIN_TRIPLANAR_MODE_ENUM =
{
    .Name = u8"triplanar_mode", .Entries = TriplanarModeEntries,
    .EntryCount = sizeof(TriplanarModeEntries) / sizeof(TriplanarModeEntries[0])
};

/* Every setting, in inspector order. The wire ids are the GHDF schema: never renumber or reuse one. The next free wire id
 * is 15. */
static const PropertyDescriptor TerrainMaterialProperties[] =
{
    {
        .Key = u8"albedo", .WireId = 1U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(TerrainMaterialSettings, Albedo), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(TERRAIN_MATERIAL_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MAPS, .DisplayName = u8"Albedo",
        .Description = u8"The texture of its colour (read as sRGB). Required.", .SinceVersion = 1U
    },
    {
        .Key = u8"height", .WireId = 2U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(TerrainMaterialSettings, Height), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(TERRAIN_MATERIAL_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MAPS, .DisplayName = u8"Height",
        .Description = u8"The texture of its surface's height (its red channel, the albedo's size): where materials meet, "
            u8"the one standing higher wins, so stones poke through grass. None is flat, at mid-height.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"normal", .WireId = 3U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(TerrainMaterialSettings, Normal), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(TERRAIN_MATERIAL_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MAPS, .DisplayName = u8"Normal map",
        .Description = u8"A tangent-space normal map in OpenGL's convention (green toward the image's top, as Blender "
            u8"bakes). None is flat.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"roughness_ao", .WireId = 4U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(TerrainMaterialSettings, RoughnessAo), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(TERRAIN_MATERIAL_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MAPS, .DisplayName = u8"Roughness and AO",
        .Description = u8"A texture with the roughness in red and the ambient occlusion in green. None is rough, without "
            u8"occlusion.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"tile_size", .WireId = 5U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(TerrainMaterialSettings, TileSize), .Default.FloatValue = 2.0f,
        .Rules = { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true,
            .Maximum = 1000.0 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MAPS, .DisplayName = u8"Tile size",
        .Description = u8"Metres per repeat of its maps, laid along the world's axes.", .SinceVersion = 1U
    },
    {
        .Key = u8"albedo_tint", .WireId = 6U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(TerrainMaterialSettings, AlbedoTint), .Default.ColorValue = { 255, 255, 255, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LOOK, .DisplayName = u8"Albedo tint",
        .Description = u8"Multiplies its colour.", .SinceVersion = 1U
    },
    {
        .Key = u8"roughness", .WireId = 7U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(TerrainMaterialSettings, Roughness), .Default.FloatValue = 0.9f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LOOK,
        .DisplayName = u8"Roughness", .Description = u8"Its roughness, multiplied by its map's.", .SinceVersion = 1U
    },
    {
        .Key = u8"normal_strength", .WireId = 8U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(TerrainMaterialSettings, NormalStrength), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 8.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LOOK,
        .DisplayName = u8"Normal strength", .Description = u8"How strongly its normal map tilts the surface (0 flat).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"height_blend_strength", .WireId = 9U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(TerrainMaterialSettings, HeightBlendStrength), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 10.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LOOK,
        .DisplayName = u8"Height blend strength",
        .Description = u8"How strongly its height wins where it meets another material: its share of a pixel is its "
            u8"weight plus its height times this. 0 blends by the weights alone.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"triplanar", .WireId = 10U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(TerrainMaterialSettings, Triplanar), .Default.EnumValue = (int32_t)TerrainTriplanarMode_Steep,
        .EnumTable = &TERRAIN_TRIPLANAR_MODE_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LOOK,
        .DisplayName = u8"Triplanar",
        .Description = u8"When its maps are laid from three sides instead of from above, so cliffs are not stretched: "
            u8"\"off\", \"steep\" (from the triplanar slope) or \"always\".",
        .SinceVersion = 1U
    },
    {
        .Key = u8"triplanar_slope", .WireId = 11U, .Type = PropertyType_Angle,
        PROPERTY_MEMBER(TerrainMaterialSettings, TriplanarSlope), .Default.FloatValue = DEFAULT_TRIPLANAR_SLOPE,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, MAX_TRIPLANAR_SLOPE), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_LOOK, .DisplayName = u8"Triplanar slope",
        .Description = u8"With \"steep\": the slope, in degrees, from which its maps are laid from three sides.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"surface", .WireId = 12U, .Type = PropertyType_String,
        PROPERTY_MEMBER(TerrainMaterialSettings, Surface), .Default.StringValue = TERRAIN_MATERIAL_DEFAULT_SURFACE,
        .Rules = PROPERTY_RULES_STRING(NAMESPACED_KEY_MAX_LENGTH, false), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_GAMEPLAY, .DisplayName = u8"Surface",
        .Description = u8"What the ground is, a namespaced key (\"ground\" is \"tbp:ground\"): footsteps, where things "
            u8"may be scattered, later systems.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"puddles", .WireId = 13U, .Type = PropertyType_Float, PROPERTY_MEMBER(TerrainMaterialSettings, Puddles),
        .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_GAMEPLAY, .DisplayName = u8"Puddles",
        .Description = u8"How much rain's puddles may form on it: 0 none (grass), 1 fully (asphalt).", .SinceVersion = 1U
    },
    {
        .Key = u8"porosity", .WireId = 14U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(TerrainMaterialSettings, Porosity), .Default.FloatValue = 0.5f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_GAMEPLAY,
        .DisplayName = u8"Porosity", .Description = u8"How much wetness darkens it: 0 not at all, 1 most (soil).",
        .SinceVersion = 1U
    }
};

static const PropertyTable TerrainMaterialTable =
{
    .Name = u8"terrain_material", .Version = 1U, .StructSize = sizeof(TerrainMaterialSettings),
    .Descriptors = TerrainMaterialProperties,
    .DescriptorCount = sizeof(TerrainMaterialProperties) / sizeof(TerrainMaterialProperties[0]),
    .RetiredWireIds = NULL, .RetiredWireIdCount = 0U, .AfterSet = NULL, .Migrate = NULL
};


// Static functions.
/* A height byte of the albedo's map at a texel (wrapped onto the map). */
static float HeightAt(const TerrainMaterialMap* map, int64_t x, int64_t y)
{
    int64_t Width = (int64_t)map->_width;
    int64_t Height = (int64_t)map->_height;
    int64_t X = ((x % Width) + Width) % Width;
    int64_t Y = ((y % Height) + Height) % Height;
    return (float)map->_pixels[(((size_t)Y * (size_t)Width) + (size_t)X) * 4U + 3U] / 255.0f;
}


// Public functions: settings.
const PropertyTable* TerrainMaterialSettings_GetPropertyTable(void)
{
    return &TerrainMaterialTable;
}

Error TerrainMaterialSettings_SetDefaults(TerrainMaterialSettings* self)
{
    return Properties_Construct(&TerrainMaterialTable, self);
}

void TerrainMaterialSettings_Deconstruct(TerrainMaterialSettings* self)
{
    Properties_Deconstruct(&TerrainMaterialTable, self);
}


// Public functions: the material.
uint32_t TerrainMaterial_GetMapTexelSize(TerrainMaterialMapKind kind)
{
    switch (kind)
    {
        case TerrainMaterialMapKind_AlbedoHeight: return 4U;
        case TerrainMaterialMapKind_Normal: return 2U;
        case TerrainMaterialMapKind_RoughnessAo: return 2U;
        case TerrainMaterialMapKind_Count: return 0U;
    }
    return 0U;
}

const uint8_t* TerrainMaterial_GetMap(const TerrainMaterial* self, TerrainMaterialMapKind kind, int32_t* outWidth,
    int32_t* outHeight)
{
    bool IsKnown = (uint32_t)kind < (uint32_t)TerrainMaterialMapKind_Count;
    const TerrainMaterialMap* Map = IsKnown ? &self->_maps[kind] : NULL;
    if (outWidth != NULL)
    {
        *outWidth = (Map != NULL) ? Map->_width : 0;
    }
    if (outHeight != NULL)
    {
        *outHeight = (Map != NULL) ? Map->_height : 0;
    }
    return (Map != NULL) ? Map->_pixels : NULL;
}

float TerrainMaterial_SampleHeight(const TerrainMaterial* self, float u, float v)
{
    const TerrainMaterialMap* Map = &self->_maps[TerrainMaterialMapKind_AlbedoHeight];
    if ((Map->_pixels == NULL) || (Map->_width <= 0) || (Map->_height <= 0) || !isfinite(u) || !isfinite(v))
    {
        return (float)TERRAIN_MATERIAL_FLAT_HEIGHT / 255.0f;
    }
    // Wrapped into one repeat first, so the texel coordinates stay small whatever the coordinate; texel centres sit at
    // (i + 0.5) / size, as the GPU's.
    float X = ((u - floorf(u)) * (float)Map->_width) - 0.5f;
    float Y = ((v - floorf(v)) * (float)Map->_height) - 0.5f;
    float X0 = floorf(X);
    float Y0 = floorf(Y);
    float Fx = X - X0;
    float Fy = Y - Y0;
    int64_t Left = (int64_t)X0;
    int64_t Top = (int64_t)Y0;
    float Upper = (HeightAt(Map, Left, Top) * (1.0f - Fx)) + (HeightAt(Map, Left + 1, Top) * Fx);
    float Lower = (HeightAt(Map, Left, Top + 1) * (1.0f - Fx)) + (HeightAt(Map, Left + 1, Top + 1) * Fx);
    return (Upper * (1.0f - Fy)) + (Lower * Fy);
}
