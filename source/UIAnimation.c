#include "UIAnimation.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** Half-turn in radians, used by the sine ease. Defined locally to avoid depending on a raylib macro. */
static const float ANIMATION_PI = 3.14159265358979323846f;


// Static functions.
/* Blends two 8-bit color channels in floating point and rounds to the nearest byte. */
static unsigned char LerpChannel(unsigned char a, unsigned char b, float t)
{
    float Result = Math_LerpFloat((float)a, (float)b, t);
    Result = Math_ClampFloat(Result, 0.0f, 255.0f);
    return (unsigned char)(Result + 0.5f);
}

/* Copies a keyframe's active value (the first @p size bytes of its value union) into @p out. All union
 * members share the union's starting address, so this copies whichever member the property type selects. */
static void CopyKeyframeValue(const UIKeyframe* keyframe, size_t size, void* out)
{
    Memory_Copy(&keyframe->Value, out, size);
}

/* Blends two keyframes' values componentwise by the eased amount @p t and writes the result to @p out. */
static void InterpolateValue(UIPropertyType type, const UIKeyframe* a, const UIKeyframe* b, float t, void* out)
{
    switch (type)
    {
        case UIPropertyType_Float:
        {
            float Value = Math_LerpFloat(a->Value.FloatValue, b->Value.FloatValue, t);
            Memory_Copy(&Value, out, sizeof(float));
            break;
        }
        case UIPropertyType_Vector2:
        {
            Vector2 Value;
            Value.x = Math_LerpFloat(a->Value.Vector2Value.x, b->Value.Vector2Value.x, t);
            Value.y = Math_LerpFloat(a->Value.Vector2Value.y, b->Value.Vector2Value.y, t);
            Memory_Copy(&Value, out, sizeof(Vector2));
            break;
        }
        case UIPropertyType_Color:
        {
            Color Value;
            Value.r = LerpChannel(a->Value.ColorValue.r, b->Value.ColorValue.r, t);
            Value.g = LerpChannel(a->Value.ColorValue.g, b->Value.ColorValue.g, t);
            Value.b = LerpChannel(a->Value.ColorValue.b, b->Value.ColorValue.b, t);
            Value.a = LerpChannel(a->Value.ColorValue.a, b->Value.ColorValue.a, t);
            Memory_Copy(&Value, out, sizeof(Color));
            break;
        }
        case UIPropertyType_RenderColor:
        {
            RenderColor Value;
            Value.Tint.r = LerpChannel(a->Value.RenderColorValue.Tint.r, b->Value.RenderColorValue.Tint.r, t);
            Value.Tint.g = LerpChannel(a->Value.RenderColorValue.Tint.g, b->Value.RenderColorValue.Tint.g, t);
            Value.Tint.b = LerpChannel(a->Value.RenderColorValue.Tint.b, b->Value.RenderColorValue.Tint.b, t);
            Value.Tint.a = LerpChannel(a->Value.RenderColorValue.Tint.a, b->Value.RenderColorValue.Tint.a, t);
            Value.Brightness = Math_LerpFloat(a->Value.RenderColorValue.Brightness, b->Value.RenderColorValue.Brightness, t);
            Value.Opacity = Math_LerpFloat(a->Value.RenderColorValue.Opacity, b->Value.RenderColorValue.Opacity, t);
            Memory_Copy(&Value, out, sizeof(RenderColor));
            break;
        }
    }
}


// Public functions.
size_t UIPropertyType_GetValueSize(UIPropertyType type)
{
    switch (type)
    {
        case UIPropertyType_Float:       return sizeof(float);
        case UIPropertyType_Vector2:     return sizeof(Vector2);
        case UIPropertyType_Color:       return sizeof(Color);
        case UIPropertyType_RenderColor: return sizeof(RenderColor);
        default:                         return 0;
    }
}

float UIInterpolation_Ease(UIInterpolation method, UIEasingFunction custom, float t)
{
    float Progress = Math_ClampFloat(t, 0.0f, 1.0f);
    float Result;

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

        default:                         Result = Progress; break;
    }

    return Math_ClampFloat(Result, 0.0f, 1.0f);
}

Error UIAnimation_EvaluateKeyframes(const UIKeyframe* keyframes,
    size_t keyframeCount,
    UIPropertyType type,
    double time,
    void* outValue)
{
    if ((keyframes == NULL) || (outValue == NULL) || (keyframeCount == 0))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIAnimation_EvaluateKeyframes: keyframes and outValue must be non-NULL and keyframeCount > 0.");
    }

    size_t ValueSize = UIPropertyType_GetValueSize(type);
    if (ValueSize == 0)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIAnimation_EvaluateKeyframes: unrecognized property type.");
    }

    // Clamp to the endpoints outside the keyframe time span.
    if (time <= keyframes[0].Time)
    {
        CopyKeyframeValue(&keyframes[0], ValueSize, outValue);
        return Error_CreateSuccess();
    }
    if (time >= keyframes[keyframeCount - 1].Time)
    {
        CopyKeyframeValue(&keyframes[keyframeCount - 1], ValueSize, outValue);
        return Error_CreateSuccess();
    }

    // Locate the segment [i, i+1] containing the time (guaranteed to exist by the checks above).
    size_t SegmentIndex = 0;
    while ((SegmentIndex + 1 < keyframeCount) && (time >= keyframes[SegmentIndex + 1].Time))
    {
        SegmentIndex++;
    }

    const UIKeyframe* Start = &keyframes[SegmentIndex];
    const UIKeyframe* End = &keyframes[SegmentIndex + 1];
    double Span = End->Time - Start->Time;
    float LocalProgress = (Span > 0.0) ? (float)((time - Start->Time) / Span) : 1.0f;
    float Eased = UIInterpolation_Ease(Start->Interpolation, Start->CustomEasing, LocalProgress);

    InterpolateValue(type, Start, End, Eased, outValue);
    return Error_CreateSuccess();
}
