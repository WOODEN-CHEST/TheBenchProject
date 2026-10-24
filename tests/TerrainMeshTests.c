#include "TestSuites.h"
#include "TestTerrain.h"
#include "CollisionShape.h"
#include "TerrainMesh.h"
#include "TerrainMeshInternal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "wr/WRFileSystem.h"
#include "wr/WRGHDF.h"
#include "wr/WRMemoryStream.h"


// Static functions: helpers.
static float Hills(float x, float z)
{
    return (3.0f * sinf(x * 0.11f)) + (2.0f * cosf(z * 0.07f)) + (0.37f * sinf((x + z) * 0.53f));
}

static float Slope(float x, float z)
{
    (void)x;
    return 0.5f * z;
}

static bool MessageContains(const Error* error, const char* text)
{
    return (error->Message != NULL) && (strstr((const char*)error->Message, text) != NULL);
}

/* Checks that building @p data fails with ErrorCode_InvalidAssetData and a message holding @p text. */
static void CheckRefused(TestRun* run, const TerrainMeshData* data, const char* text)
{
    TerrainMesh* Terrain = NULL;
    Error Result = TerrainMesh_Create(data, NULL, &Terrain);
    TEST_CHECK(run, Result.Code == ErrorCode_InvalidAssetData);
    TEST_CHECK(run, Terrain == NULL);
    if (!TEST_CHECK(run, MessageContains(&Result, text)))
    {
        printf("    expected \"%s\" in: %s\n", text, (Result.Message != NULL) ? (const char*)Result.Message : "(none)");
    }
    Error_Deconstruct(&Result);
}

/* How many chunks list each triangle (the check that every triangle is in exactly one). */
static bool IsEveryTriangleInOneChunk(const TerrainMesh* mesh)
{
    uint32_t* Seen = Memory_Allocate(mesh->_triangleCount * sizeof(uint32_t));
    Memory_Zero(Seen, mesh->_triangleCount * sizeof(uint32_t));
    bool IsAscending = true;
    for (uint32_t Chunk = 0U; Chunk < mesh->_chunkCount; Chunk++)
    {
        uint32_t Count = 0U;
        const uint32_t* Triangles = TerrainMesh_GetChunkTriangles(mesh, Chunk, &Count);
        for (uint32_t Index = 0U; Index < Count; Index++)
        {
            Seen[Triangles[Index]]++;
            IsAscending = IsAscending && ((Index == 0U) || (Triangles[Index] > Triangles[Index - 1U]))
                && (TerrainMesh_GetTriangleChunk(mesh, Triangles[Index]) == Chunk);
        }
    }
    bool IsOnce = true;
    for (uint32_t Triangle = 0U; Triangle < mesh->_triangleCount; Triangle++)
    {
        IsOnce = IsOnce && (Seen[Triangle] == 1U);
    }
    Memory_Free(Seen);
    return IsOnce && IsAscending;
}

static uint32_t GetRevision(const TerrainMesh* mesh, uint32_t chunk)
{
    return TerrainChunk_GetRevision(TerrainMesh_GetChunk(mesh, chunk));
}

/* The chunk of a cell (UINT32_MAX for none). */
static uint32_t FindChunk(const TerrainMesh* mesh, int32_t cellX, int32_t cellZ)
{
    uint32_t Index = UINT32_MAX;
    return TerrainMesh_TryFindChunk(mesh, cellX, cellZ, &Index) ? Index : UINT32_MAX;
}


// Tests: building.
static void PositionsSnapToTheGrid(TestRun* run)
{
    TEST_CHECK(run, TerrainMeshInternal_Snap(0.0004) == 0.0f);
    TEST_CHECK(run, TerrainMeshInternal_Snap(0.0005) == (float)TERRAIN_POSITION_QUANTUM);
    TEST_CHECK(run, TerrainMeshInternal_Snap(-0.0001) == 0.0f);
    float Zero = TerrainMeshInternal_Snap(-0.0001);
    TEST_CHECK(run, !signbit(Zero));
    TEST_CHECK(run, TerrainMeshInternal_Snap(8192.0) == 8192.0f);
    TEST_CHECK(run, TerrainMeshInternal_Snap(-1234.5678) == (float)(round(-1234.5678 * 1024.0) / 1024.0));
}

static void AGridIsCutIntoChunksByItsTrianglesCentres(TestRun* run)
{
    // 64 x 64 m from (-32, -32), chunks of 32 m: four cells, each with its quads (512 triangles).
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 64U, 64U, 1.0f, -32.0f, -32.0f, Hills);
    TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 32.0f);
    if (Terrain != NULL)
    {
        TEST_CHECK(run, TerrainMesh_GetChunkCount(Terrain) == 4U);
        TEST_CHECK(run, TerrainMesh_GetLayoutRevision(Terrain) == 1U);
        TEST_CHECK(run, IsEveryTriangleInOneChunk(Terrain));
        // Row order: Z, then X.
        TEST_CHECK(run, (Terrain->_chunks[0]._cellX == -1) && (Terrain->_chunks[0]._cellZ == -1));
        TEST_CHECK(run, (Terrain->_chunks[1]._cellX == 0) && (Terrain->_chunks[1]._cellZ == -1));
        TEST_CHECK(run, (Terrain->_chunks[3]._cellX == 0) && (Terrain->_chunks[3]._cellZ == 0));
        for (uint32_t Chunk = 0U; Chunk < 4U; Chunk++)
        {
            TEST_CHECK(run, Terrain->_chunks[Chunk]._triangleCount == 2048U);
            TEST_CHECK(run, TerrainChunk_GetSurfaceTriangleCount(&Terrain->_chunks[Chunk]) == 2048U);
            // 33 x 33 grid points per chunk: a border's vertices are in both chunks.
            TEST_CHECK(run, TerrainChunk_GetVertexCount(&Terrain->_chunks[Chunk]) == (33U * 33U));
            TEST_CHECK(run, TerrainChunk_GetRevision(&Terrain->_chunks[Chunk]) == 1U);
        }
        TEST_CHECK(run, FindChunk(Terrain, 0, -1) == 1U);
        TEST_CHECK(run, FindChunk(Terrain, 5, 5) == UINT32_MAX);
        int32_t CellX = 0;
        int32_t CellZ = 0;
        TerrainMesh_GetCell(Terrain, (Vector3Double){ .X = -0.5, .Y = 0.0, .Z = 31.9 }, &CellX, &CellZ);
        TEST_CHECK(run, (CellX == -1) && (CellZ == 0));
    }
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
}

static void ATriangleBelongsToTheCellOfItsCentre(TestRun* run)
{
    // A quad straddling x = 0: the triangle (v00, v01, v10) has its centre at x = -1/3 of a 2 m quad from -1, the other
    // at +1/3: they go to different cells though they share an edge across the border.
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 1U, 1U, 2.0f, -1.0f, 4.0f, NULL);
    TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    if (Terrain != NULL)
    {
        TEST_CHECK(run, TerrainMesh_GetChunkCount(Terrain) == 2U);
        TEST_CHECK(run, Terrain->_chunks[TerrainMesh_GetTriangleChunk(Terrain, 0U)]._cellX == -1);
        TEST_CHECK(run, Terrain->_chunks[TerrainMesh_GetTriangleChunk(Terrain, 1U)]._cellX == 0);
    }
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
}

static void ChunkPositionsPlusTheirCentreGiveTheTerrainBackExactly(TestRun* run)
{
    // Off the whole metres and far from the terrain's origin, where the floats are coarse.
    const float Origins[3][2] = { { -32.3f, 17.77f }, { 7000.123f, -8100.5f }, { -8150.0f, 8130.0f } };
    for (size_t Case = 0U; Case < 3U; Case++)
    {
        TestTerrainGrid Grid;
        TestTerrain_BuildGrid(&Grid, 40U, 40U, 1.37f, Origins[Case][0], Origins[Case][1], Hills);
        TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 16.0f);
        bool IsExact = (Terrain != NULL);
        for (uint32_t Chunk = 0U; IsExact && (Chunk < TerrainMesh_GetChunkCount(Terrain)); Chunk++)
        {
            const TerrainChunk* Entry = TerrainMesh_GetChunk(Terrain, Chunk);
            for (uint32_t Vertex = 0U; Vertex < Entry->_vertexCount; Vertex++)
            {
                Vector3 Own = Entry->_positions[Vertex];
                Vector3 Source = TerrainMesh_GetPosition(Terrain, Entry->_vertexSources[Vertex]);
                IsExact = IsExact && (((double)Own.x + Entry->_center.X) == (double)Source.x)
                    && (((double)Own.y + Entry->_center.Y) == (double)Source.y)
                    && (((double)Own.z + Entry->_center.Z) == (double)Source.z);
            }
        }
        TEST_CHECK(run, IsExact);
        TerrainMesh_Destroy(Terrain);
        TestTerrain_FreeGrid(&Grid);
    }
}

static void BoundsHoldTheirSurfaces(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 50U, 30U, 1.1f, 3.3f, -9.9f, Hills);
    TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    bool IsHeld = (Terrain != NULL);
    for (uint32_t Chunk = 0U; IsHeld && (Chunk < TerrainMesh_GetChunkCount(Terrain)); Chunk++)
    {
        const TerrainChunk* Entry = TerrainMesh_GetChunk(Terrain, Chunk);
        for (uint32_t Vertex = 0U; Vertex < Entry->_vertexCount; Vertex++)
        {
            Vector3 P = Entry->_positions[Vertex];
            Vector3 C = Entry->_sphereCenter;
            double Distance = sqrt(pow((double)P.x - (double)C.x, 2.0) + pow((double)P.y - (double)C.y, 2.0)
                + pow((double)P.z - (double)C.z, 2.0));
            IsHeld = IsHeld && (P.x >= Entry->_boundsMin.x) && (P.x <= Entry->_boundsMax.x)
                && (P.y >= Entry->_boundsMin.y) && (P.y <= Entry->_boundsMax.y) && (P.z >= Entry->_boundsMin.z)
                && (P.z <= Entry->_boundsMax.z) && (Distance <= (double)Entry->_sphereRadius);
        }
    }
    TEST_CHECK(run, IsHeld);
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
}

static void SmoothNormalsAreTheWholeMeshsOnBothSidesOfASeam(TestRun* run)
{
    // The same hills cut into 8 m chunks and into 256 m ones (the four cells around the origin): every normal the same,
    // bit for bit.
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 48U, 48U, 1.0f, -24.0f, -24.0f, Hills);
    TerrainMesh* Small = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    TerrainMesh* One = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 256.0f);
    if ((Small != NULL) && (One != NULL))
    {
        TEST_CHECK(run, TerrainMesh_GetChunkCount(Small) == 36U);
        TEST_CHECK(run, TerrainMesh_GetChunkCount(One) == 4U);
        bool AreSame = true;
        for (uint32_t Vertex = 0U; Vertex < Grid.VertexCount; Vertex++)
        {
            Vector3 A = TerrainMesh_GetNormal(Small, Vertex);
            Vector3 B = TerrainMesh_GetNormal(One, Vertex);
            AreSame = AreSame && (memcmp(&A, &B, sizeof(Vector3)) == 0);
        }
        TEST_CHECK(run, AreSame);
    }
    TerrainMesh_Destroy(Small);
    TerrainMesh_Destroy(One);
    TestTerrain_FreeGrid(&Grid);

    // A plane rising 0.5 m per metre along Z: every normal (0, 1, -0.5) normalized.
    TestTerrain_BuildGrid(&Grid, 10U, 10U, 1.0f, 0.0f, 0.0f, Slope);
    TerrainMesh* Plane = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    if (Plane != NULL)
    {
        double Length = sqrt(1.25);
        Vector3 Normal = TerrainMesh_GetNormal(Plane, TestTerrain_Vertex(&Grid, 4U, 4U));
        TEST_CHECK(run, fabs((double)Normal.x) < 1e-7);
        TEST_CHECK(run, fabs((double)Normal.y - (1.0 / Length)) < 1e-6);
        TEST_CHECK(run, fabs((double)Normal.z + (0.5 / Length)) < 1e-6);
    }
    TerrainMesh_Destroy(Plane);
    TestTerrain_FreeGrid(&Grid);
}

static void HolesAreLeftOutOfTheSurfaceAndTheNormals(TestRun* run)
{
    // A flat field whose centre vertex is raised: the triangles around it tilt the normals of its ring. Made holes, they
    // are not drawn and the ring's normals are straight up again.
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 8U, 8U, 1.0f, 0.0f, 0.0f, NULL);
    uint32_t Raised = TestTerrain_Vertex(&Grid, 4U, 4U);
    Grid.Positions[Raised].y = 1.0f;
    TerrainMesh* Tilted = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 16.0f);
    uint32_t Ring = TestTerrain_Vertex(&Grid, 5U, 4U);
    if (Tilted != NULL)
    {
        TEST_CHECK(run, TerrainMesh_GetNormal(Tilted, Ring).y < 0.99f);
    }
    uint32_t HoleCount = 0U;
    for (uint32_t Triangle = 0U; Triangle < Grid.TriangleCount; Triangle++)
    {
        const uint32_t* Corners = &Grid.Indices[Triangle * 3U];
        if ((Corners[0] == Raised) || (Corners[1] == Raised) || (Corners[2] == Raised))
        {
            Grid.Flags[Triangle] = TERRAIN_TRIANGLE_FLAG_HOLE;
            HoleCount++;
        }
    }
    TerrainMesh* Holed = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 16.0f);
    if (Holed != NULL)
    {
        TEST_CHECK(run, HoleCount == 6U);
        const TerrainChunk* Chunk = TerrainMesh_GetChunk(Holed, 0U);
        TEST_CHECK(run, Chunk->_triangleCount == Grid.TriangleCount);
        TEST_CHECK(run, Chunk->_surfaceTriangleCount == (Grid.TriangleCount - HoleCount));
        bool IsHoleDrawn = false;
        for (uint32_t Index = 0U; Index < Chunk->_surfaceTriangleCount; Index++)
        {
            IsHoleDrawn = IsHoleDrawn || ((Grid.Flags[Chunk->_surfaceTriangles[Index]] & TERRAIN_TRIANGLE_FLAG_HOLE) != 0U);
        }
        TEST_CHECK(run, !IsHoleDrawn);
        // The raised vertex is drawn by no triangle any more: not in the surface.
        bool IsRaisedDrawn = false;
        for (uint32_t Vertex = 0U; Vertex < Chunk->_vertexCount; Vertex++)
        {
            IsRaisedDrawn = IsRaisedDrawn || (Chunk->_vertexSources[Vertex] == Raised);
        }
        TEST_CHECK(run, !IsRaisedDrawn);
        Vector3 Normal = TerrainMesh_GetNormal(Holed, Ring);
        TEST_CHECK(run, (Normal.x == 0.0f) && (Normal.y == 1.0f) && (Normal.z == 0.0f));
    }
    TerrainMesh_Destroy(Tilted);
    TerrainMesh_Destroy(Holed);
    TestTerrain_FreeGrid(&Grid);
}

static void AFaceMaterialSplitsItsVerticesFromThePaintedOnes(TestRun* run)
{
    // One quad in the middle of a 4 x 4 field takes material 2 outright: its four corners get a copy each.
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 4U, 4U, 1.0f, 0.0f, 0.0f, NULL);
    uint32_t Patch = TestTerrain_Triangle(&Grid, 1U, 1U);
    for (uint32_t Triangle = Patch; Triangle < (Patch + 2U); Triangle++)
    {
        Grid.Flags[Triangle] = TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL;
        Grid.Materials[Triangle] = 2U;
    }
    TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 3U, 0U, 16.0f);
    if (Terrain != NULL)
    {
        const TerrainChunk* Chunk = TerrainMesh_GetChunk(Terrain, 0U);
        TEST_CHECK(run, Chunk->_vertexCount == (25U + 4U));
        bool AreRight = true;
        for (uint32_t Index = 0U; Index < Chunk->_surfaceTriangleCount; Index++)
        {
            bool IsPatch = (Chunk->_surfaceTriangles[Index] == Patch) || (Chunk->_surfaceTriangles[Index] == (Patch + 1U));
            for (uint32_t Corner = 0U; Corner < 3U; Corner++)
            {
                uint8_t ChunkMaterial = Chunk->_vertexMaterials[Chunk->_indices[(Index * 3U) + Corner]];
                AreRight = AreRight && (ChunkMaterial == (IsPatch ? 2U : TERRAIN_CHUNK_BLEND_MATERIAL));
            }
        }
        TEST_CHECK(run, AreRight);
        TEST_CHECK(run, TerrainMesh_GetTriangle(Terrain, Patch)->_material == 2U);
    }
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
}

static void EveryLimitIsRefusedWithAMessage(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 2U, 2U, 1.0f, 0.0f, 0.0f, NULL);
    TerrainMeshData Good = TestTerrain_GetData(&Grid, 2U, 1U, 32.0f);
    TerrainMesh* Terrain = NULL;
    TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Good, NULL, &Terrain));
    TerrainMesh_Destroy(Terrain);

    TerrainMeshData Data = Good;
    Data.VertexCount = 0U;
    CheckRefused(run, &Data, "vertices");
    Data = Good;
    Data.TriangleCount = TERRAIN_MESH_MAX_TRIANGLES + 1U;
    CheckRefused(run, &Data, "triangles");
    Data = Good;
    Data.MaterialCount = 17U;
    CheckRefused(run, &Data, "palette holds 17");
    Data = Good;
    Data.DensityChannelCount = 9U;
    CheckRefused(run, &Data, "9 density channels");
    Data = Good;
    Data.ChunkSize = 4.0f;
    CheckRefused(run, &Data, "chunk size");
    Data = Good;
    Data.NormalMode = TerrainNormalMode_Imported;
    Data.Normals = NULL;
    CheckRefused(run, &Data, "normals");

    Grid.Positions[3].x = 9000.0f;
    CheckRefused(run, &Good, "vertex 3 is at");
    Grid.Positions[3].x = NAN;
    CheckRefused(run, &Good, "vertex 3 is at");
    Grid.Positions[3].x = 0.0f;
    Grid.Normals[2] = (Vector3){ 0.0f, 0.0f, 0.0f };
    Data = Good;
    Data.NormalMode = TerrainNormalMode_Imported;
    CheckRefused(run, &Data, "vertex 2's normal");
    Grid.Normals[2] = (Vector3){ 0.0f, 1.0f, 0.0f };
    Grid.Weights[(4U * TERRAIN_MAX_MATERIALS) + 2U] = 1U;
    CheckRefused(run, &Good, "vertex 4 weighs material 2");
    Grid.Weights[(4U * TERRAIN_MAX_MATERIALS) + 2U] = 0U;
    Grid.Densities[(5U * TERRAIN_MAX_DENSITY_CHANNELS) + 1U] = 7U;
    CheckRefused(run, &Good, "vertex 5 has a density in channel 1");
    Grid.Densities[(5U * TERRAIN_MAX_DENSITY_CHANNELS) + 1U] = 0U;

    uint32_t Saved = Grid.Indices[4];
    Grid.Indices[4] = 99U;
    CheckRefused(run, &Good, "triangle 1 names vertex 99");
    Grid.Indices[4] = Grid.Indices[3];
    CheckRefused(run, &Good, "triangle 1 uses one vertex twice");
    Grid.Indices[4] = Saved;
    Grid.Flags[2] = 0x80U;
    CheckRefused(run, &Good, "triangle 2 has unknown flags");
    Grid.Flags[2] = TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL;
    Grid.Materials[2] = 2U;
    CheckRefused(run, &Good, "triangle 2 takes material 2");
    Grid.Flags[2] = 0U;
    Grid.Materials[2] = 0U;

    // Two corners 0.2 mm apart land on one point of the grid.
    Vector3 SavedPosition = Grid.Positions[1];
    Grid.Positions[1] = (Vector3){ Grid.Positions[0].x + 0.0002f, Grid.Positions[0].y, Grid.Positions[0].z };
    CheckRefused(run, &Good, "two corners at one position");
    Grid.Positions[1] = SavedPosition;
    // A triangle from one end of the extent to the other.
    Grid.Positions[0] = (Vector3){ -8000.0f, 0.0f, 0.0f };
    Grid.Positions[1] = (Vector3){ 8000.0f, 0.0f, 0.0f };
    CheckRefused(run, &Good, "edge longer than");
    TestTerrain_FreeGrid(&Grid);

    // Every chunk costs the renderer GPU meshes and the collision world a shape: one small triangle in each 8 m cell
    // of 128 x 128 cells is exactly the limit, 129 x 128 one row too many.
    for (uint32_t Columns = 128U; Columns <= 129U; Columns++)
    {
        uint32_t Cells = Columns * 128U;
        Vector3* Corners = Memory_Allocate((size_t)Cells * 3U * sizeof(Vector3));
        uint32_t* Indices = Memory_Allocate((size_t)Cells * 3U * sizeof(uint32_t));
        for (uint32_t Cell = 0U; Cell < Cells; Cell++)
        {
            float X = (float)(Cell % Columns) * 8.0f;
            float Z = (float)(Cell / Columns) * 8.0f;
            Corners[(Cell * 3U) + 0U] = (Vector3){ X + 1.0f, 0.0f, Z + 1.0f };
            Corners[(Cell * 3U) + 1U] = (Vector3){ X + 1.0f, 0.0f, Z + 2.0f };
            Corners[(Cell * 3U) + 2U] = (Vector3){ X + 2.0f, 0.0f, Z + 1.0f };
            for (uint32_t Corner = 0U; Corner < 3U; Corner++)
            {
                Indices[(Cell * 3U) + Corner] = (Cell * 3U) + Corner;
            }
        }
        TerrainMeshData Spread =
        {
            .Positions = Corners, .VertexCount = Cells * 3U, .Indices = Indices, .TriangleCount = Cells,
            .MaterialCount = 1U, .NormalMode = TerrainNormalMode_Smooth, .ChunkSize = 8.0f
        };
        if (Columns == 128U)
        {
            if (TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Spread, NULL, &Terrain)))
            {
                TEST_CHECK(run, TerrainMesh_GetChunkCount(Terrain) == TERRAIN_MESH_MAX_CHUNKS);
            }
            TerrainMesh_Destroy(Terrain);
            Terrain = NULL;
        }
        else
        {
            CheckRefused(run, &Spread, "its triangles fill 16512 chunks of 8 m; a terrain has at most 16384 (larger "
                "chunks hold more).");
        }
        Memory_Free(Indices);
        Memory_Free(Corners);
    }

    TEST_CHECK_ERROR(run, TerrainMesh_Create(NULL, NULL, &Terrain), ErrorCode_IllegalArgument);
}


// Tests: active edges.
/* The active edge bits of the edge of @p triangle from corner @p from to the next. */
static bool IsEdgeActive(const TerrainMesh* mesh, uint32_t triangle, uint32_t from)
{
    return (TerrainMesh_GetTriangle(mesh, triangle)->_activeEdges & (1U << from)) != 0U;
}

static void EdgesAreActiveByTheWholeTerrain(TestRun* run)
{
    // A flat field cut into 4 m chunks: every edge inside the field is inactive, seams too; the field's border is active.
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 12U, 12U, 1.0f, 0.0f, 0.0f, NULL);
    TerrainMesh* Flat = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    if (Flat != NULL)
    {
        TEST_CHECK(run, TerrainMesh_GetChunkCount(Flat) == 4U);
        uint32_t ActiveCount = 0U;
        for (uint32_t Triangle = 0U; Triangle < Grid.TriangleCount; Triangle++)
        {
            for (uint32_t Edge = 0U; Edge < 3U; Edge++)
            {
                ActiveCount += IsEdgeActive(Flat, Triangle, Edge) ? 1U : 0U;
            }
        }
        // Only the 48 border edges of the 12 x 12 field.
        TEST_CHECK(run, ActiveCount == 48U);
        // Quad (7, 3)'s first triangle (v(7,3), v(7,4), v(8,3)): its edge (v(8,3) -> v(7,3)) is the border of the cells
        // x = 0 and 1? No: x = 8 is the seam; its edge (v(7,4) -> v(8,3)) is the quad's diagonal. The seam edge at x = 8
        // belongs to quad (7, j)'s second triangle: (v(8,j), v(7,j+1), v(8,j+1)), its edge from v(8,j+1) back to v(8,j).
        uint32_t SeamTriangle = TestTerrain_Triangle(&Grid, 7U, 3U) + 1U;
        TEST_CHECK(run, TerrainMesh_GetTriangleChunk(Flat, SeamTriangle)
            != TerrainMesh_GetTriangleChunk(Flat, TestTerrain_Triangle(&Grid, 8U, 3U)));
        TEST_CHECK(run, !IsEdgeActive(Flat, SeamTriangle, 2U));
    }
    TerrainMesh_Destroy(Flat);

    // A ridge along x = 6 (convex): its edges are active; a valley (concave) leaves them inactive.
    for (uint32_t J = 0U; J <= Grid.CellsZ; J++)
    {
        for (uint32_t I = 0U; I <= Grid.CellsX; I++)
        {
            Grid.Positions[TestTerrain_Vertex(&Grid, I, J)].y = 6.0f - fabsf((float)I - 6.0f);
        }
    }
    TerrainMesh* Ridge = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    for (uint32_t J = 0U; J <= Grid.CellsZ; J++)
    {
        for (uint32_t I = 0U; I <= Grid.CellsX; I++)
        {
            Grid.Positions[TestTerrain_Vertex(&Grid, I, J)].y = fabsf((float)I - 6.0f);
        }
    }
    TerrainMesh* Valley = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    if ((Ridge != NULL) && (Valley != NULL))
    {
        // Quad (5, 2)'s second triangle (v(6,2), v(5,3), v(6,3)) has the crest edge v(6,3) -> v(6,2) as its third.
        uint32_t Crest = TestTerrain_Triangle(&Grid, 5U, 2U) + 1U;
        TEST_CHECK(run, IsEdgeActive(Ridge, Crest, 2U));
        TEST_CHECK(run, !IsEdgeActive(Valley, Crest, 2U));
    }
    TerrainMesh_Destroy(Ridge);
    TerrainMesh_Destroy(Valley);
    TestTerrain_FreeGrid(&Grid);

    // A hole's edges are borders for its neighbours.
    TestTerrain_BuildGrid(&Grid, 4U, 4U, 1.0f, 0.0f, 0.0f, NULL);
    uint32_t Hole = TestTerrain_Triangle(&Grid, 1U, 1U) + 1U;
    Grid.Flags[Hole] = TERRAIN_TRIANGLE_FLAG_HOLE;
    TerrainMesh* Holed = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 16.0f);
    if (Holed != NULL)
    {
        // The quad's first triangle shares its diagonal (v(1,2) -> v(2,1), its second edge) with the hole.
        TEST_CHECK(run, IsEdgeActive(Holed, Hole - 1U, 1U));
        TEST_CHECK(run, TerrainMesh_GetTriangle(Holed, Hole)->_activeEdges == TERRAIN_ALL_EDGES_ACTIVE);
    }
    TerrainMesh_Destroy(Holed);
    TestTerrain_FreeGrid(&Grid);
}


static void NormalsWeighEachTriangleByItsArea(TestRun* run)
{
    // Vertex 0 has a large flat triangle and a small one tilted 45 degrees: its normal leans little.
    const Vector3 Positions[5] =
    {
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 10.0f }, { 10.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }
    };
    const uint32_t Indices[6] = { 0U, 1U, 2U, 0U, 3U, 4U };
    TerrainMeshData Data =
    {
        .Positions = Positions, .VertexCount = 5U, .Indices = Indices, .TriangleCount = 2U, .MaterialCount = 1U,
        .NormalMode = TerrainNormalMode_Smooth, .ChunkSize = 32.0f
    };
    TerrainMesh* Terrain = NULL;
    if (TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Data, NULL, &Terrain)))
    {
        // The cross products (twice the areas along the normals): (0, 100, 0) and (-1, -1, 0). Their sum (-1, 99, 0)
        // normalized; the plain mean of the two unit normals would lean nearly 70 degrees.
        Vector3 Normal = TerrainMesh_GetNormal(Terrain, 0U);
        double Length = sqrt(1.0 + (99.0 * 99.0));
        TEST_CHECK(run, fabs((double)Normal.x + (1.0 / Length)) < 1e-6);
        TEST_CHECK(run, fabs((double)Normal.y - (99.0 / Length)) < 1e-6);
    }
    TerrainMesh_Destroy(Terrain);
}

static void EdgesSharedBadlyStayActive(TestRun* run)
{
    // Two flat triangles sharing the edge (1, 2) wound the same way round (one of them faces down), and three triangles
    // on one edge (a fin, also one whose other sides would each hide the edge): all stay active.
    const Vector3 Positions[5] =
    {
        { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f }, { 0.5f, 1.0f, 0.5f }
    };
    const uint32_t Inconsistent[6] = { 0U, 2U, 1U, 2U, 1U, 3U };
    TerrainMeshData Data =
    {
        .Positions = Positions, .VertexCount = 5U, .Indices = Inconsistent, .TriangleCount = 2U, .MaterialCount = 1U,
        .NormalMode = TerrainNormalMode_Smooth, .ChunkSize = 32.0f
    };
    TerrainMesh* Terrain = NULL;
    if (TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Data, NULL, &Terrain)))
    {
        // The first triangle's edge from corner 1 (2) to corner 2 (1).
        TEST_CHECK(run, (TerrainMesh_GetTriangle(Terrain, 0U)->_activeEdges & 0x2U) != 0U);
    }
    TerrainMesh_Destroy(Terrain);
    const uint32_t Fin[9] = { 0U, 2U, 1U, 2U, 3U, 1U, 1U, 4U, 2U };
    Data.Indices = Fin;
    Data.TriangleCount = 3U;
    if (TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Data, NULL, &Terrain)))
    {
        // From the first triangle's side and from the second's (its edge from corner 2, v1, back to v2), whichever
        // neighbour a search would meet last.
        TEST_CHECK(run, (TerrainMesh_GetTriangle(Terrain, 0U)->_activeEdges & 0x2U) != 0U);
        TEST_CHECK(run, (TerrainMesh_GetTriangle(Terrain, 1U)->_activeEdges & 0x4U) != 0U);
    }
    TerrainMesh_Destroy(Terrain);
    // The same two flat triangles wound consistently: the shared edge is inactive.
    const uint32_t Flat[6] = { 0U, 2U, 1U, 1U, 2U, 3U };
    Data.Indices = Flat;
    Data.TriangleCount = 2U;
    if (TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Data, NULL, &Terrain)))
    {
        TEST_CHECK(run, (TerrainMesh_GetTriangle(Terrain, 0U)->_activeEdges & 0x2U) == 0U);
    }
    TerrainMesh_Destroy(Terrain);
    // A fin whose other two sides would each hide the edge alone (the flat neighbour, and one rising from the edge's
    // far side: a concave fold, both wound consistently): three on one edge, so it stays active all the same.
    const Vector3 ValleyPositions[6] =
    {
        { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f }, { 0.5f, 1.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f }
    };
    const uint32_t ValleyFin[9] = { 0U, 2U, 1U, 2U, 3U, 1U, 1U, 2U, 5U };
    Data.Positions = ValleyPositions;
    Data.VertexCount = 6U;
    Data.Indices = ValleyFin;
    Data.TriangleCount = 3U;
    if (TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Data, NULL, &Terrain)))
    {
        TEST_CHECK(run, (TerrainMesh_GetTriangle(Terrain, 0U)->_activeEdges & 0x2U) != 0U);
    }
    TerrainMesh_Destroy(Terrain);
}


// Tests: changes.
static void AChangeRebuildsOnlyItsChunksAndTheirNeighbours(TestRun* run)
{
    // 64 x 64 m of 1 m quads in 16 m chunks: 16 chunks.
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 64U, 64U, 1.0f, 0.0f, 0.0f, Hills);
    TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 2U, 1U, 16.0f);
    if (Terrain == NULL)
    {
        TestTerrain_FreeGrid(&Grid);
        return;
    }
    TEST_CHECK(run, TerrainMesh_GetChunkCount(Terrain) == 16U);
    TEST_CHECK(run, !TerrainMesh_HasDirtyChunks(Terrain));
    TEST_CHECK(run, TerrainMesh_RebuildDirtyChunks(Terrain) == 0U);
    // The mesh's revision starts at 1 and a rebuild with nothing to do leaves it.
    TEST_CHECK(run, (TerrainMesh_GetRevision(Terrain) == 1U) && (TerrainMesh_GetLayoutRevision(Terrain) == 1U));

    // A vertex in the middle of the chunk of cell (1, 1): only that chunk.
    uint32_t Middle = TestTerrain_Vertex(&Grid, 24U, 24U);
    Vector3 Moved = Grid.Positions[Middle];
    Moved.y += 2.5f;
    TEST_CHECK_SUCCESS(run, TerrainMesh_SetVertexPositions(Terrain, &Middle, &Moved, 1U));
    TEST_CHECK(run, TerrainMesh_HasDirtyChunks(Terrain));
    TEST_CHECK(run, TerrainMesh_RebuildDirtyChunks(Terrain) == 1U);
    TEST_CHECK(run, (TerrainMesh_GetRevision(Terrain) == 2U) && (TerrainMesh_GetLayoutRevision(Terrain) == 1U));
    uint32_t Target = FindChunk(Terrain, 1, 1);
    for (uint32_t Chunk = 0U; Chunk < 16U; Chunk++)
    {
        TEST_CHECK(run, GetRevision(Terrain, Chunk) == ((Chunk == Target) ? 2U : 1U));
    }
    Grid.Positions[Middle] = Moved;

    // A vertex on the corner of four chunks: those four (its ring stays within them).
    uint32_t Corner = TestTerrain_Vertex(&Grid, 32U, 32U);
    Moved = Grid.Positions[Corner];
    Moved.y -= 1.75f;
    TEST_CHECK_SUCCESS(run, TerrainMesh_SetVertexPositions(Terrain, &Corner, &Moved, 1U));
    TEST_CHECK(run, TerrainMesh_RebuildDirtyChunks(Terrain) == 4U);
    TEST_CHECK(run, GetRevision(Terrain, FindChunk(Terrain, 1, 1)) == 3U);
    TEST_CHECK(run, GetRevision(Terrain, FindChunk(Terrain, 2, 1)) == 2U);
    TEST_CHECK(run, GetRevision(Terrain, FindChunk(Terrain, 1, 2)) == 2U);
    TEST_CHECK(run, GetRevision(Terrain, FindChunk(Terrain, 2, 2)) == 2U);
    TEST_CHECK(run, GetRevision(Terrain, FindChunk(Terrain, 0, 0)) == 1U);
    Grid.Positions[Corner] = Moved;

    // New weights: the chunks that draw the vertex (one inside a chunk).
    uint8_t Weights[TERRAIN_MAX_MATERIALS] = { 100U, 155U };
    uint32_t Painted = TestTerrain_Vertex(&Grid, 40U, 8U);
    TEST_CHECK_SUCCESS(run, TerrainMesh_SetVertexWeights(Terrain, &Painted, Weights, 1U));
    TEST_CHECK(run, TerrainMesh_RebuildDirtyChunks(Terrain) == 1U);
    TEST_CHECK(run, GetRevision(Terrain, FindChunk(Terrain, 2, 0)) == 2U);
    Memory_Copy(Weights, &Grid.Weights[(size_t)Painted * TERRAIN_MAX_MATERIALS], TERRAIN_MAX_MATERIALS);
    uint8_t Densities[TERRAIN_MAX_DENSITY_CHANNELS] = { 77U };
    TEST_CHECK_SUCCESS(run, TerrainMesh_SetVertexDensities(Terrain, &Painted, Densities, 1U));
    TEST_CHECK(run, TerrainMesh_RebuildDirtyChunks(Terrain) == 1U);
    Memory_Copy(Densities, &Grid.Densities[(size_t)Painted * TERRAIN_MAX_DENSITY_CHANNELS], TERRAIN_MAX_DENSITY_CHANNELS);

    // A hole on a chunk border, and a face material.
    uint32_t Changed[2] = { TestTerrain_Triangle(&Grid, 15U, 50U) + 1U, TestTerrain_Triangle(&Grid, 3U, 60U) };
    uint8_t Flags[2] = { TERRAIN_TRIANGLE_FLAG_HOLE, TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL };
    uint8_t Materials[2] = { 0U, 1U };
    TEST_CHECK_SUCCESS(run, TerrainMesh_SetTriangleFlags(Terrain, Changed, Flags, Materials, 2U));
    TEST_CHECK(run, TerrainMesh_RebuildDirtyChunks(Terrain) > 0U);
    Grid.Flags[Changed[0]] = Flags[0];
    Grid.Flags[Changed[1]] = Flags[1];
    Grid.Materials[Changed[1]] = Materials[1];

    // After all of it, the mesh is exactly a mesh built afresh from the changed data (normals, edges, chunks).
    TerrainMesh* Fresh = TestTerrain_CreateMesh(run, &Grid, 2U, 1U, 16.0f);
    if (Fresh != NULL)
    {
        TEST_CHECK(run, TestTerrain_AreMeshesEqual(run, Terrain, Fresh));
    }
    TerrainMesh_Destroy(Fresh);

    // Refused changes change nothing.
    Vector3 Before = Terrain->_positions[Middle];
    uint32_t Out = Grid.VertexCount;
    TEST_CHECK_ERROR(run, TerrainMesh_SetVertexPositions(Terrain, &Out, &Moved, 1U), ErrorCode_ArgumentOutOfRange);
    Vector3 Far = { 9000.0f, 0.0f, 0.0f };
    TEST_CHECK_ERROR(run, TerrainMesh_SetVertexPositions(Terrain, &Middle, &Far, 1U), ErrorCode_ArgumentOutOfRange);
    Vector3 Onto = Grid.Positions[TestTerrain_Vertex(&Grid, 25U, 24U)];
    TEST_CHECK_ERROR(run, TerrainMesh_SetVertexPositions(Terrain, &Middle, &Onto, 1U), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, memcmp(&Terrain->_positions[Middle], &Before, sizeof(Vector3)) == 0);
    uint8_t BadWeights[TERRAIN_MAX_MATERIALS] = { 0U, 0U, 9U };
    TEST_CHECK_ERROR(run, TerrainMesh_SetVertexWeights(Terrain, &Painted, BadWeights, 1U), ErrorCode_ArgumentOutOfRange);
    uint8_t BadFlag = 0x40U;
    TEST_CHECK_ERROR(run, TerrainMesh_SetTriangleFlags(Terrain, Changed, &BadFlag, Materials, 1U),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, !TerrainMesh_HasDirtyChunks(Terrain));
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
}


// Tests: the binary form.
static TerrainMesh* BuildVariedMesh(TestRun* run, TestTerrainGrid* grid, TerrainNormalMode mode)
{
    TestTerrain_BuildGrid(grid, 30U, 20U, 1.25f, -7.3f, 11.1f, Hills);
    for (uint32_t Vertex = 0U; Vertex < grid->VertexCount; Vertex++)
    {
        grid->Weights[(size_t)Vertex * TERRAIN_MAX_MATERIALS] = (uint8_t)(Vertex * 7U);
        grid->Weights[((size_t)Vertex * TERRAIN_MAX_MATERIALS) + 2U] = (uint8_t)(Vertex * 13U);
        grid->Densities[((size_t)Vertex * TERRAIN_MAX_DENSITY_CHANNELS) + 1U] = (uint8_t)(Vertex * 3U);
        grid->Normals[Vertex] = (Vector3){ 0.1f * sinf((float)Vertex), 1.0f, 0.2f };
    }
    // Normalized, this one is (0.56987214, 0.75848973, 0.31613129); normalized again its y would round one step up: the
    // mesh keeps an imported normal that is unit already as it is, so a read keeps it bit for bit.
    grid->Normals[0] = (Vector3){ 0.7355621457099915f, 0.9790201783180237f, 0.40804627537727356f };
    for (uint32_t Triangle = 0U; Triangle < grid->TriangleCount; Triangle += 17U)
    {
        grid->Flags[Triangle] = TERRAIN_TRIANGLE_FLAG_HOLE;
    }
    for (uint32_t Triangle = 5U; Triangle < grid->TriangleCount; Triangle += 23U)
    {
        grid->Flags[Triangle] = TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL;
        grid->Materials[Triangle] = (uint8_t)(Triangle % 3U);
    }
    TerrainMeshData Data = TestTerrain_GetData(grid, 3U, 2U, 8.0f);
    Data.NormalMode = mode;
    TerrainMesh* Terrain = NULL;
    TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Data, NULL, &Terrain));
    return Terrain;
}

static void TheBinaryFormReadsBackBitForBit(TestRun* run)
{
    const TerrainNormalMode Modes[2] = { TerrainNormalMode_Smooth, TerrainNormalMode_Imported };
    for (size_t Case = 0U; Case < 2U; Case++)
    {
        TestTerrainGrid Grid;
        TerrainMesh* Terrain = BuildVariedMesh(run, &Grid, Modes[Case]);
        GenericBuffer Bytes;
        GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
        TerrainMesh* Read = NULL;
        if ((Terrain != NULL) && TEST_CHECK_SUCCESS(run, TerrainMesh_WriteGHDF(Terrain, &Bytes)))
        {
            TEST_CHECK_SUCCESS(run, TerrainMesh_ReadGHDF(Bytes._data, Bytes._count, 8.0f, NULL, &Read));
        }
        if (Read != NULL)
        {
            TEST_CHECK(run, TestTerrain_AreMeshesEqual(run, Terrain, Read));
            // Written again, the same bytes.
            GenericBuffer Again;
            GenericBuffer_AllocateVariable(&Again, 0U, sizeof(unsigned char));
            TEST_CHECK_SUCCESS(run, TerrainMesh_WriteGHDF(Read, &Again));
            TEST_CHECK(run, (Again._count == Bytes._count) && (memcmp(Again._data, Bytes._data, Bytes._count) == 0));
            Memory_Free(Again._data);
        }
        TerrainMesh_Destroy(Read);
        TerrainMesh_Destroy(Terrain);
        Memory_Free(Bytes._data);
        TestTerrain_FreeGrid(&Grid);
    }
}

static void ASavedFileLoadsBack(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, 0U, sizeof(unsigned char));
    TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "a_saved_file_loads_back", &Directory));
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Path, Directory._data);
    GenericBuffer_AppendString(&Path, (const unsigned char*)u8"/terrains/hills.terrain.ghdf");
    GenericBuffer_NullTerminate(&Path);

    TestTerrainGrid Grid;
    TerrainMesh* Terrain = BuildVariedMesh(run, &Grid, TerrainNormalMode_Smooth);
    TerrainMesh* Loaded = NULL;
    if ((Terrain != NULL) && TEST_CHECK_SUCCESS(run, TerrainMesh_SaveGHDF(Terrain, Path._data)))
    {
        TEST_CHECK_SUCCESS(run, TerrainMesh_LoadGHDF(Path._data, 8.0f, NULL, &Loaded));
    }
    if (Loaded != NULL)
    {
        TEST_CHECK(run, TestTerrain_AreMeshesEqual(run, Terrain, Loaded));
    }
    size_t FileCount = 0U;
    GenericBuffer Folder;
    GenericBuffer_AllocateVariable(&Folder, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Folder, Directory._data);
    GenericBuffer_AppendString(&Folder, (const unsigned char*)u8"/terrains");
    GenericBuffer_NullTerminate(&Folder);
    TEST_CHECK_SUCCESS(run, Test_CountFiles(Folder._data, &FileCount));
    TEST_CHECK(run, FileCount == 1U); // the temporary file is gone
    TerrainMesh_Destroy(Loaded);
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
    Memory_Free(Folder._data);
    Memory_Free(Path._data);
    Memory_Free(Directory._data);
}

/* Reads @p bytes and checks the refusal's code and that its message holds @p text. */
static void CheckReadRefused(TestRun* run, const unsigned char* bytes, size_t count, ErrorCode code, const char* text)
{
    TerrainMesh* Terrain = NULL;
    Error Result = TerrainMesh_ReadGHDF(bytes, count, 8.0f, NULL, &Terrain);
    TEST_CHECK(run, Result.Code == code);
    TEST_CHECK(run, Terrain == NULL);
    if (!TEST_CHECK(run, MessageContains(&Result, text)))
    {
        printf("    expected \"%s\" in: %s\n", text, (Result.Message != NULL) ? (const char*)Result.Message : "(none)");
    }
    Error_Deconstruct(&Result);
    TerrainMesh_Destroy(Terrain);
}

/* A copy of the document @p bytes with one entry of its root replaced by @p value (or removed when it is NULL). */
static void Rewrite(const GenericBuffer* bytes, GHDFEntryID id, const GHDFObjectValue* value, GHDFCompoundEntryType type,
    GenericBuffer* outBytes)
{
    GHDFObjectPool* Pool = NULL;
    Error Result = GHDFObjectPool_Create(&Pool);
    Error_Deconstruct(&Result);
    GenericBuffer Source;
    GenericBuffer_CreateReadOnly(&Source, bytes->_data, bytes->_count, sizeof(unsigned char), bytes->_count);
    MemoryStream Stream;
    Result = MemoryStream_Construct2(&Stream, &Source, IOStreamFlags_CanRead);
    Error_Deconstruct(&Result);
    GHDFCompound* Root = NULL;
    Result = GHDF_Read(MemoryStream_AsIOStream(&Stream), Pool, &Root);
    Error_Deconstruct(&Result);
    MemoryStream_Deconstruct(&Stream);
    if (value == NULL)
    {
        Result = GHDFCompound_Remove(Root, id);
    }
    else
    {
        Result = GHDFCompound_SetValue(Root, id, type, value);
    }
    Error_Deconstruct(&Result);
    MemoryStream Out;
    Result = MemoryStream_Construct1(&Out, IOStreamFlags_CanWrite);
    Error_Deconstruct(&Result);
    Result = GHDF_Write(Root, MemoryStream_AsIOStream(&Out));
    Error_Deconstruct(&Result);
    GenericBuffer* Written = MemoryStream_GetBuffer(&Out);
    GenericBuffer_AppendRangeBytes(outBytes, (unsigned char*)Written->_data, Written->_count);
    MemoryStream_Deconstruct(&Out);
    Result = GHDFObjectPool_Deconstruct(Pool);
    Error_Deconstruct(&Result);
}

static void HostileBinaryFilesAreRefused(TestRun* run)
{
    TestTerrainGrid Grid;
    TerrainMesh* Terrain = BuildVariedMesh(run, &Grid, TerrainNormalMode_Smooth);
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    if ((Terrain == NULL) || !TEST_CHECK_SUCCESS(run, TerrainMesh_WriteGHDF(Terrain, &Bytes)))
    {
        TerrainMesh_Destroy(Terrain);
        Memory_Free(Bytes._data);
        TestTerrain_FreeGrid(&Grid);
        return;
    }
    CheckReadRefused(run, Bytes._data, Bytes._count / 2U, ErrorCode_Deserialize, "does not read");
    GenericBuffer Longer;
    GenericBuffer_AllocateVariable(&Longer, 0U, sizeof(unsigned char));
    GenericBuffer_AppendRangeBytes(&Longer, Bytes._data, Bytes._count);
    GenericBuffer_AppendByte(&Longer, 0U);
    CheckReadRefused(run, Longer._data, Longer._count, ErrorCode_Deserialize, "bytes after its document");
    Memory_Free(Longer._data);
    CheckReadRefused(run, Bytes._data, TERRAIN_FILE_MAX_BYTES + 1U, ErrorCode_BufferTooLarge, "at most");

    struct
    {
        GHDFEntryID Id;
        bool IsRemoved;
        GHDFObjectValue Value;
        GHDFCompoundEntryType Type;
        ErrorCode Code;
        const char* Text;
    } Cases[] =
    {
        { 1U, false, GHDFObjectValue_CreateUInt8(2U), { GHDFValueType_UInt8, false }, ErrorCode_Deserialize,
            "format version" },
        { 2U, true, { 0 }, { GHDFValueType_None, false }, ErrorCode_Deserialize, "vertex count" },
        { 2U, false, GHDFObjectValue_CreateUInt32(TERRAIN_MESH_MAX_VERTICES + 1U), { GHDFValueType_UInt32, false },
            ErrorCode_Deserialize, "vertex count" },
        { 2U, false, GHDFObjectValue_CreateUInt32(5U), { GHDFValueType_UInt32, false }, ErrorCode_Deserialize,
            "positions (entry 7) holds" },
        { 4U, false, GHDFObjectValue_CreateUInt8(17U), { GHDFValueType_UInt8, false }, ErrorCode_Deserialize,
            "palette" },
        { 6U, false, GHDFObjectValue_CreateUInt8(2U), { GHDFValueType_UInt8, false }, ErrorCode_Deserialize,
            "normal mode" },
        { 7U, false, GHDFObjectValue_CreateDouble(1.0), { GHDFValueType_Double, false }, ErrorCode_Deserialize,
            "positions (entry 7) must be there" },
        { 11U, true, { 0 }, { GHDFValueType_None, false }, ErrorCode_Deserialize, "indices" },
    };
    for (size_t Case = 0U; Case < (sizeof(Cases) / sizeof(Cases[0])); Case++)
    {
        GenericBuffer Changed;
        GenericBuffer_AllocateVariable(&Changed, 0U, sizeof(unsigned char));
        Rewrite(&Bytes, Cases[Case].Id, Cases[Case].IsRemoved ? NULL : &Cases[Case].Value, Cases[Case].Type, &Changed);
        CheckReadRefused(run, Changed._data, Changed._count, Cases[Case].Code, Cases[Case].Text);
        Memory_Free(Changed._data);
    }
    TerrainMesh_Destroy(Terrain);
    Memory_Free(Bytes._data);
    TestTerrain_FreeGrid(&Grid);
}


// Tests: collision.
/* Checks a chunk's collision mesh against the terrain: every surface triangle once (holes left out), each with its
 * terrain triangle as its source, that triangle's edge flags, and its corners at the terrain's positions less the
 * chunk's centre, exactly. */
static void CheckChunkCollisionMesh(TestRun* run, const TerrainMesh* terrain, uint32_t chunk, const CollisionMesh* mesh)
{
    const TerrainChunk* Chunk = TerrainMesh_GetChunk(terrain, chunk);
    if (!TEST_CHECK(run, (mesh != NULL) && (mesh->_triangleCount == TerrainChunk_GetSurfaceTriangleCount(Chunk))))
    {
        return;
    }
    uint8_t* Seen = Memory_Allocate(terrain->_triangleCount);
    Memory_Zero(Seen, terrain->_triangleCount);
    Vector3Double Center = TerrainChunk_GetCenter(Chunk);
    bool AreRight = true;
    for (uint32_t Triangle = 0U; AreRight && (Triangle < mesh->_triangleCount); Triangle++)
    {
        const CollisionMeshTriangle* Entry = &mesh->_triangles[Triangle];
        uint32_t Source = Entry->_sourceIndex;
        AreRight = (Source < terrain->_triangleCount) && (Seen[Source] == 0U)
            && (TerrainMesh_GetTriangleChunk(terrain, Source) == chunk);
        if (!AreRight)
        {
            break;
        }
        Seen[Source] = 1U;
        const TerrainTriangle* Original = TerrainMesh_GetTriangle(terrain, Source);
        AreRight = ((Original->_flags & TERRAIN_TRIANGLE_FLAG_HOLE) == 0U)
            && (Entry->_activeEdges == Original->_activeEdges);
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            Vector3 Local = mesh->_vertices[Entry->_indices[Corner]];
            Vector3 Expected = TerrainMesh_GetPosition(terrain, Original->_indices[Corner]);
            AreRight = AreRight && (((double)Local.x + Center.X) == (double)Expected.x)
                && (((double)Local.y + Center.Y) == (double)Expected.y)
                && (((double)Local.z + Center.Z) == (double)Expected.z);
        }
    }
    TEST_CHECK(run, AreRight);
    Memory_Free(Seen);
}

/* Each chunk's collision mesh is its surface: holes left out, a face material's split vertices welded back, the
 * terrain's edge flags (so the seams between chunks are as the whole terrain has them) and its triangle indices; a chunk
 * of holes only has none, and the arguments are checked. */
static void AChunksCollisionMeshKeepsTheTerrainsEdgesAndTriangles(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 12U, 12U, 1.0f, -6.0f, -6.0f, Hills);
    Grid.Flags[TestTerrain_Triangle(&Grid, 2U, 2U)] = TERRAIN_TRIANGLE_FLAG_HOLE;
    for (uint32_t I = 3U; I < 6U; I++)
    {
        Grid.Flags[TestTerrain_Triangle(&Grid, I, 4U)] = TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL;
        Grid.Materials[TestTerrain_Triangle(&Grid, I, 4U)] = 1U;
    }
    TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 2U, 0U, 8.0f);
    if ((Terrain != NULL) && TEST_CHECK(run, TerrainMesh_GetChunkCount(Terrain) == 4U))
    {
        uint32_t InactiveSeams = 0U;
        uint32_t PatchChunk = TerrainMesh_GetTriangleChunk(Terrain, TestTerrain_Triangle(&Grid, 4U, 4U));
        for (uint32_t Chunk = 0U; Chunk < 4U; Chunk++)
        {
            CollisionMesh* Collision = NULL;
            if (!TEST_CHECK_SUCCESS(run, TerrainMesh_CreateChunkCollisionMesh(Terrain, Chunk, &Collision)))
            {
                continue;
            }
            CheckChunkCollisionMesh(run, Terrain, Chunk, Collision);
            // The patch's chunk has more surface vertices than welded collision vertices (the split ones weld back).
            uint32_t SurfaceVertices = TerrainChunk_GetVertexCount(TerrainMesh_GetChunk(Terrain, Chunk));
            TEST_CHECK(run, (Chunk == PatchChunk) ? (Collision->_vertexCount < SurfaceVertices)
                : (Collision->_vertexCount == SurfaceVertices));
            // The seams (x = 0 and z = 0, inside the terrain): their edges keep what the whole terrain gives them.
            for (uint32_t Triangle = 0U; Triangle < Collision->_triangleCount; Triangle++)
            {
                const CollisionMeshTriangle* Built = &Collision->_triangles[Triangle];
                const TerrainTriangle* Source = TerrainMesh_GetTriangle(Terrain, Built->_sourceIndex);
                for (uint32_t Edge = 0U; Edge < 3U; Edge++)
                {
                    Vector3 A = TerrainMesh_GetPosition(Terrain, Source->_indices[Edge]);
                    Vector3 B = TerrainMesh_GetPosition(Terrain, Source->_indices[(Edge + 1U) % 3U]);
                    bool IsSeam = ((A.x == 0.0f) && (B.x == 0.0f)) || ((A.z == 0.0f) && (B.z == 0.0f));
                    InactiveSeams += (IsSeam && ((Built->_activeEdges & (1U << Edge)) == 0U)) ? 1U : 0U;
                }
            }
            CollisionMesh_Destroy(Collision);
        }
        // Gentle hills: the seams' edges are inactive from both sides, as a mesh of its own would never have them.
        TEST_CHECK(run, InactiveSeams > 0U);
        CollisionMesh* None = NULL;
        TEST_CHECK_ERROR(run, TerrainMesh_CreateChunkCollisionMesh(Terrain, 4U, &None), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, TerrainMesh_CreateChunkCollisionMesh(NULL, 0U, &None), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, TerrainMesh_CreateChunkCollisionMesh(Terrain, 0U, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK(run, None == NULL);
    }
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);

    // Two 8 m chunks, the second all holes: it has no collision mesh.
    TestTerrain_BuildGrid(&Grid, 4U, 2U, 4.0f, 0.0f, 0.0f, NULL);
    for (uint32_t J = 0U; J < 2U; J++)
    {
        for (uint32_t I = 2U; I < 4U; I++)
        {
            Grid.Flags[TestTerrain_Triangle(&Grid, I, J)] = TERRAIN_TRIANGLE_FLAG_HOLE;
            Grid.Flags[TestTerrain_Triangle(&Grid, I, J) + 1U] = TERRAIN_TRIANGLE_FLAG_HOLE;
        }
    }
    Terrain = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    if ((Terrain != NULL) && TEST_CHECK(run, TerrainMesh_GetChunkCount(Terrain) == 2U))
    {
        CollisionMesh* Collision = NULL;
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, TerrainMesh_CreateChunkCollisionMesh(Terrain, FindChunk(Terrain, 1, 0),
            &Collision)) && (Collision == NULL));
        TEST_CHECK_SUCCESS(run, TerrainMesh_CreateChunkCollisionMesh(Terrain, FindChunk(Terrain, 0, 0), &Collision));
        CheckChunkCollisionMesh(run, Terrain, FindChunk(Terrain, 0, 0), Collision);
        CollisionMesh_Destroy(Collision);
    }
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
}

/* A chunk of more than 65536 surface vertices (a part's indices are unsigned shorts) is cut into parts, which the mesh
 * welds back into one surface: every triangle there once, as the terrain has it. */
static void ALargeChunksCollisionMeshIsCutIntoParts(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 256U, 256U, 0.75f, 0.0f, 0.0f, Hills);
    TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 256.0f);
    if ((Terrain != NULL) && TEST_CHECK(run, (TerrainMesh_GetChunkCount(Terrain) == 1U)
        && (TerrainChunk_GetVertexCount(TerrainMesh_GetChunk(Terrain, 0U)) == (257U * 257U))))
    {
        CollisionMesh* Collision = NULL;
        if (TEST_CHECK_SUCCESS(run, TerrainMesh_CreateChunkCollisionMesh(Terrain, 0U, &Collision)))
        {
            TEST_CHECK(run, (Collision->_partCount >= 2U) && (Collision->_vertexCount == (257U * 257U)));
            CheckChunkCollisionMesh(run, Terrain, 0U, Collision);
        }
        CollisionMesh_Destroy(Collision);
    }
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
}
/* A surface cut into parts of at most 10 vertices: the parts take the surface's triangles in order, each within its
 * maximum, and each index names, among its part's own vertices, the surface vertex the triangle has there. */
static void ASurfaceCutIntoSmallPartsKeepsEveryTriangle(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 6U, 6U, 1.0f, 0.0f, 0.0f, Hills);
    Grid.Flags[TestTerrain_Triangle(&Grid, 2U, 3U)] = TERRAIN_TRIANGLE_FLAG_HOLE;
    TerrainMesh* Terrain = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f);
    GenericBuffer Parts;
    GenericBuffer_AllocateVariable(&Parts, 1U, sizeof(TerrainChunkPart));
    GenericBuffer Vertices;
    GenericBuffer_AllocateVariable(&Vertices, 1U, sizeof(uint32_t));
    GenericBuffer Indices;
    GenericBuffer_AllocateVariable(&Indices, 1U, sizeof(uint32_t));
    if ((Terrain != NULL) && TEST_CHECK(run, TerrainMesh_GetChunkCount(Terrain) == 1U)
        && TEST_CHECK_SUCCESS(run, TerrainMesh_SplitChunkSurface(Terrain, 0U, 10U, &Parts, &Vertices, &Indices)))
    {
        const TerrainChunk* Chunk = TerrainMesh_GetChunk(Terrain, 0U);
        const TerrainChunkPart* Cut = (const TerrainChunkPart*)(const void*)Parts._data;
        const uint32_t* Sources = (const uint32_t*)(const void*)Vertices._data;
        const uint32_t* Locals = (const uint32_t*)(const void*)Indices._data;
        uint32_t SurfaceCount = TerrainChunk_GetSurfaceTriangleCount(Chunk);
        TEST_CHECK(run, (SurfaceCount == (Grid.TriangleCount - 1U)) && (Parts._count > 5U)
            && (Indices._count == ((size_t)SurfaceCount * 3U)));
        bool AreRight = true;
        uint32_t NextTriangle = 0U;
        uint32_t NextVertex = 0U;
        for (size_t Index = 0U; Index < Parts._count; Index++)
        {
            AreRight = AreRight && (Cut[Index].FirstTriangle == NextTriangle) && (Cut[Index].FirstVertex == NextVertex)
                && (Cut[Index].VertexCount <= 10U) && (Cut[Index].TriangleCount > 0U);
            for (uint32_t Triangle = Cut[Index].FirstTriangle;
                Triangle < (Cut[Index].FirstTriangle + Cut[Index].TriangleCount); Triangle++)
            {
                for (uint32_t Corner = 0U; Corner < 3U; Corner++)
                {
                    uint32_t Local = Locals[(Triangle * 3U) + Corner];
                    AreRight = AreRight && (Local < Cut[Index].VertexCount)
                        && (Sources[Cut[Index].FirstVertex + Local] == TerrainChunk_GetIndices(Chunk)[(Triangle * 3U)
                            + Corner]);
                }
            }
            NextTriangle += Cut[Index].TriangleCount;
            NextVertex += Cut[Index].VertexCount;
        }
        TEST_CHECK(run, AreRight && (NextTriangle == SurfaceCount) && (NextVertex == (uint32_t)Vertices._count));
        TEST_CHECK_ERROR(run, TerrainMesh_SplitChunkSurface(Terrain, 0U, 2U, &Parts, &Vertices, &Indices),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, TerrainMesh_SplitChunkSurface(Terrain, 1U, 10U, &Parts, &Vertices, &Indices),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, TerrainMesh_SplitChunkSurface(Terrain, 0U, 10U, NULL, &Vertices, &Indices),
            ErrorCode_IllegalArgument);
    }
    Memory_Free(Indices._data);
    Memory_Free(Vertices._data);
    Memory_Free(Parts._data);
    TerrainMesh_Destroy(Terrain);
    TestTerrain_FreeGrid(&Grid);
}

// Public functions.
void TerrainMeshTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "positions_snap_to_the_grid", PositionsSnapToTheGrid },
        { "a_grid_is_cut_into_chunks_by_its_triangles_centres", AGridIsCutIntoChunksByItsTrianglesCentres },
        { "a_triangle_belongs_to_the_cell_of_its_centre", ATriangleBelongsToTheCellOfItsCentre },
        { "chunk_positions_plus_their_centre_give_the_terrain_back_exactly",
            ChunkPositionsPlusTheirCentreGiveTheTerrainBackExactly },
        { "bounds_hold_their_surfaces", BoundsHoldTheirSurfaces },
        { "smooth_normals_are_the_whole_meshs_on_both_sides_of_a_seam",
            SmoothNormalsAreTheWholeMeshsOnBothSidesOfASeam },
        { "holes_are_left_out_of_the_surface_and_the_normals", HolesAreLeftOutOfTheSurfaceAndTheNormals },
        { "a_face_material_splits_its_vertices_from_the_painted_ones",
            AFaceMaterialSplitsItsVerticesFromThePaintedOnes },
        { "every_limit_is_refused_with_a_message", EveryLimitIsRefusedWithAMessage },
        { "edges_are_active_by_the_whole_terrain", EdgesAreActiveByTheWholeTerrain },
        { "normals_weigh_each_triangle_by_its_area", NormalsWeighEachTriangleByItsArea },
        { "edges_shared_badly_stay_active", EdgesSharedBadlyStayActive },
        { "a_change_rebuilds_only_its_chunks_and_their_neighbours", AChangeRebuildsOnlyItsChunksAndTheirNeighbours },
        { "the_binary_form_reads_back_bit_for_bit", TheBinaryFormReadsBackBitForBit },
        { "a_saved_file_loads_back", ASavedFileLoadsBack },
        { "hostile_binary_files_are_refused", HostileBinaryFilesAreRefused },
        { "a_chunks_collision_mesh_keeps_the_terrains_edges_and_triangles",
            AChunksCollisionMeshKeepsTheTerrainsEdgesAndTriangles },
        { "a_large_chunks_collision_mesh_is_cut_into_parts", ALargeChunksCollisionMeshIsCutIntoParts },
        { "a_surface_cut_into_small_parts_keeps_every_triangle", ASurfaceCutIntoSmallPartsKeepsEveryTriangle },
    };
    TestRun_RunSuite(run, "TerrainMesh", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
