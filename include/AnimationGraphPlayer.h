#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Transform and Quaternion are raylib types: poses are raylib transforms, so this include is unavoidable here.
#include "raylib/raylib.h"
#include "AnimationGraph.h"
#include "GameSkeleton.h"
#include "SkeletalAnimation.h"


/**
 * @file AnimationGraphPlayer.h
 * @brief Playing an animation graph (AnimationGraph.h) on one skeleton (T30): binding it to the skeleton and its
 *        libraries, its parameters and events, its layers' states with their cross-fades and times, its timed events,
 *        and the pose it makes, look-at included.
 *
 * Pure functions over caller-owned memory: nothing here allocates, touches a world or needs a GL context, so the
 * animation system (WorldAnimation.h) keeps one binding and one instance per animator and the tests reach all of it.
 *
 * THE BINDING (AnimationGraphBinding_Build) resolves the graph's names once: each clip to the first of the animator's
 * libraries that has it (SkeletalBinding: the libraries already bound to the skeleton by bone name), with its natural
 * speed on this skeleton (its library "distance" per cycle times the height ratio, over its cycle's seconds), each
 * layer's mask to a weight per bone (the layer's weight on the masked bones and their subtrees, 0 elsewhere), and the
 * look-at bones. A clip no library has makes its state play nothing (it still runs for a second, so exit times pass);
 * a mask or look bone the skeleton lacks is left out. Both are counted for one warning.
 *
 * THE INSTANCE (AnimationGraphInstance) is the playback: every parameter's value (the built-in ones are the caller's
 * to set every tick from the character; custom ones start at their defaults), the events fired for the next advance,
 * and per layer the state it is in with up to ANIMATION_GRAPH_PLAYER_MAX_ENTRIES states cross-fading.
 *
 * ADVANCING (AnimationGraphInstance_Advance, once a tick). The events fired since the last advance (FireEvent) become
 * this tick's. Each layer takes the first of its transitions, in order, that leaves its current state (or any state)
 * whose conditions all hold and whose exit time has passed; a transition from any state into the state the layer is
 * already in is passed over unless it restarts it. Entering a state cross-fades: it rises to full weight while the
 * others fall to 0, over the transition's fade (a state still fading keeps its time unless restarted or ended). Then
 * every playing state advances: a one-clip state by the seconds times its rate (its speed, times its speed parameter,
 * times a matched speed's ratio within the state's rate limits) and its clip's own speed, wrapping when it loops; a
 * blend by phase, its clips kept in step (the blend's cycle is its clips' cycles weighted by the blend parameter). The
 * state the layer is in fires the events its playback passed: their "set" effects change parameters here, and every
 * event goes to the context (attaching and detaching objects is the caller's). Look-at's turn eases toward the look
 * parameters within the graph's limits at its turn speed.
 *
 * POSING (AnimationGraphInstance_SamplePose, once per shown frame). The first layer's states blend into the body's pose
 * (the rest pose fills what they leave); each further layer plays over it on its mask, by its weight and how much of it
 * plays something; look-at turns its bones by the eased turn times the states' look-at weights.
 */


// Macros.
/** @brief How many states one layer cross-fades at once (a fourth entering drops the lightest). */
#define ANIMATION_GRAPH_PLAYER_MAX_ENTRIES 4U


// Types.
/**
 * @brief One clip of a graph, bound: where it is and how fast it travels on the skeleton. A plain value type.
 */
typedef struct AnimationGraphBoundClipStruct
{
    /** @brief Whether one of the libraries has the clip (otherwise the rest of the record is zero). */
    bool IsFound;
    /** @brief The clip: its library (an index into the binding's libraries), its index, duration, loop and speed. */
    SkeletalClipRef Clip;
    /** @brief The metres per second it travels at rate 1 on this skeleton (its library's distance per cycle times the
     *  height ratio, over its cycle's seconds at its speed); 0 when its library gives no distance. */
    float NaturalSpeed;
} AnimationGraphBoundClip;

/**
 * @brief A graph bound to a skeleton and its libraries (AnimationGraphBinding_Build). It borrows the graph, the skeleton,
 *        the library bindings and the storage, which must outlive it. Nothing to release.
 */
typedef struct AnimationGraphBindingStruct
{
    /** @brief The graph (borrowed). */
    const AnimationGraph* Graph;
    /** @brief The skeleton (borrowed). */
    const GameSkeleton* Skeleton;
    /** @brief The libraries bound to the skeleton (borrowed; SkeletalClipRef.Library indexes them). */
    const SkeletalBinding* Libraries;
    /** @brief How many libraries. */
    uint32_t LibraryCount;
    /** @brief Every state's clips, state after state (a one-clip state has one, a blend one per point, an empty state
     *  none; borrowed storage). */
    AnimationGraphBoundClip* Clips;
    /** @brief Per state (its index among all the graph's states), the first of its clips in Clips (borrowed storage). */
    uint32_t* StateClips;
    /** @brief Per layer, one weight per bone: the layer's weight on its mask's bones (every bone without a mask), 0
     *  elsewhere (borrowed storage, layer after layer). */
    float* BoneWeights;
    /** @brief The bones look-at turns, found on the skeleton, and their shares. */
    uint32_t LookBones[ANIMATION_GRAPH_MAX_LOOK_BONES];
    float LookShares[ANIMATION_GRAPH_MAX_LOOK_BONES];
    uint32_t LookBoneCount;
    /** @brief How many of the graph's clip references no library has, and the first one's name (part of the graph). */
    uint32_t MissingClipCount;
    const unsigned char* FirstMissingClip;
    /** @brief How many mask and look-at bones the skeleton lacks, and the first one's name (part of the graph). */
    uint32_t MissingBoneCount;
    const unsigned char* FirstMissingBone;
} AnimationGraphBinding;

/**
 * @brief One state a layer plays: its place in its playback and its weight in the cross-fade. A plain value type.
 */
typedef struct AnimationGraphEntryStruct
{
    /** @brief The state (an index in its layer). */
    uint32_t State;
    /** @brief Its weight now, in [0, 1], the one its fade started from and the one it heads to. */
    float Weight;
    float StartWeight;
    float TargetWeight;
    /** @brief Its fade's length and how far the fade is, in seconds (Elapsed >= Seconds: done). */
    float FadeSeconds;
    float FadeElapsed;
    /** @brief Where it is: seconds into its clip (a one-clip state), or its phase in [0, 1) (a blend; 1 once a blend
     *  that does not loop ended). */
    double Time;
    /** @brief How much of its cycle it played since it entered: its clip's share for one that does not loop (at most
     *  1), the cycles counted for one that loops. What a transition's exit time waits for. */
    double Cycles;
    /** @brief Whether a clip (or blend) that does not loop reached its end; it holds its last pose. */
    bool HasEnded;
    /** @brief Whether it advanced since it (re)started: the first advance fires the events at its very start too. */
    bool HasAdvanced;
} AnimationGraphEntry;

/**
 * @brief One layer's playback: the state it is in and the states it cross-fades.
 */
typedef struct AnimationGraphLayerPlaybackStruct
{
    /** @brief The state the layer is in (an index in its layer): the last one entered. */
    uint32_t Current;
    /** @brief The states playing, in the order they entered (the current one among them). */
    AnimationGraphEntry Entries[ANIMATION_GRAPH_PLAYER_MAX_ENTRIES];
    /** @brief How many entries are in use (at least 1). */
    uint32_t EntryCount;
} AnimationGraphLayerPlayback;

/**
 * @brief One graph's playback (AnimationGraphInstance_Construct). It borrows the graph and the storage, which must
 *        outlive it. Nothing to release. Read its fields; change it through the functions.
 */
typedef struct AnimationGraphInstanceStruct
{
    /** @brief The graph (borrowed). */
    const AnimationGraph* Graph;
    /** @brief Every parameter's value, AnimationGraph_GetParameterSlotCount of them (borrowed storage). */
    float* Parameters;
    /** @brief The events of the last advance (bit i: event index i). */
    uint64_t Events;
    /** @brief The events fired for the next advance. */
    uint64_t PendingEvents;
    /** @brief Per layer, its playback (borrowed storage). */
    AnimationGraphLayerPlayback* Layers;
    /** @brief Look-at's turn now, radians: toward its left (yaw) and up (pitch) relative to the body, eased toward the
     *  look parameters. */
    float LookYaw;
    float LookPitch;
    /** @brief How much look-at turns the head after the last advance: the playing states' look-at weights, blended. */
    float LookWeight;
} AnimationGraphInstance;

/**
 * @brief What an advance asks of and tells the caller.
 */
typedef struct AnimationGraphContextStruct
{
    /**
     * @brief Whether something hangs on a socket of the object (socket conditions). NULL: nothing does.
     * @param user The context's User.
     * @param socket The socket's name (part of the graph).
     * @returns Whether an object hangs on it.
     */
    bool (*IsSocketHolding)(void* user, const unsigned char* socket);
    /**
     * @brief Receives every event a state fires, in order, after its "set" effects were applied (attaching, detaching
     *        and queueing it for the game are the caller's). May be NULL.
     * @param user The context's User.
     * @param layer The layer.
     * @param state The state (an index in the layer).
     * @param event The event (part of the graph).
     */
    void (*OnEvent)(void* user, uint32_t layer, uint32_t state, const AnimationGraphEventRecord* event);
    /** @brief Passed back to both. */
    void* User;
} AnimationGraphContext;


// Functions: the binding.
/**
 * @brief How many bytes of storage AnimationGraphBinding_Build needs for a graph on a skeleton.
 * @param graph The graph; must not be NULL.
 * @param skeleton The skeleton; must not be NULL.
 * @returns The size in bytes (aligned for floats and integers).
 */
size_t AnimationGraphBinding_GetStorageSize(const AnimationGraph* graph, const GameSkeleton* skeleton);

/**
 * @brief Binds a graph to a skeleton and its libraries (see the file comment).
 * @param graph The graph (borrowed: it must outlive the binding); must not be NULL.
 * @param skeleton The skeleton (borrowed likewise); must not be NULL.
 * @param libraries The libraries bound to the skeleton (borrowed likewise), searched in order for each clip; may be NULL
 *        when @p libraryCount is 0.
 * @param libraryCount How many libraries.
 * @param storage [out] At least AnimationGraphBinding_GetStorageSize bytes, aligned as Memory_Allocate aligns (borrowed
 *        likewise); must not be NULL.
 * @param outBinding [out] Receives the binding; must not be NULL.
 */
void AnimationGraphBinding_Build(const AnimationGraph* graph, const GameSkeleton* skeleton,
    const SkeletalBinding* libraries, uint32_t libraryCount, void* storage, AnimationGraphBinding* outBinding);


// Functions: the instance.
/**
 * @brief How many bytes of storage an instance of a graph needs.
 * @param graph The graph; must not be NULL.
 * @returns The size in bytes.
 */
size_t AnimationGraphInstance_GetStorageSize(const AnimationGraph* graph);

/**
 * @brief Starts a graph's playback: every parameter at its default (built-in ones 0), no events, every layer in its
 *        entry state at full weight from its start, look-at straight ahead.
 * @param self [out] The instance; must not be NULL.
 * @param graph The graph (borrowed: it must outlive the instance); must not be NULL.
 * @param storage [out] At least AnimationGraphInstance_GetStorageSize bytes, aligned as Memory_Allocate aligns
 *        (borrowed likewise); must not be NULL.
 */
void AnimationGraphInstance_Construct(AnimationGraphInstance* self, const AnimationGraph* graph, void* storage);

/**
 * @brief Sets a parameter: a built-in one as it is, a custom float clamped into its bounds, a custom boolean to 1 when
 *        above 0.5 and 0 otherwise. Values that are not finite are ignored.
 * @param self The instance; must not be NULL.
 * @param index The parameter's index (AnimationGraph_TryFindParameter); one past the graph's is ignored.
 * @param value The value.
 */
void AnimationGraphInstance_SetParameter(AnimationGraphInstance* self, uint32_t index, float value);

/**
 * @brief Returns a parameter's value.
 * @param self The instance; must not be NULL.
 * @param index The parameter's index; one past the graph's reads 0.
 * @returns The value.
 */
float AnimationGraphInstance_GetParameter(const AnimationGraphInstance* self, uint32_t index);

/**
 * @brief Fires an event for the next advance (a character's event, an action's end request, a custom trigger).
 * @param self The instance; must not be NULL.
 * @param index The event's index (AnimationGraph_TryFindEvent); one past the graph's is ignored.
 */
void AnimationGraphInstance_FireEvent(AnimationGraphInstance* self, uint32_t index);

/**
 * @brief Moves a layer into a state now, cross-fading over @p fadeSeconds, as a transition would (an action entering its
 *        first state).
 * @param self The instance; must not be NULL.
 * @param layer The layer; one past the graph's is ignored.
 * @param state The state (an index in the layer); one past the layer's is ignored.
 * @param fadeSeconds The fade (0 or below: at once).
 * @param isRestarted Whether a state already playing (fading) starts over; one that ended always does.
 */
void AnimationGraphInstance_EnterState(AnimationGraphInstance* self, uint32_t layer, uint32_t state, float fadeSeconds,
    bool isRestarted);

/**
 * @brief Returns the state a layer is in.
 * @param self The instance; must not be NULL.
 * @param layer The layer, below the graph's layer count.
 * @returns The state (an index in the layer).
 */
static inline uint32_t AnimationGraphInstance_GetCurrentState(const AnimationGraphInstance* self, uint32_t layer)
{
    return self->Layers[layer].Current;
}

/**
 * @brief Advances the playback by one tick (see the file comment).
 * @param self The instance; must not be NULL.
 * @param binding The graph bound to the skeleton; must not be NULL.
 * @param seconds The tick's seconds, the animator's speed in them (0, negative or not finite: nothing moves, but
 *        transitions and events still happen).
 * @param context What the advance asks and tells; may be NULL (no socket holds anything, events go nowhere).
 */
void AnimationGraphInstance_Advance(AnimationGraphInstance* self, const AnimationGraphBinding* binding, double seconds,
    const AnimationGraphContext* context);

/**
 * @brief Samples the playback's pose (see the file comment).
 * @param self The instance; must not be NULL.
 * @param binding The graph bound to the skeleton; must not be NULL.
 * @param importRotation The rotation of the skeleton's import transform (mesh space to the object's), so look-at turns
 *        about the object's up and right axes whatever way the model was exported.
 * @param scratch [out] Scratch room for one pose (one Transform per bone); must not be NULL.
 * @param blend [out] Scratch room for a blend (one entry per bone); must not be NULL.
 * @param outPose [out] Receives the pose (one local Transform per bone); must not be NULL (may not alias the scratch).
 */
void AnimationGraphInstance_SamplePose(const AnimationGraphInstance* self, const AnimationGraphBinding* binding,
    Quaternion importRotation, Transform* scratch, SkeletalBlendBone* blend, Transform* outPose);

/**
 * @brief Computes a blend state's clip weights for a parameter value: the two points around it share it linearly, a
 *        value below the first point (above the last) gives that point alone.
 * @param graph The graph; must not be NULL.
 * @param state The blend state; must not be NULL.
 * @param value The blend parameter's value.
 * @param outWeights [out] Receives one weight per point (they add up to 1); must not be NULL, room for the state's
 *        points.
 */
void AnimationGraphPlayer_GetBlendWeights(const AnimationGraph* graph, const AnimationGraphState* state, float value,
    float* outWeights);
