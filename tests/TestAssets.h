#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "TestFramework.h"
#include "AssetManager.h"
#include "raylib/raylib.h"


/**
 * Asset definitions built in code for headless tests. They load without files or a GL context, so tests can
 * exercise code that holds real assets (the world's sprite animations, the renderer's per-object cache):
 *  - a sprite animation with one texture-less frame;
 *  - a model without meshes whose mesh-space bounds are given (enough for anything that does not draw).
 * Each definition can count how often its asset is loaded and unloaded, which shows when an asset user really
 * releases it.
 */


// Types.
/** @brief Load and unload counts of one test definition's asset; owned by the test (e.g. on its stack). */
typedef struct TestAssetCountersStruct
{
    /** @brief How many times the asset was built. */
    size_t LoadCount;
    /** @brief How many times the asset was destroyed. */
    size_t UnloadCount;
} TestAssetCounters;

/** @brief An asset manager with the standard asset types and no search roots; tests add definitions in code. */
typedef struct TestAssetFixtureStruct
{
    /** @brief The manager. */
    AssetManager* Manager;
    /** @brief The standard type ids. */
    StandardAssetTypes Types;
    /** @brief The JSON pool the standard types parse with. */
    JSONObjectPool* Pool;
} TestAssetFixture;


// Functions.
/**
 * @brief Creates the fixture's manager and standard types (no logger, no search roots).
 * @returns true when every step succeeded (failed checks are recorded on @p run).
 */
bool TestAssetFixture_Construct(TestRun* run, TestAssetFixture* self);

/** @brief Deconstructs the manager (unloading what is still loaded) and the JSON pool. */
void TestAssetFixture_Deconstruct(TestRun* run, TestAssetFixture* self);

/**
 * @brief Registers a sprite animation definition named @p name whose asset has one texture-less frame.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddSpriteAnimation(TestRun* run, TestAssetFixture* fixture, const char* name,
    TestAssetCounters* counters);

/**
 * @brief Registers a model definition named @p name whose asset has no meshes, an identity import transform and
 *        the mesh-space bounds @p bounds.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddModel(TestRun* run, TestAssetFixture* fixture, const char* name, BoundingBox bounds,
    TestAssetCounters* counters);
