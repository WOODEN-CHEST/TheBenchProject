#include "UIAnimation.h"
#include <inttypes.h>
#include <stdint.h>
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** Half-turn in radians, used by the sine ease. Defined locally to avoid depending on a raylib macro. */
static const float ANIMATION_PI = 3.14159265358979323846f;


// Static functions.
static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static bool IsInterpolationValid(UIInterpolation method)
{
    switch (method)
    {
        case UIInterpolation_Constant:
        case UIInterpolation_Linear:
        case UIInterpolation_ExpInPow2:
        case UIInterpolation_ExpInPow3:
        case UIInterpolation_ExpInPow4:
        case UIInterpolation_ExpInPow5:
        case UIInterpolation_ExpOutPow2:
        case UIInterpolation_ExpOutPow3:
        case UIInterpolation_ExpOutPow4:
        case UIInterpolation_ExpOutPow5:
        case UIInterpolation_SineInOut:
        case UIInterpolation_Custom:
            return true;
    }
    return false;
}

/* Blends two 8-bit color channels in floating point and rounds to the nearest byte. */
static unsigned char LerpChannel(unsigned char a, unsigned char b, float t)
{
    float Result = Math_LerpFloat((float)a, (float)b, t);
    Result = Math_ClampFloat(Result, 0.0f, 255.0f);
    return (unsigned char)(Result + 0.5f);
}

/* Blends two keyframes' values componentwise by the eased amount @p t. */
static UIPropertyData InterpolateValue(UIPropertyType type, const UIPropertyData* a, const UIPropertyData* b, float t)
{
    UIPropertyData Value;
    Memory_Zero(&Value, sizeof(Value));
    switch (type)
    {
        case UIPropertyType_Float:
            Value.FloatValue = Math_LerpFloat(a->FloatValue, b->FloatValue, t);
            break;
        case UIPropertyType_Vector2:
            Value.Vector2Value.x = Math_LerpFloat(a->Vector2Value.x, b->Vector2Value.x, t);
            Value.Vector2Value.y = Math_LerpFloat(a->Vector2Value.y, b->Vector2Value.y, t);
            break;
        case UIPropertyType_Color:
            Value.ColorValue.r = LerpChannel(a->ColorValue.r, b->ColorValue.r, t);
            Value.ColorValue.g = LerpChannel(a->ColorValue.g, b->ColorValue.g, t);
            Value.ColorValue.b = LerpChannel(a->ColorValue.b, b->ColorValue.b, t);
            Value.ColorValue.a = LerpChannel(a->ColorValue.a, b->ColorValue.a, t);
            break;
        case UIPropertyType_RenderColor:
            Value.RenderColorValue.Tint.r = LerpChannel(a->RenderColorValue.Tint.r, b->RenderColorValue.Tint.r, t);
            Value.RenderColorValue.Tint.g = LerpChannel(a->RenderColorValue.Tint.g, b->RenderColorValue.Tint.g, t);
            Value.RenderColorValue.Tint.b = LerpChannel(a->RenderColorValue.Tint.b, b->RenderColorValue.Tint.b, t);
            Value.RenderColorValue.Tint.a = LerpChannel(a->RenderColorValue.Tint.a, b->RenderColorValue.Tint.a, t);
            Value.RenderColorValue.Brightness = Math_LerpFloat(a->RenderColorValue.Brightness, b->RenderColorValue.Brightness, t);
            Value.RenderColorValue.Opacity = Math_LerpFloat(a->RenderColorValue.Opacity, b->RenderColorValue.Opacity, t);
            break;
    }
    return Value;
}


// Public functions.
bool UIPropertyType_IsValid(UIPropertyType type)
{
    switch (type)
    {
        case UIPropertyType_Float:
        case UIPropertyType_Vector2:
        case UIPropertyType_Color:
        case UIPropertyType_RenderColor:
            return true;
    }
    return false;
}

bool UIPropertyValue_IsFinite(const UIPropertyValue* value)
{
    switch (value->Type)
    {
        case UIPropertyType_Float:
            return IsFloatFinite(value->Data.FloatValue);
        case UIPropertyType_Vector2:
            return IsFloatFinite(value->Data.Vector2Value.x) && IsFloatFinite(value->Data.Vector2Value.y);
        case UIPropertyType_Color:
            return true;
        case UIPropertyType_RenderColor:
            return IsFloatFinite(value->Data.RenderColorValue.Brightness) && IsFloatFinite(value->Data.RenderColorValue.Opacity);
    }
    return false;
}

float UIInterpolation_Ease(UIInterpolation method, UIEasingFunction custom, float t)
{
    float Progress = Math_ClampFloat(t, 0.0f, 1.0f);
    float Result = Progress;

    switch (method)
    {
        case UIInterpolation_Constant:   Result = 0.0f; break;
        case UIInterpolation_Linear:     Result = Progress; break;

        case UIInterpolation_ExpInPow2:  Result = Math_PowFloat(Progress, 2.0f); break;
        case UIInterpolation_ExpInPow3:  Result = Math_PowFloat(Progress, 3.0f); break;
        case UIInterpolation_ExpInPow4:  Result = Math_PowFloat(Progress, 4.0f); break;
        case UIInterpolation_ExpInPow5:  Result = Math_PowFloat(Progress, 5.0f); break;

        case UIInterpolation_ExpOutPow2: Result = 1.0f - Math_PowFloat(1.0f - Progress, 2.0f); break;
        case UIInterpolation_ExpOutPow3: Result = 1.0f - Math_PowFloat(1.0f - Progress, 3.0f); break;
        case UIInterpolation_ExpOutPow4: Result = 1.0f - Math_PowFloat(1.0f - Progress, 4.0f); break;
        case UIInterpolation_ExpOutPow5: Result = 1.0f - Math_PowFloat(1.0f - Progress, 5.0f); break;

        case UIInterpolation_SineInOut:  Result = 0.5f * (1.0f - Math_CosFloat(ANIMATION_PI * Progress)); break;

        case UIInterpolation_Custom:     Result = (custom != NULL) ? custom(Progress) : Progress; break;
    }
    // A corrupted method value keeps the linear progress; a custom curve's NaN becomes the end of the segment.
    if (Math_IsNaNFloat(Result))
    {
        Result = 1.0f;
    }
    return Math_ClampFloat(Result, 0.0f, 1.0f);
}

Error UIAnimation_ValidateKeyframes(const UIKeyframe* keyframes, size_t keyframeCount, UIPropertyType type)
{
    if ((keyframes == NULL) || (keyframeCount == 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UI animation: at least one keyframe is required.");
    }
    if (!UIPropertyType_IsValid(type))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UI animation: unknown property type.");
    }

    for (size_t Index = 0; Index < keyframeCount; Index++)
    {
        const UIKeyframe* Keyframe = &keyframes[Index];
        if (Math_IsNaNDouble(Keyframe->Time) || Math_IsInfinityDouble(Keyframe->Time) || (Keyframe->Time < 0.0))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "UI animation: keyframe %" PRIu64
                " has a time that is not a finite number >= 0.", (uint64_t)Index);
        }
        if ((Index > 0U) && (Keyframe->Time < keyframes[Index - 1U].Time))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "UI animation: keyframe %" PRIu64
                " is earlier than the keyframe before it.", (uint64_t)Index);
        }
        if (!IsInterpolationValid(Keyframe->Interpolation))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "UI animation: keyframe %" PRIu64
                " has an unknown interpolation.", (uint64_t)Index);
        }
        if ((Keyframe->Interpolation == UIInterpolation_Custom) && (Keyframe->CustomEasing == NULL))
        {
            return Error_Construct4(ErrorCode_IllegalArgument, "UI animation: keyframe %" PRIu64
                " uses a custom easing without a function.", (uint64_t)Index);
        }
        UIPropertyValue Value = { .Type = type, .Data = Keyframe->Value };
        if (!UIPropertyValue_IsFinite(&Value))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "UI animation: keyframe %" PRIu64
                " has a value that is not finite.", (uint64_t)Index);
        }
    }
    return Error_CreateSuccess();
}

Error UIAnimation_EvaluateKeyframes(const UIKeyframe* keyframes,
    size_t keyframeCount,
    UIPropertyType type,
    double time,
    UIPropertyValue* outValue)
{
    if ((keyframes == NULL) || (outValue == NULL) || (keyframeCount == 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIAnimation_EvaluateKeyframes: keyframes and outValue must be non-NULL and keyframeCount > 0.");
    }
    if (!UIPropertyType_IsValid(type))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIAnimation_EvaluateKeyframes: unknown property type.");
    }

    outValue->Type = type;
    // Clamp to the endpoints outside the keyframe time span.
    if (time <= keyframes[0].Time)
    {
        outValue->Data = keyframes[0].Value;
        return Error_CreateSuccess();
    }
    if (time >= keyframes[keyframeCount - 1U].Time)
    {
        outValue->Data = keyframes[keyframeCount - 1U].Value;
        return Error_CreateSuccess();
    }

    // Locate the segment [i, i+1] containing the time (guaranteed to exist by the checks above).
    size_t SegmentIndex = 0;
    while (((SegmentIndex + 1U) < keyframeCount) && (time >= keyframes[SegmentIndex + 1U].Time))
    {
        SegmentIndex++;
    }

    const UIKeyframe* Start = &keyframes[SegmentIndex];
    const UIKeyframe* End = &keyframes[SegmentIndex + 1U];
    double Span = End->Time - Start->Time;
    float LocalProgress = (Span > 0.0) ? (float)((time - Start->Time) / Span) : 1.0f;
    float Eased = UIInterpolation_Ease(Start->Interpolation, Start->CustomEasing, LocalProgress);

    outValue->Data = InterpolateValue(type, &Start->Value, &End->Value, Eased);
    return Error_CreateSuccess();
}
