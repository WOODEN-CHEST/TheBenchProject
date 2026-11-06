#include "TestSuites.h"
#include "GameWindowState.h"
#include "PropertyDescriptor.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <stdio.h>
#include <string.h>


// Macros.
/** Initial capacity (bytes) of path and text buffers; they grow as needed. */
#define STATE_TEST_BUFFER_CAPACITY ((size_t)256)
/** A tick of the game's update, in seconds. */
#define STATE_TEST_TICK (1.0 / 480.0)


// Static functions.
static Error ReadText(const char* text, GameWindowState* outState)
{
    return GameWindowState_ReadJSON((const unsigned char*)text, strlen(text), outState);
}

static GameWindowState MakeSentinelState(void)
{
    return (GameWindowState){ .Mode = GameWindowMode_Fullscreen, .Width = 1234, .Height = 567, .HasPosition = true, .X = 89, .Y = 10,
        .IsMaximized = true };
}

static bool AreStatesEqual(const GameWindowState* a, const GameWindowState* b)
{
    return (a->Mode == b->Mode) && (a->Width == b->Width) && (a->Height == b->Height) && (a->HasPosition == b->HasPosition)
        && (a->X == b->X) && (a->Y == b->Y) && (a->IsMaximized == b->IsMaximized);
}

static GameWindowObservation MakeSeen(int32_t width, int32_t height, int32_t x, int32_t y)
{
    return (GameWindowObservation){ .Width = width, .Height = height, .X = x, .Y = y, .IsMaximized = false, .IsMinimized = false };
}

/* A state of a 1000 x 500 window at a place, to fit. */
static GameWindowState MakePlacedState(int32_t x, int32_t y)
{
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    State.Width = 1000;
    State.Height = 500;
    State.HasPosition = true;
    State.X = x;
    State.Y = y;
    return State;
}


// Tests: modes.
static void ModesReadAndWriteByName(TestRun* run)
{
    GameWindowMode Mode = GameWindowMode_Windowed;
    TEST_CHECK(run, GameWindowMode_TryParse(u8"borderless", &Mode) && (Mode == GameWindowMode_Borderless));
    TEST_CHECK(run, GameWindowMode_TryParse(u8"fullscreen", &Mode) && (Mode == GameWindowMode_Fullscreen));
    TEST_CHECK(run, GameWindowMode_TryParse(u8"windowed", &Mode) && (Mode == GameWindowMode_Windowed));
    Mode = GameWindowMode_Borderless;
    TEST_CHECK(run, !GameWindowMode_TryParse(u8"Windowed", &Mode) && (Mode == GameWindowMode_Borderless));
    TEST_CHECK(run, !GameWindowMode_TryParse(u8"", &Mode) && (Mode == GameWindowMode_Borderless));
    TEST_CHECK(run, !GameWindowMode_TryParse(NULL, &Mode) && (Mode == GameWindowMode_Borderless));
    TEST_CHECK(run, strcmp((const char*)GameWindowMode_GetName(GameWindowMode_Windowed), "windowed") == 0);
    TEST_CHECK(run, strcmp((const char*)GameWindowMode_GetName(GameWindowMode_Borderless), "borderless") == 0);
    TEST_CHECK(run, strcmp((const char*)GameWindowMode_GetName(GameWindowMode_Fullscreen), "fullscreen") == 0);
    TEST_CHECK(run, strcmp((const char*)GameWindowMode_GetName((GameWindowMode)77), "unknown") == 0);
}

static void TheToggleGoesToItsModeAndBack(TestRun* run)
{
    TEST_CHECK(run, GameWindowMode_GetToggleTarget(GameWindowMode_Windowed, GameWindowMode_Borderless) == GameWindowMode_Borderless);
    TEST_CHECK(run, GameWindowMode_GetToggleTarget(GameWindowMode_Windowed, GameWindowMode_Fullscreen) == GameWindowMode_Fullscreen);
    TEST_CHECK(run, GameWindowMode_GetToggleTarget(GameWindowMode_Borderless, GameWindowMode_Borderless) == GameWindowMode_Windowed);
    TEST_CHECK(run, GameWindowMode_GetToggleTarget(GameWindowMode_Fullscreen, GameWindowMode_Fullscreen) == GameWindowMode_Windowed);
    // Out of whichever whole-screen mode it is in, whatever the toggle's mode says.
    TEST_CHECK(run, GameWindowMode_GetToggleTarget(GameWindowMode_Fullscreen, GameWindowMode_Borderless) == GameWindowMode_Windowed);
    TEST_CHECK(run, GameWindowMode_GetToggleTarget(GameWindowMode_Borderless, GameWindowMode_Fullscreen) == GameWindowMode_Windowed);
    // A toggle's mode of a window would do nothing.
    TEST_CHECK(run, GameWindowMode_GetToggleTarget(GameWindowMode_Windowed, GameWindowMode_Windowed) == GameWindowMode_Borderless);
}


// Tests: the file's text.
static void TheDefaultsAreAPlainWindow(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(GameWindowState_GetPropertyTable()));
    GameWindowState State = MakeSentinelState();
    GameWindowState_SetDefaults(&State);
    TEST_CHECK(run, (State.Mode == GameWindowMode_Windowed) && (State.Width == 1280) && (State.Height == 720));
    TEST_CHECK(run, !State.HasPosition && !State.IsMaximized);
}

static void EveryFieldIsRead(TestRun* run)
{
    GameWindowState State;
    TEST_CHECK_SUCCESS(run, ReadText("{\"mode\": \"fullscreen\", \"width\": 1500, \"height\": 900, \"has_position\": true, "
        "\"x\": -200, \"y\": 75, \"is_maximized\": true}", &State));
    TEST_CHECK(run, (State.Mode == GameWindowMode_Fullscreen) && (State.Width == 1500) && (State.Height == 900));
    TEST_CHECK(run, State.HasPosition && (State.X == -200) && (State.Y == 75) && State.IsMaximized);
    TEST_CHECK_SUCCESS(run, ReadText("{\"mode\": \"borderless\"}", &State));
    TEST_CHECK(run, State.Mode == GameWindowMode_Borderless);
    TEST_CHECK_SUCCESS(run, ReadText("{\"mode\": \"windowed\"}", &State));
    TEST_CHECK(run, State.Mode == GameWindowMode_Windowed);
}

// A key left out takes its default, and one this version does not know is skipped.
static void MissingAndUnknownKeysAreForgiven(TestRun* run)
{
    GameWindowState State = MakeSentinelState();
    TEST_CHECK_SUCCESS(run, ReadText("{}", &State));
    TEST_CHECK(run, (State.Mode == GameWindowMode_Windowed) && (State.Width == 1280) && (State.Height == 720));
    TEST_CHECK(run, !State.HasPosition && !State.IsMaximized);
    TEST_CHECK_SUCCESS(run, ReadText("{\"width\": 800, \"from_a_later_version\": {\"a\": [1, 2]}}", &State));
    TEST_CHECK(run, (State.Width == 800) && (State.Height == 720));
}

// Absurd sizes come back within the limits, with no file refused.
static void AbsurdSizesAreClamped(TestRun* run)
{
    static const struct
    {
        const char* Text;
        int32_t Width;
        int32_t Height;
    } Cases[] =
    {
        { "{\"width\": 0, \"height\": 0}", GAME_WINDOW_MIN_WIDTH, GAME_WINDOW_MIN_HEIGHT },
        { "{\"width\": -5, \"height\": -2147483648}", GAME_WINDOW_MIN_WIDTH, GAME_WINDOW_MIN_HEIGHT },
        { "{\"width\": 639, \"height\": 359}", GAME_WINDOW_MIN_WIDTH, GAME_WINDOW_MIN_HEIGHT },
        { "{\"width\": 640, \"height\": 360}", 640, 360 },
        { "{\"width\": 16384, \"height\": 16384}", 16384, 16384 },
        { "{\"width\": 16385, \"height\": 2147483647}", GAME_WINDOW_MAX_LENGTH, GAME_WINDOW_MAX_LENGTH },
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        GameWindowState State = MakeSentinelState();
        TEST_CHECK_SUCCESS(run, ReadText(Cases[Index].Text, &State));
        TEST_CHECK(run, (State.Width == Cases[Index].Width) && (State.Height == Cases[Index].Height));
    }
}

// A bad file leaves what the caller had, whole: truncated, of the wrong types, or too much.
static void BadFilesChangeNothing(TestRun* run)
{
    static const char* const Bad[] =
    {
        "",
        "   ",
        "{",
        "{\"mode\": \"windowed\"",
        "{\"width\": 800,",
        "[]",
        "[1, 2, 3]",
        "12",
        "null",
        "\"windowed\"",
        "{\"mode\": \"maximized\"}",
        "{\"mode\": 2}",
        "{\"mode\": null}",
        "{\"width\": \"wide\"}",
        "{\"width\": [800]}",
        "{\"width\": {\"value\": 800}}",
        "{\"width\": true}",
        "{\"width\": 800.5}",
        "{\"width\": 1e30}",
        "{\"height\": 2147483648}",
        "{\"height\": -2147483649}",
        "{\"x\": 2147483648}",
        "{\"y\": \"up\"}",
        "{\"has_position\": 1}",
        "{\"is_maximized\": \"yes\"}",
        "{\"width\": 800} trailing",
        "\xff\xfe{\"width\": 800}",
    };
    for (size_t Index = 0U; Index < (sizeof(Bad) / sizeof(Bad[0])); Index++)
    {
        GameWindowState State = MakeSentinelState();
        GameWindowState Expected = State;
        Error Result = ReadText(Bad[Index], &State);
        TEST_CHECK(run, Result.Code == ErrorCode_InvalidJSON);
        if (Result.Code != ErrorCode_InvalidJSON)
        {
            printf("    read as %d: %s\n", (int)Result.Code, Bad[Index]);
        }
        Error_Deconstruct(&Result);
        TEST_CHECK(run, AreStatesEqual(&State, &Expected));
    }
    GameWindowState State = MakeSentinelState();
    TEST_CHECK_ERROR(run, GameWindowState_ReadJSON(NULL, 4U, &State), ErrorCode_IllegalArgument);
}

// A long or deep file is refused for what it is, before it costs anything.
static void LongAndDeepFilesAreRefused(TestRun* run)
{
    // Long: a valid object past the limit.
    size_t Length = GAME_WINDOW_STATE_MAX_BYTES + 64U;
    char* Long = Memory_Allocate(Length + 1U);
    Memory_Set(Long, ' ', Length);
    Long[0] = '{';
    Long[Length - 1U] = '}';
    Long[Length] = 0;
    GameWindowState State = MakeSentinelState();
    GameWindowState Expected = State;
    TEST_CHECK_ERROR(run, GameWindowState_ReadJSON((const unsigned char*)Long, Length, &State), ErrorCode_InvalidJSON);
    TEST_CHECK(run, AreStatesEqual(&State, &Expected));
    Memory_Free(Long);

    // The longest allowed reads.
    Length = GAME_WINDOW_STATE_MAX_BYTES;
    Long = Memory_Allocate(Length);
    Memory_Set(Long, ' ', Length);
    Long[0] = '{';
    Long[Length - 1U] = '}';
    State = MakeSentinelState();
    TEST_CHECK_SUCCESS(run, GameWindowState_ReadJSON((const unsigned char*)Long, Length, &State));
    TEST_CHECK(run, State.Mode == GameWindowMode_Windowed);
    Memory_Free(Long);

    // Deep: past the parser's nesting limit, within the length.
    char Deep[GAME_WINDOW_STATE_MAX_BYTES];
    Memory_Set(Deep, '[', sizeof(Deep));
    State = MakeSentinelState();
    Expected = State;
    TEST_CHECK_ERROR(run, GameWindowState_ReadJSON((const unsigned char*)Deep, sizeof(Deep), &State), ErrorCode_InvalidJSON);
    TEST_CHECK(run, AreStatesEqual(&State, &Expected));
}

static void ARoundTripKeepsEveryField(TestRun* run)
{
    const GameWindowState Cases[] =
    {
        { .Mode = GameWindowMode_Windowed, .Width = 1024, .Height = 768, .HasPosition = false, .X = 0, .Y = 0, .IsMaximized = false },
        { .Mode = GameWindowMode_Borderless, .Width = 640, .Height = 360, .HasPosition = true, .X = -1920, .Y = 25, .IsMaximized = true },
        { .Mode = GameWindowMode_Fullscreen, .Width = 16384, .Height = 16384, .HasPosition = true, .X = INT32_MAX, .Y = INT32_MIN, .IsMaximized = false },
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        GenericBuffer Bytes;
        GenericBuffer_AllocateVariable(&Bytes, STATE_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
        GameWindowState Read = MakeSentinelState();
        if (TEST_CHECK_SUCCESS(run, GameWindowState_WriteJSON(&Cases[Index], &Bytes)))
        {
            TEST_CHECK_SUCCESS(run, GameWindowState_ReadJSON(Bytes._data, Bytes._count, &Read));
            TEST_CHECK(run, AreStatesEqual(&Read, &Cases[Index]));
        }
        Memory_Free(Bytes._data);
    }
}

// Every key is written, also at its default, so the file reads the same by hand whatever it holds.
static void TheFileListsEveryKey(TestRun* run)
{
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, STATE_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, GameWindowState_WriteJSON(&State, &Bytes)) && GenericBuffer_NullTerminate(&Bytes))
    {
        const char* Text = (const char*)Bytes._data;
        static const char* const Keys[] = { "\"mode\": \"windowed\"", "\"width\": 1280", "\"height\": 720", "\"has_position\": false",
            "\"x\": 0", "\"y\": 0", "\"is_maximized\": false" };
        for (size_t Index = 0U; Index < (sizeof(Keys) / sizeof(Keys[0])); Index++)
        {
            TEST_CHECK(run, strstr(Text, Keys[Index]) != NULL);
        }
    }
    Memory_Free(Bytes._data);
}


// Tests: the file.
static void FilesRoundTripAtomically(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, STATE_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, STATE_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "window_state_files", &Directory))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"window_state.json", &Path)))
    {
        // No file yet: the first run.
        GameWindowState Read = MakeSentinelState();
        GameWindowState Expected = Read;
        Error Missing = GameWindowState_LoadFile(Path._data, &Read);
        TEST_CHECK(run, Missing.Code == ErrorCode_FileNotFound);
        Error_Deconstruct(&Missing);
        TEST_CHECK(run, AreStatesEqual(&Read, &Expected));

        GameWindowState Saved = { .Mode = GameWindowMode_Borderless, .Width = 1600, .Height = 900, .HasPosition = true, .X = 50,
            .Y = 60, .IsMaximized = true };
        TEST_CHECK_SUCCESS(run, GameWindowState_SaveFile(&Saved, Path._data));
        TEST_CHECK_SUCCESS(run, GameWindowState_LoadFile(Path._data, &Read));
        TEST_CHECK(run, AreStatesEqual(&Read, &Saved));

        // A second save replaces the file, and no temporary is left beside it.
        Saved.Mode = GameWindowMode_Windowed;
        Saved.Width = 700;
        TEST_CHECK_SUCCESS(run, GameWindowState_SaveFile(&Saved, Path._data));
        TEST_CHECK_SUCCESS(run, GameWindowState_LoadFile(Path._data, &Read));
        TEST_CHECK(run, AreStatesEqual(&Read, &Saved));
        size_t FileCount = 0U;
        TEST_CHECK_SUCCESS(run, Test_CountFiles(Directory._data, &FileCount));
        TEST_CHECK(run, FileCount == 1U);

        // A bad file changes nothing.
        Read = MakeSentinelState();
        Expected = Read;
        TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Directory._data, "window_state.json", "{\"width\": "));
        TEST_CHECK_ERROR(run, GameWindowState_LoadFile(Path._data, &Read), ErrorCode_InvalidJSON);
        TEST_CHECK(run, AreStatesEqual(&Read, &Expected));

        // So does a file too long to read: refused by its size.
        char Long[GAME_WINDOW_STATE_MAX_BYTES + 2U];
        Memory_Set(Long, ' ', sizeof(Long));
        Long[0] = '{';
        Long[sizeof(Long) - 2U] = '}';
        Long[sizeof(Long) - 1U] = 0;
        TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Directory._data, "window_state.json", Long));
        TEST_CHECK_ERROR(run, GameWindowState_LoadFile(Path._data, &Read), ErrorCode_InvalidJSON);
        TEST_CHECK(run, AreStatesEqual(&Read, &Expected));

        // An empty file is a bad one, and so is a folder of that name.
        TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Directory._data, "window_state.json", ""));
        TEST_CHECK_ERROR(run, GameWindowState_LoadFile(Path._data, &Read), ErrorCode_InvalidJSON);
        TEST_CHECK(run, AreStatesEqual(&Read, &Expected));
        GenericBuffer Folder;
        GenericBuffer_AllocateVariable(&Folder, STATE_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
        if (TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"a_folder", &Folder)) && TEST_CHECK_SUCCESS(run, FileSystem_CreateDirectory(Folder._data)))
        {
            Error FromFolder = GameWindowState_LoadFile(Folder._data, &Read);
            TEST_CHECK(run, FromFolder.Code != ErrorCode_Success);
            Error_Deconstruct(&FromFolder);
            TEST_CHECK(run, AreStatesEqual(&Read, &Expected));
        }
        Memory_Free(Folder._data);

        // A save the file system refuses reports it and leaves no temporary behind in a folder that is not there.
        GenericBuffer Unreachable;
        GenericBuffer_AllocateVariable(&Unreachable, STATE_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
        if (TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, u8"no_such_folder/window_state.json", &Unreachable)))
        {
            Error Refused = GameWindowState_SaveFile(&Saved, Unreachable._data);
            TEST_CHECK(run, Refused.Code != ErrorCode_Success);
            Error_Deconstruct(&Refused);
        }
        Memory_Free(Unreachable._data);
    }
    Memory_Free(Path._data);
    Memory_Free(Directory._data);
}

static void TheFileFunctionsRefuseNull(TestRun* run)
{
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 8U, sizeof(unsigned char));
    TEST_CHECK_ERROR(run, GameWindowState_LoadFile(NULL, &State), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameWindowState_LoadFile(u8"x", NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameWindowState_SaveFile(NULL, u8"x"), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameWindowState_SaveFile(&State, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameWindowState_WriteJSON(NULL, &Bytes), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameWindowState_WriteJSON(&State, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameWindowState_ReadJSON((const unsigned char*)"{}", 2U, NULL), ErrorCode_IllegalArgument);
    Memory_Free(Bytes._data);
}


// Tests: the monitors.
static void APositionOnAMonitorIsKept(TestRun* run)
{
    const GameWindowMonitor Monitors[] =
    {
        { .X = 0, .Y = 0, .Width = 1920, .Height = 1080 },
        { .X = 1920, .Y = 0, .Width = 2560, .Height = 1440 },
        { .X = -1280, .Y = 200, .Width = 1280, .Height = 720 },
    };
    const int32_t Places[][2] =
    {
        { 0, 16 },        // A corner of the first, with exactly enough room for some of a title bar above it.
        { 900, 1040 },    // Its top still on the first monitor, by exactly enough.
        { 2000, 100 },    // On the second.
        { -1280, 216 },   // On the one to the left of the origin, at its corner.
        { -1000, 880 },   // By exactly enough above its bottom.
        { -900, 16 },     // Its width still reaches the first, by exactly enough.
        { 1820, 500 },    // By exactly enough of its width on the first; the rest is on the second.
    };
    for (size_t Index = 0U; Index < (sizeof(Places) / sizeof(Places[0])); Index++)
    {
        GameWindowState State = MakePlacedState(Places[Index][0], Places[Index][1]);
        GameWindowState_FitToMonitors(&State, Monitors, sizeof(Monitors) / sizeof(Monitors[0]));
        TEST_CHECK(run, State.HasPosition && (State.X == Places[Index][0]) && (State.Y == Places[Index][1]));
        TEST_CHECK(run, (State.Width == 1000) && (State.Height == 500));
    }
}

static void APositionOffEveryMonitorIsDropped(TestRun* run)
{
    const GameWindowMonitor Monitors[] =
    {
        { .X = 0, .Y = 0, .Width = 1920, .Height = 1080 },
        { .X = 1920, .Y = 0, .Width = 2560, .Height = 1440 },
    };
    const int32_t Places[][2] =
    {
        { -5000, -5000 },
        { 10000, 100 },
        { 100, 5000 },
        { 100, -1 },           // Its top above every monitor.
        { 100, 0 },            // Its top at the monitor's: positions are the content's, so its title bar is above the monitor.
        { 100, 15 },           // Not quite room for some of it.
        { 100, 1041 },         // Too little of it left above the first monitor's bottom.
        { -901, 100 },         // Too little of its width on the first.
        { 4381, 100 },         // Too little of its width on the second.
        { INT32_MAX, INT32_MAX },
        { INT32_MIN, INT32_MIN },
        { INT32_MAX, 100 },
        { 100, INT32_MIN },
        { -32000, -32000 },    // Where Windows puts a minimized window.
    };
    for (size_t Index = 0U; Index < (sizeof(Places) / sizeof(Places[0])); Index++)
    {
        GameWindowState State = MakePlacedState(Places[Index][0], Places[Index][1]);
        GameWindowState_FitToMonitors(&State, Monitors, sizeof(Monitors) / sizeof(Monitors[0]));
        TEST_CHECK(run, !State.HasPosition);
        TEST_CHECK(run, (State.Width == 1000) && (State.Height == 500));
    }

    // With no monitors there is nowhere it could be; those of no size count as none.
    GameWindowState State = MakePlacedState(100, 100);
    GameWindowState_FitToMonitors(&State, NULL, 0U);
    TEST_CHECK(run, !State.HasPosition && (State.Width == 1000) && (State.Height == 500));
    const GameWindowMonitor Empty[] = { { .X = 0, .Y = 0, .Width = 0, .Height = 1080 }, { .X = 0, .Y = 0, .Width = 1920, .Height = -1 } };
    State = MakePlacedState(100, 100);
    GameWindowState_FitToMonitors(&State, Empty, 2U);
    TEST_CHECK(run, !State.HasPosition && (State.Width == 1000) && (State.Height == 500));
}

// A window with a place may span monitors: the player left it so, and it comes back the same.
static void AWindowMaySpanTheMonitors(TestRun* run)
{
    const GameWindowMonitor Monitors[] =
    {
        { .X = 0, .Y = 0, .Width = 1920, .Height = 1080 },
        { .X = 1920, .Y = 0, .Width = 1920, .Height = 1080 },
    };
    GameWindowState State = MakePlacedState(100, 100);
    State.Width = 3000;
    State.Height = 900;
    GameWindowState_FitToMonitors(&State, Monitors, 2U);
    TEST_CHECK(run, State.HasPosition && (State.X == 100) && (State.Width == 3000) && (State.Height == 900));

    // Larger than one of them, on the other of a different size.
    const GameWindowMonitor Unequal[] =
    {
        { .X = 0, .Y = 0, .Width = 1280, .Height = 720 },
        { .X = 1280, .Y = 0, .Width = 3840, .Height = 2160 },
    };
    State = MakePlacedState(1000, 100);
    State.Width = 3000;
    State.Height = 2000;
    GameWindowState_FitToMonitors(&State, Unequal, 2U);
    TEST_CHECK(run, State.HasPosition && (State.Width == 3000) && (State.Height == 2000));

    // Past the whole desktop, it comes down to it, whichever monitors make it up.
    State = MakePlacedState(100, 100);
    State.Width = 9000;
    State.Height = 5000;
    GameWindowState_FitToMonitors(&State, Unequal, 2U);
    TEST_CHECK(run, State.HasPosition && (State.Width == 5120) && (State.Height == 2160));

    // A monitor left of and above the first counts in the desktop's box.
    const GameWindowMonitor Around[] =
    {
        { .X = 0, .Y = 0, .Width = 1000, .Height = 1000 },
        { .X = -500, .Y = -300, .Width = 500, .Height = 300 },
    };
    State = MakePlacedState(100, 100);
    State.Width = 5000;
    State.Height = 5000;
    GameWindowState_FitToMonitors(&State, Around, 2U);
    TEST_CHECK(run, State.HasPosition && (State.Width == 1500) && (State.Height == 1300));
}

// A window with no place opens on the first monitor, so that one bounds it, and no further than the minimum.
static void WithNoPlaceTheFirstMonitorBoundsIt(TestRun* run)
{
    const GameWindowMonitor Monitors[] =
    {
        { .X = 0, .Y = 0, .Width = 1280, .Height = 720 },
        { .X = 1280, .Y = 0, .Width = 3840, .Height = 2160 },
    };
    GameWindowState State = MakePlacedState(-9000, 100);
    State.Width = 3000;
    State.Height = 2000;
    GameWindowState_FitToMonitors(&State, Monitors, 2U);
    TEST_CHECK(run, !State.HasPosition && (State.Width == 1280) && (State.Height == 720));

    State.Width = 3000;
    State.Height = 2000;
    GameWindowState_FitToMonitors(&State, Monitors, 2U);
    TEST_CHECK(run, !State.HasPosition && (State.Width == 1280) && (State.Height == 720));

    // A monitor smaller than the minimum: the minimum still holds.
    const GameWindowMonitor Tiny = { .X = 0, .Y = 0, .Width = 320, .Height = 240 };
    State = MakePlacedState(0, 16);
    GameWindowState_FitToMonitors(&State, &Tiny, 1U);
    TEST_CHECK(run, (State.Width == GAME_WINDOW_MIN_WIDTH) && (State.Height == GAME_WINDOW_MIN_HEIGHT));
}

// A position is judged at the size the window will have: a window that was just past a monitor's edge, shrunk, may be wholly off it.
static void APositionIsJudgedAtTheFittedSize(TestRun* run)
{
    const GameWindowMonitor Monitor = { .X = 0, .Y = 0, .Width = 1920, .Height = 1080 };
    GameWindowState State = MakePlacedState(-2500, 100);
    State.Width = 2600; // Reaches the monitor at this width: at its 1920 it ends 580 left of it.
    GameWindowState_FitToMonitors(&State, &Monitor, 1U);
    TEST_CHECK(run, !State.HasPosition && (State.Width == 1920) && (State.Height == 500));
}

// Centred on the monitor, with room above for some of a title bar when it is nearly the monitor's height.
static void AWindowWithNoPlaceIsCentred(TestRun* run)
{
    const GameWindowMonitor Monitor = { .X = 1920, .Y = 100, .Width = 2560, .Height = 1440 };
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    State.Width = 1000;
    State.Height = 600;
    int32_t X = 0;
    int32_t Y = 0;
    GameWindowState_GetCentredPosition(&State, &Monitor, &X, &Y);
    TEST_CHECK(run, (X == (1920 + 780)) && (Y == (100 + 420)));
    State.Width = 2560;
    State.Height = 1440;
    GameWindowState_GetCentredPosition(&State, &Monitor, &X, &Y);
    TEST_CHECK(run, (X == 1920) && (Y == (100 + GAME_WINDOW_MIN_TITLE_BAR_ROOM)));
    State.Height = 1420;
    GameWindowState_GetCentredPosition(&State, &Monitor, &X, &Y);
    TEST_CHECK(run, Y == (100 + GAME_WINDOW_MIN_TITLE_BAR_ROOM));
    State.Height = 1400;
    GameWindowState_GetCentredPosition(&State, &Monitor, &X, &Y);
    TEST_CHECK(run, Y == (100 + 20));

    // What it answers holds as a position.
    GameWindowState Placed;
    GameWindowState_SetDefaults(&Placed);
    Placed.Width = 2560;
    Placed.Height = 1440;
    GameWindowState_GetCentredPosition(&Placed, &Monitor, &Placed.X, &Placed.Y);
    Placed.HasPosition = true;
    GameWindowState_FitToMonitors(&Placed, &Monitor, 1U);
    TEST_CHECK(run, Placed.HasPosition);
}

// A state with no place stays with no place, whatever its numbers say: it opens centred, not at the top-left corner.
static void NoPositionStaysNoPosition(TestRun* run)
{
    const GameWindowMonitor Monitor = { .X = 0, .Y = 0, .Width = 1920, .Height = 1080 };
    GameWindowState State = MakePlacedState(100, 100); // Numbers that would count as a place on this monitor.
    State.HasPosition = false;
    GameWindowState_FitToMonitors(&State, &Monitor, 1U);
    TEST_CHECK(run, !State.HasPosition && (State.Width == 1000) && (State.Height == 500));
}

// The monitor of no size is skipped when choosing the first one.
static void TheFirstMonitorWithASizeIsTheOneFitted(TestRun* run)
{
    const GameWindowMonitor Monitors[] =
    {
        { .X = 0, .Y = 0, .Width = 0, .Height = 0 },
        { .X = 0, .Y = 0, .Width = 800, .Height = 600 },
        { .X = 800, .Y = 0, .Width = 1920, .Height = 1080 },
    };
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    State.Width = 1500;
    State.Height = 1000;
    GameWindowState_FitToMonitors(&State, Monitors, 3U);
    TEST_CHECK(run, (State.Width == 800) && (State.Height == 600));
}


// Tests: the watch.
static void ASizeIsRememberedOnceItHolds(TestRun* run)
{
    GameWindowWatch Watch;
    GameWindowWatch_Construct(&Watch);
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    GameWindowObservation Seen = MakeSeen(900, 600, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 10.0);
    TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 10.0 + GAME_WINDOW_SETTLE_SECONDS - STATE_TEST_TICK);
    TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 10.0 + GAME_WINDOW_SETTLE_SECONDS);
    TEST_CHECK(run, (State.Width == 900) && (State.Height == 600) && State.HasPosition && (State.X == 40) && (State.Y == 50));

    // Each change starts the wait again: a drag is remembered where it ends.
    Seen = MakeSeen(901, 600, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 20.0);
    Seen = MakeSeen(902, 600, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 20.4);
    Seen = MakeSeen(903, 600, 40, 51);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 20.8);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 21.2);
    TEST_CHECK(run, (State.Width == 900) && (State.Y == 50));
    GameWindowWatch_Observe(&Watch, &State, &Seen, 21.3);
    TEST_CHECK(run, (State.Width == 903) && (State.Y == 51));

    // Any one number starts it again.
    Seen = MakeSeen(903, 601, 40, 51);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 30.0);
    Seen = MakeSeen(903, 601, 41, 51);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 30.3);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 30.6);
    TEST_CHECK(run, (State.Height == 600) && (State.X == 40));
    GameWindowWatch_Observe(&Watch, &State, &Seen, 30.8);
    TEST_CHECK(run, (State.Height == 601) && (State.X == 41));
}

// A change of any one number starts the wait again, and that number is what gets remembered.
static void AnyOneNumberStartsTheWaitAgain(TestRun* run)
{
    const GameWindowObservation Changed[] =
    {
        { .Width = 901, .Height = 600, .X = 40, .Y = 50 },
        { .Width = 900, .Height = 601, .X = 40, .Y = 50 },
        { .Width = 900, .Height = 600, .X = 41, .Y = 50 },
        { .Width = 900, .Height = 600, .X = 40, .Y = 51 },
    };
    for (size_t Index = 0U; Index < (sizeof(Changed) / sizeof(Changed[0])); Index++)
    {
        GameWindowWatch Watch;
        GameWindowWatch_Construct(&Watch);
        GameWindowState State;
        GameWindowState_SetDefaults(&State);
        GameWindowObservation First = MakeSeen(900, 600, 40, 50);
        GameWindowWatch_Observe(&Watch, &State, &First, 1.0);
        // Most of the wait is over when the one number changes: past the first's whole wait, the second's is not.
        GameWindowWatch_Observe(&Watch, &State, &Changed[Index], 1.4);
        GameWindowWatch_Observe(&Watch, &State, &Changed[Index], 1.6);
        TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition);
        GameWindowWatch_Observe(&Watch, &State, &Changed[Index], 1.9);
        TEST_CHECK(run, (State.Width == Changed[Index].Width) && (State.Height == Changed[Index].Height));
        TEST_CHECK(run, State.HasPosition && (State.X == Changed[Index].X) && (State.Y == Changed[Index].Y));
    }
}

// A window manager reports a maximize as a new size and a new state, in either order: neither order remembers the maximized size.
static void TheMaximizedSizeIsNeverRemembered(TestRun* run)
{
    GameWindowWatch Watch;
    GameWindowWatch_Construct(&Watch);
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    GameWindowObservation Normal = MakeSeen(900, 600, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Normal, 1.0);
    GameWindowWatch_Observe(&Watch, &State, &Normal, 2.0);
    TEST_CHECK(run, (State.Width == 900) && !State.IsMaximized);

    // The size first, the state a few ticks later.
    GameWindowObservation Large = MakeSeen(1920, 1040, 0, 40);
    GameWindowWatch_Observe(&Watch, &State, &Large, 3.0);
    GameWindowWatch_Observe(&Watch, &State, &Large, 3.1);
    Large.IsMaximized = true;
    GameWindowWatch_Observe(&Watch, &State, &Large, 3.2);
    GameWindowWatch_Observe(&Watch, &State, &Large, 9.0);
    TEST_CHECK(run, State.IsMaximized && (State.Width == 900) && (State.Height == 600) && (State.X == 40) && (State.Y == 50));

    // A settle while it is maximized has nothing to take.
    GameWindowWatch_Settle(&Watch, &State);
    TEST_CHECK(run, State.IsMaximized && (State.Width == 900) && (State.X == 40));

    // Restored: the state first, the old size after it.
    Large.IsMaximized = false;
    GameWindowWatch_Observe(&Watch, &State, &Large, 10.0);
    TEST_CHECK(run, !State.IsMaximized && (State.Width == 900));
    GameWindowWatch_Observe(&Watch, &State, &Normal, 10.1);
    GameWindowWatch_Observe(&Watch, &State, &Normal, 11.0);
    TEST_CHECK(run, !State.IsMaximized && (State.Width == 900));

    // The state first on the way in.
    GameWindowObservation MaximizedOldSize = Normal;
    MaximizedOldSize.IsMaximized = true;
    GameWindowWatch_Observe(&Watch, &State, &MaximizedOldSize, 12.0);
    TEST_CHECK(run, State.IsMaximized);
    Large.IsMaximized = true;
    GameWindowWatch_Observe(&Watch, &State, &Large, 12.1);
    GameWindowWatch_Observe(&Watch, &State, &Large, 13.0);
    TEST_CHECK(run, State.IsMaximized && (State.Width == 900));
}

// What a minimized window reports means nothing, and its being maximized stays as it was.
static void AMinimizedWindowIsNotLookedAt(TestRun* run)
{
    GameWindowWatch Watch;
    GameWindowWatch_Construct(&Watch);
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    State.IsMaximized = true;
    GameWindowObservation Gone = MakeSeen(160, 28, -32000, -32000);
    Gone.IsMinimized = true;
    GameWindowWatch_Observe(&Watch, &State, &Gone, 1.0);
    GameWindowWatch_Observe(&Watch, &State, &Gone, 5.0);
    TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition && State.IsMaximized);
    GameWindowWatch_Settle(&Watch, &State);
    TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition);

    // Something pending when it is minimized goes: it had not held.
    GameWindowObservation Seen = MakeSeen(900, 600, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 6.0);
    GameWindowWatch_Observe(&Watch, &State, &Gone, 6.1);
    GameWindowWatch_Settle(&Watch, &State);
    TEST_CHECK(run, State.Width == 1280);

    // Sizes of nothing are not looked at either.
    GameWindowObservation Empty = MakeSeen(0, 600, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Empty, 7.0);
    GameWindowWatch_Observe(&Watch, &State, &Empty, 9.0);
    Empty = MakeSeen(900, 0, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Empty, 10.0);
    GameWindowWatch_Observe(&Watch, &State, &Empty, 12.0);
    TEST_CHECK(run, (State.Width == 1280) && (State.Height == 720));
}

// After a change of mode, what the window reports is the switch, not the player's doing.
static void AHoldBelievesNothing(TestRun* run)
{
    GameWindowWatch Watch;
    GameWindowWatch_Construct(&Watch);
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    State.IsMaximized = true;
    GameWindowObservation Seen = MakeSeen(900, 600, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 1.0);
    TEST_CHECK(run, !State.IsMaximized);
    State.IsMaximized = true;

    GameWindowWatch_Hold(&Watch, 2.0);
    GameWindowWatch_Settle(&Watch, &State); // What was pending went with the hold.
    TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition);
    GameWindowObservation Switching = MakeSeen(1920, 1080, 0, 0);
    GameWindowWatch_Observe(&Watch, &State, &Switching, 2.0);
    GameWindowWatch_Observe(&Watch, &State, &Switching, 2.0 + GAME_WINDOW_SETTLE_SECONDS - STATE_TEST_TICK);
    TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition && State.IsMaximized);
    GameWindowWatch_Settle(&Watch, &State);
    TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition);

    // After it the window counts again, and the wait starts from there.
    GameWindowWatch_Observe(&Watch, &State, &Seen, 2.0 + GAME_WINDOW_SETTLE_SECONDS);
    TEST_CHECK(run, !State.IsMaximized && (State.Width == 1280));
    GameWindowWatch_Observe(&Watch, &State, &Seen, 2.0 + (2.0 * GAME_WINDOW_SETTLE_SECONDS));
    TEST_CHECK(run, (State.Width == 900) && State.HasPosition);
}

// A save takes what was seen last, however briefly it has held: the player changed the size and left at once.
static void ASettleTakesWhatIsPending(TestRun* run)
{
    GameWindowWatch Watch;
    GameWindowWatch_Construct(&Watch);
    GameWindowState State;
    GameWindowState_SetDefaults(&State);
    GameWindowWatch_Settle(&Watch, &State); // Nothing seen: nothing taken.
    TEST_CHECK(run, (State.Width == 1280) && !State.HasPosition);
    GameWindowObservation Seen = MakeSeen(900, 600, 40, 50);
    GameWindowWatch_Observe(&Watch, &State, &Seen, 1.0);
    TEST_CHECK(run, State.Width == 1280);
    GameWindowWatch_Settle(&Watch, &State);
    TEST_CHECK(run, (State.Width == 900) && (State.Height == 600) && State.HasPosition && (State.X == 40) && (State.Y == 50));
}


// Public functions.
void GameWindowStateTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "modes_read_and_write_by_name", ModesReadAndWriteByName },
        { "the_toggle_goes_to_its_mode_and_back", TheToggleGoesToItsModeAndBack },
        { "the_defaults_are_a_plain_window", TheDefaultsAreAPlainWindow },
        { "every_field_is_read", EveryFieldIsRead },
        { "missing_and_unknown_keys_are_forgiven", MissingAndUnknownKeysAreForgiven },
        { "absurd_sizes_are_clamped", AbsurdSizesAreClamped },
        { "bad_files_change_nothing", BadFilesChangeNothing },
        { "long_and_deep_files_are_refused", LongAndDeepFilesAreRefused },
        { "a_round_trip_keeps_every_field", ARoundTripKeepsEveryField },
        { "the_file_lists_every_key", TheFileListsEveryKey },
        { "files_round_trip_atomically", FilesRoundTripAtomically },
        { "the_file_functions_refuse_null", TheFileFunctionsRefuseNull },
        { "a_position_on_a_monitor_is_kept", APositionOnAMonitorIsKept },
        { "a_position_off_every_monitor_is_dropped", APositionOffEveryMonitorIsDropped },
        { "a_window_may_span_the_monitors", AWindowMaySpanTheMonitors },
        { "with_no_place_the_first_monitor_bounds_it", WithNoPlaceTheFirstMonitorBoundsIt },
        { "a_position_is_judged_at_the_fitted_size", APositionIsJudgedAtTheFittedSize },
        { "a_window_with_no_place_is_centred", AWindowWithNoPlaceIsCentred },
        { "no_position_stays_no_position", NoPositionStaysNoPosition },
        { "the_first_monitor_with_a_size_is_the_one_fitted", TheFirstMonitorWithASizeIsTheOneFitted },
        { "a_size_is_remembered_once_it_holds", ASizeIsRememberedOnceItHolds },
        { "any_one_number_starts_the_wait_again", AnyOneNumberStartsTheWaitAgain },
        { "the_maximized_size_is_never_remembered", TheMaximizedSizeIsNeverRemembered },
        { "a_minimized_window_is_not_looked_at", AMinimizedWindowIsNotLookedAt },
        { "a_hold_believes_nothing", AHoldBelievesNothing },
        { "a_settle_takes_what_is_pending", ASettleTakesWhatIsPending },
    };
    TestRun_RunSuite(run, "GameWindowState", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
