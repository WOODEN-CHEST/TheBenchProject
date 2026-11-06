#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"


/**
 * @file GameWindowState.h
 * @brief What the game remembers about its window (window_state.json next to the executable), as pure data with no window:
 *        the mode, the windowed size and position, whether it was maximized.
 *
 * The file is the player's, and it is treated as hostile: a size past the limits is clamped, a position off every monitor is
 * dropped (GameWindowState_FitToMonitors), and a file that is not what it should be is refused whole. A property table
 * describes the file. GameWindow.h applies the state to the real window and saves it.
 *
 * GameWindowWatch follows the window while it is windowed, and takes in a new size and position only once they have held for a
 * moment: a window manager reports a maximize as a new size and a new state, in either order, and the maximized size must not
 * be remembered as the size to come back to.
 */


// Forward declarations.
/** @brief A property table; full type in PropertyDescriptor.h. */
struct PropertyTableStruct;


// Macros.
/** @brief The smallest windowed size in pixels. */
#define GAME_WINDOW_MIN_WIDTH ((int32_t)640)
#define GAME_WINDOW_MIN_HEIGHT ((int32_t)360)
/** @brief The largest windowed width and height in pixels. */
#define GAME_WINDOW_MAX_LENGTH ((int32_t)16384)
/** @brief The largest window state file in bytes: a longer one is refused before it is read. */
#define GAME_WINDOW_STATE_MAX_BYTES ((size_t)4096)
/** @brief How much of a window must lie on a monitor for its position to count, so its title bar can be reached: this wide, with
 *  the top of its content this far above the monitor's bottom edge and at least GAME_WINDOW_MIN_TITLE_BAR_ROOM below its top edge. */
#define GAME_WINDOW_MIN_VISIBLE_WIDTH ((int32_t)100)
#define GAME_WINDOW_MIN_VISIBLE_HEIGHT ((int32_t)40)
/** @brief The least room in pixels above a window's content for some of its title bar to show: positions are the content's, and raylib does
 *  not say how tall a title bar is. */
#define GAME_WINDOW_MIN_TITLE_BAR_ROOM ((int32_t)16)
/** @brief How long in seconds a windowed size and position must hold before they are remembered, and how long after a change
 *  of mode what the window reports is not believed. */
#define GAME_WINDOW_SETTLE_SECONDS 0.5


// Types.
/** @brief How the window takes the screen. Saved by name in the state file. */
typedef enum GameWindowModeEnum
{
    /** @brief A normal window with a title bar: resizable, with a minimum size. */
    GameWindowMode_Windowed = 0,
    /** @brief The whole monitor with no border, at the desktop's video mode. */
    GameWindowMode_Borderless = 1,
    /** @brief Fullscreen on the monitor. */
    GameWindowMode_Fullscreen = 2,
} GameWindowMode;

/**
 * @brief What is remembered about the window: a plain value owning nothing.
 *
 * The size and the position are the windowed ones in every mode, and those from before a maximize while maximized: where the
 * window goes back to.
 */
typedef struct GameWindowStateStruct
{
    /** @brief The mode. */
    GameWindowMode Mode;
    /** @brief The windowed size in pixels, within the limits once read. */
    int32_t Width;
    int32_t Height;
    /** @brief Whether X and Y hold a place: without one the window is centred. */
    bool HasPosition;
    /** @brief The window's content area's top-left corner on the desktop, in pixels; valid with HasPosition. */
    int32_t X;
    int32_t Y;
    /** @brief Whether the window was maximized while windowed. */
    bool IsMaximized;
} GameWindowState;

/** @brief A monitor's place on the desktop and its size in pixels. */
typedef struct GameWindowMonitorStruct
{
    int32_t X;
    int32_t Y;
    int32_t Width;
    int32_t Height;
} GameWindowMonitor;

/** @brief What the window looks like at one moment. */
typedef struct GameWindowObservationStruct
{
    /** @brief The content area's size in pixels. */
    int32_t Width;
    int32_t Height;
    /** @brief The content area's top-left corner on the desktop. */
    int32_t X;
    int32_t Y;
    /** @brief Whether it is maximized. */
    bool IsMaximized;
    /** @brief Whether it is minimized: what it reports means nothing then (Windows puts it at -32000). */
    bool IsMinimized;
} GameWindowObservation;

/** @brief Follows a windowed window into a GameWindowState. Fields are private; a plain value owning nothing. */
typedef struct GameWindowWatchStruct
{
    /** @brief What was seen last, not yet remembered, and since when; valid with _hasPending. */
    GameWindowObservation _pending;
    double _pendingSince;
    bool _hasPending;
    /** @brief Until when nothing seen is believed (the window is still switching modes). */
    double _heldUntil;
} GameWindowWatch;


// Functions: modes.
/**
 * @brief Finds a mode by its name ("windowed", "borderless", "fullscreen").
 * @param text The name, NUL-terminated, or NULL.
 * @param outMode [out] The mode, when found; left as it was otherwise. Must not be NULL.
 * @returns Whether @p text names a mode.
 */
bool GameWindowMode_TryParse(const unsigned char* text, GameWindowMode* outMode);

/**
 * @brief Returns a mode's name.
 * @param mode The mode.
 * @returns The static name, or "unknown" for a value that is no mode.
 */
const unsigned char* GameWindowMode_GetName(GameWindowMode mode);

/**
 * @brief The mode the fullscreen toggle goes to. Pure.
 * @param current The mode now.
 * @param toggleMode The mode it switches to from a window (borderless or fullscreen; anything else counts as borderless).
 * @returns @p toggleMode from a window, and a window from anything else.
 */
GameWindowMode GameWindowMode_GetToggleTarget(GameWindowMode current, GameWindowMode toggleMode);


// Functions: the state.
/**
 * @brief Returns the state's property table: the state file's keys, their defaults and their types.
 * @returns The static table.
 */
const struct PropertyTableStruct* GameWindowState_GetPropertyTable(void);

/**
 * @brief Fills a state with the defaults: windowed at 1280 x 720, centred, not maximized.
 * @param self The state; must not be NULL.
 */
void GameWindowState_SetDefaults(GameWindowState* self);

/**
 * @brief Keeps the size within GAME_WINDOW_MIN_* and GAME_WINDOW_MAX_LENGTH. Pure.
 * @param self The state; must not be NULL.
 */
void GameWindowState_ClampSize(GameWindowState* self);

/**
 * @brief Fits a state to the monitors there are now. Pure.
 *
 * A window with a place may span monitors, so the size is kept within the box around them all. Its position is then judged at that size: one
 * with no monitor holding at least GAME_WINDOW_MIN_VISIBLE_* of the window's top is dropped, and so is any position with no monitors. A
 * window with no place opens on the first monitor, so its size is kept within that one. The size never goes below the minimum.
 * @param self The state; must not be NULL.
 * @param monitors The monitors, the primary one first; those of no size are skipped. May be NULL with a count of 0.
 * @param monitorCount How many.
 */
void GameWindowState_FitToMonitors(GameWindowState* self, const GameWindowMonitor* monitors, size_t monitorCount);

/**
 * @brief Where a window with no place opens on a monitor: centred, with room above it for some of its title bar. Pure.
 * @param self The state, for its size; must not be NULL.
 * @param monitor The monitor; must not be NULL.
 * @param outX [out] The content's left edge on the desktop. Must not be NULL.
 * @param outY [out] The content's top edge on the desktop. Must not be NULL.
 */
void GameWindowState_GetCentredPosition(const GameWindowState* self, const GameWindowMonitor* monitor, int32_t* outX, int32_t* outY);

/**
 * @brief Reads a state from the text of a state file: all or nothing, with the size clamped.
 *
 * A key left out takes its default, and an unknown key is ignored, so a file from another version of the game still reads.
 * @param bytes The file's bytes (no terminator needed); may be NULL with a count of 0.
 * @param byteCount How many.
 * @param outState [out] The state; unchanged on failure. Must not be NULL.
 * @returns Success; ErrorCode_InvalidJSON for text that is too long, is no JSON object, or holds a value of the wrong type.
 */
Error GameWindowState_ReadJSON(const unsigned char* bytes, size_t byteCount, GameWindowState* outState);

/**
 * @brief Writes a state as the text of a state file, every key included, appended to @p outBytes.
 * @param self The state; must not be NULL.
 * @param outBytes [out] A byte buffer the text goes after what it holds.
 * @returns Success, or the error of the writer.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameWindowState_WriteJSON(const GameWindowState* self, GenericBuffer* outBytes);

/**
 * @brief Reads a state file. Refuses a file longer than GAME_WINDOW_STATE_MAX_BYTES before reading it.
 * @param path The file's path, NUL-terminated; must not be NULL.
 * @param outState [out] The state; unchanged on failure. Must not be NULL.
 * @returns Success; ErrorCode_FileNotFound or ErrorCode_DirectoryNotFound when there is no file; ErrorCode_InvalidJSON for a bad
 *          one; other file system errors as reported.
 */
Error GameWindowState_LoadFile(const unsigned char* path, GameWindowState* outState);

/**
 * @brief Saves a state file atomically: a temporary file next to it, then a replace, so the file is the old one or the new one.
 * @param self The state; must not be NULL.
 * @param path The file's path, NUL-terminated; must not be NULL.
 * @returns Success, or the file system's error (the file is then unchanged).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error GameWindowState_SaveFile(const GameWindowState* self, const unsigned char* path);


// Functions: the watch.
/**
 * @brief Constructs a watch with nothing seen yet.
 * @param self The watch; must not be NULL.
 */
void GameWindowWatch_Construct(GameWindowWatch* self);

/**
 * @brief Forgets what was seen and believes nothing for GAME_WINDOW_SETTLE_SECONDS: the mode has just changed, and the window's
 *        size and position are still arriving.
 * @param self The watch; must not be NULL.
 * @param timeSeconds The time now.
 */
void GameWindowWatch_Hold(GameWindowWatch* self, double timeSeconds);

/**
 * @brief One look at a windowed window. Pure.
 *
 * Whether it is maximized goes to the state at once. A size and a position are remembered once they have held for
 * GAME_WINDOW_SETTLE_SECONDS, and never while maximized or minimized. Nothing happens during a hold.
 * @param self The watch; must not be NULL.
 * @param state The state to keep up to date; must not be NULL.
 * @param seen What the window looks like; must not be NULL. A size of 0 is not looked at.
 * @param timeSeconds The time now.
 */
void GameWindowWatch_Observe(GameWindowWatch* self, GameWindowState* state, const GameWindowObservation* seen, double timeSeconds);

/**
 * @brief Remembers what was seen last at once, however briefly it has held: the state is about to be saved.
 * @param self The watch; must not be NULL.
 * @param state The state; must not be NULL.
 */
void GameWindowWatch_Settle(GameWindowWatch* self, GameWindowState* state);
