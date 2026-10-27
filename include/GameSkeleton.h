#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Model, Matrix, Transform and BoundingBox are raylib types: a skeleton is built from a loaded raylib Model and keeps
// raylib matrices and transforms, so this include is unavoidable here.
#include "raylib/raylib.h"
#include "Logger.h"
#include "wr/WRError.h"


/**
 * @file GameSkeleton.h
 * @brief The skeleton of a skinned model: its bones, their rest and bind poses, and what the renderer and the
 *        animation need to bend its meshes.
 *
 * WHAT. A model asset (GameModel.h) whose file has a skin (a glTF armature, an IQM or M3D skeleton) gets a skeleton when
 * it loads: raylib's bones (their names, the first GAME_SKELETON_BONE_NAME_CAPACITY - 1 bytes, and their parents) and
 * bind poses, turned into what animation reads every tick:
 *  - each bone's rest pose relative to its parent (from the bind poses: a skeleton's rest pose is the pose its mesh was
 *    bound in), and its rest position in mesh space;
 *  - each bone's bind matrix and its inverse (mesh space to the bone's space at bind time), so a pose's skinning matrix
 *    is the bone's posed matrix times its inverse bind matrix;
 *  - an evaluation order in which every bone comes after its parent (a file may list a child first);
 *  - each bone's box around the vertices it moves, in mesh space at bind time, so a pose's bounds are cheap (every
 *    vertex lies in the boxes of the bones it follows, moved by their skinning matrices);
 *  - which meshes the GPU can skin: those with bone weights whose bone buffers raylib uploaded (it does when built with
 *    SUPPORT_GPU_SKINNING, BUILDING.md) and whose bone indices all name bones of the skeleton.
 *
 * MEMORY. One block, made by GameSkeleton_Create from a loaded raylib Model (its CPU arrays: bones, bind poses, the
 * meshes' bone indices and weights) and freed by GameSkeleton_Destroy; the model asset owns its skeleton. Immutable
 * once made.
 *
 * LIMITS. Bind poses are raylib's: decomposed by raylib into a translation, a rotation and a scale, so a bone bound
 * with a non-uniform scale under a rotation is not kept exactly (raymath's decomposition reads that case wrong).
 */


// Macros.
/** @brief Room for a bone's name with its terminator: raylib keeps 31 bytes of each (BoneInfo.name). */
#define GAME_SKELETON_BONE_NAME_CAPACITY ((size_t)32)
/** @brief The most bones a skeleton may have: raylib's meshes keep bone indices as bytes. */
#define GAME_SKELETON_MAX_BONES ((size_t)256)
/** @brief How far two bind matrices' entries may differ and still be the same bind pose (GameSkeleton_HasSameBindPoses):
 *  a float's rounding of the same armature exported twice, well below a millimetre or a thousandth of a turn. */
#define GAME_SKELETON_BIND_TOLERANCE 1e-4f


// Types.
/**
 * @brief One bone of a skeleton. Read-only; part of its skeleton's block.
 */
typedef struct GameSkeletonBoneStruct
{
    /** @brief The bone's name as raylib keeps it (at most GAME_SKELETON_BONE_NAME_CAPACITY - 1 bytes), NUL-terminated. */
    unsigned char _name[GAME_SKELETON_BONE_NAME_CAPACITY];
    /** @brief The parent bone's index, or -1 for a root. */
    int32_t _parent;
    /** @brief The rest pose relative to the parent (a root's: in mesh space): translation, rotation (unit length),
     *  scale. */
    Transform _rest;
    /** @brief The bone's rest position in mesh space. */
    Vector3 _restPosition;
    /** @brief The bone's matrix at bind time: its space to mesh space (raymath order). */
    Matrix _bind;
    /** @brief The inverse of _bind: mesh space to the bone's space at bind time. */
    Matrix _inverseBind;
    /** @brief Whether any vertex of a skinnable mesh follows the bone (with a weight above 0). */
    bool _hasVertices;
    /** @brief The box around those vertices, in mesh space at bind time; valid while _hasVertices. */
    BoundingBox _vertexBounds;
} GameSkeletonBone;

/**
 * @brief A skinned model's skeleton. Read-only; see the file comment.
 */
typedef struct GameSkeletonStruct
{
    /** @brief The bones, in raylib's order (the order the meshes' bone indices use). */
    const GameSkeletonBone* _bones;
    /** @brief How many bones (1 to GAME_SKELETON_MAX_BONES). */
    uint32_t _boneCount;
    /** @brief Every bone's index once, each after its parent's. */
    const uint16_t* _order;
    /** @brief One flag per mesh of the model: whether the GPU can skin it (see the file comment). */
    const bool* _isMeshSkinned;
    /** @brief How many meshes the model has (the length of _isMeshSkinned). */
    uint32_t _meshCount;
    /** @brief How many of them the GPU can skin. */
    uint32_t _skinnedMeshCount;
    /** @brief Whether the model has vertices that do not bend (meshes the GPU does not skin: they draw as bound). */
    bool _hasStaticBounds;
    /** @brief The box around those vertices in mesh space; valid while _hasStaticBounds. Every pose's bounds hold it. */
    BoundingBox _staticBounds;
} GameSkeleton;


// Functions.
/**
 * @brief Builds the skeleton of a loaded raylib model, or none when the model has no bones.
 *
 * Reads only CPU data (the skeleton, its bind poses and the meshes' vertices, bone indices and weights). A mesh whose
 * bone indices name a bone past the skeleton, or whose weights are not finite, is not skinned (it draws as it is
 * bound) and is reported once through @p logger; so is a model with bone weights that raylib did not upload (built
 * without SUPPORT_GPU_SKINNING).
 * @param model The model (borrowed for the call); must not be NULL.
 * @param description What messages call the model (e.g. u8"model \"human\""); may be NULL.
 * @param logger Where the warnings go; may be NULL.
 * @param outSkeleton [out] Receives the skeleton (owned by the caller: GameSkeleton_Destroy), or NULL when the model has
 *        no bones.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_InvalidAssetData for a broken skeleton (more
 *          than GAME_SKELETON_MAX_BONES bones, a parent that is not a bone or that makes a cycle, a bind pose that is not
 *          finite or not invertible).
 */
Error GameSkeleton_Create(const Model* model, const unsigned char* description, Logger* logger,
    GameSkeleton** outSkeleton);

/**
 * @brief Frees a skeleton made by GameSkeleton_Create.
 * @param self The skeleton, or NULL (nothing happens).
 */
void GameSkeleton_Destroy(GameSkeleton* self);

/** @brief Returns how many bones the skeleton has. @param self The skeleton; must not be NULL. */
static inline uint32_t GameSkeleton_GetBoneCount(const GameSkeleton* self)
{
    return self->_boneCount;
}

/**
 * @brief Returns one bone of the skeleton.
 * @param self The skeleton; must not be NULL.
 * @param index The bone's index, below GameSkeleton_GetBoneCount.
 * @returns The bone (part of the skeleton).
 */
static inline const GameSkeletonBone* GameSkeleton_GetBone(const GameSkeleton* self, uint32_t index)
{
    return &self->_bones[index];
}

/**
 * @brief Returns the evaluation order: every bone's index once, each after its parent's.
 * @param self The skeleton; must not be NULL.
 * @returns GameSkeleton_GetBoneCount indices (part of the skeleton).
 */
static inline const uint16_t* GameSkeleton_GetOrder(const GameSkeleton* self)
{
    return self->_order;
}

/**
 * @brief Returns whether the GPU can skin one of the model's meshes.
 * @param self The skeleton; must not be NULL.
 * @param meshIndex The mesh's index in the model; past the model's meshes reads false.
 * @returns Whether the mesh bends with the skeleton (otherwise it draws as it is bound).
 */
static inline bool GameSkeleton_IsMeshSkinned(const GameSkeleton* self, uint32_t meshIndex)
{
    return (meshIndex < self->_meshCount) && self->_isMeshSkinned[meshIndex];
}

/** @brief Returns how many of the model's meshes the GPU can skin. @param self The skeleton; must not be NULL. */
static inline uint32_t GameSkeleton_GetSkinnedMeshCount(const GameSkeleton* self)
{
    return self->_skinnedMeshCount;
}

/**
 * @brief Finds a bone by its name (compared as raylib keeps names: their first GAME_SKELETON_BONE_NAME_CAPACITY - 1
 *        bytes).
 * @param self The skeleton; must not be NULL.
 * @param name The name; NULL or empty finds nothing.
 * @param outIndex [out] Receives the first bone of that name when found; may be NULL.
 * @returns Whether a bone has that name.
 */
bool GameSkeleton_TryFindBone(const GameSkeleton* self, const unsigned char* name, uint32_t* outIndex);

/**
 * @brief Whether two skeletons have the same bones: the same count and, bone for bone, the same names and parents (a
 *        world model's skinned LODs must, so one pose drives them all).
 * @param a The first skeleton; must not be NULL.
 * @param b The second skeleton; must not be NULL.
 * @returns Whether they match.
 */
bool GameSkeleton_HasSameBones(const GameSkeleton* a, const GameSkeleton* b);

/**
 * @brief Whether two skeletons have the same bones (GameSkeleton_HasSameBones) bound the same way: every bind matrix
 *        within GAME_SKELETON_BIND_TOLERANCE of the other's, entry for entry. The LODs of one world model must, so one
 *        pose's palette skins them all.
 * @param a The first skeleton; must not be NULL.
 * @param b The second skeleton; must not be NULL.
 * @returns Whether they match.
 */
bool GameSkeleton_HasSameBindPoses(const GameSkeleton* a, const GameSkeleton* b);
