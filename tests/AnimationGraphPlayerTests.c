#include "TestSuites.h"
#include "TestAnimation.h"
#include "TestText.h"
#include "AnimationGraph.h"
#include "AnimationGraphPlayer.h"
#include "AnimationLibrary.h"
#include "AssetManager.h"
#include "GameSkeleton.h"
#include "SkeletalAnimation.h"
#include "raylib/raymath.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for poses and times. */
#define TEST_EPSILON 1e-4f
/** The figure's bones. */
#define BONE_COUNT 5
#define ROOT 0U
#define SPINE 1U
#define HEAD 2U
#define ARM 3U
#define HAND 4U
/** Room for a graph's text and for the events one test records. */
#define GRAPH_TEXT_CAPACITY 8192
#define MAX_RECORDED_EVENTS 64U


// Types.
/* The figure's skeleton, its library bound to it, a graph bound to both and its playback. */
typedef struct PlayerSceneStruct
{
    Model Model;
    GameSkeleton* Skeleton;
    AnimationLibrary* Library;
    SkeletalBindingBone LibraryBones[BONE_COUNT];
    SkeletalBinding LibraryBinding;
    JSONObjectPool* Pool;
    AnimationGraph* Graph;
    void* BindingStorage;
    AnimationGraphBinding Binding;
    void* InstanceStorage;
    AnimationGraphInstance Instance;
    Transform Scratch[BONE_COUNT];
    SkeletalBlendBone Blend[BONE_COUNT];
    Transform Pose[BONE_COUNT];
} PlayerScene;

/* The events an advance fired, and what the sockets hold. */
typedef struct RecorderStruct
{
    const AnimationGraph* Graph;
    char Names[MAX_RECORDED_EVENTS][32];
    uint32_t Layers[MAX_RECORDED_EVENTS];
    uint32_t Count;
    bool IsHandHolding;
} Recorder;


// Fields.
/* The figure in mesh space: the root at the floor, the spine 1 m up, the head 2 m up, an arm out to the side with its
 * hand. */
static const TestSkeletonBone FigureBones[BONE_COUNT] =
{
    { .Name = "root", .Parent = -1, .Bind = { .translation = { 0.0f, 0.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "spine", .Parent = 0, .Bind = { .translation = { 0.0f, 1.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "head", .Parent = 1, .Bind = { .translation = { 0.0f, 2.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "arm", .Parent = 1, .Bind = { .translation = { 1.0f, 1.5f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "hand", .Parent = 3, .Bind = { .translation = { 2.0f, 1.5f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
};

/* The clips (linear translations). idle, walk and run move the root: its x is how far into the cycle the clip is, its y
 * which clip it is (0, 1, 2), so a blend's pose reads its weights and whether its clips are in step. jump lifts the
 * spine half a metre over its 0.5 s, hold moves the arm 1 m forward, wave lifts the hand. */
static const float IdleTimes[] = { 0.0f, 2.0f };
static const float IdleValues[] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
static const float WalkTimes[] = { 0.0f, 1.0f };
static const float WalkValues[] = { 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f };
static const float RunTimes[] = { 0.0f, 0.5f };
static const float RunValues[] = { 0.0f, 2.0f, 0.0f, 1.0f, 2.0f, 0.0f };
static const float JumpTimes[] = { 0.0f, 0.5f };
static const float JumpValues[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.5f, 0.0f };
static const float HoldTimes[] = { 0.0f, 1.0f };
static const float HoldValues[] = { 1.0f, 0.5f, 1.0f, 1.0f, 0.5f, 1.0f };
static const float WaveTimes[] = { 0.0f, 1.0f };
static const float WaveValues[] = { 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f };
/* "still": one key, a clip of no length (a held pose). */
static const float StillTimes[] = { 0.0f };
static const float StillValues[] = { 0.0f, 0.5f, 0.0f };

/* The scene's graph: a body layer (a synced blend by speed matching the speed, a jump, an empty state) and an arms
 * layer over the arm's subtree that holds while the hand's socket holds something; look-at on the head. */
static const char* const SceneGraph =
    "{'parameters': {'is_high': {'type': 'bool'}, 'wave_now': {'type': 'trigger'}},"
    " 'layers': ["
    "  {'name': 'body', 'entry': 'move', 'states': {"
    "     'move': {'blend': {'parameter': 'speed', 'points': [{'clip': 'idle', 'at': 0}, {'clip': 'walk', 'at': 1},"
    "       {'clip': 'run', 'at': 3}]}, 'match_speed': 'speed', 'min_rate': 0.5, 'max_rate': 3,"
    "       'events': [{'name': 'loop', 'phase': 0}, {'name': 'step', 'phase': 0.25}]},"
    "     'jump': {'clip': 'jump', 'look_at': 0, 'events': [{'name': 'top', 'time': 0.25, 'set': {'is_high': true}}]},"
    "     'nothing': {}},"
    "   'transitions': ["
    "     {'from': 'any', 'to': 'jump', 'when': [{'event': 'jumped'}], 'fade_seconds': 0},"
    "     {'from': 'jump', 'to': 'move', 'exit_time': 1, 'fade_seconds': 0.5},"
    "     {'from': 'move', 'to': 'nothing', 'when': [{'parameter': 'is_crouching', 'is': true}], 'fade_seconds': 0},"
    "     {'from': 'nothing', 'to': 'move', 'when': [{'event': 'wave_now'}], 'fade_seconds': 0}]},"
    "  {'name': 'arms', 'mask': ['arm'], 'entry': 'free', 'states': {'free': {}, 'hold': {'clip': 'hold'}},"
    "   'transitions': [{'from': 'free', 'to': 'hold', 'when': [{'socket': 'hand_r', 'is_holding': true}],"
    "     'fade_seconds': 0}, {'from': 'hold', 'to': 'free', 'when': [{'socket': 'hand_r', 'is_holding': false}],"
    "     'fade_seconds': 0}]}"
    " ],"
    " 'look_at': {'bones': [{'bone': 'head'}], 'max_yaw': 60, 'max_pitch_up': 30, 'turn_speed': 90}"
    "}";


// Static functions: helpers.
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

static TestGltfChannel Translation(int node, const float* times, const float* values)
{
    return (TestGltfChannel){ .Node = node, .Path = "translation", .Interpolation = "LINEAR", .Times = times,
        .KeyCount = 2U, .Values = values, .ValueComponents = 3U };
}

/* The figure's library: its bones as nodes and the six clips, with their loops and the walk's and run's distances. */
static bool ReadLibrary(TestRun* run, AnimationLibrary** outLibrary)
{
    TestGltfNode Nodes[BONE_COUNT] =
    {
        { .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "spine", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
        { .Name = "head", .Parent = 1, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
        { .Name = "arm", .Parent = 1, .Rest = TestAnimation_Translation(1.0f, 0.5f, 0.0f) },
        { .Name = "hand", .Parent = 3, .Rest = TestAnimation_Translation(1.0f, 0.0f, 0.0f) },
    };
    TestGltfChannel Channels[] =
    {
        Translation(0, IdleTimes, IdleValues), Translation(0, WalkTimes, WalkValues), Translation(0, RunTimes, RunValues),
        Translation(1, JumpTimes, JumpValues), Translation(3, HoldTimes, HoldValues), Translation(4, WaveTimes, WaveValues),
        { .Node = 0, .Path = "translation", .Interpolation = "LINEAR", .Times = StillTimes, .KeyCount = 1U,
            .Values = StillValues, .ValueComponents = 3U },
    };
    TestGltfClip Clips[] =
    {
        { .Name = "idle", .Channels = &Channels[0], .ChannelCount = 1U },
        { .Name = "walk", .Channels = &Channels[1], .ChannelCount = 1U },
        { .Name = "run", .Channels = &Channels[2], .ChannelCount = 1U },
        { .Name = "jump", .Channels = &Channels[3], .ChannelCount = 1U },
        { .Name = "hold", .Channels = &Channels[4], .ChannelCount = 1U },
        { .Name = "wave", .Channels = &Channels[5], .ChannelCount = 1U },
        { .Name = "still", .Channels = &Channels[6], .ChannelCount = 1U },
    };
    static const AnimationClipSettings Settings[] =
    {
        { .Source = (const unsigned char*)u8"idle", .Name = (const unsigned char*)u8"idle", .IsLooped = true,
            .Speed = 1.0f },
        { .Source = (const unsigned char*)u8"walk", .Name = (const unsigned char*)u8"walk", .IsLooped = true,
            .Speed = 1.0f, .HasDistance = true, .Distance = 1.0f },
        { .Source = (const unsigned char*)u8"run", .Name = (const unsigned char*)u8"run", .IsLooped = true, .Speed = 1.0f,
            .HasDistance = true, .Distance = 1.5f },
        { .Source = (const unsigned char*)u8"hold", .Name = (const unsigned char*)u8"hold", .IsLooped = true,
            .Speed = 1.0f },
        { .Source = (const unsigned char*)u8"still", .Name = (const unsigned char*)u8"still", .IsLooped = true,
            .Speed = 1.0f },
    };
    GenericBuffer File;
    TestGltf_Write(Nodes, BONE_COUNT, Clips, sizeof(Clips) / sizeof(Clips[0]), &File);
    AnimationLibraryReadOptions Options = { .Clips = Settings, .ClipCount = sizeof(Settings) / sizeof(Settings[0]),
        .FilePath = NULL, .Logger = NULL, .Description = NULL };
    bool IsRead = TEST_CHECK_SUCCESS(run, AnimationLibrary_CreateFromGltf(File._data, File._count, &Options, outLibrary));
    Memory_Free(File._data);
    return IsRead;
}

/* Reads a graph (single quotes for double ones). */
static AnimationGraph* ReadGraph(TestRun* run, JSONObjectPool* pool, const char* text)
{
    static char Text[GRAPH_TEXT_CAPACITY];
    Unquote(text, Text, sizeof(Text));
    JSONObjectValue Root;
    AnimationGraph* Graph = NULL;
    if (TestText_ParseJSON(run, pool, Text, &Root))
    {
        if (TEST_CHECK(run, Root.Type == JSONValueType_Compound))
        {
            TEST_CHECK_SUCCESS(run, AnimationGraph_CreateFromJSON(Root.Value.Compound, (const unsigned char*)u8"test",
                &Graph));
        }
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(pool, &Root));
    }
    return Graph;
}

/* Binds a graph and starts its playback in the scene (replacing the scene's own). */
static bool UseGraph(TestRun* run, PlayerScene* self, AnimationGraph* graph)
{
    if (graph == NULL)
    {
        return false;
    }
    AnimationGraph_Destroy(self->Graph);
    Memory_Free(self->BindingStorage);
    Memory_Free(self->InstanceStorage);
    self->Graph = graph;
    self->BindingStorage = Memory_Allocate(AnimationGraphBinding_GetStorageSize(graph, self->Skeleton));
    AnimationGraphBinding_Build(graph, self->Skeleton, &self->LibraryBinding, 1U, self->BindingStorage, &self->Binding);
    self->InstanceStorage = Memory_Allocate(AnimationGraphInstance_GetStorageSize(graph));
    AnimationGraphInstance_Construct(&self->Instance, graph, self->InstanceStorage);
    return TEST_CHECK(run, true);
}

static bool Scene_Construct(TestRun* run, PlayerScene* self, const char* graphText)
{
    Memory_Zero(self, sizeof(*self));
    self->Model = TestSkeletonModel_Create(FigureBones, BONE_COUNT, 0.2f);
    if (!TEST_CHECK_SUCCESS(run, GameSkeleton_Create(&self->Model, (const unsigned char*)u8"figure", NULL,
        &self->Skeleton)) || !ReadLibrary(run, &self->Library) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&self->Pool)))
    {
        return false;
    }
    SkeletalBinding_Build(self->Skeleton, self->Library, self->LibraryBones, &self->LibraryBinding);
    return UseGraph(run, self, ReadGraph(run, self->Pool, graphText));
}

static void Scene_Deconstruct(PlayerScene* self)
{
    AnimationGraph_Destroy(self->Graph);
    Memory_Free(self->BindingStorage);
    Memory_Free(self->InstanceStorage);
    if (self->Pool != NULL)
    {
        JSONObjectPool_Deconstruct(self->Pool);
    }
    AnimationLibrary_Destroy(self->Library);
    GameSkeleton_Destroy(self->Skeleton);
    TestSkeletonModel_Destroy(&self->Model);
}

static void SetParameter(PlayerScene* scene, const char* name, float value)
{
    uint32_t Index = 0U;
    if (AnimationGraph_TryFindParameter(scene->Graph, (const unsigned char*)name, &Index))
    {
        AnimationGraphInstance_SetParameter(&scene->Instance, Index, value);
    }
}

static float GetParameter(const PlayerScene* scene, const char* name)
{
    uint32_t Index = 0U;
    return AnimationGraph_TryFindParameter(scene->Graph, (const unsigned char*)name, &Index)
        ? AnimationGraphInstance_GetParameter(&scene->Instance, Index) : NAN;
}

static void Fire(PlayerScene* scene, const char* name)
{
    uint32_t Index = 0U;
    if (AnimationGraph_TryFindEvent(scene->Graph, (const unsigned char*)name, &Index))
    {
        AnimationGraphInstance_FireEvent(&scene->Instance, Index);
    }
}

static bool IsSocketHolding(void* user, const unsigned char* socket)
{
    Recorder* Self = user;
    return Self->IsHandHolding && (strcmp((const char*)socket, "hand_r") == 0);
}

static void RecordEvent(void* user, uint32_t layer, uint32_t state, const AnimationGraphEventRecord* event)
{
    (void)state;
    Recorder* Self = user;
    if (Self->Count < MAX_RECORDED_EVENTS)
    {
        snprintf(Self->Names[Self->Count], sizeof(Self->Names[0]), "%s",
            (const char*)AnimationGraph_GetString(Self->Graph, event->_name));
        Self->Layers[Self->Count] = layer;
        Self->Count++;
    }
}

static uint32_t CountRecorded(const Recorder* recorder, const char* name)
{
    uint32_t Count = 0U;
    for (uint32_t Index = 0U; Index < recorder->Count; Index++)
    {
        Count += (strcmp(recorder->Names[Index], name) == 0) ? 1U : 0U;
    }
    return Count;
}

static void Advance(PlayerScene* scene, Recorder* recorder, double seconds)
{
    AnimationGraphContext Context = { .IsSocketHolding = IsSocketHolding, .OnEvent = RecordEvent, .User = recorder };
    recorder->Graph = scene->Graph;
    AnimationGraphInstance_Advance(&scene->Instance, &scene->Binding, seconds, &Context);
}

/* Advances in ticks of @p step for @p seconds. */
static void AdvanceFor(PlayerScene* scene, Recorder* recorder, double seconds, double step)
{
    for (double Time = 0.0; Time < (seconds - 1e-9); Time += step)
    {
        Advance(scene, recorder, step);
    }
}

static void Pose(PlayerScene* scene)
{
    AnimationGraphInstance_SamplePose(&scene->Instance, &scene->Binding, QuaternionIdentity(), scene->Scratch,
        scene->Blend, scene->Pose);
}

static uint32_t GetState(const PlayerScene* scene, uint32_t layer, const char* name)
{
    uint32_t Index = UINT32_MAX;
    AnimationGraph_TryFindState(scene->Graph, layer, (const unsigned char*)name, &Index);
    return Index;
}

static bool IsIn(const PlayerScene* scene, uint32_t layer, const char* name)
{
    return AnimationGraphInstance_GetCurrentState(&scene->Instance, layer) == GetState(scene, layer, name);
}

static const AnimationGraphEntry* FindEntry(const PlayerScene* scene, uint32_t layer, const char* name)
{
    const AnimationGraphLayerPlayback* Playback = &scene->Instance.Layers[layer];
    for (uint32_t Index = 0U; Index < Playback->EntryCount; Index++)
    {
        if (Playback->Entries[Index].State == GetState(scene, layer, name))
        {
            return &Playback->Entries[Index];
        }
    }
    return NULL;
}


// Static functions: tests.
static void TheBindingResolvesClipsMasksAndLookBones(TestRun* run)
{
    PlayerScene Scene;
    if (Scene_Construct(run, &Scene, SceneGraph))
    {
        const AnimationGraphBinding* Binding = &Scene.Binding;
        TEST_CHECK(run, (Binding->MissingClipCount == 0U) && (Binding->MissingBoneCount == 0U)
            && (Binding->LookBoneCount == 1U) && (Binding->LookBones[0] == HEAD) && IsNear(Binding->LookShares[0], 1.0f));
        // The move blend's clips: idle travels nowhere, walk 1 m a 1 s cycle, run 1.5 m a 0.5 s cycle.
        const AnimationGraphLayer* Body = AnimationGraph_GetLayer(Scene.Graph, 0U);
        const AnimationGraphBoundClip* Move = Binding->Clips + Binding->StateClips[Body->_firstState
            + GetState(&Scene, 0U, "move")];
        TEST_CHECK(run, Move[0].IsFound && Move[1].IsFound && Move[2].IsFound && IsNear(Move[0].NaturalSpeed, 0.0f)
            && IsNear(Move[1].NaturalSpeed, 1.0f) && IsNear(Move[2].NaturalSpeed, 3.0f)
            && IsNear(Move[2].Clip.Duration, 0.5f) && Move[2].Clip.IsLooped);
        // The body layer weighs every bone fully; the arms layer the arm and the hand under it.
        const float* BodyWeights = Binding->BoneWeights;
        const float* ArmWeights = Binding->BoneWeights + BONE_COUNT;
        TEST_CHECK(run, IsNear(BodyWeights[ROOT], 1.0f) && IsNear(BodyWeights[HAND], 1.0f));
        TEST_CHECK(run, IsNear(ArmWeights[ROOT], 0.0f) && IsNear(ArmWeights[SPINE], 0.0f) && IsNear(ArmWeights[HEAD], 0.0f)
            && IsNear(ArmWeights[ARM], 1.0f) && IsNear(ArmWeights[HAND], 1.0f));
    }
    // A graph naming a clip no library has and bones the skeleton lacks: counted, with the first names.
    AnimationGraph* Other = (Scene.Pool != NULL) ? ReadGraph(run, Scene.Pool, "{'layers': [{'name': 'body', 'entry': 'a', "
        "'states': {'a': {'clip': 'walk'}, 'b': {'clip': 'swim'}, 'c': {'clip': 'fly'}}}, {'name': 'legs', 'mask': ['leg', "
        "'arm'], 'weight': 0.25, 'entry': 'x', 'states': {'x': {}}}], 'look_at': {'bones': [{'bone': 'neck', 'share': 0.5},"
        " {'bone': 'head', 'share': 0.5}]}}") : NULL;
    if (UseGraph(run, &Scene, Other))
    {
        const AnimationGraphBinding* Binding = &Scene.Binding;
        TEST_CHECK(run, (Binding->MissingClipCount == 2U) && (strcmp((const char*)Binding->FirstMissingClip, "swim") == 0));
        TEST_CHECK(run, (Binding->MissingBoneCount == 2U) && (strcmp((const char*)Binding->FirstMissingBone, "leg") == 0)
            && (Binding->LookBoneCount == 1U) && IsNear(Binding->LookShares[0], 0.5f));
        // The layer's weight goes on its mask's bones.
        TEST_CHECK(run, IsNear(Binding->BoneWeights[BONE_COUNT + HAND], 0.25f)
            && IsNear(Binding->BoneWeights[BONE_COUNT + ROOT], 0.0f));
    }
    Scene_Deconstruct(&Scene);
}

static void AnInstanceStartsInItsEntryStates(TestRun* run)
{
    PlayerScene Scene;
    if (Scene_Construct(run, &Scene, SceneGraph))
    {
        TEST_CHECK(run, IsIn(&Scene, 0U, "move") && IsIn(&Scene, 1U, "free")
            && (Scene.Instance.Layers[0].EntryCount == 1U) && IsNear(Scene.Instance.Layers[0].Entries[0].Weight, 1.0f));
        TEST_CHECK(run, IsNear(GetParameter(&Scene, "is_high"), 0.0f) && IsNear(GetParameter(&Scene, "speed"), 0.0f)
            && IsNear(Scene.Instance.LookYaw, 0.0f) && IsNear(Scene.Instance.LookWeight, 1.0f));
        // Standing: idle at its start.
        Pose(&Scene);
        TEST_CHECK(run, IsNearVector(Scene.Pose[ROOT].translation, (Vector3){ 0.0f, 0.0f, 0.0f })
            && IsNearVector(Scene.Pose[SPINE].translation, (Vector3){ 0.0f, 1.0f, 0.0f }));
        // Parameters: custom booleans become 0 or 1, values that are not finite are ignored, unknown indices too.
        SetParameter(&Scene, "is_high", 0.7f);
        TEST_CHECK(run, IsNear(GetParameter(&Scene, "is_high"), 1.0f));
        SetParameter(&Scene, "is_high", NAN);
        TEST_CHECK(run, IsNear(GetParameter(&Scene, "is_high"), 1.0f));
        AnimationGraphInstance_SetParameter(&Scene.Instance, 1000U, 1.0f);
        TEST_CHECK(run, IsNear(AnimationGraphInstance_GetParameter(&Scene.Instance, 1000U), 0.0f));
    }
    Scene_Deconstruct(&Scene);
}

static void ABlendKeepsItsClipsInStepAndMatchesTheSpeed(TestRun* run)
{
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, SceneGraph))
    {
        // At 1 m/s the walk plays alone at its natural speed: a quarter of its 1 s cycle in 0.25 s.
        SetParameter(&Scene, "speed", 1.0f);
        AdvanceFor(&Scene, &Events, 0.25, 0.05);
        Pose(&Scene);
        TEST_CHECK(run, IsNearVector(Scene.Pose[ROOT].translation, (Vector3){ 0.25f, 1.0f, 0.0f }));
        // At 2 m/s walk and run share it half and half, in step (the same phase), at rate 1 (natural 2 m/s) over a
        // 0.75 s cycle: 0.15 s more moves the phase 0.2.
        SetParameter(&Scene, "speed", 2.0f);
        AdvanceFor(&Scene, &Events, 0.15, 0.05);
        Pose(&Scene);
        TEST_CHECK(run, IsNearVector(Scene.Pose[ROOT].translation, (Vector3){ 0.45f, 1.5f, 0.0f }));
        // At 6 m/s the run plays alone at twice its natural speed: 0.05 s is a fifth of its 0.5 s cycle.
        SetParameter(&Scene, "speed", 6.0f);
        AdvanceFor(&Scene, &Events, 0.05, 0.05);
        Pose(&Scene);
        TEST_CHECK(run, IsNearVector(Scene.Pose[ROOT].translation, (Vector3){ 0.65f, 2.0f, 0.0f }));
        // At 30 m/s the rate stops at its limit, 3: 0.05 s is 0.3 of the cycle (wrapping round).
        SetParameter(&Scene, "speed", 30.0f);
        AdvanceFor(&Scene, &Events, 0.05, 0.05);
        Pose(&Scene);
        TEST_CHECK(run, IsNearVector(Scene.Pose[ROOT].translation, (Vector3){ 0.95f, 2.0f, 0.0f }));
        // Barely moving, 0.1 m/s: idle 90%, walk 10%, natural 0.1 m/s, rate 1; the idle's 2 s cycle weighs in.
        SetParameter(&Scene, "speed", 0.1f);
        const AnimationGraphEntry* Move = FindEntry(&Scene, 0U, "move");
        double Before = (Move != NULL) ? Move->Time : 0.0;
        AdvanceFor(&Scene, &Events, 0.19, 0.19);
        double Expected = fmod(Before + (0.19 / ((0.9 * 2.0) + (0.1 * 1.0))), 1.0);
        TEST_CHECK(run, (Move != NULL) && IsNear((float)Move->Time, (float)Expected));
        Pose(&Scene);
        TEST_CHECK(run, IsNear(Scene.Pose[ROOT].translation.y, 0.1f) && IsNear(Scene.Pose[ROOT].translation.x,
            (float)Expected));
        // Standing still the matched rate is its lower limit (the idle has no distance: natural speed 0, rate 1).
        SetParameter(&Scene, "speed", 0.0f);
        Before = Move->Time;
        AdvanceFor(&Scene, &Events, 0.2, 0.2);
        TEST_CHECK(run, IsNear((float)Move->Time, (float)fmod(Before + 0.1, 1.0)));
    }
    Scene_Deconstruct(&Scene);
}

static void TransitionsTakeTheFirstThatHoldsAndExitTimesWait(TestRun* run)
{
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, SceneGraph))
    {
        // jumped: from any state into jump at once.
        Fire(&Scene, "jumped");
        Advance(&Scene, &Events, 0.1);
        TEST_CHECK(run, IsIn(&Scene, 0U, "jump") && (Scene.Instance.Layers[0].EntryCount == 1U));
        Pose(&Scene);
        TEST_CHECK(run, IsNear(Scene.Pose[SPINE].translation.y, 1.1f));
        // jumped again while it jumps: the transition from any state does not restart it.
        Fire(&Scene, "jumped");
        Advance(&Scene, &Events, 0.1);
        const AnimationGraphEntry* Jump = FindEntry(&Scene, 0U, "jump");
        TEST_CHECK(run, IsIn(&Scene, 0U, "jump") && (Jump != NULL) && IsNear((float)Jump->Time, 0.2f));
        // Its exit time (1: the clip ended) holds it in jump until the end (the tick after the one that reaches it,
        // at most two ticks: the clip's 0.5 s may be summed a hair short), then it fades back over 0.5 s.
        AdvanceFor(&Scene, &Events, 0.25, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "jump") && (Jump != NULL) && !Jump->HasEnded);
        uint32_t Ticks = 0U;
        for (; (Ticks < 3U) && IsIn(&Scene, 0U, "jump"); Ticks++)
        {
            Advance(&Scene, &Events, 0.05);
        }
        TEST_CHECK(run, IsIn(&Scene, 0U, "move") && (Ticks >= 2U) && (Scene.Instance.Layers[0].EntryCount == 2U));
        AdvanceFor(&Scene, &Events, 0.25, 0.05);
        const AnimationGraphEntry* Leaving = FindEntry(&Scene, 0U, "jump");
        const AnimationGraphEntry* Entering = FindEntry(&Scene, 0U, "move");
        TEST_CHECK(run, (Leaving != NULL) && (Entering != NULL) && IsNear(Leaving->Weight + Entering->Weight, 1.0f)
            && (Entering->Weight > 0.4f) && (Entering->Weight < 0.65f));
        AdvanceFor(&Scene, &Events, 0.3, 0.05);
        TEST_CHECK(run, (Scene.Instance.Layers[0].EntryCount == 1U) && IsIn(&Scene, 0U, "move"));
        // Crouching moves it to the empty state; one transition a layer a tick: jumped and the trigger in one tick take
        // the first that holds (the jump, listed first).
        SetParameter(&Scene, "is_crouching", 1.0f);
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "nothing"));
        Pose(&Scene);
        TEST_CHECK(run, IsNearVector(Scene.Pose[ROOT].translation, (Vector3){ 0.0f, 0.0f, 0.0f })
            && IsNearVector(Scene.Pose[SPINE].translation, (Vector3){ 0.0f, 1.0f, 0.0f }));
        Fire(&Scene, "jumped");
        Fire(&Scene, "wave_now");
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "jump"));
        // Events last one tick.
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "jump") && (Scene.Instance.Events == 0U));
    }
    Scene_Deconstruct(&Scene);
}

static void EventsFireOnceAsTheirTimePasses(TestRun* run)
{
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, SceneGraph))
    {
        // The walk alone: "loop" (phase 0) fires as the state starts and at every wrap, "step" (phase 0.25) once a cycle.
        SetParameter(&Scene, "speed", 1.0f);
        Advance(&Scene, &Events, 0.1);
        TEST_CHECK(run, (CountRecorded(&Events, "loop") == 1U) && (CountRecorded(&Events, "step") == 0U));
        AdvanceFor(&Scene, &Events, 0.2, 0.05);
        TEST_CHECK(run, (CountRecorded(&Events, "loop") == 1U) && (CountRecorded(&Events, "step") == 1U)
            && (Events.Layers[1] == 0U));
        AdvanceFor(&Scene, &Events, 1.0, 0.05);
        TEST_CHECK(run, (CountRecorded(&Events, "loop") == 2U) && (CountRecorded(&Events, "step") == 2U));
        // A jump's "top" at 0.25 s fires once and sets is_high; the move state, fading out, fires nothing more.
        Events.Count = 0U;
        Fire(&Scene, "jumped");
        AdvanceFor(&Scene, &Events, 0.2, 0.05);
        TEST_CHECK(run, (CountRecorded(&Events, "top") == 0U) && IsNear(GetParameter(&Scene, "is_high"), 0.0f));
        AdvanceFor(&Scene, &Events, 0.1, 0.05);
        TEST_CHECK(run, (CountRecorded(&Events, "top") == 1U) && IsNear(GetParameter(&Scene, "is_high"), 1.0f)
            && (CountRecorded(&Events, "step") == 0U));
        AdvanceFor(&Scene, &Events, 0.2, 0.05);
        TEST_CHECK(run, CountRecorded(&Events, "top") == 1U);
        // Restarting a state fires its start again: back to move, its "loop" fires as it enters.
        uint32_t Loops = CountRecorded(&Events, "loop");
        AdvanceFor(&Scene, &Events, 0.1, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "move") && (CountRecorded(&Events, "loop") == Loops + 1U));
    }
    Scene_Deconstruct(&Scene);
}

static void LayersPlayOverTheBodyOnTheirMask(TestRun* run)
{
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, SceneGraph))
    {
        SetParameter(&Scene, "speed", 1.0f);
        Advance(&Scene, &Events, 0.25);
        Pose(&Scene);
        Vector3 ArmBefore = Scene.Pose[ARM].translation;
        // The hand's socket holds something: the arms layer enters hold, and only the arm (with the hand under it) moves.
        Events.IsHandHolding = true;
        Advance(&Scene, &Events, 0.01);
        TEST_CHECK(run, IsIn(&Scene, 1U, "hold"));
        Pose(&Scene);
        TEST_CHECK(run, IsNearVector(Scene.Pose[ARM].translation, (Vector3){ 1.0f, 0.5f, 1.0f })
            && IsNearVector(ArmBefore, (Vector3){ 1.0f, 0.5f, 0.0f }));
        TEST_CHECK(run, IsNear(Scene.Pose[ROOT].translation.y, 1.0f) && IsNear(Scene.Pose[ROOT].translation.x, 0.26f));
        // Let go: back to free, the arm as the body has it.
        Events.IsHandHolding = false;
        Advance(&Scene, &Events, 0.01);
        Pose(&Scene);
        TEST_CHECK(run, IsIn(&Scene, 1U, "free") && IsNearVector(Scene.Pose[ARM].translation, ArmBefore));
    }
    // A layer at half weight moves its bones half way.
    AnimationGraph* Half = (Scene.Pool != NULL) ? ReadGraph(run, Scene.Pool, "{'layers': [{'name': 'body', 'entry': 'a', "
        "'states': {'a': {'clip': 'walk'}}}, {'name': 'arms', 'mask': ['arm'], 'weight': 0.5, 'entry': 'h', 'states': "
        "{'h': {'clip': 'hold'}}}]}") : NULL;
    if (UseGraph(run, &Scene, Half))
    {
        Advance(&Scene, &Events, 0.0);
        Pose(&Scene);
        TEST_CHECK(run, IsNearVector(Scene.Pose[ARM].translation, (Vector3){ 1.0f, 0.5f, 0.5f })
            && IsNear(Scene.Pose[ROOT].translation.y, 1.0f));
    }
    Scene_Deconstruct(&Scene);
}

static void LookAtEasesTowardTheLookWithinItsLimits(TestRun* run)
{
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, SceneGraph))
    {
        // A look 90 degrees to the side: the head turns at 90 degrees a second, up to its 60 degree limit.
        SetParameter(&Scene, "look_yaw", 90.0f);
        SetParameter(&Scene, "look_pitch", -10.0f);
        AdvanceFor(&Scene, &Events, 0.5, 0.05);
        TEST_CHECK(run, IsNear(Scene.Instance.LookYaw, 45.0f * DEG2RAD) && IsNear(Scene.Instance.LookPitch,
            -10.0f * DEG2RAD));
        AdvanceFor(&Scene, &Events, 0.5, 0.05);
        TEST_CHECK(run, IsNear(Scene.Instance.LookYaw, 60.0f * DEG2RAD));
        // The pose: the head turned by the yaw and the pitch, the rest of the body as it was.
        Pose(&Scene);
        Matrix Matrices[BONE_COUNT];
        SkeletalPose_ComputeModelMatrices(Scene.Skeleton, Scene.Pose, Matrices);
        Quaternion Head = QuaternionFromMatrix(Matrices[HEAD]);
        Vector3 Looking = Vector3RotateByQuaternion((Vector3){ 0.0f, 0.0f, 1.0f }, Head);
        Vector3 Expected = { sinf(60.0f * DEG2RAD) * cosf(10.0f * DEG2RAD), -sinf(10.0f * DEG2RAD),
            cosf(60.0f * DEG2RAD) * cosf(10.0f * DEG2RAD) };
        TEST_CHECK(run, IsNearVector(Looking, Expected));
        TEST_CHECK(run, IsNear(QuaternionFromMatrix(Matrices[SPINE]).w, 1.0f));
        // A model exported facing +X (its import turns mesh space -90 degrees about Y into the object's): the head
        // still looks where the object looks.
        Quaternion Import = QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, -90.0f * DEG2RAD);
        AnimationGraphInstance_SamplePose(&Scene.Instance, &Scene.Binding, Import, Scene.Scratch, Scene.Blend,
            Scene.Pose);
        SkeletalPose_ComputeModelMatrices(Scene.Skeleton, Scene.Pose, Matrices);
        Vector3 MeshForward = Vector3RotateByQuaternion((Vector3){ 0.0f, 0.0f, 1.0f }, QuaternionInvert(Import));
        Vector3 ObjectLook = Vector3RotateByQuaternion(Vector3RotateByQuaternion(MeshForward,
            QuaternionFromMatrix(Matrices[HEAD])), Import);
        TEST_CHECK(run, IsNearVector(MeshForward, (Vector3){ 1.0f, 0.0f, 0.0f }) && IsNearVector(ObjectLook, Expected));
        // In jump (look_at 0) look-at lets go: its weight follows the states.
        Fire(&Scene, "jumped");
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsNear(Scene.Instance.LookWeight, 0.0f));
        Pose(&Scene);
        TEST_CHECK(run, IsNear(Scene.Pose[HEAD].rotation.w, 1.0f));
    }
    Scene_Deconstruct(&Scene);
}

static void EnteringStatesCrossFadesAndAFullLayerDropsTheLightest(TestRun* run)
{
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'walk'}, "
        "'b': {'clip': 'run'}, 'c': {'clip': 'idle'}, 'd': {'clip': 'jump'}, 'e': {}}}]}"))
    {
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "b"), 1.0f, false);
        Advance(&Scene, &Events, 0.25);
        const AnimationGraphEntry* A = FindEntry(&Scene, 0U, "a");
        const AnimationGraphEntry* B = FindEntry(&Scene, 0U, "b");
        TEST_CHECK(run, (A != NULL) && (B != NULL) && IsNear(A->Weight, 0.75f) && IsNear(B->Weight, 0.25f));
        // Back into a: it keeps its time (it still fades), and the weights keep their sum.
        double TimeOfA = A->Time;
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "a"), 0.5f, false);
        A = FindEntry(&Scene, 0U, "a");
        TEST_CHECK(run, (A != NULL) && IsNear((float)A->Time, (float)TimeOfA) && IsIn(&Scene, 0U, "a"));
        // A state that falls to 0 at once goes at once.
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "c"), 0.0f, false);
        TEST_CHECK(run, (Scene.Instance.Layers[0].EntryCount == 1U) && IsIn(&Scene, 0U, "c"));
        // More states than the layer holds, each with some weight: the lightest goes, the one entering stays, and the
        // weights never add up past 1.
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "a"), 1.0f, false);
        Advance(&Scene, &Events, 0.25);
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "b"), 1.0f, false);
        Advance(&Scene, &Events, 0.25);
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "d"), 1.0f, false);
        Advance(&Scene, &Events, 0.25);
        const AnimationGraphLayerPlayback* Playback = &Scene.Instance.Layers[0];
        TEST_CHECK(run, Playback->EntryCount == ANIMATION_GRAPH_PLAYER_MAX_ENTRIES);
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "e"), 1.0f, false);
        float Sum = 0.0f;
        for (uint32_t Index = 0U; Index < Playback->EntryCount; Index++)
        {
            Sum += Playback->Entries[Index].Weight;
        }
        TEST_CHECK(run, (Playback->EntryCount == ANIMATION_GRAPH_PLAYER_MAX_ENTRIES) && IsIn(&Scene, 0U, "e")
            && (FindEntry(&Scene, 0U, "e") != NULL) && (FindEntry(&Scene, 0U, "d") != NULL) && (Sum <= 1.0001f));
        AdvanceFor(&Scene, &Events, 1.0, 0.25);
        TEST_CHECK(run, (Playback->EntryCount == 1U) && IsNear(Playback->Entries[0].Weight, 1.0f));
        // Unknown layers and states change nothing; a restart starts a playing state over.
        AnimationGraphInstance_EnterState(&Scene.Instance, 5U, 0U, 0.0f, false);
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, 99U, 0.0f, false);
        TEST_CHECK(run, IsIn(&Scene, 0U, "e"));
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "d"), 0.0f, false);
        Advance(&Scene, &Events, 0.2);
        AnimationGraphInstance_EnterState(&Scene.Instance, 0U, GetState(&Scene, 0U, "d"), 0.0f, true);
        const AnimationGraphEntry* D = FindEntry(&Scene, 0U, "d");
        TEST_CHECK(run, (D != NULL) && IsNear((float)D->Time, 0.0f) && (Scene.Instance.Layers[0].EntryCount == 1U));
    }
    Scene_Deconstruct(&Scene);
}

static void MissingClipsRunASecondAndPlayNothing(TestRun* run)
{
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'swim', "
        "'events': [{'name': 'kick', 'time': 0.5}]}, 'b': {'clip': 'walk'}}, 'transitions': [{'from': 'a', 'to': 'b', "
        "'exit_time': 1, 'fade_seconds': 0}]}]}"))
    {
        TEST_CHECK(run, Scene.Binding.MissingClipCount == 1U);
        Advance(&Scene, &Events, 0.6);
        Pose(&Scene);
        TEST_CHECK(run, IsIn(&Scene, 0U, "a") && (CountRecorded(&Events, "kick") == 1U)
            && IsNearVector(Scene.Pose[ROOT].translation, (Vector3){ 0.0f, 0.0f, 0.0f }));
        Advance(&Scene, &Events, 0.5);
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "b"));
    }
    Scene_Deconstruct(&Scene);
}

static void ABlendsWeightsAreSharedByNeighbours(TestRun* run)
{
    PlayerScene Scene;
    if (Scene_Construct(run, &Scene, SceneGraph))
    {
        const AnimationGraphLayer* Body = AnimationGraph_GetLayer(Scene.Graph, 0U);
        const AnimationGraphState* Move = AnimationGraph_GetLayerState(Scene.Graph, Body, GetState(&Scene, 0U, "move"));
        float Weights[ANIMATION_GRAPH_MAX_BLEND_POINTS];
        AnimationGraphPlayer_GetBlendWeights(Scene.Graph, Move, -5.0f, Weights);
        TEST_CHECK(run, IsNear(Weights[0], 1.0f) && IsNear(Weights[1], 0.0f) && IsNear(Weights[2], 0.0f));
        AnimationGraphPlayer_GetBlendWeights(Scene.Graph, Move, 0.25f, Weights);
        TEST_CHECK(run, IsNear(Weights[0], 0.75f) && IsNear(Weights[1], 0.25f) && IsNear(Weights[2], 0.0f));
        AnimationGraphPlayer_GetBlendWeights(Scene.Graph, Move, 2.5f, Weights);
        TEST_CHECK(run, IsNear(Weights[0], 0.0f) && IsNear(Weights[1], 0.25f) && IsNear(Weights[2], 0.75f));
        AnimationGraphPlayer_GetBlendWeights(Scene.Graph, Move, 3.0f, Weights);
        TEST_CHECK(run, IsNear(Weights[2], 1.0f) && IsNear(Weights[1], 0.0f));
        AnimationGraphPlayer_GetBlendWeights(Scene.Graph, Move, NAN, Weights);
        TEST_CHECK(run, IsNear(Weights[0], 1.0f));
    }
    Scene_Deconstruct(&Scene);
}


/* The first transition that holds wins (one a layer a tick), "above" is strict and "at_least" is not, a transition
 * from any state into the state the layer is in is passed over (it does not hide the ones after it), and a state fading
 * out fires no more events: only the layer's current state does. */
static void TheFirstThatHoldsWinsAndOnlyTheCurrentStateFires(TestRun* run)
{
    static const char* const Graph =
        "{'parameters': {'go': {'type': 'float'}, 'back': {'type': 'bool'}},"
        " 'layers': [{'name': 'body', 'entry': 'idle', 'states': {"
        "     'idle': {'clip': 'idle', 'events': [{'name': 'late', 'time': 0.5}]}, 'a': {'clip': 'walk'},"
        "     'b': {'clip': 'run'}},"
        "   'transitions': ["
        "     {'from': 'any', 'to': 'idle', 'when': [{'parameter': 'back', 'is': true}], 'fade_seconds': 0},"
        "     {'from': 'idle', 'to': 'a', 'when': [{'parameter': 'go', 'above': 1}], 'fade_seconds': 1},"
        "     {'from': 'idle', 'to': 'b', 'when': [{'parameter': 'go', 'at_least': 1}], 'fade_seconds': 1}]}]}";
    // At the threshold: not above it, but at least it.
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, Graph))
    {
        SetParameter(&Scene, "go", 1.0f);
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "b"));
    }
    Scene_Deconstruct(&Scene);
    // Past it both hold: the first listed wins. Idle fades out over a second, its clip passing 0.5 s: no "late".
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, Graph))
    {
        Advance(&Scene, &Events, 0.1);
        SetParameter(&Scene, "go", 2.0f);
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "a") && (Scene.Instance.Layers[0].EntryCount == 2U));
        AdvanceFor(&Scene, &Events, 0.6, 0.05);
        const AnimationGraphEntry* Idle = FindEntry(&Scene, 0U, "idle");
        TEST_CHECK(run, (Idle != NULL) && (Idle->Time > 0.5) && (CountRecorded(&Events, "late") == 0U));
    }
    Scene_Deconstruct(&Scene);
    // "back" holds from any state, idle included, but leads into the state it is in: passed over, so idle -> a still goes.
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, Graph))
    {
        SetParameter(&Scene, "back", 1.0f);
        SetParameter(&Scene, "go", 2.0f);
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "a"));
        // In a, "back" leads elsewhere: taken.
        Advance(&Scene, &Events, 0.05);
        TEST_CHECK(run, IsIn(&Scene, 0U, "idle"));
    }
    Scene_Deconstruct(&Scene);
}

/* T30 review: a looping clip of no length (one key: a held pose) fires its events once as its state starts, not every
 * tick (its time stays 0, so a later event would be passed again each tick). */
static void AClipOfOnePoseFiresItsEventsOnce(TestRun* run)
{
    static const char* const Graph =
        "{'layers': [{'name': 'body', 'entry': 'held', 'states': {"
        "   'held': {'clip': 'still', 'events': [{'name': 'begin', 'time': 0}, {'name': 'soon', 'time': 0.001}]}}}]}";
    PlayerScene Scene;
    Recorder Events;
    Memory_Zero(&Events, sizeof(Events));
    if (Scene_Construct(run, &Scene, Graph))
    {
        AdvanceFor(&Scene, &Events, 0.05, 1.0 / 480.0);
        TEST_CHECK(run, (CountRecorded(&Events, "begin") == 1U) && (CountRecorded(&Events, "soon") == 1U));
        Pose(&Scene);
        TEST_CHECK(run, IsNear(Scene.Pose[ROOT].translation.y, 0.5f));
    }
    Scene_Deconstruct(&Scene);
}

/* The state a layer of a scene is in, by name. */
static bool IsShippedInState(const AnimationGraph* graph, const AnimationGraphInstance* instance, uint32_t layer,
    const char* name)
{
    uint32_t State = 0U;
    return AnimationGraph_TryFindState(graph, layer, (const unsigned char*)name, &State)
        && (AnimationGraphInstance_GetCurrentState(instance, layer) == State);
}

/* T30: the shipped humanoid graph (compile/out/asset/animation_graphs/test_world) on the shipped library, bound to a
 * skeleton of the library's own bones (the placeholder models share them): every clip, mask bone and look-at bone it
 * names is there; it walks by speed, sits through its action's states and holds what the right hand's socket holds.
 * Every shipped definition reads, the world models' sockets and first_person blocks among them. */
static void TheShippedHumanoidGraphFitsItsLibrary(TestRun* run)
{
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, GRAPH_TEXT_CAPACITY, sizeof(unsigned char));
    AssetManager* Manager = NULL;
    StandardAssetTypes Types;
    JSONObjectPool* Pool = NULL;
    size_t FailedCount = 1U;
    AssetUserID User = ASSET_USER_ID_INVALID;
    AnimationLibrary* Library = NULL;
    AnimationGraph* Graph = NULL;
    bool IsLoaded = TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Manager, &Types, &Pool))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Manager, Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Manager, &FailedCount))
        && TEST_CHECK(run, FailedCount == 0U)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadAnimationLibrary(Manager,
            (const unsigned char*)u8"test_humanoid_motions", User, &Library))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadAnimationGraph(Manager, (const unsigned char*)u8"test_humanoid", User,
            &Graph));
    // The skeleton: the library's bones without the armature above them, bound where they rest.
    TestSkeletonBone Bones[ANIMATION_LIBRARY_MAX_BONES];
    int32_t NewIndex[ANIMATION_LIBRARY_MAX_BONES];
    int BoneCount = 0;
    for (uint32_t Index = 0U; IsLoaded && (Index < AnimationLibrary_GetBoneCount(Library)); Index++)
    {
        const AnimationLibraryBone* Bone = AnimationLibrary_GetBone(Library, Index);
        NewIndex[Index] = -1;
        if (strcmp((const char*)Bone->_name, "armature") == 0)
        {
            continue;
        }
        Bones[BoneCount] = (TestSkeletonBone){ .Name = (const char*)Bone->_name,
            .Parent = (Bone->_parent >= 0) ? NewIndex[Bone->_parent] : -1,
            .Bind = { .translation = Bone->_restPosition, .rotation = QuaternionIdentity(),
                .scale = { 1.0f, 1.0f, 1.0f } } };
        NewIndex[Index] = BoneCount++;
    }
    Model Figure = { 0 };
    GameSkeleton* Skeleton = NULL;
    SkeletalBindingBone LibraryBones[ANIMATION_LIBRARY_MAX_BONES];
    SkeletalBinding LibraryBinding;
    void* BindingStorage = NULL;
    void* InstanceStorage = NULL;
    if (IsLoaded && TEST_CHECK(run, BoneCount == 19))
    {
        Figure = TestSkeletonModel_Create(Bones, BoneCount, 0.05f);
        IsLoaded = TEST_CHECK_SUCCESS(run, GameSkeleton_Create(&Figure, (const unsigned char*)u8"humanoid", NULL,
            &Skeleton));
    }
    if (IsLoaded && (Skeleton != NULL))
    {
        SkeletalBinding_Build(Skeleton, Library, LibraryBones, &LibraryBinding);
        BindingStorage = Memory_Allocate(AnimationGraphBinding_GetStorageSize(Graph, Skeleton));
        AnimationGraphBinding Binding;
        AnimationGraphBinding_Build(Graph, Skeleton, &LibraryBinding, 1U, BindingStorage, &Binding);
        TEST_CHECK(run, (Binding.MissingClipCount == 0U) && (Binding.MissingBoneCount == 0U)
            && (Binding.LookBoneCount == 2U));
        InstanceStorage = Memory_Allocate(AnimationGraphInstance_GetStorageSize(Graph));
        AnimationGraphInstance Instance;
        AnimationGraphInstance_Construct(&Instance, Graph, InstanceStorage);
        Recorder Events = { .Count = 0U, .IsHandHolding = false };
        PlayerScene View = { .Graph = Graph, .Binding = Binding };
        View.Instance = Instance;
        // Walking at the test human's walk speed, standing again, then the sit action's states: down, sitting, and up
        // on the action's end.
        uint32_t Speed = 0U;
        AnimationGraph_TryFindParameter(Graph, (const unsigned char*)u8"speed", &Speed);
        TEST_CHECK(run, IsShippedInState(Graph, &View.Instance, 0U, "idle"));
        AnimationGraphInstance_SetParameter(&View.Instance, Speed, 2.0f);
        AdvanceFor(&View, &Events, 0.5, 1.0 / 480.0);
        TEST_CHECK(run, IsShippedInState(Graph, &View.Instance, 0U, "move"));
        AnimationGraphInstance_SetParameter(&View.Instance, Speed, 0.0f);
        AdvanceFor(&View, &Events, 0.5, 1.0 / 480.0);
        TEST_CHECK(run, IsShippedInState(Graph, &View.Instance, 0U, "idle"));
        uint32_t Sit = 0U;
        uint32_t EndAction = 0U;
        TEST_CHECK(run, AnimationGraph_TryFindAction(Graph, (const unsigned char*)u8"sit", &Sit)
            && AnimationGraph_TryFindEvent(Graph, (const unsigned char*)u8"end_action", &EndAction));
        uint32_t SitDown = 0U;
        AnimationGraph_TryFindState(Graph, 0U, (const unsigned char*)u8"sit_down", &SitDown);
        AnimationGraphInstance_EnterState(&View.Instance, 0U, SitDown, 0.2f, true);
        AdvanceFor(&View, &Events, 1.5, 1.0 / 480.0);
        TEST_CHECK(run, IsShippedInState(Graph, &View.Instance, 0U, "sitting"));
        AnimationGraphInstance_FireEvent(&View.Instance, EndAction);
        Advance(&View, &Events, 1.0 / 480.0);
        TEST_CHECK(run, IsShippedInState(Graph, &View.Instance, 0U, "stand_up"));
        AdvanceFor(&View, &Events, 1.5, 1.0 / 480.0);
        TEST_CHECK(run, IsShippedInState(Graph, &View.Instance, 0U, "idle"));
        // The right arm holds what its socket holds.
        TEST_CHECK(run, IsShippedInState(Graph, &View.Instance, 1U, "free"));
        Events.IsHandHolding = true;
        Advance(&View, &Events, 1.0 / 480.0);
        TEST_CHECK(run, IsShippedInState(Graph, &View.Instance, 1U, "holding"));
    }
    Memory_Free(BindingStorage);
    Memory_Free(InstanceStorage);
    GameSkeleton_Destroy(Skeleton);
    if (Figure.meshCount > 0)
    {
        TestSkeletonModel_Destroy(&Figure);
    }
    if (Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(Manager));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Root._data);
}


// Public functions.
void AnimationGraphPlayerTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_binding_resolves_clips_masks_and_look_bones", TheBindingResolvesClipsMasksAndLookBones },
        { "an_instance_starts_in_its_entry_states", AnInstanceStartsInItsEntryStates },
        { "a_blend_keeps_its_clips_in_step_and_matches_the_speed", ABlendKeepsItsClipsInStepAndMatchesTheSpeed },
        { "transitions_take_the_first_that_holds_and_exit_times_wait", TransitionsTakeTheFirstThatHoldsAndExitTimesWait },
        { "events_fire_once_as_their_time_passes", EventsFireOnceAsTheirTimePasses },
        { "layers_play_over_the_body_on_their_mask", LayersPlayOverTheBodyOnTheirMask },
        { "look_at_eases_toward_the_look_within_its_limits", LookAtEasesTowardTheLookWithinItsLimits },
        { "entering_states_cross_fades_and_a_full_layer_drops_the_lightest",
            EnteringStatesCrossFadesAndAFullLayerDropsTheLightest },
        { "missing_clips_run_a_second_and_play_nothing", MissingClipsRunASecondAndPlayNothing },
        { "a_blends_weights_are_shared_by_neighbours", ABlendsWeightsAreSharedByNeighbours },
        { "the_first_that_holds_wins_and_only_the_current_state_fires",
            TheFirstThatHoldsWinsAndOnlyTheCurrentStateFires },
        { "a_clip_of_one_pose_fires_its_events_once", AClipOfOnePoseFiresItsEventsOnce },
        { "the_shipped_humanoid_graph_fits_its_library", TheShippedHumanoidGraphFitsItsLibrary },
    };
    TestRun_RunSuite(run, "AnimationGraphPlayer", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
