#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "StackWidget.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIPanel.h
 * @brief The panel element: a background and padding for what it holds, its children stacked as a StackWidget's are.
 *
 * Its padding and spacing come from its look (the standard one reads the theme's) unless its options give them. It takes input,
 * so a click on a panel does not reach what lies behind it; it is not focusable. Its look (UIKit_RegisterLook: a struct starting with
 * UIPanelLookBase) draws its background and lays its children out inside its frame (UIPanel_GetLayout, then UIStackLayout_Arrange);
 * a look without a Layout of its own gets the options' padding and spacing, or none.
 */


// Types.
/**
 * @brief What a panel is created with (UIKit_CreatePanel). A plain value; build one with UIPanelOptions_Create.
 */
typedef struct UIPanelOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief How its children stack. */
    UIStackDirection Direction;
    /** @brief Whether Padding replaces the theme's padding. */
    bool HasPadding;
    /** @brief Around its children in UI pixels, when HasPadding; each finite and >= 0. */
    UIInsets Padding;
    /** @brief Whether Spacing replaces the theme's spacing. */
    bool HasSpacing;
    /** @brief Between its children in UI pixels, when HasSpacing; finite and >= 0. */
    float Spacing;
} UIPanelOptions;

/**
 * @brief A panel's state: how it stacks its children, with the options' padding and spacing. No look; embedded by a look's widget
 *        type right after its Widget (UIPanelLookBase). Its fields are read-only outside the panel's module.
 */
typedef struct UIPanelCapabilityStruct
{
    /** @brief How its children stack. */
    UIStackDirection _direction;
    /** @brief Whether _padding replaces the look's. */
    bool _hasPadding;
    /** @brief Its own padding. */
    UIInsets _padding;
    /** @brief Whether _spacing replaces the look's. */
    bool _hasSpacing;
    /** @brief Its own spacing. */
    float _spacing;
} UIPanelCapability;

/**
 * @brief How every panel look's widget struct starts: the widget, then the panel's state.
 */
typedef struct UIPanelLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The panel's state. */
    UIPanelCapability Panel;
} UIPanelLookBase;


// Helpers.
/**
 * @brief Default options: a column, with the theme's padding and spacing.
 * @returns The options.
 */
static inline UIPanelOptions UIPanelOptions_Create(void)
{
    return (UIPanelOptions)
    {
        .Element = UIElementOptions_Create(),
        .Direction = UIStackDirection_Column,
        .HasPadding = false,
        .Padding = UIInsets_All(0.0f),
        .HasSpacing = false,
        .Spacing = 0.0f
    };
}


// Helpers, for looks.
/**
 * @brief Constructs the panel's state from its options.
 * @param self The state; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param options The options; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an option that is not valid.
 */
Error UIPanelCapability_Construct(UIPanelCapability* self, Widget* widget, const UIPanelOptions* options);

/**
 * @brief Returns how the panel stacks its children: its direction, and the options' padding and spacing where they give them, else
 *        the look's.
 * @param self The state; must not be NULL.
 * @param lookPadding The look's padding, in the panel's own UI pixels.
 * @param lookSpacing The look's spacing, in the panel's own UI pixels.
 * @returns The layout.
 */
UIStackLayout UIPanel_GetLayout(const UIPanelCapability* self, UIInsets lookPadding, float lookSpacing);
