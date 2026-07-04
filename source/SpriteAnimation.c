#include "SpriteAnimation.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRMath.h"


// Types.


// Fields.


// Static functions.
static Error CreateNullError(const unsigned char* parameterName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Parameter \"%s\" cannot be null",
        parameterName);
}

static Error CreateOutOfRangeError(const unsigned char* parameterName,
    const unsigned char* details)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Parameter \"%s\" is out of range: %s",
        parameterName,
        details);
}

static bool IsFiniteDouble(double value)
{
    return !Math_IsInfinityDouble(value);
}

static size_t GetFrameCountFromAnimation(const SpriteAnimation* self)
{
    if ((self == NULL) || (self->_frames == NULL))
    {
        return 0;
    }

    return self->_frames->_count;
}

static size_t GetFrameCountFromInstance(const SpriteAnimationInstance* self)
{
    if (self == NULL)
    {
        return 0;
    }

    return GetFrameCountFromAnimation(self->_source);
}

static Error GetFrameAt(SpriteAnimation* self, size_t frameIndex, SpriteAnimationFrame* outFrame)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }
    if (outFrame == NULL)
    {
        return CreateNullError(u8"outFrame");
    }

    Memory_Zero(outFrame, sizeof(*outFrame));

    const size_t FrameCount = GetFrameCountFromAnimation(self);
    if (frameIndex >= FrameCount)
    {
        return Error_Construct3(ErrorCode_IndexOutOfBounds,
            u8"Frame index %zu is outside the valid range [0, %zu).",
            frameIndex,
            FrameCount);
    }

    SpriteAnimationFrame* Frame = GenericBuffer_GetPointerToElement(self->_frames, frameIndex);
    if (Frame == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidState,
            u8"No sprite animation frame found at index %zu (internal error).",
            frameIndex);
    }

    *outFrame = *Frame;
    return Error_CreateSuccess();
}

static SpriteAnimationStateDirection GetDirectionFromFrameStep(int64_t frameStep)
{
    if (frameStep > 0)
    {
        return SpriteAnimationStateDirection_Forwards;
    }
    if (frameStep < 0)
    {
        return SpriteAnimationStateDirection_Backwards;
    }

    return SpriteAnimationStateDirection_None;
}

static uint64_t GetFrameStepMagnitude(int64_t frameStep)
{
    if (frameStep >= 0)
    {
        return (uint64_t)frameStep;
    }

    return (uint64_t)(-frameStep);
}

static size_t AddModulo(size_t a, size_t b, size_t modulo)
{
    if (a >= (modulo - b))
    {
        return a - (modulo - b);
    }

    return a + b;
}

static size_t MultiplyModulo(size_t a, size_t b, size_t modulo)
{
    size_t Result = 0;
    size_t Left = a % modulo;
    size_t Right = b;

    while (Right > 0)
    {
        if ((Right & ((size_t)1)) != 0)
        {
            Result = AddModulo(Result, Left, modulo);
        }

        Right >>= 1;
        if (Right > 0)
        {
            Left = AddModulo(Left, Left, modulo);
        }
    }

    return Result;
}

static size_t GetLastFrameIndex(size_t frameCount)
{
    if (frameCount == 0)
    {
        return 0;
    }

    return frameCount - 1;
}

static size_t GetChangesUntilForwardBoundary(size_t frameIndex,
    uint64_t frameStepMagnitude,
    size_t frameCount)
{
    const size_t LastFrameIndex = GetLastFrameIndex(frameCount);
    const size_t RemainingFrames = LastFrameIndex - frameIndex;
    return (RemainingFrames / (size_t)frameStepMagnitude) + 1;
}

static size_t GetChangesUntilBackwardBoundary(size_t frameIndex,
    uint64_t frameStepMagnitude)
{
    return (frameIndex / (size_t)frameStepMagnitude) + 1;
}

static Error RaiseStateReachEvent(SpriteAnimationInstance* self,
    SpriteAnimationReachedState reachedState,
    SpriteAnimationStateDirection direction)
{
    SpriteAnimationStateReachEventArgs EventArgs;
    Memory_Zero(&EventArgs, sizeof(EventArgs));
    EventArgs._animationInstance = self;
    EventArgs._reachedState = reachedState;

    if (reachedState == SpriteAnimationReachedState_End)
    {
        EventArgs._args._endArgs._direction = direction;
    }
    else if (reachedState == SpriteAnimationReachedState_Loop)
    {
        EventArgs._args._loopArgs._direction = direction;
    }

    return WREvent_Raise(&self->_stateReachEvent, &EventArgs);
}

static Error ValidateFPS(double fps)
{
    if (!IsFiniteDouble(fps))
    {
        return CreateOutOfRangeError(u8"fps", u8"value must be finite");
    }
    if ((fps < SPRITE_ANIMATION_FPS_MIN) || (fps > SPRITE_ANIMATION_FPS_MAX))
    {
        return CreateOutOfRangeError(u8"fps",
            u8"value is outside the supported sprite animation FPS range");
    }

    return Error_CreateSuccess();
}

static Error ValidateFrameStep(int64_t frameStep)
{
    if ((frameStep < SPRITE_ANIMATION_FRAME_STEP_MIN) || (frameStep > SPRITE_ANIMATION_FRAME_STEP_MAX))
    {
        return CreateOutOfRangeError(u8"frameStep",
            u8"value is outside the supported sprite animation frame step range");
    }

    return Error_CreateSuccess();
}

static Error ValidateSeconds(const unsigned char* parameterName, double seconds)
{
    if (!IsFiniteDouble(seconds))
    {
        return CreateOutOfRangeError(parameterName, u8"value must be finite");
    }
    if (seconds < 0.0)
    {
        return CreateOutOfRangeError(parameterName, u8"value cannot be negative");
    }

    return Error_CreateSuccess();
}

static Error ValidateFrameIndex(SpriteAnimationInstance* self, size_t frameIndex)
{
    const size_t FrameCount = GetFrameCountFromInstance(self);
    if (FrameCount == 0)
    {
        if (frameIndex != 0)
        {
            return CreateOutOfRangeError(u8"frameIndex",
                u8"value must be 0 when the animation has no frames");
        }

        return Error_CreateSuccess();
    }

    if (frameIndex >= FrameCount)
    {
        return Error_Construct3(ErrorCode_IndexOutOfBounds,
            u8"Frame index %zu is outside the valid range [0, %zu).",
            frameIndex,
            FrameCount);
    }

    return Error_CreateSuccess();
}

static void CopyAnimationState(SpriteAnimationInstance* source,
    SpriteAnimationInstance* destination)
{
    destination->_frameIndex = source->_frameIndex;
    destination->_timeSinceFrameChangeSeconds = source->_timeSinceFrameChangeSeconds;
    destination->_fps = source->_fps;
    destination->_frameStep = source->_frameStep;
    destination->_isRunning = source->_isRunning;
    destination->_isLooped = source->_isLooped;
}

static void ApplyLoopedFrameAdvance(SpriteAnimationInstance* self,
    long double ConsumedFrameChanges,
    SpriteAnimationStateDirection direction,
    uint64_t frameStepMagnitude,
    size_t frameCount)
{
    const size_t StepMagnitudeModulo = (size_t)(frameStepMagnitude % frameCount);
    const size_t ConsumedChangesModulo = (size_t)fmodl(ConsumedFrameChanges, (long double)frameCount);
    const size_t DeltaModulo = MultiplyModulo(StepMagnitudeModulo, ConsumedChangesModulo, frameCount);

    if (direction == SpriteAnimationStateDirection_Forwards)
    {
        self->_frameIndex = AddModulo(self->_frameIndex % frameCount, DeltaModulo, frameCount);
        return;
    }
    if (direction == SpriteAnimationStateDirection_Backwards)
    {
        if (DeltaModulo == 0)
        {
            return;
        }

        self->_frameIndex = AddModulo(self->_frameIndex % frameCount,
            frameCount - DeltaModulo,
            frameCount);
    }
}


// Public functions.
Error SpriteAnimation_Construct1(SpriteAnimation* self, GenericBuffer* frames)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }
    if (frames == NULL)
    {
        return CreateNullError(u8"frames");
    }
    if (frames->_elementSize != sizeof(SpriteAnimationFrame))
    {
        return Error_Construct3(ErrorCode_IllegalArgument,
            u8"Parameter \"frames\" must contain elements of size %zu, but got %zu.",
            sizeof(SpriteAnimationFrame),
            frames->_elementSize);
    }

    Memory_Zero(self, sizeof(*self));
    self->_frames = frames;
    self->_defaultFPS = SPRITE_ANIMATION_FPS_DEFAULT;
    self->_defaultFrameStep = SPRITE_ANIMATION_FRAME_STEP_DEFAULT;
    self->_defaultIsRunning = SPRITE_ANIMATION_IS_RUNNING_DEFAULT;
    self->_defaultIsLooped = SPRITE_ANIMATION_IS_LOOPED_DEFAULT;

    return Error_CreateSuccess();
}

Error SpriteAnimation_Construct2(SpriteAnimation* self,
    GenericBuffer* frames,
    double defaultFPS,
    int64_t defaultFrameStep,
    bool defaultIsRunning,
    bool defaultIsLooped)
{
    Error Result = SpriteAnimation_Construct1(self, frames);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!isfinite(defaultFPS) || (defaultFPS < SPRITE_ANIMATION_FPS_MIN) || (defaultFPS > SPRITE_ANIMATION_FPS_MAX))
    {
        return Error_Construct3(ErrorCode_ArgumentOutOfRange,
            u8"Default FPS must be finite and within [%g, %g].", SPRITE_ANIMATION_FPS_MIN, SPRITE_ANIMATION_FPS_MAX);
    }
    if ((defaultFrameStep < SPRITE_ANIMATION_FRAME_STEP_MIN) || (defaultFrameStep > SPRITE_ANIMATION_FRAME_STEP_MAX))
    {
        return Error_Construct3(ErrorCode_ArgumentOutOfRange,
            u8"Default frame step must be within [%d, %d].", SPRITE_ANIMATION_FRAME_STEP_MIN, SPRITE_ANIMATION_FRAME_STEP_MAX);
    }

    self->_defaultFPS = defaultFPS;
    self->_defaultFrameStep = defaultFrameStep;
    self->_defaultIsRunning = defaultIsRunning;
    self->_defaultIsLooped = defaultIsLooped;
    return Error_CreateSuccess();
}

Error SpriteAnimation_Deconstruct(SpriteAnimation* self)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));

    return Error_CreateSuccess();
}

size_t SpriteAnimation_GetFrameCount(SpriteAnimation* self)
{
    return GetFrameCountFromAnimation(self);
}

Error SpriteAnimation_GetFrameAt(SpriteAnimation* self, size_t frameIndex, SpriteAnimationFrame* outFrame)
{
    return GetFrameAt(self, frameIndex, outFrame);
}

Error SpriteAnimation_CreateInstance(SpriteAnimation* self, SpriteAnimationInstance* outInstance)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }
    if (outInstance == NULL)
    {
        return CreateNullError(u8"outInstance");
    }

    Error Result = SpriteAnimationInstance_Construct1(outInstance);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    outInstance->_source = self;
    Result = SpriteAnimationInstance_ResetProperties(outInstance);
    if (Result.Code != ErrorCode_Success)
    {
        SpriteAnimationInstance_Deconstruct(outInstance);
        return Result;
    }

    return Error_CreateSuccess();
}

size_t SpriteAnimationInstance_GetFrameCount(SpriteAnimationInstance* self)
{
    return GetFrameCountFromInstance(self);
}

Error SpriteAnimationInstance_GetFrameAt(SpriteAnimationInstance* self,
    size_t frameIndex,
    SpriteAnimationFrame* outFrame)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }
    if (self->_source == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation,
            u8"Sprite animation instance has no source animation.");
    }

    return GetFrameAt(self->_source, frameIndex, outFrame);
}

Error SpriteAnimationInstance_GetCurrentFrame(SpriteAnimationInstance* self, SpriteAnimationFrame* outFrame)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    return SpriteAnimationInstance_GetFrameAt(self, self->_frameIndex, outFrame);
}

Error SpriteAnimationInstance_Construct1(SpriteAnimationInstance* self)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));

    Error Result = WREvent_Construct1(&self->_stateReachEvent);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Zero(self, sizeof(*self));
        return Result;
    }

    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_Deconstruct(SpriteAnimationInstance* self)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    WREvent_Deconstruct(&self->_stateReachEvent);
    Memory_Zero(self, sizeof(*self));

    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_CopyAnimationStateTo(SpriteAnimationInstance* self,
    SpriteAnimationInstance* destination)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }
    if (destination == NULL)
    {
        return CreateNullError(u8"destination");
    }
    if (self == destination)
    {
        return Error_CreateSuccess();
    }

    CopyAnimationState(self, destination);
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_CopyEntireStateTo(SpriteAnimationInstance* self,
    SpriteAnimationInstance* destination)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }
    if (destination == NULL)
    {
        return CreateNullError(u8"destination");
    }
    if (self == destination)
    {
        return Error_CreateSuccess();
    }

    destination->_source = self->_source;
    CopyAnimationState(self, destination);
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_SetFPS(SpriteAnimationInstance* self, double fps)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Error Result = ValidateFPS(fps);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_fps = fps;
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_SetFrameIndex(SpriteAnimationInstance* self, size_t frameIndex)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Error Result = ValidateFrameIndex(self, frameIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_frameIndex = frameIndex;
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_SetFrameStep(SpriteAnimationInstance* self, int64_t frameStep)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Error Result = ValidateFrameStep(frameStep);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_frameStep = frameStep;
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_SetSecondsSinceFrameChange(SpriteAnimationInstance* self, double seconds)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Error Result = ValidateSeconds(u8"seconds", seconds);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_timeSinceFrameChangeSeconds = seconds;
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_SetIsRunning(SpriteAnimationInstance* self, bool value)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    self->_isRunning = value;
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_SetIsLooped(SpriteAnimationInstance* self, bool value)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    self->_isLooped = value;
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_ResetProperties(SpriteAnimationInstance* self)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }
    if (self->_source == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation,
            u8"Sprite animation instance has no source animation to reset from.");
    }

    self->_fps = self->_source->_defaultFPS;
    self->_frameStep = self->_source->_defaultFrameStep;
    self->_isRunning = self->_source->_defaultIsRunning;
    self->_isLooped = self->_source->_defaultIsLooped;

    return SpriteAnimationInstance_ResetAnimation(self);
}

Error SpriteAnimationInstance_ResetAnimation(SpriteAnimationInstance* self)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    const size_t FrameCount = GetFrameCountFromInstance(self);
    self->_timeSinceFrameChangeSeconds = 0.0;

    if ((self->_frameStep < 0) && (FrameCount > 0))
    {
        self->_frameIndex = GetLastFrameIndex(FrameCount);
        return Error_CreateSuccess();
    }

    self->_frameIndex = 0;
    return Error_CreateSuccess();
}

Error SpriteAnimationInstance_Update(SpriteAnimationInstance* self, double elapsedSeconds)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Error Result = ValidateSeconds(u8"elapsedSeconds", elapsedSeconds);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    const size_t FrameCount = GetFrameCountFromInstance(self);
    if ((self->_source == NULL)
        || (FrameCount == 0)
        || !self->_isRunning
        || (self->_fps == 0.0)
        || (self->_frameStep == 0))
    {
        return Error_CreateSuccess();
    }

    const long double FrameDuration = 1.0L / (long double)self->_fps;
    const long double TotalTime = (long double)self->_timeSinceFrameChangeSeconds + (long double)elapsedSeconds;
    const long double ConsumedFrameChanges = floorl(TotalTime / FrameDuration);
    if (ConsumedFrameChanges < 1.0L)
    {
        self->_timeSinceFrameChangeSeconds = (double)TotalTime;
        return Error_CreateSuccess();
    }

    const long double RemainingTime = fmodl(TotalTime, FrameDuration);
    const SpriteAnimationStateDirection Direction = GetDirectionFromFrameStep(self->_frameStep);
    const uint64_t FrameStepMagnitude = GetFrameStepMagnitude(self->_frameStep);
    const size_t ChangesUntilBoundary = (Direction == SpriteAnimationStateDirection_Forwards)
        ? GetChangesUntilForwardBoundary(self->_frameIndex, FrameStepMagnitude, FrameCount)
        : GetChangesUntilBackwardBoundary(self->_frameIndex, FrameStepMagnitude);
    const bool ReachedBoundary = (ConsumedFrameChanges >= (long double)ChangesUntilBoundary);

    if (!self->_isLooped)
    {
        if (ReachedBoundary)
        {
            self->_frameIndex = (Direction == SpriteAnimationStateDirection_Forwards)
                ? GetLastFrameIndex(FrameCount)
                : 0;
            self->_timeSinceFrameChangeSeconds = 0.0;
            self->_isRunning = false;
            return RaiseStateReachEvent(self, SpriteAnimationReachedState_End, Direction);
        }

        const size_t ConsumedFrameChangesCount = (size_t)ConsumedFrameChanges;
        const size_t FrameDelta = (size_t)(ConsumedFrameChangesCount * (size_t)FrameStepMagnitude);
        self->_frameIndex = (Direction == SpriteAnimationStateDirection_Forwards)
            ? (self->_frameIndex + FrameDelta)
            : (self->_frameIndex - FrameDelta);
        self->_timeSinceFrameChangeSeconds = (double)RemainingTime;
        return Error_CreateSuccess();
    }

    ApplyLoopedFrameAdvance(self, ConsumedFrameChanges, Direction, FrameStepMagnitude, FrameCount);
    self->_timeSinceFrameChangeSeconds = (double)RemainingTime;

    if (ReachedBoundary)
    {
        return RaiseStateReachEvent(self, SpriteAnimationReachedState_Loop, Direction);
    }

    return Error_CreateSuccess();
}
