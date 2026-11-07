#include "TestSuites.h"
#include "TestUI.h"
#include "UIKit.h"
#include "UITooltip.h"
#include "wr/WRString.h"
#include <stdio.h>
#include <string.h>


// Macros.
/** A theme with a hover text delay of a tenth of a second and a tooltip block: padding 4 by 2, 10 down and right of the pointer,
 *  wrapping at 100. */
#define TOOLTIP_THEME \
    "{\"name\": \"hovering\", \"font\": \"mono\", \"text_size\": 10, \"tooltip_delay\": 0.1, \"elements\": {" \
    "\"tooltip\": {\"padding\": [4, 2], \"offset\": [10, 10], \"max_width\": 100, \"background\": {\"kind\": \"flat\"}}}}"
/** The same, with the box's top-left 20 left of and 30 above the pointer. */
#define UPWARD_THEME \
    "{\"name\": \"upward\", \"font\": \"mono\", \"text_size\": 10, \"tooltip_delay\": 0.1, \"elements\": {" \
    "\"tooltip\": {\"padding\": [4, 2], \"offset\": [-20, -30], \"max_width\": 100, \"background\": {\"kind\": \"flat\"}}}}"
/** Ticks of the fixture (1/480 s) in the theme's delay and a little past it. */
#define DELAY_TICKS ((size_t)48)
#define PAST_DELAY_TICKS ((size_t)52)


// Types.
/* A provider's count of how often it was asked. */
typedef struct ProviderStateStruct
{
    int Calls;
} ProviderState;


// Static functions.
static Error CountingProvider(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    ProviderState* State = UserData_GetPointer(userData);
    State->Calls++;
    char Text[32];
    int Written = snprintf(Text, sizeof(Text), "asked %d", State->Calls);
    if ((Written > 0) && !GenericBuffer_AppendRangeBytes(outText, (unsigned char*)Text, (size_t)Written))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "the provider's text did not fit");
    }
    return Error_CreateSuccess();
}

static Error BrokenProvider(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    (void)userData;
    unsigned char Bad[2] = { 0xFFU, 0xFEU };
    GenericBuffer_AppendRangeBytes(outText, Bad, 2U);
    return Error_CreateSuccess();
}

static Error EmptyProvider(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    (void)userData;
    (void)outText;
    return Error_CreateSuccess();
}

/* Writes some text, then fails. */
static Error FailingProvider(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    (void)userData;
    GenericBuffer_AppendRangeBytes(outText, (unsigned char*)"partial", 7U);
    return Error_Construct2(ErrorCode_InvalidOperation, "the provider failed");
}

/* A NUL first: no text at all. */
static Error LeadingNulProvider(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    (void)userData;
    unsigned char Bytes[5] = { 0U, (unsigned char)'a', (unsigned char)'b', (unsigned char)'c', (unsigned char)'d' };
    GenericBuffer_AppendRangeBytes(outText, Bytes, 5U);
    return Error_CreateSuccess();
}

/* A NUL in the middle: the text ends there. */
static Error InnerNulProvider(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    (void)userData;
    unsigned char Bytes[5] = { (unsigned char)'a', (unsigned char)'b', 0U, (unsigned char)'c', (unsigned char)'d' };
    GenericBuffer_AppendRangeBytes(outText, Bytes, 5U);
    return Error_CreateSuccess();
}

/* One byte short of the limit in ASCII, then a two-byte character across it. */
static Error LongProvider(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText)
{
    (void)screen;
    (void)widgetId;
    (void)userData;
    for (size_t Index = 0U; Index < (UI_TOOLTIP_MAX_TEXT_BYTES - 1U); Index++)
    {
        GenericBuffer_AppendByte(outText, (unsigned char)'a');
    }
    unsigned char Wide[2] = { 0xC3U, 0xA9U };
    GenericBuffer_AppendRangeBytes(outText, Wide, 2U);
    return Error_CreateSuccess();
}

static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, TOOLTIP_THEME);
}

static uint64_t AddButton(TestRun* run, TestUIFixture* fixture, uint64_t parent, float x, float y)
{
    UIButtonOptions Options = UIButtonOptions_Create(u8"b");
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(x, y, 100.0f, 40.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&fixture->Kit, &fixture->Screen, parent, &Options, &Id));
    return Id;
}

static bool IsShowing(TestUIFixture* fixture, const char* text)
{
    UITooltipState State;
    return UITooltip_GetCurrent(&fixture->Screen, &State) && State.IsShown && (strcmp((const char*)State.Text, text) == 0);
}

/* The widget showing the hover text: the screen's root in the overlay layer. */
static Widget* FindDisplay(TestUIFixture* fixture)
{
    for (size_t Index = 0U; Index < UIScreen_GetRootCount(&fixture->Screen); Index++)
    {
        Widget* Root = UIScreen_GetRootAt(&fixture->Screen, Index);
        if (UIScreen_GetRootLayer(&fixture->Screen, Root) == UIScreenLayer_Overlay)
        {
            return Root;
        }
    }
    return NULL;
}


// Tests.
/* The text shows once the pointer has rested for the delay, beside it, in the overlay layer, and the widget under it keeps the hover. */
static void HoverTextShowsAfterTheDelay(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Button, u8"saves the world, then the next one"));
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, DELAY_TICKS - 2U);
        UITooltipState State;
        TEST_CHECK(run, UITooltip_GetCurrent(&Fixture.Screen, &State) && !State.IsShown && (State.WidgetId == Button));
        TEST_CHECK(run, strcmp((const char*)State.Text, "saves the world, then the next one") == 0); // A status bar has it at once.
        TEST_CHECK(run, FindDisplay(&Fixture) == NULL); // Nothing made until a text shows.
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS - DELAY_TICKS + 2U);
        TEST_CHECK(run, IsShowing(&Fixture, "saves the world, then the next one"));
        TestUI_Ticks(run, &Fixture, 2U);
        Widget* Display = FindDisplay(&Fixture);
        if (TEST_CHECK(run, Display != NULL))
        {
            Rectangle Box = UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display);
            // 10 down and right of the pointer; 34 glyphs of 5 at size 10 wrap at 100: no line wider, two lines at least, and the padding.
            TEST_CHECK(run, TestUI_IsSamePixel(Box.x, 160.0f) && TestUI_IsSamePixel(Box.y, 130.0f));
            TEST_CHECK(run, (Box.width > 8.0f) && (Box.width <= 108.0f) && (Box.height >= 24.0f));
            TEST_CHECK(run, !Widget_IsInputEnabled(Display));
        }
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Button);
        // Only one is made, however often it shows.
        size_t Roots = UIScreen_GetRootCount(&Fixture.Screen);
        TestUI_MovePointer(&Fixture, 600.0f, 400.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "saves the world, then the next one") && (UIScreen_GetRootCount(&Fixture.Screen) == Roots));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A press hides it until the pointer rests on something else; moving off hides it and starts again. */
static void APressOrMovingOffHidesIt(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t First = AddButton(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f);
        uint64_t Second = AddButton(run, &Fixture, WIDGET_ID_INVALID, 300.0f, 100.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, First, u8"first"));
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Second, u8"second"));
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "first"));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsShowing(&Fixture, "first"));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS * 2U);
        TEST_CHECK(run, !IsShowing(&Fixture, "first")); // Resting on, it stays hidden.

        TestUI_MovePointer(&Fixture, 350.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, DELAY_TICKS - 2U);
        UITooltipState State;
        TEST_CHECK(run, UITooltip_GetCurrent(&Fixture.Screen, &State) && !State.IsShown && (State.WidgetId == Second));
        TestUI_Ticks(run, &Fixture, 8U);
        TEST_CHECK(run, IsShowing(&Fixture, "second"));
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f); // Back on the first: it starts again, and the press is forgotten.
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !IsShowing(&Fixture, "first") && !IsShowing(&Fixture, "second"));
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "first"));
        TestUI_RemovePointer(&Fixture);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UITooltip_GetCurrent(&Fixture.Screen, &State));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A widget with none falls back to its ancestors'; a destroyed widget takes its text with it; cleared, the ancestor's shows again. */
static void ItFallsBackUpTheTreeAndGoesWithItsWidget(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIPanelOptions PanelOptions = UIPanelOptions_Create();
        PanelOptions.Element.HasPlacement = true;
        PanelOptions.Element.Placement = UIPlacement_FromTopLeft(50.0f, 50.0f, 400.0f, 300.0f);
        uint64_t Panel = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &PanelOptions, &Panel));
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Panel, u8"the compound's"));
        uint64_t Part = AddButton(run, &Fixture, Panel, 0.0f, 0.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        Rectangle PartBox = TestUI_GetPixelBox(run, &Fixture, Part);
        TestUI_MovePointer(&Fixture, PartBox.x + 5.0f, PartBox.y + 5.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        UITooltipState State;
        TEST_CHECK(run, IsShowing(&Fixture, "the compound's") && UITooltip_GetCurrent(&Fixture.Screen, &State) && (State.WidgetId == Panel));
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Part, u8"its own"));
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "its own"));
        TEST_CHECK_SUCCESS(run, UITooltip_Clear(&Fixture.Screen, Part));
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "the compound's"));
        TEST_CHECK_SUCCESS(run, UITooltip_Clear(&Fixture.Screen, Part)); // Clearing what has none does nothing.

        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Part, u8"short lived"));
        Widget* PartWidget = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Part, &PartWidget)))
        {
            TEST_CHECK_SUCCESS(run, Widget_Destroy(PartWidget));
        }
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "the compound's")); // The panel is what is hovered now.
        TEST_CHECK(run, Fixture.Screen._tooltips._count == 1U); // The part's entry went with it.
        TEST_CHECK_ERROR(run, UITooltip_Set(&Fixture.Screen, Part, u8"x"), ErrorCode_InvalidOperation);

        // What shows goes at once with its widget, or with its text cleared: nothing is current until the next update.
        uint64_t Other = AddButton(run, &Fixture, WIDGET_ID_INVALID, 600.0f, 50.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Other, u8"goes with it"));
        TestUI_MovePointer(&Fixture, 650.0f, 70.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "goes with it"));
        TEST_CHECK_SUCCESS(run, UITooltip_Clear(&Fixture.Screen, Other));
        TEST_CHECK(run, !UITooltip_GetCurrent(&Fixture.Screen, &State));
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Other, u8"goes with it"));
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        Widget* OtherWidget = NULL;
        if (TEST_CHECK(run, IsShowing(&Fixture, "goes with it") && UIScreen_TryGetWidgetById(&Fixture.Screen, Other, &OtherWidget)))
        {
            TEST_CHECK_SUCCESS(run, Widget_Destroy(OtherWidget));
            TEST_CHECK(run, !UITooltip_GetCurrent(&Fixture.Screen, &State));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A provider is asked while the pointer rests, for text that changes; what it gives that is not UTF-8 shows nothing. */
static void AProviderGivesTextThatChanges(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f);
        ProviderState Provider = { .Calls = 0 };
        UserData Data = UserData_FromPointer(&Provider);
        TEST_CHECK_SUCCESS(run, UITooltip_SetProvider(&Fixture.Screen, Button, CountingProvider, &Data));
        TestUI_Ticks(run, &Fixture, 3U);
        TEST_CHECK(run, Provider.Calls == 0); // Not asked while nobody rests on it.
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        UITooltipState State;
        TEST_CHECK(run, UITooltip_GetCurrent(&Fixture.Screen, &State) && State.IsShown);
        TEST_CHECK(run, (Provider.Calls == (int)PAST_DELAY_TICKS) && (strcmp((const char*)State.Text, "asked 52") == 0));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsShowing(&Fixture, "asked 53"));
        TEST_CHECK_SUCCESS(run, UITooltip_SetProvider(&Fixture.Screen, Button, BrokenProvider, NULL));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !UITooltip_GetCurrent(&Fixture.Screen, &State));
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Button, u8"by value again"));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsShowing(&Fixture, "by value again"));
        TEST_CHECK_ERROR(run, UITooltip_SetProvider(&Fixture.Screen, Button, NULL, NULL), ErrorCode_IllegalArgument);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The delay is the widget's own, else the theme's; texts and delays are checked. */
static void TheDelayIsItsOwnOrTheThemes(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Button, u8"slow"));
        TEST_CHECK_SUCCESS(run, UITooltip_SetDelay(&Fixture.Screen, Button, true, 0.5f));
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, 200U);
        TEST_CHECK(run, !IsShowing(&Fixture, "slow"));
        TestUI_Ticks(run, &Fixture, 50U);
        TEST_CHECK(run, IsShowing(&Fixture, "slow"));
        TEST_CHECK_SUCCESS(run, UITooltip_SetDelay(&Fixture.Screen, Button, false, 0.0f));
        TestUI_MovePointer(&Fixture, 600.0f, 400.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "slow"));

        TEST_CHECK_ERROR(run, UITooltip_SetDelay(&Fixture.Screen, Button, true, -1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UITooltip_SetDelay(&Fixture.Screen, Button, true, UI_TOOLTIP_MAX_DELAY_SECONDS + 1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UITooltip_SetDelay(&Fixture.Screen, 12345U, true, 0.1f), ErrorCode_InvalidOperation);
        unsigned char Invalid[3] = { 0xC3U, 0x28U, 0U };
        TEST_CHECK_ERROR(run, UITooltip_Set(&Fixture.Screen, Button, Invalid), ErrorCode_InvalidTextEncoding);
        TEST_CHECK_ERROR(run, UITooltip_Set(&Fixture.Screen, Button, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UITooltip_Set(NULL, Button, u8"x"), ErrorCode_IllegalArgument);
        GenericBuffer Long;
        GenericBuffer_AllocateVariable(&Long, UI_TOOLTIP_MAX_TEXT_BYTES + 2U, sizeof(unsigned char));
        for (size_t Index = 0U; Index <= UI_TOOLTIP_MAX_TEXT_BYTES; Index++)
        {
            GenericBuffer_AppendByte(&Long, (unsigned char)'a');
        }
        GenericBuffer_NullTerminate(&Long);
        TEST_CHECK_ERROR(run, UITooltip_Set(&Fixture.Screen, Button, Long._data), ErrorCode_ArgumentOutOfRange);
        Memory_Free(Long._data);
        TEST_CHECK(run, IsShowing(&Fixture, "slow")); // Refusals changed nothing.
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Near the surface's edges it keeps on the surface: left of its right edge, above the pointer at its bottom. */
static void ItKeepsOnTheSurface(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, 895.0f, 455.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Button, u8"near the corner"));
        TestUI_MovePointer(&Fixture, 990.0f, 490.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS + 3U);
        Widget* Display = FindDisplay(&Fixture);
        if (TEST_CHECK(run, (Display != NULL) && IsShowing(&Fixture, "near the corner")))
        {
            Rectangle Box = UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display);
            TEST_CHECK(run, (Box.x >= 0.0f) && ((Box.x + Box.width) <= (TEST_UI_SURFACE_WIDTH + 0.01f)));
            TEST_CHECK(run, (Box.y >= 0.0f) && ((Box.y + Box.height) <= 480.01f)); // Above the pointer, by the offset.
            TEST_CHECK(run, Box.width > 0.0f);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A screen whose factory names no type for it: the text is still worked out, and nothing is made. */
static void WithoutATypeNothingShowsAndTheRestWorks(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIWidgetFactory_SetTooltipType(&Fixture.Widgets, WIDGET_TYPE_ID_INVALID);
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Button, u8"nowhere to show"));
        size_t Roots = UIScreen_GetRootCount(&Fixture.Screen);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        TEST_CHECK(run, IsShowing(&Fixture, "nowhere to show") && (UIScreen_GetRootCount(&Fixture.Screen) == Roots));
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* A provider that gives nothing shows nothing and makes nothing; one that gives too much shows what fits, cut before the character
 * that does not fit whole; one that fails fails the update and leaves nothing of what it wrote. */
static void AProviderGivingNothingTooMuchOrFailing(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_SetProvider(&Fixture.Screen, Button, EmptyProvider, NULL));
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
        UITooltipState State;
        TEST_CHECK(run, !UITooltip_GetCurrent(&Fixture.Screen, &State));
        TEST_CHECK(run, FindDisplay(&Fixture) == NULL);
        // A text ends at the first NUL: one that starts with it is empty, and shows and makes nothing.
        TEST_CHECK_SUCCESS(run, UITooltip_SetProvider(&Fixture.Screen, Button, LeadingNulProvider, NULL));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, !UITooltip_GetCurrent(&Fixture.Screen, &State) && (FindDisplay(&Fixture) == NULL));
        TEST_CHECK_SUCCESS(run, UITooltip_SetProvider(&Fixture.Screen, Button, InnerNulProvider, NULL));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsShowing(&Fixture, "ab"));

        TEST_CHECK_SUCCESS(run, UITooltip_SetProvider(&Fixture.Screen, Button, LongProvider, NULL));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UITooltip_GetCurrent(&Fixture.Screen, &State) && State.IsShown);
        TEST_CHECK(run, strlen((const char*)State.Text) == (UI_TOOLTIP_MAX_TEXT_BYTES - 1U));

        TEST_CHECK_SUCCESS(run, UITooltip_SetProvider(&Fixture.Screen, Button, FailingProvider, NULL));
        GameInput_BeginTick(&Fixture.Input, &Fixture.Sample);
        Fixture.Time += TEST_UI_TICK_SECONDS;
        TEST_CHECK_ERROR(run, UIScreen_Update(&Fixture.Screen, ProgramTime_Create(Fixture.Time, TEST_UI_TICK_SECONDS), &Fixture.ScreenInput),
            ErrorCode_InvalidOperation);
        TEST_CHECK(run, !UITooltip_GetCurrent(&Fixture.Screen, &State));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The box follows what it shows: empty while the next text waits for its delay, and grown for a longer text. */
static void TheBoxFollowsItsText(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t First = AddButton(run, &Fixture, WIDGET_ID_INVALID, 100.0f, 100.0f);
        uint64_t Second = AddButton(run, &Fixture, WIDGET_ID_INVALID, 300.0f, 100.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, First, u8"a"));
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Second, u8"a longer one"));
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS + 2U);
        Widget* Display = FindDisplay(&Fixture);
        if (TEST_CHECK(run, (Display != NULL) && IsShowing(&Fixture, "a")))
        {
            // One glyph of 5 and the padding of 4 on each side.
            TEST_CHECK(run, TestUI_IsSamePixel(UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display).width, 13.0f));
            TestUI_MovePointer(&Fixture, 350.0f, 120.0f);
            TestUI_Ticks(run, &Fixture, 4U);
            TEST_CHECK(run, UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display).width == 0.0f);
            TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS);
            TEST_CHECK(run, IsShowing(&Fixture, "a longer one"));
            TEST_CHECK(run, TestUI_IsSamePixel(UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display).width, 68.0f)); // 12 glyphs.
            // Idle, shown or hidden, it moves nothing: the screen has nothing to hit-test again.
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, !Fixture.Screen._isHitTestDirty);
            TestUI_MovePointer(&Fixture, 700.0f, 400.0f);
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK(run, !Fixture.Screen._isHitTestDirty && (UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display).width == 0.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Its offset and its wrapping width count in the screen's scale: at 2, 10 UI pixels from the pointer are 20 and the text wraps at
 * 200 surface pixels. On a surface narrower than the box it starts at the left edge. */
static void ItCountsInTheScreensScale(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, 50.0f, 50.0f);
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Button, u8"saves the world, then the next one"));
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS + 2U);
        Widget* Display = FindDisplay(&Fixture);
        if (TEST_CHECK(run, (Display != NULL) && IsShowing(&Fixture, "saves the world, then the next one")))
        {
            Rectangle Box = UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display);
            TEST_CHECK(run, TestUI_IsSamePixel(Box.x, 170.0f) && TestUI_IsSamePixel(Box.y, 140.0f));
            // Lines of up to 20 glyphs of 10: wider than a wrap at 100 surface pixels would allow, no wider than 200, two lines.
            TEST_CHECK(run, (Box.width > 116.0f) && (Box.width <= 216.0f) && (Box.height >= 48.0f));

            // The surface's change moves everything under the pointer for a tick: the text waits for its delay again.
            TestUI_SetSurface(run, &Fixture, 150.0f, 500.0f);
            TestUI_MovePointer(&Fixture, 120.0f, 120.0f);
            TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS + 3U);
            Box = UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display);
            TEST_CHECK(run, IsShowing(&Fixture, "saves the world, then the next one") && (Box.width > 150.0f));
            TEST_CHECK(run, TestUI_IsSamePixel(Box.x, 0.0f));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* A negative offset puts it left of and above the pointer; past the surface's top it goes below the pointer, the offset mirrored, and
 * past its left edge it slides in. */
static void ANegativeOffsetPutsItLeftAndAbove(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, UPWARD_THEME))
    {
        uint64_t Ids[4] =
        {
            AddButton(run, &Fixture, WIDGET_ID_INVALID, 450.0f, 230.0f),
            AddButton(run, &Fixture, WIDGET_ID_INVALID, 450.0f, 0.0f),
            AddButton(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 230.0f),
            AddButton(run, &Fixture, WIDGET_ID_INVALID, 450.0f, 460.0f)
        };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Ids[Index], u8"up and left"));
        }
        TEST_CHECK_SUCCESS(run, UITooltip_Set(&Fixture.Screen, Ids[3], u8"a text that wraps over more lines than the offset has room for above"));
        // 11 glyphs of 5 and the padding: 63 x 14, its top-left at the pointer less 20 and 30.
        static const float Pointers[3][2] = { { 500.0f, 250.0f }, { 500.0f, 10.0f }, { 5.0f, 250.0f } };
        static const float Expected[3][2] = { { 480.0f, 220.0f }, { 480.0f, 26.0f }, { 0.0f, 220.0f } };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            TestUI_MovePointer(&Fixture, Pointers[Index][0], Pointers[Index][1]);
            TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS + 2U);
            Widget* Display = FindDisplay(&Fixture);
            if (TEST_CHECK(run, (Display != NULL) && IsShowing(&Fixture, "up and left")))
            {
                TEST_CHECK(run, TestUI_IsBox(UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display), Expected[Index][0], Expected[Index][1],
                    63.0f, 14.0f));
            }
        }
        // Taller than the offset has room for, near the bottom: mirrored it would still leave the surface, so it is kept on it.
        TestUI_MovePointer(&Fixture, 500.0f, 490.0f);
        TestUI_Ticks(run, &Fixture, PAST_DELAY_TICKS + 2U);
        Widget* Display = FindDisplay(&Fixture);
        if (TEST_CHECK(run, Display != NULL))
        {
            Rectangle Box = UIScreen_GetWidgetPixelBox(&Fixture.Screen, Display);
            TEST_CHECK(run, (Box.height > 40.0f) && (Box.y >= 0.0f) && ((Box.y + Box.height) <= (TEST_UI_SURFACE_HEIGHT + 0.01f)));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UITooltipTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "hover_text_shows_after_the_delay", HoverTextShowsAfterTheDelay },
        { "a_press_or_moving_off_hides_it", APressOrMovingOffHidesIt },
        { "it_falls_back_up_the_tree_and_goes_with_its_widget", ItFallsBackUpTheTreeAndGoesWithItsWidget },
        { "a_provider_gives_text_that_changes", AProviderGivesTextThatChanges },
        { "the_delay_is_its_own_or_the_themes", TheDelayIsItsOwnOrTheThemes },
        { "it_keeps_on_the_surface", ItKeepsOnTheSurface },
        { "without_a_type_nothing_shows_and_the_rest_works", WithoutATypeNothingShowsAndTheRestWorks },
        { "a_provider_giving_nothing_too_much_or_failing", AProviderGivingNothingTooMuchOrFailing },
        { "the_box_follows_its_text", TheBoxFollowsItsText },
        { "it_counts_in_the_screens_scale", ItCountsInTheScreensScale },
        { "a_negative_offset_puts_it_left_and_above", ANegativeOffsetPutsItLeftAndAbove },
    };
    TestRun_RunSuite(run, "UITooltip", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
