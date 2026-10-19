#include "TestSuites.h"
#include "WorldEnvironment.h"
#include "GameClock.h"
#include "raylib/raymath.h"
#include <math.h>
#include <string.h>


// Macros.
/** Tolerance for comparing unit vectors and matrix entries computed in float. */
#define CELESTIAL_TOLERANCE 1e-5f


// Static functions.
/* Checks that @p environment fails validation with ArgumentOutOfRange and a message naming @p fieldName. */
static void CheckRejected(TestRun* run, const WorldEnvironment* environment, const char* fieldName)
{
    Error Result = WorldEnvironment_Validate(environment);
    TEST_CHECK(run, Result.Code == ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, (Result.Message != NULL) && (strstr((const char*)Result.Message, fieldName) != NULL));
    Error_Deconstruct(&Result);
}

static void DefaultsAreValid(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    TEST_CHECK_ERROR(run, WorldEnvironment_Validate(NULL), ErrorCode_IllegalArgument);
}

static void ValidateNamesTheBrokenField(TestRun* run)
{
    WorldEnvironment Environment;

    WorldEnvironment_SetDefaults(&Environment);
    Environment.TimeOfDay = NAN;
    CheckRejected(run, &Environment, "TimeOfDay");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.TimeOfDay = 1.5f;
    CheckRejected(run, &Environment, "TimeOfDay");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.Date = GAME_CALENDAR_FIRST_DAY_NUMBER - 1; // before 0001-01-01
    CheckRejected(run, &Environment, "date (Date) must be a date in the years 1 to 9999");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.Date = GAME_CALENDAR_LAST_DAY_NUMBER + 1; // after 9999-12-31
    CheckRejected(run, &Environment, "date (Date)");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.SunAngle = INFINITY;
    CheckRejected(run, &Environment, "SunAngle");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.SunSizeMultiplier = 0.0f;
    CheckRejected(run, &Environment, "SunSizeMultiplier");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.SunIntensity = -INFINITY;
    CheckRejected(run, &Environment, "SunIntensity");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.FogStrength = -0.5f;
    CheckRejected(run, &Environment, "FogStrength");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.AmbientOcclusionStrength = NAN;
    CheckRejected(run, &Environment, "AmbientOcclusionStrength");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.LightPollution = 1.01f;
    CheckRejected(run, &Environment, "LightPollution");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.LightPollution = -0.01f;
    CheckRejected(run, &Environment, "LightPollution");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.LightPollution = NAN;
    CheckRejected(run, &Environment, "LightPollution");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.StarTwinkle = 1.5f;
    CheckRejected(run, &Environment, "StarTwinkle");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.StarTwinkle = -INFINITY;
    CheckRejected(run, &Environment, "StarTwinkle");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.MilkyWayBrightness = -1.0f;
    CheckRejected(run, &Environment, "MilkyWayBrightness");

    // Zero is allowed where the rule is ">= 0" (an effect switched off), the [0, 1] ranges include both ends, and the
    // calendar's first and last days are dates.
    WorldEnvironment_SetDefaults(&Environment);
    Environment.BloomStrength = 0.0f;
    Environment.SkyTurbidity = 0.0f;
    Environment.MilkyWayBrightness = 0.0f;
    Environment.LightPollution = 1.0f;
    Environment.StarTwinkle = 0.0f;
    Environment.TimeOfDay = 1.0f;
    Environment.Date = GAME_CALENDAR_LAST_DAY_NUMBER;
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
    Environment.Date = GAME_CALENDAR_FIRST_DAY_NUMBER;
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
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
    TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&Destination, &Destination));
    TEST_CHECK_ERROR(run, WorldEnvironment_CopyTo(NULL, &Destination), ErrorCode_IllegalArgument);
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


static bool AreVectorsClose(Vector3 a, Vector3 b)
{
    return (fabsf(a.x - b.x) < CELESTIAL_TOLERANCE) && (fabsf(a.y - b.y) < CELESTIAL_TOLERANCE)
        && (fabsf(a.z - b.z) < CELESTIAL_TOLERANCE);
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
        }
    }
}


// Public functions.
void WorldEnvironmentTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "defaults_are_valid", DefaultsAreValid },
        { "validate_names_the_broken_field", ValidateNamesTheBrokenField },
        { "copy_to_copies_every_setting", CopyToCopiesEverySetting },
        { "date_and_time_come_from_the_clock", DateAndTimeComeFromTheClock },
        { "celestial_frame_follows_the_sun", CelestialFrameFollowsTheSun },
    };
    TestRun_RunSuite(run, "WorldEnvironment", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
