#include <inttypes.h>
#include <stdio.h>
#include "EditorFrame.h"
#include "EditorInternal.h"
#include "GameMath.h"
#include "Logger.h"
#include "Renderer.h"
#include "Services.h"
#include "UIRenderView.h"
#include "WorldCollision.h"
#include "WorldFile.h"
#include "WorldPathObjects.h"
#include "WorldPrefabs.h"
#include "WorldRenderer.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"


// Macros.
/** Debug name used in log and crash messages. */
#define FRAME_DEBUG_NAME ((const unsigned char*)u8"Editor")
/** How many rounds the generators take at load at most: prefabs in path objects in prefabs reach no deeper in anything built by hand. */
#define GENERATOR_SETTLE_ROUNDS 8
/** How far around the camera what keeps the sky out is looked for, for the eye indoors, in metres. */
#define CAMERA_OPENNESS_REACH 30.0f
/** How often the status line's place text is set at most while the camera moves, in seconds. */
#define PLACE_TEXT_PERIOD_SECONDS 0.1
/** Where the camera starts: the test frame's start, so their pictures can be compared. */
#define START_POSITION_X 0.0
#define START_POSITION_Y 2.5
#define START_POSITION_Z (-7.0)
#define START_PITCH (-0.2f)
/** What shows where the UI draws nothing (the workspace covers the surface). */
#define BACKGROUND_COLOR ((Color){ 18, 19, 22, 255 })


// Fields.
/* The fly actions' keys, by EditorFlyAction (input_actions/editor.json). */
static const unsigned char* const FlyActionKeys[EditorFlyAction_Count] =
{
    [EditorFlyAction_Forward] = u8"tbp:editor_fly_forward",
    [EditorFlyAction_Back] = u8"tbp:editor_fly_back",
    [EditorFlyAction_Left] = u8"tbp:editor_fly_left",
    [EditorFlyAction_Right] = u8"tbp:editor_fly_right",
    [EditorFlyAction_Up] = u8"tbp:editor_fly_up",
    [EditorFlyAction_Down] = u8"tbp:editor_fly_down",
    [EditorFlyAction_Fast] = u8"tbp:editor_fly_fast",
};

static const GameFrameVTable EditorFrameVTable;


// Static functions: helpers.
static void LogInfo(const EditorFrame* frame, const char* format, const char* text)
{
    if (frame->Services->Logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(frame->Services->Logger, (const unsigned char*)format, text);
        Error_Deconstruct(&LogResult);
    }
}

static void LogWarning(const EditorFrame* frame, const char* format, const char* text, const Error* error)
{
    if (frame->Services->Logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(frame->Services->Logger, (const unsigned char*)format, text,
            ((error != NULL) && (error->Message != NULL)) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
}

static EditorFrame* AsEditor(GameFrame* frame)
{
    return EditorFrame_IsEditorFrame(frame) ? (EditorFrame*)frame : NULL;
}

static const EditorFrame* AsConstEditor(const GameFrame* frame)
{
    return EditorFrame_IsEditorFrame(frame) ? (const EditorFrame*)frame : NULL;
}


// Static functions: the world.
/* Opens worlds/<name>.world.json; without a name, or when the name is no plain file name or the file does not load, the world is empty (and
 * the debug grid shows, so it is visible). Only failing to make even an empty world is an error. */
static Error LoadWorld(EditorFrame* frame)
{
    Services* Shared = frame->Services;
    Error Result = Error_CreateSuccess();
    if (frame->WorldName != NULL)
    {
        GenericBuffer Path;
        GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
        if (!Path_IsEntryNameValid(frame->WorldName))
        {
            Result = Error_Construct2(ErrorCode_InvalidPath, "a world's name is a plain file name, without its ending.");
        }
        else if (Shared->BaseDirectory == NULL)
        {
            Result = Error_Construct2(ErrorCode_IllegalArgument, "the game has no folder to open worlds from.");
        }
        else
        {
            Result = WorldFile_BuildPath(Shared->BaseDirectory, frame->WorldName, WorldFileFormat_JSON, &Path);
        }
        if (Result.Code == ErrorCode_Success)
        {
            WorldFileReadOptions Options = WorldFileReadOptions_Create(Shared->Keys, Shared->Assets, Shared->Logger);
            Result = WorldFile_LoadJSON(Path._data, &Options, &frame->World);
        }
        Memory_Free(Path._data);
        frame->IsWorldLoaded = Result.Code == ErrorCode_Success;
        frame->IsWorldConstructed = frame->IsWorldLoaded;
        if (frame->IsWorldLoaded)
        {
            if (Shared->Logger != NULL)
            {
                Error LogResult = Logger_LogInfoFormatted(Shared->Logger, (const unsigned char*)u8"Editor: opened the world \"%s\" (%" PRIu64 " objects).",
                    (const char*)frame->WorldName, (uint64_t)World_GetObjectCount(&frame->World));
                Error_Deconstruct(&LogResult);
            }
            return Error_CreateSuccess();
        }
        LogWarning(frame, "Editor: could not open the world \"%s\": %s The world is empty instead.", (const char*)frame->WorldName, &Result);
        Error_Deconstruct(&Result);
    }
    else
    {
        LogInfo(frame, "Editor: %s", "no world was named: the world is empty.");
    }
    Result = World_Construct2(&frame->World, Shared->Keys, Shared->Assets, Shared->Logger);
    frame->IsWorldConstructed = Result.Code == ErrorCode_Success;
    return Result;
}

/* What prefabs and path objects make, so the world shows as it is: each round reaches one level further, until a round makes nothing. */
static void SettleGenerators(EditorFrame* frame)
{
    for (int Round = 0; Round < GENERATOR_SETTLE_ROUNDS; Round++)
    {
        uint64_t Objects = World_GetObjectSetVersion(&frame->World);
        WorldPrefabs_Update(frame->Prefabs);
        WorldCollision_Sync(frame->Collision);
        WorldPathObjects_Update(frame->PathObjects);
        if ((World_GetObjectSetVersion(&frame->World) == Objects) && WorldPrefabs_IsSettled(frame->Prefabs))
        {
            break;
        }
    }
    World_UpdateTransforms(&frame->World);
    WorldCollision_Sync(frame->Collision);
}

/* The world's systems: a collision world that places rather than moves, the generators, the renderer. */
static Error BuildWorld(EditorFrame* frame)
{
    Services* Shared = frame->Services;
    Error Result = LoadWorld(frame);
    if (Result.Code == ErrorCode_Success)
    {
        WorldCollisionOptions Options = WorldCollisionOptions_CreateDefault();
        Options.AreMotionsSwept = false;
        Options.WarnsWhenStaticObjectsMove = false;
        Result = WorldCollision_Create(&frame->World, Shared->Assets, Shared->Logger, &Options, &frame->Collision);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldPrefabs_Create(&frame->World, Shared->Assets, Shared->Logger, &frame->Prefabs);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldPathObjects_Create(&frame->World, frame->Collision, Shared->Assets, Shared->Logger, &frame->PathObjects);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldRenderer_Create(Shared->Assets, Shared->Logger, Shared->Config, &frame->Renderer);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    SettleGenerators(frame);
    if (!frame->IsWorldLoaded)
    {
        WorldRenderer_SetDebugGridEnabled(frame->Renderer, true);
    }
    return Error_CreateSuccess();
}

/* The fly actions by key: an action whose pack did not load is missing, and reads as never held. */
static void FindFlyActions(EditorFrame* frame)
{
    GameInput* Input = frame->Services->Input;
    for (size_t Action = 0U; Action < (size_t)EditorFlyAction_Count; Action++)
    {
        frame->HasFlyAction[Action] = (Input != NULL) && GameInput_FindActionText(Input, FlyActionKeys[Action], &frame->FlyActions[Action]);
        if (!frame->HasFlyAction[Action])
        {
            LogWarning(frame, "Editor: the action %s is not registered (input_actions/editor.json): %s", (const char*)FlyActionKeys[Action], NULL);
        }
    }
}


// Static functions: the camera.
static bool IsFlyActionDown(const EditorFrame* frame, const GameInput* input, EditorFlyAction action)
{
    return frame->HasFlyAction[action] && GameInput_IsActionDown(input, frame->FlyActions[action]);
}

static float GetAxis(const EditorFrame* frame, const GameInput* input, EditorFlyAction positive, EditorFlyAction negative)
{
    return (IsFlyActionDown(frame, input, positive) ? 1.0f : 0.0f) - (IsFlyActionDown(frame, input, negative) ? 1.0f : 0.0f);
}

/* The tick's controls from the view's record: the wheel over the view always; flying, while the right button holds a press on the entered view,
 * with the pointer's motion and the fly actions. (A view that does not show hears neither: its presses are lost and the pointer is never over
 * it.) The actions are not consumed: a consumed key reads as up for as long as it is held, so the next tick would not fly. */
static EditorCameraControls ReadControls(const EditorFrame* frame, const GameInput* input, const UIRenderViewInput* view)
{
    EditorCameraControls Controls = { .IsFlying = false, .LookPixels = { 0.0f, 0.0f }, .Forward = 0.0f, .Right = 0.0f, .Up = 0.0f,
        .IsFast = false, .WheelNotches = view->Wheel.y };
    const UIRenderViewButton* Right = &view->Buttons[(int32_t)GameKey_MouseRight - (int32_t)GameKey_MouseLeft];
    if (!view->IsEntered || !Right->IsDown)
    {
        return Controls;
    }
    Controls.IsFlying = true;
    Controls.LookPixels = Right->DeltaPixels;
    if (input == NULL)
    {
        return Controls;
    }
    Controls.Forward = GetAxis(frame, input, EditorFlyAction_Forward, EditorFlyAction_Back);
    Controls.Right = GetAxis(frame, input, EditorFlyAction_Right, EditorFlyAction_Left);
    Controls.Up = GetAxis(frame, input, EditorFlyAction_Up, EditorFlyAction_Down);
    Controls.IsFast = IsFlyActionDown(frame, input, EditorFlyAction_Fast);
    return Controls;
}

/* The status line's place text, at most every PLACE_TEXT_PERIOD_SECONDS while the camera moves. */
static Error UpdatePlaceText(EditorFrame* frame, double time)
{
    if (!frame->IsPlaceTextOld || ((time - frame->PlaceTextTime) < PLACE_TEXT_PERIOD_SECONDS))
    {
        return Error_CreateSuccess();
    }
    char Text[EDITOR_PLACE_TEXT_CAPACITY];
    Vector3Double Position = frame->Camera.Position;
    snprintf(Text, sizeof(Text), "x %.1f, y %.1f, z %.1f m; %.2f m/s", Position.X, Position.Y, Position.Z,
        (double)EditorCamera_GetSpeed(&frame->Flight));
    frame->IsPlaceTextOld = false;
    frame->PlaceTextTime = time;
    return EditorShell_SetPlaceText(&frame->Shell, Text);
}


// Static functions: the view's picture.
/* A render target the view's size that GL completed; one it could not complete is unloaded and the target given is all zero. */
static RenderTexture2D LoadCompleteTarget(int width, int height)
{
    RenderTexture2D Target = LoadRenderTexture(width, height);
    bool IsComplete = (Target.id != 0U) && (Target.texture.id != 0U) && (Target.depth.id != 0U) && rlFramebufferComplete(Target.id);
    if (IsComplete)
    {
        return Target;
    }
    if (Target.id != 0U)
    {
        UnloadRenderTexture(Target);
    }
    return (RenderTexture2D){ 0 };
}

/* Unloads the view's picture. The view it was handed to lets go of it first while that view is there (@p screen is NULL once the UI is gone),
 * so no view shows a texture that was unloaded. */
static void ReleaseViewTarget(EditorFrame* frame, UIScreen* screen)
{
    if ((screen != NULL) && (frame->TargetViewId != WIDGET_ID_INVALID) && UIScreen_IsWidgetAlive(screen, frame->TargetViewId))
    {
        Error Result = UIRenderView_ClearTexture(screen, frame->TargetViewId);
        Error_Deconstruct(&Result);
    }
    if (frame->HasViewTarget)
    {
        frame->TargetFunctions.Unload(frame->ViewTarget);
        frame->HasViewTarget = false;
    }
    frame->TargetViewId = WIDGET_ID_INVALID;
}

/* The view's picture the view's size, made again at every new size; handed to the view whenever it is new to it. False when no picture shows
 * this render: the view is closed (its picture is let go), hidden (kept for when it shows again), or none could be made: logged once, and
 * not tried again at that size (the view shows its background until its size changes, or a view opened again tries once more). */
static bool EnsureViewTarget(EditorFrame* frame)
{
    UIScreen* Screen = &frame->Shell.Screen;
    uint64_t ViewId = frame->Shell.ViewId;
    if ((ViewId == WIDGET_ID_INVALID) || !UIScreen_IsWidgetAlive(Screen, ViewId))
    {
        ReleaseViewTarget(frame, Screen);
        frame->HasFailedTargetSize = false;
        return false;
    }
    bool IsShown = false;
    Int32Vector Size = { .X = 0, .Y = 0 };
    Error Result = UIRenderView_IsShown(Screen, ViewId, &IsShown);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIRenderView_GetPixelSize(Screen, ViewId, &Size);
    }
    if ((Result.Code != ErrorCode_Success) || !IsShown)
    {
        Error_Deconstruct(&Result);
        return false;
    }
    if (!frame->HasViewTarget || (frame->ViewTarget.texture.width != Size.X) || (frame->ViewTarget.texture.height != Size.Y))
    {
        if (frame->HasFailedTargetSize && (frame->FailedTargetSize.X == Size.X) && (frame->FailedTargetSize.Y == Size.Y))
        {
            return false;
        }
        ReleaseViewTarget(frame, Screen);
        RenderTexture2D Target = frame->TargetFunctions.Load(Size.X, Size.Y);
        if (Target.id == 0U)
        {
            frame->HasFailedTargetSize = true;
            frame->FailedTargetSize = Size;
            if (!frame->IsTargetFailureLogged)
            {
                char Text[64];
                snprintf(Text, sizeof(Text), "%" PRId32 " x %" PRId32, Size.X, Size.Y);
                LogWarning(frame, "Editor: the world's picture of %s pixels could not be made: %s", Text, NULL);
                frame->IsTargetFailureLogged = true;
            }
            return false;
        }
        frame->ViewTarget = Target;
        frame->HasViewTarget = true;
        frame->IsTargetFailureLogged = false;
        frame->HasFailedTargetSize = false;
    }
    if (frame->TargetViewId != ViewId)
    {
        // A render target's rows run bottom-up.
        Result = UIRenderView_SetTexture(Screen, ViewId, frame->ViewTarget.texture, true);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return false;
        }
        frame->TargetViewId = ViewId;
    }
    return true;
}


// Static functions: vtable behaviour.
static Error EditorFrame_Start(void* self, ProgramTime time)
{
    (void)time;
    LogInfo(self, "Editor: %s", "hold the right mouse button in the world to look around; W, A, S, D, Q and E fly while it is held; the wheel changes "
        "the speed. The Window menu opens the windows, the Layout menu keeps arrangements.");
    return Error_CreateSuccess();
}

static Error EditorFrame_End(void* self, ProgramTime time)
{
    (void)self;
    (void)time;
    return Error_CreateSuccess();
}

static Error EditorFrame_Update(void* self, ProgramTime time)
{
    EditorFrame* Frame = self;
    Frame->Time = time.TotalTime;
    GameInput* Input = Frame->Services->Input;
    UIScreen* Screen = &Frame->Shell.Screen;
    Error Result = UIWindowHost_Update(&Frame->Shell.Host, Screen, time, Input);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // The view goes with the world's window; its record is taken every tick, so nothing old waits in it.
    if ((Frame->Shell.ViewId != WIDGET_ID_INVALID) && !UIScreen_IsWidgetAlive(Screen, Frame->Shell.ViewId))
    {
        Frame->Shell.ViewId = WIDGET_ID_INVALID;
    }
    UIRenderViewInput View;
    Memory_Zero(&View, sizeof(View));
    if (Frame->Shell.ViewId != WIDGET_ID_INVALID)
    {
        Result = UIRenderView_TakeInput(Screen, Frame->Shell.ViewId, &View);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    EditorCameraControls Controls = ReadControls(Frame, Input, &View);
    EditorCameraChange Change = EditorCamera_Apply(&Frame->Flight, &Frame->Camera, &Controls, time.PassedTime);
    Frame->IsPlaceTextOld = Frame->IsPlaceTextOld || Change.HasMoved || Change.HasSpeedChanged;
    // TODO: the document's selection, picking and the active tool read the same record here, after the camera.

    // What the generators make, the world matrices of what moved, the colliders where they are: the next tick's tools read them.
    WorldPrefabs_Update(Frame->Prefabs);
    WorldPathObjects_Update(Frame->PathObjects);
    World_UpdateTransforms(&Frame->World);
    WorldCollision_Sync(Frame->Collision);
    return UpdatePlaceText(Frame, time.TotalTime);
}

static Error EditorFrame_BeginLoad(void* self)
{
    EditorFrame* Frame = self;
    return WorldRenderer_PrepareWorld(Frame->Renderer, &Frame->World);
}

static Error EditorFrame_NoStep(void* self)
{
    (void)self;
    return Error_CreateSuccess();
}

static bool EditorFrame_IsDone(void* self)
{
    (void)self;
    return true;
}

/* The update side's last step before a render: the view's picture its size (or none while it does not show), and how open the camera's
 * surroundings are to the sky, which the eye adapts indoors by. */
static Error EditorFrame_PrepareRender(void* self, ProgramTime time)
{
    (void)time;
    EditorFrame* Frame = self;
    Frame->IsViewShown = EnsureViewTarget(Frame);
    if (!Frame->IsViewShown)
    {
        return Error_CreateSuccess();
    }
    Frame->WorldRenderCount++;
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Prop),
        WorldCollisionTriggerMode_Ignore);
    WorldRenderer_SetCameraOpenness(Frame->Renderer, WorldCollision_GetSkyOpenness(Frame->Collision, Frame->Camera.Position,
        CAMERA_OPENNESS_REACH, &Filter));
    return Error_CreateSuccess();
}

/* The world into the view's picture first (it opens and closes its own passes), then the UI, which shows that picture in the view. */
static Error EditorFrame_Render(void* self, const FrameRenderContext* context, RenderTexture2D target)
{
    EditorFrame* Frame = self;
    if (Frame->IsViewShown && Frame->HasViewTarget)
    {
        Error Result = WorldRenderer_RenderToTarget(Frame->Renderer, &Frame->World, &Frame->Camera, context->Time.PassedTime, Frame->ViewTarget);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    RenderContext Context;
    RenderContext_Create(&Context, &target, context->TargetAspectRatio, context->TargetAreaPosition);
    RenderContext_BeginRendering(&Context);
    ClearBackground(BACKGROUND_COLOR);
    Error Result = UIScreen_Render(&Frame->Shell.Screen, &Context);
    RenderContext_EndRendering(&Context);
    RenderContext_Deconstruct(&Context);
    return Result;
}

/* Tears down whatever was built, in the reverse of building: the UI (its view goes before its picture), the picture, the renderer, the
 * generators and the collision world (they borrow the world), the world. */
static void EditorFrame_Destroy(void* self)
{
    EditorFrame* Frame = self;
    EditorShell_Deconstruct(&Frame->Shell);
    ReleaseViewTarget(Frame, NULL); // Its view went with the UI.
    if (Frame->Renderer != NULL)
    {
        Error Result = WorldRenderer_Deconstruct(Frame->Renderer);
        Error_Deconstruct(&Result);
        Frame->Renderer = NULL;
    }
    if (Frame->PathObjects != NULL)
    {
        WorldPathObjects_Destroy(Frame->PathObjects);
        Frame->PathObjects = NULL;
    }
    if (Frame->Prefabs != NULL)
    {
        WorldPrefabs_Destroy(Frame->Prefabs);
        Frame->Prefabs = NULL;
    }
    if (Frame->Collision != NULL)
    {
        WorldCollision_Destroy(Frame->Collision);
        Frame->Collision = NULL;
    }
    if (Frame->IsWorldConstructed)
    {
        Error Result = World_Deconstruct(&Frame->World);
        Error_Deconstruct(&Result);
        Frame->IsWorldConstructed = false;
    }
    EditorCamera_Deconstruct(&Frame->Flight);
    Memory_Free(Frame->WorldName);
    GameFrame_Deconstruct(&Frame->Base);
    Memory_Free(Frame);
}


// Fields.
static const GameFrameVTable EditorFrameVTable =
{
    .Start = EditorFrame_Start,
    .End = EditorFrame_End,
    .Update = EditorFrame_Update,
    .BeginLoad = EditorFrame_BeginLoad,
    .LoadStep = EditorFrame_NoStep,
    .IsLoaded = EditorFrame_IsDone,
    .BeginUnload = EditorFrame_NoStep,
    .UnloadStep = EditorFrame_NoStep,
    .IsUnloaded = EditorFrame_IsDone,
    .PrepareRender = EditorFrame_PrepareRender,
    .Render = EditorFrame_Render,
    .GetLoadProgress = NULL,
    .OnResize = NULL,
    .Destroy = EditorFrame_Destroy
};


// Public functions.
Error EditorFrame_Create(Services* services, const unsigned char* worldName, GameFrame** outFrame)
{
    if ((services == NULL) || (outFrame == NULL) || (services->Assets == NULL) || (services->Keys == NULL) || (services->WindowTypes == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "EditorFrame_Create: services and outFrame must not be NULL, and the services need their assets, keys and window types.");
    }
    *outFrame = NULL;
    EditorFrame* Frame = Memory_Allocate(sizeof(EditorFrame));
    Memory_Zero(Frame, sizeof(*Frame));
    Frame->Services = services;
    Frame->TargetFunctions = (EditorTargetFunctions){ .Load = LoadCompleteTarget, .Unload = UnloadRenderTexture };
    Frame->TargetViewId = WIDGET_ID_INVALID;
    Frame->IsPlaceTextOld = true;
    Frame->PlaceTextTime = -PLACE_TEXT_PERIOD_SECONDS;
    Frame->Camera = GameCamera_Create(Vector3Double_Create(START_POSITION_X, START_POSITION_Y, START_POSITION_Z), GAME_CAMERA_DEFAULT_FOV_Y);
    Frame->Camera.Pitch = START_PITCH;
    EditorCamera_Construct(&Frame->Flight);
    if (worldName != NULL)
    {
        size_t NameBytes = StringUTF8_GetByteLength(worldName) + 1U;
        Frame->WorldName = Memory_Allocate(NameBytes);
        Memory_Copy(worldName, Frame->WorldName, NameBytes);
    }
    Error Result = GameFrame_Construct(&Frame->Base, &EditorFrameVTable, FRAME_DEBUG_NAME);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Frame);
        return Result;
    }
    Result = BuildWorld(Frame);
    if (Result.Code == ErrorCode_Success)
    {
        FindFlyActions(Frame);
        Result = EditorShell_Construct(&Frame->Shell, Frame);
    }
    if (Result.Code != ErrorCode_Success)
    {
        EditorFrame_Destroy(Frame); // It tears down whatever was built.
        return Result;
    }
    *outFrame = &Frame->Base;
    return Error_CreateSuccess();
}

bool EditorFrame_IsEditorFrame(const GameFrame* frame)
{
    return (frame != NULL) && (frame->VTable == &EditorFrameVTable);
}

World* EditorFrame_GetWorld(GameFrame* frame)
{
    EditorFrame* Frame = AsEditor(frame);
    return (Frame != NULL) ? &Frame->World : NULL;
}

WorldCollision* EditorFrame_GetCollision(GameFrame* frame)
{
    EditorFrame* Frame = AsEditor(frame);
    return (Frame != NULL) ? Frame->Collision : NULL;
}

bool EditorFrame_IsWorldLoaded(const GameFrame* frame)
{
    const EditorFrame* Frame = AsConstEditor(frame);
    return (Frame != NULL) && Frame->IsWorldLoaded;
}

GameCamera* EditorFrame_GetCamera(GameFrame* frame)
{
    EditorFrame* Frame = AsEditor(frame);
    return (Frame != NULL) ? &Frame->Camera : NULL;
}

float EditorFrame_GetFlySpeed(const GameFrame* frame)
{
    const EditorFrame* Frame = AsConstEditor(frame);
    return (Frame != NULL) ? EditorCamera_GetSpeed(&Frame->Flight) : 0.0f;
}

UIScreen* EditorFrame_GetScreen(GameFrame* frame)
{
    EditorFrame* Frame = AsEditor(frame);
    return (Frame != NULL) ? &Frame->Shell.Screen : NULL;
}

uint64_t EditorFrame_GetWorkspaceId(const GameFrame* frame)
{
    const EditorFrame* Frame = AsConstEditor(frame);
    return (Frame != NULL) ? Frame->Shell.WorkspaceId : WIDGET_ID_INVALID;
}

uint64_t EditorFrame_GetViewId(const GameFrame* frame)
{
    const EditorFrame* Frame = AsConstEditor(frame);
    return (Frame != NULL) ? Frame->Shell.ViewId : WIDGET_ID_INVALID;
}

uint64_t EditorFrame_GetWorldRenderCount(const GameFrame* frame)
{
    const EditorFrame* Frame = AsConstEditor(frame);
    return (Frame != NULL) ? Frame->WorldRenderCount : 0U;
}

void EditorFrame_SetHostCursor(GameFrame* frame, const UIWindowHostCursor* cursor)
{
    EditorFrame* Frame = AsEditor(frame);
    if ((Frame != NULL) && (cursor != NULL))
    {
        UIWindowHost_SetCursor(&Frame->Shell.Host, cursor);
    }
}


// Internal functions (declared in EditorInternal.h).
void EditorFrame_InternalSetTargetFunctions(GameFrame* frame, const EditorTargetFunctions* functions)
{
    EditorFrame* Frame = AsEditor(frame);
    if ((Frame == NULL) || (functions == NULL) || (functions->Load == NULL) || (functions->Unload == NULL))
    {
        return;
    }
    ReleaseViewTarget(Frame, &Frame->Shell.Screen);
    Frame->TargetFunctions = *functions;
}
