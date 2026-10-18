#include "TestSuites.h"
#include "UIAnimation.h"
#include <math.h>
#include <string.h>


// Static functions.
static bool IsNear(float value, float expected)
{
    return fabsf(value - expected) < 1e-5f;
}

static float ReturnNaN(float t)
{
    (void)t;
    return NAN;
}


// Tests.
static void EasingCurvesHitTheirEnds(TestRun* run)
{
    for (int32_t Method = (int32_t)UIInterpolation_Linear; Method <= (int32_t)UIInterpolation_SineInOut; Method++)
    {
        TEST_CHECK(run, IsNear(UIInterpolation_Ease((UIInterpolation)Method, NULL, 0.0f), 0.0f));
        TEST_CHECK(run, IsNear(UIInterpolation_Ease((UIInterpolation)Method, NULL, 1.0f), 1.0f));
        float Middle = UIInterpolation_Ease((UIInterpolation)Method, NULL, 0.5f);
        TEST_CHECK(run, (Middle >= 0.0f) && (Middle <= 1.0f));
    }
    TEST_CHECK(run, IsNear(UIInterpolation_Ease(UIInterpolation_Constant, NULL, 0.7f), 0.0f));
    TEST_CHECK(run, IsNear(UIInterpolation_Ease(UIInterpolation_ExpInPow2, NULL, 0.5f), 0.25f));
    TEST_CHECK(run, IsNear(UIInterpolation_Ease(UIInterpolation_ExpOutPow2, NULL, 0.5f), 0.75f));
    TEST_CHECK(run, IsNear(UIInterpolation_Ease(UIInterpolation_Linear, NULL, 2.0f), 1.0f));
    // A custom curve's NaN cannot leak into a property.
    TEST_CHECK(run, IsNear(UIInterpolation_Ease(UIInterpolation_Custom, ReturnNaN, 0.5f), 1.0f));
}

static void EvaluationBlendsTypedValues(TestRun* run)
{
    UIKeyframe Tints[] =
    {
        { .Time = 1.0, .Interpolation = UIInterpolation_Linear,
          .Value.RenderColorValue = { .Tint = { 0, 0, 0, 255 }, .Brightness = 0.0f, .Opacity = 1.0f } },
        { .Time = 3.0, .Interpolation = UIInterpolation_Linear,
          .Value.RenderColorValue = { .Tint = { 200, 100, 50, 255 }, .Brightness = 1.0f, .Opacity = 0.0f } },
    };
    UIPropertyValue Value;
    TEST_CHECK_SUCCESS(run, UIAnimation_EvaluateKeyframes(Tints, 2U, UIPropertyType_RenderColor, 2.0, &Value));
    TEST_CHECK(run, (Value.Type == UIPropertyType_RenderColor) && (Value.Data.RenderColorValue.Tint.r == 100)
        && (Value.Data.RenderColorValue.Tint.g == 50) && IsNear(Value.Data.RenderColorValue.Opacity, 0.5f));
    TEST_CHECK_SUCCESS(run, UIAnimation_EvaluateKeyframes(Tints, 2U, UIPropertyType_RenderColor, 0.0, &Value));
    TEST_CHECK(run, IsNear(Value.Data.RenderColorValue.Brightness, 0.0f)); // Before the first keyframe: its value.
    TEST_CHECK_SUCCESS(run, UIAnimation_EvaluateKeyframes(Tints, 2U, UIPropertyType_RenderColor, 9.0, &Value));
    TEST_CHECK(run, IsNear(Value.Data.RenderColorValue.Brightness, 1.0f)); // After the last: its value.

    UIKeyframe Step[] =
    {
        { .Time = 0.0, .Interpolation = UIInterpolation_Constant, .Value.FloatValue = 1.0f },
        { .Time = 1.0, .Interpolation = UIInterpolation_Constant, .Value.FloatValue = 5.0f },
    };
    TEST_CHECK_SUCCESS(run, UIAnimation_EvaluateKeyframes(Step, 2U, UIPropertyType_Float, 0.99, &Value));
    TEST_CHECK(run, IsNear(Value.Data.FloatValue, 1.0f));
    TEST_CHECK_ERROR(run, UIAnimation_EvaluateKeyframes(Step, 2U, (UIPropertyType)99, 0.5, &Value), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIAnimation_EvaluateKeyframes(Step, 0U, UIPropertyType_Float, 0.5, &Value), ErrorCode_IllegalArgument);
}

static void ValidationNamesTheBadKeyframe(TestRun* run)
{
    UIKeyframe Keyframes[] =
    {
        { .Time = 0.0, .Interpolation = UIInterpolation_Linear, .Value.FloatValue = 0.0f },
        { .Time = 1.0, .Interpolation = UIInterpolation_Linear, .Value.FloatValue = INFINITY },
    };
    Error Result = UIAnimation_ValidateKeyframes(Keyframes, 2U, UIPropertyType_Float);
    TEST_CHECK(run, (Result.Message != NULL) && (strstr((const char*)Result.Message, "keyframe 1") != NULL));
    TEST_CHECK_ERROR(run, Result, ErrorCode_ArgumentOutOfRange);

    Keyframes[1].Value.FloatValue = 1.0f;
    TEST_CHECK_SUCCESS(run, UIAnimation_ValidateKeyframes(Keyframes, 2U, UIPropertyType_Float));
    Keyframes[0].Time = -1.0;
    TEST_CHECK_ERROR(run, UIAnimation_ValidateKeyframes(Keyframes, 2U, UIPropertyType_Float), ErrorCode_ArgumentOutOfRange);
    Keyframes[0].Time = 0.0;
    Keyframes[0].Interpolation = (UIInterpolation)77;
    TEST_CHECK_ERROR(run, UIAnimation_ValidateKeyframes(Keyframes, 2U, UIPropertyType_Float), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIAnimation_ValidateKeyframes(Keyframes, 0U, UIPropertyType_Float), ErrorCode_IllegalArgument);

    // A Color has no float to break, a RenderColor checks its brightness and opacity.
    UIKeyframe Colors[] =
    {
        { .Time = 0.0, .Interpolation = UIInterpolation_Linear, .Value.RenderColorValue = { .Tint = { 1, 2, 3, 4 }, .Brightness = NAN, .Opacity = 1.0f } },
    };
    TEST_CHECK_SUCCESS(run, UIAnimation_ValidateKeyframes(Colors, 1U, UIPropertyType_Color));
    TEST_CHECK_ERROR(run, UIAnimation_ValidateKeyframes(Colors, 1U, UIPropertyType_RenderColor), ErrorCode_ArgumentOutOfRange);
}


// Public functions.
void UIAnimationTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "easing_curves_hit_their_ends", EasingCurvesHitTheirEnds },
        { "evaluation_blends_typed_values", EvaluationBlendsTypedValues },
        { "validation_names_the_bad_keyframe", ValidationNamesTheBadKeyframe },
    };
    TestRun_RunSuite(run, "UIAnimation", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
