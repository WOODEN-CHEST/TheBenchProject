#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "raylib/raylib.h"

/*
 * Private header (source/): what a glTF file says about its materials that raylib's importer drops, read with cgltf
 * after source/GltfChecks.h checked the file as hostile. The model loader (source/ModelDefinition.c) reads a glTF
 * model's file with it BEFORE raylib does, so raylib's importer, which trusts every number, only ever reads a file that
 * passed those checks; the loader then applies the values to raylib's materials (the file's material i is raylib's slot
 * i + 1).
 *
 * What raylib drops: a material's metallic and roughness factors without a metallicRoughness texture, its emissive
 * factor without an emissive texture, the emission's strength (it leaves the intensity 0), the occlusion strength, and
 * every texture of a material without a pbrMetallicRoughness block. It also copies colour factors as they are, while
 * glTF's are linear and the game's material colours are sRGB-encoded (the renderer linearizes them with the gamma
 * MODEL_GLTF_COLOR_GAMMA): ModelGltfMaterials_ToColor encodes them. Pure: no GL, so tests run it headless.
 */


// Macros.
/** @brief The most bytes a model's glTF file and its buffer files may hold together. */
#define MODEL_GLTF_MAX_FILE_BYTES ((size_t)256 * 1024 * 1024)
/** @brief The gamma the game's material colours are encoded with: the renderer raises them to it to linearize them. */
#define MODEL_GLTF_COLOR_GAMMA 2.2f


// Types.
/** @brief One material of a glTF file, its values checked against glTF's ranges. */
typedef struct ModelGltfMaterialStruct
{
    /** @brief The base colour factor: linear red, green, blue and alpha, each in [0, 1]. */
    float BaseColor[4];
    /** @brief The metallic factor, in [0, 1]. */
    float Metallic;
    /** @brief The roughness factor, in [0, 1]. */
    float Roughness;
    /** @brief The emissive factor: linear red, green and blue, each in [0, 1]. */
    float Emissive[3];
    /** @brief The emission's strength (KHR_materials_emissive_strength; 1 without it): finite and at least 0. */
    float EmissiveStrength;
    /** @brief How much of the occlusion texture applies (its strength; 1 without one), in [0, 1]. */
    float OcclusionStrength;
    /** @brief Whether the material has textures raylib's importer leaves out: it has no pbrMetallicRoughness block,
     *  and raylib reads a material's textures only inside one. */
    bool HasSkippedTextures;
} ModelGltfMaterial;

/**
 * @brief A glTF file's materials, in the file's order.
 *
 * Filled by ModelGltfMaterials_Read or _ReadFile, released with ModelGltfMaterials_Deconstruct (safe on a zeroed or
 * already released struct).
 */
typedef struct ModelGltfMaterialsStruct
{
    /** @brief The materials; owned (one allocation); NULL when the file has none. */
    ModelGltfMaterial* Materials;
    /** @brief How many. */
    size_t Count;
} ModelGltfMaterials;


// Functions.
/**
 * @brief Reads the materials of the glTF (.gltf or .glb) file at @p path, at most MODEL_GLTF_MAX_FILE_BYTES with its
 *        buffer files, checked as hostile (source/GltfChecks.h) before any number is trusted.
 * @param path The file's path (UTF-8); its buffer files are found beside it.
 * @param description What messages call the file, e.g. "Model \"bench\""; must not be NULL.
 * @param outMaterials [out] The materials; zeroed on failure.
 * @returns Success; ErrorCode_InvalidAssetData for a file the checks refuse or a material value outside glTF's range
 *          (the message names the material and the value); the file read's errors (ErrorCode_FileNotFound,
 *          ErrorCode_IO, ErrorCode_BufferTooLarge past the limit).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ModelGltfMaterials_ReadFile(const unsigned char* path, const char* description, ModelGltfMaterials* outMaterials);

/**
 * @brief Reads the materials of a glTF file held in memory, as ModelGltfMaterials_ReadFile does.
 * @param data The file's bytes; borrowed for the call.
 * @param byteCount How many.
 * @param description What messages call the file; must not be NULL.
 * @param filePath The file's path, for buffer files beside it; NULL when there is none (such a buffer then fails).
 * @param outMaterials [out] The materials; zeroed on failure.
 * @returns As ModelGltfMaterials_ReadFile.
 */
Error ModelGltfMaterials_Read(const void* data, size_t byteCount, const char* description,
    const unsigned char* filePath, ModelGltfMaterials* outMaterials);

/** @brief Frees the materials and zeroes the struct. */
void ModelGltfMaterials_Deconstruct(ModelGltfMaterials* self);

/**
 * @brief A linear glTF colour factor as the game's material colours hold it: red, green and blue raised to
 *        1 / MODEL_GLTF_COLOR_GAMMA (so the renderer's linearization gives the factor back), alpha as it is, each to
 *        the nearest byte. Values outside [0, 1] (and NaN, as 0) are clamped.
 * @param linear The linear red, green and blue.
 * @param alpha The alpha.
 * @returns The colour.
 */
Color ModelGltfMaterials_ToColor(const float linear[3], float alpha);
