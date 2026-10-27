#pragma once
#include "PropertyDescriptor.h"
#include "WorldModel.h"
#include "WorldModelComponent.h"

/*
 * Private header (source/): the property rows of a world model's placement, shared by the model component's table
 * (WorldModelComponent.c: one placement) and the model instances component's table (WorldModelInstancesComponent.c:
 * many placements of one world model). Each macro holds a row's key, type, default, rules, flags and texts, so a setting
 * has them once whichever component stores it; a table's row adds its wire id, its member (PROPERTY_MEMBER) and its
 * SinceVersion:
 *
 *     { WORLD_MODEL_PLACEMENT_ROW_HAS_OUTLINE, .WireId = 2U, PROPERTY_MEMBER(WorldModelComponent, _hasOutline),
 *         .SinceVersion = 1U }
 *
 * Both tables give a setting the same wire id. Instances are never crisp: the instances table leaves out the two
 * omit_pixelation rows (wire ids 3 and 9 are the model component's alone); the instances table's list is wire id 22,
 * which the model component leaves unused, the haze's pair is 23 and 24 in both, and the sky's pair (occludes_sky) 25
 * and 26.
 */


// Macros: the inspector's categories.
#define WORLD_MODEL_PLACEMENT_CATEGORY_MODEL ((const unsigned char*)u8"Model")
#define WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING ((const unsigned char*)u8"Rendering")
#define WORLD_MODEL_PLACEMENT_CATEGORY_DRAW_DISTANCE ((const unsigned char*)u8"Draw distance")
#define WORLD_MODEL_PLACEMENT_CATEGORY_LODS ((const unsigned char*)u8"Level of detail")


// Macros: the rows (in the inspector's order).
#define WORLD_MODEL_PLACEMENT_ROW_WORLD_MODEL \
    .Key = u8"world_model", .Type = PropertyType_AssetName, .Default.StringValue = NULL, \
    .Rules = PROPERTY_RULES_STRING(WORLD_MODEL_COMPONENT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_MODEL, .DisplayName = u8"World model", \
    .Description = u8"The world model to draw (its LOD models, draw distance, render defaults and variants); none " \
        u8"draws nothing."

#define WORLD_MODEL_PLACEMENT_ROW_USES_VARIANT \
    .Key = u8"uses_variant", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_MODEL, .DisplayName = u8"Use a variant", \
    .Description = u8"Draws the object with one of its world model's variants instead of the model's own materials."

#define WORLD_MODEL_PLACEMENT_ROW_VARIANT \
    .Key = u8"variant", .Type = PropertyType_String, .Default.StringValue = NULL, \
    .Rules = PROPERTY_RULES_STRING(WORLD_MODEL_VARIANT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_MODEL, .DisplayName = u8"Variant", \
    .Description = u8"The variant drawn while \"Use a variant\" is on; an unknown name draws the model's own " \
        u8"materials."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAS_OUTLINE \
    .Key = u8"overrides_has_outline", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, \
    .DisplayName = u8"Override outline", .Description = u8"Uses this object's outline setting instead of its world " \
        u8"model's."

#define WORLD_MODEL_PLACEMENT_ROW_HAS_OUTLINE \
    .Key = u8"has_outline", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, .DisplayName = u8"Outline", \
    .Description = u8"Draws a 1-pixel outline around the model (while \"Override outline\" is on)."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_OMIT_PIXELATION \
    .Key = u8"overrides_omit_pixelation", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, \
    .DisplayName = u8"Override crisp", .Description = u8"Uses this object's crisp setting instead of its world model's."

#define WORLD_MODEL_PLACEMENT_ROW_OMIT_PIXELATION \
    .Key = u8"omit_pixelation", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, .DisplayName = u8"Crisp", \
    .Description = u8"Draws the model at full resolution over the pixelated image, for readable screens (while " \
        u8"\"Override crisp\" is on)."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_TIER \
    .Key = u8"overrides_shadow_tier", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, \
    .DisplayName = u8"Override shadow range", .Description = u8"Uses this object's shadow range instead of its world " \
        u8"model's."

#define WORLD_MODEL_PLACEMENT_ROW_SHADOW_TIER \
    .Key = u8"shadow_tier", .Type = PropertyType_Enum, .Default.EnumValue = WorldShadowTier_Both, \
    .EnumTable = &WORLD_SHADOW_TIER_ENUM, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, .DisplayName = u8"Shadow range", \
    .Description = u8"Which shadows the model casts: \"near\" only up close (small props), \"far\" only the coarse " \
        u8"long-range one (big objects), \"both\" everywhere (while \"Override shadow range\" is on)."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_CASTS_SHADOWS \
    .Key = u8"overrides_casts_shadows", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, \
    .DisplayName = u8"Override casts shadows", .Description = u8"Uses this object's \"Casts shadows\" instead of its " \
        u8"world model's."

#define WORLD_MODEL_PLACEMENT_ROW_CASTS_SHADOWS \
    .Key = u8"casts_shadows", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, .DisplayName = u8"Casts shadows", \
    .Description = u8"When off, the model casts no shadow; it still receives them (while \"Override casts " \
        u8"shadows\" is on)."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_DRAW_DISTANCE \
    .Key = u8"overrides_draw_distance", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_DRAW_DISTANCE, \
    .DisplayName = u8"Override draw distance", .Description = u8"Uses this object's draw distance instead of its " \
        u8"world model's."

#define WORLD_MODEL_PLACEMENT_ROW_DRAW_DISTANCE \
    .Key = u8"draw_distance", .Type = PropertyType_Float, .Default.FloatValue = WORLD_MODEL_DEFAULT_DRAW_DISTANCE, \
    .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_DRAW_DISTANCE, .DisplayName = u8"Draw distance", \
    .Description = u8"How far the object is seen, in metres, before the quality setting and the zoom (while " \
        u8"\"Override draw distance\" is on); beyond it the object fades out."

#define WORLD_MODEL_PLACEMENT_ROW_DRAW_DISTANCE_MULTIPLIER \
    .Key = u8"draw_distance_multiplier", .Type = PropertyType_Float, .Default.FloatValue = 1.0f, \
    .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_DRAW_DISTANCE, .DisplayName = u8"Draw distance multiplier", \
    .Description = u8"Multiplies the draw distance in effect, the world model's or this object's: 2 is seen twice as " \
        u8"far."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_DRAW_DISTANCE_FIXED \
    .Key = u8"overrides_is_draw_distance_fixed", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_DRAW_DISTANCE, \
    .DisplayName = u8"Override fixed draw distance", \
    .Description = u8"Uses this object's \"Fixed draw distance\" instead of its world model's."

#define WORLD_MODEL_PLACEMENT_ROW_IS_DRAW_DISTANCE_FIXED \
    .Key = u8"is_draw_distance_fixed", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_DRAW_DISTANCE, \
    .DisplayName = u8"Fixed draw distance", \
    .Description = u8"Keeps the draw distance as it is when the player lowers or raises the draw distance quality " \
        u8"setting: for landmarks and gameplay-critical things (while \"Override fixed draw distance\" is on)."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_LOD_DISTANCES \
    .Key = u8"overrides_lod_distances", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_LODS, \
    .DisplayName = u8"Override LOD distances", \
    .Description = u8"Uses this object's LOD distances instead of its world model's (while it gives one per LOD)."

#define WORLD_MODEL_PLACEMENT_ROW_LOD_DISTANCES \
    .Key = u8"lod_distances", .Type = PropertyType_FloatList, .Default.FloatListValue = NULL, \
    .Rules = PROPERTY_RULES_INCREASING_FLOAT_LIST_ABOVE(WORLD_MODEL_MAX_LODS, 0.0), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_LODS, .DisplayName = u8"LOD distances", \
    .Description = u8"Up to which distance, in metres, each LOD is drawn, the finest first and rising, before the " \
        u8"quality setting and the zoom (while \"Override LOD distances\" is on, and only with one distance per LOD " \
        u8"of the world model)."

#define WORLD_MODEL_PLACEMENT_ROW_LOD_DISTANCE_MULTIPLIER \
    .Key = u8"lod_distance_multiplier", .Type = PropertyType_Float, .Default.FloatValue = 1.0f, \
    .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_LODS, \
    .DisplayName = u8"LOD distance multiplier", \
    .Description = u8"Multiplies the LOD distances in effect, the world model's or this object's: 2 keeps each LOD " \
        u8"twice as far (more detail)."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAZE \
    .Key = u8"overrides_haze", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, .DisplayName = u8"Override haze", \
    .Description = u8"Uses this object's haze instead of its world model's."

#define WORLD_MODEL_PLACEMENT_ROW_HAZE \
    .Key = u8"haze", .Type = PropertyType_Float, .Default.FloatValue = 1.0f, \
    .Rules = PROPERTY_RULES_BETWEEN(0.0, WORLD_MODEL_MAX_HAZE), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, .DisplayName = u8"Haze", \
    .Description = u8"How hazy the object looks at a distance: it multiplies the haze in front of it, 1 the real air's, " \
        u8"0 as if the air were clear (a beacon, a sign that must read from far away; while \"Override haze\" is on)."

#define WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_OCCLUDES_SKY \
    .Key = u8"overrides_occludes_sky", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, \
    .DisplayName = u8"Override keeps the sky out", \
    .Description = u8"Uses this object's \"Keeps the sky out\" instead of its world model's."

#define WORLD_MODEL_PLACEMENT_ROW_OCCLUDES_SKY \
    .Key = u8"occludes_sky", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_RENDERING, .DisplayName = u8"Keeps the sky out", \
    .Description = u8"Whether the object keeps the sky's light and the rain off what lies under it (a roof, a wall, a " \
        u8"tree's canopy); off for grass, small plants, people and poles (while \"Override keeps the sky out\" is on)."

#define WORLD_MODEL_PLACEMENT_ROW_IS_LOD_FORCED \
    .Key = u8"is_lod_forced", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_LODS, .DisplayName = u8"Force a LOD", \
    .Description = u8"Draws the \"Forced LOD\" whatever the distance, instead of choosing a LOD by distance."

#define WORLD_MODEL_PLACEMENT_ROW_FORCED_LOD \
    .Key = u8"forced_lod", .Type = PropertyType_UInt32, .Default.UInt32Value = 0U, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_MODEL_PLACEMENT_CATEGORY_LODS, .DisplayName = u8"Forced LOD", \
    .Description = u8"The LOD drawn while \"Force a LOD\" is on: 0 the finest; past the world model's last LOD, its " \
        u8"last."
