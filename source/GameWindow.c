#include "GameWindow.h"
#include "raylib/raylib.h"
#include "wr/WRPath.h"


// Macros.
/** The state file's name, next to the executable. */
#define STATE_FILE_NAME ((const unsigned char*)u8"window_state.json")
/** The most monitors looked at when fitting the window; past this many a window is simply placed on one of these. */
#define MAX_MONITORS 16


// Static functions.
static void LogSavedStateProblem(GameWindow* self, const char* format, Error* problem)
{
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger, (const unsigned char*)format,
            (problem->Message != NULL) ? (const char*)problem->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(problem);
}

/* The state file over what the config says. No file is a first run; a bad one is ignored as a whole. */
static void LoadState(GameWindow* self)
{
    if (self->_statePath._count == 0U)
    {
        return;
    }
    Error Result = GameWindowState_LoadFile(self->_statePath._data, &self->_state);
    if ((Result.Code == ErrorCode_FileNotFound) || (Result.Code == ErrorCode_DirectoryNotFound))
    {
        Error_Deconstruct(&Result);
        return;
    }
    if (Result.Code != ErrorCode_Success)
    {
        LogSavedStateProblem(self, "Ignored the window state file: %s", &Result);
    }
}

static void SaveState(GameWindow* self)
{
    if (self->_statePath._count == 0U)
    {
        return;
    }
    Error Result = GameWindowState_SaveFile(&self->_state, self->_statePath._data);
    if (Result.Code != ErrorCode_Success)
    {
        LogSavedStateProblem(self, "Could not save the window state file: %s", &Result);
    }
}

/* The monitors as raylib has them, the primary one first. Needs GLFW up, so only once the window is created. */
static size_t ReadMonitors(GameWindowMonitor* outMonitors)
{
    int Count = GetMonitorCount();
    size_t Read = 0U;
    for (int Index = 0; (Index < Count) && (Read < (size_t)MAX_MONITORS); Index++)
    {
        Vector2 Position = GetMonitorPosition(Index);
        outMonitors[Read] = (GameWindowMonitor){ .X = (int32_t)Position.x, .Y = (int32_t)Position.y,
            .Width = (int32_t)GetMonitorWidth(Index), .Height = (int32_t)GetMonitorHeight(Index) };
        Read++;
    }
    return Read;
}

/* Puts a window that is not on show where the state says. It was created at the minimum size: what a file asks for could be more than a GPU
 * can allocate, and the monitors are unknown until a window exists. */
static void PlaceAsRemembered(GameWindow* self)
{
    GameWindowMonitor Monitors[MAX_MONITORS];
    size_t MonitorCount = ReadMonitors(Monitors);
    GameWindowState_FitToMonitors(&self->_state, Monitors, MonitorCount);
    SetWindowSize((int)self->_state.Width, (int)self->_state.Height);
    if (self->_state.HasPosition)
    {
        SetWindowPosition((int)self->_state.X, (int)self->_state.Y);
    }
    else if (MonitorCount > 0U)
    {
        int32_t X = 0;
        int32_t Y = 0;
        GameWindowState_GetCentredPosition(&self->_state, &Monitors[0], &X, &Y);
        SetWindowPosition((int)X, (int)Y);
    }
}

/* From a window to a mode that takes the whole monitor. raylib's two keep the window's size and place to come back to. Neither changes the
 * monitor's size, and borderless keeps its refresh rate too; fullscreen asks for none, so GLFW takes the highest the monitor has at that size. */
static void EnterWholeScreen(GameWindowMode mode)
{
    switch (mode)
    {
        case GameWindowMode_Windowed:
            break;
        case GameWindowMode_Borderless:
            ToggleBorderlessWindowed();
            break;
        case GameWindowMode_Fullscreen:
            ToggleFullscreen();
            break;
    }
}

/* raylib's own toggles go both ways. */
static void LeaveWholeScreen(GameWindowMode mode)
{
    EnterWholeScreen(mode);
}

/* Back in a window: the remembered size and place, then maximized if it was. raylib's own record is not enough: it holds the
 * maximized size when the window was maximized, and a window manager's memory of where to go back to is set by this order. */
static void RestoreWindowedPlace(const GameWindow* self)
{
    SetWindowSize((int)self->_state.Width, (int)self->_state.Height);
    if (self->_state.HasPosition)
    {
        SetWindowPosition((int)self->_state.X, (int)self->_state.Y);
    }
    if (self->_state.IsMaximized)
    {
        MaximizeWindow();
    }
}

static GameWindowObservation Observe(void)
{
    Vector2 Position = GetWindowPosition();
    return (GameWindowObservation)
    {
        .Width = (int32_t)GetScreenWidth(),
        .Height = (int32_t)GetScreenHeight(),
        .X = (int32_t)Position.x,
        .Y = (int32_t)Position.y,
        .IsMaximized = IsWindowMaximized(),
        .IsMinimized = IsWindowMinimized()
    };
}

/* The toggle, once a tick: the keys that fired it are held back until they have stayed up for a while, and consumed all that time. A switch can
 * take the focus away for a moment, which reports them up while they are still held (a modifier stays lost until it is pressed again), and the
 * next auto-repeat arrives as a new press: unheld, that would switch again, or reach the UI as a plain Enter. */
static void HandleToggle(GameWindow* self, GameInput* input, double timeSeconds)
{
    bool IsAnyKeyDown = false;
    for (size_t Index = 0U; Index < self->_toggleKeyCount; Index++)
    {
        GameKey Key = self->_toggleKeys[Index];
        if ((GameInput_GetRawKeyState(input, Key) & (GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED)) != 0U)
        {
            IsAnyKeyDown = true;
            GameInput_ConsumeKey(input, Key);
        }
    }
    if (IsAnyKeyDown)
    {
        self->_toggleKeysHeldBackUntil = timeSeconds + GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS;
    }
    else if (timeSeconds >= self->_toggleKeysHeldBackUntil)
    {
        self->_toggleKeyCount = 0U;
    }
    if (!GameInput_IsActionPressed(input, GameAction_ToggleFullscreen))
    {
        return;
    }

    // The action's other key, while one is held back, is taken and does nothing.
    bool IsHeldBack = self->_toggleKeyCount > 0U;
    if (!IsHeldBack)
    {
        self->_toggleKeyCount = GameInput_GetActionKeys(input, GameAction_ToggleFullscreen, self->_toggleKeys);
        self->_toggleKeysHeldBackUntil = timeSeconds + GAME_WINDOW_TOGGLE_HOLD_BACK_SECONDS;
    }
    GameInput_ConsumeAction(input, GameAction_ToggleFullscreen);
    if (!IsHeldBack)
    {
        GameWindow_SetMode(self, GameWindowMode_GetToggleTarget(self->_state.Mode, self->_toggleMode));
    }
}

/* A last look before the state is saved or the window leaves its windowed self. */
static void TakeInWindowed(GameWindow* self)
{
    if (!self->_isOpen || (self->_state.Mode != GameWindowMode_Windowed))
    {
        return;
    }
    GameWindowObservation Seen = Observe();
    GameWindowWatch_Observe(&self->_watch, &self->_state, &Seen, self->_time);
    GameWindowWatch_Settle(&self->_watch, &self->_state);
}


// Public functions.
void GameWindow_Construct(GameWindow* self, const GameConfig* config, const unsigned char* baseDirectory, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_logger = logger;
    self->_isVSyncEnabled = config->IsVSyncEnabled;
    self->_toggleMode = config->FullscreenToggleMode;
    GameWindowWatch_Construct(&self->_watch);
    GameWindowState_SetDefaults(&self->_state);
    self->_state.Mode = config->WindowMode;
    self->_state.Width = config->ResolutionWidth;
    self->_state.Height = config->ResolutionHeight;
    GameWindowState_ClampSize(&self->_state);

    GenericBuffer_AllocateVariable(&self->_statePath, 0U, sizeof(unsigned char));
    if (baseDirectory != NULL)
    {
        Error PathResult = Path_Append(baseDirectory, STATE_FILE_NAME, &self->_statePath);
        if (PathResult.Code != ErrorCode_Success)
        {
            GenericBuffer_Clear(&self->_statePath);
            LogSavedStateProblem(self, "Could not build the window state file's path: %s", &PathResult);
        }
    }
    LoadState(self);
}

void GameWindow_Deconstruct(GameWindow* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Free(self->_statePath._data);
    Memory_Zero(self, sizeof(*self));
}

void GameWindow_Open(GameWindow* self, const char* title)
{
    // Hidden until it is where it belongs: the monitors are unknown before the window exists, and a remembered mode goes through
    // the remembered place, so it opens on that monitor.
    unsigned int Flags = FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIDDEN;
    if (self->_isVSyncEnabled)
    {
        Flags |= FLAG_VSYNC_HINT;
    }
    SetConfigFlags(Flags);

    InitWindow((int)GAME_WINDOW_MIN_WIDTH, (int)GAME_WINDOW_MIN_HEIGHT, title);
    if (!IsWindowReady())
    {
        return;
    }
    self->_isOpen = true;
    // Deliberately no SetTargetFPS: the game loop paces rendering itself. raylib is built with SUPPORT_CUSTOM_FRAME_CONTROL (see
    // BUILDING.md), so EndDrawing neither swaps buffers nor polls input.
    SetWindowMinSize((int)GAME_WINDOW_MIN_WIDTH, (int)GAME_WINDOW_MIN_HEIGHT);
    PlaceAsRemembered(self);
    if (self->_state.Mode == GameWindowMode_Windowed)
    {
        if (self->_state.IsMaximized)
        {
            MaximizeWindow();
        }
    }
    else
    {
        EnterWholeScreen(self->_state.Mode);
    }
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    // GLFW leaves a window that was hidden when it took a monitor on show but not focused, on Windows: it would get no keys.
    SetWindowFocused();
    GameWindowWatch_Hold(&self->_watch, self->_time);
}

void GameWindow_Close(GameWindow* self)
{
    TakeInWindowed(self);
    SaveState(self);
    if (self->_isOpen)
    {
        CloseWindow();
        self->_isOpen = false;
    }
}

void GameWindow_Update(GameWindow* self, GameInput* input, double timeSeconds)
{
    self->_time = timeSeconds;
    if (self->_isOpen && WindowShouldClose())
    {
        self->_isCloseRequested = true; // raylib clears its answer at the next poll, which may come before the game loop looks.
    }
    if (input != NULL)
    {
        HandleToggle(self, input, timeSeconds);
    }
    if (self->_isOpen && (self->_state.Mode == GameWindowMode_Windowed))
    {
        GameWindowObservation Seen = Observe();
        GameWindowWatch_Observe(&self->_watch, &self->_state, &Seen, timeSeconds);
    }
}

void GameWindow_SetMode(GameWindow* self, GameWindowMode mode)
{
    GameWindowMode Previous = self->_state.Mode;
    if (mode == Previous)
    {
        return;
    }
    TakeInWindowed(self);
    if (self->_isOpen)
    {
        // Not maximized under the whole screen: what comes back is set here, the same on every window manager.
        if ((Previous == GameWindowMode_Windowed) && self->_state.IsMaximized)
        {
            RestoreWindow();
        }
        LeaveWholeScreen(Previous);
        EnterWholeScreen(mode);
        if (mode == GameWindowMode_Windowed)
        {
            RestoreWindowedPlace(self);
        }
    }
    self->_state.Mode = mode;
    GameWindowWatch_Hold(&self->_watch, self->_time);
    SaveState(self);
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"The window is %s now.",
            (const char*)GameWindowMode_GetName(mode));
        Error_Deconstruct(&LogResult);
    }
}
