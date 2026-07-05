#include <stdio.h>
#include "wr/WRCompile.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRUnicode.h"
#include "wr/WRUnicodeLoader.h"
#include "wr/WRGHDF.h"
#include "wr/WRJSON.h"
#include "Config.h"
#include "Logger.h"
#include "ProgramTime.h"
#include "GameFrameManager.h"
#include "Renderer.h"
#include "AssetManager.h"
#include "Services.h"
#include "WorldTestFrame.h"
#include "raylib/raylib.h"


// Macros.
/** File name (resolved against the working directory) of the game configuration file. */
#define CONFIG_FILE_NAME ((const unsigned char*)u8"config.json")
/** Relative path (under the working directory) of the root asset directory. */
#define ASSET_ROOT_RELATIVE ((const unsigned char*)u8"asset")
/** Relative path (under the working directory) of the Unicode database. */
#define UNICODE_DATA_RELATIVE ((const unsigned char*)u8"asset/text/unicode_data.txt")
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
/* Logs a formatted warning through the logger and releases both the log-call and the source errors. */
static void LogWarningAndRelease(Logger* logger, const unsigned char* format, Error* source)
{
    Error LogResult = Logger_LogWarningFormatted(logger, format,
        (source->Message != NULL) ? (const char*)source->Message : "no details");
    Error_Deconstruct(&LogResult);
    Error_Deconstruct(source);
}

/* Builds an absolute path by appending @p relative to @p base into a freshly allocated byte buffer.
 * The caller must free outBuffer->_data regardless of the result. */
static Error BuildAbsolutePath(const unsigned char* base, const unsigned char* relative, GenericBuffer* outBuffer)
{
    GenericBuffer_AllocateVariable(outBuffer, 64U, sizeof(unsigned char));
    return Path_Append(base, relative, outBuffer);
}

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

/* Loads the game config from the absolute config path, warning (and keeping defaults) on failure. */
static void LoadConfig(Logger* logger, const unsigned char* workingDirectory, GameConfig* outConfig)
{
    GameConfig_SetDefaults(outConfig);

    GenericBuffer ConfigPath;
    Error PathResult = BuildAbsolutePath(workingDirectory, CONFIG_FILE_NAME, &ConfigPath);
    if (PathResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to build config path: %s", &PathResult);
    }
    else
    {
        Error LoadResult = GameConfig_LoadFromFile(ConfigPath._data, outConfig);
        if (LoadResult.Code != ErrorCode_Success)
        {
            LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to load config: %s", &LoadResult);
        }
    }
    Memory_Free(ConfigPath._data);
}

/* Loads the Unicode database from disk, falling back to an empty (valid) database on failure. */
static void LoadUnicodeData(Logger* logger, const unsigned char* workingDirectory, UnicodeData* outData)
{
    GenericBuffer UnicodePath;
    Error PathResult = BuildAbsolutePath(workingDirectory, UNICODE_DATA_RELATIVE, &UnicodePath);
    if (PathResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to build unicode data path: %s", &PathResult);
        Memory_Free(UnicodePath._data);
        Error EmptyResult = UnicodeData_CreateEmpty(outData);
        Error_Deconstruct(&EmptyResult);
        return;
    }

    Error LoadResult = UnicodeData_Load(UnicodePath._data, outData);
    Memory_Free(UnicodePath._data);
    if (LoadResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to load unicode data: %s", &LoadResult);
        Error EmptyResult = UnicodeData_CreateEmpty(outData);
        Error_Deconstruct(&EmptyResult);
    }
}

/* Sets up the asset manager: standard types, the absolute asset search root, and reads definitions.
 * Returns success with the out-params set, or a failure (with both left NULL) if the manager or its
 * standard types could not be created. A failed definition read is warned about, not fatal. */
static Error SetUpAssetManager(Logger* logger, const unsigned char* workingDirectory,
    AssetManager** outManager, StandardAssetTypes* outTypes, JSONObjectPool** outPool)
{
    *outManager = NULL;
    *outPool = NULL;

    AssetManager* Manager = NULL;
    Error ConstructResult = AssetManager_Construct1(&Manager);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        return ConstructResult;
    }

    Error TypesResult = AssetManager_CreateStandardAssetTypes(Manager, outTypes, outPool);
    if (TypesResult.Code != ErrorCode_Success)
    {
        Error DeconstructResult = AssetManager_Deconstruct(Manager);
        Error_Deconstruct(&DeconstructResult);
        return TypesResult;
    }

    GenericBuffer AssetRoot;
    Error PathResult = BuildAbsolutePath(workingDirectory, ASSET_ROOT_RELATIVE, &AssetRoot);
    if (PathResult.Code == ErrorCode_Success)
    {
        Error RootResult = AssetManager_AddSearchRoot(Manager, AssetRoot._data);
        if (RootResult.Code != ErrorCode_Success)
        {
            LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to add asset search root: %s", &RootResult);
        }
    }
    else
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to build asset root path: %s", &PathResult);
    }
    Memory_Free(AssetRoot._data);

    Error ReadResult = AssetManager_ReadDefinitions(Manager);
    if (ReadResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to read some asset definitions: %s", &ReadResult);
    }

    *outManager = Manager;
    return Error_CreateSuccess();
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

    // Resolve everything against an absolute working directory so path handling does not depend on the
    // process's current directory. GetWorkingDirectory returns a static buffer, so copy it once.
    GenericBuffer WorkingDirectory;
    GenericBuffer_AllocateVariable(&WorkingDirectory, 64U, sizeof(unsigned char));
    GenericBuffer_AppendString(&WorkingDirectory, (const unsigned char*)GetWorkingDirectory());
    GenericBuffer_NullTerminate(&WorkingDirectory);

    GameConfig Config;
    LoadConfig(&Log, WorkingDirectory._data, &Config);
    Error ConfigLogResult = Logger_LogInfo(&Log, u8"Loaded game config.");
    Error_Deconstruct(&ConfigLogResult);

    UnicodeData Unicode;
    LoadUnicodeData(&Log, WorkingDirectory._data, &Unicode);
    Error UnicodeLogResult = Logger_LogInfo(&Log, u8"Loaded unicode data.");
    Error_Deconstruct(&UnicodeLogResult);

    InitializeWindow(&Config);
    // For this test milestone ESC quits the window (the world-test frame captures the cursor).
    SetExitKey(KEY_ESCAPE);
    Error WindowLogResult = Logger_LogInfo(&Log, u8"Window initialized.");
    Error_Deconstruct(&WindowLogResult);

    AssetManager* Assets = NULL;
    StandardAssetTypes AssetTypes;
    Memory_Zero(&AssetTypes, sizeof(AssetTypes));
    JSONObjectPool* JsonPool = NULL;
    Error AssetSetupResult = SetUpAssetManager(&Log, WorkingDirectory._data, &Assets, &AssetTypes, &JsonPool);
    if (AssetSetupResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(&Log, (const unsigned char*)u8"Failed to set up the asset manager: %s", &AssetSetupResult);
    }

    GHDFObjectPool* GhdfPool = NULL;
    Error GhdfPoolResult = GHDFObjectPool_Create(&GhdfPool);
    if (GhdfPoolResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(&Log, (const unsigned char*)u8"Failed to create the GHDF object pool: %s", &GhdfPoolResult);
    }

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

        if (Assets != NULL) { Error e = AssetManager_Deconstruct(Assets); Error_Deconstruct(&e); }
        if (JsonPool != NULL) { Error e = JSONObjectPool_Deconstruct(JsonPool); Error_Deconstruct(&e); }
        if (GhdfPool != NULL) { Error e = GHDFObjectPool_Deconstruct(GhdfPool); Error_Deconstruct(&e); }
        CloseWindow();
        UnicodeData_Deconstruct(&Unicode);
        GameConfig_Deconstruct(&Config);
        Memory_Free(WorkingDirectory._data);
        Error CloseLog = Logger_Deconstruct(&Log);
        Error_Deconstruct(&CloseLog);
        return 1;
    }

    // Bundle the shared services and hand them to the initial frame(s).
    Services GameServices;
    Services_Construct(&GameServices);
    GameServices.Logger = &Log;
    GameServices.Config = &Config;
    GameServices.Unicode = &Unicode;
    GameServices.Assets = Assets;
    GameServices.AssetTypes = AssetTypes;
    GameServices.FrameManager = &FrameManager;
    GameServices.GHDFPool = GhdfPool;
    GameServices.WorkingDirectory = WorkingDirectory._data;

    // The world-test frame needs the asset manager; only add it if the asset manager came up.
    if (Assets != NULL)
    {
        GameFrame* TestFrame = NULL;
        Error FrameResult = WorldTestFrame_Create(&GameServices, &TestFrame);
        if (FrameResult.Code != ErrorCode_Success)
        {
            LogWarningAndRelease(&Log, (const unsigned char*)u8"Failed to create the world-test frame: %s", &FrameResult);
        }
        else
        {
            Error AddResult = GameFrameManager_AddFrame(&FrameManager, TestFrame, GameFrameAddOptions_Default());
            if (AddResult.Code != ErrorCode_Success)
            {
                LogWarningAndRelease(&Log, (const unsigned char*)u8"Failed to add the world-test frame: %s", &AddResult);
                GameFrame_Destroy(TestFrame);
            }
        }
    }

    RunGameLoop(&FrameManager);

    // Tear down in the reverse of construction: frames first (they release assets and GPU render targets
    // and need both the asset manager and the GL context alive), then the asset manager and pools, then
    // the window, then the remaining plain resources.
    Error ManagerCloseResult = GameFrameManager_Deconstruct(&FrameManager);
    if (ManagerCloseResult.Code != ErrorCode_Success)
    {
        Error LogResult = Logger_LogErrorFormatted(&Log, (const unsigned char*)u8"Error while tearing down the frame manager: %s",
            (ManagerCloseResult.Message != NULL) ? (const char*)ManagerCloseResult.Message : "no details");
        Error_Deconstruct(&LogResult);
        Error_Deconstruct(&ManagerCloseResult);
    }

    if (Assets != NULL)
    {
        Error e = AssetManager_Deconstruct(Assets);
        Error_Deconstruct(&e);
    }
    if (JsonPool != NULL)
    {
        Error e = JSONObjectPool_Deconstruct(JsonPool);
        Error_Deconstruct(&e);
    }
    if (GhdfPool != NULL)
    {
        Error e = GHDFObjectPool_Deconstruct(GhdfPool);
        Error_Deconstruct(&e);
    }

    CloseWindow();

    Services_Deconstruct(&GameServices);
    UnicodeData_Deconstruct(&Unicode);
    GameConfig_Deconstruct(&Config);
    Memory_Free(WorkingDirectory._data);

    Error ShutdownResult = Logger_LogInfo(&Log, (const unsigned char*)u8"Shutting down.");
    Error_Deconstruct(&ShutdownResult);

    Error LoggerCloseResult = Logger_Deconstruct(&Log);
    Error_Deconstruct(&LoggerCloseResult);
    return 0;
}
