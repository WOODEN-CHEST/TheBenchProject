#include <inttypes.h>
#include <math.h>
#include "WorldTestFrame.h"
#include "GameClock.h"
#include "GameFrame.h"
#include "Services.h"
#include "World.h"
#include "WorldFile.h"
#include "WorldObject.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "GameCamera.h"
#include "WorldRenderer.h"
#include "Renderer.h"
#include "Logger.h"
#include "GameFrameManager.h"
#include "GameInput.h"
#include "TextComponentFactory.h"
#include "TextComponentJSON.h"
#include "TextDocument.h"
#include "TextLayout.h"
#include "raylib/raylib.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"
#include "wr/WRString.h"


// Macros.
/** Debug name used in log/crash messages. */
#define FRAME_DEBUG_NAME ((const unsigned char*)u8"WorldTest")
/** The test world's file, <base>/worlds/test.world.json, and the name F5 saves the live world under (as JSON and
 *  GHDF: worlds/test.saved.world.json and .world.ghdf), for round-trip checks. */
#define TEST_WORLD_FILE_NAME ((const unsigned char*)u8"test")
#define SAVED_WORLD_FILE_NAME ((const unsigned char*)u8"test.saved")
/** Base movement speed, in world units per second. */
#define MOVE_SPEED 6.0f
/** Multiplier applied to movement speed while the sprint key is held. */
#define SPRINT_MULTIPLIER 3.0f
/** Mouse look sensitivity, in radians per pixel of mouse movement. */
#define MOUSE_SENSITIVITY 0.0025f
/** The test world's clock speed in game seconds per real second: a 2-minute day, short so the cycle is easy to
 *  observe (the clock's own default is 72, a 20-minute day). */
#define TEST_TIME_SCALE 720.0
/** Manual time scrub rate (fraction of a full day per real second while [ or ] is held). */
#define TIME_SCRUB_RATE 0.1
/** The hours the number keys jump to: dawn, noon, dusk and midnight. */
#define DAWN_HOUR 6
#define NOON_HOUR 12
#define DUSK_HOUR 18
#define MIDNIGHT_HOUR 0
/** How many days the week key (/) moves the date forward. */
#define DAYS_PER_WEEK 7
/** Room for the clock's debug text ("Sun 2026-10-18 09:30:00") and its terminator. */
#define CLOCK_TEXT_CAPACITY ((size_t)64)
/** The phase the F key pins the moon at: full. */
#define FIXED_MOON_PHASE 0.5f
/** The moon's eight named phases split the cycle into eighths, each centred on its phase. */
#define MOON_PHASE_NAME_COUNT 8
/** Real seconds in a minute, for the I key's day length. */
#define SECONDS_PER_REAL_MINUTE 60.0
/** How far above the current light pollution the N key's next step must be (so 0.35 steps on to 0.7). */
#define LIGHT_POLLUTION_STEP_EPSILON 0.001f
/** The objects whose opacity the X key steps: the centre cube (a model) and the tree (a cutout sprite), both casting
 *  shadows. */
#define OPACITY_TEST_OBJECT_COUNT 2U
/** The test world's hierarchy (T08): the desk (an empty object holding its parts) and how the J key slides it along X:
 *  back and forth by this many metres either way around where it was loaded, once per period. */
#define TEST_DESK_OBJECT_NAME ((const unsigned char*)u8"desk")
#define DESK_SLIDE_DISTANCE 1.2f
#define DESK_SLIDE_PERIOD_SECONDS 6.0
/** The test world's variant example (T10): two cubes of the "painted_cube" world model, and how many looks the E key
 *  steps them through (VariantSteps). */
#define VARIANT_TEST_OBJECT_COUNT 2U
#define VARIANT_STEP_COUNT 3U
/** The vertical field of view the Z key zooms to, in degrees: about 4.4 times closer than the default 60, so models'
 *  draw distances reach 4.4 times as far. */
#define ZOOMED_FOV_Y 15.0f
/** Default font of the text sample: covers Latvian and Cyrillic. */
#define TEXT_SAMPLE_FONT_NAME ((const unsigned char*)u8"noto_sans")
/** Top-left corner of the text sample, in normalized-fitted units. */
#define TEXT_SAMPLE_POSITION_X 0.02f
#define TEXT_SAMPLE_POSITION_Y 0.02f
/** The text sample (T toggles it), as text component JSON: fonts, Latvian and Cyrillic letters, decorations, a
 *  translate fallback and an inline sprite, to check text drawing by eye. The last line uses the test font, which
 *  lacks most of these letters (they draw as '?', and its load logs which codepoints it misses). */
#define TEXT_SAMPLE_JSON \
    u8"[" \
    u8"{\"type\": \"string\", \"text\": \"Text sample (T hides it) \", \"size\": 0.04, \"color\": \"#ffe08a\"}," \
    u8"{\"type\": \"sprite\", \"animation\": \"test_sprite\", \"size\": [0.04, 0.04]}," \
    u8"{\"type\": \"string\", \"text\": \"\\nLatvie\u0161u: \u0100 \u010c \u0112 \u0122 \u012a \u0136 \u013b \u0145 \u0160 \u016a \u017d  " \
        u8"\u0101 \u010d \u0113 \u0123 \u012b \u0137 \u013c \u0146 \u0161 \u016b \u017e\\n\", \"size\": 0.035}," \
    u8"{\"type\": \"string\", \"text\": \"\u0420\u0443\u0441\u0441\u043a\u0438\u0439: \u0421\u044a\u0435\u0448\u044c " \
        u8"\u0436\u0435 \u0435\u0449\u0451 \u044d\u0442\u0438\u0445 \u043c\u044f\u0433\u043a\u0438\u0445 " \
        u8"\u0444\u0440\u0430\u043d\u0446\u0443\u0437\u0441\u043a\u0438\u0445 \u0431\u0443\u043b\u043e\u043a.\\n\", \"size\": 0.035}," \
    u8"{\"type\": \"string\", \"text\": \"underline\", \"size\": 0.035, \"underline\": true}," \
    u8"{\"type\": \"string\", \"text\": \"  \", \"size\": 0.035}," \
    u8"{\"type\": \"string\", \"text\": \"strikethrough\", \"size\": 0.035, \"strikethrough\": true, \"color\": \"#ff7070\"}," \
    u8"{\"type\": \"string\", \"text\": \"  \", \"size\": 0.035}," \
    u8"{\"type\": \"translate\", \"key\": \"test.no_such_key\", \"fallback\": \"translate fallback\\n\", \"size\": 0.035, " \
        u8"\"color\": {\"tint\": \"#80c0ff\", \"opacity\": 0.85}}," \
    u8"{\"type\": \"string\", \"text\": \"Test font: Latvie\u0161u \u0101 \u010d \u0113, \u0420\u0443\u0441\u0441\u043a\u0438\u0439\", " \
        u8"\"font\": \"test\", \"size\": 0.035}" \
    u8"]"


// Types.
typedef struct WorldTestFrameStruct
{
    /** Abstract base (must be first). */
    GameFrame Base;
    /** Shared services; borrowed. */
    Services* _services;
    /** The world being viewed; owned. */
    World _world;
    /** The game time: advanced every tick and written into the world's environment, so the sky shows it; owned. */
    GameClock _clock;
    /** The free-fly camera; owned value. */
    GameCamera _camera;
    /** The world renderer; owned. */
    WorldRenderer* _renderer;
    /** Whether the mouse cursor is currently captured. */
    bool _cursorCaptured;
    /** The text sample's components come from and go back to this factory; owned. */
    TextComponentFactory _textFactory;
    /** The text sample (empty if its JSON failed to load); owned. */
    TextDocument _textSample;
    /** The text sample laid out in Update and drawn over the world in Render; owned. */
    TextLayout _textSampleLayout;
    /** Whether the text sample is drawn (T toggles it). */
    bool _isTextSampleShown;
    /** The objects the X key fades (OPACITY_TEST_OBJECT_COUNT; 0 for one the test world lacks), and the step of
     *  OpacitySteps they are at. */
    uint64_t _opacityTestObjectIds[OPACITY_TEST_OBJECT_COUNT];
    size_t _opacityStep;
    /** The desk the J key slides and the H key hides (0 if the test world lacks it), where along X it was loaded (the
     *  slide's centre), and whether it is sliding, for how long it has (within one period). */
    uint64_t _deskId;
    float _deskStartX;
    bool _isDeskSliding;
    double _deskSlideSeconds;
    /** The variant cubes the E key steps (VARIANT_TEST_OBJECT_COUNT; 0 for one the test world lacks), and the step of
     *  VariantSteps they are at. */
    uint64_t _variantTestObjectIds[VARIANT_TEST_OBJECT_COUNT];
    size_t _variantStep;
    /** Whether the Z key's zoom is on (the camera at ZOOMED_FOV_Y). */
    bool _isZoomed;
} WorldTestFrame;


// Static functions: the world-up direction for vertical movement.
static const Vector3 FrameWorldUp = { .x = 0.0f, .y = 1.0f, .z = 0.0f };
/* The light pollution steps the N key cycles through: a dark site, a small town (the default), a suburb, a city. */
static const float LightPollutionSteps[] = { 0.0f, 0.35f, 0.7f, 1.0f };
/* The opacities the X key steps through: opaque, half (a 50% dither on the pixel grid), gone. */
static const float OpacitySteps[] = { 1.0f, 0.5f, 0.0f };
/* The names of the X key's objects (OPACITY_TEST_OBJECT_COUNT): the centre cube and the tree sprite. */
static const char* const OpacityTestObjectNames[OPACITY_TEST_OBJECT_COUNT] = { "test_object", "tree" };
/* The names of the E key's objects (VARIANT_TEST_OBJECT_COUNT): the variant cubes on the left and the right. */
static const char* const VariantTestObjectNames[VARIANT_TEST_OBJECT_COUNT] =
{
    "variant_cube_left", "variant_cube_right"
};
/* The variants the E key gives the two cubes at each step (the first is the test world's): their own, swapped, then
 * none (NULL: uses_variant off, the model's own white; the names are kept). */
static const char* const VariantSteps[VARIANT_STEP_COUNT][VARIANT_TEST_OBJECT_COUNT] =
{
    { "red", "blue" }, { "blue", "red" }, { NULL, NULL }
};
/* The moon's phases by name, from new moon (phase 0) in eighths of the cycle. */
static const char* const MoonPhaseNames[MOON_PHASE_NAME_COUNT] =
{
    "new moon", "waxing crescent", "first quarter", "waxing gibbous", "full moon", "waning gibbous", "last quarter",
    "waning crescent"
};


// Static functions: helpers.
/* The next light pollution step above @p current, wrapping to the first. */
static float NextLightPollutionStep(float current)
{
    size_t StepCount = sizeof(LightPollutionSteps) / sizeof(LightPollutionSteps[0]);
    for (size_t Index = 0U; Index < StepCount; Index++)
    {
        if (LightPollutionSteps[Index] > (current + LIGHT_POLLUTION_STEP_EPSILON))
        {
            return LightPollutionSteps[Index];
        }
    }
    return LightPollutionSteps[0];
}

/* The id of the first object named @p name, or 0 when the world has none. */
static uint64_t FindObjectId(World* world, const char* name)
{
    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        bool IsFound = false;
        Error Result = World_GetObjectByIndex(world, Index, &Object);
        if ((Result.Code == ErrorCode_Success) && (WorldObject_GetName(Object) != NULL))
        {
            Result = StringUTF8_EqualsExact(WorldObject_GetName(Object), (const unsigned char*)name, &IsFound);
        }
        Error_Deconstruct(&Result);
        if (IsFound)
        {
            return WorldObject_GetId(Object);
        }
    }
    return 0U;
}

/* Steps the opacity of the X key's objects (1, 0.5, 0, then 1 again) and logs it. */
static void StepTestOpacity(WorldTestFrame* frame)
{
    frame->_opacityStep = (frame->_opacityStep + 1U) % (sizeof(OpacitySteps) / sizeof(OpacitySteps[0]));
    float Opacity = OpacitySteps[frame->_opacityStep];
    for (size_t Index = 0U; Index < OPACITY_TEST_OBJECT_COUNT; Index++)
    {
        WorldObject* Object = NULL;
        if ((frame->_opacityTestObjectIds[Index] == 0U)
            || !World_TryGetObject(&frame->_world, frame->_opacityTestObjectIds[Index], &Object))
        {
            continue;
        }
        RenderColor Tint = WorldObject_GetTint(Object);
        Tint.Opacity = Opacity;
        Error Result = WorldObject_SetTint(Object, Tint);
        Error_Deconstruct(&Result); // every step is a valid opacity
    }
    if (frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: the centre cube and the tree at opacity %.1f.", (double)Opacity);
        Error_Deconstruct(&LogResult);
    }
}

/* The clock's debug text in @p storage ("Sun 2026-10-18 09:30:00"), or "?" if it could not be written. */
static const char* FormatClock(const GameClock* clock, unsigned char* storage, size_t capacity)
{
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, storage, capacity, sizeof(unsigned char), 0U);
    Error Result = GameClock_FormatDebug(clock, &Text);
    bool IsFormatted = Result.Code == ErrorCode_Success;
    Error_Deconstruct(&Result);
    return IsFormatted ? (const char*)storage : "?";
}

/* The sky shows the clock's time: its time of day and date go into the world's environment every tick. */
static void ApplyClockToEnvironment(WorldTestFrame* frame)
{
    WorldEnvironment* Environment = World_GetEnvironment(&frame->_world);
    Environment->TimeOfDay = GameClock_GetTimeOfDay(&frame->_clock);
    Environment->Date = GameClock_GetDay(&frame->_clock);
}

/* The name of the moon's phase, e.g. "waxing gibbous". */
static const char* GetMoonPhaseName(float phase)
{
    size_t Index = (size_t)floorf(fminf(fmaxf(phase, 0.0f), 1.0f) * (float)MOON_PHASE_NAME_COUNT + 0.5f);
    return MoonPhaseNames[Index % MOON_PHASE_NAME_COUNT];
}

/* Logs a change of the game time with the moon it brings, "WorldTest: <what>: Sun 2026-10-18 06:00:00; first quarter
 * (phase 0.24, 49% lit, from the calendar).": on key presses and new days, never per tick. The environment is brought
 * up to the clock first, so the moon is the new time's. */
static void LogClock(WorldTestFrame* frame, const char* what)
{
    if (frame->_services->Logger == NULL)
    {
        return;
    }
    ApplyClockToEnvironment(frame);
    const WorldEnvironment* Environment = World_GetEnvironment(&frame->_world);
    unsigned char Storage[CLOCK_TEXT_CAPACITY];
    const char* Clock = FormatClock(&frame->_clock, Storage, sizeof(Storage));
    Error LogResult;
    if (!Environment->IsMoonEnabled)
    {
        LogResult = Logger_LogInfoFormatted(frame->_services->Logger, (const unsigned char*)u8"WorldTest: %s: %s; no moon.",
            what, Clock);
    }
    else
    {
        float Phase = WorldEnvironment_GetMoonPhase(Environment);
        float Illumination = WorldEnvironment_GetMoonIllumination(Environment);
        LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: %s: %s; %s (phase %.2f, %.0f%% lit, %s).", what, Clock,
            GetMoonPhaseName(Phase), (double)Phase, (double)Illumination * 100.0,
            (Environment->MoonPhaseSource == WorldMoonPhaseSource_Fixed) ? "fixed" : "from the calendar");
    }
    Error_Deconstruct(&LogResult);
}

/* The I key's line about the game time: the date and time, whether the clock runs, and its speed; then the moon. */
static void LogClockStatus(WorldTestFrame* frame)
{
    if (frame->_services->Logger == NULL)
    {
        return;
    }
    unsigned char Storage[CLOCK_TEXT_CAPACITY];
    double TimeScale = GameClock_GetTimeScale(&frame->_clock);
    Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
        (const unsigned char*)u8"WorldTest: game time %s, %s, %g game seconds per real second (a day takes %g real "
        u8"minutes); seasons %s.", FormatClock(&frame->_clock, Storage, sizeof(Storage)),
        frame->_clock.IsPaused ? "paused" : "running", TimeScale,
        GAME_CLOCK_SECONDS_PER_DAY / TimeScale / SECONDS_PER_REAL_MINUTE,
        World_GetEnvironment(&frame->_world)->AreSeasonsEnabled ? "on" : "off");
    Error_Deconstruct(&LogResult);
    LogClock(frame, "the moon now");
}

/* Starts the clock at the test speed, where the world's sky starts: the environment's date and time of day. */
static Error StartClock(GameClock* clock, const WorldEnvironment* environment)
{
    // TODO: start the clock from the story's settings (its start date, time and speed) once stories have a manifest
    // (architecture A5); until then the world's environment is the starting point.
    Error Result = GameClock_SetTimeScale(clock, TEST_TIME_SCALE);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    int64_t Day = environment->Date;
    double Seconds = (double)environment->TimeOfDay * GAME_CLOCK_SECONDS_PER_DAY;
    if (Seconds >= GAME_CLOCK_SECONDS_PER_DAY)
    {
        Seconds -= GAME_CLOCK_SECONDS_PER_DAY; // a time of day of 1 is the next midnight
        Day++;
    }
    return GameClock_SetDayAndTime(clock, Day, Seconds);
}

/* Jumps to an hour of today and pauses the clock there (the number keys). */
static void JumpToHour(WorldTestFrame* frame, int32_t hour, const char* what)
{
    Error Result = GameClock_SetDayAndTime(&frame->_clock, GameClock_GetDay(&frame->_clock),
        (double)hour * GAME_CLOCK_SECONDS_PER_HOUR);
    Error_Deconstruct(&Result); // a whole hour of the clock's own day is always valid
    frame->_clock.IsPaused = true;
    LogClock(frame, what);
}

/* Jumps by a signed number of game seconds, across midnights (the scrub keys). The scrub stops at the calendar's
 * ends. */
static void ShiftClock(WorldTestFrame* frame, double gameSeconds)
{
    double Seconds = GameClock_GetSecondsOfDay(&frame->_clock) + gameSeconds;
    double Days = floor(Seconds / GAME_CLOCK_SECONDS_PER_DAY);
    Seconds -= Days * GAME_CLOCK_SECONDS_PER_DAY;
    if (Seconds >= GAME_CLOCK_SECONDS_PER_DAY)
    {
        Seconds = 0.0; // a tiny step back from midnight rounded up to the next one
        Days += 1.0;
    }
    if (Seconds < 0.0)
    {
        Seconds = 0.0;
    }
    Error Result = GameClock_SetDayAndTime(&frame->_clock, GameClock_GetDay(&frame->_clock) + (int64_t)Days, Seconds);
    Error_Deconstruct(&Result);
}

/* Moves the date by whole days and keeps the time of day (the day keys). */
static void ShiftDays(WorldTestFrame* frame, int64_t days, const char* what)
{
    Error Result = GameClock_SetDayAndTime(&frame->_clock, GameClock_GetDay(&frame->_clock) + days,
        GameClock_GetSecondsOfDay(&frame->_clock));
    if (Result.Code == ErrorCode_Success)
    {
        LogClock(frame, what);
    }
    else if (frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: the date cannot move there (%s).",
            (Result.Message != NULL) ? (const char*)Result.Message : "outside the calendar");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(&Result);
}

/* The clock's debug keys: P pause, 1-4 dawn/noon/dusk/midnight (paused), [ ] scrub, , . the previous and next day,
 * / a week ahead. Called from Update with the tick's input (what the UI above did not consume); @p deltaSeconds paces
 * the scrub. */
static void HandleDebugClockInput(WorldTestFrame* frame, const GameInput* input, float deltaSeconds)
{
    if (GameInput_IsKeyPressed(input, GameKey_P))
    {
        frame->_clock.IsPaused = !frame->_clock.IsPaused;
        LogClock(frame, frame->_clock.IsPaused ? "clock paused" : "clock running");
    }
    if (GameInput_IsKeyPressed(input, GameKey_1)) { JumpToHour(frame, DAWN_HOUR, "dawn (paused)"); }
    if (GameInput_IsKeyPressed(input, GameKey_2)) { JumpToHour(frame, NOON_HOUR, "noon (paused)"); }
    if (GameInput_IsKeyPressed(input, GameKey_3)) { JumpToHour(frame, DUSK_HOUR, "dusk (paused)"); }
    if (GameInput_IsKeyPressed(input, GameKey_4)) { JumpToHour(frame, MIDNIGHT_HOUR, "midnight (paused)"); }

    // [ and ] scrub the time while held, across midnights; the time is logged when the key is let go.
    double ScrubSeconds = TIME_SCRUB_RATE * GAME_CLOCK_SECONDS_PER_DAY * (double)deltaSeconds;
    if (GameInput_IsKeyDown(input, GameKey_LeftBracket)) { ShiftClock(frame, -ScrubSeconds); }
    if (GameInput_IsKeyDown(input, GameKey_RightBracket)) { ShiftClock(frame, ScrubSeconds); }
    if (GameInput_IsKeyReleased(input, GameKey_LeftBracket) || GameInput_IsKeyReleased(input, GameKey_RightBracket))
    {
        LogClock(frame, "time scrubbed");
    }

    // , and . step the date (the moon changes from night to night), / jumps a week ahead (about a quarter phase).
    if (GameInput_IsKeyPressed(input, GameKey_Comma)) { ShiftDays(frame, -1, "previous day"); }
    if (GameInput_IsKeyPressed(input, GameKey_Period)) { ShiftDays(frame, 1, "next day"); }
    if (GameInput_IsKeyPressed(input, GameKey_Slash)) { ShiftDays(frame, DAYS_PER_WEEK, "a week ahead"); }
}

/* The moon's and the seasons' debug keys: F pins the moon full (a story's fixed phase) or gives it back to the
 * calendar, Y switches the seasons. Each change is logged with the date, time and moon. */
static void HandleDebugSkyInput(WorldTestFrame* frame, const GameInput* input)
{
    WorldEnvironment* Environment = World_GetEnvironment(&frame->_world);
    if (GameInput_IsKeyPressed(input, GameKey_F))
    {
        bool IsFixed = (Environment->MoonPhaseSource == WorldMoonPhaseSource_Calendar);
        Environment->MoonPhaseSource = IsFixed ? WorldMoonPhaseSource_Fixed : WorldMoonPhaseSource_Calendar;
        Environment->MoonPhase = FIXED_MOON_PHASE;
        LogClock(frame, IsFixed ? "moon pinned full" : "moon back on the calendar");
    }
    if (GameInput_IsKeyPressed(input, GameKey_Y))
    {
        Environment->AreSeasonsEnabled = !Environment->AreSeasonsEnabled;
        LogClock(frame, Environment->AreSeasonsEnabled ? "seasons ON (the sun follows the date)" : "seasons OFF");
    }
}

// Static functions: the world file.
/* Loads the test world from <base>/worlds/test.world.json into @p world. A missing or broken file is logged once and
 * leaves an empty world instead (the debug grid shows that it is empty), so the frame still starts; only failing to
 * build even that is an error. @p outIsLoaded says which it was. */
static Error LoadTestWorld(World* world, Services* services, bool* outIsLoaded)
{
    *outIsLoaded = false;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    Error Result = (services->BaseDirectory != NULL)
        ? WorldFile_BuildPath(services->BaseDirectory, TEST_WORLD_FILE_NAME, WorldFileFormat_JSON, &Path)
        : Error_Construct2(ErrorCode_IllegalArgument, "The game has no base directory.");
    if (Result.Code == ErrorCode_Success)
    {
        WorldFileReadOptions Options = WorldFileReadOptions_Create(services->Keys, services->Assets, services->Logger);
        Result = WorldFile_LoadJSON(Path._data, &Options, world);
    }

    const char* PathText = (Path._count > 0U) ? (const char*)Path._data : "worlds/test.world.json";
    if (Result.Code == ErrorCode_Success)
    {
        *outIsLoaded = true;
        if (services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(services->Logger,
                (const unsigned char*)u8"WorldTest: loaded the test world from %s (%" PRIu64 " objects).", PathText,
                (uint64_t)World_GetObjectCount(world));
            Error_Deconstruct(&LogResult);
        }
    }
    else
    {
        if (services->Logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(services->Logger,
                (const unsigned char*)u8"WorldTest: could not load the test world (%s): %s The world is empty instead.",
                PathText, (Result.Message != NULL) ? (const char*)Result.Message : "no details.");
            Error_Deconstruct(&LogResult);
        }
        Error_Deconstruct(&Result);
        Result = World_Construct2(world, services->Keys, services->Assets, services->Logger);
    }
    Memory_Free(Path._data);
    return Result;
}

/* Saves the live world as worlds/test.saved.world.json or .world.ghdf (atomically) and logs where, or why not. */
static void SaveWorldAs(WorldTestFrame* frame, WorldFileFormat format)
{
    if (frame->_services->BaseDirectory == NULL)
    {
        return;
    }
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    Error Result = WorldFile_BuildPath(frame->_services->BaseDirectory, SAVED_WORLD_FILE_NAME, format, &Path);
    if (Result.Code == ErrorCode_Success)
    {
        Result = (format == WorldFileFormat_JSON) ? WorldFile_SaveJSON(&frame->_world, Path._data)
            : WorldFile_SaveGHDF(&frame->_world, Path._data);
    }
    if (frame->_services->Logger != NULL)
    {
        const char* PathText = (Path._count > 0U) ? (const char*)Path._data : "?";
        Error LogResult = (Result.Code == ErrorCode_Success)
            ? Logger_LogInfoFormatted(frame->_services->Logger, (const unsigned char*)u8"WorldTest: saved the world to %s.",
                PathText)
            : Logger_LogWarningFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: could not save the world to %s: %s", PathText,
                (Result.Message != NULL) ? (const char*)Result.Message : "no details.");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(&Result);
    Memory_Free(Path._data);
}


/* The renderer's and the sky's debug keys (A/B toggles, the light pollution steps, status lines), and F5 (save the
 * world). Called from Update with the tick's input (what the UI above did not consume). */
static void HandleDebugInput(WorldTestFrame* frame, const GameInput* input)
{
    WorldEnvironment* Environment = World_GetEnvironment(&frame->_world);

    // O toggles the whole post-effects pass (AO + outlines) for A/B-ing whether an artifact comes from it;
    // off = the scene blits straight to the tonemap, bit-exact with the pre-postfx pipeline.
    if (GameInput_IsKeyPressed(input, GameKey_O))
    {
        bool Enabled = !WorldRenderer_ArePostEffectsEnabled(frame->_renderer);
        WorldRenderer_SetPostEffectsEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: post effects (AO + outlines) %s.", Enabled ? "ON" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }

    // B toggles bloom independently (for A/B-ing the glow).
    if (GameInput_IsKeyPressed(input, GameKey_B))
    {
        bool Enabled = !WorldRenderer_IsBloomEnabled(frame->_renderer);
        WorldRenderer_SetBloomEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: bloom %s.", Enabled ? "ON" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }

    // G toggles the sun shafts (god rays) independently.
    if (GameInput_IsKeyPressed(input, GameKey_G))
    {
        bool Enabled = !WorldRenderer_AreSunshaftsEnabled(frame->_renderer);
        WorldRenderer_SetSunshaftsEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: sun shafts %s.", Enabled ? "ON" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }

    // V toggles the eye's night vision (dim light losing its colour as it does for the eye); off = the colourful night
    // a long-exposure photo shows. By day it changes nothing.
    if (GameInput_IsKeyPressed(input, GameKey_V))
    {
        bool Enabled = !WorldRenderer_IsNightVisionEnabled(frame->_renderer);
        WorldRenderer_SetNightVisionEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: night vision (dim light loses its colour, as for the eye) %s.",
                Enabled ? "ON" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }

    // K toggles sprite lighting: off draws every sprite unlit at its full colours (the look before sprites were lit),
    // for A/B-ing how they sit in the light at night and in shade.
    if (GameInput_IsKeyPressed(input, GameKey_K))
    {
        bool Enabled = !WorldRenderer_IsSpriteLightingEnabled(frame->_renderer);
        WorldRenderer_SetSpriteLightingEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: sprite lighting %s.", Enabled ? "ON" : "OFF (every sprite unlit)");
            Error_Deconstruct(&LogResult);
        }
    }

    // C toggles the crisp overlay (OmitPixelation objects rendered un-pixelated); off = they render pixelated.
    if (GameInput_IsKeyPressed(input, GameKey_C))
    {
        bool Enabled = !WorldRenderer_IsCrispOverlayEnabled(frame->_renderer);
        WorldRenderer_SetCrispOverlayEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: crisp overlay %s.", Enabled ? "ON" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }

    // N cycles the light pollution: 0 shows many faint stars and a bright Milky Way, 1 only a few bright stars, no
    // Milky Way and an orange glow along the horizon (look at night: 4, then P to let time run).
    if (GameInput_IsKeyPressed(input, GameKey_N))
    {
        Environment->LightPollution = NextLightPollutionStep(Environment->LightPollution);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: light pollution %.2f.", (double)Environment->LightPollution);
            Error_Deconstruct(&LogResult);
        }
    }

    // I logs what the renderer did in its last render (active / inactive / unavailable features, object counts),
    // e.g. to check on a new GPU which effects actually run, and the game time.
    if (GameInput_IsKeyPressed(input, GameKey_I))
    {
        WorldRenderer_LogStatus(frame->_renderer);
        LogClockStatus(frame);
    }

    // T toggles the text sample (checks font drawing, Latvian and Cyrillic glyphs, decorations, inline sprites).
    if (GameInput_IsKeyPressed(input, GameKey_T))
    {
        frame->_isTextSampleShown = !frame->_isTextSampleShown;
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: text sample %s.", frame->_isTextSampleShown ? "ON" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }

    // X steps the opacity of the centre cube and the tree: 1, 0.5 (a 50% dither on the pixel grid; their shadows thin,
    // their outlines switch off), 0 (gone, shadows too).
    if (GameInput_IsKeyPressed(input, GameKey_X))
    {
        StepTestOpacity(frame);
    }

    // U toggles frustum culling, for A/B-ing it: off draws everything within its draw distance (the image must not
    // change, only the frame time and the I key's counts).
    if (GameInput_IsKeyPressed(input, GameKey_U))
    {
        bool Enabled = !WorldRenderer_IsCullingEnabled(frame->_renderer);
        WorldRenderer_SetCullingEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: frustum culling %s.",
                Enabled ? "ON" : "OFF (everything within its draw distance is drawn)");
            Error_Deconstruct(&LogResult);
        }
    }

    // F5 saves the live world next to the test world, as JSON and GHDF, for round-trip checks: loaded back they are the
    // same world, and the JSON's diff against worlds/test.world.json shows only what changed in the game.
    if (GameInput_IsKeyPressed(input, GameKey_F5))
    {
        SaveWorldAs(frame, WorldFileFormat_JSON);
        SaveWorldAs(frame, WorldFileFormat_GHDF);
    }

    // L toggles the debug light gizmos (wireframe spheres at light positions).
    if (GameInput_IsKeyPressed(input, GameKey_L))
    {
        bool Enabled = !WorldRenderer_AreLightGizmosEnabled(frame->_renderer);
        WorldRenderer_SetLightGizmosEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: light gizmos %s.", Enabled ? "ON" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }
}

/* The tick's animation step: advances the animation playback of every sprite on an enabled object (whose ancestors are
 * enabled too) by the elapsed time (a no-op for empty / source-less instances). Best-effort: an advance error on one
 * sprite does not stop the others or the update tick. */
static void AdvanceSpriteAnimations(World* world, double deltaSeconds)
{
    size_t Count = 0;
    WorldComponent* const* Sprites = World_GetComponents(world, WorldComponentType_Sprite, &Count);
    for (size_t Index = 0; Index < Count; Index++)
    {
        if (!WorldObject_IsEffectivelyEnabled(WorldComponent_GetOwner(Sprites[Index])))
        {
            continue;
        }
        WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(Sprites[Index]);
        Error UpdateResult = SpriteAnimationInstance_Update(WorldSpriteComponent_GetAnimationInstance(Sprite),
            deltaSeconds);
        Error_Deconstruct(&UpdateResult);
    }
}


// Static functions: the world models (T10).
/* Gives one variant cube the variant of the current step (NULL: none), through the component's setters, so the
 * renderer's cache resolves the variant again. */
static void ApplyTestVariant(WorldTestFrame* frame, uint64_t objectId, const char* variant)
{
    WorldObject* Object = NULL;
    if ((objectId == 0U) || !World_TryGetObject(&frame->_world, objectId, &Object))
    {
        return;
    }
    WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(WorldObject_GetComponent(Object,
        WorldComponentType_Model));
    if (ModelComponent == NULL)
    {
        return;
    }
    Error Result = Error_CreateSuccess();
    if (variant != NULL)
    {
        Result = WorldModelComponent_SetVariant(ModelComponent, (const unsigned char*)variant);
    }
    Error_Deconstruct(&Result); // every step's name is valid
    Result = WorldModelComponent_SetUsesVariant(ModelComponent, variant != NULL);
    Error_Deconstruct(&Result);
}

/* The world models' debug keys: E steps the variant cubes through their variants (their own, swapped, none); Z toggles
 * a zoomed camera, whose draw distances reach farther. */
static void HandleDebugModelInput(WorldTestFrame* frame, const GameInput* input)
{
    Logger* Log = frame->_services->Logger;
    if (GameInput_IsKeyPressed(input, GameKey_E))
    {
        frame->_variantStep = (frame->_variantStep + 1U) % VARIANT_STEP_COUNT;
        const char* const* Variants = VariantSteps[frame->_variantStep];
        for (size_t Index = 0U; Index < VARIANT_TEST_OBJECT_COUNT; Index++)
        {
            ApplyTestVariant(frame, frame->_variantTestObjectIds[Index], Variants[Index]);
        }
        if (Log != NULL)
        {
            Error LogResult = (Variants[0] != NULL)
                ? Logger_LogInfoFormatted(Log, (const unsigned char*)u8"WorldTest: the variant cubes show their %s and "
                    u8"%s variants.", Variants[0], Variants[1])
                : Logger_LogInfo(Log, (const unsigned char*)u8"WorldTest: the variant cubes show their model's own "
                    u8"materials (no variant).");
            Error_Deconstruct(&LogResult);
        }
    }
    if (GameInput_IsKeyPressed(input, GameKey_Z))
    {
        frame->_isZoomed = !frame->_isZoomed;
        frame->_camera.FovY = frame->_isZoomed ? ZOOMED_FOV_Y : GAME_CAMERA_DEFAULT_FOV_Y;
        if (Log != NULL)
        {
            const char* What = frame->_isZoomed
                ? "zoomed in: the draw distances of models and sprites reach about 4.4 times as far" : "zoom off";
            Error LogResult = Logger_LogInfoFormatted(Log,
                (const unsigned char*)u8"WorldTest: %s (a %.0f-degree view).", What, (double)frame->_camera.FovY);
            Error_Deconstruct(&LogResult);
        }
    }
}


// Static functions: the desk (the test world's hierarchy).
/* Logs a line about the desk. */
static void LogDesk(WorldTestFrame* frame, const char* message)
{
    if (frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger, (const unsigned char*)u8"WorldTest: %s",
            message);
        Error_Deconstruct(&LogResult);
    }
}

/* The desk's debug keys: J starts and stops the desk sliding back and forth along X (its parts, the cup, the lamp and
 * the lamp's light follow); H hides and shows the desk, and with it everything on it (the lamp's light goes off). */
static void HandleDebugDeskInput(WorldTestFrame* frame, const GameInput* input)
{
    WorldObject* Desk = NULL;
    if ((frame->_deskId == 0U) || !World_TryGetObject(&frame->_world, frame->_deskId, &Desk))
    {
        return;
    }
    if (GameInput_IsKeyPressed(input, GameKey_J))
    {
        frame->_isDeskSliding = !frame->_isDeskSliding;
        LogDesk(frame, frame->_isDeskSliding
            ? "the desk slides along X; its parts, the cup, the lamp and the lamp's light follow."
            : "the desk stops.");
    }
    if (GameInput_IsKeyPressed(input, GameKey_H))
    {
        bool IsVisible = !WorldObject_IsVisible(Desk);
        Error Result = WorldObject_SetVisible(Desk, IsVisible);
        Error_Deconstruct(&Result); // cannot fail: the object exists
        LogDesk(frame, IsVisible ? "the desk is shown again, with everything on it."
            : "the desk is hidden, and with it everything on it (the lamp's light is off).");
    }
}

/* Moves a sliding desk on by the tick's real time: back and forth along X around where it was loaded, one period at a
 * time. Its children follow through the hierarchy. */
static void SlideDesk(WorldTestFrame* frame, double deltaSeconds)
{
    WorldObject* Desk = NULL;
    if (!frame->_isDeskSliding || (frame->_deskId == 0U) || !World_TryGetObject(&frame->_world, frame->_deskId, &Desk))
    {
        return;
    }
    frame->_deskSlideSeconds = fmod(frame->_deskSlideSeconds + deltaSeconds, DESK_SLIDE_PERIOD_SECONDS);
    double Turn = (2.0 * PI * frame->_deskSlideSeconds) / DESK_SLIDE_PERIOD_SECONDS;
    Vector3 Position = WorldObject_GetPosition(Desk);
    Position.x = frame->_deskStartX + (DESK_SLIDE_DISTANCE * (float)sin(Turn));
    Error Result = WorldObject_SetPosition(Desk, Position);
    Error_Deconstruct(&Result); // cannot fail: the position is finite
}


// Static functions: the text sample.
/* Tears down the sample's layout, document and component factory (in that order: the layout points into the
 * document, and the document returns its tree to the factory). */
static void DeconstructTextSample(WorldTestFrame* frame)
{
    Error LayoutResult = TextLayout_Deconstruct(&frame->_textSampleLayout);
    Error_Deconstruct(&LayoutResult);
    Error DocumentResult = TextDocument_Deconstruct(&frame->_textSample);
    Error_Deconstruct(&DocumentResult);
    Error FactoryResult = TextComponentFactory_Deconstruct(&frame->_textFactory);
    Error_Deconstruct(&FactoryResult);
}

/* Parses the sample JSON into the sample document. A failure is a content problem of a debug feature: it is logged
 * once and the sample stays empty. */
static void LoadTextSample(WorldTestFrame* frame, Services* services)
{
    JSONObjectPool* Pool = NULL;
    GenericBuffer Source;
    GenericBuffer_AllocateVariable(&Source, 0U, sizeof(unsigned char));
    JSONObjectValue Value = JSONObjectValue_CreateNull();
    TextComponent* Root = NULL;

    Error Result = JSONObjectPool_Create(&Pool);
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AppendString(&Source, (const unsigned char*)TEXT_SAMPLE_JSON))
    {
        Result = Error_Construct2(ErrorCode_BufferTooLarge, "could not copy the sample text.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSON_Deserialize(Pool, &Source, &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = TextComponentJSON_Deserialize(&Value, &frame->_textFactory, &Root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = TextDocument_SetRoot(&frame->_textSample, Root);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if ((Root != NULL) && (TextDocument_GetRoot(&frame->_textSample) != Root))
        {
            Error ReturnResult = TextComponentFactory_ReturnTree(&frame->_textFactory, Root);
            Error_Deconstruct(&ReturnResult);
        }
        if (services->Logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(services->Logger,
                (const unsigned char*)u8"WorldTest: the text sample could not be loaded (%s).",
                (Result.Message != NULL) ? (const char*)Result.Message : "no details");
            Error_Deconstruct(&LogResult);
        }
        Error_Deconstruct(&Result);
    }

    if (Pool != NULL)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Value);
        Error_Deconstruct(&ReturnResult);
        Error PoolResult = JSONObjectPool_Deconstruct(Pool);
        Error_Deconstruct(&PoolResult);
    }
    Memory_Free(Source._data);
}

/* Builds the text sample's factory, document and layout, then loads the sample. Only failing to build those is an
 * error; a sample that does not load leaves an empty document. */
static Error ConstructTextSample(WorldTestFrame* frame, Services* services)
{
    Error Result = TextComponentFactory_Construct(&frame->_textFactory);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = TextDocument_Construct(&frame->_textSample, &frame->_textFactory, services->Assets, services->Logger);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextComponentFactory_Deconstruct(&frame->_textFactory);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    Result = TextDocument_SetDefaultFontName(&frame->_textSample, TEXT_SAMPLE_FONT_NAME);
    if (Result.Code == ErrorCode_Success)
    {
        Result = TextLayout_Construct(&frame->_textSampleLayout);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextDocument_Deconstruct(&frame->_textSample);
        Error_Deconstruct(&Cleanup);
        Cleanup = TextComponentFactory_Deconstruct(&frame->_textFactory);
        Error_Deconstruct(&Cleanup);
        return Result;
    }

    LoadTextSample(frame, services);
    frame->_isTextSampleShown = true;
    return Error_CreateSuccess();
}


// Static functions: controls.
/* Captures or frees the mouse cursor (mouse look needs it captured; the UI above needs it free). */
static void SetCursorCaptured(WorldTestFrame* frame, bool isCaptured)
{
    if (isCaptured == frame->_cursorCaptured)
    {
        return;
    }
    if (isCaptured)
    {
        DisableCursor();
    }
    else
    {
        EnableCursor();
    }
    frame->_cursorCaptured = isCaptured;
}

/* The frame's own controls, from what the UI above left unconsumed: quitting, the mouse, the flycam. */
static void HandleControls(WorldTestFrame* frame, GameInput* input, float deltaSeconds)
{
    // An unconsumed pause (Escape when the UI is not using it) quits this test.
    if (GameInput_IsActionPressed(input, GameAction_Pause))
    {
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfo(frame->_services->Logger, (const unsigned char*)u8"WorldTest: quitting.");
            Error_Deconstruct(&LogResult);
        }
        GameFrameManager_RequestStop(frame->_services->FrameManager);
    }

    // M frees the mouse for the UI; a click the UI did not take captures it again.
    if (GameInput_IsKeyPressed(input, GameKey_M))
    {
        SetCursorCaptured(frame, !frame->_cursorCaptured);
    }
    else if (!frame->_cursorCaptured && GameInput_IsKeyPressed(input, GameKey_MouseLeft))
    {
        GameInput_ConsumeKey(input, GameKey_MouseLeft);
        SetCursorCaptured(frame, true);
    }

    // Mouse look (only while captured). Input is polled once per update tick, so this is the tick's movement.
    if (frame->_cursorCaptured)
    {
        Vector2 MouseDelta = GameInput_GetMouseDelta(input);
        // Mouse-right must turn the view right: increasing yaw rotates forward toward +X, which renders on
        // the screen's left under the right-handed view, so a rightward mouse delta lowers yaw.
        frame->_camera.Yaw -= MouseDelta.x * MOUSE_SENSITIVITY;
        frame->_camera.Pitch -= MouseDelta.y * MOUSE_SENSITIVITY;
        frame->_camera.Pitch = Clamp(frame->_camera.Pitch, -GAME_CAMERA_MAX_PITCH, GAME_CAMERA_MAX_PITCH);
    }

    float Speed = GameInput_IsActionDown(input, GameAction_Sprint) ? (MOVE_SPEED * SPRINT_MULTIPLIER) : MOVE_SPEED;
    Vector3 Forward = GameCamera_GetForward(&frame->_camera);
    Vector3 Right = GameCamera_GetRight(&frame->_camera);
    Vector3 Movement = { .x = 0.0f, .y = 0.0f, .z = 0.0f };
    if (GameInput_IsActionDown(input, GameAction_MoveForward)) { Movement = Vector3Add(Movement, Forward); }
    if (GameInput_IsActionDown(input, GameAction_MoveBack)) { Movement = Vector3Subtract(Movement, Forward); }
    if (GameInput_IsActionDown(input, GameAction_MoveRight)) { Movement = Vector3Add(Movement, Right); }
    if (GameInput_IsActionDown(input, GameAction_MoveLeft)) { Movement = Vector3Subtract(Movement, Right); }
    if (GameInput_IsActionDown(input, GameAction_MoveUp)) { Movement = Vector3Add(Movement, FrameWorldUp); }
    if (GameInput_IsActionDown(input, GameAction_MoveDown)) { Movement = Vector3Subtract(Movement, FrameWorldUp); }
    if ((Movement.x != 0.0f) || (Movement.y != 0.0f) || (Movement.z != 0.0f))
    {
        Movement = Vector3Scale(Vector3Normalize(Movement), Speed * deltaSeconds);
        frame->_camera.Position = Vector3Add(frame->_camera.Position, Movement);
    }

    // Debug controls (edge-triggered keys are reliable here: input is polled per update tick).
    HandleDebugClockInput(frame, input, deltaSeconds);
    HandleDebugSkyInput(frame, input);
    HandleDebugInput(frame, input);
    HandleDebugDeskInput(frame, input);
    HandleDebugModelInput(frame, input);
}


// Static functions: vtable behavior.
static Error WorldTestFrame_Start(void* self, ProgramTime time)
{
    UNUSED(time);
    WorldTestFrame* Frame = self;

    SetCursorCaptured(Frame, true);

    if (Frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfo(Frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: entered the world. WASD+SPACE/CTRL move, SHIFT sprint, mouse look; "
            u8"P pause/resume the clock, 1-4 = dawn/noon/dusk/midnight (paused), [ ] scrub time, , . previous/next "
            u8"day, / a week ahead; F full moon / calendar moon, Y seasons; O post effects, B bloom, G sun shafts, "
            u8"C crisp overlay, V night vision, K sprite lighting, L light gizmos, N light pollution, X opacity of the "
            u8"centre cube and the tree, U frustum culling, J slide the desk, H hide the desk, I renderer status and game "
            u8"time, T text sample, F5 save the world (worlds/test.saved.world.json and .ghdf); M frees the "
            u8"mouse for the UI (click the world to capture it again); ESC quits (once the UI lets go of it).");
        Error_Deconstruct(&LogResult);
    }
    return Error_CreateSuccess();
}

static Error WorldTestFrame_End(void* self, ProgramTime time)
{
    UNUSED(time);
    SetCursorCaptured(self, false);
    return Error_CreateSuccess();
}

static Error WorldTestFrame_Update(void* self, ProgramTime time)
{
    WorldTestFrame* Frame = self;
    float Delta = (float)time.PassedTime;

    if (Frame->_services->Input != NULL)
    {
        HandleControls(Frame, Frame->_services->Input, Delta);
    }

    // Game time moves (unless the clock is paused), and the sky shows it.
    GameClockCrossings Crossings;
    GameClock_Advance(&Frame->_clock, time.PassedTime, &Crossings);
    if (Crossings.Midnights > 0)
    {
        LogClock(Frame, "a new day");
    }
    ApplyClockToEnvironment(Frame);

    // Advance sprite animations (game logic; the renderer only reads the current frame), and slide the desk.
    AdvanceSpriteAnimations(&Frame->_world, time.PassedTime);
    SlideDesk(Frame, time.PassedTime);

    // The world matrices of everything that moved this tick, so the renderer reads them cached.
    World_UpdateTransforms(&Frame->_world);

    // The text sample animates its sprite and is laid out here (laying out loads its fonts); Render only draws it.
    Error TextResult = TextDocument_Update(&Frame->_textSample, time);
    if ((TextResult.Code == ErrorCode_Success) && Frame->_isTextSampleShown)
    {
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        TextResult = TextLayout_Refresh(&Frame->_textSampleLayout, &Frame->_textSample, &Options);
    }
    return TextResult;
}

static Error WorldTestFrame_BeginLoad(void* self)
{
    WorldTestFrame* Frame = self;
    return WorldRenderer_PrepareWorld(Frame->_renderer, &Frame->_world);
}

static Error WorldTestFrame_LoadStep(void* self)
{
    UNUSED(self);
    return Error_CreateSuccess();
}

static bool WorldTestFrame_IsLoaded(void* self)
{
    UNUSED(self);
    return true;
}

static Error WorldTestFrame_BeginUnload(void* self)
{
    UNUSED(self);
    return Error_CreateSuccess();
}

static Error WorldTestFrame_UnloadStep(void* self)
{
    UNUSED(self);
    return Error_CreateSuccess();
}

static bool WorldTestFrame_IsUnloaded(void* self)
{
    UNUSED(self);
    return true;
}

static Error WorldTestFrame_Render(void* self, const FrameRenderContext* context, RenderTexture2D target)
{
    WorldTestFrame* Frame = self;
    // All input (movement, mouse look, debug keys) is handled in Update, where input is polled per tick.
    // The render tick's real frame delta paces the renderer's HDR eye adaptation.
    Error Result = WorldRenderer_RenderToTarget(Frame->_renderer, &Frame->_world, &Frame->_camera,
        context->Time.PassedTime, target);
    if ((Result.Code != ErrorCode_Success) || !Frame->_isTextSampleShown)
    {
        return Result;
    }

    // The text sample goes over the final image, at the window's resolution.
    RenderContext TextContext;
    RenderContext_Create(&TextContext, &target, context->TargetAspectRatio, context->TargetAreaPosition);
    RenderContext_BeginRendering(&TextContext);
    TextLayoutDrawArguments Arguments = TextLayoutDrawArguments_Create(RenderVector2D_Fitted((Vector2)
    {
        .x = TEXT_SAMPLE_POSITION_X,
        .y = TEXT_SAMPLE_POSITION_Y
    }));
    Result = TextLayout_Draw(&Frame->_textSampleLayout, &TextContext, &Arguments);
    RenderContext_EndRendering(&TextContext);
    RenderContext_Deconstruct(&TextContext);
    return Result;
}

static void WorldTestFrame_Destroy(void* self)
{
    WorldTestFrame* Frame = self;

    SetCursorCaptured(Frame, false);

    if (Frame->_renderer != NULL)
    {
        Error RendererResult = WorldRenderer_Deconstruct(Frame->_renderer);
        Error_Deconstruct(&RendererResult);
    }

    Error WorldResult = World_Deconstruct(&Frame->_world);
    Error_Deconstruct(&WorldResult);
    GameClock_Deconstruct(&Frame->_clock);
    DeconstructTextSample(Frame);

    GameFrame_Deconstruct(&Frame->Base);
    Memory_Free(Frame);
}


// Fields.
static const GameFrameVTable WorldTestFrameVTable =
{
    .Start = WorldTestFrame_Start,
    .End = WorldTestFrame_End,
    .Update = WorldTestFrame_Update,
    .BeginLoad = WorldTestFrame_BeginLoad,
    .LoadStep = WorldTestFrame_LoadStep,
    .IsLoaded = WorldTestFrame_IsLoaded,
    .BeginUnload = WorldTestFrame_BeginUnload,
    .UnloadStep = WorldTestFrame_UnloadStep,
    .IsUnloaded = WorldTestFrame_IsUnloaded,
    .Render = WorldTestFrame_Render,
    .GetLoadProgress = NULL,
    .OnResize = NULL,
    .Destroy = WorldTestFrame_Destroy
};


// Public functions.
Error WorldTestFrame_Create(Services* services, GameFrame** outFrame)
{
    if ((services == NULL) || (outFrame == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldTestFrame_Create: services and outFrame must not be NULL.");
    }
    *outFrame = NULL;

    WorldTestFrame* Frame = Memory_Allocate(sizeof(WorldTestFrame));

    Error BaseResult = GameFrame_Construct(&Frame->Base, &WorldTestFrameVTable, FRAME_DEBUG_NAME);
    if (BaseResult.Code != ErrorCode_Success)
    {
        Memory_Free(Frame);
        return BaseResult;
    }

    bool IsTestWorldLoaded = false;
    Error WorldResult = LoadTestWorld(&Frame->_world, services, &IsTestWorldLoaded);
    if (WorldResult.Code != ErrorCode_Success)
    {
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return WorldResult;
    }

    GameClock_Construct(&Frame->_clock);
    Error ClockResult = StartClock(&Frame->_clock, World_GetEnvironment(&Frame->_world));
    if (ClockResult.Code != ErrorCode_Success)
    {
        GameClock_Deconstruct(&Frame->_clock);
        Error DeconstructResult = World_Deconstruct(&Frame->_world);
        Error_Deconstruct(&DeconstructResult);
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return ClockResult;
    }
    ApplyClockToEnvironment(Frame);

    Error RendererResult = WorldRenderer_Create(services->Assets, services->Logger, services->Config, &Frame->_renderer);
    if (RendererResult.Code != ErrorCode_Success)
    {
        GameClock_Deconstruct(&Frame->_clock);
        Error DeconstructResult = World_Deconstruct(&Frame->_world);
        Error_Deconstruct(&DeconstructResult);
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return RendererResult;
    }

    Error TextResult = ConstructTextSample(Frame, services);
    if (TextResult.Code != ErrorCode_Success)
    {
        Error Cleanup = WorldRenderer_Deconstruct(Frame->_renderer);
        Error_Deconstruct(&Cleanup);
        Cleanup = World_Deconstruct(&Frame->_world);
        Error_Deconstruct(&Cleanup);
        GameClock_Deconstruct(&Frame->_clock);
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return TextResult;
    }

    Frame->_services = services;
    for (size_t Index = 0U; Index < OPACITY_TEST_OBJECT_COUNT; Index++)
    {
        Frame->_opacityTestObjectIds[Index] = FindObjectId(&Frame->_world, OpacityTestObjectNames[Index]);
    }
    Frame->_opacityStep = 0U;
    Frame->_deskId = FindObjectId(&Frame->_world, (const char*)TEST_DESK_OBJECT_NAME);
    WorldObject* Desk = NULL;
    Frame->_deskStartX = World_TryGetObject(&Frame->_world, Frame->_deskId, &Desk) ? WorldObject_GetPosition(Desk).x : 0.0f;
    Frame->_isDeskSliding = false;
    Frame->_deskSlideSeconds = 0.0;
    for (size_t Index = 0U; Index < VARIANT_TEST_OBJECT_COUNT; Index++)
    {
        Frame->_variantTestObjectIds[Index] = FindObjectId(&Frame->_world, VariantTestObjectNames[Index]);
    }
    Frame->_variantStep = 0U;
    Frame->_isZoomed = false;
    if (!IsTestWorldLoaded)
    {
        WorldRenderer_SetDebugGridEnabled(Frame->_renderer, true); // an empty world shows the grid, so it is visible
    }
    Frame->_camera = GameCamera_Create((Vector3){ .x = 0.0f, .y = 2.5f, .z = -7.0f }, GAME_CAMERA_DEFAULT_FOV_Y);
    Frame->_camera.Pitch = -0.2f;
    Frame->_cursorCaptured = false;

    *outFrame = &Frame->Base;
    return Error_CreateSuccess();
}
