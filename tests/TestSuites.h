#pragma once
#include "TestFramework.h"


/**
 * Entry points of the test suites, one per test file, called in order by TestMain.c. To add a suite: create
 * "<Module>Tests.c" with a static TestCase array and a "<Module>Tests_Run" function that passes it to
 * TestRun_RunSuite, declare that function here, and call it from TestMain.c.
 */

/** @brief GameJSON value conversions (GameJSONTests.c). */
void GameJSONTests_Run(TestRun* run);

/** @brief Config file loading and validation (ConfigTests.c). */
void ConfigTests_Run(TestRun* run);

/** @brief Asset definition scanning, file matching and the failed-load cache (AssetManagerTests.c). */
void AssetManagerTests_Run(TestRun* run);

/** @brief Point-light reach culling (WorldLightCullingTests.c). */
void WorldLightCullingTests_Run(TestRun* run);

/** @brief World object set, sprite binding under the world's asset user, renames (WorldTests.c). */
void WorldTests_Run(TestRun* run);

/** @brief Environment validation, copying and the day-night advance (WorldEnvironmentTests.c). */
void WorldEnvironmentTests_Run(TestRun* run);

/** @brief World <-> DTO round trip and snapshot validation (WorldDTOTests.c). */
void WorldDTOTests_Run(TestRun* run);

/** @brief The renderer's per-object render cache (WorldRenderCacheTests.c; a private module of source/). */
void WorldRenderCacheTests_Run(TestRun* run);

/** @brief Log rotation, pruning and formatted output under a base directory (LoggerTests.c). */
void LoggerTests_Run(TestRun* run);
