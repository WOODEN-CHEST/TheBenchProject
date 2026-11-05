#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
// The widget base is embedded by value; a stack's options hold an element's.
#include "UIElement.h"
#include "UIPlacement.h"
#include "UIWidget.h"


/**
 * @file StackWidget.h
 * @brief Children in a row or a column, with padding and spacing: how an inspector row or a panel's content is laid out.
 *
 * Along its main axis each child is fixed, sizes to its content, or fills a share of what is left, by the hint it carries
 * (UIPlacement.h, UIStackHint); across it each aligns to the start, the centre or the end, or stretches. A child that is not
 * rendered takes no room. Stacks nest. What overflows is laid out as it is: a scroll area clips it. A stack owns its
 * children's boxes: each tick it writes their placements, so a placement a child was given is replaced. It takes no input
 * itself and the pointer passes through it to what is behind, its children still being hit.
 *
 * UIStackLayout is the arithmetic, shared with the panel. Measuring asks each child (Widget_Measure, cached), so an idle
 * stack costs a walk over its children per tick and allocates nothing.
 */


// Types.
/**
 * @brief A stack's main axis.
 */
typedef enum UIStackDirectionEnum
{
    /** @brief Children under each other. */
    UIStackDirection_Column,
    /** @brief Children beside each other. */
    UIStackDirection_Row
} UIStackDirection;

/**
 * @brief How a stack lays its children out. A plain value.
 */
typedef struct UIStackLayoutStruct
{
    /** @brief The main axis. */
    UIStackDirection Direction;
    /** @brief Around the children, in UI pixels; each within [0, UI_LAYOUT_MAX_LENGTH]. */
    UIInsets Padding;
    /** @brief Between neighbours, in UI pixels; within [0, UI_LAYOUT_MAX_LENGTH]. */
    float Spacing;
} UIStackLayout;

/**
 * @brief What a stack is created with (UIKit_CreateStack). A plain value; build one with UIStackOptions_Create.
 */
typedef struct UIStackOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief How it lays its children out. */
    UIStackLayout Layout;
} UIStackOptions;

/**
 * @brief The stack widget. Embed nothing here: create it through the kit (UIKit_CreateStack) or the factory, with a
 *        UIStackOptions as the constructor's arguments.
 */
typedef struct StackWidgetStruct
{
    /** @brief The widget base (must be first). */
    Widget Base;
    /** @brief How it lays its children out. */
    UIStackLayout _layout;
} StackWidget;


// Helpers.
/**
 * @brief Default options: no padding, no spacing.
 * @param direction The main axis.
 * @returns The options.
 */
static inline UIStackOptions UIStackOptions_Create(UIStackDirection direction)
{
    return (UIStackOptions)
    {
        .Element = UIElementOptions_Create(),
        .Layout = { .Direction = direction, .Padding = UIInsets_All(0.0f), .Spacing = 0.0f }
    };
}


// Functions.
/**
 * @brief Returns whether a layout can be used: a known direction, padding and spacing within [0, UI_LAYOUT_MAX_LENGTH].
 * @param layout The layout; must not be NULL.
 * @returns true if valid.
 */
bool UIStackLayout_IsValid(const UIStackLayout* layout);

/**
 * @brief Gives the preferred size of a widget that lays its children out so, in UI pixels: the children's, with padding and
 *        spacing. A filling child counts with its content's size.
 * @param layout The layout; must not be NULL.
 * @param widget The widget whose children these are; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width available, in UI pixels.
 * @param outSize [out] Receives the size. Must not be NULL.
 * @returns Success, or a child's Measure error.
 */
Error UIStackLayout_Measure(const UIStackLayout* layout, Widget* widget, const UIMeasureContext* context, float availableWidth,
    Vector2* outSize);

/**
 * @brief Writes the placements of a widget's children for its current box: call it from its Layout hook. The layout pass
 *        applies them as it reaches each child, right after.
 * @param layout The layout; must not be NULL.
 * @param widget The widget whose children these are; must not be NULL.
 * @param context The widget's layout context; must not be NULL.
 * @returns Success, or a child's Measure error.
 */
Error UIStackLayout_Arrange(const UIStackLayout* layout, Widget* widget, const UILayoutContext* context);

/**
 * @brief Registers the stack's widget type. Once per factory.
 * @param factory The factory; must not be NULL.
 * @param outTypeId [out] Receives the type's id. Must not be NULL.
 * @returns Success, or the factory's error.
 */
Error StackWidget_RegisterType(UIWidgetFactory* factory, uint64_t* outTypeId);

/**
 * @brief Sets how a child sits in the stack that holds it (it reads the hint at its next layout).
 * @param screen The child's screen; must not be NULL.
 * @param childId The child.
 * @param hint The hint; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id; ErrorCode_ArgumentOutOfRange for a hint that is not valid.
 */
Error StackWidget_SetChildHint(UIScreen* screen, uint64_t childId, const UIStackHint* hint);
