#pragma once
#include <stdbool.h>
// raylib.h is needed for Texture2D, which the wrapper holds by value.
#include "raylib/raylib.h"

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
 * the texture: colour textures are sRGB-encoded and shaders linearize them themselves (as they do today), data textures
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
