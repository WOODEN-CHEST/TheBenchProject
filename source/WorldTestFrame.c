#include "WorldTestFrame.h"
#include "GameFrame.h"
#include "Services.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelObject.h"
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

/** Base movement speed, in world units per second. */
#define MOVE_SPEED 6.0f
/** Multiplier applied to movement speed while the sprint key is held. */
#define SPRINT_MULTIPLIER 3.0f
/** Mouse look sensitivity, in radians per pixel of mouse movement. */
#define MOUSE_SENSITIVITY 0.0025f


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
            (const unsigned char*)u8"WorldTest: entered the world (WASD to move, mouse to look).");
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

    // Mouse look is per real frame, so it is applied here (Render runs once per frame) rather than in the
    // fixed-timestep Update (which can run several times per frame and would over-apply the mouse delta).
    if (Frame->_cursorCaptured)
    {
        Vector2 MouseDelta = GetMouseDelta();
        // Mouse-right must turn the view right: increasing yaw rotates forward toward +X, which renders on
        // the screen's left under the right-handed view, so a rightward mouse delta lowers yaw.
        Frame->_camera.Yaw -= MouseDelta.x * MOUSE_SENSITIVITY;
        Frame->_camera.Pitch -= MouseDelta.y * MOUSE_SENSITIVITY;
        Frame->_camera.Pitch = Clamp(Frame->_camera.Pitch, -GAME_CAMERA_MAX_PITCH, GAME_CAMERA_MAX_PITCH);
    }

    RenderContext Context;
    RenderContext_Create(&Context, &target, context->TargetAspectRatio, context->TargetAreaPosition);
    RenderContext_BeginRendering(&Context);
    Error RenderResult = WorldRenderer_Render(Frame->_renderer, &Frame->_world, &Frame->_camera, &Context);
    RenderContext_EndRendering(&Context);
    RenderContext_Deconstruct(&Context);

    return RenderResult;
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
/* Builds the test world (the test model at the origin). */
static Error BuildTestWorld(World* world)
{
    Error ConstructResult = World_Construct(world);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        return ConstructResult;
    }

    WorldModelObject* ModelObject = NULL;
    Error ModelResult = WorldModelObject_Create(TEST_MODEL_OBJECT_NAME, TEST_MODEL_ASSET_NAME, &ModelObject);
    if (ModelResult.Code != ErrorCode_Success)
    {
        Error DeconstructResult = World_Deconstruct(world);
        Error_Deconstruct(&DeconstructResult);
        return ModelResult;
    }

    Error AddResult = World_AddObject(world, WorldModelObject_AsObject(ModelObject));
    if (AddResult.Code != ErrorCode_Success)
    {
        WorldObject_Destroy(WorldModelObject_AsObject(ModelObject));
        Error DeconstructResult = World_Deconstruct(world);
        Error_Deconstruct(&DeconstructResult);
        return AddResult;
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

    Error RendererResult = WorldRenderer_Create(services->Assets, services->Logger, &Frame->_renderer);
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
