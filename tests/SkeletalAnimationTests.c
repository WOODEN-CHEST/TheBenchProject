#include "TestSuites.h"
#include "TestAnimation.h"
#include "AnimationLibrary.h"
#include "GameSkeleton.h"
#include "SkeletalAnimation.h"
#include "raylib/raymath.h"
#include "wr/WRRandom.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for matrix and pose comparisons. */
#define TEST_EPSILON 1e-5f
/** Room for paths. */
#define TEST_BUFFER_CAPACITY 512U
/** The shipped placeholder motions (asset_source/scripts/placeholder_humanoid.py writes them). */
#define MOTIONS_PATH "compile/out/asset/animation_libraries/placeholder_humanoid/motions.glb"
/** The most bones a test skeleton here has. */
#define TEST_MAX_BONES 32U


// Types.
/* A code-built skeleton model with its skeleton, made from a bone list. */
typedef struct TestSkeletonStruct
{
    Model Model;
    GameSkeleton* Skeleton;
} TestSkeleton;


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static bool IsNearVector(Vector3 a, Vector3 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y) && IsNear(a.z, b.z);
}

static bool IsSameRotation(Quaternion a, Quaternion b)
{
    float Dot = (a.x * b.x) + (a.y * b.y) + (a.z * b.z) + (a.w * b.w);
    return fabsf(fabsf(Dot) - 1.0f) < TEST_EPSILON;
}

static bool IsNearMatrix(Matrix a, Matrix b)
{
    const float* A = &a.m0;
    const float* B = &b.m0;
    for (int Index = 0; Index < 16; Index++)
    {
        if (fabsf(A[Index] - B[Index]) > TEST_EPSILON)
        {
            return false;
        }
    }
    return true;
}

static Quaternion Turn(float x, float y, float z, float degrees)
{
    return QuaternionFromAxisAngle(Vector3Normalize((Vector3){ x, y, z }), degrees * DEG2RAD);
}

static bool BuildSkeleton(TestRun* run, const TestSkeletonBone* bones, int boneCount, TestSkeleton* out)
{
    out->Model = TestSkeletonModel_Create(bones, boneCount, 0.2f);
    out->Skeleton = NULL;
    return TEST_CHECK_SUCCESS(run, GameSkeleton_Create(&out->Model, (const unsigned char*)u8"test model", NULL,
        &out->Skeleton)) && TEST_CHECK(run, out->Skeleton != NULL);
}

static void DestroySkeleton(TestSkeleton* skeleton)
{
    GameSkeleton_Destroy(skeleton->Skeleton);
    TestSkeletonModel_Destroy(&skeleton->Model);
}

static bool ReadMotions(TestRun* run, AnimationLibrary** outLibrary)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    AnimationLibraryReadOptions Options = { .Clips = NULL, .ClipCount = 0U, .FilePath = NULL, .Logger = NULL,
        .Description = NULL };
    bool IsRead = TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, MOTIONS_PATH, &Path))
        && TEST_CHECK_SUCCESS(run, AnimationLibrary_CreateFromFile(Path._data, &Options, outLibrary));
    Memory_Free(Path._data);
    return IsRead;
}

/* A skeleton bound as the library's own: its bones (all but the armature above them), each placed where the library
 * rests it, scaled by @p scale (1, 1, 1: the library's own figure). Names point into the library. */
static int MakeBonesFromLibrary(const AnimationLibrary* library, Vector3 scale, TestSkeletonBone* outBones)
{
    int Count = 0;
    int32_t SkeletonOf[TEST_MAX_BONES];
    for (uint32_t Index = 0U; Index < AnimationLibrary_GetBoneCount(library); Index++)
    {
        const AnimationLibraryBone* Bone = AnimationLibrary_GetBone(library, Index);
        if (Bone->_parent < 0)
        {
            SkeletonOf[Index] = -1;
            continue;
        }
        SkeletonOf[Index] = Count;
        Vector3 Position = Vector3Multiply(Bone->_restPosition, scale);
        outBones[Count] = (TestSkeletonBone){ .Name = (const char*)Bone->_name,
            .Parent = SkeletonOf[Bone->_parent], .Bind = TestAnimation_Translation(Position.x, Position.y, Position.z) };
        Count++;
    }
    return Count;
}


// Static functions: transforms.
static void ComposeAndDecomposeRoundTrip(TestRun* run)
{
    Transform Cases[] =
    {
        TestAnimation_Identity(),
        { { 1.0f, -2.0f, 3.0f }, Turn(1.0f, 2.0f, 3.0f, 70.0f), { 1.5f, 1.5f, 1.5f } },
        // A non-uniform scale under a turn (raymath's own decomposition reads this as (1, 2, 1)).
        { { 0.0f, 0.0f, 0.0f }, Turn(0.0f, 0.0f, 1.0f, 90.0f), { 2.0f, 1.0f, 1.0f } },
        { { 4.0f, 5.0f, 6.0f }, Turn(-1.0f, 0.5f, 0.2f, 130.0f), { 0.3f, 2.0f, 0.7f } },
    };
    for (size_t Index = 0U; Index < sizeof(Cases) / sizeof(Cases[0]); Index++)
    {
        Matrix Composed = SkeletalAnimation_ComposeTransform(Cases[Index]);
        Transform Back = SkeletalAnimation_DecomposeMatrix(Composed);
        TEST_CHECK(run, IsNearVector(Back.translation, Cases[Index].translation));
        TEST_CHECK(run, IsNearVector(Back.scale, Cases[Index].scale));
        TEST_CHECK(run, IsSameRotation(Back.rotation, Cases[Index].rotation));
        TEST_CHECK(run, IsNearMatrix(SkeletalAnimation_ComposeTransform(Back), Composed));
        // The composition is raymath's scale, then rotation, then translation.
        Matrix Raymath = MatrixMultiply(MatrixMultiply(MatrixScale(Cases[Index].scale.x, Cases[Index].scale.y,
            Cases[Index].scale.z), QuaternionToMatrix(Cases[Index].rotation)),
            MatrixTranslate(Cases[Index].translation.x, Cases[Index].translation.y, Cases[Index].translation.z));
        TEST_CHECK(run, IsNearMatrix(Composed, Raymath));
    }
    // A mirror comes back as a negative x scale that composes to the same matrix.
    Matrix Mirror = MatrixMultiply(MatrixScale(-1.0f, 1.0f, 1.0f), MatrixRotateY(0.5f));
    Transform Mirrored = SkeletalAnimation_DecomposeMatrix(Mirror);
    TEST_CHECK(run, Mirrored.scale.x < 0.0f);
    TEST_CHECK(run, IsNearMatrix(SkeletalAnimation_ComposeTransform(Mirrored), Mirror));
    // A zero scale on one axis keeps the others' rotation.
    Transform Flat = { { 0.0f, 0.0f, 0.0f }, Turn(0.0f, 1.0f, 0.0f, 40.0f), { 1.0f, 0.0f, 1.0f } };
    Transform FlatBack = SkeletalAnimation_DecomposeMatrix(SkeletalAnimation_ComposeTransform(Flat));
    TEST_CHECK(run, IsNear(FlatBack.scale.y, 0.0f) && IsSameRotation(FlatBack.rotation, Flat.rotation));
}


// Static functions: skeletons.
static void ASkeletonIsBuiltFromARaylibModel(TestRun* run)
{
    TestSkeletonBone Bones[3] =
    {
        { .Name = "hips", .Parent = -1, .Bind = { { 0.0f, 1.0f, 0.0f }, Turn(0.0f, 1.0f, 0.0f, 90.0f),
            { 1.0f, 1.0f, 1.0f } } },
        { .Name = "spine", .Parent = 0, .Bind = { { 0.0f, 1.5f, 0.0f }, Turn(0.0f, 1.0f, 0.0f, 90.0f),
            { 1.0f, 1.0f, 1.0f } } },
        { .Name = "a_bone_whose_name_is_longer_than_raylib_keeps", .Parent = 1,
            .Bind = { { 0.5f, 1.5f, 0.2f }, Turn(1.0f, 1.0f, 0.0f, 30.0f), { 1.0f, 1.0f, 1.0f } } },
    };
    TestSkeleton Built;
    if (!BuildSkeleton(run, Bones, 3, &Built))
    {
        TestSkeletonModel_Destroy(&Built.Model);
        return;
    }
    const GameSkeleton* Skeleton = Built.Skeleton;
    TEST_CHECK(run, GameSkeleton_GetBoneCount(Skeleton) == 3U);
    TEST_CHECK(run, GameSkeleton_GetSkinnedMeshCount(Skeleton) == 1U && GameSkeleton_IsMeshSkinned(Skeleton, 0U));
    TEST_CHECK(run, !GameSkeleton_IsMeshSkinned(Skeleton, 1U));
    for (uint32_t Index = 0U; Index < 3U; Index++)
    {
        const GameSkeletonBone* Bone = GameSkeleton_GetBone(Skeleton, Index);
        Matrix Bind = SkeletalAnimation_ComposeTransform(Bones[Index].Bind);
        TEST_CHECK(run, IsNearMatrix(Bone->_bind, Bind));
        TEST_CHECK(run, IsNearMatrix(MatrixMultiply(Bone->_bind, Bone->_inverseBind), MatrixIdentity()));
        TEST_CHECK(run, IsNearVector(Bone->_restPosition, Bones[Index].Bind.translation));
        // The rest pose is the bind pose relative to the parent's.
        Matrix Rebuilt = SkeletalAnimation_ComposeTransform(Bone->_rest);
        if (Bone->_parent >= 0)
        {
            Rebuilt = MatrixMultiply(Rebuilt, GameSkeleton_GetBone(Skeleton, (uint32_t)Bone->_parent)->_bind);
        }
        TEST_CHECK(run, IsNearMatrix(Rebuilt, Bind));
        TEST_CHECK(run, Bone->_hasVertices);
    }
    // Names as raylib keeps them: 31 bytes; a longer name finds the bone by those.
    TEST_CHECK(run, strlen((const char*)GameSkeleton_GetBone(Skeleton, 2U)->_name) == 31U);
    uint32_t Found = 0U;
    TEST_CHECK(run, GameSkeleton_TryFindBone(Skeleton, (const unsigned char*)Bones[2].Name, &Found) && (Found == 2U));
    TEST_CHECK(run, GameSkeleton_TryFindBone(Skeleton, (const unsigned char*)u8"spine", &Found) && (Found == 1U));
    TEST_CHECK(run, !GameSkeleton_TryFindBone(Skeleton, (const unsigned char*)u8"spin", NULL));
    TEST_CHECK(run, !GameSkeleton_TryFindBone(Skeleton, (const unsigned char*)u8"", NULL));
    DestroySkeleton(&Built);
}

static void BrokenSkeletonsAreRefused(TestRun* run)
{
    TestSkeletonBone Bones[3] =
    {
        { .Name = "a", .Parent = -1, .Bind = TestAnimation_Identity() },
        { .Name = "b", .Parent = 0, .Bind = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
        { .Name = "c", .Parent = 1, .Bind = TestAnimation_Translation(0.0f, 2.0f, 0.0f) },
    };
    int Parents[][3] = { { -1, 0, 5 }, { -1, 1, 1 }, { 1, 0, 1 }, { -2, 0, 1 } };
    for (size_t Case = 0U; Case < sizeof(Parents) / sizeof(Parents[0]); Case++)
    {
        for (int Index = 0; Index < 3; Index++)
        {
            Bones[Index].Parent = Parents[Case][Index];
        }
        Model Built = TestSkeletonModel_Create(Bones, 3, 0.1f);
        GameSkeleton* Skeleton = NULL;
        TEST_CHECK_ERROR(run, GameSkeleton_Create(&Built, NULL, NULL, &Skeleton), ErrorCode_InvalidAssetData);
        TEST_CHECK(run, Skeleton == NULL);
        TestSkeletonModel_Destroy(&Built);
    }
    // A bind pose that cannot be inverted.
    Bones[0].Parent = -1;
    Bones[1].Parent = 0;
    Bones[2].Parent = 1;
    Bones[1].Bind.scale = (Vector3){ 1.0f, 0.0f, 1.0f };
    Model Singular = TestSkeletonModel_Create(Bones, 3, 0.1f);
    GameSkeleton* Skeleton = NULL;
    TEST_CHECK_ERROR(run, GameSkeleton_Create(&Singular, NULL, NULL, &Skeleton), ErrorCode_InvalidAssetData);
    TestSkeletonModel_Destroy(&Singular);
    // No bones: no skeleton.
    Model Rigid;
    Memory_Zero(&Rigid, sizeof(Rigid));
    TEST_CHECK_SUCCESS(run, GameSkeleton_Create(&Rigid, NULL, NULL, &Skeleton));
    TEST_CHECK(run, Skeleton == NULL);
}

static void MeshesTheGpuCannotSkinDrawAsBound(TestRun* run)
{
    TestSkeletonBone Bones[2] =
    {
        { .Name = "a", .Parent = -1, .Bind = TestAnimation_Identity() },
        { .Name = "b", .Parent = 0, .Bind = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
    };
    for (int Case = 0; Case < 3; Case++)
    {
        Model Built = TestSkeletonModel_Create(Bones, 2, 0.4f);
        Mesh* Only = &Built.meshes[0];
        if (Case == 0)
        {
            // A CPU-skinning raylib keeps a copy of the vertices to skin into.
            Only->animVertices = Memory_Allocate((size_t)Only->vertexCount * 3U * sizeof(float));
            Memory_Copy(Only->vertices, Only->animVertices, (size_t)Only->vertexCount * 3U * sizeof(float));
        }
        else if (Case == 1)
        {
            Memory_Free(Only->vboId);
            Only->vboId = NULL;
        }
        else
        {
            Only->boneIndices[4] = 7U; // a bone the skeleton does not have
        }
        GameSkeleton* Skeleton = NULL;
        if (TEST_CHECK_SUCCESS(run, GameSkeleton_Create(&Built, NULL, NULL, &Skeleton)))
        {
            TEST_CHECK(run, (GameSkeleton_GetSkinnedMeshCount(Skeleton) == 0U) && !GameSkeleton_IsMeshSkinned(Skeleton, 0U));
            // Its vertices do not bend: they count in every pose's bounds as they are.
            TEST_CHECK(run, Skeleton->_hasStaticBounds);
            TEST_CHECK(run, IsNearVector(Skeleton->_staticBounds.min, (Vector3){ -0.1f, -0.1f, -0.1f }));
            TEST_CHECK(run, IsNearVector(Skeleton->_staticBounds.max, (Vector3){ 0.1f, 1.1f, 0.1f }));
            TEST_CHECK(run, !GameSkeleton_GetBone(Skeleton, 0U)->_hasVertices);
            GameSkeleton_Destroy(Skeleton);
        }
        TestSkeletonModel_Destroy(&Built);
    }
}

static void TwoSkeletonsHaveTheSameBonesByNameAndParent(TestRun* run)
{
    TestSkeletonBone First[2] =
    {
        { .Name = "a", .Parent = -1, .Bind = TestAnimation_Identity() },
        { .Name = "b", .Parent = 0, .Bind = TestAnimation_Translation(0.0f, 1.0f, 0.0f) },
    };
    TestSkeletonBone Second[2] =
    {
        { .Name = "a", .Parent = -1, .Bind = TestAnimation_Translation(1.0f, 0.0f, 0.0f) },
        { .Name = "b", .Parent = 0, .Bind = TestAnimation_Translation(1.0f, 2.0f, 0.0f) },
    };
    TestSkeleton A;
    TestSkeleton B;
    bool IsBuilt = BuildSkeleton(run, First, 2, &A) & BuildSkeleton(run, Second, 2, &B);
    if (IsBuilt)
    {
        // Other bind poses (another body) but the same bones.
        TEST_CHECK(run, GameSkeleton_HasSameBones(A.Skeleton, B.Skeleton));
    }
    DestroySkeleton(&B);
    Second[1].Name = "c";
    IsBuilt = BuildSkeleton(run, Second, 2, &B);
    TEST_CHECK(run, IsBuilt && !GameSkeleton_HasSameBones(A.Skeleton, B.Skeleton));
    DestroySkeleton(&B);
    IsBuilt = BuildSkeleton(run, Second, 1, &B);
    TEST_CHECK(run, IsBuilt && !GameSkeleton_HasSameBones(A.Skeleton, B.Skeleton));
    DestroySkeleton(&B);
    DestroySkeleton(&A);
}


// Static functions: poses.
static void TheRestPoseSkinsNothing(TestRun* run)
{
    TestSkeletonBone Bones[3] =
    {
        { .Name = "child", .Parent = 2, .Bind = { { 0.3f, 1.2f, 0.0f }, Turn(1.0f, 0.0f, 0.0f, 20.0f),
            { 1.0f, 1.0f, 1.0f } } },
        { .Name = "other", .Parent = 2, .Bind = TestAnimation_Translation(-0.3f, 1.2f, 0.0f) },
        { .Name = "root", .Parent = -1, .Bind = { { 0.0f, 1.0f, 0.0f }, Turn(0.0f, 1.0f, 0.0f, 45.0f),
            { 1.0f, 1.0f, 1.0f } } },
    };
    TestSkeleton Built;
    if (!BuildSkeleton(run, Bones, 3, &Built))
    {
        TestSkeletonModel_Destroy(&Built.Model);
        return;
    }
    // The parent is listed last: the order still puts it first.
    TEST_CHECK(run, GameSkeleton_GetOrder(Built.Skeleton)[0] == 2U);
    Transform Pose[3];
    Matrix Matrices[3];
    float Palette[3 * SKELETAL_PALETTE_FLOATS_PER_BONE];
    SkeletalPose_SetRest(Built.Skeleton, Pose);
    SkeletalPose_ComputeModelMatrices(Built.Skeleton, Pose, Matrices);
    SkeletalPose_ComputePalette(Built.Skeleton, Matrices, Palette);
    static const float Identity[SKELETAL_PALETTE_FLOATS_PER_BONE] =
    {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f
    };
    for (uint32_t Index = 0U; Index < 3U; Index++)
    {
        TEST_CHECK(run, IsNearMatrix(Matrices[Index], GameSkeleton_GetBone(Built.Skeleton, Index)->_bind));
        bool IsIdentity = true;
        for (uint32_t Entry = 0U; Entry < SKELETAL_PALETTE_FLOATS_PER_BONE; Entry++)
        {
            IsIdentity = IsIdentity && IsNear(Palette[(Index * SKELETAL_PALETTE_FLOATS_PER_BONE) + Entry], Identity[Entry]);
        }
        TEST_CHECK(run, IsIdentity);
    }
    // At rest the bounds are the mesh's own.
    BoundingBox Bounds;
    TEST_CHECK(run, SkeletalPose_ComputeBounds(Built.Skeleton, Palette, &Bounds));
    TEST_CHECK(run, IsNearVector(Bounds.min, (Vector3){ -0.35f, 0.95f, -0.05f }));
    TEST_CHECK(run, IsNearVector(Bounds.max, (Vector3){ 0.35f, 1.25f, 0.05f }));
    DestroySkeleton(&Built);
}

static void BlendsAddPosesByWeight(TestRun* run)
{
    TestSkeletonBone Bones[1] = { { .Name = "only", .Parent = -1, .Bind = TestAnimation_Identity() } };
    TestSkeleton Built;
    if (!BuildSkeleton(run, Bones, 1, &Built))
    {
        TestSkeletonModel_Destroy(&Built.Model);
        return;
    }
    Transform A = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
    // B in the far hemisphere (-q is the same quarter turn about X): the blend turns it over.
    Quaternion Quarter = Turn(1.0f, 0.0f, 0.0f, 90.0f);
    Transform B = { { 3.0f, 0.0f, 0.0f }, QuaternionScale(Quarter, -1.0f), { 2.0f, 2.0f, 2.0f } };
    SkeletalBlendBone Blend[1];
    Transform Result[1];

    SkeletalPose_ClearBlend(Blend, 1U);
    SkeletalPose_AddToBlend(Blend, &A, 1U, 0.5f);
    SkeletalPose_AddToBlend(Blend, &B, 1U, 0.5f);
    SkeletalPose_FinishBlend(Blend, Built.Skeleton, 1.0f, Result);
    TEST_CHECK(run, IsNearVector(Result[0].translation, (Vector3){ 2.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Result[0].scale, (Vector3){ 1.5f, 1.5f, 1.5f }));
    TEST_CHECK(run, IsSameRotation(Result[0].rotation, Turn(1.0f, 0.0f, 0.0f, 45.0f)));

    // Below a total of 1 the rest pose takes the remainder; above it the weights are divided out.
    SkeletalPose_ClearBlend(Blend, 1U);
    SkeletalPose_AddToBlend(Blend, &A, 1U, 0.25f);
    SkeletalPose_FinishBlend(Blend, Built.Skeleton, 0.25f, Result);
    TEST_CHECK(run, IsNearVector(Result[0].translation, (Vector3){ 0.25f, 0.0f, 0.0f }));
    SkeletalPose_ClearBlend(Blend, 1U);
    SkeletalPose_AddToBlend(Blend, &B, 1U, 2.0f);
    SkeletalPose_FinishBlend(Blend, Built.Skeleton, 2.0f, Result);
    TEST_CHECK(run, IsNearVector(Result[0].translation, B.translation) && IsSameRotation(Result[0].rotation, Quarter));
    // Nothing at all: the rest pose.
    SkeletalPose_ClearBlend(Blend, 1U);
    SkeletalPose_FinishBlend(Blend, Built.Skeleton, 0.0f, Result);
    TEST_CHECK(run, IsNearVector(Result[0].translation, (Vector3){ 0.0f, 0.0f, 0.0f })
        && IsSameRotation(Result[0].rotation, QuaternionIdentity()));
    DestroySkeleton(&Built);
}

static void PoseBoundsHoldEverySkinnedVertex(TestRun* run)
{
    AnimationLibrary* Library = NULL;
    if (!ReadMotions(run, &Library))
    {
        return;
    }
    TestSkeletonBone Bones[TEST_MAX_BONES];
    int BoneCount = MakeBonesFromLibrary(Library, (Vector3){ 1.0f, 1.0f, 1.0f }, Bones);
    TestSkeleton Built;
    Built.Model = TestSkeletonModel_Create(Bones, BoneCount, 0.3f);
    // Every third vertex follows its bone's parent by half as well, so blended vertices are tried too.
    Mesh* Only = &Built.Model.meshes[0];
    for (int Vertex = 0; Vertex < Only->vertexCount; Vertex += 3)
    {
        int Bone = Only->boneIndices[Vertex * 4];
        if (Bones[Bone].Parent >= 0)
        {
            Only->boneIndices[(Vertex * 4) + 1] = (unsigned char)Bones[Bone].Parent;
            Only->boneWeights[Vertex * 4] = 0.5f;
            Only->boneWeights[(Vertex * 4) + 1] = 0.5f;
        }
    }
    Built.Skeleton = NULL;
    if (TEST_CHECK_SUCCESS(run, GameSkeleton_Create(&Built.Model, NULL, NULL, &Built.Skeleton)))
    {
        SkeletalBindingBone Storage[TEST_MAX_BONES];
        SkeletalBinding Binding;
        SkeletalBinding_Build(Built.Skeleton, Library, Storage, &Binding);
        Transform Pose[TEST_MAX_BONES];
        Matrix Matrices[TEST_MAX_BONES];
        float Palette[TEST_MAX_BONES * SKELETAL_PALETTE_FLOATS_PER_BONE];
        Random Generator;
        Random_Construct2(&Generator, (RandomState){ .Values = { 0xB0E5U, 0x29U, 0x7U, 0x3U } });
        bool IsHeld = true;
        float WorstSlack = 0.0f;
        for (uint32_t Trial = 0U; Trial < 200U; Trial++)
        {
            uint32_t Clip = Trial % AnimationLibrary_GetClipCount(Library);
            float Time = Random_NextSingle(&Generator) * AnimationClip_GetDuration(AnimationLibrary_GetClip(Library, Clip));
            SkeletalBinding_SampleClip(&Binding, Clip, Time, Pose);
            // Turn a few bones further, as a blend or a later look-at might.
            for (int Bone = 0; Bone < BoneCount; Bone += 4)
            {
                Pose[Bone].rotation = QuaternionMultiply(Pose[Bone].rotation, Turn(Random_NextSingle(&Generator) - 0.5f,
                    Random_NextSingle(&Generator) - 0.5f, Random_NextSingle(&Generator) + 0.1f,
                    (Random_NextSingle(&Generator) - 0.5f) * 120.0f));
            }
            SkeletalPose_ComputeModelMatrices(Built.Skeleton, Pose, Matrices);
            SkeletalPose_ComputePalette(Built.Skeleton, Matrices, Palette);
            BoundingBox Bounds;
            if (!SkeletalPose_ComputeBounds(Built.Skeleton, Palette, &Bounds))
            {
                IsHeld = false;
                continue;
            }
            for (int Vertex = 0; Vertex < Only->vertexCount; Vertex++)
            {
                Vector3 Bound = { Only->vertices[Vertex * 3], Only->vertices[(Vertex * 3) + 1],
                    Only->vertices[(Vertex * 3) + 2] };
                Vector3 Skinned = { 0.0f, 0.0f, 0.0f };
                for (int Influence = 0; Influence < 4; Influence++)
                {
                    float Weight = Only->boneWeights[(Vertex * 4) + Influence];
                    if (Weight <= 0.0f)
                    {
                        continue;
                    }
                    const float* Rows = Palette + ((size_t)Only->boneIndices[(Vertex * 4) + Influence]
                        * SKELETAL_PALETTE_FLOATS_PER_BONE);
                    Skinned.x += Weight * ((Rows[0] * Bound.x) + (Rows[1] * Bound.y) + (Rows[2] * Bound.z) + Rows[3]);
                    Skinned.y += Weight * ((Rows[4] * Bound.x) + (Rows[5] * Bound.y) + (Rows[6] * Bound.z) + Rows[7]);
                    Skinned.z += Weight * ((Rows[8] * Bound.x) + (Rows[9] * Bound.y) + (Rows[10] * Bound.z) + Rows[11]);
                }
                float Slack = fminf(fminf(fminf(Skinned.x - Bounds.min.x, Bounds.max.x - Skinned.x),
                    fminf(Skinned.y - Bounds.min.y, Bounds.max.y - Skinned.y)),
                    fminf(Skinned.z - Bounds.min.z, Bounds.max.z - Skinned.z));
                WorstSlack = fminf(WorstSlack, Slack);
                IsHeld = IsHeld && (Slack > -TEST_EPSILON);
            }
        }
        TEST_CHECK(run, IsHeld);
        if (!IsHeld)
        {
            printf("    (a skinned vertex lay %g m outside its pose's bounds)\n", (double)-WorstSlack);
        }
        Random_Deconstruct1(&Generator);
        GameSkeleton_Destroy(Built.Skeleton);
    }
    TestSkeletonModel_Destroy(&Built.Model);
    AnimationLibrary_Destroy(Library);
}


// Static functions: bindings.
static void ABindingMatchesBonesByName(TestRun* run)
{
    AnimationLibrary* Library = NULL;
    if (!ReadMotions(run, &Library))
    {
        return;
    }
    TestSkeletonBone Bones[4] =
    {
        { .Name = "hips", .Parent = -1, .Bind = TestAnimation_Translation(0.0f, 0.95f, 0.0f) },
        { .Name = "spine", .Parent = 0, .Bind = TestAnimation_Translation(0.0f, 1.07f, 0.0f) },
        { .Name = "tail", .Parent = 0, .Bind = TestAnimation_Translation(0.0f, 0.9f, -0.2f) },
        // A bone of a known name under another parent than in the library (there: neck, chest, spine).
        { .Name = "head", .Parent = 0, .Bind = TestAnimation_Translation(0.0f, 1.55f, 0.0f) },
    };
    TestSkeleton Built;
    if (BuildSkeleton(run, Bones, 4, &Built))
    {
        SkeletalBindingBone Storage[4];
        SkeletalBinding Binding;
        SkeletalBinding_Build(Built.Skeleton, Library, Storage, &Binding);
        TEST_CHECK(run, Binding.MappedCount == 3U);
        TEST_CHECK(run, Storage[2].LibraryBone == SKELETAL_BINDING_UNMAPPED);
        uint32_t Hips = 0U;
        TEST_CHECK(run, AnimationLibrary_TryFindBone(Library, (const unsigned char*)u8"hips", &Hips)
            && (Storage[0].LibraryBone == (int32_t)Hips));
        // The armature above the hips has no match: its rest pose folds into the hips; so do the neck and the chest,
        // which this skeleton lacks, into the head.
        TEST_CHECK(run, Storage[0].HasOffset && !Storage[1].HasOffset && Storage[3].HasOffset);
        // The library's head hangs under the spine once those are folded; here it hangs under the hips.
        TEST_CHECK(run, Binding.MismatchedParentCount == 1U);
        TEST_CHECK(run, IsNear(Binding.HeightRatio, 1.0f));
        // The unmatched bone keeps its rest pose in every clip.
        uint32_t Walk = 0U;
        Transform Pose[4];
        TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"walk", &Walk));
        SkeletalBinding_SampleClip(&Binding, Walk, 0.3f, Pose);
        TEST_CHECK(run, IsNearVector(Pose[2].translation, GameSkeleton_GetBone(Built.Skeleton, 2U)->_rest.translation));
        TEST_CHECK(run, IsSameRotation(Pose[2].rotation, QuaternionIdentity()));
    }
    DestroySkeleton(&Built);
    AnimationLibrary_Destroy(Library);
}

static void AClipOnItsOwnSkeletonPlaysAsAuthored(TestRun* run)
{
    AnimationLibrary* Library = NULL;
    if (!ReadMotions(run, &Library))
    {
        return;
    }
    TestSkeletonBone Bones[TEST_MAX_BONES];
    int BoneCount = MakeBonesFromLibrary(Library, (Vector3){ 1.0f, 1.0f, 1.0f }, Bones);
    TestSkeleton Built;
    if (BuildSkeleton(run, Bones, BoneCount, &Built))
    {
        SkeletalBindingBone Storage[TEST_MAX_BONES];
        SkeletalBinding Binding;
        SkeletalBinding_Build(Built.Skeleton, Library, Storage, &Binding);
        TEST_CHECK(run, (Binding.MappedCount == (uint32_t)BoneCount) && (Binding.MismatchedParentCount == 0U));
        TEST_CHECK(run, IsNear(Binding.HeightRatio, 1.0f));
        bool IsAuthored = true;
        Transform Pose[TEST_MAX_BONES];
        for (uint32_t Clip = 0U; Clip < AnimationLibrary_GetClipCount(Library); Clip++)
        {
            const AnimationClip* Source = AnimationLibrary_GetClip(Library, Clip);
            for (float Time = 0.0f; Time <= AnimationClip_GetDuration(Source); Time += 0.07f)
            {
                SkeletalBinding_SampleClip(&Binding, Clip, Time, Pose);
                for (int Bone = 0; Bone < BoneCount; Bone++)
                {
                    uint32_t LibraryBone = (uint32_t)Storage[Bone].LibraryBone;
                    Transform Authored = AnimationClip_SampleBone(Source, LibraryBone,
                        AnimationLibrary_GetBone(Library, LibraryBone)->_rest, Time);
                    IsAuthored = IsAuthored && IsNearVector(Pose[Bone].translation, Authored.translation)
                        && IsSameRotation(Pose[Bone].rotation, Authored.rotation)
                        && IsNearVector(Pose[Bone].scale, Authored.scale);
                }
            }
        }
        TEST_CHECK(run, IsAuthored);
    }
    DestroySkeleton(&Built);
    AnimationLibrary_Destroy(Library);
}

static void AClipOnAShorterSkeletonKeepsItsBoneLengths(TestRun* run)
{
    AnimationLibrary* Library = NULL;
    if (!ReadMotions(run, &Library))
    {
        return;
    }
    // The short placeholder's proportions: 85% of the height, wider and deeper.
    TestSkeletonBone Bones[TEST_MAX_BONES];
    int BoneCount = MakeBonesFromLibrary(Library, (Vector3){ 1.15f, 0.85f, 1.1f }, Bones);
    TestSkeleton Built;
    if (BuildSkeleton(run, Bones, BoneCount, &Built))
    {
        SkeletalBindingBone Storage[TEST_MAX_BONES];
        SkeletalBinding Binding;
        SkeletalBinding_Build(Built.Skeleton, Library, Storage, &Binding);
        TEST_CHECK(run, IsNear(Binding.HeightRatio, 0.85f));
        uint32_t Crouch = 0U;
        uint32_t Hips = 0U;
        TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"crouch_idle", &Crouch));
        TEST_CHECK(run, GameSkeleton_TryFindBone(Built.Skeleton, (const unsigned char*)u8"hips", &Hips));
        Transform Pose[TEST_MAX_BONES];
        SkeletalBinding_SampleClip(&Binding, Crouch, 0.5f, Pose);
        const AnimationClip* Clip = AnimationLibrary_GetClip(Library, Crouch);
        bool AreLengthsKept = true;
        bool AreRotationsKept = true;
        for (int Bone = 0; Bone < BoneCount; Bone++)
        {
            uint32_t LibraryBone = (uint32_t)Storage[Bone].LibraryBone;
            Transform Authored = AnimationClip_SampleBone(Clip, LibraryBone,
                AnimationLibrary_GetBone(Library, LibraryBone)->_rest, 0.5f);
            AreRotationsKept = AreRotationsKept && IsSameRotation(Pose[Bone].rotation, Authored.rotation);
            if ((uint32_t)Bone != Hips)
            {
                AreLengthsKept = AreLengthsKept
                    && IsNearVector(Pose[Bone].translation, GameSkeleton_GetBone(Built.Skeleton, (uint32_t)Bone)->_rest.translation);
            }
        }
        TEST_CHECK(run, AreRotationsKept && AreLengthsKept);
        // The hips crouch 0.42 m on the tall figure: 0.85 of that here, from its own rest height.
        Transform AuthoredHips = AnimationClip_SampleBone(Clip, (uint32_t)Storage[Hips].LibraryBone,
            AnimationLibrary_GetBone(Library, (uint32_t)Storage[Hips].LibraryBone)->_rest, 0.5f);
        float TallDrop = AuthoredHips.translation.y - 0.95f;
        float ShortDrop = Pose[Hips].translation.y - (0.95f * 0.85f);
        TEST_CHECK(run, IsNear(ShortDrop, TallDrop * 0.85f) && (TallDrop < -0.4f));
    }
    DestroySkeleton(&Built);
    AnimationLibrary_Destroy(Library);
}

static void UnmatchedAncestorsFoldIntoTheirDescendants(TestRun* run)
{
    // An armature turned a quarter about Y and scaled 2 above a hips bone that a clip turns about X.
    Quaternion ArmatureTurn = Turn(0.0f, 1.0f, 0.0f, 90.0f);
    TestGltfNode Nodes[2] =
    {
        { .Name = "armature", .Parent = -1, .Rest = { { 0.0f, 0.5f, 0.0f }, ArmatureTurn, { 2.0f, 2.0f, 2.0f } } },
        { .Name = "hips", .Parent = 0, .Rest = TestAnimation_Translation(0.0f, 0.5f, 0.0f) },
    };
    Quaternion Tilt = Turn(1.0f, 0.0f, 0.0f, 30.0f);
    const float Rotations[] = { 0.0f, 0.0f, 0.0f, 1.0f, Tilt.x, Tilt.y, Tilt.z, Tilt.w };
    static const float Times[] = { 0.0f, 1.0f };
    TestGltfChannel Channel = { .Node = 1, .Path = "rotation", .Interpolation = "LINEAR", .Times = Times,
        .KeyCount = 2U, .Values = Rotations, .ValueComponents = 4U };
    TestGltfClip Clip = { .Name = "tilt", .Channels = &Channel, .ChannelCount = 1U };
    GenericBuffer Text;
    TestGltf_Write(Nodes, 2U, &Clip, 1U, &Text);
    AnimationLibraryReadOptions Options = { .Clips = NULL, .ClipCount = 0U, .FilePath = NULL, .Logger = NULL,
        .Description = NULL };
    AnimationLibrary* Library = NULL;
    if (TEST_CHECK_SUCCESS(run, AnimationLibrary_CreateFromGltf(Text._data, Text._count - 1U, &Options, &Library)))
    {
        // The model's hips rests where the armature puts the library's: 1.5 m up, turned and scaled with it.
        Matrix Armature = SkeletalAnimation_ComposeTransform(Nodes[0].Rest);
        Matrix HipsBind = MatrixMultiply(SkeletalAnimation_ComposeTransform(Nodes[1].Rest), Armature);
        TestSkeletonBone Bones[1] =
        {
            { .Name = "hips", .Parent = -1, .Bind = SkeletalAnimation_DecomposeMatrix(HipsBind) },
        };
        TestSkeleton Built;
        if (BuildSkeleton(run, Bones, 1, &Built))
        {
            SkeletalBindingBone Storage[1];
            SkeletalBinding Binding;
            SkeletalBinding_Build(Built.Skeleton, Library, Storage, &Binding);
            TEST_CHECK(run, Storage[0].HasOffset);
            TEST_CHECK(run, IsNearMatrix(SkeletalAnimation_ComposeTransform(Storage[0].LibraryRest), HipsBind));
            Transform Pose[1];
            SkeletalBinding_SampleClip(&Binding, 0U, 1.0f, Pose);
            // The clip's tilt inside the armature's turn; the hips stays where it rests.
            TEST_CHECK(run, IsSameRotation(Pose[0].rotation, QuaternionMultiply(ArmatureTurn, Tilt)));
            TEST_CHECK(run, IsNearVector(Pose[0].translation, (Vector3){ 0.0f, 1.5f, 0.0f }));
            TEST_CHECK(run, IsNearVector(Pose[0].scale, (Vector3){ 2.0f, 2.0f, 2.0f }));
        }
        DestroySkeleton(&Built);
        AnimationLibrary_Destroy(Library);
    }
    Memory_Free(Text._data);
}


// Static functions: the mixer.
static SkeletalClipRef MakeClip(uint32_t clip, float duration, bool isLooped, float speed)
{
    return (SkeletalClipRef){ .Library = 0U, .Clip = clip, .Duration = duration, .IsLooped = isLooped, .Speed = speed };
}

static float WeightOf(const SkeletalMixer* mixer, uint32_t clip)
{
    const SkeletalClipState* State = SkeletalMixer_FindClip(mixer, 0U, clip);
    return (State != NULL) ? State->Weight : -1.0f;
}

static void TheMixerCrossFadesKeepingTheWeightsSum(TestRun* run)
{
    SkeletalMixer Mixer;
    SkeletalMixer_Construct(&Mixer);
    SkeletalClipRef A = MakeClip(0U, 1.0f, true, 1.0f);
    SkeletalClipRef B = MakeClip(1U, 2.0f, true, 1.0f);
    SkeletalMixer_Play(&Mixer, &A, 0.0f, false, 0.0);
    TEST_CHECK(run, (Mixer.StateCount == 1U) && IsNear(WeightOf(&Mixer, 0U), 1.0f));
    SkeletalMixer_Play(&Mixer, &B, 1.0f, false, 0.0);
    SkeletalMixer_Advance(&Mixer, 0.25);
    TEST_CHECK(run, IsNear(WeightOf(&Mixer, 0U), 0.75f) && IsNear(WeightOf(&Mixer, 1U), 0.25f));
    // Another change halfway: the weights start from where they are and keep their sum.
    SkeletalMixer_Play(&Mixer, &A, 0.5f, false, 0.0);
    SkeletalMixer_Advance(&Mixer, 0.25);
    TEST_CHECK(run, IsNear(WeightOf(&Mixer, 0U) + WeightOf(&Mixer, 1U), 1.0f));
    TEST_CHECK(run, IsNear(WeightOf(&Mixer, 0U), 0.875f));
    SkeletalMixer_Advance(&Mixer, 0.25);
    TEST_CHECK(run, (Mixer.StateCount == 1U) && IsNear(WeightOf(&Mixer, 0U), 1.0f) && (WeightOf(&Mixer, 1U) < 0.0f));

    // From nothing, a fade rises from the rest pose (the blend fills the rest).
    SkeletalMixer_Construct(&Mixer);
    SkeletalMixer_Play(&Mixer, &A, 1.0f, false, 0.0);
    TEST_CHECK(run, IsNear(WeightOf(&Mixer, 0U), 0.0f));
    SkeletalMixer_Advance(&Mixer, 0.5);
    TEST_CHECK(run, IsNear(WeightOf(&Mixer, 0U), 0.5f));
    SkeletalMixer_StopAll(&Mixer, 0.5f);
    SkeletalMixer_Advance(&Mixer, 0.25);
    TEST_CHECK(run, IsNear(WeightOf(&Mixer, 0U), 0.25f));
    SkeletalMixer_Advance(&Mixer, 0.25);
    TEST_CHECK(run, Mixer.StateCount == 0U);
}

static void LoopingClipsWrapAndOthersHoldTheirEnd(TestRun* run)
{
    SkeletalMixer Mixer;
    SkeletalMixer_Construct(&Mixer);
    SkeletalClipRef Loop = MakeClip(0U, 1.0f, true, 1.0f);
    SkeletalClipRef Once = MakeClip(1U, 1.0f, false, 2.0f);
    SkeletalMixer_SetWeight(&Mixer, &Loop, 0.5f, 0.0f, 0.0);
    SkeletalMixer_SetWeight(&Mixer, &Once, 0.5f, 0.0f, 0.0);
    SkeletalMixer_Advance(&Mixer, 2.25);
    const SkeletalClipState* Looped = SkeletalMixer_FindClip(&Mixer, 0U, 0U);
    const SkeletalClipState* Ended = SkeletalMixer_FindClip(&Mixer, 0U, 1U);
    TEST_CHECK(run, (Looped != NULL) && (fabs(Looped->Time - 0.25) < 1e-9) && !Looped->HasEnded);
    TEST_CHECK(run, (Ended != NULL) && (Ended->Time == 1.0) && Ended->HasEnded);
    // Its speed doubles its time.
    SkeletalMixer_Construct(&Mixer);
    SkeletalMixer_Play(&Mixer, &Once, 0.0f, false, 0.0);
    SkeletalMixer_Advance(&Mixer, 0.25);
    TEST_CHECK(run, fabs(SkeletalMixer_FindClip(&Mixer, 0U, 1U)->Time - 0.5) < 1e-9);
    // Start times wrap for a looping clip and stay within any other.
    SkeletalMixer_Construct(&Mixer);
    SkeletalMixer_Play(&Mixer, &Loop, 0.0f, false, 2.5);
    TEST_CHECK(run, fabs(SkeletalMixer_FindClip(&Mixer, 0U, 0U)->Time - 0.5) < 1e-9);
    SkeletalMixer_Play(&Mixer, &Once, 0.0f, false, 5.0);
    TEST_CHECK(run, SkeletalMixer_FindClip(&Mixer, 0U, 1U)->Time == 1.0);
    // Time that is not a forward step moves nothing.
    double Before = SkeletalMixer_FindClip(&Mixer, 0U, 1U)->Time;
    SkeletalMixer_Advance(&Mixer, -1.0);
    SkeletalMixer_Advance(&Mixer, NAN);
    SkeletalMixer_Advance(&Mixer, 0.0);
    TEST_CHECK(run, SkeletalMixer_FindClip(&Mixer, 0U, 1U)->Time == Before);
}

static void ReplayingAClipKeepsItsTimeUnlessRestartedOrEnded(TestRun* run)
{
    SkeletalMixer Mixer;
    SkeletalMixer_Construct(&Mixer);
    SkeletalClipRef Loop = MakeClip(0U, 1.0f, true, 1.0f);
    SkeletalClipRef Once = MakeClip(1U, 1.0f, false, 1.0f);
    SkeletalMixer_Play(&Mixer, &Loop, 0.0f, false, 0.0);
    SkeletalMixer_Advance(&Mixer, 0.3);
    SkeletalMixer_Play(&Mixer, &Loop, 0.2f, false, 0.0);
    TEST_CHECK(run, fabs(SkeletalMixer_FindClip(&Mixer, 0U, 0U)->Time - 0.3) < 1e-9);
    SkeletalMixer_Play(&Mixer, &Loop, 0.2f, true, 0.1);
    TEST_CHECK(run, fabs(SkeletalMixer_FindClip(&Mixer, 0U, 0U)->Time - 0.1) < 1e-9);
    // A clip that ended starts over when it is played again.
    SkeletalMixer_Play(&Mixer, &Once, 0.0f, false, 0.0);
    SkeletalMixer_Advance(&Mixer, 3.0);
    TEST_CHECK(run, SkeletalMixer_FindClip(&Mixer, 0U, 1U)->HasEnded);
    SkeletalMixer_Play(&Mixer, &Once, 0.0f, false, 0.0);
    TEST_CHECK(run, !SkeletalMixer_FindClip(&Mixer, 0U, 1U)->HasEnded && (SkeletalMixer_FindClip(&Mixer, 0U, 1U)->Time == 0.0));
}

static void SetWeightMovesOneClipAlone(TestRun* run)
{
    SkeletalMixer Mixer;
    SkeletalMixer_Construct(&Mixer);
    SkeletalClipRef A = MakeClip(0U, 1.0f, true, 1.0f);
    SkeletalClipRef B = MakeClip(1U, 1.0f, true, 1.0f);
    SkeletalMixer_SetWeight(&Mixer, &A, 0.5f, 0.0f, 0.0);
    SkeletalMixer_SetWeight(&Mixer, &B, 0.25f, 1.0f, 0.0);
    SkeletalMixer_Advance(&Mixer, 0.5);
    TEST_CHECK(run, IsNear(WeightOf(&Mixer, 0U), 0.5f) && IsNear(WeightOf(&Mixer, 1U), 0.125f));
    SkeletalMixer_SetWeight(&Mixer, &A, 0.0f, 0.0f, 0.0);
    TEST_CHECK(run, (WeightOf(&Mixer, 0U) < 0.0f) && (Mixer.StateCount == 1U));
    // A weight of 0 for a clip that does not play adds nothing; weights clamp to [0, 1].
    SkeletalMixer_SetWeight(&Mixer, &A, 0.0f, 0.0f, 0.0);
    TEST_CHECK(run, Mixer.StateCount == 1U);
    SkeletalMixer_SetWeight(&Mixer, &A, 7.0f, 0.0f, 0.0);
    TEST_CHECK(run, IsNear(WeightOf(&Mixer, 0U), 1.0f));

    // A full mixer drops its lightest clip for a new one.
    SkeletalMixer_Construct(&Mixer);
    for (uint32_t Clip = 0U; Clip < SKELETAL_MIXER_MAX_CLIPS; Clip++)
    {
        SkeletalClipRef Each = MakeClip(Clip, 1.0f, true, 1.0f);
        SkeletalMixer_SetWeight(&Mixer, &Each, 0.9f - (0.1f * (float)(SKELETAL_MIXER_MAX_CLIPS - Clip)), 0.0f, 0.0);
    }
    TEST_CHECK(run, Mixer.StateCount == SKELETAL_MIXER_MAX_CLIPS);
    SkeletalClipRef Extra = MakeClip(99U, 1.0f, true, 1.0f);
    SkeletalMixer_Play(&Mixer, &Extra, 0.5f, false, 0.0);
    TEST_CHECK(run, (Mixer.StateCount == SKELETAL_MIXER_MAX_CLIPS) && (WeightOf(&Mixer, 0U) < 0.0f)
        && (WeightOf(&Mixer, 99U) >= 0.0f) && (WeightOf(&Mixer, 1U) >= 0.0f));
}

static void TheMixersPoseIsItsClipsBlended(TestRun* run)
{
    AnimationLibrary* Library = NULL;
    if (!ReadMotions(run, &Library))
    {
        return;
    }
    TestSkeletonBone Bones[TEST_MAX_BONES];
    int BoneCount = MakeBonesFromLibrary(Library, (Vector3){ 1.0f, 1.0f, 1.0f }, Bones);
    TestSkeleton Built;
    if (BuildSkeleton(run, Bones, BoneCount, &Built))
    {
        SkeletalBindingBone Storage[TEST_MAX_BONES];
        SkeletalBinding Binding;
        SkeletalBinding_Build(Built.Skeleton, Library, Storage, &Binding);
        uint32_t Walk = 0U;
        uint32_t Run = 0U;
        TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"walk", &Walk));
        TEST_CHECK(run, AnimationLibrary_TryFindClip(Library, (const unsigned char*)u8"run", &Run));
        SkeletalClipRef WalkRef = MakeClip(Walk, 1.0f, true, 1.0f);
        SkeletalClipRef RunRef = MakeClip(Run, 0.68f, true, 1.0f);
        SkeletalMixer Mixer;
        SkeletalMixer_Construct(&Mixer);
        SkeletalMixer_Play(&Mixer, &WalkRef, 0.0f, false, 0.0);
        SkeletalMixer_Advance(&Mixer, 0.3);

        Transform Scratch[TEST_MAX_BONES];
        SkeletalBlendBone Blend[TEST_MAX_BONES];
        Transform Pose[TEST_MAX_BONES];
        Transform Expected[TEST_MAX_BONES];
        SkeletalMixer_SamplePose(&Mixer, &Binding, 1U, Built.Skeleton, Scratch, Blend, Pose);
        SkeletalBinding_SampleClip(&Binding, Walk, 0.3f, Expected);
        bool IsWalk = true;
        for (int Bone = 0; Bone < BoneCount; Bone++)
        {
            IsWalk = IsWalk && IsNearVector(Pose[Bone].translation, Expected[Bone].translation)
                && IsSameRotation(Pose[Bone].rotation, Expected[Bone].rotation);
        }
        TEST_CHECK(run, IsWalk);

        // Halfway through a cross-fade to the run: each bone between the two.
        SkeletalMixer_Play(&Mixer, &RunRef, 0.4f, true, 0.0);
        SkeletalMixer_Advance(&Mixer, 0.2);
        SkeletalMixer_SamplePose(&Mixer, &Binding, 1U, Built.Skeleton, Scratch, Blend, Pose);
        Transform WalkPose[TEST_MAX_BONES];
        Transform RunPose[TEST_MAX_BONES];
        SkeletalBinding_SampleClip(&Binding, Walk, 0.5f, WalkPose);
        SkeletalBinding_SampleClip(&Binding, Run, 0.2f, RunPose);
        uint32_t Hips = 0U;
        TEST_CHECK(run, GameSkeleton_TryFindBone(Built.Skeleton, (const unsigned char*)u8"hips", &Hips));
        TEST_CHECK(run, IsNear(Pose[Hips].translation.y,
            (0.5f * WalkPose[Hips].translation.y) + (0.5f * RunPose[Hips].translation.y)));
        // A binding index past the bindings plays nothing (the rest pose fills it).
        SkeletalMixer_SamplePose(&Mixer, &Binding, 0U, Built.Skeleton, Scratch, Blend, Pose);
        TEST_CHECK(run, IsNearVector(Pose[Hips].translation, GameSkeleton_GetBone(Built.Skeleton, Hips)->_rest.translation));
    }
    DestroySkeleton(&Built);
    AnimationLibrary_Destroy(Library);
}


// Public functions.
void SkeletalAnimationTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "compose_and_decompose_round_trip", ComposeAndDecomposeRoundTrip },
        { "a_skeleton_is_built_from_a_raylib_model", ASkeletonIsBuiltFromARaylibModel },
        { "broken_skeletons_are_refused", BrokenSkeletonsAreRefused },
        { "meshes_the_gpu_cannot_skin_draw_as_bound", MeshesTheGpuCannotSkinDrawAsBound },
        { "two_skeletons_have_the_same_bones_by_name_and_parent", TwoSkeletonsHaveTheSameBonesByNameAndParent },
        { "the_rest_pose_skins_nothing", TheRestPoseSkinsNothing },
        { "blends_add_poses_by_weight", BlendsAddPosesByWeight },
        { "pose_bounds_hold_every_skinned_vertex", PoseBoundsHoldEverySkinnedVertex },
        { "a_binding_matches_bones_by_name", ABindingMatchesBonesByName },
        { "a_clip_on_its_own_skeleton_plays_as_authored", AClipOnItsOwnSkeletonPlaysAsAuthored },
        { "a_clip_on_a_shorter_skeleton_keeps_its_bone_lengths", AClipOnAShorterSkeletonKeepsItsBoneLengths },
        { "unmatched_ancestors_fold_into_their_descendants", UnmatchedAncestorsFoldIntoTheirDescendants },
        { "the_mixer_cross_fades_keeping_the_weights_sum", TheMixerCrossFadesKeepingTheWeightsSum },
        { "looping_clips_wrap_and_others_hold_their_end", LoopingClipsWrapAndOthersHoldTheirEnd },
        { "replaying_a_clip_keeps_its_time_unless_restarted_or_ended", ReplayingAClipKeepsItsTimeUnlessRestartedOrEnded },
        { "set_weight_moves_one_clip_alone", SetWeightMovesOneClipAlone },
        { "the_mixers_pose_is_its_clips_blended", TheMixersPoseIsItsClipsBlended },
    };
    TestRun_RunSuite(run, "SkeletalAnimation", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
