#pragma once
#include <stdbool.h>
#include <stdint.h>
// BoundingBox and Matrix are raylib types held by value or pointer in the pose; this include is unavoidable here.
#include "raylib/raylib.h"
#include "GameSkeleton.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"
#include "wr/WRError.h"

/** @brief A world model asset (WorldModel.h): the pose remembers which one it was made for. */
typedef struct WorldModelStruct WorldModel;
/** @brief One socket of a world model (WorldModel.h): the pose carries its sockets' places (T30). */
typedef struct WorldModelSocketStruct WorldModelSocket;
/** @brief What the animation system keeps per animator (source/WorldAnimationInternal.h, private). */
typedef struct WorldAnimatorRuntimeStruct WorldAnimatorRuntime;


/**
 * @file WorldAnimatorComponent.h
 * @brief The animator component: what plays an object's skeletal animation (T29).
 *
 * WHAT. A skinned model (a model component whose world model has a skeleton, WorldModel.h) bends with a pose. The
 * animator holds that pose's playback: its rows say which clip it plays from the start and how (the speed, a start
 * phase so placements do not move in step, pause, the cross-fade time, a loop override, its own animation libraries
 * instead of its world model's); at runtime it holds the clips playing (a SkeletalMixer: their times and weights) and the
 * pose they make: the skinning palette the renderer uploads, each bone's matrix (for sockets: T30 hangs held items on
 * them) and the box that holds every vertex of the posed model.
 *
 * WHO PLAYS IT. The animation system (WorldAnimation.h), once a tick in the tick's animation step. It binds the animator
 * to the world model its object's model component names (again when that changes) and to its libraries, matching their
 * bones to the skeleton by name (SkeletalAnimation.h); it applies the rows (a new clip cross-fades in over fade_seconds),
 * advances the mix and poses the skeleton. Code plays clips through the system (WorldAnimation_PlayClip, _SetClipWeight):
 * the test frame, T30's state graph and scripts later. Without a model component, a world model with a skeleton, or a
 * clip, the model is drawn as it is bound.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Animator, &component); reach its fields with
 * WorldAnimatorComponent_FromComponent. Its table ("animator_component") holds plays_clip and clip, phase, speed,
 * is_paused, fade_seconds, overrides_is_looped and is_looped, overrides_animation_libraries and animation_libraries,
 * and (T30) overrides_animation_graph and animation_graph: the graph it plays (AnimationGraph.h) instead of its world
 * model's. Clips from the clip row and from code play over the graph, by their weight.
 * The playback itself (times, weights, the pose) is runtime state, never saved.
 */


// Macros.
/** @brief The most bytes the clip row may have (without the terminator): a clip name's. */
#define WORLD_ANIMATOR_CLIP_NAME_MAX_LENGTH ((size_t)64)
/** @brief The most animation libraries a placement may name instead of its world model's. */
#define WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES ((size_t)16)
/** @brief The most bytes a library name in that list may have. */
#define WORLD_ANIMATOR_LIBRARY_NAME_MAX_LENGTH ((size_t)128)
/** @brief The fastest an animator may play (its speed multiplies every clip's). */
#define WORLD_ANIMATOR_MAX_SPEED 100.0f
/** @brief The longest cross-fade a clip change may take, in seconds. */
#define WORLD_ANIMATOR_MAX_FADE_SECONDS 60.0f
/** @brief The cross-fade time of a new animator, in seconds. */
#define WORLD_ANIMATOR_DEFAULT_FADE_SECONDS 0.2f
/** @brief The most bytes the animation graph row may have (an asset name). */
#define WORLD_ANIMATOR_GRAPH_NAME_MAX_LENGTH ((size_t)128)


// Types.
/**
 * @brief An animator's current pose, as the renderer and sockets read it. Read-only; valid while _boneCount is above 0,
 *        until the animation system's next update (or the animator's removal, or the system's destruction, which clear
 *        it). The pointers point into the animator's runtime.
 */
typedef struct WorldAnimatorPoseStruct
{
    /** @brief The world model the pose was made for (borrowed: compared, never dereferenced by readers that did not load
     *  it themselves): a reader draws the pose only on the same world model. */
    const WorldModel* _worldModel;
    /** @brief Its skeleton (borrowed, valid as the pose is). */
    const GameSkeleton* _skeleton;
    /** @brief How many bones (0: no pose). */
    uint32_t _boneCount;
    /** @brief The skinning palette: SKELETAL_PALETTE_FLOATS_PER_BONE floats per bone (SkeletalAnimation.h). */
    const float* _palette;
    /** @brief Each bone's matrix in the skeleton's mesh space (its space to mesh space). */
    const Matrix* _boneMatrices;
    /** @brief The box, in mesh space, holding every vertex of the posed model; valid while _hasBounds. */
    BoundingBox _bounds;
    /** @brief Whether the model has vertices to bound. */
    bool _hasBounds;
    /** @brief The world model's sockets (borrowed, as _worldModel: their names and bones), and their frames in this pose
     *  in the object's own space (the socket's space to the object's), _socketCount of each (T30). */
    const WorldModelSocket* _sockets;
    const Matrix* _socketMatrices;
    uint32_t _socketCount;
    /** @brief The eyes' socket (an index in _sockets) when the world model has a first_person block (T30, G34): the
     *  camera of a first-person view rides it. Not below _socketCount without one. */
    uint32_t _eyesSocket;
    /** @brief Advances (wrapping) whenever the pose changes. */
    uint32_t _revision;
} WorldAnimatorPose;

/**
 * @brief The animator component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldAnimatorComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Whether it plays _clip from the start (default false: only what code plays). */
    bool _playsClip;
    /** @brief The clip it plays while _playsClip (owned; a clip name of at most WORLD_ANIMATOR_CLIP_NAME_MAX_LENGTH bytes,
     *  or NULL). */
    unsigned char* _clip;
    /** @brief Where the clip starts, a share of its length in [0, 1) (default 0). */
    float _phase;
    /** @brief How fast every clip plays, multiplied into each clip's own speed, in [0, WORLD_ANIMATOR_MAX_SPEED] (default
     *  1; 0 holds the pose while fades go on). */
    float _speed;
    /** @brief Whether the playback holds still: clips and fades alike (default false). */
    bool _isPaused;
    /** @brief How long a change of _clip cross-fades, in seconds (default WORLD_ANIMATOR_DEFAULT_FADE_SECONDS). */
    float _fadeSeconds;
    /** @brief Whether _isLooped decides if _clip loops, instead of its library's setting (default false). */
    bool _overridesIsLooped;
    /** @brief Whether _clip loops while _overridesIsLooped. */
    bool _isLooped;
    /** @brief Whether _animationLibraries replace the world model's libraries (default false). */
    bool _overridesAnimationLibraries;
    /** @brief The placement's own libraries (owned; NULL while empty); used while _overridesAnimationLibraries. */
    PropertyAssetNameList* _animationLibraries;
    /** @brief Whether _animationGraph replaces the world model's graph (default false; T30). */
    bool _overridesAnimationGraph;
    /** @brief The placement's own animation graph (an owned asset name of at most WORLD_ANIMATOR_GRAPH_NAME_MAX_LENGTH
     *  bytes, or NULL: none); used while _overridesAnimationGraph. */
    unsigned char* _animationGraph;
    /** @brief The animation system's state for this animator (owned by the component, managed by the system; NULL until
     *  it binds). */
    WorldAnimatorRuntime* _runtime;
    /** @brief The current pose (cleared while unbound). */
    WorldAnimatorPose _pose;
} WorldAnimatorComponent;


// Functions.
/**
 * @brief Returns the animator component's registry entry (WorldComponentType_Animator, "animator").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldAnimatorComponent_GetTypeInfo(void);

/**
 * @brief Returns the animator component's property table ("animator_component").
 * @returns The table (static).
 */
const PropertyTable* WorldAnimatorComponent_GetPropertyTable(void);

/**
 * @brief Views a component as an animator (no check: the caller knows its type).
 * @param component The component; may be NULL.
 * @returns The animator, or NULL for NULL.
 */
static inline WorldAnimatorComponent* WorldAnimatorComponent_FromComponent(WorldComponent* component)
{
    return (WorldAnimatorComponent*)(void*)component;
}

/**
 * @brief Views an animator as its component base.
 * @param self The animator; must not be NULL.
 * @returns The base.
 */
static inline WorldComponent* WorldAnimatorComponent_AsComponent(WorldAnimatorComponent* self)
{
    return &self->Base;
}

/** @brief Returns whether the animator plays its clip row. @param self The animator; must not be NULL. */
static inline bool WorldAnimatorComponent_PlaysClip(const WorldAnimatorComponent* self)
{
    return self->_playsClip;
}

/** @brief Returns the clip row (borrowed until it changes), or NULL. @param self The animator; must not be NULL. */
static inline const unsigned char* WorldAnimatorComponent_GetClip(const WorldAnimatorComponent* self)
{
    return self->_clip;
}

/** @brief Returns where the clip starts (a share of its length). @param self The animator; must not be NULL. */
static inline float WorldAnimatorComponent_GetPhase(const WorldAnimatorComponent* self)
{
    return self->_phase;
}

/** @brief Returns the playback speed. @param self The animator; must not be NULL. */
static inline float WorldAnimatorComponent_GetSpeed(const WorldAnimatorComponent* self)
{
    return self->_speed;
}

/** @brief Returns whether the playback is paused. @param self The animator; must not be NULL. */
static inline bool WorldAnimatorComponent_IsPaused(const WorldAnimatorComponent* self)
{
    return self->_isPaused;
}

/** @brief Returns the cross-fade time of a clip change, in seconds. @param self The animator; must not be NULL. */
static inline float WorldAnimatorComponent_GetFadeSeconds(const WorldAnimatorComponent* self)
{
    return self->_fadeSeconds;
}

/** @brief Returns whether the loop row overrides the clip's own setting. @param self The animator; must not be NULL. */
static inline bool WorldAnimatorComponent_OverridesIsLooped(const WorldAnimatorComponent* self)
{
    return self->_overridesIsLooped;
}

/** @brief Returns the loop row (used while it overrides). @param self The animator; must not be NULL. */
static inline bool WorldAnimatorComponent_IsLooped(const WorldAnimatorComponent* self)
{
    return self->_isLooped;
}

/** @brief Returns whether the placement's own libraries replace the world model's. @param self The animator; must not
 *  be NULL. */
static inline bool WorldAnimatorComponent_OverridesAnimationLibraries(const WorldAnimatorComponent* self)
{
    return self->_overridesAnimationLibraries;
}

/** @brief Returns whether the placement's own graph replaces the world model's. @param self The animator; must not be
 *  NULL. */
static inline bool WorldAnimatorComponent_OverridesAnimationGraph(const WorldAnimatorComponent* self)
{
    return self->_overridesAnimationGraph;
}

/** @brief Returns the placement's own graph's name (borrowed until it changes; NULL: none). @param self The animator;
 *  must not be NULL. */
static inline const unsigned char* WorldAnimatorComponent_GetAnimationGraph(const WorldAnimatorComponent* self)
{
    return self->_animationGraph;
}

/** @brief Returns the placement's own libraries (borrowed until they change; NULL while empty). @param self The animator;
 *  must not be NULL. */
static inline const PropertyAssetNameList* WorldAnimatorComponent_GetAnimationLibraries(const WorldAnimatorComponent* self)
{
    return self->_animationLibraries;
}

/**
 * @brief Returns the animator's current pose (see WorldAnimatorPose).
 * @param self The animator; must not be NULL.
 * @returns The pose (part of the animator), or NULL while it has none (unbound, no skeleton).
 */
static inline const WorldAnimatorPose* WorldAnimatorComponent_GetPose(const WorldAnimatorComponent* self)
{
    return (self->_pose._boneCount > 0U) ? &self->_pose : NULL;
}

/**
 * @brief Sets whether the animator plays its clip row (the animation system cross-fades to it, or away from it, over
 *        the fade time at its next update).
 * @param self The animator; must not be NULL.
 * @param playsClip Whether it plays the clip.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldAnimatorComponent_SetPlaysClip(WorldAnimatorComponent* self, bool playsClip);

/**
 * @brief Sets the clip row (copied).
 * @param self The animator; must not be NULL.
 * @param clip A clip name of at most WORLD_ANIMATOR_CLIP_NAME_MAX_LENGTH bytes, or NULL. Any text fits the row; one its
 *        libraries have no clip of is reported once by the animation system, which then plays nothing from the row.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a longer name.
 */
Error WorldAnimatorComponent_SetClip(WorldAnimatorComponent* self, const unsigned char* clip);

/**
 * @brief Sets where the clip starts, a share of its length in [0, 1).
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange outside [0, 1).
 */
Error WorldAnimatorComponent_SetPhase(WorldAnimatorComponent* self, float phase);

/**
 * @brief Sets the playback speed, in [0, WORLD_ANIMATOR_MAX_SPEED].
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange outside the range.
 */
Error WorldAnimatorComponent_SetSpeed(WorldAnimatorComponent* self, float speed);

/**
 * @brief Pauses or resumes the playback.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldAnimatorComponent_SetPaused(WorldAnimatorComponent* self, bool isPaused);

/**
 * @brief Sets the cross-fade time of a clip change, in [0, WORLD_ANIMATOR_MAX_FADE_SECONDS] seconds.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange outside the range.
 */
Error WorldAnimatorComponent_SetFadeSeconds(WorldAnimatorComponent* self, float fadeSeconds);

/**
 * @brief Sets the loop override: whether @p isLooped decides if the clip row loops.
 * @param self The animator; must not be NULL.
 * @param overridesIsLooped Whether the override is on (off: the clip's library decides).
 * @param isLooped Whether the clip loops while the override is on (kept either way).
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldAnimatorComponent_SetLoopOverride(WorldAnimatorComponent* self, bool overridesIsLooped, bool isLooped);

/**
 * @brief Sets the placement's own animation graph and whether it replaces its world model's (T30).
 * @param self The animator; must not be NULL.
 * @param overridesGraph Whether @p graph replaces the world model's graph (off: the world model's plays).
 * @param graph The graph's asset name (copied; at most WORLD_ANIMATOR_GRAPH_NAME_MAX_LENGTH bytes), or NULL: no graph
 *        while overriding.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a name that is not a
 *          valid asset name or too long.
 */
Error WorldAnimatorComponent_SetAnimationGraph(WorldAnimatorComponent* self, bool overridesGraph,
    const unsigned char* graph);

/**
 * @brief Sets the placement's own animation libraries and whether they replace its world model's.
 * @param self The animator; must not be NULL.
 * @param overridesLibraries Whether @p libraries replace the world model's (off: the world model's play).
 * @param libraries The libraries (copied; at most WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES valid asset names), or NULL
 *        for none.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a list that breaks the
 *          row's rules.
 */
Error WorldAnimatorComponent_SetAnimationLibraries(WorldAnimatorComponent* self, bool overridesLibraries,
    const PropertyAssetNameList* libraries);
