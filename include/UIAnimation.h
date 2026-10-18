#pragma once
#include <stdbool.h>
#include <stddef.h>
// Color and Vector2 (animatable value kinds) come from raylib; RenderColor (a tint value kind) comes from the
// renderer. All three are part of a property value, so these includes are unavoidable here.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRError.h"


/**
 * @file UIAnimation.h
 * @brief Typed widget property values, and keyframed interpolation of them: easing methods, keyframes, validation
 *        and evaluation.
 *
 * Widget properties are typed: every property has a UIPropertyType (declared by the widget type, see
 * WidgetPropertyDescriptor), and values travel as a UIPropertyValue, a type tag plus the data, so a value of the wrong
 * kind is rejected instead of being reinterpreted.
 *
 * A UI animation is a list of keyframes over one typed property. Each keyframe carries a time (seconds from the
 * animation's start), a value, and the interpolation used for the segment that STARTS at it (so a keyframe
 * describes how it eases toward the next; the final keyframe's method is unused). The screen owns the running
 * animations; each tick it evaluates them and writes the value through the widget's property setter.
 *
 * This module is pure data and math: it holds no animation state and never allocates.
 */


// Types.
/**
 * @brief A caller-supplied easing curve for UIInterpolation_Custom.
 *
 * Maps a normalized segment progress to an eased progress. It should map 0 to 0 and 1 to 1; the evaluator clamps its
 * result to [0;1]. Must be a pure function with no side effects.
 * @param t The linear segment progress, in [0;1].
 * @returns The eased progress, normally in [0;1].
 */
typedef float (*UIEasingFunction)(float t);

/**
 * @brief The interpolation method applied across a keyframe segment.
 *
 * "In" variants accelerate (slow start, fast end); "Out" variants decelerate (fast start, slow end). The higher the
 * power, the sharper the curve.
 */
typedef enum UIInterpolationEnum
{
    /** @brief Hold the segment's start value until the next keyframe, then jump (a step function). */
    UIInterpolation_Constant,
    /** @brief Straight-line interpolation from start to end. */
    UIInterpolation_Linear,

    /** @brief Accelerating ease using t^2 (slow start). */
    UIInterpolation_ExpInPow2,
    /** @brief Accelerating ease using t^3. */
    UIInterpolation_ExpInPow3,
    /** @brief Accelerating ease using t^4. */
    UIInterpolation_ExpInPow4,
    /** @brief Accelerating ease using t^5 (sharpest accelerate). */
    UIInterpolation_ExpInPow5,

    /** @brief Decelerating ease using 1-(1-t)^2 (slow end). */
    UIInterpolation_ExpOutPow2,
    /** @brief Decelerating ease using 1-(1-t)^3. */
    UIInterpolation_ExpOutPow3,
    /** @brief Decelerating ease using 1-(1-t)^4. */
    UIInterpolation_ExpOutPow4,
    /** @brief Decelerating ease using 1-(1-t)^5 (sharpest decelerate). */
    UIInterpolation_ExpOutPow5,

    /** @brief Sine ease-in-out: slow at the start, fastest in the middle, slow at the end. */
    UIInterpolation_SineInOut,

    /** @brief Use the keyframe's CustomEasing function pointer. */
    UIInterpolation_Custom
} UIInterpolation;

/**
 * @brief The kind of a widget property's value, which selects the active member of UIPropertyData and how
 *        animations blend it (per float, per vector component, per color channel).
 */
typedef enum UIPropertyTypeEnum
{
    /** @brief A single float. */
    UIPropertyType_Float,
    /** @brief A Vector2 (x, y). */
    UIPropertyType_Vector2,
    /** @brief A raylib Color (r, g, b, a channels, each blended and rounded). */
    UIPropertyType_Color,
    /** @brief A RenderColor (tint channels + brightness + opacity). */
    UIPropertyType_RenderColor
} UIPropertyType;

/**
 * @brief The data of a property value; the active member is selected by a UIPropertyType kept next to it.
 */
typedef union UIPropertyDataUnion
{
    /** @brief The value when the type is UIPropertyType_Float. */
    float FloatValue;
    /** @brief The value when the type is UIPropertyType_Vector2. */
    Vector2 Vector2Value;
    /** @brief The value when the type is UIPropertyType_Color. */
    Color ColorValue;
    /** @brief The value when the type is UIPropertyType_RenderColor. */
    RenderColor RenderColorValue;
} UIPropertyData;

/**
 * @brief A typed property value: the type tag and the data it selects. A plain value type; build one with the
 *        UIPropertyValue_From* helpers.
 */
typedef struct UIPropertyValueStruct
{
    /** @brief The kind of value; selects the active member of Data. */
    UIPropertyType Type;
    /** @brief The value. */
    UIPropertyData Data;
} UIPropertyValue;

/**
 * @brief A single animation keyframe: a time, a value, and the easing toward the next keyframe.
 *
 * Value's active member is selected by the animation's UIPropertyType. Keyframes within one animation must be ordered
 * by non-decreasing Time. The Interpolation (and CustomEasing when it is UIInterpolation_Custom) applies to the segment
 * from this keyframe to the next; on the last keyframe it is ignored.
 */
typedef struct UIKeyframeStruct
{
    /** @brief Time of this keyframe in seconds from the animation's start; finite, >= 0 and non-decreasing. */
    double Time;
    /** @brief Interpolation for the segment starting at this keyframe (unused on the final keyframe). */
    UIInterpolation Interpolation;
    /** @brief Easing function used when Interpolation is UIInterpolation_Custom (then it must not be NULL); ignored
     *  otherwise. */
    UIEasingFunction CustomEasing;
    /** @brief The keyframe value; the active member is selected by the animation's UIPropertyType. */
    UIPropertyData Value;
} UIKeyframe;

/**
 * @brief Options controlling how a running animation behaves once it reaches its end.
 *
 * Build with UIAnimationOptions_CreateDefault and override fields as needed.
 */
typedef struct UIAnimationOptionsStruct
{
    /** @brief When true, the animation restarts from time 0 after the last keyframe instead of ending. */
    bool IsLooping;
    /** @brief When true (and not looping), the animation removes itself once it finishes. */
    bool IsRemovedOnFinish;
} UIAnimationOptions;


// Functions.
/**
 * @brief Returns default animation options: not looping, removed once finished.
 * @returns Options with IsLooping = false and IsRemovedOnFinish = true.
 */
static inline UIAnimationOptions UIAnimationOptions_CreateDefault(void)
{
    return (UIAnimationOptions)
    {
        .IsLooping = false,
        .IsRemovedOnFinish = true
    };
}

/**
 * @brief Makes a float property value.
 * @param value The value.
 * @returns The typed value.
 */
static inline UIPropertyValue UIPropertyValue_FromFloat(float value)
{
    return (UIPropertyValue){ .Type = UIPropertyType_Float, .Data.FloatValue = value };
}

/**
 * @brief Makes a Vector2 property value.
 * @param value The value.
 * @returns The typed value.
 */
static inline UIPropertyValue UIPropertyValue_FromVector2(Vector2 value)
{
    return (UIPropertyValue){ .Type = UIPropertyType_Vector2, .Data.Vector2Value = value };
}

/**
 * @brief Makes a Color property value.
 * @param value The value.
 * @returns The typed value.
 */
static inline UIPropertyValue UIPropertyValue_FromColor(Color value)
{
    return (UIPropertyValue){ .Type = UIPropertyType_Color, .Data.ColorValue = value };
}

/**
 * @brief Makes a RenderColor property value.
 * @param value The value.
 * @returns The typed value.
 */
static inline UIPropertyValue UIPropertyValue_FromRenderColor(RenderColor value)
{
    return (UIPropertyValue){ .Type = UIPropertyType_RenderColor, .Data.RenderColorValue = value };
}

/**
 * @brief Returns whether @p type is one of the UIPropertyType values.
 * @param type The type.
 * @returns true if valid.
 */
bool UIPropertyType_IsValid(UIPropertyType type);

/**
 * @brief Returns whether every float in a value is finite (vector components, a render color's brightness and
 *        opacity; Colors are always finite). A value with an invalid type is not finite.
 * @param value The value; must not be NULL.
 * @returns true if finite.
 */
bool UIPropertyValue_IsFinite(const UIPropertyValue* value);

/**
 * @brief Evaluates an easing method at a normalized progress.
 *
 * Clamps @p t to [0;1], applies the selected curve (using @p custom for UIInterpolation_Custom), and clamps the result
 * to [0;1]. UIInterpolation_Constant returns 0 so a segment holds its start value.
 * @param method The interpolation method.
 * @param custom The custom easing function; used only when @p method is UIInterpolation_Custom, and treated as linear
 *        when it is NULL.
 * @param t The linear segment progress.
 * @returns The eased progress in [0;1].
 */
float UIInterpolation_Ease(UIInterpolation method, UIEasingFunction custom, float t);

/**
 * @brief Checks that keyframes form a valid animation of a property type.
 *
 * Valid means: at least one keyframe; every time finite, >= 0 and not earlier than the previous one; every
 * interpolation a UIInterpolation value, with a custom easing function where it is UIInterpolation_Custom; every value
 * finite for the type.
 * @param keyframes The keyframes; must not be NULL.
 * @param keyframeCount The number of keyframes.
 * @param type The property type the values are read as.
 * @returns Success; ErrorCode_IllegalArgument if @p keyframes is NULL, @p keyframeCount is 0 or a custom easing
 *          function is missing; ErrorCode_ArgumentOutOfRange for an invalid type, time, interpolation or value. The
 *          message names the keyframe.
 */
Error UIAnimation_ValidateKeyframes(const UIKeyframe* keyframes, size_t keyframeCount, UIPropertyType type);

/**
 * @brief Computes the value of a keyframed animation at a given time.
 *
 * Locates the segment containing @p time and blends the two surrounding keyframes using the left keyframe's easing.
 * Before the first keyframe the first value is produced; at or after the last keyframe the last value is produced.
 * @param keyframes The keyframes, ordered by non-decreasing Time; must not be NULL.
 * @param keyframeCount The number of keyframes; must be at least 1.
 * @param type The value kind, selecting the active member and the blend rule.
 * @param time The evaluation time, in seconds from the animation's start.
 * @param outValue [out] Receives the typed value; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p keyframes or @p outValue is NULL or @p keyframeCount is 0;
 *          ErrorCode_ArgumentOutOfRange if @p type is invalid.
 */
Error UIAnimation_EvaluateKeyframes(const UIKeyframe* keyframes,
    size_t keyframeCount,
    UIPropertyType type,
    double time,
    UIPropertyValue* outValue);
