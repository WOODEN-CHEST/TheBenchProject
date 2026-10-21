#include <inttypes.h>
#include <math.h>
#include "WorldCharacters.h"
#include "WorldCharacterComponent.h"
#include "WorldCharacterInternal.h"
#include "WorldColliderComponent.h"
#include "WorldEnvironment.h"
#include "WorldObject.h"
#include "WorldTransform.h"
#include "World.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** The longest tick the system steps, seconds (a hitch must not throw characters through the world in one move). */
#define CHARACTER_MAX_DELTA_SECONDS 0.1f
/** Below this horizontal speed a character facing its movement keeps its facing, metres per second. */
#define CHARACTER_FACING_MIN_SPEED 0.05f
/** A push (the collision step moving the character) shorter than this is rounding, metres. */
#define CHARACTER_PUSH_EPSILON 1e-5f
/** The buffers' first capacities. */
#define INITIAL_EVENT_CAPACITY ((size_t)16)
#define INITIAL_ID_CAPACITY ((size_t)8)
/** A full turn, for wrapping yaws. */
#define CHARACTER_FULL_TURN (2.0f * PI)


// Types.
struct WorldCharactersStruct
{
    /** The world, its collision world and the logger (borrowed). */
    World* _world;
    WorldCollision* _collision;
    Logger* _logger;
    /** WorldCharacterEvent elements, until cleared. */
    GenericBuffer _events;
    /** uint64_t ids: the scratch of the room-to-stand checks. */
    GenericBuffer _scratchIds;
    /** Updates run. */
    uint64_t _tick;
};

/* One character's update: what every step of it reads. */
typedef struct CharacterStepStruct
{
    WorldCharacters* System;
    WorldCharacterComponent* Character;
    WorldObject* Object;
    uint64_t Id;
    CharacterSettings Settings;
    float StandingHeight;
    float CrouchHeight;
    CharacterBody Body;
    float DeltaSeconds;
    float Gravity;
} CharacterStep;


// Static functions: helpers.
static const Vector3 WorldUp = { 0.0f, 1.0f, 0.0f };

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

/* @p current moved toward @p target by at most @p maxStep. */
static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxStep)
{
    Vector3 Difference = Vector3Subtract(target, current);
    float Distance = Vector3Length(Difference);
    if ((Distance <= maxStep) || (Distance < 1e-9f))
    {
        return target;
    }
    return Vector3Add(current, Vector3Scale(Difference, maxStep / Distance));
}

/* Speeds up toward @p wish at @p acceleration, or slows down at @p deceleration when the change works against the
 * velocity (stopping, turning, reversing). */
static Vector3 Approach(Vector3 velocity, Vector3 wish, float acceleration, float deceleration, float deltaSeconds)
{
    Vector3 Change = Vector3Subtract(wish, velocity);
    float Rate = (Vector3DotProduct(Change, velocity) < 0.0f) ? deceleration : acceleration;
    return MoveTowards(velocity, wish, Rate * deltaSeconds);
}

static bool AreMatricesEqual(const Matrix* a, const Matrix* b)
{
    return Memory_IsEqual(a, b, sizeof(Matrix));
}

/* Exact equality (raymath's Vector3Equals allows a relative error, which would skip slow moves far from the origin). */
static bool AreVectorsEqual(Vector3 a, Vector3 b)
{
    return (a.x == b.x) && (a.y == b.y) && (a.z == b.z);
}

static void QueueEvent(WorldCharacters* self, WorldCharacterEventKind kind, uint64_t characterId, uint64_t otherId,
    float value)
{
    WorldCharacterEvent Event = { .Kind = kind, .CharacterId = characterId, .OtherId = otherId, .Value = value,
        .Tick = self->_tick };
    // The buffer grows (running out of memory aborts in Memory_Allocate), so adding cannot fail.
    (void)GenericBuffer_AddLast(&self->_events, &Event);
}

/* Logs a warning about one character once (the caller keeps the flag). */
static void WarnOnce(WorldCharacters* self, bool* hasWarned, uint64_t id, const char* problem)
{
    if (*hasWarned || (self->_logger == NULL))
    {
        return;
    }
    *hasWarned = true;
    Error LogResult = Logger_LogWarningFormatted(self->_logger,
        (const unsigned char*)u8"Characters: object %" PRIu64 u8": %s", id, problem);
    Error_Deconstruct(&LogResult);
}


// Static functions: settings and the collider.
/* The settings in effect, resolved again only when the component changed or its character asset was rebound. */
static const CharacterSettings* GetSettings(WorldCharacterComponent* character)
{
    WorldCharacterRuntime* Runtime = &character->_runtime;
    uint32_t Revision = WorldComponent_GetRevision(&character->Base);
    const CharacterAsset* Asset = WorldCharacterComponent_GetBoundCharacter(character);
    if (!Runtime->_hasSettings || (Runtime->_settingsRevision != Revision) || (Runtime->_settingsAsset != Asset))
    {
        Runtime->_settings = WorldCharacterComponent_GetSettings(character);
        Runtime->_settingsRevision = Revision;
        Runtime->_settingsAsset = Asset;
        Runtime->_hasSettings = true;
    }
    return &Runtime->_settings;
}

/* The filters of a character's moves: static and kinematic colliders (or every collider, with pushables) on its mask
 * that collide with characters, without triggers, itself or what it carries. */
static WorldCollisionFilter CreateFilter(const CharacterSettings* settings, uint64_t id, bool arePushablesIgnored)
{
    uint32_t Mask = settings->CollidesWith & ~COLLISION_LAYER_BIT(CollisionLayer_Trigger);
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(Mask, WorldCollisionTriggerMode_Ignore);
    Filter.IgnoredObjectId = id;
    Filter.IsIgnoredSubtree = true;
    Filter.ArePushablesIgnored = arePushablesIgnored;
    Filter.HasOwnLayer = true;
    Filter.OwnLayer = CollisionLayer_Character;
    return Filter;
}

static void BuildBody(CharacterStep* step)
{
    step->Body.Collision = step->System->_collision;
    step->Body.MoveFilter = CreateFilter(&step->Settings, step->Id, true);
    step->Body.GroundFilter = CreateFilter(&step->Settings, step->Id, false);
    step->Body.Radius = step->Settings.Radius;
    step->Body.Height = step->Character->_state.Height;
    step->Body.WalkableNormalY = cosf(step->Settings.SlopeLimit);
    step->Body.StepHeight = step->Settings.StepHeight;
}

/* The capsule a character's collider should hold: upright, its feet at the object's origin. */
static CollisionShape CreateCapsule(float radius, float height)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Capsule);
    Shape.Center = (Vector3){ 0.0f, 0.5f * height, 0.0f };
    Shape.Rotation = (Vector3){ 0.0f, 0.0f, 0.0f };
    Shape.Radius = radius;
    Shape.HalfHeight = Math_MaxFloat(0.0f, (0.5f * height) - radius);
    return Shape;
}

static bool AreShapesEqual(const CollisionShape* a, const CollisionShape* b)
{
    return (a->Type == b->Type) && AreVectorsEqual(a->Center, b->Center) && AreVectorsEqual(a->Rotation, b->Rotation)
        && (a->Radius == b->Radius) && (a->HalfHeight == b->HalfHeight);
}

/* Records the first failed collider write of a character (they cannot fail for the values written here). */
static void KeepFirstError(Error* first, Error result)
{
    if ((first->Code == ErrorCode_Success) && (result.Code != ErrorCode_Success))
    {
        *first = result;
        return;
    }
    Error_Deconstruct(&result);
}

/* Writes the rows a character's collider must hold, each only when it differs (every write advances the collider's
 * revision, which makes the collision world rebuild it). */
static Error SyncCollider(WorldColliderComponent* collider, const CharacterSettings* settings, float height)
{
    Error First = Error_CreateSuccess();
    if (!WorldColliderComponent_IsEnabled(collider))
    {
        KeepFirstError(&First, WorldColliderComponent_SetEnabled(collider, true));
    }
    if (!WorldColliderComponent_UsesOwnShape(collider))
    {
        KeepFirstError(&First, WorldColliderComponent_SetUsesOwnShape(collider, true));
    }
    CollisionShape Wanted = CreateCapsule(settings->Radius, height);
    CollisionShape Current = WorldColliderComponent_GetOwnShape(collider);
    if (!AreShapesEqual(&Wanted, &Current))
    {
        KeepFirstError(&First, WorldColliderComponent_SetOwnShape(collider, &Wanted));
    }
    if (!WorldColliderComponent_IsOverridden(collider, WorldColliderSetting_Layer)
        || (WorldColliderComponent_GetLayer(collider) != CollisionLayer_Character))
    {
        KeepFirstError(&First, WorldColliderComponent_SetLayer(collider, CollisionLayer_Character));
        KeepFirstError(&First, WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_Layer, true));
    }
    if (!WorldColliderComponent_IsOverridden(collider, WorldColliderSetting_CollidesWith)
        || (WorldColliderComponent_GetCollidesWith(collider) != settings->CollidesWith))
    {
        KeepFirstError(&First, WorldColliderComponent_SetCollidesWith(collider, settings->CollidesWith));
        KeepFirstError(&First, WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_CollidesWith, true));
    }
    if (!WorldColliderComponent_IsOverridden(collider, WorldColliderSetting_IsTrigger)
        || WorldColliderComponent_IsTrigger(collider))
    {
        KeepFirstError(&First, WorldColliderComponent_SetIsTrigger(collider, false));
        KeepFirstError(&First, WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_IsTrigger, true));
    }
    Vector3 One = { 1.0f, 1.0f, 1.0f };
    Vector3 Zero = { 0.0f, 0.0f, 0.0f };
    if (!AreVectorsEqual(WorldColliderComponent_GetOffset(collider), Zero)
        || !AreVectorsEqual(WorldColliderComponent_GetRotation(collider), Zero)
        || !AreVectorsEqual(WorldColliderComponent_GetScale(collider), One))
    {
        KeepFirstError(&First, WorldColliderComponent_SetLocalTransform(collider, Zero, Zero, One));
    }
    if (WorldColliderComponent_GetRotationMode(collider) != WorldColliderRotationMode_YawOnly)
    {
        KeepFirstError(&First, WorldColliderComponent_SetRotationMode(collider, WorldColliderRotationMode_YawOnly));
    }
    if (WorldColliderComponent_IsPushable(collider) != settings->IsPushable)
    {
        KeepFirstError(&First, WorldColliderComponent_SetPushable(collider, settings->IsPushable));
    }
    if (!WorldColliderComponent_IsOverridden(collider, WorldColliderSetting_Mass)
        || (WorldColliderComponent_GetMass(collider) != settings->Mass))
    {
        KeepFirstError(&First, WorldColliderComponent_SetMass(collider, settings->Mass));
        KeepFirstError(&First, WorldColliderComponent_SetOverridden(collider, WorldColliderSetting_Mass, true));
    }
    if (WorldColliderComponent_GetWhenBlocked(collider) != WorldColliderBlockedMode_Stop)
    {
        KeepFirstError(&First, WorldColliderComponent_SetWhenBlocked(collider, WorldColliderBlockedMode_Stop));
    }
    if (!WorldColliderComponent_IsContinuous(collider))
    {
        KeepFirstError(&First, WorldColliderComponent_SetContinuous(collider, true));
    }
    return First;
}

/* Gives the character's object a collider when it has none, and keeps it in step with the character. */
static void EnsureCollider(CharacterStep* step)
{
    WorldCharacterRuntime* Runtime = &step->Character->_runtime;
    WorldComponent* Component = WorldObject_GetComponent(step->Object, WorldComponentType_Collider);
    if (Component == NULL)
    {
        Error AddResult = World_AddComponent(step->System->_world, step->Id, WorldComponentType_Collider, &Component);
        if (AddResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&AddResult);
            WarnOnce(step->System, &Runtime->_hasWarnedCollider, step->Id,
                "its collider could not be added; it moves without one, and nothing collides with it.");
            return;
        }
    }
    WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
    Error SyncResult = SyncCollider(Collider, &step->Settings, step->Character->_state.Height);
    if (SyncResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&SyncResult);
        WarnOnce(step->System, &Runtime->_hasWarnedCollider, step->Id,
            "its collider refused the character's shape or settings.");
    }
}


// Static functions: the transform.
/* Writes the character's placement (feet and yaw, upright, scale 1) into its object, only what changed: directly for a
 * root, through the parent's inverse for a child. */
static void WriteTransform(CharacterStep* step, Vector3 feet, float yaw)
{
    WorldObject* Object = step->Object;
    Vector3 Rotation = { 0.0f, yaw, 0.0f };
    Vector3 Position = feet;
    Vector3 Scale = { 1.0f, 1.0f, 1.0f };
    WorldObject* Parent = WorldObject_GetParent(Object);
    if (Parent != NULL)
    {
        Matrix Wanted = WorldTransform_Compose(feet, Rotation, Scale);
        Matrix Local;
        if (!WorldTransform_TryGetLocalMatrix(Wanted, WorldObject_GetWorldMatrix(Parent), &Local)
            || !WorldTransform_Decompose(Local, Scale, &Position, &Rotation, &Scale))
        {
            return;
        }
    }
    else if (!AreVectorsEqual(WorldObject_GetScale(Object), Scale))
    {
        // A scaled character's collider and model are scaled too, its sweeps are not: say so once, leave the scale.
        WarnOnce(step->System, &step->Character->_runtime._hasWarnedScale, step->Id,
            "a character's object should keep a scale of 1 (its sweeps use the character's own sizes).");
        Scale = WorldObject_GetScale(Object);
    }

    // The setters validate (finite values); a refused value leaves the object where it was.
    if (!AreVectorsEqual(WorldObject_GetPosition(Object), Position))
    {
        Error Result = WorldObject_SetPosition(Object, Position);
        Error_Deconstruct(&Result);
    }
    if (!AreVectorsEqual(WorldObject_GetRotation(Object), Rotation))
    {
        Error Result = WorldObject_SetRotation(Object, Rotation);
        Error_Deconstruct(&Result);
    }
    if (!AreVectorsEqual(WorldObject_GetScale(Object), Scale))
    {
        Error Result = WorldObject_SetScale(Object, Scale);
        Error_Deconstruct(&Result);
    }
}


// Static functions: the update's parts.
static void SetGround(WorldCharacterState* state, const CharacterSupport* support)
{
    state->IsGrounded = true;
    state->IsTouchingGround = true;
    state->GroundObjectId = support->ObjectId;
    state->GroundNormal = support->Normal;
    state->GroundPoint = support->Point;
    state->TimeInAir = 0.0f;
}

static void ClearGround(WorldCharacterState* state)
{
    state->IsGrounded = false;
    state->GroundObjectId = 0U;
    state->GroundNormal = WorldUp;
    state->GroundPoint = (Vector3){ 0.0f, 0.0f, 0.0f };
}

/* The ground a grounded walker stands on, as the support its moves and steps are measured from. */
static CharacterSupport GetGround(const WorldCharacterState* state)
{
    return (CharacterSupport){ .IsSupported = true, .ObjectId = state->GroundObjectId, .Normal = state->GroundNormal,
        .Point = state->GroundPoint };
}

/* Leaving the ground (a jump, walking off, taking off): the ground's own velocity is kept, now part of its own. */
static void LeaveGround(WorldCharacterComponent* character)
{
    WorldCharacterState* State = &character->_state;
    character->_runtime._velocity = Vector3Add(character->_runtime._velocity, State->GroundVelocity);
    State->GroundVelocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    ClearGround(State);
    State->TimeInAir = 0.0f;
}

/* The reach of a look for the ground that keeps a character on it: its snap distance, but at least the landing probe's,
 * which reaches past the skin it rests above (a snap distance of 0 still finds the ground it stands on). */
static float GetGroundReach(const CharacterSettings* settings)
{
    return Math_MaxFloat(settings->GroundSnapDistance, CHARACTER_LANDING_PROBE_DISTANCE);
}

/* A character placed (its first update, a teleport): on the ground found within its reach below, or in the air without a
 * late jump (the coyote time is for walking off edges). */
static void Settle(CharacterStep* step, Vector3* feet)
{
    WorldCharacterState* State = &step->Character->_state;
    CharacterSupport Support;
    Vector3 Settled;
    bool IsWalking = State->Mode == CharacterMovementMode_Walking;
    if (IsWalking && (CharacterMotion_FollowGround(&step->Body, *feet, GetGroundReach(&step->Settings), NULL, &Settled,
        &Support) == CharacterGround_Supported))
    {
        *feet = Settled;
        SetGround(State, &Support);
        step->Character->_runtime._hasJumpedSinceGrounded = false;
        return;
    }
    ClearGround(State);
    State->TimeInAir = 0.0f;
    step->Character->_runtime._hasJumpedSinceGrounded = true;
}

/* The first update: the body at its standing height, the eyes at theirs, settled on the ground below if there is any
 * within reach. */
static void Start(CharacterStep* step, Vector3* feet)
{
    WorldCharacterState* State = &step->Character->_state;
    State->Stance = CharacterStance_Standing;
    State->Height = step->StandingHeight;
    State->EyeHeight = step->Settings.EyeHeight;
    State->CrouchAmount = 0.0f;
    step->Body.Height = State->Height;
    Settle(step, feet);
    step->Character->_runtime._isStarted = true;
}

/* What moved the character since its last update besides itself: the collision step's pushes (zero below rounding, or
 * before a first update). It is part of how fast the character moved, not of the velocity it drives with. */
static Vector3 ReadPush(const CharacterStep* step, Vector3 feet)
{
    const WorldCharacterRuntime* Runtime = &step->Character->_runtime;
    Vector3 None = { 0.0f, 0.0f, 0.0f };
    if (!Runtime->_hasLastPosition)
    {
        return None;
    }
    Vector3 Push = Vector3Subtract(feet, Runtime->_lastPosition);
    return (Vector3Length(Push) < CHARACTER_PUSH_EPSILON) ? None : Push;
}

/* Walking or flying, as the input asks and the settings allow. */
static void UpdateMode(CharacterStep* step)
{
    WorldCharacterState* State = &step->Character->_state;
    bool WantsToFly = step->Character->_input.WantsToFly && step->Settings.CanFly;
    if (WantsToFly && (State->Mode == CharacterMovementMode_Walking))
    {
        State->Mode = CharacterMovementMode_Flying;
        if (State->IsGrounded)
        {
            LeaveGround(step->Character);
        }
        QueueEvent(step->System, WorldCharacterEventKind_TookOff, step->Id, 0U, 0.0f);
    }
    else if (!WantsToFly && (State->Mode == CharacterMovementMode_Flying))
    {
        State->Mode = CharacterMovementMode_Walking;
        ClearGround(State);
        State->TimeInAir = 0.0f;
        // No late jump out of a flight: the coyote time is for walking off edges.
        step->Character->_runtime._hasJumpedSinceGrounded = true;
        QueueEvent(step->System, WorldCharacterEventKind_StoppedFlying, step->Id, 0U, 0.0f);
    }
}

/* Carries a grounded character with what it stands on: the ground's move and turn since the last update, applied to
 * the feet (exact for turning platforms) and to the facing and the look. */
static void CarryWithGround(CharacterStep* step, Vector3* feet)
{
    WorldCharacterState* State = &step->Character->_state;
    WorldCharacterRuntime* Runtime = &step->Character->_runtime;
    State->GroundVelocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    if (!State->IsGrounded || !Runtime->_hasGroundMatrix || (Runtime->_groundMatrixObjectId != State->GroundObjectId))
    {
        return;
    }
    WorldObject* Ground = NULL;
    if (!World_TryGetObject(step->System->_world, Runtime->_groundMatrixObjectId, &Ground))
    {
        return;
    }
    Matrix Now = WorldObject_GetWorldMatrix(Ground);
    if (AreMatricesEqual(&Now, &Runtime->_groundMatrix))
    {
        return;
    }
    // The ground's change since then: its old placement undone, its new one applied.
    Matrix Change = MatrixMultiply(MatrixInvert(Runtime->_groundMatrix), Now);
    Vector3 Carried = Vector3Transform(*feet, Change);
    float YawChange = atan2f(Change.m8, Change.m10);
    if (!isfinite(Carried.x) || !isfinite(Carried.y) || !isfinite(Carried.z) || !isfinite(YawChange))
    {
        return;
    }
    State->GroundVelocity = Vector3Scale(Vector3Subtract(Carried, *feet), 1.0f / step->DeltaSeconds);
    *feet = Carried;
    State->GroundPoint = Vector3Transform(State->GroundPoint, Change);
    State->FacingYaw = WrapAngle(State->FacingYaw + YawChange);
    step->Character->_input.LookYaw = WrapAngle(step->Character->_input.LookYaw + YawChange);
}

/* Standing or crouching: crouching while asked, allowed and on the ground; standing up once the input lets go (or
 * crouching is no longer allowed) and the standing capsule fits. The capsule changes at once, its feet in place. */
static void UpdateStance(CharacterStep* step, Vector3 feet)
{
    WorldCharacterState* State = &step->Character->_state;
    bool WantsCrouch = step->Character->_input.WantsCrouch && step->Settings.CanCrouch
        && (State->Mode == CharacterMovementMode_Walking);
    if (State->Stance == CharacterStance_Standing)
    {
        if (WantsCrouch && State->IsGrounded)
        {
            State->Stance = CharacterStance_Crouching;
            QueueEvent(step->System, WorldCharacterEventKind_Crouched, step->Id, 0U, 0.0f);
        }
        State->IsStandUpBlocked = false;
    }
    else if (WantsCrouch)
    {
        State->IsStandUpBlocked = false;
    }
    else if (CharacterMotion_DoesFit(&step->Body, feet, step->StandingHeight, &step->System->_scratchIds))
    {
        State->Stance = CharacterStance_Standing;
        State->IsStandUpBlocked = false;
        QueueEvent(step->System, WorldCharacterEventKind_StoodUp, step->Id, 0U, 0.0f);
    }
    else
    {
        State->IsStandUpBlocked = true;
    }
    State->Height = (State->Stance == CharacterStance_Crouching) ? step->CrouchHeight : step->StandingHeight;
    step->Body.Height = State->Height;
}

/* The speed a walker wants: crouched, running or walking. */
static float GetWalkingSpeed(const CharacterStep* step)
{
    const CharacterSettings* Settings = &step->Settings;
    if (step->Character->_state.Stance == CharacterStance_Crouching)
    {
        return Settings->CrouchSpeed;
    }
    return (step->Character->_input.IsRunning && Settings->CanRun) ? Settings->RunSpeed : Settings->WalkSpeed;
}

/* Whether a jump may start now: allowed, walking, in a stance that may jump, on the ground or within the coyote time
 * after walking off it. */
static bool CanJumpNow(const CharacterStep* step)
{
    const CharacterSettings* Settings = &step->Settings;
    const WorldCharacterState* State = &step->Character->_state;
    if (!Settings->CanJump || (State->Mode != CharacterMovementMode_Walking))
    {
        return false;
    }
    if ((State->Stance == CharacterStance_Crouching) && !Settings->CanJumpWhileCrouched)
    {
        return false;
    }
    if (State->IsGrounded)
    {
        return true;
    }
    return !step->Character->_runtime._hasJumpedSinceGrounded && (State->TimeInAir <= Settings->CoyoteSeconds);
}

/* The velocity: a walker's on the ground (and its jump) or in the air, or a flyer's. */
static void UpdateVelocity(CharacterStep* step)
{
    WorldCharacterState* State = &step->Character->_state;
    WorldCharacterRuntime* Runtime = &step->Character->_runtime;
    const CharacterInput* Input = &step->Character->_input;
    const CharacterSettings* Settings = &step->Settings;
    float Delta = step->DeltaSeconds;

    if (State->Mode == CharacterMovementMode_Flying)
    {
        float Speed = (Input->IsRunning && Settings->CanRun) ? Settings->FlyFastSpeed : Settings->FlySpeed;
        Vector3 Wish = Vector3Scale(Input->MoveDirection, Speed);
        Runtime->_velocity = Approach(Runtime->_velocity, Wish, Settings->FlyAcceleration, Settings->FlyDeceleration,
            Delta);
        return;
    }

    Vector3 WishDirection = { Input->MoveDirection.x, 0.0f, Input->MoveDirection.z };
    float WishLength = Vector3Length(WishDirection);
    if (WishLength > 1.0f)
    {
        WishDirection = Vector3Scale(WishDirection, 1.0f / WishLength);
    }
    Vector3 Wish = Vector3Scale(WishDirection, GetWalkingSpeed(step));
    Vector3 Horizontal = { Runtime->_velocity.x, 0.0f, Runtime->_velocity.z };
    if (State->IsGrounded)
    {
        Horizontal = Approach(Horizontal, Wish, Settings->Acceleration, Settings->Deceleration, Delta);
        Runtime->_velocity = Horizontal;
    }
    else
    {
        Horizontal = MoveTowards(Horizontal, Wish, Settings->AirAcceleration * Delta);
        float Vertical = Runtime->_velocity.y - (step->Gravity * Settings->GravityMultiplier * Delta);
        Runtime->_velocity = (Vector3){ Horizontal.x, Math_MaxFloat(Vertical, -Settings->MaxFallSpeed), Horizontal.z };
        State->TimeInAir += Delta;
    }

    if (Runtime->_hasJumpRequest && CanJumpNow(step))
    {
        if (State->IsGrounded)
        {
            // From the ground: the jump adds to the ground's own velocity (a rising lift throws it higher).
            LeaveGround(step->Character);
            Runtime->_velocity.y += Settings->JumpSpeed;
        }
        else
        {
            // A late jump replaces the fall.
            Runtime->_velocity.y = Settings->JumpSpeed;
        }
        Runtime->_hasJumpRequest = false;
        Runtime->_hasJumpedSinceGrounded = true;
        QueueEvent(step->System, WorldCharacterEventKind_Jumped, step->Id, 0U, 0.0f);
    }
    if (Runtime->_hasJumpRequest)
    {
        Runtime->_jumpRequestSeconds -= Delta;
        if (Runtime->_jumpRequestSeconds < 0.0f)
        {
            Runtime->_hasJumpRequest = false;
        }
    }
}

/* After a walker's move: on the ground it follows the ground down (stairs, slopes) or leaves it (@p ground: what it
 * walked on); in the air it lands on support while not rising. */
static void UpdateWalkingGround(CharacterStep* step, Vector3* feet, const CharacterMove* move,
    const CharacterSupport* ground, float fallSpeed)
{
    WorldCharacterState* State = &step->Character->_state;
    CharacterSupport Support;
    Vector3 Settled;
    if ((ground != NULL) && State->IsGrounded)
    {
        if (move->HasStepped)
        {
            SetGround(State, &move->StepSupport);
            return;
        }
        CharacterGround Ground = CharacterMotion_FollowGround(&step->Body, *feet, GetGroundReach(&step->Settings),
            ground, &Settled, &Support);
        if (Ground == CharacterGround_Supported)
        {
            *feet = Settled;
            SetGround(State, &Support);
            return;
        }
        if (Ground == CharacterGround_Unsure)
        {
            // A pushable pressing on its side hid the ground: it keeps the ground it had until the step parts them.
            return;
        }
        LeaveGround(step->Character);
        QueueEvent(step->System, WorldCharacterEventKind_LeftGround, step->Id, 0U, 0.0f);
        return;
    }

    if ((step->Character->_runtime._velocity.y <= 0.0f)
        && (CharacterMotion_FollowGround(&step->Body, *feet, CHARACTER_LANDING_PROBE_DISTANCE, NULL, &Settled, &Support)
            == CharacterGround_Supported))
    {
        *feet = Settled;
        SetGround(State, &Support);
        step->Character->_runtime._velocity.y = 0.0f;
        step->Character->_runtime._hasJumpedSinceGrounded = false;
        QueueEvent(step->System, WorldCharacterEventKind_Landed, step->Id, Support.ObjectId, fallSpeed);
    }
}

/* Moves the capsule by its velocity (along the ground while walking on it) and finds its ground afterwards. */
static void Move(CharacterStep* step, Vector3* feet)
{
    WorldCharacterState* State = &step->Character->_state;
    Vector3* Velocity = &step->Character->_runtime._velocity;
    bool IsWalking = State->Mode == CharacterMovementMode_Walking;
    bool IsOnGround = IsWalking && State->IsGrounded;
    float FallSpeed = Math_MaxFloat(0.0f, -Velocity->y);
    Vector3 Displacement = Vector3Scale(*Velocity, step->DeltaSeconds);
    CharacterSupport Ground = GetGround(State);
    if (IsOnGround)
    {
        Displacement = CharacterMotion_ProjectOnGround(Displacement, State->GroundNormal);
    }
    CharacterMove Moved = CharacterMotion_MoveAndSlide(&step->Body, *feet, Displacement, Velocity,
        IsOnGround ? &Ground : NULL);
    *feet = Moved.Feet;

    if (IsWalking)
    {
        UpdateWalkingGround(step, feet, &Moved, IsOnGround ? &Ground : NULL, FallSpeed);
        return;
    }
    // Flying: no ground, but brains land birds by whether ground is right below.
    CharacterSupport Support;
    Vector3 Below;
    State->IsTouchingGround = CharacterMotion_FollowGround(&step->Body, *feet, CHARACTER_TOUCH_PROBE_DISTANCE, NULL,
        &Below, &Support) == CharacterGround_Supported;
    State->TimeInAir = 0.0f;
}

/* Turns the body toward its facing target at its turn speed (or at once). */
static void UpdateFacing(CharacterStep* step)
{
    WorldCharacterState* State = &step->Character->_state;
    const CharacterInput* Input = &step->Character->_input;
    State->TurnRate = 0.0f;
    float Target = State->FacingYaw;
    switch (Input->Facing)
    {
        case CharacterFacing_Movement:
        {
            // Where it drives to, not where pushes take it.
            Vector3 Velocity = step->Character->_runtime._velocity;
            float Speed = sqrtf((Velocity.x * Velocity.x) + (Velocity.z * Velocity.z));
            if (Speed < CHARACTER_FACING_MIN_SPEED)
            {
                return;
            }
            Target = atan2f(Velocity.x, Velocity.z);
            break;
        }
        case CharacterFacing_Look:
            Target = Input->LookYaw;
            break;
        case CharacterFacing_Keep:
            return;
    }
    float Turn = WrapAngle(Target - State->FacingYaw);
    if (!step->Settings.IsTurnInstant)
    {
        float Most = step->Settings.TurnSpeed * step->DeltaSeconds;
        Turn = Math_ClampFloat(Turn, -Most, Most);
    }
    State->FacingYaw = WrapAngle(State->FacingYaw + Turn);
    State->TurnRate = Turn / step->DeltaSeconds;
}

/* Eases the eyes toward the stance's eye height over the crouch time. Eyes outside the two heights (the settings
 * changed: another kind of character, a script) move into their range at once. */
static void UpdateEyes(CharacterStep* step)
{
    WorldCharacterState* State = &step->Character->_state;
    const CharacterSettings* Settings = &step->Settings;
    float Target = (State->Stance == CharacterStance_Crouching) ? Settings->CrouchEyeHeight : Settings->EyeHeight;
    float Span = Settings->EyeHeight - Settings->CrouchEyeHeight;
    State->EyeHeight = Math_ClampFloat(State->EyeHeight, Math_MinFloat(Settings->EyeHeight, Settings->CrouchEyeHeight),
        Math_MaxFloat(Settings->EyeHeight, Settings->CrouchEyeHeight));
    if (Settings->CrouchTransitionSeconds <= 0.0f)
    {
        State->EyeHeight = Target;
    }
    else
    {
        float Rate = Math_MaxFloat(fabsf(Span), 1e-3f) / Settings->CrouchTransitionSeconds;
        float Step = Rate * step->DeltaSeconds;
        State->EyeHeight = (fabsf(Target - State->EyeHeight) <= Step) ? Target
            : State->EyeHeight + ((Target > State->EyeHeight) ? Step : -Step);
    }
    if (fabsf(Span) > 1e-5f)
    {
        State->CrouchAmount = Math_ClampFloat((Settings->EyeHeight - State->EyeHeight) / Span, 0.0f, 1.0f);
    }
    else
    {
        State->CrouchAmount = (State->Stance == CharacterStance_Crouching) ? 1.0f : 0.0f;
    }
}

/* Remembers the ground's placement, for carrying the character with it next time. */
static void RecordGround(CharacterStep* step)
{
    WorldCharacterRuntime* Runtime = &step->Character->_runtime;
    WorldCharacterState* State = &step->Character->_state;
    Runtime->_hasGroundMatrix = false;
    WorldObject* Ground = NULL;
    if (!State->IsGrounded || (State->GroundObjectId == 0U)
        || !World_TryGetObject(step->System->_world, State->GroundObjectId, &Ground))
    {
        return;
    }
    Runtime->_groundMatrix = WorldObject_GetWorldMatrix(Ground);
    Runtime->_groundMatrixObjectId = State->GroundObjectId;
    Runtime->_hasGroundMatrix = true;
}

/* A use press: a "used" event naming what the character looks at. */
static void HandleUse(CharacterStep* step, bool isUsePressed)
{
    if (!isUsePressed)
    {
        return;
    }
    WorldRayHit Hit;
    bool IsHit = WorldCharacters_FindLookTarget(step->System, step->Id, &Hit);
    QueueEvent(step->System, WorldCharacterEventKind_Used, step->Id, IsHit ? Hit.Shape.ObjectId : 0U,
        IsHit ? Hit.Distance : 0.0f);
}

static void UpdateCharacter(WorldCharacters* self, WorldCharacterComponent* character, float deltaSeconds,
    float gravity)
{
    WorldObject* Object = WorldComponent_GetOwner(&character->Base);
    if (!WorldObject_IsEffectivelyEnabled(Object))
    {
        // Whatever moves meanwhile is not one tick's carry or push when it comes back.
        character->_runtime._hasGroundMatrix = false;
        character->_runtime._hasLastPosition = false;
        return;
    }
    CharacterStep Step;
    Step.System = self;
    Step.Character = character;
    Step.Object = Object;
    Step.Id = WorldObject_GetId(Object);
    Step.Settings = *GetSettings(character);
    CharacterSettings_GetBodyHeights(&Step.Settings, &Step.StandingHeight, &Step.CrouchHeight);
    Step.DeltaSeconds = deltaSeconds;
    Step.Gravity = gravity;

    WorldCharacterState* State = &character->_state;
    WorldCharacterRuntime* Runtime = &character->_runtime;
    const CharacterInput* Input = &character->_input;
    State->Height = (State->Stance == CharacterStance_Crouching) ? Step.CrouchHeight : Step.StandingHeight;
    BuildBody(&Step);

    Vector3 Feet = WorldObject_GetWorldPosition(Object);
    if (!Runtime->_isStarted)
    {
        Start(&Step, &Feet);
    }
    Vector3 Push = ReadPush(&Step, Feet);

    // The input's presses (a held request acts once).
    bool IsJumpPressed = Input->WantsJump && !Runtime->_wasJumpWanted;
    bool IsUsePressed = Input->WantsUse && !Runtime->_wasUseWanted;
    Runtime->_wasJumpWanted = Input->WantsJump;
    Runtime->_wasUseWanted = Input->WantsUse;
    if (IsJumpPressed)
    {
        Runtime->_hasJumpRequest = true;
        Runtime->_jumpRequestSeconds = Step.Settings.JumpBufferSeconds;
    }

    UpdateMode(&Step);
    CarryWithGround(&Step, &Feet);
    Vector3 Carried = Feet;
    UpdateStance(&Step, Feet);
    EnsureCollider(&Step);
    UpdateVelocity(&Step);
    Move(&Step, &Feet);
    // How fast it moved: its own move this update and the collision step's push since the last (the carry is the
    // ground's).
    State->Velocity = Vector3Scale(Vector3Add(Vector3Subtract(Feet, Carried), Push), 1.0f / deltaSeconds);
    UpdateFacing(&Step);
    UpdateEyes(&Step);
    if (State->Mode == CharacterMovementMode_Walking)
    {
        State->IsTouchingGround = State->IsGrounded;
    }
    else
    {
        ClearGround(State);
    }

    WriteTransform(&Step, Feet, State->FacingYaw);
    RecordGround(&Step);
    Runtime->_lastPosition = WorldObject_GetWorldPosition(Object);
    Runtime->_hasLastPosition = true;
    HandleUse(&Step, IsUsePressed);
}


// Public functions.
Error WorldCharacters_Create(World* world, WorldCollision* collision, Logger* logger, WorldCharacters** outCharacters)
{
    if ((world == NULL) || (collision == NULL) || (outCharacters == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldCharacters_Create: world, collision and outCharacters must not be NULL.");
    }
    WorldCharacters* Self = Memory_Allocate(sizeof(WorldCharacters));
    Memory_Zero(Self, sizeof(*Self));
    Self->_world = world;
    Self->_collision = collision;
    Self->_logger = logger;
    GenericBuffer_AllocateVariable(&Self->_events, INITIAL_EVENT_CAPACITY, sizeof(WorldCharacterEvent));
    GenericBuffer_AllocateVariable(&Self->_scratchIds, INITIAL_ID_CAPACITY, sizeof(uint64_t));
    *outCharacters = Self;
    return Error_CreateSuccess();
}

void WorldCharacters_Destroy(WorldCharacters* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_events._data);
    Memory_Free(self->_scratchIds._data);
    Memory_Free(self);
}

void WorldCharacters_Update(WorldCharacters* self, float deltaSeconds)
{
    if (!(deltaSeconds > 0.0f))
    {
        return;
    }
    float Delta = Math_MinFloat(deltaSeconds, CHARACTER_MAX_DELTA_SECONDS);
    self->_tick++;
    float Gravity = World_GetEnvironment(self->_world)->Gravity;

    // By index, fetching the array each time: a character may add its collider (another type's array) on the way.
    size_t Count = 0U;
    World_GetComponents(self->_world, WorldComponentType_Character, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        size_t CurrentCount = 0U;
        WorldComponent* const* Characters = World_GetComponents(self->_world, WorldComponentType_Character,
            &CurrentCount);
        if (Index >= CurrentCount)
        {
            break;
        }
        UpdateCharacter(self, WorldCharacterComponent_FromComponent(Characters[Index]), Delta, Gravity);
    }
}

Error WorldCharacters_Teleport(WorldCharacters* self, uint64_t objectId, Vector3 feetPosition, float yaw)
{
    WorldComponent* Component = NULL;
    if ((self == NULL) || !World_TryGetComponent(self->_world, objectId, WorldComponentType_Character, &Component))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldCharacters_Teleport: self must not be NULL and the object must have a character component.");
    }
    if (!isfinite(feetPosition.x) || !isfinite(feetPosition.y) || !isfinite(feetPosition.z) || !isfinite(yaw))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldCharacters_Teleport: the position and the yaw must be finite.");
    }
    WorldCharacterComponent* Character = WorldCharacterComponent_FromComponent(Component);
    CharacterStep Step;
    Step.System = self;
    Step.Character = Character;
    Step.Object = WorldComponent_GetOwner(Component);
    Step.Id = objectId;
    Step.Settings = *GetSettings(Character);
    CharacterSettings_GetBodyHeights(&Step.Settings, &Step.StandingHeight, &Step.CrouchHeight);
    Step.DeltaSeconds = 0.0f;
    Step.Gravity = 0.0f;
    BuildBody(&Step);

    WorldCharacterState* State = &Character->_state;
    float Wrapped = WrapAngle(yaw);
    State->Velocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    Character->_runtime._velocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    State->GroundVelocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    State->FacingYaw = Wrapped;
    State->TurnRate = 0.0f;
    Character->_input.LookYaw = Wrapped;
    Character->_runtime._hasGroundMatrix = false;
    Character->_runtime._hasJumpRequest = false;

    Vector3 Feet = feetPosition;
    if (!Character->_runtime._isStarted)
    {
        Start(&Step, &Feet);
    }
    else
    {
        Settle(&Step, &Feet);
    }
    WriteTransform(&Step, Feet, Wrapped);
    RecordGround(&Step);
    Character->_runtime._lastPosition = WorldObject_GetWorldPosition(Step.Object);
    Character->_runtime._hasLastPosition = true;
    WorldCollision_Teleport(self->_collision, objectId);
    return Error_CreateSuccess();
}

bool WorldCharacters_FindLookTarget(WorldCharacters* self, uint64_t objectId, WorldRayHit* outHit)
{
    WorldComponent* Component = NULL;
    if (!World_TryGetComponent(self->_world, objectId, WorldComponentType_Character, &Component))
    {
        return false;
    }
    WorldCharacterComponent* Character = WorldCharacterComponent_FromComponent(Component);
    const CharacterSettings* Settings = GetSettings(Character);
    uint32_t Layers = Settings->InteractionLayers & ~COLLISION_LAYER_BIT(CollisionLayer_Trigger);
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(Layers, WorldCollisionTriggerMode_Ignore);
    Filter.IgnoredObjectId = objectId;
    Filter.IsIgnoredSubtree = true;
    Vector3 Eye = WorldCharacterComponent_GetEyePosition(Character);
    Vector3 Direction = CharacterInput_GetLookDirection(WorldCharacterComponent_GetInput(Character));
    return WorldCollision_Raycast(self->_collision, Eye, Direction, Settings->InteractionReach, &Filter, outHit);
}

const WorldCharacterEvent* WorldCharacters_GetEvents(const WorldCharacters* self, size_t* outCount)
{
    *outCount = self->_events._count;
    return (self->_events._count > 0U) ? (const WorldCharacterEvent*)self->_events._data : NULL;
}

void WorldCharacters_ClearEvents(WorldCharacters* self)
{
    GenericBuffer_Clear(&self->_events);
}

uint64_t WorldCharacters_GetTick(const WorldCharacters* self)
{
    return self->_tick;
}
