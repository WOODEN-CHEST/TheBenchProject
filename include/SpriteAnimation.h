#pragma once
#include "raylib/raylib.h"
#include "wr/WRMemory.h"
#include "wr/WREvent.h"
#include "wr/WRError.h"

/**
 * 2D animation related things.
 *
 * Sprite animations are made up of frames. Each frame can either have a standalone texture or have the texture be borrowed
 * from a sprite sheet. There is no requirement that all frames be of same type.
 * The sprite animation does not own any of the buffers and textured passed into it, thus deconstructing a sprite animation
 * doesn't free the data passed into it. That has to be done from where the animation was constructed.
 *
 * An animation has a default framerate, frame step (frame increment amount per frame change), whether they are looped or running.
 *
 * Animation instances contain actual animation state and are instances of animations.
 * Animation instances created via their constructor start with no backed animation (this is valid).
 * Ones created via the animation class' create instance method start out with the backed animation and its default parameters.
 * The sprite animation instance events are raised just after they happen, so the animation state is post-event.
 * Subscribers of the sprite animation events are allowed to modify the sprite animation during the event handler, because
 * everything has already been processed.
 **/


/** @brief Minimum accepted frames-per-second for an animation instance (inclusive). */
#define SPRITE_ANIMATION_FPS_MIN 0.0
/** @brief Maximum accepted frames-per-second for an animation instance (inclusive). */
#define SPRITE_ANIMATION_FPS_MAX 1e30
/** @brief Minimum accepted frame step (most negative per-change frame increment). */
#define SPRITE_ANIMATION_FRAME_STEP_MIN -1000000000
/** @brief Maximum accepted frame step (most positive per-change frame increment). */
#define SPRITE_ANIMATION_FRAME_STEP_MAX 1000000000

/** @brief Default frames-per-second applied to instances created from an animation; 0 means the animation does not advance. */
#define SPRITE_ANIMATION_FPS_DEFAULT 0.0
/** @brief Default frame step applied to instances created from an animation; 0 means the frame index never moves. */
#define SPRITE_ANIMATION_FRAME_STEP_DEFAULT 0
/** @brief Default running state applied to instances created from an animation. */
#define SPRITE_ANIMATION_IS_RUNNING_DEFAULT false
/** @brief Default looping state applied to instances created from an animation. */
#define SPRITE_ANIMATION_IS_LOOPED_DEFAULT false



/**
 * @brief A single animation frame: a texture and the region of it to draw.
 *
 * A frame either owns a standalone texture or borrows one from a sprite sheet, as indicated by
 * @c _isTextureStandalone; the animation itself never owns the texture regardless. Frames are stored
 * by value in the GenericBuffer handed to SpriteAnimation_Construct1.
 */
typedef struct SpriteAnimationFrameStruct
{
    /** @brief The texture this frame draws from; borrowed, never owned by the animation. */
    Texture2D _texture;
    /** @brief The region of @c _texture that this frame occupies, in texture pixel coordinates. */
    Rectangle _areaInTexture;
    /** @brief true if @c _texture is a standalone texture for this frame; false if it is borrowed from a sprite sheet. */
    bool _isTextureStandalone;
} SpriteAnimationFrame;

/**
 * @brief An animation definition: an ordered set of frames plus the default playback parameters.
 *
 * Holds a borrowed buffer of frames and the default FPS, frame step, running and looping states that
 * SpriteAnimation_CreateInstance copies onto new instances. The frame buffer is not owned and must
 * outlive the animation. Create with SpriteAnimation_Construct1 and release with
 * SpriteAnimation_Deconstruct (which does not free the frames).
 */
typedef struct SpriteAnimationStruct
{
    /** @brief Borrowed buffer of SpriteAnimationFrame records (element size must equal sizeof(SpriteAnimationFrame)); not owned. */
    GenericBuffer* _frames;
    /** @brief Default frames-per-second copied onto new instances; within [SPRITE_ANIMATION_FPS_MIN, SPRITE_ANIMATION_FPS_MAX]. */
    double _defaultFPS;
    /** @brief Default per-change frame increment copied onto new instances; within [SPRITE_ANIMATION_FRAME_STEP_MIN, SPRITE_ANIMATION_FRAME_STEP_MAX]. */
    int64_t _defaultFrameStep;
    /** @brief Default running state copied onto new instances. */
    bool _defaultIsRunning;
    /** @brief Default looping state copied onto new instances. */
    bool _defaultIsLooped;
} SpriteAnimation;

/** @brief Opaque forward declaration of a sprite animation instance; the full layout is defined below. */
typedef struct SpriteAnimationInstanceStruct SpriteAnimationInstance;

/**
 * @brief The playback boundary an instance reached during an update, carried by the state-reach event.
 */
typedef enum SpriteAnimationReachedStateEnum
{
    /** @brief No boundary was reached. */
    SpriteAnimationReachedState_None,
    /** @brief A non-looping animation reached its final frame and stopped. */
    SpriteAnimationReachedState_End,
    /** @brief A looping animation wrapped past a boundary and continued. */
    SpriteAnimationReachedState_Loop
} SpriteAnimationReachedState;

/**
 * @brief The direction in which playback was moving when a boundary was reached.
 *
 * Derived from the sign of the instance's frame step: positive is forwards, negative is backwards.
 */
typedef enum SpriteAnimationStateDirectionEnum
{
    /** @brief No direction (frame step was zero / not moving). */
    SpriteAnimationStateDirection_None,
    /** @brief Playback was advancing towards higher frame indices. */
    SpriteAnimationStateDirection_Forwards,
    /** @brief Playback was advancing towards lower frame indices. */
    SpriteAnimationStateDirection_Backwards
} SpriteAnimationStateDirection;

/**
 * @brief Event payload specific to an End boundary.
 */
typedef struct SpriteAnimationEndArgsStruct
{
    /** @brief The direction playback was moving in when the animation ended. */
    SpriteAnimationStateDirection _direction;
} SpriteAnimationEndArgs;

/**
 * @brief Event payload specific to a Loop boundary.
 */
typedef struct SpriteAnimationLoopArgsStruct
{
    /** @brief The direction playback was moving in when the animation looped. */
    SpriteAnimationStateDirection _direction;
} SpriteAnimationLoopArgs;

/**
 * @brief The boundary-specific payload of a state-reach event; which member is valid depends on the reached state.
 *
 * Read @c _endArgs when the reached state is SpriteAnimationReachedState_End and @c _loopArgs when it
 * is SpriteAnimationReachedState_Loop.
 */
typedef union SpriteAnimationEventSpecificArgsUnion
{
    /** @brief Valid when the reached state is SpriteAnimationReachedState_End. */
    SpriteAnimationEndArgs _endArgs;
    /** @brief Valid when the reached state is SpriteAnimationReachedState_Loop. */
    SpriteAnimationLoopArgs _loopArgs;
} SpriteAnimationEventSpecificArgs;

/**
 * @brief Arguments passed to handlers of an instance's state-reach event.
 *
 * A pointer to one of these is delivered as the event payload when an instance reaches an end or loop
 * boundary during SpriteAnimationInstance_Update. The event is raised after the state has been applied,
 * so the instance already reflects the post-boundary state.
 */
typedef struct SpriteAnimationStateReachEventArgsStruct
{
    /** @brief The instance that reached the boundary; borrowed, valid for the duration of the handler. */
    SpriteAnimationInstance* _animationInstance;
    /** @brief Which boundary was reached (End or Loop). */
    SpriteAnimationReachedState _reachedState;
    /** @brief Boundary-specific payload; the active member is selected by @c _reachedState. */
    SpriteAnimationEventSpecificArgs _args;

} SpriteAnimationStateReachEventArgs;

/**
 * @brief A playing (or paused) instance of a SpriteAnimation, holding all mutable playback state.
 *
 * Carries the current frame index, timing, and per-instance FPS/step/running/looping values, plus an
 * event raised when playback reaches an end or loop boundary. The backing animation (@c _source) is
 * borrowed and may be NULL, which is a valid "no animation" state. Construct with
 * SpriteAnimationInstance_Construct1 (no source) or SpriteAnimation_CreateInstance (source + defaults),
 * and always release with SpriteAnimationInstance_Deconstruct to tear down the embedded event.
 */
struct SpriteAnimationInstanceStruct
{
    /** @brief The animation this instance plays; borrowed and may be NULL when there is no backing animation. */
    SpriteAnimation* _source;
    /** @brief Index of the current frame within the source's frame buffer. */
    size_t _frameIndex;
    /** @brief Seconds accumulated towards the next frame change since the last one, in seconds. */
    double _timeSinceFrameChangeSeconds;
    /** @brief Frame changes per second; within [SPRITE_ANIMATION_FPS_MIN, SPRITE_ANIMATION_FPS_MAX]. 0 pauses advancement. */
    double _fps;
    /** @brief Frames advanced per frame change; sign selects direction, within [SPRITE_ANIMATION_FRAME_STEP_MIN, SPRITE_ANIMATION_FRAME_STEP_MAX]. */
    int64_t _frameStep;
    /** @brief Whether the instance advances on update. */
    bool _isRunning;
    /** @brief Whether the instance wraps around at the ends instead of stopping. */
    bool _isLooped;
    /** @brief Event raised (after the state is applied) when the instance reaches an end or loop boundary; payload is a SpriteAnimationStateReachEventArgs*. */
    WREvent _stateReachEvent;
};


// Functions.
/**
 * @brief Initializes an animation over a borrowed frame buffer and sets its default parameters.
 *
 * Stores @p frames by reference (not owned; it must outlive the animation) and seeds the default
 * FPS/step/running/looping values from the SPRITE_ANIMATION_*_DEFAULT constants.
 * @param self The animation to initialize; must not be NULL.
 * @param frames Buffer of SpriteAnimationFrame records; must not be NULL and its element size must equal
 *        sizeof(SpriteAnimationFrame). Borrowed and must outlive @p self.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p frames is NULL, or
 *          @p frames has an element size other than sizeof(SpriteAnimationFrame).
 */
Error SpriteAnimation_Construct1(SpriteAnimation* self, GenericBuffer* frames);

/**
 * @brief Initializes an animation over a borrowed frame buffer with explicit default parameters.
 *
 * Like SpriteAnimation_Construct1 but sets the default FPS/step/running/looping values that
 * SpriteAnimation_CreateInstance copies onto new instances, instead of the SPRITE_ANIMATION_*_DEFAULT
 * constants. The frame buffer is borrowed (not owned) and must outlive @p self.
 * @param self The animation to initialize; must not be NULL.
 * @param frames Buffer of SpriteAnimationFrame records; must not be NULL and its element size must equal
 *        sizeof(SpriteAnimationFrame). Borrowed and must outlive @p self.
 * @param defaultFPS Default frames-per-second; must be finite and within
 *        [SPRITE_ANIMATION_FPS_MIN, SPRITE_ANIMATION_FPS_MAX].
 * @param defaultFrameStep Default per-change frame increment; must be within
 *        [SPRITE_ANIMATION_FRAME_STEP_MIN, SPRITE_ANIMATION_FRAME_STEP_MAX].
 * @param defaultIsRunning Default running state.
 * @param defaultIsLooped Default looping state.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p frames is NULL or
 *          @p frames has the wrong element size; ErrorCode_ArgumentOutOfRange if @p defaultFPS or
 *          @p defaultFrameStep is out of range.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error SpriteAnimation_Construct2(SpriteAnimation* self,
    GenericBuffer* frames,
    double defaultFPS,
    int64_t defaultFrameStep,
    bool defaultIsRunning,
    bool defaultIsLooped);

/**
 * @brief Releases an animation, leaving its borrowed frame buffer untouched.
 *
 * Clears the animation's fields; the frame buffer is not freed.
 * @param self The animation to deconstruct; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error SpriteAnimation_Deconstruct(SpriteAnimation* self);

/**
 * @brief Returns the number of frames in the animation.
 *
 * @param self The animation to query; may be NULL.
 * @returns The frame count, or 0 if @p self is NULL or has no frame buffer.
 */
size_t SpriteAnimation_GetFrameCount(SpriteAnimation* self);

/**
 * @brief Copies the frame at the given index out of the animation.
 *
 * @p outFrame is zeroed before the lookup.
 * @param self The animation to query; must not be NULL.
 * @param frameIndex Zero-based index of the frame to fetch.
 * @param outFrame [out] Receives a copy of the frame; must not be NULL. Zeroed on entry.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p outFrame is NULL;
 *          ErrorCode_IndexOutOfBounds if @p frameIndex is not less than the frame count;
 *          ErrorCode_InvalidState if the frame slot is unexpectedly missing (internal inconsistency).
 */
Error SpriteAnimation_GetFrameAt(SpriteAnimation* self, size_t frameIndex, SpriteAnimationFrame* outFrame);

/**
 * @brief Creates a new instance backed by this animation and initialized to its default parameters.
 *
 * Constructs @p outInstance (including its embedded event), links it to @p self as its source, and
 * resets its properties to the animation's defaults. On failure after construction the instance is
 * deconstructed before returning. Release the instance with SpriteAnimationInstance_Deconstruct.
 * @param self The source animation; must not be NULL.
 * @param outInstance [out] The instance to initialize; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p outInstance is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error SpriteAnimation_CreateInstance(SpriteAnimation* self, SpriteAnimationInstance* outInstance);


/**
 * @brief Initializes a standalone animation instance with no backing animation.
 *
 * The instance starts with a NULL source, which is a valid state, and an empty state-reach event ready
 * for subscription. Release with SpriteAnimationInstance_Deconstruct.
 * @param self The instance to initialize; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error SpriteAnimationInstance_Construct1(SpriteAnimationInstance* self);

/**
 * @brief Releases an animation instance and tears down its embedded state-reach event.
 *
 * Does not free the borrowed source animation. After this call the instance is zeroed.
 * @param self The instance to deconstruct; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error SpriteAnimationInstance_Deconstruct(SpriteAnimationInstance* self);

/**
 * @brief Copies the playback state (but not the backing animation) from one instance to another.
 *
 * Copies frame index, timing, FPS, frame step, and running/looping flags. The destination's source
 * animation and its event subscriptions are left unchanged. A self-copy is a successful no-op.
 * @param self The source instance to copy from; must not be NULL.
 * @param destination The instance to copy into; must not be NULL.
 * @returns ErrorCode_Success on success (including self-copy); ErrorCode_IllegalArgument if @p self or
 *          @p destination is NULL.
 */
Error SpriteAnimationInstance_CopyAnimationStateTo(SpriteAnimationInstance* self, SpriteAnimationInstance* destination);

/**
 * @brief Copies the backing animation and the playback state from one instance to another.
 *
 * Like SpriteAnimationInstance_CopyAnimationStateTo but also points the destination at the same source
 * animation. The destination's event subscriptions are left unchanged. A self-copy is a successful no-op.
 * @param self The source instance to copy from; must not be NULL.
 * @param destination The instance to copy into; must not be NULL.
 * @returns ErrorCode_Success on success (including self-copy); ErrorCode_IllegalArgument if @p self or
 *          @p destination is NULL.
 */
Error SpriteAnimationInstance_CopyEntireStateTo(SpriteAnimationInstance* self, SpriteAnimationInstance* destination);

/**
 * @brief Returns the backing animation of an instance.
 *
 * @param self The instance to query; must not be NULL.
 * @returns The source animation (borrowed), or NULL if the instance has no backing animation.
 */
static inline SpriteAnimation* SpriteAnimationInstance_GetSource(SpriteAnimationInstance* self)
{
    return self->_source;
}

/**
 * @brief Reports whether an instance has a backing animation.
 *
 * @param self The instance to query; must not be NULL.
 * @returns true if the instance has a non-NULL source animation, false otherwise.
 */
static inline bool SpriteAnimationInstance_HasSource(SpriteAnimationInstance* self)
{
    return (self->_source != NULL);
}

/**
 * @brief Returns the number of frames available to an instance through its source animation.
 *
 * @param self The instance to query; may be NULL.
 * @returns The source animation's frame count, or 0 if @p self is NULL or has no source.
 */
size_t SpriteAnimationInstance_GetFrameCount(SpriteAnimationInstance* self);

/**
 * @brief Copies the frame at the given index out of the instance's source animation.
 *
 * @param self The instance to query; must not be NULL.
 * @param frameIndex Zero-based index of the frame to fetch.
 * @param outFrame [out] Receives a copy of the frame; must not be NULL. Zeroed on entry.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p outFrame is NULL;
 *          ErrorCode_InvalidOperation if the instance has no source animation;
 *          ErrorCode_IndexOutOfBounds if @p frameIndex is not less than the frame count.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error SpriteAnimationInstance_GetFrameAt(SpriteAnimationInstance* self, size_t frameIndex, SpriteAnimationFrame* outFrame);

/**
 * @brief Copies the instance's current frame out of its source animation.
 *
 * @param self The instance to query; must not be NULL.
 * @param outFrame [out] Receives a copy of the current frame; must not be NULL. Zeroed on entry.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p outFrame is NULL;
 *          ErrorCode_InvalidOperation if the instance has no source animation;
 *          ErrorCode_IndexOutOfBounds if the current index is out of range.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error SpriteAnimationInstance_GetCurrentFrame(SpriteAnimationInstance* self, SpriteAnimationFrame* outFrame);

/**
 * @brief Returns the instance's frames-per-second.
 *
 * @param self The instance to query; must not be NULL.
 * @returns The current FPS value.
 */
static inline double SpriteAnimationInstance_GetFPS(SpriteAnimationInstance* self)
{
    return self->_fps;
}

/**
 * @brief Sets the instance's frames-per-second.
 *
 * @param self The instance to modify; must not be NULL.
 * @param fps The new FPS; must be finite and within [SPRITE_ANIMATION_FPS_MIN, SPRITE_ANIMATION_FPS_MAX].
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p fps is not finite or is outside the allowed range.
 */
Error SpriteAnimationInstance_SetFPS(SpriteAnimationInstance* self, double fps);

/**
 * @brief Returns the instance's current frame index.
 *
 * @param self The instance to query; must not be NULL.
 * @returns The zero-based current frame index.
 */
static inline size_t SpriteAnimationInstance_GetFrameIndex(SpriteAnimationInstance* self)
{
    return self->_frameIndex;
}

/**
 * @brief Sets the instance's current frame index.
 *
 * @param self The instance to modify; must not be NULL.
 * @param frameIndex The new frame index; must be less than the frame count, or 0 when the animation has
 *        no frames.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p frameIndex is non-zero while there are no frames;
 *          ErrorCode_IndexOutOfBounds if @p frameIndex is not less than the frame count.
 */
Error SpriteAnimationInstance_SetFrameIndex(SpriteAnimationInstance* self, size_t frameIndex);

/**
 * @brief Returns the instance's frame step (per-change frame increment).
 *
 * @param self The instance to query; must not be NULL.
 * @returns The current frame step; sign indicates playback direction.
 */
static inline int64_t SpriteAnimationInstance_GetFrameStep(SpriteAnimationInstance* self)
{
    return self->_frameStep;
}

/**
 * @brief Sets the instance's frame step (per-change frame increment).
 *
 * The sign selects playback direction (positive forwards, negative backwards); a value of 0 stops the
 * frame index from moving.
 * @param self The instance to modify; must not be NULL.
 * @param frameStep The new frame step; must be within [SPRITE_ANIMATION_FRAME_STEP_MIN, SPRITE_ANIMATION_FRAME_STEP_MAX].
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p frameStep is outside the allowed range.
 */
Error SpriteAnimationInstance_SetFrameStep(SpriteAnimationInstance* self, int64_t frameStep);

/**
 * @brief Returns the seconds accumulated towards the next frame change.
 *
 * @param self The instance to query; must not be NULL.
 * @returns The accumulated time since the last frame change, in seconds.
 */
static inline double SpriteAnimationInstance_GetSecondsSinceFrameChange(SpriteAnimationInstance* self)
{
    return self->_timeSinceFrameChangeSeconds;
}

/**
 * @brief Sets the seconds accumulated towards the next frame change.
 *
 * @param self The instance to modify; must not be NULL.
 * @param seconds The new accumulated time; must be finite and non-negative.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p seconds is not finite or is negative.
 */
Error SpriteAnimationInstance_SetSecondsSinceFrameChange(SpriteAnimationInstance* self, double seconds);

/**
 * @brief Returns whether the instance is currently running (advancing on update).
 *
 * @param self The instance to query; must not be NULL.
 * @returns true if the instance is running, false otherwise.
 */
static inline bool SpriteAnimationInstance_GetIsRunning(SpriteAnimationInstance* self)
{
    return self->_isRunning;
}

/**
 * @brief Sets whether the instance is running (advancing on update).
 *
 * @param self The instance to modify; must not be NULL.
 * @param value true to run, false to hold the current frame.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error SpriteAnimationInstance_SetIsRunning(SpriteAnimationInstance* self, bool value);

/**
 * @brief Returns whether the instance loops instead of stopping at the ends.
 *
 * @param self The instance to query; must not be NULL.
 * @returns true if the instance loops, false otherwise.
 */
static inline bool SpriteAnimationInstance_GetIsLooped(SpriteAnimationInstance* self)
{
    return self->_isLooped;
}

/**
 * @brief Sets whether the instance loops instead of stopping at the ends.
 *
 * @param self The instance to modify; must not be NULL.
 * @param value true to loop, false to stop at the ends.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error SpriteAnimationInstance_SetIsLooped(SpriteAnimationInstance* self, bool value);

/**
 * @brief Returns the instance's state-reach event for subscription.
 *
 * Subscribe to this event to be notified when the instance reaches an end or loop boundary during an
 * update; handlers receive a SpriteAnimationStateReachEventArgs* payload. The returned pointer is owned
 * by the instance and remains valid until the instance is deconstructed.
 * @param self The instance to query; must not be NULL.
 * @returns A borrowed pointer to the instance's state-reach event.
 */
static inline WREvent* SpriteAnimationInstance_GetStateReachEvent(SpriteAnimationInstance* self)
{
    return &self->_stateReachEvent;
}

/**
 * @brief Resets the animation properties to the backed source's default values.
 *
 * Restores FPS, frame step, running, and looping to the source animation's defaults and then resets the
 * playback position via SpriteAnimationInstance_ResetAnimation.
 * @param self The instance to reset; must not be NULL and must have a source animation.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_InvalidOperation if the instance has no source animation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error SpriteAnimationInstance_ResetProperties(SpriteAnimationInstance* self);

/**
 * @brief Resets the animation state to be the start of the animation.
 *
 * Clears the accumulated frame-change time and positions the frame index at the first frame, or at the
 * last frame when the frame step is negative (so playback starts from the correct end for its direction).
 * @param self The instance to reset; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error SpriteAnimationInstance_ResetAnimation(SpriteAnimationInstance* self);

/**
 * @brief Advances the animation instance by the given amount of elapsed time.
 *
 * Accumulates @p elapsedSeconds and applies as many frame changes as it covers. Does nothing when the
 * instance has no source, has no frames, is not running, has an FPS of 0, or has a frame step of 0. When
 * a non-looping animation reaches an end it stops and raises the state-reach event with
 * SpriteAnimationReachedState_End; when a looping animation wraps it raises the event with
 * SpriteAnimationReachedState_Loop. The event is raised after the new state has been applied.
 * @param self The instance to advance; must not be NULL.
 * @param elapsedSeconds The elapsed time to apply, in seconds; must be finite and non-negative.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p elapsedSeconds is not finite or is negative.
 * @note May propagate errors raised by state-reach event handlers and other internal calls; consult the
 *       documentation of called functions for the full set.
 */
Error SpriteAnimationInstance_Update(SpriteAnimationInstance* self, double elapsedSeconds);
