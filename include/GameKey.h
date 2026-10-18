#pragma once
#include <stdbool.h>
#include <stdint.h>


/**
 * @file GameKey.h
 * @brief The game-wide identity of every key and button a player can press: keyboard keys and mouse buttons
 *        (gamepad buttons come later), each with a stable id string for binding files and a display name.
 *
 * GameKey replaces raylib's key and mouse-button codes everywhere above the input layer: GameInput samples the
 * devices into per-GameKey states, actions are bound to GameKeys, and the UI hands GameKeys to widgets. Only
 * GameInput knows the raylib codes behind them.
 *
 * Id strings are lowercase and stable ("w", "left_shift", "mouse_left"): controls files store them, so an id
 * must never be renamed. Display names ("W", "Left Shift", "Left Mouse") are for controls menus and on-screen
 * prompts; they are English until string tables exist. An icon per key (a sprite referenced by name) comes with
 * the controls menu.
 *
 * Everything here is constant data: the functions never fail, allocate or change state.
 */


// Macros.
/** @brief No modifier key. */
#define GAME_KEY_MODIFIER_NONE ((uint8_t)0x00)
/** @brief Either Shift key. */
#define GAME_KEY_MODIFIER_SHIFT ((uint8_t)0x01)
/** @brief Either Control key. */
#define GAME_KEY_MODIFIER_CONTROL ((uint8_t)0x02)
/** @brief Either Alt key. */
#define GAME_KEY_MODIFIER_ALT ((uint8_t)0x04)
/** @brief Either Super (Windows / Command) key. */
#define GAME_KEY_MODIFIER_SUPER ((uint8_t)0x08)
/** @brief Every modifier flag; a modifier set outside this mask is invalid. */
#define GAME_KEY_MODIFIER_ALL ((uint8_t)0x0F)


// Types.
/**
 * @brief A key or button a player can press, independent of any library's codes.
 *
 * Keyboard keys come first, then the mouse buttons. GameKey_Unknown is "no key" and GameKey_Count is one past
 * the last key; neither is a real key. Gamepad buttons will be appended after the mouse buttons, so code must
 * not assume that the mouse buttons are the last keys (use GameKey_IsMouseButton).
 */
typedef enum GameKeyEnum
{
    /** @brief No key. Not a real key. */
    GameKey_Unknown = 0,

    // Letter keys A-Z (self-evident; one constant per Latin letter, by position on a US layout).
    GameKey_A, GameKey_B, GameKey_C, GameKey_D, GameKey_E, GameKey_F, GameKey_G, GameKey_H, GameKey_I,
    GameKey_J, GameKey_K, GameKey_L, GameKey_M, GameKey_N, GameKey_O, GameKey_P, GameKey_Q, GameKey_R,
    GameKey_S, GameKey_T, GameKey_U, GameKey_V, GameKey_W, GameKey_X, GameKey_Y, GameKey_Z,

    // Top-row digit keys 0-9 (self-evident; one constant per digit).
    GameKey_0, GameKey_1, GameKey_2, GameKey_3, GameKey_4, GameKey_5, GameKey_6, GameKey_7, GameKey_8, GameKey_9,

    // Function keys F1-F12 (self-evident; one constant per function key).
    GameKey_F1, GameKey_F2, GameKey_F3, GameKey_F4, GameKey_F5, GameKey_F6,
    GameKey_F7, GameKey_F8, GameKey_F9, GameKey_F10, GameKey_F11, GameKey_F12,

    /** @brief The spacebar. */
    GameKey_Space,
    /** @brief The Enter / Return key of the main block. */
    GameKey_Enter,
    /** @brief The Escape key. */
    GameKey_Escape,
    /** @brief The Tab key. */
    GameKey_Tab,
    /** @brief The Backspace key. */
    GameKey_Backspace,
    /** @brief The Delete key. */
    GameKey_Delete,
    /** @brief The Insert key. */
    GameKey_Insert,
    /** @brief The Home key. */
    GameKey_Home,
    /** @brief The End key. */
    GameKey_End,
    /** @brief The Page Up key. */
    GameKey_PageUp,
    /** @brief The Page Down key. */
    GameKey_PageDown,

    /** @brief The Up arrow key. */
    GameKey_Up,
    /** @brief The Down arrow key. */
    GameKey_Down,
    /** @brief The Left arrow key. */
    GameKey_Left,
    /** @brief The Right arrow key. */
    GameKey_Right,

    /** @brief The left Shift key. */
    GameKey_LeftShift,
    /** @brief The right Shift key. */
    GameKey_RightShift,
    /** @brief The left Control key. */
    GameKey_LeftControl,
    /** @brief The right Control key. */
    GameKey_RightControl,
    /** @brief The left Alt key. */
    GameKey_LeftAlt,
    /** @brief The right Alt (AltGr) key. */
    GameKey_RightAlt,
    /** @brief The left Super (Windows / Command) key. */
    GameKey_LeftSuper,
    /** @brief The right Super (Windows / Command) key. */
    GameKey_RightSuper,

    /** @brief The ' key. */
    GameKey_Apostrophe,
    /** @brief The , key. */
    GameKey_Comma,
    /** @brief The - key. */
    GameKey_Minus,
    /** @brief The . key. */
    GameKey_Period,
    /** @brief The / key. */
    GameKey_Slash,
    /** @brief The ; key. */
    GameKey_Semicolon,
    /** @brief The = key. */
    GameKey_Equal,
    /** @brief The [ key. */
    GameKey_LeftBracket,
    /** @brief The \ key. */
    GameKey_Backslash,
    /** @brief The ] key. */
    GameKey_RightBracket,
    /** @brief The ` key. */
    GameKey_Grave,

    /** @brief The Caps Lock key. */
    GameKey_CapsLock,
    /** @brief The Scroll Lock key. */
    GameKey_ScrollLock,
    /** @brief The Num Lock key. */
    GameKey_NumLock,
    /** @brief The Print Screen key. */
    GameKey_PrintScreen,
    /** @brief The Pause / Break key. */
    GameKey_Pause,
    /** @brief The context-menu key. */
    GameKey_Menu,

    // Keypad digit keys 0-9 (self-evident; one constant per digit).
    GameKey_Keypad0, GameKey_Keypad1, GameKey_Keypad2, GameKey_Keypad3, GameKey_Keypad4,
    GameKey_Keypad5, GameKey_Keypad6, GameKey_Keypad7, GameKey_Keypad8, GameKey_Keypad9,
    /** @brief The keypad . key. */
    GameKey_KeypadDecimal,
    /** @brief The keypad / key. */
    GameKey_KeypadDivide,
    /** @brief The keypad * key. */
    GameKey_KeypadMultiply,
    /** @brief The keypad - key. */
    GameKey_KeypadSubtract,
    /** @brief The keypad + key. */
    GameKey_KeypadAdd,
    /** @brief The keypad Enter key. */
    GameKey_KeypadEnter,
    /** @brief The keypad = key. */
    GameKey_KeypadEqual,

    /** @brief The primary (left) mouse button. */
    GameKey_MouseLeft,
    /** @brief The secondary (right) mouse button. */
    GameKey_MouseRight,
    /** @brief The middle mouse button (pressing the wheel). */
    GameKey_MouseMiddle,
    /** @brief The first side mouse button. */
    GameKey_MouseSide,
    /** @brief The extra mouse button. */
    GameKey_MouseExtra,
    /** @brief The forward mouse button. */
    GameKey_MouseForward,
    /** @brief The back mouse button. */
    GameKey_MouseBack,

    // TODO: gamepad buttons go here, after the mouse buttons (FIX_PLAN D7: GameKey leaves room for them).

    /** @brief One past the last key; equals the number of keys including GameKey_Unknown. Not a real key. */
    GameKey_Count
} GameKey;


// Functions.
/**
 * @brief Returns whether @p key is a real key (not GameKey_Unknown, GameKey_Count or an out-of-range value).
 * @param key The key.
 * @returns true for a real key.
 */
bool GameKey_IsValid(GameKey key);

/**
 * @brief Returns whether @p key is a mouse button.
 * @param key The key.
 * @returns true for GameKey_MouseLeft through GameKey_MouseBack; false otherwise (including invalid keys).
 */
bool GameKey_IsMouseButton(GameKey key);

/**
 * @brief Returns whether @p key is a keyboard key.
 * @param key The key.
 * @returns true for a real key that is not a mouse button.
 */
bool GameKey_IsKeyboardKey(GameKey key);

/**
 * @brief Returns the modifier flag a key stands for: GAME_KEY_MODIFIER_SHIFT for either Shift key, and so on.
 * @param key The key.
 * @returns The key's GAME_KEY_MODIFIER_* flag, or GAME_KEY_MODIFIER_NONE for a key that is not a modifier.
 */
uint8_t GameKey_GetModifierFlag(GameKey key);

/**
 * @brief Returns the stable, lowercase id string of a key, as stored in controls files.
 * @param key The key.
 * @returns A static NUL-terminated UTF-8 string such as "w", "left_shift" or "mouse_left"; NULL for an invalid
 *          key.
 */
const unsigned char* GameKey_GetIdString(GameKey key);

/**
 * @brief Returns the display name of a key, for controls menus and prompts (English for now).
 * @param key The key.
 * @returns A static NUL-terminated UTF-8 string such as "W", "Left Shift" or "Left Mouse"; NULL for an invalid
 *          key.
 */
const unsigned char* GameKey_GetDisplayName(GameKey key);

/**
 * @brief Finds the key with the given id string (exact, case-sensitive match).
 *
 * A linear search over every key: meant for loading binding files, not for per-tick code.
 * @param idString The id string; NULL never matches.
 * @param outKey [out] Receives the key on success; left unchanged otherwise. Must not be NULL.
 * @returns true if a key has that id string.
 */
bool GameKey_TryParseIdString(const unsigned char* idString, GameKey* outKey);
