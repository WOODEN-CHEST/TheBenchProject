#include "TestSuites.h"
#include "Config.h"
#include "wr/WRPath.h"


// Macros.
/** Initial capacity (bytes) of path buffers; they grow as needed. */
#define CONFIG_TEST_PATH_CAPACITY ((size_t)256)


// Static functions.
/* Writes @p text as "<work>/<testName>/config.json" and loads it into @p outConfig, returning the load result.
 * The config is always left valid (defaults where the file is unusable), matching GameConfig_LoadFromFile. */
static Error LoadConfigText(TestRun* run, const char* testName, const char* text, GameConfig* outConfig)
{
    GameConfig_SetDefaults(outConfig);

    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, CONFIG_TEST_PATH_CAPACITY, sizeof(unsigned char));
    GenericBuffer ConfigPath;
    GenericBuffer_AllocateVariable(&ConfigPath, CONFIG_TEST_PATH_CAPACITY, sizeof(unsigned char));

    Error Result = TestRun_CreateWorkDirectory(run, testName, &Directory);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Test_WriteTextFile(Directory._data, "config.json", text);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Path_Append(Directory._data, (const unsigned char*)u8"config.json", &ConfigPath);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameConfig_LoadFromFile(ConfigPath._data, outConfig);
    }

    Memory_Free(ConfigPath._data);
    Memory_Free(Directory._data);
    return Result;
}

/* Whether @p config still holds the defaults of the window/pacing settings. */
static bool HasDefaultWindowSettings(const GameConfig* config)
{
    GameConfig Defaults;
    GameConfig_SetDefaults(&Defaults);
    return (config->TargetFPS == Defaults.TargetFPS)
        && (config->ResolutionWidth == Defaults.ResolutionWidth)
        && (config->ResolutionHeight == Defaults.ResolutionHeight)
        && (config->IsFPSUnlocked == Defaults.IsFPSUnlocked)
        && (config->IsVSyncEnabled == Defaults.IsVSyncEnabled)
        && (config->IsFullscreen == Defaults.IsFullscreen);
}

static void MissingFileKeepsDefaults(TestRun* run)
{
    GameConfig Config;
    GameConfig_SetDefaults(&Config);
    Config.TargetFPS = 1; // Must be overwritten by the loader's defaults.
    Error Result = GameConfig_LoadFromFile((const unsigned char*)u8"/nonexistent/dir/config.json", &Config);
    TEST_CHECK(run, (Result.Code == ErrorCode_FileNotFound) || (Result.Code == ErrorCode_DirectoryNotFound));
    Error_Deconstruct(&Result);
    TEST_CHECK(run, HasDefaultWindowSettings(&Config));
}

static void ReadsEveryKey(TestRun* run)
{
    GameConfig Config;
    TEST_CHECK_SUCCESS(run, LoadConfigText(run, "reads_every_key",
        "{ \"is_fps_unlocked\": true, \"target_fps\": 120, \"is_vsync_enabled\": true, \"is_fullscreen\": true,"
        "  \"resolution\": [ 800, 600 ], \"shadow_strength\": 2.5, \"fog_strength\": 0, \"draw_distance_scale\": 0.25,"
        "  \"lod_distance_scale\": 1.5 }",
        &Config));
    TEST_CHECK(run, Config.IsFPSUnlocked);
    TEST_CHECK(run, Config.TargetFPS == 120);
    TEST_CHECK(run, Config.IsVSyncEnabled);
    TEST_CHECK(run, Config.IsFullscreen);
    TEST_CHECK(run, (Config.ResolutionWidth == 800) && (Config.ResolutionHeight == 600));
    TEST_CHECK(run, Config.ShadowStrength == 2.5f);
    TEST_CHECK(run, Config.FogStrength == 0.0f);
    TEST_CHECK(run, Config.DrawDistanceScale == 0.25f);
    TEST_CHECK(run, Config.LodDistanceScale == 1.5f);
}

static void WholeRealIntegersAreAccepted(TestRun* run)
{
    GameConfig Config;
    TEST_CHECK_SUCCESS(run, LoadConfigText(run, "whole_real_integers", "{ \"target_fps\": 75.0 }", &Config));
    TEST_CHECK(run, Config.TargetFPS == 75);
}

static void FractionalIntegersKeepTheDefault(TestRun* run)
{
    GameConfig Config;
    TEST_CHECK_SUCCESS(run, LoadConfigText(run, "fractional_integers", "{ \"target_fps\": 59.9 }", &Config));
    TEST_CHECK(run, HasDefaultWindowSettings(&Config));
}

static void OutOfRangeValuesFallBackToDefaults(TestRun* run)
{
    GameConfig Defaults;
    GameConfig_SetDefaults(&Defaults);
    GameConfig Config;
    TEST_CHECK_SUCCESS(run, LoadConfigText(run, "out_of_range_values",
        "{ \"target_fps\": 5, \"resolution\": [ 100, 1e19 ], \"bloom_strength\": -1, \"sunshaft_strength\": 99,"
        "  \"draw_distance_scale\": 17, \"lod_distance_scale\": -2 }", &Config));
    TEST_CHECK(run, HasDefaultWindowSettings(&Config));
    TEST_CHECK(run, Config.BloomStrength == Defaults.BloomStrength);
    TEST_CHECK(run, Config.SunshaftStrength == Defaults.SunshaftStrength);
    TEST_CHECK(run, (Config.DrawDistanceScale == Defaults.DrawDistanceScale) && (Defaults.DrawDistanceScale == 1.0f));
    TEST_CHECK(run, (Config.LodDistanceScale == Defaults.LodDistanceScale) && (Defaults.LodDistanceScale == 1.0f));
}

static void ReadsTheShadowQuality(TestRun* run)
{
    GameConfig Defaults;
    GameConfig_SetDefaults(&Defaults);
    // The defaults: five levels, a factor of 4, 2048 texels, the periods 1, 1, 2, 4, ... 64, a scale of 1.
    TEST_CHECK(run, (Defaults.ShadowLevelCount == 5) && (Defaults.ShadowLevelFactor == 4.0f));
    TEST_CHECK(run, (Defaults.ShadowMapSize == 2048) && (Defaults.ShadowDistanceScale == 1.0f));
    const int32_t Periods[CONFIG_MAX_SHADOW_LEVELS] = { 1, 1, 2, 4, 8, 16, 32, 64 };
    for (size_t Level = 0U; Level < (size_t)CONFIG_MAX_SHADOW_LEVELS; Level++)
    {
        TEST_CHECK(run, Defaults.ShadowUpdatePeriods[Level] == Periods[Level]);
    }

    // Every key read; a shorter periods array changes only the levels it gives.
    GameConfig Config;
    TEST_CHECK_SUCCESS(run, LoadConfigText(run, "reads_the_shadow_quality",
        "{ \"shadow_distance_scale\": 0.5, \"shadow_level_count\": 7, \"shadow_level_factor\": 2.5,"
        "  \"shadow_map_size\": 4096, \"shadow_update_periods\": [ 1, 2, 3 ] }", &Config));
    TEST_CHECK(run, (Config.ShadowDistanceScale == 0.5f) && (Config.ShadowLevelCount == 7));
    TEST_CHECK(run, (Config.ShadowLevelFactor == 2.5f) && (Config.ShadowMapSize == 4096));
    TEST_CHECK(run, (Config.ShadowUpdatePeriods[0] == 1) && (Config.ShadowUpdatePeriods[1] == 2));
    TEST_CHECK(run, (Config.ShadowUpdatePeriods[2] == 3) && (Config.ShadowUpdatePeriods[3] == 4));

    // Out of range (or not a power of two) keeps each default; a bad period keeps its level's own default.
    TEST_CHECK_SUCCESS(run, LoadConfigText(run, "shadow_quality_out_of_range",
        "{ \"shadow_distance_scale\": -1, \"shadow_level_count\": 9, \"shadow_level_factor\": 1.2,"
        "  \"shadow_map_size\": 3000, \"shadow_update_periods\": [ 0, 1, 2, 999 ] }", &Config));
    TEST_CHECK(run, (Config.ShadowDistanceScale == 1.0f) && (Config.ShadowLevelCount == 5));
    TEST_CHECK(run, (Config.ShadowLevelFactor == 4.0f) && (Config.ShadowMapSize == 2048));
    TEST_CHECK(run, (Config.ShadowUpdatePeriods[0] == 1) && (Config.ShadowUpdatePeriods[2] == 2));
    TEST_CHECK(run, Config.ShadowUpdatePeriods[3] == 4);
    TEST_CHECK_SUCCESS(run, LoadConfigText(run, "shadow_map_size_bounds",
        "{ \"shadow_map_size\": 128, \"shadow_level_factor\": 9 }", &Config));
    TEST_CHECK(run, (Config.ShadowMapSize == 2048) && (Config.ShadowLevelFactor == 4.0f));
}

static void UnknownKeysAreIgnored(TestRun* run)
{
    GameConfig Config;
    TEST_CHECK_SUCCESS(run, LoadConfigText(run, "unknown_keys",
        "{ \"global_volume\": 0.5, \"made_up_key\": [ 1, 2 ], \"target_fps\": 90 }", &Config));
    TEST_CHECK(run, Config.TargetFPS == 90);
}

static void NonObjectRootFailsWithDefaults(TestRun* run)
{
    GameConfig Config;
    TEST_CHECK_ERROR(run, LoadConfigText(run, "non_object_root", "[ 1, 2, 3 ]", &Config), ErrorCode_Deserialize);
    TEST_CHECK(run, HasDefaultWindowSettings(&Config));
}

static void MalformedJsonFailsWithDefaults(TestRun* run)
{
    GameConfig Config;
    Error Result = LoadConfigText(run, "malformed_json", "{ \"target_fps\": ", &Config);
    TEST_CHECK(run, Result.Code != ErrorCode_Success);
    Error_Deconstruct(&Result);
    TEST_CHECK(run, HasDefaultWindowSettings(&Config));
}


// Public functions.
void ConfigTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "missing_file_keeps_defaults", MissingFileKeepsDefaults },
        { "reads_every_key", ReadsEveryKey },
        { "whole_real_integers_are_accepted", WholeRealIntegersAreAccepted },
        { "fractional_integers_keep_the_default", FractionalIntegersKeepTheDefault },
        { "out_of_range_values_fall_back_to_defaults", OutOfRangeValuesFallBackToDefaults },
        { "reads_the_shadow_quality", ReadsTheShadowQuality },
        { "unknown_keys_are_ignored", UnknownKeysAreIgnored },
        { "non_object_root_fails_with_defaults", NonObjectRootFailsWithDefaults },
        { "malformed_json_fails_with_defaults", MalformedJsonFailsWithDefaults },
    };
    TestRun_RunSuite(run, "Config", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
