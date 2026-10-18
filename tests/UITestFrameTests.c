#include "TestSuites.h"
#include "UITestFrame.h"
#include "GameFrame.h"
#include "GameFrameManager.h"
#include "GameInput.h"
#include "Logger.h"
#include "Services.h"
#include "ProgramTime.h"


// Macros.
/** Initial capacity (bytes) of the work directory path; it grows as needed. */
#define FRAME_TEST_PATH_CAPACITY ((size_t)256)
/** The fixed tick the test advances by, in seconds. */
#define FRAME_TEST_TICK_SECONDS (1.0 / 480.0)


// Types.
/* The UI test frame with what it needs: services, an input and a frame manager (for its target-area settings). Without
 * an asset manager the labels use raylib's built-in font, which has no glyphs without a window: the text lays out
 * empty, everything else runs. */
typedef struct FrameFixtureStruct
{
    Logger Log;
    GameInput Input;
    GameInputSample Sample;
    GameFrameManager Manager;
    Services Shared;
    GameFrame* Frame;
    double Time;
    bool IsLoggerConstructed;
    bool IsManagerConstructed;
} FrameFixture;


// Static functions.
static bool FrameFixture_Construct(TestRun* run, FrameFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer Base;
    GenericBuffer_AllocateVariable(&Base, FRAME_TEST_PATH_CAPACITY, sizeof(unsigned char));
    self->IsLoggerConstructed = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "frame", &Base))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&self->Log, Base._data));
    Memory_Free(Base._data);
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&self->Input));
    self->IsManagerConstructed = self->IsLoggerConstructed && TEST_CHECK_SUCCESS(run,
        GameFrameManager_Construct(&self->Manager, &self->Log, &self->Input, 16.0f / 9.0f, RenderTargetPosition_Centered()));
    GameInputSample_Clear(&self->Sample);
    self->Sample.WindowSize = (Vector2){ .x = 1280.0f, .y = 720.0f };
    self->Sample.IsCursorCaptured = true; // As while the world test's mouse look runs: keyboard only.

    Services_Construct(&self->Shared);
    self->Shared.Input = &self->Input;
    self->Shared.FrameManager = &self->Manager;
    return self->IsManagerConstructed && TEST_CHECK_SUCCESS(run, UITestFrame_Create(&self->Shared, &self->Frame))
        && TEST_CHECK_SUCCESS(run, GameFrame_Start(self->Frame, ProgramTime_Create(0.0, 0.0)));
}

static void FrameFixture_Deconstruct(TestRun* run, FrameFixture* self)
{
    if (self->Frame != NULL)
    {
        TEST_CHECK_SUCCESS(run, GameFrame_End(self->Frame, ProgramTime_Create(self->Time, 0.0)));
        GameFrame_Destroy(self->Frame);
    }
    if (self->IsManagerConstructed)
    {
        TEST_CHECK_SUCCESS(run, GameFrameManager_Deconstruct(&self->Manager));
    }
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&self->Input));
    if (self->IsLoggerConstructed)
    {
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&self->Log));
    }
}

/* One update with @p key pressed this tick (GameKey_Unknown for none), as the frame manager would run it. */
static void TickWithKey(TestRun* run, FrameFixture* self, GameKey key)
{
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        self->Sample.KeyStates[Index] = 0U;
    }
    if (key != GameKey_Unknown)
    {
        self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    }
    GameInput_BeginTick(&self->Input, &self->Sample);
    self->Time += FRAME_TEST_TICK_SECONDS;
    TEST_CHECK_SUCCESS(run, GameFrame_Update(self->Frame, ProgramTime_Create(self->Time, FRAME_TEST_TICK_SECONDS)));
}


// Tests.
// The frame's menu takes the navigation keys while it uses them and leaves the rest to the world below.
static void TheUITestFrameSharesInputWithTheWorld(TestRun* run)
{
    FrameFixture Fixture;
    if (FrameFixture_Construct(run, &Fixture))
    {
        GameInput* Input = &Fixture.Input;
        TickWithKey(run, &Fixture, GameKey_W);
        TEST_CHECK(run, GameInput_IsActionDown(Input, GameAction_MoveForward)); // Not a UI key: left to the world.

        TickWithKey(run, &Fixture, GameKey_Tab); // Starts navigation on the menu.
        TEST_CHECK(run, GameInput_IsKeyConsumed(Input, GameKey_Tab));
        TickWithKey(run, &Fixture, GameKey_Enter); // Opens the menu: focus on the first item.
        TEST_CHECK(run, GameInput_IsKeyConsumed(Input, GameKey_Enter));
        TickWithKey(run, &Fixture, GameKey_Delete); // Removes the focused item.
        TEST_CHECK(run, GameInput_IsKeyConsumed(Input, GameKey_Delete));
        TickWithKey(run, &Fixture, GameKey_Unknown); // Frees it.
        TickWithKey(run, &Fixture, GameKey_Delete); // Focus went with it: nothing to remove now.
        TEST_CHECK(run, !GameInput_IsKeyConsumed(Input, GameKey_Delete));

        TickWithKey(run, &Fixture, GameKey_R); // Rebuilds the menu (destroying the old one).
        TEST_CHECK(run, GameInput_IsKeyConsumed(Input, GameKey_R));
        TickWithKey(run, &Fixture, GameKey_Escape); // Navigation ends and the UI lets go.
        TEST_CHECK(run, GameInput_IsKeyConsumed(Input, GameKey_Escape));
        TickWithKey(run, &Fixture, GameKey_Escape); // Now Escape reaches the world (the world test quits on it).
        TEST_CHECK(run, GameInput_IsActionPressed(Input, GameAction_Pause));
    }
    FrameFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void UITestFrameTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_ui_test_frame_shares_input_with_the_world", TheUITestFrameSharesInputWithTheWorld },
    };
    TestRun_RunSuite(run, "UITestFrame", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
