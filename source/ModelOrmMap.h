#pragma once
#include "wr/WRError.h"
#include "raylib/raylib.h"

/*
 * Private header (source/): the packed occlusion/roughness/metallic map a model material's METALNESS slot carries (red
 * the ambient occlusion, green the roughness, blue the metallic: the PBR shader's mraMap), built on the CPU from separate
 * maps.
 *
 * raylib's glTF importer leaves the three apart: it splits the file's metallicRoughness texture into two grey textures
 * (its green as the roughness, its blue as the metallic) and loads the occlusion texture as the file has it (the ambient
 * occlusion in red; Blender often writes one image for both). The model loader reads those textures back, packs them
 * here and uploads the result (ModelDefinition.c). Pure: no GL, so tests run it headless.
 */


// Macros.
/** @brief The largest width or height, in pixels, of a packed map and of every map it is built from: the texture
 *         type's limit. */
#define MODEL_ORM_MAP_MAX_SIDE 8192


// Types.
/**
 * @brief The maps one material's packed map is built from, each read from its red channel (a grey image's grey).
 *
 * NULL is a missing map: its channel is 255 everywhere, so the material's factor applies alone. Each given image must
 * hold 8-bit pixels: grey, grey with alpha, RGB or RGBA (what raylib decodes a png, bmp, gif or qoi file into, and the
 * format of its grey splits). The images are borrowed for the call.
 */
typedef struct ModelOrmSourcesStruct
{
    /** @brief The ambient occlusion (glTF's occlusion texture: its red), or NULL. */
    const Image* Occlusion;
    /** @brief The roughness (raylib's grey split of glTF's metallicRoughness green), or NULL. */
    const Image* Roughness;
    /** @brief The metallic (raylib's grey split of its blue), or NULL. */
    const Image* Metallic;
} ModelOrmSources;


// Functions.
/**
 * @brief Packs up to three maps into one image: red the occlusion, green the roughness, blue the metallic, alpha 255.
 *
 * The image is the roughness map's size, else the metallic map's, else the occlusion map's; a map of another size is
 * resampled to it (raylib's ImageResize, on the map's values as they are). The occlusion is weakened by
 * @p occlusionStrength as glTF defines it: 1 + strength x (occlusion - 1), rounded to the nearest byte. Allocates the
 * image and, for each map, a grey copy of its red channel while it is packed.
 * @param sources The maps; at least one must be given.
 * @param occlusionStrength How much of the occlusion map applies (glTF's occlusion strength), in [0, 1]: 1 takes it as
 *        it is, 0 leaves red 255.
 * @param outImage [out] The packed image: PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, one mip level, in raylib's memory (free it
 *                 with UnloadImage). Zeroed on failure.
 * @returns Success; ErrorCode_IllegalArgument when every map is NULL or the strength is not in [0, 1];
 *          ErrorCode_InvalidAssetData for a map without pixels, in another pixel format, or wider or taller than
 *          MODEL_ORM_MAP_MAX_SIDE (the message names the map); ErrorCode_BufferTooLarge when a grey copy could not be
 *          allocated.
 */
Error ModelOrmMap_Build(const ModelOrmSources* sources, float occlusionStrength, Image* outImage);
