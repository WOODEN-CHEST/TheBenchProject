// The world sprite asset's data functions: the enums' names, the settings' property tables and their defaults. The
// definition and the loader are in WorldSpriteDefinition.c; the rows are shared with the sprite component's table
// (WorldSpriteInternal.h).

#include "WorldSprite.h"
#include "WorldSpriteInternal.h"


// Macros.
/** The format version of the world sprite's tables. */
#define WORLD_SPRITE_TABLE_VERSION 1U


// Fields.
static const PropertyEnumEntry OrientationEntries[] =
{
    { .Name = u8"billboard", .Value = WorldSpriteOrientation_Billboard },
    { .Name = u8"axis_billboard", .Value = WorldSpriteOrientation_AxisBillboard },
    { .Name = u8"fixed", .Value = WorldSpriteOrientation_Fixed },
};

const PropertyEnumTable WORLD_SPRITE_ORIENTATION_ENUM =
{
    .Name = u8"sprite_orientation", .Entries = OrientationEntries,
    .EntryCount = sizeof(OrientationEntries) / sizeof(OrientationEntries[0])
};

static const PropertyEnumEntry AlphaModeEntries[] =
{
    { .Name = u8"cutout", .Value = WorldSpriteAlphaMode_Cutout },
    { .Name = u8"blend", .Value = WorldSpriteAlphaMode_Blend },
    { .Name = u8"additive", .Value = WorldSpriteAlphaMode_Additive },
};

const PropertyEnumTable WORLD_SPRITE_ALPHA_MODE_ENUM =
{
    .Name = u8"sprite_alpha_mode", .Entries = AlphaModeEntries,
    .EntryCount = sizeof(AlphaModeEntries) / sizeof(AlphaModeEntries[0])
};

/* The keys of a definition's root, in the reference's order. World sprites are JSON only; each row takes the sprite
 * component's wire id for the same setting. has_draw_distance is the asset's own row (a world sprite gives a draw
 * distance or leaves it to its kind): it takes wire 32, the component's overrides_draw_distance, the flag in the same
 * place. */
static const PropertyDescriptor SettingsProperties[] =
{
    { WORLD_SPRITE_ROW_ORIENTATION, .WireId = 4U, PROPERTY_MEMBER(WorldSpriteSettings, Orientation), .SinceVersion = 1U },
    { WORLD_SPRITE_ROW_PIVOT, .WireId = 5U, PROPERTY_MEMBER(WorldSpriteSettings, Pivot), .SinceVersion = 1U },
    {
        WORLD_SPRITE_ROW_PIXELS_PER_UNIT, .WireId = 6U, PROPERTY_MEMBER(WorldSpriteSettings, PixelsPerUnit),
        .SinceVersion = 1U
    },
    { WORLD_SPRITE_ROW_FLIP_X, .WireId = 7U, PROPERTY_MEMBER(WorldSpriteSettings, IsFlippedX), .SinceVersion = 1U },
    { WORLD_SPRITE_ROW_FLIP_Y, .WireId = 8U, PROPERTY_MEMBER(WorldSpriteSettings, IsFlippedY), .SinceVersion = 1U },
    { WORLD_SPRITE_ROW_ALPHA_MODE, .WireId = 11U, PROPERTY_MEMBER(WorldSpriteSettings, AlphaMode), .SinceVersion = 1U },
    {
        WORLD_SPRITE_ROW_ALPHA_CUTOFF, .WireId = 12U, PROPERTY_MEMBER(WorldSpriteSettings, AlphaCutoff),
        .SinceVersion = 1U
    },
    { WORLD_SPRITE_ROW_IS_LIT, .WireId = 9U, PROPERTY_MEMBER(WorldSpriteSettings, IsLit), .SinceVersion = 1U },
    { WORLD_SPRITE_ROW_EMISSION, .WireId = 10U, PROPERTY_MEMBER(WorldSpriteSettings, Emission), .SinceVersion = 1U },
    { WORLD_SPRITE_ROW_LIGHT_WRAP, .WireId = 16U, PROPERTY_MEMBER(WorldSpriteSettings, LightWrap), .SinceVersion = 1U },
    { WORLD_SPRITE_ROW_IS_DECAL, .WireId = 13U, PROPERTY_MEMBER(WorldSpriteSettings, IsDecal), .SinceVersion = 1U },
    {
        WORLD_SPRITE_ROW_DECAL_OFFSET, .WireId = 14U, PROPERTY_MEMBER(WorldSpriteSettings, DecalOffset),
        .SinceVersion = 1U
    },
    {
        .Key = u8"has_draw_distance", .WireId = 32U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldSpriteSettings, HasDrawDistance), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_SPRITE_CATEGORY_DRAW_DISTANCE,
        .DisplayName = u8"Own draw distance",
        .Description = u8"Uses this world sprite's draw distance instead of its kind's (150 m, or 60 m for a decal). "
            u8"A definition that gives draw_distance without this key has it on.",
        .SinceVersion = 1U
    },
    {
        WORLD_SPRITE_ROW_DRAW_DISTANCE, .WireId = 33U, PROPERTY_MEMBER(WorldSpriteSettings, DrawDistance),
        .SinceVersion = 1U
    },
    {
        WORLD_SPRITE_ROW_IS_DRAW_DISTANCE_FIXED, .WireId = 36U,
        PROPERTY_MEMBER(WorldSpriteSettings, IsDrawDistanceFixed), .SinceVersion = 1U
    },
};

static const PropertyTable SettingsTable =
{
    .Name = u8"world_sprite",
    .Version = WORLD_SPRITE_TABLE_VERSION,
    .StructSize = sizeof(WorldSpriteSettings),
    .Descriptors = SettingsProperties,
    .DescriptorCount = sizeof(SettingsProperties) / sizeof(SettingsProperties[0]),
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = NULL,
    .Migrate = NULL
};

/* The keys of a definition's "render" block, each row with the sprite component's wire id for the same setting.
 * has_shadow_distance is the asset's own row (as has_draw_distance at the root): it takes wire 39, the component's
 * overrides_shadow_distance, the flag in the same place. */
static const PropertyDescriptor RenderProperties[] =
{
    {
        WORLD_SPRITE_ROW_HAS_OUTLINE, .WireId = 2U, PROPERTY_MEMBER(WorldSpriteRenderSettings, HasOutline),
        .SinceVersion = 1U
    },
    {
        WORLD_SPRITE_ROW_OMIT_PIXELATION, .WireId = 3U, PROPERTY_MEMBER(WorldSpriteRenderSettings, OmitsPixelation),
        .SinceVersion = 1U
    },
    {
        WORLD_SPRITE_ROW_CASTS_SHADOWS, .WireId = 15U, PROPERTY_MEMBER(WorldSpriteRenderSettings, CastsShadows),
        .SinceVersion = 1U
    },
    {
        .Key = u8"has_shadow_distance", .WireId = 39U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldSpriteRenderSettings, HasShadowDistance), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_SPRITE_CATEGORY_SHADOWS, .DisplayName = u8"Own shadow distance",
        .Description = u8"Uses this world sprite's shadow distance instead of casting as far as it is drawn. A definition "
            u8"that gives shadow_distance without this key has it on.",
        .SinceVersion = 1U
    },
    {
        WORLD_SPRITE_ROW_SHADOW_DISTANCE, .WireId = 40U, PROPERTY_MEMBER(WorldSpriteRenderSettings, ShadowDistance),
        .SinceVersion = 1U
    },
    {
        WORLD_SPRITE_ROW_IS_SHADOW_DISTANCE_FIXED, .WireId = 43U,
        PROPERTY_MEMBER(WorldSpriteRenderSettings, IsShadowDistanceFixed), .SinceVersion = 1U
    },
    {
        WORLD_SPRITE_ROW_IS_SHADOW_ONLY, .WireId = 45U, PROPERTY_MEMBER(WorldSpriteRenderSettings, IsShadowOnly),
        .SinceVersion = 1U
    },
    { WORLD_SPRITE_ROW_HAZE, .WireId = 38U, PROPERTY_MEMBER(WorldSpriteRenderSettings, Haze), .SinceVersion = 1U },
};

static const PropertyTable RenderTable =
{
    .Name = u8"world_sprite_render",
    .Version = WORLD_SPRITE_TABLE_VERSION,
    .StructSize = sizeof(WorldSpriteRenderSettings),
    .Descriptors = RenderProperties,
    .DescriptorCount = sizeof(RenderProperties) / sizeof(RenderProperties[0]),
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = NULL,
    .Migrate = NULL
};


// Public functions.
const PropertyTable* WorldSprite_GetSettingsPropertyTable(void)
{
    return &SettingsTable;
}

const PropertyTable* WorldSprite_GetRenderPropertyTable(void)
{
    return &RenderTable;
}

WorldSpriteSettings WorldSpriteSettings_CreateDefault(void)
{
    // The defaults are the tables' (one copy, shared with the sprite component). Neither table holds strings, so
    // constructing cannot fail and needs no deconstruction.
    WorldSpriteSettings Settings;
    Error Result = Properties_Construct(&SettingsTable, &Settings);
    Error_Deconstruct(&Result);
    Result = Properties_Construct(&RenderTable, &Settings.Render);
    Error_Deconstruct(&Result);
    return Settings;
}
