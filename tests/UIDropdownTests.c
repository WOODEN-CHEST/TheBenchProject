#include "TestSuites.h"
#include "TestUI.h"
#include "UIKit.h"
#include "UIPopup.h"
#include "UIRowRecycler.h"
#include "UIWidgetFactory.h"
#include "WorldScreenHost.h"
#include "wr/WRNumber.h"
#include "wr/WRUserData.h"
#include <math.h>
#include <string.h>


// Macros.
/** The fixture's look: 20 pixel rows, a one pixel list padding, 10 pixel scroll strips and bars. */
#define DROPDOWN_THEME \
    "{\"name\": \"dropdowns\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"flat\"}}," \
    "\"dropdown\": {\"padding\": [8, 4], \"min_size\": [48, 24], \"arrow_size\": 8, \"background\": {\"kind\": \"flat\"}," \
    "\"text\": {\"normal\": \"#e0e0e0\"}, \"arrow\": {\"normal\": \"#ffffff\", \"disabled\": \"#404040\"}, \"list\": {\"kind\": \"flat\"}," \
    "\"list_padding\": [1, 1], \"row_height\": 20, \"row\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#101010\", \"hovered\": \"#303030\", \"selected\": \"#505050\"}}," \
    "\"row_text\": {\"normal\": \"#e0e0e0\"}, \"scroll_arrow_height\": 10}}}"
#define HEAD_X 100.0f
#define HEAD_Y 100.0f
#define HEAD_WIDTH 200.0f
#define HEAD_HEIGHT 24.0f
#define ROW 20.0f


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, DROPDOWN_THEME);
}

static const unsigned char* const Three[] = { u8"Alpha", u8"Beta", u8"Gamma" };

static uint64_t AddDropdownAt(TestRun* run, TestUIFixture* fixture, uint64_t parentId, float y, const unsigned char* const* options, size_t count,
    size_t visibleCount)
{
    UIDropdownOptions Options = UIDropdownOptions_Create();
    Options.Options = options;
    Options.OptionCount = count;
    Options.VisibleCount = visibleCount;
    Options.MixedText = u8"-";
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(HEAD_X, y, HEAD_WIDTH, HEAD_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateDropdown(&fixture->Kit, &fixture->Screen, parentId, &Options, &Id));
    return Id;
}

static uint64_t GetList(TestRun* run, TestUIFixture* fixture, uint64_t dropdownId)
{
    bool IsOpen = false;
    uint64_t ListId = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIDropdown_IsOpen(&fixture->Screen, dropdownId, &IsOpen, &ListId));
    return ListId;
}

static bool IsOpen(TestRun* run, TestUIFixture* fixture, uint64_t dropdownId)
{
    return GetList(run, fixture, dropdownId) != WIDGET_ID_INVALID;
}

/* How many row widgets a list has: the children of its first child, the box that cuts them. */
static size_t CountRowWidgets(TestUIFixture* fixture, uint64_t listId)
{
    Widget* List = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, listId, &List) || (Widget_GetSubWidgetCount(List) == 0U))
    {
        return 0U;
    }
    return Widget_GetSubWidgetCount(Widget_GetSubWidgetAt(List, 0U));
}

static bool HasSelection(TestRun* run, TestUIFixture* fixture, uint64_t id, size_t index)
{
    bool Has = false;
    size_t Selected = 0U;
    TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(&fixture->Screen, id, &Has, &Selected));
    return Has && (Selected == index);
}

static const unsigned char* GetShownText(TestUIFixture* fixture, uint64_t id)
{
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, id, &Found))
    {
        return u8"";
    }
    return UITextContent_Get(UIDropdownCapability_GetShownText(&((UIDropdownLookBase*)(void*)Found)->Dropdown));
}

static bool IsIndexEvent(const TestUIEvents* events, size_t index, uint64_t id, UIElementEvent event, double value)
{
    return (index < events->Count) && (events->Events[index].WidgetId == id) && (events->Events[index].WidgetEventId == (int32_t)event)
        && events->Events[index].HasValue && (events->Events[index].Value.Data.DoubleValue == value);
}

static float RowY(size_t index)
{
    return HEAD_Y + HEAD_HEIGHT + 1.0f + ((float)index * ROW) + (ROW * 0.5f);
}

/* Many options, from providers. */
static size_t CountMany(UIScreen* screen, uint64_t widgetId, const UserData* userData)
{
    (void)screen;
    (void)widgetId;
    return *(const size_t*)UserData_GetPointer(userData);
}

static Error TextOfMany(UIScreen* screen, uint64_t widgetId, const UserData* userData, size_t index, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    (void)userData;
    GenericBuffer_AppendString(outText, u8"Option ");
    return Number_UInt64ToString((uint64_t)index, 10, false, outText);
}


// Tests: the pieces.
static void TheChoicePieceHoldsItsOptions(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Three, 3U, 8U);
        UIScreen* Screen = &Fixture.Screen;
        size_t Count = 0U;
        TEST_CHECK_SUCCESS(run, UIChoice_GetCount(Screen, Id, &Count));
        TEST_CHECK(run, Count == 3U);
        GenericBuffer Text;
        GenericBuffer_AllocateVariable(&Text, 16U, sizeof(unsigned char));
        TEST_CHECK_SUCCESS(run, UIChoice_GetText(Screen, Id, 2U, &Text));
        TEST_CHECK(run, strcmp((const char*)Text._data, "Gamma") == 0);
        TEST_CHECK_ERROR(run, UIChoice_GetText(Screen, Id, 3U, &Text), ErrorCode_ArgumentOutOfRange);
        bool Has = true;
        TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(Screen, Id, &Has, NULL));
        TEST_CHECK(run, !Has);
        TEST_CHECK_SUCCESS(run, UIChoice_SetSelected(Screen, Id, true, 1U));
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 1U));
        TEST_CHECK_ERROR(run, UIChoice_SetSelected(Screen, Id, true, 3U), ErrorCode_ArgumentOutOfRange);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, strcmp((const char*)GetShownText(&Fixture, Id), "Beta") == 0);
        // Mixed shows its own text whatever is selected; setting the selection clears it.
        TEST_CHECK_SUCCESS(run, UIChoice_SetMixed(Screen, Id, true));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, strcmp((const char*)GetShownText(&Fixture, Id), "-") == 0);
        bool IsMixed = false;
        TEST_CHECK_SUCCESS(run, UIChoice_IsMixed(Screen, Id, &IsMixed));
        TEST_CHECK(run, IsMixed);
        // New options: a selection past them is cleared; bad ones are refused with nothing changed.
        const unsigned char* const Two[] = { u8"One", u8"Two" };
        TEST_CHECK_SUCCESS(run, UIChoice_SetSelected(Screen, Id, true, 2U));
        TEST_CHECK_SUCCESS(run, UIChoice_SetOptions(Screen, Id, Two, 2U));
        TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(Screen, Id, &Has, NULL));
        TEST_CHECK(run, !Has);
        const unsigned char Broken[] = { 0xC3U, 0x28U, 0U };
        const unsigned char* const Bad[] = { u8"fine", Broken };
        TEST_CHECK_ERROR(run, UIChoice_SetOptions(Screen, Id, Bad, 2U), ErrorCode_InvalidTextEncoding);
        const unsigned char* const Missing[] = { u8"fine", NULL };
        TEST_CHECK_ERROR(run, UIChoice_SetOptions(Screen, Id, Missing, 2U), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIChoice_SetOptions(Screen, Id, NULL, 1U), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIChoice_SetOptions(Screen, Id, Two, UI_CHOICE_MAX_OPTIONS + 1U), ErrorCode_ArgumentOutOfRange);
        unsigned char Long[UI_CHOICE_MAX_TEXT_BYTES + 2U];
        memset(Long, 'a', sizeof(Long) - 1U);
        Long[sizeof(Long) - 1U] = 0U;
        const unsigned char* const TooLong[] = { Long };
        TEST_CHECK_ERROR(run, UIChoice_SetOptions(Screen, Id, TooLong, 1U), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIChoice_GetCount(Screen, Id, &Count));
        TEST_CHECK(run, Count == 2U);
        TEST_CHECK_SUCCESS(run, UIChoice_SetOptions(Screen, Id, NULL, 0U));

        // From providers: asked for the count and for the texts it shows.
        size_t Many = 100000U;
        UserData Data = UserData_FromPointer(&Many);
        TEST_CHECK_ERROR(run, UIChoice_SetProvider(Screen, Id, NULL, TextOfMany, &Data), ErrorCode_IllegalArgument);
        TEST_CHECK_SUCCESS(run, UIChoice_SetProvider(Screen, Id, CountMany, TextOfMany, &Data));
        TEST_CHECK_SUCCESS(run, UIChoice_GetCount(Screen, Id, &Count));
        TEST_CHECK(run, Count == 100000U);
        GenericBuffer_Clear(&Text);
        TEST_CHECK_SUCCESS(run, UIChoice_GetText(Screen, Id, 99999U, &Text));
        TEST_CHECK(run, strcmp((const char*)Text._data, "Option 99999") == 0);
        Memory_Free(Text._data);

        // A stale id, and a widget with no choice.
        uint64_t ButtonId = WIDGET_ID_INVALID;
        UIButtonOptions Button = UIButtonOptions_Create(u8"b");
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, Screen, WIDGET_ID_INVALID, &Button, &ButtonId));
        TEST_CHECK_ERROR(run, UIChoice_GetCount(Screen, ButtonId, &Count), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIDropdown_Open(Screen, ButtonId), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIDropdown_SetVisibleCount(Screen, Id, 0U), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIDropdown_SetDirection(Screen, Id, (UIDropdownDirection)7), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheRecyclersVisibleRange(TestRun* run)
{
    size_t First = 9U;
    size_t Count = 9U;
    UIRowRecycler_GetVisibleRange(100U, 20.0f, 0.0f, 100.0f, &First, &Count);
    TEST_CHECK(run, (First == 0U) && (Count == 5U));
    UIRowRecycler_GetVisibleRange(100U, 20.0f, 10.0f, 100.0f, &First, &Count);
    TEST_CHECK(run, (First == 0U) && (Count == 6U)); // Half a row at each end.
    UIRowRecycler_GetVisibleRange(100U, 20.0f, 1990.0f, 100.0f, &First, &Count);
    TEST_CHECK(run, (First == 99U) && (Count == 1U));
    UIRowRecycler_GetVisibleRange(3U, 20.0f, 0.0f, 100.0f, &First, &Count);
    TEST_CHECK(run, (First == 0U) && (Count == 3U));
    UIRowRecycler_GetVisibleRange(100U, 0.0f, 0.0f, 100.0f, &First, &Count);
    TEST_CHECK(run, Count == 0U);
    UIRowRecycler_GetVisibleRange(100U, 20.0f, 0.0f, 0.0f, &First, &Count);
    TEST_CHECK(run, Count == 0U);
    UIRowRecycler_GetVisibleRange(100U, 20.0f, 5000.0f, 100.0f, &First, &Count);
    TEST_CHECK(run, Count == 0U);
    UIRowRecycler_GetVisibleRange(1000000U, 0.5f, 0.0f, 10000.0f, &First, &Count);
    TEST_CHECK(run, Count == UI_ROW_RECYCLER_MAX_ROWS);
    UIRowRecycler_GetVisibleRange(100U, 20.0f, NAN, 40.0f, &First, &Count);
    TEST_CHECK(run, (First == 0U) && (Count == 2U));
}


/* A recycler's owner for the tests: panels for rows, and what it was asked to do. */
typedef struct RecyclerProbeStruct
{
    TestUIFixture* Fixture;
    uint64_t ParentId;
    size_t Created;
    size_t Bound;
    size_t Unbound;
} RecyclerProbe;

static Error ProbeCreateRow(void* owner, uint64_t* outWidgetId)
{
    RecyclerProbe* Probe = owner;
    UIPanelOptions Options = UIPanelOptions_Create();
    Probe->Created++;
    return UIKit_CreatePanel(&Probe->Fixture->Kit, &Probe->Fixture->Screen, Probe->ParentId, &Options, outWidgetId);
}

static Error ProbeBindRow(void* owner, uint64_t widgetId, size_t dataIndex)
{
    (void)widgetId;
    (void)dataIndex;
    ((RecyclerProbe*)owner)->Bound++;
    return Error_CreateSuccess();
}

static Error ProbeUnbindRow(void* owner, uint64_t widgetId)
{
    (void)widgetId;
    ((RecyclerProbe*)owner)->Unbound++;
    return Error_CreateSuccess();
}

/* Whether exactly the rows [first, first + count) of @p total have a widget. */
static bool ShowsRows(const UIRowRecycler* recycler, size_t first, size_t count, size_t total)
{
    size_t Bound = 0U;
    for (size_t Index = 0U; Index < UIRowRecycler_GetCount(recycler); Index++)
    {
        Bound += UIRowRecycler_GetAt(recycler, Index)->IsBound ? 1U : 0U;
    }
    for (size_t Row = 0U; Row < total; Row++)
    {
        uint64_t Id = WIDGET_ID_INVALID;
        if (UIRowRecycler_FindWidget(recycler, Row, &Id) != ((Row >= first) && (Row < (first + count))))
        {
            return false;
        }
    }
    return Bound == count;
}

static void TheRecyclerKeepsWidgetsForTheRowsInViewOnly(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Screen;
        RecyclerProbe Probe = { .Fixture = &Fixture, .ParentId = WIDGET_ID_INVALID, .Created = 0U, .Bound = 0U, .Unbound = 0U };
        UIPanelOptions Parent = UIPanelOptions_Create();
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, Screen, WIDGET_ID_INVALID, &Parent, &Probe.ParentId));
        UIRowRecyclerHooks Hooks = { .CreateRow = ProbeCreateRow, .BindRow = ProbeBindRow, .UnbindRow = ProbeUnbindRow };
        UIRowRecycler Recycler;
        UIRowRecycler_Construct(&Recycler);
        // Five rows of a hundred in view: five widgets, one per row; the same view again does nothing.
        TEST_CHECK_SUCCESS(run, UIRowRecycler_Sync(&Recycler, Screen, 100U, ROW, 0.0f, 5.0f * ROW, &Hooks, &Probe));
        TEST_CHECK(run, (Probe.Created == 5U) && ShowsRows(&Recycler, 0U, 5U, 100U));
        size_t Bound = Probe.Bound;
        TEST_CHECK_SUCCESS(run, UIRowRecycler_Sync(&Recycler, Screen, 100U, ROW, 0.0f, 5.0f * ROW, &Hooks, &Probe));
        TEST_CHECK(run, Probe.Bound == Bound);
        // Two rows down: the rows still in view keep their widgets, the two that left show the two that came, and none is made.
        uint64_t Kept = WIDGET_ID_INVALID;
        uint64_t StillKept = WIDGET_ID_INVALID;
        TEST_CHECK(run, UIRowRecycler_FindWidget(&Recycler, 3U, &Kept));
        TEST_CHECK_SUCCESS(run, UIRowRecycler_Sync(&Recycler, Screen, 100U, ROW, 2.0f * ROW, 5.0f * ROW, &Hooks, &Probe));
        TEST_CHECK(run, (Probe.Created == 5U) && ShowsRows(&Recycler, 2U, 5U, 100U) && (Probe.Bound == (Bound + 2U)));
        TEST_CHECK(run, UIRowRecycler_FindWidget(&Recycler, 3U, &StillKept) && (StillKept == Kept));
        // And back up: still five widgets.
        TEST_CHECK_SUCCESS(run, UIRowRecycler_Sync(&Recycler, Screen, 100U, ROW, 0.0f, 5.0f * ROW, &Hooks, &Probe));
        TEST_CHECK(run, (Probe.Created == 5U) && ShowsRows(&Recycler, 0U, 5U, 100U));
        // The data shrinks to three rows: the widgets past them show nothing, and stay for later.
        size_t Unbound = Probe.Unbound;
        TEST_CHECK_SUCCESS(run, UIRowRecycler_Sync(&Recycler, Screen, 3U, ROW, 0.0f, 5.0f * ROW, &Hooks, &Probe));
        TEST_CHECK(run, ShowsRows(&Recycler, 0U, 3U, 100U) && (Probe.Unbound == (Unbound + 2U)) && (UIRowRecycler_GetCount(&Recycler) == 5U));
        // A row widget destroyed under it is forgotten: its row gets a live widget at the next Sync, with the view as it was.
        uint64_t Doomed = WIDGET_ID_INVALID;
        Widget* DoomedWidget = NULL;
        TEST_CHECK(run, UIRowRecycler_FindWidget(&Recycler, 0U, &Doomed) && UIScreen_TryGetWidgetById(Screen, Doomed, &DoomedWidget));
        TEST_CHECK_SUCCESS(run, Widget_Destroy(DoomedWidget));
        TEST_CHECK_SUCCESS(run, UIRowRecycler_Sync(&Recycler, Screen, 3U, ROW, 0.0f, 5.0f * ROW, &Hooks, &Probe));
        uint64_t Replaced = WIDGET_ID_INVALID;
        TEST_CHECK(run, UIRowRecycler_FindWidget(&Recycler, 0U, &Replaced) && UIScreen_IsWidgetAlive(Screen, Replaced) && (Replaced != Doomed));
        TEST_CHECK(run, ShowsRows(&Recycler, 0U, 3U, 100U) && (UIRowRecycler_GetCount(&Recycler) == 4U));
        UIRowRecycler_Deconstruct(&Recycler);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the dropdown.
static void AClickOpensTheListAndARowChooses(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Three, 3U, 8U);
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        uint64_t ListId = GetList(run, &Fixture, Id);
        TEST_CHECK(run, ListId != WIDGET_ID_INVALID);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == ListId) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == ListId));
        TestUI_Ticks(run, &Fixture, 3U);
        // Below the head, as wide as it, three rows tall: a widget per row, no bar.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, ListId), HEAD_X, HEAD_Y + HEAD_HEIGHT, HEAD_WIDTH, (3.0f * ROW) + 2.0f));
        TEST_CHECK(run, CountRowWidgets(&Fixture, ListId) == 3U);
        TEST_CHECK(run, Events.Count == 0U);

        // A click on the second row chooses it and closes the list; the focus is back on the head.
        TestUI_Click(run, &Fixture, HEAD_X + 30.0f, RowY(1U));
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 1U));
        TEST_CHECK(run, (Events.Count == 2U) && IsIndexEvent(&Events, 0U, Id, UIElementEvent_Changed, 1.0)
            && IsIndexEvent(&Events, 1U, Id, UIElementEvent_Committed, 1.0));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, strcmp((const char*)GetShownText(&Fixture, Id), "Beta") == 0);
        TEST_CHECK(run, UIScreen_GetWidgetCount(&Fixture.Screen) == 1U); // The head alone: the list went with its rows and its bar.

        // Choosing what is selected changes nothing and posts nothing; the list still closes.
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Click(run, &Fixture, HEAD_X + 30.0f, RowY(1U));
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && (Events.Count == 2U));

        // A second press on the head closes the list without choosing.
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TEST_CHECK(run, IsOpen(run, &Fixture, Id));
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && (Events.Count == 2U));
        // A press outside closes it too.
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Click(run, &Fixture, 800.0f, 400.0f);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && (Events.Count == 2U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ThePressDragReleaseChooses(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Three, 3U, 8U);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture, 4U);
        TEST_CHECK(run, IsOpen(run, &Fixture, Id));
        TestUI_MovePointer(&Fixture, HEAD_X + 40.0f, RowY(2U));
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 2U));
        // A press and a release on the head leaves the list open.
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TEST_CHECK(run, IsOpen(run, &Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheListOpensWhereThereIsRoom(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // Near the bottom: above.
        uint64_t Low = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, 460.0f, Three, 3U, 8U);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Low));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, GetList(run, &Fixture, Low)), HEAD_X, 460.0f - 62.0f, HEAD_WIDTH, 62.0f));
        TEST_CHECK_SUCCESS(run, UIDropdown_Close(&Fixture.Screen, Low));
        // Forced down there: it shrinks to the room, never below a row with its strips, and moves to stay on the surface.
        TEST_CHECK_SUCCESS(run, UIDropdown_SetDirection(&Fixture.Screen, Low, UIDropdownDirection_Down));
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Low));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, GetList(run, &Fixture, Low));
        // 16 pixels of room below: it keeps its shortest (a row, the padding and the strips: 42) and moves up over the head.
        TEST_CHECK(run, TestUI_IsBox(Box, HEAD_X, 500.0f - 42.0f, HEAD_WIDTH, 42.0f));
        TEST_CHECK_SUCCESS(run, UIDropdown_Close(&Fixture.Screen, Low));
        // Forced up with room below: above all the same.
        uint64_t Middle = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, 200.0f, Three, 3U, 8U);
        TEST_CHECK_SUCCESS(run, UIDropdown_SetDirection(&Fixture.Screen, Middle, UIDropdownDirection_Up));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Middle));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, GetList(run, &Fixture, Middle)), HEAD_X, 200.0f - 62.0f, HEAD_WIDTH, 62.0f));
        TEST_CHECK_SUCCESS(run, UIDropdown_Close(&Fixture.Screen, Middle));
        // Forced up near the top: above, moved down to stay on the surface.
        uint64_t High = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, 10.0f, Three, 3U, 8U);
        TEST_CHECK_SUCCESS(run, UIDropdown_SetDirection(&Fixture.Screen, High, UIDropdownDirection_Up));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, High));
        TestUI_Ticks(run, &Fixture, 3U);
        Box = TestUI_GetPixelBox(run, &Fixture, GetList(run, &Fixture, High));
        TEST_CHECK(run, (Box.y >= -0.01f) && (Box.height > 0.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ALongListScrollsWithArrowsAndABar(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        static const unsigned char* const Twenty[] = { u8"a0", u8"a1", u8"a2", u8"a3", u8"a4", u8"a5", u8"a6", u8"a7", u8"a8", u8"a9", u8"b0",
            u8"b1", u8"b2", u8"b3", u8"b4", u8"b5", u8"b6", u8"b7", u8"b8", u8"b9" };
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Twenty, 20U, 5U);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 4U);
        uint64_t ListId = GetList(run, &Fixture, Id);
        // Five rows, the two strips and the padding: 5 x 20 + 2 x 10 + 2.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, ListId), HEAD_X, HEAD_Y + HEAD_HEIGHT, HEAD_WIDTH, 122.0f));
        Vector2 Range = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.Screen, ListId, &Range));
        TEST_CHECK(run, Range.y == (15.0f * ROW));
        TEST_CHECK(run, CountRowWidgets(&Fixture, ListId) <= 6U);
        // The bar shows, beside the rows, between the strips.
        Widget* List = NULL;
        UIScreen_TryGetWidgetById(&Fixture.Screen, ListId, &List);
        Rectangle Bar = TestUI_GetPixelBox(run, &Fixture, Widget_GetId(Widget_GetSubWidgetAt(List, 1U)));
        TEST_CHECK(run, TestUI_IsBox(Bar, HEAD_X + HEAD_WIDTH - 1.0f - 10.0f, HEAD_Y + HEAD_HEIGHT + 1.0f + 10.0f, 10.0f, 5.0f * ROW));

        // The wheel scrolls three rows a notch; the bottom strip scrolls a row, and holding it goes on.
        TestUI_MovePointer(&Fixture, HEAD_X + 30.0f, HEAD_Y + HEAD_HEIGHT + 40.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, ListId, &Offset));
        TEST_CHECK(run, Offset.y == (3.0f * ROW));
        float BottomStrip = HEAD_Y + HEAD_HEIGHT + 122.0f - 1.0f - 5.0f;
        TestUI_MovePointer(&Fixture, HEAD_X + 30.0f, BottomStrip);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, ListId, &Offset));
        TEST_CHECK(run, Offset.y == (4.0f * ROW));
        TestUI_Ticks(run, &Fixture, (size_t)(1.0 / TEST_UI_TICK_SECONDS));
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, ListId, &Offset));
        TEST_CHECK(run, Offset.y == Range.y);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsOpen(run, &Fixture, Id));
        // The row widgets stayed few, reused as it scrolled.
        TEST_CHECK(run, CountRowWidgets(&Fixture, ListId) <= 6U);
        // The last rows show the last options: a click on the bottom row chooses the last one.
        TestUI_Click(run, &Fixture, HEAD_X + 30.0f, HEAD_Y + HEAD_HEIGHT + 1.0f + 10.0f + (4.5f * ROW));
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 19U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void KeysPassTheHeadAndWorkInTheListAtOnce(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Three, 3U, 8U);
        UIButtonOptions Below = UIButtonOptions_Create(u8"below");
        Below.Element.HasPlacement = true;
        Below.Element.Placement = UIPlacement_FromTopLeft(HEAD_X, 300.0f, 100.0f, 24.0f);
        uint64_t BelowId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Below, &BelowId));
        TestUI_Tick(run, &Fixture);
        // Tab focuses the head; Down goes past it to the button below, Up comes back.
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == BelowId);
        TestUI_Tap(run, &Fixture, GameKey_Up);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id) && !IsOpen(run, &Fixture, Id));
        // Enter opens it, entered: Down works at once, Enter chooses.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        uint64_t ListId = GetList(run, &Fixture, Id);
        TEST_CHECK(run, (ListId != WIDGET_ID_INVALID) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == ListId));
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 1U));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id);
        // Space opens it too, the highlight on the selected option; End, then Escape: nothing chosen, closed, focus back.
        TestUI_Tap(run, &Fixture, GameKey_Space);
        TEST_CHECK(run, IsOpen(run, &Fixture, Id));
        TestUI_Tap(run, &Fixture, GameKey_End);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 1U));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id) && (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
        // A letter jumps to the next option starting with it; Tab closes it without choosing.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        Fixture.Sample.TextCodepoints[0] = (uint32_t)'g';
        Fixture.Sample.TextCodepointCount = 1U;
        TestUI_Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 2U));
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Tap(run, &Fixture, GameKey_Home);
        TestUI_Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 2U));
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void PageKeysMoveTheHighlightAListsHeight(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        static const unsigned char* const Twenty[] = { u8"a0", u8"a1", u8"a2", u8"a3", u8"a4", u8"a5", u8"a6", u8"a7", u8"a8", u8"a9", u8"b0",
            u8"b1", u8"b2", u8"b3", u8"b4", u8"b5", u8"b6", u8"b7", u8"b8", u8"b9" };
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Twenty, 20U, 5U);
        TEST_CHECK_SUCCESS(run, UIChoice_SetSelected(&Fixture.Screen, Id, true, 0U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 3U);
        // Five rows show: Page Down moves the highlight five rows, Page Up five back, and Enter chooses where it is.
        TestUI_Tap(run, &Fixture, GameKey_PageDown);
        TestUI_Tap(run, &Fixture, GameKey_PageDown);
        TestUI_Tap(run, &Fixture, GameKey_PageUp);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 5U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AHundredThousandOptionsCostTheRowsInView(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, NULL, 0U, 10U);
        size_t Many = 100000U;
        UserData Data = UserData_FromPointer(&Many);
        TEST_CHECK_SUCCESS(run, UIChoice_SetProvider(&Fixture.Screen, Id, CountMany, TextOfMany, &Data));
        TestUI_Tick(run, &Fixture);
        size_t Before = UIScreen_GetWidgetCount(&Fixture.Screen);
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TestUI_Ticks(run, &Fixture, 4U);
        uint64_t ListId = GetList(run, &Fixture, Id);
        TEST_CHECK(run, CountRowWidgets(&Fixture, ListId) <= 11U);
        // To the end: the last option is the highlight, the view at the end, and still about as many widgets.
        TestUI_Tap(run, &Fixture, GameKey_End);
        TestUI_Ticks(run, &Fixture, 3U);
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        Vector2 Range = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, ListId, &Offset));
        TEST_CHECK_SUCCESS(run, UIScrollable_GetRange(&Fixture.Screen, ListId, &Range));
        TEST_CHECK(run, (Range.y > 0.0f) && (Offset.y == Range.y));
        TEST_CHECK(run, CountRowWidgets(&Fixture, ListId) <= 11U);
        TEST_CHECK(run, (UIScreen_GetWidgetCount(&Fixture.Screen) - Before) <= 14U); // The list, its rows' box, its bar and the rows.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 99999U));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, strcmp((const char*)GetShownText(&Fixture, Id), "Option 99999") == 0);
        // Page Up a list's height: ten rows.
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TestUI_Tap(run, &Fixture, GameKey_PageUp);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 99999U - 8U) || HasSelection(run, &Fixture, Id, 99999U - 10U)
            || HasSelection(run, &Fixture, Id, 99999U - 9U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheWheelStepsOnlyWithCtrlAndGoesOnOtherwise(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIScrollAreaOptions Area = UIScrollAreaOptions_Create();
        Area.Element.HasPlacement = true;
        Area.Element.Placement = UIPlacement_FromTopLeft(50.0f, 50.0f, 400.0f, 100.0f);
        uint64_t AreaId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateScrollArea(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Area, &AreaId));
        UIStackOptions Column = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t PageId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, AreaId, &Column, &PageId));
        UIDropdownOptions Options = UIDropdownOptions_Create();
        Options.Options = Three;
        Options.OptionCount = 3U;
        Options.Element.Hint = UIStackHint_Fixed(24.0f);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateDropdown(&Fixture.Kit, &Fixture.Screen, PageId, &Options, &Id));
        UIButtonOptions Tall = UIButtonOptions_Create(u8"tall");
        Tall.Element.Hint = UIStackHint_Fixed(400.0f);
        uint64_t TallId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, PageId, &Tall, &TallId));
        TestUI_Ticks(run, &Fixture, 4U);
        TestUI_MovePointer(&Fixture, 100.0f, 60.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Id);
        // A plain wheel over the head scrolls the page; the choice stays.
        TestUI_Wheel(&Fixture, -0.25f);
        TestUI_Tick(run, &Fixture);
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, AreaId, &Offset));
        TEST_CHECK(run, (Offset.y > 0.0f) && (Events.Count == 0U));
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.Screen, AreaId, (Vector2){ .x = 0.0f, .y = 0.0f }));
        TestUI_Tick(run, &Fixture);
        // Ctrl and the wheel step: down to the next option, a whole change each notch; at an end nothing more.
        TestUI_Press(&Fixture, GameKey_LeftControl);
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 0U) && (Events.Count == 2U));
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 2U) && (Events.Count == 6U));
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 1U));
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, AreaId, &Offset));
        TEST_CHECK(run, Offset.y == 0.0f);
        TestUI_Release(&Fixture, GameKey_LeftControl);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Where the player points on a world screen showing the fixture's surface. */
static WorldScreenHit AimAt(float x, float y)
{
    Vector2 Spot = { .x = x / TEST_UI_SURFACE_WIDTH, .y = y / TEST_UI_SURFACE_HEIGHT };
    return (WorldScreenHit){ .ObjectId = 7U, .Surface = NULL, .SurfaceIndex = 0U, .Uv = Spot, .Picture = Spot, .IsOnPicture = true,
        .Point = Vector3Double_Zero(), .Distance = 1.0f };
}

/* One tick of the fixture's screen through a world screen's host, the player pointing at @p pointed. */
static void WorldTick(TestRun* run, TestUIFixture* fixture, WorldScreenHost* host, const WorldScreenHit* pointed)
{
    GameInput_BeginTick(&fixture->Input, &fixture->Sample);
    fixture->Time += TEST_UI_TICK_SECONDS;
    TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(host, &fixture->Screen, ProgramTime_Create(fixture->Time, TEST_UI_TICK_SECONDS), &fixture->Input,
        pointed));
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        uint8_t State = fixture->Sample.KeyStates[Index];
        fixture->Sample.KeyStates[Index] = ((State & GAME_INPUT_KEY_STATE_RELEASED) != 0U) ? 0U : (uint8_t)(State & GAME_INPUT_KEY_STATE_DOWN);
    }
    fixture->Sample.TextCodepointCount = 0U;
}

static void WorldTap(TestRun* run, TestUIFixture* fixture, WorldScreenHost* host, const WorldScreenHit* pointed, GameKey key)
{
    TestUI_Press(fixture, key);
    WorldTick(run, fixture, host, pointed);
    TestUI_Release(fixture, key);
    WorldTick(run, fixture, host, pointed);
}

/* On a world screen it works as on the window: a click where the player points opens it and gives the screen the keyboard, the keys
 * work in the list at once, a click on a row chooses, and Escape closes the list before it lets go of the screen. */
static void ADropdownWorksOnAWorldScreen(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Three, 3U, 8U);
        TEST_CHECK_SUCCESS(run, UIChoice_SetSelected(&Fixture.Screen, Id, true, 0U));
        TestUI_Tick(run, &Fixture);
        WorldScreenHost Host;
        WorldScreenHost_Construct(&Host, 7U);
        WorldScreenHit OnHead = AimAt(HEAD_X + 30.0f, HEAD_Y + 10.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        WorldTick(run, &Fixture, &Host, &OnHead);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        WorldTick(run, &Fixture, &Host, &OnHead);
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Host) && IsOpen(run, &Fixture, Id));
        WorldTap(run, &Fixture, &Host, &OnHead, GameKey_Down);
        WorldTap(run, &Fixture, &Host, &OnHead, GameKey_Enter);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 1U) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id));
        // A click on the head, then on the third row.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        WorldTick(run, &Fixture, &Host, &OnHead);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        WorldTick(run, &Fixture, &Host, &OnHead);
        WorldTick(run, &Fixture, &Host, &OnHead);
        WorldScreenHit OnRow = AimAt(HEAD_X + 30.0f, RowY(2U));
        WorldTick(run, &Fixture, &Host, &OnRow);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        WorldTick(run, &Fixture, &Host, &OnRow);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        WorldTick(run, &Fixture, &Host, &OnRow);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 2U));
        // Opened again: Escape closes the list and keeps the screen, the next one clears the focus, and only the third lets go.
        WorldTap(run, &Fixture, &Host, &OnHead, GameKey_Enter);
        TEST_CHECK(run, IsOpen(run, &Fixture, Id));
        WorldTap(run, &Fixture, &Host, &OnHead, GameKey_Escape);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && WorldScreenHost_IsFocused(&Host) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id));
        WorldTap(run, &Fixture, &Host, &OnHead, GameKey_Escape);
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Host) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
        WorldTap(run, &Fixture, &Host, &OnHead, GameKey_Escape);
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Host));
        WorldScreenHost_Deconstruct(&Host);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A provider whose data the test changes: its count, the text of index 0, and how often it was asked for a text. */
typedef struct ShiftingDataStruct
{
    size_t First;
    size_t Count;
    size_t TextCalls;
} ShiftingData;

static size_t CountShifting(UIScreen* screen, uint64_t widgetId, const UserData* userData)
{
    (void)screen;
    (void)widgetId;
    return ((const ShiftingData*)UserData_GetPointer(userData))->Count;
}

static Error TextOfShifting(UIScreen* screen, uint64_t widgetId, const UserData* userData, size_t index, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    ShiftingData* Data = UserData_GetPointer(userData);
    Data->TextCalls++;
    GenericBuffer_AppendString(outText, u8"item ");
    return Number_UInt64ToString((uint64_t)(Data->First + index), 10, false, outText);
}

static size_t CountEvents(const TestUIEvents* events, uint64_t id, UIElementEvent event)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        Count += ((events->Events[Index].WidgetId == id) && (events->Events[Index].WidgetEventId == (int32_t)event)) ? 1U : 0U;
    }
    return Count;
}

/* With the pointer at rest on a row, the keys move the highlight and Enter chooses where they put it; the highlight follows the pointer
 * only when it moves. */
static void TheHighlightFollowsThePointerOnlyWhenItMoves(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        static const unsigned char* const Ten[] = { u8"a0", u8"a1", u8"a2", u8"a3", u8"a4", u8"a5", u8"a6", u8"a7", u8"a8", u8"a9" };
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Ten, 10U, 8U);
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, HEAD_X + 50.0f, HEAD_Y + 10.0f);
        TestUI_Ticks(run, &Fixture, 4U);
        TestUI_MovePointer(&Fixture, HEAD_X + 50.0f, RowY(2U));
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TestUI_Tap(run, &Fixture, GameKey_Down);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 4U));
        // Opened again: moving the pointer onto a row puts the highlight there.
        TestUI_Click(run, &Fixture, HEAD_X + 50.0f, HEAD_Y + 10.0f);
        TestUI_Ticks(run, &Fixture, 4U);
        TestUI_MovePointer(&Fixture, HEAD_X + 50.0f, RowY(6U));
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 6U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A click on a row chooses it and reaches nothing behind the closing list; a release of the bar's press over a row chooses nothing. */
static void AClickOnARowReachesNothingBehindIt(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        static const unsigned char* const Twenty[] = { u8"a0", u8"a1", u8"a2", u8"a3", u8"a4", u8"a5", u8"a6", u8"a7", u8"a8", u8"a9", u8"b0",
            u8"b1", u8"b2", u8"b3", u8"b4", u8"b5", u8"b6", u8"b7", u8"b8", u8"b9" };
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Twenty, 20U, 8U);
        UIButtonOptions Behind = UIButtonOptions_Create(u8"behind");
        Behind.ClickMode = UIClickMode_OnRelease;
        Behind.Element.HasPlacement = true;
        Behind.Element.Placement = UIPlacement_FromTopLeft(HEAD_X, RowY(3U) - 10.0f, HEAD_WIDTH, ROW);
        uint64_t BehindId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Behind, &BehindId));
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, HEAD_X + 50.0f, HEAD_Y + 10.0f);
        TestUI_Ticks(run, &Fixture, 4U);
        TestUI_Click(run, &Fixture, HEAD_X + 50.0f, RowY(3U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasSelection(run, &Fixture, Id, 3U) && !IsOpen(run, &Fixture, Id));
        TEST_CHECK(run, CountEvents(&Events, BehindId, UIElementEvent_Activated) == 0U);
        // The list's bar: a press on its track below the thumb, dragged onto a row and let go there, scrolls and chooses nothing.
        TestUI_Click(run, &Fixture, HEAD_X + 50.0f, HEAD_Y + 10.0f);
        TestUI_Ticks(run, &Fixture, 4U);
        float BarX = HEAD_X + HEAD_WIDTH - 1.0f - 5.0f;
        TestUI_MovePointer(&Fixture, BarX, RowY(6U));
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, HEAD_X + 50.0f, RowY(5U));
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsOpen(run, &Fixture, Id) && HasSelection(run, &Fixture, Id, 3U));
        TEST_CHECK_SUCCESS(run, UIDropdown_Close(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A list placed over its own head: the click that opened it chooses nothing; dragged past a click's distance, the press chooses. */
static void AListOverItsHeadIsNotChosenByTheOpeningClick(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, 470.0f, Three, 3U, 8U);
        TEST_CHECK_SUCCESS(run, UIDropdown_SetDirection(&Fixture.Screen, Id, UIDropdownDirection_Down));
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, HEAD_X + 50.0f, 480.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture, 30U);
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, GetList(run, &Fixture, Id));
        TEST_CHECK(run, (Box.y < 494.0f) && ((Box.y + Box.height) > 470.0f)); // Over the head.
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture, 2U);
        bool Has = true;
        TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(&Fixture.Screen, Id, &Has, NULL));
        TEST_CHECK(run, !Has && IsOpen(run, &Fixture, Id));
        TEST_CHECK_SUCCESS(run, UIDropdown_Close(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        // Press, drag along the row past a click's distance, let go there: that row.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_MovePointer(&Fixture, HEAD_X + 90.0f, 480.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(&Fixture.Screen, Id, &Has, NULL));
        TEST_CHECK(run, Has && !IsOpen(run, &Fixture, Id));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A provider's rows in view are asked for their texts again when its count changes, or when calling code says its data changed; a
 * selection past the count is cleared. */
static void RowsAskAgainWhenAProvidersDataChanges(TestRun* run)
{
    TestUIFixture Fixture;
    ShiftingData Data = { .First = 0U, .Count = 6U, .TextCalls = 0U };
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, NULL, 0U, 8U);
        UserData User = UserData_FromPointer(&Data);
        TEST_CHECK_SUCCESS(run, UIChoice_SetSelected(&Fixture.Screen, Id, false, 0U));
        TEST_CHECK_SUCCESS(run, UIChoice_SetProvider(&Fixture.Screen, Id, CountShifting, TextOfShifting, &User));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 4U);
        size_t Calls = Data.TextCalls;
        TestUI_Ticks(run, &Fixture, 4U);
        TEST_CHECK(run, Data.TextCalls == Calls); // Nothing changed: nothing asked.
        // The first option goes: the data shifts and the count shrinks, so the rows are asked again.
        Data.First = 1U;
        Data.Count = 5U;
        TestUI_Ticks(run, &Fixture, 4U);
        TEST_CHECK(run, Data.TextCalls >= (Calls + 5U));
        // The texts change under the same count: calling code says so.
        Calls = Data.TextCalls;
        Data.First = 7U;
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, Data.TextCalls == Calls);
        TEST_CHECK_SUCCESS(run, UIChoice_Refresh(&Fixture.Screen, Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, Data.TextCalls >= (Calls + 5U));
        TEST_CHECK_SUCCESS(run, UIDropdown_Close(&Fixture.Screen, Id));
        // A selection past the count: cleared by a refresh, and by a provider that has fewer options.
        TEST_CHECK_SUCCESS(run, UIChoice_SetSelected(&Fixture.Screen, Id, true, 4U));
        Data.Count = 3U;
        TEST_CHECK_SUCCESS(run, UIChoice_Refresh(&Fixture.Screen, Id));
        bool Has = true;
        TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(&Fixture.Screen, Id, &Has, NULL));
        TEST_CHECK(run, !Has);
        Data.Count = 6U;
        TEST_CHECK_SUCCESS(run, UIChoice_SetSelected(&Fixture.Screen, Id, true, 5U));
        Data.Count = 2U;
        TEST_CHECK_SUCCESS(run, UIChoice_SetProvider(&Fixture.Screen, Id, CountShifting, TextOfShifting, &User));
        TEST_CHECK_SUCCESS(run, UIChoice_GetSelected(&Fixture.Screen, Id, &Has, NULL));
        TEST_CHECK(run, !Has);
        TEST_CHECK_ERROR(run, UIChoice_Refresh(&Fixture.Screen, 99999U), ErrorCode_InvalidOperation);
        // An empty option's text appended after another gets a terminator of its own.
        static const unsigned char* const Blank[] = { u8"x", u8"" };
        TEST_CHECK_SUCCESS(run, UIChoice_SetOptions(&Fixture.Screen, Id, Blank, 2U));
        GenericBuffer Text;
        GenericBuffer_AllocateVariable(&Text, 16U, sizeof(unsigned char));
        unsigned char Letter = (unsigned char)'A';
        unsigned char Zero = 0U;
        for (size_t Index = 0U; Index < 15U; Index++)
        {
            GenericBuffer_AddLast(&Text, &Letter);
        }
        GenericBuffer_AddLast(&Text, &Zero);
        GenericBuffer_SetCount(&Text, 0U); // What lies past the count reads as a text of its own.
        TEST_CHECK_SUCCESS(run, UIChoice_GetText(&Fixture.Screen, Id, 0U, &Text));
        size_t Start = Text._count;
        TEST_CHECK_SUCCESS(run, UIChoice_GetText(&Fixture.Screen, Id, 1U, &Text));
        TEST_CHECK(run, (Text._count == (Start + 1U)) && (Text._data[Start] == 0U));
        Memory_Free(Text._data);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void MixedAndDisabled(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddDropdownAt(run, &Fixture, WIDGET_ID_INVALID, HEAD_Y, Three, 3U, 8U);
        TEST_CHECK_SUCCESS(run, UIChoice_SetSelected(&Fixture.Screen, Id, true, 1U));
        TEST_CHECK_SUCCESS(run, UIChoice_SetMixed(&Fixture.Screen, Id, true));
        TestUI_Tick(run, &Fixture);
        // Mixed: choosing the selected option still posts, and the mixed state is gone.
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TestUI_Ticks(run, &Fixture, 3U);
        TestUI_Click(run, &Fixture, HEAD_X + 30.0f, RowY(1U));
        TEST_CHECK(run, (Events.Count == 2U) && IsIndexEvent(&Events, 1U, Id, UIElementEvent_Committed, 1.0));
        bool IsMixed = true;
        TEST_CHECK_SUCCESS(run, UIChoice_IsMixed(&Fixture.Screen, Id, &IsMixed));
        TEST_CHECK(run, !IsMixed);
        // Disabled: a press does nothing and it is no longer focusable; disabling closes an open list.
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Id));
        TEST_CHECK(run, IsOpen(run, &Fixture, Id));
        TEST_CHECK_SUCCESS(run, UIDropdown_SetIsEnabled(&Fixture.Screen, Id, false));
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id));
        TestUI_Click(run, &Fixture, HEAD_X + 20.0f, HEAD_Y + 10.0f);
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) != Id));
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Id));
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id));
        // No options: nothing opens.
        TEST_CHECK_SUCCESS(run, UIDropdown_SetIsEnabled(&Fixture.Screen, Id, true));
        TEST_CHECK_SUCCESS(run, UIChoice_SetOptions(&Fixture.Screen, Id, NULL, 0U));
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Id));
        TEST_CHECK(run, !IsOpen(run, &Fixture, Id));
        // The head destroyed while its list is open: the list closes at the next update.
        TEST_CHECK_SUCCESS(run, UIChoice_SetOptions(&Fixture.Screen, Id, Three, 3U));
        TEST_CHECK_SUCCESS(run, UIDropdown_Open(&Fixture.Screen, Id));
        uint64_t ListId = GetList(run, &Fixture, Id);
        Widget* Head = NULL;
        UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Head);
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Head));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !UIScreen_IsPopupOpen(&Fixture.Screen, ListId) && (UIScreen_GetWidgetCount(&Fixture.Screen) == 0U));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UIDropdownTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_choice_piece_holds_its_options", TheChoicePieceHoldsItsOptions },
        { "the_recyclers_visible_range", TheRecyclersVisibleRange },
        { "the_recycler_keeps_widgets_for_the_rows_in_view_only", TheRecyclerKeepsWidgetsForTheRowsInViewOnly },
        { "a_click_opens_the_list_and_a_row_chooses", AClickOpensTheListAndARowChooses },
        { "the_press_drag_release_chooses", ThePressDragReleaseChooses },
        { "the_list_opens_where_there_is_room", TheListOpensWhereThereIsRoom },
        { "a_long_list_scrolls_with_arrows_and_a_bar", ALongListScrollsWithArrowsAndABar },
        { "keys_pass_the_head_and_work_in_the_list_at_once", KeysPassTheHeadAndWorkInTheListAtOnce },
        { "page_keys_move_the_highlight_a_lists_height", PageKeysMoveTheHighlightAListsHeight },
        { "a_hundred_thousand_options_cost_the_rows_in_view", AHundredThousandOptionsCostTheRowsInView },
        { "the_wheel_steps_only_with_ctrl_and_goes_on_otherwise", TheWheelStepsOnlyWithCtrlAndGoesOnOtherwise },
        { "mixed_and_disabled", MixedAndDisabled },
        { "a_dropdown_works_on_a_world_screen", ADropdownWorksOnAWorldScreen },
        { "the_highlight_follows_the_pointer_only_when_it_moves", TheHighlightFollowsThePointerOnlyWhenItMoves },
        { "a_click_on_a_row_reaches_nothing_behind_it", AClickOnARowReachesNothingBehindIt },
        { "a_list_over_its_head_is_not_chosen_by_the_opening_click", AListOverItsHeadIsNotChosenByTheOpeningClick },
        { "rows_ask_again_when_a_providers_data_changes", RowsAskAgainWhenAProvidersDataChanges },
    };
    TestRun_RunSuite(run, "UIDropdown", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
