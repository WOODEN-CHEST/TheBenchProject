#include "TestSuites.h"
#include "TestUI.h"
#include "UIDragDrop.h"
#include "UIKit.h"
#include "UIList.h"
#include "UIWidgetFactory.h"
#include "wr/WRNumber.h"
#include "wr/WRUserData.h"
#include <string.h>


// Macros.
/** The fixture's look: two lists of 20 pixel rows, buttons, a scroll area, what a drag shows. */
#define DRAG_THEME \
    "{\"name\": \"drags\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"flat\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"scrollbar\": {\"thickness\": 10, \"min_thumb_length\": 10, \"thumb_inset\": 0, \"track\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}," \
    "\"scroll_area\": {\"background\": {\"kind\": \"flat\"}}," \
    "\"list\": {\"padding\": [1, 1], \"row_height\": 20, \"row_padding\": 4, \"cell_size\": 12, \"insertion_thickness\": 2," \
    "\"background\": {\"kind\": \"flat\"}, \"row\": {\"kind\": \"flat\"}, \"current\": {\"kind\": \"none\"}, \"text\": {\"normal\": \"#e0e0e0\"}," \
    "\"cell\": {\"kind\": \"flat\"}, \"cell_text\": {\"normal\": \"#e0e0e0\"}, \"insertion\": {\"kind\": \"flat\"}}," \
    "\"drag\": {\"padding\": [4, 2], \"offset\": [10, 10], \"max_width\": 200, \"ghost\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}," \
    "\"target\": {\"kind\": \"flat\"}}}}"
#define ROWS_KIND ((uint32_t)7U)
#define OTHER_KIND ((uint32_t)9U)
#define ROW 20.0f
#define SOURCE_X 50.0f
#define SOURCE_Y 50.0f
#define TARGET_X 500.0f
#define TARGET_Y 50.0f


// Types.
/* The drags' events a screen delivered. */
typedef struct DragEventsStruct
{
    UIEvent Events[16];
    size_t Count;
} DragEvents;

/* A list's data: a count. */
typedef struct RowsStruct
{
    size_t Count;
} Rows;


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, DRAG_THEME);
}

static Error RecordDrag(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    DragEvents* Events = UserData_GetPointer(userData);
    if (Events->Count < (sizeof(Events->Events) / sizeof(Events->Events[0])))
    {
        Events->Events[Events->Count++] = *event;
    }
    return Error_CreateSuccess();
}

static bool RecordDrags(TestRun* run, TestUIFixture* fixture, DragEvents* events)
{
    Memory_Zero(events, sizeof(*events));
    UserData Data = UserData_FromPointer(events);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    return TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&fixture->Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Drag), RecordDrag, &Data,
        &Subscription));
}

static const UIEvent* FindDrag(const DragEvents* events, uint64_t widgetId, UIDragEvent event)
{
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        if ((events->Events[Index].WidgetId == widgetId) && (events->Events[Index].WidgetEventId == (int32_t)event))
        {
            return &events->Events[Index];
        }
    }
    return NULL;
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
    (void)userData;
    GenericBuffer_AppendString(outRow->Text, u8"Row ");
    return Number_UInt64ToString((uint64_t)index, 10, false, outRow->Text);
}

static uint64_t AddList(TestRun* run, TestUIFixture* fixture, uint64_t parentId, Rows* rows, float x, float y, uint32_t dragKind)
{
    UIListOptions Options = UIListOptions_Create();
    Options.CountProvider = CountRows;
    Options.RowProvider = WriteRow;
    Options.ProviderData = UserData_FromPointer(rows);
    Options.IsDragSource = dragKind != 0U;
    Options.DragKind = dragKind;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(x, y, 200.0f, 202.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateList(&fixture->Kit, &fixture->Screen, parentId, &Options, &Id));
    return Id;
}

/* A target that takes one kind of payload. */
static bool AcceptsKind(UIScreen* screen, const UIDragQuery* query, const UserData* userData)
{
    (void)screen;
    uint32_t Kind = 0U;
    Memory_Copy(userData->Bytes, &Kind, sizeof(Kind));
    return query->Payload->Kind == Kind;
}

static void MakeTarget(TestRun* run, TestUIFixture* fixture, uint64_t widgetId, uint32_t kind)
{
    UserData Data = UserData_CreateEmpty();
    Memory_Copy(&kind, Data.Bytes, sizeof(kind));
    TEST_CHECK_SUCCESS(run, UIDragDrop_SetTarget(&fixture->Screen, widgetId, AcceptsKind, &Data));
}

static float RowY(float top, size_t row)
{
    return top + 1.0f + ((float)row * ROW) + (ROW * 0.5f);
}

static void PressAt(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Press(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
}

static void MoveTo(TestRun* run, TestUIFixture* fixture, float x, float y)
{
    TestUI_MovePointer(fixture, x, y);
    TestUI_Tick(run, fixture);
}

static void Release(TestRun* run, TestUIFixture* fixture)
{
    TestUI_Release(fixture, GameKey_MouseLeft);
    TestUI_Tick(run, fixture);
}

/* A plain widget's drag: a button that gives its own payload. */
static Error ButtonPayload(UIScreen* screen, uint64_t sourceId, Vector2 pressScreenPosition, const UserData* userData, UIDragPayload* outPayload,
    bool* outIsDragged)
{
    (void)screen;
    (void)pressScreenPosition;
    (void)userData;
    outPayload->Kind = OTHER_KIND;
    outPayload->Value = sourceId;
    *outIsDragged = true;
    return UIDragPayload_SetText(outPayload, u8"the button", 10U);
}


// Tests.
static void APressDragsOnlyPastTheDistance(TestRun* run)
{
    TestUIFixture Fixture;
    DragEvents Drags;
    if (Construct(run, &Fixture) && RecordDrags(run, &Fixture, &Drags))
    {
        Rows Data = { .Count = 30U };
        uint64_t Source = AddList(run, &Fixture, WIDGET_ID_INVALID, &Data, SOURCE_X, SOURCE_Y, ROWS_KIND);
        TestUI_Ticks(run, &Fixture, 3U);
        // Three pixels: a press still, the row selected at the press; four: a drag of the selection, the press the drag's now.
        PressAt(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 2U));
        MoveTo(run, &Fixture, SOURCE_X + 53.0f, RowY(SOURCE_Y, 2U));
        TEST_CHECK(run, !UIDragDrop_IsDragging(&Fixture.Screen));
        MoveTo(run, &Fixture, SOURCE_X + 54.0f, RowY(SOURCE_Y, 2U));
        UIDragSession Session;
        TEST_CHECK(run, UIDragDrop_GetSession(&Fixture.Screen, &Session) && (Session.SourceId == Source) && (Session.Payload.Kind == ROWS_KIND)
            && (Session.Payload.Value == 2U) && (strcmp((const char*)Session.Payload.Text, "Row 2") == 0));
        TEST_CHECK(run, UIScreen_GetCursorShape(&Fixture.Screen) == UICursorShape_NotAllowed); // Over no target that would take it.
        // Released where nothing takes it: Cancelled to the source, the selection as it was.
        Release(run, &Fixture);
        TEST_CHECK(run, !UIDragDrop_IsDragging(&Fixture.Screen) && (FindDrag(&Drags, Source, UIDragEvent_Cancelled) != NULL));
        bool IsSelected = false;
        TEST_CHECK_SUCCESS(run, UIList_IsSelected(&Fixture.Screen, Source, 2U, &IsSelected));
        TEST_CHECK(run, IsSelected && !UIScreen_HasHeldPress(&Fixture.Screen));
        // Several selected: the drag is all of them, named by the pressed row; the press no longer selects only it at its release.
        TEST_CHECK_SUCCESS(run, UIList_SetRangeSelected(&Fixture.Screen, Source, 4U, 3U, true));
        PressAt(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 5U));
        MoveTo(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 9U));
        TEST_CHECK(run, UIDragDrop_GetSession(&Fixture.Screen, &Session) && (strcmp((const char*)Session.Payload.Text, "4 rows") == 0));
        Release(run, &Fixture);
        size_t Count = 0U;
        TEST_CHECK_SUCCESS(run, UIList_GetSelection(&Fixture.Screen, Source, &Count, NULL));
        TEST_CHECK(run, Count == 4U);
        // A press on no row drags nothing.
        Data.Count = 3U;
        TestUI_Ticks(run, &Fixture, 2U);
        PressAt(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 6U));
        MoveTo(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 8U));
        TEST_CHECK(run, !UIDragDrop_IsDragging(&Fixture.Screen));
        Release(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ATargetThatTakesItGetsTheDrop(TestRun* run)
{
    TestUIFixture Fixture;
    DragEvents Drags;
    if (Construct(run, &Fixture) && RecordDrags(run, &Fixture, &Drags))
    {
        Rows SourceData = { .Count = 30U };
        Rows TargetData = { .Count = 5U };
        uint64_t Source = AddList(run, &Fixture, WIDGET_ID_INVALID, &SourceData, SOURCE_X, SOURCE_Y, ROWS_KIND);
        uint64_t Target = AddList(run, &Fixture, WIDGET_ID_INVALID, &TargetData, TARGET_X, TARGET_Y, 0U);
        uint64_t Refusing = AddList(run, &Fixture, WIDGET_ID_INVALID, &TargetData, TARGET_X, TARGET_Y + 250.0f, 0U);
        MakeTarget(run, &Fixture, Target, ROWS_KIND);
        MakeTarget(run, &Fixture, Refusing, OTHER_KIND);
        TestUI_Ticks(run, &Fixture, 3U);
        PressAt(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 3U));
        MoveTo(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 6U));
        // Over the target that refuses: the not-allowed pointer; over the one that takes it: the arrow, and the line between its rows.
        MoveTo(run, &Fixture, TARGET_X + 50.0f, TARGET_Y + 260.0f);
        TEST_CHECK(run, (UIDragDrop_GetTargetState(&Fixture.Screen, Refusing) == UIDragTargetState_Refusing)
            && (UIScreen_GetCursorShape(&Fixture.Screen) == UICursorShape_NotAllowed));
        MoveTo(run, &Fixture, TARGET_X + 50.0f, TARGET_Y + 1.0f + (2.0f * ROW) - 3.0f);
        TEST_CHECK(run, (UIDragDrop_GetTargetState(&Fixture.Screen, Target) == UIDragTargetState_Accepting)
            && (UIDragDrop_GetTargetState(&Fixture.Screen, Refusing) == UIDragTargetState_None)
            && (UIScreen_GetCursorShape(&Fixture.Screen) == UICursorShape_Arrow));
        // No row of it looks hovered meanwhile: the line says where the drop goes.
        Widget* TargetList = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Target, &TargetList)
            && !((const UIListLookBase*)(const void*)TargetList)->List._hasHovered && ((const UIListLookBase*)(const void*)TargetList)->List._hasInsertion);
        // What shows it: a widget in the overlay layer, made at the first drag.
        UIDragSession Session;
        TEST_CHECK(run, UIDragDrop_GetSession(&Fixture.Screen, &Session) && (Session.TargetId == Target));
        TEST_CHECK(run, UIScreen_IsWidgetAlive(&Fixture.Screen, Fixture.Screen._drag.DisplayId));
        // Dropped: to the target and to the source, with the record; the list says where it goes.
        Release(run, &Fixture);
        const UIEvent* Dropped = FindDrag(&Drags, Target, UIDragEvent_Dropped);
        const UIEvent* Delivered = FindDrag(&Drags, Source, UIDragEvent_Delivered);
        TEST_CHECK(run, (Dropped != NULL) && (Delivered != NULL) && (FindDrag(&Drags, Source, UIDragEvent_Cancelled) == NULL));
        TEST_CHECK(run, (Dropped != NULL) && Dropped->HasDrag && (Dropped->Drag.Kind == ROWS_KIND) && (Dropped->Drag.Value == 3U)
            && (Dropped->Drag.SourceId == Source) && (Dropped->Drag.TargetId == Target));
        size_t Place = 99U;
        TEST_CHECK(run, (Dropped != NULL) && TEST_CHECK_SUCCESS(run, UIList_GetDropIndex(&Fixture.Screen, Target, Dropped->Drag.ScreenPosition, &Place))
            && (Place == 2U));
        // Escape during a drag cancels it, and the game does not get it.
        Drags.Count = 0U;
        PressAt(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 3U));
        MoveTo(run, &Fixture, TARGET_X + 50.0f, TARGET_Y + 30.0f);
        TestUI_Press(&Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIDragDrop_IsDragging(&Fixture.Screen) && (FindDrag(&Drags, Source, UIDragEvent_Cancelled) != NULL));
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_Pause));
        TestUI_Release(&Fixture, GameKey_Escape);
        Release(run, &Fixture);
        TEST_CHECK(run, FindDrag(&Drags, Target, UIDragEvent_Dropped) == NULL);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ATargetOrASourceGoingEndsItCleanly(TestRun* run)
{
    TestUIFixture Fixture;
    DragEvents Drags;
    if (Construct(run, &Fixture) && RecordDrags(run, &Fixture, &Drags))
    {
        Rows SourceData = { .Count = 30U };
        Rows TargetData = { .Count = 5U };
        uint64_t Source = AddList(run, &Fixture, WIDGET_ID_INVALID, &SourceData, SOURCE_X, SOURCE_Y, ROWS_KIND);
        uint64_t Target = AddList(run, &Fixture, WIDGET_ID_INVALID, &TargetData, TARGET_X, TARGET_Y, 0U);
        MakeTarget(run, &Fixture, Target, ROWS_KIND);
        TestUI_Ticks(run, &Fixture, 3U);
        // The target destroyed under the pointer: forgotten; the release drops nothing.
        PressAt(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 1U));
        MoveTo(run, &Fixture, TARGET_X + 50.0f, TARGET_Y + 30.0f);
        TEST_CHECK(run, UIDragDrop_GetTargetState(&Fixture.Screen, Target) == UIDragTargetState_Accepting);
        Widget* TargetWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Target, &TargetWidget) && TEST_CHECK_SUCCESS(run, Widget_Destroy(TargetWidget)));
        TestUI_Tick(run, &Fixture);
        UIDragSession Session;
        TEST_CHECK(run, UIDragDrop_GetSession(&Fixture.Screen, &Session) && (Session.TargetId == WIDGET_ID_INVALID));
        Release(run, &Fixture);
        TEST_CHECK(run, (FindDrag(&Drags, Source, UIDragEvent_Cancelled) != NULL) && (FindDrag(&Drags, Target, UIDragEvent_Dropped) == NULL));
        // The source destroyed mid-drag: the drag is cancelled at once, and the press with it.
        Drags.Count = 0U;
        PressAt(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 1U));
        MoveTo(run, &Fixture, SOURCE_X + 150.0f, RowY(SOURCE_Y, 1U));
        TEST_CHECK(run, UIDragDrop_IsDragging(&Fixture.Screen));
        Widget* SourceWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Source, &SourceWidget) && TEST_CHECK_SUCCESS(run, Widget_Destroy(SourceWidget)));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UIDragDrop_IsDragging(&Fixture.Screen) && (FindDrag(&Drags, Source, UIDragEvent_Cancelled) != NULL));
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.Screen));
        Release(run, &Fixture);
        // Code can cancel one too.
        TEST_CHECK_SUCCESS(run, UIDragDrop_Cancel(&Fixture.Screen));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ADragCrossesAScrollArea(TestRun* run)
{
    TestUIFixture Fixture;
    DragEvents Drags;
    if (Construct(run, &Fixture) && RecordDrags(run, &Fixture, &Drags))
    {
        // The source in a scroll area scrolled down; the target outside it, and a target list that scrolls near its edge.
        UIScrollAreaOptions Area = UIScrollAreaOptions_Create();
        Area.Element.HasPlacement = true;
        Area.Element.Placement = UIPlacement_FromTopLeft(SOURCE_X, SOURCE_Y, 220.0f, 150.0f);
        uint64_t AreaId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateScrollArea(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Area, &AreaId));
        UIStackOptions Column = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t ColumnId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, AreaId, &Column, &ColumnId));
        UIButtonOptions Tall = UIButtonOptions_Create(u8"above");
        Tall.Element.Hint = UIStackHint_Fixed(100.0f);
        uint64_t TallId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, ColumnId, &Tall, &TallId));
        Rows SourceData = { .Count = 30U };
        UIListOptions Inner = UIListOptions_Create();
        Inner.CountProvider = CountRows;
        Inner.RowProvider = WriteRow;
        Inner.ProviderData = UserData_FromPointer(&SourceData);
        Inner.IsDragSource = true;
        Inner.DragKind = ROWS_KIND;
        Inner.Element.Hint = UIStackHint_Fixed(202.0f);
        uint64_t Source = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateList(&Fixture.Kit, &Fixture.Screen, ColumnId, &Inner, &Source));
        Rows TargetData = { .Count = 100U };
        uint64_t Target = AddList(run, &Fixture, WIDGET_ID_INVALID, &TargetData, TARGET_X, TARGET_Y, 0U);
        MakeTarget(run, &Fixture, Target, ROWS_KIND);
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK_SUCCESS(run, UIScrollable_SetOffset(&Fixture.Screen, AreaId, (Vector2){ .x = 0.0f, .y = 100.0f }));
        TestUI_Ticks(run, &Fixture, 2U);
        // The list's top is at the area's top now: its row 1 under the pointer.
        PressAt(run, &Fixture, SOURCE_X + 50.0f, RowY(SOURCE_Y, 1U));
        MoveTo(run, &Fixture, SOURCE_X + 50.0f, SOURCE_Y + 400.0f); // Out of the area, below it.
        MoveTo(run, &Fixture, TARGET_X + 50.0f, TARGET_Y + 195.0f); // Near the target's bottom: it scrolls.
        TestUI_Ticks(run, &Fixture, 60U);
        Vector2 Offset = { .x = 0.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, UIScrollable_GetOffset(&Fixture.Screen, Target, &Offset));
        TEST_CHECK(run, Offset.y > 0.0f);
        Release(run, &Fixture);
        const UIEvent* Dropped = FindDrag(&Drags, Target, UIDragEvent_Dropped);
        TEST_CHECK(run, (Dropped != NULL) && (Dropped->Drag.Value == 1U) && (Dropped->Drag.SourceId == Source));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APlainWidgetCanBeASource(TestRun* run)
{
    TestUIFixture Fixture;
    DragEvents Drags;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && RecordDrags(run, &Fixture, &Drags) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIButtonOptions Button = UIButtonOptions_Create(u8"drag me");
        Button.Element.HasPlacement = true;
        Button.Element.Placement = UIPlacement_FromTopLeft(SOURCE_X, SOURCE_Y, 100.0f, 30.0f);
        uint64_t ButtonId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Button, &ButtonId));
        UIPanelOptions Panel = UIPanelOptions_Create();
        Panel.Element.HasPlacement = true;
        Panel.Element.Placement = UIPlacement_FromTopLeft(TARGET_X, TARGET_Y, 100.0f, 100.0f);
        uint64_t PanelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &PanelId));
        TEST_CHECK_SUCCESS(run, UIDragDrop_SetSource(&Fixture.Screen, ButtonId, ButtonPayload, NULL));
        MakeTarget(run, &Fixture, PanelId, OTHER_KIND);
        TEST_CHECK_ERROR(run, UIDragDrop_SetSource(&Fixture.Screen, ButtonId, NULL, NULL), ErrorCode_IllegalArgument);
        TestUI_Ticks(run, &Fixture, 2U);
        // Dragged off itself and back: the button lost its press to the drag, so it does not activate.
        PressAt(run, &Fixture, SOURCE_X + 20.0f, SOURCE_Y + 15.0f);
        MoveTo(run, &Fixture, TARGET_X + 50.0f, TARGET_Y + 50.0f);
        Release(run, &Fixture);
        TEST_CHECK(run, FindDrag(&Drags, PanelId, UIDragEvent_Dropped) != NULL);
        bool IsActivated = false;
        for (size_t Index = 0U; Index < Events.Count; Index++)
        {
            IsActivated = IsActivated || (Events.Events[Index].WidgetEventId == (int32_t)UIElementEvent_Activated);
        }
        TEST_CHECK(run, !IsActivated);
        // Cleared, it drags no more: a click activates it again.
        UIDragDrop_ClearSource(&Fixture.Screen, ButtonId);
        Drags.Count = 0U;
        TestUI_Click(run, &Fixture, SOURCE_X + 20.0f, SOURCE_Y + 15.0f);
        TEST_CHECK(run, Drags.Count == 0U);
        // A payload's text is cut at a codepoint to fit.
        UIDragPayload Payload;
        Memory_Zero(&Payload, sizeof(Payload));
        unsigned char Long[UI_DRAG_MAX_TEXT_BYTES + 8U];
        for (size_t Index = 0U; Index < sizeof(Long); Index += 2U)
        {
            Long[Index] = 0xC4U; // "ā", two bytes.
            Long[Index + 1U] = 0x81U;
        }
        TEST_CHECK_SUCCESS(run, UIDragPayload_SetText(&Payload, Long, sizeof(Long)));
        size_t Length = strlen((const char*)Payload.Text);
        TEST_CHECK(run, (Length < UI_DRAG_MAX_TEXT_BYTES) && ((Length % 2U) == 0U));
        const unsigned char Broken[] = { 0xC3U, 0x28U };
        TEST_CHECK_SUCCESS(run, UIDragPayload_SetText(&Payload, Broken, 2U));
        TEST_CHECK(run, Payload.Text[0] == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "a_press_drags_only_past_the_distance", APressDragsOnlyPastTheDistance },
    { "a_target_that_takes_it_gets_the_drop", ATargetThatTakesItGetsTheDrop },
    { "a_target_or_a_source_going_ends_it_cleanly", ATargetOrASourceGoingEndsItCleanly },
    { "a_drag_crosses_a_scroll_area", ADragCrossesAScrollArea },
    { "a_plain_widget_can_be_a_source", APlainWidgetCanBeASource },
};


// Public functions.
void UIDragDropTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIDragDrop", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
