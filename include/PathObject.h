#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "CollisionLayer.h"
#include "PathGround.h"
#include "PropertyDescriptor.h"
#include "WorldModel.h"


/**
 * @file PathObject.h
 * @brief The path object: a fence, hedge, railing or row of lamps as one reusable thing, filling a path with pieces.
 *
 * THE SEQUENCE. A run along a path is filled with this sequence of slots, of which only the middle is required:
 *
 *     start_cap  middle  separator  middle  separator  middle  end_cap
 *
 * Two more replace pieces of it: `single` replaces a whole span where exactly one middle fits, `corner` replaces the
 * separator at a corner node. One segment of a fence is start_cap, middle, end_cap, with nothing special about it. A plain
 * fence names one post in three slots and one panel in the middle; a row of lamps is a middle with gaps and a side.
 *
 * THE ASSET AND ITS PLACEMENTS. A path_object asset (JSON in path_objects/) holds PathObjectSettings; a placement (the
 * path_object component, WorldPathObjectComponent.h) holds the same settings over it, so a floating fence needs no second
 * asset. Every setting is two rows, an "overrides_" flag and its value. In a placement the flags are written out, as for
 * every placement. In an asset's file a setting that is written is in effect, and its flag can be left out. What is in
 * effect is worked out by PathObject_ResolveSettings and PathObject_ResolveSlot: the built-in defaults, then the asset's
 * rows whose flags are on, then the placement's.
 *
 * SLOTS. Each slot takes the same settings (PathObjectSlotRecord, the rows of the "path_object_slot" table), held in the
 * `slots` list, a record per slot, and its weighted models are records of the flat `models` list, each naming its slot
 * (PathObjectModelRecord, the "path_object_model" table). A placement's `slots` records add to the asset's, slot by slot and
 * setting by setting; while its overrides_models is on, a slot its `models` list names takes those models and the others
 * keep the asset's.
 *
 * WHAT A PIECE IS (PathObjectLayout.h, PathObjectPlacement.h, WorldPathObjects.h). Pieces lie end to end by their length: the
 * mesh's size along its X (LOD 0's, under its import transform) less its entry depths, or the setting's. A piece's mesh runs
 * along its own +X, stands on its Y = 0 and is centred on its Z = 0, and its +Z faces right of the path's direction; where it
 * sits along X does not matter. Entry depth is how far the mesh reaches past its length into its neighbour.
 */


// Macros.
/** @brief The most model records one list holds. */
#define PATH_OBJECT_MAX_MODELS ((size_t)256)
/** @brief The most slot records one list holds: more than one per slot is refused where it can be (an asset's), and the last
 *  of them counts in a placement's. */
#define PATH_OBJECT_MAX_SLOT_RECORDS ((size_t)64)
/** @brief The longest asset name of a path object or a model record, in bytes. */
#define PATH_OBJECT_NAME_MAX_LENGTH ((size_t)128)
/** @brief The longest variant or collision set name of a record, in bytes. */
#define PATH_OBJECT_LOCAL_NAME_MAX_LENGTH ((size_t)128)
/** @brief The largest count a placement or an asset may ask for. */
#define PATH_OBJECT_MAX_COUNT 262144U
/** @brief The slice spacing a slot has by default, in metres: a coarser LOD doubles it. */
#define PATH_OBJECT_DEFAULT_SLICE_SPACING 1.0f
/** @brief The smallest stretch a squeezed middle takes by default. */
#define PATH_OBJECT_DEFAULT_MINIMUM_STRETCH 0.25f
/** @brief How far apart a bent piece's boxes lie by default, in metres. */
#define PATH_OBJECT_DEFAULT_BOX_SPACING 2.0f


// Types: the enums. Saved in files by name and in GHDF by value: values are explicit and never renumbered.
/** @brief The slots of the sequence. */
typedef enum PathObjectSlotEnum
{
    /** @brief The first piece of a run. */
    PathObjectSlot_StartCap = 0,
    /** @brief What fills the run: the only slot required. */
    PathObjectSlot_Middle = 1,
    /** @brief Between middles (every separator_every of them), and where two sections of a run meet. */
    PathObjectSlot_Separator = 2,
    /** @brief The last piece of a run. */
    PathObjectSlot_EndCap = 3,
    /** @brief Replaces a whole span, caps too, where exactly one middle fits. */
    PathObjectSlot_Single = 4,
    /** @brief Replaces the separator at a corner node (an L-shaped wall block). */
    PathObjectSlot_Corner = 5,
    /** @brief The number of slots; not a slot. */
    PathObjectSlot_Count = 6
} PathObjectSlot;

/** @brief How a span is filled. */
typedef enum PathObjectFitEnum
{
    /** @brief A whole number of middles, scaled to fill. */
    PathObjectFit_Stretch = 0,
    /** @brief Whole pieces, with the end pieces resized to fill. */
    PathObjectFit_ResizeEnds = 1,
    /** @brief Whole pieces at their length; the align setting says where the leftover goes. */
    PathObjectFit_Fixed = 2
} PathObjectFit;

/** @brief Which way a count of middles rounds, for the stretch and resize_ends fits. */
typedef enum PathObjectRoundingEnum
{
    /** @brief The count that needs the least stretch. */
    PathObjectRounding_Nearest = 0,
    /** @brief The smallest count that reaches the end: pieces shrink. */
    PathObjectRounding_Up = 1,
    /** @brief The largest count that stays within the end: pieces grow. */
    PathObjectRounding_Down = 2
} PathObjectRounding;

/** @brief Which end pieces the resize_ends fit resizes. */
typedef enum PathObjectResizedEndsEnum
{
    /** @brief The first middle of a span. */
    PathObjectResizedEnds_Start = 0,
    /** @brief The last middle of a span. */
    PathObjectResizedEnds_End = 1,
    /** @brief The first and the last, alike. */
    PathObjectResizedEnds_Both = 2
} PathObjectResizedEnds;

/** @brief How the resize_ends fit resizes a piece. */
typedef enum PathObjectResizeMethodEnum
{
    /** @brief Scales it along its length. */
    PathObjectResizeMethod_Scale = 0,
    /** @brief Cuts the mesh off (the count then rounds up), the cut hidden in the cap. */
    PathObjectResizeMethod_Cut = 1
} PathObjectResizeMethod;

/** @brief Where the fixed fit's leftover goes. */
typedef enum PathObjectAlignEnum
{
    /** @brief A gap at the end. */
    PathObjectAlign_Start = 0,
    /** @brief A gap at the start. */
    PathObjectAlign_End = 1,
    /** @brief Equal gaps at both ends. */
    PathObjectAlign_Center = 2,
    /** @brief Equal gaps between pieces, flush at both ends. */
    PathObjectAlign_Between = 3,
    /** @brief Equal gaps between pieces, half a gap at each end. */
    PathObjectAlign_Around = 4,
    /** @brief The same gap everywhere. */
    PathObjectAlign_Evenly = 5
} PathObjectAlign;

/** @brief What a span does where one middle does not fit. */
typedef enum PathObjectTooShortEnum
{
    /** @brief Squeezes it, down to minimum_stretch; shorter than that, as caps_only. */
    PathObjectTooShort_Squeeze = 0,
    /** @brief The single slot, stretched or squeezed by its own settings; without one, as caps_only. */
    PathObjectTooShort_Single = 1,
    /** @brief Only the caps, where they fit. */
    PathObjectTooShort_CapsOnly = 2,
    /** @brief Nothing at all. */
    PathObjectTooShort_Nothing = 3
} PathObjectTooShort;

/** @brief Where the caps sit when a fixed run leaves a gap. */
typedef enum PathObjectCapsAtEnum
{
    /** @brief At the ends of the path's stretch, with the gap between them and the pieces. */
    PathObjectCapsAt_Path = 0,
    /** @brief Next to the pieces, with the gap beyond them. */
    PathObjectCapsAt_Run = 1
} PathObjectCapsAt;

/** @brief How a slot chooses among its models. */
typedef enum PathObjectPickEnum
{
    /** @brief By weight, from the seed: the same on every machine and every load. */
    PathObjectPick_Random = 0,
    /** @brief In the list's order, over and over. */
    PathObjectPick_InOrder = 1
} PathObjectPick;

/** @brief Whether a piece is turned end for end (about its up, through its middle). */
typedef enum PathObjectFlipEnum
{
    /** @brief Never. */
    PathObjectFlip_None = 0,
    /** @brief Always. */
    PathObjectFlip_Always = 1,
    /** @brief Every other piece of the slot. */
    PathObjectFlip_Alternating = 2,
    /** @brief By the seed. */
    PathObjectFlip_Random = 3
} PathObjectFlip;

/** @brief How a piece follows the curve. */
typedef enum PathObjectCurveEnum
{
    /** @brief A straight copy, its ends on the curve. */
    PathObjectCurve_Rigid = 0,
    /** @brief Its vertices follow the curve: the game slices it along its length. */
    PathObjectCurve_Bent = 1
} PathObjectCurve;

/** @brief How a piece takes a slope. */
typedef enum PathObjectSlopeEnum
{
    /** @brief Its verticals stay vertical and its length climbs: panels stay level with the posts. */
    PathObjectSlope_Shear = 0,
    /** @brief It leans with the slope. */
    PathObjectSlope_Tilt = 1,
    /** @brief It stays level, at the height the step anchor says. */
    PathObjectSlope_Step = 2
} PathObjectSlope;

/** @brief Which height a level (step) piece takes. */
typedef enum PathObjectStepAnchorEnum
{
    /** @brief Half way between its ends. */
    PathObjectStepAnchor_Middle = 0,
    /** @brief Its higher end's: it floats over the lower end. */
    PathObjectStepAnchor_Higher = 1,
    /** @brief Its lower end's: it sinks at the higher end. */
    PathObjectStepAnchor_Lower = 2
} PathObjectStepAnchor;

/** @brief Which side of the path a piece stands on. */
typedef enum PathObjectSideEnum
{
    /** @brief On the path itself. */
    PathObjectSide_OnPath = 0,
    /** @brief Left of the path's direction, turned end for end to face the path as its right twin does. */
    PathObjectSide_Left = 1,
    /** @brief Right of the path's direction. */
    PathObjectSide_Right = 2,
    /** @brief Both: two copies per piece. */
    PathObjectSide_Both = 3,
    /** @brief Right, left, right, ... */
    PathObjectSide_Alternating = 4
} PathObjectSide;

/** @brief What a piece's frame follows. */
typedef enum PathObjectAlignedToEnum
{
    /** @brief The path: its X along the path. */
    PathObjectAlignedTo_Path = 0,
    /** @brief The object's axes: only its place comes from the path (lamps all facing one way). */
    PathObjectAlignedTo_World = 1
} PathObjectAlignedTo;

/** @brief What a slot's pieces collide with. */
typedef enum PathObjectCollisionEnum
{
    /** @brief Nothing. */
    PathObjectCollision_None = 0,
    /** @brief The world model's collision set, per copy (a world model without a collision section: nothing). */
    PathObjectCollision_Model = 1,
    /** @brief The bent mesh itself, per generated piece; a rigid copy's own bent mesh does not exist, so it counts as model. */
    PathObjectCollision_Mesh = 2,
    /** @brief Simple boxes along the run: one a rigid piece, every box_spacing metres along a bent one. */
    PathObjectCollision_Boxes = 3
} PathObjectCollision;


// Types: the records.
/**
 * @brief One weighted model of a slot: a record of the models list ("path_object_model"). A plain struct the table describes;
 *        its names are owned by the list it lies in.
 */
typedef struct PathObjectModelRecordStruct
{
    /** @brief The slot it belongs to. */
    PathObjectSlot Slot;
    /** @brief The world model (an asset name, kept in full). */
    unsigned char* WorldModel;
    /** @brief How often it is picked against the slot's others, above 0. */
    float Weight;
    /** @brief Whether its copies draw a variant of the world model. */
    bool UsesVariant;
    /** @brief The variant's name while UsesVariant is on. */
    unsigned char* Variant;
    /** @brief A colour multiplied into its copies' own. */
    Color Tint;
} PathObjectModelRecord;

/**
 * @brief The settings of one slot: a record of the slots list ("path_object_slot"). Every setting is a flag and a value; a
 *        flag that is off leaves the setting to what lies under it (PathObject_ResolveSlot). A plain struct the table
 *        describes; its names and lists are owned by the list it lies in.
 */
typedef struct PathObjectSlotRecordStruct
{
    /** @brief The slot it speaks for. */
    PathObjectSlot Slot;

    /** @brief How its pieces choose among the slot's models. */
    bool OverridesPick;
    PathObjectPick Pick;
    /** @brief The piece's length in metres instead of its mesh's measured size. A piece of length 0 sits on a point. */
    bool OverridesLength;
    float Length;
    /** @brief How far its mesh reaches past its length at its start, in metres. */
    bool OverridesEntryDepthStart;
    float EntryDepthStart;
    /** @brief How far its mesh reaches past its length at its end, in metres. */
    bool OverridesEntryDepthEnd;
    float EntryDepthEnd;
    /** @brief Whether the stretch fit scales it (by default the middle and the single slot yes, the others no). */
    bool OverridesStretches;
    bool Stretches;
    /** @brief A position off its place in metres: along the piece, up and to its right. */
    bool OverridesPositionOffset;
    Vector3 PositionOffset;
    /** @brief A rotation on top of its frame (Euler XYZ, radians; degrees in files). */
    bool OverridesRotationOffset;
    Vector3 RotationOffset;
    /** @brief Whether a piece is turned end for end. */
    bool OverridesFlip;
    PathObjectFlip Flip;

    /** @brief How it follows the curve. */
    bool OverridesCurve;
    PathObjectCurve Curve;
    /** @brief How far apart a bent piece's slices lie at LOD 0, in metres (each coarser LOD doubles it), above 0. */
    bool OverridesSliceSpacing;
    float SliceSpacing;
    /** @brief How it takes a slope (by default the middle shears, the others step). */
    bool OverridesSlope;
    PathObjectSlope Slope;
    /** @brief Which height a level piece takes. */
    bool OverridesStepAnchor;
    PathObjectStepAnchor StepAnchor;

    /** @brief Which side of the path it stands on. */
    bool OverridesSide;
    PathObjectSide Side;
    /** @brief How far from the path a side piece stands, in metres, at least 0 (times the node's width factor). */
    bool OverridesSideOffset;
    float SideOffset;
    /** @brief What its frame follows. */
    bool OverridesAlignedTo;
    PathObjectAlignedTo AlignedTo;
    /** @brief How far each piece strays from its place at most, in metres: along it, up and to its right, each at least 0. */
    bool OverridesPositionJitter;
    Vector3 PositionJitter;
    /** @brief How far each piece turns about its up at most, in radians (degrees in files), at least 0. */
    bool OverridesYawJitter;
    float YawJitter;
    /** @brief How much each piece's size varies at most, as a share of it, in [0, 1). */
    bool OverridesScaleJitter;
    float ScaleJitter;

    /** @brief What it collides with. */
    bool OverridesCollision;
    PathObjectCollision Collision;
    /** @brief The collision set used instead of the world model's default one. */
    bool OverridesCollisionSet;
    unsigned char* CollisionSet;
    /** @brief The collision layer instead of the world model's (or prop). */
    bool OverridesCollisionLayerValue;
    CollisionLayer CollisionLayerValue;
    /** @brief The layers it collides with instead of the world model's mask (or its layer's default). */
    bool OverridesCollidesWith;
    uint32_t CollidesWith;
    /** @brief A box's size in metres (across the X it gains on the piece's length, its height, its width) instead of the mesh's
     *  bounds. */
    bool OverridesBoxSize;
    Vector3 BoxSize;
    /** @brief A box's centre off the mesh's, in metres: along the piece, up and to its right. */
    bool OverridesBoxOffset;
    Vector3 BoxOffset;
    /** @brief How far apart a bent piece's boxes lie, in metres, above 0. */
    bool OverridesBoxSpacing;
    float BoxSpacing;

    /* The render and LOD settings a model instances component has, with its keys, its pairs over the world model's. */
    bool _overridesHasOutline;
    bool _hasOutline;
    bool _overridesCastsShadows;
    bool _castsShadows;
    bool _overridesShadowDistance;
    float _shadowDistance;
    float _shadowDistanceMultiplier;
    bool _overridesIsShadowDistanceFixed;
    bool _isShadowDistanceFixed;
    bool _overridesIsShadowOnly;
    bool _isShadowOnly;
    bool _overridesHasTwoSidedShadows;
    bool _hasTwoSidedShadows;
    bool _overridesShadowLod;
    uint32_t _shadowLod;
    bool _overridesHaze;
    float _haze;
    bool _overridesOccludesSky;
    bool _occludesSky;
    bool _overridesHasReflections;
    bool _hasReflections;
    bool _overridesDrawDistance;
    float _drawDistance;
    float _drawDistanceMultiplier;
    bool _overridesIsDrawDistanceFixed;
    bool _isDrawDistanceFixed;
    bool _overridesLodDistances;
    PropertyFloatList* _lodDistances;
    float _lodDistanceMultiplier;
    bool _isLodForced;
    uint32_t _forcedLod;
} PathObjectSlotRecord;

/**
 * @brief The settings of a whole path object: what an asset holds and a placement overrides. Every setting is a flag and a
 *        value (a multiplier-like plain value has none). The lists are owned by whoever holds the struct.
 */
typedef struct PathObjectSettingsStruct
{
    /** @brief Whether the placement's models replace the asset's, for the slots its list names. */
    bool OverridesModels;
    /** @brief The weighted models (PathObjectModelRecord), each naming its slot. */
    PropertyRecordList* Models;
    /** @brief The slots' settings (PathObjectSlotRecord), a record per slot that has any. */
    PropertyRecordList* Slots;

    /** @brief Whether the slot is in use: only the middle is required. */
    bool OverridesHasStartCap;
    bool HasStartCap;
    bool OverridesHasEndCap;
    bool HasEndCap;
    bool OverridesHasSeparator;
    bool HasSeparator;
    bool OverridesHasSingle;
    bool HasSingle;
    bool OverridesHasCorner;
    bool HasCorner;

    /** @brief How a span is filled. */
    bool OverridesFit;
    PathObjectFit Fit;
    /** @brief Which way a count of middles rounds. */
    bool OverridesRounding;
    PathObjectRounding Rounding;
    /** @brief Which end pieces resize_ends resizes. */
    bool OverridesResizedEnds;
    PathObjectResizedEnds ResizedEnds;
    /** @brief How resize_ends resizes them. */
    bool OverridesResizeMethod;
    PathObjectResizeMethod ResizeMethod;
    /** @brief Where the fixed fit's leftover goes. */
    bool OverridesAlign;
    PathObjectAlign Align;
    /** @brief Whether a span holds exactly Count middles, as far as they fit. */
    bool OverridesUsesCount;
    bool UsesCount;
    bool OverridesCount;
    uint32_t Count;
    /** @brief Whether the fixed fit keeps a gap of Gap metres between its pieces: as many pieces as that leaves room for. */
    bool OverridesUsesGap;
    bool UsesGap;
    bool OverridesGap;
    float Gap;
    /** @brief What a span does where one middle does not fit. */
    bool OverridesWhenTooShort;
    PathObjectTooShort WhenTooShort;
    /** @brief The smallest stretch a squeezed piece takes, in (0, 1]. */
    bool OverridesMinimumStretch;
    float MinimumStretch;
    /** @brief Where the caps sit when a fixed run leaves a gap. */
    bool OverridesCapsAt;
    PathObjectCapsAt CapsAt;
    /** @brief A separator after every this many middles, at least 1. */
    bool OverridesSeparatorEvery;
    uint32_t SeparatorEvery;
    /** @brief Whether each section fits on its own, so a corner never falls in the middle of a piece. */
    bool OverridesFitsEachSection;
    bool FitsEachSection;
    /** @brief The seed of every random choice. */
    bool OverridesSeed;
    uint32_t Seed;

    /** @brief Whether its points snap to the ground (a section's node may say otherwise for that section). */
    bool OverridesSnapsToTerrain;
    bool SnapsToTerrain;
    /** @brief How far above the ground its points lie, in metres (negative sinks them). */
    bool OverridesGroundOffset;
    float GroundOffset;
    /** @brief How far above a point the search for the ground starts, in metres, at least 0. */
    bool OverridesGroundSearchHeight;
    float GroundSearchHeight;
    /** @brief How far below a point the search goes, in metres, at least 0. */
    bool OverridesGroundSearchDepth;
    float GroundSearchDepth;
    /** @brief What counts as the ground. */
    bool OverridesGroundTarget;
    PathGroundTarget GroundTarget;
    /** @brief The layers that count, for the layers target. */
    bool OverridesGroundLayers;
    uint32_t GroundLayers;
} PathObjectSettings;

/** @brief A loaded path_object asset. Built and owned by the asset manager; read-only. */
typedef struct PathObjectStruct
{
    /** @brief Its settings, as its file gave them. */
    PathObjectSettings _settings;
} PathObject;


// Functions: the tables.
/**
 * @brief The enums' tables, for tests and tools.
 * @returns The tables (static).
 */
const PropertyEnumTable* PathObject_GetSlotEnumTable(void);

/**
 * @brief The table of one model record ("path_object_model").
 * @returns The table (static, immutable).
 */
const PropertyTable* PathObject_GetModelPropertyTable(void);

/**
 * @brief The table of one slot record ("path_object_slot"): what the editor shows for a slot, of an asset or a placement.
 * @returns The table (static, immutable).
 */
const PropertyTable* PathObject_GetSlotPropertyTable(void);

/**
 * @brief The asset's table ("path_object"): the keys of a definition's root.
 * @returns The table (static, immutable). Its struct is a PathObject.
 */
const PropertyTable* PathObject_GetPropertyTable(void);


// Functions: the asset.
/**
 * @brief The settings of a loaded asset.
 * @param self The asset; must not be NULL.
 * @returns Its settings (borrowed, valid while it is loaded).
 */
static inline const PathObjectSettings* PathObject_GetSettings(const PathObject* self)
{
    return &self->_settings;
}


// Functions: what is in effect.
/**
 * @brief The built-in defaults of every setting: the tables' defaults, with every flag off and the lists empty.
 * @returns The settings (no lists to release).
 */
PathObjectSettings PathObjectSettings_CreateDefault(void);

/**
 * @brief The built-in defaults of one slot: the table's, with the two that depend on the slot: whether the stretch fit scales
 *        it (the middle and the single slot) and how it takes a slope (the middle shears, the others step).
 * @param slot The slot.
 * @returns The record (no lists or names to release; its flags are off).
 */
PathObjectSlotRecord PathObjectSlotRecord_CreateDefault(PathObjectSlot slot);

/**
 * @brief The settings in effect: the built-in defaults, then each row of @p asset whose flag is on, then each of @p placement.
 *
 * A pure function. The lists in the result are borrowed: the placement's while its overrides_models is on (the caller merges
 * the models with PathObject_GetSlotModels), and @p asset's; Slots is the asset's.
 * @param asset The asset's settings, or NULL for the defaults alone.
 * @param placement The placement's settings, or NULL for the asset's alone.
 * @returns The settings in effect (borrowing; never released).
 */
PathObjectSettings PathObject_ResolveSettings(const PathObjectSettings* asset, const PathObjectSettings* placement);

/**
 * @brief The settings of one slot in effect: its defaults (PathObjectSlotRecord_CreateDefault), then the asset's record of the
 *        slot, then the placement's. A render pair's flag in the result says whether either gave it, so what stays off is left
 *        to the world model. The multipliers multiply. Of several records of one slot in a list, the last counts.
 *
 * A pure function; the names and lists in the result are borrowed from the records.
 * @param slot The slot.
 * @param asset The asset's settings, or NULL.
 * @param placement The placement's settings, or NULL.
 * @returns The slot's settings in effect (borrowing; never released).
 */
PathObjectSlotRecord PathObject_ResolveSlot(PathObjectSlot slot, const PathObjectSettings* asset,
    const PathObjectSettings* placement);

/**
 * @brief Whether a slot is in use: the middle always, the others by their flags in effect.
 * @param settings The settings in effect; must not be NULL.
 * @param slot The slot.
 * @returns true when in use.
 */
bool PathObjectSettings_IsSlotUsed(const PathObjectSettings* settings, PathObjectSlot slot);

/**
 * @brief The models of one slot in effect: while the placement overrides its models and its list names the slot, those;
 *        otherwise the asset's.
 *
 * Writes pointers to the records (borrowed from the lists), in the list's order.
 * @param slot The slot.
 * @param asset The asset's settings, or NULL.
 * @param placement The placement's settings, or NULL.
 * @param outModels [out] Receives up to @p capacity records. May be NULL when @p capacity is 0.
 * @param capacity How many it holds.
 * @returns How many records the slot has (which may be more than @p capacity).
 */
size_t PathObject_GetSlotModels(PathObjectSlot slot, const PathObjectSettings* asset, const PathObjectSettings* placement,
    const PathObjectModelRecord** outModels, size_t capacity);

/**
 * @brief The ground settings in effect (PathGround.h), from the settings in effect.
 * @param settings The settings in effect; must not be NULL.
 * @returns The settings for PathGround_Project.
 */
PathGroundSettings PathObjectSettings_GetGround(const PathObjectSettings* settings);
