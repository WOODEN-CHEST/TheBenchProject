#include "TestSuites.h"
#include "TestAssets.h"
#include "WeatherSystem.h"
#include "Logger.h"
#include "GameCalendar.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The most preset changes a test records. */
#define TEST_MAX_CHANGES 4096
/** Seconds of an hour. */
#define TEST_HOUR 3600.0

/* A cycle of four presets with a fixed seed, no transitions and no preset rates: every value moves at most at the
 * climate's own rates. */
#define CYCLE_PRESETS \
    "\"presets\": [" \
    " {\"name\": \"clear\", \"cloud_coverage\": 0.1, \"cloud_type\": 0.3, \"cloud_density\": 0.3, \"precipitation\": 0," \
    "  \"wind_speed\": [1, 4], \"temperature\": [10, 20], \"duration_hours\": [2, 6]," \
    "  \"next\": [{\"preset\": \"cloudy\", \"weight\": 3}, {\"preset\": \"clear\", \"weight\": 1}]}," \
    " {\"name\": \"cloudy\", \"cloud_coverage\": 0.6, \"cloud_type\": 0.5, \"cloud_density\": 0.5, \"precipitation\": 0," \
    "  \"wind_speed\": [3, 6], \"duration_hours\": [1, 4]," \
    "  \"next\": [{\"preset\": \"overcast\", \"weight\": 2}, {\"preset\": \"clear\", \"weight\": 2}]}," \
    " {\"name\": \"overcast\", \"cloud_coverage\": 0.9, \"cloud_type\": 0.3, \"cloud_density\": 0.7," \
    "  \"wind_direction\": [100, 260], \"duration_hours\": [1, 5]," \
    "  \"next\": [{\"preset\": \"rain\", \"weight\": 3}, {\"preset\": \"cloudy\", \"weight\": 1}]}," \
    " {\"name\": \"rain\", \"cloud_coverage\": 1, \"cloud_type\": 0.6, \"cloud_density\": 0.9, \"precipitation\": 0.8," \
    "  \"temperature\": [4, 9], \"duration_hours\": [1, 3]," \
    "  \"next\": [{\"preset\": \"overcast\", \"weight\": 2}, {\"preset\": \"cloudy\", \"weight\": 1}]}" \
    "]"
#define CYCLE_CLIMATE(name, seed) "{\"name\": \"" name "\", \"seed\": " seed ", \"start\": \"clear\", " CYCLE_PRESETS "}"


// Types.
/** A climate under test: its asset manager, a world's environment, a clock and the weather. */
typedef struct WeatherRigStruct
{
    TestAssetFixture Fixture;
    WorldEnvironment Environment;
    GameClock Clock;
    WeatherSystem System;
} WeatherRig;

/** The presets a run passed through, in order. */
typedef struct PresetLogStruct
{
    uint16_t Presets[TEST_MAX_CHANGES];
    double Times[TEST_MAX_CHANGES];
    size_t Count;
} PresetLog;


// Static functions.
/* Builds a rig whose world uses `climateName` (registered from each JSON given, NULL-terminated list), its clock at
 * 2026-10-18 12:00. */
static bool Rig_Construct(TestRun* run, WeatherRig* rig, const char* climateName, const char* const* climates)
{
    if (!TestAssetFixture_Construct(run, &rig->Fixture))
    {
        return false;
    }
    bool IsReady = true;
    for (size_t Index = 0U; IsReady && (climates[Index] != NULL); Index++)
    {
        IsReady = TestAssets_AddClimate(run, &rig->Fixture, climates[Index]);
    }
    WorldEnvironment_SetDefaults(&rig->Environment);
    PropertyValue Name = PropertyValue_FromAssetName((const unsigned char*)climateName);
    IsReady = IsReady && TEST_CHECK_SUCCESS(run, Properties_Set(WorldEnvironment_GetPropertyTable(), &rig->Environment,
        (const unsigned char*)u8"climate", &Name));
    GameClock_Construct(&rig->Clock);
    IsReady = IsReady && TEST_CHECK_SUCCESS(run, GameClock_SetDateTime(&rig->Clock, 2026, 10, 18, 12, 0, 0));
    IsReady = IsReady && TEST_CHECK_SUCCESS(run, WeatherSystem_Construct(&rig->System, rig->Fixture.Manager, NULL));
    return IsReady;
}

static void Rig_Deconstruct(TestRun* run, WeatherRig* rig)
{
    WeatherSystem_Deconstruct(&rig->System);
    GameClock_Deconstruct(&rig->Clock);
    WorldEnvironment_Deconstruct(&rig->Environment);
    TestAssetFixture_Deconstruct(run, &rig->Fixture);
}

/* One tick: the clock moves `gameSeconds` (as its time scale would make it), the environment shows its time, the weather
 * moves by those game seconds and `realSeconds` of play. */
static void Rig_Tick(WeatherRig* rig, double gameSeconds, double realSeconds)
{
    GameClockCrossings Crossings = { 0 };
    if (gameSeconds > 0.0)
    {
        Error Result = GameClock_AddGameSeconds(&rig->Clock, gameSeconds, &Crossings);
        Error_Deconstruct(&Result);
    }
    rig->Environment.TimeOfDay = GameClock_GetTimeOfDay(&rig->Clock);
    rig->Environment.Date = GameClock_GetDay(&rig->Clock);
    WeatherSystem_Update(&rig->System, &rig->Environment, &rig->Clock, Crossings.GameSeconds, realSeconds);
}

/* Runs `hours` of game time in ticks of `tickSeconds`, recording each preset change (and the first preset). */
static void Rig_Run(WeatherRig* rig, double hours, double tickSeconds, PresetLog* log)
{
    long TickCount = (long)llround(hours * TEST_HOUR / tickSeconds);
    for (long Tick = 0; Tick < TickCount; Tick++)
    {
        Rig_Tick(rig, tickSeconds, 0.0);
        if (log == NULL)
        {
            continue;
        }
        uint16_t Preset = WeatherSystem_GetSimulation(&rig->System)->Preset;
        if (((log->Count == 0U) || (log->Presets[log->Count - 1U] != Preset)) && (log->Count < TEST_MAX_CHANGES))
        {
            log->Presets[log->Count] = Preset;
            log->Times[log->Count] = (double)GameClock_GetDay(&rig->Clock) * 86400.0
                + GameClock_GetSecondsOfDay(&rig->Clock);
            log->Count++;
        }
    }
}

static bool AreLogsEqual(const PresetLog* a, const PresetLog* b)
{
    if (a->Count != b->Count)
    {
        return false;
    }
    for (size_t Index = 0U; Index < a->Count; Index++)
    {
        if (a->Presets[Index] != b->Presets[Index])
        {
            return false;
        }
    }
    return true;
}

static uint16_t FindPreset(const WeatherRig* rig, const char* name)
{
    uint16_t Index = UINT16_MAX;
    (void)Climate_TryFindPreset(WeatherSystem_GetClimate(&rig->System), (const unsigned char*)name, &Index);
    return Index;
}

static bool AreWeatherValuesEqual(const WorldEnvironment* a, const WorldEnvironment* b)
{
    WeatherValues A = WeatherValues_FromEnvironment(a);
    WeatherValues B = WeatherValues_FromEnvironment(b);
    return memcmp(&A, &B, sizeof(A)) == 0;
}

static PresetLog* NewLog(void)
{
    PresetLog* Log = Memory_Allocate(sizeof(PresetLog));
    Memory_Zero(Log, sizeof(*Log));
    return Log;
}


// Static functions: tests.
static void TheSameSeedGivesTheSameWeather(TestRun* run)
{
    static const char* const Climates[] =
    {
        CYCLE_CLIMATE("cycle", "42"), CYCLE_CLIMATE("other", "43"), NULL
    };
    WeatherRig* Rigs = Memory_Allocate(3U * sizeof(WeatherRig));
    PresetLog* Logs[3] = { NewLog(), NewLog(), NewLog() };
    const char* const Names[3] = { "cycle", "cycle", "other" };
    bool IsReady = true;
    for (int Index = 0; Index < 3; Index++)
    {
        IsReady = Rig_Construct(run, &Rigs[Index], Names[Index], Climates) && IsReady;
    }
    if (IsReady)
    {
        for (int Index = 0; Index < 3; Index++)
        {
            Rig_Run(&Rigs[Index], 100.0, 60.0, Logs[Index]);
        }
        TEST_CHECK(run, WeatherSystem_GetSeed(&Rigs[0].System) == 42U);
        TEST_CHECK(run, Logs[0]->Count > 10U); // the weather changed many times in 100 hours
        TEST_CHECK(run, AreLogsEqual(Logs[0], Logs[1]));
        TEST_CHECK(run, AreWeatherValuesEqual(&Rigs[0].Environment, &Rigs[1].Environment));
        bool IsDifferent = !AreLogsEqual(Logs[0], Logs[2]);
        for (size_t Index = 0U; !IsDifferent && (Index < Logs[0]->Count); Index++)
        {
            IsDifferent = Logs[0]->Times[Index] != Logs[2]->Times[Index];
        }
        TEST_CHECK(run, IsDifferent);
    }
    for (int Index = 0; Index < 3; Index++)
    {
        Rig_Deconstruct(run, &Rigs[Index]);
        Memory_Free(Logs[Index]);
    }
    Memory_Free(Rigs);
}

static void ValuesStayInRangeAndWithinTheirRates(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "7"), NULL };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "cycle", Climates))
    {
        const double TickSeconds = 30.0;
        Rig_Tick(&Rig, 0.0, 0.0); // starts the weather
        const ClimateSettings* Settings = Climate_GetSettings(WeatherSystem_GetClimate(&Rig.System));
        WeatherValues Before = WeatherValues_FromEnvironment(&Rig.Environment);
        bool AreInRange = true;
        bool AreWithinRates = true;
        for (int Tick = 0; Tick < 24000; Tick++) // 200 hours
        {
            Rig_Tick(&Rig, TickSeconds, 0.0);
            WeatherValues After = WeatherValues_FromEnvironment(&Rig.Environment);
            for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
            {
                const WeatherQuantityInfo* Info = WeatherQuantity_GetInfo((WeatherQuantity)Quantity);
                float Value = After.Quantities[Quantity];
                AreInRange = AreInRange && isfinite(Value) && (Info->IsAngle ? ((Value >= 0.0f) && (Value < 6.2832f))
                    : ((Value >= Info->Minimum) && (Value <= Info->Maximum)));
                double Change = fabs((double)Value - (double)Before.Quantities[Quantity]);
                if (Info->IsAngle)
                {
                    Change = fmin(Change, 6.283185307179586 - Change);
                }
                double Allowed = (double)Settings->RatesPerHour[Quantity] * TickSeconds / TEST_HOUR * 1.0001 + 1.0e-6;
                AreWithinRates = AreWithinRates && (Change <= Allowed);
            }
            AreInRange = AreInRange && (After.Wetness >= 0.0f) && (After.Wetness <= 1.0f);
            Before = After;
        }
        TEST_CHECK(run, AreInRange);
        TEST_CHECK(run, AreWithinRates);
        TEST_CHECK(run, Properties_Validate(WeatherSystem_GetPropertyTable(), &Rig.System).Code == ErrorCode_Success);
    }
    Rig_Deconstruct(run, &Rig);
}

static void RainNeedsDenseClouds(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"thin\", \"seed\": 1, \"mode\": \"fixed\", \"fixed_preset\": \"drizzle\", \"presets\": ["
        " {\"name\": \"drizzle\", \"cloud_coverage\": 0.4, \"cloud_density\": 0.5, \"precipitation\": 1}]}",
        "{\"name\": \"thick\", \"seed\": 1, \"mode\": \"fixed\", \"fixed_preset\": \"pour\", \"presets\": ["
        " {\"name\": \"pour\", \"cloud_coverage\": 1, \"cloud_density\": 1, \"precipitation\": 1}]}",
        NULL
    };
    WeatherRig Thin;
    WeatherRig Thick;
    if (Rig_Construct(run, &Thin, "thin", Climates) && Rig_Construct(run, &Thick, "thick", Climates))
    {
        float MostRain = 0.0f;
        for (int Hour = 0; Hour < 24; Hour++)
        {
            Rig_Run(&Thin, 1.0, 60.0, NULL);
            MostRain = fmaxf(MostRain, Thin.Environment.Precipitation);
        }
        TEST_CHECK(run, MostRain == 0.0f); // coverage x density 0.2: below the rain cloud range
        Rig_Run(&Thick, 24.0, 60.0, NULL);
        TEST_CHECK(run, Thick.Environment.Precipitation == 1.0f);
    }
    Rig_Deconstruct(run, &Thin);
    Rig_Deconstruct(run, &Thick);
}

static void WetnessRisesInRainAndDriesAfterwards(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"wet\", \"seed\": 1, \"mode\": \"scripted\", \"presets\": ["
        " {\"name\": \"dry\", \"cloud_coverage\": 0, \"cloud_density\": 0, \"precipitation\": 0, \"wind_speed\": 0},"
        " {\"name\": \"pour\", \"cloud_coverage\": 1, \"cloud_density\": 1, \"precipitation\": 1, \"wind_speed\": 0}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "wet", Climates))
    {
        Rig_Tick(&Rig, 0.0, 0.0);
        TEST_CHECK(run, Rig.Environment.Wetness == 0.0f);
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"pour", 0.0f));
        Rig_Run(&Rig, 0.25, 30.0, NULL);
        float AfterQuarterHour = Rig.Environment.Wetness;
        Rig_Run(&Rig, 2.0, 30.0, NULL);
        TEST_CHECK(run, (AfterQuarterHour > 0.2f) && (AfterQuarterHour < 0.5f)); // 1.5 an hour in a downpour
        TEST_CHECK(run, Rig.Environment.Wetness == 1.0f);

        // It dries afterwards: slowly at night, faster in the sun, faster again in the wind.
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"dry", 0.0f));
        TEST_CHECK_SUCCESS(run, GameClock_SetDateTime(&Rig.Clock, 2026, 10, 19, 0, 0, 0));
        Rig_Run(&Rig, 2.0, 30.0, NULL);
        float NightDrying = 1.0f - Rig.Environment.Wetness;
        TEST_CHECK(run, (NightDrying > 0.2f) && (NightDrying < 0.4f)); // 0.15 an hour at night without wind
        Rig.Environment.Wetness = 1.0f;
        TEST_CHECK_SUCCESS(run, GameClock_SetDateTime(&Rig.Clock, 2026, 10, 19, 12, 0, 0));
        Rig_Run(&Rig, 2.0, 30.0, NULL);
        float DayDrying = 1.0f - Rig.Environment.Wetness;
        TEST_CHECK(run, DayDrying > 2.5f * NightDrying);
        float Windy[WeatherQuantity_Count] = { 0 };
        Windy[WeatherQuantity_WindSpeed] = 10.0f;
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetValuesNow(&Rig.System, &Rig.Environment,
            (uint32_t)1U << WeatherQuantity_WindSpeed, Windy));
        Rig.Environment.Wetness = 1.0f;
        TEST_CHECK_SUCCESS(run, GameClock_SetDateTime(&Rig.Clock, 2026, 10, 20, 12, 0, 0));
        Rig_Run(&Rig, 1.0, 30.0, NULL);
        float WindyDrying = 1.0f - Rig.Environment.Wetness;
        TEST_CHECK(run, WindyDrying > 0.9f * DayDrying); // twice as fast for half the time
    }
    Rig_Deconstruct(run, &Rig);
}

static void PresetsFollowTheirWeightsAndMonths(TestRun* run)
{
    // A hub with two ways out, three to one; b is never chosen in October.
    static const char* const Climates[] =
    {
        "{\"name\": \"hub\", \"seed\": 99, \"presets\": ["
        " {\"name\": \"hub\", \"duration_hours\": 0.1, \"next\": [{\"preset\": \"a\", \"weight\": 3},"
        "  {\"preset\": \"b\", \"weight\": 1}]},"
        " {\"name\": \"a\", \"duration_hours\": 0.1, \"next\": [{\"preset\": \"hub\"}]},"
        " {\"name\": \"b\", \"duration_hours\": 0.1, \"next\": [{\"preset\": \"hub\"}]}]}",
        "{\"name\": \"season\", \"seed\": 99, \"presets\": ["
        " {\"name\": \"hub\", \"duration_hours\": 0.1, \"next\": [{\"preset\": \"a\"}, {\"preset\": \"b\"}]},"
        " {\"name\": \"a\", \"duration_hours\": 0.1, \"next\": [{\"preset\": \"hub\"}]},"
        " {\"name\": \"b\", \"duration_hours\": 0.1, \"month_weights\": [1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1],"
        "  \"next\": [{\"preset\": \"hub\"}]}]}",
        NULL
    };
    WeatherRig Weights;
    WeatherRig Seasons;
    PresetLog* WeightLog = NewLog();
    PresetLog* SeasonLog = NewLog();
    if (Rig_Construct(run, &Weights, "hub", Climates) && Rig_Construct(run, &Seasons, "season", Climates))
    {
        Rig_Run(&Weights, 400.0, 60.0, WeightLog);
        Rig_Run(&Seasons, 200.0, 60.0, SeasonLog);
        size_t A = 0U;
        size_t B = 0U;
        for (size_t Index = 0U; Index < WeightLog->Count; Index++)
        {
            A += (WeightLog->Presets[Index] == FindPreset(&Weights, "a")) ? 1U : 0U;
            B += (WeightLog->Presets[Index] == FindPreset(&Weights, "b")) ? 1U : 0U;
        }
        double Ratio = (B > 0U) ? (double)A / (double)B : 0.0;
        TEST_CHECK(run, (A + B) > 1500U);
        TEST_CHECK(run, (Ratio > 2.6) && (Ratio < 3.4));
        size_t SeasonB = 0U;
        for (size_t Index = 0U; Index < SeasonLog->Count; Index++)
        {
            SeasonB += (SeasonLog->Presets[Index] == FindPreset(&Seasons, "b")) ? 1U : 0U;
        }
        TEST_CHECK(run, (SeasonLog->Count > 500U) && (SeasonB == 0U)); // all of it in October
    }
    Rig_Deconstruct(run, &Weights);
    Rig_Deconstruct(run, &Seasons);
    Memory_Free(WeightLog);
    Memory_Free(SeasonLog);
}

static void StartHoursHold(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"dawn\", \"seed\": 5, \"presets\": ["
        " {\"name\": \"day\", \"duration_hours\": [0.5, 2], \"next\": [{\"preset\": \"fog\", \"weight\": 100},"
        "  {\"preset\": \"day\"}]},"
        " {\"name\": \"fog\", \"duration_hours\": [0.5, 1], \"start_hours\": [3, 9], \"next\": [{\"preset\": \"day\"}]}]}",
        NULL
    };
    WeatherRig Rig;
    PresetLog* Log = NewLog();
    if (Rig_Construct(run, &Rig, "dawn", Climates))
    {
        Rig_Run(&Rig, 30.0 * 24.0, 60.0, Log);
        uint16_t Fog = FindPreset(&Rig, "fog");
        size_t FogCount = 0U;
        bool AreAllInTheMorning = true;
        for (size_t Index = 0U; Index < Log->Count; Index++)
        {
            if (Log->Presets[Index] != Fog)
            {
                continue;
            }
            FogCount++;
            // The change is seen at the end of its tick: within a minute after it began.
            double Hour = fmod(Log->Times[Index], 86400.0) / TEST_HOUR;
            AreAllInTheMorning = AreAllInTheMorning && (Hour >= 3.0) && (Hour < 9.0 + 1.0 / 60.0);
        }
        TEST_CHECK(run, FogCount > 20U);
        TEST_CHECK(run, AreAllInTheMorning);
    }
    Rig_Deconstruct(run, &Rig);
    Memory_Free(Log);
}

static void FixedHoldsWithWetnessFollowing(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"stuck\", \"seed\": 3, \"mode\": \"fixed\", \"fixed_preset\": \"rain\", \"presets\": ["
        " {\"name\": \"clear\", \"duration_hours\": 0.5, \"next\": [{\"preset\": \"rain\"}]},"
        " {\"name\": \"rain\", \"cloud_coverage\": 1, \"cloud_density\": 0.9, \"precipitation\": 0.7,"
        "  \"duration_hours\": 0.5, \"next\": [{\"preset\": \"clear\"}]}]}",
        "{\"name\": \"values\", \"seed\": 3, \"mode\": \"fixed\", \"fixed_values\": {\"cloud_coverage\": 0.5,"
        " \"temperature\": -2}, \"presets\": [{\"name\": \"clear\", \"cloud_coverage\": 0.1, \"duration_hours\": 0.5}]}",
        NULL
    };
    WeatherRig Rig;
    WeatherRig Values;
    if (Rig_Construct(run, &Rig, "stuck", Climates) && Rig_Construct(run, &Values, "values", Climates))
    {
        Rig_Run(&Rig, 48.0, 120.0, NULL);
        TEST_CHECK(run, WeatherSystem_GetSimulation(&Rig.System)->Preset == FindPreset(&Rig, "rain"));
        TEST_CHECK(run, (Rig.Environment.CloudCoverage == 1.0f) && (Rig.Environment.Precipitation == 0.7f));
        TEST_CHECK(run, Rig.Environment.Wetness == 1.0f);
        Rig_Run(&Values, 48.0, 120.0, NULL);
        TEST_CHECK(run, (Values.Environment.CloudCoverage == 0.5f) && (Values.Environment.Temperature == -2.0f));
    }
    Rig_Deconstruct(run, &Rig);
    Rig_Deconstruct(run, &Values);
}

static void ScriptedChangesOnlyThroughTheAPI(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"story\", \"seed\": 3, \"mode\": \"scripted\", \"presets\": ["
        " {\"name\": \"clear\", \"cloud_coverage\": 0.1, \"duration_hours\": 0.5, \"next\": [{\"preset\": \"rain\"}]},"
        " {\"name\": \"rain\", \"cloud_coverage\": 1, \"cloud_density\": 0.9, \"precipitation\": 0.7,"
        "  \"duration_hours\": 0.5, \"next\": [{\"preset\": \"clear\"}]}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "story", Climates))
    {
        WorldEnvironment Before;
        WorldEnvironment_SetDefaults(&Before);
        Rig_Tick(&Rig, 0.0, 0.0);
        TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&Rig.Environment, &Before));
        Rig_Run(&Rig, 50.0, 60.0, NULL);
        TEST_CHECK(run, AreWeatherValuesEqual(&Rig.Environment, &Before)); // nothing moved by itself
        TEST_CHECK(run, WeatherSystem_GetSimulation(&Rig.System)->Preset == FindPreset(&Rig, "clear"));

        TEST_CHECK_SUCCESS(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"rain", 1.0f));
        Rig_Run(&Rig, 0.5, 30.0, NULL);
        TEST_CHECK(run, (Rig.Environment.CloudCoverage > 0.4f) && (Rig.Environment.CloudCoverage < 0.6f));
        Rig_Run(&Rig, 0.6, 30.0, NULL);
        TEST_CHECK(run, Rig.Environment.CloudCoverage == 1.0f); // arrived after the transition's hour
        Rig_Run(&Rig, 10.0, 60.0, NULL);
        TEST_CHECK(run, WeatherSystem_GetSimulation(&Rig.System)->Preset == FindPreset(&Rig, "rain"));

        float Values[WeatherQuantity_Count] = { 0 };
        Values[WeatherQuantity_CloudCoverage] = 0.3f;
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetValuesNow(&Rig.System, &Rig.Environment,
            (uint32_t)1U << WeatherQuantity_CloudCoverage, Values));
        TEST_CHECK(run, Rig.Environment.CloudCoverage == 0.3f);
        Rig_Run(&Rig, 5.0, 60.0, NULL);
        TEST_CHECK(run, Rig.Environment.CloudCoverage == 0.3f); // it stays where the script put it
        Values[WeatherQuantity_CloudCoverage] = 2.0f;
        TEST_CHECK_ERROR(run, WeatherSystem_SetValuesNow(&Rig.System, &Rig.Environment,
            (uint32_t)1U << WeatherQuantity_CloudCoverage, Values), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Rig.Environment.CloudCoverage == 0.3f);
        TEST_CHECK_ERROR(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"snow", 1.0f),
            ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"rain", -1.0f),
            ErrorCode_ArgumentOutOfRange);
        WorldEnvironment_Deconstruct(&Before);
    }
    Rig_Deconstruct(run, &Rig);
}

static void OffLeavesTheEnvironmentAlone(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "11"), NULL };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "cycle", Climates))
    {
        Rig_Run(&Rig, 5.0, 60.0, NULL);
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetMode(&Rig.System, WeatherMode_Off));
        WorldEnvironment Before;
        WorldEnvironment_SetDefaults(&Before);
        TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&Rig.Environment, &Before));
        for (int Tick = 0; Tick < 2000; Tick++)
        {
            Rig_Tick(&Rig, 60.0, 0.05);
        }
        Before.TimeOfDay = Rig.Environment.TimeOfDay; // the test's clock writes these, not the weather
        Before.Date = Rig.Environment.Date;
        TEST_CHECK(run, Properties_Equals(WorldEnvironment_GetPropertyTable(), &Rig.Environment, &Before));
        WorldEnvironment_Deconstruct(&Before);
        TEST_CHECK_ERROR(run, WeatherSystem_SetMode(&Rig.System, (WeatherMode)7), ErrorCode_ArgumentOutOfRange);
    }
    Rig_Deconstruct(run, &Rig);
}

static void LockingHoldsAndResumes(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "12"), NULL };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "cycle", Climates))
    {
        Rig_Run(&Rig, 1.0, 60.0, NULL);
        WeatherSystem_SetLocked(&Rig.System, true);
        WeatherValues Held = WeatherValues_FromEnvironment(&Rig.Environment);
        double HoursLeft = WeatherSystem_GetSimulation(&Rig.System)->PresetHoursLeft;
        Rig_Run(&Rig, 30.0, 60.0, NULL);
        WeatherValues After = WeatherValues_FromEnvironment(&Rig.Environment);
        TEST_CHECK(run, memcmp(&Held, &After, sizeof(Held)) == 0);
        TEST_CHECK(run, WeatherSystem_GetSimulation(&Rig.System)->PresetHoursLeft == HoursLeft);
        WeatherSystem_SetLocked(&Rig.System, false);
        Rig_Run(&Rig, 30.0, 60.0, NULL);
        After = WeatherValues_FromEnvironment(&Rig.Environment);
        TEST_CHECK(run, memcmp(&Held, &After, sizeof(Held)) != 0);
    }
    Rig_Deconstruct(run, &Rig);
}

static void LightningStrikesInStormsAtItsRate(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"storm\", \"seed\": 21, \"mode\": \"fixed\", \"fixed_preset\": \"storm\", \"presets\": ["
        " {\"name\": \"storm\", \"cloud_coverage\": 1, \"cloud_type\": 1, \"cloud_density\": 1, \"transition_hours\": 0,"
        "  \"lightning\": {\"per_minute\": 30, \"min_distance\": 500, \"max_distance\": 4000}}]}",
        "{\"name\": \"calm\", \"seed\": 21, \"mode\": \"fixed\", \"fixed_preset\": \"calm\", \"presets\": ["
        " {\"name\": \"calm\", \"cloud_coverage\": 0.3, \"cloud_type\": 0.2, \"cloud_density\": 0.3, \"transition_hours\": 0,"
        "  \"lightning\": {\"per_minute\": 30}}]}",
        "{\"name\": \"hourly\", \"seed\": 21, \"mode\": \"fixed\", \"fixed_preset\": \"storm\", \"presets\": ["
        " {\"name\": \"storm\", \"cloud_coverage\": 1, \"cloud_type\": 1, \"cloud_density\": 1, \"transition_hours\": 0,"
        "  \"lightning\": {\"per_hour\": 120}}]}",
        NULL
    };
    WeatherRig Storm;
    WeatherRig Calm;
    WeatherRig Hourly;
    if (Rig_Construct(run, &Storm, "storm", Climates) && Rig_Construct(run, &Calm, "calm", Climates)
        && Rig_Construct(run, &Hourly, "hourly", Climates))
    {
        Rig_Tick(&Storm, 1.0, 0.0); // the storm's values at once
        Rig_Tick(&Calm, 1.0, 0.0);
        size_t StormStrikes = 0U;
        size_t CalmStrikes = 0U;
        bool AreDistancesRight = true;
        for (int Tick = 0; Tick < 6000; Tick++) // 10 real minutes, the clock standing still
        {
            Rig_Tick(&Storm, 0.0, 0.1);
            Rig_Tick(&Calm, 0.0, 0.1);
            size_t Count = 0U;
            const WeatherStrike* Strikes = WeatherSystem_GetStrikes(&Storm.System, &Count);
            for (size_t Index = 0U; Index < Count; Index++)
            {
                AreDistancesRight = AreDistancesRight && (Strikes[Index].Distance >= 500.0f)
                    && (Strikes[Index].Distance <= 4000.0f) && (fabsf(Strikes[Index].Direction.y) == 0.0f)
                    && (fabsf(Vector3Length(Strikes[Index].Direction) - 1.0f) < 1.0e-5f);
            }
            StormStrikes += Count;
            WeatherSystem_ClearStrikes(&Storm.System);
            (void)WeatherSystem_GetStrikes(&Calm.System, &Count);
            CalmStrikes += Count;
            WeatherSystem_ClearStrikes(&Calm.System);
        }
        // 30 a minute for 10 minutes, at most one per 0.1 s tick: about 300 (300 x (1 - e^-0.05) / 0.05 = 293).
        TEST_CHECK(run, (StormStrikes > 240U) && (StormStrikes < 350U));
        TEST_CHECK(run, AreDistancesRight);
        TEST_CHECK(run, CalmStrikes == 0U); // its clouds are not storm clouds

        // Strikes given per game hour count the weather's own time: 120 an hour for 2 hours.
        Rig_Tick(&Hourly, 1.0, 0.0);
        size_t HourlyStrikes = 0U;
        for (int Tick = 0; Tick < 7200; Tick++)
        {
            Rig_Tick(&Hourly, 1.0, 0.0);
            size_t Count = 0U;
            (void)WeatherSystem_GetStrikes(&Hourly.System, &Count);
            HourlyStrikes += Count;
            WeatherSystem_ClearStrikes(&Hourly.System);
        }
        TEST_CHECK(run, (HourlyStrikes > 190U) && (HourlyStrikes < 290U));
        // At a quarter of the speed, 2 game hours are half a weather hour: about 60.
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetSpeed(&Hourly.System, 0.25f));
        HourlyStrikes = 0U;
        for (int Tick = 0; Tick < 7200; Tick++)
        {
            Rig_Tick(&Hourly, 1.0, 0.0);
            size_t Count = 0U;
            (void)WeatherSystem_GetStrikes(&Hourly.System, &Count);
            HourlyStrikes += Count;
            WeatherSystem_ClearStrikes(&Hourly.System);
        }
        TEST_CHECK(run, (HourlyStrikes > 35U) && (HourlyStrikes < 90U));

        // The flash: 1 at a strike, gone within a real second (the return strokes too).
        WeatherSystem_Strike(&Calm.System, &Calm.Clock);
        Rig_Tick(&Calm, 0.0, 0.0);
        TEST_CHECK(run, Calm.Environment.LightningFlash == 1.0f);
        TEST_CHECK(run, (Calm.Environment.LightningDistance >= 300.0f) && (Calm.Environment.LightningDistance <= 8000.0f));
        for (int Tick = 0; Tick < 20; Tick++)
        {
            Rig_Tick(&Calm, 0.0, 0.05);
        }
        TEST_CHECK(run, Calm.Environment.LightningFlash == 0.0f);
    }
    Rig_Deconstruct(run, &Storm);
    Rig_Deconstruct(run, &Calm);
    Rig_Deconstruct(run, &Hourly);
}

static void GustsRiseAndFallSmoothly(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"gusty\", \"seed\": 8, \"mode\": \"fixed\", \"fixed_preset\": \"gusty\", \"presets\": ["
        " {\"name\": \"gusty\", \"wind_speed\": 6, \"gusts\": {\"strength\": 6, \"per_minute\": 6}}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "gusty", Climates))
    {
        const double Tick = 1.0 / 60.0;
        float Previous = 0.0f;
        float Highest = 0.0f;
        bool AreSmooth = true;
        bool AreWithinStrength = true;
        int Peaks = 0;
        bool WasRising = false;
        for (int Step = 0; Step < 36000; Step++) // 10 real minutes
        {
            Rig_Tick(&Rig, 0.0, Tick);
            float Gust = Rig.Environment.WindGust;
            // A smoothstep's steepest slope is 1.5 x its rise over its time: 6 m/s over at least 1.5 s.
            AreSmooth = AreSmooth && (fabsf(Gust - Previous) <= (float)(6.0 * 1.5 / 1.5 * Tick) + 1.0e-4f);
            AreWithinStrength = AreWithinStrength && (Gust >= 0.0f) && (Gust <= 6.0f);
            bool IsRising = Gust > Previous;
            Peaks += (WasRising && !IsRising && (Gust > 3.0f)) ? 1 : 0;
            WasRising = IsRising;
            Highest = fmaxf(Highest, Gust);
            Previous = Gust;
        }
        TEST_CHECK(run, AreSmooth);
        TEST_CHECK(run, AreWithinStrength);
        TEST_CHECK(run, Highest > 4.0f);
        TEST_CHECK(run, (Peaks > 30) && (Peaks < 90)); // about 6 a minute

        // Each rise heads for a peak between half the strength and all of it, each fall for a lull below a fifth, along
        // a smoothstep; the lock holds the game-time weather, not the gusts.
        WeatherSystem_SetLocked(&Rig.System, true);
        bool AreEndsRight = true;
        bool AreSmoothSteps = true;
        int Rises = 0;
        int Falls = 0;
        float From = Rig.System._gustFrom;
        float To = Rig.System._gustTo;
        float Lowest = 1.0e9f;
        float HighestLocked = 0.0f;
        for (int Step = 0; Step < 7200; Step++) // 2 real minutes, locked
        {
            Rig_Tick(&Rig, 0.0, Tick);
            const WeatherSystem* System = &Rig.System;
            if ((System->_gustFrom != From) || (System->_gustTo != To))
            {
                From = System->_gustFrom;
                To = System->_gustTo;
                Rises += System->_isGustRising ? 1 : 0;
                Falls += System->_isGustRising ? 0 : 1;
                AreEndsRight = AreEndsRight && (System->_isGustRising ? ((To >= 3.0f) && (To <= 6.0f))
                    : ((To >= 0.0f) && (To <= 1.2f)));
            }
            float Share = fminf(fmaxf(System->_gustSeconds / System->_gustDuration, 0.0f), 1.0f);
            float Expected = From + (To - From) * Share * Share * (3.0f - 2.0f * Share);
            AreSmoothSteps = AreSmoothSteps && (fabsf(Rig.Environment.WindGust - Expected) < 1.0e-4f);
            Lowest = fminf(Lowest, Rig.Environment.WindGust);
            HighestLocked = fmaxf(HighestLocked, Rig.Environment.WindGust);
        }
        TEST_CHECK(run, AreEndsRight && (Rises > 5) && (Falls > 5));
        TEST_CHECK(run, AreSmoothSteps);
        TEST_CHECK(run, (HighestLocked - Lowest) > 2.0f);
    }
    Rig_Deconstruct(run, &Rig);
}

static void TheForecastIsWhatThenHappens(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "31"), NULL };
    WeatherRig Rig;
    GenericBuffer Steps;
    GenericBuffer_AllocateVariable(&Steps, 0U, sizeof(WeatherForecastStep));
    if (Rig_Construct(run, &Rig, "cycle", Climates))
    {
        Rig_Run(&Rig, 3.0, 60.0, NULL);
        TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Rig.System, &Rig.Environment, &Rig.Clock, 48.0, 1.0, &Steps));
        TEST_CHECK(run, Steps._count == 48U);
        const WeatherForecastStep* Forecast = (const WeatherForecastStep*)Steps._data;
        bool ArePresetsRight = true;
        bool AreValuesClose = true;
        bool IsBlurredAnywhere = false;
        size_t Changes = 0U;
        for (size_t Hour = 0U; Hour < Steps._count; Hour++)
        {
            Rig_Run(&Rig, 1.0, 20.0, NULL);
            ArePresetsRight = ArePresetsRight && (Forecast[Hour].Preset == WeatherSystem_GetSimulation(&Rig.System)->Preset)
                && (Forecast[Hour].Day == GameClock_GetDay(&Rig.Clock));
            Changes += ((Hour > 0U) && (Forecast[Hour].Preset != Forecast[Hour - 1U].Preset)) ? 1U : 0U;
            IsBlurredAnywhere = IsBlurredAnywhere || Forecast[Hour].IsBlurred;
            WeatherValues Actual = WeatherValues_FromEnvironment(&Rig.Environment);
            for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
            {
                AreValuesClose = AreValuesClose
                    && (fabsf(Forecast[Hour].Values.Quantities[Quantity] - Actual.Quantities[Quantity]) < 0.01f);
            }
            AreValuesClose = AreValuesClose && (fabsf(Forecast[Hour].Values.Wetness - Actual.Wetness) < 0.01f);
        }
        TEST_CHECK(run, Changes > 5U);
        TEST_CHECK(run, ArePresetsRight);
        TEST_CHECK(run, AreValuesClose);
        TEST_CHECK(run, !IsBlurredAnywhere); // accuracy 1 by default
        TEST_CHECK_ERROR(run, WeatherSystem_Forecast(&Rig.System, &Rig.Environment, &Rig.Clock, 0.0, 1.0, &Steps),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WeatherSystem_Forecast(&Rig.System, &Rig.Environment, &Rig.Clock, 48.0, 0.001, &Steps),
            ErrorCode_ArgumentOutOfRange);
    }
    Memory_Free(Steps._data);
    Rig_Deconstruct(run, &Rig);
}

static void ABlurredForecastIsStable(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"vague\", \"seed\": 31, \"forecast_accuracy\": 0.2, \"start\": \"clear\", " CYCLE_PRESETS "}",
        CYCLE_CLIMATE("cycle", "31"),
        NULL
    };
    WeatherRig Vague;
    WeatherRig Exact;
    GenericBuffer First;
    GenericBuffer Second;
    GenericBuffer Truth;
    GenericBuffer_AllocateVariable(&First, 0U, sizeof(WeatherForecastStep));
    GenericBuffer_AllocateVariable(&Second, 0U, sizeof(WeatherForecastStep));
    GenericBuffer_AllocateVariable(&Truth, 0U, sizeof(WeatherForecastStep));
    if (Rig_Construct(run, &Vague, "vague", Climates) && Rig_Construct(run, &Exact, "cycle", Climates))
    {
        Rig_Run(&Vague, 1.0, 60.0, NULL);
        Rig_Run(&Exact, 1.0, 60.0, NULL);
        TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Vague.System, &Vague.Environment, &Vague.Clock, 168.0, 1.0,
            &First));
        TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Vague.System, &Vague.Environment, &Vague.Clock, 168.0, 1.0,
            &Second));
        TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Exact.System, &Exact.Environment, &Exact.Clock, 168.0, 1.0,
            &Truth));
        const WeatherForecastStep* A = (const WeatherForecastStep*)First._data;
        const WeatherForecastStep* B = (const WeatherForecastStep*)Second._data;
        const WeatherForecastStep* T = (const WeatherForecastStep*)Truth._data;
        TEST_CHECK(run, (First._count == 168U) && (Second._count == 168U) && (Truth._count == 168U));
        TEST_CHECK(run, memcmp(A, B, First._count * sizeof(WeatherForecastStep)) == 0); // asked twice, said twice
        size_t Blurred = 0U;
        bool AreBlurredOnesWrong = true;
        for (size_t Hour = 0U; Hour < First._count; Hour++)
        {
            if (A[Hour].IsBlurred)
            {
                Blurred++;
                AreBlurredOnesWrong = AreBlurredOnesWrong && (A[Hour].Preset != T[Hour].Preset);
            }
            else
            {
                TEST_CHECK(run, A[Hour].Preset == T[Hour].Preset);
            }
        }
        TEST_CHECK(run, Blurred > 0U);
        TEST_CHECK(run, AreBlurredOnesWrong);
        TEST_CHECK(run, !A[0].IsBlurred); // the preset of now is known
    }
    Memory_Free(First._data);
    Memory_Free(Second._data);
    Memory_Free(Truth._data);
    Rig_Deconstruct(run, &Vague);
    Rig_Deconstruct(run, &Exact);
}

/* Saves the original's weather (its table, as JSON) into the restored one, with the world's values and the clock, and
 * runs both `ticks` more ticks: whether they go on the same. */
static bool SaveAndGoOn(TestRun* run, WeatherRig* original, WeatherRig* restored, int ticks, double gameSeconds,
    double realSeconds)
{
    JSONObjectPool* Pool = NULL;
    JSONCompound* Saved = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return false;
    }
    const PropertyTable* Table = WeatherSystem_GetPropertyTable();
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Saved)))
    {
        TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, &original->System, Pool, Saved, NULL));
        TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Saved, &restored->System, NULL));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Saved, true));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&original->Environment, &restored->Environment));
    TEST_CHECK_SUCCESS(run, GameClock_SetDayAndTime(&restored->Clock, GameClock_GetDay(&original->Clock),
        GameClock_GetSecondsOfDay(&original->Clock)));
    bool AreEqual = true;
    for (int Tick = 0; Tick < ticks; Tick++)
    {
        Rig_Tick(original, gameSeconds, realSeconds);
        Rig_Tick(restored, gameSeconds, realSeconds);
        AreEqual = AreEqual && AreWeatherValuesEqual(&original->Environment, &restored->Environment)
            && (WeatherSystem_GetSimulation(&original->System)->Preset
                == WeatherSystem_GetSimulation(&restored->System)->Preset);
    }
    return AreEqual;
}

static void TheStateRoundTripsAndGoesOnTheSame(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"stormy\", \"seed\": 77, \"start\": \"clear\", \"presets\": ["
        " {\"name\": \"clear\", \"cloud_coverage\": 0.1, \"duration_hours\": [1, 3], \"gusts\": {\"strength\": 3},"
        "  \"next\": [{\"preset\": \"storm\"}, {\"preset\": \"clear\"}]},"
        " {\"name\": \"storm\", \"cloud_coverage\": 1, \"cloud_type\": 1, \"cloud_density\": 1, \"precipitation\": 1,"
        "  \"duration_hours\": [1, 2], \"transition_hours\": 0.5, \"gusts\": {\"strength\": 8},"
        "  \"lightning\": {\"per_minute\": 10}, \"next\": [{\"preset\": \"clear\"}]}]}",
        NULL
    };
    WeatherRig Original;
    WeatherRig Restored;
    JSONObjectPool* Pool = NULL;
    if (Rig_Construct(run, &Original, "stormy", Climates) && Rig_Construct(run, &Restored, "stormy", Climates)
        && TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        for (int Tick = 0; Tick < 3000; Tick++)
        {
            Rig_Tick(&Original, 6.0, 1.0 / 30.0);
        }
        // Save the weather and the world's values; load them into another system and world.
        const PropertyTable* Table = WeatherSystem_GetPropertyTable();
        JSONCompound* Saved = NULL;
        if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Saved)))
        {
            TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, &Original.System, Pool, Saved, NULL));
            TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Saved, &Restored.System, NULL));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Saved, true));
        }
        TEST_CHECK_SUCCESS(run, WorldEnvironment_CopyTo(&Original.Environment, &Restored.Environment));
        TEST_CHECK_SUCCESS(run, GameClock_SetDayAndTime(&Restored.Clock, GameClock_GetDay(&Original.Clock),
            GameClock_GetSecondsOfDay(&Original.Clock)));
        bool AreEqual = true;
        for (int Tick = 0; Tick < 6000; Tick++)
        {
            Rig_Tick(&Original, 6.0, 1.0 / 30.0);
            Rig_Tick(&Restored, 6.0, 1.0 / 30.0);
            AreEqual = AreEqual && AreWeatherValuesEqual(&Original.Environment, &Restored.Environment)
                && (WeatherSystem_GetSimulation(&Original.System)->Preset
                    == WeatherSystem_GetSimulation(&Restored.System)->Preset);
        }
        TEST_CHECK(run, AreEqual);
        TEST_CHECK(run, Properties_Equals(Table, &Original.System, &Restored.System));
        // The GHDF form too.
        GHDFObjectPool* BinaryPool = NULL;
        GHDFCompound* Binary = NULL;
        if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&BinaryPool))
            && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(BinaryPool, &Binary)))
        {
            TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(Table, &Original.System, BinaryPool, Binary, NULL));
            WeatherSystem FromBinary;
            if (TEST_CHECK_SUCCESS(run, WeatherSystem_Construct(&FromBinary, Original.Fixture.Manager, NULL)))
            {
                TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(Table, Binary, &FromBinary, NULL));
                TEST_CHECK(run, Properties_Equals(Table, &Original.System, &FromBinary));
                WeatherSystem_Deconstruct(&FromBinary);
            }
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(BinaryPool, Binary, true));
        }
        if (BinaryPool != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(BinaryPool));
        }
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Rig_Deconstruct(run, &Original);
    Rig_Deconstruct(run, &Restored);
}

static void AFastForwardPassesTheSamePresets(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "55"), NULL };
    WeatherRig Ticked;
    WeatherRig Skipped;
    if (Rig_Construct(run, &Ticked, "cycle", Climates) && Rig_Construct(run, &Skipped, "cycle", Climates))
    {
        Rig_Tick(&Ticked, 0.0, 0.0);
        Rig_Tick(&Skipped, 0.0, 0.0);
        Rig_Run(&Ticked, 30.0, 15.0, NULL);
        Rig_Tick(&Skipped, 30.0 * TEST_HOUR, 0.0); // sleeping: thirty hours at once
        TEST_CHECK(run, WeatherSystem_GetSimulation(&Ticked.System)->Preset
            == WeatherSystem_GetSimulation(&Skipped.System)->Preset);
        TEST_CHECK(run, fabs(WeatherSystem_GetSimulation(&Ticked.System)->PresetHoursLeft
            - WeatherSystem_GetSimulation(&Skipped.System)->PresetHoursLeft) < 1.0e-6);
        TEST_CHECK(run, fabsf(Ticked.Environment.CloudCoverage - Skipped.Environment.CloudCoverage) < 1.0e-3f);
    }
    Rig_Deconstruct(run, &Ticked);
    Rig_Deconstruct(run, &Skipped);
}

static void AMissingClimateLeavesTheWeatherAlone(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "1"), NULL };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "nowhere", Climates))
    {
        Rig.Environment.CloudCoverage = 0.4f;
        for (int Tick = 0; Tick < 100; Tick++)
        {
            Rig_Tick(&Rig, 600.0, 0.1);
        }
        TEST_CHECK(run, !WeatherSystem_IsStarted(&Rig.System));
        TEST_CHECK(run, (Rig.Environment.CloudCoverage == 0.4f) && (Rig.Environment.LightningFlash == 0.0f));
        TEST_CHECK(run, WeatherSystem_GetPreset(&Rig.System) == NULL);
        TEST_CHECK_ERROR(run, WeatherSystem_SetMode(&Rig.System, WeatherMode_Fixed), ErrorCode_InvalidState);
        // Naming a climate that exists starts it.
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetClimate(&Rig.System, &Rig.Environment, (const unsigned char*)u8"cycle"));
        Rig_Tick(&Rig, 60.0, 0.0);
        TEST_CHECK(run, WeatherSystem_IsStarted(&Rig.System));
    }
    Rig_Deconstruct(run, &Rig);
}

static void SwitchingClimatesTakesTheNewOnes(TestRun* run)
{
    static const char* const Climates[] =
    {
        CYCLE_CLIMATE("cycle", "4"),
        "{\"name\": \"desert\", \"mode\": \"fixed\", \"speed\": 3, \"start\": \"dry\", \"presets\": ["
        " {\"name\": \"dry\", \"cloud_coverage\": 0, \"temperature\": 40}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "cycle", Climates))
    {
        Rig_Run(&Rig, 4.0, 60.0, NULL);
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetClimate(&Rig.System, &Rig.Environment, (const unsigned char*)u8"desert"));
        Rig_Run(&Rig, 0.1, 60.0, NULL);
        const WeatherSimulation* Sim = WeatherSystem_GetSimulation(&Rig.System);
        const ClimatePreset* Preset = WeatherSystem_GetPreset(&Rig.System);
        TEST_CHECK(run, (Preset != NULL) && (strcmp((const char*)Preset->Name, "dry") == 0));
        TEST_CHECK(run, (Sim->Mode == WeatherMode_Fixed) && (Sim->Speed == 3.0f));
        TEST_CHECK(run, Sim->Targets[WeatherQuantity_Temperature] == 40.0f);
        TEST_CHECK(run, WeatherSystem_GetSeed(&Rig.System) == 4U); // the streams go on
    }
    Rig_Deconstruct(run, &Rig);
}

static void ASavedPresetTheClimateLostStartsOver(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "9"), NULL };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "cycle", Climates))
    {
        Rig_Run(&Rig, 1.0, 60.0, NULL);
        // A preset's name is a local name of its climate: no namespace.
        PropertyValue Namespaced = PropertyValue_FromLocalName((const unsigned char*)u8"tbp:blizzard");
        TEST_CHECK_ERROR(run, Properties_Set(WeatherSystem_GetPropertyTable(), &Rig.System,
            (const unsigned char*)u8"preset", &Namespaced), ErrorCode_ArgumentOutOfRange);
        PropertyValue Gone = PropertyValue_FromLocalName((const unsigned char*)u8"blizzard");
        TEST_CHECK_SUCCESS(run, Properties_Set(WeatherSystem_GetPropertyTable(), &Rig.System,
            (const unsigned char*)u8"preset", &Gone));
        TEST_CHECK(run, WeatherSystem_GetPreset(&Rig.System) == NULL); // to be found again
        Rig_Tick(&Rig, 60.0, 0.0);
        const ClimatePreset* Preset = WeatherSystem_GetPreset(&Rig.System);
        TEST_CHECK(run, (Preset != NULL) && (strcmp((const char*)Preset->Name, "clear") == 0));
    }
    Rig_Deconstruct(run, &Rig);
}


static void TheWindTurnsTheShortWay(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"winds\", \"seed\": 5, \"mode\": \"scripted\", \"presets\": ["
        " {\"name\": \"calm\"},"
        " {\"name\": \"north\", \"wind_direction\": 10},"
        " {\"name\": \"east\", \"wind_direction\": 90},"
        " {\"name\": \"veering\", \"wind_direction\": [350, 370]}]}",
        NULL
    };
    const float Radians = 3.14159265358979f / 180.0f;
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "winds", Climates))
    {
        Rig_Tick(&Rig, 0.0, 0.0);
        float Values[WeatherQuantity_Count] = { 0.0f };
        Values[WeatherQuantity_WindDirection] = 350.0f * Radians;
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetValuesNow(&Rig.System, &Rig.Environment,
            1U << WeatherQuantity_WindDirection, Values));
        // From 350 to 10 degrees over an hour: 20 degrees the short way, through north (0), never the long way round.
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"north", 1.0f));
        Rig_Run(&Rig, 0.5, 60.0, NULL);
        float Half = Rig.Environment.WindDirection / Radians;
        TEST_CHECK(run, (Half < 0.5f) || (Half > 359.5f));
        Rig_Run(&Rig, 0.5, 60.0, NULL);
        TEST_CHECK(run, fabsf(Rig.Environment.WindDirection / Radians - 10.0f) < 0.01f);
        // A two-hour move is half done after one.
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"east", 2.0f));
        Rig_Run(&Rig, 1.0, 60.0, NULL);
        TEST_CHECK(run, fabsf(Rig.Environment.WindDirection / Radians - 50.0f) < 0.01f);
        // Targets drawn past a full turn are kept within one.
        bool AreWrapped = true;
        for (int Visit = 0; Visit < 12; Visit++)
        {
            TEST_CHECK_SUCCESS(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"veering", 1.0f));
            float Target = WeatherSystem_GetSimulation(&Rig.System)->Targets[WeatherQuantity_WindDirection];
            AreWrapped = AreWrapped && (Target >= 0.0f) && (Target < 2.0f * 3.14159265358979f);
        }
        TEST_CHECK(run, AreWrapped);
    }
    Rig_Deconstruct(run, &Rig);
}

static void TheCloudsRainAndStormByTheirKind(TestRun* run)
{
    static const char* const Climates[] =
    {
        // Full but thin clouds: coverage 1 x density 0.3 is below the rain cloud range (0.6 to 0.9).
        "{\"name\": \"veil\", \"seed\": 2, \"mode\": \"fixed\", \"fixed_preset\": \"veil\", \"presets\": ["
        " {\"name\": \"veil\", \"cloud_coverage\": 1, \"cloud_density\": 0.3, \"precipitation\": 1,"
        "  \"transition_hours\": 0}]}",
        // Clouds of 0.65 (a sixth into the range): the rain heads for the range's smooth step there.
        "{\"name\": \"edge\", \"seed\": 2, \"mode\": \"fixed\", \"fixed_preset\": \"edge\", \"presets\": ["
        " {\"name\": \"edge\", \"cloud_coverage\": 1, \"cloud_density\": 0.65, \"precipitation\": 1,"
        "  \"transition_hours\": 0}]}",
        // Dense flat layers (type 0): no storm clouds, whatever the lightning's rate.
        "{\"name\": \"layers\", \"seed\": 2, \"mode\": \"fixed\", \"fixed_preset\": \"layers\", \"presets\": ["
        " {\"name\": \"layers\", \"cloud_coverage\": 1, \"cloud_type\": 0, \"cloud_density\": 1, \"transition_hours\": 0,"
        "  \"lightning\": {\"per_minute\": 600}}]}",
        NULL
    };
    WeatherRig Veil;
    WeatherRig Edge;
    WeatherRig Layers;
    if (Rig_Construct(run, &Veil, "veil", Climates) && Rig_Construct(run, &Edge, "edge", Climates)
        && Rig_Construct(run, &Layers, "layers", Climates))
    {
        Rig_Run(&Veil, 12.0, 60.0, NULL);
        TEST_CHECK(run, Veil.Environment.Precipitation == 0.0f);
        Rig_Run(&Edge, 12.0, 60.0, NULL);
        float Step = 1.0f / 6.0f;
        TEST_CHECK(run, fabsf(Edge.Environment.Precipitation - Step * Step * (3.0f - 2.0f * Step)) < 1.0e-4f);
        Rig_Tick(&Layers, 1.0, 0.0);
        size_t Strikes = 0U;
        for (int Tick = 0; Tick < 600; Tick++) // a real minute at 600 a minute
        {
            Rig_Tick(&Layers, 0.0, 0.1);
            size_t Count = 0U;
            (void)WeatherSystem_GetStrikes(&Layers.System, &Count);
            Strikes += Count;
            WeatherSystem_ClearStrikes(&Layers.System);
        }
        TEST_CHECK(run, Strikes == 0U);
    }
    Rig_Deconstruct(run, &Veil);
    Rig_Deconstruct(run, &Edge);
    Rig_Deconstruct(run, &Layers);
}

/* One update of five game hours at a weather speed of 0.01: a single step of 0.05 weather hours (the largest), from
 * `hour` (15:30 gives a step across the sunset) with the given clouds and rain held, from a wetness of `wetness`. */
static float DryOneStep(TestRun* run, double hour, float cover, float rain, float wetness, float* outDaylight)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"drying\", \"seed\": 3, \"mode\": \"scripted\", \"speed\": 0.01, \"drying_per_hour\": 2,"
        " \"wetting_per_hour\": 0.01, \"presets\": [{\"name\": \"still\"}]}",
        NULL
    };
    float Wetness = -1.0f;
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "drying", Climates))
    {
        int Hour = (int)hour;
        int Minute = (int)lround((hour - (double)Hour) * 60.0);
        TEST_CHECK_SUCCESS(run, GameClock_SetDateTime(&Rig.Clock, 2026, 10, 18, Hour, Minute, 0));
        Rig_Tick(&Rig, 0.0, 0.0);
        float Values[WeatherQuantity_Count] = { 0.0f };
        Values[WeatherQuantity_CloudCoverage] = cover;
        Values[WeatherQuantity_CloudDensity] = 1.0f;
        Values[WeatherQuantity_Precipitation] = rain;
        uint32_t Mask = (1U << WeatherQuantity_CloudCoverage) | (1U << WeatherQuantity_CloudDensity)
            | (1U << WeatherQuantity_Precipitation);
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetValuesNow(&Rig.System, &Rig.Environment, Mask, Values));
        Rig.Environment.Wetness = wetness;
        // The sun at the step's middle: 2.5 game hours in.
        double Middle = hour + 2.5;
        *outDaylight = WorldEnvironment_GetDaylightFactorAt(&Rig.Environment, Rig.Environment.Date,
            (float)(Middle / 24.0));
        Rig_Tick(&Rig, 5.0 * TEST_HOUR, 0.0);
        Wetness = Rig.Environment.Wetness;
    }
    Rig_Deconstruct(run, &Rig);
    return Wetness;
}

static void DryingFollowsTheSunTheCloudsAndTheRain(TestRun* run)
{
    // Clear sky across the sunset: drying 2 an hour x (1 + 2 x the sun at the step's middle, 18:00) over 0.05 hours.
    float Daylight = 0.0f;
    float Wetness = DryOneStep(run, 15.5, 0.0f, 0.0f, 1.0f, &Daylight);
    WorldEnvironment Sky;
    WorldEnvironment_SetDefaults(&Sky);
    float AtStart = WorldEnvironment_GetDaylightFactorAt(&Sky, Sky.Date, 15.5f / 24.0f);
    float AtEnd = WorldEnvironment_GetDaylightFactorAt(&Sky, Sky.Date, 20.5f / 24.0f);
    WorldEnvironment_Deconstruct(&Sky);
    TEST_CHECK(run, (fabsf(Daylight - AtStart) > 0.1f) && (fabsf(Daylight - AtEnd) > 0.1f)); // the sun moves in the step
    TEST_CHECK(run, fabsf(Wetness - (1.0f - 2.0f * (1.0f + 2.0f * Daylight) * 0.05f)) < 1.0e-5f);
    // A full deck at noon hides the sun: the base rate alone.
    Wetness = DryOneStep(run, 10.0, 1.0f, 0.0f, 1.0f, &Daylight);
    TEST_CHECK(run, (Daylight > 0.9f) && (fabsf(Wetness - (1.0f - 2.0f * 0.05f)) < 1.0e-5f));
    // Steady rain holds the drying: only the (slow) wetting.
    Wetness = DryOneStep(run, 10.0, 1.0f, 0.2f, 0.5f, &Daylight);
    TEST_CHECK(run, fabsf(Wetness - (0.5f + 0.01f * 0.2f * 0.05f)) < 1.0e-5f);
}


/* The hour of the day of a logged time (game seconds since day 0). */
static double HourOf(double seconds)
{
    double Rest = fmod(seconds, 86400.0);
    return ((Rest < 0.0) ? Rest + 86400.0 : Rest) / TEST_HOUR;
}

static void StartWindowsWrapAndPresetsWait(TestRun* run)
{
    static const char* const Climates[] =
    {
        // "wait" may only be followed by "dawn" (05:00 to 06:00), which may only be followed by "late" (22:00 to 04:00):
        // until the hour allows the next one, the active preset goes on.
        "{\"name\": \"windows\", \"seed\": 4, \"start\": \"wait\", \"presets\": ["
        " {\"name\": \"wait\", \"duration_hours\": [0.5, 1], \"next\": [{\"preset\": \"dawn\"}]},"
        " {\"name\": \"dawn\", \"start_hours\": [5, 6], \"duration_hours\": [0.5, 1], \"next\": [{\"preset\": \"late\"}]},"
        " {\"name\": \"late\", \"start_hours\": [22, 4], \"duration_hours\": [0.5, 1], \"next\": [{\"preset\": \"wait\"}]}]}",
        NULL
    };
    WeatherRig Rig;
    static PresetLog Log;
    memset(&Log, 0, sizeof(Log));
    if (Rig_Construct(run, &Rig, "windows", Climates))
    {
        Rig_Run(&Rig, 24.0 * 5.0, 60.0, &Log);
        uint16_t Dawn = FindPreset(&Rig, "dawn");
        uint16_t Late = FindPreset(&Rig, "late");
        size_t DawnStarts = 0U;
        size_t LateStarts = 0U;
        bool AreHoursRight = true;
        const double Tick = 60.0 / TEST_HOUR; // a start is logged at the end of its tick
        for (size_t Index = 1U; Index < Log.Count; Index++)
        {
            double Hour = HourOf(Log.Times[Index]);
            if (Log.Presets[Index] == Dawn)
            {
                DawnStarts++;
                AreHoursRight = AreHoursRight && (Hour >= 5.0) && (Hour <= 6.0 + Tick);
            }
            else if (Log.Presets[Index] == Late)
            {
                LateStarts++;
                AreHoursRight = AreHoursRight && ((Hour >= 22.0) || (Hour <= 4.0 + Tick));
            }
        }
        TEST_CHECK(run, AreHoursRight);
        TEST_CHECK(run, (DawnStarts >= 4U) && (LateStarts >= 4U)); // once a day, through the window past midnight
    }
    Rig_Deconstruct(run, &Rig);
}

static void ARandomStartKeepsToTheHour(TestRun* run)
{
    // Two climates alike under two names: switching between them starts each on a random preset (drawn on the weather's
    // stream), at noon: "a" and "b" equally, never "night".
    static const char* const Climates[] =
    {
        "{\"name\": \"random_one\", \"seed\": 6, \"start\": \"random\", \"presets\": [{\"name\": \"a\"}, {\"name\": \"b\"},"
        " {\"name\": \"night\", \"start_hours\": [22, 4]}]}",
        "{\"name\": \"random_two\", \"seed\": 6, \"start\": \"random\", \"presets\": [{\"name\": \"a\"}, {\"name\": \"b\"},"
        " {\"name\": \"night\", \"start_hours\": [22, 4]}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "random_one", Climates))
    {
        Rig_Tick(&Rig, 0.0, 0.0);
        size_t Counts[3] = { 0U, 0U, 0U };
        for (int Switch = 0; Switch < 600; Switch++)
        {
            const unsigned char* Name = (const unsigned char*)(((Switch % 2) == 0) ? u8"random_two" : u8"random_one");
            TEST_CHECK_SUCCESS(run, WeatherSystem_SetClimate(&Rig.System, &Rig.Environment, Name));
            Rig_Tick(&Rig, 0.0, 0.0);
            uint16_t Preset = WeatherSystem_GetSimulation(&Rig.System)->Preset;
            if (Preset < 3U)
            {
                Counts[Preset]++;
            }
        }
        TEST_CHECK(run, Counts[FindPreset(&Rig, "night")] == 0U);
        size_t A = Counts[FindPreset(&Rig, "a")];
        TEST_CHECK(run, (A > 250U) && (A < 350U) && ((A + Counts[FindPreset(&Rig, "b")]) == 600U));
    }
    Rig_Deconstruct(run, &Rig);
}


static void AnEarlySaveGoesOnTheSame(TestRun* run)
{
    // Saved a game minute after the start, before any preset changed: each mode goes on as if it never stopped (the
    // first preset's drawn targets kept, the scripted weather still).
    static const char* const Climates[] =
    {
        "{\"name\": \"early_simulated\", \"seed\": 8, \"presets\": ["
        " {\"name\": \"a\", \"temperature\": [5, 25], \"cloud_coverage\": [0.2, 0.8], \"duration_hours\": [2, 4],"
        "  \"next\": [{\"preset\": \"b\"}]},"
        " {\"name\": \"b\", \"temperature\": [-5, 0], \"duration_hours\": [1, 2], \"next\": [{\"preset\": \"a\"}]}]}",
        "{\"name\": \"early_fixed\", \"seed\": 8, \"mode\": \"fixed\", \"fixed_preset\": \"a\", \"presets\": ["
        " {\"name\": \"a\", \"temperature\": [5, 25], \"cloud_coverage\": [0.2, 0.8]}]}",
        "{\"name\": \"early_scripted\", \"seed\": 8, \"mode\": \"scripted\", \"presets\": ["
        " {\"name\": \"a\", \"cloud_coverage\": 0.9, \"temperature\": 30}]}",
        NULL
    };
    const char* const Names[] = { "early_simulated", "early_fixed", "early_scripted" };
    for (size_t Index = 0U; Index < (sizeof(Names) / sizeof(Names[0])); Index++)
    {
        WeatherRig Original;
        WeatherRig Restored;
        if (Rig_Construct(run, &Original, Names[Index], Climates) && Rig_Construct(run, &Restored, Names[Index], Climates))
        {
            Rig_Run(&Original, 1.0 / 60.0, 6.0, NULL);
            bool IsSame = SaveAndGoOn(run, &Original, &Restored, 6000, 6.0, 1.0 / 30.0);
            if (!TEST_CHECK(run, IsSame))
            {
                printf("    in \"%s\"\n", Names[Index]);
            }
        }
        Rig_Deconstruct(run, &Original);
        Rig_Deconstruct(run, &Restored);
    }
}

static void AStrikeSavedInItsFlickerGoesOnTheSame(TestRun* run)
{
    // Saved while a strike's return strokes are still to come: they draw nothing later, so the gusts (on the same
    // random stream) and the later strikes go on the same.
    static const char* const Climates[] =
    {
        "{\"name\": \"flicker\", \"seed\": 9, \"mode\": \"fixed\", \"fixed_preset\": \"storm\", \"presets\": ["
        " {\"name\": \"storm\", \"cloud_coverage\": 1, \"cloud_type\": 1, \"cloud_density\": 1, \"transition_hours\": 0,"
        "  \"gusts\": {\"strength\": 8, \"per_minute\": 20}, \"lightning\": {\"per_minute\": 20}}]}",
        NULL
    };
    WeatherRig Original;
    WeatherRig Restored;
    if (Rig_Construct(run, &Original, "flicker", Climates) && Rig_Construct(run, &Restored, "flicker", Climates))
    {
        bool IsSame = true;
        for (int Strike = 0; Strike < 8; Strike++)
        {
            Rig_Tick(&Original, 1.0, 0.5);
            WeatherSystem_Strike(&Original.System, &Original.Clock);
            Rig_Tick(&Original, 0.0, 0.01 * (double)(Strike + 1));
            IsSame = IsSame && SaveAndGoOn(run, &Original, &Restored, 300, 0.0, 1.0 / 30.0);
        }
        TEST_CHECK(run, IsSame);
    }
    Rig_Deconstruct(run, &Original);
    Rig_Deconstruct(run, &Restored);
}

static void ALongForecastStepPassesEveryPreset(TestRun* run)
{
    // Presets of 3 to 6 minutes: one forecast step of 300 hours passes thousands of them, as 300 steps of an hour do.
    static const char* const Climates[] =
    {
        "{\"name\": \"quick\", \"seed\": 10, \"forecast_accuracy\": 1, \"presets\": ["
        " {\"name\": \"a\", \"temperature\": [0, 10], \"duration_hours\": [0.05, 0.1], \"next\": [{\"preset\": \"b\"},"
        "  {\"preset\": \"c\"}]},"
        " {\"name\": \"b\", \"temperature\": [10, 20], \"duration_hours\": [0.05, 0.1], \"next\": [{\"preset\": \"a\"},"
        "  {\"preset\": \"c\"}]},"
        " {\"name\": \"c\", \"temperature\": [20, 30], \"duration_hours\": [0.05, 0.1], \"next\": [{\"preset\": \"a\"},"
        "  {\"preset\": \"b\"}]}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "quick", Climates))
    {
        Rig_Tick(&Rig, 0.0, 0.0);
        GenericBuffer Long;
        GenericBuffer Short;
        GenericBuffer_AllocateVariable(&Long, 4U, sizeof(WeatherForecastStep));
        GenericBuffer_AllocateVariable(&Short, 400U, sizeof(WeatherForecastStep));
        if (TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Rig.System, &Rig.Environment, &Rig.Clock, 300.0, 300.0, &Long))
            && TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Rig.System, &Rig.Environment, &Rig.Clock, 300.0, 1.0,
                &Short)))
        {
            const WeatherForecastStep* Last = (const WeatherForecastStep*)Long._data;
            const WeatherForecastStep* Hourly = (const WeatherForecastStep*)Short._data + (Short._count - 1U);
            TEST_CHECK(run, (Long._count == 1U) && (Short._count == 300U) && (Last->Preset == Hourly->Preset)
                && (fabsf(Last->Values.Quantities[WeatherQuantity_Temperature]
                    - Hourly->Values.Quantities[WeatherQuantity_Temperature]) < 1.0e-3f));
            // And the ticked weather gets there too.
            Rig_Run(&Rig, 300.0, 600.0, NULL);
            TEST_CHECK(run, (WeatherSystem_GetSimulation(&Rig.System)->Preset == Hourly->Preset)
                && (fabsf(Rig.Environment.Temperature - Hourly->Values.Quantities[WeatherQuantity_Temperature]) < 1.0e-3f));
        }
        Memory_Free(Long._data);
        Memory_Free(Short._data);
    }
    Rig_Deconstruct(run, &Rig);
}

static void TheFlashGoesOutWhateverStopsTheWeather(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"stormy\", \"seed\": 11, \"presets\": [{\"name\": \"storm\"}]}",
        "{\"name\": \"quiet\", \"seed\": 11, \"mode\": \"off\", \"presets\": [{\"name\": \"none\"}]}",
        NULL
    };
    const char* const Targets[] = { "quiet", "no_such_climate" };
    for (size_t Index = 0U; Index < (sizeof(Targets) / sizeof(Targets[0])); Index++)
    {
        WeatherRig Rig;
        if (Rig_Construct(run, &Rig, "stormy", Climates))
        {
            Rig_Tick(&Rig, 0.0, 0.0);
            WeatherSystem_Strike(&Rig.System, &Rig.Clock);
            Rig_Tick(&Rig, 0.0, 0.01);
            TEST_CHECK(run, Rig.Environment.LightningFlash > 0.5f);
            // Another climate in the off mode, or one that fails to load: the flash goes out at the next update.
            TEST_CHECK_SUCCESS(run, WeatherSystem_SetClimate(&Rig.System, &Rig.Environment,
                (const unsigned char*)Targets[Index]));
            Rig_Tick(&Rig, 1.0, 0.01);
            bool IsOut = Rig.Environment.LightningFlash == 0.0f;
            Rig_Tick(&Rig, 1.0, 0.01);
            if (!TEST_CHECK(run, IsOut && (Rig.Environment.LightningFlash == 0.0f)))
            {
                printf("    after switching to \"%s\"\n", Targets[Index]);
            }
        }
        Rig_Deconstruct(run, &Rig);
    }
}

static void FixedValuesHoldTheRest(TestRun* run)
{
    // The fixed mode's values: what they leave out stays where the world has it (not the start preset's), and an absurd
    // angle settles too.
    static const char* const Climates[] =
    {
        "{\"name\": \"held\", \"seed\": 12, \"mode\": \"fixed\", \"start\": \"warm\","
        " \"fixed_values\": {\"cloud_coverage\": 0.5, \"wind_direction\": 1e20}, \"presets\": ["
        " {\"name\": \"warm\", \"temperature\": 30, \"wind_speed\": 9}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "held", Climates))
    {
        float Temperature = Rig.Environment.Temperature;
        Rig_Run(&Rig, 48.0, 60.0, NULL);
        float Direction = Rig.Environment.WindDirection;
        Rig_Run(&Rig, 1.0, 60.0, NULL);
        TEST_CHECK(run, (Rig.Environment.CloudCoverage == 0.5f) && (Rig.Environment.Temperature == Temperature)
            && (Rig.Environment.WindSpeed == 0.0f) && (Rig.Environment.WindDirection == Direction));
        // Back to the simulation: the held quantities move again.
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetMode(&Rig.System, WeatherMode_Simulated));
        Rig_Run(&Rig, 1.0, 60.0, NULL);
        TEST_CHECK(run, fabsf(Rig.Environment.Temperature - (Temperature + 2.0f)) < 0.01f); // at its rate again at once
        Rig_Run(&Rig, 24.0, 60.0, NULL);
        TEST_CHECK(run, (Rig.Environment.Temperature == 30.0f) && (Rig.Environment.WindSpeed == 9.0f));
    }
    Rig_Deconstruct(run, &Rig);
    // A fixed angle past a full turn is held within one: 370 degrees is 10.
    static const char* const Turned[] =
    {
        "{\"name\": \"held_turn\", \"seed\": 12, \"mode\": \"fixed\", \"fixed_values\": {\"wind_direction\": 370},"
        " \"presets\": [{\"name\": \"any\"}]}",
        NULL
    };
    if (Rig_Construct(run, &Rig, "held_turn", Turned))
    {
        Rig_Tick(&Rig, 60.0, 0.0);
        float Target = WeatherSystem_GetSimulation(&Rig.System)->Targets[WeatherQuantity_WindDirection];
        TEST_CHECK(run, fabsf(Target - 10.0f * 3.14159265f / 180.0f) < 1.0e-4f);
    }
    Rig_Deconstruct(run, &Rig);
}

static void TheApiWaitsForASaveAndForecastsAYearAtMost(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "13"), NULL };
    WeatherRig Rig;
    JSONObjectPool* Pool = NULL;
    if (Rig_Construct(run, &Rig, "cycle", Climates) && TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        Rig_Tick(&Rig, 60.0, 0.0);
        // A forecast covers at most a year of the weather's own hours: at 100 times the speed, 87 game hours.
        GenericBuffer Steps;
        GenericBuffer_AllocateVariable(&Steps, 200U, sizeof(WeatherForecastStep));
        TEST_CHECK_ERROR(run, WeatherSystem_SetSpeed(&Rig.System, 0.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetSpeed(&Rig.System, 100.0f));
        Error Result = WeatherSystem_Forecast(&Rig.System, &Rig.Environment, &Rig.Clock, 100.0, 1.0, &Steps);
        TEST_CHECK(run, (Result.Code == ErrorCode_ArgumentOutOfRange) && (Steps._count == 0U));
        Error_Deconstruct(&Result);
        TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Rig.System, &Rig.Environment, &Rig.Clock, 87.0, 1.0, &Steps));
        Memory_Free(Steps._data);
        // After a read from a save, the mode waits for the next update (which finds the saved preset).
        const PropertyTable* Table = WeatherSystem_GetPropertyTable();
        JSONCompound* Saved = NULL;
        if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Saved)))
        {
            TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, &Rig.System, Pool, Saved, NULL));
            TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Saved, &Rig.System, NULL));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Saved, true));
        }
        Result = WeatherSystem_SetMode(&Rig.System, WeatherMode_Fixed);
        TEST_CHECK(run, Result.Code == ErrorCode_InvalidState);
        Error_Deconstruct(&Result);
        Rig_Tick(&Rig, 60.0, 0.0);
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetMode(&Rig.System, WeatherMode_Fixed));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Rig_Deconstruct(run, &Rig);
}


static void PresetRatesAndTransitionsMoveWhatTheySet(TestRun* run)
{
    // "hot" moves the temperature at its own rate (10 an hour, not the climate's 2); a move into "cloudy_now" over two
    // hours times only what that preset sets, while the temperature goes on at its rate.
    static const char* const Climates[] =
    {
        "{\"name\": \"rates\", \"seed\": 16, \"start\": \"hot\", \"presets\": ["
        " {\"name\": \"hot\", \"temperature\": 35, \"rates_per_hour\": {\"temperature\": 10}, \"duration_hours\": 10,"
        "  \"next\": [{\"preset\": \"hot\"}]},"
        " {\"name\": \"cloudy_now\", \"cloud_coverage\": 0.8, \"duration_hours\": 10, \"next\": [{\"preset\": \"hot\"}]}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "rates", Climates))
    {
        float Start = Rig.Environment.Temperature;
        Rig_Run(&Rig, 1.0, 60.0, NULL);
        TEST_CHECK(run, fabsf(Rig.Environment.Temperature - (Start + 10.0f)) < 0.01f);
        float Before = Rig.Environment.Temperature;
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetPreset(&Rig.System, (const unsigned char*)u8"cloudy_now", 2.0f));
        Rig_Run(&Rig, 1.0, 60.0, NULL);
        TEST_CHECK(run, fabsf(Rig.Environment.CloudCoverage - 0.4f) < 0.01f); // half way through its two hours
        TEST_CHECK(run, fabsf(Rig.Environment.Temperature - (Before + 2.0f)) < 0.01f); // the climate's rate
    }
    Rig_Deconstruct(run, &Rig);
}

static void AScriptedClimateHoldsTheWeatherItFinds(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"moving\", \"seed\": 17, \"presets\": [{\"name\": \"warm\", \"temperature\": 30, \"duration_hours\": 10,"
        " \"next\": [{\"preset\": \"warm\"}]}]}",
        "{\"name\": \"still\", \"seed\": 17, \"mode\": \"scripted\", \"presets\": [{\"name\": \"cold\", \"temperature\": -10}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "moving", Climates))
    {
        Rig_Run(&Rig, 1.0, 60.0, NULL);
        float Midway = Rig.Environment.Temperature;
        TEST_CHECK(run, (Midway > 16.0f) && (Midway < 30.0f));
        // The scripted climate takes over from the weather as it is: nothing moves on toward the old targets or its own.
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetClimate(&Rig.System, &Rig.Environment, (const unsigned char*)u8"still"));
        Rig_Run(&Rig, 2.0, 60.0, NULL);
        TEST_CHECK(run, fabsf(Rig.Environment.Temperature - Midway) < 1.0e-4f);
    }
    Rig_Deconstruct(run, &Rig);
}

static void StrikesFlickerSpreadAndQueue(TestRun* run)
{
    static const char* const Climates[] =
    {
        "{\"name\": \"sparks\", \"seed\": 18, \"mode\": \"fixed\", \"fixed_preset\": \"storm\", \"presets\": ["
        " {\"name\": \"storm\", \"lightning\": {\"per_minute\": 0, \"min_distance\": 500, \"max_distance\": 4000}}]}",
        NULL
    };
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "sparks", Climates))
    {
        Rig_Tick(&Rig, 60.0, 0.0);
        // Return strokes light the flash again after a strike.
        int Relights = 0;
        for (int Strike = 0; Strike < 30; Strike++)
        {
            WeatherSystem_Strike(&Rig.System, &Rig.Clock);
            float Previous = 1.0f;
            for (int Tick = 0; Tick < 25; Tick++)
            {
                Rig_Tick(&Rig, 0.0, 0.02);
                Relights += (Rig.Environment.LightningFlash > Previous + 0.01f) ? 1 : 0;
                Previous = Rig.Environment.LightningFlash;
            }
        }
        TEST_CHECK(run, Relights >= 10);
        // Distances spread evenly over the area between the strike distances: their mean is 2704 m, not 2250.
        WeatherSystem_ClearStrikes(&Rig.System);
        double Sum = 0.0;
        for (int Strike = 0; Strike < 1000; Strike++)
        {
            WeatherSystem_Strike(&Rig.System, &Rig.Clock);
            Sum += (double)Rig.System._strikeDistance;
            WeatherSystem_ClearStrikes(&Rig.System);
        }
        TEST_CHECK(run, ((Sum / 1000.0) > 2600.0) && ((Sum / 1000.0) < 2810.0));
        // A full queue drops its oldest: the newest strike is always last.
        for (int Strike = 0; Strike < 70; Strike++)
        {
            WeatherSystem_Strike(&Rig.System, &Rig.Clock);
        }
        size_t Count = 0U;
        const WeatherStrike* Strikes = WeatherSystem_GetStrikes(&Rig.System, &Count);
        TEST_CHECK(run, (Count == WEATHER_SYSTEM_MAX_QUEUED_STRIKES) && (Strikes != NULL)
            && (Strikes[Count - 1U].Distance == Rig.System._strikeDistance));
        // The off mode strikes nothing.
        WeatherSystem_ClearStrikes(&Rig.System);
        TEST_CHECK_SUCCESS(run, WeatherSystem_SetMode(&Rig.System, WeatherMode_Off));
        Rig_Tick(&Rig, 1.0, 1.0);
        WeatherSystem_Strike(&Rig.System, &Rig.Clock);
        Rig_Tick(&Rig, 1.0, 0.01);
        (void)WeatherSystem_GetStrikes(&Rig.System, &Count);
        TEST_CHECK(run, (Count == 0U) && (Rig.Environment.LightningFlash == 0.0f));
    }
    Rig_Deconstruct(run, &Rig);
}

/* How many of a forecast's steps from `first` on report a blurred visit, and how many steps there are from there. */
static size_t CountBlurred(const GenericBuffer* steps, size_t first, size_t* outTotal)
{
    size_t Blurred = 0U;
    const WeatherForecastStep* Steps = (const WeatherForecastStep*)steps->_data;
    for (size_t Index = first; Index < steps->_count; Index++)
    {
        Blurred += Steps[Index].IsBlurred ? 1U : 0U;
    }
    *outTotal = (steps->_count > first) ? (steps->_count - first) : 0U;
    return Blurred;
}

static void TheForecastBlursByLeadAndInaccuracy(TestRun* run)
{
    // Two presets that only follow each other: a blurred visit is always reported as the other one.
    static const char* const Climates[] =
    {
        "{\"name\": \"near_blur\", \"seed\": 14, \"forecast_accuracy\": 0, \"start\": \"a\", \"presets\": ["
        " {\"name\": \"a\", \"temperature\": 5, \"duration_hours\": 0.1, \"next\": [{\"preset\": \"b\"}]},"
        " {\"name\": \"b\", \"temperature\": 25, \"duration_hours\": 0.1, \"next\": [{\"preset\": \"a\"}]}]}",
        "{\"name\": \"far_blur\", \"seed\": 15, \"forecast_accuracy\": 0.9, \"start\": \"a\", \"presets\": ["
        " {\"name\": \"a\", \"temperature\": 5, \"duration_hours\": 1, \"next\": [{\"preset\": \"b\"}]},"
        " {\"name\": \"b\", \"temperature\": 25, \"duration_hours\": 1, \"next\": [{\"preset\": \"a\"}]}]}",
        NULL
    };
    WeatherRig Near;
    WeatherRig Far;
    GenericBuffer Steps;
    GenericBuffer_AllocateVariable(&Steps, 2100U, sizeof(WeatherForecastStep));
    if (Rig_Construct(run, &Near, "near_blur", Climates) && Rig_Construct(run, &Far, "far_blur", Climates))
    {
        Rig_Tick(&Near, 0.0, 0.0);
        Rig_Tick(&Far, 0.0, 0.0);
        // Even at accuracy 0, the next few hours are blurred little (a share of 1 - e^(-lead / 24 hours)).
        size_t Total = 0U;
        if (TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Near.System, &Near.Environment, &Near.Clock, 3.0, 0.1,
            &Steps)))
        {
            TEST_CHECK(run, (CountBlurred(&Steps, 0U, &Total) <= 6U) && (Total == 30U));
        }
        // Far ahead the share nears 1 - accuracy: a tenth.
        GenericBuffer_Clear(&Steps);
        if (TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Far.System, &Far.Environment, &Far.Clock, 2000.0, 1.0,
            &Steps)))
        {
            double Share = (double)CountBlurred(&Steps, 500U, &Total) / (double)Total;
            TEST_CHECK(run, (Share > 0.04) && (Share < 0.2));
        }
        // A last step shorter than the rest ends where the forecast does.
        GenericBuffer_Clear(&Steps);
        double Now = (double)GameClock_GetDay(&Near.Clock) * 86400.0 + GameClock_GetSecondsOfDay(&Near.Clock);
        if (TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Near.System, &Near.Environment, &Near.Clock, 0.25, 0.1,
            &Steps)))
        {
            const WeatherForecastStep* Last = (const WeatherForecastStep*)Steps._data + (Steps._count - 1U);
            double End = (double)Last->Day * 86400.0 + Last->SecondsOfDay;
            TEST_CHECK(run, (Steps._count == 3U) && (fabs(End - (Now + 900.0)) < 1.0e-3));
        }
        // Locked, the forecast holds the weather as it is.
        WeatherSystem_SetLocked(&Near.System, true);
        GenericBuffer_Clear(&Steps);
        if (TEST_CHECK_SUCCESS(run, WeatherSystem_Forecast(&Near.System, &Near.Environment, &Near.Clock, 1.0, 0.1,
            &Steps)))
        {
            bool AreHeld = Steps._count == 10U;
            const WeatherForecastStep* Held = (const WeatherForecastStep*)Steps._data;
            for (size_t Index = 0U; AreHeld && (Index < Steps._count); Index++)
            {
                AreHeld = (Held[Index].Values.Quantities[WeatherQuantity_Temperature] == Near.Environment.Temperature)
                    && (Held[Index].Preset == WeatherSystem_GetSimulation(&Near.System)->Preset);
            }
            TEST_CHECK(run, AreHeld);
        }
    }
    Memory_Free(Steps._data);
    Rig_Deconstruct(run, &Near);
    Rig_Deconstruct(run, &Far);
}

/* How often a text occurs in the latest log under a directory. */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Found = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 512U, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 4096U, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* At = strstr((const char*)Text._data, needle); At != NULL; At = strstr(At + 1, needle))
        {
            Found++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Found;
}

static void AFailingClimateIsLoggedOnce(TestRun* run)
{
    static const char* const Climates[] = { CYCLE_CLIMATE("cycle", "19"), NULL };
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, 512U, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "weather_failing_climate", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    WeatherRig Rig;
    if (Rig_Construct(run, &Rig, "nowhere", Climates))
    {
        WeatherSystem_Deconstruct(&Rig.System);
        TEST_CHECK_SUCCESS(run, WeatherSystem_Construct(&Rig.System, Rig.Fixture.Manager, &Log));
        for (int Tick = 0; Tick < 20; Tick++)
        {
            Rig_Tick(&Rig, 60.0, 0.1);
        }
    }
    Rig_Deconstruct(run, &Rig);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "the climate \"tbp:nowhere\" could not be loaded") == 1U);
    Memory_Free(Directory._data);
}


// Public functions.
void WeatherSystemTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_same_seed_gives_the_same_weather", TheSameSeedGivesTheSameWeather },
        { "values_stay_in_range_and_within_their_rates", ValuesStayInRangeAndWithinTheirRates },
        { "rain_needs_dense_clouds", RainNeedsDenseClouds },
        { "wetness_rises_in_rain_and_dries_afterwards", WetnessRisesInRainAndDriesAfterwards },
        { "presets_follow_their_weights_and_months", PresetsFollowTheirWeightsAndMonths },
        { "start_hours_hold", StartHoursHold },
        { "fixed_holds_with_wetness_following", FixedHoldsWithWetnessFollowing },
        { "scripted_changes_only_through_the_api", ScriptedChangesOnlyThroughTheAPI },
        { "off_leaves_the_environment_alone", OffLeavesTheEnvironmentAlone },
        { "locking_holds_and_resumes", LockingHoldsAndResumes },
        { "lightning_strikes_in_storms_at_its_rate", LightningStrikesInStormsAtItsRate },
        { "gusts_rise_and_fall_smoothly", GustsRiseAndFallSmoothly },
        { "the_forecast_is_what_then_happens", TheForecastIsWhatThenHappens },
        { "a_blurred_forecast_is_stable", ABlurredForecastIsStable },
        { "the_state_round_trips_and_goes_on_the_same", TheStateRoundTripsAndGoesOnTheSame },
        { "a_fast_forward_passes_the_same_presets", AFastForwardPassesTheSamePresets },
        { "a_missing_climate_leaves_the_weather_alone", AMissingClimateLeavesTheWeatherAlone },
        { "switching_climates_takes_the_new_ones", SwitchingClimatesTakesTheNewOnes },
        { "a_saved_preset_the_climate_lost_starts_over", ASavedPresetTheClimateLostStartsOver },
        { "the_wind_turns_the_short_way", TheWindTurnsTheShortWay },
        { "the_clouds_rain_and_storm_by_their_kind", TheCloudsRainAndStormByTheirKind },
        { "drying_follows_the_sun_the_clouds_and_the_rain", DryingFollowsTheSunTheCloudsAndTheRain },
        { "start_windows_wrap_and_presets_wait", StartWindowsWrapAndPresetsWait },
        { "a_random_start_keeps_to_the_hour", ARandomStartKeepsToTheHour },
        { "an_early_save_goes_on_the_same", AnEarlySaveGoesOnTheSame },
        { "a_strike_saved_in_its_flicker_goes_on_the_same", AStrikeSavedInItsFlickerGoesOnTheSame },
        { "a_long_forecast_step_passes_every_preset", ALongForecastStepPassesEveryPreset },
        { "the_flash_goes_out_whatever_stops_the_weather", TheFlashGoesOutWhateverStopsTheWeather },
        { "fixed_values_hold_the_rest", FixedValuesHoldTheRest },
        { "the_api_waits_for_a_save_and_forecasts_a_year_at_most", TheApiWaitsForASaveAndForecastsAYearAtMost },
        { "preset_rates_and_transitions_move_what_they_set", PresetRatesAndTransitionsMoveWhatTheySet },
        { "a_scripted_climate_holds_the_weather_it_finds", AScriptedClimateHoldsTheWeatherItFinds },
        { "strikes_flicker_spread_and_queue", StrikesFlickerSpreadAndQueue },
        { "the_forecast_blurs_by_lead_and_inaccuracy", TheForecastBlursByLeadAndInaccuracy },
        { "a_failing_climate_is_logged_once", AFailingClimateIsLoggedOnce },
    };
    TestRun_RunSuite(run, "WeatherSystem", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
