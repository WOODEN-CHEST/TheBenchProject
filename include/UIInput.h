#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector2 is part of every input argument (positions are 2D), so the raylib dependency is unavoidable in this public
// header. No raylib input symbols are exposed: keys and buttons are GameKeys.
#include "raylib/raylib.h"
#include "GameKey.h"


/**
 * @file UIInput.h
 * @brief The argument structs handed to a widget's input callbacks.
 *
 * Widgets never read raylib (or GameInput) to react to input: the screen routes the tick's input and calls the
 * widget's hooks with these structs. Keys and mouse buttons are GameKeys (GameKey.h), the game-wide key identity.
 * Grouping each callback's data into a struct means fields can be added later without touching every widget. All
 * positions are normalized [0;1]: screen positions relative to the whole screen (the screen's own surface), widget
 * positions relative to the target widget's box.
 *
 * These are plain value types that own nothing; the screen builds them on the stack for one callback.
 */


// Types.
/**
 * @brief Discriminates the kind of mouse action carried by a WidgetMouseInputArgs.
 */
typedef enum UIMouseInputTypeEnum
{
    /** @brief A mouse button was pressed this tick over the widget (DurationSeconds is 0). */
    UIMouseInputType_ButtonPress,
    /** @brief A mouse button was released this tick; delivered to the widget the press started on. */
    UIMouseInputType_ButtonRelease,
    /** @brief The pointer moved while a button that was pressed on the widget is held (a drag). */
    UIMouseInputType_Move,
    /** @brief The wheel scrolled over the widget (ScrollDelta is set). */
    UIMouseInputType_Scroll
} UIMouseInputType;

/**
 * @brief Discriminates the kind of keyboard action carried by a WidgetKeyboardInputArgs.
 */
typedef enum UIKeyboardInputTypeEnum
{
    /** @brief A key went down this tick (Key is set). */
    UIKeyboardInputType_KeyPress,
    /** @brief A key went up this tick (Key is set). */
    UIKeyboardInputType_KeyRelease,
    /** @brief A held key auto-repeated this tick (Key is set). */
    UIKeyboardInputType_KeyRepeat,
    /** @brief A text codepoint was typed this tick (Codepoint is set). */
    UIKeyboardInputType_Text
} UIKeyboardInputType;

/**
 * @brief The information passed to a widget's OnHoverStart callback.
 *
 * Describes where the pointer is, both relative to the whole screen and relative to the hovered widget's own box.
 */
typedef struct WidgetHoverArgsStruct
{
    /** @brief Pointer position, normalized [0;1] relative to the whole screen. */
    Vector2 ScreenPosition;
    /** @brief Pointer position, normalized [0;1] relative to the target widget's box. */
    Vector2 WidgetPosition;
} WidgetHoverArgs;

/**
 * @brief The information passed to a widget's OnMouseInput callback.
 *
 * One struct covers presses, releases, drags and scrolls (see Type). A release and a drag are delivered to the widget
 * the press STARTED on (so a drag out of the widget still ends there), and ClickStart* / DurationSeconds describe that
 * press.
 */
typedef struct WidgetMouseInputArgsStruct
{
    /** @brief Which kind of mouse action this is. */
    UIMouseInputType Type;
    /** @brief The mouse button involved (a GameKey for which GameKey_IsMouseButton is true); GameKey_Unknown for a
     *  scroll. */
    GameKey Button;
    /** @brief Current pointer position, normalized [0;1] relative to the whole screen. */
    Vector2 ScreenPosition;
    /** @brief Current pointer position, normalized [0;1] relative to the target widget's box. */
    Vector2 WidgetPosition;
    /** @brief Where the press began, normalized [0;1] relative to the screen; equals ScreenPosition for a press. */
    Vector2 ClickStartScreenPosition;
    /** @brief Where the press began, normalized [0;1] relative to the target widget's box. */
    Vector2 ClickStartWidgetPosition;
    /** @brief Seconds since the press began; 0 for a press. */
    double DurationSeconds;
    /** @brief Wheel movement (x, y) for a scroll; zero otherwise. */
    Vector2 ScrollDelta;
} WidgetMouseInputArgs;

/**
 * @brief The information passed to a widget's OnKeyboardInput callback.
 *
 * Covers key state changes (press / release / repeat, in Key) and text entry (a Unicode codepoint, in Codepoint);
 * Type says which fields are meaningful. The focused widget gets every key first, then its ancestors, until one
 * reports it handled.
 */
typedef struct WidgetKeyboardInputArgsStruct
{
    /** @brief Which kind of keyboard action this is. */
    UIKeyboardInputType Type;
    /** @brief The key involved (a keyboard GameKey); GameKey_Unknown for text. */
    GameKey Key;
    /** @brief The GAME_KEY_MODIFIER_* flags held this tick (for shortcuts such as Control+A). */
    uint8_t Modifiers;
    /** @brief The typed Unicode codepoint; meaningful for a Text action. */
    uint32_t Codepoint;
} WidgetKeyboardInputArgs;
