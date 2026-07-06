#include "WorldTestFrame.h"
#include "GameFrame.h"
#include "Services.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelObject.h"
#include "WorldLight.h"
#include "GameCamera.h"
#include "WorldRenderer.h"
#include "Renderer.h"
#include "Logger.h"
#include "raylib/raylib.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** Debug name used in log/crash messages. */
#define FRAME_DEBUG_NAME ((const unsigned char*)u8"WorldTest")
/** Asset name of the test model placed at the world centre. */
#define TEST_MODEL_ASSET_NAME ((const unsigned char*)u8"test")
/** Name given to the test model object. */
#define TEST_MODEL_OBJECT_NAME ((const unsigned char*)u8"test_object")
/** Name given to the ground-slab object (a flattened test model that receives the sun shadow). */
#define TEST_GROUND_OBJECT_NAME ((const unsigned char*)u8"ground")

/** Base movement speed, in world units per second. */
#define MOVE_SPEED 6.0f
/** Multiplier applied to movement speed while the sprint key is held. */
#define SPRINT_MULTIPLIER 3.0f
/** Mouse look sensitivity, in radians per pixel of mouse movement. */
#define MOUSE_SENSITIVITY 0.0025f
/** Day-night cycle length for the test world, in seconds (short so the cycle is easy to observe). */
#define TEST_DAY_LENGTH_SECONDS 120.0f
/** Sun-arc tilt from the zenith at noon for the test world, in radians (~23 degrees). */
#define TEST_SUN_ANGLE 0.4f
/** Manual time-of-day scrub rate (fraction of a full day per second while [ or ] is held). */
#define TIME_SCRUB_RATE 0.1f


// Types.
typedef struct WorldTestFrameStruct
{
    /** Abstract base (must be first). */
    GameFrame Base;
    /** Shared services; borrowed. */
    Services* _services;
    /** The world being viewed; owned. */
    World _world;
    /** The free-fly camera; owned value. */
    GameCamera _camera;
    /** The world renderer; owned. */
    WorldRenderer* _renderer;
    /** Whether the mouse cursor is currently captured. */
    bool _cursorCaptured;
} WorldTestFrame;


// Static functions: the world-up direction for vertical movement.
static const Vector3 FrameWorldUp = { .x = 0.0f, .y = 1.0f, .z = 0.0f };


// Static functions: helpers.
/* Logs a time-of-day change (label + the current TimeOfDay), so debug scrubbing is observable. */
static void LogTimeOfDay(WorldTestFrame* frame, const unsigned char* label, const WorldEnvironment* environment)
{
    if (frame->_services->Logger == NULL)
    {
        return;
    }
    Error LogResult = Logger_LogInfoFormatted(frame->_services->Logger,
        (const unsigned char*)u8"WorldTest: %s (TimeOfDay=%.3f).", (const char*)label, (double)environment->TimeOfDay);
    Error_Deconstruct(&LogResult);
}

/* Applies the debug time-of-day controls (pause/resume, phase presets, manual scrub). Called from Update:
 * the frame manager polls input once per update tick, so edge-triggered keys (IsKeyPressed) register
 * reliably here. @p deltaSeconds is the fixed-step delta, used to pace the manual scrub. */
static void HandleDebugTimeInput(WorldTestFrame* frame, float deltaSeconds)
{
    WorldEnvironment* Environment = World_GetEnvironment(&frame->_world);

    if (IsKeyPressed(KEY_P))
    {
        Environment->IsDayNightCycleEnabled = !Environment->IsDayNightCycleEnabled;
        LogTimeOfDay(frame, Environment->IsDayNightCycleEnabled
            ? (const unsigned char*)u8"cycle resumed" : (const unsigned char*)u8"cycle paused", Environment);
    }
    // Number keys jump to a phase and freeze the clock there.
    if (IsKeyPressed(KEY_ONE))   { Environment->TimeOfDay = 0.25f; Environment->IsDayNightCycleEnabled = false; LogTimeOfDay(frame, (const unsigned char*)u8"dawn (frozen)", Environment); }
    if (IsKeyPressed(KEY_TWO))   { Environment->TimeOfDay = 0.50f; Environment->IsDayNightCycleEnabled = false; LogTimeOfDay(frame, (const unsigned char*)u8"noon (frozen)", Environment); }
    if (IsKeyPressed(KEY_THREE)) { Environment->TimeOfDay = 0.75f; Environment->IsDayNightCycleEnabled = false; LogTimeOfDay(frame, (const unsigned char*)u8"dusk (frozen)", Environment); }
    if (IsKeyPressed(KEY_FOUR))  { Environment->TimeOfDay = 0.00f; Environment->IsDayNightCycleEnabled = false; LogTimeOfDay(frame, (const unsigned char*)u8"midnight (frozen)", Environment); }

    // [ and ] scrub the time of day manually (wraps into [0,1)).
    float ScrubStep = TIME_SCRUB_RATE * deltaSeconds;
    if (IsKeyDown(KEY_LEFT_BRACKET))  { Environment->TimeOfDay -= ScrubStep; if (Environment->TimeOfDay < 0.0f) { Environment->TimeOfDay += 1.0f; } }
    if (IsKeyDown(KEY_RIGHT_BRACKET)) { Environment->TimeOfDay += ScrubStep; if (Environment->TimeOfDay >= 1.0f) { Environment->TimeOfDay -= 1.0f; } }

    // O toggles the whole post-effects pass (AO + outlines) for A/B-ing whether an artifact comes from it;
    // off = the scene blits straight to the tonemap, bit-exact with the pre-postfx pipeline.
    if (IsKeyPressed(KEY_O))
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
    if (IsKeyPressed(KEY_B))
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
    if (IsKeyPressed(KEY_G))
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
}


// Static functions: vtable behavior.
static Error WorldTestFrame_Start(void* self, ProgramTime time)
{
    UNUSED(time);
    WorldTestFrame* Frame = self;

    DisableCursor();
    Frame->_cursorCaptured = true;

    if (Frame->_services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfo(Frame->_services->Logger,
            (const unsigned char*)u8"WorldTest: entered the world. WASD+SPACE/CTRL move, SHIFT sprint, mouse look; "
            u8"P pause/resume day-night, 1-4 = dawn/noon/dusk/midnight (frozen), [ ] scrub time, ESC quit.");
        Error_Deconstruct(&LogResult);
    }
    return Error_CreateSuccess();
}

static Error WorldTestFrame_End(void* self, ProgramTime time)
{
    UNUSED(time);
    WorldTestFrame* Frame = self;
    if (Frame->_cursorCaptured)
    {
        EnableCursor();
        Frame->_cursorCaptured = false;
    }
    return Error_CreateSuccess();
}

static Error WorldTestFrame_Update(void* self, ProgramTime time)
{
    WorldTestFrame* Frame = self;

    float Delta = (float)time.PassedTime;

    // Mouse look. Input is polled once per update tick (by the frame manager), so GetMouseDelta returns this
    // tick's incremental movement; summed across the tick(s) in a frame it equals the full frame delta.
    if (Frame->_cursorCaptured)
    {
        Vector2 MouseDelta = GetMouseDelta();
        // Mouse-right must turn the view right: increasing yaw rotates forward toward +X, which renders on
        // the screen's left under the right-handed view, so a rightward mouse delta lowers yaw.
        Frame->_camera.Yaw -= MouseDelta.x * MOUSE_SENSITIVITY;
        Frame->_camera.Pitch -= MouseDelta.y * MOUSE_SENSITIVITY;
        Frame->_camera.Pitch = Clamp(Frame->_camera.Pitch, -GAME_CAMERA_MAX_PITCH, GAME_CAMERA_MAX_PITCH);
    }

    float Speed = MOVE_SPEED;
    if (IsKeyDown(KEY_LEFT_SHIFT))
    {
        Speed *= SPRINT_MULTIPLIER;
    }

    Vector3 Forward = GameCamera_GetForward(&Frame->_camera);
    Vector3 Right = GameCamera_GetRight(&Frame->_camera);
    Vector3 Movement = { .x = 0.0f, .y = 0.0f, .z = 0.0f };

    if (IsKeyDown(KEY_W)) { Movement = Vector3Add(Movement, Forward); }
    if (IsKeyDown(KEY_S)) { Movement = Vector3Subtract(Movement, Forward); }
    if (IsKeyDown(KEY_D)) { Movement = Vector3Add(Movement, Right); }
    if (IsKeyDown(KEY_A)) { Movement = Vector3Subtract(Movement, Right); }
    if (IsKeyDown(KEY_SPACE)) { Movement = Vector3Add(Movement, FrameWorldUp); }
    if (IsKeyDown(KEY_LEFT_CONTROL)) { Movement = Vector3Subtract(Movement, FrameWorldUp); }

    if ((Movement.x != 0.0f) || (Movement.y != 0.0f) || (Movement.z != 0.0f))
    {
        Movement = Vector3Scale(Vector3Normalize(Movement), Speed * Delta);
        Frame->_camera.Position = Vector3Add(Frame->_camera.Position, Movement);
    }

    // Debug time-of-day controls (edge-triggered; reliable here now that input is polled per update tick).
    HandleDebugTimeInput(Frame, Delta);

    // Advance the world's day-night cycle (a no-op while paused).
    WorldEnvironment_Advance(World_GetEnvironment(&Frame->_world), Delta);

    return Error_CreateSuccess();
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
    return WorldRenderer_RenderToTarget(Frame->_renderer, &Frame->_world, &Frame->_camera,
        context->Time.PassedTime, target);
}

static void WorldTestFrame_Destroy(void* self)
{
    WorldTestFrame* Frame = self;

    if (Frame->_cursorCaptured)
    {
        EnableCursor();
        Frame->_cursorCaptured = false;
    }

    if (Frame->_renderer != NULL)
    {
        Error RendererResult = WorldRenderer_Deconstruct(Frame->_renderer);
        Error_Deconstruct(&RendererResult);
    }

    Error WorldResult = World_Deconstruct(&Frame->_world);
    Error_Deconstruct(&WorldResult);

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
/* Creates a model object with the given transform and hands it to the world (which takes ownership). On any
 * failure the partial object is destroyed and the error is returned. */
static Error AddModelObject(World* world, const unsigned char* name, const unsigned char* assetName,
    Vector3 position, Vector3 scale)
{
    WorldModelObject* ModelObject = NULL;
    Error Result = WorldModelObject_Create(name, assetName, &ModelObject);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WorldObject* Base = WorldModelObject_AsObject(ModelObject);
    Result = WorldObject_SetPosition(Base, position);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldObject_SetScale(Base, scale);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = World_AddObject(world, Base);
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldObject_Destroy(Base);
    }
    return Result;
}

/* Creates a point light with the given transform/color/reach and hands it to the world (which takes
 * ownership). On any failure the partial light is destroyed and the error is returned. */
static Error AddPointLight(World* world, const unsigned char* name, Vector3 position, Color color,
    float intensity, float size)
{
    WorldLight* Light = NULL;
    Error Result = WorldLight_Create(name, &Light);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WorldObject* Base = WorldLight_AsObject(Light);
    Light->Color = color;
    Result = WorldObject_SetPosition(Base, position);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldLight_SetIntensity(Light, intensity);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldLight_SetSize(Light, size);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = World_AddObject(world, Base);
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldObject_Destroy(Base);
    }
    return Result;
}

/* Builds the test world: a raised centre model that casts a shadow onto a wide ground slab, lit by the sun
 * plus a couple of coloured point lights near the centre model (to exercise point-light reach culling). */
static Error BuildTestWorld(World* world)
{
    Error ConstructResult = World_Construct(world);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        return ConstructResult;
    }

    // Day-night cycle settings. These live on the world's WorldEnvironment and are saved WITH the world
    // (see the WorldEncoder environment schema), so this is the place to tweak them for the test world.
    WorldEnvironment* Environment = World_GetEnvironment(world);
    Environment->IsDayNightCycleEnabled = true;                    // false freezes the clock at TimeOfDay
    Environment->DayLengthSeconds = TEST_DAY_LENGTH_SECONDS;        // real seconds for one full day
    Environment->TimeOfDay = WORLD_ENVIRONMENT_TIME_OF_DAY_NOON;    // 0=midnight, 0.25=dawn, 0.5=noon, 0.75=dusk
    Environment->SunAngle = TEST_SUN_ANGLE;                        // radians the noon sun leans from straight up

    // Centre model, raised so its shadow lands on the ground; then a wide, thin slab as the ground receiver.
    Error Result = AddModelObject(world, TEST_MODEL_OBJECT_NAME, TEST_MODEL_ASSET_NAME,
        (Vector3){ 0.0f, 1.2f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f });
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddModelObject(world, TEST_GROUND_OBJECT_NAME, TEST_MODEL_ASSET_NAME,
            (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 40.0f, 0.2f, 40.0f });
    }
    // Two coloured point lights flanking the centre cube (reach 10 units, so they light the cube + nearby
    // ground and fade out before the slab edges — exercises the per-object reach culling + forward shading).
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddPointLight(world, (const unsigned char*)u8"warm_light",
            (Vector3){ 2.5f, 1.6f, 2.5f }, (Color){ 255, 150, 60, 255 }, 6.0f, 10.0f);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddPointLight(world, (const unsigned char*)u8"cool_light",
            (Vector3){ -2.5f, 1.2f, -2.0f }, (Color){ 80, 150, 255, 255 }, 6.0f, 10.0f);
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

    Error WorldResult = BuildTestWorld(&Frame->_world);
    if (WorldResult.Code != ErrorCode_Success)
    {
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return WorldResult;
    }

    Error RendererResult = WorldRenderer_Create(services->Assets, services->Logger, services->Config, &Frame->_renderer);
    if (RendererResult.Code != ErrorCode_Success)
    {
        Error DeconstructResult = World_Deconstruct(&Frame->_world);
        Error_Deconstruct(&DeconstructResult);
        GameFrame_Deconstruct(&Frame->Base);
        Memory_Free(Frame);
        return RendererResult;
    }

    Frame->_services = services;
    Frame->_camera = GameCamera_Create((Vector3){ .x = 0.0f, .y = 2.5f, .z = -7.0f }, GAME_CAMERA_DEFAULT_FOV_Y);
    Frame->_camera.Pitch = -0.2f;
    Frame->_cursorCaptured = false;

    *outFrame = &Frame->Base;
    return Error_CreateSuccess();
}
