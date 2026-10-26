#include "TestSuites.h"
#include "NightVision.h"
#include <math.h>


// Macros.
/** The scene luminance (the field) of a clear noon, a moonless night and a full-moon night, in the game's light units
 *  (T36's scale: WorldSkyLight_GetSceneLuminance with the default environment). */
#define TEST_NOON_LUMINANCE 0.40f
#define TEST_MOONLESS_LUMINANCE 0.0054f
#define TEST_FULL_MOON_LUMINANCE 0.016f


// Tests.
/* The calibration follows its anchors: noon is 5000 cd/m^2, a moonless night 0.001 cd/m^2, and Hunt's half point
 * (0.04 cd/m^2) falls at the half luminance; the full-colour luminance is where the rods' share reaches 1/1024. */
static void CalibrationFollowsTheAnchors(TestRun* run)
{
    NightVisionCalibration Calibration = NightVision_GetCalibration();
    TEST_CHECK(run, fabsf(Calibration.Steepness - 3.58f) < 0.05f);
    TEST_CHECK(run, fabsf(Calibration.HalfLuminance - 0.0151f) < 0.001f);
    TEST_CHECK(run, (Calibration.FullColourLuminance > 0.095f) && (Calibration.FullColourLuminance < 0.115f));

    TEST_CHECK(run, fabs(NightVision_GetRealLuminance(TEST_NOON_LUMINANCE) / NIGHT_VISION_NOON_CANDELAS - 1.0) < 1e-4);
    TEST_CHECK(run, fabs(NightVision_GetRealLuminance(TEST_MOONLESS_LUMINANCE) / NIGHT_VISION_NIGHT_CANDELAS - 1.0) < 1e-3);
    TEST_CHECK(run, fabs(NightVision_GetRealLuminance(Calibration.HalfLuminance) / NIGHT_VISION_HALF_CANDELAS - 1.0) < 1e-3);
    // A full-moon night stands for a few hundredths of a cd/m^2, as a real one.
    double FullMoon = NightVision_GetRealLuminance(TEST_FULL_MOON_LUMINANCE);
    TEST_CHECK(run, (FullMoon > 0.01) && (FullMoon < 0.06));
    TEST_CHECK(run, (NightVision_GetRealLuminance(0.0f) == 0.0) && (NightVision_GetRealLuminance(-1.0f) == 0.0)
        && (NightVision_GetRealLuminance(NAN) == 0.0));

    // The calibration is the one for this scale's anchors; the light before T36 (the renderer's A/B) takes its own.
    NightVisionCalibration Anchored = NightVision_GetCalibrationFor(NIGHT_VISION_NOON_LUMINANCE,
        NIGHT_VISION_NIGHT_LUMINANCE);
    TEST_CHECK(run, (Anchored.Steepness == Calibration.Steepness) && (Anchored.HalfLuminance == Calibration.HalfLuminance)
        && (Anchored.FullColourLuminance == Calibration.FullColourLuminance));
    NightVisionCalibration Legacy = NightVision_GetCalibrationFor(NIGHT_VISION_LEGACY_NOON_LUMINANCE,
        NIGHT_VISION_LEGACY_NIGHT_LUMINANCE);
    TEST_CHECK(run, (fabsf(Legacy.Steepness - 3.2f) < 0.05f) && (fabsf(Legacy.HalfLuminance - 0.019f) < 0.001f));
}

/* The field luminance from the light split in two (T36): the daylight on the real scale, linearly, the night's lights
 * through the power law, their real luminances added: noon's daylight is noon, the night's lights alone are themselves,
 * and the sum of a little daylight and the night stands above both, far above the night alone in the twilight. */
static void FieldAddsDaylightAndTheNight(TestRun* run)
{
    TEST_CHECK(run, fabsf(NightVision_GetFieldLuminance(TEST_NOON_LUMINANCE, 0.0f) - TEST_NOON_LUMINANCE) < 1e-5f);
    TEST_CHECK(run, fabsf(NightVision_GetFieldLuminance(0.0f, TEST_MOONLESS_LUMINANCE) - TEST_MOONLESS_LUMINANCE) < 1e-6f);
    TEST_CHECK(run, fabsf(NightVision_GetFieldLuminance(0.0f, TEST_FULL_MOON_LUMINANCE) - TEST_FULL_MOON_LUMINANCE) < 1e-6f);
    // Its real luminance is the daylight's (linear: noon's share of noon's candelas) plus the night's.
    float Twilight = NightVision_GetFieldLuminance(0.004f, TEST_MOONLESS_LUMINANCE);
    double Real = NightVision_GetRealLuminance(Twilight);
    double Expected = NIGHT_VISION_NOON_CANDELAS * 0.004 / NIGHT_VISION_NOON_LUMINANCE
        + NightVision_GetRealLuminance(TEST_MOONLESS_LUMINANCE);
    TEST_CHECK(run, fabs(Real / Expected - 1.0) < 1e-4);
    // A sunset's sky light (a hundredth of noon's daylight) is colour vision, though the night's light is about as bright
    // on the game's scale.
    TEST_CHECK(run, NightVision_GetRodShare(0.0f, Twilight, 1.0f) < 0.01f);
    TEST_CHECK(run, NightVision_GetRodShare(0.0f, 0.004f + TEST_MOONLESS_LUMINANCE, 1.0f) > 0.5f);
    // Rising with either; none gives none; broken values count as none.
    bool IsRising = true;
    float Previous = 0.0f;
    for (int Step = 1; Step <= 40; Step++)
    {
        float Current = NightVision_GetFieldLuminance(1e-6f * powf(1.4f, (float)Step), 0.002f);
        IsRising = IsRising && (Current > Previous);
        Previous = Current;
    }
    TEST_CHECK(run, IsRising && (NightVision_GetFieldLuminance(0.0f, 0.0f) == 0.0f));
    TEST_CHECK(run, (NightVision_GetFieldLuminance(NAN, TEST_MOONLESS_LUMINANCE)
        == NightVision_GetFieldLuminance(0.0f, TEST_MOONLESS_LUMINANCE)) && (NightVision_GetFieldLuminance(-1.0f, -1.0f) == 0.0f));
}

/* By day every pixel, however dark, keeps its colour exactly: the field is far above the full-colour luminance. */
static void DaylightKeepsEveryColour(TestRun* run)
{
    NightVisionCalibration Calibration = NightVision_GetCalibration();
    TEST_CHECK(run, NightVision_GetRodShare(0.0f, TEST_NOON_LUMINANCE, 1.0f) == 0.0f);
    TEST_CHECK(run, NightVision_GetRodShare(0.001f, TEST_NOON_LUMINANCE, 1.0f) == 0.0f);
    TEST_CHECK(run, NightVision_GetRodShare(20.0f, TEST_NOON_LUMINANCE, 1.0f) == 0.0f);
    TEST_CHECK(run, NightVision_GetRodShare(0.0f, Calibration.FullColourLuminance * 1.001f, 1.0f) == 0.0f);
    float JustBelow = NightVision_GetRodShare(0.0f, Calibration.FullColourLuminance * 0.99f, 1.0f);
    TEST_CHECK(run, (JustBelow > 0.0f) && (JustBelow < 1.0f / 512.0f));
}

/* At night the dim world is seen by the rods (colourless), a full moon leaves muted colours, a lamp's bright pool keeps
 * its colour; the share falls steadily with the light, and is half and half at the half luminance. */
static void NightIsSeenByTheRods(TestRun* run)
{
    NightVisionCalibration Calibration = NightVision_GetCalibration();
    TEST_CHECK(run, NightVision_GetRodShare(0.005f, TEST_MOONLESS_LUMINANCE, 1.0f) > 0.95f);
    TEST_CHECK(run, NightVision_GetRodShare(0.2f, TEST_MOONLESS_LUMINANCE, 1.0f) < 0.01f);
    float FullMoon = NightVision_GetRodShare(0.015f, TEST_FULL_MOON_LUMINANCE, 1.0f);
    TEST_CHECK(run, (FullMoon > 0.4f) && (FullMoon < 0.75f));
    TEST_CHECK(run, fabsf(NightVision_GetRodShare(Calibration.HalfLuminance, 0.0f, 1.0f) - 0.5f) < 1e-4f);

    // The brighter of the pixel and the field counts: a dim pixel in a moonlit scene gets the moonlit share.
    TEST_CHECK(run, NightVision_GetRodShare(0.001f, TEST_FULL_MOON_LUMINANCE, 1.0f)
        == NightVision_GetRodShare(TEST_FULL_MOON_LUMINANCE, 0.0f, 1.0f));

    bool IsFalling = true;
    float Previous = NightVision_GetRodShare(0.0005f, 0.0f, 1.0f);
    for (int Step = 1; Step <= 40; Step++)
    {
        float Luminance = 0.0005f * powf(1.15f, (float)Step);
        float Current = NightVision_GetRodShare(Luminance, 0.0f, 1.0f);
        IsFalling = IsFalling && (Current <= Previous);
        Previous = Current;
    }
    TEST_CHECK(run, IsFalling);
}

/* The strength scales the rods' share; 0 switches night vision off; bad inputs count as darkness or none. */
static void StrengthScalesIt(TestRun* run)
{
    float Full = NightVision_GetRodShare(0.005f, TEST_MOONLESS_LUMINANCE, 1.0f);
    TEST_CHECK(run, fabsf(NightVision_GetRodShare(0.005f, TEST_MOONLESS_LUMINANCE, 0.5f) - 0.5f * Full) < 1e-6f);
    TEST_CHECK(run, NightVision_GetRodShare(0.005f, TEST_MOONLESS_LUMINANCE, 0.0f) == 0.0f);
    TEST_CHECK(run, NightVision_GetRodShare(0.005f, TEST_MOONLESS_LUMINANCE, 2.0f) == Full);
    TEST_CHECK(run, NightVision_GetRodShare(0.005f, TEST_MOONLESS_LUMINANCE, NAN) == 0.0f);
    TEST_CHECK(run, NightVision_GetRodShare(NAN, NAN, 1.0f) == 1.0f);
    TEST_CHECK(run, NightVision_GetRodShare(-1.0f, -1.0f, 1.0f) == 1.0f);
}


/* The rods' grey keeps a pixel's luminance at every blue shift: white at 0, the night blue (CIE 0.25, 0.25) at 1. */
static void RodTintKeepsTheLuminance(TestRun* run)
{
    const float Shifts[] = { 0.0f, 0.25f, 0.5f, 1.0f };
    for (size_t Index = 0; Index < (sizeof(Shifts) / sizeof(Shifts[0])); Index++)
    {
        float Tint[3];
        NightVision_GetRodTint(Shifts[Index], Tint);
        float Luminance = (0.2126f * Tint[0]) + (0.7152f * Tint[1]) + (0.0722f * Tint[2]);
        TEST_CHECK(run, fabsf(Luminance - 1.0f) < 1e-5f);
        TEST_CHECK(run, (Tint[2] >= Tint[1]) && (Tint[1] >= Tint[0])); // never violet: blue, then green, then red
    }

    float White[3];
    NightVision_GetRodTint(0.0f, White);
    TEST_CHECK(run, (White[0] == 1.0f) && (White[1] == 1.0f) && (White[2] == 1.0f));

    // At 1 the colour's chromaticity is the night blue's (through the sRGB -> XYZ matrix).
    float Blue[3];
    NightVision_GetRodTint(1.0f, Blue);
    double X = (0.4124564 * Blue[0]) + (0.3575761 * Blue[1]) + (0.1804375 * Blue[2]);
    double Y = (0.2126729 * Blue[0]) + (0.7151522 * Blue[1]) + (0.0721750 * Blue[2]);
    double Z = (0.0193339 * Blue[0]) + (0.1191920 * Blue[1]) + (0.9503041 * Blue[2]);
    TEST_CHECK(run, fabs((X / (X + Y + Z)) - NIGHT_VISION_BLUE_X) < 1e-4);
    TEST_CHECK(run, fabs((Y / (X + Y + Z)) - NIGHT_VISION_BLUE_Y) < 1e-4);

    // Out of range clamps; NaN counts as 0.
    float Clamped[3];
    NightVision_GetRodTint(2.0f, Clamped);
    TEST_CHECK(run, (Clamped[0] == Blue[0]) && (Clamped[1] == Blue[1]) && (Clamped[2] == Blue[2]));
    NightVision_GetRodTint(NAN, Clamped);
    TEST_CHECK(run, (Clamped[0] == 1.0f) && (Clamped[1] == 1.0f) && (Clamped[2] == 1.0f));
    NightVision_GetRodTint(-1.0f, Clamped);
    TEST_CHECK(run, (Clamped[0] == 1.0f) && (Clamped[1] == 1.0f) && (Clamped[2] == 1.0f));
}


// Public functions.
void NightVisionTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "calibration_follows_the_anchors", CalibrationFollowsTheAnchors },
        { "field_adds_daylight_and_the_night", FieldAddsDaylightAndTheNight },
        { "daylight_keeps_every_colour", DaylightKeepsEveryColour },
        { "night_is_seen_by_the_rods", NightIsSeenByTheRods },
        { "strength_scales_it", StrengthScalesIt },
        { "rod_tint_keeps_the_luminance", RodTintKeepsTheLuminance },
    };
    TestRun_RunSuite(run, "NightVision", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
