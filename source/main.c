#include <stdio.h>
#include "wr/WRCompile.h"
#include "wr/WRError.h"
#include "Config.h"
#include "Logger.h"
#include "raylib/raylib.h"


// Macros.
/** Path (relative to the working directory) of the game configuration file. */
#define CONFIG_FILE_PATH ((const unsigned char*)u8"config.json")
/** Window title shown in the title bar. */
#define WINDOW_TITLE ((const char*)u8"The Bench Project")
/** Frame rate cap applied when the config does not request an unlocked frame rate. */
#define DEFAULT_TARGET_FPS 240


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

    // TODO: Replace this placeholder window loop with the real game loop.
    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(BLACK);
        EndDrawing();
    }

    CloseWindow();
    GameConfig_Deconstruct(&Config);

    Error ShutdownResult = Logger_LogInfo(&Log, (const unsigned char*)u8"Shutting down.");
    Error_Deconstruct(&ShutdownResult);

    Error LoggerCloseResult = Logger_Deconstruct(&Log);
    Error_Deconstruct(&LoggerCloseResult);
    return 0;
}
