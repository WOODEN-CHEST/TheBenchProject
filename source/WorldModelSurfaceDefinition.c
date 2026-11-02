#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldModelSurfaceInternal.h"
#include "AssetDefinitionCommon.h"
#include "NamespacedKey.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * A world model definition's "surfaces" section (source/WorldModelSurfaceInternal.h):
 *   "surfaces": [ { "name": "screen", "material_slot": 1, "resolution": [640, 480], "kind": "screen", "emission": 0.2 } ]
 * name, material_slot and resolution are required; kind defaults to "screen"; emission is a screen's only.
 */


// Macros.
/** Room for one message's text, a path in the section ("surfaces[3]"), a "where" for unknown keys and a name from the
 *  file. */
#define MESSAGE_CAPACITY ((size_t)384)
#define PATH_CAPACITY ((size_t)48)
#define WHERE_CAPACITY ((size_t)256)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))
/** Corners per triangle, and texture coordinates per vertex. */
#define TRIANGLE_CORNERS 3U
#define TEXCOORD_COMPONENTS 2U


// Types.
typedef struct SurfaceEntryStruct
{
    unsigned char* Name; // owned
    int32_t MaterialSlot;
    int32_t Width;
    int32_t Height;
    WorldModelSurfaceKind Kind;
    float Emission;
} SurfaceEntry;

struct WorldModelSurfaceDefinitionStruct
{
    SurfaceEntry Surfaces[WORLD_MODEL_MAX_SURFACES];
    size_t SurfaceCount;
};

/* The loaded block: the meshes it owns, the surfaces, then their triangles' texture coordinates, then their names. */
typedef struct SurfaceBlockStruct
{
    CollisionMesh* Meshes[WORLD_MODEL_MAX_SURFACES]; // owned; the surfaces point at them
    uint32_t SurfaceCount;
    WorldModelSurface Surfaces[];
} SurfaceBlock;


// Fields.
static const unsigned char* const SurfaceKeys[] =
{
    u8"name", u8"material_slot", u8"resolution", u8"kind", u8"emission"
};


// Static functions: messages.
/* snprintf for paths in messages: a path cut short only shortens a message. */
static void FormatPath(char* buffer, size_t capacity, const char* format, ...)
{
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(buffer, capacity, format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        buffer[0] = '\0';
    }
}

static Error Fail(const char* name, ErrorCode code, const char* format, ...)
{
    char Text[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    return Error_Construct4(code, "World model \"%s\": %s", name, Text);
}

static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}


// Static functions: parsing.
/* "name": a local name of at most WORLD_MODEL_SURFACE_NAME_MAX_LENGTH bytes, unique in the world model. */
static Error ParseName(const char* name, JSONCompound* compound, const char* path,
    WorldModelSurfaceDefinition* definition, size_t index)
{
    SurfaceEntry* Entry = &definition->Surfaces[index];
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"name", &Entry->Name, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.name must be a string.", path);
    }
    if (!Found)
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s needs a \"name\": what screen components call it.", path);
    }
    char Shown[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Entry->Name, Shown, sizeof(Shown));
    if (!NamespacedKey_IsValidLocalName(Entry->Name)
        || (StringUTF8_GetByteLength(Entry->Name) > WORLD_MODEL_SURFACE_NAME_MAX_LENGTH))
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.name \"%s\" is not a valid surface name "
            "([a-z][a-z0-9_.-]*, at most %" PRIu64 " bytes).", path, Shown, (uint64_t)WORLD_MODEL_SURFACE_NAME_MAX_LENGTH);
    }
    for (size_t Earlier = 0U; Earlier < index; Earlier++)
    {
        if (AreNamesEqual(definition->Surfaces[Earlier].Name, Entry->Name))
        {
            return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.name \"%s\" is used by surfaces[%" PRIu64 "] too; "
                "surface names must be unique.", path, Shown, (uint64_t)Earlier);
        }
    }
    return Error_CreateSuccess();
}

/* "material_slot": a whole number from 0 to INT32_MAX that no earlier surface shows its picture on. */
static Error ParseMaterialSlot(const char* name, JSONCompound* compound, const char* path,
    WorldModelSurfaceDefinition* definition, size_t index)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"material_slot", &Value, &Found);
    int64_t Slot = -1;
    if ((Result.Code == ErrorCode_Success) && Found)
    {
        Result = AssetJSON_ReadOptionalInteger(compound, (const unsigned char*)u8"material_slot", -1, &Slot);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        Slot = -1;
    }
    if ((Slot < 0) || (Slot > (int64_t)INT32_MAX))
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s needs a \"material_slot\": the index of the material "
            "that shows the picture, a whole number from 0 to %" PRId32 ".", path, INT32_MAX);
    }
    SurfaceEntry* Entry = &definition->Surfaces[index];
    Entry->MaterialSlot = (int32_t)Slot;
    for (size_t Earlier = 0U; Earlier < index; Earlier++)
    {
        if (definition->Surfaces[Earlier].MaterialSlot == Entry->MaterialSlot)
        {
            return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.material_slot %" PRId32 " is surfaces[%" PRIu64 "]'s "
                "too; a material slot shows one surface.", path, Entry->MaterialSlot, (uint64_t)Earlier);
        }
    }
    return Error_CreateSuccess();
}

/* "resolution": [width, height], each WORLD_MODEL_SURFACE_MIN_RESOLUTION to WORLD_MODEL_SURFACE_MAX_RESOLUTION. */
static Error ParseResolution(const char* name, JSONCompound* compound, const char* path, SurfaceEntry* entry)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"resolution", &Value, &Found);
    int64_t Width = 0;
    int64_t Height = 0;
    if ((Result.Code == ErrorCode_Success) && Found)
    {
        Result = AssetJSON_ReadOptionalVectorInt(compound, (const unsigned char*)u8"resolution", 0, 0, &Width, &Height);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        Found = false;
    }
    bool IsInRange = (Width >= WORLD_MODEL_SURFACE_MIN_RESOLUTION) && (Width <= WORLD_MODEL_SURFACE_MAX_RESOLUTION)
        && (Height >= WORLD_MODEL_SURFACE_MIN_RESOLUTION) && (Height <= WORLD_MODEL_SURFACE_MAX_RESOLUTION);
    if (!Found || !IsInRange)
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s needs a \"resolution\": [width, height] of its picture, "
            "each %d to %d pixels.", path, WORLD_MODEL_SURFACE_MIN_RESOLUTION, WORLD_MODEL_SURFACE_MAX_RESOLUTION);
    }
    entry->Width = (int32_t)Width;
    entry->Height = (int32_t)Height;
    return Error_CreateSuccess();
}

/* "kind": "screen" (the default) or "paper". */
static Error ParseKind(const char* name, JSONCompound* compound, const char* path, SurfaceEntry* entry)
{
    unsigned char* Text = NULL;
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"kind", &Text, &Found);
    entry->Kind = WorldModelSurfaceKind_Screen;
    if ((Result.Code == ErrorCode_Success) && !Found)
    {
        return Result;
    }
    bool IsKnown = false;
    for (int32_t Kind = 0; (Result.Code == ErrorCode_Success) && (Kind < (int32_t)WorldModelSurfaceKind_Count); Kind++)
    {
        if (AreNamesEqual(Text, WorldModelSurfaceKind_GetName((WorldModelSurfaceKind)Kind)))
        {
            entry->Kind = (WorldModelSurfaceKind)Kind;
            IsKnown = true;
        }
    }
    Error_Deconstruct(&Result);
    char Shown[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Text, Shown, sizeof(Shown));
    Memory_Free(Text);
    if (!IsKnown)
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.kind is \"%s\"; it must be \"screen\" (the picture glows) "
            "or \"paper\" (it is lit like the surface it is on).", path, Shown);
    }
    return Error_CreateSuccess();
}

/* "emission": a screen's brightness, 0 to WORLD_MODEL_MAX_SURFACE_EMISSION (a paper gives off no light). */
static Error ParseEmission(const char* name, JSONCompound* compound, const char* path, SurfaceEntry* entry)
{
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalFloat(compound, (const unsigned char*)u8"emission",
        WORLD_MODEL_DEFAULT_SURFACE_EMISSION, &entry->Emission, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.emission must be a number.", path);
    }
    if (entry->Kind == WorldModelSurfaceKind_Paper)
    {
        entry->Emission = 0.0f;
        return Found ? Fail(name, ErrorCode_InvalidAssetDefinition, "%s.emission is a screen's; a paper is lit like the "
            "surface it is on and gives off no light.", path) : Error_CreateSuccess();
    }
    if (!(entry->Emission >= 0.0f) || !(entry->Emission <= WORLD_MODEL_MAX_SURFACE_EMISSION))
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.emission is %g; it must be at least 0 and at most %g (its "
            "picture's white, 1 a white surface in full sun).", path, (double)entry->Emission,
            (double)WORLD_MODEL_MAX_SURFACE_EMISSION);
    }
    return Error_CreateSuccess();
}

/* One surface: its keys checked, then each value. */
static Error ParseSurface(const char* name, JSONArray* surfaces, size_t index, WorldModelSurfaceDefinition* definition)
{
    char Path[PATH_CAPACITY];
    FormatPath(Path, sizeof(Path), "surfaces[%" PRIu64 "]", (uint64_t)index);
    JSONObjectValue Element;
    Error Result = JSONArray_GetVerified(surfaces, index, JSONValueType_Compound, &Element);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s must be a JSON object.", Path);
    }
    JSONCompound* Compound = Element.Value.Compound;
    char KeysWhere[WHERE_CAPACITY];
    FormatPath(KeysWhere, sizeof(KeysWhere), "world model \"%s\", %s", name, Path);
    Result = AssetJSON_RejectUnknownKeys(Compound, SurfaceKeys, KEY_COUNT(SurfaceKeys), KeysWhere);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // Counted before it is read, so the definition's teardown frees what a failed read kept.
    definition->SurfaceCount = index + 1U;
    SurfaceEntry* Entry = &definition->Surfaces[index];
    Result = ParseName(name, Compound, Path, definition, index);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseMaterialSlot(name, Compound, Path, definition, index);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseResolution(name, Compound, Path, Entry);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseKind(name, Compound, Path, Entry);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseEmission(name, Compound, Path, Entry);
    }
    return Result;
}


// Static functions: loading.
/* Releases the meshes built so far. */
static void DestroyMeshes(CollisionMesh** meshes, size_t count)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        CollisionMesh_Destroy(meshes[Index]);
    }
}

/* Builds one surface's triangles from LOD 0's meshes of its slot (the mesh indices into @p outMeshIndices, how many into
 * *outMeshCount): each of them must have finite texture coordinates. */
static Error BuildSurfaceMesh(const SurfaceEntry* entry, const char* name, const Model* lod0, int* outMeshIndices,
    size_t* outMeshCount, CollisionMesh** outMesh)
{
    *outMesh = NULL;
    *outMeshCount = 0U;
    if ((lod0->meshMaterial == NULL) || (lod0->meshes == NULL))
    {
        return Fail(name, ErrorCode_InvalidAssetData, "surface \"%s\": LOD 0's model has no meshes.", (const char*)entry->Name);
    }
    for (int MeshIndex = 0; MeshIndex < lod0->meshCount; MeshIndex++)
    {
        if (lod0->meshMaterial[MeshIndex] != entry->MaterialSlot)
        {
            continue;
        }
        const Mesh* Source = &lod0->meshes[MeshIndex];
        if ((Source->texcoords == NULL) || (Source->vertexCount <= 0))
        {
            return Fail(name, ErrorCode_InvalidAssetData, "surface \"%s\": LOD 0's mesh %d (material slot %" PRId32 ") has "
                "no texture coordinates; they place the picture on it.", (const char*)entry->Name, MeshIndex,
                entry->MaterialSlot);
        }
        for (size_t Component = 0U; Component < ((size_t)Source->vertexCount * TEXCOORD_COMPONENTS); Component++)
        {
            if (!isfinite(Source->texcoords[Component]))
            {
                return Fail(name, ErrorCode_InvalidAssetData, "surface \"%s\": LOD 0's mesh %d has texture coordinates "
                    "that are not finite numbers.", (const char*)entry->Name, MeshIndex);
            }
        }
        outMeshIndices[(*outMeshCount)++] = MeshIndex;
    }
    if (*outMeshCount == 0U)
    {
        return Fail(name, ErrorCode_InvalidAssetData, "surface \"%s\": no mesh of LOD 0 uses material slot %" PRId32 ".",
            (const char*)entry->Name, entry->MaterialSlot);
    }
    size_t PartBytes = 0U;
    if (!Memory_TryMultiplySize(*outMeshCount, sizeof(CollisionMeshPart), &PartBytes))
    {
        return Fail(name, ErrorCode_InvalidAssetData, "surface \"%s\": LOD 0 has too many meshes of its slot.",
            (const char*)entry->Name);
    }
    CollisionMeshPart* Parts = Memory_Allocate(PartBytes);
    Memory_Zero(Parts, PartBytes);
    for (size_t Part = 0U; Part < *outMeshCount; Part++)
    {
        const Mesh* Source = &lod0->meshes[outMeshIndices[Part]];
        Parts[Part].Vertices = Source->vertices;
        Parts[Part].VertexCount = (Source->vertexCount > 0) ? (uint32_t)Source->vertexCount : 0U;
        Parts[Part].Indices = Source->indices;
        Parts[Part].TriangleCount = (Source->triangleCount > 0) ? (uint32_t)Source->triangleCount : 0U;
        Parts[Part].MaterialIndex = entry->MaterialSlot;
    }
    Error Result = CollisionMesh_Create(Parts, *outMeshCount, lod0->transform, outMesh);
    Memory_Free(Parts);
    if (Result.Code != ErrorCode_Success)
    {
        Error Wrapped = Fail(name, Result.Code, "surface \"%s\": its triangles: %s", (const char*)entry->Name,
            (Result.Message != NULL) ? (const char*)Result.Message : "they make no mesh.");
        Error_Deconstruct(&Result);
        return Wrapped;
    }
    return Error_CreateSuccess();
}

/* Copies each triangle's three corners' texture coordinates (from the LOD 0 mesh and triangle it came from) into
 * @p outUvs, in the mesh's triangle order. */
static void CopyTriangleUvs(const CollisionMesh* mesh, const Model* lod0, const int* meshIndices, Vector2* outUvs)
{
    for (uint32_t Triangle = 0U; Triangle < mesh->_triangleCount; Triangle++)
    {
        const CollisionMeshTriangle* Built = &mesh->_triangles[Triangle];
        const Mesh* Source = &lod0->meshes[meshIndices[Built->_part]];
        for (uint32_t Corner = 0U; Corner < TRIANGLE_CORNERS; Corner++)
        {
            size_t Local = ((size_t)Built->_sourceIndex * TRIANGLE_CORNERS) + Corner;
            size_t Vertex = (Source->indices != NULL) ? (size_t)Source->indices[Local] : Local;
            outUvs[((size_t)Triangle * TRIANGLE_CORNERS) + Corner] = (Vector2)
            {
                Source->texcoords[Vertex * TEXCOORD_COMPONENTS], Source->texcoords[(Vertex * TEXCOORD_COMPONENTS) + 1U]
            };
        }
    }
}

/* The block's size for the built meshes, or false when it would overflow. */
static bool TryGetBlockSize(const WorldModelSurfaceDefinition* self, CollisionMesh* const* meshes, size_t* outUvOffset,
    size_t* outNameOffset, size_t* outByteCount)
{
    size_t UvCount = 0U;
    size_t NameBytes = 0U;
    for (size_t Index = 0U; Index < self->SurfaceCount; Index++)
    {
        size_t Corners = 0U;
        if (!Memory_TryMultiplySize((size_t)meshes[Index]->_triangleCount, TRIANGLE_CORNERS, &Corners)
            || !Memory_TryAddSize(UvCount, Corners, &UvCount))
        {
            return false;
        }
        NameBytes += StringUTF8_GetByteLength(self->Surfaces[Index].Name) + 1U;
    }
    size_t UvBytes = 0U;
    size_t Total = 0U;
    *outUvOffset = sizeof(SurfaceBlock) + (self->SurfaceCount * sizeof(WorldModelSurface));
    bool IsSized = Memory_TryMultiplySize(UvCount, sizeof(Vector2), &UvBytes)
        && Memory_TryAddSize(*outUvOffset, UvBytes, outNameOffset)
        && Memory_TryAddSize(*outNameOffset, NameBytes, &Total);
    *outByteCount = Total;
    return IsSized;
}


// Functions (declared in WorldModelSurfaceInternal.h).
Error WorldModelSurfaceDefinition_Parse(const char* printableName, JSONCompound* root,
    WorldModelSurfaceDefinition** outDefinition)
{
    *outDefinition = NULL;
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"surfaces", JSONValueType_Array, &Value,
        &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(printableName, ErrorCode_InvalidAssetDefinition, "\"surfaces\" must be an array of surfaces.");
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    if (Count > WORLD_MODEL_MAX_SURFACES)
    {
        return Fail(printableName, ErrorCode_InvalidAssetDefinition, "\"surfaces\" holds %" PRIu64 " surfaces; at most %"
            PRIu64 " are allowed.", (uint64_t)Count, (uint64_t)WORLD_MODEL_MAX_SURFACES);
    }
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }
    WorldModelSurfaceDefinition* Definition = Memory_Allocate(sizeof(WorldModelSurfaceDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = ParseSurface(printableName, Value.Value.Array, Index, Definition);
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldModelSurfaceDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = Definition;
    return Error_CreateSuccess();
}

void WorldModelSurfaceDefinition_Destroy(WorldModelSurfaceDefinition* self)
{
    if (self == NULL)
    {
        return;
    }
    for (size_t Index = 0U; Index < self->SurfaceCount; Index++)
    {
        Memory_Free(self->Surfaces[Index].Name);
    }
    Memory_Free(self);
}

Error WorldModelSurfaceDefinition_Load(const WorldModelSurfaceDefinition* self, const unsigned char* worldModelName,
    GameModel* lod0, WorldModel* model, void** outBlock)
{
    *outBlock = NULL;
    model->_surfaces = NULL;
    model->_surfaceCount = 0U;
    if ((self == NULL) || (self->SurfaceCount == 0U))
    {
        return Error_CreateSuccess();
    }
    const char* Name = (const char*)worldModelName;
    Model Lod0 = GameModel_GetRaylibModel(lod0);
    size_t MeshCapacity = (Lod0.meshCount > 0) ? (size_t)Lod0.meshCount : 1U;
    size_t IndexCount = 0U;
    size_t IndexBytes = 0U;
    if (!Memory_TryMultiplySize(self->SurfaceCount, MeshCapacity, &IndexCount)
        || !Memory_TryMultiplySize(IndexCount, sizeof(int), &IndexBytes))
    {
        return Fail(Name, ErrorCode_InvalidAssetData, "LOD 0 has too many meshes for its surfaces.");
    }
    int* MeshIndices = Memory_Allocate(IndexBytes);
    size_t MeshCounts[WORLD_MODEL_MAX_SURFACES] = { 0U };
    CollisionMesh* Meshes[WORLD_MODEL_MAX_SURFACES] = { NULL };
    Error Result = Error_CreateSuccess();
    size_t Built = 0U;
    for (; (Built < self->SurfaceCount) && (Result.Code == ErrorCode_Success); Built++)
    {
        Result = BuildSurfaceMesh(&self->Surfaces[Built], Name, &Lod0, &MeshIndices[Built * MeshCapacity],
            &MeshCounts[Built], &Meshes[Built]);
    }
    size_t UvOffset = 0U;
    size_t NameOffset = 0U;
    size_t ByteCount = 0U;
    if ((Result.Code == ErrorCode_Success) && !TryGetBlockSize(self, Meshes, &UvOffset, &NameOffset, &ByteCount))
    {
        Result = Fail(Name, ErrorCode_BufferTooLarge, "its surfaces are too large.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyMeshes(Meshes, Built);
        Memory_Free(MeshIndices);
        return Result;
    }

    unsigned char* Bytes = Memory_Allocate(ByteCount);
    Memory_Zero(Bytes, ByteCount);
    SurfaceBlock* Block = (SurfaceBlock*)(void*)Bytes;
    Block->SurfaceCount = (uint32_t)self->SurfaceCount;
    Vector2* Uvs = (Vector2*)(void*)(Bytes + UvOffset);
    unsigned char* Names = Bytes + NameOffset;
    for (size_t Index = 0U; Index < self->SurfaceCount; Index++)
    {
        const SurfaceEntry* Entry = &self->Surfaces[Index];
        WorldModelSurface* Surface = &Block->Surfaces[Index];
        size_t NameBytes = StringUTF8_GetByteLength(Entry->Name) + 1U;
        Memory_Copy(Entry->Name, Names, NameBytes);
        CopyTriangleUvs(Meshes[Index], &Lod0, &MeshIndices[Index * MeshCapacity], Uvs);
        Block->Meshes[Index] = Meshes[Index];
        *Surface = (WorldModelSurface)
        {
            ._name = Names, ._materialSlot = Entry->MaterialSlot, ._width = Entry->Width, ._height = Entry->Height,
            ._kind = Entry->Kind, ._emission = Entry->Emission, ._mesh = Meshes[Index], ._triangleUvs = Uvs
        };
        Names += NameBytes;
        Uvs += (size_t)Meshes[Index]->_triangleCount * TRIANGLE_CORNERS;
    }
    Memory_Free(MeshIndices);
    model->_surfaces = Block->Surfaces;
    model->_surfaceCount = Block->SurfaceCount;
    *outBlock = Block;
    return Error_CreateSuccess();
}

void WorldModelSurfaces_DestroyBlock(void* block)
{
    if (block == NULL)
    {
        return;
    }
    SurfaceBlock* Block = block;
    for (uint32_t Index = 0U; Index < Block->SurfaceCount; Index++)
    {
        CollisionMesh_Destroy(Block->Meshes[Index]);
    }
    Memory_Free(Block);
}
