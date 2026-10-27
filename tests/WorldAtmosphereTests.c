#include "TestSuites.h"
#include "WorldAtmosphere.h"
#include "WorldEnvironment.h"
#include "wr/WRVectorMath.h"
#include <math.h>
#include <stdio.h>


/**
 * The atmosphere's model (source/WorldAtmosphere.h): the air's coefficients from the environment, the height the sky
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
/* The defaults: Earth's Rayleigh coefficients and the reference haze (its albedo about 1 / 1.1) with the haze of 20 km
 * visibility (turbidity 24: the reference haze times 8), plus the ozone layer and all of the multiple scattering. */
static void MediumDefaultsAreTheSkyBeforeTheTables(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(&Environment, &Medium);
    const float Rayleigh[3] = { 5.5e-6f, 13.0e-6f, 22.4e-6f };
    const float Extinction[3] = { 8.0f * 23.1e-6f, 8.0f * 23.1e-6f, 8.0f * 23.1e-6f };
    const float Scattering[3] = { 8.0f * 21.0e-6f, 8.0f * 21.0e-6f, 8.0f * 21.0e-6f };
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
    Environment.SkyTurbidity = 2.0f * Environment.SkyTurbidity;
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
    TEST_CHECK(run, IsClose((double)Medium.MieExtinction[1], 8.0 * 23.1e-6, 1e-6)); // the default, turbidity 24 (float)
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

/* The irradiance table is stale when the tables it reads are, and also when the haze's forward scattering or the
 * share of multiple scattering changes (it holds both); its record builds on the first frame and then only then. */
static void IrradianceIsStaleWithItsInputs(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereMedium Built;
    WorldAtmosphere_GetMedium(&Environment, &Built);
    WorldAtmosphereMedium Medium = Built;
    TEST_CHECK(run, !WorldAtmosphere_IsIrradianceStale(&Built, &Medium));
    Medium.MieAnisotropy *= 1.001f;
    TEST_CHECK(run, WorldAtmosphere_IsIrradianceStale(&Built, &Medium) && !WorldAtmosphere_AreTablesStale(&Built, &Medium));
    Medium = Built;
    Medium.MultipleScattering = 0.5f;
    TEST_CHECK(run, WorldAtmosphere_IsIrradianceStale(&Built, &Medium));
    Medium = Built;
    Medium.GroundAlbedo[0] *= 1.001f;
    TEST_CHECK(run, WorldAtmosphere_IsIrradianceStale(&Built, &Medium));

    WorldAtmosphereTableRecord Record = { 0 };
    Medium = Built;
    TEST_CHECK(run, WorldAtmosphere_UpdateIrradianceRecord(&Record, &Medium) && (Record.BuildCount == 1U));
    TEST_CHECK(run, !WorldAtmosphere_UpdateIrradianceRecord(&Record, &Medium));
    Medium.MieAnisotropy = 0.9f;
    TEST_CHECK(run, WorldAtmosphere_UpdateIrradianceRecord(&Record, &Medium) && (Record.BuildCount == 2U));
    TEST_CHECK(run, !WorldAtmosphere_UpdateIrradianceRecord(&Record, &Medium));
    Record.IsBuilt = false; // the target was dropped
    TEST_CHECK(run, WorldAtmosphere_UpdateIrradianceRecord(&Record, &Medium) && (Record.BuildCount == 3U));
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


/* The frame's haze: the environment's multipliers and the player's, the ground haze, the far ground's height; values
 * outside their rules clamped, non-finite ones their defaults. */
static void HazeFollowsTheEnvironment(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereHaze Haze;
    WorldAtmosphere_GetHaze(&Environment, 1.0f, &Haze);
    TEST_CHECK(run, (Haze.DistanceScale == 1.0f) && (Haze.StartDistance == 0.0f) && (Haze.GroundHazeDensity == 0.0f));
    TEST_CHECK(run, (Haze.GroundHazeBase == 0.0f) && (Haze.GroundHazeThickness == 20.0f) && (Haze.GroundHeight == 0.0f));
    TEST_CHECK(run, IsClose((double)Haze.GroundHazeAlbedo[1], pow(250.0 / 255.0, 2.2), 1e-6));
    TEST_CHECK(run, Haze.GroundHazeGlow == 0.8f);

    // Every multiplier multiplies: the world's distance scale and strength and the player's.
    Environment.AerialDistanceScale = 2.0f;
    Environment.FogStrength = 3.0f;
    WorldAtmosphere_GetHaze(&Environment, 0.5f, &Haze);
    TEST_CHECK(run, Haze.DistanceScale == 3.0f);
    WorldAtmosphere_GetHaze(&Environment, 0.0f, &Haze);
    TEST_CHECK(run, Haze.DistanceScale == 0.0f);
    Environment.FogStrength = 0.0f;
    WorldAtmosphere_GetHaze(&Environment, 1.0f, &Haze);
    TEST_CHECK(run, Haze.DistanceScale == 0.0f);

    Environment.FogStrength = 1.0f;
    Environment.AerialDistanceScale = 1.0f;
    Environment.HazeStartDistance = 120.0f;
    Environment.GroundHazeDensity = 0.02f;
    Environment.GroundHazeBase = -35.5f;
    Environment.GroundHazeThickness = 15.0f;
    Environment.GroundHazeAlbedo = (Color){ 255, 0, 255, 255 };
    Environment.GroundHazeGlow = 0.5f;
    Environment.HorizonGroundHeight = 812.0f;
    WorldAtmosphere_GetHaze(&Environment, 1.0f, &Haze);
    TEST_CHECK(run, (Haze.StartDistance == 120.0f) && (Haze.GroundHazeDensity == 0.02f) && (Haze.GroundHazeBase == -35.5f));
    TEST_CHECK(run, (Haze.GroundHazeThickness == 15.0f) && (Haze.GroundHazeGlow == 0.5f) && (Haze.GroundHeight == 812.0f));
    TEST_CHECK(run, (Haze.GroundHazeAlbedo[0] == 1.0f) && (Haze.GroundHazeAlbedo[1] == 0.0f));

    // What validation refuses is clamped (a hostile product stays finite); what is not finite takes its default.
    Environment.FogStrength = 3.0e38f;
    Environment.AerialDistanceScale = 1.0e6f;
    Environment.GroundHazeDensity = -1.0f;
    Environment.GroundHazeThickness = 0.0f;
    Environment.GroundHazeGlow = 2.0f;
    Environment.HazeStartDistance = -5.0f;
    Environment.HorizonGroundHeight = -1.0e9f;
    WorldAtmosphere_GetHaze(&Environment, 3.0e38f, &Haze);
    TEST_CHECK(run, Haze.DistanceScale == (float)WORLD_ATMOSPHERE_MAX_HAZE_SCALE);
    TEST_CHECK(run, (Haze.GroundHazeDensity == 0.0f) && (Haze.StartDistance == 0.0f));
    TEST_CHECK(run, Haze.GroundHazeThickness == (float)WORLD_ENVIRONMENT_MIN_GROUND_HAZE_THICKNESS);
    TEST_CHECK(run, Haze.GroundHazeGlow == (float)WORLD_ENVIRONMENT_MAX_SUN_GLOW);
    TEST_CHECK(run, Haze.GroundHeight == -(float)WORLD_ENVIRONMENT_MAX_DISTANCE);
    Environment.FogStrength = NAN;
    Environment.AerialDistanceScale = INFINITY;
    Environment.GroundHazeDensity = NAN;
    Environment.GroundHazeThickness = NAN;
    Environment.GroundHazeGlow = NAN;
    Environment.GroundHazeBase = NAN;
    Environment.HazeStartDistance = NAN;
    Environment.HorizonGroundHeight = INFINITY;
    WorldAtmosphere_GetHaze(&Environment, NAN, &Haze);
    TEST_CHECK(run, (Haze.DistanceScale == 1.0f) && (Haze.GroundHazeDensity == 0.0f) && (Haze.GroundHazeBase == 0.0f));
    TEST_CHECK(run, (Haze.GroundHazeThickness == 20.0f) && (Haze.GroundHazeGlow == 0.8f));
    TEST_CHECK(run, (Haze.StartDistance == 0.0f) && (Haze.GroundHeight == 0.0f));
    WorldEnvironment_Deconstruct(&Environment);
}

/* The volume's distance slices grow by one ratio from 1 m to 200 km, 21 of them within 4 km, and a
 * distance and its slice coordinate round-trip: linear in distance between two slices, from the camera to the first,
 * and from the last distance slice to the whole ray's. */
static void VolumeSlicesRoundTrip(TestRun* run)
{
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetVolumeSliceDistance(0), WORLD_ATMOSPHERE_VOLUME_NEAREST, 1e-12));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetVolumeSliceDistance(WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE),
        WORLD_ATMOSPHERE_VOLUME_FARTHEST, 1e-6));
    double Ratio = WorldAtmosphere_GetVolumeSliceDistance(1) / WorldAtmosphere_GetVolumeSliceDistance(0);
    TEST_CHECK(run, IsClose(Ratio, 1.502, 1e-3));
    int NearCount = 0;
    for (int Slice = 0; Slice <= WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE; Slice++)
    {
        double Distance = WorldAtmosphere_GetVolumeSliceDistance(Slice);
        NearCount += (Distance <= 4000.0) ? 1 : 0;
        if (Slice > 0)
        {
            TEST_CHECK(run, IsClose(Distance / WorldAtmosphere_GetVolumeSliceDistance(Slice - 1), Ratio, 1e-9));
        }
        // Each slice's distance is its own coordinate, whatever the ray's length.
        TEST_CHECK(run, IsClose(WorldAtmosphere_GetVolumeSlice(Distance, 1.1e6), (double)Slice, 1e-9));
        TEST_CHECK(run, IsClose(WorldAtmosphere_GetVolumeSlice(Distance, 50000.0), (double)Slice, 1e-9));
    }
    TEST_CHECK(run, NearCount == 21);
    // Clamped outside the slices.
    TEST_CHECK(run, WorldAtmosphere_GetVolumeSliceDistance(-3) == WorldAtmosphere_GetVolumeSliceDistance(0));
    TEST_CHECK(run, WorldAtmosphere_GetVolumeSliceDistance(99)
        == WorldAtmosphere_GetVolumeSliceDistance(WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE));

    // The camera, and the way to the first slice.
    TEST_CHECK(run, WorldAtmosphere_GetVolumeSlice(0.0, 1e6) == -1.0);
    TEST_CHECK(run, WorldAtmosphere_GetVolumeSlice(-5.0, 1e6) == -1.0);
    TEST_CHECK(run, WorldAtmosphere_GetVolumeSlice(NAN, 1e6) == -1.0);
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetVolumeSlice(0.25, 1e6), -0.75, 1e-12));
    // Linear in distance between two slices: half way in metres is half way in slices.
    double Middle = 0.5 * (WorldAtmosphere_GetVolumeSliceDistance(12) + WorldAtmosphere_GetVolumeSliceDistance(13));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetVolumeSlice(Middle, 1e6), 12.5, 1e-9));
    // Past the last distance slice: on toward the whole ray, reached at the top of the atmosphere.
    double Top = 1.13e6;
    double Last = (double)WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE;
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetVolumeSlice(0.5 * (WORLD_ATMOSPHERE_VOLUME_FARTHEST + Top), Top),
        Last + 0.5, 1e-9));
    TEST_CHECK(run, WorldAtmosphere_GetVolumeSlice(Top, Top) == Last + 1.0);
    TEST_CHECK(run, WorldAtmosphere_GetVolumeSlice(3.0 * Top, Top) == Last + 1.0);
    // A steep ray leaves the air before the last distance slice: everything past it is the whole ray.
    TEST_CHECK(run, WorldAtmosphere_GetVolumeSlice(WORLD_ATMOSPHERE_VOLUME_FARTHEST * 1.01, 100000.0) == Last + 1.0);

    // Round trips both ways, and the coordinate rises with the distance.
    double Previous = -2.0;
    for (int Step = 0; Step <= 400; Step++)
    {
        double Distance = 1e-3 * pow(10.0, 9.0 * (double)Step / 400.0); // 1 mm to 1000 km
        double Slice = WorldAtmosphere_GetVolumeSlice(Distance, Top);
        TEST_CHECK(run, Slice >= Previous);
        Previous = Slice;
        if (Distance <= Top)
        {
            double Back = WorldAtmosphere_GetVolumeDistance(Slice, Top);
            if (!TEST_CHECK(run, IsClose(Back, Distance, 1e-9 * Distance + 1e-12)))
            {
                printf("    %.9g m -> slice %.9f -> %.9g m\n", Distance, Slice, Back);
            }
        }
    }
    for (int Step = 0; Step <= 320; Step++)
    {
        double Slice = -1.0 + (double)Step / 10.0;
        double Back = WorldAtmosphere_GetVolumeSlice(WorldAtmosphere_GetVolumeDistance(Slice, Top), Top);
        TEST_CHECK(run, IsClose(Back, Slice, 1e-9));
    }
    TEST_CHECK(run, WorldAtmosphere_GetVolumeDistance(-1.0, Top) == 0.0);
    TEST_CHECK(run, WorldAtmosphere_GetVolumeDistance(Last + 1.0, Top) == Top);
}

/* The volume's texture: each slice a tile of the screen's directions, the in-scatter's tiles below the transmittance's,
 * the frame's values in the last row; a tile is sampled at least half a texel inside, never reading its neighbour. */
static void VolumeTextureLayout(TestRun* run)
{
    double Width = (double)WORLD_ATMOSPHERE_VOLUME_WIDTH;
    double Height = (double)WORLD_ATMOSPHERE_VOLUME_HEIGHT;
    double Size = (double)WORLD_ATMOSPHERE_VOLUME_SIZE;
    TEST_CHECK(run, (WORLD_ATMOSPHERE_VOLUME_WIDTH == 1024) && (WORLD_ATMOSPHERE_VOLUME_HEIGHT == 65));
    // A direction's centre lands on its texel's centre in every slice and both parts.
    for (int Slice = 0; Slice < WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT; Slice += 7)
    {
        for (int Across = 0; Across < WORLD_ATMOSPHERE_VOLUME_SIZE; Across += 5)
        {
            Vector2Double Screen = Vector2Double_Create(((double)Across + 0.5) / Size, ((double)(31 - Across) + 0.5) / Size);
            Vector2Double InScatter = WorldAtmosphere_GetVolumeUv(Screen, Slice, WorldAtmosphereVolumePart_InScatter);
            Vector2Double Transmittance = WorldAtmosphere_GetVolumeUv(Screen, Slice,
                WorldAtmosphereVolumePart_Transmittance);
            TEST_CHECK(run, IsClose(InScatter.X * Width, (double)(Slice * WORLD_ATMOSPHERE_VOLUME_SIZE + Across) + 0.5,
                1e-9));
            TEST_CHECK(run, IsClose(InScatter.Y * Height, (double)(31 - Across) + 0.5, 1e-9));
            TEST_CHECK(run, IsClose(Transmittance.X, InScatter.X, 1e-12));
            TEST_CHECK(run, IsClose(Transmittance.Y * Height, Size + (double)(31 - Across) + 0.5, 1e-9));
        }
    }
    // The screen's edges are held half a texel inside the tile.
    Vector2Double Corner = WorldAtmosphere_GetVolumeUv(Vector2Double_Create(0.0, 1.0), 3,
        WorldAtmosphereVolumePart_Transmittance);
    TEST_CHECK(run, IsClose(Corner.X * Width, 3.0 * Size + 0.5, 1e-9));
    TEST_CHECK(run, IsClose(Corner.Y * Height, 2.0 * Size - 0.5, 1e-9));
    Vector2Double Outside = WorldAtmosphere_GetVolumeUv(Vector2Double_Create(-0.2, 1.4), 0,
        WorldAtmosphereVolumePart_InScatter);
    TEST_CHECK(run, IsClose(Outside.X * Width, 0.5, 1e-9) && IsClose(Outside.Y * Height, Size - 0.5, 1e-9));
    // The frame's values: their own texels in the last row.
    for (int Value = 0; Value < (int)WorldAtmosphereVolumeValue_Count; Value++)
    {
        Vector2Double Uv = WorldAtmosphere_GetVolumeValueUv((WorldAtmosphereVolumeValue)Value);
        TEST_CHECK(run, IsClose(Uv.X * Width, (double)Value + 0.5, 1e-9) && IsClose(Uv.Y * Height, Height - 0.5, 1e-9));
    }
}

/* The ground haze's density in long double: as dense as the base below it, thinning by e every thickness above. */
static long double ReferenceGroundHazeDensity(long double density, long double thickness, long double height)
{
    return (height <= 0.0L) ? density : density * expl(-height / thickness);
}

/* The ground haze's optical depth the slow way, in long double: a level ray crosses one density; a sloped one is
 * integrated over the heights it crosses (divided by its climb per metre), above the base in the density's own variable
 * so the steep part near the base gets the steps, below it at the base's density. */
static double ReferenceGroundHazeDepth(double density, double thickness, double heightAboveBase, double rayY,
    double from, double to)
{
    long double Start = (long double)heightAboveBase + (long double)rayY * (long double)from;
    long double End = (long double)heightAboveBase + (long double)rayY * (long double)to;
    if (rayY == 0.0)
    {
        return (double)(ReferenceGroundHazeDensity(density, thickness, Start) * ((long double)to - (long double)from));
    }
    long double Low = fminl(Start, End);
    long double High = fmaxl(Start, End);
    long double Sum = 0.0L;
    const int Steps = 200000;
    long double AboveLow = fmaxl(Low, 0.0L);
    if (High > AboveLow)
    {
        long double UpperU = fminl((High - AboveLow) / (long double)thickness, 60.0L);
        for (int Index = 0; Index < Steps; Index++)
        {
            long double U = UpperU * ((long double)Index + 0.5L) / (long double)Steps;
            Sum += ReferenceGroundHazeDensity(density, thickness, AboveLow + U * (long double)thickness)
                * (UpperU / (long double)Steps) * (long double)thickness;
        }
    }
    if (Low < 0.0L)
    {
        Sum += (long double)density * (fminl(High, 0.0L) - Low);
    }
    return (double)(Sum / fabsl((long double)rayY));
}

/* The ground haze's closed form against the slow integral: rays going up, going down and level, from below, at and above
 * the base, short and to the sky's distance. */
static void GroundHazeDepthIsTheIntegral(TestRun* run)
{
    const double Thicknesses[] = { 15.0, 0.1, 1000.0 };
    const double Heights[] = { -30.0, 0.0, 1.7, 40.0, 500.0 };
    const double Slopes[] = { -1.0, -0.3, -0.001, -2e-7, 0.0, 1e-7, 0.002, 0.2, 1.0 };
    const double Ranges[][2] = { { 0.0, 10.0 }, { 3.0, 400.0 }, { 0.0, 1e5 }, { 50.0, WORLD_ATMOSPHERE_SKY_DISTANCE } };
    double Density = 0.02;
    for (size_t T = 0U; T < (sizeof(Thicknesses) / sizeof(Thicknesses[0])); T++)
    {
        for (size_t H = 0U; H < (sizeof(Heights) / sizeof(Heights[0])); H++)
        {
            for (size_t S = 0U; S < (sizeof(Slopes) / sizeof(Slopes[0])); S++)
            {
                for (size_t R = 0U; R < (sizeof(Ranges) / sizeof(Ranges[0])); R++)
                {
                    double Got = WorldAtmosphere_GetGroundHazeDepth(Density, Thicknesses[T], Heights[H], Slopes[S],
                        Ranges[R][0], Ranges[R][1]);
                    double Expected = ReferenceGroundHazeDepth(Density, Thicknesses[T], Heights[H], Slopes[S],
                        Ranges[R][0], Ranges[R][1]);
                    if (!TEST_CHECK(run, isfinite(Got) && (Got >= 0.0) && IsClose(Got, Expected,
                        1e-6 * fabs(Expected) + 1e-12)))
                    {
                        printf("    thickness %.1f height %.1f slope %.1e [%.0f, %.0e]: %.12g against %.12g\n",
                            Thicknesses[T], Heights[H], Slopes[S], Ranges[R][0], Ranges[R][1], Got, Expected);
                    }
                }
            }
        }
    }
    // No density, no length, a reversed part: no depth.
    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDepth(0.0, 15.0, 0.0, 0.0, 0.0, 100.0) == 0.0);
    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 0.0, 0.0, 5.0, 5.0) == 0.0);
    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 0.0, 0.0, 9.0, 5.0) == 0.0);
    // Straight up from the base, all of it: density x thickness; level at the base over 100 m: density x 100.
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 0.0, 1.0, 0.0, WORLD_ATMOSPHERE_SKY_DISTANCE),
        0.3, 1e-12));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 0.0, 0.0, 0.0, 100.0), 2.0, 1e-12));
    // A level ray in the mist to the sky's distance hides the horizon (the depth is huge, not infinite).
    double Horizon = WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 1.7, 0.0, 0.0, WORLD_ATMOSPHERE_SKY_DISTANCE);
    TEST_CHECK(run, isfinite(Horizon) && (Horizon > 1e5));
}

/* The ground haze's inverse: the distance where its depth reaches a share of the whole brings that depth back (the closed
 * form, which the test above checks against the integral), in [from, to], over the same rays; a share of 0 is the start,
 * all of it or more the end. Then two mists: around the camera half the light of a thick one comes from its first ln 2
 * of depth, and seen from above, from where the ray reaches it. */
static void GroundHazeDistanceInvertsTheDepth(TestRun* run)
{
    const double Thicknesses[] = { 15.0, 0.1, 1000.0 };
    const double Heights[] = { -30.0, 0.0, 1.7, 40.0, 500.0 };
    const double Slopes[] = { -1.0, -0.3, -0.001, -2e-7, 0.0, 1e-7, 0.002, 0.2, 1.0 };
    const double Ranges[][2] = { { 0.0, 10.0 }, { 3.0, 400.0 }, { 0.0, 1e5 }, { 50.0, WORLD_ATMOSPHERE_SKY_DISTANCE } };
    const double Shares[] = { 0.0, 1e-6, 0.001, 0.1, 0.5, 0.9, 0.999, 1.0, 1.5 };
    double Density = 0.02;
    for (size_t T = 0U; T < (sizeof(Thicknesses) / sizeof(Thicknesses[0])); T++)
    {
        for (size_t H = 0U; H < (sizeof(Heights) / sizeof(Heights[0])); H++)
        {
            for (size_t S = 0U; S < (sizeof(Slopes) / sizeof(Slopes[0])); S++)
            {
                for (size_t R = 0U; R < (sizeof(Ranges) / sizeof(Ranges[0])); R++)
                {
                    double From = Ranges[R][0];
                    double To = Ranges[R][1];
                    double Whole = WorldAtmosphere_GetGroundHazeDepth(Density, Thicknesses[T], Heights[H], Slopes[S],
                        From, To);
                    for (size_t Share = 0U; Share < (sizeof(Shares) / sizeof(Shares[0])); Share++)
                    {
                        double Sought = Shares[Share] * Whole;
                        double Got = WorldAtmosphere_GetGroundHazeDistance(Density, Thicknesses[T], Heights[H],
                            Slopes[S], From, To, Sought);
                        double Reached = WorldAtmosphere_GetGroundHazeDepth(Density, Thicknesses[T], Heights[H],
                            Slopes[S], From, Got);
                        // Exact where the density changes along the stretch; an even density's (a climb under a
                        // thousandth of a thickness) is off by a part in 10 000 of the depth at most.
                        double Tolerance = 2e-4 * Whole + 1e-12;
                        bool IsInside = isfinite(Got) && (Got >= From) && (Got <= To);
                        bool IsBack = (Shares[Share] >= 1.0) ? IsClose(Reached, Whole, Tolerance)
                            : IsClose(Reached, Sought, Tolerance);
                        if (!TEST_CHECK(run, IsInside && IsBack))
                        {
                            printf("    thickness %.1f height %.1f slope %.1e [%.0f, %.0e] share %.6f: %.12g reaches "
                                "%.12g of %.12g\n", Thicknesses[T], Heights[H], Slopes[S], From, To, Shares[Share], Got,
                                Reached, Sought);
                        }
                    }
                    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDistance(Density, Thicknesses[T], Heights[H],
                        Slopes[S], From, To, 0.0) == From);
                    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDistance(Density, Thicknesses[T], Heights[H],
                        Slopes[S], From, To, 2.0 * Whole + 1.0) == To);
                }
            }
        }
    }
    // No mist: never reached, the end. A depth that is not a number: the start.
    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDistance(0.0, 15.0, 1.7, 0.0, 0.0, 500.0, 0.1) == 500.0);
    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDistance(0.02, 15.0, -5.0, 0.1, 0.0, 500.0, NAN) == 0.0);
    // A thick morning mist around the camera, looking level over a kilometre: half its light from the first ln 2 of depth,
    // 39 m out (ln 2 / (0.02 e^(-1.7 / 15))).
    double Whole = WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 1.7, 0.0, 0.0, 1000.0);
    double Median = -log(0.5 * (1.0 + exp(-Whole)));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetGroundHazeDistance(0.02, 15.0, 1.7, 0.0, 0.0, 1000.0, Median),
        log(2.0) / (0.02 * exp(-1.7 / 15.0)), 1e-6));
    // From 300 m above it, looking down at a tenth: the ray reaches the base 3 km out, and the mist's light comes from just
    // before, where the depth above the base (3 (e^(-(3000 - t) / 150) - e^-20) down to it) has reached about ln 2: 220 m
    // short of it, 2.78 km out, 22 m above the base.
    Whole = WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 300.0, -0.1, 0.0, 5000.0);
    Median = -log(0.5 * (1.0 + exp(-Whole)));
    double FromAbove = WorldAtmosphere_GetGroundHazeDistance(0.02, 15.0, 300.0, -0.1, 0.0, 5000.0, Median);
    TEST_CHECK(run, IsClose(FromAbove, 3000.0 + 150.0 * log(Median / 3.0 + exp(-20.0)), 1e-6));
}

/* The ground haze's depth along a ray over the curved planet, the slow way in long double: the exact height of the
 * straight ray (its distance from the planet's centre minus the start's), Simpson's rule over @p steps. */
static double ReferenceCurvedGroundHazeDepth(double density, double thickness, double heightAboveBase, double rayY,
    double radius, double from, double to, int steps)
{
    long double R = (long double)radius;
    long double Mu = (long double)rayY;
    long double Step = ((long double)to - (long double)from) / (long double)steps;
    long double Sum = 0.0L;
    for (int Index = 0; Index <= steps; Index++)
    {
        long double T = (long double)from + Step * (long double)Index;
        long double Rise = sqrtl(R * R + 2.0L * R * Mu * T + T * T) - R;
        long double Height = (long double)heightAboveBase + Rise;
        long double Value = (long double)density * expl(-fmaxl(Height, 0.0L) / (long double)thickness);
        long double Weight = ((Index == 0) || (Index == steps)) ? 1.0L : ((Index % 2 == 1) ? 4.0L : 2.0L);
        Sum += Weight * Value;
    }
    return (double)(Sum * Step / 3.0L);
}

/* e^(x^2) erfc(x) against the C library's erfc within the formula's 1.2e-7; and the ground haze over the curved planet
 * against the slow integral along the exact ray, within 0.01 %: from inside the mist, just above it and high above it (up
 * to 99 km), with the base on the ground or above it (rays dipping below the base), level, at fractions of the way to
 * the horizon and just above it, and climbing. From up high, a ray between level and the horizon misses
 * the mist where the flat world's straight ray sinks into it; near the camera, inside the mist, the two agree. */
static void GroundHazeFollowsTheCurvedPlanet(TestRun* run)
{
    for (int Index = 0; Index <= 200; Index++)
    {
        double X = 0.1 * (double)Index;
        double Expected = exp(X * X) * erfc(X);
        TEST_CHECK(run, IsClose(WorldAtmosphere_GetScaledErfc(X), Expected, 2e-7 * Expected));
    }
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetScaledErfc(0.0), 1.0, 2e-7));

    // The camera's height above the ground and above the mist's base: the base on the ground, above it (the camera
    // above the layer, or inside its even part below the base, so rays dip below the base), and up high.
    const double Placements[][2] = { { 1.7, 1.7 }, { 12.0, 12.0 }, { 40.0, 40.0 }, { 300.0, 300.0 }, { 300.0, 100.0 },
        { 300.0, -20.0 }, { 5000.0, 5000.0 }, { 20000.0, 20000.0 }, { 99000.0, 99000.0 } };
    const double Thicknesses[] = { 15.0, 100.0 };
    const double Shares[] = { 0.0, 0.3, 0.7, 0.95, 0.999 }; // of the way from level to the horizon
    double Density = 0.02;
    for (size_t P = 0U; P < (sizeof(Placements) / sizeof(Placements[0])); P++)
    {
        double ViewHeight = Placements[P][0];
        double AboveBase = Placements[P][1];
        double Radius = WORLD_ATMOSPHERE_PLANET_RADIUS + ViewHeight;
        double Horizon = WorldAtmosphere_GetHorizonCos(ViewHeight);
        for (size_t T = 0U; T < (sizeof(Thicknesses) / sizeof(Thicknesses[0])); T++)
        {
            for (size_t Share = 0U; Share <= (sizeof(Shares) / sizeof(Shares[0])); Share++)
            {
                // The last case climbs (a tenth of a degree up).
                double RayY = (Share < (sizeof(Shares) / sizeof(Shares[0]))) ? Shares[Share] * Horizon : 0.0017;
                double Expected = ReferenceCurvedGroundHazeDepth(Density, Thicknesses[T], AboveBase, RayY, Radius, 0.0,
                    3.0e6, 6000000);
                double Got = WorldAtmosphere_GetGroundHazeDepthCurved(Density, Thicknesses[T], AboveBase, RayY, Radius,
                    0.0, WORLD_ATMOSPHERE_SKY_DISTANCE);
                if (!TEST_CHECK(run, isfinite(Got) && IsClose(Got, Expected, 1e-4 * Expected + 1e-9)))
                {
                    printf("    view height %.1f above the base %.1f thickness %.0f rayY %.6g: %.9g against %.9g\n",
                        ViewHeight, AboveBase, Thicknesses[T], RayY, Got, Expected);
                }
                // A part of the ray: from 1 km to 300 km.
                Expected = ReferenceCurvedGroundHazeDepth(Density, Thicknesses[T], AboveBase, RayY, Radius, 1000.0,
                    300000.0, 4000000);
                Got = WorldAtmosphere_GetGroundHazeDepthCurved(Density, Thicknesses[T], AboveBase, RayY, Radius, 1000.0,
                    300000.0);
                TEST_CHECK(run, isfinite(Got) && IsClose(Got, Expected, 1e-4 * Expected + 1e-9));
            }
        }
    }
    // From 300 m, halfway from level to the horizon (0.28 degrees down), over a mist of 15 m: the flat world's ray sinks
    // into it 61 km out; the curved one passes 150 m above it.
    double Horizon300 = WorldAtmosphere_GetHorizonCos(300.0);
    double Curved = WorldAtmosphere_GetGroundHazeDepthCurved(0.02, 15.0, 300.0, 0.5 * Horizon300,
        WORLD_ATMOSPHERE_PLANET_RADIUS + 300.0, 0.0, WORLD_ATMOSPHERE_SKY_DISTANCE);
    double Flat = WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 300.0, 0.5 * Horizon300, 0.0,
        WORLD_ATMOSPHERE_SKY_DISTANCE);
    TEST_CHECK(run, (Curved < 1e-3) && (Flat > 100.0));
    // Inside the mist, its first 100 m agree with the flat world's.
    double Near = WorldAtmosphere_GetGroundHazeDepthCurved(0.02, 15.0, 1.7, 0.5 * WorldAtmosphere_GetHorizonCos(1.7),
        WORLD_ATMOSPHERE_PLANET_RADIUS + 1.7, 0.0, 100.0);
    double NearFlat = WorldAtmosphere_GetGroundHazeDepth(0.02, 15.0, 1.7, 0.5 * WorldAtmosphere_GetHorizonCos(1.7), 0.0,
        100.0);
    TEST_CHECK(run, IsClose(Near, NearFlat, 1e-4 * NearFlat));
    // No density, no length.
    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDepthCurved(0.0, 15.0, 1.7, 0.0, 6.4e6, 0.0, 1e8) == 0.0);
    TEST_CHECK(run, WorldAtmosphere_GetGroundHazeDepthCurved(0.02, 15.0, 1.7, 0.0, 6.4e6, 5.0, 5.0) == 0.0);
}

/* The sky's light by height: its heights from the ground to WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST, closest near the
 * ground, round-trip through their coordinates, and heights outside clamp; its blocks lie in the volume's last row after
 * the ground haze's values, one per value, side by side. */
static void SkyLightLayoutRoundTrips(TestRun* run)
{
    bool AreRoundTrips = true;
    for (int Index = 0; Index < WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT; Index++)
    {
        double Height = WorldAtmosphere_GetSkyLightHeight(Index);
        AreRoundTrips = AreRoundTrips
            && IsClose(WorldAtmosphere_GetSkyLightCoordinate(Height), (double)Index, ROUND_TRIP_TOLERANCE);
    }
    int Last = WORLD_ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1;
    TEST_CHECK(run, AreRoundTrips && (WorldAtmosphere_GetSkyLightHeight(0) == 0.0)
        && (WorldAtmosphere_GetSkyLightHeight(Last) == WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST));
    TEST_CHECK(run, WorldAtmosphere_GetSkyLightHeight(1) < 6.0); // about 5 m apart at the ground
    TEST_CHECK(run, (WorldAtmosphere_GetSkyLightCoordinate(-10.0) == 0.0) && (WorldAtmosphere_GetSkyLightCoordinate(NAN) == 0.0)
        && (WorldAtmosphere_GetSkyLightCoordinate(1e6) == (double)Last));
    TEST_CHECK(run, (WorldAtmosphere_GetSkyLightHeight(-3) == 0.0)
        && (WorldAtmosphere_GetSkyLightHeight(1000) == WORLD_ATMOSPHERE_SKY_LIGHT_HIGHEST));

    TEST_CHECK(run, WorldAtmosphere_GetSkyLightColumn(WorldAtmosphereSkyLight_Sun, 0) == WORLD_ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN);
    TEST_CHECK(run, WORLD_ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN > (int)WorldAtmosphereVolumeValue_GroundHazeAmbient);
    for (int Value = 1; Value < (int)WorldAtmosphereSkyLight_Count; Value++)
    {
        TEST_CHECK(run, WorldAtmosphere_GetSkyLightColumn((WorldAtmosphereSkyLight)Value, 0)
            == WorldAtmosphere_GetSkyLightColumn((WorldAtmosphereSkyLight)(Value - 1), Last) + 1);
    }
    TEST_CHECK(run, WorldAtmosphere_GetSkyLightColumn(WorldAtmosphereSkyLight_Ground, Last) < WORLD_ATMOSPHERE_VOLUME_WIDTH);
    TEST_CHECK(run, (WorldAtmosphere_GetSkyLightColumn(WorldAtmosphereSkyLight_Sky, -1)
        == WorldAtmosphere_GetSkyLightColumn(WorldAtmosphereSkyLight_Sky, 0))
        && (WorldAtmosphere_GetSkyLightColumn(WorldAtmosphereSkyLight_Sky, 99)
            == WorldAtmosphere_GetSkyLightColumn(WorldAtmosphereSkyLight_Sky, Last)));
}

/* The irradiance table's layout round-trips: across, the light's elevation from the lowest to the zenith, dense at
 * the horizon (even in its asinh over the elevation scale); up, the height, even near the ground (in log(1 + height /
 * the height scale)); lower lights and heights outside clamp; the lights' part takes the texture's first columns from
 * texel centre to texel centre, light pollution the last one, at the same rows. */
static void IrradianceLayoutRoundTrips(TestRun* run)
{
    const double Heights[] = { 0.0, 1.7, 300.0, 5000.0, 60000.0, 100000.0 };
    const double Elevations[] = { -20.0, -4.0, 0.0, 5.0, 45.0, 90.0 };
    bool AreRoundTrips = true;
    for (size_t Height = 0U; Height < (sizeof(Heights) / sizeof(Heights[0])); Height++)
    {
        for (size_t Elevation = 0U; Elevation < (sizeof(Elevations) / sizeof(Elevations[0])); Elevation++)
        {
            WorldAtmosphereRay Light = AtmosphereRay(Heights[Height], sin(Elevations[Elevation] / TEST_DEGREES_PER_RADIAN));
            WorldAtmosphereRay Back = WorldAtmosphere_GetIrradianceRay(WorldAtmosphere_GetIrradianceUnit(Light));
            AreRoundTrips = AreRoundTrips && IsClose(Back.CosZenith, Light.CosZenith, ROUND_TRIP_TOLERANCE)
                && IsClose(Back.Height, Light.Height, fmax(HEIGHT_TOLERANCE * Light.Height, 1e-6));
        }
    }
    TEST_CHECK(run, AreRoundTrips);
    double Rows = (double)WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT;
    double Lowest = WORLD_ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION;
    double Top = WORLD_ATMOSPHERE_TOP_RADIUS - WORLD_ATMOSPHERE_PLANET_RADIUS;
    // (Within an ulp of 1 at the zenith: the compiler folds the zenith's asinh exactly, the library's may be an ulp off.)
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(0.0, sin(Lowest))).X, 0.0, 1e-12)
        && IsClose(WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(0.0, 1.0)).X, 1.0, 1e-12)
        && (WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(Top, 1.0)).Y == 1.0));
    double Scale = WORLD_ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE;
    double Middle = Scale * sinh(0.5 * (asinh(Lowest / Scale) + asinh(0.5 * TEST_ATMOSPHERE_PI / Scale)));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(0.0, sin(Middle))).X, 0.5, 1e-12));
    double HeightScale = WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE;
    double HalfUp = HeightScale * (sqrt(1.0 + Top / HeightScale) - 1.0);
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(HalfUp, 1.0)).Y, 0.5, 1e-12));
    // Columns closest at the horizon, rows evenly spaced near the ground (within a tenth over the first two).
    double Across = 1.0 / (double)(WORLD_ATMOSPHERE_IRRADIANCE_WIDTH - 1);
    double AtHorizon = WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(0.0, sin(0.005))).X
        - WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(0.0, sin(-0.005))).X;
    double AtZenith = WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(0.0, 1.0)).X
        - WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(0.0, sin(0.5 * TEST_ATMOSPHERE_PI - 0.01))).X;
    TEST_CHECK(run, (AtHorizon / 0.01 > 10.0 * AtZenith / 0.01) && (AtHorizon / 0.01 * 0.0075 > Across));
    double First = WorldAtmosphere_GetIrradianceRay(Vector2Double_Create(0.0, 1.0 / (Rows - 1.0))).Height;
    double Second = WorldAtmosphere_GetIrradianceRay(Vector2Double_Create(0.0, 2.0 / (Rows - 1.0))).Height - First;
    TEST_CHECK(run, (First > 100.0) && (First < 250.0) && (Second > First) && (Second < 1.2 * First));
    TEST_CHECK(run, (WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(-5.0, -0.9)).X == 0.0)
        && (WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(-5.0, -0.9)).Y == 0.0)
        && (WorldAtmosphere_GetIrradianceUnit(AtmosphereRay(1e7, 1.0)).Y == 1.0));

    // The texture: the lights' columns, then light pollution's.
    double Width = (double)WORLD_ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH;
    Vector2Double LowUv = WorldAtmosphere_GetIrradianceUv(AtmosphereRay(0.0, sin(Lowest)));
    Vector2Double HighUv = WorldAtmosphere_GetIrradianceUv(AtmosphereRay(Top, 1.0));
    TEST_CHECK(run, IsClose(LowUv.X, 0.5 / Width, 1e-12) && IsClose(LowUv.Y, 0.5 / Rows, 1e-12));
    TEST_CHECK(run, IsClose(HighUv.X, ((double)WORLD_ATMOSPHERE_IRRADIANCE_WIDTH - 0.5) / Width, 1e-12)
        && IsClose(HighUv.Y, (Rows - 0.5) / Rows, 1e-12));
    for (size_t Height = 0U; Height < (sizeof(Heights) / sizeof(Heights[0])); Height++)
    {
        Vector2Double Town = WorldAtmosphere_GetLightPollutionIrradianceUv(Heights[Height]);
        TEST_CHECK(run, IsClose(Town.X, ((double)WORLD_ATMOSPHERE_IRRADIANCE_WIDTH + 0.5) / Width, 1e-12)
            && (Town.Y == WorldAtmosphere_GetIrradianceUv(AtmosphereRay(Heights[Height], 0.3)).Y));
    }
}

/* How high a light stands over a far point of the ground (the curved planet's own zenith there), against the geometry in
 * long double; the sun sets on far ground away from it first. */
static void LightStandsLowerOverFarGround(TestRun* run)
{
    const double Heights[] = { 1.7, 300.0, 5000.0 };
    const double Cosines[] = { -0.3, -0.01, -0.0005, 0.0, 0.2 };
    const double Distances[] = { 0.0, 100.0, 4650.0, 60000.0, 250000.0 };
    for (size_t H = 0U; H < (sizeof(Heights) / sizeof(Heights[0])); H++)
    {
        for (size_t C = 0U; C < (sizeof(Cosines) / sizeof(Cosines[0])); C++)
        {
            for (size_t D = 0U; D < (sizeof(Distances) / sizeof(Distances[0])); D++)
            {
                // The ray in the plane x, y (y up through the start), the light 40 degrees off it in azimuth.
                long double Mu = (long double)Cosines[C];
                long double SinMu = sqrtl(1.0L - Mu * Mu);
                long double LightMu = 0.15L;
                long double LightSin = sqrtl(1.0L - LightMu * LightMu);
                long double Azimuth = 40.0L / 57.29577951308232L;
                long double Point[3] = { (long double)Distances[D] * SinMu,
                    (long double)WORLD_ATMOSPHERE_PLANET_RADIUS + (long double)Heights[H] + (long double)Distances[D] * Mu,
                    0.0L };
                long double Light[3] = { LightSin * cosl(Azimuth), LightMu, LightSin * sinl(Azimuth) };
                long double Length = sqrtl(Point[0] * Point[0] + Point[1] * Point[1]);
                double Expected = (double)((Point[0] * Light[0] + Point[1] * Light[1]) / Length);
                double CosAngle = (double)(SinMu * Light[0] + Mu * Light[1]);
                double Got = WorldAtmosphere_GetLightCosAt(AtmosphereRay(Heights[H], Cosines[C]), Distances[D],
                    (double)LightMu, CosAngle);
                TEST_CHECK(run, IsClose(Got, Expected, 1e-9));
            }
        }
    }
    // The sun on the horizon behind the camera: a ground point 100 km ahead sees it 0.9 degrees lower, set for it.
    double Ahead = WorldAtmosphere_GetLightCosAt(AtmosphereRay(1.7, -0.0005), 100000.0, 0.0, -1.0);
    TEST_CHECK(run, IsClose(asin(Ahead) * TEST_DEGREES_PER_RADIAN, -100000.0 / WORLD_ATMOSPHERE_PLANET_RADIUS
        * TEST_DEGREES_PER_RADIAN, 1e-3));
    TEST_CHECK(run, WorldEnvironment_GetDaylightForElevation((float)Ahead) < 1.0f);
    // From 1.7 m the far ground begins where the world's ground would end: the horizon, 4.65 km out.
    double ToGround = WorldAtmosphere_GetDistanceToGround(AtmosphereRay(1.7, WorldAtmosphere_GetHorizonCos(1.7) - 1e-9));
    TEST_CHECK(run, (ToGround > 4600.0) && (ToGround < 4700.0));
}

/* Light pollution's light in the air: 1 at the camera's foot, a third (1 / e) a scale length up or out, as on the ground
 * below it. */
static void LightPollutionFillsTheLowAirNearby(TestRun* run)
{
    TEST_CHECK(run, WorldAtmosphere_GetLightPollutionShare(0.0, 0.0) == 1.0);
    TEST_CHECK(run, WorldAtmosphere_GetLightPollutionShare(-40.0, 0.0) == 1.0);
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetLightPollutionShare(WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT, 0.0),
        exp(-1.0), 1e-12));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetLightPollutionShare(0.0, WORLD_ATMOSPHERE_LIGHT_POLLUTION_REACH),
        exp(-1.0), 1e-12));
    TEST_CHECK(run, IsClose(WorldAtmosphere_GetLightPollutionShare(1000.0, 5000.0), exp(-2.0 - 0.5), 1e-12));
}

/* Light pollution's radiance: the light pollution's colour times its strength (clamped to [0, 1], a broken one none) times
 * the calibrated radiance, at night only. */
static void LightPollutionRadianceIsTheTownsAtNight(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.LightPollution = 0.5f;
    Environment.LightPollutionColor = (Color){ .r = 255, .g = 128, .b = 0, .a = 255 };
    float Radiance[3];
    WorldAtmosphere_GetLightPollutionRadiance(&Environment, 0.0f, Radiance);
    float Expected[3] = { 0.5f * (float)WORLD_ATMOSPHERE_LIGHT_POLLUTION_RADIANCE,
        0.5f * powf(128.0f / 255.0f, 2.2f) * (float)WORLD_ATMOSPHERE_LIGHT_POLLUTION_RADIANCE, 0.0f };
    TEST_CHECK(run, IsClose(Radiance[0], Expected[0], 1e-7) && IsClose(Radiance[1], Expected[1], 1e-7)
        && (Radiance[2] == 0.0f));
    // Twilight halves it; the day has none.
    WorldAtmosphere_GetLightPollutionRadiance(&Environment, 0.5f, Radiance);
    TEST_CHECK(run, IsClose(Radiance[0], 0.5 * Expected[0], 1e-7));
    WorldAtmosphere_GetLightPollutionRadiance(&Environment, 1.0f, Radiance);
    TEST_CHECK(run, (Radiance[0] == 0.0f) && (Radiance[1] == 0.0f));
    WorldAtmosphere_GetLightPollutionRadiance(&Environment, 7.0f, Radiance);
    TEST_CHECK(run, Radiance[0] == 0.0f);
    // Beyond 1 counts as 1; below 0 and NaN as none.
    Environment.LightPollution = 3.0f;
    WorldAtmosphere_GetLightPollutionRadiance(&Environment, 0.0f, Radiance);
    TEST_CHECK(run, IsClose(Radiance[0], 2.0 * Expected[0], 1e-7));
    Environment.LightPollution = -1.0f;
    WorldAtmosphere_GetLightPollutionRadiance(&Environment, 0.0f, Radiance);
    TEST_CHECK(run, Radiance[0] == 0.0f);
    Environment.LightPollution = NAN;
    WorldAtmosphere_GetLightPollutionRadiance(&Environment, NAN, Radiance);
    TEST_CHECK(run, isfinite(Radiance[0]) && (Radiance[0] == 0.0f));
    WorldEnvironment_Deconstruct(&Environment);
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
        { "irradiance_is_stale_with_its_inputs", IrradianceIsStaleWithItsInputs },
        { "view_height_is_clamped", ViewHeightIsClamped },
        { "heights_along_rays_are_exact", HeightsAlongRaysAreExact },
        { "horizon_and_distances", HorizonAndDistances },
        { "transmittance_mapping_round_trips", TransmittanceMappingRoundTrips },
        { "multiscatter_mapping_round_trips", MultiscatterMappingRoundTrips },
        { "sky_view_mapping_round_trips", SkyViewMappingRoundTrips },
        { "sky_view_is_dense_near_the_horizon", SkyViewIsDenseNearTheHorizon },
        { "sky_view_azimuth_is_the_lights", SkyViewAzimuthIsTheLights },
        { "sky_view_uv_picks_the_half", SkyViewUvPicksTheHalf },
        { "haze_follows_the_environment", HazeFollowsTheEnvironment },
        { "volume_slices_round_trip", VolumeSlicesRoundTrip },
        { "volume_texture_layout", VolumeTextureLayout },
        { "ground_haze_depth_is_the_integral", GroundHazeDepthIsTheIntegral },
        { "ground_haze_distance_inverts_the_depth", GroundHazeDistanceInvertsTheDepth },
        { "ground_haze_follows_the_curved_planet", GroundHazeFollowsTheCurvedPlanet },
        { "sky_light_layout_round_trips", SkyLightLayoutRoundTrips },
        { "irradiance_layout_round_trips", IrradianceLayoutRoundTrips },
        { "light_stands_lower_over_far_ground", LightStandsLowerOverFarGround },
        { "light_pollution_fills_the_low_air_nearby", LightPollutionFillsTheLowAirNearby },
        { "light_pollution_radiance_is_the_towns_at_night", LightPollutionRadianceIsTheTownsAtNight },
    };
    TestRun_RunSuite(run, "WorldAtmosphere", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
