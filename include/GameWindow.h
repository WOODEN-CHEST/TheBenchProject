#pragma once
#include <stdbool.h>
#include "wr/WRMemory.h"
#include "Config.h"
#include "GameInput.h"
#include "GameWindowState.h"
#include "Logger.h"


/**
 * @file GameWindow.h
 * @brief The game's one window: the one module that calls raylib's window functions after start.
 *
 * It opens the window, remembers how the player left it (window_state.json next to the executable, GameWindowState.h: written
 * when the mode changes and when the game ends, never per tick), switches between a resizable window, borderless and fullscreen,
 * and handles the fullscreen toggle (the tbp:toggle_fullscreen action, rebindable) before any frame updates, so no frame sees its
 * keys, which it holds back until they have stayed up for a moment. A window is resizable with a minimum size. High DPI stays off: one window pixel is one physical pixel, and the UI's scale
 * policy (UIScalePolicy.h) answers a dense monitor by its height.
 *
 * Which mode it comes up in, and at what size: the state file's, or on a first run the config's. A bad state file is ignored with one
 * log line.
 *
 * A window that was never opened keeps its state and saves it with no raylib call, so everything but the real window runs headless.
 * Main thread only.
 */


// Macros.
/** @brief How long in seconds the toggle's keys must stay up after a switch before they count again. A window manager's switch takes the
 *  focus away for a moment, losing focus reports every held key as up, and the key's next auto-repeat then arrives as a new press: this
 *  outlasts the longest usual delay before a key repeats. */
#define GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS 1.0


// Types.
/** @brief The game window. Fields are read-only (underscore-prefixed). Construct in place; never move it. */
typedef struct GameWindowStruct
{
    /** @brief What the next save writes: the mode, and the windowed size and place. */
    GameWindowState _state;
    /** @brief Follows the window while it is windowed. */
    GameWindowWatch _watch;
    /** @brief The mode the toggle switches to from a window. */
    GameWindowMode _toggleMode;
    /** @brief Whether the window asks for vertical sync. */
    bool _isVSyncEnabled;
    /** @brief Whether the real window is open. */
    bool _isOpen;
    /** @brief Whether the player asked to close it. Kept here because raylib's answer lasts only until its next poll, and several
     *  update ticks can run between two looks from the game loop. */
    bool _isCloseRequested;
    /** @brief The time of the last update, in seconds. */
    double _time;
    /** @brief The keys that last fired the toggle, held back until they have stayed up until _toggleKeysHeldBackUntil. */
    GameKey _toggleKeys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t _toggleKeyCount;
    double _toggleKeysHeldBackUntil;
    /** @brief The borrowed logger, or NULL. */
    Logger* _logger;
    /** @brief The state file's path, NUL-terminated (empty: nothing is read or saved); owned. */
    GenericBuffer _statePath;
} GameWindow;


// Functions.
/**
 * @brief Constructs a window that is not open yet: the config's mode and size, or the state file's when there is one.
 *
 * A missing state file is a first run. A bad one is ignored with one log line.
 * @param self The window; must not be NULL.
 * @param config The config (validated, as GameConfig_LoadFromFile leaves it); read now and not kept. Must not be NULL.
 * @param baseDirectory The directory of the state file, NUL-terminated; NULL for no file.
 * @param logger Where messages go; borrowed. NULL for none.
 */
void GameWindow_Construct(GameWindow* self, const GameConfig* config, const unsigned char* baseDirectory, Logger* logger);

/**
 * @brief Frees what the window owns. Close it first.
 * @param self The window, or NULL.
 */
void GameWindow_Deconstruct(GameWindow* self);

/**
 * @brief Opens the real window and its GL context as the state says, on the monitors there are now, and shows it once it is where
 *        it belongs. Check GameWindow_IsOpen: raylib says why it could not.
 * @param self The window; must not be NULL.
 * @param title The window's title, NUL-terminated UTF-8.
 */
void GameWindow_Open(GameWindow* self, const char* title);

/**
 * @brief Saves the state and closes the real window, if it is open. Everything that holds GL resources must be gone.
 * @param self The window; must not be NULL.
 */
void GameWindow_Close(GameWindow* self);

/**
 * @brief Whether the real window is open.
 * @param self The window; must not be NULL.
 * @returns Whether it is.
 */
static inline bool GameWindow_IsOpen(const GameWindow* self)
{
    return self->_isOpen;
}

/**
 * @brief Whether the player asked to close the window, in any update tick so far. Always false for one that is not open.
 * @param self The window; must not be NULL.
 * @returns Whether the game should end.
 */
static inline bool GameWindow_ShouldClose(const GameWindow* self)
{
    return self->_isCloseRequested;
}

/**
 * @brief One update tick, right after the poll: a request to close, the fullscreen toggle (consumed when it fires), then a look at
 *        the window. Cheap: it asks raylib only for what raylib already holds.
 * @param self The window; must not be NULL.
 * @param input The tick's input (polled, before any frame reads it), or NULL.
 * @param timeSeconds The time now, for what must hold for a moment.
 */
void GameWindow_Update(GameWindow* self, GameInput* input, double timeSeconds);

/**
 * @brief Switches the window to a mode, keeping the windowed size and place to come back to, and saves the state. Does nothing when
 *        it is that mode already.
 * @param self The window; must not be NULL.
 * @param mode The mode.
 */
void GameWindow_SetMode(GameWindow* self, GameWindowMode mode);

/**
 * @brief Returns the window's mode.
 * @param self The window; must not be NULL.
 * @returns The mode.
 */
static inline GameWindowMode GameWindow_GetMode(const GameWindow* self)
{
    return self->_state.Mode;
}

/**
 * @brief Returns what the next save writes.
 * @param self The window; must not be NULL.
 * @returns The state; borrowed, valid until the next call that changes the window.
 */
static inline const GameWindowState* GameWindow_GetState(const GameWindow* self)
{
    return &self->_state;
}
