#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wr/WRError.h"
// GenericBuffer backs the screen's registries and queues and WRBufferPool its keyframe storage (both held by value);
// UserData is stored per subscription; Vector2 (positions), RenderContext (render target), ProgramTime (update time),
// GameInput (the tick's input) and the UI animation types are part of this public API. All are unavoidable here.
#include "wr/WRMemory.h"
#include "wr/WRBufferPool.h"
#include "wr/WRUserData.h"
#include "raylib/raylib.h"
#include "Renderer.h"
#include "ProgramTime.h"
#include "GameInput.h"
#include "UIAnimation.h"
#include "UIDragDrop.h"
#include "UIInput.h"
#include "UIPlacement.h"
#include "UIWidgetFactory.h"


/**
 * @file UIScreen.h
 * @brief A UI context: its widgets, focus, keyboard navigation, animations and notifications, on any surface.
 *
 * A UIScreen is one self-contained UI on one surface: the window, or a texture shown in the world (a paper on a desk,
 * an office computer's screen). It knows nothing about the window: each tick its host passes the input
 * (UIScreenInput: the tick's GameInput, where the pointer is in the screen's own [0;1] space, if anywhere, and whether
 * the screen has the keyboard) and keeps its viewport (UIViewport: the surface's size in pixels and the fitted-area
 * settings) up to date, then draws it into a render context for that surface. UIScreenInput_CreateFromWindow and
 * UIViewport_Create cover the full-window case.
 *
 * The caller drives it: UIScreen_Update routes input, advances animations, updates and lays out the widgets, delivers
 * the tick's notifications and frees destroyed widgets; UIScreen_Render draws the tree (each parent under its children,
 * siblings by z then insertion) and the keyboard-navigation outlines.
 *
 * INPUT. The pointer hovers and clicks the top-most input-enabled widget under it; a press on a widget is consumed (the
 * game does not see it). A held press owns the pointer: its drags and its release go to that widget wherever the pointer
 * goes, past its box, past the screen and, with a host that tracks it, past the window; then the top widget under the
 * pointer, if another one, hears of the release too (UIMouseInputType_ReleaseOver). A press that stops reaching its widget is
 * reported (OnPressLost) unless the widget is being destroyed, and can be handed to another one (UIScreen_TransferPress). A
 * widget holding a press can ask for a hidden-cursor drag (UIScreen_CapturePointer). The wheel goes to the widget under the
 * pointer, then up through its ancestors until one handles it. Keys and typed text go to the focused widget first, then up
 * through its parents, until one handles them (they are then consumed); a focused edit element that is not entered is passed
 * over but for the short list (below, ENTERING). A press of the reset, copy or paste action nobody handled then goes to the
 * hovered widget and its ancestors. The widget that handles a key's press owns the key until it is released: the key's
 * repeats and release go to that widget alone, even after focus moved. Keyboard navigation acts on the navigation actions
 * nobody handled (GameAction_UI*), and whatever is still unconsumed reaches the game. Focus can be on any focusable widget: a
 * click focuses the nearest focusable widget under the pointer (unless a widget on the way up keeps the focus on a press:
 * Widget_SetKeepsFocusOnPress), and navigation moves focus between focusable widgets. A click
 * that hits no widget, including any click while the screen has no pointer (the cursor captured for mouse look, or over UI
 * above), is a click outside: it ends navigation and clears focus, and the game gets the click.
 *
 * ENTERING. An edit element (Widget_NeedsEntering: a slider, a text box) takes keys of its own only once it is entered. Moving
 * the focus never changes anything: focused but not entered, it hears only the presses of the reset, copy, paste and context
 * menu actions, and every other key and the typed text pass it by, so the arrows and Tab move past it and typing on it does
 * nothing. The accept action's press on it (taken at its place, before its ancestors), a press of any mouse button on it
 * (unless it enters itself, and only while the screen has the keyboard) or UIScreen_EnterWidget enters it. Entered, it hears every key first, then its ancestors; what
 * both leave is the screen's in place of navigation's: the back action leaves it with a cancel, the accept action leaves it
 * keeping what was done, Tab and Shift+Tab leave it and move on at once (entering the next element if that is an edit
 * element, also on each repeat of the held key), and the arrows do nothing. Every one of them pressed in a tick is consumed, also
 * when another acts first, and the keys that leave it stay consumed until they are released. Focus moving anywhere leaves it (keeping), and a destroyed one is left silently. At most one widget is
 * entered, and it is always the focused one; pointer movement ends navigation but not the edit. An element that keeps the
 * keys (Widget_SetKeepsKeysWhenEntered) is left only by focus moving and code, and those keys stay unconsumed for its reader.
 *
 * LAYERS. Every root stands in a layer (UIScreenLayer, UIScreen_SetRootLayer): drawing and hit testing go by layer, then by z,
 * then by insertion. A press raises its root within a Normal or Floating layer, and in no other. Windows use the workspace: the
 * surface less the strips its insets name (UIScreen_SetWorkspaceInsets, UIScreen_GetWorkspaceBox).
 *
 * NAVIGATION. ui_next / ui_previous move focus between the focusable widgets of the current level in reading order, the
 * arrows move it to the nearest one in that direction (the first such key only starts navigation and shows the
 * outlines; holding one repeats with the key's auto-repeat). ui_accept enters the focused widget when it contains
 * focusable widgets (it becomes tabbed; this also starts navigation, so Enter opens a container focused by a click), and
 * ui_back leaves the entered widget, or at the top level ends navigation and clears focus. Pointer movement ends
 * navigation (focus stays). Non-focusable widgets are transparent to navigation: their focusable descendants belong to
 * the enclosing level. The entered widgets always lead to focus: when focus moves without navigation, the next
 * navigation step enters and leaves widgets to match.
 *
 * NOTIFICATIONS. Changes of a widget's hover, focus, tab, active and entered state, and events a widget type posts
 * (UIScreen_PostWidgetEvent), are queued and delivered to subscribers (UIScreen_Subscribe) at one point, at the end of
 * UIScreen_Update, never in the middle of input routing. A notification names its widget by id. A state change is queued
 * before the widget's hook runs, so when hooks change a state again the last notification tells the final state; it is
 * dropped when its widget is destroyed before delivery. A widget's own event is delivered even then (it carries
 * everything by value). A widget's own vtable hooks are still called immediately.
 *
 * SCALE. A UI pixel is a surface pixel times the screen's scale (UIScreen_SetScale): what its host's policy answers for a UI in
 * the window (UIWindowHost.h, UIScalePolicy.h), a screen's own for one in the world. It sizes placements (UIPlacement.h), stacks and every element of the kit;
 * widgets placed in normalized units do not change with it.
 *
 * THEME. A screen may have a theme (UIScreen_SetTheme, UITheme.h): the look of the elements on it, as data. It is held
 * under an asset user of the screen's own, so the look can change at any time, and a screen in the world can have a look
 * of its own. Elements read it at layout and at render, and never keep a pointer into it. A missing theme or a part it
 * leaves out draws a plain box, logged once.
 *
 * Construct with UIScreen_Construct and release with UIScreen_Deconstruct (which destroys every widget still in it).
 * Not thread-safe.
 */


// Forward declarations.
/** @brief The abstract widget base; full type in UIWidget.h. */
typedef struct WidgetStruct Widget;
/** @brief The logger; full type in Logger.h. Borrowed. */
typedef struct LoggerStruct Logger;
/** @brief The asset manager; full type in AssetManager.h. Borrowed. */
typedef struct AssetManagerStruct AssetManager;
/** @brief A theme asset; full type in UITheme.h. Borrowed from the asset manager. */
typedef struct UIThemeStruct UITheme;


// Macros.
/** @brief Number of mouse buttons the screen tracks (GameKey_MouseLeft through GameKey_MouseBack). */
#define UI_SCREEN_MOUSE_BUTTON_COUNT ((size_t)(GameKey_MouseBack - GameKey_MouseLeft) + 1U)
/** @brief Most notifications delivered by one UIScreen_Update; the rest are dropped (and logged, once per streak of such
 *  updates) so handlers that keep posting each other cannot hang the game. */
#define UI_SCREEN_MAX_EVENTS_PER_DISPATCH ((size_t)4096)
/** @brief The bit of a UIEventKind in a subscription's kind mask. */
#define UI_EVENT_KIND_MASK(kind) ((uint32_t)1U << (uint32_t)(kind))
/** @brief A kind mask that matches every UIEventKind. */
#define UI_EVENT_KIND_MASK_ALL ((uint32_t)0xFFFFFFFFU)
/** @brief Invalid subscription or animation id (a real one is >= 1). */
#define UI_SCREEN_ID_INVALID ((uint64_t)0)
/** @brief The bounds of a screen's scale. */
#define UI_SCREEN_MIN_SCALE 0.25f
#define UI_SCREEN_MAX_SCALE 8.0f
/** @brief How many element kinds a screen remembers reporting missing theme parts of. */
#define UI_SCREEN_THEME_REPORT_KINDS ((size_t)32)
/** @brief Presses of one button on one widget count as a row (a double click) while each comes within this many seconds of the
 *  last ... */
#define UI_SCREEN_MULTI_CLICK_SECONDS 0.4
/** @brief ... and within this many UI pixels of it. */
#define UI_SCREEN_MULTI_CLICK_DISTANCE 4.0f


// Types.
/**
 * @brief The surface a screen is drawn on: its size in pixels and the fitted-area settings (see Renderer.h), which the
 *        Layout pass uses to convert units. Build with UIViewport_Create.
 */
typedef struct UIViewportStruct
{
    /** @brief The surface's size in pixels; both components finite and > 0. */
    Vector2 SizePixels;
    /** @brief The aspect ratio (width / height) the fitted area has; finite and > 0. */
    float TargetAspectRatio;
    /** @brief Normalized [0;1] placement of the fitted area within the surface. */
    Vector2 TargetRelativePosition;
} UIViewport;

/**
 * @brief What a screen gets from its host each tick: the input, the pointer in the screen's own space, and the
 *        keyboard.
 *
 * For a full-window UI use UIScreenInput_CreateFromWindow. For a UI on a surface in the world, the host raycasts the
 * pointer onto the surface and passes the hit's [0;1] position (or no pointer), and gives the keyboard to the surface
 * the player is using.
 */
typedef struct UIScreenInputStruct
{
    /** @brief The tick's input (keys, buttons, wheel, text, actions); consumption is recorded in it. Borrowed for the
     *  call; NULL means no input reaches the screen this tick. */
    GameInput* Input;
    /** @brief Whether the pointer is over this screen this tick: it hovers widgets and starts presses. */
    bool IsPointerPresent;
    /** @brief Whether PointerPosition is known even though the pointer is not over this screen, so a held press keeps following
     *  it: past the window's edge, or below UI above that claimed the pointer. */
    bool IsPointerTracked;
    /** @brief The pointer position in the screen's own normalized space, [0;1] on the screen and past it off the screen; used
     *  when IsPointerPresent or IsPointerTracked. */
    Vector2 PointerPosition;
    /** @brief Whether the host hid the cursor for the drag the screen asked for (UIScreen_CapturePointer): the position stands
     *  still, hover does not change, and held presses hear PointerDeltaPixels. Meaningless without that request. */
    bool IsPointerRelative;
    /** @brief The raw motion since the last tick, in surface pixels; used when IsPointerRelative. */
    Vector2 PointerDeltaPixels;
    /** @brief Whether keys and typed text go to this screen this tick (keyboard navigation included). */
    bool HasKeyboardFocus;
} UIScreenInput;

/**
 * @brief What a notification is about.
 */
typedef enum UIEventKindEnum
{
    /** @brief The widget's hover state changed (IsSet is the new state). */
    UIEventKind_HoverChanged,
    /** @brief The widget's focus state changed (IsSet is the new state). */
    UIEventKind_FocusChanged,
    /** @brief Keyboard navigation entered or left the widget (IsSet is the new tabbed state). */
    UIEventKind_TabChanged,
    /** @brief The widget's active state changed (IsSet is the new state). */
    UIEventKind_ActiveChanged,
    /** @brief The edit element was entered or left (IsSet is the new entered state). */
    UIEventKind_EnteredChanged,
    /** @brief An event the widget's type posted (WidgetEventId says which; Value may carry data). */
    UIEventKind_Widget,
    /** @brief A named sound cue (WidgetEventId is a UIElementCue): what an element would sound like. Posted only while a
     *  subscription asks for this kind (UIScreen_PostCue), and delivered even if the widget was destroyed after posting it. */
    UIEventKind_Cue,
    /** @brief A popup the widget owns closed (UIPopup.h; WidgetEventId is the UIPopupCloseReason). Like a state change, it is dropped
     *  when the owner is destroyed before delivery (a submenu's item going with its menu). */
    UIEventKind_PopupClosed,
    /** @brief A drag ended (UIDragDrop.h; WidgetEventId is the UIDragEvent, Drag the drag's record): dropped on the widget, delivered from
     *  it, or cancelled. Like a widget's own event, delivered even if the widget was destroyed after it was posted. */
    UIEventKind_Drag
} UIEventKind;

/**
 * @brief The named cues elements post for what they do, for sounds to hang on later (UIEventKind_Cue). Nothing plays them yet; a
 *        cue costs nothing while nobody subscribes to the kind.
 */
typedef enum UIElementCueEnum
{
    /** @brief The pointer came onto it. */
    UIElementCue_Hover,
    /** @brief A button went down on it. */
    UIElementCue_Press,
    /** @brief The button of a press on it came up. */
    UIElementCue_Release,
    /** @brief It did what it is for (a click, a toggle). */
    UIElementCue_Activate,
    /** @brief Its value moved by a step (an arrow, a notch of the wheel, a reset). */
    UIElementCue_ValueStep,
    /** @brief It opened (a popup, a list). */
    UIElementCue_Open,
    /** @brief It closed. */
    UIElementCue_Close,
    /** @brief It refused what was asked (a press on a disabled element). */
    UIElementCue_Refused,
    /** @brief An edit element was entered: its keys are its own now. The screen posts it. */
    UIElementCue_Enter,
    /** @brief An edit element was left. The screen posts it. */
    UIElementCue_Leave
} UIElementCue;

/**
 * @brief The input a widget's own event tells of, by value, so a handler needs no widget (UIScreen_PostWidgetMouseEvent).
 */
typedef struct UIEventMouseStruct
{
    /** @brief The mouse button. For what the keyboard did (an activation, the menu key) the key; GameKey_Unknown for the wheel. */
    GameKey Button;
    /** @brief The GAME_KEY_MODIFIER_* flags held. */
    uint8_t Modifiers;
    /** @brief The pointer's position on the screen; for what the keyboard did, the middle of the widget. */
    Vector2 ScreenPosition;
    /** @brief The wheel's movement; zero for anything else. */
    Vector2 ScrollDelta;
    /** @brief Whether the pointer is over the widget. */
    bool IsOverWidget;
} UIEventMouse;

/**
 * @brief One queued notification, delivered by value. A plain value type.
 */
typedef struct UIEventStruct
{
    /** @brief What happened. */
    UIEventKind Kind;
    /** @brief The widget it happened to. Alive at delivery for a state change (those of destroyed widgets are dropped); a
     *  UIEventKind_Widget event is delivered even if its widget was destroyed after posting it. */
    uint64_t WidgetId;
    /** @brief The widget's type id. */
    uint64_t WidgetTypeId;
    /** @brief For the state changes: the new state. */
    bool IsSet;
    /** @brief For UIEventKind_Widget: the type-defined event id (e.g. a button's "activated"). */
    int32_t WidgetEventId;
    /** @brief For UIEventKind_Widget: whether Value carries data. */
    bool HasValue;
    /** @brief For UIEventKind_Widget: the event's data, when HasValue. */
    UIPropertyValue Value;
    /** @brief For UIEventKind_Widget: whether Mouse tells of the input behind it. */
    bool HasMouse;
    /** @brief For UIEventKind_Widget: the input behind it, when HasMouse. */
    UIEventMouse Mouse;
    /** @brief For UIEventKind_Widget: whether Index says which of the widget's parts it is about (UIScreen_PostWidgetIndexedEvent). */
    bool HasIndex;
    /** @brief For UIEventKind_Widget: the part, when HasIndex (a row of fields' axis). */
    int32_t Index;
    /** @brief For UIEventKind_Drag: whether Drag tells of the drag. */
    bool HasDrag;
    /** @brief For UIEventKind_Drag: the drag's record, when HasDrag. */
    UIDragRecord Drag;
} UIEvent;

/**
 * @brief Receives a notification at the end of UIScreen_Update.
 *
 * May do anything a widget hook may: destroy widgets (deferred), change focus, start animations, post further
 * notifications (delivered in the same pass) and subscribe or unsubscribe. A non-success return stops the delivery and
 * is returned from UIScreen_Update as a broken invariant.
 * @param screen The screen.
 * @param event The notification; valid for the call only.
 * @param userData The UserData given to UIScreen_Subscribe (the subscription's stored copy).
 * @returns Success, or an Error that stops the update.
 */
typedef Error (*UIEventHandler)(UIScreen* screen, const UIEvent* event, const UserData* userData);

/**
 * @brief Press tracking for one mouse button, so the release and drags reach the widget the press started on.
 *        Internal state of the screen.
 */
typedef struct UIScreenButtonTrackingStruct
{
    /** @brief True while a press this screen took is held. */
    bool IsPressed;
    /** @brief True once the press reached its widget, so a lost press has someone to tell. */
    bool HasReachedWidget;
    /** @brief Id of the widget that holds the press. */
    uint64_t TargetWidgetId;
    /** @brief Where the press started, normalized screen coordinates. */
    Vector2 StartScreenPosition;
    /** @brief The update-tick total time when the press started. */
    double StartTime;
    /** @brief Where the pointer was at the last delivery of this press, normalized screen coordinates. */
    Vector2 LastPosition;
    /** @brief How many presses in a row this one is (WidgetMouseInputArgs.ClickCount). */
    uint32_t ClickCount;
    /** @brief Whether its widget's drag source was asked and dragged nothing (it is asked once per press, UIDragDrop.h). */
    bool IsDragRefused;
} UIScreenButtonTracking;

/**
 * @brief The last press and how many came in a row, for counting clicks. Internal state of the screen.
 */
typedef struct UIScreenClickRecordStruct
{
    /** @brief How many presses in a row; 0 when nothing can continue the row. */
    uint32_t Count;
    /** @brief The last press's button and widget. */
    GameKey Button;
    uint64_t WidgetId;
    /** @brief Where it was, normalized screen coordinates, and when, in update-tick total time. */
    Vector2 Position;
    double Time;
} UIScreenClickRecord;

/**
 * @brief The UI context object. All fields are internal; use the UIScreen_* functions.
 */
typedef struct UIScreenStruct
{
    /** @brief The program-wide widget factory; borrowed. */
    UIWidgetFactory* _factory;
    /** @brief Where recoverable problems are reported (once each); borrowed, may be NULL. */
    Logger* _logger;

    /** @brief The widget registry: one slot per id slot (private element type). */
    GenericBuffer _slots;
    /** @brief Index + 1 of the first free registry slot, or 0 when none is free. */
    uint32_t _freeSlotHead;
    /** @brief Number of alive widgets (created and not destroyed). */
    size_t _aliveWidgetCount;
    /** @brief Next insertion sequence number (orders siblings of equal z). */
    uint64_t _nextSequence;

    /** @brief The roots (Widget*) in insertion order. */
    GenericBuffer _roots;
    /** @brief The same roots in drawing order: ascending z, then insertion. */
    GenericBuffer _rootDrawOrder;
    /** @brief Roots of destroyed subtrees (Widget*) waiting for the flush. */
    GenericBuffer _pendingDestroy;

    /** @brief The focused widget's id, or WIDGET_ID_INVALID. */
    uint64_t _focusedWidgetId;
    /** @brief The entered edit element's id, or WIDGET_ID_INVALID; when set, it is the focused widget's. */
    uint64_t _enteredWidgetId;
    /** @brief The hovered widget's id, or WIDGET_ID_INVALID. */
    uint64_t _hoveredWidgetId;
    /** @brief True while keyboard navigation is active (outlines are drawn). */
    bool _isNavigating;
    /** @brief Ids (uint64_t) of the widgets navigation has entered, outermost first. */
    GenericBuffer _navStack;
    /** @brief Scratch list of navigation candidates (private element type). */
    GenericBuffer _navCandidates;
    /** @brief Per GameKey: navigation consumed the held key, so its auto-repeat (followed with the raw state) keeps
     *  navigating. */
    bool _navigationKeys[GameKey_Count];
    /** @brief Per GameKey, meaningful while navigation follows the key (_navigationKeys, which sets it with each step): the held
     *  key is a Tab that went on from an entered element, so each step its auto-repeat takes enters the edit element it lands
     *  on. */
    bool _editingKeys[GameKey_Count];

    /** @brief The input of the running UIScreen_Update; NULL outside it. Borrowed. */
    GameInput* _input;
    /** @brief Whether the pointer was over the screen at the last update. */
    bool _hasPointer;
    /** @brief Whether the pointer's position was known at the last update: over the screen, or tracked past it. */
    bool _isPointerKnown;
    /** @brief The pointer position at the last update, normalized screen coordinates. */
    Vector2 _pointerPosition;
    /** @brief The widget a hidden-cursor drag was asked for, or WIDGET_ID_INVALID. */
    uint64_t _pointerCaptureWidgetId;
    /** @brief The last press, for counting clicks. */
    UIScreenClickRecord _lastClick;
    /** @brief Whether the screen had the keyboard at the last update. */
    bool _hasKeyboardFocus;
    /** @brief Set when geometry or the tree changed, so the hovered widget is looked up again. */
    bool _isHitTestDirty;
    /** @brief Press tracking per mouse button (index = key - GameKey_MouseLeft). */
    UIScreenButtonTracking _buttons[UI_SCREEN_MOUSE_BUTTON_COUNT];
    /** @brief Per keyboard GameKey: the id of the widget that handled the held key (0: none); it alone gets the key's
     *  repeats and release. */
    uint64_t _keyOwnerIds[GameKey_Count];

    /** @brief The surface the screen is drawn on. */
    UIViewport _viewport;
    /** @brief The height in pixels of one normalized-fitted unit on that surface. */
    float _fittedPixelsPerUnit;
    /** @brief Surface pixels per UI pixel. */
    float _scale;
    /** @brief Moves on whenever the scale, the viewport or the theme changes: widgets' cached preferred sizes are then old. */
    uint64_t _measureGeneration;
    /** @brief The strips along the surface's edges that windows leave alone (UIScreen_SetWorkspaceInsets), in UI pixels. */
    UIInsets _workspaceInsets;

    /** @brief The asset manager the theme came from; borrowed, meaningful while _theme is set. */
    AssetManager* _themeAssets;
    /** @brief The asset user that holds the theme, of its own (an AssetUserID); meaningful while _theme is set. */
    uint64_t _themeUser;
    /** @brief The current theme; borrowed from the manager under _themeUser, NULL for none. */
    const UITheme* _theme;
    /** @brief Moves on whenever the theme changes. */
    uint64_t _themeRevision;
    /** @brief Whether a missing theme was logged since the theme last changed. */
    bool _isMissingThemeReported;
    /** @brief Per element kind, a bit per part already logged as missing from the current theme. */
    uint32_t _reportedThemeParts[UI_SCREEN_THEME_REPORT_KINDS];

    /** @brief The running animations (private element type). */
    GenericBuffer _animations;
    /** @brief Next animation id; starts at 1. */
    uint64_t _nextAnimationId;
    /** @brief True while the animations are being advanced (removals are deferred then). */
    bool _isAdvancingAnimations;
    /** @brief Reusable keyframe storage for the animations. */
    WRBufferPool _keyframeBuffers;

    /** @brief Notifications (UIEvent) waiting for the end of the update. */
    GenericBuffer _eventQueue;
    /** @brief The subscriptions (private element type). */
    GenericBuffer _subscriptions;
    /** @brief Next subscription id; starts at 1. */
    uint64_t _nextSubscriptionId;
    /** @brief True while notifications are being delivered (unsubscriptions are deferred then). */
    bool _isDispatching;
    /** @brief True once a delivery dropped notifications, until a later one delivers notifications without dropping
     *  any (the warning is logged once per streak). */
    bool _isEventOverflowLogged;

    /** @brief Hover text's side table (UITooltip.h; a private element type), one entry per widget that has one. Owned. */
    GenericBuffer _tooltips;
    /** @brief The widget whose hover text the pointer rests on (the hovered widget or the nearest ancestor with one), or
     *  WIDGET_ID_INVALID. */
    uint64_t _tooltipWidgetId;
    /** @brief How long the pointer has rested there, in seconds. */
    double _tooltipRestSeconds;
    /** @brief Whether its text shows. */
    bool _isTooltipShown;
    /** @brief Whether a press hid it: it stays hidden until the pointer rests on something else. */
    bool _isTooltipSuppressed;
    /** @brief The text where the pointer rests, NUL-terminated UTF-8 (bytes). Owned. */
    GenericBuffer _tooltipText;
    /** @brief The widget that shows it (a root in the overlay layer), or WIDGET_ID_INVALID until one is needed. */
    uint64_t _tooltipDisplayId;

    /** @brief The open popups (UIPopup.h; a private element type), the first opened first. Owned. */
    GenericBuffer _popups;
    /** @brief Context menus' side table (UIContextMenu.h; a private element type), one entry per widget with a provider. Owned. */
    GenericBuffer _contextMenus;
    /** @brief Drag sources' and drop targets' side tables (UIDragDrop.h; a private element type), one entry per widget that declared
     *  itself one. Owned. */
    GenericBuffer _dragSources;
    GenericBuffer _dragTargets;
    /** @brief The drag in progress (UIDragDrop.h). */
    UIScreenDrag _drag;

    /** @brief True while UIScreen_Update runs. */
    bool _isUpdating;
    /** @brief True while UIScreen_Deconstruct runs. */
    bool _isTearingDown;
    /** @brief Reused buffer of widget ids (uint64_t) for the per-tick update snapshot. */
    GenericBuffer _updateSnapshot;
} UIScreen;


// Helpers.
/**
 * @brief Makes a viewport description.
 * @param sizePixels The surface's size in pixels.
 * @param targetAspectRatio The aspect ratio (width / height) the fitted area has.
 * @param targetRelativePosition Normalized [0;1] placement of the fitted area within the surface.
 * @returns The viewport.
 */
static inline UIViewport UIViewport_Create(Vector2 sizePixels, float targetAspectRatio, Vector2 targetRelativePosition)
{
    return (UIViewport)
    {
        .SizePixels = sizePixels,
        .TargetAspectRatio = targetAspectRatio,
        .TargetRelativePosition = targetRelativePosition
    };
}

/**
 * @brief Makes a screen input with nothing in it: no input, no pointer, no keyboard.
 * @returns The input.
 */
static inline UIScreenInput UIScreenInput_CreateEmpty(void)
{
    return (UIScreenInput)
    {
        .Input = NULL,
        .IsPointerPresent = false,
        .IsPointerTracked = false,
        .PointerPosition = (Vector2){ .x = 0.0f, .y = 0.0f },
        .IsPointerRelative = false,
        .PointerDeltaPixels = (Vector2){ .x = 0.0f, .y = 0.0f },
        .HasKeyboardFocus = false
    };
}

/**
 * @brief Makes the input of a screen that fills the window: the keyboard, and the mouse position divided by the window's size,
 *        as it is, also past the window's edges (raylib does not clamp it, and the system keeps reporting it while a button is
 *        held).
 *
 * The pointer is tracked unless the cursor is captured. It is present only while the cursor is on the window, inside it, and no UI
 * above has claimed it: over UI above it stays tracked, so a drag held below that UI goes on.
 * @param input The tick's input; may be NULL (then the result is empty).
 * @returns The input.
 */
UIScreenInput UIScreenInput_CreateFromWindow(GameInput* input);


// Lifecycle.
/**
 * @brief Initializes an empty screen.
 *
 * The viewport starts at 1280x720 pixels with a 16:9 centered fitted area; set the real one with UIScreen_SetViewport.
 * @param self The screen to initialize; must not be NULL.
 * @param factory The program-wide widget factory; borrowed, must outlive the screen and must not be NULL.
 * @param logger Reports recoverable problems (a failing animation, dropped notifications) once each; borrowed, may be
 *        NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p factory is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_Construct(UIScreen* self, UIWidgetFactory* factory, Logger* logger);

/**
 * @brief Destroys every widget still in the screen (their teardown hooks run) and releases its storage. The factory is
 *        borrowed and stays. Pending notifications are dropped. Best-effort teardown. Safe on NULL.
 * @param self The screen to deconstruct, or NULL.
 * @returns Success (including the NULL case); ErrorCode_InvalidOperation if called from inside UIScreen_Update or from
 *          inside this same deconstruction (a teardown hook); or the first error from a teardown hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_Deconstruct(UIScreen* self);

/**
 * @brief Returns the screen's (borrowed, program-wide) widget factory.
 * @param self The screen; must not be NULL.
 * @returns The factory.
 */
static inline UIWidgetFactory* UIScreen_GetFactory(const UIScreen* self)
{
    return self->_factory;
}

/**
 * @brief Returns the screen's logger.
 * @param self The screen; must not be NULL.
 * @returns The borrowed logger, or NULL.
 */
static inline Logger* UIScreen_GetLogger(const UIScreen* self)
{
    return self->_logger;
}


// Viewport.
/**
 * @brief Sets the surface the screen is drawn on (call when it changes, e.g. on a window resize).
 * @param self The screen; must not be NULL.
 * @param viewport The viewport; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_ArgumentOutOfRange if the size or the
 *          aspect ratio is not finite and positive, or the placement is not finite.
 */
Error UIScreen_SetViewport(UIScreen* self, const UIViewport* viewport);

/**
 * @brief Returns the screen's viewport.
 * @param self The screen; must not be NULL.
 * @returns The viewport.
 */
static inline UIViewport UIScreen_GetViewport(const UIScreen* self)
{
    return self->_viewport;
}

/**
 * @brief Sets the screen's scale: how many surface pixels one UI pixel takes (a host's scale policy sets it; a screen in the world
 *        has its own). Takes effect at the next layout pass.
 * @param self The screen; must not be NULL.
 * @param scale The scale; finite and within [UI_SCREEN_MIN_SCALE, UI_SCREEN_MAX_SCALE].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p scale is not.
 */
Error UIScreen_SetScale(UIScreen* self, float scale);

/**
 * @brief Returns the screen's scale.
 * @param self The screen; must not be NULL.
 * @returns Surface pixels per UI pixel; 1 unless set.
 */
static inline float UIScreen_GetScale(const UIScreen* self)
{
    return self->_scale;
}

/**
 * @brief Converts surface pixels to normalized-fitted units on this screen's viewport (the units of text): a size for a
 *        label from its height in pixels.
 * @param self The screen; must not be NULL.
 * @param pixels The length in surface pixels.
 * @returns The length in normalized-fitted units.
 */
static inline float UIScreen_PixelsToFitted(const UIScreen* self, float pixels)
{
    return pixels / self->_fittedPixelsPerUnit;
}

/**
 * @brief Returns the number that moves on whenever the scale, the viewport or the theme changes, which cached preferred
 *        sizes are compared against.
 * @param self The screen; must not be NULL.
 * @returns The generation.
 */
static inline uint64_t UIScreen_GetMeasureGeneration(const UIScreen* self)
{
    return self->_measureGeneration;
}


// The workspace.
/**
 * @brief Sets the strips along the surface's edges that windows leave alone (a menu bar at the top, a status bar at the bottom): what is
 *        left is the workspace, where floating windows stay reachable and a maximized one goes (UIScreen_GetWorkspaceBox). None by
 *        default, so the workspace is the whole surface. Whoever lays the bars out sets it; windows read it at their next update.
 * @param self The screen; must not be NULL.
 * @param insets The strips' widths from the left, top, right and bottom, in UI pixels; each finite, >= 0 and at most
 *        UI_LAYOUT_MAX_LENGTH.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for an inset that is not.
 */
Error UIScreen_SetWorkspaceInsets(UIScreen* self, UIInsets insets);

/**
 * @brief Returns the strips windows leave alone (UIScreen_SetWorkspaceInsets).
 * @param self The screen; must not be NULL.
 * @returns The insets in UI pixels; all 0 unless set.
 */
static inline UIInsets UIScreen_GetWorkspaceInsets(const UIScreen* self)
{
    return self->_workspaceInsets;
}

/**
 * @brief Returns the workspace: the area of the surface windows may use, the surface less the strips UIScreen_SetWorkspaceInsets
 *        names. The one place windows read it from.
 * @param self The screen; must not be NULL.
 * @returns The box in the screen's UI pixels from the surface's top-left (its root widgets' placement units); a size the strips leave
 *          nothing of is 0.
 */
Rectangle UIScreen_GetWorkspaceBox(const UIScreen* self);


// Theme.
/**
 * @brief Gives the screen a theme by name: the look of the kit's elements on it, from the next layout and render on.
 *
 * Loads the theme under a new asset user of the screen's, then retires the user of the one it had, which releases it. A
 * theme that does not load is logged once by the asset manager and changes nothing: the screen keeps what it had, and with
 * no theme its elements draw plain boxes. That is never an error of this call, so a pack's broken theme cannot stop a frame.
 * @param self The screen; must not be NULL. May be called from a handler or a hook.
 * @param assets The asset manager; borrowed, must outlive the screen (or its next theme). Must not be NULL.
 * @param themeName The theme's asset name; must not be NULL.
 * @returns Success (also when the theme did not load: see UIScreen_GetTheme); ErrorCode_IllegalArgument if a pointer is
 *          NULL; otherwise an error from the asset users.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_SetTheme(UIScreen* self, AssetManager* assets, const unsigned char* themeName);

/**
 * @brief Takes the screen's theme away: its elements draw plain boxes.
 * @param self The screen; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; otherwise an error from retiring the asset user.
 */
Error UIScreen_ClearTheme(UIScreen* self);

/**
 * @brief Returns the screen's theme. Valid until the screen's theme next changes: read it where it is used, never keep it.
 * @param self The screen; must not be NULL.
 * @returns The theme, or NULL for none.
 */
static inline const UITheme* UIScreen_GetTheme(const UIScreen* self)
{
    return self->_theme;
}

/**
 * @brief Returns the number that moves on whenever the screen's theme changes.
 * @param self The screen; must not be NULL.
 * @returns The revision.
 */
static inline uint64_t UIScreen_GetThemeRevision(const UIScreen* self)
{
    return self->_themeRevision;
}

/**
 * @brief Logs, once until the theme changes, that the screen's theme has no such part: the element draws a plain box.
 *
 * Elements call it on the update side when they find a part missing (never in Render). With no theme at all it logs that
 * once instead.
 * @param self The screen; must not be NULL.
 * @param elementKindName The element kind's name in the theme's format, e.g. u8"button"; must not be NULL.
 * @param elementKindIndex The kind's index, below UI_SCREEN_THEME_REPORT_KINDS.
 * @param partName The part's name, e.g. u8"background"; must not be NULL.
 * @param partIndex The part's index, below 32.
 */
void UIScreen_ReportMissingThemePart(UIScreen* self, const unsigned char* elementKindName, size_t elementKindIndex,
    const unsigned char* partName, size_t partIndex);


// Widgets by id.
/**
 * @brief Looks up an alive widget by id.
 * @param self The screen; must not be NULL.
 * @param widgetId The id.
 * @param outWidget [out] Receives the widget if alive, NULL otherwise; must not be NULL.
 * @returns true if the id names an alive widget of this screen (never for a destroyed one or a stale id).
 */
bool UIScreen_TryGetWidgetById(const UIScreen* self, uint64_t widgetId, Widget** outWidget);

/**
 * @brief Returns whether an id names an alive widget of this screen.
 * @param self The screen; must not be NULL.
 * @param widgetId The id.
 * @returns true if alive.
 */
bool UIScreen_IsWidgetAlive(const UIScreen* self, uint64_t widgetId);

/**
 * @brief Returns the number of alive widgets (attached or not).
 * @param self The screen; must not be NULL.
 * @returns The count.
 */
static inline size_t UIScreen_GetWidgetCount(const UIScreen* self)
{
    return self->_aliveWidgetCount;
}


// Root widgets.
/**
 * @brief Adds a widget as a root of the screen, on top of the roots of equal z.
 *
 * The widget must belong to this screen, be initialized, not be being destroyed and be detached.
 * @param self The screen; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL or the widget belongs to another screen;
 *          ErrorCode_InvalidOperation if the widget is not initialized, is being destroyed or is attached;
 *          ErrorCode_BufferTooLarge if it could not be stored.
 */
Error UIScreen_AddWidget(UIScreen* self, Widget* widget);

/**
 * @brief Removes a root from the screen without destroying it (it can be added again).
 *
 * Like Widget_RemoveSubWidget: hover, focus, keyboard navigation and mouse presses inside the subtree end (with their
 * hooks and notifications); its animations keep running.
 * @param self The screen; must not be NULL.
 * @param widget The root; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidOperation if the widget is not a
 *          root of this screen or is being destroyed; otherwise the first error from a fired hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_RemoveWidget(UIScreen* self, Widget* widget);

/**
 * @brief Returns the number of roots.
 * @param self The screen; must not be NULL.
 * @returns The root count.
 */
size_t UIScreen_GetRootCount(const UIScreen* self);

/**
 * @brief Returns the root at an index, in insertion order (z never reorders this list).
 * @param self The screen; must not be NULL.
 * @param index Index in [0, UIScreen_GetRootCount).
 * @returns The borrowed root, or NULL if @p index is out of range.
 */
Widget* UIScreen_GetRootAt(const UIScreen* self, size_t index);


// Focus, z order and hit testing.
/**
 * @brief Returns the focused widget's id.
 * @param self The screen; must not be NULL.
 * @returns The id, or WIDGET_ID_INVALID (0) when nothing is focused.
 */
static inline uint64_t UIScreen_GetFocusedWidgetId(const UIScreen* self)
{
    return self->_focusedWidgetId;
}

/**
 * @brief Returns the hovered widget's id.
 * @param self The screen; must not be NULL.
 * @returns The id, or 0 when the pointer is over no widget.
 */
static inline uint64_t UIScreen_GetHoveredWidgetId(const UIScreen* self)
{
    return self->_hoveredWidgetId;
}

/**
 * @brief Returns the entered edit element's id: the focused widget, while it is being edited.
 * @param self The screen; must not be NULL.
 * @returns The id, or WIDGET_ID_INVALID (0) when nothing is entered.
 */
static inline uint64_t UIScreen_GetEnteredWidgetId(const UIScreen* self)
{
    return self->_enteredWidgetId;
}

/**
 * @brief Returns whether keyboard navigation is active (its outlines are drawn).
 * @param self The screen; must not be NULL.
 * @returns true while navigating.
 */
static inline bool UIScreen_IsNavigating(const UIScreen* self)
{
    return self->_isNavigating;
}

/**
 * @brief Focuses a widget, or clears focus.
 *
 * When the focus moves, an entered edit element is left first (OnLeave with UILeaveReason_Keep). The old focus gets OnFocusDisable and the new one
 * OnFocusEnable, immediately; FocusChanged notifications are queued. If a hook moves focus again, the latest change wins and
 * only that widget ends up focused; if a hook makes @p widget unable to take focus (destroys, detaches or disables it),
 * nothing ends up focused. Focusing an edit element does not enter it (UIScreen_EnterWidget does).
 * @param self The screen; must not be NULL.
 * @param widget An alive, attached, focusable and input-enabled widget of this screen, or NULL to clear focus.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p widget belongs to another screen;
 *          ErrorCode_InvalidOperation if @p widget is being destroyed, detached, not focusable or input-disabled;
 *          otherwise the first error from a focus hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_SetFocusedWidget(UIScreen* self, Widget* widget);

/**
 * @brief Enters an edit element, focusing it first: from now on it hears every key first, until it is left. For a popup's
 *        list, a dialog's first field, the console's line when it opens.
 *
 * Moving the focus leaves the element entered before (UILeaveReason_Keep). The element hears OnEnter with UIEnterCause_Code,
 * and an EnteredChanged notification is queued. A hook that moves the focus elsewhere meanwhile wins: nothing is entered then.
 * Entering the entered element does nothing. Safe from hooks and handlers.
 * @param self The screen; must not be NULL.
 * @param widgetId The edit element (Widget_NeedsEntering): alive, attached, focusable, input-enabled, shown and not in an
 *        overlay.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the widget is gone, is no
 *          edit element or cannot take focus; otherwise the first error from a hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_EnterWidget(UIScreen* self, uint64_t widgetId);

/**
 * @brief Leaves the entered edit element, if there is one: it stays focused, and hears OnLeave with @p reason; an
 *        EnteredChanged notification is queued. Safe from hooks and handlers.
 * @param self The screen; must not be NULL.
 * @param reason Why: what a cancel takes back is the element's.
 * @returns Success (also with nothing entered); ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for
 *          an unknown reason; otherwise the error of the hook.
 */
Error UIScreen_LeaveWidget(UIScreen* self, UILeaveReason reason);

/**
 * @brief Raises a widget above its siblings (the roots of its layer, or its parent's children) in z order.
 * @param self The screen; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; propagates the z setter's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_BringToTop(UIScreen* self, Widget* widget);

/**
 * @brief Moves a root to another layer: the draw order and the hit test follow at once. A root that becomes an overlay takes no
 *        input any more: what it holds loses the focus and its presses.
 * @param self The screen; must not be NULL.
 * @param root A root of this screen; must not be NULL. A root keeps its layer while it is away from the roots.
 * @param layer The layer.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidOperation if the widget is not a root of this
 *          screen or is being destroyed; ErrorCode_ArgumentOutOfRange for an unknown layer; otherwise the first error from a
 *          fired hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_SetRootLayer(UIScreen* self, Widget* root, UIScreenLayer layer);

/**
 * @brief Returns the layer a root stands in.
 * @param self The screen; must not be NULL.
 * @param root The widget; must not be NULL.
 * @returns The layer; UIScreenLayer_Normal for a new one, and for a widget that is not a root.
 */
UIScreenLayer UIScreen_GetRootLayer(const UIScreen* self, const Widget* root);

/**
 * @brief Finds the top-most input-enabled widget whose box contains a screen position.
 *
 * Respects layers and z order (front-most wins) and subwidget clipping (AreSubWidgetsCut); input-disabled widgets are skipped
 * (input passes through them to whatever is behind), and overlays are never hit.
 * @param self The screen; must not be NULL.
 * @param screenPosition The position, normalized [0;1] screen coordinates.
 * @param outWidget [out] Receives the top-most widget, or NULL if none; must not be NULL.
 * @returns true if a widget was found.
 */
bool UIScreen_TryGetTopmostAt(UIScreen* self, Vector2 screenPosition, Widget** outWidget);


// Coordinate conversion.
/**
 * @brief Converts a widget-local [0;1] position to a screen [0;1] position: where it shows, through the render transforms of the
 *        widget and its ancestors.
 * @param self The screen; must not be NULL.
 * @param widget The reference widget; must not be NULL.
 * @param widgetLocal The widget-local position.
 * @returns The equivalent screen position.
 */
Vector2 UIScreen_WidgetToScreen(const UIScreen* self, const Widget* widget, Vector2 widgetLocal);

/**
 * @brief Converts a screen [0;1] position to a widget-local [0;1] position, the render transforms of the widget and its ancestors
 *        undone (the inverse of UIScreen_WidgetToScreen), so a turned slider is dragged along its own track. A transform that holds
 *        no point (a visual scale of 0) is passed over.
 * @param self The screen; must not be NULL.
 * @param widget The reference widget; must not be NULL.
 * @param screenPosition The screen position.
 * @returns The equivalent widget-local position (components are 0 where the widget has zero size).
 */
Vector2 UIScreen_ScreenToWidget(const UIScreen* self, const Widget* widget, Vector2 screenPosition);

/**
 * @brief Returns a widget's box as layout put it, in surface pixels of the screen's viewport: the box its look draws in, before
 *        any render transform turns it.
 * @param self The screen; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns The box.
 */
Rectangle UIScreen_GetWidgetPixelBox(const UIScreen* self, const Widget* widget);


// Update and render.
/**
 * @brief Advances the screen by one update tick.
 *
 * In order: routes the input (hover, mouse buttons and wheel, then keys and text to the focused widget and its
 * ancestors, then keyboard navigation), advances the animations, updates every attached widget whose update flag is set,
 * lays out every attached widget (parents first), delivers the queued notifications, and tears down destroyed widgets.
 * The update and layout passes cover the widgets that were in the tree when the tick began: widgets created or attached
 * during the tick (by a hook, a handler or the routing) get their first Update and Layout next tick.
 * @param self The screen; must not be NULL.
 * @param time The update-tick time.
 * @param input The tick's input; must not be NULL (use UIScreenInput_CreateEmpty for none). Its GameInput must stay
 *        valid for the call.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidOperation if called from inside
 *          an update of the same screen; otherwise the first error of a widget hook or a notification handler (a broken
 *          invariant).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_Update(UIScreen* self, ProgramTime time, const UIScreenInput* input);

/**
 * @brief Draws the widget tree and the keyboard-navigation outlines through a render context.
 *
 * Draws the roots, then each subtree parent first and children by z then insertion, with each widget's accumulated
 * transform and tint, clipping subtrees marked AreSubWidgetsCut through the context's clip stack. While navigating it
 * then draws green outlines around the widgets navigation entered and around an entered edit element, and a white one around
 * the focused widget that is only focused. Reads state only.
 * @param self The screen; must not be NULL.
 * @param renderContext The render context of the screen's surface; must not be NULL, inside a rendering pass.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise the first error of a widget's Render.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_Render(UIScreen* self, RenderContext* renderContext);


// Input state for widgets (valid during UIScreen_Update).
/**
 * @brief Returns the input of the running update, for widgets that read keys or actions themselves.
 * @param self The screen; must not be NULL.
 * @returns The borrowed input, or NULL outside UIScreen_Update or when the host passed none.
 */
static inline GameInput* UIScreen_GetInput(const UIScreen* self)
{
    return self->_input;
}

/**
 * @brief Returns whether the pointer was over the screen at the last update.
 * @param self The screen; must not be NULL.
 * @returns true if present.
 */
static inline bool UIScreen_IsPointerPresent(const UIScreen* self)
{
    return self->_hasPointer;
}

/**
 * @brief Returns the pointer position at the last update, normalized screen coordinates.
 * @param self The screen; must not be NULL.
 * @returns The position (the last known one when the pointer is neither present nor tracked; it stands still during a
 *          hidden-cursor drag).
 */
static inline Vector2 UIScreen_GetPointerPosition(const UIScreen* self)
{
    return self->_pointerPosition;
}

/**
 * @brief Returns whether a press this screen took is held: what keeps a host's pointer going off its surface.
 * @param self The screen; must not be NULL.
 * @returns true while any button's press is held by a widget.
 */
bool UIScreen_HasHeldPress(const UIScreen* self);

/**
 * @brief Hands a held press to another widget: its drags and its release go there from now on, with its start, its click count
 *        and its time kept. The widget that held it gets OnPressLost, and a hidden-cursor drag it asked for ends.
 * @param self The screen; must not be NULL.
 * @param button The press's mouse button.
 * @param newWidgetId The widget to take it: alive, attached, taking input and shown. Handing it to the one that holds it changes
 *        nothing.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p button is no mouse button; ErrorCode_InvalidOperation if the
 *          button holds no press, or the widget cannot take one; otherwise the error of the hook.
 */
Error UIScreen_TransferPress(UIScreen* self, GameKey button, uint64_t newWidgetId);

/**
 * @brief Asks for a hidden-cursor drag: the cursor hides, the pointer's position stands still, hover does not change, and drags
 *        carry the raw motion in DeltaPixels, with no edge to stop them.
 *
 * Legal only for a widget holding a press, and it ends with that widget's last press (released, lost or its widget destroyed) or
 * UIScreen_ReleasePointerCapture; the cursor then returns to where it began. The screen only records the request: a host applies
 * it (UIWindowHost), and one that does not (a screen in the world) leaves the drag absolute.
 * @param self The screen; must not be NULL.
 * @param widgetId The widget.
 * @returns Success (also when it already has it); ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the
 *          widget is gone or holds no press, or another widget has the capture.
 */
Error UIScreen_CapturePointer(UIScreen* self, uint64_t widgetId);

/**
 * @brief Ends a hidden-cursor drag before its press ends. Nothing happens without one.
 * @param self The screen; must not be NULL.
 */
void UIScreen_ReleasePointerCapture(UIScreen* self);

/**
 * @brief Returns whether a hidden-cursor drag is asked for.
 * @param self The screen; must not be NULL.
 * @returns true from UIScreen_CapturePointer until it ends.
 */
static inline bool UIScreen_IsPointerCaptured(const UIScreen* self)
{
    return self->_pointerCaptureWidgetId != ((uint64_t)0);
}

/**
 * @brief Returns the shape the pointer should show: the one of the widget holding a press, or else the hovered widget's. Read it
 *        after the update.
 * @param self The screen; must not be NULL.
 * @returns The shape; UICursorShape_Arrow with neither.
 */
UICursorShape UIScreen_GetCursorShape(const UIScreen* self);


// Animation.
/**
 * @brief Starts a keyframed animation of a widget property.
 *
 * The keyframes are validated (UIAnimation_ValidateKeyframes) and copied into the screen's storage. Each tick the value
 * is evaluated and written through the widget's property setter; several animations may target the same property, the
 * one started last wins. An animation whose setter fails is stopped and logged once (not fatal). Destroying the widget
 * stops its animations; detaching it does not.
 * @param self The screen; must not be NULL.
 * @param widgetId The target widget's id; must be alive.
 * @param propertyId The property to animate (a WidgetBaseProperty or one the widget's type declares).
 * @param type The property's type; must match the property.
 * @param keyframes The keyframes; must not be NULL.
 * @param keyframeCount The number of keyframes; at least 1.
 * @param options The end-of-animation behavior.
 * @param outAnimationId [out] Receives the new animation id (>= 1); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL, @p type is not the property's type or a keyframe
 *          is invalid (see UIAnimation_ValidateKeyframes); ErrorCode_InvalidOperation if the widget is not alive or has
 *          no such property; ErrorCode_ArgumentOutOfRange for an invalid keyframe value or time;
 *          ErrorCode_BufferTooLarge if storage could not grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_StartAnimation(UIScreen* self,
    uint64_t widgetId,
    int32_t propertyId,
    UIPropertyType type,
    const UIKeyframe* keyframes,
    size_t keyframeCount,
    UIAnimationOptions options,
    uint64_t* outAnimationId);

/**
 * @brief Stops an animation.
 * @param self The screen; must not be NULL.
 * @param animationId The animation id from UIScreen_StartAnimation.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if no such animation is
 *          running.
 */
Error UIScreen_StopAnimation(UIScreen* self, uint64_t animationId);

/**
 * @brief Stops every animation of a widget.
 * @param self The screen; must not be NULL.
 * @param widgetId The widget's id.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error UIScreen_StopWidgetAnimations(UIScreen* self, uint64_t widgetId);

/**
 * @brief Returns whether an animation is running (started, and not finished, stopped or failed).
 * @param self The screen; must not be NULL.
 * @param animationId The animation id.
 * @returns true if running.
 */
bool UIScreen_IsAnimationRunning(const UIScreen* self, uint64_t animationId);


// Notifications.
/**
 * @brief Subscribes a handler to the notifications of one widget, or of every widget.
 *
 * A subscription to one widget ends when that widget is torn down (at the end of the update it was destroyed in, after
 * the notifications are delivered).
 * @param self The screen; must not be NULL.
 * @param widgetId The widget to watch (alive), or WIDGET_ID_INVALID (0) for every widget.
 * @param kindMask Which kinds to receive: UI_EVENT_KIND_MASK(kind) bits ORed together, or UI_EVENT_KIND_MASK_ALL.
 * @param handler The handler; must not be NULL.
 * @param userData Data handed back to the handler; copied. May be NULL (an empty UserData).
 * @param outSubscriptionId [out] Receives the subscription id (>= 1); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p handler or @p outSubscriptionId is NULL or @p kindMask is
 *          0; ErrorCode_InvalidOperation if @p widgetId is not 0 and not alive; ErrorCode_BufferTooLarge if it could not
 *          be stored.
 */
Error UIScreen_Subscribe(UIScreen* self,
    uint64_t widgetId,
    uint32_t kindMask,
    UIEventHandler handler,
    const UserData* userData,
    uint64_t* outSubscriptionId);

/**
 * @brief Ends a subscription (safe from inside a handler).
 * @param self The screen; must not be NULL.
 * @param subscriptionId The subscription id.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if no such subscription
 *          exists.
 */
Error UIScreen_Unsubscribe(UIScreen* self, uint64_t subscriptionId);

/**
 * @brief Queues a widget type's own notification (e.g. a button's "activated"), delivered at the end of the update, even if
 *        the widget is destroyed meanwhile.
 * @param self The screen; must not be NULL.
 * @param widgetId The widget the event belongs to; must be alive.
 * @param widgetEventId The type-defined event id.
 * @param value Optional data carried by value; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the widget is not
 *          alive; ErrorCode_BufferTooLarge if the queue could not grow.
 */
Error UIScreen_PostWidgetEvent(UIScreen* self, uint64_t widgetId, int32_t widgetEventId, const UIPropertyValue* value);

/**
 * @brief Queues a widget type's own notification that tells of the input behind it: UIScreen_PostWidgetEvent, with the mouse data
 *        carried by value.
 * @param self The screen; must not be NULL.
 * @param widgetId The widget the event belongs to; must be alive.
 * @param widgetEventId The type-defined event id.
 * @param mouse The input behind it; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p mouse is NULL; ErrorCode_InvalidOperation if the widget is not
 *          alive; ErrorCode_BufferTooLarge if the queue could not grow.
 */
Error UIScreen_PostWidgetMouseEvent(UIScreen* self, uint64_t widgetId, int32_t widgetEventId, const UIEventMouse* mouse);

/**
 * @brief Queues a widget type's own notification about one of its parts: UIScreen_PostWidgetEvent, with an index saying which (a row of
 *        fields posts one event per field, its axis the index), carried as UIEvent.Index with UIEvent.HasIndex set.
 * @param self The screen; must not be NULL.
 * @param widgetId The widget the event belongs to; must be alive.
 * @param widgetEventId The type-defined event id.
 * @param value Optional data carried by value; may be NULL.
 * @param index Which part.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the widget is not alive;
 *          ErrorCode_BufferTooLarge if the queue could not grow.
 */
Error UIScreen_PostWidgetIndexedEvent(UIScreen* self, uint64_t widgetId, int32_t widgetEventId, const UIPropertyValue* value, int32_t index);

/**
 * @brief Posts a sound cue for a widget (UIEventKind_Cue, its WidgetEventId the cue), delivered at the end of the update like every
 *        notification. Nothing is queued while no subscription asks for UIEventKind_Cue for that widget or for every widget, so it
 *        costs nothing while nobody listens.
 * @param self The screen; must not be NULL.
 * @param widgetId The widget; must be alive.
 * @param cue The cue.
 * @returns Success (also when nobody listens); ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the
 *          widget is not alive; ErrorCode_BufferTooLarge if the queue could not grow.
 */
Error UIScreen_PostCue(UIScreen* self, uint64_t widgetId, UIElementCue cue);
