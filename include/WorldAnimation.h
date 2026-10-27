#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "AnimationGraph.h"
#include "AssetManager.h"
#include "Logger.h"
#include "World.h"
#include "wr/WRError.h"
#include "wr/WRVectorMath.h"


/**
 * @file WorldAnimation.h
 * @brief The animation system: once a tick it plays every animator component of a world, posing its object's
 *        skinned model; it plays clips for code and answers where a bone is (sockets).
 *
 * THE TICK. WorldAnimation_Update runs in the tick's animation step (after the collision step and its events, before
 * World_UpdateTransforms; WorldData.md, "Ready for things that move"), so the playback follows where things really
 * went. For each animator of an effectively enabled object, in the world's component order, it:
 *  1. binds the animator when needed: to the world model its object's model component names (loaded under the system's
 *     own asset user; again whenever the name changes) and its skeleton, and to the animation libraries it plays (the
 *     world model's, or the placement's own while overrides_animation_libraries is on), each matched to the skeleton by
 *     bone name (SkeletalAnimation.h). A bind publishes the rest pose at once and applies the rows: the clip row's clip
 *     plays at full weight from its start phase;
 *  2. applies the animator's rows when they changed since: a new clip row cross-fades in over fade_seconds (a clip
 *     already playing keeps its time), or out when plays_clip goes off; a clip its libraries do not have is reported
 *     once;
 *  3. advances its clips by the tick's seconds times their speed and the animator's (nothing moves while paused).
 *
 * THE POSE STEP. A rendered frame shows one pose in four to eight ticks, so poses are made
 * once per shown frame: WorldAnimation_Pose runs in the frame's step right before its render (GameFrame's PrepareRender:
 * the update side, rendering never writes), and for every effectively enabled animator whose playback changed since its
 * last pose it samples the mixed local pose and computes each bone's matrix, the skinning palette and the pose's bounds,
 * which the renderer reads from the component (WorldAnimatorComponent_GetPose). An animator resting in its rest pose with
 * nothing playing is not posed again. A pose is what the playback was after the last update: computing it later shows
 * the same thing, so nothing is interpolated. Code that needs a fresh pose inside a tick (a test) calls the pose step
 * itself.
 * A disabled object's animator keeps its pose. Every problem (a missing world model, no skeleton, a library that does not
 * load or matches no bone, an unknown clip) is logged once per animator, never per tick.
 *
 * CODE PLAYS CLIPS through WorldAnimation_PlayClip (a cross-fade: the clip rises to full weight, the others fall to 0),
 * WorldAnimation_SetClipWeight (one clip's weight alone, for blends) and WorldAnimation_StopClips; they bind the animator
 * first if it is not bound yet. A clip played from code fades the row's clip out like any other; the rows apply again
 * only when they change.
 *
 * SOCKETS. WorldAnimation_TryFindBone gives a bone's index by name, WorldAnimation_TryGetBoneWorldMatrix its world matrix
 * in the last published pose (the rest pose before the first): the object's world matrix (double) times the skeleton's LOD
 * import transform times the bone's matrix, so a held item does not wobble far from the world's origin (held items hang
 * on it).
 *
 * GRAPHS. An animator whose world model names an animation graph (AnimationGraph.h), or
 * that names its own (overrides_animation_graph), plays it: the graph is bound with the animator (its clips from the
 * animator's libraries) and runs in the tick's step before the clips from the rows and code, which play over the
 * graph's pose by their weight (a clip at full weight covers it; fading out, it hands back to the graph). Every tick the
 * graph's built-in parameters are filled from the object's character component, if it has one (its velocity relative
 * to its ground, along and across its facing, its ground, flight, crouch, time in the air, turn rate, its look relative
 * to its body, its last landing), and its character's events of that tick fire the graph's events; code sets custom
 * parameters and fires triggers (WorldAnimation_SetParameter, _FireTrigger). Socket conditions ask whether an object
 * hangs on a socket of the animator's object. Events the graph's states fire are queued (WorldAnimation_GetEvents),
 * after their effects: "set" changes parameters, "attach" hangs the action's target on a socket of the object
 * (World_SetParentSocket, gripped by the target's own socket when the event names one), "detach" puts what hangs on a
 * socket back in the world where it is.
 *
 * ACTIONS. WorldAnimation_StartAction starts one of the graph's actions on an object, for the player and NPCs
 * alike, with a target when the action takes one. An action with an anchor (a socket of its target: a bench's seat)
 * first has the object's character slide there and face the socket's way (WorldCharacterComponent_SetActionControl;
 * the character system moves it, swept), then enters the action's first state; one without enters at once. While it
 * lasts the character's movement is locked as the action says. It lasts while its layer is in one of its states, so
 * the graph's own transitions take it out (the end_action event, which WorldAnimation_EndAction fires, is how a driver
 * asks). Starting another action replaces one that approaches, or plays on the same layer (the new one's first state
 * takes the layer); one playing on another layer must end first, or its layer would stay in its states with nothing to
 * take it out. Start, end and failure are queued as events.
 *
 * LIFETIME. Create it after the world (it borrows the world, the asset manager and the logger) and destroy it before the
 * world: destroying it releases every animator's runtime and pose, frees the characters its actions held, and releases
 * its asset user.
 */


// Macros.
/** @brief Room for a queued event's name with its terminator. */
#define WORLD_ANIMATION_EVENT_NAME_CAPACITY (ANIMATION_GRAPH_NAME_MAX_LENGTH + 1U)


// Types.
/** @brief The animation system (opaque; WorldAnimation_Create). */
typedef struct WorldAnimationStruct WorldAnimation;

/**
 * @brief What a queued event says. The values are stable.
 */
typedef enum WorldAnimationEventKindEnum
{
    /** @brief A state of the object's graph fired a timed event: Name is the event's. */
    WorldAnimationEventKind_Graph = 0,
    /** @brief An action entered its first state (after its approach): Name is the action's. */
    WorldAnimationEventKind_ActionStarted = 1,
    /** @brief An action ended (its layer left its states, another replaced it, the animator rebound): Name is the
     *  action's. */
    WorldAnimationEventKind_ActionEnded = 2,
    /** @brief An action could not reach its anchor (the character was blocked, or is no character): Name is the
     *  action's. */
    WorldAnimationEventKind_ActionFailed = 3
} WorldAnimationEventKind;

/**
 * @brief One queued event. A plain value type.
 */
typedef struct WorldAnimationEventStruct
{
    /** @brief What happened. */
    WorldAnimationEventKind Kind;
    /** @brief The animator's object. */
    uint64_t ObjectId;
    /** @brief The event's or the action's name (a copy, NUL-terminated). */
    unsigned char Name[WORLD_ANIMATION_EVENT_NAME_CAPACITY];
    /** @brief The object's action's target (0: none). */
    uint64_t TargetId;
    /** @brief The update it happened in (WorldAnimation_GetTick). */
    uint64_t Tick;
} WorldAnimationEvent;

/**
 * @brief What the last update did (WorldAnimation_GetStatus). A plain value type.
 */
typedef struct WorldAnimationStatusStruct
{
    /** @brief The world's animators, at the last update. */
    size_t AnimatorCount;
    /** @brief Those with a pose (bound to a skeleton), at the last update. */
    size_t PosedCount;
    /** @brief The clips playing on all of them together, at the last update. */
    size_t ClipCount;
    /** @brief The animators playing a graph, at the last update. */
    size_t GraphCount;
    /** @brief The actions playing or approaching, at the last update. */
    size_t ActionCount;
    /** @brief The animators whose pose the last pose step recomputed (resting and unchanged ones are not). */
    size_t RecomputedCount;
    /** @brief The bones the last pose step posed. */
    size_t BoneCount;
    /** @brief How long the last update (the tick's part) took, in microseconds. */
    double UpdateMicroseconds;
    /** @brief How long the last pose step took, in microseconds. */
    double PoseMicroseconds;
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
 * @brief The tick's animation step: binds, applies the rows and advances every animator (see the file comment); the
 *        poses wait for WorldAnimation_Pose.
 * @param self The system; must not be NULL.
 * @param deltaSeconds The tick's seconds (0, negative or not finite: nothing advances, rows and binds still apply).
 */
void WorldAnimation_Update(WorldAnimation* self, float deltaSeconds);

/**
 * @brief The pose step, once per shown frame before its render (see the file comment): poses every effectively enabled
 *        animator whose playback changed since its last pose.
 *
 * Costs one sampled and blended pose, its bone matrices, palette and bounds per changed animator; nothing for the
 * others. Call it after the frame's last update, then World_UpdateTransforms (what hangs on the poses' bones follows
 * them), then render.
 * @param self The system; must not be NULL.
 */
void WorldAnimation_Pose(WorldAnimation* self);

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

/**
 * @brief Sets a parameter of an object's graph: a built-in one (the character's are overwritten every tick) or a
 *        custom float (clamped into its bounds) or bool (true above 0.5).
 * @param self The system; must not be NULL.
 * @param objectId The object; it must have an animator playing a graph.
 * @param name The parameter's name; must not be NULL.
 * @param value The value (finite).
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments or a value that is not finite;
 *          ErrorCode_InvalidOperation when the object has no animator playing a graph; ErrorCode_ArgumentOutOfRange
 *          when its graph has no such parameter.
 */
Error WorldAnimation_SetParameter(WorldAnimation* self, uint64_t objectId, const unsigned char* name, float value);

/**
 * @brief Reads a parameter of an object's graph.
 * @param self The system; must not be NULL.
 * @param objectId The object.
 * @param name The parameter's name; NULL finds nothing.
 * @param outValue [out] Receives the value when found; must not be NULL.
 * @returns Whether the object's animator plays a graph with that parameter.
 */
bool WorldAnimation_TryGetParameter(WorldAnimation* self, uint64_t objectId, const unsigned char* name,
    float* outValue);

/**
 * @brief Fires a custom trigger of an object's graph for its next update.
 * @param self The system; must not be NULL.
 * @param objectId The object; it must have an animator playing a graph.
 * @param name The trigger's name; must not be NULL.
 * @returns As WorldAnimation_SetParameter (ErrorCode_ArgumentOutOfRange when its graph has no such trigger).
 */
Error WorldAnimation_FireTrigger(WorldAnimation* self, uint64_t objectId, const unsigned char* name);

/**
 * @brief Gives the state a layer of an object's graph is in.
 * @param self The system; must not be NULL.
 * @param objectId The object.
 * @param layer The layer's name, or NULL for the first layer.
 * @param outState [out] Receives the state's name (part of the graph: valid while the animator stays bound); must not be
 *        NULL.
 * @returns Whether the object's animator plays a graph with that layer.
 */
bool WorldAnimation_TryGetState(WorldAnimation* self, uint64_t objectId, const unsigned char* layer,
    const unsigned char** outState);

/**
 * @brief Starts one of the actions of an object's graph (see the file comment): the player and NPCs alike.
 *
 * An action already approaching, or playing on the same layer, ends first (its states fade as the new one enters); one
 * playing on another layer is not replaced: end it first (WorldAnimation_EndAction) and start this one once it has
 * ended. With an anchor the object's character slides onto the anchor (the target's socket) before the action's first
 * state; a target farther from the character's feet than the anchor's reach is refused.
 * @param self The system; must not be NULL.
 * @param objectId The object; it must have an animator playing a graph.
 * @param action The action's name; must not be NULL.
 * @param targetId The object the action is done to or with (0: none).
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_InvalidOperation when the object has no
 *          animator playing a graph, or plays an action on another layer, or the action needs a target it lacks, or
 *          takes none and was given one, or its anchor needs a character the object is not;
 *          ErrorCode_ArgumentOutOfRange when the graph has no such action,
 *          the target does not exist or has no such socket, or its anchor is out of reach.
 */
Error WorldAnimation_StartAction(WorldAnimation* self, uint64_t objectId, const unsigned char* action,
    uint64_t targetId);

/**
 * @brief Asks the action an object plays to end: fires its graph's end_action event (its states move on their way out
 *        on it); an action still approaching its anchor ends at once.
 * @param self The system; must not be NULL.
 * @param objectId The object.
 * @returns Whether the object had an action to end.
 */
bool WorldAnimation_EndAction(WorldAnimation* self, uint64_t objectId);

/**
 * @brief Gives the action an object plays (or approaches).
 * @param self The system; must not be NULL.
 * @param objectId The object.
 * @param outAction [out] Receives the action's name (part of the graph); may be NULL.
 * @param outTargetId [out] Receives its target (0: none); may be NULL.
 * @returns Whether the object has an action.
 */
bool WorldAnimation_TryGetAction(WorldAnimation* self, uint64_t objectId, const unsigned char** outAction,
    uint64_t* outTargetId);

/**
 * @brief Returns the queued events, oldest first.
 * @param self The system; must not be NULL.
 * @param outCount [out] Receives how many; must not be NULL.
 * @returns The events (borrowed: valid until the next update or clear), or NULL with a count of 0.
 */
const WorldAnimationEvent* WorldAnimation_GetEvents(const WorldAnimation* self, size_t* outCount);

/**
 * @brief Drops the queued events (after handling them).
 * @param self The system; must not be NULL.
 */
void WorldAnimation_ClearEvents(WorldAnimation* self);

/**
 * @brief Returns how many updates have run.
 * @param self The system; must not be NULL.
 * @returns The count.
 */
uint64_t WorldAnimation_GetTick(const WorldAnimation* self);
