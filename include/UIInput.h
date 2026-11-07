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
 * positions are normalized: screen positions relative to the whole screen (the screen's own surface), widget positions
 * relative to the target widget's box. They are in [0;1] while the pointer is inside; a press held keeps being told where
 * the pointer goes, and then they run past [0;1], also past the game window's edges.
 *
 * These are plain value types that own nothing; the screen builds them on the stack for one callback. The file also
 * holds the two small enums every part of the UI names: a root's layer and the pointer's shape.
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
    /** @brief A press some other widget of the screen held came up this tick with the pointer over this one: the top widget
     *  there gets this, right after the press's widget got its ButtonRelease. ClickStart* and DurationSeconds describe that
     *  press. What a press-drag-release menu and a click that activates on the release stand on. */
    UIMouseInputType_ReleaseOver
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
 * @brief What entered an edit element (WidgetVTable.OnEnter): from then on it hears every key first, until it is left.
 */
typedef enum UIEnterCauseEnum
{
    /** @brief The keyboard: the accept action's key on the focused element, or Tab going on from another element that was
     *  entered. */
    UIEnterCause_Key,
    /** @brief A press of any mouse button on it. */
    UIEnterCause_Press,
    /** @brief Code (UIScreen_EnterWidget): a popup's list, a dialog's first field, the console's line. */
    UIEnterCause_Code
} UIEnterCause;

/**
 * @brief Why an entered element is left (WidgetVTable.OnLeave). What each means is the element's own: a text box goes back
 *        to its text at entering on a cancel, a slider whose presses each committed has nothing to put back.
 */
typedef enum UILeaveReasonEnum
{
    /** @brief What was done stays: the accept action, Tab, focus moving anywhere else, a world screen let go, code. */
    UILeaveReason_Keep,
    /** @brief The back action (Escape): what is not finished is taken back. */
    UILeaveReason_Cancel
} UILeaveReason;

/**
 * @brief Where a root of a screen stands among the others (UIScreen_SetRootLayer): drawing and hit testing go by layer, then by
 *        z, then by insertion. From the bottom up.
 */
typedef enum UIScreenLayerEnum
{
    /** @brief Under everything: a dock's root and the bars. A press does not raise it. */
    UIScreenLayer_Background,
    /** @brief The default. A press raises the root above the others of this layer. */
    UIScreenLayer_Normal,
    /** @brief Floating windows: a press raises the root above the others of this layer. */
    UIScreenLayer_Floating,
    /** @brief A maximized window. A press does not raise it. */
    UIScreenLayer_Maximized,
    /** @brief Popups, above every window. A press does not raise it. */
    UIScreenLayer_Popup,
    /** @brief Tooltips and a drag's preview: on top of everything, taking no input at all. */
    UIScreenLayer_Overlay,
    /** @brief The number of layers. Not a layer. */
    UIScreenLayer_Count
} UIScreenLayer;

/**
 * @brief The shape the pointer shows over a widget (Widget_SetCursorShape).
 */
typedef enum UICursorShapeEnum
{
    /** @brief The normal arrow. */
    UICursorShape_Arrow,
    /** @brief An I-beam: text can be typed or selected here. */
    UICursorShape_IBeam,
    /** @brief Resizing across: a vertical edge. */
    UICursorShape_ResizeHorizontal,
    /** @brief Resizing up and down: a horizontal edge. */
    UICursorShape_ResizeVertical,
    /** @brief Resizing along the diagonal from the top-left to the bottom-right: those two corners. */
    UICursorShape_ResizeDiagonalDown,
    /** @brief Resizing along the diagonal from the bottom-left to the top-right: those two corners. */
    UICursorShape_ResizeDiagonalUp,
    /** @brief Moving in every direction. */
    UICursorShape_Move,
    /** @brief A pointing hand: this can be clicked. */
    UICursorShape_Hand,
    /** @brief This cannot be done here. */
    UICursorShape_NotAllowed,
    /** @brief The number of shapes. Not a shape. */
    UICursorShape_Count
} UICursorShape;

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
 * @brief The information passed to a widget's HitTest callback: a point the screen asks about, and whether it lies in the
 *        widget's box.
 */
typedef struct WidgetHitTestArgsStruct
{
    /** @brief The point as the screen was asked about it, normalized [0;1] relative to the whole screen (where the pointer is),
     *  before any render transform is undone: what a widget standing for a world object casts its ray through. */
    Vector2 ScreenPosition;
    /** @brief The same point in the widget's own box, normalized [0;1] when inside it, with the render transforms of the widget
     *  and its ancestors undone. */
    Vector2 LocalPosition;
    /** @brief Whether the point lies in the widget's box (a box of no size holds none): the answer without a HitTest slot. */
    bool IsInsideBox;
} WidgetHitTestArgs;

/**
 * @brief The information passed to a widget's OnMouseInput callback.
 *
 * One struct covers presses, releases and drags (see Type). A release and a drag are delivered to the widget the press
 * STARTED on or was handed to (UIScreen_TransferPress), wherever the pointer goes, for as long as it stays attached, takes
 * input and shows; a pointer that moved in the release's tick arrives as a drag first. ClickStart*, DurationSeconds and
 * ClickCount describe that press.
 */
typedef struct WidgetMouseInputArgsStruct
{
    /** @brief Which kind of mouse action this is. */
    UIMouseInputType Type;
    /** @brief The mouse button involved (a GameKey for which GameKey_IsMouseButton is true). */
    GameKey Button;
    /** @brief The GAME_KEY_MODIFIER_* flags held this tick. */
    uint8_t Modifiers;
    /** @brief Current pointer position relative to the whole screen; past [0;1] when a held press's pointer left it. */
    Vector2 ScreenPosition;
    /** @brief Current pointer position relative to the target widget's box. */
    Vector2 WidgetPosition;
    /** @brief Where the press began, relative to the screen; equals ScreenPosition for a press. */
    Vector2 ClickStartScreenPosition;
    /** @brief Where the press began, relative to the target widget's box. */
    Vector2 ClickStartWidgetPosition;
    /** @brief Seconds since the press began; 0 for a press. */
    double DurationSeconds;
    /** @brief How far the pointer moved since the last delivery of this press, in surface pixels; zero for a press. During a
     *  hidden-cursor drag (UIScreen_CapturePointer) this is the raw motion, and the positions stand still. */
    Vector2 DeltaPixels;
    /** @brief How many presses of this one button on this one widget came in a row, each soon after the last and near it: 1
     *  for a single one, 2 for a double click. */
    uint32_t ClickCount;
} WidgetMouseInputArgs;

/**
 * @brief The information passed to a widget's OnScrollInput callback.
 *
 * The wheel goes to the widget under the pointer, then up through its ancestors, until one handles it.
 */
typedef struct WidgetScrollInputArgsStruct
{
    /** @brief Wheel movement (x: horizontal, y: vertical), in notches. */
    Vector2 ScrollDelta;
    /** @brief The GAME_KEY_MODIFIER_* flags held this tick. */
    uint8_t Modifiers;
    /** @brief Pointer position relative to the whole screen. */
    Vector2 ScreenPosition;
    /** @brief Pointer position relative to the target widget's box. */
    Vector2 WidgetPosition;
} WidgetScrollInputArgs;

/**
 * @brief The information passed to a widget's OnKeyboardInput callback.
 *
 * Covers key state changes (press / release / repeat, in Key) and text entry (a Unicode codepoint, in Codepoint);
 * Type says which fields are meaningful. The focused widget gets every key first, then its ancestors, until one
 * reports it handled; a focused edit element that is not entered hears only the presses of the short list (reset, copy,
 * paste, the context menu) and is passed over for the rest. A press of the reset, copy or paste action that nobody handled
 * then goes to the hovered widget and its ancestors (IsHoverDelivery).
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
    /** @brief Whether this press reaches the widget because the pointer is over it, not because it has the focus. */
    bool IsHoverDelivery;
} WidgetKeyboardInputArgs;
