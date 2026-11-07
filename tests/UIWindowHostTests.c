#include "TestSuites.h"
#include "GameInput.h"
#include "NamespacedKey.h"
#include "Renderer.h"
#include "TextComponentFactory.h"
#include "UIInternal.h"
#include "UIKit.h"
#include "UIWidgetFactory.h"
#include "UIWindowHost.h"
#include "wr/WRMemory.h"
#include <math.h>


// Macros.
/** A tick of the game's update, in seconds. */
#define HOST_TEST_TICK (1.0 / 480.0)


// Types.
/* A screen with a stretched panel and a button of fixed UI pixels at its corner, an input and a host. */
typedef struct HostFixtureStruct
{
    KeyRegistry Keys;
    GameInput Input;
    GameInputSample Sample;
    TextComponentFactory Text;
    UIWidgetFactory Factory;
    UIKit Kit;
    UIScreen Screen;
    UIWindowHost Host;
    uint64_t PanelId;
    uint64_t ButtonId;
    double Time;
} HostFixture;


// Static functions.
static bool Fixture_Construct(TestRun* run, HostFixture* self, UIScalePolicy policy, float playerScale)
{
    Memory_Zero(self, sizeof(*self));
    GameInputSample_Clear(&self->Sample);
    TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&self->Keys));
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&self->Input, &self->Keys, NULL, NULL));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&self->Text));
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_Construct(&self->Factory));
    TEST_CHECK_SUCCESS(run, UIKit_Construct(&self->Kit, &self->Factory, &self->Text, NULL, NULL));
    TEST_CHECK_SUCCESS(run, UIScreen_Construct(&self->Screen, &self->Factory, NULL));
    UIWindowHost_Construct(&self->Host, policy, playerScale, 16.0f / 9.0f, RenderTargetPosition_Centered());

    UIPanelOptions PanelOptions = UIPanelOptions_Create();
    PanelOptions.Element.HasPlacement = true;
    PanelOptions.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
    UIButtonOptions ButtonOptions = UIButtonOptions_Create(u8"Fixed");
    ButtonOptions.Element.HasPlacement = true;
    ButtonOptions.Element.Placement = UIPlacement_FromTopLeft(10.0f, 20.0f, 100.0f, 24.0f);
    return TEST_CHECK_SUCCESS(run, UIKit_CreatePanel(&self->Kit, &self->Screen, WIDGET_ID_INVALID, &PanelOptions, &self->PanelId))
        && TEST_CHECK_SUCCESS(run, UIKit_CreateButton(&self->Kit, &self->Screen, WIDGET_ID_INVALID, &ButtonOptions, &self->ButtonId));
}

static void Fixture_Deconstruct(TestRun* run, HostFixture* self)
{
    UIWindowHost_Deconstruct(&self->Host);
    TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&self->Screen));
    TEST_CHECK_SUCCESS(run, UIKit_Deconstruct(&self->Kit));
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_Deconstruct(&self->Factory));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&self->Text));
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&self->Input));
    KeyRegistry_Deconstruct(&self->Keys);
}

static bool Tick(TestRun* run, HostFixture* self, float windowWidth, float windowHeight, float monitorHeight)
{
    self->Sample.WindowSize = (Vector2){ .x = windowWidth, .y = windowHeight };
    self->Sample.MonitorSize = (Vector2){ .x = monitorHeight * 2.0f, .y = monitorHeight };
    GameInput_BeginTick(&self->Input, &self->Sample);
    self->Time += HOST_TEST_TICK;
    return TEST_CHECK_SUCCESS(run, UIWindowHost_Update(&self->Host, &self->Screen, ProgramTime_Create(self->Time, HOST_TEST_TICK), &self->Input));
}

/* The button's box in surface pixels. */
static Rectangle GetButtonPixels(HostFixture* self)
{
    Widget* Button = NULL;
    Rectangle Box = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (UIScreen_TryGetWidgetById(&self->Screen, self->ButtonId, &Button))
    {
        Vector2 Position;
        Vector2 Size;
        UIScreen_InternalGetAbsoluteBox(Button, &Position, &Size);
        UIViewport Viewport = UIScreen_GetViewport(&self->Screen);
        Box = (Rectangle){ .x = Position.x * Viewport.SizePixels.x, .y = Position.y * Viewport.SizePixels.y,
            .width = Size.x * Viewport.SizePixels.x, .height = Size.y * Viewport.SizePixels.y };
    }
    return Box;
}

static bool IsClose(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

/* A widget over the whole surface that keeps the raw motion a drag brought it. */
typedef struct MotionProbeStruct
{
    Widget Base;
    Vector2 LastDelta;
    int MoveCount;
} MotionProbe;

static Error MotionProbe_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    MotionProbe* Probe = self;
    if (args->Type == UIMouseInputType_Move)
    {
        Probe->LastDelta = args->DeltaPixels;
        Probe->MoveCount++;
    }
    return Error_CreateSuccess();
}

static const WidgetVTable MotionProbeVTable = { .OnMouseInput = MotionProbe_OnMouseInput };

/* What a host asked of the cursor, in place of the real one. */
typedef struct CursorRecordStruct
{
    int CaptureCount;
    int ReleaseCount;
    int ShapeCount;
    Vector2 ReleasedAt;
    UICursorShape Shape;
    GameInput* Input;
} CursorRecord;

static void RecordCapture(void* context, GameInput* input)
{
    CursorRecord* Record = context;
    Record->CaptureCount++;
    Record->Input = input;
}

static void RecordRelease(void* context, GameInput* input, Vector2 position)
{
    CursorRecord* Record = context;
    Record->ReleaseCount++;
    Record->ReleasedAt = position;
    Record->Input = input;
}

static void RecordShape(void* context, UICursorShape shape)
{
    CursorRecord* Record = context;
    Record->ShapeCount++;
    Record->Shape = shape;
}

/* Replaces the fixture's cursor with one that records. */
static void RecordCursor(HostFixture* self, CursorRecord* record)
{
    Memory_Zero(record, sizeof(*record));
    UIWindowHostCursor Cursor = { .Capture = RecordCapture, .Release = RecordRelease, .SetShape = RecordShape, .Context = record };
    UIWindowHost_SetCursor(&self->Host, &Cursor);
}

/* One tick of a 1000 x 500 window, its pointer at a place in window pixels; then this tick's presses become holds and its releases end. */
static void PointerTick(TestRun* run, HostFixture* self, float x, float y)
{
    self->Sample.MousePosition = (Vector2){ .x = x, .y = y };
    Tick(run, self, 1000.0f, 500.0f, 1080.0f);
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        uint8_t State = self->Sample.KeyStates[Index];
        self->Sample.KeyStates[Index] = ((State & GAME_INPUT_KEY_STATE_RELEASED) != 0) ? 0U : (uint8_t)(State & GAME_INPUT_KEY_STATE_DOWN);
    }
}

/* The screen's panel stretches over the whole window: what the pointer lands on at (10, 10) is it. The button stands at (10, 20). */
static Widget* GetWidget(HostFixture* self, uint64_t id)
{
    Widget* Found = NULL;
    UIScreen_TryGetWidgetById(&self->Screen, id, &Found);
    return Found;
}


// Tests.
// The viewport is the window's, tick after tick, with the fitted area's settings the host was given.
static void TheViewportFollowsTheWindow(TestRun* run)
{
    HostFixture Fixture;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_ConstantPixels(), 1.0f))
    {
        const Vector2 Sizes[] = { { 1920.0f, 1080.0f }, { 1024.0f, 768.0f }, { 2560.0f, 1080.0f }, { 800.0f, 1000.0f }, { 640.0f, 360.0f } };
        for (size_t Index = 0U; Index < (sizeof(Sizes) / sizeof(Sizes[0])); Index++)
        {
            Tick(run, &Fixture, Sizes[Index].x, Sizes[Index].y, 1080.0f);
            UIViewport Viewport = UIScreen_GetViewport(&Fixture.Screen);
            TEST_CHECK(run, (Viewport.SizePixels.x == Sizes[Index].x) && (Viewport.SizePixels.y == Sizes[Index].y));
            TEST_CHECK(run, (Viewport.TargetAspectRatio == (16.0f / 9.0f)) && (Viewport.TargetRelativePosition.x == 0.5f));
        }
        // A window of no size leaves it as it was.
        Tick(run, &Fixture, 0.0f, 0.0f, 1080.0f);
        TEST_CHECK(run, UIScreen_GetViewport(&Fixture.Screen).SizePixels.x == 640.0f);

        // What fitted units mean is the host's to say.
        UIWindowHost_Construct(&Fixture.Host, UIScalePolicy_ConstantPixels(), 1.0f, 4.0f / 3.0f, (Vector2){ .x = 0.25f, .y = 0.75f });
        Tick(run, &Fixture, 1000.0f, 700.0f, 1080.0f);
        UIViewport Viewport = UIScreen_GetViewport(&Fixture.Screen);
        TEST_CHECK(run, (Viewport.TargetAspectRatio == (4.0f / 3.0f)) && (Viewport.TargetRelativePosition.x == 0.25f)
            && (Viewport.TargetRelativePosition.y == 0.75f));
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Constant pixels: the monitor sets the scale, and the window's size changes nothing about a control.
static void ConstantPixelsKeepAControlsSize(TestRun* run)
{
    HostFixture Fixture;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_ConstantPixels(), 1.0f))
    {
        const Vector2 Sizes[] = { { 1920.0f, 1080.0f }, { 1280.0f, 720.0f }, { 1024.0f, 768.0f }, { 2560.0f, 1080.0f }, { 800.0f, 1000.0f }, { 640.0f, 360.0f } };
        for (size_t Index = 0U; Index < (sizeof(Sizes) / sizeof(Sizes[0])); Index++)
        {
            // A tick to take the new size, and a tick for layout to run with it.
            Tick(run, &Fixture, Sizes[Index].x, Sizes[Index].y, 1080.0f);
            Tick(run, &Fixture, Sizes[Index].x, Sizes[Index].y, 1080.0f);
            TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.0f);
            Rectangle Box = GetButtonPixels(&Fixture);
            TEST_CHECK(run, IsClose(Box.x, 10.0f) && IsClose(Box.y, 20.0f) && IsClose(Box.width, 100.0f) && IsClose(Box.height, 24.0f));
        }

        // Another monitor: every control is as many times larger, whatever the window's size.
        Tick(run, &Fixture, 1000.0f, 700.0f, 2160.0f);
        Tick(run, &Fixture, 1000.0f, 700.0f, 2160.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 2.0f);
        Rectangle Box = GetButtonPixels(&Fixture);
        TEST_CHECK(run, IsClose(Box.x, 20.0f) && IsClose(Box.y, 40.0f) && IsClose(Box.width, 200.0f) && IsClose(Box.height, 48.0f));
        Tick(run, &Fixture, 1000.0f, 700.0f, 1440.0f);
        Tick(run, &Fixture, 1000.0f, 700.0f, 1440.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.25f);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Follow the height: the window's height sets the scale, and the monitor means nothing.
static void FollowHeightScalesWithTheWindow(TestRun* run)
{
    HostFixture Fixture;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_FollowHeight(), 1.0f))
    {
        Tick(run, &Fixture, 1920.0f, 1080.0f, 2160.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.0f);
        Tick(run, &Fixture, 3840.0f, 2160.0f, 1080.0f);
        Tick(run, &Fixture, 3840.0f, 2160.0f, 1080.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 2.0f);
        Rectangle Box = GetButtonPixels(&Fixture);
        TEST_CHECK(run, IsClose(Box.x, 20.0f) && IsClose(Box.width, 200.0f) && IsClose(Box.height, 48.0f));
        Tick(run, &Fixture, 640.0f, 360.0f, 1080.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 0.5f); // A third, kept to the floor.
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Fixed stays, and the player's scale and the policy change from the next update on.
static void ThePolicyAndThePlayersScaleCanChange(TestRun* run)
{
    HostFixture Fixture;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_Fixed(1.5f), 3.0f))
    {
        TEST_CHECK(run, UIWindowHost_GetPolicy(&Fixture.Host).Kind == UIScalePolicyKind_Fixed);
        TEST_CHECK(run, UIWindowHost_GetPlayerScale(&Fixture.Host) == 3.0f);
        Tick(run, &Fixture, 1000.0f, 2160.0f, 2160.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.5f);

        UIWindowHost_SetPolicy(&Fixture.Host, UIScalePolicy_ConstantPixels());
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.5f); // Not until the next update.
        Tick(run, &Fixture, 1000.0f, 2160.0f, 2160.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 6.0f);

        UIWindowHost_SetPlayerScale(&Fixture.Host, 1.5f);
        TEST_CHECK(run, UIWindowHost_GetPlayerScale(&Fixture.Host) == 1.5f);
        Tick(run, &Fixture, 1000.0f, 2160.0f, 2160.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 3.0f);

        UIWindowHost_SetPolicy(&Fixture.Host, UIScalePolicy_FollowHeight());
        TEST_CHECK(run, UIWindowHost_GetPolicy(&Fixture.Host).Kind == UIScalePolicyKind_FollowHeight);
        Tick(run, &Fixture, 1000.0f, 720.0f, 2160.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.0f);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// An unknown monitor, as before the first asking, gives the player's scale alone; so does no input.
static void WithNothingToGoByThePlayersScaleStands(TestRun* run)
{
    HostFixture Fixture;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_ConstantPixels(), 1.5f))
    {
        Tick(run, &Fixture, 1000.0f, 700.0f, 0.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.5f);
        Tick(run, &Fixture, 1000.0f, 700.0f, 2160.0f);
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 3.0f);
        Fixture.Time += HOST_TEST_TICK;
        TEST_CHECK_SUCCESS(run, UIWindowHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(Fixture.Time, HOST_TEST_TICK), NULL));
        TEST_CHECK(run, UIScreen_GetScale(&Fixture.Screen) == 1.5f);
        TEST_CHECK(run, UIScreen_GetViewport(&Fixture.Screen).SizePixels.x == 1000.0f); // The viewport stays.
    }
    Fixture_Deconstruct(run, &Fixture);
}

// The pointer is the window's at any size, and the UI reads first: what it took the game does not see.
static void ThePointerAndTheKeysAreTheWindows(TestRun* run)
{
    HostFixture Fixture;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_ConstantPixels(), 1.0f))
    {
        Fixture.Sample.MousePosition = (Vector2){ .x = 50.0f, .y = 30.0f };
        Tick(run, &Fixture, 800.0f, 1000.0f, 1080.0f);
        Tick(run, &Fixture, 800.0f, 1000.0f, 1080.0f);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Fixture.ButtonId);
        TEST_CHECK(run, GameInput_IsPointerConsumed(&Fixture.Input));

        // The same pixels hit it in a window of another shape.
        Tick(run, &Fixture, 2560.0f, 600.0f, 1080.0f);
        Tick(run, &Fixture, 2560.0f, 600.0f, 1080.0f);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Fixture.ButtonId);

        // The press reaches it through the host and is consumed; a key nothing wants stays for the game.
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        Fixture.Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        Tick(run, &Fixture, 2560.0f, 600.0f, 1080.0f);
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_MouseLeft));
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Fixture.ButtonId);
        TEST_CHECK(run, GameInput_IsActionPressed(&Fixture.Input, GameAction_MoveForward));
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Public functions.
// The host's pointer comes from the window: a drag held past the window's edge goes on, and the press hears where it is.
static void TheHostsPointerFollowsAPressPastTheWindow(TestRun* run)
{
    HostFixture Fixture;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_Fixed(1.0f), 1.0f))
    {
        PointerTick(run, &Fixture, 50.0f, 30.0f); // Placed.
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, Widget_IsHovered(GetWidget(&Fixture, Fixture.ButtonId)));
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, UIScreen_HasHeldPress(&Fixture.Screen));
        PointerTick(run, &Fixture, -400.0f, 2000.0f);
        TEST_CHECK(run, !UIScreen_IsPointerPresent(&Fixture.Screen) && IsClose(UIScreen_GetPointerPosition(&Fixture.Screen).x, -0.4f)
            && IsClose(UIScreen_GetPointerPosition(&Fixture.Screen).y, 4.0f));
        TEST_CHECK(run, !Widget_IsHovered(GetWidget(&Fixture, Fixture.ButtonId)) && UIScreen_HasHeldPress(&Fixture.Screen));
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_RELEASED;
        PointerTick(run, &Fixture, -400.0f, 2000.0f);
        TEST_CHECK(run, !UIScreen_HasHeldPress(&Fixture.Screen));

        // A stale place just inside the edge, the cursor having left: nothing hovers.
        Fixture.Sample.IsCursorOnWindow = false;
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, !UIScreen_IsPointerPresent(&Fixture.Screen) && !Widget_IsHovered(GetWidget(&Fixture, Fixture.ButtonId)));
        Fixture.Sample.IsCursorOnWindow = true;
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, Widget_IsHovered(GetWidget(&Fixture, Fixture.ButtonId)));
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A hidden-cursor drag: asked for in one tick, applied after it; raw motion from the next on, the position standing still; and the cursor
// put back where it began when the press ends, once.
static void TheHostAppliesAHiddenCursorDrag(TestRun* run)
{
    HostFixture Fixture;
    CursorRecord Record;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_Fixed(1.0f), 1.0f))
    {
        RecordCursor(&Fixture, &Record);
        PointerTick(run, &Fixture, 50.0f, 30.0f); // Placed.
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, Record.CaptureCount == 0);
        PointerTick(run, &Fixture, 52.0f, 30.0f);
        TEST_CHECK(run, Record.CaptureCount == 0);

        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(&Fixture.Screen, Fixture.ButtonId));
        PointerTick(run, &Fixture, 54.0f, 30.0f);
        TEST_CHECK(run, (Record.CaptureCount == 1) && (Record.Input == &Fixture.Input) && (Record.ReleaseCount == 0));

        // As the real cursor would be: hidden, at the window's centre, with raw motion. The screen does not see any of that move.
        Fixture.Sample.IsCursorCaptured = true;
        Fixture.Sample.MouseDelta = (Vector2){ .x = 11.0f, .y = 3.0f };
        PointerTick(run, &Fixture, 500.0f, 250.0f);
        TEST_CHECK(run, IsClose(UIScreen_GetPointerPosition(&Fixture.Screen).x, 0.054f) && UIScreen_IsPointerPresent(&Fixture.Screen));
        TEST_CHECK(run, Widget_IsHovered(GetWidget(&Fixture, Fixture.ButtonId)));
        TEST_CHECK(run, GameInput_IsPointerConsumed(&Fixture.Input));
        Fixture.Sample.MouseDelta = (Vector2){ .x = 0.0f, .y = 0.0f };
        PointerTick(run, &Fixture, 500.0f, 250.0f);
        TEST_CHECK(run, (Record.CaptureCount == 1) && (Record.ReleaseCount == 0)); // Once, not per tick.

        // The press ends: back where it began.
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_RELEASED;
        PointerTick(run, &Fixture, 500.0f, 250.0f);
        TEST_CHECK(run, (Record.ReleaseCount == 1) && (Record.ReleasedAt.x == 54.0f) && (Record.ReleasedAt.y == 30.0f));
        TEST_CHECK(run, !UIScreen_IsPointerCaptured(&Fixture.Screen));
        Fixture.Sample.IsCursorCaptured = false;
        PointerTick(run, &Fixture, 54.0f, 30.0f);
        TEST_CHECK(run, (Record.ReleaseCount == 1) && (Record.CaptureCount == 1));
        TEST_CHECK(run, UIScreen_IsPointerPresent(&Fixture.Screen) && Widget_IsHovered(GetWidget(&Fixture, Fixture.ButtonId)));
    }
    Fixture_Deconstruct(run, &Fixture);
}

// The hidden cursor's raw motion is what the widget hears, past the position's reach; with the capture asked for and no input yet, nothing happens to the cursor.
static void TheHostHandsOnTheRawMotion(TestRun* run)
{
    HostFixture Fixture;
    CursorRecord Record;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_Fixed(1.0f), 1.0f))
    {
        RecordCursor(&Fixture, &Record);
        WidgetTypeDescriptor Descriptor = { .Name = u8"MotionProbe", .StructSize = sizeof(MotionProbe), .VTable = &MotionProbeVTable };
        uint64_t ProbeType = 0U;
        Widget* Created = NULL;
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&Fixture.Factory, &Descriptor, &ProbeType));
        TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.Factory, &Fixture.Screen, ProbeType, NULL, &Created));
        TEST_CHECK_SUCCESS(run, Widget_SetSize(Created, (Vector2){ .x = 1.0f, .y = 1.0f }));
        TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&Fixture.Screen, Created));
        MotionProbe* Probe = (MotionProbe*)Created;

        PointerTick(run, &Fixture, 500.0f, 250.0f); // Placed.
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        PointerTick(run, &Fixture, 500.0f, 250.0f);
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_DOWN;

        // Asked for between two ticks, with the next one having no input to take the cursor from: it waits.
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(&Fixture.Screen, Widget_GetId(Created)));
        TEST_CHECK_SUCCESS(run, UIWindowHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(Fixture.Time, HOST_TEST_TICK), NULL));
        TEST_CHECK(run, Record.CaptureCount == 0);

        PointerTick(run, &Fixture, 500.0f, 250.0f);
        TEST_CHECK(run, Record.CaptureCount == 1);
        Fixture.Sample.IsCursorCaptured = true;
        Fixture.Sample.MouseDelta = (Vector2){ .x = 11.0f, .y = 3.0f };
        int MovesBefore = Probe->MoveCount;
        PointerTick(run, &Fixture, 500.0f, 250.0f);
        TEST_CHECK(run, (Probe->MoveCount == (MovesBefore + 1)) && (Probe->LastDelta.x == 11.0f) && (Probe->LastDelta.y == 3.0f));
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A drag asked for and ended within a tick asks nothing of the cursor. The host going away with one running gives the cursor back.
static void TheHostGivesTheCursorBack(TestRun* run)
{
    HostFixture Fixture;
    CursorRecord Record;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_Fixed(1.0f), 1.0f))
    {
        RecordCursor(&Fixture, &Record);
        PointerTick(run, &Fixture, 50.0f, 30.0f); // Placed.
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(&Fixture.Screen, Fixture.ButtonId));
        UIScreen_ReleasePointerCapture(&Fixture.Screen);
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, (Record.CaptureCount == 0) && (Record.ReleaseCount == 0));

        TEST_CHECK_SUCCESS(run, UIScreen_CapturePointer(&Fixture.Screen, Fixture.ButtonId));
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, Record.CaptureCount == 1);

        // A tick with no input changes nothing about it.
        TEST_CHECK_SUCCESS(run, UIWindowHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(Fixture.Time, HOST_TEST_TICK), NULL));
        TEST_CHECK(run, (Record.CaptureCount == 1) && (Record.ReleaseCount == 0));

        // Going away with it running.
        UIWindowHost_Deconstruct(&Fixture.Host);
        TEST_CHECK(run, (Record.ReleaseCount == 1) && (Record.ReleasedAt.x == 50.0f));
        UIWindowHost_Deconstruct(&Fixture.Host);
        TEST_CHECK(run, Record.ReleaseCount == 1);
    }
    Fixture_Deconstruct(run, &Fixture);
    UIWindowHost_Deconstruct(NULL);
}

// The pointer's shape: applied when it changes, not per tick, and the arrow again when the host goes.
static void TheHostAppliesThePointersShape(TestRun* run)
{
    HostFixture Fixture;
    CursorRecord Record;
    if (Fixture_Construct(run, &Fixture, UIScalePolicy_Fixed(1.0f), 1.0f))
    {
        RecordCursor(&Fixture, &Record);
        TEST_CHECK_SUCCESS(run, Widget_SetCursorShape(GetWidget(&Fixture, Fixture.ButtonId), UICursorShape_Hand));
        PointerTick(run, &Fixture, 500.0f, 250.0f); // Placed.
        PointerTick(run, &Fixture, 500.0f, 250.0f);
        TEST_CHECK(run, Record.ShapeCount == 0); // The arrow it starts with.
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, (Record.ShapeCount == 1) && (Record.Shape == UICursorShape_Hand));
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        PointerTick(run, &Fixture, 51.0f, 31.0f);
        TEST_CHECK(run, Record.ShapeCount == 1);
        PointerTick(run, &Fixture, 500.0f, 250.0f);
        TEST_CHECK(run, (Record.ShapeCount == 2) && (Record.Shape == UICursorShape_Arrow));
        PointerTick(run, &Fixture, 50.0f, 30.0f);
        TEST_CHECK(run, (Record.ShapeCount == 3) && (Record.Shape == UICursorShape_Hand));
        UIWindowHost_Deconstruct(&Fixture.Host);
        TEST_CHECK(run, (Record.ShapeCount == 4) && (Record.Shape == UICursorShape_Arrow));
        UIWindowHost_Deconstruct(&Fixture.Host);
        TEST_CHECK(run, Record.ShapeCount == 4);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// The standard cursor, with no window: every call does nothing, so a test or a windowless harness that reaches it is safe.
static void TheStandardCursorIsSafeWithNoWindow(TestRun* run)
{
    UIWindowHostCursor Cursor = UIWindowHostCursor_CreateStandard();
    TEST_CHECK(run, (Cursor.Capture != NULL) && (Cursor.Release != NULL) && (Cursor.SetShape != NULL));
    Cursor.Capture(Cursor.Context, NULL);
    Cursor.Release(Cursor.Context, NULL, (Vector2){ .x = 1.0f, .y = 1.0f });
    for (int32_t Shape = 0; Shape <= (int32_t)UICursorShape_Count; Shape++)
    {
        Cursor.SetShape(Cursor.Context, (UICursorShape)Shape);
    }
}

void UIWindowHostTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_viewport_follows_the_window", TheViewportFollowsTheWindow },
        { "constant_pixels_keep_a_controls_size", ConstantPixelsKeepAControlsSize },
        { "follow_height_scales_with_the_window", FollowHeightScalesWithTheWindow },
        { "the_policy_and_the_players_scale_can_change", ThePolicyAndThePlayersScaleCanChange },
        { "with_nothing_to_go_by_the_players_scale_stands", WithNothingToGoByThePlayersScaleStands },
        { "the_pointer_and_the_keys_are_the_windows", ThePointerAndTheKeysAreTheWindows },
        { "the_hosts_pointer_follows_a_press_past_the_window", TheHostsPointerFollowsAPressPastTheWindow },
        { "the_host_applies_a_hidden_cursor_drag", TheHostAppliesAHiddenCursorDrag },
        { "the_host_hands_on_the_raw_motion", TheHostHandsOnTheRawMotion },
        { "the_host_gives_the_cursor_back", TheHostGivesTheCursorBack },
        { "the_host_applies_the_pointers_shape", TheHostAppliesThePointersShape },
        { "the_standard_cursor_is_safe_with_no_window", TheStandardCursorIsSafeWithNoWindow },
    };
    TestRun_RunSuite(run, "UIWindowHost", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
