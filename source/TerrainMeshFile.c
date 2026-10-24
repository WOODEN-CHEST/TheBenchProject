#include <inttypes.h>
#include "TerrainMeshInternal.h"
#include "GameGHDF.h"
#include "wr/WRFileSystem.h"
#include "wr/WRGHDF.h"
#include "wr/WRMemory.h"
#include "wr/WRMemoryStream.h"
#include "wr/WRPath.h"

/*
 * A terrain mesh's binary form (include/TerrainMesh.h): one GHDF document, the root compound holding the counts and one
 * array per kind of data. Written by the editor (later) and by round-trip tests; read as hostile data (a pack's file).
 *
 *   1 format version (1)               7 positions: Float x 3 per vertex (on the position grid)
 *   2 vertex count                     8 normals: Float x 3 per vertex, only with imported normals (smooth ones are
 *   3 triangle count                     computed again when it is read, bit for bit as before)
 *   4 the palette's size               9 weights: UInt8 x the palette's size per vertex
 *   5 density channel count           10 densities: UInt8 x the channel count per vertex, only with channels
 *   6 normal mode (0 smooth,          11 indices: UInt32 x 3 per triangle
 *     1 imported)                     12 triangle flags: UInt8 per triangle
 *                                     13 face materials: UInt8 per triangle
 *
 * Integers are written in the smallest type that holds them and read from any integer type (GameGHDF.h); arrays must
 * have exactly their type and length. The active edges and chunks are computed again when it is read.
 */


// Macros.
/** The format this file writes, and the newest it reads. */
#define TERRAIN_FILE_FORMAT_VERSION 1U
/** What messages call the document. */
#define DOCUMENT_NAME "the terrain file"
/** The suffix of the file an atomic save writes first. */
#define TEMPORARY_SUFFIX ((const unsigned char*)u8".tmp")


// Types.
/* The root's entries. Saved: never renumber. */
typedef enum TerrainFileEntryEnum
{
    TerrainFileEntry_FormatVersion = 1,
    TerrainFileEntry_VertexCount = 2,
    TerrainFileEntry_TriangleCount = 3,
    TerrainFileEntry_MaterialCount = 4,
    TerrainFileEntry_DensityChannelCount = 5,
    TerrainFileEntry_NormalMode = 6,
    TerrainFileEntry_Positions = 7,
    TerrainFileEntry_Normals = 8,
    TerrainFileEntry_Weights = 9,
    TerrainFileEntry_Densities = 10,
    TerrainFileEntry_Indices = 11,
    TerrainFileEntry_TriangleFlags = 12,
    TerrainFileEntry_TriangleMaterials = 13
} TerrainFileEntry;


// Static functions: writing.
/* Hands a filled array to the root under @p id (on failure the pool reclaims it with the root). */
static Error PutArray(GHDFCompound* root, GHDFEntryID id, GHDFArray* array, GHDFValueType type)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateArray(array, type);
    return GHDFCompound_SetValue(root, id, GHDF_CreateArrayType(type), &Value);
}

static Error PutFloats(GHDFObjectPool* pool, GHDFCompound* root, GHDFEntryID id, const float* values, size_t count)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_Float, &Array);
    for (size_t Index = 0U; (Index < count) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Value = GHDFObjectValue_CreateFloat(values[Index]);
        Result = GHDFArray_AddValue(Array, &Value);
    }
    return (Result.Code == ErrorCode_Success) ? PutArray(root, id, Array, GHDFValueType_Float) : Result;
}

static Error PutUInt32s(GHDFObjectPool* pool, GHDFCompound* root, GHDFEntryID id, const uint32_t* values, size_t count)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_UInt32, &Array);
    for (size_t Index = 0U; (Index < count) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Value = GHDFObjectValue_CreateUInt32(values[Index]);
        Result = GHDFArray_AddValue(Array, &Value);
    }
    return (Result.Code == ErrorCode_Success) ? PutArray(root, id, Array, GHDFValueType_UInt32) : Result;
}

/* @p used bytes of every @p stride-byte record of @p count records. */
static Error PutBytes(GHDFObjectPool* pool, GHDFCompound* root, GHDFEntryID id, const uint8_t* values, size_t count,
    size_t stride, size_t used)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_UInt8, &Array);
    for (size_t Index = 0U; (Index < count) && (Result.Code == ErrorCode_Success); Index++)
    {
        for (size_t Entry = 0U; (Entry < used) && (Result.Code == ErrorCode_Success); Entry++)
        {
            GHDFObjectValue Value = GHDFObjectValue_CreateUInt8(values[(Index * stride) + Entry]);
            Result = GHDFArray_AddValue(Array, &Value);
        }
    }
    return (Result.Code == ErrorCode_Success) ? PutArray(root, id, Array, GHDFValueType_UInt8) : Result;
}

static Error WriteRoot(const TerrainMesh* self, GHDFObjectPool* pool, GHDFCompound* root)
{
    size_t Vertices = self->_vertexCount;
    size_t Triangles = self->_triangleCount;
    Error Result = GameGHDF_PutUnsigned(root, TerrainFileEntry_FormatVersion, TERRAIN_FILE_FORMAT_VERSION);
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameGHDF_PutUnsigned(root, TerrainFileEntry_VertexCount, self->_vertexCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameGHDF_PutUnsigned(root, TerrainFileEntry_TriangleCount, self->_triangleCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameGHDF_PutUnsigned(root, TerrainFileEntry_MaterialCount, self->_materialCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameGHDF_PutUnsigned(root, TerrainFileEntry_DensityChannelCount, self->_densityChannelCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameGHDF_PutUnsigned(root, TerrainFileEntry_NormalMode, (uint64_t)self->_normalMode);
    }
    // A Vector3 is three packed floats.
    if (Result.Code == ErrorCode_Success)
    {
        Result = PutFloats(pool, root, TerrainFileEntry_Positions, (const float*)(const void*)self->_positions,
            Vertices * 3U);
    }
    if ((Result.Code == ErrorCode_Success) && (self->_normalMode == TerrainNormalMode_Imported))
    {
        Result = PutFloats(pool, root, TerrainFileEntry_Normals, (const float*)(const void*)self->_normals,
            Vertices * 3U);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PutBytes(pool, root, TerrainFileEntry_Weights, self->_weights, Vertices, TERRAIN_MAX_MATERIALS,
            self->_materialCount);
    }
    if ((Result.Code == ErrorCode_Success) && (self->_densityChannelCount > 0U))
    {
        Result = PutBytes(pool, root, TerrainFileEntry_Densities, self->_densities, Vertices,
            TERRAIN_MAX_DENSITY_CHANNELS, self->_densityChannelCount);
    }
    // The triangles' indices, flags and face materials, each as one array.
    uint32_t* Indices = Memory_Allocate(Triangles * 3U * sizeof(uint32_t));
    uint8_t* Flags = Memory_Allocate(Triangles * 2U);
    for (size_t Triangle = 0U; Triangle < Triangles; Triangle++)
    {
        for (size_t Corner = 0U; Corner < 3U; Corner++)
        {
            Indices[(Triangle * 3U) + Corner] = self->_triangles[Triangle]._indices[Corner];
        }
        Flags[Triangle] = self->_triangles[Triangle]._flags;
        Flags[Triangles + Triangle] = self->_triangles[Triangle]._material;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PutUInt32s(pool, root, TerrainFileEntry_Indices, Indices, Triangles * 3U);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PutBytes(pool, root, TerrainFileEntry_TriangleFlags, Flags, Triangles, 1U, 1U);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PutBytes(pool, root, TerrainFileEntry_TriangleMaterials, &Flags[Triangles], Triangles, 1U, 1U);
    }
    Memory_Free(Indices);
    Memory_Free(Flags);
    return Result;
}

/* Writes @p bytes to "<path>.tmp" next to the target, then replaces the target with it; missing folders are made. */
static Error SaveBytesAtomically(const unsigned char* path, const GenericBuffer* bytes)
{
    GenericBuffer Parent;
    GenericBuffer_AllocateVariable(&Parent, 0U, sizeof(unsigned char));
    Error Result = Path_GetParentPath(path, &Parent);
    if ((Result.Code == ErrorCode_Success) && (Parent._count > 0U) && (((const unsigned char*)Parent._data)[0] != 0U))
    {
        Result = FileSystem_CreateAllDirectories(Parent._data);
    }
    Memory_Free(Parent._data);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GenericBuffer Temporary;
    GenericBuffer_AllocateVariable(&Temporary, 0U, sizeof(unsigned char));
    if (!GenericBuffer_AppendString(&Temporary, path) || !GenericBuffer_AppendString(&Temporary, TEMPORARY_SUFFIX)
        || !GenericBuffer_NullTerminate(&Temporary))
    {
        Memory_Free(Temporary._data);
        return Error_Construct2(ErrorCode_BufferTooSmall, "Terrain file: could not build the temporary path.");
    }
    Result = FileSystem_WriteAllBytes(Temporary._data, bytes->_data, bytes->_count);
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_ReplaceEntry(Temporary._data, path);
    }
    if (Result.Code != ErrorCode_Success)
    {
        // Best effort: the target is unchanged either way.
        Error DeleteResult = FileSystem_DeleteEntry(Temporary._data);
        Error_Deconstruct(&DeleteResult);
    }
    Memory_Free(Temporary._data);
    return Result;
}


// Static functions: reading.
/* Parses the bytes into a tree from @p pool within the size limits; the document must use every byte. */
static Error ParseDocument(const unsigned char* bytes, size_t byteCount, GHDFObjectPool* pool, GHDFCompound** outRoot)
{
    // A read-only view: the reader never writes through it, but GenericBuffer's storage pointer is not const.
    GenericBuffer Source;
    GenericBuffer_CreateReadOnly(&Source, (void*)bytes, byteCount, sizeof(unsigned char), byteCount);
    MemoryStream Stream;
    Error Result = MemoryStream_Construct2(&Stream, &Source, IOStreamFlags_CanRead);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFReadLimits Limits = { .MaxTreeByteCount = TERRAIN_FILE_MAX_TREE_BYTES };
    Result = GHDF_ReadWithLimits(MemoryStream_AsIOStream(&Stream), pool, &Limits, outRoot);
    if ((Result.Code != ErrorCode_Success) && (Result.Code != ErrorCode_Deserialize))
    {
        // Data that ends early comes back as a stream error: to callers it is a document that does not read.
        Error Recoded = Error_Construct4(ErrorCode_Deserialize, "%s",
            (Result.Message != NULL) ? (const char*)Result.Message : "The terrain file is not a GHDF document.");
        Error_Deconstruct(&Result);
        Result = Recoded;
    }
    size_t Position = 0U;
    if (Result.Code == ErrorCode_Success)
    {
        Result = IOStream_GetPosition(MemoryStream_AsIOStream(&Stream), &Position);
    }
    if ((Result.Code == ErrorCode_Success) && (Position != byteCount))
    {
        Result = Error_Construct4(ErrorCode_Deserialize, "it has %" PRIu64 " bytes after its document.",
            (uint64_t)(byteCount - Position));
    }
    MemoryStream_Deconstruct(&Stream);
    return Result;
}

/* A required unsigned entry in [minimum, maximum]. */
static Error GetCount(GHDFCompound* root, GHDFEntryID id, uint64_t minimum, uint64_t maximum, const char* what,
    const unsigned char* description, uint64_t* outValue)
{
    bool IsFound = false;
    Error Result = GameGHDF_GetOptionalUnsigned(root, id, maximum, outValue, &IsFound);
    if ((Result.Code == ErrorCode_Success) && IsFound && (*outValue >= minimum))
    {
        return Result;
    }
    Error_Deconstruct(&Result);
    return TerrainMeshInternal_Fail(description, ErrorCode_Deserialize, "%s (entry %" PRIu64 ") must be there, an "
        "integer from %" PRIu64 " to %" PRIu64 ".", what, (uint64_t)id, minimum, maximum);
}

/* A required array of @p type with exactly @p count elements, copied into @p outValues (cleared first by the caller; its
 * element size is the type's). */
static Error GetArray(GHDFCompound* root, GHDFEntryID id, GHDFValueType type, size_t count, const char* what,
    const unsigned char* description, GenericBuffer* outValues)
{
    GHDFObjectValue Value;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(root, id, GHDF_CreateArrayType(type), &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        Error_Deconstruct(&Result);
        return TerrainMeshInternal_Fail(description, ErrorCode_Deserialize, "%s (entry %" PRIu64 ") must be there, an "
            "array of the right type.", what, (uint64_t)id);
    }
    size_t Elements = GHDFArray_GetElementCount(Value.Value.Array);
    if (Elements != count)
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_Deserialize, "%s (entry %" PRIu64 ") holds %" PRIu64
            " values; %" PRIu64 " were expected.", what, (uint64_t)id, (uint64_t)Elements, (uint64_t)count);
    }
    Result = GHDFArray_CopyRawBytes(Value.Value.Array, 0U, count, outValues);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return TerrainMeshInternal_Fail(description, ErrorCode_Deserialize, "%s (entry %" PRIu64 ") cannot be read.",
            what, (uint64_t)id);
    }
    return Result;
}

/* Spreads @p used bytes per record into @p stride-byte records (the rest 0). */
static void SpreadBytes(const uint8_t* packed, size_t count, size_t used, size_t stride, uint8_t* outRecords)
{
    Memory_Zero(outRecords, count * stride);
    for (size_t Index = 0U; Index < count; Index++)
    {
        for (size_t Entry = 0U; Entry < used; Entry++)
        {
            outRecords[(Index * stride) + Entry] = packed[(Index * used) + Entry];
        }
    }
}

/* The arrays a mesh is built from, read from the root. */
typedef struct ReadArraysStruct
{
    GenericBuffer Positions;
    GenericBuffer Normals;
    GenericBuffer Weights;
    GenericBuffer Densities;
    GenericBuffer Indices;
    GenericBuffer Flags;
    GenericBuffer Materials;
} ReadArrays;

static void ConstructArrays(ReadArrays* arrays)
{
    GenericBuffer_AllocateVariable(&arrays->Positions, 0U, sizeof(float));
    GenericBuffer_AllocateVariable(&arrays->Normals, 0U, sizeof(float));
    GenericBuffer_AllocateVariable(&arrays->Weights, 0U, sizeof(uint8_t));
    GenericBuffer_AllocateVariable(&arrays->Densities, 0U, sizeof(uint8_t));
    GenericBuffer_AllocateVariable(&arrays->Indices, 0U, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&arrays->Flags, 0U, sizeof(uint8_t));
    GenericBuffer_AllocateVariable(&arrays->Materials, 0U, sizeof(uint8_t));
}

static void DeconstructArrays(ReadArrays* arrays)
{
    Memory_Free(arrays->Positions._data);
    Memory_Free(arrays->Normals._data);
    Memory_Free(arrays->Weights._data);
    Memory_Free(arrays->Densities._data);
    Memory_Free(arrays->Indices._data);
    Memory_Free(arrays->Flags._data);
    Memory_Free(arrays->Materials._data);
}

static Error ReadMesh(GHDFCompound* root, float chunkSize, const unsigned char* description, TerrainMesh** outMesh)
{
    uint64_t Version = 0U;
    uint64_t Vertices = 0U;
    uint64_t Triangles = 0U;
    uint64_t Materials = 0U;
    uint64_t Channels = 0U;
    uint64_t Mode = 0U;
    Error Result = GetCount(root, TerrainFileEntry_FormatVersion, 1U, TERRAIN_FILE_FORMAT_VERSION,
        "The format version (one this game reads)", description, &Version);
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetCount(root, TerrainFileEntry_VertexCount, 1U, TERRAIN_MESH_MAX_VERTICES, "The vertex count",
            description, &Vertices);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetCount(root, TerrainFileEntry_TriangleCount, 1U, TERRAIN_MESH_MAX_TRIANGLES, "The triangle count",
            description, &Triangles);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetCount(root, TerrainFileEntry_MaterialCount, 1U, TERRAIN_MAX_MATERIALS, "The palette's size",
            description, &Materials);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetCount(root, TerrainFileEntry_DensityChannelCount, 0U, TERRAIN_MAX_DENSITY_CHANNELS,
            "The density channel count", description, &Channels);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetCount(root, TerrainFileEntry_NormalMode, 0U, (uint64_t)TerrainNormalMode_Imported,
            "The normal mode (0 smooth, 1 imported)", description, &Mode);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // Every count is bounded now, so no product below overflows.
    ReadArrays Arrays;
    ConstructArrays(&Arrays);
    bool IsImported = (Mode == (uint64_t)TerrainNormalMode_Imported);
    Result = GetArray(root, TerrainFileEntry_Positions, GHDFValueType_Float, (size_t)Vertices * 3U, "The positions",
        description, &Arrays.Positions);
    if ((Result.Code == ErrorCode_Success) && IsImported)
    {
        Result = GetArray(root, TerrainFileEntry_Normals, GHDFValueType_Float, (size_t)Vertices * 3U, "The normals",
            description, &Arrays.Normals);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetArray(root, TerrainFileEntry_Weights, GHDFValueType_UInt8, (size_t)(Vertices * Materials),
            "The weights", description, &Arrays.Weights);
    }
    if ((Result.Code == ErrorCode_Success) && (Channels > 0U))
    {
        Result = GetArray(root, TerrainFileEntry_Densities, GHDFValueType_UInt8, (size_t)(Vertices * Channels),
            "The densities", description, &Arrays.Densities);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetArray(root, TerrainFileEntry_Indices, GHDFValueType_UInt32, (size_t)Triangles * 3U, "The indices",
            description, &Arrays.Indices);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetArray(root, TerrainFileEntry_TriangleFlags, GHDFValueType_UInt8, (size_t)Triangles,
            "The triangle flags", description, &Arrays.Flags);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetArray(root, TerrainFileEntry_TriangleMaterials, GHDFValueType_UInt8, (size_t)Triangles,
            "The face materials", description, &Arrays.Materials);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DeconstructArrays(&Arrays);
        return Result;
    }

    uint8_t* Weights = Memory_Allocate((size_t)Vertices * TERRAIN_MAX_MATERIALS);
    uint8_t* Densities = Memory_Allocate((size_t)Vertices * TERRAIN_MAX_DENSITY_CHANNELS);
    SpreadBytes(Arrays.Weights._data, (size_t)Vertices, (size_t)Materials, TERRAIN_MAX_MATERIALS, Weights);
    SpreadBytes(Arrays.Densities._data, (size_t)Vertices, (size_t)Channels, TERRAIN_MAX_DENSITY_CHANNELS, Densities);
    TerrainMeshData Data =
    {
        .Positions = (const Vector3*)Arrays.Positions._data,
        .Normals = IsImported ? (const Vector3*)Arrays.Normals._data : NULL,
        .Weights = Weights,
        .Densities = Densities,
        .VertexCount = (uint32_t)Vertices,
        .Indices = (const uint32_t*)Arrays.Indices._data,
        .TriangleFlags = (const uint8_t*)Arrays.Flags._data,
        .TriangleMaterials = (const uint8_t*)Arrays.Materials._data,
        .TriangleCount = (uint32_t)Triangles,
        .MaterialCount = (uint32_t)Materials,
        .DensityChannelCount = (uint32_t)Channels,
        .NormalMode = IsImported ? TerrainNormalMode_Imported : TerrainNormalMode_Smooth,
        .ChunkSize = chunkSize
    };
    Result = TerrainMesh_Create(&Data, description, outMesh);
    Memory_Free(Weights);
    Memory_Free(Densities);
    DeconstructArrays(&Arrays);
    return Result;
}


// Public functions.
Error TerrainMesh_WriteGHDF(const TerrainMesh* self, GenericBuffer* outBytes)
{
    if ((self == NULL) || (outBytes == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TerrainMesh_WriteGHDF: self and outBytes must not be NULL.");
    }
    GHDFObjectPool* Pool = NULL;
    Error Result = GHDFObjectPool_Create(&Pool);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFCompound* Root = NULL;
    Result = GHDFObjectPool_BorrowCompound(Pool, &Root);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WriteRoot(self, Pool, Root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        // Written into its own stream first, so nothing is appended on failure.
        MemoryStream Stream;
        Result = MemoryStream_Construct1(&Stream, IOStreamFlags_CanWrite);
        if (Result.Code == ErrorCode_Success)
        {
            Result = GHDF_Write(Root, MemoryStream_AsIOStream(&Stream));
            GenericBuffer* Written = MemoryStream_GetBuffer(&Stream);
            if ((Result.Code == ErrorCode_Success)
                && !GenericBuffer_AppendRangeBytes(outBytes, (unsigned char*)Written->_data, Written->_count))
            {
                Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not hold the terrain file.");
            }
            MemoryStream_Deconstruct(&Stream);
        }
    }
    // The pool's teardown frees the tree with it.
    Error PoolResult = GHDFObjectPool_Deconstruct(Pool);
    if (Result.Code == ErrorCode_Success)
    {
        Result = PoolResult;
    }
    else
    {
        Error_Deconstruct(&PoolResult);
    }
    return Result;
}

Error TerrainMesh_ReadGHDF(const unsigned char* bytes, size_t byteCount, float chunkSize,
    const unsigned char* description, TerrainMesh** outMesh)
{
    if (((bytes == NULL) && (byteCount > 0U)) || (outMesh == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TerrainMesh_ReadGHDF: the bytes and outMesh must not be NULL.");
    }
    *outMesh = NULL;
    if (byteCount > TERRAIN_FILE_MAX_BYTES)
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_BufferTooLarge, DOCUMENT_NAME " has %" PRIu64 " bytes; "
            "at most %" PRIu64 " are read.", (uint64_t)byteCount, (uint64_t)TERRAIN_FILE_MAX_BYTES);
    }
    GHDFObjectPool* Pool = NULL;
    Error Result = GHDFObjectPool_Create(&Pool);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFCompound* Root = NULL;
    Result = ParseDocument(bytes, byteCount, Pool, &Root);
    if (Result.Code != ErrorCode_Success)
    {
        Error Wrapped = TerrainMeshInternal_Fail(description, ErrorCode_Deserialize, DOCUMENT_NAME " does not read: %s",
            (Result.Message != NULL) ? (const char*)Result.Message : "it is not a GHDF document.");
        Error_Deconstruct(&Result);
        Result = Wrapped;
    }
    else
    {
        Result = ReadMesh(Root, chunkSize, description, outMesh);
    }
    Error PoolResult = GHDFObjectPool_Deconstruct(Pool);
    Error_Deconstruct(&PoolResult);
    return Result;
}

Error TerrainMesh_SaveGHDF(const TerrainMesh* self, const unsigned char* path)
{
    if ((self == NULL) || (path == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TerrainMesh_SaveGHDF: self and path must not be NULL.");
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    Error Result = TerrainMesh_WriteGHDF(self, &Bytes);
    if (Result.Code == ErrorCode_Success)
    {
        Result = SaveBytesAtomically(path, &Bytes);
    }
    Memory_Free(Bytes._data);
    return Result;
}

Error TerrainMesh_LoadGHDF(const unsigned char* path, float chunkSize, const unsigned char* description,
    TerrainMesh** outMesh)
{
    if ((path == NULL) || (outMesh == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TerrainMesh_LoadGHDF: path and outMesh must not be NULL.");
    }
    *outMesh = NULL;
    FileSystemEntryInfo Info;
    Memory_Zero(&Info, sizeof(Info));
    Error Result = FileSystem_GetEntryInfo(path, &Info);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsFile = Info._entryType == FileSystemEntryType_File;
    size_t Size = Info._sizeInBytes;
    FileSystemEntryInfo_Deconstruct(&Info);
    if (!IsFile)
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_IO, "\"%s\" is not a file.", (const char*)path);
    }
    if (Size > TERRAIN_FILE_MAX_BYTES)
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_BufferTooLarge, DOCUMENT_NAME " has %" PRIu64 " bytes; "
            "at most %" PRIu64 " are read.", (uint64_t)Size, (uint64_t)TERRAIN_FILE_MAX_BYTES);
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    Result = FileSystem_ReadAllBytes(path, &Bytes);
    if (Result.Code == ErrorCode_Success)
    {
        Result = TerrainMesh_ReadGHDF(Bytes._data, Bytes._count, chunkSize, description, outMesh);
    }
    Memory_Free(Bytes._data);
    return Result;
}
