#include <inttypes.h>
#include <math.h>
#include "WorldAnimationInternal.h"
#include "WorldCharacterComponent.h"
#include "WorldInternal.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "WorldTransform.h"
#include "GameMath.h"
#include "raylib/raymath.h"
#include "wr/WRString.h"

/*
 * The animation system's special actions (WorldAnimation.h, "Actions"): starting one (its target and anchor
 * checked, its character's movement locked and sent to the anchor), following it (an approach that arrives enters the
 * action's first state; a playing action ends when its layer leaves its states), ending it, and the effects of the
 * events its states fire: hanging the action's target on a socket of the object, and putting what hangs on a socket
 * back in the world.
 */


// Macros.
/** The most objects one socket's detach puts back in the world at once (more stay; there is rarely more than one). */
#define MAX_DETACHED_OBJECTS 16U


// Static functions: the character's side.
/* The character component of the animator's object, or NULL. */
static WorldCharacterComponent* GetCharacter(const WorldAnimatorComponent* animator)
{
    WorldComponent* Component = WorldObject_GetComponent(WorldComponent_GetOwner(&animator->Base),
        WorldComponentType_Character);
    return (Component != NULL) ? WorldCharacterComponent_FromComponent(Component) : NULL;
}

/* An action's movement lock as the character takes it (the two enums share their values). */
static CharacterMovementLock ToCharacterLock(AnimationGraphMovementLock lock)
{
    switch (lock)
    {
        case AnimationGraphMovementLock_Free: return CharacterMovementLock_Free;
        case AnimationGraphMovementLock_NoMoving: return CharacterMovementLock_NoMoving;
        case AnimationGraphMovementLock_NoTurning: return CharacterMovementLock_NoTurning;
        case AnimationGraphMovementLock_Locked: return CharacterMovementLock_Locked;
    }
    return CharacterMovementLock_Free;
}

/* Hands the character its action's control (NULL: frees it). Errors cannot come from the values made here. */
static void SetControl(WorldAnimatorComponent* animator, const CharacterActionControl* control)
{
    WorldCharacterComponent* Character = GetCharacter(animator);
    if (Character != NULL)
    {
        Error Result = WorldCharacterComponent_SetActionControl(Character, control);
        Error_Deconstruct(&Result);
    }
}


// Static functions: sockets of other objects.
/* The world model an object's model component names, loaded under the system's asset user; NULL without one. */
static const WorldModel* LoadWorldModelOf(WorldAnimation* self, const WorldObject* object)
{
    WorldComponent* Component = WorldObject_GetComponent(object, WorldComponentType_Model);
    const unsigned char* Name = (Component != NULL)
        ? WorldModelComponent_GetWorldModel(WorldModelComponent_FromComponent(Component)) : NULL;
    if ((Name == NULL) || (self->_assets == NULL))
    {
        return NULL;
    }
    WorldModel* Loaded = NULL;
    Error Result = AssetManager_LoadWorldModel(self->_assets, Name, self->_assetUser, &Loaded);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return NULL;
    }
    return Loaded;
}

/* A socket's frame in an object's own space: from its animator's pose when it is posed, otherwise from its world model
 * as bound (a bench's seat, a cup's handle). */
static bool TryGetSocketFrame(WorldAnimation* self, const WorldObject* object, const unsigned char* socket,
    Matrix* outFrame)
{
    WorldComponent* Component = WorldObject_GetComponent(object, WorldComponentType_Animator);
    const WorldAnimatorPose* Pose = (Component != NULL)
        ? WorldAnimatorComponent_GetPose(WorldAnimatorComponent_FromComponent(Component)) : NULL;
    size_t Length = StringUTF8_GetByteLength(socket);
    for (uint32_t Index = 0U; (Pose != NULL) && (Index < Pose->_socketCount); Index++)
    {
        const unsigned char* Name = Pose->_sockets[Index]._name;
        if ((StringUTF8_GetByteLength(Name) == Length) && Memory_IsEqual(Name, socket, Length))
        {
            *outFrame = Pose->_socketMatrices[Index];
            return true;
        }
    }
    const WorldModel* Definition = LoadWorldModelOf(self, object);
    uint32_t Index = 0U;
    if ((Definition == NULL) || !WorldModel_TryFindSocket(Definition, socket, &Index))
    {
        return false;
    }
    *outFrame = WorldModel_GetSocketMatrix(Definition, Index, NULL);
    return true;
}


// Static functions: starting and following.
static const AnimationGraphAction* GetAction(const WorldAnimatorRuntime* runtime)
{
    return AnimationGraph_GetAction(runtime->Graph, runtime->Action.Index);
}

static const unsigned char* GetActionName(const WorldAnimatorRuntime* runtime)
{
    return AnimationGraph_GetString(runtime->Graph, GetAction(runtime)->_name);
}

static uint64_t GetOwnerId(const WorldAnimatorComponent* animator)
{
    return WorldObject_GetId(WorldComponent_GetOwner(&animator->Base));
}

/* The action enters its first state (restarted) and plays. */
static void EnterAction(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime)
{
    const AnimationGraphAction* Action = GetAction(runtime);
    AnimationGraphInstance_EnterState(&runtime->GraphInstance, Action->_layer,
        AnimationGraph_GetActionState(runtime->Graph, Action, 0U), Action->_fadeSeconds, true);
    runtime->Action.Phase = WorldAnimationActionPhase_Playing;
    WorldAnimationInternal_QueueEvent(self, WorldAnimationEventKind_ActionStarted, GetOwnerId(animator),
        GetActionName(runtime), runtime->Action.TargetId);
}

/* Whether the action's layer is in one of its states. */
static bool IsInActionStates(const WorldAnimatorRuntime* runtime)
{
    const AnimationGraphAction* Action = GetAction(runtime);
    uint32_t Current = AnimationGraphInstance_GetCurrentState(&runtime->GraphInstance, Action->_layer);
    for (uint32_t Index = 0U; Index < Action->_stateCount; Index++)
    {
        if (AnimationGraph_GetActionState(runtime->Graph, Action, Index) == Current)
        {
            return true;
        }
    }
    return false;
}

/* Where an anchored action sends the character: the target's socket in the world (its place, and the yaw its +Z
 * faces), checked against the action's reach from the character's feet. */
static Error FindAnchor(WorldAnimation* self, const WorldAnimatorComponent* animator, const AnimationGraph* graph,
    const AnimationGraphAction* action, const WorldObject* target, CharacterActionControl* outControl)
{
    const unsigned char* Socket = AnimationGraph_GetString(graph, action->_anchorSocket);
    Matrix Frame;
    if (!TryGetSocketFrame(self, target, Socket, &Frame))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WorldAnimation_StartAction: object %" PRIu64 " has no "
            "socket \"%s\" to anchor action \"%s\" on.", WorldObject_GetId(target), (const char*)Socket,
            (const char*)AnimationGraph_GetString(graph, action->_name));
    }
    Matrix4Double FrameDouble = GameMath_FromMatrix(Frame);
    Matrix4Double TargetWorld = WorldObject_GetWorldMatrix(target);
    Matrix4Double AnchorWorld = Matrix4Double_Multiply(&FrameDouble, &TargetWorld);
    Vector3Double Facing = Matrix4Double_TransformDirection(&AnchorWorld, Vector3Double_Create(0.0, 0.0, 1.0));
    Vector3Double Position = Matrix4Double_GetTranslation(&AnchorWorld);
    Vector3Double Feet = WorldObject_GetWorldPosition(WorldComponent_GetOwner(&animator->Base));
    double DeltaX = Position.X - Feet.X;
    double DeltaZ = Position.Z - Feet.Z;
    if (sqrt((DeltaX * DeltaX) + (DeltaZ * DeltaZ)) > (double)action->_anchorDistance)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WorldAnimation_StartAction: the anchor of action \"%s\" is "
            "more than %g m from object %" PRIu64 "'s feet.", (const char*)AnimationGraph_GetString(graph, action->_name),
            (double)action->_anchorDistance, GetOwnerId(animator));
    }
    outControl->HasAnchor = true;
    outControl->AnchorPosition = Position;
    outControl->AnchorYaw = (float)atan2(Facing.X, Facing.Z);
    outControl->ApproachSeconds = action->_approachSeconds;
    return Error_CreateSuccess();
}

/* Checks the target an action takes. */
static Error CheckTarget(WorldAnimation* self, const AnimationGraph* graph, const AnimationGraphAction* action,
    uint64_t targetId, WorldObject** outTarget)
{
    *outTarget = NULL;
    const char* Name = (const char*)AnimationGraph_GetString(graph, action->_name);
    if ((action->_targetRule == AnimationGraphTargetRule_Required) && (targetId == 0U))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "WorldAnimation_StartAction: action \"%s\" needs a target.",
            Name);
    }
    if ((action->_targetRule == AnimationGraphTargetRule_None) && (targetId != 0U))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "WorldAnimation_StartAction: action \"%s\" takes no target.",
            Name);
    }
    if ((targetId != 0U) && !World_TryGetObject(self->_world, targetId, outTarget))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WorldAnimation_StartAction: no object has the id %" PRIu64
            " to be the target of action \"%s\".", targetId, Name);
    }
    return Error_CreateSuccess();
}


// Static functions: effects.
/* Hangs the action's target on a socket of the object, gripped by the target's own socket when the event names one (its
 * local transform becomes that grip's inverse, so the grip lands on the socket); it becomes movable. */
static void Attach(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime,
    const AnimationGraphEventRecord* event)
{
    const AnimationGraph* Graph = runtime->Graph;
    WorldObject* Target = NULL;
    uint64_t TargetId = runtime->Action.TargetId;
    const unsigned char* Socket = AnimationGraph_GetString(Graph, event->_attachSocket);
    if ((TargetId == 0U) || !World_TryGetObject(self->_world, TargetId, &Target))
    {
        WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedEffect, "has event \"%s\" attach to socket \"%s\", "
            "but its action has no target.", (const char*)AnimationGraph_GetString(Graph, event->_name),
            (const char*)Socket);
        return;
    }
    Matrix Grip = MatrixIdentity();
    if ((event->_gripSocket != ANIMATION_GRAPH_NONE)
        && !TryGetSocketFrame(self, Target, AnimationGraph_GetString(Graph, event->_gripSocket), &Grip))
    {
        WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedEffect, "grips object %" PRIu64 " by socket \"%s\", "
            "which it lacks; it is held by its origin.", TargetId,
            (const char*)AnimationGraph_GetString(Graph, event->_gripSocket));
    }
    // Its place on the socket: the grip's inverse. The target keeps its own scale, which applies before its rotation and
    // position, so the grip's offset is inverted as the target scales it: the grip lands on the socket.
    Vector3 TargetScale = WorldObject_GetScale(Target);
    Grip.m12 *= TargetScale.x;
    Grip.m13 *= TargetScale.y;
    Grip.m14 *= TargetScale.z;
    Matrix4Double Inverse = GameMath_FromMatrix(MatrixInvert(Grip));
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
    WorldTransform_Decompose(&Inverse, (Vector3){ 1.0f, 1.0f, 1.0f }, &Position, &Rotation, &Scale);
    Error Result = World_SetParentSocket(self->_world, TargetId, GetOwnerId(animator), Socket, false);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldObject_SetPosition(Target, Position);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldObject_SetRotation(Target, Rotation);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldObject_SetMobility(Target, WorldMobility_Movable);
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedEffect, "could not attach object %" PRIu64 " to "
            "socket \"%s\" (%s).", TargetId, (const char*)Socket,
            (Result.Message != NULL) ? (const char*)Result.Message : "no details");
        Error_Deconstruct(&Result);
    }
}

/* Puts what hangs on a socket of the object back in the world, where it is. */
static void Detach(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime,
    const AnimationGraphEventRecord* event)
{
    const unsigned char* Socket = AnimationGraph_GetString(runtime->Graph, event->_detachSocket);
    size_t Length = StringUTF8_GetByteLength(Socket);
    uint64_t Detached[MAX_DETACHED_OBJECTS];
    uint32_t Count = 0U;
    const WorldObject* Owner = WorldComponent_GetOwner(&animator->Base);
    for (const WorldObject* Child = WorldObject_GetFirstChild(Owner); (Child != NULL) && (Count < MAX_DETACHED_OBJECTS);
        Child = WorldObject_GetNextSibling(Child))
    {
        const unsigned char* Name = WorldObject_GetParentSocket(Child);
        if ((Name != NULL) && (StringUTF8_GetByteLength(Name) == Length) && Memory_IsEqual(Name, Socket, Length))
        {
            Detached[Count] = WorldObject_GetId(Child);
            Count++;
        }
    }
    // Collected first: putting a child back in the world changes the list walked above.
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        Error Result = World_SetParentSocket(self->_world, Detached[Index], 0U, NULL, true);
        if (Result.Code != ErrorCode_Success)
        {
            WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedEffect, "could not put object %" PRIu64 " back "
                "in the world from socket \"%s\" (%s).", Detached[Index], (const char*)Socket,
                (Result.Message != NULL) ? (const char*)Result.Message : "no details");
            Error_Deconstruct(&Result);
        }
    }
}


// Internal functions (declared in WorldAnimationInternal.h).
void WorldAnimationInternal_StopAction(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime, WorldAnimationEventKind kind)
{
    if (runtime->Action.Phase == WorldAnimationActionPhase_None)
    {
        return;
    }
    if (runtime->Graph != NULL)
    {
        WorldAnimationInternal_QueueEvent(self, kind, GetOwnerId(animator), GetActionName(runtime),
            runtime->Action.TargetId);
    }
    SetControl(animator, NULL);
    runtime->Action = (WorldAnimationAction){ .Phase = WorldAnimationActionPhase_None };
}

void WorldAnimationInternal_UpdateAction(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime, double seconds)
{
    WorldAnimationAction* Action = &runtime->Action;
    switch (Action->Phase)
    {
        case WorldAnimationActionPhase_None:
            return;
        case WorldAnimationActionPhase_Approaching:
        {
            Action->Seconds += (float)seconds;
            const WorldCharacterComponent* Character = GetCharacter(animator);
            CharacterApproach Approach = (Character != NULL) ? WorldCharacterComponent_GetState(Character)->Approach
                : CharacterApproach_Failed;
            if (Action->IsEndRequested)
            {
                WorldAnimationInternal_StopAction(self, animator, runtime, WorldAnimationEventKind_ActionEnded);
            }
            else if (Approach == CharacterApproach_Arrived)
            {
                EnterAction(self, animator, runtime);
            }
            else if (Approach != CharacterApproach_Approaching)
            {
                WorldAnimationInternal_StopAction(self, animator, runtime, WorldAnimationEventKind_ActionFailed);
            }
            return;
        }
        case WorldAnimationActionPhase_Playing:
            Action->Seconds += (float)seconds;
            if (!IsInActionStates(runtime))
            {
                WorldAnimationInternal_StopAction(self, animator, runtime, WorldAnimationEventKind_ActionEnded);
            }
            return;
    }
}

void WorldAnimationInternal_ApplyEventEffects(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime, const AnimationGraphEventRecord* event)
{
    if (event->_attachSocket != ANIMATION_GRAPH_NONE)
    {
        Attach(self, animator, runtime, event);
    }
    if (event->_detachSocket != ANIMATION_GRAPH_NONE)
    {
        Detach(self, animator, runtime, event);
    }
}


// Public functions (declared in WorldAnimation.h).
Error WorldAnimation_StartAction(WorldAnimation* self, uint64_t objectId, const unsigned char* action,
    uint64_t targetId)
{
    if ((self == NULL) || (action == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldAnimation_StartAction: self and action must not be "
            "NULL.");
    }
    WorldAnimatorRuntime* Runtime = NULL;
    WorldAnimatorComponent* Animator = WorldAnimationInternal_FindAnimator(self, objectId, &Runtime);
    if ((Animator == NULL) || (Runtime->GraphBlock == NULL))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "WorldAnimation_StartAction: object %" PRIu64 " has no "
            "animator playing a graph.", objectId);
    }
    uint32_t Index = 0U;
    if (!AnimationGraph_TryFindAction(Runtime->Graph, action, &Index))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WorldAnimation_StartAction: the graph of object %" PRIu64
            " has no action \"%s\".", objectId, (const char*)action);
    }
    const AnimationGraphAction* Record = AnimationGraph_GetAction(Runtime->Graph, Index);
    // An action playing on another layer would stay in its states with nothing to take it out: it ends first.
    if ((Runtime->Action.Phase == WorldAnimationActionPhase_Playing) && (GetAction(Runtime)->_layer != Record->_layer))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "WorldAnimation_StartAction: object %" PRIu64 " plays action "
            "\"%s\" on another layer; end it first.", objectId, (const char*)GetActionName(Runtime));
    }
    WorldObject* Target = NULL;
    Error Result = CheckTarget(self, Runtime->Graph, Record, targetId, &Target);
    CharacterActionControl Control = CharacterActionControl_CreateFree();
    Control.Lock = ToCharacterLock(Record->_movementLock);
    if ((Result.Code == ErrorCode_Success) && Record->_hasAnchor)
    {
        Result = (GetCharacter(Animator) == NULL)
            ? Error_Construct4(ErrorCode_InvalidOperation, "WorldAnimation_StartAction: action \"%s\" has an anchor, "
                "but object %" PRIu64 " is no character to go there.", (const char*)action, objectId)
            : FindAnchor(self, Animator, Runtime->Graph, Record, Target, &Control);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WorldAnimationInternal_StopAction(self, Animator, Runtime, WorldAnimationEventKind_ActionEnded);
    Runtime->Action = (WorldAnimationAction)
    {
        .Phase = Record->_hasAnchor ? WorldAnimationActionPhase_Approaching : WorldAnimationActionPhase_Playing,
        .Index = Index, .TargetId = targetId, .Seconds = 0.0f, .IsEndRequested = false
    };
    SetControl(Animator, &Control);
    if (!Record->_hasAnchor)
    {
        EnterAction(self, Animator, Runtime);
    }
    Runtime->IsPoseStale = true;
    return Error_CreateSuccess();
}

bool WorldAnimation_EndAction(WorldAnimation* self, uint64_t objectId)
{
    WorldAnimatorRuntime* Runtime = NULL;
    WorldAnimatorComponent* Animator = WorldAnimationInternal_FindAnimator(self, objectId, &Runtime);
    if ((Animator == NULL) || (Runtime->Action.Phase == WorldAnimationActionPhase_None))
    {
        return false;
    }
    if (Runtime->Action.Phase == WorldAnimationActionPhase_Approaching)
    {
        Runtime->Action.IsEndRequested = true;
        WorldAnimationInternal_StopAction(self, Animator, Runtime, WorldAnimationEventKind_ActionEnded);
        return true;
    }
    AnimationGraphInstance_FireEvent(&Runtime->GraphInstance, (uint32_t)AnimationGraphEvent_EndAction);
    return true;
}

bool WorldAnimation_TryGetAction(WorldAnimation* self, uint64_t objectId, const unsigned char** outAction,
    uint64_t* outTargetId)
{
    WorldAnimatorRuntime* Runtime = NULL;
    WorldAnimatorComponent* Animator = WorldAnimationInternal_FindAnimator(self, objectId, &Runtime);
    if ((Animator == NULL) || (Runtime->Action.Phase == WorldAnimationActionPhase_None))
    {
        return false;
    }
    if (outAction != NULL)
    {
        *outAction = GetActionName(Runtime);
    }
    if (outTargetId != NULL)
    {
        *outTargetId = Runtime->Action.TargetId;
    }
    return true;
}
