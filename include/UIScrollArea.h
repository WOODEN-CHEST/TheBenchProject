#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIScrollable.h"
#include "UIWidget.h"


/**
 * @file UIScrollArea.h
 * @brief The scroll area element: content larger than its box, clipped to it, with scroll bars by the modes of its scrollable piece.
 *
 * Calling code adds its content to the area as to any parent (one child is the usual; every child but the area's two bars gets the
 * same box). The area measures it at the view's width while only the vertical axis scrolls, and unbounded across while that axis
 * scrolls; the content box is at least the view, so a short page still fills it. It places the content at its offset, cut to its box,
 * and its two bars (UIScrollbar.h, made with it by UIKit_CreateScrollArea, on top of the content) along its right and bottom edges by
 * the modes (UIScrollable.h): a bar that does not show has no size. The vertical bar runs the whole height, and where the horizontal
 * one meets it the corner is the end of its track, which its thumb never enters. A bar's track is drawn over the content, so a theme
 * whose track is empty lets the content show under it.
 *
 * The wheel over it, or over anything inside it that leaves the wheel alone, scrolls it while it can still scroll that way, and goes
 * on up the tree at the end (so an area inside an area scrolls the outer one once the inner is at its end); Shift turns a plain wheel
 * across. Page Up and Page Down that the focused widget inside it leaves alone scroll it by a page. When focus moves to a widget
 * inside it by the keyboard or by code, it scrolls as little as it takes to show it (UIScrollable_RevealWidget); a press does not,
 * so the content never jumps under the pointer. It takes input, so a press on it does not reach what lies behind; it is not focusable.
 *
 * Calling code moves it through the scrollable piece by widget id (UIScrollable_SetOffset and the rest). Its look (UIKit_RegisterLook:
 * a struct starting with UIScrollAreaLookBase) draws its background and measures it (UIScrollArea_MeasurePreferred); which bars show
 * and how thick they are is there for a look that wants more (UIScrollArea_IsBarShown, UIScrollArea_GetBarThickness).
 */


// Types.
/**
 * @brief What a scroll area is created with (UIKit_CreateScrollArea). A plain value; build one with UIScrollAreaOptions_Create.
 */
typedef struct UIScrollAreaOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief How it scrolls across. */
    UIScrollMode Horizontal;
    /** @brief How it scrolls down. */
    UIScrollMode Vertical;
    /** @brief How far a notch of the wheel scrolls, in its UI pixels; within (0, UI_SCROLLABLE_MAX_WHEEL_STEP]. */
    float WheelStep;
} UIScrollAreaOptions;

/**
 * @brief What a scroll area look's Construct gets (UIKit_CreateScrollArea passes it): the kit and the options.
 */
typedef struct UIScrollAreaArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIScrollAreaOptions* Options;
} UIScrollAreaArgs;

/**
 * @brief A scroll area's state: its scrollable piece, which calling code reaches by UIScrollable_*, and its bars. No look; embedded by
 *        a look's widget type right after its Widget (UIScrollAreaLookBase). Its fields are read-only outside the area's module.
 */
typedef struct UIScrollAreaCapabilityStruct
{
    /** @brief What scrolls. */
    UIScrollableCapability Scrollable;
    /** @brief Its bars by axis (UIScrollAxis), made with it; WIDGET_ID_INVALID until then. */
    uint64_t _barIds[UIScrollAxis_Count];
    /** @brief Which bars showed at the last layout. */
    bool _isBarShown[UIScrollAxis_Count];
    /** @brief How thick each bar was at the last layout, in the area's UI pixels. */
    float _barThickness[UIScrollAxis_Count];
} UIScrollAreaCapability;

/**
 * @brief How every scroll area look's widget struct starts: the widget, then the area's state.
 */
typedef struct UIScrollAreaLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The area's state. */
    UIScrollAreaCapability Area;
} UIScrollAreaLookBase;


// Helpers.
/**
 * @brief Default options: only down scrolls, with a bar while the content is longer; the default wheel step.
 * @returns The options.
 */
static inline UIScrollAreaOptions UIScrollAreaOptions_Create(void)
{
    return (UIScrollAreaOptions){ .Element = UIElementOptions_Create(), .Horizontal = UIScrollMode_Disabled, .Vertical = UIScrollMode_Auto,
        .WheelStep = UI_SCROLLABLE_DEFAULT_WHEEL_STEP };
}


// Helpers, for looks and the kit.
/**
 * @brief Constructs the area's state from its options.
 * @param self The state; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param options The options; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an unknown mode or a wheel step out of bounds; otherwise the options' error.
 */
Error UIScrollAreaCapability_Construct(UIScrollAreaCapability* self, Widget* widget, const UIScrollAreaOptions* options);

/**
 * @brief Returns whether a bar showed at the last layout.
 * @param self The state; must not be NULL.
 * @param axis The bar's axis.
 * @returns true if it showed.
 */
bool UIScrollArea_IsBarShown(const UIScrollAreaCapability* self, UIScrollAxis axis);

/**
 * @brief Returns how thick a bar was at the last layout.
 * @param self The state; must not be NULL.
 * @param axis The bar's axis.
 * @returns The thickness in the area's UI pixels; 0 for an unknown axis.
 */
float UIScrollArea_GetBarThickness(const UIScrollAreaCapability* self, UIScrollAxis axis);

/**
 * @brief Gives an area's preferred size, for its look's Measure: its content's, measured as its layout measures it, with the bars that
 *        always show added. An area sized to its content never scrolls; give it a fixed, filling or placed box to scroll.
 * @param self The state; must not be NULL.
 * @param widget The area's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width available, in the area's own UI pixels.
 * @param outSize [out] Receives the size in its own UI pixels. Must not be NULL.
 * @returns Success, or a child's Measure error.
 */
Error UIScrollArea_MeasurePreferred(UIScrollAreaCapability* self, Widget* widget, const UIMeasureContext* context, float availableWidth,
    Vector2* outSize);

/**
 * @brief Records the area's bars, made by the kit right after the area. Called by UIKit_CreateScrollArea.
 * @param self The state; must not be NULL.
 * @param horizontalBarId The bar across.
 * @param verticalBarId The bar down.
 */
void UIScrollArea_SetBars(UIScrollAreaCapability* self, uint64_t horizontalBarId, uint64_t verticalBarId);
