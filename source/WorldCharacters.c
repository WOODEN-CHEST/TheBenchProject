#include <inttypes.h>
#include <math.h>
#include "WorldCharacters.h"
#include "WorldCharacterComponent.h"
#include "WorldCharacterInternal.h"
#include "WorldColliderComponent.h"
#include "WorldEnvironment.h"
#include "GameMath.h"
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
/** Movement keys pushed less far than this are let go (no direction to push along). */
#define CHARACTER_MIN_WISH 1e-6f
/** The steepest drop per metre across the ground that a walker's ground follow reaches down for: the tangent of its
 *  slope limit, but at most this (about 76 degrees; a limit near 90 would let it drop far in a tick). */
#define CHARACTER_MAX_FOLLOW_SLOPE 4.0f
/** The eyes ease toward the stance's eye height leaving this share of the way after the crouch time, and are there
 *  once that close. */
#define CHARACTER_EYE_EASE_REMAINDER 0.01f
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
    /** The collision step's push since the last update (ReadPush). */
    Vector3 Push;
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
 * velocity (stopping, turning, reversing): flying. */
static Vector3 Approach(Vector3 velocity, Vector3 wish, float acceleration, float deceleration, float deltaSeconds)
{
    Vector3 Change = Vector3Subtract(wish, velocity);
    float Rate = (Vector3DotProduct(Change, velocity) < 0.0f) ? deceleration : acceleration;
    return MoveTowards(velocity, wish, Rate * deltaSeconds);
}

/* @p velocity slowed by a constant @p deceleration over the tick, not past a standstill. */
static Vector3 Decelerate(Vector3 velocity, float deceleration, float deltaSeconds)
{
    float Speed = Vector3Length(velocity);
    float Drop = deceleration * deltaSeconds;
    if (Speed <= Drop)
    {
        return (Vector3){ 0.0f, 0.0f, 0.0f };
    }
    return Vector3Scale(velocity, (Speed - Drop) / Speed);
}

/* @p velocity (across the ground) pushed along @p direction (unit, or zero) at @p acceleration until its part along it
 * reaches @p wishSpeed (a part against it is pushed back first: a turn back), never raising its speed past the wished
 * speed or past what it had. A push across the motion turns it rather than adding to it, and a wall pushed into at a
 * slant, which takes the part into it every move, is slid along no faster (without the cap, as fast as the wished speed
 * over the cosine of the slant). */
static Vector3 PushAlong(Vector3 velocity, Vector3 direction, float wishSpeed, float acceleration, float deltaSeconds)
{
    float Room = wishSpeed - Vector3DotProduct(velocity, direction);
    if (Room <= 0.0f)
    {
        return velocity;
    }
    float Before = Vector3Length(velocity);
    Vector3 Pushed = Vector3Add(velocity, Vector3Scale(direction, Math_MinFloat(acceleration * deltaSeconds, Room)));
    float Most = Math_MaxFloat(wishSpeed, Before);
    float After = Vector3Length(Pushed);
    return (After > Most) ? Vector3Scale(Pushed, Most / After) : Pushed;
}

/* The length of @p vector across the ground (its horizontal part). */
static float GetHorizontalLength(Vector3 vector)
{
    return sqrtf((vector.x * vector.x) + (vector.z * vector.z));
}

static bool AreMatricesEqual(const Matrix4Double* a, const Matrix4Double* b)
{
    return Memory_IsEqual(a, b, sizeof(Matrix4Double));
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
/* The local transform that places a character's object with its feet at @p feet facing @p yaw, upright, scale 1: those
 * values for a root, through the parent's inverse for a child. False when the parent's world matrix cannot be inverted
 * (a zero scale). */
static bool GetLocalTransform(const WorldObject* object, Vector3Double feet, float yaw, Vector3Double* outPosition,
    Vector3* outRotation, Vector3* outScale)
{
    *outPosition = feet;
    *outRotation = (Vector3){ 0.0f, yaw, 0.0f };
    *outScale = (Vector3){ 1.0f, 1.0f, 1.0f };
    const WorldObject* Parent = WorldObject_GetParent(object);
    if (Parent == NULL)
    {
        return true;
    }
    Matrix4Double Wanted = WorldTransform_Compose(feet, *outRotation, *outScale);
    Matrix4Double ParentWorld = WorldObject_GetWorldMatrix(Parent);
    Matrix4Double Local;
    return WorldTransform_TryGetLocalMatrix(&Wanted, &ParentWorld, &Local)
        && WorldTransform_Decompose(&Local, *outScale, outPosition, outRotation, outScale);
}

/* Writes the character's placement (feet and yaw, upright, scale 1) into its object, only what changed: directly for a
 * root, through the parent's inverse for a child. */
static void WriteTransform(CharacterStep* step, Vector3Double feet, float yaw)
{
    WorldObject* Object = step->Object;
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
    if (!GetLocalTransform(Object, feet, yaw, &Position, &Rotation, &Scale))
    {
        return;
    }
    if ((WorldObject_GetParent(Object) == NULL) && !AreVectorsEqual(WorldObject_GetScale(Object), Scale))
    {
        // A scaled character's collider and model are scaled too, its sweeps are not: say so once, leave the scale.
        WarnOnce(step->System, &step->Character->_runtime._hasWarnedScale, step->Id,
            "a character's object should keep a scale of 1 (its sweeps use the character's own sizes).");
        Scale = WorldObject_GetScale(Object);
    }

    // The setters validate (finite values); a refused value leaves the object where it was.
    Vector3Double Current = WorldObject_GetPosition(Object);
    if ((Current.X != Position.X) || (Current.Y != Position.Y) || (Current.Z != Position.Z))
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
static void SetGround(WorldCharacterComponent* character, const CharacterSupport* support)
{
    WorldCharacterState* State = &character->_state;
    State->IsGrounded = true;
    State->IsTouchingGround = true;
    State->GroundObjectId = support->ObjectId;
    State->GroundNormal = support->Normal;
    State->GroundPoint = support->Point;
    State->TimeInAir = 0.0f;
    character->_runtime._isClimbing = support->IsSteepEdge;
}

static void ClearGround(WorldCharacterComponent* character)
{
    WorldCharacterState* State = &character->_state;
    State->IsGrounded = false;
    State->GroundObjectId = 0U;
    State->GroundNormal = WorldUp;
    State->GroundPoint = Vector3Double_Zero();
    character->_runtime._isClimbing = false;
}

/* The ground a grounded walker stands on, as the support its moves and steps are measured from. */
static CharacterSupport GetGround(const WorldCharacterComponent* character)
{
    const WorldCharacterState* State = &character->_state;
    return (CharacterSupport){ .IsSupported = true, .ObjectId = State->GroundObjectId, .Normal = State->GroundNormal,
        .Point = State->GroundPoint, .IsSteepEdge = character->_runtime._isClimbing };
}

/* Leaving the ground (a jump, walking off, taking off): the ground's own velocity is kept, now part of its own. */
static void LeaveGround(WorldCharacterComponent* character)
{
    WorldCharacterState* State = &character->_state;
    character->_runtime._velocity = Vector3Add(character->_runtime._velocity, State->GroundVelocity);
    State->GroundVelocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    ClearGround(character);
    State->TimeInAir = 0.0f;
}

/* How far down a walker on the ground looks for it after going @p distance metres across it: as far as a slope at its
 * slope limit drops over that, and the landing probe's reach past the skin it rests above (so a walker standing still
 * finds the ground it stands on); or its step-down distance, when that is more. A steeper drop it falls down. */
static float GetGroundReach(const CharacterSettings* settings, float distance)
{
    float Slope = Math_MinFloat(tanf(settings->SlopeLimit), CHARACTER_MAX_FOLLOW_SLOPE);
    return Math_MaxFloat(settings->GroundSnapDistance, (distance * Slope) + CHARACTER_LANDING_PROBE_DISTANCE);
}

/* A character placed (its first update, a teleport): on the ground found within its step height (or its step-down
 * distance) below, so a placement a little above the floor (a spawn point set high) stands on it at once; otherwise
 * in the air without a late jump (the coyote time is for walking off edges). */
static void Settle(CharacterStep* step, Vector3Double* feet)
{
    WorldCharacterState* State = &step->Character->_state;
    CharacterSupport Support;
    Vector3Double Settled;
    bool IsWalking = State->Mode == CharacterMovementMode_Walking;
    float Reach = Math_MaxFloat(GetGroundReach(&step->Settings, 0.0f), step->Settings.StepHeight);
    if (IsWalking && (CharacterMotion_FollowGround(&step->Body, *feet, Reach, NULL, &Settled, &Support)
        == CharacterGround_Supported))
    {
        *feet = Settled;
        SetGround(step->Character, &Support);
        step->Character->_runtime._hasJumpedSinceGrounded = false;
        return;
    }
    ClearGround(step->Character);
    State->TimeInAir = 0.0f;
    step->Character->_runtime._hasJumpedSinceGrounded = true;
}

/* The first update: the body at its standing height, the eyes at theirs, settled on the ground below if there is any
 * within reach. */
static void Start(CharacterStep* step, Vector3Double* feet)
{
    WorldCharacterState* State = &step->Character->_state;
    State->Stance = CharacterStance_Standing;
    State->Height = step->StandingHeight;
    State->EyeHeight = step->Settings.EyeHeight;
    State->CrouchAmount = 0.0f;
    step->Body.Height = State->Height;
    // Long since any jump: a jump held from the start jumps at once.
    step->Character->_runtime._secondsSinceJump = (float)CHARACTER_MAX_SECONDS;
    Settle(step, feet);
    step->Character->_runtime._isStarted = true;
}

/* What moved the character since its last update besides itself: the collision step's pushes (zero below rounding, or
 * before a first update). It is part of how fast the character moved, not of the velocity it drives with. */
static Vector3 ReadPush(const CharacterStep* step, Vector3Double feet)
{
    const WorldCharacterRuntime* Runtime = &step->Character->_runtime;
    Vector3 None = { 0.0f, 0.0f, 0.0f };
    if (!Runtime->_hasLastPosition)
    {
        return None;
    }
    Vector3 Push = GameMath_ToVector3Relative(feet, Runtime->_lastPosition);
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
        ClearGround(step->Character);
        State->TimeInAir = 0.0f;
        // No late jump out of a flight: the coyote time is for walking off edges.
        step->Character->_runtime._hasJumpedSinceGrounded = true;
        QueueEvent(step->System, WorldCharacterEventKind_StoppedFlying, step->Id, 0U, 0.0f);
    }
}

/* Where the ground's move from @p before to @p after (its world matrices; @p beforeInverse inverts @p before) takes a
 * world @p point: the point in the ground's old frame, put back with its new one. Relative to the ground's
 * translations, as WRVectorMath advises: far from the origin, the old inverse times the new matrix would cancel terms
 * as large as the ground's position, off by a few of a double's steps there; this way only the result's own rounding
 * is. */
static Vector3Double CarryPoint(const Matrix4Double* before, const Matrix4Double* beforeInverse,
    const Matrix4Double* after, Vector3Double point)
{
    Vector3Double Local = Matrix4Double_TransformDirection(beforeInverse,
        Vector3Double_Subtract(point, Matrix4Double_GetTranslation(before)));
    return Vector3Double_Add(Matrix4Double_GetTranslation(after), Matrix4Double_TransformDirection(after, Local));
}

/* Carries a grounded character with what it stands on: the ground's move and turn since the last update, applied to
 * the feet (exact for turning platforms) and to the facing and the look. */
static void CarryWithGround(CharacterStep* step, Vector3Double* feet)
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
    Matrix4Double Now = WorldObject_GetWorldMatrix(Ground);
    Matrix4Double Before = Runtime->_groundMatrix;
    Matrix4Double BeforeInverse;
    if (AreMatricesEqual(&Now, &Before) || !Matrix4Double_TryInvertAffine(&Before, &BeforeInverse))
    {
        return;
    }
    // The ground's change since then: its old placement undone, its new one applied. The turn is where it takes the
    // forward axis.
    Vector3Double Carried = CarryPoint(&Before, &BeforeInverse, &Now, *feet);
    Vector3Double Forward = Matrix4Double_TransformDirection(&Now,
        Matrix4Double_TransformDirection(&BeforeInverse, Vector3Double_Create(0.0, 0.0, 1.0)));
    float YawChange = (float)atan2(Forward.X, Forward.Z);
    if (!Vector3Double_IsFinite(Carried) || !isfinite(YawChange))
    {
        return;
    }
    State->GroundVelocity = Vector3Scale(GameMath_ToVector3Relative(Carried, *feet), 1.0f / step->DeltaSeconds);
    *feet = Carried;
    State->GroundPoint = CarryPoint(&Before, &BeforeInverse, &Now, State->GroundPoint);
    State->FacingYaw = WrapAngle(State->FacingYaw + YawChange);
    step->Character->_input.LookYaw = WrapAngle(step->Character->_input.LookYaw + YawChange);
}

/* Standing or crouching: crouching while asked, allowed and on the ground; standing up once the input lets go (or
 * crouching is no longer allowed) and the standing capsule fits. The capsule changes at once, its feet in place. */
static void UpdateStance(CharacterStep* step, Vector3Double feet)
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

/* Whether a walker runs now: asked, allowed and standing. */
static bool IsRunningNow(const CharacterStep* step)
{
    return step->Character->_input.IsRunning && step->Settings.CanRun
        && (step->Character->_state.Stance != CharacterStance_Crouching);
}

/* The speed a walker wants with its keys pushed all the way: crouched, running or walking. */
static float GetWalkingSpeed(const CharacterStep* step)
{
    const CharacterSettings* Settings = &step->Settings;
    if (step->Character->_state.Stance == CharacterStance_Crouching)
    {
        return Settings->CrouchSpeed;
    }
    return IsRunningNow(step) ? Settings->RunSpeed : Settings->WalkSpeed;
}

/* The movement keys across the ground: their direction (unit; zero when let go) and how far they are pushed (0 to
 * 1). */
static Vector3 GetWishDirection(const CharacterInput* input, float* outAmount)
{
    Vector3 Direction = { input->MoveDirection.x, 0.0f, input->MoveDirection.z };
    float Length = Vector3Length(Direction);
    if (!(Length > CHARACTER_MIN_WISH))
    {
        *outAmount = 0.0f;
        return (Vector3){ 0.0f, 0.0f, 0.0f };
    }
    *outAmount = Math_MinFloat(Length, 1.0f);
    return Vector3Scale(Direction, 1.0f / Length);
}

/* A walker's velocity across the ground with the push control: the ground's friction takes what the keys do not drive
 * (the motion across their direction or against it, and along it past the wished speed; all of it when they let go),
 * then the keys push along their direction. A turn back meets both; a turn sideways slides until friction takes the
 * old motion. */
// TODO: a wall pressed into at a slant turns the keys' push into motion along it, which this friction takes as
// undriven: the walker slides at the wished speed or, pushing nearly straight in, sticks. Telling the wall's part
// apart needs the walls it touches (a sweep along the keys each tick, or the last move's planes kept while it stays
// against them); steering has no such case and is the default.
static Vector3 PushOnGround(Vector3 velocity, Vector3 direction, float wishSpeed, const CharacterSettings* settings,
    float deltaSeconds)
{
    float Driven = Math_ClampFloat(Vector3DotProduct(velocity, direction), 0.0f, wishSpeed);
    Vector3 Undriven = Vector3Subtract(velocity, Vector3Scale(direction, Driven));
    Undriven = Decelerate(Undriven, settings->GroundFriction, deltaSeconds);
    Vector3 Slowed = Vector3Add(Vector3Scale(direction, Driven), Undriven);
    return PushAlong(Slowed, direction, wishSpeed, settings->Acceleration, deltaSeconds);
}

/* The same with the steer control: the keys turn the whole motion toward their direction at the acceleration; speed
 * past the wished speed is the friction's to take, and all of it when they let go. */
static Vector3 SteerOnGround(Vector3 velocity, Vector3 direction, float wishSpeed, const CharacterSettings* settings,
    float deltaSeconds)
{
    if (wishSpeed <= 0.0f)
    {
        return Decelerate(velocity, settings->GroundFriction, deltaSeconds);
    }
    float Speed = Math_MaxFloat(wishSpeed, Vector3Length(velocity) - (settings->GroundFriction * deltaSeconds));
    return MoveTowards(velocity, Vector3Scale(direction, Speed), settings->Acceleration * deltaSeconds);
}

/* A walker's velocity in the air: the keys push along their direction at the air acceleration (none by default), never
 * raising the speed across the ground past the wished speed or past what it had (they steer and brake); gravity pulls;
 * the air drags (a deceleration of the drag times the speed squared along the motion, solved over the tick); the fall
 * is capped. */
static Vector3 MoveInAir(Vector3 velocity, Vector3 direction, float wishSpeed, const CharacterSettings* settings,
    float gravity, float deltaSeconds)
{
    Vector3 Horizontal = { velocity.x, 0.0f, velocity.z };
    Horizontal = PushAlong(Horizontal, direction, wishSpeed, settings->AirAcceleration, deltaSeconds);
    Vector3 Velocity = { Horizontal.x, velocity.y - (gravity * deltaSeconds), Horizontal.z };
    float Speed = Vector3Length(Velocity);
    Velocity = Vector3Scale(Velocity, 1.0f / (1.0f + (settings->AirDrag * Speed * deltaSeconds)));
    Velocity.y = Math_MaxFloat(Velocity.y, -settings->MaxFallSpeed);
    return Velocity;
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

/* Whether a jump held down asks again: auto-jumping allowed, on the ground, and the delay passed since the last jump
 * (with jumps longer than the delay, it hops again as it lands). */
static bool IsAutoJumpDue(const CharacterStep* step)
{
    const WorldCharacterComponent* Character = step->Character;
    return Character->_input.WantsJump && step->Settings.CanAutoJump && Character->_state.IsGrounded
        && (Character->_runtime._secondsSinceJump >= step->Settings.AutoJumpDelaySeconds);
}

/* A jump's boost: the running or walking boost, times how far the keys are pushed (@p amount), along their
 * @p direction; never past the wished speed plus the boost, or the speed it already had, so jumps in a row do not add
 * up. */
static void AddJumpBoost(CharacterStep* step, Vector3 direction, float amount)
{
    const CharacterSettings* Settings = &step->Settings;
    float Boost = amount * (IsRunningNow(step) ? Settings->RunJumpBoost : Settings->WalkJumpBoost);
    if (Boost <= 0.0f)
    {
        return;
    }
    Vector3* Velocity = &step->Character->_runtime._velocity;
    Vector3 Horizontal = { Velocity->x, 0.0f, Velocity->z };
    float Most = Math_MaxFloat((amount * GetWalkingSpeed(step)) + Boost, Vector3Length(Horizontal));
    Horizontal = Vector3Add(Horizontal, Vector3Scale(direction, Boost));
    float Speed = Vector3Length(Horizontal);
    if (Speed > Most)
    {
        Horizontal = Vector3Scale(Horizontal, Most / Speed);
    }
    Velocity->x = Horizontal.x;
    Velocity->z = Horizontal.z;
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

    float Amount = 0.0f;
    Vector3 Direction = GetWishDirection(Input, &Amount);
    float WishSpeed = Amount * GetWalkingSpeed(step);
    if (State->IsGrounded)
    {
        Vector3 Horizontal = { Runtime->_velocity.x, 0.0f, Runtime->_velocity.z };
        switch (Settings->GroundControl)
        {
            case CharacterGroundControl_Push:
                Horizontal = PushOnGround(Horizontal, Direction, WishSpeed, Settings, Delta);
                break;
            case CharacterGroundControl_Steer:
                Horizontal = SteerOnGround(Horizontal, Direction, WishSpeed, Settings, Delta);
                break;
        }
        Runtime->_velocity = Horizontal;
    }
    else
    {
        Runtime->_velocity = MoveInAir(Runtime->_velocity, Direction, WishSpeed, Settings,
            step->Gravity * Settings->GravityMultiplier, Delta);
        State->TimeInAir += Delta;
    }

    if (Runtime->_hasJumpRequest && CanJumpNow(step))
    {
        // The boost first: on the ground it is measured against the walker's own speed, not its ground's.
        AddJumpBoost(step, Direction, Amount);
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
        Runtime->_secondsSinceJump = 0.0f;
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

/* The vertical speed a walker that walked off its @p ground keeps: @p moveSpeed, what its last move went down (or up)
 * with, as off a slope or down a stair. Off an edge with no walkable ground within its step height below the face it
 * walked on (a ledge, not a stair), though, it falls no faster than dropping from that face would have made it: its
 * round bottom rolls down over the rim faster than gravity at a run, and a 5 m/s run off a 1 m ledge would otherwise
 * dive, landing in half the time a fall takes. */
static float GetWalkOffSpeed(const CharacterStep* step, const CharacterSupport* ground, Vector3Double feet,
    float moveSpeed)
{
    Vector3 Face = CharacterMotion_GetFaceNormal(&step->Body, ground);
    if (Face.y < 1e-3f)
    {
        return moveSpeed;
    }
    // How far it went down past where it stood on the face (its bottom cap's centre a radius from the face's plane),
    // rolling over the face's rim; none on the face itself. The centre is measured from the contact.
    float Radius = step->Body.Radius;
    Vector3 Center = Vector3Add(GameMath_ToVector3Relative(feet, ground->Point), (Vector3){ 0.0f, Radius, 0.0f });
    float Sink = (Radius - Vector3DotProduct(Face, Center)) / Face.y;
    if ((Sink <= 0.0f) || CharacterMotion_HasGroundBelow(&step->Body, feet, step->Settings.StepHeight - Sink))
    {
        return moveSpeed;
    }
    // The face's own rise or fall under the velocity across it, and a drop from it to where the feet are.
    Vector3 Velocity = step->Character->_runtime._velocity;
    float FaceSpeed = -((Face.x * Velocity.x) + (Face.z * Velocity.z)) / Face.y;
    float Gravity = step->Gravity * step->Settings.GravityMultiplier;
    return Math_MaxFloat(moveSpeed, FaceSpeed - sqrtf(2.0f * Gravity * Sink));
}

/* After a walker's move from @p start: on the ground it follows the ground down a walkable slope (or within its
 * step-down distance) or leaves it (@p ground: what it walked on), keeping the vertical speed it moved with
 * (GetWalkOffSpeed); in the air it lands on support while not rising. */
static void UpdateWalkingGround(CharacterStep* step, Vector3Double start, Vector3Double* feet,
    const CharacterMove* move, const CharacterSupport* ground, float fallSpeed)
{
    WorldCharacterComponent* Character = step->Character;
    WorldCharacterState* State = &Character->_state;
    CharacterSupport Support;
    Vector3Double Settled;
    if ((ground != NULL) && State->IsGrounded)
    {
        if (move->HasStepped)
        {
            SetGround(Character, &move->StepSupport);
            return;
        }
        // How far it went across the ground since the last look: its move and the collision step's push. The ground's
        // own move this tick comes on top: the collision world still holds the ground where the last collision step
        // left it, so a rising lift is a tick lower there than its carry put the walker.
        float Across = GetHorizontalLength(GameMath_ToVector3Relative(*feet, start)) + GetHorizontalLength(step->Push);
        float Reach = GetGroundReach(&step->Settings, Across)
            + (Vector3Length(State->GroundVelocity) * step->DeltaSeconds);
        CharacterGround Ground = CharacterMotion_FollowGround(&step->Body, *feet, Reach, ground, &Settled, &Support);
        if (Ground == CharacterGround_Supported)
        {
            *feet = Settled;
            SetGround(Character, &Support);
            return;
        }
        if (Ground == CharacterGround_Unsure)
        {
            // A pushable pressing on its side hid the ground: it keeps the ground it had until the step parts them.
            return;
        }
        float MoveSpeed = (float)(feet->Y - start.Y) / step->DeltaSeconds;
        Character->_runtime._velocity.y = GetWalkOffSpeed(step, ground, *feet, MoveSpeed);
        LeaveGround(Character);
        QueueEvent(step->System, WorldCharacterEventKind_LeftGround, step->Id, 0U, 0.0f);
        return;
    }

    if ((Character->_runtime._velocity.y <= 0.0f)
        && (CharacterMotion_FollowGround(&step->Body, *feet, CHARACTER_LANDING_PROBE_DISTANCE, NULL, &Settled, &Support)
            == CharacterGround_Supported))
    {
        *feet = Settled;
        SetGround(Character, &Support);
        Character->_runtime._velocity.y = 0.0f;
        Character->_runtime._hasJumpedSinceGrounded = false;
        QueueEvent(step->System, WorldCharacterEventKind_Landed, step->Id, Support.ObjectId, fallSpeed);
    }
}

/* Moves the capsule by its velocity (along the ground while walking on it) and finds its ground afterwards. */
static void Move(CharacterStep* step, Vector3Double* feet)
{
    WorldCharacterState* State = &step->Character->_state;
    Vector3* Velocity = &step->Character->_runtime._velocity;
    bool IsWalking = State->Mode == CharacterMovementMode_Walking;
    bool IsOnGround = IsWalking && State->IsGrounded;
    float FallSpeed = Math_MaxFloat(0.0f, -Velocity->y);
    Vector3 Displacement = Vector3Scale(*Velocity, step->DeltaSeconds);
    CharacterSupport Ground = GetGround(step->Character);
    if (IsOnGround)
    {
        Displacement = CharacterMotion_ProjectOnGround(Displacement, State->GroundNormal);
    }
    Vector3Double Start = *feet;
    CharacterMove Moved = CharacterMotion_MoveAndSlide(&step->Body, *feet, Displacement, Velocity,
        IsOnGround ? &Ground : NULL);
    *feet = Moved.Feet;

    if (IsWalking)
    {
        UpdateWalkingGround(step, Start, feet, &Moved, IsOnGround ? &Ground : NULL, FallSpeed);
        return;
    }
    // Flying: no ground, but brains land birds by whether ground is right below.
    CharacterSupport Support;
    Vector3Double Below;
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

/* Eases the eyes toward the stance's eye height: fast at first, slowing as they near it (each moment they close the
 * same share of what is left), there after the crouch time from the other height. Eyes outside the two heights (the
 * settings changed: another kind of character, a script) move into their range at once. */
static void UpdateEyes(CharacterStep* step)
{
    WorldCharacterState* State = &step->Character->_state;
    const CharacterSettings* Settings = &step->Settings;
    float Target = (State->Stance == CharacterStance_Crouching) ? Settings->CrouchEyeHeight : Settings->EyeHeight;
    float Span = Settings->EyeHeight - Settings->CrouchEyeHeight;
    State->EyeHeight = Math_ClampFloat(State->EyeHeight, Math_MinFloat(Settings->EyeHeight, Settings->CrouchEyeHeight),
        Math_MaxFloat(Settings->EyeHeight, Settings->CrouchEyeHeight));
    float Eased = Target;
    if (Settings->CrouchTransitionSeconds > 0.0f)
    {
        // The rate that leaves the remainder's share of the whole way after the crouch time.
        float Rate = logf(1.0f / CHARACTER_EYE_EASE_REMAINDER) / Settings->CrouchTransitionSeconds;
        Eased = Target - ((Target - State->EyeHeight) * expf(-Rate * step->DeltaSeconds));
    }
    State->EyeHeight = (fabsf(Target - Eased) <= (CHARACTER_EYE_EASE_REMAINDER * fabsf(Span))) ? Target : Eased;
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

    Vector3Double Feet = WorldObject_GetWorldPosition(Object);
    if (!Runtime->_isStarted)
    {
        Start(&Step, &Feet);
    }
    Vector3 Push = ReadPush(&Step, Feet);
    Step.Push = Push;

    // The input's presses (a held request acts once), and a held jump again on the ground once the auto-jump delay
    // has passed since the last jump.
    Runtime->_secondsSinceJump = Math_MinFloat(Runtime->_secondsSinceJump + deltaSeconds, (float)CHARACTER_MAX_SECONDS);
    bool IsJumpPressed = Input->WantsJump && !Runtime->_wasJumpWanted;
    bool IsUsePressed = Input->WantsUse && !Runtime->_wasUseWanted;
    Runtime->_wasJumpWanted = Input->WantsJump;
    Runtime->_wasUseWanted = Input->WantsUse;
    if (IsJumpPressed || IsAutoJumpDue(&Step))
    {
        Runtime->_hasJumpRequest = true;
        Runtime->_jumpRequestSeconds = Step.Settings.JumpBufferSeconds;
    }

    UpdateMode(&Step);
    CarryWithGround(&Step, &Feet);
    Vector3Double Carried = Feet;
    UpdateStance(&Step, Feet);
    EnsureCollider(&Step);
    UpdateVelocity(&Step);
    Move(&Step, &Feet);
    // How fast it moved: its own move this update and the collision step's push since the last (the carry is the
    // ground's).
    State->Velocity = Vector3Scale(Vector3Add(GameMath_ToVector3Relative(Feet, Carried), Push), 1.0f / deltaSeconds);
    UpdateFacing(&Step);
    UpdateEyes(&Step);
    if (State->Mode == CharacterMovementMode_Walking)
    {
        State->IsTouchingGround = State->IsGrounded;
    }
    else
    {
        ClearGround(character);
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

Error WorldCharacters_Teleport(WorldCharacters* self, uint64_t objectId, Vector3Double feetPosition, float yaw)
{
    WorldComponent* Component = NULL;
    if ((self == NULL) || !World_TryGetComponent(self->_world, objectId, WorldComponentType_Character, &Component))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldCharacters_Teleport: self must not be NULL and the object must have a character component.");
    }
    if (!Vector3Double_IsFinite(feetPosition) || !isfinite(yaw))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldCharacters_Teleport: the position and the yaw must be finite.");
    }
    // Refused before anything changes: the object's position row would refuse a place beyond the limit (its local one,
    // under a parent: WorldObject.h) and leave the character where it was, without a word.
    Vector3Double Local;
    Vector3 LocalRotation;
    Vector3 LocalScale;
    if (!GetLocalTransform(WorldComponent_GetOwner(Component), feetPosition, yaw, &Local, &LocalRotation, &LocalScale))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "WorldCharacters_Teleport: the object's parent cannot be inverted (a zero scale): it cannot be placed.");
    }
    if ((fabs(Local.X) > WORLD_POSITION_LIMIT) || (fabs(Local.Y) > WORLD_POSITION_LIMIT)
        || (fabs(Local.Z) > WORLD_POSITION_LIMIT))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldCharacters_Teleport: the object's position there (under a parent, its local one) would pass "
            "WORLD_POSITION_LIMIT.");
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
    Step.Push = (Vector3){ 0.0f, 0.0f, 0.0f };
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

    Vector3Double Feet = feetPosition;
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
    Vector3Double Eye = WorldCharacterComponent_GetEyePosition(Character);
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
