#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "AnimationLibrary.h"
#include "GameSkeleton.h"
#include "SkeletalAnimation.h"
#include "WorldAnimatorComponent.h"
#include "WorldModel.h"

/*
 * Private header (source/): what the animation system (WorldAnimation.c) keeps per animator component, and the one
 * function the component (WorldAnimatorComponent.c) needs from it: freeing that state when the component goes.
 *
 * The runtime is made by the system when it first binds an animator and lives in the component (_runtime), so removing
 * the component frees it and the system keeps no list of animators: it walks the world's animator array every tick.
 * Its per-bone arrays (the bindings' bones, the bone matrices, the palette) are one block, rebuilt when the skeleton or
 * the libraries change. The asset pointers in it are held under the system's asset user.
 */


// Types.
struct WorldAnimatorRuntimeStruct
{
    /** The name of the world model it was bound for (owned copy, NULL for none) and whether a bind was tried. */
    unsigned char* WorldModelName;
    bool IsBound;
    /** The world model (NULL when the name is NULL or failed to load) and its skeleton (NULL: nothing to pose). */
    const WorldModel* WorldModel;
    const GameSkeleton* Skeleton;
    uint32_t BoneCount;
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
    /** The clips playing. */
    SkeletalMixer Mixer;
    /** The animator's revision when its rows were last applied, and whether they were applied since the bind. */
    uint32_t AppliedRevision;
    bool HasAppliedRows;
    /** The clip row's clip as last applied: whether it plays, its name (owned copy) and where it was found. */
    bool IsRowClipPlaying;
    unsigned char* RowClipName;
    SkeletalClipRef RowClip;
    /** Whether the pose is the rest pose and nothing plays (it is not recomputed then). */
    bool IsAtRest;
    /** Warnings given once per animator (reset when what they are about changes). */
    bool HasWarnedWorldModel;
    bool HasWarnedSkeleton;
    bool HasWarnedLibraries;
    bool HasWarnedClip;
};


// Functions.
/**
 * Frees an animator's runtime and everything it owns (the copies of names and lists, the per-bone block). The asset
 * pointers in it are borrowed and stay held under the system's user.
 * @param runtime The runtime, or NULL.
 */
void WorldAnimationInternal_DestroyRuntime(WorldAnimatorRuntime* runtime);
