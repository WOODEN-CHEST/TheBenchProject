#include "TestSuites.h"
#include "TestUI.h"
#include "UIKit.h"
#include "UIPopup.h"
#include "UIScrollArea.h"
#include "UIScrollable.h"
#include "UIWidgetFactory.h"
#include "wr/WRUserData.h"
#include <math.h>


// Macros.
/** The fixture's look: panels with no padding and a 120 x 80 smallest size, buttons at least 24 x 24. */
#define POPUP_THEME \
    "{\"name\": \"popups\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"min_size\": [120, 80], \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"slider\": {\"min_size\": [64, 24], \"thumb_size\": [20, 20], \"track\": {\"kind\": \"flat\"}, \"fill\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"flat\"}}," \
    "\"backdrop\": {\"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#00000080\"}}}}}"
#define POPUP_WIDTH 120.0f
#define POPUP_HEIGHT 80.0f


// Types.
/* The popup closed notifications a screen delivered. */
typedef struct ClosedRecordStruct
{
    uint64_t Owners[8];
    int32_t Reasons[8];
    size_t Count;
} ClosedRecord;


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, POPUP_THEME);
}

static uint64_t AddButtonAt(TestRun* run, TestUIFixture* fixture, uint64_t parentId, float x, float y, float width, float height)
{
    UIButtonOptions Options = UIButtonOptions_Create(u8"b");
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(x, y, width, height);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&fixture->Kit, &fixture->Screen, parentId, &Options, &Id));
    return Id;
}

/* A panel made as a root, to open as a popup: 120 x 80 by the theme. */
static uint64_t AddPopupPanel(TestRun* run, TestUIFixture* fixture)
{
    UIPanelOptions Options = UIPanelOptions_Create();
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    return Id;
}

static UIPopupOptions BelowWidget(uint64_t anchorId)
{
    UIPopupOptions Options = UIPopupOptions_Create();
    Options.AnchorWidgetId = anchorId;
    Options.OwnerId = anchorId;
    return Options;
}

static Widget* Get(TestUIFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found);
    return Found;
}

static Error RecordClosed(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    ClosedRecord* Record = UserData_GetPointer(userData);
    if ((event->Kind == UIEventKind_PopupClosed) && (Record->Count < 8U))
    {
        Record->Owners[Record->Count] = event->WidgetId;
        Record->Reasons[Record->Count] = event->WidgetEventId;
        Record->Count++;
    }
    return Error_CreateSuccess();
}

static void RecordClosedEvents(TestRun* run, TestUIFixture* fixture, ClosedRecord* record)
{
    record->Count = 0U;
    UserData Data = UserData_FromPointer(record);
    uint64_t Subscription = 0U;
    TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&fixture->Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_PopupClosed), RecordClosed, &Data,
        &Subscription));
}

static bool IsBoxAt(Rectangle box, float x, float y, float width, float height)
{
    return TestUI_IsBox(box, x, y, width, height);
}

/* An edit element that writes down how it was left. */
typedef struct LeaveProbeStruct
{
    Widget Base;
    int32_t* LeaveReason;
} LeaveProbe;

static Error LeaveProbe_Construct(void* widget, void* args)
{
    ((LeaveProbe*)widget)->LeaveReason = args;
    return Error_CreateSuccess();
}

static Error LeaveProbe_OnLeave(void* self, UILeaveReason reason)
{
    LeaveProbe* Probe = self;
    *Probe->LeaveReason = (int32_t)reason;
    return Error_CreateSuccess();
}

static const WidgetVTable LeaveProbeVTable = { .OnLeave = LeaveProbe_OnLeave };


// Tests: the arithmetic.
static void ThePlacementsArithmetic(TestRun* run)
{
    Vector2 Surface = { .x = 1000.0f, .y = 500.0f };
    Vector2 Size = { .x = 120.0f, .y = 80.0f };
    UIPopupPlacementRules Below = { .Side = UIPopupSide_Below, .Flips = true, .AllowsShrinking = false, .MinimumLength = 0.0f };
    Rectangle Anchor = { .x = 100.0f, .y = 100.0f, .width = 60.0f, .height = 20.0f };
    // Room below: under the anchor, left edges lined up.
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Below), 100.0f, 120.0f, 120.0f, 80.0f));
    // At the bottom it flips above; with too little room both ways it takes the larger side and stays on the surface.
    Anchor.y = 450.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Below), 100.0f, 370.0f, 120.0f, 80.0f));
    Vector2 Tall = { .x = 120.0f, .y = 470.0f };
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Tall, Surface, &Below), 100.0f, 0.0f, 120.0f, 470.0f));
    // Without flipping it stays below, moved up to stay on the surface (covering the anchor).
    UIPopupPlacementRules Forced = Below;
    Forced.Flips = false;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Forced), 100.0f, 420.0f, 120.0f, 80.0f));
    // Shrinking: to the room on its side, never below the minimum (then moved to stay on the surface).
    Forced.AllowsShrinking = true;
    Forced.MinimumLength = 22.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Forced), 100.0f, 470.0f, 120.0f, 30.0f));
    Anchor.y = 490.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Forced), 100.0f, 478.0f, 120.0f, 22.0f));
    UIPopupPlacementRules Shrinks = Below;
    Shrinks.AllowsShrinking = true;
    Anchor.y = 200.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, (Vector2){ .x = 120.0f, .y = 400.0f }, Surface, &Shrinks), 100.0f, 220.0f, 120.0f,
        280.0f));
    // Above: over the anchor, flipping below at the top.
    UIPopupPlacementRules Above = Below;
    Above.Side = UIPopupSide_Above;
    Anchor.y = 300.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Above), 100.0f, 220.0f, 120.0f, 80.0f));
    Anchor.y = 30.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Above), 100.0f, 50.0f, 120.0f, 80.0f));
    // Across, it stays on the surface at either side.
    Anchor = (Rectangle){ .x = 950.0f, .y = 100.0f, .width = 40.0f, .height = 20.0f };
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Below), 880.0f, 120.0f, 120.0f, 80.0f));
    Anchor.x = -30.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Below), 0.0f, 120.0f, 120.0f, 80.0f));
    // Right of an item, a submenu flips left at the surface's right edge; at the bottom it moves up.
    UIPopupPlacementRules Right = Below;
    Right.Side = UIPopupSide_Right;
    Anchor = (Rectangle){ .x = 400.0f, .y = 100.0f, .width = 200.0f, .height = 20.0f };
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Right), 600.0f, 100.0f, 120.0f, 80.0f));
    Anchor.x = 800.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Right), 680.0f, 100.0f, 120.0f, 80.0f));
    Anchor.y = 470.0f;
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Right), 680.0f, 420.0f, 120.0f, 80.0f));
    UIPopupPlacementRules Left = Right;
    Left.Side = UIPopupSide_Left;
    Anchor = (Rectangle){ .x = 50.0f, .y = 100.0f, .width = 40.0f, .height = 20.0f };
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, Size, Surface, &Left), 90.0f, 100.0f, 120.0f, 80.0f));
    // Centred: on the anchor's middle, never flipped, kept on the surface and no larger than it.
    UIPopupPlacementRules Centre = Below;
    Centre.Side = UIPopupSide_Center;
    Rectangle Whole = { .x = 0.0f, .y = 0.0f, .width = 1000.0f, .height = 500.0f };
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Whole, Size, Surface, &Centre), 440.0f, 210.0f, 120.0f, 80.0f));
    Rectangle Corner = { .x = 980.0f, .y = 490.0f, .width = 10.0f, .height = 10.0f };
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Corner, Size, Surface, &Centre), 880.0f, 420.0f, 120.0f, 80.0f));
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Whole, (Vector2){ .x = 1200.0f, .y = 30.0f }, Surface, &Centre), 0.0f, 235.0f, 1000.0f, 30.0f));
    // Larger than the surface: the surface. Sizes that are no numbers: nothing. A point anchor. Whole pixels.
    TEST_CHECK(run, IsBoxAt(UIPopupPlacement_Resolve(Anchor, (Vector2){ .x = 3000.0f, .y = 3000.0f }, Surface, &Below), 0.0f, 0.0f, 1000.0f,
        500.0f));
    Rectangle None = UIPopupPlacement_Resolve(Anchor, (Vector2){ .x = NAN, .y = -4.0f }, Surface, &Below);
    TEST_CHECK(run, (None.width == 0.0f) && (None.height == 0.0f));
    Rectangle Point = UIPopupPlacement_Resolve((Rectangle){ .x = 300.4f, .y = 200.6f, .width = 0.0f, .height = 0.0f }, (Vector2){ .x = 50.3f, .y = 20.2f },
        Surface, &Below);
    TEST_CHECK(run, IsBoxAt(Point, 300.0f, 201.0f, 51.0f, 20.0f));
}


// Tests: the layer.
static void APopupOpensAboveEverythingAndFollowsItsAnchor(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AnchorId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t FloatingId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 130.0f, 200.0f, 40.0f);
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(&Fixture.Screen, Get(&Fixture, FloatingId), UIScreenLayer_Floating));
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = BelowWidget(AnchorId);
        Options.MatchesAnchorWidth = true;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (UIScreen_GetPopupCount(&Fixture.Screen) == 1U));
        TEST_CHECK(run, (UIScreen_GetPopupAt(&Fixture.Screen, 0U) == PopupId) && (UIScreen_GetPopupAt(&Fixture.Screen, 1U) == WIDGET_ID_INVALID));
        TEST_CHECK(run, UIScreen_GetRootLayer(&Fixture.Screen, Get(&Fixture, PopupId)) == UIScreenLayer_Popup);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsBoxAt(TestUI_GetPixelBox(run, &Fixture, PopupId), 100.0f, 120.0f, POPUP_WIDTH, POPUP_HEIGHT));
        // Over the floating root, the popup is hit.
        Widget* Top = NULL;
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 150.0f / 1000.0f, .y = 150.0f / 500.0f }, &Top)
            && (Widget_GetId(Top) == PopupId));
        TEST_CHECK(run, UIScreen_FindPopupOf(&Fixture.Screen, PopupId) == PopupId);
        TEST_CHECK(run, UIScreen_FindPopupOf(&Fixture.Screen, AnchorId) == WIDGET_ID_INVALID);

        // It follows its anchor, and keeps its place when the surface changes size; a wider anchor makes it wider.
        UIPlacement Moved = UIPlacement_FromTopLeft(300.0f, 50.0f, 200.0f, 20.0f);
        TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Get(&Fixture, AnchorId), &Moved));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsBoxAt(TestUI_GetPixelBox(run, &Fixture, PopupId), 300.0f, 70.0f, 200.0f, POPUP_HEIGHT));
        TestUI_SetSurface(run, &Fixture, 400.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        // At 400 x 120 the anchor still sits at (300, 50): too little room below or above for 80, so it stays below, moved to stay on the
        // surface, and across too.
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, PopupId);
        TEST_CHECK(run, (Box.x == 200.0f) && (Box.width == 200.0f) && (Box.y >= 0.0f) && ((Box.y + Box.height) <= 120.0f));
        TestUI_SetSurface(run, &Fixture, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsBoxAt(TestUI_GetPixelBox(run, &Fixture, PopupId), 300.0f, 70.0f, 200.0f, POPUP_HEIGHT));
        // Its anchor gone, it stays where it was last.
        TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Screen, Get(&Fixture, AnchorId)));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId)); // Its owner, the same widget, went with it.
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APressOutsideClosesItAndGoesOn(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    ClosedRecord Closed;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        RecordClosedEvents(run, &Fixture, &Closed);
        uint64_t AnchorId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t BehindId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 600.0f, 300.0f, 100.0f, 40.0f);
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        uint64_t InsideId = AddButtonAt(run, &Fixture, PopupId, 10.0f, 10.0f, 50.0f, 30.0f);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = BelowWidget(AnchorId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);

        // A click inside does what it does and keeps it open.
        TestUI_Click(run, &Fixture, 130.0f, 140.0f);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, PopupId));
        TEST_CHECK(run, (Events.Count == 1U) && (Events.Events[0].WidgetId == InsideId)
            && (Events.Events[0].WidgetEventId == (int32_t)UIElementEvent_Activated));

        // A click on a button behind closes it, and the button still gets its click.
        TestUI_Click(run, &Fixture, 650.0f, 320.0f);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (Get(&Fixture, PopupId) == NULL));
        TEST_CHECK(run, (Events.Count == 2U) && (Events.Events[1].WidgetId == BehindId)
            && (Events.Events[1].WidgetEventId == (int32_t)UIElementEvent_Activated));
        TEST_CHECK(run, (Closed.Count == 1U) && (Closed.Owners[0] == AnchorId) && (Closed.Reasons[0] == (int32_t)UIPopupCloseReason_PressOutside));

        // A press on nothing closes one too, and clears the focus as ever.
        PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 900.0f, 450.0f);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
        TEST_CHECK(run, (Closed.Count == 2U) && (Closed.Reasons[1] == (int32_t)UIPopupCloseReason_PressOutside));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapeClosesTheTopmostAndTheFocusComesBack(TestRun* run)
{
    TestUIFixture Fixture;
    ClosedRecord Closed;
    if (Construct(run, &Fixture))
    {
        RecordClosedEvents(run, &Fixture, &Closed);
        // The opener: a slider, entered.
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Element.HasPlacement = true;
        Slider.Element.Placement = UIPlacement_FromTopLeft(100.0f, 100.0f, 200.0f, 24.0f);
        uint64_t SliderId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Slider, &SliderId));
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        uint64_t InsideId = AddButtonAt(run, &Fixture, PopupId, 10.0f, 10.0f, 50.0f, 30.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, SliderId));

        UIPopupOptions Options = BelowWidget(SliderId);
        Options.FocusId = InsideId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == InsideId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
        TestUI_Ticks(run, &Fixture, 2U);
        // Escape: closed, and the slider has the focus back, entered again.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == SliderId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == SliderId));
        TEST_CHECK(run, (Closed.Count == 1U) && (Closed.Reasons[0] == (int32_t)UIPopupCloseReason_Escape));
        // That Escape went to the popup alone: the slider is still entered, and a second one leaves it.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == SliderId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));

        // An entered element inside the popup is left with a cancel, then the popup closes, in one press.
        PopupId = AddPopupPanel(run, &Fixture);
        UISliderOptions Inner = UISliderOptions_Create();
        Inner.Element.HasPlacement = true;
        Inner.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 100.0f, 24.0f);
        uint64_t InnerId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, PopupId, &Inner, &InnerId));
        TestUI_Tick(run, &Fixture);
        Options.FocusId = InnerId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == InnerId); // An edit element is entered at once.
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == SliderId));

        // A popup that does not close on Escape lets it go to the rules under it.
        PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        Options.FocusId = WIDGET_ID_INVALID;
        Options.ClosesOnEscape = false;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID); // Moved in, to nothing.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, PopupId));
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(&Fixture.Screen));
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == SliderId));
        TestUI_Tick(run, &Fixture); // Notifications are delivered at the end of an update.
        TEST_CHECK(run, Closed.Reasons[Closed.Count - 1U] == (int32_t)UIPopupCloseReason_Code);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void NestedPopupsCloseFromTheInsideOut(TestRun* run)
{
    TestUIFixture Fixture;
    ClosedRecord Closed;
    if (Construct(run, &Fixture))
    {
        RecordClosedEvents(run, &Fixture, &Closed);
        uint64_t AnchorId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t OuterId = AddPopupPanel(run, &Fixture);
        uint64_t ItemId = AddButtonAt(run, &Fixture, OuterId, 0.0f, 0.0f, 120.0f, 24.0f);
        uint64_t InnerId = AddPopupPanel(run, &Fixture);
        uint64_t InnerItemId = AddButtonAt(run, &Fixture, InnerId, 0.0f, 0.0f, 120.0f, 24.0f);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Outer = BelowWidget(AnchorId);
        Outer.FocusId = ItemId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, OuterId, &Outer));
        TestUI_Ticks(run, &Fixture, 2U);
        UIPopupOptions Inner = BelowWidget(ItemId);
        Inner.Side = UIPopupSide_Right;
        Inner.FocusId = InnerItemId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, InnerId, &Inner));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsBoxAt(TestUI_GetPixelBox(run, &Fixture, InnerId), 220.0f, 120.0f, POPUP_WIDTH, POPUP_HEIGHT));

        // A press in the outer one (not on the inner's owner) closes the inner one alone.
        TestUI_Click(run, &Fixture, 150.0f, 180.0f);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, OuterId) && !UIScreen_IsPopupOpen(&Fixture.Screen, InnerId));
        TEST_CHECK(run, (Closed.Count == 1U) && (Closed.Owners[0] == ItemId));

        // Open again; Escape closes one level at a time, and the focus steps back with it.
        InnerId = AddPopupPanel(run, &Fixture);
        InnerItemId = AddButtonAt(run, &Fixture, InnerId, 0.0f, 0.0f, 120.0f, 24.0f);
        TestUI_Tick(run, &Fixture);
        Inner.FocusId = InnerItemId;
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Get(&Fixture, ItemId)));
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, InnerId, &Inner));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, OuterId) && !UIScreen_IsPopupOpen(&Fixture.Screen, InnerId));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == ItemId);

        // Closing the outer one by code closes the inner first; the inner's owner went with the outer one, so only the outer's owner hears.
        InnerId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, InnerId, &Inner));
        size_t Before = Closed.Count;
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(&Fixture.Screen, OuterId));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetPopupCount(&Fixture.Screen) == 0U) && (Closed.Count == (Before + 1U)));
        TEST_CHECK(run, (Closed.Owners[Before] == AnchorId) && (Closed.Reasons[Before] == (int32_t)UIPopupCloseReason_Code));
        TEST_CHECK_ERROR(run, UIScreen_ClosePopup(&Fixture.Screen, OuterId), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APopupClosedWithItsOwnLeavesTheOthers(TestRun* run)
{
    TestUIFixture Fixture;
    ClosedRecord Closed;
    if (Construct(run, &Fixture))
    {
        RecordClosedEvents(run, &Fixture, &Closed);
        uint64_t AnchorId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t OtherAnchorId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 400.0f, 100.0f, 60.0f, 20.0f);
        // The first popup; one opened from inside it (its owner the first's item); and one with an owner outside, above them both.
        uint64_t FirstId = AddPopupPanel(run, &Fixture);
        uint64_t ItemId = AddButtonAt(run, &Fixture, FirstId, 0.0f, 0.0f, 120.0f, 24.0f);
        uint64_t NestedId = AddPopupPanel(run, &Fixture);
        uint64_t OtherId = AddPopupPanel(run, &Fixture);
        uint64_t OtherItemId = AddButtonAt(run, &Fixture, OtherId, 0.0f, 0.0f, 120.0f, 24.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Get(&Fixture, AnchorId)));
        UIPopupOptions First = BelowWidget(AnchorId);
        First.FocusId = ItemId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, FirstId, &First));
        TestUI_Ticks(run, &Fixture, 2U);
        UIPopupOptions Nested = BelowWidget(ItemId);
        Nested.Side = UIPopupSide_Right;
        Nested.MovesFocus = false;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, NestedId, &Nested));
        TestUI_Ticks(run, &Fixture, 2U);
        UIPopupOptions Other = BelowWidget(OtherAnchorId);
        Other.FocusId = OtherItemId; // Its focus would go back to the first's item.
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, OtherId, &Other));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 3U);
        // Closed with its own: the first and the nested one go (the nested one's owner went with the first, so only the first's owner
        // hears), and the other stays where it was, focused.
        Closed.Count = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopupAndItsOwn(&Fixture.Screen, FirstId));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, FirstId) && !UIScreen_IsPopupOpen(&Fixture.Screen, NestedId)
            && UIScreen_IsPopupOpen(&Fixture.Screen, OtherId) && (UIScreen_GetPopupCount(&Fixture.Screen) == 1U));
        TEST_CHECK(run, (Closed.Count == 1U) && (Closed.Owners[0] == AnchorId));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == OtherItemId);
        // The other closed: the focus goes back where the first would have given it, not to its item that is gone.
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(&Fixture.Screen, OtherId));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == AnchorId);
        TEST_CHECK_ERROR(run, UIScreen_ClosePopupAndItsOwn(&Fixture.Screen, OtherId), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScreen_ClosePopupAndItsOwn(NULL, OtherId), ErrorCode_IllegalArgument);
    }
    TestUI_Deconstruct(run, &Fixture);
}


static void TheOwnerGoingAwayClosesIt(TestRun* run)
{
    TestUIFixture Fixture;
    ClosedRecord Closed;
    if (Construct(run, &Fixture))
    {
        RecordClosedEvents(run, &Fixture, &Closed);
        uint64_t OwnerId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = BelowWidget(OwnerId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Tick(run, &Fixture);
        // Hidden: it closes at the next update, and the owner is told.
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Get(&Fixture, OwnerId), false));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId));
        TEST_CHECK(run, (Closed.Count == 1U) && (Closed.Reasons[0] == (int32_t)UIPopupCloseReason_OwnerGone));
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Get(&Fixture, OwnerId), true));

        // Destroyed: it closes too, with nobody to tell.
        PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Get(&Fixture, OwnerId)));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (Get(&Fixture, PopupId) == NULL) && (Closed.Count == 1U));

        // Its root destroyed by someone: it is closed.
        PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Unowned = UIPopupOptions_Create();
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Unowned));
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Get(&Fixture, PopupId)));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetPopupCount(&Fixture.Screen) == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AModalPopupTakesEveryPress(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t BehindId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 600.0f, 300.0f, 100.0f, 40.0f);
        uint64_t DialogId = AddPopupPanel(run, &Fixture);
        uint64_t YesId = AddButtonAt(run, &Fixture, DialogId, 10.0f, 10.0f, 50.0f, 30.0f);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = UIPopupOptions_Create();
        Options.AnchorBox = (Rectangle){ .x = 0.4f, .y = 0.4f, .width = 0.0f, .height = 0.0f };
        Options.IsModal = true;
        Options.FocusId = YesId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, DialogId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsBoxAt(TestUI_GetPixelBox(run, &Fixture, DialogId), 400.0f, 200.0f, POPUP_WIDTH, POPUP_HEIGHT));

        // Behind it nothing is hovered, pressed or scrolled, and the focus stays in the dialog.
        TestUI_MovePointer(&Fixture, 650.0f, 320.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) != BehindId);
        TestUI_Click(run, &Fixture, 650.0f, 320.0f);
        TEST_CHECK(run, (Events.Count == 0U) && UIScreen_IsPopupOpen(&Fixture.Screen, DialogId));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == YesId);
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GameInput_GetWheelDelta(&Fixture.Input).y == 0.0f);
        // Its own buttons work.
        TestUI_Click(run, &Fixture, 430.0f, 220.0f);
        TEST_CHECK(run, (Events.Count == 1U) && (Events.Events[0].WidgetId == YesId));
        // A popup over it closes on a press on the backdrop; the dialog stays.
        uint64_t OverId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Over = BelowWidget(YesId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, OverId, &Over));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 900.0f, 20.0f);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, OverId) && UIScreen_IsPopupOpen(&Fixture.Screen, DialogId));
        // A press while the screen has no pointer (the cursor taken for the world) is no press of its: the dialog stays.
        TestUI_RemovePointer(&Fixture);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, DialogId));
        // Closed, the backdrop goes with it and presses reach what is behind again.
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(&Fixture.Screen, DialogId));
        TestUI_Click(run, &Fixture, 650.0f, 320.0f);
        TEST_CHECK(run, (Events.Count == 2U) && (Events.Events[1].WidgetId == BehindId));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APressOnAnOwnerThatCountsAsInsideIsTheOwners(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t OwnerId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = BelowWidget(OwnerId);
        Options.IsOwnerInside = true;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 120.0f, 110.0f);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, PopupId));
        TEST_CHECK(run, (Events.Count == 1U) && (Events.Events[0].WidgetId == OwnerId));
        // Without it, the same press closes the popup (and still clicks the owner).
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(&Fixture.Screen, PopupId));
        PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        Options.IsOwnerInside = false;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Click(run, &Fixture, 120.0f, 110.0f);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (Events.Count == 2U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APopupOverAClippedScrollAreaIsNotCut(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIScrollAreaOptions Area = UIScrollAreaOptions_Create();
        Area.Element.HasPlacement = true;
        Area.Element.Placement = UIPlacement_FromTopLeft(100.0f, 100.0f, 200.0f, 50.0f);
        uint64_t AreaId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateScrollArea(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Area, &AreaId));
        uint64_t AnchorId = AddButtonAt(run, &Fixture, AreaId, 0.0f, 0.0f, 60.0f, 30.0f);
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        uint64_t InsideId = AddButtonAt(run, &Fixture, PopupId, 0.0f, 0.0f, 120.0f, 30.0f); // The panel stacks it at its top.
        TestUI_Ticks(run, &Fixture, 2U);
        UIPopupOptions Options = BelowWidget(AnchorId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        // Below the area's box (which ends at 150), the popup's button is hit.
        Widget* Top = NULL;
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 150.0f / 1000.0f, .y = 165.0f / 500.0f }, &Top)
            && (Widget_GetId(Top) == InsideId));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void NavigationStaysInTheTopmostPopup(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t OutsideId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        uint64_t First = AddButtonAt(run, &Fixture, PopupId, 0.0f, 0.0f, 50.0f, 30.0f);
        uint64_t Second = AddButtonAt(run, &Fixture, PopupId, 60.0f, 0.0f, 50.0f, 30.0f);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = BelowWidget(OutsideId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        // Opened with nothing focused, the first Tab goes in; the next ones wrap inside.
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == First);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Second);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == First);
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == First);
        // Closed: navigation reaches the rest again.
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(&Fixture.Screen, PopupId));
        TestUI_Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == OutsideId);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static Error OpenAnotherOnClose(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)event;
    uint64_t* Next = UserData_GetPointer(userData);
    if (*Next == WIDGET_ID_INVALID)
    {
        return Error_CreateSuccess();
    }
    UIPopupOptions Options = UIPopupOptions_Create();
    uint64_t Id = *Next;
    *Next = WIDGET_ID_INVALID;
    return UIScreen_OpenPopup(screen, Id, &Options);
}

static void OpeningIsCheckedAndHandlersMayOpenMore(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AnchorId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        uint64_t ChildId = AddButtonAt(run, &Fixture, PopupId, 0.0f, 0.0f, 10.0f, 10.0f);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = UIPopupOptions_Create();
        UIScreen* Screen = &Fixture.Screen;
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(NULL, PopupId, &Options), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(Screen, PopupId, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(Screen, ChildId, &Options), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(Screen, 12345U, &Options), ErrorCode_InvalidOperation);
        Options.Side = (UIPopupSide)9;
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(Screen, PopupId, &Options), ErrorCode_ArgumentOutOfRange);
        Options = UIPopupOptions_Create();
        Options.AnchorBox.width = -1.0f;
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(Screen, PopupId, &Options), ErrorCode_ArgumentOutOfRange);
        Options = UIPopupOptions_Create();
        Options.MinimumLength = NAN;
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(Screen, PopupId, &Options), ErrorCode_ArgumentOutOfRange);
        // A modal popup with no backdrop to stand on is refused, with nothing changed.
        Options = UIPopupOptions_Create();
        Options.IsModal = true;
        uint64_t Backdrop = UIWidgetFactory_GetBackdropType(&Fixture.Widgets);
        UIWidgetFactory_SetBackdropType(&Fixture.Widgets, WIDGET_TYPE_ID_INVALID);
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(Screen, PopupId, &Options), ErrorCode_InvalidOperation);
        UIWidgetFactory_SetBackdropType(&Fixture.Widgets, Backdrop);
        TEST_CHECK(run, UIScreen_GetPopupCount(Screen) == 0U);
        Options.IsModal = false;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(Screen, PopupId, &Options));
        TEST_CHECK_ERROR(run, UIScreen_OpenPopup(Screen, PopupId, &Options), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScreen_ClosePopup(Screen, AnchorId), ErrorCode_InvalidOperation);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(Screen, WIDGET_ID_INVALID));

        // A handler told of a close may open another popup.
        uint64_t NextId = AddPopupPanel(run, &Fixture);
        uint64_t Pending = NextId;
        UserData Data = UserData_FromPointer(&Pending);
        uint64_t Subscription = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(Screen, AnchorId, UI_EVENT_KIND_MASK(UIEventKind_PopupClosed), OpenAnotherOnClose, &Data, &Subscription));
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(Screen, PopupId));
        uint64_t OwnedId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Owned = BelowWidget(AnchorId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(Screen, OwnedId, &Owned));
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(Screen, OwnedId));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_IsPopupOpen(Screen, NextId) && (UIScreen_GetPopupCount(Screen) == 1U));
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(Screen));
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(Screen));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* At a screen scale of 2 a popup's size and its shortest length count in UI pixels: twice as many surface pixels. */
static void APopupIsPlacedInTheScreensScale(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        uint64_t AnchorId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 50.0f, 50.0f, 30.0f, 10.0f);
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = BelowWidget(AnchorId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsBoxAt(TestUI_GetPixelBox(run, &Fixture, PopupId), 100.0f, 120.0f, 2.0f * POPUP_WIDTH, 2.0f * POPUP_HEIGHT));
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(&Fixture.Screen, PopupId));
        // Low on the surface, kept below and allowed to shrink: 40 surface pixels of room, but never shorter than 40 UI pixels (80
        // surface pixels), so it moves up to stay on the surface.
        UIPlacement Low = UIPlacement_FromTopLeft(50.0f, 220.0f, 30.0f, 10.0f);
        TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Get(&Fixture, AnchorId), &Low));
        PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        Options.Flips = false;
        Options.AllowsShrinking = true;
        Options.MinimumLength = 40.0f;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsBoxAt(TestUI_GetPixelBox(run, &Fixture, PopupId), 100.0f, 420.0f, 2.0f * POPUP_WIDTH, 80.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Escape on an element entered in a popup leaves it with a cancel as the popup closes, as Escape does everywhere. */
static void EscapeLeavesAnElementEnteredInItWithACancel(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        int32_t Reason = -1;
        WidgetTypeDescriptor Descriptor = { .Name = u8"LeaveProbe", .StructSize = sizeof(LeaveProbe), .VTable = &LeaveProbeVTable,
            .Constructor = LeaveProbe_Construct, .IsFocusable = true, .NeedsEntering = true };
        uint64_t ProbeType = WIDGET_TYPE_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&Fixture.Widgets, &Descriptor, &ProbeType));
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        Widget* Created = NULL;
        uint64_t ProbeId = WIDGET_ID_INVALID;
        if (TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.Widgets, &Fixture.Screen, ProbeType, &Reason, &Created)))
        {
            ProbeId = Widget_GetId(Created);
            UIPlacement Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, 50.0f, 30.0f);
            TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Created, &Placement));
            TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
            TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Get(&Fixture, PopupId), Created));
        }
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = UIPopupOptions_Create();
        Options.FocusId = ProbeId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == ProbeId);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (Reason == (int32_t)UILeaveReason_Cancel));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A press outside a popup puts the focus where it lands: the opener it closes does not take the focus back on its way, so a scroll area
 * holding the opener does not scroll it into view under the press. */
static void APressOutsideScrollsNothingIntoView(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIScrollAreaOptions Area = UIScrollAreaOptions_Create();
        Area.Element.HasPlacement = true;
        Area.Element.Placement = UIPlacement_FromTopLeft(100.0f, 50.0f, 200.0f, 100.0f);
        uint64_t AreaId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateScrollArea(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Area, &AreaId));
        UIStackOptions Column = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t ColumnId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, AreaId, &Column, &ColumnId));
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Element.Hint = UIStackHint_Fixed(30.0f);
        uint64_t SliderId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, ColumnId, &Slider, &SliderId));
        for (size_t Index = 0U; Index < 10U; Index++)
        {
            UIButtonOptions Button = UIButtonOptions_Create(u8"x");
            Button.Element.Hint = UIStackHint_Fixed(30.0f);
            uint64_t ButtonId = WIDGET_ID_INVALID;
            TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, ColumnId, &Button, &ButtonId));
        }
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        uint64_t InsideId = AddButtonAt(run, &Fixture, PopupId, 0.0f, 0.0f, 50.0f, 30.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, SliderId));
        UIPopupOptions Options = UIPopupOptions_Create();
        Options.AnchorBox = (Rectangle){ .x = 0.6f, .y = 0.6f, .width = 0.0f, .height = 0.0f };
        Options.OwnerId = SliderId;
        Options.FocusId = InsideId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_MovePointer(&Fixture, 150.0f, 100.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, -3.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        Vector2 Scrolled = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, AreaId, &Scrolled));
        TEST_CHECK(run, Scrolled.y > 30.0f); // The slider is out of view.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, AreaId, &Offset));
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, PopupId) && (Offset.y == Scrolled.y));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) != SliderId);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* From the update a modal popup opens in, nothing behind it takes a press, before its backdrop's first layout too. */
static void AModalPopupBlocksFromItsFirstUpdate(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIButtonOptions Behind = UIButtonOptions_Create(u8"behind");
        Behind.ClickMode = UIClickMode_OnPress;
        Behind.Element.HasPlacement = true;
        Behind.Element.Placement = UIPlacement_FromTopLeft(600.0f, 300.0f, 100.0f, 40.0f);
        uint64_t BehindId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Behind, &BehindId));
        uint64_t DialogId = AddPopupPanel(run, &Fixture);
        TestUI_MovePointer(&Fixture, 650.0f, 320.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        UIPopupOptions Options = UIPopupOptions_Create();
        Options.AnchorBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f };
        Options.Side = UIPopupSide_Center;
        Options.IsModal = true;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, DialogId, &Options));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 0U) && UIScreen_IsPopupOpen(&Fixture.Screen, DialogId));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) != BehindId);
        (void)BehindId;
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A group navigation entered before a modal popup opened lies behind it: Tab stays in the popup, and Escape, which the popup refuses,
 * does not step back into the group. */
static void NavigationNeverStepsBehindAModalPopup(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIPanelOptions Group = UIPanelOptions_Create();
        Group.Element.HasPlacement = true;
        Group.Element.Placement = UIPlacement_FromTopLeft(100.0f, 100.0f, 300.0f, 100.0f);
        uint64_t GroupId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Group, &GroupId));
        uint64_t A = AddButtonAt(run, &Fixture, GroupId, 0.0f, 0.0f, 60.0f, 30.0f);
        uint64_t B = AddButtonAt(run, &Fixture, GroupId, 100.0f, 0.0f, 60.0f, 30.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(Get(&Fixture, GroupId), true));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        uint64_t InGroup = UIScreen_GetFocusedWidgetId(&Fixture.Screen);
        TEST_CHECK(run, (InGroup == A) || (InGroup == B));
        uint64_t DialogId = AddPopupPanel(run, &Fixture);
        uint64_t YesId = AddButtonAt(run, &Fixture, DialogId, 0.0f, 0.0f, 50.0f, 30.0f);
        UIPopupOptions Options = UIPopupOptions_Create();
        Options.AnchorBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f };
        Options.Side = UIPopupSide_Center;
        Options.IsModal = true;
        Options.ClosesOnEscape = false;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, DialogId, &Options)); // The focus moves in to nothing.
        TestUI_Ticks(run, &Fixture, 2U);
        // Escape, which the popup refuses, does not step back into the group behind it.
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        uint64_t Focused = UIScreen_GetFocusedWidgetId(&Fixture.Screen);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, DialogId) && (Focused != A) && (Focused != B) && (Focused != GroupId));
        // Tab stays in the popup.
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        Focused = UIScreen_GetFocusedWidgetId(&Fixture.Screen);
        TEST_CHECK(run, (Focused == YesId) || (Focused == WIDGET_ID_INVALID));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        Focused = UIScreen_GetFocusedWidgetId(&Fixture.Screen);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, DialogId) && (Focused != A) && (Focused != B) && (Focused != GroupId));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A popup is scaled like its owner: a popup of an element in a panel of scale 2 is twice as large; one that says otherwise keeps its own. */
static void APopupIsScaledLikeItsOwner(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIPanelOptions Zoomed = UIPanelOptions_Create();
        Zoomed.Element.HasPlacement = true;
        Zoomed.Element.Placement = UIPlacement_FromTopLeft(50.0f, 50.0f, 300.0f, 200.0f);
        Zoomed.Element.Scale = 2.0f;
        uint64_t ZoomedId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Zoomed, &ZoomedId));
        uint64_t OwnerId = AddButtonAt(run, &Fixture, ZoomedId, 0.0f, 0.0f, 30.0f, 10.0f);
        uint64_t PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Ticks(run, &Fixture, 2U);
        UIPopupOptions Options = BelowWidget(OwnerId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, PopupId)) == 2.0f);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, PopupId);
        TEST_CHECK(run, (Box.width == (2.0f * POPUP_WIDTH)) && (Box.height == (2.0f * POPUP_HEIGHT)));
        TEST_CHECK_SUCCESS(run, UIScreen_ClosePopup(&Fixture.Screen, PopupId));
        PopupId = AddPopupPanel(run, &Fixture);
        TestUI_Tick(run, &Fixture);
        Options.IsScaledLikeOwner = false;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, Widget_GetEffectiveScale(Get(&Fixture, PopupId)) == 1.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Escape closes the topmost popup or none: a topmost popup that refuses it keeps those under it open too. */
static void EscapeLeavesAPopupThatRefusesItAndThoseUnderIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t AnchorId = AddButtonAt(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f, 60.0f, 20.0f);
        uint64_t LowerId = AddPopupPanel(run, &Fixture);
        uint64_t DialogId = AddPopupPanel(run, &Fixture);
        uint64_t YesId = AddButtonAt(run, &Fixture, DialogId, 0.0f, 0.0f, 50.0f, 30.0f);
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Lower = BelowWidget(AnchorId);
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, LowerId, &Lower));
        UIPopupOptions Dialog = UIPopupOptions_Create();
        Dialog.AnchorBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f };
        Dialog.Side = UIPopupSide_Center;
        Dialog.IsModal = true;
        Dialog.ClosesOnEscape = false;
        Dialog.FocusId = YesId;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, DialogId, &Dialog));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, UIScreen_IsPopupOpen(&Fixture.Screen, DialogId) && UIScreen_IsPopupOpen(&Fixture.Screen, LowerId));
        TEST_CHECK_SUCCESS(run, UIScreen_CloseAllPopups(&Fixture.Screen));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A popup the surface keeps narrower than it asked is asked again at the width it gets, so text that wraps is as tall as it will be. */
static void APopupKeptNarrowerIsMeasuredAtItsWidth(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TestUI_SetSurface(run, &Fixture, 300.0f, TEST_UI_SURFACE_HEIGHT);
        UIPanelOptions Panel = UIPanelOptions_Create();
        uint64_t PopupId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &PopupId));
        UILabelOptions Label = UILabelOptions_Create(u8"a long text that wraps to many lines on a narrow surface, many more than the eighty pixels "
            "a panel of this theme is tall at least, so the popup must be asked again at the width it gets to hold them all");
        Label.IsWrapped = true;
        uint64_t LabelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, PopupId, &Label, &LabelId));
        TestUI_Tick(run, &Fixture);
        UIPopupOptions Options = UIPopupOptions_Create();
        Options.AnchorBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f };
        Options.Side = UIPopupSide_Center;
        TEST_CHECK_SUCCESS(run, UIScreen_OpenPopup(&Fixture.Screen, PopupId, &Options));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, PopupId);
        Rectangle Text = TestUI_GetPixelBox(run, &Fixture, LabelId);
        TEST_CHECK(run, (Box.width == 300.0f) && (Text.height > 20.0f) && ((Text.y + Text.height) <= (Box.y + Box.height + 0.01f)));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UIPopupTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_placements_arithmetic", ThePlacementsArithmetic },
        { "a_popup_is_placed_in_the_screens_scale", APopupIsPlacedInTheScreensScale },
        { "escape_leaves_an_element_entered_in_it_with_a_cancel", EscapeLeavesAnElementEnteredInItWithACancel },
        { "a_press_outside_scrolls_nothing_into_view", APressOutsideScrollsNothingIntoView },
        { "a_modal_popup_blocks_from_its_first_update", AModalPopupBlocksFromItsFirstUpdate },
        { "navigation_never_steps_behind_a_modal_popup", NavigationNeverStepsBehindAModalPopup },
        { "a_popup_is_scaled_like_its_owner", APopupIsScaledLikeItsOwner },
        { "escape_leaves_a_popup_that_refuses_it_and_those_under_it", EscapeLeavesAPopupThatRefusesItAndThoseUnderIt },
        { "a_popup_kept_narrower_is_measured_at_its_width", APopupKeptNarrowerIsMeasuredAtItsWidth },
        { "a_popup_opens_above_everything_and_follows_its_anchor", APopupOpensAboveEverythingAndFollowsItsAnchor },
        { "a_press_outside_closes_it_and_goes_on", APressOutsideClosesItAndGoesOn },
        { "escape_closes_the_topmost_and_the_focus_comes_back", EscapeClosesTheTopmostAndTheFocusComesBack },
        { "nested_popups_close_from_the_inside_out", NestedPopupsCloseFromTheInsideOut },
    { "a_popup_closed_with_its_own_leaves_the_others", APopupClosedWithItsOwnLeavesTheOthers },
        { "the_owner_going_away_closes_it", TheOwnerGoingAwayClosesIt },
        { "a_modal_popup_takes_every_press", AModalPopupTakesEveryPress },
        { "a_press_on_an_owner_that_counts_as_inside_is_the_owners", APressOnAnOwnerThatCountsAsInsideIsTheOwners },
        { "a_popup_over_a_clipped_scroll_area_is_not_cut", APopupOverAClippedScrollAreaIsNotCut },
        { "navigation_stays_in_the_topmost_popup", NavigationStaysInTheTopmostPopup },
        { "opening_is_checked_and_handlers_may_open_more", OpeningIsCheckedAndHandlersMayOpenMore },
    };
    TestRun_RunSuite(run, "UIPopup", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
