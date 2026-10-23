#pragma once
#include <stdbool.h>
// raylib.h is needed for Model and Mesh, which these wrappers hold by value, and for the Color and MaterialMap of the
// material factors.
#include "raylib/raylib.h"
#include "GameSkeleton.h"

/**
 * @file GameModel.h
 * @brief Thin wrappers around a Raylib Model and Mesh loaded by the asset manager, and the PBR factors a material
 *        override sets.
 *
 * A GameModel holds a loaded Raylib Model (meshes + materials, with its import transform baked into the
 * Model's transform matrix). It is the static-state model asset produced by the asset manager's model type
 * (load one with AssetManager_LoadModel). A model whose file has a skin also holds its skeleton (GameSkeleton.h, T29):
 * what a pose needs to bend its meshes, which are then drawn by the world renderer's skinned shaders. The asset is never
 * posed itself (raylib's UpdateModelAnimation would pose the shared model for everyone): each animated object keeps its
 * own pose (WorldAnimatorComponent.h).
 *
 * GameMaterialFactors are the scalar part of a material override: the model type's "material_overrides" apply them to
 * the model's own materials while it loads, and world model variants (WorldModel.h) to the renderer's per-draw copy of
 * a material. Both read them with one parser and apply them with GameMaterialFactors_Apply.
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
    /** @brief The skeleton of a skinned model (owned by the asset, freed with it), or NULL for a model without bones. */
    GameSkeleton* _skeleton;
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
 * @brief The PBR factors a material override sets on top of a model's material.
 *
 * Each factor has its own flag: a factor whose flag is false leaves the material's value as it is. A plain value type;
 * the model type's "material_overrides" and world model variants read it from their definitions and apply it with
 * GameMaterialFactors_Apply.
 */
typedef struct GameMaterialFactorsStruct
{
    /** @brief Whether Tint replaces the albedo colour. */
    bool HasTint;
    /** @brief The albedo colour (multiplies the albedo texture; the object's tint multiplies it in turn). */
    Color Tint;
    /** @brief Whether Metallic replaces the metallic factor. */
    bool HasMetallic;
    /** @brief The metallic factor, in [0, 1] (multiplies a packed ORM map's blue channel). */
    float Metallic;
    /** @brief Whether Roughness replaces the roughness factor. */
    bool HasRoughness;
    /** @brief The roughness factor, in [0, 1] (multiplies a packed ORM map's green channel). */
    float Roughness;
    /** @brief Whether EmissiveColor replaces the emissive colour. */
    bool HasEmissiveColor;
    /** @brief The emissive colour (multiplies an emissive map). */
    Color EmissiveColor;
    /** @brief Whether EmissiveIntensity replaces the emissive intensity. */
    bool HasEmissiveIntensity;
    /** @brief The emissive intensity, >= 0. */
    float EmissiveIntensity;
} GameMaterialFactors;


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

/**
 * @brief Returns the model's skeleton (GameSkeleton.h): its bones and which of its meshes bend with them.
 * @param self The game model to read; must not be NULL.
 * @returns The skeleton (owned by the asset, valid while it is loaded), or NULL for a model without bones.
 */
static inline const GameSkeleton* GameModel_GetSkeleton(const GameModel* self)
{
    return self->_skeleton;
}

/**
 * @brief Applies material factors to a material's maps: each factor whose flag is set replaces its map's value.
 *
 * Tint goes to the albedo map's colour, Metallic and Roughness to the metalness and roughness maps' values,
 * EmissiveColor and EmissiveIntensity to the emission map's colour and value. An override that brings an emissive map
 * (@p hasEmissiveMap) but no emissive colour or intensity of its own shows that map at white x 1, since the importer's
 * default intensity of 0 would hide it. Writes only to @p maps: give it the model's own materials while the model
 * loads, or the renderer's per-draw copy, never a loaded asset's maps.
 * @param self The factors; must not be NULL.
 * @param hasEmissiveMap Whether the same override set an emissive map.
 * @param maps [in,out] A material's maps, indexed by MaterialMapIndex (raylib's MAX_MATERIAL_MAPS entries); must not be
 *        NULL.
 */
void GameMaterialFactors_Apply(const GameMaterialFactors* self, bool hasEmissiveMap, MaterialMap* maps);
