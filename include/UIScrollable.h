#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
// Vector2 and Rectangle are the offsets and the boxes it works in.
#include "raylib/raylib.h"


/**
 * @file UIScrollable.h
 * @brief The scrollable piece: content larger than the view that shows it, how far it is scrolled, and which bars show.
 *
 * Every element whose content can be larger than its box holds one (a scroll area, a dropdown's open list, later a text box, a
 * list, a window): the content's size and the view's in UI pixels (the element's own), the offset of the view into the content,
 * and per axis a mode: the axis does not scroll, it scrolls with a bar while the content is longer than the view, it scrolls with
 * a bar always, or it scrolls with no bar. The offset is always within [0, content - view] per axis: it clamps whenever the content or the view changes. The
 * element that holds the piece measures and places its content and tells the piece the sizes each layout; calling code moves the
 * view by widget id (UIScrollable_SetOffset, _ScrollBy, _EnsureVisible, _RevealWidget). Scroll bars (UIScrollbar.h) read and move it.
 *
 * Offsets are floats: content past about 16 million UI pixels scrolls in steps larger than a pixel.
 *
 * The arithmetic on the capability struct is pure, for the elements that hold it and for tests.
 */


// Forward declarations.
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;


// Macros.
/** @brief How far the wheel scrolls per notch unless the element says otherwise, in UI pixels. */
#define UI_SCROLLABLE_DEFAULT_WHEEL_STEP 48.0f
/** @brief The largest wheel step, in UI pixels. */
#define UI_SCROLLABLE_MAX_WHEEL_STEP 100000.0f


// Types.
/**
 * @brief An axis of a scrollable.
 */
typedef enum UIScrollAxisEnum
{
    /** @brief Across: the content moves left and right. */
    UIScrollAxis_Horizontal,
    /** @brief Down: the content moves up and down. */
    UIScrollAxis_Vertical,
    /** @brief The number of axes. Not an axis. */
    UIScrollAxis_Count
} UIScrollAxis;

/**
 * @brief How one axis of a scrollable scrolls.
 */
typedef enum UIScrollModeEnum
{
    /** @brief It does not scroll: the content gets the view's length on that axis. */
    UIScrollMode_Disabled,
    /** @brief It scrolls, with a bar while the content is longer than the view. */
    UIScrollMode_Auto,
    /** @brief It scrolls, with a bar always (its thumb fills the track while nothing scrolls). */
    UIScrollMode_Always,
    /** @brief It scrolls, with no bar ever: by the wheel, the keys and what the element keeps in view (a one-line text box's text). */
    UIScrollMode_Hidden
} UIScrollMode;

/**
 * @brief The scrollable piece's state. Embedded by an element's widget type; its fields are read-only outside the elements that hold
 *        it. Build it with UIScrollableCapability_Construct.
 */
typedef struct UIScrollableCapabilityStruct
{
    /** @brief How far the view is scrolled into the content, per axis, in UI pixels; within [0, content - view]. */
    Vector2 _offset;
    /** @brief The content's size, in UI pixels; finite and >= 0. */
    Vector2 _contentSize;
    /** @brief The view's size, in UI pixels; finite and >= 0. */
    Vector2 _viewSize;
    /** @brief How each axis scrolls (by UIScrollAxis). */
    UIScrollMode _modes[UIScrollAxis_Count];
    /** @brief How far a notch of the wheel scrolls, in UI pixels. */
    float _wheelStep;
    /** @brief The offset the content was last placed with (the element's layout): a widget's place on the surface counts from it until
     *  the next layout. */
    Vector2 _placedOffset;
    /** @brief The widget whose box's top-left corner is the view's origin as laid out: content coordinate 0 shows there when the offset
     *  is 0. WIDGET_ID_INVALID until the element names one. */
    uint64_t _viewWidgetId;
} UIScrollableCapability;


// Calling code: by widget id.
/**
 * @brief Scrolls a scrollable to an offset, clamped into its range.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element (one that holds the piece: a scroll area, a dropdown's list).
 * @param offset The offset per axis, in the element's UI pixels; finite.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no scrollable; ErrorCode_ArgumentOutOfRange for a
 *          component that is not finite, which changes nothing.
 */
Error UIScrollable_SetOffset(UIScreen* screen, uint64_t widgetId, Vector2 offset);

/**
 * @brief Reads a scrollable's offset.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param outOffset [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no scrollable.
 */
Error UIScrollable_GetOffset(UIScreen* screen, uint64_t widgetId, Vector2* outOffset);

/**
 * @brief Scrolls a scrollable by a distance, clamped into its range.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param delta The distance per axis, in the element's UI pixels (positive: further into the content); finite.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no scrollable; ErrorCode_ArgumentOutOfRange for a
 *          component that is not finite, which changes nothing.
 */
Error UIScrollable_ScrollBy(UIScreen* screen, uint64_t widgetId, Vector2 delta);

/**
 * @brief Scrolls as little as it takes to show a box of the content: one larger than the view shows its start, unless the view is
 *        already inside it.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param contentBox The box, in content coordinates (UI pixels from the content's top-left); finite, with a size >= 0.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no scrollable; ErrorCode_ArgumentOutOfRange for a box
 *          that is not valid, which changes nothing.
 */
Error UIScrollable_EnsureVisible(UIScreen* screen, uint64_t widgetId, Rectangle contentBox);

/**
 * @brief Scrolls as little as it takes to show a widget inside the scrollable's content: what focus moving does in a scroll area.
 *
 * The widget's box is read as layout last put it, less what scrollables between the two have scrolled since (their offsets counted as
 * they are now), so nested scrollables can each be told in turn, innermost first, within one tick.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param descendantId A widget inside the element's content.
 * @returns Success (also when the element has no view yet, which changes nothing); ErrorCode_InvalidOperation for a stale id, a widget
 *          with no scrollable, or a @p descendantId that is not alive inside the element.
 */
Error UIScrollable_RevealWidget(UIScreen* screen, uint64_t widgetId, uint64_t descendantId);

/**
 * @brief Reads how far a scrollable can scroll per axis: the content less the view, never below 0.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param outRange [out] Receives it, in the element's UI pixels. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no scrollable.
 */
Error UIScrollable_GetRange(UIScreen* screen, uint64_t widgetId, Vector2* outRange);

/**
 * @brief Reads a scrollable's sizes as its last layout told them.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param outContentSize [out] Receives the content's size, UI pixels; may be NULL.
 * @param outViewSize [out] Receives the view's size, UI pixels; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no scrollable.
 */
Error UIScrollable_GetSizes(UIScreen* screen, uint64_t widgetId, Vector2* outContentSize, Vector2* outViewSize);

/**
 * @brief Sets how each axis of a scrollable scrolls; the element lays its content out by them from its next layout on.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param horizontal The mode across.
 * @param vertical The mode down.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no scrollable; ErrorCode_ArgumentOutOfRange for an
 *          unknown mode, which changes nothing.
 */
Error UIScrollable_SetModes(UIScreen* screen, uint64_t widgetId, UIScrollMode horizontal, UIScrollMode vertical);

/**
 * @brief Sets how far a notch of the wheel scrolls a scrollable.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param step The distance in the element's UI pixels; finite, within (0, UI_SCROLLABLE_MAX_WHEEL_STEP].
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no scrollable; ErrorCode_ArgumentOutOfRange for a step
 *          out of bounds, which changes nothing.
 */
Error UIScrollable_SetWheelStep(UIScreen* screen, uint64_t widgetId, float step);


// The capability: pure arithmetic for the elements that hold it.
/**
 * @brief Starts the piece: nothing to scroll, the modes and the wheel step given.
 * @param self The piece; must not be NULL.
 * @param horizontal The mode across.
 * @param vertical The mode down.
 * @param wheelStep How far a notch scrolls, UI pixels; a value that is not within (0, UI_SCROLLABLE_MAX_WHEEL_STEP] gives
 *        UI_SCROLLABLE_DEFAULT_WHEEL_STEP.
 */
void UIScrollableCapability_Construct(UIScrollableCapability* self, UIScrollMode horizontal, UIScrollMode vertical, float wheelStep);

/**
 * @brief Returns whether a mode is one of the enum's values.
 * @param mode The mode.
 * @returns true if known.
 */
bool UIScrollMode_IsValid(UIScrollMode mode);

/**
 * @brief Returns how far the piece can scroll per axis: the content less the view, never below 0; 0 on an axis that does not scroll.
 * @param self The piece; must not be NULL.
 * @returns The range, UI pixels.
 */
Vector2 UIScrollableCapability_GetRange(const UIScrollableCapability* self);

/**
 * @brief Tells the piece the content's size and the view's; the offset clamps into the new range.
 * @param self The piece; must not be NULL.
 * @param contentSize The content's size, UI pixels; a component that is not a finite number >= 0 counts as 0, and one past
 *        UI_LAYOUT_MAX_LENGTH as that.
 * @param viewSize The view's size, UI pixels; the same rules.
 * @returns true when the offset moved.
 */
bool UIScrollableCapability_SetSizes(UIScrollableCapability* self, Vector2 contentSize, Vector2 viewSize);

/**
 * @brief Moves the offset, clamped into the range.
 * @param self The piece; must not be NULL.
 * @param offset The offset; a component that is not a finite number keeps the old one.
 * @returns true when the offset moved.
 */
bool UIScrollableCapability_SetOffset(UIScrollableCapability* self, Vector2 offset);

/**
 * @brief Moves the offset by a distance, clamped into the range.
 * @param self The piece; must not be NULL.
 * @param delta The distance; a component that is not a finite number moves nothing.
 * @returns How far it moved per axis.
 */
Vector2 UIScrollableCapability_ScrollBy(UIScrollableCapability* self, Vector2 delta);

/**
 * @brief Scrolls as little as it takes to show a box of the content (UIScrollable_EnsureVisible).
 * @param self The piece; must not be NULL.
 * @param contentBox The box in content coordinates, UI pixels; a box that is not finite, or of negative size, changes nothing.
 * @returns true when the offset moved.
 */
bool UIScrollableCapability_EnsureVisible(UIScrollableCapability* self, Rectangle contentBox);

/**
 * @brief Returns whether the piece can still scroll from where it is in a direction along an axis.
 * @param self The piece; must not be NULL.
 * @param axis The axis.
 * @param direction Negative toward the content's start, positive toward its end; 0 is never.
 * @returns true when it can.
 */
bool UIScrollableCapability_CanScroll(const UIScrollableCapability* self, UIScrollAxis axis, float direction);

/**
 * @brief Returns the offset along one axis that shows a span of the content with as little scrolling as it takes: a span longer than
 *        the view shows its start unless the view is already inside it. Pure.
 * @param offset The offset now.
 * @param spanStart The span's start, in content coordinates.
 * @param spanLength The span's length; >= 0.
 * @param viewLength The view's length; >= 0.
 * @returns The new offset (not clamped into a range).
 */
float UIScrollable_GetOffsetShowing(float offset, float spanStart, float spanLength, float viewLength);
