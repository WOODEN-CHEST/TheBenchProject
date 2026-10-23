#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "AssetManager.h"
#include "Logger.h"
#include "World.h"
#include "wr/WRError.h"
#include "wr/WRVectorMath.h"


/**
 * @file WorldAnimation.h
 * @brief The animation system (T29): once a tick it plays every animator component of a world, posing its object's
 *        skinned model; it plays clips for code and answers where a bone is (sockets).
 *
 * THE TICK. WorldAnimation_Update runs in the tick's animation step (after the collision step and its events, before
 * World_UpdateTransforms; WorldData.md, "Ready for things that move"), so a pose shows where things really went. For each
 * animator of an effectively enabled object, in the world's component order, it:
 *  1. binds the animator when needed: to the world model its object's model component names (loaded under the system's
 *     own asset user; again whenever the name changes) and its skeleton, and to the animation libraries it plays (the
 *     world model's, or the placement's own while overrides_animation_libraries is on), each matched to the skeleton by
 *     bone name (SkeletalAnimation.h). A bind starts over from the rest pose and applies the rows at once: the clip row's
 *     clip plays at full weight from its start phase;
 *  2. applies the animator's rows when they changed since: a new clip row cross-fades in over fade_seconds (a clip
 *     already playing keeps its time), or out when plays_clip goes off; a clip its libraries do not have is reported
 *     once;
 *  3. advances its clips by the tick's seconds times their speed and the animator's (nothing moves while paused);
 *  4. poses the skeleton: the mixed local pose, each bone's matrix, the skinning palette and the pose's bounds, which the
 *     renderer reads from the component (WorldAnimatorComponent_GetPose). Nothing is recomputed while an animator rests
 *     in its rest pose with nothing playing.
 * A disabled object's animator keeps its pose. Every problem (a missing world model, no skeleton, a library that does not
 * load or matches no bone, an unknown clip) is logged once per animator, never per tick.
 *
 * CODE PLAYS CLIPS through WorldAnimation_PlayClip (a cross-fade: the clip rises to full weight, the others fall to 0),
 * WorldAnimation_SetClipWeight (one clip's weight alone, for blends) and WorldAnimation_StopClips; they bind the animator
 * first if it is not bound yet. A clip played from code fades the row's clip out like any other; the rows apply again
 * only when they change.
 *
 * SOCKETS. WorldAnimation_TryFindBone gives a bone's index by name, WorldAnimation_TryGetBoneWorldMatrix its world matrix
 * in the last pose (the rest pose before the first): the object's world matrix (double) times the skeleton's LOD
 * import transform times the bone's matrix, so a held item does not wobble far from the world's origin (T30 hangs items
 * on it).
 *
 * LIFETIME. Create it after the world (it borrows the world, the asset manager and the logger) and destroy it before the
 * world: destroying it releases every animator's runtime and pose, and its asset user.
 */


// Types.
/** @brief The animation system (opaque; WorldAnimation_Create). */
typedef struct WorldAnimationStruct WorldAnimation;

/**
 * @brief What the last update did (WorldAnimation_GetStatus). A plain value type.
 */
typedef struct WorldAnimationStatusStruct
{
    /** @brief The world's animators. */
    size_t AnimatorCount;
    /** @brief Those with a pose (bound to a skeleton). */
    size_t PosedCount;
    /** @brief The clips playing on all of them together. */
    size_t ClipCount;
    /** @brief The bones posed by the last update (resting animators pose none). */
    size_t BoneCount;
    /** @brief How long the last update took, in microseconds. */
    double UpdateMicroseconds;
} WorldAnimationStatus;


// Functions.
/**
 * @brief Creates the animation system of a world.
 * @param world The world (borrowed: it must outlive the system); must not be NULL.
 * @param assets The asset manager the world models and libraries load from (borrowed), or NULL: then no animator binds.
 * @param logger Where the one-time warnings go (borrowed); may be NULL.
 * @param outAnimation [out] Receives the system (WorldAnimation_Destroy); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p world or @p outAnimation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error WorldAnimation_Create(World* world, AssetManager* assets, Logger* logger, WorldAnimation** outAnimation);

/**
 * @brief Destroys the system: every animator's runtime and pose are released (they bind again under a new system), then
 *        its asset user.
 * @param self The system, or NULL (nothing happens).
 */
void WorldAnimation_Destroy(WorldAnimation* self);

/**
 * @brief The tick's animation step: binds, applies the rows, advances and poses every animator (see the file comment).
 * @param self The system; must not be NULL.
 * @param deltaSeconds The tick's seconds (0, negative or not finite: nothing advances, rows and binds still apply).
 */
void WorldAnimation_Update(WorldAnimation* self, float deltaSeconds);

/**
 * @brief Cross-fades an object's animator to a clip: the clip rises to full weight over @p fadeSeconds, every other clip
 *        falls to 0 (a clip already playing keeps its time unless @p isRestarted or it ended).
 * @param self The system; must not be NULL.
 * @param objectId The object; it must have an animator.
 * @param clip The clip's name, in the animator's libraries; must not be NULL.
 * @param fadeSeconds The fade (0: at once).
 * @param isRestarted Whether a clip already playing starts over.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_InvalidOperation when the object has no
 *          animator, or its animator no skeleton; ErrorCode_ArgumentOutOfRange when its libraries have no such clip.
 */
Error WorldAnimation_PlayClip(WorldAnimation* self, uint64_t objectId, const unsigned char* clip, float fadeSeconds,
    bool isRestarted);

/**
 * @brief Moves one clip's weight on an object's animator toward @p weight over @p fadeSeconds, leaving the others as they
 *        are (a clip not playing starts from its beginning at weight 0; one that reaches 0 stops).
 * @param self The system; must not be NULL.
 * @param objectId The object; it must have an animator.
 * @param clip The clip's name; must not be NULL.
 * @param weight The weight it heads to, in [0, 1].
 * @param fadeSeconds The fade (0: at once).
 * @returns As WorldAnimation_PlayClip.
 */
Error WorldAnimation_SetClipWeight(WorldAnimation* self, uint64_t objectId, const unsigned char* clip, float weight,
    float fadeSeconds);

/**
 * @brief Fades every clip of an object's animator out over @p fadeSeconds: back to the rest pose.
 * @param self The system; must not be NULL.
 * @param objectId The object; it must have an animator.
 * @param fadeSeconds The fade (0: at once).
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_InvalidOperation when the object has no
 *          animator.
 */
Error WorldAnimation_StopClips(WorldAnimation* self, uint64_t objectId, float fadeSeconds);

/**
 * @brief Finds a bone of an object's skeleton by name (binding its animator first if needed).
 * @param self The system; must not be NULL.
 * @param objectId The object, with an animator.
 * @param boneName The bone's name; NULL finds nothing.
 * @param outBone [out] Receives the bone's index when found; may be NULL.
 * @returns Whether the object has an animator bound to a skeleton with that bone.
 */
bool WorldAnimation_TryFindBone(WorldAnimation* self, uint64_t objectId, const unsigned char* boneName,
    uint32_t* outBone);

/**
 * @brief Returns a bone's world matrix in the object's last pose (its rest pose before the first update): the object's
 *        world matrix times the skeleton's LOD import transform times the bone's matrix.
 * @param self The system; must not be NULL.
 * @param objectId The object, with an animator bound to a skeleton.
 * @param bone The bone's index (WorldAnimation_TryFindBone).
 * @param outMatrix [out] Receives the bone's space to world space; must not be NULL.
 * @returns Whether the object has such a bone.
 */
bool WorldAnimation_TryGetBoneWorldMatrix(WorldAnimation* self, uint64_t objectId, uint32_t bone,
    Matrix4Double* outMatrix);

/**
 * @brief Returns what the last update did.
 * @param self The system; must not be NULL.
 * @returns The status.
 */
WorldAnimationStatus WorldAnimation_GetStatus(const WorldAnimation* self);
