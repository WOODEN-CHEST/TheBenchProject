#include <math.h>
#include "PlayerController.h"
#include "World.h"
#include "WorldCharacterComponent.h"
#include "WorldCharacters.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** A full turn, radians. */
#define PLAYER_FULL_TURN (2.0f * PI)
/** How fast the head bob fades in and out, per second. */
#define PLAYER_HEAD_BOB_EASE_RATE 8.0f
/** Below this speed on the ground the head does not bob, metres per second. */
#define PLAYER_HEAD_BOB_MIN_SPEED 0.05f


// Static functions.
static WorldCharacterComponent* FindCharacter(World* world, uint64_t id)
{
    WorldComponent* Component = NULL;
    if ((id == 0U) || !World_TryGetComponent(world, id, WorldComponentType_Character, &Component))
    {
        return NULL;
    }
    return WorldCharacterComponent_FromComponent(Component);
}

/* +1 while @p positive is held, -1 while @p negative is, 0 for both or neither. */
static float GetAxis(const GameInput* input, GameAction positive, GameAction negative)
{
    float Value = 0.0f;
    if (GameInput_IsActionDown(input, positive))
    {
        Value += 1.0f;
    }
    if (GameInput_IsActionDown(input, negative))
    {
        Value -= 1.0f;
    }
    return Value;
}


// Public functions.
void PlayerController_Construct(PlayerController* self)
{
    Memory_Zero(self, sizeof(*self));
    self->MouseSensitivity = PLAYER_CONTROLLER_DEFAULT_MOUSE_SENSITIVITY;
    self->IsCrouchToggled = false;
    self->IsHeadBobEnabled = false;
    self->HeadBobAmplitude = PLAYER_CONTROLLER_DEFAULT_HEAD_BOB_AMPLITUDE;
    self->HeadBobStepLength = PLAYER_CONTROLLER_DEFAULT_HEAD_BOB_STEP_LENGTH;
}

void PlayerController_Deconstruct(PlayerController* self)
{
    if (self != NULL)
    {
        Memory_Zero(self, sizeof(*self));
    }
}

Error PlayerController_SetCharacter(PlayerController* self, World* world, uint64_t characterId, float lookYaw,
    float lookPitch)
{
    if ((self == NULL) || (world == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "PlayerController_SetCharacter: self and world must not be NULL.");
    }
    // The character let go of stops doing what the player asked (it stands still, stands up, stops flying), looking where
    // it looked: nobody drives it now.
    WorldCharacterComponent* Released = FindCharacter(world, self->_characterId);
    if (Released != NULL)
    {
        CharacterInput Idle = CharacterInput_CreateDefault();
        Idle.LookYaw = WorldCharacterComponent_GetInput(Released)->LookYaw;
        Idle.LookPitch = WorldCharacterComponent_GetInput(Released)->LookPitch;
        Idle.Facing = CharacterFacing_Keep;
        Error IdleResult = WorldCharacterComponent_SetInput(Released, &Idle);
        Error_Deconstruct(&IdleResult);
    }
    self->_characterId = 0U;
    self->_isCrouchLatched = false;
    self->_hasLookTarget = false;
    self->_headBobPhase = 0.0f;
    self->_headBobWeight = 0.0f;
    if (characterId == 0U)
    {
        return Error_CreateSuccess();
    }
    WorldCharacterComponent* Character = FindCharacter(world, characterId);
    if (Character == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "PlayerController_SetCharacter: the object has no character component.");
    }
    CharacterInput Input = *WorldCharacterComponent_GetInput(Character);
    Input.LookYaw = lookYaw;
    Input.LookPitch = lookPitch;
    Input.Facing = CharacterFacing_Look;
    Error Result = WorldCharacterComponent_SetInput(Character, &Input);
    if (Result.Code == ErrorCode_Success)
    {
        self->_characterId = characterId;
    }
    return Result;
}

void PlayerController_Update(PlayerController* self, GameInput* input, World* world, bool isMouseLookActive)
{
    WorldCharacterComponent* Character = FindCharacter(world, self->_characterId);
    if (Character == NULL)
    {
        return;
    }
    // The look as the character has it (a turning platform may have turned it), then the mouse.
    CharacterInput Input = *WorldCharacterComponent_GetInput(Character);
    if (isMouseLookActive)
    {
        // Mouse-right must turn the view right: increasing yaw turns toward +X, which renders on the screen's left, so
        // a rightward movement lowers the yaw (as the flycam).
        Vector2 MouseDelta = GameInput_GetMouseDelta(input);
        Input.LookYaw -= MouseDelta.x * self->MouseSensitivity;
        Input.LookPitch = Clamp(Input.LookPitch - (MouseDelta.y * self->MouseSensitivity), -CHARACTER_MAX_LOOK_PITCH,
            CHARACTER_MAX_LOOK_PITCH);
    }

    bool IsFlying = WorldCharacterComponent_GetState(Character)->Mode == CharacterMovementMode_Flying;
    Vector3 Forward = IsFlying ? CharacterInput_GetLookDirection(&Input)
        : (Vector3){ sinf(Input.LookYaw), 0.0f, cosf(Input.LookYaw) };
    Vector3 Right = { -cosf(Input.LookYaw), 0.0f, sinf(Input.LookYaw) };
    Vector3 Move = Vector3Add(Vector3Scale(Forward, GetAxis(input, GameAction_MoveForward, GameAction_MoveBack)),
        Vector3Scale(Right, GetAxis(input, GameAction_MoveRight, GameAction_MoveLeft)));
    if (IsFlying)
    {
        Move.y += GetAxis(input, GameAction_MoveUp, GameAction_MoveDown);
    }
    float Length = Vector3Length(Move);
    Input.MoveDirection = (Length > 1e-6f) ? Vector3Scale(Move, 1.0f / Length) : (Vector3){ 0.0f, 0.0f, 0.0f };
    Input.IsRunning = GameInput_IsActionDown(input, GameAction_Sprint);
    Input.WantsJump = !IsFlying && GameInput_IsActionDown(input, GameAction_Jump);

    if (self->IsCrouchToggled)
    {
        if (!IsFlying && GameInput_IsActionPressed(input, GameAction_Crouch))
        {
            self->_isCrouchLatched = !self->_isCrouchLatched;
        }
        Input.WantsCrouch = self->_isCrouchLatched;
    }
    else
    {
        Input.WantsCrouch = !IsFlying && GameInput_IsActionDown(input, GameAction_Crouch);
    }

    // Use: held while down; its press is the character's to act on, and nothing below sees it.
    Input.WantsUse = GameInput_IsActionDown(input, GameAction_Use);
    if (GameInput_IsActionPressed(input, GameAction_Use))
    {
        GameInput_ConsumeAction(input, GameAction_Use);
    }
    Input.WantsToFly = self->WantsToFly;
    Input.Facing = CharacterFacing_Look;
    Error Result = WorldCharacterComponent_SetInput(Character, &Input);
    // Every value above is finite and in range, so this cannot fail; release the error regardless.
    Error_Deconstruct(&Result);
}

bool PlayerController_UpdateLookTarget(PlayerController* self, WorldCharacters* characters)
{
    WorldRayHit Hit;
    bool HasTarget = (self->_characterId != 0U)
        && WorldCharacters_FindLookTarget(characters, self->_characterId, &Hit);
    uint64_t Before = self->_hasLookTarget ? self->_lookTarget.Shape.ObjectId : 0U;
    uint64_t After = HasTarget ? Hit.Shape.ObjectId : 0U;
    self->_hasLookTarget = HasTarget;
    if (HasTarget)
    {
        self->_lookTarget = Hit;
    }
    return Before != After;
}

bool PlayerController_GetLookTarget(const PlayerController* self, WorldRayHit* outHit)
{
    if (self->_hasLookTarget)
    {
        *outHit = self->_lookTarget;
    }
    return self->_hasLookTarget;
}

void PlayerController_ApplyCamera(PlayerController* self, World* world, GameCamera* camera, float deltaSeconds)
{
    WorldCharacterComponent* Character = FindCharacter(world, self->_characterId);
    if (Character == NULL)
    {
        return;
    }
    const CharacterInput* Input = WorldCharacterComponent_GetInput(Character);
    const WorldCharacterState* State = WorldCharacterComponent_GetState(Character);
    Vector3 Eye = WorldCharacterComponent_GetEyePosition(Character);

    // TODO: rework the head bob (the owner, 2026-10-22: it reads badly). It is a plain up-and-down sine a step long,
    // one amplitude at every speed: no sway from side to side, no dip as a foot lands, and it fades out on every short
    // fall (each stair step now). Tie it to the footfalls once characters have animated models (T29, T30).
    float Bob = 0.0f;
    if (self->IsHeadBobEnabled && (self->HeadBobStepLength > 0.0f))
    {
        // One bob a step while walking on the ground, fading in and out rather than stopping mid-bob.
        float Speed = sqrtf((State->Velocity.x * State->Velocity.x) + (State->Velocity.z * State->Velocity.z));
        bool IsWalking = State->IsGrounded && (Speed > PLAYER_HEAD_BOB_MIN_SPEED);
        if (IsWalking)
        {
            self->_headBobPhase = fmodf(self->_headBobPhase
                + ((PLAYER_FULL_TURN * Speed * deltaSeconds) / self->HeadBobStepLength), PLAYER_FULL_TURN);
        }
        float Target = IsWalking ? 1.0f : 0.0f;
        float Ease = Clamp(PLAYER_HEAD_BOB_EASE_RATE * deltaSeconds, 0.0f, 1.0f);
        self->_headBobWeight += (Target - self->_headBobWeight) * Ease;
        Bob = self->HeadBobAmplitude * self->_headBobWeight * sinf(self->_headBobPhase);
    }

    // TODO: T33 gives characters double eyes; this widens the float eye until then.
    camera->Position = Vector3Double_Create((double)Eye.x, (double)Eye.y + (double)Bob, (double)Eye.z);
    camera->Yaw = Input->LookYaw;
    camera->Pitch = Input->LookPitch;
}
