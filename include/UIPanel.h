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
 * Its padding and spacing come from the theme unless its options give them. It takes input, so a click on a panel does not
 * reach what lies behind it; it is not focusable.
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
 * @brief A panel's capability: its look and how it stacks its children. Embedded by a widget type; its fields are
 *        read-only outside the panel's module.
 */
typedef struct UIPanelCapabilityStruct
{
    /** @brief The look. */
    UIElementLook _look;
    /** @brief How its children stack. */
    UIStackDirection _direction;
    /** @brief Whether _padding replaces the theme's. */
    bool _hasPadding;
    /** @brief Its own padding. */
    UIInsets _padding;
    /** @brief Whether _spacing replaces the theme's. */
    bool _hasSpacing;
    /** @brief Its own spacing. */
    float _spacing;
} UIPanelCapability;


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


// Behaviour helpers, for widget types embedding the capability.
/**
 * @brief Constructs the capability from a widget type's constructor.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param options The options; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for an option that is not valid.
 */
Error UIPanelCapability_Construct(UIPanelCapability* self, Widget* widget, const UIPanelOptions* options);

/**
 * @brief Advances the look; call it from Update.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param time The tick's time.
 */
void UIPanelCapability_Update(UIPanelCapability* self, Widget* widget, ProgramTime time);

/**
 * @brief Writes its children's placements; call it from Layout.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param context The layout context; must not be NULL.
 * @returns Success, or a child's Measure error.
 */
Error UIPanelCapability_Layout(UIPanelCapability* self, Widget* widget, const UILayoutContext* context);

/**
 * @brief Gives its preferred size: its children's with padding, at least the theme's minimum; call it from Measure.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width available, in UI pixels.
 * @param outSize [out] Receives the size. Must not be NULL.
 * @returns Success, or a child's Measure error.
 */
Error UIPanelCapability_Measure(UIPanelCapability* self, Widget* widget, const UIMeasureContext* context, float availableWidth,
    Vector2* outSize);

/**
 * @brief Registers the panel's standard widget type. Once per factory (the kit does).
 * @param factory The factory; must not be NULL.
 * @param lookCapabilityId The look's capability id.
 * @param outTypeId [out] Receives the type's id. Must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIPanel_RegisterType(UIWidgetFactory* factory, uint64_t lookCapabilityId, uint64_t* outTypeId);
