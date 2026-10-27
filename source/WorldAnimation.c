#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldAnimation.h"
#include "WorldAnimationInternal.h"
#include "WorldAnimatorComponent.h"
#include "WorldInternal.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "GameMath.h"
#include "GameModel.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRStopwatch.h"

/*
 * The animation system (WorldAnimation.h): binds animators to their world models' skeletons and libraries, applies their
 * rows and advances their clips once a tick, and poses them once per shown frame. Per-animator state is the component's
 * runtime (WorldAnimationInternal.h); the system keeps its asset user, scratch room for one pose (grown to the largest
 * skeleton it posed) and its queued events. Graphs are bound and advanced by WorldAnimationGraph.c, actions by
 * WorldAnimationActions.c.
 */


// Macros.
/** Room for one warning's own text (after the animator's prefix). */
#define MESSAGE_CAPACITY ((size_t)512)
/** Nanoseconds in a microsecond. */
#define NANOSECONDS_PER_MICROSECOND 1000.0


/** How many events the queue starts with room for. */
#define INITIAL_EVENT_CAPACITY 16U


// Static functions: messages.
void WorldAnimationInternal_Warn(const WorldAnimation* self, const WorldAnimatorComponent* animator, bool* isWarned,
    const char* format, ...)
{
    if ((self->_logger == NULL) || *isWarned)
    {
        return;
    }
    *isWarned = true;
    char Text[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    const WorldObject* Owner = WorldComponent_GetOwner(&animator->Base);
    const unsigned char* Name = WorldObject_GetName(Owner);
    Error LogResult = Logger_LogWarningFormatted(self->_logger, (const unsigned char*)u8"WorldAnimation: the animator of "
        u8"object %" PRIu64 u8" (\"%s\") %s", WorldObject_GetId(Owner), (Name != NULL) ? (const char*)Name : "", Text);
    Error_Deconstruct(&LogResult);
}

/* An error's message for a warning. */
static const char* GetErrorText(const Error* error)
{
    return (error->Message != NULL) ? (const char*)error->Message : "no details";
}


// Static functions: the runtime.
static WorldAnimatorRuntime* GetRuntime(WorldAnimatorComponent* animator)
{
    if (animator->_runtime == NULL)
    {
        WorldAnimatorRuntime* Runtime = Memory_Allocate(sizeof(WorldAnimatorRuntime));
        Memory_Zero(Runtime, sizeof(*Runtime));
        SkeletalMixer_Construct(&Runtime->Mixer);
        animator->_runtime = Runtime;
    }
    return animator->_runtime;
}

/* The world model name the animator's object's model component names, or NULL. */
static const unsigned char* GetWorldModelName(const WorldAnimatorComponent* animator)
{
    const WorldObject* Owner = WorldComponent_GetOwner(&animator->Base);
    const WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(WorldObject_GetComponent(Owner,
        WorldComponentType_Model));
    return (ModelComponent != NULL) ? WorldModelComponent_GetWorldModel(ModelComponent) : NULL;
}

/* Whether the animator must be bound (again): never bound, its world model renamed, or its libraries changed. */
static bool NeedsBind(const WorldAnimatorComponent* animator, const WorldAnimatorRuntime* runtime,
    const unsigned char* worldModelName)
{
    if (!runtime->IsBound || !WorldInternal_AreStringsEqual(worldModelName, runtime->WorldModelName))
    {
        return true;
    }
    if (animator->_overridesAnimationLibraries != runtime->BoundOverridesLibraries)
    {
        return true;
    }
    if (animator->_overridesAnimationLibraries
        && !PropertyAssetNameList_Equals(animator->_animationLibraries, runtime->BoundLibraryNames))
    {
        return true;
    }
    return WorldAnimationInternal_DoesGraphNeedBind(animator, runtime);
}

/* Loads the world model for a new name under the system's user (a failure is warned about once and leaves none). */
static void ResolveWorldModel(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime,
    const unsigned char* name)
{
    Memory_Free(runtime->WorldModelName);
    runtime->WorldModelName = WorldInternal_CloneString(name);
    runtime->WorldModel = NULL;
    runtime->HasWarnedWorldModel = false;
    runtime->HasWarnedSkeleton = false;
    if ((name == NULL) || (self->_assets == NULL))
    {
        return;
    }
    WorldModel* Asset = NULL;
    Error Result = AssetManager_LoadWorldModel(self->_assets, name, self->_assetUser, &Asset);
    if (Result.Code == ErrorCode_Success)
    {
        runtime->WorldModel = Asset;
        return;
    }
    WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedWorldModel, "plays nothing: world model \"%s\" could not be loaded (%s).",
        (const char*)name, GetErrorText(&Result));
    Error_Deconstruct(&Result);
}

/* The libraries the animator plays: the placement's own while it overrides, otherwise its world model's. */
static void CollectLibraries(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime)
{
    runtime->LibraryCount = 0U;
    if (!animator->_overridesAnimationLibraries)
    {
        if (runtime->WorldModel == NULL)
        {
            return;
        }
        size_t Count = WorldModel_GetAnimationLibraryCount(runtime->WorldModel);
        for (size_t Index = 0U; (Index < Count) && (Index < WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES); Index++)
        {
            runtime->Libraries[runtime->LibraryCount] = WorldModel_GetAnimationLibrary(runtime->WorldModel, Index);
            runtime->LibraryNames[runtime->LibraryCount] = WorldModel_GetAnimationLibraryName(runtime->WorldModel, Index);
            runtime->LibraryCount++;
        }
        return;
    }
    const PropertyAssetNameList* Names = runtime->BoundLibraryNames;
    for (size_t Index = 0U; (Index < PropertyAssetNameList_GetCount(Names)) && (self->_assets != NULL); Index++)
    {
        AnimationLibrary* Library = NULL;
        const unsigned char* Name = PropertyAssetNameList_GetName(Names, Index);
        Error Result = AssetManager_LoadAnimationLibrary(self->_assets, Name, self->_assetUser, &Library);
        if (Result.Code != ErrorCode_Success)
        {
            WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedLibraries, "plays without animation library \"%s\": it could not be "
                "loaded (%s).", (const char*)Name, GetErrorText(&Result));
            Error_Deconstruct(&Result);
            continue;
        }
        if (runtime->LibraryCount < WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES)
        {
            runtime->Libraries[runtime->LibraryCount] = Library;
            runtime->LibraryNames[runtime->LibraryCount] = Name;
            runtime->LibraryCount++;
        }
    }
}

/* Makes the per-bone block for the skeleton and the libraries, and binds each library to the skeleton. */
static void BuildBindings(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime)
{
    Memory_Free(runtime->Block);
    runtime->Block = NULL;
    runtime->BindingBones = NULL;
    runtime->BoneMatrices = NULL;
    runtime->SocketMatrices = NULL;
    runtime->SocketCount = 0U;
    runtime->Palette = NULL;
    runtime->BoneCount = (runtime->Skeleton != NULL) ? GameSkeleton_GetBoneCount(runtime->Skeleton) : 0U;
    if (runtime->Skeleton == NULL)
    {
        return;
    }
    // The bones are at most GAME_SKELETON_MAX_BONES, the sockets WORLD_MODEL_MAX_SOCKETS and the libraries
    // WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES, so the block's size cannot overflow. Matrices first (the strictest
    // alignment: the bones', then the sockets'), then the palette, then the bindings.
    size_t BoneCount = runtime->BoneCount;
    runtime->SocketCount = WorldModel_GetSocketCount(runtime->WorldModel);
    size_t MatrixBytes = (BoneCount + (size_t)runtime->SocketCount) * sizeof(Matrix);
    size_t PaletteBytes = BoneCount * SKELETAL_PALETTE_FLOATS_PER_BONE * sizeof(float);
    size_t BindingBytes = (size_t)runtime->LibraryCount * BoneCount * sizeof(SkeletalBindingBone);
    unsigned char* Block = Memory_Allocate(MatrixBytes + PaletteBytes + BindingBytes);
    runtime->Block = Block;
    runtime->BoneMatrices = (Matrix*)(void*)Block;
    runtime->SocketMatrices = runtime->BoneMatrices + BoneCount;
    runtime->Palette = (float*)(void*)(Block + MatrixBytes);
    runtime->BindingBones = (SkeletalBindingBone*)(void*)(Block + MatrixBytes + PaletteBytes);
    for (uint32_t Index = 0U; Index < runtime->LibraryCount; Index++)
    {
        SkeletalBinding_Build(runtime->Skeleton, runtime->Libraries[Index], runtime->BindingBones + (Index * BoneCount),
            &runtime->Bindings[Index]);
        if (runtime->Bindings[Index].MappedCount == 0U)
        {
            WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedLibraries, "plays nothing from animation library \"%s\": none of its "
                "bones is named like a bone of the skeleton (a library for another skeleton?).",
                (const char*)runtime->LibraryNames[Index]);
        }
    }
}

/* Grows the scratch room to @p boneCount bones. */
static void EnsureScratch(WorldAnimation* self, uint32_t boneCount)
{
    if (boneCount <= self->_scratchBoneCount)
    {
        return;
    }
    Memory_Free(self->_localPose);
    Memory_Free(self->_samplePose);
    Memory_Free(self->_blend);
    self->_localPose = Memory_Allocate((size_t)boneCount * sizeof(Transform));
    self->_samplePose = Memory_Allocate((size_t)boneCount * sizeof(Transform));
    self->_blend = Memory_Allocate((size_t)boneCount * sizeof(SkeletalBlendBone));
    self->_scratchBoneCount = boneCount;
}

/* Turns the local pose in the scratch room into the runtime's bone matrices, socket frames and palette, publishes the
 * pose, and marks what hangs on the object's sockets changed. */
static void PublishPose(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime,
    const Transform* localPose)
{
    const GameSkeleton* Skeleton = runtime->Skeleton;
    SkeletalPose_ComputeModelMatrices(Skeleton, localPose, runtime->BoneMatrices);
    SkeletalPose_ComputePalette(Skeleton, runtime->BoneMatrices, runtime->Palette);
    for (uint32_t Socket = 0U; Socket < runtime->SocketCount; Socket++)
    {
        runtime->SocketMatrices[Socket] = WorldModel_GetSocketMatrix(runtime->WorldModel, Socket, runtime->BoneMatrices);
    }
    WorldAnimatorPose* Pose = &animator->_pose;
    Pose->_worldModel = runtime->WorldModel;
    Pose->_skeleton = Skeleton;
    Pose->_boneCount = runtime->BoneCount;
    Pose->_palette = runtime->Palette;
    Pose->_boneMatrices = runtime->BoneMatrices;
    Pose->_hasBounds = SkeletalPose_ComputeBounds(Skeleton, runtime->Palette, &Pose->_bounds);
    Pose->_sockets = (runtime->SocketCount > 0U) ? WorldModel_GetSocket(runtime->WorldModel, 0U) : NULL;
    Pose->_socketMatrices = runtime->SocketMatrices;
    Pose->_socketCount = runtime->SocketCount;
    uint32_t EyesSocket = UINT32_MAX;
    Pose->_eyesSocket = WorldModel_GetFirstPerson(runtime->WorldModel, &EyesSocket, NULL, NULL) ? EyesSocket
        : UINT32_MAX;
    Pose->_revision++;
    WorldInternal_MarkSocketChildrenChanged(self->_world, WorldComponent_GetOwner(&animator->Base));
}

static void ClearPose(WorldAnimation* self, WorldAnimatorComponent* animator)
{
    uint32_t Revision = animator->_pose._revision;
    Memory_Zero(&animator->_pose, sizeof(animator->_pose));
    animator->_pose._revision = Revision + 1U;
    WorldInternal_MarkSocketChildrenChanged(self->_world, WorldComponent_GetOwner(&animator->Base));
}

// Static functions: clips and rows.
/* Finds a clip by name in the animator's libraries (the first that has it) and fills its reference. */
static bool FindClip(const WorldAnimatorComponent* animator, const WorldAnimatorRuntime* runtime, const unsigned char* name,
    SkeletalClipRef* outClip)
{
    for (uint32_t Library = 0U; Library < runtime->LibraryCount; Library++)
    {
        uint32_t Index = 0U;
        if (!AnimationLibrary_TryFindClip(runtime->Libraries[Library], name, &Index))
        {
            continue;
        }
        const AnimationClip* Clip = AnimationLibrary_GetClip(runtime->Libraries[Library], Index);
        *outClip = (SkeletalClipRef)
        {
            .Library = Library, .Clip = Index, .Duration = AnimationClip_GetDuration(Clip),
            .IsLooped = AnimationClip_IsLooped(Clip), .Speed = AnimationClip_GetSpeed(Clip) * animator->_speed
        };
        return true;
    }
    return false;
}

/* Fades the row's clip out (it stays wherever code put it otherwise). */
static void StopRowClip(WorldAnimatorRuntime* runtime, float fadeSeconds)
{
    if (runtime->IsRowClipPlaying)
    {
        SkeletalMixer_SetWeight(&runtime->Mixer, &runtime->RowClip, 0.0f, fadeSeconds, 0.0);
        runtime->IsRowClipPlaying = false;
    }
    Memory_Free(runtime->RowClipName);
    runtime->RowClipName = NULL;
}

/* Applies the animator's rows when they changed since the last time (or after a bind): the clip row. */
static void ApplyRows(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime)
{
    uint32_t Revision = WorldComponent_GetRevision(&animator->Base);
    if (runtime->HasAppliedRows && (Revision == runtime->AppliedRevision))
    {
        return;
    }
    bool IsFirst = !runtime->HasAppliedRows;
    runtime->HasAppliedRows = true;
    runtime->AppliedRevision = Revision;
    float Fade = IsFirst ? 0.0f : animator->_fadeSeconds;
    if (!animator->_playsClip || (animator->_clip == NULL))
    {
        StopRowClip(runtime, Fade);
        return;
    }
    bool IsSameClip = runtime->IsRowClipPlaying && WorldInternal_AreStringsEqual(runtime->RowClipName, animator->_clip);
    if (!IsSameClip && !WorldInternal_AreStringsEqual(runtime->RowClipName, animator->_clip))
    {
        runtime->HasWarnedClip = false;
    }
    SkeletalClipRef Clip;
    if (!FindClip(animator, runtime, animator->_clip, &Clip))
    {
        WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedClip, "has no clip \"%s\" in its animation libraries; it plays nothing "
            "from its clip row.", (const char*)animator->_clip);
        StopRowClip(runtime, Fade);
        runtime->RowClipName = WorldInternal_CloneString(animator->_clip);
        return;
    }
    if (animator->_overridesIsLooped)
    {
        Clip.IsLooped = animator->_isLooped;
    }
    if (IsSameClip)
    {
        // The same clip: only its loop may have changed (the phase applies when a clip starts).
        runtime->RowClip.IsLooped = Clip.IsLooped;
        for (uint32_t Index = 0U; Index < runtime->Mixer.StateCount; Index++)
        {
            SkeletalClipState* State = &runtime->Mixer.States[Index];
            if ((State->Clip.Library == Clip.Library) && (State->Clip.Clip == Clip.Clip))
            {
                State->Clip.IsLooped = Clip.IsLooped;
            }
        }
        return;
    }
    SkeletalMixer_Play(&runtime->Mixer, &Clip, Fade, false, (double)animator->_phase * (double)Clip.Duration);
    Memory_Free(runtime->RowClipName);
    runtime->RowClipName = WorldInternal_CloneString(animator->_clip);
    runtime->RowClip = Clip;
    runtime->IsRowClipPlaying = true;
    runtime->IsAtRest = false;
    runtime->IsPoseStale = true;
}

/* Every playing clip's speed: its own times the animator's (both may change at any time). */
static void UpdateSpeeds(const WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime)
{
    for (uint32_t Index = 0U; Index < runtime->Mixer.StateCount; Index++)
    {
        SkeletalClipState* State = &runtime->Mixer.States[Index];
        const AnimationClip* Clip = AnimationLibrary_GetClip(runtime->Libraries[State->Clip.Library], State->Clip.Clip);
        State->Clip.Speed = AnimationClip_GetSpeed(Clip) * animator->_speed;
    }
}

// Static functions: binding and updating.
/* Binds the animator to its world model, skeleton and libraries, and starts it over from the rest pose with its rows
 * applied (the first time: at once, at the start phase). */
static void Bind(WorldAnimation* self, WorldAnimatorComponent* animator, WorldAnimatorRuntime* runtime,
    const unsigned char* worldModelName)
{
    // An action does not survive a bind: its graph may be another.
    WorldAnimationInternal_StopAction(self, animator, runtime, WorldAnimationEventKind_ActionEnded);
    if (!runtime->IsBound || !WorldInternal_AreStringsEqual(worldModelName, runtime->WorldModelName))
    {
        ResolveWorldModel(self, animator, runtime, worldModelName);
    }
    runtime->IsBound = true;
    runtime->BoundOverridesLibraries = animator->_overridesAnimationLibraries;
    PropertyAssetNameList_Destroy(runtime->BoundLibraryNames);
    runtime->BoundLibraryNames = PropertyAssetNameList_Clone(animator->_animationLibraries);
    runtime->HasWarnedLibraries = false;
    runtime->Skeleton = (runtime->WorldModel != NULL) ? WorldModel_GetSkeleton(runtime->WorldModel) : NULL;
    if ((runtime->WorldModel != NULL) && (runtime->Skeleton == NULL))
    {
        WorldAnimationInternal_Warn(self, animator, &runtime->HasWarnedSkeleton, "plays nothing: world model \"%s\" has no skinned LOD (no "
            "skeleton).", (const char*)worldModelName);
    }
    CollectLibraries(self, animator, runtime);
    BuildBindings(self, animator, runtime);
    runtime->ImportRotation = QuaternionIdentity();
    if (runtime->Skeleton != NULL)
    {
        GameModel* SkeletonModel = WorldModel_GetLodModel(runtime->WorldModel, WorldModel_GetSkeletonLod(runtime->WorldModel));
        runtime->ImportRotation = SkeletalAnimation_DecomposeMatrix(GameModel_GetRaylibModel(SkeletonModel).transform)
            .rotation;
    }
    WorldAnimationInternal_BindGraph(self, animator, runtime);

    SkeletalMixer_Construct(&runtime->Mixer);
    runtime->HasAppliedRows = false;
    runtime->IsRowClipPlaying = false;
    Memory_Free(runtime->RowClipName);
    runtime->RowClipName = NULL;
    runtime->HasWarnedClip = false;
    if (runtime->Skeleton == NULL)
    {
        ClearPose(self, animator);
        runtime->IsAtRest = true;
        return;
    }
    EnsureScratch(self, runtime->BoneCount);
    SkeletalPose_SetRest(runtime->Skeleton, self->_localPose);
    PublishPose(self, animator, runtime, self->_localPose);
    runtime->IsAtRest = true;
    runtime->IsPoseStale = false;
    // The rows apply at once, so a clip code plays right after the bind cross-fades from the row's clip.
    ApplyRows(self, animator, runtime);
}

/* Binds the animator when it needs it; returns its runtime (bound). */
static WorldAnimatorRuntime* EnsureBound(WorldAnimation* self, WorldAnimatorComponent* animator)
{
    WorldAnimatorRuntime* Runtime = GetRuntime(animator);
    Runtime->System = self;
    const unsigned char* Name = GetWorldModelName(animator);
    if (NeedsBind(animator, Runtime, Name))
    {
        Bind(self, animator, Runtime, Name);
    }
    return Runtime;
}


/* The tick's part: binds, applies the rows and advances the clips; the pose waits for the pose step. */
static void UpdateAnimator(WorldAnimation* self, WorldAnimatorComponent* animator, float deltaSeconds)
{
    if (!WorldObject_IsEffectivelyEnabled(WorldComponent_GetOwner(&animator->Base)))
    {
        return;
    }
    WorldAnimatorRuntime* Runtime = EnsureBound(self, animator);
    if (Runtime->Skeleton == NULL)
    {
        return;
    }
    self->_status.PosedCount++;
    ApplyRows(self, animator, Runtime);
    UpdateSpeeds(animator, Runtime);
    bool IsAdvancing = !animator->_isPaused && isfinite(deltaSeconds) && (deltaSeconds > 0.0f);
    if (Runtime->GraphBlock != NULL)
    {
        // The graph first (its clips under the rows' and code's); paused, it waits as the clips do.
        self->_status.GraphCount++;
        if (!animator->_isPaused)
        {
            WorldAnimationInternal_UpdateGraph(self, animator, Runtime,
                IsAdvancing ? ((double)deltaSeconds * (double)animator->_speed) : 0.0);
        }
        self->_status.ActionCount += (Runtime->Action.Phase != WorldAnimationActionPhase_None) ? 1U : 0U;
        Runtime->IsPoseStale = true;
    }
    if (IsAdvancing)
    {
        SkeletalMixer_Advance(&Runtime->Mixer, (double)deltaSeconds);
    }
    self->_status.ClipCount += Runtime->Mixer.StateCount;
    // Something plays, or the last clip just faded out (the rest pose is published once more).
    if ((Runtime->Mixer.StateCount > 0U) || !Runtime->IsAtRest)
    {
        Runtime->IsPoseStale = true;
    }
}

/* The pose step's part: an animator whose playback changed since its last pose is posed again. */
static void PoseAnimator(WorldAnimation* self, WorldAnimatorComponent* animator)
{
    WorldAnimatorRuntime* Runtime = animator->_runtime;
    if ((Runtime == NULL) || (Runtime->Skeleton == NULL) || !Runtime->IsPoseStale
        || !WorldObject_IsEffectivelyEnabled(WorldComponent_GetOwner(&animator->Base)))
    {
        return;
    }
    EnsureScratch(self, Runtime->BoneCount);
    if (Runtime->GraphBlock != NULL)
    {
        // The graph's pose, with the clips from the rows and code over it by their weight.
        AnimationGraphInstance_SamplePose(&Runtime->GraphInstance, &Runtime->GraphBinding, Runtime->ImportRotation,
            self->_samplePose, self->_blend, self->_localPose);
        if (Runtime->Mixer.StateCount > 0U)
        {
            SkeletalPose_ClearBlend(self->_blend, Runtime->BoneCount);
            float Total = SkeletalMixer_AddToBlend(&Runtime->Mixer, Runtime->Bindings, Runtime->LibraryCount,
                Runtime->Skeleton, self->_samplePose, self->_blend);
            SkeletalPose_BlendOver(self->_blend, Total, NULL, Runtime->BoneCount, self->_localPose);
        }
    }
    else
    {
        SkeletalMixer_SamplePose(&Runtime->Mixer, Runtime->Bindings, Runtime->LibraryCount, Runtime->Skeleton,
            self->_samplePose, self->_blend, self->_localPose);
    }
    PublishPose(self, animator, Runtime, self->_localPose);
    Runtime->IsAtRest = (Runtime->Mixer.StateCount == 0U) && (Runtime->GraphBlock == NULL);
    Runtime->IsPoseStale = false;
    self->_status.RecomputedCount++;
    self->_status.BoneCount += Runtime->BoneCount;
}

/* The animator of an object, bound and with a skeleton, for the play functions. */
static Error GetPlayableAnimator(WorldAnimation* self, uint64_t objectId, WorldAnimatorComponent** outAnimator,
    WorldAnimatorRuntime** outRuntime)
{
    WorldComponent* Component = NULL;
    if (!World_TryGetComponent(self->_world, objectId, WorldComponentType_Animator, &Component))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "Object %" PRIu64 " has no animator.", objectId);
    }
    WorldAnimatorComponent* Animator = WorldAnimatorComponent_FromComponent(Component);
    WorldAnimatorRuntime* Runtime = EnsureBound(self, Animator);
    if (Runtime->Skeleton == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "The animator of object %" PRIu64 " has no skeleton to play "
            "on.", objectId);
    }
    *outAnimator = Animator;
    *outRuntime = Runtime;
    return Error_CreateSuccess();
}

static Error FindPlayableClip(const WorldAnimatorComponent* animator, const WorldAnimatorRuntime* runtime,
    uint64_t objectId, const unsigned char* clip, SkeletalClipRef* outClip)
{
    if (FindClip(animator, runtime, clip, outClip))
    {
        return Error_CreateSuccess();
    }
    return Error_Construct4(ErrorCode_ArgumentOutOfRange, "The animator of object %" PRIu64 " has no clip \"%s\".",
        objectId, (const char*)clip);
}


// Public functions.
void WorldAnimationInternal_DestroyRuntime(WorldAnimatorRuntime* runtime)
{
    if (runtime == NULL)
    {
        return;
    }
    Memory_Free(runtime->WorldModelName);
    PropertyAssetNameList_Destroy(runtime->BoundLibraryNames);
    Memory_Free(runtime->RowClipName);
    Memory_Free(runtime->Block);
    WorldAnimationInternal_ReleaseGraph(runtime);
    Memory_Free(runtime);
}

void WorldAnimationInternal_DetachAnimator(WorldAnimatorComponent* animator)
{
    WorldAnimatorRuntime* Runtime = animator->_runtime;
    if ((Runtime != NULL) && (Runtime->System != NULL))
    {
        WorldAnimationInternal_StopAction(Runtime->System, animator, Runtime, WorldAnimationEventKind_ActionEnded);
    }
}

WorldAnimatorComponent* WorldAnimationInternal_FindAnimator(WorldAnimation* self, uint64_t objectId,
    WorldAnimatorRuntime** outRuntime)
{
    WorldComponent* Component = NULL;
    if ((self == NULL) || !World_TryGetComponent(self->_world, objectId, WorldComponentType_Animator, &Component))
    {
        return NULL;
    }
    WorldAnimatorComponent* Animator = WorldAnimatorComponent_FromComponent(Component);
    WorldAnimatorRuntime* Runtime = EnsureBound(self, Animator);
    if (outRuntime != NULL)
    {
        *outRuntime = Runtime;
    }
    return Animator;
}

Error WorldAnimation_Create(World* world, AssetManager* assets, Logger* logger, WorldAnimation** outAnimation)
{
    if ((world == NULL) || (outAnimation == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldAnimation_Create: world and outAnimation must not be NULL.");
    }
    *outAnimation = NULL;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (assets != NULL)
    {
        Error Result = AssetManager_GetNewUserID(assets, &User);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    WorldAnimation* Self = Memory_Allocate(sizeof(WorldAnimation));
    Memory_Zero(Self, sizeof(*Self));
    Self->_world = world;
    Self->_assets = assets;
    Self->_assetUser = User;
    Self->_logger = logger;
    GenericBuffer_AllocateVariable(&Self->_events, INITIAL_EVENT_CAPACITY, sizeof(WorldAnimationEvent));
    *outAnimation = Self;
    return Error_CreateSuccess();
}

void WorldAnimation_Destroy(WorldAnimation* self)
{
    if (self == NULL)
    {
        return;
    }
    // Every animator lets go of what it was bound to before the assets are released.
    size_t Count = 0U;
    WorldComponent* const* Animators = World_GetComponents(self->_world, WorldComponentType_Animator, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        WorldAnimatorComponent* Animator = WorldAnimatorComponent_FromComponent(Animators[Index]);
        if (Animator->_runtime != NULL)
        {
            // The characters its actions held are freed.
            WorldAnimationInternal_StopAction(self, Animator, Animator->_runtime, WorldAnimationEventKind_ActionEnded);
        }
        WorldAnimationInternal_DestroyRuntime(Animator->_runtime);
        Animator->_runtime = NULL;
        ClearPose(self, Animator);
    }
    if ((self->_assets != NULL) && (self->_assetUser != ASSET_USER_ID_INVALID))
    {
        Error Result = AssetManager_RetireUser(self->_assets, self->_assetUser);
        Error_Deconstruct(&Result);
    }
    Memory_Free(self->_localPose);
    Memory_Free(self->_samplePose);
    Memory_Free(self->_blend);
    Memory_Free(self->_events._data);
    Memory_Free(self);
}

void WorldAnimation_Update(WorldAnimation* self, float deltaSeconds)
{
    Stopwatch Clock = Stopwatch_StartNew();
    size_t Count = 0U;
    WorldComponent* const* Animators = World_GetComponents(self->_world, WorldComponentType_Animator, &Count);
    self->_tick++;
    self->_status.AnimatorCount = Count;
    self->_status.PosedCount = 0U;
    self->_status.ClipCount = 0U;
    self->_status.GraphCount = 0U;
    self->_status.ActionCount = 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        UpdateAnimator(self, WorldAnimatorComponent_FromComponent(Animators[Index]), deltaSeconds);
    }
    self->_status.UpdateMicroseconds = (double)Stopwatch_ElapsedNanoseconds(&Clock) / NANOSECONDS_PER_MICROSECOND;
    Stopwatch_Deconstruct(&Clock);
}

void WorldAnimation_Pose(WorldAnimation* self)
{
    Stopwatch Clock = Stopwatch_StartNew();
    size_t Count = 0U;
    WorldComponent* const* Animators = World_GetComponents(self->_world, WorldComponentType_Animator, &Count);
    self->_status.RecomputedCount = 0U;
    self->_status.BoneCount = 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        PoseAnimator(self, WorldAnimatorComponent_FromComponent(Animators[Index]));
    }
    self->_status.PoseMicroseconds = (double)Stopwatch_ElapsedNanoseconds(&Clock) / NANOSECONDS_PER_MICROSECOND;
    Stopwatch_Deconstruct(&Clock);
}

Error WorldAnimation_PlayClip(WorldAnimation* self, uint64_t objectId, const unsigned char* clip, float fadeSeconds,
    bool isRestarted)
{
    if ((self == NULL) || (clip == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldAnimation_PlayClip: self and clip must not be NULL.");
    }
    WorldAnimatorComponent* Animator = NULL;
    WorldAnimatorRuntime* Runtime = NULL;
    Error Result = GetPlayableAnimator(self, objectId, &Animator, &Runtime);
    SkeletalClipRef Clip;
    if (Result.Code == ErrorCode_Success)
    {
        Result = FindPlayableClip(Animator, Runtime, objectId, clip, &Clip);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    SkeletalMixer_Play(&Runtime->Mixer, &Clip, fadeSeconds, isRestarted, 0.0);
    Runtime->IsAtRest = false;
    Runtime->IsPoseStale = true;
    return Error_CreateSuccess();
}

Error WorldAnimation_SetClipWeight(WorldAnimation* self, uint64_t objectId, const unsigned char* clip, float weight,
    float fadeSeconds)
{
    if ((self == NULL) || (clip == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldAnimation_SetClipWeight: self and clip must not be NULL.");
    }
    WorldAnimatorComponent* Animator = NULL;
    WorldAnimatorRuntime* Runtime = NULL;
    Error Result = GetPlayableAnimator(self, objectId, &Animator, &Runtime);
    SkeletalClipRef Clip;
    if (Result.Code == ErrorCode_Success)
    {
        Result = FindPlayableClip(Animator, Runtime, objectId, clip, &Clip);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    SkeletalMixer_SetWeight(&Runtime->Mixer, &Clip, weight, fadeSeconds, 0.0);
    Runtime->IsAtRest = false;
    Runtime->IsPoseStale = true;
    return Error_CreateSuccess();
}

Error WorldAnimation_StopClips(WorldAnimation* self, uint64_t objectId, float fadeSeconds)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldAnimation_StopClips: self must not be NULL.");
    }
    WorldComponent* Component = NULL;
    if (!World_TryGetComponent(self->_world, objectId, WorldComponentType_Animator, &Component))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "Object %" PRIu64 " has no animator.", objectId);
    }
    WorldAnimatorRuntime* Runtime = EnsureBound(self, WorldAnimatorComponent_FromComponent(Component));
    SkeletalMixer_StopAll(&Runtime->Mixer, fadeSeconds);
    Runtime->IsAtRest = false;
    Runtime->IsPoseStale = true;
    return Error_CreateSuccess();
}

bool WorldAnimation_TryFindBone(WorldAnimation* self, uint64_t objectId, const unsigned char* boneName,
    uint32_t* outBone)
{
    WorldComponent* Component = NULL;
    if ((self == NULL) || !World_TryGetComponent(self->_world, objectId, WorldComponentType_Animator, &Component))
    {
        return false;
    }
    WorldAnimatorRuntime* Runtime = EnsureBound(self, WorldAnimatorComponent_FromComponent(Component));
    return (Runtime->Skeleton != NULL) && GameSkeleton_TryFindBone(Runtime->Skeleton, boneName, outBone);
}

bool WorldAnimation_TryGetBoneWorldMatrix(WorldAnimation* self, uint64_t objectId, uint32_t bone,
    Matrix4Double* outMatrix)
{
    WorldComponent* Component = NULL;
    if ((self == NULL) || (outMatrix == NULL)
        || !World_TryGetComponent(self->_world, objectId, WorldComponentType_Animator, &Component))
    {
        return false;
    }
    WorldAnimatorRuntime* Runtime = EnsureBound(self, WorldAnimatorComponent_FromComponent(Component));
    if ((Runtime->Skeleton == NULL) || (bone >= Runtime->BoneCount))
    {
        return false;
    }
    // The bone's matrix (its space to mesh space), then the LOD's import transform (mesh space to the object's), then
    // the object's world matrix, in double.
    GameModel* SkeletonModel = WorldModel_GetLodModel(Runtime->WorldModel, WorldModel_GetSkeletonLod(Runtime->WorldModel));
    Matrix Local = MatrixMultiply(Runtime->BoneMatrices[bone], GameModel_GetRaylibModel(SkeletonModel).transform);
    Matrix4Double LocalDouble = GameMath_FromMatrix(Local);
    Matrix4Double ObjectWorld = WorldObject_GetWorldMatrix(WorldComponent_GetOwner(Component));
    *outMatrix = Matrix4Double_Multiply(&LocalDouble, &ObjectWorld);
    return true;
}

WorldAnimationStatus WorldAnimation_GetStatus(const WorldAnimation* self)
{
    return self->_status;
}

const WorldAnimationEvent* WorldAnimation_GetEvents(const WorldAnimation* self, size_t* outCount)
{
    *outCount = self->_events._count;
    return (self->_events._count > 0U) ? (const WorldAnimationEvent*)(const void*)self->_events._data : NULL;
}

void WorldAnimation_ClearEvents(WorldAnimation* self)
{
    GenericBuffer_Clear(&self->_events);
}

uint64_t WorldAnimation_GetTick(const WorldAnimation* self)
{
    return self->_tick;
}
