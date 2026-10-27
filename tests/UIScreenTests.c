#include "TestSuites.h"
#include "UIScreen.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "UIAnimation.h"
#include "GameInput.h"
#include "Logger.h"
#include "ProgramTime.h"
#include "wr/WRCompile.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include "wr/WRUserData.h"
#include <math.h>
#include <string.h>


// Macros.
/** The fixed tick the tests advance by, in seconds (the game's update rate). */
#define TEST_TICK_SECONDS (1.0 / 480.0)
/** How many teardown ids the probe log keeps. */
#define PROBE_LOG_CAPACITY ((size_t)128)
/** The probe's own typed property. */
#define PROBE_PROPERTY_VALUE ((int32_t)0)
/** How many notifications a test handler keeps, in order. */
#define HANDLER_EVENT_CAPACITY ((size_t)16)
/** Initial capacity (bytes) of path and text buffers; they grow as needed. */
#define UI_TEST_BUFFER_CAPACITY ((size_t)256)


// Types.
/* What a probe does inside a hook. */
typedef enum ProbeActionEnum
{
    ProbeAction_None,
    ProbeAction_DestroySelf,
    ProbeAction_DestroyTarget,
    ProbeAction_FocusTarget,
    /* Removes the probe from its parent, or from the roots. */
    ProbeAction_DetachSelf,
    /* Removes the target from its parent, or from the roots. */
    ProbeAction_DetachTarget
} ProbeAction;

/* Teardown hook calls of every probe, kept outside the probes (their memory is gone after the flush). */
typedef struct ProbeLogStruct
{
    uint64_t DeconstructIds[PROBE_LOG_CAPACITY];
    size_t DeconstructCount;
    uint64_t DeinitializeIds[PROBE_LOG_CAPACITY];
    size_t DeinitializeCount;
    /* What UIScreen_Deconstruct answered when a probe called it from its own teardown. */
    ErrorCode NestedDeconstructCode;
} ProbeLog;

/* A widget type that records its hooks and can run a scripted action inside them. */
typedef struct ProbeWidgetStruct
{
    Widget Base;
    ProbeLog* Log;
    bool IsHandlingKeys;
    bool IsValueSetterFailing;
    float Value;
    ProbeAction OnFocusEnableAction;
    ProbeAction OnFocusDisableAction;
    ProbeAction OnHoverStartAction;
    ProbeAction OnTabbedEnableAction;
    uint64_t ActionTargetId;
    bool IsDeconstructingTheScreen;
    /* Read in Update: shows whether notification handlers ran before the widget updates. */
    const int* WatchedCounter;
    int WatchedAtUpdate;

    int HoverStartCount;
    int HoverEndCount;
    int FocusEnableCount;
    int FocusDisableCount;
    int TabbedEnableCount;
    int TabbedDisableCount;
    int PressCount;
    int ReleaseCount;
    int MoveCount;
    int ScrollCount;
    int KeyEventCount;
    GameKey LastKey;
    UIKeyboardInputType LastKeyType;
    int TextCount;
    int UpdateCount;
    int LayoutCount;
} ProbeWidget;

/* The probe constructor's arguments. */
typedef struct ProbeArgsStruct
{
    ProbeLog* Log;
    bool IsConstructorFailing;
    /* The constructor tries to unregister its own type and writes the answer to UnregisterCode. */
    bool IsUnregisteringItsType;
    ErrorCode UnregisterCode;
} ProbeArgs;

/* A screen with the probe type, an input and a fixed tick. */
typedef struct UIFixtureStruct
{
    UIWidgetFactory Factory;
    UIScreen Screen;
    GameInput Input;
    GameInputSample Sample;
    UIScreenInput ScreenInput;
    ProbeLog Log;
    uint64_t ProbeType;
    double Time;
    bool IsScreenConstructed;
    bool IsReady;
} UIFixture;

/* What a test notification handler saw. */
typedef struct HandlerRecordStruct
{
    int CallCount;
    UIEvent LastEvent;
    /* The first HANDLER_EVENT_CAPACITY notifications, in delivery order. */
    UIEvent Events[HANDLER_EVENT_CAPACITY];
    size_t EventCount;
    uint64_t SubscriptionId;
    bool IsDestroyingTheWidget;
    bool IsReposting;
    bool IsUnsubscribing;
} HandlerRecord;


// Static functions: the probe widget.
static Error DetachWidget(UIScreen* screen, Widget* widget)
{
    return Widget_IsScreenRoot(widget) ? UIScreen_RemoveWidget(screen, widget) : Widget_RemoveSubWidget(Widget_GetParent(widget), widget);
}

static Error RunProbeAction(ProbeWidget* probe, ProbeAction action)
{
    UIScreen* Screen = Widget_GetScreen(&probe->Base);
    Widget* Target = NULL;
    switch (action)
    {
        case ProbeAction_None:
            return Error_CreateSuccess();
        case ProbeAction_DestroySelf:
            return Widget_Destroy(&probe->Base);
        case ProbeAction_DestroyTarget:
            return UIScreen_TryGetWidgetById(Screen, probe->ActionTargetId, &Target) ? Widget_Destroy(Target) : Error_CreateSuccess();
        case ProbeAction_FocusTarget:
            return UIScreen_TryGetWidgetById(Screen, probe->ActionTargetId, &Target) ? UIScreen_SetFocusedWidget(Screen, Target)
                : Error_CreateSuccess();
        case ProbeAction_DetachSelf:
            return DetachWidget(Screen, &probe->Base);
        case ProbeAction_DetachTarget:
            return UIScreen_TryGetWidgetById(Screen, probe->ActionTargetId, &Target) ? DetachWidget(Screen, Target) : Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static void LogId(uint64_t* ids, size_t* count, uint64_t id)
{
    if (*count < PROBE_LOG_CAPACITY)
    {
        ids[*count] = id;
        (*count)++;
    }
}

static Error Probe_Deinitialize(void* self)
{
    ProbeWidget* Probe = self;
    LogId(Probe->Log->DeinitializeIds, &Probe->Log->DeinitializeCount, Widget_GetId(&Probe->Base));
    return Error_CreateSuccess();
}

static Error Probe_OnDeconstruct(void* self)
{
    ProbeWidget* Probe = self;
    LogId(Probe->Log->DeconstructIds, &Probe->Log->DeconstructCount, Widget_GetId(&Probe->Base));
    if (Probe->IsDeconstructingTheScreen)
    {
        Error Nested = UIScreen_Deconstruct(Widget_GetScreen(&Probe->Base));
        Probe->Log->NestedDeconstructCode = Nested.Code;
        Error_Deconstruct(&Nested);
    }
    return Error_CreateSuccess();
}

static Error Probe_Update(void* self, ProgramTime time)
{
    UNUSED(time);
    ProbeWidget* Probe = self;
    Probe->UpdateCount++;
    if (Probe->WatchedCounter != NULL)
    {
        Probe->WatchedAtUpdate = *Probe->WatchedCounter;
    }
    return Error_CreateSuccess();
}

static Error Probe_Layout(void* self, const UILayoutContext* context)
{
    UNUSED(context);
    ((ProbeWidget*)self)->LayoutCount++;
    return Error_CreateSuccess();
}

static Error Probe_OnHoverStart(void* self, const WidgetHoverArgs* args)
{
    UNUSED(args);
    ProbeWidget* Probe = self;
    Probe->HoverStartCount++;
    return RunProbeAction(Probe, Probe->OnHoverStartAction);
}

static Error Probe_OnHoverEnd(void* self)
{
    ((ProbeWidget*)self)->HoverEndCount++;
    return Error_CreateSuccess();
}

static Error Probe_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    ProbeWidget* Probe = self;
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:   Probe->PressCount++; break;
        case UIMouseInputType_ButtonRelease: Probe->ReleaseCount++; break;
        case UIMouseInputType_Move:          Probe->MoveCount++; break;
        case UIMouseInputType_Scroll:        Probe->ScrollCount++; break;
    }
    return Error_CreateSuccess();
}

static Error Probe_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    ProbeWidget* Probe = self;
    if (args->Type == UIKeyboardInputType_Text)
    {
        Probe->TextCount++;
    }
    else
    {
        Probe->KeyEventCount++;
        Probe->LastKey = args->Key;
        Probe->LastKeyType = args->Type;
    }
    *outIsHandled = Probe->IsHandlingKeys;
    return Error_CreateSuccess();
}

static Error Probe_OnFocusEnable(void* self)
{
    ProbeWidget* Probe = self;
    Probe->FocusEnableCount++;
    return RunProbeAction(Probe, Probe->OnFocusEnableAction);
}

static Error Probe_OnFocusDisable(void* self)
{
    ProbeWidget* Probe = self;
    Probe->FocusDisableCount++;
    return RunProbeAction(Probe, Probe->OnFocusDisableAction);
}

static Error Probe_OnTabbedEnable(void* self)
{
    ProbeWidget* Probe = self;
    Probe->TabbedEnableCount++;
    return RunProbeAction(Probe, Probe->OnTabbedEnableAction);
}

static Error Probe_OnTabbedDisable(void* self)
{
    ((ProbeWidget*)self)->TabbedDisableCount++;
    return Error_CreateSuccess();
}

static Error Probe_GetProperty(void* self, int32_t propertyId, UIPropertyValue* outValue)
{
    UNUSED(propertyId);
    outValue->Data.FloatValue = ((ProbeWidget*)self)->Value;
    return Error_CreateSuccess();
}

static Error Probe_SetProperty(void* self, int32_t propertyId, const UIPropertyValue* value)
{
    UNUSED(propertyId);
    ProbeWidget* Probe = self;
    if (Probe->IsValueSetterFailing)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "probe: the value is refused.");
    }
    Probe->Value = value->Data.FloatValue;
    return Error_CreateSuccess();
}

static Error Probe_Construct(void* widget, void* args)
{
    ProbeWidget* Probe = widget;
    ProbeArgs* Arguments = args;
    if (Arguments->IsConstructorFailing)
    {
        return Error_Construct2(ErrorCode_InvalidState, "probe: told to fail.");
    }
    if (Arguments->IsUnregisteringItsType)
    {
        Error Result = UIWidgetFactory_UnregisterType(UIScreen_GetFactory(Widget_GetScreen(&Probe->Base)),
            Widget_GetTypeId(&Probe->Base));
        Arguments->UnregisterCode = Result.Code;
        Error_Deconstruct(&Result);
    }
    Probe->Log = Arguments->Log;
    return Error_CreateSuccess();
}

static const WidgetVTable ProbeVTable =
{
    .Deinitialize = Probe_Deinitialize,
    .OnDeconstruct = Probe_OnDeconstruct,
    .Update = Probe_Update,
    .Layout = Probe_Layout,
    .OnHoverStart = Probe_OnHoverStart,
    .OnHoverEnd = Probe_OnHoverEnd,
    .OnMouseInput = Probe_OnMouseInput,
    .OnKeyboardInput = Probe_OnKeyboardInput,
    .OnFocusEnable = Probe_OnFocusEnable,
    .OnFocusDisable = Probe_OnFocusDisable,
    .OnTabbedEnable = Probe_OnTabbedEnable,
    .OnTabbedDisable = Probe_OnTabbedDisable,
    .GetProperty = Probe_GetProperty,
    .SetProperty = Probe_SetProperty
};

static const WidgetPropertyDescriptor ProbeProperties[] =
{
    { .Id = PROBE_PROPERTY_VALUE, .Type = UIPropertyType_Float, .Name = u8"value" },
};


// Static functions: the fixture.
/* Builds the fixture; its screen reports to @p logger (may be NULL). */
static bool Fixture_ConstructLogged(TestRun* run, UIFixture* self, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"Probe",
        .StructSize = sizeof(ProbeWidget),
        .VTable = &ProbeVTable,
        .Constructor = Probe_Construct,
        .Properties = ProbeProperties,
        .PropertyCount = 1U,
        .IsFocusable = false
    };
    bool IsBuilt = TEST_CHECK_SUCCESS(run, GameInput_Construct(&self->Input))
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_Construct(&self->Factory))
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&self->Factory, &Descriptor, &self->ProbeType));
    self->IsScreenConstructed = IsBuilt && TEST_CHECK_SUCCESS(run, UIScreen_Construct(&self->Screen, &self->Factory, logger));
    GameInputSample_Clear(&self->Sample);
    self->Sample.WindowSize = (Vector2){ .x = 1280.0f, .y = 720.0f };
    self->ScreenInput = (UIScreenInput)
    {
        .Input = &self->Input,
        .IsPointerPresent = false,
        .PointerPosition = (Vector2){ .x = 0.0f, .y = 0.0f },
        .HasKeyboardFocus = true
    };
    self->IsReady = self->IsScreenConstructed;
    return self->IsReady;
}

static bool Fixture_Construct(TestRun* run, UIFixture* self)
{
    return Fixture_ConstructLogged(run, self, NULL);
}

static void Fixture_Deconstruct(TestRun* run, UIFixture* self)
{
    if (self->IsScreenConstructed)
    {
        TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&self->Screen));
    }
    TEST_CHECK(run, UIWidgetFactory_GetTotalLiveWidgetCount(&self->Factory) == 0U);
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_Deconstruct(&self->Factory));
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&self->Input));
}

/* Creates an initialized, detached probe with a box (parent-relative). */
static ProbeWidget* CreateProbe(TestRun* run, UIFixture* self, Rectangle box, bool isFocusable)
{
    ProbeArgs Arguments = { .Log = &self->Log, .IsConstructorFailing = false };
    Widget* Created = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&self->Factory, &self->Screen, self->ProbeType, &Arguments, &Created)))
    {
        return NULL;
    }
    TEST_CHECK_SUCCESS(run, Widget_SetPosition(Created, (Vector2){ .x = box.x, .y = box.y }));
    TEST_CHECK_SUCCESS(run, Widget_SetSize(Created, (Vector2){ .x = box.width, .y = box.height }));
    TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(Created, isFocusable));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    return (ProbeWidget*)Created;
}

static ProbeWidget* AddRoot(TestRun* run, UIFixture* self, Rectangle box, bool isFocusable)
{
    ProbeWidget* Probe = CreateProbe(run, self, box, isFocusable);
    if ((Probe != NULL) && !TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&self->Screen, &Probe->Base)))
    {
        return NULL;
    }
    return Probe;
}

static ProbeWidget* AddChild(TestRun* run, UIFixture* self, ProbeWidget* parent, Rectangle box, bool isFocusable)
{
    ProbeWidget* Probe = CreateProbe(run, self, box, isFocusable);
    if ((Probe != NULL) && !TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(&parent->Base, &Probe->Base)))
    {
        return NULL;
    }
    return Probe;
}

/* Runs one update of @p seconds, then turns this tick's presses into holds and ends the releases. */
static void TickFor(TestRun* run, UIFixture* self, double seconds)
{
    GameInput_BeginTick(&self->Input, &self->Sample);
    self->Time += seconds;
    TEST_CHECK_SUCCESS(run, UIScreen_Update(&self->Screen, ProgramTime_Create(self->Time, seconds), &self->ScreenInput));
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        uint8_t State = self->Sample.KeyStates[Index];
        self->Sample.KeyStates[Index] = ((State & GAME_INPUT_KEY_STATE_RELEASED) != 0) ? 0U : (uint8_t)(State & GAME_INPUT_KEY_STATE_DOWN);
    }
    self->Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->Sample.TextCodepointCount = 0U;
}

static void Tick(TestRun* run, UIFixture* self)
{
    TickFor(run, self, TEST_TICK_SECONDS);
}

static void PressKey(UIFixture* self, GameKey key)
{
    self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
}

static void ReleaseKey(UIFixture* self, GameKey key)
{
    self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_RELEASED;
}

/* The held key auto-repeats this tick. */
static void RepeatKey(UIFixture* self, GameKey key)
{
    self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_REPEATED;
}

/* Presses a key for one tick and releases it on the next. */
static void TapKey(TestRun* run, UIFixture* self, GameKey key)
{
    PressKey(self, key);
    Tick(run, self);
    ReleaseKey(self, key);
    Tick(run, self);
}

static void MovePointer(UIFixture* self, float x, float y)
{
    self->ScreenInput.IsPointerPresent = true;
    self->ScreenInput.PointerPosition = (Vector2){ .x = x, .y = y };
}

static size_t IndexOfTeardown(const ProbeLog* log, uint64_t id)
{
    for (size_t Index = 0; Index < log->DeconstructCount; Index++)
    {
        if (log->DeconstructIds[Index] == id)
        {
            return Index;
        }
    }
    return PROBE_LOG_CAPACITY;
}

static Rectangle Box(float x, float y, float width, float height)
{
    return (Rectangle){ .x = x, .y = y, .width = width, .height = height };
}

static uint64_t IdOf(const ProbeWidget* probe)
{
    return (probe != NULL) ? Widget_GetId(&probe->Base) : WIDGET_ID_INVALID;
}

static bool IsNear(float value, float expected)
{
    return fabsf(value - expected) < 1e-4f;
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Count = 0;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, UI_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, UI_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text)) && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* Found = strstr((const char*)Text._data, needle); Found != NULL; Found = strstr(Found + 1, needle))
        {
            Count++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Count;
}


// Static functions: notification handlers.
static Error RecordEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    HandlerRecord* Record = UserData_GetPointer(userData);
    Record->CallCount++;
    Record->LastEvent = *event;
    if (Record->EventCount < HANDLER_EVENT_CAPACITY)
    {
        Record->Events[Record->EventCount] = *event;
        Record->EventCount++;
    }
    Widget* Target = NULL;
    if (Record->IsDestroyingTheWidget && UIScreen_TryGetWidgetById(screen, event->WidgetId, &Target))
    {
        Error Result = Widget_Destroy(Target);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if (Record->IsUnsubscribing)
    {
        Error Result = UIScreen_Unsubscribe(screen, Record->SubscriptionId);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if (Record->IsReposting)
    {
        return UIScreen_PostWidgetEvent(screen, event->WidgetId, event->WidgetEventId, NULL);
    }
    return Error_CreateSuccess();
}

static bool Subscribe(TestRun* run, UIFixture* self, uint64_t widgetId, uint32_t kindMask, HandlerRecord* record)
{
    UserData Data = UserData_FromPointer(record);
    return TEST_CHECK_SUCCESS(run, UIScreen_Subscribe(&self->Screen, widgetId, kindMask, RecordEvent, &Data, &record->SubscriptionId));
}


// Tests: identity and destruction.
static void StaleIdsNeverResolve(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        uint64_t IdA = IdOf(A);
        TEST_CHECK(run, UIScreen_IsWidgetAlive(&Fixture.Screen, IdA) && (UIScreen_GetWidgetCount(&Fixture.Screen) == 1U));
        TEST_CHECK_SUCCESS(run, Widget_Destroy(&A->Base));
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, IdA) && (UIScreen_GetWidgetCount(&Fixture.Screen) == 0U));
        TEST_CHECK(run, UIScreen_GetRootCount(&Fixture.Screen) == 0U);
        // Torn down only at the end of the next update.
        TEST_CHECK(run, (UIWidgetFactory_GetLiveWidgetCount(&Fixture.Factory, Fixture.ProbeType) == 1U) && (Fixture.Log.DeconstructCount == 0U));
        Tick(run, &Fixture);
        TEST_CHECK(run, (Fixture.Log.DeconstructCount == 1U) && (Fixture.Log.DeconstructIds[0] == IdA));
        TEST_CHECK(run, UIWidgetFactory_GetLiveWidgetCount(&Fixture.Factory, Fixture.ProbeType) == 0U);

        // The slot is reused, the id is not.
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        uint64_t IdB = IdOf(B);
        TEST_CHECK(run, ((IdB & 0xFFFFFFFFU) == (IdA & 0xFFFFFFFFU)) && (IdB != IdA));
        Widget* Found = &B->Base;
        TEST_CHECK(run, !UIScreen_TryGetWidgetById(&Fixture.Screen, IdA, &Found) && (Found == NULL));
        TEST_CHECK(run, UIScreen_IsWidgetAlive(&Fixture.Screen, IdB));
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, WIDGET_ID_INVALID) && !UIScreen_IsWidgetAlive(&Fixture.Screen, 12345U));
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void DestroyTakesTheSubtreeChildrenFirst(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Root = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
        ProbeWidget* First = (Root != NULL) ? AddChild(run, &Fixture, Root, Box(0.0f, 0.0f, 0.5f, 0.5f), false) : NULL;
        ProbeWidget* Grandchild = (First != NULL) ? AddChild(run, &Fixture, First, Box(0.0f, 0.0f, 0.5f, 0.5f), false) : NULL;
        ProbeWidget* Second = (Root != NULL) ? AddChild(run, &Fixture, Root, Box(0.5f, 0.5f, 0.5f, 0.5f), false) : NULL;
        if ((Grandchild != NULL) && (Second != NULL))
        {
            uint64_t Ids[] = { IdOf(Root), IdOf(First), IdOf(Grandchild), IdOf(Second) };
            TEST_CHECK_SUCCESS(run, Widget_Destroy(&Root->Base));
            TEST_CHECK_SUCCESS(run, Widget_Destroy(&First->Base)); // Already being destroyed: nothing happens.
            for (size_t Index = 0; Index < 4U; Index++)
            {
                TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, Ids[Index]));
            }
            TEST_CHECK(run, Fixture.Log.DeconstructCount == 0U);
            Tick(run, &Fixture);
            TEST_CHECK(run, (Fixture.Log.DeconstructCount == 4U) && (Fixture.Log.DeinitializeCount == 4U));
            TEST_CHECK(run, IndexOfTeardown(&Fixture.Log, Ids[2]) < IndexOfTeardown(&Fixture.Log, Ids[1]));
            TEST_CHECK(run, IndexOfTeardown(&Fixture.Log, Ids[1]) < IndexOfTeardown(&Fixture.Log, Ids[0]));
            TEST_CHECK(run, IndexOfTeardown(&Fixture.Log, Ids[3]) < IndexOfTeardown(&Fixture.Log, Ids[0]));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A widget may be destroyed inside its own hook, and a hook may destroy its parent mid-routing.
static void WidgetsCanBeDestroyedInsideHooks(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.2f, 0.2f), true);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.5f, 0.0f, 0.2f, 0.2f), true);
        ProbeWidget* Parent = AddRoot(run, &Fixture, Box(0.0f, 0.5f, 0.5f, 0.5f), false);
        ProbeWidget* Child = (Parent != NULL) ? AddChild(run, &Fixture, Parent, Box(0.0f, 0.0f, 1.0f, 1.0f), false) : NULL;
        if ((A != NULL) && (B != NULL) && (Child != NULL))
        {
            uint64_t IdA = IdOf(A);
            uint64_t IdParent = IdOf(Parent);
            uint64_t IdChild = IdOf(Child);
            A->OnFocusDisableAction = ProbeAction_DestroySelf;
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &A->Base));
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &B->Base));
            TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, IdA));
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(B)) && Widget_IsFocused(&B->Base));

            Child->OnHoverStartAction = ProbeAction_DestroyTarget;
            Child->ActionTargetId = IdParent;
            MovePointer(&Fixture, 0.25f, 0.75f);
            Tick(run, &Fixture);
            TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, IdParent) && !UIScreen_IsWidgetAlive(&Fixture.Screen, IdChild));
            TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            TEST_CHECK(run, (IndexOfTeardown(&Fixture.Log, IdA) != PROBE_LOG_CAPACITY)
                && (IndexOfTeardown(&Fixture.Log, IdChild) < IndexOfTeardown(&Fixture.Log, IdParent)));
            Tick(run, &Fixture);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A focus change made inside a focus hook leaves one widget focused, not two.
static void FocusChangedInAFocusHookLeavesOneWidgetFocused(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.2f, 0.2f), true);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.3f, 0.0f, 0.2f, 0.2f), true);
        ProbeWidget* C = AddRoot(run, &Fixture, Box(0.6f, 0.0f, 0.2f, 0.2f), true);
        if ((A != NULL) && (B != NULL) && (C != NULL))
        {
            A->OnFocusDisableAction = ProbeAction_FocusTarget;
            A->ActionTargetId = IdOf(C);
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &A->Base));
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &B->Base));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(C));
            TEST_CHECK(run, Widget_IsFocused(&C->Base) && !Widget_IsFocused(&A->Base) && !Widget_IsFocused(&B->Base));
            TEST_CHECK(run, (C->FocusEnableCount == 1) && (B->FocusEnableCount == 0));

            // Only focusable, input-enabled, attached widgets take focus.
            ProbeWidget* Detached = CreateProbe(run, &Fixture, Box(0.0f, 0.0f, 0.1f, 0.1f), true);
            TEST_CHECK_ERROR(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &Detached->Base), ErrorCode_InvalidOperation);
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&C->Base, false));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            TEST_CHECK(run, !Widget_IsFocused(&C->Base) && (C->FocusDisableCount == 1));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Removing a root detaches that root, not another one, and ends the removed widget's hover.
static void RemovingARootDetachesItAndEndsItsHover(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.5f, 0.5f, 0.5f, 0.5f), false);
        if ((A != NULL) && (B != NULL))
        {
            MovePointer(&Fixture, 0.25f, 0.25f);
            Tick(run, &Fixture);
            TEST_CHECK(run, Widget_IsHovered(&A->Base) && (A->HoverStartCount == 1));
            TEST_CHECK(run, GameInput_IsPointerConsumed(&Fixture.Input));

            TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Screen, &A->Base));
            TEST_CHECK(run, (UIScreen_GetRootCount(&Fixture.Screen) == 1U) && (UIScreen_GetRootAt(&Fixture.Screen, 0U) == &B->Base));
            TEST_CHECK(run, !Widget_IsScreenRoot(&A->Base) && UIScreen_IsWidgetAlive(&Fixture.Screen, IdOf(A)));
            TEST_CHECK(run, !Widget_IsHovered(&A->Base) && (A->HoverEndCount == 1));
            TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            TEST_CHECK_ERROR(run, UIScreen_RemoveWidget(&Fixture.Screen, &A->Base), ErrorCode_InvalidOperation);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->HoverStartCount == 1) && (A->UpdateCount == 1));
            TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.Screen, &A->Base));
            TEST_CHECK(run, UIScreen_GetRootCount(&Fixture.Screen) == 2U);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void WidgetsMustBeInitializedBeforeTheyAreAdded(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeArgs Arguments = { .Log = &Fixture.Log, .IsConstructorFailing = false };
        Widget* Created = NULL;
        ProbeWidget* Root = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
        if ((Root != NULL) && TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.Factory, &Fixture.Screen,
            Fixture.ProbeType, &Arguments, &Created)))
        {
            TEST_CHECK_ERROR(run, UIScreen_AddWidget(&Fixture.Screen, Created), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, Widget_AddSubWidget(&Root->Base, Created), ErrorCode_InvalidOperation);
            TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
            TEST_CHECK_ERROR(run, Widget_InitializeWidget(Created), ErrorCode_InvalidOperation);
            TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(&Root->Base, Created));
            TEST_CHECK_ERROR(run, Widget_AddSubWidget(&Root->Base, Created), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, Widget_AddSubWidget(Created, &Root->Base), ErrorCode_InvalidOperation); // A root is attached.
            // A cycle: a detached widget cannot take its own ancestor.
            TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Screen, &Root->Base));
            TEST_CHECK_ERROR(run, Widget_AddSubWidget(Created, &Root->Base), ErrorCode_IllegalArgument);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void FailedCreationLeavesNothingRegistered(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeArgs Failing = { .Log = &Fixture.Log, .IsConstructorFailing = true };
        Widget Sentinel;
        Widget* Created = &Sentinel;
        TEST_CHECK_ERROR(run, UIWidgetFactory_CreateWidget(&Fixture.Factory, &Fixture.Screen, Fixture.ProbeType, &Failing, &Created),
            ErrorCode_InvalidState);
        TEST_CHECK(run, (Created == NULL) && (UIScreen_GetWidgetCount(&Fixture.Screen) == 0U));
        TEST_CHECK(run, UIWidgetFactory_GetLiveWidgetCount(&Fixture.Factory, Fixture.ProbeType) == 0U);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Screen, ((uint64_t)1U << 32U) | 1U));

        // The cancelled slot is reused with a new generation.
        ProbeWidget* Probe = CreateProbe(run, &Fixture, Box(0.0f, 0.0f, 0.1f, 0.1f), false);
        TEST_CHECK(run, IdOf(Probe) == (((uint64_t)2U << 32U) | 1U));
        TEST_CHECK_ERROR(run, UIWidgetFactory_UnregisterType(&Fixture.Factory, Fixture.ProbeType), ErrorCode_InvalidOperation);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A constructor that unregisters its own type is refused: a widget counts as alive from the moment its constructor
// starts, so the type's pool cannot be freed under it.
static void TypesCannotBeUnregisteredWhileTheirWidgetIsConstructed(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeArgs Arguments = { .Log = &Fixture.Log, .IsUnregisteringItsType = true, .UnregisterCode = ErrorCode_Success };
        Widget* Created = NULL;
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.Factory, &Fixture.Screen, Fixture.ProbeType, &Arguments, &Created));
        TEST_CHECK(run, (Arguments.UnregisterCode == ErrorCode_InvalidOperation) && (Created != NULL));
        TEST_CHECK(run, UIWidgetFactory_GetLiveWidgetCount(&Fixture.Factory, Fixture.ProbeType) == 1U);
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void DeconstructingTheScreenDestroysEveryWidget(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Root = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
        ProbeWidget* Child = (Root != NULL) ? AddChild(run, &Fixture, Root, Box(0.0f, 0.0f, 0.5f, 0.5f), false) : NULL;
        ProbeArgs Arguments = { .Log = &Fixture.Log, .IsConstructorFailing = false };
        Widget* Uninitialized = NULL;
        if ((Child != NULL) && TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.Factory, &Fixture.Screen,
            Fixture.ProbeType, &Arguments, &Uninitialized)))
        {
            Child->IsDeconstructingTheScreen = true; // A teardown hook cannot deconstruct the screen again.
            Fixture.IsScreenConstructed = false;
            TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&Fixture.Screen));
            TEST_CHECK(run, Fixture.Log.DeconstructCount == 3U);
            TEST_CHECK(run, Fixture.Log.DeinitializeCount == 2U); // The uninitialized one is not deinitialized.
            TEST_CHECK(run, Fixture.Log.NestedDeconstructCode == ErrorCode_InvalidOperation);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void WidgetTreesHaveADepthLimit(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        // A chain of WIDGET_MAX_TREE_DEPTH levels: Chain[i] is at depth i + 1.
        ProbeWidget* Chain[WIDGET_MAX_TREE_DEPTH];
        Chain[0] = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
        bool IsBuilt = Chain[0] != NULL;
        for (size_t Index = 1U; IsBuilt && (Index < WIDGET_MAX_TREE_DEPTH); Index++)
        {
            Chain[Index] = AddChild(run, &Fixture, Chain[Index - 1U], Box(0.0f, 0.0f, 1.0f, 1.0f), false);
            IsBuilt = Chain[Index] != NULL;
        }
        ProbeWidget* Extra = IsBuilt ? CreateProbe(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), false) : NULL;
        if (Extra != NULL)
        {
            TEST_CHECK_ERROR(run, Widget_AddSubWidget(&Chain[WIDGET_MAX_TREE_DEPTH - 1U]->Base, &Extra->Base), ErrorCode_ArgumentOutOfRange);
            // A two-level subtree fits under depth 62 (making 64) but not under depth 63.
            ProbeWidget* ExtraChild = AddChild(run, &Fixture, Extra, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
            if (ExtraChild != NULL)
            {
                TEST_CHECK_ERROR(run, Widget_AddSubWidget(&Chain[WIDGET_MAX_TREE_DEPTH - 2U]->Base, &Extra->Base),
                    ErrorCode_ArgumentOutOfRange);
                TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(&Chain[WIDGET_MAX_TREE_DEPTH - 3U]->Base, &Extra->Base));
            }
            Tick(run, &Fixture); // Every walk of the full-depth tree (snapshot, layout, hit test) stays bounded.
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Tests: input routing.
// The focused widget gets every key first, then its parents; whoever handles it consumes it.
static void KeysReachTheFocusedWidgetThenItsParents(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Parent = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
        ProbeWidget* Child = (Parent != NULL) ? AddChild(run, &Fixture, Parent, Box(0.1f, 0.1f, 0.5f, 0.5f), true) : NULL;
        if (Child != NULL)
        {
            Parent->IsHandlingKeys = true;
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &Child->Base));
            PressKey(&Fixture, GameKey_A);
            Tick(run, &Fixture);
            TEST_CHECK(run, (Child->KeyEventCount == 1) && (Parent->KeyEventCount == 1) && (Parent->LastKey == GameKey_A));
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_A));
            // The release of a press the UI took still reaches it.
            ReleaseKey(&Fixture, GameKey_A);
            Tick(run, &Fixture);
            TEST_CHECK(run, (Parent->LastKeyType == UIKeyboardInputType_KeyRelease) && (Parent->KeyEventCount == 2));

            // Typed text follows the same path.
            Fixture.Sample.TextCodepoints[0] = (uint32_t)'x';
            Fixture.Sample.TextCodepointCount = 1U;
            Tick(run, &Fixture);
            TEST_CHECK(run, (Child->TextCount == 1) && (Parent->TextCount == 1));
            TEST_CHECK(run, GameInput_GetTextCodepointCount(&Fixture.Input) == 0U);

            // A focused widget that handles Tab keeps it from navigation.
            Child->IsHandlingKeys = true;
            PressKey(&Fixture, GameKey_Tab);
            Tick(run, &Fixture);
            TEST_CHECK(run, !UIScreen_IsNavigating(&Fixture.Screen) && (Child->LastKey == GameKey_Tab));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// An unhandled Tab starts navigation; Escape ends it and lets go of focus; then Escape (and Enter) reach the game.
static void NavigationKeysPassThroughWhenUnused(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.2f, 0.1f), true);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.5f, 0.1f, 0.2f, 0.1f), true);
        if ((A != NULL) && (B != NULL))
        {
            PressKey(&Fixture, GameKey_Enter);
            Tick(run, &Fixture);
            TEST_CHECK(run, GameInput_IsActionPressed(&Fixture.Input, GameAction_UIAccept));
            ReleaseKey(&Fixture, GameKey_Enter);

            PressKey(&Fixture, GameKey_Tab);
            Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_IsNavigating(&Fixture.Screen) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(A)));
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Tab));
            ReleaseKey(&Fixture, GameKey_Tab);
            Tick(run, &Fixture);

            PressKey(&Fixture, GameKey_Escape);
            Tick(run, &Fixture);
            TEST_CHECK(run, !UIScreen_IsNavigating(&Fixture.Screen) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
            TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_Pause));
            ReleaseKey(&Fixture, GameKey_Escape);
            Tick(run, &Fixture);

            PressKey(&Fixture, GameKey_Escape);
            Tick(run, &Fixture);
            TEST_CHECK(run, GameInput_IsActionPressed(&Fixture.Input, GameAction_Pause));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void NavigationFollowsReadingOrderAndArrows(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.1f, 0.1f), true);
        ProbeWidget* C = AddRoot(run, &Fixture, Box(0.1f, 0.5f, 0.1f, 0.1f), true); // Added before B: order is by position.
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.5f, 0.1f, 0.1f, 0.1f), true);
        if ((A != NULL) && (B != NULL) && (C != NULL))
        {
            const UIScreen* Screen = &Fixture.Screen;
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(B));
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(C));
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));

            Fixture.Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(C));
            Fixture.Sample.KeyStates[GameKey_LeftShift] = 0U;

            TapKey(run, &Fixture, GameKey_Up);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            TapKey(run, &Fixture, GameKey_Right);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(B));
            TapKey(run, &Fixture, GameKey_Left);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            TapKey(run, &Fixture, GameKey_Down);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(C));
            // Nothing below: focus stays, and the key is still used while navigating.
            PressKey(&Fixture, GameKey_Down);
            Tick(run, &Fixture);
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == IdOf(C)) && GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Down));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Leaving keyboard navigation leaves no widget stuck "tabbed".
static void AcceptEntersContainersAndLeavingClearsTabbed(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Menu = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.3f, 0.1f), true);
        ProbeWidget* First = (Menu != NULL) ? AddChild(run, &Fixture, Menu, Box(0.0f, 1.0f, 1.0f, 1.0f), true) : NULL;
        ProbeWidget* Second = (Menu != NULL) ? AddChild(run, &Fixture, Menu, Box(0.0f, 2.0f, 1.0f, 1.0f), true) : NULL;
        if ((First != NULL) && (Second != NULL))
        {
            const UIScreen* Screen = &Fixture.Screen;
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(Menu));
            TapKey(run, &Fixture, GameKey_Enter);
            TEST_CHECK(run, Widget_IsTabbed(&Menu->Base) && Widget_IsActive(&Menu->Base) && (UIScreen_GetFocusedWidgetId(Screen) == IdOf(First)));
            TapKey(run, &Fixture, GameKey_Down);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(Second));
            TapKey(run, &Fixture, GameKey_Escape);
            TEST_CHECK(run, !Widget_IsTabbed(&Menu->Base) && (UIScreen_GetFocusedWidgetId(Screen) == IdOf(Menu)));

            // Entering again, then moving the pointer: navigation ends, nothing stays tabbed, focus stays.
            TapKey(run, &Fixture, GameKey_Enter);
            TEST_CHECK(run, Widget_IsTabbed(&Menu->Base) && (Menu->TabbedEnableCount == 2));
            MovePointer(&Fixture, 0.9f, 0.9f);
            Tick(run, &Fixture);
            TEST_CHECK(run, !UIScreen_IsNavigating(Screen) && !Widget_IsTabbed(&Menu->Base) && (Menu->TabbedDisableCount == 2));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(First));

            // The next navigation key resumes inside the menu, where focus is.
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_IsNavigating(Screen) && Widget_IsTabbed(&Menu->Base) && (UIScreen_GetFocusedWidgetId(Screen) == IdOf(First)));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Holding Tab or an arrow repeats navigation, although navigation consumes its key. A repeat follows the modifiers
// held at that moment.
static void HoldingANavigationKeyRepeatsNavigation(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.1f, 0.1f), true);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.4f, 0.1f, 0.1f, 0.1f), true);
        ProbeWidget* C = AddRoot(run, &Fixture, Box(0.7f, 0.1f, 0.1f, 0.1f), true);
        if ((A != NULL) && (B != NULL) && (C != NULL))
        {
            const UIScreen* Screen = &Fixture.Screen;
            PressKey(&Fixture, GameKey_Tab);
            Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            Tick(run, &Fixture); // Held without a repeat: nothing moves.
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            RepeatKey(&Fixture, GameKey_Tab);
            Tick(run, &Fixture);
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == IdOf(B)) && GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Tab));
            // Shift pressed while Tab is held: the next repeat goes backwards.
            Fixture.Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
            RepeatKey(&Fixture, GameKey_Tab);
            Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            Fixture.Sample.KeyStates[GameKey_LeftShift] = 0U;
            ReleaseKey(&Fixture, GameKey_Tab);
            Tick(run, &Fixture);

            PressKey(&Fixture, GameKey_Right);
            Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(B));
            RepeatKey(&Fixture, GameKey_Right);
            Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(C));
            ReleaseKey(&Fixture, GameKey_Right);
            Tick(run, &Fixture);

            // Once the pointer ends navigation, a key held since before does not start it again.
            PressKey(&Fixture, GameKey_Left);
            Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(B));
            MovePointer(&Fixture, 0.95f, 0.95f);
            Tick(run, &Fixture);
            TEST_CHECK(run, !UIScreen_IsNavigating(Screen));
            RepeatKey(&Fixture, GameKey_Left);
            Tick(run, &Fixture);
            TEST_CHECK(run, !UIScreen_IsNavigating(Screen) && (UIScreen_GetFocusedWidgetId(Screen) == IdOf(B)));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Navigation does not assume ui_previous holds the Shift chord: with the Tab bindings swapped, Shift+Tab goes forwards
// and Tab backwards.
static void SwappedTabBindingsNavigateBothWays(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.1f, 0.1f), true);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.4f, 0.1f, 0.1f, 0.1f), true);
        ProbeWidget* C = AddRoot(run, &Fixture, Box(0.7f, 0.1f, 0.1f, 0.1f), true);
        GameInputBinding ShiftTab = { .Key = GameKey_Tab, .Modifiers = GAME_KEY_MODIFIER_SHIFT };
        GameInputBinding PlainTab = { .Key = GameKey_Tab, .Modifiers = GAME_KEY_MODIFIER_NONE };
        if ((A != NULL) && (B != NULL) && (C != NULL)
            && TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UINext, &ShiftTab, 1U))
            && TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_UIPrevious, &PlainTab, 1U)))
        {
            const UIScreen* Screen = &Fixture.Screen;
            TapKey(run, &Fixture, GameKey_Tab); // Starts navigation on the first candidate.
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(C));
            Fixture.Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(B));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Enter on a container focused by a click enters it (starting navigation) instead of falling through to the game.
static void EnterOpensAContainerFocusedByAClick(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Menu = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.3f, 0.1f), true);
        ProbeWidget* First = (Menu != NULL) ? AddChild(run, &Fixture, Menu, Box(0.0f, 1.0f, 1.0f, 1.0f), true) : NULL;
        if (First != NULL)
        {
            MovePointer(&Fixture, 0.2f, 0.15f);
            TapKey(run, &Fixture, GameKey_MouseLeft);
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(Menu)) && !UIScreen_IsNavigating(&Fixture.Screen));
            PressKey(&Fixture, GameKey_Enter);
            Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_IsNavigating(&Fixture.Screen) && Widget_IsTabbed(&Menu->Base));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(First));
            TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_UIAccept));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Focus can move without navigation (here the game moves it): the next navigation step brings the entered widgets back
// in line with focus, so none stays tabbed.
static void NavigationFollowsFocusMovedByTheGame(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Menu = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.3f, 0.1f), true);
        ProbeWidget* First = (Menu != NULL) ? AddChild(run, &Fixture, Menu, Box(0.0f, 1.0f, 1.0f, 1.0f), true) : NULL;
        ProbeWidget* Other = AddRoot(run, &Fixture, Box(0.6f, 0.1f, 0.3f, 0.1f), true);
        if ((First != NULL) && (Other != NULL))
        {
            const UIScreen* Screen = &Fixture.Screen;
            TapKey(run, &Fixture, GameKey_Tab);
            TapKey(run, &Fixture, GameKey_Enter);
            TEST_CHECK(run, Widget_IsTabbed(&Menu->Base) && (UIScreen_GetFocusedWidgetId(Screen) == IdOf(First)));
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &Other->Base));
            TapKey(run, &Fixture, GameKey_Tab);
            // Back at the top level, where Other is: Menu is left, and the next candidate after Other (wrapping) is Menu.
            TEST_CHECK(run, !Widget_IsTabbed(&Menu->Base) && (Menu->TabbedDisableCount == 1));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(Menu));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A hook that detaches the widget navigation is entering leaves no focus inside the detached subtree.
static void AHookThatDetachesTheEnteredWidgetLeavesNoDetachedFocus(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Menu = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.3f, 0.1f), true);
        ProbeWidget* First = (Menu != NULL) ? AddChild(run, &Fixture, Menu, Box(0.0f, 1.0f, 1.0f, 1.0f), true) : NULL;
        if (First != NULL)
        {
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(Menu));
            Menu->OnTabbedEnableAction = ProbeAction_DetachSelf;
            TapKey(run, &Fixture, GameKey_Enter);
            TEST_CHECK(run, !Widget_IsScreenRoot(&Menu->Base) && !Widget_IsTabbed(&Menu->Base));
            TEST_CHECK(run, (Menu->TabbedEnableCount == 1) && (Menu->TabbedDisableCount == 1));
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID) && !Widget_IsFocused(&First->Base));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// The same one level deeper: starting navigation enters Menu then Sub, and Menu's hook detaches Sub on the way. Sub
// must not end up tabbed (it is off the path), and nothing inside it focused.
static void AHookThatDetachesAnInnerEnteredWidgetLeavesItUntabbed(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Menu = AddRoot(run, &Fixture, Box(0.1f, 0.1f, 0.5f, 0.5f), true);
        ProbeWidget* Sub = (Menu != NULL) ? AddChild(run, &Fixture, Menu, Box(0.0f, 0.0f, 0.5f, 0.5f), true) : NULL;
        ProbeWidget* Item = (Sub != NULL) ? AddChild(run, &Fixture, Sub, Box(0.0f, 0.0f, 0.5f, 0.5f), true) : NULL;
        if ((Item != NULL) && TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &Item->Base)))
        {
            Menu->OnTabbedEnableAction = ProbeAction_DetachTarget;
            Menu->ActionTargetId = IdOf(Sub);
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_IsNavigating(&Fixture.Screen) && Widget_IsTabbed(&Menu->Base));
            TEST_CHECK(run, (Widget_GetParent(&Sub->Base) == NULL) && !Widget_IsTabbed(&Sub->Base) && (Sub->TabbedEnableCount == 0));
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID) && !Widget_IsFocused(&Item->Base));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void NonFocusableWidgetsAreTransparentToNavigation(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Panel = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
        ProbeWidget* A = (Panel != NULL) ? AddChild(run, &Fixture, Panel, Box(0.1f, 0.1f, 0.2f, 0.2f), true) : NULL;
        ProbeWidget* B = (Panel != NULL) ? AddChild(run, &Fixture, Panel, Box(0.5f, 0.1f, 0.2f, 0.2f), true) : NULL;
        if ((A != NULL) && (B != NULL))
        {
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(A));
            TapKey(run, &Fixture, GameKey_Tab);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(B));
            // B contains nothing focusable: Enter is left to the game.
            PressKey(&Fixture, GameKey_Enter);
            Tick(run, &Fixture);
            TEST_CHECK(run, GameInput_IsActionPressed(&Fixture.Input, GameAction_UIAccept));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A click on a button does not also reach the game: a press over a widget is consumed, its release follows it; a press
// over empty space reaches the game and clears focus.
static void ClicksOverWidgetsAreConsumed(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), true);
        if (A != NULL)
        {
            MovePointer(&Fixture, 0.25f, 0.25f);
            PressKey(&Fixture, GameKey_MouseLeft);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->PressCount == 1) && GameInput_IsKeyConsumed(&Fixture.Input, GameKey_MouseLeft));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(A));
            MovePointer(&Fixture, 0.8f, 0.8f); // A drag out of the widget still belongs to it.
            Tick(run, &Fixture);
            TEST_CHECK(run, A->MoveCount == 1);
            ReleaseKey(&Fixture, GameKey_MouseLeft);
            Tick(run, &Fixture);
            TEST_CHECK(run, A->ReleaseCount == 1);

            PressKey(&Fixture, GameKey_MouseLeft);
            Tick(run, &Fixture);
            TEST_CHECK(run, !GameInput_IsKeyConsumed(&Fixture.Input, GameKey_MouseLeft) && (A->PressCount == 1));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            ReleaseKey(&Fixture, GameKey_MouseLeft);
            Tick(run, &Fixture);

            // The wheel over a widget is consumed too.
            MovePointer(&Fixture, 0.25f, 0.25f);
            Fixture.Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 1.0f };
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->ScrollCount == 1) && (GameInput_GetWheelDelta(&Fixture.Input).y == 0.0f));
            MovePointer(&Fixture, 0.75f, 0.75f);
            Fixture.Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 1.0f };
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->ScrollCount == 1) && (GameInput_GetWheelDelta(&Fixture.Input).y == 1.0f));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A click focuses the nearest focusable widget under the pointer and raises its root.
static void AClickFocusesTheNearestFocusableAncestor(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Back = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.6f, 0.6f), true);
        ProbeWidget* Front = AddRoot(run, &Fixture, Box(0.4f, 0.4f, 0.6f, 0.6f), true);
        ProbeWidget* Decoration = (Back != NULL) ? AddChild(run, &Fixture, Back, Box(0.1f, 0.1f, 0.3f, 0.3f), false) : NULL;
        if ((Front != NULL) && (Decoration != NULL))
        {
            Widget* Top = NULL;
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 0.5f, .y = 0.5f }, &Top) && (Top == &Front->Base));
            MovePointer(&Fixture, 0.1f, 0.1f);
            PressKey(&Fixture, GameKey_MouseLeft);
            Tick(run, &Fixture);
            TEST_CHECK(run, (Decoration->PressCount == 1) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(Back)));
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 0.5f, .y = 0.5f }, &Top) && (Top == &Back->Base));
            float RaisedZ = Widget_GetZLayer(&Back->Base);
            ReleaseKey(&Fixture, GameKey_MouseLeft);
            Tick(run, &Fixture);
            TapKey(run, &Fixture, GameKey_MouseLeft); // Clicking the top root again does not raise it further.
            TEST_CHECK(run, Widget_GetZLayer(&Back->Base) == RaisedZ);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Surface-agnostic input: without the pointer nothing is hovered, without the keyboard no key is routed.
static void ScreensRouteOnlyWhatTheirHostGives(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), true);
        if (A != NULL)
        {
            Fixture.ScreenInput.IsPointerPresent = false;
            Fixture.ScreenInput.HasKeyboardFocus = false;
            PressKey(&Fixture, GameKey_Tab);
            Tick(run, &Fixture);
            TEST_CHECK(run, !Widget_IsHovered(&A->Base) && !UIScreen_IsNavigating(&Fixture.Screen));
            TEST_CHECK(run, !GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Tab) && !GameInput_IsPointerConsumed(&Fixture.Input));

            // The window helper: the mouse normalized by the window, and none while the cursor is captured.
            GameInputSample Sample = Fixture.Sample;
            Sample.MousePosition = (Vector2){ .x = 640.0f, .y = 180.0f };
            GameInput_BeginTick(&Fixture.Input, &Sample);
            UIScreenInput FromWindow = UIScreenInput_CreateFromWindow(&Fixture.Input);
            TEST_CHECK(run, FromWindow.IsPointerPresent && IsNear(FromWindow.PointerPosition.x, 0.5f) && IsNear(FromWindow.PointerPosition.y, 0.25f));
            TEST_CHECK(run, FromWindow.HasKeyboardFocus && (FromWindow.Input == &Fixture.Input));
            Sample.IsCursorCaptured = true;
            GameInput_BeginTick(&Fixture.Input, &Sample);
            TEST_CHECK(run, !UIScreenInput_CreateFromWindow(&Fixture.Input).IsPointerPresent);
            Sample.IsCursorCaptured = false;
            GameInput_BeginTick(&Fixture.Input, &Sample);
            GameInput_ConsumePointer(&Fixture.Input);
            TEST_CHECK(run, !UIScreenInput_CreateFromWindow(&Fixture.Input).IsPointerPresent);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}


// The repeats and the release of a key a widget handled go to that widget, not to whatever is focused by then.
static void AHandledKeyRepeatsAndReleasesToItsHandler(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.2f, 0.2f), true);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.5f, 0.0f, 0.2f, 0.2f), true);
        if ((A != NULL) && (B != NULL))
        {
            A->IsHandlingKeys = true;
            B->IsHandlingKeys = true;
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &A->Base));
            PressKey(&Fixture, GameKey_X);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->KeyEventCount == 1) && (A->LastKeyType == UIKeyboardInputType_KeyPress));

            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &B->Base));
            RepeatKey(&Fixture, GameKey_X);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->KeyEventCount == 2) && (A->LastKeyType == UIKeyboardInputType_KeyRepeat) && (B->KeyEventCount == 0));
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_X));
            ReleaseKey(&Fixture, GameKey_X);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->KeyEventCount == 3) && (A->LastKeyType == UIKeyboardInputType_KeyRelease) && (B->KeyEventCount == 0));

            // A handler that stops taking input gets nothing more, and the key stays consumed until it is released.
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &A->Base));
            PressKey(&Fixture, GameKey_Y);
            Tick(run, &Fixture);
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&A->Base, false));
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &B->Base));
            RepeatKey(&Fixture, GameKey_Y);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->KeyEventCount == 4) && (B->KeyEventCount == 0) && GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Y));
            ReleaseKey(&Fixture, GameKey_Y);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->KeyEventCount == 4) && (B->KeyEventCount == 0));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A press does not reach a widget that a focus hook has just detached.
static void AFocusHookThatDetachesTheClickedWidgetGetsNoPress(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), true);
        if (A != NULL)
        {
            A->OnFocusEnableAction = ProbeAction_DetachSelf;
            MovePointer(&Fixture, 0.25f, 0.25f);
            PressKey(&Fixture, GameKey_MouseLeft);
            Tick(run, &Fixture);
            TEST_CHECK(run, !Widget_IsScreenRoot(&A->Base) && (A->PressCount == 0));
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_MouseLeft));
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
            ReleaseKey(&Fixture, GameKey_MouseLeft);
            Tick(run, &Fixture);
            TEST_CHECK(run, A->ReleaseCount == 0);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Tests: drawing order and hit testing.
// Siblings of equal z keep their insertion order from tick to tick: an unstable per-tick sort would swap them and
// change what GetSubWidgetAt returns.
static void SiblingsOfEqualZKeepInsertionOrder(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Parent = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
        ProbeWidget* Children[3] = { NULL, NULL, NULL };
        for (size_t Index = 0; (Parent != NULL) && (Index < 3U); Index++)
        {
            Children[Index] = AddChild(run, &Fixture, Parent, Box(0.0f, 0.0f, 1.0f, 1.0f), false);
        }
        if (Children[2] != NULL)
        {
            Widget* Top = NULL;
            Vector2 Middle = { .x = 0.5f, .y = 0.5f };
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, Middle, &Top) && (Top == &Children[2]->Base));
            TEST_CHECK_SUCCESS(run, Widget_SetZLayer(&Children[0]->Base, 1.0f));
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, Middle, &Top) && (Top == &Children[0]->Base));
            for (size_t Index = 0; Index < 3U; Index++)
            {
                TEST_CHECK(run, Widget_GetSubWidgetAt(&Parent->Base, Index) == &Children[Index]->Base);
            }
            TEST_CHECK_SUCCESS(run, Widget_SetZLayer(&Children[0]->Base, 0.0f));
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, Middle, &Top) && (Top == &Children[2]->Base));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

static void HitTestingRespectsClippingAndDisabledInput(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Clip = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.4f, 0.4f), false);
        ProbeWidget* Outside = (Clip != NULL) ? AddChild(run, &Fixture, Clip, Box(1.5f, 1.5f, 0.5f, 0.5f), false) : NULL;
        ProbeWidget* Back = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.2f, 0.2f), false);
        ProbeWidget* Glass = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.2f, 0.2f), false);
        if ((Outside != NULL) && (Back != NULL) && (Glass != NULL))
        {
            Widget* Top = NULL;
            Vector2 InChild = { .x = 0.7f, .y = 0.7f };
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, InChild, &Top) && (Top == &Outside->Base));
            TEST_CHECK_SUCCESS(run, Widget_SetAreSubWidgetsCut(&Clip->Base, true));
            TEST_CHECK(run, !UIScreen_TryGetTopmostAt(&Fixture.Screen, InChild, &Top) && (Top == NULL));

            Vector2 Corner = { .x = 0.1f, .y = 0.1f };
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, Corner, &Top) && (Top == &Glass->Base));
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&Glass->Base, false));
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, Corner, &Top) && (Top == &Back->Base));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Layout runs in update for attached widgets only, after their update.
static void LayoutRunsInUpdateForAttachedWidgets(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Attached = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        ProbeWidget* Detached = CreateProbe(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        if ((Attached != NULL) && (Detached != NULL))
        {
            Tick(run, &Fixture);
            Tick(run, &Fixture);
            TEST_CHECK(run, (Attached->UpdateCount == 2) && (Attached->LayoutCount == 2));
            TEST_CHECK(run, (Detached->UpdateCount == 0) && (Detached->LayoutCount == 0));
            UIViewport Bad = UIViewport_Create((Vector2){ .x = 0.0f, .y = 720.0f }, 16.0f / 9.0f, (Vector2){ .x = 0.5f, .y = 0.5f });
            TEST_CHECK_ERROR(run, UIScreen_SetViewport(&Fixture.Screen, &Bad), ErrorCode_ArgumentOutOfRange);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Render may not write state: the bounds it outlines with are computed without filling the cache.
static void RenderReadsBoundsWithoutCaching(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* Parent = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        ProbeWidget* Child = (Parent != NULL) ? AddChild(run, &Fixture, Parent, Box(0.5f, 0.5f, 1.0f, 1.0f), false) : NULL;
        if (Child != NULL)
        {
            Rectangle Peeked = Widget_InternalPeekEstimatedBounds(&Parent->Base);
            TEST_CHECK(run, !Parent->Base._areBoundsValid && !Child->Base._areBoundsValid);
            Rectangle ScreenBox = UIScreen_InternalPeekEstimatedScreenBox(&Child->Base);
            TEST_CHECK(run, IsNear(ScreenBox.x, 0.25f) && IsNear(ScreenBox.width, 0.5f) && !Child->Base._areBoundsValid);
            Rectangle Cached = Widget_GetEstimatedBounds(&Parent->Base);
            TEST_CHECK(run, Parent->Base._areBoundsValid && Child->Base._areBoundsValid);
            TEST_CHECK(run, IsNear(Cached.width, 0.75f) && IsNear(Peeked.width, Cached.width) && IsNear(Peeked.x, Cached.x));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Tests: animations.
static void AnimationsAreTypedAndValidated(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        if (A != NULL)
        {
            uint64_t Id = IdOf(A);
            uint64_t AnimationId = UI_SCREEN_ID_INVALID;
            UIAnimationOptions Options = UIAnimationOptions_CreateDefault();
            UIKeyframe Move[] =
            {
                { .Time = 0.0, .Interpolation = UIInterpolation_Linear, .Value.Vector2Value = { 0.0f, 0.0f } },
                { .Time = 1.0, .Interpolation = UIInterpolation_Linear, .Value.Vector2Value = { 1.0f, 0.0f } },
            };
            TEST_CHECK_ERROR(run, UIScreen_StartAnimation(&Fixture.Screen, Id, WidgetBaseProperty_Position, UIPropertyType_Float,
                Move, 2U, Options, &AnimationId), ErrorCode_IllegalArgument);
            TEST_CHECK_ERROR(run, UIScreen_StartAnimation(&Fixture.Screen, Id, 1234, UIPropertyType_Float,
                Move, 2U, Options, &AnimationId), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, UIScreen_StartAnimation(&Fixture.Screen, Id + 1U, WidgetBaseProperty_Position, UIPropertyType_Vector2,
                Move, 2U, Options, &AnimationId), ErrorCode_InvalidOperation);
            UIKeyframe Backwards[] = { Move[1], Move[0] };
            TEST_CHECK_ERROR(run, UIScreen_StartAnimation(&Fixture.Screen, Id, WidgetBaseProperty_Position, UIPropertyType_Vector2,
                Backwards, 2U, Options, &AnimationId), ErrorCode_ArgumentOutOfRange);
            UIKeyframe NotFinite[] = { Move[0], Move[1] };
            NotFinite[1].Value.Vector2Value.x = NAN;
            TEST_CHECK_ERROR(run, UIScreen_StartAnimation(&Fixture.Screen, Id, WidgetBaseProperty_Position, UIPropertyType_Vector2,
                NotFinite, 2U, Options, &AnimationId), ErrorCode_ArgumentOutOfRange);
            UIKeyframe NoCurve[] = { Move[0], Move[1] };
            NoCurve[0].Interpolation = UIInterpolation_Custom;
            TEST_CHECK_ERROR(run, UIScreen_StartAnimation(&Fixture.Screen, Id, WidgetBaseProperty_Position, UIPropertyType_Vector2,
                NoCurve, 2U, Options, &AnimationId), ErrorCode_IllegalArgument);

            TEST_CHECK_SUCCESS(run, UIScreen_StartAnimation(&Fixture.Screen, Id, WidgetBaseProperty_Position, UIPropertyType_Vector2,
                Move, 2U, Options, &AnimationId));
            TickFor(run, &Fixture, 0.5);
            TEST_CHECK(run, IsNear(Widget_GetPosition(&A->Base).x, 0.5f) && UIScreen_IsAnimationRunning(&Fixture.Screen, AnimationId));
            TickFor(run, &Fixture, 0.6);
            TEST_CHECK(run, IsNear(Widget_GetPosition(&A->Base).x, 1.0f) && !UIScreen_IsAnimationRunning(&Fixture.Screen, AnimationId));
            TEST_CHECK_ERROR(run, UIScreen_StopAnimation(&Fixture.Screen, AnimationId), ErrorCode_InvalidOperation);

            // The property read back is typed.
            UIPropertyValue Value;
            TEST_CHECK_SUCCESS(run, Widget_GetProperty(&A->Base, WidgetBaseProperty_Position, &Value));
            TEST_CHECK(run, (Value.Type == UIPropertyType_Vector2) && IsNear(Value.Data.Vector2Value.x, 1.0f));
            UIPropertyValue WrongType = UIPropertyValue_FromFloat(2.0f);
            TEST_CHECK_ERROR(run, Widget_SetProperty(&A->Base, WidgetBaseProperty_Size, &WrongType), ErrorCode_IllegalArgument);
            TEST_CHECK_SUCCESS(run, Widget_SetProperty(&A->Base, PROBE_PROPERTY_VALUE, &WrongType));
            TEST_CHECK(run, IsNear(A->Value, 2.0f));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A failing animation stops (and would be logged) instead of failing the update; destroying a widget stops its animations.
static void FailingAnimationsStopWithoutFailingTheUpdate(TestRun* run)
{
    UIFixture Fixture;
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.5f, 0.0f, 0.5f, 0.5f), false);
        if ((A != NULL) && (B != NULL))
        {
            UIKeyframe Values[] =
            {
                { .Time = 0.0, .Interpolation = UIInterpolation_Linear, .Value.FloatValue = 0.0f },
                { .Time = 1.0, .Interpolation = UIInterpolation_Linear, .Value.FloatValue = 1.0f },
            };
            UIAnimationOptions Looping = UIAnimationOptions_CreateDefault();
            Looping.IsLooping = true;
            uint64_t Failing = UI_SCREEN_ID_INVALID;
            uint64_t Running = UI_SCREEN_ID_INVALID;
            A->IsValueSetterFailing = true;
            TEST_CHECK_SUCCESS(run, UIScreen_StartAnimation(&Fixture.Screen, IdOf(A), PROBE_PROPERTY_VALUE, UIPropertyType_Float,
                Values, 2U, Looping, &Failing));
            TEST_CHECK_SUCCESS(run, UIScreen_StartAnimation(&Fixture.Screen, IdOf(B), PROBE_PROPERTY_VALUE, UIPropertyType_Float,
                Values, 2U, Looping, &Running));
            Tick(run, &Fixture);
            TEST_CHECK(run, !UIScreen_IsAnimationRunning(&Fixture.Screen, Failing) && UIScreen_IsAnimationRunning(&Fixture.Screen, Running));
            TEST_CHECK_SUCCESS(run, Widget_Destroy(&B->Base));
            TEST_CHECK(run, !UIScreen_IsAnimationRunning(&Fixture.Screen, Running));
            Tick(run, &Fixture);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Tests: notifications.
// Notifications are delivered at the end of the update (after the widget updates), with the widget's id.
static void NotificationsWaitForTheEndOfTheUpdate(TestRun* run)
{
    UIFixture Fixture;
    HandlerRecord Record = { 0 };
    if (Fixture_Construct(run, &Fixture) && Subscribe(run, &Fixture, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_HoverChanged), &Record))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        if (A != NULL)
        {
            A->WatchedCounter = &Record.CallCount;
            A->WatchedAtUpdate = -1;
            MovePointer(&Fixture, 0.25f, 0.25f);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->WatchedAtUpdate == 0) && (Record.CallCount == 1));
            TEST_CHECK(run, (Record.LastEvent.Kind == UIEventKind_HoverChanged) && Record.LastEvent.IsSet
                && (Record.LastEvent.WidgetId == IdOf(A)) && (Record.LastEvent.WidgetTypeId == Fixture.ProbeType));
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// When a widget's focus hook moves focus again, the last notification about the widget tells its final state (a state
// change queued after the hook would tell a stale one).
static void NotificationsTellTheFinalStateWhenHooksMoveFocus(TestRun* run)
{
    UIFixture Fixture;
    HandlerRecord Record = { 0 };
    if (Fixture_Construct(run, &Fixture) && Subscribe(run, &Fixture, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_FocusChanged), &Record))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.2f, 0.2f), true);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.5f, 0.0f, 0.2f, 0.2f), true);
        if ((A != NULL) && (B != NULL))
        {
            A->OnFocusEnableAction = ProbeAction_FocusTarget;
            A->ActionTargetId = IdOf(B);
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &A->Base));
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == IdOf(B)) && !Widget_IsFocused(&A->Base));
            Tick(run, &Fixture);
            if (TEST_CHECK(run, Record.EventCount == 3U))
            {
                TEST_CHECK(run, (Record.Events[0].WidgetId == IdOf(A)) && Record.Events[0].IsSet);
                TEST_CHECK(run, (Record.Events[1].WidgetId == IdOf(A)) && !Record.Events[1].IsSet);
                TEST_CHECK(run, (Record.Events[2].WidgetId == IdOf(B)) && Record.Events[2].IsSet);
            }
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A state change of a destroyed widget is dropped, also when a handler destroys the widget mid-delivery. A widget's own
// event is delivered even after its widget is destroyed, so a button that closes its dialog still reports the click.
static void WidgetEventsOutliveTheirWidgetStateChangesDoNot(TestRun* run)
{
    UIFixture Fixture;
    HandlerRecord Record = { 0 };
    if (Fixture_Construct(run, &Fixture) && Subscribe(run, &Fixture, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK_ALL, &Record))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), true);
        ProbeWidget* B = AddRoot(run, &Fixture, Box(0.5f, 0.0f, 0.5f, 0.5f), false);
        if ((A != NULL) && (B != NULL))
        {
            UIPropertyValue Data = UIPropertyValue_FromFloat(3.0f);
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Screen, &A->Base));
            TEST_CHECK_SUCCESS(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdOf(A), 7, &Data));
            TEST_CHECK_SUCCESS(run, Widget_Destroy(&A->Base));
            Tick(run, &Fixture);
            TEST_CHECK(run, (Record.CallCount == 1) && (Record.LastEvent.Kind == UIEventKind_Widget));
            TEST_CHECK(run, (Record.LastEvent.WidgetEventId == 7) && Record.LastEvent.HasValue
                && IsNear(Record.LastEvent.Value.Data.FloatValue, 3.0f));

            // The handler destroys B on B's event: B's hover change, queued after it, is dropped.
            uint64_t IdB = IdOf(B);
            Record.IsDestroyingTheWidget = true;
            TEST_CHECK_SUCCESS(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdB, 8, NULL));
            MovePointer(&Fixture, 0.75f, 0.25f);
            Tick(run, &Fixture);
            TEST_CHECK(run, (Record.CallCount == 2) && (Record.LastEvent.WidgetEventId == 8) && !UIScreen_IsWidgetAlive(&Fixture.Screen, IdB));
            TEST_CHECK(run, IndexOfTeardown(&Fixture.Log, IdB) != PROBE_LOG_CAPACITY);
            TEST_CHECK_ERROR(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdB, 1, NULL), ErrorCode_InvalidOperation);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Handlers that keep posting cannot hang the game: one update delivers at most UI_SCREEN_MAX_EVENTS_PER_DISPATCH. The
// warning is logged once per streak of such updates (an update with nothing to deliver does not end it), not every update.
static void RunawayNotificationsAreCappedAndLoggedOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, UI_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "runaway_notifications", &Directory))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        UIFixture Fixture;
        HandlerRecord Record = { 0 };
        if (Fixture_ConstructLogged(run, &Fixture, &Log)
            && Subscribe(run, &Fixture, WIDGET_ID_INVALID, UI_EVENT_KIND_MASK(UIEventKind_Widget), &Record))
        {
            ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
            if (A != NULL)
            {
                int Cap = (int)UI_SCREEN_MAX_EVENTS_PER_DISPATCH;
                Record.IsReposting = true;
                TEST_CHECK_SUCCESS(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdOf(A), 1, NULL));
                Tick(run, &Fixture);
                TEST_CHECK(run, Record.CallCount == Cap);
                Tick(run, &Fixture); // The dropped ones are gone: nothing is left to deliver.
                TEST_CHECK(run, Record.CallCount == Cap);
                TEST_CHECK_SUCCESS(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdOf(A), 1, NULL));
                Tick(run, &Fixture); // Same streak: not logged again.
                TEST_CHECK(run, Record.CallCount == (2 * Cap));

                Record.IsReposting = false;
                TEST_CHECK_SUCCESS(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdOf(A), 1, NULL));
                Tick(run, &Fixture); // Delivered without dropping any: the streak ends.
                Record.IsReposting = true;
                TEST_CHECK_SUCCESS(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdOf(A), 1, NULL));
                Tick(run, &Fixture);
                TEST_CHECK(run, Record.CallCount == ((3 * Cap) + 1));
            }
        }
        Fixture_Deconstruct(run, &Fixture);
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
        TEST_CHECK(run, CountInLatestLog(run, Directory._data, "notifications were dropped") == 2U);
    }
    Memory_Free(Directory._data);
}

static void HandlersCanUnsubscribeThemselves(TestRun* run)
{
    UIFixture Fixture;
    HandlerRecord Record = { 0 };
    if (Fixture_Construct(run, &Fixture))
    {
        ProbeWidget* A = AddRoot(run, &Fixture, Box(0.0f, 0.0f, 0.5f, 0.5f), false);
        if ((A != NULL) && Subscribe(run, &Fixture, IdOf(A), UI_EVENT_KIND_MASK(UIEventKind_Widget), &Record))
        {
            Record.IsUnsubscribing = true;
            TEST_CHECK_SUCCESS(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdOf(A), 1, NULL));
            TEST_CHECK_SUCCESS(run, UIScreen_PostWidgetEvent(&Fixture.Screen, IdOf(A), 2, NULL));
            Tick(run, &Fixture);
            TEST_CHECK(run, Record.CallCount == 1);
            TEST_CHECK_ERROR(run, UIScreen_Unsubscribe(&Fixture.Screen, Record.SubscriptionId), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, UIScreen_Subscribe(&Fixture.Screen, IdOf(A) + 1U, UI_EVENT_KIND_MASK_ALL, RecordEvent, NULL,
                &Record.SubscriptionId), ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, UIScreen_Subscribe(&Fixture.Screen, IdOf(A), 0U, RecordEvent, NULL, &Record.SubscriptionId),
                ErrorCode_IllegalArgument);
        }
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Public functions.
void UIScreenTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "stale_ids_never_resolve", StaleIdsNeverResolve },
        { "destroy_takes_the_subtree_children_first", DestroyTakesTheSubtreeChildrenFirst },
        { "widgets_can_be_destroyed_inside_hooks", WidgetsCanBeDestroyedInsideHooks },
        { "focus_changed_in_a_focus_hook_leaves_one_widget_focused", FocusChangedInAFocusHookLeavesOneWidgetFocused },
        { "removing_a_root_detaches_it_and_ends_its_hover", RemovingARootDetachesItAndEndsItsHover },
        { "widgets_must_be_initialized_before_they_are_added", WidgetsMustBeInitializedBeforeTheyAreAdded },
        { "failed_creation_leaves_nothing_registered", FailedCreationLeavesNothingRegistered },
        { "types_cannot_be_unregistered_while_their_widget_is_constructed", TypesCannotBeUnregisteredWhileTheirWidgetIsConstructed },
        { "deconstructing_the_screen_destroys_every_widget", DeconstructingTheScreenDestroysEveryWidget },
        { "widget_trees_have_a_depth_limit", WidgetTreesHaveADepthLimit },
        { "keys_reach_the_focused_widget_then_its_parents", KeysReachTheFocusedWidgetThenItsParents },
        { "navigation_keys_pass_through_when_unused", NavigationKeysPassThroughWhenUnused },
        { "navigation_follows_reading_order_and_arrows", NavigationFollowsReadingOrderAndArrows },
        { "accept_enters_containers_and_leaving_clears_tabbed", AcceptEntersContainersAndLeavingClearsTabbed },
        { "holding_a_navigation_key_repeats_navigation", HoldingANavigationKeyRepeatsNavigation },
        { "swapped_tab_bindings_navigate_both_ways", SwappedTabBindingsNavigateBothWays },
        { "enter_opens_a_container_focused_by_a_click", EnterOpensAContainerFocusedByAClick },
        { "navigation_follows_focus_moved_by_the_game", NavigationFollowsFocusMovedByTheGame },
        { "a_hook_that_detaches_the_entered_widget_leaves_no_detached_focus", AHookThatDetachesTheEnteredWidgetLeavesNoDetachedFocus },
        { "a_hook_that_detaches_an_inner_entered_widget_leaves_it_untabbed", AHookThatDetachesAnInnerEnteredWidgetLeavesItUntabbed },
        { "non_focusable_widgets_are_transparent_to_navigation", NonFocusableWidgetsAreTransparentToNavigation },
        { "clicks_over_widgets_are_consumed", ClicksOverWidgetsAreConsumed },
        { "a_click_focuses_the_nearest_focusable_ancestor", AClickFocusesTheNearestFocusableAncestor },
        { "screens_route_only_what_their_host_gives", ScreensRouteOnlyWhatTheirHostGives },
        { "a_handled_key_repeats_and_releases_to_its_handler", AHandledKeyRepeatsAndReleasesToItsHandler },
        { "a_focus_hook_that_detaches_the_clicked_widget_gets_no_press", AFocusHookThatDetachesTheClickedWidgetGetsNoPress },
        { "siblings_of_equal_z_keep_insertion_order", SiblingsOfEqualZKeepInsertionOrder },
        { "hit_testing_respects_clipping_and_disabled_input", HitTestingRespectsClippingAndDisabledInput },
        { "layout_runs_in_update_for_attached_widgets", LayoutRunsInUpdateForAttachedWidgets },
        { "render_reads_bounds_without_caching", RenderReadsBoundsWithoutCaching },
        { "animations_are_typed_and_validated", AnimationsAreTypedAndValidated },
        { "failing_animations_stop_without_failing_the_update", FailingAnimationsStopWithoutFailingTheUpdate },
        { "notifications_wait_for_the_end_of_the_update", NotificationsWaitForTheEndOfTheUpdate },
        { "notifications_tell_the_final_state_when_hooks_move_focus", NotificationsTellTheFinalStateWhenHooksMoveFocus },
        { "widget_events_outlive_their_widget_state_changes_do_not", WidgetEventsOutliveTheirWidgetStateChangesDoNot },
        { "runaway_notifications_are_capped_and_logged_once", RunawayNotificationsAreCappedAndLoggedOnce },
        { "handlers_can_unsubscribe_themselves", HandlersCanUnsubscribeThemselves },
    };
    TestRun_RunSuite(run, "UIScreen", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
