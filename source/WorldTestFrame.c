#include <math.h>
#include "WorldTestFrame.h"
#include "GameClock.h"
#include "GameFrame.h"
#include "Services.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "WorldLightComponent.h"
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


// Macros.
/** Debug name used in log/crash messages. */
#define FRAME_DEBUG_NAME ((const unsigned char*)u8"WorldTest")
/** Asset name of the test model placed at the world centre. */
#define TEST_MODEL_ASSET_NAME ((const unsigned char*)u8"test")
/** Name given to the test model object. */
#define TEST_MODEL_OBJECT_NAME ((const unsigned char*)u8"test_object")
/** Name given to the ground-slab object (a flattened test model that receives the sun shadow). */
#define TEST_GROUND_OBJECT_NAME ((const unsigned char*)u8"ground")
/** Name given to the crisp (OmitPixelation) demo object — renders un-pixelated to stand in for a screen. */
#define TEST_CRISP_OBJECT_NAME ((const unsigned char*)u8"crisp_object")
/** Pixels per metre of the test image (a 1920x1080 screenshot): 1.5 metres wide, as its billboard always was. */
#define TEST_IMAGE_PIXELS_PER_UNIT 1280.0f
/** Demo of per-material PBR MAPS: a cube whose material carries albedo/normal/ORM/emissive textures. */
#define TEST_PBR_DEMO_OBJECT_NAME ((const unsigned char*)u8"pbr_demo_object")
#define TEST_PBR_DEMO_ASSET_NAME ((const unsigned char*)u8"pbr_demo")
/** Demo of per-material PBR scalar VALUES (no maps): a polished gold cube (metallic 1, low roughness). */
#define TEST_PBR_METAL_OBJECT_NAME ((const unsigned char*)u8"pbr_metal_object")
#define TEST_PBR_METAL_ASSET_NAME ((const unsigned char*)u8"pbr_metal")

/** Base movement speed, in world units per second. */
#define MOVE_SPEED 6.0f
/** Multiplier applied to movement speed while the sprint key is held. */
#define SPRINT_MULTIPLIER 3.0f
/** Mouse look sensitivity, in radians per pixel of mouse movement. */
#define MOUSE_SENSITIVITY 0.0025f
/** The test world's clock speed in game seconds per real second: a 2-minute day, short so the cycle is easy to
 *  observe (the clock's own default is 72, a 20-minute day). */
#define TEST_TIME_SCALE 720.0
/** Sun-arc tilt from the zenith at noon for the test world, in radians (~23 degrees). */
#define TEST_SUN_ANGLE 0.4f
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
/* One sprite of the test world: its object, its animation and the settings that differ from the defaults. */
typedef struct TestSpriteStruct
{
    const char* ObjectName;
    const char* Animation;
    Vector3 Position;
    /** Euler rotation in degrees (X, then Y, then Z). */
    Vector3 RotationDegrees;
    /** The object's uniform scale. */
    float Scale;
    WorldSpriteOrientation Orientation;
    WorldSpriteAlphaMode AlphaMode;
    Vector2 Pivot;
    float PixelsPerUnit;
    bool IsLit;
    float Emission;
    bool IsDecal;
} TestSprite;

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
} WorldTestFrame;


// Fields: the test world's sprites (T06): one of each kind, placed so the camera's start view shows them.
static const TestSprite TestSprites[] =
{
    // The original test sprite: a camera-facing billboard of an opaque screenshot, 1.5 m wide.
    { "sprite_object", "test_sprite", { -3.0f, 1.5f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f,
        WorldSpriteOrientation_Billboard, WorldSpriteAlphaMode_Cutout, { 0.5f, 0.5f }, TEST_IMAGE_PIXELS_PER_UNIT, true,
        1.0f, false },
    // A blended tire-mark decal lying on the ground slab (its top is at y 0.05), turned 25 degrees.
    { "tire_mark", "test_tire_mark", { -1.0f, 0.05f, -3.0f }, { -90.0f, 25.0f, 0.0f }, 1.0f,
        WorldSpriteOrientation_Fixed, WorldSpriteAlphaMode_Blend, { 0.5f, 0.5f }, 32.0f, true, 1.0f, true },
    // An axis-billboard tree standing on its bottom centre; it casts a shadow that faces the light.
    { "tree", "test_tree", { -6.0f, 0.05f, 4.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f, WorldSpriteOrientation_AxisBillboard,
        WorldSpriteAlphaMode_Cutout, { 0.5f, 0.0f }, 32.0f, true, 1.0f, false },
    // A fixed sign behind the tree, turned toward the start view: the tree's transparent texels show it.
    { "sign", "test_sign", { -8.5f, 0.05f, 7.0f }, { 0.0f, 200.0f, 0.0f }, 1.0f, WorldSpriteOrientation_Fixed,
        WorldSpriteAlphaMode_Cutout, { 0.5f, 0.0f }, 32.0f, true, 1.0f, false },
    // A blended, lit smoke puff in front of the warm lamp's glow (the two blend over each other).
    { "smoke", "test_smoke", { 1.5f, 1.95f, -1.3f }, { 0.0f, 0.0f, 0.0f }, 2.0f, WorldSpriteOrientation_Billboard,
        WorldSpriteAlphaMode_Blend, { 0.5f, 0.5f }, 32.0f, true, 1.0f, false },
    // An additive, unlit glow around the warm lamp.
    { "lamp_glow", "test_glow", { 2.5f, 1.6f, 2.5f }, { 0.0f, 0.0f, 0.0f }, 2.0f, WorldSpriteOrientation_Billboard,
        WorldSpriteAlphaMode_Additive, { 0.5f, 0.5f }, 32.0f, false, 2.0f, false },
    // An unlit neon sign facing the start view: as bright at night as by day.
    { "neon_sign", "test_neon", { 4.5f, 2.2f, 4.5f }, { 0.0f, 180.0f, 0.0f }, 1.0f, WorldSpriteOrientation_Fixed,
        WorldSpriteAlphaMode_Cutout, { 0.5f, 0.5f }, 32.0f, false, 3.0f, false },
};


// Static functions: the world-up direction for vertical movement.
static const Vector3 FrameWorldUp = { .x = 0.0f, .y = 1.0f, .z = 0.0f };
/* The light pollution steps the N key cycles through: a dark site, a small town (the default), a suburb, a city. */
static const float LightPollutionSteps[] = { 0.0f, 0.35f, 0.7f, 1.0f };
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

/* The renderer's and the sky's debug keys (A/B toggles, the light pollution steps, status lines). Called from Update
 * with the tick's input (what the UI above did not consume). */
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

/* The tick's animation step: advances the animation playback of every sprite on an enabled object by the elapsed
 * time (a no-op for empty / source-less instances). Best-effort: an advance error on one sprite does not stop the
 * others or the update tick. */
static void AdvanceSpriteAnimations(World* world, double deltaSeconds)
{
    size_t Count = 0;
    WorldComponent* const* Sprites = World_GetComponents(world, WorldComponentType_Sprite, &Count);
    for (size_t Index = 0; Index < Count; Index++)
    {
        if (!WorldObject_IsEnabled(WorldComponent_GetOwner(Sprites[Index])))
        {
            continue;
        }
        WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(Sprites[Index]);
        Error UpdateResult = SpriteAnimationInstance_Update(WorldSpriteComponent_GetAnimationInstance(Sprite),
            deltaSeconds);
        Error_Deconstruct(&UpdateResult);
    }
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
            u8"C crisp overlay, V night vision, L light gizmos, N light pollution, I renderer status and game time, T text "
            u8"sample; M frees the mouse for the UI (click the world to capture it again); ESC quits (once the UI lets go "
            u8"of it).");
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

    // Advance sprite animations (game logic; the renderer only reads the current frame).
    AdvanceSpriteAnimations(&Frame->_world, time.PassedTime);

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


// Static functions: world construction.
/* Creates an object at @p position with @p scale and gives it a component of @p type. The world owns both at once,
 * so on a failure the caller tears the whole world down. */
static Error AddObject(World* world, const unsigned char* name, Vector3 position, Vector3 scale,
    WorldComponentType type, WorldComponent** outComponent)
{
    WorldObject* Object = NULL;
    Error Result = World_CreateObject(world, name, &Object);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldObject_SetPosition(Object, position);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldObject_SetScale(Object, scale);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = World_AddComponent(world, WorldObject_GetId(Object), type, outComponent);
    }
    return Result;
}

/* Creates an object drawing the named model. */
static Error AddModelObject(World* world, const unsigned char* name, const unsigned char* assetName,
    Vector3 position, Vector3 scale, bool omitPixelation, WorldShadowTier shadowTier)
{
    WorldComponent* Component = NULL;
    Error Result = AddObject(world, name, position, scale, WorldComponentType_Model, &Component);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(Component);
    Result = WorldModelComponent_SetModel(ModelComponent, assetName);
    if (Result.Code == ErrorCode_Success)
    {
        // Crisp (un-pixelated) full-res overlay when true.
        Result = WorldModelComponent_SetOmitsPixelation(ModelComponent, omitPixelation);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldModelComponent_SetShadowTier(ModelComponent, shadowTier); // which shadow cascade(s) it casts into
    }
    return Result;
}

/* Creates an object drawing a test sprite: its animation, rotation and the settings that differ from the defaults. */
static Error AddSpriteObject(World* world, const TestSprite* sprite)
{
    WorldComponent* Component = NULL;
    Error Result = AddObject(world, (const unsigned char*)sprite->ObjectName, sprite->Position,
        (Vector3){ sprite->Scale, sprite->Scale, sprite->Scale }, WorldComponentType_Sprite, &Component);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WorldObject* Object = WorldComponent_GetOwner(Component);
    Vector3 Rotation = Vector3Scale(sprite->RotationDegrees, DEG2RAD);
    WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(Component);
    Result = WorldObject_SetRotation(Object, Rotation);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldSpriteComponent_SetSpriteAnimation(Sprite, (const unsigned char*)sprite->Animation);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldSpriteComponent_SetOrientation(Sprite, sprite->Orientation);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldSpriteComponent_SetAlphaMode(Sprite, sprite->AlphaMode);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldSpriteComponent_SetPivot(Sprite, sprite->Pivot);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldSpriteComponent_SetPixelsPerUnit(Sprite, sprite->PixelsPerUnit);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldSpriteComponent_SetLit(Sprite, sprite->IsLit);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldSpriteComponent_SetEmission(Sprite, sprite->Emission);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldSpriteComponent_SetDecal(Sprite, sprite->IsDecal);
    }
    return Result;
}

/* Creates an object carrying a point light with the given colour and reach. */
static Error AddPointLight(World* world, const unsigned char* name, Vector3 position, Color color,
    float intensity, float range, bool castsShadows)
{
    WorldComponent* Component = NULL;
    Error Result = AddObject(world, name, position, (Vector3){ 1.0f, 1.0f, 1.0f }, WorldComponentType_Light,
        &Component);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WorldLightComponent* Light = WorldLightComponent_FromComponent(Component);
    Result = WorldLightComponent_SetColor(Light, color);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldLightComponent_SetIntensity(Light, intensity);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldLightComponent_SetRange(Light, range);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldLightComponent_SetCastsShadows(Light, castsShadows);
    }
    return Result;
}

/* Builds the test world: a raised centre model that casts a shadow onto a wide ground slab, lit by the sun
 * plus a couple of coloured point lights near the centre model (to exercise point-light reach culling). The world
 * gets the asset manager so it loads its sprites' animations itself. */
static Error BuildTestWorld(World* world, Services* services)
{
    Error ConstructResult = World_Construct2(world, services->Keys, services->Assets, services->Logger);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        return ConstructResult;
    }

    // Where the sky starts. These live on the world's WorldEnvironment and are saved WITH the world (its property
    // table); the frame's game clock starts from the time of day and the date (the environment's default, 2026-10-18)
    // and writes them every tick from then on.
    WorldEnvironment* Environment = World_GetEnvironment(world);
    Environment->TimeOfDay = WORLD_ENVIRONMENT_TIME_OF_DAY_NOON;    // 0=midnight, 0.25=dawn, 0.5=noon, 0.75=dusk
    Environment->SunAngle = TEST_SUN_ANGLE;                        // radians the noon sun leans from straight up

    // Centre model, raised so its shadow lands on the ground; then a wide, thin slab as the ground receiver.
    // (Both cast into both shadow cascades by default.)
    Error Result = AddModelObject(world, TEST_MODEL_OBJECT_NAME, TEST_MODEL_ASSET_NAME,
        (Vector3){ 0.0f, 1.2f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, false, WorldShadowTier_Both);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddModelObject(world, TEST_GROUND_OBJECT_NAME, TEST_MODEL_ASSET_NAME,
            (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 40.0f, 0.2f, 40.0f }, false, WorldShadowTier_Both);
    }
    // A crisp (OmitPixelation) model to the side: it renders un-pixelated + full-res while the rest of the world
    // stays chunky — a stand-in for a readable in-world screen. Toggle it against pixelated with the C key.
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddModelObject(world, TEST_CRISP_OBJECT_NAME, TEST_MODEL_ASSET_NAME,
            (Vector3){ 3.0f, 1.2f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, true, WorldShadowTier_Both);
    }
    // Two coloured point lights flanking the centre cube (reach 10 units, so they light the cube + nearby
    // ground and fade out before the slab edges — exercises the per-object reach culling + forward shading).
    if (Result.Code == ErrorCode_Success)
    {
        // The warm light casts shadows (exercises the point-light cube shadow map); the cool one does not.
        Result = AddPointLight(world, (const unsigned char*)u8"warm_light",
            (Vector3){ 2.5f, 1.6f, 2.5f }, (Color){ 255, 150, 60, 255 }, 6.0f, 10.0f, true);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddPointLight(world, (const unsigned char*)u8"cool_light",
            (Vector3){ -2.5f, 1.2f, -2.0f }, (Color){ 80, 150, 255, 255 }, 6.0f, 10.0f, false);
    }
    // The sprites (TestSprites): the original test image, a decal, a tree, a sign, smoke, a glow and a neon sign.
    for (size_t Index = 0; (Index < (sizeof(TestSprites) / sizeof(TestSprites[0]))) && (Result.Code == ErrorCode_Success);
        Index++)
    {
        Result = AddSpriteObject(world, &TestSprites[Index]);
    }
    // Per-material PBR demos: a mapped cube (albedo/normal/ORM/emissive textures) on the left, and a scalar-
    // value gold metal cube (metallic 1, low roughness, no maps) on the right — A/B for the maps vs values path.
    // Shadow-tier demo: the mapped cube casts NEAR-only (its shadow vanishes once you fly far from it — a
    // detailed prop), the metal cube casts FAR-only (still shadows coarsely at a distance — a large object).
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddModelObject(world, TEST_PBR_DEMO_OBJECT_NAME, TEST_PBR_DEMO_ASSET_NAME,
            (Vector3){ -6.0f, 1.2f, 0.0f }, (Vector3){ 2.0f, 2.0f, 2.0f }, false, WorldShadowTier_Near);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddModelObject(world, TEST_PBR_METAL_OBJECT_NAME, TEST_PBR_METAL_ASSET_NAME,
            (Vector3){ 6.0f, 1.2f, 0.0f }, (Vector3){ 2.0f, 2.0f, 2.0f }, false, WorldShadowTier_Far);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error DeconstructResult = World_Deconstruct(world);
        Error_Deconstruct(&DeconstructResult);
        return Result;
    }

    return Error_CreateSuccess();
}


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

    Error WorldResult = BuildTestWorld(&Frame->_world, services);
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
    Frame->_camera = GameCamera_Create((Vector3){ .x = 0.0f, .y = 2.5f, .z = -7.0f }, GAME_CAMERA_DEFAULT_FOV_Y);
    Frame->_camera.Pitch = -0.2f;
    Frame->_cursorCaptured = false;

    *outFrame = &Frame->Base;
    return Error_CreateSuccess();
}
