#include "TestSuites.h"
#include "TestAnimation.h"
#include "TestAssets.h"
#include "TestWorld.h"
#include "AnimationLibrary.h"
#include "World.h"
#include "WorldAnimation.h"
#include "WorldAnimatorComponent.h"
#include "WorldCharacterComponent.h"
#include "WorldCharacters.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "PlayerController.h"
#include "raylib/raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The tick's length (the game's 480 Hz). */
#define TICK_SECONDS (1.0f / 480.0f)
/** Tolerance for poses and placements. */
#define TEST_EPSILON 2e-3f
/** The figure's bones. */
#define FIGURE_BONE_COUNT 4
/** Room for the graph's text. */
#define GRAPH_TEXT_CAPACITY 8192


// Types.
/* Assets for a figure, a cup and a bench; a world on them with collision, characters and animation. */
typedef struct GraphSceneStruct
{
    TestAssetFixture Assets;
    World World;
    KeyRegistry Keys;
    bool HasWorld;
    WorldCollision* Collision;
    WorldCharacters* Characters;
    WorldAnimation* Animation;
    uint64_t Figure;
    uint64_t Cup;
    uint64_t Bench;
} GraphScene;


// Fields: the figure. In mesh space: the root at the floor, the spine 1 m up, the head 2 m up, a hand beside the chest.
static const TestSkeletonBone FigureBones[FIGURE_BONE_COUNT] =
{
    { .Name = "root", .Parent = -1, .Bind = { .translation = { 0.0f, 0.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "spine", .Parent = 0, .Bind = { .translation = { 0.0f, 1.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "head", .Parent = 1, .Bind = { .translation = { 0.0f, 2.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "hand", .Parent = 1, .Bind = { .translation = { 0.5f, 1.2f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
};

/* The clips: stand and walk tell themselves apart by the head (2 m and 2.5 m up), reach lifts the hand 1 m over a second,
 * sit lowers the spine half a metre, hold brings the hand half a metre forward. */
static const float OneSecond[] = { 0.0f, 1.0f };
static const float StandValues[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
static const float WalkValues[] = { 0.0f, 1.5f, 0.0f, 0.0f, 1.5f, 0.0f };
static const float ReachValues[] = { 0.5f, 0.2f, 0.0f, 0.5f, 1.2f, 0.0f };
static const float SitValues[] = { 0.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f };
static const float HoldValues[] = { 0.5f, 0.2f, 0.5f, 0.5f, 0.2f, 0.5f };

/* The figure's graph: a body layer (a stand-walk blend by speed, a hop on a jump, picking up, putting down, sitting,
 * cheering on a trigger) and an arm layer that holds while the hand's socket holds something; three actions. */
static const char* const FigureGraph =
    "{'name': 'figure_graph', 'parameters': {'mood': {'type': 'float', 'min': -1, 'max': 1}, 'cheer': {'type': 'trigger'}},"
    " 'layers': ["
    "  {'name': 'body', 'entry': 'move', 'states': {"
    "     'move': {'blend': {'parameter': 'speed', 'points': [{'clip': 'stand', 'at': 0}, {'clip': 'walk', 'at': 1}]},"
    "       'match_speed': 'speed'},"
    "     'hop': {'clip': 'reach'},"
    "     'pick_up': {'clip': 'reach', 'events': [{'name': 'grab', 'time': 0.5, 'attach': 'hand', 'grip': 'handle'}]},"
    "     'put_down': {'clip': 'reach', 'events': [{'name': 'release', 'time': 0.5, 'detach': 'hand'}]},"
    "     'sitting': {'clip': 'sit'},"
    "     'cheering': {'clip': 'reach'}},"
    "   'transitions': ["
    "     {'from': 'any', 'to': 'hop', 'when': [{'event': 'jumped'}], 'fade_seconds': 0},"
    "     {'from': ['hop', 'pick_up', 'put_down', 'cheering'], 'to': 'move', 'exit_time': 1, 'fade_seconds': 0},"
    "     {'from': 'sitting', 'to': 'move', 'when': [{'event': 'end_action'}], 'fade_seconds': 0},"
    "     {'from': 'move', 'to': 'cheering', 'when': [{'event': 'cheer'}], 'fade_seconds': 0}]},"
    "  {'name': 'arm', 'mask': ['hand'], 'entry': 'free', 'states': {'free': {}, 'holding': {'clip': 'hold'}},"
    "   'transitions': [{'from': 'free', 'to': 'holding', 'when': [{'socket': 'hand', 'is_holding': true}],"
    "     'fade_seconds': 0}, {'from': 'holding', 'to': 'free', 'when': [{'socket': 'hand', 'is_holding': false}],"
    "     'fade_seconds': 0}]}"
    " ],"
    " 'actions': {"
    "   'pick_up': {'layer': 'body', 'states': ['pick_up'], 'movement': 'no_moving', 'target': 'required'},"
    "   'put_down': {'layer': 'body', 'states': ['put_down'], 'movement': 'no_moving'},"
    "   'sit': {'layer': 'body', 'states': ['sitting'], 'movement': 'locked', 'anchor': {'socket': 'seat',"
    "     'max_distance': 3, 'approach_seconds': 0.25}},"
    "   'wave': {'layer': 'arm', 'states': ['holding'], 'target': 'none'}"
    " }"
    "}";


// Static functions: the scene.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) <= TEST_EPSILON;
}

static bool IsNearVector(Vector3 a, Vector3 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y) && IsNear(a.z, b.z);
}

static void Unquote(const char* text, char* buffer, size_t capacity)
{
    size_t Index = 0U;
    for (; (text[Index] != '\0') && (Index < (capacity - 1U)); Index++)
    {
        buffer[Index] = (text[Index] == '\'') ? '"' : text[Index];
    }
    buffer[Index] = '\0';
}

static TestGltfChannel Translation(int node, const float* values)
{
    return (TestGltfChannel){ .Node = node, .Path = "translation", .Interpolation = "LINEAR", .Times = OneSecond,
        .KeyCount = 2U, .Values = values, .ValueComponents = 3U };
}

static bool AddLibrary(TestRun* run, GraphScene* self)
{
    TestGltfNode Nodes[FIGURE_BONE_COUNT] =
    {
        { .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "spine", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
        { .Name = "head", .Parent = 1, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
        { .Name = "hand", .Parent = 1, .Rest = TestAnimation_Translation(0.5f, 0.2f, 0.0f) },
    };
    TestGltfChannel Channels[] =
    {
        Translation(2, StandValues), Translation(2, WalkValues), Translation(3, ReachValues), Translation(1, SitValues),
        Translation(3, HoldValues),
    };
    TestGltfClip Clips[] =
    {
        { .Name = "stand", .Channels = &Channels[0], .ChannelCount = 1U },
        { .Name = "walk", .Channels = &Channels[1], .ChannelCount = 1U },
        { .Name = "reach", .Channels = &Channels[2], .ChannelCount = 1U },
        { .Name = "sit", .Channels = &Channels[3], .ChannelCount = 1U },
        { .Name = "hold", .Channels = &Channels[4], .ChannelCount = 1U },
    };
    static const AnimationClipSettings Settings[] =
    {
        { .Source = (const unsigned char*)u8"stand", .Name = (const unsigned char*)u8"stand", .IsLooped = true,
            .Speed = 1.0f },
        { .Source = (const unsigned char*)u8"walk", .Name = (const unsigned char*)u8"walk", .IsLooped = true,
            .Speed = 1.0f, .HasDistance = true, .Distance = 1.0f },
        { .Source = (const unsigned char*)u8"sit", .Name = (const unsigned char*)u8"sit", .IsLooped = true, .Speed = 1.0f },
        { .Source = (const unsigned char*)u8"hold", .Name = (const unsigned char*)u8"hold", .IsLooped = true,
            .Speed = 1.0f },
    };
    GenericBuffer File;
    TestGltf_Write(Nodes, FIGURE_BONE_COUNT, Clips, sizeof(Clips) / sizeof(Clips[0]), &File);
    bool IsAdded = TestAssets_AddAnimationLibrary(run, &self->Assets, "moves", (const char*)File._data, Settings,
        sizeof(Settings) / sizeof(Settings[0]), NULL);
    Memory_Free(File._data);
    return IsAdded;
}

/* An object at @p position (the scene's) with a model component drawing @p worldModel. */
static uint64_t AddModelObject(TestRun* run, GraphScene* self, const char* worldModel, Vector3 position)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&self->World, (const unsigned char*)worldModel, &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(position)))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(&self->World, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component))
        || !TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component),
            (const unsigned char*)worldModel)))
    {
        return 0U;
    }
    return WorldObject_GetId(Object);
}

static bool AddFloor(TestRun* run, GraphScene* self)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&self->World, (const unsigned char*)u8"floor", &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At((Vector3){ 0.0f, -0.5f, 0.0f })))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(&self->World, WorldObject_GetId(Object),
            WorldComponentType_Collider, &Component)))
    {
        return false;
    }
    WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = (Vector3){ 30.0f, 0.5f, 30.0f };
    return TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Shape))
        && TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
}

static WorldCharacterComponent* GetCharacter(GraphScene* self)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&self->World, self->Figure, WorldComponentType_Character, &Component)
        ? WorldCharacterComponent_FromComponent(Component) : NULL;
}

static WorldAnimatorComponent* GetAnimator(GraphScene* self)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&self->World, self->Figure, WorldComponentType_Animator, &Component)
        ? WorldAnimatorComponent_FromComponent(Component) : NULL;
}

/* The figure (a character with an animator), a cup 2 m to its side and a bench 1 m ahead of it (its seat 1 m further,
 * facing back). */
static bool Scene_Construct(TestRun* run, GraphScene* self)
{
    Memory_Zero(self, sizeof(*self));
    char Graph[GRAPH_TEXT_CAPACITY];
    Unquote(FigureGraph, Graph, sizeof(Graph));
    BoundingBox Box = { .min = { -0.1f, -0.1f, -0.1f }, .max = { 0.1f, 0.1f, 0.1f } };
    bool IsBuilt = TestAssetFixture_Construct(run, &self->Assets)
        && TestAssets_AddSkinnedModel(run, &self->Assets, "figure_mesh", FigureBones, FIGURE_BONE_COUNT, 0.2f,
            MatrixIdentity(), NULL)
        && TestAssets_AddModel(run, &self->Assets, "box_mesh", Box, NULL)
        && AddLibrary(run, self)
        && TestAssets_AddAnimationGraph(run, &self->Assets, Graph)
        && TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"figure\", \"lods\": [{\"model\": \"figure_mesh\", "
            "\"max_distance\": 50}], \"animation_libraries\": [\"moves\"], \"animation_graph\": \"figure_graph\", "
            "\"sockets\": {\"hand\": {\"bone\": \"hand\"}, \"eyes\": {\"bone\": \"head\", \"position\": [0, 0.1, 0.2]}}, "
            "\"first_person\": {\"eyes\": \"eyes\", \"hidden_bones\": [\"head\"]}}")
        && TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"plain_figure\", \"lods\": [{\"model\": "
            "\"figure_mesh\", \"max_distance\": 50}], \"animation_libraries\": [\"moves\"]}")
        && TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"cup\", \"lods\": [{\"model\": \"box_mesh\", "
            "\"max_distance\": 50}], \"sockets\": {\"handle\": {\"position\": [0.1, 0, 0]}}}")
        && TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"bench\", \"lods\": [{\"model\": \"box_mesh\", "
            "\"max_distance\": 50}], \"sockets\": {\"seat\": {\"position\": [0, 0, 1], \"rotation\": [0, 180, 0]}}}");
    if (IsBuilt)
    {
        self->HasWorld = TestWorld_Construct(run, &self->World, &self->Keys, self->Assets.Manager, NULL);
        IsBuilt = self->HasWorld && AddFloor(run, self)
            && TEST_CHECK_SUCCESS(run, WorldCollision_Create(&self->World, self->Assets.Manager, NULL, NULL,
                &self->Collision))
            && TEST_CHECK_SUCCESS(run, WorldCharacters_Create(&self->World, self->Collision, NULL, &self->Characters))
            && TEST_CHECK_SUCCESS(run, WorldAnimation_Create(&self->World, self->Assets.Manager, NULL, &self->Animation));
    }
    if (IsBuilt)
    {
        self->Figure = AddModelObject(run, self, "figure", (Vector3){ 0.0f, 0.0f, 0.0f });
        self->Cup = AddModelObject(run, self, "cup", (Vector3){ 2.0f, 1.0f, 0.0f });
        self->Bench = AddModelObject(run, self, "bench", (Vector3){ 0.0f, 0.0f, 1.0f });
        WorldComponent* Component = NULL;
        IsBuilt = (self->Figure != 0U) && (self->Cup != 0U) && (self->Bench != 0U)
            && TEST_CHECK_SUCCESS(run, World_AddComponent(&self->World, self->Figure, WorldComponentType_Animator,
                &Component))
            && TEST_CHECK_SUCCESS(run, World_AddComponent(&self->World, self->Figure, WorldComponentType_Character,
                &Component));
    }
    if (IsBuilt)
    {
        PropertyValue CanJump = PropertyValue_FromBool(true);
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_OverrideSetting(GetCharacter(self), CharacterSetting_CanJump,
            &CanJump));
        WorldCollision_Sync(self->Collision);
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(self->Characters, self->Figure,
            TestWorld_At((Vector3){ 0.0f, 0.0f, 0.0f }), 0.0f));
    }
    return IsBuilt;
}

static void Scene_Deconstruct(TestRun* run, GraphScene* self)
{
    WorldAnimation_Destroy(self->Animation);
    WorldCharacters_Destroy(self->Characters);
    WorldCollision_Destroy(self->Collision);
    if (self->HasWorld)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

/* One tick in the game's order, then the frame's pose step and the world matrices. */
static void Tick(GraphScene* self)
{
    WorldCharacters_Update(self->Characters, TICK_SECONDS);
    WorldCharacters_ClearEvents(self->Characters);
    WorldCollision_Step(self->Collision, TICK_SECONDS);
    WorldAnimation_Update(self->Animation, TICK_SECONDS);
    WorldAnimation_Pose(self->Animation);
    World_UpdateTransforms(&self->World);
}

static void TickFor(GraphScene* self, float seconds)
{
    uint32_t Ticks = (uint32_t)lroundf(seconds / TICK_SECONDS);
    for (uint32_t Index = 0U; Index < Ticks; Index++)
    {
        Tick(self);
    }
}

static bool IsInState(GraphScene* self, const char* layer, const char* state)
{
    const unsigned char* Name = NULL;
    return WorldAnimation_TryGetState(self->Animation, self->Figure, (const unsigned char*)layer, &Name)
        && (strcmp((const char*)Name, state) == 0);
}

static float GetParameter(GraphScene* self, const char* name)
{
    float Value = NAN;
    WorldAnimation_TryGetParameter(self->Animation, self->Figure, (const unsigned char*)name, &Value);
    return Value;
}

/* A bone's position in mesh space in the figure's pose. */
static Vector3 GetBone(GraphScene* self, uint32_t bone)
{
    const WorldAnimatorPose* Pose = WorldAnimatorComponent_GetPose(GetAnimator(self));
    if ((Pose == NULL) || (bone >= Pose->_boneCount))
    {
        return (Vector3){ NAN, NAN, NAN };
    }
    return (Vector3){ Pose->_boneMatrices[bone].m12, Pose->_boneMatrices[bone].m13, Pose->_boneMatrices[bone].m14 };
}

static Vector3 GetWorldPosition(GraphScene* self, uint64_t id)
{
    Vector3Double Position = { 0.0, 0.0, 0.0 };
    World_GetWorldPosition(&self->World, id, &Position);
    return TestWorld_ToScene(Position);
}

static size_t CountEvents(GraphScene* self, WorldAnimationEventKind kind, const char* name)
{
    size_t Count = 0U;
    size_t Found = 0U;
    const WorldAnimationEvent* Events = WorldAnimation_GetEvents(self->Animation, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if ((Events[Index].Kind == kind) && (Events[Index].ObjectId == self->Figure)
            && (strcmp((const char*)Events[Index].Name, name) == 0))
        {
            Found++;
        }
    }
    return Found;
}

static CharacterMovementLock GetLock(GraphScene* self)
{
    return WorldCharacterComponent_GetActionControl(GetCharacter(self))->Lock;
}


// Static functions: tests.
static void AGraphPlaysFromTheWorldModelAndFollowsItsCharacter(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.1f);
        TEST_CHECK(run, IsInState(&Scene, NULL, "move") && IsInState(&Scene, "arm", "free"));
        TEST_CHECK(run, IsNear(GetParameter(&Scene, "speed"), 0.0f) && IsNear(GetParameter(&Scene, "is_grounded"), 1.0f));
        TEST_CHECK(run, IsNear(GetBone(&Scene, 2U).y, 2.0f));
        WorldAnimationStatus Status = WorldAnimation_GetStatus(Scene.Animation);
        TEST_CHECK(run, (Status.GraphCount == 1U) && (Status.ActionCount == 0U));
        // Walking along +X (facing its movement): the speed, along its facing, none across; the blend walks.
        CharacterInput Walk = CharacterInput_CreateDefault();
        Walk.MoveDirection = (Vector3){ 1.0f, 0.0f, 0.0f };
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(GetCharacter(&Scene), &Walk));
        TickFor(&Scene, 1.5f);
        float Speed = GetParameter(&Scene, "speed");
        float WalkSpeed = WorldCharacterComponent_GetSettings(GetCharacter(&Scene)).WalkSpeed;
        TEST_CHECK(run, (fabsf(Speed - WalkSpeed) < 0.05f)
            && (fabsf(GetParameter(&Scene, "forward_speed") - Speed) < 0.05f)
            && (fabsf(GetParameter(&Scene, "side_speed")) < 0.05f));
        TEST_CHECK(run, IsNear(GetBone(&Scene, 2U).y, 2.5f));
        // A jump: the character's event fires the graph's.
        Walk.WantsJump = true;
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(GetCharacter(&Scene), &Walk));
        Tick(&Scene);
        TEST_CHECK(run, IsInState(&Scene, NULL, "hop") && IsNear(GetParameter(&Scene, "is_grounded"), 0.0f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void CodeSetsParametersFiresTriggersAndReadsStates(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        Tick(&Scene);
        TEST_CHECK_SUCCESS(run, WorldAnimation_SetParameter(Scene.Animation, Scene.Figure, (const unsigned char*)u8"mood",
            5.0f));
        TEST_CHECK(run, IsNear(GetParameter(&Scene, "mood"), 1.0f));
        TEST_CHECK_ERROR(run, WorldAnimation_SetParameter(Scene.Animation, Scene.Figure, (const unsigned char*)u8"nope",
            1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimation_SetParameter(Scene.Animation, Scene.Figure, (const unsigned char*)u8"mood",
            NAN), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldAnimation_SetParameter(Scene.Animation, Scene.Cup, (const unsigned char*)u8"mood",
            1.0f), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, WorldAnimation_FireTrigger(Scene.Animation, Scene.Figure, (const unsigned char*)u8"mood"),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, WorldAnimation_FireTrigger(Scene.Animation, Scene.Figure, (const unsigned char*)u8"cheer"));
        Tick(&Scene);
        TEST_CHECK(run, IsInState(&Scene, NULL, "cheering"));
        const unsigned char* State = NULL;
        TEST_CHECK(run, !WorldAnimation_TryGetState(Scene.Animation, Scene.Figure, (const unsigned char*)u8"legs", &State)
            && !WorldAnimation_TryGetState(Scene.Animation, Scene.Cup, NULL, &State));
        // An animator naming its own graph, then none: the graph goes with it.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationGraph(GetAnimator(&Scene), true, NULL));
        Tick(&Scene);
        TEST_CHECK(run, !WorldAnimation_TryGetState(Scene.Animation, Scene.Figure, NULL, &State)
            && (WorldAnimation_GetStatus(Scene.Animation).GraphCount == 0U));
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationGraph(GetAnimator(&Scene), true,
            (const unsigned char*)u8"figure_graph"));
        Tick(&Scene);
        TEST_CHECK(run, IsInState(&Scene, NULL, "move"));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ClipsFromCodePlayOverTheGraph(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.1f);
        TEST_CHECK(run, IsNear(GetBone(&Scene, 2U).y, 2.0f));
        // A clip at full weight covers the graph (the head on the lowered spine); stopped, the graph shows again.
        TEST_CHECK_SUCCESS(run, WorldAnimation_PlayClip(Scene.Animation, Scene.Figure, (const unsigned char*)u8"sit", 0.0f,
            false));
        Tick(&Scene);
        TEST_CHECK(run, IsNear(GetBone(&Scene, 1U).y, 0.5f) && IsNear(GetBone(&Scene, 2U).y, 1.5f));
        TEST_CHECK_SUCCESS(run, WorldAnimation_StopClips(Scene.Animation, Scene.Figure, 0.0f));
        Tick(&Scene);
        TEST_CHECK(run, IsNear(GetBone(&Scene, 1U).y, 1.0f) && IsNear(GetBone(&Scene, 2U).y, 2.0f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void SocketsCarryChildrenWithThePose(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.1f);
        Vector3 Feet = GetWorldPosition(&Scene, Scene.Figure);
        // The cup on the hand: it sits on the socket (the hand), and the arm layer holds (its socket holds something).
        TEST_CHECK_SUCCESS(run, World_SetParentSocket(&Scene.World, Scene.Cup, Scene.Figure, (const unsigned char*)u8"hand",
            false));
        WorldObject* Cup = NULL;
        TEST_CHECK(run, World_TryGetObject(&Scene.World, Scene.Cup, &Cup));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Cup, (Vector3Double){ 0.0, 0.0, 0.0 }));
        Tick(&Scene);
        TEST_CHECK(run, IsInState(&Scene, "arm", "holding"));
        // Holding brings the hand half a metre forward: the cup follows it.
        Vector3 Hand = GetBone(&Scene, 3U);
        TEST_CHECK(run, IsNearVector(Hand, (Vector3){ 0.5f, 1.2f, 0.5f }));
        TEST_CHECK(run, IsNearVector(GetWorldPosition(&Scene, Scene.Cup), Vector3Add(Feet, Hand)));
        Matrix4Double Socket;
        TEST_CHECK(run, World_TryGetSocketWorldMatrix(&Scene.World, Scene.Figure, (const unsigned char*)u8"hand", &Socket)
            && IsNearVector(TestWorld_ToScene(Matrix4Double_GetTranslation(&Socket)), Vector3Add(Feet, Hand)));
        TEST_CHECK(run, !World_TryGetSocketWorldMatrix(&Scene.World, Scene.Figure, (const unsigned char*)u8"tail",
            &Socket) && !World_TryGetSocketWorldMatrix(&Scene.World, Scene.Cup, (const unsigned char*)u8"hand", &Socket));
        // The figure walks: the cup moves with it.
        CharacterInput Walk = CharacterInput_CreateDefault();
        Walk.MoveDirection = (Vector3){ 0.0f, 0.0f, 1.0f };
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(GetCharacter(&Scene), &Walk));
        TickFor(&Scene, 0.5f);
        Feet = GetWorldPosition(&Scene, Scene.Figure);
        TEST_CHECK(run, (Feet.z > 0.3f) && IsNearVector(GetWorldPosition(&Scene, Scene.Cup), Vector3Add(Feet,
            GetBone(&Scene, 3U))));
        // Off the socket (World_SetParent clears it): the arm lets go.
        TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Scene.Cup, 0U, true));
        TEST_CHECK(run, WorldObject_GetParentSocket(Cup) == NULL);
        Tick(&Scene);
        TEST_CHECK(run, IsInState(&Scene, "arm", "free"));
        // The socket's rules.
        TEST_CHECK_ERROR(run, World_SetParentSocket(&Scene.World, Scene.Cup, Scene.Figure, (const unsigned char*)u8"Hand",
            false), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, World_SetParentSocket(&Scene.World, Scene.Cup, 0U, (const unsigned char*)u8"hand", false),
            ErrorCode_InvalidOperation);
    }
    Scene_Deconstruct(run, &Scene);
}

static void AnActionAttachesAndDetachesItsTarget(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.1f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"pick_up",
            Scene.Cup));
        TEST_CHECK(run, IsInState(&Scene, NULL, "pick_up") && (GetLock(&Scene) == CharacterMovementLock_NoMoving)
            && (CountEvents(&Scene, WorldAnimationEventKind_ActionStarted, "pick_up") == 1U));
        const unsigned char* Action = NULL;
        uint64_t Target = 0U;
        TEST_CHECK(run, WorldAnimation_TryGetAction(Scene.Animation, Scene.Figure, &Action, &Target)
            && (strcmp((const char*)Action, "pick_up") == 0) && (Target == Scene.Cup));
        TickFor(&Scene, 0.4f);
        WorldObject* Cup = NULL;
        World_TryGetObject(&Scene.World, Scene.Cup, &Cup);
        TEST_CHECK(run, WorldObject_GetParentId(Cup) == 0U);
        // "grab" at 0.5 s: the cup hangs on the hand by its handle (its origin 10 cm the other way).
        TickFor(&Scene, 0.15f);
        TEST_CHECK(run, (WorldObject_GetParentId(Cup) == Scene.Figure) && (WorldObject_GetParentSocket(Cup) != NULL)
            && (strcmp((const char*)WorldObject_GetParentSocket(Cup), "hand") == 0)
            && IsNear((float)WorldObject_GetPosition(Cup).X, -0.1f)
            && (WorldObject_GetMobility(Cup) == WorldMobility_Movable));
        TEST_CHECK(run, CountEvents(&Scene, WorldAnimationEventKind_Graph, "grab") == 1U);
        // The clip ends: back to move, the action over, the character free.
        TickFor(&Scene, 0.6f);
        TEST_CHECK(run, IsInState(&Scene, NULL, "move") && (CountEvents(&Scene, WorldAnimationEventKind_ActionEnded,
            "pick_up") == 1U) && (GetLock(&Scene) == CharacterMovementLock_Free)
            && !WorldAnimation_TryGetAction(Scene.Animation, Scene.Figure, NULL, NULL));
        // Putting it down: at "release" it goes back to the world where it is.
        WorldAnimation_ClearEvents(Scene.Animation);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure,
            (const unsigned char*)u8"put_down", 0U));
        TickFor(&Scene, 0.45f);
        Vector3 Held = GetWorldPosition(&Scene, Scene.Cup);
        TickFor(&Scene, 0.1f);
        TEST_CHECK(run, (WorldObject_GetParentId(Cup) == 0U) && (WorldObject_GetParentSocket(Cup) == NULL)
            && (CountEvents(&Scene, WorldAnimationEventKind_Graph, "release") == 1U));
        Vector3 Released = GetWorldPosition(&Scene, Scene.Cup);
        // It stays where the hand let it go (between the two frames' hand places, the reach lifting it).
        TEST_CHECK(run, IsNear(Released.x, Held.x) && (fabsf(Released.y - Held.y) < 0.12f) && IsNear(Released.z, Held.z));
    }
    Scene_Deconstruct(run, &Scene);
}

/* A scaled target is held by its grip all the same: the grip's offset scales with it, so the handle lands on the hand
 * (the scale kept, the rotation unchanged). */
static void AScaledTargetIsHeldByItsGrip(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        WorldObject* Cup = NULL;
        World_TryGetObject(&Scene.World, Scene.Cup, &Cup);
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Cup, (Vector3){ 2.0f, 3.0f, 0.5f }));
        TickFor(&Scene, 0.1f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"pick_up",
            Scene.Cup));
        TickFor(&Scene, 0.55f);
        Matrix4Double Hand;
        Matrix4Double CupWorld;
        if (TEST_CHECK(run, (WorldObject_GetParentId(Cup) == Scene.Figure)
            && World_TryGetSocketWorldMatrix(&Scene.World, Scene.Figure, (const unsigned char*)u8"hand", &Hand)
            && TEST_CHECK_SUCCESS(run, World_GetWorldMatrix(&Scene.World, Scene.Cup, &CupWorld))))
        {
            // The handle: 0.1 m along the cup's own X, scaled to 0.2 m.
            Vector3Double Handle = Matrix4Double_TransformPoint(&CupWorld, Vector3Double_Create(0.1, 0.0, 0.0));
            TEST_CHECK(run, IsNearVector(TestWorld_ToScene(Handle), TestWorld_ToScene(Matrix4Double_GetTranslation(&Hand))));
            TEST_CHECK(run, Vector3Equals(WorldObject_GetScale(Cup), (Vector3){ 2.0f, 3.0f, 0.5f })
                && IsNear((float)WorldObject_GetPosition(Cup).X, -0.2f));
        }
    }
    Scene_Deconstruct(run, &Scene);
}

static void AnAnchoredActionSlidesTheCharacterOntoItsSeat(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.1f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"sit",
            Scene.Bench));
        TEST_CHECK(run, (WorldCharacterComponent_GetState(GetCharacter(&Scene))->Approach == CharacterApproach_Approaching)
            && IsInState(&Scene, NULL, "move") && (GetLock(&Scene) == CharacterMovementLock_Locked)
            && (CountEvents(&Scene, WorldAnimationEventKind_ActionStarted, "sit") == 0U));
        TEST_CHECK(run, IsNear(GetParameter(&Scene, "is_in_action"), 0.0f));
        TickFor(&Scene, 0.5f);
        // There: on the seat (the bench's socket 1 m in front of it), facing back toward -Z, sitting.
        Vector3 Feet = GetWorldPosition(&Scene, Scene.Figure);
        const WorldCharacterState* State = WorldCharacterComponent_GetState(GetCharacter(&Scene));
        TEST_CHECK(run, (State->Approach == CharacterApproach_Arrived) && IsNear(Feet.x, 0.0f) && IsNear(Feet.z, 2.0f)
            && IsNear(fabsf(State->FacingYaw), PI));
        TEST_CHECK(run, IsInState(&Scene, NULL, "sitting") && (CountEvents(&Scene, WorldAnimationEventKind_ActionStarted,
            "sit") == 1U) && IsNear(GetParameter(&Scene, "is_in_action"), 1.0f));
        TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).ActionCount == 1U);
        // Asked to end: the graph's end_action takes it out of sitting, and the action ends.
        TEST_CHECK(run, WorldAnimation_EndAction(Scene.Animation, Scene.Figure));
        TickFor(&Scene, 0.05f);
        TEST_CHECK(run, IsInState(&Scene, NULL, "move") && (CountEvents(&Scene, WorldAnimationEventKind_ActionEnded,
            "sit") == 1U) && (GetLock(&Scene) == CharacterMovementLock_Free));
        TEST_CHECK(run, !WorldAnimation_EndAction(Scene.Animation, Scene.Figure));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ActionsRefuseWhatTheyCannotDo(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.05f);
        WorldAnimation* Animation = Scene.Animation;
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Animation, Scene.Figure, (const unsigned char*)u8"fly", 0U),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Animation, Scene.Figure, (const unsigned char*)u8"pick_up", 0U),
            ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Animation, Scene.Figure, (const unsigned char*)u8"wave",
            Scene.Cup), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Animation, Scene.Figure, (const unsigned char*)u8"pick_up",
            987654U), ErrorCode_ArgumentOutOfRange);
        // The cup has no seat; the bench moved 10 m away is out of reach.
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Animation, Scene.Figure, (const unsigned char*)u8"sit", Scene.Cup),
            ErrorCode_ArgumentOutOfRange);
        WorldObject* Bench = NULL;
        World_TryGetObject(&Scene.World, Scene.Bench, &Bench);
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Bench, TestWorld_At((Vector3){ 10.0f, 0.0f, 0.0f })));
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Animation, Scene.Figure, (const unsigned char*)u8"sit",
            Scene.Bench), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Animation, Scene.Cup, (const unsigned char*)u8"wave", 0U),
            ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Animation, Scene.Figure, NULL, 0U), ErrorCode_IllegalArgument);
        // Refused starts changed nothing.
        TEST_CHECK(run, !WorldAnimation_TryGetAction(Animation, Scene.Figure, NULL, NULL)
            && (GetLock(&Scene) == CharacterMovementLock_Free));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ARebindOrTheSystemsEndFreesTheCharacter(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.05f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"sit",
            Scene.Bench));
        TEST_CHECK(run, GetLock(&Scene) == CharacterMovementLock_Locked);
        // Another world model: the animator rebinds, and the action ends.
        WorldComponent* Component = NULL;
        World_TryGetComponent(&Scene.World, Scene.Figure, WorldComponentType_Model, &Component);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component),
            (const unsigned char*)u8"plain_figure"));
        Tick(&Scene);
        TEST_CHECK(run, (CountEvents(&Scene, WorldAnimationEventKind_ActionEnded, "sit") == 1U)
            && (GetLock(&Scene) == CharacterMovementLock_Free)
            && (WorldCharacterComponent_GetState(GetCharacter(&Scene))->Approach == CharacterApproach_None));
        // Back, sitting again; then the system goes: the character is freed.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component),
            (const unsigned char*)u8"figure"));
        Tick(&Scene);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"sit",
            Scene.Bench));
        TEST_CHECK(run, GetLock(&Scene) == CharacterMovementLock_Locked);
        WorldAnimation_Destroy(Scene.Animation);
        Scene.Animation = NULL;
        TEST_CHECK(run, GetLock(&Scene) == CharacterMovementLock_Free);
    }
    Scene_Deconstruct(run, &Scene);
}

/* T30, G34: the player's camera rides its body's eyes socket as posed (the pose and the object's turn move it); a body
 * whose world model has no first_person block leaves it at the character's eye height. */
static void ThePlayersCameraRidesTheEyesSocket(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.1f);
        PlayerController Controller;
        PlayerController_Construct(&Controller);
        TEST_CHECK_SUCCESS(run, PlayerController_SetCharacter(&Controller, &Scene.World, Scene.Figure, 0.0f, 0.0f));
        // Standing at the origin facing +Z (its feet resting just above the floor): the head 2 m up, the eyes 0.1 m above
        // it and 0.2 m ahead.
        Vector3 Feet = GetWorldPosition(&Scene, Scene.Figure);
        GameCamera View = GameCamera_Create(Vector3Double_Zero(), 60.0f);
        PlayerController_ApplyCamera(&Controller, &Scene.World, &View);
        TEST_CHECK(run, IsNearVector(TestWorld_ToScene(View.Position), Vector3Add(Feet, (Vector3){ 0.0f, 2.1f, 0.2f })));
        // A clip lifting the head half a metre, and a quarter turn: the eyes follow both (ahead is +X now).
        TEST_CHECK_SUCCESS(run, WorldAnimation_PlayClip(Scene.Animation, Scene.Figure, (const unsigned char*)u8"walk",
            0.0f, false));
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Scene.Figure,
            TestWorld_At((Vector3){ 0.0f, 0.0f, 0.0f }), 0.5f * PI));
        Tick(&Scene);
        Feet = GetWorldPosition(&Scene, Scene.Figure);
        Vector3Double Eyes = Vector3Double_Zero();
        TEST_CHECK(run, PlayerController_TryGetEyes(&Scene.World, Scene.Figure, &Eyes)
            && IsNearVector(TestWorld_ToScene(Eyes), Vector3Add(Feet, (Vector3){ 0.2f, 2.6f, 0.0f })));
        PlayerController_ApplyCamera(&Controller, &Scene.World, &View);
        TEST_CHECK(run, Vector3Double_EqualsClose(View.Position, Eyes, 1e-9));
        // No first_person block: the character's eye height above its feet.
        WorldComponent* Component = NULL;
        World_TryGetComponent(&Scene.World, Scene.Figure, WorldComponentType_Model, &Component);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component),
            (const unsigned char*)u8"plain_figure"));
        Tick(&Scene);
        PlayerController_ApplyCamera(&Controller, &Scene.World, &View);
        TEST_CHECK(run, Vector3Double_EqualsClose(View.Position,
            WorldCharacterComponent_GetEyePosition(GetCharacter(&Scene)), 1e-9));
        TEST_CHECK(run, !PlayerController_TryGetEyes(&Scene.World, Scene.Cup, &Eyes));
        PlayerController_Deconstruct(&Controller);
    }
    Scene_Deconstruct(run, &Scene);
}


/* A static box of @p halfExtents at @p center (the scene's), or a pushable movable one. */
static uint64_t AddBoxCollider(TestRun* run, GraphScene* self, const char* name, Vector3 center, Vector3 halfExtents)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&self->World, (const unsigned char*)name, &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(center)))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(&self->World, WorldObject_GetId(Object),
            WorldComponentType_Collider, &Component)))
    {
        return 0U;
    }
    WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = halfExtents;
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Shape));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
    return WorldObject_GetId(Object);
}

/* T30: the built-in parameters read the character in its body's frame: strafing to its right is side speed, a look to
 * its left is look_yaw relative to its facing, and a fall is vertical speed, not speed across the ground. */
static void BuiltInsReadTheCharacterInItsBodysFrame(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.1f);
        CharacterInput Strafe = CharacterInput_CreateDefault();
        Strafe.MoveDirection = (Vector3){ -1.0f, 0.0f, 0.0f };
        Strafe.LookYaw = 0.0f;
        Strafe.Facing = CharacterFacing_Look;
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(GetCharacter(&Scene), &Strafe));
        TickFor(&Scene, 1.0f);
        float Speed = GetParameter(&Scene, "speed");
        TEST_CHECK(run, (Speed > 0.5f) && (fabsf(GetParameter(&Scene, "side_speed") - Speed) < 0.05f)
            && (fabsf(GetParameter(&Scene, "forward_speed")) < 0.05f));
        // Its body turned to 1 rad, looking at 1.5 rad: look_yaw is the half radian between them.
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Scene.Figure,
            TestWorld_At((Vector3){ 0.0f, 0.0f, 0.0f }), 1.0f));
        CharacterInput Look = CharacterInput_CreateDefault();
        Look.LookYaw = 1.5f;
        Look.Facing = CharacterFacing_Keep;
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(GetCharacter(&Scene), &Look));
        TickFor(&Scene, 0.5f);
        TEST_CHECK(run, fabsf(GetParameter(&Scene, "look_yaw") - (0.5f * RAD2DEG)) < 0.01f);
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Scene.Figure,
            TestWorld_At((Vector3){ 0.0f, 3.0f, 0.0f }), 0.0f));
        TickFor(&Scene, 0.3f);
        float Vertical = GetParameter(&Scene, "vertical_speed");
        TEST_CHECK(run, (GetParameter(&Scene, "speed") < 0.01f) && (Vertical < -1.0f)
            && (fabsf(GetParameter(&Scene, "speed_3d") + Vertical) < 0.01f));
    }
    Scene_Deconstruct(run, &Scene);
}

/* T30: an approach a wall blocks fails the action: the failure is queued and the character is free again. */
static void ABlockedApproachFailsTheAction(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene) && (AddBoxCollider(run, &Scene, "wall", (Vector3){ 0.0f, 1.0f, 1.0f },
        (Vector3){ 2.0f, 1.0f, 0.05f }) != 0U))
    {
        WorldCollision_Sync(Scene.Collision);
        TickFor(&Scene, 0.05f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"sit",
            Scene.Bench));
        TickFor(&Scene, 1.0f);
        TEST_CHECK(run, (CountEvents(&Scene, WorldAnimationEventKind_ActionFailed, "sit") == 1U)
            && (CountEvents(&Scene, WorldAnimationEventKind_ActionStarted, "sit") == 0U)
            && (GetLock(&Scene) == CharacterMovementLock_Free)
            && !WorldAnimation_TryGetAction(Scene.Animation, Scene.Figure, NULL, NULL));
    }
    Scene_Deconstruct(run, &Scene);
}

/* T30: standing on the anchor already, the character still turns to its way before it has arrived. */
static void AnApproachArrivesOnlyFacingTheAnchorsWay(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        // The seat: 1 m past the bench (at z = 1), facing back (-Z).
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Scene.Figure,
            TestWorld_At((Vector3){ 0.0f, 0.0f, 2.0f }), 0.0f));
        TickFor(&Scene, 0.05f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"sit",
            Scene.Bench));
        Tick(&Scene);
        TEST_CHECK(run, (WorldCharacterComponent_GetState(GetCharacter(&Scene))->Approach == CharacterApproach_Approaching)
            && !IsInState(&Scene, NULL, "sitting"));
        TickFor(&Scene, 0.4f);
        const WorldCharacterState* State = WorldCharacterComponent_GetState(GetCharacter(&Scene));
        TEST_CHECK(run, (State->Approach == CharacterApproach_Arrived) && (fabsf(fabsf(State->FacingYaw) - PI) < 1e-3f)
            && IsInState(&Scene, NULL, "sitting"));
    }
    Scene_Deconstruct(run, &Scene);
}

/* T30: a pushable held on a socket is its holder's in the collision step: it is not pushed out of the hand when it
 * touches a wall (what gives way is the holder, which carries it). */
static void AHeldPushableStaysInTheHand(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        // The cup gets a pushable collider and hangs on the hand, on the socket itself (at (0.5, 1.2, 0)); a static box
        // overlaps it there.
        WorldComponent* Component = NULL;
        WorldObject* Cup = NULL;
        World_TryGetObject(&Scene.World, Scene.Cup, &Cup);
        bool IsBuilt = TEST_CHECK_SUCCESS(run, World_AddComponent(&Scene.World, Scene.Cup, WorldComponentType_Collider,
            &Component));
        if (IsBuilt)
        {
            WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
            CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
            Shape.HalfExtents = (Vector3){ 0.1f, 0.1f, 0.1f };
            IsBuilt = TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Shape))
                && TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true))
                && TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetPushable(Collider, true))
                && TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Cup, WorldMobility_Movable))
                && TEST_CHECK_SUCCESS(run, World_SetParentSocket(&Scene.World, Scene.Cup, Scene.Figure,
                    (const unsigned char*)u8"hand", false))
                && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Cup, Vector3Double_Zero()))
                && (AddBoxCollider(run, &Scene, "shelf", (Vector3){ 0.62f, 1.2f, 0.0f }, (Vector3){ 0.1f, 0.1f, 0.1f })
                    != 0U);
        }
        if (IsBuilt)
        {
            TickFor(&Scene, 0.2f);
            Vector3Double Local = WorldObject_GetPosition(Cup);
            TEST_CHECK(run, (WorldObject_GetParentId(Cup) == Scene.Figure) && (fabs(Local.X) < 1e-9)
                && (fabs(Local.Y) < 1e-9) && (fabs(Local.Z) < 1e-9));
        }
    }
    Scene_Deconstruct(run, &Scene);
}

/* T30 review: an action replaces one playing on its own layer, but one playing on another layer must end first:
 * replacing it would leave that layer in its states (sitting) with nothing to take it out, the character freed. */
static void AnActionOnAnotherLayerWaitsForThePlayingOne(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.05f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"sit",
            Scene.Bench));
        TickFor(&Scene, 0.5f);
        TEST_CHECK(run, IsInState(&Scene, NULL, "sitting"));
        // The arm's wave is refused while the body sits; the sit goes on.
        TEST_CHECK_ERROR(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"wave", 0U),
            ErrorCode_InvalidOperation);
        const unsigned char* Action = NULL;
        TEST_CHECK(run, WorldAnimation_TryGetAction(Scene.Animation, Scene.Figure, &Action, NULL)
            && (strcmp((const char*)Action, "sit") == 0) && (GetLock(&Scene) == CharacterMovementLock_Locked)
            && IsInState(&Scene, NULL, "sitting"));
        // Picking up, on the body's own layer, replaces it: the sit ends, the body reaches.
        WorldAnimation_ClearEvents(Scene.Animation);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure,
            (const unsigned char*)u8"pick_up", Scene.Cup));
        TEST_CHECK(run, (CountEvents(&Scene, WorldAnimationEventKind_ActionEnded, "sit") == 1U)
            && WorldAnimation_TryGetAction(Scene.Animation, Scene.Figure, &Action, NULL)
            && (strcmp((const char*)Action, "pick_up") == 0) && IsInState(&Scene, NULL, "pick_up"));
        // Once it has ended, the wave starts.
        TickFor(&Scene, 1.2f);
        TEST_CHECK(run, !WorldAnimation_TryGetAction(Scene.Animation, Scene.Figure, NULL, NULL));
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"wave",
            0U));
    }
    Scene_Deconstruct(run, &Scene);
}

/* T30 review: an animator removed from a holder takes its sockets' frames with it: what hangs on them falls back to the
 * holder's own space at once (their cached world matrices are marked dirty), not left where the hand was. */
static void RemovingTheAnimatorDropsTheSocketFrames(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.1f);
        WorldObject* Cup = NULL;
        World_TryGetObject(&Scene.World, Scene.Cup, &Cup);
        TEST_CHECK_SUCCESS(run, World_SetParentSocket(&Scene.World, Scene.Cup, Scene.Figure, (const unsigned char*)u8"hand",
            false));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Cup, Vector3Double_Zero()));
        World_UpdateTransforms(&Scene.World);
        Vector3 Feet = GetWorldPosition(&Scene, Scene.Figure);
        TEST_CHECK(run, IsNearVector(GetWorldPosition(&Scene, Scene.Cup), Vector3Add(Feet, (Vector3){ 0.5f, 1.2f, 0.0f })));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Scene.World, Scene.Figure, WorldComponentType_Animator));
        World_UpdateTransforms(&Scene.World);
        TEST_CHECK(run, IsNearVector(GetWorldPosition(&Scene, Scene.Cup), Feet));
    }
    Scene_Deconstruct(run, &Scene);
}

/* T30: an animator removed mid-action ends the action as it goes: its character is free, and the end is queued. */
static void RemovingTheAnimatorEndsItsAction(TestRun* run)
{
    GraphScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        TickFor(&Scene, 0.05f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_StartAction(Scene.Animation, Scene.Figure, (const unsigned char*)u8"sit",
            Scene.Bench));
        TickFor(&Scene, 0.5f);
        TEST_CHECK(run, GetLock(&Scene) == CharacterMovementLock_Locked);
        WorldAnimation_ClearEvents(Scene.Animation);
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Scene.World, Scene.Figure, WorldComponentType_Animator));
        TEST_CHECK(run, (GetLock(&Scene) == CharacterMovementLock_Free)
            && !WorldCharacterComponent_GetActionControl(GetCharacter(&Scene))->HasAnchor
            && (CountEvents(&Scene, WorldAnimationEventKind_ActionEnded, "sit") == 1U));
        Tick(&Scene);
        TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).ActionCount == 0U);
    }
    Scene_Deconstruct(run, &Scene);
}

// Public functions.
void WorldAnimationGraphTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_graph_plays_from_the_world_model_and_follows_its_character",
            AGraphPlaysFromTheWorldModelAndFollowsItsCharacter },
        { "code_sets_parameters_fires_triggers_and_reads_states", CodeSetsParametersFiresTriggersAndReadsStates },
        { "clips_from_code_play_over_the_graph", ClipsFromCodePlayOverTheGraph },
        { "sockets_carry_children_with_the_pose", SocketsCarryChildrenWithThePose },
        { "an_action_attaches_and_detaches_its_target", AnActionAttachesAndDetachesItsTarget },
        { "a_scaled_target_is_held_by_its_grip", AScaledTargetIsHeldByItsGrip },
        { "an_anchored_action_slides_the_character_onto_its_seat", AnAnchoredActionSlidesTheCharacterOntoItsSeat },
        { "actions_refuse_what_they_cannot_do", ActionsRefuseWhatTheyCannotDo },
        { "a_rebind_or_the_systems_end_frees_the_character", ARebindOrTheSystemsEndFreesTheCharacter },
        { "removing_the_animator_ends_its_action", RemovingTheAnimatorEndsItsAction },
        { "built_ins_read_the_character_in_its_bodys_frame", BuiltInsReadTheCharacterInItsBodysFrame },
        { "a_blocked_approach_fails_the_action", ABlockedApproachFailsTheAction },
        { "an_approach_arrives_only_facing_the_anchors_way", AnApproachArrivesOnlyFacingTheAnchorsWay },
        { "a_held_pushable_stays_in_the_hand", AHeldPushableStaysInTheHand },
        { "an_action_on_another_layer_waits_for_the_playing_one", AnActionOnAnotherLayerWaitsForThePlayingOne },
        { "removing_the_animator_drops_the_socket_frames", RemovingTheAnimatorDropsTheSocketFrames },
        { "the_players_camera_rides_the_eyes_socket", ThePlayersCameraRidesTheEyesSocket },
    };
    TestWorld_RunSuite(run, "WorldAnimationGraph", "WorldAnimationGraphFar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
