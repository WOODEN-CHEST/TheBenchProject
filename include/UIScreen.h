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
 * game does not see it) and its release and drags go to that widget. Keys and typed text go to the focused widget
 * first, then up through its parents, until one handles them (they are then consumed). The widget that handles a key's
 * press owns the key until it is released: the key's repeats and release go to that widget alone, even after focus
 * moved. Keyboard navigation acts on the navigation actions nobody handled (GameAction_UI*), and whatever is still
 * unconsumed reaches the game. Focus can be on any focusable widget: a click focuses the nearest focusable widget under
 * the pointer, and navigation moves focus between focusable widgets. A click that hits no widget, including any click
 * while the screen has no pointer (the cursor captured for mouse look, or over UI above), is a click outside: it ends
 * navigation and clears focus, and the game gets the click.
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
 * NOTIFICATIONS. Changes of a widget's hover, focus, tab and active state, and events a widget type posts
 * (UIScreen_PostWidgetEvent), are queued and delivered to subscribers (UIScreen_Subscribe) at one point, at the end of
 * UIScreen_Update, never in the middle of input routing. A notification names its widget by id. A state change is queued
 * before the widget's hook runs, so when hooks change a state again the last notification tells the final state; it is
 * dropped when its widget is destroyed before delivery. A widget's own event is delivered even then (it carries
 * everything by value). A widget's own vtable hooks are still called immediately.
 *
 * Construct with UIScreen_Construct and release with UIScreen_Deconstruct (which destroys every widget still in it).
 * Not thread-safe.
 */


// Forward declarations.
/** @brief The abstract widget base; full type in UIWidget.h. */
typedef struct WidgetStruct Widget;
/** @brief The logger; full type in Logger.h. Borrowed. */
typedef struct LoggerStruct Logger;


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
    /** @brief Whether the pointer is over this screen this tick. */
    bool IsPointerPresent;
    /** @brief The pointer position in the screen's own normalized [0;1] space; used when IsPointerPresent. */
    Vector2 PointerPosition;
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
    /** @brief An event the widget's type posted (WidgetEventId says which; Value may carry data). */
    UIEventKind_Widget
} UIEventKind;

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
    /** @brief For UIEventKind_Widget: the type-defined event id (e.g. a button's "clicked"). */
    int32_t WidgetEventId;
    /** @brief For UIEventKind_Widget: whether Value carries data. */
    bool HasValue;
    /** @brief For UIEventKind_Widget: the event's data, when HasValue. */
    UIPropertyValue Value;
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
    /** @brief Id of the widget the press started on. */
    uint64_t TargetWidgetId;
    /** @brief Where the press started, normalized [0;1] screen coordinates. */
    Vector2 StartScreenPosition;
    /** @brief The update-tick total time when the press started. */
    double StartTime;
} UIScreenButtonTracking;

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

    /** @brief The input of the running UIScreen_Update; NULL outside it. Borrowed. */
    GameInput* _input;
    /** @brief Whether the pointer was over the screen at the last update. */
    bool _hasPointer;
    /** @brief The pointer position at the last update, normalized [0;1] screen coordinates. */
    Vector2 _pointerPosition;
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
        .PointerPosition = (Vector2){ .x = 0.0f, .y = 0.0f },
        .HasKeyboardFocus = false
    };
}

/**
 * @brief Makes the input of a screen that fills the window: the mouse position normalized by the window size (no
 *        pointer while the cursor is captured, outside the window, or already claimed by UI above), and the keyboard.
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
 * The old focus gets OnFocusDisable and the new one OnFocusEnable, immediately; FocusChanged notifications are queued.
 * If a hook moves focus again, the latest change wins and only that widget ends up focused; if a hook makes @p widget
 * unable to take focus (destroys, detaches or disables it), nothing ends up focused.
 * @param self The screen; must not be NULL.
 * @param widget An alive, attached, focusable and input-enabled widget of this screen, or NULL to clear focus.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL or @p widget belongs to another screen;
 *          ErrorCode_InvalidOperation if @p widget is being destroyed, detached, not focusable or input-disabled;
 *          otherwise the first error from a focus hook.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_SetFocusedWidget(UIScreen* self, Widget* widget);

/**
 * @brief Raises a widget above its siblings (the roots, or its parent's children) in z order.
 * @param self The screen; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; propagates the z setter's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_BringToTop(UIScreen* self, Widget* widget);

/**
 * @brief Finds the top-most input-enabled widget whose box contains a screen position.
 *
 * Respects z order (front-most wins) and subwidget clipping (AreSubWidgetsCut); input-disabled widgets are skipped
 * (input passes through them to whatever is behind).
 * @param self The screen; must not be NULL.
 * @param screenPosition The position, normalized [0;1] screen coordinates.
 * @param outWidget [out] Receives the top-most widget, or NULL if none; must not be NULL.
 * @returns true if a widget was found.
 */
bool UIScreen_TryGetTopmostAt(UIScreen* self, Vector2 screenPosition, Widget** outWidget);


// Coordinate conversion.
/**
 * @brief Converts a widget-local [0;1] position to a screen [0;1] position.
 * @param self The screen; must not be NULL.
 * @param widget The reference widget; must not be NULL.
 * @param widgetLocal The widget-local position.
 * @returns The equivalent screen position.
 */
Vector2 UIScreen_WidgetToScreen(const UIScreen* self, const Widget* widget, Vector2 widgetLocal);

/**
 * @brief Converts a screen [0;1] position to a widget-local [0;1] position.
 * @param self The screen; must not be NULL.
 * @param widget The reference widget; must not be NULL.
 * @param screenPosition The screen position.
 * @returns The equivalent widget-local position (components are 0 where the widget has zero size).
 */
Vector2 UIScreen_ScreenToWidget(const UIScreen* self, const Widget* widget, Vector2 screenPosition);


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
 * then draws green outlines around the entered widgets and a white one around the focused widget. Reads state only.
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
 * @brief Returns the pointer position at the last update, normalized [0;1] screen coordinates.
 * @param self The screen; must not be NULL.
 * @returns The position (the last known one when the pointer is not present).
 */
static inline Vector2 UIScreen_GetPointerPosition(const UIScreen* self)
{
    return self->_pointerPosition;
}


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
 * @brief Queues a widget type's own notification (e.g. a button's "clicked"), delivered at the end of the update, even if
 *        the widget is destroyed meanwhile.
 * @param self The screen; must not be NULL.
 * @param widgetId The widget the event belongs to; must be alive.
 * @param widgetEventId The type-defined event id.
 * @param value Optional data carried by value; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the widget is not
 *          alive; ErrorCode_BufferTooLarge if the queue could not grow.
 */
Error UIScreen_PostWidgetEvent(UIScreen* self, uint64_t widgetId, int32_t widgetEventId, const UIPropertyValue* value);
