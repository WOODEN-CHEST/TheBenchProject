#include "WorldAnimatorComponent.h"
#include "WorldAnimationInternal.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the animator component's property table. */
#define ANIMATOR_TABLE_VERSION 1U
/** The inspector's category. */
#define CATEGORY_ANIMATION ((const unsigned char*)u8"Animation")


// Types.
/* The rows of the animator's table, so the setters reach their descriptors without a search. */
typedef enum AnimatorPropertyEnum
{
    AnimatorProperty_PlaysClip,
    AnimatorProperty_Clip,
    AnimatorProperty_Phase,
    AnimatorProperty_Speed,
    AnimatorProperty_IsPaused,
    AnimatorProperty_FadeSeconds,
    AnimatorProperty_OverridesIsLooped,
    AnimatorProperty_IsLooped,
    AnimatorProperty_OverridesAnimationLibraries,
    AnimatorProperty_AnimationLibraries,
    AnimatorProperty_OverridesAnimationGraph,
    AnimatorProperty_AnimationGraph,
    AnimatorProperty_Count
} AnimatorProperty;


// Fields.
/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. The next free wire id is 13. */
static const PropertyDescriptor AnimatorProperties[AnimatorProperty_Count] =
{
    [AnimatorProperty_PlaysClip] =
    {
        .Key = u8"plays_clip", .WireId = 1U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldAnimatorComponent, _playsClip), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION, .DisplayName = u8"Plays a clip",
        .Description = u8"Plays the clip below from the start (off: only what scripts and the game play).",
        .SinceVersion = 1U
    },
    [AnimatorProperty_Clip] =
    {
        .Key = u8"clip", .WireId = 2U, .Type = PropertyType_String, PROPERTY_MEMBER(WorldAnimatorComponent, _clip),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(WORLD_ANIMATOR_CLIP_NAME_MAX_LENGTH, true),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION, .DisplayName = u8"Clip",
        .Description = u8"The clip it plays, by name, from its animation libraries; changing it cross-fades.",
        .SinceVersion = 1U
    },
    [AnimatorProperty_Phase] =
    {
        .Key = u8"phase", .WireId = 3U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldAnimatorComponent, _phase),
        .Default.FloatValue = 0.0f, .Rules = PROPERTY_RULES_HALF_OPEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_ANIMATION, .DisplayName = u8"Start phase",
        .Description = u8"Where the clip starts, as a share of its length, so placements of one model do not move in step.",
        .SinceVersion = 1U
    },
    [AnimatorProperty_Speed] =
    {
        .Key = u8"speed", .WireId = 4U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldAnimatorComponent, _speed),
        .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_BETWEEN(0.0, (double)WORLD_ANIMATOR_MAX_SPEED),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION, .DisplayName = u8"Speed",
        .Description = u8"How fast every clip plays, times each clip's own speed (0 holds the pose).", .SinceVersion = 1U
    },
    [AnimatorProperty_IsPaused] =
    {
        .Key = u8"is_paused", .WireId = 5U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldAnimatorComponent, _isPaused),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION,
        .DisplayName = u8"Paused", .Description = u8"Holds the pose: clips and cross-fades stand still.",
        .SinceVersion = 1U
    },
    [AnimatorProperty_FadeSeconds] =
    {
        .Key = u8"fade_seconds", .WireId = 6U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldAnimatorComponent, _fadeSeconds), .Default.FloatValue = WORLD_ANIMATOR_DEFAULT_FADE_SECONDS,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, (double)WORLD_ANIMATOR_MAX_FADE_SECONDS), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_ANIMATION, .DisplayName = u8"Cross-fade time",
        .Description = u8"How long a change of the clip cross-fades, in seconds.", .SinceVersion = 1U
    },
    [AnimatorProperty_OverridesIsLooped] =
    {
        .Key = u8"overrides_is_looped", .WireId = 7U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldAnimatorComponent, _overridesIsLooped), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION, .DisplayName = u8"Override looping",
        .Description = u8"Decides below whether the clip loops, instead of its animation library.", .SinceVersion = 1U
    },
    [AnimatorProperty_IsLooped] =
    {
        .Key = u8"is_looped", .WireId = 8U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldAnimatorComponent, _isLooped),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION,
        .DisplayName = u8"Loops", .Description = u8"Whether the clip loops, while overridden.", .SinceVersion = 1U
    },
    [AnimatorProperty_OverridesAnimationLibraries] =
    {
        .Key = u8"overrides_animation_libraries", .WireId = 9U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldAnimatorComponent, _overridesAnimationLibraries), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION, .DisplayName = u8"Override animation libraries",
        .Description = u8"Plays the libraries below instead of its world model's.", .SinceVersion = 1U
    },
    [AnimatorProperty_AnimationLibraries] =
    {
        .Key = u8"animation_libraries", .WireId = 10U, .Type = PropertyType_AssetNameList,
        PROPERTY_MEMBER(WorldAnimatorComponent, _animationLibraries), .Default.AssetNameListValue = NULL,
        .Rules = PROPERTY_RULES_ASSET_NAME_LIST(WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES,
            WORLD_ANIMATOR_LIBRARY_NAME_MAX_LENGTH),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION, .DisplayName = u8"Animation libraries",
        .Description = u8"The animation libraries it plays while overridden: their clips, matched to its skeleton by bone "
            u8"name.", .SinceVersion = 1U
    },
    [AnimatorProperty_OverridesAnimationGraph] =
    {
        // Version 1 like the rest: a file without the graph rows reads them as their defaults (no new table version).
        .Key = u8"overrides_animation_graph", .WireId = 11U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldAnimatorComponent, _overridesAnimationGraph), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_ANIMATION, .DisplayName = u8"Override animation graph",
        .Description = u8"Plays the graph below instead of its world model's (none: no graph).", .SinceVersion = 1U
    },
    [AnimatorProperty_AnimationGraph] =
    {
        .Key = u8"animation_graph", .WireId = 12U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldAnimatorComponent, _animationGraph), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(WORLD_ANIMATOR_GRAPH_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_ANIMATION, .DisplayName = u8"Animation graph",
        .Description = u8"The animation graph it plays while overridden: which clip plays when, and its special actions.",
        .SinceVersion = 1U
    },
};

static const PropertyTable AnimatorTable =
{
    .Name = u8"animator_component",
    .Version = ANIMATOR_TABLE_VERSION,
    .StructSize = sizeof(WorldAnimatorComponent),
    .Descriptors = AnimatorProperties,
    .DescriptorCount = AnimatorProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
};


// Static functions.
static Error ConstructAnimator(WorldComponent* component)
{
    WorldAnimatorComponent* Self = (WorldAnimatorComponent*)component;
    Error Result = Properties_Construct(&AnimatorTable, Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_runtime = NULL;
    Memory_Zero(&Self->_pose, sizeof(Self->_pose));
    return Error_CreateSuccess();
}

static void DeconstructAnimator(WorldComponent* component)
{
    WorldAnimatorComponent* Self = (WorldAnimatorComponent*)component;
    WorldAnimationInternal_DestroyRuntime(Self->_runtime);
    Self->_runtime = NULL;
    Memory_Zero(&Self->_pose, sizeof(Self->_pose));
    Properties_Deconstruct(&AnimatorTable, Self);
}

/* An animator leaving its object ends its action first, so the character it locked is not left locked, and what
 * hangs on its sockets is marked dirty: their frames go with the pose (they fall back to the object's own space). */
static void DetachAnimator(World* world, WorldObject* object, WorldComponent* component)
{
    WorldAnimationInternal_DetachAnimator((WorldAnimatorComponent*)component);
    WorldInternal_MarkSocketChildrenChanged(world, object);
}

static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
}

/* Sets one row through the table: its rules, the ownership of a string or list, the revision and the hooks. */
static Error SetRow(WorldAnimatorComponent* self, AnimatorProperty property, PropertyValue value, const char* function)
{
    if (self == NULL)
    {
        return CreateNullSelfError(function);
    }
    return Properties_Set(&AnimatorTable, self, AnimatorProperties[property].Key, &value);
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo AnimatorTypeInfo =
{
    .Type = WorldComponentType_Animator,
    .Name = u8"animator",
    .Size = sizeof(WorldAnimatorComponent),
    .Table = &AnimatorTable,
    .IsTransformDriver = false,
    .Construct = ConstructAnimator,
    .Deconstruct = DeconstructAnimator,
    .OnAttach = NULL,
    .OnDetach = DetachAnimator,
    .OnPropertySet = NULL
};


// Public functions.
const WorldComponentTypeInfo* WorldAnimatorComponent_GetTypeInfo(void)
{
    return &AnimatorTypeInfo;
}

const PropertyTable* WorldAnimatorComponent_GetPropertyTable(void)
{
    return &AnimatorTable;
}

Error WorldAnimatorComponent_SetPlaysClip(WorldAnimatorComponent* self, bool playsClip)
{
    return SetRow(self, AnimatorProperty_PlaysClip, PropertyValue_FromBool(playsClip),
        "WorldAnimatorComponent_SetPlaysClip");
}

Error WorldAnimatorComponent_SetClip(WorldAnimatorComponent* self, const unsigned char* clip)
{
    return SetRow(self, AnimatorProperty_Clip, PropertyValue_FromString(clip), "WorldAnimatorComponent_SetClip");
}

Error WorldAnimatorComponent_SetPhase(WorldAnimatorComponent* self, float phase)
{
    return SetRow(self, AnimatorProperty_Phase, PropertyValue_FromFloat(phase), "WorldAnimatorComponent_SetPhase");
}

Error WorldAnimatorComponent_SetSpeed(WorldAnimatorComponent* self, float speed)
{
    return SetRow(self, AnimatorProperty_Speed, PropertyValue_FromFloat(speed), "WorldAnimatorComponent_SetSpeed");
}

Error WorldAnimatorComponent_SetPaused(WorldAnimatorComponent* self, bool isPaused)
{
    return SetRow(self, AnimatorProperty_IsPaused, PropertyValue_FromBool(isPaused), "WorldAnimatorComponent_SetPaused");
}

Error WorldAnimatorComponent_SetFadeSeconds(WorldAnimatorComponent* self, float fadeSeconds)
{
    return SetRow(self, AnimatorProperty_FadeSeconds, PropertyValue_FromFloat(fadeSeconds),
        "WorldAnimatorComponent_SetFadeSeconds");
}

Error WorldAnimatorComponent_SetLoopOverride(WorldAnimatorComponent* self, bool overridesIsLooped, bool isLooped)
{
    Error Result = SetRow(self, AnimatorProperty_IsLooped, PropertyValue_FromBool(isLooped),
        "WorldAnimatorComponent_SetLoopOverride");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetRow(self, AnimatorProperty_OverridesIsLooped, PropertyValue_FromBool(overridesIsLooped),
        "WorldAnimatorComponent_SetLoopOverride");
}

Error WorldAnimatorComponent_SetAnimationGraph(WorldAnimatorComponent* self, bool overridesGraph,
    const unsigned char* graph)
{
    Error Result = SetRow(self, AnimatorProperty_AnimationGraph, PropertyValue_FromAssetName(graph),
        "WorldAnimatorComponent_SetAnimationGraph");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetRow(self, AnimatorProperty_OverridesAnimationGraph, PropertyValue_FromBool(overridesGraph),
        "WorldAnimatorComponent_SetAnimationGraph");
}

Error WorldAnimatorComponent_SetAnimationLibraries(WorldAnimatorComponent* self, bool overridesLibraries,
    const PropertyAssetNameList* libraries)
{
    Error Result = SetRow(self, AnimatorProperty_AnimationLibraries, PropertyValue_FromAssetNameList(libraries),
        "WorldAnimatorComponent_SetAnimationLibraries");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return SetRow(self, AnimatorProperty_OverridesAnimationLibraries, PropertyValue_FromBool(overridesLibraries),
        "WorldAnimatorComponent_SetAnimationLibraries");
}
