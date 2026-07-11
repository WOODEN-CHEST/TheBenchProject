#pragma once
#include <stdint.h>
// Vector2 is part of every input argument (positions are 2D), so the raylib dependency is unavoidable
// in this public header. No raylib input symbols are exposed: keys/buttons use the UI enums below.
#include "raylib/raylib.h"


/**
 * @file UIInput.h
 * @brief Input value types shared by the widget and the screen: key/button identities and the argument
 *        structs handed to a widget's input callbacks.
 *
 * The UI framework never lets widgets call raylib input functions directly; instead the screen polls
 * input, translates raylib's keys/buttons into the raylib-free UIKey / UIMouseButton enums here, and
 * delivers the results to widgets through the argument structs (WidgetHoverArgs, WidgetMouseInputArgs,
 * WidgetKeyboardInputArgs). Grouping each callback's data into a struct means new fields can be added
 * later without touching every call site. All positions are normalized [0;1]: screen positions relative
 * to the whole screen, widget positions relative to the target widget's own box.
 *
 * These are plain value types that own nothing; they are created on the stack by the screen for the
 * duration of a single callback and need no construction or destruction.
 */


// Types.
/**
 * @brief A keyboard key identity, decoupled from raylib's key constants.
 *
 * The screen maps raylib's KeyboardKey values onto these; any key the framework does not model maps to
 * UIKey_Unknown. The set covers navigation/control keys, modifiers, letters, digits and the function
 * keys — enough for the framework's own keyboard navigation and for early widgets — and can be extended
 * as needed.
 */
typedef enum UIKeyEnum
{
    /** @brief No key, or a raylib key the framework does not model. */
    UIKey_Unknown = 0,

    /** @brief The Tab key (advances keyboard navigation to the next widget). */
    UIKey_Tab,
    /** @brief The Enter/Return key (tabs into the navigated widget). */
    UIKey_Enter,
    /** @brief The Escape key (tabs out of a widget, or stops navigation at the top level). */
    UIKey_Escape,
    /** @brief The spacebar. */
    UIKey_Space,
    /** @brief The Backspace key. */
    UIKey_Backspace,
    /** @brief The Delete key. */
    UIKey_Delete,

    /** @brief The Up arrow (navigates to the nearest widget above). */
    UIKey_Up,
    /** @brief The Down arrow (navigates to the nearest widget below). */
    UIKey_Down,
    /** @brief The Left arrow (navigates to the nearest widget to the left). */
    UIKey_Left,
    /** @brief The Right arrow (navigates to the nearest widget to the right). */
    UIKey_Right,

    /** @brief The Home key. */
    UIKey_Home,
    /** @brief The End key. */
    UIKey_End,
    /** @brief The Page Up key. */
    UIKey_PageUp,
    /** @brief The Page Down key. */
    UIKey_PageDown,
    /** @brief The Insert key. */
    UIKey_Insert,

    /** @brief The left Shift modifier. */
    UIKey_LeftShift,
    /** @brief The right Shift modifier. */
    UIKey_RightShift,
    /** @brief The left Control modifier. */
    UIKey_LeftControl,
    /** @brief The right Control modifier. */
    UIKey_RightControl,
    /** @brief The left Alt modifier. */
    UIKey_LeftAlt,
    /** @brief The right Alt modifier. */
    UIKey_RightAlt,

    // Letter keys A-Z (self-evident; one constant per Latin letter).
    UIKey_A, UIKey_B, UIKey_C, UIKey_D, UIKey_E, UIKey_F, UIKey_G, UIKey_H, UIKey_I,
    UIKey_J, UIKey_K, UIKey_L, UIKey_M, UIKey_N, UIKey_O, UIKey_P, UIKey_Q, UIKey_R,
    UIKey_S, UIKey_T, UIKey_U, UIKey_V, UIKey_W, UIKey_X, UIKey_Y, UIKey_Z,

    // Top-row digit keys 0-9 (self-evident; one constant per digit).
    UIKey_0, UIKey_1, UIKey_2, UIKey_3, UIKey_4, UIKey_5, UIKey_6, UIKey_7, UIKey_8, UIKey_9,

    // Function keys F1-F12 (self-evident; one constant per function key).
    UIKey_F1, UIKey_F2, UIKey_F3, UIKey_F4, UIKey_F5, UIKey_F6,
    UIKey_F7, UIKey_F8, UIKey_F9, UIKey_F10, UIKey_F11, UIKey_F12,

    /** @brief One past the last valid key; equals the number of modelled keys. Not a real key. */
    UIKey_Count
} UIKey;

/**
 * @brief A mouse button identity, decoupled from raylib's button constants.
 *
 * The screen maps raylib's MouseButton values onto these. Ordering mirrors raylib's so the mapping is a
 * direct correspondence.
 */
typedef enum UIMouseButtonEnum
{
    /** @brief The primary (left) button. */
    UIMouseButton_Left = 0,
    /** @brief The secondary (right) button. */
    UIMouseButton_Right,
    /** @brief The middle button (wheel press). */
    UIMouseButton_Middle,
    /** @brief The first side button. */
    UIMouseButton_Side,
    /** @brief The second side/extra button. */
    UIMouseButton_Extra,
    /** @brief The forward button. */
    UIMouseButton_Forward,
    /** @brief The back button. */
    UIMouseButton_Back,

    /** @brief One past the last valid button; equals the number of modelled buttons. Not a real button. */
    UIMouseButton_Count
} UIMouseButton;

/**
 * @brief Discriminates the kind of mouse action carried by a WidgetMouseInputArgs.
 */
typedef enum UIMouseInputTypeEnum
{
    /** @brief A mouse button was pressed this tick (DurationSeconds is 0). */
    UIMouseInputType_ButtonPress,
    /** @brief A mouse button was released this tick; delivered to the widget the press started on. */
    UIMouseInputType_ButtonRelease,
    /** @brief The mouse moved while a button was held (a drag) over the target widget. */
    UIMouseInputType_Move,
    /** @brief The mouse wheel scrolled over the target widget (ScrollDelta is set). */
    UIMouseInputType_Scroll
} UIMouseInputType;

/**
 * @brief Discriminates the kind of keyboard action carried by a WidgetKeyboardInputArgs.
 */
typedef enum UIKeyboardInputTypeEnum
{
    /** @brief A key transitioned to pressed this tick (Key is set). */
    UIKeyboardInputType_KeyPress,
    /** @brief A key transitioned to released this tick (Key is set). */
    UIKeyboardInputType_KeyRelease,
    /** @brief A held key auto-repeated this tick (Key is set). */
    UIKeyboardInputType_KeyRepeat,
    /** @brief A text codepoint was entered this tick (Codepoint is set). */
    UIKeyboardInputType_Text
} UIKeyboardInputType;


/**
 * @brief The information passed to a widget's hover callbacks (OnHoverStart / OnHoverEnd).
 *
 * Describes where the pointer is, both relative to the whole screen and relative to the hovered widget's
 * own box. All coordinates are normalized [0;1].
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
 * A single struct covers presses, releases, drags and scrolls (see Type). For a release, the args are
 * delivered to the widget the press STARTED on (so a drag out of the widget still ends there), and
 * ClickStart* / DurationSeconds describe that press. All positions are normalized [0;1].
 */
typedef struct WidgetMouseInputArgsStruct
{
    /** @brief Which kind of mouse action this is. */
    UIMouseInputType Type;
    /** @brief The button involved; meaningful for ButtonPress and ButtonRelease. */
    UIMouseButton Button;
    /** @brief Current pointer position, normalized [0;1] relative to the whole screen. */
    Vector2 ScreenPosition;
    /** @brief Current pointer position, normalized [0;1] relative to the target widget's box. */
    Vector2 WidgetPosition;
    /** @brief Where the active press began, normalized [0;1] relative to the screen; equals ScreenPosition for a fresh press. */
    Vector2 ClickStartScreenPosition;
    /** @brief Where the active press began, normalized [0;1] relative to the target widget's box. */
    Vector2 ClickStartWidgetPosition;
    /** @brief Seconds elapsed since the active press began; 0 for a fresh press, larger for a release/drag. */
    double DurationSeconds;
    /** @brief Wheel scroll delta (x, y) for a Scroll action; zero otherwise. */
    Vector2 ScrollDelta;
} WidgetMouseInputArgs;

/**
 * @brief The information passed to a widget's OnKeyboardInput callback.
 *
 * Covers key state changes (press/release/repeat, in Key) and text entry (a Unicode codepoint, in
 * Codepoint); Type says which fields are meaningful.
 */
typedef struct WidgetKeyboardInputArgsStruct
{
    /** @brief Which kind of keyboard action this is. */
    UIKeyboardInputType Type;
    /** @brief The key involved; meaningful for KeyPress, KeyRelease and KeyRepeat. */
    UIKey Key;
    /** @brief The entered Unicode codepoint; meaningful for a Text action. */
    uint32_t Codepoint;
} WidgetKeyboardInputArgs;
