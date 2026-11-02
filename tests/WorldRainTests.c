#include "TestSuites.h"
#include "WorldRain.h"
#include "WorldWeatherLight.h"
#include "raylib/raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The box's half side, as the positions are compared with it. */
#define HALF_BOX ((float)(0.5 * WORLD_RAIN_BOX_SIZE))


// Types.
/* A synthetic rain map for the landing's bisection: open above a ground at GroundY, and (with HasRoof) covered below a roof
 * at RoofY over the whole column. */
typedef struct FieldStruct
{
    float GroundY;
    bool HasRoof;
    float RoofY;
} Field;

/* A wall in a slanting rain, as a rain map along the rain sees it: the ground at y 0, a wall from x 0 to Thickness up to
 * Height across every z; the rain falls toward +x, Slant metres sideways for every metre down. */
typedef struct LeeFieldStruct
{
    float Height;
    float Thickness;
    float Slant;
} LeeField;


// Static functions.
static WorldEnvironment MakeEnvironment(void)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    return Environment;
}

static bool IsNear(double a, double b, double tolerance)
{
    return fabs(a - b) <= tolerance;
}

/* The distance between two coordinates of the box's lattice, taking its wrap into account. */
static float WrappedDifference(float a, float b)
{
    float Box = (float)WORLD_RAIN_BOX_SIZE;
    float Difference = fmodf(a - b, Box);
    if (Difference > 0.5f * Box)
    {
        Difference -= Box;
    }
    else if (Difference < -0.5f * Box)
    {
        Difference += Box;
    }
    return Difference;
}

static bool IsOpen(const void* context, Vector3 point)
{
    const Field* Ground = context;
    if (Ground->HasRoof && (point.y < Ground->RoofY))
    {
        return false;
    }
    return point.y > Ground->GroundY;
}

/* Covered under the ground, inside the wall, and where the line from a point up the rain (x falling by Slant a metre up)
 * meets the wall: in its lee, the wall's rain shadow. */
static bool IsOpenInLee(const void* context, Vector3 point)
{
    const LeeField* Lee = context;
    if ((point.y < 0.0f) || ((point.x >= 0.0f) && (point.x <= Lee->Thickness) && (point.y <= Lee->Height)))
    {
        return false;
    }
    return !((point.x > Lee->Thickness) && ((point.y + (point.x - Lee->Thickness) / Lee->Slant) <= Lee->Height));
}

/* A seed of the unit box from three numbers (a fixed spread of test seeds). */
static Vector3 TestSeed(uint32_t index)
{
    return (Vector3)
    {
        (float)(WorldRain_Hash(index, 0U, 77U) >> 8) / 16777216.0f,
        (float)(WorldRain_Hash(index, 1U, 77U) >> 8) / 16777216.0f,
        (float)(WorldRain_Hash(index, 2U, 77U) >> 8) / 16777216.0f
    };
}


// Tests.
/* The rain rate is 100 p^3 mm/h, and the light rain takes gives a light rain's visibility of about 8 km, the rain preset's
 * about 1.5 km and a downpour's about 720 m (visibility = -ln(0.02) / extinction); no rain, no mist. */
static void TheRainRateAndItsMist(TestRun* run)
{
    TEST_CHECK(run, WorldRain_GetRainRate(0.0f) == 0.0f);
    TEST_CHECK(run, IsNear(WorldRain_GetRainRate(1.0f), WORLD_RAIN_DOWNPOUR_RATE, 1e-4));
    TEST_CHECK(run, IsNear(WorldRain_GetRainRate(0.3f), 2.7, 1e-4));
    TEST_CHECK(run, WorldRain_GetRainRate(2.0f) == WorldRain_GetRainRate(1.0f));
    TEST_CHECK(run, WorldRain_GetRainRate(-1.0f) == 0.0f);
    TEST_CHECK(run, (WorldRain_GetRainRate(NAN) == 0.0f) && (WorldRain_GetRainRate(INFINITY) == 0.0f));
    TEST_CHECK(run, WorldRain_GetMistExtinction(0.0f) == 0.0f);
    double Visibility = 3.912 / (double)WorldRain_GetMistExtinction(1.0f);
    TEST_CHECK(run, IsNear(Visibility, 720.0, 15.0));
    Visibility = 3.912 / (double)WorldRain_GetMistExtinction(0.7f);
    TEST_CHECK(run, IsNear(Visibility, 1470.0, 40.0));
    Visibility = 3.912 / (double)WorldRain_GetMistExtinction(0.3f);
    TEST_CHECK(run, IsNear(Visibility, 8000.0, 250.0));
    float Last = 0.0f;
    for (int Step = 1; Step <= 100; Step++)
    {
        float Extinction = WorldRain_GetMistExtinction((float)Step / 100.0f);
        TEST_CHECK(run, Extinction > Last);
        Last = Extinction;
    }
    // The mist reaches from the ground haze's base to the clouds' base, at least the minimum.
    TEST_CHECK(run, WorldRain_GetMistThickness(1500.0f, 0.0f) == 1500.0f);
    TEST_CHECK(run, WorldRain_GetMistThickness(1500.0f, 1490.0f) == WORLD_RAIN_MIN_MIST_THICKNESS);
    TEST_CHECK(run, (WorldRain_GetMistThickness(NAN, 0.0f) == WORLD_RAIN_MIN_MIST_THICKNESS)
        && (WorldRain_GetMistThickness(INFINITY, 0.0f) == WORLD_RAIN_MIN_MIST_THICKNESS)
        && (WorldRain_GetMistThickness(1500.0f, -INFINITY) == WORLD_RAIN_MIN_MIST_THICKNESS));
    TEST_CHECK(run, WorldRain_GetMistThickness(1.0e9f, 0.0f) == (float)WORLD_ENVIRONMENT_MAX_GROUND_HAZE_THICKNESS);
}

/* Without weather nothing of the rain shows and no map is needed; with it, the shares follow the precipitation times the
 * settings, the rain map looks up and upwind against the mean wind, and the drops drift with the gusts too. */
static void TheFrameFollowsTheEnvironment(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    WorldRainFrame Frame;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    TEST_CHECK(run, !Frame.AreStreaksVisible && !Frame.AreSplashesVisible && !Frame.IsMapNeeded);
    TEST_CHECK(run, (Frame.StreakShare == 0.0f) && (Frame.SplashShare == 0.0f) && (Frame.PuddleCoverage == 0.0f));
    TEST_CHECK(run, (Frame.TowardRain.x == 0.0f) && (Frame.TowardRain.y == 1.0f) && (Frame.TowardRain.z == 0.0f));
    TEST_CHECK(run, Frame.FallSpeed == WORLD_ENVIRONMENT_DEFAULT_RAIN_FALL_SPEED);

    // Wet without rain: the map keeps covered surfaces dry.
    Environment.Wetness = 0.5f;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    TEST_CHECK(run, Frame.IsMapNeeded && !Frame.AreStreaksVisible);

    Environment.Precipitation = 0.5f;
    Environment.RainStreakDensity = 1.5f;
    Environment.RainSplashDensity = 4.0f;
    Environment.WindDirection = (float)(0.5 * 3.14159265358979323846); // toward +X
    Environment.WindSpeed = 6.0f;
    Environment.WindGust = 2.0f;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    TEST_CHECK(run, Frame.AreStreaksVisible && Frame.AreSplashesVisible);
    TEST_CHECK(run, IsNear(Frame.StreakShare, 0.75, 1e-6) && (Frame.SplashShare == 1.0f));
    TEST_CHECK(run, IsNear(Frame.Wind.x, 8.0, 1e-5) && IsNear(Frame.MeanWind.x, 6.0, 1e-5));
    float Length = sqrtf(Frame.TowardRain.x * Frame.TowardRain.x + Frame.TowardRain.y * Frame.TowardRain.y
        + Frame.TowardRain.z * Frame.TowardRain.z);
    TEST_CHECK(run, IsNear(Length, 1.0, 1e-6));
    // Up and against the mean wind (not the gust): the slant's tangent is the wind over the fall.
    TEST_CHECK(run, (Frame.TowardRain.y > 0.0f) && (Frame.TowardRain.x < 0.0f));
    TEST_CHECK(run, IsNear(-Frame.TowardRain.x / Frame.TowardRain.y, 6.0 / WORLD_ENVIRONMENT_DEFAULT_RAIN_FALL_SPEED,
        1e-5));
    TEST_CHECK(run, IsNear(Frame.StreakExposure, WORLD_RAIN_EXPOSURE, 1e-9));

    // Non-finite settings count as their defaults, and the shares never pass 1.
    Environment.RainStreakDensity = NAN;
    Environment.RainFallSpeed = NAN;
    Environment.Precipitation = 1.0f;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    TEST_CHECK(run, (Frame.StreakShare == 1.0f) && (Frame.FallSpeed == WORLD_ENVIRONMENT_DEFAULT_RAIN_FALL_SPEED));
    Environment.RainStreakDensity = 1.5f;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    TEST_CHECK(run, Frame.StreakShare == 1.0f);

    // The look's settings: the streaks' length, the colour (linear), the puddles' amount and size (clamped to their
    // ranges), and the rain's rings and splashes by the precipitation.
    Environment.Precipitation = 0.25f;
    Environment.Wetness = 1.0f;
    Environment.RainStreakLength = 2.0f;
    Environment.RainColor = (Color){ 128, 200, 64, 255 };
    Environment.PuddleAmount = 0.5f;
    Environment.PuddleScale = 2.0f;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    TEST_CHECK(run, IsNear(Frame.StreakExposure, 2.0 * WORLD_RAIN_EXPOSURE, 1e-9));
    TEST_CHECK(run, IsNear(Frame.Color[0], pow(128.0 / 255.0, 2.2), 1e-6)
        && IsNear(Frame.Color[1], pow(200.0 / 255.0, 2.2), 1e-6) && IsNear(Frame.Color[2], pow(64.0 / 255.0, 2.2), 1e-6));
    TEST_CHECK(run, IsNear(Frame.PuddleCoverage, 0.5 * WORLD_RAIN_PUDDLE_MAX_COVERAGE, 1e-7)
        && IsNear(Frame.PuddleTile, 2.0 * WORLD_RAIN_PUDDLE_TILE, 1e-5));
    TEST_CHECK(run, IsNear(Frame.RippleStrength, 0.5, 1e-7) && IsNear(Frame.RippleRate, 1.125, 1e-6)
        && IsNear(Frame.SplashPeriod, WORLD_RAIN_SPLASH_PERIOD * 1.75, 1e-6));
    Environment.PuddleScale = 50.0f;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    TEST_CHECK(run, IsNear(Frame.PuddleTile, WORLD_ENVIRONMENT_MAX_PUDDLE_SCALE * WORLD_RAIN_PUDDLE_TILE, 1e-4));
    Environment.PuddleScale = 0.01f;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    TEST_CHECK(run, IsNear(Frame.PuddleTile, WORLD_ENVIRONMENT_MIN_PUDDLE_SCALE * WORLD_RAIN_PUDDLE_TILE, 1e-5));
    WorldEnvironment_Deconstruct(&Environment);
}

/* The rain falls from the clouds' base: its top is the atmosphere's ground plus the base; the streaks, the splashes and the
 * map run only while what they reach lies below it (a non-finite camera height counts as below); the rain fades out over
 * the last WORLD_RAIN_TOP_FADE metres below it. */
static void TheRainStopsAtTheCloudsBase(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    Environment.Precipitation = 0.7f;
    Environment.Wetness = 1.0f;
    Environment.CloudBase = 800.0f;
    Environment.HorizonGroundHeight = -20.0f;
    WorldRainFrame Frame;
    WorldRain_GetFrame(&Environment, 2.5, &Frame);
    TEST_CHECK(run, (Frame.TopY == 780.0) && Frame.AreStreaksVisible && Frame.AreSplashesVisible && Frame.IsMapNeeded);
    // The streaks' box reaches half a box below the camera, the splashes' search WORLD_RAIN_SPLASH_REACH, the map its
    // depth below its eye.
    double Half = 0.5 * WORLD_RAIN_BOX_SIZE;
    WorldRain_GetFrame(&Environment, 780.0 + Half - 0.01, &Frame);
    TEST_CHECK(run, Frame.AreStreaksVisible && !Frame.AreSplashesVisible && Frame.IsMapNeeded);
    WorldRain_GetFrame(&Environment, 780.0 + Half, &Frame);
    TEST_CHECK(run, !Frame.AreStreaksVisible);
    WorldRain_GetFrame(&Environment, 780.0 + (double)WORLD_RAIN_SPLASH_REACH - 0.01, &Frame);
    TEST_CHECK(run, Frame.AreSplashesVisible);
    double MapReach = (double)(WORLD_RAIN_MAP_DEPTH - WORLD_RAIN_MAP_EYE_DISTANCE);
    WorldRain_GetFrame(&Environment, 780.0 + MapReach - 0.01, &Frame);
    TEST_CHECK(run, Frame.IsMapNeeded);
    WorldRain_GetFrame(&Environment, 780.0 + MapReach, &Frame);
    TEST_CHECK(run, !Frame.IsMapNeeded && !Frame.AreStreaksVisible && !Frame.AreSplashesVisible);
    WorldRain_GetFrame(&Environment, NAN, &Frame);
    TEST_CHECK(run, Frame.AreStreaksVisible && Frame.AreSplashesVisible && Frame.IsMapNeeded);
    WorldEnvironment_Deconstruct(&Environment);

    // The fade below the top.
    TEST_CHECK(run, WorldRain_GetTopOpenness(100.0f - WORLD_RAIN_TOP_FADE - 1.0f, 100.0f) == 1.0f);
    TEST_CHECK(run, IsNear(WorldRain_GetTopOpenness(100.0f - 0.5f * WORLD_RAIN_TOP_FADE, 100.0f), 0.5, 1e-6));
    TEST_CHECK(run, (WorldRain_GetTopOpenness(100.0f, 100.0f) == 0.0f) && (WorldRain_GetTopOpenness(160.0f, 100.0f) == 0.0f));
    TEST_CHECK(run, (WorldRain_GetTopOpenness(NAN, 100.0f) == 1.0f) && (WorldRain_GetTopOpenness(5.0f, INFINITY) == 1.0f)
        && (WorldRain_GetTopOpenness(INFINITY, 100.0f) == 1.0f));
    float Last = 1.0f;
    for (int Step = 0; Step <= 100; Step++)
    {
        float Openness = WorldRain_GetTopOpenness(60.0f + 0.5f * (float)Step, 100.0f);
        TEST_CHECK(run, Openness <= Last);
        Last = Openness;
    }
}

/* A splash plays the splash animation once at its fps: its life is the frames (at most WORLD_RAIN_SPLASH_MAX_FRAMES) over
 * the fps, none without frames or an fps; the frames spread evenly over the life, the last one held at its end. */
static void SplashesPlayTheirAnimation(TestRun* run)
{
    TEST_CHECK(run, IsNear(WorldRain_GetSplashLife(4U, 20.0), 0.2, 1e-7));
    TEST_CHECK(run, IsNear(WorldRain_GetSplashLife(1U, 8.0), 0.125, 1e-7));
    TEST_CHECK(run, (WorldRain_GetSplashLife(0U, 20.0) == 0.0f) && (WorldRain_GetSplashLife(4U, 0.0) == 0.0f)
        && (WorldRain_GetSplashLife(4U, -1.0) == 0.0f) && (WorldRain_GetSplashLife(4U, NAN) == 0.0f)
        && (WorldRain_GetSplashLife(4U, INFINITY) == 0.0f));
    TEST_CHECK(run, IsNear(WorldRain_GetSplashLife(200U, 10.0), (double)WORLD_RAIN_SPLASH_MAX_FRAMES / 10.0, 1e-5));
    // Each of four frames shows for a quarter of a 0.2 s life.
    uint32_t Shown[4] = { 0U, 0U, 0U, 0U };
    for (int Step = 0; Step < 400; Step++)
    {
        uint32_t Frame = WorldRain_GetSplashFrame(((float)Step + 0.5f) * 0.0005f, 0.2f, 4U);
        TEST_CHECK(run, Frame < 4U);
        if (Frame < 4U)
        {
            Shown[Frame]++;
        }
    }
    TEST_CHECK(run, (Shown[0] == 100U) && (Shown[1] == 100U) && (Shown[2] == 100U) && (Shown[3] == 100U));
    TEST_CHECK(run, (WorldRain_GetSplashFrame(0.3f, 0.2f, 4U) == 3U) && (WorldRain_GetSplashFrame(0.0f, 0.2f, 4U) == 0U));
    TEST_CHECK(run, (WorldRain_GetSplashFrame(-1.0f, 0.2f, 4U) == 0U) && (WorldRain_GetSplashFrame(NAN, 0.2f, 4U) == 0U)
        && (WorldRain_GetSplashFrame(0.1f, 0.0f, 4U) == 0U) && (WorldRain_GetSplashFrame(0.1f, 0.2f, 0U) == 0U));
}

/* Every streak's copy lies in the box around the camera, near the world's origin and far from it. */
static void StreaksStayInTheBox(TestRun* run)
{
    const double Origins[][3] =
    {
        { 0.0, 0.0, 0.0 }, { 12.7, -3.0, 44.4 }, { -1234.5, 87.0, 9876.25 }, { 1.0e6 + 0.37, 2500.0, -7.5e6 },
        { 9.99e6, -9.99e6, 9.99e6 }
    };
    WorldRainDrift Drift = { 0 };
    for (size_t OriginIndex = 0U; OriginIndex < sizeof(Origins) / sizeof(Origins[0]); OriginIndex++)
    {
        Vector3Double Origin = { Origins[OriginIndex][0], Origins[OriginIndex][1], Origins[OriginIndex][2] };
        Drift.Wind.X = 3.0 * (double)OriginIndex;
        Drift.Wind.Z = -7.0 * (double)OriginIndex;
        Drift.Fall = 37.0 * (double)OriginIndex;
        float Anchor[3];
        float Fall = 0.0f;
        WorldRain_GetStreakAnchor(&Drift, Origin, Anchor, &Fall);
        for (int Axis = 0; Axis < 3; Axis++)
        {
            TEST_CHECK(run, (Anchor[Axis] >= 0.0f) && (Anchor[Axis] < (float)WORLD_RAIN_BOX_SIZE));
        }
        TEST_CHECK(run, (Fall >= 0.0f) && (Fall < (float)(WORLD_RAIN_FALL_WRAP_BOXES * WORLD_RAIN_BOX_SIZE)));
        for (uint32_t Streak = 0U; Streak < 500U; Streak++)
        {
            Vector3 Position = WorldRain_GetStreakPosition(TestSeed(Streak), Streak % WORLD_RAIN_SPEED_CLASS_COUNT,
                Anchor, Fall);
            TEST_CHECK(run, (fabsf(Position.x) <= HALF_BOX) && (fabsf(Position.y) <= HALF_BOX)
                && (fabsf(Position.z) <= HALF_BOX));
        }
        // The box is centred on the camera: the streak whose seed lies at the anchor's own place in the lattice (its
        // class's share of the fall in) is at the camera.
        for (uint32_t Class = 0U; Class < WORLD_RAIN_SPEED_CLASS_COUNT; Class++)
        {
            double Box = WORLD_RAIN_BOX_SIZE;
            double AnchorY = (double)Anchor[1] + (double)WorldRain_GetSpeedFactor(Class) * (double)Fall;
            Vector3 Seed = { (float)((double)Anchor[0] / Box), (float)(fmod(AnchorY, Box) / Box),
                (float)((double)Anchor[2] / Box) };
            Vector3 Here = WorldRain_GetStreakPosition(Seed, Class, Anchor, Fall);
            TEST_CHECK(run, (fabsf(WrappedDifference(Here.x, 0.0f)) < 1e-3f)
                && (fabsf(WrappedDifference(Here.y, 0.0f)) < 1e-3f) && (fabsf(WrappedDifference(Here.z, 0.0f)) < 1e-3f));
        }
    }
}

/* The splashes' and the ripples' cycles run at the rain's rates now: each frame they advance by its seconds over the
 * splashes' period and times the ripples' rate, however the rain changes (a clock times a changing rate would race or
 * run backwards), and wrap by whole cycles. */
static void TheCyclesRunAtTheRainsRates(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    WorldRainDrift Drift = { 0 };
    Drift.SplashCycles = WORLD_RAIN_CYCLE_WRAP - 2.0;
    Drift.RippleCycles = WORLD_RAIN_CYCLE_WRAP - 5.0;
    const double Seconds = 1.0 / 60.0;
    for (int Step = 0; Step < 600; Step++)
    {
        // The rain rises and eases while it runs, across both wraps.
        Environment.Precipitation = 0.5f + 0.5f * sinf(0.02f * (float)Step);
        WorldRainFrame Frame;
        WorldRain_GetFrame(&Environment, 0.0, &Frame);
        WorldRainDrift Before = Drift;
        WorldRain_AdvanceDrift(&Drift, &Frame, Seconds);
        double Splashes = fmod(Drift.SplashCycles - Before.SplashCycles + WORLD_RAIN_CYCLE_WRAP, WORLD_RAIN_CYCLE_WRAP);
        double Ripples = fmod(Drift.RippleCycles - Before.RippleCycles + WORLD_RAIN_CYCLE_WRAP, WORLD_RAIN_CYCLE_WRAP);
        TEST_CHECK(run, IsNear(Splashes, Seconds / (double)Frame.SplashPeriod, 1e-9)
            && IsNear(Ripples, Seconds * (double)Frame.RippleRate, 1e-9));
        TEST_CHECK(run, (Drift.SplashCycles >= 0.0) && (Drift.SplashCycles < WORLD_RAIN_CYCLE_WRAP)
            && (Drift.RippleCycles >= 0.0) && (Drift.RippleCycles < WORLD_RAIN_CYCLE_WRAP));
    }
    // Both crossed their wraps.
    TEST_CHECK(run, (Drift.SplashCycles < 10.0) && (Drift.RippleCycles < 20.0));
    TEST_CHECK(run, (WORLD_RAIN_CYCLE_WRAP == floor(WORLD_RAIN_CYCLE_WRAP)) && (WORLD_RAIN_CYCLE_WRAP > 0.0));
    WorldEnvironment_Deconstruct(&Environment);
}

/* From frame to frame every streak moves by its own velocity times the seconds (the wind sideways, its class's share of
 * the fall downward), wrapping only by whole boxes: across the drift's own wraps too, for every speed class. */
static void StreaksMoveWithoutJumps(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    Environment.Precipitation = 0.8f;
    Environment.WindDirection = 0.6f;
    Environment.WindSpeed = 5.0f;
    WorldRainFrame Frame;
    WorldRain_GetFrame(&Environment, 0.0, &Frame);
    Vector3Double Origin = { 31.25, 4.5, -17.75 };
    WorldRainDrift Drift = { 0 };
    // Start just short of the fall's wrap, so the run crosses it.
    Drift.Fall = WORLD_RAIN_FALL_WRAP_BOXES * WORLD_RAIN_BOX_SIZE - 3.0;
    Drift.Wind.X = WORLD_RAIN_BOX_SIZE - 0.5;
    Drift.Wind.Z = WORLD_RAIN_BOX_SIZE - 0.2;
    const double Seconds = 1.0 / 60.0;
    for (int Step = 0; Step < 120; Step++)
    {
        float AnchorBefore[3];
        float FallBefore = 0.0f;
        WorldRain_GetStreakAnchor(&Drift, Origin, AnchorBefore, &FallBefore);
        WorldRain_AdvanceDrift(&Drift, &Frame, Seconds);
        float AnchorAfter[3];
        float FallAfter = 0.0f;
        WorldRain_GetStreakAnchor(&Drift, Origin, AnchorAfter, &FallAfter);
        for (uint32_t Streak = 0U; Streak < 64U; Streak++)
        {
            uint32_t Class = Streak % WORLD_RAIN_SPEED_CLASS_COUNT;
            Vector3 Seed = TestSeed(Streak + 1000U);
            Vector3 Before = WorldRain_GetStreakPosition(Seed, Class, AnchorBefore, FallBefore);
            Vector3 After = WorldRain_GetStreakPosition(Seed, Class, AnchorAfter, FallAfter);
            float Factor = WorldRain_GetSpeedFactor(Class);
            TEST_CHECK(run, IsNear(WrappedDifference(After.x, Before.x), (double)Frame.Wind.x * Seconds, 1e-3));
            TEST_CHECK(run, IsNear(WrappedDifference(After.y, Before.y),
                -(double)(Factor * Frame.FallSpeed) * Seconds, 1e-3));
            TEST_CHECK(run, IsNear(WrappedDifference(After.z, Before.z), (double)Frame.Wind.z * Seconds, 1e-3));
        }
        // The drift itself stays small: every part within its wrap.
        TEST_CHECK(run, (Drift.Wind.X >= 0.0) && (Drift.Wind.X < WORLD_RAIN_BOX_SIZE) && (Drift.Wind.Z >= 0.0)
            && (Drift.Wind.Z < WORLD_RAIN_BOX_SIZE) && (Drift.Fall >= 0.0)
            && (Drift.Fall < WORLD_RAIN_FALL_WRAP_BOXES * WORLD_RAIN_BOX_SIZE));
    }
    // A non-finite or negative step holds the drift.
    WorldRainDrift Held = Drift;
    WorldRain_AdvanceDrift(&Drift, &Frame, -1.0);
    WorldRain_AdvanceDrift(&Drift, &Frame, NAN);
    TEST_CHECK(run, memcmp(&Held, &Drift, sizeof(Drift)) == 0);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The streaks stand still in the world while the camera moves: a camera moved by d sees every streak d nearer (up to the
 * wrap), however far from the world's origin, and a camera moved by whole boxes sees the same rain. */
static void StreaksStandStillInTheWorld(TestRun* run)
{
    const double Bases[] = { 0.0, 4321.0, 2.0e6 + 0.123, -9.0e6 };
    WorldRainDrift Drift = { .Wind = { 5.5, 0.0, -2.25 }, .Fall = 17.0 };
    for (size_t BaseIndex = 0U; BaseIndex < sizeof(Bases) / sizeof(Bases[0]); BaseIndex++)
    {
        double Base = Bases[BaseIndex];
        Vector3Double Origin = { Base + 1.5, 20.0 - Base * 0.25, Base * 0.5 - 3.0 };
        Vector3Double Moved = { Origin.X + 0.731, Origin.Y - 0.25, Origin.Z + 2.0 };
        Vector3Double Boxes = { Origin.X + 3.0 * WORLD_RAIN_BOX_SIZE, Origin.Y - 7.0 * WORLD_RAIN_BOX_SIZE,
            Origin.Z + 11.0 * WORLD_RAIN_BOX_SIZE };
        float Anchor[3];
        float Fall = 0.0f;
        float MovedAnchor[3];
        float MovedFall = 0.0f;
        float BoxesAnchor[3];
        float BoxesFall = 0.0f;
        WorldRain_GetStreakAnchor(&Drift, Origin, Anchor, &Fall);
        WorldRain_GetStreakAnchor(&Drift, Moved, MovedAnchor, &MovedFall);
        WorldRain_GetStreakAnchor(&Drift, Boxes, BoxesAnchor, &BoxesFall);
        for (uint32_t Streak = 0U; Streak < 64U; Streak++)
        {
            uint32_t Class = Streak % WORLD_RAIN_SPEED_CLASS_COUNT;
            Vector3 Seed = TestSeed(Streak + 5000U);
            Vector3 Here = WorldRain_GetStreakPosition(Seed, Class, Anchor, Fall);
            Vector3 There = WorldRain_GetStreakPosition(Seed, Class, MovedAnchor, MovedFall);
            Vector3 Again = WorldRain_GetStreakPosition(Seed, Class, BoxesAnchor, BoxesFall);
            TEST_CHECK(run, IsNear(WrappedDifference(There.x, Here.x), -0.731, 1e-3)
                && IsNear(WrappedDifference(There.y, Here.y), 0.25, 1e-3)
                && IsNear(WrappedDifference(There.z, Here.z), -2.0, 1e-3));
            TEST_CHECK(run, IsNear(WrappedDifference(Again.x, Here.x), 0.0, 1e-3)
                && IsNear(WrappedDifference(Again.y, Here.y), 0.0, 1e-3)
                && IsNear(WrappedDifference(Again.z, Here.z), 0.0, 1e-3));
        }
    }
}

/* The box's fade: whole inside, gone at its faces; the speed classes are 3/4, 1 and 5/4 of the fall. */
static void TheBoxFadesAtItsFaces(TestRun* run)
{
    TEST_CHECK(run, WorldRain_GetBoxFade((Vector3){ 0.0f, 0.0f, 0.0f }) == 1.0f);
    TEST_CHECK(run, WorldRain_GetBoxFade((Vector3){ HALF_BOX - WORLD_RAIN_BOX_FADE - 0.01f, 0.0f, 0.0f }) == 1.0f);
    TEST_CHECK(run, WorldRain_GetBoxFade((Vector3){ 0.0f, -HALF_BOX, 0.0f }) == 0.0f);
    TEST_CHECK(run, WorldRain_GetBoxFade((Vector3){ 1.0f, 2.0f, HALF_BOX - 1.0f }) > 0.0f);
    TEST_CHECK(run, WorldRain_GetBoxFade((Vector3){ 1.0f, 2.0f, HALF_BOX - 1.0f }) < 1.0f);
    TEST_CHECK(run, (WorldRain_GetSpeedFactor(0U) == 0.75f) && (WorldRain_GetSpeedFactor(1U) == 1.0f)
        && (WorldRain_GetSpeedFactor(2U) == 1.25f) && (WorldRain_GetSpeedFactor(99U) == 1.25f));
    // Every class's fall over the wrap is a whole number of boxes, so the anchors' wrap never moves a streak.
    for (uint32_t Class = 0U; Class < WORLD_RAIN_SPEED_CLASS_COUNT; Class++)
    {
        double Boxes = (double)WorldRain_GetSpeedFactor(Class) * WORLD_RAIN_FALL_WRAP_BOXES;
        TEST_CHECK(run, Boxes == floor(Boxes));
    }
}

/* The rain map's box and lookups: its size and texels, a depth bias of a texel over its depth, a normal offset of a texel
 * and a half; its sides fade to open over its outer part, and beyond its far plane everything is open (the float depth
 * does not clamp the comparison); the slant of a fall. */
static void TheRainMapsBoxAndLookups(TestRun* run)
{
    WorldRenderCascadeShape Shape = WorldRain_GetMapShape();
    TEST_CHECK(run, (Shape.Size == WORLD_RAIN_MAP_SIZE) && (Shape.MapSize == WORLD_RAIN_MAP_TEXELS)
        && (Shape.LightDistance == WORLD_RAIN_MAP_EYE_DISTANCE) && (Shape.NearPlane == 0.0f)
        && (Shape.FarPlane == WORLD_RAIN_MAP_DEPTH));
    float DepthBias = 0.0f;
    float NormalOffset = 0.0f;
    WorldRain_GetMapOffsets(&DepthBias, &NormalOffset);
    float Texel = WORLD_RAIN_MAP_SIZE / (float)WORLD_RAIN_MAP_TEXELS;
    TEST_CHECK(run, IsNear(DepthBias, Texel / WORLD_RAIN_MAP_DEPTH, 1e-10));
    TEST_CHECK(run, IsNear(NormalOffset, 1.5 * Texel, 1e-7));
    TEST_CHECK(run, WorldRain_GetMapEdgeOpenness(0.0f, 0.0f, 0.0f) == 0.0f);
    TEST_CHECK(run, WorldRain_GetMapEdgeOpenness(-0.84f, 0.84f, 0.5f) == 0.0f);
    TEST_CHECK(run, WorldRain_GetMapEdgeOpenness(1.0f, 0.2f, 0.0f) == 1.0f);
    TEST_CHECK(run, WorldRain_GetMapEdgeOpenness(0.0f, -3.0f, 0.0f) == 1.0f);
    TEST_CHECK(run, WorldRain_GetMapEdgeOpenness(NAN, 0.0f, 0.0f) == 1.0f);
    // Along the rain: compared up to the far plane (and above the near plane, where everything is nearer the rain than
    // the map's first surface), open from it on.
    TEST_CHECK(run, (WorldRain_GetMapEdgeOpenness(0.1f, 0.0f, 0.999f) == 0.0f)
        && (WorldRain_GetMapEdgeOpenness(0.1f, 0.0f, -1.5f) == 0.0f));
    TEST_CHECK(run, (WorldRain_GetMapEdgeOpenness(0.1f, 0.0f, 1.0f) == 1.0f)
        && (WorldRain_GetMapEdgeOpenness(0.1f, 0.0f, 1.002f) == 1.0f)
        && (WorldRain_GetMapEdgeOpenness(0.1f, 0.0f, NAN) == 1.0f));
    float Last = 0.0f;
    for (int Step = 0; Step <= 100; Step++)
    {
        float Openness = WorldRain_GetMapEdgeOpenness(0.8f + 0.002f * (float)Step, 0.1f, 0.3f);
        TEST_CHECK(run, Openness >= Last);
        Last = Openness;
    }
    // A fall's slant: x and z over y; nothing for a fall that is level, upward or not finite.
    Vector3 Slanting = Vector3Normalize((Vector3){ -1.0f, 1.0f, 0.5f });
    Vector2 Slant = WorldRain_GetSlant(Slanting);
    TEST_CHECK(run, IsNear(Slant.x, -1.0, 1e-6) && IsNear(Slant.y, 0.5, 1e-6));
    Vector2 Straight = WorldRain_GetSlant((Vector3){ 0.0f, 1.0f, 0.0f });
    TEST_CHECK(run, (Straight.x == 0.0f) && (Straight.y == 0.0f));
    const Vector3 Bad[] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { NAN, 1.0f, 0.0f }, { 0.0f, INFINITY, 0.0f } };
    for (size_t Index = 0U; Index < sizeof(Bad) / sizeof(Bad[0]); Index++)
    {
        Vector2 None = WorldRain_GetSlant(Bad[Index]);
        TEST_CHECK(run, (None.x == 0.0f) && (None.y == 0.0f));
    }
}

/* The rain map is kept between its draws: a frame with no map draws one; then it is drawn again every
 * WORLD_RAIN_MAP_PERIOD frames, at once when the camera has moved WORLD_RAIN_MAP_MOVE metres from its box (far from the
 * world's origin too) or the rain has turned more than a degree, and when a place or a direction is not finite. A frame
 * reading the kept map through its box (WorldRenderSpace_GetCascadeAt) puts a point of the world where the drawing frame
 * put it. */
static void TheRainMapIsKeptBetweenDraws(TestRun* run)
{
    const Vector3 Down = { 0.0f, 1.0f, 0.0f };
    const double Origins[] = { 0.0, 1.0e7 + 0.37 };
    for (size_t Index = 0U; Index < sizeof(Origins) / sizeof(Origins[0]); Index++)
    {
        Vector3Double Origin = { Origins[Index], 12.0, -Origins[Index] };
        WorldRainMapKeep Keep;
        memset(&Keep, 0, sizeof(Keep));
        TEST_CHECK(run, WorldRain_IsMapDue(&Keep, Origin, Down));
        WorldRain_KeepMap(&Keep, Origin, Down);
        TEST_CHECK(run, Keep.HasMap && (Keep.FramesSince == 0U));
        // The frames after it read it, until the period is up.
        for (uint32_t Frame = 1U; Frame < WORLD_RAIN_MAP_PERIOD; Frame++)
        {
            TEST_CHECK(run, !WorldRain_IsMapDue(&Keep, Origin, Down) && (Keep.FramesSince == Frame));
        }
        TEST_CHECK(run, WorldRain_IsMapDue(&Keep, Origin, Down));
        // A move short of WORLD_RAIN_MAP_MOVE reads it; one past it draws again.
        WorldRain_KeepMap(&Keep, Origin, Down);
        Vector3Double Near = { Origin.X + 2.3, Origin.Y + 0.4, Origin.Z - 3.2 };
        TEST_CHECK(run, !WorldRain_IsMapDue(&Keep, Near, Down));
        WorldRain_KeepMap(&Keep, Origin, Down);
        Vector3Double Far = { Origin.X + 2.4, Origin.Y + 0.4, Origin.Z - 3.2 };
        TEST_CHECK(run, WorldRain_IsMapDue(&Keep, Far, Down));
        // A turn short of a degree reads it; one past it draws again.
        float Small = 0.9f * (float)(PI / 180.0);
        float Large = 1.1f * (float)(PI / 180.0);
        WorldRain_KeepMap(&Keep, Origin, Down);
        TEST_CHECK(run, !WorldRain_IsMapDue(&Keep, Origin, (Vector3){ sinf(Small), cosf(Small), 0.0f }));
        WorldRain_KeepMap(&Keep, Origin, Down);
        TEST_CHECK(run, WorldRain_IsMapDue(&Keep, Origin, (Vector3){ 0.0f, cosf(Large), sinf(Large) }));
        // What is not finite draws again.
        WorldRain_KeepMap(&Keep, Origin, Down);
        TEST_CHECK(run, WorldRain_IsMapDue(&Keep, (Vector3Double){ NAN, Origin.Y, Origin.Z }, Down));
        WorldRain_KeepMap(&Keep, Origin, Down);
        TEST_CHECK(run, WorldRain_IsMapDue(&Keep, Origin, (Vector3){ NAN, 1.0f, 0.0f }));
    }
    // A keep that holds no map draws, whatever else it holds.
    WorldRainMapKeep Empty = { .HasMap = false, .Center = { 0.0, 0.0, 0.0 }, .TowardRain = Down, .FramesSince = 0U };
    TEST_CHECK(run, WorldRain_IsMapDue(&Empty, (Vector3Double){ 0.0, 0.0, 0.0 }, Down));
    // The count stops at its largest.
    WorldRainMapKeep Old = { .HasMap = true, .Center = { 0.0, 0.0, 0.0 }, .TowardRain = Down, .FramesSince = UINT32_MAX };
    TEST_CHECK(run, WorldRain_IsMapDue(&Old, (Vector3Double){ 0.0, 0.0, 0.0 }, Down) && (Old.FramesSince == UINT32_MAX));

    // A slanting rain far out: the frame that draws keeps its box, a later frame 2.5 m away reads it through that box.
    Vector3 Toward = Vector3Normalize((Vector3){ -0.36f, 0.68f, -0.63f });
    WorldRenderCascadeShape Shape = WorldRain_GetMapShape();
    WorldRenderCascadeGrid Grid;
    memset(&Grid, 0, sizeof(Grid));
    Vector3Double First = { 61440.25, 3.5, -122880.75 };
    WorldRenderCascade Drawn = WorldRenderSpace_StepCascade(&Grid, First, Toward, &Shape);
    WorldRainMapKeep Keep;
    memset(&Keep, 0, sizeof(Keep));
    TEST_CHECK(run, WorldRain_IsMapDue(&Keep, First, Toward));
    WorldRain_KeepMap(&Keep, Drawn.Anchor, Toward);
    Vector3Double Later = { First.X + 1.5, First.Y + 0.25, First.Z + 1.9 };
    TEST_CHECK(run, !WorldRain_IsMapDue(&Keep, Later, Toward));
    WorldRenderCascade Read = WorldRenderSpace_GetCascadeAt(Later, Keep.Center, Keep.TowardRain, &Shape);
    const double Points[][3] = { { 3.0, -2.5, 4.0 }, { -20.0, 6.0, 11.0 }, { 0.0, 0.0, 0.0 } };
    for (size_t Index = 0U; Index < sizeof(Points) / sizeof(Points[0]); Index++)
    {
        // The same point of the world in each frame's render space.
        Vector3Double World = { First.X + Points[Index][0], First.Y + Points[Index][1], First.Z + Points[Index][2] };
        Vector3 AtFirst = { (float)(World.X - First.X), (float)(World.Y - First.Y), (float)(World.Z - First.Z) };
        Vector3 AtLater = { (float)(World.X - Later.X), (float)(World.Y - Later.Y), (float)(World.Z - Later.Z) };
        Vector3 A = Vector3Transform(AtFirst, Drawn.ViewProjection);
        Vector3 B = Vector3Transform(AtLater, Read.ViewProjection);
        TEST_CHECK(run, (fabsf(A.x - B.x) < 1e-5f) && (fabsf(A.y - B.y) < 1e-5f) && (fabsf(A.z - B.z) < 1e-6f));
    }
}

/* A splash lands on the first surface on the way down (a roof before the ground under it), to within the bisection's
 * resolution; nothing when the column is covered at its top (under a roof above the search) or open at its bottom
 * (nothing to land on). */
static void SplashesLandOnTheFirstSurface(TestRun* run)
{
    float Resolution = 2.0f * WORLD_RAIN_SPLASH_REACH / (float)(1 << WORLD_RAIN_SPLASH_STEPS);
    const Vector2 Down = { 0.0f, 0.0f };
    const float Grounds[] = { -1.7f, 0.0f, 3.33f, -11.5f, 11.9f };
    for (size_t Index = 0U; Index < sizeof(Grounds) / sizeof(Grounds[0]); Index++)
    {
        Field Ground = { .GroundY = Grounds[Index], .HasRoof = false };
        Vector3 Landing = { 0.0f, 0.0f, 0.0f };
        TEST_CHECK(run, WorldRain_FindLanding(IsOpen, &Ground, 1.0f, 2.0f, Down, WORLD_RAIN_SPLASH_REACH,
            -WORLD_RAIN_SPLASH_REACH, &Landing));
        // The middle of the last interval: within half the resolution.
        TEST_CHECK(run, (fabsf(Landing.y - Grounds[Index]) <= 0.5f * Resolution + 1e-5f) && (Landing.x == 1.0f)
            && (Landing.z == 2.0f));
    }
    // A roof within the search: the drop lands on it, not on the ground under it.
    Field Roofed = { .GroundY = -1.5f, .HasRoof = true, .RoofY = 3.0f };
    Vector3 Landing = { 0.0f, 0.0f, 0.0f };
    TEST_CHECK(run, WorldRain_FindLanding(IsOpen, &Roofed, 0.0f, 0.0f, Down, WORLD_RAIN_SPLASH_REACH,
        -WORLD_RAIN_SPLASH_REACH, &Landing) && (fabsf(Landing.y - 3.0f) <= Resolution));
    // Under a roof above the search: covered all the way.
    Field Indoors = { .GroundY = -1.5f, .HasRoof = true, .RoofY = WORLD_RAIN_SPLASH_REACH + 5.0f };
    TEST_CHECK(run, !WorldRain_FindLanding(IsOpen, &Indoors, 0.0f, 0.0f, Down, WORLD_RAIN_SPLASH_REACH,
        -WORLD_RAIN_SPLASH_REACH, &Landing));
    Field Bottomless = { .GroundY = -50.0f, .HasRoof = false };
    TEST_CHECK(run, !WorldRain_FindLanding(IsOpen, &Bottomless, 0.0f, 0.0f, Down, WORLD_RAIN_SPLASH_REACH,
        -WORLD_RAIN_SPLASH_REACH, &Landing));
}

/* In a slanting rain a drop lands on a surface, never in the air: beside a 3 m wall in the rain preset's 47 degrees, the
 * lines along the rain through its lee meet the wall (its windward face or its top) and those past its rain shadow or
 * upwind of it the ground; along a vertical line the change from open to covered in the lee would be the shadow's edge,
 * in the air. A line along a slanting fall moves upwind as it rises. */
static void SplashesLandOnSurfacesInASlantingRain(TestRun* run)
{
    LeeField Wall = { .Height = 3.0f, .Thickness = 0.2f, .Slant = tanf(47.0f * (float)(PI / 180.0)) };
    Vector3 Toward = Vector3Normalize((Vector3){ -Wall.Slant, 1.0f, 0.0f });
    Vector2 Slant = WorldRain_GetSlant(Toward);
    float Step = 2.0f * WORLD_RAIN_SPLASH_REACH / (float)(1 << WORLD_RAIN_SPLASH_STEPS);
    float Across = Step * Wall.Slant;
    const float Grounds[] = { -4.0f, -0.5f, 0.5f, 1.25f, 2.0f, 2.75f, 3.4f, 4.5f, 8.0f };
    for (size_t Index = 0U; Index < sizeof(Grounds) / sizeof(Grounds[0]); Index++)
    {
        Vector3 Landing = { 0.0f, 0.0f, 0.0f };
        TEST_CHECK(run, WorldRain_FindLanding(IsOpenInLee, &Wall, Grounds[Index], 3.0f, Slant, WORLD_RAIN_SPLASH_REACH,
            -WORLD_RAIN_SPLASH_REACH, &Landing));
        bool IsOnGround = fabsf(Landing.y) <= Step;
        bool IsOnTop = (fabsf(Landing.y - Wall.Height) <= Step) && (Landing.x >= -Across)
            && (Landing.x <= Wall.Thickness + Across);
        bool IsOnFace = (fabsf(Landing.x) <= Across) && (Landing.y <= Wall.Height + Step);
        TEST_CHECK(run, IsOnGround || IsOnTop || IsOnFace);
        // On the line: its point at the landing's height, and at the line's own z.
        TEST_CHECK(run, IsNear(Landing.x, Grounds[Index] + Slant.x * Landing.y, 1e-4) && (Landing.z == 3.0f));
        // Upwind of the wall and past its rain shadow (3 m x the slant beyond its lee face) the ground; in the shadow the
        // wall.
        bool IsInShadow = (Grounds[Index] > 0.0f) && (Grounds[Index] < Wall.Thickness + Wall.Height * Wall.Slant);
        TEST_CHECK(run, IsInShadow ? !IsOnGround : IsOnGround);
    }
}

/* The splash grid's cells stand in the world: the first cell starts half the grid below the camera's cell, its corner in
 * render space within a cell of there, its hash coordinates wrapped; a camera moved by a cell moves them by one. */
static void TheSplashGridStandsInTheWorld(TestRun* run)
{
    const double Origins[][3] = { { 0.25, 1.0, 0.75 }, { -3.5, 0.0, 7.25 }, { 1.0e6 + 0.5, 0.0, -2.5e6 - 0.25 } };
    double Half = (double)(WORLD_RAIN_SPLASH_GRID / 2);
    for (size_t Index = 0U; Index < sizeof(Origins) / sizeof(Origins[0]); Index++)
    {
        Vector3Double Origin = { Origins[Index][0], Origins[Index][1], Origins[Index][2] };
        float Cell[2];
        float Corner[2];
        WorldRain_GetSplashGrid(Origin, Cell, Corner);
        for (int Axis = 0; Axis < 2; Axis++)
        {
            TEST_CHECK(run, (Cell[Axis] >= 0.0f) && (Cell[Axis] < (float)WORLD_RAIN_SPLASH_HASH_PERIOD)
                && (Cell[Axis] == floorf(Cell[Axis])));
            TEST_CHECK(run, (Corner[Axis] <= (float)(-Half * WORLD_RAIN_SPLASH_CELL) + 1e-4f)
                && (Corner[Axis] > (float)(-(Half + 1.0) * WORLD_RAIN_SPLASH_CELL) - 1e-4f));
        }
        Vector3Double Moved = { Origin.X + WORLD_RAIN_SPLASH_CELL, Origin.Y, Origin.Z - WORLD_RAIN_SPLASH_CELL };
        float MovedCell[2];
        float MovedCorner[2];
        WorldRain_GetSplashGrid(Moved, MovedCell, MovedCorner);
        TEST_CHECK(run, fmodf(MovedCell[0] - Cell[0] + (float)WORLD_RAIN_SPLASH_HASH_PERIOD,
            (float)WORLD_RAIN_SPLASH_HASH_PERIOD) == 1.0f);
        TEST_CHECK(run, fmodf(Cell[1] - MovedCell[1] + (float)WORLD_RAIN_SPLASH_HASH_PERIOD,
            (float)WORLD_RAIN_SPLASH_HASH_PERIOD) == 1.0f);
        TEST_CHECK(run, IsNear(MovedCorner[0], Corner[0], 1e-3) && IsNear(MovedCorner[1], Corner[1], 1e-3));
    }
}

/* The hash is deterministic, mixes every input and spreads its outputs evenly; the patterns' origins wrap by their
 * periods. */
static void TheHashAndThePatternsOrigins(TestRun* run)
{
    TEST_CHECK(run, WorldRain_Hash(1U, 2U, 3U) == WorldRain_Hash(1U, 2U, 3U));
    TEST_CHECK(run, WorldRain_Hash(1U, 2U, 3U) != WorldRain_Hash(2U, 1U, 3U));
    TEST_CHECK(run, WorldRain_Hash(0U, 0U, 1U) != WorldRain_Hash(0U, 1U, 0U));
    // Pinned: the GLSL twin (common/rain's rainHash) must give the same numbers (tests/ShaderSourceTests.c pins its
    // multipliers).
    TEST_CHECK(run, WorldRain_Hash(0U, 0U, 0U) == 0U);
    TEST_CHECK(run, (WorldRain_Hash(1U, 2U, 3U) == 0x04569F7CU) && (WorldRain_Hash(0U, 0U, 1U) == 0x6D8754E2U)
        && (WorldRain_Hash(4095U, 17U, 4U) == 0x9F8EB5B5U) && (WorldRain_Hash(123456U, 654321U, 99U) == 0x83F56217U));
    uint32_t Buckets[16] = { 0 };
    for (uint32_t Index = 0U; Index < 16000U; Index++)
    {
        Buckets[WorldRain_Hash(Index, Index * 3U, 7U) >> 28]++;
    }
    for (int Bucket = 0; Bucket < 16; Bucket++)
    {
        TEST_CHECK(run, (Buckets[Bucket] > 850U) && (Buckets[Bucket] < 1150U));
    }
    float Wrapped[2];
    WorldRain_GetPatternOrigin((Vector3Double){ 65.5, 9.0, -1.25 }, 64.0, Wrapped);
    TEST_CHECK(run, IsNear(Wrapped[0], 1.5, 1e-6) && IsNear(Wrapped[1], 62.75, 1e-5));
    WorldRain_GetPatternOrigin((Vector3Double){ 6.4e6 + 3.0, 0.0, -6.4e6 - 3.0 }, 64.0, Wrapped);
    TEST_CHECK(run, IsNear(Wrapped[0], 3.0, 1e-6) && IsNear(Wrapped[1], 61.0, 1e-5));
}

/* Puddles: none below a quarter wet, growing to their most soaked, times the world's amount; the threshold for a coverage
 * keeps that share of the mean of two even reads (checked over a fine lattice of read pairs). */
static void PuddlesGrowWithTheWetness(TestRun* run)
{
    TEST_CHECK(run, WorldRain_GetPuddleCoverage(0.0f, 1.0f) == 0.0f);
    TEST_CHECK(run, WorldRain_GetPuddleCoverage(WORLD_RAIN_PUDDLE_WETNESS, 1.0f) == 0.0f);
    TEST_CHECK(run, IsNear(WorldRain_GetPuddleCoverage(1.0f, 1.0f), WORLD_RAIN_PUDDLE_MAX_COVERAGE, 1e-7));
    TEST_CHECK(run, IsNear(WorldRain_GetPuddleCoverage(1.0f, 2.0f), 2.0 * WORLD_RAIN_PUDDLE_MAX_COVERAGE, 1e-6));
    TEST_CHECK(run, WorldRain_GetPuddleCoverage(1.0f, 10.0f) == 1.0f);
    TEST_CHECK(run, WorldRain_GetPuddleCoverage(1.0f, 0.0f) == 0.0f);
    TEST_CHECK(run, WorldRain_GetPuddleCoverage(NAN, 1.0f) == 0.0f);
    TEST_CHECK(run, WorldRain_GetPuddleCoverage(0.75f, 1.0f) > WorldRain_GetPuddleCoverage(0.5f, 1.0f));

    TEST_CHECK(run, WorldRain_GetPuddleThreshold(0.0f) == 1.0f);
    TEST_CHECK(run, WorldRain_GetPuddleThreshold(1.0f) == 0.0f);
    TEST_CHECK(run, IsNear(WorldRain_GetPuddleThreshold(0.5f), 0.5, 1e-6));
    const float Coverages[] = { 0.05f, 0.2f, 0.3f, WORLD_RAIN_PUDDLE_MAX_COVERAGE, 0.45f, 0.5f, 0.7f, 0.95f };
    const int Steps = 400;
    for (size_t Index = 0U; Index < sizeof(Coverages) / sizeof(Coverages[0]); Index++)
    {
        float Threshold = WorldRain_GetPuddleThreshold(Coverages[Index]);
        int Above = 0;
        for (int A = 0; A < Steps; A++)
        {
            for (int B = 0; B < Steps; B++)
            {
                float Mean = 0.5f * (((float)A + 0.5f) / (float)Steps + ((float)B + 0.5f) / (float)Steps);
                Above += (Mean > Threshold) ? 1 : 0;
            }
        }
        TEST_CHECK(run, IsNear((double)Above / (double)(Steps * Steps), Coverages[Index], 0.005));
    }
}

/* The bolt stands while a flash lasts (and its brightness is above 0): from the ground to the clouds' base toward the
 * strike, in render space; its light comes from its middle at the flash's on a level surface times its share. */
static void TheBoltStandsTowardTheStrike(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment();
    float Flash[3] = { 0.08f, 0.09f, 0.1f };
    WorldRainBolt Bolt;
    WorldRain_GetBolt(&Environment, Flash, 1.7, 4U, &Bolt);
    TEST_CHECK(run, !Bolt.IsVisible && (Bolt.Irradiance[0] == 0.0f));

    Environment.LightningFlash = 0.5f;
    Environment.LightningDistance = 2000.0f;
    Environment.LightningDirection = (Vector3){ 3.0f, 0.0f, 4.0f };
    Environment.CloudBase = 1200.0f;
    Environment.HorizonGroundHeight = -5.0f;
    WorldRain_GetBolt(&Environment, Flash, 101.5, 4U, &Bolt);
    TEST_CHECK(run, Bolt.IsVisible);
    TEST_CHECK(run, IsNear(Bolt.Foot.x, 1200.0, 1e-3) && IsNear(Bolt.Foot.z, 1600.0, 1e-3)
        && IsNear(Bolt.Foot.y, -106.5, 1e-4));
    TEST_CHECK(run, IsNear(Bolt.Top.y, 1093.5, 1e-3) && (Bolt.Top.x == Bolt.Foot.x) && (Bolt.Top.z == Bolt.Foot.z));
    TEST_CHECK(run, Bolt.Variant < 4U);
    TEST_CHECK(run, IsNear(Bolt.Radiance, WORLD_RAIN_BOLT_RADIANCE * 0.5, 1e-4));
    float Length = sqrtf(Bolt.Direction.x * Bolt.Direction.x + Bolt.Direction.y * Bolt.Direction.y
        + Bolt.Direction.z * Bolt.Direction.z);
    TEST_CHECK(run, IsNear(Length, 1.0, 1e-5));
    TEST_CHECK(run, IsNear(Bolt.Direction.y / sqrtf(Bolt.Direction.x * Bolt.Direction.x
        + Bolt.Direction.z * Bolt.Direction.z), 0.5 * (-106.5 + 1093.5) / 2000.0, 1e-4));
    TEST_CHECK(run, IsNear(Bolt.Irradiance[2], 0.1 * WORLD_RAIN_BOLT_LIGHT_SHARE, 1e-6));
    // The same strike picks the same bolt; the nearest distance counts for nearer ones.
    WorldRainBolt Again;
    WorldRain_GetBolt(&Environment, Flash, 101.5, 4U, &Again);
    TEST_CHECK(run, (Again.Variant == Bolt.Variant) && (Again.IsMirrored == Bolt.IsMirrored));
    Environment.LightningDistance = 10.0f;
    WorldRain_GetBolt(&Environment, Flash, 0.0, 4U, &Bolt);
    TEST_CHECK(run, IsNear(sqrtf(Bolt.Foot.x * Bolt.Foot.x + Bolt.Foot.z * Bolt.Foot.z),
        WORLD_WEATHER_FLASH_NEAREST_DISTANCE, 1e-3));
    // Too faint a flash, or no brightness: none.
    Environment.LightningFlash = 0.5f * WORLD_RAIN_BOLT_MIN_FLASH;
    WorldRain_GetBolt(&Environment, Flash, 0.0, 4U, &Bolt);
    TEST_CHECK(run, !Bolt.IsVisible);
    Environment.LightningFlash = 1.0f;
    Environment.LightningBoltBrightness = 0.0f;
    WorldRain_GetBolt(&Environment, Flash, 0.0, 4U, &Bolt);
    TEST_CHECK(run, !Bolt.IsVisible);
    Environment.LightningBoltBrightness = 2.0f;
    WorldRain_GetBolt(&Environment, Flash, 0.0, 4U, &Bolt);
    TEST_CHECK(run, Bolt.IsVisible && IsNear(Bolt.Irradiance[0], 0.16 * WORLD_RAIN_BOLT_LIGHT_SHARE, 1e-6));
    // A negative light is none.
    float Negative[3] = { -0.5f, 0.09f, NAN };
    WorldRain_GetBolt(&Environment, Negative, 0.0, 4U, &Bolt);
    TEST_CHECK(run, (Bolt.Irradiance[0] == 0.0f) && (Bolt.Irradiance[2] == 0.0f) && (Bolt.Irradiance[1] > 0.0f));
    // The frame comes from the animation's frames: one frame, the first; none, the first too, and the bolt still lights.
    WorldRain_GetBolt(&Environment, Flash, 0.0, 1U, &Bolt);
    TEST_CHECK(run, Bolt.IsVisible && (Bolt.Variant == 0U));
    WorldRain_GetBolt(&Environment, Flash, 0.0, 0U, &Bolt);
    TEST_CHECK(run, Bolt.IsVisible && (Bolt.Variant == 0U) && (Bolt.Irradiance[1] > 0.0f));
    // Strikes at one distance in different directions show different bolts.
    Environment.LightningDistance = 3000.0f;
    uint32_t Seen = 0U;
    for (int Direction = 0; Direction < 16; Direction++)
    {
        float Angle = 0.39f * (float)Direction;
        Environment.LightningDirection = (Vector3){ sinf(Angle), 0.0f, cosf(Angle) };
        WorldRain_GetBolt(&Environment, Flash, 0.0, 4U, &Bolt);
        TEST_CHECK(run, Bolt.Variant < 4U);
        Seen |= 1U << (Bolt.Variant & 31U);
    }
    TEST_CHECK(run, (Seen != 0U) && ((Seen & (Seen - 1U)) != 0U));
    WorldEnvironment_Deconstruct(&Environment);
}


// Public functions.
void WorldRainTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_rain_rate_and_its_mist", TheRainRateAndItsMist },
        { "the_rain_stops_at_the_clouds_base", TheRainStopsAtTheCloudsBase },
        { "the_frame_follows_the_environment", TheFrameFollowsTheEnvironment },
        { "streaks_stay_in_the_box", StreaksStayInTheBox },
        { "streaks_move_without_jumps", StreaksMoveWithoutJumps },
        { "the_cycles_run_at_the_rains_rates", TheCyclesRunAtTheRainsRates },
        { "streaks_stand_still_in_the_world", StreaksStandStillInTheWorld },
        { "the_box_fades_at_its_faces", TheBoxFadesAtItsFaces },
        { "the_rain_maps_box_and_lookups", TheRainMapsBoxAndLookups },
        { "the_rain_map_is_kept_between_draws", TheRainMapIsKeptBetweenDraws },
        { "splashes_land_on_the_first_surface", SplashesLandOnTheFirstSurface },
        { "splashes_land_on_surfaces_in_a_slanting_rain", SplashesLandOnSurfacesInASlantingRain },
        { "splashes_play_their_animation", SplashesPlayTheirAnimation },
        { "the_splash_grid_stands_in_the_world", TheSplashGridStandsInTheWorld },
        { "the_hash_and_the_patterns_origins", TheHashAndThePatternsOrigins },
        { "puddles_grow_with_the_wetness", PuddlesGrowWithTheWetness },
        { "the_bolt_stands_toward_the_strike", TheBoltStandsTowardTheStrike },
    };
    TestRun_RunSuite(run, "WorldRain", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
