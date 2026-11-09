#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRUserData.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIWindow.h
 * @brief The window element (kind window): an icon, a title, a frame that moves and resizes, minimize, maximize and close buttons, and
 *        content calling code fills. It works on any screen: the game window's, or a screen in the world.
 *
 * MAKING ONE. UIKit_CreateWindow makes it whole, as a root in its screen's Floating layer: its frame (a part of the kit's under the rest),
 * its title bar (a part of the kit's that moves it), its caption buttons (kit buttons of the theme's "window_button" and "window_close"
 * variants), eight edges and corners that resize it (parts of the kit's) and its content: a column in a scroll area. Calling code adds its
 * elements to that column (the content's id, which the creation gives) and never touches the frame: it talks to the window by its id
 * (UIWindow_*).
 *
 * WHERE IT IS. Its floating box (a position and a size) is in its screen's UI pixels, the units of a root's placement, and it is the box
 * the window goes back to. What shows is that box grown to the window's least size and kept reachable on the workspace
 * (UIScreen_GetWorkspaceBox): the title bar's whole height on it, and at least UI_WINDOW_REACHABLE_WIDTH of the title bar's draggable
 * part (the title bar less its buttons). So a game window that shrinks and grows again puts the window back where it was. Dragging the
 * title bar moves it; its edges and corners resize it, within its least size and the workspace, the pointer's shape showing which way. A
 * double click on the title bar maximizes it or restores it.
 *
 * ITS LEAST SIZE, in its own UI pixels (its scale multiplies it), is the largest of the theme's "min_size", its own
 * (UIWindow_SetMinimumSize), what its title bar needs, and, per axis that fits (UIWindowFit_Fit), what its content needs: across, the
 * content's width measured with no room at all (as narrow as it can be); down, its height at the window's width. An axis that scrolls
 * (UIWindowFit_Scroll) gives the content its preferred size, with a bar while that is larger than the window. The default fits across
 * and scrolls down: an inspector's shape.
 *
 * MINIMIZED, its content is hidden (keeping its state) and its frame is only a title bar the theme's "minimized_width" wide (or what the
 * title bar needs, when that is more), where it was: the icon, as much of the title as fits, restore and close. Restoring gives the same
 * size back. MAXIMIZED, it stands in the screen's Maximized layer and fills the workspace; restoring goes back to its floating box. One
 * window of a screen is maximized at a time: maximizing another restores the first, and so does giving another the focus
 * (UIWindow_Focus). A maximized window minimized shows as a title bar, and restores to maximized. Only a window that is a root of its
 * screen minimizes or maximizes: one another widget places has no such buttons.
 *
 * CLOSING (its close button while it can close, or UIWindow_Close) asks its close query first when it has one: unsaved work can say no,
 * open a dialog, and close the window later with UIWindow_CloseNow. A closed window is destroyed. One opened through its type
 * (UIWindowTypes.h) leaves its floating box with the type, which opens the next one there.
 *
 * FOCUS. A press anywhere in it raises it within its layer, as a press raises any root. It is a focusable group: keyboard navigation
 * reaches it as one, and Enter goes in. A press on its title bar, its caption buttons, its edges or its frame acts on nothing inside it:
 * the focus stays where it is when it is inside the window already, and otherwise (from elsewhere, or on the window itself) goes back to
 * what last had it inside, entered again if it was (a text box being typed in, on a screen that has the keyboard), else to the window's
 * own focus target (UIWindow_SetDefaultFocus, entered if asked: a world view), else to the window itself. Restored from minimized by its
 * button or a double click, it does so again once its content shows. The window is active while the focus is in it, or in a popup whose
 * owner is in it (a list or a menu it opened: UIScreen_GetPopupOwner): its look shows so.
 *
 * Its look (UIKit_RegisterLook: a struct starting with UIWindowLookBase) draws the frame, the title bar with the icon and the title, the
 * content's background and an edge's highlight, tells the behaviour the theme's metrics (UIWindowCapability_SetMetrics) and measures
 * through it (UIWindow_Measure: its least size).
 */


// Forward declarations.
/** @brief The registry of window types; full type in UIWindowTypes.h. */
typedef struct UIWindowTypesStruct UIWindowTypes;
/** @brief The widget factory; full type in UIWidgetFactory.h. */
typedef struct UIWidgetFactoryStruct UIWidgetFactory;


// Macros.
/** @brief How much of a window's title bar, at least, stays on the workspace across, in the window's own UI pixels: of its draggable part,
 *  so it can always be dragged back. */
#define UI_WINDOW_REACHABLE_WIDTH 48.0f
/** @brief The least title bar height a window takes, in its own UI pixels, whatever its theme says: there is always a title bar to drag. */
#define UI_WINDOW_MIN_TITLE_HEIGHT 8.0f
/** @brief The thickest an edge's band is, as a share of the title bar's height: most of the title bar is left to drag by. */
#define UI_WINDOW_MAX_EDGE_SHARE_OF_TITLE 0.25f
/** @brief The placeholder letters of the caption buttons, until icon art exists: minimize, maximize, restore and close. */
#define UI_WINDOW_MINIMIZE_LETTER ((uint32_t)'_')
#define UI_WINDOW_MAXIMIZE_LETTER ((uint32_t)'+')
#define UI_WINDOW_RESTORE_LETTER ((uint32_t)'=')
#define UI_WINDOW_CLOSE_LETTER ((uint32_t)0xD7U)


// Types.
/**
 * @brief How a window's content takes one axis.
 */
typedef enum UIWindowFitEnum
{
    /** @brief The content gets the window's size, its elements resizing to fit, and the window is never smaller than the content needs. */
    UIWindowFit_Fit,
    /** @brief The content gets its preferred size, with a scroll bar while that is larger than the window. */
    UIWindowFit_Scroll
} UIWindowFit;

/**
 * @brief One of the edges and corners a window is resized by.
 */
typedef enum UIWindowEdgeEnum
{
    /** @brief The left edge. */
    UIWindowEdge_Left,
    /** @brief The top edge. */
    UIWindowEdge_Top,
    /** @brief The right edge. */
    UIWindowEdge_Right,
    /** @brief The bottom edge. */
    UIWindowEdge_Bottom,
    /** @brief The top-left corner. */
    UIWindowEdge_TopLeft,
    /** @brief The top-right corner. */
    UIWindowEdge_TopRight,
    /** @brief The bottom-left corner. */
    UIWindowEdge_BottomLeft,
    /** @brief The bottom-right corner. */
    UIWindowEdge_BottomRight,
    /** @brief The number of edges and corners. Not one. */
    UIWindowEdge_Count
} UIWindowEdge;

/**
 * @brief One of a window's caption buttons.
 */
typedef enum UIWindowButtonEnum
{
    /** @brief Minimizes it; restores it while it is minimized. */
    UIWindowButton_Minimize,
    /** @brief Maximizes it; restores it while it is maximized. */
    UIWindowButton_Maximize,
    /** @brief Closes it. */
    UIWindowButton_Close,
    /** @brief The number of caption buttons. Not one. */
    UIWindowButton_Count
} UIWindowButton;

/**
 * @brief What a held press on a window's frame does.
 */
typedef enum UIWindowGrabEnum
{
    /** @brief Nothing: no press, or one that moves nothing (a maximized window's title bar). */
    UIWindowGrab_None,
    /** @brief The title bar's: it moves the window. */
    UIWindowGrab_Move,
    /** @brief An edge's or a corner's: it resizes the window. */
    UIWindowGrab_Resize
} UIWindowGrab;

/**
 * @brief Asks whether a window may close now (unsaved work). Called when its close button is activated or UIWindow_Close asks, during the
 *        screen's update as a hook is: it may open a dialog, destroy widgets and move the focus, and close the window later with
 *        UIWindow_CloseNow.
 * @param screen The window's screen.
 * @param windowId The window.
 * @param userData What was given with the query (its stored copy).
 * @returns true to close it now, false to keep it.
 */
typedef bool (*UIWindowCloseQuery)(UIScreen* screen, uint64_t windowId, const UserData* userData);

/**
 * @brief What a window is created with (UIKit_CreateWindow). A plain value; build one with UIWindowOptions_Create.
 */
typedef struct UIWindowOptionsStruct
{
    /** @brief What every element is created with: its variant, look and scale. Its placement and stack hint are not used: a window is placed
     *  by its own box. */
    UIElementOptions Element;
    /** @brief The title, UTF-8 (copied), or NULL for none. */
    const unsigned char* Title;
    /** @brief The icon before the title; none by default. Copied. */
    UIElementIcon Icon;
    /** @brief Whether Position places it; without it, it opens centred on the workspace. */
    bool HasPosition;
    /** @brief Its top-left, in the screen's UI pixels, when HasPosition; each finite. */
    Vector2 Position;
    /** @brief Its size, in the screen's UI pixels; each finite and above 0. */
    Vector2 Size;
    /** @brief Its least size, in its own UI pixels; each finite, >= 0 and at most UI_LAYOUT_MAX_LENGTH. */
    Vector2 MinimumSize;
    /** @brief How its content takes the width. */
    UIWindowFit FitAcross;
    /** @brief How its content takes the height. */
    UIWindowFit FitDown;
    /** @brief Whether its edges resize it. */
    bool IsResizable;
    /** @brief Whether it has a minimize button. */
    bool CanMinimize;
    /** @brief Whether it has a maximize button (and a double click on its title bar maximizes it). */
    bool CanMaximize;
    /** @brief Whether it has a close button. */
    bool CanClose;
    /** @brief Asks before it closes; NULL for none. */
    UIWindowCloseQuery CloseQuery;
    /** @brief Handed to the close query; copied. */
    UserData CloseData;
} UIWindowOptions;

/**
 * @brief What a window look's Construct gets (UIKit_CreateWindow passes it): the kit and the options.
 */
typedef struct UIWindowArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIWindowOptions* Options;
} UIWindowArgs;

/**
 * @brief The theme's sizes a window lays itself out by, in its own UI pixels; its look tells them (UIWindowCapability_SetMetrics).
 */
typedef struct UIWindowMetricsStruct
{
    /** @brief The title bar's height. */
    float TitleHeight;
    /** @brief From the title bar's left to the icon, and from its right to the last caption button. */
    float TitlePadding;
    /** @brief Between the icon and the title. */
    float IconSpacing;
    /** @brief The icon's width as the look draws it; 0 without an icon. */
    float IconWidth;
    /** @brief A caption button's size. */
    Vector2 ButtonSize;
    /** @brief Between two caption buttons, and between the title and the first of them. */
    float ButtonSpacing;
    /** @brief The frame around the content, at its left, right and bottom. */
    float Border;
    /** @brief How far in from each edge a press resizes it (at most UI_WINDOW_MAX_EDGE_SHARE_OF_TITLE of the title bar's height). */
    float ResizeMargin;
    /** @brief Its least size whatever it holds. */
    Vector2 MinSize;
    /** @brief A minimized window's width. */
    float MinimizedWidth;
    /** @brief Inside the content's column. */
    Vector2 ContentPadding;
    /** @brief Between the content's elements. */
    float ContentSpacing;
} UIWindowMetrics;

/**
 * @brief A window's capability: its content, its box, its states, its parts and what its frame is doing, no look. Embedded by a look's
 *        widget type right after its Widget (UIWindowLookBase); its fields are read-only outside the window's module.
 */
typedef struct UIWindowCapabilityStruct
{
    /** @brief The title, as plain content. */
    UITextContent _title;
    /** @brief The icon. */
    UIElementIcon _icon;
    /** @brief The floating box as wanted, in the screen's UI pixels: what it goes back to. */
    Rectangle _floatingBox;
    /** @brief What its last update placed, in the screen's UI pixels (for a window another widget places, what its last layout gave it). */
    Rectangle _shownBox;
    /** @brief Its own least size, in its own UI pixels. */
    Vector2 _minimumSize;
    /** @brief How its content takes each axis. */
    UIWindowFit _fitAcross;
    UIWindowFit _fitDown;
    /** @brief What the user may do to it. */
    bool _isResizable;
    bool _canMinimize;
    bool _canMaximize;
    bool _canClose;
    /** @brief Its states: both can hold (a maximized window minimized). */
    bool _isMinimized;
    bool _isMaximized;
    /** @brief Whether the focus is in it, or in a popup whose owner is in it. */
    bool _isActive;
    /** @brief What last had the focus inside it and whether it was entered then; WIDGET_ID_INVALID for nothing yet. */
    uint64_t _lastFocusId;
    bool _wasLastFocusEntered;
    /** @brief Where the focus goes when nothing inside had it yet, and whether it is entered then; WIDGET_ID_INVALID for none. */
    uint64_t _defaultFocusId;
    bool _isDefaultFocusEntered;
    /** @brief Asked before it closes, with its data; NULL for none. */
    UIWindowCloseQuery _closeQuery;
    UserData _closeData;
    /** @brief Its parts, made with it; WIDGET_ID_INVALID until then. */
    uint64_t _frameId;
    uint64_t _titleBarId;
    uint64_t _buttonIds[UIWindowButton_Count];
    uint64_t _areaId;
    uint64_t _contentId;
    uint64_t _edgeIds[UIWindowEdge_Count];
    /** @brief Where the shown caption buttons start, from its left, in its own UI pixels: where the title ends. */
    float _buttonsStart;
    /** @brief What the held press on its frame does: moving or resizing (by which edge), where the pointer and the shown box were when it
     *  began (the screen's UI pixels), and its button. */
    UIWindowGrab _grab;
    UIWindowEdge _grabEdge;
    Vector2 _grabStart;
    Rectangle _grabBox;
    GameKey _grabButton;
    /** @brief Whether the held press on its title bar is a double click's second: its release maximizes or restores the window, so the
     *  release still lands on the title bar. */
    bool _isToggleOnRelease;
    /** @brief The registry of the type it was opened as, borrowed (it outlives the window), or NULL; and the type's index there. */
    UIWindowTypes* _types;
    uint32_t _typeIndex;
    /** @brief The sizes it lays itself out by. */
    UIWindowMetrics _metrics;
} UIWindowCapability;

/**
 * @brief How every window look's widget struct starts: the widget, then the capability.
 */
typedef struct UIWindowLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The window's behaviour. */
    UIWindowCapability Window;
} UIWindowLookBase;


// Helpers.
/**
 * @brief Default options: a title, centred on the workspace, 360 x 260 UI pixels, no least size of its own, fitting across and scrolling
 *        down, resizable, with every caption button and no close query.
 * @param title The title (borrowed until the window is made), or NULL.
 * @returns The options.
 */
static inline UIWindowOptions UIWindowOptions_Create(const unsigned char* title)
{
    UIWindowOptions Options =
    {
        .Element = UIElementOptions_Create(),
        .Title = title,
        .HasPosition = false,
        .Position = { .x = 0.0f, .y = 0.0f },
        .Size = { .x = 360.0f, .y = 260.0f },
        .MinimumSize = { .x = 0.0f, .y = 0.0f },
        .FitAcross = UIWindowFit_Fit,
        .FitDown = UIWindowFit_Scroll,
        .IsResizable = true,
        .CanMinimize = true,
        .CanMaximize = true,
        .CanClose = true,
        .CloseQuery = NULL,
        .CloseData = UserData_CreateEmpty()
    };
    Options.Icon = UIElementIcon_None();
    return Options;
}

/**
 * @brief Returns whether a window's options can be made: a title of valid UTF-8, a valid icon, a size above 0, a least size >= 0 and a
 *        position, all finite, and fits that are known.
 * @param options The options; must not be NULL.
 * @returns true if valid.
 */
bool UIWindowOptions_IsValid(const UIWindowOptions* options);


// Calling code: by widget id.
/**
 * @brief Reads the column calling code adds a window's content to.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param outContentId [out] Receives the column (a stack). Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_GetContentId(UIScreen* screen, uint64_t windowId, uint64_t* outContentId);

/**
 * @brief Returns whether a widget is a window of the kit's.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 * @returns true for a live window.
 */
bool UIWindow_IsWindow(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Changes a window's title.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param title The title as UTF-8; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; otherwise the text's error.
 */
Error UIWindow_SetTitle(UIScreen* screen, uint64_t windowId, const unsigned char* title);

/**
 * @brief Reads a window's title.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param outTitle [out] Receives the title, valid until it changes or the window goes. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_GetTitle(UIScreen* screen, uint64_t windowId, const unsigned char** outTitle);

/**
 * @brief Changes a window's icon.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param icon The icon; must not be NULL. Copied.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; ErrorCode_ArgumentOutOfRange for an icon that is
 *          not valid.
 */
Error UIWindow_SetIcon(UIScreen* screen, uint64_t windowId, const UIElementIcon* icon);

/**
 * @brief Sets a window's floating box: where it goes back to, kept reachable on the workspace from its next update on.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param box Its top-left and size in the screen's UI pixels; finite, the size above 0.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; ErrorCode_ArgumentOutOfRange for a box that is
 *          not valid.
 */
Error UIWindow_SetBox(UIScreen* screen, uint64_t windowId, Rectangle box);

/**
 * @brief Reads a window's floating box: what it goes back to when it is restored, or when the workspace grows again.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param outBox [out] Receives it, in the screen's UI pixels. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_GetBox(UIScreen* screen, uint64_t windowId, Rectangle* outBox);

/**
 * @brief Reads where a window shows, as its last update placed it: its floating box grown to its least size and kept reachable, the
 *        workspace while it is maximized, its title bar while it is minimized; for a window another widget places, the box its last
 *        layout gave it.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param outBox [out] Receives it, in the screen's UI pixels. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_GetShownBox(UIScreen* screen, uint64_t windowId, Rectangle* outBox);

/**
 * @brief Sets a window's own least size.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param size In its own UI pixels; each finite, >= 0 and at most UI_LAYOUT_MAX_LENGTH.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; ErrorCode_ArgumentOutOfRange for a size that is
 *          not valid.
 */
Error UIWindow_SetMinimumSize(UIScreen* screen, uint64_t windowId, Vector2 size);

/**
 * @brief Sets how a window's content takes each axis.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param across The width's.
 * @param down The height's.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; ErrorCode_ArgumentOutOfRange for an unknown fit.
 */
Error UIWindow_SetFit(UIScreen* screen, uint64_t windowId, UIWindowFit across, UIWindowFit down);

/**
 * @brief Sets whether a window's edges resize it.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param isResizable The new flag.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_SetIsResizable(UIScreen* screen, uint64_t windowId, bool isResizable);

/**
 * @brief Sets which caption buttons a window has. Code may still minimize and maximize it (when it is a root of its screen) and close it.
 *        A window another widget places shows neither minimize nor maximize; a minimized window always shows the button that restores it.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param canMinimize Whether it has a minimize button.
 * @param canMaximize Whether it has a maximize button (and a double click on its title bar maximizes it).
 * @param canClose Whether it has a close button.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_SetButtons(UIScreen* screen, uint64_t windowId, bool canMinimize, bool canMaximize, bool canClose);

/**
 * @brief Minimizes a window: its content hides (the focus in it is lost, its state is kept) and its frame becomes a title bar. Nothing
 *        happens to one minimized already. A drag of it ends.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no window, or a window that is not a root of its screen
 *          (another widget places it); otherwise a hook's error.
 */
Error UIWindow_Minimize(UIScreen* screen, uint64_t windowId);

/**
 * @brief Maximizes a window: it fills the workspace in the Maximized layer, and a window of the screen maximized before is restored. A
 *        minimized window is restored too. A drag of it ends.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no window, or a window that is not a root of its screen;
 *          otherwise a hook's error.
 */
Error UIWindow_Maximize(UIScreen* screen, uint64_t windowId);

/**
 * @brief Restores a window: a minimized one to what it was (maximized or floating), a maximized one to its floating box, on top of the
 *        floating windows. Nothing happens to a floating one.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; otherwise a hook's error.
 */
Error UIWindow_Restore(UIScreen* screen, uint64_t windowId);

/**
 * @brief Reads a window's states.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param outIsMinimized [out] Receives whether it is minimized. May be NULL.
 * @param outIsMaximized [out] Receives whether it is maximized (also while it is minimized from there). May be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_GetState(UIScreen* screen, uint64_t windowId, bool* outIsMinimized, bool* outIsMaximized);

/**
 * @brief Closes a window as its close button does: its close query is asked first, if it has one; then it is destroyed (and its type, if
 *        it was opened as one, remembers its floating box).
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param outIsClosed [out] Receives whether it closed: also when its query closed it itself. May be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; otherwise the destruction's error.
 */
Error UIWindow_Close(UIScreen* screen, uint64_t windowId, bool* outIsClosed);

/**
 * @brief Closes a window without asking its close query: what a query's dialog calls once the user chose to close.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; otherwise the destruction's error.
 */
Error UIWindow_CloseNow(UIScreen* screen, uint64_t windowId);

/**
 * @brief Sets what a window asks before it closes.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param query The query, or NULL for none.
 * @param userData Handed to it; copied. May be NULL for none.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_SetCloseQuery(UIScreen* screen, uint64_t windowId, UIWindowCloseQuery query, const UserData* userData);

/**
 * @brief Brings a window to the front of its layer and gives it the focus as a press on its title bar does: nothing changes when the
 *        focus is inside it already; otherwise what last had the focus inside it gets it back (entered again if it was), else its default
 *        focus target, else the window itself. A window of the screen maximized over it (not minimized) is restored first, so it shows.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; otherwise a hook's error.
 */
Error UIWindow_Focus(UIScreen* screen, uint64_t windowId);

/**
 * @brief Sets where the focus goes when a window gets it and nothing inside it had it yet (UIWindow_Focus, a press on its title bar).
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param focusId A focusable widget inside the window, or WIDGET_ID_INVALID for none (the window itself takes the focus then).
 * @param isEntered Whether it is entered as it gets the focus (an edit element: a world view).
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no window, or a focus target that is not inside it.
 */
Error UIWindow_SetDefaultFocus(UIScreen* screen, uint64_t windowId, uint64_t focusId, bool isEntered);

/**
 * @brief Reads whether a window is active: the focus is in it, or in a popup whose owner is in it (a list or a menu it opened, or a
 *        popup opened from one of those), as its last update saw.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param outIsActive [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_IsActive(UIScreen* screen, uint64_t windowId, bool* outIsActive);

/**
 * @brief Reads one of a window's caption buttons, a button of the kit (hidden while the window does not offer it).
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param button Which one.
 * @param outButtonId [out] Receives the button. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window; ErrorCode_ArgumentOutOfRange for an unknown
 *          button.
 */
Error UIWindow_GetButtonId(UIScreen* screen, uint64_t windowId, UIWindowButton button, uint64_t* outButtonId);

/**
 * @brief Reads which type a window was opened as (UIWindowTypes_Open).
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param outTypes [out] Receives the registry, or NULL for a window opened as no type. Must not be NULL.
 * @param outIndex [out] Receives the type's index there. May be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_GetType(UIScreen* screen, uint64_t windowId, UIWindowTypes** outTypes, uint32_t* outIndex);

/**
 * @brief Records which type a window was opened as, so it leaves its floating box with that type when it closes. UIWindowTypes_Open
 *        calls it.
 * @param screen The window's screen; must not be NULL.
 * @param windowId The window.
 * @param types The registry; borrowed, it must outlive the window. NULL for none.
 * @param index The type's index there.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no window.
 */
Error UIWindow_SetType(UIScreen* screen, uint64_t windowId, UIWindowTypes* types, uint32_t index);


// Behaviour helpers, for looks and the kit.
/**
 * @brief Tells the behaviour the theme's sizes it lays the window out by; a look calls it from its Update. Values that are not finite numbers
 *        >= 0 count as 0; the title bar is at least UI_WINDOW_MIN_TITLE_HEIGHT high and the resize margin at most
 *        UI_WINDOW_MAX_EDGE_SHARE_OF_TITLE of it.
 * @param self The capability; must not be NULL.
 * @param widget The window's widget; must not be NULL.
 * @param metrics The sizes; must not be NULL.
 */
void UIWindowCapability_SetMetrics(UIWindowCapability* self, Widget* widget, const UIWindowMetrics* metrics);

/**
 * @brief Measures a window as its look would: its least size (above), in its own UI pixels, with its content's height at @p availableWidth
 *        where the height fits.
 * @param widget The window's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width it has, in its own UI pixels.
 * @param outSize [out] Receives the size. Must not be NULL.
 * @returns Success, or a part's Measure error.
 */
Error UIWindow_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize);

/**
 * @brief Returns whether a window shows as active (UIWindow_IsActive).
 * @param self The capability; must not be NULL.
 * @returns true while it is active.
 */
static inline bool UIWindowCapability_IsActive(const UIWindowCapability* self)
{
    return self->_isActive;
}

/**
 * @brief Returns whether a window is minimized, for its look: it draws only its title bar then.
 * @param self The capability; must not be NULL.
 * @returns true while minimized.
 */
static inline bool UIWindowCapability_IsMinimized(const UIWindowCapability* self)
{
    return self->_isMinimized;
}

/**
 * @brief Returns where a window's title may run to, for its look: the shown caption buttons' start less the spacing, from its left, in its
 *        own UI pixels, as its last layout put them.
 * @param self The capability; must not be NULL.
 * @returns The end.
 */
static inline float UIWindowCapability_GetTitleEnd(const UIWindowCapability* self)
{
    return self->_buttonsStart - self->_metrics.ButtonSpacing;
}

/**
 * @brief Returns the state an edge's highlight shows, for its look: pressed while it is dragged, hovered while the pointer is on it, normal
 *        otherwise (when a look draws nothing), and normal for an edge that does not show (a window that is not resizable, minimized or
 *        maximized).
 * @param widget The window's widget; must not be NULL.
 * @param edge The edge.
 * @param outBox [out] Receives its band in the window's box, in its own UI pixels from its top-left; an empty box at the origin for an
 *        edge that does not show. Must not be NULL.
 * @returns The state.
 */
UIElementState UIWindow_GetEdgeState(const Widget* widget, UIWindowEdge edge, Rectangle* outBox);

/**
 * @brief Registers the kit's own widget types a window's frame is made of, the frame, the title bar and an edge, into the kit's records.
 *        UIKit_Construct calls it.
 * @param kit The kit; must not be NULL.
 * @param factory The kit's factory; must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIWindow_RegisterTypes(UIKit* kit, UIWidgetFactory* factory);


// Pure arithmetic.
/**
 * @brief Keeps a window's box reachable on a workspace: its title bar's whole height on it (at its top when it is shorter than the title
 *        bar), and at least @p reachableWidth of its draggable part across (the title bar less @p buttonsWidth at its right), or all of that
 *        part when it or the workspace is narrower. Only moves the box. Pure; every length in one unit.
 * @param box The box.
 * @param workspace The workspace.
 * @param titleHeight The title bar's height.
 * @param buttonsWidth How much of the title bar's right end its buttons take.
 * @param reachableWidth How much of the rest must stay on the workspace.
 * @returns The box, moved as little as it takes.
 */
Rectangle UIWindow_KeepReachable(Rectangle box, Rectangle workspace, float titleHeight, float buttonsWidth, float reachableWidth);

/**
 * @brief Resizes a box by one of its edges or corners: the edges it names move by @p delta, each kept from passing the workspace's edge
 *        (or the place it began at, when that lies beyond) and from leaving less than @p minimumSize; a top edge also stays a title bar's
 *        height above the workspace's bottom. Pure; every length in one unit.
 * @param box The box at the drag's start.
 * @param edge The edge or corner dragged.
 * @param delta How far the pointer went since the drag began.
 * @param minimumSize The least size.
 * @param workspace The workspace.
 * @param titleHeight The title bar's height.
 * @returns The box.
 */
Rectangle UIWindow_ResizeBox(Rectangle box, UIWindowEdge edge, Vector2 delta, Vector2 minimumSize, Rectangle workspace, float titleHeight);
