#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIMenu.h"


/*
 * Private to the menus and the menu bar (UIMenu.c, UIMenuBar.c): how a menu bar opens its entries' menus and how such a menu asks its
 * bar to move to the entry beside.
 */


/** A menu level's own copy of its items and, after them, its submenus' items, with every text in one block. */
typedef struct UIMenuDataStruct
{
    UIMenuItem* Items;
    size_t Count;
    unsigned char* Strings;
} UIMenuData;

/** Checks a tree of items and copies it into one block (UI_MENU_MAX_ITEMS, UI_MENU_MAX_DEPTH); nothing to release on a failure. */
Error UIMenu_InternalCopyItems(const UIMenuItem* items, size_t count, UIMenuData* outData);

/** Releases a copy. */
void UIMenu_InternalReleaseItems(UIMenuData* data);

/** Opens a menu bar's entry's menu under the entry: owned by the bar (a press on it counts as inside), its Left and Right moving to the
 *  entries beside. Its first item is focused when @p isFromKey. */
Error UIMenu_InternalOpenForBar(UIScreen* screen, uint64_t barId, uint64_t entryId, const UIMenuItem* items, size_t count, UIMenuHandler handler,
    const UserData* userData, bool isFromKey, uint64_t* outMenuId);

/** Moves a menu bar's open menu to the entry beside (@p direction -1 or 1), its first item focused. */
Error UIMenuBar_InternalSwitch(UIScreen* screen, uint64_t barId, int32_t direction);

/** Registers the kit's own widget types a menu is made of: its list and its items. */
Error UIMenu_RegisterTypes(UIKit* kit, UIWidgetFactory* factory);
