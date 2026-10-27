#include "TestSuites.h"
#include "EyeAdaptation.h"
#include "NightVision.h"
#include <math.h>
#include <stdio.h>


// Macros.
/** The game's noon and moonless night (night vision's anchors), as floats. */
#define TEST_NOON_LUMINANCE ((float)NIGHT_VISION_NOON_LUMINANCE)
#define TEST_NIGHT_LUMINANCE ((float)NIGHT_VISION_NIGHT_LUMINANCE)


// Static functions.
/* Fills the meter's blocks with one value. */
static void FillBlocks(float* blocks, float value)
{
    for (int Index = 0; Index < EYE_ADAPTATION_METER_BLOCK_COUNT; Index++)
    {
        blocks[Index] = value;
    }
}

/* The block at a column and a row. */
static float* BlockAt(float* blocks, int column, int row)
{
    return &blocks[row * EYE_ADAPTATION_METER_WIDTH + column];
}

/* The measurement of blocks that are 0 but one, which is 1: that block's share of the weights. */
static float ShareOf(float* blocks, int column, int row, float aspect)
{
    FillBlocks(blocks, 0.0f);
    *BlockAt(blocks, column, row) = 1.0f;
    return EyeAdaptation_Measure(blocks, aspect);
}

/* An eye already set by a measurement at @p level. */
static EyeAdaptation MeasuredEye(float level)
{
    EyeAdaptation Eye;
    EyeAdaptation_Construct(&Eye);
    EyeAdaptation_Update(&Eye, NAN, level, 0.0f);
    return Eye;
}


// Tests.
/* The meter's mean leans on the middle of the view: blocks of one value measure that value; one block's share is its
 * Gaussian weight over the rest (at 16:9 a corner about a sixtieth of the middle's, the middle of the top edge about two
 * fifths, of the left edge about a twentieth); the weights are symmetric, so the rows' order does not matter; a block that
 * is not finite is left out, and none finite measures NaN; an unusable aspect counts as 16:9. */
static void MeterWeighsTheMiddle(TestRun* run)
{
    static float Blocks[EYE_ADAPTATION_METER_BLOCK_COUNT];
    float Aspect = 16.0f / 9.0f;
    FillBlocks(Blocks, -3.25f);
    TEST_CHECK(run, fabsf(EyeAdaptation_Measure(Blocks, Aspect) + 3.25f) < 1e-5f);

    float Middle = ShareOf(Blocks, 31, 17, Aspect);
    float Corner = ShareOf(Blocks, 0, 0, Aspect);
    float Top = ShareOf(Blocks, 31, 0, Aspect);
    float Left = ShareOf(Blocks, 0, 17, Aspect);
    if (!TEST_CHECK(run, (fabsf(Corner / Middle - 0.01677f) < 2e-4f) && (fabsf(Top / Middle - 0.3815f) < 2e-3f)
        && (fabsf(Left / Middle - 0.04397f) < 3e-4f)))
    {
        printf("    corner %.5f, top %.5f, left %.5f of the middle's\n", (double)(Corner / Middle), (double)(Top / Middle),
            (double)(Left / Middle));
    }
    // Mirrored blocks weigh alike, along both sides.
    TEST_CHECK(run, (ShareOf(Blocks, 63, 35, Aspect) == Corner) && (ShareOf(Blocks, 31, 35, Aspect) == Top)
        && (ShareOf(Blocks, 63, 17, Aspect) == ShareOf(Blocks, 0, 17, Aspect)));
    // The view's aspect stretches the circle: a narrower view weighs its side edges more.
    TEST_CHECK(run, ShareOf(Blocks, 0, 17, 1.0f) > 2.0f * Left);
    // A bright middle (a quarter of the blocks) and a dark rim: the middle holds half the weight.
    FillBlocks(Blocks, -6.0f);
    for (int Row = 9; Row < 27; Row++)
    {
        for (int Column = 16; Column < 48; Column++)
        {
            *BlockAt(Blocks, Column, Row) = 0.0f;
        }
    }
    TEST_CHECK(run, fabsf(EyeAdaptation_Measure(Blocks, Aspect) + 3.0062f) < 1e-3f); // the plain mean is -4.5

    // Unusable blocks are left out; an unusable aspect is 16:9's.
    FillBlocks(Blocks, 2.0f);
    *BlockAt(Blocks, 31, 17) = NAN;
    *BlockAt(Blocks, 30, 17) = INFINITY;
    *BlockAt(Blocks, 29, 17) = -INFINITY;
    TEST_CHECK(run, fabsf(EyeAdaptation_Measure(Blocks, Aspect) - 2.0f) < 1e-5f);
    FillBlocks(Blocks, NAN);
    TEST_CHECK(run, isnan(EyeAdaptation_Measure(Blocks, Aspect)));
    const float Unusable[] = { 0.0f, -1.0f, NAN, INFINITY };
    for (size_t Index = 0U; Index < (sizeof(Unusable) / sizeof(Unusable[0])); Index++)
    {
        TEST_CHECK(run, ShareOf(Blocks, 0, 17, Unusable[Index]) == Left);
    }
}

/* The anchor is a share of the sky's scene luminance; no usable luminance gives none. */
static void AnchorIsAShareOfTheSkyLight(TestRun* run)
{
    TEST_CHECK(run, fabsf(EyeAdaptation_GetAnchor(TEST_NOON_LUMINANCE)
        - log2f(TEST_NOON_LUMINANCE * EYE_ADAPTATION_ANCHOR_SHARE)) < 1e-6f);
    TEST_CHECK(run, fabsf(EyeAdaptation_GetAnchor(2.0f * TEST_NIGHT_LUMINANCE)
        - EyeAdaptation_GetAnchor(TEST_NIGHT_LUMINANCE) - 1.0f) < 1e-5f);
    const float Unusable[] = { 0.0f, -1.0f, NAN, INFINITY };
    for (size_t Index = 0U; Index < (sizeof(Unusable) / sizeof(Unusable[0])); Index++)
    {
        TEST_CHECK(run, isnan(EyeAdaptation_GetAnchor(Unusable[Index])));
    }
}

/* A new eye has no level; it follows the anchor (and keeps it through an unusable one) until the first measurement,
 * which sets it at once; from then on the anchor plays no part. */
static void FirstMeasurementSetsTheEye(TestRun* run)
{
    EyeAdaptation Eye;
    EyeAdaptation_Construct(&Eye);
    TEST_CHECK(run, isnan(EyeAdaptation_GetLevel(&Eye)) && !EyeAdaptation_IsMeasured(&Eye));
    EyeAdaptation_Update(&Eye, NAN, NAN, 0.1f);
    TEST_CHECK(run, isnan(EyeAdaptation_GetLevel(&Eye)));
    EyeAdaptation_Update(&Eye, -2.0f, NAN, 0.1f);
    TEST_CHECK(run, (EyeAdaptation_GetLevel(&Eye) == -2.0f) && !EyeAdaptation_IsMeasured(&Eye));
    EyeAdaptation_Update(&Eye, -7.0f, NAN, 0.1f);
    TEST_CHECK(run, EyeAdaptation_GetLevel(&Eye) == -7.0f);
    EyeAdaptation_Update(&Eye, NAN, NAN, 0.1f);
    EyeAdaptation_Update(&Eye, INFINITY, -INFINITY, 0.1f);
    TEST_CHECK(run, (EyeAdaptation_GetLevel(&Eye) == -7.0f) && !EyeAdaptation_IsMeasured(&Eye));
    EyeAdaptation_Update(&Eye, -7.0f, 1.5f, 0.1f);
    TEST_CHECK(run, (EyeAdaptation_GetLevel(&Eye) == 1.5f) && EyeAdaptation_IsMeasured(&Eye));
    for (int Frame = 0; Frame < 100; Frame++)
    {
        EyeAdaptation_Update(&Eye, -20.0f, NAN, 0.1f);
    }
    TEST_CHECK(run, EyeAdaptation_GetLevel(&Eye) == 1.5f);
}

/* After that it eases toward the last measurement in stops: the share 1 - e^(-rate t) of the step a frame, the rate the
 * base speed raised per stop of the step, five times it while brightening; at most a tenth of a second counts; a frame
 * without time moves nothing but takes a new measurement as its target; it closes in without overshooting, faster for
 * a larger step, and brightens faster than it darkens. */
static void EyeEasesInStops(TestRun* run)
{
    EyeAdaptation Eye = MeasuredEye(0.0f);
    EyeAdaptation_Update(&Eye, NAN, -2.0f, 0.1f);
    double Darkening = -2.0 * (1.0 - exp(-0.1 * EYE_ADAPTATION_BASE_SPEED * (1.0 + 2.0 * EYE_ADAPTATION_STOP_GAIN)));
    TEST_CHECK(run, fabs((double)EyeAdaptation_GetLevel(&Eye) - Darkening) < 1e-6);
    Eye = MeasuredEye(0.0f);
    EyeAdaptation_Update(&Eye, NAN, 2.0f, 0.1f);
    double Brightening = 2.0 * (1.0 - exp(-0.1 * EYE_ADAPTATION_BASE_SPEED * EYE_ADAPTATION_BRIGHTEN_MULTIPLIER
        * (1.0 + 2.0 * EYE_ADAPTATION_STOP_GAIN)));
    TEST_CHECK(run, fabs((double)EyeAdaptation_GetLevel(&Eye) - Brightening) < 1e-6);

    // A long frame counts as a tenth of a second.
    EyeAdaptation Long = MeasuredEye(0.0f);
    EyeAdaptation_Update(&Long, NAN, -2.0f, 3.0f);
    TEST_CHECK(run, fabs((double)EyeAdaptation_GetLevel(&Long) - Darkening) < 1e-6);
    // No time: no move, but the measurement is the target the next frame eases toward.
    const float NoTime[] = { 0.0f, -0.1f, NAN };
    for (size_t Index = 0U; Index < (sizeof(NoTime) / sizeof(NoTime[0])); Index++)
    {
        EyeAdaptation Held = MeasuredEye(0.0f);
        EyeAdaptation_Update(&Held, NAN, -2.0f, NoTime[Index]);
        TEST_CHECK(run, EyeAdaptation_GetLevel(&Held) == 0.0f);
        EyeAdaptation_Update(&Held, NAN, NAN, 0.1f);
        TEST_CHECK(run, fabs((double)EyeAdaptation_GetLevel(&Held) - Darkening) < 1e-6);
    }

    // It closes in, never past the target; a step twice as large halves sooner (the rate rises with the step).
    int HalfTimes[2] = { 0, 0 };
    const float Steps[2] = { -2.0f, -4.0f };
    for (int Index = 0; Index < 2; Index++)
    {
        EyeAdaptation Closing = MeasuredEye(0.0f);
        EyeAdaptation_Update(&Closing, NAN, Steps[Index], 0.0f);
        float Previous = 0.0f;
        bool IsClosing = true;
        for (int Frame = 1; Frame <= 2000; Frame++)
        {
            EyeAdaptation_Update(&Closing, NAN, NAN, 1.0f / 60.0f);
            float Level = EyeAdaptation_GetLevel(&Closing);
            IsClosing = IsClosing && (Level <= Previous) && (Level >= Steps[Index]);
            Previous = Level;
            if ((HalfTimes[Index] == 0) && (Level <= 0.5f * Steps[Index]))
            {
                HalfTimes[Index] = Frame;
            }
        }
        TEST_CHECK(run, IsClosing && (fabsf(Previous - Steps[Index]) < 0.01f));
    }
    TEST_CHECK(run, (HalfTimes[0] > 0) && (HalfTimes[1] > 0) && (HalfTimes[1] < HalfTimes[0]));
    // Brightening by two stops halves its step about five times sooner than darkening by two.
    EyeAdaptation Up = MeasuredEye(0.0f);
    EyeAdaptation_Update(&Up, NAN, 2.0f, 0.0f);
    int UpHalf = 0;
    for (int Frame = 1; (Frame <= 2000) && (UpHalf == 0); Frame++)
    {
        EyeAdaptation_Update(&Up, NAN, NAN, 1.0f / 60.0f);
        UpHalf = (EyeAdaptation_GetLevel(&Up) >= 1.0f) ? Frame : 0;
    }
    if (!TEST_CHECK(run, (UpHalf > 0) && (UpHalf * 4 < HalfTimes[0]) && (UpHalf * 6 > HalfTimes[0])))
    {
        printf("    brightening halves in %d frames, darkening in %d\n", UpHalf, HalfTimes[0]);
    }
}

/* The meter's window spans the stops below and above the anchor; the sky's floor is the night light's luminance, held
 * within the window (a brighter night light counts as the window's top, none as its bottom); without an anchor the
 * window is the widest. */
static void WindowSpansTheAnchor(TestRun* run)
{
    EyeAdaptationWindow Window = EyeAdaptation_GetWindow(-9.0f, exp2f(-8.0f));
    TEST_CHECK(run, (Window.Low == -9.0f - EYE_ADAPTATION_WINDOW_BELOW) && (Window.High == -9.0f
        + EYE_ADAPTATION_WINDOW_ABOVE) && (fabsf(Window.SkyLow + 8.0f) < 1e-5f));
    TEST_CHECK(run, EyeAdaptation_GetWindow(-9.0f, exp2f(-1.0f)).SkyLow == Window.High);
    TEST_CHECK(run, EyeAdaptation_GetWindow(-9.0f, exp2f(-30.0f)).SkyLow == Window.Low);
    const float NoNight[] = { 0.0f, -1.0f, NAN, INFINITY };
    for (size_t Index = 0U; Index < (sizeof(NoNight) / sizeof(NoNight[0])); Index++)
    {
        TEST_CHECK(run, EyeAdaptation_GetWindow(-9.0f, NoNight[Index]).SkyLow == Window.Low);
    }
    const float Unusable[] = { NAN, INFINITY, -INFINITY };
    for (size_t Index = 0U; Index < (sizeof(Unusable) / sizeof(Unusable[0])); Index++)
    {
        Window = EyeAdaptation_GetWindow(Unusable[Index], 0.0f);
        TEST_CHECK(run, (Window.Low == -64.0f) && (Window.SkyLow == -64.0f) && (Window.High == 64.0f));
        TEST_CHECK(run, EyeAdaptation_GetWindow(Unusable[Index], exp2f(-8.0f)).SkyLow == -8.0f);
    }
}

/* The real scale reads the sky's light at night vision's anchors: noon's daylight 5000 cd/m^2 for its luminance, the
 * moonless night 0.001 cd/m^2 for its own; the twilight's real light is the daylight's (far brighter than the raised
 * night's on the real scale); a darker night, a world without the sky's light and broken values are read at the
 * moonless night's scale; more daylight raises it. */
static void RealScaleReadsTheSkyLight(TestRun* run)
{
    double Floor = NIGHT_VISION_NIGHT_CANDELAS / NIGHT_VISION_NIGHT_LUMINANCE;
    double Noon = EyeAdaptation_GetRealScale(TEST_NOON_LUMINANCE, 0.0f);
    TEST_CHECK(run, fabs(Noon * NIGHT_VISION_NOON_LUMINANCE / NIGHT_VISION_NOON_CANDELAS - 1.0) < 1e-6);
    TEST_CHECK(run, fabs(EyeAdaptation_GetRealScale(0.0f, TEST_NIGHT_LUMINANCE) / Floor - 1.0) < 1e-4);
    double Twilight = EyeAdaptation_GetRealScale(0.004f, TEST_NIGHT_LUMINANCE);
    double Expected = NightVision_GetRealFieldLuminance(0.004f, TEST_NIGHT_LUMINANCE)
        / ((double)0.004f + (double)TEST_NIGHT_LUMINANCE);
    TEST_CHECK(run, (fabs(Twilight / Expected - 1.0) < 1e-6) && (Twilight > 1000.0 * Floor));
    const float Dark[][2] = { { 0.0f, 0.5f * TEST_NIGHT_LUMINANCE }, { 0.0f, 0.0f }, { NAN, NAN }, { -1.0f, -1.0f },
        { INFINITY, 0.0f } };
    for (size_t Index = 0U; Index < (sizeof(Dark) / sizeof(Dark[0])); Index++)
    {
        TEST_CHECK(run, EyeAdaptation_GetRealScale(Dark[Index][0], Dark[Index][1]) == Floor);
    }
    bool IsRising = true;
    double Previous = 0.0;
    for (int Step = 0; Step <= 40; Step++)
    {
        double Scale = EyeAdaptation_GetRealScale(1e-6f * powf(1.5f, (float)Step), TEST_NIGHT_LUMINANCE);
        IsRising = IsRising && (Scale >= Previous);
        Previous = Scale;
    }
    TEST_CHECK(run, IsRising);
}

/* The exposure is the curve's: the screen shows the level at KEY x (R / noon)^POWER, so one more stop of real light at
 * the same scale shows POWER stops brighter, and the exposure falls (1 - POWER) stops; a view of a white surface under a
 * clear noon shows at KEY; outside its range it is clamped; an eye without a level is neutral, and an unusable scale is
 * read as the floor. */
static void ExposureFollowsPartialAdaptation(TestRun* run)
{
    double Noon = NIGHT_VISION_NOON_CANDELAS / NIGHT_VISION_NOON_LUMINANCE;
    float White = log2f(TEST_NOON_LUMINANCE);
    TEST_CHECK(run, fabsf(EyeAdaptation_GetExposure(White, Noon) * TEST_NOON_LUMINANCE / EYE_ADAPTATION_KEY - 1.0f)
        < 1e-4f);
    double Floor = NIGHT_VISION_NIGHT_CANDELAS / NIGHT_VISION_NIGHT_LUMINANCE;
    const double Scales[] = { Noon, Floor, 50.0 };
    for (size_t Index = 0U; Index < (sizeof(Scales) / sizeof(Scales[0])); Index++)
    {
        for (float Level = -12.0f; Level <= 0.0f; Level += 1.0f)
        {
            float Exposure = EyeAdaptation_GetExposure(Level, Scales[Index]);
            float Brighter = EyeAdaptation_GetExposure(Level + 1.0f, Scales[Index]);
            bool IsInside = (Exposure > EYE_ADAPTATION_MIN_EXPOSURE) && (Exposure < EYE_ADAPTATION_MAX_EXPOSURE)
                && (Brighter > EYE_ADAPTATION_MIN_EXPOSURE) && (Brighter < EYE_ADAPTATION_MAX_EXPOSURE);
            if (!IsInside)
            {
                continue;
            }
            // The screen level 2^level x exposure gains POWER stops; the exposure loses 1 - POWER.
            double Gain = log2((double)Brighter * 2.0 / (double)Exposure);
            TEST_CHECK(run, fabs(Gain - (double)EYE_ADAPTATION_POWER) < 1e-4);
        }
    }
    TEST_CHECK(run, EyeAdaptation_GetExposure(20.0f, Noon) == EYE_ADAPTATION_MIN_EXPOSURE);
    TEST_CHECK(run, EyeAdaptation_GetExposure(-40.0f, Floor) == EYE_ADAPTATION_MAX_EXPOSURE);
    TEST_CHECK(run, (EyeAdaptation_GetExposure(NAN, Noon) == 1.0f) && (EyeAdaptation_GetExposure(INFINITY, Noon) == 1.0f));
    const double Unusable[] = { 0.0, -5.0, NAN, INFINITY, 0.5 * Floor };
    for (size_t Index = 0U; Index < (sizeof(Unusable) / sizeof(Unusable[0])); Index++)
    {
        TEST_CHECK(run, EyeAdaptation_GetExposure(-9.0f, Unusable[Index]) == EyeAdaptation_GetExposure(-9.0f, Floor));
    }
    // More real light at the same level (a brighter mix) shows brighter.
    TEST_CHECK(run, EyeAdaptation_GetExposure(-6.0f, 2.0 * Floor) > EyeAdaptation_GetExposure(-6.0f, Floor));
}

/* A room's bias multiplies the exposure by 2^stops: none (or a broken one) leaves it exactly, every stop doubles it, and
 * the result keeps within the exposure's range. */
static void BiasMultipliesTheExposure(TestRun* run)
{
    const float Exposures[] = { 0.5f, 1.88f, 42.0f };
    for (size_t Index = 0U; Index < (sizeof(Exposures) / sizeof(Exposures[0])); Index++)
    {
        float Exposure = Exposures[Index];
        TEST_CHECK(run, (EyeAdaptation_GetBiasedExposure(Exposure, 0.0f) == Exposure)
            && (EyeAdaptation_GetBiasedExposure(Exposure, -0.0f) == Exposure)
            && (EyeAdaptation_GetBiasedExposure(Exposure, NAN) == Exposure)
            && (EyeAdaptation_GetBiasedExposure(Exposure, INFINITY) == Exposure));
        TEST_CHECK(run, (EyeAdaptation_GetBiasedExposure(Exposure, 1.0f) == 2.0f * Exposure)
            && (EyeAdaptation_GetBiasedExposure(Exposure, -2.0f) == 0.25f * Exposure)
            && (fabsf(EyeAdaptation_GetBiasedExposure(Exposure, 0.5f) / (Exposure * 1.41421356f) - 1.0f) < 1e-6f));
    }
    TEST_CHECK(run, EyeAdaptation_GetBiasedExposure(EYE_ADAPTATION_MAX_EXPOSURE, 1.0f) == EYE_ADAPTATION_MAX_EXPOSURE);
    TEST_CHECK(run, EyeAdaptation_GetBiasedExposure(EYE_ADAPTATION_MIN_EXPOSURE, -1.0f) == EYE_ADAPTATION_MIN_EXPOSURE);
}


// Public functions.
void EyeAdaptationTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "meter_weighs_the_middle", MeterWeighsTheMiddle },
        { "anchor_is_a_share_of_the_sky_light", AnchorIsAShareOfTheSkyLight },
        { "first_measurement_sets_the_eye", FirstMeasurementSetsTheEye },
        { "eye_eases_in_stops", EyeEasesInStops },
        { "window_spans_the_anchor", WindowSpansTheAnchor },
        { "real_scale_reads_the_sky_light", RealScaleReadsTheSkyLight },
        { "exposure_follows_partial_adaptation", ExposureFollowsPartialAdaptation },
        { "bias_multiplies_the_exposure", BiasMultipliesTheExposure },
    };
    TestRun_RunSuite(run, "EyeAdaptation", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
