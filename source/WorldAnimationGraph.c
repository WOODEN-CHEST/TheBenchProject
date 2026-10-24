#include <inttypes.h>
#include <math.h>
#include "WorldAnimationInternal.h"
#include "WorldCharacterComponent.h"
#include "WorldCharacters.h"
#include "WorldInternal.h"
#include "WorldObject.h"
#include "raylib/raymath.h"
#include "wr/WRString.h"

/*
 * The animation system's graphs (T30; WorldAnimation.h, "Graphs"): binding the graph an animator plays to its skeleton
 * and libraries, feeding it its character's state and events every tick, advancing it (socket conditions asked of the
 * world, events handed to their effects and queued), and the functions code uses on it: parameters, triggers, states.
 */

// The character's events fire the graph's built-in events of the same index.
_Static_assert((int)WorldCharacterEventKind_Jumped == (int)AnimationGraphEvent_Jumped, "event kinds must match");
_Static_assert((int)WorldCharacterEventKind_Landed == (int)AnimationGraphEvent_Landed, "event kinds must match");
_Static_assert((int)WorldCharacterEventKind_LeftGround == (int)AnimationGraphEvent_LeftGround, "event kinds must match");
_Static_assert((int)WorldCharacterEventKind_Crouched == (int)AnimationGraphEvent_Crouched, "event kinds must match");
_Static_assert((int)WorldCharacterEventKind_StoodUp == (int)AnimationGraphEvent_StoodUp, "event kinds must match");
_Static_assert((int)WorldCharacterEventKind_TookOff == (int)AnimationGraphEvent_TookOff, "event kinds must match");
_Static_assert((int)WorldCharacterEventKind_StoppedFlying == (int)AnimationGraphEvent_StoppedFlying,
    "event kinds must match");
_Static_assert((int)WorldCharacterEventKind_Used == (int)AnimationGraphEvent_Used, "event kinds must match");


// Macros.
/** Every part of the graph's block starts at a multiple of this. */
#define BLOCK_ALIGNMENT ((size_t)8)
/** How many of the character's event kinds there are (the first built-in events of a graph). */
#define CHARACTER_EVENT_KIND_COUNT 8U


// Types.
/* What the graph's advance hands its events with. */
typedef struct EventContextStruct
{
    WorldAnimation* System;
    WorldAnimatorComponent* Animator;
    WorldAnimatorRuntime* Runtime;
} EventContext;


// Static functions.
static size_t AlignUp(size_t value)
{
    return (value + (BLOCK_ALIGNMENT - 1U)) & ~(BLOCK_ALIGNMENT - 1U);
}

/* An angle in (-pi, pi]. */
static float WrapRadians(float angle)
{
    float Wrapped = fmodf(angle + PI, 2.0f * PI);
    Wrapped = (Wrapped < 0.0f) ? (Wrapped + (2.0f * PI)) : Wrapped;
    return Wrapped - PI;
}

static void SetBuiltIn(AnimationGraphInstance* instance, AnimationGraphBuiltIn parameter, float value)
{
    AnimationGraphInstance_SetParameter(instance, (uint32_t)parameter, value);
}

static float FromBool(bool value)
{
    return value ? 1.0f : 0.0f;
}

/* The character's state of its last update into the graph's built-in parameters, and its events into the graph's. An
 * object without a character leaves them to code. */
static void FeedCharacter(WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime)
{
    AnimationGraphInstance* Instance = &runtime->GraphInstance;
    SetBuiltIn(Instance, AnimationGraphBuiltIn_IsInAction, FromBool(runtime->Action.Phase != WorldAnimationActionPhase_None));
    const WorldObject* Owner = WorldComponent_GetOwner(&animator->Base);
    WorldComponent* Component = WorldObject_GetComponent(Owner, WorldComponentType_Character);
    if (Component == NULL)
    {
        return;
    }
    const WorldCharacterComponent* Character = WorldCharacterComponent_FromComponent(Component);
    const WorldCharacterState* State = WorldCharacterComponent_GetState(Character);
    const CharacterInput* Input = WorldCharacterComponent_GetInput(Character);
    Vector3 Velocity = State->Velocity;
    // The body's axes: its facing, and its right (a yaw grows toward its left, as GameCamera's).
    Vector3 Forward = { sinf(State->FacingYaw), 0.0f, cosf(State->FacingYaw) };
    Vector3 Right = { -cosf(State->FacingYaw), 0.0f, sinf(State->FacingYaw) };
    SetBuiltIn(Instance, AnimationGraphBuiltIn_Speed, sqrtf((Velocity.x * Velocity.x) + (Velocity.z * Velocity.z)));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_Speed3d, Vector3Length(Velocity));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_ForwardSpeed, Vector3DotProduct(Velocity, Forward));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_SideSpeed, Vector3DotProduct(Velocity, Right));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_VerticalSpeed, Velocity.y);
    SetBuiltIn(Instance, AnimationGraphBuiltIn_IsGrounded, FromBool(State->IsGrounded));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_IsTouchingGround, FromBool(State->IsTouchingGround));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_IsFlying, FromBool(State->Mode == CharacterMovementMode_Flying));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_IsCrouching, FromBool(State->Stance == CharacterStance_Crouching));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_CrouchAmount, State->CrouchAmount);
    SetBuiltIn(Instance, AnimationGraphBuiltIn_TimeInAir, State->TimeInAir);
    SetBuiltIn(Instance, AnimationGraphBuiltIn_TurnRate, State->TurnRate * RAD2DEG);
    SetBuiltIn(Instance, AnimationGraphBuiltIn_LookYaw, WrapRadians(Input->LookYaw - State->FacingYaw) * RAD2DEG);
    SetBuiltIn(Instance, AnimationGraphBuiltIn_LookPitch, Input->LookPitch * RAD2DEG);
    SetBuiltIn(Instance, AnimationGraphBuiltIn_IsStandUpBlocked, FromBool(State->IsStandUpBlocked));
    SetBuiltIn(Instance, AnimationGraphBuiltIn_LandingSpeed, State->LastLandingSpeed);
    SetBuiltIn(Instance, AnimationGraphBuiltIn_MoveAmount, Vector3Length(Input->MoveDirection));
    for (uint32_t Kind = 0U; Kind < CHARACTER_EVENT_KIND_COUNT; Kind++)
    {
        if (((State->Events >> Kind) & 1U) != 0U)
        {
            AnimationGraphInstance_FireEvent(Instance, Kind);
        }
    }
}

/* Socket conditions: whether an object hangs on a socket of the animator's object. */
static bool IsSocketHolding(void* user, const unsigned char* socket)
{
    const EventContext* Context = user;
    const WorldObject* Owner = WorldComponent_GetOwner(&Context->Animator->Base);
    size_t Length = StringUTF8_GetByteLength(socket);
    for (const WorldObject* Child = WorldObject_GetFirstChild(Owner); Child != NULL; Child = WorldObject_GetNextSibling(Child))
    {
        const unsigned char* Name = WorldObject_GetParentSocket(Child);
        if ((Name != NULL) && (StringUTF8_GetByteLength(Name) == Length) && Memory_IsEqual(Name, socket, Length))
        {
            return true;
        }
    }
    return false;
}

/* A graph event: its effects, then the queue. */
static void OnGraphEvent(void* user, uint32_t layer, uint32_t state, const AnimationGraphEventRecord* event)
{
    (void)layer;
    (void)state;
    EventContext* Context = user;
    WorldAnimationInternal_ApplyEventEffects(Context->System, Context->Animator, Context->Runtime, event);
    const AnimationGraph* Graph = Context->Runtime->Graph;
    WorldAnimationInternal_QueueEvent(Context->System, WorldAnimationEventKind_Graph,
        WorldObject_GetId(WorldComponent_GetOwner(&Context->Animator->Base)), AnimationGraph_GetString(Graph, event->_name),
        Context->Runtime->Action.TargetId);
}

/* The playing animator's graph, or an error naming the function. */
static Error FindGraph(WorldAnimation* self, uint64_t objectId, const unsigned char* name, const char* function,
    WorldAnimatorRuntime** outRuntime)
{
    if ((self == NULL) || (name == NULL))
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "%s: self and the name must not be NULL.", function);
    }
    WorldAnimatorRuntime* Runtime = NULL;
    if ((WorldAnimationInternal_FindAnimator(self, objectId, &Runtime) == NULL) || (Runtime->GraphBlock == NULL))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "%s: object %" PRIu64 " has no animator playing a graph.",
            function, objectId);
    }
    *outRuntime = Runtime;
    return Error_CreateSuccess();
}


// Internal functions (declared in WorldAnimationInternal.h).
void WorldAnimationInternal_ReleaseGraph(WorldAnimatorRuntime* runtime)
{
    Memory_Free(runtime->GraphBlock);
    runtime->GraphBlock = NULL;
    runtime->Graph = NULL;
    Memory_Free(runtime->BoundGraphName);
    runtime->BoundGraphName = NULL;
    runtime->BoundOverridesGraph = false;
}

bool WorldAnimationInternal_DoesGraphNeedBind(const WorldAnimatorComponent* animator, const WorldAnimatorRuntime* runtime)
{
    if (animator->_overridesAnimationGraph != runtime->BoundOverridesGraph)
    {
        return true;
    }
    return animator->_overridesAnimationGraph
        && !WorldInternal_AreStringsEqual(animator->_animationGraph, runtime->BoundGraphName);
}

void WorldAnimationInternal_BindGraph(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime)
{
    WorldAnimationInternal_ReleaseGraph(runtime);
    runtime->HasWarnedGraph = false;
    runtime->BoundOverridesGraph = animator->_overridesAnimationGraph;
    runtime->BoundGraphName = WorldInternal_CloneString(animator->_animationGraph);
    const AnimationGraph* Graph = NULL;
    if (!animator->_overridesAnimationGraph)
    {
        Graph = (runtime->WorldModel != NULL) ? WorldModel_GetAnimationGraph(runtime->WorldModel) : NULL;
    }
    else if ((animator->_animationGraph != NULL) && (self->_assets != NULL))
    {
        AnimationGraph* Loaded = NULL;
        Error Result = AssetManager_LoadAnimationGraph(self->_assets, animator->_animationGraph, self->_assetUser, &Loaded);
        if (Result.Code == ErrorCode_Success)
        {
            Graph = Loaded;
        }
        else
        {
            WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedGraph, "plays no graph: animation graph \"%s\" "
                "could not be loaded (%s).", (const char*)animator->_animationGraph,
                (Result.Message != NULL) ? (const char*)Result.Message : "no details");
            Error_Deconstruct(&Result);
        }
    }
    if ((Graph == NULL) || (runtime->Skeleton == NULL))
    {
        return;
    }
    size_t BindingBytes = AlignUp(AnimationGraphBinding_GetStorageSize(Graph, runtime->Skeleton));
    size_t InstanceBytes = AnimationGraphInstance_GetStorageSize(Graph);
    unsigned char* Block = Memory_Allocate(BindingBytes + InstanceBytes);
    runtime->GraphBlock = Block;
    runtime->Graph = Graph;
    AnimationGraphBinding_Build(Graph, runtime->Skeleton, runtime->Bindings, runtime->LibraryCount, Block,
        &runtime->GraphBinding);
    AnimationGraphInstance_Construct(&runtime->GraphInstance, Graph, Block + BindingBytes);
    const AnimationGraphBinding* Binding = &runtime->GraphBinding;
    if (Binding->MissingClipCount > 0U)
    {
        WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedGraph, "plays its graph without %" PRIu32 " of its "
            "clips, \"%s\" the first: none of its animation libraries has them (those states play nothing).",
            Binding->MissingClipCount, (const char*)Binding->FirstMissingClip);
    }
    if (Binding->MissingBoneCount > 0U)
    {
        WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedGraph, "plays its graph without %" PRIu32 " of the "
            "bones its masks and look-at name, \"%s\" the first: its skeleton has no such bones.",
            Binding->MissingBoneCount, (const char*)Binding->FirstMissingBone);
    }
}

void WorldAnimationInternal_UpdateGraph(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime, double seconds)
{
    FeedCharacter(animator, runtime);
    EventContext Events = { .System = self, .Animator = animator, .Runtime = runtime };
    AnimationGraphContext Context = { .IsSocketHolding = IsSocketHolding, .OnEvent = OnGraphEvent, .User = &Events };
    AnimationGraphInstance_Advance(&runtime->GraphInstance, &runtime->GraphBinding, seconds, &Context);
    WorldAnimationInternal_UpdateAction(self, animator, runtime, seconds);
}

void WorldAnimationInternal_QueueEvent(WorldAnimation* self, WorldAnimationEventKind kind, uint64_t objectId,
    const unsigned char* name, uint64_t targetId)
{
    WorldAnimationEvent Event;
    Memory_Zero(&Event, sizeof(Event));
    Event.Kind = kind;
    Event.ObjectId = objectId;
    Event.TargetId = targetId;
    Event.Tick = self->_tick;
    size_t Length = (name != NULL) ? StringUTF8_GetByteLength(name) : 0U;
    Length = (Length < (WORLD_ANIMATION_EVENT_NAME_CAPACITY - 1U)) ? Length : (WORLD_ANIMATION_EVENT_NAME_CAPACITY - 1U);
    if (Length > 0U)
    {
        Memory_Copy(name, Event.Name, Length);
    }
    // The buffer grows (running out of memory aborts in Memory_Allocate), so adding cannot fail.
    (void)GenericBuffer_AddLast(&self->_events, &Event);
}


// Public functions (declared in WorldAnimation.h).
Error WorldAnimation_SetParameter(WorldAnimation* self, uint64_t objectId, const unsigned char* name, float value)
{
    WorldAnimatorRuntime* Runtime = NULL;
    Error Result = FindGraph(self, objectId, name, "WorldAnimation_SetParameter", &Runtime);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!isfinite(value))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldAnimation_SetParameter: the value must be finite.");
    }
    uint32_t Index = 0U;
    if (!AnimationGraph_TryFindParameter(Runtime->Graph, name, &Index))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WorldAnimation_SetParameter: the graph of object %" PRIu64
            " has no parameter \"%s\".", objectId, (const char*)name);
    }
    AnimationGraphInstance_SetParameter(&Runtime->GraphInstance, Index, value);
    return Error_CreateSuccess();
}

bool WorldAnimation_TryGetParameter(WorldAnimation* self, uint64_t objectId, const unsigned char* name,
    float* outValue)
{
    WorldAnimatorRuntime* Runtime = NULL;
    if ((outValue == NULL) || (name == NULL) || (WorldAnimationInternal_FindAnimator(self, objectId, &Runtime) == NULL)
        || (Runtime->GraphBlock == NULL))
    {
        return false;
    }
    uint32_t Index = 0U;
    if (!AnimationGraph_TryFindParameter(Runtime->Graph, name, &Index))
    {
        return false;
    }
    *outValue = AnimationGraphInstance_GetParameter(&Runtime->GraphInstance, Index);
    return true;
}

Error WorldAnimation_FireTrigger(WorldAnimation* self, uint64_t objectId, const unsigned char* name)
{
    WorldAnimatorRuntime* Runtime = NULL;
    Error Result = FindGraph(self, objectId, name, "WorldAnimation_FireTrigger", &Runtime);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const AnimationGraphParameter* Trigger = AnimationGraph_FindCustomParameter(Runtime->Graph, name);
    if ((Trigger == NULL) || (Trigger->_type != AnimationGraphValueType_Trigger))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WorldAnimation_FireTrigger: the graph of object %" PRIu64
            " has no trigger \"%s\".", objectId, (const char*)name);
    }
    AnimationGraphInstance_FireEvent(&Runtime->GraphInstance, Trigger->_index);
    return Error_CreateSuccess();
}

bool WorldAnimation_TryGetState(WorldAnimation* self, uint64_t objectId, const unsigned char* layer,
    const unsigned char** outState)
{
    WorldAnimatorRuntime* Runtime = NULL;
    if ((outState == NULL) || (WorldAnimationInternal_FindAnimator(self, objectId, &Runtime) == NULL)
        || (Runtime->GraphBlock == NULL))
    {
        return false;
    }
    uint32_t Layer = 0U;
    if ((layer != NULL) && !AnimationGraph_TryFindLayer(Runtime->Graph, layer, &Layer))
    {
        return false;
    }
    const AnimationGraphLayer* Record = AnimationGraph_GetLayer(Runtime->Graph, Layer);
    uint32_t State = AnimationGraphInstance_GetCurrentState(&Runtime->GraphInstance, Layer);
    *outState = AnimationGraph_GetString(Runtime->Graph, AnimationGraph_GetLayerState(Runtime->Graph, Record, State)->_name);
    return true;
}
