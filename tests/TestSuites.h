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

/** @brief Shader #include expansion: line numbering, repeats, depth, errors, the shipped shaders (ShaderSourceTests.c;
 *  a private module of source/). */
void ShaderSourceTests_Run(TestRun* run);

/** @brief The texture asset type and the shared texture_properties parser: keys, defaults, rejections, the size limit
 *  and the shipped placeholder (TextureTests.c). */
void TextureTests_Run(TestRun* run);

/** @brief Point-light reach culling (WorldLightCullingTests.c). */
void WorldLightCullingTests_Run(TestRun* run);

/** @brief World object set, sprite binding under the world's asset user, renames (WorldTests.c). */
void WorldTests_Run(TestRun* run);

/** @brief Environment validation, copying, the day-night advance and the celestial frame (WorldEnvironmentTests.c). */
void WorldEnvironmentTests_Run(TestRun* run);

/** @brief The night sky's maths: the star catalogue, limiting magnitude, air mass, star colours and the galaxy's
 *  orientation (NightSkyTests.c; a private module of source/). */
void NightSkyTests_Run(TestRun* run);

/** @brief World <-> DTO round trip and snapshot validation (WorldDTOTests.c). */
void WorldDTOTests_Run(TestRun* run);

/** @brief The renderer's per-object render cache (WorldRenderCacheTests.c; a private module of source/). */
void WorldRenderCacheTests_Run(TestRun* run);

/** @brief Log rotation, pruning and formatted output under a base directory (LoggerTests.c). */
void LoggerTests_Run(TestRun* run);

/** @brief The text component tree: ownership, depth limit, owned strings, revisions, clone, style (TextComponentTests.c). */
void TextComponentTests_Run(TestRun* run);

/** @brief The shared JSON/GHDF text codec: round trips, shorthands, errors, depth limit (TextComponentCodecTests.c). */
void TextComponentCodecTests_Run(TestRun* run);

/** @brief Text documents: tree ownership, font and animation bindings, fallbacks, ticking (TextDocumentTests.c). */
void TextDocumentTests_Run(TestRun* run);

/** @brief Text layout: lines, wrapping, shrinking, alignment, runs and staleness (TextLayoutTests.c). */
void TextLayoutTests_Run(TestRun* run);

/** @brief Fonts: glyph lookup, the "codepoints" key, font definitions and the shipped fonts (FontTests.c). */
void FontTests_Run(TestRun* run);

/** @brief The label widget's text, layout properties and construction (LabelWidgetTests.c). */
void LabelWidgetTests_Run(TestRun* run);

/** @brief Keys, the per-tick input with consumption, actions and controls files (GameInputTests.c). */
void GameInputTests_Run(TestRun* run);

/** @brief Easing, keyframe validation and evaluation of typed values (UIAnimationTests.c). */
void UIAnimationTests_Run(TestRun* run);

/** @brief The UI screen: ids, deferred destruction, focus, navigation, input routing, drawing order, animations and
 *  notifications (UIScreenTests.c). */
void UIScreenTests_Run(TestRun* run);

/** @brief The UI test frame run headless: how its menu shares input with the world below (UITestFrameTests.c). */
void UITestFrameTests_Run(TestRun* run);
