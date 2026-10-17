#pragma once
// raylib.h is needed for Model and Mesh, which these wrappers hold by value.
#include "raylib/raylib.h"

/**
 * @file GameModel.h
 * @brief Thin wrappers around a Raylib Model and Mesh loaded by the asset manager.
 *
 * A GameModel holds a loaded Raylib Model (meshes + materials, with its import transform baked into the
 * Model's transform matrix). It is the static-state model asset produced by the asset manager's model type
 * (load one with AssetManager_LoadModel).
 *
 * OWNERSHIP. The asset manager owns everything the asset loaded: the meshes, each material's map array, and
 * every GPU texture referenced by a material map at load time (textures the OBJ/MTL or glTF importer loaded,
 * plus the "material_overrides" maps from the definition). When the asset unloads, the manager unloads those
 * textures and then calls UnloadModel. raylib's UnloadModel alone frees only meshes and map arrays, never
 * textures or shaders, which is why the manager tracks the textures itself. The asset never owns a material's
 * shader. Callers therefore must not call UnloadModel, UnloadMaterial or UnloadTexture on anything reached
 * through GameModel_GetRaylibModel (UnloadMaterial would also unload the material's shader, and a texture
 * shared by two materials twice). The asset is shared and read-only: draw with copies (a Material built on
 * the stack) instead of writing tints, shaders or maps into its materials.
 */


/**
 * @brief A loaded 3D model backed by a Raylib Model.
 *
 * Created and owned by the asset manager, and valid until the asset is unloaded (after its last holder
 * releases it). The Raylib Model is stored by value; see the file comment for what the manager unloads.
 */
typedef struct GameModelStruct
{
    /** @brief The wrapped Raylib model (meshes, materials and their textures); owned by the asset manager,
     *         not by this wrapper or its users. */
    Model _rayModel;
    /** @brief Axis-aligned bounds of all meshes in mesh space (before the import transform), computed once at
     *         load. */
    BoundingBox _localBounds;
} GameModel;

/**
 * @brief A standalone mesh backed by a Raylib Mesh.
 *
 * Not produced by the asset manager. Whoever builds one owns the mesh and releases it with UnloadMesh.
 */
typedef struct GameMeshStruct
{
    /** @brief The wrapped Raylib mesh; owned by whoever built the GameMesh. */
    Mesh _rayMesh;
} GameMesh;


/**
 * @brief Returns the underlying Raylib Model for rendering.
 *
 * The returned Model is a by-value copy whose pointers (meshes, materials, map arrays) alias the asset's
 * storage. It is valid until the asset is unloaded. Do not unload it or anything it references (meshes,
 * materials, textures); see the file comment.
 * @param self The game model to read; must not be NULL.
 * @returns The wrapped Raylib Model by value.
 */
static inline Model GameModel_GetRaylibModel(GameModel* self)
{
    return self->_rayModel;
}

/**
 * @brief Returns the axis-aligned bounds of all the model's meshes in mesh space.
 *
 * The bounds are taken before the model's import transform (Model.transform) and were computed once, when the
 * model loaded, so this is cheap; transform them yourself (for example into a world-space bounding sphere).
 * @param self The game model to read; must not be NULL.
 * @returns The mesh-space bounding box. All zero for a model without vertex data.
 */
static inline BoundingBox GameModel_GetLocalBounds(const GameModel* self)
{
    return self->_localBounds;
}
