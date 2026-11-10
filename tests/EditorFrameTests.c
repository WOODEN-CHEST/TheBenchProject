#include "TestSuites.h"
#include "TestAssets.h"
#include "AssetManager.h"
#include "EditorFrame.h"
#include "EditorInternal.h"
#include "GameFrame.h"
#include "GameInput.h"
#include "GameMath.h"
#include "InputActions.h"
#include "NamespacedKey.h"
#include "Registry.h"
#include "Services.h"
#include "UIKit.h"
#include "UIRenderView.h"
#include "UIScreen.h"
#include "UIWindow.h"
#include "UIWindowTypes.h"
#include "UIWorkspace.h"
#include "World.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldFile.h"
#include "WorldObject.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include <math.h>
#include <string.h>


// Macros.
/** The fixture's tick, in seconds (the game's update rate). */
#define TICK_SECONDS (1.0 / 480.0)
/** The fixture's window. */
#define WINDOW_WIDTH 1280.0f
#define WINDOW_HEIGHT 720.0f
/** Room for paths. */
#define PATH_CAPACITY ((size_t)256)
/** Where the code-built world's crate stands. */
#define CRATE_X 0.0
#define CRATE_Y 1.0
#define CRATE_Z 10.0


// Types.
/* What the editor's UI did to the cursor, with no window. */
typedef struct CursorRecordStruct
{
    size_t Captures;
    size_t Releases;
} CursorRecord;

/* The editor with what it needs, and no window: a test asset manager (no theme, no font: its UI works in plain boxes), the program's
 * registries, an input with the shipped editor actions, the window types and a work folder for its worlds and layouts. */
typedef struct EditorFixtureStruct
{
    GenericBuffer Work;
    TestAssetFixture Assets;
    KeyRegistry Keys;
    RegistryRoot Root;
    GameInput Input;
    GameInputSample Sample;
    UIWindowTypes Types;
    Services Shared;
    GameFrame* Frame;
    CursorRecord Cursor;
    double Time;
    bool HasAssets;
    bool HasKeys;
    bool HasRoot;
    bool HasInput;
    bool HasTypes;
} EditorFixture;

/* What the editor did with the view's pictures, made and unloaded by stand-ins (the tests have no GL). */
typedef struct TargetRecordStruct
{
    /** The editor whose view is looked at as each picture is unloaded; NULL while it is torn down (its UI goes first). */
    GameFrame* Frame;
    size_t Loads;
    size_t Unloads;
    /** The last picture made (its texture's id is 1000 more), and the size last asked for. */
    unsigned int LastId;
    Int32Vector LastSize;
    /** The next pictures cannot be made. */
    bool IsRefusing;
    /** A picture was unloaded while the view still showed it. */
    bool WasShownWhenUnloaded;
} TargetRecord;


// Fields.
/* The stand-ins' record: they take no context, as raylib's functions do not. */
static TargetRecord Targets;


// Static functions: the cursor.
static void RecordCapture(void* context, GameInput* input)
{
    (void)input;
    ((CursorRecord*)context)->Captures++;
}

static void RecordRelease(void* context, GameInput* input, Vector2 position)
{
    (void)input;
    (void)position;
    ((CursorRecord*)context)->Releases++;
}

static void RecordShape(void* context, UICursorShape shape)
{
    (void)context;
    (void)shape;
}



// Static functions: the view's picture, without GL.
/* The texture's id the editor's view shows; 0 for none. */
static unsigned int ShownTexture(GameFrame* frame)
{
    UIScreen* Screen = EditorFrame_GetScreen(frame);
    Widget* View = NULL;
    if (!UIScreen_TryGetWidgetById(Screen, EditorFrame_GetViewId(frame), &View))
    {
        return 0U;
    }
    UIRenderViewCapability* Capability = Widget_GetCapability(View, UIKit_GetCapabilityId(UIKit_FromScreen(Screen), UIElementCapability_RenderView));
    Texture2D Shown = { 0 };
    bool IsBottomUp = false;
    return ((Capability != NULL) && UIRenderViewCapability_GetTexture(Capability, &Shown, &IsBottomUp)) ? Shown.id : 0U;
}

static RenderTexture2D FakeLoad(int width, int height)
{
    Targets.Loads++;
    Targets.LastSize = (Int32Vector){ .X = width, .Y = height };
    if (Targets.IsRefusing)
    {
        return (RenderTexture2D){ 0 };
    }
    Targets.LastId++;
    unsigned int Id = Targets.LastId;
    return (RenderTexture2D){ .id = Id, .texture = { .id = Id + 1000U, .width = width, .height = height, .mipmaps = 1, .format = 7 },
        .depth = { .id = Id + 2000U, .width = width, .height = height, .mipmaps = 1, .format = 19 } };
}

static void FakeUnload(RenderTexture2D target)
{
    Targets.Unloads++;
    if ((Targets.Frame != NULL) && (ShownTexture(Targets.Frame) == target.texture.id))
    {
        Targets.WasShownWhenUnloaded = true;
    }
}


// Static functions: the fixture.
/* The shipped input_actions definitions (the editor's among them), registered as the game does at start. */
static bool RegisterShippedActions(TestRun* run, GameInput* input)
{
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, PATH_CAPACITY, sizeof(unsigned char));
    AssetManager* Manager = NULL;
    StandardAssetTypes Types;
    JSONObjectPool* Pool = NULL;
    bool IsRegistered = TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Manager, &Types, &Pool))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Manager, Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Manager, NULL))
        && TEST_CHECK_SUCCESS(run, InputActions_RegisterDefinitions(Manager, input, NULL));
    if (Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(Manager));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Root._data);
    return IsRegistered;
}

/* Everything but the frame, its work folder <tests' work>/<folder>. */
static bool ConstructServices(TestRun* run, EditorFixture* fixture, const char* folder)
{
    Memory_Zero(fixture, sizeof(*fixture));
    GenericBuffer_AllocateVariable(&fixture->Work, PATH_CAPACITY, sizeof(unsigned char));
    fixture->HasAssets = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, folder, &fixture->Work))
        && TestAssetFixture_Construct(run, &fixture->Assets);
    fixture->HasKeys = fixture->HasAssets && TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&fixture->Keys));
    fixture->HasRoot = fixture->HasKeys && TEST_CHECK_SUCCESS(run, RegistryRoot_Construct(&fixture->Root, &fixture->Keys));
    fixture->HasInput = fixture->HasRoot && TEST_CHECK_SUCCESS(run, GameInput_Construct(&fixture->Input, &fixture->Keys, &fixture->Root, NULL));
    fixture->HasTypes = fixture->HasInput && TEST_CHECK_SUCCESS(run, UIWindowTypes_Construct(&fixture->Types, &fixture->Keys, &fixture->Root));
    if (!fixture->HasTypes || !RegisterShippedActions(run, &fixture->Input))
    {
        return false;
    }
    GameInputSample_Clear(&fixture->Sample);
    fixture->Sample.WindowSize = (Vector2){ .x = WINDOW_WIDTH, .y = WINDOW_HEIGHT };
    fixture->Sample.MousePosition = (Vector2){ .x = 5.0f, .y = WINDOW_HEIGHT - 5.0f };
    Services_Construct(&fixture->Shared);
    fixture->Shared.Assets = fixture->Assets.Manager;
    fixture->Shared.AssetTypes = fixture->Assets.Types;
    fixture->Shared.Keys = &fixture->Keys;
    fixture->Shared.Registries = &fixture->Root;
    fixture->Shared.Input = &fixture->Input;
    fixture->Shared.WindowTypes = &fixture->Types;
    fixture->Shared.BaseDirectory = fixture->Work._data;
    return true;
}

static void Tick(TestRun* run, EditorFixture* fixture)
{
    GameInput_BeginTick(&fixture->Input, &fixture->Sample);
    fixture->Time += TICK_SECONDS;
    TEST_CHECK_SUCCESS(run, GameFrame_Update(fixture->Frame, ProgramTime_Create(fixture->Time, TICK_SECONDS)));
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        uint8_t State = fixture->Sample.KeyStates[Index];
        fixture->Sample.KeyStates[Index] = ((State & GAME_INPUT_KEY_STATE_RELEASED) != 0) ? 0U : (uint8_t)(State & GAME_INPUT_KEY_STATE_DOWN);
    }
    fixture->Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 0.0f };
    fixture->Sample.MouseDelta = (Vector2){ .x = 0.0f, .y = 0.0f };
}

static void Ticks(TestRun* run, EditorFixture* fixture, size_t count)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        Tick(run, fixture);
    }
}

/* The editor over the services, its cursor recorded, started and laid out. */
static bool CreateFrame(TestRun* run, EditorFixture* fixture, const char* worldName)
{
    if (!TEST_CHECK_SUCCESS(run, EditorFrame_Create(&fixture->Shared, (const unsigned char*)worldName, &fixture->Frame)))
    {
        fixture->Frame = NULL;
        return false;
    }
    UIWindowHostCursor Cursor = { .Capture = RecordCapture, .Release = RecordRelease, .SetShape = RecordShape, .Context = &fixture->Cursor };
    EditorFrame_SetHostCursor(fixture->Frame, &Cursor);
    TEST_CHECK_SUCCESS(run, GameFrame_Start(fixture->Frame, ProgramTime_Create(fixture->Time, 0.0)));
    Ticks(run, fixture, 3U);
    return true;
}

static void DestroyFrame(TestRun* run, EditorFixture* fixture)
{
    if (fixture->Frame != NULL)
    {
        TEST_CHECK_SUCCESS(run, GameFrame_End(fixture->Frame, ProgramTime_Create(fixture->Time, 0.0)));
        GameFrame_Destroy(fixture->Frame);
        fixture->Frame = NULL;
    }
}

static void Deconstruct(TestRun* run, EditorFixture* fixture)
{
    DestroyFrame(run, fixture);
    if (fixture->HasTypes)
    {
        UIWindowTypes_Deconstruct(&fixture->Types);
    }
    if (fixture->HasInput)
    {
        TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&fixture->Input));
    }
    if (fixture->HasRoot)
    {
        RegistryRoot_Deconstruct(&fixture->Root);
    }
    if (fixture->HasKeys)
    {
        KeyRegistry_Deconstruct(&fixture->Keys);
    }
    if (fixture->HasAssets)
    {
        TestAssetFixture_Deconstruct(run, &fixture->Assets);
    }
    Memory_Free(fixture->Work._data);
}

/* A world built in code, written where the editor opens it from: one crate with a box of its own, movable. @p relative is where under the
 * work folder its file goes ("worlds/code.world.json"). */
static bool WriteWorld(TestRun* run, EditorFixture* fixture, const char* relative, uint64_t* outCrateId)
{
    World Built;
    if (!TEST_CHECK_SUCCESS(run, World_Construct2(&Built, &fixture->Keys, fixture->Assets.Manager, NULL)))
    {
        return false;
    }
    WorldObject* Crate = NULL;
    WorldComponent* Component = NULL;
    CollisionShape Box = CollisionShape_Create(CollisionShapeType_Box);
    Box.HalfExtents = (Vector3){ .x = 0.5f, .y = 0.5f, .z = 0.5f };
    bool IsBuilt = TEST_CHECK_SUCCESS(run, World_CreateObject(&Built, u8"crate", &Crate))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Crate, Vector3Double_Create(CRATE_X, CRATE_Y, CRATE_Z)))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Crate, WorldMobility_Movable))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&Built, WorldObject_GetId(Crate), WorldComponentType_Collider, &Component))
        && TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(WorldColliderComponent_FromComponent(Component), &Box))
        && TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(WorldColliderComponent_FromComponent(Component), true));
    if (IsBuilt)
    {
        *outCrateId = WorldObject_GetId(Crate);
        GenericBuffer Folder;
        GenericBuffer Path;
        GenericBuffer_AllocateVariable(&Folder, PATH_CAPACITY, sizeof(unsigned char));
        GenericBuffer_AllocateVariable(&Path, PATH_CAPACITY, sizeof(unsigned char));
        IsBuilt = TEST_CHECK_SUCCESS(run, Path_Append(fixture->Work._data, u8"worlds", &Folder))
            && TEST_CHECK_SUCCESS(run, FileSystem_CreateAllDirectories(Folder._data))
            && TEST_CHECK_SUCCESS(run, Path_Append(fixture->Work._data, (const unsigned char*)relative, &Path))
            && TEST_CHECK_SUCCESS(run, WorldFile_SaveJSON(&Built, Path._data));
        Memory_Free(Folder._data);
        Memory_Free(Path._data);
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&Built));
    return IsBuilt;
}

static UIScreen* ScreenOf(EditorFixture* fixture)
{
    return EditorFrame_GetScreen(fixture->Frame);
}

static Rectangle BoxOf(EditorFixture* fixture, uint64_t widgetId)
{
    Widget* Found = NULL;
    UIScreen* Screen = ScreenOf(fixture);
    return UIScreen_TryGetWidgetById(Screen, widgetId, &Found) ? UIScreen_GetWidgetPixelBox(Screen, Found)
        : (Rectangle){ .x = -1.0f, .y = -1.0f, .width = 0.0f, .height = 0.0f };
}

/* The window the view stands in: up from it to the first window. */
static uint64_t WorldWindowOf(EditorFixture* fixture)
{
    UIScreen* Screen = ScreenOf(fixture);
    Widget* Current = NULL;
    if (!UIScreen_TryGetWidgetById(Screen, EditorFrame_GetViewId(fixture->Frame), &Current))
    {
        return WIDGET_ID_INVALID;
    }
    for (; Current != NULL; Current = Widget_GetParent(Current))
    {
        if (UIWindow_IsWindow(Screen, Widget_GetId(Current)))
        {
            return Widget_GetId(Current);
        }
    }
    return WIDGET_ID_INVALID;
}

static void MoveMouse(EditorFixture* fixture, float x, float y)
{
    fixture->Sample.MousePosition = (Vector2){ .x = x, .y = y };
}

static void Press(EditorFixture* fixture, GameKey key)
{
    fixture->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
}

static void Release(EditorFixture* fixture, GameKey key)
{
    fixture->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_RELEASED;
}

/* The middle of the world's view, in window pixels. */
static Vector2 ViewMiddle(EditorFixture* fixture)
{
    Rectangle Box = BoxOf(fixture, EditorFrame_GetViewId(fixture->Frame));
    return (Vector2){ .x = Box.x + (Box.width * 0.5f), .y = Box.y + (Box.height * 0.5f) };
}

static double Moved(Vector3Double from, Vector3Double to)
{
    return Vector3Double_Length(Vector3Double_Subtract(to, from));
}

/* The update side's step before a shown frame's render. */
static void PrepareFrame(TestRun* run, EditorFixture* fixture)
{
    TEST_CHECK_SUCCESS(run, GameFrame_PrepareRender(fixture->Frame, ProgramTime_Create(fixture->Time, 0.0)));
}

static Int32Vector ViewSize(TestRun* run, EditorFixture* fixture)
{
    Int32Vector Size = { .X = -1, .Y = -1 };
    TEST_CHECK_SUCCESS(run, UIRenderView_GetPixelSize(ScreenOf(fixture), EditorFrame_GetViewId(fixture->Frame), &Size));
    return Size;
}

static bool IsLastMadeShown(EditorFixture* fixture)
{
    return (Targets.LastId != 0U) && (ShownTexture(fixture->Frame) == (Targets.LastId + 1000U));
}

/* Holds W for @p ticks and gives how far the camera went. */
static double HoldForward(TestRun* run, EditorFixture* fixture, size_t ticks)
{
    Vector3Double Start = EditorFrame_GetCamera(fixture->Frame)->Position;
    Press(fixture, GameKey_W);
    Ticks(run, fixture, ticks);
    Release(fixture, GameKey_W);
    Tick(run, fixture);
    return Moved(Start, EditorFrame_GetCamera(fixture->Frame)->Position);
}


// Tests.
static void WithoutANameTheWorldIsEmpty(TestRun* run)
{
    EditorFixture Fixture;
    if (ConstructServices(run, &Fixture, "editor_empty") && CreateFrame(run, &Fixture, NULL))
    {
        TEST_CHECK(run, EditorFrame_IsEditorFrame(Fixture.Frame) && !EditorFrame_IsWorldLoaded(Fixture.Frame));
        TEST_CHECK(run, World_GetObjectCount(EditorFrame_GetWorld(Fixture.Frame)) == 0U);
        // The world's window is open, docked, and its view shows.
        uint64_t ViewId = EditorFrame_GetViewId(Fixture.Frame);
        bool IsShown = false;
        TEST_CHECK(run, ViewId != WIDGET_ID_INVALID);
        TEST_CHECK_SUCCESS(run, UIRenderView_IsShown(ScreenOf(&Fixture), ViewId, &IsShown));
        UIWorkspacePlace Place = UIWorkspacePlace_Floating;
        TEST_CHECK(run, IsShown && UIWorkspace_GetPlace(ScreenOf(&Fixture), EditorFrame_GetWorkspaceId(Fixture.Frame), WorldWindowOf(&Fixture), &Place)
            && (Place == UIWorkspacePlace_Docked));
        // Nothing has the focus to begin with; the first press on the world's tab gives its view the keys, entered.
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(ScreenOf(&Fixture)) == WIDGET_ID_INVALID);
        uint64_t Tab = UIWorkspace_GetTabId(ScreenOf(&Fixture), EditorFrame_GetWorkspaceId(Fixture.Frame), WorldWindowOf(&Fixture));
        Rectangle TabBox = BoxOf(&Fixture, Tab);
        MoveMouse(&Fixture, TabBox.x + 6.0f, TabBox.y + (TabBox.height * 0.5f));
        Tick(run, &Fixture);
        Press(&Fixture, GameKey_MouseLeft);
        Tick(run, &Fixture);
        Release(&Fixture, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(ScreenOf(&Fixture)) == ViewId);
    }
    Deconstruct(run, &Fixture);
}

static void ANameOpensItsWorldAndABadOneNothing(TestRun* run)
{
    EditorFixture Fixture;
    uint64_t CrateId = 0U;
    if (ConstructServices(run, &Fixture, "editor_names") && WriteWorld(run, &Fixture, "worlds/code.world.json", &CrateId)
        && WriteWorld(run, &Fixture, "code.world.json", &CrateId))
    {
        if (CreateFrame(run, &Fixture, "code"))
        {
            TEST_CHECK(run, EditorFrame_IsWorldLoaded(Fixture.Frame) && (World_GetObjectCount(EditorFrame_GetWorld(Fixture.Frame)) == 1U));
        }
        DestroyFrame(run, &Fixture);
        // A name that is no plain file name opens nothing, not even the file it would reach.
        static const char* const BadNames[] = { "missing", "../code", "worlds/code", "" };
        for (size_t Index = 0U; Index < (sizeof(BadNames) / sizeof(BadNames[0])); Index++)
        {
            if (CreateFrame(run, &Fixture, BadNames[Index]))
            {
                TEST_CHECK(run, !EditorFrame_IsWorldLoaded(Fixture.Frame) && (World_GetObjectCount(EditorFrame_GetWorld(Fixture.Frame)) == 0U));
            }
            DestroyFrame(run, &Fixture);
        }
        char Long[200];
        memset(Long, 'a', sizeof(Long) - 1U);
        Long[sizeof(Long) - 1U] = 0;
        if (CreateFrame(run, &Fixture, Long))
        {
            TEST_CHECK(run, !EditorFrame_IsWorldLoaded(Fixture.Frame));
        }
    }
    Deconstruct(run, &Fixture);
}

static void ATickSyncsWhatMovedAndTheCamera(TestRun* run)
{
    EditorFixture Fixture;
    uint64_t CrateId = 0U;
    if (ConstructServices(run, &Fixture, "editor_tick") && WriteWorld(run, &Fixture, "worlds/code.world.json", &CrateId)
        && CreateFrame(run, &Fixture, "code"))
    {
        WorldCollision* Collision = EditorFrame_GetCollision(Fixture.Frame);
        WorldRayHit Hit;
        Vector3 Across = { .x = -1.0f, .y = 0.0f, .z = 0.0f };
        TEST_CHECK(run, WorldCollision_Raycast(Collision, Vector3Double_Create(5.0, CRATE_Y, CRATE_Z), Across, 10.0f, NULL, &Hit)
            && (fabs(Hit.Point.X - 0.5) < 1e-3));
        // Moved between two ticks, the crate is where it went for whatever reads the collision world after the next one.
        WorldObject* Crate = NULL;
        if (TEST_CHECK(run, World_TryGetObject(EditorFrame_GetWorld(Fixture.Frame), CrateId, &Crate)))
        {
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Crate, Vector3Double_Create(CRATE_X, CRATE_Y, CRATE_Z + 10.0)));
        }
        Tick(run, &Fixture);
        TEST_CHECK(run, !WorldCollision_Raycast(Collision, Vector3Double_Create(5.0, CRATE_Y, CRATE_Z), Across, 10.0f, NULL, &Hit));
        TEST_CHECK(run, WorldCollision_Raycast(Collision, Vector3Double_Create(5.0, CRATE_Y, CRATE_Z + 10.0), Across, 10.0f, NULL, &Hit));
        // Flying moves the camera in the tick of the input.
        Vector2 Middle = ViewMiddle(&Fixture);
        MoveMouse(&Fixture, Middle.x, Middle.y);
        Tick(run, &Fixture);
        Press(&Fixture, GameKey_MouseRight);
        Press(&Fixture, GameKey_W);
        Vector3Double Before = EditorFrame_GetCamera(Fixture.Frame)->Position;
        Tick(run, &Fixture);
        TEST_CHECK(run, fabs(Moved(Before, EditorFrame_GetCamera(Fixture.Frame)->Position) - (6.0 * TICK_SECONDS)) < 1e-6);
        Release(&Fixture, GameKey_W);
        Release(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
    }
    Deconstruct(run, &Fixture);
}

static void KeysReachTheCameraOnlyWhileFlyingInTheEnteredView(TestRun* run)
{
    EditorFixture Fixture;
    if (ConstructServices(run, &Fixture, "editor_keys") && CreateFrame(run, &Fixture, NULL))
    {
        UIScreen* Screen = ScreenOf(&Fixture);
        uint64_t ViewId = EditorFrame_GetViewId(Fixture.Frame);
        Vector2 Middle = ViewMiddle(&Fixture);
        // Nothing focused: the keys move nothing.
        TEST_CHECK(run, HoldForward(run, &Fixture, 48U) == 0.0);
        // The right button held outside the view, the keys move nothing either.
        MoveMouse(&Fixture, 5.0f, WINDOW_HEIGHT * 0.5f);
        Tick(run, &Fixture);
        Press(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
        TEST_CHECK(run, HoldForward(run, &Fixture, 48U) == 0.0);
        Release(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
        // Flying: a press of the right button on the view enters it and acts at once.
        MoveMouse(&Fixture, Middle.x, Middle.y);
        Tick(run, &Fixture);
        Press(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(Screen) == ViewId);
        TEST_CHECK(run, Fixture.Cursor.Captures == 1U);
        Vector3Double FlightStart = EditorFrame_GetCamera(Fixture.Frame)->Position;
        double Flown = HoldForward(run, &Fixture, 48U);
        TEST_CHECK(run, fabs(Flown - (6.0 * 48.0 * TICK_SECONDS)) < 1e-6);
        TEST_CHECK(run, EditorFrame_GetCamera(Fixture.Frame)->Position.Z > FlightStart.Z); // Forward: the camera starts looking along +Z.
        // Fast.
        Press(&Fixture, GameKey_LeftShift);
        Tick(run, &Fixture);
        TEST_CHECK(run, fabs(HoldForward(run, &Fixture, 48U) - (18.0 * 48.0 * TICK_SECONDS)) < 1e-6);
        Release(&Fixture, GameKey_LeftShift);
        Tick(run, &Fixture);
        // The focus taken away by code while the button stays held: the view keeps its press but not the keys, and nothing flies.
        TEST_CHECK_SUCCESS(run, UIScreen_SetFocusedWidget(Screen, NULL));
        TEST_CHECK(run, HoldForward(run, &Fixture, 24U) == 0.0);
        Release(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
        TEST_CHECK(run, Fixture.Cursor.Releases == 1U);
        // Its press again enters it.
        Press(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
        Release(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
        // Let go of the right button, the view stays entered and the keys are its host's, but the camera does not fly.
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(Screen) == ViewId);
        TEST_CHECK(run, HoldForward(run, &Fixture, 48U) == 0.0);
        // A click elsewhere (the controls' window) takes the focus away: the right button pressed back on the view enters it again.
        Rectangle WorldBox = BoxOf(&Fixture, ViewId);
        MoveMouse(&Fixture, WorldBox.x - 30.0f, WorldBox.y + 50.0f);
        Tick(run, &Fixture);
        Press(&Fixture, GameKey_MouseLeft);
        Tick(run, &Fixture);
        Release(&Fixture, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetEnteredWidgetId(Screen) != ViewId);
        TEST_CHECK(run, HoldForward(run, &Fixture, 48U) == 0.0);
        // A press on the world's tab gives it the keys back and acts on nothing.
        uint64_t Tab = UIWorkspace_GetTabId(Screen, EditorFrame_GetWorkspaceId(Fixture.Frame), WorldWindowOf(&Fixture));
        Rectangle TabBox = BoxOf(&Fixture, Tab);
        Vector3Double Before = EditorFrame_GetCamera(Fixture.Frame)->Position;
        float Yaw = EditorFrame_GetCamera(Fixture.Frame)->Yaw;
        MoveMouse(&Fixture, TabBox.x + 6.0f, TabBox.y + (TabBox.height * 0.5f));
        Tick(run, &Fixture);
        Press(&Fixture, GameKey_MouseLeft);
        Tick(run, &Fixture);
        Release(&Fixture, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, (UIScreen_GetEnteredWidgetId(Screen) == ViewId) && (EditorFrame_GetCamera(Fixture.Frame)->Yaw == Yaw)
            && (Moved(Before, EditorFrame_GetCamera(Fixture.Frame)->Position) == 0.0));
    }
    Deconstruct(run, &Fixture);
}

static void ThePointerTurnsTheCameraWhileTheRightButtonIsHeld(TestRun* run)
{
    EditorFixture Fixture;
    if (ConstructServices(run, &Fixture, "editor_look") && CreateFrame(run, &Fixture, NULL))
    {
        Vector2 Middle = ViewMiddle(&Fixture);
        GameCamera* Eye = EditorFrame_GetCamera(Fixture.Frame);
        float Yaw = Eye->Yaw;
        float Pitch = Eye->Pitch;
        MoveMouse(&Fixture, Middle.x, Middle.y);
        Tick(run, &Fixture);
        Press(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
        // The cursor is captured: the motion is raw, the pointer stands still.
        Fixture.Sample.MouseDelta = (Vector2){ .x = 40.0f, .y = -20.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, (fabsf(Eye->Yaw - (Yaw - 0.1f)) < 1e-5f) && (fabsf(Eye->Pitch - (Pitch + 0.05f)) < 1e-5f));
        Release(&Fixture, GameKey_MouseRight);
        Tick(run, &Fixture);
        // Not held, the pointer turns nothing.
        Yaw = Eye->Yaw;
        MoveMouse(&Fixture, Middle.x + 50.0f, Middle.y);
        Tick(run, &Fixture);
        TEST_CHECK(run, Eye->Yaw == Yaw);
        // The left button's drag turns nothing either.
        Press(&Fixture, GameKey_MouseLeft);
        Tick(run, &Fixture);
        MoveMouse(&Fixture, Middle.x - 50.0f, Middle.y);
        Tick(run, &Fixture);
        Release(&Fixture, GameKey_MouseLeft);
        Tick(run, &Fixture);
        TEST_CHECK(run, Eye->Yaw == Yaw);
    }
    Deconstruct(run, &Fixture);
}

static void TheWheelOverTheViewChangesTheSpeed(TestRun* run)
{
    EditorFixture Fixture;
    if (ConstructServices(run, &Fixture, "editor_wheel") && CreateFrame(run, &Fixture, NULL))
    {
        Vector2 Middle = ViewMiddle(&Fixture);
        // Not focused.
        MoveMouse(&Fixture, Middle.x, Middle.y);
        Tick(run, &Fixture);
        Fixture.Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 3.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetFocusedWidgetId(ScreenOf(&Fixture)) == WIDGET_ID_INVALID);
        TEST_CHECK(run, fabsf(EditorFrame_GetFlySpeed(Fixture.Frame) - 12.0f) < 1e-3f);
        // Off the view it changes nothing.
        MoveMouse(&Fixture, 5.0f, WINDOW_HEIGHT * 0.5f);
        Tick(run, &Fixture);
        Fixture.Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = -3.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, fabsf(EditorFrame_GetFlySpeed(Fixture.Frame) - 12.0f) < 1e-3f);
    }
    Deconstruct(run, &Fixture);
}

static void TheWorldsWindowClosesAndComesBack(TestRun* run)
{
    EditorFixture Fixture;
    if (ConstructServices(run, &Fixture, "editor_close") && CreateFrame(run, &Fixture, NULL))
    {
        UIScreen* Screen = ScreenOf(&Fixture);
        uint64_t WorkspaceId = EditorFrame_GetWorkspaceId(Fixture.Frame);
        bool IsClosed = false;
        TEST_CHECK_SUCCESS(run, UIWindow_Close(Screen, WorldWindowOf(&Fixture), &IsClosed));
        Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, IsClosed && (EditorFrame_GetViewId(Fixture.Frame) == WIDGET_ID_INVALID));
        // Closed, the keys and the wheel move nothing.
        Fixture.Sample.WheelDelta = (Vector2){ .x = 0.0f, .y = 3.0f };
        Tick(run, &Fixture);
        TEST_CHECK(run, (fabsf(EditorFrame_GetFlySpeed(Fixture.Frame) - 6.0f) < 1e-3f) && (HoldForward(run, &Fixture, 10U) == 0.0));
        uint64_t Reopened = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWorkspace_OpenTypeByKey(Screen, WorkspaceId, u8"tbp:editor_world", &Reopened));
        Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, (EditorFrame_GetViewId(Fixture.Frame) != WIDGET_ID_INVALID) && (WorldWindowOf(&Fixture) == Reopened));
        // Only one: opening it again brings the same one.
        uint64_t Again = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWorkspace_OpenTypeByKey(Screen, WorkspaceId, u8"tbp:editor_world", &Again));
        TEST_CHECK(run, Again == Reopened);
    }
    Deconstruct(run, &Fixture);
}

static void ThePictureFollowsTheView(TestRun* run)
{
    EditorFixture Fixture;
    Memory_Zero(&Targets, sizeof(Targets));
    if (ConstructServices(run, &Fixture, "editor_picture") && CreateFrame(run, &Fixture, NULL))
    {
        Targets.Frame = Fixture.Frame;
        EditorTargetFunctions Fakes = { .Load = FakeLoad, .Unload = FakeUnload };
        EditorFrame_InternalSetTargetFunctions(Fixture.Frame, &Fakes);
        UIScreen* Screen = ScreenOf(&Fixture);
        uint64_t WorkspaceId = EditorFrame_GetWorkspaceId(Fixture.Frame);
        // Shown: a picture the view's size, made once, handed to the view, and the world drawn at every render.
        PrepareFrame(run, &Fixture);
        PrepareFrame(run, &Fixture);
        Int32Vector Docked = ViewSize(run, &Fixture);
        TEST_CHECK(run, (Targets.Loads == 1U) && (Targets.LastSize.X == Docked.X) && (Targets.LastSize.Y == Docked.Y) && IsLastMadeShown(&Fixture));
        TEST_CHECK(run, EditorFrame_GetWorldRenderCount(Fixture.Frame) == 2U);
        // A new size: the view lets go of the old picture before it is unloaded, and shows the new one.
        uint64_t WindowId = WorldWindowOf(&Fixture);
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(Screen, WorkspaceId, WindowId));
        Ticks(run, &Fixture, 2U);
        PrepareFrame(run, &Fixture);
        Int32Vector Floating = ViewSize(run, &Fixture);
        TEST_CHECK(run, (Floating.X != Docked.X) || (Floating.Y != Docked.Y));
        TEST_CHECK(run, (Targets.Loads == 2U) && (Targets.Unloads == 1U) && !Targets.WasShownWhenUnloaded && IsLastMadeShown(&Fixture));
        TEST_CHECK(run, (Targets.LastSize.X == Floating.X) && (Targets.LastSize.Y == Floating.Y));
        // Minimized: nothing made, nothing drawn, the picture kept for when it shows again.
        TEST_CHECK_SUCCESS(run, UIWindow_Minimize(Screen, WindowId));
        Ticks(run, &Fixture, 2U);
        uint64_t Drawn = EditorFrame_GetWorldRenderCount(Fixture.Frame);
        PrepareFrame(run, &Fixture);
        TEST_CHECK(run, (EditorFrame_GetWorldRenderCount(Fixture.Frame) == Drawn) && (Targets.Loads == 2U) && (Targets.Unloads == 1U));
        TEST_CHECK_SUCCESS(run, UIWindow_Restore(Screen, WindowId));
        Ticks(run, &Fixture, 2U);
        PrepareFrame(run, &Fixture);
        TEST_CHECK(run, (EditorFrame_GetWorldRenderCount(Fixture.Frame) == (Drawn + 1U)) && (Targets.Loads == 2U) && IsLastMadeShown(&Fixture));
        // A picture that cannot be made: the view shows none, the world is not drawn, and that size is not tried again.
        Targets.IsRefusing = true;
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(Screen, WindowId, (Rectangle){ .x = 200.0f, .y = 150.0f, .width = 500.0f, .height = 350.0f }));
        Ticks(run, &Fixture, 2U);
        Drawn = EditorFrame_GetWorldRenderCount(Fixture.Frame);
        PrepareFrame(run, &Fixture);
        PrepareFrame(run, &Fixture);
        PrepareFrame(run, &Fixture);
        TEST_CHECK(run, (Targets.Loads == 3U) && (Targets.Unloads == 2U) && !Targets.WasShownWhenUnloaded && (ShownTexture(Fixture.Frame) == 0U));
        TEST_CHECK(run, EditorFrame_GetWorldRenderCount(Fixture.Frame) == Drawn);
        // A view opened again tries once more, where its window was (the same size).
        bool IsClosed = false;
        TEST_CHECK_SUCCESS(run, UIWindow_Close(Screen, WindowId, &IsClosed));
        Ticks(run, &Fixture, 2U);
        PrepareFrame(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIWorkspace_OpenTypeByKey(Screen, WorkspaceId, u8"tbp:editor_world", &WindowId));
        Ticks(run, &Fixture, 2U);
        PrepareFrame(run, &Fixture);
        Int32Vector Again = ViewSize(run, &Fixture);
        TEST_CHECK(run, IsClosed && (Again.X == Targets.LastSize.X) && (Again.Y == Targets.LastSize.Y));
        TEST_CHECK(run, (Targets.Loads == 4U) && (Targets.Unloads == 2U) && (ShownTexture(Fixture.Frame) == 0U));
        // Another size tries again, and the picture is made.
        Targets.IsRefusing = false;
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(Screen, WindowId, (Rectangle){ .x = 200.0f, .y = 150.0f, .width = 520.0f, .height = 350.0f }));
        Ticks(run, &Fixture, 2U);
        PrepareFrame(run, &Fixture);
        TEST_CHECK(run, (Targets.Loads == 5U) && IsLastMadeShown(&Fixture) && (EditorFrame_GetWorldRenderCount(Fixture.Frame) == (Drawn + 1U)));
        // Once one is made, the size that failed is tried again.
        TEST_CHECK_SUCCESS(run, UIWindow_SetBox(Screen, WindowId, (Rectangle){ .x = 200.0f, .y = 150.0f, .width = 500.0f, .height = 350.0f }));
        Ticks(run, &Fixture, 2U);
        PrepareFrame(run, &Fixture);
        TEST_CHECK(run, (Targets.Loads == 6U) && (Targets.Unloads == 3U) && IsLastMadeShown(&Fixture));
        // Closed, its picture goes; opened again, a new one goes to the new view.
        IsClosed = false;
        TEST_CHECK_SUCCESS(run, UIWindow_Close(Screen, WindowId, &IsClosed));
        Ticks(run, &Fixture, 2U);
        Drawn = EditorFrame_GetWorldRenderCount(Fixture.Frame);
        PrepareFrame(run, &Fixture);
        TEST_CHECK(run, IsClosed && (Targets.Unloads == 4U) && (EditorFrame_GetWorldRenderCount(Fixture.Frame) == Drawn));
        uint64_t Reopened = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIWorkspace_OpenTypeByKey(Screen, WorkspaceId, u8"tbp:editor_world", &Reopened));
        Ticks(run, &Fixture, 2U);
        PrepareFrame(run, &Fixture);
        TEST_CHECK(run, (Targets.Loads == 7U) && IsLastMadeShown(&Fixture));
        // Torn down, its last picture goes too, after its view (with the UI): every picture made is unloaded once.
        Targets.Frame = NULL;
        DestroyFrame(run, &Fixture);
        TEST_CHECK(run, (Targets.Unloads == 5U) && !Targets.WasShownWhenUnloaded);
    }
    Deconstruct(run, &Fixture);
}

static void ItsLayoutIsKeptAcrossARestart(TestRun* run)
{
    EditorFixture Fixture;
    if (ConstructServices(run, &Fixture, "editor_layouts") && CreateFrame(run, &Fixture, NULL))
    {
        UIScreen* Screen = ScreenOf(&Fixture);
        uint64_t WorkspaceId = EditorFrame_GetWorkspaceId(Fixture.Frame);
        TEST_CHECK_SUCCESS(run, UIWorkspace_Undock(Screen, WorkspaceId, WorldWindowOf(&Fixture)));
        Ticks(run, &Fixture, 2U);
        DestroyFrame(run, &Fixture);
        GenericBuffer Path;
        GenericBuffer_AllocateVariable(&Path, PATH_CAPACITY, sizeof(unsigned char));
        FileSystemEntryInfo Info;
        TEST_CHECK_SUCCESS(run, Path_Append(Fixture.Work._data, u8"editor_layouts.json", &Path));
        bool IsWritten = TEST_CHECK_SUCCESS(run, FileSystem_GetEntryInfo(Path._data, &Info));
        if (IsWritten)
        {
            FileSystemEntryInfo_Deconstruct(&Info);
        }
        Memory_Free(Path._data);
        if (IsWritten && CreateFrame(run, &Fixture, NULL))
        {
            UIWorkspacePlace Place = UIWorkspacePlace_Docked;
            TEST_CHECK(run, UIWorkspace_GetPlace(ScreenOf(&Fixture), EditorFrame_GetWorkspaceId(Fixture.Frame), WorldWindowOf(&Fixture), &Place)
                && (Place == UIWorkspacePlace_Floating));
        }
    }
    Deconstruct(run, &Fixture);
}

static Error Stub_Step(void* self)
{
    (void)self;
    return Error_CreateSuccess();
}

static bool Stub_IsDone(void* self)
{
    (void)self;
    return true;
}

static Error Stub_Time(void* self, ProgramTime time)
{
    (void)self;
    (void)time;
    return Error_CreateSuccess();
}

static Error Stub_Render(void* self, const FrameRenderContext* context, RenderTexture2D target)
{
    (void)self;
    (void)context;
    (void)target;
    return Error_CreateSuccess();
}

static void Stub_Destroy(void* self)
{
    (void)self;
}

static const GameFrameVTable StubVTable = { .Start = Stub_Time, .End = Stub_Time, .Update = Stub_Time, .BeginLoad = Stub_Step,
    .LoadStep = Stub_Step, .IsLoaded = Stub_IsDone, .BeginUnload = Stub_Step, .UnloadStep = Stub_Step, .IsUnloaded = Stub_IsDone,
    .Render = Stub_Render, .Destroy = Stub_Destroy };

static void ItNeedsItsServices(TestRun* run)
{
    GameFrame Other;
    if (TEST_CHECK_SUCCESS(run, GameFrame_Construct(&Other, &StubVTable, u8"other")))
    {
        TEST_CHECK(run, !EditorFrame_IsEditorFrame(&Other) && (EditorFrame_GetWorld(&Other) == NULL) && (EditorFrame_GetCamera(&Other) == NULL)
            && (EditorFrame_GetScreen(&Other) == NULL) && (EditorFrame_GetCollision(&Other) == NULL)
            && (EditorFrame_GetViewId(&Other) == WIDGET_ID_INVALID) && (EditorFrame_GetWorkspaceId(&Other) == WIDGET_ID_INVALID)
            && !EditorFrame_IsWorldLoaded(&Other) && (EditorFrame_GetFlySpeed(&Other) == 0.0f) && (EditorFrame_GetWorldRenderCount(&Other) == 0U));
        GameFrame_Deconstruct(&Other);
    }
    EditorFixture Fixture;
    GameFrame* Frame = NULL;
    if (ConstructServices(run, &Fixture, "editor_services"))
    {
        TEST_CHECK_ERROR(run, EditorFrame_Create(NULL, NULL, &Frame), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, EditorFrame_Create(&Fixture.Shared, NULL, NULL), ErrorCode_IllegalArgument);
        Fixture.Shared.WindowTypes = NULL;
        TEST_CHECK_ERROR(run, EditorFrame_Create(&Fixture.Shared, NULL, &Frame), ErrorCode_IllegalArgument);
        TEST_CHECK(run, Frame == NULL);
        TEST_CHECK(run, !EditorFrame_IsEditorFrame(NULL) && (EditorFrame_GetWorld(NULL) == NULL) && (EditorFrame_GetViewId(NULL) == WIDGET_ID_INVALID));
    }
    Deconstruct(run, &Fixture);
}


// Public functions.
void EditorFrameTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "without_a_name_the_world_is_empty", WithoutANameTheWorldIsEmpty },
        { "a_name_opens_its_world_and_a_bad_one_nothing", ANameOpensItsWorldAndABadOneNothing },
        { "a_tick_syncs_what_moved_and_the_camera", ATickSyncsWhatMovedAndTheCamera },
        { "keys_reach_the_camera_only_while_flying_in_the_entered_view", KeysReachTheCameraOnlyWhileFlyingInTheEnteredView },
        { "the_pointer_turns_the_camera_while_the_right_button_is_held", ThePointerTurnsTheCameraWhileTheRightButtonIsHeld },
        { "the_wheel_over_the_view_changes_the_speed", TheWheelOverTheViewChangesTheSpeed },
        { "the_worlds_window_closes_and_comes_back", TheWorldsWindowClosesAndComesBack },
        { "the_picture_follows_the_view", ThePictureFollowsTheView },
        { "its_layout_is_kept_across_a_restart", ItsLayoutIsKeptAcrossARestart },
        { "it_needs_its_services", ItNeedsItsServices },
    };
    TestRun_RunSuite(run, "EditorFrame", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
