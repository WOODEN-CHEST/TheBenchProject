#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Transform, Matrix, Quaternion and BoundingBox are raylib types: poses are raylib transforms, so this include is
// unavoidable here.
#include "raylib/raylib.h"
#include "AnimationLibrary.h"
#include "GameSkeleton.h"


/**
 * @file SkeletalAnimation.h
 * @brief Skeletal animation's runtime maths (T29): playing a library's clips on a model's skeleton, blending and
 *        cross-fading them, and the pose's matrices, skinning palette and bounds.
 *
 * Pure functions over caller-owned memory: nothing here allocates, touches a world or needs a GL context, so the
 * animation system (WorldAnimation.h) and later T30's state graph build on it and the tests reach all of it.
 *
 * BINDING A LIBRARY TO A SKELETON (SkeletalBinding_Build). A library's clips name bones; a model's skeleton has its own.
 * Each skeleton bone takes the library bone of the same name (as both keep names: their first 31 bytes), so one
 * library plays on every model whose skeleton shares its bone names, and on any other with the bones that match (the
 * rest keep their rest pose: a wrong skeleton stands still). A library bone whose parent has no match on the skeleton
 * (an armature object above the root bone) has that parent's rest pose folded in.
 *
 * RETARGETING (SkeletalBinding_SampleClip). A bone takes the clip's ROTATION as it is, and keeps its OWN rest
 * translation and scale, plus the clip's change from the library's rest translation times the binding's height ratio
 * (the skeleton's height over the library's, from the highest bone both share) and the clip's change of scale as a
 * factor. So the same walk plays on a short and a tall human with each one's limb lengths, its hips' bob and its
 * crouch scaled to its size; on the skeleton the library was made on, the clip plays as authored. This needs the
 * skeletons to share their bone axes and rest posture (models rigged from one base armature); bones turned otherwise
 * twist. A part of a bone the clip does not animate keeps the skeleton's rest value.
 *
 * BLENDING (SkeletalPose_AddToBlend, _FinishBlend). Poses add up by weight: translations and scales as weighted sums,
 * rotations as a weighted sum turned to one hemisphere and normalized. A total weight below 1 is filled with the rest
 * pose (fading in from the rest pose), above 1 it is divided out.
 *
 * THE MIXER (SkeletalMixer). Up to SKELETAL_MIXER_MAX_CLIPS clips play at once, each with its time, speed and weight.
 * Play cross-fades: the clip's weight rises to 1 while every other clip's falls to 0, all over the same fade, so the
 * weights keep their sum (a fade from nothing fades in from the rest pose). SetWeight moves one clip's weight on its
 * own (blends, T30's blend spaces). Looping clips wrap; others stop at their end and hold their last pose.
 *
 * THE POSE. A local pose is one Transform per skeleton bone. SkeletalPose_ComputeModelMatrices turns it into each bone's
 * matrix in mesh space (in the skeleton's evaluation order); SkeletalPose_ComputePalette into the skinning palette
 * the renderer uploads (per bone, the first three rows of its matrix times its inverse bind matrix); the palette into a
 * box holding every skinned vertex of the pose (SkeletalPose_ComputeBounds).
 */


// Macros.
/** @brief Floats per bone in a skinning palette: the first three rows of the bone's affine skinning matrix (the last
 *  row is always 0, 0, 0, 1), each as four floats: row r holds the matrix's entries (r, 0), (r, 1), (r, 2), (r, 3). */
#define SKELETAL_PALETTE_FLOATS_PER_BONE 12U
/** @brief How many clips a mixer plays at once. */
#define SKELETAL_MIXER_MAX_CLIPS 8U
/** @brief A binding bone with no library bone of its name. */
#define SKELETAL_BINDING_UNMAPPED (-1)
/** @brief Skeleton heights below this (metres) give a height ratio of 1. */
#define SKELETAL_BINDING_MIN_HEIGHT 0.01f


// Types.
/**
 * @brief How one skeleton bone takes a library's clips (part of a SkeletalBinding). A plain value type.
 */
typedef struct SkeletalBindingBoneStruct
{
    /** @brief The library bone of the same name, or SKELETAL_BINDING_UNMAPPED. */
    int32_t LibraryBone;
    /** @brief Whether library bones without a match lie between the library bone and its nearest matched ancestor (or
     *  above it, for a root): then Offset holds their rest poses, applied before the bone's own transform. */
    bool HasOffset;
    /** @brief Those ancestors' rest poses composed (identity when HasOffset is false). */
    Transform Offset;
    /** @brief The library bone's rest pose with the offset in it: what the retargeting measures a clip's change from. */
    Transform LibraryRest;
} SkeletalBindingBone;

/**
 * @brief A library bound to a skeleton: which library bone drives each skeleton bone, and the proportions between
 *        them. Built by SkeletalBinding_Build into caller storage; borrows the library, the skeleton and the storage,
 *        which must outlive it. Nothing to release.
 */
typedef struct SkeletalBindingStruct
{
    /** @brief The library (borrowed). */
    const AnimationLibrary* Library;
    /** @brief The skeleton (borrowed). */
    const GameSkeleton* Skeleton;
    /** @brief One entry per skeleton bone (borrowed storage). */
    SkeletalBindingBone* Bones;
    /** @brief How many skeleton bones have a library bone. */
    uint32_t MappedCount;
    /** @brief How many matched bones have a parent that matches another library bone than their library bone's parent
     *  (the hierarchies differ there; their rotation may be off). */
    uint32_t MismatchedParentCount;
    /** @brief The skeleton's height over the library's (see the file comment); 1 when either is too small to measure. */
    float HeightRatio;
} SkeletalBinding;

/**
 * @brief One bone's running sums while poses are blended (SkeletalPose_ClearBlend, _AddToBlend, _FinishBlend).
 */
typedef struct SkeletalBlendBoneStruct
{
    /** @brief The weighted sum of translations. */
    Vector3 Translation;
    /** @brief The weighted sum of rotations, each turned to the hemisphere of the sum so far. */
    Quaternion Rotation;
    /** @brief The weighted sum of scales. */
    Vector3 Scale;
} SkeletalBlendBone;

/**
 * @brief Which clip a mixer plays: everything the mixer keeps about it. A plain value type.
 */
typedef struct SkeletalClipRefStruct
{
    /** @brief Which of the caller's libraries (an index into the bindings it samples with). */
    uint32_t Library;
    /** @brief The clip's index in that library. */
    uint32_t Clip;
    /** @brief The clip's length in seconds (AnimationClip_GetDuration). */
    float Duration;
    /** @brief Whether it loops. */
    bool IsLooped;
    /** @brief How fast it plays (the clip's speed, times whatever the caller adds; at least 0). */
    float Speed;
} SkeletalClipRef;

/**
 * @brief One clip a mixer plays: where it is and how much it counts. Read through the mixer.
 */
typedef struct SkeletalClipStateStruct
{
    /** @brief The clip. */
    SkeletalClipRef Clip;
    /** @brief Seconds into the clip: within [0, duration) while it loops, at most its duration otherwise. */
    double Time;
    /** @brief Whether a clip that does not loop reached its end (it holds its last pose). */
    bool HasEnded;
    /** @brief Its weight now, in [0, 1]. */
    float Weight;
    /** @brief The weight its fade started from and the one it heads to. */
    float StartWeight;
    float TargetWeight;
    /** @brief Its fade's length and how far the fade is, in seconds (Elapsed >= Seconds: done). */
    float FadeSeconds;
    float FadeElapsed;
} SkeletalClipState;

/**
 * @brief The clips playing on one skeleton, with their times and weights. A plain value type: construct it with
 *        SkeletalMixer_Construct; it holds no memory.
 */
typedef struct SkeletalMixerStruct
{
    /** @brief The clips playing, in the order they started. */
    SkeletalClipState States[SKELETAL_MIXER_MAX_CLIPS];
    /** @brief How many entries of States are in use. */
    uint32_t StateCount;
} SkeletalMixer;


// Functions: transforms.
/**
 * @brief The matrix of a transform: its scale, then its rotation, then its translation (raymath order, as glTF nodes).
 * @param transform The transform.
 * @returns The matrix.
 */
Matrix SkeletalAnimation_ComposeTransform(Transform transform);

/**
 * @brief The transform of an affine matrix without shear: its translation, its columns' lengths as the scale (the x
 *        scale negative for a mirroring matrix) and the rotation of its normalized columns. Unlike raymath's
 *        MatrixDecompose it reads the columns, so a non-uniform scale under a rotation comes back as it was composed.
 * @param matrix The matrix (its bottom row is ignored).
 * @returns The transform; a column of length 0 keeps a scale of 0 on that axis and leaves the rotation as the others
 *          give it.
 */
Transform SkeletalAnimation_DecomposeMatrix(Matrix matrix);


// Functions: bindings.
/**
 * @brief Binds a library to a skeleton: matches every skeleton bone to the library bone of its name, folds the rest
 *        poses of unmatched library ancestors into their matched descendants, and measures the height ratio.
 * @param skeleton The skeleton (borrowed: it must outlive the binding); must not be NULL.
 * @param library The library (borrowed likewise); must not be NULL.
 * @param storage [out] GameSkeleton_GetBoneCount entries the binding fills and keeps (borrowed likewise); must not be
 *        NULL.
 * @param outBinding [out] Receives the binding; must not be NULL.
 */
void SkeletalBinding_Build(const GameSkeleton* skeleton, const AnimationLibrary* library,
    SkeletalBindingBone* storage, SkeletalBinding* outBinding);

/**
 * @brief Samples one clip of the binding's library onto its skeleton, retargeted (see the file comment).
 * @param self The binding; must not be NULL.
 * @param clipIndex The clip, below AnimationLibrary_GetClipCount of the binding's library.
 * @param time Seconds into the clip (wrapped by the caller for a looping clip).
 * @param outPose [out] Receives one local Transform per skeleton bone; must not be NULL.
 */
void SkeletalBinding_SampleClip(const SkeletalBinding* self, uint32_t clipIndex, float time, Transform* outPose);


// Functions: poses.
/**
 * @brief Writes the skeleton's rest pose.
 * @param skeleton The skeleton; must not be NULL.
 * @param outPose [out] Receives one local Transform per bone; must not be NULL.
 */
void SkeletalPose_SetRest(const GameSkeleton* skeleton, Transform* outPose);

/**
 * @brief Clears a blend's running sums.
 * @param blend [out] One entry per bone; must not be NULL.
 * @param boneCount How many bones.
 */
void SkeletalPose_ClearBlend(SkeletalBlendBone* blend, uint32_t boneCount);

/**
 * @brief Adds a pose to a blend by a weight.
 * @param blend [in,out] The blend's running sums, one per bone; must not be NULL.
 * @param pose The pose, one local Transform per bone; must not be NULL.
 * @param boneCount How many bones.
 * @param weight How much the pose counts (above 0; the caller adds the weights up for SkeletalPose_FinishBlend).
 */
void SkeletalPose_AddToBlend(SkeletalBlendBone* blend, const Transform* pose, uint32_t boneCount, float weight);

/**
 * @brief Turns a blend into a pose: a total weight below 1 is filled with the skeleton's rest pose, above 1 divided out;
 *        rotations are normalized (a rotation sum of length 0, opposite rotations of equal weight, gives the rest
 *        rotation).
 * @param blend The blend's running sums; must not be NULL.
 * @param skeleton The skeleton (its rest pose); must not be NULL.
 * @param totalWeight The sum of the weights added (at least 0).
 * @param outPose [out] Receives one local Transform per bone; must not be NULL (may not alias @p blend).
 */
void SkeletalPose_FinishBlend(const SkeletalBlendBone* blend, const GameSkeleton* skeleton, float totalWeight,
    Transform* outPose);

/**
 * @brief Plays a blend over a base pose: the blend's pose (its sums over their weight) replaces the base bone by bone by
 *        a share, its total weight (at most 1) times the bone's weight. Layers over the body (an arm holding a cup
 *        while the legs walk: the bone weights are the layer's weight on its mask's bones, 0 elsewhere) and clips played
 *        over a graph (every bone weight 1) use it.
 *
 * A bone takes lerp(base, blend pose, share): translations and scales straight, rotations along the shorter way
 * (normalized). A total weight of 0 (nothing in the blend) or a bone weight of 0 keeps the base bone; a total above 1
 * counts as 1.
 * @param blend The blend's running sums (SkeletalPose_AddToBlend); must not be NULL.
 * @param totalWeight The sum of the weights added to it.
 * @param boneWeights One weight per bone in [0, 1], or NULL for 1 on every bone.
 * @param boneCount How many bones.
 * @param inOutPose [in,out] The base pose, one local Transform per bone, which receives the result; must not be NULL.
 */
void SkeletalPose_BlendOver(const SkeletalBlendBone* blend, float totalWeight, const float* boneWeights,
    uint32_t boneCount, Transform* inOutPose);

/**
 * @brief Turns a chain of bones (a neck, then a head) by a rotation given in mesh space, each by its share of it:
 *        look-at.
 *
 * Each bone, parents first, turns about its own joint so that its mesh-space rotation becomes its share of
 * @p meshRotation (times @p weight) applied after the rotation it had: with shares that add up to 1 the last bone of a
 * chain ends up turned by the whole rotation (a part of a rotation about one axis composes with the rest). The bones'
 * translations stay; their children follow. The parents' rotations are read from the pose itself, so the bones may be
 * in any order as long as a parent comes before its child.
 * @param skeleton The skeleton; must not be NULL.
 * @param inOutPose [in,out] One local Transform per bone; must not be NULL.
 * @param bones The bones to turn (indices below the skeleton's bone count); must not be NULL when @p count is above 0.
 * @param shares Each bone's share of the rotation, in [0, 1]; must not be NULL when @p count is above 0.
 * @param count How many bones.
 * @param meshRotation The whole turn, in mesh space (unit length).
 * @param weight How much of it applies, in [0, 1] (0 changes nothing).
 */
void SkeletalPose_ApplyLookAt(const GameSkeleton* skeleton, Transform* inOutPose, const uint32_t* bones,
    const float* shares, uint32_t count, Quaternion meshRotation, float weight);

/**
 * @brief Turns a local pose into each bone's matrix in mesh space (the bone's space to mesh space), parents first.
 * @param skeleton The skeleton; must not be NULL.
 * @param pose One local Transform per bone; must not be NULL.
 * @param outMatrices [out] Receives one matrix per bone; must not be NULL.
 */
void SkeletalPose_ComputeModelMatrices(const GameSkeleton* skeleton, const Transform* pose, Matrix* outMatrices);

/**
 * @brief Turns bone matrices into the skinning palette: per bone the first three rows of its matrix times its inverse
 *        bind matrix, SKELETAL_PALETTE_FLOATS_PER_BONE floats (the rest pose gives identities).
 * @param skeleton The skeleton; must not be NULL.
 * @param modelMatrices One matrix per bone (SkeletalPose_ComputeModelMatrices); must not be NULL.
 * @param outPalette [out] Receives SKELETAL_PALETTE_FLOATS_PER_BONE floats per bone; must not be NULL.
 */
void SkeletalPose_ComputePalette(const GameSkeleton* skeleton, const Matrix* modelMatrices, float* outPalette);

/**
 * @brief The box, in mesh space, that holds every vertex of the skeleton's model in a pose: each bone's vertex box moved
 *        by its skinning matrix, united (a vertex lies in the boxes of every bone it follows, so its blended position
 *        lies in their union's box), with the box of the meshes that do not bend.
 * @param skeleton The skeleton; must not be NULL.
 * @param palette The pose's skinning palette (SkeletalPose_ComputePalette): its rows are the skinning matrices; must not
 *        be NULL.
 * @param outBounds [out] Receives the box; must not be NULL.
 * @returns false when the model has no vertices (no box).
 */
bool SkeletalPose_ComputeBounds(const GameSkeleton* skeleton, const float* palette, BoundingBox* outBounds);


// Functions: the mixer.
/**
 * @brief Makes an empty mixer (nothing plays: the rest pose).
 * @param self [out] The mixer; must not be NULL.
 */
void SkeletalMixer_Construct(SkeletalMixer* self);

/**
 * @brief Cross-fades to a clip: its weight rises to 1 while every other clip's falls to 0, all over @p fadeSeconds
 *        (0: at once).
 *
 * A clip already playing keeps its time unless @p isRestarted or it ended; otherwise it starts at @p startTime
 * (wrapped for a looping clip, clamped otherwise). A mixer that is full drops the clip with the smallest weight.
 * @param self The mixer; must not be NULL.
 * @param clip The clip; must not be NULL.
 * @param fadeSeconds The fade's length (a negative or non-finite value counts as 0).
 * @param isRestarted Whether a clip already playing starts over.
 * @param startTime Where a starting clip starts, in seconds.
 */
void SkeletalMixer_Play(SkeletalMixer* self, const SkeletalClipRef* clip, float fadeSeconds, bool isRestarted,
    double startTime);

/**
 * @brief Moves one clip's weight toward @p weight over @p fadeSeconds, leaving the others as they are; a clip that is
 *        not playing starts at @p startTime with weight 0 first.
 * @param self The mixer; must not be NULL.
 * @param clip The clip; must not be NULL.
 * @param weight The weight it heads to, clamped to [0, 1]; a clip that reaches 0 this way stops.
 * @param fadeSeconds The fade's length (0: at once).
 * @param startTime Where a starting clip starts, in seconds.
 */
void SkeletalMixer_SetWeight(SkeletalMixer* self, const SkeletalClipRef* clip, float weight, float fadeSeconds,
    double startTime);

/**
 * @brief Stops every clip over @p fadeSeconds: the pose fades back to the rest pose.
 * @param self The mixer; must not be NULL.
 * @param fadeSeconds The fade's length (0: at once).
 */
void SkeletalMixer_StopAll(SkeletalMixer* self, float fadeSeconds);

/**
 * @brief Advances every clip by @p seconds times its speed (looping clips wrap, others stop at their end) and every fade
 *        by @p seconds; clips faded out to 0 stop.
 * @param self The mixer; must not be NULL.
 * @param seconds The time passed (0 or below, or not finite: nothing moves).
 */
void SkeletalMixer_Advance(SkeletalMixer* self, double seconds);

/**
 * @brief Finds a clip the mixer plays.
 * @param self The mixer; must not be NULL.
 * @param library The clip's library index.
 * @param clip The clip's index in it.
 * @returns The clip's state (part of the mixer, valid until the mixer changes), or NULL when it does not play.
 */
const SkeletalClipState* SkeletalMixer_FindClip(const SkeletalMixer* self, uint32_t library, uint32_t clip);

/**
 * @brief Samples the mixer's pose: every playing clip through its library's binding, blended by weight.
 * @param self The mixer; must not be NULL.
 * @param bindings The bindings, indexed by SkeletalClipRef.Library (every playing clip's library must be below
 *        @p bindingCount); may be NULL when @p bindingCount is 0.
 * @param bindingCount How many bindings.
 * @param skeleton The skeleton the bindings bind to; must not be NULL.
 * @param scratch [out] Scratch room for one pose (one Transform per bone); must not be NULL.
 * @param blend [out] Scratch room for the blend (one entry per bone); must not be NULL.
 * @param outPose [out] Receives the pose (one local Transform per bone); must not be NULL.
 */
void SkeletalMixer_SamplePose(const SkeletalMixer* self, const SkeletalBinding* bindings, uint32_t bindingCount,
    const GameSkeleton* skeleton, Transform* scratch, SkeletalBlendBone* blend, Transform* outPose);

/**
 * @brief Adds every playing clip of the mixer to a blend by its weight (the blend is not cleared first): what
 *        SkeletalMixer_SamplePose blends, without the rest pose filling the rest, so the clips can play over another pose
 *        (SkeletalPose_BlendOver: clips from code over an animation graph's pose, T30).
 * @param self The mixer; must not be NULL.
 * @param bindings The bindings, as SkeletalMixer_SamplePose's.
 * @param bindingCount How many bindings.
 * @param skeleton The skeleton the bindings bind to; must not be NULL.
 * @param scratch [out] Scratch room for one pose; must not be NULL.
 * @param blend [in,out] The blend's running sums; must not be NULL.
 * @returns The total weight added (0 when nothing plays).
 */
float SkeletalMixer_AddToBlend(const SkeletalMixer* self, const SkeletalBinding* bindings, uint32_t bindingCount,
    const GameSkeleton* skeleton, Transform* scratch, SkeletalBlendBone* blend);
