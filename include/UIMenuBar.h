#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRUserData.h"
#include "UIElement.h"
#include "UIMenu.h"
#include "UIWidget.h"


/**
 * @file UIMenuBar.h
 * @brief The menu bar element: a strip of entries, each opening a menu (UIMenu.h) under it.
 *
 * Entries are press elements in a row (UIMenuBar_AddEntry, each a label and a menu's items, copied): a left press on one opens its menu
 * at once (another press on it closes it), so a press, a drag to an item and a release there activates that item; Enter or Space opens
 * it with its first item focused, and the arrows move between entries as keyboard navigation does. While one entry's menu is open,
 * the pointer coming onto another entry switches to its menu, and in an open menu Left and Right with nothing more to open move to the
 * entries beside. A press on the bar counts as inside its menus, so it never closes them by itself. Alt alone does nothing yet. An
 * activated item calls the bar's handler (UIMenuBar_SetHandler) with its id.
 *
 * The bar's look (UIKit_RegisterLook: a struct starting with UIMenuBarLookBase) draws its background and lays its entries out in a row
 * (UIMenuBar_ArrangeEntries); the entries are the kit's own widgets, drawn from the theme's menu bar kind, their look not replaceable yet.
 */


// Types.
/**
 * @brief What a menu bar is created with (UIKit_CreateMenuBar). A plain value; build one with UIMenuBarOptions_Create.
 */
typedef struct UIMenuBarOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief Called with an activated item's id; may be NULL. */
    UIMenuHandler Handler;
    /** @brief Handed to it. */
    UserData Data;
} UIMenuBarOptions;

/**
 * @brief What a menu bar look's Construct gets (UIKit_CreateMenuBar passes it): the kit and the options.
 */
typedef struct UIMenuBarArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIMenuBarOptions* Options;
} UIMenuBarArgs;

/**
 * @brief A menu bar's capability: its handler and which entry's menu is open. Embedded by a look's widget type right after its Widget
 *        (UIMenuBarLookBase); its fields are read-only outside the bar's module.
 */
typedef struct UIMenuBarCapabilityStruct
{
    /** @brief Called with an activated item's id; may be NULL. */
    UIMenuHandler _handler;
    /** @brief Handed to it. */
    UserData _data;
    /** @brief The open menu (a popup's root), or WIDGET_ID_INVALID. */
    uint64_t _openMenuId;
    /** @brief The entry whose menu is open, or WIDGET_ID_INVALID. */
    uint64_t _openEntryId;
} UIMenuBarCapability;

/**
 * @brief How every menu bar look's widget struct starts: the widget, then the capability.
 */
typedef struct UIMenuBarLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The bar's behaviour. */
    UIMenuBarCapability MenuBar;
} UIMenuBarLookBase;


// Helpers.
/**
 * @brief Default options: no handler.
 * @returns The options.
 */
static inline UIMenuBarOptions UIMenuBarOptions_Create(void)
{
    return (UIMenuBarOptions){ .Element = UIElementOptions_Create(), .Handler = NULL, .Data = UserData_CreateEmpty() };
}


// Calling code: by widget id.
/**
 * @brief Adds an entry at the end of a menu bar: a label and its menu's items (copied, the whole tree).
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param label The entry's label as UTF-8; must not be NULL.
 * @param items Its menu's items; may be NULL only when @p count is 0.
 * @param count How many.
 * @param outEntryId [out] Receives the entry's id; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL label; ErrorCode_InvalidOperation for a stale id or a widget that is no menu bar;
 *          otherwise the errors UIMenu_Open gives for items, or an error from making the entry.
 */
Error UIMenuBar_AddEntry(UIScreen* screen, uint64_t widgetId, const unsigned char* label, const UIMenuItem* items, size_t count, uint64_t* outEntryId);

/**
 * @brief Sets the handler a menu bar's activated items call.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param handler The handler; may be NULL.
 * @param userData Handed to it; copied. May be NULL for none.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no menu bar.
 */
Error UIMenuBar_SetHandler(UIScreen* screen, uint64_t widgetId, UIMenuHandler handler, const UserData* userData);

/**
 * @brief Reads which menu of a bar is open.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @param outMenuId [out] Receives the open menu, or WIDGET_ID_INVALID. Must not be NULL.
 * @param outEntryId [out] Receives its entry, or WIDGET_ID_INVALID; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no menu bar.
 */
Error UIMenuBar_GetOpen(UIScreen* screen, uint64_t widgetId, uint64_t* outMenuId, uint64_t* outEntryId);

/**
 * @brief Closes a menu bar's open menu, if one is.
 * @param screen The bar's screen; must not be NULL.
 * @param widgetId The bar.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no menu bar; otherwise a hook's error.
 */
Error UIMenuBar_Close(UIScreen* screen, uint64_t widgetId);


// Helpers, for looks.
/**
 * @brief Lays a bar's entries out in a row inside a padding, each as wide as its label and the entry padding (their measures are cached);
 *        for a look's Layout.
 * @param widget The bar's widget; must not be NULL.
 * @param context The layout's context; must not be NULL.
 * @param padding Inside the bar, in its own UI pixels.
 * @returns Success, or an entry's Measure or placement error.
 */
Error UIMenuBar_ArrangeEntries(Widget* widget, const UILayoutContext* context, Vector2 padding);

/**
 * @brief Measures a bar's entries in a row inside a padding; for a look's Measure.
 * @param widget The bar's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param padding Inside the bar, in its own UI pixels.
 * @param outSize [out] Receives the size in its own UI pixels. Must not be NULL.
 * @returns Success, or an entry's Measure error.
 */
Error UIMenuBar_MeasureEntries(Widget* widget, const UIMeasureContext* context, Vector2 padding, Vector2* outSize);

/**
 * @brief Registers the kit's own widget type a menu bar's entries are; the kit calls it when it is constructed.
 * @param kit The kit; must not be NULL.
 * @param factory The kit's factory; must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIMenuBar_RegisterTypes(UIKit* kit, UIWidgetFactory* factory);
