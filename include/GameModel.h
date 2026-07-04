#pragma once
#include "raylib/raylib.h"

/**
 * @file GameModel.h
 * @brief Thin wrappers around a Raylib Model and Mesh loaded by the asset manager.
 *
 * A GameModel holds a loaded Raylib Model (meshes + materials, with its import transform baked into the
 * Model's transform matrix). It is the static-state model asset produced by the asset manager's model
 * type. The asset manager owns the wrapped Model (and any override textures) and unloads them when the
 * asset is unloaded; do not call UnloadModel on the handle returned by GameModel_GetRaylibModel.
 */


/**
 * @brief A loaded 3D model backed by a Raylib Model.
 *
 * Created and owned by the asset manager. The Raylib Model is stored by value and released by whoever
 * loaded it (the asset manager) when the asset unloads.
 */
typedef struct GameModelStruct
{
    /** @brief The wrapped Raylib model; owned by the asset manager, not this wrapper. */
    Model _rayModel;
} GameModel;

/**
 * @brief A standalone mesh backed by a Raylib Mesh.
 */
typedef struct GameMeshStruct
{
    /** @brief The wrapped Raylib mesh. */
    Mesh _rayMesh;
} GameMesh;


/**
 * @brief Returns the underlying Raylib Model for rendering.
 *
 * The returned Model is borrowed and remains valid until the asset is unloaded; do not unload it.
 * @param self The game model to read; must not be NULL.
 * @returns The wrapped Raylib Model by value.
 */
static inline Model GameModel_GetRaylibModel(GameModel* self)
{
    return self->_rayModel;
}
