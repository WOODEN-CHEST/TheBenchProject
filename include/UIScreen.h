#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wr/WRError.h"
// GenericBuffer backs the screen's registries; Vector2 is used throughout; RenderContext is the render
// target; ProgramTime is the update time. The UI submodules provide the factory (embedded by value), the
// input enums/args, and the animation types. All are unavoidable in this public API.
#include "wr/WRMemory.h"
#include "raylib/raylib.h"
#include "Renderer.h"
#include "ProgramTime.h"
#include "UIInput.h"
#include "UIAnimation.h"
#include "UIWidgetFactory.h"


/**
 * @file UIScreen.h
 * @brief The UI context: it owns the widget factory and registry, routes input, drives keyboard
 *        navigation, hosts animations, and renders the widget tree with focus/z ordering.
 *
 * A UIScreen is one self-contained UI context (many can exist; usually one is used). It holds a factory
 * that produces widgets, a registry of every widget it owns (by id), an ordered set of top-level "root"
 * widgets, the currently focused root, and the keyboard-navigation and animation state. The caller drives
 * it each frame: UIScreen_Update advances input, navigation, animations and per-widget updates (on a
 * reused snapshot so widgets may be added/removed mid-update safely), and UIScreen_Render draws the tree
 * (parents under children, siblings by ascending z) and the navigation outlines on top.
 *
 * All positions and sizes are window-relative normalized [0;1]: (0,0) top-left, (1,1) bottom-right. Input
 * is delivered to the top-most widget at the pointer (for hover/click/scroll) or to the focused widget
 * (for keyboard), and a button release is delivered to the widget the press started on. Widgets query the
 * screen (not raylib) for input state through the screen's input-state query functions (UIScreen_IsKeyDown,
 * UIScreen_GetMousePosition, and so on).
 *
 * Construct with UIScreen_Construct and release with UIScreen_Deconstruct. Not thread-safe.
 *
 * BEHAVIOR NOTES. The IsRendered / IsUpdated / IsInputEnabled flags are per-widget and do NOT cascade to
 * subwidgets (each widget participates independently). Navigation keys (Tab, arrows, Enter, Escape) drive
 * keyboard navigation and are not forwarded to widgets; other keys and text go to the focused widget.
 * Pointer positions are normalized against the window size, so the screen is assumed to fill the window.
 */


// Forward declarations.
/** @brief The abstract widget base; full type in UIWidget.h. */
typedef struct WidgetStruct Widget;


// Types.
/**
 * @brief Per-mouse-button press tracking, so a release can be delivered to the press-start widget.
 *
 * Internal state of the screen; one entry per modelled mouse button.
 */
typedef struct UIScreenButtonTrackingStruct
{
    /** @brief True while this button is held after a press the screen saw. */
    bool IsPressed;
    /** @brief Id of the widget the press started on (0 if the press hit empty space). */
    uint64_t TargetWidgetId;
    /** @brief Where the press started, normalized [0;1] screen coordinates. */
    Vector2 StartScreenPosition;
    /** @brief The update-tick total time when the press started, for computing release duration. */
    double StartTime;
} UIScreenButtonTracking;

/**
 * @brief The UI context object: factory, registries, focus/navigation state, animations and input state.
 *
 * Underscore-prefixed fields are internal. Construct with UIScreen_Construct, release with
 * UIScreen_Deconstruct.
 */
typedef struct UIScreenStruct
{
    /** @brief The program-wide widget factory; borrowed, not owned (one factory serves many screens). */
    UIWidgetFactory* _factory;
    /** @brief Registry of every widget owned by this screen (by id). Layout is internal. */
    GenericBuffer _widgets;
    /** @brief Next widget id to mint; starts at 1 and only increases. */
    uint64_t _nextWidgetId;
    /** @brief Ordered set of top-level root widgets (Widget*). */
    GenericBuffer _roots;
    /** @brief The focused (front-most) root widget, or NULL for none. */
    Widget* _focusedWidget;

    /** @brief True while keyboard navigation is active (outlines are shown). */
    bool _isNavigating;
    /** @brief Id of the currently targeted (white-outlined) widget, or 0 for none. */
    uint64_t _navTargetId;
    /** @brief Stack of tabbed-into (green-outlined) widget ids; deepest last (uint64_t). */
    GenericBuffer _navStack;

    /** @brief Current pointer position, normalized [0;1] screen coordinates. */
    Vector2 _mousePosition;
    /** @brief Previous pointer position in pixels, for detecting movement (which stops navigation). */
    Vector2 _previousMousePixel;
    /** @brief Wheel scroll delta captured this tick. */
    Vector2 _scrollDelta;
    /** @brief Per-button press tracking for release routing. */
    UIScreenButtonTracking _buttons[UIMouseButton_Count];
    /** @brief Id of the currently hovered widget, or 0 for none. */
    uint64_t _hoveredWidgetId;

    /** @brief Active animations (layout internal). */
    GenericBuffer _animations;
    /** @brief Next animation id to mint; starts at 1 and only increases. */
    uint64_t _nextAnimationId;

    /** @brief Reused buffer of widget ids for the per-tick update snapshot (uint64_t). */
    GenericBuffer _updateSnapshot;
    /** @brief Reused buffer of candidate widgets for keyboard navigation (Widget*). */
    GenericBuffer _navCandidates;
    /** @brief Small scratch for in-place z sorting of siblings (two element slots). */
    unsigned char _sortScratch[2 * sizeof(void*)];
} UIScreen;


// Lifecycle.
/**
 * @brief Initializes an empty screen: its factory, registries and input/navigation/animation state.
 * @param self The screen to initialize; must not be NULL.
 * @param factory The program-wide widget factory the screen builds widgets from; borrowed, must outlive
 *        the screen and must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p factory is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_Construct(UIScreen* self, UIWidgetFactory* factory);

/**
 * @brief Releases the screen and everything it owns (the factory and internal storage).
 *
 * Frees the widget registry, roots, navigation, animation (including each animation's keyframes) and
 * scratch storage, and deconstructs the factory (which releases every type's pool). Widgets are pooled by
 * the factory, so any still-live widgets are released with their pools; deconstruct widgets that own
 * external resources first. Best-effort teardown. Safe on NULL.
 * @param self The screen to deconstruct, or NULL.
 * @returns Success (including the NULL case), or the first non-success Error encountered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_Deconstruct(UIScreen* self);

/**
 * @brief Returns the screen's (borrowed, program-wide) widget factory.
 * @param self The screen; must not be NULL.
 * @returns The borrowed factory pointer.
 */
static inline UIWidgetFactory* UIScreen_GetFactory(UIScreen* self)
{
    return self->_factory;
}


// Widget registration (used by Widget_Construct / Widget_Deconstruct; not general-purpose).
/**
 * @brief Mints the next widget id and registers a widget with the screen. Intended for Widget_Construct.
 * @param self The screen; must not be NULL.
 * @param widget The widget to register; must not be NULL.
 * @param outId [out] Receives the minted id (>= 1). Must not be NULL.
 * @returns Success with *outId set; ErrorCode_IllegalArgument if @p self, @p widget or @p outId is NULL;
 *          ErrorCode_BufferTooLarge if the registry could not grow.
 */
Error UIScreen_RegisterWidget(UIScreen* self, Widget* widget, uint64_t* outId);

/**
 * @brief Unregisters a widget and clears any screen references to it. Intended for Widget_Deconstruct.
 *
 * Removes the widget from the registry and (defensively) from the roots, clears focus / navigation
 * references to it without firing its hooks (it is being torn down), and purges its animations.
 * @param self The screen; must not be NULL.
 * @param widgetId The id of the widget to unregister.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_UnregisterWidget(UIScreen* self, uint64_t widgetId);

/**
 * @brief Looks up a widget by id.
 * @param self The screen; must not be NULL.
 * @param widgetId The id to look up.
 * @param outWidget [out] Receives the borrowed widget if found, NULL otherwise. Must not be NULL.
 * @returns true if found (and written to @p outWidget); false otherwise (including NULL arguments).
 */
bool UIScreen_TryGetWidgetById(UIScreen* self, uint64_t widgetId, Widget** outWidget);


// Root widgets.
/**
 * @brief Adds a widget as a top-level root of the screen.
 *
 * The widget must belong to this screen and must not already be attached (no parent, not already a root).
 * @param self The screen; must not be NULL.
 * @param widget The widget to add as a root; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p widget is NULL or the widget belongs to
 *          another screen; ErrorCode_InvalidOperation if the widget is already attached or could not be stored.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_AddWidget(UIScreen* self, Widget* widget);

/**
 * @brief Removes a top-level root widget from the screen. Does not deconstruct it.
 *
 * Clears focus if the removed widget was focused, resets navigation if it was involved, and purges the
 * removed subtree's animations (per "removing a widget removes its animations").
 * @param self The screen; must not be NULL.
 * @param widget The root widget to remove; must not be NULL and must be a current root.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p widget is NULL; ErrorCode_InvalidOperation if
 *          the widget is not a current root.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_RemoveWidget(UIScreen* self, Widget* widget);

/**
 * @brief Returns the number of top-level root widgets.
 * @param self The screen; must not be NULL.
 * @returns The root count.
 */
size_t UIScreen_GetRootCount(const UIScreen* self);

/**
 * @brief Returns the root widget at a given index (order is unspecified beyond z at render time).
 * @param self The screen; must not be NULL.
 * @param index Index in [0, UIScreen_GetRootCount).
 * @returns The borrowed root, or NULL if @p index is out of range.
 */
Widget* UIScreen_GetRootAt(const UIScreen* self, size_t index);


// Focus and z ordering.
/**
 * @brief Returns the focused (front-most) root widget, or NULL.
 * @param self The screen; must not be NULL.
 * @returns The focused widget, or NULL.
 */
static inline Widget* UIScreen_GetFocusedWidget(const UIScreen* self)
{
    return self->_focusedWidget;
}

/**
 * @brief Focuses a widget's root (bringing it to the top), or clears focus when @p widget is NULL.
 *
 * The focus lands on the widget's root (its top-most ancestor). Fires OnFocusDisable on the old focus and
 * OnFocusEnable on the new, and brings the new focused root above the other roots.
 * @param self The screen; must not be NULL.
 * @param widget The widget to focus (its root is focused), or NULL to clear focus.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; propagates focus-hook / z-order errors.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_SetFocusedWidget(UIScreen* self, Widget* widget);

/**
 * @brief Raises a widget above its siblings (roots, or its parent's children) in z order.
 * @param self The screen; must not be NULL.
 * @param widget The widget to bring to the top; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p widget is NULL; propagates the z setter's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_BringToTop(UIScreen* self, Widget* widget);

/**
 * @brief Finds the top-most input-enabled widget whose box contains a screen position.
 *
 * Respects z order (front-most wins) and subwidget clipping (AreSubWidgetsCut); input-disabled widgets are
 * skipped as candidates (input passes through them to whatever is behind).
 * @param self The screen; must not be NULL.
 * @param screenPosition The position, normalized [0;1] screen coordinates.
 * @param outWidget [out] Receives the top-most widget, or NULL if none. Must not be NULL.
 * @returns true if a widget was found; false otherwise.
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
Vector2 UIScreen_WidgetToScreen(UIScreen* self, const Widget* widget, Vector2 widgetLocal);

/**
 * @brief Converts a screen [0;1] position to a widget-local [0;1] position.
 * @param self The screen; must not be NULL.
 * @param widget The reference widget; must not be NULL.
 * @param screenPosition The screen position.
 * @returns The equivalent widget-local position (components are 0 where the widget has zero size).
 */
Vector2 UIScreen_ScreenToWidget(UIScreen* self, const Widget* widget, Vector2 screenPosition);


// Update and render.
/**
 * @brief Advances the screen by one update tick: input, navigation, animations, and per-widget updates.
 *
 * Snapshots the live widgets (into a reused buffer) so widgets added this tick update next tick and widgets
 * removed this tick are skipped; polls and routes input (hover, clicks with focus/z changes, scroll,
 * keyboard navigation, and keyboard to the focused widget); advances animations (writing values through
 * widget property setters); then updates each snapshot widget whose IsUpdated is set.
 * @param self The screen; must not be NULL.
 * @param time The update-tick time.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; otherwise the first error from a widget
 *          callback, animation apply, or navigation transition.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_Update(UIScreen* self, ProgramTime time);

/**
 * @brief Renders the widget tree and the navigation outlines through the given render context.
 *
 * Draws roots in ascending z, each subtree parent-first then children in ascending z, applying each
 * widget's accumulated transform and tint and clipping subtrees marked AreSubWidgetsCut. Then draws the
 * keyboard-navigation outlines on top: green around each tabbed widget, white around the current target.
 * @param self The screen; must not be NULL.
 * @param renderContext The render context to draw through; must not be NULL, mid-pass.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p renderContext is NULL; otherwise the first
 *          error from a widget's Render.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_Render(UIScreen* self, RenderContext* renderContext);


// Input state queries (widgets call these instead of raylib).
/**
 * @brief Returns the current pointer position, normalized [0;1] screen coordinates.
 * @param self The screen; must not be NULL.
 * @returns The pointer position.
 */
static inline Vector2 UIScreen_GetMousePosition(const UIScreen* self)
{
    return self->_mousePosition;
}

/**
 * @brief Returns the wheel scroll delta captured this tick.
 * @param self The screen; must not be NULL.
 * @returns The scroll delta (x, y).
 */
static inline Vector2 UIScreen_GetScrollDelta(const UIScreen* self)
{
    return self->_scrollDelta;
}

/**
 * @brief Returns whether a mouse button is currently held.
 * @param self The screen; must not be NULL.
 * @param button The button to query.
 * @returns true if the button is down.
 */
bool UIScreen_IsMouseButtonDown(const UIScreen* self, UIMouseButton button);

/**
 * @brief Returns whether a mouse button was pressed this tick.
 * @param self The screen; must not be NULL.
 * @param button The button to query.
 * @returns true if the button transitioned to pressed this tick.
 */
bool UIScreen_IsMouseButtonPressed(const UIScreen* self, UIMouseButton button);

/**
 * @brief Returns whether a mouse button was released this tick.
 * @param self The screen; must not be NULL.
 * @param button The button to query.
 * @returns true if the button transitioned to released this tick.
 */
bool UIScreen_IsMouseButtonReleased(const UIScreen* self, UIMouseButton button);

/**
 * @brief Returns whether a key is currently held.
 * @param self The screen; must not be NULL.
 * @param key The key to query.
 * @returns true if the key is down.
 */
bool UIScreen_IsKeyDown(const UIScreen* self, UIKey key);

/**
 * @brief Returns whether a key was pressed this tick.
 * @param self The screen; must not be NULL.
 * @param key The key to query.
 * @returns true if the key transitioned to pressed this tick.
 */
bool UIScreen_IsKeyPressed(const UIScreen* self, UIKey key);

/**
 * @brief Returns whether a key was released this tick.
 * @param self The screen; must not be NULL.
 * @param key The key to query.
 * @returns true if the key transitioned to released this tick.
 */
bool UIScreen_IsKeyReleased(const UIScreen* self, UIKey key);


// Animation.
/**
 * @brief Starts a keyframed animation on a widget property.
 *
 * Copies the keyframes into the screen's storage and drives them each tick, writing the evaluated value
 * through the widget's generic property setter. Multiple animations may target the same widget+property;
 * the one added last takes priority (it is applied last each tick).
 * @param self The screen; must not be NULL.
 * @param widgetId The target widget's id; must currently exist.
 * @param propertyId The property id to animate (a WidgetBaseProperty id or a concrete widget property id).
 * @param type The value type of the property (selects the blend rule and value size).
 * @param keyframes The keyframes (ordered by non-decreasing time); must not be NULL.
 * @param keyframeCount The number of keyframes; must be at least 1.
 * @param options The end-of-animation behavior (looping / auto-remove).
 * @param outAnimationId [out] Receives the new animation id (>= 1). Must not be NULL.
 * @returns Success with *outAnimationId set; ErrorCode_IllegalArgument if @p self, @p keyframes or
 *          @p outAnimationId is NULL or @p keyframeCount is 0; ErrorCode_InvalidOperation if @p widgetId
 *          does not exist; ErrorCode_ArgumentOutOfRange if @p type is unrecognized; ErrorCode_BufferTooLarge
 *          if storage could not grow.
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
 * @brief Stops and removes a single animation by id.
 * @param self The screen; must not be NULL.
 * @param animationId The animation id returned by UIScreen_StartAnimation.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if no such
 *          animation exists.
 */
Error UIScreen_StopAnimation(UIScreen* self, uint64_t animationId);

/**
 * @brief Stops and removes every animation targeting a given widget.
 * @param self The screen; must not be NULL.
 * @param widgetId The target widget's id.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error UIScreen_StopWidgetAnimations(UIScreen* self, uint64_t widgetId);
