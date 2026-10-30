// World files as JSON (references/world_structure.md): the walk over the JSON tree, reading and writing. The loading
// rules are in WorldFile.c; the values come from the property tables.

#include <inttypes.h>
#include <stdio.h>
#include "WorldFileInternal.h"
#include "GameJSON.h"
#include "WorldObject.h"
#include "WorldComponent.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** The root's keys. */
#define WORLD_JSON_FORMAT_VERSION_KEY ((const unsigned char*)u8"format_version")
#define WORLD_JSON_NAME_KEY ((const unsigned char*)u8"name")
#define WORLD_JSON_NEXT_OBJECT_ID_KEY ((const unsigned char*)u8"next_object_id")
#define WORLD_JSON_ENVIRONMENT_KEY ((const unsigned char*)u8"environment")
#define WORLD_JSON_OBJECTS_KEY ((const unsigned char*)u8"objects")
/** An object's own keys, next to its table's. */
#define WORLD_JSON_ID_KEY ((const unsigned char*)u8"id")
#define WORLD_JSON_COMPONENTS_KEY ((const unsigned char*)u8"components")
/** The object table's row a refused parent came from (named in the error). */
#define WORLD_JSON_PARENT_KEY ((const unsigned char*)u8"parent")
/** What unknown root keys are reported in. */
#define WORLD_JSON_ROOT_NAME ((const unsigned char*)u8"the root")
/** Room for an error prefix such as "objects[99999]". */
#define WORLD_JSON_PREFIX_CAPACITY ((size_t)64)


// Fields.
static const unsigned char* const RootKeys[] =
{
    WORLD_JSON_FORMAT_VERSION_KEY, WORLD_JSON_NAME_KEY, WORLD_JSON_NEXT_OBJECT_ID_KEY, WORLD_JSON_ENVIRONMENT_KEY,
    WORLD_JSON_OBJECTS_KEY
};
static const unsigned char* const ObjectKeys[] = { WORLD_JSON_ID_KEY, WORLD_JSON_COMPONENTS_KEY };


// Static functions: enumeration.
/* An enumerator over @p compound's entries, on the stack when it fits (WorldFileInternal_BeginEnumeration). */
static CollectionEnumerator* BeginEntries(JSONCompound* compound, WorldFileEnumeratorStorage* storage)
{
    return WorldFileInternal_BeginEnumeration(JSONCompound_GetEntryCollection(compound), storage);
}


// Static functions: reading.
static bool IsOneOf(const unsigned char* key, const unsigned char* const* keys, size_t keyCount)
{
    size_t Length = StringUTF8_GetByteLength(key);
    for (size_t Index = 0U; Index < keyCount; Index++)
    {
        if ((StringUTF8_GetByteLength(keys[Index]) == Length) && Memory_IsEqual(keys[Index], key, Length))
        {
            return true;
        }
    }
    return false;
}

/* Notes every key of @p compound that is neither one of @p ownKeys nor a key @p table reads (NULL: no table). */
static Error NoteUnknownKeys(WorldFileLoad* load, JSONCompound* compound, const PropertyTable* table,
    const unsigned char* const* ownKeys, size_t ownKeyCount, const unsigned char* where, uint64_t objectId)
{
    WorldFileEnumeratorStorage Storage;
    CollectionEnumerator* Enumerator = BeginEntries(compound, &Storage);
    if (Enumerator == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "Could not enumerate the JSON keys.");
    }
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        bool IsKnown = IsOneOf(Entry.Key, ownKeys, ownKeyCount)
            || ((table != NULL) && (PropertyTable_FindByJSONKey(table, Entry.Key) != NULL));
        if (!IsKnown)
        {
            WorldFileLoad_NoteUnknown(load, WorldFileUnknownKind_Key, where, Entry.Key,
                StringUTF8_GetByteLength(Entry.Key), objectId);
        }
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    }
    WorldFileInternal_EndEnumeration(Enumerator, &Storage);
    return Result;
}

static Error ReadUInt64(const WorldFileLoad* load, const JSONObjectValue* value, const char* what, uint64_t* outValue)
{
    Error Result = GameJSON_ValueToUInt64(value, outValue);
    return (Result.Code == ErrorCode_Success) ? Result : WorldFileLoad_Wrap(load, what, &Result);
}

/* The parent id an object's JSON asked for (0 when it named none or none can be read), for the refused-parent error. */
static uint64_t GetRequestedParent(JSONCompound* json)
{
    JSONObjectValue Value;
    bool IsFound = false;
    uint64_t ParentId = 0U;
    Error Result = JSONCompound_GetOptional(json, WORLD_JSON_PARENT_KEY, &Value, &IsFound);
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = GameJSON_ValueToUInt64(&Value, &ParentId);
    }
    Error_Deconstruct(&Result);
    return ParentId;
}

/* A table's block brought up to the table's current version (its migration, for a file of an older format), before the
 * load notes its unknown keys; @p outOptions then reads it as current data. */
static Error MigrateBlock(const WorldFileLoad* load, const PropertyTable* table, JSONCompound* block,
    PropertyReadOptions* outOptions)
{
    *outOptions = WorldFileLoad_GetTableOptions(load, table);
    Error Result = Properties_MigrateJSON(table, block, outOptions);
    outOptions->DataVersion = 0U;
    return Result;
}

/* One component block: a known type is added and read through its table, an unknown one noted and skipped. */
static Error ReadComponent(WorldFileLoad* load, WorldObject* object, const JSONCompoundEntry* entry)
{
    uint64_t Id = WorldObject_GetId(object);
    WorldComponentType Type = WorldComponentType_Model;
    if (!World_TryFindComponentType(load->Target, entry->Key, &Type))
    {
        WorldFileLoad_NoteUnknown(load, WorldFileUnknownKind_Component, NULL, entry->Key,
            StringUTF8_GetByteLength(entry->Key), Id);
        return Error_CreateSuccess();
    }
    if (entry->Value.Type != JSONValueType_Compound)
    {
        return Error_Construct4(load->FormatErrorCode, "object %" PRIu64 ": the component \"%s\" must be an object.",
            Id, (const char*)entry->Key);
    }

    const PropertyTable* Table = World_GetComponentTypeInfo(load->Target, Type)->Table;
    WorldComponent* Component = NULL;
    PropertyReadOptions Options;
    Error Result = WorldFileLoad_AddComponent(load, object, Type, &Component);
    if (Result.Code == ErrorCode_Success)
    {
        Result = MigrateBlock(load, Table, entry->Value.Value.Compound, &Options);
        if (Result.Code != ErrorCode_Success)
        {
            return WorldFileLoad_WrapObject(load, Id, &Result);
        }
        Result = NoteUnknownKeys(load, entry->Value.Value.Compound, Table, NULL, 0U, Table->Name, Id);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_ReadJSON(Table, entry->Value.Value.Compound, Component, &Options);
        if (Result.Code != ErrorCode_Success)
        {
            Result = WorldFileLoad_WrapObject(load, Id, &Result);
        }
    }
    return Result;
}

static Error ReadComponents(WorldFileLoad* load, JSONCompound* json, WorldObject* object)
{
    JSONObjectValue Components;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(json, WORLD_JSON_COMPONENTS_KEY, &Components, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    if (Components.Type != JSONValueType_Compound)
    {
        return Error_Construct4(load->FormatErrorCode, "object %" PRIu64 ": \"components\" must be an object.",
            WorldObject_GetId(object));
    }

    WorldFileEnumeratorStorage Storage;
    CollectionEnumerator* Enumerator = BeginEntries(Components.Value.Compound, &Storage);
    if (Enumerator == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "Could not enumerate the components.");
    }
    bool HasNext = false;
    Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadComponent(load, object, &Entry);
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
static Error ReadObject(WorldFileLoad* load, JSONCompound* json, WorldObject* object)
{
    uint64_t Id = WorldObject_GetId(object);
    const PropertyTable* Table = WorldObject_GetPropertyTable();
    PropertyReadOptions Options;
    Error Result = MigrateBlock(load, Table, json, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WorldFileLoad_WrapObject(load, Id, &Result);
    }
    Result = NoteUnknownKeys(load, json, Table, ObjectKeys, sizeof(ObjectKeys) / sizeof(ObjectKeys[0]), Table->Name, Id);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    uint64_t RefusedBefore = World_GetRefusedParentCount(load->Target);
    Result = Properties_ReadJSON(Table, json, object, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WorldFileLoad_WrapObject(load, Id, &Result);
    }
    if (World_GetRefusedParentCount(load->Target) != RefusedBefore)
    {
        return WorldFileLoad_CheckParent(load, object, RefusedBefore, GetRequestedParent(json));
    }

    Result = ReadComponents(load, json, object);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return WorldFileLoad_CheckDataLimit(load, object);
}

/* The object at @p index of the list, which must be a compound. */
static Error GetObjectEntry(const WorldFileLoad* load, JSONArray* objects, size_t index, JSONCompound** outJson)
{
    JSONObjectValue Element;
    Error Result = JSONArray_Get(objects, index, &Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Element.Type != JSONValueType_Compound)
    {
        return Error_Construct4(load->FormatErrorCode, "objects[%" PRIu64 "] must be an object.", (uint64_t)index);
    }
    *outJson = Element.Value.Compound;
    return Error_CreateSuccess();
}

/* The id of an object's JSON, which is required. */
static Error GetObjectId(const WorldFileLoad* load, JSONCompound* json, size_t index, uint64_t* outId)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(json, WORLD_JSON_ID_KEY, &Value, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsFound)
    {
        return Error_Construct4(load->FormatErrorCode, "objects[%" PRIu64 "] has no \"id\".", (uint64_t)index);
    }
    char What[WORLD_JSON_PREFIX_CAPACITY];
    snprintf(What, sizeof(What), "objects[%" PRIu64 "].id", (uint64_t)index);
    return ReadUInt64(load, &Value, What, outId);
}

/* Every object is created first, so each parent named in the second pass is an object that exists. */
static Error ReadObjects(WorldFileLoad* load, JSONCompound* root)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(root, WORLD_JSON_OBJECTS_KEY, &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    if (Value.Type != JSONValueType_Array)
    {
        return Error_Construct2(load->FormatErrorCode, "\"objects\" must be an array.");
    }
    JSONArray* Objects = Value.Value.Array;
    size_t Count = JSONArray_GetElementCount(Objects);
    Result = WorldFileLoad_CheckObjectCount(load, Count);

    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONCompound* Json = NULL;
        uint64_t Id = 0U;
        WorldObject* Object = NULL;
        Result = GetObjectEntry(load, Objects, Index, &Json);
        if (Result.Code == ErrorCode_Success)
        {
            Result = GetObjectId(load, Json, Index, &Id);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = WorldFileLoad_CreateObject(load, Index, Id, &Object);
        }
    }
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONCompound* Json = NULL;
        uint64_t Id = 0U;
        WorldObject* Object = NULL;
        Result = GetObjectEntry(load, Objects, Index, &Json);
        if (Result.Code == ErrorCode_Success)
        {
            Result = GetObjectId(load, Json, Index, &Id);
        }
        if ((Result.Code == ErrorCode_Success) && World_TryGetObject(load->Target, Id, &Object))
        {
            Result = ReadObject(load, Json, Object);
        }
    }
    return Result;
}

static Error ReadEnvironment(WorldFileLoad* load, JSONCompound* root)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(root, WORLD_JSON_ENVIRONMENT_KEY, &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    if (Value.Type != JSONValueType_Compound)
    {
        return Error_Construct2(load->FormatErrorCode, "\"environment\" must be an object.");
    }
    const PropertyTable* Table = WorldEnvironment_GetPropertyTable();
    PropertyReadOptions Options;
    Result = MigrateBlock(load, Table, Value.Value.Compound, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WorldFileLoad_Wrap(load, "environment", &Result);
    }
    Result = NoteUnknownKeys(load, Value.Value.Compound, Table, NULL, 0U, Table->Name, 0U);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_ReadJSON(Table, Value.Value.Compound, World_GetEnvironment(load->Target), &Options);
        if (Result.Code != ErrorCode_Success)
        {
            Result = WorldFileLoad_Wrap(load, "environment", &Result);
        }
    }
    return Result;
}

/* The format version (required), the name and the next object id (both optional). */
static Error ReadHeader(WorldFileLoad* load, JSONCompound* root, bool* outHasNextObjectId, uint64_t* outNextObjectId)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(root, WORLD_JSON_FORMAT_VERSION_KEY, &Value, &IsFound);
    if ((Result.Code == ErrorCode_Success) && !IsFound)
    {
        Result = Error_Construct2(load->FormatErrorCode, "\"format_version\" is missing.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        uint64_t Version = 0U;
        Result = ReadUInt64(load, &Value, "format_version", &Version);
        if (Result.Code == ErrorCode_Success)
        {
            Result = WorldFileLoad_SetFormatVersion(load, Version);
        }
    }

    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(root, WORLD_JSON_NAME_KEY, &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound && (Value.Type != JSONValueType_Null))
    {
        Result = (Value.Type == JSONValueType_String)
            ? WorldFileLoad_SetName(load, Value.Value.String->_data, Value.Value.String->_count)
            : Error_Construct2(load->FormatErrorCode, "\"name\" must be a string or null.");
    }

    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(root, WORLD_JSON_NEXT_OBJECT_ID_KEY, &Value, outHasNextObjectId);
    }
    if ((Result.Code == ErrorCode_Success) && *outHasNextObjectId)
    {
        Result = ReadUInt64(load, &Value, "next_object_id", outNextObjectId);
    }
    return Result;
}

static Error ReadWorld(WorldFileLoad* load, JSONCompound* root)
{
    bool HasNextObjectId = false;
    uint64_t NextObjectId = 0U;
    Error Result = NoteUnknownKeys(load, root, NULL, RootKeys, sizeof(RootKeys) / sizeof(RootKeys[0]),
        WORLD_JSON_ROOT_NAME, 0U);
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


// Static functions: writing.
static Error SetValue(JSONCompound* compound, const unsigned char* key, JSONObjectValue* value)
{
    return JSONCompound_Set(compound, key, value);
}

/* A compound of an instance's non-default properties (the pool reclaims it if anything fails later). */
static Error CreateTableBlock(JSONObjectPool* pool, const PropertyTable* table, const void* instance,
    JSONCompound** outBlock)
{
    Error Result = JSONObjectPool_BorrowCompound(pool, outBlock);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_WriteJSON(table, instance, pool, *outBlock, NULL);
    }
    return Result;
}

/* The object's components, keyed by their registry names in type order; nothing when it has none. */
static Error WriteComponents(const World* world, JSONObjectPool* pool, const WorldObject* object, JSONCompound* json)
{
    JSONCompound* Components = NULL;
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
            Result = JSONObjectPool_BorrowCompound(pool, &Components);
        }
        const WorldComponentTypeInfo* Info = World_GetComponentTypeInfo(world, (WorldComponentType)Type);
        JSONCompound* Block = NULL;
        if (Result.Code == ErrorCode_Success)
        {
            Result = CreateTableBlock(pool, Info->Table, Component, &Block);
        }
        if (Result.Code == ErrorCode_Success)
        {
            JSONObjectValue BlockValue = JSONObjectValue_CreateCompound(Block);
            Result = SetValue(Components, Info->Name, &BlockValue);
        }
    }
    if ((Result.Code == ErrorCode_Success) && (Components != NULL))
    {
        JSONObjectValue ComponentsValue = JSONObjectValue_CreateCompound(Components);
        Result = SetValue(json, WORLD_JSON_COMPONENTS_KEY, &ComponentsValue);
    }
    return Result;
}

/* { "id": ..., the object table's keys in table order, "components": {...} } */
static Error WriteObject(const World* world, JSONObjectPool* pool, const WorldObject* object, JSONArray* objects)
{
    JSONCompound* Json = NULL;
    Error Result = JSONObjectPool_BorrowCompound(pool, &Json);
    JSONObjectValue Value;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameJSON_CreateUInt64Value(pool, WorldObject_GetId(object), &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetValue(Json, WORLD_JSON_ID_KEY, &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_WriteJSON(WorldObject_GetPropertyTable(), object, pool, Json, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WriteComponents(world, pool, object, Json);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return WorldFileInternal_WrapObjectError(WorldObject_GetId(object), &Result);
    }
    Value = JSONObjectValue_CreateCompound(Json);
    return JSONArray_Add(objects, &Value);
}

static Error WriteRoot(const World* world, JSONObjectPool* pool, const GenericBuffer* order, JSONCompound* root)
{
    JSONObjectValue Value = { .Type = JSONValueType_Integer, .Value.Integer = (int64_t)WORLD_FILE_FORMAT_VERSION };
    Error Result = SetValue(root, WORLD_JSON_FORMAT_VERSION_KEY, &Value);
    if ((Result.Code == ErrorCode_Success) && (World_GetName(world) != NULL))
    {
        Result = GameJSON_CreateStringValue(pool, World_GetName(world), &Value);
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetValue(root, WORLD_JSON_NAME_KEY, &Value);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameJSON_CreateUInt64Value(pool, WorldFileInternal_GetNextObjectIdToWrite(world), &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = SetValue(root, WORLD_JSON_NEXT_OBJECT_ID_KEY, &Value);
    }

    JSONCompound* Environment = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreateTableBlock(pool, WorldEnvironment_GetPropertyTable(), &world->Environment, &Environment);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Value = JSONObjectValue_CreateCompound(Environment);
        Result = SetValue(root, WORLD_JSON_ENVIRONMENT_KEY, &Value);
    }

    JSONArray* Objects = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_BorrowArray(pool, &Objects);
    }
    WorldObject* const* Ordered = (WorldObject* const*)order->_data;
    for (size_t Index = 0U; (Index < order->_count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = WriteObject(world, pool, Ordered[Index], Objects);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Value = JSONObjectValue_CreateArray(Objects);
        Result = SetValue(root, WORLD_JSON_OBJECTS_KEY, &Value);
    }
    return Result;
}


// Public functions.
Error WorldFile_ReadJSON(const unsigned char* bytes, size_t byteCount, const WorldFileReadOptions* options,
    World* outWorld)
{
    if (((bytes == NULL) && (byteCount > 0U)) || (options == NULL) || (options->Keys == NULL)
        || (options->MaxTreeByteCount == 0U) || (outWorld == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldFile_ReadJSON: the bytes, the options with their key "
            "registry and a tree limit above 0, and the world are required.");
    }
    if (byteCount > WORLD_FILE_MAX_BYTE_COUNT)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "The world file has %" PRIu64 " bytes; at most %" PRIu64
            " are allowed.", (uint64_t)byteCount, (uint64_t)WORLD_FILE_MAX_BYTE_COUNT);
    }

    // A pool per load: parsing keeps a copy of every key until the pool goes, so a shared one would only grow.
    JSONObjectPool* Pool = NULL;
    Error Result = JSONObjectPool_Create(&Pool);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // A read-only view: the parser never writes through it, but GenericBuffer's storage pointer is not const.
    GenericBuffer Source;
    GenericBuffer_CreateReadOnly(&Source, (void*)bytes, byteCount, sizeof(unsigned char), byteCount);
    JSONObjectValue Root = { .Type = JSONValueType_None };
    JSONReadLimits Limits = { .MaxTreeByteCount = options->MaxTreeByteCount };
    Result = JSON_DeserializeWithLimits(Pool, &Source, &Limits, &Root);
    if ((Result.Code == ErrorCode_Success) && (Root.Type != JSONValueType_Compound))
    {
        Result = Error_Construct2(ErrorCode_InvalidJSON, "A world file must be a JSON object.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        WorldFileLoad Load;
        Result = WorldFileLoad_Begin(&Load, options, outWorld, ErrorCode_InvalidJSON);
        if (Result.Code == ErrorCode_Success)
        {
            Load.JSONPool = Pool;
            Result = ReadWorld(&Load, Root.Value.Compound);
        }
    }

    Error PoolResult = JSONObjectPool_Deconstruct(Pool);
    Error_Deconstruct(&PoolResult);
    return Result;
}

Error WorldFile_WriteJSON(const World* world, GenericBuffer* outBytes)
{
    if ((world == NULL) || (outBytes == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldFile_WriteJSON: world and outBytes must not be NULL.");
    }
    GenericBuffer Order;
    GenericBuffer_AllocateVariable(&Order, World_GetObjectCount(world), sizeof(WorldObject*));
    Error Result = WorldFileInternal_CollectObjects(world, &Order);
    JSONObjectPool* Pool = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_Create(&Pool);
    }
    JSONCompound* Root = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_BorrowCompound(Pool, &Root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WriteRoot(world, Pool, &Order, Root);
    }

    // Serialized after the existing bytes; on failure they are cut back, so nothing is appended.
    size_t StartCount = outBytes->_count;
    if (Result.Code == ErrorCode_Success)
    {
        JSONObjectValue RootValue = JSONObjectValue_CreateCompound(Root);
        Result = JSON_Serialize(&RootValue, outBytes,
            (JSONSerializeFlags)(JSONSerializeFlags_PrettyPrinted | JSONSerializeFlags_InlineScalarArrays));
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AppendByte(outBytes, (unsigned char)'\n'))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not end the world file with a newline.");
    }
    if ((Result.Code != ErrorCode_Success) && (outBytes->_count > StartCount))
    {
        GenericBuffer_SetCount(outBytes, StartCount);
    }

    if (Pool != NULL)
    {
        Error PoolResult = JSONObjectPool_Deconstruct(Pool);
        Error_Deconstruct(&PoolResult);
    }
    Memory_Free(Order._data);
    return Result;
}
