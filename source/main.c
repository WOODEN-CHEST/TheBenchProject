#include <inttypes.h>
#include <stdio.h>
#include "wr/WRCompile.h"
#include "wr/WRError.h"
#include "wr/WRFileSystem.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include "wr/WRUnicode.h"
#include "wr/WRUnicodeLoader.h"
#include "wr/WRGHDF.h"
#include "wr/WRJSON.h"
#include "Config.h"
#include "GameInput.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "Registry.h"
#include "ProgramTime.h"
#include "GameFrameManager.h"
#include "Renderer.h"
#include "AssetManager.h"
#include "Services.h"
#include "WorldTestFrame.h"
#include "UITestFrame.h"
#include "raylib/raylib.h"
#include "wr/WRStopwatch.h"
#include "wr/WRThread.h"


// Macros.
/** File name (resolved against the base directory) of the game configuration file. */
#define CONFIG_FILE_NAME ((const unsigned char*)u8"config.json")
/** File name (resolved against the base directory) of the player's controls (rebound actions); optional. */
#define CONTROLS_FILE_NAME ((const unsigned char*)u8"controls.json")
/** Relative path (under the base directory) of the root asset directory. */
#define ASSET_ROOT_RELATIVE ((const unsigned char*)u8"asset")
/** Relative path (under the base directory) of the Unicode database. */
#define UNICODE_DATA_RELATIVE ((const unsigned char*)u8"asset/text/unicode_data.txt")
/** Relative path (under the base directory) of the asset manager's temp-file cache. */
#define ASSET_CACHE_RELATIVE ((const unsigned char*)u8".wr_asset_cache")
/** Initial capacity (bytes) of the base directory path buffer; it grows if needed. */
#define BASE_DIRECTORY_INITIAL_CAPACITY ((size_t)256)
/** Window title shown in the title bar. */
#define WINDOW_TITLE ((const char*)u8"The Bench Project")
/** Fixed update rate in hertz. Deliberately a code constant (not a config value) and higher than the
 *  render rate so simulation stays smooth and deterministic regardless of frame rate. */
#define UPDATE_RATE_HZ 480.0
/** Upper bound on a single frame's measured time, in seconds. Clamps the accumulator so a long stall
 *  (debugger break, window drag) cannot snowball into a burst of catch-up updates ("spiral of death"). */
#define MAX_FRAME_TIME_SECONDS 0.25
/** Aspect ratio (width / height) the game's content is designed and fitted for. */
#define TARGET_ASPECT_RATIO (16.0f / 9.0f)
/** Idle threshold in seconds: while the next update tick and the next paced render are both at least this
 *  far away, the loop sleeps instead of spinning. Matches IDLE_SLEEP_MILLISECONDS. */
#define IDLE_SLEEP_THRESHOLD_SECONDS 0.001
/** Length of one idle sleep in milliseconds, the shortest sleep the OS reliably honours (raylib raises the
 *  Windows timer resolution to 1 ms). Waking slightly late is harmless: every owed update tick still runs,
 *  each with the fixed delta. */
#define IDLE_SLEEP_MILLISECONDS ((size_t)1)


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
    unsigned int Flags = 0;
    if (config->IsFullscreen)
    {
        Flags |= FLAG_FULLSCREEN_MODE;
    }
    if (config->IsVSyncEnabled)
    {
        Flags |= FLAG_VSYNC_HINT;
    }
    if (Flags != 0)
    {
        SetConfigFlags(Flags);
    }

    InitWindow((int)config->ResolutionWidth, (int)config->ResolutionHeight, WINDOW_TITLE);

    // Deliberately no SetTargetFPS: RunGameLoop paces rendering itself. This project's raylib is built with
    // SUPPORT_CUSTOM_FRAME_CONTROL (see BUILDING.md), so EndDrawing neither swaps buffers nor polls input:
    // GameFrameManager_Update polls input once per update tick, and GameFrameManager_Render swaps buffers.
}

static double GetClockSeconds(void)
{
    return (double)Stopwatch_GetTimestampNanoseconds() / 1e9;
}

/* Advances the render deadline by one interval, keeping any overshoot so the average rate matches the target
 * (re-anchoring on the actual render time would make 60 FPS settle below 60). After a stall of a whole
 * interval or more it re-anchors to now instead of rendering a burst of catch-up frames. */
static double AdvanceRenderDeadline(double deadline, double currentTime, double interval)
{
    double Next = deadline + interval;
    return (Next <= currentTime) ? (currentTime + interval) : Next;
}

/* Sleeps one idle step when nothing is due soon: rendering is paced and both the next update tick and the
 * next render deadline are at least IDLE_SLEEP_THRESHOLD_SECONDS away. With unlocked rendering every
 * iteration renders, so the loop is never idle. */
static void SleepIfIdle(bool isRenderPaced, double secondsUntilUpdate, double secondsUntilRender)
{
    if (!isRenderPaced
        || (secondsUntilUpdate < IDLE_SLEEP_THRESHOLD_SECONDS)
        || (secondsUntilRender < IDLE_SLEEP_THRESHOLD_SECONDS))
    {
        return;
    }

    Error SleepResult = Thread_Sleep(IDLE_SLEEP_MILLISECONDS);
    Error_Deconstruct(&SleepResult); // A failed sleep only costs one more loop iteration.
}

/* Runs the game loop with the update and render cadences decoupled, both timed from WRStopwatch's monotonic
 * clock. Fixed-timestep updates run at UPDATE_RATE_HZ through an accumulator. A render happens whenever the
 * render deadline (every @p targetRenderSeconds) has passed, or every iteration when @p targetRenderSeconds is
 * <= 0 (unlocked).
 *
 * Both cadences report simulation time: update ticks get the fixed-step clock, and a render gets that clock
 * plus the not-yet-simulated remainder. They share one epoch (0 when the loop starts) and one pace, so a time
 * captured in update can be compared with one seen in render, even after a stall clamps the elapsed time.
 *
 * When neither an update tick nor a paced render is due soon, the loop sleeps briefly instead of spinning, so
 * it does not hold a CPU core at 100%. Returns when the window is closed or the manager reports a fatal
 * error. */
static void RunGameLoop(GameFrameManager* frameManager, double targetRenderSeconds)
{
    const double UpdateDelta = 1.0 / UPDATE_RATE_HZ;
    const bool IsRenderPaced = (targetRenderSeconds > 0.0);
    const double LoopStartTime = GetClockSeconds();

    double PreviousTime = 0.0;
    double UpdateAccumulator = 0.0;
    double TotalUpdateTime = 0.0;
    double NextRenderTime = 0.0;
    double LastRenderSimulationTime = 0.0;
    bool HasFatalError = false;

    while (!WindowShouldClose() && !GameFrameManager_ShouldStop(frameManager) && !HasFatalError)
    {
        // Real time since the loop started; drives the accumulator, the render deadline and idle sleeping.
        double CurrentTime = GetClockSeconds() - LoopStartTime;
        double Elapsed = CurrentTime - PreviousTime;
        PreviousTime = CurrentTime;
        if (Elapsed > MAX_FRAME_TIME_SECONDS)
        {
            Elapsed = MAX_FRAME_TIME_SECONDS;
        }

        // Update loop: run whole fixed steps for the real time elapsed. GameFrameManager_Update polls input
        // at its start, so edge-triggered input stays reliable no matter the render rate.
        UpdateAccumulator += Elapsed;
        while (UpdateAccumulator >= UpdateDelta)
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
            UpdateAccumulator -= UpdateDelta;
        }
        if (HasFatalError)
        {
            break;
        }

        // Render loop: when the deadline has passed (or every iteration when unlocked).
        if (!IsRenderPaced || (CurrentTime >= NextRenderTime))
        {
            double SimulationTime = TotalUpdateTime + UpdateAccumulator;
            double SinceRender = SimulationTime - LastRenderSimulationTime;
            LastRenderSimulationTime = SimulationTime;
            NextRenderTime = AdvanceRenderDeadline(NextRenderTime, CurrentTime, targetRenderSeconds);

            // The frames' last update-side step (poses, the camera on a posed head), once per shown frame; the render
            // only reads.
            ProgramTime RenderTime = ProgramTime_Create(SimulationTime, SinceRender);
            Error PrepareResult = GameFrameManager_PrepareRender(frameManager, RenderTime);
            if (PrepareResult.Code != ErrorCode_Success)
            {
                Error_Deconstruct(&PrepareResult);
                HasFatalError = true;
                break;
            }
            Error RenderResult = GameFrameManager_Render(frameManager, RenderTime);
            if (RenderResult.Code != ErrorCode_Success)
            {
                Error_Deconstruct(&RenderResult);
                HasFatalError = true;
                break;
            }
        }

        double Now = GetClockSeconds() - LoopStartTime;
        double SecondsUntilUpdate = (UpdateDelta - UpdateAccumulator) - (Now - CurrentTime);
        SleepIfIdle(IsRenderPaced, SecondsUntilUpdate, NextRenderTime - Now);
    }
}

/* Loads the game config from the absolute config path, warning (and keeping defaults) on failure. */
static void LoadConfig(Logger* logger, const unsigned char* baseDirectory, GameConfig* outConfig)
{
    GameConfig_SetDefaults(outConfig);

    GenericBuffer ConfigPath;
    Error PathResult = BuildAbsolutePath(baseDirectory, CONFIG_FILE_NAME, &ConfigPath);
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

/* Loads the player's controls over the default bindings. Without a controls file the defaults are used; a bad file is
 * warned about and ignored as a whole. */
// TODO: nothing writes the file yet; the controls menu will save rebound controls with GameInput_SaveBindingsToFile.
static void LoadControls(Logger* logger, const unsigned char* baseDirectory, GameInput* input)
{
    GenericBuffer ControlsPath;
    Error PathResult = BuildAbsolutePath(baseDirectory, CONTROLS_FILE_NAME, &ControlsPath);
    if (PathResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to build the controls path: %s", &PathResult);
        Memory_Free(ControlsPath._data);
        return;
    }

    Error LoadResult = GameInput_LoadBindingsFromFile(input, ControlsPath._data);
    Memory_Free(ControlsPath._data);
    if ((LoadResult.Code == ErrorCode_FileNotFound) || (LoadResult.Code == ErrorCode_DirectoryNotFound))
    {
        Error_Deconstruct(&LoadResult);
        return;
    }
    if (LoadResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to load the controls file, using the default controls: %s",
            &LoadResult);
        return;
    }
    Error LogResult = Logger_LogInfo(logger, (const unsigned char*)u8"Loaded the controls file.");
    Error_Deconstruct(&LogResult);
}

/* Logs the registries in the root and their active entries, once at startup. */
static void LogRegistries(Logger* logger, const RegistryRoot* registries)
{
    GenericBuffer Line;
    GenericBuffer_AllocateVariable(&Line, 128U, sizeof(unsigned char));
    const Registry* Root = RegistryRoot_AsRegistry(registries);
    Error Result = StringUTF8_Format((const unsigned char*)u8"Registries:", &Line);
    uint32_t ListedCount = 0U;
    uint32_t SlotCount = RegistryRoot_GetCount(registries);
    for (uint32_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < SlotCount); Index++)
    {
        const Registry* Listed = RegistryRoot_Get(registries, Index);
        if (Listed == NULL)
        {
            continue;
        }
        const char* Separator = (ListedCount == 0U) ? "" : ",";
        uint32_t EntryCount = Registry_GetActiveCount(Listed);
        Result = StringUTF8_Format((const unsigned char*)u8"%s %s (%" PRIu32 " %s)", &Line, Separator,
            (const char*)Registry_GetKeyText(Root, Index), EntryCount, (EntryCount == 1U) ? "entry" : "entries");
        ListedCount++;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringUTF8_Format((ListedCount == 0U) ? (const unsigned char*)u8" none." : (const unsigned char*)u8".",
            &Line);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Error LogResult = Logger_LogInfo(logger, Line._data);
        Error_Deconstruct(&LogResult);
    }
    else
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Could not list the registries: %s", &Result);
    }
    Memory_Free(Line._data);
}

/* Loads the Unicode database from disk, falling back to an empty (valid) database on failure. */
static void LoadUnicodeData(Logger* logger, const unsigned char* baseDirectory, UnicodeData* outData)
{
    GenericBuffer UnicodePath;
    Error PathResult = BuildAbsolutePath(baseDirectory, UNICODE_DATA_RELATIVE, &UnicodePath);
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
static Error SetUpAssetManager(Logger* logger, const unsigned char* baseDirectory,
    AssetManager** outManager, StandardAssetTypes* outTypes, JSONObjectPool** outPool)
{
    *outManager = NULL;
    *outPool = NULL;

    AssetManager* Manager = NULL;
    Error ConstructResult = AssetManager_Construct1(logger, &Manager);
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

    // Keep temp files next to the executable too (the manager's default is relative to the working
    // directory). Only the path is stored; the directory is created on first use.
    GenericBuffer CacheDirectory;
    Error CachePathResult = BuildAbsolutePath(baseDirectory, ASSET_CACHE_RELATIVE, &CacheDirectory);
    if (CachePathResult.Code == ErrorCode_Success)
    {
        Error CacheResult = AssetManager_SetCacheDirectory(Manager, CacheDirectory._data);
        if (CacheResult.Code != ErrorCode_Success)
        {
            LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to set the asset cache directory: %s", &CacheResult);
        }
    }
    else
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Failed to build the asset cache path: %s", &CachePathResult);
    }
    Memory_Free(CacheDirectory._data);

    GenericBuffer AssetRoot;
    Error PathResult = BuildAbsolutePath(baseDirectory, ASSET_ROOT_RELATIVE, &AssetRoot);
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

    // Bad individual definition files are skipped, and the asset manager logs each one plus a count; an error
    // here means a whole directory could not be scanned.
    Error ReadResult = AssetManager_ReadDefinitions(Manager, NULL);
    if (ReadResult.Code != ErrorCode_Success)
    {
        LogWarningAndRelease(logger, (const unsigned char*)u8"Asset definition scan was incomplete: %s", &ReadResult);
    }

    *outManager = Manager;
    return Error_CreateSuccess();
}


// Program entry.
int main(int argc, char** argv)
{
    UNUSED(argc);
    UNUSED(argv);

    // Every game file (config, assets, logs, the asset cache) is resolved against the executable's directory,
    // so the game works no matter which directory it is started from.
    GenericBuffer BaseDirectory;
    GenericBuffer_AllocateVariable(&BaseDirectory, BASE_DIRECTORY_INITIAL_CAPACITY, sizeof(unsigned char));
    Error BaseDirectoryResult = FileSystem_GetExecutableDirectory(&BaseDirectory);
    bool HasExecutableDirectory = (BaseDirectoryResult.Code == ErrorCode_Success);

    // The logger is the very first thing initialized so every later step can log through it.
    Logger Log;
    Error LoggerResult = Logger_Construct(&Log, HasExecutableDirectory ? BaseDirectory._data : NULL);
    if (LoggerResult.Code != ErrorCode_Success)
    {
        // The logger itself is unavailable, so this is the one place we fall back to a raw stdout
        // print before aborting the whole program.
        fprintf(stdout, "Failed to initialize the logger: %s\n",
            (LoggerResult.Message != NULL) ? (const char*)LoggerResult.Message : "no details");
        Error_Deconstruct(&LoggerResult);
        Error_Deconstruct(&BaseDirectoryResult);
        Memory_Free(BaseDirectory._data);
        return 1;
    }

    if (!HasExecutableDirectory)
    {
        // Fall back to the working directory so the game can still start, and say so.
        // GetWorkingDirectory returns a static buffer, so copy it.
        LogWarningAndRelease(&Log, (const unsigned char*)u8"Could not determine the executable's directory; using the working directory instead: %s",
            &BaseDirectoryResult);
        GenericBuffer_Clear(&BaseDirectory);
        GenericBuffer_AppendString(&BaseDirectory, (const unsigned char*)GetWorkingDirectory());
        GenericBuffer_NullTerminate(&BaseDirectory);
    }

    GameConfig Config;
    LoadConfig(&Log, BaseDirectory._data, &Config);
    Error ConfigLogResult = Logger_LogInfo(&Log, u8"Loaded game config.");
    Error_Deconstruct(&ConfigLogResult);

    UnicodeData Unicode;
    LoadUnicodeData(&Log, BaseDirectory._data, &Unicode);
    Error UnicodeLogResult = Logger_LogInfo(&Log, u8"Loaded unicode data.");
    Error_Deconstruct(&UnicodeLogResult);

    InitializeWindow(&Config);
    if (!IsWindowReady())
    {
        // Everything after this point needs a window and its GL context. raylib has printed the cause (e.g. no
        // display, or no usable OpenGL 3.3 driver) to stdout.
        Error FailLogResult = Logger_LogCritical(&Log,
            (const unsigned char*)u8"Could not create the game window or its OpenGL context; see raylib's output above.");
        Error_Deconstruct(&FailLogResult);
        UnicodeData_Deconstruct(&Unicode);
        GameConfig_Deconstruct(&Config);
        Memory_Free(BaseDirectory._data);
        Error CloseLog = Logger_Deconstruct(&Log);
        Error_Deconstruct(&CloseLog);
        return 1;
    }
    // ESC does not quit through raylib: it is the UI's ui_back and the game's pause action, read through GameInput after
    // the UI had first refusal (the world-test frame quits on an unconsumed pause).
    SetExitKey(KEY_NULL);
    Error WindowLogResult = Logger_LogInfo(&Log, u8"Window initialized.");
    Error_Deconstruct(&WindowLogResult);

    AssetManager* Assets = NULL;
    StandardAssetTypes AssetTypes;
    Memory_Zero(&AssetTypes, sizeof(AssetTypes));
    JSONObjectPool* JsonPool = NULL;
    Error AssetSetupResult = SetUpAssetManager(&Log, BaseDirectory._data, &Assets, &AssetTypes, &JsonPool);
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

    // The program's one key registry: tags and object data keys are interned here. Every world borrows it, so it is
    // torn down after the frames.
    KeyRegistry Keys;
    Error KeysResult = KeyRegistry_Construct(&Keys);
    Error_Deconstruct(&KeysResult); // Only fails for a NULL argument.

    // The root registry: every system adds its registries to it, for tools that work on every kind. It is torn down
    // after everything that added a registry withdrew it, and before the key registry it borrows.
    RegistryRoot Registries;
    Error RegistriesResult = RegistryRoot_Construct(&Registries, &Keys);
    Error_Deconstruct(&RegistriesResult); // Only fails for a NULL argument (its key is the first key interned).

    // The input: default bindings, then the player's controls file over them. The frame manager polls it every update
    // tick.
    GameInput Input;
    Error InputResult = GameInput_Construct(&Input);
    Error_Deconstruct(&InputResult); // Only fails for a NULL argument.
    LoadControls(&Log, BaseDirectory._data, &Input);
    LogRegistries(&Log, &Registries);

    // The frame manager owns the scenes and the per-frame update/render logic. The window (GL context)
    // exists by now, which is what its render targets need.
    GameFrameManager FrameManager;
    Error ManagerResult = GameFrameManager_Construct(&FrameManager, &Log, &Input, TARGET_ASPECT_RATIO,
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
        Error InputCloseResult = GameInput_Deconstruct(&Input);
        Error_Deconstruct(&InputCloseResult);
        RegistryRoot_Deconstruct(&Registries);
        KeyRegistry_Deconstruct(&Keys);
        UnicodeData_Deconstruct(&Unicode);
        GameConfig_Deconstruct(&Config);
        Memory_Free(BaseDirectory._data);
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
    GameServices.Input = &Input;
    GameServices.GHDFPool = GhdfPool;
    GameServices.Keys = &Keys;
    GameServices.Registries = &Registries;
    GameServices.BaseDirectory = BaseDirectory._data;

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

        // The UI test runs in its own frame on top of the world, so it gets the input first (frames update top-down).
        GameFrame* UIFrame = NULL;
        Error UIFrameResult = UITestFrame_Create(&GameServices, &UIFrame);
        if (UIFrameResult.Code != ErrorCode_Success)
        {
            LogWarningAndRelease(&Log, (const unsigned char*)u8"Failed to create the UI test frame: %s", &UIFrameResult);
        }
        else
        {
            Error AddResult = GameFrameManager_AddFrame(&FrameManager, UIFrame, GameFrameAddOptions_Default());
            if (AddResult.Code != ErrorCode_Success)
            {
                LogWarningAndRelease(&Log, (const unsigned char*)u8"Failed to add the UI test frame: %s", &AddResult);
                GameFrame_Destroy(UIFrame);
            }
        }
    }

    // Render pacing target (seconds/frame); 0 = unlocked (render every loop iteration). The update loop
    // runs at its own fixed rate regardless.
    double TargetRenderSeconds = Config.IsFPSUnlocked ? 0.0 : (1.0 / (double)Config.TargetFPS);
    RunGameLoop(&FrameManager, TargetRenderSeconds);

    // Tear down in the reverse of construction: frames first (they release assets and GPU render targets
    // and need both the asset manager and the GL context alive), then the asset manager and pools, then
    // the window, then the remaining plain resources (the input before the root registry, the root before the key
    // registry).
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
    Error InputCloseResult = GameInput_Deconstruct(&Input);
    Error_Deconstruct(&InputCloseResult);
    RegistryRoot_Deconstruct(&Registries);
    KeyRegistry_Deconstruct(&Keys);
    UnicodeData_Deconstruct(&Unicode);
    GameConfig_Deconstruct(&Config);
    Memory_Free(BaseDirectory._data);

    Error ShutdownResult = Logger_LogInfo(&Log, (const unsigned char*)u8"Shutting down.");
    Error_Deconstruct(&ShutdownResult);

    Error LoggerCloseResult = Logger_Deconstruct(&Log);
    Error_Deconstruct(&LoggerCloseResult);
    return 0;
}
