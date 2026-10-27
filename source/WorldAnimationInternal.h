#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "AnimationGraph.h"
#include "AnimationGraphPlayer.h"
#include "AnimationLibrary.h"
#include "AssetManager.h"
#include "GameSkeleton.h"
#include "Logger.h"
#include "SkeletalAnimation.h"
#include "World.h"
#include "WorldAnimation.h"
#include "WorldAnimatorComponent.h"
#include "WorldModel.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): what the animation system (WorldAnimation.c, its graphs in WorldAnimationGraph.c and its
 * actions in WorldAnimationActions.c) keeps per animator component and for itself, and the one function the component
 * (WorldAnimatorComponent.c) needs from it: freeing that state when the component goes.
 *
 * The runtime is made by the system when it first binds an animator and lives in the component (_runtime), so removing
 * the component frees it and the system keeps no list of animators: it walks the world's animator array every tick.
 * Its per-bone arrays (the bindings' bones, the bone matrices, the socket frames, the palette) are one block, rebuilt
 * when the skeleton or the libraries change; the graph's binding and playback are a second block. The asset pointers
 * in it are held under the system's asset user (or with the world model).
 */


// Types.
/**
 * Where an action is: sliding its character onto its anchor, or playing its states.
 */
typedef enum WorldAnimationActionPhaseEnum
{
    WorldAnimationActionPhase_None = 0,
    WorldAnimationActionPhase_Approaching = 1,
    WorldAnimationActionPhase_Playing = 2
} WorldAnimationActionPhase;

/**
 * The action an animator plays: which, with what, where it is.
 */
typedef struct WorldAnimationActionStruct
{
    WorldAnimationActionPhase Phase;
    /** The action's index in the graph, and its target object (0: none). */
    uint32_t Index;
    uint64_t TargetId;
    /** Seconds since it started (approaching or playing). */
    float Seconds;
    /** Whether an end was asked for while it approached (it ends before playing). */
    bool IsEndRequested;
} WorldAnimationAction;

struct WorldAnimatorRuntimeStruct
{
    /** The system that made it (borrowed): the system destroys every runtime before it goes, so it never dangles. */
    WorldAnimation* System;
    /** The name of the world model it was bound for (owned copy, NULL for none) and whether a bind was tried. */
    unsigned char* WorldModelName;
    bool IsBound;
    /** The world model (NULL when the name is NULL or failed to load) and its skeleton (NULL: nothing to pose). */
    const WorldModel* WorldModel;
    const GameSkeleton* Skeleton;
    uint32_t BoneCount;
    /** The rotation of the skeleton LOD's import transform (mesh space to the object's): look-at turns through it. */
    Quaternion ImportRotation;
    /** Whether the placement's own libraries were bound, and a copy of the list that was (owned; NULL while empty). */
    bool BoundOverridesLibraries;
    PropertyAssetNameList* BoundLibraryNames;
    /** The libraries it plays (held under the system's asset user), their names (borrowed from the world model or from
     *  BoundLibraryNames) and their bindings to the skeleton. */
    const AnimationLibrary* Libraries[WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES];
    const unsigned char* LibraryNames[WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES];
    SkeletalBinding Bindings[WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES];
    uint32_t LibraryCount;
    /** The block of per-bone arrays (owned; NULL without a skeleton) and its parts. */
    void* Block;
    SkeletalBindingBone* BindingBones;
    Matrix* BoneMatrices;
    float* Palette;
    /** The sockets' frames in the pose, in the object's own space (in Block): one per socket of the world model. */
    Matrix* SocketMatrices;
    uint32_t SocketCount;
    /** The clips playing from the rows and from code (over the graph, by their weight). */
    SkeletalMixer Mixer;
    /** The animator's revision when its rows were last applied, and whether they were applied since the bind. */
    uint32_t AppliedRevision;
    bool HasAppliedRows;
    /** The clip row's clip as last applied: whether it plays, its name (owned copy) and where it was found. */
    bool IsRowClipPlaying;
    unsigned char* RowClipName;
    SkeletalClipRef RowClip;
    /** Whether the published pose is the rest pose and nothing plays (it is not recomputed then). */
    bool IsAtRest;
    /** Whether the playback changed since the pose was last published: the next pose step (WorldAnimation_Pose)
     *  recomputes it. */
    bool IsPoseStale;
    /** The graph it plays: its world model's (held with it) or the placement's own (held under the system's
     *  asset user); NULL without one. Whether the placement's own was bound, and a copy of its name (owned, or NULL). */
    const AnimationGraph* Graph;
    bool BoundOverridesGraph;
    unsigned char* BoundGraphName;
    /** The graph's binding and playback in one block (owned; NULL without a graph or a skeleton). */
    void* GraphBlock;
    AnimationGraphBinding GraphBinding;
    AnimationGraphInstance GraphInstance;
    /** The action it plays. */
    WorldAnimationAction Action;
    /** Warnings given once per animator (reset when what they are about changes). */
    bool HasWarnedWorldModel;
    bool HasWarnedSkeleton;
    bool HasWarnedLibraries;
    bool HasWarnedClip;
    bool HasWarnedGraph;
    bool HasWarnedEffect;
};

/* The animation system (WorldAnimation.h). */
struct WorldAnimationStruct
{
    World* _world;
    AssetManager* _assets;
    AssetUserID _assetUser;
    Logger* _logger;
    /* Scratch room for one pose (the mixed local pose, one clip's sample, the blend's sums), _scratchBoneCount bones. */
    Transform* _localPose;
    Transform* _samplePose;
    SkeletalBlendBone* _blend;
    uint32_t _scratchBoneCount;
    WorldAnimationStatus _status;
    /* The queued events (WorldAnimationEvent), until WorldAnimation_ClearEvents; and how many updates ran. */
    GenericBuffer _events;
    uint64_t _tick;
};


// Functions: WorldAnimation.c.
/**
 * Frees an animator's runtime and everything it owns (the copies of names and lists, the per-bone block, the graph's
 * block). The asset pointers in it are borrowed and stay held under the system's user.
 * @param runtime The runtime, or NULL.
 */
void WorldAnimationInternal_DestroyRuntime(WorldAnimatorRuntime* runtime);

/** Logs a warning about one animator once (*isWarned is set): "WorldAnimation: the animator of object 12 ("clerk") ...". */
void WorldAnimationInternal_Warn(const WorldAnimation* self, const WorldAnimatorComponent* animator, bool* isWarned,
    const char* format, ...);

/** An animator leaving its object (its component's OnDetach): its action ends as WorldAnimation_EndAction's
 *  would at once (the character it locked is free, the end is queued). Nothing without a runtime. */
void WorldAnimationInternal_DetachAnimator(WorldAnimatorComponent* animator);

/** The animator of an object, bound (binding it now if needed); NULL when the object has none. */
WorldAnimatorComponent* WorldAnimationInternal_FindAnimator(WorldAnimation* self, uint64_t objectId,
    WorldAnimatorRuntime** outRuntime);


// Functions: WorldAnimationGraph.c.
/** Binds the graph the animator plays (its world model's or its own) to its skeleton and libraries, and starts its
 *  playback; the runtime's skeleton and library bindings are bound already. Without a graph or a skeleton it has none. */
void WorldAnimationInternal_BindGraph(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime);

/** Whether the graph the animator plays is no longer the one bound (its override or its name changed). */
bool WorldAnimationInternal_DoesGraphNeedBind(const WorldAnimatorComponent* animator, const WorldAnimatorRuntime* runtime);

/** Frees the graph's block and its name (the runtime then has no graph). */
void WorldAnimationInternal_ReleaseGraph(WorldAnimatorRuntime* runtime);

/** The tick's graph step for one animator: the character's state into the built-in parameters and its events, the
 *  action's progress, then the graph's advance by @p seconds (its events' effects and queue). */
void WorldAnimationInternal_UpdateGraph(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime, double seconds);

/** Queues one event of an animator's object (its name copied, cut to WORLD_ANIMATION_EVENT_NAME_CAPACITY - 1 bytes). */
void WorldAnimationInternal_QueueEvent(WorldAnimation* self, WorldAnimationEventKind kind, uint64_t objectId,
    const unsigned char* name, uint64_t targetId);


// Functions: WorldAnimationActions.c.
/** The tick's action step for one animator with a graph, before the graph advances: an approach that arrived enters the
 *  action's first state, one that failed (or was ended) ends; a playing action whose layer left its states ends. */
void WorldAnimationInternal_UpdateAction(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime, double seconds);

/** Ends an animator's action at once, if it has one (its character's movement is free again), queueing the end. */
void WorldAnimationInternal_StopAction(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime, WorldAnimationEventKind kind);

/** An event's effects: attaching the action's target to a socket, detaching what hangs on one. */
void WorldAnimationInternal_ApplyEventEffects(WorldAnimation* self, WorldAnimatorComponent* animator,
    WorldAnimatorRuntime* runtime, const AnimationGraphEventRecord* event);
