#include <inttypes.h>
#include <math.h>
#include "GameSkeleton.h"
#include "SkeletalAnimation.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * A skinned model's skeleton, built from the CPU data of a loaded raylib Model (see GameSkeleton.h). raylib's glTF,
 * IQM and M3D loaders fill Model.skeleton (bone names, parents, bind poses in mesh space) and each mesh's bone indices
 * and weights; with SUPPORT_GPU_SKINNING they also upload the two bone buffers (vertex buffers 7 and 8, which the
 * shaders read as vertexBoneIndices and vertexBoneWeights) and keep no CPU-skinning copies (Mesh.animVertices stays
 * NULL), which is how a mesh the GPU can skin is told apart.
 */


// Macros.
/** raylib's vertex buffer slots of the bone indices and weights (RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES and
 *  _BONEWEIGHTS); Mesh.vboId has entries for them only when raylib was built with SUPPORT_GPU_SKINNING. */
#define BONE_INDEX_BUFFER_SLOT 7
#define BONE_WEIGHT_BUFFER_SLOT 8
/** Bone influences per vertex in raylib's meshes. */
#define INFLUENCES_PER_VERTEX 4
/** Bind matrices whose determinant is nearer zero than this cannot be inverted. */
#define MIN_BIND_DETERMINANT 1e-12f


// Types.
/* How one mesh's bone data reads. */
typedef enum MeshSkinningEnum
{
    MeshSkinning_None,         // no bone indices or weights: it draws as it is bound
    MeshSkinning_Skinned,      // the GPU skins it
    MeshSkinning_NotUploaded,  // weights, but raylib did not upload them (built without GPU skinning)
    MeshSkinning_Broken        // an index past the skeleton or a weight that is not finite
} MeshSkinning;


// Static functions.
static bool IsMatrixFinite(const Matrix* matrix)
{
    const float Values[16] =
    {
        matrix->m0, matrix->m1, matrix->m2, matrix->m3, matrix->m4, matrix->m5, matrix->m6, matrix->m7,
        matrix->m8, matrix->m9, matrix->m10, matrix->m11, matrix->m12, matrix->m13, matrix->m14, matrix->m15
    };
    for (size_t Index = 0U; Index < 16U; Index++)
    {
        if (!isfinite(Values[Index]))
        {
            return false;
        }
    }
    return true;
}

static void LogWarning(Logger* logger, const unsigned char* description, const char* text)
{
    if (logger == NULL)
    {
        return;
    }
    Error LogResult = Logger_LogWarningFormatted(logger, (const unsigned char*)u8"%s: %s",
        (description != NULL) ? (const char*)description : "A skinned model", text);
    Error_Deconstruct(&LogResult);
}

/* How a mesh's bone data reads (see MeshSkinning). */
static MeshSkinning GetMeshSkinning(const Mesh* mesh, int boneCount)
{
    if ((mesh->boneIndices == NULL) || (mesh->boneWeights == NULL) || (mesh->vertexCount <= 0))
    {
        return MeshSkinning_None;
    }
    size_t InfluenceCount = (size_t)mesh->vertexCount * INFLUENCES_PER_VERTEX;
    for (size_t Index = 0U; Index < InfluenceCount; Index++)
    {
        float Weight = mesh->boneWeights[Index];
        if (!isfinite(Weight) || ((Weight != 0.0f) && ((int)mesh->boneIndices[Index] >= boneCount)))
        {
            return MeshSkinning_Broken;
        }
    }
    // A GPU-skinning raylib keeps no CPU-skinning copy, and has vertex buffer slots for the bone data.
    if ((mesh->animVertices != NULL) || (mesh->vboId == NULL) || (mesh->vboId[BONE_INDEX_BUFFER_SLOT] == 0U)
        || (mesh->vboId[BONE_WEIGHT_BUFFER_SLOT] == 0U))
    {
        return MeshSkinning_NotUploaded;
    }
    return MeshSkinning_Skinned;
}

static void ExpandBox(BoundingBox* box, bool* hasBox, Vector3 point)
{
    if (!*hasBox)
    {
        box->min = point;
        box->max = point;
        *hasBox = true;
        return;
    }
    box->min = Vector3Min(box->min, point);
    box->max = Vector3Max(box->max, point);
}

/* Each bone's vertex box from the skinned meshes, and the static box from every other mesh's vertices. */
static void MeasureVertices(const Model* model, const bool* isMeshSkinned, GameSkeleton* skeleton,
    GameSkeletonBone* bones)
{
    for (int MeshIndex = 0; MeshIndex < model->meshCount; MeshIndex++)
    {
        const Mesh* Source = &model->meshes[MeshIndex];
        const float* Vertices = (Source->animVertices != NULL) ? Source->animVertices : Source->vertices;
        if (Vertices == NULL)
        {
            continue;
        }
        for (int Vertex = 0; Vertex < Source->vertexCount; Vertex++)
        {
            Vector3 Point = { Vertices[(Vertex * 3) + 0], Vertices[(Vertex * 3) + 1], Vertices[(Vertex * 3) + 2] };
            if (!isfinite(Point.x) || !isfinite(Point.y) || !isfinite(Point.z))
            {
                continue;
            }
            if (!isMeshSkinned[MeshIndex])
            {
                ExpandBox(&skeleton->_staticBounds, &skeleton->_hasStaticBounds, Point);
                continue;
            }
            for (int Influence = 0; Influence < INFLUENCES_PER_VERTEX; Influence++)
            {
                size_t Slot = ((size_t)Vertex * INFLUENCES_PER_VERTEX) + (size_t)Influence;
                if (Source->boneWeights[Slot] > 0.0f)
                {
                    GameSkeletonBone* Bone = &bones[Source->boneIndices[Slot]];
                    ExpandBox(&Bone->_vertexBounds, &Bone->_hasVertices, Point);
                }
            }
        }
    }
}

/* Checks the parents (each a bone, none its own ancestor) and writes the evaluation order, parents first. */
static Error CheckParentsAndOrder(const BoneInfo* bones, int boneCount, uint16_t* outOrder)
{
    for (int Index = 0; Index < boneCount; Index++)
    {
        int Parent = bones[Index].parent;
        if ((Parent < -1) || (Parent >= boneCount) || (Parent == Index))
        {
            return Error_Construct4(ErrorCode_InvalidAssetData, "bone %d has parent %d, which is not another bone.",
                Index, Parent);
        }
        // Walking up must reach a root within boneCount steps; a longer walk is a cycle.
        int Ancestor = Parent;
        for (int Step = 0; Ancestor >= 0; Step++)
        {
            if (Step >= boneCount)
            {
                return Error_Construct4(ErrorCode_InvalidAssetData, "bone %d is its own ancestor.", Index);
            }
            Ancestor = bones[Ancestor].parent;
        }
    }
    // Parents first: repeatedly emit the bones whose parent is out (at most boneCount passes).
    uint32_t Emitted = 0U;
    bool IsOut[GAME_SKELETON_MAX_BONES] = { false };
    while (Emitted < (uint32_t)boneCount)
    {
        for (int Index = 0; Index < boneCount; Index++)
        {
            int Parent = bones[Index].parent;
            if (!IsOut[Index] && ((Parent < 0) || IsOut[Parent]))
            {
                IsOut[Index] = true;
                outOrder[Emitted] = (uint16_t)Index;
                Emitted++;
            }
        }
    }
    return Error_CreateSuccess();
}

/* Fills every bone's name, parent, bind matrix and its inverse, rest pose and rest position. */
static Error FillBones(const Model* model, GameSkeletonBone* bones)
{
    const ModelSkeleton* Source = &model->skeleton;
    for (int Index = 0; Index < Source->boneCount; Index++)
    {
        GameSkeletonBone* Bone = &bones[Index];
        size_t NameLength = 0U;
        while ((NameLength < (GAME_SKELETON_BONE_NAME_CAPACITY - 1U)) && (Source->bones[Index].name[NameLength] != '\0'))
        {
            Bone->_name[NameLength] = (unsigned char)Source->bones[Index].name[NameLength];
            NameLength++;
        }
        Bone->_name[NameLength] = 0U;
        Bone->_parent = Source->bones[Index].parent;

        Transform Bind = Source->bindPose[Index];
        Bind.rotation = QuaternionNormalize(Bind.rotation);
        Bone->_bind = SkeletalAnimation_ComposeTransform(Bind);
        float Determinant = MatrixDeterminant(Bone->_bind);
        if (!IsMatrixFinite(&Bone->_bind) || !(fabsf(Determinant) > MIN_BIND_DETERMINANT))
        {
            return Error_Construct4(ErrorCode_InvalidAssetData, "bone %d (\"%s\") has a bind pose that is not finite or "
                "cannot be inverted.", Index, (const char*)Bone->_name);
        }
        Bone->_inverseBind = MatrixInvert(Bone->_bind);
        Bone->_restPosition = Bind.translation;
    }
    // A bone's rest pose is its bind pose relative to its parent's.
    for (int Index = 0; Index < Source->boneCount; Index++)
    {
        GameSkeletonBone* Bone = &bones[Index];
        if (Bone->_parent < 0)
        {
            Bone->_rest = SkeletalAnimation_DecomposeMatrix(Bone->_bind);
            continue;
        }
        Bone->_rest = SkeletalAnimation_DecomposeMatrix(MatrixMultiply(Bone->_bind, bones[Bone->_parent]._inverseBind));
    }
    return Error_CreateSuccess();
}

/* Whether a stored bone name equals @p name as raylib keeps names (their first GAME_SKELETON_BONE_NAME_CAPACITY - 1
 * bytes). */
static bool IsBoneNamed(const GameSkeletonBone* bone, const unsigned char* name)
{
    size_t Length = StringUTF8_GetByteLength(name);
    if (Length > (GAME_SKELETON_BONE_NAME_CAPACITY - 1U))
    {
        Length = GAME_SKELETON_BONE_NAME_CAPACITY - 1U;
    }
    return (StringUTF8_GetByteLength(bone->_name) == Length) && Memory_IsEqual(bone->_name, name, Length);
}


// Public functions.
Error GameSkeleton_Create(const Model* model, const unsigned char* description, Logger* logger,
    GameSkeleton** outSkeleton)
{
    if ((model == NULL) || (outSkeleton == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameSkeleton_Create: model and outSkeleton must not be NULL.");
    }
    *outSkeleton = NULL;
    const ModelSkeleton* Source = &model->skeleton;
    if ((Source->boneCount <= 0) || (Source->bones == NULL) || (Source->bindPose == NULL))
    {
        return Error_CreateSuccess();
    }
    if ((size_t)Source->boneCount > GAME_SKELETON_MAX_BONES)
    {
        return Error_Construct4(ErrorCode_InvalidAssetData, "%s has %d bones; a skeleton may have at most %" PRIu64 ".",
            (description != NULL) ? (const char*)description : "A skinned model", Source->boneCount,
            (uint64_t)GAME_SKELETON_MAX_BONES);
    }
    uint32_t BoneCount = (uint32_t)Source->boneCount;
    uint32_t MeshCount = (model->meshCount > 0) ? (uint32_t)model->meshCount : 0U;

    // One block: the skeleton, its bones, the order and the mesh flags (each part aligned for what follows it).
    size_t BonesOffset = sizeof(GameSkeleton);
    size_t OrderOffset = BonesOffset + ((size_t)BoneCount * sizeof(GameSkeletonBone));
    size_t FlagsOffset = OrderOffset + ((size_t)BoneCount * sizeof(uint16_t));
    size_t ByteCount = FlagsOffset + ((size_t)MeshCount * sizeof(bool));
    unsigned char* Block = Memory_Allocate(ByteCount);
    Memory_Zero(Block, ByteCount);
    GameSkeleton* Skeleton = (GameSkeleton*)(void*)Block;
    GameSkeletonBone* Bones = (GameSkeletonBone*)(void*)(Block + BonesOffset);
    uint16_t* Order = (uint16_t*)(void*)(Block + OrderOffset);
    bool* IsMeshSkinned = (bool*)(void*)(Block + FlagsOffset);

    Error Result = CheckParentsAndOrder(Source->bones, Source->boneCount, Order);
    if (Result.Code == ErrorCode_Success)
    {
        Result = FillBones(model, Bones);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Wrapped = Error_Construct4(Result.Code, "%s: %s",
            (description != NULL) ? (const char*)description : "A skinned model",
            (Result.Message != NULL) ? (const char*)Result.Message : "its skeleton is broken.");
        Error_Deconstruct(&Result);
        Memory_Free(Block);
        return Wrapped;
    }

    bool HasNotUploaded = false;
    bool HasBroken = false;
    for (uint32_t Index = 0U; Index < MeshCount; Index++)
    {
        MeshSkinning Skinning = GetMeshSkinning(&model->meshes[Index], Source->boneCount);
        IsMeshSkinned[Index] = Skinning == MeshSkinning_Skinned;
        Skeleton->_skinnedMeshCount += IsMeshSkinned[Index] ? 1U : 0U;
        HasNotUploaded = HasNotUploaded || (Skinning == MeshSkinning_NotUploaded);
        HasBroken = HasBroken || (Skinning == MeshSkinning_Broken);
    }
    if (HasNotUploaded)
    {
        LogWarning(logger, description, "its meshes have bone weights the GPU does not have (raylib was built without "
            "SUPPORT_GPU_SKINNING, see BUILDING.md); they draw as they are bound and do not bend.");
    }
    if (HasBroken)
    {
        LogWarning(logger, description, "a mesh names a bone past its skeleton or has a weight that is not a number; "
            "it draws as it is bound and does not bend.");
    }

    Skeleton->_bones = Bones;
    Skeleton->_boneCount = BoneCount;
    Skeleton->_order = Order;
    Skeleton->_isMeshSkinned = IsMeshSkinned;
    Skeleton->_meshCount = MeshCount;
    MeasureVertices(model, IsMeshSkinned, Skeleton, Bones);
    *outSkeleton = Skeleton;
    return Error_CreateSuccess();
}

void GameSkeleton_Destroy(GameSkeleton* self)
{
    // The skeleton, its bones, its order and its mesh flags are one block.
    Memory_Free(self);
}

bool GameSkeleton_TryFindBone(const GameSkeleton* self, const unsigned char* name, uint32_t* outIndex)
{
    if (StringUTF8_IsNullOrEmpty(name))
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < self->_boneCount; Index++)
    {
        if (IsBoneNamed(&self->_bones[Index], name))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    return false;
}

bool GameSkeleton_HasSameBindPoses(const GameSkeleton* a, const GameSkeleton* b)
{
    if (!GameSkeleton_HasSameBones(a, b))
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < a->_boneCount; Index++)
    {
        const float* First = &a->_bones[Index]._bind.m0;
        const float* Second = &b->_bones[Index]._bind.m0;
        for (size_t Entry = 0U; Entry < 16U; Entry++)
        {
            if (!(fabsf(First[Entry] - Second[Entry]) <= GAME_SKELETON_BIND_TOLERANCE))
            {
                return false;
            }
        }
    }
    return true;
}

bool GameSkeleton_HasSameBones(const GameSkeleton* a, const GameSkeleton* b)
{
    if (a->_boneCount != b->_boneCount)
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < a->_boneCount; Index++)
    {
        const GameSkeletonBone* First = &a->_bones[Index];
        const GameSkeletonBone* Second = &b->_bones[Index];
        size_t Length = StringUTF8_GetByteLength(First->_name);
        if ((First->_parent != Second->_parent) || (Length != StringUTF8_GetByteLength(Second->_name))
            || !Memory_IsEqual(First->_name, Second->_name, Length))
        {
            return false;
        }
    }
    return true;
}
