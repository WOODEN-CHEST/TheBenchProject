#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "GameKey.h"
#include "UIElement.h"
#include "UIScrollable.h"
#include "UIWidget.h"


/**
 * @file UIScrollbar.h
 * @brief The scroll bar element: a track and a thumb along one axis that show and move another element's scrollable piece.
 *
 * A bar is bound to a target, an element holding a scrollable (UIScrollable.h): a scroll area makes its two, a dropdown's open list
 * one, and calling code can bind one anywhere. The thumb's length is the view's share of the content, never shorter than the look's
 * minimum; its place along the track is the offset's share of the range. A left press on the thumb holds it where it was grabbed and
 * a drag moves it, past the bar and past the window too; a left press on the track pages toward the pointer, by the view's length,
 * and while it is held pages again after a short wait until the thumb reaches the pointer. The wheel over the bar scrolls its target
 * along the bar's axis. With nothing to scroll the thumb fills the track and presses do nothing. A bar is not focusable. It posts
 * the events every element shares (UIElement_PostMouseEvents) and its cues; the scrolling itself is the target's.
 *
 * Calling code uses the UIScrollbar_* functions by widget id. A look (UIKit_RegisterLook: a struct starting with
 * UIScrollbarLookBase) draws the track and the thumb (UIScrollbarCapability_GetThumbBox) and tells the behaviour its minimum thumb
 * length (UIScrollbarCapability_SetMinThumbLength). The thumb's arithmetic is pure, at the end of this file.
 */


// Macros.
/** @brief How long a held press on the track waits before it pages again, in seconds ... */
#define UI_SCROLLBAR_PAGE_DELAY_SECONDS 0.35
/** @brief ... and how often it pages after that, in seconds. */
#define UI_SCROLLBAR_PAGE_REPEAT_SECONDS 0.06


// Types.
/**
 * @brief What a scroll bar is created with (UIKit_CreateScrollbar). A plain value; build one with UIScrollbarOptions_Create.
 */
typedef struct UIScrollbarOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief The axis it scrolls along. */
    UIScrollAxis Axis;
    /** @brief The element whose scrollable it moves, or WIDGET_ID_INVALID to bind one later (UIScrollbar_SetTarget). */
    uint64_t TargetId;
} UIScrollbarOptions;

/**
 * @brief What a scroll bar look's Construct gets (UIKit_CreateScrollbar passes it): the kit and the options.
 */
typedef struct UIScrollbarArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIScrollbarOptions* Options;
} UIScrollbarArgs;

/**
 * @brief A scroll bar's capability: its logical state, no look. Embedded by a look's widget type right after its Widget
 *        (UIScrollbarLookBase); its fields are read-only outside the bar's module.
 */
typedef struct UIScrollbarCapabilityStruct
{
    /** @brief The element whose scrollable it moves, or WIDGET_ID_INVALID. */
    uint64_t _targetId;
    /** @brief The axis it scrolls along. */
    UIScrollAxis _axis;
    /** @brief The shortest the thumb gets, in the bar's own UI pixels, as its look said. */
    float _minThumbLength;
    /** @brief How much of the bar's end is no track, in its own UI pixels: the corner where a scroll area's other bar meets it, which its
     *  track covers but its thumb never enters. */
    float _endInset;
    /** @brief A left press on the thumb is being dragged. */
    bool _isDragging;
    /** @brief How far along the track from the thumb's start the drag holds it, in surface pixels. */
    float _grabOffsetPixels;
    /** @brief A left press on the track is held: it pages toward the pointer. */
    bool _isPaging;
    /** @brief Where along the track the pointer of that press is, in surface pixels from the track's start. */
    float _pagePointerPixels;
    /** @brief Which way it pages: -1 toward the content's start, 1 toward its end. */
    float _pageDirection;
    /** @brief How long until it pages again, in seconds. */
    double _pageCountdownSeconds;
    /** @brief Whether the pointer is over the thumb (worked out each update, for the look). */
    bool _isPointerOnThumb;
} UIScrollbarCapability;

/**
 * @brief How every scroll bar look's widget struct starts: the widget, then the capability.
 */
typedef struct UIScrollbarLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The bar's behaviour. */
    UIScrollbarCapability Scrollbar;
} UIScrollbarLookBase;


// Helpers.
/**
 * @brief Default options: vertical, bound to nothing.
 * @returns The options.
 */
static inline UIScrollbarOptions UIScrollbarOptions_Create(void)
{
    return (UIScrollbarOptions){ .Element = UIElementOptions_Create(), .Axis = UIScrollAxis_Vertical, .TargetId = 0U };
}


// Calling code: by widget id.
/**
 * @brief Binds a scroll bar to the element whose scrollable it shows and moves; a drag or a paging press in progress ends.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param targetId The element, or WIDGET_ID_INVALID for none (the thumb then fills the track).
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no scroll bar.
 */
Error UIScrollbar_SetTarget(UIScreen* screen, uint64_t widgetId, uint64_t targetId);

/**
 * @brief Reads the element a scroll bar is bound to.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param outTargetId [out] Receives it, or WIDGET_ID_INVALID. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no scroll bar.
 */
Error UIScrollbar_GetTarget(UIScreen* screen, uint64_t widgetId, uint64_t* outTargetId);


// Behaviour helpers, for looks and tests.
/**
 * @brief Constructs the capability from the kind's arguments.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The arguments; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an unknown axis; otherwise the options' error.
 */
Error UIScrollbarCapability_Construct(UIScrollbarCapability* self, Widget* widget, const UIScrollbarArgs* args);

/**
 * @brief The behaviour's part of a tick: a held press on the track pages again when its wait is over, a press whose release cannot
 *        reach the bar any more ends, and whether the pointer is over the thumb is worked out.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param time The tick's time.
 */
void UIScrollbarCapability_Update(UIScrollbarCapability* self, Widget* widget, ProgramTime time);

/**
 * @brief Tells the behaviour the shortest the look's thumb gets; call it from the look's Update when it can change.
 * @param self The capability; must not be NULL.
 * @param length The length in the bar's own UI pixels; a value that is not a finite number >= 0 counts as 0.
 */
void UIScrollbarCapability_SetMinThumbLength(UIScrollbarCapability* self, float length);

/**
 * @brief Tells the bar how much of its end is no track: a scroll area's corner, where the other bar meets it. Its track still covers
 *        it (so content never shows there), and its thumb never enters it.
 * @param self The capability; must not be NULL.
 * @param inset The length in the bar's own UI pixels; a value that is not a finite number >= 0 counts as 0.
 */
void UIScrollbarCapability_SetEndInset(UIScrollbarCapability* self, float inset);

/**
 * @brief Finds where the thumb is: along the track (the box less the end inset) as the target's scrollable says, across the track less
 *        an inset on each side.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param track The track's box in surface pixels (the bar's own box for the standard look).
 * @param insetPixels The gap between the thumb and the track's sides across, in surface pixels.
 * @param outThumb [out] Receives the thumb's box in surface pixels. Must not be NULL.
 * @returns true when there is something to scroll; false fills the track (the box is still written).
 */
bool UIScrollbarCapability_GetThumbBox(const UIScrollbarCapability* self, const Widget* widget, Rectangle track, float insetPixels,
    Rectangle* outThumb);

/**
 * @brief Returns the state a look shows the thumb in: pressed while dragged, hovered under the pointer, else normal.
 * @param self The capability; must not be NULL.
 * @returns The state.
 */
UIElementState UIScrollbarCapability_GetThumbState(const UIScrollbarCapability* self);

/**
 * @brief Interprets mouse input.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIScrollbarCapability_OnMouseInput(UIScrollbarCapability* self, Widget* widget, const WidgetMouseInputArgs* args);

/**
 * @brief Ends a drag or a paging press the screen says is lost.
 * @param self The capability; must not be NULL.
 * @param button The press's mouse button.
 */
void UIScrollbarCapability_OnPressLost(UIScrollbarCapability* self, GameKey button);

/**
 * @brief Interprets the wheel: it scrolls the target along the bar's axis, handled when it moved.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when the target scrolled. Must not be NULL.
 */
void UIScrollbarCapability_OnScrollInput(UIScrollbarCapability* self, Widget* widget, const WidgetScrollInputArgs* args, bool* outIsHandled);


// The thumb's arithmetic.
/**
 * @brief Finds the thumb along a track: its length is the view's share of the content, at least @p minThumbLength (but never past
 *        the track), and its start is the offset's share of the range over the track less the thumb.
 * @param trackLength The track's length; a value that is not > 0 gives an empty thumb at 0.
 * @param minThumbLength The shortest thumb.
 * @param contentLength The content's length.
 * @param viewLength The view's length.
 * @param offset The offset into the content.
 * @param outStart [out] Receives the thumb's start from the track's. Must not be NULL.
 * @param outLength [out] Receives its length; the whole track when the content is no longer than the view. Must not be NULL.
 */
void UIScrollbar_GetThumb(float trackLength, float minThumbLength, float contentLength, float viewLength, float offset, float* outStart,
    float* outLength);

/**
 * @brief Returns the offset that puts the thumb's start at a place along the track: the inverse of UIScrollbar_GetThumb.
 * @param trackLength The track's length.
 * @param thumbLength The thumb's length.
 * @param range How far the content scrolls.
 * @param thumbStart Where the thumb's start would be.
 * @returns The offset, within [0, range]; 0 when the thumb fills the track.
 */
float UIScrollbar_GetOffsetAt(float trackLength, float thumbLength, float range, float thumbStart);
