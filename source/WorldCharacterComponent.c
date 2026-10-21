#include <inttypes.h>
#include <math.h>
#include "WorldCharacterComponent.h"
#include "CharacterInternal.h"
#include "WorldInternal.h"
#include "AssetManager.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the character component's property table. */
#define CHARACTER_COMPONENT_TABLE_VERSION 1U
/** The rows: the character's name, then an override flag and a value per setting. */
#define CHARACTER_PROPERTY_COUNT (1U + 2U * (unsigned int)CharacterSetting_Count)
/** A setting's flag row and value row. The flag ("overrides_" + the setting's key, a Bool; false: the character asset's
 *  value applies) sits right before the value, which shares its row with the asset's table (CharacterInternal.h).
 *  @p name is how the flag's texts call the setting. */
#define CHARACTER_PROPERTY_PAIR(setting, key, row, member, category, name) \
    [1U + 2U * (unsigned int)(setting)] = \
    { \
        .Key = u8"overrides_" key, .WireId = CHARACTER_FLAG_WIRE_ID(setting), .Type = PropertyType_Bool, \
        PROPERTY_MEMBER(WorldCharacterComponent, _overrides[setting]), .Default.BoolValue = false, \
        .Flags = PROPERTY_FLAGS_SETTING, .Category = (category), .DisplayName = u8"Override " name, \
        .Description = u8"Uses this character's own " name u8" instead of its kind's.", .SinceVersion = 1U \
    }, \
    [2U + 2U * (unsigned int)(setting)] = \
    { \
        row, .WireId = CHARACTER_VALUE_WIRE_ID(setting), PROPERTY_MEMBER(WorldCharacterComponent, _values.member), \
        .SinceVersion = 1U \
    }
/** The name row's index. */
#define CHARACTER_PROPERTY_NAME 0U
/** A full turn, for wrapping yaws. */
#define CHARACTER_FULL_TURN (2.0f * PI)


// Fields.
static const PropertyEnumEntry FacingEntries[] =
{
    { .Name = u8"movement", .Value = CharacterFacing_Movement },
    { .Name = u8"look", .Value = CharacterFacing_Look },
    { .Name = u8"keep", .Value = CharacterFacing_Keep },
};

const PropertyEnumTable CHARACTER_FACING_ENUM =
{
    .Name = u8"character_facing", .Entries = FacingEntries,
    .EntryCount = sizeof(FacingEntries) / sizeof(FacingEntries[0])
};

/* Every property, in inspector order: the name, then each setting's override flag right before the value it switches
 * on. The wire ids are the save schema: never renumber or reuse one (CharacterInternal.h: setting i's flag 2 + 2i, its
 * value 3 + 2i); a removed property's id goes into the table's retired list. The next free wire id is 74. */
static const PropertyDescriptor CharacterProperties[CHARACTER_PROPERTY_COUNT] =
{
    [CHARACTER_PROPERTY_NAME] =
    {
        .Key = u8"character", .WireId = 1U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldCharacterComponent, _character), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(WORLD_CHARACTER_COMPONENT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CHARACTER_CATEGORY_BODY, .DisplayName = u8"Character",
        .Description = u8"The kind of character (a human, a dog, a pigeon): its body and how it moves; none uses the "
            u8"defaults (a human).",
        .SinceVersion = 1U
    },
    CHARACTER_PROPERTY_PAIR(CharacterSetting_Radius, "radius", CHARACTER_ROW_RADIUS, Radius, CHARACTER_CATEGORY_BODY,
        "radius"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_Height, "height", CHARACTER_ROW_HEIGHT, Height, CHARACTER_CATEGORY_BODY,
        "height"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_EyeHeight, "eye_height", CHARACTER_ROW_EYE_HEIGHT, EyeHeight,
        CHARACTER_CATEGORY_BODY, "eye height"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_Mass, "mass", CHARACTER_ROW_MASS, Mass, CHARACTER_CATEGORY_BODY, "mass"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_IsPushable, "is_pushable", CHARACTER_ROW_IS_PUSHABLE, IsPushable,
        CHARACTER_CATEGORY_BODY, "pushability"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CollidesWith, "collides_with", CHARACTER_ROW_COLLIDES_WITH, CollidesWith,
        CHARACTER_CATEGORY_BODY, "collision layers"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_WalkSpeed, "walk_speed", CHARACTER_ROW_WALK_SPEED, WalkSpeed,
        CHARACTER_CATEGORY_WALKING, "walking speed"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CanRun, "can_run", CHARACTER_ROW_CAN_RUN, CanRun,
        CHARACTER_CATEGORY_WALKING, "running permission"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_RunSpeed, "run_speed", CHARACTER_ROW_RUN_SPEED, RunSpeed,
        CHARACTER_CATEGORY_WALKING, "running speed"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_Acceleration, "acceleration", CHARACTER_ROW_ACCELERATION, Acceleration,
        CHARACTER_CATEGORY_WALKING, "acceleration"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_Deceleration, "deceleration", CHARACTER_ROW_DECELERATION, Deceleration,
        CHARACTER_CATEGORY_WALKING, "deceleration"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_StepHeight, "step_height", CHARACTER_ROW_STEP_HEIGHT, StepHeight,
        CHARACTER_CATEGORY_WALKING, "step height"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_SlopeLimit, "slope_limit", CHARACTER_ROW_SLOPE_LIMIT, SlopeLimit,
        CHARACTER_CATEGORY_WALKING, "slope limit"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_GroundSnapDistance, "ground_snap_distance",
        CHARACTER_ROW_GROUND_SNAP_DISTANCE, GroundSnapDistance, CHARACTER_CATEGORY_WALKING, "ground snap"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_TurnSpeed, "turn_speed", CHARACTER_ROW_TURN_SPEED, TurnSpeed,
        CHARACTER_CATEGORY_TURNING, "turn speed"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_IsTurnInstant, "is_turn_instant", CHARACTER_ROW_IS_TURN_INSTANT,
        IsTurnInstant, CHARACTER_CATEGORY_TURNING, "instant turning"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CanCrouch, "can_crouch", CHARACTER_ROW_CAN_CROUCH, CanCrouch,
        CHARACTER_CATEGORY_CROUCHING, "crouching permission"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CrouchHeight, "crouch_height", CHARACTER_ROW_CROUCH_HEIGHT, CrouchHeight,
        CHARACTER_CATEGORY_CROUCHING, "crouched height"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CrouchEyeHeight, "crouch_eye_height", CHARACTER_ROW_CROUCH_EYE_HEIGHT,
        CrouchEyeHeight, CHARACTER_CATEGORY_CROUCHING, "crouched eye height"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CrouchSpeed, "crouch_speed", CHARACTER_ROW_CROUCH_SPEED, CrouchSpeed,
        CHARACTER_CATEGORY_CROUCHING, "crouched speed"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CrouchTransitionSeconds, "crouch_transition_seconds",
        CHARACTER_ROW_CROUCH_TRANSITION_SECONDS, CrouchTransitionSeconds, CHARACTER_CATEGORY_CROUCHING, "crouch time"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CanJump, "can_jump", CHARACTER_ROW_CAN_JUMP, CanJump,
        CHARACTER_CATEGORY_JUMPING, "jumping permission"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_JumpSpeed, "jump_speed", CHARACTER_ROW_JUMP_SPEED, JumpSpeed,
        CHARACTER_CATEGORY_JUMPING, "jump speed"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CanJumpWhileCrouched, "can_jump_while_crouched",
        CHARACTER_ROW_CAN_JUMP_WHILE_CROUCHED, CanJumpWhileCrouched, CHARACTER_CATEGORY_JUMPING,
        "crouched jumping permission"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CoyoteSeconds, "coyote_seconds", CHARACTER_ROW_COYOTE_SECONDS,
        CoyoteSeconds, CHARACTER_CATEGORY_JUMPING, "late jump time"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_JumpBufferSeconds, "jump_buffer_seconds",
        CHARACTER_ROW_JUMP_BUFFER_SECONDS, JumpBufferSeconds, CHARACTER_CATEGORY_JUMPING, "early jump time"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_GravityMultiplier, "gravity_multiplier", CHARACTER_ROW_GRAVITY_MULTIPLIER,
        GravityMultiplier, CHARACTER_CATEGORY_FALLING, "gravity"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_AirAcceleration, "air_acceleration", CHARACTER_ROW_AIR_ACCELERATION,
        AirAcceleration, CHARACTER_CATEGORY_FALLING, "air control"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_MaxFallSpeed, "max_fall_speed", CHARACTER_ROW_MAX_FALL_SPEED,
        MaxFallSpeed, CHARACTER_CATEGORY_FALLING, "fastest fall"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_CanFly, "can_fly", CHARACTER_ROW_CAN_FLY, CanFly,
        CHARACTER_CATEGORY_FLYING, "flying permission"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_FlySpeed, "fly_speed", CHARACTER_ROW_FLY_SPEED, FlySpeed,
        CHARACTER_CATEGORY_FLYING, "flying speed"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_FlyFastSpeed, "fly_fast_speed", CHARACTER_ROW_FLY_FAST_SPEED,
        FlyFastSpeed, CHARACTER_CATEGORY_FLYING, "fast flying speed"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_FlyAcceleration, "fly_acceleration", CHARACTER_ROW_FLY_ACCELERATION,
        FlyAcceleration, CHARACTER_CATEGORY_FLYING, "flying acceleration"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_FlyDeceleration, "fly_deceleration", CHARACTER_ROW_FLY_DECELERATION,
        FlyDeceleration, CHARACTER_CATEGORY_FLYING, "flying deceleration"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_InteractionReach, "interaction_reach", CHARACTER_ROW_INTERACTION_REACH,
        InteractionReach, CHARACTER_CATEGORY_INTERACTION, "reach"),
    CHARACTER_PROPERTY_PAIR(CharacterSetting_InteractionLayers, "interaction_layers",
        CHARACTER_ROW_INTERACTION_LAYERS, InteractionLayers, CHARACTER_CATEGORY_INTERACTION, "seen layers"),
};

static const PropertyTable CharacterTable =
{
    .Name = u8"character_component",
    .Version = CHARACTER_COMPONENT_TABLE_VERSION,
    .StructSize = sizeof(WorldCharacterComponent),
    .Descriptors = CharacterProperties,
    .DescriptorCount = CHARACTER_PROPERTY_COUNT,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
};


// Static functions: binding.
/* Logs (once, now) that a character's asset could not be bound, and releases the error. */
static void ReportBindFailure(World* world, const WorldObject* object, const unsigned char* assetName, Error* error)
{
    Logger* Log = WorldInternal_GetLogger(world);
    if (Log != NULL)
    {
        const unsigned char* ObjectName = WorldObject_GetName(object);
        Error LogResult = Logger_LogWarningFormatted(Log,
            (const unsigned char*)u8"World: the character of object %" PRIu64 u8" (\"%s\") uses the default settings: "
            u8"character \"%s\" could not be loaded (%s).",
            WorldObject_GetId(object),
            (ObjectName != NULL) ? (const char*)ObjectName : "",
            (assetName != NULL) ? (const char*)assetName : "",
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Binds the character asset the component names, loaded under the world's asset user, or none when the world has no
 * asset manager, the component names none, or the load fails (a recoverable content problem: logged once, here, and the
 * defaults apply). The name is remembered either way, so the same name is not bound (or reported) again. */
static void Bind(World* world, const WorldObject* object, WorldCharacterComponent* character)
{
    // TODO: A previous character asset stays held under the world's user until the world is destroyed (holds are not
    // counted, so releasing it here could unload it under another character), as for sprites.
    const unsigned char* AssetName = character->_character;
    AssetManager* Assets = WorldInternal_GetAssets(world);
    CharacterAsset* Bound = NULL;
    if ((Assets != NULL) && (AssetName != NULL))
    {
        Error LoadResult = AssetManager_LoadCharacter(Assets, AssetName, WorldInternal_GetAssetUser(world), &Bound);
        if (LoadResult.Code != ErrorCode_Success)
        {
            ReportBindFailure(world, object, AssetName, &LoadResult);
            Bound = NULL;
        }
    }
    character->_boundCharacter = Bound;

    unsigned char* BoundName = WorldInternal_CloneString(AssetName);
    Memory_Free(character->_boundName);
    character->_boundName = BoundName;
}


// Static functions: the input.
/* An angle wrapped into (-pi, pi]. */
static float WrapAngle(float angle)
{
    float Wrapped = fmodf(angle, CHARACTER_FULL_TURN);
    if (Wrapped > PI)
    {
        Wrapped -= CHARACTER_FULL_TURN;
    }
    else if (Wrapped <= -PI)
    {
        Wrapped += CHARACTER_FULL_TURN;
    }
    return Wrapped;
}

/* The yaw an object's world matrix faces: its +Z axis turned about +Y. */
static float GetObjectYaw(const WorldObject* object)
{
    Matrix WorldMatrix = WorldObject_GetWorldMatrix(object);
    float ForwardX = WorldMatrix.m8;
    float ForwardZ = WorldMatrix.m10;
    if ((fabsf(ForwardX) < 1e-6f) && (fabsf(ForwardZ) < 1e-6f))
    {
        return 0.0f;
    }
    return atan2f(ForwardX, ForwardZ);
}


// Static functions: the registry's hooks.
/* The state before the first update: standing, walking, nowhere in particular; the system fills in the rest. */
static WorldCharacterState CreateInitialState(void)
{
    WorldCharacterState State;
    Memory_Zero(&State, sizeof(State));
    State.Mode = CharacterMovementMode_Walking;
    State.Stance = CharacterStance_Standing;
    State.GroundNormal = (Vector3){ 0.0f, 1.0f, 0.0f };
    State.Height = CHARACTER_DEFAULT_HEIGHT;
    State.EyeHeight = CHARACTER_DEFAULT_EYE_HEIGHT;
    return State;
}

static Error ConstructCharacter(WorldComponent* component)
{
    WorldCharacterComponent* Self = (WorldCharacterComponent*)component;
    Error Result = Properties_Construct(&CharacterTable, Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_boundCharacter = NULL;
    Self->_boundName = NULL;
    Self->_input = CharacterInput_CreateDefault();
    Self->_state = CreateInitialState();
    Memory_Zero(&Self->_runtime, sizeof(Self->_runtime));
    return Error_CreateSuccess();
}

static void DeconstructCharacter(WorldComponent* component)
{
    WorldCharacterComponent* Self = (WorldCharacterComponent*)component;
    Memory_Free(Self->_boundName);
    Self->_boundName = NULL;
    Self->_boundCharacter = NULL;
    Properties_Deconstruct(&CharacterTable, Self);
}

/* Binds the character asset, and faces and looks the way the object already faces (a placed NPC keeps its turn). */
static void AttachCharacter(World* world, WorldObject* object, WorldComponent* component)
{
    WorldCharacterComponent* Self = (WorldCharacterComponent*)component;
    Bind(world, object, Self);
    float Yaw = GetObjectYaw(object);
    Self->_state.FacingYaw = Yaw;
    Self->_input.LookYaw = Yaw;
}

/* Rebinds when the name changed: through its own row, or through a read or a copy that may have changed anything. */
static void OnCharacterPropertySet(World* world, WorldObject* object, WorldComponent* component,
    const PropertyDescriptor* descriptor)
{
    WorldCharacterComponent* Self = (WorldCharacterComponent*)component;
    bool MayHaveRenamed = (descriptor == NULL) || (descriptor == &CharacterProperties[CHARACTER_PROPERTY_NAME]);
    if (MayHaveRenamed && !WorldInternal_AreStringsEqual(Self->_character, Self->_boundName))
    {
        Bind(world, object, Self);
    }
}


// Static functions: the setters.
static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
}

static Error CreateSettingError(const char* function, CharacterSetting setting)
{
    return Error_Construct4(ErrorCode_ArgumentOutOfRange, "%s: %d is not a CharacterSetting.", function, (int)setting);
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo CharacterTypeInfo =
{
    .Type = WorldComponentType_Character,
    .Name = u8"character",
    .Size = sizeof(WorldCharacterComponent),
    .Table = &CharacterTable,
    .IsTransformDriver = true,
    .Construct = ConstructCharacter,
    .Deconstruct = DeconstructCharacter,
    .OnAttach = AttachCharacter,
    .OnDetach = NULL,
    .OnPropertySet = OnCharacterPropertySet
};


// Public functions: the input.
CharacterInput CharacterInput_CreateDefault(void)
{
    return (CharacterInput)
    {
        .MoveDirection = { 0.0f, 0.0f, 0.0f },
        .IsRunning = false,
        .WantsCrouch = false,
        .WantsJump = false,
        .WantsToFly = false,
        .WantsUse = false,
        .LookYaw = 0.0f,
        .LookPitch = 0.0f,
        .Facing = CharacterFacing_Movement
    };
}

Vector3 CharacterInput_GetLookDirection(const CharacterInput* self)
{
    float CosinePitch = cosf(self->LookPitch);
    return (Vector3){ sinf(self->LookYaw) * CosinePitch, sinf(self->LookPitch), cosf(self->LookYaw) * CosinePitch };
}

void CharacterInput_SetLookDirection(CharacterInput* self, Vector3 direction)
{
    float Length = Vector3Length(direction);
    if (!(Length > 1e-6f) || !WorldInternal_IsFloatFinite(Length))
    {
        return;
    }
    Vector3 Unit = Vector3Scale(direction, 1.0f / Length);
    float Horizontal = sqrtf((Unit.x * Unit.x) + (Unit.z * Unit.z));
    if (Horizontal > 1e-6f)
    {
        self->LookYaw = atan2f(Unit.x, Unit.z);
    }
    self->LookPitch = Clamp(atan2f(Unit.y, Horizontal), -CHARACTER_MAX_LOOK_PITCH, CHARACTER_MAX_LOOK_PITCH);
}


// Public functions: the type.
const WorldComponentTypeInfo* WorldCharacterComponent_GetTypeInfo(void)
{
    return &CharacterTypeInfo;
}

const PropertyTable* WorldCharacterComponent_GetPropertyTable(void)
{
    return &CharacterTable;
}


// Public functions: the settings.
Error WorldCharacterComponent_SetCharacter(WorldCharacterComponent* self, const unsigned char* character)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldCharacterComponent_SetCharacter");
    }
    PropertyValue Value = PropertyValue_FromAssetName(character);
    Error CheckResult = Properties_CheckValue(&CharacterTable, &CharacterProperties[CHARACTER_PROPERTY_NAME], &Value);
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    unsigned char* Clone = WorldInternal_CloneString(character);
    Memory_Free(self->_character);
    self->_character = Clone;
    WorldInternal_ComponentAfterSet(self, &CharacterProperties[CHARACTER_PROPERTY_NAME]);
    return Error_CreateSuccess();
}

bool WorldCharacterComponent_IsOverridden(const WorldCharacterComponent* self, CharacterSetting setting)
{
    // A value outside the enum: nothing is overridden.
    return ((unsigned int)setting < (unsigned int)CharacterSetting_Count) && self->_overrides[setting];
}

Error WorldCharacterComponent_SetOverridden(WorldCharacterComponent* self, CharacterSetting setting, bool isOverridden)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldCharacterComponent_SetOverridden");
    }
    if ((unsigned int)setting >= (unsigned int)CharacterSetting_Count)
    {
        return CreateSettingError("WorldCharacterComponent_SetOverridden", setting);
    }
    self->_overrides[setting] = isOverridden;
    WorldInternal_ComponentAfterSet(self, &CharacterProperties[1U + 2U * (unsigned int)setting]);
    return Error_CreateSuccess();
}

Error WorldCharacterComponent_OverrideSetting(WorldCharacterComponent* self, CharacterSetting setting,
    const PropertyValue* value)
{
    if ((self == NULL) || (value == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldCharacterComponent_OverrideSetting: self and value must not be NULL.");
    }
    if ((unsigned int)setting >= (unsigned int)CharacterSetting_Count)
    {
        return CreateSettingError("WorldCharacterComponent_OverrideSetting", setting);
    }
    // The table checks the value's type and rules and writes it (nothing changes on failure); then the flag.
    const PropertyDescriptor* ValueRow = &CharacterProperties[2U + 2U * (unsigned int)setting];
    Error Result = Properties_Set(&CharacterTable, self, ValueRow->Key, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return WorldCharacterComponent_SetOverridden(self, setting, true);
}

CharacterSettings WorldCharacterComponent_ResolveSettings(const WorldCharacterComponent* self,
    const CharacterSettings* characterSettings)
{
    CharacterSettings Settings = (characterSettings != NULL) ? *characterSettings : CharacterSettings_CreateDefault();
    // The placement's value of every overridden setting, field by field through the shared rows' offsets (the
    // placement's values are a CharacterSettings too).
    for (unsigned int Setting = 0U; Setting < (unsigned int)CharacterSetting_Count; Setting++)
    {
        if (!self->_overrides[Setting])
        {
            continue;
        }
        const PropertyDescriptor* Row = CharacterSettings_GetDescriptor((CharacterSetting)Setting);
        Memory_Copy((const unsigned char*)&self->_values + Row->Offset, (unsigned char*)&Settings + Row->Offset,
            Row->Size);
    }
    return Settings;
}

CharacterSettings WorldCharacterComponent_GetSettings(const WorldCharacterComponent* self)
{
    return WorldCharacterComponent_ResolveSettings(self,
        (self->_boundCharacter != NULL) ? CharacterAsset_GetSettings(self->_boundCharacter) : NULL);
}


// Public functions: the input and the state.
Error WorldCharacterComponent_SetInput(WorldCharacterComponent* self, const CharacterInput* input)
{
    if ((self == NULL) || (input == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldCharacterComponent_SetInput: self and input must not be NULL.");
    }
    bool IsFinite = WorldInternal_IsFloatFinite(input->MoveDirection.x)
        && WorldInternal_IsFloatFinite(input->MoveDirection.y) && WorldInternal_IsFloatFinite(input->MoveDirection.z)
        && WorldInternal_IsFloatFinite(input->LookYaw) && WorldInternal_IsFloatFinite(input->LookPitch);
    if (!IsFinite)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldCharacterComponent_SetInput: the move direction and the look angles must be finite.");
    }
    if ((unsigned int)input->Facing > (unsigned int)CharacterFacing_Keep)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "WorldCharacterComponent_SetInput: %d is not a CharacterFacing.", (int)input->Facing);
    }

    CharacterInput Input = *input;
    // Shortened to length 1 when longer; scaled by its largest component first, so a huge finite direction's length
    // cannot overflow (to infinity, which made it zero).
    Vector3 Move = Input.MoveDirection;
    float Largest = Math_MaxFloat(fabsf(Move.x), Math_MaxFloat(fabsf(Move.y), fabsf(Move.z)));
    if (Largest > 1.0f)
    {
        Move = Vector3Scale(Move, 1.0f / Largest);
    }
    float Length = Vector3Length(Move);
    if (Length > 1.0f)
    {
        Move = Vector3Scale(Move, 1.0f / Length);
    }
    Input.MoveDirection = Move;
    Input.LookYaw = WrapAngle(Input.LookYaw);
    Input.LookPitch = Clamp(Input.LookPitch, -CHARACTER_MAX_LOOK_PITCH, CHARACTER_MAX_LOOK_PITCH);
    self->_input = Input;
    return Error_CreateSuccess();
}

Vector3 WorldCharacterComponent_GetEyePosition(const WorldCharacterComponent* self)
{
    Vector3 Feet = WorldObject_GetWorldPosition(WorldComponent_GetOwner(&self->Base));
    return (Vector3){ Feet.x, Feet.y + self->_state.EyeHeight, Feet.z };
}
