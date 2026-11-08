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

/** Widget_MoveTo's work, its checks done: links the widget under @p newParent (NULL: the roots), with room for it made first so a
 *  failure changes nothing, then brings navigation, the focus and the presses in line with where it is now. */
Error UIScreen_InternalMoveWidget(UIScreen* self, Widget* widget, Widget* newParent);

/** Keeps the draw order of a widget's siblings sorted after its z changed. */
void UIScreen_InternalOnZChanged(UIScreen* self, Widget* widget);

/** The one detach path for an alive widget (a root or a child): unlinks it, then ends hover, the entered element, focus,
 *  navigation and mouse presses inside its subtree, running the hooks (OnLeave and OnPressLost among them) last with every
 *  widget re-resolved by id. */
Error UIScreen_InternalDetachWidget(UIScreen* self, Widget* widget);

/** Widget_Destroy's work: unlinks the subtree, marks it dying, forgets every reference into it (no hooks) and queues it
 *  for the flush. */
Error UIScreen_InternalDestroyWidget(UIScreen* self, Widget* widget);

/** Returns whether @p node is @p ancestor or lies in its subtree. */
bool UIScreen_InternalIsInSubtree(const Widget* node, const Widget* ancestor);

/** Returns whether a widget is attached to one of its screen's roots (so it is updated, laid out and drawn). */
bool UIScreen_InternalIsAttached(const Widget* widget);

/**
 * @brief Whether a widget and every widget above it are rendered: a hidden widget hides what it holds.
 * @param widget The widget; must not be NULL.
 * @returns true when nothing on its way up is hidden.
 */
bool UIScreen_InternalIsShown(const Widget* widget);

/** Returns whether a widget stands under a root of the overlay layer, which takes no input. */
bool UIScreen_InternalIsInOverlay(const Widget* widget);

/** Composes a widget's absolute box (normalized screen coordinates) from its parent chain. */
void UIScreen_InternalGetAbsoluteBox(const Widget* widget, Vector2* outPosition, Vector2* outSize);

/** Returns a widget's estimated bounds (it and its subtree) in absolute screen coordinates, caching them (update only). */
Rectangle UIScreen_InternalGetEstimatedScreenBox(Widget* widget);

/** UIScreen_InternalGetEstimatedScreenBox without writing any cache, for Render. */
Rectangle UIScreen_InternalPeekEstimatedScreenBox(const Widget* widget);


// Screen: input and focus (UIScreenInput.c).
/** Routes the tick's input: pointer, buttons, wheel, then keys, text and navigation. */
Error UIScreen_InternalRouteInput(UIScreen* self, ProgramTime time, const UIScreenInput* input);

/** Moves focus to a widget id (WIDGET_ID_INVALID clears it) without the public argument checks. The entered edit element is
 *  left first (keeping), then the old widget's hooks run, and a change made by a hook wins. A target that cannot take focus by
 *  then (UIScreen_InternalCanFocus: destroyed, detached or disabled, also by a hook) leaves nothing focused, so focus is never
 *  on an ineligible widget. */
Error UIScreen_InternalSetFocus(UIScreen* self, uint64_t widgetId);

/** Returns whether a widget can take focus now: alive, attached, focusable, input-enabled, shown and not in an overlay. */
bool UIScreen_InternalCanFocus(const Widget* widget);

/** Returns whether a widget a press or a held key began on is still there for what follows: attached, taking input and shown. */
bool UIScreen_InternalTakesPressedInput(const Widget* widget);

/* Whether a press or entering it brings a widget to the top of its siblings: a root only in the layers made for that. */
bool UIScreen_InternalIsRaisedByInput(const Widget* widget);

/** Forgets a button's press, and with its holder's last press the hidden-cursor drag it asked for. Runs no hook. */
void UIScreen_InternalEndPress(UIScreen* self, size_t buttonIndex);

/** After something stopped taking input or showing: takes the focus away from a widget that cannot have it any more, and ends each
 *  press whose widget cannot take it any more, telling those it had reached (OnPressLost). Keeps the first error. */
Error UIScreen_InternalReleaseUnreachable(UIScreen* self);

/** Returns whether a key is one of those an action's bindings match this tick, consumed or not. */
bool UIScreen_InternalIsActionKey(const GameInput* input, GameActionId action, GameKey key);

/** Returns whether a widget can be entered now: an edit element that can take focus. */
bool UIScreen_InternalCanEnter(const Widget* widget);

/** Enters the focused widget when it can be entered: the screen's entered widget is set first, then the widget's flag, its
 *  notification and OnEnter. A widget that is not focused, cannot be entered or is entered already changes nothing. */
Error UIScreen_InternalEnter(UIScreen* self, uint64_t widgetId, UIEnterCause cause);

/** Leaves the entered widget, if any: the screen forgets it first, then the widget's flag, its notification and OnLeave. */
Error UIScreen_InternalLeave(UIScreen* self, UILeaveReason reason);


// Screen: keyboard navigation (UIScreenNavigation.c).
/** Allocates the navigation module's buffers (screen construction). */
void UIScreen_InternalInitializeNavigation(UIScreen* self);

/** Frees the navigation module's buffers (screen teardown). */
void UIScreen_InternalReleaseNavigation(UIScreen* self);

/** Acts on the navigation actions nobody handled, consuming the ones it uses. */
Error UIScreen_InternalRouteNavigation(UIScreen* self);

/** Ends keyboard navigation (pointer movement or a click): leaves every widget navigation entered and stops following the held
 *  navigation keys; focus stays, and so does an entered edit element. */
Error UIScreen_InternalStopNavigation(UIScreen* self);

/** A widget moved in the tree: the entered path stays a path down it. It follows focus, or with nothing focused what no longer
 *  follows down the tree is left. Does nothing outside navigation. */
Error UIScreen_InternalOnWidgetMoved(UIScreen* self);


// Screen: animations (UIScreenAnimation.c).
/** Allocates the animation module's storage (screen construction). */
Error UIScreen_InternalInitializeAnimations(UIScreen* self);

/** Advances every running animation by the tick; failing ones stop and are logged once. */
Error UIScreen_InternalAdvanceAnimations(UIScreen* self, ProgramTime time);

/** Stops the animations of widgets that are dying. */
void UIScreen_InternalStopDyingAnimations(UIScreen* self);

/** Releases every animation and the keyframe storage (screen teardown); keeps the first error. */
Error UIScreen_InternalReleaseAnimations(UIScreen* self);


// Screen: hover text (UIScreenTooltips.c).
/** Allocates hover text's table and text (screen construction). */
void UIScreen_InternalInitializeTooltips(UIScreen* self);

/** Frees them (screen teardown). */
void UIScreen_InternalReleaseTooltips(UIScreen* self);

/** Works out where the pointer rests after the tick's input: the text there, the rest time, whether it shows; makes the widget that
 *  shows it the first time one does. */
Error UIScreen_InternalUpdateTooltip(UIScreen* self, ProgramTime time);

/** Forgets a widget's hover text (it is being torn down, or its text cleared); what shows goes with it when it is the current one. */
void UIScreen_InternalRemoveWidgetTooltip(UIScreen* self, uint64_t widgetId);

/** Forgets what hover text is current: none until the next update finds where the pointer rests (its widget is dying). */
void UIScreen_InternalForgetCurrentTooltip(UIScreen* self);

/** The widget whose hover text is current, or WIDGET_ID_INVALID. */
uint64_t UIScreen_InternalGetCurrentTooltipWidgetId(const UIScreen* self);


// Screen: popups (UIScreenPopups.c).
/** Allocates the popups' list (screen construction). */
void UIScreen_InternalInitializePopups(UIScreen* self);

/** Frees it (screen teardown; the widgets go with the screen's). */
void UIScreen_InternalReleasePopups(UIScreen* self);

/** Before the layout: closes the popups whose owner or root is gone, then places each against its anchor. */
Error UIScreen_InternalPlacePopups(UIScreen* self);

/** A press on @p targetId (WIDGET_ID_INVALID: on no widget) while popups are open: closes those it is outside of, from the top down.
 *  @p outIsSwallowed is set when it landed on a modal popup's backdrop: nothing else is done with it. @p outIsBlocked is set when it
 *  hit no widget while a modal popup is open: it is not this screen's to clear the focus with. */
Error UIScreen_InternalClosePopupsForPress(UIScreen* self, uint64_t targetId, bool* outIsSwallowed, bool* outIsBlocked);

/** The back action's press nobody used while popups are open: leaves the element entered in the topmost popup that closes on Escape
 *  with a cancel and closes that popup. @p outIsActed is set when it did. */
Error UIScreen_InternalRoutePopupEscape(UIScreen* self, bool* outIsActed);

/** The root keyboard navigation's top level is confined to while popups are open (the topmost one), or NULL. */
Widget* UIScreen_InternalGetNavigationRoot(const UIScreen* self);

/** Whether any popup is open. */
bool UIScreen_InternalHasPopups(const UIScreen* self);


// Screen: context menus (UIScreenContextMenus.c).
/** Allocates the side table (screen construction). */
void UIScreen_InternalInitializeContextMenus(UIScreen* self);

/** Frees it (screen teardown). */
void UIScreen_InternalReleaseContextMenus(UIScreen* self);

/** Forgets a widget's provider (it is being torn down). */
void UIScreen_InternalRemoveWidgetContextMenu(UIScreen* self, uint64_t widgetId);


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
/** The defaults a widget type gives its widgets' flags. */
typedef struct WidgetTypeFlagsStruct
{
    bool IsFocusable;
    bool NeedsEntering;
    bool EntersItself;
} WidgetTypeFlags;

/** Initializes the base of freshly borrowed widget storage: defaults (the type's flags), no id yet, empty child lists. Cannot
 *  fail. */
void Widget_InternalInitializeBase(Widget* self, const WidgetVTable* vtable, UIScreen* screen, uint64_t typeId,
    const WidgetTypeFlags* flags);

/** Frees the base's own storage (the child lists). */
void Widget_InternalReleaseBase(Widget* self);

/** Brings the effective scales of a widget and its subtree in line with its parent's (or the screen's, for a root): each
 *  one's parent's times its own, kept to the bounds, with the cached measure of each that changed dropped. Called wherever one
 *  of them can change: a widget's scale, the screen's, and a widget attached anywhere. O(subtree). */
void Widget_InternalUpdateEffectiveScale(Widget* self, float parentScale);

/** Returns the effective scale a widget's placement offsets and render transform offset count in: its parent's, or the
 *  screen's for a root or a detached widget. */
float Widget_InternalGetParentScale(const Widget* self);

/** Runs a dying widget's teardown hooks: the disable hooks and Deinitialize if it was initialized, then OnDeconstruct.
 *  Keeps the first error. */
Error Widget_InternalRunTeardownHooks(Widget* self);

/** Sets the hover state: notification, hook, active state. */
Error Widget_InternalSetHovered(Widget* self, bool isHovered, const WidgetHoverArgs* args);

/** Sets the focus state: notification, then hook. */
Error Widget_InternalSetFocused(Widget* self, bool isFocused);

/** Sets the tabbed state: notification, hook, active state. */
Error Widget_InternalSetTabbed(Widget* self, bool isTabbed);

/** Sets the entered state: the flag, the notification, then OnEnter. Nothing happens to an entered or dying widget. */
Error Widget_InternalEnter(Widget* self, UIEnterCause cause);

/** Clears the entered state: the flag, the notification, then OnLeave. Nothing happens to a widget that is not entered or is
 *  dying. */
Error Widget_InternalLeave(Widget* self, UILeaveReason reason);

/** Marks a widget as a root (with its insertion sequence) or not. */
void Widget_InternalSetScreenRoot(Widget* self, bool isScreenRoot, uint64_t sequence);

/** Removes a child from its parent's lists and clears its parent (bounds invalidated up the old parent's chain). */
void Widget_InternalUnlinkFromParent(Widget* child);

/** Links a detached widget under a parent, on top of its siblings of equal z, invalidating bounds and measures up the chain, and
 *  the hit test. Returns false, with nothing changed, when the parent's lists could not grow. */
bool Widget_InternalLinkToParent(Widget* parent, Widget* child);

/** Returns the layer a widget draws in among its siblings: a root's own; every child's is the same. */
UIScreenLayer Widget_InternalGetDrawLayer(const Widget* self);

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
