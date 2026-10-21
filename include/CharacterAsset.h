#pragma once
#include <stdbool.h>
#include <stdint.h>
// PropertyDescriptor.h for the settings' property table (and the PropertyValue the generic setters take).
#include "PropertyDescriptor.h"


/**
 * @file CharacterAsset.h
 * @brief The character asset: the defaults of a kind of character (a human, a dog, a pigeon), kept apart from the
 *        characters placed in a world.
 *
 * A character is an asset of the "character" type (JSON definitions in a resource pack's characters/ folder,
 * references/asset_structure.md; load one with AssetManager_LoadCharacter). It holds CharacterSettings: the body (an
 * upright capsule, its eye height and mass), how it walks, turns, crouches, jumps, falls and flies, and how far it
 * reaches. Every key of a definition is optional (the table's defaults apply), unknown keys are refused, and every
 * rule is checked when the definition is read.
 *
 * A placed object names its character in its character component (WorldCharacterComponent.h) and may override EVERY
 * setting with an explicit flag next to its own value (the owner's decision G24); WorldCharacterComponent_ResolveSettings
 * turns the two into the settings that apply. Changing the component's name swaps the kind of character and keeps the
 * object. Gravity is the world's (WorldEnvironment's gravity); a character multiplies it.
 *
 * ONE COPY OF THE RULES. The settings are described by one property table (CharacterSettings_GetPropertyTable), and the
 * character component's table is built from the same rows: a setting has one key, default, rule and description
 * wherever it is stored. Each setting has a CharacterSetting value, which the component's generic setters take.
 *
 * TWO RULES ACROSS SETTINGS, which a property table cannot hold: the height and the crouch height include the capsule's
 * caps, so each must be at least twice the radius, and the crouch height must not pass the height. A definition that
 * breaks them is refused; a placement whose overrides break them is resolved by CharacterSettings_GetBodyHeights.
 *
 * OWNERSHIP. The asset is pure data with no dependencies; the asset manager owns it. Never keep its pointer past your
 * asset user (AssetManager.md).
 */


// Macros.
/** @brief The default capsule radius, in metres. */
#define CHARACTER_DEFAULT_RADIUS 0.3f
/** @brief The default standing height, in metres (the capsule's whole height, caps included). */
#define CHARACTER_DEFAULT_HEIGHT 1.75f
/** @brief The default eye height above the feet while standing, in metres. */
#define CHARACTER_DEFAULT_EYE_HEIGHT 1.62f
/** @brief The default mass, in kilograms. */
#define CHARACTER_DEFAULT_MASS 75.0f
/** @brief The largest radius, height or reach a character may have, in metres (a bound for hostile content). */
#define CHARACTER_MAX_SIZE 100.0
/** @brief The largest speed a character setting may give, in metres per second. */
#define CHARACTER_MAX_SPEED 1000.0
/** @brief The largest acceleration a character setting may give, in metres per second squared. */
#define CHARACTER_MAX_ACCELERATION 10000.0
/** @brief The longest time a character setting may give, in seconds. */
#define CHARACTER_MAX_SECONDS 60.0
/** @brief The layers a character collides with unless told otherwise: the character layer's default mask (world, prop,
 *  character, vehicle, dynamic and trigger), as a constant (CollisionLayer.h's bit per layer). */
#define CHARACTER_DEFAULT_COLLIDES_WITH ((uint32_t)0x3FU)
/** @brief The layers a character's interaction ray sees unless told otherwise: every layer but trigger (world, prop,
 *  character, vehicle, dynamic and interaction). */
#define CHARACTER_DEFAULT_INTERACTION_LAYERS ((uint32_t)0x5FU)


// Types.
/**
 * @brief One setting of a character: the index of its override flag on the component and of its row in the settings
 *        table. Values follow the table's order; they are not saved (the keys are), so they may change between builds.
 */
typedef enum CharacterSettingEnum
{
    /** @brief radius: the capsule's radius, metres. */
    CharacterSetting_Radius,
    /** @brief height: the standing capsule's whole height, metres. */
    CharacterSetting_Height,
    /** @brief eye_height: the eyes above the feet while standing, metres. */
    CharacterSetting_EyeHeight,
    /** @brief mass: kilograms, for pushing (T27). */
    CharacterSetting_Mass,
    /** @brief is_pushable: whether others push it. */
    CharacterSetting_IsPushable,
    /** @brief collides_with: the layers it collides with. */
    CharacterSetting_CollidesWith,
    /** @brief walk_speed: metres per second. */
    CharacterSetting_WalkSpeed,
    /** @brief can_run: whether it may run. */
    CharacterSetting_CanRun,
    /** @brief run_speed: metres per second. */
    CharacterSetting_RunSpeed,
    /** @brief acceleration: speeding up on the ground, metres per second squared. */
    CharacterSetting_Acceleration,
    /** @brief deceleration: slowing down on the ground, metres per second squared. */
    CharacterSetting_Deceleration,
    /** @brief step_height: the highest step it climbs, metres. */
    CharacterSetting_StepHeight,
    /** @brief slope_limit: the steepest walkable ground, radians. */
    CharacterSetting_SlopeLimit,
    /** @brief ground_snap_distance: how far it follows the ground down without falling, metres. */
    CharacterSetting_GroundSnapDistance,
    /** @brief turn_speed: radians per second. */
    CharacterSetting_TurnSpeed,
    /** @brief is_turn_instant: whether it turns at once. */
    CharacterSetting_IsTurnInstant,
    /** @brief can_crouch: whether it may crouch. */
    CharacterSetting_CanCrouch,
    /** @brief crouch_height: the crouched capsule's whole height, metres. */
    CharacterSetting_CrouchHeight,
    /** @brief crouch_eye_height: the eyes above the feet while crouched, metres. */
    CharacterSetting_CrouchEyeHeight,
    /** @brief crouch_speed: metres per second. */
    CharacterSetting_CrouchSpeed,
    /** @brief crouch_transition_seconds: how long the eyes take to go down or up. */
    CharacterSetting_CrouchTransitionSeconds,
    /** @brief can_jump: whether it may jump. */
    CharacterSetting_CanJump,
    /** @brief jump_speed: the upward speed a jump starts with, metres per second. */
    CharacterSetting_JumpSpeed,
    /** @brief can_jump_while_crouched: whether a crouched character may jump. */
    CharacterSetting_CanJumpWhileCrouched,
    /** @brief coyote_seconds: how long after leaving the ground a jump still starts. */
    CharacterSetting_CoyoteSeconds,
    /** @brief jump_buffer_seconds: how long a jump asked for in the air waits for the ground. */
    CharacterSetting_JumpBufferSeconds,
    /** @brief gravity_multiplier: times the world's gravity. */
    CharacterSetting_GravityMultiplier,
    /** @brief air_acceleration: steering in the air, metres per second squared. */
    CharacterSetting_AirAcceleration,
    /** @brief max_fall_speed: metres per second. */
    CharacterSetting_MaxFallSpeed,
    /** @brief can_fly: whether it may fly. */
    CharacterSetting_CanFly,
    /** @brief fly_speed: metres per second. */
    CharacterSetting_FlySpeed,
    /** @brief fly_fast_speed: metres per second, while running. */
    CharacterSetting_FlyFastSpeed,
    /** @brief fly_acceleration: metres per second squared. */
    CharacterSetting_FlyAcceleration,
    /** @brief fly_deceleration: metres per second squared. */
    CharacterSetting_FlyDeceleration,
    /** @brief interaction_reach: how far it reaches, metres. */
    CharacterSetting_InteractionReach,
    /** @brief interaction_layers: the layers its interaction ray sees. */
    CharacterSetting_InteractionLayers,
    /** @brief The number of settings; not a setting. */
    CharacterSetting_Count
} CharacterSetting;

/**
 * @brief Everything a placed character can override: as a character asset sets it (the defaults of its placements), or
 *        as it applies to one placement (WorldCharacterComponent_ResolveSettings).
 *
 * A plain value type, described by CharacterSettings_GetPropertyTable (one row per CharacterSetting, in its order).
 * Lengths are metres, speeds metres per second, accelerations metres per second squared, angles radians (degrees in
 * JSON), times seconds.
 */
typedef struct CharacterSettingsStruct
{
    /** @brief The capsule's radius (above 0, at most CHARACTER_MAX_SIZE). */
    float Radius;
    /** @brief The standing capsule's whole height, caps included (above 0, at most CHARACTER_MAX_SIZE; at least twice
     *  the radius in a character asset). The object's position is at its feet. */
    float Height;
    /** @brief The eyes above the feet while standing (at least 0): the camera's height, the interaction ray's start. */
    float EyeHeight;
    /** @brief The mass in kilograms (above 0, at most COLLISION_MAX_MASS): how pushes are shared (T27). */
    float Mass;
    /** @brief Whether other characters, movers and pushables push it (its collider is then pushable); off, it is
     *  kinematic: it pushes and is never pushed. */
    bool IsPushable;
    /** @brief The layers it collides with (CollisionLayer.h's bit per layer). */
    uint32_t CollidesWith;
    /** @brief The walking speed (at least 0). */
    float WalkSpeed;
    /** @brief Whether running is allowed (the input's run request is ignored otherwise). */
    bool CanRun;
    /** @brief The running speed (at least 0). */
    float RunSpeed;
    /** @brief How fast it speeds up toward the wished speed on the ground (above 0). */
    float Acceleration;
    /** @brief How fast it slows down on the ground: without input, or toward a lower or opposite wished velocity (above
     *  0). */
    float Deceleration;
    /** @brief The highest step it climbs while walking (at least 0). */
    float StepHeight;
    /** @brief The steepest ground it walks on, in [0, pi/2): steeper ground is a wall while walking and slides it down. */
    float SlopeLimit;
    /** @brief How far down it follows the ground (slopes, stairs) without leaving it (at least 0). */
    float GroundSnapDistance;
    /** @brief How fast its body turns toward its facing target, radians per second (above 0). */
    float TurnSpeed;
    /** @brief Whether its body turns at once instead (the turn speed is then unused). */
    bool IsTurnInstant;
    /** @brief Whether it may crouch. */
    bool CanCrouch;
    /** @brief The crouched capsule's whole height (above 0; in a character asset at least twice the radius and at most
     *  the height). */
    float CrouchHeight;
    /** @brief The eyes above the feet while crouched (at least 0). */
    float CrouchEyeHeight;
    /** @brief The speed while crouched (at least 0; running does not change it). */
    float CrouchSpeed;
    /** @brief How long the eyes take to go down or up between the two eye heights (at least 0: 0 at once). The capsule
     *  changes at once. */
    float CrouchTransitionSeconds;
    /** @brief Whether it may jump. */
    bool CanJump;
    /** @brief The upward speed a jump starts with (at least 0): the jump's height is its square over twice the gravity. */
    float JumpSpeed;
    /** @brief Whether a crouched character may jump. */
    bool CanJumpWhileCrouched;
    /** @brief How long after walking off the ground a jump still starts (at least 0). */
    float CoyoteSeconds;
    /** @brief How long a jump asked for just before landing waits for the ground (at least 0). */
    float JumpBufferSeconds;
    /** @brief Times the world's gravity while walking or falling (at least 0); flying has none. */
    float GravityMultiplier;
    /** @brief How fast it steers toward the wished velocity in the air (at least 0: no air control). */
    float AirAcceleration;
    /** @brief The fastest it falls (above 0). */
    float MaxFallSpeed;
    /** @brief Whether it may fly. */
    bool CanFly;
    /** @brief The flying speed (at least 0). */
    float FlySpeed;
    /** @brief The flying speed while running (at least 0). */
    float FlyFastSpeed;
    /** @brief How fast it speeds up while flying (above 0). */
    float FlyAcceleration;
    /** @brief How fast it slows down while flying (above 0). */
    float FlyDeceleration;
    /** @brief How far its interaction ray reaches from its eyes (above 0, at most CHARACTER_MAX_SIZE). */
    float InteractionReach;
    /** @brief The layers its interaction ray sees (CollisionLayer.h's bit per layer); triggers are never seen. */
    uint32_t InteractionLayers;
} CharacterSettings;

/**
 * @brief A loaded character asset. Built and owned by the asset manager (the "character" type); read-only.
 */
typedef struct CharacterAssetStruct
{
    /** @brief The defaults of its placements. */
    CharacterSettings _settings;
} CharacterAsset;


// Functions.
/**
 * @brief Returns the property table of a character's settings ("character"): the keys of its definition, one row per
 *        CharacterSetting in its order, describing every field of CharacterSettings.
 * @returns The table (static, immutable).
 */
const PropertyTable* CharacterSettings_GetPropertyTable(void);

/**
 * @brief Returns a setting's row in the settings table.
 * @param setting The setting.
 * @returns The descriptor (static), or NULL for a value that is not a setting.
 */
const PropertyDescriptor* CharacterSettings_GetDescriptor(CharacterSetting setting);

/**
 * @brief The settings a character asset and a placement start from: the table's defaults (a human: 0.3 m by 1.75 m,
 *        75 kg, walking at 1.4 m/s, crouching, not jumping, not flying).
 * @returns The settings.
 */
CharacterSettings CharacterSettings_CreateDefault(void);

/**
 * @brief Checks the rules across settings that the table cannot hold: the height and the crouch height at least twice
 *        the radius, the crouch height at most the height.
 * @param self The settings; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange naming the broken rule.
 */
Error CharacterSettings_CheckBody(const CharacterSettings* self);

/**
 * @brief The capsule heights settings give, made consistent: the height at least twice the radius, the crouch height
 *        between twice the radius and the height. A character asset holds them already; a placement's overrides may
 *        not, and then these are what apply.
 * @param self The settings; must not be NULL.
 * @param outHeight [out] The standing height. Must not be NULL.
 * @param outCrouchHeight [out] The crouched height. Must not be NULL.
 */
void CharacterSettings_GetBodyHeights(const CharacterSettings* self, float* outHeight, float* outCrouchHeight);

/**
 * @brief Returns the defaults of the character's placements.
 * @param self The character; must not be NULL.
 * @returns The settings (borrowed: valid while the character is loaded).
 */
static inline const CharacterSettings* CharacterAsset_GetSettings(const CharacterAsset* self)
{
    return &self->_settings;
}
