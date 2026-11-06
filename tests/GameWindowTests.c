#include "TestSuites.h"
#include "GameWindow.h"
#include "NamespacedKey.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <string.h>


// Macros.
/** Initial capacity (bytes) of path and text buffers; they grow as needed. */
#define WINDOW_TEST_BUFFER_CAPACITY ((size_t)256)
/** A tick of the game's update, in seconds. */
#define WINDOW_TEST_TICK (1.0 / 480.0)


// Types.
/* A game window that is never opened, with an input, in a folder of its own. */
typedef struct WindowFixtureStruct
{
    GenericBuffer Directory;
    GenericBuffer StatePath;
    KeyRegistry Keys;
    GameInput Input;
    GameInputSample Sample;
    GameConfig Config;
    Logger Log;
    bool IsLogOpen;
    double Time;
} WindowFixture;


// Static functions.
static bool Fixture_Construct(TestRun* run, WindowFixture* self, const char* name)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->Directory, WINDOW_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->StatePath, WINDOW_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GameConfig_SetDefaults(&self->Config);
    GameInputSample_Clear(&self->Sample);
    TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&self->Keys));
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&self->Input, &self->Keys, NULL, NULL));
    self->IsLogOpen = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, name, &self->Directory))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&self->Log, self->Directory._data));
    return self->IsLogOpen && TEST_CHECK_SUCCESS(run, Path_Append(self->Directory._data, u8"window_state.json", &self->StatePath));
}

static void Fixture_CloseLog(TestRun* run, WindowFixture* self)
{
    if (self->IsLogOpen)
    {
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&self->Log));
        self->IsLogOpen = false;
    }
}

static void Fixture_Deconstruct(TestRun* run, WindowFixture* self)
{
    Fixture_CloseLog(run, self);
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&self->Input));
    KeyRegistry_Deconstruct(&self->Keys);
    Memory_Free(self->StatePath._data);
    Memory_Free(self->Directory._data);
}

/* One tick of the manager's order: the input, then the window. @p key goes down this tick with the left Alt if asked. */
static void Tick(WindowFixture* self, GameWindow* window, GameKey key, bool isAltHeld)
{
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        uint8_t Old = self->Sample.KeyStates[Index];
        self->Sample.KeyStates[Index] = ((Old & GAME_INPUT_KEY_STATE_DOWN) != 0U) ? GAME_INPUT_KEY_STATE_RELEASED : 0U;
    }
    if (isAltHeld)
    {
        self->Sample.KeyStates[GameKey_LeftAlt] = GAME_INPUT_KEY_STATE_DOWN;
    }
    if (key != GameKey_Unknown)
    {
        self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
    }
    GameInput_BeginTick(&self->Input, &self->Sample);
    self->Time += WINDOW_TEST_TICK;
    GameWindow_Update(window, &self->Input, self->Time);
}

/* Lets the toggle's keys stay up until they count again. */
static void WaitOutTheHoldBack(WindowFixture* self, GameWindow* window)
{
    Tick(self, window, GameKey_Unknown, false);
    self->Time += GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS;
    Tick(self, window, GameKey_Unknown, false);
}

static bool StateFileHolds(TestRun* run, const WindowFixture* self, GameWindowMode mode)
{
    GameWindowState Saved;
    GameWindowState_SetDefaults(&Saved);
    return TEST_CHECK_SUCCESS(run, GameWindowState_LoadFile(self->StatePath._data, &Saved)) && (Saved.Mode == mode);
}

static bool StateFileExists(const WindowFixture* self)
{
    GameWindowState Saved;
    GameWindowState_SetDefaults(&Saved);
    Error Result = GameWindowState_LoadFile(self->StatePath._data, &Saved);
    bool IsThere = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    return IsThere;
}

/* Counts the occurrences of @p needle in the fixture's log, which must be closed. */
static size_t CountInLog(TestRun* run, const WindowFixture* self, const char* needle)
{
    size_t Count = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, WINDOW_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WINDOW_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(self->Directory._data, (const unsigned char*)u8"logs/latest.log", &Path))
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


// Tests.
// With no state file, the config says: its mode, and its resolution within the window's limits.
static void AFirstRunTakesTheConfig(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_first_run"))
    {
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        TEST_CHECK(run, !GameWindow_IsOpen(&Window) && (GameWindow_GetMode(&Window) == GameWindowMode_Windowed));
        TEST_CHECK(run, (GameWindow_GetState(&Window)->Width == 1280) && (GameWindow_GetState(&Window)->Height == 720));
        TEST_CHECK(run, !GameWindow_GetState(&Window)->HasPosition && !GameWindow_GetState(&Window)->IsMaximized);
        TEST_CHECK(run, !StateFileExists(&Fixture)); // Nothing written just by starting.
        TEST_CHECK(run, !GameWindow_ShouldClose(&Window)); // No window to close.
        GameWindow_Deconstruct(&Window);

        Fixture.Config.WindowMode = GameWindowMode_Fullscreen;
        Fixture.Config.ResolutionWidth = 320; // The config's lowest, under the window's.
        Fixture.Config.ResolutionHeight = 240;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Fullscreen);
        TEST_CHECK(run, (GameWindow_GetState(&Window)->Width == GAME_WINDOW_MIN_WIDTH)
            && (GameWindow_GetState(&Window)->Height == GAME_WINDOW_MIN_HEIGHT));
        GameWindow_Deconstruct(&Window);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// The state file wins over the config: it is what the player last had.
static void TheStateFileWinsOverTheConfig(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_state_wins"))
    {
        TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Fixture.Directory._data, "window_state.json", "{\"mode\": \"borderless\", \"width\": 900, "
            "\"height\": 700, \"has_position\": true, \"x\": 11, \"y\": 22, \"is_maximized\": true}"));
        Fixture.Config.WindowMode = GameWindowMode_Fullscreen;
        Fixture.Config.ResolutionWidth = 1600;
        Fixture.Config.ResolutionHeight = 1000;
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        const GameWindowState* State = GameWindow_GetState(&Window);
        TEST_CHECK(run, (State->Mode == GameWindowMode_Borderless) && (State->Width == 900) && (State->Height == 700));
        TEST_CHECK(run, State->HasPosition && (State->X == 11) && (State->Y == 22) && State->IsMaximized);
        GameWindow_Deconstruct(&Window);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A bad file is ignored with one log line, and the config stands. So is a file too long to read.
static void ABadStateFileIsIgnoredWithOneLine(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_bad_state"))
    {
        TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Fixture.Directory._data, "window_state.json", "{\"mode\": \"borderl"));
        Fixture.Config.ResolutionWidth = 1000;
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        TEST_CHECK(run, (GameWindow_GetMode(&Window) == GameWindowMode_Windowed) && (GameWindow_GetState(&Window)->Width == 1000));
        GameWindow_Deconstruct(&Window);
        Fixture_CloseLog(run, &Fixture);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Ignored the window state file") == 1U);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// With no folder to save in, or no logger, the window still works.
static void NoFolderAndNoLoggerAreFine(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_no_folder"))
    {
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, NULL, NULL);
        Tick(&Fixture, &Window, GameKey_F11, false);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        GameWindow_Close(&Window);
        GameWindow_Deconstruct(&Window);
        TEST_CHECK(run, !StateFileExists(&Fixture));
        GameWindow_Deconstruct(NULL);

        // With no folder there is nowhere to save, which is not a failure to save.
        GameWindow_Construct(&Window, &Fixture.Config, NULL, &Fixture.Log);
        GameWindow_SetMode(&Window, GameWindowMode_Fullscreen);
        GameWindow_Close(&Window);
        GameWindow_Deconstruct(&Window);
        Fixture_CloseLog(run, &Fixture);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Could not save") == 0U);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Alt+Enter and F11 by default: to the toggle's mode, and back. It is handled before the frames, which never see its keys.
static void TheToggleFiresAndIsConsumed(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_toggle"))
    {
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);

        // Plain Enter is the UI's, and Alt alone is nobody's.
        Tick(&Fixture, &Window, GameKey_Enter, false);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        TEST_CHECK(run, GameInput_IsActionPressed(&Fixture.Input, GameAction_UIAccept));
        Tick(&Fixture, &Window, GameKey_Unknown, true);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        Tick(&Fixture, &Window, GameKey_Unknown, false);
        Tick(&Fixture, &Window, GameKey_Unknown, false);

        // Alt+Enter: the toggle, not the UI's accept, and nothing is left of the key for the frames.
        Tick(&Fixture, &Window, GameKey_Enter, true);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        TEST_CHECK(run, StateFileHolds(run, &Fixture, GameWindowMode_Borderless));
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_UIAccept));
        TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_Enter));
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Enter));

        // F11 back.
        WaitOutTheHoldBack(&Fixture, &Window);
        Tick(&Fixture, &Window, GameKey_F11, false);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        TEST_CHECK(run, StateFileHolds(run, &Fixture, GameWindowMode_Windowed));
        TEST_CHECK(run, !GameInput_IsKeyPressed(&Fixture.Input, GameKey_F11));
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_F11));

        // F11 with a modifier held still toggles: the binding asks for none, and no other asks for one.
        WaitOutTheHoldBack(&Fixture, &Window);
        Fixture.Sample.KeyStates[GameKey_LeftShift] = GAME_INPUT_KEY_STATE_DOWN;
        Fixture.Sample.KeyStates[GameKey_F11] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        GameInput_BeginTick(&Fixture.Input, &Fixture.Sample);
        GameWindow_Update(&Window, &Fixture.Input, Fixture.Time);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        GameWindow_Deconstruct(&Window);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// One press is one switch: a held key does not switch again, and neither do its repeats.
static void AHeldKeySwitchesOnce(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_held"))
    {
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        Fixture.Sample.KeyStates[GameKey_F11] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        GameInput_BeginTick(&Fixture.Input, &Fixture.Sample);
        GameWindow_Update(&Window, &Fixture.Input, 1.0);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        for (int Index = 0; Index < 50; Index++)
        {
            Fixture.Sample.KeyStates[GameKey_F11] = GAME_INPUT_KEY_STATE_DOWN
                | (((Index % 10) == 9) ? GAME_INPUT_KEY_STATE_REPEATED : (uint8_t)0U);
            GameInput_BeginTick(&Fixture.Input, &Fixture.Sample);
            GameWindow_Update(&Window, &Fixture.Input, 1.0);
            TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
            TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_F11));
        }
        GameWindow_Deconstruct(&Window);
    }
    Fixture_Deconstruct(run, &Fixture);
}

/* One tick at a time of its own, with exact key states: what the window manager's switch makes of a held key. */
static void TickAt(WindowFixture* self, GameWindow* window, double time, GameKey key, uint8_t state)
{
    GameInputSample_Clear(&self->Sample);
    if (key != GameKey_Unknown)
    {
        self->Sample.KeyStates[key] = state;
    }
    GameInput_BeginTick(&self->Input, &self->Sample);
    self->Time = time;
    GameWindow_Update(window, &self->Input, time);
}

// A switch can take the focus away for a moment: every held key reads as up, and its next auto-repeat as a new press. A key held across a
// switch still switches once.
static void AKeyReportedUpWhileHeldSwitchesOnce(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_flush"))
    {
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        const uint8_t Pressed = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;

        TickAt(&Fixture, &Window, 1.0, GameKey_F11, Pressed);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        // Reported up within a few ticks, while it is still held.
        TickAt(&Fixture, &Window, 1.015, GameKey_F11, GAME_INPUT_KEY_STATE_RELEASED);
        TickAt(&Fixture, &Window, 1.1, GameKey_Unknown, 0U);
        // The key's repeat, the key's delay later: a new press to everybody but the window.
        TickAt(&Fixture, &Window, 1.66, GameKey_F11, Pressed);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_F11) && !GameInput_IsKeyPressed(&Fixture.Input, GameKey_F11));
        // Down as long as it repeats, and the second starts only when it stops.
        TickAt(&Fixture, &Window, 3.0, GameKey_F11, GAME_INPUT_KEY_STATE_DOWN);
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_F11));
        TickAt(&Fixture, &Window, 3.5, GameKey_F11, GAME_INPUT_KEY_STATE_RELEASED);
        TickAt(&Fixture, &Window, 3.5 + GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS - 0.01, GameKey_F11, Pressed);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless); // Pressed again too soon: taken, and the wait starts over.
        TickAt(&Fixture, &Window, 3.6 + GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS, GameKey_F11, GAME_INPUT_KEY_STATE_RELEASED);
        TickAt(&Fixture, &Window, 3.6 + (2.0 * GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS) - 0.11, GameKey_F11, Pressed);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        TickAt(&Fixture, &Window, 8.0, GameKey_F11, GAME_INPUT_KEY_STATE_RELEASED);
        TickAt(&Fixture, &Window, 8.0 + GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS, GameKey_Unknown, 0U);

        // After that, a press is a press.
        TickAt(&Fixture, &Window, 8.0 + GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS + 0.01, GameKey_F11, Pressed);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        GameWindow_Deconstruct(&Window);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Alt+Enter across a switch: Alt reads as up and never comes back, so Enter's repeat is a plain Enter, which must not reach the UI.
static void AnEnterHeldAcrossASwitchDoesNotReachTheUI(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_flush_enter"))
    {
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        const uint8_t Pressed = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
        GameInputSample_Clear(&Fixture.Sample);
        Fixture.Sample.KeyStates[GameKey_LeftAlt] = GAME_INPUT_KEY_STATE_DOWN;
        Fixture.Sample.KeyStates[GameKey_Enter] = Pressed;
        GameInput_BeginTick(&Fixture.Input, &Fixture.Sample);
        GameWindow_Update(&Window, &Fixture.Input, 1.0);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        TickAt(&Fixture, &Window, 1.015, GameKey_Enter, GAME_INPUT_KEY_STATE_RELEASED);
        TickAt(&Fixture, &Window, 1.66, GameKey_Enter, Pressed);
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_UIAccept) && !GameInput_IsKeyPressed(&Fixture.Input, GameKey_Enter));
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        TickAt(&Fixture, &Window, 2.0, GameKey_Enter, GAME_INPUT_KEY_STATE_DOWN);
        TEST_CHECK(run, !GameInput_IsActionDown(&Fixture.Input, GameAction_UIAccept));

        // Keys that did not fire it are not held back, and other keys go on as ever.
        TickAt(&Fixture, &Window, 2.01, GameKey_F11, Pressed);
        TEST_CHECK(run, GameInput_IsKeyConsumed(&Fixture.Input, GameKey_F11)); // The toggle's other key: taken, nothing more.
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        TickAt(&Fixture, &Window, 2.02, GameKey_W, Pressed);
        TEST_CHECK(run, !GameInput_IsKeyConsumed(&Fixture.Input, GameKey_W) && GameInput_IsActionPressed(&Fixture.Input, GameAction_MoveForward));

        // Once it has been up long enough, Enter is the UI's again.
        TickAt(&Fixture, &Window, 2.03, GameKey_Enter, GAME_INPUT_KEY_STATE_RELEASED);
        TickAt(&Fixture, &Window, 2.03 + GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS, GameKey_Unknown, 0U);
        TickAt(&Fixture, &Window, 2.04 + GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS, GameKey_Enter, Pressed);
        TEST_CHECK(run, GameInput_IsActionPressed(&Fixture.Input, GameAction_UIAccept) && !GameInput_IsKeyConsumed(&Fixture.Input, GameKey_Enter));
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        GameWindow_Deconstruct(&Window);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// The config chooses what the toggle goes to; out of any whole-screen mode it goes back to a window.
static void TheConfigChoosesTheTogglesMode(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_toggle_mode"))
    {
        Fixture.Config.FullscreenToggleMode = GameWindowMode_Fullscreen;
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        Tick(&Fixture, &Window, GameKey_F11, false);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Fullscreen);
        WaitOutTheHoldBack(&Fixture, &Window);
        Tick(&Fixture, &Window, GameKey_F11, false);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        GameWindow_Deconstruct(&Window);

        // Started in the other whole-screen mode: still back to a window.
        Fixture.Config.WindowMode = GameWindowMode_Borderless;
        TEST_CHECK_SUCCESS(run, FileSystem_DeleteEntry(Fixture.StatePath._data));
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        WaitOutTheHoldBack(&Fixture, &Window);
        Tick(&Fixture, &Window, GameKey_F11, false);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        GameWindow_Deconstruct(&Window);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// The toggle is an action like any other: the player's binding replaces the keys.
static void TheToggleCanBeRebound(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_rebound"))
    {
        GameInputBinding Rebound = { .Key = GameKey_F9, .Modifiers = GAME_KEY_MODIFIER_NONE };
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_ToggleFullscreen, &Rebound, 1U));
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        Tick(&Fixture, &Window, GameKey_F11, false);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        TEST_CHECK(run, GameInput_IsKeyPressed(&Fixture.Input, GameKey_F11)); // Left for the frames.
        Tick(&Fixture, &Window, GameKey_Enter, true);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        Tick(&Fixture, &Window, GameKey_Unknown, false);
        Tick(&Fixture, &Window, GameKey_F9, false);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        GameWindow_Deconstruct(&Window);

        // And it can be left with no binding: nothing then switches, and the keys are the frames'.
        TEST_CHECK_SUCCESS(run, GameInput_SetBindings(&Fixture.Input, GameAction_ToggleFullscreen, NULL, 0U));
        TEST_CHECK(run, GameInput_GetBindingCount(&Fixture.Input, GameAction_ToggleFullscreen) == 0U);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// Saved when the mode changes and at the end, and only then.
static void SavedOnlyWhenTheModeChangesAndAtTheEnd(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_saving"))
    {
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        for (int Index = 0; Index < 100; Index++)
        {
            Tick(&Fixture, &Window, GameKey_Unknown, false);
        }
        TEST_CHECK(run, !StateFileExists(&Fixture)); // Never per tick.

        // Asking for the mode it has changes nothing, and writes nothing.
        GameWindow_SetMode(&Window, GameWindowMode_Windowed);
        TEST_CHECK(run, !StateFileExists(&Fixture));

        GameWindow_SetMode(&Window, GameWindowMode_Fullscreen);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Fullscreen);
        TEST_CHECK(run, StateFileHolds(run, &Fixture, GameWindowMode_Fullscreen));

        // Straight from one whole-screen mode to the other.
        GameWindow_SetMode(&Window, GameWindowMode_Borderless);
        TEST_CHECK(run, StateFileHolds(run, &Fixture, GameWindowMode_Borderless));

        TEST_CHECK_SUCCESS(run, FileSystem_DeleteEntry(Fixture.StatePath._data));
        GameWindow_Close(&Window);
        TEST_CHECK(run, StateFileHolds(run, &Fixture, GameWindowMode_Borderless));
        GameWindow_Deconstruct(&Window);

        // The next start comes back as it was left.
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
        GameWindow_Deconstruct(&Window);

        // Each change is one line, and the first run logged none.
        Fixture_CloseLog(run, &Fixture);
        TEST_CHECK(run, CountInLog(run, &Fixture, "The window is fullscreen now.") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "The window is borderless now.") == 1U);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Ignored the window state file") == 0U);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// A save the file system refuses is one log line, and the mode still changes.
static void AFailedSaveIsLogged(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_failed_save"))
    {
        GenericBuffer Missing;
        GenericBuffer_AllocateVariable(&Missing, WINDOW_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
        if (TEST_CHECK_SUCCESS(run, Path_Append(Fixture.Directory._data, u8"no_such_folder", &Missing)))
        {
            GameWindow Window;
            GameWindow_Construct(&Window, &Fixture.Config, Missing._data, &Fixture.Log);
            GameWindow_SetMode(&Window, GameWindowMode_Borderless);
            TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Borderless);
            GameWindow_Deconstruct(&Window);
        }
        Memory_Free(Missing._data);
        Fixture_CloseLog(run, &Fixture);
        TEST_CHECK(run, CountInLog(run, &Fixture, "Could not save the window state file") == 1U);
    }
    Fixture_Deconstruct(run, &Fixture);
}

// With no input to read there is nothing to toggle.
static void AnUpdateWithNoInputIsFine(TestRun* run)
{
    WindowFixture Fixture;
    if (Fixture_Construct(run, &Fixture, "window_no_input"))
    {
        GameWindow Window;
        GameWindow_Construct(&Window, &Fixture.Config, Fixture.Directory._data, &Fixture.Log);
        GameWindow_Update(&Window, NULL, 1.0);
        TEST_CHECK(run, GameWindow_GetMode(&Window) == GameWindowMode_Windowed);
        GameWindow_Deconstruct(&Window);
    }
    Fixture_Deconstruct(run, &Fixture);
}


// Public functions.
void GameWindowTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_first_run_takes_the_config", AFirstRunTakesTheConfig },
        { "the_state_file_wins_over_the_config", TheStateFileWinsOverTheConfig },
        { "a_bad_state_file_is_ignored_with_one_line", ABadStateFileIsIgnoredWithOneLine },
        { "no_folder_and_no_logger_are_fine", NoFolderAndNoLoggerAreFine },
        { "the_toggle_fires_and_is_consumed", TheToggleFiresAndIsConsumed },
        { "a_held_key_switches_once", AHeldKeySwitchesOnce },
        { "a_key_reported_up_while_held_switches_once", AKeyReportedUpWhileHeldSwitchesOnce },
        { "an_enter_held_across_a_switch_does_not_reach_the_ui", AnEnterHeldAcrossASwitchDoesNotReachTheUI },
        { "the_config_chooses_the_toggles_mode", TheConfigChoosesTheTogglesMode },
        { "the_toggle_can_be_rebound", TheToggleCanBeRebound },
        { "saved_only_when_the_mode_changes_and_at_the_end", SavedOnlyWhenTheModeChangesAndAtTheEnd },
        { "a_failed_save_is_logged", AFailedSaveIsLogged },
        { "an_update_with_no_input_is_fine", AnUpdateWithNoInputIsFine },
    };
    TestRun_RunSuite(run, "GameWindow", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
