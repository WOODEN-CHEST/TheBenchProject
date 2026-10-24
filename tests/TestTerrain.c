#include "TestTerrain.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


// Macros.
#define GLTF_FLOAT 5126U
#define GLTF_UNSIGNED_BYTE 5121U
#define GLTF_UNSIGNED_SHORT 5123U
#define GLTF_UNSIGNED_INT 5125U
#define GLB_MAGIC 0x46546C67U
#define GLB_CHUNK_JSON 0x4E4F534AU
#define GLB_CHUNK_BIN 0x004E4942U
#define JSON_TEXT_CAPACITY 512U


// Static functions: helpers.
static void AppendText(GenericBuffer* text, const char* format, ...)
{
    char Piece[JSON_TEXT_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Piece, sizeof(Piece), format, Arguments);
    va_end(Arguments);
    if (Length > 0)
    {
        GenericBuffer_AppendRangeBytes(text, (unsigned char*)Piece, (size_t)Length);
    }
}

static void AppendBytes(GenericBuffer* bytes, const void* data, size_t count)
{
    GenericBuffer_AppendRangeBytes(bytes, (unsigned char*)(void*)data, count);
}

static void PadTo4(GenericBuffer* bytes, unsigned char fill)
{
    while ((bytes->_count % 4U) != 0U)
    {
        GenericBuffer_AppendByte(bytes, fill);
    }
}

static void AppendUInt32(GenericBuffer* bytes, uint32_t value)
{
    unsigned char Little[4] = { (unsigned char)(value & 0xFFU), (unsigned char)((value >> 8U) & 0xFFU),
        (unsigned char)((value >> 16U) & 0xFFU), (unsigned char)((value >> 24U) & 0xFFU) };
    AppendBytes(bytes, Little, sizeof(Little));
}

static bool AreFloatsSame(float a, float b)
{
    return memcmp(&a, &b, sizeof(float)) == 0;
}

static bool AreVectorsSame(Vector3 a, Vector3 b)
{
    return AreFloatsSame(a.x, b.x) && AreFloatsSame(a.y, b.y) && AreFloatsSame(a.z, b.z);
}


// Functions: grids.
void TestTerrain_BuildGrid(TestTerrainGrid* outGrid, uint32_t cellsX, uint32_t cellsZ, float spacing, float originX,
    float originZ, TestTerrainHeightFunction height)
{
    TestTerrainGrid Grid = { .CellsX = cellsX, .CellsZ = cellsZ, .Spacing = spacing, .OriginX = originX,
        .OriginZ = originZ };
    Grid.VertexCount = (cellsX + 1U) * (cellsZ + 1U);
    Grid.TriangleCount = 2U * cellsX * cellsZ;
    Grid.Positions = Memory_Allocate((size_t)Grid.VertexCount * sizeof(Vector3));
    Grid.Normals = Memory_Allocate((size_t)Grid.VertexCount * sizeof(Vector3));
    Grid.Weights = Memory_Allocate((size_t)Grid.VertexCount * TERRAIN_MAX_MATERIALS);
    Grid.Densities = Memory_Allocate((size_t)Grid.VertexCount * TERRAIN_MAX_DENSITY_CHANNELS);
    Grid.Indices = Memory_Allocate((size_t)Grid.TriangleCount * 3U * sizeof(uint32_t));
    Grid.Flags = Memory_Allocate(Grid.TriangleCount);
    Grid.Materials = Memory_Allocate(Grid.TriangleCount);
    Memory_Zero(Grid.Weights, (size_t)Grid.VertexCount * TERRAIN_MAX_MATERIALS);
    Memory_Zero(Grid.Densities, (size_t)Grid.VertexCount * TERRAIN_MAX_DENSITY_CHANNELS);
    Memory_Zero(Grid.Flags, Grid.TriangleCount);
    Memory_Zero(Grid.Materials, Grid.TriangleCount);
    for (uint32_t J = 0U; J <= cellsZ; J++)
    {
        for (uint32_t I = 0U; I <= cellsX; I++)
        {
            uint32_t Vertex = (J * (cellsX + 1U)) + I;
            float X = originX + ((float)I * spacing);
            float Z = originZ + ((float)J * spacing);
            Grid.Positions[Vertex] = (Vector3){ X, (height != NULL) ? height(X, Z) : 0.0f, Z };
            Grid.Normals[Vertex] = (Vector3){ 0.0f, 1.0f, 0.0f };
            Grid.Weights[(size_t)Vertex * TERRAIN_MAX_MATERIALS] = 255U;
        }
    }
    for (uint32_t J = 0U; J < cellsZ; J++)
    {
        for (uint32_t I = 0U; I < cellsX; I++)
        {
            uint32_t V00 = (J * (cellsX + 1U)) + I;
            uint32_t V10 = V00 + 1U;
            uint32_t V01 = V00 + cellsX + 1U;
            uint32_t V11 = V01 + 1U;
            uint32_t* Corners = &Grid.Indices[(size_t)(2U * ((J * cellsX) + I)) * 3U];
            Corners[0] = V00;
            Corners[1] = V01;
            Corners[2] = V10;
            Corners[3] = V10;
            Corners[4] = V01;
            Corners[5] = V11;
        }
    }
    *outGrid = Grid;
}

void TestTerrain_FreeGrid(TestTerrainGrid* grid)
{
    Memory_Free(grid->Positions);
    Memory_Free(grid->Normals);
    Memory_Free(grid->Weights);
    Memory_Free(grid->Densities);
    Memory_Free(grid->Indices);
    Memory_Free(grid->Flags);
    Memory_Free(grid->Materials);
    Memory_Zero(grid, sizeof(*grid));
}

TerrainMeshData TestTerrain_GetData(const TestTerrainGrid* grid, uint32_t materialCount, uint32_t channelCount,
    float chunkSize)
{
    return (TerrainMeshData)
    {
        .Positions = grid->Positions, .Normals = grid->Normals, .Weights = grid->Weights, .Densities = grid->Densities,
        .VertexCount = grid->VertexCount, .Indices = grid->Indices, .TriangleFlags = grid->Flags,
        .TriangleMaterials = grid->Materials, .TriangleCount = grid->TriangleCount, .MaterialCount = materialCount,
        .DensityChannelCount = channelCount, .NormalMode = TerrainNormalMode_Smooth, .ChunkSize = chunkSize
    };
}

TerrainMesh* TestTerrain_CreateMesh(TestRun* run, const TestTerrainGrid* grid, uint32_t materialCount,
    uint32_t channelCount, float chunkSize)
{
    TerrainMeshData Data = TestTerrain_GetData(grid, materialCount, channelCount, chunkSize);
    TerrainMesh* Terrain = NULL;
    TEST_CHECK_SUCCESS(run, TerrainMesh_Create(&Data, NULL, &Terrain));
    return Terrain;
}

/* Records a failed check naming what differs. */
static bool Differ(TestRun* run, const char* what, uint32_t index)
{
    printf("    meshes differ: %s %u\n", what, (unsigned)index);
    return TEST_CHECK(run, false);
}

static bool AreChunksEqual(TestRun* run, const TerrainChunk* a, const TerrainChunk* b, uint32_t chunk)
{
    if ((a->_cellX != b->_cellX) || (a->_cellZ != b->_cellZ) || (a->_center.X != b->_center.X)
        || (a->_center.Y != b->_center.Y) || (a->_center.Z != b->_center.Z) || (a->_triangleCount != b->_triangleCount)
        || (a->_vertexCount != b->_vertexCount) || (a->_surfaceTriangleCount != b->_surfaceTriangleCount)
        || !AreVectorsSame(a->_boundsMin, b->_boundsMin) || !AreVectorsSame(a->_boundsMax, b->_boundsMax)
        || !AreVectorsSame(a->_sphereCenter, b->_sphereCenter) || !AreFloatsSame(a->_sphereRadius, b->_sphereRadius))
    {
        return Differ(run, "the header of chunk", chunk);
    }
    for (uint32_t Vertex = 0U; Vertex < a->_vertexCount; Vertex++)
    {
        if (!AreVectorsSame(a->_positions[Vertex], b->_positions[Vertex])
            || (a->_vertexSources[Vertex] != b->_vertexSources[Vertex])
            || (a->_vertexMaterials[Vertex] != b->_vertexMaterials[Vertex]))
        {
            return Differ(run, "a surface vertex of chunk", chunk);
        }
    }
    for (uint32_t Triangle = 0U; Triangle < a->_surfaceTriangleCount; Triangle++)
    {
        if ((a->_surfaceTriangles[Triangle] != b->_surfaceTriangles[Triangle])
            || (memcmp(&a->_indices[Triangle * 3U], &b->_indices[Triangle * 3U], 3U * sizeof(uint32_t)) != 0))
        {
            return Differ(run, "a surface triangle of chunk", chunk);
        }
    }
    return true;
}

bool TestTerrain_AreMeshesEqual(TestRun* run, const TerrainMesh* a, const TerrainMesh* b)
{
    if ((a->_vertexCount != b->_vertexCount) || (a->_triangleCount != b->_triangleCount)
        || (a->_materialCount != b->_materialCount) || (a->_densityChannelCount != b->_densityChannelCount)
        || (a->_normalMode != b->_normalMode) || (a->_chunkCount != b->_chunkCount)
        || !AreFloatsSame(a->_chunkSize, b->_chunkSize))
    {
        return Differ(run, "the counts", 0U);
    }
    for (uint32_t Vertex = 0U; Vertex < a->_vertexCount; Vertex++)
    {
        if (!AreVectorsSame(a->_positions[Vertex], b->_positions[Vertex])
            || !AreVectorsSame(a->_normals[Vertex], b->_normals[Vertex])
            || (memcmp(TerrainMesh_GetWeights(a, Vertex), TerrainMesh_GetWeights(b, Vertex), TERRAIN_MAX_MATERIALS) != 0)
            || (memcmp(TerrainMesh_GetDensities(a, Vertex), TerrainMesh_GetDensities(b, Vertex),
                TERRAIN_MAX_DENSITY_CHANNELS) != 0))
        {
            return Differ(run, "vertex", Vertex);
        }
    }
    for (uint32_t Triangle = 0U; Triangle < a->_triangleCount; Triangle++)
    {
        if (memcmp(&a->_triangles[Triangle], &b->_triangles[Triangle], sizeof(TerrainTriangle)) != 0)
        {
            return Differ(run, "triangle", Triangle);
        }
        if (a->_triangleChunks[Triangle] != b->_triangleChunks[Triangle])
        {
            return Differ(run, "the chunk of triangle", Triangle);
        }
    }
    for (uint32_t Chunk = 0U; Chunk < a->_chunkCount; Chunk++)
    {
        if (!AreChunksEqual(run, &a->_chunks[Chunk], &b->_chunks[Chunk], Chunk))
        {
            return false;
        }
    }
    return true;
}


// Functions: glTF files.
/* Appends one accessor's data (4-byte aligned) and its buffer view and accessor JSON. */
static void AddAccessor(GenericBuffer* binary, GenericBuffer* views, GenericBuffer* accessors, uint32_t* ioViewCount,
    const void* data, size_t byteCount, uint32_t componentType, const char* type, uint32_t count, bool isNormalized,
    const char* extra)
{
    PadTo4(binary, 0U);
    size_t Offset = binary->_count;
    AppendBytes(binary, data, byteCount);
    AppendText(views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", (*ioViewCount > 0U) ? "," : "", Offset,
        byteCount);
    AppendText(accessors, "%s{\"bufferView\":%u,\"componentType\":%u,\"type\":\"%s\",\"count\":%u%s%s}",
        (*ioViewCount > 0U) ? "," : "", (unsigned)*ioViewCount, (unsigned)componentType, type, (unsigned)count,
        isNormalized ? ",\"normalized\":true" : "", (extra != NULL) ? extra : "");
    (*ioViewCount)++;
}

static uint32_t AddAttributeAccessor(GenericBuffer* binary, GenericBuffer* views, GenericBuffer* accessors,
    uint32_t* ioViewCount, const TestGltfAttribute* attribute, uint32_t vertexCount)
{
    size_t ValueCount = (size_t)vertexCount * attribute->ChannelCount;
    size_t Size = (attribute->ComponentType == GLTF_UNSIGNED_BYTE) ? 1U
        : ((attribute->ComponentType == GLTF_UNSIGNED_SHORT) ? 2U : 4U);
    unsigned char* Data = Memory_Allocate(ValueCount * Size);
    for (size_t Index = 0U; Index < ValueCount; Index++)
    {
        float Value = attribute->Values[Index];
        if (attribute->ComponentType == GLTF_UNSIGNED_BYTE)
        {
            Data[Index] = (unsigned char)lroundf(fminf(fmaxf(Value, 0.0f), 1.0f) * 255.0f);
        }
        else if (attribute->ComponentType == GLTF_UNSIGNED_SHORT)
        {
            uint16_t Short = (uint16_t)lroundf(fminf(fmaxf(Value, 0.0f), 1.0f) * 65535.0f);
            memcpy(&Data[Index * 2U], &Short, sizeof(Short));
        }
        else
        {
            memcpy(&Data[Index * 4U], &Value, sizeof(Value));
        }
    }
    // Each vertex's element is padded to 4 bytes, as glTF requires of vertex attributes: one view per accessor with
    // the stride of a padded element.
    size_t ElementSize = Size * attribute->ChannelCount;
    size_t PaddedSize = (ElementSize + 3U) & ~(size_t)3U;
    unsigned char* Padded = Memory_Allocate((size_t)vertexCount * PaddedSize);
    Memory_Zero(Padded, (size_t)vertexCount * PaddedSize);
    for (size_t Vertex = 0U; Vertex < vertexCount; Vertex++)
    {
        memcpy(&Padded[Vertex * PaddedSize], &Data[Vertex * ElementSize], ElementSize);
    }
    const char* Type = (attribute->ChannelCount == 1U) ? "SCALAR" : ((attribute->ChannelCount == 3U) ? "VEC3" : "VEC4");
    uint32_t Index = *ioViewCount;
    PadTo4(binary, 0U);
    size_t Offset = binary->_count;
    AppendBytes(binary, Padded, (size_t)vertexCount * PaddedSize);
    if (PaddedSize != ElementSize)
    {
        AppendText(views, ",{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"byteStride\":%zu}", Offset,
            (size_t)vertexCount * PaddedSize, PaddedSize);
    }
    else
    {
        AppendText(views, ",{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", Offset,
            (size_t)vertexCount * PaddedSize);
    }
    AppendText(accessors, ",{\"bufferView\":%u,\"componentType\":%u,\"type\":\"%s\",\"count\":%u%s}", (unsigned)Index,
        (unsigned)attribute->ComponentType, Type, (unsigned)vertexCount,
        (attribute->ComponentType != GLTF_FLOAT) ? ",\"normalized\":true" : "");
    (*ioViewCount)++;
    Memory_Free(Data);
    Memory_Free(Padded);
    return Index;
}

void TestTerrain_WriteGlb(const TestGltfMesh* mesh, GenericBuffer* outBytes)
{
    GenericBuffer Binary;
    GenericBuffer Views;
    GenericBuffer Accessors;
    GenericBuffer Json;
    GenericBuffer_AllocateVariable(&Binary, 256U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&Views, 256U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&Accessors, 256U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&Json, 1024U, sizeof(unsigned char));
    uint32_t ViewCount = 0U;

    Vector3 Min = mesh->Positions[0];
    Vector3 Max = mesh->Positions[0];
    for (uint32_t Vertex = 1U; Vertex < mesh->VertexCount; Vertex++)
    {
        Vector3 P = mesh->Positions[Vertex];
        Min = (Vector3){ fminf(Min.x, P.x), fminf(Min.y, P.y), fminf(Min.z, P.z) };
        Max = (Vector3){ fmaxf(Max.x, P.x), fmaxf(Max.y, P.y), fmaxf(Max.z, P.z) };
    }
    char Bounds[JSON_TEXT_CAPACITY];
    snprintf(Bounds, sizeof(Bounds), ",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]", (double)Min.x, (double)Min.y,
        (double)Min.z, (double)Max.x, (double)Max.y, (double)Max.z);
    AddAccessor(&Binary, &Views, &Accessors, &ViewCount, mesh->Positions, (size_t)mesh->VertexCount * sizeof(Vector3),
        GLTF_FLOAT, "VEC3", mesh->VertexCount, false, Bounds);
    uint32_t NormalAccessor = UINT32_MAX;
    if (mesh->HasNormals)
    {
        NormalAccessor = ViewCount;
        AddAccessor(&Binary, &Views, &Accessors, &ViewCount, mesh->Normals, (size_t)mesh->VertexCount * sizeof(Vector3),
            GLTF_FLOAT, "VEC3", mesh->VertexCount, false, NULL);
    }
    uint32_t AttributeAccessors[16];
    for (size_t Attribute = 0U; Attribute < mesh->AttributeCount; Attribute++)
    {
        AttributeAccessors[Attribute] = AddAttributeAccessor(&Binary, &Views, &Accessors, &ViewCount,
            &mesh->Attributes[Attribute], mesh->VertexCount);
    }
    bool AreShortIndices = mesh->VertexCount <= 65535U;
    uint32_t IndexAccessors[16];
    uint32_t FirstTriangle = 0U;
    for (size_t Primitive = 0U; Primitive < mesh->PrimitiveCount; Primitive++)
    {
        uint32_t Count = mesh->PrimitiveTriangleCounts[Primitive] * 3U;
        const uint32_t* Source = &mesh->Indices[(size_t)FirstTriangle * 3U];
        IndexAccessors[Primitive] = ViewCount;
        if (AreShortIndices)
        {
            uint16_t* Shorts = Memory_Allocate(((size_t)Count + 1U) * sizeof(uint16_t));
            for (uint32_t Index = 0U; Index < Count; Index++)
            {
                Shorts[Index] = (uint16_t)Source[Index];
            }
            AddAccessor(&Binary, &Views, &Accessors, &ViewCount, Shorts, (size_t)Count * sizeof(uint16_t),
                GLTF_UNSIGNED_SHORT, "SCALAR", Count, false, NULL);
            Memory_Free(Shorts);
        }
        else
        {
            AddAccessor(&Binary, &Views, &Accessors, &ViewCount, Source, (size_t)Count * sizeof(uint32_t),
                GLTF_UNSIGNED_INT, "SCALAR", Count, false, NULL);
        }
        FirstTriangle += mesh->PrimitiveTriangleCounts[Primitive];
    }
    PadTo4(&Binary, 0U);

    AppendText(&Json, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],");
    AppendText(&Json, "\"nodes\":[");
    if (mesh->HasParent)
    {
        AppendText(&Json, "{\"children\":[1],\"translation\":[%.9g,%.9g,%.9g],\"rotation\":[%.9g,%.9g,%.9g,%.9g]},",
            (double)mesh->ParentTranslation.x, (double)mesh->ParentTranslation.y, (double)mesh->ParentTranslation.z,
            (double)mesh->ParentRotation[0], (double)mesh->ParentRotation[1], (double)mesh->ParentRotation[2],
            (double)mesh->ParentRotation[3]);
    }
    AppendText(&Json, "{\"mesh\":0,\"translation\":[%.9g,%.9g,%.9g],\"scale\":[%.9g,%.9g,%.9g]}],",
        (double)mesh->NodeTranslation.x, (double)mesh->NodeTranslation.y, (double)mesh->NodeTranslation.z,
        (double)mesh->NodeScale, (double)mesh->NodeScale, (double)mesh->NodeScale);
    AppendText(&Json, "\"materials\":[");
    for (size_t Primitive = 0U; Primitive < mesh->PrimitiveCount; Primitive++)
    {
        AppendText(&Json, "%s{\"name\":\"%s\"}", (Primitive > 0U) ? "," : "",
            (mesh->MaterialNames[Primitive] != NULL) ? mesh->MaterialNames[Primitive] : "unused");
    }
    AppendText(&Json, "],\"meshes\":[{\"name\":\"terrain\",\"primitives\":[");
    for (size_t Primitive = 0U; Primitive < mesh->PrimitiveCount; Primitive++)
    {
        AppendText(&Json, "%s{\"attributes\":{\"POSITION\":0", (Primitive > 0U) ? "," : "");
        if (mesh->HasNormals)
        {
            AppendText(&Json, ",\"NORMAL\":%u", (unsigned)NormalAccessor);
        }
        for (size_t Attribute = 0U; Attribute < mesh->AttributeCount; Attribute++)
        {
            AppendText(&Json, ",\"%s\":%u", mesh->Attributes[Attribute].Name, (unsigned)AttributeAccessors[Attribute]);
        }
        AppendText(&Json, "},\"indices\":%u,\"mode\":%u", (unsigned)IndexAccessors[Primitive],
            (mesh->PrimitiveModes != NULL) ? (unsigned)mesh->PrimitiveModes[Primitive] : 4U);
        if (mesh->MaterialNames[Primitive] != NULL)
        {
            AppendText(&Json, ",\"material\":%u", (unsigned)Primitive);
        }
        AppendText(&Json, "}");
    }
    AppendText(&Json, "]}],\"bufferViews\":[");
    GenericBuffer_AppendRangeBytes(&Json, (unsigned char*)Views._data, Views._count);
    AppendText(&Json, "],\"accessors\":[");
    GenericBuffer_AppendRangeBytes(&Json, (unsigned char*)Accessors._data, Accessors._count);
    AppendText(&Json, "],\"buffers\":[{\"byteLength\":%zu}]}", Binary._count);
    PadTo4(&Json, (unsigned char)' ');

    uint32_t Total = (uint32_t)(12U + 8U + Json._count + 8U + Binary._count);
    AppendUInt32(outBytes, GLB_MAGIC);
    AppendUInt32(outBytes, 2U);
    AppendUInt32(outBytes, Total);
    AppendUInt32(outBytes, (uint32_t)Json._count);
    AppendUInt32(outBytes, GLB_CHUNK_JSON);
    AppendBytes(outBytes, Json._data, Json._count);
    AppendUInt32(outBytes, (uint32_t)Binary._count);
    AppendUInt32(outBytes, GLB_CHUNK_BIN);
    AppendBytes(outBytes, Binary._data, Binary._count);
    Memory_Free(Binary._data);
    Memory_Free(Views._data);
    Memory_Free(Accessors._data);
    Memory_Free(Json._data);
}

static uint32_t ReadUInt32(const unsigned char* bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

bool TestTerrain_ReplaceInGlbJson(const GenericBuffer* glb, const char* find, const char* replacement,
    GenericBuffer* outBytes)
{
    const unsigned char* Bytes = glb->_data;
    uint32_t JsonLength = ReadUInt32(&Bytes[12]);
    const unsigned char* Json = &Bytes[20];
    const unsigned char* Binary = &Bytes[20U + JsonLength];
    uint32_t BinaryLength = ReadUInt32(Binary);
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, JsonLength + 1U, sizeof(unsigned char));
    GenericBuffer_AppendRangeBytes(&Text, (unsigned char*)(void*)Json, JsonLength);
    GenericBuffer_AppendByte(&Text, 0U);
    const char* Found = strstr((const char*)Text._data, find);
    if (Found == NULL)
    {
        Memory_Free(Text._data);
        return false;
    }
    size_t Before = (size_t)(Found - (const char*)Text._data);
    GenericBuffer NewJson;
    GenericBuffer_AllocateVariable(&NewJson, JsonLength + 64U, sizeof(unsigned char));
    GenericBuffer_AppendRangeBytes(&NewJson, (unsigned char*)Text._data, Before);
    GenericBuffer_AppendRangeBytes(&NewJson, (unsigned char*)(void*)replacement, strlen(replacement));
    size_t After = Before + strlen(find);
    GenericBuffer_AppendRangeBytes(&NewJson, (unsigned char*)Text._data + After, JsonLength - After);
    PadTo4(&NewJson, (unsigned char)' ');
    uint32_t Total = (uint32_t)(12U + 8U + NewJson._count + 8U + BinaryLength);
    AppendUInt32(outBytes, GLB_MAGIC);
    AppendUInt32(outBytes, 2U);
    AppendUInt32(outBytes, Total);
    AppendUInt32(outBytes, (uint32_t)NewJson._count);
    AppendUInt32(outBytes, GLB_CHUNK_JSON);
    AppendBytes(outBytes, NewJson._data, NewJson._count);
    AppendBytes(outBytes, Binary, 8U + BinaryLength);
    Memory_Free(Text._data);
    Memory_Free(NewJson._data);
    return true;
}
