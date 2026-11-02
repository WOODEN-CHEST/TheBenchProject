#include <math.h>
#include <stdint.h>
#include "TestFramework.h"
#include "TestSuites.h"
#include "CrtScreen.h"


// Macros.
/** How close two floats count as the same in these tests (a few float steps around 1). */
#define NEAR_TOLERANCE 1e-5f
/** How many steps the continuity sweeps take, and the largest jump one step may show in an eased value. */
#define SWEEP_STEPS 4000
#define SWEEP_MAX_JUMP 0.02f


// Static functions.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) <= NEAR_TOLERANCE;
}

static bool IsNearVector(Vector2 a, Vector2 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y);
}

/* The point of the glass that shows the picture's corner (1, 1), found by fixed-point iteration of the curvature's two
 * equations in centred coordinates; returned in texture coordinates. */
static Vector2 FindPictureCorner(float curvature, float aspect)
{
    float X = 1.0f;
    float Y = 1.0f;
    for (int Step = 0; Step < 200; Step++)
    {
        X = 1.0f / (1.0f + (curvature * Y * Y));
        Y = 1.0f / (1.0f + (curvature * aspect * X * X));
    }
    return (Vector2){ (X * 0.5f) + 0.5f, (Y * 0.5f) + 0.5f };
}


// Tests: the curvature.
/* The centre stays, the middles of the edges stay, and with no curvature every point is itself. */
static void TheCurvatureKeepsTheCentreAndIsTheIdentityAtZero(TestRun* run)
{
    const float Curvatures[] = { 0.0f, 0.08f, 0.25f, CRT_SCREEN_MAX_CURVATURE };
    const float Aspects[] = { 4.0f / 3.0f, 16.0f / 9.0f, 0.75f, 1.0f };
    for (size_t Index = 0U; Index < (sizeof(Curvatures) / sizeof(Curvatures[0])); Index++)
    {
        float Aspect = Aspects[Index];
        TEST_CHECK(run, IsNearVector(CrtScreen_Distort((Vector2){ 0.5f, 0.5f }, Curvatures[Index], Aspect),
            (Vector2){ 0.5f, 0.5f }));
        TEST_CHECK(run, IsNearVector(CrtScreen_Distort((Vector2){ 1.0f, 0.5f }, Curvatures[Index], Aspect),
            (Vector2){ 1.0f, 0.5f }));
        TEST_CHECK(run, IsNearVector(CrtScreen_Distort((Vector2){ 0.5f, 0.0f }, Curvatures[Index], Aspect),
            (Vector2){ 0.5f, 0.0f }));
    }
    for (int Row = 0; Row <= 10; Row++)
    {
        for (int Column = 0; Column <= 10; Column++)
        {
            Vector2 Point = { (float)Column / 10.0f, (float)Row / 10.0f };
            TEST_CHECK(run, IsNearVector(CrtScreen_Distort(Point, 0.0f, 4.0f / 3.0f), Point));
        }
    }
}

/* The edges bend in: the glass's corners show nothing (beyond the picture), the picture's corner lies inside the glass
 * by about the curvature times the half width, as far across as down (a round corner on any aspect), and the bend is
 * symmetric and keeps the picture's order. */
static void TheCurvatureBendsTheCornersInByItsAmount(TestRun* run)
{
    const float Curvature = 0.08f;
    const float Aspects[] = { 4.0f / 3.0f, 16.0f / 9.0f, 0.75f };
    for (size_t Index = 0U; Index < (sizeof(Aspects) / sizeof(Aspects[0])); Index++)
    {
        float Aspect = Aspects[Index];
        Vector2 GlassCorner = CrtScreen_Distort((Vector2){ 1.0f, 1.0f }, Curvature, Aspect);
        TEST_CHECK(run, (GlassCorner.x > 1.0f) && (GlassCorner.y > 1.0f));
        Vector2 Corner = FindPictureCorner(Curvature, Aspect);
        TEST_CHECK(run, IsNearVector(CrtScreen_Distort(Corner, Curvature, Aspect), (Vector2){ 1.0f, 1.0f }));
        // In: by a little less than the curvature's share of the half width across, and about as far down, measured in
        // the width's units (the height is the width over the aspect).
        float AcrossInset = (1.0f - Corner.x) * 2.0f;
        float DownInset = (1.0f - Corner.y) * 2.0f / Aspect;
        TEST_CHECK(run, (AcrossInset > (0.6f * Curvature)) && (AcrossInset < Curvature));
        TEST_CHECK(run, fabsf(AcrossInset - DownInset) < (0.2f * AcrossInset));
        for (int Step = 0; Step <= 20; Step++)
        {
            Vector2 Point = { 0.05f * (float)Step, 0.9f };
            Vector2 Mirror = CrtScreen_Distort((Vector2){ 1.0f - Point.x, 1.0f - Point.y }, Curvature, Aspect);
            Vector2 Shown = CrtScreen_Distort(Point, Curvature, Aspect);
            TEST_CHECK(run, IsNearVector(Mirror, (Vector2){ 1.0f - Shown.x, 1.0f - Shown.y }));
            if (Step > 0)
            {
                Vector2 Before = CrtScreen_Distort((Vector2){ 0.05f * (float)(Step - 1), 0.9f }, Curvature, Aspect);
                TEST_CHECK(run, Shown.x > Before.x);
            }
        }
    }
}


// Tests: the power animation.
/* The transition rises over the power-on time while powered and falls over the power-off time while not; a time of 0
 * switches at once, a step of nothing (0, negative, NaN) changes nothing, a value out of range is clamped first, and a
 * toggle half way goes back from where it is. */
static void ThePowerTurnsOnAndOffOverItsTimes(TestRun* run)
{
    TEST_CHECK(run, IsNear(CrtScreen_AdvancePower(0.0f, true, 0.8f, 0.6f, 0.4f), 0.5f));
    TEST_CHECK(run, CrtScreen_AdvancePower(0.0f, true, 0.8f, 0.6f, 0.8f) == 1.0f);
    TEST_CHECK(run, CrtScreen_AdvancePower(0.9f, true, 0.8f, 0.6f, 5.0f) == 1.0f);
    TEST_CHECK(run, IsNear(CrtScreen_AdvancePower(1.0f, false, 0.8f, 0.6f, 0.3f), 0.5f));
    TEST_CHECK(run, CrtScreen_AdvancePower(1.0f, false, 0.8f, 0.6f, 0.6f) == 0.0f);
    TEST_CHECK(run, CrtScreen_AdvancePower(0.1f, false, 0.8f, 0.6f, 5.0f) == 0.0f);
    // Sixty steps of a sixtieth of a second turn a one-second screen fully on.
    float Transition = 0.0f;
    for (int Tick = 0; Tick < 60; Tick++)
    {
        Transition = CrtScreen_AdvancePower(Transition, true, 1.0f, 1.0f, 1.0f / 60.0f);
    }
    TEST_CHECK(run, fabsf(Transition - 1.0f) < 1e-4f);
    TEST_CHECK(run, CrtScreen_AdvancePower(0.0f, true, 0.0f, 0.6f, 0.0f) == 1.0f);
    TEST_CHECK(run, CrtScreen_AdvancePower(1.0f, false, 0.8f, 0.0f, 0.0f) == 0.0f);
    TEST_CHECK(run, CrtScreen_AdvancePower(0.3f, true, 0.8f, 0.6f, 0.0f) == 0.3f);
    TEST_CHECK(run, CrtScreen_AdvancePower(0.3f, false, 0.8f, 0.6f, -1.0f) == 0.3f);
    TEST_CHECK(run, CrtScreen_AdvancePower(0.3f, true, 0.8f, 0.6f, NAN) == 0.3f);
    TEST_CHECK(run, CrtScreen_AdvancePower(NAN, true, 0.8f, 0.6f, 0.0f) == 0.0f);
    TEST_CHECK(run, CrtScreen_AdvancePower(7.0f, false, 0.8f, 0.6f, 0.0f) == 1.0f);
    TEST_CHECK(run, CrtScreen_AdvancePower(0.3f, true, NAN, 0.6f, 0.1f) == 1.0f);
    // Toggled half way through turning on, it turns off from there.
    float Half = CrtScreen_AdvancePower(0.0f, true, 1.0f, 1.0f, 0.5f);
    TEST_CHECK(run, IsNear(CrtScreen_AdvancePower(Half, false, 1.0f, 1.0f, 0.25f), 0.25f));
}

/* Turning on, the glass shows a bright line across that opens into the picture, which settles at its own brightness;
 * turning off, the picture collapses into the line, the line into a round dot, and the dot fades. Fully on is the
 * picture itself, fully off is dark. */
static void ThePowerShowsALineOpeningAndADotFading(TestRun* run)
{
    const float Aspect = 4.0f / 3.0f;
    CrtPowerShape On = CrtScreen_GetPowerShape(true, 1.0f, Aspect);
    TEST_CHECK(run, (On.OpenX == 1.0f) && (On.OpenY == 1.0f) && (On.Brightness == 1.0f) && (On.Whiteness == 0.0f)
        && (On.Light == 1.0f));
    CrtPowerShape Off = CrtScreen_GetPowerShape(false, 0.0f, Aspect);
    TEST_CHECK(run, (Off.Brightness == 0.0f) && (Off.Light == 0.0f));
    CrtPowerShape JustOff = CrtScreen_GetPowerShape(false, 1.0f, Aspect);
    TEST_CHECK(run, (JustOff.OpenX == 1.0f) && (JustOff.OpenY == 1.0f) && (JustOff.Brightness == 1.0f));
    TEST_CHECK(run, CrtScreen_GetPowerShape(true, 0.0f, Aspect).Brightness == 0.0f);

    // The line, appearing: as wide as the picture, thin, white.
    CrtPowerShape Line = CrtScreen_GetPowerShape(true, 0.5f * CRT_SCREEN_POWER_ON_LINE_END, Aspect);
    TEST_CHECK(run, (Line.OpenX == 1.0f) && (Line.OpenY == CRT_SCREEN_POWER_LINE) && (Line.Whiteness == 1.0f)
        && (Line.Brightness > 0.0f) && (Line.Brightness < CRT_SCREEN_POWER_MAX_BRIGHTNESS));
    // Opening: between the line and the picture; open, a little brighter than the picture.
    CrtPowerShape Opening = CrtScreen_GetPowerShape(true,
        0.5f * (CRT_SCREEN_POWER_ON_LINE_END + CRT_SCREEN_POWER_ON_OPEN_END), Aspect);
    TEST_CHECK(run, (Opening.OpenY > CRT_SCREEN_POWER_LINE) && (Opening.OpenY < 1.0f) && (Opening.OpenX == 1.0f));
    CrtPowerShape Opened = CrtScreen_GetPowerShape(true, CRT_SCREEN_POWER_ON_OPEN_END + 0.01f, Aspect);
    TEST_CHECK(run, (Opened.OpenY == 1.0f) && (Opened.Brightness > 1.0f)
        && (Opened.Brightness <= (1.0f + CRT_SCREEN_POWER_OVERSHOOT)) && (Opened.Whiteness == 0.0f));

    // Collapsing: into the line, then the dot, round on the glass (its half width the line's half height across the
    // aspect), then fading out.
    CrtPowerShape Collapsed = CrtScreen_GetPowerShape(false, 1.0f - CRT_SCREEN_POWER_OFF_LINE_END - 0.01f, Aspect);
    TEST_CHECK(run, (Collapsed.OpenY == CRT_SCREEN_POWER_LINE) && (Collapsed.OpenX < 1.0f) && (Collapsed.OpenX > 0.5f)
        && (Collapsed.Brightness == CRT_SCREEN_POWER_MAX_BRIGHTNESS) && (Collapsed.Whiteness == 1.0f));
    CrtPowerShape Dot = CrtScreen_GetPowerShape(false, 1.0f - CRT_SCREEN_POWER_OFF_DOT_END - 0.05f, Aspect);
    TEST_CHECK(run, IsNear(Dot.OpenX, CRT_SCREEN_POWER_LINE / Aspect) && (Dot.OpenY == CRT_SCREEN_POWER_LINE)
        && (Dot.Brightness > 0.0f) && (Dot.Brightness < CRT_SCREEN_POWER_MAX_BRIGHTNESS));
    TEST_CHECK(run, IsNear(CrtScreen_GetPowerShape(false, 0.5f, 1.0f).OpenY, CRT_SCREEN_POWER_LINE));
    // An aspect that is no aspect is taken as square.
    TEST_CHECK(run, IsNear(CrtScreen_GetPowerShape(false, 0.05f, 0.0f).OpenX, CRT_SCREEN_POWER_LINE));
    TEST_CHECK(run, IsNear(CrtScreen_GetPowerShape(false, 0.05f, NAN).OpenX, CRT_SCREEN_POWER_LINE));
    TEST_CHECK(run, CrtScreen_GetPowerShape(true, NAN, Aspect).Brightness == 0.0f);
}

/* Both animations run without a jump in what the glass shows (the open part and its brightness move by small steps),
 * never brighter than the most, and the light given off never more than the picture's own with its overshoot. */
static void ThePowerAnimationsMoveSmoothly(TestRun* run)
{
    const float Aspect = 4.0f / 3.0f;
    for (int Direction = 0; Direction < 2; Direction++)
    {
        bool IsPowered = Direction == 0;
        CrtPowerShape Before = CrtScreen_GetPowerShape(IsPowered, IsPowered ? 0.0f : 1.0f, Aspect);
        float LargestJump = 0.0f;
        bool IsWithinRange = true;
        for (int Step = 1; Step <= SWEEP_STEPS; Step++)
        {
            float Progress = (float)Step / (float)SWEEP_STEPS;
            CrtPowerShape Shape = CrtScreen_GetPowerShape(IsPowered, IsPowered ? Progress : (1.0f - Progress), Aspect);
            LargestJump = fmaxf(LargestJump, fabsf(Shape.OpenX - Before.OpenX));
            LargestJump = fmaxf(LargestJump, fabsf(Shape.OpenY - Before.OpenY));
            LargestJump = fmaxf(LargestJump, fabsf(Shape.Brightness - Before.Brightness)
                / CRT_SCREEN_POWER_MAX_BRIGHTNESS);
            IsWithinRange = IsWithinRange && (Shape.Brightness >= 0.0f)
                && (Shape.Brightness <= CRT_SCREEN_POWER_MAX_BRIGHTNESS) && (Shape.OpenX > 0.0f) && (Shape.OpenX <= 1.0f)
                && (Shape.OpenY > 0.0f) && (Shape.OpenY <= 1.0f) && (Shape.Whiteness >= 0.0f)
                && (Shape.Whiteness <= 1.0f) && (Shape.Light <= (1.0f + CRT_SCREEN_POWER_OVERSHOOT) + NEAR_TOLERANCE);
            Before = Shape;
        }
        TEST_CHECK(run, IsWithinRange);
        TEST_CHECK(run, LargestJump < SWEEP_MAX_JUMP);
    }
}


/* Switched half way, the transition turns around to the point of the other animation that carries on what is shown:
 * the open height between the opening and the collapse both ways, the line, the line about to open, a dot starting the
 * tube over; the ends never move. */
static void ASwitchHalfWayCarriesOnTheShape(TestRun* run)
{
    const float Aspect = 4.0f / 3.0f;
    TEST_CHECK(run, (CrtScreen_GetSwitchedTransition(true, 0.0f) == 0.0f)
        && (CrtScreen_GetSwitchedTransition(false, 0.0f) == 0.0f) && (CrtScreen_GetSwitchedTransition(true, 1.0f) == 1.0f)
        && (CrtScreen_GetSwitchedTransition(false, 1.0f) == 1.0f) && (CrtScreen_GetSwitchedTransition(false, 2.0f) == 1.0f)
        && (CrtScreen_GetSwitchedTransition(true, -1.0f) == 0.0f) && (CrtScreen_GetSwitchedTransition(false, NAN) == 0.0f));
    // Switched off while the line opens or the picture settles: the collapse at the same open height, the whole width.
    bool IsCarriedOn = true;
    for (int Step = 0; Step <= SWEEP_STEPS; Step++)
    {
        float Progress = CRT_SCREEN_POWER_ON_LINE_END
            + ((1.0f - CRT_SCREEN_POWER_ON_LINE_END) * ((float)Step / (float)SWEEP_STEPS));
        CrtPowerShape Before = CrtScreen_GetPowerShape(true, Progress, Aspect);
        CrtPowerShape After = CrtScreen_GetPowerShape(false, CrtScreen_GetSwitchedTransition(false, Progress), Aspect);
        IsCarriedOn = IsCarriedOn && IsNear(After.OpenY, Before.OpenY) && (After.OpenX == 1.0f);
    }
    TEST_CHECK(run, IsCarriedOn);
    // Half way through the opening (its eased height three quarters of the way): half of the collapse still to run
    // before the line; and the collapse runs on from there.
    float Halfway = CRT_SCREEN_POWER_ON_LINE_END + ((CRT_SCREEN_POWER_ON_OPEN_END - CRT_SCREEN_POWER_ON_LINE_END) * 0.5f);
    float Collapsing = CrtScreen_GetSwitchedTransition(false, Halfway);
    TEST_CHECK(run, IsNear(Collapsing, 1.0f - (CRT_SCREEN_POWER_OFF_LINE_END * 0.5f)));
    float Later = CrtScreen_AdvancePower(Collapsing, false, CRT_SCREEN_DEFAULT_POWER_ON_SECONDS,
        CRT_SCREEN_DEFAULT_POWER_OFF_SECONDS, 0.02f);
    TEST_CHECK(run, CrtScreen_GetPowerShape(false, Later, Aspect).OpenY
        < (CrtScreen_GetPowerShape(false, Collapsing, Aspect).OpenY - 0.01f));
    // A line still brightening goes out as the line: the collapse's end, before it shrinks to the dot.
    const float Lines[] = { 0.01f, 0.1f, CRT_SCREEN_POWER_ON_LINE_END - 0.001f };
    for (size_t Index = 0U; Index < (sizeof(Lines) / sizeof(Lines[0])); Index++)
    {
        float Switched = CrtScreen_GetSwitchedTransition(false, Lines[Index]);
        CrtPowerShape After = CrtScreen_GetPowerShape(false, Switched, Aspect);
        TEST_CHECK(run, IsNear(Switched, 1.0f - CRT_SCREEN_POWER_OFF_LINE_END)
            && (After.OpenY == CRT_SCREEN_POWER_LINE) && (After.OpenX == 1.0f));
    }

    // Switched on while the picture collapses: the opening at the same open height.
    IsCarriedOn = true;
    for (int Step = 0; Step <= SWEEP_STEPS; Step++)
    {
        float Transition = 1.0f - (CRT_SCREEN_POWER_OFF_LINE_END * ((float)Step / (float)SWEEP_STEPS));
        CrtPowerShape Before = CrtScreen_GetPowerShape(false, Transition, Aspect);
        CrtPowerShape After = CrtScreen_GetPowerShape(true, CrtScreen_GetSwitchedTransition(true, Transition), Aspect);
        IsCarriedOn = IsCarriedOn && IsNear(After.OpenY, Before.OpenY) && (After.OpenX == 1.0f);
    }
    TEST_CHECK(run, IsCarriedOn);
    // A line shrinking to its dot comes back as the whole line about to open, as bright; a fading dot starts over, dark.
    float Shrinking = 1.0f - ((CRT_SCREEN_POWER_OFF_LINE_END + CRT_SCREEN_POWER_OFF_DOT_END) * 0.5f);
    CrtPowerShape Line = CrtScreen_GetPowerShape(true, CrtScreen_GetSwitchedTransition(true, Shrinking), Aspect);
    TEST_CHECK(run, (CrtScreen_GetPowerShape(false, Shrinking, Aspect).OpenX < 0.9f)
        && (CrtScreen_GetSwitchedTransition(true, Shrinking) == CRT_SCREEN_POWER_ON_LINE_END)
        && (Line.OpenY == CRT_SCREEN_POWER_LINE) && (Line.OpenX == 1.0f)
        && (Line.Brightness == CRT_SCREEN_POWER_MAX_BRIGHTNESS));
    float Fading = 1.0f - ((CRT_SCREEN_POWER_OFF_DOT_END + 1.0f) * 0.5f);
    TEST_CHECK(run, (CrtScreen_GetPowerShape(false, Fading, Aspect).Light > 0.0f)
        && (CrtScreen_GetSwitchedTransition(true, Fading) == 0.0f));
}

/* A mask tile's mean is taken in linear light (sRGB decoded with the shader's 2.2 before averaging), its alpha unread; a
 * channel the tile never lights is floored, and with nothing to average the mean is 1 (no change). */
static void AMaskTilesMeanIsTakenInLinearLight(TestRun* run)
{
    const unsigned char Pixels[] = { 255U, 0U, 128U, 255U, 0U, 255U, 0U, 0U };
    Vector3 Srgb = CrtScreen_GetMaskTileMean(Pixels, 2U, true);
    TEST_CHECK(run, IsNear(Srgb.x, 0.5f) && IsNear(Srgb.y, 0.5f)
        && IsNear(Srgb.z, (float)(pow(128.0 / 255.0, 2.2) / 2.0)));
    Vector3 Linear = CrtScreen_GetMaskTileMean(Pixels, 2U, false);
    TEST_CHECK(run, IsNear(Linear.x, 0.5f) && IsNear(Linear.y, 0.5f) && IsNear(Linear.z, (128.0f / 255.0f) / 2.0f));
    const unsigned char One[] = { 51U, 102U, 204U, 7U };
    Vector3 Single = CrtScreen_GetMaskTileMean(One, 1U, false);
    TEST_CHECK(run, IsNear(Single.x, 0.2f) && IsNear(Single.y, 0.4f) && IsNear(Single.z, 0.8f));
    const unsigned char Red[] = { 255U, 0U, 0U, 255U };
    Vector3 Floored = CrtScreen_GetMaskTileMean(Red, 1U, true);
    TEST_CHECK(run, (Floored.x == 1.0f) && (Floored.y == CRT_SCREEN_MIN_MASK_TILE_MEAN)
        && (Floored.z == CRT_SCREEN_MIN_MASK_TILE_MEAN));
    Vector3 Missing = CrtScreen_GetMaskTileMean(NULL, 4U, true);
    Vector3 Empty = CrtScreen_GetMaskTileMean(Pixels, 0U, true);
    TEST_CHECK(run, (Missing.x == 1.0f) && (Missing.y == 1.0f) && (Missing.z == 1.0f) && (Empty.x == 1.0f)
        && (Empty.y == 1.0f) && (Empty.z == 1.0f));
}


// Tests: the scanlines, the detail and the flicker.
/* The lines never span fewer than the least pixels (neither the drawn level nor the coarser one blending in), one row
 * each when the rows are large, and the choice runs on without a jump across each level's end. */
static void ScanlinesNeverDrawFinerThanTheLeastSpan(TestRun* run)
{
    bool IsWideEnough = true;
    for (int Step = 0; Step <= 400; Step++)
    {
        float RowsPerPixel = exp2f(-6.0f + (12.0f * (float)Step / 400.0f));
        CrtScanlineLevels Levels = CrtScreen_GetScanlineLevels(RowsPerPixel);
        float Span = Levels.RowsPerLine / RowsPerPixel;
        IsWideEnough = IsWideEnough && (Span >= (CRT_SCREEN_SCANLINE_MIN_PIXELS * 0.9999f))
            && (Levels.CoarseWeight >= 0.0f) && (Levels.CoarseWeight <= 1.0f);
    }
    TEST_CHECK(run, IsWideEnough);
    CrtScanlineLevels Large = CrtScreen_GetScanlineLevels(0.05f);
    TEST_CHECK(run, (Large.RowsPerLine == 1.0f) && (Large.CoarseWeight == 0.0f));
    // Across a level's end the lines are the coarser level's on both sides: all of it blended in just before, drawn on
    // its own just after.
    float End = 4.0f / CRT_SCREEN_SCANLINE_MIN_PIXELS; // lines of 4 rows span the least exactly
    CrtScanlineLevels JustBefore = CrtScreen_GetScanlineLevels(End * 0.9999f);
    CrtScanlineLevels JustAfter = CrtScreen_GetScanlineLevels(End * 1.0001f);
    TEST_CHECK(run, (JustBefore.RowsPerLine == 4.0f) && (JustBefore.CoarseWeight > 0.99f));
    TEST_CHECK(run, (JustAfter.RowsPerLine == 8.0f) && (JustAfter.CoarseWeight < 0.01f));
    // Early in a level's range only its own lines show.
    TEST_CHECK(run, CrtScreen_GetScanlineLevels(End * 0.6f).CoarseWeight == 0.0f);
    TEST_CHECK(run, (CrtScreen_GetScanlineLevels(0.0f).RowsPerLine == 1.0f)
        && (CrtScreen_GetScanlineLevels(-1.0f).RowsPerLine == 1.0f) && (CrtScreen_GetScanlineLevels(NAN).RowsPerLine == 1.0f));
}

/* The scanlines and the mask fade out while the picture is small on screen: none below the start, all from the end. */
static void TheDetailFadesWithThePicturesSize(TestRun* run)
{
    TEST_CHECK(run, CrtScreen_GetDetailFade(CRT_SCREEN_DETAIL_FADE_START * 0.5f) == 0.0f);
    TEST_CHECK(run, CrtScreen_GetDetailFade(CRT_SCREEN_DETAIL_FADE_START) == 0.0f);
    TEST_CHECK(run, CrtScreen_GetDetailFade(CRT_SCREEN_DETAIL_FADE_END) == 1.0f);
    TEST_CHECK(run, CrtScreen_GetDetailFade(CRT_SCREEN_DETAIL_FADE_END * 4.0f) == 1.0f);
    float Middle = CrtScreen_GetDetailFade(0.5f * (CRT_SCREEN_DETAIL_FADE_START + CRT_SCREEN_DETAIL_FADE_END));
    TEST_CHECK(run, IsNear(Middle, 0.5f));
}

/* The flicker stays within its strength, is steady at 0, and two screens do not wobble together. */
static void TheFlickerStaysWithinItsStrength(TestRun* run)
{
    bool IsWithin = true;
    size_t Different = 0U;
    for (int Step = 0; Step < 1000; Step++)
    {
        double Seconds = 0.0137 * (double)Step;
        float First = CrtScreen_GetFlicker(0.01f, Seconds, 70U);
        float Second = CrtScreen_GetFlicker(0.01f, Seconds, 71U);
        IsWithin = IsWithin && (fabsf(First - 1.0f) <= 0.0100001f) && (CrtScreen_GetFlicker(0.0f, Seconds, 70U) == 1.0f);
        Different += (fabsf(First - Second) > 1e-4f) ? 1U : 0U;
    }
    TEST_CHECK(run, IsWithin);
    TEST_CHECK(run, Different > 900U);
    // It wobbles at all.
    TEST_CHECK(run, fabsf(CrtScreen_GetFlicker(0.5f, 0.1, 0U) - CrtScreen_GetFlicker(0.5f, 0.2, 0U)) > 1e-3f);
}

/* The defaults are the header's, and the masks have their names (a texture's is its texture's). */
static void TheDefaultsAndTheMasksNames(TestRun* run)
{
    CrtScreenSettings Settings = CrtScreenSettings_CreateDefault();
    TEST_CHECK(run, (Settings.Curvature == CRT_SCREEN_DEFAULT_CURVATURE) && Settings.AreScanlinesEnabled
        && (Settings.ScanlineStrength == CRT_SCREEN_DEFAULT_SCANLINE_STRENGTH)
        && (Settings.Mask == CrtMaskKind_ApertureGrille) && (Settings.MaskStrength == CRT_SCREEN_DEFAULT_MASK_STRENGTH)
        && (Settings.Glow == CRT_SCREEN_DEFAULT_GLOW) && (Settings.Reflection == CRT_SCREEN_DEFAULT_REFLECTION)
        && (Settings.Flicker == CRT_SCREEN_DEFAULT_FLICKER) && (Settings.Vignette == CRT_SCREEN_DEFAULT_VIGNETTE)
        && (Settings.PowerOnSeconds == CRT_SCREEN_DEFAULT_POWER_ON_SECONDS)
        && (Settings.PowerOffSeconds == CRT_SCREEN_DEFAULT_POWER_OFF_SECONDS));
    TEST_CHECK(run, (CrtMaskKind_GetName(CrtMaskKind_ApertureGrille) != NULL)
        && (CrtMaskKind_GetName(CrtMaskKind_ShadowMask) != NULL) && (CrtMaskKind_GetName(CrtMaskKind_None) != NULL));
    TEST_CHECK(run, (CrtMaskKind_GetName(CrtMaskKind_Texture) == NULL)
        && (CrtMaskKind_GetName(CrtMaskKind_Count) == NULL) && (CrtMaskKind_GetName((CrtMaskKind)-1) == NULL));
}


// Public functions.
void CrtScreenTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_curvature_keeps_the_centre_and_is_the_identity_at_zero",
            TheCurvatureKeepsTheCentreAndIsTheIdentityAtZero },
        { "the_curvature_bends_the_corners_in_by_its_amount", TheCurvatureBendsTheCornersInByItsAmount },
        { "the_power_turns_on_and_off_over_its_times", ThePowerTurnsOnAndOffOverItsTimes },
        { "the_power_shows_a_line_opening_and_a_dot_fading", ThePowerShowsALineOpeningAndADotFading },
        { "the_power_animations_move_smoothly", ThePowerAnimationsMoveSmoothly },
        { "a_switch_half_way_carries_on_the_shape", ASwitchHalfWayCarriesOnTheShape },
        { "scanlines_never_draw_finer_than_the_least_span", ScanlinesNeverDrawFinerThanTheLeastSpan },
        { "the_detail_fades_with_the_pictures_size", TheDetailFadesWithThePicturesSize },
        { "the_flicker_stays_within_its_strength", TheFlickerStaysWithinItsStrength },
        { "a_mask_tiles_mean_is_taken_in_linear_light", AMaskTilesMeanIsTakenInLinearLight },
        { "the_defaults_and_the_masks_names", TheDefaultsAndTheMasksNames },
    };
    TestRun_RunSuite(run, "CrtScreen", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
