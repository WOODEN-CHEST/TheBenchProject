#include "TestSuites.h"
#include "TestUI.h"
#include "UIContextMenu.h"
#include "UIKit.h"
#include "UIMenu.h"
#include "UIMenuBar.h"
#include "UIPopup.h"
#include "UIStatusBar.h"
#include "UITooltip.h"
#include "wr/WRUserData.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The fixture's look: 20 pixel items in a 2 pixel padding, 7 pixel separators. */
#define MENU_THEME \
    "{\"name\": \"menus\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"min_size\": [200, 100], \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"menu\": {\"padding\": [2, 2], \"item_height\": 20, \"item_padding\": [8, 0], \"check_width\": 18, \"separator_height\": 7," \
    "\"separator_thickness\": 1, \"background\": {\"kind\": \"flat\"}, \"item\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}," \
    "\"shortcut\": {\"normal\": \"#a0a0a0\"}, \"separator\": {\"kind\": \"flat\"}}," \
    "\"menu_bar\": {\"padding\": [2, 0], \"entry_padding\": [8, 3], \"min_size\": [0, 22], \"background\": {\"kind\": \"flat\"}," \
    "\"entry\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"status_bar\": {\"padding\": [6, 2], \"spacing\": 8, \"min_size\": [0, 22], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}}}"
/** Where the tests open menus: a point at (300, 100); the first item's top is the padding below it. */
#define MENU_X 300.0f
#define MENU_Y 100.0f
#define ITEM 20.0f


// Types.
/* What a menu's handler was told. */
typedef struct ActivationRecordStruct
{
    UIMenuActivation Last;
    size_t Count;
    uint64_t DestroyId;
} ActivationRecord;

/* What a context provider was asked. */
typedef struct RequestRecordStruct
{
    UIContextMenuRequest Last;
    size_t Count;
    bool IsOpening;
} RequestRecord;


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, MENU_THEME);
}

static Error RecordActivation(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData)
{
    ActivationRecord* Record = UserData_GetPointer(userData);
    Record->Last = *activation;
    Record->Count++;
    Widget* Doomed = NULL;
    if ((Record->DestroyId != WIDGET_ID_INVALID) && UIScreen_TryGetWidgetById(screen, Record->DestroyId, &Doomed))
    {
        return Widget_Destroy(Doomed);
    }
    return Error_CreateSuccess();
}

static UIMenuOptions AtPoint(ActivationRecord* record)
{
    UIMenuOptions Options = UIMenuOptions_Create();
    Options.AnchorBox = (Rectangle){ .x = MENU_X / TEST_UI_SURFACE_WIDTH, .y = MENU_Y / TEST_UI_SURFACE_HEIGHT, .width = 0.0f, .height = 0.0f };
    Options.Handler = RecordActivation;
    Options.Data = UserData_FromPointer(record);
    return Options;
}

static float ItemY(size_t index)
{
    return MENU_Y + 2.0f + ((float)index * ITEM) + (ITEM * 0.5f);
}

static uint64_t GetItem(TestUIFixture* fixture, uint64_t menuId, size_t index)
{
    Widget* Menu = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, menuId, &Menu) || (index >= Widget_GetSubWidgetCount(Menu)))
    {
        return WIDGET_ID_INVALID;
    }
    return Widget_GetId(Widget_GetSubWidgetAt(Menu, index));
}

static uint64_t AddButtonAt(TestRun* run, TestUIFixture* fixture, uint64_t parentId, float x, float y)
{
    UIButtonOptions Options = UIButtonOptions_Create(u8"b");
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(x, y, 80.0f, 24.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&fixture->Kit, &fixture->Screen, parentId, &Options, &Id));
    return Id;
}

static const UIMenuItem* GetSimpleItems(void)
{
    static UIMenuItem Items[4];
    Items[0] = UIMenuItem_Create(1U, u8"Alpha");
    Items[1] = UIMenuItem_Separator();
    Items[2] = UIMenuItem_Create(2U, u8"Beta");
    Items[2].IsEnabled = false;
    Items[3] = UIMenuItem_Create(3U, u8"Gamma");
    Items[3].IsChecked = true;
    Items[3].Shortcut = u8"Ctrl+G";
    return Items;
}


// Tests: menus.
static void AClickOnAnItemActivatesItAndClosesTheMenu(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        UIMenuOptions Options = AtPoint(&Record);
        uint64_t MenuId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, GetSimpleItems(), 4U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, MenuId);
        TEST_CHECK(run, (Box.x == MENU_X) && (Box.y == MENU_Y) && (Box.height == ((3.0f * ITEM) + 7.0f + 4.0f)));
        // The separator and the disabled item do nothing, and the menu stays.
        TestUI_Click(run, &Fixture, MENU_X + 30.0f, MENU_Y + 2.0f + ITEM + 3.0f);
        TestUI_Click(run, &Fixture, MENU_X + 30.0f, MENU_Y + 2.0f + ITEM + 7.0f + 10.0f);
        TEST_CHECK(run, (Record.Count == 0U) && UIScreen_IsPopupOpen(&Fixture.Screen, MenuId));
        // A click on an item activates it: every level closes, then the handler hears its id.
        TestUI_Click(run, &Fixture, MENU_X + 30.0f, ItemY(0U));
        TEST_CHECK(run, (Record.Count == 1U) && (Record.Last.ItemId == 1U) && (Record.Last.Button == GameKey_MouseLeft));
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, MenuId));
        // A right click activates too.
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, GetSimpleItems(), 4U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        float GammaY = MENU_Y + 2.0f + (2.0f * ITEM) + 7.0f + (ITEM * 0.5f);
        TestUI_MovePointer(&Fixture, MENU_X + 30.0f, GammaY);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Record.Count == 2U) && (Record.Last.ItemId == 3U) && (Record.Last.Button == GameKey_MouseRight));
        // UIMenu_Close closes one that is open, and does nothing to one that is not.
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, GetSimpleItems(), 4U, &Options, &MenuId));
        TEST_CHECK_SUCCESS(run, UIMenu_Close(&Fixture.Screen, MenuId));
        TEST_CHECK_SUCCESS(run, UIMenu_Close(&Fixture.Screen, MenuId));
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void KeysMoveBetweenItemsAndEnterActivates(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        uint64_t OpenerId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 50.0f, 50.0f);
        TestUI_Tick(run, &Fixture);
        Widget* Opener = NULL;
        UIScreen_TryGetWidgetById(&Fixture.Screen, OpenerId, &Opener);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Opener));
        UIMenuOptions Options = AtPoint(&Record);
        Options.FocusesFirstItem = true;
        uint64_t MenuId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, GetSimpleItems(), 4U, &Options, &MenuId));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == GetItem(&Fixture, MenuId, 0U));
        TestUI_Ticks(run, &Fixture, 3U);
        // Down passes the separator and the disabled item.
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == GetItem(&Fixture, MenuId, 3U));
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Record.Count == 1U) && (Record.Last.ItemId == 3U) && (Record.Last.Button == GameKey_Enter));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == OpenerId);
        // Escape closes it without activating, and the focus comes back.
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, GetSimpleItems(), 4U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, MenuId) && (Record.Count == 1U));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == OpenerId);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static Error ProvideSubmenu(UIScreen* screen, uint32_t itemId, UIMenuBuilder* builder, const UserData* userData)
{
    (void)screen;
    (void)userData;
    UIMenuItem Made = UIMenuItem_Create(itemId * 10U, u8"Made");
    return UIMenuBuilder_Add(builder, &Made);
}

static void SubmenusOpenByRestKeyAndClick(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        UIMenuItem Children[2] = { UIMenuItem_Create(11U, u8"New"), UIMenuItem_Create(12U, u8"Open") };
        UIMenuItem Items[3] = { UIMenuItem_Create(10U, u8"File"), UIMenuItem_Create(20U, u8"Edit"), UIMenuItem_Create(30U, u8"More") };
        Items[0].Children = Children;
        Items[0].ChildCount = 2U;
        Items[2].HasProvidedSubmenu = true;
        UIMenuOptions Options = AtPoint(&Record);
        Options.SubmenuProvider = ProvideSubmenu;
        uint64_t MenuId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, Items, 3U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        // Resting on File opens its submenu, beside it, leaving the focus where it is; resting on Edit closes it.
        TestUI_MovePointer(&Fixture, MENU_X + 30.0f, ItemY(0U));
        TestUI_Ticks(run, &Fixture, (size_t)(UI_MENU_SUBMENU_DELAY_SECONDS / TEST_UI_TICK_SECONDS) / 2U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U); // Not before the rest is over.
        TestUI_Ticks(run, &Fixture, (size_t)(UI_MENU_SUBMENU_DELAY_SECONDS / TEST_UI_TICK_SECONDS) + 3U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 2U);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        uint64_t SubmenuId = UIScreen_GetPopupAt(&Fixture.Screen, 1U);
        TestUI_Ticks(run, &Fixture, 2U);
        Rectangle Menu = TestUI_GetPixelBox(run, &Fixture, MenuId);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, SubmenuId).x == (Menu.x + Menu.width - 2.0f));
        TestUI_MovePointer(&Fixture, MENU_X + 30.0f, ItemY(1U));
        TestUI_Ticks(run, &Fixture, (size_t)(UI_MENU_SUBMENU_DELAY_SECONDS / TEST_UI_TICK_SECONDS) + 3U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U);
        // Right on File (focused) opens it with its first item focused; Left closes it and the focus goes back.
        Widget* File = NULL;
        UIScreen_TryGetWidgetById(&Fixture.Screen, GetItem(&Fixture, MenuId, 0U), &File);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, File));
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 2U);
        SubmenuId = UIScreen_GetPopupAt(&Fixture.Screen, 1U);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == GetItem(&Fixture, SubmenuId, 0U));
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 1U) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Widget_GetId(File)));
        // A click on File opens it; activating Open in it closes everything and the handler hears Open.
        TestUI_Click(run, &Fixture, MENU_X + 30.0f, ItemY(0U));
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 2U);
        TestUI_Ticks(run, &Fixture, 3U);
        SubmenuId = UIScreen_GetPopupAt(&Fixture.Screen, 1U);
        Rectangle Open = TestUI_GetPixelBox(run, &Fixture, GetItem(&Fixture, SubmenuId, 1U));
        TestUI_Click(run, &Fixture, Open.x + 10.0f, Open.y + 10.0f);
        TEST_CHECK(run, (Record.Count == 1U) && (Record.Last.ItemId == 12U) && (UIScreen_GetPopupCount(&Fixture.Screen) == 0U));
        // A provided submenu is built as it opens.
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, Items, 3U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Click(run, &Fixture, MENU_X + 30.0f, ItemY(2U));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 2U);
        SubmenuId = UIScreen_GetPopupAt(&Fixture.Screen, 1U);
        Rectangle Made = TestUI_GetPixelBox(run, &Fixture, GetItem(&Fixture, SubmenuId, 0U));
        TestUI_Click(run, &Fixture, Made.x + 10.0f, Made.y + 10.0f);
        TEST_CHECK(run, (Record.Count == 2U) && (Record.Last.ItemId == 300U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static Error ProvideMenu(UIScreen* screen, const UIContextMenuRequest* request, const UserData* userData)
{
    RequestRecord* Record = UserData_GetPointer(userData);
    Record->Last = *request;
    Record->Count++;
    if (!Record->IsOpening)
    {
        return Error_CreateSuccess();
    }
    UIMenuItem Items[1] = { UIMenuItem_Create(7U, u8"Reset") };
    return UIMenu_OpenAt(screen, request, Items, 1U, NULL, NULL);
}

static void AContextRequestWalksUpToItsProvider(TestRun* run)
{
    TestUIFixture Fixture;
    RequestRecord Record = { .Count = 0U, .IsOpening = true };
    if (Construct(run, &Fixture))
    {
        UIPanelOptions Panel = UIPanelOptions_Create();
        Panel.Element.HasPlacement = true;
        Panel.Element.Placement = UIPlacement_FromTopLeft(100.0f, 100.0f, 300.0f, 200.0f);
        uint64_t PanelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &PanelId));
        uint64_t InsideId = AddButtonAt(run, &Fixture, PanelId, 0.0f, 0.0f);
        uint64_t OutsideId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 600.0f, 300.0f);
        UserData Data = UserData_FromPointer(&Record);
        TEST_CHECK_SUCCESS(run, UIContextMenu_SetProvider(&Fixture.Screen, PanelId, ProvideMenu, &Data));
        TEST_CHECK(run, UIContextMenu_HasProvider(&Fixture.Screen, PanelId) && !UIContextMenu_HasProvider(&Fixture.Screen, InsideId));
        TestUI_Ticks(run, &Fixture, 2U);
        // A right click on the button in the panel: the panel's provider answers, for the button, at the pointer.
        TestUI_MovePointer(&Fixture, 120.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Record.Count == 1U) && (Record.Last.ProviderWidgetId == PanelId) && (Record.Last.TargetWidgetId == InsideId)
            && !Record.Last.IsFromKey && (fabsf(Record.Last.ScreenPosition.x - 0.12f) < 1e-4f));
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U);
        // The menu's focus came in from the button, which had it from the press.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 0U) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == InsideId));
        // The context menu key on the focused button asks too, from the key, at its middle.
        TestUI_Tap(run, &Fixture, GameKey_Menu);
        TEST_CHECK(run, (Record.Count == 2U) && Record.Last.IsFromKey && (Record.Last.TargetWidgetId == InsideId));
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U);
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(&Fixture.Screen));
        // With no provider up the tree, nothing answers.
        TestUI_MovePointer(&Fixture, 620.0f, 310.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Record.Count == 2U) && (UIScreen_GetPopupCount(&Fixture.Screen) == 0U));
        bool IsAnswered = true;
        TEST_CHECK_SUCCESS(run, UIContextMenu_Request(&Fixture.Screen, OutsideId, (Vector2){ .x = 0.5f, .y = 0.5f }, false, &IsAnswered));
        TEST_CHECK(run, !IsAnswered);
        TEST_CHECK_ERROR(run, UIContextMenu_Request(&Fixture.Screen, OutsideId, (Vector2){ .x = NAN, .y = 0.5f }, false, NULL),
            ErrorCode_ArgumentOutOfRange);
        // A right press dragged off before its release asks nothing; a cleared provider answers nothing.
        TestUI_MovePointer(&Fixture, 120.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 620.0f, 310.0f); // Onto the other button.
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Record.Count == 2U);
        TEST_CHECK_SUCCESS(run, UIContextMenu_Clear(&Fixture.Screen, PanelId));
        TEST_CHECK_SUCCESS(run, UIContextMenu_Request(&Fixture.Screen, InsideId, (Vector2){ .x = 0.5f, .y = 0.5f }, false, &IsAnswered));
        TEST_CHECK(run, !IsAnswered && (Record.Count == 2U));
        TEST_CHECK_ERROR(run, UIContextMenu_SetProvider(&Fixture.Screen, 99999U, ProvideMenu, &Data), ErrorCode_InvalidOperation);

        // An answered request says so; the menu key on a focused widget that leaves it is the menu's, consumed from the game.
        TEST_CHECK_SUCCESS(run, UIContextMenu_SetProvider(&Fixture.Screen, PanelId, ProvideMenu, &Data));
        TEST_CHECK_SUCCESS(run, UIContextMenu_Request(&Fixture.Screen, InsideId, (Vector2){ .x = 0.5f, .y = 0.5f }, false, &IsAnswered));
        TEST_CHECK(run, IsAnswered && (Record.Count == 3U));
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(&Fixture.Screen));
        UIPanelOptions Inner = UIPanelOptions_Create();
        uint64_t InnerId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, PanelId, &Inner, &InnerId));
        TestUI_Tick(run, &Fixture);
        Widget* InnerWidget = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, InnerId, &InnerWidget)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(InnerWidget, true));
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, InnerWidget));
        }
        TestUI_Press(&Fixture, GameKey_Menu);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Record.Count == 4U) && Record.Last.IsFromKey && (Record.Last.TargetWidgetId == InnerId));
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Menu));
        TestUI_Release(&Fixture, GameKey_Menu);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AHandlerMayDestroyTheMenusOwner(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        uint64_t OwnerId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 50.0f, 50.0f);
        TestUI_Tick(run, &Fixture);
        Record.DestroyId = OwnerId;
        UIMenuOptions Options = AtPoint(&Record);
        Options.OwnerId = OwnerId;
        uint64_t MenuId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, GetSimpleItems(), 4U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Click(run, &Fixture, MENU_X + 30.0f, ItemY(0U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Record.Count == 1U) && (Record.Last.OwnerId == OwnerId) && !UIScreen_IsWidgetAlive(&Fixture.Screen, OwnerId));
        TEST_CHECK(run, UIScreen_GetWidgetCount(&Fixture.Screen) == 0U);
        // Its owner gone while it is open, a menu closes.
        OwnerId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 50.0f, 50.0f);
        TestUI_Tick(run, &Fixture);
        Options.OwnerId = OwnerId;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, GetSimpleItems(), 4U, &Options, &MenuId));
        Widget* Owner = NULL;
        UIScreen_TryGetWidgetById(&Fixture.Screen, OwnerId, &Owner);
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Owner));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void MenusAreChecked(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        UIMenuOptions Options = AtPoint(&Record);
        UIMenuItem NoLabel = UIMenuItem_Create(1U, NULL);
        TEST_CHECK_ERROR(run, UIMenu_Open(&Fixture.Screen, &NoLabel, 1U, &Options, NULL), ErrorCode_ArgumentOutOfRange);
        const unsigned char Broken[] = { 0xC3U, 0x28U, 0U };
        UIMenuItem BadText = UIMenuItem_Create(1U, Broken);
        TEST_CHECK_ERROR(run, UIMenu_Open(&Fixture.Screen, &BadText, 1U, &Options, NULL), ErrorCode_InvalidTextEncoding);
        TEST_CHECK_ERROR(run, UIMenu_Open(&Fixture.Screen, NULL, 2U, &Options, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIMenu_Open(&Fixture.Screen, GetSimpleItems(), 4U, NULL, NULL), ErrorCode_IllegalArgument);
        // Deeper than the limit: a chain of submenus one level too deep.
        UIMenuItem Chain[UI_MENU_MAX_DEPTH + 1U];
        for (size_t Index = 0U; Index <= UI_MENU_MAX_DEPTH; Index++)
        {
            Chain[Index] = UIMenuItem_Create((uint32_t)Index, u8"level");
            if (Index < UI_MENU_MAX_DEPTH)
            {
                Chain[Index].Children = &Chain[Index + 1U];
                Chain[Index].ChildCount = 1U;
            }
        }
        TEST_CHECK_ERROR(run, UIMenu_Open(&Fixture.Screen, Chain, 1U, &Options, NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, &Chain[1], 1U, &Options, NULL));
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 1U);
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(&Fixture.Screen));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetWidgetCount(&Fixture.Screen) == 0U); // Refused menus left nothing behind.
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the menu bar.
static void AMenuBarOpensSwitchesAndActivates(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        UIMenuBarOptions Options = UIMenuBarOptions_Create();
        Options.Handler = RecordActivation;
        Options.Data = UserData_FromPointer(&Record);
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 1000.0f, 22.0f);
        uint64_t BarId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateMenuBar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &BarId));
        UIMenuItem FileItems[2] = { UIMenuItem_Create(1U, u8"New"), UIMenuItem_Create(2U, u8"Quit") };
        UIMenuItem EditItems[1] = { UIMenuItem_Create(3U, u8"Undo") };
        UIMenuItem ViewItems[1] = { UIMenuItem_Create(4U, u8"Zoom") };
        uint64_t FileId = WIDGET_ID_INVALID;
        uint64_t EditId = WIDGET_ID_INVALID;
        uint64_t ViewId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenuBar_AddEntry(&Fixture.Screen, BarId, u8"File", FileItems, 2U, &FileId));
        TEST_CHECK_SUCCESS(run, UIMenuBar_AddEntry(&Fixture.Screen, BarId, u8"Edit", EditItems, 1U, &EditId));
        TEST_CHECK_SUCCESS(run, UIMenuBar_AddEntry(&Fixture.Screen, BarId, u8"View", ViewItems, 1U, &ViewId));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle File = TestUI_GetPixelBox(run, &Fixture, FileId);
        Rectangle Edit = TestUI_GetPixelBox(run, &Fixture, EditId);
        TEST_CHECK(run, (File.x == 2.0f) && (Edit.x == (File.x + File.width)) && (File.height == 22.0f));

        // A press on File opens its menu under it; resting on Edit switches; a press on Edit closes it.
        TestUI_Click(run, &Fixture, File.x + 5.0f, 10.0f);
        uint64_t MenuId = WIDGET_ID_INVALID;
        uint64_t EntryId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, (MenuId != WIDGET_ID_INVALID) && (EntryId == FileId));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, MenuId).y == 22.0f);
        TestUI_MovePointer(&Fixture, Edit.x + 5.0f, 10.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, (MenuId != WIDGET_ID_INVALID) && (EntryId == EditId) && (UIScreen_GetPopupCount(&Fixture.Screen) == 1U));
        TestUI_Click(run, &Fixture, Edit.x + 5.0f, 10.0f);
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, (MenuId == WIDGET_ID_INVALID) && (UIScreen_GetPopupCount(&Fixture.Screen) == 0U));

        // Press File, drag to Quit, release: Quit is activated.
        TestUI_MovePointer(&Fixture, File.x + 5.0f, 10.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture, 4U);
        TestUI_MovePointer(&Fixture, File.x + 20.0f, 22.0f + 2.0f + ITEM + 10.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Record.Count == 1U) && (Record.Last.ItemId == 2U) && (Record.Last.OwnerId == BarId));
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);

        // By the keyboard: Enter on File opens it with New focused; Right moves to Edit's menu, Left back to File's.
        Widget* FileEntry = NULL;
        UIScreen_TryGetWidgetById(&Fixture.Screen, FileId, &FileEntry);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, FileEntry));
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, (EntryId == FileId) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == GetItem(&Fixture, MenuId, 0U)));
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, (EntryId == EditId) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == GetItem(&Fixture, MenuId, 0U)));
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, EntryId == FileId);
        // Left from the first entry goes round to the last.
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, EntryId == ViewId);
        TEST_CHECK_SUCCESS(run, UIMenuBar_Close(&Fixture.Screen, BarId));
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
        TEST_CHECK_ERROR(run, UIMenuBar_AddEntry(&Fixture.Screen, BarId, NULL, FileItems, 2U, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIMenuBar_AddEntry(&Fixture.Screen, FileId, u8"x", FileItems, 2U, NULL), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UIMenuBar_SetHandler(&Fixture.Screen, BarId, NULL, NULL));
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* A click on an item activates it and reaches nothing behind the closing menu. */
static void AClickOnAnItemReachesNothingBehindIt(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIButtonOptions Behind = UIButtonOptions_Create(u8"behind");
        Behind.ClickMode = UIClickMode_OnRelease;
        Behind.Element.HasPlacement = true;
        Behind.Element.Placement = UIPlacement_FromTopLeft(MENU_X, MENU_Y, 200.0f, 40.0f);
        uint64_t BehindId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Behind, &BehindId));
        TestUI_Tick(run, &Fixture);
        UIMenuItem Items[2] = { UIMenuItem_Create(1U, u8"Alpha"), UIMenuItem_Create(2U, u8"Beta") };
        UIMenuOptions Options = AtPoint(&Record);
        uint64_t MenuId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, Items, 2U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Click(run, &Fixture, MENU_X + 30.0f, ItemY(0U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Record.Count == 1U) && (Record.Last.ItemId == 1U) && (UIScreen_GetPopupCount(&Fixture.Screen) == 0U));
        size_t Activated = 0U;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            Activated += ((Events.Events[Index].WidgetId == BehindId) && (Events.Events[Index].WidgetEventId == (int32_t)UIElementEvent_Activated)) ? 1U : 0U;
        }
        TEST_CHECK(run, Activated == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A provider's labels are copied as they are added: one written into a buffer it changes afterwards still shows. */
static unsigned char ProviderLabel[64];

static Error ProvideFromABuffer(UIScreen* screen, uint32_t itemId, UIMenuBuilder* builder, const UserData* userData)
{
    (void)screen;
    (void)itemId;
    (void)userData;
    for (uint32_t Index = 0U; Index < 3U; Index++)
    {
        snprintf((char*)ProviderLabel, sizeof(ProviderLabel), "Recent %u", (unsigned)Index);
        UIMenuItem Item = UIMenuItem_Create(100U + Index, ProviderLabel);
        Error Result = UIMenuBuilder_Add(builder, &Item);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    memset(ProviderLabel, 'W', 40U);
    ProviderLabel[40] = 0U;
    return Error_CreateSuccess();
}

/* A provided submenu that provides a deeper one at every level stops at UI_MENU_MAX_DEPTH, with no error. */
static Error ProvideDeeper(UIScreen* screen, uint32_t itemId, UIMenuBuilder* builder, const UserData* userData)
{
    (void)screen;
    (void)userData;
    UIMenuItem Item = UIMenuItem_Create(itemId + 1U, u8"Deeper");
    Item.HasProvidedSubmenu = true;
    return UIMenuBuilder_Add(builder, &Item);
}

static void ProvidedSubmenusAreCopiedAndBounded(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        UIMenuItem Items[1] = { UIMenuItem_Create(1U, u8"Open recent") };
        Items[0].HasProvidedSubmenu = true;
        UIMenuOptions Options = AtPoint(&Record);
        Options.SubmenuProvider = ProvideFromABuffer;
        Options.FocusesFirstItem = true;
        uint64_t MenuId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, Items, 1U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 2U);
        // Three items as narrow as "Recent 0", not the forty letters the buffer held when the provider returned.
        Rectangle Submenu = TestUI_GetPixelBox(run, &Fixture, UIScreen_GetPopupAt(&Fixture.Screen, 1U));
        TEST_CHECK(run, (Submenu.width > 0.0f) && (Submenu.width < 200.0f) && TestUI_IsSamePixel(Submenu.height, (3.0f * ITEM) + 4.0f));
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Record.Count == 1U) && (Record.Last.ItemId == 100U));

        // Deeper and deeper: Right opens a level each time, up to UI_MENU_MAX_DEPTH levels, and never fails the update.
        Items[0] = UIMenuItem_Create(1U, u8"Folders");
        Items[0].HasProvidedSubmenu = true;
        Options.SubmenuProvider = ProvideDeeper;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, Items, 1U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        for (size_t Level = 0U; Level < (2U * UI_MENU_MAX_DEPTH); Level++)
        {
            TestUI_Tap(run, &Fixture, GameKey_Right);
        }
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == UI_MENU_MAX_DEPTH);
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(&Fixture.Screen));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Left in a submenu the pointer's rest opened closes it and focuses its item, also when the focus came into it by the arrows. */
static void LeftInARestedSubmenuFocusesItsItem(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        UIMenuItem Children[2] = { UIMenuItem_Create(11U, u8"New"), UIMenuItem_Create(12U, u8"Open") };
        UIMenuItem Items[2] = { UIMenuItem_Create(10U, u8"File"), UIMenuItem_Create(20U, u8"Edit") };
        Items[0].Children = Children;
        Items[0].ChildCount = 2U;
        UIMenuOptions Options = AtPoint(&Record);
        uint64_t MenuId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenu_Open(&Fixture.Screen, Items, 2U, &Options, &MenuId));
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_MovePointer(&Fixture, MENU_X + 30.0f, ItemY(0U));
        TestUI_Ticks(run, &Fixture, (size_t)(UI_MENU_SUBMENU_DELAY_SECONDS / TEST_UI_TICK_SECONDS) + 3U);
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 2U) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
        TestUI_RemovePointer(&Fixture);
        TestUI_Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        uint64_t SubmenuId = UIScreen_GetPopupAt(&Fixture.Screen, 1U);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == GetItem(&Fixture, SubmenuId, 0U));
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 1U) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == GetItem(&Fixture, MenuId, 0U)));
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(&Fixture.Screen));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The menu bar's keys move between its menus while the pointer rests on an entry: resting there does not take the menu back. */
static void TheMenuBarsKeysWorkWithThePointerOnAnEntry(TestRun* run)
{
    TestUIFixture Fixture;
    ActivationRecord Record = { .Count = 0U, .DestroyId = WIDGET_ID_INVALID };
    if (Construct(run, &Fixture))
    {
        UIMenuBarOptions Options = UIMenuBarOptions_Create();
        Options.Handler = RecordActivation;
        Options.Data = UserData_FromPointer(&Record);
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 1000.0f, 22.0f);
        uint64_t BarId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateMenuBar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &BarId));
        UIMenuItem FileItems[2] = { UIMenuItem_Create(1U, u8"New"), UIMenuItem_Create(2U, u8"Quit") };
        UIMenuItem EditItems[1] = { UIMenuItem_Create(3U, u8"Undo") };
        uint64_t FileId = WIDGET_ID_INVALID;
        uint64_t EditId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenuBar_AddEntry(&Fixture.Screen, BarId, u8"File", FileItems, 2U, &FileId));
        TEST_CHECK_SUCCESS(run, UIMenuBar_AddEntry(&Fixture.Screen, BarId, u8"Edit", EditItems, 1U, &EditId));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle File = TestUI_GetPixelBox(run, &Fixture, FileId);
        TestUI_Click(run, &Fixture, File.x + 5.0f, 10.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TestUI_Ticks(run, &Fixture, 3U);
        uint64_t MenuId = WIDGET_ID_INVALID;
        uint64_t EntryId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, EntryId == EditId);
        // Coming onto File again switches back.
        TestUI_MovePointer(&Fixture, File.x + 6.0f, 10.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_MovePointer(&Fixture, TestUI_GetPixelBox(run, &Fixture, EditId).x + 5.0f, 10.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_MovePointer(&Fixture, File.x + 5.0f, 10.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIMenuBar_GetOpen(&Fixture.Screen, BarId, &MenuId, &EntryId));
        TEST_CHECK(run, EntryId == FileId);
        TEST_CHECK_SUCCESS(run, UIMenuBar_Close(&Fixture.Screen, BarId));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the status bar.
static void AStatusBarShowsMessagesAndHoverText(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStatusBarOptions Options = UIStatusBarOptions_Create();
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(0.0f, 478.0f, 1000.0f, 22.0f);
        uint64_t BarId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStatusBar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &BarId));
        uint64_t Sections[UIStatusBarSection_Count];
        for (int32_t Section = 0; Section < (int32_t)UIStatusBarSection_Count; Section++)
        {
            TEST_CHECK_SUCCESS(run, UIStatusBar_GetSection(&Fixture.Screen, BarId, (UIStatusBarSection)Section, &Sections[Section]));
            TEST_CHECK(run, UIScreen_IsWidgetAlive(&Fixture.Screen, Sections[Section]));
        }
        TEST_CHECK_ERROR(run, UIStatusBar_GetSection(&Fixture.Screen, BarId, UIStatusBarSection_Count, &Sections[0]), ErrorCode_ArgumentOutOfRange);
        // Elements in the right section sit at its right end.
        UIButtonOptions Button = UIButtonOptions_Create(u8"ok");
        Button.Element.Hint = UIStackHint_Fixed(60.0f);
        uint64_t ButtonId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Sections[UIStatusBarSection_Right], &Button, &ButtonId));
        uint64_t HoverId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, HoverId, u8"what it does"));
        TestUI_Ticks(run, &Fixture, 4U);
        Rectangle Right = TestUI_GetPixelBox(run, &Fixture, ButtonId);
        TEST_CHECK(run, fabsf((Right.x + Right.width) - (1000.0f - 6.0f)) < 0.01f);

        Widget* Bar = NULL;
        UIScreen_TryGetWidgetById(&Fixture.Screen, BarId, &Bar);
        const UIStatusBarCapability* Capability = &((UIStatusBarLookBase*)(void*)Bar)->StatusBar;
        TEST_CHECK(run, strcmp((const char*)UITextContent_Get(UIStatusBarCapability_GetShownText(Capability)), "") == 0);
        // The hover text where the pointer rests shows at once, before its delay.
        TestUI_MovePointer(&Fixture, 120.0f, 110.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, strcmp((const char*)UITextContent_Get(UIStatusBarCapability_GetShownText(Capability)), "what it does") == 0);
        // A message shows over it for its time, then the hover text again.
        TEST_CHECK_SUCCESS(run, UIStatusBar_SetMessage(&Fixture.Screen, BarId, u8"Saved", 0.5));
        TEST_CHECK(run, strcmp((const char*)UITextContent_Get(UIStatusBarCapability_GetShownText(Capability)), "Saved") == 0);
        TestUI_Ticks(run, &Fixture, (size_t)(0.4 / TEST_UI_TICK_SECONDS));
        TEST_CHECK(run, strcmp((const char*)UITextContent_Get(UIStatusBarCapability_GetShownText(Capability)), "Saved") == 0);
        TestUI_Ticks(run, &Fixture, (size_t)(0.2 / TEST_UI_TICK_SECONDS));
        TEST_CHECK(run, strcmp((const char*)UITextContent_Get(UIStatusBarCapability_GetShownText(Capability)), "what it does") == 0);
        // A message without a time stays until it is cleared; with the hover text off, nothing then.
        TEST_CHECK_SUCCESS(run, UIStatusBar_SetMessage(&Fixture.Screen, BarId, u8"Stays", 0.0));
        TestUI_Ticks(run, &Fixture, (size_t)(2.0 / TEST_UI_TICK_SECONDS));
        TEST_CHECK(run, strcmp((const char*)UITextContent_Get(UIStatusBarCapability_GetShownText(Capability)), "Stays") == 0);
        TEST_CHECK_SUCCESS(run, UIStatusBar_SetShowsHoverText(&Fixture.Screen, BarId, false));
        TEST_CHECK_SUCCESS(run, UIStatusBar_ClearMessage(&Fixture.Screen, BarId));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, strcmp((const char*)UITextContent_Get(UIStatusBarCapability_GetShownText(Capability)), "") == 0);
        TEST_CHECK_ERROR(run, UIStatusBar_SetMessage(&Fixture.Screen, BarId, u8"x", -1.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIStatusBar_SetMessage(&Fixture.Screen, BarId, u8"x", INFINITY), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIStatusBar_SetMessage(&Fixture.Screen, BarId, NULL, 1.0), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIStatusBar_SetMessage(&Fixture.Screen, ButtonId, u8"x", 1.0), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UIMenuTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_click_on_an_item_activates_it_and_closes_the_menu", AClickOnAnItemActivatesItAndClosesTheMenu },
        { "keys_move_between_items_and_enter_activates", KeysMoveBetweenItemsAndEnterActivates },
        { "submenus_open_by_rest_key_and_click", SubmenusOpenByRestKeyAndClick },
        { "a_context_request_walks_up_to_its_provider", AContextRequestWalksUpToItsProvider },
        { "a_handler_may_destroy_the_menus_owner", AHandlerMayDestroyTheMenusOwner },
        { "menus_are_checked", MenusAreChecked },
        { "a_menu_bar_opens_switches_and_activates", AMenuBarOpensSwitchesAndActivates },
        { "a_status_bar_shows_messages_and_hover_text", AStatusBarShowsMessagesAndHoverText },
        { "a_click_on_an_item_reaches_nothing_behind_it", AClickOnAnItemReachesNothingBehindIt },
        { "provided_submenus_are_copied_and_bounded", ProvidedSubmenusAreCopiedAndBounded },
        { "left_in_a_rested_submenu_focuses_its_item", LeftInARestedSubmenuFocusesItsItem },
        { "the_menu_bars_keys_work_with_the_pointer_on_an_entry", TheMenuBarsKeysWorkWithThePointerOnAnEntry },
    };
    TestRun_RunSuite(run, "UIMenu", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
