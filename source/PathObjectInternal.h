#pragma once
#include "PathObject.h"
#include "PropertyDescriptor.h"

/*
 * Private header (source/): the rows of a path object's settings, shared by the asset's table (PathObject.c: a definition's
 * keys) and the path object component's (WorldPathObjectComponent.c: a placement's), so a setting has its key, wire id,
 * default, rules and texts once. Both structs hold the settings in a member named _settings.
 *
 * Wire ids 1 to 63 are these rows' in both tables; the next free one is 56, and a table's own rows take ids from 64 up.
 */


// Macros: the inspector's categories.
#define PATH_OBJECT_CATEGORY_SLOTS ((const unsigned char*)u8"Slots")
#define PATH_OBJECT_CATEGORY_FIT ((const unsigned char*)u8"Fit")
#define PATH_OBJECT_CATEGORY_GROUND ((const unsigned char*)u8"Ground")


// Macros: the two rows of one setting.
/** The flag and the value of a setting of the settings struct of @p owner. The rest of the arguments are the value row's type,
 *  default and rules. */
#define PATH_OBJECT_PAIR_ROWS(owner, wire, key, member, category, display, description, ...) \
    { \
        .Key = u8"overrides_" key, .WireId = (wire), .Type = PropertyType_Bool, \
        PROPERTY_MEMBER(owner, _settings.Overrides##member), .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
        .Category = category, .DisplayName = u8"Override " display, \
        .Description = u8"Uses this setting's own value instead of what lies under it (the asset's, or the default).", \
        .SinceVersion = 1U \
    }, \
    { \
        .Key = u8"" key, .WireId = (wire) + 1U, PROPERTY_MEMBER(owner, _settings.member), .Flags = PROPERTY_FLAGS_SETTING, \
        .Category = category, .DisplayName = u8"" display, .Description = u8"" description, .SinceVersion = 1U, __VA_ARGS__ \
    }


// Macros: the rows, in the inspector's order.
#define PATH_OBJECT_SETTINGS_ROWS(owner) \
    { \
        .Key = u8"overrides_models", .WireId = 1U, .Type = PropertyType_Bool, \
        PROPERTY_MEMBER(owner, _settings.OverridesModels), .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
        .Category = PATH_OBJECT_CATEGORY_SLOTS, .DisplayName = u8"Override models", \
        .Description = u8"Uses the models below for the slots they name; the other slots keep the asset's.", \
        .SinceVersion = 1U \
    }, \
    { \
        .Key = u8"models", .WireId = 2U, .Type = PropertyType_RecordList, PROPERTY_MEMBER(owner, _settings.Models), \
        .Default.RecordListValue = NULL, .RecordTable = &PathObjectInternal_ModelTable, \
        .Rules = PROPERTY_RULES_RECORD_LIST(PATH_OBJECT_MAX_MODELS), .Flags = PROPERTY_FLAGS_SETTING, \
        .Category = PATH_OBJECT_CATEGORY_SLOTS, .DisplayName = u8"Models", \
        .Description = u8"The weighted world models, each naming the slot it belongs to.", .SinceVersion = 1U \
    }, \
    { \
        .Key = u8"slots", .WireId = 3U, .Type = PropertyType_RecordList, PROPERTY_MEMBER(owner, _settings.Slots), \
        .Default.RecordListValue = NULL, .RecordTable = &PathObjectInternal_SlotTable, \
        .Rules = PROPERTY_RULES_RECORD_LIST(PATH_OBJECT_MAX_SLOT_RECORDS), .Flags = PROPERTY_FLAGS_SETTING, \
        .Category = PATH_OBJECT_CATEGORY_SLOTS, .DisplayName = u8"Slots", \
        .Description = u8"The slots' settings, a record per slot that has any.", .SinceVersion = 1U \
    }, \
    PATH_OBJECT_PAIR_ROWS(owner, 4U, "has_start_cap", HasStartCap, PATH_OBJECT_CATEGORY_SLOTS, "Start cap", \
        "Whether a run starts with the start cap slot's piece.", .Type = PropertyType_Bool, .Default.BoolValue = false), \
    PATH_OBJECT_PAIR_ROWS(owner, 6U, "has_end_cap", HasEndCap, PATH_OBJECT_CATEGORY_SLOTS, "End cap", \
        "Whether a run ends with the end cap slot's piece.", .Type = PropertyType_Bool, .Default.BoolValue = false), \
    PATH_OBJECT_PAIR_ROWS(owner, 8U, "has_separator", HasSeparator, PATH_OBJECT_CATEGORY_SLOTS, "Separator", \
        "Whether a separator stands between middles and where two sections of a run meet.", .Type = PropertyType_Bool, \
        .Default.BoolValue = false), \
    PATH_OBJECT_PAIR_ROWS(owner, 10U, "has_single", HasSingle, PATH_OBJECT_CATEGORY_SLOTS, "Single", \
        "Whether the single slot's piece replaces a whole span, caps too, where exactly one middle fits.", \
        .Type = PropertyType_Bool, .Default.BoolValue = false), \
    PATH_OBJECT_PAIR_ROWS(owner, 12U, "has_corner", HasCorner, PATH_OBJECT_CATEGORY_SLOTS, "Corner", \
        "Whether the corner slot's piece replaces the separator at corner nodes.", .Type = PropertyType_Bool, \
        .Default.BoolValue = false), \
    PATH_OBJECT_PAIR_ROWS(owner, 14U, "fit", Fit, PATH_OBJECT_CATEGORY_FIT, "Fit", \
        "How a span is filled: \"stretch\" scales a whole number of middles to fill it, \"resize_ends\" keeps whole pieces " \
        "and resizes the end pieces, \"fixed\" keeps whole pieces and leaves the rest to the alignment.", \
        .Type = PropertyType_Enum, .Default.EnumValue = PathObjectFit_Stretch, .EnumTable = &PathObjectInternal_FitEnum), \
    PATH_OBJECT_PAIR_ROWS(owner, 16U, "rounding", Rounding, PATH_OBJECT_CATEGORY_FIT, "Rounding", \
        "Which way a count of middles rounds for \"stretch\" and \"resize_ends\": \"nearest\" stretches least, \"up\" makes " \
        "pieces shrink, \"down\" makes them grow.", .Type = PropertyType_Enum, .Default.EnumValue = PathObjectRounding_Nearest, \
        .EnumTable = &PathObjectInternal_RoundingEnum), \
    PATH_OBJECT_PAIR_ROWS(owner, 18U, "resized_ends", ResizedEnds, PATH_OBJECT_CATEGORY_FIT, "Resized ends", \
        "Which end pieces \"resize_ends\" resizes: the first, the last or both alike.", .Type = PropertyType_Enum, \
        .Default.EnumValue = PathObjectResizedEnds_Both, .EnumTable = &PathObjectInternal_ResizedEndsEnum), \
    PATH_OBJECT_PAIR_ROWS(owner, 20U, "resize_method", ResizeMethod, PATH_OBJECT_CATEGORY_FIT, "Resize method", \
        "How \"resize_ends\" resizes them: \"scale\" along their length, or \"cut\" the mesh off, the cut hidden in the cap " \
        "(the count then rounds up).", .Type = PropertyType_Enum, .Default.EnumValue = PathObjectResizeMethod_Scale, \
        .EnumTable = &PathObjectInternal_ResizeMethodEnum), \
    PATH_OBJECT_PAIR_ROWS(owner, 22U, "align", Align, PATH_OBJECT_CATEGORY_FIT, "Align", \
        "Where the \"fixed\" fit's leftover goes: \"start\" a gap at the end, \"end\" a gap at the start, \"center\" equal " \
        "gaps at both ends, \"between\" equal gaps between pieces, \"around\" half a gap at each end, \"evenly\" the same gap " \
        "everywhere.", .Type = PropertyType_Enum, .Default.EnumValue = PathObjectAlign_Start, \
        .EnumTable = &PathObjectInternal_AlignEnum), \
    PATH_OBJECT_PAIR_ROWS(owner, 24U, "uses_count", UsesCount, PATH_OBJECT_CATEGORY_FIT, "Use a count", \
        "Whether a span holds exactly the count of middles below, as far as they fit, instead of as many as the fit finds.", \
        .Type = PropertyType_Bool, .Default.BoolValue = false), \
    PATH_OBJECT_PAIR_ROWS(owner, 26U, "count", Count, PATH_OBJECT_CATEGORY_FIT, "Count", \
        "How many middles a span holds (while \"Use a count\" is on).", .Type = PropertyType_UInt32, \
        .Default.UInt32Value = 1U, .Rules = PROPERTY_RULES_BETWEEN(1.0, (double)PATH_OBJECT_MAX_COUNT)), \
    PATH_OBJECT_PAIR_ROWS(owner, 28U, "uses_gap", UsesGap, PATH_OBJECT_CATEGORY_FIT, "Use a gap", \
        "Whether the \"fixed\" fit keeps the gap below between its pieces (a lamp every 25 m), with as many pieces as that " \
        "leaves room for; \"between\", \"around\" and \"evenly\" then share the leftover on top of it.", \
        .Type = PropertyType_Bool, .Default.BoolValue = false), \
    PATH_OBJECT_PAIR_ROWS(owner, 30U, "gap", Gap, PATH_OBJECT_CATEGORY_FIT, "Gap", \
        "The free space between one middle and the next, in metres, a separator's length not counted (while \"Use a gap\" is " \
        "on).", .Type = PropertyType_Float, .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_AT_LEAST(0.0)), \
    PATH_OBJECT_PAIR_ROWS(owner, 32U, "when_too_short", WhenTooShort, PATH_OBJECT_CATEGORY_FIT, "When too short", \
        "What a span does where one middle does not fit: \"squeeze\" it down to the minimum stretch, the \"single\" slot, " \
        "\"caps_only\" or \"nothing\".", .Type = PropertyType_Enum, .Default.EnumValue = PathObjectTooShort_Squeeze, \
        .EnumTable = &PathObjectInternal_TooShortEnum), \
    PATH_OBJECT_PAIR_ROWS(owner, 34U, "minimum_stretch", MinimumStretch, PATH_OBJECT_CATEGORY_FIT, "Minimum stretch", \
        "The smallest stretch a squeezed middle takes; shorter than that, the span shows only its caps.", \
        .Type = PropertyType_Float, .Default.FloatValue = PATH_OBJECT_DEFAULT_MINIMUM_STRETCH, \
        .Rules = { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true, .Maximum = 1.0 }), \
    PATH_OBJECT_PAIR_ROWS(owner, 36U, "caps_at", CapsAt, PATH_OBJECT_CATEGORY_FIT, "Caps at", \
        "Where the caps sit when a \"fixed\" run leaves a gap: at the ends of the \"path\" or next to the pieces of the " \
        "\"run\".", .Type = PropertyType_Enum, .Default.EnumValue = PathObjectCapsAt_Path, \
        .EnumTable = &PathObjectInternal_CapsAtEnum), \
    PATH_OBJECT_PAIR_ROWS(owner, 38U, "separator_every", SeparatorEvery, PATH_OBJECT_CATEGORY_FIT, "Separator every", \
        "A separator after every this many middles.", .Type = PropertyType_UInt32, .Default.UInt32Value = 1U, \
        .Rules = PROPERTY_RULES_BETWEEN(1.0, (double)PATH_OBJECT_MAX_COUNT)), \
    PATH_OBJECT_PAIR_ROWS(owner, 40U, "fits_each_section", FitsEachSection, PATH_OBJECT_CATEGORY_FIT, "Fit each section", \
        "Whether each section of a run fits on its own, so a corner never falls in the middle of a piece; off, the whole " \
        "run fits as one.", .Type = PropertyType_Bool, .Default.BoolValue = true), \
    PATH_OBJECT_PAIR_ROWS(owner, 42U, "seed", Seed, PATH_OBJECT_CATEGORY_FIT, "Seed", \
        "The seed of every random choice: the models, the flips, the jitter.", .Type = PropertyType_UInt32, \
        .Default.UInt32Value = 0U), \
    PATH_OBJECT_PAIR_ROWS(owner, 44U, "snaps_to_terrain", SnapsToTerrain, PATH_OBJECT_CATEGORY_GROUND, "Snaps to terrain", \
        "Whether its points snap to the ground under them; a section's node may say otherwise for its section (a bridge).", \
        .Type = PropertyType_Bool, .Default.BoolValue = true), \
    PATH_OBJECT_PAIR_ROWS(owner, 46U, "ground_offset", GroundOffset, PATH_OBJECT_CATEGORY_GROUND, "Ground offset", \
        "How far above the ground its points lie, in metres; negative sinks them.", .Type = PropertyType_Float, \
        .Default.FloatValue = 0.0f), \
    PATH_OBJECT_PAIR_ROWS(owner, 48U, "ground_search_height", GroundSearchHeight, PATH_OBJECT_CATEGORY_GROUND, \
        "Ground search height", "How far above a point the search for the ground starts, in metres: low enough for a tunnel " \
        "under a hill to find its floor.", .Type = PropertyType_Float, .Default.FloatValue = PATH_GROUND_DEFAULT_SEARCH_HEIGHT, \
        .Rules = PROPERTY_RULES_AT_LEAST(0.0)), \
    PATH_OBJECT_PAIR_ROWS(owner, 50U, "ground_search_depth", GroundSearchDepth, PATH_OBJECT_CATEGORY_GROUND, \
        "Ground search depth", "How far below a point the search goes, in metres.", .Type = PropertyType_Float, \
        .Default.FloatValue = PATH_GROUND_DEFAULT_SEARCH_DEPTH, .Rules = PROPERTY_RULES_AT_LEAST(0.0)), \
    PATH_OBJECT_PAIR_ROWS(owner, 52U, "ground_target", GroundTarget, PATH_OBJECT_CATEGORY_GROUND, "Ground target", \
        "What counts as the ground: the \"terrain\" alone, or anything solid on the \"layers\" below (a railing on a bridge " \
        "deck).", .Type = PropertyType_Enum, .Default.EnumValue = PathGroundTarget_Terrain, \
        .EnumTable = &PathObjectInternal_GroundTargetEnum), \
    PATH_OBJECT_PAIR_ROWS(owner, 54U, "ground_layers", GroundLayers, PATH_OBJECT_CATEGORY_GROUND, "Ground layers", \
        "The collision layers that count as the ground for the \"layers\" target.", .Type = PropertyType_EnumSet, \
        .Default.EnumSetValue = COLLISION_LAYER_MASK_ALL, .EnumTable = &COLLISION_LAYER_ENUM)


// Fields: what the rows name, defined in PathObject.c.
extern const PropertyTable PathObjectInternal_ModelTable;
extern const PropertyTable PathObjectInternal_SlotTable;
extern const PropertyEnumTable PathObjectInternal_FitEnum;
extern const PropertyEnumTable PathObjectInternal_RoundingEnum;
extern const PropertyEnumTable PathObjectInternal_ResizedEndsEnum;
extern const PropertyEnumTable PathObjectInternal_ResizeMethodEnum;
extern const PropertyEnumTable PathObjectInternal_AlignEnum;
extern const PropertyEnumTable PathObjectInternal_TooShortEnum;
extern const PropertyEnumTable PathObjectInternal_CapsAtEnum;
extern const PropertyEnumTable PathObjectInternal_GroundTargetEnum;
