#include "TestSuites.h"
#include "WorldEnvironment.h"
#include <math.h>
#include <string.h>


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
    Environment.DayLengthSeconds = NAN;
    CheckRejected(run, &Environment, "DayLengthSeconds");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.DayLengthSeconds = 0.0f; // a zero day length cannot advance
    CheckRejected(run, &Environment, "DayLengthSeconds");

    WorldEnvironment_SetDefaults(&Environment);
    Environment.TimeOfDay = 1.5f;
    CheckRejected(run, &Environment, "TimeOfDay");

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

    // Zero is allowed where the rule is ">= 0" (an effect switched off).
    WorldEnvironment_SetDefaults(&Environment);
    Environment.BloomStrength = 0.0f;
    Environment.SkyTurbidity = 0.0f;
    TEST_CHECK_SUCCESS(run, WorldEnvironment_Validate(&Environment));
}

static void CopyToCopiesEverySetting(TestRun* run)
{
    WorldEnvironment Source;
    WorldEnvironment_SetDefaults(&Source);
    Source.TimeOfDay = 0.3f;
    Source.DayLengthSeconds = 77.0f;
    Source.SkyTint = (Color){ 1, 2, 3, 4 };
    Source.StarSeed = 123456789u;
    Source.IsBloomEnabled = false;
    Source.AmbientOcclusionStrength = 0.25f;

    WorldEnvironment Destination;
    WorldEnvironment_SetDefaults(&Destination);
    TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&Source, &Destination));
    TEST_CHECK(run, Destination.TimeOfDay == 0.3f);
    TEST_CHECK(run, Destination.DayLengthSeconds == 77.0f);
    TEST_CHECK(run, (Destination.SkyTint.r == 1) && (Destination.SkyTint.a == 4));
    TEST_CHECK(run, Destination.StarSeed == 123456789u);
    TEST_CHECK(run, !Destination.IsBloomEnabled);
    TEST_CHECK(run, Destination.AmbientOcclusionStrength == 0.25f);
    TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&Destination, &Destination));
    TEST_CHECK_ERROR(run, WorldEnvironment_CopyTo(NULL, &Destination), ErrorCode_IllegalArgument);
}

static void AdvanceWrapsAndIgnoresBadSteps(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.TimeOfDay = 0.9f;
    Environment.DayLengthSeconds = 10.0f;

    WorldEnvironment_Advance(&Environment, 2.0f); // 0.9 + 0.2 wraps to ~0.1
    TEST_CHECK(run, fabsf(Environment.TimeOfDay - 0.1f) < 1e-4f);

    float Before = Environment.TimeOfDay;
    WorldEnvironment_Advance(&Environment, NAN);
    WorldEnvironment_Advance(&Environment, INFINITY);
    WorldEnvironment_Advance(&Environment, -1.0f);
    TEST_CHECK(run, Environment.TimeOfDay == Before);

    // A day length so short that the step overflows must not leave the time of day NaN.
    Environment.DayLengthSeconds = 1e-40f;
    WorldEnvironment_Advance(&Environment, 1.0f);
    TEST_CHECK(run, isfinite(Environment.TimeOfDay));

    Environment.DayLengthSeconds = NAN;
    WorldEnvironment_Advance(&Environment, 1.0f);
    TEST_CHECK(run, isfinite(Environment.TimeOfDay));

    WorldEnvironment_Advance(NULL, 1.0f);
}


// Public functions.
void WorldEnvironmentTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "defaults_are_valid", DefaultsAreValid },
        { "validate_names_the_broken_field", ValidateNamesTheBrokenField },
        { "copy_to_copies_every_setting", CopyToCopiesEverySetting },
        { "advance_wraps_and_ignores_bad_steps", AdvanceWrapsAndIgnoresBadSteps },
    };
    TestRun_RunSuite(run, "WorldEnvironment", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
