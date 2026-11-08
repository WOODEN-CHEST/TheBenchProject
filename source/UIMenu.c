#include "UIMenu.h"
#include "UIMenuInternal.h"
#include "Logger.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"
#include "wr/WRString.h"


// Macros.
/** The gap between an item's label and its shortcut, and before its submenu arrow, in UI pixels. */
#define ITEM_GAP 16.0f
/** How far a check stands inside its column, in UI pixels. */
#define CHECK_INSET 5.0f


// Types.
/* What a menu list is made with. */
typedef struct MenuListArgsStruct
{
    UIKit* Kit;
    const UIMenuItem* Items;
    size_t Count;
    UIMenuHandler Handler;
    UIMenuSubmenuProvider Provider;
    const UserData* Data;
    uint64_t OwnerId;
    uint64_t RootMenuId;
    uint64_t ParentMenuId;
    uint64_t BarId;
} MenuListArgs;

/* One level of a menu: a popup's root holding an item widget per item. */
typedef struct MenuListStruct
{
    Widget Base;
    UIElementLook Look;
    UIKit* Kit;
    UIMenuData Data;
    UIMenuHandler Handler;
    UIMenuSubmenuProvider Provider;
    UserData UserData;
    uint64_t OwnerId;
    uint64_t RootMenuId;
    uint64_t ParentMenuId;
    uint64_t BarId;
    uint64_t SubmenuId;
    size_t SubmenuItem;
    bool HasRestItem;
    size_t RestItem;
    double RestSeconds;
    bool HasRested;
    /* An item a mouse release activated, acted on at the list's next update: the item stays under the pointer until the screen has
     * delivered that release, so what lies behind the closing menu hears nothing of it. */
    bool HasPendingActivation;
    size_t PendingItem;
    GameKey PendingButton;
    uint8_t PendingModifiers;
} MenuList;

/* What an item is made with. */
typedef struct MenuItemArgsStruct
{
    UIKit* Kit;
    uint64_t ListId;
    size_t Index;
    const UIMenuItem* Item;
} MenuItemArgs;

/* One item of a menu level. */
typedef struct MenuItemStruct
{
    Widget Base;
    UIElementLook Look;
    UIElementText Label;
    UIElementText Shortcut;
    UIElementIconView Icon;
    UITextContent LabelContent;
    UITextContent ShortcutContent;
    UIPressable Press;
    uint64_t ListId;
    size_t Index;
} MenuItem;

/* A menu a provider builds: each item added is copied at once, with its texts and its children, since what the provider pointed to may be
 * gone when it returns (a label written in a buffer of its own). */
struct UIMenuBuilderStruct
{
    UIMenuItem Items[UI_MENU_MAX_ITEMS];
    UIMenuData Copies[UI_MENU_MAX_ITEMS];
    size_t Count;
};


// Static functions: copying a menu's items.
static Error CheckText(const unsigned char* text, bool isRequired)
{
    if (text == NULL)
    {
        return isRequired ? Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIMenu: an item that is no separator needs a label.")
            : Error_CreateSuccess();
    }
    if (StringUTF8_GetByteLength(text) > UI_MENU_MAX_TEXT_BYTES)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIMenu: a label or a shortcut is longer than UI_MENU_MAX_TEXT_BYTES.");
    }
    return StringUTF8_IsEncodingValid(text) ? Error_CreateSuccess()
        : Error_Construct2(ErrorCode_InvalidTextEncoding, "UIMenu: a label or a shortcut is not valid UTF-8.");
}

static Error CheckItem(const UIMenuItem* item)
{
    if (!item->IsSeparator && !UIElementIcon_IsValid(&item->Icon))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIMenu: an item's icon is not valid.");
    }
    Error Result = CheckText(item->Label, !item->IsSeparator);
    return (Result.Code == ErrorCode_Success) ? CheckText(item->Shortcut, false) : Result;
}

static size_t GetTextBytes(const unsigned char* text)
{
    return (text != NULL) ? (StringUTF8_GetByteLength(text) + 1U) : 0U;
}

/* Counts a tree's items and text bytes, checking every item; bounded by UI_MENU_MAX_DEPTH and UI_MENU_MAX_ITEMS. */
static Error CountTree(const UIMenuItem* items, size_t count, size_t depth, size_t* ioItems, size_t* ioBytes)
{
    if (depth > UI_MENU_MAX_DEPTH)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIMenu: submenus nest deeper than UI_MENU_MAX_DEPTH.");
    }
    if ((count > 0U) && (items == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenu: the items must not be NULL.");
    }
    if ((count > UI_MENU_MAX_ITEMS) || (*ioItems > (UI_MENU_MAX_ITEMS - count)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIMenu: more than UI_MENU_MAX_ITEMS items.");
    }
    *ioItems += count;
    for (size_t Index = 0U; Index < count; Index++)
    {
        Error Result = CheckItem(&items[Index]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        *ioBytes += GetTextBytes(items[Index].Label) + GetTextBytes(items[Index].Shortcut);
        if (!items[Index].IsSeparator && (items[Index].ChildCount > 0U))
        {
            Result = CountTree(items[Index].Children, items[Index].ChildCount, depth + 1U, ioItems, ioBytes);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }
    return Error_CreateSuccess();
}

static const unsigned char* CopyText(const unsigned char* text, unsigned char* strings, size_t* ioAt)
{
    if (text == NULL)
    {
        return NULL;
    }
    size_t Bytes = StringUTF8_GetByteLength(text) + 1U;
    unsigned char* Copy = strings + *ioAt;
    Memory_Copy(text, Copy, Bytes);
    *ioAt += Bytes;
    return Copy;
}

/* Copies a level into @p out (room for @p count), then each item's submenu after the items placed so far. Checked already by CountTree. */
static void FillTree(const UIMenuItem* items, size_t count, UIMenuItem* all, UIMenuItem* out, size_t* ioNext, unsigned char* strings, size_t* ioAt)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        out[Index] = items[Index];
        out[Index].Label = CopyText(items[Index].Label, strings, ioAt);
        out[Index].Shortcut = CopyText(items[Index].Shortcut, strings, ioAt);
        out[Index].Children = NULL;
        out[Index].ChildCount = items[Index].IsSeparator ? 0U : items[Index].ChildCount;
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (out[Index].ChildCount == 0U)
        {
            continue;
        }
        UIMenuItem* Children = all + *ioNext;
        *ioNext += out[Index].ChildCount;
        out[Index].Children = Children;
        FillTree(items[Index].Children, out[Index].ChildCount, all, Children, ioNext, strings, ioAt);
    }
}

Error UIMenu_InternalCopyItems(const UIMenuItem* items, size_t count, UIMenuData* outData)
{
    *outData = (UIMenuData){ .Items = NULL, .Count = 0U, .Strings = NULL };
    size_t ItemCount = 0U;
    size_t Bytes = 0U;
    Error Result = CountTree(items, count, 1U, &ItemCount, &Bytes);
    if ((Result.Code != ErrorCode_Success) || (count == 0U))
    {
        return Result;
    }
    outData->Items = Memory_Allocate(ItemCount * sizeof(UIMenuItem));
    outData->Strings = (Bytes > 0U) ? Memory_Allocate(Bytes) : NULL;
    outData->Count = count;
    size_t Next = count;
    size_t At = 0U;
    FillTree(items, count, outData->Items, outData->Items, &Next, outData->Strings, &At);
    return Error_CreateSuccess();
}

void UIMenu_InternalReleaseItems(UIMenuData* data)
{
    Memory_Free(data->Items);
    Memory_Free(data->Strings);
    *data = (UIMenuData){ .Items = NULL, .Count = 0U, .Strings = NULL };
}


// Static functions: helpers.
static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

static MenuList* ResolveList(const UIScreen* screen, uint64_t id)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(screen, id, &Found) ? (MenuList*)(void*)Found : NULL;
}

static const UIThemeStyle* GetStyle(const UIElementLook* look, const Widget* widget)
{
    return UIElementLook_GetStyle(look, Widget_GetScreen(widget));
}

static bool HasSubmenu(const UIMenuItem* item)
{
    return !item->IsSeparator && ((item->ChildCount > 0U) || item->HasProvidedSubmenu);
}

static bool IsActivatable(const UIMenuItem* item)
{
    return !item->IsSeparator && item->IsEnabled;
}

/* The item widget of a list's item index, or WIDGET_ID_INVALID: items are the list's children in their order. */
static uint64_t GetItemWidgetId(const MenuList* list, size_t index)
{
    return (index < Widget_GetSubWidgetCount(&list->Base)) ? Widget_GetId(Widget_GetSubWidgetAt(&list->Base, index)) : WIDGET_ID_INVALID;
}

static uint64_t GetFirstEnabledItem(const MenuList* list)
{
    for (size_t Index = 0U; Index < list->Data.Count; Index++)
    {
        if (IsActivatable(&list->Data.Items[Index]))
        {
            return GetItemWidgetId(list, Index);
        }
    }
    return WIDGET_ID_INVALID;
}

static Error CreateDetached(UIScreen* screen, UIKit* kit, uint64_t typeId, void* args, uint64_t* outId)
{
    Widget* Made = NULL;
    *outId = WIDGET_ID_INVALID;
    Error Result = UIWidgetFactory_CreateWidget(kit->_factory, screen, typeId, args, &Made);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outId = Widget_GetId(Made);
    Result = Widget_InitializeWidget(Made);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = Widget_Destroy(Made);
        Error_Deconstruct(&Cleanup);
        *outId = WIDGET_ID_INVALID;
    }
    return Result;
}

static void DestroyIfAlive(UIScreen* screen, uint64_t id)
{
    Widget* Found = NULL;
    if (UIScreen_TryGetWidgetById(screen, id, &Found))
    {
        Error Cleanup = Widget_Destroy(Found);
        Error_Deconstruct(&Cleanup);
    }
}

/* Makes a menu level, detached: the list with an item widget per item. */
static Error CreateMenuList(UIScreen* screen, MenuListArgs* args, uint64_t* outListId)
{
    uint64_t ListId = WIDGET_ID_INVALID;
    Error Result = CreateDetached(screen, args->Kit, args->Kit->_menuListType, args, &ListId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    MenuList* List = ResolveList(screen, ListId);
    if ((List != NULL) && (List->RootMenuId == WIDGET_ID_INVALID))
    {
        List->RootMenuId = ListId; // The first level is its own root.
    }
    size_t Count = (List != NULL) ? List->Data.Count : 0U;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        List = ResolveList(screen, ListId);
        MenuItemArgs ItemArgs = { .Kit = args->Kit, .ListId = ListId, .Index = Index, .Item = (List != NULL) ? &List->Data.Items[Index] : NULL };
        uint64_t ItemId = WIDGET_ID_INVALID;
        Result = (List != NULL) ? CreateDetached(screen, args->Kit, args->Kit->_menuItemType, &ItemArgs, &ItemId)
            : Error_Construct2(ErrorCode_InvalidOperation, "UIMenu: a hook took the menu away.");
        Widget* Item = NULL;
        Widget* Parent = NULL;
        if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, ItemId, &Item) && UIScreen_TryGetWidgetById(screen, ListId, &Parent))
        {
            Result = Widget_AddSubWidget(Parent, Item);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyIfAlive(screen, ListId);
        return Result;
    }
    *outListId = ListId;
    return Error_CreateSuccess();
}

/* Opens a made menu level as a popup. */
static Error OpenListAsPopup(UIScreen* screen, uint64_t listId, const UIPopupOptions* options)
{
    Error Result = UIScreen_OpenPopup(screen, listId, options);
    if (Result.Code != ErrorCode_Success)
    {
        DestroyIfAlive(screen, listId);
    }
    return Result;
}

/* A submenu that cannot open (a provider's items refused, submenus nested past UI_MENU_MAX_DEPTH, as many popups open as a screen holds)
 * is no failure of the screen's: nothing opens, and it is said once, when it happens. */
static Error ReportUnopened(UIScreen* screen, const char* why)
{
    Logger* Log = UIScreen_GetLogger(screen);
    if (Log != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(Log, (const unsigned char*)u8"UIMenu: a submenu did not open: %s", why);
        Error_Deconstruct(&LogResult);
    }
    return Error_CreateSuccess();
}

/* How deep a menu level is: 1 for a menu, 2 for its submenu, and so on. */
static size_t GetDepth(const UIScreen* screen, const MenuList* list)
{
    size_t Depth = 1U;
    const MenuList* Level = list;
    while ((Level->ParentMenuId != WIDGET_ID_INVALID) && (Depth < UI_SCREEN_MAX_POPUPS))
    {
        const MenuList* Parent = ResolveList(screen, Level->ParentMenuId);
        if (Parent == NULL)
        {
            break;
        }
        Level = Parent;
        Depth++;
    }
    return Depth;
}

static void ReleaseBuilder(UIMenuBuilder* builder)
{
    if (builder == NULL)
    {
        return;
    }
    for (size_t Index = 0U; Index < builder->Count; Index++)
    {
        UIMenu_InternalReleaseItems(&builder->Copies[Index]);
    }
    Memory_Free(builder);
}

/* Opens the submenu of a list's item: closing another of the list's first. By the keyboard its first item is focused; by a click the focus
 * moves in to nothing; by the pointer resting it stays where it is. */
static Error OpenSubmenu(UIScreen* screen, uint64_t listId, size_t index, bool movesFocus, bool focusesFirst)
{
    MenuList* List = ResolveList(screen, listId);
    if ((List == NULL) || (index >= List->Data.Count) || !HasSubmenu(&List->Data.Items[index]))
    {
        return Error_CreateSuccess();
    }
    if (GetDepth(screen, List) >= UI_MENU_MAX_DEPTH)
    {
        return ReportUnopened(screen, "submenus nest deeper than UI_MENU_MAX_DEPTH.");
    }
    if (UIScreen_IsPopupOpen(screen, List->SubmenuId) && (List->SubmenuItem == index))
    {
        MenuList* Open = ResolveList(screen, List->SubmenuId);
        Widget* First = NULL;
        if (focusesFirst && (Open != NULL) && UIScreen_TryGetWidgetById(screen, GetFirstEnabledItem(Open), &First))
        {
            return UIScreen_SetFocusedWidget(screen, First);
        }
        return Error_CreateSuccess();
    }
    Error Result = UIScreen_IsPopupOpen(screen, List->SubmenuId) ? UIScreen_ClosePopup(screen, List->SubmenuId) : Error_CreateSuccess();
    if ((Result.Code != ErrorCode_Success) || ((List = ResolveList(screen, listId)) == NULL))
    {
        return Result;
    }
    if (UIScreen_GetPopupCount(screen) >= UI_SCREEN_MAX_POPUPS)
    {
        return ReportUnopened(screen, "as many popups as a screen holds are open.");
    }
    uint64_t ItemWidgetId = GetItemWidgetId(List, index);
    const UIMenuItem* Item = &List->Data.Items[index];
    UIMenuBuilder* Builder = NULL;
    const UIMenuItem* Children = Item->Children;
    size_t ChildCount = Item->ChildCount;
    if (Item->HasProvidedSubmenu && (List->Provider != NULL))
    {
        Builder = Memory_Allocate(sizeof(UIMenuBuilder));
        Builder->Count = 0U;
        UserData Data = List->UserData;
        Result = List->Provider(screen, Item->Id, Builder, &Data);
        Children = Builder->Items;
        ChildCount = Builder->Count;
        if ((Result.Code == ErrorCode_Success) && ((List = ResolveList(screen, listId)) == NULL))
        {
            ReleaseBuilder(Builder);
            return Error_CreateSuccess(); // The provider took the menu away: nothing to open it in.
        }
    }
    uint64_t SubmenuId = WIDGET_ID_INVALID;
    if ((Result.Code == ErrorCode_Success) && (ChildCount > 0U))
    {
        MenuListArgs Arguments = { .Kit = List->Kit, .Items = Children, .Count = ChildCount, .Handler = List->Handler, .Provider = List->Provider,
            .Data = &List->UserData, .OwnerId = ItemWidgetId, .RootMenuId = List->RootMenuId, .ParentMenuId = listId, .BarId = List->BarId };
        Result = CreateMenuList(screen, &Arguments, &SubmenuId);
        if ((Result.Code == ErrorCode_ArgumentOutOfRange) || (Result.Code == ErrorCode_InvalidTextEncoding))
        {
            Error_Deconstruct(&Result);
            ReleaseBuilder(Builder);
            return ReportUnopened(screen, "its items were refused (too many, or a text that is not valid).");
        }
    }
    ReleaseBuilder(Builder);
    if ((Result.Code != ErrorCode_Success) || (SubmenuId == WIDGET_ID_INVALID))
    {
        return Result;
    }
    MenuList* Submenu = ResolveList(screen, SubmenuId);
    UIPopupOptions Options = UIPopupOptions_Create();
    Options.AnchorWidgetId = ItemWidgetId;
    Options.Side = UIPopupSide_Right;
    Options.OwnerId = ItemWidgetId;
    Options.IsOwnerInside = true;
    Options.MovesFocus = movesFocus;
    Options.FocusId = (focusesFirst && (Submenu != NULL)) ? GetFirstEnabledItem(Submenu) : WIDGET_ID_INVALID;
    Result = OpenListAsPopup(screen, SubmenuId, &Options);
    if ((Result.Code == ErrorCode_Success) && ((List = ResolveList(screen, listId)) != NULL))
    {
        List->SubmenuId = SubmenuId;
        List->SubmenuItem = index;
    }
    return Result;
}

/* Activates an item: its submenu opens; any other closes every level and calls the handler. */
static Error ActivateItem(UIScreen* screen, uint64_t listId, size_t index, GameKey button, uint8_t modifiers, bool isFromKey)
{
    MenuList* List = ResolveList(screen, listId);
    if ((List == NULL) || (index >= List->Data.Count) || !IsActivatable(&List->Data.Items[index]))
    {
        return Error_CreateSuccess();
    }
    if (HasSubmenu(&List->Data.Items[index]))
    {
        return OpenSubmenu(screen, listId, index, true, isFromKey);
    }
    // Everything the handler needs is copied out first: closing destroys the menus.
    UIMenuHandler Handler = List->Handler;
    UserData Data = List->UserData;
    UIMenuActivation Activation = { .ItemId = List->Data.Items[index].Id, .OwnerId = List->OwnerId, .Button = button, .Modifiers = modifiers };
    MenuList* Root = ResolveList(screen, List->RootMenuId);
    if (Root != NULL)
    {
        Activation.OwnerId = Root->OwnerId;
    }
    uint64_t RootId = List->RootMenuId;
    Error Result = UIScreen_IsPopupOpen(screen, RootId) ? UIScreen_ClosePopup(screen, RootId) : Error_CreateSuccess();
    if ((Result.Code != ErrorCode_Success) || (Handler == NULL))
    {
        return Result;
    }
    return Handler(screen, &Activation, &Data);
}


// Static functions: the list.
static Error List_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenu: a list's arguments are required.");
    }
    const MenuListArgs* Arguments = args;
    MenuList* List = widget;
    Error Result = UIElementLook_Construct(&List->Look, UIElementKind_Menu, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIMenu_InternalCopyItems(Arguments->Items, Arguments->Count, &List->Data);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    List->Kit = Arguments->Kit;
    List->Handler = Arguments->Handler;
    List->Provider = Arguments->Provider;
    List->UserData = (Arguments->Data != NULL) ? *Arguments->Data : UserData_CreateEmpty();
    List->OwnerId = Arguments->OwnerId;
    List->RootMenuId = Arguments->RootMenuId;
    List->ParentMenuId = Arguments->ParentMenuId;
    List->BarId = Arguments->BarId;
    List->SubmenuId = WIDGET_ID_INVALID;
    List->SubmenuItem = 0U;
    List->HasRestItem = false;
    List->RestItem = 0U;
    List->RestSeconds = 0.0;
    List->HasRested = false;
    List->HasPendingActivation = false;
    List->PendingItem = 0U;
    List->PendingButton = GameKey_Unknown;
    List->PendingModifiers = 0U;
    return Error_CreateSuccess();
}

static Error List_OnDeconstruct(void* self)
{
    UIMenu_InternalReleaseItems(&((MenuList*)self)->Data);
    return Error_CreateSuccess();
}

/* The item the pointer is on, if any. */
static bool TryGetHoveredItem(const MenuList* list, size_t* outIndex)
{
    for (size_t Index = 0U; Index < Widget_GetSubWidgetCount(&list->Base); Index++)
    {
        if (Widget_IsHovered(Widget_GetSubWidgetAt(&list->Base, Index)))
        {
            *outIndex = Index;
            return true;
        }
    }
    return false;
}

/* The pointer resting on an item opens its submenu, without moving the focus; resting on another closes the open one. */
static Error List_Update(void* self, ProgramTime time)
{
    MenuList* List = self;
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    if (List->HasPendingActivation)
    {
        List->HasPendingActivation = false;
        return ActivateItem(Screen, Widget_GetId(&List->Base), List->PendingItem, List->PendingButton, List->PendingModifiers, false);
    }
    UIElementLook_Update(&List->Look, Screen, UIElementState_Normal, time.PassedTime);
    if ((List->SubmenuId != WIDGET_ID_INVALID) && !UIScreen_IsPopupOpen(Screen, List->SubmenuId))
    {
        List->SubmenuId = WIDGET_ID_INVALID;
    }
    size_t Hovered = 0U;
    bool IsHovering = TryGetHoveredItem(List, &Hovered);
    if ((IsHovering != List->HasRestItem) || (IsHovering && (Hovered != List->RestItem)))
    {
        List->HasRestItem = IsHovering;
        List->RestItem = Hovered;
        List->RestSeconds = 0.0;
        List->HasRested = false;
        return Error_CreateSuccess();
    }
    if (!IsHovering || List->HasRested)
    {
        return Error_CreateSuccess();
    }
    List->RestSeconds += time.PassedTime;
    if (List->RestSeconds < UI_MENU_SUBMENU_DELAY_SECONDS)
    {
        return Error_CreateSuccess();
    }
    List->HasRested = true;
    const UIMenuItem* Item = &List->Data.Items[Hovered];
    if (HasSubmenu(Item) && Item->IsEnabled)
    {
        return OpenSubmenu(Screen, Widget_GetId(&List->Base), Hovered, false, false);
    }
    return UIScreen_IsPopupOpen(Screen, List->SubmenuId) ? UIScreen_ClosePopup(Screen, List->SubmenuId) : Error_CreateSuccess();
}

static float GetItemHeight(const UIThemeStyle* style, const UIMenuItem* item)
{
    return UIThemeStyle_GetNumber(style, UIElementKind_Menu, item->IsSeparator ? UIMenuPart_SeparatorHeight : UIMenuPart_ItemHeight);
}

/* Its items in a column inside its padding. */
static Error List_Layout(void* self, const UILayoutContext* context)
{
    (void)context;
    MenuList* List = self;
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    const UIThemeStyle* Style = GetStyle(&List->Look, &List->Base);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Menu, UIMenuPart_Padding);
    float Width = UIScreen_GetWidgetPixelBox(Screen, &List->Base).width / Widget_GetEffectiveScale(&List->Base);
    float Y = Padding.y;
    for (size_t Index = 0U; (Index < Widget_GetSubWidgetCount(&List->Base)) && (Index < List->Data.Count); Index++)
    {
        float Height = GetItemHeight(Style, &List->Data.Items[Index]);
        UIPlacement Placement = UIPlacement_FromTopLeft(Padding.x, Y, Math_MaxFloat(0.0f, Width - (2.0f * Padding.x)), Height);
        Error Result = Widget_SetPlacement(Widget_GetSubWidgetAt(&List->Base, Index), &Placement);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Y += Height;
    }
    return Error_CreateSuccess();
}

static Error List_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)availableWidth;
    MenuList* List = self;
    const UIThemeStyle* Style = GetStyle(&List->Look, &List->Base);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Menu, UIMenuPart_Padding);
    float Width = 0.0f;
    float Height = 0.0f;
    for (size_t Index = 0U; (Index < Widget_GetSubWidgetCount(&List->Base)) && (Index < List->Data.Count); Index++)
    {
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = Widget_Measure(Widget_GetSubWidgetAt(&List->Base, Index), context, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Width = Math_MaxFloat(Width, Size.x);
        Height += GetItemHeight(Style, &List->Data.Items[Index]);
    }
    outSize->x = Width + (2.0f * Padding.x);
    outSize->y = Height + (2.0f * Padding.y);
    return Error_CreateSuccess();
}

static Error List_Render(void* self, UIRenderContext* context)
{
    MenuList* List = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    return UIElementLook_DrawBox(&List->Look, GetStyle(&List->Look, &List->Base), UIMenuPart_Background, context, Pixels,
        Widget_GetEffectiveScale(&List->Base));
}

/* Left and Right its items left: Left closes a submenu, or in a menu bar's menu moves to the entry before; Right in a menu bar's menu
 * moves to the entry after. */
static Error List_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    MenuList* List = self;
    UIScreen* Screen = Widget_GetScreen(&List->Base);
    if (args->Type != UIKeyboardInputType_KeyPress)
    {
        return Error_CreateSuccess();
    }
    bool IsLeft = UIElement_IsActionKey(Screen, GameAction_UILeft, args->Key);
    bool IsRight = UIElement_IsActionKey(Screen, GameAction_UIRight, args->Key);
    if (!IsLeft && !IsRight)
    {
        return Error_CreateSuccess();
    }
    if (IsLeft && (List->ParentMenuId != WIDGET_ID_INVALID))
    {
        // It closes, and the focus goes to its item (a submenu opened by the pointer resting had taken none).
        *outIsHandled = true;
        uint64_t ItemId = List->OwnerId;
        Error Result = UIScreen_ClosePopup(Screen, Widget_GetId(&List->Base));
        Widget* Item = NULL;
        if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(Screen, ItemId, &Item) && Widget_IsFocusable(Item))
        {
            Result = UIScreen_SetFocusedWidget(Screen, Item);
        }
        return Result;
    }
    MenuList* Root = ResolveList(Screen, List->RootMenuId);
    if ((Root == NULL) || (Root->BarId == WIDGET_ID_INVALID))
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    return UIMenuBar_InternalSwitch(Screen, Root->BarId, IsLeft ? -1 : 1);
}


// Static functions: the items.
static MenuList* GetItemList(const MenuItem* item)
{
    return ResolveList(Widget_GetScreen(&item->Base), item->ListId);
}

/* The item's data in its list, or NULL. */
static const UIMenuItem* GetItemData(const MenuItem* item)
{
    MenuList* List = GetItemList(item);
    return ((List != NULL) && (item->Index < List->Data.Count)) ? &List->Data.Items[item->Index] : NULL;
}

static Error Item_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenu: an item's arguments are required.");
    }
    const MenuItemArgs* Arguments = args;
    MenuItem* Item = widget;
    const UIMenuItem* Data = Arguments->Item;
    Error Result = UIElementLook_Construct(&Item->Look, UIElementKind_Menu, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Item->LabelContent, (Data != NULL) ? Data->Label : NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITextContent_Construct(&Item->ShortcutContent, (Data != NULL) ? Data->Shortcut : NULL);
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Item->LabelContent);
        return Result;
    }
    UIKit* Kit = Arguments->Kit;
    Result = UIElementText_Construct(&Item->Label, UIKit_GetTextFactory(Kit), UIKit_GetAssets(Kit), UIKit_GetLogger(Kit));
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementText_Construct(&Item->Shortcut, UIKit_GetTextFactory(Kit), UIKit_GetAssets(Kit), UIKit_GetLogger(Kit));
        if (Result.Code != ErrorCode_Success)
        {
            Error Cleanup = UIElementText_Deconstruct(&Item->Label);
            Error_Deconstruct(&Cleanup);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementIconView_Construct(&Item->Icon, UIKit_GetTextFactory(Kit), UIKit_GetAssets(Kit), UIKit_GetLogger(Kit));
        if (Result.Code != ErrorCode_Success)
        {
            Error Cleanup = UIElementText_Deconstruct(&Item->Shortcut);
            Error_Deconstruct(&Cleanup);
            Cleanup = UIElementText_Deconstruct(&Item->Label);
            Error_Deconstruct(&Cleanup);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        UITextContent_Deconstruct(&Item->ShortcutContent);
        UITextContent_Deconstruct(&Item->LabelContent);
        return Result;
    }
    Item->Press = UIPressable_Create();
    Item->Press._clickMode = UIClickMode_OnRelease;
    Item->Press._activationButtons = (uint8_t)(UI_BUTTON_BIT(GameKey_MouseLeft) | UI_BUTTON_BIT(GameKey_MouseRight));
    Item->ListId = Arguments->ListId;
    Item->Index = Arguments->Index;
    Error Focus = Widget_SetIsFocusable(widget, (Data != NULL) && IsActivatable(Data)); // Not alive yet: only the flag.
    Error_Deconstruct(&Focus);
    return Error_CreateSuccess();
}

static Error Item_OnDeconstruct(void* self)
{
    MenuItem* Item = self;
    Error First = UIElementIconView_Deconstruct(&Item->Icon);
    Error Second = UIElementText_Deconstruct(&Item->Shortcut);
    Error Third = UIElementText_Deconstruct(&Item->Label);
    UITextContent_Deconstruct(&Item->ShortcutContent);
    UITextContent_Deconstruct(&Item->LabelContent);
    if (First.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Second);
        Error_Deconstruct(&Third);
        return First;
    }
    if (Second.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Third);
        return Second;
    }
    return Third;
}

static Error Item_Refresh(MenuItem* item)
{
    const UIMenuItem* Data = GetItemData(item);
    Error Result = UIElementText_Refresh(&item->Label, &item->Base, &item->LabelContent);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementText_Refresh(&item->Shortcut, &item->Base, &item->ShortcutContent);
    }
    UIElementIcon None = UIElementIcon_None();
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementIconView_Refresh(&item->Icon, &item->Base, ((Data != NULL) && !Data->IsSeparator) ? &Data->Icon : &None);
    }
    return Result;
}

static UIElementState GetItemState(const MenuItem* item)
{
    const UIMenuItem* Data = GetItemData(item);
    const MenuList* List = GetItemList(item);
    bool IsEnabled = (Data != NULL) && IsActivatable(Data);
    bool IsSubmenuOpen = (List != NULL) && UIScreen_IsPopupOpen(Widget_GetScreen(&item->Base), List->SubmenuId) && (List->SubmenuItem == item->Index);
    return UIElementState_Choose(IsEnabled, UIPressable_IsPressed(&item->Press), false, false, IsSubmenuOpen, Widget_IsHovered(&item->Base),
        Widget_IsFocused(&item->Base));
}

static Error Item_Update(void* self, ProgramTime time)
{
    MenuItem* Item = self;
    UIScreen* Screen = Widget_GetScreen(&Item->Base);
    UIPressable_Update(&Item->Press, Screen);
    UIElementLook_Update(&Item->Look, Screen, GetItemState(Item), time.PassedTime);
    Error Result = Item_Refresh(Item);
    return (Result.Code == ErrorCode_Success) ? UIElementIconView_Update(&Item->Icon, time) : Result;
}

static Error Item_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    MenuItem* Item = self;
    Error Result = Item_Refresh(Item);
    const UIScreen* Screen = Widget_GetScreen(&Item->Base);
    const UIThemeStyle* Style = GetStyle(&Item->Look, &Item->Base);
    const UIMenuItem* Data = GetItemData(Item);
    float Scale = Widget_GetEffectiveScale(&Item->Base);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Menu, UIMenuPart_ItemPadding);
    float Check = UIThemeStyle_GetNumber(Style, UIElementKind_Menu, UIMenuPart_CheckWidth);
    float Label = UIElementText_GetPixelSize(&Item->Label, Screen).x / Scale;
    float Shortcut = UIElementText_GetPixelSize(&Item->Shortcut, Screen).x / Scale;
    float Arrow = ((Data != NULL) && HasSubmenu(Data)) ? (ITEM_GAP + (UIThemeStyle_GetNumber(Style, UIElementKind_Menu, UIMenuPart_ItemHeight) * 0.3f)) : 0.0f;
    outSize->x = (2.0f * Padding.x) + Check + Label + ((Shortcut > 0.0f) ? (ITEM_GAP + Shortcut) : 0.0f) + Arrow;
    outSize->y = (Data != NULL) ? GetItemHeight(Style, Data) : 0.0f;
    return Result;
}

static Error Item_RenderSeparator(const MenuItem* item, UIRenderContext* context, Rectangle pixels, const UIThemeStyle* style, float scale)
{
    Vector2 Padding = UIThemeStyle_GetVector(style, UIElementKind_Menu, UIMenuPart_ItemPadding);
    float Thickness = Math_MaxFloat(1.0f, RoundToPixel(UIThemeStyle_GetNumber(style, UIElementKind_Menu, UIMenuPart_SeparatorThickness) * scale));
    float Inset = RoundToPixel(Padding.x * scale * 0.5f);
    Rectangle Line = { .x = pixels.x + Inset, .y = pixels.y + RoundToPixel((pixels.height - Thickness) * 0.5f),
        .width = Math_MaxFloat(0.0f, pixels.width - (2.0f * Inset)), .height = Thickness };
    return UIElementLook_DrawBox(&item->Look, style, UIMenuPart_Separator, context, Line, scale);
}

static Error Item_Render(void* self, UIRenderContext* context)
{
    MenuItem* Item = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    const UIMenuItem* Data = GetItemData(Item);
    if (UIElement_IsBoxEmpty(Pixels) || (Data == NULL))
    {
        return Error_CreateSuccess();
    }
    const UIScreen* Screen = Widget_GetScreen(&Item->Base);
    const UIThemeStyle* Style = GetStyle(&Item->Look, &Item->Base);
    float Scale = Widget_GetEffectiveScale(&Item->Base);
    if (Data->IsSeparator)
    {
        return Item_RenderSeparator(Item, context, Pixels, Style, Scale);
    }
    Error Result = UIElementLook_DrawBox(&Item->Look, Style, UIMenuPart_Item, context, Pixels, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Color TextColor = UIElementLook_BlendColors(&Item->Look, UIThemeStyle_GetColors(Style, UIMenuPart_Text), WHITE);
    Color ShortcutColor = UIElementLook_BlendColors(&Item->Look, UIThemeStyle_GetColors(Style, UIMenuPart_Shortcut), TextColor);
    float PaddingX = RoundToPixel(UIThemeStyle_GetVector(Style, UIElementKind_Menu, UIMenuPart_ItemPadding).x * Scale);
    float Check = RoundToPixel(UIThemeStyle_GetNumber(Style, UIElementKind_Menu, UIMenuPart_CheckWidth) * Scale);
    float Left = Pixels.x + PaddingX;
    // The check column: a check, else the icon.
    if (Data->IsChecked)
    {
        float Inset = RoundToPixel(CHECK_INSET * Scale);
        float Side = Math_MaxFloat(1.0f, Math_MinFloat(Check, Pixels.height) - (2.0f * Inset));
        UIRenderContext_RenderPixelRectangle(context, (Rectangle){ .x = Left + RoundToPixel((Check - Side) * 0.5f),
            .y = Pixels.y + RoundToPixel((Pixels.height - Side) * 0.5f), .width = Side, .height = Side }, TextColor);
    }
    else if (UIElementIconView_IsShowing(&Item->Icon))
    {
        Vector2 Icon = UIElementIconView_GetPixelSize(&Item->Icon, Screen);
        Result = UIElementIconView_Draw(&Item->Icon, context, Left + RoundToPixel((Check - Icon.x) * 0.5f),
            Pixels.y + RoundToPixel((Pixels.height - Icon.y) * 0.5f), TextColor);
    }
    RenderContext_PushClip(UIRenderContext_GetRenderContext(context), Pixels);
    Vector2 Label = UIElementText_GetPixelSize(&Item->Label, Screen);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElementText_Draw(&Item->Label, context, Left + Check, Pixels.y + RoundToPixel((Pixels.height - Label.y) * 0.5f), TextColor);
    }
    float Right = (Pixels.x + Pixels.width) - PaddingX;
    if (HasSubmenu(Data))
    {
        float Arrow = Math_MaxFloat(3.0f, RoundToPixel(Pixels.height * 0.3f));
        UIElement_DrawArrow(context, (Rectangle){ .x = Right - Arrow, .y = Pixels.y, .width = Arrow, .height = Pixels.height }, UIArrowDirection_Right,
            TextColor);
        Right -= Arrow + RoundToPixel(ITEM_GAP * Scale);
    }
    Vector2 Shortcut = UIElementText_GetPixelSize(&Item->Shortcut, Screen);
    if ((Result.Code == ErrorCode_Success) && (Shortcut.x > 0.0f))
    {
        Result = UIElementText_Draw(&Item->Shortcut, context, Right - Shortcut.x, Pixels.y + RoundToPixel((Pixels.height - Shortcut.y) * 0.5f),
            ShortcutColor);
    }
    RenderContext_PopClip(UIRenderContext_GetRenderContext(context));
    return Result;
}

static Error Item_OnHoverStart(void* self, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(&((MenuItem*)self)->Base, UIElementCue_Hover);
}

static Error Item_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    MenuItem* Item = self;
    const UIMenuItem* Data = GetItemData(Item);
    bool IsEnabled = (Data != NULL) && IsActivatable(Data);
    Error Result = (Data != NULL) && !Data->IsSeparator ? UIElement_PostMouseCues(&Item->Base, args, IsEnabled) : Error_CreateSuccess();
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsActivated = false;
    UIPressable_OnMouseInput(&Item->Press, &Item->Base, args, IsEnabled, &IsActivated);
    if (!IsActivated)
    {
        return Error_CreateSuccess();
    }
    Result = UIElement_PostCue(&Item->Base, UIElementCue_Activate);
    MenuList* List = GetItemList(Item);
    if ((Result.Code == ErrorCode_Success) && (List != NULL))
    {
        List->HasPendingActivation = true;
        List->PendingItem = Item->Index;
        List->PendingButton = args->Button;
        List->PendingModifiers = args->Modifiers;
    }
    return Result;
}

static Error Item_OnPressLost(void* self, GameKey button)
{
    UIPressable_OnPressLost(&((MenuItem*)self)->Press, button);
    return Error_CreateSuccess();
}

/* Enter or Space activates; Right opens a submenu, its first item focused. Up and Down are navigation's, Left the list's. */
static Error Item_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    MenuItem* Item = self;
    UIScreen* Screen = Widget_GetScreen(&Item->Base);
    const UIMenuItem* Data = GetItemData(Item);
    bool IsEnabled = (Data != NULL) && IsActivatable(Data);
    if (IsEnabled && (args->Type == UIKeyboardInputType_KeyPress) && HasSubmenu(Data) && UIElement_IsActionKey(Screen, GameAction_UIRight, args->Key))
    {
        *outIsHandled = true;
        return OpenSubmenu(Screen, Item->ListId, Item->Index, true, true);
    }
    bool IsActivated = false;
    UIPressable_OnKeyboardInput(&Item->Press, &Item->Base, args, IsEnabled, outIsHandled, &IsActivated);
    if (!IsActivated)
    {
        return Error_CreateSuccess();
    }
    Error Result = UIElement_PostCue(&Item->Base, UIElementCue_Activate);
    return (Result.Code == ErrorCode_Success) ? ActivateItem(Screen, Item->ListId, Item->Index, args->Key, args->Modifiers, true) : Result;
}


// Fields.
static const WidgetVTable ListVTable =
{
    .OnDeconstruct = List_OnDeconstruct,
    .Update = List_Update,
    .Layout = List_Layout,
    .Render = List_Render,
    .OnKeyboardInput = List_OnKeyboardInput,
    .Measure = List_Measure
};

static const WidgetVTable ItemVTable =
{
    .OnDeconstruct = Item_OnDeconstruct,
    .Update = Item_Update,
    .Render = Item_Render,
    .OnHoverStart = Item_OnHoverStart,
    .OnMouseInput = Item_OnMouseInput,
    .OnPressLost = Item_OnPressLost,
    .OnKeyboardInput = Item_OnKeyboardInput,
    .Measure = Item_Measure
};


// Internal functions (declared in UIMenuInternal.h).
Error UIMenu_RegisterTypes(UIKit* kit, UIWidgetFactory* factory)
{
    WidgetTypeDescriptor List = { .Name = u8"MenuList", .StructSize = sizeof(MenuList), .VTable = &ListVTable, .Constructor = List_Construct };
    WidgetTypeDescriptor Item = { .Name = u8"MenuItem", .StructSize = sizeof(MenuItem), .VTable = &ItemVTable, .Constructor = Item_Construct,
        .IsFocusable = true };
    Error Result = UIWidgetFactory_RegisterType(factory, &List, &kit->_menuListType);
    return (Result.Code == ErrorCode_Success) ? UIWidgetFactory_RegisterType(factory, &Item, &kit->_menuItemType) : Result;
}

Error UIMenu_InternalOpenForBar(UIScreen* screen, uint64_t barId, uint64_t entryId, const UIMenuItem* items, size_t count, UIMenuHandler handler,
    const UserData* userData, bool isFromKey, uint64_t* outMenuId)
{
    *outMenuId = WIDGET_ID_INVALID;
    UIKit* Kit = UIKit_FromScreen(screen);
    if (Kit == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIMenu: the screen's factory has no kit.");
    }
    MenuListArgs Arguments = { .Kit = Kit, .Items = items, .Count = count, .Handler = handler, .Provider = NULL, .Data = userData, .OwnerId = barId,
        .RootMenuId = WIDGET_ID_INVALID, .ParentMenuId = WIDGET_ID_INVALID, .BarId = barId };
    uint64_t ListId = WIDGET_ID_INVALID;
    Error Result = CreateMenuList(screen, &Arguments, &ListId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    MenuList* List = ResolveList(screen, ListId);
    UIPopupOptions Options = UIPopupOptions_Create();
    Options.AnchorWidgetId = entryId;
    Options.OwnerId = barId;
    Options.IsOwnerInside = true;
    Options.FocusId = (isFromKey && (List != NULL)) ? GetFirstEnabledItem(List) : WIDGET_ID_INVALID;
    Result = OpenListAsPopup(screen, ListId, &Options);
    if (Result.Code == ErrorCode_Success)
    {
        *outMenuId = ListId;
    }
    return Result;
}


// Public functions.
Error UIMenu_Open(UIScreen* screen, const UIMenuItem* items, size_t count, const UIMenuOptions* options, uint64_t* outMenuId)
{
    if (outMenuId != NULL)
    {
        *outMenuId = WIDGET_ID_INVALID;
    }
    if ((screen == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenu_Open: screen and options must not be NULL.");
    }
    UIKit* Kit = UIKit_FromScreen(screen);
    if (Kit == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIMenu_Open: the screen's factory has no kit.");
    }
    MenuListArgs Arguments = { .Kit = Kit, .Items = items, .Count = count, .Handler = options->Handler, .Provider = options->SubmenuProvider,
        .Data = &options->Data, .OwnerId = options->OwnerId, .RootMenuId = WIDGET_ID_INVALID, .ParentMenuId = WIDGET_ID_INVALID,
        .BarId = WIDGET_ID_INVALID };
    uint64_t ListId = WIDGET_ID_INVALID;
    Error Result = CreateMenuList(screen, &Arguments, &ListId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    MenuList* List = ResolveList(screen, ListId);
    UIPopupOptions Popup = UIPopupOptions_Create();
    Popup.AnchorBox = options->AnchorBox;
    Popup.AnchorWidgetId = options->AnchorWidgetId;
    Popup.Side = options->Side;
    Popup.OwnerId = options->OwnerId;
    Popup.IsOwnerInside = options->IsOwnerInside;
    Popup.FocusId = (options->FocusesFirstItem && (List != NULL)) ? GetFirstEnabledItem(List) : WIDGET_ID_INVALID;
    Result = OpenListAsPopup(screen, ListId, &Popup);
    if ((Result.Code == ErrorCode_Success) && (outMenuId != NULL))
    {
        *outMenuId = ListId;
    }
    return Result;
}

Error UIMenu_OpenAt(UIScreen* screen, const UIContextMenuRequest* request, const UIMenuItem* items, size_t count, UIMenuHandler handler,
    const UserData* userData)
{
    if ((screen == NULL) || (request == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenu_OpenAt: screen and request must not be NULL.");
    }
    UIMenuOptions Options = UIMenuOptions_Create();
    Options.AnchorBox = (Rectangle){ .x = request->ScreenPosition.x, .y = request->ScreenPosition.y, .width = 0.0f, .height = 0.0f };
    Options.OwnerId = request->TargetWidgetId;
    Options.FocusesFirstItem = request->IsFromKey;
    Options.Handler = handler;
    Options.Data = (userData != NULL) ? *userData : UserData_CreateEmpty();
    return UIMenu_Open(screen, items, count, &Options, NULL);
}

Error UIMenu_Close(UIScreen* screen, uint64_t menuId)
{
    if (screen == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenu_Close: screen must not be NULL.");
    }
    return UIScreen_IsPopupOpen(screen, menuId) ? UIScreen_ClosePopup(screen, menuId) : Error_CreateSuccess();
}

Error UIMenuBuilder_Add(UIMenuBuilder* builder, const UIMenuItem* item)
{
    if ((builder == NULL) || (item == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIMenuBuilder_Add: builder and item must not be NULL.");
    }
    if (builder->Count >= UI_MENU_MAX_ITEMS)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIMenuBuilder_Add: more than UI_MENU_MAX_ITEMS items.");
    }
    Error Result = CheckItem(item);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // Copied now, texts and children: what the provider points to may be gone when it returns.
    Result = UIMenu_InternalCopyItems(item, 1U, &builder->Copies[builder->Count]);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    builder->Items[builder->Count] = builder->Copies[builder->Count].Items[0];
    builder->Count++;
    return Error_CreateSuccess();
}
