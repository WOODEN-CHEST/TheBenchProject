#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldTestFrame.h"
#include "GameClock.h"
#include "GameFrame.h"
#include "Services.h"
#include "World.h"
#include "WorldCharacters.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldTestAnimation.h"
#include "WorldTestCharacters.h"
#include "WorldTestInstances.h"
#include "WorldTestShadows.h"
#include "WorldTestWeather.h"
#include "WorldFile.h"
#include "WorldObject.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "GameCamera.h"
#include "GameMath.h"
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
#include "wr/WRStopwatch.h"
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
/** The mouse wheel steps the flycam's speed: each notch multiplies it by the cube root of two, so this many notches
 *  double it. */
#define FLY_SPEED_STEPS_PER_DOUBLING 3
/** The slowest step: MOVE_SPEED / 16, under 0.4 m a second, for looking closely. */
#define FLY_SPEED_MIN_STEP (-12)
/** The fastest step: MOVE_SPEED x 1024, about 6 km a second, for crossing the far view quickly (the sprint key still
 *  multiplies it). */
#define FLY_SPEED_MAX_STEP 30
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
/** The morning mist keypad 7 lays down: its density per metre at its base and how fast it thins above it
 *  (ground_haze_density 0.02, ground_haze_thickness 15). */
#define TEST_MIST_DENSITY 0.02f
#define TEST_MIST_THICKNESS 15.0f
/** Room for one haze key's log line. */
#define HAZE_TEXT_CAPACITY ((size_t)160)
/** How far the camera's openness rays reach, in metres: past it everything counts as open sky. */
#define CAMERA_OPENNESS_REACH 30.0f
/** The objects whose opacity the X key steps: the centre cube (a model) and the tree (a cutout sprite), both casting
 *  shadows. */
#define OPACITY_TEST_OBJECT_COUNT 2U
/** The test world's hierarchy: the desk (an empty object holding its parts) and how the J key slides it along X:
 *  back and forth by this many metres either way around where it was loaded, once per period. */
#define TEST_DESK_OBJECT_NAME ((const unsigned char*)u8"desk")
/** The test world's door: the F6 key swings it open (a quarter turn about its hinge, toward +Z) and closed at
 *  DOOR_SWING_SPEED radians a second. It stops on the crate pinned in the corner in its way. */
#define TEST_DOOR_OBJECT_NAME "door"
#define DOOR_OPEN_TURN (-0.5f * PI)
#define DOOR_SWING_SPEED (PI / 3.0f)
/** The pushing wall: the F9 key sends it PUSHER_TRAVEL metres along +X at PUSHER_SPEED, through a row of five
 *  pushable crates that it pushes against a stop (where it is blocked and stops), and back; back where it started, the
 *  crates go back where they were (a teleport). */
#define TEST_PUSHER_OBJECT_NAME "pusher_wall"
#define TEST_ROW_CRATE_NAME_FORMAT "row_crate_%u"
#define ROW_CRATE_COUNT 5U
#define PUSHER_TRAVEL 2.5f
#define PUSHER_SPEED 0.6f
/** The crate the door stops on: the F10 key teleports it out of the door's way and back. */
#define TEST_DOOR_CRATE_OBJECT_NAME "door_crate"
#define DOOR_CRATE_ASIDE_SHIFT 1.2f
#define DESK_SLIDE_DISTANCE 1.2f
#define DESK_SLIDE_PERIOD_SECONDS 6.0
/** The test world's variant example: two cubes of the "painted_cube" world model, and how many looks the E key
 *  steps them through (VariantSteps). */
#define VARIANT_TEST_OBJECT_COUNT 2U
#define VARIANT_STEP_COUNT 3U
/** The vertical field of view the Z key zooms to, in degrees: about 4.4 times closer than the default 60, so models'
 *  draw distances reach 4.4 times as far. */
#define ZOOMED_FOV_Y 15.0f
/** How many places the F11 key's far shift steps through (FarShifts). */
#define FAR_SHIFT_COUNT 3U
/** Metres in a kilometre, for the far shift's log lines. */
#define METRES_PER_KILOMETRE 1000.0
/** Milliseconds in a second and nanoseconds in a millisecond, for the I key's frame times. */
#define MILLISECONDS_PER_SECOND 1000.0
#define NANOSECONDS_PER_MILLISECOND 1.0e6
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
    /** The flycam's speed in wheel notches from MOVE_SPEED (FLY_SPEED_STEPS_PER_DOUBLING notches to a doubling), from
     *  FLY_SPEED_MIN_STEP to FLY_SPEED_MAX_STEP. */
    int _flySpeedStep;
    /** Wheel movement short of a whole notch (smooth wheels and touchpads report fractions), kept for the next tick. */
    float _flyWheelRemainder;
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
    double _deskStartX;
    bool _isDeskSliding;
    double _deskSlideSeconds;
    /** The variant cubes the E key steps (VARIANT_TEST_OBJECT_COUNT; 0 for one the test world lacks), and the step of
     *  VariantSteps they are at. */
    uint64_t _variantTestObjectIds[VARIANT_TEST_OBJECT_COUNT];
    size_t _variantStep;
    /** Whether the Z key's zoom is on (the camera at ZOOMED_FOV_Y). */
    bool _isZoomed;
    /** The world's colliders, stepped every tick after the drivers move things; owned. Its trigger, contact and motion
     *  events are logged. */
    WorldCollision* _collision;
    /** Whether the F7 key's collider wireframes and the F8 key's contact points are shown. */
    bool _areCollidersShown;
    bool _areContactsShown;
    /** The door the F6 key swings (0 if the test world lacks it), its turn about Y when loaded (closed), and whether it is
     *  swinging open (else closed). */
    uint64_t _doorId;
    float _doorClosedYaw;
    bool _isDoorOpen;
    /** The pushing wall the F9 key sends out and back (0 if the test world lacks it), where along X it was loaded, and
     *  whether it is going out; the row's crates (0 for one the world lacks) and where they were loaded. */
    uint64_t _pusherId;
    double _pusherHomeX;
    bool _isPusherOut;
    bool _isPusherHome;
    uint64_t _rowCrateIds[ROW_CRATE_COUNT];
    Vector3Double _rowCrateHomes[ROW_CRATE_COUNT];
    /** The crate the door stops on, where it was loaded, and whether F10 has it aside. */
    uint64_t _doorCrateId;
    Vector3Double _doorCrateHome;
    bool _isDoorCrateAside;
    /** The world's characters, moved every tick after the other drivers; owned. */
    WorldCharacters* _characters;
    /** The test characters: walk mode (F1), the test NPCs (F2), the player's options (F3, F4) and the
     *  bodies' actions (E, 8, 9, 0); owned. */
    WorldTestCharacters _testCharacters;
    /** The animation system, ticked after the collision step's events, and its debug keys: the showcase's clips
     *  (5), the crowd (6), slow motion (7); owned. */
    WorldTestAnimation _testAnimation;
    /** The instanced copies' test content: the pebble field (the apostrophe) and the instancing A/B (the
     *  semicolon). */
    WorldTestInstances _testInstances;
    /** The shadows' test content: the far field (keypad +) and the shadow levels' debug view (keypad *). */
    WorldTestShadows _testShadows;
    /** The weather: the test world's climate run on the frame's clock, and its keys (Insert, Home, End, Page Up, Page
     *  Down, the backslash). */
    WorldTestWeather _testWeather;
    /** Where the F11 key's far shift has moved the test world, its characters and the camera: an index into FarShifts
     *  (0: not moved). */
    size_t _farShiftIndex;
    /** The last tick's update time, in milliseconds, for the I key's frame line (the frame time and the render's
     *  CPU time are in the renderer's status). */
    double _lastUpdateMilliseconds;
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
/* The far shift's offsets (FAR_SHIFT_COUNT), in the order the F11 key steps through them and back to the first: none,
 * 28 km from the world's origin (20 km along X and Z), and 141 km (100 km along X and -Z: the far-away tests' offset
 * along the ground). Out there nothing may shake or sink: the renderer draws relative to the camera, and positions are
 * doubles. */
static const Vector3Double FarShifts[FAR_SHIFT_COUNT] =
{
    { .X = 0.0, .Y = 0.0, .Z = 0.0 }, { .X = 20000.0, .Y = 0.0, .Z = 20000.0 },
    { .X = 100000.0, .Y = 0.0, .Z = -100000.0 }
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

/* Logs a line of the frame's, "WorldTest: <message>" (the atmosphere's keys, the desk, the door, the wireframes). */
static void LogLine(WorldTestFrame* frame, const char* message)
{
    if (frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger, (const unsigned char*)u8"WorldTest: %s",
            message);
        Error_Deconstruct(&LogResult);
    }
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

/* The I key's line about the collision world. */
static void LogCollisionStatus(WorldTestFrame* frame)
{
    if (frame->_services->Logger == NULL)
    {
        return;
    }
    WorldCollisionStats Stats = WorldCollision_GetStats(frame->_collision);
    Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
        (const unsigned char*)u8"WorldTest: collision: %" PRIu64 u8" colliders (%" PRIu64 u8" static, %" PRIu64
        u8" movable), %" PRIu64 u8" shapes (%" PRIu64 u8" mesh triangles, %" PRIu64 u8" infinite planes), %" PRIu64
        u8" trigger pairs; %" PRIu64 u8" rebuilds and %" PRIu64 u8" moves in %" PRIu64 u8" syncs.",
        (uint64_t)Stats.InstanceCount, (uint64_t)Stats.StaticInstanceCount, (uint64_t)Stats.MovableInstanceCount,
        (uint64_t)Stats.ShapeCount, (uint64_t)Stats.TriangleCount, (uint64_t)Stats.PlaneCount,
        (uint64_t)Stats.TriggerPairCount, Stats.RebuildCount, Stats.MoveCount, Stats.Tick);
    Error_Deconstruct(&LogResult);
    LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
        (const unsigned char*)u8"WorldTest: the last collision step: %" PRIu64 u8" movers, %" PRIu64 u8" pushables awake, %"
        PRIu64 u8" pairs of shapes, %" PRIu64 u8" touching contacts (%" PRIu64 u8" object pairs in contact), %u solver "
        u8"iterations in %u substeps (%u solved again), deepest overlap left %.2f mm, %" PRIu64 u8" blocked, %" PRIu64
        u8" crushed.", (uint64_t)Stats.MoverCount, (uint64_t)Stats.AwakeCount, (uint64_t)Stats.PairCount,
        (uint64_t)Stats.ContactCount, (uint64_t)Stats.ContactPairCount, Stats.SolverIterations, Stats.SubstepCount,
        Stats.ResolveCount, (double)(Stats.DeepestOverlap * 1000.0f), (uint64_t)Stats.BlockedCount,
        (uint64_t)Stats.CrushedCount);
    Error_Deconstruct(&LogResult);
}

/* The flycam's speed in metres a second before the sprint key: MOVE_SPEED stepped by the mouse wheel. */
static float FlySpeed(const WorldTestFrame* frame)
{
    return MOVE_SPEED * exp2f((float)frame->_flySpeedStep / (float)FLY_SPEED_STEPS_PER_DOUBLING);
}

/* The I key's camera line: where the camera is, how far from the world's origin, and the flycam's speed. */
static void LogCameraStatus(WorldTestFrame* frame)
{
    if (frame->_services->Logger == NULL)
    {
        return;
    }
    Vector3Double Position = frame->_camera.Position;
    Vector3Double Shift = FarShifts[frame->_farShiftIndex];
    Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
        (const unsigned char*)u8"WorldTest: the camera is at (%.3f, %.3f, %.3f), %.3f km from the world's origin; far "
        u8"shift (%.0f, %.0f, %.0f) m (F11); flycam speed %.2f m/s (the mouse wheel).", Position.X, Position.Y,
        Position.Z, Vector3Double_Length(Position) / METRES_PER_KILOMETRE, Shift.X, Shift.Y, Shift.Z,
        (double)FlySpeed(frame));
    Error_Deconstruct(&LogResult);
}

/* The I key's frame line: the time between the last two renders (the frame time: the GPU's share shows in it when it
 * is the slower), the last tick's update and the last render's CPU time. */
static void LogFrameTimes(WorldTestFrame* frame)
{
    if (frame->_services->Logger == NULL)
    {
        return;
    }
    WorldRendererStatus Status;
    WorldRenderer_GetStatus(frame->_renderer, &Status);
    double FramesPerSecond = (Status.FrameMilliseconds > 0.0) ? (MILLISECONDS_PER_SECOND / Status.FrameMilliseconds) : 0.0;
    Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
        (const unsigned char*)u8"WorldTest: the last frame took %.2f ms (%.1f a second; with vsync on it waits for the "
        u8"screen); its update %.2f ms and its render %.2f ms on the CPU (the GPU works on after it: its share shows in "
        u8"the frame time when it is the slower).", Status.FrameMilliseconds, FramesPerSecond,
        frame->_lastUpdateMilliseconds, Status.RenderMilliseconds);
    Error_Deconstruct(&LogResult);
}

/* Starts the clock at the test speed, where the world's sky starts: the environment's date and time of day. */
static Error StartClock(GameClock* clock, const WorldEnvironment* environment)
{
    // TODO: start the clock from the story's settings (its start date, time and speed) once stories have a manifest;
    // until then the world's environment is the starting point.
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

/* Logs the haze's debug view after keypad 4 or 5 changed it: the view, its slice and the slice's distance. */
static void LogHazeView(WorldTestFrame* frame)
{
    int Slice = 0;
    WorldRendererHazeView View = WorldRenderer_GetHazeView(frame->_renderer, &Slice);
    char Text[HAZE_TEXT_CAPACITY];
    double Distance = WorldRenderer_GetHazeSliceDistance(Slice);
    if (View == WorldRendererHazeView_Off)
    {
        snprintf(Text, sizeof(Text), "haze view off (the scene).");
    }
    else if (Distance > 0.0)
    {
        snprintf(Text, sizeof(Text), "haze view: the volume's %s at slice %d (%.1f m).",
            (const char*)WorldRenderer_GetHazeViewName(View), Slice, Distance);
    }
    else
    {
        snprintf(Text, sizeof(Text), "haze view: the volume's %s at slice %d (the whole ray, to the top of the "
            "atmosphere).", (const char*)WorldRenderer_GetHazeViewName(View), Slice);
    }
    LogLine(frame, Text);
}

/* The haze's keys: keypad 4 steps the debug view (off, a slice's in-scatter, its transmittance), keypad 5 the slice
 * it shows (wrapping), keypad 6 the environment's aerial_distance_scale (1, 5, 10), keypad 7 a morning mist (the ground
 * haze off or on), keypad 8 the haze strength (fog_strength 1 or 0: clear air). Each logged. */
static void HandleDebugHazeInput(WorldTestFrame* frame, const GameInput* input, WorldEnvironment* environment)
{
    int Slice = 0;
    WorldRendererHazeView View = WorldRenderer_GetHazeView(frame->_renderer, &Slice);
    if (GameInput_IsKeyPressed(input, GameKey_Keypad4))
    {
        WorldRenderer_SetHazeView(frame->_renderer,
            (WorldRendererHazeView)(((int)View + 1) % (int)WorldRendererHazeView_Count), Slice);
        LogHazeView(frame);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad5))
    {
        WorldRenderer_SetHazeView(frame->_renderer, View, (Slice + 1) % WORLD_RENDERER_HAZE_SLICE_COUNT);
        LogHazeView(frame);
    }
    char Text[HAZE_TEXT_CAPACITY];
    if (GameInput_IsKeyPressed(input, GameKey_Keypad6))
    {
        float Scale = environment->AerialDistanceScale;
        environment->AerialDistanceScale = (Scale < 5.0f) ? 5.0f : ((Scale < 10.0f) ? 10.0f : 1.0f);
        snprintf(Text, sizeof(Text), "aerial distance scale %g (every haze distance that many times longer).",
            (double)environment->AerialDistanceScale);
        LogLine(frame, Text);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad7))
    {
        bool IsOn = !(environment->GroundHazeDensity > 0.0f);
        environment->GroundHazeDensity = IsOn ? TEST_MIST_DENSITY : 0.0f;
        environment->GroundHazeThickness = TEST_MIST_THICKNESS;
        snprintf(Text, sizeof(Text), IsOn ? "morning mist ON (ground haze %g per metre at y %g, thinning by e every %g "
            "m)." : "morning mist OFF (no ground haze).", (double)environment->GroundHazeDensity,
            (double)environment->GroundHazeBase, (double)environment->GroundHazeThickness);
        LogLine(frame, Text);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad8))
    {
        environment->FogStrength = (environment->FogStrength > 0.0f) ? 0.0f : 1.0f;
        LogLine(frame, (environment->FogStrength > 0.0f) ? "haze strength 1 (the real air's haze)."
            : "haze strength 0 (no haze on anything: crisp to the horizon).");
    }
}

/* The atmosphere's keys: keypad 1 switches the tables off and on (the A/B against the direct march), keypad 2
 * the multiple scattering (1 or 0) and keypad 3 the ozone (1 or 0), so the sky's parts can be seen one at a time. Each
 * logged. Then the haze's (keypad 4 to 8). */
static void HandleDebugAtmosphereInput(WorldTestFrame* frame, const GameInput* input, WorldEnvironment* environment)
{
    // TODO: remove keypad 1 with the direct march once the owner approves the tables' sky.
    if (GameInput_IsKeyPressed(input, GameKey_Keypad1))
    {
        bool Enabled = !WorldRenderer_AreAtmosphereTablesEnabled(frame->_renderer);
        WorldRenderer_SetAtmosphereTablesEnabled(frame->_renderer, Enabled);
        LogLine(frame, Enabled ? "atmosphere tables ON (the sky and the far ground from the tables, with the light "
            "scattered more than once, and the haze)."
            : "atmosphere tables OFF (the direct march: single scattering per pixel, and no haze).");
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad2))
    {
        environment->MultipleScattering = (environment->MultipleScattering > 0.0f) ? 0.0f : 1.0f;
        LogLine(frame, (environment->MultipleScattering > 0.0f)
            ? "multiple scattering 1 (the tables draw the light scattered more than once)."
            : "multiple scattering 0 (single scattering only, as the direct march draws it).");
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad3))
    {
        environment->Ozone = (environment->Ozone > 0.0f) ? 0.0f : 1.0f;
        LogLine(frame, (environment->Ozone > 0.0f)
            ? "ozone 1 (Earth's: it keeps the twilight sky blue overhead)."
            : "ozone 0 (no ozone layer: a greyer twilight sky).");
    }
    HandleDebugHazeInput(frame, input, environment);
}

/* Interior lighting's keys: keypad 9 switches it on and off (it starts off: every surface takes the whole sky's light, no
 * ambient zone applies and the eye takes the sky's light as in the open), keypad 0 steps its debug view (off, the
 * ambient zones' wireframes, the sky's share on every surface with the zones). Each logged. */
static void HandleDebugInteriorInput(WorldTestFrame* frame, const GameInput* input)
{
    if (GameInput_IsKeyPressed(input, GameKey_Keypad9))
    {
        bool Enabled = !WorldRenderer_IsInteriorLightingEnabled(frame->_renderer);
        WorldRenderer_SetInteriorLightingEnabled(frame->_renderer, Enabled);
        LogLine(frame, Enabled ? "interior lighting ON (roofs keep the sky's light out, ambient zones light rooms, the "
            "eye adapts indoors)." : "interior lighting OFF (every surface takes the whole sky's light, no ambient zone, "
            "the eye as in the open).");
    }
    if (GameInput_IsKeyPressed(input, GameKey_Keypad0))
    {
        WorldRendererInteriorView View = WorldRenderer_GetInteriorView(frame->_renderer);
        View = (WorldRendererInteriorView)(((int)View + 1) % (int)WorldRendererInteriorView_Count);
        WorldRenderer_SetInteriorView(frame->_renderer, View);
        char Text[HAZE_TEXT_CAPACITY];
        snprintf(Text, sizeof(Text), "interior view: %s.", (const char*)WorldRenderer_GetInteriorViewName(View));
        LogLine(frame, Text);
    }
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
    HandleDebugAtmosphereInput(frame, input, Environment);
    HandleDebugInteriorInput(frame, input);
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

    // Keypad . toggles the contact shadows (the seam the shadow maps leave at a caster's foot, closed on screen), for
    // A/B-ing a lit seam or a shadow reaching too far.
    if (GameInput_IsKeyPressed(input, GameKey_KeypadDecimal))
    {
        bool Enabled = !WorldRenderer_AreContactShadowsEnabled(frame->_renderer);
        WorldRenderer_SetContactShadowsEnabled(frame->_renderer, Enabled);
        if (frame->_services->Logger != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                (const unsigned char*)u8"WorldTest: contact shadows %s.", Enabled ? "ON" : "OFF");
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

    // K toggles sprite lighting: off draws every sprite unlit at its full colours, for A/B-ing how they sit in the
    // light at night and in shade.
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
        LogCollisionStatus(frame);
        WorldTestCharacters_LogStatus(&frame->_testCharacters);
        WorldTestAnimation_LogStatus(&frame->_testAnimation);
        WorldTestInstances_LogStatus(&frame->_testInstances);
        WorldTestShadows_LogStatus(&frame->_testShadows);
        WorldTestWeather_LogStatus(&frame->_testWeather);
        LogCameraStatus(frame);
        LogFrameTimes(frame);
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


// Static functions: the world models.
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
 * a zoomed camera, whose draw distances reach farther and whose LODs are finer; Q toggles the LOD tinting. */
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
                ? "zoomed in: the draw distances of models and sprites, and the LOD distances, reach about 4.4 times as "
                    "far" : "zoom off";
            Error LogResult = Logger_LogInfoFormatted(Log,
                (const unsigned char*)u8"WorldTest: %s (a %.0f-degree view).", What, (double)frame->_camera.FovY);
            Error_Deconstruct(&LogResult);
        }
    }
    // Minus shows the terrain's chunk key: each chunk tinted by its cell, and the terrain's wireframe. Not F12:
    // raylib's EndDrawing takes a screenshot on it (SUPPORT_SCREEN_CAPTURE).
    if (GameInput_IsKeyPressed(input, GameKey_Minus))
    {
        bool IsEnabled = !WorldRenderer_IsTerrainKeyEnabled(frame->_renderer);
        WorldRenderer_SetTerrainKeyEnabled(frame->_renderer, IsEnabled);
        if (Log != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(Log, (const unsigned char*)u8"WorldTest: terrain chunk key %s.",
                IsEnabled ? "ON (each chunk its own colour, the wireframe over it)" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }
    // Equals cycles what the terrain shows: shaded, the painted weights, the weights after the rules, the dominant
    // material, shaded again.
    if (GameInput_IsKeyPressed(input, GameKey_Equal))
    {
        WorldRendererTerrainView View = (WorldRendererTerrainView)(((uint32_t)WorldRenderer_GetTerrainView(
            frame->_renderer) + 1U) % (uint32_t)WorldRendererTerrainView_Count);
        WorldRenderer_SetTerrainView(frame->_renderer, View);
        if (Log != NULL)
        {
            static const char* const Explanations[WorldRendererTerrainView_Count] =
            {
                "the materials, blended and shaded",
                "each palette material's debug colour by its painted weights, before the rules",
                "the debug colours by the weights after the rules (slope, height, noise)",
                "each pixel the debug colour of the material with the largest share after height blending"
            };
            Error LogResult = Logger_LogInfoFormatted(Log, (const unsigned char*)u8"WorldTest: terrain view: %s (%s).",
                (const char*)WorldRenderer_GetTerrainViewName(View), Explanations[View]);
            Error_Deconstruct(&LogResult);
        }
    }
    // Q colours every model by the LOD it shows (0 white, 1 green, 2 yellow, 3 red, coarser magenta), to watch the
    // switches along the lod_test row.
    if (GameInput_IsKeyPressed(input, GameKey_Q))
    {
        bool IsEnabled = !WorldRenderer_IsLodTintingEnabled(frame->_renderer);
        WorldRenderer_SetLodTintingEnabled(frame->_renderer, IsEnabled);
        if (Log != NULL)
        {
            Error LogResult = Logger_LogInfoFormatted(Log, (const unsigned char*)u8"WorldTest: LOD tinting %s.",
                IsEnabled ? "ON (LOD 0 white, 1 green, 2 yellow, 3 red, coarser magenta)" : "OFF");
            Error_Deconstruct(&LogResult);
        }
    }
}


// Static functions: the desk (the test world's hierarchy).
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
        LogLine(frame, frame->_isDeskSliding
            ? "the desk slides along X; its parts, the cup, the lamp and the lamp's light follow."
            : "the desk stops.");
    }
    if (GameInput_IsKeyPressed(input, GameKey_H))
    {
        bool IsVisible = !WorldObject_IsVisible(Desk);
        Error Result = WorldObject_SetVisible(Desk, IsVisible);
        Error_Deconstruct(&Result); // cannot fail: the object exists
        LogLine(frame, IsVisible ? "the desk is shown again, with everything on it."
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
    Vector3Double Position = WorldObject_GetPosition(Desk);
    Position.X = frame->_deskStartX + ((double)DESK_SLIDE_DISTANCE * sin(Turn));
    Error Result = WorldObject_SetPosition(Desk, Position);
    Error_Deconstruct(&Result); // cannot fail: the position is finite
}


// Static functions: collision.
static const char* GetNameOf(World* world, uint64_t id)
{
    WorldObject* Object = NULL;
    const unsigned char* Name = World_TryGetObject(world, id, &Object) ? WorldObject_GetName(Object) : NULL;
    return (Name != NULL) ? (const char*)Name : "";
}

/* The tick's events step: logs the trigger events of this tick's sync (the desk's parts entering and leaving the zone
 * at the end of its slide), then drops them. An exit can name an object that was just removed: its name is then empty. */
static void LogTriggerEvents(WorldTestFrame* frame)
{
    size_t Count = 0U;
    const WorldTriggerEvent* Events = WorldCollision_GetTriggerEvents(frame->_collision, &Count);
    for (size_t Index = 0U; (Index < Count) && (frame->_services->Logger != NULL); Index++)
    {
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: trigger %" PRIu64 u8" (\"%s\") %s: object %" PRIu64 u8" (\"%s\").",
            Events[Index].TriggerId, GetNameOf(&frame->_world, Events[Index].TriggerId),
            (Events[Index].Kind == WorldTriggerEventKind_Enter) ? "enter" : "exit", Events[Index].OtherId,
            GetNameOf(&frame->_world, Events[Index].OtherId));
        Error_Deconstruct(&LogResult);
    }
    WorldCollision_ClearTriggerEvents(frame->_collision);
}

/* The contact events of this tick's step: each begin and end (persists only for colliders that ask), then drops them. */
static void LogContactEvents(WorldTestFrame* frame)
{
    size_t Count = 0U;
    const WorldContactEvent* Events = WorldCollision_GetContactEvents(frame->_collision, &Count);
    for (size_t Index = 0U; (Index < Count) && (frame->_services->Logger != NULL); Index++)
    {
        const WorldContactEvent* Event = &Events[Index];
        const char* Kind = (Event->Kind == WorldContactEventKind_Begin) ? "begins"
            : ((Event->Kind == WorldContactEventKind_Persist) ? "goes on" : "ends");
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: contact %s: object %" PRIu64 u8" (\"%s\") and object %" PRIu64
            u8" (\"%s\"), %u shape pairs, impact %.2f m/s.", Kind, Event->ObjectIdA,
            GetNameOf(&frame->_world, Event->ObjectIdA), Event->ObjectIdB, GetNameOf(&frame->_world, Event->ObjectIdB),
            Event->ContactCount, (double)Event->ApproachSpeed);
        Error_Deconstruct(&LogResult);
    }
    WorldCollision_ClearContactEvents(frame->_collision);
}

/* The motion events of this tick's step: movers blocked and pushables crushed, when it begins and ends. */
static void LogMotionEvents(WorldTestFrame* frame)
{
    size_t Count = 0U;
    const WorldMotionEvent* Events = WorldCollision_GetMotionEvents(frame->_collision, &Count);
    for (size_t Index = 0U; (Index < Count) && (frame->_services->Logger != NULL); Index++)
    {
        const WorldMotionEvent* Event = &Events[Index];
        Error LogResult = Error_CreateSuccess();
        switch (Event->Kind)
        {
            case WorldMotionEventKind_BlockedBegin:
                LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                    (const unsigned char*)u8"WorldTest: object %" PRIu64 u8" (\"%s\") is blocked by object %" PRIu64
                    u8" (\"%s\"), which cannot get out of its way; it %s.", Event->ObjectId,
                    GetNameOf(&frame->_world, Event->ObjectId), Event->OtherId, GetNameOf(&frame->_world, Event->OtherId),
                    Event->IsStopped ? "stops" : "pushes through");
                break;
            case WorldMotionEventKind_BlockedEnd:
                LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                    (const unsigned char*)u8"WorldTest: object %" PRIu64 u8" (\"%s\") is no longer blocked.",
                    Event->ObjectId, GetNameOf(&frame->_world, Event->ObjectId));
                break;
            case WorldMotionEventKind_CrushedBegin:
                LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                    (const unsigned char*)u8"WorldTest: object %" PRIu64 u8" (\"%s\") is crushed: pinned %.1f mm into "
                    u8"object %" PRIu64 u8" (\"%s\").", Event->ObjectId, GetNameOf(&frame->_world, Event->ObjectId),
                    (double)(Event->Depth * 1000.0f), Event->OtherId, GetNameOf(&frame->_world, Event->OtherId));
                break;
            case WorldMotionEventKind_CrushedEnd:
                LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
                    (const unsigned char*)u8"WorldTest: object %" PRIu64 u8" (\"%s\") is no longer crushed.",
                    Event->ObjectId, GetNameOf(&frame->_world, Event->ObjectId));
                break;
        }
        Error_Deconstruct(&LogResult);
    }
    WorldCollision_ClearMotionEvents(frame->_collision);
}

/* The door's driver: turns it toward open or closed at its swing speed, from where it actually is (the collision step
 * may have stopped it on something it could not push). */
static void SwingDoor(WorldTestFrame* frame, float deltaSeconds)
{
    WorldObject* Door = NULL;
    if ((frame->_doorId == 0U) || !World_TryGetObject(&frame->_world, frame->_doorId, &Door))
    {
        return;
    }
    Vector3 Rotation = WorldObject_GetRotation(Door);
    float Target = frame->_doorClosedYaw + (frame->_isDoorOpen ? DOOR_OPEN_TURN : 0.0f);
    float Left = Target - Rotation.y;
    if (Left == 0.0f)
    {
        return;
    }
    float Step = DOOR_SWING_SPEED * deltaSeconds;
    Rotation.y = (fabsf(Left) <= Step) ? Target : (Rotation.y + ((Left > 0.0f) ? Step : -Step));
    Error Result = WorldObject_SetRotation(Door, Rotation);
    Error_Deconstruct(&Result); // cannot fail: the rotation is finite
}

/* F6, or a character's push (its open_door action): the door swings open, or closed again. */
static void SwingDoorOpenOrClosed(WorldTestFrame* frame)
{
    frame->_isDoorOpen = !frame->_isDoorOpen;
    LogLine(frame, frame->_isDoorOpen ? "the door swings open (it stops on the crate pinned in its way; F10 moves the "
        "crate)." : "the door swings closed.");
}

/* Puts a crate where it was loaded, as a teleport (no sweep: it is only pushed out of what it lands in). */
static void TeleportObject(WorldTestFrame* frame, uint64_t id, Vector3Double position)
{
    WorldObject* Object = NULL;
    if ((id == 0U) || !World_TryGetObject(&frame->_world, id, &Object))
    {
        return;
    }
    Error Result = WorldObject_SetPosition(Object, position);
    Error_Deconstruct(&Result); // cannot fail: the position is finite
    WorldCollision_Teleport(frame->_collision, id);
}

/* The pushing wall's driver: toward its far end or home along X at its speed, from where it actually is (the collision
 * step stops it when the row it pushes is pinned against the stop). Home again, the row's crates go back. */
static void MovePusher(WorldTestFrame* frame, float deltaSeconds)
{
    WorldObject* Pusher = NULL;
    if ((frame->_pusherId == 0U) || !World_TryGetObject(&frame->_world, frame->_pusherId, &Pusher))
    {
        return;
    }
    Vector3Double Position = WorldObject_GetPosition(Pusher);
    double Target = frame->_pusherHomeX + (frame->_isPusherOut ? (double)PUSHER_TRAVEL : 0.0);
    double Left = Target - Position.X;
    if (Left == 0.0)
    {
        if (!frame->_isPusherOut && !frame->_isPusherHome)
        {
            frame->_isPusherHome = true;
            for (size_t Index = 0U; Index < ROW_CRATE_COUNT; Index++)
            {
                TeleportObject(frame, frame->_rowCrateIds[Index], frame->_rowCrateHomes[Index]);
            }
            LogLine(frame, "the pushing wall is home; the crates are back where they were (teleported).");
        }
        return;
    }
    double Step = (double)(PUSHER_SPEED * deltaSeconds);
    Position.X = (fabs(Left) <= Step) ? Target : (Position.X + ((Left > 0.0) ? Step : -Step));
    Error Result = WorldObject_SetPosition(Pusher, Position);
    Error_Deconstruct(&Result); // cannot fail: the position is finite
}

static void UpdateCollisionDebug(WorldTestFrame* frame)
{
    WorldCollision_SetDebugListEnabled(frame->_collision, frame->_areCollidersShown);
    WorldCollision_SetDebugContactsEnabled(frame->_collision, frame->_areContactsShown);
    WorldRenderer_SetCollisionDebug(frame->_renderer, (frame->_areCollidersShown || frame->_areContactsShown)
        ? frame->_collision : NULL);
}

/* The collision keys: F7 shows and hides the collider wireframes (coloured by layer: world grey, prop orange, triggers
 * violet; static ones darker), F8 the contact points (normals cyan, pushes red); F6 swings the door open and closed, F9
 * sends the pushing wall out and back, F10 teleports the door's crate aside and back. Not R: the UI test frame above
 * takes it. */
static void HandleDebugCollisionInput(WorldTestFrame* frame, const GameInput* input)
{
    if (GameInput_IsKeyPressed(input, GameKey_F7))
    {
        frame->_areCollidersShown = !frame->_areCollidersShown;
        UpdateCollisionDebug(frame);
        LogLine(frame, frame->_areCollidersShown ? "collider wireframes ON." : "collider wireframes OFF.");
    }
    if (GameInput_IsKeyPressed(input, GameKey_F8))
    {
        frame->_areContactsShown = !frame->_areContactsShown;
        UpdateCollisionDebug(frame);
        LogLine(frame, frame->_areContactsShown ? "contact points ON (white touching, grey near; normals cyan, pushes "
            "red)." : "contact points OFF.");
    }
    if (GameInput_IsKeyPressed(input, GameKey_F6) && (frame->_doorId != 0U))
    {
        SwingDoorOpenOrClosed(frame);
    }
    if (GameInput_IsKeyPressed(input, GameKey_F9) && (frame->_pusherId != 0U))
    {
        frame->_isPusherOut = !frame->_isPusherOut;
        frame->_isPusherHome = false;
        LogLine(frame, frame->_isPusherOut ? "the pushing wall goes out along +X: it pushes the row of crates against "
            "the stop." : "the pushing wall goes back home.");
    }
    if (GameInput_IsKeyPressed(input, GameKey_F10) && (frame->_doorCrateId != 0U))
    {
        frame->_isDoorCrateAside = !frame->_isDoorCrateAside;
        Vector3Double Position = frame->_doorCrateHome;
        Position.X += frame->_isDoorCrateAside ? (double)DOOR_CRATE_ASIDE_SHIFT : 0.0;
        TeleportObject(frame, frame->_doorCrateId, Position);
        LogLine(frame, frame->_isDoorCrateAside ? "the door's crate is teleported out of its way." : "the door's crate "
            "is teleported back into its corner (pushed out of the door if they overlap).");
    }
}


// Static functions: the far shift.
/* Moves every root object by @p offset (their children follow), the characters too, each as a teleport, so the
 * collision step does not sweep the move. Everything moves before the collision world syncs again, so no trigger sees
 * anything leave. Returns how many moved. */
static size_t ShiftRootObjects(WorldTestFrame* frame, Vector3Double offset)
{
    size_t Count = 0U;
    size_t ObjectCount = World_GetObjectCount(&frame->_world);
    for (size_t Index = 0U; Index < ObjectCount; Index++)
    {
        WorldObject* Object = NULL;
        Error Result = World_GetObjectByIndex(&frame->_world, Index, &Object);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            continue;
        }
        if (WorldObject_GetParent(Object) != NULL)
        {
            continue; // children follow their parents
        }
        Result = WorldObject_SetPosition(Object, Vector3Double_Add(WorldObject_GetPosition(Object), offset));
        Error_Deconstruct(&Result); // cannot fail: the position is finite
        WorldCollision_Teleport(frame->_collision, WorldObject_GetId(Object));
        Count++;
    }
    return Count;
}

/* Moves the places the frame's drivers move things around (the desk's slide, the pushing wall's and the crates' homes)
 * by @p offset, with the objects. */
static void ShiftHomes(WorldTestFrame* frame, Vector3Double offset)
{
    frame->_deskStartX += offset.X;
    frame->_pusherHomeX += offset.X;
    for (size_t Index = 0U; Index < ROW_CRATE_COUNT; Index++)
    {
        frame->_rowCrateHomes[Index] = Vector3Double_Add(frame->_rowCrateHomes[Index], offset);
    }
    frame->_doorCrateHome = Vector3Double_Add(frame->_doorCrateHome, offset);
}

/* F11: moves every root object (the characters among them) and the camera to the far shift's next place (FarShifts),
 * so the picture and the play can be compared out there. The objects move as teleports; then the collision world is
 * synced and every character is placed again where it now stands, through the character system, so its motion starts
 * afresh on its moved ground. Positions are doubles: a trip out and back returns everything to within a double's
 * step out there (1.5e-11 m at 100 km). */
static void StepFarShift(WorldTestFrame* frame)
{
    size_t Next = (frame->_farShiftIndex + 1U) % FAR_SHIFT_COUNT;
    Vector3Double Offset = Vector3Double_Subtract(FarShifts[Next], FarShifts[frame->_farShiftIndex]);
    size_t ObjectCount = ShiftRootObjects(frame, Offset);
    WorldCollision_Sync(frame->_collision);
    size_t CharacterCount = WorldTestCharacters_Shift(&frame->_testCharacters, Offset);
    ShiftHomes(frame, Offset);
    frame->_camera.Position = Vector3Double_Add(frame->_camera.Position, Offset);
    frame->_farShiftIndex = Next;
    if (frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: far shift %" PRIu64 u8" of %" PRIu64 u8": %" PRIu64 u8" root objects "
            u8"and the camera moved by (%.0f, %.0f, %.0f) m, as teleports, and %" PRIu64 u8" characters placed "
            u8"again on their ground; the camera is %.2f km from the world's origin%s",
            (uint64_t)Next, (uint64_t)(FAR_SHIFT_COUNT - 1U), (uint64_t)ObjectCount, Offset.X, Offset.Y, Offset.Z,
            (uint64_t)CharacterCount, Vector3Double_Length(frame->_camera.Position) / METRES_PER_KILOMETRE,
            (Next == 0U) ? " (back at the start)." : " (F11 goes on; after the last place, back to the start).");
        Error_Deconstruct(&LogResult);
    }
}

/* The far shift's key: F11 moves everything 28 km out, then 141 km, then back. */
static void HandleDebugFarShiftInput(WorldTestFrame* frame, const GameInput* input)
{
    if (GameInput_IsKeyPressed(input, GameKey_F11))
    {
        StepFarShift(frame);
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

/* The mouse wheel steps the flycam's speed: up is faster, down slower, in whole notches between the limits (a fraction
 * waits for the rest of its notch). Logs the new speed once for a tick that changed it. */
static void HandleFlySpeedWheel(WorldTestFrame* frame, GameInput* input)
{
    Vector2 Wheel = GameInput_GetWheelDelta(input);
    if (Wheel.y == 0.0f)
    {
        return;
    }
    GameInput_ConsumeWheel(input);
    frame->_flyWheelRemainder += Wheel.y;
    float Notches = truncf(frame->_flyWheelRemainder);
    frame->_flyWheelRemainder -= Notches;
    float Range = (float)(FLY_SPEED_MAX_STEP - FLY_SPEED_MIN_STEP);
    int Step = frame->_flySpeedStep + (int)Clamp(Notches, -Range, Range);
    Step = (Step < FLY_SPEED_MIN_STEP) ? FLY_SPEED_MIN_STEP : ((Step > FLY_SPEED_MAX_STEP) ? FLY_SPEED_MAX_STEP : Step);
    if (Step == frame->_flySpeedStep)
    {
        return;
    }
    frame->_flySpeedStep = Step;
    if (frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: flycam speed %.2f m/s (the mouse wheel; the sprint key multiplies it by "
            u8"%.0f).", (double)FlySpeed(frame), (double)SPRINT_MULTIPLIER);
        Error_Deconstruct(&LogResult);
    }
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

    // The characters' keys (F1 walk mode, F2 the test NPCs, F3 flight, F4 the player's kind) and,
    // while walking, the player controller: it reads the movement actions and the mouse into the player's character
    // (and takes the use press, so the E debug key below does not see it). The flycam stands still meanwhile.
    WorldTestCharacters_HandleInput(&frame->_testCharacters, input, &frame->_camera, frame->_cursorCaptured);
    bool IsWalking = WorldTestCharacters_IsWalking(&frame->_testCharacters);

    // Mouse look (only while captured). Input is polled once per update tick, so this is the tick's movement.
    if (frame->_cursorCaptured && !IsWalking)
    {
        Vector2 MouseDelta = GameInput_GetMouseDelta(input);
        // Mouse-right must turn the view right: increasing yaw rotates forward toward +X, which renders on
        // the screen's left under the right-handed view, so a rightward mouse delta lowers yaw.
        frame->_camera.Yaw -= MouseDelta.x * MOUSE_SENSITIVITY;
        frame->_camera.Pitch -= MouseDelta.y * MOUSE_SENSITIVITY;
        frame->_camera.Pitch = Clamp(frame->_camera.Pitch, -GAME_CAMERA_MAX_PITCH, GAME_CAMERA_MAX_PITCH);
    }

    // The wheel steps the flycam's speed (not while walking: the character has its own) and the sprint key multiplies it.
    if (!IsWalking)
    {
        HandleFlySpeedWheel(frame, input);
    }
    float Speed = FlySpeed(frame) * (GameInput_IsActionDown(input, GameAction_Sprint) ? SPRINT_MULTIPLIER : 1.0f);
    Vector3 Forward = GameCamera_GetForward(&frame->_camera);
    Vector3 Right = GameCamera_GetRight(&frame->_camera);
    Vector3 Movement = { .x = 0.0f, .y = 0.0f, .z = 0.0f };
    if (GameInput_IsActionDown(input, GameAction_MoveForward)) { Movement = Vector3Add(Movement, Forward); }
    if (GameInput_IsActionDown(input, GameAction_MoveBack)) { Movement = Vector3Subtract(Movement, Forward); }
    if (GameInput_IsActionDown(input, GameAction_MoveRight)) { Movement = Vector3Add(Movement, Right); }
    if (GameInput_IsActionDown(input, GameAction_MoveLeft)) { Movement = Vector3Subtract(Movement, Right); }
    if (GameInput_IsActionDown(input, GameAction_MoveUp)) { Movement = Vector3Add(Movement, FrameWorldUp); }
    if (GameInput_IsActionDown(input, GameAction_MoveDown)) { Movement = Vector3Subtract(Movement, FrameWorldUp); }
    if (!IsWalking && ((Movement.x != 0.0f) || (Movement.y != 0.0f) || (Movement.z != 0.0f)))
    {
        Movement = Vector3Scale(Vector3Normalize(Movement), Speed * deltaSeconds);
        frame->_camera.Position = Vector3Double_Add(frame->_camera.Position, GameMath_FromVector3(Movement));
    }

    // Debug controls (edge-triggered keys are reliable here: input is polled per update tick).
    HandleDebugClockInput(frame, input, deltaSeconds);
    HandleDebugSkyInput(frame, input);
    HandleDebugInput(frame, input);
    HandleDebugDeskInput(frame, input);
    HandleDebugModelInput(frame, input);
    HandleDebugCollisionInput(frame, input);
    HandleDebugFarShiftInput(frame, input);
    WorldTestAnimation_HandleInput(&frame->_testAnimation, input, &frame->_camera);
    WorldTestInstances_HandleInput(&frame->_testInstances, input);
    WorldTestShadows_HandleInput(&frame->_testShadows, input);
    WorldTestWeather_HandleInput(&frame->_testWeather, input, &frame->_clock);
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
            u8"centre cube and the tree, U frustum culling, J slide the desk (through a trigger zone), H hide the "
            u8"desk, E variants, Z zoom, Q LOD tinting, F7 collider wireframes, F8 contact points, F6 swing the door, "
            u8"F9 the pushing wall, F10 move the door's crate, F11 far shift (everything 28 km out, then 141 km, then "
            u8"back), - the terrain's chunk key, = the terrain's views (shaded, painted weights, rule weights, the "
            u8"dominant material); F1 walk (the player as a character: WASD, SHIFT run, SPACE jump, "
            u8"CTRL crouch, E use: sit on the "
            u8"bench, pick up the coffee cup, open the door, E again to stand up; 9 put down, 0 wave), F2 the test NPCs "
            u8"(a human, a dog, a pigeon), 8 the NPC human sits on the bench and gets up, F3 fly, F4 the player's kind; "
            u8"5 the showcase's next clip, 6 the crowd, 7 slow motion; ' the pebble field (20 000 instanced copies), ; "
            u8"instanced drawing (off: every copy on its own, the same picture); I renderer, game "
            u8"time, collision and camera status, T text sample, F5 save the world (worlds/test.saved.world.json and "
            u8".ghdf); M frees the mouse for the UI (click the world to capture it again); ESC quits (once the UI lets "
            u8"go of it).");
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
    Stopwatch UpdateClock = Stopwatch_StartNew();

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
    // The weather moves with the game time the clock moved, its gusts and lightning with the real time of play: a held
    // clock holds the presets, the values and the wetness, while a flash still goes out (as the characters still move).
    WorldTestWeather_Update(&Frame->_testWeather, &Frame->_clock, Crossings.GameSeconds, time.PassedTime);

    // The tick's order (agents/WorldData.md): the NPCs' brains set their inputs (the scripts' slot), the drivers move
    // things (the desk, the door, the pushing wall, then the characters, so what moved this tick carries them), the
    // collision step sweeps and pushes (it syncs first and writes the pushes back), its queued trigger, contact and
    // motion events and the characters' events are handled, then the animations (the skeletons' playback and their
    // graphs and actions: their poses wait for PrepareRender, once per shown frame) and their events, and the sprites'.
    WorldTestCharacters_UpdateBrains(&Frame->_testCharacters, Delta);
    SlideDesk(Frame, time.PassedTime);
    SwingDoor(Frame, Delta);
    MovePusher(Frame, Delta);
    WorldCharacters_Update(Frame->_characters, Delta);
    WorldCollision_Step(Frame->_collision, Delta);
    LogTriggerEvents(Frame);
    LogContactEvents(Frame);
    LogMotionEvents(Frame);
    WorldTestAnimation_Update(&Frame->_testAnimation, Delta);
    // The animation events: the actions logged, a released cup settled, the door's push swings it.
    uint64_t Pushed = WorldTestCharacters_HandleAnimationEvents(&Frame->_testCharacters);
    if ((Pushed != 0U) && (Pushed == Frame->_doorId))
    {
        SwingDoorOpenOrClosed(Frame);
    }
    AdvanceSpriteAnimations(&Frame->_world, time.PassedTime);

    // The world matrices of everything that moved this tick, so the renderer reads them cached; then the look target
    // (its changes logged). The camera rides the player's eyes from PrepareRender, after the pose step.
    World_UpdateTransforms(&Frame->_world);
    WorldTestCharacters_AfterStep(&Frame->_testCharacters);

    // The text sample animates its sprite and is laid out here (laying out loads its fonts); Render only draws it.
    Error TextResult = TextDocument_Update(&Frame->_textSample, time);
    if ((TextResult.Code == ErrorCode_Success) && Frame->_isTextSampleShown)
    {
        TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
        TextResult = TextLayout_Refresh(&Frame->_textSampleLayout, &Frame->_textSample, &Options);
    }
    Frame->_lastUpdateMilliseconds = (double)Stopwatch_ElapsedNanoseconds(&UpdateClock) / NANOSECONDS_PER_MILLISECOND;
    Stopwatch_Deconstruct(&UpdateClock);
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

/* The update side's last step before a render: the skeletons are posed once per shown frame, then the world
 * matrices of what the poses moved are brought up to date, so the render reads them cached, the camera rides the
 * player's eyes (its body's eyes socket as posed) while walking, and how open the sky is around the camera is
 * measured for the eye. */
static Error WorldTestFrame_PrepareRender(void* self, ProgramTime time)
{
    (void)time;
    WorldTestFrame* Frame = self;
    WorldTestAnimation_Pose(&Frame->_testAnimation);
    World_UpdateTransforms(&Frame->_world);
    WorldTestCharacters_PlaceCamera(&Frame->_testCharacters, &Frame->_camera);
    // The player's body is seen from its eyes: the camera's passes leave out its head.
    WorldRenderer_SetFirstPersonObject(Frame->_renderer, WorldTestCharacters_GetPlayer(&Frame->_testCharacters));
    // How open the camera's surroundings are to the sky, against what keeps the sky out (the world and prop layers): the
    // eye adapts indoors by it.
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_BIT(CollisionLayer_World)
        | COLLISION_LAYER_BIT(CollisionLayer_Prop), WorldCollisionTriggerMode_Ignore);
    WorldRenderer_SetCameraOpenness(Frame->_renderer, WorldCollision_GetSkyOpenness(Frame->_collision,
        Frame->_camera.Position, CAMERA_OPENNESS_REACH, &Filter));
    return Error_CreateSuccess();
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
    // The characters borrow the collision world, which borrows the world (and the renderer borrowed it): they go after
    // the renderer, before the world, as the animation system does.
    WorldTestAnimation_Deconstruct(&Frame->_testAnimation);
    WorldTestCharacters_Deconstruct(&Frame->_testCharacters);
    WorldCharacters_Destroy(Frame->_characters);
    WorldCollision_Destroy(Frame->_collision);
    WorldTestWeather_Deconstruct(&Frame->_testWeather);

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
    .PrepareRender = WorldTestFrame_PrepareRender,
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

    Error CollisionResult = WorldCollision_Create(&Frame->_world, services->Assets, services->Logger, NULL,
        &Frame->_collision);
    if (CollisionResult.Code != ErrorCode_Success)
    {
        DeconstructTextSample(Frame);
        Error Cleanup = WorldRenderer_Deconstruct(Frame->_renderer);
        Error_Deconstruct(&Cleanup);
        Cleanup = World_Deconstruct(&Frame->_world);
        Error_Deconstruct(&Cleanup);
        GameClock_Deconstruct(&Frame->_clock);
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return CollisionResult;
    }

    Error CharactersResult = WorldCharacters_Create(&Frame->_world, Frame->_collision, services->Logger,
        &Frame->_characters);
    if (CharactersResult.Code != ErrorCode_Success)
    {
        DeconstructTextSample(Frame);
        WorldCollision_Destroy(Frame->_collision);
        Error Cleanup = WorldRenderer_Deconstruct(Frame->_renderer);
        Error_Deconstruct(&Cleanup);
        Cleanup = World_Deconstruct(&Frame->_world);
        Error_Deconstruct(&Cleanup);
        GameClock_Deconstruct(&Frame->_clock);
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return CharactersResult;
    }
    WorldTestCharacters_Construct(&Frame->_testCharacters, &Frame->_world, Frame->_collision, Frame->_characters,
        services->Logger);
    Error AnimationResult = WorldTestAnimation_Construct(&Frame->_testAnimation, &Frame->_world, services->Assets,
        services->Logger);
    if (AnimationResult.Code != ErrorCode_Success)
    {
        WorldTestCharacters_Deconstruct(&Frame->_testCharacters);
        WorldCharacters_Destroy(Frame->_characters);
        DeconstructTextSample(Frame);
        WorldCollision_Destroy(Frame->_collision);
        Error Cleanup = WorldRenderer_Deconstruct(Frame->_renderer);
        Error_Deconstruct(&Cleanup);
        Cleanup = World_Deconstruct(&Frame->_world);
        Error_Deconstruct(&Cleanup);
        GameClock_Deconstruct(&Frame->_clock);
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return AnimationResult;
    }
    WorldTestCharacters_SetAnimation(&Frame->_testCharacters, Frame->_testAnimation._animation);
    WorldTestInstances_Construct(&Frame->_testInstances, &Frame->_world, Frame->_collision, Frame->_renderer,
        services->Logger);
    WorldTestShadows_Construct(&Frame->_testShadows, &Frame->_world, Frame->_renderer, services->Logger);
    WorldTestWeather_Construct(&Frame->_testWeather, &Frame->_world, services->Assets, services->Logger);

    Frame->_services = services;
    for (size_t Index = 0U; Index < OPACITY_TEST_OBJECT_COUNT; Index++)
    {
        Frame->_opacityTestObjectIds[Index] = FindObjectId(&Frame->_world, OpacityTestObjectNames[Index]);
    }
    Frame->_opacityStep = 0U;
    Frame->_deskId = FindObjectId(&Frame->_world, (const char*)TEST_DESK_OBJECT_NAME);
    WorldObject* Desk = NULL;
    Frame->_deskStartX = World_TryGetObject(&Frame->_world, Frame->_deskId, &Desk) ? WorldObject_GetPosition(Desk).X
        : 0.0;
    Frame->_isDeskSliding = false;
    Frame->_deskSlideSeconds = 0.0;
    for (size_t Index = 0U; Index < VARIANT_TEST_OBJECT_COUNT; Index++)
    {
        Frame->_variantTestObjectIds[Index] = FindObjectId(&Frame->_world, VariantTestObjectNames[Index]);
    }
    Frame->_variantStep = 0U;
    Frame->_isZoomed = false;
    Frame->_areCollidersShown = false;
    Frame->_areContactsShown = false;
    Frame->_doorId = FindObjectId(&Frame->_world, TEST_DOOR_OBJECT_NAME);
    WorldObject* Door = NULL;
    Frame->_doorClosedYaw = World_TryGetObject(&Frame->_world, Frame->_doorId, &Door) ? WorldObject_GetRotation(Door).y
        : 0.0f;
    Frame->_isDoorOpen = false;
    Frame->_pusherId = FindObjectId(&Frame->_world, TEST_PUSHER_OBJECT_NAME);
    WorldObject* Pusher = NULL;
    Frame->_pusherHomeX = World_TryGetObject(&Frame->_world, Frame->_pusherId, &Pusher)
        ? WorldObject_GetPosition(Pusher).X : 0.0;
    Frame->_isPusherOut = false;
    Frame->_isPusherHome = true;
    for (unsigned Index = 0U; Index < ROW_CRATE_COUNT; Index++)
    {
        char Name[32];
        snprintf(Name, sizeof(Name), TEST_ROW_CRATE_NAME_FORMAT, Index + 1U);
        Frame->_rowCrateIds[Index] = FindObjectId(&Frame->_world, Name);
        WorldObject* Crate = NULL;
        Frame->_rowCrateHomes[Index] = World_TryGetObject(&Frame->_world, Frame->_rowCrateIds[Index], &Crate)
            ? WorldObject_GetPosition(Crate) : Vector3Double_Zero();
    }
    Frame->_doorCrateId = FindObjectId(&Frame->_world, TEST_DOOR_CRATE_OBJECT_NAME);
    WorldObject* DoorCrate = NULL;
    Frame->_doorCrateHome = World_TryGetObject(&Frame->_world, Frame->_doorCrateId, &DoorCrate)
        ? WorldObject_GetPosition(DoorCrate) : Vector3Double_Zero();
    Frame->_isDoorCrateAside = false;
    Frame->_farShiftIndex = 0U;
    if (!IsTestWorldLoaded)
    {
        WorldRenderer_SetDebugGridEnabled(Frame->_renderer, true); // an empty world shows the grid, so it is visible
    }
    Frame->_camera = GameCamera_Create(Vector3Double_Create(0.0, 2.5, -7.0), GAME_CAMERA_DEFAULT_FOV_Y);
    Frame->_camera.Pitch = -0.2f;
    Frame->_cursorCaptured = false;
    Frame->_flySpeedStep = 0;
    Frame->_flyWheelRemainder = 0.0f;
    Frame->_lastUpdateMilliseconds = 0.0;

    *outFrame = &Frame->Base;
    return Error_CreateSuccess();
}
