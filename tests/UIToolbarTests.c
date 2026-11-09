#include "TestSuites.h"
#include "TestUI.h"
#include "UIButton.h"
#include "UIKit.h"
#include "UIToolbar.h"
#include "UITooltip.h"
#include <string.h>


// Macros.
/** The fixture's look: buttons of a "tool" variant, a bar with 26 pixel buttons, 2 between them and 9 for a separator. */
#define TOOL_THEME \
    "{\"name\": \"tools\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [0, 0], \"spacing\": 0, \"background\": {\"kind\": \"none\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}," \
    "\"variants\": {\"tool\": {\"padding\": [3, 3], \"min_size\": [20, 20]}}}," \
    "\"toolbar\": {\"padding\": [3, 3], \"spacing\": 2, \"button_size\": [26, 26], \"separator_thickness\": 1, \"separator_spacing\": 9," \
    "\"background\": {\"kind\": \"flat\"}, \"separator\": {\"kind\": \"flat\"}}}}"
#define BAR_X 40.0f
#define BAR_Y 40.0f

enum
{
    ITEM_NEW = 1,
    ITEM_GRID = 2,
    ITEM_MOVE = 3,
    ITEM_TURN = 4,
    ITEM_SIZE = 5,
    ITEM_PLAY = 6,
    ITEM_LOCKED = 7,
    ITEM_COUNT = 9
};


// Static functions: helpers.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, TOOL_THEME);
}

/* A button, a toggle that is on, a separator, three choices of one group (the first taken), a separator, a button with a text, and a
 * disabled one. */
static void MakeItems(UIToolbarItem items[ITEM_COUNT])
{
    items[0] = UIToolbarItem_CreateButton(ITEM_NEW, UIElementIcon_FromLetter('N'), u8"New");
    items[1] = UIToolbarItem_CreateToggle(ITEM_GRID, UIElementIcon_FromLetter('G'), u8"Grid", true);
    items[2] = UIToolbarItem_CreateSeparator();
    items[3] = UIToolbarItem_CreateChoice(ITEM_MOVE, UIElementIcon_FromLetter('M'), u8"Move", 1U, true);
    items[4] = UIToolbarItem_CreateChoice(ITEM_TURN, UIElementIcon_FromLetter('T'), u8"Turn", 1U, false);
    items[5] = UIToolbarItem_CreateChoice(ITEM_SIZE, UIElementIcon_FromLetter('S'), u8"Size", 1U, false);
    items[6] = UIToolbarItem_CreateSeparator();
    items[7] = UIToolbarItem_CreateButton(ITEM_PLAY, UIElementIcon_FromLetter('P'), u8"Play the story");
    items[7].Text = u8"Play";
    items[8] = UIToolbarItem_CreateButton(ITEM_LOCKED, UIElementIcon_FromLetter('L'), u8"Locked");
    items[8].IsEnabled = false;
}

/* The bar in a column, sized by its own measure. */
static uint64_t AddBar(TestRun* run, TestUIFixture* fixture, UIStackDirection direction)
{
    UIStackOptions Column = UIStackOptions_Create(UIStackDirection_Column);
    Column.Element.HasPlacement = true;
    Column.Element.Placement = UIPlacement_FromTopLeft(BAR_X, BAR_Y, 600.0f, 500.0f);
    uint64_t ColumnId = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Column, &ColumnId));
    UIToolbarItem Items[ITEM_COUNT];
    MakeItems(Items);
    UIToolbarOptions Options = UIToolbarOptions_Create();
    Options.Direction = direction;
    Options.Items = Items;
    Options.ItemCount = ITEM_COUNT;
    Options.Element.Hint = UIStackHint_Content();
    Options.Element.Hint.Align = UIStackAlign_Start;
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateToolbar(&fixture->Kit, &fixture->Screen, ColumnId, &Options, &Id));
    TestUI_Ticks(run, fixture, 2U);
    return Id;
}

static Rectangle ItemBox(TestRun* run, TestUIFixture* fixture, uint64_t barId, int32_t itemId)
{
    uint64_t ButtonId = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIToolbar_GetButtonId(&fixture->Screen, barId, itemId, &ButtonId));
    return TestUI_GetPixelBox(run, fixture, ButtonId);
}

static void ClickItem(TestRun* run, TestUIFixture* fixture, uint64_t barId, int32_t itemId)
{
    Rectangle Box = ItemBox(run, fixture, barId, itemId);
    TestUI_Click(run, fixture, Box.x + (Box.width * 0.5f), Box.y + (Box.height * 0.5f));
}

static bool IsToggled(TestRun* run, TestUIFixture* fixture, uint64_t barId, int32_t itemId)
{
    bool Result = false;
    TEST_CHECK_SUCCESS(run, UIToolbar_IsToggled(&fixture->Screen, barId, itemId, &Result));
    return Result;
}

/* The bar's events, in their order: kind and item. */
static size_t CountBarEvents(const TestUIEvents* events, uint64_t barId, UIElementEvent kind, int32_t itemId)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        const UIEvent* Event = &events->Events[Index];
        Count += ((Event->WidgetId == barId) && (Event->WidgetEventId == (int32_t)kind) && Event->HasIndex && (Event->Index == itemId)) ? 1U : 0U;
    }
    return Count;
}

static size_t CountAllBarEvents(const TestUIEvents* events, uint64_t barId)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        Count += (events->Events[Index].WidgetId == barId) ? 1U : 0U;
    }
    return Count;
}

static const UIEvent* FindBarEvent(const TestUIEvents* events, uint64_t barId, UIElementEvent kind)
{
    for (size_t Index = 0U; Index < events->Count; Index++)
    {
        if ((events->Events[Index].WidgetId == barId) && (events->Events[Index].WidgetEventId == (int32_t)kind))
        {
            return &events->Events[Index];
        }
    }
    return NULL;
}


// Tests.
static void ItsItemsAreLaidOutInARowOrAColumn(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Bar = AddBar(run, &Fixture, UIStackDirection_Row);
        // From the padding, 2 between two buttons, a separator's 9 and no spacing by it.
        Rectangle New = ItemBox(run, &Fixture, Bar, ITEM_NEW);
        Rectangle Grid = ItemBox(run, &Fixture, Bar, ITEM_GRID);
        Rectangle Move = ItemBox(run, &Fixture, Bar, ITEM_MOVE);
        Rectangle Turn = ItemBox(run, &Fixture, Bar, ITEM_TURN);
        Rectangle Size = ItemBox(run, &Fixture, Bar, ITEM_SIZE);
        Rectangle Play = ItemBox(run, &Fixture, Bar, ITEM_PLAY);
        Rectangle Locked = ItemBox(run, &Fixture, Bar, ITEM_LOCKED);
        TEST_CHECK(run, TestUI_IsBox(New, BAR_X + 3.0f, BAR_Y + 3.0f, 26.0f, 26.0f));
        TEST_CHECK(run, TestUI_IsBox(Grid, BAR_X + 31.0f, BAR_Y + 3.0f, 26.0f, 26.0f));
        TEST_CHECK(run, TestUI_IsBox(Move, BAR_X + 66.0f, BAR_Y + 3.0f, 26.0f, 26.0f));
        TEST_CHECK(run, TestUI_IsBox(Turn, BAR_X + 94.0f, BAR_Y + 3.0f, 26.0f, 26.0f));
        TEST_CHECK(run, TestUI_IsBox(Size, BAR_X + 122.0f, BAR_Y + 3.0f, 26.0f, 26.0f));
        // A text makes a button longer, never shorter.
        TEST_CHECK(run, TestUI_IsSamePixel(Play.x, BAR_X + 157.0f) && (Play.width > 40.0f) && TestUI_IsSamePixel(Play.height, 26.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(Locked.x, Play.x + Play.width + 2.0f));
        // The bar is as large as its items and its padding, and the separators sit where it says.
        Rectangle Box = TestUI_GetPixelBox(run, &Fixture, Bar);
        TEST_CHECK(run, TestUI_IsBox(Box, BAR_X, BAR_Y, Locked.x + Locked.width + 3.0f - BAR_X, 32.0f));
        Widget* BarWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Bar, &BarWidget));
        if (BarWidget != NULL)
        {
            const UIToolbarCapability* Capability = &((const UIToolbarLookBase*)(const void*)BarWidget)->Toolbar;
            TEST_CHECK(run, UIToolbarCapability_GetEntryCount(Capability) == ITEM_COUNT);
            const UIToolbarEntry* First = UIToolbarCapability_GetEntryAt(Capability, 2U);
            const UIToolbarEntry* Second = UIToolbarCapability_GetEntryAt(Capability, 6U);
            TEST_CHECK(run, (First->Kind == UIToolbarItemKind_Separator) && TestUI_IsSamePixel(First->Start, 57.0f)
                && TestUI_IsSamePixel(First->Length, 9.0f) && TestUI_IsSamePixel(Second->Start, 148.0f));
        }
        // Turned upright: top to bottom, every button as wide as the widest.
        TEST_CHECK_SUCCESS(run, UIToolbar_SetDirection(&Fixture.Screen, Bar, UIStackDirection_Column));
        TestUI_Ticks(run, &Fixture, 2U);
        New = ItemBox(run, &Fixture, Bar, ITEM_NEW);
        Grid = ItemBox(run, &Fixture, Bar, ITEM_GRID);
        Move = ItemBox(run, &Fixture, Bar, ITEM_MOVE);
        Play = ItemBox(run, &Fixture, Bar, ITEM_PLAY);
        TEST_CHECK(run, TestUI_IsSamePixel(New.x, BAR_X + 3.0f) && TestUI_IsSamePixel(New.y, BAR_Y + 3.0f) && TestUI_IsSamePixel(New.height, 26.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(Grid.y, BAR_Y + 31.0f) && TestUI_IsSamePixel(Move.y, BAR_Y + 66.0f));
        TEST_CHECK(run, TestUI_IsSamePixel(New.width, Play.width) && (New.width > 40.0f));
        Locked = ItemBox(run, &Fixture, Bar, ITEM_LOCKED);
        Box = TestUI_GetPixelBox(run, &Fixture, Bar);
        TEST_CHECK(run, TestUI_IsSamePixel(Box.width, New.width + 6.0f) && TestUI_IsSamePixel(Box.y + Box.height, Locked.y + Locked.height + 3.0f));
        TEST_CHECK_ERROR(run, UIToolbar_SetDirection(&Fixture.Screen, Bar, (UIStackDirection)7), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ItsClicksBecomeItsEvents(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Bar = AddBar(run, &Fixture, UIStackDirection_Row);
        // A button: Activated, with its id.
        Events.Count = 0U;
        ClickItem(run, &Fixture, Bar, ITEM_NEW);
        TEST_CHECK(run, (CountBarEvents(&Events, Bar, UIElementEvent_Activated, ITEM_NEW) == 1U) && (CountAllBarEvents(&Events, Bar) == 1U));
        // A toggle: flips, Changed then Committed with its new state.
        Events.Count = 0U;
        ClickItem(run, &Fixture, Bar, ITEM_GRID);
        const UIEvent* Changed = FindBarEvent(&Events, Bar, UIElementEvent_Changed);
        const UIEvent* Committed = FindBarEvent(&Events, Bar, UIElementEvent_Committed);
        TEST_CHECK(run, (Changed != NULL) && (Committed != NULL) && (Changed < Committed) && (Changed->Index == ITEM_GRID)
            && Changed->HasValue && (Changed->Value.Type == UIPropertyType_Bool) && !Changed->Value.Data.BoolValue && !Committed->Value.Data.BoolValue);
        TEST_CHECK(run, !IsToggled(run, &Fixture, Bar, ITEM_GRID));
        Events.Count = 0U;
        ClickItem(run, &Fixture, Bar, ITEM_GRID);
        Committed = FindBarEvent(&Events, Bar, UIElementEvent_Committed);
        TEST_CHECK(run, IsToggled(run, &Fixture, Bar, ITEM_GRID) && (Committed != NULL) && Committed->Value.Data.BoolValue);
        // A choice: taking one lets go of the others; the one taken does nothing.
        Events.Count = 0U;
        ClickItem(run, &Fixture, Bar, ITEM_SIZE);
        TEST_CHECK(run, IsToggled(run, &Fixture, Bar, ITEM_SIZE) && !IsToggled(run, &Fixture, Bar, ITEM_MOVE) && !IsToggled(run, &Fixture, Bar, ITEM_TURN));
        TEST_CHECK(run, (CountBarEvents(&Events, Bar, UIElementEvent_Changed, ITEM_SIZE) == 1U)
            && (CountBarEvents(&Events, Bar, UIElementEvent_Committed, ITEM_SIZE) == 1U) && (CountAllBarEvents(&Events, Bar) == 2U));
        Events.Count = 0U;
        ClickItem(run, &Fixture, Bar, ITEM_SIZE);
        TEST_CHECK(run, IsToggled(run, &Fixture, Bar, ITEM_SIZE) && (CountAllBarEvents(&Events, Bar) == 0U));
        // A disabled one takes no clicks, until it is enabled.
        Events.Count = 0U;
        ClickItem(run, &Fixture, Bar, ITEM_LOCKED);
        TEST_CHECK(run, CountAllBarEvents(&Events, Bar) == 0U);
        TEST_CHECK_SUCCESS(run, UIToolbar_SetIsEnabled(&Fixture.Screen, Bar, ITEM_LOCKED, true));
        ClickItem(run, &Fixture, Bar, ITEM_LOCKED);
        TEST_CHECK(run, CountBarEvents(&Events, Bar, UIElementEvent_Activated, ITEM_LOCKED) == 1U);
        // The keyboard: the focus on a button, Space clicks it.
        uint64_t TurnButton = WIDGET_ID_INVALID;
        Widget* Turn = NULL;
        TEST_CHECK_SUCCESS(run, UIToolbar_GetButtonId(&Fixture.Screen, Bar, ITEM_TURN, &TurnButton));
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, TurnButton, &Turn) && TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Turn)));
        Events.Count = 0U;
        TestUI_Tap(run, &Fixture, GameKey_Space);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsToggled(run, &Fixture, Bar, ITEM_TURN) && !IsToggled(run, &Fixture, Bar, ITEM_SIZE)
            && (CountBarEvents(&Events, Bar, UIElementEvent_Committed, ITEM_TURN) == 1U));
        // Its button's state shows it: selected.
        Widget* TurnWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, TurnButton, &TurnWidget));
        if (TurnWidget != NULL)
        {
            UIElementState State = UIButtonCapability_GetState(&((const UIButtonLookBase*)(const void*)TurnWidget)->Button, TurnWidget);
            TEST_CHECK(run, State == UIElementState_Selected);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void CallingCodeSetsItsStatesQuietly(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Bar = AddBar(run, &Fixture, UIStackDirection_Row);
        Events.Count = 0U;
        TEST_CHECK_SUCCESS(run, UIToolbar_SetIsToggled(&Fixture.Screen, Bar, ITEM_TURN, true));
        TEST_CHECK_SUCCESS(run, UIToolbar_SetIsToggled(&Fixture.Screen, Bar, ITEM_GRID, false));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsToggled(run, &Fixture, Bar, ITEM_TURN) && !IsToggled(run, &Fixture, Bar, ITEM_MOVE) && !IsToggled(run, &Fixture, Bar, ITEM_GRID));
        TEST_CHECK(run, CountAllBarEvents(&Events, Bar) == 0U);
        // Letting go of a choice leaves its group with none.
        TEST_CHECK_SUCCESS(run, UIToolbar_SetIsToggled(&Fixture.Screen, Bar, ITEM_TURN, false));
        TEST_CHECK(run, !IsToggled(run, &Fixture, Bar, ITEM_TURN) && !IsToggled(run, &Fixture, Bar, ITEM_MOVE) && !IsToggled(run, &Fixture, Bar, ITEM_SIZE));
        // Items it does not have, or of the wrong kind; a stale bar.
        bool State = false;
        uint64_t ButtonId = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIToolbar_SetIsToggled(&Fixture.Screen, Bar, ITEM_NEW, true), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIToolbar_IsToggled(&Fixture.Screen, Bar, 99, &State), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIToolbar_SetIsEnabled(&Fixture.Screen, Bar, 0, false), ErrorCode_ArgumentOutOfRange); // A separator's.
        TEST_CHECK_ERROR(run, UIToolbar_GetButtonId(&Fixture.Screen, Bar, 99, &ButtonId), ErrorCode_ArgumentOutOfRange);
        uint64_t Panel = WIDGET_ID_INVALID;
        UIPanelOptions PanelOptions = UIPanelOptions_Create();
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &PanelOptions, &Panel));
        TEST_CHECK_ERROR(run, UIToolbar_SetIsToggled(&Fixture.Screen, Panel, ITEM_GRID, true), ErrorCode_InvalidOperation);
        Widget* BarWidget = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Bar, &BarWidget) && TEST_CHECK_SUCCESS(run, Widget_Destroy(BarWidget)));
        TEST_CHECK_ERROR(run, UIToolbar_SetIsToggled(&Fixture.Screen, Bar, ITEM_GRID, true), ErrorCode_InvalidOperation);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ItsNamesAreItsButtonsHoverText(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Bar = AddBar(run, &Fixture, UIStackDirection_Row);
        Rectangle Play = ItemBox(run, &Fixture, Bar, ITEM_PLAY);
        TestUI_MovePointer(&Fixture, Play.x + 5.0f, Play.y + 5.0f);
        TestUI_Tick(run, &Fixture);
        uint64_t PlayButton = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIToolbar_GetButtonId(&Fixture.Screen, Bar, ITEM_PLAY, &PlayButton));
        UITooltipState State;
        TEST_CHECK(run, UITooltip_GetCurrent(&Fixture.Screen, &State) && (State.WidgetId == PlayButton)
            && (strcmp((const char*)State.Text, "Play the story") == 0));
        // A disabled one has its name too.
        Rectangle Locked = ItemBox(run, &Fixture, Bar, ITEM_LOCKED);
        TestUI_MovePointer(&Fixture, Locked.x + 5.0f, Locked.y + 5.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UITooltip_GetCurrent(&Fixture.Screen, &State) && (strcmp((const char*)State.Text, "Locked") == 0));
        // Between two buttons, over a separator: the bar's, which has none.
        Rectangle Grid = ItemBox(run, &Fixture, Bar, ITEM_GRID);
        TestUI_MovePointer(&Fixture, Grid.x + Grid.width + 4.0f, Grid.y + 5.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UITooltip_GetCurrent(&Fixture.Screen, &State));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EachGroupIsAChoiceOfItsOwn(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // Two groups of two: the first of each taken.
        UIToolbarItem Items[] =
        {
            UIToolbarItem_CreateChoice(1, UIElementIcon_FromLetter('A'), u8"A1", 1U, true),
            UIToolbarItem_CreateChoice(2, UIElementIcon_FromLetter('B'), u8"A2", 1U, false),
            UIToolbarItem_CreateSeparator(),
            UIToolbarItem_CreateChoice(3, UIElementIcon_FromLetter('C'), u8"B1", 2U, true),
            UIToolbarItem_CreateChoice(4, UIElementIcon_FromLetter('D'), u8"B2", 2U, false)
        };
        UIToolbarOptions Options = UIToolbarOptions_Create();
        Options.Items = Items;
        Options.ItemCount = sizeof(Items) / sizeof(Items[0]);
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(BAR_X, BAR_Y, 200.0f, 32.0f);
        uint64_t Bar = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Bar));
        TestUI_Ticks(run, &Fixture, 2U);
        // Taking A2 lets go of A1 and leaves the other group as it was.
        ClickItem(run, &Fixture, Bar, 2);
        TEST_CHECK(run, IsToggled(run, &Fixture, Bar, 2) && !IsToggled(run, &Fixture, Bar, 1) && IsToggled(run, &Fixture, Bar, 3)
            && !IsToggled(run, &Fixture, Bar, 4));
        // Code letting go of a choice not taken changes nothing else in its group.
        TEST_CHECK_SUCCESS(run, UIToolbar_SetIsToggled(&Fixture.Screen, Bar, 4, false));
        TEST_CHECK(run, IsToggled(run, &Fixture, Bar, 3) && IsToggled(run, &Fixture, Bar, 2));
        // Code taking one lets go of its group's other.
        TEST_CHECK_SUCCESS(run, UIToolbar_SetIsToggled(&Fixture.Screen, Bar, 4, true));
        TEST_CHECK(run, IsToggled(run, &Fixture, Bar, 4) && !IsToggled(run, &Fixture, Bar, 3) && IsToggled(run, &Fixture, Bar, 2));
    }
    TestUI_Deconstruct(run, &Fixture);
}


static void ABarShorterThanItsItemsCutsThem(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIToolbarItem Items[ITEM_COUNT];
        MakeItems(Items);
        UIToolbarOptions Options = UIToolbarOptions_Create();
        Options.Items = Items;
        Options.ItemCount = ITEM_COUNT;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(BAR_X, BAR_Y, 60.0f, 32.0f); // Two buttons' room.
        uint64_t Bar = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Bar));
        TestUI_Ticks(run, &Fixture, 2U);
        // Turn lies past the bar's end: a click where it would show reaches nothing.
        Rectangle Turn = ItemBox(run, &Fixture, Bar, ITEM_TURN);
        TEST_CHECK(run, Turn.x > (BAR_X + 60.0f));
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, Turn.x + (Turn.width * 0.5f), Turn.y + (Turn.height * 0.5f));
        TEST_CHECK(run, (CountAllBarEvents(&Events, Bar) == 0U) && !IsToggled(run, &Fixture, Bar, ITEM_TURN));
        // Inside, the first still works.
        ClickItem(run, &Fixture, Bar, ITEM_NEW);
        TEST_CHECK(run, CountBarEvents(&Events, Bar, UIElementEvent_Activated, ITEM_NEW) == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}


static void BadItemsAreRefused(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIToolbarItem Items[ITEM_COUNT];
        UIToolbarOptions Options = UIToolbarOptions_Create();
        Options.Items = Items;
        Options.ItemCount = ITEM_COUNT;
        uint64_t Id = WIDGET_ID_INVALID;
        // Two items of one id; two choices of one group taken; no icon; an unknown kind; text that is no UTF-8; no items with a count.
        MakeItems(Items);
        Items[4].Id = ITEM_MOVE;
        TEST_CHECK_ERROR(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);
        MakeItems(Items);
        Items[5].IsToggled = true;
        TEST_CHECK_ERROR(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        MakeItems(Items);
        Items[5].Group = 2U;
        Items[5].IsToggled = true; // Another group: fine.
        TEST_CHECK_SUCCESS(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        MakeItems(Items);
        Items[0].Icon = UIElementIcon_None(); // Nothing to show.
        TEST_CHECK_ERROR(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Items[0].Text = u8"New"; // A text alone.
        TEST_CHECK_SUCCESS(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        MakeItems(Items);
        Items[0].Icon = UIElementIcon_FromLetter(0U); // Not a valid letter.
        TEST_CHECK_ERROR(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        MakeItems(Items);
        Items[1].Kind = UIToolbarItemKind_Count;
        TEST_CHECK_ERROR(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        MakeItems(Items);
        Items[1].Name = (const unsigned char*)"\xC3\x28";
        TEST_CHECK_ERROR(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        MakeItems(Items);
        Items[2].Id = ITEM_NEW; // A separator's id is not read.
        TEST_CHECK_SUCCESS(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        Options.Items = NULL;
        TEST_CHECK_ERROR(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Options.ItemCount = 0U;
        TEST_CHECK_SUCCESS(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id)); // An empty bar.
        Options.Direction = (UIStackDirection)5;
        TEST_CHECK_ERROR(run, UIKit_CreateToolbar(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        TestUI_Ticks(run, &Fixture, 2U);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Fields.
static const TestCase Cases[] =
{
    { "its_items_are_laid_out_in_a_row_or_a_column", ItsItemsAreLaidOutInARowOrAColumn },
    { "its_clicks_become_its_events", ItsClicksBecomeItsEvents },
    { "calling_code_sets_its_states_quietly", CallingCodeSetsItsStatesQuietly },
    { "each_group_is_a_choice_of_its_own", EachGroupIsAChoiceOfItsOwn },
    { "a_bar_shorter_than_its_items_cuts_them", ABarShorterThanItsItemsCutsThem },
    { "its_names_are_its_buttons_hover_text", ItsNamesAreItsButtonsHoverText },
    { "bad_items_are_refused", BadItemsAreRefused },
};


// Public functions.
void UIToolbarTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "UIToolbar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
