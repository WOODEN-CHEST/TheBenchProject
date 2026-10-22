#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector3 and Matrix are raylib types held by value (the input's direction, the state's velocities, the ground's
// placement); this include is unavoidable here.
#include "raylib/raylib.h"
#include "CharacterAsset.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"
#include "wr/WRError.h"


/**
 * @file WorldCharacterComponent.h
 * @brief The character component: what makes an object walk, crouch, jump, fly and look around (T13), the player and
 *        NPCs alike.
 *
 * WHAT. A character is an upright capsule whose feet are at its object's position. Its settings come from a character
 * asset (CharacterAsset.h: a human, a dog, a pigeon) named by the character row, and the placement may override EVERY
 * setting with an explicit flag next to its own value (overrides_<key> with <key>; the owner's decision G24). Without a
 * character asset (none named, a world without assets, a failed load, logged once) the table's defaults apply.
 * WorldCharacterComponent_ResolveSettings gives the settings in effect. Changing the name swaps the kind of character
 * and keeps the object.
 *
 * WHO DRIVES IT. Nobody in particular: the component holds a CharacterInput (where to move, run, crouch, jump, fly,
 * use, where to look, how to face), kept until changed. The player controller fills it from the keyboard and the
 * mouse; an NPC's brain (later scripts) fills it the same way. A character nobody drives still stands, falls, rides
 * platforms and is pushed.
 *
 * WHO MOVES IT. The character system (WorldCharacters.h) once a tick, in the tick's drivers slot: the component is a
 * TRANSFORM DRIVER, so an object carries at most one and nothing else writes its transform (besides the collision
 * step's pushes, which the system reads back). The system also gives the object a capsule collider if it has none, and
 * keeps that collider's shape, layer, mask, mass and motion rows in step with the settings and the stance: those rows
 * belong to the character. The object's scale should stay 1.
 *
 * WHAT IT SHOWS. WorldCharacterComponent_GetState: the movement mode, the stance, the ground, the velocity, the facing,
 * the eye height, the crouch amount, the time in the air: what the camera, the animation (T29, T30) and scripts read.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Character, &component); reach its fields with
 * WorldCharacterComponent_FromComponent. Its table ("character_component") holds the character's name, then for each
 * setting, in CharacterSetting's order, its override flag and the placement's own value.
 */


// Macros.
/** @brief The most bytes the character asset's name may have (without the terminator). */
#define WORLD_CHARACTER_COMPONENT_NAME_MAX_LENGTH ((size_t)128)
/** @brief The steepest a character looks up or down, in radians (89 degrees, the camera's limit). */
#define CHARACTER_MAX_LOOK_PITCH 1.5533f


// Types.
/**
 * @brief How a character moves. Runtime state, not saved; the values are stable for scripts.
 */
typedef enum CharacterMovementModeEnum
{
    /** @brief On its feet: walking, running, crouching, jumping and falling, with gravity (the default). */
    CharacterMovementMode_Walking = 0,
    /** @brief Flying in 3D without gravity (a character whose asset allows it, while its input asks). */
    CharacterMovementMode_Flying = 1
} CharacterMovementMode;

/**
 * @brief How a character stands. Runtime state, not saved; the values are stable for scripts.
 */
typedef enum CharacterStanceEnum
{
    /** @brief Standing at its full height (the default). */
    CharacterStance_Standing = 0,
    /** @brief Crouched at its crouch height. */
    CharacterStance_Crouching = 1
} CharacterStance;

/**
 * @brief How a character's body turns. Part of its input; the values are stable for scripts.
 */
typedef enum CharacterFacingEnum
{
    /** @brief Toward where it moves (NPCs, animals); it keeps its facing while it stands still (the default). */
    CharacterFacing_Movement = 0,
    /** @brief Toward where it looks (the player in first person; an NPC talking to someone). */
    CharacterFacing_Look = 1,
    /** @brief Not at all: it keeps its facing. */
    CharacterFacing_Keep = 2
} CharacterFacing;

/**
 * @brief What whoever drives a character wants it to do: the same for the player and for NPCs. A plain value type,
 *        kept by the component until changed (WorldCharacterComponent_SetInput).
 *
 * The requests are HELD states, not one-shot commands: a jump or a use happens when its flag turns on (a press), so a
 * driver that keeps setting the same input does not use again and again. A jump held down jumps again on the ground
 * once the character's auto-jump delay has passed since its last jump, when it may auto-jump (hopping).
 */
typedef struct CharacterInputStruct
{
    /** @brief Where to move, in world space; its length (at most 1, longer is shortened) scales the speed, so an analog
     *  stick or a strolling NPC moves slower. Walking uses its horizontal part, flying all of it. */
    Vector3 MoveDirection;
    /** @brief Run (walking) or fly fast (flying), when the character may. */
    bool IsRunning;
    /** @brief Crouch while held, when the character may; standing up waits for room. */
    bool WantsCrouch;
    /** @brief Jump: a press asks for one jump, kept for the character's jump buffer time until it can; held on the
     *  ground, it asks again once the auto-jump delay has passed since the last jump (CanAutoJump). */
    bool WantsJump;
    /** @brief Fly while held, when the character may; letting go walks again (and falls if in the air). */
    bool WantsToFly;
    /** @brief Use: a press uses what the character looks at (a "used" event, WorldCharacters.h). */
    bool WantsUse;
    /** @brief Where it looks: the yaw in radians, world space (0 looks along +Z, growing toward +X, as GameCamera's). */
    float LookYaw;
    /** @brief Where it looks: the pitch in radians, up positive, within +-CHARACTER_MAX_LOOK_PITCH. */
    float LookPitch;
    /** @brief How its body turns. */
    CharacterFacing Facing;
} CharacterInput;

/**
 * @brief What a character is doing: read-only to everything but the character system. A plain value type.
 */
typedef struct WorldCharacterStateStruct
{
    /** @brief How it moves. */
    CharacterMovementMode Mode;
    /** @brief How it stands. */
    CharacterStance Stance;
    /** @brief Whether it stands on walkable ground (walking only). */
    bool IsGrounded;
    /** @brief Whether walkable ground is right below it, whatever the mode: a flying bird about to land. */
    bool IsTouchingGround;
    /** @brief The object it stands on (0: none, while not grounded). */
    uint64_t GroundObjectId;
    /** @brief The walkable ground's normal (straight up while not grounded). */
    Vector3 GroundNormal;
    /** @brief Where it touches its ground, world space (under its round bottom: on a slope or an edge, not straight below
     *  its feet); valid while grounded. The step height is measured from the plane through it along GroundNormal. */
    Vector3 GroundPoint;
    /** @brief How fast it actually moved in its last update, relative to the ground under it, metres per second: its own
     *  move after walls took their part, and what the collision step pushed it since (what an animation shows: two
     *  characters pressing into each other stand still, one shoved by a door moves). */
    Vector3 Velocity;
    /** @brief The velocity of the ground under its feet (a moving platform), metres per second; zero without one. */
    Vector3 GroundVelocity;
    /** @brief The body's yaw, radians (its object's turn about +Y). */
    float FacingYaw;
    /** @brief How fast the body turned in the last update, radians per second (positive: the yaw grew). */
    float TurnRate;
    /** @brief The capsule's height now (the stance's), metres. */
    float Height;
    /** @brief The eyes above the feet now, metres: eased between the standing and the crouched eye height. */
    float EyeHeight;
    /** @brief How far the eyes are down toward the crouched eye height: 0 standing, 1 crouched. */
    float CrouchAmount;
    /** @brief Whether it stays crouched only because standing up would not fit. */
    bool IsStandUpBlocked;
    /** @brief Seconds since it left the ground (0 while grounded or flying). */
    float TimeInAir;
} WorldCharacterState;

/**
 * @brief The character system's bookkeeping for one character (WorldCharacters.c). Private: never read or write it.
 */
typedef struct WorldCharacterRuntimeStruct
{
    /** The settings in effect, cached: valid while _hasSettings, for the component revision and the bound asset they
     *  were resolved from. */
    bool _hasSettings;
    uint32_t _settingsRevision;
    const CharacterAsset* _settingsAsset;
    CharacterSettings _settings;
    /** Whether the system has placed the character yet (its first update finds the ground). */
    bool _isStarted;
    /** The velocity it drives with (its wish, accelerated toward, gravity, jumps; walls take their part, pushes do not:
     *  a heavy character keeps walking while it shoves a light one), metres per second: relative to its ground while
     *  grounded, to the world in the air. */
    Vector3 _velocity;
    /** The input's held requests in the last update (their presses start a jump or a use). */
    bool _wasJumpWanted;
    bool _wasUseWanted;
    /** A jump asked for and not yet started: how long it still waits. */
    bool _hasJumpRequest;
    float _jumpRequestSeconds;
    /** Whether it jumped since it last stood on the ground (no second jump in the coyote time). */
    bool _hasJumpedSinceGrounded;
    /** Seconds since its last jump (a held jump jumps again once the auto-jump delay has passed). */
    float _secondsSinceJump;
    /** Whether it is climbing a step: from a step up until it stands on walkable ground. Only then does the edge of a
     *  step whose contact is steeper than its slope limit hold it; walking off an edge, it falls instead. */
    bool _isClimbing;
    /** The ground object's world matrix at the last update, for carrying the character with it. */
    bool _hasGroundMatrix;
    uint64_t _groundMatrixObjectId;
    Matrix _groundMatrix;
    /** Where the update left the feet (the collision step's pushes show as the difference next tick). */
    bool _hasLastPosition;
    Vector3 _lastPosition;
    /** Warnings given once per character. */
    bool _hasWarnedScale;
    bool _hasWarnedCollider;
} WorldCharacterRuntime;

/**
 * @brief The character component. Underscore-prefixed fields are read-only: use the accessors and setters (or the
 *        table).
 */
typedef struct WorldCharacterComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Owned name of the character asset (at most WORLD_CHARACTER_COMPONENT_NAME_MAX_LENGTH bytes), or NULL for
     *  none (the defaults apply). */
    unsigned char* _character;
    /** @brief Per setting: whether the placement's own value in _values replaces the character asset's. */
    bool _overrides[CharacterSetting_Count];
    /** @brief The placement's own values, used while their flags are on (kept while off). */
    CharacterSettings _values;
    /** @brief The character asset bound under the world's asset user (borrowed; NULL without one). */
    const CharacterAsset* _boundCharacter;
    /** @brief Owned copy of the name _boundCharacter was bound for (a rename rebinds). */
    unsigned char* _boundName;
    /** @brief What its driver wants (runtime, not saved). */
    CharacterInput _input;
    /** @brief What it is doing (runtime, not saved). */
    WorldCharacterState _state;
    /** @brief The system's bookkeeping (runtime). */
    WorldCharacterRuntime _runtime;
} WorldCharacterComponent;


// Fields.
/** @brief The facings' names ("movement", "look", "keep"), for scripts and tools. */
extern const PropertyEnumTable CHARACTER_FACING_ENUM;


// Functions: the input.
/**
 * @brief An input that wants nothing: no movement, no requests, looking along +Z, facing its movement.
 * @returns The input.
 */
CharacterInput CharacterInput_CreateDefault(void);

/**
 * @brief The direction an input looks along: a unit vector from its look yaw and pitch.
 * @param self The input; must not be NULL.
 * @returns The direction.
 */
Vector3 CharacterInput_GetLookDirection(const CharacterInput* self);

/**
 * @brief Points an input's look along a direction (its yaw and pitch; the pitch kept within CHARACTER_MAX_LOOK_PITCH).
 * @param self The input; must not be NULL.
 * @param direction The direction (any length but 0; a zero or non-finite direction changes nothing).
 */
void CharacterInput_SetLookDirection(CharacterInput* self, Vector3 direction);


// Functions: the type.
/**
 * @brief Returns the character component's registry entry (WorldComponentType_Character, "character"): a transform
 *        driver.
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldCharacterComponent_GetTypeInfo(void);

/**
 * @brief Returns the character component's property table ("character_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldCharacterComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component to a character component.
 * @param component The component, or NULL.
 * @returns The character component, or NULL when @p component is NULL or of another type.
 */
static inline WorldCharacterComponent* WorldCharacterComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Character))
        ? (WorldCharacterComponent*)component : NULL;
}

/**
 * @brief Upcasts a character component to its base.
 * @param self The character component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldCharacterComponent_AsComponent(WorldCharacterComponent* self)
{
    return &self->Base;
}


// Functions: the settings.
/**
 * @brief The character asset's name the component uses.
 * @param self The component; must not be NULL.
 * @returns The name (borrowed: valid until it changes), or NULL for none.
 */
static inline const unsigned char* WorldCharacterComponent_GetCharacter(const WorldCharacterComponent* self)
{
    return self->_character;
}

/**
 * @brief Names the character asset to use, and binds it (a name is loaded under the world's asset user).
 * @param self The component; must not be NULL.
 * @param character A valid asset name of at most WORLD_CHARACTER_COMPONENT_NAME_MAX_LENGTH bytes, or NULL for none.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for an invalid name.
 */
Error WorldCharacterComponent_SetCharacter(WorldCharacterComponent* self, const unsigned char* character);

/**
 * @brief Whether the placement overrides a setting with its own value.
 * @param self The component; must not be NULL.
 * @param setting The setting (a value outside the enum: false).
 * @returns true while its override flag is on.
 */
bool WorldCharacterComponent_IsOverridden(const WorldCharacterComponent* self, CharacterSetting setting);

/**
 * @brief Turns a setting's override flag on or off; the placement's own value is kept either way.
 * @param self The component; must not be NULL.
 * @param setting The setting.
 * @param isOverridden Whether the placement's value applies.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a value outside the
 *          enum.
 */
Error WorldCharacterComponent_SetOverridden(WorldCharacterComponent* self, CharacterSetting setting, bool isOverridden);

/**
 * @brief Overrides a setting: sets the placement's own value and turns its flag on, after checking the value against
 *        the setting's rules (nothing changes when it breaks them).
 *
 * The generic setter for every setting, so a script's "this NPC walks slower" is one call:
 * `WorldCharacterComponent_OverrideSetting(npc, CharacterSetting_WalkSpeed, &(PropertyValue){...})`. The value's type
 * is the setting's (CharacterSettings_GetDescriptor): Float, Angle (radians), Bool or EnumSet.
 * @param self The component; must not be NULL.
 * @param setting The setting.
 * @param value The value; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument or a value of another type;
 *          ErrorCode_ArgumentOutOfRange for a setting outside the enum or a value breaking its rules.
 */
Error WorldCharacterComponent_OverrideSetting(WorldCharacterComponent* self, CharacterSetting setting,
    const PropertyValue* value);

/**
 * @brief The settings that apply to a placement: the character asset's (or the defaults), with the placement's own
 *        value wherever its flag is on. A pure function.
 * @param self The component; must not be NULL.
 * @param characterSettings The character asset's settings, or NULL for the defaults (CharacterSettings_CreateDefault).
 * @returns The settings.
 */
CharacterSettings WorldCharacterComponent_ResolveSettings(const WorldCharacterComponent* self,
    const CharacterSettings* characterSettings);

/**
 * @brief The settings in effect now: WorldCharacterComponent_ResolveSettings with the bound character asset.
 * @param self The component; must not be NULL.
 * @returns The settings.
 */
CharacterSettings WorldCharacterComponent_GetSettings(const WorldCharacterComponent* self);

/**
 * @brief The character asset the component is bound to.
 * @param self The component; must not be NULL.
 * @returns The asset (borrowed: valid while the world holds it), or NULL without one.
 */
static inline const CharacterAsset* WorldCharacterComponent_GetBoundCharacter(const WorldCharacterComponent* self)
{
    return self->_boundCharacter;
}


// Functions: the input and the state.
/**
 * @brief Returns the character's input: what its driver last asked for.
 * @param self The component; must not be NULL.
 * @returns The input (borrowed: valid until the next set).
 */
static inline const CharacterInput* WorldCharacterComponent_GetInput(const WorldCharacterComponent* self)
{
    return &self->_input;
}

/**
 * @brief Sets the character's input, kept until the next set; the system acts on it at its next update.
 *
 * The move direction is shortened to length 1 when longer, the look pitch kept within CHARACTER_MAX_LOOK_PITCH and the
 * look yaw wrapped into (-pi, pi].
 * @param self The component; must not be NULL.
 * @param input The input; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; ErrorCode_ArgumentOutOfRange for a value that is not
 *          finite or a facing outside its enum (nothing changes).
 */
Error WorldCharacterComponent_SetInput(WorldCharacterComponent* self, const CharacterInput* input);

/**
 * @brief Returns what the character is doing.
 * @param self The component; must not be NULL.
 * @returns The state (borrowed: valid until the next update).
 */
static inline const WorldCharacterState* WorldCharacterComponent_GetState(const WorldCharacterComponent* self)
{
    return &self->_state;
}

/**
 * @brief Where the character's eyes are: its object's world position plus its eye height, straight up.
 * @param self The component; must not be NULL (and attached).
 * @returns The point, in world space.
 */
Vector3 WorldCharacterComponent_GetEyePosition(const WorldCharacterComponent* self);
