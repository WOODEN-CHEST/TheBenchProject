#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "TerrainMesh.h"
#include "wr/WRError.h"

/*
 * Private header (source/): what the terrain mesh's files share. source/TerrainMesh.c builds and changes a mesh (checks,
 * groups, normals, active edges, the setters and the rebuild), source/TerrainMeshChunks.c cuts it into chunks and
 * builds their surfaces, source/TerrainMeshFile.c reads and writes its binary form, source/TerrainMeshGltf.c imports it
 * from glTF, source/TerrainMeshCollision.c builds a chunk's collision mesh.
 */


// Macros.
/** A vertex, triangle, group or chunk that names nothing. */
#define TERRAIN_NONE UINT32_MAX
/** Room for one message's text. */
#define TERRAIN_MESSAGE_CAPACITY ((size_t)512)


// Functions.
/* An error of @p code: "<description>: <text>", the description "The terrain" when NULL (printable ASCII only). */
Error TerrainMeshInternal_Fail(const unsigned char* description, ErrorCode code, const char* format, ...);

/* A coordinate on the position grid (TERRAIN_POSITION_QUANTUM), rounded to the nearest; -0 becomes +0. */
float TerrainMeshInternal_Snap(double value);

/* Whether a coordinate is finite and within the extent. */
bool TerrainMeshInternal_IsInExtent(double value);

/* Cuts the mesh into chunks (each triangle to the cell of its centre) and builds every chunk's surface. Replaces any
 * chunks it had and advances the layout revision and the mesh's revision. False (the mesh then has no chunks) when its
 * triangles would fill more than TERRAIN_MESH_MAX_CHUNKS cells; @p outChunkCount receives how many they fill. */
bool TerrainMeshInternal_BuildChunks(TerrainMesh* self, uint32_t* outChunkCount);

/* Rebuilds chunks' centres, surfaces and bounds from the mesh's current data and advances their revisions (and the
 * mesh's, when there is any). */
void TerrainMeshInternal_RebuildChunks(TerrainMesh* self, const uint32_t* chunks, size_t count);

/* Releases the chunks' surfaces and the chunk lists. */
void TerrainMeshInternal_DestroyChunks(TerrainMesh* self);
