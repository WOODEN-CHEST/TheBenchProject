// World files as GHDF (references/world_structure.md): the walk over the GHDF tree, reading and writing. The entry ids
// are WorldFile.h's (the root and the object's own) and the tables' wire ids; the loading rules are in WorldFile.c.

#include <inttypes.h>
#include <stdio.h>
#include "WorldFileInternal.h"
#include "GameGHDF.h"
#include "WorldObject.h"
#include "WorldComponent.h"
#include "wr/WRGHDF.h"
#include "wr/WRMemory.h"
#include "wr/WRMemoryStream.h"
#include "wr/WRString.h"


// Macros.
/** Component entries are keyed by the type's value + 1: GHDF entry ids start at 1, component types at 0. */
#define WORLD_GHDF_COMPONENT_ID_OFFSET ((GHDFEntryID)1)
/** What unknown root and object entries are reported in. */
#define WORLD_GHDF_ROOT_NAME ((const unsigned char*)u8"the root")
/** Room for an id's decimal text ("18446744073709551615"). */
#define WORLD_GHDF_ID_TEXT_CAPACITY ((size_t)24)


// Fields.
static const GHDFEntryID RootIds[] =
{
    WorldFileRootEntry_FormatVersion, WorldFileRootEntry_Name, WorldFileRootEntry_NextObjectId,
    WorldFileRootEntry_Environment, WorldFileRootEntry_Objects
};
static const GHDFEntryID ObjectIds[] = { WorldFileObjectEntry_Id, WorldFileObjectEntry_Components };


// Static functions: reading.
static bool IsOneOfIds(GHDFEntryID id, const GHDFEntryID* ids, size_t idCount)
{
    for (size_t Index = 0U; Index < idCount; Index++)
    {
        if (ids[Index] == id)
        {
            return true;
        }
    }
    return false;
}

/* Whether @p table reads (or knowingly skips) the entry: one of its wire ids, or a retired one. */
static bool IsTableEntry(const PropertyTable* table, GHDFEntryID id)
{
    for (size_t Index = 0U; Index < table->DescriptorCount; Index++)
    {
        if ((GHDFEntryID)table->Descriptors[Index].WireId == id)
        {
            return true;
        }
    }
    for (size_t Index = 0U; Index < table->RetiredWireIdCount; Index++)
    {
        if ((GHDFEntryID)table->RetiredWireIds[Index] == id)
        {
            return true;
        }
    }
    return false;
}

static void NoteUnknownId(WorldFileLoad* load, WorldFileUnknownKind kind, const unsigned char* where, uint64_t id,
    uint64_t objectId)
{
    char Text[WORLD_GHDF_ID_TEXT_CAPACITY];
    int Length = snprintf(Text, sizeof(Text), "%" PRIu64, id);
    WorldFileLoad_NoteUnknown(load, kind, where, (const unsigned char*)Text, (Length > 0) ? (size_t)Length : 0U,
        objectId);
}

/* Notes every entry of @p compound that is neither one of @p ownIds nor an entry @p table reads (NULL: no table). */
static Error NoteUnknownEntries(WorldFileLoad* load, GHDFCompound* compound, const PropertyTable* table,
    const GHDFEntryID* ownIds, size_t ownIdCount, const unsigned char* where, uint64_t objectId)
{
    WorldFileEnumeratorStorage Storage;
    CollectionEnumerator* Enumerator = WorldFileInternal_BeginEnumeration(GHDFCompound_GetKeyCollection(compound),
        &Storage);
    if (Enumerator == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "Could not enumerate the GHDF entries.");
    }
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        GHDFEntryID Id = GHDF_ENTRY_ID_INVALID;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Id);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (!IsOneOfIds(Id, ownIds, ownIdCount) && ((table == NULL) || !IsTableEntry(table, Id)))
        {
            NoteUnknownId(load, WorldFileUnknownKind_Entry, where, Id, objectId);
        }
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    }
    WorldFileInternal_EndEnumeration(Enumerator, &Storage);
    return Result;
}

/* An optional entry that must be a compound (not an array of them). */
static Error GetOptionalCompound(const WorldFileLoad* load, GHDFCompound* compound, GHDFEntryID id, const char* what,
    GHDFCompound** outCompound)
{
    GHDFObjectValue Entry = { .Type = GHDFValueType_None };
    bool IsFound = false;
    *outCompound = NULL;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &Entry,
        &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct4(load->FormatErrorCode, "%s must be a compound.", what);
    }
    *outCompound = IsFound ? Entry.Value.Compound : NULL;
    return Error_CreateSuccess();
}

static Error GetOptionalUnsigned(const WorldFileLoad* load, GHDFCompound* compound, GHDFEntryID id, const char* what,
    uint64_t* outValue, bool* outFound)
{
    Error Result = GameGHDF_GetOptionalUnsigned(compound, id, UINT64_MAX, outValue, outFound);
    return (Result.Code == ErrorCode_Success) ? Result : WorldFileLoad_Wrap(load, what, &Result);
}

/* The parent id an object's compound asked for (0 when it named none or none can be read), for the refused-parent
 * error. */
static uint64_t GetRequestedParent(GHDFCompound* compound)
{
    const PropertyDescriptor* ParentRow = PropertyTable_FindByKey(WorldObject_GetPropertyTable(), u8"parent");
    uint64_t ParentId = 0U;
    bool IsFound = false;
    if (ParentRow != NULL)
    {
        Error Result = GameGHDF_GetOptionalUnsigned(compound, (GHDFEntryID)ParentRow->WireId, UINT64_MAX, &ParentId,
            &IsFound);
        Error_Deconstruct(&Result);
    }
    return IsFound ? ParentId : 0U;
}

/* A table's block brought up to the table's current version (its migration, for a file of an older format), before the
 * load notes its unknown entries; @p outOptions then reads it as current data. */
static Error MigrateBlock(const WorldFileLoad* load, const PropertyTable* table, GHDFCompound* block,
    PropertyReadOptions* outOptions)
{
    *outOptions = WorldFileLoad_GetTableOptions(load, table);
    Error Result = Properties_MigrateGHDF(table, block, outOptions);
    outOptions->DataVersion = 0U;
    return Result;
}

/* The component entry @p entryId of @p components: a component of a known type, read through its table (an unknown type
 * is noted and skipped). */
static Error ReadComponent(WorldFileLoad* load, WorldObject* object, GHDFCompound* components, GHDFEntryID entryId)
{
    uint64_t Id = WorldObject_GetId(object);
    uint64_t TypeValue = entryId - WORLD_GHDF_COMPONENT_ID_OFFSET;
    if (TypeValue >= (uint64_t)World_GetComponentTypeCount(load->Target))
    {
        NoteUnknownId(load, WorldFileUnknownKind_ComponentType, NULL, TypeValue, Id);
        return Error_CreateSuccess();
    }
    WorldComponentType Type = (WorldComponentType)TypeValue;
    const WorldComponentTypeInfo* Info = World_GetComponentTypeInfo(load->Target, Type);
    GHDFObjectValue Block;
    Error Result = GHDFCompound_GetVerified(components, entryId, GHDF_CreateRegularType(GHDFValueType_Compound), &Block);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct4(load->FormatErrorCode, "object %" PRIu64 ": the component \"%s\" must be a compound.",
            Id, (const char*)Info->Name);
    }

    WorldComponent* Component = NULL;
    PropertyReadOptions Options;
    Result = WorldFileLoad_AddComponent(load, object, Type, &Component);
    if (Result.Code == ErrorCode_Success)
    {
        Result = MigrateBlock(load, Info->Table, Block.Value.Compound, &Options);
        if (Result.Code != ErrorCode_Success)
        {
            return WorldFileLoad_WrapObject(load, Id, &Result);
        }
        Result = NoteUnknownEntries(load, Block.Value.Compound, Info->Table, NULL, 0U, Info->Table->Name, Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_ReadGHDF(Info->Table, Block.Value.Compound, Component, &Options);
        if (Result.Code != ErrorCode_Success)
        {
            Result = WorldFileLoad_WrapObject(load, Id, &Result);
        }
    }
    return Result;
}

static Error ReadComponents(WorldFileLoad* load, GHDFCompound* compound, WorldObject* object)
{
    GHDFCompound* Components = NULL;
    char What[WORLD_GHDF_ID_TEXT_CAPACITY + 32U];
    snprintf(What, sizeof(What), "object %" PRIu64 ": the components", WorldObject_GetId(object));
    Error Result = GetOptionalCompound(load, compound, WorldFileObjectEntry_Components, What, &Components);
    if ((Result.Code != ErrorCode_Success) || (Components == NULL))
    {
        return Result;
    }

    // The ids, then each entry by id: the key collection's enumerator fits on the stack, the entry collection's
    // allocates an inner one.
    WorldFileEnumeratorStorage Storage;
    CollectionEnumerator* Enumerator = WorldFileInternal_BeginEnumeration(GHDFCompound_GetKeyCollection(Components),
        &Storage);
    if (Enumerator == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "Could not enumerate the components.");
    }
    bool HasNext = false;
    Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        GHDFEntryID EntryId = GHDF_ENTRY_ID_INVALID;
        Result = CollectionEnumerator_NextByValue(Enumerator, &EntryId);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadComponent(load, object, Components, EntryId);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        }
    }
    WorldFileInternal_EndEnumeration(Enumerator, &Storage);
    return Result;
}

/* An object's table (its parent attaching through the world's checks), its components, then the data limit. */
static Error ReadObject(WorldFileLoad* load, GHDFCompound* compound, WorldObject* object)
{
    uint64_t Id = WorldObject_GetId(object);
    const PropertyTable* Table = WorldObject_GetPropertyTable();
    PropertyReadOptions Options;
    Error Result = MigrateBlock(load, Table, compound, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WorldFileLoad_WrapObject(load, Id, &Result);
    }
    Result = NoteUnknownEntries(load, compound, Table, ObjectIds, sizeof(ObjectIds) / sizeof(ObjectIds[0]), Table->Name,
        Id);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    uint64_t RefusedBefore = World_GetRefusedParentCount(load->Target);
    Result = Properties_ReadGHDF(Table, compound, object, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WorldFileLoad_WrapObject(load, Id, &Result);
    }
    if (World_GetRefusedParentCount(load->Target) != RefusedBefore)
    {
        return WorldFileLoad_CheckParent(load, object, RefusedBefore, GetRequestedParent(compound));
    }

    Result = ReadComponents(load, compound, object);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return WorldFileLoad_CheckDataLimit(load, object);
}

/* The object compound at @p index of the list, and its id (required). */
static Error GetObjectEntry(WorldFileLoad* load, GHDFArray* objects, size_t index, GHDFCompound** outCompound,
    uint64_t* outId)
{
    GHDFObjectValue Element = { .Type = GHDFValueType_None };
    Error Result = GHDFArray_Get(objects, index, &Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outCompound = Element.Value.Compound;
    char What[WORLD_GHDF_ID_TEXT_CAPACITY + 16U];
    snprintf(What, sizeof(What), "objects[%" PRIu64 "].id", (uint64_t)index);
    bool IsFound = false;
    Result = GetOptionalUnsigned(load, *outCompound, WorldFileObjectEntry_Id, What, outId, &IsFound);
    if ((Result.Code == ErrorCode_Success) && !IsFound)
    {
        Result = Error_Construct4(load->FormatErrorCode, "objects[%" PRIu64 "] has no id (entry %" PRIu64 ").",
            (uint64_t)index, (uint64_t)WorldFileObjectEntry_Id);
    }
    return Result;
}

/* Every object is created first, so each parent named in the second pass is an object that exists. */
static Error ReadObjects(WorldFileLoad* load, GHDFCompound* root)
{
    GHDFObjectValue Entry = { .Type = GHDFValueType_None };
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(root, WorldFileRootEntry_Objects,
        GHDF_CreateArrayType(GHDFValueType_Compound), &Entry, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct4(load->FormatErrorCode, "The objects (entry %" PRIu64 ") must be an array of compounds.",
            (uint64_t)WorldFileRootEntry_Objects);
    }
    if (!IsFound)
    {
        return Error_CreateSuccess();
    }
    GHDFArray* Objects = Entry.Value.Array;
    size_t Count = GHDFArray_GetElementCount(Objects);
    Result = WorldFileLoad_CheckObjectCount(load, Count);

    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFCompound* Compound = NULL;
        uint64_t Id = 0U;
        WorldObject* Object = NULL;
        Result = GetObjectEntry(load, Objects, Index, &Compound, &Id);
        if (Result.Code == ErrorCode_Success)
        {
            Result = WorldFileLoad_CreateObject(load, Index, Id, &Object);
        }
    }
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFCompound* Compound = NULL;
        uint64_t Id = 0U;
        WorldObject* Object = NULL;
        Result = GetObjectEntry(load, Objects, Index, &Compound, &Id);
        if ((Result.Code == ErrorCode_Success) && World_TryGetObject(load->Target, Id, &Object))
        {
            Result = ReadObject(load, Compound, Object);
        }
    }
    return Result;
}

static Error ReadEnvironment(WorldFileLoad* load, GHDFCompound* root)
{
    GHDFCompound* Environment = NULL;
    Error Result = GetOptionalCompound(load, root, WorldFileRootEntry_Environment, "The environment (entry 4)",
        &Environment);
    if ((Result.Code != ErrorCode_Success) || (Environment == NULL))
    {
        return Result;
    }
    const PropertyTable* Table = WorldEnvironment_GetPropertyTable();
    PropertyReadOptions Options;
    Result = MigrateBlock(load, Table, Environment, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WorldFileLoad_Wrap(load, "environment", &Result);
    }
    Result = NoteUnknownEntries(load, Environment, Table, NULL, 0U, Table->Name, 0U);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_ReadGHDF(Table, Environment, World_GetEnvironment(load->Target), &Options);
        if (Result.Code != ErrorCode_Success)
        {
            Result = WorldFileLoad_Wrap(load, "environment", &Result);
        }
    }
    return Result;
}

/* The format version (required), the name and the next object id (both optional). */
static Error ReadHeader(WorldFileLoad* load, GHDFCompound* root, bool* outHasNextObjectId, uint64_t* outNextObjectId)
{
    uint64_t Version = 0U;
    bool IsFound = false;
    Error Result = GetOptionalUnsigned(load, root, WorldFileRootEntry_FormatVersion, "The format version (entry 1)",
        &Version, &IsFound);
    if ((Result.Code == ErrorCode_Success) && !IsFound)
    {
        Result = Error_Construct2(load->FormatErrorCode, "The format version (entry 1) is missing.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldFileLoad_SetFormatVersion(load, Version);
    }

    const unsigned char* Name = NULL;
    size_t NameLength = 0U;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameGHDF_GetOptionalString(root, WorldFileRootEntry_Name, &Name, &NameLength, &IsFound);
        if (Result.Code != ErrorCode_Success)
        {
            Result = WorldFileLoad_Wrap(load, "The name (entry 2)", &Result);
        }
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = WorldFileLoad_SetName(load, Name, NameLength);
    }

    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptionalUnsigned(load, root, WorldFileRootEntry_NextObjectId, "The next object id (entry 3)",
            outNextObjectId, outHasNextObjectId);
    }
    return Result;
}

static Error ReadWorld(WorldFileLoad* load, GHDFCompound* root)
{
    bool HasNextObjectId = false;
    uint64_t NextObjectId = 0U;
    Error Result = NoteUnknownEntries(load, root, NULL, RootIds, sizeof(RootIds) / sizeof(RootIds[0]),
        WORLD_GHDF_ROOT_NAME, 0U);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadHeader(load, root, &HasNextObjectId, &NextObjectId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadEnvironment(load, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadObjects(load, root);
    }
    return (Result.Code == ErrorCode_Success) ? WorldFileLoad_Finish(load, HasNextObjectId, NextObjectId)
        : WorldFileLoad_Fail(load, Result);
}

/* Parses the bytes into a compound tree from @p pool, the tree taking at most @p maxTreeByteCount (the reader's
 * estimate); the document must use every byte. */
static Error ParseDocument(const unsigned char* bytes, size_t byteCount, size_t maxTreeByteCount, GHDFObjectPool* pool,
    GHDFCompound** outRoot)
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
    GHDFReadLimits Limits = { .MaxTreeByteCount = maxTreeByteCount };
    Result = GHDF_ReadWithLimits(MemoryStream_AsIOStream(&Stream), pool, &Limits, outRoot);
    if ((Result.Code != ErrorCode_Success) && (Result.Code != ErrorCode_Deserialize))
    {
        // The reader reports some broken documents as stream errors (data that ends early); to callers they are all
        // documents that do not read.
        Error Recoded = Error_Construct4(ErrorCode_Deserialize, "%s",
            (Result.Message != NULL) ? (const char*)Result.Message : "The world file is not a GHDF document.");
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
        Result = Error_Construct4(ErrorCode_Deserialize, "The world file has %" PRIu64 " bytes after its document.",
            (uint64_t)(byteCount - Position));
    }
    MemoryStream_Deconstruct(&Stream);
    return Result;
}


// Static functions: writing.
static Error PutCompound(GHDFCompound* compound, GHDFEntryID id, GHDFCompound* value)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateCompound(value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &Value);
}

/* A compound of an instance's non-default properties (the pool reclaims it if anything fails later). */
static Error CreateTableBlock(GHDFObjectPool* pool, const PropertyTable* table, const void* instance,
    GHDFCompound** outBlock)
{
    Error Result = GHDFObjectPool_BorrowCompound(pool, outBlock);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_WriteGHDF(table, instance, pool, *outBlock, NULL);
    }
    return Result;
}

/* The object's components, keyed by their type's value + 1; nothing when it has none. */
static Error WriteComponents(const World* world, GHDFObjectPool* pool, const WorldObject* object, GHDFCompound* compound)
{
    GHDFCompound* Components = NULL;
    Error Result = Error_CreateSuccess();
    for (size_t Type = 0U; (Type < World_GetComponentTypeCount(world)) && (Result.Code == ErrorCode_Success); Type++)
    {
        WorldComponent* Component = WorldObject_GetComponent(object, (WorldComponentType)Type);
        if (Component == NULL)
        {
            continue;
        }
        if (Components == NULL)
        {
            Result = GHDFObjectPool_BorrowCompound(pool, &Components);
        }
        GHDFCompound* Block = NULL;
        if (Result.Code == ErrorCode_Success)
        {
            Result = CreateTableBlock(pool, World_GetComponentTypeInfo(world, (WorldComponentType)Type)->Table, Component,
                &Block);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = PutCompound(Components, (GHDFEntryID)Type + WORLD_GHDF_COMPONENT_ID_OFFSET, Block);
        }
    }
    if ((Result.Code == ErrorCode_Success) && (Components != NULL))
    {
        Result = PutCompound(compound, WorldFileObjectEntry_Components, Components);
    }
    return Result;
}

static Error WriteObject(const World* world, GHDFObjectPool* pool, const WorldObject* object, GHDFArray* objects)
{
    GHDFCompound* Compound = NULL;
    Error Result = GHDFObjectPool_BorrowCompound(pool, &Compound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameGHDF_PutUnsigned(Compound, WorldFileObjectEntry_Id, WorldObject_GetId(object));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_WriteGHDF(WorldObject_GetPropertyTable(), object, pool, Compound, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WriteComponents(world, pool, object, Compound);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return WorldFileInternal_WrapObjectError(WorldObject_GetId(object), &Result);
    }
    GHDFObjectValue Value = GHDFObjectValue_CreateCompound(Compound);
    return GHDFArray_AddValue(objects, &Value);
}

static Error WriteRoot(const World* world, GHDFObjectPool* pool, const GenericBuffer* order, GHDFCompound* root)
{
    Error Result = GameGHDF_PutUnsigned(root, WorldFileRootEntry_FormatVersion, WORLD_FILE_FORMAT_VERSION);
    if ((Result.Code == ErrorCode_Success) && (World_GetName(world) != NULL))
    {
        Result = GameGHDF_PutString(pool, root, WorldFileRootEntry_Name, World_GetName(world));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameGHDF_PutUnsigned(root, WorldFileRootEntry_NextObjectId, WorldFileInternal_GetNextObjectIdToWrite(world));
    }

    GHDFCompound* Environment = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateTableBlock(pool, WorldEnvironment_GetPropertyTable(), &world->Environment, &Environment);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PutCompound(root, WorldFileRootEntry_Environment, Environment);
    }

    GHDFArray* Objects = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_Compound, &Objects);
    }
    WorldObject* const* Ordered = (WorldObject* const*)order->_data;
    for (size_t Index = 0U; (Index < order->_count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = WriteObject(world, pool, Ordered[Index], Objects);
    }
    if (Result.Code == ErrorCode_Success)
    {
        GHDFObjectValue Value = GHDFObjectValue_CreateArray(Objects, GHDFValueType_Compound);
        Result = GHDFCompound_SetValue(root, WorldFileRootEntry_Objects, GHDF_CreateArrayType(GHDFValueType_Compound),
            &Value);
    }
    return Result;
}

/* Writes the document into its own stream, then appends its bytes, so nothing is appended on failure. */
static Error AppendDocument(GHDFCompound* root, GenericBuffer* outBytes)
{
    MemoryStream Stream;
    Error Result = MemoryStream_Construct1(&Stream, IOStreamFlags_CanWrite);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = GHDF_Write(root, MemoryStream_AsIOStream(&Stream));
    GenericBuffer* Written = MemoryStream_GetBuffer(&Stream);
    if ((Result.Code == ErrorCode_Success)
        && !GenericBuffer_AppendRangeBytes(outBytes, (unsigned char*)Written->_data, Written->_count))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not hold the world file.");
    }
    MemoryStream_Deconstruct(&Stream);
    return Result;
}


// Public functions.
// TODO: worlds sent over the network could be compressed (raylib's CompressData / DecompressData); untrusted compressed
// data then needs its own size limit before it is decompressed, as WORLD_FILE_MAX_BYTE_COUNT limits it after.
Error WorldFile_ReadGHDF(const unsigned char* bytes, size_t byteCount, const WorldFileReadOptions* options,
    World* outWorld)
{
    if (((bytes == NULL) && (byteCount > 0U)) || (options == NULL) || (options->Keys == NULL)
        || (options->MaxTreeByteCount == 0U) || (outWorld == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldFile_ReadGHDF: the bytes, the options with their key "
            "registry and a tree limit above 0, and the world are required.");
    }
    if (byteCount > WORLD_FILE_MAX_BYTE_COUNT)
    {
        return Error_Construct4(ErrorCode_Deserialize, "The world file has %" PRIu64 " bytes; at most %" PRIu64
            " are allowed.", (uint64_t)byteCount, (uint64_t)WORLD_FILE_MAX_BYTE_COUNT);
    }

    // A pool per load, so a large world's compounds are freed with it instead of staying pooled.
    GHDFObjectPool* Pool = NULL;
    Error Result = GHDFObjectPool_Create(&Pool);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFCompound* Root = NULL;
    Result = ParseDocument(bytes, byteCount, options->MaxTreeByteCount, Pool, &Root);
    if (Result.Code == ErrorCode_Success)
    {
        WorldFileLoad Load;
        Result = WorldFileLoad_Begin(&Load, options, outWorld, ErrorCode_Deserialize);
        if (Result.Code == ErrorCode_Success)
        {
            Load.GHDFPool = Pool;
            Result = ReadWorld(&Load, Root);
        }
    }

    // Deconstructing the pool frees the tree with it; returning the root first would only walk it once more.
    Error PoolResult = GHDFObjectPool_Deconstruct(Pool);
    Error_Deconstruct(&PoolResult);
    return Result;
}

Error WorldFile_WriteGHDF(const World* world, GenericBuffer* outBytes)
{
    if ((world == NULL) || (outBytes == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldFile_WriteGHDF: world and outBytes must not be NULL.");
    }
    GenericBuffer Order;
    GenericBuffer_AllocateVariable(&Order, World_GetObjectCount(world), sizeof(WorldObject*));
    Error Result = WorldFileInternal_CollectObjects(world, &Order);
    GHDFObjectPool* Pool = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDFObjectPool_Create(&Pool);
    }
    GHDFCompound* Root = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDFObjectPool_BorrowCompound(Pool, &Root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WriteRoot(world, Pool, &Order, Root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AppendDocument(Root, outBytes);
    }

    if (Pool != NULL)
    {
        // Deconstructing the pool frees the tree with it.
        Error PoolResult = GHDFObjectPool_Deconstruct(Pool);
        Error_Deconstruct(&PoolResult);
    }
    Memory_Free(Order._data);
    return Result;
}
