#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"


/*
 * Private to the UI framework (UIWidget.c, UIWidgetFactory.c and the UIScreen*.c files): what one part needs from
 * another but no widget or game code may call. The screen is split by job:
 *   - UIScreen.c: lifecycle, the id registry (slot map), roots and draw order, destruction and its flush, update and
 *     render orchestration, the layout pass, viewport, coordinates.
 *   - UIScreenInput.c: pointer, mouse buttons, wheel, keys and text, focus.
 *   - UIScreenNavigation.c: keyboard navigation.
 *   - UIScreenAnimation.c: animations.
 *   - UIScreenEvents.c: the notification queue and the subscriptions.
 */


// Screen: registry (UIScreen.c).
/**
 * Reserves a registry slot for a widget being created and writes the minted id into it. The id does not resolve until
 * UIScreen_InternalActivateWidget. Fails with ErrorCode_InvalidOperation while the screen is being torn down, and with
 * ErrorCode_BufferTooLarge when the registry cannot grow or every id of a slot is used up.
 */
Error UIScreen_InternalReserveWidget(UIScreen* self, Widget* widget);

/** Frees the slot of a widget whose creation failed after UIScreen_InternalReserveWidget. */
void UIScreen_InternalCancelWidget(UIScreen* self, uint64_t widgetId);

/** Makes a reserved widget alive (its id resolves). */
void UIScreen_InternalActivateWidget(UIScreen* self, uint64_t widgetId);

/** Returns the widget of an id when it is alive, NULL otherwise (stale, dying, reserved or never minted). */
Widget* UIScreen_InternalResolve(const UIScreen* self, uint64_t widgetId);

/** Returns the next insertion sequence number. */
uint64_t UIScreen_InternalNextSequence(UIScreen* self);

/** Marks the hovered widget as needing a new hit test (geometry, flags or the tree changed). */
void UIScreen_InternalInvalidateHitTest(UIScreen* self);

/** Keeps the draw order of a widget's siblings sorted after its z changed. */
void UIScreen_InternalOnZChanged(UIScreen* self, Widget* widget);

/** The one detach path for an alive widget (a root or a child): unlinks it, then ends hover, focus, navigation and
 *  mouse presses inside its subtree, running the hooks last with every widget re-resolved by id. */
Error UIScreen_InternalDetachWidget(UIScreen* self, Widget* widget);

/** Widget_Destroy's work: unlinks the subtree, marks it dying, forgets every reference into it (no hooks) and queues it
 *  for the flush. */
Error UIScreen_InternalDestroyWidget(UIScreen* self, Widget* widget);

/** Returns whether @p node is @p ancestor or lies in its subtree. */
bool UIScreen_InternalIsInSubtree(const Widget* node, const Widget* ancestor);

/** Returns whether a widget is attached to one of its screen's roots (so it is updated, laid out and drawn). */
bool UIScreen_InternalIsAttached(const Widget* widget);

/** Composes a widget's absolute box (normalized screen coordinates) from its parent chain. */
void UIScreen_InternalGetAbsoluteBox(const Widget* widget, Vector2* outPosition, Vector2* outSize);

/** Returns a widget's estimated bounds (it and its subtree) in absolute screen coordinates, caching them (update only). */
Rectangle UIScreen_InternalGetEstimatedScreenBox(Widget* widget);

/** UIScreen_InternalGetEstimatedScreenBox without writing any cache, for Render. */
Rectangle UIScreen_InternalPeekEstimatedScreenBox(const Widget* widget);


// Screen: input and focus (UIScreenInput.c).
/** Routes the tick's input: pointer, buttons, wheel, then keys, text and navigation. */
Error UIScreen_InternalRouteInput(UIScreen* self, ProgramTime time, const UIScreenInput* input);

/** Moves focus to a widget id (WIDGET_ID_INVALID clears it) without the public argument checks. The old widget's hooks
 *  run first and a change made by a hook wins. A target that cannot take focus by then (UIScreen_InternalCanFocus:
 *  destroyed, detached or disabled, also by a hook) leaves nothing focused, so focus is never on an ineligible widget. */
Error UIScreen_InternalSetFocus(UIScreen* self, uint64_t widgetId);

/** Returns whether a widget can take focus now: alive, attached, focusable and input-enabled. */
bool UIScreen_InternalCanFocus(const Widget* widget);


// Screen: keyboard navigation (UIScreenNavigation.c).
/** Allocates the navigation module's buffers (screen construction). */
void UIScreen_InternalInitializeNavigation(UIScreen* self);

/** Frees the navigation module's buffers (screen teardown). */
void UIScreen_InternalReleaseNavigation(UIScreen* self);

/** Acts on the navigation actions nobody handled, consuming the ones it uses. */
Error UIScreen_InternalRouteNavigation(UIScreen* self);

/** Ends keyboard navigation (pointer movement or a click): leaves every entered widget and stops following the held
 *  navigation keys; focus stays. */
Error UIScreen_InternalStopNavigation(UIScreen* self);


// Screen: animations (UIScreenAnimation.c).
/** Allocates the animation module's storage (screen construction). */
Error UIScreen_InternalInitializeAnimations(UIScreen* self);

/** Advances every running animation by the tick; failing ones stop and are logged once. */
Error UIScreen_InternalAdvanceAnimations(UIScreen* self, ProgramTime time);

/** Stops the animations of widgets that are dying. */
void UIScreen_InternalStopDyingAnimations(UIScreen* self);

/** Releases every animation and the keyframe storage (screen teardown); keeps the first error. */
Error UIScreen_InternalReleaseAnimations(UIScreen* self);


// Screen: notifications (UIScreenEvents.c).
/** Allocates the notification module's buffers (screen construction). */
void UIScreen_InternalInitializeEvents(UIScreen* self);

/** Queues a notification; returns false if the queue could not grow. Ignored (true) while the screen is torn down. */
bool UIScreen_InternalPostEvent(UIScreen* self, const UIEvent* event);

/** Delivers the queued notifications, at most UI_SCREEN_MAX_EVENTS_PER_DISPATCH. */
Error UIScreen_InternalDispatchEvents(UIScreen* self);

/** Ends the subscriptions to one widget (it is being torn down). */
void UIScreen_InternalRemoveWidgetSubscriptions(UIScreen* self, uint64_t widgetId);

/** Releases the queue and every subscription (screen teardown). */
void UIScreen_InternalReleaseEvents(UIScreen* self);


// Widget base (UIWidget.c).
/** Initializes the base of freshly borrowed widget storage: defaults, no id yet, empty child lists. Cannot fail. */
void Widget_InternalInitializeBase(Widget* self, const WidgetVTable* vtable, UIScreen* screen, uint64_t typeId,
    bool isFocusable);

/** Frees the base's own storage (the child lists). */
void Widget_InternalReleaseBase(Widget* self);

/** Runs a dying widget's teardown hooks: the disable hooks and Deinitialize if it was initialized, then OnDeconstruct.
 *  Keeps the first error. */
Error Widget_InternalRunTeardownHooks(Widget* self);

/** Sets the hover state: notification, hook, active state. */
Error Widget_InternalSetHovered(Widget* self, bool isHovered, const WidgetHoverArgs* args);

/** Sets the focus state: notification, then hook. */
Error Widget_InternalSetFocused(Widget* self, bool isFocused);

/** Sets the tabbed state: notification, hook, active state. */
Error Widget_InternalSetTabbed(Widget* self, bool isTabbed);

/** Marks a widget as a root (with its insertion sequence) or not. */
void Widget_InternalSetScreenRoot(Widget* self, bool isScreenRoot, uint64_t sequence);

/** Removes a child from its parent's lists and clears its parent (bounds invalidated up the old parent's chain). */
void Widget_InternalUnlinkFromParent(Widget* child);

/** Inserts a widget into a draw-order list (Widget*, ascending z then sequence) at its place. */
bool Widget_InternalInsertIntoDrawOrder(GenericBuffer* drawOrder, Widget* widget);

/** Removes a widget from a draw-order list (nothing happens when it is not in it). */
void Widget_InternalRemoveFromDrawOrder(GenericBuffer* drawOrder, const Widget* widget);

/** Moves a widget to its place in a draw-order list after its z changed. */
void Widget_InternalResortInDrawOrder(GenericBuffer* drawOrder, Widget* widget);

/** Returns the widget at an index of a draw-order list. */
Widget* Widget_InternalGetDrawOrderAt(const GenericBuffer* drawOrder, size_t index);

/** Returns the estimated bounds Widget_GetEstimatedBounds would, without writing any cache (for Render). */
Rectangle Widget_InternalPeekEstimatedBounds(const Widget* self);


// Factory (UIWidgetFactory.c).
/** Returns a torn-down widget's storage to its type's pool and counts it out. */
Error UIWidgetFactory_InternalReturnWidget(UIWidgetFactory* self, Widget* widget);
