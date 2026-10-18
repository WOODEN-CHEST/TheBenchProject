#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "TestFramework.h"
#include "AssetManager.h"
#include "raylib/raylib.h"


/**
 * Asset definitions built in code for headless tests. They load without files or a GL context, so tests can
 * exercise code that holds real assets (the world's sprite animations, the renderer's per-object cache):
 *  - a sprite animation with one texture-less frame, or with several frames and running playback;
 *  - a model without meshes whose mesh-space bounds are given (enough for anything that does not draw);
 *  - a monospace font of the printable ASCII glyphs with metrics but no texture (enough to measure text).
 * Each definition can count how often its asset is loaded and unloaded, which shows when an asset user really
 * releases it.
 */


// Macros.
/** @brief Base (rasterization) size, in pixels, of the fonts TestAssets_AddFont registers. */
#define TEST_FONT_BASE_SIZE (10)
/** @brief First and last codepoint of the fonts TestAssets_AddFont registers (printable ASCII). */
#define TEST_FONT_FIRST_CODEPOINT (32)
#define TEST_FONT_LAST_CODEPOINT (126)


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
 * @brief Registers a sprite animation definition named @p name whose asset has @p frameCount texture-less frames and
 *        plays them running and looped at @p fps (frame step 1).
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddPlayingSpriteAnimation(TestRun* run, TestAssetFixture* fixture, const char* name, size_t frameCount,
    double fps, TestAssetCounters* counters);

/**
 * @brief Registers a font definition named @p name whose asset is a texture-less monospace font: every printable
 *        ASCII glyph (TEST_FONT_FIRST_CODEPOINT to TEST_FONT_LAST_CODEPOINT, '?' included) advances @p advancePixels at
 *        a base size of TEST_FONT_BASE_SIZE pixels. Its shadow offset is the default one.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddFont(TestRun* run, TestAssetFixture* fixture, const char* name, int advancePixels,
    TestAssetCounters* counters);

/**
 * @brief Like TestAssets_AddFont, but the font's shadow offset (GameFont_GetShadowOffset) is @p shadowOffset.
 * @returns true when it was registered.
 */
bool TestAssets_AddFontWithShadowOffset(TestRun* run, TestAssetFixture* fixture, const char* name, int advancePixels,
    Vector2 shadowOffset, TestAssetCounters* counters);

/**
 * @brief Registers a model definition named @p name whose asset has no meshes, an identity import transform and
 *        the mesh-space bounds @p bounds.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddModel(TestRun* run, TestAssetFixture* fixture, const char* name, BoundingBox bounds,
    TestAssetCounters* counters);
