#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "WorldModel.h"


/**
 * @file WorldGeneratedModel.h
 * @brief Meshes a generator made while the game runs (a bent hedge, a cut rail), which a model component draws in place of its
 *        world model's: LOD by LOD, each LOD's meshes with their material slots. No GL: arrays in CPU memory, which the renderer
 *        uploads and draws in every pass as it draws any model.
 *
 * WHAT STAYS THE WORLD MODEL'S. The model component still names a world model, and that is what gives a generated model its
 * materials (each mesh names a material slot of the same LOD's model), its variants, its LOD distances and fades and its render
 * defaults, with every override the component holds. Only which meshes are drawn and their bounds change. A generated model has
 * as many LODs as its world model, and its positions are in its object's own space, import transform and all.
 *
 * Meshes are unindexed: three vertices a triangle. Hold one with WorldModelComponent_SetGeneratedModel, which takes it over.
 */


// Types.
/** @brief One generated mesh: arrays as raylib's Mesh holds them, owned by the generated model. Any but the positions and the
 *  normals may be NULL. */
typedef struct WorldGeneratedMeshStruct
{
    /** @brief The material slot of the LOD's model it draws with. */
    int32_t MaterialSlot;
    /** @brief How many vertices: three a triangle. */
    uint32_t VertexCount;
    /** @brief Three floats a vertex. */
    float* Positions;
    float* Normals;
    /** @brief Four floats a vertex. */
    float* Tangents;
    /** @brief Two floats a vertex. */
    float* Texcoords;
    float* Texcoords2;
    /** @brief Four bytes a vertex. */
    unsigned char* Colors;
} WorldGeneratedMesh;

/** @brief One LOD of a generated model. */
typedef struct WorldGeneratedLodStruct
{
    /** @brief Its meshes (owned), and how many. */
    WorldGeneratedMesh* Meshes;
    uint32_t MeshCount;
} WorldGeneratedLod;

/** @brief A generated model. Fill it through its functions, then hand it to a model component. */
typedef struct WorldGeneratedModelStruct
{
    /** @brief The LODs, the finest first: as many as the world model has. */
    WorldGeneratedLod Lods[WORLD_MODEL_MAX_LODS];
    size_t LodCount;
    /** @brief The box of LOD 0's positions, in the object's space, and the sphere around it. */
    BoundingBox Box;
    Vector3 Center;
    float Radius;
} WorldGeneratedModel;


// Functions.
/**
 * @brief Makes an empty generated model of a number of LODs.
 * @param lodCount How many LODs, 1 to WORLD_MODEL_MAX_LODS (clamped).
 * @returns The model (the caller owns it until it hands it over).
 */
WorldGeneratedModel* WorldGeneratedModel_Create(size_t lodCount);

/**
 * @brief Releases a generated model and its meshes.
 * @param self The model, or NULL.
 */
void WorldGeneratedModel_Destroy(WorldGeneratedModel* self);

/**
 * @brief Adds a mesh to a LOD: the model takes over the arrays, each allocated with Memory_Allocate.
 * @param self The model; must not be NULL.
 * @param lod The LOD, below its count.
 * @param mesh The mesh, its arrays handed over. Must not be NULL.
 * @returns true; false for a LOD out of range (the arrays stay the caller's).
 */
bool WorldGeneratedModel_AddMesh(WorldGeneratedModel* self, size_t lod, const WorldGeneratedMesh* mesh);

/**
 * @brief Finishes a model: its box and sphere from LOD 0's positions (from every LOD's while LOD 0 has none).
 * @param self The model; must not be NULL.
 */
void WorldGeneratedModel_UpdateBounds(WorldGeneratedModel* self);

/**
 * @brief Whether two generated models hold the same meshes, byte for byte.
 * @param a A model, or NULL.
 * @param b A model, or NULL.
 * @returns true when equal (NULL equals only NULL).
 */
bool WorldGeneratedModel_Equals(const WorldGeneratedModel* a, const WorldGeneratedModel* b);
