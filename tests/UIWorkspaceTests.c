#include "TestSuites.h"
#include "TestUI.h"
#include "Registry.h"
#include "UIKit.h"
#include "UILayouts.h"
#include "UIPopup.h"
#include "UIChoice.h"
#include "UIStatusBar.h"
#include "wr/WRFileSystem.h"
#include "UITextBox.h"
#include "UITooltip.h"
#include "UIWidgetFactory.h"
#include "UIWindow.h"
#include "UIWindowTypes.h"
#include "UIWorkspace.h"
#include "wr/WRPath.h"
#include "wr/WRUserData.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The fixture's look: windows, bars, menus and a workspace with small round numbers. */
#define WORKSPACE_THEME \
    "{\"name\": \"workspace\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"label\": {\"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"none\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"text_box\": {\"padding\": [4, 4], \"min_size\": [100, 24], \"caret_width\": 1, \"mask\": \"*\", \"box\": {\"kind\": \"flat\"}," \
    "\"read_only_box\": {\"kind\": \"flat\"}, \"selection\": {\"kind\": \"flat\"}, \"inactive_selection\": {\"kind\": \"flat\"}, " \
    "\"caret\": {\"kind\": \"flat\"}, \"overwrite_caret\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}, \"placeholder\": {\"normal\": \"#808080\"}}," \
    "\"menu\": {\"padding\": [2, 2], \"item_height\": 22, \"item_padding\": [8, 0], \"check_width\": 18, \"separator_height\": 7," \
    "\"separator_thickness\": 1, \"background\": {\"kind\": \"flat\"}, \"item\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}," \
    "\"shortcut\": {\"normal\": \"#a0a0a0\"}, \"separator\": {\"kind\": \"flat\"}}," \
    "\"menu_bar\": {\"padding\": [0, 0], \"entry_padding\": [8, 2], \"min_size\": [0, 24], \"background\": {\"kind\": \"flat\"}," \
    "\"entry\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"status_bar\": {\"padding\": [4, 1], \"spacing\": 4, \"min_size\": [0, 22], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"window\": {\"title_height\": 20, \"title_padding\": 4, \"icon_spacing\": 4, \"button_size\": [16, 14], \"button_spacing\": 2, \"border\": 2," \
    "\"resize_margin\": 4, \"min_size\": [100, 60], \"minimized_width\": 150, \"content_padding\": [4, 4], \"content_spacing\": 2," \
    "\"frame\": {\"kind\": \"flat\"}, \"title_bar\": {\"kind\": \"flat\"}, \"title_text\": {\"normal\": \"#ffffff\"}, " \
    "\"content_background\": {\"kind\": \"flat\"}, \"resize_edge\": {\"kind\": \"flat\"}}," \
    "\"workspace\": {\"tab_height\": 20, \"tab_padding\": 4, \"tab_spacing\": 2, \"icon_spacing\": 4, \"tab_max_width\": 150," \
    "\"splitter_thickness\": 4, \"guide_size\": 30, \"background\": {\"kind\": \"flat\"}, \"strip\": {\"kind\": \"flat\"}," \
    "\"tab\": {\"kind\": \"flat\"}, \"tab_text\": {\"normal\": \"#e0e0e0\"}, \"splitter\": {\"kind\": \"flat\"}, \"zone\": {\"kind\": \"flat\"}," \
    "\"drop_preview\": {\"kind\": \"flat\"}}}}"
/** The theme's numbers the tests reason with, in UI pixels (the screen's scale is 1). */
#define MENU_BAR_HEIGHT 24.0f
#define STATUS_BAR_HEIGHT 22.0f
#define STRIP 20.0f
#define SPLITTER 4.0f
#define GUIDE 30.0f
#define TITLE_HEIGHT 20.0f
/** The dock's area on the fixture's 1000 x 500 surface. */
#define DOCK_TOP MENU_BAR_HEIGHT
#define DOCK_HEIGHT (500.0f - MENU_BAR_HEIGHT - STATUS_BAR_HEIGHT)
/** The test's window types, by slot. */
#define TYPE_A 0U
#define TYPE_B 1U
#define TYPE_C 2U
#define TYPE_NOTES 3U
#define TYPE_COUNT 4U
/** How many windows' text boxes the fixture remembers. */
#define MAX_BOXES ((size_t)32)


// Types.
/* A widget that counts its updates. */
typedef struct CountingWidgetStruct
{
    Widget Base;
    size_t Updates;
} CountingWidget;

/* How many times the entered element changed. */
typedef struct EnteringCountStruct
{
    size_t Changes;
} EnteringCount;

/* The fixture: a UI fixture, the window types and a workspace with both bars. */
typedef struct WorkspaceFixtureStruct
{
    TestUIFixture UI;
    RegistryRoot Root;
    UIWindowTypes Types;
    bool HasRoot;
    bool HasTypes;
    uint32_t TypeIndices[TYPE_COUNT];
    uint64_t WorkspaceId;
    /* Each window its fill made, with the text box it put in it. */
    uint64_t Windows[MAX_BOXES];
    uint64_t Boxes[MAX_BOXES];
    size_t BoxCount;
    uint64_t CountingType;
} WorkspaceFixture;


// Static functions: the fixture.
static Error FillWithTextBox(UIScreen* screen, uint64_t windowId, uint64_t contentId, const UserData* userData)
{
    WorkspaceFixture* Fixture = UserData_GetPointer(userData);
    UITextBoxOptions Options = UITextBoxOptions_Create();
    Options.Element.Hint = UIStackHint_Fixed(24.0f);
    uint64_t BoxId = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateTextBox(UIKit_FromScreen(screen), screen, contentId, &Options, &BoxId);
    if ((Result.Code == ErrorCode_Success) && (Fixture->BoxCount < MAX_BOXES))
    {
        Fixture->Windows[Fixture->BoxCount] = windowId;
        Fixture->Boxes[Fixture->BoxCount] = BoxId;
        Fixture->BoxCount++;
    }
    return (Result.Code == ErrorCode_Success) ? UIWindow_SetDefaultFocus(screen, windowId, BoxId, false) : Result;
}

static Error Counting_Update(void* self, ProgramTime time)
{
    (void)time;
    ((CountingWidget*)self)->Updates++;
    return Error_CreateSuccess();
}

static const WidgetVTable CountingVTable = { .Update = Counting_Update };

static bool Construct(TestRun* run, WorkspaceFixture* fixture, bool hasBars)
{
    Memory_Zero(fixture, sizeof(*fixture));
    if (!TestUI_Construct(run, &fixture->UI) || !TestUI_SetTheme(run, &fixture->UI, WORKSPACE_THEME))
    {
        return false;
    }
    fixture->HasRoot = TEST_CHECK_SUCCESS(run, RegistryRoot_Construct(&fixture->Root, &fixture->UI.Keys));
    fixture->HasTypes = fixture->HasRoot && TEST_CHECK_SUCCESS(run, UIWindowTypes_Construct(&fixture->Types, &fixture->UI.Keys, &fixture->Root));
    if (!fixture->HasTypes)
    {
        return false;
    }
    static const char* const Keys[TYPE_COUNT] = { "tbp:test_a", "tbp:test_b", "tbp:test_c", "tbp:test_notes" };
    static const unsigned char* const Names[TYPE_COUNT] = { u8"Alpha", u8"Beta", u8"Gamma", u8"Notes" };
    for (size_t Type = 0U; Type < TYPE_COUNT; Type++)
    {
        UIWindowTypeDefinition Definition = UIWindowTypeDefinition_Create(Names[Type], FillWithTextBox);
        Definition.Data = UserData_FromPointer(fixture);
        Definition.DefaultSize = (Vector2){ .x = 240.0f, .y = 160.0f };
        Definition.IsSingle = Type != TYPE_NOTES;
        if (Type != TYPE_C)
        {
            Definition.Icon = UIElementIcon_FromLetter((uint32_t)Names[Type][0]); // Gamma has none: its tab shows its name's first letter.
        }
        if (!TEST_CHECK_SUCCESS(run, UIWindowTypes_Register(&fixture->Types, (const unsigned char*)Keys[Type], &Definition, NAMESPACED_KEY_ID_NONE,
            &fixture->TypeIndices[Type])))
        {
            return false;
        }
    }
    WidgetTypeDescriptor Counting = { .Name = u8"CountingProbe", .StructSize = sizeof(CountingWidget), .VTable = &CountingVTable,
        .IsFocusable = false };
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&fixture->UI.Widgets, &Counting, &fixture->CountingType)))
    {
        return false;
    }
    UIWorkspaceOptions Options = UIWorkspaceOptions_Create();
    Options.HasMenuBar = hasBars;
    Options.HasStatusBar = hasBars;
    Options.Types = &fixture->Types;
    if (!TEST_CHECK_SUCCESS(run, UIKit_CreateWorkspace(&fixture->UI.Kit, &fixture->UI.Screen, &Options, &fixture->WorkspaceId)))
    {
        return false;
    }
    TestUI_Ticks(run, &fixture->UI, 2U);
    return true;
}

static void Deconstruct(TestRun* run, WorkspaceFixture* fixture)
{
    if (fixture->HasTypes)
    {
        UIWindowTypes_Deconstruct(&fixture->Types);
    }
    if (fixture->HasRoot)
    {
        RegistryRoot_Deconstruct(&fixture->Root);
    }
    TestUI_Deconstruct(run, &fixture->UI);
}


// Static functions: helpers.
static uint64_t Open(TestRun* run, WorkspaceFixture* fixture, size_t type)
{
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIWorkspace_OpenType(&fixture->UI.Screen, fixture->WorkspaceId, fixture->TypeIndices[type], &Id));
    TestUI_Ticks(run, &fixture->UI, 2U);
    return Id;
}

static void Dock(TestRun* run, WorkspaceFixture* fixture, uint64_t windowId, uint64_t besideId, UIDockZone zone)
{
    TEST_CHECK_SUCCESS(run, UIWorkspace_Dock(&fixture->UI.Screen, fixture->WorkspaceId, windowId, besideId, zone));
    TestUI_Ticks(run, &fixture->UI, 2U);
}

static uint64_t GetBox(const WorkspaceFixture* fixture, uint64_t windowId)
{
    for (size_t Index = 0U; Index < fixture->BoxCount; Index++)
    {
        if (fixture->Windows[Index] == windowId)
        {
            return fixture->Boxes[Index];
        }
    }
    return WIDGET_ID_INVALID;
}

static Rectangle PixelBox(TestRun* run, WorkspaceFixture* fixture, uint64_t widgetId)
{
    return TestUI_GetPixelBox(run, &fixture->UI, widgetId);
}

static Rectangle TabBox(TestRun* run, WorkspaceFixture* fixture, uint64_t windowId)
{
    return PixelBox(run, fixture, UIWorkspace_GetTabId(&fixture->UI.Screen, fixture->WorkspaceId, windowId));
}

static Vector2 Middle(Rectangle box)
{
    return (Vector2){ .x = box.x + (box.width * 0.5f), .y = box.y + (box.height * 0.5f) };
}

static UIWorkspacePlace PlaceOf(WorkspaceFixture* fixture, uint64_t windowId)
{
    UIWorkspacePlace Place = UIWorkspacePlace_Floating;
    return UIWorkspace_GetPlace(&fixture->UI.Screen, fixture->WorkspaceId, windowId, &Place) ? Place : (UIWorkspacePlace)-1;
}

static const UIDockTree* Tree(WorkspaceFixture* fixture)
{
    return UIWorkspace_GetTree(&fixture->UI.Screen, fixture->WorkspaceId);
}

static int32_t LeafOf(WorkspaceFixture* fixture, uint64_t windowId)
{
    int32_t Leaf = UI_DOCK_NODE_NONE;
    return UIDockTree_FindTab(Tree(fixture), windowId, &Leaf, NULL) ? Leaf : UI_DOCK_NODE_NONE;
}

static bool IsShown(WorkspaceFixture* fixture, uint64_t widgetId)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(&fixture->UI.Screen, widgetId, &Found) && Widget_IsRendered(Found);
}

static bool IsRoot(WorkspaceFixture* fixture, uint64_t widgetId)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(&fixture->UI.Screen, widgetId, &Found) && Widget_IsScreenRoot(Found);
}

static UIWindowDock DockOf(WorkspaceFixture* fixture, uint64_t windowId)
{
    UIWindowDock Dock = UIWindowDock_None;
    Error Result = UIWindow_GetDock(&fixture->UI.Screen, windowId, &Dock);
    Error_Deconstruct(&Result);
    return Dock;
}

static Rectangle FloatingBox(TestRun* run, WorkspaceFixture* fixture, uint64_t windowId)
{
    Rectangle Box = { 0 };
    TEST_CHECK_SUCCESS(run, UIWindow_GetBox(&fixture->UI.Screen, windowId, &Box));
    return Box;
}

static void PressAt(TestRun* run, WorkspaceFixture* fixture, float x, float y)
{
    TestUI_MovePointer(&fixture->UI, x, y);
    TestUI_Tick(run, &fixture->UI);
    TestUI_Press(&fixture->UI, GameKey_MouseLeft);
    TestUI_Tick(run, &fixture->UI);
}

static void MoveTo(TestRun* run, WorkspaceFixture* fixture, float x, float y)
{
    TestUI_MovePointer(&fixture->UI, x, y);
    TestUI_Tick(run, &fixture->UI);
}

static void ReleaseAt(TestRun* run, WorkspaceFixture* fixture, float x, float y)
{
    TestUI_MovePointer(&fixture->UI, x, y);
    TestUI_Tick(run, &fixture->UI);
    TestUI_Release(&fixture->UI, GameKey_MouseLeft);
    TestUI_Tick(run, &fixture->UI);
    TestUI_Tick(run, &fixture->UI);
}

static void TypeText(TestRun* run, WorkspaceFixture* fixture, const char* text)
{
    for (const char* Cursor = text; *Cursor != 0; Cursor++)
    {
        fixture->UI.Sample.TextCodepoints[0] = (uint32_t)(unsigned char)*Cursor;
        fixture->UI.Sample.TextCodepointCount = 1U;
        TestUI_Tick(run, &fixture->UI);
    }
}

static bool HasText(WorkspaceFixture* fixture, uint64_t textBoxId, const char* expected)
{
    const unsigned char* Text = NULL;
    Error Result = UITextBox_GetText(&fixture->UI.Screen, textBoxId, &Text, NULL);
    bool IsSame = (Result.Code == ErrorCode_Success) && (Text != NULL) && (strcmp((const char*)Text, expected) == 0);
    Error_Deconstruct(&Result);
    return IsSame;
}

/* A and B as tabs of one area (B selected), C at their right: the arrangement most cases start from. */
static void BuildThree(TestRun* run, WorkspaceFixture* fixture, uint64_t* outA, uint64_t* outB, uint64_t* outC)
{
    *outA = Open(run, fixture, TYPE_A);
    *outB = Open(run, fixture, TYPE_B);
    *outC = Open(run, fixture, TYPE_C);
    Dock(run, fixture, *outA, WIDGET_ID_INVALID, UIDockZone_Center);
    Dock(run, fixture, *outB, *outA, UIDockZone_Center);
    Dock(run, fixture, *outC, *outA, UIDockZone_Right);
}

/* Waits out the double-click time, so the next press counts as a first. */
static void WaitOutClicks(TestRun* run, WorkspaceFixture* fixture)
{
    TestUI_Ticks(run, &fixture->UI, (size_t)(UI_SCREEN_MULTI_CLICK_SECONDS / TEST_UI_TICK_SECONDS) + 2U);
}


// Static functions: test cases.
static void ItCoversTheSurfaceWithItsBars(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        UIScreen* Screen = &Fixture.UI.Screen;
        Widget* Root = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(Screen, Fixture.WorkspaceId, &Root) && Widget_IsScreenRoot(Root));
        TEST_CHECK(run, (Root != NULL) && (UIScreen_GetRootLayer(Screen, Root) == UIScreenLayer_Background));
        uint64_t MenuBar = WIDGET_ID_INVALID;
        uint64_t StatusBar = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWorkspace_GetMenuBarId(Screen, Fixture.WorkspaceId, &MenuBar));
        TEST_CHECK_SUCCESS(run, UIWorkspace_GetStatusBarId(Screen, Fixture.WorkspaceId, &StatusBar));
        TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Fixture, MenuBar), 0.0f, 0.0f, 1000.0f, MENU_BAR_HEIGHT));
        TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Fixture, StatusBar), 0.0f, 500.0f - STATUS_BAR_HEIGHT, 1000.0f, STATUS_BAR_HEIGHT));
        UIInsets Insets = UIScreen_GetWorkspaceInsets(Screen);
        TEST_CHECK(run, (Insets.Top == MENU_BAR_HEIGHT) && (Insets.Bottom == STATUS_BAR_HEIGHT) && (Insets.Left == 0.0f) && (Insets.Right == 0.0f));
        // A window maximized fills what the bars leave.
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(Screen, Notes));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Fixture, Notes), 0.0f, DOCK_TOP, 1000.0f, DOCK_HEIGHT));
        TEST_CHECK(run, !UIWorkspace_IsWorkspace(Screen, MenuBar) && UIWorkspace_IsWorkspace(Screen, Fixture.WorkspaceId));
    }
    Deconstruct(run, &Fixture);
}

static void WindowsDockAsTabsAndSplits(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = Open(run, &Fixture, TYPE_A);
        TEST_CHECK(run, (PlaceOf(&Fixture, A) == UIWorkspacePlace_Floating) && IsRoot(&Fixture, A));
        Dock(run, &Fixture, A, WIDGET_ID_INVALID, UIDockZone_Center);
        TEST_CHECK(run, PlaceOf(&Fixture, A) == UIWorkspacePlace_Docked);
        TEST_CHECK(run, !IsRoot(&Fixture, A) && (DockOf(&Fixture, A) == UIWindowDock_Docked));
        TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Fixture, A), 0.0f, DOCK_TOP + STRIP, 1000.0f, DOCK_HEIGHT - STRIP));
        // Its tab: the strip's first, as tall as the strip; its title bar hides.
        Rectangle Tab = TabBox(run, &Fixture, A);
        TEST_CHECK(run, TestUI_IsSamePixel(Tab.x, 2.0f) && TestUI_IsSamePixel(Tab.y, DOCK_TOP) && TestUI_IsSamePixel(Tab.height, STRIP));
        uint64_t B = Open(run, &Fixture, TYPE_B);
        Dock(run, &Fixture, B, A, UIDockZone_Center);
        TEST_CHECK(run, LeafOf(&Fixture, A) == LeafOf(&Fixture, B));
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), LeafOf(&Fixture, A)) == B);
        TEST_CHECK(run, IsShown(&Fixture, B) && !IsShown(&Fixture, A));
        // The selected tab shows its name, the other its icon alone.
        TEST_CHECK(run, TabBox(run, &Fixture, B).width > TabBox(run, &Fixture, A).width);
        uint64_t C = Open(run, &Fixture, TYPE_C);
        Dock(run, &Fixture, C, A, UIDockZone_Right);
        TEST_CHECK(run, UIDockTree_GetLeafCount(Tree(&Fixture)) == 2U);
        TEST_CHECK(run, UIDockTree_IsValid(Tree(&Fixture)));
        Rectangle Left = PixelBox(run, &Fixture, B);
        Rectangle Right = PixelBox(run, &Fixture, C);
        TEST_CHECK(run, TestUI_IsBox(Left, 0.0f, DOCK_TOP + STRIP, 498.0f, DOCK_HEIGHT - STRIP));
        TEST_CHECK(run, TestUI_IsBox(Right, 502.0f, DOCK_TOP + STRIP, 498.0f, DOCK_HEIGHT - STRIP));
        TEST_CHECK(run, UIWorkspace_GetSplitterId(&Fixture.UI.Screen, Fixture.WorkspaceId, UIDockTree_GetRoot(Tree(&Fixture))) != WIDGET_ID_INVALID);
        // A tab beside the first of an area goes after it.
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        Dock(run, &Fixture, Notes, A, UIDockZone_Center);
        TEST_CHECK(run, UIDockTree_GetTabAt(Tree(&Fixture), LeafOf(&Fixture, A), 1U) == Notes);
        // Refusals.
        TEST_CHECK_ERROR(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, A, A, UIDockZone_Left), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, A, WIDGET_ID_INVALID, UIDockZone_Left), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, Fixture.WorkspaceId, A, UIDockZone_Left), ErrorCode_InvalidOperation);
    }
    Deconstruct(run, &Fixture);
}

static void APressOnATabSelectsItAndFocusesItsWindow(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        Vector2 At = Middle(TabBox(run, &Fixture, A));
        PressAt(run, &Fixture, At.x, At.y);
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), LeafOf(&Fixture, A)) == A);
        TEST_CHECK(run, IsShown(&Fixture, A) && !IsShown(&Fixture, B));
        // The focus goes into the window as a title bar's press gives it: to its default target, nothing clicked.
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == GetBox(&Fixture, A));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID);
        ReleaseAt(run, &Fixture, At.x, At.y);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.UI.Screen));
        // A press inside it enters its box; then a press on its own tab keeps the box entered (the same visit).
        Rectangle Box = PixelBox(run, &Fixture, GetBox(&Fixture, A));
        WaitOutClicks(run, &Fixture);
        TestUI_Click(run, &Fixture.UI, Box.x + 10.0f, Box.y + 10.0f);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == GetBox(&Fixture, A));
        WaitOutClicks(run, &Fixture);
        TestUI_Click(run, &Fixture.UI, At.x, At.y);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == GetBox(&Fixture, A));
        // Selecting another area's tab moves the focus there; coming back enters A's box again.
        WaitOutClicks(run, &Fixture);
        Vector2 CAt = Middle(TabBox(run, &Fixture, C));
        TestUI_Click(run, &Fixture.UI, CAt.x, CAt.y);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == GetBox(&Fixture, C));
        WaitOutClicks(run, &Fixture);
        TestUI_Click(run, &Fixture.UI, At.x, At.y);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == GetBox(&Fixture, A));
    }
    Deconstruct(run, &Fixture);
}

static void WindowsInTabsNotSelectedGetNoUpdates(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        uint64_t Content = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindow_GetContentId(&Fixture.UI.Screen, A, &Content));
        Widget* Made = NULL;
        Widget* Parent = NULL;
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.UI.Widgets, &Fixture.UI.Screen, Fixture.CountingType, NULL, &Made));
        uint64_t ProbeId = Widget_GetId(Made);
        TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Made));
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Content, &Parent));
        TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Parent, Made));
        TestUI_Ticks(run, &Fixture.UI, 5U);
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, ProbeId, &Made) && (((CountingWidget*)(void*)Made)->Updates == 0U));
        TEST_CHECK_SUCCESS(run, UIWorkspace_SelectTab(&Fixture.UI.Screen, Fixture.WorkspaceId, A));
        TestUI_Ticks(run, &Fixture.UI, 5U);
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, ProbeId, &Made) && (((CountingWidget*)(void*)Made)->Updates == 5U));
        TEST_CHECK(run, IsShown(&Fixture, A) && !IsShown(&Fixture, B));
    }
    Deconstruct(run, &Fixture);
}

static void ClosingTabsSelectsTheNextAndCollapsesEmptyAreas(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        uint64_t BTab = UIWorkspace_GetTabId(&Fixture.UI.Screen, Fixture.WorkspaceId, B);
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, B, NULL));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.UI.Screen, BTab));
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), LeafOf(&Fixture, A)) == A);
        TEST_CHECK(run, IsShown(&Fixture, A));
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, A, NULL));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (UIDockTree_GetLeafCount(Tree(&Fixture)) == 1U) && (UIDockTree_GetRoot(Tree(&Fixture)) == LeafOf(&Fixture, C)));
        TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Fixture, C), 0.0f, DOCK_TOP + STRIP, 1000.0f, DOCK_HEIGHT - STRIP));
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, C, NULL));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, UIDockTree_IsEmpty(Tree(&Fixture)));
    }
    Deconstruct(run, &Fixture);
}

static void ATabDraggedOutCarriesItsWindowOnToADrop(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        uint64_t BTab = UIWorkspace_GetTabId(&Fixture.UI.Screen, Fixture.WorkspaceId, B);
        Vector2 At = Middle(TabBox(run, &Fixture, B));
        PressAt(run, &Fixture, At.x, At.y);
        // Along the strip nothing leaves; a few pixels below it, the window floats under the pointer.
        MoveTo(run, &Fixture, At.x, At.y + 5.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked);
        MoveTo(run, &Fixture, At.x, At.y + 60.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Floating);
        TEST_CHECK(run, IsRoot(&Fixture, B) && (DockOf(&Fixture, B) == UIWindowDock_None) && IsShown(&Fixture, B));
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.UI.Screen, BTab));
        TEST_CHECK(run, UIScreen_HasHeldPress(&Fixture.UI.Screen));
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), LeafOf(&Fixture, A)) == A); // Its old area shows A.
        // The same press carries it: its title bar's middle stays under the pointer as it moves.
        Rectangle Before = FloatingBox(run, &Fixture, B);
        MoveTo(run, &Fixture, At.x + 30.0f, At.y + 80.0f);
        Rectangle After = FloatingBox(run, &Fixture, B);
        TEST_CHECK(run, TestUI_IsSamePixel(After.x - Before.x, 30.0f) && TestUI_IsSamePixel(After.y - Before.y, 20.0f));
        // Over C's area its guides show; on the centre's, a release adds a tab there.
        Rectangle CBox = PixelBox(run, &Fixture, C);
        Vector2 Centre = Middle(CBox);
        MoveTo(run, &Fixture, Centre.x, Centre.y);
        UIWorkspaceDropView View;
        TEST_CHECK(run, UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View) && View.HasTarget && (View.Zone == UIDockZone_Center));
        ReleaseAt(run, &Fixture, Centre.x, Centre.y);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.UI.Screen));
        TEST_CHECK(run, (PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked) && (LeafOf(&Fixture, B) == LeafOf(&Fixture, C)));
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), LeafOf(&Fixture, C)) == B);
        TEST_CHECK(run, !UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View));
        TEST_CHECK(run, UIDockTree_IsValid(Tree(&Fixture)));
    }
    Deconstruct(run, &Fixture);
}

/* A tab torn out by one long move straight onto a guide of the area it leaves room for: the guide shows lit and a release there docks, with
 * no further move (the dock changed after that move was heard). */
static void ATabTornOutOntoAGuideInOneMoveShowsItLit(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        // C's area goes with its tab: A's and B's then fills the dock, its centre guide in the dock's middle under the strip, where the
        // splitter stood before.
        Vector2 At = Middle(TabBox(run, &Fixture, C));
        PressAt(run, &Fixture, At.x, At.y);
        Vector2 Centre = { .x = 500.0f, .y = DOCK_TOP + STRIP + roundf((DOCK_HEIGHT - STRIP - GUIDE) * 0.5f) + (GUIDE * 0.5f) };
        MoveTo(run, &Fixture, Centre.x, Centre.y);
        TEST_CHECK(run, PlaceOf(&Fixture, C) == UIWorkspacePlace_Floating);
        UIWorkspaceDropView View;
        TEST_CHECK(run, UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View) && View.HasTarget && (View.Zone == UIDockZone_Center));
        TEST_CHECK(run, TestUI_IsBox(View.LeafBox, 0.0f, DOCK_TOP, 1000.0f, DOCK_HEIGHT));
        TestUI_Ticks(run, &Fixture.UI, 3U);
        TEST_CHECK(run, UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View) && View.HasTarget && (View.Zone == UIDockZone_Center));
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (PlaceOf(&Fixture, C) == UIWorkspacePlace_Docked) && (LeafOf(&Fixture, C) == LeafOf(&Fixture, A)));
        TEST_CHECK(run, (UIDockTree_GetLeafCount(Tree(&Fixture)) == 1U) && UIDockTree_IsValid(Tree(&Fixture)));
    }
    Deconstruct(run, &Fixture);
}

static Error OnEnteredChanged(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    (void)event;
    ((EnteringCount*)UserData_GetPointer(userData))->Changes++;
    return Error_CreateSuccess();
}

/* A tab leaves its strip only past UI_WORKSPACE_UNDOCK_DISTANCE from it, every way, its window floating with its title bar's middle under
 * the pointer. */
static void ATabTearsOutOnlyPastTheUndockDistance(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        Rectangle BTab = TabBox(run, &Fixture, B);
        float X = BTab.x + 6.0f;
        float Y = BTab.y + 5.0f;
        float Bottom = DOCK_TOP + STRIP;
        // Down: at the distance it stays; a pixel past it, it floats, its title bar's middle under the pointer.
        PressAt(run, &Fixture, X, Y);
        MoveTo(run, &Fixture, X, Bottom + UI_WORKSPACE_UNDOCK_DISTANCE - 0.5f); // Within it, away from the edge's rounding.
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked);
        MoveTo(run, &Fixture, X, Bottom + UI_WORKSPACE_UNDOCK_DISTANCE + 1.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Floating);
        Rectangle Torn = FloatingBox(run, &Fixture, B);
        TEST_CHECK(run, TestUI_IsSamePixel(Torn.y, Bottom + UI_WORKSPACE_UNDOCK_DISTANCE + 1.0f - (TITLE_HEIGHT * 0.5f)));
        TestUI_Tap(run, &Fixture.UI, GameKey_Escape);
        ReleaseAt(run, &Fixture, X, Bottom + 20.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked);
        // Up, over the menu bar.
        WaitOutClicks(run, &Fixture);
        PressAt(run, &Fixture, X, Y);
        MoveTo(run, &Fixture, X, DOCK_TOP - UI_WORKSPACE_UNDOCK_DISTANCE + 0.5f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked);
        MoveTo(run, &Fixture, X, DOCK_TOP - UI_WORKSPACE_UNDOCK_DISTANCE - 1.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Floating);
        TestUI_Tap(run, &Fixture.UI, GameKey_Escape);
        ReleaseAt(run, &Fixture, X, 2.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked);
        // Sideways, out of C's area into the one beside it.
        WaitOutClicks(run, &Fixture);
        Rectangle CTab = TabBox(run, &Fixture, C);
        float Left = PixelBox(run, &Fixture, C).x;
        PressAt(run, &Fixture, CTab.x + 6.0f, CTab.y + 5.0f);
        MoveTo(run, &Fixture, Left - UI_WORKSPACE_UNDOCK_DISTANCE + 0.5f, CTab.y + 5.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, C) == UIWorkspacePlace_Docked);
        MoveTo(run, &Fixture, Left - UI_WORKSPACE_UNDOCK_DISTANCE - 1.0f, CTab.y + 5.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, C) == UIWorkspacePlace_Floating);
        TestUI_Tap(run, &Fixture.UI, GameKey_Escape);
        ReleaseAt(run, &Fixture, Left - 20.0f, CTab.y + 5.0f);
        TEST_CHECK(run, (PlaceOf(&Fixture, C) == UIWorkspacePlace_Docked) && UIDockTree_IsValid(Tree(&Fixture)));
    }
    Deconstruct(run, &Fixture);
}

/* A double click's second press on a tab, dragged, tears nothing out: let go, it maximizes the window out of the dock. */
static void ADoubleClickOnATabDraggedTearsNothingOut(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        WaitOutClicks(run, &Fixture);
        Vector2 At = Middle(TabBox(run, &Fixture, B));
        TestUI_Click(run, &Fixture.UI, At.x, At.y);
        PressAt(run, &Fixture, At.x, At.y);
        MoveTo(run, &Fixture, At.x, At.y + 80.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked);
        ReleaseAt(run, &Fixture, At.x, At.y + 80.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Maximized);
    }
    Deconstruct(run, &Fixture);
}

/* A press on the tab of a window being typed in leaves it being typed in: nothing is left or entered again. */
static void APressOnATabLeavesTheTypingInItsWindowGoing(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        uint64_t Box = GetBox(&Fixture, B);
        Rectangle BoxPixels = PixelBox(run, &Fixture, Box);
        TestUI_Click(run, &Fixture.UI, BoxPixels.x + 10.0f, BoxPixels.y + 10.0f);
        TypeText(run, &Fixture, "ab");
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Box);
        EnteringCount Count = { 0 };
        UserData Data = UserData_FromPointer(&Count);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.UI.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_EnteredChanged), OnEnteredChanged,
            &Data, &Subscription));
        WaitOutClicks(run, &Fixture);
        Vector2 At = Middle(TabBox(run, &Fixture, B));
        TestUI_Click(run, &Fixture.UI, At.x, At.y);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (Count.Changes == 0U) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Box));
        TypeText(run, &Fixture, "c");
        TEST_CHECK(run, HasText(&Fixture, Box, "abc"));
    }
    Deconstruct(run, &Fixture);
}

/* A strip too narrow for its tabs takes the room from the selected tab's name, so every tab stays on it to press. */
static void ANarrowStripTakesTheRoomFromTheSelectedName(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        uint64_t Notes[4] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            Notes[Index] = Open(run, &Fixture, TYPE_NOTES);
            Dock(run, &Fixture, Notes[Index], C, UIDockZone_Center);
        }
        uint64_t Second = UIDockTree_GetTabAt(Tree(&Fixture), LeafOf(&Fixture, C), 1U); // Selected, and not the last tab.
        TEST_CHECK_SUCCESS(run, UIWorkspace_SelectTab(&Fixture.UI.Screen, Fixture.WorkspaceId, Second));
        TEST_CHECK_SUCCESS(run, UIWorkspace_SetAreaShare(&Fixture.UI.Screen, Fixture.WorkspaceId, C, 0.05f));
        TestUI_Ticks(run, &Fixture.UI, 3U);
        Rectangle Area = PixelBox(run, &Fixture, Second);
        TEST_CHECK(run, Area.width < 140.0f);
        uint64_t Windows[5] = { C, Notes[0], Notes[1], Notes[2], Notes[3] };
        float Used = 0.0f;
        for (size_t Index = 0U; Index < 5U; Index++)
        {
            Rectangle Tab = TabBox(run, &Fixture, Windows[Index]);
            TEST_CHECK(run, (Tab.width >= 10.0f) && (Tab.x >= Area.x) && ((Tab.x + Tab.width) <= (Area.x + Area.width)));
            Used = Math_MaxFloat(Used, (Tab.x + Tab.width) - Area.x);
        }
        float Crowded = TabBox(run, &Fixture, Second).width;
        // With room again, the selected tab shows its whole name: more than the crowded strip had for it.
        TEST_CHECK_SUCCESS(run, UIWorkspace_SetAreaShare(&Fixture.UI.Screen, Fixture.WorkspaceId, C, 0.5f));
        TestUI_Ticks(run, &Fixture.UI, 3U);
        float Roomy = TabBox(run, &Fixture, Second).width;
        TEST_CHECK(run, Roomy > TabBox(run, &Fixture, C).width + 30.0f);
        TEST_CHECK(run, Crowded < Roomy); // The name gave the room...
        TEST_CHECK(run, (Used + (Roomy - Crowded)) > Area.width); // ...because the whole names did not fit.
    }
    Deconstruct(run, &Fixture);
}

/* A dock with as many areas as it holds refuses a new one, and stays as it was: a window that asked stays in its tab or floats on. */
static void AFullDockRefusesANewAreaAndStaysAsItWas(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t Windows[UI_DOCK_TREE_MAX_LEAVES];
        Windows[0] = Open(run, &Fixture, TYPE_NOTES);
        Dock(run, &Fixture, Windows[0], WIDGET_ID_INVALID, UIDockZone_Center);
        size_t Count = 1U;
        while ((UIDockTree_GetLeafCount(Tree(&Fixture)) < UI_DOCK_TREE_MAX_LEAVES) && (Count < UI_DOCK_TREE_MAX_LEAVES))
        {
            // Beside the shallowest area's window, so the depth stays low.
            const UIDockTree* Current = Tree(&Fixture);
            int32_t Shallowest = UI_DOCK_NODE_NONE;
            for (int32_t Node = 0; Node < (int32_t)UI_DOCK_TREE_MAX_NODES; Node++)
            {
                if (UIDockTree_IsLeaf(Current, Node) && ((Shallowest == UI_DOCK_NODE_NONE)
                    || (UIDockTree_GetDepth(Current, Node) < UIDockTree_GetDepth(Current, Shallowest))))
                {
                    Shallowest = Node;
                }
            }
            uint64_t Beside = UIDockTree_GetSelected(Current, Shallowest);
            TEST_CHECK_SUCCESS(run, UIWorkspace_OpenType(&Fixture.UI.Screen, Fixture.WorkspaceId, Fixture.TypeIndices[TYPE_NOTES], &Windows[Count]));
            TEST_CHECK_SUCCESS(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, Windows[Count], Beside, UIDockZone_Right));
            Count++;
        }
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, UIDockTree_GetLeafCount(Tree(&Fixture)) == UI_DOCK_TREE_MAX_LEAVES);
        int32_t Before = LeafOf(&Fixture, Windows[5]);
        TEST_CHECK_ERROR(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, Windows[5], Windows[9], UIDockZone_Bottom),
            ErrorCode_InvalidOperation);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (PlaceOf(&Fixture, Windows[5]) == UIWorkspacePlace_Docked) && (LeafOf(&Fixture, Windows[5]) == Before));
        TEST_CHECK(run, (UIDockTree_GetLeafCount(Tree(&Fixture)) == UI_DOCK_TREE_MAX_LEAVES) && UIDockTree_IsValid(Tree(&Fixture)));
        uint64_t Extra = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWorkspace_OpenType(&Fixture.UI.Screen, Fixture.WorkspaceId, Fixture.TypeIndices[TYPE_NOTES], &Extra));
        TEST_CHECK_ERROR(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, Extra, Windows[9], UIDockZone_Left), ErrorCode_InvalidOperation);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, PlaceOf(&Fixture, Extra) == UIWorkspacePlace_Floating);
        Dock(run, &Fixture, Extra, Windows[9], UIDockZone_Center); // A tab needs no new area.
        TEST_CHECK(run, (PlaceOf(&Fixture, Extra) == UIWorkspacePlace_Docked) && (LeafOf(&Fixture, Extra) == LeafOf(&Fixture, Windows[9])));
    }
    Deconstruct(run, &Fixture);
}

/* A splitter shows the pointer's resize shape of its way: across between areas side by side, down between areas one above the other. */
static void ASplittersPointerShowsItsWay(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        Dock(run, &Fixture, B, A, UIDockZone_Bottom);
        const UIDockTree* Current = Tree(&Fixture);
        int32_t Across = UIDockTree_GetRoot(Current);
        const UIDockNode* ALeaf = UIDockTree_GetNode(Current, LeafOf(&Fixture, A));
        int32_t Down = (ALeaf != NULL) ? ALeaf->Parent : UI_DOCK_NODE_NONE;
        Vector2 AcrossAt = Middle(PixelBox(run, &Fixture, UIWorkspace_GetSplitterId(&Fixture.UI.Screen, Fixture.WorkspaceId, Across)));
        Vector2 DownAt = Middle(PixelBox(run, &Fixture, UIWorkspace_GetSplitterId(&Fixture.UI.Screen, Fixture.WorkspaceId, Down)));
        MoveTo(run, &Fixture, AcrossAt.x, AcrossAt.y);
        TEST_CHECK(run, UIScreen_GetCursorShape(&Fixture.UI.Screen) == UICursorShape_ResizeHorizontal);
        MoveTo(run, &Fixture, DownAt.x, DownAt.y);
        TEST_CHECK(run, UIScreen_GetCursorShape(&Fixture.UI.Screen) == UICursorShape_ResizeVertical);
    }
    Deconstruct(run, &Fixture);
}

// The layouts' helpers, further down with the layouts' cases.
static bool MakeLayoutsPath(TestRun* run, const char* folder, GenericBuffer* outPath);
static bool StartLayouts(TestRun* run, WorkspaceFixture* fixture, UILayouts* layouts, const unsigned char* path);
static void StopLayouts(TestRun* run, WorkspaceFixture* fixture, UILayouts* layouts);
static uint64_t WindowOfType(WorkspaceFixture* fixture, size_t type);
static size_t CountKept(WorkspaceFixture* fixture);

static bool RefuseToClose(UIScreen* screen, uint64_t windowId, const UserData* userData)
{
    (void)screen;
    (void)windowId;
    (void)userData;
    return false;
}

static Error FailToBuild(UIScreen* screen, uint64_t workspaceId, const UserData* userData)
{
    (void)screen;
    (void)workspaceId;
    (void)userData;
    return Error_Construct2(ErrorCode_InvalidOperation, "test: the default could not be built");
}

/* A window maximized then minimized keeps that it was, while another shows maximized: saving, switching, a new layout all go on, nothing is
 * tried again every tick, and both states come back after a restart. */
static void AMinimizedWindowKeepsThatItWasMaximized(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    bool HasPath = MakeLayoutsPath(run, "workspace_two_maximized", &Path);
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    if (HasPath && Construct(run, &Fixture, true) && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        UIScreen* Screen = &Fixture.UI.Screen;
        TEST_CHECK_SUCCESS(run, UIWorkspace_NewLayout(Screen, Fixture.WorkspaceId, u8"Second", NULL));
        uint64_t Notes = WindowOfType(&Fixture, TYPE_NOTES);
        uint64_t Other = Open(run, &Fixture, TYPE_NOTES);
        // The later one minimized from maximized, the earlier one (first in the layout) showing maximized: loading must not let the later
        // one's maximize undo the earlier one's.
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(Screen, Other));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(Screen, Other));
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(Screen, Notes));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK_SUCCESS(run, UIWorkspace_SaveLayouts(Screen, Fixture.WorkspaceId));
        TEST_CHECK_SUCCESS(run, UIWorkspace_SwitchLayout(Screen, Fixture.WorkspaceId, 0U));
        TEST_CHECK_SUCCESS(run, UIWorkspace_SwitchLayout(Screen, Fixture.WorkspaceId, 1U));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK_SUCCESS(run, UIWorkspace_NewLayout(Screen, Fixture.WorkspaceId, u8"Third", NULL));
        TestUI_Ticks(run, &Fixture.UI, (size_t)(UI_LAYOUTS_SETTLE_SECONDS / TEST_UI_TICK_SECONDS) + 4U);
        TEST_CHECK(run, !UILayouts_HasChange(&Layouts) && (UILayouts_GetCount(&Layouts) == 3U));
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    WorkspaceFixture Again;
    UILayouts AgainLayouts;
    if (HasPath && Construct(run, &Again, true) && StartLayouts(run, &Again, &AgainLayouts, Path._data))
    {
        size_t ShowingMaximized = 0U;
        size_t MinimizedFromMaximized = 0U;
        for (size_t Index = 0U; Index < Again.BoxCount; Index++)
        {
            bool IsMinimized = false;
            bool IsMaximized = false;
            Error Result = UIWindow_GetState(&Again.UI.Screen, Again.Windows[Index], &IsMinimized, &IsMaximized);
            Error_Deconstruct(&Result);
            ShowingMaximized += (IsMaximized && !IsMinimized) ? 1U : 0U;
            MinimizedFromMaximized += (IsMaximized && IsMinimized) ? 1U : 0U;
        }
        TEST_CHECK(run, (ShowingMaximized == 1U) && (MinimizedFromMaximized == 1U));
        StopLayouts(run, &Again, &AgainLayouts);
    }
    Deconstruct(run, &Again);
    Memory_Free(Path._data);
}

/* Resetting: a window whose close query keeps it floats out of the dock, and the default is built whole on the empty dock; a builder that
 * fails is told, what it built kept. */
static void ResettingFloatsWhatStaysAndBuildsTheDefaultWhole(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    if (MakeLayoutsPath(run, "workspace_reset_refused", &Path) && Construct(run, &Fixture, true)
        && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        UIScreen* Screen = &Fixture.UI.Screen;
        uint64_t A = WindowOfType(&Fixture, TYPE_A);
        uint64_t C = Open(run, &Fixture, TYPE_C); // Not in the default.
        Dock(run, &Fixture, C, A, UIDockZone_Bottom);
        TEST_CHECK_SUCCESS(run, UIWindow_SetCloseQuery(Screen, C, RefuseToClose, NULL));
        TEST_CHECK_SUCCESS(run, UIWorkspace_ResetLayout(Screen, Fixture.WorkspaceId));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, UIScreen_IsWidgetAlive(Screen, C) && (PlaceOf(&Fixture, C) == UIWorkspacePlace_Floating) && IsShown(&Fixture, C));
        uint64_t NewA = WindowOfType(&Fixture, TYPE_A);
        uint64_t NewB = WindowOfType(&Fixture, TYPE_B);
        for (size_t Index = 0U; Index < Fixture.BoxCount; Index++)
        {
            UIWindowTypes* Types = NULL;
            uint32_t TypeIndex = 0U;
            Error Result = UIWindow_GetType(Screen, Fixture.Windows[Index], &Types, &TypeIndex);
            NewA = ((Result.Code == ErrorCode_Success) && (TypeIndex == Fixture.TypeIndices[TYPE_A])) ? Fixture.Windows[Index] : NewA;
            NewB = ((Result.Code == ErrorCode_Success) && (TypeIndex == Fixture.TypeIndices[TYPE_B])) ? Fixture.Windows[Index] : NewB;
            Error_Deconstruct(&Result);
        }
        TEST_CHECK(run, (PlaceOf(&Fixture, NewA) == UIWorkspacePlace_Docked) && (PlaceOf(&Fixture, NewB) == UIWorkspacePlace_Docked));
        TEST_CHECK(run, (UIDockTree_GetLeafCount(Tree(&Fixture)) == 2U) && (UIDockTree_GetTotalTabCount(Tree(&Fixture)) == 2U));
        // A builder that fails: told, and what it built is kept.
        TEST_CHECK_SUCCESS(run, UIWindow_SetCloseQuery(Screen, C, NULL, NULL));
        TEST_CHECK_SUCCESS(run, UIWorkspace_SetLayouts(Screen, Fixture.WorkspaceId, &Layouts, FailToBuild, NULL));
        TEST_CHECK_ERROR(run, UIWorkspace_ResetLayout(Screen, Fixture.WorkspaceId), ErrorCode_InvalidOperation);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (CountKept(&Fixture) == 0U) && UIDockTree_IsEmpty(Tree(&Fixture)));
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    Memory_Free(Path._data);
}

/* A docked window closed after the workspace's update (a handler, its own content), its tab still there in the next tick: a press, a double
 * click or its menu's Undock there does nothing, and the screen's update goes on. */
static void TheTabOfAWindowJustClosedDoesNothing(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        Vector2 At = Middle(TabBox(run, &Fixture, C));
        MoveTo(run, &Fixture, At.x, At.y);
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, C, NULL));
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI); // Fails the case if the screen's update fails.
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.UI.Screen, C) && UIDockTree_IsValid(Tree(&Fixture)));
        // A double click's second press, on B's tab once B has gone.
        WaitOutClicks(run, &Fixture);
        At = Middle(TabBox(run, &Fixture, B));
        TestUI_Click(run, &Fixture.UI, At.x, At.y);
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, B, NULL));
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        // Its menu's Undock, chosen once the window has gone.
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        Dock(run, &Fixture, Notes, A, UIDockZone_Right);
        WaitOutClicks(run, &Fixture);
        At = Middle(TabBox(run, &Fixture, Notes));
        TestUI_MovePointer(&Fixture.UI, At.x, At.y);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Release(&Fixture.UI, GameKey_MouseRight);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.UI.Screen) == 1U);
        uint64_t MenuId = UIScreen_GetPopupAt(&Fixture.UI.Screen, 0U);
        Rectangle Menu = PixelBox(run, &Fixture, MenuId);
        TestUI_MovePointer(&Fixture.UI, Menu.x + 20.0f, Menu.y + 2.0f + 11.0f); // Undock, the menu's first item.
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft); // A menu's item acts on its release.
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.UI.Screen, MenuId));
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, Notes, NULL));
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (CountKept(&Fixture) == 1U) && (PlaceOf(&Fixture, A) == UIWorkspacePlace_Docked));
    }
    Deconstruct(run, &Fixture);
}

/* A tab torn out and let go in one tick, where the area it left had a guide: nothing docks (the dock has not been laid out again), as in two
 * ticks, where the guide is no longer there. */
static void ATearOutLetGoInOneTickDocksNothing(TestRun* run)
{
    for (int32_t Ticks = 1; Ticks <= 2; Ticks++)
    {
        WorkspaceFixture Fixture;
        if (Construct(run, &Fixture, true))
        {
            uint64_t A = WIDGET_ID_INVALID;
            uint64_t B = WIDGET_ID_INVALID;
            uint64_t C = WIDGET_ID_INVALID;
            BuildThree(run, &Fixture, &A, &B, &C);
            Rectangle OldLeaf = { .x = 0.0f, .y = DOCK_TOP, .width = 498.0f, .height = DOCK_HEIGHT };
            Vector2 Target = Middle(UIDockZone_GetGuide(OldLeaf, STRIP, UIDockZone_Center, GUIDE));
            Vector2 At = Middle(TabBox(run, &Fixture, C));
            PressAt(run, &Fixture, At.x, At.y);
            TestUI_MovePointer(&Fixture.UI, Target.x, Target.y);
            if (Ticks == 2)
            {
                TestUI_Tick(run, &Fixture.UI);
            }
            TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
            TestUI_Ticks(run, &Fixture.UI, 3U);
            TEST_CHECK(run, (PlaceOf(&Fixture, C) == UIWorkspacePlace_Floating) && !UIScreen_HasHeldPress(&Fixture.UI.Screen));
            TEST_CHECK(run, UIDockTree_GetLeafCount(Tree(&Fixture)) == 1U);
        }
        Deconstruct(run, &Fixture);
    }
}

/* A strip with more tabs than it can show keeps the selected one on it, and the wheel over it steps through them all. */
static void TheWheelStepsThroughTabsAStripCannotShow(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = Open(run, &Fixture, TYPE_A);
        uint64_t B = Open(run, &Fixture, TYPE_B);
        Dock(run, &Fixture, A, WIDGET_ID_INVALID, UIDockZone_Center);
        Dock(run, &Fixture, B, A, UIDockZone_Right);
        TEST_CHECK_SUCCESS(run, UIWorkspace_SetAreaShare(&Fixture.UI.Screen, Fixture.WorkspaceId, A, 0.2f));
        uint64_t Notes[24];
        uint64_t Last = A;
        for (size_t Index = 0U; Index < 24U; Index++)
        {
            TEST_CHECK_SUCCESS(run, UIWorkspace_OpenType(&Fixture.UI.Screen, Fixture.WorkspaceId, Fixture.TypeIndices[TYPE_NOTES], &Notes[Index]));
            TEST_CHECK_SUCCESS(run, UIWorkspace_Dock(&Fixture.UI.Screen, Fixture.WorkspaceId, Notes[Index], Last, UIDockZone_Center));
            Last = Notes[Index];
        }
        TestUI_Ticks(run, &Fixture.UI, 3U);
        int32_t Leaf = LeafOf(&Fixture, A);
        Rectangle Area = PixelBox(run, &Fixture, Last);
        Rectangle Selected = TabBox(run, &Fixture, Last);
        TEST_CHECK(run, (Selected.width > 0.0f) && (Selected.x >= Area.x) && ((Selected.x + Selected.width) <= (Area.x + Area.width)));
        TEST_CHECK(run, TabBox(run, &Fixture, A).width == 0.0f); // The first ones are left out.
        // The wheel away from the player over the strip: the tab before, as a press would (shown, focused); towards: the next again.
        Vector2 Over = { .x = Selected.x + (Selected.width * 0.5f), .y = Selected.y + (Selected.height * 0.5f) };
        TestUI_MovePointer(&Fixture.UI, Over.x, Over.y);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Wheel(&Fixture.UI, 1.0f);
        TestUI_Ticks(run, &Fixture.UI, 3U);
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), Leaf) == Notes[22]);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == GetBox(&Fixture, Notes[22]));
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Ticks(run, &Fixture.UI, 3U);
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), Leaf) == Last);
        // All the way to the first: it shows.
        for (size_t Step = 0U; Step < 30U; Step++)
        {
            TestUI_Wheel(&Fixture.UI, 1.0f);
            TestUI_Tick(run, &Fixture.UI);
        }
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), Leaf) == A);
        Rectangle First = TabBox(run, &Fixture, A);
        TEST_CHECK(run, (First.width > 0.0f) && (First.x >= Area.x));
        // Over a window's content, the wheel is the content's: the tabs stay.
        Rectangle Content = PixelBox(run, &Fixture, A);
        TestUI_MovePointer(&Fixture.UI, Content.x + 20.0f, Content.y + 60.0f);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Wheel(&Fixture.UI, -1.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), Leaf) == A);
    }
    Deconstruct(run, &Fixture);
}

/* Escape on a torn tab once code has changed the dock meanwhile: the dock is kept as code left it, and the window floats where it was
 * before the drag. */
static void EscapeKeepsADockChangedMeanwhile(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        Rectangle Before = FloatingBox(run, &Fixture, B);
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        Vector2 At = Middle(TabBox(run, &Fixture, B));
        PressAt(run, &Fixture, At.x, At.y);
        MoveTo(run, &Fixture, At.x + 100.0f, At.y + 100.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Floating);
        Dock(run, &Fixture, Notes, C, UIDockZone_Bottom);
        TestUI_Tap(run, &Fixture.UI, GameKey_Escape);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (PlaceOf(&Fixture, Notes) == UIWorkspacePlace_Docked) && (PlaceOf(&Fixture, B) == UIWorkspacePlace_Floating));
        TEST_CHECK(run, TestUI_IsBox(FloatingBox(run, &Fixture, B), Before.x, Before.y, Before.width, Before.height));
        ReleaseAt(run, &Fixture, At.x + 100.0f, At.y + 100.0f);
        TEST_CHECK(run, (PlaceOf(&Fixture, B) == UIWorkspacePlace_Floating) && UIDockTree_IsValid(Tree(&Fixture)));
    }
    Deconstruct(run, &Fixture);
}

/* Docked as a tab beside a window of its own area that stood after it: right after that window. */
static void DockedBesideAWindowOfItsOwnAreaItStandsAfterIt(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t W = Open(run, &Fixture, TYPE_A);
        uint64_t X = Open(run, &Fixture, TYPE_B);
        uint64_t Y = Open(run, &Fixture, TYPE_C);
        Dock(run, &Fixture, W, WIDGET_ID_INVALID, UIDockZone_Center);
        Dock(run, &Fixture, X, W, UIDockZone_Center);
        Dock(run, &Fixture, Y, X, UIDockZone_Center);
        Dock(run, &Fixture, W, X, UIDockZone_Center);
        int32_t Leaf = LeafOf(&Fixture, W);
        TEST_CHECK(run, (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 0U) == X) && (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 1U) == W)
            && (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 2U) == Y));
        Dock(run, &Fixture, Y, X, UIDockZone_Center); // From after it: still right after it.
        TEST_CHECK(run, (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 0U) == X) && (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 1U) == Y)
            && (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 2U) == W));
    }
    Deconstruct(run, &Fixture);
}

static void EscapePutsATornTabBack(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        UIDockTree Before = *Tree(&Fixture);
        Rectangle BoxBefore = FloatingBox(run, &Fixture, B);
        Rectangle Shown = PixelBox(run, &Fixture, B);
        Vector2 At = Middle(TabBox(run, &Fixture, B));
        PressAt(run, &Fixture, At.x, At.y);
        MoveTo(run, &Fixture, At.x + 100.0f, At.y + 100.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Floating);
        TestUI_Tap(run, &Fixture.UI, GameKey_Escape);
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked);
        TEST_CHECK(run, LeafOf(&Fixture, B) == LeafOf(&Fixture, A));
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), LeafOf(&Fixture, A)) == B);
        Rectangle BoxAfter = FloatingBox(run, &Fixture, B);
        TEST_CHECK(run, TestUI_IsBox(BoxAfter, BoxBefore.x, BoxBefore.y, BoxBefore.width, BoxBefore.height));
        // The rest of the press does nothing more, and nothing is left held.
        MoveTo(run, &Fixture, At.x + 200.0f, At.y + 150.0f);
        ReleaseAt(run, &Fixture, At.x + 200.0f, At.y + 150.0f);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.UI.Screen) && (PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked));
        TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Fixture, B), Shown.x, Shown.y, Shown.width, Shown.height));
        TEST_CHECK(run, (UIDockTree_GetLeafCount(Tree(&Fixture)) == UIDockTree_GetLeafCount(&Before))
            && (UIDockTree_GetTabCount(Tree(&Fixture), LeafOf(&Fixture, A)) == 2U));
    }
    Deconstruct(run, &Fixture);
}

static void ATabDraggedAlongItsStripReorders(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = Open(run, &Fixture, TYPE_A);
        uint64_t B = Open(run, &Fixture, TYPE_B);
        uint64_t C = Open(run, &Fixture, TYPE_C);
        Dock(run, &Fixture, A, WIDGET_ID_INVALID, UIDockZone_Center);
        Dock(run, &Fixture, B, A, UIDockZone_Center);
        Dock(run, &Fixture, C, B, UIDockZone_Center);
        int32_t Leaf = LeafOf(&Fixture, A);
        Vector2 At = Middle(TabBox(run, &Fixture, A));
        PressAt(run, &Fixture, At.x, At.y); // It is selected now: its name shows, and the others move to its right.
        Rectangle BTab = TabBox(run, &Fixture, B);
        Rectangle CTab = TabBox(run, &Fixture, C);
        MoveTo(run, &Fixture, BTab.x + 2.0f, At.y); // Short of B's middle: A stays first.
        TEST_CHECK(run, UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 0U) == A);
        MoveTo(run, &Fixture, BTab.x + BTab.width - 2.0f, At.y);
        TEST_CHECK(run, (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 0U) == B) && (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 1U) == A));
        MoveTo(run, &Fixture, CTab.x + CTab.width + 30.0f, At.y);
        TEST_CHECK(run, (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 1U) == C) && (UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 2U) == A));
        MoveTo(run, &Fixture, 0.0f, At.y);
        TEST_CHECK(run, UIDockTree_GetTabAt(Tree(&Fixture), Leaf, 0U) == A);
        ReleaseAt(run, &Fixture, 0.0f, At.y);
        TEST_CHECK(run, (PlaceOf(&Fixture, A) == UIWorkspacePlace_Docked) && !UIScreen_HasHeldPress(&Fixture.UI.Screen));
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), Leaf) == A);
    }
    Deconstruct(run, &Fixture);
}

static void AFloatingWindowDropsOnGuidesAndFloatsElsewhere(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, Notes, (Rectangle){ .x = 600.0f, .y = 300.0f, .width = 240.0f, .height = 140.0f }));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        // Over the dock but on no guide: it moves and floats on.
        PressAt(run, &Fixture, 700.0f, 310.0f);
        MoveTo(run, &Fixture, 650.0f, 120.0f);
        UIWorkspaceDropView View;
        TEST_CHECK(run, UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View) && !View.HasTarget);
        ReleaseAt(run, &Fixture, 650.0f, 120.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, Notes) == UIWorkspacePlace_Floating);
        TEST_CHECK(run, TestUI_IsSamePixel(FloatingBox(run, &Fixture, Notes).y, 110.0f));
        // On C's left guide: a new area at C's left.
        Rectangle CBox = PixelBox(run, &Fixture, C);
        Vector2 Centre = Middle(CBox);
        Rectangle Title = { .x = 600.0f, .y = 110.0f };
        WaitOutClicks(run, &Fixture);
        PressAt(run, &Fixture, Title.x + 60.0f, Title.y + 10.0f);
        MoveTo(run, &Fixture, Centre.x - GUIDE, Centre.y);
        TEST_CHECK(run, UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View) && View.HasTarget && (View.Zone == UIDockZone_Left));
        ReleaseAt(run, &Fixture, Centre.x - GUIDE, Centre.y);
        TEST_CHECK(run, PlaceOf(&Fixture, Notes) == UIWorkspacePlace_Docked);
        const UIDockNode* Leaf = UIDockTree_GetNode(Tree(&Fixture), LeafOf(&Fixture, Notes));
        const UIDockNode* Split = (Leaf != NULL) ? UIDockTree_GetNode(Tree(&Fixture), Leaf->Parent) : NULL;
        TEST_CHECK(run, (Split != NULL) && (Split->Split == UIDockSplit_Across) && (Split->Children[0] == LeafOf(&Fixture, Notes))
            && (Split->Children[1] == LeafOf(&Fixture, C)));
        TEST_CHECK(run, PixelBox(run, &Fixture, Notes).x < PixelBox(run, &Fixture, C).x);
        // Another window over the dock hides the guides under it.
        uint64_t Second = Open(run, &Fixture, TYPE_NOTES);
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, Second, (Rectangle){ .x = 50.0f, .y = 300.0f, .width = 200.0f, .height = 120.0f }));
        uint64_t Third = Open(run, &Fixture, TYPE_NOTES);
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, Third, (Rectangle){ .x = 400.0f, .y = 300.0f, .width = 200.0f, .height = 120.0f }));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        WaitOutClicks(run, &Fixture);
        PressAt(run, &Fixture, 450.0f, 310.0f);
        MoveTo(run, &Fixture, 100.0f, 330.0f);
        TEST_CHECK(run, !UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View));
        ReleaseAt(run, &Fixture, 100.0f, 330.0f);
        TEST_CHECK(run, PlaceOf(&Fixture, Third) == UIWorkspacePlace_Floating);
    }
    Deconstruct(run, &Fixture);
}

static void AnEmptyDockTakesAWindowOnItsCentreGuide(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, Notes, (Rectangle){ .x = 50.0f, .y = 50.0f, .width = 240.0f, .height = 140.0f }));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        PressAt(run, &Fixture, 100.0f, 60.0f);
        MoveTo(run, &Fixture, 500.0f, DOCK_TOP + (DOCK_HEIGHT * 0.5f));
        UIWorkspaceDropView View;
        TEST_CHECK(run, UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View) && View.IsDockEmpty && View.HasTarget);
        TEST_CHECK(run, TestUI_IsBox(View.Preview, 0.0f, DOCK_TOP, 1000.0f, DOCK_HEIGHT));
        ReleaseAt(run, &Fixture, 500.0f, DOCK_TOP + (DOCK_HEIGHT * 0.5f));
        TEST_CHECK(run, PlaceOf(&Fixture, Notes) == UIWorkspacePlace_Docked);
        TEST_CHECK(run, UIDockTree_GetRoot(Tree(&Fixture)) == LeafOf(&Fixture, Notes));
    }
    Deconstruct(run, &Fixture);
}

static void AWindowGoneOrChangedMidDragLeavesNothingHeld(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        Vector2 At = Middle(TabBox(run, &Fixture, B));
        PressAt(run, &Fixture, At.x, At.y);
        MoveTo(run, &Fixture, At.x + 80.0f, At.y + 80.0f);
        Widget* Window = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, B, &Window));
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Window));
        MoveTo(run, &Fixture, 750.0f, 250.0f);
        UIWorkspaceDropView View;
        TEST_CHECK(run, !UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View));
        ReleaseAt(run, &Fixture, 750.0f, 250.0f);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.UI.Screen));
        TEST_CHECK(run, (PlaceOf(&Fixture, B) == (UIWorkspacePlace)-1) && UIDockTree_IsValid(Tree(&Fixture)));
        TEST_CHECK(run, UIDockTree_GetTotalTabCount(Tree(&Fixture)) == 2U);
        // Maximized or minimized by code mid-drag over the dock: the guides go, and the release docks nothing.
        for (int32_t State = 0; State < 2; State++)
        {
            WaitOutClicks(run, &Fixture);
            uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
            Rectangle Shown = PixelBox(run, &Fixture, Notes);
            PressAt(run, &Fixture, Shown.x + 30.0f, Shown.y + 8.0f);
            Rectangle Left = PixelBox(run, &Fixture, A);
            MoveTo(run, &Fixture, Left.x + (Left.width * 0.5f), Left.y + (Left.height * 0.5f));
            TEST_CHECK(run, UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View));
            TEST_CHECK_SUCCESS(run, (State == 0) ? UIWindow_Maximize(&Fixture.UI.Screen, Notes) : UIWindow_Minimize(&Fixture.UI.Screen, Notes));
            TestUI_Tick(run, &Fixture.UI);
            TEST_CHECK(run, !UIWorkspace_GetDropView(&Fixture.UI.Screen, Fixture.WorkspaceId, &View));
            ReleaseAt(run, &Fixture, Left.x + (Left.width * 0.5f), Left.y + (Left.height * 0.5f));
            TEST_CHECK(run, (PlaceOf(&Fixture, Notes) == UIWorkspacePlace_Floating) && (UIDockTree_GetTotalTabCount(Tree(&Fixture)) == 2U));
            TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.UI.Screen, Notes, NULL));
            TestUI_Ticks(run, &Fixture.UI, 2U);
        }
    }
    Deconstruct(run, &Fixture);
}

static void WhatAWindowHoldsStaysThroughDockingAndUndocking(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = Open(run, &Fixture, TYPE_A);
        uint64_t Box = GetBox(&Fixture, A);
        Rectangle BoxPixels = PixelBox(run, &Fixture, Box);
        TestUI_Click(run, &Fixture.UI, BoxPixels.x + 10.0f, BoxPixels.y + 10.0f);
        TypeText(run, &Fixture, "kept");
        TEST_CHECK(run, HasText(&Fixture, Box, "kept") && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Box));
        Dock(run, &Fixture, A, WIDGET_ID_INVALID, UIDockZone_Center);
        TEST_CHECK(run, HasText(&Fixture, Box, "kept") && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Box));
        TypeText(run, &Fixture, "!");
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(&Fixture.UI.Screen, Fixture.WorkspaceId, A));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (PlaceOf(&Fixture, A) == UIWorkspacePlace_Floating) && IsRoot(&Fixture, A));
        TEST_CHECK(run, HasText(&Fixture, Box, "kept!") && UIScreen_IsWidgetAlive(&Fixture.UI.Screen, Box));
        TEST_CHECK(run, UIDockTree_IsEmpty(Tree(&Fixture)));
    }
    Deconstruct(run, &Fixture);
}

static void MaximizedFromTheDockItGoesBackToItsTab(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        Rectangle Docked = PixelBox(run, &Fixture, C);
        TEST_CHECK_SUCCESS(run, UIWorkspace_MaximizeDocked(&Fixture.UI.Screen, Fixture.WorkspaceId, C));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (PlaceOf(&Fixture, C) == UIWorkspacePlace_Maximized) && IsRoot(&Fixture, C));
        TEST_CHECK(run, DockOf(&Fixture, C) == UIWindowDock_Maximized);
        TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Fixture, C), 0.0f, DOCK_TOP, 1000.0f, DOCK_HEIGHT));
        TEST_CHECK(run, LeafOf(&Fixture, C) != UI_DOCK_NODE_NONE); // Its tab keeps its place.
        uint64_t Minimize = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindow_GetButtonId(&Fixture.UI.Screen, C, UIWindowButton_Minimize, &Minimize));
        TEST_CHECK(run, !IsShown(&Fixture, Minimize));
        TEST_CHECK_ERROR(run, UIWindow_Minimize(&Fixture.UI.Screen, C), ErrorCode_InvalidOperation);
        // Its restore button: back in its tab.
        uint64_t Maximize = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWindow_GetButtonId(&Fixture.UI.Screen, C, UIWindowButton_Maximize, &Maximize));
        Vector2 At = Middle(PixelBox(run, &Fixture, Maximize));
        TestUI_Click(run, &Fixture.UI, At.x, At.y);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (PlaceOf(&Fixture, C) == UIWorkspacePlace_Docked) && !IsRoot(&Fixture, C));
        TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Fixture, C), Docked.x, Docked.y, Docked.width, Docked.height));
        // A double click on its tab does the same, and restores it again.
        WaitOutClicks(run, &Fixture);
        Vector2 TabAt = Middle(TabBox(run, &Fixture, C));
        TestUI_Click(run, &Fixture.UI, TabAt.x, TabAt.y);
        TestUI_Click(run, &Fixture.UI, TabAt.x, TabAt.y);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, PlaceOf(&Fixture, C) == UIWorkspacePlace_Maximized);
        // Another window maximized restores it to its tab.
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.UI.Screen, Notes));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, PlaceOf(&Fixture, C) == UIWorkspacePlace_Docked);
    }
    Deconstruct(run, &Fixture);
}

static void SplittersDragWithinWhatTheWindowsNeed(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        int32_t Root = UIDockTree_GetRoot(Tree(&Fixture));
        uint64_t Splitter = UIWorkspace_GetSplitterId(&Fixture.UI.Screen, Fixture.WorkspaceId, Root);
        Rectangle Bar = PixelBox(run, &Fixture, Splitter);
        TEST_CHECK(run, TestUI_IsBox(Bar, 498.0f, DOCK_TOP, SPLITTER, DOCK_HEIGHT));
        Widget* SplitterWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.UI.Screen, Splitter, &SplitterWidget)
            && (Widget_GetCursorShape(SplitterWidget) == UICursorShape_ResizeHorizontal));
        Vector2 At = Middle(Bar);
        uint64_t Focused = UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen);
        PressAt(run, &Fixture, At.x, At.y);
        MoveTo(run, &Fixture, 302.0f, At.y);
        TEST_CHECK(run, TestUI_IsSamePixel(PixelBox(run, &Fixture, B).width, 300.0f));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Focused); // A splitter acts on nothing in the windows.
        // Far left: the left area keeps its windows' least width, their text box's 100 with the content's padding (4 a side) and the bar's
        // room (10) beside it.
        MoveTo(run, &Fixture, 5.0f, At.y);
        TEST_CHECK(run, TestUI_IsSamePixel(PixelBox(run, &Fixture, B).width, 118.0f));
        ReleaseAt(run, &Fixture, 5.0f, At.y);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.UI.Screen));
        // The ratio holds through a smaller surface, and the boxes are exact again when it comes back.
        TEST_CHECK_SUCCESS(run, UIWorkspace_SetAreaShare(&Fixture.UI.Screen, Fixture.WorkspaceId, C, 0.75f));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Rectangle Wide = PixelBox(run, &Fixture, C);
        TEST_CHECK(run, TestUI_IsSamePixel(Wide.width, 747.0f));
        TestUI_SetSurface(run, &Fixture.UI, 640.0f, 360.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, TestUI_IsSamePixel(PixelBox(run, &Fixture, C).width, 477.0f));
        TestUI_SetSurface(run, &Fixture.UI, 1000.0f, 500.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Rectangle Back = PixelBox(run, &Fixture, C);
        TEST_CHECK(run, TestUI_IsBox(Back, Wide.x, Wide.y, Wide.width, Wide.height));
    }
    Deconstruct(run, &Fixture);
}

static void ATabsContextMenuUndocks(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        Vector2 At = Middle(TabBox(run, &Fixture, C));
        TestUI_MovePointer(&Fixture.UI, At.x, At.y);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Press(&Fixture.UI, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Release(&Fixture.UI, GameKey_MouseRight);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.UI.Screen) == 1U);
        // Its first item, Undock, a menu padding and half an item below the pointer.
        TestUI_Click(run, &Fixture.UI, At.x + 20.0f, At.y + 2.0f + 11.0f);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.UI.Screen) == 0U) && (PlaceOf(&Fixture, C) == UIWorkspacePlace_Floating));
        TEST_CHECK(run, UIDockTree_GetLeafCount(Tree(&Fixture)) == 1U);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == GetBox(&Fixture, C));
    }
    Deconstruct(run, &Fixture);
}

static void ItsTabsShowWhatTheirWindowsAre(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        // Hover text: the window's name.
        Vector2 At = Middle(TabBox(run, &Fixture, A));
        TestUI_MovePointer(&Fixture.UI, At.x, At.y);
        TestUI_Ticks(run, &Fixture.UI, 400U);
        UITooltipState Current;
        TEST_CHECK(run, UITooltip_GetCurrent(&Fixture.UI.Screen, &Current) && Current.IsShown && (Current.Text != NULL)
            && (strcmp((const char*)Current.Text, "Alpha") == 0));
        // A renamed window renames its tab, which grows while selected.
        float Before = TabBox(run, &Fixture, B).width;
        TEST_CHECK_SUCCESS(run, UIWindow_SetTitle(&Fixture.UI.Screen, B, u8"Beta with a longer name"));
        TestUI_Ticks(run, &Fixture.UI, 3U);
        TEST_CHECK(run, TabBox(run, &Fixture, B).width > Before);
        // The selected tab is no wider than the theme's tab_max_width.
        TEST_CHECK(run, TabBox(run, &Fixture, B).width <= 150.0f);
    }
    Deconstruct(run, &Fixture);
}

static void HiddenItHidesItsFloatingWindowsAndTakesNoDrops(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        TEST_CHECK_SUCCESS(run, UIWorkspace_SetShown(&Fixture.UI.Screen, Fixture.WorkspaceId, false));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, !IsShown(&Fixture, Notes) && !IsShown(&Fixture, Fixture.WorkspaceId));
        TEST_CHECK_SUCCESS(run, UIWorkspace_SetShown(&Fixture.UI.Screen, Fixture.WorkspaceId, true));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, IsShown(&Fixture, Notes) && IsShown(&Fixture, Fixture.WorkspaceId) && IsShown(&Fixture, C));
    }
    Deconstruct(run, &Fixture);
}

static void ASingleTypeOpenedAgainComesBack(TestRun* run)
{
    WorkspaceFixture Fixture;
    if (Construct(run, &Fixture, true))
    {
        uint64_t A = WIDGET_ID_INVALID;
        uint64_t B = WIDGET_ID_INVALID;
        uint64_t C = WIDGET_ID_INVALID;
        BuildThree(run, &Fixture, &A, &B, &C);
        uint64_t Again = Open(run, &Fixture, TYPE_A);
        TEST_CHECK(run, Again == A);
        TEST_CHECK(run, UIDockTree_GetSelected(Tree(&Fixture), LeafOf(&Fixture, A)) == A);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == GetBox(&Fixture, A));
        uint64_t Notes = Open(run, &Fixture, TYPE_NOTES);
        uint64_t MoreNotes = Open(run, &Fixture, TYPE_NOTES);
        TEST_CHECK(run, (Notes != MoreNotes) && (PlaceOf(&Fixture, MoreNotes) == UIWorkspacePlace_Floating));
        TEST_CHECK_ERROR(run, UIWorkspace_OpenType(&Fixture.UI.Screen, Fixture.WorkspaceId, 999U, NULL), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIWorkspace_OpenTypeByKey(&Fixture.UI.Screen, Fixture.WorkspaceId, u8"tbp:nothing", NULL), ErrorCode_InvalidOperation);
    }
    Deconstruct(run, &Fixture);
}


/* The default layout the layouts' cases build: A in the dock, B at its left taking a quarter, Notes floating. */
static Error BuildDefault(UIScreen* screen, uint64_t workspaceId, const UserData* userData)
{
    WorkspaceFixture* Fixture = UserData_GetPointer(userData);
    uint64_t A = WIDGET_ID_INVALID;
    uint64_t B = WIDGET_ID_INVALID;
    uint64_t Notes = WIDGET_ID_INVALID;
    Error Result = UIWorkspace_OpenType(screen, workspaceId, Fixture->TypeIndices[TYPE_A], &A);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_OpenType(screen, workspaceId, Fixture->TypeIndices[TYPE_B], &B);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_OpenType(screen, workspaceId, Fixture->TypeIndices[TYPE_NOTES], &Notes);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, A, WIDGET_ID_INVALID, UIDockZone_Center);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_Dock(screen, workspaceId, B, A, UIDockZone_Left);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWorkspace_SetAreaShare(screen, workspaceId, B, 0.25f);
    }
    return (Result.Code == ErrorCode_Success) ? UIWindow_SetBox(screen, Notes, (Rectangle){ .x = 600.0f, .y = 200.0f, .width = 250.0f,
        .height = 150.0f }) : Result;
}

/* Whether a file is there. */
static bool IsFileThere(const unsigned char* path)
{
    FileSystemEntryInfo Info;
    Error Result = FileSystem_GetEntryInfo(path, &Info);
    bool IsThere = Result.Code == ErrorCode_Success;
    if (IsThere)
    {
        FileSystemEntryInfo_Deconstruct(&Info);
    }
    Error_Deconstruct(&Result);
    return IsThere;
}

/* The layouts' file in a work folder of the case's, none there from an earlier run. */
static bool MakeLayoutsPath(TestRun* run, const char* folder, GenericBuffer* outPath)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, 256U, sizeof(unsigned char));
    bool IsMade = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, folder, &Directory))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"editor_layouts.json", outPath));
    Memory_Free(Directory._data);
    if (IsMade && IsFileThere(outPath->_data))
    {
        IsMade = TEST_CHECK_SUCCESS(run, FileSystem_DeleteEntry(outPath->_data));
    }
    return IsMade;
}

static bool StartLayouts(TestRun* run, WorkspaceFixture* fixture, UILayouts* layouts, const unsigned char* path)
{
    if (!TEST_CHECK_SUCCESS(run, UILayouts_Construct(layouts, path, &fixture->Types, &fixture->UI.Keys, NULL)))
    {
        return false;
    }
    TEST_CHECK_SUCCESS(run, UILayouts_Load(layouts, NULL));
    UserData Data = UserData_FromPointer(fixture);
    TEST_CHECK_SUCCESS(run, UIWorkspace_SetLayouts(&fixture->UI.Screen, fixture->WorkspaceId, layouts, BuildDefault, &Data));
    TestUI_Ticks(run, &fixture->UI, 2U);
    return true;
}

static void StopLayouts(TestRun* run, WorkspaceFixture* fixture, UILayouts* layouts)
{
    TEST_CHECK_SUCCESS(run, UIWorkspace_SaveLayouts(&fixture->UI.Screen, fixture->WorkspaceId));
    TEST_CHECK_SUCCESS(run, UIWorkspace_SetLayouts(&fixture->UI.Screen, fixture->WorkspaceId, NULL, NULL, NULL));
    UILayouts_Deconstruct(layouts);
}

/* The window of a type the workspace keeps (its first). */
static uint64_t WindowOfType(WorkspaceFixture* fixture, size_t type)
{
    for (size_t Index = 0U; Index < fixture->BoxCount; Index++)
    {
        UIWindowTypes* Types = NULL;
        uint32_t TypeIndex = 0U;
        Error Result = UIWindow_GetType(&fixture->UI.Screen, fixture->Windows[Index], &Types, &TypeIndex);
        bool IsIt = (Result.Code == ErrorCode_Success) && (TypeIndex == fixture->TypeIndices[type]);
        Error_Deconstruct(&Result);
        if (IsIt)
        {
            return fixture->Windows[Index];
        }
    }
    return WIDGET_ID_INVALID;
}

/* How many windows the workspace keeps. */
static size_t CountKept(WorkspaceFixture* fixture)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < fixture->BoxCount; Index++)
    {
        UIWorkspacePlace Place = UIWorkspacePlace_Floating;
        Count += UIWorkspace_GetPlace(&fixture->UI.Screen, fixture->WorkspaceId, fixture->Windows[Index], &Place) ? 1U : 0U;
    }
    return Count;
}

static void WithNoLayoutsItBuildsTheDefaultAndSavesItOnceSettled(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    if (MakeLayoutsPath(run, "workspace_default", &Path) && Construct(run, &Fixture, true) && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        TEST_CHECK(run, (UILayouts_GetCount(&Layouts) == 1U) && (strcmp((const char*)UILayouts_Get(&Layouts, 0U)->Name, "Default") == 0));
        uint64_t A = WindowOfType(&Fixture, TYPE_A);
        uint64_t B = WindowOfType(&Fixture, TYPE_B);
        TEST_CHECK(run, (PlaceOf(&Fixture, A) == UIWorkspacePlace_Docked) && (PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked));
        TEST_CHECK(run, TestUI_IsSamePixel(PixelBox(run, &Fixture, B).width, 249.0f));
        // Not written at once: once the changes have settled.
        TEST_CHECK(run, !IsFileThere(Path._data) && UILayouts_HasChange(&Layouts));
        TestUI_Ticks(run, &Fixture.UI, (size_t)(UI_LAYOUTS_SETTLE_SECONDS / TEST_UI_TICK_SECONDS) - 8U);
        TEST_CHECK(run, !IsFileThere(Path._data));
        TestUI_Ticks(run, &Fixture.UI, 12U);
        TEST_CHECK(run, IsFileThere(Path._data));
        TEST_CHECK(run, !UILayouts_HasChange(&Layouts));
        // An idle workspace notes nothing; a change is noted, and saved once it settles.
        TestUI_Ticks(run, &Fixture.UI, 10U);
        TEST_CHECK(run, !UILayouts_HasChange(&Layouts));
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(&Fixture.UI.Screen, Fixture.WorkspaceId, A));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, UILayouts_HasChange(&Layouts));
        TestUI_Ticks(run, &Fixture.UI, (size_t)(UI_LAYOUTS_SETTLE_SECONDS / TEST_UI_TICK_SECONDS) + 4U);
        TEST_CHECK(run, !UILayouts_HasChange(&Layouts));
        TEST_CHECK(run, !UILayout_IsWindowDocked(UILayouts_Get(&Layouts, 0U), 0U)); // A, the first window, kept floating.
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    Memory_Free(Path._data);
}

static void ALayoutSavedComesBackAfterARestart(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    UILayout* Saved = Memory_Allocate(sizeof(UILayout));
    bool HasPath = MakeLayoutsPath(run, "workspace_restart", &Path);
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    Rectangle NotesBox = { 0 };
    Rectangle CBox = { 0 };
    if (HasPath && Construct(run, &Fixture, true) && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        // Arranged by hand: C at A's bottom, Notes maximized, B's tab selected after A's.
        uint64_t A = WindowOfType(&Fixture, TYPE_A);
        uint64_t B = WindowOfType(&Fixture, TYPE_B);
        uint64_t C = Open(run, &Fixture, TYPE_C);
        Dock(run, &Fixture, C, A, UIDockZone_Bottom);
        Dock(run, &Fixture, B, A, UIDockZone_Center);
        uint64_t Notes = WindowOfType(&Fixture, TYPE_NOTES);
        uint64_t Second = Open(run, &Fixture, TYPE_NOTES); // A second one, floating minimized.
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.UI.Screen, Second));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, Notes, (Rectangle){ .x = 33.0f, .y = 44.0f, .width = 222.0f, .height = 111.0f }));
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.UI.Screen, Notes));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK_SUCCESS(run, UIWorkspace_SaveLayouts(&Fixture.UI.Screen, Fixture.WorkspaceId));
        *Saved = *UILayouts_Get(&Layouts, 0U);
        NotesBox = FloatingBox(run, &Fixture, Notes);
        CBox = PixelBox(run, &Fixture, C);
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    // A new start, its windows opened from the file.
    WorkspaceFixture Again;
    UILayouts AgainLayouts;
    if (HasPath && Construct(run, &Again, true) && StartLayouts(run, &Again, &AgainLayouts, Path._data))
    {
        TEST_CHECK(run, UILayouts_GetCount(&AgainLayouts) == 1U);
        TEST_CHECK(run, UILayout_Equals(UILayouts_Get(&AgainLayouts, 0U), Saved));
        TEST_CHECK_SUCCESS(run, UIWorkspace_SaveLayouts(&Again.UI.Screen, Again.WorkspaceId));
        TEST_CHECK(run, UILayout_Equals(UILayouts_Get(&AgainLayouts, 0U), Saved)); // What shows captures as it was saved.
        uint64_t Notes = WindowOfType(&Again, TYPE_NOTES);
        uint64_t C = WindowOfType(&Again, TYPE_C);
        bool IsMaximized = false;
        Error Result = UIWindow_GetState(&Again.UI.Screen, Notes, NULL, &IsMaximized);
        Error_Deconstruct(&Result);
        TEST_CHECK(run, IsMaximized);
        Rectangle Box = FloatingBox(run, &Again, Notes);
        TEST_CHECK(run, TestUI_IsBox(Box, NotesBox.x, NotesBox.y, NotesBox.width, NotesBox.height));
        Rectangle Shown = PixelBox(run, &Again, C);
        TEST_CHECK(run, TestUI_IsBox(Shown, CBox.x, CBox.y, CBox.width, CBox.height));
        size_t MaximizedCount = 0U;
        size_t MinimizedCount = 0U;
        for (size_t Index = 0U; Index < Again.BoxCount; Index++)
        {
            UIWindowTypes* Types = NULL;
            uint32_t TypeIndex = 0U;
            Error TypeResult = UIWindow_GetType(&Again.UI.Screen, Again.Windows[Index], &Types, &TypeIndex);
            bool IsNotes = (TypeResult.Code == ErrorCode_Success) && (TypeIndex == Again.TypeIndices[TYPE_NOTES]);
            Error_Deconstruct(&TypeResult);
            bool IsWindowMinimized = false;
            bool IsWindowMaximized = false;
            Error StateResult = IsNotes ? UIWindow_GetState(&Again.UI.Screen, Again.Windows[Index], &IsWindowMinimized, &IsWindowMaximized)
                : Error_CreateSuccess();
            Error_Deconstruct(&StateResult);
            MaximizedCount += (IsNotes && IsWindowMaximized && !IsWindowMinimized) ? 1U : 0U;
            MinimizedCount += (IsNotes && IsWindowMinimized) ? 1U : 0U;
        }
        TEST_CHECK(run, (MaximizedCount == 1U) && (MinimizedCount == 1U));
        StopLayouts(run, &Again, &AgainLayouts);
    }
    Deconstruct(run, &Again);
    Memory_Free(Saved);
    Memory_Free(Path._data);
}

static void SwitchingLayoutsReusesWindowsOfTheSameTypes(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    if (MakeLayoutsPath(run, "workspace_switch", &Path) && Construct(run, &Fixture, true) && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        uint64_t A = WindowOfType(&Fixture, TYPE_A);
        uint64_t B = WindowOfType(&Fixture, TYPE_B);
        uint64_t Notes = WindowOfType(&Fixture, TYPE_NOTES);
        uint64_t MoreNotes = Open(run, &Fixture, TYPE_NOTES); // Two of a type that opens many times.
        TEST_CHECK_SUCCESS(run, UITextBox_SetText(&Fixture.UI.Screen, GetBox(&Fixture, A), u8"inside A"));
        // A second layout: B undocked, C opened beside A.
        size_t Second = 0U;
        TEST_CHECK_SUCCESS(run, UIWorkspace_NewLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, NULL, &Second));
        TEST_CHECK(run, (Second == 1U) && (strcmp((const char*)UILayouts_Get(&Layouts, 1U)->Name, "Layout") == 0));
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(&Fixture.UI.Screen, Fixture.WorkspaceId, B));
        uint64_t C = Open(run, &Fixture, TYPE_C);
        Dock(run, &Fixture, C, A, UIDockZone_Right);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        // Back to the first: C (which it lacks) closes, B docks again; A is the same window, its text with it.
        TEST_CHECK_SUCCESS(run, UIWorkspace_SwitchLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, 0U));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.UI.Screen, C));
        TEST_CHECK(run, (PlaceOf(&Fixture, A) == UIWorkspacePlace_Docked) && (PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked));
        TEST_CHECK(run, HasText(&Fixture, GetBox(&Fixture, A), "inside A"));
        TEST_CHECK(run, UILayouts_GetActiveIndex(&Layouts) == 0U);
        TEST_CHECK(run, UIScreen_IsWidgetAlive(&Fixture.UI.Screen, Notes) && UIScreen_IsWidgetAlive(&Fixture.UI.Screen, MoreNotes));
        TEST_CHECK(run, (PlaceOf(&Fixture, Notes) == UIWorkspacePlace_Floating) && (PlaceOf(&Fixture, MoreNotes) == UIWorkspacePlace_Floating));
        // And to the second again: C opens anew, B floats.
        TEST_CHECK_SUCCESS(run, UIWorkspace_SwitchLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, 1U));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        uint64_t NewC = WIDGET_ID_INVALID;
        for (size_t Index = 0U; Index < Fixture.BoxCount; Index++)
        {
            UIWindowTypes* Types = NULL;
            uint32_t TypeIndex = 0U;
            Error Result = UIWindow_GetType(&Fixture.UI.Screen, Fixture.Windows[Index], &Types, &TypeIndex);
            NewC = ((Result.Code == ErrorCode_Success) && (TypeIndex == Fixture.TypeIndices[TYPE_C])) ? Fixture.Windows[Index] : NewC;
            Error_Deconstruct(&Result);
        }
        TEST_CHECK(run, (NewC != C) && (PlaceOf(&Fixture, NewC) == UIWorkspacePlace_Docked));
        TEST_CHECK(run, PlaceOf(&Fixture, B) == UIWorkspacePlace_Floating);
        TEST_CHECK(run, HasText(&Fixture, GetBox(&Fixture, A), "inside A"));
        // Renaming, deleting the active one (the one before it shows), refusing to delete the last.
        TEST_CHECK_SUCCESS(run, UIWorkspace_RenameLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, 1U, u8"Paths"));
        TEST_CHECK_ERROR(run, UIWorkspace_RenameLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, 1U, u8"Default"), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIWorkspace_DeleteLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, 1U));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, (UILayouts_GetCount(&Layouts) == 1U) && (PlaceOf(&Fixture, B) == UIWorkspacePlace_Docked));
        TEST_CHECK_ERROR(run, UIWorkspace_DeleteLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, 0U), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIWorkspace_SwitchLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, 4U), ErrorCode_ArgumentOutOfRange);
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    Memory_Free(Path._data);
}

static void TheSwitcherSwitchesAtTheNextUpdate(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    if (MakeLayoutsPath(run, "workspace_switcher", &Path) && Construct(run, &Fixture, true) && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        uint64_t StatusBar = WIDGET_ID_INVALID;
        uint64_t Right = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWorkspace_GetStatusBarId(&Fixture.UI.Screen, Fixture.WorkspaceId, &StatusBar));
        TEST_CHECK_SUCCESS(run, UIStatusBar_GetSection(&Fixture.UI.Screen, StatusBar, UIStatusBarSection_Right, &Right));
        uint64_t Switcher = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWorkspace_CreateLayoutSwitcher(&Fixture.UI.Kit, &Fixture.UI.Screen, Right, Fixture.WorkspaceId, &Switcher));
        TEST_CHECK_SUCCESS(run, UIWorkspace_NewLayout(&Fixture.UI.Screen, Fixture.WorkspaceId, u8"Second", NULL));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        size_t Count = 0U;
        bool HasSelection = false;
        size_t Selected = 0U;
        TEST_CHECK_SUCCESS(run, UIChoice_GetCount(&Fixture.UI.Screen, Switcher, &Count));
        TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(&Fixture.UI.Screen, Switcher, &HasSelection, &Selected));
        TEST_CHECK(run, (Count == 2U) && HasSelection && (Selected == 1U));
        // Choosing the first: Ctrl and the wheel step it, a whole change.
        uint64_t A = WindowOfType(&Fixture, TYPE_A);
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(&Fixture.UI.Screen, Fixture.WorkspaceId, A));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Vector2 At = Middle(PixelBox(run, &Fixture, Switcher));
        TestUI_MovePointer(&Fixture.UI, At.x, At.y);
        TestUI_Press(&Fixture.UI, GameKey_LeftControl);
        TestUI_Tick(run, &Fixture.UI);
        TestUI_Wheel(&Fixture.UI, 1.0f);
        TestUI_Ticks(run, &Fixture.UI, 3U);
        TestUI_Release(&Fixture.UI, GameKey_LeftControl);
        TestUI_Tick(run, &Fixture.UI);
        TEST_CHECK(run, UILayouts_GetActiveIndex(&Layouts) == 0U);
        TEST_CHECK(run, PlaceOf(&Fixture, A) == UIWorkspacePlace_Docked);
        TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(&Fixture.UI.Screen, Switcher, &HasSelection, &Selected));
        TEST_CHECK(run, HasSelection && (Selected == 0U));
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    Memory_Free(Path._data);
}

static void AWindowTypeGoneAtTheNextStartIsLeftOut(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    bool HasPath = MakeLayoutsPath(run, "workspace_type_gone", &Path);
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    if (HasPath && Construct(run, &Fixture, true) && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    WorkspaceFixture Again;
    UILayouts AgainLayouts;
    if (HasPath && Construct(run, &Again, true))
    {
        TEST_CHECK_SUCCESS(run, UIWindowTypes_Withdraw(&Again.Types, Again.TypeIndices[TYPE_B]));
        if (StartLayouts(run, &Again, &AgainLayouts, Path._data))
        {
            // B's area went with it: A fills the dock.
            uint64_t A = WindowOfType(&Again, TYPE_A);
            TEST_CHECK(run, (WindowOfType(&Again, TYPE_B) == WIDGET_ID_INVALID) && (PlaceOf(&Again, A) == UIWorkspacePlace_Docked));
            TEST_CHECK(run, UIDockTree_GetLeafCount(Tree(&Again)) == 1U);
            TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Again, A), 0.0f, DOCK_TOP + STRIP, 1000.0f, DOCK_HEIGHT - STRIP));
            StopLayouts(run, &Again, &AgainLayouts);
        }
    }
    Deconstruct(run, &Again);
    Memory_Free(Path._data);
}

static void ALayoutSavedLargeLoadsOnASmallerSurface(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    bool HasPath = MakeLayoutsPath(run, "workspace_smaller", &Path);
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    Rectangle Saved = { 0 };
    Rectangle SavedShown = { 0 };
    if (HasPath && Construct(run, &Fixture, true) && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        uint64_t Notes = WindowOfType(&Fixture, TYPE_NOTES);
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.UI.Screen, Notes, (Rectangle){ .x = 700.0f, .y = 300.0f, .width = 250.0f, .height = 150.0f }));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        Saved = FloatingBox(run, &Fixture, Notes);
        SavedShown = PixelBox(run, &Fixture, Notes);
        TEST_CHECK(run, TestUI_IsBox(SavedShown, 700.0f, 300.0f, 250.0f, 150.0f));
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    WorkspaceFixture Again;
    UILayouts AgainLayouts;
    if (HasPath && Construct(run, &Again, true))
    {
        TestUI_SetSurface(run, &Again.UI, 640.0f, 360.0f);
        TestUI_Ticks(run, &Again.UI, 2U);
        if (StartLayouts(run, &Again, &AgainLayouts, Path._data))
        {
            uint64_t Notes = WindowOfType(&Again, TYPE_NOTES);
            Rectangle Box = FloatingBox(run, &Again, Notes);
            TEST_CHECK(run, TestUI_IsBox(Box, Saved.x, Saved.y, Saved.width, Saved.height)); // Kept as it was.
            Rectangle Shown = PixelBox(run, &Again, Notes);
            TEST_CHECK(run, (Shown.y + TITLE_HEIGHT <= 360.0f - STATUS_BAR_HEIGHT) && (Shown.x < 640.0f)); // Shown where it can be reached.
            // The surface grows back: it stands where it was saved.
            TestUI_SetSurface(run, &Again.UI, 1000.0f, 500.0f);
            TestUI_Ticks(run, &Again.UI, 2U);
            TEST_CHECK(run, TestUI_IsBox(PixelBox(run, &Again, Notes), SavedShown.x, SavedShown.y, SavedShown.width, SavedShown.height));
            StopLayouts(run, &Again, &AgainLayouts);
        }
    }
    Deconstruct(run, &Again);
    Memory_Free(Path._data);
}

static void ResettingBuildsTheDefaultAgain(TestRun* run)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 256U, sizeof(unsigned char));
    WorkspaceFixture Fixture;
    UILayouts Layouts;
    if (MakeLayoutsPath(run, "workspace_reset", &Path) && Construct(run, &Fixture, true) && StartLayouts(run, &Fixture, &Layouts, Path._data))
    {
        uint64_t A = WindowOfType(&Fixture, TYPE_A);
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(&Fixture.UI.Screen, Fixture.WorkspaceId, A));
        uint64_t C = Open(run, &Fixture, TYPE_C);
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, CountKept(&Fixture) == 4U);
        TEST_CHECK_SUCCESS(run, UIWorkspace_ResetLayout(&Fixture.UI.Screen, Fixture.WorkspaceId));
        TestUI_Ticks(run, &Fixture.UI, 2U);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.UI.Screen, C) && !UIScreen_IsWidgetAlive(&Fixture.UI.Screen, A));
        TEST_CHECK(run, CountKept(&Fixture) == 3U);
        TEST_CHECK(run, UIDockTree_GetLeafCount(Tree(&Fixture)) == 2U);
        StopLayouts(run, &Fixture, &Layouts);
    }
    Deconstruct(run, &Fixture);
    Memory_Free(Path._data);
}


// Fields.
static const TestCase Cases[] =
{
    { "it_covers_the_surface_with_its_bars", ItCoversTheSurfaceWithItsBars },
    { "windows_dock_as_tabs_and_splits", WindowsDockAsTabsAndSplits },
    { "a_press_on_a_tab_selects_it_and_focuses_its_window", APressOnATabSelectsItAndFocusesItsWindow },
    { "windows_in_tabs_not_selected_get_no_updates", WindowsInTabsNotSelectedGetNoUpdates },
    { "closing_tabs_selects_the_next_and_collapses_empty_areas", ClosingTabsSelectsTheNextAndCollapsesEmptyAreas },
    { "a_tab_dragged_out_carries_its_window_on_to_a_drop", ATabDraggedOutCarriesItsWindowOnToADrop },
    { "a_tab_torn_out_onto_a_guide_in_one_move_shows_it_lit", ATabTornOutOntoAGuideInOneMoveShowsItLit },
    { "escape_puts_a_torn_tab_back", EscapePutsATornTabBack },
    { "escape_keeps_a_dock_changed_meanwhile", EscapeKeepsADockChangedMeanwhile },
    { "a_tear_out_let_go_in_one_tick_docks_nothing", ATearOutLetGoInOneTickDocksNothing },
    { "the_tab_of_a_window_just_closed_does_nothing", TheTabOfAWindowJustClosedDoesNothing },
    { "the_wheel_steps_through_tabs_a_strip_cannot_show", TheWheelStepsThroughTabsAStripCannotShow },
    { "docked_beside_a_window_of_its_own_area_it_stands_after_it", DockedBesideAWindowOfItsOwnAreaItStandsAfterIt },
    { "a_minimized_window_keeps_that_it_was_maximized", AMinimizedWindowKeepsThatItWasMaximized },
    { "resetting_floats_what_stays_and_builds_the_default_whole", ResettingFloatsWhatStaysAndBuildsTheDefaultWhole },
    { "a_tab_tears_out_only_past_the_undock_distance", ATabTearsOutOnlyPastTheUndockDistance },
    { "a_double_click_on_a_tab_dragged_tears_nothing_out", ADoubleClickOnATabDraggedTearsNothingOut },
    { "a_press_on_a_tab_leaves_the_typing_in_its_window_going", APressOnATabLeavesTheTypingInItsWindowGoing },
    { "a_narrow_strip_takes_the_room_from_the_selected_name", ANarrowStripTakesTheRoomFromTheSelectedName },
    { "a_full_dock_refuses_a_new_area_and_stays_as_it_was", AFullDockRefusesANewAreaAndStaysAsItWas },
    { "a_splitters_pointer_shows_its_way", ASplittersPointerShowsItsWay },
    { "a_tab_dragged_along_its_strip_reorders", ATabDraggedAlongItsStripReorders },
    { "a_floating_window_drops_on_guides_and_floats_elsewhere", AFloatingWindowDropsOnGuidesAndFloatsElsewhere },
    { "an_empty_dock_takes_a_window_on_its_centre_guide", AnEmptyDockTakesAWindowOnItsCentreGuide },
    { "a_window_gone_or_changed_mid_drag_leaves_nothing_held", AWindowGoneOrChangedMidDragLeavesNothingHeld },
    { "what_a_window_holds_stays_through_docking_and_undocking", WhatAWindowHoldsStaysThroughDockingAndUndocking },
    { "maximized_from_the_dock_it_goes_back_to_its_tab", MaximizedFromTheDockItGoesBackToItsTab },
    { "splitters_drag_within_what_the_windows_need", SplittersDragWithinWhatTheWindowsNeed },
    { "a_tabs_context_menu_undocks", ATabsContextMenuUndocks },
    { "its_tabs_show_what_their_windows_are", ItsTabsShowWhatTheirWindowsAre },
    { "hidden_it_hides_its_floating_windows_and_takes_no_drops", HiddenItHidesItsFloatingWindowsAndTakesNoDrops },
    { "a_single_type_opened_again_comes_back", ASingleTypeOpenedAgainComesBack },
    { "with_no_layouts_it_builds_the_default_and_saves_it_once_settled", WithNoLayoutsItBuildsTheDefaultAndSavesItOnceSettled },
    { "a_layout_saved_comes_back_after_a_restart", ALayoutSavedComesBackAfterARestart },
    { "switching_layouts_reuses_windows_of_the_same_types", SwitchingLayoutsReusesWindowsOfTheSameTypes },
    { "the_switcher_switches_at_the_next_update", TheSwitcherSwitchesAtTheNextUpdate },
    { "a_window_type_gone_at_the_next_start_is_left_out", AWindowTypeGoneAtTheNextStartIsLeftOut },
    { "a_layout_saved_large_loads_on_a_smaller_surface", ALayoutSavedLargeLoadsOnASmallerSurface },
    { "resetting_builds_the_default_again", ResettingBuildsTheDefaultAgain },
};


// Public functions.
void UIWorkspaceTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIWorkspace", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
