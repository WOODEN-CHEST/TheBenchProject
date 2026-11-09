#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
// Rectangle and Vector2 are the anchor's box, sizes and the surface.
#include "raylib/raylib.h"


/**
 * @file UIPopup.h
 * @brief The popup layer: widgets that open above everything (a dropdown's list, a menu, a modal dialog), placed against an anchor,
 *        closing on a press outside, on Escape or when their owner goes away.
 *
 * A popup is a root of the screen in the popup layer (UIScreenLayer_Popup), so no panel's clip ever cuts it, opened with
 * UIScreen_OpenPopup and closed by the screen or with UIScreen_ClosePopup. Each update, before the layout, the screen places it
 * against its anchor (a box on the screen, or a widget's box followed while it lives) on its preferred side, flips it to the other
 * side when it does not fit there and fits better there, shrinks it to the room if it may, and keeps it on the surface; so it keeps
 * its place when the window changes size. Its size is its root's preferred size (Widget_Measure), at least the anchor's width when
 * asked; kept narrower than that by the surface, it is asked again at the width it gets, so what wraps is as tall as it will be. It is
 * scaled like its owner unless its options say otherwise: a popup of a zoomed element is zoomed with it.
 *
 * CLOSING. A press outside it closes it and goes on to the widget behind (a modal popup's backdrop takes that press instead); a press
 * on its owner counts as inside when it says so (a dropdown's head toggles its own list). Escape that the focused widget and its
 * ancestors leave unused closes the topmost popup when it closes on Escape, leaving the element entered inside it with a cancel first;
 * a topmost popup that refuses Escape keeps every popup under it open. A popup whose owner is destroyed, detached or hidden closes at
 * the next update. Popups nest (a submenu over its menu): closing one closes those opened above it first, from the top down, and a
 * press inside a popup closes only those above it. A popup that closes posts the Close cue (UIElementCue_Close) for its root, gives the
 * focus back to the widget that had it when it opened (entered again if it was entered: a text box under its context menu) when the
 * focus is inside it or nowhere, unless a press that goes on closed it (that press puts the focus where it lands), tells its owner
 * (UIEventKind_PopupClosed, with the reason as its WidgetEventId) and is destroyed with its subtree.
 *
 * FOCUS. Opening moves the focus in: to the widget the options name (entered at once if it is an edit element, as a dropdown's list
 * is, so its keys work), or nowhere; a popup may leave the focus where it is instead (a submenu opened by the pointer resting).
 * While popups are open keyboard navigation stays inside the topmost one.
 *
 * MODAL. A modal popup opens over a backdrop that covers the whole surface below it and takes every press, the wheel and hover, so
 * nothing behind takes a click (from the update it opens in on, before the backdrop's first layout too); the backdrop is a widget of
 * the type the factory names (UIWidgetFactory_SetBackdropType; the kit registers one, themed by the theme's backdrop kind).
 *
 * The placement's arithmetic is pure (UIPopupPlacement_Resolve).
 */


// Forward declarations.
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;


// Macros.
/** @brief The most popups a screen has open at once. */
#define UI_SCREEN_MAX_POPUPS ((size_t)32)


// Types.
/**
 * @brief Where a popup opens against its anchor.
 */
typedef enum UIPopupSideEnum
{
    /** @brief Under it, its left edges lined up: a dropdown's list, a menu bar's menu. */
    UIPopupSide_Below,
    /** @brief Over it, its left edges lined up. */
    UIPopupSide_Above,
    /** @brief At its right, its top edges lined up: a submenu. */
    UIPopupSide_Right,
    /** @brief At its left, its top edges lined up. */
    UIPopupSide_Left,
    /** @brief Centred on it, never flipped: a dialog (an anchor box of the whole surface, { 0, 0, 1, 1 }, centres it on the screen). */
    UIPopupSide_Center
} UIPopupSide;

/**
 * @brief Why a popup closed: the WidgetEventId of the UIEventKind_PopupClosed notification its owner gets.
 */
typedef enum UIPopupCloseReasonEnum
{
    /** @brief Code closed it (UIScreen_ClosePopup), or its root was destroyed. */
    UIPopupCloseReason_Code,
    /** @brief A press outside it. */
    UIPopupCloseReason_PressOutside,
    /** @brief Escape. */
    UIPopupCloseReason_Escape,
    /** @brief Its owner was destroyed, detached or hidden. */
    UIPopupCloseReason_OwnerGone
} UIPopupCloseReason;

/**
 * @brief How a popup opens. A plain value; build one with UIPopupOptions_Create.
 */
typedef struct UIPopupOptionsStruct
{
    /** @brief The anchor's box in normalized screen coordinates ([0;1] of the surface; a point is a box of no size). Used while
     *  AnchorWidgetId names no live widget. */
    Rectangle AnchorBox;
    /** @brief A widget whose box is the anchor, followed each update while it is alive and attached (its last box stays the anchor
     *  after that), or WIDGET_ID_INVALID for AnchorBox alone. */
    uint64_t AnchorWidgetId;
    /** @brief The side it opens on. */
    UIPopupSide Side;
    /** @brief Whether it opens on the other side when it does not fit on its own and fits there, or there is more room there. */
    bool Flips;
    /** @brief Whether it is made shorter to fit the room on its side (a long list that scrolls), down to MinimumLength. */
    bool AllowsShrinking;
    /** @brief The shortest it is made when it shrinks, in UI pixels; finite and >= 0. Past it the popup moves to stay on the surface. */
    float MinimumLength;
    /** @brief Whether it is at least as wide as its anchor (on the side below or above). */
    bool MatchesAnchorWidth;
    /** @brief The widget it belongs to: told when it closes, and it closes when the widget goes; WIDGET_ID_INVALID for none. */
    uint64_t OwnerId;
    /** @brief Whether a press on its owner (or inside it) counts as inside the popup: it does not close it, and the owner decides. */
    bool IsOwnerInside;
    /** @brief Whether its own scale follows its owner's effective scale (as a share of the screen's), each update while the owner is
     *  there: a popup of a zoomed element is zoomed with it. Otherwise its own scale is calling code's. */
    bool IsScaledLikeOwner;
    /** @brief Whether a backdrop under it takes every press, the wheel and hover over the rest of the surface. */
    bool IsModal;
    /** @brief Whether Escape closes it, while it is the topmost popup (one that refuses Escape keeps those under it open too). */
    bool ClosesOnEscape;
    /** @brief Whether opening moves the focus in (to FocusId, or nowhere); otherwise the focus stays where it is. */
    bool MovesFocus;
    /** @brief The widget focused when it opens, entered at once if it is an edit element; WIDGET_ID_INVALID for nothing focused. */
    uint64_t FocusId;
} UIPopupOptions;

/**
 * @brief The rules UIPopupPlacement_Resolve places by, in surface pixels. A plain value.
 */
typedef struct UIPopupPlacementRulesStruct
{
    /** @brief The side it opens on. */
    UIPopupSide Side;
    /** @brief Whether it may open on the other side. */
    bool Flips;
    /** @brief Whether it may be made shorter to fit. */
    bool AllowsShrinking;
    /** @brief The shortest it is made when it shrinks, in surface pixels. */
    float MinimumLength;
} UIPopupPlacementRules;


// Helpers.
/**
 * @brief Default options: below a box of no size at the surface's top-left, flipping when it does not fit, never shrinking, closed by
 *        Escape, moving the focus to nothing, owned by nobody, scaled like its owner.
 * @returns The options.
 */
static inline UIPopupOptions UIPopupOptions_Create(void)
{
    return (UIPopupOptions)
    {
        .AnchorBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f },
        .AnchorWidgetId = 0U,
        .Side = UIPopupSide_Below,
        .Flips = true,
        .AllowsShrinking = false,
        .MinimumLength = 0.0f,
        .MatchesAnchorWidth = false,
        .OwnerId = 0U,
        .IsOwnerInside = false,
        .IsScaledLikeOwner = true,
        .IsModal = false,
        .ClosesOnEscape = true,
        .MovesFocus = true,
        .FocusId = 0U
    };
}


// Functions.
/**
 * @brief Opens a widget as a popup: it becomes a root of the screen in the popup layer, above every popup open before it, and the
 *        focus moves in as the options say. It is placed at the next update, before the layout.
 *
 * A modal popup gets a backdrop under it first, of the factory's backdrop type. Safe from hooks and handlers (a press, a key, a
 * notification); the popup takes input from the next update on.
 * @param screen The screen; must not be NULL.
 * @param rootWidgetId The popup: an alive, initialized widget of the screen that is detached or a root (not a child), and no open
 *        popup's root already.
 * @param options How it opens; must not be NULL. Copied.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_ArgumentOutOfRange for options that are not valid (an
 *          unknown side, an anchor box that is not finite or of negative size, a minimum length that is not a finite number >= 0);
 *          ErrorCode_InvalidOperation if the widget cannot be opened (gone, a child, open already), UI_SCREEN_MAX_POPUPS are open, or a
 *          modal popup has no backdrop type to stand on; ErrorCode_BufferTooLarge if the screen could not record it; otherwise a hook's
 *          error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_OpenPopup(UIScreen* screen, uint64_t rootWidgetId, const UIPopupOptions* options);

/**
 * @brief Closes a popup and every popup opened above it, from the top down (UIPopupCloseReason_Code). Safe from hooks and handlers.
 * @param screen The screen; must not be NULL.
 * @param rootWidgetId An open popup's root.
 * @returns Success; ErrorCode_IllegalArgument if @p screen is NULL; ErrorCode_InvalidOperation if no open popup has that root; otherwise
 *          a hook's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_ClosePopup(UIScreen* screen, uint64_t rootWidgetId);

/**
 * @brief Closes a popup and the popups opened from inside it (each whose owner lies in its subtree, or in one of theirs), from the top down
 *        (UIPopupCloseReason_Code), and leaves every other popup open where it is: a dialog closing after its answer keeps the dialog its
 *        answer opened. A popup left open whose focus would go back into the closed one gives it back where the closed one would have.
 *        Safe from hooks and handlers.
 * @param screen The screen; must not be NULL.
 * @param rootWidgetId An open popup's root.
 * @returns Success; ErrorCode_IllegalArgument if @p screen is NULL; ErrorCode_InvalidOperation if no open popup has that root; otherwise
 *          a hook's error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIScreen_ClosePopupAndItsOwn(UIScreen* screen, uint64_t rootWidgetId);

/**
 * @brief Closes every open popup, from the top down (UIPopupCloseReason_Code). Safe from hooks and handlers.
 * @param screen The screen; must not be NULL.
 * @returns Success (also with none open); ErrorCode_IllegalArgument if @p screen is NULL; otherwise a hook's error.
 */
Error UIScreen_CloseAllPopups(UIScreen* screen);

/**
 * @brief Returns whether a widget is an open popup's root.
 * @param screen The screen; must not be NULL.
 * @param rootWidgetId The widget.
 * @returns true if it is.
 */
bool UIScreen_IsPopupOpen(const UIScreen* screen, uint64_t rootWidgetId);

/**
 * @brief Returns how many popups are open.
 * @param screen The screen; must not be NULL.
 * @returns The count.
 */
size_t UIScreen_GetPopupCount(const UIScreen* screen);

/**
 * @brief Returns an open popup's root, the first opened first.
 * @param screen The screen; must not be NULL.
 * @param index In [0, UIScreen_GetPopupCount).
 * @returns The root's id, or WIDGET_ID_INVALID for an index out of range.
 */
uint64_t UIScreen_GetPopupAt(const UIScreen* screen, size_t index);

/**
 * @brief Finds the open popup a widget lies in.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 * @returns The popup's root, or WIDGET_ID_INVALID when the widget is in none (or gone).
 */
uint64_t UIScreen_FindPopupOf(const UIScreen* screen, uint64_t widgetId);

/**
 * @brief Places a box against an anchor on a surface, all in surface pixels: on the rules' side, flipped to the other when it does not
 *        fit there and fits (or has more room) on the other and the rules allow it, made shorter to the room on its side when they allow
 *        it (never below their minimum, nor below nothing), then moved to stay on the surface, every edge on a whole pixel. Pure.
 * @param anchor The anchor's box; a box of no size is a point.
 * @param size The box's size; a component that is not a finite number >= 0 counts as 0.
 * @param surface The surface's size.
 * @param rules How it is placed; must not be NULL.
 * @returns The box.
 */
Rectangle UIPopupPlacement_Resolve(Rectangle anchor, Vector2 size, Vector2 surface, const UIPopupPlacementRules* rules);
