#include "TestSuites.h"
#include "TestTerrain.h"
#include "TerrainMesh.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
#define GLTF_FLOAT 5126U
#define GLTF_UNSIGNED_BYTE 5121U
#define GLTF_UNSIGNED_SHORT 5123U


// Fields.
static const unsigned char* const Palette[3] = { u8"grass", u8"dirt", u8"rock" };


// Static functions: helpers.
static float Bumps(float x, float z)
{
    return 0.25f * sinf(x * 1.3f) * cosf(z * 0.9f);
}

static bool MessageContains(const Error* error, const char* text)
{
    return (error->Message != NULL) && (strstr((const char*)error->Message, text) != NULL);
}

/* Options with the three-material palette, two density channels and no mappings. */
static TerrainGltfOptions MakeOptions(void)
{
    return (TerrainGltfOptions)
    {
        .Weights = NULL, .WeightMappingCount = 0U, .Densities = NULL, .DensityMappingCount = 0U,
        .MaterialNames = Palette, .MaterialCount = 3U, .DensityChannelCount = 2U, .BlendSlotName = NULL,
        .NormalMode = TerrainNormalMode_Smooth, .ChunkSize = 16.0f, .FilePath = NULL, .Logger = NULL,
        .Description = (const unsigned char*)u8"Terrain \"test\""
    };
}

/* A one-primitive file of a grid in the blend slot. */
static TestGltfMesh MakeGridFile(const TestTerrainGrid* grid, const char* const* slots, const uint32_t* counts)
{
    return (TestGltfMesh)
    {
        .Positions = grid->Positions, .Normals = grid->Normals, .HasNormals = true, .VertexCount = grid->VertexCount,
        .Attributes = NULL, .AttributeCount = 0U, .Indices = grid->Indices, .TriangleCount = grid->TriangleCount,
        .MaterialNames = slots, .PrimitiveTriangleCounts = counts, .PrimitiveModes = NULL, .PrimitiveCount = 1U,
        .NodeTranslation = { 0.0f, 0.0f, 0.0f }, .NodeScale = 1.0f
    };
}

static Error Import(const GenericBuffer* bytes, const TerrainGltfOptions* options, TerrainMesh** outMesh)
{
    return TerrainMesh_CreateFromGltf(bytes->_data, bytes->_count, options, outMesh);
}

/* Checks that importing @p bytes fails with @p code and a message holding @p text. */
static void CheckImportRefused(TestRun* run, const GenericBuffer* bytes, const TerrainGltfOptions* options,
    ErrorCode code, const char* text)
{
    TerrainMesh* Terrain = NULL;
    Error Result = Import(bytes, options, &Terrain);
    TEST_CHECK(run, Result.Code == code);
    TEST_CHECK(run, Terrain == NULL);
    if (!TEST_CHECK(run, MessageContains(&Result, text)))
    {
        printf("    expected \"%s\" in: %s\n", text, (Result.Message != NULL) ? (const char*)Result.Message : "(none)");
    }
    Error_Deconstruct(&Result);
    TerrainMesh_Destroy(Terrain);
}

/* Checks the import of a copy of @p bytes with @p find replaced in its JSON. */
static void CheckPatchedRefused(TestRun* run, const GenericBuffer* bytes, const char* find, const char* replacement,
    ErrorCode code, const char* text)
{
    GenericBuffer Patched;
    GenericBuffer_AllocateVariable(&Patched, 0U, sizeof(unsigned char));
    if (TEST_CHECK(run, TestTerrain_ReplaceInGlbJson(bytes, find, replacement, &Patched)))
    {
        TerrainGltfOptions Options = MakeOptions();
        CheckImportRefused(run, &Patched, &Options, code, text);
    }
    Memory_Free(Patched._data);
}


// Tests.
static void AGlbImportsItsPositionsIndicesAndNodeTransform(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 6U, 4U, 1.5f, -3.0f, 2.0f, Bumps);
    const char* Slots[1] = { "blend" };
    uint32_t Counts[1] = { Grid.TriangleCount };
    TestGltfMesh File = MakeGridFile(&Grid, Slots, Counts);
    File.NodeTranslation = (Vector3){ 10.25f, 2.0f, -5.5f };
    File.NodeScale = 2.0f;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    TerrainGltfOptions Options = MakeOptions();
    TerrainMesh* Terrain = NULL;
    if (TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Terrain)))
    {
        TEST_CHECK(run, TerrainMesh_GetVertexCount(Terrain) == Grid.VertexCount);
        TEST_CHECK(run, TerrainMesh_GetTriangleCount(Terrain) == Grid.TriangleCount);
        bool AreMoved = true;
        for (uint32_t Vertex = 0U; Vertex < Grid.VertexCount; Vertex++)
        {
            Vector3 P = Grid.Positions[Vertex];
            Vector3 Q = TerrainMesh_GetPosition(Terrain, Vertex);
            AreMoved = AreMoved && (fabs((double)Q.x - ((2.0 * (double)P.x) + 10.25)) <= TERRAIN_POSITION_QUANTUM)
                && (fabs((double)Q.y - ((2.0 * (double)P.y) + 2.0)) <= TERRAIN_POSITION_QUANTUM)
                && (fabs((double)Q.z - ((2.0 * (double)P.z) - 5.5)) <= TERRAIN_POSITION_QUANTUM);
        }
        TEST_CHECK(run, AreMoved);
        bool AreSame = true;
        for (uint32_t Triangle = 0U; Triangle < Grid.TriangleCount; Triangle++)
        {
            AreSame = AreSame && (memcmp(TerrainMesh_GetTriangle(Terrain, Triangle)->_indices,
                &Grid.Indices[Triangle * 3U], 3U * sizeof(uint32_t)) == 0);
        }
        TEST_CHECK(run, AreSame);
        TEST_CHECK(run, TerrainMesh_GetNormal(Terrain, 7U).y > 0.9f);
    }
    TerrainMesh_Destroy(Terrain);

    // A mirroring node (scale -1) turns the winding back: the surface faces down, as its turned normals do.
    Memory_Free(Bytes._data);
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    File.NodeScale = -1.0f;
    TestTerrain_WriteGlb(&File, &Bytes);
    if (TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Terrain)))
    {
        TEST_CHECK(run, TerrainMesh_GetNormal(Terrain, 7U).y < -0.9f);
        TEST_CHECK(run, TerrainMesh_GetTriangle(Terrain, 0U)->_indices[1] == Grid.Indices[2]);
    }
    TerrainMesh_Destroy(Terrain);
    Memory_Free(Bytes._data);
    TestTerrain_FreeGrid(&Grid);
}

static void ANodeUnderATurnedParentIsPlacedThroughBoth(TestRun* run)
{
    // The parent stands at (100, 0, 0) turned 90 degrees about Y (+X goes to -Z), the mesh's node 1 m along the
    // parent's X: a vertex at the mesh's origin lands at (100, 0, -1).
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 2U, 2U, 1.0f, 0.0f, 0.0f, NULL);
    const char* Slots[1] = { "blend" };
    uint32_t Counts[1] = { Grid.TriangleCount };
    TestGltfMesh File = MakeGridFile(&Grid, Slots, Counts);
    File.NodeTranslation = (Vector3){ 1.0f, 0.0f, 0.0f };
    File.HasParent = true;
    File.ParentTranslation = (Vector3){ 100.0f, 0.0f, 0.0f };
    File.ParentRotation[0] = 0.0f;
    File.ParentRotation[1] = 0.70710678f;
    File.ParentRotation[2] = 0.0f;
    File.ParentRotation[3] = 0.70710678f;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    TerrainGltfOptions Options = MakeOptions();
    TerrainMesh* Terrain = NULL;
    if (TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Terrain)))
    {
        // Vertex (0, 0) at the origin, vertex (1, 0) one metre along local X: turned to -Z.
        Vector3 Origin = TerrainMesh_GetPosition(Terrain, TestTerrain_Vertex(&Grid, 0U, 0U));
        Vector3 AlongX = TerrainMesh_GetPosition(Terrain, TestTerrain_Vertex(&Grid, 1U, 0U));
        TEST_CHECK(run, (fabsf(Origin.x - 100.0f) < 0.001f) && (fabsf(Origin.y) < 0.001f)
            && (fabsf(Origin.z + 1.0f) < 0.001f));
        TEST_CHECK(run, (fabsf(AlongX.x - 100.0f) < 0.001f) && (fabsf(AlongX.z + 2.0f) < 0.001f));
        // A turn is no mirror: the surface still faces up.
        TEST_CHECK(run, TerrainMesh_GetNormal(Terrain, 4U).y > 0.99f);
    }
    TerrainMesh_Destroy(Terrain);
    Memory_Free(Bytes._data);
    TestTerrain_FreeGrid(&Grid);
}

static void EqualPositionsAreWeldedAndSeamsAveraged(TestRun* run)
{
    // Every triangle of a grid with its own three vertices (what Blender writes where face corners differ): welded
    // back to the grid's vertices. Vertex (2, 2)'s copies carry other weights: they average.
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 4U, 4U, 1.0f, 0.0f, 0.0f, NULL);
    uint32_t Corners = Grid.TriangleCount * 3U;
    Vector3* Positions = Memory_Allocate(Corners * sizeof(Vector3));
    Vector3* Normals = Memory_Allocate(Corners * sizeof(Vector3));
    float* Colours = Memory_Allocate((size_t)Corners * 4U * sizeof(float));
    uint32_t* Indices = Memory_Allocate(Corners * sizeof(uint32_t));
    uint32_t Shared = TestTerrain_Vertex(&Grid, 2U, 2U);
    uint32_t SharedCopies = 0U;
    for (uint32_t Corner = 0U; Corner < Corners; Corner++)
    {
        uint32_t Source = Grid.Indices[Corner];
        Positions[Corner] = Grid.Positions[Source];
        Normals[Corner] = (Vector3){ 0.0f, 1.0f, 0.0f };
        Indices[Corner] = Corner;
        float Red = 1.0f;
        if (Source == Shared)
        {
            // Six copies: three at 1, three at 0.
            Red = ((SharedCopies++ % 2U) == 0U) ? 1.0f : 0.0f;
        }
        Colours[(Corner * 4U) + 0U] = Red;
        Colours[(Corner * 4U) + 1U] = 0.0f;
        Colours[(Corner * 4U) + 2U] = 0.0f;
        Colours[(Corner * 4U) + 3U] = 1.0f;
    }
    TestGltfAttribute Attribute = { .Name = "COLOR_0", .ChannelCount = 4U, .ComponentType = GLTF_FLOAT,
        .Values = Colours };
    const char* Slots[1] = { "blend" };
    uint32_t Counts[1] = { Grid.TriangleCount };
    TestGltfMesh File = { .Positions = Positions, .Normals = Normals, .HasNormals = true, .VertexCount = Corners,
        .Attributes = &Attribute, .AttributeCount = 1U, .Indices = Indices, .TriangleCount = Grid.TriangleCount,
        .MaterialNames = Slots, .PrimitiveTriangleCounts = Counts, .PrimitiveModes = NULL, .PrimitiveCount = 1U,
        .NodeTranslation = { 0.0f, 0.0f, 0.0f }, .NodeScale = 1.0f };
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    TerrainAttributeMapping Mapping = { .Attribute = (const unsigned char*)u8"COLOR_0", .Targets = { 0, -1, -1, 2 } };
    TerrainGltfOptions Options = MakeOptions();
    Options.Weights = &Mapping;
    Options.WeightMappingCount = 1U;
    TerrainMesh* Terrain = NULL;
    if (TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Terrain)))
    {
        TEST_CHECK(run, SharedCopies == 6U);
        TEST_CHECK(run, TerrainMesh_GetVertexCount(Terrain) == Grid.VertexCount);
        TEST_CHECK(run, TerrainMesh_GetTriangleCount(Terrain) == Grid.TriangleCount);
        // The first of the shared vertex's copies is where it lands; find it by position.
        bool IsFound = false;
        for (uint32_t Vertex = 0U; Vertex < TerrainMesh_GetVertexCount(Terrain); Vertex++)
        {
            Vector3 P = TerrainMesh_GetPosition(Terrain, Vertex);
            if ((P.x == 2.0f) && (P.z == 2.0f))
            {
                IsFound = true;
                // (3 x 255 + 3 x 0) / 6, rounded: 128; the alpha channel feeds rock fully.
                TEST_CHECK(run, TerrainMesh_GetWeights(Terrain, Vertex)[0] == 128U);
                TEST_CHECK(run, TerrainMesh_GetWeights(Terrain, Vertex)[2] == 255U);
            }
        }
        TEST_CHECK(run, IsFound);
        TEST_CHECK(run, TerrainMesh_GetWeights(Terrain, 0U)[0] == 255U);
    }
    TerrainMesh_Destroy(Terrain);
    Memory_Free(Bytes._data);
    Memory_Free(Positions);
    Memory_Free(Normals);
    Memory_Free(Colours);
    Memory_Free(Indices);
    TestTerrain_FreeGrid(&Grid);
}

static void ColourAttributesOfEveryKindFeedWeightsAndDensities(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 2U, 2U, 1.0f, 0.0f, 0.0f, NULL);
    uint32_t Vertices = Grid.VertexCount;
    float* Bytes4 = Memory_Allocate(Vertices * 4U * sizeof(float));
    float* Shorts3 = Memory_Allocate(Vertices * 3U * sizeof(float));
    float* Floats4 = Memory_Allocate(Vertices * 4U * sizeof(float));
    float* Scalar = Memory_Allocate(Vertices * sizeof(float));
    for (uint32_t Vertex = 0U; Vertex < Vertices; Vertex++)
    {
        float T = (float)Vertex / (float)(Vertices - 1U);
        for (uint32_t Channel = 0U; Channel < 4U; Channel++)
        {
            Bytes4[(Vertex * 4U) + Channel] = (Channel == 0U) ? T : 0.0f;
            // Floats past 0..1 are clamped.
            Floats4[(Vertex * 4U) + Channel] = (Channel == 1U) ? ((2.0f * T) - 0.5f) : 0.0f;
        }
        for (uint32_t Channel = 0U; Channel < 3U; Channel++)
        {
            Shorts3[(Vertex * 3U) + Channel] = (Channel == 2U) ? (1.0f - T) : 0.0f;
        }
        Scalar[Vertex] = 0.5f;
    }
    TestGltfAttribute Attributes[4] =
    {
        { .Name = "COLOR_0", .ChannelCount = 4U, .ComponentType = GLTF_UNSIGNED_BYTE, .Values = Bytes4 },
        { .Name = "_WEIGHTS_B", .ChannelCount = 3U, .ComponentType = GLTF_UNSIGNED_SHORT, .Values = Shorts3 },
        { .Name = "_WEIGHTS_C", .ChannelCount = 4U, .ComponentType = GLTF_FLOAT, .Values = Floats4 },
        { .Name = "_DENSITY_0", .ChannelCount = 1U, .ComponentType = GLTF_FLOAT, .Values = Scalar },
    };
    const char* Slots[1] = { "blend" };
    uint32_t Counts[1] = { Grid.TriangleCount };
    TestGltfMesh File = MakeGridFile(&Grid, Slots, Counts);
    File.Attributes = Attributes;
    File.AttributeCount = 4U;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    TerrainAttributeMapping Weights[3] =
    {
        { .Attribute = (const unsigned char*)u8"COLOR_0", .Targets = { 0, -1, -1, -1 } },
        { .Attribute = (const unsigned char*)u8"_WEIGHTS_B", .Targets = { -1, -1, 1, -1 } },
        { .Attribute = (const unsigned char*)u8"_WEIGHTS_C", .Targets = { -1, 2, -1, -1 } },
    };
    TerrainAttributeMapping Densities[1] = { { .Attribute = (const unsigned char*)u8"_DENSITY_0", .Targets = { 1, -1, -1,
        -1 } } };
    TerrainGltfOptions Options = MakeOptions();
    Options.Weights = Weights;
    Options.WeightMappingCount = 3U;
    Options.Densities = Densities;
    Options.DensityMappingCount = 1U;
    TerrainMesh* Terrain = NULL;
    if (TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Terrain)))
    {
        bool AreRight = true;
        for (uint32_t Vertex = 0U; Vertex < Vertices; Vertex++)
        {
            float T = (float)Vertex / (float)(Vertices - 1U);
            const uint8_t* W = TerrainMesh_GetWeights(Terrain, Vertex);
            uint8_t ByteT = (uint8_t)lroundf(T * 255.0f);
            uint8_t ShortT = (uint8_t)lroundf(((float)lroundf((1.0f - T) * 65535.0f) / 65535.0f) * 255.0f);
            uint8_t FloatT = (uint8_t)lroundf(fminf(fmaxf((2.0f * T) - 0.5f, 0.0f), 1.0f) * 255.0f);
            AreRight = AreRight && (W[0] == ByteT) && (W[1] == ShortT) && (W[2] == FloatT)
                && (TerrainMesh_GetDensities(Terrain, Vertex)[1] == 128U)
                && (TerrainMesh_GetDensities(Terrain, Vertex)[0] == 0U);
        }
        TEST_CHECK(run, AreRight);
    }
    TerrainMesh_Destroy(Terrain);

    // What the definition maps must be in the file, in a shape the terrain reads.
    TerrainAttributeMapping Missing = { .Attribute = (const unsigned char*)u8"_NOT_THERE", .Targets = { 0, -1, -1, -1 } };
    Options.Weights = &Missing;
    Options.WeightMappingCount = 1U;
    Options.DensityMappingCount = 0U;
    CheckImportRefused(run, &Bytes, &Options, ErrorCode_InvalidAssetData, "without the attribute \"_NOT_THERE\"");
    TerrainAttributeMapping AlphaOfRgb = { .Attribute = (const unsigned char*)u8"_WEIGHTS_B", .Targets = { -1, -1, -1,
        0 } };
    Options.Weights = &AlphaOfRgb;
    CheckImportRefused(run, &Bytes, &Options, ErrorCode_InvalidAssetData, "has 3 channels; the definition maps channel 4");
    TerrainAttributeMapping PastPalette = { .Attribute = (const unsigned char*)u8"COLOR_0", .Targets = { 3, -1, -1, -1 } };
    Options.Weights = &PastPalette;
    CheckImportRefused(run, &Bytes, &Options, ErrorCode_IllegalArgument, "past the 3 there are");

    Memory_Free(Bytes._data);
    Memory_Free(Bytes4);
    Memory_Free(Shorts3);
    Memory_Free(Floats4);
    Memory_Free(Scalar);
    TestTerrain_FreeGrid(&Grid);
}

static void MaterialSlotsSetTheFaceFlags(TestRun* run)
{
    // Four primitives over the grid's triangles: the blend slot, a hole, the palette's rock, and none (blend).
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 4U, 2U, 1.0f, 0.0f, 0.0f, NULL);
    const char* Slots[4] = { "blend", "hole", "rock", NULL };
    uint32_t Counts[4] = { 4U, 2U, 6U, 4U };
    TestGltfMesh File = MakeGridFile(&Grid, Slots, Counts);
    File.PrimitiveCount = 4U;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    TerrainGltfOptions Options = MakeOptions();
    TerrainMesh* Terrain = NULL;
    if (TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Terrain)))
    {
        bool AreRight = true;
        for (uint32_t Triangle = 0U; Triangle < Grid.TriangleCount; Triangle++)
        {
            const TerrainTriangle* Entry = TerrainMesh_GetTriangle(Terrain, Triangle);
            uint8_t Expected = 0U;
            uint8_t FaceMaterial = 0U;
            if ((Triangle >= 4U) && (Triangle < 6U))
            {
                Expected = TERRAIN_TRIANGLE_FLAG_HOLE;
            }
            else if ((Triangle >= 6U) && (Triangle < 12U))
            {
                Expected = TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL;
                FaceMaterial = 2U;
            }
            AreRight = AreRight && (Entry->_flags == Expected) && (Entry->_material == FaceMaterial);
        }
        TEST_CHECK(run, AreRight);
    }
    TerrainMesh_Destroy(Terrain);

    // A blend slot of another name, set by the definition.
    Options.BlendSlotName = (const unsigned char*)u8"painted";
    CheckImportRefused(run, &Bytes, &Options, ErrorCode_InvalidAssetData, "material slot \"blend\"");
    Options.BlendSlotName = NULL;
    Memory_Free(Bytes._data);

    // A slot the terrain does not know is refused, named.
    const char* Unknown[1] = { "grass_2" };
    uint32_t All[1] = { Grid.TriangleCount };
    TestGltfMesh Other = MakeGridFile(&Grid, Unknown, All);
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&Other, &Bytes);
    CheckImportRefused(run, &Bytes, &Options, ErrorCode_InvalidAssetData,
        "mesh \"terrain\" has the material slot \"grass_2\"");
    Memory_Free(Bytes._data);

    // Lines are not a terrain.
    const char* Blend[1] = { "blend" };
    uint32_t Lines[1] = { 1U };
    TestGltfMesh Lined = MakeGridFile(&Grid, Blend, All);
    Lined.PrimitiveModes = Lines;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&Lined, &Bytes);
    CheckImportRefused(run, &Bytes, &Options, ErrorCode_InvalidAssetData,
        "has a primitive of lines (mode 1), not triangles");
    Memory_Free(Bytes._data);
    TestTerrain_FreeGrid(&Grid);
}

static void ImportedNormalsKeepHardEdges(TestRun* run)
{
    // A valley: two slopes meeting at x = 2, each with its own vertices along the fold and its own normals.
    TestTerrainGrid Left;
    TestTerrainGrid Right;
    TestTerrain_BuildGrid(&Left, 2U, 2U, 1.0f, 0.0f, 0.0f, NULL);
    TestTerrain_BuildGrid(&Right, 2U, 2U, 1.0f, 2.0f, 0.0f, NULL);
    uint32_t Count = Left.VertexCount + Right.VertexCount;
    Vector3* Positions = Memory_Allocate(Count * sizeof(Vector3));
    Vector3* Normals = Memory_Allocate(Count * sizeof(Vector3));
    uint32_t* Indices = Memory_Allocate((Left.TriangleCount + Right.TriangleCount) * 3U * sizeof(uint32_t));
    for (uint32_t Vertex = 0U; Vertex < Left.VertexCount; Vertex++)
    {
        Positions[Vertex] = Left.Positions[Vertex];
        Positions[Vertex].y = (2.0f - Positions[Vertex].x) * 0.5f;
        Normals[Vertex] = (Vector3){ 0.4472136f, 0.8944272f, 0.0f };
        Positions[Left.VertexCount + Vertex] = Right.Positions[Vertex];
        Positions[Left.VertexCount + Vertex].y = (Positions[Left.VertexCount + Vertex].x - 2.0f) * 0.5f;
        Normals[Left.VertexCount + Vertex] = (Vector3){ -0.4472136f, 0.8944272f, 0.0f };
    }
    for (uint32_t Corner = 0U; Corner < (Left.TriangleCount * 3U); Corner++)
    {
        Indices[Corner] = Left.Indices[Corner];
        Indices[(Left.TriangleCount * 3U) + Corner] = Right.Indices[Corner] + Left.VertexCount;
    }
    const char* Slots[1] = { "blend" };
    uint32_t Counts[1] = { Left.TriangleCount + Right.TriangleCount };
    TestGltfMesh File = { .Positions = Positions, .Normals = Normals, .HasNormals = true, .VertexCount = Count,
        .Attributes = NULL, .AttributeCount = 0U, .Indices = Indices, .TriangleCount = Counts[0],
        .MaterialNames = Slots, .PrimitiveTriangleCounts = Counts, .PrimitiveModes = NULL, .PrimitiveCount = 1U,
        .NodeTranslation = { 0.0f, 0.0f, 0.0f }, .NodeScale = 1.0f };
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    TerrainGltfOptions Options = MakeOptions();
    TerrainMesh* Smooth = NULL;
    TerrainMesh* Hard = NULL;
    TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Smooth));
    Options.NormalMode = TerrainNormalMode_Imported;
    TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Hard));
    if ((Smooth != NULL) && (Hard != NULL))
    {
        // The fold's three vertices weld into one each with smooth normals; imported ones keep both sides.
        TEST_CHECK(run, TerrainMesh_GetVertexCount(Smooth) == (Count - 3U));
        TEST_CHECK(run, TerrainMesh_GetVertexCount(Hard) == Count);
        TEST_CHECK(run, TerrainMesh_GetNormalMode(Hard) == TerrainNormalMode_Imported);
        TEST_CHECK(run, TerrainMesh_GetNormal(Hard, 2U).x > 0.4f);
        TEST_CHECK(run, TerrainMesh_GetNormal(Hard, Left.VertexCount).x < -0.4f);
        // Hard edges still join for collision: the fold is concave, so its edges are inactive across the two sides (as
        // a border they would be active). Quad (1, 0)'s second triangle runs along the fold with its third edge.
        uint32_t Fold = TestTerrain_Triangle(&Left, 1U, 0U) + 1U;
        TEST_CHECK(run, (TerrainMesh_GetTriangle(Smooth, Fold)->_activeEdges & 0x4U) == 0U);
        TEST_CHECK(run, (TerrainMesh_GetTriangle(Hard, Fold)->_activeEdges & 0x4U) == 0U);
    }
    TerrainMesh_Destroy(Smooth);
    TerrainMesh_Destroy(Hard);
    Memory_Free(Bytes._data);
    Memory_Free(Positions);
    Memory_Free(Normals);
    Memory_Free(Indices);
    TestTerrain_FreeGrid(&Left);
    TestTerrain_FreeGrid(&Right);
}

static void HostileFilesAreRefused(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 3U, 3U, 1.0f, 0.0f, 0.0f, Bumps);
    const char* Slots[1] = { "blend" };
    uint32_t Counts[1] = { Grid.TriangleCount };
    TestGltfMesh File = MakeGridFile(&Grid, Slots, Counts);
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    TerrainGltfOptions Options = MakeOptions();

    GenericBuffer Empty;
    GenericBuffer_AllocateVariable(&Empty, 0U, sizeof(unsigned char));
    CheckImportRefused(run, &Empty, &Options, ErrorCode_InvalidAssetData, "the file is empty");
    GenericBuffer_AppendString(&Empty, (const unsigned char*)u8"this is not glTF at all");
    CheckImportRefused(run, &Empty, &Options, ErrorCode_InvalidAssetData, "cannot be read as glTF 2.0");
    Memory_Free(Empty._data);

    // The shared checks (source/GltfChecks.h), each naming what it refused.
    CheckPatchedRefused(run, &Bytes, "\"buffers\":[{\"byteLength\":", "\"buffers\":[{\"byteLength\":9",
        ErrorCode_InvalidAssetData, "buffers cannot be loaded");
    CheckPatchedRefused(run, &Bytes, "{\"buffer\":0,\"byteOffset\":0,", "{\"buffer\":0,\"byteOffset\":999999,",
        ErrorCode_InvalidAssetData, "buffer view 0");
    CheckPatchedRefused(run, &Bytes, "{\"buffer\":0,\"byteOffset\":0,", "{\"buffer\":0,\"byteOffset\":0,\"byteStride\":3,",
        ErrorCode_InvalidAssetData, "has a stride of 3 bytes");
    CheckPatchedRefused(run, &Bytes, "\"type\":\"VEC3\",\"count\":", "\"type\":\"VEC3\",\"count\":9",
        ErrorCode_InvalidAssetData, "does not fit in its buffer view");
    CheckPatchedRefused(run, &Bytes, "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],", "\"scenes\":[],",
        ErrorCode_InvalidAssetData, "has no scene");
    CheckPatchedRefused(run, &Bytes, "\"translation\":[0,0,0]", "\"translation\":[9000,0,0]",
        ErrorCode_InvalidAssetData, "positions must be finite and within 8192 m");
    CheckPatchedRefused(run, &Bytes, "\"scale\":[1,1,1]", "\"scale\":[1,0,1]",
        ErrorCode_InvalidAssetData, "flattens it");
    // Buffers past the file limit, alone or together, are refused before any is read.
    CheckPatchedRefused(run, &Bytes, "\"buffers\":[{", "\"buffers\":[{\"uri\":\"far.bin\",\"byteLength\":268435457},{",
        ErrorCode_InvalidAssetData, "buffer 0 declares 268435457 bytes; the buffers hold 1 to 268435456 bytes together.");
    CheckPatchedRefused(run, &Bytes, "\"buffers\":[{", "\"buffers\":[{\"uri\":\"a.bin\",\"byteLength\":200000000},"
        "{\"uri\":\"b.bin\",\"byteLength\":68435457},{", ErrorCode_InvalidAssetData, "buffer 1 declares 68435457 bytes;");

    // What the terrain reads must be stored in the file: an accessor without a buffer view is zeros of any count, memory
    // the file never paid for (a 240-byte file once took 1.9 GiB this way).
    CheckPatchedRefused(run, &Bytes, "{\"bufferView\":0,\"componentType\":5126,", "{\"componentType\":5126,",
        ErrorCode_InvalidAssetData, "mesh \"terrain\"'s positions are not stored in the file (an accessor without a "
        "buffer view)");
    CheckPatchedRefused(run, &Bytes, "\"bufferView\":2,\"componentType\":5123,", "\"componentType\":5123,",
        ErrorCode_InvalidAssetData, "mesh \"terrain\"'s indices are not stored in the file");
    GenericBuffer NoNormals;
    GenericBuffer_AllocateVariable(&NoNormals, 0U, sizeof(unsigned char));
    if (TEST_CHECK(run, TestTerrain_ReplaceInGlbJson(&Bytes, "\"bufferView\":1,\"componentType\":5126,",
        "\"componentType\":5126,", &NoNormals)))
    {
        TerrainGltfOptions Imported = MakeOptions();
        Imported.NormalMode = TerrainNormalMode_Imported;
        CheckImportRefused(run, &NoNormals, &Imported, ErrorCode_InvalidAssetData, "mesh \"terrain\"'s normals are not "
            "stored in the file");
    }
    Memory_Free(NoNormals._data);
    // A small file listing many empty objects: cgltf allocates them all before any check could run, so its allocations
    // have a budget (source/GltfChecks.c).
    GenericBuffer Many;
    GenericBuffer_AllocateVariable(&Many, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Many, (const unsigned char*)u8"\"materials\":[");
    for (uint32_t Entry = 0U; Entry < 200000U; Entry++)
    {
        GenericBuffer_AppendString(&Many, (const unsigned char*)u8"{},");
    }
    GenericBuffer_AppendByte(&Many, 0U);
    CheckPatchedRefused(run, &Bytes, "\"materials\":[", (const char*)Many._data, ErrorCode_InvalidAssetData,
        "reading its glTF structure would take more than 64 MiB (it lists too many objects for its size).");
    Memory_Free(Many._data);

    // A palette material named as a slot the import reads first (holes, the painted weights) could never be a face's.
    const unsigned char* const HolePalette[2] = { u8"grass", u8"hole" };
    TerrainGltfOptions Named = MakeOptions();
    Named.MaterialNames = HolePalette;
    Named.MaterialCount = 2U;
    CheckImportRefused(run, &Bytes, &Named, ErrorCode_IllegalArgument, "palette entry 1 is \"hole\", a material "
        "slot's own name");
    const unsigned char* const BlendPalette[2] = { u8"blend", u8"rock" };
    Named.MaterialNames = BlendPalette;
    CheckImportRefused(run, &Bytes, &Named, ErrorCode_IllegalArgument, "palette entry 0 is \"blend\"");
    // With another blend slot, "blend" is a palette material like any other: the file's "blend" faces take it.
    Named.BlendSlotName = (const unsigned char*)u8"paint";
    TerrainMesh* Renamed = NULL;
    if (TEST_CHECK_SUCCESS(run, Import(&Bytes, &Named, &Renamed)))
    {
        const TerrainTriangle* First = TerrainMesh_GetTriangle(Renamed, 0U);
        TEST_CHECK(run, ((First->_flags & TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL) != 0U) && (First->_material == 0U));
    }
    TerrainMesh_Destroy(Renamed);

    // A mapped attribute without stored values.
    float* Colours = Memory_Allocate((size_t)Grid.VertexCount * 3U * sizeof(float));
    for (size_t Value = 0U; Value < ((size_t)Grid.VertexCount * 3U); Value++)
    {
        Colours[Value] = 0.5f;
    }
    TestGltfAttribute Colour = { .Name = "COLOR_0", .ChannelCount = 3U, .ComponentType = GLTF_FLOAT, .Values = Colours };
    TestGltfMesh Painted = File;
    Painted.Attributes = &Colour;
    Painted.AttributeCount = 1U;
    GenericBuffer PaintedBytes;
    GenericBuffer_AllocateVariable(&PaintedBytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&Painted, &PaintedBytes);
    GenericBuffer Unstored;
    GenericBuffer_AllocateVariable(&Unstored, 0U, sizeof(unsigned char));
    TerrainAttributeMapping Mapping = { .Attribute = (const unsigned char*)u8"COLOR_0", .Targets = { 0, -1, -1, -1 } };
    TerrainGltfOptions Mapped = MakeOptions();
    Mapped.Weights = &Mapping;
    Mapped.WeightMappingCount = 1U;
    if (TEST_CHECK(run, TestTerrain_ReplaceInGlbJson(&PaintedBytes, "\"bufferView\":2,\"componentType\":5126,\"type\":"
        "\"VEC3\"", "\"componentType\":5126,\"type\":\"VEC3\"", &Unstored)))
    {
        CheckImportRefused(run, &Unstored, &Mapped, ErrorCode_InvalidAssetData, "mesh \"terrain\"'s values of "
            "\"COLOR_0\" are not stored in the file");
    }
    Memory_Free(Unstored._data);
    Memory_Free(PaintedBytes._data);
    Memory_Free(Colours);

    // Imported normals need normals.
    File.HasNormals = false;
    Memory_Free(Bytes._data);
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    Options.NormalMode = TerrainNormalMode_Imported;
    CheckImportRefused(run, &Bytes, &Options, ErrorCode_InvalidAssetData, "without float normals");
    Memory_Free(Bytes._data);
    TestTerrain_FreeGrid(&Grid);
}

static void AnImportWrittenAndReadBackIsTheSameMesh(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 20U, 12U, 1.7f, -11.0f, 3.0f, Bumps);
    uint32_t Vertices = Grid.VertexCount;
    float* Colours = Memory_Allocate(Vertices * 4U * sizeof(float));
    for (uint32_t Vertex = 0U; Vertex < Vertices; Vertex++)
    {
        Colours[(Vertex * 4U) + 0U] = (float)(Vertex % 7U) / 6.0f;
        Colours[(Vertex * 4U) + 1U] = (float)(Vertex % 5U) / 4.0f;
        Colours[(Vertex * 4U) + 2U] = 0.25f;
        Colours[(Vertex * 4U) + 3U] = (float)(Vertex % 3U) / 2.0f;
    }
    TestGltfAttribute Attribute = { .Name = "COLOR_0", .ChannelCount = 4U, .ComponentType = GLTF_UNSIGNED_SHORT,
        .Values = Colours };
    const char* Slots[3] = { "blend", "hole", "dirt" };
    uint32_t Counts[3] = { 400U, 30U, Grid.TriangleCount - 430U };
    TestGltfMesh File = MakeGridFile(&Grid, Slots, Counts);
    File.Attributes = &Attribute;
    File.AttributeCount = 1U;
    File.PrimitiveCount = 3U;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TestTerrain_WriteGlb(&File, &Bytes);
    TerrainAttributeMapping Mapping = { .Attribute = (const unsigned char*)u8"COLOR_0", .Targets = { 0, 1, 2, -1 } };
    TerrainAttributeMapping Density = { .Attribute = (const unsigned char*)u8"COLOR_0", .Targets = { -1, -1, -1, 1 } };
    TerrainGltfOptions Options = MakeOptions();
    Options.Weights = &Mapping;
    Options.WeightMappingCount = 1U;
    Options.Densities = &Density;
    Options.DensityMappingCount = 1U;
    TerrainMesh* Imported = NULL;
    TerrainMesh* Read = NULL;
    GenericBuffer Written;
    GenericBuffer_AllocateVariable(&Written, 0U, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Import(&Bytes, &Options, &Imported))
        && TEST_CHECK_SUCCESS(run, TerrainMesh_WriteGHDF(Imported, &Written))
        && TEST_CHECK_SUCCESS(run, TerrainMesh_ReadGHDF(Written._data, Written._count, 16.0f, NULL, &Read)))
    {
        TEST_CHECK(run, TestTerrain_AreMeshesEqual(run, Imported, Read));
    }
    TerrainMesh_Destroy(Imported);
    TerrainMesh_Destroy(Read);
    Memory_Free(Written._data);
    Memory_Free(Bytes._data);
    Memory_Free(Colours);
    TestTerrain_FreeGrid(&Grid);
}


// Public functions.
void TerrainGltfTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_glb_imports_its_positions_indices_and_node_transform", AGlbImportsItsPositionsIndicesAndNodeTransform },
        { "a_node_under_a_turned_parent_is_placed_through_both", ANodeUnderATurnedParentIsPlacedThroughBoth },
        { "equal_positions_are_welded_and_seams_averaged", EqualPositionsAreWeldedAndSeamsAveraged },
        { "colour_attributes_of_every_kind_feed_weights_and_densities",
            ColourAttributesOfEveryKindFeedWeightsAndDensities },
        { "material_slots_set_the_face_flags", MaterialSlotsSetTheFaceFlags },
        { "imported_normals_keep_hard_edges", ImportedNormalsKeepHardEdges },
        { "hostile_files_are_refused", HostileFilesAreRefused },
        { "an_import_written_and_read_back_is_the_same_mesh", AnImportWrittenAndReadBackIsTheSameMesh },
    };
    TestRun_RunSuite(run, "TerrainGltf", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
