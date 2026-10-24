#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "TestFramework.h"
#include "TerrainMesh.h"
#include "raylib/raylib.h"
#include "wr/WRMemory.h"


/**
 * Helpers shared by the terrain suites (the mesh, its import, the asset, collision and the renderer's cache): terrains
 * built as grids of quads with heights from a function, their arrays as a TerrainMeshData, a comparison of two meshes bit
 * for bit, and a writer of small binary glTF files (one mesh, chosen attributes and material slots) for the import tests.
 *
 * A grid of CellsX x CellsZ quads of Spacing metres from (OriginX, OriginZ): vertex (i, j) is index j * (CellsX + 1) + i
 * at (OriginX + i * Spacing, height, OriginZ + j * Spacing); quad (i, j) is the triangles 2 * (j * CellsX + i) and the one
 * after, counter-clockwise seen from above (their fronts face up): (v(i,j), v(i,j+1), v(i+1,j)) and (v(i+1,j), v(i,j+1),
 * v(i+1,j+1)).
 */


// Types.
/** @brief A height for a point of a grid (metres). */
typedef float (*TestTerrainHeightFunction)(float x, float z);

/** @brief A grid terrain's arrays (owned; TestTerrain_FreeGrid releases them). The weights give every vertex material 0
 *  fully; densities, flags and materials start at 0. Change them before building a mesh from them. */
typedef struct TestTerrainGridStruct
{
    Vector3* Positions;
    Vector3* Normals;
    uint8_t* Weights;
    uint8_t* Densities;
    uint32_t VertexCount;
    uint32_t* Indices;
    uint8_t* Flags;
    uint8_t* Materials;
    uint32_t TriangleCount;
    uint32_t CellsX;
    uint32_t CellsZ;
    float Spacing;
    float OriginX;
    float OriginZ;
} TestTerrainGrid;

/** @brief One attribute of a glTF file the writer adds to every primitive: its name, its channel count (1, 3 or 4), its
 *  component type (5121 unsigned byte, 5123 unsigned short, 5126 float; the integers normalized) and its values (Count x
 *  ChannelCount floats in 0..1, converted to the type; borrowed). */
typedef struct TestGltfAttributeStruct
{
    const char* Name;
    uint32_t ChannelCount;
    uint32_t ComponentType;
    const float* Values;
} TestGltfAttribute;

/** @brief A glTF file the writer makes: one mesh, one primitive per material slot (each a range of the triangles), its
 *  node with a translation, and attributes. Everything is borrowed. */
typedef struct TestGltfMeshStruct
{
    /** @brief The positions (and, with HasNormals, the normals) of VertexCount vertices. */
    const Vector3* Positions;
    const Vector3* Normals;
    bool HasNormals;
    uint32_t VertexCount;
    /** @brief The attributes. */
    const TestGltfAttribute* Attributes;
    size_t AttributeCount;
    /** @brief Three indices per triangle, written as unsigned shorts while VertexCount fits, else unsigned ints. */
    const uint32_t* Indices;
    uint32_t TriangleCount;
    /** @brief The primitives: each takes PrimitiveTriangleCounts[p] triangles after the previous one's, with the material
     *  named MaterialNames[p] (NULL: none). PrimitiveModes[p] is its glTF mode (4 triangles); NULL modes: all 4. */
    const char* const* MaterialNames;
    const uint32_t* PrimitiveTriangleCounts;
    const uint32_t* PrimitiveModes;
    size_t PrimitiveCount;
    /** @brief The node's translation and uniform scale (1 for none). */
    Vector3 NodeTranslation;
    float NodeScale;
    /** @brief Whether the mesh's node hangs under a parent node with this translation and rotation (a quaternion: x,
     *  y, z, w). */
    bool HasParent;
    Vector3 ParentTranslation;
    float ParentRotation[4];
} TestGltfMesh;


// Functions.
/** @brief Builds a grid (see the file comment); @p height may be NULL (flat at 0). */
void TestTerrain_BuildGrid(TestTerrainGrid* outGrid, uint32_t cellsX, uint32_t cellsZ, float spacing, float originX,
    float originZ, TestTerrainHeightFunction height);

/** @brief Releases a grid's arrays. */
void TestTerrain_FreeGrid(TestTerrainGrid* grid);

/** @brief The vertex index of grid point (i, j). */
static inline uint32_t TestTerrain_Vertex(const TestTerrainGrid* grid, uint32_t i, uint32_t j)
{
    return (j * (grid->CellsX + 1U)) + i;
}

/** @brief The first of the two triangles of quad (i, j). */
static inline uint32_t TestTerrain_Triangle(const TestTerrainGrid* grid, uint32_t i, uint32_t j)
{
    return 2U * ((j * grid->CellsX) + i);
}

/** @brief A grid's arrays as mesh data, smooth normals. */
TerrainMeshData TestTerrain_GetData(const TestTerrainGrid* grid, uint32_t materialCount, uint32_t channelCount,
    float chunkSize);

/** @brief Builds a mesh from a grid (a failed check is recorded when it fails). */
TerrainMesh* TestTerrain_CreateMesh(TestRun* run, const TestTerrainGrid* grid, uint32_t materialCount,
    uint32_t channelCount, float chunkSize);

/** @brief Whether two meshes are the same bit for bit: their data, triangles (active edges too) and chunks (records the
 *  first difference as a failed check). */
bool TestTerrain_AreMeshesEqual(TestRun* run, const TerrainMesh* a, const TerrainMesh* b);

/** @brief Appends a binary glTF file (.glb) of @p mesh to @p outBytes. */
void TestTerrain_WriteGlb(const TestGltfMesh* mesh, GenericBuffer* outBytes);

/** @brief Copies a .glb with the first occurrence of @p find in its JSON chunk replaced by @p replacement (the chunk
 *  padded again); false when @p find is not there. */
bool TestTerrain_ReplaceInGlbJson(const GenericBuffer* glb, const char* find, const char* replacement,
    GenericBuffer* outBytes);
