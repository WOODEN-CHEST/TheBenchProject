#pragma once
#include <stdbool.h>
#include <stddef.h>
// Color and Vector2 (animatable value kinds) come from raylib; RenderColor (a tint value kind) comes
// from the renderer. All three are part of a keyframe's value, so these includes are unavoidable here.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRError.h"


/**
 * @file UIAnimation.h
 * @brief Keyframed interpolation of widget properties: the easing methods, a keyframe, and evaluation.
 *
 * A UI animation is a list of keyframes over a single typed property (a float, a Vector2, a Color or a
 * RenderColor). Each keyframe carries a time (seconds from the animation's start), a value, and the
 * interpolation method used for the segment that STARTS at it (so a keyframe describes how it eases
 * toward the next; the final keyframe's method is unused). The screen owns the running animations and,
 * each tick, calls UIAnimation_EvaluateKeyframes to obtain the current value and writes it back through
 * the target widget's generic property setter.
 *
 * This module is pure data + math: it holds no animation state and performs no allocation. Construction
 * and lifetime of running animations belong to the screen (see UIScreen_StartAnimation).
 */


// Types.
/**
 * @brief A caller-supplied easing curve for UIInterpolation_Custom.
 *
 * Maps a normalized segment progress to an eased progress. It should map 0 to 0 and 1 to 1 and stay
 * within [0;1] for inputs in [0;1] (the evaluator clamps the result to [0;1] defensively), but is free
 * to overshoot the ends conceptually if desired. Must be a pure function with no side effects.
 * @param t The linear segment progress, in [0;1].
 * @returns The eased progress, normally in [0;1].
 */
typedef float (*UIEasingFunction)(float t);

/**
 * @brief The interpolation method applied across a keyframe segment.
 *
 * "In" variants accelerate (slow start, fast end); "Out" variants decelerate (fast start, slow end).
 * The higher the power, the sharper the curve.
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
 * @brief The kind (and thus size) of the value an animation drives.
 *
 * The property type selects both how many bytes UIAnimation_EvaluateKeyframes writes to its output and
 * how components are blended (per float / per vector component / per color channel). It must match the
 * value size the target widget's generic property setter expects for the animated property id.
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
 * @brief A single animation keyframe: a time, a value, and the easing toward the next keyframe.
 *
 * Value is a union; the active member is chosen by the animation's UIPropertyType. Keyframes within one
 * animation must be ordered by non-decreasing Time. The Interpolation (and CustomEasing when it is
 * UIInterpolation_Custom) applies to the segment from this keyframe to the next; on the last keyframe it
 * is ignored.
 */
typedef struct UIKeyframeStruct
{
    /** @brief Time of this keyframe, in seconds from the animation's start; keyframes are ordered by this. */
    double Time;
    /** @brief Interpolation for the segment starting at this keyframe (unused on the final keyframe). */
    UIInterpolation Interpolation;
    /** @brief Easing function used when Interpolation is UIInterpolation_Custom; ignored otherwise. May be NULL only if unused. */
    UIEasingFunction CustomEasing;
    /** @brief The keyframe value; the active union member is selected by the animation's UIPropertyType. */
    union
    {
        /** @brief Value when the property type is UIPropertyType_Float. */
        float FloatValue;
        /** @brief Value when the property type is UIPropertyType_Vector2. */
        Vector2 Vector2Value;
        /** @brief Value when the property type is UIPropertyType_Color. */
        Color ColorValue;
        /** @brief Value when the property type is UIPropertyType_RenderColor. */
        RenderColor RenderColorValue;
    } Value;
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
 * @brief Returns the size in bytes of the value produced by the given property type.
 * @param type The property type.
 * @returns sizeof(float), sizeof(Vector2), sizeof(Color) or sizeof(RenderColor) for the respective type;
 *          0 for an unrecognized type.
 */
size_t UIPropertyType_GetValueSize(UIPropertyType type);

/**
 * @brief Evaluates an easing method at a normalized progress.
 *
 * Clamps @p t to [0;1], applies the selected curve (using @p custom for UIInterpolation_Custom), and
 * clamps the result to [0;1]. UIInterpolation_Constant returns 0 so a segment holds its start value.
 * @param method The interpolation method.
 * @param custom The custom easing function; used only when @p method is UIInterpolation_Custom, and
 *        treated as linear when it is NULL.
 * @param t The linear segment progress.
 * @returns The eased progress in [0;1].
 */
float UIInterpolation_Ease(UIInterpolation method, UIEasingFunction custom, float t);

/**
 * @brief Computes the value of a keyframed animation at a given time.
 *
 * Locates the segment containing @p time and blends the two surrounding keyframes using the left
 * keyframe's easing, writing the result to @p outValue. Before the first keyframe the first value is
 * produced; at or after the last keyframe the last value is produced. The number of bytes written is
 * UIPropertyType_GetValueSize(@p type); @p outValue must be at least that large and correctly aligned.
 * @param keyframes The keyframes, ordered by non-decreasing Time; must not be NULL.
 * @param keyframeCount The number of keyframes; must be at least 1.
 * @param type The value kind, selecting the active union member and the blend rule.
 * @param time The evaluation time, in seconds from the animation's start.
 * @param outValue [out] Receives the evaluated value; must not be NULL and must be at least
 *        UIPropertyType_GetValueSize(@p type) bytes.
 * @returns Success; ErrorCode_IllegalArgument if @p keyframes or @p outValue is NULL or @p keyframeCount
 *          is 0; ErrorCode_ArgumentOutOfRange if @p type is unrecognized.
 */
Error UIAnimation_EvaluateKeyframes(const UIKeyframe* keyframes,
    size_t keyframeCount,
    UIPropertyType type,
    double time,
    void* outValue);
