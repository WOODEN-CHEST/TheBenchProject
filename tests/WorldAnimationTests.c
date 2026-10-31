#include "TestSuites.h"
#include "TestAnimation.h"
#include "TestAssets.h"
#include "TestWorld.h"
#include "AnimationLibrary.h"
#include "GameSkeleton.h"
#include "Logger.h"
#include "PropertyDescriptor.h"
#include "SkeletalAnimation.h"
#include "World.h"
#include "WorldAnimation.h"
#include "WorldAnimatorComponent.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for positions the poses give (single precision in mesh space, double from there). */
#define TEST_EPSILON 1e-4f
/** Room for paths and a log file. */
#define ANIMATION_TEST_TEXT_CAPACITY 4096
/** The test figure's box size: each bone's box reaches a quarter of it (5 cm) from the bone. */
#define FIGURE_BOX_SIZE 0.2f
/** The test figure's bones. */
#define FIGURE_BONE_COUNT 4


// Types.
/* The fixture's assets, a world on them and its animation system. */
typedef struct AnimationSceneStruct
{
    TestAssetFixture Assets;
    World World;
    KeyRegistry Keys;
    WorldAnimation* Animation;
    bool HasWorld;
} AnimationScene;

/* An object of a scene with a model component (unless it names no world model) and an animator. */
typedef struct FigureStruct
{
    uint64_t Id;
    WorldObject* Object;
    WorldModelComponent* ModelComponent;
    WorldAnimatorComponent* Animator;
} Figure;


// Fields: the test figure. Its bones in mesh space: the root at the floor, the spine 1 m up, the head 2 m up and a hand
// 1 m to the side of the spine. Every library below has the same bones (the same names and rest poses), so its height
// ratio is 1 and a clip's translations show unscaled.
static const TestSkeletonBone FigureBones[FIGURE_BONE_COUNT] =
{
    { .Name = "root", .Parent = -1, .Bind = { .translation = { 0.0f, 0.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "spine", .Parent = 0, .Bind = { .translation = { 0.0f, 1.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "head", .Parent = 1, .Bind = { .translation = { 0.0f, 2.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "hand", .Parent = 1, .Bind = { .translation = { 1.0f, 1.5f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
};

// The clips. lift: the spine rises 1 m over 1 s. up and down: the spine held 1 m above or below its rest. bob: the spine
// rises 0.5 m and comes back over 1 s (the definition loops it and plays it twice as fast). wave (another library): the
// hand rises 1 m over 1 s. spin (a library of other bones): moves a bone the figure does not have.
static const float TwoKeys[] = { 0.0f, 1.0f };
static const float ThreeKeys[] = { 0.0f, 0.5f, 1.0f };
static const float LiftValues[] = { 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f };
static const float UpValues[] = { 0.0f, 2.0f, 0.0f, 0.0f, 2.0f, 0.0f };
static const float DownValues[] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float BobValues[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, 1.0f, 0.0f };
static const float WaveValues[] = { 1.0f, 0.5f, 0.0f, 1.0f, 1.5f, 0.0f };
static const float SpinValues[] = { 0.0f, 1.0f, 0.0f, 0.0f, 3.0f, 0.0f };


// Static functions: helpers.
/* One tick and the frame after it: the update, then the pose step (poses are made once per shown frame). */
static void Tick(AnimationScene* scene, float seconds)
{
    WorldAnimation_Update(scene->Animation, seconds);
    WorldAnimation_Pose(scene->Animation);
}

static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static bool IsNearVector(Vector3 a, Vector3 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y) && IsNear(a.z, b.z);
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Found = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, ANIMATION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, ANIMATION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* At = strstr((const char*)Text._data, needle); At != NULL; At = strstr(At + 1, needle))
        {
            Found++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Found;
}

/* The figure's bones as library nodes (local rest poses). */
static void GetFigureNodes(TestGltfNode* outNodes)
{
    outNodes[0] = (TestGltfNode){ .Name = "root", .Parent = -1, .Rest = TestAnimation_Identity() };
    outNodes[1] = (TestGltfNode){ .Name = "spine", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) };
    outNodes[2] = (TestGltfNode){ .Name = "head", .Parent = 1, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) };
    outNodes[3] = (TestGltfNode){ .Name = "hand", .Parent = 1, .Rest = TestAnimation_Translation(1.0f, 0.5f, 0.0f) };
}

/* A linear translation channel. */
static TestGltfChannel MakeTranslation(int node, const float* times, uint32_t keyCount, const float* values)
{
    return (TestGltfChannel){ .Node = node, .Path = "translation", .Interpolation = "LINEAR", .Times = times,
        .KeyCount = keyCount, .Values = values, .ValueComponents = 3U };
}

/* "motions": lift, up, down and bob, all on the spine. */
static void WriteMotions(GenericBuffer* out)
{
    TestGltfNode Nodes[FIGURE_BONE_COUNT];
    GetFigureNodes(Nodes);
    TestGltfChannel Lift = MakeTranslation(1, TwoKeys, 2U, LiftValues);
    TestGltfChannel Up = MakeTranslation(1, TwoKeys, 2U, UpValues);
    TestGltfChannel Down = MakeTranslation(1, TwoKeys, 2U, DownValues);
    TestGltfChannel Bob = MakeTranslation(1, ThreeKeys, 3U, BobValues);
    TestGltfClip Clips[] =
    {
        { .Name = "lift", .Channels = &Lift, .ChannelCount = 1U },
        { .Name = "up", .Channels = &Up, .ChannelCount = 1U },
        { .Name = "down", .Channels = &Down, .ChannelCount = 1U },
        { .Name = "bob", .Channels = &Bob, .ChannelCount = 1U },
    };
    TestGltf_Write(Nodes, FIGURE_BONE_COUNT, Clips, sizeof(Clips) / sizeof(Clips[0]), out);
}

/* "gestures": wave, on the hand. */
static void WriteGestures(GenericBuffer* out)
{
    TestGltfNode Nodes[FIGURE_BONE_COUNT];
    GetFigureNodes(Nodes);
    TestGltfChannel WaveChannel = MakeTranslation(3, TwoKeys, 2U, WaveValues);
    TestGltfClip Clip = { .Name = "wave", .Channels = &WaveChannel, .ChannelCount = 1U };
    TestGltf_Write(Nodes, FIGURE_BONE_COUNT, &Clip, 1U, out);
}

/* "foreign": spin, on bones the figure does not have. */
static void WriteForeign(GenericBuffer* out)
{
    TestGltfNode Nodes[2] =
    {
        { .Name = "a", .Parent = -1, .Rest = TestAnimation_Identity() },
        { .Name = "b", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
    };
    TestGltfChannel Spin = MakeTranslation(1, TwoKeys, 2U, SpinValues);
    TestGltfClip Clip = { .Name = "spin", .Channels = &Spin, .ChannelCount = 1U };
    TestGltf_Write(Nodes, 2U, &Clip, 1U, out);
}

/*
 * The scene's assets: the figure as two skinned models (figure_model with the identity import transform, tall_model
 * scaled 2 times then raised 0.5 m) and a statue without bones; the libraries motions, gestures and foreign; the world
 * models figure, tall_figure and statue (playing motions) and foreigner (the figure playing foreign). Then a world on
 * them and its animation system, both with @p logger (may be NULL).
 */
static bool Scene_Construct(TestRun* run, AnimationScene* self, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    static const AnimationClipSettings MotionClips[] =
    {
        { .Source = (const unsigned char*)u8"bob", .Name = (const unsigned char*)u8"bob", .IsLooped = true, .Speed = 2.0f },
        { .Source = (const unsigned char*)u8"lift", .Name = (const unsigned char*)u8"lift", .Speed = 1.0f },
        { .Source = (const unsigned char*)u8"up", .Name = (const unsigned char*)u8"up", .Speed = 1.0f },
        { .Source = (const unsigned char*)u8"down", .Name = (const unsigned char*)u8"down", .Speed = 1.0f },
    };
    Matrix Tall = MatrixMultiply(MatrixScale(2.0f, 2.0f, 2.0f), MatrixTranslate(0.0f, 0.5f, 0.0f));
    BoundingBox StatueBounds = { .min = { -0.5f, 0.0f, -0.5f }, .max = { 0.5f, 2.0f, 0.5f } };
    GenericBuffer Motions;
    GenericBuffer Gestures;
    GenericBuffer Foreign;
    WriteMotions(&Motions);
    WriteGestures(&Gestures);
    WriteForeign(&Foreign);
    bool IsBuilt = TestAssetFixture_Construct(run, &self->Assets)
        && TestAssets_AddSkinnedModel(run, &self->Assets, "figure_model", FigureBones, FIGURE_BONE_COUNT,
            FIGURE_BOX_SIZE, MatrixIdentity(), NULL)
        && TestAssets_AddSkinnedModel(run, &self->Assets, "tall_model", FigureBones, FIGURE_BONE_COUNT, FIGURE_BOX_SIZE,
            Tall, NULL)
        && TestAssets_AddModel(run, &self->Assets, "statue_model", StatueBounds, NULL)
        && TestAssets_AddAnimationLibrary(run, &self->Assets, "motions", (const char*)Motions._data, MotionClips,
            sizeof(MotionClips) / sizeof(MotionClips[0]), NULL)
        && TestAssets_AddAnimationLibrary(run, &self->Assets, "gestures", (const char*)Gestures._data, NULL, 0U, NULL)
        && TestAssets_AddAnimationLibrary(run, &self->Assets, "foreign", (const char*)Foreign._data, NULL, 0U, NULL)
        && TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"figure\", \"lods\": [{\"model\": \"figure_model\", "
            "\"max_distance\": 600}], \"animation_libraries\": [\"motions\"]}")
        && TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"tall_figure\", \"lods\": [{\"model\": "
            "\"tall_model\", \"max_distance\": 600}], \"animation_libraries\": [\"motions\"]}")
        && TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"statue\", \"lods\": [{\"model\": \"statue_model\", "
            "\"max_distance\": 600}], \"animation_libraries\": [\"motions\"]}")
        && TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"foreigner\", \"lods\": [{\"model\": "
            "\"figure_model\", \"max_distance\": 600}], \"animation_libraries\": [\"foreign\"]}");
    Memory_Free(Motions._data);
    Memory_Free(Gestures._data);
    Memory_Free(Foreign._data);
    if (IsBuilt)
    {
        self->HasWorld = TestWorld_Construct(run, &self->World, &self->Keys, self->Assets.Manager, logger);
        IsBuilt = self->HasWorld && TEST_CHECK_SUCCESS(run, WorldAnimation_Create(&self->World, self->Assets.Manager,
            logger, &self->Animation));
    }
    return IsBuilt;
}

static void Scene_Deconstruct(TestRun* run, AnimationScene* self)
{
    WorldAnimation_Destroy(self->Animation);
    if (self->HasWorld)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

/* Adds an object at @p position (the scene's) with a model component drawing @p worldModel (none for NULL) and an
 * animator. */
static bool AddFigure(TestRun* run, AnimationScene* scene, const char* worldModel, Vector3 position, Figure* outFigure)
{
    Memory_Zero(outFigure, sizeof(*outFigure));
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"figure", &outFigure->Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(outFigure->Object, TestWorld_At(position))))
    {
        return false;
    }
    outFigure->Id = WorldObject_GetId(outFigure->Object);
    WorldComponent* Component = NULL;
    if (worldModel != NULL)
    {
        if (!TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, outFigure->Id, WorldComponentType_Model,
            &Component)))
        {
            return false;
        }
        outFigure->ModelComponent = WorldModelComponent_FromComponent(Component);
        if (!TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(outFigure->ModelComponent,
            (const unsigned char*)worldModel)))
        {
            return false;
        }
    }
    if (!TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, outFigure->Id, WorldComponentType_Animator,
        &Component)))
    {
        return false;
    }
    outFigure->Animator = WorldAnimatorComponent_FromComponent(Component);
    return true;
}

/* Sets an animator's clip row: plays @p clip (NULL: nothing) and cross-fades changes over @p fadeSeconds. */
static bool PlayRow(TestRun* run, const Figure* figure, const char* clip, float fadeSeconds)
{
    return TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetFadeSeconds(figure->Animator, fadeSeconds))
        && TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetClip(figure->Animator, (const unsigned char*)clip))
        && TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetPlaysClip(figure->Animator, clip != NULL));
}

/* A bone's position in mesh space in the animator's current pose; NaN without a pose or such a bone. */
static Vector3 GetPosePosition(const Figure* figure, const char* bone)
{
    const WorldAnimatorPose* Pose = WorldAnimatorComponent_GetPose(figure->Animator);
    uint32_t Index = 0U;
    if ((Pose == NULL) || !GameSkeleton_TryFindBone(Pose->_skeleton, (const unsigned char*)bone, &Index))
    {
        return (Vector3){ NAN, NAN, NAN };
    }
    Matrix BoneMatrix = Pose->_boneMatrices[Index];
    return (Vector3){ BoneMatrix.m12, BoneMatrix.m13, BoneMatrix.m14 };
}

/* The head's height in mesh space in the animator's current pose (2 m at rest). */
static float GetHeadHeight(const Figure* figure)
{
    return GetPosePosition(figure, "head").y;
}

/* A bone's socket (WorldAnimation_TryGetBoneWorldMatrix) in the scene, by name. */
static bool GetSocket(TestRun* run, AnimationScene* scene, const Figure* figure, const char* bone, Matrix* outSocket)
{
    uint32_t Index = 0U;
    Matrix4Double Socket;
    if (!TEST_CHECK(run, WorldAnimation_TryFindBone(scene->Animation, figure->Id, (const unsigned char*)bone, &Index))
        || !TEST_CHECK(run, WorldAnimation_TryGetBoneWorldMatrix(scene->Animation, figure->Id, Index, &Socket)))
    {
        return false;
    }
    *outSocket = TestWorld_ToSceneMatrix(&Socket);
    return true;
}

/* Whether a bone's socket is at @p expected in the scene. */
static bool IsSocketAt(TestRun* run, AnimationScene* scene, const Figure* figure, const char* bone, Vector3 expected)
{
    Matrix Socket;
    return GetSocket(run, scene, figure, bone, &Socket)
        && IsNearVector((Vector3){ Socket.m12, Socket.m13, Socket.m14 }, expected);
}

/* A list of asset names (PropertyAssetNameList_Destroy it). */
static PropertyAssetNameList* MakeNames(TestRun* run, const char* const* names, size_t count)
{
    PropertyAssetNameList* List = NULL;
    TEST_CHECK_SUCCESS(run, PropertyAssetNameList_Create((const unsigned char* const*)names, count, &List));
    return List;
}


// Static functions: tests.
static void AnAnimatorBindsOnDemandAndRestsInItsBindPose(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 3.0f, 0.0f, 4.0f }, &Clerk))
    {
        // Nothing is bound before the system looks at the animator; asking for a bone binds it.
        TEST_CHECK(run, WorldAnimatorComponent_GetPose(Clerk.Animator) == NULL);
        TEST_CHECK(run, IsSocketAt(run, &Scene, &Clerk, "head", (Vector3){ 3.0f, 2.0f, 4.0f }));
        const WorldAnimatorPose* Pose = WorldAnimatorComponent_GetPose(Clerk.Animator);
        if (TEST_CHECK(run, (Pose != NULL) && (Pose->_boneCount == (uint32_t)FIGURE_BONE_COUNT)))
        {
            // The rest pose is the bind pose: every bone's skinning matrix is the identity (three rows each).
            bool IsIdentity = true;
            for (uint32_t Entry = 0U; Entry < (Pose->_boneCount * SKELETAL_PALETTE_FLOATS_PER_BONE); Entry++)
            {
                float Expected = (((Entry % SKELETAL_PALETTE_FLOATS_PER_BONE) % 5U) == 0U) ? 1.0f : 0.0f;
                IsIdentity = IsIdentity && IsNear(Pose->_palette[Entry], Expected);
            }
            TEST_CHECK(run, IsIdentity);
            // The bounds hold every bone's box: the hand's reaches 1.05 m to the side, the head's 2.05 m up.
            TEST_CHECK(run, Pose->_hasBounds && IsNearVector(Pose->_bounds.min, (Vector3){ -0.05f, -0.05f, -0.05f })
                && IsNearVector(Pose->_bounds.max, (Vector3){ 1.05f, 2.05f, 0.05f }));

            // A resting animator is counted but not posed again.
            uint32_t Revision = Pose->_revision;
            Tick(&Scene, 0.1f);
            Tick(&Scene, 0.1f);
            WorldAnimationStatus Status = WorldAnimation_GetStatus(Scene.Animation);
            TEST_CHECK(run, (Status.AnimatorCount == 1U) && (Status.PosedCount == 1U) && (Status.ClipCount == 0U)
                && (Status.BoneCount == 0U) && (Status.UpdateMicroseconds >= 0.0));
            TEST_CHECK(run, (Pose->_revision == Revision) && IsNear(GetHeadHeight(&Clerk), 2.0f));
        }
    }
    Scene_Deconstruct(run, &Scene);
}

static void TheClipRowPlaysAtOnceFromItsPhase(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "lift", 0.5f)
        && TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetPhase(Clerk.Animator, 0.25f)))
    {
        // The first update binds and starts the clip at a quarter of its length, at full weight (no fade from the rest
        // pose), then advances it.
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.75f));
        WorldAnimationStatus Status = WorldAnimation_GetStatus(Scene.Animation);
        TEST_CHECK(run, (Status.ClipCount == 1U) && (Status.BoneCount == (size_t)FIGURE_BONE_COUNT));
        const WorldAnimatorPose* Pose = WorldAnimatorComponent_GetPose(Clerk.Animator);
        uint32_t Revision = (Pose != NULL) ? Pose->_revision : 0U;
        // lift does not loop: it holds its last key, and the pose's bounds follow the head up.
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 3.0f));
        TEST_CHECK(run, (Pose != NULL) && (Pose->_revision != Revision));
        TEST_CHECK(run, (Pose != NULL) && IsNear(Pose->_bounds.max.y, 3.05f) && IsNear(Pose->_bounds.min.y, -0.05f));
        // The skinning matrix of the head moves its vertices 1 m up: its rows' translations (the 4th, 8th, 12th).
        uint32_t Head = 0U;
        if ((Pose != NULL) && TEST_CHECK(run, GameSkeleton_TryFindBone(Pose->_skeleton, (const unsigned char*)u8"head",
            &Head)))
        {
            const float* Rows = Pose->_palette + (Head * SKELETAL_PALETTE_FLOATS_PER_BONE);
            TEST_CHECK(run, IsNear(Rows[3], 0.0f) && IsNear(Rows[7], 1.0f) && IsNear(Rows[11], 0.0f));
        }
    }
    Scene_Deconstruct(run, &Scene);
}

static void PosesWaitForThePoseStepAndComeOncePerChange(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "lift", 0.0f))
    {
        // The update binds (publishing the rest pose at once) and advances the clip; the pose waits for the pose step.
        WorldAnimation_Update(Scene.Animation, 0.5f);
        const WorldAnimatorPose* Pose = WorldAnimatorComponent_GetPose(Clerk.Animator);
        uint32_t Revision = (Pose != NULL) ? Pose->_revision : 0U;
        TEST_CHECK(run, (Pose != NULL) && IsNear(GetHeadHeight(&Clerk), 2.0f));
        // Two ticks between frames: the pose step shows the playback after the last of them, computed once.
        WorldAnimation_Update(Scene.Animation, 0.25f);
        WorldAnimation_Pose(Scene.Animation);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.75f));
        WorldAnimationStatus Status = WorldAnimation_GetStatus(Scene.Animation);
        TEST_CHECK(run, (Status.RecomputedCount == 1U) && (Status.BoneCount == (size_t)FIGURE_BONE_COUNT)
            && (Status.PoseMicroseconds >= 0.0));
        TEST_CHECK(run, (Pose != NULL) && (Pose->_revision != Revision));
        // A second pose step with nothing changed computes nothing.
        Revision = (Pose != NULL) ? Pose->_revision : 0U;
        WorldAnimation_Pose(Scene.Animation);
        TEST_CHECK(run, (WorldAnimation_GetStatus(Scene.Animation).RecomputedCount == 0U)
            && (Pose != NULL) && (Pose->_revision == Revision));
        // Code playing a clip shows at the next pose step, without waiting for an update.
        TEST_CHECK_SUCCESS(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, (const unsigned char*)u8"down", 0.0f,
            false));
        WorldAnimation_Pose(Scene.Animation);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 1.0f) && (Pose != NULL) && (Pose->_revision != Revision));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ANewClipRowCrossFadesOverItsFadeTime(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "up", 1.0f))
    {
        Tick(&Scene, 0.1f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 3.0f));
        // Halfway through the fade the spine is halfway between up and down: at its rest.
        if (TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetClip(Clerk.Animator, (const unsigned char*)u8"down")))
        {
            Tick(&Scene, 0.5f);
            TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.0f));
            TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).ClipCount == 2U);
            Tick(&Scene, 0.5f);
            TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 1.0f));
            // up faded out and stopped.
            TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).ClipCount == 1U);
        }
    }
    Scene_Deconstruct(run, &Scene);
}

static void TurningTheClipRowOffFadesBackToTheRestPose(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "up", 0.5f))
    {
        Tick(&Scene, 0.1f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 3.0f));
        if (TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetPlaysClip(Clerk.Animator, false)))
        {
            // Half faded out: the rest pose fills the other half.
            Tick(&Scene, 0.25f);
            TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.5f));
            Tick(&Scene, 0.25f);
            WorldAnimationStatus Status = WorldAnimation_GetStatus(Scene.Animation);
            TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.0f) && (Status.ClipCount == 0U)
                && (Status.BoneCount == (size_t)FIGURE_BONE_COUNT));
            // Back at rest, it is not posed again.
            Tick(&Scene, 0.25f);
            TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).BoneCount == 0U);
        }
    }
    Scene_Deconstruct(run, &Scene);
}

static void SpeedAndPauseScaleTheClipsTime(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "lift", 0.2f)
        && TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetSpeed(Clerk.Animator, 0.5f)))
    {
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.25f));
        // Paused, nothing moves; a speed change and a resume take effect on the next update, without a fade.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetPaused(Clerk.Animator, true));
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.25f));
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetPaused(Clerk.Animator, false));
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetSpeed(Clerk.Animator, 2.0f));
        Tick(&Scene, 0.25f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.75f));
        // Speed 0 holds the pose.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetSpeed(Clerk.Animator, 0.0f));
        Tick(&Scene, 0.25f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.75f));
        // No time passes for a tick of 0 s, a negative one or one that is not a number.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetSpeed(Clerk.Animator, 1.0f));
        Tick(&Scene, 0.0f);
        Tick(&Scene, -1.0f);
        Tick(&Scene, NAN);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.75f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void TheLibrarysLoopAndSpeedApplyAndTheRowOverridesTheLoop(TestRun* run)
{
    AnimationScene Scene;
    Figure Bobber;
    Figure Lifter;
    if (Scene_Construct(run, &Scene, NULL)
        && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Bobber)
        && AddFigure(run, &Scene, "figure", (Vector3){ 2.0f, 0.0f, 0.0f }, &Lifter)
        && PlayRow(run, &Bobber, "bob", 0.0f) && PlayRow(run, &Lifter, "lift", 0.0f)
        && TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetLoopOverride(Lifter.Animator, true, true)))
    {
        // bob plays at twice its speed (its definition's setting): 0.125 s is a quarter of it, 0.25 m up.
        Tick(&Scene, 0.125f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Bobber), 2.25f));
        // Looped: a whole length later it is there again.
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Bobber), 2.25f));
        // lift does not loop in its library, but the row loops it: 1.125 s in is 0.125 s in.
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Lifter), 2.125f));

        // The row's override stops bob's loop, as it plays: it runs to its end and holds it.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetLoopOverride(Bobber.Animator, true, false));
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Bobber), 2.0f));
        Tick(&Scene, 0.125f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Bobber), 2.0f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ProblemsAreReportedOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, ANIMATION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "world_animation_problems", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }

    AnimationScene Scene;
    Figure Unknown;
    Figure Statue;
    Figure Ghost;
    Figure Foreigner;
    Figure Lost;
    static const char* const LostNames[] = { "lost" };
    if (Scene_Construct(run, &Scene, &Log)
        && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Unknown)
        && AddFigure(run, &Scene, "statue", (Vector3){ 2.0f, 0.0f, 0.0f }, &Statue)
        && AddFigure(run, &Scene, "ghost", (Vector3){ 4.0f, 0.0f, 0.0f }, &Ghost)
        && AddFigure(run, &Scene, "foreigner", (Vector3){ 6.0f, 0.0f, 0.0f }, &Foreigner)
        && AddFigure(run, &Scene, "figure", (Vector3){ 8.0f, 0.0f, 0.0f }, &Lost)
        && PlayRow(run, &Unknown, "nowhere", 0.0f) && PlayRow(run, &Statue, "lift", 0.0f)
        && PlayRow(run, &Foreigner, "spin", 0.0f) && PlayRow(run, &Lost, "lift", 0.0f))
    {
        PropertyAssetNameList* Names = MakeNames(run, LostNames, 1U);
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationLibraries(Lost.Animator, true, Names));
        PropertyAssetNameList_Destroy(Names);
        for (int Step = 0; Step < 3; Step++)
        {
            Tick(&Scene, 0.1f);
        }
        // What has a skeleton rests in its bind pose; the rest has no pose.
        TEST_CHECK(run, IsNear(GetHeadHeight(&Unknown), 2.0f) && IsNear(GetHeadHeight(&Lost), 2.0f));
        TEST_CHECK(run, (WorldAnimatorComponent_GetPose(Statue.Animator) == NULL)
            && (WorldAnimatorComponent_GetPose(Ghost.Animator) == NULL));
        // spin moves no bone of the figure: it plays, and leaves it at rest.
        TEST_CHECK(run, IsNear(GetHeadHeight(&Foreigner), 2.0f));
        TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).PosedCount == 3U);
        // Code is told what the rows are not.
        TEST_CHECK_ERROR(run, WorldAnimation_PlayClip(Scene.Animation, Statue.Id, (const unsigned char*)u8"lift", 0.0f,
            false), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, WorldAnimation_PlayClip(Scene.Animation, Ghost.Id, (const unsigned char*)u8"lift", 0.0f,
            false), ErrorCode_InvalidOperation);
        // Another unknown clip is a new problem: reported again, once.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetClip(Unknown.Animator, (const unsigned char*)u8"elsewhere"));
        Tick(&Scene, 0.1f);
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetPhase(Unknown.Animator, 0.5f));
        Tick(&Scene, 0.1f);
    }
    Scene_Deconstruct(run, &Scene);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "has no clip \"nowhere\"") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "has no clip \"elsewhere\"") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "world model \"tbp:statue\" has no skinned LOD") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "world model \"tbp:ghost\" could not be loaded") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "plays nothing from animation library \"tbp:foreign\"") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "plays without animation library \"tbp:lost\"") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "has no clip \"lift\"") == 1U);
    Memory_Free(Directory._data);
}

static void CodePlaysClipsOnTopOfTheRows(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    WorldObject* Plain = NULL;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "up", 0.0f)
        && TEST_CHECK_SUCCESS(run, World_CreateObject(&Scene.World, (const unsigned char*)u8"plain", &Plain)))
    {
        const unsigned char* Down = (const unsigned char*)u8"down";
        TEST_CHECK_ERROR(run, WorldAnimation_PlayClip(Scene.Animation, WorldObject_GetId(Plain), Down, 0.0f, false),
            ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, WorldAnimation_StopClips(Scene.Animation, WorldObject_GetId(Plain), 0.0f),
            ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, (const unsigned char*)u8"nowhere", 0.0f,
            false), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimation_SetClipWeight(Scene.Animation, Clerk.Id, (const unsigned char*)u8"nowhere",
            1.0f, 0.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, NULL, 0.0f, false),
            ErrorCode_IllegalArgument);
        TEST_CHECK(run, !WorldAnimation_TryFindBone(Scene.Animation, WorldObject_GetId(Plain),
            (const unsigned char*)u8"head", NULL));
        TEST_CHECK(run, !WorldAnimation_TryFindBone(Scene.Animation, Clerk.Id, (const unsigned char*)u8"tail", NULL));
        TEST_CHECK(run, !WorldAnimation_TryGetBoneWorldMatrix(Scene.Animation, Clerk.Id, (uint32_t)FIGURE_BONE_COUNT,
            &(Matrix4Double){ 0 }));

        // Played before the first update, the clip is not undone by the rows: they applied when the animator bound.
        TEST_CHECK_SUCCESS(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, Down, 0.0f, false));
        Tick(&Scene, 0.1f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 1.0f));
        // One clip's weight alone: down 1 and up 0.5 add up to 1.5, divided out.
        TEST_CHECK_SUCCESS(run, WorldAnimation_SetClipWeight(Scene.Animation, Clerk.Id, (const unsigned char*)u8"up", 0.5f,
            0.0f));
        Tick(&Scene, 0.1f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 1.0f + (2.0f / 3.0f)));
        TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).ClipCount == 2U);
        // Stopped over 0.5 s: after 0.25 s down weighs 0.5 and up 0.25, and the rest pose fills the last quarter.
        TEST_CHECK_SUCCESS(run, WorldAnimation_StopClips(Scene.Animation, Clerk.Id, 0.5f));
        Tick(&Scene, 0.25f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 1.0f + ((0.25f * 2.0f) + (0.25f * 1.0f))));
        Tick(&Scene, 0.25f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.0f));
        // The rows apply again when they change: a new clip row plays.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetClip(Clerk.Animator, (const unsigned char*)u8"lift"));
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.5f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ARowSwitchingToAPlayingClipKeepsItsTime(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "up", 0.0f))
    {
        Tick(&Scene, 0.1f);
        TEST_CHECK_SUCCESS(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, (const unsigned char*)u8"lift", 0.0f,
            false));
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.5f));
        // The row now names the clip code plays: it goes on from 0.5 s (a restart from the row's phase would jump).
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetClip(Clerk.Animator, (const unsigned char*)u8"lift"));
        Tick(&Scene, 0.25f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.75f));
        // Code restarts it on request.
        TEST_CHECK_SUCCESS(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, (const unsigned char*)u8"lift", 0.0f,
            true));
        Tick(&Scene, 0.125f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.125f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void OverridingTheLibrariesReplacesTheWorldModels(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    static const char* const GestureNames[] = { "gestures" };
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "wave", 0.0f))
    {
        PropertyAssetNameList* Names = MakeNames(run, GestureNames, 1U);
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationLibraries(Clerk.Animator, true, Names));
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNearVector(GetPosePosition(&Clerk, "hand"), (Vector3){ 1.0f, 2.0f, 0.0f }));
        TEST_CHECK_ERROR(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, (const unsigned char*)u8"lift", 0.0f,
            false), ErrorCode_ArgumentOutOfRange);

        // Another list while overriding rebinds to it: motions has lift (and no wave).
        static const char* const MotionNames[] = { "motions" };
        PropertyAssetNameList* Motions = MakeNames(run, MotionNames, 1U);
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationLibraries(Clerk.Animator, true, Motions));
        PropertyAssetNameList_Destroy(Motions);
        TEST_CHECK_SUCCESS(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, (const unsigned char*)u8"lift", 0.0f,
            false));
        TEST_CHECK(run, IsNearVector(GetPosePosition(&Clerk, "hand"), (Vector3){ 1.0f, 1.5f, 0.0f }));
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationLibraries(Clerk.Animator, true, Names));
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNearVector(GetPosePosition(&Clerk, "hand"), (Vector3){ 1.0f, 2.0f, 0.0f }));

        // Off again (the list kept): the world model's libraries play. A rebind starts from the rest pose.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationLibraries(Clerk.Animator, false, Names));
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNearVector(GetPosePosition(&Clerk, "hand"), (Vector3){ 1.0f, 1.5f, 0.0f }));
        TEST_CHECK_SUCCESS(run, WorldAnimation_PlayClip(Scene.Animation, Clerk.Id, (const unsigned char*)u8"lift", 0.0f,
            false));
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.5f));
        // A list without the override changes nothing.
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationLibraries(Clerk.Animator, false, NULL));
        Tick(&Scene, 0.25f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.75f));
        PropertyAssetNameList_Destroy(Names);
    }
    Scene_Deconstruct(run, &Scene);
}

static void ANewWorldModelRebindsFromTheRestPose(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 3.0f, 0.0f, 4.0f }, &Clerk)
        && PlayRow(run, &Clerk, "lift", 0.0f))
    {
        Tick(&Scene, 0.5f);
        const WorldAnimatorPose* Pose = WorldAnimatorComponent_GetPose(Clerk.Animator);
        const WorldModel* Before = (Pose != NULL) ? Pose->_worldModel : NULL;
        TEST_CHECK(run, (Before != NULL) && IsNear(GetHeadHeight(&Clerk), 2.5f));

        // The tall figure: the same bones, the rows applied again from the start (lift at 0), drawn twice as big and
        // raised 0.5 m by its import transform, which the sockets include.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(Clerk.ModelComponent,
            (const unsigned char*)u8"tall_figure"));
        Tick(&Scene, 0.25f);
        Pose = WorldAnimatorComponent_GetPose(Clerk.Animator);
        TEST_CHECK(run, (Pose != NULL) && (Pose->_worldModel != NULL) && (Pose->_worldModel != Before));
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.25f));
        TEST_CHECK(run, IsSocketAt(run, &Scene, &Clerk, "head", (Vector3){ 3.0f, (2.25f * 2.0f) + 0.5f, 4.0f }));

        // No world model: nothing to pose.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(Clerk.ModelComponent, NULL));
        Tick(&Scene, 0.25f);
        TEST_CHECK(run, WorldAnimatorComponent_GetPose(Clerk.Animator) == NULL);
        TEST_CHECK(run, !WorldAnimation_TryFindBone(Scene.Animation, Clerk.Id, (const unsigned char*)u8"head", NULL));
        TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).PosedCount == 0U);
    }
    Scene_Deconstruct(run, &Scene);
}

static void SocketsCarryTheObjectsTransformInDouble(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    Figure Bare;
    if (Scene_Construct(run, &Scene, NULL)
        && AddFigure(run, &Scene, "tall_figure", (Vector3){ 3.0f, 0.0f, 4.0f }, &Clerk)
        && AddFigure(run, &Scene, NULL, (Vector3){ 5.0f, 0.0f, 0.0f }, &Bare)
        && TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Clerk.Object, (Vector3){ 0.0f, PI * 0.5f, 0.0f }))
        && PlayRow(run, &Clerk, "lift", 0.0f)
        && TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetPhase(Clerk.Animator, 0.5f)))
    {
        Tick(&Scene, 0.0f);
        // The hand: (1, 2, 0) in mesh space at lift's middle, then the import transform (twice as big, 0.5 m up):
        // (2, 4.5, 0), then the object's quarter turn about y: (0, 4.5, -2), then its place.
        Matrix Hand;
        if (GetSocket(run, &Scene, &Clerk, "hand", &Hand))
        {
            TEST_CHECK(run, IsNearVector((Vector3){ Hand.m12, Hand.m13, Hand.m14 }, (Vector3){ 3.0f, 4.5f, 2.0f }));
            // Its x axis: the bone's own, doubled by the import transform, turned to -z.
            TEST_CHECK(run, IsNearVector((Vector3){ Hand.m0, Hand.m1, Hand.m2 }, (Vector3){ 0.0f, 0.0f, -2.0f }));
        }
        TEST_CHECK(run, IsSocketAt(run, &Scene, &Clerk, "head", (Vector3){ 3.0f, (2.5f * 2.0f) + 0.5f, 4.0f }));
        // The object moves: the socket follows without an update of the animation (it reads the world matrix).
        if (TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Clerk.Object, TestWorld_At((Vector3){ 3.0f, 1.0f, 4.0f }))))
        {
            TEST_CHECK(run, IsSocketAt(run, &Scene, &Clerk, "hand", (Vector3){ 3.0f, 5.5f, 2.0f }));
        }
        // An animator without a model component binds to nothing.
        TEST_CHECK(run, !WorldAnimation_TryFindBone(Scene.Animation, Bare.Id, (const unsigned char*)u8"hand", NULL)
            && (WorldAnimatorComponent_GetPose(Bare.Animator) == NULL));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ADisabledObjectKeepsItsPoseAndItsRowsWait(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && PlayRow(run, &Clerk, "lift", 0.0f))
    {
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.5f));
        if (TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Clerk.Object, false)))
        {
            Tick(&Scene, 0.25f);
            TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.5f));
            WorldAnimationStatus Status = WorldAnimation_GetStatus(Scene.Animation);
            TEST_CHECK(run, (Status.AnimatorCount == 1U) && (Status.PosedCount == 0U));
            // A row changed meanwhile applies once the object is enabled again.
            TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetClip(Clerk.Animator, (const unsigned char*)u8"up"));
            Tick(&Scene, 0.25f);
            TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.5f));
            TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Clerk.Object, true));
            Tick(&Scene, 0.25f);
            TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 3.0f));
        }
    }
    Scene_Deconstruct(run, &Scene);
}

static void TheSystemLetsGoOfEveryAnimator(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    Figure Removed;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk)
        && AddFigure(run, &Scene, "figure", (Vector3){ 2.0f, 0.0f, 0.0f }, &Removed)
        && PlayRow(run, &Clerk, "lift", 0.0f) && PlayRow(run, &Removed, "lift", 0.0f))
    {
        Tick(&Scene, 0.5f);
        TEST_CHECK(run, (Clerk.Animator->_runtime != NULL) && (Removed.Animator->_runtime != NULL));
        // An animator removed with its state frees it (the component owns it).
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Scene.World, Removed.Id, WorldComponentType_Animator));
        Tick(&Scene, 0.25f);
        TEST_CHECK(run, WorldAnimation_GetStatus(Scene.Animation).AnimatorCount == 1U);

        // Destroyed, the system leaves no state or pose behind; a new one binds again, from the rows.
        WorldAnimation_Destroy(Scene.Animation);
        Scene.Animation = NULL;
        TEST_CHECK(run, (Clerk.Animator->_runtime == NULL) && (WorldAnimatorComponent_GetPose(Clerk.Animator) == NULL));
        if (TEST_CHECK_SUCCESS(run, WorldAnimation_Create(&Scene.World, Scene.Assets.Manager, NULL, &Scene.Animation)))
        {
            Tick(&Scene, 0.25f);
            TEST_CHECK(run, IsNear(GetHeadHeight(&Clerk), 2.25f));
        }
        // A system without assets binds nothing.
        WorldAnimation_Destroy(Scene.Animation);
        Scene.Animation = NULL;
        if (TEST_CHECK_SUCCESS(run, WorldAnimation_Create(&Scene.World, NULL, NULL, &Scene.Animation)))
        {
            Tick(&Scene, 0.25f);
            TEST_CHECK(run, (WorldAnimatorComponent_GetPose(Clerk.Animator) == NULL)
                && (WorldAnimation_GetStatus(Scene.Animation).PosedCount == 0U));
        }
        TEST_CHECK_ERROR(run, WorldAnimation_Create(NULL, NULL, NULL, &(WorldAnimation*){ NULL }),
            ErrorCode_IllegalArgument);
    }
    Scene_Deconstruct(run, &Scene);
}

static void TheAnimatorRowsAreChecked(TestRun* run)
{
    AnimationScene Scene;
    Figure Clerk;
    if (Scene_Construct(run, &Scene, NULL) && AddFigure(run, &Scene, "figure", (Vector3){ 0.0f, 0.0f, 0.0f }, &Clerk))
    {
        WorldAnimatorComponent* Animator = Clerk.Animator;
        // The defaults: nothing plays from the rows, at speed 1, cross-fading over 0.2 s, without overrides.
        TEST_CHECK(run, !WorldAnimatorComponent_PlaysClip(Animator) && (WorldAnimatorComponent_GetClip(Animator) == NULL)
            && (WorldAnimatorComponent_GetPhase(Animator) == 0.0f) && (WorldAnimatorComponent_GetSpeed(Animator) == 1.0f)
            && !WorldAnimatorComponent_IsPaused(Animator)
            && (WorldAnimatorComponent_GetFadeSeconds(Animator) == WORLD_ANIMATOR_DEFAULT_FADE_SECONDS)
            && !WorldAnimatorComponent_OverridesIsLooped(Animator) && !WorldAnimatorComponent_IsLooped(Animator)
            && !WorldAnimatorComponent_OverridesAnimationLibraries(Animator)
            && (WorldAnimatorComponent_GetAnimationLibraries(Animator) == NULL));

        uint32_t Revision = WorldComponent_GetRevision(&Animator->Base);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetPhase(Animator, 1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetPhase(Animator, -0.01f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetPhase(Animator, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetSpeed(Animator, WORLD_ANIMATOR_MAX_SPEED + 1.0f),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetSpeed(Animator, -0.5f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetFadeSeconds(Animator, WORLD_ANIMATOR_MAX_FADE_SECONDS + 1.0f),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetFadeSeconds(Animator, -0.1f), ErrorCode_ArgumentOutOfRange);
        char Long[WORLD_ANIMATOR_CLIP_NAME_MAX_LENGTH + 2U];
        memset(Long, 'a', sizeof(Long) - 1U);
        Long[sizeof(Long) - 1U] = '\0';
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetClip(Animator, (const unsigned char*)Long),
            ErrorCode_ArgumentOutOfRange);
        // Refused changes change nothing.
        TEST_CHECK(run, (WorldComponent_GetRevision(&Animator->Base) == Revision)
            && (WorldAnimatorComponent_GetPhase(Animator) == 0.0f) && (WorldAnimatorComponent_GetClip(Animator) == NULL));
        Long[sizeof(Long) - 2U] = '\0';
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetClip(Animator, (const unsigned char*)Long));
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetPhase(Animator, 0.999f));
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetSpeed(Animator, WORLD_ANIMATOR_MAX_SPEED));
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetFadeSeconds(Animator, WORLD_ANIMATOR_MAX_FADE_SECONDS));
        TEST_CHECK(run, WorldComponent_GetRevision(&Animator->Base) != Revision);

        // At most WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES libraries, each a valid asset name.
        char Texts[WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES + 1U][16];
        const char* Many[WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES + 1U];
        for (size_t Index = 0U; Index < (WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES + 1U); Index++)
        {
            snprintf(Texts[Index], sizeof(Texts[Index]), "tbp:library_%u", (unsigned int)Index);
            Many[Index] = Texts[Index];
        }
        PropertyAssetNameList* TooMany = MakeNames(run, Many, WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES + 1U);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetAnimationLibraries(Animator, true, TooMany),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, !WorldAnimatorComponent_OverridesAnimationLibraries(Animator));
        PropertyAssetNameList_Destroy(TooMany);
        static const char* const Bad[] = { "Not A Name" };
        PropertyAssetNameList* BadList = MakeNames(run, Bad, 1U);
        TEST_CHECK_ERROR(run, WorldAnimatorComponent_SetAnimationLibraries(Animator, true, BadList),
            ErrorCode_ArgumentOutOfRange);
        PropertyAssetNameList_Destroy(BadList);
        PropertyAssetNameList* Enough = MakeNames(run, Many, WORLD_ANIMATOR_MAX_ANIMATION_LIBRARIES);
        TEST_CHECK_SUCCESS(run, WorldAnimatorComponent_SetAnimationLibraries(Animator, true, Enough));
        TEST_CHECK(run, WorldAnimatorComponent_OverridesAnimationLibraries(Animator)
            && PropertyAssetNameList_Equals(WorldAnimatorComponent_GetAnimationLibraries(Animator), Enough));
        PropertyAssetNameList_Destroy(Enough);
    }
    Scene_Deconstruct(run, &Scene);
}


// Public functions.
void WorldAnimationTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "an_animator_binds_on_demand_and_rests_in_its_bind_pose", AnAnimatorBindsOnDemandAndRestsInItsBindPose },
        { "the_clip_row_plays_at_once_from_its_phase", TheClipRowPlaysAtOnceFromItsPhase },
        { "poses_wait_for_the_pose_step_and_come_once_per_change", PosesWaitForThePoseStepAndComeOncePerChange },
        { "a_new_clip_row_cross_fades_over_its_fade_time", ANewClipRowCrossFadesOverItsFadeTime },
        { "turning_the_clip_row_off_fades_back_to_the_rest_pose", TurningTheClipRowOffFadesBackToTheRestPose },
        { "speed_and_pause_scale_the_clips_time", SpeedAndPauseScaleTheClipsTime },
        { "the_librarys_loop_and_speed_apply_and_the_row_overrides_the_loop",
            TheLibrarysLoopAndSpeedApplyAndTheRowOverridesTheLoop },
        { "problems_are_reported_once", ProblemsAreReportedOnce },
        { "code_plays_clips_on_top_of_the_rows", CodePlaysClipsOnTopOfTheRows },
        { "a_row_switching_to_a_playing_clip_keeps_its_time", ARowSwitchingToAPlayingClipKeepsItsTime },
        { "overriding_the_libraries_replaces_the_world_models", OverridingTheLibrariesReplacesTheWorldModels },
        { "a_new_world_model_rebinds_from_the_rest_pose", ANewWorldModelRebindsFromTheRestPose },
        { "sockets_carry_the_objects_transform_in_double", SocketsCarryTheObjectsTransformInDouble },
        { "a_disabled_object_keeps_its_pose_and_its_rows_wait", ADisabledObjectKeepsItsPoseAndItsRowsWait },
        { "the_system_lets_go_of_every_animator", TheSystemLetsGoOfEveryAnimator },
        { "the_animator_rows_are_checked", TheAnimatorRowsAreChecked },
    };
    TestWorld_RunSuite(run, "WorldAnimation", "WorldAnimationFar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
