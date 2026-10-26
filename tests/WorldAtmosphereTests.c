#include "TestSuites.h"
#include "WorldAtmosphere.h"
#include "WorldEnvironment.h"
#include "wr/WRVectorMath.h"
#include <math.h>
#include <stdio.h>


/**
 * The atmosphere's model (T15, source/WorldAtmosphere.h): the air's coefficients from the environment, the height the sky
 * is seen from, and the lookup tables' layout, the C mirror of shaders/common/atmosphere.glsl (the same formulas; the
 * shader source tests pin the constants). The mappings round-trip, put their texels where the sky changes fastest, and
 * keep a millimetre's precision near the ground.
 */


// Macros.
/** Pi in double. */
#define TEST_ATMOSPHERE_PI 3.14159265358979323846
/** Degrees per radian. */
#define TEST_DEGREES_PER_RADIAN (180.0 / TEST_ATMOSPHERE_PI)
/** How close a mapping and its inverse must bring a coordinate back (unit coordinates and cosines). */
#define ROUND_TRIP_TOLERANCE 1e-9
/** How close a height must come back, relative to the height (and at least a micrometre). */
#define HEIGHT_TOLERANCE 1e-9


// Static functions: helpers.
static bool IsClose(double a, double b, double tolerance)
{
    return fabs(a - b) <= tolerance;
}

/* The height of the point @p distance along a ray, the slow way in long double: the point's distance from the planet's
 * centre minus the radius. */
static double ReferenceHeightAt(double height, double cosZenith, double distance)
{
    long double Radius = (long double)WORLD_ATMOSPHERE_PLANET_RADIUS + (long double)height;
    long double SinZenith = sqrtl(fmaxl(1.0L - (long double)cosZenith * (long double)cosZenith, 0.0L));
    long double X = (long double)distance * SinZenith;
    long double Y = Radius + (long double)distance * (long double)cosZenith;
    return (double)(sqrtl(X * X + Y * Y) - (long double)WORLD_ATMOSPHERE_PLANET_RADIUS);
}

static WorldAtmosphereRay AtmosphereRay(double height, double cosZenith)
{
    return (WorldAtmosphereRay){ .Height = height, .CosZenith = cosZenith };
}

static bool AreColorsClose(const float a[3], const float b[3], double relativeTolerance)
{
    for (int Channel = 0; Channel < 3; Channel++)
    {
        double Scale = fmax(fabs((double)b[Channel]), 1e-30);
        if (fabs((double)a[Channel] - (double)b[Channel]) > relativeTolerance * Scale)
        {
            return false;
        }
    }
    return true;
}


// Static functions: tests.
/* The defaults are the sky before T15 (the same Rayleigh and haze coefficients, the haze's albedo about 1 / 1.1), plus the
 * ozone layer and all of the multiple scattering. */
static void MediumDefaultsAreTheSkyBeforeTheTables(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    const float Rayleigh[3] = { 5.5e-6f, 13.0e-6f, 22.4e-6f };
    const float Extinction[3] = { 23.1e-6f, 23.1e-6f, 23.1e-6f };
    const float Scattering[3] = { 21.0e-6f, 21.0e-6f, 21.0e-6f };
    const float Ozone[3] = { 0.650e-6f, 1.881e-6f, 0.085e-6f };
    float GroundValue = powf(90.0f / 255.0f, 2.2f); // the default 90 grey, linear
    const float Ground[3] = { GroundValue, GroundValue, GroundValue };
    TEST_CHECK(run, AreColorsClose(Medium.RayleighScattering, Rayleigh, 1e-6));
    TEST_CHECK(run, AreColorsClose(Medium.MieExtinction, Extinction, 1e-6));
    TEST_CHECK(run, AreColorsClose(Medium.MieScattering, Scattering, 3e-3)); // 244 grey is linear 0.907, not 1 / 1.1
    TEST_CHECK(run, AreColorsClose(Medium.OzoneAbsorption, Ozone, 1e-6));
    TEST_CHECK(run, AreColorsClose(Medium.GroundAlbedo, Ground, 1e-3));
    TEST_CHECK(run, (Medium.MieHeight == 1200.0f) && (Medium.MieAnisotropy == 0.758f));
    TEST_CHECK(run, Medium.MultipleScattering == 1.0f);
    WorldEnvironment_Deconstruct(&Environment);
}

static void MediumFollowsTheSettings(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Default;
    WorldAtmosphere_GetMedium(&Environment, &Default);

    Environment.AirDensity = 2.0f;
    Environment.SkyTurbidity = 6.0f;
    Environment.HazeAlbedo = (Color){ 255, 0, 0, 255 };
    Environment.Ozone = 0.5f;
    Environment.HazeHeight = 300.0f;
    Environment.SunGlow = 0.9f;
    Environment.MultipleScattering = 0.0f;
    Environment.SkyGroundAlbedo = (Color){ 255, 255, 255, 255 };
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        TEST_CHECK(run, IsClose((double)Medium.RayleighScattering[Channel],
            2.0 * (double)Default.RayleighScattering[Channel], 1e-12));
        TEST_CHECK(run, IsClose((double)Medium.MieExtinction[Channel], 2.0 * (double)Default.MieExtinction[Channel],
            1e-12));
        TEST_CHECK(run, IsClose((double)Medium.OzoneAbsorption[Channel], 0.5 * (double)Default.OzoneAbsorption[Channel],
            1e-12));
        TEST_CHECK(run, Medium.GroundAlbedo[Channel] == 1.0f);
    }
    // A red haze scatters red only, and takes as much light as a grey one (its albedo is what it scatters of it).
    TEST_CHECK(run, IsClose((double)Medium.MieScattering[0], (double)Medium.MieExtinction[0], 1e-12));
    TEST_CHECK(run, (Medium.MieScattering[1] == 0.0f) && (Medium.MieScattering[2] == 0.0f));
    TEST_CHECK(run, (Medium.MieHeight == 300.0f) && (Medium.MieAnisotropy == 0.9f) && (Medium.MultipleScattering == 0.0f));

    // No haze at all: turbidity 0.
    Environment.SkyTurbidity = 0.0f;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    TEST_CHECK(run, (Medium.MieExtinction[0] == 0.0f) && (Medium.MieScattering[0] == 0.0f));
    WorldEnvironment_Deconstruct(&Environment);
}

/* What validation refuses never reaches the shaders: clamped to the rules, non-finite values as their defaults. */
static void MediumClampsWhatValidationRefuses(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.SkyTurbidity = NAN;
    Environment.HazeHeight = 1e9f;
    Environment.SunGlow = -1.0f;
    Environment.AirDensity = INFINITY;
    Environment.Ozone = -2.0f;
    Environment.MultipleScattering = 2.0f;
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    TEST_CHECK(run, IsClose((double)Medium.MieExtinction[1], 23.1e-6, 1e-12));
    TEST_CHECK(run, Medium.MieHeight == (float)WORLD_ENVIRONMENT_MAX_HAZE_HEIGHT);
    TEST_CHECK(run, Medium.MieAnisotropy == 0.0f);
    TEST_CHECK(run, IsClose((double)Medium.RayleighScattering[1], 13.0e-6, 1e-12)); // INFINITY counts as non-finite
    TEST_CHECK(run, Medium.OzoneAbsorption[1] == 0.0f);
    TEST_CHECK(run, Medium.MultipleScattering == 1.0f);
    Environment.HazeHeight = 0.0f;
    Environment.SunGlow = 5.0f;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    TEST_CHECK(run, Medium.MieHeight == (float)WORLD_ENVIRONMENT_MIN_HAZE_HEIGHT);
    TEST_CHECK(run, Medium.MieAnisotropy == (float)WORLD_ENVIRONMENT_MAX_SUN_GLOW);

    // Above the multiples' maximum; and NaN where a clamp alone would hide a wrong fallback: each takes its default.
    WorldEnvironment Defaults;
    WorldEnvironment_SetDefaults(&Defaults);
    Environment.AirDensity = 1000.0f;
    Environment.Ozone = 1000.0f;
    Environment.HazeHeight = NAN;
    Environment.SunGlow = NAN;
    Environment.MultipleScattering = NAN;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    TEST_CHECK(run, IsClose((double)Medium.RayleighScattering[1], 13.0e-6 * WORLD_ENVIRONMENT_MAX_AIR_MULTIPLE, 1e-9));
    TEST_CHECK(run, IsClose((double)Medium.OzoneAbsorption[1], 1.881e-6 * WORLD_ENVIRONMENT_MAX_AIR_MULTIPLE, 1e-9));
    TEST_CHECK(run, Medium.MieHeight == Defaults.HazeHeight);
    TEST_CHECK(run, Medium.MieAnisotropy == Defaults.SunGlow);
    TEST_CHECK(run, Medium.MultipleScattering == Defaults.MultipleScattering);
    Environment.AirDensity = NAN;
    Environment.Ozone = NAN;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    TEST_CHECK(run, IsClose((double)Medium.RayleighScattering[1], 13.0e-6 * (double)Defaults.AirDensity, 1e-12));
    TEST_CHECK(run, IsClose((double)Medium.OzoneAbsorption[1], 1.881e-6 * (double)Defaults.Ozone, 1e-12));

    // A haze far too thick to see through still gives finite coefficients (its rule has no maximum).
    Environment.SkyTurbidity = 3.4e38f;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    TEST_CHECK(run, isfinite(Medium.MieExtinction[0]) && isfinite(Medium.MieScattering[0])
        && (Medium.MieExtinction[0] > 1e30f));
    WorldEnvironment_Deconstruct(&Defaults);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The transmittance and multiple scattering tables are built again only when something they hold changes: not for the
 * haze's forward scattering nor the share of multiple scattering, which only the sky-view table uses. */
static void TablesAreStaleOnlyWhenTheirAirChanges(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Built;
    WorldAtmosphere_GetMedium(&Environment, &Built);
    WorldAtmosphereMedium Medium = Built;
    TEST_CHECK(run, !WorldAtmosphere_AreTablesStale(&Built, &Medium));
    Medium.MieAnisotropy = 0.5f;
    Medium.MultipleScattering = 0.0f;
    TEST_CHECK(run, !WorldAtmosphere_AreTablesStale(&Built, &Medium));

    float* const Fields[] =
    {
        &Medium.RayleighScattering[2], &Medium.MieScattering[0], &Medium.MieExtinction[1], &Medium.MieHeight,
        &Medium.OzoneAbsorption[1], &Medium.GroundAlbedo[2]
    };
    for (size_t Index = 0U; Index < (sizeof(Fields) / sizeof(Fields[0])); Index++)
    {
        Medium = Built;
        *Fields[Index] *= 1.001f;
        if (!TEST_CHECK(run, WorldAtmosphere_AreTablesStale(&Built, &Medium)))
        {
            printf("    field %zu\n", Index);
        }
    }
    WorldEnvironment_Deconstruct(&Environment);
}

/* The renderer builds the transmittance and multiple scattering tables on its first frame and then only when their air
 * changes, counting the builds (its status line); dropping the targets forgets them. */
static void TableRecordBuildsOnlyWhenNeeded(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    WorldAtmosphereTableRecord Record = { 0 };
    TEST_CHECK(run, WorldAtmosphere_UpdateTableRecord(&Record, &Medium));
    TEST_CHECK(run, Record.IsBuilt && (Record.BuildCount == 1U));
    TEST_CHECK(run, !WorldAtmosphere_UpdateTableRecord(&Record, &Medium));
    TEST_CHECK(run, !WorldAtmosphere_UpdateTableRecord(&Record, &Medium));
    Medium.MieAnisotropy = 0.9f; // only the sky-view table uses it
    TEST_CHECK(run, !WorldAtmosphere_UpdateTableRecord(&Record, &Medium));

    Environment.SkyTurbidity = 8.0f;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    TEST_CHECK(run, WorldAtmosphere_UpdateTableRecord(&Record, &Medium));
    TEST_CHECK(run, !WorldAtmosphere_UpdateTableRecord(&Record, &Medium));
    TEST_CHECK(run, Record.BuildCount == 2U);
    TEST_CHECK(run, Record.BuiltFor.MieExtinction[0] == Medium.MieExtinction[0]);

    Record.IsBuilt = false; // the targets were dropped
    TEST_CHECK(run, WorldAtmosphere_UpdateTableRecord(&Record, &Medium));
    TEST_CHECK(run, Record.IsBuilt && (Record.BuildCount == 3U));
    WorldEnvironment_Deconstruct(&Environment);
}

static void ViewHeightIsClamped(TestRun* run)
{
    TEST_CHECK(run, WorldAtmosphere_GetViewHeight(-5.0) == WORLD_ATMOSPHERE_MIN_VIEW_HEIGHT);
    TEST_CHECK(run, WorldAtmosphere_GetViewHeight(0.5) == WORLD_ATMOSPHERE_MIN_VIEW_HEIGHT);
    TEST_CHECK(run, WorldAtmosphere_GetViewHeight(1.7) == 1.7);
    TEST_CHECK(run, WorldAtmosphere_GetViewHeight(2000.0) == 2000.0);
    TEST_CHECK(run, WorldAtmosphere_GetViewHeight(1e7) == WORLD_ATMOSPHERE_MAX_VIEW_HEIGHT);
    TEST_CHECK(run, WorldAtmosphere_GetViewHeight(NAN) == WORLD_ATMOSPHERE_MIN_VIEW_HEIGHT);
    TEST_CHECK(run, WorldAtmosphere_GetViewHeight(INFINITY) == WORLD_ATMOSPHERE_MIN_VIEW_HEIGHT);
    TEST_CHECK(run, WORLD_ATMOSPHERE_MAX_VIEW_HEIGHT < (WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS));
}

/* Heights along a ray need no radius minus the planet's: a millimetre's precision near the ground, as far as rays go. */
static void HeightsAlongRaysAreExact(TestRun* run)
{
    const double Heights[] = { 0.0, 1.0, 1.7, 100.0, 1234.5, 8000.0, 50000.0 };
    const double Cosines[] = { -1.0, -0.3, -0.001, 0.0, 0.0007, 0.02, 0.5, 1.0 };
    const double Distances[] = { 0.0, 0.25, 30.0, 4654.0, 1e5, 1.2e6 };
    for (size_t H = 0U; H < (sizeof(Heights) / sizeof(Heights[0])); H++)
    {
        for (size_t C = 0U; C < (sizeof(Cosines) / sizeof(Cosines[0])); C++)
        {
            for (size_t D = 0U; D < (sizeof(Distances) / sizeof(Distances[0])); D++)
            {
                double Expected = ReferenceHeightAt(Heights[H], Cosines[C], Distances[D]);
                double Got = WorldAtmosphere_GetHeightAt(AtmosphereRay(Heights[H], Cosines[C]), Distances[D]);
                if (!TEST_CHECK(run, IsClose(Got, Expected, fmax(1e-12 * fabs(Expected), 1e-6))))
                {
                    printf("    h %.3f cos %.4f t %.1f: %.9f against %.9f\n", Heights[H], Cosines[C], Distances[D], Got,
                        Expected);
                }
            }
        }
    }
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetHeightAt(AtmosphereRay(1.7, 0.3), 0.0), 1.7, 1e-12));
}

/* The horizon from 1.7 m is 4.65 km away and dips 0.042 degrees; the top of the atmosphere is 100 km up. */
static void HorizonAndDistances(TestRun* run)
{
    TEST_CHECK(run, WorldAtmosphere_GetHorizonCos(0.0) == 0.0);
    double Cos = WorldAtmosphere_GetHorizonCos(1.7);
    TEST_CHECK(run, IsClose(Cos, -sqrt(2.0 * 1.7 / WORLD_ATMOSPHERE_PLANET_RADIUS), 1e-9));
    TEST_CHECK(run, IsClose(asin(-Cos) * TEST_DEGREES_PER_RADIAN, 0.0418, 1e-3));
    // Just below the horizon the ray meets the ground at the horizon's distance; just above it misses.
    double ToGround = WorldAtmosphere_GetDistanceToGround(AtmosphereRay(1.7, Cos - 1e-12));
    TEST_CHECK(run, IsClose(ToGround, sqrt(2.0 * WORLD_ATMOSPHERE_PLANET_RADIUS * 1.7), 2.0));
    TEST_CHECK(run, (ToGround > 4640.0) && (ToGround < 4670.0));
    TEST_CHECK(run, WorldAtmosphere_GetDistanceToGround(AtmosphereRay(1.7, Cos + 1e-6)) == -1.0);
    TEST_CHECK(run, WorldAtmosphere_GetDistanceToGround(AtmosphereRay(1.7, 0.5)) == -1.0);
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetDistanceToGround(AtmosphereRay(100.0, -1.0)), 100.0, 1e-6));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetDistanceToTop(AtmosphereRay(0.0, 1.0)), 100000.0, 1e-6));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetDistanceToTop(AtmosphereRay(1000.0, 1.0)), 99000.0, 1e-6));
    double AlongHorizon = sqrt(WORLD_ATMOSPHERE_TOP_RADIUS * WORLD_ATMOSPHERE_TOP_RADIUS
        - WORLD_ATMOSPHERE_PLANET_RADIUS * WORLD_ATMOSPHERE_PLANET_RADIUS);
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetDistanceToTop(AtmosphereRay(0.0, 0.0)), AlongHorizon, 1e-6));
    TEST_CHECK(run, IsClose(AlongHorizon, 1133225.5, 1.0));
}

static void TransmittanceMappingRoundTrips(TestRun* run)
{
    // (At the top of the atmosphere itself every ray that climbs has no air left: they all share the first column.)
    const double Heights[] = { 0.0, 1.7, 25.0, 100.0, 1000.0, 8000.0, 30000.0, 99000.0 };
    for (size_t H = 0U; H < (sizeof(Heights) / sizeof(Heights[0])); H++)
    {
        double Horizon = WorldAtmosphere_GetHorizonCos(Heights[H]);
        for (int Step = 0; Step <= 20; Step++)
        {
            double CosZenith = Horizon + (1.0 - Horizon) * (double)Step / 20.0;
            Vector2Double Uv = WorldAtmosphere_GetTransmittanceUv(AtmosphereRay(Heights[H], CosZenith));
            WorldAtmosphereRay Back = WorldAtmosphere_GetTransmittanceRay(Uv);
            bool IsBack = IsClose(Back.Height, Heights[H], fmax(HEIGHT_TOLERANCE * Heights[H], 1e-6))
                && IsClose(Back.CosZenith, CosZenith, 1e-8);
            Vector2Double Again = WorldAtmosphere_GetTransmittanceUv(Back);
            bool IsUvBack = IsClose(Again.X, Uv.X, ROUND_TRIP_TOLERANCE) && IsClose(Again.Y, Uv.Y, ROUND_TRIP_TOLERANCE);
            if (!TEST_CHECK(run, IsBack && IsUvBack))
            {
                printf("    h %.3f cos %.6f: back h %.6f cos %.9f\n", Heights[H], CosZenith, Back.Height, Back.CosZenith);
            }
        }
    }

    // The corners: straight up in the first column, the horizon in the last; the ground in the first row, the top of the
    // atmosphere in the last. A ray below the horizon gets the horizon's column.
    double FirstColumn = WorldAtmosphere_UnitToTexel(0.0, WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH);
    double LastColumn = WorldAtmosphere_UnitToTexel(1.0, WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH);
    double FirstRow = WorldAtmosphere_UnitToTexel(0.0, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT);
    double LastRow = WorldAtmosphere_UnitToTexel(1.0, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT);
    WorldAtmosphereRay Zenith = WorldAtmosphere_GetTransmittanceRay(Vector2Double_Create(FirstColumn, FirstRow));
    WorldAtmosphereRay Horizon = WorldAtmosphere_GetTransmittanceRay(Vector2Double_Create(LastColumn, FirstRow));
    WorldAtmosphereRay Top = WorldAtmosphere_GetTransmittanceRay(Vector2Double_Create(LastColumn, LastRow));
    TEST_CHECK(run, IsClose(Zenith.CosZenith, 1.0, 1e-12) && IsClose(Zenith.Height, 0.0, 1e-9));
    TEST_CHECK(run, IsClose(Horizon.CosZenith, 0.0, 1e-9) && IsClose(Horizon.Height, 0.0, 1e-9));
    TEST_CHECK(run, IsClose(Top.Height, 100000.0, 1e-6)
        && IsClose(Top.CosZenith, WorldAtmosphere_GetHorizonCos(100000.0), 1e-9));
    Vector2Double Below = WorldAtmosphere_GetTransmittanceUv(AtmosphereRay(1.7, -0.5));
    TEST_CHECK(run, IsClose(Below.X, LastColumn, 1e-12));
    // The rows are dense near the ground: the first above it is 25 m up, the next 101 m.
    double SecondRow = WorldAtmosphere_UnitToTexel(1.0 / (WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT - 1),
        WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT);
    WorldAtmosphereRay Low = WorldAtmosphere_GetTransmittanceRay(Vector2Double_Create(FirstColumn, SecondRow));
    TEST_CHECK(run, (Low.Height > 20.0) && (Low.Height < 30.0));
}

static void MultiscatterMappingRoundTrips(TestRun* run)
{
    for (int Across = 0; Across < WORLD_ATMOSPHERE_MULTISCATTER_SIZE; Across++)
    {
        for (int Up = 0; Up < WORLD_ATMOSPHERE_MULTISCATTER_SIZE; Up++)
        {
            Vector2Double Uv = Vector2Double_Create(((double)Across + 0.5) / WORLD_ATMOSPHERE_MULTISCATTER_SIZE,
                ((double)Up + 0.5) / WORLD_ATMOSPHERE_MULTISCATTER_SIZE);
            WorldAtmosphereRay Point = WorldAtmosphere_GetMultiscatterRay(Uv);
            Vector2Double Back = WorldAtmosphere_GetMultiscatterUv(Point);
            TEST_CHECK(run, IsClose(Back.X, Uv.X, ROUND_TRIP_TOLERANCE) && IsClose(Back.Y, Uv.Y, ROUND_TRIP_TOLERANCE));
        }
    }
    // The corners: the light straight below and the ground; the light straight above and the top.
    double First = WorldAtmosphere_UnitToTexel(0.0, WORLD_ATMOSPHERE_MULTISCATTER_SIZE);
    double Last = WorldAtmosphere_UnitToTexel(1.0, WORLD_ATMOSPHERE_MULTISCATTER_SIZE);
    WorldAtmosphereRay Bottom = WorldAtmosphere_GetMultiscatterRay(Vector2Double_Create(First, First));
    WorldAtmosphereRay Top = WorldAtmosphere_GetMultiscatterRay(Vector2Double_Create(Last, Last));
    TEST_CHECK(run, IsClose(Bottom.CosZenith, -1.0, 1e-12) && IsClose(Bottom.Height, 0.0, 1e-9));
    TEST_CHECK(run, IsClose(Top.CosZenith, 1.0, 1e-12) && IsClose(Top.Height, 100000.0, 1e-6));
}

static void SkyViewMappingRoundTrips(TestRun* run)
{
    const double Heights[] = { 1.0, 1.7, 300.0, 2000.0, 50000.0, WORLD_ATMOSPHERE_MAX_VIEW_HEIGHT };
    for (size_t H = 0U; H < (sizeof(Heights) / sizeof(Heights[0])); H++)
    {
        for (int Across = 0; Across <= 16; Across++)
        {
            for (int Up = 0; Up <= 64; Up++)
            {
                Vector2Double Unit = Vector2Double_Create((double)Across / 16.0, (double)Up / 64.0);
                WorldAtmosphereView View = WorldAtmosphere_GetSkyViewDirection(Heights[H], Unit);
                Vector2Double Back = WorldAtmosphere_GetSkyViewUnit(Heights[H], View);
                // Very near the horizon the quadratic's slope is steep: a cosine off by 1e-16 moves the unit more there.
                if (!TEST_CHECK(run, IsClose(Back.X, Unit.X, 1e-7) && IsClose(Back.Y, Unit.Y, 1e-7)))
                {
                    printf("    height %.1f unit (%.4f, %.4f): back (%.9f, %.9f)\n", Heights[H], Unit.X, Unit.Y, Back.X,
                        Back.Y);
                }
            }
        }
        // And from directions: every zenith angle, a few azimuths.
        for (int Degrees = 0; Degrees <= 180; Degrees += 5)
        {
            WorldAtmosphereView View = { .CosZenith = cos((double)Degrees / TEST_DEGREES_PER_RADIAN), .CosLightAzimuth = 0.3 };
            WorldAtmosphereView Back = WorldAtmosphere_GetSkyViewDirection(Heights[H],
                WorldAtmosphere_GetSkyViewUnit(Heights[H], View));
            TEST_CHECK(run, IsClose(Back.CosZenith, View.CosZenith, ROUND_TRIP_TOLERANCE)
                && IsClose(Back.CosLightAzimuth, View.CosLightAzimuth, ROUND_TRIP_TOLERANCE));
        }
    }
}

/* Hillaire's mapping puts the rows where the sky changes fastest: next to the horizon, which is the half's middle. */
static void SkyViewIsDenseNearTheHorizon(TestRun* run)
{
    double ViewHeight = 1.7;
    double Horizon = WorldAtmosphere_GetHorizonCos(ViewHeight);
    WorldAtmosphereView AtHalf = WorldAtmosphere_GetSkyViewDirection(ViewHeight, Vector2Double_Create(0.0, 0.5));
    TEST_CHECK(run, IsClose(AtHalf.CosZenith, Horizon, 1e-12));
    WorldAtmosphereView Zenith = WorldAtmosphere_GetSkyViewDirection(ViewHeight, Vector2Double_Create(0.0, 0.0));
    WorldAtmosphereView Nadir = WorldAtmosphere_GetSkyViewDirection(ViewHeight, Vector2Double_Create(0.0, 1.0));
    TEST_CHECK(run, IsClose(Zenith.CosZenith, 1.0, 1e-12) && IsClose(Nadir.CosZenith, -1.0, 1e-12));

    // The two rows on either side of the horizon are within 0.05 degrees of it; ten rows up is still within 3.5 degrees.
    double Rows = WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT - 1.0;
    double Above = WorldAtmosphere_GetSkyViewDirection(ViewHeight, Vector2Double_Create(0.0, 53.0 / Rows)).CosZenith;
    double Below = WorldAtmosphere_GetSkyViewDirection(ViewHeight, Vector2Double_Create(0.0, 54.0 / Rows)).CosZenith;
    double TenUp = WorldAtmosphere_GetSkyViewDirection(ViewHeight, Vector2Double_Create(0.0, 43.0 / Rows)).CosZenith;
    double HorizonDegrees = asin(Horizon) * TEST_DEGREES_PER_RADIAN;
    TEST_CHECK(run, (Above > Horizon) && ((asin(Above) * TEST_DEGREES_PER_RADIAN - HorizonDegrees) < 0.05));
    TEST_CHECK(run, (Below < Horizon) && ((HorizonDegrees - asin(Below) * TEST_DEGREES_PER_RADIAN) < 0.05));
    TEST_CHECK(run, (asin(TenUp) * TEST_DEGREES_PER_RADIAN - HorizonDegrees) < 3.5);

    // From 2 km up the horizon dips 1.44 degrees, and the half's middle follows it.
    double High = WorldAtmosphere_GetSkyViewDirection(2000.0, Vector2Double_Create(0.0, 0.5)).CosZenith;
    TEST_CHECK(run, IsClose(asin(High) * TEST_DEGREES_PER_RADIAN, -1.4358, 2e-3));
    TEST_CHECK(run, IsClose(High, WorldAtmosphere_GetHorizonCos(2000.0), 1e-12));
}

/* Across, the azimuth from the light: toward it in the first column, away in the last, the sine of half the angle in
 * between (0.6 degrees a texel next to the light). */
static void SkyViewAzimuthIsTheLights(TestRun* run)
{
    WorldAtmosphereView Toward = WorldAtmosphere_GetSkyViewDirection(1.7, Vector2Double_Create(0.0, 0.25));
    WorldAtmosphereView Away = WorldAtmosphere_GetSkyViewDirection(1.7, Vector2Double_Create(1.0, 0.25));
    TEST_CHECK(run, IsClose(Toward.CosLightAzimuth, 1.0, 1e-12) && IsClose(Away.CosLightAzimuth, -1.0, 1e-12));
    for (int Degrees = 0; Degrees <= 180; Degrees += 15)
    {
        double Angle = (double)Degrees / TEST_DEGREES_PER_RADIAN;
        WorldAtmosphereView View = { .CosZenith = 0.5, .CosLightAzimuth = cos(Angle) };
        TEST_CHECK(run, IsClose(WorldAtmosphere_GetSkyViewUnit(1.7, View).X, sin(0.5 * Angle), 1e-9));
    }
    double FirstStep = 2.0 * asin(1.0 / (WORLD_ATMOSPHERE_SKY_VIEW_WIDTH - 1.0)) * TEST_DEGREES_PER_RADIAN;
    TEST_CHECK(run, IsClose(FirstStep, 0.6, 1e-3));

    Vector3Double Light = Vector3Double_Create(0.6, 0.6, 0.0);
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetCosLightAzimuth(Vector3Double_Create(0.8, 0.6, 0.0), Light), 1.0, 1e-12));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetCosLightAzimuth(Vector3Double_Create(-0.5, 0.1, 0.0), Light), -1.0, 1e-12));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetCosLightAzimuth(Vector3Double_Create(0.0, 0.2, -0.9), Light), 0.0, 1e-12));
    TEST_CHECK(run, WorldAtmosphere_GetCosLightAzimuth(Vector3Double_Create(0.0, 1.0, 0.0), Light) == 1.0);
    TEST_CHECK(run, WorldAtmosphere_GetCosLightAzimuth(Vector3Double_Create(0.3, 0.1, 0.9),
        Vector3Double_Create(0.0, -1.0, 0.0)) == 1.0);
}

/* The texture's halves: the sun's in its first rows, the moon's in the rows after them, each on its own texel centres. */
static void SkyViewUvPicksTheHalf(TestRun* run)
{
    Vector3Double Direction = Vector3Double_Create(0.3, 0.2, sqrt(1.0 - 0.13));
    Vector3Double Light = Vector3Double_Create(0.0, 0.1, sqrt(1.0 - 0.01));
    Vector2Double Sun = WorldAtmosphere_GetSkyViewUv(1.7, Direction, Light, WorldAtmosphereSkyViewHalf_Sun);
    Vector2Double Moon = WorldAtmosphere_GetSkyViewUv(1.7, Direction, Light, WorldAtmosphereSkyViewHalf_Moon);
    TEST_CHECK(run, (Sun.Y > 0.0) && (Sun.Y < 0.5) && (Moon.Y > 0.5) && (Moon.Y < 1.0));
    TEST_CHECK(run, IsClose(Moon.Y - Sun.Y, 0.5, 1e-12) && IsClose(Moon.X, Sun.X, 1e-12));
    // Straight up and straight down sit on the first and last texel centres of a half: bilinear sampling there never
    // reads the other half.
    double Rows = (double)(WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT * WORLD_ATMOSPHERE_SKY_VIEW_HALF_COUNT);
    Vector2Double Up = WorldAtmosphere_GetSkyViewUv(1.7, Vector3Double_Create(0.0, 1.0, 0.0), Light,
        WorldAtmosphereSkyViewHalf_Moon);
    Vector2Double Down = WorldAtmosphere_GetSkyViewUv(1.7, Vector3Double_Create(0.0, -1.0, 0.0), Light,
        WorldAtmosphereSkyViewHalf_Sun);
    TEST_CHECK(run, IsClose(Up.Y * Rows, (double)WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT + 0.5, 1e-9));
    TEST_CHECK(run, IsClose(Down.Y * Rows, (double)WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT - 0.5, 1e-9));
}


// Public functions.
void WorldAtmosphereTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "medium_defaults_are_the_sky_before_the_tables", MediumDefaultsAreTheSkyBeforeTheTables },
        { "medium_follows_the_settings", MediumFollowsTheSettings },
        { "medium_clamps_what_validation_refuses", MediumClampsWhatValidationRefuses },
        { "tables_are_stale_only_when_their_air_changes", TablesAreStaleOnlyWhenTheirAirChanges },
        { "table_record_builds_only_when_needed", TableRecordBuildsOnlyWhenNeeded },
        { "view_height_is_clamped", ViewHeightIsClamped },
        { "heights_along_rays_are_exact", HeightsAlongRaysAreExact },
        { "horizon_and_distances", HorizonAndDistances },
        { "transmittance_mapping_round_trips", TransmittanceMappingRoundTrips },
        { "multiscatter_mapping_round_trips", MultiscatterMappingRoundTrips },
        { "sky_view_mapping_round_trips", SkyViewMappingRoundTrips },
        { "sky_view_is_dense_near_the_horizon", SkyViewIsDenseNearTheHorizon },
        { "sky_view_azimuth_is_the_lights", SkyViewAzimuthIsTheLights },
        { "sky_view_uv_picks_the_half", SkyViewUvPicksTheHalf },
    };
    TestRun_RunSuite(run, "WorldAtmosphere", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
