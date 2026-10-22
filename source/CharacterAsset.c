// The character asset's data functions: the settings' property table, its defaults and the rules across settings. The
// definition and the loader are in CharacterDefinition.c; the rows are shared with the character component's table
// (CharacterInternal.h).

#include "CharacterAsset.h"
#include "CharacterInternal.h"
#include "wr/WRMath.h"


// Macros.
/** The format version of the character's table. */
#define CHARACTER_TABLE_VERSION 1U
/** One row of the settings table: the shared row, its wire id and its member. */
#define SETTINGS_ROW(setting, row, member) \
    [setting] = { row, .WireId = CHARACTER_VALUE_WIRE_ID(setting), PROPERTY_MEMBER(CharacterSettings, member), \
        .SinceVersion = 1U }


// Fields.
static const PropertyEnumEntry GroundControlEntries[] =
{
    { .Name = u8"push", .Value = CharacterGroundControl_Push },
    { .Name = u8"steer", .Value = CharacterGroundControl_Steer },
};

const PropertyEnumTable CHARACTER_GROUND_CONTROL_ENUM =
{
    .Name = u8"character_ground_control", .Entries = GroundControlEntries,
    .EntryCount = sizeof(GroundControlEntries) / sizeof(GroundControlEntries[0])
};

/* The keys of a definition, one per CharacterSetting in its order. Character assets are JSON only; each row takes the
 * component's wire id for the same setting's value. */
static const PropertyDescriptor SettingsProperties[CharacterSetting_Count] =
{
    SETTINGS_ROW(CharacterSetting_Radius, CHARACTER_ROW_RADIUS, Radius),
    SETTINGS_ROW(CharacterSetting_Height, CHARACTER_ROW_HEIGHT, Height),
    SETTINGS_ROW(CharacterSetting_EyeHeight, CHARACTER_ROW_EYE_HEIGHT, EyeHeight),
    SETTINGS_ROW(CharacterSetting_Mass, CHARACTER_ROW_MASS, Mass),
    SETTINGS_ROW(CharacterSetting_IsPushable, CHARACTER_ROW_IS_PUSHABLE, IsPushable),
    SETTINGS_ROW(CharacterSetting_CollidesWith, CHARACTER_ROW_COLLIDES_WITH, CollidesWith),
    SETTINGS_ROW(CharacterSetting_WalkSpeed, CHARACTER_ROW_WALK_SPEED, WalkSpeed),
    SETTINGS_ROW(CharacterSetting_CanRun, CHARACTER_ROW_CAN_RUN, CanRun),
    SETTINGS_ROW(CharacterSetting_RunSpeed, CHARACTER_ROW_RUN_SPEED, RunSpeed),
    SETTINGS_ROW(CharacterSetting_Acceleration, CHARACTER_ROW_ACCELERATION, Acceleration),
    SETTINGS_ROW(CharacterSetting_GroundFriction, CHARACTER_ROW_GROUND_FRICTION, GroundFriction),
    SETTINGS_ROW(CharacterSetting_StepHeight, CHARACTER_ROW_STEP_HEIGHT, StepHeight),
    SETTINGS_ROW(CharacterSetting_SlopeLimit, CHARACTER_ROW_SLOPE_LIMIT, SlopeLimit),
    SETTINGS_ROW(CharacterSetting_GroundSnapDistance, CHARACTER_ROW_GROUND_SNAP_DISTANCE, GroundSnapDistance),
    SETTINGS_ROW(CharacterSetting_TurnSpeed, CHARACTER_ROW_TURN_SPEED, TurnSpeed),
    SETTINGS_ROW(CharacterSetting_IsTurnInstant, CHARACTER_ROW_IS_TURN_INSTANT, IsTurnInstant),
    SETTINGS_ROW(CharacterSetting_CanCrouch, CHARACTER_ROW_CAN_CROUCH, CanCrouch),
    SETTINGS_ROW(CharacterSetting_CrouchHeight, CHARACTER_ROW_CROUCH_HEIGHT, CrouchHeight),
    SETTINGS_ROW(CharacterSetting_CrouchEyeHeight, CHARACTER_ROW_CROUCH_EYE_HEIGHT, CrouchEyeHeight),
    SETTINGS_ROW(CharacterSetting_CrouchSpeed, CHARACTER_ROW_CROUCH_SPEED, CrouchSpeed),
    SETTINGS_ROW(CharacterSetting_CrouchTransitionSeconds, CHARACTER_ROW_CROUCH_TRANSITION_SECONDS,
        CrouchTransitionSeconds),
    SETTINGS_ROW(CharacterSetting_CanJump, CHARACTER_ROW_CAN_JUMP, CanJump),
    SETTINGS_ROW(CharacterSetting_JumpSpeed, CHARACTER_ROW_JUMP_SPEED, JumpSpeed),
    SETTINGS_ROW(CharacterSetting_CanJumpWhileCrouched, CHARACTER_ROW_CAN_JUMP_WHILE_CROUCHED, CanJumpWhileCrouched),
    SETTINGS_ROW(CharacterSetting_CoyoteSeconds, CHARACTER_ROW_COYOTE_SECONDS, CoyoteSeconds),
    SETTINGS_ROW(CharacterSetting_JumpBufferSeconds, CHARACTER_ROW_JUMP_BUFFER_SECONDS, JumpBufferSeconds),
    SETTINGS_ROW(CharacterSetting_GravityMultiplier, CHARACTER_ROW_GRAVITY_MULTIPLIER, GravityMultiplier),
    SETTINGS_ROW(CharacterSetting_AirAcceleration, CHARACTER_ROW_AIR_ACCELERATION, AirAcceleration),
    SETTINGS_ROW(CharacterSetting_MaxFallSpeed, CHARACTER_ROW_MAX_FALL_SPEED, MaxFallSpeed),
    SETTINGS_ROW(CharacterSetting_CanFly, CHARACTER_ROW_CAN_FLY, CanFly),
    SETTINGS_ROW(CharacterSetting_FlySpeed, CHARACTER_ROW_FLY_SPEED, FlySpeed),
    SETTINGS_ROW(CharacterSetting_FlyFastSpeed, CHARACTER_ROW_FLY_FAST_SPEED, FlyFastSpeed),
    SETTINGS_ROW(CharacterSetting_FlyAcceleration, CHARACTER_ROW_FLY_ACCELERATION, FlyAcceleration),
    SETTINGS_ROW(CharacterSetting_FlyDeceleration, CHARACTER_ROW_FLY_DECELERATION, FlyDeceleration),
    SETTINGS_ROW(CharacterSetting_InteractionReach, CHARACTER_ROW_INTERACTION_REACH, InteractionReach),
    SETTINGS_ROW(CharacterSetting_InteractionLayers, CHARACTER_ROW_INTERACTION_LAYERS, InteractionLayers),
    SETTINGS_ROW(CharacterSetting_GroundControl, CHARACTER_ROW_GROUND_CONTROL, GroundControl),
    SETTINGS_ROW(CharacterSetting_WalkJumpBoost, CHARACTER_ROW_WALK_JUMP_BOOST, WalkJumpBoost),
    SETTINGS_ROW(CharacterSetting_RunJumpBoost, CHARACTER_ROW_RUN_JUMP_BOOST, RunJumpBoost),
    SETTINGS_ROW(CharacterSetting_CanAutoJump, CHARACTER_ROW_CAN_AUTO_JUMP, CanAutoJump),
    SETTINGS_ROW(CharacterSetting_AutoJumpDelaySeconds, CHARACTER_ROW_AUTO_JUMP_DELAY_SECONDS,
        AutoJumpDelaySeconds),
    SETTINGS_ROW(CharacterSetting_AirDrag, CHARACTER_ROW_AIR_DRAG, AirDrag),
};

static const PropertyTable SettingsTable =
{
    .Name = u8"character",
    .Version = CHARACTER_TABLE_VERSION,
    .StructSize = sizeof(CharacterSettings),
    .Descriptors = SettingsProperties,
    .DescriptorCount = CharacterSetting_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = NULL,
    .Migrate = NULL
};


// Public functions.
const PropertyTable* CharacterSettings_GetPropertyTable(void)
{
    return &SettingsTable;
}

const PropertyDescriptor* CharacterSettings_GetDescriptor(CharacterSetting setting)
{
    return ((unsigned int)setting < (unsigned int)CharacterSetting_Count) ? &SettingsProperties[setting] : NULL;
}

CharacterSettings CharacterSettings_CreateDefault(void)
{
    // The defaults are the table's (one copy, shared with the character component). The table holds no strings, so
    // constructing cannot fail and needs no deconstruction.
    CharacterSettings Settings;
    Error Result = Properties_Construct(&SettingsTable, &Settings);
    Error_Deconstruct(&Result);
    return Settings;
}

Error CharacterSettings_CheckBody(const CharacterSettings* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "CharacterSettings_CheckBody: self must not be NULL.");
    }
    float Diameter = 2.0f * self->Radius;
    if (self->Height < Diameter)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "character: height (%g m) must be at least twice the radius (%g m): the capsule's caps are part of it.",
            (double)self->Height, (double)self->Radius);
    }
    if (self->CrouchHeight < Diameter)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "character: crouch_height (%g m) must be at least twice the radius (%g m): the capsule's caps are part of it.",
            (double)self->CrouchHeight, (double)self->Radius);
    }
    if (self->CrouchHeight > self->Height)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "character: crouch_height (%g m) must not be above height (%g m).", (double)self->CrouchHeight,
            (double)self->Height);
    }
    return Error_CreateSuccess();
}

void CharacterSettings_GetBodyHeights(const CharacterSettings* self, float* outHeight, float* outCrouchHeight)
{
    float Diameter = 2.0f * self->Radius;
    float Height = Math_MaxFloat(self->Height, Diameter);
    *outHeight = Height;
    *outCrouchHeight = Math_ClampFloat(self->CrouchHeight, Diameter, Height);
}
