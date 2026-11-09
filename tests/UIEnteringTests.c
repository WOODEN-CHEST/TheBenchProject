#include "TestSuites.h"
#include "TestUI.h"
#include "UIBoxStyle.h"
#include "UIKit.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"
#include <math.h>


/*
 * Entering elements: one keyboard rule for every element. Moving the focus never changes anything; an edit element (a slider,
 * a text box) takes keys of its own only once it is entered, by Enter, a press or code; Escape, Enter, Tab and focus moving
 * leave it; a held Tab carries editing on. A probe edit element records its hooks, keys and presses in a log the test keeps,
 * so even a destroyed probe's silence can be checked; a probe container records what its children leave.
 */


// Macros.
/** Flat boxes and a thumb whose entered colour differs from its focused one. */
#define ENTERING_THEME \
    "{\"name\": \"entering\", \"font\": \"mono\", \"text_size\": 16, \"elements\": {" \
    "\"button\": {\"padding\": [8, 4], \"min_size\": [24, 24], \"background\": {\"kind\": \"flat\"}, \"text\": {\"normal\": \"#e0e0e0\"}}," \
    "\"slider\": {\"min_size\": [64, 24], \"thumb_size\": [20, 20], \"track_thickness\": 4, \"track\": {\"kind\": \"flat\"}," \
    "\"fill\": {\"kind\": \"flat\"}, \"thumb\": {\"kind\": \"flat\", \"colors\": {\"normal\": \"#808080\", \"focused\": \"#a0a0a0\"," \
    "\"entered\": \"#40c060\"}}}}}"
/** How many entries a probe log keeps; the rest are counted, not kept. */
#define PROBE_LOG_CAPACITY ((size_t)128)
/** How many state changes the recorder keeps. */
#define STATE_LOG_CAPACITY ((size_t)64)
/** The row the tests lay elements out in: each 150 x 30 UI pixels, 50 apart, from x = 50. */
#define ROW_Y 200.0f
#define CELL_WIDTH 150.0f
#define CELL_HEIGHT 30.0f
#define CELL_STEP 200.0f
#define ROW_LEFT 50.0f


// Types.
/* What a probe's log tells of. */
typedef enum ProbeEntryKindEnum
{
    ProbeEntry_Enter,
    ProbeEntry_Leave,
    ProbeEntry_Press,
    ProbeEntry_KeyPress,
    ProbeEntry_KeyRepeat,
    ProbeEntry_KeyRelease,
    ProbeEntry_Text,
    ProbeEntry_Scroll
} ProbeEntryKind;

/* One thing that reached a probe: its kind, the widget, a detail (the cause, the reason, the key, the codepoint or the button)
 * and whether the widget was entered then. */
typedef struct ProbeEntryStruct
{
    ProbeEntryKind Kind;
    uint64_t WidgetId;
    int32_t Detail;
    bool WasEntered;
} ProbeEntry;

/* The log every probe of a test writes to; it outlives the probes. */
typedef struct ProbeLogStruct
{
    ProbeEntry Entries[PROBE_LOG_CAPACITY];
    size_t Count;
} ProbeLog;

/* What a probe's hook does once, after it logged. */
typedef enum ProbeActionEnum
{
    ProbeAction_None,
    ProbeAction_FocusTarget,
    ProbeAction_EnterTarget,
    ProbeAction_DestroySelf,
    ProbeAction_ClearFocus
} ProbeAction;

/* A widget that records what reaches it, takes what it is told to take and acts from its hooks. */
typedef struct ProbeWidgetStruct
{
    Widget Base;
    ProbeLog* Log;
    /** The one key it takes (its press, repeats and release); GameKey_Unknown for none. */
    GameKey HandledKey;
    bool IsHandlingText;
    bool IsTakingWheel;
    ProbeAction OnEnterAction;
    ProbeAction OnLeaveAction;
    ProbeAction OnFocusAction;
    uint64_t ActionTargetId;
} ProbeWidget;

/* A state change a screen delivered. */
typedef struct StateChangeStruct
{
    UIEventKind Kind;
    uint64_t WidgetId;
    bool IsSet;
} StateChange;

/* The state changes a screen delivered, in order. */
typedef struct StateLogStruct
{
    StateChange Changes[STATE_LOG_CAPACITY];
    size_t Count;
} StateLog;

/* The kit's screen with two probe types: an edit element and a container. */
typedef struct EnteringFixtureStruct
{
    TestUIFixture UI;
    ProbeLog Log;
    StateLog States;
    TestUIEvents Events;
    uint64_t EditType;
    /** The edit probe's type made to enter itself: a press does not enter its widgets. */
    uint64_t SelfEnteringType;
    uint64_t ContainerType;
} EnteringFixture;


// Static functions: the probe.
static void LogEntry(ProbeWidget* self, ProbeEntryKind kind, int32_t detail)
{
    if (self->Log->Count < PROBE_LOG_CAPACITY)
    {
        self->Log->Entries[self->Log->Count] = (ProbeEntry){ .Kind = kind, .WidgetId = Widget_GetId(&self->Base), .Detail = detail,
            .WasEntered = Widget_IsEntered(&self->Base) };
    }
    self->Log->Count++;
}

/* Runs an action once: it is cleared first, so a hook it sets off again does nothing more. */
static Error RunAction(ProbeWidget* self, ProbeAction* action)
{
    ProbeAction Action = *action;
    *action = ProbeAction_None;
    UIScreen* Screen = Widget_GetScreen(&self->Base);
    Widget* Target = NULL;
    switch (Action)
    {
        case ProbeAction_None:
            return Error_CreateSuccess();
        case ProbeAction_FocusTarget:
            return UIScreen_TryGetWidgetById(Screen, self->ActionTargetId, &Target) ? UIScreen_SetFocusedWidget(Screen, Target)
                : Error_CreateSuccess();
        case ProbeAction_EnterTarget:
            return UIScreen_EnterWidget(Screen, self->ActionTargetId);
        case ProbeAction_DestroySelf:
            return Widget_Destroy(&self->Base);
        case ProbeAction_ClearFocus:
            return UIScreen_SetFocusedWidget(Screen, NULL);
    }
    return Error_CreateSuccess();
}

static Error Probe_Construct(void* widget, void* args)
{
    ProbeWidget* Probe = widget;
    Probe->Log = args;
    Probe->HandledKey = GameKey_Unknown;
    return Error_CreateSuccess();
}

static Error Probe_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    if (args->Type == UIMouseInputType_ButtonPress)
    {
        LogEntry(self, ProbeEntry_Press, (int32_t)args->Button);
    }
    return Error_CreateSuccess();
}

static Error Probe_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    ProbeWidget* Probe = self;
    switch (args->Type)
    {
        case UIKeyboardInputType_KeyPress:
            LogEntry(Probe, ProbeEntry_KeyPress, (int32_t)args->Key);
            break;
        case UIKeyboardInputType_KeyRepeat:
            LogEntry(Probe, ProbeEntry_KeyRepeat, (int32_t)args->Key);
            break;
        case UIKeyboardInputType_KeyRelease:
            LogEntry(Probe, ProbeEntry_KeyRelease, (int32_t)args->Key);
            break;
        case UIKeyboardInputType_Text:
            LogEntry(Probe, ProbeEntry_Text, (int32_t)args->Codepoint);
            break;
    }
    *outIsHandled = (args->Type == UIKeyboardInputType_Text) ? Probe->IsHandlingText
        : ((Probe->HandledKey != GameKey_Unknown) && (args->Key == Probe->HandledKey));
    return Error_CreateSuccess();
}

static Error Probe_OnScrollInput(void* self, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    (void)args;
    ProbeWidget* Probe = self;
    if (Probe->IsTakingWheel)
    {
        LogEntry(Probe, ProbeEntry_Scroll, 0);
        *outIsHandled = true;
    }
    return Error_CreateSuccess();
}

static Error Probe_OnFocusEnable(void* self)
{
    ProbeWidget* Probe = self;
    return RunAction(Probe, &Probe->OnFocusAction);
}

static Error Probe_OnEnter(void* self, UIEnterCause cause)
{
    ProbeWidget* Probe = self;
    LogEntry(Probe, ProbeEntry_Enter, (int32_t)cause);
    return RunAction(Probe, &Probe->OnEnterAction);
}

static Error Probe_OnLeave(void* self, UILeaveReason reason)
{
    ProbeWidget* Probe = self;
    LogEntry(Probe, ProbeEntry_Leave, (int32_t)reason);
    return RunAction(Probe, &Probe->OnLeaveAction);
}

static const WidgetVTable ProbeVTable =
{
    .OnMouseInput = Probe_OnMouseInput,
    .OnKeyboardInput = Probe_OnKeyboardInput,
    .OnScrollInput = Probe_OnScrollInput,
    .OnFocusEnable = Probe_OnFocusEnable,
    .OnEnter = Probe_OnEnter,
    .OnLeave = Probe_OnLeave
};


// Static functions: the fixture.
static Error RecordState(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)screen;
    StateLog* States = UserData_GetPointer(userData);
    if (States->Count < STATE_LOG_CAPACITY)
    {
        States->Changes[States->Count] = (StateChange){ .Kind = event->Kind, .WidgetId = event->WidgetId, .IsSet = event->IsSet };
        States->Count++;
    }
    return Error_CreateSuccess();
}

static bool Construct(TestRun* run, EnteringFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    if (!TestUI_Construct(run, &self->UI) || !TestUI_SetTheme(run, &self->UI, ENTERING_THEME)
        || !TestUI_RecordEvents(run, &self->UI, &self->Events))
    {
        return false;
    }
    WidgetTypeDescriptor Edit = { .Name = u8"EditProbe", .StructSize = sizeof(ProbeWidget), .VTable = &ProbeVTable, .Constructor = Probe_Construct,
        .IsFocusable = true, .NeedsEntering = true };
    WidgetTypeDescriptor SelfEntering = Edit;
    SelfEntering.Name = u8"SelfEnteringProbe";
    SelfEntering.EntersItself = true;
    WidgetTypeDescriptor Container = { .Name = u8"ContainerProbe", .StructSize = sizeof(ProbeWidget), .VTable = &ProbeVTable,
        .Constructor = Probe_Construct };
    UserData States = UserData_FromPointer(&self->States);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    return TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&self->UI.Widgets, &Edit, &self->EditType))
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&self->UI.Widgets, &SelfEntering, &self->SelfEnteringType))
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&self->UI.Widgets, &Container, &self->ContainerType))
        && TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&self->UI.Screen, WIDGET_ID_INVALID,
            UI_EVENT_KIND_MASK(UIEventKind_EnteredChanged) | UI_EVENT_KIND_MASK(UIEventKind_FocusChanged), RecordState, &States, &Subscription));
}

static void Deconstruct(TestRun* run, EnteringFixture* self)
{
    TestUI_Deconstruct(run, &self->UI);
}

static void Tick(TestRun* run, EnteringFixture* self)
{
    TestUI_Tick(run, &self->UI);
}

static void Tap(TestRun* run, EnteringFixture* self, GameKey key)
{
    TestUI_Tap(run, &self->UI, key);
}

/* A probe of a type at (x, y) in UI pixels of its parent, initialized and added (to the roots for WIDGET_ID_INVALID). */
static uint64_t AddProbe(TestRun* run, EnteringFixture* self, uint64_t typeId, uint64_t parentId, float x, float y, float width, float height)
{
    Widget* Created = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&self->UI.Widgets, &self->UI.Screen, typeId, &self->Log, &Created)))
    {
        return WIDGET_ID_INVALID;
    }
    uint64_t Id = Widget_GetId(Created);
    UIPlacement Placement = UIPlacement_FromTopLeft(x, y, width, height);
    TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Created, &Placement));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    Widget* Parent = NULL;
    if (parentId == WIDGET_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&self->UI.Screen, Created));
    }
    else if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&self->UI.Screen, parentId, &Parent)))
    {
        TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Parent, Created));
    }
    return Id;
}

/* An edit probe in the row's cell @p cell. */
static uint64_t AddEditProbe(TestRun* run, EnteringFixture* self, uint64_t parentId, int cell)
{
    return AddProbe(run, self, self->EditType, parentId, ROW_LEFT + ((float)cell * CELL_STEP), ROW_Y, CELL_WIDTH, CELL_HEIGHT);
}

/* A container filling the screen, as a dialog around its fields. */
static uint64_t AddContainer(TestRun* run, EnteringFixture* self)
{
    return AddProbe(run, self, self->ContainerType, WIDGET_ID_INVALID, 0.0f, 0.0f, TEST_UI_SURFACE_WIDTH, TEST_UI_SURFACE_HEIGHT);
}

static ProbeWidget* GetProbe(EnteringFixture* self, uint64_t id)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(&self->UI.Screen, id, &Found) ? (ProbeWidget*)Found : NULL;
}

static Widget* GetWidget(EnteringFixture* self, uint64_t id)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(&self->UI.Screen, id, &Found) ? Found : NULL;
}

/* A kit slider over [0, 10] in steps of 1, in the row's cell @p cell. */
static uint64_t AddSlider(TestRun* run, EnteringFixture* self, uint64_t parentId, float x, float y, double value)
{
    UISliderOptions Options = UISliderOptions_Create();
    Options.Value.Settings.Maximum = 10.0;
    Options.Value.Value = value;
    Options.Value.Settings.HasStep = true;
    Options.Value.Settings.Step = 1.0;
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(x, y, CELL_WIDTH, CELL_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&self->UI.Kit, &self->UI.Screen, parentId, &Options, &Id));
    return Id;
}

static uint64_t AddButton(TestRun* run, EnteringFixture* self, uint64_t parentId, float x, float y)
{
    UIButtonOptions Options = UIButtonOptions_Create(u8"b");
    Options.Element.HasPlacement = true;
    Options.Element.Placement = UIPlacement_FromTopLeft(x, y, CELL_WIDTH, CELL_HEIGHT);
    uint64_t Id = WIDGET_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&self->UI.Kit, &self->UI.Screen, parentId, &Options, &Id));
    return Id;
}

static float CellX(int cell)
{
    return ROW_LEFT + ((float)cell * CELL_STEP);
}

static double GetValue(TestRun* run, EnteringFixture* self, uint64_t id)
{
    double Value = NAN;
    TEST_CHECK_SUCCESS(run, UIValue_Get(&self->UI.Screen, id, &Value));
    return Value;
}

/* The pointer over a cell's middle. */
static void PointAtCell(EnteringFixture* self, int cell)
{
    TestUI_MovePointer(&self->UI, CellX(cell) + (CELL_WIDTH * 0.5f), ROW_Y + (CELL_HEIGHT * 0.5f));
}

/* A press of @p button on a cell, and its release: two ticks. */
static void ClickCell(TestRun* run, EnteringFixture* self, int cell, GameKey button)
{
    PointAtCell(self, cell);
    TestUI_Press(&self->UI, button);
    Tick(run, self);
    TestUI_Release(&self->UI, button);
    Tick(run, self);
}

/* How many entries of a kind a widget's log has, with that detail (any detail for -1). */
static size_t CountEntries(const EnteringFixture* self, uint64_t id, ProbeEntryKind kind, int32_t detail)
{
    size_t Count = 0U;
    for (size_t Index = 0U; (Index < self->Log.Count) && (Index < PROBE_LOG_CAPACITY); Index++)
    {
        const ProbeEntry* Entry = &self->Log.Entries[Index];
        Count += ((Entry->WidgetId == id) && (Entry->Kind == kind) && ((detail < 0) || (Entry->Detail == detail))) ? 1U : 0U;
    }
    return Count;
}

/* The last entry of the log, or one of no widget. */
static ProbeEntry GetLastEntry(const EnteringFixture* self)
{
    if ((self->Log.Count == 0U) || (self->Log.Count > PROBE_LOG_CAPACITY))
    {
        return (ProbeEntry){ .Kind = ProbeEntry_Scroll, .WidgetId = WIDGET_ID_INVALID, .Detail = -1, .WasEntered = false };
    }
    return self->Log.Entries[self->Log.Count - 1U];
}

/* Whether the last entry is a leave of @p id for @p reason. */
static bool WasLastLeft(const EnteringFixture* self, uint64_t id, UILeaveReason reason)
{
    ProbeEntry Last = GetLastEntry(self);
    return (Last.Kind == ProbeEntry_Leave) && (Last.WidgetId == id) && (Last.Detail == (int32_t)reason);
}

/* How many element events a widget posted of a kind. */
static size_t CountEvents(const EnteringFixture* self, uint64_t id, UIElementEvent event)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < self->Events.Count; Index++)
    {
        Count += ((self->Events.Events[Index].WidgetId == id) && (self->Events.Events[Index].WidgetEventId == (int32_t)event)) ? 1U : 0U;
    }
    return Count;
}

/* How many entered changes a widget's notifications told, to @p isSet. */
static size_t CountEnteredChanges(const EnteringFixture* self, uint64_t id, bool isSet)
{
    size_t Count = 0U;
    for (size_t Index = 0U; Index < self->States.Count; Index++)
    {
        const StateChange* Change = &self->States.Changes[Index];
        Count += ((Change->Kind == UIEventKind_EnteredChanged) && (Change->WidgetId == id) && (Change->IsSet == isSet)) ? 1U : 0U;
    }
    return Count;
}

/* The screen's promises: at most one entered widget, always the focused one, and every widget's flags in line with the
 * screen's ids. */
static void CheckInvariants(TestRun* run, EnteringFixture* self, const uint64_t* ids, size_t count)
{
    uint64_t Entered = UIScreen_GetEnteredWidgetId(&self->UI.Screen);
    uint64_t Focused = UIScreen_GetFocusedWidgetId(&self->UI.Screen);
    TEST_CHECK(run, (Entered == WIDGET_ID_INVALID) || (Entered == Focused));
    TEST_CHECK(run, (Entered == WIDGET_ID_INVALID) || UIScreen_IsWidgetAlive(&self->UI.Screen, Entered));
    for (size_t Index = 0U; Index < count; Index++)
    {
        Widget* Target = GetWidget(self, ids[Index]);
        if (Target != NULL)
        {
            TEST_CHECK(run, Widget_IsEntered(Target) == (ids[Index] == Entered));
            TEST_CHECK(run, Widget_IsFocused(Target) == (ids[Index] == Focused));
        }
    }
}


// Tests: moving past.
/* Arrow navigation and Tab pass over a slider in every direction and never change it; the wheel over a slider that is only
 * focused goes on up the tree. */
static void ArrowsAndTabMovePastASliderAndChangeNothing(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        // A cross: buttons above, below and beside a slider, in a container that takes the wheel.
        uint64_t Container = AddContainer(run, &Fixture);
        GetProbe(&Fixture, Container)->IsTakingWheel = true;
        uint64_t Up = AddButton(run, &Fixture, Container, 425.0f, 60.0f);
        uint64_t Left = AddButton(run, &Fixture, Container, 175.0f, ROW_Y);
        uint64_t Slider = AddSlider(run, &Fixture, Container, 425.0f, ROW_Y, 5.0);
        uint64_t Right = AddButton(run, &Fixture, Container, 675.0f, ROW_Y);
        uint64_t Down = AddButton(run, &Fixture, Container, 425.0f, 340.0f);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, GetWidget(&Fixture, Up)));

        // Down and up through it, then left and right through it: it is only ever focused.
        const GameKey Keys[] = { GameKey_Down, GameKey_Down, GameKey_Down, GameKey_Up, GameKey_Up, GameKey_Down, GameKey_Left, GameKey_Right,
            GameKey_Right, GameKey_Left };
        const uint64_t Lands[] = { Up, Slider, Down, Slider, Up, Slider, Left, Slider, Right, Slider };
        for (size_t Index = 0U; Index < (sizeof(Keys) / sizeof(Keys[0])); Index++)
        {
            Tap(run, &Fixture, Keys[Index]);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Lands[Index]);
            TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID);
        }
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 5.0) && (Fixture.Events.Count == 0U));

        // Tab and Shift+Tab through every element in reading order, both ways round.
        const uint64_t Order[] = { Right, Down, Up, Left, Slider, Right };
        for (size_t Index = 0U; Index < (sizeof(Order) / sizeof(Order[0])); Index++)
        {
            Tap(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Order[Index]);
        }
        Fixture.UI.Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Slider);
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Left);
        Fixture.UI.Sample.KeyStates[GameKey_LeftShift] = 0U;
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 5.0) && (Fixture.Events.Count == 0U) && (CountEnteredChanges(&Fixture, Slider, true) == 0U));

        // Focused only, the wheel over it is not its: it goes up the tree.
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, GetWidget(&Fixture, Slider)));
        TestUI_MovePointer(&Fixture.UI, 500.0f, ROW_Y + 15.0f);
        Tick(run, &Fixture);
        TestUI_Wheel(&Fixture.UI, 1.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 5.0) && (CountEntries(&Fixture, Container, ProbeEntry_Scroll, -1) == 1U));
    }
    Deconstruct(run, &Fixture);
}


// Tests: a slider inside.
/* Enter enters a slider, and the four arrows change it, each press committing; Enter leaves keeping, Escape leaves; Escape
 * during a held key cancels only that change and stays inside; at the ends the arrows do nothing and are still consumed. */
static void EnterEntersASliderWhoseArrowsEachCommit(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Slider = AddSlider(run, &Fixture, WIDGET_ID_INVALID, CellX(0), ROW_Y, 5.0);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, GetWidget(&Fixture, Slider)));
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Slider) && (GetValue(run, &Fixture, Slider) == 5.0));
        TEST_CHECK(run, CountEnteredChanges(&Fixture, Slider, true) == 1U);

        // Each press is a whole change: Changed, then Committed when its key comes up.
        const GameKey Keys[] = { GameKey_Right, GameKey_Up, GameKey_Left, GameKey_Down, GameKey_Down };
        const double Values[] = { 6.0, 7.0, 6.0, 5.0, 4.0 };
        for (size_t Index = 0U; Index < (sizeof(Keys) / sizeof(Keys[0])); Index++)
        {
            size_t Commits = CountEvents(&Fixture, Slider, UIElementEvent_Committed);
            TestUI_Press(&Fixture.UI, Keys[Index]);
            Tick(run, &Fixture);
            TEST_CHECK(run, GetValue(run, &Fixture, Slider) == Values[Index]);
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.UI.Input, Keys[Index]));
            TestUI_Release(&Fixture.UI, Keys[Index]);
            Tick(run, &Fixture);
            TEST_CHECK(run, CountEvents(&Fixture, Slider, UIElementEvent_Committed) == (Commits + 1U));
        }
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Slider); // The arrows never threw it out.

        // Enter leaves, keeping; Enter enters again; Escape leaves too, and the focus stays.
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Slider));
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 4.0) && (CountEnteredChanges(&Fixture, Slider, false) == 1U));
        Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, GetValue(run, &Fixture, Slider) == 4.0); // Outside again: the arrows are navigation's.
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Slider);
        TestUI_Press(&Fixture.UI, GameKey_Escape);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Slider));
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.UI.Input, GameAction_Pause)); // That Escape is used: no pause.
        TestUI_Release(&Fixture.UI, GameKey_Escape);
        Tick(run, &Fixture);
        TEST_CHECK(run, GetValue(run, &Fixture, Slider) == 4.0); // The presses it committed stay.

        // Escape during a held key takes back only that change, and the slider stays inside.
        Tap(run, &Fixture, GameKey_Enter);
        TestUI_Press(&Fixture.UI, GameKey_Right);
        Tick(run, &Fixture);
        TestUI_Repeat(&Fixture.UI, GameKey_Right);
        Tick(run, &Fixture);
        TEST_CHECK(run, GetValue(run, &Fixture, Slider) == 6.0);
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 4.0) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Slider));
        TEST_CHECK(run, CountEvents(&Fixture, Slider, UIElementEvent_Cancelled) == 1U);
        TestUI_Release(&Fixture.UI, GameKey_Right);
        Tick(run, &Fixture);

        // At the ends the arrows move nothing and are still the slider's: consumed, never the game's.
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.UI.Screen, Slider, 10.0));
        size_t EventCount = Fixture.Events.Count;
        const GameKey Higher[] = { GameKey_Right, GameKey_Up };
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            TestUI_Press(&Fixture.UI, Higher[Index]);
            Tick(run, &Fixture);
            TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 10.0) && GameInput_IsKeyConsumed(&Fixture.UI.Input, Higher[Index]));
            TestUI_Release(&Fixture.UI, Higher[Index]);
            Tick(run, &Fixture);
        }
        TEST_CHECK_SUCCESS(run, UIValue_Set(&Fixture.UI.Screen, Slider, 0.0));
        const GameKey Lower[] = { GameKey_Left, GameKey_Down };
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            TestUI_Press(&Fixture.UI, Lower[Index]);
            Tick(run, &Fixture);
            TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 0.0) && GameInput_IsKeyConsumed(&Fixture.UI.Input, Lower[Index]));
            TestUI_Release(&Fixture.UI, Lower[Index]);
            Tick(run, &Fixture);
        }
        TEST_CHECK(run, (Fixture.Events.Count == EventCount) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Slider));

        // Entered, the wheel is its.
        TestUI_MovePointer(&Fixture.UI, CellX(0) + 70.0f, ROW_Y + 15.0f);
        Tick(run, &Fixture);
        TestUI_Wheel(&Fixture.UI, 2.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, GetValue(run, &Fixture, Slider) == 2.0);
    }
    Deconstruct(run, &Fixture);
}

/* Leaving a slider, any way, ends its change in progress there: a held key's or a drag's, committed on a keep and taken back on a
 * cancel; what is left of that key or drag does nothing, and a new press is a new change. */
static void LeavingASliderEndsItsChange(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Slider = AddSlider(run, &Fixture, WIDGET_ID_INVALID, CellX(0), ROW_Y, 5.0);
        uint64_t Other = AddSlider(run, &Fixture, WIDGET_ID_INVALID, CellX(1), ROW_Y, 5.0);
        Tick(run, &Fixture);

        // A held arrow, then Enter: committed at the leave; the arrow's repeats and release do nothing more. (A repeat in the very tick
        // of the Enter would still count: keys reach the element before the screen's rules act on what it left.)
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, Slider));
        TestUI_Press(&Fixture.UI, GameKey_Right);
        Tick(run, &Fixture);
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 6.0) && (CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 0U));
        TestUI_Press(&Fixture.UI, GameKey_Enter);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID) && (CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 1U));
        TestUI_Release(&Fixture.UI, GameKey_Enter);
        TestUI_Repeat(&Fixture.UI, GameKey_Right);
        Tick(run, &Fixture);
        TestUI_Release(&Fixture.UI, GameKey_Right);
        Tick(run, &Fixture);
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 6.0) && (CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 1U));
        TEST_CHECK(run, CountEvents(&Fixture, Slider, UIElementEvent_Changed) == 1U);
        // Entered again, the same key pressed anew is a new change.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, Slider));
        Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 7.0) && (CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 2U));

        // Left by code with a cancel during a held key: the change is taken back.
        TestUI_Press(&Fixture.UI, GameKey_Right);
        Tick(run, &Fixture);
        TEST_CHECK(run, GetValue(run, &Fixture, Slider) == 8.0);
        TEST_CHECK_SUCCESS(run, UIScreen_LeaveWidget(&Fixture.UI.Screen, UILeaveReason_Cancel));
        Tick(run, &Fixture);
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 7.0) && (CountEvents(&Fixture, Slider, UIElementEvent_Cancelled) == 1U));
        TestUI_Release(&Fixture.UI, GameKey_Right);
        Tick(run, &Fixture);
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == 7.0) && (CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 2U));

        // A drag, then Enter: committed at the leave; the pointer no longer moves it, its release tells nothing, and Escape is not the
        // slider's any more (it clears the focus, and cancels nothing).
        PointAtCell(&Fixture, 0);
        Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture.UI, CellX(0) + 20.0f, ROW_Y + 15.0f);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Slider) && (GetValue(run, &Fixture, Slider) < 2.0));
        double Dragged = GetValue(run, &Fixture, Slider);
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID) && (CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 3U));
        TestUI_MovePointer(&Fixture.UI, CellX(0) + 130.0f, ROW_Y + 15.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, GetValue(run, &Fixture, Slider) == Dragged);
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (GetValue(run, &Fixture, Slider) == Dragged) && (CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 3U)
            && (CountEvents(&Fixture, Slider, UIElementEvent_Cancelled) == 1U));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID);

        // Tab during a drag: committed there, and the next slider is entered.
        TestUI_MovePointer(&Fixture.UI, CellX(0) + 120.0f, ROW_Y + 15.0f);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        Tick(run, &Fixture);
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Other) && (CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 4U));
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, CountEvents(&Fixture, Slider, UIElementEvent_Committed) == 4U);
    }
    Deconstruct(run, &Fixture);
}

/* Escape, Enter and Tab pressed in one tick on an entered element: one acts (Escape, then Enter, then Tab), and every one is consumed, so
 * none reaches the game. */
static void LeaveKeysInOneTickAllStayTheScreens(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t A = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 0);
        uint64_t B = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 1);
        Tick(run, &Fixture);
        const GameKey Firsts[] = { GameKey_Escape, GameKey_Enter, GameKey_Escape };
        const GameKey Seconds[] = { GameKey_Tab, GameKey_Tab, GameKey_Enter };
        const UILeaveReason Reasons[] = { UILeaveReason_Cancel, UILeaveReason_Keep, UILeaveReason_Cancel };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
            TestUI_Press(&Fixture.UI, Firsts[Index]);
            TestUI_Press(&Fixture.UI, Seconds[Index]);
            Tick(run, &Fixture);
            TEST_CHECK(run, WasLastLeft(&Fixture, A, Reasons[Index]) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == A));
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.UI.Input, Firsts[Index]) && GameInput_IsKeyConsumed(&Fixture.UI.Input, Seconds[Index]));
            TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.UI.Input, GameAction_Pause));
            TestUI_Release(&Fixture.UI, Firsts[Index]);
            TestUI_Release(&Fixture.UI, Seconds[Index]);
            Tick(run, &Fixture);
        }
        TEST_CHECK(run, CountEntries(&Fixture, B, ProbeEntry_Enter, -1) == 0U);

        // ui_previous on a key of its own and ui_next together: the first of them (back) acts, and both are consumed.
        uint64_t C = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 2);
        Tick(run, &Fixture);
        GameInputBinding PreviousOnQ = { .Key = GameKey_Q, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.UI.Input, GameAction_UIPrevious, &PreviousOnQ, 1U));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, B));
        TestUI_Press(&Fixture.UI, GameKey_Tab);
        TestUI_Press(&Fixture.UI, GameKey_Q);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == A) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A));
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.UI.Input, GameKey_Tab) && GameInput_IsKeyConsumed(&Fixture.UI.Input, GameKey_Q));
        TEST_CHECK(run, CountEntries(&Fixture, C, ProbeEntry_Enter, -1) == 0U);
        TestUI_Release(&Fixture.UI, GameKey_Tab);
        TestUI_Release(&Fixture.UI, GameKey_Q);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, GameInput_ResetActionBindings(&Fixture.UI.Input, GameAction_UIPrevious));
    }
    Deconstruct(run, &Fixture);
}

/* Tab leaves the entered element keeping and moves on at once, entering the next element when it is an edit element and only
 * focusing a button; Tab from what is only focused enters nothing. */
static void TabLeavesAndEntersOnlyTheNextEditElement(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t First = AddSlider(run, &Fixture, WIDGET_ID_INVALID, CellX(0), ROW_Y, 5.0);
        uint64_t Second = AddSlider(run, &Fixture, WIDGET_ID_INVALID, CellX(1), ROW_Y, 5.0);
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(2), ROW_Y);
        uint64_t Probe = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 3);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, First));
        TEST_CHECK(run, !UIScreen_IsNavigating(&Fixture.UI.Screen));

        // Out of the first and into the second at once: no first Tab that only shows the outlines.
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Second) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Second));
        TEST_CHECK(run, UIScreen_IsNavigating(&Fixture.UI.Screen));
        TEST_CHECK(run, (CountEnteredChanges(&Fixture, First, false) == 1U) && (CountEnteredChanges(&Fixture, Second, true) == 1U));
        // Its arrows are its own at once.
        Tap(run, &Fixture, GameKey_Right);
        TEST_CHECK(run, (GetValue(run, &Fixture, Second) == 6.0) && (GetValue(run, &Fixture, First) == 5.0));

        // On to a button: only focused. From there Tab enters nothing.
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Button) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Probe) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TEST_CHECK(run, CountEntries(&Fixture, Probe, ProbeEntry_Enter, -1) == 0U);

        // Shift+Tab out of an entered element goes back and enters the one before.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, Second));
        Fixture.UI.Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
        Tap(run, &Fixture, GameKey_Tab);
        Fixture.UI.Sample.KeyStates[GameKey_LeftShift] = 0U;
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == First) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == First));

        // With the button hidden the probe comes next: Tab enters it, and it hears that a key did.
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(GetWidget(&Fixture, Button), false));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, Second));
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Probe) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Probe));
        TEST_CHECK(run, (CountEntries(&Fixture, Probe, ProbeEntry_Enter, (int32_t)UIEnterCause_Key) == 1U)
            && (CountEntries(&Fixture, Probe, ProbeEntry_Enter, -1) == 1U));
        const uint64_t All[] = { First, Second, Button, Probe };
        CheckInvariants(run, &Fixture, All, 4U);
    }
    Deconstruct(run, &Fixture);
}


// Tests: held keys.
/* Holding Enter enters once and does not leave again; holding Tab from an entered element moves on with its repeats, entering
 * each edit element on the way, and no element hears those repeats. */
static void HoldingEnterOrTabDoesNothingMoreThanItsPress(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t A = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 0);
        uint64_t B = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 1);
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(2), ROW_Y);
        uint64_t Button2 = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(3), ROW_Y);
        uint64_t C = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 4);
        Tick(run, &Fixture);

        // An Enter held from before the focus came is no press of the element's: its repeats and release enter nothing.
        TestUI_Press(&Fixture.UI, GameKey_Enter);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, GetWidget(&Fixture, A)));
        TestUI_Repeat(&Fixture.UI, GameKey_Enter);
        Tick(run, &Fixture);
        TestUI_Release(&Fixture.UI, GameKey_Enter);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID) && (CountEntries(&Fixture, A, ProbeEntry_Enter, -1) == 0U));

        TestUI_Press(&Fixture.UI, GameKey_Enter);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A) && (CountEntries(&Fixture, A, ProbeEntry_Enter, (int32_t)UIEnterCause_Key) == 1U));
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            TestUI_Repeat(&Fixture.UI, GameKey_Enter);
            Tick(run, &Fixture);
        }
        TestUI_Release(&Fixture.UI, GameKey_Enter);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A) && (CountEntries(&Fixture, A, ProbeEntry_Leave, -1) == 0U));
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_KeyRepeat, -1) == 0U) && (CountEntries(&Fixture, A, ProbeEntry_KeyPress, (int32_t)GameKey_Enter) == 0U)
            && (CountEntries(&Fixture, A, ProbeEntry_KeyRelease, -1) == 0U));

        // A held Tab: the press is A's to hear first, then each repeat moves on; the buttons between are only focused, and the
        // edit element after them is entered still.
        TestUI_Press(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == B);
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_Leave, (int32_t)UILeaveReason_Keep) == 1U)
            && (CountEntries(&Fixture, B, ProbeEntry_Enter, (int32_t)UIEnterCause_Key) == 1U));
        TestUI_Repeat(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Button) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TestUI_Repeat(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Button2) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TestUI_Repeat(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == C) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == C));
        TestUI_Release(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_KeyPress, (int32_t)GameKey_Tab) == 1U);
        const uint64_t Probes[] = { A, B, C };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            TEST_CHECK(run, (CountEntries(&Fixture, Probes[Index], ProbeEntry_KeyRepeat, -1) == 0U)
                && (CountEntries(&Fixture, Probes[Index], ProbeEntry_KeyRelease, (int32_t)GameKey_Tab) == 0U));
        }
        TEST_CHECK(run, (CountEntries(&Fixture, B, ProbeEntry_KeyPress, -1) == 0U) && (CountEntries(&Fixture, C, ProbeEntry_KeyPress, -1) == 0U));

        // A new Tab press after the release is a press like any other: out of C, on round to A, entered.
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == A) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A));

        // The carry ended with its key: Tab held from a button, which is only focused, enters nothing on its way.
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, GetWidget(&Fixture, Button)));
        TestUI_Press(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Button2) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TestUI_Repeat(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == C) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TestUI_Release(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);

        // The carry is its own key's: another key going back in the same tick (ui_previous on Q) is plain navigation, and
        // enters nothing.
        GameInputBinding PreviousOnQ = { .Key = GameKey_Q, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.UI.Input, GameAction_UIPrevious, &PreviousOnQ, 1U));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TestUI_Press(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TestUI_Repeat(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Button);
        size_t EntersOfB = CountEntries(&Fixture, B, ProbeEntry_Enter, -1);
        TestUI_Repeat(&Fixture.UI, GameKey_Tab);
        TestUI_Press(&Fixture.UI, GameKey_Q);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Button) && (CountEntries(&Fixture, B, ProbeEntry_Enter, -1) == EntersOfB));
        TestUI_Release(&Fixture.UI, GameKey_Tab);
        TestUI_Release(&Fixture.UI, GameKey_Q);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, GameInput_ResetActionBindings(&Fixture.UI.Input, GameAction_UIPrevious));
        const uint64_t All[] = { A, B, Button, Button2, C };
        CheckInvariants(run, &Fixture, All, 5U);
    }
    Deconstruct(run, &Fixture);
}


// Tests: presses.
/* Any press enters an edit element before the press arrives; one that enters itself is only focused; moving the pointer
 * afterwards ends navigation but not the edit. */
static void AnyPressEntersBeforeItArrives(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t A = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 0);
        uint64_t B = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 1);
        uint64_t Self = AddProbe(run, &Fixture, Fixture.SelfEnteringType, WIDGET_ID_INVALID, CellX(2), ROW_Y, CELL_WIDTH, CELL_HEIGHT);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_EntersItself(GetWidget(&Fixture, Self)) && !Widget_EntersItself(GetWidget(&Fixture, A)));

        const GameKey Buttons[] = { GameKey_MouseLeft, GameKey_MouseRight, GameKey_MouseMiddle };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            uint64_t Target = ((Index % 2U) == 0U) ? A : B;
            ClickCell(run, &Fixture, ((Index % 2U) == 0U) ? 0 : 1, Buttons[Index]);
            TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Target);
            TEST_CHECK(run, CountEntries(&Fixture, Target, ProbeEntry_Enter, (int32_t)UIEnterCause_Press) == ((Index == 2U) ? 2U : 1U));
            // It saw itself entered at its press.
            size_t Presses = 0U;
            bool WasEnteredAtEach = true;
            for (size_t Entry = 0U; Entry < Fixture.Log.Count; Entry++)
            {
                if ((Fixture.Log.Entries[Entry].WidgetId == Target) && (Fixture.Log.Entries[Entry].Kind == ProbeEntry_Press))
                {
                    Presses++;
                    WasEnteredAtEach = WasEnteredAtEach && Fixture.Log.Entries[Entry].WasEntered;
                }
            }
            TEST_CHECK(run, (Presses > 0U) && WasEnteredAtEach);
        }

        // Moving the pointer after the press leaves nothing.
        TestUI_MovePointer(&Fixture.UI, 700.0f, 420.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A);

        // A press on what is entered already enters nothing again.
        size_t Enters = CountEntries(&Fixture, A, ProbeEntry_Enter, -1);
        ClickCell(run, &Fixture, 0, GameKey_MouseLeft);
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_Enter, -1) == Enters) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A));

        // One that enters itself is only focused by a press.
        ClickCell(run, &Fixture, 2, GameKey_MouseLeft);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Self) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TEST_CHECK(run, CountEntries(&Fixture, Self, ProbeEntry_Enter, -1) == 0U);
        TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_Leave, (int32_t)UILeaveReason_Keep) == 2U); // Left by B's press, then by this one.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, Self)); // It enters itself when it chooses.
        TEST_CHECK(run, CountEntries(&Fixture, Self, ProbeEntry_Enter, (int32_t)UIEnterCause_Code) == 1U);
        // Per widget: made to leave the entering to the press again, a press enters it.
        TEST_CHECK_SUCCESS(run, Widget_SetEntersItself(GetWidget(&Fixture, Self), false));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, NULL));
        ClickCell(run, &Fixture, 2, GameKey_MouseLeft);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == Self) && (CountEntries(&Fixture, Self, ProbeEntry_Enter, (int32_t)UIEnterCause_Press) == 1U));
        // And the other way round: an edit element made to enter itself is only focused by a press.
        TEST_CHECK_SUCCESS(run, Widget_SetEntersItself(GetWidget(&Fixture, B), true));
        ClickCell(run, &Fixture, 1, GameKey_MouseLeft);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == B) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TEST_CHECK_SUCCESS(run, Widget_SetEntersItself(GetWidget(&Fixture, B), false));

        // Moving the pointer ends navigation, not the edit.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        Tap(run, &Fixture, GameKey_Tab); // Out of A into B, navigating.
        TEST_CHECK(run, UIScreen_IsNavigating(&Fixture.UI.Screen) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == B));
        TestUI_MovePointer(&Fixture.UI, 600.0f, 450.0f);
        Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture.UI, 610.0f, 455.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsNavigating(&Fixture.UI.Screen) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == B));
        const uint64_t All[] = { A, B, Self };
        CheckInvariants(run, &Fixture, All, 3U);
    }
    Deconstruct(run, &Fixture);
}

/* Focus moving leaves keeping, however it moves: a press elsewhere or outside, hiding, disabling, taking the focus away,
 * detaching, an overlay, code; destroying leaves silently. */
static void FocusMovingLeavesKeepingAndDestroyingIsSilent(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t A = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 0);
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(1), ROW_Y);
        Tick(run, &Fixture);
        Widget* Base = GetWidget(&Fixture, A);

        // A press on a button elsewhere, a press on nothing.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        ClickCell(run, &Fixture, 1, GameKey_MouseLeft);
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Button));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TestUI_MovePointer(&Fixture.UI, 500.0f, 450.0f);
        TestUI_Press(&Fixture.UI, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TestUI_Release(&Fixture.UI, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));

        // Hiding, disabling, not focusable any more, code moving the focus.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Base, false));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(Base, true));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, false));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(Base, true));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(Base, false));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(Base, true));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, GetWidget(&Fixture, Button)));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, NULL));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep));

        // Detached, and an overlay.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.UI.Screen, Base));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID)
            && !Widget_IsEntered(Base) && !Widget_IsFocused(Base));
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.UI.Screen, Base));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(&Fixture.UI.Screen, Base, UIScreenLayer_Overlay));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        TEST_CHECK_ERROR(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A), ErrorCode_InvalidOperation); // An overlay takes no input.
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(&Fixture.UI.Screen, Base, UIScreenLayer_Normal));

        // Destroyed: no hook, and the screen forgets it at once.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        size_t Leaves = CountEntries(&Fixture, A, ProbeEntry_Leave, -1);
        Tick(run, &Fixture);
        size_t Changes = Fixture.States.Count;
        TEST_CHECK_SUCCESS(run, Widget_Destroy(Base));
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID));
        Tick(run, &Fixture);
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_Leave, -1) == Leaves) && (Fixture.States.Count == Changes));
        TEST_CHECK_SUCCESS(run, UIScreen_LeaveWidget(&Fixture.UI.Screen, UILeaveReason_Cancel)); // Nothing to leave.
    }
    Deconstruct(run, &Fixture);
}


// Tests: keys.
/* Focused but not entered, an edit element hears only the presses of the short list (reset, copy, paste, the menu key), none
 * of the other keys and no typed text: its ancestors hear the rest. Enter there is the screen's, at the element's place. */
static void OutsideItHearsOnlyTheShortList(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Container = AddContainer(run, &Fixture);
        uint64_t A = AddEditProbe(run, &Fixture, Container, 0);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, GetWidget(&Fixture, A)));

        Tap(run, &Fixture, GameKey_Backspace);
        Tap(run, &Fixture, GameKey_Menu);
        Fixture.UI.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
        Tap(run, &Fixture, GameKey_C);
        Tap(run, &Fixture, GameKey_V);
        Fixture.UI.Sample.KeyStates[GameKey_LeftControl] = 0U;
        const GameKey Heard[] = { GameKey_Backspace, GameKey_Menu, GameKey_C, GameKey_V };
        for (size_t Index = 0U; Index < 4U; Index++)
        {
            TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_KeyPress, (int32_t)Heard[Index]) == 1U);
            TEST_CHECK(run, CountEntries(&Fixture, Container, ProbeEntry_KeyPress, (int32_t)Heard[Index]) == 1U); // It did not take them.
        }

        // The rest pass it by: plain letters (a C with no Ctrl too), the arrows, Tab, Home, Space; and typed text.
        const GameKey Passed[] = { GameKey_A, GameKey_C, GameKey_Left, GameKey_Down, GameKey_Home, GameKey_Space, GameKey_Tab };
        for (size_t Index = 0U; Index < (sizeof(Passed) / sizeof(Passed[0])); Index++)
        {
            size_t Before = CountEntries(&Fixture, A, ProbeEntry_KeyPress, -1);
            Tap(run, &Fixture, Passed[Index]);
            TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_KeyPress, -1) == Before);
            TEST_CHECK(run, CountEntries(&Fixture, Container, ProbeEntry_KeyPress, (int32_t)Passed[Index]) >= 1U);
            TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_KeyRelease, (int32_t)Passed[Index]) == 0U);
        }
        Fixture.UI.Sample.TextCodepoints[0] = 'x';
        Fixture.UI.Sample.TextCodepointCount = 1U;
        Tick(run, &Fixture);
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_Text, -1) == 0U) && (CountEntries(&Fixture, Container, ProbeEntry_Text, 'x') == 1U));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID);

        // Enter enters it, and neither it nor what holds it hears that press. Inside, it hears every key and the text first.
        GetProbe(&Fixture, Container)->HandledKey = GameKey_Enter;
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A);
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_KeyPress, (int32_t)GameKey_Enter) == 0U)
            && (CountEntries(&Fixture, Container, ProbeEntry_KeyPress, (int32_t)GameKey_Enter) == 0U));
        Tap(run, &Fixture, GameKey_A);
        Fixture.UI.Sample.TextCodepoints[0] = 'y';
        Fixture.UI.Sample.TextCodepointCount = 1U;
        Tick(run, &Fixture);
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_KeyPress, (int32_t)GameKey_A) == 1U) && (CountEntries(&Fixture, A, ProbeEntry_Text, 'y') == 1U));
    }
    Deconstruct(run, &Fixture);
}

/* Inside, the element hears every key first and its ancestors hear what it leaves, before the screen's leave: an ancestor taking
 * Enter keeps the element entered, as a dialog's default button would. */
static void InsideItsAncestorsHearWhatItLeavesFirst(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Container = AddContainer(run, &Fixture);
        uint64_t A = AddEditProbe(run, &Fixture, Container, 0);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        GetProbe(&Fixture, Container)->HandledKey = GameKey_Enter;
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_KeyPress, (int32_t)GameKey_Enter) == 1U)
            && (CountEntries(&Fixture, Container, ProbeEntry_KeyPress, (int32_t)GameKey_Enter) == 1U));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A);

        // What neither takes is the screen's: Escape leaves with a cancel, after both heard it.
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (CountEntries(&Fixture, A, ProbeEntry_KeyPress, (int32_t)GameKey_Escape) == 1U)
            && (CountEntries(&Fixture, Container, ProbeEntry_KeyPress, (int32_t)GameKey_Escape) == 1U));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Cancel));
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == WIDGET_ID_INVALID) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == A));

        // The element itself can take Escape (a slider's held key does): it stays entered.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(&Fixture.UI.Screen, A));
        GetProbe(&Fixture, A)->HandledKey = GameKey_Escape;
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A);
        GetProbe(&Fixture, A)->HandledKey = GameKey_Unknown;

        // Enter that neither takes leaves keeping; the arrows nobody takes do nothing and never reach the game.
        GetProbe(&Fixture, Container)->HandledKey = GameKey_Unknown;
        TestUI_Press(&Fixture.UI, GameKey_Up);
        Tick(run, &Fixture);
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.UI.Input, GameKey_Up) && (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == A));
        TestUI_Repeat(&Fixture.UI, GameKey_Up);
        Tick(run, &Fixture);
        TestUI_Release(&Fixture.UI, GameKey_Up);
        Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsNavigating(&Fixture.UI.Screen));
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == A));
    }
    Deconstruct(run, &Fixture);
}

/* UIScreen_EnterWidget enters an edit element and focuses it; it refuses what is no edit element or cannot take focus; it works
 * from a handler and from a hook; leaving by code takes a reason. */
static void CodeEntersOnlyAnEditElement(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t A = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 0);
        uint64_t B = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 1);
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(2), ROW_Y);
        Tick(run, &Fixture);
        UIScreen* Screen = &Fixture.UI.Screen;

        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(Screen) == A) && (UIScreen_GetFocusedWidgetId(Screen) == A) && Widget_IsEntered(GetWidget(&Fixture, A)));
        TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_Enter, (int32_t)UIEnterCause_Code) == 1U);
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_Enter, -1) == 1U); // Entered already: nothing again.
        Tick(run, &Fixture);
        TEST_CHECK(run, CountEnteredChanges(&Fixture, A, true) == 1U);

        // Refused: a button, a hidden edit element, a stale id; nothing changes.
        TEST_CHECK_ERROR(run, UIScreen_EnterWidget(Screen, Button), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(GetWidget(&Fixture, B), false));
        TEST_CHECK_ERROR(run, UIScreen_EnterWidget(Screen, B), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScreen_EnterWidget(Screen, 12345U), ErrorCode_InvalidOperation);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(Screen) == A);
        TEST_CHECK_ERROR(run, UIScreen_EnterWidget(NULL, A), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_LeaveWidget(NULL, UILeaveReason_Keep), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_LeaveWidget(Screen, (UILeaveReason)7), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(Screen) == A);

        // Into another: the first is left, keeping.
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(GetWidget(&Fixture, B), true));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, B));
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(Screen) == B) && (CountEntries(&Fixture, A, ProbeEntry_Leave, (int32_t)UILeaveReason_Keep) == 1U));
        TEST_CHECK_SUCCESS(run, UIScreen_LeaveWidget(Screen, UILeaveReason_Cancel));
        TEST_CHECK(run, WasLastLeft(&Fixture, B, UILeaveReason_Cancel) && (UIScreen_GetFocusedWidgetId(Screen) == B));

        // From a hook: a widget focused that enters itself, as a console's line does when it opens.
        ProbeWidget* Probe = GetProbe(&Fixture, A);
        Probe->OnFocusAction = ProbeAction_EnterTarget;
        Probe->ActionTargetId = A;
        ClickCell(run, &Fixture, 2, GameKey_MouseLeft);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, GetWidget(&Fixture, A)));
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(Screen) == A) && (CountEntries(&Fixture, A, ProbeEntry_Enter, (int32_t)UIEnterCause_Code) == 2U));

        // Made no edit element while entered, it is left, keeping.
        TEST_CHECK_SUCCESS(run, Widget_SetNeedsEntering(GetWidget(&Fixture, A), false));
        TEST_CHECK(run, WasLastLeft(&Fixture, A, UILeaveReason_Keep) && (UIScreen_GetEnteredWidgetId(Screen) == WIDGET_ID_INVALID)
            && (UIScreen_GetFocusedWidgetId(Screen) == A));
        TEST_CHECK_ERROR(run, UIScreen_EnterWidget(Screen, A), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, Widget_SetNeedsEntering(GetWidget(&Fixture, A), true));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        TEST_CHECK_SUCCESS(run, Widget_SetNeedsEntering(GetWidget(&Fixture, B), false)); // Another widget's flag leaves nothing.
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(Screen) == A);
        TEST_CHECK_SUCCESS(run, Widget_SetNeedsEntering(GetWidget(&Fixture, B), true));
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(Screen) == A);
        const uint64_t All[] = { A, B, Button };
        CheckInvariants(run, &Fixture, All, 3U);
    }
    Deconstruct(run, &Fixture);
}

/* An element that keeps its keys while entered: Tab, the arrows, Enter and Escape stay unconsumed for its reader and move
 * nothing; a press elsewhere leaves it, and navigation works again. */
static void KeptKeysStayForTheirReader(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t View = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 0);
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(1), ROW_Y);
        uint64_t Other = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(2), ROW_Y);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, Widget_SetKeepsKeysWhenEntered(GetWidget(&Fixture, View), true));
        TEST_CHECK(run, Widget_KeepsKeysWhenEntered(GetWidget(&Fixture, View)));
        ClickCell(run, &Fixture, 0, GameKey_MouseLeft);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == View);

        const GameKey Kept[] = { GameKey_Tab, GameKey_Left, GameKey_Right, GameKey_Up, GameKey_Down, GameKey_Enter, GameKey_Escape };
        for (size_t Index = 0U; Index < (sizeof(Kept) / sizeof(Kept[0])); Index++)
        {
            TestUI_Press(&Fixture.UI, Kept[Index]);
            Tick(run, &Fixture);
            TEST_CHECK(run, GameInput_IsKeyPressed(&Fixture.UI.Input, Kept[Index]));
            TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(&Fixture.UI.Screen) == View) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == View));
            TEST_CHECK(run, CountEntries(&Fixture, View, ProbeEntry_KeyPress, (int32_t)Kept[Index]) == 1U); // It heard each first.
            TestUI_Release(&Fixture.UI, Kept[Index]);
            Tick(run, &Fixture);
        }
        TEST_CHECK(run, !UIScreen_IsNavigating(&Fixture.UI.Screen));

        // A press elsewhere leaves it; then Tab navigates again.
        ClickCell(run, &Fixture, 1, GameKey_MouseLeft);
        TEST_CHECK(run, WasLastLeft(&Fixture, View, UILeaveReason_Keep) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Button));
        Tap(run, &Fixture, GameKey_Tab);
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, UIScreen_IsNavigating(&Fixture.UI.Screen) && (UIScreen_GetFocusedWidgetId(&Fixture.UI.Screen) == Other));
    }
    Deconstruct(run, &Fixture);
}


// Tests: hooks.
/* Hooks may move the focus, destroy or enter something else from OnEnter and OnLeave: the latest change wins, and the screen's
 * promises hold after each. */
static void HooksMayMoveFocusDestroyOrEnterFromEnterAndLeave(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t A = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 0);
        uint64_t B = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 1);
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(2), ROW_Y);
        uint64_t Doomed = AddEditProbe(run, &Fixture, WIDGET_ID_INVALID, 3);
        Tick(run, &Fixture);
        UIScreen* Screen = &Fixture.UI.Screen;
        const uint64_t All[] = { A, B, Button, Doomed };

        // A focus hook moves the focus on before the element is entered: only the focused widget is entered, so nothing is, by a
        // press or by code.
        GetProbe(&Fixture, A)->OnFocusAction = ProbeAction_FocusTarget;
        GetProbe(&Fixture, A)->ActionTargetId = Button;
        ClickCell(run, &Fixture, 0, GameKey_MouseLeft);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == Button) && (UIScreen_GetEnteredWidgetId(Screen) == WIDGET_ID_INVALID));
        GetProbe(&Fixture, A)->OnFocusAction = ProbeAction_FocusTarget;
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == Button) && (UIScreen_GetEnteredWidgetId(Screen) == WIDGET_ID_INVALID));
        TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_Enter, -1) == 0U);
        CheckInvariants(run, &Fixture, All, 4U);

        // OnEnter moves the focus on: the element is left again, keeping, and the button keeps the focus.
        GetProbe(&Fixture, A)->OnEnterAction = ProbeAction_FocusTarget;
        GetProbe(&Fixture, A)->ActionTargetId = Button;
        ClickCell(run, &Fixture, 0, GameKey_MouseLeft);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == Button) && (UIScreen_GetEnteredWidgetId(Screen) == WIDGET_ID_INVALID));
        TEST_CHECK(run, CountEntries(&Fixture, A, ProbeEntry_Leave, (int32_t)UILeaveReason_Keep) == 1U);
        CheckInvariants(run, &Fixture, All, 4U);

        // OnEnter enters something else: that one is entered.
        GetProbe(&Fixture, A)->OnEnterAction = ProbeAction_EnterTarget;
        GetProbe(&Fixture, A)->ActionTargetId = B;
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(Screen) == B) && (UIScreen_GetFocusedWidgetId(Screen) == B));
        CheckInvariants(run, &Fixture, All, 4U);

        // OnEnter clears the focus: nothing is entered. (B is left first, so A is only focused.)
        GetProbe(&Fixture, A)->OnEnterAction = ProbeAction_ClearFocus;
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, GetWidget(&Fixture, A)));
        Tap(run, &Fixture, GameKey_Enter);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID) && (UIScreen_GetEnteredWidgetId(Screen) == WIDGET_ID_INVALID));
        CheckInvariants(run, &Fixture, All, 4U);

        // OnLeave moves the focus: on a cancel by Escape, the hook's focus stays.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        GetProbe(&Fixture, A)->OnLeaveAction = ProbeAction_FocusTarget;
        GetProbe(&Fixture, A)->ActionTargetId = Button;
        Tap(run, &Fixture, GameKey_Escape);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == Button) && (UIScreen_GetEnteredWidgetId(Screen) == WIDGET_ID_INVALID));
        CheckInvariants(run, &Fixture, All, 4U);

        // OnLeave enters something else while a press moves the focus elsewhere: the hook's change wins.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        GetProbe(&Fixture, A)->OnLeaveAction = ProbeAction_EnterTarget;
        GetProbe(&Fixture, A)->ActionTargetId = B;
        ClickCell(run, &Fixture, 2, GameKey_MouseLeft);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == B) && (UIScreen_GetEnteredWidgetId(Screen) == B));
        CheckInvariants(run, &Fixture, All, 4U);

        // OnLeave focuses the widget it leaves again: it keeps the focus, and is not entered.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        GetProbe(&Fixture, A)->OnLeaveAction = ProbeAction_FocusTarget;
        GetProbe(&Fixture, A)->ActionTargetId = A;
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, GetWidget(&Fixture, Button)));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == A) && (UIScreen_GetEnteredWidgetId(Screen) == WIDGET_ID_INVALID));
        TEST_CHECK(run, !Widget_IsFocused(GetWidget(&Fixture, Button)));
        CheckInvariants(run, &Fixture, All, 4U);

        // OnLeave enters the widget it leaves again: it is entered.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        GetProbe(&Fixture, A)->OnLeaveAction = ProbeAction_EnterTarget;
        GetProbe(&Fixture, A)->ActionTargetId = A;
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, GetWidget(&Fixture, Button)));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == A) && (UIScreen_GetEnteredWidgetId(Screen) == A));
        CheckInvariants(run, &Fixture, All, 4U);

        // OnEnter destroys the widget: nothing is entered, and the press it was entered for reaches nobody.
        GetProbe(&Fixture, Doomed)->OnEnterAction = ProbeAction_DestroySelf;
        ClickCell(run, &Fixture, 3, GameKey_MouseLeft);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(Screen, Doomed) && (UIScreen_GetEnteredWidgetId(Screen) == WIDGET_ID_INVALID)
            && (UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID));
        TEST_CHECK(run, CountEntries(&Fixture, Doomed, ProbeEntry_Press, -1) == 0U);
        CheckInvariants(run, &Fixture, All, 4U);

        // OnLeave clears the focus as Tab leaves: navigation starts where nothing is focused, on the first element, which counts as
        // the step, and that edit element is entered.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, B));
        GetProbe(&Fixture, B)->OnLeaveAction = ProbeAction_ClearFocus;
        Tap(run, &Fixture, GameKey_Tab);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == A) && (UIScreen_GetEnteredWidgetId(Screen) == A));
        CheckInvariants(run, &Fixture, All, 4U);
        TestUI_MovePointer(&Fixture.UI, 990.0f, 490.0f); // Navigation ends.
        Tick(run, &Fixture);

        // OnLeave destroys the widget it leaves, as focus moves on: the move goes on.
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, B));
        GetProbe(&Fixture, B)->OnLeaveAction = ProbeAction_DestroySelf;
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, GetWidget(&Fixture, Button)));
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(Screen, B) && (UIScreen_GetFocusedWidgetId(Screen) == Button));
        CheckInvariants(run, &Fixture, All, 4U);
        Tick(run, &Fixture);

        // A Tab whose leave hook took away everything it could go to still left the element: it is used, not the game's.
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(GetWidget(&Fixture, Button), false));
        TEST_CHECK_SUCCESS(run, UIScreen_EnterWidget(Screen, A));
        GetProbe(&Fixture, A)->OnLeaveAction = ProbeAction_DestroySelf;
        TestUI_Press(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(Screen, A) && GameInput_IsKeyConsumed(&Fixture.UI.Input, GameKey_Tab));
        TestUI_Release(&Fixture.UI, GameKey_Tab);
        Tick(run, &Fixture);
    }
    Deconstruct(run, &Fixture);
}


// Tests: showing it.
/* Entering and leaving are told (EnteredChanged, before the focus's own change when focus moves), the slider shows its entered
 * state, and a theme that leaves the entered state out shows the focused one. */
static void TheEnteredStateIsToldAndShown(TestRun* run)
{
    EnteringFixture Fixture;
    if (Construct(run, &Fixture))
    {
        uint64_t Slider = AddSlider(run, &Fixture, WIDGET_ID_INVALID, CellX(0), ROW_Y, 5.0);
        uint64_t Button = AddButton(run, &Fixture, WIDGET_ID_INVALID, CellX(1), ROW_Y);
        Tick(run, &Fixture);
        ClickCell(run, &Fixture, 0, GameKey_MouseLeft);
        TestUI_RemovePointer(&Fixture.UI);
        Tick(run, &Fixture);
        Widget* Base = GetWidget(&Fixture, Slider);
        const UIElementLook* Look = Widget_GetCapability(Base, UIKit_GetCapabilityId(&Fixture.UI.Kit, UIElementCapability_Look));
        TEST_CHECK(run, (Look != NULL) && (Look->_state == UIElementState_Entered));
        TEST_CHECK(run, CountEnteredChanges(&Fixture, Slider, true) == 1U);

        // Focus moving: the entered change comes before the focus's.
        Fixture.States.Count = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, GetWidget(&Fixture, Button)));
        Tick(run, &Fixture);
        TEST_CHECK(run, (Fixture.States.Count >= 3U) && (Fixture.States.Changes[0].Kind == UIEventKind_EnteredChanged) && !Fixture.States.Changes[0].IsSet
            && (Fixture.States.Changes[0].WidgetId == Slider) && (Fixture.States.Changes[1].Kind == UIEventKind_FocusChanged)
            && (Fixture.States.Changes[1].WidgetId == Slider));
        TEST_CHECK(run, (Look->_state == UIElementState_Normal) || (Look->_state == UIElementState_Hovered));

        // Focused only, it shows focused.
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.UI.Screen, Base));
        Tick(run, &Fixture);
        TEST_CHECK(run, Look->_state == UIElementState_Focused);

        // The colours: entered's own, else focused's, else normal's, else the fallback; the art's rectangle alike.
        UIStateColors Colors;
        Memory_Zero(&Colors, sizeof(Colors));
        Color Fallback = { 1, 2, 3, 4 };
        TEST_CHECK(run, ColorIsEqual(UIStateColors_Resolve(&Colors, UIElementState_Entered, Fallback), Fallback));
        Colors.Colors[UIElementState_Normal] = (Color){ 10, 10, 10, 255 };
        Colors.HasColor[UIElementState_Normal] = true;
        TEST_CHECK(run, UIStateColors_Resolve(&Colors, UIElementState_Entered, Fallback).r == 10);
        Colors.Colors[UIElementState_Focused] = (Color){ 20, 20, 20, 255 };
        Colors.HasColor[UIElementState_Focused] = true;
        TEST_CHECK(run, UIStateColors_Resolve(&Colors, UIElementState_Entered, Fallback).r == 20);
        Colors.Colors[UIElementState_Entered] = (Color){ 30, 30, 30, 255 };
        Colors.HasColor[UIElementState_Entered] = true;
        TEST_CHECK(run, UIStateColors_Resolve(&Colors, UIElementState_Entered, Fallback).r == 30);
        TEST_CHECK(run, UIStateColors_Resolve(&Colors, UIElementState_Hovered, Fallback).r == 10); // Only entered falls back to focused.
        UIBoxStyle Box;
        Memory_Zero(&Box, sizeof(Box));
        Box.Sources[UIElementState_Normal] = (Rectangle){ 0.0f, 0.0f, 8.0f, 8.0f };
        Box.HasSource[UIElementState_Normal] = true;
        Box.Sources[UIElementState_Focused] = (Rectangle){ 8.0f, 0.0f, 8.0f, 8.0f };
        Box.HasSource[UIElementState_Focused] = true;
        TEST_CHECK(run, UIBoxStyle_Resolve(&Box, UIElementState_Entered).Source.x == 8.0f);
        TEST_CHECK(run, (UIElementState_GetName(UIElementState_Entered) != NULL) && (UIElementState_GetName(UIElementState_Entered)[0] == 'e'));
    }
    Deconstruct(run, &Fixture);
}


// Public functions.
void UIEnteringTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "arrows_and_tab_move_past_a_slider_and_change_nothing", ArrowsAndTabMovePastASliderAndChangeNothing },
        { "enter_enters_a_slider_whose_arrows_each_commit", EnterEntersASliderWhoseArrowsEachCommit },
        { "leaving_a_slider_ends_its_change", LeavingASliderEndsItsChange },
        { "leave_keys_in_one_tick_all_stay_the_screens", LeaveKeysInOneTickAllStayTheScreens },
        { "tab_leaves_and_enters_only_the_next_edit_element", TabLeavesAndEntersOnlyTheNextEditElement },
        { "holding_enter_or_tab_does_nothing_more_than_its_press", HoldingEnterOrTabDoesNothingMoreThanItsPress },
        { "any_press_enters_before_it_arrives", AnyPressEntersBeforeItArrives },
        { "focus_moving_leaves_keeping_and_destroying_is_silent", FocusMovingLeavesKeepingAndDestroyingIsSilent },
        { "outside_it_hears_only_the_short_list", OutsideItHearsOnlyTheShortList },
        { "inside_its_ancestors_hear_what_it_leaves_first", InsideItsAncestorsHearWhatItLeavesFirst },
        { "code_enters_only_an_edit_element", CodeEntersOnlyAnEditElement },
        { "kept_keys_stay_for_their_reader", KeptKeysStayForTheirReader },
        { "hooks_may_move_focus_destroy_or_enter_from_enter_and_leave", HooksMayMoveFocusDestroyOrEnterFromEnterAndLeave },
        { "the_entered_state_is_told_and_shown", TheEnteredStateIsToldAndShown },
    };
    TestRun_RunSuite(run, "UIEntering", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
