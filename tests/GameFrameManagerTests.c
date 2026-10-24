#include "TestSuites.h"
#include "GameFrame.h"
#include "GameFrameManager.h"
#include "GameInput.h"
#include "Logger.h"
#include "ProgramTime.h"
#include "wr/WRMemory.h"
#include <string.h>


// Macros.
/** Initial capacity (bytes) of the work directory path; it grows as needed. */
#define MANAGER_TEST_PATH_CAPACITY ((size_t)256)
/** The most PrepareRender calls one log keeps. */
#define MANAGER_TEST_MAX_CALLS 15U
/** The fixed tick the test advances by, in seconds. */
#define MANAGER_TEST_TICK_SECONDS (1.0 / 480.0)
/** The ticks the frames need to load and start (each takes one lifecycle step a tick). */
#define MANAGER_TEST_START_TICKS 4


// Types.
/* The order the test frames' PrepareRender ran in: their one-letter names. */
typedef struct CallLogStruct
{
    char Names[MANAGER_TEST_MAX_CALLS + 1U];
    uint32_t Count;
} CallLog;

/* A frame that loads at once and does nothing but write its name into the log from PrepareRender (or fail it). */
typedef struct RecordingFrameStruct
{
    GameFrame Base;
    char Name;
    CallLog* Log;
    bool FailsPrepare;
} RecordingFrame;


// Static functions: the recording frame.
static Error RecordingFrame_Succeed(void* self, ProgramTime time)
{
    (void)self;
    (void)time;
    return Error_CreateSuccess();
}

static Error RecordingFrame_Step(void* self)
{
    (void)self;
    return Error_CreateSuccess();
}

static bool RecordingFrame_IsDone(void* self)
{
    (void)self;
    return true;
}

static Error RecordingFrame_PrepareRender(void* self, ProgramTime time)
{
    (void)time;
    RecordingFrame* Frame = self;
    if (Frame->FailsPrepare)
    {
        return Error_Construct2(ErrorCode_InvalidState, "RecordingFrame: PrepareRender fails on purpose.");
    }
    if (Frame->Log->Count < MANAGER_TEST_MAX_CALLS)
    {
        Frame->Log->Names[Frame->Log->Count] = Frame->Name;
        Frame->Log->Count++;
        Frame->Log->Names[Frame->Log->Count] = '\0';
    }
    return Error_CreateSuccess();
}

static Error RecordingFrame_Render(void* self, const FrameRenderContext* context, RenderTexture2D target)
{
    (void)self;
    (void)context;
    (void)target;
    return Error_CreateSuccess();
}

static void RecordingFrame_Destroy(void* self)
{
    RecordingFrame* Frame = self;
    GameFrame_Deconstruct(&Frame->Base);
    Memory_Free(Frame);
}

static const GameFrameVTable RecordingVTable =
{
    .Start = RecordingFrame_Succeed, .End = RecordingFrame_Succeed, .Update = RecordingFrame_Succeed,
    .BeginLoad = RecordingFrame_Step, .LoadStep = RecordingFrame_Step, .IsLoaded = RecordingFrame_IsDone,
    .BeginUnload = RecordingFrame_Step, .UnloadStep = RecordingFrame_Step, .IsUnloaded = RecordingFrame_IsDone,
    .PrepareRender = RecordingFrame_PrepareRender, .Render = RecordingFrame_Render, .GetLoadProgress = NULL,
    .OnResize = NULL, .Destroy = RecordingFrame_Destroy
};

/* The same frame without the optional slot. */
static const GameFrameVTable SilentVTable =
{
    .Start = RecordingFrame_Succeed, .End = RecordingFrame_Succeed, .Update = RecordingFrame_Succeed,
    .BeginLoad = RecordingFrame_Step, .LoadStep = RecordingFrame_Step, .IsLoaded = RecordingFrame_IsDone,
    .BeginUnload = RecordingFrame_Step, .UnloadStep = RecordingFrame_Step, .IsUnloaded = RecordingFrame_IsDone,
    .PrepareRender = NULL, .Render = RecordingFrame_Render, .GetLoadProgress = NULL, .OnResize = NULL,
    .Destroy = RecordingFrame_Destroy
};

/* Makes a frame and adds it on top of the manager's stack (it then belongs to the manager); NULL with a failed check. */
static RecordingFrame* AddFrame(TestRun* run, GameFrameManager* manager, const GameFrameVTable* vtable, char name,
    CallLog* log, bool startsAutomatically)
{
    RecordingFrame* Frame = Memory_Allocate(sizeof(RecordingFrame));
    Memory_Zero(Frame, sizeof(*Frame));
    Frame->Name = name;
    Frame->Log = log;
    if (!TEST_CHECK_SUCCESS(run, GameFrame_Construct(&Frame->Base, vtable, (const unsigned char*)u8"recording")))
    {
        Memory_Free(Frame);
        return NULL;
    }
    GameFrameAddOptions Options = GameFrameAddOptions_Default();
    Options.StartAutomatically = startsAutomatically;
    Options.AddToTop = true;
    if (!TEST_CHECK_SUCCESS(run, GameFrameManager_AddFrame(manager, &Frame->Base, Options)))
    {
        RecordingFrame_Destroy(Frame);
        return NULL;
    }
    return Frame;
}

static bool HasCalls(const CallLog* log, const char* expected)
{
    return strcmp(log->Names, expected) == 0;
}


// Tests.
/* G35: PrepareRender runs top-down over the active frames that are updated and rendered, skips those without the slot,
 * and a failure stops the manager. */
static void PrepareRenderRunsTopDownOverTheActiveFrames(TestRun* run)
{
    Logger Log;
    GameInput Input;
    GameFrameManager Manager;
    GenericBuffer Base;
    GenericBuffer_AllocateVariable(&Base, MANAGER_TEST_PATH_CAPACITY, sizeof(unsigned char));
    bool IsLoggerConstructed = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "frame_manager", &Base))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Base._data));
    Memory_Free(Base._data);
    TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input));
    bool IsManagerConstructed = IsLoggerConstructed && TEST_CHECK_SUCCESS(run, GameFrameManager_Construct(&Manager, &Log,
        &Input, 16.0f / 9.0f, RenderTargetPosition_Centered()));
    CallLog Calls = { .Names = { 0 }, .Count = 0U };
    // From the bottom up: A, B, a frame without the slot, D (never activated), C on top.
    RecordingFrame* A = IsManagerConstructed ? AddFrame(run, &Manager, &RecordingVTable, 'A', &Calls, true) : NULL;
    RecordingFrame* B = (A != NULL) ? AddFrame(run, &Manager, &RecordingVTable, 'B', &Calls, true) : NULL;
    RecordingFrame* Silent = (B != NULL) ? AddFrame(run, &Manager, &SilentVTable, 'S', &Calls, true) : NULL;
    RecordingFrame* D = (Silent != NULL) ? AddFrame(run, &Manager, &RecordingVTable, 'D', &Calls, false) : NULL;
    RecordingFrame* C = (D != NULL) ? AddFrame(run, &Manager, &RecordingVTable, 'C', &Calls, true) : NULL;
    if (C != NULL)
    {
        double Time = 0.0;
        for (int Tick = 0; Tick < MANAGER_TEST_START_TICKS; Tick++)
        {
            Time += MANAGER_TEST_TICK_SECONDS;
            TEST_CHECK_SUCCESS(run, GameFrameManager_Update(&Manager, ProgramTime_Create(Time, MANAGER_TEST_TICK_SECONDS)));
        }
        GameFrameState State = GameFrameState_Active;
        TEST_CHECK(run, GameFrameManager_TryGetFrameState(&Manager, &C->Base, &State) && (State == GameFrameState_Active));
        TEST_CHECK(run, GameFrameManager_TryGetFrameState(&Manager, &D->Base, &State) && (State != GameFrameState_Active));
        ProgramTime Frame = ProgramTime_Create(Time, 1.0 / 60.0);

        // Top-down; D waits to be activated, S has no slot.
        TEST_CHECK_SUCCESS(run, GameFrameManager_PrepareRender(&Manager, Frame));
        TEST_CHECK(run, HasCalls(&Calls, "CBA"));
        // A hidden frame has nothing to show its state on, a frozen one nothing new to show.
        Calls.Count = 0U;
        Calls.Names[0] = '\0';
        B->Base.IsRendered = false;
        A->Base.IsUpdated = false;
        TEST_CHECK_SUCCESS(run, GameFrameManager_PrepareRender(&Manager, Frame));
        TEST_CHECK(run, HasCalls(&Calls, "C") && !GameFrameManager_ShouldStop(&Manager));
        // A failure is fatal, as every frame slot's.
        C->FailsPrepare = true;
        TEST_CHECK_ERROR(run, GameFrameManager_PrepareRender(&Manager, Frame), ErrorCode_InvalidState);
        TEST_CHECK(run, GameFrameManager_ShouldStop(&Manager));
    }
    if (IsManagerConstructed)
    {
        TEST_CHECK_SUCCESS(run, GameFrameManager_Deconstruct(&Manager));
    }
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
    if (IsLoggerConstructed)
    {
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    }
}


// Public functions.
void GameFrameManagerTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "prepare_render_runs_top_down_over_the_active_frames", PrepareRenderRunsTopDownOverTheActiveFrames },
    };
    TestRun_RunSuite(run, "GameFrameManager", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
