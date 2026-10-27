#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRMemory.h"


/**
 * Helpers for the animation tests: glTF files written in memory, so the reader's every rule can be tried with
 * exact values and hostile variations, and raylib skinned models built in code (CPU arrays only, never uploaded), so a
 * skeleton can be made without a GL context.
 */


// Types.
/** @brief One node of a test glTF file. */
typedef struct TestGltfNodeStruct
{
    /** @brief The node's name; NULL writes none. */
    const char* Name;
    /** @brief The parent node's index, or -1 for a root. */
    int Parent;
    /** @brief The node's rest pose (written as translation, rotation and scale unless HasMatrix). */
    Transform Rest;
    /** @brief Writes "matrix" (Matrix, raylib's layout) instead of the rest pose. */
    bool HasMatrix;
    /** @brief The matrix written while HasMatrix. */
    Matrix Matrix;
} TestGltfNode;

/** @brief One channel of a test clip. */
typedef struct TestGltfChannelStruct
{
    /** @brief The node it animates, or -1 for none (a channel without a target). */
    int Node;
    /** @brief "translation", "rotation", "scale" or "weights". */
    const char* Path;
    /** @brief "LINEAR", "STEP" or "CUBICSPLINE". */
    const char* Interpolation;
    /** @brief The key times (KeyCount of them). */
    const float* Times;
    /** @brief How many keys. */
    uint32_t KeyCount;
    /** @brief The values: KeyCount (three times as many for "CUBICSPLINE") values of ValueComponents floats each. */
    const float* Values;
    /** @brief Floats per value: 3 for translations and scales, 4 for rotations, 1 for a weight. */
    uint32_t ValueComponents;
    /** @brief Writes the values as normalized 16-bit integers (rotations may come so). */
    bool AreValuesShorts;
} TestGltfChannel;

/** @brief One clip of a test glTF file. */
typedef struct TestGltfClipStruct
{
    /** @brief The clip's name; NULL writes none. */
    const char* Name;
    /** @brief Its channels. */
    const TestGltfChannel* Channels;
    /** @brief How many. */
    uint32_t ChannelCount;
} TestGltfClip;

/** @brief One bone of a code-built skinned model. */
typedef struct TestSkeletonBoneStruct
{
    /** @brief The bone's name. */
    const char* Name;
    /** @brief The parent bone's index, or -1 for a root. */
    int Parent;
    /** @brief The bone's bind pose in mesh space. */
    Transform Bind;
} TestSkeletonBone;


// Functions.
/** @brief The identity transform. */
Transform TestAnimation_Identity(void);

/** @brief A transform that only translates. */
Transform TestAnimation_Translation(float x, float y, float z);

/**
 * @brief Writes a glTF 2.0 JSON file with one buffer as a base64 data URI (accessors per channel: its key times, then
 *        its values) into @p out (allocated, NUL-terminated; free its _data).
 */
void TestGltf_Write(const TestGltfNode* nodes, uint32_t nodeCount, const TestGltfClip* clips, uint32_t clipCount,
    GenericBuffer* out);

/**
 * @brief Like TestGltf_Write, but the buffer names the file @p bufferUri (written as given, so it may hold percent
 *        escapes) and its bytes go to @p outBinary (allocated; free its _data) for the caller to write there.
 */
void TestGltf_WriteWithBufferFile(const TestGltfNode* nodes, uint32_t nodeCount, const TestGltfClip* clips,
    uint32_t clipCount, const char* bufferUri, GenericBuffer* out, GenericBuffer* outBinary);

/** @brief Replaces the first occurrence of @p from in a NUL-terminated buffer with @p to; false when it is not there. */
bool TestGltf_Replace(GenericBuffer* text, const char* from, const char* to);

/**
 * @brief Builds a raylib Model in CPU memory: the skeleton and one mesh with an 8-vertex box per bone, each corner a
 *        quarter of @p boxSize from the bone's bind position and fully weighted to it, and fake GPU bone buffer ids, so
 *        GameSkeleton_Create counts the mesh as skinned. Free it with TestSkeletonModel_Destroy.
 */
Model TestSkeletonModel_Create(const TestSkeletonBone* bones, int boneCount, float boxSize);

/** @brief Frees a model made by TestSkeletonModel_Create. */
void TestSkeletonModel_Destroy(Model* model);
