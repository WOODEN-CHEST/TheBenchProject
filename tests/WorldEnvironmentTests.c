#include "TestSuites.h"
#include "WorldEnvironment.h"
#include "GameCalendar.h"
#include "GameClock.h"
#include "PropertyDescriptor.h"
#include "raylib/raymath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>


// Macros.
/** Tolerance for comparing unit vectors and matrix entries computed in float. */
#define CELESTIAL_TOLERANCE 1e-5f
/** Degrees per radian, for readable angle checks. */
#define TEST_DEGREES_PER_RADIAN 57.29577951308232
/** Minutes in a day, for scanning a day minute by minute. */
#define MINUTES_PER_DAY 1440
/** Seconds in a day, for scanning a day second by second. */
#define SECONDS_PER_DAY 86400
/** The stars gain one turn a year on the sun: about 0.9856 degrees a day. */
#define STAR_DRIFT_DEGREES_PER_DAY (360.0 / 365.2422)


// Static functions: helpers.
/* Checks that @p environment fails validation with ArgumentOutOfRange and a message naming @p fieldName. */
static void CheckRejected(TestRun* run, const WorldEnvironment* environment, const char* fieldName)
{
    Error Result = WorldEnvironment_Validate(environment);
    TEST_CHECK(run, Result.Code == ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, (Result.Message != NULL) && (strstr((const char*)Result.Message, fieldName) != NULL));
    Error_Deconstruct(&Result);
}

/* Gives an environment its defaults again (it owns its texture names, so it is released first). */
static void ResetEnvironment(WorldEnvironment* environment)
{
    WorldEnvironment_Deconstruct(environment);
    WorldEnvironment_SetDefaults(environment);
}

/* Sets the date and time of day the sky shows (hours and minutes as UTC, the moon epoch's time). */
static bool SetDateAndTime(TestRun* run, WorldEnvironment* environment, int32_t year, int32_t month, int32_t day,
    int32_t hour, int32_t minute)
{
    int64_t DayNumber = 0;
    bool IsValid = GameCalendar_TryGetDayNumber((GameDate){ .Year = year, .Month = month, .Day = day }, &DayNumber);
    environment->Date = DayNumber;
    environment->TimeOfDay = (float)((double)(hour * 60 + minute) / (double)MINUTES_PER_DAY);
    return TEST_CHECK(run, IsValid);
}

/* How far apart two phases are around the cycle, in [0, 0.5]. */
static float GetPhaseDistance(float a, float b)
{
    float Distance = fabsf(a - b);
    return fminf(Distance, 1.0f - Distance);
}

static bool AreVectorsClose(Vector3 a, Vector3 b)
{
    return (fabsf(a.x - b.x) < CELESTIAL_TOLERANCE) && (fabsf(a.y - b.y) < CELESTIAL_TOLERANCE)
        && (fabsf(a.z - b.z) < CELESTIAL_TOLERANCE);
}

/* The angle between two unit vectors, in degrees. */
static double GetAngleDegrees(Vector3 a, Vector3 b)
{
    double Dot = (double)Vector3DotProduct(a, b);
    return acos(fmin(fmax(Dot, -1.0), 1.0)) * TEST_DEGREES_PER_RADIAN;
}

/* The moon's direction (when @p isMoon) or the sun's. */
static Vector3 GetBodyDirection(const WorldEnvironment* environment, bool isMoon)
{
    return isMoon ? WorldEnvironment_GetMoonDirection(environment) : WorldEnvironment_GetSunDirection(environment);
}

/* The time of day (in minutes) at which the moon or the sun rises on the environment's date, scanning minute by minute;
 * -1 when it does not rise that day. */
static int32_t FindRiseMinute(WorldEnvironment* environment, bool isMoon)
{
    environment->TimeOfDay = 0.0f;
    float Previous = GetBodyDirection(environment, isMoon).y;
    for (int32_t Minute = 1; Minute < MINUTES_PER_DAY; Minute++)
    {
        environment->TimeOfDay = (float)((double)Minute / (double)MINUTES_PER_DAY);
        float Current = GetBodyDirection(environment, isMoon).y;
        if ((Previous <= 0.0f) && (Current > 0.0f))
        {
            return Minute;
        }
        Previous = Current;
    }
    return -1;
}

/* The time of day (in seconds) at which a star-frame direction rises on the environment's date, scanning second by
 * second; -1 when it does not rise that day. */
static int32_t FindStarRiseSecond(WorldEnvironment* environment, Vector3 star)
{
    environment->TimeOfDay = 0.0f;
    float Previous = Vector3Transform(star, WorldEnvironment_GetStarsToWorld(environment)).y;
    for (int32_t Second = 1; Second < SECONDS_PER_DAY; Second++)
    {
        environment->TimeOfDay = (float)((double)Second / (double)SECONDS_PER_DAY);
        float Current = Vector3Transform(star, WorldEnvironment_GetStarsToWorld(environment)).y;
        if ((Previous <= 0.0f) && (Current > 0.0f))
        {
            return Second;
        }
        Previous = Current;
    }
    return -1;
}

/* How many minutes of the environment's date the sun spends above the horizon. */
static int32_t CountDaylightMinutes(WorldEnvironment* environment)
{
    int32_t Count = 0;
    for (int32_t Minute = 0; Minute < MINUTES_PER_DAY; Minute++)
    {
        environment->TimeOfDay = (float)(((double)Minute + 0.5) / (double)MINUTES_PER_DAY);
        if (WorldEnvironment_GetSunDirection(environment).y > 0.0f)
        {
            Count++;
        }
    }
    return Count;
}

/* The star frame's turn against the celestial (sun) frame, in degrees [0, 360): where the star frame's x axis lies
 * along the sun's path. */
static double GetStarDriftDegrees(const WorldEnvironment* environment)
{
    Vector3 World = Vector3Transform((Vector3){ 1.0f, 0.0f, 0.0f }, WorldEnvironment_GetStarsToWorld(environment));
    Vector3 Celestial = Vector3Transform(World, MatrixTranspose(WorldEnvironment_GetCelestialToWorld(environment)));
    double Degrees = atan2((double)Celestial.y, (double)Celestial.x) * TEST_DEGREES_PER_RADIAN;
    return (Degrees < 0.0) ? (Degrees + 360.0) : Degrees;
}


// Static functions: tests.
static void DefaultsAreValid(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    TEST_CHECK_ERROR(run, WorldEnvironment_Validate(NULL), ErrorCode_IllegalArgument);

    // The texture names are owned copies of the shipped asset names; the moon starts on the calendar.
    TEST_CHECK(run, (Environment.MilkyWayTexture != NULL)
        && (strcmp((const char*)Environment.MilkyWayTexture, "sky_milky_way") == 0));
    TEST_CHECK(run, (Environment.MoonTexture != NULL) && (strcmp((const char*)Environment.MoonTexture, "sky_moon") == 0));
    TEST_CHECK(run, Environment.IsMoonEnabled && (Environment.MoonPhaseSource == WorldMoonPhaseSource_Calendar));
    TEST_CHECK(run, (Environment.MoonCycleDays == WORLD_ENVIRONMENT_MOON_CYCLE_DAYS) && !Environment.AreSeasonsEnabled);
    TEST_CHECK(run, (Environment.NightVisionStrength == 1.0f) && (Environment.NightVisionBlueShift == 0.5f));
    // The air (T15): the sky before the tables (turbidity 3, a 1200 m haze of albedo about 0.9, g 0.758), Earth's air
    // and ozone, all of the multiple scattering.
    TEST_CHECK(run, (Environment.SkyTurbidity == 3.0f) && (Environment.HazeHeight == 1200.0f));
    TEST_CHECK(run, (Environment.HazeAlbedo.r == 244) && (Environment.HazeAlbedo.g == 244) && (Environment.HazeAlbedo.b == 244));
    TEST_CHECK(run, (Environment.SunGlow == 0.758f) && (Environment.AirDensity == 1.0f) && (Environment.Ozone == 1.0f));
    TEST_CHECK(run, Environment.MultipleScattering == 1.0f);
    // The haze (T35): the real air (no extra distance, the whole of it, from the camera), no ground haze, a near-white mist
    // of glow 0.8 when one is set, the far ground at world Y 0.
    TEST_CHECK(run, (Environment.FogStrength == 1.0f) && (Environment.AerialDistanceScale == 1.0f)
        && (Environment.HazeStartDistance == 0.0f));
    TEST_CHECK(run, (Environment.GroundHazeDensity == 0.0f) && (Environment.GroundHazeBase == 0.0f)
        && (Environment.GroundHazeThickness == 20.0f) && (Environment.GroundHazeGlow == 0.8f));
    TEST_CHECK(run, (Environment.GroundHazeAlbedo.r == 250) && (Environment.GroundHazeAlbedo.b == 250)
        && (Environment.HorizonGroundHeight == 0.0f));
    WorldEnvironment_Deconstruct(&Environment);
    WorldEnvironment_Deconstruct(&Environment); // safe to repeat
    TEST_CHECK(run, (Environment.MilkyWayTexture == NULL) && (Environment.MoonTexture == NULL));
}

static void ValidateNamesTheBrokenField(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);

    Environment.TimeOfDay = NAN;
    CheckRejected(run, &Environment, "TimeOfDay");

    ResetEnvironment(&Environment);
    Environment.TimeOfDay = 1.5f;
    CheckRejected(run, &Environment, "TimeOfDay");

    ResetEnvironment(&Environment);
    Environment.Date = GAME_CALENDAR_FIRST_DAY_NUMBER - 1; // before 0001-01-01
    CheckRejected(run, &Environment, "date (Date) must be a date in the years 1 to 9999");

    ResetEnvironment(&Environment);
    Environment.Date = GAME_CALENDAR_LAST_DAY_NUMBER + 1; // after 9999-12-31
    CheckRejected(run, &Environment, "date (Date)");

    ResetEnvironment(&Environment);
    Environment.SunAngle = INFINITY;
    CheckRejected(run, &Environment, "SunAngle");

    ResetEnvironment(&Environment);
    Environment.SunSizeMultiplier = 0.0f;
    CheckRejected(run, &Environment, "SunSizeMultiplier");

    ResetEnvironment(&Environment);
    Environment.SunIntensity = -INFINITY;
    CheckRejected(run, &Environment, "SunIntensity");

    ResetEnvironment(&Environment);
    Environment.FogStrength = -0.5f;
    CheckRejected(run, &Environment, "FogStrength");

    ResetEnvironment(&Environment);
    Environment.AmbientOcclusionStrength = NAN;
    CheckRejected(run, &Environment, "AmbientOcclusionStrength");

    ResetEnvironment(&Environment);
    Environment.LightPollution = 1.01f;
    CheckRejected(run, &Environment, "LightPollution");

    ResetEnvironment(&Environment);
    Environment.LightPollution = -0.01f;
    CheckRejected(run, &Environment, "LightPollution");

    ResetEnvironment(&Environment);
    Environment.LightPollution = NAN;
    CheckRejected(run, &Environment, "LightPollution");

    ResetEnvironment(&Environment);
    Environment.StarTwinkle = 1.5f;
    CheckRejected(run, &Environment, "StarTwinkle");

    ResetEnvironment(&Environment);
    Environment.StarTwinkle = -INFINITY;
    CheckRejected(run, &Environment, "StarTwinkle");

    ResetEnvironment(&Environment);
    Environment.MilkyWayBrightness = -1.0f;
    CheckRejected(run, &Environment, "MilkyWayBrightness");

    ResetEnvironment(&Environment);
    Environment.NightVisionStrength = 1.5f;
    CheckRejected(run, &Environment, "NightVisionStrength");

    ResetEnvironment(&Environment);
    Environment.NightVisionStrength = NAN;
    CheckRejected(run, &Environment, "NightVisionStrength");

    ResetEnvironment(&Environment);
    Environment.NightVisionBlueShift = -0.25f;
    CheckRejected(run, &Environment, "NightVisionBlueShift");

    // The air's settings (T15).
    ResetEnvironment(&Environment);
    Environment.HazeHeight = 5.0f;
    CheckRejected(run, &Environment, "world_environment.haze_height (HazeHeight) must be finite and in [10, 100000]");
    ResetEnvironment(&Environment);
    Environment.HazeHeight = NAN;
    CheckRejected(run, &Environment, "HazeHeight");
    ResetEnvironment(&Environment);
    Environment.SunGlow = 1.0f;
    CheckRejected(run, &Environment, "world_environment.sun_glow (SunGlow) must be finite and in [0, 0.99]");
    ResetEnvironment(&Environment);
    Environment.SunGlow = -0.1f;
    CheckRejected(run, &Environment, "SunGlow");
    ResetEnvironment(&Environment);
    Environment.AirDensity = -1.0f;
    CheckRejected(run, &Environment, "AirDensity");
    ResetEnvironment(&Environment);
    Environment.AirDensity = 101.0f;
    CheckRejected(run, &Environment, "world_environment.air_density (AirDensity) must be finite and in [0, 100]");
    ResetEnvironment(&Environment);
    Environment.Ozone = INFINITY;
    CheckRejected(run, &Environment, "Ozone");
    ResetEnvironment(&Environment);
    Environment.Ozone = 101.0f;
    CheckRejected(run, &Environment, "world_environment.ozone (Ozone) must be finite and in [0, 100]");
    ResetEnvironment(&Environment);
    Environment.MultipleScattering = 1.5f;
    CheckRejected(run, &Environment, "MultipleScattering");
    ResetEnvironment(&Environment);
    Environment.MultipleScattering = -0.5f;
    CheckRejected(run, &Environment, "MultipleScattering");

    // The haze's settings (T35).
    ResetEnvironment(&Environment);
    Environment.AerialDistanceScale = -1.0f;
    CheckRejected(run, &Environment, "AerialDistanceScale");
    ResetEnvironment(&Environment);
    Environment.AerialDistanceScale = 1001.0f;
    CheckRejected(run, &Environment,
        "world_environment.aerial_distance_scale (AerialDistanceScale) must be finite and in [0, 1000]");
    ResetEnvironment(&Environment);
    Environment.HazeStartDistance = -0.5f;
    CheckRejected(run, &Environment, "HazeStartDistance");
    ResetEnvironment(&Environment);
    Environment.GroundHazeDensity = NAN;
    CheckRejected(run, &Environment, "GroundHazeDensity");
    ResetEnvironment(&Environment);
    Environment.GroundHazeDensity = 101.0f;
    CheckRejected(run, &Environment, "world_environment.ground_haze_density (GroundHazeDensity) must be finite and in "
        "[0, 100]");
    ResetEnvironment(&Environment);
    Environment.GroundHazeBase = INFINITY;
    CheckRejected(run, &Environment, "GroundHazeBase");
    ResetEnvironment(&Environment);
    Environment.GroundHazeBase = -2.0e7f;
    CheckRejected(run, &Environment, "GroundHazeBase");
    ResetEnvironment(&Environment);
    Environment.GroundHazeThickness = 0.05f;
    CheckRejected(run, &Environment, "GroundHazeThickness");
    ResetEnvironment(&Environment);
    Environment.GroundHazeGlow = 1.0f;
    CheckRejected(run, &Environment, "GroundHazeGlow");
    ResetEnvironment(&Environment);
    Environment.HorizonGroundHeight = 2.0e7f;
    CheckRejected(run, &Environment, "HorizonGroundHeight");

    // Zero is allowed where the rule is ">= 0" (an effect switched off), the [0, 1] ranges include both ends, and the
    // calendar's first and last days are dates.
    ResetEnvironment(&Environment);
    Environment.BloomStrength = 0.0f;
    Environment.SkyTurbidity = 0.0f;
    Environment.MilkyWayBrightness = 0.0f;
    Environment.LightPollution = 1.0f;
    Environment.StarTwinkle = 0.0f;
    Environment.NightVisionStrength = 0.0f;
    Environment.NightVisionBlueShift = 1.0f;
    Environment.TimeOfDay = 1.0f;
    Environment.Date = GAME_CALENDAR_LAST_DAY_NUMBER;
    Environment.HazeHeight = 10.0f;
    Environment.SunGlow = 0.99f;
    Environment.AirDensity = 0.0f;
    Environment.Ozone = 100.0f;
    Environment.MultipleScattering = 0.0f;
    Environment.HazeAlbedo = (Color){ 0, 0, 0, 0 };
    Environment.FogStrength = 0.0f;
    Environment.AerialDistanceScale = 0.0f;
    Environment.HazeStartDistance = 1.0e7f;
    Environment.GroundHazeDensity = 100.0f;
    Environment.GroundHazeBase = -1.0e7f;
    Environment.GroundHazeThickness = 0.1f;
    Environment.GroundHazeGlow = 0.0f;
    Environment.HorizonGroundHeight = 1.0e7f;
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    Environment.AerialDistanceScale = 1000.0f;
    Environment.GroundHazeThickness = 100000.0f;
    Environment.GroundHazeGlow = 0.99f;
    Environment.HorizonGroundHeight = -1.0e7f;
    Environment.HazeHeight = 100000.0f;
    Environment.SunGlow = 0.0f;
    Environment.AirDensity = 100.0f;
    Environment.Ozone = 0.0f;
    Environment.MultipleScattering = 1.0f;
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    Environment.Date = GAME_CALENDAR_FIRST_DAY_NUMBER;
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    WorldEnvironment_Deconstruct(&Environment);
}

static void MoonSettingsAreValidated(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);

    Environment.MoonPhase = 1.5f;
    CheckRejected(run, &Environment, "MoonPhase");
    ResetEnvironment(&Environment);
    Environment.MoonPhase = NAN;
    CheckRejected(run, &Environment, "MoonPhase");
    ResetEnvironment(&Environment);
    Environment.MoonCycleDays = 0.0f;
    CheckRejected(run, &Environment, "MoonCycleDays");
    ResetEnvironment(&Environment);
    Environment.MoonCycleDays = -29.5f;
    CheckRejected(run, &Environment, "MoonCycleDays");
    ResetEnvironment(&Environment);
    Environment.MoonPhaseOffsetDays = INFINITY;
    CheckRejected(run, &Environment, "MoonPhaseOffsetDays");
    ResetEnvironment(&Environment);
    Environment.MoonOrbitTilt = NAN;
    CheckRejected(run, &Environment, "MoonOrbitTilt");
    ResetEnvironment(&Environment);
    Environment.MoonSizeMultiplier = 0.0f;
    CheckRejected(run, &Environment, "MoonSizeMultiplier");
    ResetEnvironment(&Environment);
    Environment.MoonBrightness = -0.5f;
    CheckRejected(run, &Environment, "MoonBrightness");
    ResetEnvironment(&Environment);
    Environment.MoonIntensity = -0.01f;
    CheckRejected(run, &Environment, "MoonIntensity");
    ResetEnvironment(&Environment);
    Environment.MoonPhaseSource = (WorldMoonPhaseSource)7;
    CheckRejected(run, &Environment, "MoonPhaseSource");

    // Texture names must be asset names of at most the limit. (Set through the table: it clones and frees the names.)
    ResetEnvironment(&Environment);
    const PropertyTable* Table = WorldEnvironment_GetPropertyTable();
    TEST_CHECK_ERROR(run, Properties_Set(Table, &Environment, u8"moon_texture",
        &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = u8"Not A Name" }), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, Properties_Set(Table, &Environment, u8"milky_way_texture",
        &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = NULL }), ErrorCode_ArgumentOutOfRange);
    unsigned char LongName[WORLD_ENVIRONMENT_TEXTURE_NAME_MAX_LENGTH + 2U];
    memset(LongName, 'a', sizeof(LongName) - 1U);
    LongName[sizeof(LongName) - 1U] = (unsigned char)'\0';
    TEST_CHECK_ERROR(run, Properties_Set(Table, &Environment, u8"moon_texture",
        &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = LongName }), ErrorCode_ArgumentOutOfRange);
    LongName[WORLD_ENVIRONMENT_TEXTURE_NAME_MAX_LENGTH] = (unsigned char)'\0'; // exactly the limit
    TEST_CHECK_SUCCESS(run, Properties_Set(Table, &Environment, u8"moon_texture",
        &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = LongName }));
    TEST_CHECK(run, strlen((const char*)Environment.MoonTexture) == WORLD_ENVIRONMENT_TEXTURE_NAME_MAX_LENGTH);

    // Validate names a broken name too (the name is lent to the environment for the check, then given back).
    unsigned char BadName[] = u8"Bad Name";
    unsigned char* OwnedName = Environment.MoonTexture;
    Environment.MoonTexture = BadName;
    CheckRejected(run, &Environment, "MoonTexture");
    Environment.MoonTexture = OwnedName;

    // The ends of the ranges, and settings any value suits.
    ResetEnvironment(&Environment);
    Environment.MoonPhase = 1.0f;
    Environment.MoonPhaseOffsetDays = -1000.0f;
    Environment.MoonOrbitTilt = 0.0f;
    Environment.MoonIntensity = 0.0f;
    Environment.MoonBrightness = 0.0f;
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.IsMoonEnabled = false;
    Environment.AreSeasonsEnabled = true;
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    Environment.MoonPhase = 0.0f;
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    WorldEnvironment_Deconstruct(&Environment);
}

static void CopyToCopiesEverySetting(TestRun* run)
{
    WorldEnvironment Source;
    WorldEnvironment_SetDefaults(&Source);
    Source.TimeOfDay = 0.3f;
    Source.Date = 740000;
    Source.SkyTint = (Color){ 1, 2, 3, 4 };
    Source.StarSeed = 123456789u;
    Source.IsBloomEnabled = false;
    Source.AmbientOcclusionStrength = 0.25f;
    Source.LightPollution = 0.8f;
    Source.LightPollutionColor = (Color){ 5, 6, 7, 8 };
    Source.MilkyWayBrightness = 2.5f;
    Source.StarTwinkle = 0.75f;
    Source.AreSeasonsEnabled = true;
    Source.IsMoonEnabled = false;
    Source.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Source.MoonPhase = 0.125f;
    Source.MoonIntensity = 0.5f;
    Source.MoonColor = (Color){ 9, 10, 11, 12 };
    Source.HazeHeight = 300.0f;
    Source.HazeAlbedo = (Color){ 13, 14, 15, 16 };
    Source.SunGlow = 0.9f;
    Source.AirDensity = 2.5f;
    Source.Ozone = 0.0f;
    Source.MultipleScattering = 0.25f;
    Source.AerialDistanceScale = 5.0f;
    Source.HazeStartDistance = 30.0f;
    Source.GroundHazeDensity = 0.02f;
    Source.GroundHazeBase = -4.0f;
    Source.GroundHazeThickness = 15.0f;
    Source.GroundHazeAlbedo = (Color){ 17, 18, 19, 20 };
    Source.GroundHazeGlow = 0.5f;
    Source.HorizonGroundHeight = 250.0f;
    TEST_CHECK_SUCCESS(run, Properties_Set(WorldEnvironment_GetPropertyTable(), &Source, u8"moon_texture",
        &(PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = u8"red_moon" }));

    WorldEnvironment Destination;
    WorldEnvironment_SetDefaults(&Destination);
    TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&Source, &Destination));
    TEST_CHECK(run, Destination.TimeOfDay == 0.3f);
    TEST_CHECK(run, Destination.Date == 740000);
    TEST_CHECK(run, (Destination.SkyTint.r == 1) && (Destination.SkyTint.a == 4));
    TEST_CHECK(run, Destination.StarSeed == 123456789u);
    TEST_CHECK(run, !Destination.IsBloomEnabled);
    TEST_CHECK(run, Destination.AmbientOcclusionStrength == 0.25f);
    TEST_CHECK(run, Destination.LightPollution == 0.8f);
    TEST_CHECK(run, (Destination.LightPollutionColor.r == 5) && (Destination.LightPollutionColor.a == 8));
    TEST_CHECK(run, Destination.MilkyWayBrightness == 2.5f);
    TEST_CHECK(run, Destination.StarTwinkle == 0.75f);
    TEST_CHECK(run, Destination.AreSeasonsEnabled && !Destination.IsMoonEnabled);
    TEST_CHECK(run, (Destination.MoonPhaseSource == WorldMoonPhaseSource_Fixed) && (Destination.MoonPhase == 0.125f));
    TEST_CHECK(run, (Destination.MoonIntensity == 0.5f) && (Destination.MoonColor.b == 11));
    TEST_CHECK(run, (Destination.HazeHeight == 300.0f) && (Destination.HazeAlbedo.g == 14) && (Destination.SunGlow == 0.9f));
    TEST_CHECK(run, (Destination.AirDensity == 2.5f) && (Destination.Ozone == 0.0f)
        && (Destination.MultipleScattering == 0.25f));
    TEST_CHECK(run, (Destination.AerialDistanceScale == 5.0f) && (Destination.HazeStartDistance == 30.0f)
        && (Destination.GroundHazeDensity == 0.02f) && (Destination.GroundHazeBase == -4.0f));
    TEST_CHECK(run, (Destination.GroundHazeThickness == 15.0f) && (Destination.GroundHazeAlbedo.b == 19)
        && (Destination.GroundHazeGlow == 0.5f) && (Destination.HorizonGroundHeight == 250.0f));
    // The names are the destination's own copies.
    TEST_CHECK(run, (Destination.MoonTexture != Source.MoonTexture)
        && (strcmp((const char*)Destination.MoonTexture, "red_moon") == 0));
    TEST_CHECK(run, (Destination.MilkyWayTexture != Source.MilkyWayTexture)
        && (strcmp((const char*)Destination.MilkyWayTexture, "sky_milky_way") == 0));
    TEST_CHECK(run, Properties_Equals(WorldEnvironment_GetPropertyTable(), &Source, &Destination));
    TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&Destination, &Destination));
    TEST_CHECK_ERROR(run, WorldEnvironment_CopyTo(NULL, &Destination), ErrorCode_IllegalArgument);
    WorldEnvironment_Deconstruct(&Source);
    WorldEnvironment_Deconstruct(&Destination);
}

/* The environment no longer keeps time; its date and time of day start where a new game clock starts (noon,
 * 2026-10-18), and the clock writes them as a game would. The removed day-night settings' wire ids stay retired. */
static void DateAndTimeComeFromTheClock(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    GameClock Clock;
    GameClock_Construct(&Clock);
    TEST_CHECK(run, Environment.Date == GameClock_GetDay(&Clock));
    TEST_CHECK(run, Environment.TimeOfDay == GameClock_GetTimeOfDay(&Clock));
    GameDate Date = GameCalendar_GetDate(Environment.Date);
    TEST_CHECK(run, (Date.Year == 2026) && (Date.Month == 10) && (Date.Day == 18));

    TEST_CHECK_SUCCESS(run, GameClock_SetDateTime(&Clock, 2026, 12, 24, 18, 0, 0));
    Environment.TimeOfDay = GameClock_GetTimeOfDay(&Clock);
    Environment.Date = GameClock_GetDay(&Clock);
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    TEST_CHECK(run, Environment.TimeOfDay == 0.75f); // dusk: the sun sets on the horizon
    TEST_CHECK(run, fabsf(WorldEnvironment_GetSunDirection(&Environment).y) < 1e-6f);

    const PropertyTable* Table = WorldEnvironment_GetPropertyTable();
    TEST_CHECK(run, (PropertyTable_FindByKey(Table, u8"is_day_night_cycle_enabled") == NULL)
        && (PropertyTable_FindByKey(Table, u8"day_length_seconds") == NULL));
    TEST_CHECK(run, (Table->RetiredWireIdCount == 2U) && (Table->RetiredWireIds[0] == 2U)
        && (Table->RetiredWireIds[1] == 3U));
    const PropertyDescriptor* DateRow = PropertyTable_FindByKey(Table, u8"date");
    TEST_CHECK(run, (DateRow != NULL) && (DateRow->Type == PropertyType_Date) && (DateRow->WireId == 30U));
    GameClock_Deconstruct(&Clock);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The matrix maps the celestial (1,0,0) to the sun at every time of day and several latitudes, is a rotation
 * (orthonormal, no mirror, no translation), and turns the sky around a pole that stays put all day. */
static void CelestialFrameFollowsTheSun(TestRun* run)
{
    const float SunAngles[] = { 0.0f, 0.4f, 0.9f, 1.3f };
    for (size_t AngleIndex = 0U; AngleIndex < (sizeof(SunAngles) / sizeof(SunAngles[0])); AngleIndex++)
    {
        WorldEnvironment Environment;
        WorldEnvironment_SetDefaults(&Environment);
        Environment.SunAngle = SunAngles[AngleIndex];
        Vector3 VisiblePole = { 0.0f, sinf(Environment.SunAngle), -cosf(Environment.SunAngle) };
        for (int Step = 0; Step <= 10; Step++)
        {
            Environment.TimeOfDay = (float)Step * 0.1f;
            Matrix CelestialToWorld = WorldEnvironment_GetCelestialToWorld(&Environment);

            Vector3 Sun = Vector3Transform((Vector3){ 1.0f, 0.0f, 0.0f }, CelestialToWorld);
            TEST_CHECK(run, AreVectorsClose(Sun, WorldEnvironment_GetSunDirection(&Environment)));
            TEST_CHECK(run, AreVectorsClose(Vector3Transform((Vector3){ 0.0f, 0.0f, -1.0f }, CelestialToWorld),
                VisiblePole));

            Vector3 X = Vector3Transform((Vector3){ 1.0f, 0.0f, 0.0f }, CelestialToWorld);
            Vector3 Y = Vector3Transform((Vector3){ 0.0f, 1.0f, 0.0f }, CelestialToWorld);
            Vector3 Z = Vector3Transform((Vector3){ 0.0f, 0.0f, 1.0f }, CelestialToWorld);
            TEST_CHECK(run, fabsf(Vector3Length(X) - 1.0f) < CELESTIAL_TOLERANCE);
            TEST_CHECK(run, fabsf(Vector3Length(Y) - 1.0f) < CELESTIAL_TOLERANCE);
            TEST_CHECK(run, fabsf(Vector3DotProduct(X, Y)) < CELESTIAL_TOLERANCE);
            TEST_CHECK(run, fabsf(Vector3DotProduct(Y, Z)) < CELESTIAL_TOLERANCE);
            TEST_CHECK(run, AreVectorsClose(Vector3CrossProduct(X, Y), Z)); // right-handed: no mirror image
            TEST_CHECK(run, AreVectorsClose(Vector3Transform(Vector3Zero(), CelestialToWorld), Vector3Zero()));

            // The stars turn about the same pole.
            TEST_CHECK(run, AreVectorsClose(Vector3Transform((Vector3){ 0.0f, 0.0f, -1.0f },
                WorldEnvironment_GetStarsToWorld(&Environment)), VisiblePole));
        }
        WorldEnvironment_Deconstruct(&Environment);
    }
}

/* With the defaults the phase is the real moon's: the epoch's new moon, the real full moon of 2000-01-21 (a lunar
 * eclipse), and the real new and full moons of October 2026 (Meeus' true phases) are within a day of the game's. The
 * offset shifts the cycle, the cycle length changes its pace, and a fixed phase ignores the date. */
static void MoonPhaseFollowsTheCalendar(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);

    if (SetDateAndTime(run, &Environment, 2000, 1, 6, 18, 14))
    {
        TEST_CHECK(run, GetPhaseDistance(WorldEnvironment_GetMoonPhase(&Environment), 0.0f) < 0.001f);
    }
    if (SetDateAndTime(run, &Environment, 2000, 1, 21, 4, 40))
    {
        TEST_CHECK(run, GetPhaseDistance(WorldEnvironment_GetMoonPhase(&Environment), 0.5f) < 0.02f);
    }
    if (SetDateAndTime(run, &Environment, 2026, 10, 10, 15, 50))
    {
        TEST_CHECK(run, GetPhaseDistance(WorldEnvironment_GetMoonPhase(&Environment), 0.0f) < 0.03f);
    }
    if (SetDateAndTime(run, &Environment, 2026, 10, 26, 4, 11))
    {
        TEST_CHECK(run, GetPhaseDistance(WorldEnvironment_GetMoonPhase(&Environment), 0.5f) < 0.03f);

        // A quarter of a cycle's offset turns that full moon into a last quarter.
        Environment.MoonPhaseOffsetDays = 0.25f * WORLD_ENVIRONMENT_MOON_CYCLE_DAYS;
        TEST_CHECK(run, GetPhaseDistance(WorldEnvironment_GetMoonPhase(&Environment), 0.75f) < 0.03f);
        Environment.MoonPhaseOffsetDays = -0.5f * WORLD_ENVIRONMENT_MOON_CYCLE_DAYS;
        TEST_CHECK(run, GetPhaseDistance(WorldEnvironment_GetMoonPhase(&Environment), 0.0f) < 0.03f);
        Environment.MoonPhaseOffsetDays = 0.0f;
    }

    // The phase grows by a day over the cycle length: twice as long a cycle, half the pace.
    Environment.Date = 730124;
    Environment.TimeOfDay = 0.7597f;
    float Start = WorldEnvironment_GetMoonPhase(&Environment);
    Environment.Date += 7;
    float Week = WorldEnvironment_GetMoonPhase(&Environment);
    TEST_CHECK(run, fabsf((Week - Start) - 7.0f / WORLD_ENVIRONMENT_MOON_CYCLE_DAYS) < 1e-4f);
    Environment.MoonCycleDays = 2.0f * WORLD_ENVIRONMENT_MOON_CYCLE_DAYS;
    TEST_CHECK(run, fabsf((WorldEnvironment_GetMoonPhase(&Environment) - Start) - 3.5f / WORLD_ENVIRONMENT_MOON_CYCLE_DAYS)
        < 1e-4f);
    Environment.MoonCycleDays = 0.0f; // not a valid cycle: counts as the default
    TEST_CHECK(run, fabsf(WorldEnvironment_GetMoonPhase(&Environment) - Week) < 1e-5f);
    Environment.MoonCycleDays = WORLD_ENVIRONMENT_MOON_CYCLE_DAYS;

    // A fixed phase ignores the date; 1 is new moon again.
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.5f;
    TEST_CHECK(run, WorldEnvironment_GetMoonPhase(&Environment) == 0.5f);
    Environment.Date += 11;
    TEST_CHECK(run, WorldEnvironment_GetMoonPhase(&Environment) == 0.5f);
    Environment.MoonPhase = 1.0f;
    TEST_CHECK(run, WorldEnvironment_GetMoonPhase(&Environment) == 0.0f);
    Environment.MoonPhase = 0.3f;
    TEST_CHECK(run, fabsf(WorldEnvironment_GetMoonPhase(&Environment) - 0.3f) < 1e-6f);
    WorldEnvironment_Deconstruct(&Environment);
}

/* The moon stands with the sun at new moon and opposite it at full moon; the first quarter rises at noon and sets at
 * midnight, the last quarter rises at midnight; the tilt lifts the quarters off the sun's path; and on the calendar the
 * moon rises about 50 minutes later every day. */
static void MoonMovesWithItsPhase(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonOrbitTilt = 0.0f;
    for (int Step = 0; Step < 8; Step++)
    {
        Environment.TimeOfDay = (float)Step * 0.125f;
        Vector3 Sun = WorldEnvironment_GetSunDirection(&Environment);
        Environment.MoonPhase = 0.0f;
        TEST_CHECK(run, AreVectorsClose(WorldEnvironment_GetMoonDirection(&Environment), Sun));
        Environment.MoonPhase = 0.5f;
        TEST_CHECK(run, AreVectorsClose(WorldEnvironment_GetMoonDirection(&Environment), Vector3Negate(Sun)));
    }

    // +X is east: the first quarter is on the east horizon at noon and climbing, on the west horizon at midnight and
    // sinking. The last quarter rises at midnight.
    Environment.MoonPhase = 0.25f;
    Environment.TimeOfDay = 0.5f;
    TEST_CHECK(run, AreVectorsClose(WorldEnvironment_GetMoonDirection(&Environment), (Vector3){ 1.0f, 0.0f, 0.0f }));
    Environment.TimeOfDay = 0.51f;
    TEST_CHECK(run, WorldEnvironment_GetMoonDirection(&Environment).y > 0.0f);
    Environment.TimeOfDay = 0.0f;
    TEST_CHECK(run, AreVectorsClose(WorldEnvironment_GetMoonDirection(&Environment), (Vector3){ -1.0f, 0.0f, 0.0f }));
    Environment.TimeOfDay = 0.01f;
    TEST_CHECK(run, WorldEnvironment_GetMoonDirection(&Environment).y < 0.0f);
    Environment.MoonPhase = 0.75f;
    Environment.TimeOfDay = 0.0f;
    TEST_CHECK(run, AreVectorsClose(WorldEnvironment_GetMoonDirection(&Environment), (Vector3){ 1.0f, 0.0f, 0.0f }));
    Environment.TimeOfDay = 0.01f;
    TEST_CHECK(run, WorldEnvironment_GetMoonDirection(&Environment).y > 0.0f);

    // The tilt: a quarter moon stands MoonOrbitTilt off the celestial equator, toward the pole above the horizon.
    Environment.MoonOrbitTilt = 0.09f;
    Environment.MoonPhase = 0.25f;
    Vector3 VisiblePole = { 0.0f, sinf(Environment.SunAngle), -cosf(Environment.SunAngle) };
    TEST_CHECK(run, fabsf(asinf(Vector3DotProduct(WorldEnvironment_GetMoonDirection(&Environment), VisiblePole)) - 0.09f)
        < 1e-4f);

    // On the calendar: moonrise about 49 minutes later from one day to the next (12.2 degrees of the cycle a day).
    ResetEnvironment(&Environment);
    if (SetDateAndTime(run, &Environment, 2026, 10, 20, 0, 0))
    {
        int32_t FirstRise = FindRiseMinute(&Environment, true);
        Environment.Date += 1;
        int32_t NextRise = FindRiseMinute(&Environment, true);
        TEST_CHECK(run, (FirstRise >= 0) && (NextRise >= 0));
        TEST_CHECK(run, ((NextRise - FirstRise) > 42) && ((NextRise - FirstRise) < 56));
    }
    WorldEnvironment_Deconstruct(&Environment);
}

/* The lit fraction is 0, 0.5 and 1 at new moon, the quarters and full moon, with or without the tilt; the moon's total
 * light is far from linear in it (a half moon gives about a tenth of the full moon's). */
static void MoonIlluminationFollowsThePhase(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.TimeOfDay = 0.3f;
    const float Tilts[] = { 0.0f, 0.09f };
    for (size_t Index = 0U; Index < (sizeof(Tilts) / sizeof(Tilts[0])); Index++)
    {
        Environment.MoonOrbitTilt = Tilts[Index];
        Environment.MoonPhase = 0.0f;
        TEST_CHECK(run, WorldEnvironment_GetMoonIllumination(&Environment) < 1e-5f);
        Environment.MoonPhase = 0.25f;
        TEST_CHECK(run, fabsf(WorldEnvironment_GetMoonIllumination(&Environment) - 0.5f) < 1e-4f);
        Environment.MoonPhase = 0.5f;
        TEST_CHECK(run, WorldEnvironment_GetMoonIllumination(&Environment) > 0.9999f);
        Environment.MoonPhase = 0.75f;
        TEST_CHECK(run, fabsf(WorldEnvironment_GetMoonIllumination(&Environment) - 0.5f) < 1e-4f);
    }

    TEST_CHECK(run, WorldEnvironment_GetMoonBrightness(1.0f) == 1.0f);
    float Half = WorldEnvironment_GetMoonBrightness(0.5f);
    TEST_CHECK(run, (Half > 0.07f) && (Half < 0.12f));
    TEST_CHECK(run, WorldEnvironment_GetMoonBrightness(0.0f) < 0.001f);
    TEST_CHECK(run, (WorldEnvironment_GetMoonBrightness(NAN) == WorldEnvironment_GetMoonBrightness(0.0f))
        && (WorldEnvironment_GetMoonBrightness(2.0f) == 1.0f));
    bool IsMonotonic = true;
    float Previous = WorldEnvironment_GetMoonBrightness(0.0f);
    for (int Step = 1; Step <= 100; Step++)
    {
        float Current = WorldEnvironment_GetMoonBrightness((float)Step / 100.0f);
        IsMonotonic = IsMonotonic && (Current > Previous);
        Previous = Current;
    }
    TEST_CHECK(run, IsMonotonic);
    WorldEnvironment_Deconstruct(&Environment);
}

/* Moonlight: a high full moon at midnight gives all of it, the full moon lights nothing by day (it is down) and a moon
 * low over the horizon little; a quarter moon at dusk gives a tenth of it, dimmed by the twilight; no moon, none. */
static void MoonlightFollowsThePhaseHeightAndDaylight(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.5f;

    Environment.TimeOfDay = 0.0f;
    TEST_CHECK(run, WorldEnvironment_GetMoonlightFactor(&Environment) > 0.999f);
    WorldSkyLights Lights;
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    TEST_CHECK(run, fabsf(Lights.MoonIntensity - Environment.MoonIntensity) < 1e-6f);
    TEST_CHECK(run, (Lights.SunIntensity == 0.0f) && (Lights.Daylight == 0.0f));
    TEST_CHECK(run, (Lights.MoonPhase == 0.5f) && (Lights.MoonIllumination > 0.9999f));

    Environment.TimeOfDay = 0.5f;
    TEST_CHECK(run, WorldEnvironment_GetMoonlightFactor(&Environment) == 0.0f);
    Environment.TimeOfDay = 0.755f; // just risen opposite the setting sun: low, and the twilight is still bright
    float Rising = WorldEnvironment_GetMoonlightFactor(&Environment);
    TEST_CHECK(run, (Rising > 0.0f) && (Rising < 0.1f));

    Environment.MoonPhase = 0.25f;
    Environment.TimeOfDay = 0.8f; // high in the south an hour after sunset
    float Quarter = WorldEnvironment_GetMoonlightFactor(&Environment);
    TEST_CHECK(run, (Quarter > 0.05f) && (Quarter < WorldEnvironment_GetMoonBrightness(0.5f) + 1e-4f));

    Environment.MoonPhase = 0.5f;
    Environment.TimeOfDay = 0.0f;
    Environment.IsMoonEnabled = false;
    TEST_CHECK(run, WorldEnvironment_GetMoonlightFactor(&Environment) == 0.0f);
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    TEST_CHECK(run, (Lights.MoonIntensity == 0.0f) && (Lights.ShadowLight == WorldShadowLight_None));
    WorldEnvironment_Deconstruct(&Environment);
}

/* The stars gain about 0.99 degrees a day on the sun (so at the same time of day they stand that much further along their
 * path, and rise about 4 minutes earlier), a full turn a tropical year, continuously across New Year. */
static void StarsDriftThroughTheYear(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.TimeOfDay = 0.25f;
    Vector3 EquatorStar = Vector3Normalize((Vector3){ 0.6f, 0.8f, 0.0f }); // on the celestial equator
    Vector3 Today = Vector3Transform(EquatorStar, WorldEnvironment_GetStarsToWorld(&Environment));
    double Drift = GetStarDriftDegrees(&Environment);
    Environment.Date += 1;
    Vector3 Tomorrow = Vector3Transform(EquatorStar, WorldEnvironment_GetStarsToWorld(&Environment));
    TEST_CHECK(run, fabs(GetAngleDegrees(Today, Tomorrow) - STAR_DRIFT_DEGREES_PER_DAY) < 0.005);
    TEST_CHECK(run, fabs(fmod(GetStarDriftDegrees(&Environment) - Drift + 360.0, 360.0) - STAR_DRIFT_DEGREES_PER_DAY)
        < 0.005);

    // Half a tropical year later the stars have gained half a turn, a whole year later a whole one.
    Environment.Date += 181;
    Environment.TimeOfDay = 0.25f + 0.6211f;
    TEST_CHECK(run, fabs(fmod(GetStarDriftDegrees(&Environment) - Drift + 360.0, 360.0) - 180.0) < 0.01);
    Environment.Date += 183;
    Environment.TimeOfDay = 0.25f + 0.2422f;
    double Turned = fmod(GetStarDriftDegrees(&Environment) - Drift + 360.0, 360.0);
    TEST_CHECK(run, fmin(Turned, 360.0 - Turned) < 0.01);

    // No jump at New Year: the last and first minutes of the year are a minute's drift apart.
    if (SetDateAndTime(run, &Environment, 2026, 12, 31, 23, 59))
    {
        double OldYear = GetStarDriftDegrees(&Environment);
        SetDateAndTime(run, &Environment, 2027, 1, 1, 0, 0);
        TEST_CHECK(run, fabs(remainder(GetStarDriftDegrees(&Environment) - OldYear, 360.0)) < 0.01);
    }

    // The direction of the drift, by its effect: a star rises 3.93 minutes (236 seconds) earlier the next day. Pick a
    // star of the equator that rises in the middle of the day, so both rises fall on their own dates.
    ResetEnvironment(&Environment);
    for (int Quarter = 0; Quarter < 4; Quarter++)
    {
        float Angle = (float)Quarter * 1.5707963f;
        Vector3 Star = { cosf(Angle), sinf(Angle), 0.0f };
        Environment.Date = GAME_CLOCK_DEFAULT_DAY_NUMBER;
        int32_t FirstRise = FindStarRiseSecond(&Environment, Star);
        if ((FirstRise < (SECONDS_PER_DAY / 5)) || (FirstRise > (SECONDS_PER_DAY * 4 / 5)))
        {
            continue;
        }
        Environment.Date += 1;
        int32_t NextRise = FindStarRiseSecond(&Environment, Star);
        TEST_CHECK(run, abs((FirstRise - NextRise) - 236) <= 3);
        break;
    }
    WorldEnvironment_Deconstruct(&Environment);
}

/* The sun casts the shadows by day, the moon on a full-moon midnight, neither when both are down; and at dusk with a
 * full moon rising the hand-over from the sun to the moon goes through no shadows, without a jump anywhere. */
static void ShadowLightFollowsTheBrighterLight(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.5f;
    WorldSkyLights Lights;

    Environment.TimeOfDay = 0.5f;
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    TEST_CHECK(run, (Lights.ShadowLight == WorldShadowLight_Sun) && (Lights.ShadowFade == 1.0f));
    TEST_CHECK(run, AreVectorsClose(Lights.ShadowDirection, Lights.SunDirection) && (Lights.SunIntensity == 1.0f));
    TEST_CHECK(run, (Lights.MoonIntensity == 0.0f) && (Lights.Daylight == 1.0f));

    Environment.TimeOfDay = 0.0f;
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    TEST_CHECK(run, (Lights.ShadowLight == WorldShadowLight_Moon) && (Lights.ShadowFade > 0.999f));
    TEST_CHECK(run, AreVectorsClose(Lights.ShadowDirection, Lights.MoonDirection));

    // A new moon is down with the sun at midnight: no light casts shadows. Nor does a moon without light.
    Environment.MoonPhase = 0.0f;
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    TEST_CHECK(run, (Lights.ShadowLight == WorldShadowLight_None) && (Lights.ShadowFade == 0.0f));
    TEST_CHECK(run, AreVectorsClose(Lights.ShadowDirection, Lights.SunDirection));
    Environment.MoonPhase = 0.5f;
    Environment.MoonIntensity = 0.0f;
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    TEST_CHECK(run, Lights.ShadowLight == WorldShadowLight_None);
    Environment.MoonIntensity = WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY;

    // An hour after sunset: a quarter moon high in the sky casts shadows, a thin crescent is too dim for one to show.
    Environment.TimeOfDay = 0.8f;
    Environment.MoonPhase = 0.25f;
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    TEST_CHECK(run, (Lights.ShadowLight == WorldShadowLight_Moon) && (Lights.ShadowFade > 0.99f));
    Environment.MoonPhase = 0.1f;
    WorldEnvironment_GetSkyLights(&Environment, &Lights);
    TEST_CHECK(run, (Lights.MoonIntensity > 0.0f) && (Lights.ShadowLight == WorldShadowLight_None));
    Environment.MoonPhase = 0.5f;

    // Dusk to night with a full moon rising: sun shadows, then none, then moon shadows, never a jump.
    bool HasSeenNone = false;
    bool IsOrderRight = true;
    bool IsSmooth = true;
    bool AreSwitchesHidden = true;
    WorldShadowLight Previous = WorldShadowLight_Sun;
    float PreviousFade = 1.0f;
    for (int Step = 0; Step <= 75000; Step++)
    {
        Environment.TimeOfDay = 0.70f + (float)Step * 2e-6f; // steps of 0.17 game seconds
        WorldEnvironment_GetSkyLights(&Environment, &Lights);
        IsSmooth = IsSmooth && (fabsf(Lights.ShadowFade - PreviousFade) < 0.01f);
        if (Lights.ShadowLight != Previous)
        {
            AreSwitchesHidden = AreSwitchesHidden && (PreviousFade < 0.01f) && (Lights.ShadowFade < 0.01f);
            IsOrderRight = IsOrderRight && ((Lights.ShadowLight == WorldShadowLight_None)
                || ((Lights.ShadowLight == WorldShadowLight_Moon) && HasSeenNone));
        }
        HasSeenNone = HasSeenNone || (Lights.ShadowLight == WorldShadowLight_None);
        Previous = Lights.ShadowLight;
        PreviousFade = Lights.ShadowFade;
    }
    TEST_CHECK(run, IsSmooth && AreSwitchesHidden && IsOrderRight && HasSeenNone);
    TEST_CHECK(run, (Previous == WorldShadowLight_Moon) && (PreviousFade > 0.99f));
    WorldEnvironment_Deconstruct(&Environment);
}

/* With the seasons on (and a northern latitude), the June solstice day is longer than 12 hours, the December one
 * shorter, the equinoxes' about 12; the noon sun is higher in summer; the full moon stands low in summer, high in
 * winter. Without them every day lasts 12 hours. (The year is uniform: the model's September equinox falls about two
 * days before the real one, whose sun moves slower through the northern summer.) */
/* The sun's and the moon's light by how high they stand: the ramps the daylight factor and the moonlight use, which the
 * renderer's far ground evaluates where the light stands over a far point of the ground. */
static void LightRampsFollowTheElevation(TestRun* run)
{
    TEST_CHECK(run, WorldEnvironment_GetDaylightForElevation(WORLD_ENVIRONMENT_DAYLIGHT_SET_ELEVATION) == 0.0f);
    TEST_CHECK(run, WorldEnvironment_GetDaylightForElevation(-0.5f) == 0.0f);
    TEST_CHECK(run, WorldEnvironment_GetDaylightForElevation(WORLD_ENVIRONMENT_DAYLIGHT_RISE_ELEVATION) == 1.0f);
    TEST_CHECK(run, WorldEnvironment_GetDaylightForElevation(0.9f) == 1.0f);
    TEST_CHECK(run, WorldEnvironment_GetDaylightForElevation(NAN) == 0.0f);
    float Middle = 0.5f * (WORLD_ENVIRONMENT_DAYLIGHT_SET_ELEVATION + WORLD_ENVIRONMENT_DAYLIGHT_RISE_ELEVATION);
    TEST_CHECK(run, fabsf(WorldEnvironment_GetDaylightForElevation(Middle) - 0.5f) < 1e-6f);
    TEST_CHECK(run, WorldEnvironment_GetMoonlightForElevation(0.0f) == 0.0f);
    TEST_CHECK(run, WorldEnvironment_GetMoonlightForElevation(-0.1f) == 0.0f);
    TEST_CHECK(run, WorldEnvironment_GetMoonlightForElevation(WORLD_ENVIRONMENT_MOONLIGHT_FULL_ELEVATION) == 1.0f);
    TEST_CHECK(run, fabsf(WorldEnvironment_GetMoonlightForElevation(0.1f) - 0.5f) < 1e-6f);
    TEST_CHECK(run, WorldEnvironment_GetMoonlightForElevation(NAN) == 0.0f);

    // The environment's own factors are these ramps at the sun's and the moon's elevation.
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    for (int Step = 0; Step < 48; Step++)
    {
        Environment.TimeOfDay = (float)Step / 48.0f;
        TEST_CHECK(run, WorldEnvironment_GetDaylightFactor(&Environment)
            == WorldEnvironment_GetDaylightForElevation(WorldEnvironment_GetSunDirection(&Environment).y));
    }
    WorldEnvironment_Deconstruct(&Environment);
}

static void SeasonsChangeTheDayLength(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.SunAngle = 0.4f; // about 23 degrees north
    if (!SetDateAndTime(run, &Environment, 2026, 6, 21, 0, 0))
    {
        WorldEnvironment_Deconstruct(&Environment);
        return;
    }
    int64_t JuneSolstice = Environment.Date;
    TEST_CHECK(run, abs(CountDaylightMinutes(&Environment) - 720) <= 1);

    Environment.AreSeasonsEnabled = true;
    int32_t JuneDay = CountDaylightMinutes(&Environment);
    TEST_CHECK(run, (JuneDay > 13 * 60 + 15) && (JuneDay < 13 * 60 + 35)); // 13.4 hours at this latitude
    Environment.TimeOfDay = 0.5f;
    float JuneNoon = WorldEnvironment_GetSunDirection(&Environment).y;

    SetDateAndTime(run, &Environment, 2026, 12, 21, 0, 0);
    int32_t DecemberDay = CountDaylightMinutes(&Environment);
    TEST_CHECK(run, (DecemberDay > 10 * 60 + 25) && (DecemberDay < 10 * 60 + 45));
    Environment.TimeOfDay = 0.5f;
    TEST_CHECK(run, JuneNoon > WorldEnvironment_GetSunDirection(&Environment).y + 0.2f);

    SetDateAndTime(run, &Environment, 2026, 3, 20, 0, 0);
    TEST_CHECK(run, abs(CountDaylightMinutes(&Environment) - 720) < 5);
    SetDateAndTime(run, &Environment, 2026, 9, 21, 0, 0);
    TEST_CHECK(run, abs(CountDaylightMinutes(&Environment) - 720) < 5);

    // The full moon at midnight: low in June, high in December.
    Environment.MoonPhaseSource = WorldMoonPhaseSource_Fixed;
    Environment.MoonPhase = 0.5f;
    Environment.Date = JuneSolstice;
    Environment.TimeOfDay = 0.0f;
    float JuneMoon = WorldEnvironment_GetMoonDirection(&Environment).y;
    SetDateAndTime(run, &Environment, 2026, 12, 21, 0, 0);
    TEST_CHECK(run, WorldEnvironment_GetMoonDirection(&Environment).y > JuneMoon + 0.2f);
    WorldEnvironment_Deconstruct(&Environment);
}


// Public functions.
void WorldEnvironmentTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "defaults_are_valid", DefaultsAreValid },
        { "validate_names_the_broken_field", ValidateNamesTheBrokenField },
        { "moon_settings_are_validated", MoonSettingsAreValidated },
        { "copy_to_copies_every_setting", CopyToCopiesEverySetting },
        { "date_and_time_come_from_the_clock", DateAndTimeComeFromTheClock },
        { "celestial_frame_follows_the_sun", CelestialFrameFollowsTheSun },
        { "moon_phase_follows_the_calendar", MoonPhaseFollowsTheCalendar },
        { "moon_moves_with_its_phase", MoonMovesWithItsPhase },
        { "moon_illumination_follows_the_phase", MoonIlluminationFollowsThePhase },
        { "moonlight_follows_the_phase_height_and_daylight", MoonlightFollowsThePhaseHeightAndDaylight },
        { "stars_drift_through_the_year", StarsDriftThroughTheYear },
        { "shadow_light_follows_the_brighter_light", ShadowLightFollowsTheBrighterLight },
        { "seasons_change_the_day_length", SeasonsChangeTheDayLength },
        { "light_ramps_follow_the_elevation", LightRampsFollowTheElevation },
    };
    TestRun_RunSuite(run, "WorldEnvironment", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
