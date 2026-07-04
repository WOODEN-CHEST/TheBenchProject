#pragma once
#include "raylib/raylib.h"
#include "wr/WRMemory.h"
#include "wr/WRError.h"
#include "wr/WRUserData.h"
#include <stddef.h>
#include <stdint.h>


/**
 * The audio engine of the game.
 *
 * At the core of everything is the audio engine. It contains a master track and plays all the sounds.
 * Only 1 audio engine may exist at a time. Constructing a new audio engine while one already exists will
 * raise an error.
 *
 * An audio track is a single track which contains both subtracks and sounds. There is no limit on the nesting
 * of audio tracks or the number of sounds a track can have.
 *
 * Audio modifiers (effects) can be applies to audio tracks and sounds.
 *
 * The audio engine, sound, track and modifier pointers and all stable and won't change after creation.
 *
 * Some of the audio engine is thread safe, some of it is not:
 * The audio track functions and audio engine functions are thread safe and can be called any time.
 * They queue work to the audio thread when exact sample timing matters.
 * The sound instance functions are not generally thread safe. To modify sound instances, use audio commands:
 *     Schedule a command on the track timeline, then in that command's function modify the sound instance.
 *     Do not set audio instance properties or query timing-sensitive sound state while not on the audio thread.
 * For commands, if a command is scheduled on a track at a time which has already been passed, it just gets executed
 * at the next opportunity as if it just happened.
 * The command setters all copy the command objects when setting them, so the references do not need to be kept alive after
 * the setters.
 * Creating a sound instance creates the object immediately and calls the initializer in the same method call,
 * but attaching it to the track is deferred to the audio thread. The pointer remains stable after creation.
 *
 * For the automated values, setting the duration to be instant instantly sets the current value in the same method call.
 *
 * To execute actions (like change modifiers) on the audio track or just.
 *
 * When sampling, samples are interpolated between the left and right sample at a point. The sample rate
 * in the sample provider determines the distance between the points (so no, the sample rate does not change
 * the playback speed, just sampling quality).
 *
 * Sounds, after being added to the audio track, are automatically removed from it once they end and no
 * modifiers for them are leaving tails anymore.
 * AudioTrack_GetCurrentSecond returns an atomic snapshot of the track timeline at a sample boundary.
 * If queried while a buffer is currently being filled, it may observe either the boundary before or after the
 * sample currently being mixed, but it never reports a partial in-between sample time.
 *
 * Panning additionally changes volume to adjust for perceived loudness.
 */


/** @brief Pan value for full-left placement. */
#define SOUND_SAMPLE_PAN_LEFT -1.0f
/** @brief Pan value for centered placement. */
#define SOUND_SAMPLE_PAN_MIDDLE 0.0f
/** @brief Pan value for full-right placement. */
#define SOUND_SAMPLE_PAN_RIGHT 1.0f

/** @brief Change duration that makes an automated value jump to its target immediately, within the same call. */
#define AUTOMATED_VALUE_DURATION_INSTANT 0.0

/** @brief Maximum number of channels a GameSound may have. */
#define MAX_AUDIO_CHANNELS 2


/**
 * @brief A PCM audio format: sample rate and channel count.
 */
typedef struct AudioFormatStruct
{
    /** @brief Sample rate in hertz (frames per second); must be positive and finite. */
    float SampleRate;
    /** @brief Number of interleaved channels; for a GameSound, within [1, MAX_AUDIO_CHANNELS]. */
    int32_t ChannelCount;
} AudioFormat;

/**
 * @brief The timing part of an automated value: how much of the current transition remains.
 */
typedef struct SoundAutomatedValueTimeStruct
{
    /** @brief Seconds left before the value reaches its target; 0 means the transition is complete. */
    double _changeDurationLeftSeconds;
} SoundAutomatedValueTime;

/**
 * @brief A float that smoothly transitions from its current value towards a target over a set duration.
 *
 * Advanced by the audio thread each frame and retargeted with AutomatedFloat_SetValue (a zero duration
 * applies the new value instantly). Read the live value from @c _currentValue.
 */
typedef struct SoundAutomatedFloatStruct
{
    /** @brief The value in effect right now; interpolated towards @c _targetValue as time advances. */
    float _currentValue;
    /** @brief The value being transitioned towards. */
    float _targetValue;
    /** @brief Remaining transition time. */
    SoundAutomatedValueTime _time;
} SoundAutomatedFloat;

/**
 * @brief A double that smoothly transitions from its current value towards a target over a set duration.
 *
 * Advanced by the audio thread each frame and retargeted with AutomatedDouble_SetValue (a zero duration
 * applies the new value instantly). Read the live value from @c _currentValue.
 */
typedef struct SoundAutomatedDoubleStruct
{
    /** @brief The value in effect right now; interpolated towards @c _targetValue as time advances. */
    double _currentValue;
    /** @brief The value being transitioned towards. */
    double _targetValue;
    /** @brief Remaining transition time. */
    SoundAutomatedValueTime _time;
} SoundAutomatedDouble;

/** @brief Opaque forward declaration of a sound instance; the full layout is defined below. */
typedef struct GameSoundInstanceStruct GameSoundInstance;

/**
 * @brief The working context passed to a sound modifier for one block of samples.
 *
 * Describes the interleaved float sample buffer to process, which portion of it to touch, the audio
 * timing, and the destination format. A modifier reads and writes @c SampleBuffer in place over the
 * range [@c _startIndex, @c _startIndex + @c _modifyCount). Counts are in individual samples (channels
 * interleaved), so the number of frames is @c _modifyCount divided by the channel count.
 */
typedef struct SoundModifySectionContextStruct
{
    /** @brief The sound instance being modified, or NULL when the modifier is applied to a whole track. */
    GameSoundInstance* _sound;
    double _sampleTimeSeconds; // Current audio track second at which this modification is taking place.
    double _singleFrameDurationSeconds; // Duration of a single audio frame, in seconds.
    /** @brief The interleaved float sample buffer to read and modify in place. */
    float* SampleBuffer;
    size_t _sampleCount; // Number of samples in the sample buffer.
    size_t _modifyCount; // Number of samples to modify in this operation.
    size_t _startIndex; // Start index in the sample buffer from which to modify.
    AudioFormat _targetFormat; // The format into which the audio is being modifier to (destination format).
} SoundModifySectionContext;

/**
 * @brief An immutable source sound: its interleaved samples and their format.
 *
 * Does not own the sample memory; it only borrows the buffer handed to GameSound_Construct1, which must
 * outlive the sound. Sound instances read their audio from a GameSound.
 */
typedef struct GameSoundStruct
{
    /** @brief Borrowed interleaved float samples; not owned and must outlive the sound. */
    float* _samples;
    /** @brief Total number of individual samples in @c _samples (frames * channel count). */
    size_t _sampleCount;
    /** @brief The format of @c _samples. */
    AudioFormat _format;
} GameSound;

/** @brief Opaque forward declaration of an audio track; the concrete type lives in the implementation. */
typedef struct AudioTrackStruct AudioTrack;

/** @brief Opaque forward declaration of the audio engine; the concrete type lives in the implementation. */
typedef struct AudioEngineStruct AudioEngine;

/**
 * @brief The dispatch table for a sound modifier implementation.
 *
 * Holds the concrete modifier pointer and its per-block processing hooks. Callers do not use this
 * directly; they go through the ISoundModifier wrappers (ISoundModifier_Modify / ISoundModifier_ResetState).
 */
typedef struct ISoundModifierVTableStruct
{
    /** @brief Pointer to the concrete modifier instance passed back to the hooks below. */
    void* Self;

    /** @brief Processes one block of samples in place and returns whether the modifier still has an active tail. */
    bool (*_modify)(void* self, SoundModifySectionContext* context);
    /** @brief Resets any internal per-run state (filter history, delay lines, etc.). */
    void (*_resetState)(void* self); // Resets modifier related state.

} ISoundModifierVTable;

/**
 * @brief The interface handle for a sound modifier, embedding its vtable by value.
 *
 * Obtained from a concrete modifier via its GetModifier accessor and handed to
 * SampleProvider_AddModifier. The concrete modifier must outlive its use on a sample provider.
 */
typedef struct ISoundModifierStruct
{
    /** @brief The modifier's dispatch table. */
    ISoundModifierVTable _vtable;
} ISoundModifier;

/**
 * @brief The mutable playback properties shared by tracks and sound instances.
 *
 * Bundles the automated sample rate, volume, and pan together with the effect chain and a back-reference
 * to the owning engine and object. Retrieved via AudioTrack_GetProperties or
 * GameSoundInstance_GetSampleProperties; individual automated values are changed through their
 * AutomatedFloat_SetValue / AutomatedDouble_SetValue setters.
 */
typedef struct SampleProviderStruct
{
    /** @brief Resampling rate used when reading source samples; higher only improves sampling quality, not speed. */
    SoundAutomatedDouble _sampleRate;
    /** @brief Output volume multiplier. */
    SoundAutomatedFloat _volume;
    SoundAutomatedFloat _pan; // [-1 left, 0 middle, 1 right]
    /** @brief Borrowed buffer of ISoundModifier* forming the effect chain; not owned. */
    GenericBuffer* _modifiers;
    /** @brief The engine this provider belongs to; borrowed. */
    AudioEngine* _ownerEngine;
    /** @brief The owning track or sound slot; borrowed, interpreted per @c _ownerIsTrack. Internal. */
    void* _owner;
    /** @brief true if @c _owner is a track, false if it is a sound slot. */
    bool _ownerIsTrack;
} SampleProvider;

/**
 * @brief A function executed on the audio thread against a track, with caller-attached user data.
 *
 * Invoked when a scheduled command fires or when a sound loop/end boundary is reached.
 * @param track The track the command runs against; borrowed, valid for the call.
 * @param userData Pointer to the command's stored user data (may carry a pointer or inline value); read-only.
 * @returns ErrorCode_Success on success, or an error the mixer will surface.
 */
typedef Error (*AudioCommandFunction)(AudioTrack* track, const UserData* userData);

/**
 * @brief A schedulable/triggerable audio action: a function plus the user data handed to it.
 *
 * Copied by value wherever it is set or scheduled, so it need not outlive the call. The user data is
 * stored inline (by value) and delivered to @c _function as a pointer to that stored copy.
 */
typedef struct AudioCommandStruct
{
    /** @brief User data copied into the command and passed to @c _function when it runs. */
    UserData _userData;
    /** @brief The action to run; NULL means "no command" and is treated as a no-op. */
    AudioCommandFunction _function;
} AudioCommand;

/**
 * @brief The playback state of a sound instance.
 */
typedef enum SoundInstanceStateEnum
{
    /** @brief The instance is advancing and producing samples. */
    SoundInstanceState_Playing,
    /** @brief The instance is held in place and produces silence until resumed. */
    SoundInstanceState_Paused,
    /** @brief The instance has finished; it is removed once no modifier tails remain. */
    SoundInstanceState_Ended,
} SoundInstanceState;

/**
 * @brief A playing occurrence of a GameSound with its own position, speed, effects, and callbacks.
 *
 * Created on a track by AudioTrack_CreateSoundInstance; the pointer stays stable for the instance's
 * lifetime. Its timing-sensitive fields are meant to be touched on the audio thread (typically from an
 * audio command); see the module-level threading notes.
 */
struct GameSoundInstanceStruct
{
    /** @brief The source sound this instance plays; borrowed. */
    GameSound* _source;
    /** @brief Current playback state. */
    SoundInstanceState _state;
    /** @brief Current playback position in source sample frames; fractional positions are interpolated. */
    double _sampleIndex;
    /** @brief Playback speed multiplier (1.0 is normal speed). */
    SoundAutomatedDouble _sampleSpeed;
    /** @brief Volume, pan, resampling rate, and the per-instance effect chain. */
    SampleProvider _sampleProperties;
    /** @brief Whether the instance wraps around instead of ending. */
    bool _isLooped;
    AudioCommand _loopCommand; // Ran when the audio loops (forwards or backwards).
    AudioCommand _endCommand ; // Ran when the audio ends (tail from modifiers not accounted for).
    AudioCommand _tailEndCommand; // Ran when the sound and all modifier tail have ended.
};

/**
 * @brief A modifier that adds a damped feedback-delay reverb to the signal.
 *
 * Add it to a SampleProvider via ReverbSoundModifier_GetModifier. The public automated fields are the
 * user-facing controls (set them through AutomatedFloat_SetValue / AutomatedDouble_SetValue); the
 * underscore-prefixed fields are internal delay-line state.
 */
typedef struct ReverbSoundModifierStruct
{
    /** @brief The interface handle used to attach this modifier to a sample provider. */
    ISoundModifier _modifier;
    /** @brief Gain applied to the dry (unprocessed) signal. */
    SoundAutomatedFloat DryVolume;
    /** @brief Gain applied to the wet (reverberated) signal. */
    SoundAutomatedFloat WetVolume;
    /** @brief Feedback amount fed back into the delay line, controlling reverb length. */
    SoundAutomatedFloat Feedback;
    /** @brief Delay time of the feedback line, in seconds. */
    SoundAutomatedDouble DelaySeconds;
    /** @brief Low-pass damping applied within the feedback loop, in [0;1]. */
    SoundAutomatedFloat Damping;
    /** @brief Internal delay ring buffer; owned by the modifier and freed on deconstruct. */
    float* _delayBuffer;
    /** @brief Allocated capacity of @c _delayBuffer, in frames. */
    size_t _delayBufferFrameCount;
    /** @brief Delay length currently in use, in frames. */
    size_t _activeDelayBufferFrameCount;
    /** @brief Current write cursor into the delay ring buffer, in frames. */
    size_t _delayBufferWriteFrameIndex;
    /** @brief Left-channel low-pass filter state for damping. */
    float _previousLowPassLeft;
    /** @brief Right-channel low-pass filter state for damping. */
    float _previousLowPassRight;
} ReverbSoundModifier;

/**
 * @brief Selects whether a biquad modifier acts as a low-pass or high-pass filter.
 */
typedef enum BiQuadPassTypeEnum
{
    /** @brief Attenuate frequencies above the cutoff. */
    BiQuadPassType_Low,
    /** @brief Attenuate frequencies below the cutoff. */
    BiQuadPassType_High,
} BiQuadPassTypeEnum;

/**
 * @brief A second-order (biquad) low-pass or high-pass filter modifier.
 *
 * Add it to a SampleProvider via BiQuadPassSoundModifier_GetModifier. The public automated fields are
 * the user-facing controls; the underscore-prefixed fields are internal per-channel filter history.
 */
typedef struct BiQuadPassSoundModifierStruct
{
    /** @brief The interface handle used to attach this modifier to a sample provider. */
    ISoundModifier _modifier;
    /** @brief Whether this filter is low-pass or high-pass. */
    BiQuadPassTypeEnum PassType;
    /** @brief Mix of the filtered signal against the dry signal, in [0;1]. */
    SoundAutomatedFloat WetVolume;
    /** @brief Cutoff frequency, in hertz. */
    SoundAutomatedFloat CutoffFrequency;
    /** @brief Filter resonance (Q); higher values emphasize the cutoff. */
    SoundAutomatedFloat Resonance;
    /** @brief Left-channel input history sample x[n-1]. */
    float _x1Left;
    /** @brief Left-channel input history sample x[n-2]. */
    float _x2Left;
    /** @brief Left-channel output history sample y[n-1]. */
    float _y1Left;
    /** @brief Left-channel output history sample y[n-2]. */
    float _y2Left;
    /** @brief Right-channel input history sample x[n-1]. */
    float _x1Right;
    /** @brief Right-channel input history sample x[n-2]. */
    float _x2Right;
    /** @brief Right-channel output history sample y[n-1]. */
    float _y1Right;
    /** @brief Right-channel output history sample y[n-2]. */
    float _y2Right;
} BiQuadPassSoundModifier;

/**
 * @brief A modifier that reduces bit depth and sample rate for a lo-fi "crushed" sound.
 *
 * Add it to a SampleProvider via BitCrusherModifier_GetModifier. The public automated fields are the
 * user-facing controls; the underscore-prefixed fields are internal sample-and-hold state.
 */
typedef struct BitCrusherModifierStruct
{
    /** @brief The interface handle used to attach this modifier to a sample provider. */
    ISoundModifier _modifier;
    /** @brief Mix of the crushed signal against the dry signal, in [0;1]. */
    SoundAutomatedFloat WetVolume;
    /** @brief Sample-and-hold interval (sample-rate reduction), in seconds. */
    SoundAutomatedDouble HoldSeconds;
    /** @brief Effective bit depth to quantize to. */
    SoundAutomatedFloat BitDepth;
    /** @brief Seconds remaining before the next sample is captured. */
    double _holdSecondsLeft;
    /** @brief Currently held left-channel sample. */
    float _heldLeft;
    /** @brief Currently held right-channel sample. */
    float _heldRight;
} BitCrusherModifier;

/**
 * @brief A function that initializes a freshly created sound instance before it starts playing.
 *
 * Called synchronously inside AudioTrack_CreateSoundInstance, so it may configure the instance directly.
 * @param soundInstance The newly created instance to initialize; valid for the call.
 * @param userData The user data forwarded from AudioTrack_CreateSoundInstance; may be NULL. Read-only.
 * @returns ErrorCode_Success on success, or an error that aborts creation.
 */
typedef Error (*SoundInstanceInitializer)(GameSoundInstance* soundInstance, const UserData* userData);


// Functions.

// Audio Engine.
/**
 * @brief Creates the process-wide audio engine and starts its output stream.
 *
 * Allocates the engine, sets up the master track, opens the audio device if one is not already open, and
 * begins streaming. Only one engine may exist at a time. The returned engine is owned by the caller and
 * must be released with AudioEngine_Deconstruct.
 * @param outEngine [out] Receives the created engine; must not be NULL. Set to NULL on failure.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p outEngine is NULL;
 *          ErrorCode_InvalidOperation if an audio engine already exists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AudioEngine_Construct1(AudioEngine** outEngine);

/**
 * @brief Stops and destroys the audio engine, freeing all its tracks, sounds, and instances.
 *
 * Halts and unloads the stream, closes the audio device if the engine opened it, frees every registered
 * sound instance and slot, tears down the track tree, and frees the engine itself.
 * @param self The engine to destroy; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error AudioEngine_Deconstruct(AudioEngine* self);

/**
 * @brief Returns the engine's output audio format.
 * @param self The engine to query; must not be NULL.
 * @returns The engine's AudioFormat (sample rate and channel count).
 */
AudioFormat AudioEngine_GetAudioFormat(AudioEngine* self);

/**
 * @brief Returns the engine's master track.
 *
 * All other tracks descend from this one. The pointer is borrowed and stays valid for the engine's life.
 * @param self The engine to query; must not be NULL.
 * @returns The master track (borrowed).
 */
AudioTrack* AudioEngine_GetMasterTrack(AudioEngine* self);

/**
 * @brief The number of seconds the latest callback to the audio buffer fill operation took.
 *
 * A performance metric read as an atomic snapshot of the most recent mixing callback.
 * @param self The engine to query; must not be NULL.
 * @returns The duration of the last buffer-fill callback, in seconds.
 */
double AudioEngine_GetBufferFillDurationSeconds(AudioEngine* self);

/**
 * @brief Returns the duration of a single audio frame at the engine's sample rate.
 * @param self The engine to query; must not be NULL.
 * @returns The reciprocal of the engine's sample rate, in seconds per frame.
 */
static inline double AudioEngine_GetSecondsPerFrame(AudioEngine* self)
{
    return 1.0 / (double)AudioEngine_GetAudioFormat(self).SampleRate;
}


// Audio Track.
/**
 * @brief Returns the track's sample provider (its volume, pan, sample rate, and effect chain).
 *
 * The returned pointer is borrowed and stays valid for the track's lifetime.
 * @param self The track to query; must not be NULL.
 * @returns The track's SampleProvider (borrowed).
 */
SampleProvider* AudioTrack_GetProperties(AudioTrack* self);

/**
 * @brief Creates a child track under this track and schedules its attachment.
 *
 * Allocates the subtrack and registers it; the actual attachment to the mixing tree happens immediately
 * when called on the audio thread, otherwise it is queued to the audio thread. The returned pointer is
 * stable and owned by the engine (released when the parent or engine is destroyed). Thread-safe.
 * @param self The parent track; must not be NULL.
 * @param outSubTrack [out] Receives the created subtrack; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p outSubTrack is NULL;
 *          ErrorCode_BufferTooLarge if track storage or the audio-thread queue could not accommodate it.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AudioTrack_CreateSubTrack(AudioTrack* self, AudioTrack** outSubTrack);

/**
 * @brief Removes a direct child track from this track and schedules its detachment.
 *
 * Detaches immediately when called on the audio thread, otherwise queues the detachment. Thread-safe.
 * @param self The parent track; must not be NULL.
 * @param subTrackToRemove The child track to remove; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p subTrackToRemove is
 *          NULL; ErrorCode_InvalidOperation if @p subTrackToRemove is not a child of @p self.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AudioTrack_RemoveSubTrack(AudioTrack* self, AudioTrack* subTrackToRemove);

/**
 * @brief Appends this track's direct child tracks to the given buffer.
 *
 * Pointers to the child tracks are appended to @p outTrackPointers; the buffer is not cleared first, so
 * clear it beforehand if you want only these entries.
 * @param self The track to query; must not be NULL.
 * @param outTrackPointers [out] Buffer receiving AudioTrack* entries; must not be NULL and its element
 *        size must equal sizeof(AudioTrack*).
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p outTrackPointers is
 *          NULL, or the buffer's element size is wrong; ErrorCode_BufferTooLarge if appending failed.
 */
Error AudioTrack_GetSubTracks(AudioTrack* self, GenericBuffer* outTrackPointers);

/**
 * @brief Returns the number of sound instances publicly registered on this track.
 * @param self The track to query; must not be NULL.
 * @returns The current sound instance count (atomic snapshot).
 */
size_t AudioTrack_GetSoundInstanceCount(AudioTrack* self);

/**
 * @brief The current time, in seconds, of this audio track as an atomic sample-boundary snapshot.
 * @param self The track to query; must not be NULL.
 * @returns The track's current timeline position, in seconds.
 */
double AudioTrack_GetCurrentSecond(AudioTrack* self);

/**
 * @brief Creates a new sound instance and instantly (in this method call) calls the initializer on it.
 *
 * The instance is created and initialized synchronously (its pointer is stable from here on), then its
 * attachment to the track is deferred to the audio thread. The initializerUserData pointer is forwarded
 * to the initializer and only needs to stay valid for the duration of this call (may be NULL).
 * @param self The track to add the sound to; must not be NULL.
 * @param sourceSound The source sound to play; must not be NULL and must outlive the instance.
 * @param initializer Optional initializer run on the new instance; may be NULL.
 * @param initializerUserData User data forwarded to @p initializer; may be NULL, needed only for the call.
 * @param outSoundInstance [out] Receives the created instance; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self, @p sourceSound, or
 *          @p outSoundInstance is NULL; ErrorCode_BufferTooLarge if registry or queue storage failed.
 * @note May propagate the error returned by @p initializer and errors from internal calls; consult the
 *       documentation of called functions for the full set.
 */
Error AudioTrack_CreateSoundInstance(AudioTrack* self,
    GameSound* sourceSound,
    SoundInstanceInitializer initializer,
    const UserData* initializerUserData,
    GameSoundInstance** outSoundInstance);

/**
 * @brief Removes a sound from the track. Does not error if the sound isn't present, instead a bool is returned indicating that.
 *
 * Marks the instance for removal; the actual detachment happens on the audio thread (immediately if
 * already on it, otherwise queued).
 * @param self The track to remove the sound from; must not be NULL.
 * @param soundInstance The instance to remove; must not be NULL.
 * @param wasRemoved [out] Set to true if the instance belonged to this track and was scheduled for
 *        removal, false otherwise; must not be NULL.
 * @returns ErrorCode_Success on success (including the not-present case); ErrorCode_IllegalArgument if
 *          @p self, @p soundInstance, or @p wasRemoved is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AudioTrack_RemoveSoundInstance(AudioTrack* self, GameSoundInstance* soundInstance, bool* wasRemoved);

/**
 * @brief Schedules a command to run on this track's timeline.
 *
 * The command is copied by value, so it need not outlive the call. The second in track timeline is the
 * second at which this command is to be executed; a time already passed runs at the next opportunity.
 * @param self The track to schedule on; must not be NULL.
 * @param secondInTrackTimeline The timeline second at which to run the command; must be finite.
 * @param command The command to schedule; must not be NULL (copied by value).
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p command is NULL, or
 *          @p secondInTrackTimeline is not finite.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error AudioTrack_ScheduleCommand(AudioTrack* self, double secondInTrackTimeline, AudioCommand* command);



// Sound modifiers.
/**
 * @brief Processes one block of samples through a modifier and reports whether it still has a tail.
 * @param self The modifier interface handle; must not be NULL.
 * @param context The block-processing context; must not be NULL.
 * @returns true if the modifier still has an active tail (e.g. reverb ringing out), false otherwise.
 */
static inline bool ISoundModifier_Modify(ISoundModifier* self, SoundModifySectionContext* context)
{
    return self->_vtable._modify(self->_vtable.Self, context);
}

/**
 * @brief Resets a modifier's internal per-run state (filter history, delay lines, held samples).
 * @param self The modifier interface handle; must not be NULL.
 */
static inline void ISoundModifier_ResetState(ISoundModifier* self)
{
    self->_vtable._resetState(self->_vtable.Self);
}

/**
 * @brief Initializes a reverb modifier with default parameters.
 * @param self The modifier to initialize; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error ReverbSoundModifier_Construct1(ReverbSoundModifier* self);

/**
 * @brief Releases a reverb modifier, freeing its internal delay buffer.
 * @param self The modifier to deconstruct; NULL is tolerated as a no-op.
 */
void ReverbSoundModifier_Deconstruct(ReverbSoundModifier* self);

/**
 * @brief Clears the reverb modifier's delay line and filter state without freeing its buffer.
 * @param self The modifier to reset; must not be NULL.
 */
void ReverbSoundModifier_ResetState(ReverbSoundModifier* self);

/**
 * @brief Returns the interface handle used to attach this reverb modifier to a sample provider.
 * @param self The modifier; must not be NULL.
 * @returns A borrowed pointer to the modifier's ISoundModifier handle.
 */
static inline ISoundModifier* ReverbSoundModifier_GetModifier(ReverbSoundModifier* self)
{
    return &self->_modifier;
}

/**
 * @brief Initializes a biquad pass filter modifier of the given type with default parameters.
 * @param self The modifier to initialize; must not be NULL.
 * @param passType Whether the filter is low-pass or high-pass.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error BiQuadPassSoundModifier_Construct1(BiQuadPassSoundModifier* self, BiQuadPassTypeEnum passType);

/**
 * @brief Releases a biquad pass filter modifier.
 * @param self The modifier to deconstruct; NULL is tolerated as a no-op.
 */
void BiQuadPassSoundModifier_Deconstruct(BiQuadPassSoundModifier* self);

/**
 * @brief Clears the biquad filter's per-channel history state.
 * @param self The modifier to reset; must not be NULL.
 */
void BiQuadPassSoundModifier_ResetState(BiQuadPassSoundModifier* self);

/**
 * @brief Returns the interface handle used to attach this biquad modifier to a sample provider.
 * @param self The modifier; must not be NULL.
 * @returns A borrowed pointer to the modifier's ISoundModifier handle.
 */
static inline ISoundModifier* BiQuadPassSoundModifier_GetModifier(BiQuadPassSoundModifier* self)
{
    return &self->_modifier;
}

/**
 * @brief Initializes a bit crusher modifier with default parameters.
 * @param self The modifier to initialize; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error BitCrusherModifier_Construct1(BitCrusherModifier* self);

/**
 * @brief Releases a bit crusher modifier.
 * @param self The modifier to deconstruct; NULL is tolerated as a no-op.
 */
void BitCrusherModifier_Deconstruct(BitCrusherModifier* self);

/**
 * @brief Clears the bit crusher's sample-and-hold state.
 * @param self The modifier to reset; must not be NULL.
 */
void BitCrusherModifier_ResetState(BitCrusherModifier* self);

/**
 * @brief Returns the interface handle used to attach this bit crusher modifier to a sample provider.
 * @param self The modifier; must not be NULL.
 * @returns A borrowed pointer to the modifier's ISoundModifier handle.
 */
static inline ISoundModifier* BitCrusherModifier_GetModifier(BitCrusherModifier* self)
{
    return &self->_modifier;
}


// Automated values.

/* Automated values prohibit infinity and NaN. */

/**
 * @brief Retargets an automated float, transitioning to it over the given duration.
 *
 * A duration of AUTOMATED_VALUE_DURATION_INSTANT applies the new value immediately within this call.
 * @param value The automated float to update; must not be NULL.
 * @param newTarget The value to transition towards; must be finite.
 * @param changeDurationSeconds Transition length in seconds; must be finite and non-negative.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p value is NULL, @p newTarget is
 *          not finite, or @p changeDurationSeconds is not finite or is negative.
 */
Error AutomatedFloat_SetValue(SoundAutomatedFloat* value, float newTarget, double changeDurationSeconds);

/**
 * @brief Retargets an automated double, transitioning to it over the given duration.
 *
 * A duration of AUTOMATED_VALUE_DURATION_INSTANT applies the new value immediately within this call.
 * @param value The automated double to update; must not be NULL.
 * @param newTarget The value to transition towards; must be finite.
 * @param changeDurationSeconds Transition length in seconds; must be finite and non-negative.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p value is NULL, @p newTarget is
 *          not finite, or @p changeDurationSeconds is not finite or is negative.
 */
Error AutomatedDouble_SetValue(SoundAutomatedDouble* value, double newTarget, double changeDurationSeconds);


// Sample provider.
/**
 * @brief Returns the provider's automated sample-rate value for reading or retargeting.
 * @param self The provider; must not be NULL.
 * @returns A borrowed pointer to the automated sample rate.
 */
static inline SoundAutomatedDouble* SampleProvider_GetSampleRate(SampleProvider* self)
{
    return &self->_sampleRate;
}

/**
 * @brief Returns the provider's automated pan value for reading or retargeting.
 *
 * Pan ranges from SOUND_SAMPLE_PAN_LEFT to SOUND_SAMPLE_PAN_RIGHT.
 * @param self The provider; must not be NULL.
 * @returns A borrowed pointer to the automated pan.
 */
static inline SoundAutomatedFloat* SampleProvider_GetPan(SampleProvider* self)
{
    return &self->_pan;
}

/**
 * @brief Returns the provider's automated volume value for reading or retargeting.
 * @param self The provider; must not be NULL.
 * @returns A borrowed pointer to the automated volume.
 */
static inline SoundAutomatedFloat* SampleProvider_GetVolume(SampleProvider* self)
{
    return &self->_volume;
}

/**
 * @brief Inserts a modifier into the provider's effect chain at the given index.
 *
 * Modifier must be kept alive through the entirety of the sample provider's lifetime. The change is
 * applied on the audio thread (immediately if already on it, otherwise queued). Thread-safe.
 * @param self The provider to modify; must not be NULL.
 * @param modifier The modifier to add; must not be NULL and must outlive the provider.
 * @param index The position at which to insert; must not exceed the current modifier count.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p modifier is NULL or
 *          the provider does not belong to the active engine; ErrorCode_InvalidOperation if no audio
 *          engine is active; ErrorCode_IndexOutOfBounds if @p index is out of range;
 *          ErrorCode_BufferTooLarge if storage or the audio-thread queue could not accommodate it.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error SampleProvider_AddModifier(SampleProvider* self, ISoundModifier* modifier, size_t index);

/**
 * @brief Removes a modifier from the provider's effect chain.
 *
 * The change is applied on the audio thread (immediately if already on it, otherwise queued). Thread-safe.
 * @param self The provider to modify; must not be NULL.
 * @param modifier The modifier to remove; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p modifier is NULL or
 *          the provider does not belong to the active engine; ErrorCode_InvalidOperation if no audio
 *          engine is active or the modifier is not present.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error SampleProvider_RemoveModifier(SampleProvider* self, ISoundModifier* modifier);

/**
 * @brief Removes all modifiers from the provider's effect chain.
 *
 * The change is applied on the audio thread (immediately if already on it, otherwise queued). Thread-safe.
 * @param self The provider to clear; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL or the provider
 *          does not belong to the active engine; ErrorCode_InvalidOperation if no audio engine is active.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error SampleProvider_ClearModifiers(SampleProvider* self);


// Sounds.
/**
 * @brief Initializes a GameSound over a borrowed sample buffer. Sound does not own the audio samples, only borrows them.
 *
 * The samples must remain valid for the sound's lifetime and are not freed by GameSound_Deconstruct.
 * @param self The sound to initialize; must not be NULL.
 * @param samples Borrowed interleaved float samples; must not be NULL and must outlive the sound.
 * @param sampleCount Total number of samples; must be non-zero and divisible by the channel count.
 * @param format The sample format; channel count must be within [1, MAX_AUDIO_CHANNELS] and the sample
 *        rate must be positive and finite.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p samples is NULL, the
 *          channel count or sample rate is invalid, or @p sampleCount is zero or not divisible by the
 *          channel count.
 */
Error GameSound_Construct1(GameSound* self, float* samples, size_t sampleCount, AudioFormat format);

/**
 * @brief Releases a GameSound without freeing its borrowed samples.
 * @param self The sound to deconstruct; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error GameSound_Deconstruct(GameSound* self);


// Sound instances.
/**
 * @brief Sets the instance's playback position, in source sample frames.
 * @param self The instance to modify; must not be NULL.
 * @param sampleIndex The new position in source frames; must be finite and non-negative.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL or @p sampleIndex
 *          is not finite or is negative.
 */
Error GameSoundInstance_SetSampleIndex(GameSoundInstance* self, double sampleIndex);

/**
 * @brief Returns the instance's playback position, in source sample frames.
 * @param self The instance to query; must not be NULL.
 * @returns The current position in source frames.
 */
static inline double GameSoundInstance_GetSampleIndex(GameSoundInstance* self)
{
    return self->_sampleIndex;
}

/**
 * @brief Sets the instance's playback position from a time in seconds.
 * @param self The instance to modify; must not be NULL.
 * @param second The new position in seconds; must be finite and non-negative.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL or @p second is
 *          not finite or is negative.
 */
Error GameSoundInstance_SetSampleSecond(GameSoundInstance* self, double second);

/**
 * @brief Returns the instance's playback position converted to seconds.
 * @param self The instance to query; must not be NULL and must have a source sound.
 * @returns The current position in seconds (frame position divided by the source sample rate).
 */
static inline double GameSoundInstance_GetSampleSecond(GameSoundInstance* self)
{
    return self->_sampleIndex / (double)self->_source->_format.SampleRate;
}

/**
 * @brief Returns whether the instance loops instead of ending.
 * @param self The instance to query; must not be NULL.
 * @returns true if the instance loops, false otherwise.
 */
static inline bool GameSoundInstance_GetIsLooped(GameSoundInstance* self)
{
    return self->_isLooped;
}

/**
 * @brief Sets whether the instance loops instead of ending.
 * @param self The instance to modify; must not be NULL.
 * @param value true to loop, false to end at the boundaries.
 * @returns ErrorCode_Success.
 */
static inline Error GameSoundInstance_SetIsLooped(GameSoundInstance* self, bool value)
{
    self->_isLooped = value;
    return Error_CreateSuccess();
}

/**
 * @brief Returns the instance's automated playback-speed value for reading or retargeting.
 * @param self The instance to query; must not be NULL.
 * @returns A borrowed pointer to the automated sample speed.
 */
static inline SoundAutomatedDouble* GameSoundInstance_GetSampleSpeed(GameSoundInstance* self)
{
    return &self->_sampleSpeed;
}

/**
 * @brief Returns the instance's sample provider (volume, pan, sample rate, and effect chain).
 * @param self The instance to query; must not be NULL.
 * @returns A borrowed pointer to the instance's SampleProvider.
 */
static inline SampleProvider* GameSoundInstance_GetSampleProperties(GameSoundInstance* self)
{
    return &self->_sampleProperties;
}

/**
 * @brief Sets the command run when the sound ends (before modifier tails finish).
 *
 * The command is copied by value, so it need not outlive the call.
 * @param self The instance to modify; must not be NULL.
 * @param command The command to copy in; must not be NULL.
 * @returns ErrorCode_Success.
 */
static inline Error GameSoundInstance_SetEndCommand(GameSoundInstance* self, AudioCommand* command)
{
    self->_endCommand = *command;
    return Error_CreateSuccess();
}

/**
 * @brief Sets the command run once the sound and all of its modifier tails have finished.
 *
 * The command is copied by value, so it need not outlive the call.
 * @param self The instance to modify; must not be NULL.
 * @param command The command to copy in; must not be NULL.
 * @returns ErrorCode_Success.
 */
static inline Error GameSoundInstance_SetTailEndCommand(GameSoundInstance* self, AudioCommand* command)
{
    self->_tailEndCommand = *command;
    return Error_CreateSuccess();
}

/**
 * @brief Sets the command run when the sound loops (in either direction).
 *
 * The command is copied by value, so it need not outlive the call.
 * @param self The instance to modify; must not be NULL.
 * @param command The command to copy in; must not be NULL.
 * @returns ErrorCode_Success.
 */
static inline Error GameSoundInstance_SetLoopCommand(GameSoundInstance* self, AudioCommand* command)
{
    self->_loopCommand = *command;
    return Error_CreateSuccess();
}

/**
 * @brief Returns the instance's current playback state.
 * @param self The instance to query; must not be NULL.
 * @returns The current SoundInstanceState.
 */
static inline SoundInstanceState GameSoundInstance_GetState(GameSoundInstance* self)
{
    return self->_state;
}

/**
 * @brief Sets the instance's playback state.
 * @param self The instance to modify; must not be NULL.
 * @param state The new state.
 * @returns ErrorCode_Success.
 */
static inline Error GameSoundInstance_SetState(GameSoundInstance* self, SoundInstanceState state)
{
    self->_state = state;
    return Error_CreateSuccess();
}
