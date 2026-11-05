#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "GameCamera.h"
#include "GameClock.h"
#include "ProgramTime.h"
#include "Services.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "World.h"
#include "WorldCollision.h"
#include "WorldRenderer.h"
#include "WorldScreenHost.h"
#include "WorldScreens.h"

/*
 * Private header (source/) of the world test frame's screen: the test monitor (the object named "monitor", world model
 * tbp:test_monitor, on its own table in the start view) and the UI it shows, drawn into a render target the renderer
 * shows on the monitor's surface each frame. Test content only: WorldTestFrame.c calls it from its tick and its render.
 *
 * The UI is a full-screen "desk" widget (focusable: a click anywhere on the picture focuses it) that counts the clicks
 * on it and keeps a line of what is typed while it has the keyboard, with labels over it (input-disabled, so every
 * click reaches the desk): a title, the game clock, where the player points, the clicks, the typed line and a hint. Each
 * click also steps the picture's background (blue, green, amber, white), so the monitor's light on the table changes
 * colour. A row of kit elements stands on the desk, checked through the CRT's curved pointer: a button that clears the
 * clicks and the typed line, and a slider for the screen's brightness. The monitor is a CRT: the grave key (`) turns it off and on (its power animation), Shift and the grave key
 * step every CRT's mask (its own, the aperture grille, the shadow mask, none).
 *
 * The player points at the monitor with the camera's aim (walk mode: the eyes, out to the character's reach, the
 * player's own body left out; fly mode: the flycam, out to WORLD_TEST_SCREEN_FLY_REACH) or, while the mouse is free (M),
 * with the cursor's ray, and uses it through a WorldScreenHost: E or a click gives the screen the keyboard, Escape or
 * pointing away lets go. While it is in use the desk or an element on it has the UI's focus, and the UI letting go of it
 * (Escape) lets go of the screen too, so one Escape gives control back to the world.
 */


// Macros.
/** How far the flycam's aim reaches for the monitor, in metres (walk mode uses the character's reach). */
#define WORLD_TEST_SCREEN_FLY_REACH 4.0f
/** The most codepoints the typed line keeps. */
#define WORLD_TEST_SCREEN_TYPED_CAPACITY 40U
/** The most bytes one codepoint takes in UTF-8. */
#define WORLD_TEST_SCREEN_UTF8_MAX_BYTES 4U


// Types.
/** The test monitor and its UI. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestScreensStruct
{
    Services* _services;
    World* _world;
    WorldCollision* _collision;
    WorldScreens* _screens;
    /** The monitor's object (0: the test world has none, and nothing here runs). */
    uint64_t _monitorId;
    WorldScreenHost _host;
    TextComponentFactory _textFactory;
    UIWidgetFactory _widgetFactory;
    UIKit _kit;
    UIScreen _screen;
    /** Which of the four above are constructed (for teardown after a failed construction), and whether the UI is
     *  built. */
    bool _isTextFactoryConstructed;
    bool _isWidgetFactoryConstructed;
    bool _isKitConstructed;
    bool _isScreenConstructed;
    bool _isBuilt;
    uint64_t _labelType;
    uint64_t _deskType;
    /** The desk and the labels whose text changes (WIDGET_ID_INVALID until built). */
    uint64_t _deskId;
    uint64_t _clockId;
    uint64_t _pointerId;
    uint64_t _clicksId;
    uint64_t _typedId;
    uint64_t _hintId;
    /** The kit's button and slider on the desk. */
    uint64_t _clearButtonId;
    uint64_t _brightnessSliderId;
    /** The clicks on the picture and where the last one was (the UI's [0;1] space). */
    uint32_t _clickCount;
    Vector2 _lastClick;
    /** The typed line (UTF-8, NUL-terminated), its bytes and its codepoints. */
    unsigned char _typed[(WORLD_TEST_SCREEN_TYPED_CAPACITY * WORLD_TEST_SCREEN_UTF8_MAX_BYTES) + 1U];
    size_t _typedByteCount;
    size_t _typedCount;
    /** Whether the clicks or the typed line changed since their labels were last written. */
    bool _isTextChanged;
    /** The game minute and the pointer's position (in hundredths; -1: off the picture) the labels show. */
    int64_t _shownMinute;
    int32_t _shownPointerX;
    int32_t _shownPointerY;
    /** Whether the screen was in use, and whether the desk had the UI's focus, after the last update. */
    bool _wasInUse;
    bool _wasDeskFocused;
    /** The picture's render target, made at the surface's resolution on the first render. */
    RenderTexture2D _target;
    bool _hasTarget;
    bool _isTargetFailureReported;
} WorldTestScreens;


// Functions.
/** Finds the test monitor in @p world and builds its UI (its viewport the monitor's surface's resolution, as
 *  @p screens resolved it: sync them first). Without a monitor nothing is built and nothing runs. Fails only on a broken
 *  invariant of the UI's construction. */
Error WorldTestScreens_Construct(WorldTestScreens* self, Services* services, World* world, WorldCollision* collision,
    WorldScreens* screens);

/** Releases the UI and the render target (needs the GL context). */
void WorldTestScreens_Deconstruct(WorldTestScreens* self);

/** One tick, before the frame's own controls (the screen reads first): where the player points (@p playerId the walking
 *  player's object, 0 in fly mode; the cursor's ray while it is free), the screen's UI through its host, the desk's
 *  focus kept with the screen's, the labels. The use and the letting go are logged. */
Error WorldTestScreens_Update(WorldTestScreens* self, ProgramTime time, const GameCamera* camera, uint64_t playerId,
    bool isCursorCaptured, const GameClock* clock);

/** The monitor's keys, while it is not in use (the frame's debug keys' slot): the grave key turns it off and on, Shift
 *  with it steps every CRT's mask through @p renderer's override. Each logged. */
void WorldTestScreens_HandleInput(WorldTestScreens* self, GameInput* input, WorldRenderer* renderer);

/** Draws the UI into the monitor's picture and hands it to the renderer for its next render (call it right before the
 *  world renders). */
Error WorldTestScreens_Render(WorldTestScreens* self, WorldRenderer* renderer);

/** Whether the player uses the monitor (it has the keyboard). */
static inline bool WorldTestScreens_IsInUse(const WorldTestScreens* self)
{
    return WorldScreenHost_IsFocused(&self->_host);
}
