#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "StackWidget.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIToolbar.h
 * @brief The tool bar element (kind toolbar): a strip of buttons with icons, made from a list, with separators between their groups;
 *        horizontal or vertical.
 *
 * ITEMS. Its items (UIToolbarItem, copied when it is made) are buttons, which activate; toggles, which flip at each click; choices, one of
 * a group, where a click takes one and lets go of the others of its group; and separators. Each item but a separator is a kit button, of
 * the button variant its options name ("tool" by default), with the item's icon, its text if it has one, and its name as its hover text
 * (UITooltip.h), which a status bar shows too. The editor makes its bars from its actions.
 *
 * EVENTS, on the bar, each with the item's id as its index (UIEvent.Index). A button's click posts UIElementEvent_Activated. A toggle's or
 * a choice's click posts UIElementEvent_Changed and then UIElementEvent_Committed with its new state as a bool; a click on the choice
 * already taken changes nothing and posts nothing. Calling code sets the states from its own (UIToolbar_SetIsToggled), which posts
 * nothing. A disabled item takes no clicks.
 *
 * The keyboard reaches the buttons as any others: the focus moves to them, and Enter and Space click them.
 *
 * Its look (UIKit_RegisterLook: a struct starting with UIToolbarLookBase) draws its background and its separators (where they are, from
 * its entries: UIToolbarCapability_GetEntryCount and UIToolbarCapability_GetEntryAt), lays its buttons out (UIToolbar_Arrange) and
 * measures them (UIToolbar_Measure). The buttons are drawn by the button kind's look. A bar shorter than its items cuts the last ones off.
 */


// Macros.
/** @brief The most items a tool bar holds, its separators counted. */
#define UI_TOOLBAR_MAX_ITEMS ((size_t)256)


// Types.
/**
 * @brief What a tool bar's item is.
 */
typedef enum UIToolbarItemKindEnum
{
    /** @brief A button: a click activates it. */
    UIToolbarItemKind_Button,
    /** @brief A toggle: a click flips its state. */
    UIToolbarItemKind_Toggle,
    /** @brief One of a group (UIToolbarItem.Group): a click takes it and lets go of the others of its group. */
    UIToolbarItemKind_Choice,
    /** @brief A line between two groups of buttons; no id, no button. */
    UIToolbarItemKind_Separator,
    /** @brief The number of kinds. Not a kind. */
    UIToolbarItemKind_Count
} UIToolbarItemKind;

/**
 * @brief One item of a tool bar, as its options give it. A plain value; build one with UIToolbarItem_CreateButton and its siblings.
 */
typedef struct UIToolbarItemStruct
{
    /** @brief What it is. */
    UIToolbarItemKind Kind;
    /** @brief What its events carry as their index; unique among the bar's items. A separator's is not read. */
    int32_t Id;
    /** @brief Its icon; valid (UIElementIcon_IsValid). Copied. An item with no icon needs a text. */
    UIElementIcon Icon;
    /** @brief Text shown beside its icon as UTF-8, or NULL for none: for an action with no icon yet. Copied. */
    const unsigned char* Text;
    /** @brief Its name, its hover text, as UTF-8, or NULL for none. Copied. */
    const unsigned char* Name;
    /** @brief Whether it takes clicks. */
    bool IsEnabled;
    /** @brief A toggle's or a choice's state: whether it is on (taken). At most one choice of a group is. */
    bool IsToggled;
    /** @brief A choice's group: the choices of the same group are one choice. Not read for the other kinds. */
    uint32_t Group;
} UIToolbarItem;

/**
 * @brief What a tool bar is created with (UIKit_CreateToolbar). A plain value; build one with UIToolbarOptions_Create.
 */
typedef struct UIToolbarOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief Its direction: UIStackDirection_Row lays the items out left to right, UIStackDirection_Column top to bottom. */
    UIStackDirection Direction;
    /** @brief Its items, in their order; may be NULL only when ItemCount is 0. Read for the call. */
    const UIToolbarItem* Items;
    /** @brief How many, at most UI_TOOLBAR_MAX_ITEMS. */
    size_t ItemCount;
    /** @brief The button variant its buttons are drawn with as UTF-8, or NULL for the button kind's own values. Copied. */
    const unsigned char* ButtonVariant;
} UIToolbarOptions;

/**
 * @brief What a tool bar look's Construct gets (UIKit_CreateToolbar passes it): the kit and the options.
 */
typedef struct UIToolbarArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIToolbarOptions* Options;
} UIToolbarArgs;

/**
 * @brief One item as a tool bar keeps it. Read-only outside the bar's module.
 */
typedef struct UIToolbarEntryStruct
{
    /** @brief What it is. */
    UIToolbarItemKind Kind;
    /** @brief Its id. */
    int32_t Id;
    /** @brief A choice's group. */
    uint32_t Group;
    /** @brief Its button, WIDGET_ID_INVALID for a separator (and until the kit made it). */
    uint64_t ButtonId;
    /** @brief Where it starts along the bar as last laid out, from the bar's start, in the bar's own UI pixels. */
    float Start;
    /** @brief How long it is along the bar as last laid out, in the bar's own UI pixels; a separator's room. */
    float Length;
} UIToolbarEntry;

/**
 * @brief The sizes a tool bar is laid out with, in its own UI pixels: a look's, from its theme (UIToolbar_Arrange, UIToolbar_Measure).
 */
typedef struct UIToolbarMetricsStruct
{
    /** @brief Inside the bar, horizontal and vertical. */
    Vector2 Padding;
    /** @brief Between two buttons side by side. */
    float Spacing;
    /** @brief The least size of a button: one with a longer text is longer along the bar; across it, all are as wide as the widest. */
    Vector2 ButtonSize;
    /** @brief The room a separator takes along the bar, its line in the middle. */
    float SeparatorSpacing;
} UIToolbarMetrics;

/**
 * @brief A tool bar's capability: its items and its direction. Embedded by a look's widget type right after its Widget (UIToolbarLookBase);
 *        its fields are read-only outside the bar's module.
 */
typedef struct UIToolbarCapabilityStruct
{
    /** @brief Its items, as UIToolbarEntry, in their order. */
    GenericBuffer _entries;
    /** @brief Its direction. */
    UIStackDirection _direction;
} UIToolbarCapability;

/**
 * @brief How every tool bar look's widget struct starts: the widget, then the capability.
 */
typedef struct UIToolbarLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The bar's behaviour. */
    UIToolbarCapability Toolbar;
} UIToolbarLookBase;


// Helpers.
/**
 * @brief Default options: a horizontal bar of no items, its buttons of the "tool" variant.
 * @returns The options.
 */
static inline UIToolbarOptions UIToolbarOptions_Create(void)
{
    return (UIToolbarOptions){ .Element = UIElementOptions_Create(), .Direction = UIStackDirection_Row, .Items = NULL, .ItemCount = 0U,
        .ButtonVariant = u8"tool" };
}

/**
 * @brief A button item: enabled.
 * @param id What its events carry.
 * @param icon Its icon.
 * @param name Its hover text as UTF-8, or NULL.
 * @returns The item.
 */
static inline UIToolbarItem UIToolbarItem_CreateButton(int32_t id, UIElementIcon icon, const unsigned char* name)
{
    return (UIToolbarItem){ .Kind = UIToolbarItemKind_Button, .Id = id, .Icon = icon, .Text = NULL, .Name = name, .IsEnabled = true,
        .IsToggled = false, .Group = 0U };
}

/**
 * @brief A toggle item: enabled.
 * @param id What its events carry.
 * @param icon Its icon.
 * @param name Its hover text as UTF-8, or NULL.
 * @param isToggled Whether it starts on.
 * @returns The item.
 */
static inline UIToolbarItem UIToolbarItem_CreateToggle(int32_t id, UIElementIcon icon, const unsigned char* name, bool isToggled)
{
    return (UIToolbarItem){ .Kind = UIToolbarItemKind_Toggle, .Id = id, .Icon = icon, .Text = NULL, .Name = name, .IsEnabled = true,
        .IsToggled = isToggled, .Group = 0U };
}

/**
 * @brief A choice item: enabled.
 * @param id What its events carry.
 * @param icon Its icon.
 * @param name Its hover text as UTF-8, or NULL.
 * @param group Its group.
 * @param isToggled Whether it starts taken.
 * @returns The item.
 */
static inline UIToolbarItem UIToolbarItem_CreateChoice(int32_t id, UIElementIcon icon, const unsigned char* name, uint32_t group, bool isToggled)
{
    return (UIToolbarItem){ .Kind = UIToolbarItemKind_Choice, .Id = id, .Icon = icon, .Text = NULL, .Name = name, .IsEnabled = true,
        .IsToggled = isToggled, .Group = group };
}

/**
 * @brief A separator.
 * @returns The item.
 */
static inline UIToolbarItem UIToolbarItem_CreateSeparator(void)
{
    return (UIToolbarItem){ .Kind = UIToolbarItemKind_Separator, .Id = 0, .Icon = UIElementIcon_None(), .Text = NULL, .Name = NULL,
        .IsEnabled = true, .IsToggled = false, .Group = 0U };
}

/**
 * @brief The sizes a tool bar is laid out with when its look gives none.
 * @returns The metrics.
 */
static inline UIToolbarMetrics UIToolbarMetrics_CreateDefault(void)
{
    return (UIToolbarMetrics){ .Padding = { .x = 3.0f, .y = 3.0f }, .Spacing = 2.0f, .ButtonSize = { .x = 26.0f, .y = 26.0f },
        .SeparatorSpacing = 9.0f };
}

/**
 * @brief Checks a tool bar's options: at most UI_TOOLBAR_MAX_ITEMS items, each of a known kind with a valid icon and valid UTF-8 texts
 *        (each but a separator with an icon or a text, so it shows something), their ids different (separators aside), at most one choice
 *        of a group taken, and a direction that is a row or a column.
 * @param options The options; must not be NULL.
 * @returns Whether they are valid.
 */
bool UIToolbarOptions_IsValid(const UIToolbarOptions* options);


// Calling code: by widget id.
/**
 * @brief Sets whether a toggle is on, or whether a choice is taken (taking one lets go of the others of its group; letting go of one leaves
 *        its group with none). Posts nothing.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param itemId The item.
 * @param isToggled Whether it is.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tool bar; ErrorCode_ArgumentOutOfRange for an item the
 *          bar does not have or one that is no toggle or choice; otherwise its button's error.
 */
Error UIToolbar_SetIsToggled(UIScreen* screen, uint64_t widgetId, int32_t itemId, bool isToggled);

/**
 * @brief Reads whether a toggle is on, or a choice taken.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param itemId The item.
 * @param outIsToggled [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tool bar; ErrorCode_ArgumentOutOfRange for an item the
 *          bar does not have or one that is no toggle or choice; otherwise its button's error.
 */
Error UIToolbar_IsToggled(UIScreen* screen, uint64_t widgetId, int32_t itemId, bool* outIsToggled);

/**
 * @brief Enables or disables an item.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param itemId The item.
 * @param isEnabled Whether it takes clicks.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tool bar; ErrorCode_ArgumentOutOfRange for an item the
 *          bar does not have or a separator; otherwise its button's error.
 */
Error UIToolbar_SetIsEnabled(UIScreen* screen, uint64_t widgetId, int32_t itemId, bool isEnabled);

/**
 * @brief Gives an item's button, for calling code to change its icon or its text (UIButton.h).
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param itemId The item.
 * @param outButtonId [out] Receives its button's id. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tool bar; ErrorCode_ArgumentOutOfRange for an item the
 *          bar does not have or a separator.
 */
Error UIToolbar_GetButtonId(UIScreen* screen, uint64_t widgetId, int32_t itemId, uint64_t* outButtonId);

/**
 * @brief Sets a bar's direction: a bar docked at a side turns upright.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param direction UIStackDirection_Row or UIStackDirection_Column.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tool bar; ErrorCode_ArgumentOutOfRange for another
 *          direction.
 */
Error UIToolbar_SetDirection(UIScreen* screen, uint64_t widgetId, UIStackDirection direction);


// Helpers, for looks and the kit.
/**
 * @brief Returns how many items a bar has, its separators counted.
 * @param self The capability; must not be NULL.
 * @returns The count.
 */
static inline size_t UIToolbarCapability_GetEntryCount(const UIToolbarCapability* self)
{
    return self->_entries._count;
}

/**
 * @brief Returns an item as the bar keeps it: a look draws its separators from their place (Start and Length along the bar).
 * @param self The capability; must not be NULL.
 * @param index The item's index; below UIToolbarCapability_GetEntryCount.
 * @returns The entry; valid until the bar changes.
 */
static inline const UIToolbarEntry* UIToolbarCapability_GetEntryAt(const UIToolbarCapability* self, size_t index)
{
    return (const UIToolbarEntry*)(const void*)(self->_entries._data + (index * sizeof(UIToolbarEntry)));
}

/**
 * @brief Returns whether a bar lays its items out top to bottom.
 * @param self The capability; must not be NULL.
 * @returns Whether it is upright.
 */
static inline bool UIToolbarCapability_IsVertical(const UIToolbarCapability* self)
{
    return self->_direction == UIStackDirection_Column;
}

/**
 * @brief Lays a bar's buttons out and records where its separators are; for a look's Layout. Along the bar each button is as long as the
 *        metrics' button size or its own measure, whichever is more; across it every button is as wide as the widest, centred in the bar.
 * @param widget The bar's widget; must not be NULL.
 * @param context The layout's context; must not be NULL.
 * @param metrics The sizes; must not be NULL.
 * @returns Success, or a button's Measure or placement error.
 */
Error UIToolbar_Arrange(Widget* widget, const UILayoutContext* context, const UIToolbarMetrics* metrics);

/**
 * @brief Measures a bar as UIToolbar_Arrange lays it out; for a look's Measure.
 * @param widget The bar's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param metrics The sizes; must not be NULL.
 * @param outSize [out] Receives its size, the padding counted, in its own UI pixels. Must not be NULL.
 * @returns Success, or a button's Measure error.
 */
Error UIToolbar_Measure(Widget* widget, const UIMeasureContext* context, const UIToolbarMetrics* metrics, Vector2* outSize);

/**
 * @brief Records an item's button, made by the kit right after the bar, and listens to its clicks. Called by UIKit_CreateToolbar.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param index The item's index.
 * @param buttonId Its button.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no tool bar; ErrorCode_ArgumentOutOfRange for an index
 *          past its items or a separator's; otherwise the subscription's error.
 */
Error UIToolbar_AttachButton(UIScreen* screen, uint64_t widgetId, size_t index, uint64_t buttonId);
