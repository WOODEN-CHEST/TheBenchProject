#include "UIScreen.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "UIAnimation.h"
#include "Logger.h"
#include <inttypes.h>
#include "wr/WRBufferPool.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Types.
/* One running animation of a widget property. */
typedef struct AnimationRecordStruct
{
    uint64_t Id;
    uint64_t WidgetId;
    int32_t PropertyId;
    UIPropertyType Type;
    UIAnimationOptions Options;
    double Elapsed;
    /* Finished, stopped or failed: removed at the end of the advance (or at once outside it). */
    bool IsPendingRemoval;
    /* The keyframes (UIKeyframe), borrowed from the screen's keyframe pool. */
    GenericBuffer* Keyframes;
} AnimationRecord;


// Static functions.
static AnimationRecord* GetAnimationAt(UIScreen* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_animations, index);
}

/* Wraps a looping animation's elapsed time into [0, cycleLength). */
static double WrapLoopTime(double elapsed, double cycleLength)
{
    if (cycleLength <= 0.0)
    {
        return 0.0;
    }
    return elapsed - (Math_FloorDouble(elapsed / cycleLength) * cycleLength);
}

/* Removes the animations marked for removal, returning their keyframe storage. */
static void RemovePendingAnimations(UIScreen* self)
{
    for (size_t Index = self->_animations._count; Index-- > 0U;)
    {
        AnimationRecord* Record = GetAnimationAt(self, Index);
        if (Record->IsPendingRemoval)
        {
            Error ReturnResult = BufferPool_Return(&self->_keyframeBuffers, Record->Keyframes);
            Error_Deconstruct(&ReturnResult); // A pool buffer always goes back.
            GenericBuffer_RemoveAt(&self->_animations, Index);
        }
    }
}

/* Marks an animation for removal, and removes it at once unless the animations are being advanced (whose loop must not
 * see the list shift under it). */
static void StopRecord(UIScreen* self, AnimationRecord* record)
{
    record->IsPendingRemoval = true;
    if (!self->_isAdvancingAnimations)
    {
        RemovePendingAnimations(self);
    }
}

/* A failed animation is stopped, not fatal: it is logged once, now. */
static void LogFailedAnimation(UIScreen* self, const AnimationRecord* record, Error* failure)
{
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"UI animation %" PRIu64 " (widget %" PRIu64 ", property %" PRId32 ") stopped: %s",
            record->Id, record->WidgetId, record->PropertyId,
            (failure->Message != NULL) ? (const char*)failure->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(failure);
}


// Internal functions (declared in UIInternal.h).
Error UIScreen_InternalInitializeAnimations(UIScreen* self)
{
    Error Result = BufferPool_Construct1(&self->_keyframeBuffers);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GenericBuffer_AllocateVariable(&self->_animations, 4U, sizeof(AnimationRecord));
    self->_nextAnimationId = 1U;
    return Error_CreateSuccess();
}

Error UIScreen_InternalAdvanceAnimations(UIScreen* self, ProgramTime time)
{
    self->_isAdvancingAnimations = true;
    // Animations started during the pass begin next tick.
    size_t Count = self->_animations._count;
    for (size_t Index = 0; Index < Count; Index++)
    {
        AnimationRecord* Record = GetAnimationAt(self, Index);
        Widget* Target = UIScreen_InternalResolve(self, Record->WidgetId);
        if (Record->IsPendingRemoval || (Target == NULL))
        {
            Record->IsPendingRemoval = true;
            continue;
        }

        Record->Elapsed += time.PassedTime;
        const UIKeyframe* Keyframes = GenericBuffer_GetPointerToElement(Record->Keyframes, 0U);
        size_t KeyframeCount = Record->Keyframes->_count;
        double LastTime = Keyframes[KeyframeCount - 1U].Time;
        double EvaluationTime = Record->Options.IsLooping ? WrapLoopTime(Record->Elapsed, LastTime) : Record->Elapsed;

        UIPropertyValue Value;
        Error Result = UIAnimation_EvaluateKeyframes(Keyframes, KeyframeCount, Record->Type, EvaluationTime, &Value);
        if (Result.Code == ErrorCode_Success)
        {
            // The setter can run widget code that starts animations: the list may move, so the record is fetched again.
            Result = Widget_SetProperty(Target, Record->PropertyId, &Value);
        }
        Record = GetAnimationAt(self, Index);
        if (Result.Code != ErrorCode_Success)
        {
            LogFailedAnimation(self, Record, &Result);
            Record->IsPendingRemoval = true;
            continue;
        }
        if (!Record->Options.IsLooping && Record->Options.IsRemovedOnFinish && (Record->Elapsed >= LastTime))
        {
            Record->IsPendingRemoval = true;
        }
    }
    self->_isAdvancingAnimations = false;
    RemovePendingAnimations(self);
    return Error_CreateSuccess();
}

void UIScreen_InternalStopDyingAnimations(UIScreen* self)
{
    for (size_t Index = 0; Index < self->_animations._count; Index++)
    {
        AnimationRecord* Record = GetAnimationAt(self, Index);
        if (UIScreen_InternalResolve(self, Record->WidgetId) == NULL)
        {
            Record->IsPendingRemoval = true;
        }
    }
    if (!self->_isAdvancingAnimations)
    {
        RemovePendingAnimations(self);
    }
}

Error UIScreen_InternalReleaseAnimations(UIScreen* self)
{
    for (size_t Index = 0; Index < self->_animations._count; Index++)
    {
        Error ReturnResult = BufferPool_Return(&self->_keyframeBuffers, GetAnimationAt(self, Index)->Keyframes);
        Error_Deconstruct(&ReturnResult);
    }
    Memory_Free(self->_animations._data);
    Memory_Zero(&self->_animations, sizeof(self->_animations));
    return BufferPool_Deconstruct(&self->_keyframeBuffers);
}


// Public functions.
Error UIScreen_StartAnimation(UIScreen* self,
    uint64_t widgetId,
    int32_t propertyId,
    UIPropertyType type,
    const UIKeyframe* keyframes,
    size_t keyframeCount,
    UIAnimationOptions options,
    uint64_t* outAnimationId)
{
    if ((self == NULL) || (keyframes == NULL) || (outAnimationId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIScreen_StartAnimation: self, keyframes and outAnimationId must not be NULL.");
    }
    Widget* Target = UIScreen_InternalResolve(self, widgetId);
    if (Target == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_StartAnimation: the widget is not alive.");
    }
    UIPropertyType PropertyType = UIPropertyType_Float;
    if (!Widget_TryGetPropertyType(Target, propertyId, &PropertyType))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "UIScreen_StartAnimation: the widget has no property %" PRId32 ".",
            propertyId);
    }
    if (PropertyType != type)
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "UIScreen_StartAnimation: property %" PRId32
            " has another value type than the animation.", propertyId);
    }
    Error Result = UIAnimation_ValidateKeyframes(keyframes, keyframeCount, type);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    GenericBuffer* Storage = NULL;
    Result = BufferPool_Borrow(&self->_keyframeBuffers, sizeof(UIKeyframe), &Storage);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsStored = true;
    for (size_t Index = 0; IsStored && (Index < keyframeCount); Index++)
    {
        UIKeyframe Keyframe = keyframes[Index];
        IsStored = GenericBuffer_AddLast(Storage, &Keyframe);
    }
    AnimationRecord Record =
    {
        .Id = self->_nextAnimationId,
        .WidgetId = widgetId,
        .PropertyId = propertyId,
        .Type = type,
        .Options = options,
        .Elapsed = 0.0,
        .IsPendingRemoval = false,
        .Keyframes = Storage
    };
    if (!IsStored || !GenericBuffer_AddLast(&self->_animations, &Record))
    {
        Error ReturnResult = BufferPool_Return(&self->_keyframeBuffers, Storage);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_StartAnimation: animation storage could not grow.");
    }
    *outAnimationId = Record.Id;
    self->_nextAnimationId++;
    return Error_CreateSuccess();
}

Error UIScreen_StopAnimation(UIScreen* self, uint64_t animationId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_StopAnimation: self must not be NULL.");
    }
    for (size_t Index = 0; Index < self->_animations._count; Index++)
    {
        AnimationRecord* Record = GetAnimationAt(self, Index);
        if ((Record->Id == animationId) && !Record->IsPendingRemoval)
        {
            StopRecord(self, Record);
            return Error_CreateSuccess();
        }
    }
    return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_StopAnimation: no animation with that id is running.");
}

Error UIScreen_StopWidgetAnimations(UIScreen* self, uint64_t widgetId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_StopWidgetAnimations: self must not be NULL.");
    }
    for (size_t Index = 0; Index < self->_animations._count; Index++)
    {
        AnimationRecord* Record = GetAnimationAt(self, Index);
        if (Record->WidgetId == widgetId)
        {
            Record->IsPendingRemoval = true;
        }
    }
    if (!self->_isAdvancingAnimations)
    {
        RemovePendingAnimations(self);
    }
    return Error_CreateSuccess();
}

bool UIScreen_IsAnimationRunning(const UIScreen* self, uint64_t animationId)
{
    for (size_t Index = 0; Index < self->_animations._count; Index++)
    {
        const AnimationRecord* Record = GenericBuffer_GetPointerToElement((GenericBuffer*)&self->_animations, Index);
        if (Record->Id == animationId)
        {
            return !Record->IsPendingRemoval;
        }
    }
    return false;
}
