#pragma once
#include "PropertyDescriptor.h"
#include "WorldSprite.h"

/*
 * Private header (source/): the property rows of a sprite's settings, shared by the world sprite's tables
 * (WorldSprite.c: the definition's keys) and the sprite component's table (WorldSpriteComponent.c: a placement's own
 * values behind their override flags). Each macro holds a row's key, type, default, rules, flags and texts, so a setting
 * has them once wherever it is stored; a table's row adds its wire id, its member (PROPERTY_MEMBER) and its
 * SinceVersion:
 *
 *     { WORLD_SPRITE_ROW_PIVOT, .WireId = 5U, PROPERTY_MEMBER(WorldSpriteComponent, _pivot), .SinceVersion = 1U }
 *
 * Both tables give a setting the same wire id.
 */


// Macros: the inspector's categories.
#define WORLD_SPRITE_CATEGORY_SPRITE ((const unsigned char*)u8"Sprite")
#define WORLD_SPRITE_CATEGORY_LIGHTING ((const unsigned char*)u8"Lighting")
#define WORLD_SPRITE_CATEGORY_DECAL ((const unsigned char*)u8"Decal")
#define WORLD_SPRITE_CATEGORY_DRAW_DISTANCE ((const unsigned char*)u8"Draw distance")


// Macros: the rows (in the inspector's order).
#define WORLD_SPRITE_ROW_ORIENTATION \
    .Key = u8"orientation", .Type = PropertyType_Enum, .Default.EnumValue = WorldSpriteOrientation_Billboard, \
    .EnumTable = &WORLD_SPRITE_ORIENTATION_ENUM, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"Orientation", \
    .Description = u8"\"billboard\" faces the camera; \"axis_billboard\" turns only around the object's own up axis to " \
        u8"face it (trees, lamp posts); \"fixed\" keeps the object's rotation, facing its +Z (signs, decals)."

#define WORLD_SPRITE_ROW_PIVOT \
    .Key = u8"pivot", .Type = PropertyType_Vector2, .Default.Vector2Value = { 0.5f, 0.5f }, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"Pivot", \
    .Description = u8"The point of the image at the object's position: [0, 0] its bottom left, [1, 1] its top right; " \
        u8"[0.5, 0] for things standing on the ground."

#define WORLD_SPRITE_ROW_PIXELS_PER_UNIT \
    .Key = u8"pixels_per_unit", .Type = PropertyType_Float, .Default.FloatValue = WORLD_SPRITE_DEFAULT_PIXELS_PER_UNIT, \
    .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_SPRITE_CATEGORY_SPRITE, \
    .DisplayName = u8"Pixels per metre", \
    .Description = u8"Image pixels per metre: the sprite's size is its frame's size divided by this, times the object's " \
        u8"X and Y scale."

#define WORLD_SPRITE_ROW_FLIP_X \
    .Key = u8"flip_x", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"Flip left-right", \
    .Description = u8"Mirrors the image left to right, about the pivot."

#define WORLD_SPRITE_ROW_FLIP_Y \
    .Key = u8"flip_y", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"Flip upside down", \
    .Description = u8"Mirrors the image top to bottom, about the pivot."

#define WORLD_SPRITE_ROW_ALPHA_MODE \
    .Key = u8"alpha_mode", .Type = PropertyType_Enum, .Default.EnumValue = WorldSpriteAlphaMode_Cutout, \
    .EnumTable = &WORLD_SPRITE_ALPHA_MODE_ENUM, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"Transparency", \
    .Description = u8"\"cutout\": hard edges, opaque where drawn (pixel art; casts shadows); \"blend\": soft edges " \
        u8"(smoke, stains); \"additive\": adds its light (glows)."

#define WORLD_SPRITE_ROW_ALPHA_CUTOFF \
    .Key = u8"alpha_cutoff", .Type = PropertyType_Float, .Default.FloatValue = WORLD_SPRITE_DEFAULT_ALPHA_CUTOFF, \
    .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"Cutout threshold", \
    .Description = u8"With \"cutout\" transparency, texels less opaque than this are not drawn."

#define WORLD_SPRITE_ROW_IS_LIT \
    .Key = u8"is_lit", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_LIGHTING, .DisplayName = u8"Lit", \
    .Description = u8"Lit by the sun, the moon and lamps like the models around it. Off: its colours glow as they are, " \
        u8"by day and night (neon, fire)."

#define WORLD_SPRITE_ROW_EMISSION \
    .Key = u8"emission", .Type = PropertyType_Float, .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_AT_LEAST(0.0), \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_SPRITE_CATEGORY_LIGHTING, .DisplayName = u8"Glow", \
    .Description = u8"How bright an unlit sprite glows; 1 shows its colours as they are."

#define WORLD_SPRITE_ROW_LIGHT_WRAP \
    .Key = u8"light_wrap", .Type = PropertyType_Float, .Default.FloatValue = 1.0f, \
    .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_LIGHTING, .DisplayName = u8"Light wrap", \
    .Description = u8"Billboards only: how far light wraps around the sprite. 0 lights it as a flat card facing you, 1 " \
        u8"as a round thing, half lit from the side."

#define WORLD_SPRITE_ROW_CASTS_SHADOWS \
    .Key = u8"casts_shadows", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_LIGHTING, .DisplayName = u8"Casts shadows", \
    .Description = u8"Cutout sprites only: casts a shadow (a billboard's shadow faces the light, so it does not turn " \
        u8"with the camera). Decals and soft sprites never cast."

#define WORLD_SPRITE_ROW_IS_DECAL \
    .Key = u8"is_decal", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_DECAL, .DisplayName = u8"Decal", \
    .Description = u8"With the \"fixed\" orientation: lies on a surface (dirt, tire marks), drawn over it without " \
        u8"shadow or outline."

#define WORLD_SPRITE_ROW_DECAL_OFFSET \
    .Key = u8"decal_offset", .Type = PropertyType_Float, .Default.FloatValue = WORLD_SPRITE_DEFAULT_DECAL_OFFSET, \
    .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_SPRITE_CATEGORY_DECAL, \
    .DisplayName = u8"Lift", .Description = u8"Metres a decal is lifted off its surface so it does not flicker against it."

#define WORLD_SPRITE_ROW_HAS_OUTLINE \
    .Key = u8"has_outline", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"Outline", \
    .Description = u8"Draws a 1-pixel outline around the sprite's shape (cutout sprites)."

#define WORLD_SPRITE_ROW_OMIT_PIXELATION \
    .Key = u8"omit_pixelation", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"Crisp", \
    .Description = u8"Draws the sprite at full resolution over the pixelated image (cutout sprites)."

#define WORLD_SPRITE_ROW_DRAW_DISTANCE \
    .Key = u8"draw_distance", .Type = PropertyType_Float, .Default.FloatValue = WORLD_SPRITE_DEFAULT_DRAW_DISTANCE, \
    .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = WORLD_SPRITE_CATEGORY_DRAW_DISTANCE, .DisplayName = u8"Draw distance", \
    .Description = u8"How far the sprite is seen, in metres, before the quality setting and the zoom; beyond it the " \
        u8"sprite fades out."

#define WORLD_SPRITE_ROW_IS_DRAW_DISTANCE_FIXED \
    .Key = u8"is_draw_distance_fixed", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_SPRITE_CATEGORY_DRAW_DISTANCE, \
    .DisplayName = u8"Fixed draw distance", \
    .Description = u8"Keeps the draw distance as it is when the player lowers or raises the draw distance quality " \
        u8"setting: for landmarks and gameplay-critical signs."
