#include "SoundEngine.h"
#include "wr/WRCompile.h"
#include <math.h>
#include <stdatomic.h>
#include <time.h>


// Macros.
#define AUDIO_ENGINE_OUTPUT_SAMPLE_RATE 48000U
#define AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT 2U
#define AUDIO_ENGINE_OUTPUT_SAMPLE_SIZE_BITS 32U
#define AUDIO_ENGINE_QUEUE_CAPACITY 2048U
#define AUDIO_ENGINE_INITIAL_SUBTRACK_CAPACITY 4U
#define AUDIO_ENGINE_INITIAL_SOUND_CAPACITY 8U
#define AUDIO_ENGINE_INITIAL_COMMAND_CAPACITY 8U
#define AUDIO_ENGINE_INITIAL_MODIFIER_CAPACITY 4U
#define AUDIO_ENGINE_INITIAL_REGISTRY_CAPACITY 32U
#define AUDIO_ENGINE_SIGNAL_EPSILON 0.0001f


// Types.
typedef enum AudioThreadOperationTypeEnum
{
    AudioThreadOperationType_AttachSubTrack,
    AudioThreadOperationType_DetachSubTrack,
    AudioThreadOperationType_AttachSound,
    AudioThreadOperationType_RemoveSound,
    AudioThreadOperationType_ScheduleCommand,
    AudioThreadOperationType_AddTrackModifier,
    AudioThreadOperationType_RemoveTrackModifier,
    AudioThreadOperationType_ClearTrackModifiers,
    AudioThreadOperationType_AddSoundModifier,
    AudioThreadOperationType_RemoveSoundModifier,
    AudioThreadOperationType_ClearSoundModifiers,
} AudioThreadOperationType;

typedef struct ScheduledAudioCommandStruct
{
    uint64_t TargetFrameIndex;
    AudioCommand Command;
} ScheduledAudioCommand;

typedef struct TrackSoundSlotStruct
{
    GameSoundInstance* Instance;
    struct AudioTrackStruct* OwningTrack;
    GenericBuffer _publicModifierBuffer;
    GenericBuffer _activeModifierBuffer;
    atomic_bool _isAttached;
    atomic_bool _isPendingAttach;
    atomic_bool _isPendingRemove;
    bool _hasModifierTail;
    bool _endCommandRan;
    bool _tailEndCommandRan;
} TrackSoundSlot;

typedef struct AudioThreadOperationStruct
{
    AudioThreadOperationType Type;
    union
    {
        struct
        {
            struct AudioTrackStruct* Parent;
            struct AudioTrackStruct* Child;
        } SubTrack;
        struct
        {
            TrackSoundSlot* SoundSlot;
        } AttachSound;
        struct
        {
            struct AudioTrackStruct* Track;
            GameSoundInstance* SoundInstance;
        } RemoveSound;
        struct
        {
            struct AudioTrackStruct* Track;
            ScheduledAudioCommand ScheduledCommand;
        } ScheduleCommand;
        struct
        {
            struct AudioTrackStruct* Track;
            ISoundModifier* Modifier;
            size_t Index;
        } TrackModifier;
        struct
        {
            TrackSoundSlot* SoundSlot;
            ISoundModifier* Modifier;
            size_t Index;
        } SoundModifier;
    } Data;
} AudioThreadOperation;

typedef struct AudioThreadOperationQueueStruct
{
    AudioThreadOperation Operations[AUDIO_ENGINE_QUEUE_CAPACITY];
    atomic_size_t WriteIndex;
    atomic_size_t ReadIndex;
} AudioThreadOperationQueue;

typedef struct AudioTrackStruct
{
    struct AudioEngineStruct* Engine;
    struct AudioTrackStruct* ParentTrack;
    SampleProvider _properties;
    GenericBuffer _publicModifierBuffer;
    GenericBuffer _activeModifierBuffer;
    GenericBuffer _publicSubTrackBuffer;
    GenericBuffer _activeSubTrackBuffer;
    GenericBuffer _activeSoundBuffer;
    GenericBuffer _scheduledCommandBuffer;
    atomic_ullong _currentFrameIndex;
    atomic_size_t _publicSoundCount;
    atomic_bool _isAttached;
} AudioTrack;

typedef struct AudioEngineStruct
{
    AudioTrack _masterTrack;
    AudioFormat _format;
    AudioStream _stream;
    AudioThreadOperationQueue _operationQueue;
    GenericBuffer _publicSoundRegistry;
    bool _ownsAudioDevice;
    atomic_ullong _bufferFillDurationNanoseconds;
} AudioEngine;


// Fields.
static AudioEngine* _activeAudioEngine = NULL;
static _Thread_local AudioEngine* _currentAudioThreadEngine = NULL;


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument, u8"Argument \"%s\" cannot be null.", argumentName);
}

static Error CreateQueueFullError(void)
{
    return Error_Construct3(ErrorCode_BufferTooLarge, u8"Audio engine operation queue is full.");
}

static Error CreateInvalidProviderError(void)
{
    return Error_Construct3(ErrorCode_IllegalArgument, u8"Sample provider does not belong to the active audio engine.");
}

static bool IsFiniteFloat(float value)
{
    return isfinite((double)value);
}

static bool IsFiniteDouble(double value)
{
    return isfinite(value);
}

static double GetCurrentProviderSampleRate(const SampleProvider* provider, float sourceSampleRate)
{
    double SampleRate = provider->_sampleRate._currentValue;
    if (SampleRate <= 0.0)
    {
        SampleRate = (double)sourceSampleRate;
    }

    return SampleRate;
}

static double QuantizeSourceFramePosition(double sourceFramePosition, float sourceSampleRate, double providerSampleRate)
{
    if (providerSampleRate >= (double)sourceSampleRate)
    {
        return sourceFramePosition;
    }

    double SourceFramesPerProviderSample = (double)sourceSampleRate / providerSampleRate;
    if (SourceFramesPerProviderSample <= 1.0)
    {
        return sourceFramePosition;
    }

    if (sourceFramePosition >= 0.0)
    {
        return floor(sourceFramePosition / SourceFramesPerProviderSample) * SourceFramesPerProviderSample;
    }

    return ceil(sourceFramePosition / SourceFramesPerProviderSample) * SourceFramesPerProviderSample;
}

static float ClampFloat(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

static size_t GetSampleFrameCount(const GameSound* sound)
{
    return sound->_sampleCount / (size_t)sound->_format.ChannelCount;
}

static bool Queue_Push(AudioThreadOperationQueue* queue, AudioThreadOperation operation)
{
    size_t ReadIndex = atomic_load_explicit(&queue->ReadIndex, memory_order_acquire);
    size_t WriteIndex = atomic_load_explicit(&queue->WriteIndex, memory_order_relaxed);
    size_t NextWriteIndex = (WriteIndex + 1U) % AUDIO_ENGINE_QUEUE_CAPACITY;
    if (NextWriteIndex == ReadIndex)
    {
        return false;
    }

    queue->Operations[WriteIndex] = operation;
    atomic_store_explicit(&queue->WriteIndex, NextWriteIndex, memory_order_release);
    return true;
}

static bool Queue_TryPop(AudioThreadOperationQueue* queue, AudioThreadOperation* outOperation)
{
    size_t ReadIndex = atomic_load_explicit(&queue->ReadIndex, memory_order_relaxed);
    size_t WriteIndex = atomic_load_explicit(&queue->WriteIndex, memory_order_acquire);
    if (ReadIndex == WriteIndex)
    {
        return false;
    }

    *outOperation = queue->Operations[ReadIndex];
    atomic_store_explicit(&queue->ReadIndex,
        (ReadIndex + 1U) % AUDIO_ENGINE_QUEUE_CAPACITY,
        memory_order_release);
    return true;
}

static void InitializeAutomatedFloat(SoundAutomatedFloat* value, float initialValue)
{
    value->_currentValue = initialValue;
    value->_targetValue = initialValue;
    value->_time._changeDurationLeftSeconds = 0.0;
}

static void InitializeAutomatedDouble(SoundAutomatedDouble* value, double initialValue)
{
    value->_currentValue = initialValue;
    value->_targetValue = initialValue;
    value->_time._changeDurationLeftSeconds = 0.0;
}

static void AdvanceAutomatedFloat(SoundAutomatedFloat* value, double deltaSeconds)
{
    if (value->_time._changeDurationLeftSeconds <= 0.0)
    {
        value->_currentValue = value->_targetValue;
        value->_time._changeDurationLeftSeconds = 0.0;
        return;
    }

    if (deltaSeconds >= value->_time._changeDurationLeftSeconds)
    {
        value->_currentValue = value->_targetValue;
        value->_time._changeDurationLeftSeconds = 0.0;
        return;
    }

    double Remaining = value->_time._changeDurationLeftSeconds;
    double Delta = ((double)value->_targetValue - (double)value->_currentValue) * (deltaSeconds / Remaining);
    value->_currentValue = (float)((double)value->_currentValue + Delta);
    value->_time._changeDurationLeftSeconds -= deltaSeconds;
}

static void AdvanceAutomatedDouble(SoundAutomatedDouble* value, double deltaSeconds)
{
    if (value->_time._changeDurationLeftSeconds <= 0.0)
    {
        value->_currentValue = value->_targetValue;
        value->_time._changeDurationLeftSeconds = 0.0;
        return;
    }

    if (deltaSeconds >= value->_time._changeDurationLeftSeconds)
    {
        value->_currentValue = value->_targetValue;
        value->_time._changeDurationLeftSeconds = 0.0;
        return;
    }

    double Remaining = value->_time._changeDurationLeftSeconds;
    double Delta = (value->_targetValue - value->_currentValue) * (deltaSeconds / Remaining);
    value->_currentValue += Delta;
    value->_time._changeDurationLeftSeconds -= deltaSeconds;
}

static void InitializeSampleProvider(SampleProvider* self,
    GenericBuffer* modifierBuffer,
    AudioEngine* ownerEngine,
    void* owner,
    bool ownerIsTrack,
    double sampleRate,
    float volume,
    float pan)
{
    self->_modifiers = modifierBuffer;
    self->_ownerEngine = ownerEngine;
    self->_owner = owner;
    self->_ownerIsTrack = ownerIsTrack;
    InitializeAutomatedDouble(&self->_sampleRate, sampleRate);
    InitializeAutomatedFloat(&self->_volume, volume);
    InitializeAutomatedFloat(&self->_pan, pan);
}

static void InitializeLazyBuffer(GenericBuffer* buffer, size_t elementSize)
{
    GenericBuffer_AllocateVariable(buffer, 0U, elementSize);
}

static bool EnsureBufferAdditionalCapacity(GenericBuffer* buffer, size_t initialCapacity, size_t additionalElementCount)
{
    if (additionalElementCount == 0U)
    {
        return true;
    }

    size_t RequiredCount = buffer->_count + additionalElementCount;
    if (RequiredCount < buffer->_count)
    {
        return false;
    }

    if (buffer->_capacity == 0U)
    {
        size_t InitialRequiredCount = initialCapacity;
        if (InitialRequiredCount < RequiredCount)
        {
            InitialRequiredCount = RequiredCount;
        }

        return GenericBuffer_EnsureTotalCapacity(buffer, InitialRequiredCount);
    }

    return GenericBuffer_EnsureTotalCapacity(buffer, RequiredCount);
}

static void ResetReverbDelayState(ReverbSoundModifier* self)
{
    size_t DelayBufferFloatCount = self->_delayBufferFrameCount * AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT;
    if ((self->_delayBuffer != NULL) && (DelayBufferFloatCount > 0U))
    {
        Memory_Zero(self->_delayBuffer, sizeof(float) * DelayBufferFloatCount);
    }

    self->_delayBufferWriteFrameIndex = 0U;
    self->_previousLowPassLeft = 0.0f;
    self->_previousLowPassRight = 0.0f;
}

static Error InitializeTrack(AudioEngine* engine, AudioTrack* track, AudioTrack* parentTrack)
{
    Memory_Zero(track, sizeof(*track));
    track->Engine = engine;
    track->ParentTrack = parentTrack;
    InitializeLazyBuffer(&track->_publicModifierBuffer, sizeof(ISoundModifier*));
    InitializeLazyBuffer(&track->_activeModifierBuffer, sizeof(ISoundModifier*));
    InitializeLazyBuffer(&track->_publicSubTrackBuffer, sizeof(AudioTrack*));
    InitializeLazyBuffer(&track->_activeSubTrackBuffer, sizeof(AudioTrack*));
    InitializeLazyBuffer(&track->_activeSoundBuffer, sizeof(TrackSoundSlot*));
    InitializeLazyBuffer(&track->_scheduledCommandBuffer, sizeof(ScheduledAudioCommand));
    InitializeSampleProvider(&track->_properties,
        &track->_publicModifierBuffer,
        engine,
        track,
        true,
        (double)engine->_format.SampleRate,
        1.0f,
        SOUND_SAMPLE_PAN_MIDDLE);
    atomic_store(&track->_currentFrameIndex, 0U);
    atomic_store(&track->_publicSoundCount, 0U);
    atomic_store(&track->_isAttached, parentTrack == NULL);
    return Error_CreateSuccess();
}

static void DeconstructTrack(AudioTrack* track)
{
    for (size_t i = 0; i < track->_publicSubTrackBuffer._count; i++)
    {
        AudioTrack* ChildTrack = *(AudioTrack**)GenericBuffer_GetPointerToElement(&track->_publicSubTrackBuffer, i);
        DeconstructTrack(ChildTrack);
        Memory_Free(ChildTrack);
    }

    Memory_Free(track->_publicModifierBuffer._data);
    Memory_Free(track->_activeModifierBuffer._data);
    Memory_Free(track->_publicSubTrackBuffer._data);
    Memory_Free(track->_activeSubTrackBuffer._data);
    Memory_Free(track->_activeSoundBuffer._data);
    Memory_Free(track->_scheduledCommandBuffer._data);
}

static TrackSoundSlot* FindSoundSlot(AudioEngine* engine, GameSoundInstance* instance)
{
    for (size_t i = 0; i < engine->_publicSoundRegistry._count; i++)
    {
        TrackSoundSlot* Slot = *(TrackSoundSlot**)GenericBuffer_GetPointerToElement(&engine->_publicSoundRegistry, i);
        if (Slot->Instance == instance)
        {
            return Slot;
        }
    }

    return NULL;
}

static bool SampleProvider_BelongsToActiveEngine(const SampleProvider* provider)
{
    return (provider->_ownerEngine != NULL) && (provider->_ownerEngine == _activeAudioEngine) && (provider->_owner != NULL);
}

static AudioTrack* SampleProvider_GetOwningTrack(SampleProvider* provider)
{
    if (provider->_ownerIsTrack)
    {
        return provider->_owner;
    }

    TrackSoundSlot* SoundSlot = provider->_owner;
    return SoundSlot->OwningTrack;
}

static TrackSoundSlot* SampleProvider_GetOwningSoundSlot(SampleProvider* provider)
{
    if (provider->_ownerIsTrack)
    {
        return NULL;
    }

    return provider->_owner;
}

static ComparisonResult CompareScheduledCommands(GenericBuffer* buffer,
    GenericBufferElementData a,
    GenericBufferElementData b,
    const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);

    const ScheduledAudioCommand* CommandA = a._element;
    const ScheduledAudioCommand* CommandB = b._element;
    if (CommandA->TargetFrameIndex < CommandB->TargetFrameIndex)
    {
        return ComparisonResult_ALessThanB;
    }
    if (CommandA->TargetFrameIndex > CommandB->TargetFrameIndex)
    {
        return ComparisonResult_AGreaterThanB;
    }

    return ComparisonResult_AEqualsB;
}

static Error InsertScheduledCommand(AudioTrack* track, ScheduledAudioCommand scheduledCommand)
{
    if (!EnsureBufferAdditionalCapacity(&track->_scheduledCommandBuffer, AUDIO_ENGINE_INITIAL_COMMAND_CAPACITY, 1U))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to reserve scheduled audio command storage.");
    }
    if (!GenericBuffer_AddLast(&track->_scheduledCommandBuffer, &scheduledCommand))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to append scheduled audio command.");
    }
    if (!GenericBuffer_SortAscendingAllocating(&track->_scheduledCommandBuffer, CompareScheduledCommands, NULL))
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"Failed to sort scheduled audio commands.");
    }

    return Error_CreateSuccess();
}

static void ApplyPanAndVolume(float* leftSample, float* rightSample, float volume, float pan)
{
    float ClampedPan = ClampFloat(pan, SOUND_SAMPLE_PAN_LEFT, SOUND_SAMPLE_PAN_RIGHT);
    float Angle = (ClampedPan + 1.0f) * (PI / 4.0f);
    float LeftGain = cosf(Angle) * volume;
    float RightGain = sinf(Angle) * volume;
    *leftSample *= LeftGain;
    *rightSample *= RightGain;
}

static float ReadInterpolatedSourceSample(const GameSound* sound, double sourceFramePosition, size_t channelIndex)
{
    size_t FrameCount = GetSampleFrameCount(sound);
    if (FrameCount == 0U)
    {
        return 0.0f;
    }

    if (sourceFramePosition < 0.0)
    {
        sourceFramePosition = 0.0;
    }

    double MaximumFrameIndex = (double)(FrameCount - 1U);
    if (sourceFramePosition > MaximumFrameIndex)
    {
        sourceFramePosition = MaximumFrameIndex;
    }

    size_t FloorFrameIndex = (size_t)sourceFramePosition;
    size_t NextFrameIndex = FloorFrameIndex + 1U;
    if (NextFrameIndex >= FrameCount)
    {
        NextFrameIndex = FloorFrameIndex;
    }

    double Fraction = sourceFramePosition - (double)FloorFrameIndex;
    size_t ChannelCount = (size_t)sound->_format.ChannelCount;
    size_t BaseIndexA = (FloorFrameIndex * ChannelCount) + channelIndex;
    size_t BaseIndexB = (NextFrameIndex * ChannelCount) + channelIndex;
    float SampleA = sound->_samples[BaseIndexA];
    float SampleB = sound->_samples[BaseIndexB];
    return (float)((double)SampleA + ((double)(SampleB - SampleA) * Fraction));
}

static bool CommandIsSet(const AudioCommand* command)
{
    return command->_function != NULL;
}

static Error ExecuteCommand(AudioTrack* track, const AudioCommand* command)
{
    if (!CommandIsSet(command))
    {
        return Error_CreateSuccess();
    }

    return command->_function(track, &command->_userData);
}

static void ResetSoundEndStateIfPlaying(TrackSoundSlot* soundSlot)
{
    if (soundSlot->Instance->_state == SoundInstanceState_Playing)
    {
        soundSlot->_endCommandRan = false;
        soundSlot->_tailEndCommandRan = false;
    }
}

static Error ProcessTailEndAfterModifiers(TrackSoundSlot* soundSlot, AudioTrack* track)
{
    if ((soundSlot->Instance->_state != SoundInstanceState_Ended) || soundSlot->_hasModifierTail)
    {
        return Error_CreateSuccess();
    }
    if (soundSlot->_tailEndCommandRan)
    {
        return Error_CreateSuccess();
    }

    soundSlot->_tailEndCommandRan = true;
    Error Result = ExecuteCommand(track, &soundSlot->Instance->_tailEndCommand);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ResetSoundEndStateIfPlaying(soundSlot);
    return Error_CreateSuccess();
}

static Error ProcessLoopOrEndBeforeSampling(TrackSoundSlot* soundSlot, AudioTrack* track)
{
    GameSoundInstance* Instance = soundSlot->Instance;
    size_t FrameCount = GetSampleFrameCount(Instance->_source);
    if (FrameCount == 0U)
    {
        Instance->_state = SoundInstanceState_Ended;
        return Error_CreateSuccess();
    }

    while (true)
    {
        double SourceFramePosition = Instance->_sampleIndex;
        if ((SourceFramePosition >= 0.0) && (SourceFramePosition < (double)FrameCount))
        {
            return Error_CreateSuccess();
        }

        if ((Instance->_state != SoundInstanceState_Playing) || !Instance->_isLooped)
        {
            Instance->_state = SoundInstanceState_Ended;
            if (!soundSlot->_endCommandRan)
            {
                soundSlot->_endCommandRan = true;
                Error Result = ExecuteCommand(track, &Instance->_endCommand);
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
                ResetSoundEndStateIfPlaying(soundSlot);
                continue;
            }

            return Error_CreateSuccess();
        }

        Error Result = ExecuteCommand(track, &Instance->_loopCommand);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        if (Instance->_state != SoundInstanceState_Playing)
        {
            return Error_CreateSuccess();
        }

        double LoopFrameCount = (double)FrameCount;
        while (SourceFramePosition < 0.0)
        {
            SourceFramePosition += LoopFrameCount;
        }
        while (SourceFramePosition >= LoopFrameCount)
        {
            SourceFramePosition -= LoopFrameCount;
        }

        Instance->_sampleIndex = SourceFramePosition;
    }
}

static bool HasModifierSignal(float leftSample, float rightSample)
{
    return (fabsf(leftSample) > AUDIO_ENGINE_SIGNAL_EPSILON)
        || (fabsf(rightSample) > AUDIO_ENGINE_SIGNAL_EPSILON);
}

static bool ReverbSoundModifier_ModifyInternal(void* selfPointer, SoundModifySectionContext* context)
{
    ReverbSoundModifier* Self = selfPointer;
    size_t FrameCount = context->_modifyCount / (size_t)context->_targetFormat.ChannelCount;
    double DelaySeconds = Self->DelaySeconds._currentValue;
    if (DelaySeconds < (1.0 / (double)context->_targetFormat.SampleRate))
    {
        DelaySeconds = 1.0 / (double)context->_targetFormat.SampleRate;
    }

    size_t DelayFrames = (size_t)(DelaySeconds * (double)context->_targetFormat.SampleRate);
    if (DelayFrames == 0U)
    {
        DelayFrames = 1U;
    }

    if (Self->_delayBufferFrameCount < DelayFrames)
    {
        Self->_delayBuffer = Memory_Reallocate(Self->_delayBuffer,
            sizeof(float) * DelayFrames * AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT);
        Self->_delayBufferFrameCount = DelayFrames;
        ResetReverbDelayState(Self);
    }
    else if ((Self->_activeDelayBufferFrameCount != DelayFrames) && (Self->_delayBuffer != NULL))
    {
        ResetReverbDelayState(Self);
    }
    Self->_activeDelayBufferFrameCount = DelayFrames;

    bool HasTail = false;
    for (size_t i = 0; i < FrameCount; i++)
    {
        AdvanceAutomatedFloat(&Self->DryVolume, context->_singleFrameDurationSeconds);
        AdvanceAutomatedFloat(&Self->WetVolume, context->_singleFrameDurationSeconds);
        AdvanceAutomatedFloat(&Self->Feedback, context->_singleFrameDurationSeconds);
        AdvanceAutomatedFloat(&Self->Damping, context->_singleFrameDurationSeconds);
        AdvanceAutomatedDouble(&Self->DelaySeconds, context->_singleFrameDurationSeconds);

        size_t SampleIndex = context->_startIndex + (i * AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT);
        size_t DelayBaseIndex = Self->_delayBufferWriteFrameIndex * AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT;

        float InputLeft = context->SampleBuffer[SampleIndex];
        float InputRight = context->SampleBuffer[SampleIndex + 1U];
        float DelayedLeft = Self->_delayBuffer[DelayBaseIndex];
        float DelayedRight = Self->_delayBuffer[DelayBaseIndex + 1U];

        Self->_previousLowPassLeft += (DelayedLeft - Self->_previousLowPassLeft)
            * (1.0f - ClampFloat(Self->Damping._currentValue, 0.0f, 1.0f));
        Self->_previousLowPassRight += (DelayedRight - Self->_previousLowPassRight)
            * (1.0f - ClampFloat(Self->Damping._currentValue, 0.0f, 1.0f));

        context->SampleBuffer[SampleIndex] = (InputLeft * Self->DryVolume._currentValue)
            + (Self->_previousLowPassLeft * Self->WetVolume._currentValue);
        context->SampleBuffer[SampleIndex + 1U] = (InputRight * Self->DryVolume._currentValue)
            + (Self->_previousLowPassRight * Self->WetVolume._currentValue);

        Self->_delayBuffer[DelayBaseIndex] = InputLeft
            + (Self->_previousLowPassLeft * ClampFloat(Self->Feedback._currentValue, 0.0f, 0.999f));
        Self->_delayBuffer[DelayBaseIndex + 1U] = InputRight
            + (Self->_previousLowPassRight * ClampFloat(Self->Feedback._currentValue, 0.0f, 0.999f));
        Self->_delayBufferWriteFrameIndex = (Self->_delayBufferWriteFrameIndex + 1U) % Self->_activeDelayBufferFrameCount;

        if (HasModifierSignal(Self->_delayBuffer[DelayBaseIndex], Self->_delayBuffer[DelayBaseIndex + 1U])
            || HasModifierSignal(Self->_previousLowPassLeft, Self->_previousLowPassRight))
        {
            HasTail = true;
        }
    }

    return HasTail;
}

static void ReverbSoundModifier_ResetStateInternal(void* selfPointer)
{
    ReverbSoundModifier* Self = selfPointer;
    ResetReverbDelayState(Self);
}

static bool BiQuadPassSoundModifier_ModifyInternal(void* selfPointer, SoundModifySectionContext* context)
{
    BiQuadPassSoundModifier* Self = selfPointer;
    size_t FrameCount = context->_modifyCount / (size_t)context->_targetFormat.ChannelCount;
    bool HasTail = false;

    for (size_t i = 0; i < FrameCount; i++)
    {
        AdvanceAutomatedFloat(&Self->WetVolume, context->_singleFrameDurationSeconds);
        AdvanceAutomatedFloat(&Self->CutoffFrequency, context->_singleFrameDurationSeconds);
        AdvanceAutomatedFloat(&Self->Resonance, context->_singleFrameDurationSeconds);

        float Cutoff = ClampFloat(Self->CutoffFrequency._currentValue, 10.0f, (context->_targetFormat.SampleRate * 0.45f));
        float Resonance = ClampFloat(Self->Resonance._currentValue, 0.1f, 20.0f);
        float Omega = 2.0f * PI * Cutoff / context->_targetFormat.SampleRate;
        float SinOmega = sinf(Omega);
        float CosOmega = cosf(Omega);
        float Alpha = SinOmega / (2.0f * Resonance);

        float B0;
        float B1;
        float B2;
        float A0;
        float A1;
        float A2;
        if (Self->PassType == BiQuadPassType_High)
        {
            B0 = (1.0f + CosOmega) * 0.5f;
            B1 = -(1.0f + CosOmega);
            B2 = (1.0f + CosOmega) * 0.5f;
        }
        else
        {
            B0 = (1.0f - CosOmega) * 0.5f;
            B1 = 1.0f - CosOmega;
            B2 = (1.0f - CosOmega) * 0.5f;
        }
        A0 = 1.0f + Alpha;
        A1 = -2.0f * CosOmega;
        A2 = 1.0f - Alpha;

        B0 /= A0;
        B1 /= A0;
        B2 /= A0;
        A1 /= A0;
        A2 /= A0;

        size_t SampleIndex = context->_startIndex + (i * AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT);
        float InputLeft = context->SampleBuffer[SampleIndex];
        float InputRight = context->SampleBuffer[SampleIndex + 1U];

        float FilteredLeft = (B0 * InputLeft) + (B1 * Self->_x1Left) + (B2 * Self->_x2Left)
            - (A1 * Self->_y1Left) - (A2 * Self->_y2Left);
        float FilteredRight = (B0 * InputRight) + (B1 * Self->_x1Right) + (B2 * Self->_x2Right)
            - (A1 * Self->_y1Right) - (A2 * Self->_y2Right);

        Self->_x2Left = Self->_x1Left;
        Self->_x1Left = InputLeft;
        Self->_y2Left = Self->_y1Left;
        Self->_y1Left = FilteredLeft;
        Self->_x2Right = Self->_x1Right;
        Self->_x1Right = InputRight;
        Self->_y2Right = Self->_y1Right;
        Self->_y1Right = FilteredRight;

        context->SampleBuffer[SampleIndex] = (InputLeft * (1.0f - Self->WetVolume._currentValue))
            + (FilteredLeft * Self->WetVolume._currentValue);
        context->SampleBuffer[SampleIndex + 1U] = (InputRight * (1.0f - Self->WetVolume._currentValue))
            + (FilteredRight * Self->WetVolume._currentValue);

        if (HasModifierSignal(FilteredLeft, FilteredRight))
        {
            HasTail = true;
        }
    }

    return HasTail;
}

static void BiQuadPassSoundModifier_ResetStateInternal(void* selfPointer)
{
    BiQuadPassSoundModifier* Self = selfPointer;
    Self->_x1Left = 0.0f;
    Self->_x2Left = 0.0f;
    Self->_y1Left = 0.0f;
    Self->_y2Left = 0.0f;
    Self->_x1Right = 0.0f;
    Self->_x2Right = 0.0f;
    Self->_y1Right = 0.0f;
    Self->_y2Right = 0.0f;
}

static float QuantizeSample(float value, float bitDepth)
{
    float Levels = powf(2.0f, bitDepth) - 1.0f;
    if (Levels < 1.0f)
    {
        Levels = 1.0f;
    }

    float Normalized = ClampFloat(value, -1.0f, 1.0f);
    float Quantized = roundf(((Normalized + 1.0f) * 0.5f) * Levels) / Levels;
    return (Quantized * 2.0f) - 1.0f;
}

static bool BitCrusherModifier_ModifyInternal(void* selfPointer, SoundModifySectionContext* context)
{
    BitCrusherModifier* Self = selfPointer;
    size_t FrameCount = context->_modifyCount / (size_t)context->_targetFormat.ChannelCount;
    bool HasTail = false;

    for (size_t i = 0; i < FrameCount; i++)
    {
        AdvanceAutomatedFloat(&Self->WetVolume, context->_singleFrameDurationSeconds);
        AdvanceAutomatedDouble(&Self->HoldSeconds, context->_singleFrameDurationSeconds);
        AdvanceAutomatedFloat(&Self->BitDepth, context->_singleFrameDurationSeconds);

        size_t SampleIndex = context->_startIndex + (i * AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT);
        float InputLeft = context->SampleBuffer[SampleIndex];
        float InputRight = context->SampleBuffer[SampleIndex + 1U];

        if (Self->_holdSecondsLeft <= 0.0)
        {
            double HoldSeconds = Self->HoldSeconds._currentValue;
            if (HoldSeconds <= 0.0)
            {
                HoldSeconds = context->_singleFrameDurationSeconds;
            }
            Self->_holdSecondsLeft = HoldSeconds;
            Self->_heldLeft = QuantizeSample(InputLeft, ClampFloat(Self->BitDepth._currentValue, 1.0f, 24.0f));
            Self->_heldRight = QuantizeSample(InputRight, ClampFloat(Self->BitDepth._currentValue, 1.0f, 24.0f));
        }
        Self->_holdSecondsLeft -= context->_singleFrameDurationSeconds;

        context->SampleBuffer[SampleIndex] = (InputLeft * (1.0f - Self->WetVolume._currentValue))
            + (Self->_heldLeft * Self->WetVolume._currentValue);
        context->SampleBuffer[SampleIndex + 1U] = (InputRight * (1.0f - Self->WetVolume._currentValue))
            + (Self->_heldRight * Self->WetVolume._currentValue);

        if (HasModifierSignal(Self->_heldLeft, Self->_heldRight))
        {
            HasTail = true;
        }
    }

    return HasTail;
}

static void BitCrusherModifier_ResetStateInternal(void* selfPointer)
{
    BitCrusherModifier* Self = selfPointer;
    Self->_holdSecondsLeft = 0.0;
    Self->_heldLeft = 0.0f;
    Self->_heldRight = 0.0f;
}

static Error InsertModifier(GenericBuffer* buffer, ISoundModifier* modifier, size_t index)
{
    if (index > buffer->_count)
    {
        return Error_Construct3(ErrorCode_IndexOutOfBounds, u8"Modifier index %zu is out of bounds.", index);
    }
    if (!EnsureBufferAdditionalCapacity(buffer, AUDIO_ENGINE_INITIAL_MODIFIER_CAPACITY, 1U))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to reserve sound modifier storage.");
    }
    if (!GenericBuffer_Insert(buffer, &modifier, index))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to insert sound modifier.");
    }

    return Error_CreateSuccess();
}

static Error RemoveModifier(GenericBuffer* buffer, ISoundModifier* modifier)
{
    for (size_t i = 0; i < buffer->_count; i++)
    {
        ISoundModifier* ExistingModifier = *(ISoundModifier**)GenericBuffer_GetPointerToElement(buffer, i);
        if (ExistingModifier == modifier)
        {
            if (!GenericBuffer_RemoveAt(buffer, i))
            {
                return Error_Construct3(ErrorCode_InvalidOperation, u8"Failed to remove sound modifier.");
            }

            return Error_CreateSuccess();
        }
    }

    return Error_Construct3(ErrorCode_InvalidOperation, u8"Modifier is not present on the sample provider.");
}

static Error ApplyModifiers(GenericBuffer* modifierBuffer, SoundModifySectionContext* context, bool* outHasTail)
{
    bool HasTail = false;
    for (size_t i = 0; i < modifierBuffer->_count; i++)
    {
        ISoundModifier* Modifier = *(ISoundModifier**)GenericBuffer_GetPointerToElement(modifierBuffer, i);
        if (ISoundModifier_Modify(Modifier, context))
        {
            HasTail = true;
        }
    }

    *outHasTail = HasTail;
    return Error_CreateSuccess();
}

static void RemoveSoundSlotAt(AudioTrack* track, size_t index)
{
    TrackSoundSlot* SoundSlot = *(TrackSoundSlot**)GenericBuffer_GetPointerToElement(&track->_activeSoundBuffer, index);
    atomic_store(&SoundSlot->_isAttached, false);
    if (!GenericBuffer_RemoveAt(&track->_activeSoundBuffer, index))
    {
        return;
    }

    size_t PublicCount = atomic_load(&track->_publicSoundCount);
    if (PublicCount > 0U)
    {
        atomic_store(&track->_publicSoundCount, PublicCount - 1U);
    }
}

static Error AttachSoundSlot(AudioTrack* track, TrackSoundSlot* soundSlot)
{
    soundSlot->OwningTrack = track;
    if (!EnsureBufferAdditionalCapacity(&track->_activeSoundBuffer, AUDIO_ENGINE_INITIAL_SOUND_CAPACITY, 1U))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to reserve attached sound storage.");
    }
    if (!GenericBuffer_AddLast(&track->_activeSoundBuffer, &soundSlot))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to attach sound instance to track.");
    }

    atomic_store(&soundSlot->_isAttached, true);
    atomic_store(&soundSlot->_isPendingAttach, false);
    return Error_CreateSuccess();
}

static Error AttachSubTrack(AudioTrack* parent, AudioTrack* child)
{
    if (!EnsureBufferAdditionalCapacity(&parent->_activeSubTrackBuffer, AUDIO_ENGINE_INITIAL_SUBTRACK_CAPACITY, 1U))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to reserve audio subtrack storage.");
    }
    if (!GenericBuffer_AddLast(&parent->_activeSubTrackBuffer, &child))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to attach audio subtrack.");
    }
    child->ParentTrack = parent;
    atomic_store(&child->_isAttached, true);
    return Error_CreateSuccess();
}

static void DetachSubTrack(AudioTrack* parent, AudioTrack* child)
{
    for (size_t i = 0; i < parent->_activeSubTrackBuffer._count; i++)
    {
        AudioTrack* ExistingChild = *(AudioTrack**)GenericBuffer_GetPointerToElement(&parent->_activeSubTrackBuffer, i);
        if (ExistingChild == child)
        {
            GenericBuffer_RemoveAt(&parent->_activeSubTrackBuffer, i);
            atomic_store(&child->_isAttached, false);
            child->ParentTrack = NULL;
            return;
        }
    }
}

static Error ProcessAudioThreadOperation(AudioEngine* engine, AudioThreadOperation operation)
{
    UNUSED(engine);

    switch (operation.Type)
    {
        case AudioThreadOperationType_AttachSubTrack:
            return AttachSubTrack(operation.Data.SubTrack.Parent, operation.Data.SubTrack.Child);

        case AudioThreadOperationType_DetachSubTrack:
            DetachSubTrack(operation.Data.SubTrack.Parent, operation.Data.SubTrack.Child);
            return Error_CreateSuccess();

        case AudioThreadOperationType_AttachSound:
            return AttachSoundSlot(operation.Data.AttachSound.SoundSlot->OwningTrack, operation.Data.AttachSound.SoundSlot);

        case AudioThreadOperationType_RemoveSound:
        {
            AudioTrack* Track = operation.Data.RemoveSound.Track;
            for (size_t i = 0; i < Track->_activeSoundBuffer._count; i++)
            {
                TrackSoundSlot* SoundSlot = *(TrackSoundSlot**)GenericBuffer_GetPointerToElement(&Track->_activeSoundBuffer, i);
                if (SoundSlot->Instance == operation.Data.RemoveSound.SoundInstance)
                {
                    RemoveSoundSlotAt(Track, i);
                    break;
                }
            }
            return Error_CreateSuccess();
        }

        case AudioThreadOperationType_ScheduleCommand:
            return InsertScheduledCommand(operation.Data.ScheduleCommand.Track,
                operation.Data.ScheduleCommand.ScheduledCommand);

        case AudioThreadOperationType_AddTrackModifier:
            return InsertModifier(&operation.Data.TrackModifier.Track->_activeModifierBuffer,
                operation.Data.TrackModifier.Modifier,
                operation.Data.TrackModifier.Index);

        case AudioThreadOperationType_RemoveTrackModifier:
            return RemoveModifier(&operation.Data.TrackModifier.Track->_activeModifierBuffer,
                operation.Data.TrackModifier.Modifier);

        case AudioThreadOperationType_ClearTrackModifiers:
            GenericBuffer_Clear(&operation.Data.TrackModifier.Track->_activeModifierBuffer);
            return Error_CreateSuccess();

        case AudioThreadOperationType_AddSoundModifier:
            return InsertModifier(&operation.Data.SoundModifier.SoundSlot->_activeModifierBuffer,
                operation.Data.SoundModifier.Modifier,
                operation.Data.SoundModifier.Index);

        case AudioThreadOperationType_RemoveSoundModifier:
            return RemoveModifier(&operation.Data.SoundModifier.SoundSlot->_activeModifierBuffer,
                operation.Data.SoundModifier.Modifier);

        case AudioThreadOperationType_ClearSoundModifiers:
            GenericBuffer_Clear(&operation.Data.SoundModifier.SoundSlot->_activeModifierBuffer);
            return Error_CreateSuccess();
    }

    return Error_Construct3(ErrorCode_InvalidOperation, u8"Unknown audio thread operation.");
}

static void ProcessPendingOperations(AudioEngine* engine)
{
    AudioThreadOperation Operation;
    while (Queue_TryPop(&engine->_operationQueue, &Operation))
    {
        Error Result = ProcessAudioThreadOperation(engine, Operation);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
        }
    }
}

static Error DispatchTrackCommands(AudioTrack* track)
{
    uint64_t CurrentFrameIndex = atomic_load(&track->_currentFrameIndex);
    while (track->_scheduledCommandBuffer._count > 0U)
    {
        ScheduledAudioCommand* ScheduledCommand = GenericBuffer_GetPointerToFirst(&track->_scheduledCommandBuffer);
        if (ScheduledCommand->TargetFrameIndex > CurrentFrameIndex)
        {
            break;
        }

        AudioCommand Command = ScheduledCommand->Command;
        GenericBuffer_RemoveFirst(&track->_scheduledCommandBuffer);
        Error Result = ExecuteCommand(track, &Command);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error MixSingleSoundFrame(TrackSoundSlot* soundSlot, AudioTrack* track, float* outLeft, float* outRight)
{
    GameSoundInstance* Instance = soundSlot->Instance;
    *outLeft = 0.0f;
    *outRight = 0.0f;

    if (atomic_load(&soundSlot->_isPendingRemove))
    {
        Instance->_state = SoundInstanceState_Ended;
    }

    if (Instance->_state == SoundInstanceState_Paused)
    {
        return Error_CreateSuccess();
    }

    AdvanceAutomatedDouble(&Instance->_sampleSpeed, AudioEngine_GetSecondsPerFrame(track->Engine));
    AdvanceAutomatedDouble(&Instance->_sampleProperties._sampleRate, AudioEngine_GetSecondsPerFrame(track->Engine));
    AdvanceAutomatedFloat(&Instance->_sampleProperties._volume, AudioEngine_GetSecondsPerFrame(track->Engine));
    AdvanceAutomatedFloat(&Instance->_sampleProperties._pan, AudioEngine_GetSecondsPerFrame(track->Engine));

    Error Result = ProcessLoopOrEndBeforeSampling(soundSlot, track);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    float FrameSamples[AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT] = { 0.0f, 0.0f };
    if (Instance->_state == SoundInstanceState_Playing)
    {
        double SamplingRate = GetCurrentProviderSampleRate(&Instance->_sampleProperties, Instance->_source->_format.SampleRate);
        double SourceFramePosition = QuantizeSourceFramePosition(Instance->_sampleIndex,
            Instance->_source->_format.SampleRate,
            SamplingRate);
        FrameSamples[0] = ReadInterpolatedSourceSample(Instance->_source, SourceFramePosition, 0U);
        if (Instance->_source->_format.ChannelCount > 1)
        {
            FrameSamples[1] = ReadInterpolatedSourceSample(Instance->_source, SourceFramePosition, 1U);
        }
        else
        {
            FrameSamples[1] = FrameSamples[0];
        }

        Instance->_sampleIndex += Instance->_sampleSpeed._currentValue
            * ((double)Instance->_source->_format.SampleRate / (double)track->Engine->_format.SampleRate);
    }

    SoundModifySectionContext Context;
    Context._sound = Instance;
    Context._sampleTimeSeconds = AudioTrack_GetCurrentSecond(track);
    Context._singleFrameDurationSeconds = AudioEngine_GetSecondsPerFrame(track->Engine);
    Context.SampleBuffer = FrameSamples;
    Context._sampleCount = AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT;
    Context._modifyCount = AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT;
    Context._startIndex = 0U;
    Context._targetFormat = track->Engine->_format;

    bool HasModifierTail = false;
    Result = ApplyModifiers(&soundSlot->_activeModifierBuffer, &Context, &HasModifierTail);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    soundSlot->_hasModifierTail = HasModifierTail;

    Result = ProcessTailEndAfterModifiers(soundSlot, track);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ApplyPanAndVolume(&FrameSamples[0],
        &FrameSamples[1],
        Instance->_sampleProperties._volume._currentValue,
        Instance->_sampleProperties._pan._currentValue);

    *outLeft = FrameSamples[0];
    *outRight = FrameSamples[1];
    return Error_CreateSuccess();
}

static Error MixTrackFrame(AudioTrack* track, float* outLeft, float* outRight)
{
    *outLeft = 0.0f;
    *outRight = 0.0f;

    AdvanceAutomatedDouble(&track->_properties._sampleRate, AudioEngine_GetSecondsPerFrame(track->Engine));
    AdvanceAutomatedFloat(&track->_properties._volume, AudioEngine_GetSecondsPerFrame(track->Engine));
    AdvanceAutomatedFloat(&track->_properties._pan, AudioEngine_GetSecondsPerFrame(track->Engine));

    Error Result = DispatchTrackCommands(track);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t SoundIndex = 0U;
    while (SoundIndex < track->_activeSoundBuffer._count)
    {
        TrackSoundSlot* SoundSlot = *(TrackSoundSlot**)GenericBuffer_GetPointerToElement(&track->_activeSoundBuffer, SoundIndex);
        float SoundLeft = 0.0f;
        float SoundRight = 0.0f;
        Result = MixSingleSoundFrame(SoundSlot, track, &SoundLeft, &SoundRight);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        *outLeft += SoundLeft;
        *outRight += SoundRight;

        bool RemoveSound = atomic_load(&SoundSlot->_isPendingRemove)
            || ((SoundSlot->Instance->_state == SoundInstanceState_Ended) && !SoundSlot->_hasModifierTail);
        if (RemoveSound)
        {
            RemoveSoundSlotAt(track, SoundIndex);
            continue;
        }

        SoundIndex++;
    }

    for (size_t i = 0; i < track->_activeSubTrackBuffer._count; i++)
    {
        AudioTrack* ChildTrack = *(AudioTrack**)GenericBuffer_GetPointerToElement(&track->_activeSubTrackBuffer, i);
        float ChildLeft = 0.0f;
        float ChildRight = 0.0f;
        Result = MixTrackFrame(ChildTrack, &ChildLeft, &ChildRight);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        *outLeft += ChildLeft;
        *outRight += ChildRight;
    }

    float TrackSamples[AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT] = { *outLeft, *outRight };
    SoundModifySectionContext Context;
    Context._sound = NULL;
    Context._sampleTimeSeconds = AudioTrack_GetCurrentSecond(track);
    Context._singleFrameDurationSeconds = AudioEngine_GetSecondsPerFrame(track->Engine);
    Context.SampleBuffer = TrackSamples;
    Context._sampleCount = AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT;
    Context._modifyCount = AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT;
    Context._startIndex = 0U;
    Context._targetFormat = track->Engine->_format;
    bool HasModifierTail;
    Result = ApplyModifiers(&track->_activeModifierBuffer, &Context, &HasModifierTail);
    UNUSED(HasModifierTail);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ApplyPanAndVolume(&TrackSamples[0],
        &TrackSamples[1],
        track->_properties._volume._currentValue,
        track->_properties._pan._currentValue);

    *outLeft = TrackSamples[0];
    *outRight = TrackSamples[1];

    atomic_fetch_add(&track->_currentFrameIndex, 1U);
    return Error_CreateSuccess();
}

static void AudioEngine_StreamCallback(void* bufferData, unsigned int frames)
{
    AudioEngine* Engine = _activeAudioEngine;
    if (Engine == NULL)
    {
        return;
    }

    _currentAudioThreadEngine = Engine;
    clock_t StartClock = clock();
    float* SampleBuffer = bufferData;

    ProcessPendingOperations(Engine);

    for (unsigned int i = 0U; i < frames; i++)
    {
        ProcessPendingOperations(Engine);

        float MixedLeft = 0.0f;
        float MixedRight = 0.0f;
        Error Result = MixTrackFrame(&Engine->_masterTrack, &MixedLeft, &MixedRight);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            MixedLeft = 0.0f;
            MixedRight = 0.0f;
        }

        SampleBuffer[(size_t)i * AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT] = ClampFloat(MixedLeft, -1.0f, 1.0f);
        SampleBuffer[((size_t)i * AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT) + 1U] = ClampFloat(MixedRight, -1.0f, 1.0f);
    }

    clock_t ElapsedClock = clock() - StartClock;
    double ElapsedSeconds = (double)ElapsedClock / (double)CLOCKS_PER_SEC;
    atomic_store(&Engine->_bufferFillDurationNanoseconds, (uint64_t)(ElapsedSeconds * 1000000000.0));
    _currentAudioThreadEngine = NULL;
}

static bool IsAudioThreadForTrack(AudioTrack* track)
{
    return (_currentAudioThreadEngine != NULL) && (_currentAudioThreadEngine == track->Engine);
}

static bool IsAudioThreadForProviderEngine(AudioEngine* engine)
{
    return (_currentAudioThreadEngine != NULL) && (_currentAudioThreadEngine == engine);
}

static Error QueueOperation(AudioEngine* engine, AudioThreadOperation operation)
{
    if (!Queue_Push(&engine->_operationQueue, operation))
    {
        return CreateQueueFullError();
    }

    return Error_CreateSuccess();
}

static Error QueueTrackModifierOperation(AudioTrack* track,
    AudioThreadOperationType type,
    ISoundModifier* modifier,
    size_t index)
{
    AudioThreadOperation Operation;
    Memory_Zero(&Operation, sizeof(Operation));
    Operation.Type = type;
    Operation.Data.TrackModifier.Track = track;
    Operation.Data.TrackModifier.Modifier = modifier;
    Operation.Data.TrackModifier.Index = index;
    return QueueOperation(track->Engine, Operation);
}

static Error QueueSoundModifierOperation(AudioEngine* engine,
    TrackSoundSlot* soundSlot,
    AudioThreadOperationType type,
    ISoundModifier* modifier,
    size_t index)
{
    AudioThreadOperation Operation;
    Memory_Zero(&Operation, sizeof(Operation));
    Operation.Type = type;
    Operation.Data.SoundModifier.SoundSlot = soundSlot;
    Operation.Data.SoundModifier.Modifier = modifier;
    Operation.Data.SoundModifier.Index = index;
    return QueueOperation(engine, Operation);
}


// Public functions.
Error AudioEngine_Construct1(AudioEngine** outEngine)
{
    if (outEngine == NULL)
    {
        return CreateNullArgumentError(u8"outEngine");
    }
    *outEngine = NULL;

    if (_activeAudioEngine != NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"Only one audio engine may exist at a time.");
    }

    AudioEngine* Engine = Memory_Allocate(sizeof(AudioEngine));
    Memory_Zero(Engine, sizeof(*Engine));
    Engine->_format.SampleRate = (float)AUDIO_ENGINE_OUTPUT_SAMPLE_RATE;
    Engine->_format.ChannelCount = (int32_t)AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT;
    atomic_store(&Engine->_operationQueue.WriteIndex, 0U);
    atomic_store(&Engine->_operationQueue.ReadIndex, 0U);
    atomic_store(&Engine->_bufferFillDurationNanoseconds, 0U);
    InitializeLazyBuffer(&Engine->_publicSoundRegistry, sizeof(TrackSoundSlot*));

    Error Result = InitializeTrack(Engine, &Engine->_masterTrack, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Engine);
        return Result;
    }

    if (!IsAudioDeviceReady())
    {
        InitAudioDevice();
        Engine->_ownsAudioDevice = true;
    }

    SetAudioStreamBufferSizeDefault(512);
    Engine->_stream = LoadAudioStream(AUDIO_ENGINE_OUTPUT_SAMPLE_RATE,
        AUDIO_ENGINE_OUTPUT_SAMPLE_SIZE_BITS,
        AUDIO_ENGINE_OUTPUT_CHANNEL_COUNT);
    SetAudioStreamCallback(Engine->_stream, AudioEngine_StreamCallback);
    PlayAudioStream(Engine->_stream);

    _activeAudioEngine = Engine;
    *outEngine = Engine;
    return Error_CreateSuccess();
}

Error AudioEngine_Deconstruct(AudioEngine* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    if (_activeAudioEngine == self)
    {
        _activeAudioEngine = NULL;
    }

    StopAudioStream(self->_stream);
    UnloadAudioStream(self->_stream);
    if (self->_ownsAudioDevice)
    {
        CloseAudioDevice();
    }

    for (size_t i = 0; i < self->_publicSoundRegistry._count; i++)
    {
        TrackSoundSlot* SoundSlot = *(TrackSoundSlot**)GenericBuffer_GetPointerToElement(&self->_publicSoundRegistry, i);
        Memory_Free(SoundSlot->_publicModifierBuffer._data);
        Memory_Free(SoundSlot->_activeModifierBuffer._data);
        Memory_Free(SoundSlot->Instance);
        Memory_Free(SoundSlot);
    }

    DeconstructTrack(&self->_masterTrack);
    Memory_Free(self->_publicSoundRegistry._data);
    Memory_Free(self);
    return Error_CreateSuccess();
}

AudioFormat AudioEngine_GetAudioFormat(AudioEngine* self)
{
    return self->_format;
}

AudioTrack* AudioEngine_GetMasterTrack(AudioEngine* self)
{
    return &self->_masterTrack;
}

double AudioEngine_GetBufferFillDurationSeconds(AudioEngine* self)
{
    return (double)atomic_load(&self->_bufferFillDurationNanoseconds) / 1000000000.0;
}

SampleProvider* AudioTrack_GetProperties(AudioTrack* self)
{
    return &self->_properties;
}

Error AudioTrack_CreateSubTrack(AudioTrack* self, AudioTrack** outSubTrack)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outSubTrack == NULL)
    {
        return CreateNullArgumentError(u8"outSubTrack");
    }

    AudioTrack* SubTrack = Memory_Allocate(sizeof(AudioTrack));
    Error Result = InitializeTrack(self->Engine, SubTrack, self);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(SubTrack);
        return Result;
    }

    if (!EnsureBufferAdditionalCapacity(&self->_publicSubTrackBuffer, AUDIO_ENGINE_INITIAL_SUBTRACK_CAPACITY, 1U))
    {
        DeconstructTrack(SubTrack);
        Memory_Free(SubTrack);
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to reserve public audio subtrack storage.");
    }
    if (!GenericBuffer_AddLast(&self->_publicSubTrackBuffer, &SubTrack))
    {
        DeconstructTrack(SubTrack);
        Memory_Free(SubTrack);
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to add public audio subtrack.");
    }

    if (IsAudioThreadForTrack(self))
    {
        Result = AttachSubTrack(self, SubTrack);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    else
    {
        AudioThreadOperation Operation;
        Memory_Zero(&Operation, sizeof(Operation));
        Operation.Type = AudioThreadOperationType_AttachSubTrack;
        Operation.Data.SubTrack.Parent = self;
        Operation.Data.SubTrack.Child = SubTrack;
        Result = QueueOperation(self->Engine, Operation);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    *outSubTrack = SubTrack;
    return Error_CreateSuccess();
}

Error AudioTrack_RemoveSubTrack(AudioTrack* self, AudioTrack* subTrackToRemove)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (subTrackToRemove == NULL)
    {
        return CreateNullArgumentError(u8"subTrackToRemove");
    }

    for (size_t i = 0; i < self->_publicSubTrackBuffer._count; i++)
    {
        AudioTrack* ExistingSubTrack = *(AudioTrack**)GenericBuffer_GetPointerToElement(&self->_publicSubTrackBuffer, i);
        if (ExistingSubTrack == subTrackToRemove)
        {
            GenericBuffer_RemoveAt(&self->_publicSubTrackBuffer, i);
            if (IsAudioThreadForTrack(self))
            {
                DetachSubTrack(self, subTrackToRemove);
                return Error_CreateSuccess();
            }

            AudioThreadOperation Operation;
            Memory_Zero(&Operation, sizeof(Operation));
            Operation.Type = AudioThreadOperationType_DetachSubTrack;
            Operation.Data.SubTrack.Parent = self;
            Operation.Data.SubTrack.Child = subTrackToRemove;
            return QueueOperation(self->Engine, Operation);
        }
    }

    return Error_Construct3(ErrorCode_InvalidOperation, u8"Subtrack is not present on this track.");
}

Error AudioTrack_GetSubTracks(AudioTrack* self, GenericBuffer* outTrackPointers)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outTrackPointers == NULL)
    {
        return CreateNullArgumentError(u8"outTrackPointers");
    }
    if (outTrackPointers->_elementSize != sizeof(AudioTrack*))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Output buffer element size must match AudioTrack*.");
    }

    for (size_t i = 0; i < self->_publicSubTrackBuffer._count; i++)
    {
        AudioTrack* ChildTrack = *(AudioTrack**)GenericBuffer_GetPointerToElement(&self->_publicSubTrackBuffer, i);
        if (!GenericBuffer_AddLast(outTrackPointers, &ChildTrack))
        {
            return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to append subtrack pointer to output buffer.");
        }
    }

    return Error_CreateSuccess();
}

size_t AudioTrack_GetSoundInstanceCount(AudioTrack* self)
{
    return atomic_load(&self->_publicSoundCount);
}

double AudioTrack_GetCurrentSecond(AudioTrack* self)
{
    return (double)atomic_load(&self->_currentFrameIndex) / (double)self->Engine->_format.SampleRate;
}

Error AudioTrack_CreateSoundInstance(AudioTrack* self,
    GameSound* sourceSound,
    SoundInstanceInitializer initializer,
    const UserData* initializerUserData,
    GameSoundInstance** outSoundInstance)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (sourceSound == NULL)
    {
        return CreateNullArgumentError(u8"sourceSound");
    }
    if (outSoundInstance == NULL)
    {
        return CreateNullArgumentError(u8"outSoundInstance");
    }

    TrackSoundSlot* SoundSlot = Memory_Allocate(sizeof(TrackSoundSlot));
    GameSoundInstance* SoundInstance = Memory_Allocate(sizeof(GameSoundInstance));
    Memory_Zero(SoundSlot, sizeof(*SoundSlot));
    Memory_Zero(SoundInstance, sizeof(*SoundInstance));
    InitializeLazyBuffer(&SoundSlot->_publicModifierBuffer, sizeof(ISoundModifier*));
    InitializeLazyBuffer(&SoundSlot->_activeModifierBuffer, sizeof(ISoundModifier*));

    SoundSlot->Instance = SoundInstance;
    SoundSlot->OwningTrack = self;
    atomic_store(&SoundSlot->_isAttached, false);
    atomic_store(&SoundSlot->_isPendingAttach, true);
    atomic_store(&SoundSlot->_isPendingRemove, false);

    SoundInstance->_source = sourceSound;
    SoundInstance->_state = SoundInstanceState_Playing;
    SoundInstance->_sampleIndex = 0.0;
    InitializeAutomatedDouble(&SoundInstance->_sampleSpeed, 1.0);
    InitializeSampleProvider(&SoundInstance->_sampleProperties,
        &SoundSlot->_publicModifierBuffer,
        self->Engine,
        SoundSlot,
        false,
        (double)sourceSound->_format.SampleRate,
        1.0f,
        SOUND_SAMPLE_PAN_MIDDLE);
    SoundInstance->_isLooped = false;
    Memory_Zero(&SoundInstance->_loopCommand, sizeof(AudioCommand));
    Memory_Zero(&SoundInstance->_endCommand, sizeof(AudioCommand));
    Memory_Zero(&SoundInstance->_tailEndCommand, sizeof(AudioCommand));

    if (initializer != NULL)
    {
        Error Result = initializer(SoundInstance, initializerUserData);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(SoundSlot->_publicModifierBuffer._data);
            Memory_Free(SoundSlot->_activeModifierBuffer._data);
            Memory_Free(SoundInstance);
            Memory_Free(SoundSlot);
            return Result;
        }
    }

    if (!EnsureBufferAdditionalCapacity(&self->Engine->_publicSoundRegistry, AUDIO_ENGINE_INITIAL_REGISTRY_CAPACITY, 1U))
    {
        Memory_Free(SoundSlot->_publicModifierBuffer._data);
        Memory_Free(SoundSlot->_activeModifierBuffer._data);
        Memory_Free(SoundInstance);
        Memory_Free(SoundSlot);
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to reserve sound registry storage.");
    }
    if (!GenericBuffer_AddLast(&self->Engine->_publicSoundRegistry, &SoundSlot))
    {
        Memory_Free(SoundSlot->_publicModifierBuffer._data);
        Memory_Free(SoundSlot->_activeModifierBuffer._data);
        Memory_Free(SoundInstance);
        Memory_Free(SoundSlot);
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to register sound instance.");
    }

    atomic_fetch_add(&self->_publicSoundCount, 1U);
    if (IsAudioThreadForTrack(self))
    {
        Error Result = AttachSoundSlot(self, SoundSlot);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    else
    {
        AudioThreadOperation Operation;
        Memory_Zero(&Operation, sizeof(Operation));
        Operation.Type = AudioThreadOperationType_AttachSound;
        Operation.Data.AttachSound.SoundSlot = SoundSlot;
        Error Result = QueueOperation(self->Engine, Operation);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    *outSoundInstance = SoundInstance;
    return Error_CreateSuccess();
}

Error AudioTrack_RemoveSoundInstance(AudioTrack* self, GameSoundInstance* soundInstance, bool* wasRemoved)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (soundInstance == NULL)
    {
        return CreateNullArgumentError(u8"soundInstance");
    }
    if (wasRemoved == NULL)
    {
        return CreateNullArgumentError(u8"wasRemoved");
    }

    TrackSoundSlot* SoundSlot = FindSoundSlot(self->Engine, soundInstance);
    if ((SoundSlot == NULL) || (SoundSlot->OwningTrack != self))
    {
        *wasRemoved = false;
        return Error_CreateSuccess();
    }

    *wasRemoved = true;
    atomic_store(&SoundSlot->_isPendingRemove, true);

    if (IsAudioThreadForTrack(self))
    {
        for (size_t i = 0; i < self->_activeSoundBuffer._count; i++)
        {
            TrackSoundSlot* ExistingSoundSlot = *(TrackSoundSlot**)GenericBuffer_GetPointerToElement(&self->_activeSoundBuffer, i);
            if (ExistingSoundSlot == SoundSlot)
            {
                RemoveSoundSlotAt(self, i);
                break;
            }
        }
        return Error_CreateSuccess();
    }

    AudioThreadOperation Operation;
    Memory_Zero(&Operation, sizeof(Operation));
    Operation.Type = AudioThreadOperationType_RemoveSound;
    Operation.Data.RemoveSound.Track = self;
    Operation.Data.RemoveSound.SoundInstance = soundInstance;
    return QueueOperation(self->Engine, Operation);
}

Error AudioTrack_ScheduleCommand(AudioTrack* self, double secondInTrackTimeline, AudioCommand* command)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (command == NULL)
    {
        return CreateNullArgumentError(u8"command");
    }
    if (!IsFiniteDouble(secondInTrackTimeline))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Command second must be finite.");
    }

    uint64_t CurrentFrameIndex = atomic_load(&self->_currentFrameIndex);
    uint64_t TargetFrameIndex;
    if (secondInTrackTimeline <= AudioTrack_GetCurrentSecond(self))
    {
        TargetFrameIndex = CurrentFrameIndex;
    }
    else
    {
        double TargetFrame = secondInTrackTimeline * (double)self->Engine->_format.SampleRate;
        if (TargetFrame < 0.0)
        {
            TargetFrame = 0.0;
        }
        TargetFrameIndex = (uint64_t)TargetFrame;
    }

    ScheduledAudioCommand ScheduledCommand;
    ScheduledCommand.TargetFrameIndex = TargetFrameIndex;
    ScheduledCommand.Command = *command;
    if (IsAudioThreadForTrack(self))
    {
        return InsertScheduledCommand(self, ScheduledCommand);
    }

    AudioThreadOperation Operation;
    Memory_Zero(&Operation, sizeof(Operation));
    Operation.Type = AudioThreadOperationType_ScheduleCommand;
    Operation.Data.ScheduleCommand.Track = self;
    Operation.Data.ScheduleCommand.ScheduledCommand = ScheduledCommand;
    return QueueOperation(self->Engine, Operation);
}

Error ReverbSoundModifier_Construct1(ReverbSoundModifier* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    self->_modifier._vtable.Self = self;
    self->_modifier._vtable._modify = ReverbSoundModifier_ModifyInternal;
    self->_modifier._vtable._resetState = ReverbSoundModifier_ResetStateInternal;
    InitializeAutomatedFloat(&self->DryVolume, 1.0f);
    InitializeAutomatedFloat(&self->WetVolume, 0.25f);
    InitializeAutomatedFloat(&self->Feedback, 0.35f);
    InitializeAutomatedDouble(&self->DelaySeconds, 0.12);
    InitializeAutomatedFloat(&self->Damping, 0.4f);
    return Error_CreateSuccess();
}

void ReverbSoundModifier_Deconstruct(ReverbSoundModifier* self)
{
    if (self == NULL)
    {
        return;
    }

    Memory_Free(self->_delayBuffer);
    self->_delayBuffer = NULL;
    self->_delayBufferFrameCount = 0U;
}

void ReverbSoundModifier_ResetState(ReverbSoundModifier* self)
{
    ReverbSoundModifier_ResetStateInternal(self);
}

Error BiQuadPassSoundModifier_Construct1(BiQuadPassSoundModifier* self, BiQuadPassTypeEnum passType)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    self->_modifier._vtable.Self = self;
    self->_modifier._vtable._modify = BiQuadPassSoundModifier_ModifyInternal;
    self->_modifier._vtable._resetState = BiQuadPassSoundModifier_ResetStateInternal;
    self->PassType = passType;
    InitializeAutomatedFloat(&self->WetVolume, 1.0f);
    InitializeAutomatedFloat(&self->CutoffFrequency, 1200.0f);
    InitializeAutomatedFloat(&self->Resonance, 0.707f);
    return Error_CreateSuccess();
}

void BiQuadPassSoundModifier_Deconstruct(BiQuadPassSoundModifier* self)
{
    UNUSED(self);
}

void BiQuadPassSoundModifier_ResetState(BiQuadPassSoundModifier* self)
{
    BiQuadPassSoundModifier_ResetStateInternal(self);
}

Error BitCrusherModifier_Construct1(BitCrusherModifier* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    self->_modifier._vtable.Self = self;
    self->_modifier._vtable._modify = BitCrusherModifier_ModifyInternal;
    self->_modifier._vtable._resetState = BitCrusherModifier_ResetStateInternal;
    InitializeAutomatedFloat(&self->WetVolume, 1.0f);
    InitializeAutomatedDouble(&self->HoldSeconds, 1.0 / 8000.0);
    InitializeAutomatedFloat(&self->BitDepth, 8.0f);
    return Error_CreateSuccess();
}

void BitCrusherModifier_Deconstruct(BitCrusherModifier* self)
{
    UNUSED(self);
}

void BitCrusherModifier_ResetState(BitCrusherModifier* self)
{
    BitCrusherModifier_ResetStateInternal(self);
}

Error AutomatedFloat_SetValue(SoundAutomatedFloat* value, float newTarget, double changeDurationSeconds)
{
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }
    if (!IsFiniteFloat(newTarget))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Automated float target must be finite.");
    }
    if (!IsFiniteDouble(changeDurationSeconds) || (changeDurationSeconds < 0.0))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Automated float duration must be finite and non-negative.");
    }

    value->_targetValue = newTarget;
    value->_time._changeDurationLeftSeconds = changeDurationSeconds;
    if (changeDurationSeconds == AUTOMATED_VALUE_DURATION_INSTANT)
    {
        value->_currentValue = newTarget;
    }

    return Error_CreateSuccess();
}

Error AutomatedDouble_SetValue(SoundAutomatedDouble* value, double newTarget, double changeDurationSeconds)
{
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }
    if (!IsFiniteDouble(newTarget))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Automated double target must be finite.");
    }
    if (!IsFiniteDouble(changeDurationSeconds) || (changeDurationSeconds < 0.0))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Automated double duration must be finite and non-negative.");
    }

    value->_targetValue = newTarget;
    value->_time._changeDurationLeftSeconds = changeDurationSeconds;
    if (changeDurationSeconds == AUTOMATED_VALUE_DURATION_INSTANT)
    {
        value->_currentValue = newTarget;
    }

    return Error_CreateSuccess();
}

Error SampleProvider_AddModifier(SampleProvider* self, ISoundModifier* modifier, size_t index)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (modifier == NULL)
    {
        return CreateNullArgumentError(u8"modifier");
    }
    if (_activeAudioEngine == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"No active audio engine exists.");
    }
    if (!SampleProvider_BelongsToActiveEngine(self))
    {
        return CreateInvalidProviderError();
    }

    AudioTrack* Track = SampleProvider_GetOwningTrack(self);
    if (Track != NULL)
    {
        Error Result = InsertModifier(self->_modifiers, modifier, index);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        if (IsAudioThreadForProviderEngine(_activeAudioEngine))
        {
            return InsertModifier(&Track->_activeModifierBuffer, modifier, index);
        }

        return QueueTrackModifierOperation(Track, AudioThreadOperationType_AddTrackModifier, modifier, index);
    }

    TrackSoundSlot* SoundSlot = SampleProvider_GetOwningSoundSlot(self);
    if (SoundSlot != NULL)
    {
        Error Result = InsertModifier(self->_modifiers, modifier, index);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        if (IsAudioThreadForProviderEngine(_activeAudioEngine))
        {
            return InsertModifier(&SoundSlot->_activeModifierBuffer, modifier, index);
        }

        return QueueSoundModifierOperation(_activeAudioEngine, SoundSlot, AudioThreadOperationType_AddSoundModifier, modifier, index);
    }

    return CreateInvalidProviderError();
}

Error SampleProvider_RemoveModifier(SampleProvider* self, ISoundModifier* modifier)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (modifier == NULL)
    {
        return CreateNullArgumentError(u8"modifier");
    }
    if (_activeAudioEngine == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"No active audio engine exists.");
    }
    if (!SampleProvider_BelongsToActiveEngine(self))
    {
        return CreateInvalidProviderError();
    }

    AudioTrack* Track = SampleProvider_GetOwningTrack(self);
    if (Track != NULL)
    {
        Error Result = RemoveModifier(self->_modifiers, modifier);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        if (IsAudioThreadForProviderEngine(_activeAudioEngine))
        {
            return RemoveModifier(&Track->_activeModifierBuffer, modifier);
        }

        return QueueTrackModifierOperation(Track, AudioThreadOperationType_RemoveTrackModifier, modifier, 0U);
    }

    TrackSoundSlot* SoundSlot = SampleProvider_GetOwningSoundSlot(self);
    if (SoundSlot != NULL)
    {
        Error Result = RemoveModifier(self->_modifiers, modifier);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        if (IsAudioThreadForProviderEngine(_activeAudioEngine))
        {
            return RemoveModifier(&SoundSlot->_activeModifierBuffer, modifier);
        }

        return QueueSoundModifierOperation(_activeAudioEngine, SoundSlot, AudioThreadOperationType_RemoveSoundModifier, modifier, 0U);
    }

    return CreateInvalidProviderError();
}

Error SampleProvider_ClearModifiers(SampleProvider* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (_activeAudioEngine == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"No active audio engine exists.");
    }
    if (!SampleProvider_BelongsToActiveEngine(self))
    {
        return CreateInvalidProviderError();
    }

    AudioTrack* Track = SampleProvider_GetOwningTrack(self);
    if (Track != NULL)
    {
        GenericBuffer_Clear(self->_modifiers);
        if (IsAudioThreadForProviderEngine(_activeAudioEngine))
        {
            GenericBuffer_Clear(&Track->_activeModifierBuffer);
            return Error_CreateSuccess();
        }

        return QueueTrackModifierOperation(Track, AudioThreadOperationType_ClearTrackModifiers, NULL, 0U);
    }

    TrackSoundSlot* SoundSlot = SampleProvider_GetOwningSoundSlot(self);
    if (SoundSlot != NULL)
    {
        GenericBuffer_Clear(self->_modifiers);
        if (IsAudioThreadForProviderEngine(_activeAudioEngine))
        {
            GenericBuffer_Clear(&SoundSlot->_activeModifierBuffer);
            return Error_CreateSuccess();
        }

        return QueueSoundModifierOperation(_activeAudioEngine, SoundSlot, AudioThreadOperationType_ClearSoundModifiers, NULL, 0U);
    }

    return CreateInvalidProviderError();
}

Error GameSound_Construct1(GameSound* self, float* samples, size_t sampleCount, AudioFormat format)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (samples == NULL)
    {
        return CreateNullArgumentError(u8"samples");
    }
    if ((format.ChannelCount <= 0) || (format.ChannelCount > MAX_AUDIO_CHANNELS))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Audio format channel count must be between 1 and %d.", MAX_AUDIO_CHANNELS);
    }
    if (!IsFiniteFloat(format.SampleRate) || (format.SampleRate <= 0.0f))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Audio format sample rate must be positive and finite.");
    }
    if ((sampleCount == 0U) || ((sampleCount % (size_t)format.ChannelCount) != 0U))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Sample count must be non-zero and divisible by the channel count.");
    }

    self->_samples = samples;
    self->_sampleCount = sampleCount;
    self->_format = format;
    return Error_CreateSuccess();
}

Error GameSound_Deconstruct(GameSound* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    self->_samples = NULL;
    self->_sampleCount = 0U;
    self->_format.SampleRate = 0.0f;
    self->_format.ChannelCount = 0;
    return Error_CreateSuccess();
}

Error GameSoundInstance_SetSampleIndex(GameSoundInstance* self, double sampleIndex)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!IsFiniteDouble(sampleIndex) || (sampleIndex < 0.0))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Sample index must be finite and non-negative.");
    }

    self->_sampleIndex = sampleIndex;
    return Error_CreateSuccess();
}

Error GameSoundInstance_SetSampleSecond(GameSoundInstance* self, double second)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!IsFiniteDouble(second) || (second < 0.0))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Sample second must be finite and non-negative.");
    }

    self->_sampleIndex = second * (double)self->_source->_format.SampleRate;
    return Error_CreateSuccess();
}
