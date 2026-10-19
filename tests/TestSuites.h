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

/** @brief Namespaced keys: the key characters, the length limit, the default namespace, interning, stable texts and
 *  prefix search (NamespacedKeyTests.c). */
void NamespacedKeyTests_Run(TestRun* run);

/** @brief Data values: equality, deep copies, homogeneous lists, the depth and size limits, compound names, validation,
 *  byte sizes, and the JSON and GHDF forms with hostile input (DataValueTests.c). */
void DataValueTests_Run(TestRun* run);

/** @brief Object tags and data: the data functions, tags, the data revision, the object and world limits, queries (each
 *  filter, the order, a brute-force comparison on a seeded random world), the tag index, and saving through the object
 *  table with hostile input (ObjectDataTests.c). */
void ObjectDataTests_Run(TestRun* run);

/** @brief The world's objects and components: ids, the per-type arrays, revisions, the sprite binding under the
 *  world's asset user, flags, and the one-transform-driver rule with test-only component types (WorldTests.c). */
void WorldTests_Run(TestRun* run);

/** @brief Property tables: the self-check (every table in the game), construction, validation, copies, JSON and GHDF
 *  round trips, unknown keys, access by key, migration, and the environment's table against the old encoder
 *  (PropertyDescriptorTests.c). */
void PropertyDescriptorTests_Run(TestRun* run);

/** @brief The calendar: day numbers to dates and back over eight centuries, weekdays, leap years, month lengths,
 *  validation, names and the date text (GameCalendarTests.c). */
void GameCalendarTests_Run(TestRun* run);

/** @brief The game clock: the time scale, pausing, crossings, the largest step, fast-forwards over month and year
 *  ends, 1000 days of 480 Hz ticks without drift, validated jumps, the calendar's end, and saving through its table
 *  (GameClockTests.c). */
void GameClockTests_Run(TestRun* run);

/** @brief Environment validation, copying, the date and time the clock writes, and the celestial frame
 *  (WorldEnvironmentTests.c). */
void WorldEnvironmentTests_Run(TestRun* run);

/** @brief The night sky's maths: the star catalogue, limiting magnitude, air mass, star colours and the galaxy's
 *  orientation (NightSkyTests.c; a private module of source/). */
void NightSkyTests_Run(TestRun* run);

/** @brief The eye's night vision: its calibration on the game's light scale, untouched daylight, rods at night, the
 *  strength, the rods' grey (NightVisionTests.c; a private module of source/). */
void NightVisionTests_Run(TestRun* run);

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
