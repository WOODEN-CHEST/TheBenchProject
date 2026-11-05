// The path object's data: its enums, its tables and what is in effect. The definition and the loader are in
// PathObjectDefinition.c; the settings' rows are shared with the path object component's table (PathObjectInternal.h).

#include <math.h>
#include "PathObject.h"
#include "PathObjectInternal.h"
#include "WorldModelComponentInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format versions of the tables. */
#define MODEL_TABLE_VERSION 1U
#define SLOT_TABLE_VERSION 1U
#define PATH_OBJECT_TABLE_VERSION 1U
/** The inspector's categories of a slot's rows. */
#define CATEGORY_MODEL ((const unsigned char*)u8"Model")
#define CATEGORY_PIECE ((const unsigned char*)u8"Piece")
#define CATEGORY_CURVE ((const unsigned char*)u8"Curve and slope")
#define CATEGORY_SIDE ((const unsigned char*)u8"Side and jitter")
#define CATEGORY_COLLISION ((const unsigned char*)u8"Collision")
/** An enum table of the entries of an array. */
#define ENUM_TABLE(name, entries) { .Name = name, .Entries = entries, .EntryCount = sizeof(entries) / sizeof(entries[0]) }
/** The two rows of one slot setting: the flag and the value; the rest of the arguments are the value's type, default and
 *  rules. */
#define SLOT_PAIR_ROWS(wire, key, member, category, display, description, ...) \
    { \
        .Key = u8"overrides_" key, .WireId = (wire), .Type = PropertyType_Bool, \
        PROPERTY_MEMBER(PathObjectSlotRecord, Overrides##member), .Default.BoolValue = false, \
        .Flags = PROPERTY_FLAGS_SETTING, .Category = category, .DisplayName = u8"Override " display, \
        .Description = u8"Uses this setting's own value instead of what lies under it (the asset's, or the default).", \
        .SinceVersion = 1U \
    }, \
    { \
        .Key = u8"" key, .WireId = (wire) + 1U, PROPERTY_MEMBER(PathObjectSlotRecord, member), .Flags = PROPERTY_FLAGS_SETTING, \
        .Category = category, .DisplayName = u8"" display, .Description = u8"" description, .SinceVersion = 1U, __VA_ARGS__ \
    }
/** One of the rows a model instances component has, by its macro (WorldModelComponentInternal.h). */
#define SLOT_RENDER_ROW(row, wire, member) \
    { row, .WireId = (wire), PROPERTY_MEMBER(PathObjectSlotRecord, member), .SinceVersion = 1U }


// Fields: the enums.
static const PropertyEnumEntry SlotEntries[] =
{
    { .Name = u8"start_cap", .Value = PathObjectSlot_StartCap },
    { .Name = u8"middle", .Value = PathObjectSlot_Middle },
    { .Name = u8"separator", .Value = PathObjectSlot_Separator },
    { .Name = u8"end_cap", .Value = PathObjectSlot_EndCap },
    { .Name = u8"single", .Value = PathObjectSlot_Single },
    { .Name = u8"corner", .Value = PathObjectSlot_Corner },
};
static const PropertyEnumTable SlotEnum = ENUM_TABLE(u8"path_object_slot", SlotEntries);

static const PropertyEnumEntry FitEntries[] =
{
    { .Name = u8"stretch", .Value = PathObjectFit_Stretch },
    { .Name = u8"resize_ends", .Value = PathObjectFit_ResizeEnds },
    { .Name = u8"fixed", .Value = PathObjectFit_Fixed },
};
const PropertyEnumTable PathObjectInternal_FitEnum = ENUM_TABLE(u8"path_object_fit", FitEntries);

static const PropertyEnumEntry RoundingEntries[] =
{
    { .Name = u8"nearest", .Value = PathObjectRounding_Nearest },
    { .Name = u8"up", .Value = PathObjectRounding_Up },
    { .Name = u8"down", .Value = PathObjectRounding_Down },
};
const PropertyEnumTable PathObjectInternal_RoundingEnum = ENUM_TABLE(u8"path_object_rounding", RoundingEntries);

static const PropertyEnumEntry ResizedEndsEntries[] =
{
    { .Name = u8"start", .Value = PathObjectResizedEnds_Start },
    { .Name = u8"end", .Value = PathObjectResizedEnds_End },
    { .Name = u8"both", .Value = PathObjectResizedEnds_Both },
};
const PropertyEnumTable PathObjectInternal_ResizedEndsEnum = ENUM_TABLE(u8"path_object_resized_ends", ResizedEndsEntries);

static const PropertyEnumEntry ResizeMethodEntries[] =
{
    { .Name = u8"scale", .Value = PathObjectResizeMethod_Scale },
    { .Name = u8"cut", .Value = PathObjectResizeMethod_Cut },
};
const PropertyEnumTable PathObjectInternal_ResizeMethodEnum = ENUM_TABLE(u8"path_object_resize_method", ResizeMethodEntries);

static const PropertyEnumEntry AlignEntries[] =
{
    { .Name = u8"start", .Value = PathObjectAlign_Start },
    { .Name = u8"end", .Value = PathObjectAlign_End },
    { .Name = u8"center", .Value = PathObjectAlign_Center },
    { .Name = u8"between", .Value = PathObjectAlign_Between },
    { .Name = u8"around", .Value = PathObjectAlign_Around },
    { .Name = u8"evenly", .Value = PathObjectAlign_Evenly },
};
const PropertyEnumTable PathObjectInternal_AlignEnum = ENUM_TABLE(u8"path_object_align", AlignEntries);

static const PropertyEnumEntry TooShortEntries[] =
{
    { .Name = u8"squeeze", .Value = PathObjectTooShort_Squeeze },
    { .Name = u8"single", .Value = PathObjectTooShort_Single },
    { .Name = u8"caps_only", .Value = PathObjectTooShort_CapsOnly },
    { .Name = u8"nothing", .Value = PathObjectTooShort_Nothing },
};
const PropertyEnumTable PathObjectInternal_TooShortEnum = ENUM_TABLE(u8"path_object_too_short", TooShortEntries);

static const PropertyEnumEntry CapsAtEntries[] =
{
    { .Name = u8"path", .Value = PathObjectCapsAt_Path },
    { .Name = u8"run", .Value = PathObjectCapsAt_Run },
};
const PropertyEnumTable PathObjectInternal_CapsAtEnum = ENUM_TABLE(u8"path_object_caps_at", CapsAtEntries);

static const PropertyEnumEntry GroundTargetEntries[] =
{
    { .Name = u8"terrain", .Value = PathGroundTarget_Terrain },
    { .Name = u8"layers", .Value = PathGroundTarget_Layers },
};
const PropertyEnumTable PathObjectInternal_GroundTargetEnum = ENUM_TABLE(u8"path_ground_target", GroundTargetEntries);

static const PropertyEnumEntry PickEntries[] =
{
    { .Name = u8"random", .Value = PathObjectPick_Random },
    { .Name = u8"in_order", .Value = PathObjectPick_InOrder },
};
static const PropertyEnumTable PickEnum = ENUM_TABLE(u8"path_object_pick", PickEntries);

static const PropertyEnumEntry FlipEntries[] =
{
    { .Name = u8"none", .Value = PathObjectFlip_None },
    { .Name = u8"always", .Value = PathObjectFlip_Always },
    { .Name = u8"alternating", .Value = PathObjectFlip_Alternating },
    { .Name = u8"random", .Value = PathObjectFlip_Random },
};
static const PropertyEnumTable FlipEnum = ENUM_TABLE(u8"path_object_flip", FlipEntries);

static const PropertyEnumEntry CurveEntries[] =
{
    { .Name = u8"rigid", .Value = PathObjectCurve_Rigid },
    { .Name = u8"bent", .Value = PathObjectCurve_Bent },
};
static const PropertyEnumTable CurveEnum = ENUM_TABLE(u8"path_object_curve", CurveEntries);

static const PropertyEnumEntry SlopeEntries[] =
{
    { .Name = u8"shear", .Value = PathObjectSlope_Shear },
    { .Name = u8"tilt", .Value = PathObjectSlope_Tilt },
    { .Name = u8"step", .Value = PathObjectSlope_Step },
};
static const PropertyEnumTable SlopeEnum = ENUM_TABLE(u8"path_object_slope", SlopeEntries);

static const PropertyEnumEntry StepAnchorEntries[] =
{
    { .Name = u8"middle", .Value = PathObjectStepAnchor_Middle },
    { .Name = u8"higher", .Value = PathObjectStepAnchor_Higher },
    { .Name = u8"lower", .Value = PathObjectStepAnchor_Lower },
};
static const PropertyEnumTable StepAnchorEnum = ENUM_TABLE(u8"path_object_step_anchor", StepAnchorEntries);

static const PropertyEnumEntry SideEntries[] =
{
    { .Name = u8"on_path", .Value = PathObjectSide_OnPath },
    { .Name = u8"left", .Value = PathObjectSide_Left },
    { .Name = u8"right", .Value = PathObjectSide_Right },
    { .Name = u8"both", .Value = PathObjectSide_Both },
    { .Name = u8"alternating", .Value = PathObjectSide_Alternating },
};
static const PropertyEnumTable SideEnum = ENUM_TABLE(u8"path_object_side", SideEntries);

static const PropertyEnumEntry AlignedToEntries[] =
{
    { .Name = u8"path", .Value = PathObjectAlignedTo_Path },
    { .Name = u8"world", .Value = PathObjectAlignedTo_World },
};
static const PropertyEnumTable AlignedToEnum = ENUM_TABLE(u8"path_object_aligned_to", AlignedToEntries);

static const PropertyEnumEntry CollisionEntries[] =
{
    { .Name = u8"none", .Value = PathObjectCollision_None },
    { .Name = u8"model", .Value = PathObjectCollision_Model },
    { .Name = u8"mesh", .Value = PathObjectCollision_Mesh },
    { .Name = u8"boxes", .Value = PathObjectCollision_Boxes },
};
static const PropertyEnumTable CollisionEnum = ENUM_TABLE(u8"path_object_collision", CollisionEntries);


// Fields: the tables.
/* One weighted model, in the inspector's order. The wire ids are the save schema: never renumber or reuse one. The next free
 * wire id is 7. */
static const PropertyDescriptor ModelProperties[] =
{
    {
        .Key = u8"slot", .WireId = 1U, .Type = PropertyType_Enum, PROPERTY_MEMBER(PathObjectModelRecord, Slot),
        .Default.EnumValue = PathObjectSlot_Middle, .EnumTable = &SlotEnum, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MODEL, .DisplayName = u8"Slot", .Description = u8"The slot this model belongs to.",
        .SinceVersion = 1U
    },
    { WORLD_MODEL_PLACEMENT_ROW_WORLD_MODEL, .WireId = 2U, PROPERTY_MEMBER(PathObjectModelRecord, WorldModel),
        .SinceVersion = 1U },
    {
        .Key = u8"weight", .WireId = 3U, .Type = PropertyType_Float, PROPERTY_MEMBER(PathObjectModelRecord, Weight),
        .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MODEL, .DisplayName = u8"Weight",
        .Description = u8"How often it is picked against the slot's other models.", .SinceVersion = 1U
    },
    { WORLD_MODEL_PLACEMENT_ROW_USES_VARIANT, .WireId = 4U, PROPERTY_MEMBER(PathObjectModelRecord, UsesVariant),
        .SinceVersion = 1U },
    { WORLD_MODEL_PLACEMENT_ROW_VARIANT, .WireId = 5U, PROPERTY_MEMBER(PathObjectModelRecord, Variant), .SinceVersion = 1U },
    {
        .Key = u8"tint", .WireId = 6U, .Type = PropertyType_Color, PROPERTY_MEMBER(PathObjectModelRecord, Tint),
        .Default.ColorValue = { 255, 255, 255, 255 }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MODEL,
        .DisplayName = u8"Tint", .Description = u8"A colour multiplied into its copies' own; its alpha is an opacity.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"prefab", .WireId = 7U, .Type = PropertyType_AssetName, PROPERTY_MEMBER(PathObjectModelRecord, Prefab),
        .Rules = PROPERTY_RULES_STRING(PATH_OBJECT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MODEL, .DisplayName = u8"Prefab",
        .Description = u8"A prefab its pieces are copies of, in place of a world model: a lamp with its light. Always rigid, each piece "
            u8"an object of its own.",
        .SinceVersion = 1U
    },
    {
        .Key = u8"values", .WireId = 8U, .Type = PropertyType_DataValue, PROPERTY_MEMBER(PathObjectModelRecord, Values),
        .Rules = PROPERTY_RULES_DATA_VALUE(PATH_OBJECT_MODEL_VALUES_MAX_BYTES, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_MODEL, .DisplayName = u8"Prefab values",
        .Description = u8"The values its prefab's copies take, by property.", .SinceVersion = 1U
    },
    {
        .Key = u8"randomizes_enums", .WireId = 9U, .Type = PropertyType_Bool, PROPERTY_MEMBER(PathObjectModelRecord, RandomizesEnums),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_MODEL,
        .DisplayName = u8"Random variants",
        .Description = u8"Every enum property its values leave out is picked per copy from the seed.", .SinceVersion = 1U
    },
};

const PropertyTable PathObjectInternal_ModelTable =
{
    .Name = u8"path_object_model", .Version = MODEL_TABLE_VERSION, .StructSize = sizeof(PathObjectModelRecord),
    .Descriptors = ModelProperties, .DescriptorCount = sizeof(ModelProperties) / sizeof(ModelProperties[0])
};

/* One slot, in the inspector's order: wire ids 1 to 49, 78 and 79 its own rows, 50 to 77 and 80 to 82 the rows a model
 * instances component has. The wire ids are the save schema: never renumber or reuse one. The next free wire id is 83. */
static const PropertyDescriptor SlotProperties[] =
{
    {
        .Key = u8"slot", .WireId = 1U, .Type = PropertyType_Enum, PROPERTY_MEMBER(PathObjectSlotRecord, Slot),
        .Default.EnumValue = PathObjectSlot_Middle, .EnumTable = &SlotEnum, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_PIECE, .DisplayName = u8"Slot", .Description = u8"The slot these settings are for.",
        .SinceVersion = 1U
    },
    SLOT_PAIR_ROWS(2U, "pick", Pick, CATEGORY_MODEL, "Pick", "How a piece chooses among the slot's models: \"random\" by "
        "weight, from the seed, or \"in_order\", over and over.", .Type = PropertyType_Enum,
        .Default.EnumValue = PathObjectPick_Random, .EnumTable = &PickEnum),
    SLOT_PAIR_ROWS(4U, "length", Length, CATEGORY_PIECE, "Length", "The piece's length along the path, in metres, instead of "
        "its mesh's measured size; 0 stands it on a point. Pieces lie end to end by their lengths.",
        .Type = PropertyType_Float, .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_AT_LEAST(0.0)),
    SLOT_PAIR_ROWS(6U, "entry_depth_start", EntryDepthStart, CATEGORY_PIECE, "Entry depth at the start", "How far the mesh "
        "reaches past its length into the piece before it, in metres.", .Type = PropertyType_Float,
        .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_AT_LEAST(0.0)),
    SLOT_PAIR_ROWS(8U, "entry_depth_end", EntryDepthEnd, CATEGORY_PIECE, "Entry depth at the end", "How far the mesh reaches "
        "past its length into the piece after it, in metres.", .Type = PropertyType_Float, .Default.FloatValue = 0.0f,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0)),
    SLOT_PAIR_ROWS(10U, "stretches", Stretches, CATEGORY_PIECE, "Stretches", "Whether the \"stretch\" fit scales it; by "
        "default the middle and the single slot yes, the others no.", .Type = PropertyType_Bool, .Default.BoolValue = false),
    SLOT_PAIR_ROWS(12U, "position_offset", PositionOffset, CATEGORY_PIECE, "Position offset", "A position off its place, in "
        "metres: along the piece, up and to its right.", .Type = PropertyType_Vector3,
        .Default.Vector3Value = { 0.0f, 0.0f, 0.0f }),
    SLOT_PAIR_ROWS(14U, "rotation_offset", RotationOffset, CATEGORY_PIECE, "Rotation offset", "A rotation on top of its frame, "
        "in degrees (Euler XYZ).", .Type = PropertyType_AngleVector3, .Default.Vector3Value = { 0.0f, 0.0f, 0.0f }),
    SLOT_PAIR_ROWS(16U, "flip", Flip, CATEGORY_PIECE, "Flip", "Whether a piece is turned end for end: \"none\", \"always\", "
        "\"alternating\" or \"random\".", .Type = PropertyType_Enum, .Default.EnumValue = PathObjectFlip_None,
        .EnumTable = &FlipEnum),
    SLOT_PAIR_ROWS(18U, "curve", Curve, CATEGORY_CURVE, "Curve", "How a piece follows the curve: \"rigid\" stays straight with "
        "its ends on the curve, \"bent\" follows it (the game slices it along its length).", .Type = PropertyType_Enum,
        .Default.EnumValue = PathObjectCurve_Rigid, .EnumTable = &CurveEnum),
    SLOT_PAIR_ROWS(20U, "slice_spacing", SliceSpacing, CATEGORY_CURVE, "Slice spacing", "How far apart a bent piece's slices "
        "lie at its finest LOD, in metres, closer where the path bends; each coarser LOD doubles it.",
        .Type = PropertyType_Float, .Default.FloatValue = PATH_OBJECT_DEFAULT_SLICE_SPACING,
        .Rules = PROPERTY_RULES_ABOVE(0.0)),
    SLOT_PAIR_ROWS(22U, "slope", Slope, CATEGORY_CURVE, "Slope", "How a piece takes a slope: \"shear\" keeps its verticals "
        "vertical, \"tilt\" leans it, \"step\" keeps it level; by default the middle shears and the others step.",
        .Type = PropertyType_Enum, .Default.EnumValue = PathObjectSlope_Step, .EnumTable = &SlopeEnum),
    SLOT_PAIR_ROWS(24U, "step_anchor", StepAnchor, CATEGORY_CURVE, "Step anchor", "Which height a level piece takes: half way "
        "between its ends, its \"higher\" end's (it floats over the lower end) or its \"lower\" end's (it sinks at the higher "
        "end).", .Type = PropertyType_Enum, .Default.EnumValue = PathObjectStepAnchor_Middle, .EnumTable = &StepAnchorEnum),
    SLOT_PAIR_ROWS(26U, "side", Side, CATEGORY_SIDE, "Side", "Which side of the path it stands on: \"on_path\", \"left\", "
        "\"right\", \"both\" or \"alternating\". A left piece is its right twin turned end for end.",
        .Type = PropertyType_Enum, .Default.EnumValue = PathObjectSide_OnPath, .EnumTable = &SideEnum),
    SLOT_PAIR_ROWS(28U, "side_offset", SideOffset, CATEGORY_SIDE, "Side offset", "How far from the path a side piece stands, "
        "in metres, times the node's width factor.", .Type = PropertyType_Float, .Default.FloatValue = 0.0f,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0)),
    SLOT_PAIR_ROWS(30U, "aligned_to", AlignedTo, CATEGORY_SIDE, "Aligned to", "What its frame follows: the \"path\", or the "
        "\"world\": the object's axes, with only its place from the path (lamps all facing one way).",
        .Type = PropertyType_Enum, .Default.EnumValue = PathObjectAlignedTo_Path, .EnumTable = &AlignedToEnum),
    SLOT_PAIR_ROWS(32U, "position_jitter", PositionJitter, CATEGORY_SIDE, "Position jitter", "How far each piece strays from "
        "its place at most, in metres: along it, up and to its right.", .Type = PropertyType_Vector3,
        .Default.Vector3Value = { 0.0f, 0.0f, 0.0f }, .Rules = PROPERTY_RULES_AT_LEAST(0.0)),
    SLOT_PAIR_ROWS(34U, "yaw_jitter", YawJitter, CATEGORY_SIDE, "Yaw jitter", "How far each piece turns about its up at most, "
        "in degrees.", .Type = PropertyType_Angle, .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_AT_LEAST(0.0)),
    SLOT_PAIR_ROWS(36U, "scale_jitter", ScaleJitter, CATEGORY_SIDE, "Scale jitter", "How much each piece's size varies at most, "
        "as a share of it.", .Type = PropertyType_Float, .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_HALF_OPEN(0.0, 1.0)),
    SLOT_PAIR_ROWS(38U, "collision", Collision, CATEGORY_COLLISION, "Collision", "What its pieces collide as: \"none\", the "
        "\"model\"'s collision set per copy, the bent \"mesh\" itself, or simple \"boxes\" along the run.",
        .Type = PropertyType_Enum, .Default.EnumValue = PathObjectCollision_Model, .EnumTable = &CollisionEnum),
    SLOT_PAIR_ROWS(40U, "collision_set", CollisionSet, CATEGORY_COLLISION, "Collision set", "The collision set used instead "
        "of the world model's default one.", .Type = PropertyType_String, .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(PATH_OBJECT_LOCAL_NAME_MAX_LENGTH, true)),
    SLOT_PAIR_ROWS(42U, "collision_layer", CollisionLayerValue, CATEGORY_COLLISION, "Collision layer", "The collision layer "
        "instead of the world model's (or prop).", .Type = PropertyType_Enum, .Default.EnumValue = CollisionLayer_Prop,
        .EnumTable = &COLLISION_LAYER_ENUM),
    SLOT_PAIR_ROWS(44U, "collides_with", CollidesWith, CATEGORY_COLLISION, "Collides with", "The layers it collides with "
        "instead of the world model's mask (or its layer's default), so posts can block while rails do not.",
        .Type = PropertyType_EnumSet, .Default.EnumSetValue = COLLISION_LAYER_MASK_ALL, .EnumTable = &COLLISION_LAYER_ENUM),
    SLOT_PAIR_ROWS(46U, "box_size", BoxSize, CATEGORY_COLLISION, "Box size", "A box's size instead of the mesh's bounds, in "
        "metres: what it adds to the piece's length, its height and its width.", .Type = PropertyType_Vector3,
        .Default.Vector3Value = { 0.0f, 1.0f, 0.2f }, .Rules = PROPERTY_RULES_AT_LEAST(0.0)),
    SLOT_PAIR_ROWS(48U, "box_offset", BoxOffset, CATEGORY_COLLISION, "Box offset", "A box's centre off the mesh's, in metres: "
        "along the piece, up and to its right.", .Type = PropertyType_Vector3, .Default.Vector3Value = { 0.0f, 0.0f, 0.0f }),
    SLOT_PAIR_ROWS(78U, "box_spacing", BoxSpacing, CATEGORY_COLLISION, "Box spacing", "How far apart a bent piece's boxes lie, "
        "in metres.", .Type = PropertyType_Float, .Default.FloatValue = PATH_OBJECT_DEFAULT_BOX_SPACING,
        .Rules = PROPERTY_RULES_ABOVE(0.0)),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAS_OUTLINE, 50U, _overridesHasOutline),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_HAS_OUTLINE, 51U, _hasOutline),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_CASTS_SHADOWS, 52U, _overridesCastsShadows),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_CASTS_SHADOWS, 53U, _castsShadows),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_DISTANCE, 54U, _overridesShadowDistance),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_SHADOW_DISTANCE, 55U, _shadowDistance),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_SHADOW_DISTANCE_MULTIPLIER, 56U, _shadowDistanceMultiplier),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_SHADOW_DISTANCE_FIXED, 57U, _overridesIsShadowDistanceFixed),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_IS_SHADOW_DISTANCE_FIXED, 58U, _isShadowDistanceFixed),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_SHADOW_ONLY, 59U, _overridesIsShadowOnly),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_IS_SHADOW_ONLY, 60U, _isShadowOnly),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAS_TWO_SIDED_SHADOWS, 61U, _overridesHasTwoSidedShadows),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_HAS_TWO_SIDED_SHADOWS, 62U, _hasTwoSidedShadows),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_SHADOW_LOD, 63U, _overridesShadowLod),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_SHADOW_LOD, 64U, _shadowLod),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAZE, 65U, _overridesHaze),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_HAZE, 66U, _haze),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_OCCLUDES_SKY, 67U, _overridesOccludesSky),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OCCLUDES_SKY, 68U, _occludesSky),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_HAS_REFLECTIONS, 69U, _overridesHasReflections),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_HAS_REFLECTIONS, 70U, _hasReflections),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_DRAW_DISTANCE, 71U, _overridesDrawDistance),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_DRAW_DISTANCE, 72U, _drawDistance),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_DRAW_DISTANCE_MULTIPLIER, 73U, _drawDistanceMultiplier),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_IS_DRAW_DISTANCE_FIXED, 74U, _overridesIsDrawDistanceFixed),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_IS_DRAW_DISTANCE_FIXED, 75U, _isDrawDistanceFixed),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_OVERRIDES_LOD_DISTANCES, 76U, _overridesLodDistances),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_LOD_DISTANCES, 77U, _lodDistances),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_LOD_DISTANCE_MULTIPLIER, 80U, _lodDistanceMultiplier),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_IS_LOD_FORCED, 81U, _isLodForced),
    SLOT_RENDER_ROW(WORLD_MODEL_PLACEMENT_ROW_FORCED_LOD, 82U, _forcedLod),
};

const PropertyTable PathObjectInternal_SlotTable =
{
    .Name = u8"path_object_slot", .Version = SLOT_TABLE_VERSION, .StructSize = sizeof(PathObjectSlotRecord),
    .Descriptors = SlotProperties, .DescriptorCount = sizeof(SlotProperties) / sizeof(SlotProperties[0])
};

/* A definition's root: the settings' rows (PathObjectInternal.h). */
static const PropertyDescriptor AssetProperties[] =
{
    PATH_OBJECT_SETTINGS_ROWS(PathObject),
};

static const PropertyTable AssetTable =
{
    .Name = u8"path_object", .Version = PATH_OBJECT_TABLE_VERSION, .StructSize = sizeof(PathObject),
    .Descriptors = AssetProperties, .DescriptorCount = sizeof(AssetProperties) / sizeof(AssetProperties[0])
};


// Static functions.
/* A value when its flag is on. */
#define APPLY(source, target, member) \
    do \
    { \
        if ((source)->Overrides##member) \
        { \
            (target)->Overrides##member = true; \
            (target)->member = (source)->member; \
        } \
    } \
    while (false)

/* The rows of @p source whose flags are on over @p target. */
static void ApplySettings(const PathObjectSettings* source, PathObjectSettings* target)
{
    APPLY(source, target, HasStartCap);
    APPLY(source, target, HasEndCap);
    APPLY(source, target, HasSeparator);
    APPLY(source, target, HasSingle);
    APPLY(source, target, HasCorner);
    APPLY(source, target, Fit);
    APPLY(source, target, Rounding);
    APPLY(source, target, ResizedEnds);
    APPLY(source, target, ResizeMethod);
    APPLY(source, target, Align);
    APPLY(source, target, UsesCount);
    APPLY(source, target, Count);
    APPLY(source, target, UsesGap);
    APPLY(source, target, Gap);
    APPLY(source, target, WhenTooShort);
    APPLY(source, target, MinimumStretch);
    APPLY(source, target, CapsAt);
    APPLY(source, target, SeparatorEvery);
    APPLY(source, target, FitsEachSection);
    APPLY(source, target, Seed);
    APPLY(source, target, SnapsToTerrain);
    APPLY(source, target, GroundOffset);
    APPLY(source, target, GroundSearchHeight);
    APPLY(source, target, GroundSearchDepth);
    APPLY(source, target, GroundTarget);
    APPLY(source, target, GroundLayers);
}

/* A render row's value when its flag is on. */
#define APPLY_RENDER(source, target, flag, value) \
    do \
    { \
        if ((source)->flag) \
        { \
            (target)->flag = true; \
            (target)->value = (source)->value; \
        } \
    } \
    while (false)

/* The rows of @p source whose flags are on over @p target; the multipliers multiply. */
static void ApplySlot(const PathObjectSlotRecord* source, PathObjectSlotRecord* target)
{
    APPLY(source, target, Pick);
    APPLY(source, target, Length);
    APPLY(source, target, EntryDepthStart);
    APPLY(source, target, EntryDepthEnd);
    APPLY(source, target, Stretches);
    APPLY(source, target, PositionOffset);
    APPLY(source, target, RotationOffset);
    APPLY(source, target, Flip);
    APPLY(source, target, Curve);
    APPLY(source, target, SliceSpacing);
    APPLY(source, target, Slope);
    APPLY(source, target, StepAnchor);
    APPLY(source, target, Side);
    APPLY(source, target, SideOffset);
    APPLY(source, target, AlignedTo);
    APPLY(source, target, PositionJitter);
    APPLY(source, target, YawJitter);
    APPLY(source, target, ScaleJitter);
    APPLY(source, target, Collision);
    APPLY(source, target, CollisionSet);
    APPLY(source, target, CollisionLayerValue);
    APPLY(source, target, CollidesWith);
    APPLY(source, target, BoxSize);
    APPLY(source, target, BoxOffset);
    APPLY(source, target, BoxSpacing);
    APPLY_RENDER(source, target, _overridesHasOutline, _hasOutline);
    APPLY_RENDER(source, target, _overridesCastsShadows, _castsShadows);
    APPLY_RENDER(source, target, _overridesShadowDistance, _shadowDistance);
    APPLY_RENDER(source, target, _overridesIsShadowDistanceFixed, _isShadowDistanceFixed);
    APPLY_RENDER(source, target, _overridesIsShadowOnly, _isShadowOnly);
    APPLY_RENDER(source, target, _overridesHasTwoSidedShadows, _hasTwoSidedShadows);
    APPLY_RENDER(source, target, _overridesShadowLod, _shadowLod);
    APPLY_RENDER(source, target, _overridesHaze, _haze);
    APPLY_RENDER(source, target, _overridesOccludesSky, _occludesSky);
    APPLY_RENDER(source, target, _overridesHasReflections, _hasReflections);
    APPLY_RENDER(source, target, _overridesDrawDistance, _drawDistance);
    APPLY_RENDER(source, target, _overridesIsDrawDistanceFixed, _isDrawDistanceFixed);
    APPLY_RENDER(source, target, _overridesLodDistances, _lodDistances);
    APPLY_RENDER(source, target, _isLodForced, _forcedLod);
    target->_shadowDistanceMultiplier *= source->_shadowDistanceMultiplier;
    target->_drawDistanceMultiplier *= source->_drawDistanceMultiplier;
    target->_lodDistanceMultiplier *= source->_lodDistanceMultiplier;
}

/* Applies a list's record of @p slot, the last of them where it holds several. */
static void ApplySlotFrom(const PathObjectSettings* settings, PathObjectSlot slot, PathObjectSlotRecord* target)
{
    if (settings == NULL)
    {
        return;
    }
    const PathObjectSlotRecord* Records = PropertyRecordList_GetRecords(settings->Slots);
    const PathObjectSlotRecord* Last = NULL;
    for (size_t Index = 0U; Index < PropertyRecordList_GetCount(settings->Slots); Index++)
    {
        if (Records[Index].Slot == slot)
        {
            Last = &Records[Index];
        }
    }
    if (Last != NULL)
    {
        ApplySlot(Last, target);
    }
}

/* Writes the records of @p slot in a list, up to @p capacity; how many it holds. */
static size_t CollectModels(const PropertyRecordList* list, PathObjectSlot slot, const PathObjectModelRecord** outModels,
    size_t capacity)
{
    const PathObjectModelRecord* Records = PropertyRecordList_GetRecords(list);
    size_t Found = 0U;
    for (size_t Index = 0U; Index < PropertyRecordList_GetCount(list); Index++)
    {
        if (Records[Index].Slot != slot)
        {
            continue;
        }
        if (Found < capacity)
        {
            outModels[Found] = &Records[Index];
        }
        Found++;
    }
    return Found;
}


// Functions.
const PropertyEnumTable* PathObject_GetSlotEnumTable(void)
{
    return &SlotEnum;
}

const PropertyTable* PathObject_GetModelPropertyTable(void)
{
    return &PathObjectInternal_ModelTable;
}

const PropertyTable* PathObject_GetSlotPropertyTable(void)
{
    return &PathObjectInternal_SlotTable;
}

const PropertyTable* PathObject_GetPropertyTable(void)
{
    return &AssetTable;
}

PathObjectSettings PathObjectSettings_CreateDefault(void)
{
    // The table's defaults hold no string or list, so there is nothing to release and construction cannot fail.
    PathObject Defaults;
    Error Result = Properties_Construct(&AssetTable, &Defaults);
    Error_Deconstruct(&Result);
    return Defaults._settings;
}

PathObjectSlotRecord PathObjectSlotRecord_CreateDefault(PathObjectSlot slot)
{
    PathObjectSlotRecord Defaults;
    Error Result = Properties_Construct(&PathObjectInternal_SlotTable, &Defaults);
    Error_Deconstruct(&Result);
    Defaults.Slot = slot;
    Defaults.Stretches = (slot == PathObjectSlot_Middle) || (slot == PathObjectSlot_Single);
    Defaults.Slope = (slot == PathObjectSlot_Middle) ? PathObjectSlope_Shear : PathObjectSlope_Step;
    return Defaults;
}

PathObjectSettings PathObject_ResolveSettings(const PathObjectSettings* asset, const PathObjectSettings* placement)
{
    PathObjectSettings Result = PathObjectSettings_CreateDefault();
    if (asset != NULL)
    {
        ApplySettings(asset, &Result);
        Result.Models = asset->Models;
        Result.Slots = asset->Slots;
    }
    if (placement != NULL)
    {
        ApplySettings(placement, &Result);
    }
    return Result;
}

PathObjectSlotRecord PathObject_ResolveSlot(PathObjectSlot slot, const PathObjectSettings* asset,
    const PathObjectSettings* placement)
{
    PathObjectSlotRecord Result = PathObjectSlotRecord_CreateDefault(slot);
    ApplySlotFrom(asset, slot, &Result);
    ApplySlotFrom(placement, slot, &Result);
    return Result;
}

bool PathObjectSettings_IsSlotUsed(const PathObjectSettings* settings, PathObjectSlot slot)
{
    switch (slot)
    {
        case PathObjectSlot_StartCap: return settings->HasStartCap;
        case PathObjectSlot_Middle: return true;
        case PathObjectSlot_Separator: return settings->HasSeparator;
        case PathObjectSlot_EndCap: return settings->HasEndCap;
        case PathObjectSlot_Single: return settings->HasSingle;
        case PathObjectSlot_Corner: return settings->HasCorner;
        case PathObjectSlot_Count: return false;
    }
    return false;
}

size_t PathObject_GetSlotModels(PathObjectSlot slot, const PathObjectSettings* asset, const PathObjectSettings* placement,
    const PathObjectModelRecord** outModels, size_t capacity)
{
    if ((placement != NULL) && placement->OverridesModels)
    {
        size_t Count = CollectModels(placement->Models, slot, outModels, capacity);
        if (Count > 0U)
        {
            return Count;
        }
    }
    return (asset != NULL) ? CollectModels(asset->Models, slot, outModels, capacity) : 0U;
}

PathGroundSettings PathObjectSettings_GetGround(const PathObjectSettings* settings)
{
    PathGroundSettings Ground = PathGroundSettings_CreateDefault();
    Ground.Target = settings->GroundTarget;
    Ground.LayerMask = settings->GroundLayers;
    Ground.SearchHeight = settings->GroundSearchHeight;
    Ground.SearchDepth = settings->GroundSearchDepth;
    Ground.Offset = settings->GroundOffset;
    return Ground;
}
