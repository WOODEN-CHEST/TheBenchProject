#pragma once
#include "raylib/raylib.h"

/**
 * @file GameShader.h
 * @brief Thin wrapper around a Raylib Shader loaded by the asset manager.
 *
 * A GameShader holds a compiled Raylib Shader. It is the static-state shader asset produced by the asset
 * manager's shader type. The asset manager owns the wrapped Shader and unloads it when the asset is
 * unloaded; do not call UnloadShader on the handle returned by GameShader_GetRaylibShader.
 */


/**
 * @brief A loaded shader backed by a Raylib Shader.
 *
 * Created and owned by the asset manager. The Raylib Shader is stored by value and released by whoever
 * loaded it (the asset manager) when the asset unloads.
 */
typedef struct GameShaderStruct
{
    /** @brief The wrapped Raylib shader; owned by the asset manager, not this wrapper. */
    Shader _rayShader;
} GameShader;


/**
 * @brief Returns the underlying Raylib Shader for rendering.
 *
 * The returned Shader is borrowed and remains valid until the asset is unloaded; do not unload it.
 * @param self The game shader to read; must not be NULL.
 * @returns The wrapped Raylib Shader by value.
 */
static inline Shader GameShader_GetRaylibShader(GameShader* self)
{
    return self->_rayShader;
}
