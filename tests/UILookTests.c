#include "TestSuites.h"
#include "TestUI.h"
#include "Logger.h"
#include "UIKit.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <string.h>


// Macros.
/** Text 14 pixels tall, glyphs advancing 7; a button's padding 8 by 4, its minimum 24 by 24. */
#define LOOK_THEME \
    "{\"name\": \"looks\", \"font\": \"mono\", \"text_size\": 14, \"elements\": {" \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}}}}"
/** The same, naming a look for the button. */
#define NAMING_THEME \
    "{\"name\": \"naming\", \"font\": \"mono\", \"text_size\": 14, \"elements\": {" \
    "\"button\": {\"look\": \"tbp:plain\", \"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}}}}"
/** A theme naming a look nobody registered. */
#define UNKNOWN_LOOK_THEME "{\"name\": \"unknown\", \"elements\": {\"button\": {\"look\": \"tbp:nobody\"}, \"toggle\": {\"look\": \"nobody\"}}}"
/** LOOK_THEME with 24 between a button's icon and its text instead of the standard look's 4. */
#define SPACED_THEME \
    "{\"name\": \"spaced\", \"font\": \"mono\", \"text_size\": 14, \"elements\": {" \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#ffffff\"}," \
    "\"custom\": {\"icon_spacing\": 24}}}}"
/** Room for a log's path and text. */
#define LOG_BUFFER_CAPACITY ((size_t)4096)


// Types.
/* A look that draws nothing and is 50 by 20. */
typedef struct PlainLookStruct
{
    UIButtonLookBase Base;
} PlainLook;

/* What the cue handler heard. */
typedef struct HeardCuesStruct
{
    UIElementCue Cues[32];
    uint64_t Ids[32];
    size_t Count;
} HeardCues;


// Static functions.
static Error Plain_Render(void* self, UIRenderContext* context)
{
    (void)self;
    (void)context;
    return Error_CreateSuccess();
}

static Error Plain_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)self;
    (void)context;
    (void)availableWidth;
    *outSize = (Vector2){ .x = 50.0f, .y = 20.0f };
    return Error_CreateSuccess();
}

/* Hit only in the left half of its box. */
static bool LeftHalf_HitTest(void* self, const WidgetHitTestArgs* args)
{
    (void)self;
    return args->IsInsideBox && (args->LocalPosition.x < 0.5f);
}

static const UILookVTable PlainVTable = { .Render = Plain_Render, .Measure = Plain_Measure };
static const UILookVTable LeftHalfVTable = { .Render = Plain_Render, .Measure = Plain_Measure, .HitTest = LeftHalf_HitTest };

static Error OnCue(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    HeardCues* Heard = UserData_GetPointer(userData);
    if ((event->Kind == UIEventKind_Cue) && (Heard->Count < 32U))
    {
        Heard->Cues[Heard->Count] = (UIElementCue)event->WidgetEventId;
        Heard->Ids[Heard->Count] = event->WidgetId;
        Heard->Count++;
    }
    return Error_CreateSuccess();
}

static bool HasHeard(const HeardCues* heard, uint64_t id, UIElementCue cue)
{
    for (size_t Index = 0U; Index < heard->Count; Index++)
    {
        if ((heard->Ids[Index] == id) && (heard->Cues[Index] == cue))
        {
            return true;
        }
    }
    return false;
}

static size_t CountInLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Count = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, LOG_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, LOG_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text)) && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* Found = strstr((const char*)Text._data, needle); Found != NULL; Found = strstr(Found + 1, needle))
        {
            Count++;
        }
    }
    Memory_Free(Path._data);
    Memory_Free(Text._data);
    return Count;
}

static uint64_t AddButton(TestRun* run, TestUIFixture* fixture, const unsigned char* look, float x)
{
    UIButtonOptions Options = UIButtonOptions_Create(u8"ab");
    Options.Element.Look = look;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(x, 100.0f, 100.0f, 40.0f);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    return Id;
}

static uint64_t TypeOf(TestUIFixture* fixture, uint64_t id)
{
    Widget* Target = NULL;
    return UIScreen_TryGetWidgetById(&fixture->Screen, id, &Target) ? Widget_GetTypeId(Target) : WIDGET_TYPE_ID_INVALID;
}

static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, LOOK_THEME)
        && TEST_CHECK_SUCCESS(run, UIKit_RegisterLook(&fixture->Kit, UIElementKind_Button, u8"tbp:plain", sizeof(PlainLook), &PlainVTable, NULL));
}


// Tests: looks.
/* An element's look: its options' name, else its theme's for the kind, else the standard one; looks are fixed at creation. */
static void TheLookComesFromTheOptionsTheThemeOrTheStandard(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Plain = WIDGET_TYPE_ID_INVALID;
        TEST_CHECK(run, UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Button, u8"tbp:plain", &Plain));
        uint64_t Standard = UIKit_GetElementType(&Fixture.Kit, UIElementKind_Button);
        TEST_CHECK(run, TypeOf(&Fixture, AddButton(run, &Fixture, NULL, 0.0f)) == Standard);
        TEST_CHECK(run, TypeOf(&Fixture, AddButton(run, &Fixture, u8"plain", 0.0f)) == Plain);

        TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, NAMING_THEME));
        uint64_t Named = AddButton(run, &Fixture, NULL, 0.0f);
        TEST_CHECK(run, TypeOf(&Fixture, Named) == Plain);
        TEST_CHECK(run, TypeOf(&Fixture, AddButton(run, &Fixture, u8"tbp:test_box", 0.0f)) == Standard); // The options win.

        // Another theme changes values, not the widget type of what exists.
        TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, LOOK_THEME));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, TypeOf(&Fixture, Named) == Plain);
        TEST_CHECK(run, TypeOf(&Fixture, AddButton(run, &Fixture, NULL, 0.0f)) == Standard);

        // Every kind has the standard look, and the panel's and the toggle's and the slider's are composed as the button's is.
        uint64_t Found = WIDGET_TYPE_ID_INVALID;
        TEST_CHECK(run, UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Panel, UI_KIT_STANDARD_LOOK, &Found));
        TEST_CHECK(run, UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Label, UI_KIT_STANDARD_LOOK, &Found));
        TEST_CHECK(run, UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Toggle, u8"test_box", &Found));
        TEST_CHECK(run, UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Slider, u8"tbp:test_box", &Found));
        TEST_CHECK(run, UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Tooltip, u8"tbp:test_box", &Found));
        TEST_CHECK(run, !UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Button, u8"Not A Key", &Found));
        TEST_CHECK(run, !UIKit_TryGetLookType(&Fixture.Kit, (UIElementKind)99, u8"tbp:test_box", &Found));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A name no look answers gets the standard look, and is logged once, however many elements ask. */
static void AnUnknownLookIsLoggedOnceAndGetsTheStandard(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, 0U, sizeof(unsigned char));
    Logger Log;
    TestUIFixture Fixture;
    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "ui_unknown_look", &Directory))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        if (TestUI_ConstructLogged(run, &Fixture, &Log) && TestUI_SetTheme(run, &Fixture, UNKNOWN_LOOK_THEME))
        {
            uint64_t Standard = UIKit_GetElementType(&Fixture.Kit, UIElementKind_Button);
            for (int Index = 0; Index < 3; Index++)
            {
                TEST_CHECK(run, TypeOf(&Fixture, AddButton(run, &Fixture, NULL, 0.0f)) == Standard);
                TEST_CHECK(run, TypeOf(&Fixture, AddButton(run, &Fixture, u8"nobody", 0.0f)) == Standard);
            }
            UIToggleOptions Toggle = UIToggleOptions_Create(u8"t");
            uint64_t ToggleId = WIDGET_ID_INVALID;
            TEST_CHECK_SUCCESS(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Toggle, &ToggleId));
            TEST_CHECK(run, TypeOf(&Fixture, ToggleId) == UIKit_GetElementType(&Fixture.Kit, UIElementKind_Toggle));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, CountInLog(run, Directory._data, "no look \"tbp:nobody\" for the element \"button\"") == 1U);
            TEST_CHECK(run, CountInLog(run, Directory._data, "no look \"tbp:nobody\" for the element \"toggle\"") == 0U); // One line a name.
        }
        TestUI_Deconstruct(run, &Fixture);
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    }
    Memory_Free(Directory._data);
}

/* Two looks of one kind on one screen: calling code cannot tell them apart, and a stale id is refused for both. */
static void TwoLooksOfAKindShareAScreen(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Ids[2] = { AddButton(run, &Fixture, NULL, 100.0f), AddButton(run, &Fixture, u8"tbp:plain", 400.0f) };
        TestUI_Tick(run, &Fixture);
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            TEST_CHECK_SUCCESS(run, UIButton_SetText(&Fixture.Screen, Ids[Index], u8"same"));
            TEST_CHECK_SUCCESS(run, UIButton_SetIsToggled(&Fixture.Screen, Ids[Index], true));
            bool IsToggled = false;
            TEST_CHECK_SUCCESS(run, UIButton_IsToggled(&Fixture.Screen, Ids[Index], &IsToggled));
            TEST_CHECK(run, IsToggled);
            Events.Count = 0U;
            TestUI_Click(run, &Fixture, (Index == 0U) ? 150.0f : 450.0f, 120.0f);
            TEST_CHECK(run, (Events.Count == 1U) && (Events.Events[0].WidgetId == Ids[Index])
                && (Events.Events[0].WidgetEventId == (int32_t)UIElementEvent_Activated));
            Widget* Target = NULL;
            if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Ids[Index], &Target)))
            {
                TEST_CHECK_SUCCESS(run, Widget_Destroy(Target));
            }
            TEST_CHECK_ERROR(run, UIButton_SetText(&Fixture.Screen, Ids[Index], u8"gone"), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, UIElement_SetScale(&Fixture.Screen, Ids[Index], 2.0f), ErrorCode_InvalidOperation);
        }
        TestUI_Tick(run, &Fixture);
        // A slider's calls refuse a button of either look.
        uint64_t Other = AddButton(run, &Fixture, u8"tbp:plain", 0.0f);
        TEST_CHECK_ERROR(run, UISlider_SetValue(&Fixture.Screen, Other, 1.0), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIToggle_SetState(&Fixture.Screen, Other, UIToggleState_On), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A look of a kind whose look the kit composes for panels, toggles and sliders works the same way: the kind answers the input. */
static void LooksOfEveryComposedKindWork(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        TEST_CHECK_SUCCESS(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Toggle, u8"tbp:plain", sizeof(UIToggleLookBase), &PlainVTable, NULL));
        TEST_CHECK_SUCCESS(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Slider, u8"tbp:plain", sizeof(UISliderLookBase), &PlainVTable, NULL));
        TEST_CHECK_SUCCESS(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Panel, u8"tbp:plain", sizeof(UIPanelLookBase), &PlainVTable, NULL));
        UIToggleOptions Toggle = UIToggleOptions_Create(u8"t");
        Toggle.Element.Look = u8"tbp:plain";
        Toggle.Element.HasPlacement = true;
        Toggle.Element.Placement = UIPlacement_FromTopLeft(100.0f, 100.0f, 100.0f, 40.0f);
        uint64_t ToggleId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Toggle, &ToggleId));
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Element.Look = u8"tbp:plain";
        Slider.Element.HasPlacement = true;
        Slider.Element.Placement = UIPlacement_FromTopLeft(300.0f, 100.0f, 200.0f, 20.0f);
        uint64_t SliderId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Slider, &SliderId));
        UIPanelOptions Panel = UIPanelOptions_Create();
        Panel.Element.Look = u8"tbp:plain";
        Panel.HasPadding = true;
        Panel.Padding = UIInsets_All(3.0f);
        Panel.Element.HasPlacement = true;
        Panel.Element.Placement = UIPlacement_FromTopLeft(600.0f, 100.0f, 200.0f, 200.0f);
        uint64_t PanelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &PanelId));
        UIButtonOptions Inside = UIButtonOptions_Create(u8"in");
        Inside.Element.Look = u8"tbp:plain";
        Inside.Element.Hint.Align = UIStackAlign_Start;
        uint64_t InsideId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, PanelId, &Inside, &InsideId));
        TestUI_Ticks(run, &Fixture, 2U);

        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        UIToggleState State = UIToggleState_Off;
        TEST_CHECK_SUCCESS(run, UIToggle_GetState(&Fixture.Screen, ToggleId, &State));
        TEST_CHECK(run, State == UIToggleState_On);
        // A slider look that says nothing of its thumb: the pointer is the value.
        TestUI_Click(run, &Fixture, 450.0f, 110.0f);
        double Value = 0.0;
        TEST_CHECK_SUCCESS(run, UISlider_GetValue(&Fixture.Screen, SliderId, &Value));
        TEST_CHECK(run, (Value > 0.74) && (Value < 0.76));
        // A panel look without a Layout of its own gets the options' padding.
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, InsideId), 603.0f, 103.0f, 50.0f, 20.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: cues.
/* Cues are posted for what elements do, the screen's own for entering and leaving, and nothing is queued while nobody listens. */
static void CuesArePostedOnlyWhileSomeoneListens(TestRun* run)
{
    TestUIFixture Fixture;
    HeardCues Heard = { .Count = 0U };
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddButton(run, &Fixture, NULL, 100.0f);
        UISliderOptions Slider = UISliderOptions_Create();
        Slider.Maximum = 10.0;
        Slider.HasStep = true;
        Slider.Step = 1.0;
        Slider.Element.HasPlacement = true;
        Slider.Element.Placement = UIPlacement_FromTopLeft(400.0f, 100.0f, 200.0f, 24.0f);
        uint64_t SliderId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Slider, &SliderId));
        TestUI_Tick(run, &Fixture);

        // Nobody listens: not even queued.
        TEST_CHECK_SUCCESS(run, UIScreen_PostCue(&Fixture.Screen, Button, UIElementCue_Open));
        TEST_CHECK(run, Fixture.Screen._eventQueue._count == 0U);
        uint64_t Subscription = 0U;
        UserData Data = UserData_FromPointer(&Heard);
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Cue), OnCue, &Data,
            &Subscription));
        TEST_CHECK_SUCCESS(run, UIScreen_PostCue(&Fixture.Screen, Button, UIElementCue_Open));
        TEST_CHECK(run, Fixture.Screen._eventQueue._count == 1U);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasHeard(&Heard, Button, UIElementCue_Open));

        // A button: hover, press, release, activate.
        Heard.Count = 0U;
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, HasHeard(&Heard, Button, UIElementCue_Hover) && HasHeard(&Heard, Button, UIElementCue_Press));
        TEST_CHECK(run, HasHeard(&Heard, Button, UIElementCue_Release) && HasHeard(&Heard, Button, UIElementCue_Activate));
        // Disabled, a press is refused.
        Heard.Count = 0U;
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Button, false));
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, HasHeard(&Heard, Button, UIElementCue_Refused) && !HasHeard(&Heard, Button, UIElementCue_Activate));

        // A slider entered by a press: the screen's enter cue, a step by a key, leave on Escape.
        Heard.Count = 0U;
        TestUI_Click(run, &Fixture, 410.0f, 112.0f);
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, HasHeard(&Heard, SliderId, UIElementCue_Enter) && HasHeard(&Heard, SliderId, UIElementCue_ValueStep));
        TEST_CHECK(run, HasHeard(&Heard, SliderId, UIElementCue_Leave));

        // A subscription to one widget hears its cues alone; gone, nothing is queued again.
        TEST_CHECK_SUCCESS(run, UIScreen_Unsubscribe(&Fixture.Screen, Subscription));
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, SliderId, UI_EVENT_KIND_MASK(UIEventKind_Cue), OnCue, &Data, &Subscription));
        TEST_CHECK_SUCCESS(run, UIScreen_PostCue(&Fixture.Screen, Button, UIElementCue_Open));
        TEST_CHECK(run, Fixture.Screen._eventQueue._count == 0U);
        TEST_CHECK_SUCCESS(run, UIScreen_PostCue(&Fixture.Screen, SliderId, UIElementCue_Close));
        TEST_CHECK(run, Fixture.Screen._eventQueue._count == 1U);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_ERROR(run, UIScreen_PostCue(&Fixture.Screen, 12345U, UIElementCue_Open), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScreen_PostCue(NULL, Button, UIElementCue_Open), ErrorCode_IllegalArgument);
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* The kit stays while widgets of its looks live, since they find their hooks through it: their screen goes first, then the kit. */
static void TheKitStaysWhileItsWidgetsLive(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddButton(run, &Fixture, NULL, 100.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_ERROR(run, UIKit_Deconstruct(&Fixture.Kit), ErrorCode_InvalidOperation);
        TEST_CHECK(run, UIKit_FromScreen(&Fixture.Screen) == &Fixture.Kit); // Left as it was.
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK_SUCCESS(run, UIButton_SetText(&Fixture.Screen, Button, u8"still here"));
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture); // The screen, then the kit, as always.
}

/* Each element cues what it does, however it is done: a button activated by a key, a toggle clicked, a slider moved by the wheel. A
 * slider at its end steps nowhere and cues nothing; a subscription to other kinds of event hears no cue, and none is queued for it. */
static void EveryElementCuesWhatItDoes(TestRun* run)
{
    TestUIFixture Fixture;
    HeardCues Heard = { .Count = 0U };
    if (Construct(run, &Fixture))
    {
        uint64_t Button = AddButton(run, &Fixture, NULL, 100.0f);
        UIToggleOptions ToggleOptions = UIToggleOptions_Create(u8"t");
        ToggleOptions.Element.HasPlacement = true;
        ToggleOptions.Element.Placement = UIPlacement_FromTopLeft(100.0f, 200.0f, 100.0f, 40.0f);
        uint64_t Toggle = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ToggleOptions, &Toggle));
        UISliderOptions SliderOptions = UISliderOptions_Create();
        SliderOptions.Maximum = 10.0;
        SliderOptions.HasStep = true;
        SliderOptions.Step = 1.0;
        SliderOptions.Element.HasPlacement = true;
        SliderOptions.Element.Placement = UIPlacement_FromTopLeft(400.0f, 100.0f, 200.0f, 24.0f);
        uint64_t Slider = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &SliderOptions, &Slider));
        TestUI_Tick(run, &Fixture);

        uint64_t Other = 0U;
        UserData Data = UserData_FromPointer(&Heard);
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnCue, &Data,
            &Other));
        TEST_CHECK_SUCCESS(run, UIScreen_PostCue(&Fixture.Screen, Button, UIElementCue_Open));
        TEST_CHECK(run, Fixture.Screen._eventQueue._count == 0U);
        TEST_CHECK_SUCCESS(run, UIScreen_Unsubscribe(&Fixture.Screen, Other));
        uint64_t Subscription = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&Fixture.Screen, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Cue), OnCue, &Data,
            &Subscription));

        Widget* ButtonWidget = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Button, &ButtonWidget)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, ButtonWidget));
        }
        TestUI_Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, HasHeard(&Heard, Button, UIElementCue_Activate));

        Heard.Count = 0U;
        TestUI_Click(run, &Fixture, 150.0f, 220.0f);
        TEST_CHECK(run, HasHeard(&Heard, Toggle, UIElementCue_Activate));

        Heard.Count = 0U;
        TestUI_Click(run, &Fixture, 410.0f, 112.0f); // Entered by the press, at its start.
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        double Value = -1.0;
        TEST_CHECK_SUCCESS(run, UISlider_GetValue(&Fixture.Screen, Slider, &Value));
        TEST_CHECK(run, (Value == 1.0) && HasHeard(&Heard, Slider, UIElementCue_ValueStep));
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Slider, 10.0));
        Heard.Count = 0U;
        TestUI_Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, !HasHeard(&Heard, Slider, UIElementCue_ValueStep));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: icons.
/* An icon is content: a letter or a sprite animation by name, checked; the standard look shows it before the text. */
static void IconsAreContentTheLookShows(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions ColumnOptions = UIStackOptions_Create(UIStackDirection_Column);
        ColumnOptions.Element.HasPlacement = true;
        ColumnOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ColumnOptions, &Column));
        UIButtonOptions Options = UIButtonOptions_Create(u8"ab");
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Plain = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Plain));
        Options.Icon = UIElementIcon_FromLetter((uint32_t)'Q');
        uint64_t Lettered = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Lettered));
        TestUI_Ticks(run, &Fixture, 3U);
        Rectangle Without = TestUI_GetPixelBox(run, &Fixture, Plain);
        Rectangle With = TestUI_GetPixelBox(run, &Fixture, Lettered);
        TEST_CHECK(run, Without.width == 30.0f);
        TEST_CHECK(run, With.width == (30.0f + 7.0f + 4.0f)); // A letter's advance and the gap's default.

        // Taken away, it measures as without; a sprite animation by name is completed and checked.
        UIElementIcon None = UIElementIcon_None();
        TEST_CHECK_SUCCESS(run, UIButton_SetIcon(&Fixture.Screen, Lettered, &None));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Lettered).width == 30.0f);
        UIElementIcon Sprite;
        TEST_CHECK_SUCCESS(run, UIElementIcon_FromSpriteAnimation(u8"icons/save", &Sprite));
        TEST_CHECK(run, (Sprite.Kind == UIElementIconKind_SpriteAnimation) && (strcmp((const char*)Sprite.Name, "tbp:icons/save") == 0));
        TEST_CHECK(run, UIElementIcon_IsValid(&Sprite));
        TEST_CHECK_ERROR(run, UIElementIcon_FromSpriteAnimation(u8"Not Valid", &Sprite), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIButton_SetIcon(&Fixture.Screen, Lettered, &Sprite)); // An animation that does not exist takes its box.
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Lettered).width == (30.0f + 14.0f + 4.0f));

        UIElementIcon Bad = UIElementIcon_FromLetter(0U);
        TEST_CHECK(run, !UIElementIcon_IsValid(&Bad));
        TEST_CHECK_ERROR(run, UIButton_SetIcon(&Fixture.Screen, Lettered, &Bad), ErrorCode_ArgumentOutOfRange);
        Bad = UIElementIcon_FromLetter(0xD800U);
        TEST_CHECK_ERROR(run, UIButton_SetIcon(&Fixture.Screen, Lettered, &Bad), ErrorCode_ArgumentOutOfRange);
        Bad = UIElementIcon_None();
        Bad.Kind = (UIElementIconKind)99;
        TEST_CHECK(run, !UIElementIcon_IsValid(&Bad));
        TEST_CHECK_ERROR(run, UIButton_SetIcon(&Fixture.Screen, Lettered, NULL), ErrorCode_ArgumentOutOfRange);
        Options.Icon = UIElementIcon_FromLetter(0U);
        uint64_t Refused = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Refused), ErrorCode_ArgumentOutOfRange);
        UIElementIcon Same = UIElementIcon_FromLetter((uint32_t)'Q');
        UIElementIcon Other = UIElementIcon_FromLetter((uint32_t)'R');
        TEST_CHECK(run, UIElementIcon_Equals(&Same, &Same) && !UIElementIcon_Equals(&Same, &Other) && !UIElementIcon_Equals(&Same, &Sprite));

        // The gap is the theme's custom value when it gives one; with no icon there is none.
        TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, SPACED_THEME));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Lettered).width == (30.0f + 14.0f + 24.0f));
        TEST_CHECK(run, TestUI_GetPixelBox(run, &Fixture, Plain).width == 30.0f);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A look's own hit test stands in for its box: this one is hit only in the box's left half. */
static void ALooksHitTestStandsInForItsBox(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Button, u8"tbp:left_half", sizeof(PlainLook), &LeftHalfVTable,
            NULL)))
    {
        uint64_t Button = AddButton(run, &Fixture, u8"tbp:left_half", 100.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 120.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Button);
        TestUI_MovePointer(&Fixture, 180.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UILookTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_look_comes_from_the_options_the_theme_or_the_standard", TheLookComesFromTheOptionsTheThemeOrTheStandard },
        { "an_unknown_look_is_logged_once_and_gets_the_standard", AnUnknownLookIsLoggedOnceAndGetsTheStandard },
        { "two_looks_of_a_kind_share_a_screen", TwoLooksOfAKindShareAScreen },
        { "looks_of_every_composed_kind_work", LooksOfEveryComposedKindWork },
        { "cues_are_posted_only_while_someone_listens", CuesArePostedOnlyWhileSomeoneListens },
        { "icons_are_content_the_look_shows", IconsAreContentTheLookShows },
        { "a_looks_hit_test_stands_in_for_its_box", ALooksHitTestStandsInForItsBox },
        { "every_element_cues_what_it_does", EveryElementCuesWhatItDoes },
        { "the_kit_stays_while_its_widgets_live", TheKitStaysWhileItsWidgetsLive },
    };
    TestRun_RunSuite(run, "UILook", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
