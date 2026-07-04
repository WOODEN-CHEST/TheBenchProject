#include <stdio.h>
#include "wr/WRCompile.h"
#include "wr/WRError.h"
#include "Config.h"
#include "Logger.h"
#include "ProgramTime.h"
#include "GameFrameManager.h"
#include "Renderer.h"
#include "raylib/raylib.h"


// Macros.
/** Path (relative to the working directory) of the game configuration file. */
#define CONFIG_FILE_PATH ((const unsigned char*)u8"config.json")
/** Window title shown in the title bar. */
#define WINDOW_TITLE ((const char*)u8"The Bench Project")
/** Frame rate cap applied when the config does not request an unlocked frame rate. */
#define DEFAULT_TARGET_FPS 240
/** Fixed update rate in hertz. Deliberately a code constant (not a config value) and higher than the
 *  render rate so simulation stays smooth and deterministic regardless of frame rate. */
#define UPDATE_RATE_HZ 480.0
/** Upper bound on a single frame's measured time, in seconds. Clamps the accumulator so a long stall
 *  (debugger break, window drag) cannot snowball into a burst of catch-up updates ("spiral of death"). */
#define MAX_FRAME_TIME_SECONDS 0.25
/** Aspect ratio (width / height) the game's content is designed and fitted for. */
#define TARGET_ASPECT_RATIO (16.0f / 9.0f)


// Static functions.
/* Creates the game window from the (already validated) config values. */
static void InitializeWindow(const GameConfig* config)
{
    if (config->IsFullscreen)
    {
        SetConfigFlags(FLAG_FULLSCREEN_MODE);
    }

    InitWindow((int)config->ResolutionWidth, (int)config->ResolutionHeight, WINDOW_TITLE);

    if (!config->IsFPSUnlocked)
    {
        SetTargetFPS(DEFAULT_TARGET_FPS);
    }
}

/* Runs the game loop: a fixed-timestep update accumulator plus a render each iteration, both delegated
 * to the frame manager. Returns when the window is closed or the manager reports a fatal error. */
static void RunGameLoop(GameFrameManager* frameManager)
{
    const double UpdateDelta = 1.0 / UPDATE_RATE_HZ;

    double PreviousTime = GetTime();
    double Accumulator = 0.0;
    double TotalUpdateTime = 0.0;
    bool HasFatalError = false;

    while (!WindowShouldClose() && !GameFrameManager_ShouldStop(frameManager) && !HasFatalError)
    {
        double CurrentTime = GetTime();
        double FrameTime = CurrentTime - PreviousTime;
        PreviousTime = CurrentTime;

        if (FrameTime > MAX_FRAME_TIME_SECONDS)
        {
            FrameTime = MAX_FRAME_TIME_SECONDS;
        }
        Accumulator += FrameTime;

        // Fixed-timestep updates: run as many whole steps as have accumulated.
        while (Accumulator >= UpdateDelta)
        {
            TotalUpdateTime += UpdateDelta;
            Error UpdateResult = GameFrameManager_Update(frameManager, ProgramTime_Create(TotalUpdateTime, UpdateDelta));
            if (UpdateResult.Code != ErrorCode_Success)
            {
                // The manager already logged the cause as CRITICAL; just stop gracefully.
                Error_Deconstruct(&UpdateResult);
                HasFatalError = true;
                break;
            }
            Accumulator -= UpdateDelta;
        }

        if (HasFatalError)
        {
            break;
        }

        // One render at the real, variable frame delta; the FPS cap is applied inside EndDrawing.
        Error RenderResult = GameFrameManager_Render(frameManager, ProgramTime_Create(CurrentTime, FrameTime));
        if (RenderResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&RenderResult);
            HasFatalError = true;
            break;
        }
    }
}


// Program entry.
int main(int argc, char** argv)
{
    UNUSED(argc);
    UNUSED(argv);

    // The logger is the very first thing initialized so every later step can log through it.
    Logger Log;
    Error LoggerResult = Logger_Construct(&Log);
    if (LoggerResult.Code != ErrorCode_Success)
    {
        // The logger itself is unavailable, so this is the one place we fall back to a raw stdout
        // print before aborting the whole program.
        fprintf(stdout, "Failed to initialize the logger: %s\n",
            (LoggerResult.Message != NULL) ? (const char*)LoggerResult.Message : "no details");
        Error_Deconstruct(&LoggerResult);
        return 1;
    }

    GameConfig Config;
    Error LoadResult = GameConfig_LoadFromFile(CONFIG_FILE_PATH, &Config);
    if (LoadResult.Code != ErrorCode_Success)
    {
        // The config is left at valid defaults on failure, so the game can still start.
        Error LogResult = Logger_LogWarningFormatted(&Log, (const unsigned char*)u8"Failed to load config \"%s\": %s",
            (const char*)CONFIG_FILE_PATH,
            (LoadResult.Message != NULL) ? (const char*)LoadResult.Message : "no details");
        Error_Deconstruct(&LogResult);
        Error_Deconstruct(&LoadResult);
    }

    Logger_LogInfo(&Log, u8"Loaded game config");

    InitializeWindow(&Config);

    Error ReadyResult = Logger_LogInfo(&Log, (const unsigned char*)u8"Window initialized.");
    Error_Deconstruct(&ReadyResult);

    // The frame manager owns the scenes and the per-frame update/render logic. The window (GL context)
    // exists by now, which is what its render targets need.
    GameFrameManager FrameManager;
    Error ManagerResult = GameFrameManager_Construct(&FrameManager, &Log, TARGET_ASPECT_RATIO,
        RenderTargetPosition_Centered());
    if (ManagerResult.Code != ErrorCode_Success)
    {
        Error LogResult = Logger_LogCriticalFormatted(&Log, (const unsigned char*)u8"Failed to create the frame manager: %s",
            (ManagerResult.Message != NULL) ? (const char*)ManagerResult.Message : "no details");
        Error_Deconstruct(&LogResult);
        Error_Deconstruct(&ManagerResult);
        CloseWindow();
        GameConfig_Deconstruct(&Config);
        Error CloseLog = Logger_Deconstruct(&Log);
        Error_Deconstruct(&CloseLog);
        return 1;
    }

    // TODO: Construct the AssetManager and add the initial game frame(s) here (e.g. the main menu or a
    // loading screen), passing shared services such as the AssetManager and Logger to their constructors.
    // With no frames added, the loop simply clears the screen each render.

    RunGameLoop(&FrameManager);

    // Tear the manager down before the window: it owns GPU render targets that need the GL context.
    Error ManagerCloseResult = GameFrameManager_Deconstruct(&FrameManager);
    if (ManagerCloseResult.Code != ErrorCode_Success)
    {
        Error LogResult = Logger_LogErrorFormatted(&Log, (const unsigned char*)u8"Error while tearing down the frame manager: %s",
            (ManagerCloseResult.Message != NULL) ? (const char*)ManagerCloseResult.Message : "no details");
        Error_Deconstruct(&LogResult);
        Error_Deconstruct(&ManagerCloseResult);
    }

    CloseWindow();
    GameConfig_Deconstruct(&Config);

    Error ShutdownResult = Logger_LogInfo(&Log, (const unsigned char*)u8"Shutting down.");
    Error_Deconstruct(&ShutdownResult);

    Error LoggerCloseResult = Logger_Deconstruct(&Log);
    Error_Deconstruct(&LoggerCloseResult);
    return 0;
}
