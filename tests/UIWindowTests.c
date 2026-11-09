#include "TestSuites.h"
#include "TestUI.h"
#include "Renderer.h"
#include "UIButton.h"
#include "UIDropdown.h"
#include "UIElement.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UILabel.h"
#include "UIScrollable.h"
#include "UITextBox.h"
#include "UIWidgetFactory.h"
#include "UIWindow.h"
#include "WorldScreenHost.h"
#include "WorldScreens.h"
#include "wr/WRUserData.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The fixture's look: windows with small round numbers, their content's scroll bars 10 wide, text boxes and buttons. */
#define WINDOW_THEME \
    "{\"name\": \"windows\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"label\": {\"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"none\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"text_box\": {\"padding\": [4, 4], \"min_size\": [100, 24], \"caret_width\": 1, \"mask\": \"*\", \"box\": {\"kind\": \"flat\"}," \
    "\"read_only_box\": {\"kind\": \"flat\"}, \"selection\": {\"kind\": \"flat\"}, \"inactive_selection\": {\"kind\": \"flat\"}, " \
    "\"caret\": {\"kind\": \"flat\"}, \"overwrite_caret\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}, \"placeholder\": {\"normal\": \"#808080\"}}," \
    "\"dropdown\": {\"padding\": [8, 4], \"min_size\": [48, 24], \"arrow_size\": 8, \"background\": {\"kind\": \"flat\"}," \
    "\"text\": {\"normal\": \"#e0e0e0\"}, \"arrow\": {\"normal\": \"#ffffff\"}, \"list\": {\"kind\": \"flat\"}, \"list_padding\": [1, 1]," \
    "\"row_height\": 20, \"row\": {\"kind\": \"flat\"}, \"row_text\": {\"normal\": \"#e0e0e0\"}, \"scroll_arrow_height\": 10}," \
    "\"window\": {\"title_height\": 20, \"title_padding\": 4, \"icon_spacing\": 4, \"button_size\": [16, 14], \"button_spacing\": 2, \"border\": 2," \
    "\"resize_margin\": 4, \"min_size\": [100, 60], \"minimized_width\": 150, \"content_padding\": [4, 4], \"content_spacing\": 2," \
    "\"frame\": {\"kind\": \"flat\"}, \"title_bar\": {\"kind\": \"flat\"}, \"title_text\": {\"normal\": \"#ffffff\"}, " \
    "\"content_background\": {\"kind\": \"flat\"}, \"resize_edge\": {\"kind\": \"flat\"}}}}"
/** The theme's numbers the tests reason with, in UI pixels. */
#define TITLE_HEIGHT 20.0f
#define MIN_WIDTH 100.0f
#define MIN_HEIGHT 60.0f
#define MINIMIZED_WIDTH 150.0f
#define BORDER 2.0f
#define BAR 10.0f
/** What the three caption buttons and the padding at their right take: 4 + 3 x 16 + 2 x 2. */
#define BUTTONS_WIDTH 56.0f
/** The first window's box, and a place on its title bar that is no button and no edge. */
#define WINDOW_X 100.0f
#define WINDOW_Y 50.0f
#define WINDOW_WIDTH 300.0f
#define WINDOW_HEIGHT 200.0f
#define TITLE_X 150.0f
#define TITLE_Y 62.0f


// Types.
/* What a close query saw and says. */
typedef struct CloseProbeStruct
{
    size_t Asked;
    bool IsAllowed;
} CloseProbe;

/* The entered element's changes, counted. */
typedef struct EnteringProbeStruct
{
    size_t Changes;
} EnteringProbe;

/* The cues a window posted. */
typedef struct CueProbeStruct
{
    size_t Opens;
    size_t Closes;
    uint64_t WindowId;
} CueProbe;


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, WINDOW_THEME);
}

static Rectangle Box(float x, float y, float width, float height)
{
    return (Rectangle){ .x = x, .y = y, .width = width, .height = height };
}

static bool IsSameBox(Rectangle a, Rectangle b)
{
    return TestUI_IsBox(a, b.x, b.y, b.width, b.height);
}

static uint64_t MakeWindowWith(TestRun* run, TestUIFixture* fixture, UIWindowOptions* options, Rectangle box, uint64_t* outContentId)
{
    options->HasPosition = true;
    options->Position = (Vector2){ .x = box.x, .y = box.y };
    options->Size = (Vector2){ .x = box.width, .y = box.height };
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateWindow(&fixture->Kit, &fixture->Screen, options, &Id, outContentId));
    TestUI_Ticks(run, fixture, 2U);
    return Id;
}

static uint64_t MakeWindow(TestRun* run, TestUIFixture* fixture, Rectangle box, uint64_t* outContentId)
{
    UIWindowOptions Options = UIWindowOptions_Create(u8"Window");
    return MakeWindowWith(run, fixture, &Options, box, outContentId);
}

static Rectangle GetShown(TestRun* run, TestUIFixture* fixture, uint64_t windowId)
{
    Rectangle Shown = Box(0.0f, 0.0f, 0.0f, 0.0f);
    TEST_CHECK_SUCCESS(run, UIWindow_GetShownBox(&fixture->Screen, windowId, &Shown));
    return Shown;
}

static Rectangle GetWanted(TestRun* run, TestUIFixture* fixture, uint64_t windowId)
{
    Rectangle Wanted = Box(0.0f, 0.0f, 0.0f, 0.0f);
    TEST_CHECK_SUCCESS(run, UIWindow_GetBox(&fixture->Screen, windowId, &Wanted));
    return Wanted;
}

static void PressAt(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
    TestUI_Press(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
}

static void ReleaseAt(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
    TestUI_Release(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
}

static void Drag(TestRun* run, TestUIFixture* fixture, float fromX, float fromY, float toX, float toY)
{
    PressAt(run, fixture, fromX, fromY);
    ReleaseAt(run, fixture, toX, toY);
    TestUI_Tick(run, fixture);
}

static uint64_t AddTextBox(TestRun* run, TestUIFixture* fixture, uint64_t contentId)
{
    UITextBoxOptions Options = UITextBoxOptions_Create();
    Options.Element.Hint = UIStackHint_Fixed(24.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateTextBox(&fixture->Kit, &fixture->Screen, contentId, &Options, &Id));
    return Id;
}

static void TypeText(TestRun* run, TestUIFixture* fixture, const char* text)
{
    for (const char* Cursor = text; *Cursor != 0; Cursor++)
    {
        fixture->Sample.TextCodepoints[0] = (uint32_t)(unsigned char)*Cursor;
        fixture->Sample.TextCodepointCount = 1U;
        TestUI_Tick(run, fixture);
    }
}

static bool HasText(TestUIFixture* fixture, uint64_t textBoxId, const char* expected)
{
    const unsigned char* Text = NULL;
    Error Result = UITextBox_GetText(&fixture->Screen, textBoxId, &Text, NULL);
    bool IsSame = (Result.Code == ErrorCode_Success) && (Text != NULL) && (strcmp((const char*)Text, expected) == 0);
    Error_Deconstruct(&Result);
    return IsSame;
}

/* The id of the widget the pointer would hit at a place, or WIDGET_ID_INVALID. */
static uint64_t TopmostAt(TestUIFixture* fixture, float x, float y)
{
    UIViewport Viewport = UIScreen_GetViewport(&fixture->Screen);
    Widget* Found = NULL;
    Vector2 Point = { .x = x / Viewport.SizePixels.x, .y = y / Viewport.SizePixels.y };
    return UIScreen_TryGetTopmostAt(&fixture->Screen, Point, &Found) ? Widget_GetId(Found) : WIDGET_ID_INVALID;
}

/* Whether a widget lies in a window (or is the window). */
static bool IsInWindow(TestUIFixture* fixture, uint64_t widgetId, uint64_t windowId)
{
    Widget* Current = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, widgetId, &Current))
    {
        return false;
    }
    for (size_t Step = 0U; (Step < WIDGET_MAX_TREE_DEPTH) && (Current != NULL); Step++)
    {
        if (Widget_GetId(Current) == windowId)
        {
            return true;
        }
        Current = Widget_GetParent(Current);
    }
    return false;
}

static UIScreenLayer GetLayer(TestUIFixture* fixture, uint64_t windowId)
{
    Widget* Window = NULL;
    return UIScreen_TryGetWidgetById(&fixture->Screen, windowId, &Window) ? UIScreen_GetRootLayer(&fixture->Screen, Window)
        : UIScreenLayer_Count;
}

static bool IsMaximized(TestUIFixture* fixture, uint64_t windowId)
{
    bool Maximized = false;
    Error Result = UIWindow_GetState(&fixture->Screen, windowId, NULL, &Maximized);
    Error_Deconstruct(&Result);
    return Maximized;
}

static bool IsActive(TestUIFixture* fixture, uint64_t windowId)
{
    bool Active = false;
    Error Result = UIWindow_IsActive(&fixture->Screen, windowId, &Active);
    Error_Deconstruct(&Result);
    return Active;
}

static Rectangle GetButtonBox(TestRun* run, TestUIFixture* fixture, uint64_t windowId, UIWindowButton button)
{
    uint64_t ButtonId = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIWindow_GetButtonId(&fixture->Screen, windowId, button, &ButtonId));
    return TestUI_GetPixelBox(run, fixture, ButtonId);
}

static void ClickButton(TestRun* run, TestUIFixture* fixture, uint64_t windowId, UIWindowButton button)
{
    Rectangle Button = GetButtonBox(run, fixture, windowId, button);
    TestUI_Click(run, fixture, Button.x + (Button.width * 0.5f), Button.y + (Button.height * 0.5f));
    TestUI_Tick(run, fixture);
}

static bool OnClose(UIScreen* screen, uint64_t windowId, const UserData* userData)
{
    (void)screen;
    (void)windowId;
    CloseProbe* Probe = UserData_GetPointer(userData);
    Probe->Asked++;
    return Probe->IsAllowed;
}

static Error OnCue(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    CueProbe* Probe = UserData_GetPointer(userData);
    if (event->WidgetId != Probe->WindowId)
    {
        return Error_CreateSuccess();
    }
    Probe->Opens += (event->WidgetEventId == (int32_t)UIElementCue_Open) ? 1U : 0U;
    Probe->Closes += (event->WidgetEventId == (int32_t)UIElementCue_Close) ? 1U : 0U;
    return Error_CreateSuccess();
}

/* A close query that closes the window itself, as one that asked and got its answer at once might. */
static bool OnCloseItself(UIScreen* screen, uint64_t windowId, const UserData* userData)
{
    CloseProbe* Probe = UserData_GetPointer(userData);
    Probe->Asked++;
    Error Result = UIWindow_CloseNow(screen, windowId);
    bool IsClosed = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    return IsClosed && Probe->IsAllowed;
}

static Error OnEnteredChanged(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    (void)event;
    EnteringProbe* Probe = UserData_GetPointer(userData);
    Probe->Changes++;
    return Error_CreateSuccess();
}

/* Whether a shown box keeps its title bar reachable on a workspace of a size: its whole height on it, and of its draggable part (the
 * title bar less its buttons) the reachable width, or all of it, or the workspace's width. */
static bool IsReachable(Rectangle shown, float workspaceWidth, float workspaceHeight, float titleHeight, float buttonsWidth, float reachable)
{
    float Draggable = shown.width - buttonsWidth;
    float Needed = fminf(reachable, fminf(Draggable, workspaceWidth));
    float Overlap = fminf(shown.x + Draggable, workspaceWidth) - fmaxf(shown.x, 0.0f);
    bool IsTitleOn = (shown.y >= -0.01f) && ((shown.y + titleHeight) <= (workspaceHeight + 0.01f));
    return IsTitleOn && (Overlap >= (Needed - 0.01f));
}

/* Lets the double-click time pass, so the next click counts as a first. */
static void WaitPastDoubleClick(TestRun* run, TestUIFixture* fixture)
{
    TestUI_Ticks(run, fixture, (size_t)(UI_SCREEN_MULTI_CLICK_SECONDS / TEST_UI_TICK_SECONDS) + 2U);
}

/* Replaces the first piece of a theme's text with another, in place. */
static bool ReplaceInTheme(char* text, size_t capacity, const char* from, const char* to)
{
    char* At = strstr(text, from);
    size_t Length = strlen(text);
    size_t FromLength = strlen(from);
    size_t ToLength = strlen(to);
    if ((At == NULL) || ((Length - FromLength + ToLength + 1U) > capacity))
    {
        return false;
    }
    memmove(At + ToLength, At + FromLength, Length - (size_t)(At - text) - FromLength + 1U);
    memcpy(At, to, ToLength);
    return true;
}

/* The fixture with the windows' theme, two of its pieces replaced (a NULL piece is left alone). */
static bool ConstructWith(TestRun* run, TestUIFixture* fixture, const char* from, const char* to, const char* otherFrom, const char* otherTo)
{
    char Theme[4096];
    snprintf(Theme, sizeof(Theme), "%s", WINDOW_THEME);
    bool IsMade = ((from == NULL) || ReplaceInTheme(Theme, sizeof(Theme), from, to))
        && ((otherFrom == NULL) || ReplaceInTheme(Theme, sizeof(Theme), otherFrom, otherTo));
    return TEST_CHECK(run, IsMade) && TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, Theme);
}

/* The window's capability, read-only, or NULL. Found again after anything that runs hooks. */
static const UIWindowCapability* GetWindow(TestUIFixture* fixture, uint64_t windowId)
{
    Widget* Window = NULL;
    return UIScreen_TryGetWidgetById(&fixture->Screen, windowId, &Window)
        ? Widget_GetCapability(Window, UIKit_GetCapabilityId(&fixture->Kit, UIElementCapability_Window)) : NULL;
}

static bool IsShown(TestUIFixture* fixture, uint64_t widgetId)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(&fixture->Screen, widgetId, &Found) && Widget_IsRendered(Found);
}

static uint64_t GetButton(TestRun* run, TestUIFixture* fixture, uint64_t windowId, UIWindowButton button)
{
    uint64_t ButtonId = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIWindow_GetButtonId(&fixture->Screen, windowId, button, &ButtonId));
    return ButtonId;
}

/* Whether a caption button shows a letter. */
static bool HasLetter(TestRun* run, TestUIFixture* fixture, uint64_t windowId, UIWindowButton button, uint32_t letter)
{
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, GetButton(run, fixture, windowId, button), &Found))
    {
        return false;
    }
    const UIButtonCapability* Button = Widget_GetCapability(Found, UIKit_GetCapabilityId(&fixture->Kit, UIElementCapability_Button));
    UIElementIcon Wanted = UIElementIcon_FromLetter(letter);
    return (Button != NULL) && UIElementIcon_Equals(&Button->_icon, &Wanted);
}

/* Whether a caption button is of a theme variant. */
static bool HasVariant(TestRun* run, TestUIFixture* fixture, uint64_t windowId, UIWindowButton button, const char* variant)
{
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, GetButton(run, fixture, windowId, button), &Found))
    {
        return false;
    }
    const UIElementLook* Look = Widget_GetCapability(Found, UIKit_GetCapabilityId(&fixture->Kit, UIElementCapability_Look));
    return (Look != NULL) && (strcmp((const char*)Look->_variant, variant) == 0);
}

/* An edge's band in the window's box, and the state its highlight shows. */
static Rectangle GetEdgeBand(TestUIFixture* fixture, uint64_t windowId, UIWindowEdge edge, UIElementState* outState)
{
    Rectangle Band = Box(-1.0f, -1.0f, -1.0f, -1.0f);
    UIElementState State = UIElementState_Disabled;
    Widget* Window = NULL;
    if (UIScreen_TryGetWidgetById(&fixture->Screen, windowId, &Window))
    {
        State = UIWindow_GetEdgeState(Window, edge, &Band);
    }
    if (outState != NULL)
    {
        *outState = State;
    }
    return Band;
}

/* Whether no edge of a window shows: each an empty box, normal. */
static bool AreEdgesHidden(TestUIFixture* fixture, uint64_t windowId)
{
    bool AreHidden = true;
    for (int32_t Edge = 0; Edge < (int32_t)UIWindowEdge_Count; Edge++)
    {
        UIElementState State = UIElementState_Disabled;
        Rectangle Band = GetEdgeBand(fixture, windowId, (UIWindowEdge)Edge, &State);
        AreHidden = AreHidden && IsSameBox(Band, Box(0.0f, 0.0f, 0.0f, 0.0f)) && (State == UIElementState_Normal);
    }
    return AreHidden;
}

/* The pointer's shape with the pointer at a place. */
static UICursorShape CursorAt(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
    return UIScreen_GetCursorShape(&fixture->Screen);
}

/* A drag with the right button. */
static void RightDrag(TestRun* run, TestUIFixture* fixture, float fromX, float fromY, float toX, float toY)
{
    TestUI_MovePointer(fixture, fromX, fromY);
    TestUI_Tick(run, fixture);
    TestUI_Press(fixture, GameKey_MouseRight);
    TestUI_Tick(run, fixture);
    TestUI_MovePointer(fixture, toX, toY);
    TestUI_Tick(run, fixture);
    TestUI_Release(fixture, GameKey_MouseRight);
    TestUI_Ticks(run, fixture, 2U);
}

/* Moves a window into a holder that leaves its children's boxes alone (as a dock's area would place them), at a box. */
static void PutInHolder(TestRun* run, TestUIFixture* fixture, uint64_t windowId, Rectangle box)
{
    static const WidgetVTable HolderVTable = { .Render = NULL };
    WidgetTypeDescriptor Descriptor = { .Name = u8"TestWindowHolder", .StructSize = sizeof(Widget), .VTable = &HolderVTable,
        .IsFocusable = false };
    uint64_t HolderType = WIDGET_TYPE_ID_INVALID;
    Widget* Holder = NULL;
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&fixture->Widgets, &Descriptor, &HolderType));
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&fixture->Widgets, &fixture->Screen, HolderType, NULL, &Holder));
    UIPlacement Everywhere = UIPlacement_FromTopLeft(0.0f, 0.0f, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT);
    TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Holder, &Everywhere));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Holder));
    TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&fixture->Screen, Holder));
    uint64_t HolderId = Widget_GetId(Holder);
    Widget* Window = NULL;
    if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, windowId, &Window) && UIScreen_TryGetWidgetById(&fixture->Screen, HolderId, &Holder)))
    {
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(Window, Holder));
        UIPlacement Placed = UIPlacement_FromTopLeft(box.x, box.y, box.width, box.height);
        TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Window, &Placed));
    }
    TestUI_Ticks(run, fixture, 2U);
}

/* What a window measures at a width, as its parent would measure it. */
static Vector2 MeasureWindow(TestRun* run, TestUIFixture* fixture, uint64_t windowId, float width)
{
    UIViewport Viewport = UIScreen_GetViewport(&fixture->Screen);
    RenderContext Measure;
    RenderContext_CreateMeasuring(&Measure, Viewport.SizePixels, Viewport.TargetAspectRatio, Viewport.TargetRelativePosition);
    UIMeasureContext Sizing = { .Measure = &Measure };
    Vector2 Size = { .x = -1.0f, .y = -1.0f };
    Widget* Window = NULL;
    if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, windowId, &Window)))
    {
        TEST_CHECK_SUCCESS(run, Widget_Measure(Window, &Sizing, width, &Size));
    }
    return Size;
}

/* A label that wraps at the width it gets, with words of 4 letters. */
static uint64_t AddWrappedLabel(TestRun* run, TestUIFixture* fixture, uint64_t contentId)
{
    UILabelOptions Options = UILabelOptions_Create(u8"aaaa bbbb cccc dddd eeee ffff gggg hhhh");
    Options.IsWrapped = true;
    Options.Element.Hint.Align = UIStackAlign_Stretch;
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&fixture->Kit, &fixture->Screen, contentId, &Options, &Id));
    return Id;
}

/* The scroll modes of a window's content area. */
static bool HasModes(TestUIFixture* fixture, uint64_t windowId, UIScrollMode across, UIScrollMode down)
{
    const UIWindowCapability* Window = GetWindow(fixture, windowId);
    Widget* Area = NULL;
    if ((Window == NULL) || !UIScreen_TryGetWidgetById(&fixture->Screen, Window->_areaId, &Area))
    {
        return false;
    }
    const UIScrollableCapability* Scrollable = Widget_GetCapability(Area, UIKit_GetCapabilityId(&fixture->Kit, UIElementCapability_Scrollable));
    return (Scrollable != NULL) && (Scrollable->_modes[UIScrollAxis_Horizontal] == across) && (Scrollable->_modes[UIScrollAxis_Vertical] == down);
}

static void SetFocus(TestRun* run, TestUIFixture* fixture, uint64_t widgetId)
{
    Widget* Found = NULL;
    if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, widgetId, &Found)))
    {
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&fixture->Screen, Found));
    }
}


// Tests: moving and resizing.
static void TheTitleBarMovesIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        TEST_CHECK(run, IsSameBox(TestUI_GetPixelBox(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        TEST_CHECK(run, GetLayer(&Fixture, Id) == UIScreenLayer_Floating);
        Drag(run, &Fixture, TITLE_X, TITLE_Y, TITLE_X + 100.0f, TITLE_Y + 50.0f);
        Rectangle Moved = Box(WINDOW_X + 100.0f, WINDOW_Y + 50.0f, WINDOW_WIDTH, WINDOW_HEIGHT);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Moved) && IsSameBox(GetShown(run, &Fixture, Id), Moved));
        TEST_CHECK(run, IsSameBox(TestUI_GetPixelBox(run, &Fixture, Id), Moved));
        // A drag past the surface's edge (the pointer tracked off it) leaves the title bar on it.
        Fixture.ScreenInput.IsPointerTracked = true;
        Drag(run, &Fixture, TITLE_X + 100.0f, TITLE_Y + 50.0f, 2000.0f, -400.0f);
        Rectangle Shown = GetShown(run, &Fixture, Id);
        TEST_CHECK(run, IsReachable(Shown, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT, TITLE_HEIGHT, BUTTONS_WIDTH, UI_WINDOW_REACHABLE_WIDTH));
        TEST_CHECK(run, TestUI_IsSamePixel(Shown.y, 0.0f) && TestUI_IsSamePixel(Shown.x, TEST_UI_SURFACE_WIDTH - UI_WINDOW_REACHABLE_WIDTH));
        // The box it wants was kept so too: it shows where it was left.
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Shown));
        // Off the left, its buttons and the reachable width of what drags stay on: 48 of its 244.
        Rectangle Start = Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT);
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Start));
        TestUI_Ticks(run, &Fixture, 2U);
        Drag(run, &Fixture, TITLE_X, TITLE_Y, -2000.0f, TITLE_Y);
        Rectangle Left = Box(UI_WINDOW_REACHABLE_WIDTH - (WINDOW_WIDTH - BUTTONS_WIDTH), WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Left) && IsSameBox(GetShown(run, &Fixture, Id), Left));
        // A box set off the left by code shows there too, while the box it wants stays as set.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Box(-1000.0f, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Left)
            && IsSameBox(GetWanted(run, &Fixture, Id), Box(-1000.0f, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        // Kept reachable on a smaller surface, it is dragged from where it shows, not from where it wanted to be.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Box(800.0f, 300.0f, WINDOW_WIDTH, WINDOW_HEIGHT)));
        TestUI_SetSurface(run, &Fixture, 500.0f, 400.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(500.0f - UI_WINDOW_REACHABLE_WIDTH, 300.0f, WINDOW_WIDTH, WINDOW_HEIGHT)));
        Drag(run, &Fixture, 502.0f, 312.0f, 402.0f, 312.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(352.0f, 300.0f, WINDOW_WIDTH, WINDOW_HEIGHT)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A drag moves it in the screen's UI pixels; a window of its own scale keeps its own title bar, buttons and reachable width on. */
static void ItMovesByTheScreensScaleAndKeepsItsOwnReachable(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // At scale 2, a drag of 100 by 50 pixels is one of 50 by 25 UI pixels. Its title bar spans 50 to 90 pixels down.
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        uint64_t Id = MakeWindow(run, &Fixture, Box(50.0f, 25.0f, 200.0f, 100.0f), NULL);
        Drag(run, &Fixture, 150.0f, 62.0f, 250.0f, 112.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(100.0f, 50.0f, 200.0f, 100.0f)));
    }
    TestUI_Deconstruct(run, &Fixture);
    TestUIFixture Scaled;
    if (Construct(run, &Scaled))
    {
        Scaled.ScreenInput.IsPointerTracked = true;
        UIWindowOptions Options = UIWindowOptions_Create(u8"Scaled");
        Options.Element.Scale = 1.5f;
        Rectangle Start = Box(300.0f, 200.0f, 300.0f, 200.0f);
        uint64_t Id = MakeWindowWith(run, &Scaled, &Options, Start, NULL);
        // Off the bottom left: its title bar of 30 at the bottom, 72 of its 216 that drag (300 less 84 of buttons) on.
        Drag(run, &Scaled, 400.0f, 210.0f, -3000.0f, 3000.0f);
        float Draggable = 300.0f - (1.5f * BUTTONS_WIDTH);
        Rectangle Kept = Box((1.5f * UI_WINDOW_REACHABLE_WIDTH) - Draggable, TEST_UI_SURFACE_HEIGHT - (1.5f * TITLE_HEIGHT), 300.0f, 200.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Scaled, Id), Kept) && IsSameBox(GetShown(run, &Scaled, Id), Kept));
        // Minimized, its bar is its own size too.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Scaled.Screen, Id, Start));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Scaled.Screen, Id));
        TestUI_Ticks(run, &Scaled, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Scaled, Id), Box(300.0f, 200.0f, 1.5f * MINIMIZED_WIDTH, 1.5f * TITLE_HEIGHT)));
    }
    TestUI_Deconstruct(run, &Scaled);
}

static void EveryEdgeAndCornerResizesIt(TestRun* run)
{
    static const struct
    {
        UIWindowEdge Edge;
        Vector2 Press;
        Vector2 Delta;
        Rectangle Expected;
        UICursorShape Shape;
    } Cases[] =
    {
        { UIWindowEdge_Left, { 102.0f, 150.0f }, { -30.0f, 0.0f }, { 70.0f, 50.0f, 330.0f, 200.0f }, UICursorShape_ResizeHorizontal },
        { UIWindowEdge_Top, { 250.0f, 52.0f }, { 0.0f, -20.0f }, { 100.0f, 30.0f, 300.0f, 220.0f }, UICursorShape_ResizeVertical },
        { UIWindowEdge_Right, { 398.0f, 150.0f }, { 50.0f, 0.0f }, { 100.0f, 50.0f, 350.0f, 200.0f }, UICursorShape_ResizeHorizontal },
        { UIWindowEdge_Bottom, { 250.0f, 248.0f }, { 0.0f, 40.0f }, { 100.0f, 50.0f, 300.0f, 240.0f }, UICursorShape_ResizeVertical },
        { UIWindowEdge_TopLeft, { 104.0f, 54.0f }, { -10.0f, -10.0f }, { 90.0f, 40.0f, 310.0f, 210.0f }, UICursorShape_ResizeDiagonalDown },
        { UIWindowEdge_TopRight, { 396.0f, 54.0f }, { 20.0f, -10.0f }, { 100.0f, 40.0f, 320.0f, 210.0f }, UICursorShape_ResizeDiagonalUp },
        { UIWindowEdge_BottomLeft, { 104.0f, 246.0f }, { -10.0f, 20.0f }, { 90.0f, 50.0f, 310.0f, 220.0f }, UICursorShape_ResizeDiagonalUp },
        { UIWindowEdge_BottomRight, { 396.0f, 246.0f }, { 20.0f, 20.0f }, { 100.0f, 50.0f, 320.0f, 220.0f }, UICursorShape_ResizeDiagonalDown },
    };
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rectangle Start = Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT);
        uint64_t Id = MakeWindow(run, &Fixture, Start, NULL);
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Start));
            TestUI_Ticks(run, &Fixture, 2U);
            TestUI_MovePointer(&Fixture, Cases[Index].Press.x, Cases[Index].Press.y);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_GetCursorShape(&Fixture.Screen) == Cases[Index].Shape);
            Drag(run, &Fixture, Cases[Index].Press.x, Cases[Index].Press.y, Cases[Index].Press.x + Cases[Index].Delta.x,
                Cases[Index].Press.y + Cases[Index].Delta.y);
            if (!TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Cases[Index].Expected)))
            {
                Rectangle Got = GetWanted(run, &Fixture, Id);
                printf("    edge %d: %.1f %.1f %.1f %.1f\n", (int)Cases[Index].Edge, (double)Got.x, (double)Got.y, (double)Got.width, (double)Got.height);
            }
            TEST_CHECK(run, IsSameBox(TestUI_GetPixelBox(run, &Fixture, Id), Cases[Index].Expected));
        }
        // The bands: 4 wide along the frame, the corners 8 square.
        static const Rectangle Bands[UIWindowEdge_Count] =
        {
            { 0.0f, 8.0f, 4.0f, 184.0f }, { 8.0f, 0.0f, 284.0f, 4.0f }, { 296.0f, 8.0f, 4.0f, 184.0f }, { 8.0f, 196.0f, 284.0f, 4.0f },
            { 0.0f, 0.0f, 8.0f, 8.0f }, { 292.0f, 0.0f, 8.0f, 8.0f }, { 0.0f, 192.0f, 8.0f, 8.0f }, { 292.0f, 192.0f, 8.0f, 8.0f }
        };
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Start));
        TestUI_Ticks(run, &Fixture, 2U);
        for (int32_t Edge = 0; Edge < (int32_t)UIWindowEdge_Count; Edge++)
        {
            TEST_CHECK(run, IsSameBox(GetEdgeBand(&Fixture, Id, (UIWindowEdge)Edge, NULL), Bands[Edge]));
        }
        // Its highlight: hovered under the pointer, pressed while dragged, normal again once let go and left.
        UIElementState State = UIElementState_Disabled;
        TEST_CHECK(run, CursorAt(run, &Fixture, 398.0f, 150.0f) == UICursorShape_ResizeHorizontal);
        GetEdgeBand(&Fixture, Id, UIWindowEdge_Right, &State);
        TEST_CHECK(run, State == UIElementState_Hovered);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 420.0f, 150.0f);
        TestUI_Tick(run, &Fixture);
        GetEdgeBand(&Fixture, Id, UIWindowEdge_Right, &State);
        TEST_CHECK(run, State == UIElementState_Pressed);
        ReleaseAt(run, &Fixture, 420.0f, 150.0f);
        TestUI_MovePointer(&Fixture, 800.0f, 450.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        for (int32_t Edge = 0; Edge < (int32_t)UIWindowEdge_Count; Edge++)
        {
            GetEdgeBand(&Fixture, Id, (UIWindowEdge)Edge, &State);
            TEST_CHECK(run, State == UIElementState_Normal);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A short window's corners take a quarter of its height at most: a stretch of edge stays between them. */
static void AShortWindowKeepsSomeEdgeBetweenItsCorners(TestRun* run)
{
    TestUIFixture Fixture;
    if (ConstructWith(run, &Fixture, "\"min_size\": [100, 60]", "\"min_size\": [20, 20]", NULL, NULL))
    {
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, 200.0f, 24.0f), NULL);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, 200.0f, 24.0f)));
        TEST_CHECK(run, IsSameBox(GetEdgeBand(&Fixture, Id, UIWindowEdge_Left, NULL), Box(0.0f, 6.0f, 4.0f, 12.0f)));
        TEST_CHECK(run, IsSameBox(GetEdgeBand(&Fixture, Id, UIWindowEdge_TopLeft, NULL), Box(0.0f, 0.0f, 6.0f, 6.0f)));
        TEST_CHECK(run, IsSameBox(GetEdgeBand(&Fixture, Id, UIWindowEdge_BottomRight, NULL), Box(194.0f, 18.0f, 6.0f, 6.0f)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Edges show only while it can be resized: not when it is not resizable, minimized or maximized. */
static void ItsEdgesShowOnlyWhileItCanBeResized(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        TEST_CHECK(run, !AreEdgesHidden(&Fixture, Id));
        TEST_CHECK_SUCCESS(run, UIWindow_SetIsResizable(&Fixture.Screen, Id, false));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, AreEdgesHidden(&Fixture, Id) && (CursorAt(run, &Fixture, 398.0f, 150.0f) == UICursorShape_Arrow));
        TEST_CHECK_SUCCESS(run, UIWindow_SetIsResizable(&Fixture.Screen, Id, true));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !AreEdgesHidden(&Fixture, Id) && (CursorAt(run, &Fixture, 398.0f, 150.0f) == UICursorShape_ResizeHorizontal));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, AreEdgesHidden(&Fixture, Id) && (CursorAt(run, &Fixture, WINDOW_X + 1.0f, WINDOW_Y + 10.0f) == UICursorShape_Arrow));
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, AreEdgesHidden(&Fixture, Id) && (CursorAt(run, &Fixture, 1.0f, 250.0f) == UICursorShape_Arrow));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ResizingStopsAtItsLeastSizeAndTheWorkspace(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Fixture.ScreenInput.IsPointerTracked = true;
        Rectangle Start = Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT);
        uint64_t Id = MakeWindow(run, &Fixture, Start, NULL);
        Drag(run, &Fixture, 398.0f, 150.0f, 398.0f - 500.0f, 150.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, MIN_WIDTH, WINDOW_HEIGHT)));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Start));
        TestUI_Ticks(run, &Fixture, 2U);
        Drag(run, &Fixture, 102.0f, 150.0f, 102.0f - 500.0f, 150.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(0.0f, WINDOW_Y, WINDOW_X + WINDOW_WIDTH, WINDOW_HEIGHT)));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Start));
        TestUI_Ticks(run, &Fixture, 2U);
        Drag(run, &Fixture, 250.0f, 248.0f, 250.0f, 248.0f + 1000.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, TEST_UI_SURFACE_HEIGHT - WINDOW_Y)));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Start));
        TestUI_Ticks(run, &Fixture, 2U);
        Drag(run, &Fixture, 250.0f, 52.0f, 250.0f, 52.0f + 1000.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y + WINDOW_HEIGHT - MIN_HEIGHT, WINDOW_WIDTH, MIN_HEIGHT)));
        // Mostly off the left, its right edge pulled further left: what is left of it stays reachable (44 that drag, all on).
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Box(-150.0f, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        TestUI_Ticks(run, &Fixture, 2U);
        Drag(run, &Fixture, 148.0f, 150.0f, -1000.0f, 150.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(0.0f, WINDOW_Y, MIN_WIDTH, WINDOW_HEIGHT)));
        // A window not resizable shows no edges: a press there moves nothing.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Start));
        TEST_CHECK_SUCCESS(run, UIWindow_SetIsResizable(&Fixture.Screen, Id, false));
        TestUI_Ticks(run, &Fixture, 2U);
        Drag(run, &Fixture, 398.0f, 150.0f, 450.0f, 150.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Start));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ItsLeastSizeFitsItsContent(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIWindowOptions Options = UIWindowOptions_Create(u8"Fit");
        Options.FitAcross = UIWindowFit_Fit;
        Options.FitDown = UIWindowFit_Fit;
        uint64_t ContentId = WIDGET_ID_INVALID;
        uint64_t Id = MakeWindowWith(run, &Fixture, &Options, Box(WINDOW_X, WINDOW_Y, 50.0f, 50.0f), &ContentId);
        // Content 200 wide (its cross size) and 80 tall, in the column's padding of 4.
        UIButtonOptions Button = UIButtonOptions_Create(u8"x");
        Button.Element.Hint = UIStackHint_Fixed(80.0f);
        Button.Element.Hint.Align = UIStackAlign_Start;
        Button.Element.Hint.HasCrossSize = true;
        Button.Element.Hint.CrossSize = 200.0f;
        uint64_t ButtonId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, ContentId, &Button, &ButtonId));
        TestUI_Ticks(run, &Fixture, 3U);
        // Both ways fit: the content's 208 x 88, the border at the sides and the bottom, the title bar.
        Rectangle Fitted = Box(WINDOW_X, WINDOW_Y, 208.0f + (2.0f * BORDER), 88.0f + TITLE_HEIGHT + BORDER);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Fitted));
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, 50.0f, 50.0f)));
        TEST_CHECK(run, HasModes(&Fixture, Id, UIScrollMode_Disabled, UIScrollMode_Disabled));
        // Across fits and down scrolls: the content's width and a bar's room, the theme's least height.
        TEST_CHECK_SUCCESS(run, UIWindow_SetFit(&Fixture.Screen, Id, UIWindowFit_Fit, UIWindowFit_Scroll));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, 208.0f + (2.0f * BORDER) + BAR, MIN_HEIGHT)));
        TEST_CHECK(run, HasModes(&Fixture, Id, UIScrollMode_Disabled, UIScrollMode_Auto));
        // Across scrolls and down fits: the content's height and a bar's room under it, the theme's least width.
        TEST_CHECK_SUCCESS(run, UIWindow_SetFit(&Fixture.Screen, Id, UIWindowFit_Scroll, UIWindowFit_Fit));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, MIN_WIDTH, 88.0f + TITLE_HEIGHT + BORDER + BAR)));
        TEST_CHECK(run, HasModes(&Fixture, Id, UIScrollMode_Auto, UIScrollMode_Disabled));
        // Both scroll: only the theme's least size.
        TEST_CHECK_SUCCESS(run, UIWindow_SetFit(&Fixture.Screen, Id, UIWindowFit_Scroll, UIWindowFit_Scroll));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, MIN_WIDTH, MIN_HEIGHT)));
        // Its own least size counts too, in its own UI pixels.
        TEST_CHECK_SUCCESS(run, UIWindow_SetMinimumSize(&Fixture.Screen, Id, (Vector2){ .x = 260.0f, .y = 140.0f }));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, 260.0f, 140.0f)));
        // Resizing smaller than the least size stops at it.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Box(WINDOW_X, WINDOW_Y, 300.0f, 200.0f)));
        TestUI_Ticks(run, &Fixture, 2U);
        Drag(run, &Fixture, 396.0f, 246.0f, 100.0f, 60.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, 260.0f, 140.0f)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Content whose height follows its width: its least height is measured at the width the window gets, its least width is the content's
 * narrowest (not the width it has). */
static void ContentThatWrapsIsMeasuredAtTheWidthItGets(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Fixture.ScreenInput.IsPointerTracked = true;
        UIWindowOptions Options = UIWindowOptions_Create(u8"Wraps");
        Options.FitAcross = UIWindowFit_Fit;
        Options.FitDown = UIWindowFit_Fit;
        uint64_t NarrowContent = WIDGET_ID_INVALID;
        uint64_t Narrow = MakeWindowWith(run, &Fixture, &Options, Box(10.0f, 10.0f, 50.0f, 10.0f), &NarrowContent);
        AddWrappedLabel(run, &Fixture, NarrowContent);
        uint64_t ExactContent = WIDGET_ID_INVALID;
        uint64_t Exact = MakeWindowWith(run, &Fixture, &Options, Box(10.0f, 300.0f, MIN_WIDTH, 10.0f), &ExactContent);
        AddWrappedLabel(run, &Fixture, ExactContent);
        TestUI_Ticks(run, &Fixture, 3U);
        // Wanted 50 or 100 wide, both show at the least width with the text's height there.
        Rectangle AtLeast = GetShown(run, &Fixture, Exact);
        TEST_CHECK(run, TestUI_IsSamePixel(AtLeast.width, MIN_WIDTH) && (AtLeast.height > (TITLE_HEIGHT + BORDER + 40.0f)));
        if (!TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Narrow), Box(10.0f, 10.0f, MIN_WIDTH, AtLeast.height))))
        {
            Rectangle Got = GetShown(run, &Fixture, Narrow);
            printf("    narrow %.1f x %.1f, exact %.1f x %.1f\n", (double)Got.width, (double)Got.height, (double)AtLeast.width, (double)AtLeast.height);
        }
        // Wide, the text takes fewer lines; resized by its corner as small as it goes, it is the least width with the height there.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Narrow, Box(10.0f, 10.0f, 600.0f, 10.0f)));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle Wide = GetShown(run, &Fixture, Narrow);
        TEST_CHECK(run, Wide.height < AtLeast.height);
        Drag(run, &Fixture, 608.0f, 10.0f + Wide.height - 2.0f, -1000.0f, -1000.0f);
        if (!TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Narrow), Box(10.0f, 10.0f, MIN_WIDTH, AtLeast.height))))
        {
            Rectangle Got = GetWanted(run, &Fixture, Narrow);
            printf("    resized %.1f x %.1f\n", (double)Got.width, (double)Got.height);
        }
        // Across it scrolls, down it fits: the text's one line however narrow the window, and a bar's room.
        TEST_CHECK_SUCCESS(run, UIWindow_SetFit(&Fixture.Screen, Narrow, UIWindowFit_Scroll, UIWindowFit_Fit));
        TEST_CHECK_SUCCESS(run, UIWindow_SetFit(&Fixture.Screen, Exact, UIWindowFit_Scroll, UIWindowFit_Fit));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Narrow, Box(10.0f, 10.0f, 150.0f, 10.0f)));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Exact, Box(10.0f, 300.0f, 900.0f, 10.0f)));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, TestUI_IsSamePixel(GetShown(run, &Fixture, Narrow).height, GetShown(run, &Fixture, Exact).height)
            && (GetShown(run, &Fixture, Narrow).height < Wide.height + BAR + 0.5f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Its title bar has room for its padding, its icon and its buttons: what it shows at least, minimized too, and what it measures. */
static void ItsTitleBarHasRoomForItsIconAndButtons(TestRun* run)
{
    TestUIFixture Fixture;
    if (ConstructWith(run, &Fixture, "\"button_size\": [16, 14]", "\"button_size\": [80, 14]", "\"min_size\": [100, 60]", "\"min_size\": [20, 20]"))
    {
        // Buttons of 80: 4 + 3 x 80 + 2 x 2 = 248 with the padding at their right; the title bar's padding of 4 and a gap of 2: 254.
        UIWindowOptions Options = UIWindowOptions_Create(u8"Wide");
        Options.FitAcross = UIWindowFit_Scroll;
        uint64_t Id = MakeWindowWith(run, &Fixture, &Options, Box(WINDOW_X, WINDOW_Y, 50.0f, 10.0f), NULL);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, 254.0f, TITLE_HEIGHT + BORDER)));
        // An icon adds its width and its spacing of 4.
        UIElementIcon Icon = UIElementIcon_FromLetter((uint32_t)'W');
        TEST_CHECK_SUCCESS(run, UIWindow_SetIcon(&Fixture.Screen, Id, &Icon));
        TestUI_Ticks(run, &Fixture, 2U);
        const UIWindowCapability* Window = GetWindow(&Fixture, Id);
        float IconWidth = (Window != NULL) ? Window->_metrics.IconWidth : 0.0f;
        TEST_CHECK(run, IconWidth > 0.0f);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, 254.0f + IconWidth + 4.0f, TITLE_HEIGHT + BORDER)));
        // Minimized (its minimize and close buttons left: 4 + 80 + 2 + 80): wider than the theme's 150, and so is what it measures.
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        float Minimized = 4.0f + IconWidth + 4.0f + 2.0f + (4.0f + 80.0f + 2.0f + 80.0f);
        TEST_CHECK(run, Minimized > MINIMIZED_WIDTH);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, Minimized, TITLE_HEIGHT)));
        Vector2 Measured = MeasureWindow(run, &Fixture, Id, 0.0f);
        TEST_CHECK(run, TestUI_IsSamePixel(Measured.x, Minimized) && TestUI_IsSamePixel(Measured.y, TITLE_HEIGHT));
        // Placed by another widget, it shows its close button alone (4 + 80), and its least width counts that.
        UIWindowOptions Placed = UIWindowOptions_Create(u8"Placed");
        Placed.FitAcross = UIWindowFit_Scroll;
        uint64_t PlacedId = MakeWindowWith(run, &Fixture, &Placed, Box(WINDOW_X, 300.0f, 300.0f, 100.0f), NULL);
        PutInHolder(run, &Fixture, PlacedId, Box(WINDOW_X, 300.0f, 300.0f, 100.0f));
        Measured = MeasureWindow(run, &Fixture, PlacedId, 0.0f);
        TEST_CHECK(run, TestUI_IsSamePixel(Measured.x, 4.0f + 2.0f + 4.0f + 80.0f) && TestUI_IsSamePixel(Measured.y, TITLE_HEIGHT + BORDER));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The caption buttons sit right to left at the title bar's end with the letters of its state; the content lies inside the border. */
static void ItsCaptionButtonsFollowItsState(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentId = WIDGET_ID_INVALID;
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentId);
        // From the padding of 4: close, maximize, minimize, 16 by 14 each, 2 apart, centred down the title bar of 20.
        TEST_CHECK(run, IsSameBox(GetButtonBox(run, &Fixture, Id, UIWindowButton_Close), Box(380.0f, 53.0f, 16.0f, 14.0f)));
        TEST_CHECK(run, IsSameBox(GetButtonBox(run, &Fixture, Id, UIWindowButton_Maximize), Box(362.0f, 53.0f, 16.0f, 14.0f)));
        TEST_CHECK(run, IsSameBox(GetButtonBox(run, &Fixture, Id, UIWindowButton_Minimize), Box(344.0f, 53.0f, 16.0f, 14.0f)));
        const UIWindowCapability* Window = GetWindow(&Fixture, Id);
        TEST_CHECK(run, (Window != NULL) && TestUI_IsSamePixel(UIWindowCapability_GetTitleEnd(Window), 242.0f));
        TEST_CHECK(run, (Window != NULL) && IsSameBox(TestUI_GetPixelBox(run, &Fixture, Window->_areaId), Box(102.0f, 70.0f, 296.0f, 178.0f)));
        TEST_CHECK(run, HasVariant(run, &Fixture, Id, UIWindowButton_Close, "window_close")
            && HasVariant(run, &Fixture, Id, UIWindowButton_Maximize, "window_button") && HasVariant(run, &Fixture, Id, UIWindowButton_Minimize, "window_button"));
        // The content is padded by 4 and spaced by 2, as the theme says.
        UIButtonOptions Button = UIButtonOptions_Create(u8"x");
        Button.Element.Hint = UIStackHint_Fixed(30.0f);
        uint64_t FirstId = WIDGET_ID_INVALID;
        uint64_t SecondId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, ContentId, &Button, &FirstId));
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, ContentId, &Button, &SecondId));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, FirstId).y, 74.0f)
            && TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, SecondId).y, 106.0f));
        // Letters: minimize, maximize, close; minimized, the first restores and the maximize one hides.
        TEST_CHECK(run, HasLetter(run, &Fixture, Id, UIWindowButton_Minimize, UI_WINDOW_MINIMIZE_LETTER)
            && HasLetter(run, &Fixture, Id, UIWindowButton_Maximize, UI_WINDOW_MAXIMIZE_LETTER) && HasLetter(run, &Fixture, Id, UIWindowButton_Close, UI_WINDOW_CLOSE_LETTER));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, HasLetter(run, &Fixture, Id, UIWindowButton_Minimize, UI_WINDOW_RESTORE_LETTER)
            && !IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Maximize)) && IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Close)));
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, HasLetter(run, &Fixture, Id, UIWindowButton_Minimize, UI_WINDOW_MINIMIZE_LETTER)
            && IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Maximize)));
        // Maximized, the second restores.
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, HasLetter(run, &Fixture, Id, UIWindowButton_Maximize, UI_WINDOW_RESTORE_LETTER)
            && HasLetter(run, &Fixture, Id, UIWindowButton_Minimize, UI_WINDOW_MINIMIZE_LETTER));
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, HasLetter(run, &Fixture, Id, UIWindowButton_Maximize, UI_WINDOW_MAXIMIZE_LETTER));
        // One that offers no minimize, minimized by code, still shows the button that restores it.
        TEST_CHECK_SUCCESS(run, UIWindow_SetButtons(&Fixture.Screen, Id, false, true, true));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Minimize)));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Minimize))
            && HasLetter(run, &Fixture, Id, UIWindowButton_Minimize, UI_WINDOW_RESTORE_LETTER));
        ClickButton(run, &Fixture, Id, UIWindowButton_Minimize);
        bool IsMinimized = true;
        TEST_CHECK_SUCCESS(run, UIWindow_GetState(&Fixture.Screen, Id, &IsMinimized, NULL));
        TEST_CHECK(run, !IsMinimized && !IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Minimize)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A window at each corner of the workspace, the surface shrunk: each title bar stays reachable, at scales that are not whole numbers and
 * for a window of its own scale; grown back, each is where it was. */
static void ItStaysReachableWhenTheSurfaceShrinks(TestRun* run)
{
    static const float ScreenScales[] = { 1.0f, 1.25f, 1.5f, 0.75f };
    for (size_t ScaleIndex = 0U; ScaleIndex < (sizeof(ScreenScales) / sizeof(ScreenScales[0])); ScaleIndex++)
    {
        TestUIFixture Fixture;
        if (!Construct(run, &Fixture))
        {
            TestUI_Deconstruct(run, &Fixture);
            continue;
        }
        float Scale = ScreenScales[ScaleIndex];
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, Scale));
        float Width = TEST_UI_SURFACE_WIDTH / Scale;
        float Height = TEST_UI_SURFACE_HEIGHT / Scale;
        Rectangle Places[4] =
        {
            Box(0.0f, 0.0f, 300.0f, 200.0f), Box(Width - 300.0f, 0.0f, 300.0f, 200.0f),
            Box(0.0f, Height - 200.0f, 300.0f, 200.0f), Box(Width - 300.0f, Height - 200.0f, 300.0f, 200.0f)
        };
        uint64_t Ids[5];
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            Ids[Index] = MakeWindow(run, &Fixture, Places[Index], NULL);
        }
        UIWindowOptions Scaled = UIWindowOptions_Create(u8"Scaled");
        Scaled.Element.Scale = 1.5f;
        Rectangle ScaledPlace = Box(Width - 300.0f, Height - 200.0f, 300.0f, 200.0f);
        Ids[4] = MakeWindowWith(run, &Fixture, &Scaled, ScaledPlace, NULL);
        static const Vector2 Surfaces[] = { { 400.0f, 260.0f }, { 150.0f, 90.0f }, { 1000.0f, 30.0f } };
        for (size_t Surface = 0U; Surface < (sizeof(Surfaces) / sizeof(Surfaces[0])); Surface++)
        {
            TestUI_SetSurface(run, &Fixture, Surfaces[Surface].x, Surfaces[Surface].y);
            TestUI_Ticks(run, &Fixture, 2U);
            float SmallWidth = Surfaces[Surface].x / Scale;
            float SmallHeight = Surfaces[Surface].y / Scale;
            for (size_t Index = 0U; Index < 5U; Index++)
            {
                float Own = (Index == 4U) ? 1.5f : 1.0f;
                Rectangle Shown = GetShown(run, &Fixture, Ids[Index]);
                bool IsFine = IsReachable(Shown, SmallWidth, SmallHeight, TITLE_HEIGHT * Own, BUTTONS_WIDTH * Own, UI_WINDOW_REACHABLE_WIDTH * Own)
                    || ((SmallHeight < (TITLE_HEIGHT * Own)) && TestUI_IsSamePixel(Shown.y, 0.0f));
                if (!TEST_CHECK(run, IsFine))
                {
                    printf("    scale %.2f surface %zu window %zu: %.2f %.2f %.2f %.2f\n", (double)Scale, Surface, Index, (double)Shown.x,
                        (double)Shown.y, (double)Shown.width, (double)Shown.height);
                }
            }
        }
        TestUI_SetSurface(run, &Fixture, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT);
        TestUI_Ticks(run, &Fixture, 2U);
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Ids[Index]), Places[Index]));
        }
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Ids[4]), ScaledPlace));
        TestUI_Deconstruct(run, &Fixture);
    }
}


// Tests: minimize, maximize, close.
static void MinimizeAndRestoreKeepItsSizeAndWhatIsInside(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentId = WIDGET_ID_INVALID;
        Rectangle Start = Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT);
        uint64_t Id = MakeWindow(run, &Fixture, Start, &ContentId);
        uint64_t TextId = AddTextBox(run, &Fixture, ContentId);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 150.0f, 90.0f);
        TypeText(run, &Fixture, "kept");
        ClickButton(run, &Fixture, Id, UIWindowButton_Minimize);
        bool Minimized = false;
        TEST_CHECK_SUCCESS(run, UIWindow_GetState(&Fixture.Screen, Id, &Minimized, NULL));
        TEST_CHECK(run, Minimized && IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, MINIMIZED_WIDTH, TITLE_HEIGHT)));
        // Its content is hidden: not hit, and the focus in it is gone.
        TEST_CHECK(run, !IsInWindow(&Fixture, TopmostAt(&Fixture, 150.0f, 90.0f), Id));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) != TextId);
        // Minimized, its first button restores it: as it was, the text still there.
        ClickButton(run, &Fixture, Id, UIWindowButton_Minimize);
        TEST_CHECK_SUCCESS(run, UIWindow_GetState(&Fixture.Screen, Id, &Minimized, NULL));
        TEST_CHECK(run, !Minimized && IsSameBox(GetShown(run, &Fixture, Id), Start) && HasText(&Fixture, TextId, "kept"));
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 150.0f, 90.0f), Id));
        // A minimized window can be moved by its title bar, and restores at its new place with its size.
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        Drag(run, &Fixture, TITLE_X, TITLE_Y, TITLE_X + 40.0f, TITLE_Y + 30.0f);
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X + 40.0f, WINDOW_Y + 30.0f, WINDOW_WIDTH, WINDOW_HEIGHT)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void MaximizeFillsTheWorkspaceAndRestoresExactly(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rectangle Start = Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT);
        uint64_t Id = MakeWindow(run, &Fixture, Start, NULL);
        ClickButton(run, &Fixture, Id, UIWindowButton_Maximize);
        TEST_CHECK(run, IsMaximized(&Fixture, Id) && (GetLayer(&Fixture, Id) == UIScreenLayer_Maximized));
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(0.0f, 0.0f, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT)));
        // The bars a workspace keeps for themselves are left alone.
        TEST_CHECK_SUCCESS(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = 0.0f, .Top = 30.0f, .Right = 0.0f, .Bottom = 20.0f }));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(0.0f, 30.0f, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT - 50.0f)));
        // Maximized, it has no edges and its title bar moves nothing.
        Drag(run, &Fixture, 300.0f, 40.0f, 400.0f, 120.0f);
        TEST_CHECK(run, IsMaximized(&Fixture, Id) && IsSameBox(GetWanted(run, &Fixture, Id), Start));
        ClickButton(run, &Fixture, Id, UIWindowButton_Maximize);
        TEST_CHECK(run, !IsMaximized(&Fixture, Id) && (GetLayer(&Fixture, Id) == UIScreenLayer_Floating));
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Start));
        // Minimized from maximized, it restores to maximized.
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, Id));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, (GetLayer(&Fixture, Id) == UIScreenLayer_Floating)
            && IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, MINIMIZED_WIDTH, TITLE_HEIGHT)));
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, (GetLayer(&Fixture, Id) == UIScreenLayer_Maximized) && IsMaximized(&Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void OneWindowIsMaximizedAtATime(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t First = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        uint64_t Second = MakeWindow(run, &Fixture, Box(500.0f, 200.0f, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, First));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, Second));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !IsMaximized(&Fixture, First) && IsMaximized(&Fixture, Second));
        TEST_CHECK(run, (GetLayer(&Fixture, First) == UIScreenLayer_Floating) && (GetLayer(&Fixture, Second) == UIScreenLayer_Maximized));
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, First), Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        // Restoring a minimized window that was maximized restores the other one.
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, First));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, First));
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, Second));
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.Screen, First));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsMaximized(&Fixture, First) && !IsMaximized(&Fixture, Second));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADoubleClickOnTheTitleBarMaximizesAndRestores(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, IsMaximized(&Fixture, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, TITLE_X, 10.0f);
        TestUI_Click(run, &Fixture, TITLE_X, 10.0f);
        TEST_CHECK(run, !IsMaximized(&Fixture, Id));
        // A window that cannot maximize ignores it.
        TEST_CHECK_SUCCESS(run, UIWindow_SetButtons(&Fixture.Screen, Id, true, false, true));
        WaitPastDoubleClick(run, &Fixture);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, !IsMaximized(&Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ClosingAsksItsQueryFirst(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        CloseProbe Probe = { .Asked = 0U, .IsAllowed = false };
        UIWindowOptions Options = UIWindowOptions_Create(u8"Asks");
        Options.CloseQuery = OnClose;
        Options.CloseData = UserData_FromPointer(&Probe);
        uint64_t Id = MakeWindowWith(run, &Fixture, &Options, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        ClickButton(run, &Fixture, Id, UIWindowButton_Close);
        TEST_CHECK(run, (Probe.Asked == 1U) && UIScreen_IsWidgetAlive(&Fixture.Screen, Id));
        bool IsClosed = true;
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.Screen, Id, &IsClosed));
        TEST_CHECK(run, (Probe.Asked == 2U) && !IsClosed && UIScreen_IsWidgetAlive(&Fixture.Screen, Id));
        Probe.IsAllowed = true;
        ClickButton(run, &Fixture, Id, UIWindowButton_Close);
        TEST_CHECK(run, (Probe.Asked == 3U) && !UIScreen_IsWidgetAlive(&Fixture.Screen, Id));
        // Without asking.
        Probe.IsAllowed = false;
        Options.HasPosition = false;
        uint64_t Again = MakeWindowWith(run, &Fixture, &Options, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        TEST_CHECK_SUCCESS(run, UIWindow_CloseNow(&Fixture.Screen, Again));
        TEST_CHECK(run, (Probe.Asked == 3U) && !UIScreen_IsWidgetAlive(&Fixture.Screen, Again));
        // With no query, and with no close button.
        uint64_t Plain = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        TEST_CHECK_SUCCESS(run, UIWindow_SetButtons(&Fixture.Screen, Plain, true, true, false));
        TestUI_Ticks(run, &Fixture, 2U);
        uint64_t CloseId = WIDGET_ID_INVALID;
        Widget* CloseButton = NULL;
        TEST_CHECK_SUCCESS(run, UIWindow_GetButtonId(&Fixture.Screen, Plain, UIWindowButton_Close, &CloseId));
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, CloseId, &CloseButton) && !Widget_IsRendered(CloseButton));
        TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.Screen, Plain, &IsClosed));
        TEST_CHECK(run, IsClosed && !UIScreen_IsWidgetAlive(&Fixture.Screen, Plain));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetWidgetCount(&Fixture.Screen) == 0U);
        // A query that closes the window itself: closed once, whatever it answers.
        CueProbe Cues = { .Opens = 0U, .Closes = 0U, .WindowId = WIDGET_ID_INVALID };
        UserData CueData = UserData_FromPointer(&Cues);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Cue), OnCue, &CueData,
            &Subscription));
        for (int32_t Answer = 0; Answer < 2; Answer++)
        {
            Probe.IsAllowed = Answer == 1;
            Options.CloseQuery = OnCloseItself;
            uint64_t Itself = MakeWindowWith(run, &Fixture, &Options, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
            Cues.WindowId = Itself;
            Cues.Closes = 0U;
            IsClosed = false;
            TEST_CHECK_SUCCESS(run, UIWindow_Close(&Fixture.Screen, Itself, &IsClosed));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, IsClosed && !UIScreen_IsWidgetAlive(&Fixture.Screen, Itself) && (Cues.Closes == 1U));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADestroyedWindowIsLetGoMidDrag(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        PressAt(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_MovePointer(&Fixture, TITLE_X + 30.0f, TITLE_Y + 10.0f);
        TestUI_Tick(run, &Fixture);
        Widget* Window = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Window)))
        {
            TEST_CHECK_SUCCESS(run, Widget_Destroy(Window));
        }
        TestUI_MovePointer(&Fixture, TITLE_X + 60.0f, TITLE_Y + 20.0f);
        TestUI_Tick(run, &Fixture);
        ReleaseAt(run, &Fixture, TITLE_X + 70.0f, TITLE_Y + 20.0f);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.Screen) && (UIScreen_GetWidgetCount(&Fixture.Screen) == 0U));
        // Resized by an edge, and destroyed there.
        uint64_t Other = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        PressAt(run, &Fixture, 398.0f, 150.0f);
        TestUI_MovePointer(&Fixture, 420.0f, 150.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIWindow_CloseNow(&Fixture.Screen, Other));
        ReleaseAt(run, &Fixture, 440.0f, 150.0f);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.Screen));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: focus and raising.
/* A press on a window's title bar acts on nothing inside it. With the focus in it, nothing changes (no edit is left); from elsewhere,
 * the focus comes back to what had it inside, entered again if it was. */
static void APressOnTheTitleBarActsOnNothingInside(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentA = WIDGET_ID_INVALID;
        uint64_t ContentB = WIDGET_ID_INVALID;
        uint64_t A = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentA);
        uint64_t B = MakeWindow(run, &Fixture, Box(500.0f, 200.0f, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentB);
        uint64_t TextA = AddTextBox(run, &Fixture, ContentA);
        uint64_t TextB = AddTextBox(run, &Fixture, ContentB);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 150.0f, 90.0f);
        TypeText(run, &Fixture, "ab");
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextA) && IsActive(&Fixture, A) && !IsActive(&Fixture, B));
        EnteringProbe Entering = { .Changes = 0U };
        UserData Data = UserData_FromPointer(&Entering);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_EnteredChanged),
            OnEnteredChanged, &Data, &Subscription));
        TestUIEvents Events;
        TestUI_RecordEvents(run, &Fixture, &Events);
        Events.IsRecordingInputEvents = true;
        // The focus in it already: the press and a drag change nothing inside, the box stays entered in the same visit.
        Drag(run, &Fixture, TITLE_X, TITLE_Y, TITLE_X + 5.0f, TITLE_Y + 5.0f);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextA) && (Entering.Changes == 0U) && (Events.Count == 0U));
        TypeText(run, &Fixture, "c");
        TEST_CHECK(run, HasText(&Fixture, TextA, "abc"));
        // The focus elsewhere: a press on A's title bar gives it back to its box, entered again; B's box is left.
        TestUI_Click(run, &Fixture, 550.0f, 240.0f);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextB) && IsActive(&Fixture, B) && !IsActive(&Fixture, A));
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, TITLE_X + 5.0f, TITLE_Y + 5.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextA));
        TEST_CHECK(run, IsActive(&Fixture, A) && !IsActive(&Fixture, B));
        // Nothing inside A heard a press.
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            TEST_CHECK(run, !IsInWindow(&Fixture, Events.Events[Index].WidgetId, A)
                || (Events.Events[Index].WidgetEventId != (int32_t)UIElementEvent_Pressed));
        }
        TypeText(run, &Fixture, "d");
        TEST_CHECK(run, HasText(&Fixture, TextA, "abcd"));
        // Left by Escape (only focused), then the focus elsewhere: the title bar gives it back focused, not entered.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
        TestUI_Click(run, &Fixture, 550.0f, 240.0f);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void WithNothingRememberedTheFocusGoesToItsTargetOrItself(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentA = WIDGET_ID_INVALID;
        uint64_t A = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentA);
        uint64_t TextA = AddTextBox(run, &Fixture, ContentA);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == A) && IsActive(&Fixture, A));
        // A default focus target asked entered: a world view's window.
        TEST_CHECK_SUCCESS(run, UIWindow_SetDefaultFocus(&Fixture.Screen, A, TextA, true));
        TestUI_Click(run, &Fixture, 800.0f, 450.0f);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextA));
        // A target that is not inside is refused.
        uint64_t Other = MakeWindow(run, &Fixture, Box(500.0f, 200.0f, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        TEST_CHECK_ERROR(run, UIWindow_SetDefaultFocus(&Fixture.Screen, Other, TextA, false), ErrorCode_InvalidOperation);
        // A caption button's press gives the focus back too, then does what it is for.
        TestUI_Click(run, &Fixture, 800.0f, 450.0f);
        ClickButton(run, &Fixture, A, UIWindowButton_Maximize);
        TEST_CHECK(run, IsMaximized(&Fixture, A) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The focus goes back only to what can take it inside the window: not to what was disabled, made unfocusable or moved away meanwhile;
 * what moved into a window with the focus is remembered there. */
static void TheFocusGoesBackOnlyToWhatCanTakeItInside(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentA = WIDGET_ID_INVALID;
        uint64_t ContentB = WIDGET_ID_INVALID;
        uint64_t A = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentA);
        uint64_t B = MakeWindow(run, &Fixture, Box(500.0f, 200.0f, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentB);
        uint64_t TextA = AddTextBox(run, &Fixture, ContentA);
        TestUI_Ticks(run, &Fixture, 2U);
        Widget* Text = NULL;
        // Disabled meanwhile.
        TestUI_Click(run, &Fixture, 150.0f, 90.0f);
        TestUI_Click(run, &Fixture, 800.0f, 100.0f);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, TextA, &Text)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Text, false));
        }
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == A);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, TextA, &Text)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Text, true));
        }
        // Made unfocusable meanwhile.
        TestUI_Click(run, &Fixture, 150.0f, 90.0f);
        TestUI_Click(run, &Fixture, 800.0f, 100.0f);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, TextA, &Text)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(Text, false));
        }
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == A);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, TextA, &Text)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(Text, true));
        }
        // Moved to the other window with the focus: B remembers it, A no more.
        TestUI_Click(run, &Fixture, 150.0f, 90.0f);
        Widget* Into = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, TextA, &Text) && UIScreen_TryGetWidgetById(&Fixture.Screen, ContentB, &Into)))
        {
            TEST_CHECK_SUCCESS(run, Widget_MoveTo(Text, Into));
        }
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA) && IsActive(&Fixture, B) && !IsActive(&Fixture, A));
        TestUI_Click(run, &Fixture, 800.0f, 100.0f);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == A);
        TestUI_Click(run, &Fixture, 550.0f, 212.0f);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA);
        // A box of A's focused, then one moved into A with the focus and A's title bar pressed in the same tick (before A's update saw
        // it): the focus is inside A already, so it stays on the box that moved in.
        uint64_t Other = AddTextBox(run, &Fixture, ContentA);
        WaitPastDoubleClick(run, &Fixture);
        Rectangle OtherBox = TestUI_GetPixelBox(run, &Fixture, Other);
        TestUI_Click(run, &Fixture, OtherBox.x + 20.0f, OtherBox.y + 10.0f);
        TestUI_Click(run, &Fixture, 550.0f, 240.0f);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA);
        TestUI_Tick(run, &Fixture);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, TextA, &Text) && UIScreen_TryGetWidgetById(&Fixture.Screen, ContentA, &Into)))
        {
            TEST_CHECK_SUCCESS(run, Widget_MoveTo(Text, Into));
        }
        TestUI_MovePointer(&Fixture, TITLE_X + 30.0f, TITLE_Y);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What had the focus inside is remembered even when the focus moved on before the window updated; with the focus on the window itself,
 * a press on its title bar leaves it there. */
static void ItRemembersTheFocusBetweenItsUpdates(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentA = WIDGET_ID_INVALID;
        uint64_t ContentB = WIDGET_ID_INVALID;
        uint64_t A = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentA);
        MakeWindow(run, &Fixture, Box(500.0f, 200.0f, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentB);
        uint64_t TextA = AddTextBox(run, &Fixture, ContentA);
        uint64_t TextB = AddTextBox(run, &Fixture, ContentB);
        TestUI_Ticks(run, &Fixture, 2U);
        SetFocus(run, &Fixture, TextA);
        SetFocus(run, &Fixture, TextB);
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA);
        // On the window itself, a press on its title bar goes on inside: to what had the focus there (before the default target set
        // since), not entered, as it was.
        SetFocus(run, &Fixture, A);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIWindow_SetDefaultFocus(&Fixture.Screen, A, TextA, true));
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID)
            && IsActive(&Fixture, A));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A list opened from inside the window holds the focus as a popup: the window stays active. */
static void APopupFromInsideLeavesItActive(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentId = WIDGET_ID_INVALID;
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentId);
        static const unsigned char* const Choices[] = { u8"One", u8"Two", u8"Three" };
        UIDropdownOptions Options = UIDropdownOptions_Create();
        Options.Options = Choices;
        Options.OptionCount = 3U;
        Options.Element.Hint = UIStackHint_Fixed(24.0f);
        uint64_t DropdownId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateDropdown(&Fixture.Kit, &Fixture.Screen, ContentId, &Options, &DropdownId));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 150.0f, 86.0f);
        bool IsOpen = false;
        uint64_t ListId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIDropdown_IsOpen(&Fixture.Screen, DropdownId, &IsOpen, &ListId));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsOpen && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == ListId) && IsActive(&Fixture, Id));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        // A popup that its content column (which takes no focus) opened, the focus in it: the window stays active, and a press on its
        // title bar later gives the focus back to the box that had it before, entered as it was.
        uint64_t TextId = AddTextBox(run, &Fixture, ContentId);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 150.0f, 112.0f);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId);
        UIPanelOptions Panel = UIPanelOptions_Create();
        uint64_t PanelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &PanelId));
        UIButtonOptions Button = UIButtonOptions_Create(u8"in the popup");
        uint64_t ButtonId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, PanelId, &Button, &ButtonId));
        UIPopupOptions Popup = UIPopupOptions_Create();
        Popup.AnchorWidgetId = ContentId;
        Popup.OwnerId = ContentId;
        Popup.FocusId = ButtonId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PanelId, &Popup));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == ButtonId) && IsActive(&Fixture, Id));
        TestUI_Click(run, &Fixture, 800.0f, 100.0f);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PanelId) && !IsActive(&Fixture, Id));
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A press on an edge acts on nothing inside either, and gives its window the focus back as one on the title bar does. */
static void APressOnAnEdgeActsOnNothingInside(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentA = WIDGET_ID_INVALID;
        uint64_t ContentB = WIDGET_ID_INVALID;
        uint64_t A = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentA);
        MakeWindow(run, &Fixture, Box(500.0f, 200.0f, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentB);
        uint64_t TextA = AddTextBox(run, &Fixture, ContentA);
        uint64_t TextB = AddTextBox(run, &Fixture, ContentB);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 150.0f, 90.0f);
        EnteringProbe Entering = { .Changes = 0U };
        UserData Data = UserData_FromPointer(&Entering);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_EnteredChanged),
            OnEnteredChanged, &Data, &Subscription));
        TestUI_Click(run, &Fixture, 398.0f, 150.0f);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextA)
            && (Entering.Changes == 0U));
        TestUI_Click(run, &Fixture, 550.0f, 240.0f);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextB);
        TestUI_Click(run, &Fixture, 398.0f, 150.0f);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextA) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextA)
            && IsActive(&Fixture, A));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Only the left button moves or resizes it; another button's press and release meanwhile does not end a drag, a press lost does. */
static void OnlyTheLeftButtonDragsIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rectangle Start = Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT);
        uint64_t Id = MakeWindow(run, &Fixture, Start, NULL);
        RightDrag(run, &Fixture, TITLE_X, TITLE_Y, TITLE_X + 50.0f, TITLE_Y + 50.0f);
        RightDrag(run, &Fixture, 398.0f, 150.0f, 450.0f, 150.0f);
        RightDrag(run, &Fixture, TITLE_X, TITLE_Y, TITLE_X, TITLE_Y);
        RightDrag(run, &Fixture, TITLE_X, TITLE_Y, TITLE_X, TITLE_Y);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Start) && !IsMaximized(&Fixture, Id));
        // A left drag goes on through a right click.
        PressAt(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_MovePointer(&Fixture, TITLE_X + 20.0f, TITLE_Y);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        ReleaseAt(run, &Fixture, TITLE_X + 40.0f, TITLE_Y + 10.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(WINDOW_X + 40.0f, WINDOW_Y + 10.0f, WINDOW_WIDTH, WINDOW_HEIGHT)));
        // Hidden mid-drag by an edge, then shown: the press was lost, the drag is over.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Start));
        TestUI_Ticks(run, &Fixture, 2U);
        Widget* Window = NULL;
        PressAt(run, &Fixture, 398.0f, 150.0f);
        TestUI_MovePointer(&Fixture, 420.0f, 150.0f);
        TestUI_Tick(run, &Fixture);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Window)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Window, false));
        }
        TestUI_Tick(run, &Fixture);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Window)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Window, true));
        }
        TestUI_Ticks(run, &Fixture, 2U);
        UIElementState State = UIElementState_Disabled;
        GetEdgeBand(&Fixture, Id, UIWindowEdge_Right, &State);
        const UIWindowCapability* Capability = GetWindow(&Fixture, Id);
        TEST_CHECK(run, (State != UIElementState_Pressed) && (Capability != NULL) && (Capability->_grab == UIWindowGrab_None));
        ReleaseAt(run, &Fixture, 420.0f, 150.0f);
        // The same by the title bar.
        PressAt(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_MovePointer(&Fixture, TITLE_X + 20.0f, TITLE_Y);
        TestUI_Tick(run, &Fixture);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Window)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Window, false));
        }
        TestUI_Tick(run, &Fixture);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Window)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Window, true));
        }
        TestUI_Ticks(run, &Fixture, 2U);
        Capability = GetWindow(&Fixture, Id);
        TEST_CHECK(run, (Capability != NULL) && (Capability->_grab == UIWindowGrab_None));
        ReleaseAt(run, &Fixture, TITLE_X + 20.0f, TITLE_Y);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A change of state ends a drag; maximizing a minimized window shows it maximized; a double click on a minimized one restores it; restored
 * from maximized, a window is back on top of the floating ones; minimized from maximized, its bar stays reachable. */
static void ItsStatesEndADragAndStack(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rectangle Start = Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT);
        uint64_t A = MakeWindow(run, &Fixture, Start, NULL);
        uint64_t B = MakeWindow(run, &Fixture, Box(200.0f, 100.0f, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        // Maximized by code mid-drag.
        PressAt(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_MovePointer(&Fixture, TITLE_X + 20.0f, TITLE_Y);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, A));
        TestUI_MovePointer(&Fixture, TITLE_X + 60.0f, TITLE_Y + 30.0f);
        TestUI_Tick(run, &Fixture);
        ReleaseAt(run, &Fixture, TITLE_X + 60.0f, TITLE_Y + 30.0f);
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.Screen, A));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, A), Box(WINDOW_X + 20.0f, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        // Minimized by code mid-drag.
        PressAt(run, &Fixture, TITLE_X + 20.0f, TITLE_Y);
        TestUI_MovePointer(&Fixture, TITLE_X + 40.0f, TITLE_Y);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, A));
        TestUI_MovePointer(&Fixture, TITLE_X + 80.0f, TITLE_Y + 30.0f);
        TestUI_Tick(run, &Fixture);
        ReleaseAt(run, &Fixture, TITLE_X + 80.0f, TITLE_Y + 30.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, A), Box(WINDOW_X + 40.0f, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        // Maximized while minimized: maximized, not minimized.
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, A));
        TestUI_Ticks(run, &Fixture, 2U);
        bool IsMinimized = true;
        TEST_CHECK_SUCCESS(run, UIWindow_GetState(&Fixture.Screen, A, &IsMinimized, NULL));
        TEST_CHECK(run, !IsMinimized && IsMaximized(&Fixture, A) && (GetLayer(&Fixture, A) == UIScreenLayer_Maximized)
            && IsSameBox(GetShown(run, &Fixture, A), Box(0.0f, 0.0f, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT)));
        // B raised over A, A maximized and restored: A is back on top.
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.Screen, A));
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, A, Start));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 420.0f, 112.0f);
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 300.0f, 200.0f), B));
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, A));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(&Fixture.Screen, A));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 300.0f, 200.0f), A));
        // A double click on a minimized window's title bar restores it, maximizing nothing.
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, A));
        WaitPastDoubleClick(run, &Fixture);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK_SUCCESS(run, UIWindow_GetState(&Fixture.Screen, A, &IsMinimized, NULL));
        TEST_CHECK(run, !IsMinimized && !IsMaximized(&Fixture, A));
        // Minimized from maximized, its bar (minimize and close: 38 of buttons) stays reachable on a smaller surface.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, A, Box(800.0f, 400.0f, WINDOW_WIDTH, WINDOW_HEIGHT)));
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, A));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, A));
        TestUI_SetSurface(run, &Fixture, 400.0f, 260.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, A), Box(400.0f - UI_WINDOW_REACHABLE_WIDTH, 260.0f - TITLE_HEIGHT, MINIMIZED_WIDTH, TITLE_HEIGHT)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Restored from minimized by its button or a double click, the window gives the focus back inside: the box being typed in takes the
 * typing again. A press on the border of a window with no edges acts on nothing inside either. */
static void RestoredItGivesTheFocusBackInside(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentId = WIDGET_ID_INVALID;
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentId);
        uint64_t TextId = AddTextBox(run, &Fixture, ContentId);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 150.0f, 90.0f);
        TypeText(run, &Fixture, "ab");
        ClickButton(run, &Fixture, Id, UIWindowButton_Minimize);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) != TextId);
        ClickButton(run, &Fixture, Id, UIWindowButton_Minimize);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId));
        TypeText(run, &Fixture, "c");
        TEST_CHECK(run, HasText(&Fixture, TextId, "abc"));
        // The same by a double click on the minimized title bar.
        ClickButton(run, &Fixture, Id, UIWindowButton_Minimize);
        WaitPastDoubleClick(run, &Fixture);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        bool IsMinimized = true;
        TEST_CHECK_SUCCESS(run, UIWindow_GetState(&Fixture.Screen, Id, &IsMinimized, NULL));
        TEST_CHECK(run, !IsMinimized && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId));
        TypeText(run, &Fixture, "d");
        TEST_CHECK(run, HasText(&Fixture, TextId, "abcd"));
        // Not resizable, its border is the frame's: a press there leaves the box entered.
        TEST_CHECK_SUCCESS(run, UIWindow_SetIsResizable(&Fixture.Screen, Id, false));
        WaitPastDoubleClick(run, &Fixture);
        TEST_CHECK(run, !IsInWindow(&Fixture, TopmostAt(&Fixture, WINDOW_X + 1.0f, 150.0f), ContentId));
        EnteringProbe Entering = { .Changes = 0U };
        UserData Data = UserData_FromPointer(&Entering);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_EnteredChanged),
            OnEnteredChanged, &Data, &Subscription));
        TestUI_Click(run, &Fixture, WINDOW_X + 1.0f, 150.0f);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId)
            && (Entering.Changes == 0U));
        // From elsewhere, a press on the border gives the focus back as the title bar does.
        TestUI_Click(run, &Fixture, 800.0f, 450.0f);
        TestUI_Click(run, &Fixture, WINDOW_X + 1.0f, 150.0f);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A double click on the title bar maximizes the window when its second press is let go: the release lands on the title bar, so nothing
 * the maximized window then shows under the pointer hears it. */
static void ADoubleClickActsOnNothingInside(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t ContentId = WIDGET_ID_INVALID;
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentId);
        // Maximized, the content's first element spans 24 to 104 down: under the title bar's place before.
        UIButtonOptions Button = UIButtonOptions_Create(u8"on release");
        Button.Element.Hint = UIStackHint_Fixed(80.0f);
        Button.ClickMode = UIClickMode_OnRelease;
        uint64_t ButtonId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, ContentId, &Button, &ButtonId));
        TestUI_Ticks(run, &Fixture, 2U);
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsMaximized(&Fixture, Id) && IsInWindow(&Fixture, TopmostAt(&Fixture, TITLE_X, TITLE_Y), Id)
            && (TopmostAt(&Fixture, TITLE_X, TITLE_Y) == ButtonId));
        bool IsHeard = false;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            IsHeard = IsHeard || (Events.Events[Index].WidgetId == ButtonId);
        }
        TEST_CHECK(run, !IsHeard);
        // A single click on its title bar afterwards toggles nothing.
        WaitPastDoubleClick(run, &Fixture);
        TestUI_Click(run, &Fixture, TITLE_X, 10.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsMaximized(&Fixture, Id));
        // Another button let go while the second press is held toggles nothing yet; letting go of the left one does.
        WaitPastDoubleClick(run, &Fixture);
        TestUI_Click(run, &Fixture, TITLE_X, 10.0f);
        PressAt(run, &Fixture, TITLE_X, 10.0f);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsMaximized(&Fixture, Id));
        ReleaseAt(run, &Fixture, TITLE_X, 10.0f);
        TEST_CHECK(run, !IsMaximized(&Fixture, Id));
        // A double click whose second press is lost (the window hidden meanwhile) does nothing.
        WaitPastDoubleClick(run, &Fixture);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        PressAt(run, &Fixture, TITLE_X, TITLE_Y);
        Widget* Window = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Window)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Window, false));
        }
        TestUI_Tick(run, &Fixture);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Window)))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Window, true));
        }
        ReleaseAt(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Ticks(run, &Fixture, 2U);
        const UIWindowCapability* Capability = GetWindow(&Fixture, Id);
        TEST_CHECK(run, !IsMaximized(&Fixture, Id) && (Capability != NULL) && !Capability->_isToggleOnRelease);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* On a screen without the keyboard (a world screen not in use), a press on the title bar gives the focus back but enters nothing: what
 * entering gives, the keys, are not the screen's. */
static void WithoutTheKeyboardItFocusesButEntersNothing(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t ContentId = WIDGET_ID_INVALID;
        MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), &ContentId);
        uint64_t TextId = AddTextBox(run, &Fixture, ContentId);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 150.0f, 90.0f);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId);
        TestUI_Click(run, &Fixture, 800.0f, 450.0f);
        Fixture.ScreenInput.HasKeyboardFocus = false;
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, TITLE_X, TITLE_Y);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
        Fixture.ScreenInput.HasKeyboardFocus = true;
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Theme values that would leave nothing to drag by are kept sane: a title bar of at least UI_WINDOW_MIN_TITLE_HEIGHT, edges at most a
 * quarter of it thick. */
static void ItsTitleBarAlwaysLeavesSomethingToDrag(TestRun* run)
{
    TestUIFixture Fixture;
    if (ConstructWith(run, &Fixture, "\"title_height\": 20", "\"title_height\": 0", "\"resize_margin\": 4", "\"resize_margin\": 40"))
    {
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        const UIWindowCapability* Window = GetWindow(&Fixture, Id);
        TEST_CHECK(run, (Window != NULL) && (Window->_metrics.TitleHeight == UI_WINDOW_MIN_TITLE_HEIGHT)
            && (Window->_metrics.ResizeMargin == (UI_WINDOW_MIN_TITLE_HEIGHT * UI_WINDOW_MAX_EDGE_SHARE_OF_TITLE)));
        // Its title bar's middle moves it.
        Drag(run, &Fixture, 200.0f, WINDOW_Y + 4.0f, 250.0f, WINDOW_Y + 34.0f);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(WINDOW_X + 50.0f, WINDOW_Y + 30.0f, WINDOW_WIDTH, WINDOW_HEIGHT)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APressRaisesItWithinItsLayer(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t A = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        uint64_t B = MakeWindow(run, &Fixture, Box(200.0f, 100.0f, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        // B was made last: it covers A where they overlap, and a press there is B's.
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 250.0f, 200.0f), B));
        TestUI_Click(run, &Fixture, 250.0f, 200.0f);
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 250.0f, 200.0f), B));
        // A press on A's part that shows raises A.
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 250.0f, 200.0f), A));
        // A press on its content does too.
        TestUI_Click(run, &Fixture, 450.0f, 280.0f);
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 250.0f, 200.0f), B));
        // A maximized window stays over the floating ones; giving one of them the focus by code restores it, so the one focused shows.
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, B));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 250.0f, 200.0f), B) && IsInWindow(&Fixture, TopmostAt(&Fixture, 900.0f, 450.0f), B));
        TEST_CHECK_SUCCESS(run, UIWindow_Focus(&Fixture.Screen, A));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !IsMaximized(&Fixture, B) && (GetLayer(&Fixture, B) == UIScreenLayer_Floating)
            && IsInWindow(&Fixture, TopmostAt(&Fixture, 250.0f, 200.0f), A) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == A));
        // A window minimized from maximized keeps that it was.
        TEST_CHECK_SUCCESS(run, UIWindow_Maximize(&Fixture.Screen, B));
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(&Fixture.Screen, B));
        TEST_CHECK_SUCCESS(run, UIWindow_Focus(&Fixture.Screen, A));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsMaximized(&Fixture, B));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A window's scale zooms all it holds: the box stays, its frame and its buttons double. */
static void ItsScaleZoomsWhatItHolds(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIWindowOptions Options = UIWindowOptions_Create(u8"Zoomed");
        Options.Element.Scale = 2.0f;
        Rectangle Start = Box(WINDOW_X, WINDOW_Y, 400.0f, 300.0f);
        uint64_t Id = MakeWindowWith(run, &Fixture, &Options, Start, NULL);
        TEST_CHECK(run, IsSameBox(TestUI_GetPixelBox(run, &Fixture, Id), Start));
        Rectangle Close = GetButtonBox(run, &Fixture, Id, UIWindowButton_Close);
        TEST_CHECK(run, TestUI_IsSamePixel(Close.width, 32.0f) && TestUI_IsSamePixel(Close.height, 28.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(Close.x + Close.width, WINDOW_X + 400.0f - 8.0f));
        // Its least size doubles too.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(&Fixture.Screen, Id, Box(WINDOW_X, WINDOW_Y, 10.0f, 10.0f)));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, 2.0f * MIN_WIDTH, 2.0f * MIN_HEIGHT)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A new window opens over the floating windows raised before it, and tells its opening and its closing as cues. */
static void ANewWindowOpensOnTopAndTellsItsCues(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        CueProbe Cues = { .Opens = 0U, .Closes = 0U, .WindowId = WIDGET_ID_INVALID };
        UserData Data = UserData_FromPointer(&Cues);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Cue), OnCue, &Data, &Subscription));
        uint64_t A = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        uint64_t B = MakeWindow(run, &Fixture, Box(200.0f, 100.0f, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Click(run, &Fixture, 420.0f, 112.0f);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 300.0f, 200.0f), A));
        UIWindowOptions Options = UIWindowOptions_Create(u8"New");
        Options.HasPosition = true;
        Options.Position = (Vector2){ .x = 250.0f, .y = 150.0f };
        uint64_t C = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateWindow(&Fixture.Kit, &Fixture.Screen, &Options, &C, NULL));
        Cues.WindowId = C;
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsInWindow(&Fixture, TopmostAt(&Fixture, 300.0f, 200.0f), C) && (B != C));
        TEST_CHECK(run, Cues.Opens == 1U);
        TEST_CHECK_SUCCESS(run, UIWindow_CloseNow(&Fixture.Screen, C));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Cues.Opens == 1U) && (Cues.Closes == 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A window under another widget is placed by it: it neither places itself, nor maximizes, nor shows its edges; nothing it is asked to do
 * by the mouse breaks the screen. */
static void AWindowInsideAnotherWidgetIsPlacedByIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // Its own floating box elsewhere: the holder's placement is what counts.
        Rectangle Floating = Box(600.0f, 300.0f, 200.0f, 150.0f);
        uint64_t Id = MakeWindow(run, &Fixture, Floating, NULL);
        PutInHolder(run, &Fixture, Id, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT));
        TEST_CHECK(run, IsSameBox(TestUI_GetPixelBox(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        TEST_CHECK(run, AreEdgesHidden(&Fixture, Id) && (CursorAt(run, &Fixture, 398.0f, 150.0f) == UICursorShape_Arrow));
        Drag(run, &Fixture, TITLE_X, TITLE_Y, TITLE_X + 50.0f, TITLE_Y + 50.0f);
        WaitPastDoubleClick(run, &Fixture);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        TestUI_Click(run, &Fixture, TITLE_X, TITLE_Y);
        ClickButton(run, &Fixture, Id, UIWindowButton_Maximize);
        Drag(run, &Fixture, 398.0f, 150.0f, 450.0f, 150.0f);
        TEST_CHECK(run, !IsMaximized(&Fixture, Id) && IsSameBox(TestUI_GetPixelBox(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Floating));
        TEST_CHECK_ERROR(run, UIWindow_Maximize(&Fixture.Screen, Id), ErrorCode_InvalidOperation);
        // It neither minimizes nor maximizes there: no such buttons, and code is refused; its least width counts its close button alone.
        TEST_CHECK_ERROR(run, UIWindow_Minimize(&Fixture.Screen, Id), ErrorCode_InvalidOperation);
        TEST_CHECK(run, !IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Minimize))
            && !IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Maximize)) && IsShown(&Fixture, GetButton(run, &Fixture, Id, UIWindowButton_Close)));
        TEST_CHECK(run, IsSameBox(GetButtonBox(run, &Fixture, Id, UIWindowButton_Close), Box(380.0f, 53.0f, 16.0f, 14.0f)));
        // What shows is what its holder gave it.
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT)));
        // Its other buttons still work there.
        ClickButton(run, &Fixture, Id, UIWindowButton_Close);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the framework's parts.
static void APressOnWhatKeepsTheFocusLeavesIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UITextBoxOptions Text = UITextBoxOptions_Create();
        Text.Element.HasPlacement = true;
        Text.Element.Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, 200.0f, 24.0f);
        uint64_t TextId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateTextBox(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Text, &TextId));
        UIButtonOptions Button = UIButtonOptions_Create(u8"keeps");
        Button.Element.HasPlacement = true;
        Button.Element.Placement = UIPlacement_FromTopLeft(300.0f, 10.0f, 100.0f, 24.0f);
        uint64_t ButtonId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Button, &ButtonId));
        Widget* ButtonWidget = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, ButtonId, &ButtonWidget)))
        {
            TEST_CHECK(run, !Widget_KeepsFocusOnPress(ButtonWidget));
            TEST_CHECK_SUCCESS(run, Widget_SetKeepsFocusOnPress(ButtonWidget, true));
            TEST_CHECK(run, Widget_KeepsFocusOnPress(ButtonWidget));
        }
        TestUIEvents Events;
        TestUI_RecordEvents(run, &Fixture, &Events);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 50.0f, 20.0f);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId);
        // The button keeps the focus where it is (a focusable widget that keeps it is still not focused by a press), and is clicked.
        TestUI_Click(run, &Fixture, 350.0f, 20.0f);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == TextId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == TextId));
        bool IsActivated = false;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            IsActivated = IsActivated || ((Events.Events[Index].WidgetId == ButtonId)
                && (Events.Events[Index].WidgetEventId == (int32_t)UIElementEvent_Activated));
        }
        TEST_CHECK(run, IsActivated);
        // Without the flag, the same press takes the focus.
        if (UIScreen_TryGetWidgetById(&Fixture.Screen, ButtonId, &ButtonWidget))
        {
            TEST_CHECK_SUCCESS(run, Widget_SetKeepsFocusOnPress(ButtonWidget, false));
        }
        TestUI_Click(run, &Fixture, 350.0f, 20.0f);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == ButtonId);
        TEST_CHECK_ERROR(run, Widget_SetKeepsFocusOnPress(NULL, true), ErrorCode_IllegalArgument);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheWorkspaceIsTheSurfaceLessItsStrips(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rectangle Whole = UIScreen_GetWorkspaceBox(&Fixture.Screen);
        TEST_CHECK(run, IsSameBox(Whole, Box(0.0f, 0.0f, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT)));
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        TEST_CHECK_SUCCESS(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = 10.0f, .Top = 20.0f, .Right = 30.0f, .Bottom = 40.0f }));
        TEST_CHECK(run, IsSameBox(UIScreen_GetWorkspaceBox(&Fixture.Screen), Box(10.0f, 20.0f, 460.0f, 190.0f)));
        UIInsets Insets = UIScreen_GetWorkspaceInsets(&Fixture.Screen);
        TEST_CHECK(run, (Insets.Left == 10.0f) && (Insets.Top == 20.0f) && (Insets.Right == 30.0f) && (Insets.Bottom == 40.0f));
        TEST_CHECK_SUCCESS(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = 400.0f, .Top = 0.0f, .Right = 400.0f, .Bottom = 300.0f }));
        TEST_CHECK(run, IsSameBox(UIScreen_GetWorkspaceBox(&Fixture.Screen), Box(400.0f, 0.0f, 0.0f, 0.0f)));
        TEST_CHECK_ERROR(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = -1.0f, .Top = 0.0f, .Right = 0.0f, .Bottom = 0.0f }),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = 0.0f, .Top = NAN, .Right = 0.0f, .Bottom = 0.0f }),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = 0.0f, .Top = 0.0f, .Right = -1.0f, .Bottom = 0.0f }),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = 0.0f, .Top = 0.0f, .Right = 0.0f, .Bottom = INFINITY }),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScreen_SetWorkspaceInsets(NULL, (UIInsets){ .Left = 0.0f, .Top = 0.0f, .Right = 0.0f, .Bottom = 0.0f }),
            ErrorCode_IllegalArgument);
        // A floating window keeps to it: its title bar below the top strip.
        TEST_CHECK_SUCCESS(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = 0.0f, .Top = 30.0f, .Right = 0.0f, .Bottom = 0.0f }));
        uint64_t Id = MakeWindow(run, &Fixture, Box(50.0f, 0.0f, 200.0f, 100.0f), NULL);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(50.0f, 30.0f, 200.0f, 100.0f)));
        // One with no place opens centred on the workspace: here 100 to 480 across and 30 to 250 down, at scale 2.
        TEST_CHECK_SUCCESS(run, UIScreen_SetWorkspaceInsets(&Fixture.Screen, (UIInsets){ .Left = 100.0f, .Top = 30.0f, .Right = 20.0f, .Bottom = 0.0f }));
        UIWindowOptions Options = UIWindowOptions_Create(u8"Centred");
        Options.Size = (Vector2){ .x = 200.0f, .y = 100.0f };
        uint64_t Centred = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateWindow(&Fixture.Kit, &Fixture.Screen, &Options, &Centred, NULL));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Centred), Box(190.0f, 90.0f, 200.0f, 100.0f)));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void OptionsThatCannotBeMadeAreRefused(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        static const unsigned char BadText[] = { 0xFFU, 0x41U, 0U };
        UIWindowOptions Options[6];
        for (size_t Index = 0U; Index < 6U; Index++)
        {
            Options[Index] = UIWindowOptions_Create(u8"x");
        }
        Options[0].Title = BadText;
        Options[1].Size.x = 0.0f;
        Options[2].Size.y = NAN;
        Options[3].MinimumSize.x = -1.0f;
        Options[4].HasPosition = true;
        Options[4].Position.x = INFINITY;
        Options[5].FitDown = (UIWindowFit)7;
        size_t Before = UIScreen_GetWidgetCount(&Fixture.Screen);
        for (size_t Index = 0U; Index < 6U; Index++)
        {
            TEST_CHECK(run, !UIWindowOptions_IsValid(&Options[Index]));
            uint64_t Id = 1U;
            TEST_CHECK_ERROR(run, UIKit_CreateWindow(&Fixture.Kit, &Fixture.Screen, &Options[Index], &Id, NULL), ErrorCode_ArgumentOutOfRange);
            TEST_CHECK(run, (Id == WIDGET_ID_INVALID) && (UIScreen_GetWidgetCount(&Fixture.Screen) == Before));
        }
        UIWindowOptions Fine = UIWindowOptions_Create(NULL);
        TEST_CHECK(run, UIWindowOptions_IsValid(&Fine));
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateWindow(&Fixture.Kit, &Fixture.Screen, &Fine, &Id, NULL));
        // Centred on the workspace with no position.
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(320.0f, 120.0f, 360.0f, 260.0f)));
        TEST_CHECK_ERROR(run, UIWindow_SetBox(&Fixture.Screen, Id, Box(0.0f, 0.0f, -5.0f, 10.0f)), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIWindow_SetFit(&Fixture.Screen, Id, (UIWindowFit)9, UIWindowFit_Fit), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIWindow_SetMinimumSize(&Fixture.Screen, Id, (Vector2){ .x = NAN, .y = 0.0f }), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIWindow_GetButtonId(&Fixture.Screen, Id, UIWindowButton_Count, &Id), ErrorCode_ArgumentOutOfRange);
        // Its parts are given once.
        UIWindowParts Parts = { .FrameId = WIDGET_ID_INVALID, .TitleBarId = WIDGET_ID_INVALID, .AreaId = WIDGET_ID_INVALID, .ContentId = WIDGET_ID_INVALID };
        TEST_CHECK_ERROR(run, UIWindow_SetParts(&Fixture.Screen, Id, &Parts), ErrorCode_InvalidOperation);
        // A widget that is no window is refused by id.
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t StackId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &StackId));
        TEST_CHECK(run, !UIWindow_IsWindow(&Fixture.Screen, StackId) && UIWindow_IsWindow(&Fixture.Screen, Id));
        TEST_CHECK_ERROR(run, UIWindow_Minimize(&Fixture.Screen, StackId), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheTitleAndTheIconChange(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = MakeWindow(run, &Fixture, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        const unsigned char* Title = NULL;
        TEST_CHECK_SUCCESS(run, UIWindow_GetTitle(&Fixture.Screen, Id, &Title));
        TEST_CHECK(run, (Title != NULL) && (strcmp((const char*)Title, "Window") == 0));
        TEST_CHECK_SUCCESS(run, UIWindow_SetTitle(&Fixture.Screen, Id, u8"Renamed"));
        TEST_CHECK_SUCCESS(run, UIWindow_GetTitle(&Fixture.Screen, Id, &Title));
        TEST_CHECK(run, strcmp((const char*)Title, "Renamed") == 0);
        UIElementIcon Icon = UIElementIcon_FromLetter((uint32_t)'W');
        TEST_CHECK_SUCCESS(run, UIWindow_SetIcon(&Fixture.Screen, Id, &Icon));
        UIElementIcon Bad = UIElementIcon_FromLetter(0U);
        TEST_CHECK_ERROR(run, UIWindow_SetIcon(&Fixture.Screen, Id, &Bad), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIWindow_SetTitle(&Fixture.Screen, Id, NULL), ErrorCode_IllegalArgument);
        TestUI_Ticks(run, &Fixture, 2U);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: arithmetic.
static void TheArithmeticKeepsItReachable(TestRun* run)
{
    Rectangle Workspace = Box(0.0f, 0.0f, 800.0f, 600.0f);
    // Inside: unmoved.
    TEST_CHECK(run, IsSameBox(UIWindow_KeepReachable(Box(10.0f, 10.0f, 300.0f, 200.0f), Workspace, 20.0f, 56.0f, 48.0f),
        Box(10.0f, 10.0f, 300.0f, 200.0f)));
    // Off the left: the buttons and 48 of what drags stay on.
    TEST_CHECK(run, IsSameBox(UIWindow_KeepReachable(Box(-1000.0f, 10.0f, 300.0f, 200.0f), Workspace, 20.0f, 56.0f, 48.0f),
        Box(48.0f - 244.0f, 10.0f, 300.0f, 200.0f)));
    // Off the right and the bottom: 48 at the left, the title bar's height at the bottom.
    TEST_CHECK(run, IsSameBox(UIWindow_KeepReachable(Box(2000.0f, 900.0f, 300.0f, 200.0f), Workspace, 20.0f, 56.0f, 48.0f),
        Box(752.0f, 580.0f, 300.0f, 200.0f)));
    // Off the top.
    TEST_CHECK(run, IsSameBox(UIWindow_KeepReachable(Box(10.0f, -50.0f, 300.0f, 200.0f), Workspace, 20.0f, 56.0f, 48.0f),
        Box(10.0f, 0.0f, 300.0f, 200.0f)));
    // A draggable part shorter than 48 stays on whole; a workspace narrower than 48 keeps its width of it; one shorter than the title bar
    // keeps it at its top.
    TEST_CHECK(run, IsSameBox(UIWindow_KeepReachable(Box(-500.0f, 0.0f, 80.0f, 50.0f), Workspace, 20.0f, 56.0f, 48.0f), Box(0.0f, 0.0f, 80.0f, 50.0f)));
    TEST_CHECK(run, IsSameBox(UIWindow_KeepReachable(Box(500.0f, 500.0f, 300.0f, 200.0f), Box(0.0f, 0.0f, 30.0f, 10.0f), 20.0f, 56.0f, 48.0f),
        Box(0.0f, 0.0f, 300.0f, 200.0f)));
    // A workspace away from the origin.
    TEST_CHECK(run, IsSameBox(UIWindow_KeepReachable(Box(0.0f, 0.0f, 300.0f, 200.0f), Box(100.0f, 40.0f, 600.0f, 400.0f), 20.0f, 56.0f, 48.0f),
        Box(0.0f, 40.0f, 300.0f, 200.0f)));
}

static void TheArithmeticResizesWithinItsLimits(TestRun* run)
{
    Rectangle Workspace = Box(0.0f, 0.0f, 800.0f, 600.0f);
    Rectangle Start = Box(100.0f, 100.0f, 300.0f, 200.0f);
    Vector2 Least = { .x = 120.0f, .y = 80.0f };
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Start, UIWindowEdge_Right, (Vector2){ 1000.0f, 50.0f }, Least, Workspace, 20.0f),
        Box(100.0f, 100.0f, 700.0f, 200.0f)));
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Start, UIWindowEdge_Left, (Vector2){ 1000.0f, 0.0f }, Least, Workspace, 20.0f),
        Box(280.0f, 100.0f, 120.0f, 200.0f)));
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Start, UIWindowEdge_TopLeft, (Vector2){ -1000.0f, -1000.0f }, Least, Workspace, 20.0f),
        Box(0.0f, 0.0f, 400.0f, 300.0f)));
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Start, UIWindowEdge_BottomRight, (Vector2){ -1000.0f, -1000.0f }, Least, Workspace, 20.0f),
        Box(100.0f, 100.0f, 120.0f, 80.0f)));
    // An edge that began past the workspace's stops where it began, never jumping in.
    Rectangle Beyond = Box(600.0f, 100.0f, 300.0f, 200.0f);
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Beyond, UIWindowEdge_Right, (Vector2){ 50.0f, 0.0f }, Least, Workspace, 20.0f), Beyond));
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Beyond, UIWindowEdge_Right, (Vector2){ -50.0f, 0.0f }, Least, Workspace, 20.0f),
        Box(600.0f, 100.0f, 250.0f, 200.0f)));
    // A top edge stays a title bar above the workspace's bottom, unless the least height keeps it higher.
    Rectangle Tall = Box(100.0f, 100.0f, 300.0f, 700.0f);
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Tall, UIWindowEdge_Top, (Vector2){ 0.0f, 1000.0f }, Least, Workspace, 20.0f),
        Box(100.0f, 580.0f, 300.0f, 220.0f)));
    // The least size wins over the workspace.
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Box(0.0f, 0.0f, 50.0f, 50.0f), UIWindowEdge_Left, (Vector2){ 0.0f, 0.0f }, Least, Workspace, 20.0f),
        Box(-70.0f, 0.0f, 120.0f, 50.0f)));
    // Edges that began past the workspace's left, top and bottom stay where they began: none jumps in.
    Rectangle Out = Box(-50.0f, -20.0f, 300.0f, 700.0f);
    static const UIWindowEdge Sides[] = { UIWindowEdge_Left, UIWindowEdge_Top, UIWindowEdge_Bottom, UIWindowEdge_TopLeft, UIWindowEdge_BottomLeft };
    for (size_t Index = 0U; Index < (sizeof(Sides) / sizeof(Sides[0])); Index++)
    {
        TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Out, Sides[Index], (Vector2){ 0.0f, 0.0f }, Least, Workspace, 20.0f), Out));
    }
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Out, UIWindowEdge_Left, (Vector2){ 10.0f, 0.0f }, Least, Workspace, 20.0f),
        Box(-40.0f, -20.0f, 290.0f, 700.0f)));
    // On a workspace lower than the title bar, a top edge stays where it is.
    TEST_CHECK(run, IsSameBox(UIWindow_ResizeBox(Box(0.0f, 0.0f, 300.0f, 200.0f), UIWindowEdge_Top, (Vector2){ 0.0f, 0.0f }, Least,
        Box(0.0f, 0.0f, 800.0f, 10.0f), 20.0f), Box(0.0f, 0.0f, 300.0f, 200.0f)));
}


// Tests: a world screen.
static void WorldBoxTick(TestRun* run, TestUIFixture* fixture, WorldScreenHost* host, const WorldScreenHit* pointed)
{
    GameInput_BeginTick(&fixture->Input, &fixture->Sample);
    fixture->Time += TEST_UI_TICK_SECONDS;
    TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(host, &fixture->Screen, ProgramTime_Create(fixture->Time, TEST_UI_TICK_SECONDS),
        &fixture->Input, pointed));
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        uint8_t State = fixture->Sample.KeyStates[Index];
        fixture->Sample.KeyStates[Index] = ((State & GAME_INPUT_KEY_STATE_RELEASED) != 0U) ? 0U : (uint8_t)(State & GAME_INPUT_KEY_STATE_DOWN);
    }
    fixture->Sample.TextCodepointCount = 0U;
}

static WorldScreenHit HitAtPixel(float x, float y)
{
    Vector2 Uv = { .x = x / TEST_UI_SURFACE_WIDTH, .y = y / TEST_UI_SURFACE_HEIGHT };
    return (WorldScreenHit){ .ObjectId = 7U, .Surface = NULL, .SurfaceIndex = 0U, .Uv = Uv, .Picture = Uv, .IsOnPicture = true,
        .Point = Vector3Double_Zero(), .Distance = 1.0f };
}

/* Windows on a screen in the world need nothing of their own: the title bar moves one where the player points, an edge resizes it. */
static void AWindowWorksOnAWorldScreen(TestRun* run)
{
    TestUIFixture Fixture;
    WorldScreenHost Host;
    WorldScreenHost_Construct(&Host, 7U);
    if (Construct(run, &Fixture))
    {
        UIWindowOptions Options = UIWindowOptions_Create(u8"On the monitor");
        uint64_t Id = MakeWindowWith(run, &Fixture, &Options, Box(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT), NULL);
        WorldScreenHit OnTitle = HitAtPixel(TITLE_X, TITLE_Y);
        WorldBoxTick(run, &Fixture, &Host, &OnTitle);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        WorldBoxTick(run, &Fixture, &Host, &OnTitle);
        WorldScreenHit Moved = HitAtPixel(TITLE_X + 100.0f, TITLE_Y + 50.0f);
        WorldBoxTick(run, &Fixture, &Host, &Moved);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        WorldBoxTick(run, &Fixture, &Host, &Moved);
        WorldBoxTick(run, &Fixture, &Host, &Moved);
        TEST_CHECK(run, IsSameBox(GetShown(run, &Fixture, Id), Box(WINDOW_X + 100.0f, WINDOW_Y + 50.0f, WINDOW_WIDTH, WINDOW_HEIGHT)));
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Host) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id));
        // Its right edge, pointed at.
        WorldScreenHit OnEdge = HitAtPixel(498.0f, 200.0f);
        WorldBoxTick(run, &Fixture, &Host, &OnEdge);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        WorldBoxTick(run, &Fixture, &Host, &OnEdge);
        WorldScreenHit Wider = HitAtPixel(538.0f, 200.0f);
        WorldBoxTick(run, &Fixture, &Host, &Wider);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        WorldBoxTick(run, &Fixture, &Host, &Wider);
        TEST_CHECK(run, IsSameBox(GetWanted(run, &Fixture, Id), Box(WINDOW_X + 100.0f, WINDOW_Y + 50.0f, WINDOW_WIDTH + 40.0f, WINDOW_HEIGHT)));
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Release(&Host, &Fixture.Screen));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "the_title_bar_moves_it", TheTitleBarMovesIt },
    { "it_moves_by_the_screens_scale_and_keeps_its_own_reachable", ItMovesByTheScreensScaleAndKeepsItsOwnReachable },
    { "every_edge_and_corner_resizes_it", EveryEdgeAndCornerResizesIt },
    { "a_short_window_keeps_some_edge_between_its_corners", AShortWindowKeepsSomeEdgeBetweenItsCorners },
    { "its_edges_show_only_while_it_can_be_resized", ItsEdgesShowOnlyWhileItCanBeResized },
    { "resizing_stops_at_its_least_size_and_the_workspace", ResizingStopsAtItsLeastSizeAndTheWorkspace },
    { "its_least_size_fits_its_content", ItsLeastSizeFitsItsContent },
    { "content_that_wraps_is_measured_at_the_width_it_gets", ContentThatWrapsIsMeasuredAtTheWidthItGets },
    { "its_title_bar_has_room_for_its_icon_and_buttons", ItsTitleBarHasRoomForItsIconAndButtons },
    { "its_caption_buttons_follow_its_state", ItsCaptionButtonsFollowItsState },
    { "it_stays_reachable_when_the_surface_shrinks", ItStaysReachableWhenTheSurfaceShrinks },
    { "minimize_and_restore_keep_its_size_and_what_is_inside", MinimizeAndRestoreKeepItsSizeAndWhatIsInside },
    { "maximize_fills_the_workspace_and_restores_exactly", MaximizeFillsTheWorkspaceAndRestoresExactly },
    { "one_window_is_maximized_at_a_time", OneWindowIsMaximizedAtATime },
    { "a_double_click_on_the_title_bar_maximizes_and_restores", ADoubleClickOnTheTitleBarMaximizesAndRestores },
    { "closing_asks_its_query_first", ClosingAsksItsQueryFirst },
    { "a_destroyed_window_is_let_go_mid_drag", ADestroyedWindowIsLetGoMidDrag },
    { "a_press_on_the_title_bar_acts_on_nothing_inside", APressOnTheTitleBarActsOnNothingInside },
    { "with_nothing_remembered_the_focus_goes_to_its_target_or_itself", WithNothingRememberedTheFocusGoesToItsTargetOrItself },
    { "the_focus_goes_back_only_to_what_can_take_it_inside", TheFocusGoesBackOnlyToWhatCanTakeItInside },
    { "it_remembers_the_focus_between_its_updates", ItRemembersTheFocusBetweenItsUpdates },
    { "a_popup_from_inside_leaves_it_active", APopupFromInsideLeavesItActive },
    { "a_press_on_an_edge_acts_on_nothing_inside", APressOnAnEdgeActsOnNothingInside },
    { "only_the_left_button_drags_it", OnlyTheLeftButtonDragsIt },
    { "its_states_end_a_drag_and_stack", ItsStatesEndADragAndStack },
    { "restored_it_gives_the_focus_back_inside", RestoredItGivesTheFocusBackInside },
    { "a_double_click_acts_on_nothing_inside", ADoubleClickActsOnNothingInside },
    { "without_the_keyboard_it_focuses_but_enters_nothing", WithoutTheKeyboardItFocusesButEntersNothing },
    { "its_title_bar_always_leaves_something_to_drag", ItsTitleBarAlwaysLeavesSomethingToDrag },
    { "a_press_raises_it_within_its_layer", APressRaisesItWithinItsLayer },
    { "its_scale_zooms_what_it_holds", ItsScaleZoomsWhatItHolds },
    { "a_new_window_opens_on_top_and_tells_its_cues", ANewWindowOpensOnTopAndTellsItsCues },
    { "a_window_inside_another_widget_is_placed_by_it", AWindowInsideAnotherWidgetIsPlacedByIt },
    { "a_press_on_what_keeps_the_focus_leaves_it", APressOnWhatKeepsTheFocusLeavesIt },
    { "the_workspace_is_the_surface_less_its_strips", TheWorkspaceIsTheSurfaceLessItsStrips },
    { "options_that_cannot_be_made_are_refused", OptionsThatCannotBeMadeAreRefused },
    { "the_title_and_the_icon_change", TheTitleAndTheIconChange },
    { "the_arithmetic_keeps_it_reachable", TheArithmeticKeepsItReachable },
    { "the_arithmetic_resizes_within_its_limits", TheArithmeticResizesWithinItsLimits },
    { "a_window_works_on_a_world_screen", AWindowWorksOnAWorldScreen },
};


// Public functions.
void UIWindowTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIWindow", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
