#include <math.h>
#include "TerrainMeshInternal.h"
#include "wr/WRMemory.h"

/*
 * A terrain mesh's chunks (include/TerrainMesh.h, "CHUNKS"): each triangle goes to the grid cell of its centre (a
 * counting sort by cell, so a chunk's triangles stay ascending), each non-empty cell becomes a chunk in row order, and
 * each chunk's surface is built from its triangles that are not holes: one vertex per (terrain vertex, material) pair
 * (the painted weights, or a face material: a face material splits its vertices from the painted ones), positions
 * relative to the chunk's centre, the triangles as indices into that list, and the box and sphere around them.
 *
 * THE CENTRE is the middle of the box around every vertex of the chunk's triangles, on the position grid. Every
 * position is on the grid and within the extent, so a position relative to the centre is a whole number of grid steps
 * no larger than the extent: exact in float, and the centre plus it, in double, is the position again bit for bit.
 */


// Macros.
/** How much a surface's bounding sphere is widened past its farthest vertex: a relative and an absolute margin, metres
 *  (float rounding never leaves a vertex outside it). */
#define SPHERE_RELATIVE_MARGIN 1e-6
#define SPHERE_ABSOLUTE_MARGIN 1e-5
/** The first capacity of the scratch lists. */
#define INITIAL_SCRATCH_CAPACITY 64U


// Types.
/* A face material's copy of a terrain vertex in the surface being built: its material, its surface vertex and the next
 * copy of the same terrain vertex (TERRAIN_NONE: the last). */
typedef struct FaceCopyStruct
{
    uint32_t Material;
    uint32_t Index;
    uint32_t Next;
} FaceCopy;

/* What building surfaces needs, made once per build of many chunks: per terrain vertex its surface vertex with the
 * painted weights and its first face-material copy (TERRAIN_NONE when the chunk being built has none; set back after
 * each chunk), and the lists one chunk's surface is collected in. Bounded by the mesh's own size, whatever a file
 * holds. */
typedef struct SurfaceScratchStruct
{
    uint32_t* BlendVertexOf;
    uint32_t* FirstCopyOf;
    GenericBuffer Copies;    // FaceCopy
    GenericBuffer Sources;   // uint32_t: the terrain vertex of each surface vertex
    GenericBuffer Materials; // uint8_t: its material
    GenericBuffer Indices;   // uint32_t: three per surface triangle
    GenericBuffer Surface;   // uint32_t: the terrain triangle of each surface triangle
} SurfaceScratch;


// Static functions.
static void ConstructScratch(const TerrainMesh* self, SurfaceScratch* outScratch)
{
    size_t Vertices = self->_vertexCount;
    outScratch->BlendVertexOf = Memory_Allocate(Vertices * sizeof(uint32_t));
    outScratch->FirstCopyOf = Memory_Allocate(Vertices * sizeof(uint32_t));
    for (size_t Vertex = 0U; Vertex < Vertices; Vertex++)
    {
        outScratch->BlendVertexOf[Vertex] = TERRAIN_NONE;
        outScratch->FirstCopyOf[Vertex] = TERRAIN_NONE;
    }
    GenericBuffer_AllocateVariable(&outScratch->Copies, INITIAL_SCRATCH_CAPACITY, sizeof(FaceCopy));
    GenericBuffer_AllocateVariable(&outScratch->Sources, INITIAL_SCRATCH_CAPACITY, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&outScratch->Materials, INITIAL_SCRATCH_CAPACITY, sizeof(uint8_t));
    GenericBuffer_AllocateVariable(&outScratch->Indices, INITIAL_SCRATCH_CAPACITY, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&outScratch->Surface, INITIAL_SCRATCH_CAPACITY, sizeof(uint32_t));
}

static void DeconstructScratch(SurfaceScratch* scratch)
{
    Memory_Free(scratch->BlendVertexOf);
    Memory_Free(scratch->FirstCopyOf);
    Memory_Free(scratch->Copies._data);
    Memory_Free(scratch->Sources._data);
    Memory_Free(scratch->Materials._data);
    Memory_Free(scratch->Indices._data);
    Memory_Free(scratch->Surface._data);
}

static void FreeSurface(TerrainChunk* chunk)
{
    // The surface is one block, its positions first.
    Memory_Free(chunk->_positions);
    chunk->_positions = NULL;
    chunk->_vertexSources = NULL;
    chunk->_vertexMaterials = NULL;
    chunk->_indices = NULL;
    chunk->_surfaceTriangles = NULL;
    chunk->_vertexCount = 0U;
    chunk->_surfaceTriangleCount = 0U;
}

/* The chunk's centre: the middle of the box around its triangles' vertices, on the grid. */
static Vector3Double ComputeCenter(const TerrainMesh* self, const TerrainChunk* chunk)
{
    double Min[3] = { INFINITY, INFINITY, INFINITY };
    double Max[3] = { -INFINITY, -INFINITY, -INFINITY };
    const uint32_t* Triangles = &self->_chunkTriangles[chunk->_firstTriangle];
    for (uint32_t Index = 0U; Index < chunk->_triangleCount; Index++)
    {
        const TerrainTriangle* Triangle = &self->_triangles[Triangles[Index]];
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            Vector3 Position = self->_positions[Triangle->_indices[Corner]];
            const double Axes[3] = { (double)Position.x, (double)Position.y, (double)Position.z };
            for (size_t Axis = 0U; Axis < 3U; Axis++)
            {
                Min[Axis] = fmin(Min[Axis], Axes[Axis]);
                Max[Axis] = fmax(Max[Axis], Axes[Axis]);
            }
        }
    }
    return (Vector3Double){ .X = (double)TerrainMeshInternal_Snap((Min[0] + Max[0]) * 0.5),
        .Y = (double)TerrainMeshInternal_Snap((Min[1] + Max[1]) * 0.5),
        .Z = (double)TerrainMeshInternal_Snap((Min[2] + Max[2]) * 0.5) };
}

/* The surface vertex of a (terrain vertex, material) pair in the surface being built, added when new. */
static uint32_t FindOrAddVertex(SurfaceScratch* scratch, uint32_t vertex, uint8_t material)
{
    if (material == TERRAIN_CHUNK_BLEND_MATERIAL)
    {
        if (scratch->BlendVertexOf[vertex] != TERRAIN_NONE)
        {
            return scratch->BlendVertexOf[vertex];
        }
    }
    else
    {
        const FaceCopy* Copies = (const FaceCopy*)(const void*)scratch->Copies._data;
        for (uint32_t Copy = scratch->FirstCopyOf[vertex]; Copy != TERRAIN_NONE; Copy = Copies[Copy].Next)
        {
            if (Copies[Copy].Material == (uint32_t)material)
            {
                return Copies[Copy].Index;
            }
        }
    }
    uint32_t Index = (uint32_t)scratch->Sources._count;
    GenericBuffer_AddLast(&scratch->Sources, &vertex);
    GenericBuffer_AddLast(&scratch->Materials, &material);
    if (material == TERRAIN_CHUNK_BLEND_MATERIAL)
    {
        scratch->BlendVertexOf[vertex] = Index;
    }
    else
    {
        FaceCopy Copy = { .Material = material, .Index = Index, .Next = scratch->FirstCopyOf[vertex] };
        scratch->FirstCopyOf[vertex] = (uint32_t)scratch->Copies._count;
        GenericBuffer_AddLast(&scratch->Copies, &Copy);
    }
    return Index;
}

/* Sets the per-vertex scratch back for the next chunk (only the vertices this one touched) and empties the lists. */
static void ResetScratch(SurfaceScratch* scratch)
{
    const uint32_t* Sources = (const uint32_t*)(const void*)scratch->Sources._data;
    for (size_t Index = 0U; Index < scratch->Sources._count; Index++)
    {
        scratch->BlendVertexOf[Sources[Index]] = TERRAIN_NONE;
        scratch->FirstCopyOf[Sources[Index]] = TERRAIN_NONE;
    }
    GenericBuffer_Clear(&scratch->Copies);
    GenericBuffer_Clear(&scratch->Sources);
    GenericBuffer_Clear(&scratch->Materials);
    GenericBuffer_Clear(&scratch->Indices);
    GenericBuffer_Clear(&scratch->Surface);
}

/* The box and sphere around a surface's positions. */
static void ComputeBounds(TerrainChunk* chunk)
{
    if (chunk->_vertexCount == 0U)
    {
        chunk->_boundsMin = (Vector3){ 0.0f, 0.0f, 0.0f };
        chunk->_boundsMax = (Vector3){ 0.0f, 0.0f, 0.0f };
        chunk->_sphereCenter = (Vector3){ 0.0f, 0.0f, 0.0f };
        chunk->_sphereRadius = 0.0f;
        return;
    }
    Vector3 Min = chunk->_positions[0];
    Vector3 Max = chunk->_positions[0];
    for (uint32_t Vertex = 1U; Vertex < chunk->_vertexCount; Vertex++)
    {
        Vector3 P = chunk->_positions[Vertex];
        Min = (Vector3){ fminf(Min.x, P.x), fminf(Min.y, P.y), fminf(Min.z, P.z) };
        Max = (Vector3){ fmaxf(Max.x, P.x), fmaxf(Max.y, P.y), fmaxf(Max.z, P.z) };
    }
    Vector3 Center = { (Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f, (Min.z + Max.z) * 0.5f };
    double FarthestSquared = 0.0;
    for (uint32_t Vertex = 0U; Vertex < chunk->_vertexCount; Vertex++)
    {
        Vector3 P = chunk->_positions[Vertex];
        double X = (double)P.x - (double)Center.x;
        double Y = (double)P.y - (double)Center.y;
        double Z = (double)P.z - (double)Center.z;
        FarthestSquared = fmax(FarthestSquared, (X * X) + (Y * Y) + (Z * Z));
    }
    double Radius = sqrt(FarthestSquared);
    chunk->_boundsMin = Min;
    chunk->_boundsMax = Max;
    chunk->_sphereCenter = Center;
    chunk->_sphereRadius = (float)((Radius * (1.0 + SPHERE_RELATIVE_MARGIN)) + SPHERE_ABSOLUTE_MARGIN);
}

/* Builds a chunk's centre and surface (replacing its old one) and advances its revision. */
static void BuildSurface(TerrainMesh* self, TerrainChunk* chunk, SurfaceScratch* scratch)
{
    FreeSurface(chunk);
    chunk->_center = ComputeCenter(self, chunk);
    chunk->_revision++;
    const uint32_t* Triangles = &self->_chunkTriangles[chunk->_firstTriangle];
    for (uint32_t Index = 0U; Index < chunk->_triangleCount; Index++)
    {
        const TerrainTriangle* Triangle = &self->_triangles[Triangles[Index]];
        if ((Triangle->_flags & TERRAIN_TRIANGLE_FLAG_HOLE) != 0U)
        {
            continue;
        }
        uint8_t FaceMaterial = ((Triangle->_flags & TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL) != 0U)
            ? Triangle->_material : TERRAIN_CHUNK_BLEND_MATERIAL;
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            uint32_t Local = FindOrAddVertex(scratch, Triangle->_indices[Corner], FaceMaterial);
            GenericBuffer_AddLast(&scratch->Indices, &Local);
        }
        uint32_t SourceTriangle = Triangles[Index];
        GenericBuffer_AddLast(&scratch->Surface, &SourceTriangle);
    }
    uint32_t VertexCount = (uint32_t)scratch->Sources._count;
    uint32_t SurfaceCount = (uint32_t)scratch->Surface._count;
    if (SurfaceCount > 0U)
    {
        // One block: positions, sources, indices, surface triangles, materials (the byte array last, for alignment).
        size_t PositionBytes = (size_t)VertexCount * sizeof(Vector3);
        size_t SourceBytes = (size_t)VertexCount * sizeof(uint32_t);
        size_t IndexBytes = (size_t)SurfaceCount * 3U * sizeof(uint32_t);
        size_t SurfaceBytes = (size_t)SurfaceCount * sizeof(uint32_t);
        unsigned char* Block = Memory_Allocate(PositionBytes + SourceBytes + IndexBytes + SurfaceBytes + VertexCount);
        chunk->_positions = (Vector3*)(void*)Block;
        chunk->_vertexSources = (uint32_t*)(void*)(Block + PositionBytes);
        chunk->_indices = (uint32_t*)(void*)(Block + PositionBytes + SourceBytes);
        chunk->_surfaceTriangles = (uint32_t*)(void*)(Block + PositionBytes + SourceBytes + IndexBytes);
        chunk->_vertexMaterials = Block + PositionBytes + SourceBytes + IndexBytes + SurfaceBytes;
        chunk->_vertexCount = VertexCount;
        chunk->_surfaceTriangleCount = SurfaceCount;
        Memory_Copy(scratch->Sources._data, chunk->_vertexSources, SourceBytes);
        Memory_Copy(scratch->Materials._data, chunk->_vertexMaterials, VertexCount);
        Memory_Copy(scratch->Indices._data, chunk->_indices, IndexBytes);
        Memory_Copy(scratch->Surface._data, chunk->_surfaceTriangles, SurfaceBytes);
        for (uint32_t Vertex = 0U; Vertex < VertexCount; Vertex++)
        {
            // Both on the grid, so each difference is exact (the file comment).
            Vector3 P = self->_positions[chunk->_vertexSources[Vertex]];
            chunk->_positions[Vertex] = (Vector3){ (float)((double)P.x - chunk->_center.X),
                (float)((double)P.y - chunk->_center.Y), (float)((double)P.z - chunk->_center.Z) };
        }
    }
    ResetScratch(scratch);
    ComputeBounds(chunk);
}

/* The cell of each triangle's centre. */
static void ComputeTriangleCells(const TerrainMesh* self, int32_t* cellsX, int32_t* cellsZ)
{
    double Size = (double)self->_chunkSize;
    for (uint32_t Triangle = 0U; Triangle < self->_triangleCount; Triangle++)
    {
        const uint32_t* Corners = self->_triangles[Triangle]._indices;
        double X = ((double)self->_positions[Corners[0]].x + (double)self->_positions[Corners[1]].x
            + (double)self->_positions[Corners[2]].x) / 3.0;
        double Z = ((double)self->_positions[Corners[0]].z + (double)self->_positions[Corners[1]].z
            + (double)self->_positions[Corners[2]].z) / 3.0;
        // Within the extent and at least TERRAIN_MIN_CHUNK_SIZE a cell: at most about a thousand cells either way.
        cellsX[Triangle] = (int32_t)floor(X / Size);
        cellsZ[Triangle] = (int32_t)floor(Z / Size);
    }
}


// Internal functions (TerrainMeshInternal.h).
void TerrainMeshInternal_DestroyChunks(TerrainMesh* self)
{
    for (uint32_t Chunk = 0U; Chunk < self->_chunkCount; Chunk++)
    {
        FreeSurface(&self->_chunks[Chunk]);
    }
    Memory_Free(self->_chunks);
    Memory_Free(self->_chunkTriangles);
    Memory_Free(self->_triangleChunks);
    Memory_Free(self->_isChunkDirty);
    self->_chunks = NULL;
    self->_chunkTriangles = NULL;
    self->_triangleChunks = NULL;
    self->_isChunkDirty = NULL;
    self->_chunkCount = 0U;
    GenericBuffer_Clear(&self->_dirtyChunks);
}

bool TerrainMeshInternal_BuildChunks(TerrainMesh* self, uint32_t* outChunkCount)
{
    TerrainMeshInternal_DestroyChunks(self);
    size_t TriangleCount = self->_triangleCount;
    int32_t* CellsX = Memory_Allocate(TriangleCount * sizeof(int32_t));
    int32_t* CellsZ = Memory_Allocate(TriangleCount * sizeof(int32_t));
    ComputeTriangleCells(self, CellsX, CellsZ);
    int32_t MinX = CellsX[0];
    int32_t MaxX = CellsX[0];
    int32_t MinZ = CellsZ[0];
    int32_t MaxZ = CellsZ[0];
    for (size_t Triangle = 1U; Triangle < TriangleCount; Triangle++)
    {
        MinX = (CellsX[Triangle] < MinX) ? CellsX[Triangle] : MinX;
        MaxX = (CellsX[Triangle] > MaxX) ? CellsX[Triangle] : MaxX;
        MinZ = (CellsZ[Triangle] < MinZ) ? CellsZ[Triangle] : MinZ;
        MaxZ = (CellsZ[Triangle] > MaxZ) ? CellsZ[Triangle] : MaxZ;
    }

    // A counting sort by cell, rows of Z: each cell's triangles come out ascending.
    size_t Width = (size_t)((int64_t)MaxX - (int64_t)MinX + 1);
    size_t Height = (size_t)((int64_t)MaxZ - (int64_t)MinZ + 1);
    size_t CellCount = Width * Height;
    uint32_t* Starts = Memory_Allocate((CellCount + 1U) * sizeof(uint32_t));
    Memory_Zero(Starts, (CellCount + 1U) * sizeof(uint32_t));
    for (size_t Triangle = 0U; Triangle < TriangleCount; Triangle++)
    {
        size_t Cell = ((size_t)(CellsZ[Triangle] - MinZ) * Width) + (size_t)(CellsX[Triangle] - MinX);
        Starts[Cell + 1U]++;
    }
    uint32_t ChunkCount = 0U;
    for (size_t Cell = 0U; Cell < CellCount; Cell++)
    {
        ChunkCount += (Starts[Cell + 1U] > 0U) ? 1U : 0U;
        Starts[Cell + 1U] += Starts[Cell];
    }
    *outChunkCount = ChunkCount;
    if (ChunkCount > TERRAIN_MESH_MAX_CHUNKS)
    {
        // Every chunk costs the renderer GPU meshes and the collision world a shape: the count is bounded.
        Memory_Free(Starts);
        Memory_Free(CellsX);
        Memory_Free(CellsZ);
        return false;
    }
    self->_chunkTriangles = Memory_Allocate(TriangleCount * sizeof(uint32_t));
    self->_triangleChunks = Memory_Allocate(TriangleCount * sizeof(uint32_t));
    uint32_t* Cursor = Memory_Allocate(CellCount * sizeof(uint32_t));
    Memory_Copy(Starts, Cursor, CellCount * sizeof(uint32_t));
    for (size_t Triangle = 0U; Triangle < TriangleCount; Triangle++)
    {
        size_t Cell = ((size_t)(CellsZ[Triangle] - MinZ) * Width) + (size_t)(CellsX[Triangle] - MinX);
        self->_chunkTriangles[Cursor[Cell]++] = (uint32_t)Triangle;
    }
    Memory_Free(Cursor);

    self->_chunks = Memory_Allocate((size_t)ChunkCount * sizeof(TerrainChunk));
    Memory_Zero(self->_chunks, (size_t)ChunkCount * sizeof(TerrainChunk));
    self->_isChunkDirty = Memory_Allocate((ChunkCount > 0U) ? ChunkCount : 1U);
    Memory_Zero(self->_isChunkDirty, (ChunkCount > 0U) ? ChunkCount : 1U);
    self->_chunkCount = ChunkCount;
    uint32_t Chunk = 0U;
    for (size_t Cell = 0U; Cell < CellCount; Cell++)
    {
        uint32_t First = Starts[Cell];
        uint32_t Count = Starts[Cell + 1U] - First;
        if (Count == 0U)
        {
            continue;
        }
        TerrainChunk* Target = &self->_chunks[Chunk];
        Target->_cellX = MinX + (int32_t)(Cell % Width);
        Target->_cellZ = MinZ + (int32_t)(Cell / Width);
        Target->_firstTriangle = First;
        Target->_triangleCount = Count;
        for (uint32_t Index = First; Index < (First + Count); Index++)
        {
            self->_triangleChunks[self->_chunkTriangles[Index]] = Chunk;
        }
        Chunk++;
    }
    Memory_Free(Starts);
    Memory_Free(CellsX);
    Memory_Free(CellsZ);
    SurfaceScratch Scratch;
    ConstructScratch(self, &Scratch);
    for (uint32_t Index = 0U; Index < ChunkCount; Index++)
    {
        BuildSurface(self, &self->_chunks[Index], &Scratch);
    }
    DeconstructScratch(&Scratch);
    self->_layoutRevision++;
    self->_revision++;
    return true;
}

void TerrainMeshInternal_RebuildChunks(TerrainMesh* self, const uint32_t* chunks, size_t count)
{
    if (count == 0U)
    {
        return;
    }
    SurfaceScratch Scratch;
    ConstructScratch(self, &Scratch);
    for (size_t Index = 0U; Index < count; Index++)
    {
        BuildSurface(self, &self->_chunks[chunks[Index]], &Scratch);
    }
    DeconstructScratch(&Scratch);
    self->_revision++;
}
