#include "TestSuites.h"
#include "TestUI.h"
#include "UIContextMenu.h"
#include "UIDragDrop.h"
#include "UIKit.h"
#include "UIList.h"
#include "UIListRow.h"
#include "UIWidgetFactory.h"
#include "wr/WRMemory.h"
#include "wr/WRNumber.h"
#include "wr/WRString.h"
#include "wr/WRUserData.h"
#include <string.h>


// Macros.
/** The fixture's look: 20 pixel rows inside a one pixel padding, rows padded by 4, 12 pixel cells and a 10 pixel bar. */
#define LIST_THEME \
    "{\"name\": \"lists\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"flat\"}}," \
    "\"list\": {\"padding\": [1, 1], \"row_height\": 20, \"row_padding\": 4, \"cell_size\": 12, \"insertion_thickness\": 2," \
    "\"background\": {\"kind\": \"flat\"}, \"row\": {\"kind\": \"flat\"}, \"current\": {\"kind\": \"none\"}, \"text\": {\"normal\": \"#e0e0e0\"}," \
    "\"cell\": {\"kind\": \"flat\"}, \"cell_text\": {\"normal\": \"#e0e0e0\"}, \"insertion\": {\"kind\": \"flat\"}}}}"
#define LIST_X 100.0f
#define LIST_Y 100.0f
#define LIST_WIDTH 300.0f
#define LIST_HEIGHT 202.0f
#define ROW 20.0f
/** The rows' box: inside the one pixel padding, ten rows tall. */
#define ROWS_X (LIST_X + 1.0f)
#define ROWS_Y (LIST_Y + 1.0f)
#define ROWS_IN_VIEW ((size_t)10)


// Types.
/* The caller's data: a count, toggle cells by a rule, and how many rows were asked for. */
typedef struct RowsStruct
{
    size_t Count;
    size_t Asked;
    bool AreCellsOn;
    size_t Dimmed;
} Rows;


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, LIST_THEME);
}

static size_t CountRows(UIScreen* screen, uint64_t listId, const UserData* userData)
{
    (void)screen;
    (void)listId;
    return ((const Rows*)UserData_GetPointer(userData))->Count;
}

static Error WriteRow(UIScreen* screen, uint64_t listId, const UserData* userData, size_t index, UIListRowData* outRow)
{
    (void)screen;
    (void)listId;
    Rows* Data = UserData_GetPointer(userData);
    Data->Asked++;
    outRow->IsDimmed = index == Data->Dimmed;
    outRow->Cells[0] = Data->AreCellsOn;
    outRow->Cells[1] = (index % 2U) == 0U;
    GenericBuffer_AppendString(outRow->Text, u8"Row ");
    return Number_UInt64ToString((uint64_t)index, 10, false, outRow->Text);
}

static uint64_t AddList(TestRun* run, TestUIFixture* fixture, Rows* rows, UISelectionMode mode, size_t cellCount)
{
    UIListOptions Options = UIListOptions_Create();
    Options.SelectionMode = mode;
    Options.CountProvider = CountRows;
    Options.RowProvider = WriteRow;
    Options.ProviderData = UserData_FromPointer(rows);
    Options.CellCount = cellCount;
    Options.CellIcons[0] = UIElementIcon_FromLetter((uint32_t)'E');
    Options.CellIcons[1] = UIElementIcon_FromLetter((uint32_t)'V');
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateList(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TestUI_Ticks(run, fixture, 3U);
    return Id;
}

static float RowY(size_t row)
{
    return ROWS_Y + ((float)row * ROW) + (ROW * 0.5f);
}

/* A click on a row, never the second of a double click: each lands further across than a double click's distance from the last. */
static void ClickRow(TestRun* run, TestUIFixture* fixture, size_t row)
{
    static float Across = 0.0f;
    Across = (Across >= 120.0f) ? 0.0f : (Across + 20.0f);
    TestUI_Click(run, fixture, LIST_X + 40.0f + Across, RowY(row));
}

static void ClickRowWith(TestRun* run, TestUIFixture* fixture, size_t row, GameKey modifier)
{
    TestUI_Press(fixture, modifier);
    TestUI_Tick(run, fixture);
    ClickRow(run, fixture, row);
    TestUI_Release(fixture, modifier);
    TestUI_Tick(run, fixture);
}

static bool IsSelected(TestRun* run, TestUIFixture* fixture, uint64_t id, size_t row)
{
    bool Is = false;
    TEST_CHECK_SUCCESS(run, UIList_IsSelected(&fixture->Screen, id, row, &Is));
    return Is;
}

static size_t SelectedCount(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    size_t Count = 0U;
    TEST_CHECK_SUCCESS(run, UIList_GetSelection(&fixture->Screen, id, &Count, NULL));
    return Count;
}

/* Whether exactly the rows given are selected, among the first @p span. */
static bool IsSelectionExactly(TestRun* run, TestUIFixture* fixture, uint64_t id, size_t span, const size_t* rows, size_t count)
{
    if (SelectedCount(run, fixture, id) != count)
    {
        return false;
    }
    for (size_t Row = 0U; Row < span; Row++)
    {
        bool IsListed = false;
        for (size_t Index = 0U; Index < count; Index++)
        {
            IsListed = IsListed || (rows[Index] == Row);
        }
        if (IsSelected(run, fixture, id, Row) != IsListed)
        {
            return false;
        }
    }
    return true;
}

static bool HasCurrent(TestRun* run, TestUIFixture* fixture, uint64_t id, size_t row)
{
    bool Has = false;
    size_t Current = 0U;
    TEST_CHECK_SUCCESS(run, UIList_GetCurrent(&fixture->Screen, id, &Has, &Current));
    return Has && (Current == row);
}

static size_t CountRowWidgets(TestUIFixture* fixture, uint64_t listId)
{
    Widget* List = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, listId, &List) || (Widget_GetSubWidgetCount(List) == 0U))
    {
        return 0U;
    }
    // The rows' box is the first child, the bar the second.
    return Widget_GetSubWidgetCount(Widget_GetSubWidgetAt(List, 0U));
}

static bool IsItemEvent(const TestUIEvents* events, size_t index, uint64_t id, UIElementEvent event, uint64_t item)
{
    return (index < events->Count) && (events->Events[index].WidgetId == id) && (events->Events[index].WidgetEventId == (int32_t)event)
        && events->Events[index].HasValue && (events->Events[index].Value.Type == UIPropertyType_UInt64)
        && (events->Events[index].Value.Data.UInt64Value == item);
}

static bool IsCountEvent(const TestUIEvents* events, size_t index, uint64_t id, UIElementEvent event, double count)
{
    return (index < events->Count) && (events->Events[index].WidgetId == id) && (events->Events[index].WidgetEventId == (int32_t)event)
        && events->Events[index].HasValue && (events->Events[index].Value.Type == UIPropertyType_Double)
        && (events->Events[index].Value.Data.DoubleValue == count);
}

static void Tap(TestRun* run, TestUIFixture* fixture, GameKey key)
{
    TestUI_Tap(run, fixture, key);
}

static void TapWith(TestRun* run, TestUIFixture* fixture, GameKey modifier, GameKey key)
{
    TestUI_Press(fixture, modifier);
    TestUI_Tick(run, fixture);
    TestUI_Tap(run, fixture, key);
    TestUI_Release(fixture, modifier);
    TestUI_Tick(run, fixture);
}


// Tests: the selection's runs.
static void TheRunsStaySortedApartAndCounted(TestRun* run)
{
    GenericBuffer Runs;
    GenericBuffer_AllocateVariable(&Runs, 0U, sizeof(UIListRange));
    size_t Count = 0U;
    TEST_CHECK(run, UIListRanges_Set(&Runs, 10U, 5U, true, &Count) && (Count == 5U) && (Runs._count == 1U));
    TEST_CHECK(run, UIListRanges_Contains(&Runs, 10U) && UIListRanges_Contains(&Runs, 14U));
    TEST_CHECK(run, !UIListRanges_Contains(&Runs, 9U) && !UIListRanges_Contains(&Runs, 15U));
    // A run apart, then one touching both: all three merge.
    TEST_CHECK(run, UIListRanges_Set(&Runs, 20U, 2U, true, &Count) && (Count == 7U) && (Runs._count == 2U));
    TEST_CHECK(run, UIListRanges_Set(&Runs, 15U, 5U, true, &Count) && (Count == 12U) && (Runs._count == 1U));
    const UIListRange* First = GenericBuffer_GetPointerToElement(&Runs, 0U);
    TEST_CHECK(run, (First->Start == 10U) && (First->Count == 12U));
    // Selecting what is selected changes nothing; one touching the start merges.
    TEST_CHECK(run, UIListRanges_Set(&Runs, 12U, 3U, true, &Count) && (Count == 12U) && (Runs._count == 1U));
    TEST_CHECK(run, UIListRanges_Set(&Runs, 9U, 1U, true, &Count) && (Count == 13U) && (Runs._count == 1U));
    // Deselecting inside splits it; at its ends shortens it; the whole of it removes it.
    TEST_CHECK(run, UIListRanges_Set(&Runs, 12U, 2U, false, &Count) && (Count == 11U) && (Runs._count == 2U));
    TEST_CHECK(run, !UIListRanges_Contains(&Runs, 12U) && !UIListRanges_Contains(&Runs, 13U) && UIListRanges_Contains(&Runs, 14U));
    TEST_CHECK(run, UIListRanges_Set(&Runs, 9U, 1U, false, &Count) && (Count == 10U) && !UIListRanges_Contains(&Runs, 9U));
    TEST_CHECK(run, UIListRanges_Set(&Runs, 14U, 8U, false, &Count) && (Count == 2U) && (Runs._count == 1U));
    TEST_CHECK(run, UIListRanges_Set(&Runs, 0U, 100U, false, &Count) && (Count == 0U) && (Runs._count == 0U));
    // Deselecting across several runs.
    UIListRanges_Set(&Runs, 0U, 2U, true, &Count);
    UIListRanges_Set(&Runs, 4U, 2U, true, &Count);
    UIListRanges_Set(&Runs, 8U, 2U, true, &Count);
    TEST_CHECK(run, (Count == 6U) && (Runs._count == 3U));
    TEST_CHECK(run, UIListRanges_Set(&Runs, 1U, 8U, false, &Count) && (Count == 2U) && (Runs._count == 2U));
    TEST_CHECK(run, UIListRanges_Contains(&Runs, 0U) && UIListRanges_Contains(&Runs, 9U) && !UIListRanges_Contains(&Runs, 5U));

    // Inserted rows move what lies at and after them, and split a run they fall inside (the new rows unselected).
    GenericBuffer_Clear(&Runs);
    Count = 0U;
    UIListRanges_Set(&Runs, 5U, 5U, true, &Count);
    TEST_CHECK(run, UIListRanges_Insert(&Runs, 7U, 3U) && (Runs._count == 2U));
    TEST_CHECK(run, UIListRanges_Contains(&Runs, 6U) && !UIListRanges_Contains(&Runs, 7U) && !UIListRanges_Contains(&Runs, 9U));
    TEST_CHECK(run, UIListRanges_Contains(&Runs, 10U) && UIListRanges_Contains(&Runs, 12U) && !UIListRanges_Contains(&Runs, 13U));
    TEST_CHECK(run, UIListRanges_Insert(&Runs, 0U, 1U) && UIListRanges_Contains(&Runs, 6U) && !UIListRanges_Contains(&Runs, 5U));
    TEST_CHECK(run, UIListRanges_Insert(&Runs, 5U, 1U) && UIListRanges_Contains(&Runs, 7U) && !UIListRanges_Contains(&Runs, 6U));
    // Removed rows drop out; what was apart comes together and merges.
    GenericBuffer_Clear(&Runs);
    Count = 0U;
    UIListRanges_Set(&Runs, 0U, 3U, true, &Count);
    UIListRanges_Set(&Runs, 6U, 3U, true, &Count);
    UIListRanges_Remove(&Runs, 3U, 3U, &Count);
    TEST_CHECK(run, (Runs._count == 1U) && (Count == 6U) && UIListRanges_Contains(&Runs, 5U) && !UIListRanges_Contains(&Runs, 6U));
    UIListRanges_Remove(&Runs, 1U, 2U, &Count);
    TEST_CHECK(run, (Runs._count == 1U) && (Count == 4U) && UIListRanges_Contains(&Runs, 3U) && !UIListRanges_Contains(&Runs, 4U));
    UIListRanges_Remove(&Runs, 0U, 100U, &Count);
    TEST_CHECK(run, (Runs._count == 0U) && (Count == 0U));
    // A removal past the end of SIZE_MAX's room stays sound.
    UIListRanges_Set(&Runs, 10U, 2U, true, &Count);
    UIListRanges_Remove(&Runs, 11U, SIZE_MAX - 11U, &Count);
    TEST_CHECK(run, (Runs._count == 1U) && (Count == 1U) && UIListRanges_Contains(&Runs, 10U));
    Memory_Free(Runs._data);
}


// Tests: the view.
static void AHundredThousandRowsCostTheRowsInView(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 100000U, .Dimmed = SIZE_MAX };
        size_t Before = UIScreen_GetWidgetCount(&Fixture.Screen);
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        TEST_CHECK(run, CountRowWidgets(&Fixture, Id) <= (ROWS_IN_VIEW + 1U));
        TEST_CHECK(run, Data.Asked <= (ROWS_IN_VIEW + 1U) * 2U);
        // The list, its rows' box, its bar and the rows: the data's size changes nothing.
        TEST_CHECK(run, (UIScreen_GetWidgetCount(&Fixture.Screen) - Before) <= (ROWS_IN_VIEW + 4U));
        // To the end and back: still about as many widgets, and only the rows that came into view were asked.
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.Screen, Id, (Vector2){ .x = 0.0f, .y = 1.0e9f }));
        TestUI_Ticks(run, &Fixture, 2U);
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y == ((100000.0f * ROW) - (ROWS_IN_VIEW * ROW)));
        TEST_CHECK(run, CountRowWidgets(&Fixture, Id) <= (ROWS_IN_VIEW + 1U));
        size_t AskedAtEnd = Data.Asked;
        TestUI_Ticks(run, &Fixture, 5U);
        TEST_CHECK(run, Data.Asked == AskedAtEnd); // Nothing moved: nothing is asked again.
        TEST_CHECK_SUCCESS(run, UIList_Refresh(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Data.Asked > AskedAtEnd) && (Data.Asked <= (AskedAtEnd + ROWS_IN_VIEW + 1U)));
        // The last row is there to click.
        TestUI_Click(run, &Fixture, LIST_X + 60.0f, ROWS_Y + (ROWS_IN_VIEW * ROW) - 5.0f);
        TEST_CHECK(run, IsSelected(run, &Fixture, Id, 99999U) && HasCurrent(run, &Fixture, Id, 99999U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AnIdleListAllocatesNothing(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 100000U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 2U);
        ClickRow(run, &Fixture, 3U);
        TestUI_MovePointer(&Fixture, LIST_X + 60.0f, RowY(5U));
        TestUI_Ticks(run, &Fixture, 10U);
        size_t Allocations = Memory_GetTotalAllocationCount();
        size_t Reallocations = Memory_GetTotalReallocationCount();
        TestUI_Ticks(run, &Fixture, 30U);
        TEST_CHECK(run, Memory_GetTotalAllocationCount() == Allocations);
        TEST_CHECK(run, Memory_GetTotalReallocationCount() == Reallocations);
        // Scrolling a row at a time binds a row whose text is no longer than before: the widgets stay, nothing is made.
        size_t Widgets = UIScreen_GetWidgetCount(&Fixture.Screen);
        for (size_t Step = 0U; Step < 20U; Step++)
        {
            TEST_CHECK_SUCCESS(run, UIScrollable_ScrollBy(&Fixture.Screen, Id, (Vector2){ .x = 0.0f, .y = ROW }));
            TestUI_Tick(run, &Fixture);
        }
        TEST_CHECK(run, UIScreen_GetWidgetCount(&Fixture.Screen) == Widgets);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the selection by the mouse.
static void ClicksSelectByTheModifiers(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Rows Data = { .Count = 50U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        // A click selects its row only, makes it current, enters the list and is one whole change.
        ClickRow(run, &Fixture, 2U);
        size_t Two[] = { 2U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 50U, Two, 1U) && HasCurrent(run, &Fixture, Id, 2U));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Id);
        TEST_CHECK(run, (Events.Count == 2U) && IsCountEvent(&Events, 0U, Id, UIElementEvent_Changed, 1.0)
            && IsCountEvent(&Events, 1U, Id, UIElementEvent_Committed, 1.0));
        // Ctrl toggles a row into the selection and out of it, and the row is the anchor.
        ClickRowWith(run, &Fixture, 9U, GameKey_LeftControl);
        size_t TwoNine[] = { 2U, 9U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 50U, TwoNine, 2U));
        ClickRowWith(run, &Fixture, 2U, GameKey_LeftControl);
        size_t Nine[] = { 9U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 50U, Nine, 1U));
        ClickRowWith(run, &Fixture, 5U, GameKey_LeftControl);
        size_t FiveNine[] = { 5U, 9U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 50U, FiveNine, 2U));
        // Shift selects from the anchor (the last row clicked without Shift) to the row; again from the same anchor.
        ClickRowWith(run, &Fixture, 8U, GameKey_LeftShift);
        size_t FiveToEight[] = { 5U, 6U, 7U, 8U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 50U, FiveToEight, 4U) && HasCurrent(run, &Fixture, Id, 8U));
        ClickRowWith(run, &Fixture, 3U, GameKey_LeftShift);
        size_t ThreeToFive[] = { 3U, 4U, 5U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 50U, ThreeToFive, 3U));
        // Ctrl and Shift add the run to what is selected.
        ClickRow(run, &Fixture, 0U);
        TestUI_Press(&Fixture, GameKey_LeftControl);
        TestUI_Press(&Fixture, GameKey_LeftShift);
        TestUI_Tick(run, &Fixture);
        ClickRow(run, &Fixture, 1U);
        TestUI_Release(&Fixture, GameKey_LeftShift);
        TestUI_Release(&Fixture, GameKey_LeftControl);
        TestUI_Tick(run, &Fixture);
        size_t ZeroOne[] = { 0U, 1U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 50U, ZeroOne, 2U));
        // A click on no row (below the last, in a short list) clears the selection; with Ctrl it keeps it.
        Data.Count = 3U;
        TestUI_Ticks(run, &Fixture, 2U);
        ClickRow(run, &Fixture, 0U);
        ClickRowWith(run, &Fixture, 7U, GameKey_LeftControl);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 1U);
        Events.Count = 0U;
        ClickRow(run, &Fixture, 7U);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 0U) && IsCountEvent(&Events, 0U, Id, UIElementEvent_Changed, 0.0));
        // A click that changes nothing posts nothing.
        ClickRow(run, &Fixture, 1U);
        Events.Count = 0U;
        ClickRow(run, &Fixture, 1U);
        TEST_CHECK(run, Events.Count == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APressOnASelectedRowWaitsForItsRelease(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 20U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        TEST_CHECK_SUCCESS(run, UIList_SetRangeSelected(&Fixture.Screen, Id, 2U, 4U, true));
        // Pressed, the selection stays (it could be dragged away); let go on the row, only it stays.
        TestUI_MovePointer(&Fixture, LIST_X + 60.0f, RowY(3U));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 4U);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        size_t Three[] = { 3U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 20U, Three, 1U));
        // Let go on another row, the selection stays as it was.
        TEST_CHECK_SUCCESS(run, UIList_SetRangeSelected(&Fixture.Screen, Id, 2U, 4U, true));
        TestUI_MovePointer(&Fixture, LIST_X + 60.0f, RowY(3U));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, LIST_X + 60.0f, RowY(8U));
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 4U);
        // A right press on a selected row keeps the selection; on another row selects only it.
        TestUI_MovePointer(&Fixture, LIST_X + 60.0f, RowY(4U));
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 4U) && HasCurrent(run, &Fixture, Id, 4U));
        TestUI_MovePointer(&Fixture, LIST_X + 60.0f, RowY(9U));
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        size_t Nine[] = { 9U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 20U, Nine, 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void SingleAndNoneModesKeepToTheirCounts(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 20U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Single, 0U);
        ClickRow(run, &Fixture, 2U);
        ClickRowWith(run, &Fixture, 5U, GameKey_LeftShift);
        size_t Five[] = { 5U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 20U, Five, 1U));
        ClickRowWith(run, &Fixture, 6U, GameKey_LeftControl);
        size_t Six[] = { 6U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 20U, Six, 1U));
        ClickRowWith(run, &Fixture, 6U, GameKey_LeftControl);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 0U);
        TEST_CHECK_ERROR(run, UIList_SetRangeSelected(&Fixture.Screen, Id, 0U, 2U, true), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UIList_SetSelected(&Fixture.Screen, Id, 3U, true));
        TEST_CHECK_SUCCESS(run, UIList_SetSelected(&Fixture.Screen, Id, 4U, true));
        size_t Four[] = { 4U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 20U, Four, 1U));
        // Several selected, then one mode: the first stays; none: nothing.
        TEST_CHECK_SUCCESS(run, UIList_SetSelectionMode(&Fixture.Screen, Id, UISelectionMode_Multiple));
        TEST_CHECK_SUCCESS(run, UIList_SetRangeSelected(&Fixture.Screen, Id, 7U, 3U, true));
        TEST_CHECK_SUCCESS(run, UIList_SetSelectionMode(&Fixture.Screen, Id, UISelectionMode_Single));
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 20U, Four, 1U));
        TEST_CHECK_SUCCESS(run, UIList_SetSelectionMode(&Fixture.Screen, Id, UISelectionMode_None));
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 0U);
        TEST_CHECK_ERROR(run, UIList_SetSelectionMode(&Fixture.Screen, Id, (UISelectionMode)9), ErrorCode_ArgumentOutOfRange);
        // None: a click moves the current row and selects nothing.
        ClickRow(run, &Fixture, 8U);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 0U) && HasCurrent(run, &Fixture, Id, 8U));
        TEST_CHECK_ERROR(run, UIList_SetSelected(&Fixture.Screen, Id, 1U, true), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the keys.
static void KeysReachTheListOnlyOnceEntered(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIButtonOptions Before = UIButtonOptions_Create(u8"before");
        Before.Element.HasPlacement = true;
        Before.Element.Placement = UIPlacement_FromTopLeft(10.0f, 10.0f, 60.0f, 24.0f);
        uint64_t BeforeId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Before, &BeforeId));
        Rows Data = { .Count = 30U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        Widget* List = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &List) && TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, List)));
        // Focused, the arrows pass it (to navigation), and nothing changes.
        Tap(run, &Fixture, GameKey_Down);
        Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 0U) && (Events.Count == 0U));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) != Id);
        // Enter enters it; the first row becomes current, and Down moves on and selects.
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &List) && TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, List)));
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Id) && HasCurrent(run, &Fixture, Id, 0U));
        Tap(run, &Fixture, GameKey_Down);
        size_t One[] = { 1U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 30U, One, 1U) && HasCurrent(run, &Fixture, Id, 1U));
        // Escape leaves it, the selection kept.
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID) && (SelectedCount(run, &Fixture, Id) == 1U));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void KeysMoveTheCurrentRowAndSelect(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Rows Data = { .Count = 100U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        ClickRow(run, &Fixture, 4U);
        Events.Count = 0U;
        Tap(run, &Fixture, GameKey_Down);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 5U) && IsSelected(run, &Fixture, Id, 5U) && (SelectedCount(run, &Fixture, Id) == 1U));
        TEST_CHECK(run, IsCountEvent(&Events, 0U, Id, UIElementEvent_Changed, 1.0) && IsCountEvent(&Events, 1U, Id, UIElementEvent_Committed, 1.0));
        // Shift extends from the anchor; Ctrl moves the current row alone.
        TapWith(run, &Fixture, GameKey_LeftShift, GameKey_Down);
        TapWith(run, &Fixture, GameKey_LeftShift, GameKey_Down);
        size_t FiveToSeven[] = { 5U, 6U, 7U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 100U, FiveToSeven, 3U));
        TapWith(run, &Fixture, GameKey_LeftShift, GameKey_Up);
        size_t FiveSix[] = { 5U, 6U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 100U, FiveSix, 2U));
        TapWith(run, &Fixture, GameKey_LeftControl, GameKey_Down);
        TapWith(run, &Fixture, GameKey_LeftControl, GameKey_Down);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 8U) && IsSelectionExactly(run, &Fixture, Id, 100U, FiveSix, 2U));
        // Ctrl and Space toggle the current row; Space alone selects only it.
        TapWith(run, &Fixture, GameKey_LeftControl, GameKey_Space);
        size_t FiveSixEight[] = { 5U, 6U, 8U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 100U, FiveSixEight, 3U));
        Tap(run, &Fixture, GameKey_Space);
        size_t Eight[] = { 8U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 100U, Eight, 1U));
        // The ends, a page, and the view follows the current row.
        Tap(run, &Fixture, GameKey_End);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 99U) && IsSelected(run, &Fixture, Id, 99U));
        TestUI_Tick(run, &Fixture);
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y == ((100.0f - (float)ROWS_IN_VIEW) * ROW));
        Tap(run, &Fixture, GameKey_PageUp);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 99U - (ROWS_IN_VIEW - 1U)));
        Tap(run, &Fixture, GameKey_Home);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 0U));
        Tap(run, &Fixture, GameKey_Up);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 0U)); // At an end, still consumed and nothing moves.
        TapWith(run, &Fixture, GameKey_LeftShift, GameKey_PageDown);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == ROWS_IN_VIEW);
        // Select all.
        TapWith(run, &Fixture, GameKey_LeftControl, GameKey_A);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 100U);
        size_t Ranges = 0U;
        TEST_CHECK_SUCCESS(run, UIList_GetSelection(&Fixture.Screen, Id, NULL, &Ranges));
        TEST_CHECK(run, Ranges == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EnterAndADoubleClickActivateTheRow(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Events.IsRecordingInputEvents = true;
        Rows Data = { .Count = 20U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        ClickRow(run, &Fixture, 6U);
        Events.Count = 0U;
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (Events.Count == 1U) && IsItemEvent(&Events, 0U, Id, UIElementEvent_Activated, 6U));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Id); // Enter is the list's: it stays entered.
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, LIST_X + 60.0f, RowY(3U));
        TestUI_Click(run, &Fixture, LIST_X + 60.0f, RowY(3U));
        bool IsDoubleClicked = false;
        bool IsActivated = false;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            IsDoubleClicked = IsDoubleClicked || IsItemEvent(&Events, Index, Id, UIElementEvent_DoubleClicked, 3U);
            IsActivated = IsActivated || IsItemEvent(&Events, Index, Id, UIElementEvent_Activated, 3U);
        }
        TEST_CHECK(run, IsDoubleClicked && IsActivated);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A context menu provider that remembers which row it was asked about. */
static Error RememberMenuRow(UIScreen* screen, const UIContextMenuRequest* request, const UserData* userData)
{
    size_t* Row = UserData_GetPointer(userData);
    bool Has = false;
    Error Result = UIList_GetCurrent(screen, request->ProviderWidgetId, &Has, Row);
    if (!Has)
    {
        *Row = SIZE_MAX;
    }
    return Result;
}

static void ARightClickAsksForTheRowsMenu(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Events.IsRecordingInputEvents = true;
        Rows Data = { .Count = 20U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        size_t MenuRow = SIZE_MAX;
        UserData Remember = UserData_FromPointer(&MenuRow);
        TEST_CHECK_SUCCESS(run, UIContextMenu_SetProvider(&Fixture.Screen, Id, RememberMenuRow, &Remember));
        TestUI_MovePointer(&Fixture, LIST_X + 60.0f, RowY(7U));
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, MenuRow == 7U);
        bool IsRequested = false;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            IsRequested = IsRequested || IsItemEvent(&Events, Index, Id, UIElementEvent_ContextRequested, 7U);
        }
        TEST_CHECK(run, IsRequested);
        // The menu key asks for the current row's.
        MenuRow = SIZE_MAX;
        TEST_CHECK_SUCCESS(run, UIList_SetCurrent(&Fixture.Screen, Id, 2U));
        Tap(run, &Fixture, GameKey_Menu);
        TEST_CHECK(run, MenuRow == 2U);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the data changing under the view.
static void InsertedAndRemovedRowsMoveTheSelection(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 20U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        TEST_CHECK_SUCCESS(run, UIList_SetRangeSelected(&Fixture.Screen, Id, 4U, 3U, true));
        TEST_CHECK_SUCCESS(run, UIList_SetSelected(&Fixture.Screen, Id, 10U, true));
        TEST_CHECK_SUCCESS(run, UIList_SetCurrent(&Fixture.Screen, Id, 10U));
        // Two rows inserted before them: everything moves down by two.
        Data.Count = 22U;
        TEST_CHECK_SUCCESS(run, UIList_InsertRows(&Fixture.Screen, Id, 1U, 2U));
        TestUI_Tick(run, &Fixture);
        size_t Moved[] = { 6U, 7U, 8U, 12U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 22U, Moved, 4U) && HasCurrent(run, &Fixture, Id, 12U));
        // Rows removed among them: the removed ones drop out, the rest move up.
        Data.Count = 19U;
        TEST_CHECK_SUCCESS(run, UIList_RemoveRows(&Fixture.Screen, Id, 7U, 3U));
        TestUI_Tick(run, &Fixture);
        size_t Left[] = { 6U, 9U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 19U, Left, 2U) && HasCurrent(run, &Fixture, Id, 9U));
        // The current row removed: the row that took its place is current.
        Data.Count = 18U;
        TEST_CHECK_SUCCESS(run, UIList_RemoveRows(&Fixture.Screen, Id, 9U, 1U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 9U) && (SelectedCount(run, &Fixture, Id) == 1U));
        TEST_CHECK_ERROR(run, UIList_RemoveRows(&Fixture.Screen, Id, 17U, 2U), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIList_InsertRows(&Fixture.Screen, Id, 19U, 1U), ErrorCode_ArgumentOutOfRange);
        // Other data: nothing selected or current.
        Rows Other = { .Count = 5U, .Dimmed = SIZE_MAX };
        UserData OtherData = UserData_FromPointer(&Other);
        TEST_CHECK_SUCCESS(run, UIList_SetProvider(&Fixture.Screen, Id, CountRows, WriteRow, &OtherData));
        bool Has = true;
        TEST_CHECK_SUCCESS(run, UIList_GetCurrent(&Fixture.Screen, Id, &Has, NULL));
        TEST_CHECK(run, !Has && (SelectedCount(run, &Fixture, Id) == 0U));
        TEST_CHECK_ERROR(run, UIList_SetProvider(&Fixture.Screen, Id, CountRows, NULL, NULL), ErrorCode_IllegalArgument);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ACountChangedWithNoWordIsTakenAsItIs(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 30U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        TEST_CHECK_SUCCESS(run, UIList_SetRangeSelected(&Fixture.Screen, Id, 5U, 20U, true));
        TEST_CHECK_SUCCESS(run, UIList_SetCurrent(&Fixture.Screen, Id, 25U));
        Data.Count = 10U;
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 5U) && HasCurrent(run, &Fixture, Id, 9U));
        Data.Count = 0U;
        TestUI_Tick(run, &Fixture);
        bool Has = true;
        TEST_CHECK_SUCCESS(run, UIList_GetCurrent(&Fixture.Screen, Id, &Has, NULL));
        TEST_CHECK(run, !Has && (SelectedCount(run, &Fixture, Id) == 0U) && (CountRowWidgets(&Fixture, Id) <= (ROWS_IN_VIEW + 1U)));
        // Keys on an empty list do nothing.
        ClickRow(run, &Fixture, 0U);
        Tap(run, &Fixture, GameKey_Down);
        Tap(run, &Fixture, GameKey_End);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Both modifiers held for a click, as Ctrl+Shift. */
static void ClickRowWithBoth(TestRun* run, TestUIFixture* fixture, size_t row)
{
    TestUI_Press(fixture, GameKey_LeftControl);
    TestUI_Press(fixture, GameKey_LeftShift);
    TestUI_Tick(run, fixture);
    ClickRow(run, fixture, row);
    TestUI_Release(fixture, GameKey_LeftControl);
    TestUI_Release(fixture, GameKey_LeftShift);
    TestUI_Tick(run, fixture);
}

static size_t CountCommits(const TestUIEvents* events, uint64_t id)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        Count += ((events->Events[Index].WidgetId == id) && (events->Events[Index].WidgetEventId == (int32_t)UIElementEvent_Committed)) ? 1U : 0U;
    }
    return Count;
}

static void TheSelectionsEdgesKeepTheirRules(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Rows Data = { .Count = 30U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        // A Shift click whose span is the selection's first run still drops what lies past it.
        TEST_CHECK_SUCCESS(run, UIList_SetRangeSelected(&Fixture.Screen, Id, 2U, 3U, true));
        TEST_CHECK_SUCCESS(run, UIList_SetSelected(&Fixture.Screen, Id, 8U, true));
        TEST_CHECK_SUCCESS(run, UIList_SetCurrent(&Fixture.Screen, Id, 2U)); // The anchor too.
        Events.Count = 0U;
        ClickRowWith(run, &Fixture, 4U, GameKey_LeftShift);
        size_t Span[] = { 2U, 3U, 4U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 30U, Span, 3U) && (CountCommits(&Events, Id) == 1U));
        // Ctrl and Shift add the span from the anchor and keep the rest; a whole change.
        ClickRowWith(run, &Fixture, 8U, GameKey_LeftControl); // The anchor moves to 8.
        Events.Count = 0U;
        ClickRowWithBoth(run, &Fixture, 6U);
        size_t Added[] = { 2U, 3U, 4U, 6U, 7U, 8U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 30U, Added, 6U) && (CountCommits(&Events, Id) == 1U));
        // The same again changes nothing: no event.
        Events.Count = 0U;
        ClickRowWithBoth(run, &Fixture, 6U);
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 30U, Added, 6U) && (CountCommits(&Events, Id) == 0U));
        // Select all, then again: the second changes nothing and posts nothing.
        TapWith(run, &Fixture, GameKey_LeftControl, GameKey_A);
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 30U);
        Events.Count = 0U;
        TapWith(run, &Fixture, GameKey_LeftControl, GameKey_A);
        TEST_CHECK(run, CountCommits(&Events, Id) == 0U);
        // Rows inserted above the anchor and at the current row move them both.
        ClickRow(run, &Fixture, 5U); // Anchor and current 5.
        Data.Count = 33U;
        TEST_CHECK_SUCCESS(run, UIList_InsertRows(&Fixture.Screen, Id, 0U, 3U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 8U));
        Data.Count = 35U;
        TEST_CHECK_SUCCESS(run, UIList_InsertRows(&Fixture.Screen, Id, 8U, 2U)); // At the current row: it moves down too.
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasCurrent(run, &Fixture, Id, 10U));
        ClickRowWith(run, &Fixture, 9U, GameKey_LeftShift); // From the anchor, now 10.
        size_t FromAnchor[] = { 9U, 10U };
        TEST_CHECK(run, IsSelectionExactly(run, &Fixture, Id, 35U, FromAnchor, 2U));
        // A list with no current row entered by a key starts at its first selected row.
        Rows Other = { .Count = 30U, .Dimmed = SIZE_MAX };
        uint64_t OtherId = AddList(run, &Fixture, &Other, UISelectionMode_Multiple, 0U);
        TEST_CHECK_SUCCESS(run, UIList_SetSelected(&Fixture.Screen, OtherId, 6U, true));
        Widget* OtherList = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, OtherId, &OtherList)
            && TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, OtherList)));
        TestUI_Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, Widget_IsEntered(OtherList) && HasCurrent(run, &Fixture, OtherId, 6U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The caller's items: what each row shows is the item at its index, so a change of the data shows only when the rows are written again. */
typedef struct ItemsStruct
{
    uint32_t Values[64];
    size_t Count;
    size_t Odd;
} Items;

static size_t CountItems(UIScreen* screen, uint64_t listId, const UserData* userData)
{
    (void)screen;
    (void)listId;
    return ((const Items*)UserData_GetPointer(userData))->Count;
}

static Error WriteItem(UIScreen* screen, uint64_t listId, const UserData* userData, size_t index, UIListRowData* outRow)
{
    (void)screen;
    (void)listId;
    const Items* Data = UserData_GetPointer(userData);
    if (index == Data->Odd)
    {
        // A text with a NUL inside, then a long one cut at a codepoint: "a" and 300 two-byte letters.
        GenericBuffer_AddLastRange(outRow->Text, (void*)"cut\0here", 8U);
        return Error_CreateSuccess();
    }
    if (index == (Data->Odd + 1U))
    {
        GenericBuffer_AppendString(outRow->Text, u8"a");
        for (size_t Letter = 0U; Letter < 300U; Letter++)
        {
            GenericBuffer_AppendString(outRow->Text, u8"ā");
        }
        return Error_CreateSuccess();
    }
    GenericBuffer_AppendString(outRow->Text, u8"Item ");
    return Number_UInt64ToString((uint64_t)Data->Values[index], 10, false, outRow->Text);
}

/* The text the row showing a data row shows, or NULL when no row shows it. */
static const char* ShownText(TestUIFixture* fixture, uint64_t listId, size_t dataIndex, uint64_t* outRowId)
{
    Widget* List = NULL;
    uint64_t RowId = WIDGET_ID_INVALID;
    const unsigned char* Text = NULL;
    bool IsBound = false;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, listId, &List)
        || !UIRowRecycler_FindWidget(&((const UIListLookBase*)(const void*)List)->List._rows, dataIndex, &RowId)
        || !UIListRow_Peek(&fixture->Screen, RowId, &Text, &IsBound, NULL) || !IsBound)
    {
        return NULL;
    }
    if (outRowId != NULL)
    {
        *outRowId = RowId;
    }
    return (const char*)Text;
}

static bool Shows(TestUIFixture* fixture, uint64_t listId, size_t dataIndex, const char* text)
{
    const char* Shown = ShownText(fixture, listId, dataIndex, NULL);
    return (Shown != NULL) && (strcmp(Shown, text) == 0);
}

static void RowsShowTheDataAfterEveryChange(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Items Data = { .Count = 40U, .Odd = 60U };
        for (size_t Index = 0U; Index < 64U; Index++)
        {
            Data.Values[Index] = (uint32_t)Index;
        }
        UIListOptions Options = UIListOptions_Create();
        Options.SelectionMode = UISelectionMode_Multiple;
        Options.CountProvider = CountItems;
        Options.RowProvider = WriteItem;
        Options.ProviderData = UserData_FromPointer(&Data);
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEIGHT);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateList(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, Shows(&Fixture, Id, 0U, "Item 0") && Shows(&Fixture, Id, 9U, "Item 9"));
        // Two items inserted at the top: the rows in view show what now stands at their index.
        Memory_Move(Data.Values, Data.Values + 2U, 40U * sizeof(uint32_t));
        Data.Values[0] = 100U;
        Data.Values[1] = 101U;
        Data.Count = 42U;
        TEST_CHECK_SUCCESS(run, UIList_InsertRows(&Fixture.Screen, Id, 0U, 2U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Shows(&Fixture, Id, 0U, "Item 100") && Shows(&Fixture, Id, 2U, "Item 0"));
        // Three removed from the top.
        Memory_Move(Data.Values + 3U, Data.Values, 39U * sizeof(uint32_t));
        Data.Count = 39U;
        TEST_CHECK_SUCCESS(run, UIList_RemoveRows(&Fixture.Screen, Id, 0U, 3U));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Shows(&Fixture, Id, 0U, "Item 1") && Shows(&Fixture, Id, 5U, "Item 6"));
        // The count and the items changed with no word: the rows are written again too.
        for (size_t Index = 0U; Index < 64U; Index++)
        {
            Data.Values[Index] = (uint32_t)(1000U + Index);
        }
        Data.Count = 38U;
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Shows(&Fixture, Id, 0U, "Item 1000") && Shows(&Fixture, Id, 3U, "Item 1003"));
        // Scrolled: the rows bound are the ones in view, placed at the view's top, showing their selection.
        TEST_CHECK_SUCCESS(run, UIList_SetSelected(&Fixture.Screen, Id, 21U, true));
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.Screen, Id, (Vector2){ .x = 0.0f, .y = 20.0f * ROW }));
        TestUI_Tick(run, &Fixture);
        uint64_t RowId = WIDGET_ID_INVALID;
        TEST_CHECK(run, Shows(&Fixture, Id, 20U, "Item 1020") && (ShownText(&Fixture, Id, 0U, NULL) == NULL));
        TEST_CHECK(run, (ShownText(&Fixture, Id, 20U, &RowId) != NULL) && TestUI_IsSamePixel(TestUI_GetPixelBox(run, &Fixture, RowId).y, ROWS_Y));
        UIListRowView View;
        uint64_t Selected = WIDGET_ID_INVALID;
        TEST_CHECK(run, (ShownText(&Fixture, Id, 21U, &Selected) != NULL) && UIListRow_Peek(&Fixture.Screen, Selected, NULL, NULL, &View)
            && View.IsSelected);
        TEST_CHECK(run, UIListRow_Peek(&Fixture.Screen, RowId, NULL, NULL, &View) && !View.IsSelected);
        // A text ends at its first NUL, and a long one is cut at a codepoint within the most a row shows.
        Data.Odd = 22U;
        TEST_CHECK_SUCCESS(run, UIList_Refresh(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);
        const char* Long = ShownText(&Fixture, Id, 23U, NULL);
        TEST_CHECK(run, Shows(&Fixture, Id, 22U, "cut") && (Long != NULL) && (strlen(Long) == 511U)
            && StringUTF8_IsEncodingValid((const unsigned char*)Long));
    }
    TestUI_Deconstruct(run, &Fixture);
}


static void APressOnASelectedRowsCellDragsNothing(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Rows Data = { .Count = 30U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 2U);
        TEST_CHECK_SUCCESS(run, UIList_SetDragSource(&Fixture.Screen, Id, true, 1U));
        TEST_CHECK_SUCCESS(run, UIList_SetRangeSelected(&Fixture.Screen, Id, 2U, 3U, true));
        float LastCellX = ROWS_X + 288.0f - 4.0f - 6.0f;
        // A press on a selected row's cell moved past the drag distance, inside the cell: no drag, and the release on it toggles it.
        Events.Count = 0U;
        TestUI_MovePointer(&Fixture, LastCellX - 3.0f, RowY(3U) - 3.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, LastCellX + 3.0f, RowY(3U) + 3.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIDragDrop_IsDragging(&Fixture.Screen));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 1U) && IsItemEvent(&Events, 0U, Id, UIElementEvent_CellToggled, 3U) && (SelectedCount(run, &Fixture, Id) == 3U));
        // The same press on the row beside the cells drags the selection.
        TestUI_MovePointer(&Fixture, LastCellX - 60.0f, RowY(3U));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, LastCellX - 60.0f, RowY(3U) + 8.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIDragDrop_IsDragging(&Fixture.Screen));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AListMadeWithItsDataHasItsRowsAtOnce(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 30U, .Dimmed = SIZE_MAX };
        UIListOptions Options = UIListOptions_Create();
        Options.SelectionMode = UISelectionMode_Multiple;
        Options.CountProvider = CountRows;
        Options.RowProvider = WriteRow;
        Options.ProviderData = UserData_FromPointer(&Data);
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEIGHT);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateList(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        // Before any update: the current object selected and shown, as an outliner opens.
        TEST_CHECK_SUCCESS(run, UIList_SetSelected(&Fixture.Screen, Id, 25U, true));
        TEST_CHECK_SUCCESS(run, UIList_SetCurrent(&Fixture.Screen, Id, 25U));
        TEST_CHECK_SUCCESS(run, UIList_Reveal(&Fixture.Screen, Id, 25U));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsSelected(run, &Fixture, Id, 25U) && HasCurrent(run, &Fixture, Id, 25U));
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y > 0.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: cells, the wheel, disabled, places.
static void ToggleCellsPostTheirRowAndCell(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Rows Data = { .Count = 30U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 2U);
        // The rows are 288 wide (the bar takes 10): the last cell stands 4 from the right edge, 12 wide; the other 4 before it.
        float LastCellX = ROWS_X + 288.0f - 4.0f - 6.0f;
        float FirstCellX = LastCellX - 16.0f;
        TestUI_Click(run, &Fixture, LastCellX, RowY(3U));
        TEST_CHECK(run, (Events.Count == 1U) && IsItemEvent(&Events, 0U, Id, UIElementEvent_CellToggled, 3U)
            && Events.Events[0].HasIndex && (Events.Events[0].Index == 1));
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 0U); // A cell's click selects nothing.
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, FirstCellX, RowY(5U));
        TEST_CHECK(run, (Events.Count == 1U) && IsItemEvent(&Events, 0U, Id, UIElementEvent_CellToggled, 5U) && (Events.Events[0].Index == 0));
        // Pressed on a cell and let go on another: nothing.
        Events.Count = 0U;
        TestUI_MovePointer(&Fixture, FirstCellX, RowY(5U));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, LastCellX, RowY(5U));
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
        // Between the cells is the row: a click there selects it.
        TestUI_Click(run, &Fixture, LastCellX - 9.0f, RowY(5U));
        TEST_CHECK(run, IsSelected(run, &Fixture, Id, 5U));
        TEST_CHECK_ERROR(run, UIList_SetCells(&Fixture.Screen, Id, UI_LIST_MAX_CELLS + 1U, NULL), ErrorCode_ArgumentOutOfRange);
        UIElementIcon Bad = { .Kind = UIElementIconKind_Letter, .Letter = 0U };
        TEST_CHECK_ERROR(run, UIList_SetCells(&Fixture.Screen, Id, 1U, &Bad), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIList_SetCells(&Fixture.Screen, Id, 0U, NULL));
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, LastCellX, RowY(7U));
        TEST_CHECK(run, IsSelected(run, &Fixture, Id, 7U) && (Events.Count == 2U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void TheWheelScrollsThenGoesOn(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 15U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        TestUI_MovePointer(&Fixture, LIST_X + 60.0f, RowY(3U));
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y == (3.0f * ROW));
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y == (5.0f * ROW)); // To the end.
        TEST_CHECK(run, SelectedCount(run, &Fixture, Id) == 0U);
        // At the end it is left alone: the game gets it (no widget used it, but it was over UI: consumed).
        TestUI_Wheel(&Fixture, -1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Id, &Offset));
        TEST_CHECK(run, Offset.y == (5.0f * ROW));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADisabledListSwallowsPresses(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Rows Data = { .Count = 20U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        TEST_CHECK_SUCCESS(run, UIList_SetIsEnabled(&Fixture.Screen, Id, false));
        Events.IsRecordingInputEvents = true;
        ClickRow(run, &Fixture, 2U);
        TEST_CHECK(run, (SelectedCount(run, &Fixture, Id) == 0U) && (Events.Count == 0U));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) != Id);
        TEST_CHECK_SUCCESS(run, UIList_SetIsEnabled(&Fixture.Screen, Id, true));
        ClickRow(run, &Fixture, 2U);
        TEST_CHECK(run, IsSelected(run, &Fixture, Id, 2U) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id));
        // A stale id, a widget that is no list.
        uint64_t ButtonId = WIDGET_ID_INVALID;
        UIButtonOptions Button = UIButtonOptions_Create(u8"b");
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Button, &ButtonId));
        TEST_CHECK_ERROR(run, UIList_Refresh(&Fixture.Screen, ButtonId), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIList_SetCurrent(&Fixture.Screen, Id, 20U), ErrorCode_ArgumentOutOfRange);
        UIListOptions Bad = UIListOptions_Create();
        Bad.VisibleRows = 0U;
        uint64_t BadId = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateList(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Bad, &BadId), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, BadId == WIDGET_ID_INVALID);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void WhereAPointFalls(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        Rows Data = { .Count = 40U, .Dimmed = SIZE_MAX };
        uint64_t Id = AddList(run, &Fixture, &Data, UISelectionMode_Multiple, 0U);
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.Screen, Id, (Vector2){ .x = 0.0f, .y = 30.0f }));
        TestUI_Tick(run, &Fixture);
        // Normalized screen coordinates: the surface is 1000 x 500.
        bool Has = false;
        size_t Row = 0U;
        Vector2 At = { .x = (LIST_X + 60.0f) / TEST_UI_SURFACE_WIDTH, .y = (ROWS_Y + 5.0f) / TEST_UI_SURFACE_HEIGHT };
        TEST_CHECK_SUCCESS(run, UIList_GetItemAt(&Fixture.Screen, Id, At, &Has, &Row));
        TEST_CHECK(run, Has && (Row == 1U)); // 30 scrolled: 5 pixels down is row 1's second half.
        At.y = (ROWS_Y + 12.0f) / TEST_UI_SURFACE_HEIGHT;
        TEST_CHECK_SUCCESS(run, UIList_GetItemAt(&Fixture.Screen, Id, At, &Has, &Row));
        TEST_CHECK(run, Has && (Row == 2U));
        size_t Drop = 0U;
        TEST_CHECK_SUCCESS(run, UIList_GetDropIndex(&Fixture.Screen, Id, At, &Drop));
        TEST_CHECK(run, Drop == 2U); // 42 into the content: nearer row 2's top than its bottom.
        At.y = (ROWS_Y + 25.0f) / TEST_UI_SURFACE_HEIGHT;
        TEST_CHECK_SUCCESS(run, UIList_GetDropIndex(&Fixture.Screen, Id, At, &Drop));
        TEST_CHECK(run, Drop == 3U);
        At.y = 0.0f;
        TEST_CHECK_SUCCESS(run, UIList_GetItemAt(&Fixture.Screen, Id, At, &Has, &Row));
        TEST_CHECK(run, !Has);
        TEST_CHECK_SUCCESS(run, UIList_GetDropIndex(&Fixture.Screen, Id, At, &Drop));
        TEST_CHECK(run, Drop == 0U);
        // Below the last row the drop goes after it.
        Data.Count = 5U;
        TestUI_Ticks(run, &Fixture, 2U);
        At.y = (ROWS_Y + 150.0f) / TEST_UI_SURFACE_HEIGHT;
        TEST_CHECK_SUCCESS(run, UIList_GetDropIndex(&Fixture.Screen, Id, At, &Drop));
        TEST_CHECK(run, Drop == 5U);
        TEST_CHECK_SUCCESS(run, UIList_GetItemAt(&Fixture.Screen, Id, At, &Has, &Row));
        TEST_CHECK(run, !Has);
        // Over the bar is no row.
        Data.Count = 40U;
        TestUI_Ticks(run, &Fixture, 2U);
        At = (Vector2){ .x = (LIST_X + LIST_WIDTH - 5.0f) / TEST_UI_SURFACE_WIDTH, .y = (ROWS_Y + 12.0f) / TEST_UI_SURFACE_HEIGHT };
        TEST_CHECK_SUCCESS(run, UIList_GetItemAt(&Fixture.Screen, Id, At, &Has, &Row));
        TEST_CHECK(run, !Has);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "the_runs_stay_sorted_apart_and_counted", TheRunsStaySortedApartAndCounted },
    { "a_hundred_thousand_rows_cost_the_rows_in_view", AHundredThousandRowsCostTheRowsInView },
    { "an_idle_list_allocates_nothing", AnIdleListAllocatesNothing },
    { "clicks_select_by_the_modifiers", ClicksSelectByTheModifiers },
    { "a_press_on_a_selected_row_waits_for_its_release", APressOnASelectedRowWaitsForItsRelease },
    { "single_and_none_modes_keep_to_their_counts", SingleAndNoneModesKeepToTheirCounts },
    { "keys_reach_the_list_only_once_entered", KeysReachTheListOnlyOnceEntered },
    { "keys_move_the_current_row_and_select", KeysMoveTheCurrentRowAndSelect },
    { "enter_and_a_double_click_activate_the_row", EnterAndADoubleClickActivateTheRow },
    { "a_right_click_asks_for_the_rows_menu", ARightClickAsksForTheRowsMenu },
    { "inserted_and_removed_rows_move_the_selection", InsertedAndRemovedRowsMoveTheSelection },
    { "a_count_changed_with_no_word_is_taken_as_it_is", ACountChangedWithNoWordIsTakenAsItIs },
    { "the_selections_edges_keep_their_rules", TheSelectionsEdgesKeepTheirRules },
    { "rows_show_the_data_after_every_change", RowsShowTheDataAfterEveryChange },
    { "a_press_on_a_selected_rows_cell_drags_nothing", APressOnASelectedRowsCellDragsNothing },
    { "a_list_made_with_its_data_has_its_rows_at_once", AListMadeWithItsDataHasItsRowsAtOnce },
    { "toggle_cells_post_their_row_and_cell", ToggleCellsPostTheirRowAndCell },
    { "the_wheel_scrolls_then_goes_on", TheWheelScrollsThenGoesOn },
    { "a_disabled_list_swallows_presses", ADisabledListSwallowsPresses },
    { "where_a_point_falls", WhereAPointFalls },
};


// Public functions.
void UIListTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIList", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
