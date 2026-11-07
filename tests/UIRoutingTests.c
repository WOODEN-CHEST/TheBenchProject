#include "TestSuites.h"
#include "TestUI.h"
#include "GameInput.h"
#include "UIInternal.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "wr/WRCompile.h"
#include "wr/WRMemory.h"
#include <math.h>


// Macros.
/** How many mouse inputs a probe keeps. */
#define PROBE_MOUSE_CAPACITY ((size_t)32)
/** Longer than a row of clicks waits. */
#define LONG_PAUSE_SECONDS 1.0


// Types.
typedef struct RoutingProbeStruct RoutingProbe;

/* What a probe does from inside a hook. */
typedef Error (*RoutingProbeScript)(RoutingProbe* self);

/* A widget that keeps what it hears, in the order everything heard it (the fixture's counter), and can run a script from a hook. */
struct RoutingProbeStruct
{
    Widget Base;
    int* Clock;

    WidgetMouseInputArgs Mouse[PROBE_MOUSE_CAPACITY];
    int MouseOrder[PROBE_MOUSE_CAPACITY];
    size_t MouseCount;
    int PressCount;
    int ReleaseCount;
    int MoveCount;
    int ReleaseOverCount;

    int LostCount;
    GameKey LostButton;
    int LostOrder;

    bool IsHandlingScroll;
    int ScrollCount;
    int ScrollOrder;
    WidgetScrollInputArgs LastScroll;

    bool IsHandlingKeys;
    bool IsHandlingOnlyHoverKeys;
    int KeyCount;
    WidgetKeyboardInputArgs LastKey;
    int TextCount;

    int HoverStartCount;
    int HoverEndCount;
    int FocusEnableCount;
    int FocusDisableCount;
    int TabbedDisableCount;

    uint64_t TargetId;
    RoutingProbeScript OnPress;
    RoutingProbeScript OnMove;
    RoutingProbeScript OnRelease;
    RoutingProbeScript OnLost;
    RoutingProbeScript OnScroll;
    RoutingProbeScript OnKey;
    RoutingProbeScript OnFocus;
};

/* A surface with the probe type and a clock the probes' hooks read. */
typedef struct RoutingFixtureStruct
{
    TestUIFixture Base;
    uint64_t ProbeType;
    int Clock;
} RoutingFixture;


// Static functions: the probe's hooks.
static Error RoutingProbe_Construct(void* widget, void* args)
{
    RoutingProbe* Probe = widget;
    Probe->Clock = args;
    return Error_CreateSuccess();
}

static Error RunScript(RoutingProbe* probe, RoutingProbeScript script)
{
    return (script != NULL) ? script(probe) : Error_CreateSuccess();
}

static Error RoutingProbe_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    RoutingProbe* Probe = self;
    if (Probe->MouseCount < PROBE_MOUSE_CAPACITY)
    {
        Probe->Mouse[Probe->MouseCount] = *args;
        Probe->MouseOrder[Probe->MouseCount] = (*Probe->Clock)++;
        Probe->MouseCount++;
    }
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            Probe->PressCount++;
            return RunScript(Probe, Probe->OnPress);
        case UIMouseInputType_Move:
            Probe->MoveCount++;
            return RunScript(Probe, Probe->OnMove);
        case UIMouseInputType_ButtonRelease:
            Probe->ReleaseCount++;
            return RunScript(Probe, Probe->OnRelease);
        case UIMouseInputType_ReleaseOver:
            Probe->ReleaseOverCount++;
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static Error RoutingProbe_OnPressLost(void* self, GameKey button)
{
    RoutingProbe* Probe = self;
    Probe->LostCount++;
    Probe->LostButton = button;
    Probe->LostOrder = (*Probe->Clock)++;
    return RunScript(Probe, Probe->OnLost);
}

static Error RoutingProbe_OnScrollInput(void* self, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    RoutingProbe* Probe = self;
    Probe->ScrollCount++;
    Probe->ScrollOrder = (*Probe->Clock)++;
    Probe->LastScroll = *args;
    *outIsHandled = Probe->IsHandlingScroll;
    return RunScript(Probe, Probe->OnScroll);
}

static Error RoutingProbe_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    RoutingProbe* Probe = self;
    if (args->Type == UIKeyboardInputType_Text)
    {
        Probe->TextCount++;
        return Error_CreateSuccess();
    }
    Probe->KeyCount++;
    Probe->LastKey = *args;
    *outIsHandled = Probe->IsHandlingKeys || (Probe->IsHandlingOnlyHoverKeys && args->IsHoverDelivery);
    return RunScript(Probe, Probe->OnKey);
}

static Error RoutingProbe_OnHoverStart(void* self, const WidgetHoverArgs* args)
{
    UNUSED(args);
    ((RoutingProbe*)self)->HoverStartCount++;
    return Error_CreateSuccess();
}

static Error RoutingProbe_OnHoverEnd(void* self)
{
    ((RoutingProbe*)self)->HoverEndCount++;
    return Error_CreateSuccess();
}

static Error RoutingProbe_OnFocusEnable(void* self)
{
    RoutingProbe* Probe = self;
    Probe->FocusEnableCount++;
    return RunScript(Probe, Probe->OnFocus);
}

static Error RoutingProbe_OnFocusDisable(void* self)
{
    ((RoutingProbe*)self)->FocusDisableCount++;
    return Error_CreateSuccess();
}

static Error RoutingProbe_OnTabbedDisable(void* self)
{
    ((RoutingProbe*)self)->TabbedDisableCount++;
    return Error_CreateSuccess();
}

static const WidgetVTable RoutingProbeVTable =
{
    .OnMouseInput = RoutingProbe_OnMouseInput,
    .OnPressLost = RoutingProbe_OnPressLost,
    .OnScrollInput = RoutingProbe_OnScrollInput,
    .OnKeyboardInput = RoutingProbe_OnKeyboardInput,
    .OnHoverStart = RoutingProbe_OnHoverStart,
    .OnHoverEnd = RoutingProbe_OnHoverEnd,
    .OnFocusEnable = RoutingProbe_OnFocusEnable,
    .OnFocusDisable = RoutingProbe_OnFocusDisable,
    .OnTabbedDisable = RoutingProbe_OnTabbedDisable
};


// Static functions: scripts.
static Error ScriptDestroySelf(RoutingProbe* self)
{
    return Widget_Destroy(&self->Base);
}

static Error ScriptDestroyTarget(RoutingProbe* self)
{
    Widget* Target = NULL;
    return UIScreen_TryGetWidgetById(Widget_GetScreen(&self->Base), self->TargetId, &Target) ? Widget_Destroy(Target) : Error_CreateSuccess();
}

static Error ScriptDetachSelf(RoutingProbe* self)
{
    UIScreen* Screen = Widget_GetScreen(&self->Base);
    return Widget_IsScreenRoot(&self->Base) ? UIScreen_RemoveWidget(Screen, &self->Base) : Widget_RemoveSubWidget(Widget_GetParent(&self->Base), &self->Base);
}

static Error ScriptHideSelf(RoutingProbe* self)
{
    return Widget_SetIsRendered(&self->Base, false);
}

static Error ScriptMoveSelfToRoots(RoutingProbe* self)
{
    return Widget_MoveTo(&self->Base, NULL);
}

static Error ScriptTransferLeftToTarget(RoutingProbe* self)
{
    return UIScreen_TransferPress(Widget_GetScreen(&self->Base), GameKey_MouseLeft, self->TargetId);
}

static Error ScriptHideParent(RoutingProbe* self)
{
    return Widget_SetIsRendered(Widget_GetParent(&self->Base), false);
}

static Error ScriptDetachParent(RoutingProbe* self)
{
    return UIScreen_RemoveWidget(Widget_GetScreen(&self->Base), Widget_GetParent(&self->Base));
}

static Error ScriptMakeParentOverlay(RoutingProbe* self)
{
    return UIScreen_SetRootLayer(Widget_GetScreen(&self->Base), Widget_GetParent(&self->Base), UIScreenLayer_Overlay);
}

static Error ScriptCapture(RoutingProbe* self)
{
    return UIScreen_CapturePointer(Widget_GetScreen(&self->Base), Widget_GetId(&self->Base));
}


// Static functions: the fixture.
static bool Routing_Construct(TestRun* run, RoutingFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"RoutingProbe", .StructSize = sizeof(RoutingProbe), .VTable = &RoutingProbeVTable, .Constructor = RoutingProbe_Construct,
        .IsFocusable = false
    };
    return TestUI_Construct(run, &self->Base)
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&self->Base.Widgets, &Descriptor, &self->ProbeType));
}

static void Routing_Deconstruct(TestRun* run, RoutingFixture* self)
{
    TestUI_Deconstruct(run, &self->Base);
}

/* A probe over a box (relative to its parent) under a parent, or a root for WIDGET_ID_INVALID. */
static RoutingProbe* AddProbe(TestRun* run, RoutingFixture* self, uint64_t parentId, float x, float y, float width, float height, bool isFocusable)
{
    Widget* Created = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&self->Base.Widgets, &self->Base.Screen, self->ProbeType, &self->Clock, &Created)))
    {
        return NULL;
    }
    TEST_CHECK_SUCCESS(run, Widget_SetPosition(Created, (Vector2){ .x = x, .y = y }));
    TEST_CHECK_SUCCESS(run, Widget_SetSize(Created, (Vector2){ .x = width, .y = height }));
    TEST_CHECK_SUCCESS(run, Widget_SetIsFocusable(Created, isFocusable));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    Widget* Parent = NULL;
    if (parentId == WIDGET_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&self->Base.Screen, Created));
    }
    else if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&self->Base.Screen, parentId, &Parent)))
    {
        TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Parent, Created));
    }
    return (RoutingProbe*)Created;
}

static uint64_t IdOf(const RoutingProbe* probe)
{
    return (probe != NULL) ? Widget_GetId(&probe->Base) : WIDGET_ID_INVALID;
}

/* The pointer at a place in the screen's own space, as a window has it: tracked everywhere, over the screen inside it. */
static void PointAt(RoutingFixture* self, float x, float y)
{
    self->Base.ScreenInput.IsPointerTracked = true;
    self->Base.ScreenInput.IsPointerPresent = (x >= 0.0f) && (x <= 1.0f) && (y >= 0.0f) && (y <= 1.0f);
    self->Base.ScreenInput.PointerPosition = (Vector2){ .x = x, .y = y };
}

static void Tick(TestRun* run, RoutingFixture* self)
{
    TestUI_Tick(run, &self->Base);
}

static void Press(TestRun* run, RoutingFixture* self, GameKey button)
{
    TestUI_Press(&self->Base, button);
    Tick(run, self);
}

static void Release(TestRun* run, RoutingFixture* self, GameKey button)
{
    TestUI_Release(&self->Base, button);
    Tick(run, self);
}

/* A press and a release of one button at a place, and a tick to take the pointer there first. */
static void ClickAt(TestRun* run, RoutingFixture* self, GameKey button, float x, float y)
{
    PointAt(self, x, y);
    Tick(run, self);
    Press(run, self, button);
    Release(run, self, button);
}

static bool IsClose(float a, float b)
{
    return fabsf(a - b) < 1e-3f;
}

static const WidgetMouseInputArgs* LastMouse(const RoutingProbe* probe)
{
    return &probe->Mouse[probe->MouseCount - 1U];
}


// Tests: the held pointer.
/* Past the surface in every direction the press's widget keeps hearing where the pointer is, unclamped, and the release tells the last
 * place; the pointer hovers nothing there, and it stays the UI's: a held press owns it. */
static void AHeldPressFollowsThePointerPastTheSurface(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.4f, 0.4f, 0.2f, 0.2f, false);
        PointAt(&Fixture, 0.5f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&A->Base));
        Press(run, &Fixture, GameKey_MouseLeft);

        const Vector2 Places[] = { { -2.0f, 0.5f }, { 3.5f, 0.5f }, { 0.5f, -1.5f }, { 0.5f, 4.0f }, { -2.0f, -2.0f } };
        for (size_t Index = 0U; Index < (sizeof(Places) / sizeof(Places[0])); Index++)
        {
            int Moves = A->MoveCount;
            PointAt(&Fixture, Places[Index].x, Places[Index].y);
            Tick(run, &Fixture);
            TEST_CHECK(run, A->MoveCount == (Moves + 1));
            TEST_CHECK(run, IsClose(LastMouse(A)->ScreenPosition.x, Places[Index].x) && IsClose(LastMouse(A)->ScreenPosition.y, Places[Index].y));
            TEST_CHECK(run, IsClose(LastMouse(A)->WidgetPosition.x, (Places[Index].x - 0.4f) / 0.2f));
            TEST_CHECK(run, !Widget_IsHovered(&A->Base) && (UIScreen_GetHoveredWidgetId(&Fixture.Base.Screen) == WIDGET_ID_INVALID));
            TEST_CHECK(run, GameInput_IsPointerConsumed(&Fixture.Base.Input));
        }

        // It stays where it is: no more moves. The release tells the last place.
        int Moves = A->MoveCount;
        Tick(run, &Fixture);
        TEST_CHECK(run, A->MoveCount == Moves);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (A->ReleaseCount == 1) && (LastMouse(A)->Type == UIMouseInputType_ButtonRelease) && IsClose(LastMouse(A)->ScreenPosition.x, -2.0f));

        // With the press over, a pointer off the surface is no one's.
        Tick(run, &Fixture);
        TEST_CHECK(run, !GameInput_IsPointerConsumed(&Fixture.Base.Input));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* What a host hands over without tracking acts as it always has: off the surface a press hears nothing until the pointer is back. */
static void AnInputThatDoesNotTrackGivesNoDrags(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        TestUI_MovePointer(&Fixture.Base, 100.0f, 100.0f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TestUI_RemovePointer(&Fixture.Base);
        Tick(run, &Fixture);
        Tick(run, &Fixture);
        TEST_CHECK(run, A->MoveCount == 0);
        TEST_CHECK(run, !Widget_IsHovered(&A->Base));
        TestUI_MovePointer(&Fixture.Base, 100.0f, 100.0f); // Back where it was: that still counts.
        Tick(run, &Fixture);
        TEST_CHECK(run, A->MoveCount == 1);
        TestUI_RemovePointer(&Fixture.Base);
        Tick(run, &Fixture);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (A->MoveCount == 1) && (A->ReleaseCount == 1));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A pointer tracked but not present, at the place it was, is still news for the press: the widget hears it left, at no distance. */
static void ThePointerLeavingWhereItStandsTellsThePress(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        PointAt(&Fixture, 0.99f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, A->MoveCount == 0);
        Fixture.Base.ScreenInput.IsPointerPresent = false;
        Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsPointerPresent(&Fixture.Base.Screen));
        TEST_CHECK(run, (A->MoveCount == 1) && (LastMouse(A)->DeltaPixels.x == 0.0f) && (LastMouse(A)->DeltaPixels.y == 0.0f));
        Release(run, &Fixture, GameKey_MouseLeft);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* The window's pointer: tracked with its position as it is, past the edges too; over the screen only inside it, with the cursor on it and
 * no UI above claiming it. A captured cursor gives nothing. */
static void TheWindowsPointerIsTrackedAndPresentOnlyOnIt(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        GameInput* Input = &Fixture.Base.Input;
        GameInputSample Sample = Fixture.Base.Sample;
        Sample.WindowSize = (Vector2){ .x = 1000.0f, .y = 500.0f };
        const struct { float X; float Y; bool IsInside; } Cases[] =
        {
            { 500.0f, 250.0f, true }, { 0.0f, 0.0f, true }, { 1000.0f, 500.0f, true }, { -1.0f, 250.0f, false }, { 1001.0f, 250.0f, false },
            { 500.0f, -1.0f, false }, { 500.0f, 501.0f, false }, { -4000.0f, 9000.0f, false }
        };
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            Sample.MousePosition = (Vector2){ .x = Cases[Index].X, .y = Cases[Index].Y };
            GameInput_BeginTick(Input, &Sample);
            UIScreenInput Made = UIScreenInput_CreateFromWindow(Input);
            TEST_CHECK(run, Made.IsPointerTracked && (Made.IsPointerPresent == Cases[Index].IsInside));
            TEST_CHECK(run, IsClose(Made.PointerPosition.x, Cases[Index].X / 1000.0f) && IsClose(Made.PointerPosition.y, Cases[Index].Y / 500.0f));
            TEST_CHECK(run, !Made.IsPointerRelative);
        }

        // Just inside the edge, but the cursor has left: the last place reported means nothing for hover.
        Sample.MousePosition = (Vector2){ .x = 999.0f, .y = 250.0f };
        Sample.IsCursorOnWindow = false;
        GameInput_BeginTick(Input, &Sample);
        UIScreenInput Made = UIScreenInput_CreateFromWindow(Input);
        TEST_CHECK(run, Made.IsPointerTracked && !Made.IsPointerPresent);
        Sample.IsCursorOnWindow = true;

        // UI above claimed it: still tracked, so a drag held below goes on.
        GameInput_BeginTick(Input, &Sample);
        GameInput_ConsumePointer(Input);
        Made = UIScreenInput_CreateFromWindow(Input);
        TEST_CHECK(run, Made.IsPointerTracked && !Made.IsPointerPresent && IsClose(Made.PointerPosition.x, 0.999f));

        Sample.IsCursorCaptured = true;
        GameInput_BeginTick(Input, &Sample);
        Made = UIScreenInput_CreateFromWindow(Input);
        TEST_CHECK(run, !Made.IsPointerTracked && !Made.IsPointerPresent);
        Sample.IsCursorCaptured = false;

        Sample.WindowSize = (Vector2){ .x = 0.0f, .y = 500.0f };
        GameInput_BeginTick(Input, &Sample);
        Made = UIScreenInput_CreateFromWindow(Input);
        TEST_CHECK(run, !Made.IsPointerTracked && !Made.IsPointerPresent);
        Sample.WindowSize = (Vector2){ .x = 1000.0f, .y = 0.0f };
        GameInput_BeginTick(Input, &Sample);
        Made = UIScreenInput_CreateFromWindow(Input);
        TEST_CHECK(run, !Made.IsPointerTracked && !Made.IsPointerPresent);

        Made = UIScreenInput_CreateFromWindow(NULL);
        TEST_CHECK(run, (Made.Input == NULL) && !Made.IsPointerTracked && !Made.IsPointerPresent && !Made.HasKeyboardFocus);
        Made = UIScreenInput_CreateEmpty();
        TEST_CHECK(run, !Made.IsPointerTracked && !Made.IsPointerRelative);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A held press owns the pointer: another button's press over nothing, on the surface or past it, is nobody's. Focus stays, the game never sees it, and the drag goes on. */
static void AnotherPressOverNothingDuringADragIsNobodys(TestRun* run)
{
    const float Places[] = { 0.75f, 1.5f };
    for (size_t Index = 0U; Index < (sizeof(Places) / sizeof(Places[0])); Index++)
    {
        RoutingFixture Fixture;
        if (Routing_Construct(run, &Fixture))
        {
            UIScreen* Screen = &Fixture.Base.Screen;
            RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, true);
            PointAt(&Fixture, 0.25f, 0.5f);
            Press(run, &Fixture, GameKey_MouseLeft);
            TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
            PointAt(&Fixture, Places[Index], 0.5f);
            Tick(run, &Fixture);

            Press(run, &Fixture, GameKey_MouseRight);
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Base.Input, GameKey_MouseRight));
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == IdOf(A)) && (A->FocusDisableCount == 0));
            TEST_CHECK(run, (A->PressCount == 1) && UIScreen_HasHeldPress(Screen));
            Release(run, &Fixture, GameKey_MouseRight);
            TEST_CHECK(run, (A->ReleaseCount == 0) && (A->LostCount == 0) && UIScreen_HasHeldPress(Screen));
            Release(run, &Fixture, GameKey_MouseLeft);
            TEST_CHECK(run, A->ReleaseCount == 1);

            // With nothing held it is a click outside again.
            Press(run, &Fixture, GameKey_MouseRight);
            TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID) && !GameInput_IsKeyConsumed(&Fixture.Base.Input, GameKey_MouseRight));
            Release(run, &Fixture, GameKey_MouseRight);
        }
        Routing_Deconstruct(run, &Fixture);
    }
}

/* A drag below a frame above that claimed the pointer goes on: tracked, it hears the moves, hover ends and no new press lands. */
static void ADragBelowUiThatClaimedThePointerGoesOn(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, A->PressCount == 1);

        Fixture.Base.ScreenInput.IsPointerPresent = false;
        Fixture.Base.ScreenInput.PointerPosition = (Vector2){ .x = 0.75f, .y = 0.5f };
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->MoveCount == 1) && IsClose(LastMouse(A)->ScreenPosition.x, 0.75f));
        TEST_CHECK(run, !Widget_IsHovered(&A->Base) && !Widget_IsHovered(&B->Base));
        Press(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, B->PressCount == 0);
        Release(run, &Fixture, GameKey_MouseRight);
        // The release off the screen's pointer has nothing to go over.
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (A->ReleaseCount == 1) && (B->ReleaseOverCount == 0));
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: the arguments.
/* Every delivery tells the modifiers held then, and how far the pointer went since the last delivery of this press, in surface pixels. */
static void TheArgumentsTellModifiersAndDeltas(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        PointAt(&Fixture, 0.5f, 0.5f);
        Tick(run, &Fixture);
        Fixture.Base.Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, LastMouse(A)->Modifiers == GAME_KEY_MODIFIER_SHIFT);
        TEST_CHECK(run, (LastMouse(A)->DeltaPixels.x == 0.0f) && (LastMouse(A)->DeltaPixels.y == 0.0f) && (LastMouse(A)->DurationSeconds == 0.0));

        // 100 pixels across and 50 down of a 1000 x 500 surface.
        Fixture.Base.Sample.KeyStates[GameKey_LeftShift] = 0U;
        Fixture.Base.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
        PointAt(&Fixture, 0.6f, 0.6f);
        Tick(run, &Fixture);
        TEST_CHECK(run, LastMouse(A)->Modifiers == GAME_KEY_MODIFIER_CONTROL);
        TEST_CHECK(run, IsClose(LastMouse(A)->DeltaPixels.x, 100.0f) && IsClose(LastMouse(A)->DeltaPixels.y, 50.0f));
        TEST_CHECK(run, LastMouse(A)->DurationSeconds > 0.0);
        // Since the last delivery, not since the press; and off the surface too.
        PointAt(&Fixture, 1.1f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, IsClose(LastMouse(A)->DeltaPixels.x, 500.0f) && IsClose(LastMouse(A)->DeltaPixels.y, -50.0f));
        TEST_CHECK(run, IsClose(LastMouse(A)->ClickStartScreenPosition.x, 0.5f) && IsClose(LastMouse(A)->ClickStartWidgetPosition.x, 0.5f));

        // A move in the release's tick arrives first; the release itself moved nowhere.
        Fixture.Base.Sample.KeyStates[GameKey_LeftControl] = 0U;
        PointAt(&Fixture, 1.2f, 0.5f);
        Release(run, &Fixture, GameKey_MouseLeft);
        const WidgetMouseInputArgs* Move = &A->Mouse[A->MouseCount - 2U];
        TEST_CHECK(run, (Move->Type == UIMouseInputType_Move) && IsClose(Move->DeltaPixels.x, 100.0f));
        TEST_CHECK(run, (LastMouse(A)->Type == UIMouseInputType_ButtonRelease) && (LastMouse(A)->DeltaPixels.x == 0.0f) && (LastMouse(A)->Modifiers == GAME_KEY_MODIFIER_NONE));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Each button's press keeps its own track of what it last heard. */
static void TwoHeldButtonsEachHearTheirOwnDrag(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, IsClose(LastMouse(A)->DeltaPixels.x, 500.0f));
        Press(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, (B->PressCount == 1) && (LastMouse(B)->Button == GameKey_MouseRight));
        PointAt(&Fixture, 0.8f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (LastMouse(A)->Button == GameKey_MouseLeft) && IsClose(LastMouse(A)->DeltaPixels.x, 50.0f));
        TEST_CHECK(run, (LastMouse(B)->Button == GameKey_MouseRight) && IsClose(LastMouse(B)->DeltaPixels.x, 50.0f));
        Release(run, &Fixture, GameKey_MouseRight);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (A->ReleaseCount == 1) && (B->ReleaseCount == 1));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Presses of one button on one widget count up while each comes soon after the last and near it; anything else starts again. */
static void ClicksCountOverTimeAndDistance(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 0.5f, false);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.2f, 0.5f);
        size_t First = A->MouseCount;
        TEST_CHECK(run, A->Mouse[First - 2U].ClickCount == 1U);
        TEST_CHECK(run, A->Mouse[First - 1U].ClickCount == 1U); // The release tells its press's count.

        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.2f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 2U);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.2f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 3U);

        // Too late: within the time is still a row, past it is not.
        Fixture.Base.Time += UI_SCREEN_MULTI_CLICK_SECONDS * 0.5;
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.2f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 4U);
        Fixture.Base.Time += UI_SCREEN_MULTI_CLICK_SECONDS * 2.0;
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.2f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 1U);

        // Too far: 3 UI pixels is near, 5 is not (a 1000 pixel wide surface at scale 1).
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.203f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 2U);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.208f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 1U);

        // Down a surface half as tall: a share of the height counts twice as many pixels of the width's share.
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.208f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 2U);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.208f, 0.51f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 1U);

        // Another button, another widget or a click on nothing break the row.
        ClickAt(run, &Fixture, GameKey_MouseRight, 0.208f, 0.51f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 1U);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.208f, 0.51f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 1U);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.51f, 0.2f);
        TEST_CHECK(run, B->Mouse[B->MouseCount - 2U].ClickCount == 1U);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.208f, 0.51f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 1U);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.75f, 0.75f); // Nothing there.
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.208f, 0.51f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 1U);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A row of clicks stays on one widget: a click on its neighbour, however soon and however near, starts another row. */
static void ARowOfClicksStaysOnOneWidget(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.499f, 0.5f);
        TEST_CHECK(run, A->Mouse[0].ClickCount == 1U);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.501f, 0.5f);
        TEST_CHECK(run, (B->PressCount == 1) && (B->Mouse[0].ClickCount == 1U));
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.501f, 0.5f);
        TEST_CHECK(run, (B->PressCount == 2) && (LastMouse(B)->ClickCount == 2U));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* The distance counts in UI pixels: at twice the scale twice the surface pixels are near. */
static void TheClickDistanceFollowsTheScale(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.2f, 0.5f);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.206f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 1U);
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Base.Screen, 2.0f));
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.212f, 0.5f);
        TEST_CHECK(run, A->Mouse[A->MouseCount - 2U].ClickCount == 2U);
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: a release over another widget.
/* After the press's widget hears its release, the top widget under the pointer hears of it, with the press's start in its own box. */
static void AReleaseGoesOverToTheWidgetUnderThePointer(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseMiddle);
        PointAt(&Fixture, 0.75f, 0.25f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (B->MouseCount == 0U) && Widget_IsHovered(&B->Base));
        Fixture.Base.Sample.KeyStates[GameKey_LeftAlt] = GAME_INPUT_KEY_STATE_DOWN;
        Release(run, &Fixture, GameKey_MouseMiddle);
        TEST_CHECK(run, (A->ReleaseCount == 1) && (A->ReleaseOverCount == 0));
        TEST_CHECK(run, (B->ReleaseOverCount == 1) && (B->ReleaseCount == 0) && (B->MouseCount == 1U));
        TEST_CHECK(run, B->MouseOrder[0] > A->MouseOrder[A->MouseCount - 1U]);
        const WidgetMouseInputArgs* Over = LastMouse(B);
        TEST_CHECK(run, (Over->Type == UIMouseInputType_ReleaseOver) && (Over->Button == GameKey_MouseMiddle) && (Over->Modifiers == GAME_KEY_MODIFIER_ALT));
        TEST_CHECK(run, IsClose(Over->ScreenPosition.x, 0.75f) && IsClose(Over->WidgetPosition.x, 0.5f) && IsClose(Over->WidgetPosition.y, 0.25f));
        TEST_CHECK(run, IsClose(Over->ClickStartScreenPosition.x, 0.25f) && IsClose(Over->ClickStartWidgetPosition.x, -0.5f));
        TEST_CHECK(run, (Over->DurationSeconds > 0.0) && (Over->ClickCount == 1U));
        Fixture.Base.Sample.KeyStates[GameKey_LeftAlt] = 0U;

        // A release on the press's own widget goes over nowhere.
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.25f, 0.5f);
        TEST_CHECK(run, (A->ReleaseOverCount == 0) && (B->ReleaseOverCount == 1));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Only a press a widget of this screen took: one that began on nothing is the game's, wherever it comes up. Only the top widget, and one
 * that takes input. Nothing off the surface. */
static void OnlyAPressAWidgetTookGoesOverAndOnlyToTheTop(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.25f, 1.0f, false);
        RoutingProbe* Under = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Top = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 0.5f, false);

        PointAt(&Fixture, 0.35f, 0.5f); // On nothing.
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.75f, 0.25f);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Top->MouseCount == 0U) && (Under->MouseCount == 0U));

        PointAt(&Fixture, 0.1f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.75f, 0.25f);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Top->ReleaseOverCount == 1) && (Under->ReleaseOverCount == 0));

        // Disabled for input, the top is passed through.
        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&Top->Base, false));
        PointAt(&Fixture, 0.1f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.75f, 0.25f);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Top->ReleaseOverCount == 1) && (Under->ReleaseOverCount == 1));

        // Past the surface there is nothing to go over; nor with the pointer gone from an input with no tracking.
        PointAt(&Fixture, 0.1f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 1.5f, 0.25f);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Under->ReleaseOverCount == 1) && (A->ReleaseCount == 3));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A child of the press's widget is another widget, and what the release's hook does to the tree counts. */
static void AReleaseOverSeesWhatTheReleasesHookDid(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Parent = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Child = AddProbe(run, &Fixture, IdOf(Parent), 0.5f, 0.5f, 0.5f, 0.5f, false);
        PointAt(&Fixture, 0.1f, 0.1f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.4f, 0.75f);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Child->ReleaseOverCount == 1) && (Parent->ReleaseCount == 1));

        // The hook destroys what lies under the pointer: the release goes over what is left there.
        RoutingProbe* Second = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.75f, 0.0f, 0.25f, 1.0f, false);
        uint64_t ChildId = IdOf(Child);
        Parent->TargetId = ChildId;
        Parent->OnRelease = ScriptDestroyTarget;
        PointAt(&Fixture, 0.1f, 0.1f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.4f, 0.75f);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Base.Screen, ChildId) && (Parent->ReleaseCount == 2) && (Parent->ReleaseOverCount == 0));
        TEST_CHECK(run, Second->ReleaseOverCount == 0);

        // The hook destroys its own widget, under the pointer's way: nothing more happens, nothing breaks.
        Parent->OnRelease = ScriptDestroySelf;
        PointAt(&Fixture, 0.1f, 0.1f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.9f, 0.5f);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, Second->ReleaseOverCount == 1);
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: a lost press.
/* Hidden, disabled for input, taken out of the tree, hidden through an ancestor or put in an overlay: the widget hears at once, once, which
 * button; nothing more reaches it. */
static void ALostPressIsToldAtOnceAndOnlyOnce(TestRun* run)
{
    for (int Case = 0; Case < 6; Case++)
    {
        RoutingFixture Fixture;
        if (Routing_Construct(run, &Fixture))
        {
            RoutingProbe* Root = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
            RoutingProbe* A = AddProbe(run, &Fixture, IdOf(Root), 0.25f, 0.25f, 0.5f, 0.5f, false);
            PointAt(&Fixture, 0.5f, 0.5f);
            Press(run, &Fixture, GameKey_MouseRight);
            TEST_CHECK(run, (A->PressCount == 1) && (A->LostCount == 0));
            switch (Case)
            {
                case 0: TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&A->Base, false)); break;
                case 1: TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&A->Base, false)); break;
                case 2: TEST_CHECK_SUCCESS(run, Widget_RemoveSubWidget(&Root->Base, &A->Base)); break;
                case 3: TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&Root->Base, false)); break;
                case 4: TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Base.Screen, &Root->Base)); break;
                default: TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(&Fixture.Base.Screen, &Root->Base, UIScreenLayer_Overlay)); break;
            }
            TEST_CHECK(run, (A->LostCount == 1) && (A->LostButton == GameKey_MouseRight));
            TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.Base.Screen));

            PointAt(&Fixture, 0.6f, 0.6f);
            Tick(run, &Fixture);
            TEST_CHECK(run, (A->MoveCount == 0) && (A->LostCount == 1));
            Release(run, &Fixture, GameKey_MouseRight);
            TEST_CHECK(run, (A->ReleaseCount == 0) && (A->LostCount == 1));
            // The press stays the screen's: the button reads consumed until it came up, so the game never saw it.
            TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Base.Input, GameKey_MouseRight));
        }
        Routing_Deconstruct(run, &Fixture);
    }
}

/* Every press the widget holds is told; another widget's stays; an input-disabled ancestor changes nothing: its children still take input. */
static void OnlyThePressesThatAreLostAreTold(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Root = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* A = AddProbe(run, &Fixture, IdOf(Root), 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, IdOf(Root), 0.5f, 0.0f, 0.5f, 1.0f, false);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        Press(run, &Fixture, GameKey_MouseRight);
        PointAt(&Fixture, 0.75f, 0.5f);
        Press(run, &Fixture, GameKey_MouseMiddle);
        TEST_CHECK(run, B->PressCount == 1);

        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&Root->Base, false));
        TEST_CHECK(run, (A->LostCount == 0) && (B->LostCount == 0) && (Root->LostCount == 0));

        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&A->Base, false));
        TEST_CHECK(run, (A->LostCount == 2) && (B->LostCount == 0) && UIScreen_HasHeldPress(&Fixture.Base.Screen));
        Release(run, &Fixture, GameKey_MouseMiddle);
        TEST_CHECK(run, (B->ReleaseCount == 1) && (B->LostCount == 0) && !UIScreen_HasHeldPress(&Fixture.Base.Screen));
        Release(run, &Fixture, GameKey_MouseLeft);
        Release(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, A->ReleaseCount == 0);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Destroying a widget stays silent, also through an ancestor, as the framework promises: whoever destroys it knows. */
static void ADestroyedWidgetHearsNothing(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Root = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        AddProbe(run, &Fixture, IdOf(Root), 0.25f, 0.25f, 0.5f, 0.5f, false);
        PointAt(&Fixture, 0.5f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        // The clock moves on every hook that is told anything: it stands still here.
        int Clock = Fixture.Clock;
        TEST_CHECK_SUCCESS(run, Widget_Destroy(&Root->Base));
        TEST_CHECK(run, Fixture.Clock == Clock);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.Base.Screen));
        Tick(run, &Fixture);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, Fixture.Clock == Clock);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A press that never reached its widget (a focus hook took it away first) has nothing to tell it. */
static void APressThatNeverArrivedIsNotLost(TestRun* run)
{
    for (int Case = 0; Case < 3; Case++)
    {
        RoutingFixture Fixture;
        if (Routing_Construct(run, &Fixture))
        {
            RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, true);
            A->OnFocus = (Case == 0) ? ScriptHideSelf : ((Case == 1) ? ScriptDetachSelf : ScriptDestroySelf);
            PointAt(&Fixture, 0.5f, 0.5f);
            Tick(run, &Fixture);
            int Clock = Fixture.Clock;
            Press(run, &Fixture, GameKey_MouseLeft);
            TEST_CHECK(run, Fixture.Clock == Clock); // Neither the press nor its loss.
            TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.Base.Screen));
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Base.Input, GameKey_MouseLeft));
            Release(run, &Fixture, GameKey_MouseLeft);
            TEST_CHECK(run, Fixture.Clock == Clock);
        }
        Routing_Deconstruct(run, &Fixture);
    }
}

/* A focus hook may hand the press on before it arrives: the new widget gets it, and the old one never had anything to lose. */
static void AFocusHookMayHandThePressOnBeforeItArrives(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, true);
        A->OnFocus = ScriptTransferLeftToTarget;
        A->TargetId = IdOf(B);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (A->FocusEnableCount == 1) && (A->PressCount == 0) && (A->LostCount == 0));
        TEST_CHECK(run, B->PressCount == 1);
        TEST_CHECK(run, IsClose(LastMouse(B)->ClickStartScreenPosition.x, 0.25f));
        // It has the press like any other: hiding it now loses it.
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&B->Base, false));
        TEST_CHECK(run, (B->LostCount == 1) && !UIScreen_HasHeldPress(&Fixture.Base.Screen));
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&B->Base, true));
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, B->ReleaseCount == 0);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Hooks may do anything: one that hides what holds a press from inside a drag ends it there; one told of a loss may destroy itself. */
static void HooksMayTakeAPressAwayOrDestroyTheirWidget(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        A->OnMove = ScriptHideSelf;
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.3f, 0.5f);
        TestUI_Release(&Fixture.Base, GameKey_MouseLeft); // The move's tick is the release's: its hook ends it first.
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->MoveCount == 1) && (A->LostCount == 1) && (A->ReleaseCount == 0));

        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        uint64_t BId = IdOf(B);
        B->OnLost = ScriptDestroySelf;
        PointAt(&Fixture, 0.75f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&B->Base, false));
        TEST_CHECK(run, !UIScreen_IsWidgetAlive(&Fixture.Base.Screen, BId));
        Tick(run, &Fixture);
        Release(run, &Fixture, GameKey_MouseLeft);
    }
    Routing_Deconstruct(run, &Fixture);
}


/* A hook that takes the press away from inside a drag leaves nothing to release: what lies under the pointer hears no release either. */
static void AHookThatEndedThePressLeavesNoRelease(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Under = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        A->OnMove = ScriptHideSelf;
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.3f, 0.5f);
        TestUI_Release(&Fixture.Base, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->LostCount == 1) && (A->ReleaseCount == 0) && (A->ReleaseOverCount == 0));
        TEST_CHECK(run, (Under->ReleaseCount == 0) && (Under->ReleaseOverCount == 0));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A release that came while the screen was not updated has no edge left to see: the button being up ends the press all the same. */
static void AReleaseNeverSeenStillEndsThePress(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        PointAt(&Fixture, 0.5f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        Fixture.Base.Sample.KeyStates[GameKey_MouseLeft] = 0U;
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->ReleaseCount == 1) && !UIScreen_HasHeldPress(&Fixture.Base.Screen));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Told of a lost press, a widget may destroy another that lost one in the same detach: that one hears nothing more, and nothing breaks. */
static void TheHookOfOneLossMayDestroyTheOtherHolder(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Parent = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* A = AddProbe(run, &Fixture, IdOf(Parent), 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, IdOf(Parent), 0.5f, 0.0f, 0.5f, 1.0f, false);
        A->OnLost = ScriptDestroyTarget;
        A->TargetId = IdOf(B);
        B->OnLost = ScriptDestroySelf; // It must not run: the widget is gone.
        uint64_t BId = IdOf(B);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        Press(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, (A->PressCount == 1) && (B->PressCount == 1));
        TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Base.Screen, &Parent->Base));
        TEST_CHECK(run, (A->LostCount == 1) && !UIScreen_IsWidgetAlive(&Fixture.Base.Screen, BId));
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.Base.Screen));
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.Base.Screen, &Parent->Base));
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: whose the pointer is.
/* The pointer is the UI's over a widget, and the game's over nothing or off the surface. */
static void ThePointerIsClaimedOverAWidgetAlone(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        GameInput* Input = &Fixture.Base.Input;
        PointAt(&Fixture, 0.25f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, GameInput_IsPointerConsumed(Input));
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, !GameInput_IsPointerConsumed(Input));
        PointAt(&Fixture, 1.5f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, !GameInput_IsPointerConsumed(Input));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A widget added under a pointer that stands still is hovered, a child as much as a root. */
static void AWidgetAddedUnderAStillPointerIsHovered(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Root = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        PointAt(&Fixture, 0.5f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&Root->Base));
        RoutingProbe* Child = AddProbe(run, &Fixture, IdOf(Root), 0.0f, 0.0f, 1.0f, 1.0f, false);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&Child->Base) && !Widget_IsHovered(&Root->Base));
        RoutingProbe* Above = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&Above->Base) && !Widget_IsHovered(&Child->Base));

        // Linked again with nothing else having changed: only the link can say so.
        TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(&Fixture.Base.Screen, &Above->Base));
        TEST_CHECK_SUCCESS(run, Widget_RemoveSubWidget(&Root->Base, &Child->Base));
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&Root->Base));
        TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(&Root->Base, &Child->Base));
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&Child->Base) && !Widget_IsHovered(&Root->Base));
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.Base.Screen, &Above->Base));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* An element's mouse event must have its data. */
static void AMouseEventNeedsItsData(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK_ERROR(run, UIScreen_PostWidgetMouseEvent(&Fixture.Base.Screen, IdOf(A), 1, NULL), ErrorCode_IllegalArgument);
        UIEventMouse Mouse = { .Button = GameKey_MouseLeft };
        TEST_CHECK_ERROR(run, UIScreen_PostWidgetMouseEvent(NULL, IdOf(A), 1, &Mouse), ErrorCode_IllegalArgument);
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: handing a press on.
/* After the transfer the drags and the release go to the new widget, with the press's start, its time and its count kept; the old one hears
 * it lost, at once. */
static void APressCanBeHandedOn(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.3f, 0.5f);
        Tick(run, &Fixture);
        double Start = Fixture.Base.Time;

        TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(&Fixture.Base.Screen, GameKey_MouseLeft, IdOf(B)));
        TEST_CHECK(run, (A->LostCount == 1) && (A->LostButton == GameKey_MouseLeft));
        int Moves = A->MoveCount;

        Fixture.Base.Time += 1.0;
        PointAt(&Fixture, 0.4f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->MoveCount == Moves) && (B->MoveCount == 1));
        TEST_CHECK(run, IsClose(LastMouse(B)->ClickStartScreenPosition.x, 0.25f) && IsClose(LastMouse(B)->ClickStartWidgetPosition.x, -0.5f));
        TEST_CHECK(run, IsClose(LastMouse(B)->DeltaPixels.x, 100.0f) && (LastMouse(B)->ClickCount == 2U));
        TEST_CHECK(run, LastMouse(B)->DurationSeconds >= (Fixture.Base.Time - Start - 0.01));
        TEST_CHECK(run, B->PressCount == 0);

        // It comes up over the old one: that is another widget now.
        int Releases = A->ReleaseCount;
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (B->ReleaseCount == 1) && (A->ReleaseCount == Releases) && (A->ReleaseOverCount == 1));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* What cannot take a press is refused and changes nothing; handing it to the one that holds it does nothing; the hook may destroy either. */
static void AHandedPressIsChecked(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        TEST_CHECK_ERROR(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, IdOf(B)), ErrorCode_InvalidOperation); // Nothing held.
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);

        TEST_CHECK_ERROR(run, UIScreen_TransferPress(NULL, GameKey_MouseLeft, IdOf(B)), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_TransferPress(Screen, GameKey_A, IdOf(B)), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_TransferPress(Screen, GameKey_MouseRight, IdOf(B)), ErrorCode_InvalidOperation); // Another button.
        TEST_CHECK_ERROR(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, WIDGET_ID_INVALID), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&B->Base, false));
        TEST_CHECK_ERROR(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, IdOf(B)), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&B->Base, true));
        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&B->Base, false));
        TEST_CHECK_ERROR(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, IdOf(B)), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&B->Base, true));
        TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(Screen, &B->Base));
        TEST_CHECK_ERROR(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, IdOf(B)), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(Screen, &B->Base));
        TEST_CHECK(run, A->LostCount == 0);

        TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, IdOf(A)));
        TEST_CHECK(run, A->LostCount == 0);

        // The old widget's hook destroys the new one: the press is over, with no one to tell.
        A->TargetId = IdOf(B);
        A->OnLost = ScriptDestroyTarget;
        uint64_t BId = IdOf(B);
        TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, BId));
        TEST_CHECK(run, (A->LostCount == 1) && !UIScreen_IsWidgetAlive(Screen, BId) && !UIScreen_HasHeldPress(Screen));
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, A->ReleaseCount == 0);

        // A hook handing the press on from inside a drag: the drag's release goes to the new one in the same tick.
        RoutingProbe* C = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        A->OnLost = NULL;
        A->TargetId = IdOf(C);
        A->OnMove = ScriptTransferLeftToTarget;
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.3f, 0.5f);
        TestUI_Release(&Fixture.Base, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, (C->ReleaseCount == 1) && (A->LostCount == 2));
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: moving a widget.
/* Moved to another parent and to the roots it keeps what points at it, with no hook saying otherwise. */
static void AMovedWidgetKeepsWhatPointsAtIt(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* First = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Second = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* A = AddProbe(run, &Fixture, IdOf(First), 0.0f, 0.0f, 1.0f, 1.0f, true);
        RoutingProbe* Inner = AddProbe(run, &Fixture, IdOf(A), 0.0f, 0.0f, 0.5f, 1.0f, true);
        PointAt(&Fixture, 0.1f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        Fixture.Base.Sample.KeyStates[GameKey_F] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        Inner->IsHandlingKeys = true;
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == IdOf(Inner)) && Widget_IsHovered(&Inner->Base));
        int HoverEnds = Inner->HoverEndCount;
        int FocusDisables = Inner->FocusDisableCount;

        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, &Second->Base));
        TEST_CHECK(run, (Widget_GetParent(&A->Base) == &Second->Base) && (Widget_GetSubWidgetCount(&First->Base) == 0U) && (Widget_GetSubWidgetCount(&Second->Base) == 1U));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == IdOf(Inner)) && Widget_IsFocused(&Inner->Base) && Widget_IsHovered(&Inner->Base));
        TEST_CHECK(run, (Inner->HoverEndCount == HoverEnds) && (Inner->FocusDisableCount == FocusDisables) && (Inner->LostCount == 0));
        TEST_CHECK(run, !Widget_IsTabbed(&A->Base)); // Nobody is navigating: a move enters nothing.
        // The press goes on, in the same tick as the hit test finds another widget there, and the held key stays its owner's.
        PointAt(&Fixture, 0.1f, 0.6f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Inner->MoveCount == 1) && (Inner->LostCount == 0));

        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, NULL));
        TEST_CHECK(run, Widget_IsScreenRoot(&A->Base) && (Widget_GetParent(&A->Base) == NULL) && (Widget_GetSubWidgetCount(&Second->Base) == 0U));
        TEST_CHECK(run, UIScreen_GetRootCount(Screen) == 3U);
        Fixture.Base.Sample.KeyStates[GameKey_F] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_REPEATED;
        Tick(run, &Fixture);
        TEST_CHECK(run, (Inner->LastKey.Key == GameKey_F) && (Inner->LastKey.Type == UIKeyboardInputType_KeyRepeat));
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Inner->ReleaseCount == 1) && (Inner->LostCount == 0));

        // And from the roots under a parent again; to where it is already, nothing.
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, &First->Base));
        TEST_CHECK(run, !Widget_IsScreenRoot(&A->Base) && (Widget_GetParent(&A->Base) == &First->Base) && (UIScreen_GetRootCount(Screen) == 2U));
        uint64_t Sequence = A->Base._sequence;
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, &First->Base));
        TEST_CHECK(run, A->Base._sequence == Sequence);
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&Second->Base, NULL));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Cycles, too deep a tree, another screen, being destroyed or not attached are refused, and nothing changes. */
static void AMoveIsChecked(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Root = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Child = AddProbe(run, &Fixture, IdOf(Root), 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Grandchild = AddProbe(run, &Fixture, IdOf(Child), 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK_ERROR(run, Widget_MoveTo(NULL, &Root->Base), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Child->Base, &Child->Base), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Child->Base, &Grandchild->Base), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Root->Base, &Grandchild->Base), ErrorCode_IllegalArgument);
        TEST_CHECK(run, (Widget_GetParent(&Grandchild->Base) == &Child->Base) && (Widget_GetParent(&Child->Base) == &Root->Base));

        // Not attached.
        RoutingProbe* Loose = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(Screen, &Loose->Base));
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Loose->Base, &Root->Base), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Loose->Base, NULL), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Grandchild->Base, &Loose->Base), ErrorCode_InvalidOperation);

        // Being destroyed.
        RoutingProbe* Doomed = AddProbe(run, &Fixture, IdOf(Root), 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Other = AddProbe(run, &Fixture, IdOf(Root), 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK_SUCCESS(run, Widget_Destroy(&Doomed->Base));
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Doomed->Base, NULL), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Other->Base, &Doomed->Base), ErrorCode_InvalidOperation);
        Tick(run, &Fixture);

        // Another screen.
        UIScreen OtherScreen;
        if (TEST_CHECK_SUCCESS(run, UIScreen_Construct(&OtherScreen, &Fixture.Base.Widgets, NULL)))
        {
            Widget* Foreign = NULL;
            TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.Base.Widgets, &OtherScreen, Fixture.ProbeType, &Fixture.Clock, &Foreign));
            TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Foreign));
            TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&OtherScreen, Foreign));
            TEST_CHECK_ERROR(run, Widget_MoveTo(&Other->Base, Foreign), ErrorCode_IllegalArgument);
            TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&OtherScreen));
        }

        // Too deep: a chain of the most levels takes no more; its bottom alone may move within it.
        uint64_t ChainId = IdOf(Other);
        RoutingProbe* Bottom = Other;
        for (size_t Level = 2U; Level < WIDGET_MAX_TREE_DEPTH; Level++)
        {
            Bottom = AddProbe(run, &Fixture, ChainId, 0.0f, 0.0f, 1.0f, 1.0f, false);
            ChainId = IdOf(Bottom);
        }
        TEST_CHECK_ERROR(run, Widget_MoveTo(&Child->Base, &Bottom->Base), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Widget_GetParent(&Child->Base) == &Root->Base);
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&Grandchild->Base, Widget_GetParent(&Bottom->Base)));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A moved widget lands on top of its new siblings of equal z, and what caches its sizes hears; the bounds and the hit test follow at once. */
static void AMoveInvalidatesWhatDependsOnTheTree(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* First = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Second = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Sibling = AddProbe(run, &Fixture, IdOf(Second), 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* A = AddProbe(run, &Fixture, IdOf(First), 0.0f, 0.0f, 1.0f, 1.0f, false);
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&Sibling->Base));
        Rectangle Before = Widget_GetEstimatedBounds(&Second->Base);
        TEST_CHECK(run, IsClose(Before.width, 0.5f));

        First->Base._isMeasureValid = true;
        Second->Base._isMeasureValid = true;
        A->Base._isMeasureValid = true;
        A->Base._size = (Vector2){ .x = 3.0f, .y = 1.0f }; // Bigger than its new parent, to show in the bounds.
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, &Second->Base));
        TEST_CHECK(run, !First->Base._isMeasureValid && !Second->Base._isMeasureValid && !A->Base._isMeasureValid);
        Rectangle After = Widget_GetEstimatedBounds(&Second->Base);
        TEST_CHECK(run, IsClose(After.width, 1.5f));
        Rectangle FirstBounds = Widget_GetEstimatedBounds(&First->Base);
        TEST_CHECK(run, IsClose(FirstBounds.width, 0.5f));

        // On top of the sibling that was there.
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&A->Base) && !Widget_IsHovered(&Sibling->Base));
        Widget* Top = NULL;
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(Screen, (Vector2){ .x = 0.75f, .y = 0.5f }, &Top) && (Top == &A->Base));

        // Moved to the roots it lands on top of them; an unlink elsewhere keeps the rest in order.
        RoutingProbe* Above = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(Screen, (Vector2){ .x = 0.75f, .y = 0.5f }, &Top) && (Top == &Above->Base));
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&Sibling->Base, NULL));
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(Screen, (Vector2){ .x = 0.75f, .y = 0.5f }, &Top) && (Top == &Sibling->Base));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Moved under something hidden, it is hidden: the focus and the press go, as hiding takes them. */
static void MovedUnderSomethingHiddenItLosesFocusAndPress(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, true);
        RoutingProbe* Hidden = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.1f, 0.1f, false);
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&Hidden->Base, false));
        PointAt(&Fixture, 0.5f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(A));
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, &Hidden->Base));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID) && !Widget_IsFocused(&A->Base) && (A->LostCount == 1));
        TEST_CHECK(run, !UIScreen_HasHeldPress(Screen));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* While navigating, the entered path stays a path down the tree: it follows focus, keeping what is still on it tabbed. With nothing focused,
 * what no longer follows is left. Destroying what it passed through after that leaves nothing tabbed behind. */
static void AMoveKeepsTheEnteredPathAPath(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Outer = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, true);
        RoutingProbe* A = AddProbe(run, &Fixture, IdOf(Outer), 0.0f, 0.0f, 1.0f, 1.0f, true);
        RoutingProbe* Inner = AddProbe(run, &Fixture, IdOf(A), 0.0f, 0.0f, 1.0f, 1.0f, true);
        RoutingProbe* Elsewhere = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, &Inner->Base));
        TestUI_Tap(run, &Fixture.Base, GameKey_Tab);
        TEST_CHECK(run, UIScreen_IsNavigating(Screen) && Widget_IsTabbed(&Outer->Base) && Widget_IsTabbed(&A->Base) && !Widget_IsTabbed(&Inner->Base));

        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, &Elsewhere->Base));
        TEST_CHECK(run, UIScreen_IsNavigating(Screen) && (UIScreen_GetFocusedWidgetId(Screen) == IdOf(Inner)));
        TEST_CHECK(run, !Widget_IsTabbed(&Outer->Base) && (Outer->TabbedDisableCount == 1));
        TEST_CHECK(run, Widget_IsTabbed(&A->Base) && (A->TabbedDisableCount == 0));

        // With nothing focused: the path stays while it is one, and is cut where it stops.
        TEST_CHECK_SUCCESS(run, Widget_Destroy(&Inner->Base));
        TEST_CHECK(run, UIScreen_IsNavigating(Screen) && Widget_IsTabbed(&A->Base));
        RoutingProbe* Fresh = AddProbe(run, &Fixture, IdOf(Outer), 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&Fresh->Base, NULL));
        TEST_CHECK(run, Widget_IsTabbed(&A->Base));
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, NULL));
        TEST_CHECK(run, Widget_IsTabbed(&A->Base));
        TEST_CHECK_SUCCESS(run, Widget_Destroy(&Elsewhere->Base));
        TestUI_Tap(run, &Fixture.Base, GameKey_Escape);
        TEST_CHECK(run, !Widget_IsTabbed(&A->Base));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* With nothing focused, what is moved out from under the one before it on the path is left, and the path stops there. */
static void AMoveWithNothingFocusedCutsThePath(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Outer = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, true);
        RoutingProbe* A = AddProbe(run, &Fixture, IdOf(Outer), 0.0f, 0.0f, 1.0f, 1.0f, true);
        RoutingProbe* Inner = AddProbe(run, &Fixture, IdOf(A), 0.0f, 0.0f, 1.0f, 1.0f, true);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, &Inner->Base));
        TestUI_Tap(run, &Fixture.Base, GameKey_Tab);
        TEST_CHECK_SUCCESS(run, Widget_Destroy(&Inner->Base));
        TEST_CHECK(run, UIScreen_IsNavigating(Screen) && (UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID));
        TEST_CHECK(run, Widget_IsTabbed(&Outer->Base) && Widget_IsTabbed(&A->Base));

        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&A->Base, NULL));
        TEST_CHECK(run, Widget_IsTabbed(&Outer->Base) && !Widget_IsTabbed(&A->Base) && (A->TabbedDisableCount == 1));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Undocking in one tick: a tab's press handed to the window it became, moved to the roots, and the drag carries on where it was. */
static void AMoveAndATransferInOneTickCarryTheDragOn(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Dock = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 0.5f, false);
        RoutingProbe* Tab = AddProbe(run, &Fixture, IdOf(Dock), 0.0f, 0.0f, 0.2f, 1.0f, false);
        RoutingProbe* Window = AddProbe(run, &Fixture, IdOf(Dock), 0.2f, 0.0f, 0.8f, 1.0f, true);
        PointAt(&Fixture, 0.1f, 0.25f);
        Press(run, &Fixture, GameKey_MouseLeft);

        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, &Window->Base));
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&Window->Base, NULL));
        TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, IdOf(Window)));
        TEST_CHECK(run, (Tab->LostCount == 1) && (Window->LostCount == 0) && (UIScreen_GetFocusedWidgetId(Screen) == IdOf(Window)));

        PointAt(&Fixture, 0.5f, 0.75f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Window->MoveCount == 1) && (Tab->MoveCount == 0) && IsClose(LastMouse(Window)->ClickStartScreenPosition.x, 0.1f));
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Window->ReleaseCount == 1) && (Tab->ReleaseCount == 0));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A hook may move the widget from inside its own press. */
static void AHookMayMoveItsOwnWidget(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Parent = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* A = AddProbe(run, &Fixture, IdOf(Parent), 0.0f, 0.0f, 1.0f, 1.0f, false);
        A->OnPress = ScriptMoveSelfToRoots;
        PointAt(&Fixture, 0.5f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, Widget_IsScreenRoot(&A->Base) && (A->LostCount == 0));
        PointAt(&Fixture, 0.6f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, A->MoveCount == 1);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, A->ReleaseCount == 1);
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: the wheel.
/* From the widget under the pointer up through its ancestors until one handles it, each told its own position, with the modifiers. */
static void TheWheelGoesUpTheTree(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Root = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Middle = AddProbe(run, &Fixture, IdOf(Root), 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Leaf = AddProbe(run, &Fixture, IdOf(Middle), 0.0f, 0.0f, 0.5f, 1.0f, false);
        PointAt(&Fixture, 0.125f, 0.5f);
        Tick(run, &Fixture);

        // Nobody handles it: all of them hear, in order, and it stays the UI's.
        Fixture.Base.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
        Fixture.Base.Sample.WheelDelta = (Vector2){ .x = -1.0f, .y = 2.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, (Leaf->ScrollCount == 1) && (Middle->ScrollCount == 1) && (Root->ScrollCount == 1));
        TEST_CHECK(run, (Leaf->ScrollOrder < Middle->ScrollOrder) && (Middle->ScrollOrder < Root->ScrollOrder));
        TEST_CHECK(run, (Leaf->LastScroll.ScrollDelta.x == -1.0f) && (Leaf->LastScroll.ScrollDelta.y == 2.0f) && (Leaf->LastScroll.Modifiers == GAME_KEY_MODIFIER_CONTROL));
        TEST_CHECK(run, IsClose(Leaf->LastScroll.ScreenPosition.x, 0.125f) && IsClose(Leaf->LastScroll.WidgetPosition.x, 0.5f));
        TEST_CHECK(run, IsClose(Middle->LastScroll.WidgetPosition.x, 0.25f) && IsClose(Root->LastScroll.WidgetPosition.x, 0.125f));
        TEST_CHECK(run, GameInput_GetWheelDelta(&Fixture.Base.Input).y == 0.0f);
        Fixture.Base.Sample.KeyStates[GameKey_LeftControl] = 0U;

        // Handled, it stops there.
        Middle->IsHandlingScroll = true;
        TestUI_Wheel(&Fixture.Base, 1.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Leaf->ScrollCount == 2) && (Middle->ScrollCount == 2) && (Root->ScrollCount == 1));
        TEST_CHECK(run, GameInput_GetWheelDelta(&Fixture.Base.Input).y == 0.0f);
        Middle->IsHandlingScroll = false;

        // An ancestor that takes no input is passed; it does not hold the rest back.
        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&Middle->Base, false));
        TestUI_Wheel(&Fixture.Base, 1.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Leaf->ScrollCount == 3) && (Middle->ScrollCount == 2) && (Root->ScrollCount == 2));
        TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&Middle->Base, true));

        // No wheel, no call.
        Tick(run, &Fixture);
        TEST_CHECK(run, Leaf->ScrollCount == 3);

        // A wheel moving only across counts.
        Fixture.Base.Sample.WheelDelta = (Vector2){ .x = 1.0f, .y = 0.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, Leaf->ScrollCount == 4);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* What a hook takes away (hidden, detached, made an overlay) hears no more of what was going up the tree, the wheel or a key. */
static void WhatAHookTookAwayHearsNoMoreOnTheWay(TestRun* run)
{
    RoutingProbeScript Scripts[] = { ScriptHideParent, ScriptDetachParent, ScriptMakeParentOverlay };
    for (size_t Index = 0U; Index < (sizeof(Scripts) / sizeof(Scripts[0])); Index++)
    {
        RoutingFixture Fixture;
        if (Routing_Construct(run, &Fixture))
        {
            RoutingProbe* Parent = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
            RoutingProbe* Child = AddProbe(run, &Fixture, IdOf(Parent), 0.0f, 0.0f, 1.0f, 1.0f, true);
            Child->OnScroll = Scripts[Index];
            PointAt(&Fixture, 0.5f, 0.5f);
            Tick(run, &Fixture);
            TestUI_Wheel(&Fixture.Base, 1.0f);
            Tick(run, &Fixture);
            TEST_CHECK(run, (Child->ScrollCount == 1) && (Parent->ScrollCount == 0));
        }
        Routing_Deconstruct(run, &Fixture);

        if (Routing_Construct(run, &Fixture))
        {
            RoutingProbe* Parent = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
            RoutingProbe* Child = AddProbe(run, &Fixture, IdOf(Parent), 0.0f, 0.0f, 1.0f, 1.0f, true);
            Child->OnKey = Scripts[Index];
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Base.Screen, &Child->Base));
            TestUI_Press(&Fixture.Base, GameKey_F);
            Tick(run, &Fixture);
            TEST_CHECK(run, (Child->KeyCount == 1) && (Parent->KeyCount == 0));
            TestUI_Release(&Fixture.Base, GameKey_F);
            Tick(run, &Fixture);
        }
        Routing_Deconstruct(run, &Fixture);
    }
}

/* It is the game's wherever no widget is under the pointer; the rest of a walk survives a hook taking a widget away. */
static void TheWheelOverNothingIsTheGames(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* Parent = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Child = AddProbe(run, &Fixture, IdOf(Parent), 0.0f, 0.0f, 0.5f, 1.0f, false);
        PointAt(&Fixture, 0.75f, 0.5f);
        TestUI_Wheel(&Fixture.Base, 1.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Parent->ScrollCount == 0) && (GameInput_GetWheelDelta(&Fixture.Base.Input).y == 1.0f));

        // Off the surface, also while a press is held.
        PointAt(&Fixture, 0.1f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 2.0f, 0.5f);
        TestUI_Wheel(&Fixture.Base, 1.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Child->ScrollCount == 0) && (GameInput_GetWheelDelta(&Fixture.Base.Input).y == 1.0f));
        Release(run, &Fixture, GameKey_MouseLeft);

        // The hook destroys the parent it would go to next: the walk ends there.
        PointAt(&Fixture, 0.1f, 0.5f);
        Tick(run, &Fixture);
        Child->TargetId = IdOf(Parent);
        Child->OnScroll = ScriptDestroyTarget;
        TestUI_Wheel(&Fixture.Base, 1.0f);
        Tick(run, &Fixture);
        Tick(run, &Fixture);
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: layers.
/* Hit testing goes by layer, then z, then insertion; so does the draw order. */
static void RootsGoByLayerThenZThenInsertion(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Popup = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Floating = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Maximized = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Normal = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Background = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Overlay = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK(run, UIScreen_GetRootLayer(Screen, &Normal->Base) == UIScreenLayer_Normal);
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Popup->Base, UIScreenLayer_Popup));
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Floating->Base, UIScreenLayer_Floating));
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Maximized->Base, UIScreenLayer_Maximized));
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Background->Base, UIScreenLayer_Background));
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Overlay->Base, UIScreenLayer_Overlay));
        TEST_CHECK(run, UIScreen_GetRootLayer(Screen, &Overlay->Base) == UIScreenLayer_Overlay);

        // The draw order, whatever the z says: a higher z in a lower layer stays under.
        TEST_CHECK_SUCCESS(run, Widget_SetZLayer(&Background->Base, 100.0f));
        TEST_CHECK_SUCCESS(run, Widget_SetZLayer(&Popup->Base, -5.0f));
        const RoutingProbe* Order[] = { Background, Normal, Floating, Maximized, Popup, Overlay };
        for (size_t Index = 0U; Index < (sizeof(Order) / sizeof(Order[0])); Index++)
        {
            TEST_CHECK(run, Widget_InternalGetDrawOrderAt(&Screen->_rootDrawOrder, Index) == &Order[Index]->Base);
        }

        // Hit testing: the overlay never; then down the layers as each stops taking input.
        Widget* Top = NULL;
        Vector2 Middle = { .x = 0.5f, .y = 0.5f };
        const RoutingProbe* Hits[] = { Popup, Maximized, Floating, Normal, Background };
        for (size_t Index = 0U; Index < (sizeof(Hits) / sizeof(Hits[0])); Index++)
        {
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(Screen, Middle, &Top) && (Top == &Hits[Index]->Base));
            TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&((RoutingProbe*)Hits[Index])->Base, false));
        }
        TEST_CHECK(run, !UIScreen_TryGetTopmostAt(Screen, Middle, &Top) && (Top == NULL));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A press raises its root within Normal and Floating, within its own layer only; the others stay. */
static void APressRaisesOnlyWithinItsLayer(TestRun* run)
{
    const struct { UIScreenLayer Layer; bool IsRaised; } Cases[] =
    {
        { UIScreenLayer_Background, false }, { UIScreenLayer_Normal, true }, { UIScreenLayer_Floating, true },
        { UIScreenLayer_Maximized, false }, { UIScreenLayer_Popup, false }
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        RoutingFixture Fixture;
        if (Routing_Construct(run, &Fixture))
        {
            UIScreen* Screen = &Fixture.Base.Screen;
            RoutingProbe* Pressed = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
            RoutingProbe* Other = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.25f, 1.0f, false);
            RoutingProbe* AboveAll = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.25f, 1.0f, false);
            TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Pressed->Base, Cases[Index].Layer));
            TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Other->Base, Cases[Index].Layer));
            TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &AboveAll->Base, UIScreenLayer_Overlay));
            float OtherZ = Widget_GetZLayer(&Other->Base);

            ClickAt(run, &Fixture, GameKey_MouseLeft, 0.4f, 0.5f); // On the pressed one only.
            TEST_CHECK(run, Pressed->PressCount == 1);
            Widget* Top = NULL;
            TEST_CHECK(run, UIScreen_TryGetTopmostAt(Screen, (Vector2){ .x = 0.1f, .y = 0.5f }, &Top));
            TEST_CHECK(run, Top == (Cases[Index].IsRaised ? &Pressed->Base : &Other->Base));
            TEST_CHECK(run, (Widget_GetZLayer(&Pressed->Base) > OtherZ) == Cases[Index].IsRaised);
            TEST_CHECK(run, Widget_GetZLayer(&AboveAll->Base) == 0.0f);

            // Raised, it does not go on growing.
            float Raised = Widget_GetZLayer(&Pressed->Base);
            ClickAt(run, &Fixture, GameKey_MouseLeft, 0.4f, 0.5f);
            TEST_CHECK(run, Widget_GetZLayer(&Pressed->Base) == Raised);
        }
        Routing_Deconstruct(run, &Fixture);
    }
}

/* Raising looks at the root's own layer: a root in the layer above with a far higher z does not push it there. */
static void RaisingStaysWithinTheLayer(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Above = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.75f, 0.0f, 0.25f, 1.0f, false);
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Above->Base, UIScreenLayer_Floating));
        TEST_CHECK_SUCCESS(run, Widget_SetZLayer(&Above->Base, 500.0f));
        TEST_CHECK_SUCCESS(run, UIScreen_BringToTop(Screen, &A->Base));
        TEST_CHECK(run, Widget_GetZLayer(&A->Base) == (Widget_GetZLayer(&B->Base) + 1.0f));
        // On top of its layer already, with another layer above it: nothing to do.
        float Z = Widget_GetZLayer(&A->Base);
        TEST_CHECK_SUCCESS(run, UIScreen_BringToTop(Screen, &A->Base));
        TEST_CHECK(run, Widget_GetZLayer(&A->Base) == Z);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* An overlay takes no input: no pointer, no focus, no navigation. Becoming one takes away what it had. */
static void AnOverlayTakesNoInput(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Under = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Overlay = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, true);
        RoutingProbe* Inner = AddProbe(run, &Fixture, IdOf(Overlay), 0.0f, 0.0f, 1.0f, 1.0f, true);
        PointAt(&Fixture, 0.5f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&Inner->Base));
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (Inner->PressCount == 1) && (UIScreen_GetFocusedWidgetId(Screen) == IdOf(Inner)));

        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Overlay->Base, UIScreenLayer_Overlay));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID) && (Inner->LostCount == 1));
        Tick(run, &Fixture);
        TEST_CHECK(run, !Widget_IsHovered(&Inner->Base) && Widget_IsHovered(&Under->Base));
        Release(run, &Fixture, GameKey_MouseLeft);
        ClickAt(run, &Fixture, GameKey_MouseLeft, 0.5f, 0.5f);
        TEST_CHECK(run, (Under->PressCount == 1) && (Inner->PressCount == 1));
        TestUI_Wheel(&Fixture.Base, 1.0f);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Under->ScrollCount == 1) && (Inner->ScrollCount == 0));

        // Not by code either, and navigation passes it.
        TEST_CHECK_ERROR(run, UIScreen_SetFocusedWidget(Screen, &Inner->Base), ErrorCode_InvalidOperation);
        TestUI_Tap(run, &Fixture.Base, GameKey_Tab);
        TEST_CHECK(run, !UIScreen_IsNavigating(Screen) && (UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID));

        // Back from it, it takes input again.
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Overlay->Base, UIScreenLayer_Normal));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, &Inner->Base));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* The level navigation is on becomes an overlay: the path leaves it at once, and the next step goes on where focus can land. */
static void NavigationGoesOnWhenItsLevelBecomesAnOverlay(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Panel = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, true);
        RoutingProbe* Inner = AddProbe(run, &Fixture, IdOf(Panel), 0.0f, 0.0f, 1.0f, 0.5f, true);
        RoutingProbe* Other = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, true);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, &Inner->Base));
        TestUI_Tap(run, &Fixture.Base, GameKey_Tab);
        TEST_CHECK(run, UIScreen_IsNavigating(Screen) && Widget_IsTabbed(&Panel->Base));

        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Panel->Base, UIScreenLayer_Overlay));
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID) && !Widget_IsTabbed(&Panel->Base) && UIScreen_IsNavigating(Screen));
        TestUI_Tap(run, &Fixture.Base, GameKey_Tab);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == IdOf(Other));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* The focused widget moves into an overlay: focus goes first, so the path does not follow it there. With nothing left on its level navigation ends, and the next Tab starts it
 * where focus can land. */
static void AWidgetMovedIntoAnOverlayTakesNoPathWithIt(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Holder = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.2f, 0.2f, true);
        RoutingProbe* Other = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, true);
        RoutingProbe* Overlay = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.5f, 0.5f, 0.5f, true);
        RoutingProbe* Moving = AddProbe(run, &Fixture, IdOf(Holder), 0.0f, 0.0f, 1.0f, 1.0f, true);
        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Overlay->Base, UIScreenLayer_Overlay));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, &Moving->Base));
        TestUI_Tap(run, &Fixture.Base, GameKey_Tab);
        TEST_CHECK(run, UIScreen_IsNavigating(Screen) && Widget_IsTabbed(&Holder->Base));

        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&Moving->Base, &Overlay->Base));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(Screen) == WIDGET_ID_INVALID);
        TEST_CHECK(run, !Widget_IsTabbed(&Overlay->Base));
        TestUI_Tap(run, &Fixture.Base, GameKey_Tab);
        TEST_CHECK(run, !UIScreen_IsNavigating(Screen) && !Widget_IsTabbed(&Holder->Base));
        TestUI_Tap(run, &Fixture.Base, GameKey_Tab);
        uint64_t Focused = UIScreen_GetFocusedWidgetId(Screen);
        TEST_CHECK(run, UIScreen_IsNavigating(Screen) && ((Focused == IdOf(Holder)) || (Focused == IdOf(Other))));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Entering a root raises it only where a press would: within the layers made for that. */
static void EnteringARootRaisesItOnlyWhereAPressWould(TestRun* run)
{
    const struct { UIScreenLayer Layer; bool IsRaised; } Cases[] =
    {
        { UIScreenLayer_Background, false }, { UIScreenLayer_Normal, true }, { UIScreenLayer_Floating, true }, { UIScreenLayer_Maximized, false }, { UIScreenLayer_Popup, false }
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        RoutingFixture Fixture;
        if (Routing_Construct(run, &Fixture))
        {
            UIScreen* Screen = &Fixture.Base.Screen;
            RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, true);
            AddProbe(run, &Fixture, IdOf(A), 0.0f, 0.0f, 1.0f, 1.0f, true);
            RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
            TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &A->Base, Cases[Index].Layer));
            TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &B->Base, Cases[Index].Layer));
            float Before = Widget_GetZLayer(&A->Base);
            TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, &A->Base));
            TestUI_Tap(run, &Fixture.Base, GameKey_Enter);
            TEST_CHECK(run, Widget_IsTabbed(&A->Base));
            TEST_CHECK(run, (Widget_GetZLayer(&A->Base) > Before) == Cases[Index].IsRaised);
        }
        Routing_Deconstruct(run, &Fixture);
    }
}

/* A layer is set per root, and a root keeps it while it is away; what is not a root or not a layer is refused. */
static void ALayerBelongsToARoot(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Root = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        RoutingProbe* Child = AddProbe(run, &Fixture, IdOf(Root), 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK_ERROR(run, UIScreen_SetRootLayer(NULL, &Root->Base, UIScreenLayer_Popup), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_SetRootLayer(Screen, NULL, UIScreenLayer_Popup), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_SetRootLayer(Screen, &Child->Base, UIScreenLayer_Popup), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScreen_SetRootLayer(Screen, &Root->Base, UIScreenLayer_Count), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, UIScreen_SetRootLayer(Screen, &Root->Base, (UIScreenLayer)-1), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, UIScreen_GetRootLayer(Screen, &Root->Base) == UIScreenLayer_Normal);

        TEST_CHECK_SUCCESS(run, UIScreen_SetRootLayer(Screen, &Root->Base, UIScreenLayer_Popup));
        TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(Screen, &Root->Base));
        TEST_CHECK_ERROR(run, UIScreen_SetRootLayer(Screen, &Root->Base, UIScreenLayer_Normal), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(Screen, &Root->Base));
        TEST_CHECK(run, UIScreen_GetRootLayer(Screen, &Root->Base) == UIScreenLayer_Popup);

        // Under a parent it has no layer of its own, and draws with its siblings.
        RoutingProbe* Second = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&Root->Base, &Second->Base));
        TEST_CHECK(run, UIScreen_GetRootLayer(Screen, &Root->Base) == UIScreenLayer_Normal);
        RoutingProbe* Sibling = AddProbe(run, &Fixture, IdOf(Second), 0.0f, 0.0f, 1.0f, 1.0f, false);
        Widget* Top = NULL;
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(Screen, (Vector2){ .x = 0.5f, .y = 0.5f }, &Top) && (Top == &Sibling->Base));
        TEST_CHECK_SUCCESS(run, Widget_MoveTo(&Root->Base, NULL));
        TEST_CHECK(run, UIScreen_GetRootLayer(Screen, &Root->Base) == UIScreenLayer_Popup);
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(Screen, (Vector2){ .x = 0.5f, .y = 0.5f }, &Top) && (Top == &Child->Base));
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: a hidden-cursor drag.
/* Only a widget holding a press may ask; another one may not take it over; the request is the screen's to tell. */
static void ACaptureIsLegalOnlyDuringAPress(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        TEST_CHECK_ERROR(run, UIScreen_CapturePointer(NULL, IdOf(A)), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, UIScreen_CapturePointer(Screen, IdOf(A)), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, UIScreen_CapturePointer(Screen, WIDGET_ID_INVALID), ErrorCode_InvalidOperation);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(Screen));

        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK_ERROR(run, UIScreen_CapturePointer(Screen, IdOf(B)), ErrorCode_InvalidOperation); // It holds no press.
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(Screen, IdOf(A)));
        TEST_CHECK(run, UIScreen_IsPointerCaptured(Screen));
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(Screen, IdOf(A))); // Again: nothing changes.

        PointAt(&Fixture, 0.75f, 0.5f);
        Fixture.Base.ScreenInput.IsPointerRelative = false;
        Press(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, B->PressCount == 1);
        TEST_CHECK_ERROR(run, UIScreen_CapturePointer(Screen, IdOf(B)), ErrorCode_InvalidOperation); // Another has it.
        TEST_CHECK(run, UIScreen_IsPointerCaptured(Screen));

        UIScreen_ReleasePointerCapture(Screen);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(Screen));
        UIScreen_ReleasePointerCapture(Screen); // Nothing to end.
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(Screen, IdOf(B)));
        TEST_CHECK(run, UIScreen_IsPointerCaptured(Screen));
        Release(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(Screen));
        Release(run, &Fixture, GameKey_MouseLeft);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* While the host says the cursor is hidden: the position stands still whatever it says, hover does not change even if the tree does, and drags
 * carry the raw motion. */
static void ACapturedDragFreezesThePointerAndCarriesRawMotion(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(Screen, IdOf(A)));

        // The host hid the cursor: it says nothing of a position now, and a captured cursor reads as no pointer.
        Fixture.Base.ScreenInput.IsPointerRelative = true;
        Fixture.Base.ScreenInput.IsPointerPresent = false;
        Fixture.Base.ScreenInput.IsPointerTracked = false;
        Fixture.Base.ScreenInput.PointerPosition = (Vector2){ .x = 0.75f, .y = 0.1f };
        Fixture.Base.ScreenInput.PointerDeltaPixels = (Vector2){ .x = 37.0f, .y = -4.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->MoveCount == 1) && (LastMouse(A)->DeltaPixels.x == 37.0f) && (LastMouse(A)->DeltaPixels.y == -4.0f));
        TEST_CHECK(run, IsClose(LastMouse(A)->ScreenPosition.x, 0.25f) && IsClose(LastMouse(A)->ScreenPosition.y, 0.5f));
        TEST_CHECK(run, IsClose(UIScreen_GetPointerPosition(Screen).x, 0.25f) && UIScreen_IsPointerPresent(Screen));
        TEST_CHECK(run, Widget_IsHovered(&A->Base) && !Widget_IsHovered(&B->Base));
        TEST_CHECK(run, GameInput_IsPointerConsumed(&Fixture.Base.Input));

        // No motion, no move. The motion is each tick's own: nothing adds up.
        Fixture.Base.ScreenInput.PointerDeltaPixels = (Vector2){ .x = 0.0f, .y = 0.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, A->MoveCount == 1);
        Fixture.Base.ScreenInput.PointerDeltaPixels = (Vector2){ .x = 0.0f, .y = 5.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->MoveCount == 2) && (LastMouse(A)->DeltaPixels.x == 0.0f) && (LastMouse(A)->DeltaPixels.y == 5.0f));

        // The tree changing under the frozen pointer does not change hover.
        TEST_CHECK_SUCCESS(run, Widget_SetZLayer(&B->Base, 3.0f));
        TEST_CHECK_SUCCESS(run, Widget_SetSize(&B->Base, (Vector2){ .x = 1.0f, .y = 1.0f }));
        TEST_CHECK_SUCCESS(run, Widget_SetPosition(&B->Base, (Vector2){ .x = 0.0f, .y = 0.0f }));
        Fixture.Base.ScreenInput.PointerDeltaPixels = (Vector2){ .x = 1.0f, .y = 0.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&A->Base) && !Widget_IsHovered(&B->Base));

        // The press ends there, the position it froze at; then the screen takes the pointer in again.
        Fixture.Base.ScreenInput.PointerDeltaPixels = (Vector2){ .x = 0.0f, .y = 0.0f };
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, (A->ReleaseCount == 1) && IsClose(LastMouse(A)->ScreenPosition.x, 0.25f) && !UIScreen_IsPointerCaptured(Screen));
        Fixture.Base.ScreenInput.IsPointerRelative = false;
        PointAt(&Fixture, 0.25f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&B->Base) && !Widget_IsHovered(&A->Base));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A host that does not apply the request (a screen in the world) leaves the drag absolute; relative without a request means nothing. */
/* Raw motion along either axis alone is a drag; none is none. */
static void RawMotionAlongOneAxisIsADrag(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        A->OnPress = ScriptCapture;
        PointAt(&Fixture, 0.5f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        Fixture.Base.ScreenInput.IsPointerRelative = true;
        const struct { Vector2 Delta; int Moves; } Steps[] =
        {
            { { .x = 3.0f, .y = 0.0f }, 1 }, { { .x = 0.0f, .y = -2.0f }, 2 }, { { .x = 0.0f, .y = 0.0f }, 2 }
        };
        for (size_t Index = 0U; Index < (sizeof(Steps) / sizeof(Steps[0])); Index++)
        {
            Fixture.Base.ScreenInput.PointerDeltaPixels = Steps[Index].Delta;
            Tick(run, &Fixture);
            TEST_CHECK(run, A->MoveCount == Steps[Index].Moves);
        }
        TEST_CHECK(run, IsClose(LastMouse(A)->DeltaPixels.y, -2.0f) && IsClose(LastMouse(A)->DeltaPixels.x, 0.0f));
    }
    Routing_Deconstruct(run, &Fixture);
}

static void ACaptureNoHostAppliesStaysAbsolute(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(Screen, IdOf(A)));
        PointAt(&Fixture, 0.35f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, IsClose(LastMouse(A)->ScreenPosition.x, 0.35f) && IsClose(LastMouse(A)->DeltaPixels.x, 100.0f));
        Release(run, &Fixture, GameKey_MouseLeft);

        // A host's flag with no drag asked for: the position counts.
        Fixture.Base.ScreenInput.IsPointerRelative = true;
        Fixture.Base.ScreenInput.PointerDeltaPixels = (Vector2){ .x = 9.0f, .y = 9.0f };
        PointAt(&Fixture, 0.45f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, IsClose(UIScreen_GetPointerPosition(Screen).x, 0.45f));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* It ends with its widget's last press: released, lost, handed on or destroyed; asked for from a hook it holds. */
static void ACaptureEndsWithItsPress(TestRun* run)
{
    for (int Case = 0; Case < 6; Case++)
    {
        RoutingFixture Fixture;
        if (Routing_Construct(run, &Fixture))
        {
            UIScreen* Screen = &Fixture.Base.Screen;
            RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
            RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
            A->OnMove = ScriptCapture;
            PointAt(&Fixture, 0.25f, 0.5f);
            Press(run, &Fixture, GameKey_MouseLeft);
            Press(run, &Fixture, GameKey_MouseRight); // A second press of the same widget.
            PointAt(&Fixture, 0.3f, 0.5f);
            Tick(run, &Fixture);
            TEST_CHECK(run, UIScreen_IsPointerCaptured(Screen));
            A->OnMove = NULL;

            Release(run, &Fixture, GameKey_MouseRight);
            TEST_CHECK(run, UIScreen_IsPointerCaptured(Screen)); // Its other press still holds it.
            switch (Case)
            {
                case 0: Release(run, &Fixture, GameKey_MouseLeft); break;
                case 1: TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&A->Base, false)); break;
                case 2: TEST_CHECK_SUCCESS(run, Widget_SetIsInputEnabled(&A->Base, false)); break;
                case 3: TEST_CHECK_SUCCESS(run, UIScreen_RemoveWidget(Screen, &A->Base)); break;
                case 4: TEST_CHECK_SUCCESS(run, Widget_Destroy(&A->Base)); break;
                default: TEST_CHECK_SUCCESS(run, UIScreen_TransferPress(Screen, GameKey_MouseLeft, IdOf(B))); break;
            }
            TEST_CHECK(run, !UIScreen_IsPointerCaptured(Screen));
        }
        Routing_Deconstruct(run, &Fixture);
    }
}


/* The press of another widget ending says nothing about the capture. */
static void AnotherWidgetsPressLeavesTheCapture(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(Screen, IdOf(A)));
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        Press(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, (B->PressCount == 1) && UIScreen_IsPointerCaptured(Screen));
        Release(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK(run, (B->ReleaseCount == 1) && UIScreen_IsPointerCaptured(Screen));
        Press(run, &Fixture, GameKey_MouseRight);
        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&B->Base, false));
        TEST_CHECK(run, (B->LostCount == 1) && UIScreen_IsPointerCaptured(Screen));
        Release(run, &Fixture, GameKey_MouseRight);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(Screen));
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: hover keys.
/* The focused widget and its ancestors first; what they leave of the short list goes to the hovered widget and up its ancestors, marked. */
static void AHoverKeyGoesToTheFocusedThenTheHovered(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* FocusedParent = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Focused = AddProbe(run, &Fixture, IdOf(FocusedParent), 0.0f, 0.0f, 1.0f, 1.0f, true);
        RoutingProbe* HoveredParent = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Hovered = AddProbe(run, &Fixture, IdOf(HoveredParent), 0.0f, 0.0f, 1.0f, 1.0f, false);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, &Focused->Base));
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);

        // Handled by the focused one: the hovered one never hears.
        Focused->IsHandlingKeys = true;
        TestUI_Tap(run, &Fixture.Base, GameKey_Backspace);
        TEST_CHECK(run, (Focused->LastKey.Key == GameKey_Backspace) && !Focused->LastKey.IsHoverDelivery && (Hovered->KeyCount == 0));
        Focused->IsHandlingKeys = false;
        TEST_CHECK(run, Hovered->KeyCount == 0);

        // Left by them: it goes on, marked, up the hovered widget's ancestors, in the same tick.
        int Keys = Focused->KeyCount;
        TestUI_Press(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Focused->KeyCount == (Keys + 1)) && (FocusedParent->KeyCount == 1) && !FocusedParent->LastKey.IsHoverDelivery);
        TEST_CHECK(run, (Hovered->KeyCount == 1) && Hovered->LastKey.IsHoverDelivery && (Hovered->LastKey.Type == UIKeyboardInputType_KeyPress) && (Hovered->LastKey.Key == GameKey_Backspace));
        TEST_CHECK(run, HoveredParent->KeyCount == 1);
        TEST_CHECK(run, HoveredParent->LastKey.IsHoverDelivery);
        // Nobody took it: it is the game's.
        TEST_CHECK(run, !GameInput_IsKeyConsumed(&Fixture.Base.Input, GameKey_Backspace) && GameInput_IsKeyPressed(&Fixture.Base.Input, GameKey_Backspace));
        // The press alone: a repeat or the release does not go this way.
        TestUI_Repeat(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, Hovered->KeyCount == 1);
        TestUI_Release(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, Hovered->KeyCount == 1);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Handled by the hovered widget it is consumed and owned as usual: its repeats and release follow there, wherever the pointer goes. */
static void AHandledHoverKeyIsOwnedByItsHandler(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        A->IsHandlingKeys = true;
        PointAt(&Fixture, 0.25f, 0.5f);
        Tick(run, &Fixture);
        TestUI_Press(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->KeyCount == 1) && GameInput_IsKeyConsumed(&Fixture.Base.Input, GameKey_Backspace));
        TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Base.Input, GameKey_Backspace));

        PointAt(&Fixture, 0.75f, 0.5f);
        TestUI_Repeat(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->KeyCount == 2) && (A->LastKey.Type == UIKeyboardInputType_KeyRepeat) && !A->LastKey.IsHoverDelivery && (B->KeyCount == 0));
        TestUI_Release(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->KeyCount == 3) && (A->LastKey.Type == UIKeyboardInputType_KeyRelease) && (B->KeyCount == 0));
    }
    Routing_Deconstruct(run, &Fixture);
}

/* Only the three actions, with the modifiers they ask for; no typed text; nothing with no hovered widget or no keyboard. */
static void OnlyTheShortListGoesToTheHovered(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, false);
        A->IsHandlingKeys = true;
        PointAt(&Fixture, 0.5f, 0.5f);
        Tick(run, &Fixture);

        const GameKey NotOnTheList[] = { GameKey_A, GameKey_Enter, GameKey_Escape, GameKey_Tab, GameKey_Left, GameKey_Delete, GameKey_C, GameKey_V };
        for (size_t Index = 0U; Index < (sizeof(NotOnTheList) / sizeof(NotOnTheList[0])); Index++)
        {
            TestUI_Tap(run, &Fixture.Base, NotOnTheList[Index]);
            TEST_CHECK(run, A->KeyCount == 0);
        }
        Fixture.Base.Sample.TextCodepoints[0] = (uint32_t)'x';
        Fixture.Base.Sample.TextCodepointCount = 1U;
        Tick(run, &Fixture);
        TEST_CHECK(run, A->TextCount == 0);

        // With Ctrl held, C and V are copy and paste; a repeat of Backspace with no press never comes this way.
        Fixture.Base.Sample.KeyStates[GameKey_LeftControl] = GAME_INPUT_KEY_STATE_DOWN;
        TestUI_Tap(run, &Fixture.Base, GameKey_C);
        TEST_CHECK(run, (A->KeyCount == 2) && (A->LastKey.Key == GameKey_C) && (A->LastKey.Modifiers == GAME_KEY_MODIFIER_CONTROL));
        TestUI_Tap(run, &Fixture.Base, GameKey_V);
        TEST_CHECK(run, (A->KeyCount == 4) && (A->LastKey.Key == GameKey_V));
        Fixture.Base.Sample.KeyStates[GameKey_LeftControl] = 0U;

        // Backspace is the reset action; rebound, its old key is nothing here.
        int Keys = A->KeyCount;
        GameInputBinding Rebound = { .Key = GameKey_R, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Base.Input, GameAction_UIReset, &Rebound, 1U));
        TestUI_Tap(run, &Fixture.Base, GameKey_Backspace);
        TEST_CHECK(run, A->KeyCount == Keys);
        TestUI_Tap(run, &Fixture.Base, GameKey_R);
        TEST_CHECK(run, A->KeyCount == (Keys + 2));
        TEST_CHECK_SUCCESS(run, GameInput_ResetActionBindings(&Fixture.Base.Input, GameAction_UIReset));

        // No hovered widget: nowhere to go.
        Keys = A->KeyCount;
        PointAt(&Fixture, 2.0f, 0.5f);
        Tick(run, &Fixture);
        TestUI_Press(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->KeyCount == Keys) && !GameInput_IsKeyConsumed(&Fixture.Base.Input, GameKey_Backspace));
        TestUI_Release(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);

        // No keyboard: nothing goes anywhere.
        PointAt(&Fixture, 0.5f, 0.5f);
        Fixture.Base.ScreenInput.HasKeyboardFocus = false;
        Tick(run, &Fixture);
        TestUI_Tap(run, &Fixture.Base, GameKey_Backspace);
        TEST_CHECK(run, A->KeyCount == Keys);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A focused widget under the pointer hears the press twice: once for its focus, once for the pointer, and may tell them apart. */
static void TheFocusedWidgetHearsAHoverKeyOnceForEachReason(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, true);
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(&Fixture.Base.Screen, &A->Base));
        A->IsHandlingOnlyHoverKeys = true;
        PointAt(&Fixture, 0.5f, 0.5f);
        Tick(run, &Fixture);
        TestUI_Press(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->KeyCount == 2) && A->LastKey.IsHoverDelivery && GameInput_IsKeyConsumed(&Fixture.Base.Input, GameKey_Backspace));
        TestUI_Release(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* During a hidden-cursor drag hover stands still, but what has been hidden meanwhile takes no key: what holds it still may. */
static void AHiddenWidgetHearsNoHoverKeyDuringACapture(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* Holder = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Outer = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* Under = AddProbe(run, &Fixture, IdOf(Outer), 0.0f, 0.0f, 1.0f, 1.0f, false);
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&Under->Base));
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(Screen, IdOf(Holder)));
        Fixture.Base.ScreenInput.IsPointerRelative = true;
        Tick(run, &Fixture);

        TEST_CHECK_SUCCESS(run, Widget_SetIsRendered(&Under->Base, false));
        TestUI_Press(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, (Under->KeyCount == 0) && (Outer->KeyCount == 1) && Outer->LastKey.IsHoverDelivery);
        TestUI_Release(&Fixture.Base, GameKey_Backspace);
        Fixture.Base.ScreenInput.IsPointerRelative = false;
        Release(run, &Fixture, GameKey_MouseLeft);
    }
    Routing_Deconstruct(run, &Fixture);
}

/* A hover key nobody takes reaches navigation: Backspace bound to a navigation action still navigates, in that order. */
static void AHoverKeyComesBeforeNavigation(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 1.0f, 1.0f, true);
        GameInputBinding Bound = { .Key = GameKey_Backspace, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Base.Input, GameAction_UINext, &Bound, 1U));
        PointAt(&Fixture, 0.5f, 0.5f);
        Tick(run, &Fixture);

        TestUI_Press(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, (A->KeyCount == 1) && UIScreen_IsNavigating(Screen));
        TestUI_Release(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIScreen_InternalStopNavigation(Screen));
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, NULL));

        A->IsHandlingKeys = true;
        TestUI_Press(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
        TEST_CHECK(run, !UIScreen_IsNavigating(Screen));
        TestUI_Release(&Fixture.Base, GameKey_Backspace);
        Tick(run, &Fixture);
    }
    Routing_Deconstruct(run, &Fixture);
}


// Tests: cursor shapes.
/* The shape of the widget holding a press, wherever the pointer is; or else the hovered widget's; or else the arrow. */
static void TheScreenAnswersTheShapeOfThePressOrTheHover(TestRun* run)
{
    RoutingFixture Fixture;
    if (Routing_Construct(run, &Fixture))
    {
        UIScreen* Screen = &Fixture.Base.Screen;
        RoutingProbe* A = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.0f, 0.0f, 0.5f, 1.0f, false);
        RoutingProbe* B = AddProbe(run, &Fixture, WIDGET_ID_INVALID, 0.5f, 0.0f, 0.5f, 1.0f, false);
        TEST_CHECK(run, Widget_GetCursorShape(&A->Base) == UICursorShape_Arrow);
        TEST_CHECK_SUCCESS(run, Widget_SetCursorShape(&A->Base, UICursorShape_ResizeHorizontal));
        TEST_CHECK_SUCCESS(run, Widget_SetCursorShape(&B->Base, UICursorShape_IBeam));
        TEST_CHECK_ERROR(run, Widget_SetCursorShape(NULL, UICursorShape_IBeam), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, Widget_SetCursorShape(&A->Base, UICursorShape_Count), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, Widget_SetCursorShape(&A->Base, (UICursorShape)-1), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Widget_GetCursorShape(&A->Base) == UICursorShape_ResizeHorizontal);

        TEST_CHECK(run, UIScreen_GetCursorShape(Screen) == UICursorShape_Arrow);
        PointAt(&Fixture, 0.25f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetCursorShape(Screen) == UICursorShape_ResizeHorizontal);
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetCursorShape(Screen) == UICursorShape_IBeam);
        PointAt(&Fixture, 1.5f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetCursorShape(Screen) == UICursorShape_Arrow);

        // The press's widget, over another and off the surface; then the hovered one again.
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, Widget_IsHovered(&B->Base) && (UIScreen_GetCursorShape(Screen) == UICursorShape_ResizeHorizontal));
        PointAt(&Fixture, 1.5f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetCursorShape(Screen) == UICursorShape_ResizeHorizontal);
        PointAt(&Fixture, 0.75f, 0.5f);
        Release(run, &Fixture, GameKey_MouseLeft);
        TEST_CHECK(run, UIScreen_GetCursorShape(Screen) == UICursorShape_IBeam);

        // A press holding an arrow is an arrow, whatever is hovered.
        TEST_CHECK_SUCCESS(run, Widget_SetCursorShape(&A->Base, UICursorShape_Arrow));
        PointAt(&Fixture, 0.25f, 0.5f);
        Press(run, &Fixture, GameKey_MouseLeft);
        PointAt(&Fixture, 0.75f, 0.5f);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetCursorShape(Screen) == UICursorShape_Arrow);
        Release(run, &Fixture, GameKey_MouseLeft);
    }
    Routing_Deconstruct(run, &Fixture);
}


// Public functions.
void UIRoutingTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_held_press_follows_the_pointer_past_the_surface", AHeldPressFollowsThePointerPastTheSurface },
        { "an_input_that_does_not_track_gives_no_drags", AnInputThatDoesNotTrackGivesNoDrags },
        { "the_pointer_leaving_where_it_stands_tells_the_press", ThePointerLeavingWhereItStandsTellsThePress },
        { "the_windows_pointer_is_tracked_and_present_only_on_it", TheWindowsPointerIsTrackedAndPresentOnlyOnIt },
        { "another_press_over_nothing_during_a_drag_is_nobodys", AnotherPressOverNothingDuringADragIsNobodys },
        { "a_drag_below_ui_that_claimed_the_pointer_goes_on", ADragBelowUiThatClaimedThePointerGoesOn },
        { "the_arguments_tell_modifiers_and_deltas", TheArgumentsTellModifiersAndDeltas },
        { "two_held_buttons_each_hear_their_own_drag", TwoHeldButtonsEachHearTheirOwnDrag },
        { "clicks_count_over_time_and_distance", ClicksCountOverTimeAndDistance },
        { "the_click_distance_follows_the_scale", TheClickDistanceFollowsTheScale },
        { "a_release_goes_over_to_the_widget_under_the_pointer", AReleaseGoesOverToTheWidgetUnderThePointer },
        { "only_a_press_a_widget_took_goes_over_and_only_to_the_top", OnlyAPressAWidgetTookGoesOverAndOnlyToTheTop },
        { "a_release_over_sees_what_the_releases_hook_did", AReleaseOverSeesWhatTheReleasesHookDid },
        { "a_lost_press_is_told_at_once_and_only_once", ALostPressIsToldAtOnceAndOnlyOnce },
        { "only_the_presses_that_are_lost_are_told", OnlyThePressesThatAreLostAreTold },
        { "a_destroyed_widget_hears_nothing", ADestroyedWidgetHearsNothing },
        { "a_press_that_never_arrived_is_not_lost", APressThatNeverArrivedIsNotLost },
        { "hooks_may_take_a_press_away_or_destroy_their_widget", HooksMayTakeAPressAwayOrDestroyTheirWidget },
        { "a_press_can_be_handed_on", APressCanBeHandedOn },
        { "a_handed_press_is_checked", AHandedPressIsChecked },
        { "a_moved_widget_keeps_what_points_at_it", AMovedWidgetKeepsWhatPointsAtIt },
        { "a_move_is_checked", AMoveIsChecked },
        { "a_move_invalidates_what_depends_on_the_tree", AMoveInvalidatesWhatDependsOnTheTree },
        { "moved_under_something_hidden_it_loses_focus_and_press", MovedUnderSomethingHiddenItLosesFocusAndPress },
        { "a_move_keeps_the_entered_path_a_path", AMoveKeepsTheEnteredPathAPath },
        { "a_move_with_nothing_focused_cuts_the_path", AMoveWithNothingFocusedCutsThePath },
        { "a_move_and_a_transfer_in_one_tick_carry_the_drag_on", AMoveAndATransferInOneTickCarryTheDragOn },
        { "a_hook_may_move_its_own_widget", AHookMayMoveItsOwnWidget },
        { "the_wheel_goes_up_the_tree", TheWheelGoesUpTheTree },
        { "what_a_hook_took_away_hears_no_more_on_the_way", WhatAHookTookAwayHearsNoMoreOnTheWay },
        { "the_wheel_over_nothing_is_the_games", TheWheelOverNothingIsTheGames },
        { "roots_go_by_layer_then_z_then_insertion", RootsGoByLayerThenZThenInsertion },
        { "a_press_raises_only_within_its_layer", APressRaisesOnlyWithinItsLayer },
        { "raising_stays_within_the_layer", RaisingStaysWithinTheLayer },
        { "an_overlay_takes_no_input", AnOverlayTakesNoInput },
        { "navigation_goes_on_when_its_level_becomes_an_overlay", NavigationGoesOnWhenItsLevelBecomesAnOverlay },
        { "a_widget_moved_into_an_overlay_takes_no_path_with_it", AWidgetMovedIntoAnOverlayTakesNoPathWithIt },
        { "entering_a_root_raises_it_only_where_a_press_would", EnteringARootRaisesItOnlyWhereAPressWould },
        { "a_layer_belongs_to_a_root", ALayerBelongsToARoot },
        { "a_capture_is_legal_only_during_a_press", ACaptureIsLegalOnlyDuringAPress },
        { "a_captured_drag_freezes_the_pointer_and_carries_raw_motion", ACapturedDragFreezesThePointerAndCarriesRawMotion },
        { "a_capture_no_host_applies_stays_absolute", ACaptureNoHostAppliesStaysAbsolute },
        { "a_capture_ends_with_its_press", ACaptureEndsWithItsPress },
        { "another_widgets_press_leaves_the_capture", AnotherWidgetsPressLeavesTheCapture },
        { "raw_motion_along_one_axis_is_a_drag", RawMotionAlongOneAxisIsADrag },
        { "a_hook_that_ended_the_press_leaves_no_release", AHookThatEndedThePressLeavesNoRelease },
        { "a_release_never_seen_still_ends_the_press", AReleaseNeverSeenStillEndsThePress },
        { "the_hook_of_one_loss_may_destroy_the_other_holder", TheHookOfOneLossMayDestroyTheOtherHolder },
        { "the_pointer_is_claimed_over_a_widget_alone", ThePointerIsClaimedOverAWidgetAlone },
        { "a_widget_added_under_a_still_pointer_is_hovered", AWidgetAddedUnderAStillPointerIsHovered },
        { "a_mouse_event_needs_its_data", AMouseEventNeedsItsData },
        { "a_focus_hook_may_hand_the_press_on_before_it_arrives", AFocusHookMayHandThePressOnBeforeItArrives },
        { "a_row_of_clicks_stays_on_one_widget", ARowOfClicksStaysOnOneWidget },
        { "a_hover_key_goes_to_the_focused_then_the_hovered", AHoverKeyGoesToTheFocusedThenTheHovered },
        { "a_handled_hover_key_is_owned_by_its_handler", AHandledHoverKeyIsOwnedByItsHandler },
        { "only_the_short_list_goes_to_the_hovered", OnlyTheShortListGoesToTheHovered },
        { "the_focused_widget_hears_a_hover_key_once_for_each_reason", TheFocusedWidgetHearsAHoverKeyOnceForEachReason },
        { "a_hidden_widget_hears_no_hover_key_during_a_capture", AHiddenWidgetHearsNoHoverKeyDuringACapture },
        { "a_hover_key_comes_before_navigation", AHoverKeyComesBeforeNavigation },
        { "the_screen_answers_the_shape_of_the_press_or_the_hover", TheScreenAnswersTheShapeOfThePressOrTheHover },
    };
    TestRun_RunSuite(run, "UIRouting", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
