#include "TestSuites.h"
#include "TestUI.h"
#include "LabelWidget.h"
#include "UIKit.h"
#include "wr/WRCompile.h"
#include <math.h>
#include <string.h>


// Macros.
/** The fixture's look: flat boxes, a 16 pixel font whose glyphs advance 8, fixed thumb and box sizes. */
#define ELEMENT_THEME \
    "{\"name\": \"elements\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"panel\": {\"padding\": [10, 6], \"spacing\": 4, \"min_size\": [50, 40], \"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#101010\"}}}," \
    "\"label\": {\"text\": {\"normal\": \"#d0d0d0\"}}," \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24]," \
    "\"background\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#303030\", \"hovered\": \"#404040\", \"pressed\": \"#505050\", \"focused\": \"#606060\", \"disabled\": \"#707070\", \"selected\": \"#808080\"}}," \
    "\"text\": {\"normal\": \"#e0e0e0\"}, \"variants\": {\"danger\": {\"min_size\": [99, 24]}}}," \
    "\"toggle\": {\"box_size\": [16, 16], \"spacing\": 6, \"min_size\": [10, 20], \"box\": {\"kind\": \"flat\"}, \"check\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"slider\": {\"min_size\": [64, 24], \"thumb_size\": [20, 20], \"track_thickness\": 4, \"track\": {\"kind\": \"flat\"}, \"fill\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\"}}}}"
/** A theme whose colours fade over half a second. */
#define FADING_THEME \
    "{\"name\": \"fading\", \"transition_time\": 0.5, \"elements\": {\"button\": {\"background\": {\"kind\": \"flat\"," \
    "\"colors\": {\"normal\": \"#000000\", \"hovered\": \"#c80000\", \"pressed\": \"#00c800\"}}, \"text\": {\"normal\": \"#ffffff\"}}}}"
/** The button the tests click: at (100, 100), 200 x 40. */
#define BUTTON_X 100.0f
#define BUTTON_Y 100.0f
#define BUTTON_WIDTH 200.0f
#define BUTTON_HEIGHT 40.0f
/** The slider the tests drag: at (100, 100), 220 x 24, its 20 pixel thumb's centre travelling 200 pixels from 110. */
#define SLIDER_X 100.0f
#define SLIDER_Y 100.0f
#define SLIDER_WIDTH 220.0f
#define SLIDER_HEIGHT 24.0f
#define SLIDER_THUMB_START 110.0f
#define SLIDER_TRAVEL 200.0f


// Types.
/* A button of another look: it writes Render and Measure alone, and draws nothing of the theme's. */
typedef struct OwnButtonLookStruct
{
    UIButtonLookBase Base;
} OwnButtonLook;

/* A button look that keeps a laid-out text, to see what the theme gives it. */
typedef struct FontProbeLookStruct
{
    UIButtonLookBase Base;
    UIElementText Text;
} FontProbeLook;


// Static functions: other button looks.
static Error OwnButton_Render(void* self, UIRenderContext* context)
{
    (void)self;
    (void)context;
    return Error_CreateSuccess();
}

static Error OwnButton_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)self;
    (void)context;
    (void)availableWidth;
    *outSize = (Vector2){ .x = 77.0f, .y = 33.0f };
    return Error_CreateSuccess();
}

static const UILookVTable OwnButtonVTable = { .Render = OwnButton_Render, .Measure = OwnButton_Measure };

static Error FontProbe_Construct(void* self, void* args)
{
    const UIButtonArgs* Arguments = args;
    return UIElementText_Construct(&((FontProbeLook*)self)->Text, UIKit_GetTextFactory(Arguments->Kit), UIKit_GetAssets(Arguments->Kit),
        NULL);
}

static Error FontProbe_Deconstruct(void* self)
{
    return UIElementText_Deconstruct(&((FontProbeLook*)self)->Text);
}

static Error FontProbe_Update(void* self, ProgramTime time)
{
    (void)time;
    FontProbeLook* Probe = self;
    return UIElementText_Refresh(&Probe->Text, &Probe->Base.Base, &Probe->Base.Button._text);
}

static Error FontProbe_Render(void* self, UIRenderContext* context)
{
    (void)self;
    (void)context;
    return Error_CreateSuccess();
}

static Error FontProbe_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    FontProbeLook* Probe = self;
    Error Result = UIElementText_Refresh(&Probe->Text, &Probe->Base.Base, &Probe->Base.Button._text);
    Vector2 Pixels = UIElementText_GetPixelSize(&Probe->Text, Widget_GetScreen(&Probe->Base.Base));
    float Scale = Widget_GetEffectiveScale(&Probe->Base.Base);
    *outSize = (Vector2){ .x = Pixels.x / Scale, .y = Pixels.y / Scale };
    return Result;
}

static const UILookVTable FontProbeVTable =
{
    .Construct = FontProbe_Construct,
    .Deconstruct = FontProbe_Deconstruct,
    .Update = FontProbe_Update,
    .Render = FontProbe_Render,
    .Measure = FontProbe_Measure
};


// Static functions: the fixture.
static bool Construct(TestRun* run, TestUIFixture* fixture)
{
    return TestUI_Construct(run, fixture) && TestUI_SetTheme(run, fixture, ELEMENT_THEME);
}

static UIPlacement PlaceAt(float x, float y, float width, float height)
{
    return UIPlacement_FromTopLeft(x, y, width, height);
}

static uint64_t AddButton(TestRun* run, TestUIFixture* fixture, const unsigned char* text)
{
    UIButtonOptions Options = UIButtonOptions_Create(text);
    Options.Element.HasPlacement = true;
    Options.Element.Placement = PlaceAt(BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TestUI_Tick(run, fixture); // placed
    return Id;
}

static uint64_t AddToggle(TestRun* run, TestUIFixture* fixture, UIToggleState state)
{
    UIToggleOptions Options = UIToggleOptions_Create(u8"on");
    Options.State = state;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = PlaceAt(BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateToggle(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TestUI_Tick(run, fixture);
    return Id;
}

/* A slider over [0, 10] at @p value. */
static uint64_t AddSlider(TestRun* run, TestUIFixture* fixture, double value, bool hasStep, double step)
{
    UISliderOptions Options = UISliderOptions_Create();
    Options.Maximum = 10.0;
    Options.Value = value;
    Options.HasStep = hasStep;
    Options.Step = step;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = PlaceAt(SLIDER_X, SLIDER_Y, SLIDER_WIDTH, SLIDER_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&fixture->Kit, &fixture->Screen, WIDGET_ID_INVALID, &Options, &Id));
    TestUI_Tick(run, fixture);
    return Id;
}

static const UIElementLook* GetLook(TestUIFixture* fixture, uint64_t id)
{
    Widget* Target = NULL;
    if (!UIScreen_TryGetWidgetById(&fixture->Screen, id, &Target))
    {
        return NULL;
    }
    return Widget_GetCapability(Target, UIKit_GetCapabilityId(&fixture->Kit, UIElementCapability_Look));
}

/* The state an element shows, or a value of its own for one that is gone. */
static UIElementState GetState(TestUIFixture* fixture, uint64_t id)
{
    const UIElementLook* Look = GetLook(fixture, id);
    return (Look != NULL) ? Look->_state : UIElementState_Count;
}

static double GetSliderValue(TestRun* run, TestUIFixture* fixture, uint64_t id)
{
    double Value = NAN;
    TEST_CHECK_SUCCESS(run, UISlider_GetValue(&fixture->Screen, id, &Value));
    return Value;
}

static bool IsNearDouble(double value, double expected)
{
    return fabs(value - expected) < 1e-3;
}

/* The pointer's x for a value on the [0, 10] slider. */
static float SliderPointerFor(double value)
{
    return SLIDER_THUMB_START + (float)((value / 10.0) * (double)SLIDER_TRAVEL);
}

static bool IsEvent(const TestUIEvents* events, size_t index, uint64_t id, UIElementEvent event)
{
    return (index < events->Count) && (events->Events[index].Kind == UIEventKind_Widget) && (events->Events[index].WidgetId == id)
        && (events->Events[index].WidgetEventId == (int32_t)event);
}

static bool IsDoubleEvent(const TestUIEvents* events, size_t index, uint64_t id, UIElementEvent event, double value)
{
    return IsEvent(events, index, id, event) && events->Events[index].HasValue && (events->Events[index].Value.Type == UIPropertyType_Double)
        && IsNearDouble(events->Events[index].Value.Data.DoubleValue, value);
}

static bool IsBoolEvent(const TestUIEvents* events, size_t index, uint64_t id, UIElementEvent event, bool value)
{
    return IsEvent(events, index, id, event) && events->Events[index].HasValue && (events->Events[index].Value.Type == UIPropertyType_Bool)
        && (events->Events[index].Value.Data.BoolValue == value);
}


// Tests: the button.
static void AButtonClicksOnAPressAndAReleaseOnIt(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Hovered);

        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);
        TEST_CHECK(run, Events.Count == 0U);

        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Activated) && !Events.Events[0].HasValue);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Hovered);

        // It took the focus: the pointer leaving shows it.
        TestUI_RemovePointer(&Fixture);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Focused);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AReleaseElsewhereDoesNothing(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        // Off it the look lets go, back on it the look returns, and a release away does nothing.
        TestUI_MovePointer(&Fixture, 600.0f, 400.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);
        TestUI_MovePointer(&Fixture, 600.0f, 400.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);

        // A press that began elsewhere and a release on it does nothing either.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);

        // Another button than the left does nothing, and off the surface there is nothing to release on.
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_RemovePointer(&Fixture);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A release over what covers the button is a release elsewhere. */
static void AReleaseOverACoverDoesNothing(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        // A panel appears over it during the press.
        UIPanelOptions Cover = UIPanelOptions_Create();
        Cover.Element.HasPlacement = true;
        Cover.Element.Placement = PlaceAt(0.0f, 0.0f, 500.0f, 400.0f);
        uint64_t CoverId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Cover, &CoverId));
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
        TEST_CHECK(run, Id != WIDGET_ID_INVALID);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A disabled button takes the click and reports nothing: it must not reach what lies behind. */
static void ADisabledButtonSwallowsTheClickAndReportsNothing(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIButtonOptions Behind = UIButtonOptions_Create(u8"behind");
        Behind.Element.HasPlacement = true;
        Behind.Element.Placement = PlaceAt(0.0f, 0.0f, 500.0f, 400.0f);
        uint64_t BehindId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Behind, &BehindId));

        UIButtonOptions Options = UIButtonOptions_Create(u8"off");
        Options.IsEnabled = false;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Disabled);

        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_MouseLeft)); // Consumed: the game does not see it.
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Disabled);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);

        // It takes no focus and no key.
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) != Id);
        Widget* Base = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base) && !Widget_IsFocusable(Base));

        bool IsEnabled = true;
        TEST_CHECK_SUCCESS(run, UIButton_IsEnabled(&Fixture.Screen, Id, &IsEnabled));
        TEST_CHECK(run, !IsEnabled);
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, true));
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Activated));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Disabled during a press, the release does nothing even when enabled again; its focus goes. */
static void DisablingEndsAPressInProgress(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id);
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, false));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, true));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EnterOrSpaceClicksAndAHeldKeyDoesNotClickAgain(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Base));
            TestUI_Press(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Activated));
            TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_Enter)); // Consumed.
            TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);

            // Its repeats do not click again; its release ends the look.
            TestUI_Repeat(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TestUI_Repeat(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 1U);
            TestUI_Release(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Focused);
            TEST_CHECK(run, Events.Count == 1U);

            // Space too, and the space it types goes nowhere.
            TestUI_Press(&Fixture, GameKey_Space);
            Fixture.Sample.TextCodepoints[0] = (uint32_t)' ';
            Fixture.Sample.TextCodepointCount = 1U;
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 2U) && IsEvent(&Events, 1U, Id, UIElementEvent_Activated));
            TEST_CHECK(run, GameInput_GetTextCodepointCount(&Fixture.Input) == 0U);
            TestUI_Release(&Fixture, GameKey_Space);
            TestUI_Tick(run, &Fixture);

            // Another key is left alone.
            TestUI_Press(&Fixture, GameKey_A);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 2U);
            TEST_CHECK(run, GameInput_IsKeyDown(&Fixture.Input, GameKey_A));
            TestUI_Release(&Fixture, GameKey_A);
            TestUI_Tick(run, &Fixture);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The accept action is rebindable: the key it is bound to clicks, the old one does not. */
static void TheAcceptActionsKeyClicks(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Base));
            GameInputBinding Binding = { .Key = GameKey_F, .Modifiers = 0U };
            TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UIAccept, &Binding, 1U));
            TestUI_Tap(run, &Fixture, GameKey_Enter);
            TEST_CHECK(run, Events.Count == 0U);
            TestUI_Tap(run, &Fixture, GameKey_F);
            TEST_CHECK(run, Events.Count == 1U);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The event survives its widget: a handler destroying the button, or a later one, still sees it; the order is the input's. */
static void AHandlerMayDestroyTheButton(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        Events.IsDestroyingTheWidget = true;
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Activated));
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, Id));
        TestUI_Tick(run, &Fixture);

        // A stale id is refused, not a crash, by every function.
        const unsigned char* Text = NULL;
        bool Flag = false;
        TEST_CHECK_ERROR(run, UIButton_SetText(&Fixture.Screen, Id, u8"x"), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_GetText(&Fixture.Screen, Id, &Text), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, true), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_IsEnabled(&Fixture.Screen, Id, &Flag), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_SetIsToggled(&Fixture.Screen, Id, true), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_IsToggled(&Fixture.Screen, Id, &Flag), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIElement_SetVariant(&Fixture.Screen, Id, u8"danger"), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_SetText(NULL, Id, u8"x"), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A press whose release cannot reach the button any more does not leave it pressed. */
static void ADetachedPressDoesNotStayPressed(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            // Out of the tree during the press, and back after the button came up.
            TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Screen, Base));
            TestUI_Tick(run, &Fixture);
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.Screen, Base));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Hovered);
            TEST_CHECK(run, Events.Count == 0U);

            // The same for a key: focused, its hold is lost with it.
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Base));
            TestUI_Press(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 1U);
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, false));
            TestUI_Tick(run, &Fixture);
            TestUI_Release(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, true));
            TestUI_RemovePointer(&Fixture);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Normal);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The states show in their order; a toggled button stays selected under the pointer. */
static void OnlyTheLeftButtonPresses(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);

        // While the left button holds a press, the right one coming and going is no click.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 1U);

        // Moved off and let go in one tick is a release elsewhere.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 700.0f, 300.0f);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 1U) && (GetState(&Fixture, Id) != UIElementState_Pressed));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A release is no click once the button stopped taking input, also with something of its own under the pointer. */
static void AButtonThatStoppedTakingInputDoesNotClick(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        UIButtonOptions Inner = UIButtonOptions_Create(u8"in");
        Inner.Element.HasPlacement = true;
        Inner.Element.Placement = PlaceAt(100.0f, 0.0f, 50.0f, BUTTON_HEIGHT);
        uint64_t InnerId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Id, &Inner, &InnerId));
        TestUI_Tick(run, &Fixture);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TestUI_MovePointer(&Fixture, 110.0f, 120.0f);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, false));
            TestUI_MovePointer(&Fixture, 220.0f, 120.0f); // Over the inner button.
            TestUI_Tick(run, &Fixture);
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 0U);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A second key while the first is held is no second press. */
static void ASecondKeyDuringAHoldDoesNotClick(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Base));
            TestUI_Press(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 1U);
            TestUI_Press(&Fixture, GameKey_Space);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 1U);
            TestUI_Release(&Fixture, GameKey_Space);
            TestUI_Release(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 1U);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What the press helper answers for each key event, called directly: what is handled, and what is an activation. */
static void ThePressHelperAnswersForKeys(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            UIPressable Press = UIPressable_Create();
            bool IsHandled = false;
            bool IsActivated = false;
            WidgetKeyboardInputArgs Args = { .Type = UIKeyboardInputType_KeyRepeat, .Key = GameKey_Space };

            // With nothing held, a repeat and a typed space are nobody's.
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, !IsHandled && !IsActivated);
            Args = (WidgetKeyboardInputArgs){ .Type = UIKeyboardInputType_Text, .Key = GameKey_Unknown, .Codepoint = (uint32_t)' ' };
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, !IsHandled && !IsActivated);

            // Disabled, a press is not taken.
            Args = (WidgetKeyboardInputArgs){ .Type = UIKeyboardInputType_KeyPress, .Key = GameKey_Space };
            UIPressable_OnKeyboardInput(&Press, Base, &Args, false, &IsHandled, &IsActivated);
            TEST_CHECK(run, !IsHandled && !IsActivated && !UIPressable_IsPressed(&Press));

            // Enabled, it is, and it owns that key's repeat and the space it types; another key's are not its.
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, IsHandled && IsActivated && UIPressable_IsPressed(&Press));
            IsHandled = false;
            Args = (WidgetKeyboardInputArgs){ .Type = UIKeyboardInputType_KeyRepeat, .Key = GameKey_Space };
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, IsHandled && !IsActivated);
            Args = (WidgetKeyboardInputArgs){ .Type = UIKeyboardInputType_KeyRepeat, .Key = GameKey_Enter };
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, !IsHandled && !IsActivated);
            Args = (WidgetKeyboardInputArgs){ .Type = UIKeyboardInputType_Text, .Key = GameKey_Unknown, .Codepoint = (uint32_t)' ' };
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, IsHandled);
            IsHandled = false;
            Args.Codepoint = (uint32_t)'x';
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, !IsHandled);

            Args = (WidgetKeyboardInputArgs){ .Type = UIKeyboardInputType_KeyRelease, .Key = GameKey_Enter };
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, !IsHandled && UIPressable_IsPressed(&Press));
            Args = (WidgetKeyboardInputArgs){ .Type = UIKeyboardInputType_KeyRelease, .Key = GameKey_Space };
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, IsHandled && !UIPressable_IsPressed(&Press));

            // Cancelled with the key still down: nothing is held, and the next press of it counts as new.
            Args = (WidgetKeyboardInputArgs){ .Type = UIKeyboardInputType_KeyPress, .Key = GameKey_Space };
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, UIPressable_IsPressed(&Press));
            UIPressable_Cancel(&Press);
            TEST_CHECK(run, !UIPressable_IsPressed(&Press));
            IsActivated = false;
            UIPressable_OnKeyboardInput(&Press, Base, &Args, true, &IsHandled, &IsActivated);
            TEST_CHECK(run, IsActivated);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Made toggled, a button shows its selected look from the start. */
static void AButtonCanBeMadeToggled(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIButtonOptions Options = UIButtonOptions_Create(u8"go");
        Options.IsToggled = true;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Selected);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The same text again changes nothing: a cached size stays. */
static void TheSameTextKeepsTheCachedSize(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        UIButtonOptions Options = UIButtonOptions_Create(u8"same");
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 2U);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK(run, Base->_isMeasureValid);
            TEST_CHECK_SUCCESS(run, UIButton_SetText(&Fixture.Screen, Id, u8"same"));
            TEST_CHECK(run, Base->_isMeasureValid);
            TEST_CHECK_SUCCESS(run, UIButton_SetText(&Fixture.Screen, Id, u8"other"));
            TEST_CHECK(run, !Base->_isMeasureValid);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What is hidden, itself or by what holds it, is out of the keyboard's reach. */
static void AHiddenButtonIsOutOfTheKeyboardsReach(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = PlaceAt(0.0f, 0.0f, 200.0f, 100.0f);
        uint64_t Holder = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Holder));
        UIButtonOptions Options = UIButtonOptions_Create(u8"in");
        uint64_t Inside = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Holder, &Options, &Inside));
        TestUI_Tick(run, &Fixture);
        Widget* HolderBase = NULL;
        Widget* InsideBase = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Holder, &HolderBase))
            && TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Inside, &InsideBase)))
        {
            // Hiding what holds the focused button takes its focus away.
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, InsideBase));
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(HolderBase, false));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            TEST_CHECK(run, !Widget_IsFocused(InsideBase));
            TestUI_Tick(run, &Fixture);

            // It cannot be given back, by code or by navigation, and its keys do nothing.
            TEST_CHECK_ERROR(run, UIScreen_SetFocusedWidget(&Fixture.Screen, InsideBase), ErrorCode_InvalidOperation);
            TestUI_Tap(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            TEST_CHECK(run, !UIScreen_IsNavigating(&Fixture.Screen)); // Nothing to navigate.
            TestUI_Tap(run, &Fixture, GameKey_Enter);
            TEST_CHECK(run, Events.Count == 0U);

            // Shown again, it can.
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(HolderBase, true));
            TestUI_Ticks(run, &Fixture, 2U);
            TestUI_Tap(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Inside);

            // Hiding the focused button itself, and a button that has focus does not click once hidden.
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(InsideBase, false));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            TestUI_Tap(run, &Fixture, GameKey_Space);
            TEST_CHECK(run, Events.Count == 0U);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AButtonsStatesShowInTheirOrder(TestRun* run)
{
    TEST_CHECK(run, UIElementState_Choose(false, true, true, true, true, true, true) == UIElementState_Disabled);
    TEST_CHECK(run, UIElementState_Choose(true, true, true, true, true, true, true) == UIElementState_Pressed);
    TEST_CHECK(run, UIElementState_Choose(true, false, true, true, true, true, true) == UIElementState_Invalid);
    TEST_CHECK(run, UIElementState_Choose(true, false, false, true, true, true, true) == UIElementState_Entered);
    TEST_CHECK(run, UIElementState_Choose(true, false, false, false, true, true, true) == UIElementState_Selected);
    TEST_CHECK(run, UIElementState_Choose(true, false, false, false, false, true, true) == UIElementState_Hovered);
    TEST_CHECK(run, UIElementState_Choose(true, false, false, false, false, false, true) == UIElementState_Focused);
    TEST_CHECK(run, UIElementState_Choose(true, false, false, false, false, false, false) == UIElementState_Normal);

    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TEST_CHECK_SUCCESS(run, UIButton_SetIsToggled(&Fixture.Screen, Id, true));
        bool IsToggled = false;
        TEST_CHECK_SUCCESS(run, UIButton_IsToggled(&Fixture.Screen, Id, &IsToggled));
        TEST_CHECK(run, IsToggled);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Selected);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A button's text is measured with the theme's padding, its variant's minimum counts, its size follows the text. */
static void AButtonsPreferredSizeFollowsItsTextAndLook(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        UIButtonOptions Options = UIButtonOptions_Create(u8"abcd"); // 4 glyphs of 8: 32 wide, 16 tall
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 48.0f, 24.0f)); // 32 + 2 x 8, 16 + 2 x 4

        // New text, a variant whose minimum is larger, and a scale.
        TEST_CHECK_SUCCESS(run, UIButton_SetText(&Fixture.Screen, Id, u8"abcdefgh"));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 80.0f, 24.0f));
        const unsigned char* Text = NULL;
        TEST_CHECK_SUCCESS(run, UIButton_GetText(&Fixture.Screen, Id, &Text));
        TEST_CHECK(run, (Text != NULL) && (strcmp((const char*)Text, "abcdefgh") == 0));

        TEST_CHECK_SUCCESS(run, UIElement_SetVariant(&Fixture.Screen, Id, u8"danger"));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 99.0f, 24.0f));
        TEST_CHECK_SUCCESS(run, UIElement_SetVariant(&Fixture.Screen, Id, NULL));
        TEST_CHECK_ERROR(run, UIElement_SetVariant(&Fixture.Screen, Id, u8"Bad Name"), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 160.0f, 48.0f));

        // A text that is not valid UTF-8 is refused and the old one stays.
        const unsigned char Bad[] = { 0xFF, 0xFE, 0x00 };
        TEST_CHECK_ERROR(run, UIButton_SetText(&Fixture.Screen, Id, Bad), ErrorCode_InvalidTextEncoding);
        TEST_CHECK_SUCCESS(run, UIButton_GetText(&Fixture.Screen, Id, &Text));
        TEST_CHECK(run, strcmp((const char*)Text, "abcdefgh") == 0);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Colours fade between states over the theme's time and never jump, also when the state turns around half way. */
static void ColoursFadeFromStateToState(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, FADING_THEME))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        const UIElementLook* Look = GetLook(&Fixture, Id);
        if (TEST_CHECK(run, Look != NULL))
        {
            const UIStateColors* Colors = &UIThemeStyle_GetBox(UIElementLook_GetStyle(Look, &Fixture.Screen), UIButtonPart_Background)->Colors;
            TEST_CHECK(run, UIElementLook_BlendColors(Look, Colors, WHITE).r == 0U); // Its first state shows at once.

            TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
            TestUI_Ticks(run, &Fixture, 120U); // A quarter of a second: half way to hovered.
            Color HalfWay = UIElementLook_BlendColors(Look, Colors, WHITE);
            TEST_CHECK(run, fabs((double)HalfWay.r - 100.0) <= 3.0);

            // The pointer leaves: back toward normal from where it is, without a jump.
            TestUI_RemovePointer(&Fixture);
            TestUI_Tick(run, &Fixture);
            Color Turned = UIElementLook_BlendColors(Look, Colors, WHITE);
            TEST_CHECK(run, fabs((double)Turned.r - (double)HalfWay.r) <= 3.0);
            TestUI_Ticks(run, &Fixture, 240U);
            TEST_CHECK(run, UIElementLook_BlendColors(Look, Colors, WHITE).r == 0U);

            // The weights always sum to one, through a press across three states.
            TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
            TestUI_Ticks(run, &Fixture, 60U);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Ticks(run, &Fixture, 30U);
            float Sum = 0.0f;
            for (int32_t Index = 0; Index < (int32_t)UIElementState_Count; Index++)
            {
                Sum += Look->_weights[Index];
            }
            TEST_CHECK(run, fabsf(Sum - 1.0f) < 1e-3f);
            Color Mixed = UIElementLook_BlendColors(Look, Colors, WHITE);
            TEST_CHECK(run, (Mixed.r > 0U) && (Mixed.g > 0U)); // Hovered's red and pressed's green at once.
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);

            // Render only reads: asking twice gives the same answer.
            TEST_CHECK(run, UIElementLook_BlendColors(Look, Colors, WHITE).g == UIElementLook_BlendColors(Look, Colors, WHITE).g);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* An element's first state shows at once whichever it is, and a theme with no transition time switches at once also in a tick of no time. */
static void TheFirstStateAndNoTransitionAreImmediate(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture) && TestUI_SetTheme(run, &Fixture, FADING_THEME))
    {
        UIButtonOptions Options = UIButtonOptions_Create(u8"go");
        Options.IsEnabled = false;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Tick(run, &Fixture);
        const UIElementLook* Look = GetLook(&Fixture, Id);
        if (TEST_CHECK(run, Look != NULL))
        {
            TEST_CHECK(run, (Look->_weights[UIElementState_Disabled] == 1.0f) && (Look->_weights[UIElementState_Normal] == 0.0f));
        }

        TEST_CHECK_SUCCESS(run, UIScreen_ClearTheme(&Fixture.Screen));
        UIElementLook Own;
        TEST_CHECK_SUCCESS(run, UIElementLook_Construct(&Own, UIElementKind_Button, NULL));
        UIElementLook_Update(&Own, &Fixture.Screen, UIElementState_Normal, 0.0);
        UIElementLook_Update(&Own, &Fixture.Screen, UIElementState_Hovered, 0.0);
        TEST_CHECK(run, (Own._weights[UIElementState_Hovered] == 1.0f) && (Own._weights[UIElementState_Normal] == 0.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the toggle.
static void AToggleSwapsAndAClickFromMixedTurnsItOn(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddToggle(run, &Fixture, UIToggleState_Mixed);
        UIToggleState State = UIToggleState_Off;
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK_SUCCESS(run, UIToggle_GetState(&Fixture.Screen, Id, &State));
        TEST_CHECK(run, State == UIToggleState_On);
        TEST_CHECK(run, (Events.Count == 2U) && IsBoolEvent(&Events, 0U, Id, UIElementEvent_Changed, true)
            && IsBoolEvent(&Events, 1U, Id, UIElementEvent_Committed, true));

        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK_SUCCESS(run, UIToggle_GetState(&Fixture.Screen, Id, &State));
        TEST_CHECK(run, State == UIToggleState_Off);
        TEST_CHECK(run, (Events.Count == 4U) && IsBoolEvent(&Events, 2U, Id, UIElementEvent_Changed, false)
            && IsBoolEvent(&Events, 3U, Id, UIElementEvent_Committed, false));

        // A release elsewhere changes nothing.
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 700.0f, 300.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 4U);

        // Keys do what a click does, once per press: it has the focus from its last click.
        TestUI_Tap(run, &Fixture, GameKey_Space);
        TEST_CHECK_SUCCESS(run, UIToggle_GetState(&Fixture.Screen, Id, &State));
        TEST_CHECK(run, State == UIToggleState_On);
        TestUI_Press(&Fixture, GameKey_Enter);
        TestUI_Tick(run, &Fixture);
        TestUI_Repeat(&Fixture, GameKey_Enter);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_Enter);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIToggle_GetState(&Fixture.Screen, Id, &State));
        TEST_CHECK(run, State == UIToggleState_Off);
        TEST_CHECK(run, Events.Count == 8U);

        // Code sets it without an event; what is not a state is refused.
        TEST_CHECK_SUCCESS(run, UIToggle_SetState(&Fixture.Screen, Id, UIToggleState_Mixed));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 8U);
        TEST_CHECK_ERROR(run, UIToggle_SetState(&Fixture.Screen, Id, (UIToggleState)9), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, UIToggle_GetState(&Fixture.Screen, Id, &State));
        TEST_CHECK(run, State == UIToggleState_Mixed);

        // Disabled, it swallows the click.
        TEST_CHECK_SUCCESS(run, UIToggle_SetIsEnabled(&Fixture.Screen, Id, false));
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK_SUCCESS(run, UIToggle_GetState(&Fixture.Screen, Id, &State));
        TEST_CHECK(run, State == UIToggleState_Mixed);
        TEST_CHECK(run, Events.Count == 8U);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Disabled);

        TEST_CHECK_SUCCESS(run, UIToggle_SetText(&Fixture.Screen, Id, u8"new"));
        TEST_CHECK_ERROR(run, UIToggle_SetText(&Fixture.Screen, 99U, u8"new"), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIToggle_SetState(&Fixture.Screen, 99U, UIToggleState_On), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIToggle_GetState(&Fixture.Screen, 99U, &State), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIToggle_SetIsEnabled(&Fixture.Screen, 99U, true), ErrorCode_InvalidOperation);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void DisablingAToggleEndsItsPressAndItsFocus(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddToggle(run, &Fixture, UIToggleState_Off);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id);
        TEST_CHECK_SUCCESS(run, UIToggle_SetIsEnabled(&Fixture.Screen, Id, false));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIToggle_SetIsEnabled(&Fixture.Screen, Id, true));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AToggleMeasuresItsBoxAndText(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        UIToggleOptions Options = UIToggleOptions_Create(u8"abc");
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 46.0f, 20.0f)); // 16 + 6 + 3 x 8; the minimum 20 tall

        Options.State = (UIToggleState)9;
        uint64_t Refused = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Refused), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Refused == WIDGET_ID_INVALID);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the slider.
static void TheSlidersArithmetic(TestRun* run)
{
    TEST_CHECK(run, UISlider_ClampAndSnap(5.0, 0.0, 10.0, false, 0.0) == 5.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(-5.0, 0.0, 10.0, false, 0.0) == 0.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(15.0, 0.0, 10.0, false, 0.0) == 10.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(NAN, 2.0, 10.0, false, 0.0) == 2.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(INFINITY, 2.0, 10.0, false, 0.0) == 10.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(-INFINITY, 2.0, 10.0, false, 0.0) == 2.0);

    // Steps count from the lower end, and the upper end stays reachable.
    TEST_CHECK(run, UISlider_ClampAndSnap(4.4, 0.0, 10.0, true, 1.0) == 4.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(4.6, 0.0, 10.0, true, 1.0) == 5.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(3.0, 1.0, 10.0, true, 4.0) == 5.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(9.4, 1.0, 10.0, true, 4.0) == 9.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(10.0, 1.0, 10.0, true, 4.0) == 10.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(9.99, 0.0, 10.0, true, 7.0) == 10.0); // Past the last whole step the end counts as one.
    TEST_CHECK(run, UISlider_ClampAndSnap(8.4, 0.0, 10.0, true, 7.0) == 7.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(8.6, 0.0, 10.0, true, 7.0) == 10.0);

    // A step goes to the next whole step, and from the end to the last one below it, never past a value on the way.
    TEST_CHECK(run, UISlider_GetStepped(4.0, 0.0, 10.0, true, 1.0, 1.0) == 5.0);
    TEST_CHECK(run, UISlider_GetStepped(4.0, 0.0, 10.0, true, 1.0, -1.0) == 3.0);
    TEST_CHECK(run, UISlider_GetStepped(4.0, 0.0, 10.0, true, 1.0, 3.0) == 7.0);
    TEST_CHECK(run, UISlider_GetStepped(9.0, 0.0, 10.0, true, 1.0, 5.0) == 10.0);
    TEST_CHECK(run, UISlider_GetStepped(1.0, 0.0, 10.0, true, 1.0, -5.0) == 0.0);
    TEST_CHECK(run, UISlider_GetStepped(7.0, 0.0, 10.0, true, 7.0, 1.0) == 10.0);
    TEST_CHECK(run, UISlider_GetStepped(10.0, 0.0, 10.0, true, 7.0, -1.0) == 7.0);
    TEST_CHECK(run, UISlider_GetStepped(7.0, 0.0, 10.0, true, 7.0, -1.0) == 0.0);
    TEST_CHECK(run, UISlider_GetStepped(0.3, 0.1, 1.0, true, 0.1, 1.0) > 0.35); // 0.30000000000000004 is on its step
    TEST_CHECK(run, UISlider_GetStepped(0.3, 0.1, 1.0, true, 0.1, -1.0) < 0.25);
    TEST_CHECK(run, UISlider_GetStepped(4.0, 0.0, 10.0, true, 1.0, 0.4) == 4.0); // Less than half a step moves nothing.
    TEST_CHECK(run, UISlider_GetStepped(2.7, 0.0, 10.0, true, 1.0, 0.4) == 2.7); // Also where it is not on a step.
    TEST_CHECK(run, UISlider_GetStepped(2.7, 0.0, 10.0, true, 1.0, 1.0) == 3.0); // From between two, the next one.
    TEST_CHECK(run, UISlider_GetStepped(2.3, 0.0, 10.0, true, 1.0, -1.0) == 2.0);
    TEST_CHECK(run, IsNearDouble(UISlider_GetStepped(4.0, 0.0, 10.0, false, 0.0, 1.0), 4.1)); // A hundredth of the range.
    TEST_CHECK(run, IsNearDouble(UISlider_GetStepped(4.0, 0.0, 10.0, false, 0.0, -0.5), 3.95));

    TEST_CHECK(run, UISlider_GetFraction(5.0, 0.0, 10.0) == 0.5);
    TEST_CHECK(run, UISlider_GetFraction(-1.0, 0.0, 10.0) == 0.0);
    TEST_CHECK(run, UISlider_GetFraction(11.0, 0.0, 10.0) == 1.0);
    TEST_CHECK(run, UISlider_GetFraction(5.0, 10.0, 10.0) == 0.0);

    TEST_CHECK(run, UISlider_GetPointerFraction(10.0, 220.0, 20.0) == 0.0);
    TEST_CHECK(run, UISlider_GetPointerFraction(110.0, 220.0, 20.0) == 0.5);
    TEST_CHECK(run, UISlider_GetPointerFraction(210.0, 220.0, 20.0) == 1.0);
    TEST_CHECK(run, UISlider_GetPointerFraction(-500.0, 220.0, 20.0) == 0.0);
    TEST_CHECK(run, UISlider_GetPointerFraction(9000.0, 220.0, 20.0) == 1.0);
    TEST_CHECK(run, UISlider_GetPointerFraction(10.0, 20.0, 20.0) == 0.0); // No room to travel.
    TEST_CHECK(run, UISlider_GetPointerFraction(15.0, 20.0, 20.0) == 0.0);

    // What is no number stays in the range; a step that cannot be counted with is no step.
    TEST_CHECK(run, UISlider_ClampAndSnap(5.0, 0.0, 10.0, true, INFINITY) == 5.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(5.0, 0.0, 10.0, true, NAN) == 5.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(5.0, 0.0, 10.0, true, -1.0) == 5.0);
    TEST_CHECK(run, UISlider_ClampAndSnap(0.5, 0.0, 1.0, true, 1e-320) == 0.5);
    TEST_CHECK(run, UISlider_GetStepped(NAN, 2.0, 10.0, true, 1.0, 1.0) == 3.0);
    TEST_CHECK(run, UISlider_GetStepped(NAN, 2.0, 10.0, false, 0.0, 0.0) == 2.0);
    TEST_CHECK(run, UISlider_GetStepped(4.0, 0.0, 10.0, true, 1.0, NAN) == 4.0);
    TEST_CHECK(run, UISlider_GetStepped(4.0, 0.0, 10.0, false, 0.0, NAN) == 4.0);
    TEST_CHECK(run, UISlider_GetStepped(20.0, 0.0, 10.0, true, 1.0, 0.0) == 10.0);
    TEST_CHECK(run, UISlider_GetStepped(-20.0, 0.0, 10.0, true, 1.0, 0.0) == 0.0);
    TEST_CHECK(run, UISlider_GetStepped(4.0, 0.0, 10.0, true, 1.0, INFINITY) == 10.0);
    TEST_CHECK(run, UISlider_GetStepped(4.0, 0.0, 10.0, true, 1.0, -INFINITY) == 0.0);
    TEST_CHECK(run, IsNearDouble(UISlider_GetStepped(4.0, 0.0, 10.0, true, INFINITY, 1.0), 4.1));
    TEST_CHECK(run, UISlider_GetStepped(0.5, 0.0, 1.0, true, 1e-320, 1.0) == 0.5);
    TEST_CHECK(run, UISlider_GetPointerFraction(10.0, 5.0, 20.0) == 0.0);
}

static void DraggingMapsThePointerAndClampsOutsideTheBox(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);

        // The press itself moves the value to the pointer: Changed, no commit yet.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 5.0));
        TEST_CHECK(run, (Events.Count == 1U) && IsDoubleEvent(&Events, 0U, Id, UIElementEvent_Changed, 5.0));
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);

        TestUI_MovePointer(&Fixture, SliderPointerFor(7.5), 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 7.5));
        TEST_CHECK(run, (Events.Count == 2U) && IsDoubleEvent(&Events, 1U, Id, UIElementEvent_Changed, 7.5));

        // Past either end of the box it stays at the end; far above the box it still follows along x.
        TestUI_MovePointer(&Fixture, 900.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 10.0);
        TestUI_MovePointer(&Fixture, 0.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 0.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(4.0), 450.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 4.0));

        // A pointer that does not move posts nothing; the release commits.
        size_t Count = Events.Count;
        TestUI_Ticks(run, &Fixture, 5U);
        TEST_CHECK(run, Events.Count == Count);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == (Count + 1U)) && IsDoubleEvent(&Events, Count, Id, UIElementEvent_Committed, 4.0));
        TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);

        // The pointer leaving the surface during a drag: the value stays, the release still ends it.
        TestUI_MovePointer(&Fixture, SliderPointerFor(4.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_RemovePointer(&Fixture);
        TestUI_Tick(run, &Fixture);
        Count = Events.Count;
        TestUI_MovePointer(&Fixture, SliderPointerFor(6.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 6.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed, 6.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A click on the thumb changes nothing, wherever on it, so there is nothing to commit; a drag from there keeps the hold. */
static void AClickOnTheThumbChangesNothing(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, false, 0.0);
        TestUI_Click(run, &Fixture, SliderPointerFor(5.0), 110.0f);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Click(run, &Fixture, SliderPointerFor(5.0) + 8.0f, 110.0f); // Near the thumb's edge.
        TEST_CHECK(run, Events.Count == 0U);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 5.0);

        // Dragged from near its edge, it moves as far as the pointer does, without a jump.
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0) + 8.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_MovePointer(&Fixture, SliderPointerFor(6.0) + 8.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 6.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed, 6.0));

        // Dragged away and put back exactly by code before the release: it ended where it began, so no commit.
        double Began = GetSliderValue(run, &Fixture, Id);
        size_t Count = Events.Count;
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, SliderPointerFor(7.0) + 8.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == (Count + 1U));
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, Began));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == (Count + 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void AStepKeepsTheValueOnWholeSteps(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 3.3, true, 2.0);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 4.0); // Snapped when created.

        TestUI_MovePointer(&Fixture, SliderPointerFor(6.4), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
        // A move inside the same step posts nothing.
        size_t Count = Events.Count;
        TestUI_MovePointer(&Fixture, SliderPointerFor(6.8), 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == Count);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        TEST_CHECK_SUCCESS(run, UISlider_SetStep(&Fixture.Screen, Id, true, 3.0));
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, 7.0));
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
        TEST_CHECK_SUCCESS(run, UISlider_SetStep(&Fixture.Screen, Id, false, 0.0));
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, 7.0));
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ArrowsStepHomeAndEndJumpAndTheKeysReleaseCommits(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id)); // Its keys are its own once it is entered.
            TestUI_Press(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
            TEST_CHECK(run, (Events.Count == 1U) && IsDoubleEvent(&Events, 0U, Id, UIElementEvent_Changed, 6.0));
            TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_Right));

            // Repeats follow, each a step, each a Changed; the release commits the last value.
            TestUI_Repeat(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TestUI_Repeat(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 8.0);
            TEST_CHECK(run, Events.Count == 3U);
            TestUI_Release(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 4U) && IsDoubleEvent(&Events, 3U, Id, UIElementEvent_Committed, 8.0));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id); // The arrows did not move the focus.

            TestUI_Tap(run, &Fixture, GameKey_Left);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
            TEST_CHECK(run, Events.Count == 6U);

            // Home and End jump; their repeats do nothing more, their release commits.
            TestUI_Press(&Fixture, GameKey_End);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 10.0);
            TestUI_Repeat(&Fixture, GameKey_End);
            TestUI_Tick(run, &Fixture);
            TestUI_Release(&Fixture, GameKey_End);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 8U) && IsDoubleEvent(&Events, 7U, Id, UIElementEvent_Committed, 10.0));
            TestUI_Tap(run, &Fixture, GameKey_Home);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 0.0);

            // At the end already: nothing moves, nothing is committed.
            size_t Count = Events.Count;
            TestUI_Tap(run, &Fixture, GameKey_Home);
            TEST_CHECK(run, Events.Count == Count);

            // Up steps higher and down lower, as right and left do: all four arrows are the entered slider's. A key of no meaning
            // is left alone.
            TestUI_Tap(run, &Fixture, GameKey_Up);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 1.0);
            TestUI_Tap(run, &Fixture, GameKey_Down);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 0.0);
            TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Id);
            TestUI_Press(&Fixture, GameKey_A);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GameInput_IsKeyDown(&Fixture.Input, GameKey_A));
            TestUI_Release(&Fixture, GameKey_A);
            TestUI_Tick(run, &Fixture);

            // Without a step the arrows move a hundredth of the range.
            TEST_CHECK_SUCCESS(run, UISlider_SetStep(&Fixture.Screen, Id, false, 0.0));
            TestUI_Tap(run, &Fixture, GameKey_Right);
            TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 0.1));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The ui_right action's keys step: rebound, they follow. */
static void TheStepActionsKeysStep(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id)); // Its keys are its own once it is entered.
            GameInputBinding Binding = { .Key = GameKey_L, .Modifiers = 0U };
            TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UIRight, &Binding, 1U));
            TestUI_Tap(run, &Fixture, GameKey_L);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
            TestUI_Tap(run, &Fixture, GameKey_Right); // No longer bound: left to navigation.
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapeDuringADragPutsTheFirstValueBack(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, SliderPointerFor(8.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 8.0));
        size_t Count = Events.Count;

        TestUI_Press(&Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 2.0);
        TEST_CHECK(run, (Events.Count == (Count + 1U)) && IsDoubleEvent(&Events, Count, Id, UIElementEvent_Cancelled, 2.0));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id); // It used the Escape: its focus stays, and it is still entered.
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Id);
        TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_Escape));
        TestUI_Release(&Fixture, GameKey_Escape);
        TestUI_Tick(run, &Fixture);

        // The drag is over: moves and the release change nothing and report nothing.
        TestUI_MovePointer(&Fixture, SliderPointerFor(9.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 2.0);
        TEST_CHECK(run, Events.Count == (Count + 1U));

        // The press entered it, and the cancel kept it entered. Escape with nothing in progress is not the slider's: the screen
        // leaves it (it stays focused), and the next Escape clears the focus.
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Id);
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        TEST_CHECK(run, Events.Count == (Count + 1U));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void EscapeDuringAKeysHoldPutsTheFirstValueBack(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id)); // Its keys are its own once it is entered.
            TestUI_Press(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TestUI_Repeat(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
            TestUI_Tap(run, &Fixture, GameKey_Escape);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 5.0);
            TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Cancelled, 5.0));
            TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.Screen) == Id); // It used that Escape: still entered.
            size_t Count = Events.Count;
            TestUI_Release(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == Count); // Its release commits nothing.
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The wheel changes it only while entered, so scrolling a panel never changes a value by accident: hovered or only focused, the
 * wheel is not its. */
static void TheWheelChangesItOnlyWhileEntered(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        TestUI_MovePointer(&Fixture, 150.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 5.0);
        TEST_CHECK(run, Events.Count == 0U);

        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Base));
            TestUI_Wheel(&Fixture, 1.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (GetSliderValue(run, &Fixture, Id) == 5.0) && (Events.Count == 0U));

            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
            TestUI_Wheel(&Fixture, 1.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
            // A notch is a whole change: both events at once.
            TEST_CHECK(run, (Events.Count == 2U) && IsDoubleEvent(&Events, 0U, Id, UIElementEvent_Changed, 6.0)
                && IsDoubleEvent(&Events, 1U, Id, UIElementEvent_Committed, 6.0));
            TestUI_Wheel(&Fixture, -2.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 4.0);
            TEST_CHECK(run, Events.Count == 4U);

            // At the end: nothing changes, nothing is reported.
            TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, 10.0));
            TestUI_Wheel(&Fixture, 1.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 4U);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void BadValuesAndRangesAreRefused(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, false, 0.0);
        TEST_CHECK_ERROR(run, UISlider_SetValue(&Fixture.Screen, Id, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetValue(&Fixture.Screen, Id, INFINITY), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetValue(&Fixture.Screen, Id, -INFINITY), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 5.0);

        // A value past the ends is clamped; code posts nothing.
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, 99.0));
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 10.0);
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, -99.0));
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 0.0);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);

        TEST_CHECK_ERROR(run, UISlider_SetRange(&Fixture.Screen, Id, 5.0, 5.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetRange(&Fixture.Screen, Id, 6.0, 5.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetRange(&Fixture.Screen, Id, NAN, 5.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetRange(&Fixture.Screen, Id, 0.0, INFINITY), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetRange(&Fixture.Screen, Id, -1e308, 1e308), ErrorCode_ArgumentOutOfRange); // its span is infinite
        TEST_CHECK_SUCCESS(run, UISlider_SetRange(&Fixture.Screen, Id, 20.0, 30.0));
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 20.0);

        TEST_CHECK_ERROR(run, UISlider_SetStep(&Fixture.Screen, Id, true, 0.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetStep(&Fixture.Screen, Id, true, -1.0), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetStep(&Fixture.Screen, Id, true, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetStep(&Fixture.Screen, Id, true, INFINITY), ErrorCode_ArgumentOutOfRange);

        double Value = 0.0;
        TEST_CHECK_ERROR(run, UISlider_SetValue(&Fixture.Screen, 99U, 1.0), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UISlider_GetValue(&Fixture.Screen, 99U, &Value), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UISlider_SetRange(&Fixture.Screen, 99U, 0.0, 1.0), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UISlider_SetStep(&Fixture.Screen, 99U, false, 0.0), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UISlider_SetIsEnabled(&Fixture.Screen, 99U, true), ErrorCode_InvalidOperation);

        // Options are checked the same way, and a slider that is refused leaves nothing behind.
        size_t Live = UIWidgetFactory_GetTotalLiveWidgetCount(&Fixture.Widgets);
        uint64_t Refused = WIDGET_ID_INVALID;
        UISliderOptions Options = UISliderOptions_Create();
        Options.Value = NAN;
        TEST_CHECK_ERROR(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Refused), ErrorCode_ArgumentOutOfRange);
        Options = UISliderOptions_Create();
        Options.Maximum = Options.Minimum;
        TEST_CHECK_ERROR(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Refused), ErrorCode_ArgumentOutOfRange);
        Options = UISliderOptions_Create();
        Options.HasStep = true;
        Options.Step = 0.0;
        TEST_CHECK_ERROR(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Refused), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, (Refused == WIDGET_ID_INVALID) && (UIWidgetFactory_GetTotalLiveWidgetCount(&Fixture.Widgets) == Live));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void DisablingCancelsADragAndSwallowsInput(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 5.0));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id);
        TEST_CHECK_SUCCESS(run, UISlider_SetIsEnabled(&Fixture.Screen, Id, false));
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 2.0);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Cancelled, 2.0));
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Disabled);
        size_t Count = Events.Count;

        TestUI_MovePointer(&Fixture, SliderPointerFor(9.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, SliderPointerFor(7.0), 110.0f);
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 2.0);
        TEST_CHECK(run, Events.Count == Count);

        // Enabled again, it is whole.
        TEST_CHECK_SUCCESS(run, UISlider_SetIsEnabled(&Fixture.Screen, Id, true));
        TestUI_Click(run, &Fixture, SliderPointerFor(7.0), 110.0f);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 7.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A slider off the screen hears nothing: a drag that lost it ends when it is back and sees the button up, and commits then. */
/* A drag whose slider is taken out of the tree ends at once, with its commit, and it stays ended when the slider comes back. */
static void ADetachedDragCommitsAtOnce(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            size_t Count = Events.Count;
            TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Screen, Base));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == (Count + 1U)) && IsDoubleEvent(&Events, Count, Id, UIElementEvent_Committed, 5.0));
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Ticks(run, &Fixture, 3U);
            TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.Screen, Base));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
            TEST_CHECK(run, Events.Count == (Count + 1U));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void OnlyTheLeftButtonDrags(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 2.0);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);

        // Another button coming and going during a left drag does not end it.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 5.0));
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 1U);
        TestUI_MovePointer(&Fixture, SliderPointerFor(8.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 8.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 3U) && IsDoubleEvent(&Events, 2U, Id, UIElementEvent_Committed, 8.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* While one change is going on, nothing else moves the value. */
static void NothingElseMovesASliderDuringAChange(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(8.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 8.0);
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 8.0);
        TestUI_Tap(run, &Fixture, GameKey_Left);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 8.0);
        TestUI_Tap(run, &Fixture, GameKey_End);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 8.0);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Id); // The arrow was the drag's, not navigation's.
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 2U) && IsDoubleEvent(&Events, 1U, Id, UIElementEvent_Committed, 8.0));

        // A press during a key's change is not a drag: the key's change goes on and ends by itself.
        TestUI_Press(&Fixture, GameKey_Left);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(2.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 3U); // No commit yet.
        TestUI_Release(&Fixture, GameKey_Left);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 4U) && IsDoubleEvent(&Events, 3U, Id, UIElementEvent_Committed, 7.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Home and End jump once, however long they are held: a value set meanwhile stays. */
static void ARepeatOfAJumpDoesNotJumpAgain(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id)); // Its keys are its own once it is entered.
            TestUI_Press(&Fixture, GameKey_Home);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 0.0);
            TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, 5.0));
            TestUI_Repeat(&Fixture, GameKey_Home);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 5.0);
            TestUI_Release(&Fixture, GameKey_Home);
            TestUI_Tick(run, &Fixture);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A key's change that lost its slider ends when the slider is back and sees the key up. */
static void ADetachedKeysChangeEndsOnceItIsBack(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id)); // Its keys are its own once it is entered.
            TestUI_Press(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Screen, Base));
            TestUI_Tick(run, &Fixture);
            TestUI_Release(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.Screen, Base));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed, 6.0));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What the slider's helpers answer when called directly, for what the screen's routing never brings to them. */
static void TheSlidersHelpersAnswerForTheirInput(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            UISliderCapability* Slider = Widget_GetCapability(Base, UIKit_GetCapabilityId(&Fixture.Kit, UIElementCapability_Slider));
            bool IsHandled = false;
            WidgetKeyboardInputArgs Press = { .Type = UIKeyboardInputType_KeyPress, .Key = GameKey_End };
            TEST_CHECK_SUCCESS(run, UISlider_SetIsEnabled(&Fixture.Screen, Id, false));
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnKeyboardInput(Slider, Base, &Press, &IsHandled));
            TEST_CHECK(run, !IsHandled && (GetSliderValue(run, &Fixture, Id) == 5.0));
            TEST_CHECK_SUCCESS(run, UISlider_SetIsEnabled(&Fixture.Screen, Id, true));

            TEST_CHECK_SUCCESS(run, UISliderCapability_OnKeyboardInput(Slider, Base, &Press, &IsHandled));
            TEST_CHECK(run, IsHandled && (GetSliderValue(run, &Fixture, Id) == 10.0));
            TEST_CHECK(run, Slider->_sessionKey == GameKey_End);
            IsHandled = false;
            WidgetKeyboardInputArgs Release = { .Type = UIKeyboardInputType_KeyRelease, .Key = GameKey_Home };
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnKeyboardInput(Slider, Base, &Release, &IsHandled));
            TEST_CHECK(run, !IsHandled && (Slider->_sessionKey == GameKey_End));
            Release.Key = GameKey_End;
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnKeyboardInput(Slider, Base, &Release, &IsHandled));
            TEST_CHECK(run, IsHandled && (Slider->_sessionKey == GameKey_Unknown));

            // A move of another button during a drag is not the drag's.
            TestUI_MovePointer(&Fixture, SliderPointerFor(3.0), 110.0f);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 3.0);
            WidgetMouseInputArgs Move = { .Type = UIMouseInputType_Move, .Button = GameKey_MouseRight,
                .WidgetPosition = { .x = 1.0f, .y = 0.5f } };
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnMouseInput(Slider, Base, &Move));
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 3.0);
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A left press's drag past the window's edges reaches the limit, in every direction and at any speed, through the window's own
 * pointer: the drag follows the mouse's position as it is, and the release still ends it. */
static void ADragPastTheWindowReachesTheLimit(TestRun* run)
{
    const struct { float X; float Y; double Expected; } Cases[] =
    {
        { -300.0f, 110.0f, 0.0 }, { 1400.0f, 110.0f, 10.0 }, { -1.0f, 110.0f, 0.0 }, { 1001.0f, 110.0f, 10.0 },
        { -5000.0f, -5000.0f, 0.0 }, { 9000.0f, 9000.0f, 10.0 }, { 2000.0f, -40.0f, 10.0 }, { -2000.0f, 540.0f, 0.0 },
        { SLIDER_THUMB_START + (SLIDER_TRAVEL * 0.25f), -40.0f, 2.5 }, { SLIDER_THUMB_START + (SLIDER_TRAVEL * 0.25f), 800.0f, 2.5 }
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        TestUIFixture Fixture;
        TestUIEvents Events;
        if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
        {
            uint64_t Id = AddSlider(run, &Fixture, 5.0, false, 0.0);
            GameInput* Input = &Fixture.Input;
            Fixture.Sample.WindowSize = (Vector2){ .x = TEST_UI_SURFACE_WIDTH, .y = TEST_UI_SURFACE_HEIGHT };
            const Vector2 Path[] = { { SliderPointerFor(5.0), 110.0f }, { SliderPointerFor(5.0), 110.0f }, { Cases[Index].X, Cases[Index].Y } };
            for (size_t Step = 0U; Step < (sizeof(Path) / sizeof(Path[0])); Step++)
            {
                Fixture.Sample.MousePosition = Path[Step];
                if (Step == 1U)
                {
                    TestUI_Press(&Fixture, GameKey_MouseLeft);
                }
                GameInput_BeginTick(Input, &Fixture.Sample);
                Fixture.ScreenInput = UIScreenInput_CreateFromWindow(Input);
                TestUI_Tick(run, &Fixture);
            }
            // Exactly at a limit, past it: there is no rounding to forgive.
            double Value = GetSliderValue(run, &Fixture, Id);
            TEST_CHECK(run, (Cases[Index].Expected == 2.5) ? IsNearDouble(Value, 2.5) : (Value == Cases[Index].Expected));
            TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);

            // The release comes off the window: it ends the change.
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            GameInput_BeginTick(Input, &Fixture.Sample);
            Fixture.ScreenInput = UIScreenInput_CreateFromWindow(Input);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed, Cases[Index].Expected));
            TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
        }
        TestUI_Deconstruct(run, &Fixture);
    }
}

/* Coming back onto the window the slider follows again from where the pointer is, and a stepped one keeps to its steps past the ends. */
static void ADragComesBackFromPastTheWindow(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 3.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(6.0), 110.0f);
        Fixture.ScreenInput.IsPointerTracked = true;
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
        Fixture.ScreenInput.IsPointerPresent = false;
        Fixture.ScreenInput.PointerPosition = (Vector2){ .x = 4.0f, .y = 0.2f };
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 10.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(3.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 3.0);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Each click mode: what a press and a release on it and off it do. */
static void TheClickModesActivateWhenTheySay(TestRun* run)
{
    const struct { UIClickMode Mode; bool ByPress; bool ByReleaseOn; bool ByPressOffRelease; bool ByReleaseOnlyOn; } Cases[] =
    {
        { UIClickMode_OnPress, true, true, true, false },
        { UIClickMode_OnRelease, false, true, false, true },
        { UIClickMode_OnPressAndRelease, false, true, false, false }
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        TestUIFixture Fixture;
        TestUIEvents Events;
        if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
        {
            uint64_t Id = AddButton(run, &Fixture, u8"go");
            TEST_CHECK_SUCCESS(run, UIButton_SetClickMode(&Fixture.Screen, Id, Cases[Index].Mode));

            // A press on it.
            TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);
            TEST_CHECK(run, (Events.Count == 1U) == Cases[Index].ByPress);
            // ... and its release on it.
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 1U);
            TEST_CHECK(run, IsEvent(&Events, 0U, Id, UIElementEvent_Activated));
            Events.Count = 0U;

            // A press on it, dragged off and let go.
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TestUI_MovePointer(&Fixture, 700.0f, 300.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 1U) == Cases[Index].ByPressOffRelease);
            Events.Count = 0U;

            // A press elsewhere, on a widget that takes it, and a release on it.
            UIPanelOptions Panel = UIPanelOptions_Create();
            Panel.Element.HasPlacement = true;
            Panel.Element.Placement = PlaceAt(600.0f, 200.0f, 200.0f, 200.0f);
            uint64_t PanelId = WIDGET_ID_INVALID;
            TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &PanelId));
            TestUI_Tick(run, &Fixture);
            TestUI_MovePointer(&Fixture, 700.0f, 300.0f);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
            TestUI_Tick(run, &Fixture);
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 1U) == Cases[Index].ByReleaseOnlyOn);
            Events.Count = 0U;

            // A press on nothing and a release on it: that press was the game's.
            TestUI_MovePointer(&Fixture, 950.0f, 20.0f);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 0U);

            // The keyboard activates at the key's press in every mode, and once.
            Widget* Base = NULL;
            TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base));
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Base));
            TestUI_Press(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Activated));
            TestUI_Release(&Fixture, GameKey_Enter);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 1U);
        }
        TestUI_Deconstruct(run, &Fixture);
    }
}

/* Modes and buttons from the options, and what is no mode or no button is refused. */
static void ClickSettingsAreChecked(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIScreen* Screen = &Fixture.Screen;
        UIButtonOptions Options = UIButtonOptions_Create(u8"go");
        TEST_CHECK(run, (Options.ClickMode == UIClickMode_OnPressAndRelease) && (Options.ActivationButtons == UI_BUTTON_BIT(GameKey_MouseLeft)) && !Options.TakesWheel);
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
        uint64_t Id = WIDGET_ID_INVALID;
        Options.ClickMode = (UIClickMode)7;
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Options.ClickMode = UIClickMode_OnPress;
        Options.ActivationButtons = 0x80U;
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, Screen, WIDGET_ID_INVALID, &Options, &Id), ErrorCode_ArgumentOutOfRange);
        Options.ActivationButtons = UI_BUTTON_BIT(GameKey_MouseRight);
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_ERROR(run, UIButton_SetClickMode(Screen, Id, (UIClickMode)-1), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIButton_SetClickMode(Screen, Id, (UIClickMode)3), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIButton_SetActivationButtons(Screen, Id, 0xFFU), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIButton_SetClickMode(Screen, WIDGET_ID_INVALID, UIClickMode_OnPress), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_SetActivationButtons(Screen, WIDGET_ID_INVALID, UI_BUTTONS_LEFT), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_SetTakesWheel(Screen, WIDGET_ID_INVALID, true), ErrorCode_InvalidOperation);

        // What the options said holds until changed: the right button at its press, not the left.
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tap(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 1U);
        TEST_CHECK(run, Events.Events[0].HasMouse && (Events.Events[0].Mouse.Button == GameKey_MouseRight));
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);

        // Every button; none leaves the keyboard alone.
        Events.Count = 0U;
        TEST_CHECK_SUCCESS(run, UIButton_SetActivationButtons(Screen, Id, UI_BUTTONS_ALL));
        for (int32_t Button = (int32_t)GameKey_MouseLeft; Button <= (int32_t)GameKey_MouseBack; Button++)
        {
            TestUI_Tap(run, &Fixture, (GameKey)Button);
        }
        TEST_CHECK(run, Events.Count == ((size_t)GameKey_MouseBack - (size_t)GameKey_MouseLeft + 1U));
        Events.Count = 0U;
        TEST_CHECK_SUCCESS(run, UIButton_SetActivationButtons(Screen, Id, 0U));
        TestUI_Tap(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Tap(run, &Fixture, GameKey_Space);
        TEST_CHECK(run, Events.Count == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A press on it looks pressed for the buttons that activate it, and a held press of another does not end that; each button is its own. */
static void EachActivationButtonIsItsOwn(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TEST_CHECK_SUCCESS(run, UIButton_SetActivationButtons(&Fixture.Screen, Id, UI_BUTTON_BIT(GameKey_MouseLeft) | UI_BUTTON_BIT(GameKey_MouseMiddle)));
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 1U) && (Events.Events[0].Mouse.Button == GameKey_MouseMiddle));
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed); // The left one still holds it.
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 2U) && (Events.Events[1].Mouse.Button == GameKey_MouseLeft));
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Hovered);

        // Off it and back during two presses: a move of one tells it for both.
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 700.0f, 300.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);

        // Cancelling keeps the mode and the buttons; the press in progress is forgotten.
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, false));
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, true));
        Events.Count = 0U;
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Tap(run, &Fixture, GameKey_MouseMiddle);
        TEST_CHECK(run, Events.Count == 1U);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Hidden or disabled during a press in each mode: the press is over, and nothing activates by it. A disabled one swallows all. */
static void APressLostOrDisabledActivatesNothing(TestRun* run)
{
    const UIClickMode Modes[] = { UIClickMode_OnRelease, UIClickMode_OnPressAndRelease };
    for (size_t ModeIndex = 0U; ModeIndex < (sizeof(Modes) / sizeof(Modes[0])); ModeIndex++)
    {
        for (int Case = 0; Case < 3; Case++)
        {
            TestUIFixture Fixture;
            TestUIEvents Events;
            if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
            {
                uint64_t Id = AddButton(run, &Fixture, u8"go");
                Events.IsRecordingInputEvents = true;
                TEST_CHECK_SUCCESS(run, UIButton_SetClickMode(&Fixture.Screen, Id, Modes[ModeIndex]));
                Widget* Base = NULL;
                if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
                {
                    TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
                    TestUI_Press(&Fixture, GameKey_MouseLeft);
                    TestUI_Tick(run, &Fixture);
                    TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);
                    Events.Count = 0U;
                    switch (Case)
                    {
                        case 0: TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Base, false)); break;
                        case 1: TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, false)); break;
                        default: TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, false)); break;
                    }
                    TestUI_Tick(run, &Fixture);
                    TestUI_Release(&Fixture, GameKey_MouseLeft);
                    TestUI_Tick(run, &Fixture);
                    TEST_CHECK(run, Events.Count == 0U);
                    switch (Case)
                    {
                        case 0: TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Base, true)); break;
                        case 1: TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, true)); break;
                        default: TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, true)); break;
                    }
                    TestUI_Tick(run, &Fixture);
                    TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
                }
            }
            TestUI_Deconstruct(run, &Fixture);
        }
    }
}

/* A disabled element reports nothing, at any button, in any mode, and still swallows the press from what lies behind it. */
static void ADisabledElementReportsNothing(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Events.IsRecordingInputEvents = true;
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TEST_CHECK_SUCCESS(run, UIButton_SetClickMode(&Fixture.Screen, Id, UIClickMode_OnPress));
        TEST_CHECK_SUCCESS(run, UIButton_SetTakesWheel(&Fixture.Screen, Id, true));
        TEST_CHECK_SUCCESS(run, UIButton_SetActivationButtons(&Fixture.Screen, Id, UI_BUTTONS_ALL));
        TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, false));
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        for (int32_t Button = (int32_t)GameKey_MouseLeft; Button <= (int32_t)GameKey_MouseBack; Button++)
        {
            TestUI_Press(&Fixture, (GameKey)Button);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, (GameKey)Button));
            TestUI_Release(&Fixture, (GameKey)Button);
            TestUI_Tick(run, &Fixture);
        }
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
        TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Disabled);

        // The menu key coming up from a focused child passes a disabled button or toggle by: neither asks, nor uses it.
        uint64_t ToggleId = AddToggle(run, &Fixture, UIToggleState_Off);
        TEST_CHECK_SUCCESS(run, UIToggle_SetIsEnabled(&Fixture.Screen, ToggleId, false));
        const uint64_t Holders[] = { Id, ToggleId };
        for (size_t Index = 0U; Index < (sizeof(Holders) / sizeof(Holders[0])); Index++)
        {
            uint64_t ChildId = WIDGET_ID_INVALID;
            UIPanelOptions ChildOptions = UIPanelOptions_Create();
            Widget* Child = NULL;
            TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, Holders[Index], &ChildOptions, &ChildId));
            if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, ChildId, &Child)))
            {
                TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(Child, true));
                TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Child));
                TestUI_Tick(run, &Fixture);
                TestUI_Press(&Fixture, GameKey_Menu);
                TestUI_Tick(run, &Fixture);
                TEST_CHECK(run, (Events.Count == 0U) && !GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Menu));
                TestUI_Release(&Fixture, GameKey_Menu);
                TestUI_Tick(run, &Fixture);
                TEST_CHECK_SUCCESS(run, Widget_Destroy(Child));
            }
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What a press held ends with its release, whatever changed since: taking its button away from the activating ones leaves no element pressed, and activates nothing. */
static void ChangingTheButtonsDuringAPressLeavesNoStuckPress(TestRun* run)
{
    for (int Kind = 0; Kind < 2; Kind++)
    {
        TestUIFixture Fixture;
        TestUIEvents Events;
        if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
        {
            uint64_t Id = (Kind == 0) ? AddButton(run, &Fixture, u8"go") : AddToggle(run, &Fixture, UIToggleState_Off);
            TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
            TestUI_Tick(run, &Fixture);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);
            if (Kind == 0)
            {
                TEST_CHECK_SUCCESS(run, UIButton_SetActivationButtons(&Fixture.Screen, Id, UI_BUTTON_BIT(GameKey_MouseRight)));
            }
            else
            {
                TEST_CHECK_SUCCESS(run, UIToggle_SetActivationButtons(&Fixture.Screen, Id, 0U));
            }
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
            TEST_CHECK(run, Events.Count == 0U);
            TestUI_MovePointer(&Fixture, 600.0f, 400.0f);
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
        }
        TestUI_Deconstruct(run, &Fixture);
    }
}

/* A lost press is over at once: shown again with the button still down, nothing shows it pressed. */
static void ALostPressShowsNothingWhileTheButtonStaysDown(TestRun* run)
{
    for (int Kind = 0; Kind < 2; Kind++)
    {
        for (int Case = 0; Case < 2; Case++)
        {
            TestUIFixture Fixture;
            if (Construct(run, &Fixture))
            {
                uint64_t Id = (Kind == 0) ? AddButton(run, &Fixture, u8"go") : AddToggle(run, &Fixture, UIToggleState_Off);
                Widget* Base = NULL;
                if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
                {
                    TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
                    TestUI_Tick(run, &Fixture);
                    TestUI_Press(&Fixture, GameKey_MouseLeft);
                    TestUI_Tick(run, &Fixture);
                    TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);
                    TEST_CHECK_SUCCESS(run, (Case == 0) ? Widget_SetIsRendered(Base, false) : Widget_SetIsInputEnabled(Base, false));
                    TestUI_Tick(run, &Fixture);
                    TEST_CHECK_SUCCESS(run, (Case == 0) ? Widget_SetIsRendered(Base, true) : Widget_SetIsInputEnabled(Base, true));
                    TestUI_Ticks(run, &Fixture, 2U);
                    TEST_CHECK(run, GetState(&Fixture, Id) != UIElementState_Pressed);
                    TestUI_Release(&Fixture, GameKey_MouseLeft);
                    TestUI_Tick(run, &Fixture);
                }
            }
            TestUI_Deconstruct(run, &Fixture);
        }
    }
}

/* A release over an OnRelease element activates it only if that button activates it, and only while it is enabled. */
static void AReleaseOverActivatesOnlyWhatIsAsked(TestRun* run)
{
    for (int Case = 0; Case < 3; Case++)
    {
        TestUIFixture Fixture;
        TestUIEvents Events;
        if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
        {
            UIButtonOptions FromOptions = UIButtonOptions_Create(u8"from");
            FromOptions.Element.HasPlacement = true;
            FromOptions.Element.Placement = PlaceAt(100.0f, 100.0f, 100.0f, 40.0f);
            UIButtonOptions ToOptions = UIButtonOptions_Create(u8"to");
            ToOptions.Element.HasPlacement = true;
            ToOptions.Element.Placement = PlaceAt(300.0f, 100.0f, 100.0f, 40.0f);
            ToOptions.ClickMode = UIClickMode_OnRelease;
            uint64_t From = WIDGET_ID_INVALID;
            uint64_t To = WIDGET_ID_INVALID;
            TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &FromOptions, &From));
            TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ToOptions, &To));
            TestUI_Tick(run, &Fixture);
            if (Case == 2)
            {
                TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, To, false));
            }
            GameKey Button = (Case == 1) ? GameKey_MouseRight : GameKey_MouseLeft;
            TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
            TestUI_Tick(run, &Fixture);
            TestUI_Press(&Fixture, Button);
            TestUI_Tick(run, &Fixture);
            TestUI_MovePointer(&Fixture, 350.0f, 120.0f);
            TestUI_Tick(run, &Fixture);
            TestUI_Release(&Fixture, Button);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 1U) == (Case == 0));
            TEST_CHECK(run, (Case != 0) || IsEvent(&Events, 0U, To, UIElementEvent_Activated));
        }
        TestUI_Deconstruct(run, &Fixture);
    }
}

/* A press handed to an element is one that began elsewhere: it activates one that asks only for a release on it, and no other. */
static void AHandedPressActivatesOnlyAnOnReleaseElement(TestRun* run)
{
    const struct { UIClickMode Mode; bool IsActivated; } Cases[] =
    {
        { UIClickMode_OnRelease, true }, { UIClickMode_OnPressAndRelease, false }, { UIClickMode_OnPress, false }
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        for (int IsOver = 0; IsOver < 2; IsOver++)
        {
            TestUIFixture Fixture;
            TestUIEvents Events;
            if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
            {
                UIButtonOptions FromOptions = UIButtonOptions_Create(u8"from");
                FromOptions.Element.HasPlacement = true;
                FromOptions.Element.Placement = PlaceAt(100.0f, 100.0f, 100.0f, 40.0f);
                UIButtonOptions ToOptions = UIButtonOptions_Create(u8"to");
                ToOptions.Element.HasPlacement = true;
                ToOptions.Element.Placement = PlaceAt(300.0f, 100.0f, 100.0f, 40.0f);
                ToOptions.ClickMode = Cases[Index].Mode;
                uint64_t From = WIDGET_ID_INVALID;
                uint64_t To = WIDGET_ID_INVALID;
                TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &FromOptions, &From));
                TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &ToOptions, &To));
                TestUI_Tick(run, &Fixture);
                TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
                TestUI_Tick(run, &Fixture);
                TestUI_Press(&Fixture, GameKey_MouseLeft);
                TestUI_Tick(run, &Fixture);
                TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(&Fixture.Screen, GameKey_MouseLeft, To));
                TestUI_MovePointer(&Fixture, (IsOver != 0) ? 350.0f : 700.0f, 120.0f);
                TestUI_Tick(run, &Fixture);
                TestUI_Release(&Fixture, GameKey_MouseLeft);
                TestUI_Tick(run, &Fixture);
                bool IsExpected = Cases[Index].IsActivated && (IsOver != 0);
                TEST_CHECK(run, (Events.Count == 1U) == IsExpected);
                TEST_CHECK(run, !IsExpected || IsEvent(&Events, 0U, To, UIElementEvent_Activated));
            }
            TestUI_Deconstruct(run, &Fixture);
        }
    }
}

/* A widget type that embeds an element's capability and does not answer OnPressLost does not stay pressed: a button that is up holds nothing. */
static void AnElementWithoutTheLossHookStillLetsGo(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        // The press, as such a type has it once its press was lost: held, with nothing more to come.
        UIPressable Press = UIPressable_Create();
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            WidgetMouseInputArgs Args = { .Type = UIMouseInputType_ButtonPress, .Button = GameKey_MouseMiddle };
            bool IsActivated = false;
            Press._activationButtons = UI_BUTTONS_ALL;
            UIPressable_OnMouseInput(&Press, Base, &Args, true, &IsActivated);
            Args.Button = GameKey_MouseRight;
            UIPressable_OnMouseInput(&Press, Base, &Args, true, &IsActivated);
            TEST_CHECK(run, UIPressable_IsPressed(&Press));

            // A release ends what its own press held at once, with no update to help.
            Args.Type = UIMouseInputType_ButtonRelease;
            UIPressable_OnMouseInput(&Press, Base, &Args, true, &IsActivated);
            Args.Button = GameKey_MouseMiddle;
            UIPressable_OnMouseInput(&Press, Base, &Args, true, &IsActivated);
            TEST_CHECK(run, !UIPressable_IsPressed(&Press));
            Args.Type = UIMouseInputType_ButtonPress;
            UIPressable_OnMouseInput(&Press, Base, &Args, true, &IsActivated);
            Args.Button = GameKey_MouseRight;
            UIPressable_OnMouseInput(&Press, Base, &Args, true, &IsActivated);
            TEST_CHECK(run, UIPressable_IsPressed(&Press));

            // Inside an update, as an element's own Update is: the one up goes, the one down stays.
            Fixture.Sample.KeyStates[GameKey_MouseRight] = GAME_INPUT_KEY_STATE_DOWN;
            GameInput_BeginTick(&Fixture.Input, &Fixture.Sample);
            Fixture.Screen._input = &Fixture.Input;
            UIPressable_Update(&Press, &Fixture.Screen);
            TEST_CHECK(run, UIPressable_IsPressed(&Press));
            TEST_CHECK(run, (Press._heldButtons & UI_BUTTON_BIT(GameKey_MouseMiddle)) == 0U);
            Fixture.Sample.KeyStates[GameKey_MouseRight] = 0U;
            GameInput_BeginTick(&Fixture.Input, &Fixture.Sample);
            UIPressable_Update(&Press, &Fixture.Screen);
            TEST_CHECK(run, !UIPressable_IsPressed(&Press));
            Fixture.Screen._input = NULL;
        }

        // A slider's drag that heard no end ends the same way, with its commit.
        uint64_t SliderId = AddSlider(run, &Fixture, 2.0, false, 0.0);
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, SliderId, &Base)))
        {
            UISliderCapability* Slider = Widget_GetCapability(Base, UIKit_GetCapabilityId(&Fixture.Kit, UIElementCapability_Slider));
            Slider->_isDragging = true;
            Slider->_sessionStartValue = 7.0;
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, !Slider->_isDragging && (Events.Count == 1U) && IsDoubleEvent(&Events, 0U, SliderId, UIElementEvent_Committed, 2.0));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A toggle has the click modes and the same buttons; a whole change at the moment its mode says. */
static void AToggleTakesTheClickModes(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddToggle(run, &Fixture, UIToggleState_Off);
        UIToggleState State = UIToggleState_Off;
        TEST_CHECK_ERROR(run, UIToggle_SetClickMode(&Fixture.Screen, Id, (UIClickMode)9), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIToggle_SetActivationButtons(&Fixture.Screen, Id, 0x80U), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIToggle_SetClickMode(&Fixture.Screen, WIDGET_ID_INVALID, UIClickMode_OnPress), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIToggle_SetActivationButtons(&Fixture.Screen, WIDGET_ID_INVALID, UI_BUTTONS_LEFT), ErrorCode_InvalidOperation);

        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TEST_CHECK_SUCCESS(run, UIToggle_SetClickMode(&Fixture.Screen, Id, UIClickMode_OnPress));
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 2U) && IsBoolEvent(&Events, 0U, Id, UIElementEvent_Changed, true) && IsBoolEvent(&Events, 1U, Id, UIElementEvent_Committed, true));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 2U);

        // Its options say the same.
        UIToggleOptions MadeWith = UIToggleOptions_Create(u8"own");
        MadeWith.ClickMode = UIClickMode_OnPress;
        MadeWith.ActivationButtons = UI_BUTTON_BIT(GameKey_MouseMiddle);
        MadeWith.Element.HasPlacement = true;
        MadeWith.Element.Placement = PlaceAt(500.0f, 100.0f, 50.0f, 24.0f);
        uint64_t Made = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &MadeWith, &Made));
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 510.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        Events.Count = 0U;
        TestUI_Tap(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 2U) && IsBoolEvent(&Events, 1U, Made, UIElementEvent_Committed, true));
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 2U);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);

        // The right button only: the left one does nothing now.
        Events.Count = 0U;
        TEST_CHECK_SUCCESS(run, UIToggle_SetClickMode(&Fixture.Screen, Id, UIClickMode_OnRelease));
        TEST_CHECK_SUCCESS(run, UIToggle_SetActivationButtons(&Fixture.Screen, Id, UI_BUTTON_BIT(GameKey_MouseRight)));
        TestUI_Tap(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, Events.Count == 0U);
        TestUI_Tap(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, Events.Count == 2U);
        TEST_CHECK_SUCCESS(run, UIToggle_GetState(&Fixture.Screen, Id, &State));
        TEST_CHECK(run, State == UIToggleState_Off);

        // The press lost during a hold does nothing at its release.
        Events.Count = 0U;
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TestUI_Press(&Fixture, GameKey_MouseRight);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetState(&Fixture, Id) == UIElementState_Pressed);
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, false));
            TestUI_Release(&Fixture, GameKey_MouseRight);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, true));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 0U) && (GetState(&Fixture, Id) != UIElementState_Pressed));
        }

        // Its options.
        UIToggleOptions Options = UIToggleOptions_Create(u8"x");
        TEST_CHECK(run, (Options.ClickMode == UIClickMode_OnPressAndRelease) && (Options.ActivationButtons == UI_BUTTONS_LEFT));
        uint64_t Other = WIDGET_ID_INVALID;
        Options.ActivationButtons = 0x80U;
        TEST_CHECK_ERROR(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Other), ErrorCode_ArgumentOutOfRange);
        Options.ActivationButtons = UI_BUTTONS_LEFT;
        Options.ClickMode = (UIClickMode)5;
        TEST_CHECK_ERROR(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Other), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* What every element tells: the press, the release with whether it is over it, a double click, and what they carry by value, so a handler
 * needs no widget. */
static void AnElementTellsItsMouseEvents(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Events.IsRecordingInputEvents = true;
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        Fixture.Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
        TestUI_Press(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Pressed));
        const UIEvent* Pressed = &Events.Events[0];
        TEST_CHECK(run, Pressed->HasMouse && !Pressed->HasValue && (Pressed->Mouse.Button == GameKey_MouseMiddle) && (Pressed->Mouse.Modifiers == GAME_KEY_MODIFIER_SHIFT));
        TEST_CHECK(run, (fabsf(Pressed->Mouse.ScreenPosition.x - 0.15f) < 1e-4f) && (fabsf(Pressed->Mouse.ScreenPosition.y - 0.24f) < 1e-4f) && Pressed->Mouse.IsOverWidget);
        TEST_CHECK(run, (Pressed->Mouse.ScrollDelta.x == 0.0f) && (Pressed->Mouse.ScrollDelta.y == 0.0f));
        Fixture.Sample.KeyStates[GameKey_LeftShift] = 0U;

        // The middle button activates nothing here, and it comes up off the button: the release says so.
        TestUI_MovePointer(&Fixture, 700.0f, 300.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_MouseMiddle);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 2U) && IsEvent(&Events, 1U, Id, UIElementEvent_Released));
        TEST_CHECK(run, Events.Events[1].HasMouse && !Events.Events[1].Mouse.IsOverWidget && (Events.Events[1].Mouse.Button == GameKey_MouseMiddle));

        // A click: pressed, then released and activated, over it. Quickly again: a double click right after the press.
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, (Events.Count == 3U) && IsEvent(&Events, 0U, Id, UIElementEvent_Pressed) && IsEvent(&Events, 1U, Id, UIElementEvent_Released)
            && IsEvent(&Events, 2U, Id, UIElementEvent_Activated));
        TEST_CHECK(run, Events.Events[1].Mouse.IsOverWidget && Events.Events[2].HasMouse && (Events.Events[2].Mouse.Button == GameKey_MouseLeft) && Events.Events[2].Mouse.IsOverWidget);
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, (Events.Count == 4U) && IsEvent(&Events, 0U, Id, UIElementEvent_Pressed) && IsEvent(&Events, 1U, Id, UIElementEvent_DoubleClicked)
            && (Events.Events[1].Mouse.Button == GameKey_MouseLeft));
        // A third in the row is no second double click.
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, Events.Count == 3U);

        // A release over it of a press elsewhere is told too.
        UIPanelOptions Panel = UIPanelOptions_Create();
        Panel.Element.HasPlacement = true;
        Panel.Element.Placement = PlaceAt(600.0f, 200.0f, 200.0f, 200.0f);
        uint64_t PanelId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Panel, &PanelId));
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 700.0f, 300.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        Events.Count = 0U;
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Released) && Events.Events[0].Mouse.IsOverWidget);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A right press and release on it asks for its context menu, at the pointer; off it, or by the keyboard's key when focused, as its rules say. */
static void AnElementAsksForItsContextMenu(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Events.IsRecordingInputEvents = true;
        uint64_t ButtonId = AddButton(run, &Fixture, u8"go");
        uint64_t SliderId = AddSlider(run, &Fixture, 5.0, false, 0.0);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        // The slider lies over the button's place in this fixture: the pointer finds it first.
        uint64_t Id = SliderId;
        TestUI_Tap(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, (Events.Count == 3U) && IsEvent(&Events, 0U, Id, UIElementEvent_Pressed) && IsEvent(&Events, 1U, Id, UIElementEvent_Released)
            && IsEvent(&Events, 2U, Id, UIElementEvent_ContextRequested));
        TEST_CHECK(run, Events.Events[2].HasMouse && (fabsf(Events.Events[2].Mouse.ScreenPosition.x - 0.15f) < 1e-4f) && Events.Events[2].Mouse.IsOverWidget);

        // Dragged off: no menu.
        Events.Count = 0U;
        Fixture.Time += 1.0; // No double click.
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 700.0f, 300.0f);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 2U) && IsEvent(&Events, 1U, Id, UIElementEvent_Released) && !Events.Events[1].Mouse.IsOverWidget);

        // Pressed elsewhere and let go over it: no menu either.
        Events.Count = 0U;
        TestUI_Press(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Release(&Fixture, GameKey_MouseRight);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);

        // Another button is no menu.
        TestUI_Tap(run, &Fixture, GameKey_MouseMiddle);
        TEST_CHECK(run, (Events.Count == 2U) && IsEvent(&Events, 1U, Id, UIElementEvent_Released));

        // The menu key on what is focused: it tells the middle of the widget, handled at its press.
        Widget* Slider = NULL;
        Widget* Button = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, SliderId, &Slider) && UIScreen_TryGetWidgetById(&Fixture.Screen, ButtonId, &Button)))
        {
            TestUI_RemovePointer(&Fixture);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Slider));
            Events.Count = 0U;
            TestUI_Press(&Fixture, GameKey_Menu);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, SliderId, UIElementEvent_ContextRequested) && GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Menu));
            TEST_CHECK(run, Events.Events[0].HasMouse && (Events.Events[0].Mouse.Button == GameKey_Menu) && !Events.Events[0].Mouse.IsOverWidget);
            TEST_CHECK(run, fabsf(Events.Events[0].Mouse.ScreenPosition.x - (SLIDER_X + (SLIDER_WIDTH * 0.5f)) / 1000.0f) < 1e-3f);
            TestUI_Repeat(&Fixture, GameKey_Menu);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 1U);
            TestUI_Release(&Fixture, GameKey_Menu);
            TestUI_Tick(run, &Fixture);

            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Button));
            Events.Count = 0U;
            TestUI_Tap(run, &Fixture, GameKey_Menu);
            TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, ButtonId, UIElementEvent_ContextRequested));

            // The action's key, rebound: the old key does nothing, and with its modifier only.
            GameInputBinding Rebound = { .Key = GameKey_M, .Modifiers = GAME_KEY_MODIFIER_CONTROL };
            TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UIContextMenu, &Rebound, 1U));
            Events.Count = 0U;
            TestUI_Tap(run, &Fixture, GameKey_Menu);
            TestUI_Tap(run, &Fixture, GameKey_M);
            TEST_CHECK(run, Events.Count == 0U);
            Fixture.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
            TestUI_Tap(run, &Fixture, GameKey_M);
            TEST_CHECK(run, (Events.Count == 1U) && (Events.Events[0].Mouse.Modifiers == GAME_KEY_MODIFIER_CONTROL));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A toggle and a slider tell the same. */
static void EveryElementTellsTheSameMouseEvents(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Events.IsRecordingInputEvents = true;
        uint64_t Toggle = AddToggle(run, &Fixture, UIToggleState_Off);
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tap(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, (Events.Count == 3U) && IsEvent(&Events, 0U, Toggle, UIElementEvent_Pressed) && IsEvent(&Events, 2U, Toggle, UIElementEvent_ContextRequested));
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, (Events.Count == 4U) && IsEvent(&Events, 0U, Toggle, UIElementEvent_Pressed) && IsEvent(&Events, 1U, Toggle, UIElementEvent_Released)
            && IsEvent(&Events, 2U, Toggle, UIElementEvent_Changed) && IsEvent(&Events, 3U, Toggle, UIElementEvent_Committed));
        Events.Count = 0U;
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, IsEvent(&Events, 1U, Toggle, UIElementEvent_DoubleClicked));
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Toggle, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Base));
            Events.Count = 0U;
            TestUI_Tap(run, &Fixture, GameKey_Menu);
            TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Toggle, UIElementEvent_ContextRequested));
            TEST_CHECK_SUCCESS(run, UIToggle_SetIsEnabled(&Fixture.Screen, Toggle, false));
            Events.Count = 0U;
            TestUI_Tap(run, &Fixture, GameKey_MouseRight);
            TEST_CHECK(run, Events.Count == 0U);
            TEST_CHECK_SUCCESS(run, Widget_Destroy(Base));
        }

        uint64_t Slider = AddSlider(run, &Fixture, 5.0, false, 0.0);
        Events.Count = 0U;
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Events.Count == 2U) && IsEvent(&Events, 0U, Slider, UIElementEvent_Pressed) && IsEvent(&Events, 1U, Slider, UIElementEvent_Released));
        TEST_CHECK_SUCCESS(run, UISlider_SetIsEnabled(&Fixture.Screen, Slider, false));
        Events.Count = 0U;
        TestUI_Tap(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, Events.Count == 0U);
        TEST_CHECK_SUCCESS(run, UISlider_SetIsEnabled(&Fixture.Screen, Slider, true));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A button made to take the wheel posts it, with the delta and the modifiers, and stops it; otherwise it goes on up the tree. */
static void AButtonTakesTheWheelOnlyWhenMadeTo(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        Events.IsRecordingInputEvents = true;
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_MovePointer(&Fixture, 150.0f, 120.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Wheel(&Fixture, 2.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, Events.Count == 0U);
        TEST_CHECK(run, GameInput_GetWheelDelta(&Fixture.Input).y == 0.0f); // Still the UI's: a widget is under the pointer.

        // It says so to what holds it: without taking the wheel it leaves it to them, taking it it stops it, enabled or not.
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            UIButtonCapability* Button = Widget_GetCapability(Base, UIKit_GetCapabilityId(&Fixture.Kit, UIElementCapability_Button));
            WidgetScrollInputArgs Wheel = { .ScrollDelta = { .x = 0.0f, .y = 1.0f } };
            bool IsHandled = false;
            TEST_CHECK_SUCCESS(run, UIButtonCapability_OnScrollInput(Button, Base, &Wheel, &IsHandled));
            TEST_CHECK(run, !IsHandled);
            TEST_CHECK_SUCCESS(run, UIButton_SetTakesWheel(&Fixture.Screen, Id, true));
            TEST_CHECK_SUCCESS(run, UIButtonCapability_OnScrollInput(Button, Base, &Wheel, &IsHandled));
            TEST_CHECK(run, IsHandled);
            Events.Count = 0U;
            TestUI_Tick(run, &Fixture);
            Events.Count = 0U;
            IsHandled = false;
            TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, false));
            TEST_CHECK_SUCCESS(run, UIButtonCapability_OnScrollInput(Button, Base, &Wheel, &IsHandled));
            TEST_CHECK(run, IsHandled);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 0U);
            TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, true));
        }

        TEST_CHECK_SUCCESS(run, UIButton_SetTakesWheel(&Fixture.Screen, Id, true));
        Fixture.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
        Fixture.Sample.WheelDelta = (Vector2){ .x = 1.0f, .y = -3.0f };
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Scrolled) && Events.Events[0].HasMouse);
        const UIEventMouse* Mouse = &Events.Events[0].Mouse;
        TEST_CHECK(run, (Mouse->ScrollDelta.x == 1.0f) && (Mouse->ScrollDelta.y == -3.0f) && (Mouse->Modifiers == GAME_KEY_MODIFIER_CONTROL));
        TEST_CHECK(run, (Mouse->Button == GameKey_Unknown) && Mouse->IsOverWidget && (fabsf(Mouse->ScreenPosition.x - 0.15f) < 1e-4f));

        // Its options say the same.
        UIButtonOptions Options = UIButtonOptions_Create(u8"own");
        Options.TakesWheel = true;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(500.0f, 100.0f, 50.0f, 24.0f);
        uint64_t Other = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Other));
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 510.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        Events.Count = 0U;
        TestUI_Wheel(&Fixture, 1.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Other, UIElementEvent_Scrolled));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The wheel over a slider: only entered or with Ctrl it is the slider's; otherwise it goes on up the tree to what holds it. */
static void ASliderTakesTheWheelWhenEnteredOrWithControl(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        TestUI_MovePointer(&Fixture, 150.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            // Plain: nothing here, and the screen consumes it anyway.
            TestUI_Wheel(&Fixture, 1.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (GetSliderValue(run, &Fixture, Id) == 5.0) && (Events.Count == 0U));

            // It says so to what holds it: left to them plain, stopped when entered or with Ctrl, in a change or disabled too.
            UISliderCapability* Slider = Widget_GetCapability(Base, UIKit_GetCapabilityId(&Fixture.Kit, UIElementCapability_Slider));
            WidgetScrollInputArgs Wheel = { .ScrollDelta = { .x = 0.0f, .y = 0.0f }, .Modifiers = 0U };
            bool IsHandled = false;
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnScrollInput(Slider, Base, &Wheel, &IsHandled));
            TEST_CHECK(run, !IsHandled);
            Wheel.Modifiers = GAME_KEY_MODIFIER_CONTROL;
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnScrollInput(Slider, Base, &Wheel, &IsHandled));
            TEST_CHECK(run, IsHandled);

            // With Ctrl, unfocused: a whole change at once, and it did not take the focus.
            Fixture.Sample.KeyStates[GameKey_RightControl] = GAME_INPUT_KEY_STATE_DOWN;
            TestUI_Wheel(&Fixture, 2.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
            TEST_CHECK(run, (Events.Count == 2U) && IsDoubleEvent(&Events, 0U, Id, UIElementEvent_Changed, 7.0) && IsDoubleEvent(&Events, 1U, Id, UIElementEvent_Committed, 7.0));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            Fixture.Sample.KeyStates[GameKey_RightControl] = 0U;

            // During a change nothing else moves it: the wheel stops there.
            Events.Count = 0U;
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id));
            TestUI_Press(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TestUI_Wheel(&Fixture, 1.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 8.0);
            TestUI_Release(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);

            // Disabled: with Ctrl it stops there and moves nothing.
            TEST_CHECK_SUCCESS(run, UISlider_SetIsEnabled(&Fixture.Screen, Id, false));
            Fixture.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
            TestUI_Wheel(&Fixture, 1.0f);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 8.0);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The reset action on a slider with a default: focused or only hovered, a whole change; without one it goes on; other keys and modifiers
 * do nothing. */
static void TheResetActionPutsASliderBack(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        UIScreen* Screen = &Fixture.Screen;
        UISliderOptions Options = UISliderOptions_Create();
        TEST_CHECK(run, !Options.HasDefault);
        Options.Maximum = 10.0;
        Options.Value = 5.0;
        Options.HasDefault = true;
        Options.Default = 2.5;
        Options.HasStep = true;
        Options.Step = 1.0;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(SLIDER_X, SLIDER_Y, SLIDER_WIDTH, SLIDER_HEIGHT);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Tick(run, &Fixture);

        // Hovered only: the value goes to the default, on its step; one change, whole; the key is used.
        TestUI_MovePointer(&Fixture, 150.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 3.0);
        TEST_CHECK(run, (Events.Count == 2U) && IsDoubleEvent(&Events, 0U, Id, UIElementEvent_Changed, 3.0) && IsDoubleEvent(&Events, 1U, Id, UIElementEvent_Committed, 3.0));
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Backspace));
        // Its repeat and release do nothing more; at the default already nothing is told, though it is used.
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(Screen, Id, 8.0));
        TestUI_Repeat(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TestUI_Release(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 8.0);
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(Screen, Id, 3.0));
        Events.Count = 0U;
        TestUI_Press(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (Events.Count == 0U) && GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Backspace));
        TestUI_Release(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);

        // Focused with the pointer elsewhere: the same.
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, Base));
            TEST_CHECK_SUCCESS(run, UISlider_SetValue(Screen, Id, 8.0));
            TestUI_RemovePointer(&Fixture);
            TestUI_Tick(run, &Fixture);

            // Another key pressed while the action's key is down is not the action. Focused but not entered it hears the reset (the
            // short list), and entered the arrows too.
            TestUI_Press(&Fixture, GameKey_Backspace);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 3.0);
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, Id));
            TEST_CHECK_SUCCESS(run, UISlider_SetValue(Screen, Id, 8.0));
            TestUI_Press(&Fixture, GameKey_Left);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
            TestUI_Release(&Fixture, GameKey_Left);
            TestUI_Release(&Fixture, GameKey_Backspace);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, UISlider_SetValue(Screen, Id, 8.0));

            TestUI_Tap(run, &Fixture, GameKey_Backspace);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 3.0);

            // During a change it does nothing, but it is used.
            TEST_CHECK_SUCCESS(run, UISlider_SetValue(Screen, Id, 8.0));
            TestUI_Press(&Fixture, GameKey_Left);
            TestUI_Tick(run, &Fixture);
            TestUI_Press(&Fixture, GameKey_Backspace);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, (GetSliderValue(run, &Fixture, Id) == 7.0) && GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Backspace));
            TestUI_Release(&Fixture, GameKey_Backspace);
            TestUI_Release(&Fixture, GameKey_Left);
            TestUI_Tick(run, &Fixture);
        }

        // The action, not the key: rebound with a modifier, a plain press is nothing and the old key is nothing.
        GameInputBinding Rebound = { .Key = GameKey_R, .Modifiers = GAME_KEY_MODIFIER_CONTROL };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UIReset, &Rebound, 1U));
        TestUI_Tap(run, &Fixture, GameKey_Backspace);
        TestUI_Tap(run, &Fixture, GameKey_R);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
        Fixture.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
        TestUI_Tap(run, &Fixture, GameKey_R);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 3.0);
        Fixture.Sample.KeyStates[GameKey_LeftControl] = 0U;
        TEST_CHECK_SUCCESS(run, GameInput_ResetActionBindings(&Fixture.Input, GameAction_UIReset));

        // No default: it goes on, to the game.
        TEST_CHECK_SUCCESS(run, UISlider_SetDefault(Screen, Id, false, 0.0));
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(Screen, Id, 8.0));
        TestUI_Press(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (GetSliderValue(run, &Fixture, Id) == 8.0) && !GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Backspace));
        TestUI_Release(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);

        // A default past the range is clamped when it is used; what is not a number is refused.
        TEST_CHECK_SUCCESS(run, UISlider_SetDefault(Screen, Id, true, 99.0));
        TestUI_Tap(run, &Fixture, GameKey_Backspace);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 10.0);
        TEST_CHECK_ERROR(run, UISlider_SetDefault(Screen, Id, true, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetDefault(Screen, Id, true, INFINITY), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UISlider_SetDefault(Screen, WIDGET_ID_INVALID, true, 1.0), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UISlider_SetDefault(Screen, Id, false, NAN)); // With no default the value means nothing.

        // Its options check it too.
        UISliderOptions Bad = UISliderOptions_Create();
        Bad.HasDefault = true;
        Bad.Default = NAN;
        uint64_t Other = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateSlider(&Fixture.Kit, Screen, WIDGET_ID_INVALID, &Bad, &Other), ErrorCode_ArgumentOutOfRange);
        Bad.HasDefault = false;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, Screen, WIDGET_ID_INVALID, &Bad, &Other));

        // Disabled: it hears nothing of it.
        TEST_CHECK_SUCCESS(run, UISlider_SetDefault(Screen, Id, true, 1.0));
        TEST_CHECK_SUCCESS(run, UISlider_SetIsEnabled(Screen, Id, false));
        TestUI_MovePointer(&Fixture, 150.0f, 110.0f);
        TestUI_Tick(run, &Fixture);
        TestUI_Press(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, (GetSliderValue(run, &Fixture, Id) == 10.0) && !GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Backspace));
        TestUI_Release(&Fixture, GameKey_Backspace);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A press taken away from another button than the drag's leaves the drag alone; a loss of nothing changes nothing. */
static void OnlyTheDragsOwnPressEndsTheDrag(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            UISliderCapability* Slider = Widget_GetCapability(Base, UIKit_GetCapabilityId(&Fixture.Kit, UIElementCapability_Slider));
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnPressLost(Slider, Base, GameKey_MouseLeft));
            TEST_CHECK(run, Events.Count == 0U);

            TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnPressLost(Slider, Base, GameKey_MouseRight));
            TEST_CHECK(run, Slider->_isDragging);
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnPressLost(Slider, Base, GameKey_MouseLeft));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, !Slider->_isDragging && IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed, 5.0));
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);

            // A left press lost while a key's change runs was never a drag: the change goes on until its key comes up.
            Events.Count = 0U;
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, Base));
            TestUI_Press(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Slider->_sessionKey == GameKey_Right);
            size_t EventsBefore = Events.Count;
            TEST_CHECK_SUCCESS(run, UISliderCapability_OnPressLost(Slider, Base, GameKey_MouseLeft));
            TEST_CHECK(run, Slider->_sessionKey == GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == EventsBefore);
            TestUI_Release(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Slider->_sessionKey == GameKey_Unknown);

            // A drag ending where it began tells no commit, lost or not.
            Events.Count = 0U;
            TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, 5.0));
            TestUI_Tick(run, &Fixture);
            Events.Count = 0U;
            TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
            TestUI_Press(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Base, false));
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == 0U);
            TestUI_Release(&Fixture, GameKey_MouseLeft);
            TestUI_Tick(run, &Fixture);
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}


/* The back action cancels, whichever key it is bound to. */
static void TheBackActionsKeyCancels(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
        GameInputBinding Binding = { .Key = GameKey_B, .Modifiers = 0U };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UIBack, &Binding, 1U));
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 5.0));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 5.0));
        TestUI_Tap(run, &Fixture, GameKey_B);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 2.0);
        TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Cancelled, 2.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The calls that change a slider's limits keep what it holds inside them, also what a cancel would go back to. */
static void NewLimitsKeepTheValuesInside(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.3, false, 0.0);
        TEST_CHECK_SUCCESS(run, UISlider_SetStep(&Fixture.Screen, Id, true, 1.0));
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 2.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UISlider_SetRange(&Fixture.Screen, Id, 3.0, 9.0));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 3.0);
        TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Cancelled, 3.0));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        // A step set during a drag: a cancel goes back to a whole step too.
        TEST_CHECK_SUCCESS(run, UISlider_SetStep(&Fixture.Screen, Id, false, 0.0));
        TEST_CHECK_SUCCESS(run, UISlider_SetValue(&Fixture.Screen, Id, 4.37));
        TestUI_MovePointer(&Fixture, SliderPointerFor(8.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UISlider_SetStep(&Fixture.Screen, Id, true, 0.5));
        TestUI_Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 4.5);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A slider that stops taking input or showing stops following the pointer; what it reached is committed at the release. */
static void ASliderThatStopsTakingInputStopsFollowing(TestRun* run)
{
    for (int Case = 0; Case < 2; Case++)
    {
        TestUIFixture Fixture;
        TestUIEvents Events;
        if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
        {
            uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
            Widget* Base = NULL;
            if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
            {
                TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
                TestUI_Press(&Fixture, GameKey_MouseLeft);
                TestUI_Tick(run, &Fixture);
                TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 5.0));
                TEST_CHECK_SUCCESS(run, (Case == 0) ? Widget_SetIsInputEnabled(Base, false) : Widget_SetIsRendered(Base, false));
                TestUI_MovePointer(&Fixture, SliderPointerFor(8.0), 110.0f);
                TestUI_Tick(run, &Fixture);
                TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 5.0));
                TEST_CHECK(run, (Events.Count == 2U) && IsDoubleEvent(&Events, 1U, Id, UIElementEvent_Committed, 5.0));
                TestUI_Release(&Fixture, GameKey_MouseLeft);
                TestUI_Tick(run, &Fixture);
                TEST_CHECK(run, Events.Count == 2U);

                // Back, it is whole again.
                TEST_CHECK_SUCCESS(run, (Case == 0) ? Widget_SetIsInputEnabled(Base, true) : Widget_SetIsRendered(Base, true));
                TestUI_Tick(run, &Fixture);
                TestUI_Click(run, &Fixture, SliderPointerFor(7.0), 110.0f);
                TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 7.0));
            }
        }
        TestUI_Deconstruct(run, &Fixture);
    }
}

/* A held key whose slider was hidden does nothing more, and what it reached is committed when it comes up. */
static void AHiddenSlidersHeldKeyDoesNothingMore(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id)); // Its keys are its own once it is entered.
            TestUI_Press(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
            TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Base, false));
            TestUI_Repeat(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
            TestUI_Release(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed, 6.0));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Where the pointer ended counts, also when it moved in the very tick of the release. */
static void TheLastMoveOfADragCountsAtItsRelease(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 2.0, false, 0.0);
        TestUI_MovePointer(&Fixture, SliderPointerFor(5.0), 110.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, SliderPointerFor(9.0), 110.0f);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Id), 9.0));
        TEST_CHECK(run, (Events.Count == 3U) && IsDoubleEvent(&Events, 0U, Id, UIElementEvent_Changed, 5.0)
            && IsDoubleEvent(&Events, 1U, Id, UIElementEvent_Changed, 9.0) && IsDoubleEvent(&Events, 2U, Id, UIElementEvent_Committed, 9.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A cancel is final while its key stays down: its repeats start nothing, its release commits nothing, and a new press is a new change. */
static void ACancelIsFinalWhileItsKeyStaysDown(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddSlider(run, &Fixture, 5.0, true, 1.0);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.Screen, Id)); // Its keys are its own once it is entered.
            TestUI_Press(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
            TestUI_Tap(run, &Fixture, GameKey_Escape);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 5.0);
            size_t Count = Events.Count;
            TEST_CHECK(run, IsDoubleEvent(&Events, Count - 1U, Id, UIElementEvent_Cancelled, 5.0));

            TestUI_Repeat(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 5.0);
            TestUI_Release(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, Events.Count == Count);

            TestUI_Press(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 6.0);
            TestUI_Repeat(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, GetSliderValue(run, &Fixture, Id) == 7.0);
            TestUI_Release(&Fixture, GameKey_Right);
            TestUI_Tick(run, &Fixture);
            TEST_CHECK(run, IsDoubleEvent(&Events, Events.Count - 1U, Id, UIElementEvent_Committed, 7.0));
        }
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void ASlidersPreferredSizeIsTheThemes(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        UISliderOptions Options = UISliderOptions_Create();
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Id));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Id), 0.0f, 0.0f, 64.0f, 24.0f));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the panel and the label.
static void APanelStacksItsChildrenWithTheThemesPadding(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIPanelOptions Options = UIPanelOptions_Create();
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(10.0f, 20.0f, 300.0f, 200.0f);
        uint64_t Panel = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Panel));
        UIButtonOptions Button = UIButtonOptions_Create(u8"a");
        uint64_t First = WIDGET_ID_INVALID;
        uint64_t Second = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Panel, &Button, &First));
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Panel, &Button, &Second));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, First), 20.0f, 26.0f, 280.0f, 24.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Second), 20.0f, 54.0f, 280.0f, 24.0f));

        // A click on a panel is its: it takes input and does not take focus.
        TestUI_Click(run, &Fixture, 200.0f, 150.0f);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        TestUI_MovePointer(&Fixture, 200.0f, 150.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_MouseLeft));
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);

        // Options replace the theme's padding and spacing; its preferred size has the minimum.
        UIPanelOptions Own = UIPanelOptions_Create();
        Own.Direction = UIStackDirection_Row;
        Own.HasPadding = true;
        Own.Padding = UIInsets_All(1.0f);
        Own.HasSpacing = true;
        Own.Spacing = 2.0f;
        Own.Element.HasPlacement = true;
        Own.Element.Placement = UIPlacement_FromTopLeft(400.0f, 0.0f, 300.0f, 100.0f);
        uint64_t OwnPanel = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Own, &OwnPanel));
        Button.Element.Hint.Align = UIStackAlign_Start;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, OwnPanel, &Button, &First));
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, OwnPanel, &Button, &Second));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, First), 401.0f, 1.0f, 24.0f, 24.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Second), 427.0f, 1.0f, 24.0f, 24.0f));

        Own.Spacing = -1.0f;
        uint64_t Refused = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Own, &Refused), ErrorCode_ArgumentOutOfRange);
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void APanelsPreferredSizeHasTheMinimum(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        UIPanelOptions Options = UIPanelOptions_Create();
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Panel = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Panel));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Panel), 0.0f, 0.0f, 50.0f, 40.0f)); // Empty: the minimum.
        UIButtonOptions Button = UIButtonOptions_Create(u8"abcdefghij"); // 80 + 16 wide
        uint64_t Inner = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Panel, &Button, &Inner));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Panel), 0.0f, 0.0f, 116.0f, 40.0f)); // 96 + 2 x 10; 24 + 12 under the minimum
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A kit label takes no input: a click on it goes to what is behind. Its capability answers; a plain label has none. */
static void AKitLabelLetsThePointerThrough(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"");
        UILabelOptions Options = UILabelOptions_Create(u8"over the button");
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
        uint64_t Label = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Label));
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Activated));

        // The label's own text, its capability on the wrong widget, a bad size or variant name.
        TEST_CHECK_SUCCESS(run, UILabel_SetText(&Fixture.Screen, Label, u8"new text"));
        TEST_CHECK_ERROR(run, UILabel_SetText(&Fixture.Screen, Id, u8"no"), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIButton_SetText(&Fixture.Screen, Label, u8"no"), ErrorCode_InvalidOperation);
        const unsigned char Bad[] = { 0xFF, 0xFE, 0x00 };
        TEST_CHECK_ERROR(run, UILabel_SetText(&Fixture.Screen, Label, Bad), ErrorCode_InvalidTextEncoding);
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Components) > 0U);

        UILabelOptions BadSize = UILabelOptions_Create(u8"x");
        BadSize.HasTextSize = true;
        BadSize.TextSize = NAN;
        uint64_t Refused = WIDGET_ID_INVALID;
        TEST_CHECK_ERROR(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &BadSize, &Refused), ErrorCode_ArgumentOutOfRange);
        UILabelOptions BadVariant = UILabelOptions_Create(u8"x");
        BadVariant.Element.Variant = u8"Not A Name";
        TEST_CHECK_ERROR(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &BadVariant, &Refused), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Refused == WIDGET_ID_INVALID);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Elements on a screen with no theme work and measure with the defaults; drawing them is the plain box. */
static void ElementsWorkWithoutATheme(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (TestUI_Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Id = AddButton(run, &Fixture, u8"go");
        TestUI_Click(run, &Fixture, 150.0f, 120.0f);
        TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Activated));
        uint64_t Slider = WIDGET_ID_INVALID;
        UISliderOptions Options = UISliderOptions_Create();
        Options.Maximum = 10.0;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(100.0f, 200.0f, 112.0f, 24.0f); // The default thumb is 12: 100 pixels of travel from 106.
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Slider));
        TestUI_Tick(run, &Fixture);
        TestUI_Click(run, &Fixture, 156.0f, 210.0f);
        TEST_CHECK(run, IsNearDouble(GetSliderValue(run, &Fixture, Slider), 5.0));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A theme swap reaches every element at once: sizes, and no element changes its code for it. */
static void ASwapChangesEveryElementAtOnce(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        UIButtonOptions Options = UIButtonOptions_Create(u8"ab");
        Options.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Button = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &Options, &Button));
        UILabelOptions LabelOptions = UILabelOptions_Create(u8"ab");
        LabelOptions.Element.Hint.Align = UIStackAlign_Start;
        uint64_t Label = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, Column, &LabelOptions, &Label));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Button), 0.0f, 0.0f, 32.0f, 24.0f)); // 16 + 16; the minimum 24
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Label), 0.0f, 24.0f, 16.0f, 16.0f));

        TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, "{\"name\": \"large\", \"font\": \"mono\", \"text_size\": 40,"
            "\"elements\": {\"button\": {\"padding\": [1, 1], \"min_size\": [0, 0], \"background\": {\"kind\": \"none\"}, \"text\": {\"normal\": \"#ffffff\"}}}}"));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Button), 0.0f, 0.0f, 42.0f, 42.0f));
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, Label), 0.0f, 42.0f, 40.0f, 40.0f));

        // The font is the theme's too, for a look's text and a label's.
        TEST_CHECK_SUCCESS(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Button, u8"tbp:font_probe", sizeof(FontProbeLook),
            &FontProbeVTable, NULL));
        UIButtonOptions ProbeOptions = UIButtonOptions_Create(u8"ab");
        ProbeOptions.Element.Look = u8"font_probe";
        uint64_t Probe = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &ProbeOptions, &Probe));
        TestUI_Ticks(run, &Fixture, 2U);
        Widget* ProbeBase = NULL;
        Widget* LabelBase = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Probe, &ProbeBase))
            && TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Label, &LabelBase)))
        {
            const FontProbeLook* ProbeLook = (const FontProbeLook*)ProbeBase;
            const unsigned char* Before = TextDocument_GetDefaultFontName(&ProbeLook->Text._document);
            TEST_CHECK(run, (Before != NULL) && (strstr((const char*)Before, "mono") != NULL));
            TEST_CHECK(run, TestUI_SetTheme(run, &Fixture, "{\"name\": \"fontless\"}"));
            TestUI_Ticks(run, &Fixture, 2U);
            TEST_CHECK(run, TextDocument_GetDefaultFontName(&ProbeLook->Text._document) == NULL);
            TEST_CHECK(run, TextDocument_GetDefaultFontName(LabelWidget_GetDocument((LabelWidget*)LabelBase)) == NULL);
        }

        // Taken away, what they look like goes to the defaults: elements keep working, teardown is clean.
        TEST_CHECK_SUCCESS(run, UIScreen_ClearTheme(&Fixture.Screen));
        TestUI_Ticks(run, &Fixture, 3U);
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Tests: the kit and capabilities.
/* A look that writes Render and Measure alone: the kind answers everything else, and calling code does not change. */
static void ALookWithTwoHooksWorks(TestRun* run)
{
    TestUIFixture Fixture;
    TestUIEvents Events;
    if (Construct(run, &Fixture) && TestUI_RecordEvents(run, &Fixture, &Events))
    {
        uint64_t Own = WIDGET_TYPE_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Button, u8"tbp:own", sizeof(OwnButtonLook), &OwnButtonVTable,
            &Own));
        uint64_t Found = WIDGET_TYPE_ID_INVALID;
        TEST_CHECK(run, UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Button, u8"own", &Found) && (Found == Own));
        TEST_CHECK(run, UIKit_GetElementType(&Fixture.Kit, UIElementKind_Button) != Own); // The standard look stays the standard.

        UIButtonOptions Options = UIButtonOptions_Create(u8"mine");
        Options.Element.Look = u8"tbp:own";
        Options.Element.HasPlacement = true;
        Options.Element.Placement = PlaceAt(BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Tick(run, &Fixture);
        Widget* Base = NULL;
        if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, Id, &Base)))
        {
            TEST_CHECK(run, Widget_GetTypeId(Base) == Own);
            TEST_CHECK(run, Widget_IsFocusable(Base)); // The kind's flags.
            // Calling code is what it always was.
            TEST_CHECK_SUCCESS(run, UIButton_SetText(&Fixture.Screen, Id, u8"other"));
            const unsigned char* Text = NULL;
            TEST_CHECK_SUCCESS(run, UIButton_GetText(&Fixture.Screen, Id, &Text));
            TEST_CHECK(run, strcmp((const char*)Text, "other") == 0);
            TestUI_Click(run, &Fixture, 150.0f, 120.0f);
            TEST_CHECK(run, (Events.Count == 1U) && IsEvent(&Events, 0U, Id, UIElementEvent_Activated));
            TEST_CHECK_SUCCESS(run, UIButton_SetIsEnabled(&Fixture.Screen, Id, false));
            TestUI_Click(run, &Fixture, 150.0f, 120.0f);
            TEST_CHECK(run, Events.Count == 1U);
            // It ignores the theme: no look capability, so a variant is refused, politely.
            TEST_CHECK(run, GetLook(&Fixture, Id) == NULL);
            TEST_CHECK_ERROR(run, UIElement_SetVariant(&Fixture.Screen, Id, u8"danger"), ErrorCode_InvalidOperation);
        }

        // Its Measure is what a stack asks.
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        Stack.Element.HasPlacement = true;
        Stack.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
        uint64_t Column = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Stack, &Column));
        UIButtonOptions Stacked = UIButtonOptions_Create(u8"x");
        Stacked.Element.Look = u8"tbp:own";
        Stacked.Element.Hint.Align = UIStackAlign_Start;
        uint64_t StackedId = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Column, &Stacked, &StackedId));
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, TestUI_IsBox(TestUI_GetPixelBox(run, &Fixture, StackedId), 0.0f, 0.0f, 77.0f, 33.0f));

        // Refusals.
        TEST_CHECK_ERROR(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Button, u8"tbp:own", sizeof(OwnButtonLook), &OwnButtonVTable,
            NULL), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Button, u8"Bad Name", sizeof(OwnButtonLook), &OwnButtonVTable,
            NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Button, u8"tbp:small", sizeof(Widget), &OwnButtonVTable,
            NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Label, u8"tbp:label", sizeof(OwnButtonLook), &OwnButtonVTable,
            NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIKit_RegisterLook(&Fixture.Kit, (UIElementKind)99, u8"tbp:x", sizeof(OwnButtonLook), &OwnButtonVTable, NULL),
            ErrorCode_ArgumentOutOfRange);
        UILookVTable NoRender = { .Measure = OwnButton_Measure };
        TEST_CHECK_ERROR(run, UIKit_RegisterLook(&Fixture.Kit, UIElementKind_Button, u8"tbp:x", sizeof(OwnButtonLook), &NoRender, NULL),
            ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIKit_RegisterLook(NULL, UIElementKind_Button, u8"tbp:x", sizeof(OwnButtonLook), &OwnButtonVTable, NULL),
            ErrorCode_IllegalArgument);
        TEST_CHECK(run, !UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Button, u8"tbp:missing", &Found));
        TEST_CHECK(run, !UIKit_TryGetLookType(&Fixture.Kit, UIElementKind_Toggle, u8"tbp:own", &Found));
    }
    TestUI_Deconstruct(run, &Fixture);
}

static void OneKitPerFactoryAndItsNulls(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        TEST_CHECK(run, UIKit_FromScreen(&Fixture.Screen) == &Fixture.Kit);
        UIKit Second;
        TEST_CHECK_ERROR(run, UIKit_Construct(&Second, &Fixture.Widgets, &Fixture.Components, NULL, NULL), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIKit_Construct(NULL, &Fixture.Widgets, &Fixture.Components, NULL, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIKit_Construct(&Second, NULL, &Fixture.Components, NULL, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIKit_Construct(&Second, &Fixture.Widgets, NULL, NULL, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_SUCCESS(run, UIKit_Deconstruct(NULL));
        TEST_CHECK(run, UIKit_GetCapabilityId(&Fixture.Kit, (UIElementCapability)99) == WIDGET_CAPABILITY_ID_INVALID);
        TEST_CHECK(run, UIKit_GetElementType(&Fixture.Kit, (UIElementKind)99) == WIDGET_TYPE_ID_INVALID);

        uint64_t Id = WIDGET_ID_INVALID;
        UIButtonOptions Button = UIButtonOptions_Create(u8"a");
        TEST_CHECK_ERROR(run, UIKit_CreateButton(NULL, &Fixture.Screen, 0U, &Button, &Id), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, NULL, 0U, &Button, &Id), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, 0U, NULL, &Id), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, 0U, &Button, NULL), ErrorCode_IllegalArgument);

        // A kit gone leaves its factory free for another.
        UIWidgetFactory Own;
        UIKit OwnKit;
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_Construct(&Own));
        TEST_CHECK_SUCCESS(run, UIKit_Construct(&OwnKit, &Own, &Fixture.Components, NULL, NULL));
        TEST_CHECK(run, UIWidgetFactory_GetKit(&Own) == &OwnKit);
        TEST_CHECK_SUCCESS(run, UIKit_Deconstruct(&OwnKit));
        TEST_CHECK(run, UIWidgetFactory_GetKit(&Own) == NULL);
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_Deconstruct(&Own));

        // A screen with no kit over its factory refuses by id, not a crash.
        UIWidgetFactory Bare;
        UIScreen BareScreen;
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_Construct(&Bare));
        TEST_CHECK_SUCCESS(run, UIScreen_Construct(&BareScreen, &Bare, NULL));
        TEST_CHECK(run, UIKit_FromScreen(&BareScreen) == NULL);
        TEST_CHECK_ERROR(run, UIButton_SetText(&BareScreen, 1U, u8"a"), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UILabel_SetText(&BareScreen, 1U, u8"a"), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UISlider_SetValue(&BareScreen, 1U, 1.0), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&BareScreen));
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_Deconstruct(&Bare));
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Creating under a parent that is gone, with options that fail or that the tree refuses, leaves nothing behind. */
static void AFailedCreationLeavesNothingBehind(TestRun* run)
{
    TestUIFixture Fixture;
    if (Construct(run, &Fixture))
    {
        size_t Live = UIWidgetFactory_GetTotalLiveWidgetCount(&Fixture.Widgets);
        size_t Text = TextComponentFactory_GetLiveComponentCount(&Fixture.Components);
        uint64_t Id = 123U;
        UIButtonOptions Button = UIButtonOptions_Create(u8"a");
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, 9999U, &Button, &Id), ErrorCode_InvalidOperation);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);

        Id = 123U;
        Button.Element.HasPlacement = true;
        Button.Element.Placement.OffsetMax.x = NAN;
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Button, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);

        Button = UIButtonOptions_Create(u8"a");
        Button.Element.Hint.Mode = (UIStackSizeMode)99;
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Button, &Id), ErrorCode_ArgumentOutOfRange);

        Button = UIButtonOptions_Create(u8"a");
        Button.Element.Variant = u8"Not A Name";
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Button, &Id), ErrorCode_ArgumentOutOfRange);

        const unsigned char Bad[] = { 0xFF, 0xFE, 0x00 };
        Button = UIButtonOptions_Create(Bad);
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Button, &Id), ErrorCode_InvalidTextEncoding);
        UIToggleOptions Toggle = UIToggleOptions_Create(Bad);
        TEST_CHECK_ERROR(run, UIKit_CreateToggle(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Toggle, &Id), ErrorCode_InvalidTextEncoding);
        UILabelOptions Label = UILabelOptions_Create(Bad);
        TEST_CHECK_ERROR(run, UIKit_CreateLabel(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Label, &Id), ErrorCode_InvalidTextEncoding);

        TestUI_Tick(run, &Fixture); // Anything destroyed is torn down here.
        TEST_CHECK(run, UIWidgetFactory_GetTotalLiveWidgetCount(&Fixture.Widgets) == Live);
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Components) == Text);

        // One level past the tree's limit: the element is made, refused a place, and gone.
        UIStackOptions Stack = UIStackOptions_Create(UIStackDirection_Column);
        uint64_t Parent = WIDGET_ID_INVALID;
        size_t Levels = 0U;
        for (; Levels < WIDGET_MAX_TREE_DEPTH; Levels++)
        {
            TEST_CHECK_SUCCESS(run, UIKit_CreateStack(&Fixture.Kit, &Fixture.Screen, Parent, &Stack, &Parent));
        }
        Button = UIButtonOptions_Create(u8"a");
        Id = 123U;
        TEST_CHECK_ERROR(run, UIKit_CreateButton(&Fixture.Kit, &Fixture.Screen, Parent, &Button, &Id), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Id == WIDGET_ID_INVALID);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIWidgetFactory_GetTotalLiveWidgetCount(&Fixture.Widgets) == (Live + Levels));
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Fixture.Components) == Text);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Bool and double travel in events and properties; nothing blends them. */
static void ValuesThatDoNotBlendAreRefusedByKeyframes(TestRun* run)
{
    TEST_CHECK(run, UIPropertyType_IsValid(UIPropertyType_Bool) && UIPropertyType_IsValid(UIPropertyType_Double));
    TEST_CHECK(run, !UIPropertyType_IsAnimatable(UIPropertyType_Bool) && !UIPropertyType_IsAnimatable(UIPropertyType_Double));
    TEST_CHECK(run, UIPropertyType_IsAnimatable(UIPropertyType_Float) && UIPropertyType_IsAnimatable(UIPropertyType_Vector2)
        && UIPropertyType_IsAnimatable(UIPropertyType_Color) && UIPropertyType_IsAnimatable(UIPropertyType_RenderColor));
    TEST_CHECK(run, !UIPropertyType_IsAnimatable((UIPropertyType)99));

    UIPropertyValue Yes = UIPropertyValue_FromBool(true);
    TEST_CHECK(run, (Yes.Type == UIPropertyType_Bool) && Yes.Data.BoolValue && UIPropertyValue_IsFinite(&Yes));
    UIPropertyValue Number = UIPropertyValue_FromDouble(1.5);
    TEST_CHECK(run, (Number.Type == UIPropertyType_Double) && (Number.Data.DoubleValue == 1.5) && UIPropertyValue_IsFinite(&Number));
    Number = UIPropertyValue_FromDouble(NAN);
    TEST_CHECK(run, !UIPropertyValue_IsFinite(&Number));
    Number = UIPropertyValue_FromDouble(INFINITY);
    TEST_CHECK(run, !UIPropertyValue_IsFinite(&Number));

    UIKeyframe Frames[] = { { .Time = 0.0, .Interpolation = UIInterpolation_Linear }, { .Time = 1.0, .Interpolation = UIInterpolation_Linear } };
    TEST_CHECK_ERROR(run, UIAnimation_ValidateKeyframes(Frames, 2U, UIPropertyType_Bool), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIAnimation_ValidateKeyframes(Frames, 2U, UIPropertyType_Double), ErrorCode_ArgumentOutOfRange);
    UIPropertyValue Out;
    TEST_CHECK_ERROR(run, UIAnimation_EvaluateKeyframes(Frames, 2U, UIPropertyType_Bool, 0.5, &Out), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, UIAnimation_EvaluateKeyframes(Frames, 2U, UIPropertyType_Double, 0.5, &Out), ErrorCode_ArgumentOutOfRange);
}


// Public functions.
void UIElementTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_button_clicks_on_a_press_and_a_release_on_it", AButtonClicksOnAPressAndAReleaseOnIt },
        { "a_release_elsewhere_does_nothing", AReleaseElsewhereDoesNothing },
        { "a_release_over_a_cover_does_nothing", AReleaseOverACoverDoesNothing },
        { "a_disabled_button_swallows_the_click_and_reports_nothing", ADisabledButtonSwallowsTheClickAndReportsNothing },
        { "disabling_ends_a_press_in_progress", DisablingEndsAPressInProgress },
        { "enter_or_space_clicks_and_a_held_key_does_not_click_again", EnterOrSpaceClicksAndAHeldKeyDoesNotClickAgain },
        { "the_accept_actions_key_clicks", TheAcceptActionsKeyClicks },
        { "a_handler_may_destroy_the_button", AHandlerMayDestroyTheButton },
        { "a_detached_press_does_not_stay_pressed", ADetachedPressDoesNotStayPressed },
        { "only_the_left_button_presses", OnlyTheLeftButtonPresses },
        { "a_button_that_stopped_taking_input_does_not_click", AButtonThatStoppedTakingInputDoesNotClick },
        { "a_second_key_during_a_hold_does_not_click", ASecondKeyDuringAHoldDoesNotClick },
        { "the_press_helper_answers_for_keys", ThePressHelperAnswersForKeys },
        { "a_button_can_be_made_toggled", AButtonCanBeMadeToggled },
        { "the_same_text_keeps_the_cached_size", TheSameTextKeepsTheCachedSize },
        { "a_hidden_button_is_out_of_the_keyboards_reach", AHiddenButtonIsOutOfTheKeyboardsReach },
        { "a_buttons_states_show_in_their_order", AButtonsStatesShowInTheirOrder },
        { "a_buttons_preferred_size_follows_its_text_and_look", AButtonsPreferredSizeFollowsItsTextAndLook },
        { "colours_fade_from_state_to_state", ColoursFadeFromStateToState },
        { "the_first_state_and_no_transition_are_immediate", TheFirstStateAndNoTransitionAreImmediate },
        { "a_toggle_swaps_and_a_click_from_mixed_turns_it_on", AToggleSwapsAndAClickFromMixedTurnsItOn },
        { "disabling_a_toggle_ends_its_press_and_its_focus", DisablingAToggleEndsItsPressAndItsFocus },
        { "a_toggle_measures_its_box_and_text", AToggleMeasuresItsBoxAndText },
        { "the_sliders_arithmetic", TheSlidersArithmetic },
        { "dragging_maps_the_pointer_and_clamps_outside_the_box", DraggingMapsThePointerAndClampsOutsideTheBox },
        { "a_click_on_the_thumb_changes_nothing", AClickOnTheThumbChangesNothing },
        { "a_step_keeps_the_value_on_whole_steps", AStepKeepsTheValueOnWholeSteps },
        { "arrows_step_home_and_end_jump_and_the_keys_release_commits", ArrowsStepHomeAndEndJumpAndTheKeysReleaseCommits },
        { "the_step_actions_keys_step", TheStepActionsKeysStep },
        { "escape_during_a_drag_puts_the_first_value_back", EscapeDuringADragPutsTheFirstValueBack },
        { "escape_during_a_keys_hold_puts_the_first_value_back", EscapeDuringAKeysHoldPutsTheFirstValueBack },
        { "the_wheel_changes_it_only_while_entered", TheWheelChangesItOnlyWhileEntered },
        { "bad_values_and_ranges_are_refused", BadValuesAndRangesAreRefused },
        { "disabling_cancels_a_drag_and_swallows_input", DisablingCancelsADragAndSwallowsInput },
        { "a_detached_drag_commits_at_once", ADetachedDragCommitsAtOnce },
        { "only_the_left_button_drags", OnlyTheLeftButtonDrags },
        { "nothing_else_moves_a_slider_during_a_change", NothingElseMovesASliderDuringAChange },
        { "a_repeat_of_a_jump_does_not_jump_again", ARepeatOfAJumpDoesNotJumpAgain },
        { "a_detached_keys_change_ends_once_it_is_back", ADetachedKeysChangeEndsOnceItIsBack },
        { "the_sliders_helpers_answer_for_their_input", TheSlidersHelpersAnswerForTheirInput },
        { "the_back_actions_key_cancels", TheBackActionsKeyCancels },
        { "new_limits_keep_the_values_inside", NewLimitsKeepTheValuesInside },
        { "a_slider_that_stops_taking_input_stops_following", ASliderThatStopsTakingInputStopsFollowing },
        { "a_drag_past_the_window_reaches_the_limit", ADragPastTheWindowReachesTheLimit },
        { "a_drag_comes_back_from_past_the_window", ADragComesBackFromPastTheWindow },
        { "the_click_modes_activate_when_they_say", TheClickModesActivateWhenTheySay },
        { "click_settings_are_checked", ClickSettingsAreChecked },
        { "each_activation_button_is_its_own", EachActivationButtonIsItsOwn },
        { "a_press_lost_or_disabled_activates_nothing", APressLostOrDisabledActivatesNothing },
        { "a_disabled_element_reports_nothing", ADisabledElementReportsNothing },
        { "changing_the_buttons_during_a_press_leaves_no_stuck_press", ChangingTheButtonsDuringAPressLeavesNoStuckPress },
        { "a_lost_press_shows_nothing_while_the_button_stays_down", ALostPressShowsNothingWhileTheButtonStaysDown },
        { "a_release_over_activates_only_what_is_asked", AReleaseOverActivatesOnlyWhatIsAsked },
        { "a_handed_press_activates_only_an_on_release_element", AHandedPressActivatesOnlyAnOnReleaseElement },
        { "an_element_without_the_loss_hook_still_lets_go", AnElementWithoutTheLossHookStillLetsGo },
        { "a_toggle_takes_the_click_modes", AToggleTakesTheClickModes },
        { "an_element_tells_its_mouse_events", AnElementTellsItsMouseEvents },
        { "an_element_asks_for_its_context_menu", AnElementAsksForItsContextMenu },
        { "every_element_tells_the_same_mouse_events", EveryElementTellsTheSameMouseEvents },
        { "a_button_takes_the_wheel_only_when_made_to", AButtonTakesTheWheelOnlyWhenMadeTo },
        { "a_slider_takes_the_wheel_when_entered_or_with_control", ASliderTakesTheWheelWhenEnteredOrWithControl },
        { "the_reset_action_puts_a_slider_back", TheResetActionPutsASliderBack },
        { "only_the_drags_own_press_ends_the_drag", OnlyTheDragsOwnPressEndsTheDrag },
        { "a_hidden_sliders_held_key_does_nothing_more", AHiddenSlidersHeldKeyDoesNothingMore },
        { "the_last_move_of_a_drag_counts_at_its_release", TheLastMoveOfADragCountsAtItsRelease },
        { "a_cancel_is_final_while_its_key_stays_down", ACancelIsFinalWhileItsKeyStaysDown },
        { "a_sliders_preferred_size_is_the_themes", ASlidersPreferredSizeIsTheThemes },
        { "a_panel_stacks_its_children_with_the_themes_padding", APanelStacksItsChildrenWithTheThemesPadding },
        { "a_panels_preferred_size_has_the_minimum", APanelsPreferredSizeHasTheMinimum },
        { "a_kit_label_lets_the_pointer_through", AKitLabelLetsThePointerThrough },
        { "elements_work_without_a_theme", ElementsWorkWithoutATheme },
        { "a_swap_changes_every_element_at_once", ASwapChangesEveryElementAtOnce },
        { "a_look_with_two_hooks_works", ALookWithTwoHooksWorks },
        { "one_kit_per_factory_and_its_nulls", OneKitPerFactoryAndItsNulls },
        { "a_failed_creation_leaves_nothing_behind", AFailedCreationLeavesNothingBehind },
        { "values_that_do_not_blend_are_refused_by_keyframes", ValuesThatDoNotBlendAreRefusedByKeyframes },
    };
    TestRun_RunSuite(run, "UIElement", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
