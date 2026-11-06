#include "GameWindowState.h"
#include "PropertyDescriptor.h"
#include "wr/WRFileSystem.h"
#include "wr/WRFileStream.h"
#include "wr/WRIO.h"
#include "wr/WRJSON.h"
#include "wr/WRString.h"


// Macros.
/** The parsed tree a state file may take: a small file of a few keys is far below it. */
#define STATE_MAX_TREE_BYTES ((size_t)65536)
/** What a state file's temporary copy adds to its name. */
#define STATE_TEMPORARY_SUFFIX ((const unsigned char*)u8".tmp")
/** The size of a state before anything is known about the monitors. */
#define STATE_DEFAULT_WIDTH 1280
#define STATE_DEFAULT_HEIGHT 720


// Fields.
static const PropertyEnumEntry ModeEntries[] =
{
    { .Name = u8"windowed", .Value = GameWindowMode_Windowed },
    { .Name = u8"borderless", .Value = GameWindowMode_Borderless },
    { .Name = u8"fullscreen", .Value = GameWindowMode_Fullscreen },
};

static const PropertyEnumTable ModeEnum =
{
    .Name = u8"window_mode", .Entries = ModeEntries, .EntryCount = sizeof(ModeEntries) / sizeof(ModeEntries[0])
};

/* No bounds on the numbers: a size past the limits is clamped after the read and a position is judged against the monitors,
 * so only a value of the wrong type refuses the file. */
static const PropertyDescriptor StateProperties[] =
{
    {
        .Key = u8"mode", .WireId = 1U, .Type = PropertyType_Enum, PROPERTY_MEMBER(GameWindowState, Mode),
        .Default.EnumValue = GameWindowMode_Windowed, .EnumTable = &ModeEnum, .Flags = PropertyFlags_Persistent,
        .DisplayName = u8"Mode", .Description = u8"How the window takes the screen.", .SinceVersion = 1U
    },
    {
        .Key = u8"width", .WireId = 2U, .Type = PropertyType_Int32, PROPERTY_MEMBER(GameWindowState, Width),
        .Default.Int32Value = STATE_DEFAULT_WIDTH, .Flags = PropertyFlags_Persistent, .DisplayName = u8"Width",
        .Description = u8"The windowed width in pixels.", .SinceVersion = 1U
    },
    {
        .Key = u8"height", .WireId = 3U, .Type = PropertyType_Int32, PROPERTY_MEMBER(GameWindowState, Height),
        .Default.Int32Value = STATE_DEFAULT_HEIGHT, .Flags = PropertyFlags_Persistent, .DisplayName = u8"Height",
        .Description = u8"The windowed height in pixels.", .SinceVersion = 1U
    },
    {
        .Key = u8"has_position", .WireId = 4U, .Type = PropertyType_Bool, PROPERTY_MEMBER(GameWindowState, HasPosition),
        .Default.BoolValue = false, .Flags = PropertyFlags_Persistent, .DisplayName = u8"Has a position",
        .Description = u8"Whether x and y hold a place; without one the window is centred.", .SinceVersion = 1U
    },
    {
        .Key = u8"x", .WireId = 5U, .Type = PropertyType_Int32, PROPERTY_MEMBER(GameWindowState, X),
        .Default.Int32Value = 0, .Flags = PropertyFlags_Persistent, .DisplayName = u8"X",
        .Description = u8"Where the window's content area starts on the desktop, in pixels.", .SinceVersion = 1U
    },
    {
        .Key = u8"y", .WireId = 6U, .Type = PropertyType_Int32, PROPERTY_MEMBER(GameWindowState, Y),
        .Default.Int32Value = 0, .Flags = PropertyFlags_Persistent, .DisplayName = u8"Y",
        .Description = u8"Where the window's content area starts on the desktop, in pixels.", .SinceVersion = 1U
    },
    {
        .Key = u8"is_maximized", .WireId = 7U, .Type = PropertyType_Bool, PROPERTY_MEMBER(GameWindowState, IsMaximized),
        .Default.BoolValue = false, .Flags = PropertyFlags_Persistent, .DisplayName = u8"Maximized",
        .Description = u8"Whether the window was maximized while windowed.", .SinceVersion = 1U
    },
};

static const PropertyTable StateTable =
{
    .Name = u8"window_state", .Version = 1U, .StructSize = sizeof(GameWindowState), .Descriptors = StateProperties,
    .DescriptorCount = sizeof(StateProperties) / sizeof(StateProperties[0]), .RetiredWireIds = NULL, .RetiredWireIdCount = 0U,
    .AfterSet = NULL, .Migrate = NULL, .MigrateGHDF = NULL
};


// Static functions.
static int32_t ClampLength(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    return (value > maximum) ? maximum : value;
}

/* Whether a monitor has a size to speak of. */
static bool IsMonitorUsable(const GameWindowMonitor* monitor)
{
    return (monitor->Width > 0) && (monitor->Height > 0);
}

/* Whether enough of a window's top lies on a monitor for its title bar to be reached. In 64 bits: a hostile position is any 32-bit number, and
 * a monitor's edge or the window's width added to it must not wrap. */
static bool IsReachableOn(const GameWindowState* state, const GameWindowMonitor* monitor)
{
    int64_t Top = (int64_t)state->Y;
    int64_t MonitorTop = (int64_t)monitor->Y;
    if ((Top < (MonitorTop + (int64_t)GAME_WINDOW_MIN_TITLE_BAR_ROOM))
        || (Top > (MonitorTop + (int64_t)monitor->Height - (int64_t)GAME_WINDOW_MIN_VISIBLE_HEIGHT)))
    {
        return false;
    }
    int64_t Left = (int64_t)state->X;
    int64_t MonitorLeft = (int64_t)monitor->X;
    int64_t OverlapStart = (Left > MonitorLeft) ? Left : MonitorLeft;
    int64_t WindowEnd = Left + (int64_t)state->Width;
    int64_t MonitorEnd = MonitorLeft + (int64_t)monitor->Width;
    int64_t OverlapEnd = (WindowEnd < MonitorEnd) ? WindowEnd : MonitorEnd;
    return (OverlapEnd - OverlapStart) >= (int64_t)GAME_WINDOW_MIN_VISIBLE_WIDTH;
}

static int32_t KeepWithin(int32_t length, int64_t limit)
{
    return ((int64_t)length > limit) ? (int32_t)limit : length;
}

/* What is seen becomes what is remembered. */
static void Remember(GameWindowState* state, const GameWindowObservation* seen)
{
    state->Width = seen->Width;
    state->Height = seen->Height;
    state->X = seen->X;
    state->Y = seen->Y;
    state->HasPosition = true;
}


// Public functions: modes.
bool GameWindowMode_TryParse(const unsigned char* text, GameWindowMode* outMode)
{
    if (text == NULL)
    {
        return false;
    }
    for (size_t Index = 0U; Index < ModeEnum.EntryCount; Index++)
    {
        bool IsSame = false;
        Error Compare = StringUTF8_EqualsExact(text, ModeEntries[Index].Name, &IsSame);
        Error_Deconstruct(&Compare);
        if (IsSame)
        {
            *outMode = (GameWindowMode)ModeEntries[Index].Value;
            return true;
        }
    }
    return false;
}

const unsigned char* GameWindowMode_GetName(GameWindowMode mode)
{
    switch (mode)
    {
        case GameWindowMode_Windowed:
        case GameWindowMode_Borderless:
        case GameWindowMode_Fullscreen:
            return ModeEntries[(size_t)mode].Name;
    }
    return (const unsigned char*)u8"unknown";
}

GameWindowMode GameWindowMode_GetToggleTarget(GameWindowMode current, GameWindowMode toggleMode)
{
    if (current != GameWindowMode_Windowed)
    {
        return GameWindowMode_Windowed;
    }
    return (toggleMode == GameWindowMode_Fullscreen) ? GameWindowMode_Fullscreen : GameWindowMode_Borderless;
}


// Public functions: the state.
const struct PropertyTableStruct* GameWindowState_GetPropertyTable(void)
{
    return &StateTable;
}

void GameWindowState_SetDefaults(GameWindowState* self)
{
    Error Result = Properties_Construct(&StateTable, self);
    Error_Deconstruct(&Result); // Only fails for a NULL argument or a table that fails its check, which a test guards.
}

void GameWindowState_ClampSize(GameWindowState* self)
{
    self->Width = ClampLength(self->Width, GAME_WINDOW_MIN_WIDTH, GAME_WINDOW_MAX_LENGTH);
    self->Height = ClampLength(self->Height, GAME_WINDOW_MIN_HEIGHT, GAME_WINDOW_MAX_LENGTH);
}

void GameWindowState_FitToMonitors(GameWindowState* self, const GameWindowMonitor* monitors, size_t monitorCount)
{
    const GameWindowMonitor* First = NULL;
    int64_t Left = 0;
    int64_t Top = 0;
    int64_t Right = 0;
    int64_t Bottom = 0;
    for (size_t Index = 0U; Index < monitorCount; Index++)
    {
        const GameWindowMonitor* Monitor = &monitors[Index];
        if (!IsMonitorUsable(Monitor))
        {
            continue;
        }
        int64_t MonitorRight = (int64_t)Monitor->X + (int64_t)Monitor->Width;
        int64_t MonitorBottom = (int64_t)Monitor->Y + (int64_t)Monitor->Height;
        Left = ((First == NULL) || ((int64_t)Monitor->X < Left)) ? (int64_t)Monitor->X : Left;
        Top = ((First == NULL) || ((int64_t)Monitor->Y < Top)) ? (int64_t)Monitor->Y : Top;
        Right = ((First == NULL) || (MonitorRight > Right)) ? MonitorRight : Right;
        Bottom = ((First == NULL) || (MonitorBottom > Bottom)) ? MonitorBottom : Bottom;
        First = (First == NULL) ? Monitor : First;
    }
    if (First == NULL)
    {
        self->HasPosition = false;
        GameWindowState_ClampSize(self);
        return;
    }

    // A window may span the monitors, so only the whole desktop bounds it. Its place is judged at that size: what shrinks may leave a monitor.
    self->Width = KeepWithin(self->Width, Right - Left);
    self->Height = KeepWithin(self->Height, Bottom - Top);
    bool IsReachable = false;
    for (size_t Index = 0U; self->HasPosition && !IsReachable && (Index < monitorCount); Index++)
    {
        IsReachable = IsReachableOn(self, &monitors[Index]); // A monitor of no size holds nothing.
    }
    self->HasPosition = IsReachable;

    // With no place it opens on the first monitor, so that one bounds it.
    if (!self->HasPosition)
    {
        self->Width = KeepWithin(self->Width, (int64_t)First->Width);
        self->Height = KeepWithin(self->Height, (int64_t)First->Height);
    }
    GameWindowState_ClampSize(self);
}

void GameWindowState_GetCentredPosition(const GameWindowState* self, const GameWindowMonitor* monitor, int32_t* outX, int32_t* outY)
{
    int64_t X = (int64_t)monitor->X + (((int64_t)monitor->Width - (int64_t)self->Width) / 2);
    int64_t Y = (int64_t)monitor->Y + (((int64_t)monitor->Height - (int64_t)self->Height) / 2);
    int64_t LowestY = (int64_t)monitor->Y + (int64_t)GAME_WINDOW_MIN_TITLE_BAR_ROOM;
    *outX = (int32_t)X;
    *outY = (int32_t)((Y < LowestY) ? LowestY : Y);
}

Error GameWindowState_ReadJSON(const unsigned char* bytes, size_t byteCount, GameWindowState* outState)
{
    if (((bytes == NULL) && (byteCount > 0U)) || (outState == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameWindowState_ReadJSON: the bytes and the state are required.");
    }
    if (byteCount > GAME_WINDOW_STATE_MAX_BYTES)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "The window state file is too long.");
    }

    JSONObjectPool* Pool = NULL;
    Error Result = JSONObjectPool_Create(&Pool);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // A read-only view: the parser never writes through it, but GenericBuffer's storage pointer is not const.
    GenericBuffer Source;
    GenericBuffer_CreateReadOnly(&Source, (void*)bytes, byteCount, sizeof(unsigned char), byteCount);
    JSONObjectValue Root = { .Type = JSONValueType_None };
    JSONReadLimits Limits = { .MaxTreeByteCount = STATE_MAX_TREE_BYTES };
    Result = JSON_DeserializeWithLimits(Pool, &Source, &Limits, &Root);
    if ((Result.Code == ErrorCode_Success) && (Root.Type != JSONValueType_Compound))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "A window state file must be a JSON object.");
    }
    GameWindowState Read;
    GameWindowState_SetDefaults(&Read);
    if (Result.Code == ErrorCode_Success)
    {
        PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
        Options.UnknownKeyMode = PropertyUnknownKeyMode_Ignore;
        Result = Properties_ReadJSON(&StateTable, Root.Value.Compound, &Read, &Options);
    }
    if (Result.Code == ErrorCode_Success)
    {
        GameWindowState_ClampSize(&Read);
        *outState = Read;
    }
    Error CloseResult = JSONObjectPool_Deconstruct(Pool);
    if (Result.Code == ErrorCode_Success)
    {
        return CloseResult;
    }
    Error_Deconstruct(&CloseResult);
    return Result;
}

Error GameWindowState_WriteJSON(const GameWindowState* self, GenericBuffer* outBytes)
{
    if ((self == NULL) || (outBytes == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameWindowState_WriteJSON: the state and the buffer are required.");
    }
    JSONObjectPool* Pool = NULL;
    Error Result = JSONObjectPool_Create(&Pool);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    JSONCompound* Root = NULL;
    Result = JSONObjectPool_BorrowCompound(Pool, &Root);
    if (Result.Code == ErrorCode_Success)
    {
        PropertyWriteOptions Options = PropertyWriteOptions_CreateDefault();
        Options.AreDefaultsWritten = true;
        Result = Properties_WriteJSON(&StateTable, self, Pool, Root, &Options);
    }
    if (Result.Code == ErrorCode_Success)
    {
        JSONObjectValue RootValue = { .Type = JSONValueType_Compound, .Value.Compound = Root };
        Result = JSON_Serialize(&RootValue, outBytes, JSONSerializeFlags_PrettyPrinted);
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AppendByte(outBytes, (unsigned char)'\n'))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not hold the window state file.");
    }
    Error CloseResult = JSONObjectPool_Deconstruct(Pool);
    if (Result.Code == ErrorCode_Success)
    {
        return CloseResult;
    }
    Error_Deconstruct(&CloseResult);
    return Result;
}

Error GameWindowState_LoadFile(const unsigned char* path, GameWindowState* outState)
{
    if ((path == NULL) || (outState == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameWindowState_LoadFile: the path and the state are required.");
    }
    FileStream Stream;
    Error Result = FileSystem_OpenFileStream(path, FileOpenMode_ReadBinary, &Stream);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    size_t Size = 0U;
    Result = IOStream_GetStreamSizeTotal(FileStream_AsIOStream(&Stream), &Size);
    if ((Result.Code == ErrorCode_Success) && (Size > GAME_WINDOW_STATE_MAX_BYTES))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "The window state file is too long.");
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_TryPrepareForManualMutation(&Bytes, Size))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not hold the window state file.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = IOStream_ReadAll(FileStream_AsIOStream(&Stream), &Bytes);
    }
    Error CloseResult = FileStream_Deconstruct(&Stream);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CloseResult;
    }
    else
    {
        Error_Deconstruct(&CloseResult);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameWindowState_ReadJSON(Bytes._data, Bytes._count, outState);
    }
    Memory_Free(Bytes._data);
    return Result;
}

Error GameWindowState_SaveFile(const GameWindowState* self, const unsigned char* path)
{
    if ((self == NULL) || (path == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameWindowState_SaveFile: the state and the path are required.");
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer Temporary;
    GenericBuffer_AllocateVariable(&Temporary, 0U, sizeof(unsigned char));
    Error Result = GameWindowState_WriteJSON(self, &Bytes);
    if ((Result.Code == ErrorCode_Success) && (!GenericBuffer_AppendString(&Temporary, path)
        || !GenericBuffer_AppendString(&Temporary, STATE_TEMPORARY_SUFFIX) || !GenericBuffer_NullTerminate(&Temporary)))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not build the window state file's temporary path.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_WriteAllBytes(Temporary._data, Bytes._data, Bytes._count);
        if (Result.Code == ErrorCode_Success)
        {
            Result = FileSystem_ReplaceEntry(Temporary._data, path);
        }
        if (Result.Code != ErrorCode_Success)
        {
            // Best effort: the file itself is unchanged either way.
            Error DeleteResult = FileSystem_DeleteEntry(Temporary._data);
            Error_Deconstruct(&DeleteResult);
        }
    }
    Memory_Free(Temporary._data);
    Memory_Free(Bytes._data);
    return Result;
}


// Public functions: the watch.
void GameWindowWatch_Construct(GameWindowWatch* self)
{
    Memory_Zero(self, sizeof(*self));
}

void GameWindowWatch_Hold(GameWindowWatch* self, double timeSeconds)
{
    self->_hasPending = false;
    self->_heldUntil = timeSeconds + GAME_WINDOW_SETTLE_SECONDS;
}

void GameWindowWatch_Observe(GameWindowWatch* self, GameWindowState* state, const GameWindowObservation* seen, double timeSeconds)
{
    if ((timeSeconds < self->_heldUntil) || seen->IsMinimized || (seen->Width <= 0) || (seen->Height <= 0))
    {
        self->_hasPending = false;
        return;
    }
    state->IsMaximized = seen->IsMaximized;
    if (seen->IsMaximized)
    {
        self->_hasPending = false;
        return;
    }
    bool IsSameAsPending = self->_hasPending && (seen->Width == self->_pending.Width) && (seen->Height == self->_pending.Height)
        && (seen->X == self->_pending.X) && (seen->Y == self->_pending.Y);
    if (!IsSameAsPending)
    {
        self->_pending = *seen;
        self->_pendingSince = timeSeconds;
        self->_hasPending = true;
    }
    if ((timeSeconds - self->_pendingSince) >= GAME_WINDOW_SETTLE_SECONDS)
    {
        Remember(state, &self->_pending);
    }
}

void GameWindowWatch_Settle(GameWindowWatch* self, GameWindowState* state)
{
    if (self->_hasPending)
    {
        Remember(state, &self->_pending);
    }
}
