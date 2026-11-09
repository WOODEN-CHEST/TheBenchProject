#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRUserData.h"
#include "GameKey.h"
#include "UIContextMenu.h"
#include "UIElement.h"
#include "UIPopup.h"


/**
 * @file UIMenu.h
 * @brief Menus as data: context menus, a menu bar's menus and submenus, each a popup list (UIPopup.h) of items.
 *
 * A MENU is a list of items, each a label with an optional icon, shortcut text, check, a submenu (items of its own, or built by the
 * menu's provider when it opens), or a separator; disabled items show and do nothing. UIMenu_Open copies the items (the whole tree) and
 * opens the first level as a popup against an anchor (a point for a context menu: UIMenu_OpenAt with a request, UIContextMenu.h).
 *
 * Items are press elements: the arrows move between them (keyboard navigation, kept inside the topmost menu), Enter or Space activates
 * one, and a click on one, or a release over one of a press that began elsewhere (a menu bar's entry: press, drag, release), activates
 * it, at the menu's next update (the item stays under the pointer until the screen has delivered the release, so what lies behind the
 * closing menu hears nothing of it). An item with a submenu opens it instead: after the pointer rests on it for
 * UI_MENU_SUBMENU_DELAY_SECONDS (resting on another item of the same menu closes it again), on a click, or on Right (with its first item
 * focused); Left closes a submenu (the focus goes back to its item), and Escape closes one level. In a menu bar's menu, Left and Right
 * with nothing more to open move to the menu of the entry beside. A menu closes on a press outside it, on Escape, when the widget it was opened for goes away, and when an item is
 * activated: then every level closes, the focus goes back to where it was, and the menu's handler is called with the item's id.
 *
 * Submenus nest at most UI_MENU_MAX_DEPTH levels, a provider's too. A submenu that cannot open (deeper than that, a provider's items
 * refused, as many popups open as a screen holds) does not, and is logged once when it happens: never an error of the screen's.
 *
 * The menu knows nothing of actions: the editor builds menus from its registered actions and runs one from the handler.
 */


// Forward declarations.
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;
/** @brief A menu being built by a provider; private, filled with UIMenuBuilder_Add. */
typedef struct UIMenuBuilderStruct UIMenuBuilder;


// Macros.
/** @brief How long the pointer rests on an item before its submenu opens, in seconds. */
#define UI_MENU_SUBMENU_DELAY_SECONDS 0.3
/** @brief The most items a menu holds, its submenus' counted. */
#define UI_MENU_MAX_ITEMS ((size_t)1024)
/** @brief The deepest a menu's submenus nest (the menu itself is one level). */
#define UI_MENU_MAX_DEPTH ((size_t)8)
/** @brief The longest label or shortcut text, in bytes. */
#define UI_MENU_MAX_TEXT_BYTES ((size_t)256)


/** @brief Room for any shortcut text UIMenu_WriteShortcut writes, its NUL included. */
#define UI_MENU_SHORTCUT_CAPACITY ((size_t)48)


// Types.
/**
 * @brief One item of a menu. A plain value; build one with UIMenuItem_Create or UIMenuItem_Separator. The menu copies what it points
 *        to when it opens.
 */
typedef struct UIMenuItemStruct
{
    /** @brief What the handler is told when it is activated. */
    uint32_t Id;
    /** @brief Its label as UTF-8; NULL only for a separator. */
    const unsigned char* Label;
    /** @brief Its icon, shown in the check column when it is not checked. */
    UIElementIcon Icon;
    /** @brief Shortcut text shown at its right (UTF-8), or NULL. */
    const unsigned char* Shortcut;
    /** @brief Whether it can be activated; a disabled item shows, is passed over by the keyboard and does nothing. */
    bool IsEnabled;
    /** @brief Whether it shows a check. */
    bool IsChecked;
    /** @brief Whether it is a separator: a line, never an item. */
    bool IsSeparator;
    /** @brief Its submenu as data, ChildCount items, or NULL. */
    const struct UIMenuItemStruct* Children;
    /** @brief How many items Children holds. */
    size_t ChildCount;
    /** @brief Whether it has a submenu the menu's provider builds when it opens (UIMenuOptions.SubmenuProvider), in place of Children. */
    bool HasProvidedSubmenu;
} UIMenuItem;

/**
 * @brief What a menu's handler is told when an item is activated. A plain value.
 */
typedef struct UIMenuActivationStruct
{
    /** @brief The item's id. */
    uint32_t ItemId;
    /** @brief The menu's owner (UIMenuOptions.OwnerId), or WIDGET_ID_INVALID. */
    uint64_t OwnerId;
    /** @brief The mouse button that activated it, or the key. */
    GameKey Button;
    /** @brief The GAME_KEY_MODIFIER_* flags held. */
    uint8_t Modifiers;
} UIMenuActivation;

/**
 * @brief Runs what an activated item stands for. Called once every level of the menu has closed, during the screen's update, as a hook
 *        is: it may create and destroy widgets and move the focus.
 * @param screen The screen.
 * @param activation What was activated; valid for the call.
 * @param userData The UserData of the menu's options (its copy).
 * @returns Success, or an Error that stops the screen's update as a broken invariant.
 */
typedef Error (*UIMenuHandler)(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData);

/**
 * @brief Builds the submenu of an item that has a provided submenu, when it opens.
 * @param screen The screen.
 * @param itemId The item whose submenu opens.
 * @param builder Where its items go (UIMenuBuilder_Add); valid for the call.
 * @param userData The UserData of the menu's options (its copy).
 * @returns Success, or an Error that stops the screen's update as a broken invariant.
 */
typedef Error (*UIMenuSubmenuProvider)(UIScreen* screen, uint32_t itemId, UIMenuBuilder* builder, const UserData* userData);

/**
 * @brief How a menu opens. A plain value; build one with UIMenuOptions_Create.
 */
typedef struct UIMenuOptionsStruct
{
    /** @brief The anchor's box in normalized screen coordinates (a point for a context menu). */
    Rectangle AnchorBox;
    /** @brief A widget whose box is the anchor, followed while it lives, or WIDGET_ID_INVALID. */
    uint64_t AnchorWidgetId;
    /** @brief The side it opens on; it flips when it does not fit. */
    UIPopupSide Side;
    /** @brief The widget it is for: told when it closes, and it closes when the widget goes; WIDGET_ID_INVALID for none. */
    uint64_t OwnerId;
    /** @brief Whether a press on the owner counts as inside the menu (a menu bar). */
    bool IsOwnerInside;
    /** @brief Whether its first enabled item is focused as it opens (opened by a key); otherwise nothing is, until a key moves in. */
    bool FocusesFirstItem;
    /** @brief Called with an activated item's id; may be NULL. */
    UIMenuHandler Handler;
    /** @brief Builds provided submenus; may be NULL. */
    UIMenuSubmenuProvider SubmenuProvider;
    /** @brief Handed to both. */
    UserData Data;
} UIMenuOptions;


// Helpers.
/**
 * @brief Makes an enabled item with a label and nothing else.
 * @param id What the handler is told.
 * @param label The label as UTF-8; must not be NULL.
 * @returns The item.
 */
static inline UIMenuItem UIMenuItem_Create(uint32_t id, const unsigned char* label)
{
    UIMenuItem Item = { .Id = id, .Label = label, .Shortcut = NULL, .IsEnabled = true, .IsChecked = false, .IsSeparator = false, .Children = NULL,
        .ChildCount = 0U, .HasProvidedSubmenu = false };
    Item.Icon = UIElementIcon_None();
    return Item;
}

/**
 * @brief Makes a separator.
 * @returns The item.
 */
static inline UIMenuItem UIMenuItem_Separator(void)
{
    UIMenuItem Item = UIMenuItem_Create(0U, NULL);
    Item.IsSeparator = true;
    Item.IsEnabled = false;
    return Item;
}

/**
 * @brief Default options: below a point at the surface's top-left, owned by nobody, nothing focused, no handler.
 * @returns The options.
 */
static inline UIMenuOptions UIMenuOptions_Create(void)
{
    return (UIMenuOptions)
    {
        .AnchorBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f },
        .AnchorWidgetId = 0U,
        .Side = UIPopupSide_Below,
        .OwnerId = 0U,
        .IsOwnerInside = false,
        .FocusesFirstItem = false,
        .Handler = NULL,
        .SubmenuProvider = NULL,
        .Data = UserData_CreateEmpty()
    };
}


// Functions.
/**
 * @brief Opens a menu: copies the items (the whole tree) and opens its first level as a popup.
 * @param screen The screen; must not be NULL. Its factory must have a kit.
 * @param items The items; may be NULL only when @p count is 0.
 * @param count How many.
 * @param options How it opens; must not be NULL.
 * @param outMenuId [out] Receives the menu's id (its popup's root, for UIMenu_Close); may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer that must not be NULL is; ErrorCode_InvalidOperation for a screen with no kit;
 *          ErrorCode_ArgumentOutOfRange for more than UI_MENU_MAX_ITEMS items, submenus deeper than UI_MENU_MAX_DEPTH, a label that is
 *          missing or past UI_MENU_MAX_TEXT_BYTES, or an icon that is not valid; ErrorCode_InvalidTextEncoding for text that is not valid
 *          UTF-8; otherwise an error from making the widgets or opening the popup.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UIMenu_Open(UIScreen* screen, const UIMenuItem* items, size_t count, const UIMenuOptions* options, uint64_t* outMenuId);

/**
 * @brief Opens a menu for a context request (UIContextMenu.h) at its place, owned by the widget it is for, its first item focused when a
 *        key asked: what a provider calls.
 * @param screen The screen; must not be NULL.
 * @param request The request; must not be NULL.
 * @param items The items; may be NULL only when @p count is 0.
 * @param count How many.
 * @param handler Called with an activated item's id; may be NULL.
 * @param userData Handed to it; copied. May be NULL for none.
 * @returns As UIMenu_Open.
 */
Error UIMenu_OpenAt(UIScreen* screen, const UIContextMenuRequest* request, const UIMenuItem* items, size_t count, UIMenuHandler handler,
    const UserData* userData);

/**
 * @brief Closes a menu with every submenu open over it. Nothing happens to one that is closed already.
 * @param screen The screen; must not be NULL.
 * @param menuId The menu (UIMenu_Open's id).
 * @returns Success; ErrorCode_IllegalArgument if @p screen is NULL; otherwise a hook's error.
 */
Error UIMenu_Close(UIScreen* screen, uint64_t menuId);

/**
 * @brief Adds an item to a menu a provider builds: copied at once, with its texts and its children, so they may live in a buffer the
 *        provider reuses.
 * @param builder The builder; must not be NULL.
 * @param item The item; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_ArgumentOutOfRange past UI_MENU_MAX_ITEMS items or for an
 *          item that is not valid; ErrorCode_InvalidTextEncoding for text that is not valid UTF-8.
 */
Error UIMenuBuilder_Add(UIMenuBuilder* builder, const UIMenuItem* item);

/**
 * @brief Writes an action's first binding as a shortcut text ("Ctrl+Z"), for a menu item's Shortcut: what a context menu shows next to an
 *        item an action also does. Only text: nothing binds it.
 * @param screen The screen whose running update's input knows the bindings; must not be NULL.
 * @param action The action.
 * @param outText [out] Receives the NUL-terminated text, empty when the action has no binding (or no input), or when it does not fit;
 *        must not be NULL.
 * @param capacity The size of @p outText in bytes, its NUL included; at least 1 (UI_MENU_SHORTCUT_CAPACITY holds every binding).
 */
void UIMenu_WriteShortcut(const UIScreen* screen, GameActionId action, unsigned char* outText, size_t capacity);
