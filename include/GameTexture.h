#pragma once
#include <stdbool.h>
#include <stdint.h>
// raylib.h is needed for Texture2D, which the wrapper holds by value.
#include "raylib/raylib.h"
#include "wr/WRMemory.h"

/**
 * @file GameTexture.h
 * @brief Thin wrapper around a standalone raylib texture loaded by the asset manager.
 *
 * A GameTexture is the asset the manager's "texture" type produces (load one with AssetManager_LoadTexture): one image
 * file uploaded as a GPU texture, with its definition's filtering, wrap mode and optional mipmaps applied. It is for
 * textures that stand on their own: sky panoramas, the moon, noise, normal maps, masks. (Sprites keep coming from
 * sprite sheets and sprite animations, model maps from model definitions.)
 *
 * OWNERSHIP. The asset manager owns the texture and unloads it when the asset unloads (after its last holder releases
 * it). The wrapper is shared and read-only: never call UnloadTexture on it, never change its parameters (filter, wrap)
 * through raylib, and never keep the pointer or a by-value copy of the Texture2D past your asset user.
 *
 * COLOUR SPACE. GameTexture_IsSrgb reports the definition's "color_space". It is metadata for the shader that samples
 * the texture: colour textures are sRGB-encoded and shaders linearize them themselves, data textures
 * (normal maps, masks, noise) are linear and used as they are. The GPU format is the same in both cases.
 */


/**
 * @brief A loaded standalone texture backed by a raylib Texture2D.
 *
 * Created and owned by the asset manager; valid until the asset is unloaded.
 */
typedef struct GameTextureStruct
{
    /** @brief The wrapped raylib texture (its size, mip count and format are readable); owned by the asset manager. */
    Texture2D _rayTexture;
    /** @brief Whether the texels are sRGB-encoded colour (true) or linear data (false), from "color_space". */
    bool _isSrgb;
} GameTexture;


/**
 * @brief Returns the underlying raylib texture for drawing or binding.
 *
 * The returned Texture2D is a by-value copy of the asset's handle: it names the same GPU texture and is valid until the
 * asset is unloaded. Do not unload it or change its parameters.
 * @param self The texture to read; must not be NULL.
 * @returns The wrapped raylib texture by value.
 */
static inline Texture2D GameTexture_GetRaylibTexture(const GameTexture* self)
{
    return self->_rayTexture;
}

/**
 * @brief Reports whether the texture holds sRGB-encoded colour (to linearize in the shader) or linear data.
 * @param self The texture to read; must not be NULL.
 * @returns true for "color_space": "srgb" (the default), false for "linear".
 */
static inline bool GameTexture_IsSrgb(const GameTexture* self)
{
    return self->_isSrgb;
}


/**
 * @brief A texture asset's image decoded into CPU memory as 8-bit RGBA, for code that needs the pixels themselves rather
 *        than a GPU texture (the terrain materials, whose maps become texture arrays).
 *
 * Filled by AssetManager_LoadTexturePixels, which reads the image file the texture's definition names without loading the
 * texture asset (no hold, nothing uploaded). The caller owns the pixels: release them with TexturePixels_Deconstruct.
 * The definition's texture_properties (filtering, wrap, mipmaps) do not apply: they describe the GPU texture.
 */
typedef struct TexturePixelsStruct
{
    /** @brief Width x Height texels of 4 bytes each (red, green, blue, alpha), row by row from the image's top row; a
     *  greyscale image's grey is in red, green and blue, an image without alpha has alpha 255. Owned (Memory_Allocate). */
    uint8_t* Pixels;
    /** @brief The image's width and height in pixels (1 to the reader's limit: the caller's, at most the texture
     *  type's 8192). */
    int32_t Width;
    int32_t Height;
    /** @brief The definition's "color_space": true for "srgb" (the default), false for "linear". */
    bool IsSrgb;
} TexturePixels;

/**
 * @brief Frees the pixels and empties the struct. Safe on a zeroed or already deconstructed struct.
 * @param self The pixels to release; must not be NULL.
 */
static inline void TexturePixels_Deconstruct(TexturePixels* self)
{
    Memory_Free(self->Pixels);
    self->Pixels = NULL;
    self->Width = 0;
    self->Height = 0;
}
