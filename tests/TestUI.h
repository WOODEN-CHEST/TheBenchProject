#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "TestAssets.h"
#include "TestFramework.h"
#include "GameInput.h"
#include "NamespacedKey.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"


/**
 * A screen with the kit over a test asset manager, for the suites of the UI's elements and layout: a 1000 x 500 surface whose
 * target area fills it (one normalized-fitted unit is 500 pixels), a monospace font named "mono" whose glyphs advance half
 * their size, and a driven GameInput. A theme is set by name after TestUI_AddTheme, or not at all.
 */


// Macros.
/** @brief The surface's size in the fixture. */
#define TEST_UI_SURFACE_WIDTH 1000.0f
#define TEST_UI_SURFACE_HEIGHT 500.0f
/** @brief The fixture's tick, in seconds (the game's update rate). */
#define TEST_UI_TICK_SECONDS (1.0 / 480.0)
/** @brief How many element events a recorder keeps. */
#define TEST_UI_MAX_RECORDED_EVENTS ((size_t)64)


// Types.
/** @brief The fixture. Fields are the tests' to use. */
typedef struct TestUIFixtureStruct
{
    TestAssetFixture Assets;
    TextComponentFactory Components;
    UIWidgetFactory Widgets;
    UIKit Kit;
    UIScreen Screen;
    KeyRegistry Keys;
    GameInput Input;
    GameInputSample Sample;
    UIScreenInput ScreenInput;
    double Time;
    bool IsReady;
} TestUIFixture;

/** @brief The element events a screen delivered, in order. */
typedef struct TestUIEventsStruct
{
    UIEvent Events[TEST_UI_MAX_RECORDED_EVENTS];
    size_t Count;
    /** @brief Destroy the widget of the first event delivered, from inside its handler. */
    bool IsDestroyingTheWidget;
    /** @brief Also keep the events that only tell of the input (pressed, released, double clicked, scrolled, context requested): the
     *  tests of those events set it, and the others count only what an element did. */
    bool IsRecordingInputEvents;
} TestUIEvents;


// Functions.
/**
 * @brief Builds the fixture; its screen reports to @p logger.
 * @param logger May be NULL.
 * @returns true when every step succeeded (failed checks are recorded on @p run).
 */
bool TestUI_ConstructLogged(TestRun* run, TestUIFixture* self, Logger* logger);

/** @brief Builds the fixture with no logger. */
bool TestUI_Construct(TestRun* run, TestUIFixture* self);

/** @brief Tears it down, checking that no widget and no text component stays behind. */
void TestUI_Deconstruct(TestRun* run, TestUIFixture* self);

/** @brief Registers a theme definition from @p json and makes it the screen's. */
bool TestUI_SetTheme(TestRun* run, TestUIFixture* self, const char* json);

/** @brief Runs one tick, then turns this tick's presses into holds and ends the releases. */
void TestUI_Tick(TestRun* run, TestUIFixture* self);

/** @brief Runs @p count ticks. */
void TestUI_Ticks(TestRun* run, TestUIFixture* self, size_t count);

/** @brief A key or button goes down at the next tick. */
void TestUI_Press(TestUIFixture* self, GameKey key);

/** @brief A key or button comes up at the next tick. */
void TestUI_Release(TestUIFixture* self, GameKey key);

/** @brief A held key auto-repeats at the next tick. */
void TestUI_Repeat(TestUIFixture* self, GameKey key);

/** @brief A key is pressed for one tick and let go at the next. */
void TestUI_Tap(TestRun* run, TestUIFixture* self, GameKey key);

/** @brief The pointer is at a place on the surface, in surface pixels, from the next tick on. */
void TestUI_MovePointer(TestUIFixture* self, float x, float y);

/** @brief The pointer leaves the surface. */
void TestUI_RemovePointer(TestUIFixture* self);

/** @brief Moves the pointer, then a full left click there: press, tick, release, tick. */
void TestUI_Click(TestRun* run, TestUIFixture* self, float x, float y);

/** @brief The wheel moves by @p notches at the next tick. */
void TestUI_Wheel(TestUIFixture* self, float notches);

/** @brief Makes the surface another size (the target area keeps filling it). */
void TestUI_SetSurface(TestRun* run, TestUIFixture* self, float width, float height);

/** @brief A widget's box in surface pixels, as its normalized geometry says. */
Rectangle TestUI_GetPixelBox(TestRun* run, TestUIFixture* self, uint64_t widgetId);

/** @brief Records every element event of the screen from now on; @p events must outlive the screen. */
bool TestUI_RecordEvents(TestRun* run, TestUIFixture* self, TestUIEvents* events);

/** @brief Whether two lengths in pixels are the same whole-pixel edge, float noise aside. */
bool TestUI_IsSamePixel(float a, float b);

/** @brief Whether a box in pixels is at (x, y) with that size, float noise aside. */
bool TestUI_IsBox(Rectangle box, float x, float y, float width, float height);
