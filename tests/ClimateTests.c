#include "TestSuites.h"
#include "TestAssets.h"
#include "Climate.h"
#include "WorldEnvironment.h"
#include "wr/WRFileSystem.h"
#include "wr/WRString.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Degrees to radians, as the climate reads angles. */
#define TEST_DEGREES_TO_RADIANS 0.017453292519943295
/** How close a float read from text must come to its value. */
#define TEST_EPSILON 1.0e-6f


// Types.
/** A definition that must be refused: the JSON, the error code and a piece of its message. */
typedef struct BrokenClimateStruct
{
    const char* Json;
    ErrorCode Code;
    const char* MessagePart;
} BrokenClimate;


// Static functions.
/* Builds a climate definition from JSON with the real type; returns its error (the definition is destroyed). */
static Error BuildClimate(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.Climate, &Bytes,
        (const unsigned char*)u8"test climate", &Definition);
    Memory_Free(Bytes._data);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    return Result;
}

/* Registers a climate from JSON and loads it under a new user. */
static const Climate* LoadClimate(TestRun* run, TestAssetFixture* fixture, const char* json, const char* name,
    AssetUserID* outUser)
{
    *outUser = ASSET_USER_ID_INVALID;
    if (!TestAssets_AddClimate(run, fixture, json)
        || !TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(fixture->Manager, outUser)))
    {
        return NULL;
    }
    Climate* Loaded = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_LoadClimate(fixture->Manager, (const unsigned char*)name, *outUser,
        &Loaded)))
    {
        return NULL;
    }
    return Loaded;
}

static bool IsClose(float a, float b)
{
    return fabsf(a - b) <= TEST_EPSILON * fmaxf(1.0f, fabsf(b));
}

static bool HasLink(const Climate* climate, const char* from, const char* to)
{
    uint16_t FromIndex = 0U;
    uint16_t ToIndex = 0U;
    if (!Climate_TryFindPreset(climate, (const unsigned char*)from, &FromIndex)
        || !Climate_TryFindPreset(climate, (const unsigned char*)to, &ToIndex))
    {
        return false;
    }
    uint16_t Count = 0U;
    const ClimateLink* Links = Climate_GetLinks(climate, Climate_GetPreset(climate, FromIndex), &Count);
    for (uint16_t Index = 0U; Index < Count; Index++)
    {
        if (Links[Index].Preset == ToIndex)
        {
            return true;
        }
    }
    return false;
}


// Static functions: tests.
static void TheShippedDefaultClimateLoads(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    TestAssetFixture Fixture;
    if (TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset/climates/default.json", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllText(Path._data, &Text))
        && TestAssetFixture_Construct(run, &Fixture))
    {
        AssetUserID User = ASSET_USER_ID_INVALID;
        const Climate* Loaded = LoadClimate(run, &Fixture, (const char*)Text._data, "default", &User);
        if (TEST_CHECK(run, Loaded != NULL))
        {
            const ClimateSettings* Settings = Climate_GetSettings(Loaded);
            TEST_CHECK(run, Settings->Mode == WeatherMode_Simulated);
            TEST_CHECK(run, Settings->IsSeedPerSave);
            TEST_CHECK(run, Climate_GetPresetCount(Loaded) == 7U);
            static const char* const Names[] = { "clear", "cloudy", "overcast", "light_rain", "rain", "storm", "fog" };
            for (size_t Index = 0U; Index < (sizeof(Names) / sizeof(Names[0])); Index++)
            {
                uint16_t Found = 0U;
                TEST_CHECK(run, Climate_TryFindPreset(Loaded, (const unsigned char*)Names[Index], &Found));
            }
            // The shipped sequence: clear, cloudy, overcast, light rain or rain, storm, light rain, cloudy, clear.
            TEST_CHECK(run, HasLink(Loaded, "clear", "cloudy"));
            TEST_CHECK(run, HasLink(Loaded, "cloudy", "overcast"));
            TEST_CHECK(run, HasLink(Loaded, "overcast", "light_rain"));
            TEST_CHECK(run, HasLink(Loaded, "overcast", "rain"));
            TEST_CHECK(run, HasLink(Loaded, "rain", "storm"));
            TEST_CHECK(run, HasLink(Loaded, "storm", "light_rain"));
            TEST_CHECK(run, HasLink(Loaded, "light_rain", "cloudy"));
            TEST_CHECK(run, HasLink(Loaded, "cloudy", "clear"));
            // Fog only in the morning, more likely in autumn than in summer.
            uint16_t Fog = 0U;
            if (TEST_CHECK(run, Climate_TryFindPreset(Loaded, (const unsigned char*)u8"fog", &Fog)))
            {
                const ClimatePreset* Preset = Climate_GetPreset(Loaded, Fog);
                TEST_CHECK(run, Preset->HasStartHours && (Preset->StartHours.Minimum == 3.0f)
                    && (Preset->StartHours.Maximum == 9.0f));
                TEST_CHECK(run, Preset->HasMonthWeights && (Preset->MonthWeights[9] > Preset->MonthWeights[6]));
            }
            uint16_t Storm = 0U;
            if (TEST_CHECK(run, Climate_TryFindPreset(Loaded, (const unsigned char*)u8"storm", &Storm)))
            {
                TEST_CHECK(run, Climate_GetPreset(Loaded, Storm)->HasLightning);
            }
        }
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
    Memory_Free(Path._data);
    Memory_Free(Text._data);
}

static void AMinimalClimateTakesTheDefaults(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    AssetUserID User = ASSET_USER_ID_INVALID;
    const Climate* Loaded = LoadClimate(run, &Fixture, "{\"name\": \"plain\", \"presets\": [{\"name\": \"only\"}]}",
        "plain", &User);
    if (TEST_CHECK(run, Loaded != NULL))
    {
        const ClimateSettings* Settings = Climate_GetSettings(Loaded);
        TEST_CHECK(run, Settings->Mode == WeatherMode_Simulated);
        TEST_CHECK(run, Settings->IsSeedPerSave);
        TEST_CHECK(run, !Settings->IsStartRandom && (Settings->StartPreset == 0U));
        TEST_CHECK(run, Settings->Speed == 1.0f);
        for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
        {
            TEST_CHECK(run, Settings->RatesPerHour[Quantity]
                == WeatherQuantity_GetInfo((WeatherQuantity)Quantity)->DefaultRatePerHour);
        }
        TEST_CHECK(run, Settings->WettingPerHour == CLIMATE_DEFAULT_WETTING_PER_HOUR);
        TEST_CHECK(run, Settings->DryingPerHour == CLIMATE_DEFAULT_DRYING_PER_HOUR);
        TEST_CHECK(run, Settings->ForecastAccuracy == 1.0f);
        TEST_CHECK(run, (Settings->RainCloudRange.Minimum == CLIMATE_DEFAULT_RAIN_CLOUD_MIN)
            && (Settings->RainCloudRange.Maximum == CLIMATE_DEFAULT_RAIN_CLOUD_MAX));
        TEST_CHECK(run, (Settings->StormCloudRange.Minimum == CLIMATE_DEFAULT_STORM_CLOUD_MIN)
            && (Settings->StormCloudRange.Maximum == CLIMATE_DEFAULT_STORM_CLOUD_MAX));
        TEST_CHECK(run, !Settings->HasFixedPreset && (Settings->FixedValueMask == 0U));
        const ClimatePreset* Only = Climate_GetPreset(Loaded, 0U);
        TEST_CHECK(run, (Only->TargetMask == 0U) && (Only->RateMask == 0U) && (Only->LinkCount == 0U));
        TEST_CHECK(run, (Only->DurationHours.Minimum == CLIMATE_DEFAULT_DURATION_MIN_HOURS)
            && (Only->DurationHours.Maximum == CLIMATE_DEFAULT_DURATION_MAX_HOURS));
        TEST_CHECK(run, !Only->HasMonthWeights && !Only->HasStartHours && !Only->HasTransitionHours);
        TEST_CHECK(run, !Only->HasGusts && !Only->HasLightning);
        uint16_t Count = 99U;
        TEST_CHECK(run, (Climate_GetLinks(Loaded, Only, &Count) == NULL) && (Count == 0U));
        TEST_CHECK(run, Climate_GetPreset(Loaded, 1U) == NULL);
    }
    TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void EveryKeyReads(TestRun* run)
{
    static const char* const Json =
        "{\"name\": \"full\", \"mode\": \"fixed\", \"seed\": 1234, \"start\": \"random\", \"speed\": 2.5,"
        " \"forecast_accuracy\": 0.25, \"wetting_per_hour\": 2, \"drying_per_hour\": 0.5,"
        " \"rain_cloud_range\": [0.2, 0.4], \"storm_cloud_range\": 0.7,"
        " \"rates_per_hour\": {\"cloud_coverage\": 0.5, \"wind_direction\": 90},"
        " \"fixed_preset\": \"later\", \"fixed_values\": {\"temperature\": -5, \"wind_direction\": 180},"
        " \"presets\": ["
        "  {\"name\": \"first\", \"cloud_coverage\": [0.2, 0.6], \"temperature\": -3, \"wind_direction\": [10, 20],"
        "   \"duration_hours\": [1, 3], \"transition_hours\": 0, \"start_hours\": [22, 4],"
        "   \"month_weights\": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],"
        "   \"rates_per_hour\": {\"precipitation\": 2},"
        "   \"gusts\": {\"strength\": 7, \"per_minute\": 5},"
        "   \"lightning\": {\"per_minute\": 3, \"per_hour\": 40, \"min_distance\": 100, \"max_distance\": 900},"
        "   \"next\": [{\"preset\": \"later\", \"weight\": 2.5}, {\"preset\": \"first\"}]},"
        "  {\"name\": \"later\", \"duration_hours\": 4, \"lightning\": {}}"
        " ]}";
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    AssetUserID User = ASSET_USER_ID_INVALID;
    const Climate* Loaded = LoadClimate(run, &Fixture, Json, "full", &User);
    if (TEST_CHECK(run, Loaded != NULL))
    {
        const ClimateSettings* Settings = Climate_GetSettings(Loaded);
        TEST_CHECK(run, Settings->Mode == WeatherMode_Fixed);
        TEST_CHECK(run, !Settings->IsSeedPerSave && (Settings->Seed == 1234U));
        TEST_CHECK(run, Settings->IsStartRandom);
        TEST_CHECK(run, Settings->Speed == 2.5f);
        TEST_CHECK(run, Settings->ForecastAccuracy == 0.25f);
        TEST_CHECK(run, (Settings->WettingPerHour == 2.0f) && (Settings->DryingPerHour == 0.5f));
        TEST_CHECK(run, (Settings->RainCloudRange.Minimum == 0.2f) && (Settings->RainCloudRange.Maximum == 0.4f));
        TEST_CHECK(run, (Settings->StormCloudRange.Minimum == 0.7f) && (Settings->StormCloudRange.Maximum == 0.7f));
        TEST_CHECK(run, Settings->RatesPerHour[WeatherQuantity_CloudCoverage] == 0.5f);
        TEST_CHECK(run, IsClose(Settings->RatesPerHour[WeatherQuantity_WindDirection], (float)(90.0 * TEST_DEGREES_TO_RADIANS)));
        TEST_CHECK(run, Settings->RatesPerHour[WeatherQuantity_WindSpeed]
            == WeatherQuantity_GetInfo(WeatherQuantity_WindSpeed)->DefaultRatePerHour);
        TEST_CHECK(run, Settings->HasFixedPreset && (Settings->FixedPreset == 1U));
        TEST_CHECK(run, Settings->FixedValueMask
            == (((uint32_t)1U << WeatherQuantity_Temperature) | ((uint32_t)1U << WeatherQuantity_WindDirection)));
        TEST_CHECK(run, Settings->FixedValues[WeatherQuantity_Temperature] == -5.0f);
        TEST_CHECK(run, IsClose(Settings->FixedValues[WeatherQuantity_WindDirection], (float)(180.0 * TEST_DEGREES_TO_RADIANS)));

        const ClimatePreset* First = Climate_GetPreset(Loaded, 0U);
        TEST_CHECK(run, First->TargetMask == (((uint32_t)1U << WeatherQuantity_CloudCoverage)
            | ((uint32_t)1U << WeatherQuantity_Temperature) | ((uint32_t)1U << WeatherQuantity_WindDirection)));
        TEST_CHECK(run, (First->Targets[WeatherQuantity_CloudCoverage].Minimum == 0.2f)
            && (First->Targets[WeatherQuantity_CloudCoverage].Maximum == 0.6f));
        TEST_CHECK(run, (First->Targets[WeatherQuantity_Temperature].Minimum == -3.0f)
            && (First->Targets[WeatherQuantity_Temperature].Maximum == -3.0f));
        TEST_CHECK(run, IsClose(First->Targets[WeatherQuantity_WindDirection].Maximum, (float)(20.0 * TEST_DEGREES_TO_RADIANS)));
        TEST_CHECK(run, (First->DurationHours.Minimum == 1.0f) && (First->DurationHours.Maximum == 3.0f));
        TEST_CHECK(run, First->HasTransitionHours && (First->TransitionHours == 0.0f));
        TEST_CHECK(run, First->HasStartHours && (First->StartHours.Minimum == 22.0f) && (First->StartHours.Maximum == 4.0f));
        TEST_CHECK(run, First->HasMonthWeights && (First->MonthWeights[0] == 0.0f) && (First->MonthWeights[11] == 11.0f));
        TEST_CHECK(run, (First->RateMask == ((uint32_t)1U << WeatherQuantity_Precipitation))
            && (First->RatesPerHour[WeatherQuantity_Precipitation] == 2.0f));
        TEST_CHECK(run, First->HasGusts && (First->Gusts.Strength == 7.0f) && (First->Gusts.PerMinute == 5.0f));
        TEST_CHECK(run, First->HasLightning && (First->Lightning.PerMinute == 3.0f) && (First->Lightning.PerHour == 40.0f)
            && (First->Lightning.MinDistance == 100.0f) && (First->Lightning.MaxDistance == 900.0f));
        uint16_t LinkCount = 0U;
        const ClimateLink* Links = Climate_GetLinks(Loaded, First, &LinkCount);
        TEST_CHECK(run, (LinkCount == 2U) && (Links != NULL));
        if (LinkCount == 2U)
        {
            // A link may name a preset that comes later in the file; a link's weight is 1 when left out.
            TEST_CHECK(run, (Links[0].Preset == 1U) && (Links[0].Weight == 2.5f));
            TEST_CHECK(run, (Links[1].Preset == 0U) && (Links[1].Weight == 1.0f));
        }
        const ClimatePreset* Later = Climate_GetPreset(Loaded, 1U);
        TEST_CHECK(run, (Later->DurationHours.Minimum == 4.0f) && (Later->DurationHours.Maximum == 4.0f));
        TEST_CHECK(run, Later->HasLightning && (Later->Lightning.PerMinute == 0.0f) && (Later->Lightning.PerHour == 0.0f));
    }
    TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void BrokenDefinitionsAreRefused(TestRun* run)
{
#define PRESET(body) "{\"name\": \"bad\", \"presets\": [" body "]}"
#define ROOT(keys) "{\"name\": \"bad\", " keys ", \"presets\": [{\"name\": \"a\"}, {\"name\": \"b\"}]}"
    static const BrokenClimate Broken[] =
    {
        { "{\"name\": \"bad\"}", ErrorCode_InvalidAssetDefinition, "at least one preset" },
        { "{\"name\": \"bad\", \"presets\": []}", ErrorCode_InvalidAssetDefinition, "1 to 64 presets" },
        { "{\"name\": \"bad\", \"presets\": {}}", ErrorCode_InvalidJSON, "expected an array" },
        { PRESET("{\"name\": \"a\"}, {\"name\": \"a\"}"), ErrorCode_InvalidAssetDefinition, "used by presets[0] too" },
        { PRESET("{\"name\": \"Big\"}"), ErrorCode_InvalidAssetDefinition, "not a valid name" },
        { PRESET("{\"name\": \"random\"}"), ErrorCode_InvalidAssetDefinition, "start key's word" },
        { PRESET("{\"cloud_coverage\": 1}"), ErrorCode_InvalidAssetDefinition, "needs a \"name\"" },
        { PRESET("{\"name\": \"a\", \"next\": [{\"preset\": \"nowhere\"}]}"), ErrorCode_InvalidAssetDefinition,
            "presets[0] (\"a\").next[0].preset: no preset is named \"nowhere\"" },
        { PRESET("{\"name\": \"a\", \"next\": [{\"weight\": 1}]}"), ErrorCode_InvalidAssetDefinition, "needs a \"preset\"" },
        { PRESET("{\"name\": \"a\", \"next\": [{\"preset\": \"a\", \"weight\": 0}]}"), ErrorCode_InvalidAssetDefinition,
            "next[0].weight" },
        { PRESET("{\"name\": \"a\", \"next\": [{\"preset\": \"a\", \"weight\": -1}]}"), ErrorCode_InvalidAssetDefinition,
            "next[0].weight" },
        { PRESET("{\"name\": \"a\", \"month_weights\": [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]}"),
            ErrorCode_InvalidAssetDefinition, "expected 12 month weights" },
        { PRESET("{\"name\": \"a\", \"month_weights\": [1, 1, 1, 1, 1, -1, 1, 1, 1, 1, 1, 1]}"),
            ErrorCode_InvalidAssetDefinition, "month_weights[5]" },
        { PRESET("{\"name\": \"a\", \"start_hours\": [3, 25]}"), ErrorCode_InvalidAssetDefinition, "start_hours[1]" },
        { PRESET("{\"name\": \"a\", \"start_hours\": [5, 5]}"), ErrorCode_InvalidAssetDefinition, "empty window" },
        { PRESET("{\"name\": \"a\", \"start_hours\": [24, 0]}"), ErrorCode_InvalidAssetDefinition,
            "start_hours[0]: a window starts below 24 (midnight" },
        { PRESET("{\"name\": \"a\", \"start_hours\": 5}"), ErrorCode_InvalidJSON, "expected an array" },
        { PRESET("{\"name\": \"a\", \"duration_hours\": [5, 2]}"), ErrorCode_InvalidAssetDefinition, "not in order" },
        { PRESET("{\"name\": \"a\", \"duration_hours\": 0}"), ErrorCode_InvalidAssetDefinition, "duration_hours" },
        { PRESET("{\"name\": \"a\", \"duration_hours\": [0.001, 1]}"), ErrorCode_InvalidAssetDefinition,
            "duration_hours" },
        { PRESET("{\"name\": \"a\", \"transition_hours\": -1}"), ErrorCode_InvalidAssetDefinition, "transition_hours" },
        { PRESET("{\"name\": \"a\", \"cloud_coverage\": 1.5}"), ErrorCode_InvalidAssetDefinition,
            "presets[0] (\"a\").cloud_coverage" },
        { PRESET("{\"name\": \"a\", \"temperature\": [20, 10]}"), ErrorCode_InvalidAssetDefinition, "not in order" },
        { PRESET("{\"name\": \"a\", \"temperature\": [1, 2, 3]}"), ErrorCode_InvalidAssetDefinition,
            "expected a number or [min, max]" },
        { PRESET("{\"name\": \"a\", \"wind_speed\": \"fast\"}"), ErrorCode_InvalidJSON, "wind_speed" },
        { PRESET("{\"name\": \"a\", \"wetness\": 1}"), ErrorCode_InvalidAssetDefinition, "wetness" },
        { PRESET("{\"name\": \"a\", \"gusts\": {\"strength\": 2, \"often\": 1}}"), ErrorCode_InvalidAssetDefinition,
            "often" },
        { PRESET("{\"name\": \"a\", \"gusts\": {\"per_minute\": 0}}"), ErrorCode_InvalidAssetDefinition,
            "gusts.per_minute" },
        { PRESET("{\"name\": \"a\", \"lightning\": {\"per_minute\": 1, \"rate\": 3}}"), ErrorCode_InvalidAssetDefinition,
            "rate" },
        { PRESET("{\"name\": \"a\", \"lightning\": {\"min_distance\": 900, \"max_distance\": 100}}"),
            ErrorCode_InvalidAssetDefinition, "must not be past max_distance" },
        { PRESET("{\"name\": \"a\", \"rates_per_hour\": {\"cloud_coverage\": -1}}"), ErrorCode_InvalidAssetDefinition,
            "rates_per_hour.cloud_coverage" },
        { PRESET("{\"name\": \"a\", \"rates_per_hour\": {\"clouds\": 1}}"), ErrorCode_InvalidAssetDefinition, "clouds" },
        { ROOT("\"weather\": 1"), ErrorCode_InvalidAssetDefinition, "weather" },
        { ROOT("\"mode\": \"sometimes\""), ErrorCode_InvalidAssetDefinition, "is no mode" },
        { ROOT("\"seed\": -1"), ErrorCode_InvalidAssetDefinition, "seed" },
        { ROOT("\"seed\": \"per_game\""), ErrorCode_InvalidAssetDefinition, "seed" },
        { ROOT("\"start\": \"c\""), ErrorCode_InvalidAssetDefinition, "start: no preset is named \"c\"" },
        { ROOT("\"speed\": 0"), ErrorCode_InvalidAssetDefinition, "speed" },
        { ROOT("\"forecast_accuracy\": 1.5"), ErrorCode_InvalidAssetDefinition, "forecast_accuracy" },
        { ROOT("\"rain_cloud_range\": [0.9, 0.6]"), ErrorCode_InvalidAssetDefinition, "not in order" },
        { ROOT("\"storm_cloud_range\": [0.5, 1.5]"), ErrorCode_InvalidAssetDefinition, "storm_cloud_range[1]" },
        { ROOT("\"drying_per_hour\": -0.1"), ErrorCode_InvalidAssetDefinition, "drying_per_hour" },
        { ROOT("\"fixed_preset\": \"c\""), ErrorCode_InvalidAssetDefinition, "fixed_preset: no preset" },
        { ROOT("\"fixed_values\": {\"fog\": 1}"), ErrorCode_InvalidAssetDefinition, "fog" },
        { ROOT("\"fixed_values\": {\"cloud_coverage\": [0, 1]}"), ErrorCode_InvalidAssetDefinition, "expected a number" },
        { ROOT("\"fixed_values\": {\"precipitation\": 2}"), ErrorCode_InvalidAssetDefinition,
            "fixed_values.precipitation" },
    };
#undef PRESET
#undef ROOT
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    for (size_t Index = 0U; Index < (sizeof(Broken) / sizeof(Broken[0])); Index++)
    {
        Error Result = BuildClimate(&Fixture, Broken[Index].Json);
        bool IsRefused = (Result.Code == Broken[Index].Code) && (Result.Message != NULL)
            && (strstr((const char*)Result.Message, Broken[Index].MessagePart) != NULL);
        if (!TEST_CHECK(run, IsRefused))
        {
            printf("      case %zu: code %d, message: %s\n", Index, (int)Result.Code,
                (Result.Message != NULL) ? (const char*)Result.Message : "(none)");
        }
        Error_Deconstruct(&Result);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TooManyPresetsAreRefused(TestRun* run)
{
    GenericBuffer Json;
    GenericBuffer_AllocateVariable(&Json, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Json, (const unsigned char*)u8"{\"name\": \"many\", \"presets\": [");
    for (int Index = 0; Index <= CLIMATE_MAX_PRESETS; Index++)
    {
        char Entry[64];
        snprintf(Entry, sizeof(Entry), "%s{\"name\": \"p%d\"}", (Index > 0) ? ", " : "", Index);
        GenericBuffer_AppendString(&Json, (const unsigned char*)Entry);
    }
    GenericBuffer_AppendString(&Json, (const unsigned char*)u8"]}");
    GenericBuffer_NullTerminate(&Json);
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK_ERROR(run, BuildClimate(&Fixture, (const char*)Json._data), ErrorCode_InvalidAssetDefinition);
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
    Memory_Free(Json._data);
}

static void QuantitiesMatchTheEnvironmentsRules(TestRun* run)
{
    const PropertyTable* Table = WorldEnvironment_GetPropertyTable();
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        const WeatherQuantityInfo* Info = WeatherQuantity_GetInfo((WeatherQuantity)Quantity);
        WeatherQuantity Parsed = WeatherQuantity_Count;
        TEST_CHECK(run, WeatherQuantity_TryParseKey(Info->Key, &Parsed) && ((int)Parsed == Quantity));
        const PropertyDescriptor* Row = PropertyTable_FindByKey(Table, Info->Key);
        if (!TEST_CHECK(run, Row != NULL))
        {
            continue;
        }
        if (Info->IsAngle)
        {
            TEST_CHECK(run, (Row->Type == PropertyType_Angle) && !Row->Rules.HasMinimum && !Row->Rules.HasMaximum);
            continue;
        }
        TEST_CHECK(run, Row->Rules.HasMinimum && Row->Rules.HasMaximum);
        TEST_CHECK(run, ((float)Row->Rules.Minimum == Info->Minimum) && ((float)Row->Rules.Maximum == Info->Maximum));
        TEST_CHECK(run, Info->DefaultRatePerHour > 0.0f);
    }
    WeatherQuantity Unused = WeatherQuantity_Count;
    TEST_CHECK(run, !WeatherQuantity_TryParseKey((const unsigned char*)u8"wetness", &Unused));
    TEST_CHECK(run, !WeatherQuantity_TryParseKey(NULL, &Unused));
    TEST_CHECK(run, StringUTF8_GetByteLength(WeatherMode_GetName(WeatherMode_Scripted)) == 8U);
}

static void ACloneIsTheSameClimate(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    AssetUserID User = ASSET_USER_ID_INVALID;
    const Climate* Loaded = LoadClimate(run, &Fixture,
        "{\"name\": \"two\", \"presets\": [{\"name\": \"a\", \"next\": [{\"preset\": \"b\"}]}, {\"name\": \"b\"}]}", "two",
        &User);
    if (TEST_CHECK(run, Loaded != NULL))
    {
        Climate* Copy = Climate_Clone(Loaded);
        uint16_t Index = 0U;
        TEST_CHECK(run, (Climate_GetPresetCount(Copy) == 2U) && Climate_TryFindPreset(Copy, (const unsigned char*)u8"b",
            &Index) && (Index == 1U));
        TEST_CHECK(run, HasLink(Copy, "a", "b"));
        Climate_Destroy(Copy);
        Climate_Destroy(NULL);
    }
    TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void ClimateTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_shipped_default_climate_loads", TheShippedDefaultClimateLoads },
        { "a_minimal_climate_takes_the_defaults", AMinimalClimateTakesTheDefaults },
        { "every_key_reads", EveryKeyReads },
        { "broken_definitions_are_refused", BrokenDefinitionsAreRefused },
        { "too_many_presets_are_refused", TooManyPresetsAreRefused },
        { "quantities_match_the_environments_rules", QuantitiesMatchTheEnvironmentsRules },
        { "a_clone_is_the_same_climate", ACloneIsTheSameClimate },
    };
    TestRun_RunSuite(run, "Climate", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
