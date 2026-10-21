#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "GameCamera.h"
#include "GameInput.h"
#include "WorldCollision.h"

// Forward declarations (World.h, WorldCharacters.h).
typedef struct WorldStruct World;
typedef struct WorldCharactersStruct WorldCharacters;


/**
 * @file PlayerController.h
 * @brief The player's side of a character (T13): turns the tick's GameInput into the character's CharacterInput, puts
 *        the camera at the character's eyes, and keeps what the player looks at.
 *
 * The character itself (WorldCharacterComponent.h) does not know who drives it: an NPC's brain fills the same
 * CharacterInput. The controller reads ACTIONS (rebindable): move_forward / move_back / move_left / move_right relative
 * to where the player looks, sprint to run, jump, crouch (held, or toggled with IsCrouchToggled), use; while flying the
 * look direction is forward, and move_up / move_down climb and sink. The mouse turns the look (yaw and pitch), and the
 * body faces the look. A turn the ground gives the character (a turning platform) turns the view too, since the look is
 * read back from the character every tick.
 *
 * Call PlayerController_Update in a frame's Update, after the UI had first refusal and before the characters update;
 * PlayerController_UpdateLookTarget after the tick's collision step; PlayerController_ApplyCamera last, so the camera
 * shows where the character ended up. Input is read only through GameInput, only in Update (AGENTS.md rule 9).
 */


// Macros.
/** @brief The default mouse sensitivity: radians the look turns per pixel the mouse moves. */
#define PLAYER_CONTROLLER_DEFAULT_MOUSE_SENSITIVITY 0.0025f
/** @brief The default head bob: how far the camera bobs up and down while walking, metres. */
#define PLAYER_CONTROLLER_DEFAULT_HEAD_BOB_AMPLITUDE 0.03f
/** @brief The default head bob: metres walked per bob, one bob a step (two a stride). */
#define PLAYER_CONTROLLER_DEFAULT_HEAD_BOB_STEP_LENGTH 0.7f


// Types.
/**
 * @brief The player controller. Public fields are the player's options; underscore-prefixed ones are read-only.
 */
typedef struct PlayerControllerStruct
{
    /** @brief Radians the look turns per pixel of mouse movement (default PLAYER_CONTROLLER_DEFAULT_MOUSE_SENSITIVITY). */
    float MouseSensitivity;
    /** @brief Whether the crouch action toggles crouching instead of crouching while held (default false). */
    bool IsCrouchToggled;
    /** @brief Whether the camera bobs while walking (default false). */
    bool IsHeadBobEnabled;
    /** @brief How far the camera bobs, metres (default PLAYER_CONTROLLER_DEFAULT_HEAD_BOB_AMPLITUDE). */
    float HeadBobAmplitude;
    /** @brief Metres walked per bob (default PLAYER_CONTROLLER_DEFAULT_HEAD_BOB_STEP_LENGTH; above 0). */
    float HeadBobStepLength;
    /** @brief Whether the player wants to fly (the character flies only when its settings allow it). */
    bool WantsToFly;
    /** @brief The character's object (0: none). */
    uint64_t _characterId;
    /** @brief Whether toggled crouching is on. */
    bool _isCrouchLatched;
    /** @brief The head bob's phase, radians, and how much of it shows (eased toward 1 while walking, 0 standing). */
    float _headBobPhase;
    float _headBobWeight;
    /** @brief What the player looks at, valid while _hasLookTarget. */
    bool _hasLookTarget;
    WorldRayHit _lookTarget;
} PlayerController;


// Functions.
/**
 * @brief Constructs a controller with the default options, controlling no character.
 * @param self The controller; must not be NULL.
 */
void PlayerController_Construct(PlayerController* self);

/**
 * @brief Deconstructs a controller (it owns nothing).
 * @param self The controller, or NULL.
 */
void PlayerController_Deconstruct(PlayerController* self);

/**
 * @brief Takes control of a character, its look starting at @p lookYaw and @p lookPitch (where the camera looked), or
 *        lets go (an id of 0). The character let go of (if any, and still there) is left idle: no movement and no
 *        requests, looking where it looked, keeping its facing.
 * @param self The controller; must not be NULL.
 * @param world The character's world; must not be NULL.
 * @param characterId The character's object, or 0 for none.
 * @param lookYaw The look's yaw, radians.
 * @param lookPitch The look's pitch, radians (kept within CHARACTER_MAX_LOOK_PITCH).
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument or an object without a character component (the
 *          controller then controls none).
 */
Error PlayerController_SetCharacter(PlayerController* self, World* world, uint64_t characterId, float lookYaw,
    float lookPitch);

/**
 * @brief Returns the character the controller drives.
 * @param self The controller; must not be NULL.
 * @returns Its object's id, or 0 for none.
 */
static inline uint64_t PlayerController_GetCharacter(const PlayerController* self)
{
    return self->_characterId;
}

/**
 * @brief Reads the tick's input into the character's input (see the file comment). Consumes the use action's keys when
 *        it is pressed, so nothing below acts on the same press. Does nothing without a character (or when it is gone).
 * @param self The controller; must not be NULL.
 * @param input The tick's input; must not be NULL.
 * @param world The character's world; must not be NULL.
 * @param isMouseLookActive Whether the mouse turns the look (the cursor is captured).
 */
void PlayerController_Update(PlayerController* self, GameInput* input, World* world, bool isMouseLookActive);

/**
 * @brief Finds what the character looks at now (WorldCharacters_FindLookTarget), after the tick's collision step.
 * @param self The controller; must not be NULL.
 * @param characters The world's character system; must not be NULL.
 * @returns true when the object looked at changed (another object, or none instead of one): worth a log line.
 */
bool PlayerController_UpdateLookTarget(PlayerController* self, WorldCharacters* characters);

/**
 * @brief What the character looked at in the last PlayerController_UpdateLookTarget.
 * @param self The controller; must not be NULL.
 * @param outHit [out] Receives the hit when there is one. Must not be NULL.
 * @returns true when it looks at something.
 */
bool PlayerController_GetLookTarget(const PlayerController* self, WorldRayHit* outHit);

/**
 * @brief Puts the camera at the character's eyes, looking where it looks, with the head bob if enabled (it advances by
 *        how far the character walked on the ground).
 * @param self The controller; must not be NULL.
 * @param world The character's world; must not be NULL.
 * @param camera The camera; must not be NULL.
 * @param deltaSeconds The tick's length, seconds.
 */
void PlayerController_ApplyCamera(PlayerController* self, World* world, GameCamera* camera, float deltaSeconds);
