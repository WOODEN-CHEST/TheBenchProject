#include <inttypes.h>
#include "TerrainMeshInternal.h"
#include "CollisionShape.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"

/*
 * A chunk's surface in parts (include/TerrainMesh.h, TerrainMesh_SplitChunkSurface) and its collision mesh
 * (TerrainMesh_CreateChunkCollisionMesh): parts of at most PART_MAX_VERTICES vertices, each with the terrain's edge flags
 * and triangle indices beside its triangles. CollisionMesh_Create welds the parts' shared vertices (and the face
 * materials' copies) back into one surface.
 */


// Macros.
/** The most vertices one collision mesh part holds: its indices are unsigned shorts. */
#define PART_MAX_VERTICES ((uint32_t)UINT16_MAX + 1U)


// Static functions.
static Error FailToGrow(void)
{
    return Error_Construct2(ErrorCode_InvalidOperation, "TerrainMesh_SplitChunkSurface: a buffer could not grow.");
}


// Public functions.
Error TerrainMesh_SplitChunkSurface(const TerrainMesh* self, uint32_t chunk, uint32_t maxVertices, GenericBuffer* outParts,
    GenericBuffer* outVertices, GenericBuffer* outIndices)
{
    if ((self == NULL) || (outParts == NULL) || (outVertices == NULL) || (outIndices == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TerrainMesh_SplitChunkSurface: self and the buffers must not be NULL.");
    }
    if ((chunk >= self->_chunkCount) || (maxVertices < 3U))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_SplitChunkSurface: chunk %" PRIu32 " of %"
            PRIu32 " with at most %" PRIu32 " vertices a part (at least 3).", chunk, self->_chunkCount, maxVertices);
    }
    GenericBuffer_Clear(outParts);
    GenericBuffer_Clear(outVertices);
    GenericBuffer_Clear(outIndices);
    const TerrainChunk* Chunk = &self->_chunks[chunk];
    uint32_t TriangleCount = Chunk->_surfaceTriangleCount;
    if (TriangleCount == 0U)
    {
        return Error_CreateSuccess();
    }
    // Per surface vertex: the part (counted from 1) that last took it, and its index there.
    uint32_t VertexCount = Chunk->_vertexCount;
    uint32_t* StampOf = Memory_Allocate((size_t)VertexCount * sizeof(uint32_t));
    uint32_t* LocalOf = Memory_Allocate((size_t)VertexCount * sizeof(uint32_t));
    Memory_Zero(StampOf, (size_t)VertexCount * sizeof(uint32_t));
    TerrainChunkPart Part = { .FirstTriangle = 0U, .TriangleCount = 0U, .FirstVertex = 0U, .VertexCount = 0U };
    uint32_t PartNumber = 1U;
    bool IsGrown = true;
    for (uint32_t Triangle = 0U; IsGrown && (Triangle <= TriangleCount); Triangle++)
    {
        const uint32_t* Corners = (Triangle < TriangleCount) ? &Chunk->_indices[(size_t)Triangle * 3U] : NULL;
        uint32_t NewCount = 0U;
        for (uint32_t Corner = 0U; (Corners != NULL) && (Corner < 3U); Corner++)
        {
            NewCount += (StampOf[Corners[Corner]] != PartNumber) ? 1U : 0U;
        }
        if ((Corners == NULL) || ((Part.VertexCount + NewCount) > maxVertices))
        {
            // The part so far is complete: the last triangle is in, or the next one's new vertices do not fit.
            Part.TriangleCount = Triangle - Part.FirstTriangle;
            IsGrown = GenericBuffer_AddLast(outParts, &Part);
            Part = (TerrainChunkPart){ .FirstTriangle = Triangle, .TriangleCount = 0U,
                .FirstVertex = (uint32_t)outVertices->_count, .VertexCount = 0U };
            PartNumber++;
        }
        for (uint32_t Corner = 0U; IsGrown && (Corners != NULL) && (Corner < 3U); Corner++)
        {
            uint32_t Vertex = Corners[Corner];
            if (StampOf[Vertex] != PartNumber)
            {
                StampOf[Vertex] = PartNumber;
                LocalOf[Vertex] = Part.VertexCount++;
                IsGrown = GenericBuffer_AddLast(outVertices, &Vertex);
            }
            IsGrown = IsGrown && GenericBuffer_AddLast(outIndices, &LocalOf[Vertex]);
        }
    }
    Memory_Free(LocalOf);
    Memory_Free(StampOf);
    return IsGrown ? Error_CreateSuccess() : FailToGrow();
}

Error TerrainMesh_CreateChunkCollisionMesh(const TerrainMesh* self, uint32_t chunk, CollisionMesh** outMesh)
{
    if ((self == NULL) || (outMesh == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TerrainMesh_CreateChunkCollisionMesh: self and outMesh must not be NULL.");
    }
    *outMesh = NULL;
    if (chunk >= self->_chunkCount)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_CreateChunkCollisionMesh: chunk %" PRIu32
            " is past the %" PRIu32 " chunks.", chunk, self->_chunkCount);
    }
    const TerrainChunk* Chunk = &self->_chunks[chunk];
    uint32_t TriangleCount = Chunk->_surfaceTriangleCount;
    if (TriangleCount == 0U)
    {
        return Error_CreateSuccess();
    }
    GenericBuffer Parts;
    GenericBuffer_AllocateVariable(&Parts, 1U, sizeof(TerrainChunkPart));
    GenericBuffer Vertices;
    GenericBuffer_AllocateVariable(&Vertices, Chunk->_vertexCount, sizeof(uint32_t));
    GenericBuffer Indices;
    GenericBuffer_AllocateVariable(&Indices, (size_t)TriangleCount * 3U, sizeof(uint32_t));
    Error Result = TerrainMesh_SplitChunkSurface(self, chunk, PART_MAX_VERTICES, &Parts, &Vertices, &Indices);
    if (Result.Code == ErrorCode_Success)
    {
        const TerrainChunkPart* Cut = (const TerrainChunkPart*)(const void*)Parts._data;
        const uint32_t* Sources = (const uint32_t*)(const void*)Vertices._data;
        const uint32_t* Locals = (const uint32_t*)(const void*)Indices._data;
        float* Positions = Memory_Allocate(Vertices._count * 3U * sizeof(float));
        unsigned short* Narrow = Memory_Allocate(Indices._count * sizeof(unsigned short));
        uint8_t* Active = Memory_Allocate(TriangleCount);
        CollisionMeshPart* Source = Memory_Allocate(Parts._count * sizeof(CollisionMeshPart));
        for (size_t Vertex = 0U; Vertex < Vertices._count; Vertex++)
        {
            Vector3 Position = Chunk->_positions[Sources[Vertex]];
            Positions[(Vertex * 3U) + 0U] = Position.x;
            Positions[(Vertex * 3U) + 1U] = Position.y;
            Positions[(Vertex * 3U) + 2U] = Position.z;
        }
        for (size_t Index = 0U; Index < Indices._count; Index++)
        {
            Narrow[Index] = (unsigned short)Locals[Index];
        }
        for (uint32_t Triangle = 0U; Triangle < TriangleCount; Triangle++)
        {
            Active[Triangle] = self->_triangles[Chunk->_surfaceTriangles[Triangle]]._activeEdges;
        }
        for (size_t Index = 0U; Index < Parts._count; Index++)
        {
            Source[Index] = (CollisionMeshPart)
            {
                .Vertices = &Positions[(size_t)Cut[Index].FirstVertex * 3U], .VertexCount = Cut[Index].VertexCount,
                .Indices = &Narrow[(size_t)Cut[Index].FirstTriangle * 3U], .TriangleCount = Cut[Index].TriangleCount,
                .MaterialIndex = -1, .ActiveEdges = &Active[Cut[Index].FirstTriangle],
                .SourceIndices = &Chunk->_surfaceTriangles[Cut[Index].FirstTriangle]
            };
        }
        Result = CollisionMesh_Create(Source, Parts._count, MatrixIdentity(), outMesh);
        Memory_Free(Source);
        Memory_Free(Active);
        Memory_Free(Narrow);
        Memory_Free(Positions);
    }
    Memory_Free(Indices._data);
    Memory_Free(Vertices._data);
    Memory_Free(Parts._data);
    return Result;
}
