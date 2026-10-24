#include "GameFrameManager.h"
#include "Logger.h"
#include "wr/WRCompile.h"
#include "raylib/raylib.h"
#include <stdint.h>


// Macros.
/** Initial record capacity; only a handful of frames are ever resident at once. */
#define INITIAL_RECORD_CAPACITY 4


// Types.
/**
 * Internal bookkeeping the manager keeps for one resident frame. Records are heap-allocated so their
 * addresses stay stable while the pointer array that references them grows or shrinks; the manager owns
 * the frame and, when rendered, its render target.
 */
typedef struct GameFrameRecordStruct
{
    GameFrame* Frame;          // Owned by the manager.
    GameFrameState State;
    int64_t ZOrder;            // Higher = drawn later (on top).
    bool StartRequested;       // Auto-start on load, or requested via ActivateFrame.
    bool RemovalRequested;     // RemoveFrame was called; teardown begins on the next update.
    bool HasTarget;
    RenderTexture2D Target;    // Valid only when HasTarget is true; owned.
} GameFrameRecord;


// Static functions.
static GameFrameRecord* RecordAt(GameFrameManager* self, size_t index)
{
    void* Slot = GenericBuffer_GetPointerToElement(&self->_records, index);
    if (Slot == NULL)
    {
        return NULL;
    }
    return *(GameFrameRecord**)Slot;
}

static GameFrameRecord* FindRecord(GameFrameManager* self, GameFrame* frame, size_t* outIndex)
{
    for (size_t i = 0; i < self->_records._count; i++)
    {
        GameFrameRecord* Record = RecordAt(self, i);
        if ((Record != NULL) && (Record->Frame == frame))
        {
            if (outIndex != NULL)
            {
                *outIndex = i;
            }
            return Record;
        }
    }
    return NULL;
}

static int64_t ComputeZOrder(GameFrameManager* self, bool addToTop)
{
    size_t Count = self->_records._count;
    if (Count == 0)
    {
        return 0;
    }

    int64_t MinZ = INT64_MAX;
    int64_t MaxZ = INT64_MIN;
    for (size_t i = 0; i < Count; i++)
    {
        GameFrameRecord* Record = RecordAt(self, i);
        if (Record == NULL)
        {
            continue;
        }
        if (Record->ZOrder < MinZ)
        {
            MinZ = Record->ZOrder;
        }
        if (Record->ZOrder > MaxZ)
        {
            MaxZ = Record->ZOrder;
        }
    }

    return addToTop ? (MaxZ + 1) : (MinZ - 1);
}

/* Frees a record's render target and its frame, then the record itself. */
static void DestroyRecord(GameFrameRecord* record)
{
    if (record == NULL)
    {
        return;
    }
    if (record->HasTarget)
    {
        UnloadRenderTexture(record->Target);
        record->HasTarget = false;
    }
    if (record->Frame != NULL)
    {
        GameFrame_Destroy(record->Frame);
        record->Frame = NULL;
    }
    Memory_Free(record);
}

/* Logs a fatal frame error as CRITICAL, flags the manager to stop, and hands the same error back so it
 * can propagate to the caller (which owns and releases it). */
static Error FailFatally(GameFrameManager* self, GameFrame* frame, const char* phase, Error error)
{
    const unsigned char* Name = ((frame != NULL) && (frame->DebugName != NULL))
        ? frame->DebugName : (const unsigned char*)u8"<unnamed>";

    Error LogResult = Logger_LogCriticalFormatted(self->_logger,
        (const unsigned char*)u8"Fatal error in game frame \"%s\" during %s: %s",
        (const char*)Name,
        phase,
        (error.Message != NULL) ? (const char*)error.Message : "no details");
    Error_Deconstruct(&LogResult);

    self->_shouldStop = true;
    return error;
}

/* Removes a record pointer from the record list (the record itself is not freed). */
static void RemoveRecordPointer(GameFrameManager* self, GameFrameRecord* record)
{
    for (size_t i = 0; i < self->_records._count; i++)
    {
        if (RecordAt(self, i) == record)
        {
            GenericBuffer_RemoveAt(&self->_records, i);
            return;
        }
    }
}

/* Performs one tick's lifecycle action for a record (an Unloaded record is destroyed: it must not be used again). */
static Error StepRecord(GameFrameManager* self, GameFrameRecord* record, ProgramTime time)
{
    GameFrame* Frame = record->Frame;

    // Teardown initiation takes priority over the normal state action.
    if (record->RemovalRequested
        && (record->State != GameFrameState_Unloading)
        && (record->State != GameFrameState_Unloaded))
    {
        record->RemovalRequested = false;
        if (record->State == GameFrameState_Active)
        {
            Error EndResult = GameFrame_End(Frame, time);
            if (EndResult.Code != ErrorCode_Success)
            {
                return FailFatally(self, Frame, "end", EndResult);
            }
        }
        Error BeginUnloadResult = GameFrame_BeginUnload(Frame);
        if (BeginUnloadResult.Code != ErrorCode_Success)
        {
            return FailFatally(self, Frame, "begin unload", BeginUnloadResult);
        }
        record->State = GameFrameState_Unloading;
        return Error_CreateSuccess();
    }

    switch (record->State)
    {
        case GameFrameState_Loading:
        {
            Error StepResult = GameFrame_LoadStep(Frame);
            if (StepResult.Code != ErrorCode_Success)
            {
                return FailFatally(self, Frame, "load step", StepResult);
            }
            if (GameFrame_IsLoaded(Frame))
            {
                Error RaiseResult = WREvent_Raise(&Frame->OnLoad, Frame);
                if (RaiseResult.Code != ErrorCode_Success)
                {
                    return FailFatally(self, Frame, "OnLoad event", RaiseResult);
                }
                record->State = GameFrameState_Loaded;
            }
            return Error_CreateSuccess();
        }

        case GameFrameState_Loaded:
        {
            if (record->StartRequested)
            {
                Error StartResult = GameFrame_Start(Frame, time);
                if (StartResult.Code != ErrorCode_Success)
                {
                    return FailFatally(self, Frame, "start", StartResult);
                }
                record->State = GameFrameState_Active;
            }
            return Error_CreateSuccess();
        }

        case GameFrameState_Active:
        {
            if (Frame->IsUpdated)
            {
                Error UpdateResult = GameFrame_Update(Frame, time);
                if (UpdateResult.Code != ErrorCode_Success)
                {
                    return FailFatally(self, Frame, "update", UpdateResult);
                }
            }
            return Error_CreateSuccess();
        }

        case GameFrameState_Unloading:
        {
            Error StepResult = GameFrame_UnloadStep(Frame);
            if (StepResult.Code != ErrorCode_Success)
            {
                return FailFatally(self, Frame, "unload step", StepResult);
            }
            if (GameFrame_IsUnloaded(Frame))
            {
                Error RaiseResult = WREvent_Raise(&Frame->OnUnload, Frame);
                if (RaiseResult.Code != ErrorCode_Success)
                {
                    return FailFatally(self, Frame, "OnUnload event", RaiseResult);
                }
                record->State = GameFrameState_Unloaded;
            }
            return Error_CreateSuccess();
        }

        case GameFrameState_Unloaded:
        {
            RemoveRecordPointer(self, record);
            DestroyRecord(record);
            return Error_CreateSuccess();
        }
    }
    return Error_CreateSuccess();
}

/* Recreates render targets at the new size and notifies frames when the window size changed. */
static Error HandleResize(GameFrameManager* self, int32_t width, int32_t height)
{
    bool WasInitialized = (self->_targetWidth != 0) && (self->_targetHeight != 0);
    self->_targetWidth = width;
    self->_targetHeight = height;

    for (size_t i = 0; i < self->_records._count; i++)
    {
        GameFrameRecord* Record = RecordAt(self, i);
        if ((Record != NULL) && Record->HasTarget)
        {
            UnloadRenderTexture(Record->Target);
            Record->Target = LoadRenderTexture((int)width, (int)height);
        }
    }

    // The first size discovery (0 -> real) is not a real resize, so don't notify frames about it.
    if (!WasInitialized)
    {
        return Error_CreateSuccess();
    }

    for (size_t i = 0; i < self->_records._count; i++)
    {
        GameFrameRecord* Record = RecordAt(self, i);
        if (Record == NULL)
        {
            continue;
        }
        Error ResizeResult = GameFrame_OnResize(Record->Frame, width, height);
        if (ResizeResult.Code != ErrorCode_Success)
        {
            return FailFatally(self, Record->Frame, "resize", ResizeResult);
        }
    }
    return Error_CreateSuccess();
}


// Public functions.
GameFrameAddOptions GameFrameAddOptions_Default(void)
{
    return (GameFrameAddOptions)
    {
        .StartAutomatically = true,
        .AddToTop = true
    };
}

Error GameFrameManager_Construct(GameFrameManager* self, Logger* logger, GameInput* input, float targetAspectRatio,
    Vector2 targetAreaPosition)
{
    if ((self == NULL) || (logger == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrameManager_Construct: self and logger must not be NULL.");
    }
    if (targetAspectRatio <= 0.0f)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrameManager_Construct: targetAspectRatio must be positive.");
    }

    self->_logger = logger;
    self->_input = input;
    GenericBuffer_AllocateVariable(&self->_records, INITIAL_RECORD_CAPACITY, sizeof(GameFrameRecord*));
    GenericBuffer_AllocateVariable(&self->_updateOrder, INITIAL_RECORD_CAPACITY, sizeof(GameFrameRecord*));
    self->_targetAspectRatio = targetAspectRatio;
    self->_targetAreaPosition = targetAreaPosition;
    self->_targetWidth = 0;
    self->_targetHeight = 0;
    self->_shouldStop = false;

    return Error_CreateSuccess();
}

Error GameFrameManager_Deconstruct(GameFrameManager* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error FirstError = Error_CreateSuccess();

    for (size_t i = 0; i < self->_records._count; i++)
    {
        GameFrameRecord* Record = RecordAt(self, i);
        if (Record == NULL)
        {
            continue;
        }

        // Best-effort logic teardown for still-active frames before forced destruction.
        if (Record->State == GameFrameState_Active)
        {
            Error EndResult = GameFrame_End(Record->Frame, ProgramTime_Create(0.0, 0.0));
            if (EndResult.Code != ErrorCode_Success)
            {
                if (FirstError.Code == ErrorCode_Success)
                {
                    FirstError = EndResult;
                }
                else
                {
                    Error_Deconstruct(&EndResult);
                }
            }
        }

        DestroyRecord(Record);
    }

    Memory_Free(self->_records._data);
    Memory_Zero(&self->_records, sizeof(self->_records));
    Memory_Free(self->_updateOrder._data);
    Memory_Zero(&self->_updateOrder, sizeof(self->_updateOrder));

    return FirstError;
}

Error GameFrameManager_AddFrame(GameFrameManager* self, GameFrame* frame, GameFrameAddOptions options)
{
    if ((self == NULL) || (frame == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrameManager_AddFrame: self and frame must not be NULL.");
    }
    if (FindRecord(self, frame, NULL) != NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "GameFrameManager_AddFrame: frame is already managed.");
    }

    GameFrameRecord* Record = Memory_Allocate(sizeof(GameFrameRecord));
    Memory_Zero(Record, sizeof(*Record));
    Record->Frame = frame;
    Record->State = GameFrameState_Loading;
    Record->ZOrder = ComputeZOrder(self, options.AddToTop);
    Record->StartRequested = options.StartAutomatically;
    Record->RemovalRequested = false;
    Record->HasTarget = false;

    // The list stays in ascending z: a new top frame goes last, a new bottom frame first.
    bool IsStored = options.AddToTop ? GenericBuffer_AddLast(&self->_records, &Record)
        : GenericBuffer_Insert(&self->_records, &Record, 0U);
    if (!IsStored)
    {
        Memory_Free(Record);
        return Error_Construct2(ErrorCode_BufferTooSmall, "GameFrameManager_AddFrame: could not store the frame record.");
    }

    Error BeginLoadResult = GameFrame_BeginLoad(frame);
    if (BeginLoadResult.Code != ErrorCode_Success)
    {
        // Roll back the add; the caller retains ownership of the frame on failure (do not destroy it).
        size_t Index = 0;
        if (FindRecord(self, frame, &Index) != NULL)
        {
            GenericBuffer_RemoveAt(&self->_records, Index);
        }
        Memory_Free(Record);
        return BeginLoadResult;
    }

    return Error_CreateSuccess();
}

Error GameFrameManager_RemoveFrame(GameFrameManager* self, GameFrame* frame)
{
    if ((self == NULL) || (frame == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrameManager_RemoveFrame: self and frame must not be NULL.");
    }

    GameFrameRecord* Record = FindRecord(self, frame, NULL);
    if (Record == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "GameFrameManager_RemoveFrame: frame is not managed by this manager.");
    }

    if ((Record->State != GameFrameState_Unloading) && (Record->State != GameFrameState_Unloaded))
    {
        Record->RemovalRequested = true;
    }
    return Error_CreateSuccess();
}

Error GameFrameManager_ActivateFrame(GameFrameManager* self, GameFrame* frame)
{
    if ((self == NULL) || (frame == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrameManager_ActivateFrame: self and frame must not be NULL.");
    }

    GameFrameRecord* Record = FindRecord(self, frame, NULL);
    if (Record == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "GameFrameManager_ActivateFrame: frame is not managed by this manager.");
    }

    Record->StartRequested = true;
    return Error_CreateSuccess();
}

Error GameFrameManager_Update(GameFrameManager* self, ProgramTime time)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrameManager_Update: self must not be NULL.");
    }

    // Poll input once here, before any frame's update, so frames read fresh input this tick. This is the
    // program's ONLY input poll: raylib is built with SUPPORT_CUSTOM_FRAME_CONTROL (see BUILDING.md), so
    // EndDrawing does not poll (nor swap; GameFrameManager_Render swaps manually). Polling per update tick,
    // not per render, is what keeps edge-triggered input reliable in frame updates. The tick's GameInput snapshot
    // is taken right after.
    PollInputEvents();
    if (self->_input != NULL)
    {
        GameInput_Poll(self->_input);
    }

    // Top-down (the list is in ascending z), so the frame on top gets first refusal on input. The order is fixed for
    // the tick: a frame added during it waits for the next tick, and only this loop destroys records.
    GenericBuffer_Clear(&self->_updateOrder);
    if ((self->_records._count > 0U)
        && !GenericBuffer_AddLastRange(&self->_updateOrder, self->_records._data, self->_records._count))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "GameFrameManager_Update: could not order the frames.");
    }
    for (size_t i = self->_updateOrder._count; i-- > 0U;)
    {
        GameFrameRecord* Record = *(GameFrameRecord**)GenericBuffer_GetPointerToElement(&self->_updateOrder, i);
        Error StepResult = StepRecord(self, Record, time);
        if (StepResult.Code != ErrorCode_Success)
        {
            return StepResult;
        }
    }
    return Error_CreateSuccess();
}

Error GameFrameManager_PrepareRender(GameFrameManager* self, ProgramTime time)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrameManager_PrepareRender: self must not be NULL.");
    }

    // Top-down, as the update, over the same kind of snapshot: a frame added meanwhile waits for the next render, and
    // records are destroyed only by the update, so the snapshot's pointers stay valid.
    GenericBuffer_Clear(&self->_updateOrder);
    if ((self->_records._count > 0U)
        && !GenericBuffer_AddLastRange(&self->_updateOrder, self->_records._data, self->_records._count))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "GameFrameManager_PrepareRender: could not order the frames.");
    }
    for (size_t i = self->_updateOrder._count; i-- > 0U;)
    {
        GameFrameRecord* Record = *(GameFrameRecord**)GenericBuffer_GetPointerToElement(&self->_updateOrder, i);
        if ((Record->State != GameFrameState_Active) || !Record->Frame->IsUpdated || !Record->Frame->IsRendered)
        {
            continue;
        }
        Error PrepareResult = GameFrame_PrepareRender(Record->Frame, time);
        if (PrepareResult.Code != ErrorCode_Success)
        {
            return FailFatally(self, Record->Frame, "prepare render", PrepareResult);
        }
    }
    return Error_CreateSuccess();
}

Error GameFrameManager_Render(GameFrameManager* self, ProgramTime time)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrameManager_Render: self must not be NULL.");
    }

    int32_t Width = (int32_t)GetScreenWidth();
    int32_t Height = (int32_t)GetScreenHeight();
    if ((Width != self->_targetWidth) || (Height != self->_targetHeight))
    {
        Error ResizeResult = HandleResize(self, Width, Height);
        if (ResizeResult.Code != ErrorCode_Success)
        {
            return ResizeResult;
        }
    }

    // Draw and composite bottom-to-top (the record list is kept in ascending z).
    // Pass 1: each active, rendered frame draws into its own target.
    FrameRenderContext Context =
    {
        .Time = time,
        .TargetAspectRatio = self->_targetAspectRatio,
        .TargetAreaPosition = self->_targetAreaPosition
    };
    for (size_t i = 0; i < self->_records._count; i++)
    {
        GameFrameRecord* Record = RecordAt(self, i);
        if ((Record == NULL) || (Record->State != GameFrameState_Active) || !Record->Frame->IsRendered)
        {
            continue;
        }
        if (!Record->HasTarget)
        {
            Record->Target = LoadRenderTexture((int)Width, (int)Height);
            Record->HasTarget = true;
        }
        Error RenderResult = GameFrame_Render(Record->Frame, &Context, Record->Target);
        if (RenderResult.Code != ErrorCode_Success)
        {
            return FailFatally(self, Record->Frame, "render", RenderResult);
        }
    }

    // Pass 2: composite every rendered target onto the backbuffer. This is a straight 1:1 blit (with the
    // vertical flip Raylib render textures need), tinted by the frame's CompositeColor, so it deliberately
    // uses Raylib directly rather than the aspect-fitting renderer path.
    // TODO: plain alpha blending, here and when a frame draws into its transparent target, thins the antialiased edges
    // of overlay frames (UI over the world); rendering premultiplied and compositing with (ONE, ONE_MINUS_SRC_ALPHA)
    // fixes it.
    BeginDrawing();
    ClearBackground(BLACK);
    for (size_t i = 0; i < self->_records._count; i++)
    {
        GameFrameRecord* Record = RecordAt(self, i);
        if ((Record == NULL) || (Record->State != GameFrameState_Active)
            || !Record->Frame->IsRendered || !Record->HasTarget)
        {
            continue;
        }

        Color Tint = RenderColor_GetFinalColor(Record->Frame->CompositeColor);
        Rectangle Source = { 0.0f, 0.0f, (float)Record->Target.texture.width, -(float)Record->Target.texture.height };
        Rectangle Destination = { 0.0f, 0.0f, (float)Width, (float)Height };
        DrawTexturePro(Record->Target.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, Tint);
    }
    EndDrawing();
    // Required: raylib is built with SUPPORT_CUSTOM_FRAME_CONTROL (see BUILDING.md), so EndDrawing does not
    // swap buffers. Without this call the screen never updates; with a stock raylib it would double-swap.
    SwapScreenBuffer();

    return Error_CreateSuccess();
}

bool GameFrameManager_ShouldStop(GameFrameManager* self)
{
    return (self == NULL) ? true : self->_shouldStop;
}

void GameFrameManager_RequestStop(GameFrameManager* self)
{
    if (self != NULL)
    {
        self->_shouldStop = true;
    }
}

size_t GameFrameManager_GetFrameCount(GameFrameManager* self)
{
    return (self == NULL) ? 0 : self->_records._count;
}

bool GameFrameManager_TryGetFrameState(GameFrameManager* self, GameFrame* frame, GameFrameState* outState)
{
    if ((self == NULL) || (frame == NULL) || (outState == NULL))
    {
        return false;
    }

    GameFrameRecord* Record = FindRecord(self, frame, NULL);
    if (Record == NULL)
    {
        return false;
    }

    *outState = Record->State;
    return true;
}
