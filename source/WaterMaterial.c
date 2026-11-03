#include "WaterMaterial.h"

/*
 * The water material's data functions (include/WaterMaterial.h): its settings' property table and their accessors. The
 * asset type itself (the definition's parser and the loader that loads the maps as dependencies) is
 * source/WaterMaterialDefinition.c.
 */


// Macros.
/** The settings' inspector groups. */
#define CATEGORY_WAVES ((const unsigned char*)u8"Waves")
#define CATEGORY_FLOW ((const unsigned char*)u8"Flow")
#define CATEGORY_LOOK ((const unsigned char*)u8"Look")
#define CATEGORY_SHORE ((const unsigned char*)u8"Shore")
/** The second map's default turn from the first: 35 degrees in radians. */
#define DEFAULT_NORMAL_B_TURN 0.61086524f
/** The largest share of the flow's speed a map may move at, and the largest wave strength. */
#define MAX_MAP_SPEED 10.0
#define MAX_WAVE_STRENGTH 8.0
/** The largest wind response: twice the wind-driven sea's growth. */
#define MAX_WIND_RESPONSE 2.0
/** The largest reflectance: five times water's (about a glass's at 10 %). */
#define MAX_REFLECTANCE 5.0


// Fields.
static const PropertyEnumEntry FlowEntries[] =
{
    { u8"wind", (int32_t)WaterFlow_Wind },
    { u8"fixed", (int32_t)WaterFlow_Fixed },
    { u8"uv", (int32_t)WaterFlow_Uv }
};

const PropertyEnumTable WATER_FLOW_ENUM =
{
    .Name = u8"water_flow", .Entries = FlowEntries, .EntryCount = sizeof(FlowEntries) / sizeof(FlowEntries[0])
};

/* Every setting, in inspector order. The wire ids are the GHDF schema: never renumber or reuse one. The next free wire id
 * is 23. */
static const PropertyDescriptor WaterMaterialProperties[] =
{
    {
        .Key = u8"normal_a", .WireId = 1U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WaterMaterialSettings, NormalA), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(WATER_MATERIAL_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_WAVES, .DisplayName = u8"Normal map A",
        .Description = u8"The larger waves: a tileable tangent-space normal map in OpenGL's convention (green toward the "
            u8"image's top), read as linear. None: no large waves.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"normal_a_tile_size", .WireId = 2U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, NormalATileSize), .Default.FloatValue = 5.0f,
        .Rules = { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true,
            .Maximum = WATER_MATERIAL_MAX_TILE_SIZE },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_WAVES, .DisplayName = u8"Tile size A",
        .Description = u8"Metres per repeat of normal map A (texture coordinates per repeat along a river).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"normal_a_speed", .WireId = 3U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, NormalASpeed), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, MAX_MAP_SPEED), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_WAVES, .DisplayName = u8"Speed A",
        .Description = u8"How fast normal map A moves, as a share of the flow's speed.", .SinceVersion = 1U
    },
    {
        .Key = u8"normal_b", .WireId = 4U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WaterMaterialSettings, NormalB), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(WATER_MATERIAL_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_WAVES, .DisplayName = u8"Normal map B",
        .Description = u8"The smaller waves, a second tileable normal map summed with the first. None: no small waves.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"normal_b_tile_size", .WireId = 5U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, NormalBTileSize), .Default.FloatValue = 1.7f,
        .Rules = { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true,
            .Maximum = WATER_MATERIAL_MAX_TILE_SIZE },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_WAVES, .DisplayName = u8"Tile size B",
        .Description = u8"Metres per repeat of normal map B.", .SinceVersion = 1U
    },
    {
        .Key = u8"normal_b_speed", .WireId = 6U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, NormalBSpeed), .Default.FloatValue = 0.6f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, MAX_MAP_SPEED), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_WAVES, .DisplayName = u8"Speed B",
        .Description = u8"How fast normal map B moves, as a share of the flow's speed.", .SinceVersion = 1U
    },
    {
        .Key = u8"normal_b_turn", .WireId = 7U, .Type = PropertyType_Angle,
        PROPERTY_MEMBER(WaterMaterialSettings, NormalBTurn), .Default.FloatValue = DEFAULT_NORMAL_B_TURN,
        .Rules = PROPERTY_RULES_NONE, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_WAVES,
        .DisplayName = u8"Turn B",
        .Description = u8"How far normal map B's layout and motion are turned from map A's, in degrees, so the two "
            u8"patterns never line up.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"wave_strength", .WireId = 8U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, WaveStrength), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, MAX_WAVE_STRENGTH), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_WAVES, .DisplayName = u8"Wave strength",
        .Description = u8"How strongly the summed normal maps tilt the surface (0 flat), in a 5 m/s breeze.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"wind_response", .WireId = 9U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, WindResponse), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, MAX_WIND_RESPONSE), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_WAVES, .DisplayName = u8"Wind response",
        .Description = u8"How much the wind moves and roughens the waves: at 1 they are a third as strong in still air as "
            u8"in a 5 m/s breeze and twice as strong in a gale, as wind-driven seas are, and with the wind's flow they "
            u8"drift along it at 3 % of its speed. 0: the wind leaves the water as it is.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"flow", .WireId = 10U, .Type = PropertyType_Enum, PROPERTY_MEMBER(WaterMaterialSettings, Flow),
        .Default.EnumValue = (int32_t)WaterFlow_Wind, .EnumTable = &WATER_FLOW_ENUM, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_FLOW, .DisplayName = u8"Flow",
        .Description = u8"Which way the waves move: \"wind\" along the wind (lakes, the sea), \"fixed\" along the flow "
            u8"angle at the flow speed, \"uv\" along the mesh's texture coordinates' +V at the flow speed (a river whose "
            u8"V runs along it, in metres), its maps laid on those coordinates.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"flow_angle", .WireId = 11U, .Type = PropertyType_Angle,
        PROPERTY_MEMBER(WaterMaterialSettings, FlowAngle), .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_NONE,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_FLOW, .DisplayName = u8"Flow angle",
        .Description = u8"With the fixed flow: where the water flows toward, in degrees around the vertical (0 toward +Z, "
            u8"90 toward +X).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"flow_speed", .WireId = 12U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, FlowSpeed), .Default.FloatValue = 0.3f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WATER_MATERIAL_MAX_FLOW_SPEED), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_FLOW, .DisplayName = u8"Flow speed",
        .Description = u8"With the fixed and uv flows: how fast the water flows, in metres a second.", .SinceVersion = 1U
    },
    {
        .Key = u8"color", .WireId = 13U, .Type = PropertyType_Color,
        PROPERTY_MEMBER(WaterMaterialSettings, ScatterColor), .Default.ColorValue = { 26, 50, 56, 255 },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LOOK, .DisplayName = u8"Colour",
        .Description = u8"What the water scatters back: deep water's colour, lit by the sun, the moon and the sky.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"absorption", .WireId = 14U, .Type = PropertyType_Vector3,
        PROPERTY_MEMBER(WaterMaterialSettings, Absorption), .Default.Vector3Value = { 0.45f, 0.09f, 0.06f },
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WATER_MATERIAL_MAX_ABSORPTION), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_LOOK, .DisplayName = u8"Absorption",
        .Description = u8"How fast the water takes the light of what lies under it, per metre of the view's path, for red, "
            u8"green and blue: low for clear water (its bottom shows deep down), high for murky.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"roughness", .WireId = 15U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, Roughness), .Default.FloatValue = 0.06f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LOOK,
        .DisplayName = u8"Roughness",
        .Description = u8"The surface's roughness: the sun's, the moon's and the lamps' glints grow and blur with it.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"reflectance", .WireId = 16U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, Reflectance), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, MAX_REFLECTANCE), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_LOOK, .DisplayName = u8"Reflectance",
        .Description = u8"How much it mirrors, as a share of water's 2 % facing it (toward grazing angles any water mirrors "
            u8"more).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"foam", .WireId = 17U, .Type = PropertyType_AssetName, PROPERTY_MEMBER(WaterMaterialSettings, Foam),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(WATER_MATERIAL_NAME_MAX_LENGTH, true),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SHORE, .DisplayName = u8"Foam",
        .Description = u8"The foam's pattern, a tileable texture (its red channel): a texel shows foam while the water is "
            u8"shallower than the foam depth times its value. None: no foam.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"foam_tile_size", .WireId = 18U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, FoamTileSize), .Default.FloatValue = 2.0f,
        .Rules = { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true,
            .Maximum = WATER_MATERIAL_MAX_TILE_SIZE },
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SHORE, .DisplayName = u8"Foam tile size",
        .Description = u8"Metres per repeat of the foam's pattern (it drifts with the flow).", .SinceVersion = 1U
    },
    {
        .Key = u8"foam_depth", .WireId = 19U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, FoamDepth), .Default.FloatValue = 0.3f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, WATER_MATERIAL_MAX_FOAM_DEPTH), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SHORE, .DisplayName = u8"Foam depth",
        .Description = u8"How deep the water may be and still show foam, in metres: at the shore the whole pattern shows, "
            u8"at this depth only its brightest texels.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"foam_strength", .WireId = 20U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WaterMaterialSettings, FoamStrength), .Default.FloatValue = 0.9f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SHORE,
        .DisplayName = u8"Foam strength", .Description = u8"How much of the surface the foam covers where it shows.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"has_ripples", .WireId = 21U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WaterMaterialSettings, HasRipples), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_LOOK, .DisplayName = u8"Ripples",
        .Description = u8"Whether the rain's rings show on it while it rains where the rain reaches.", .SinceVersion = 1U
    },
    {
        .Key = u8"has_reflections", .WireId = 22U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WaterMaterialSettings, HasReflections), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LOOK, .DisplayName = u8"Reflections",
        .Description = u8"Whether it mirrors what the screen shows around it (the town, the hills, the sky with its "
            u8"clouds); off, it mirrors the sky alone.",
        .SinceVersion = 1U
    }
};

static const PropertyTable WaterMaterialTable =
{
    .Name = u8"water_material", .Version = 1U, .StructSize = sizeof(WaterMaterialSettings),
    .Descriptors = WaterMaterialProperties,
    .DescriptorCount = sizeof(WaterMaterialProperties) / sizeof(WaterMaterialProperties[0]),
    .RetiredWireIds = NULL, .RetiredWireIdCount = 0U, .AfterSet = NULL, .Migrate = NULL
};


// Public functions.
const PropertyTable* WaterMaterialSettings_GetPropertyTable(void)
{
    return &WaterMaterialTable;
}

Error WaterMaterialSettings_SetDefaults(WaterMaterialSettings* self)
{
    return Properties_Construct(&WaterMaterialTable, self);
}

void WaterMaterialSettings_Deconstruct(WaterMaterialSettings* self)
{
    Properties_Deconstruct(&WaterMaterialTable, self);
}

const unsigned char* WaterMaterialSettings_GetMapName(const WaterMaterialSettings* self, WaterMaterialMap map)
{
    switch (map)
    {
        case WaterMaterialMap_NormalA: return self->NormalA;
        case WaterMaterialMap_NormalB: return self->NormalB;
        case WaterMaterialMap_Foam: return self->Foam;
        case WaterMaterialMap_Count: return NULL;
    }
    return NULL;
}
