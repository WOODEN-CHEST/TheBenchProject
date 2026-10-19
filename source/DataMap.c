#include <inttypes.h>
#include <stdio.h>
#include "DataMap.h"
#include "DataValueInternal.h"
#include "GameJSON.h"
#include "wr/WRCompile.h"
#include "wr/WRComparator.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRUserData.h"


// Macros.
/** The first capacity a map grows to. */
#define DATA_MAP_FIRST_CAPACITY 4U
/** The type byte an entry counts. */
#define DATA_MAP_TYPE_BYTE ((size_t)1)


// Static functions: entries.
/* The index of @p key, or where it would go (*outIsFound false). */
static size_t FindIndex(const DataMap* map, uint32_t key, bool* outIsFound)
{
    size_t Low = 0U;
    size_t High = (map != NULL) ? map->_count : 0U;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        uint32_t Candidate = map->_entries[Middle].Key;
        if (Candidate == key)
        {
            *outIsFound = true;
            return Middle;
        }
        if (Candidate < key)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    *outIsFound = false;
    return Low;
}

/* Whether the map stays within @p limits after an entry of @p oldBytes (absent when !isReplacing) becomes @p newBytes. */
static Error CheckRoom(const DataMap* map, const DataMapLimits* limits, bool isReplacing, size_t oldBytes,
    size_t newBytes)
{
    if (limits == NULL)
    {
        return Error_CreateSuccess();
    }
    if (!isReplacing && ((size_t)map->_count >= limits->MaxEntryCount))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "The data holds at most %" PRIu64 " entries.",
            (uint64_t)limits->MaxEntryCount);
    }
    if (((map->_byteSize - oldBytes) + newBytes) > limits->MaxByteSize)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "The data takes at most %" PRIu64 " bytes.",
            (uint64_t)limits->MaxByteSize);
    }
    return Error_CreateSuccess();
}

/* Orders entry indices by their key's text (the map is stored inline in the user data). */
static ComparisonResult CompareEntriesByKeyText(GenericBuffer* buffer, GenericBufferElementData a,
    GenericBufferElementData b, const UserData* userData)
{
    UNUSED(buffer);
    const DataMap* Map = NULL;
    Memory_Copy(userData->Bytes, &Map, sizeof(Map));
    uint32_t First = 0U;
    uint32_t Second = 0U;
    Memory_Copy(a._element, &First, sizeof(First));
    Memory_Copy(b._element, &Second, sizeof(Second));
    const unsigned char* FirstText = KeyRegistry_GetText(Map->_keys, Map->_entries[First].Key);
    const unsigned char* SecondText = KeyRegistry_GetText(Map->_keys, Map->_entries[Second].Key);
    size_t Index = 0U;
    while ((FirstText[Index] == SecondText[Index]) && (FirstText[Index] != (unsigned char)0))
    {
        Index++;
    }
    return COMPARE_NUMBER(FirstText[Index], SecondText[Index]);
}

/* Fills @p outOrder (uint32_t, empty) with the entry indices in the order of their keys' text. */
static void SortByKeyText(const DataMap* map, GenericBuffer* outOrder)
{
    for (uint32_t Index = 0U; Index < DataMap_GetCount(map); Index++)
    {
        GenericBuffer_AddLast(outOrder, &Index);
    }
    if (outOrder->_count < 2U)
    {
        return;
    }
    UserData Context = UserData_CreateEmpty();
    Memory_Copy(&map, Context.Bytes, sizeof(map));
    uint32_t Scratch[2];
    GenericBuffer_SortAscending(outOrder, CompareEntriesByKeyText, &Context, Scratch);
}

/* Validates every value before anything is written. */
static Error ValidateValues(const DataMap* map)
{
    for (size_t Index = 0U; Index < DataMap_GetCount(map); Index++)
    {
        const DataMapEntry* Entry = &map->_entries[Index];
        DataPath Path = DataPath_Create();
        const unsigned char* Text = KeyRegistry_GetText(map->_keys, Entry->Key);
        if (Text == NULL)
        {
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "A key of the data is not in its key registry.");
        }
        DataPath_PushName(&Path, Text, StringUTF8_GetByteLength(Text));
        if (DataValue_GetDepth(&Entry->Value) > DATA_VALUE_MAX_DEPTH)
        {
            return DataPath_MakeError(&Path, ErrorCode_ArgumentOutOfRange, "a value nests at most 16 levels deep");
        }
        Error Result = DataValueInternal_Validate(&Entry->Value, map->_keys, &Path);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}


// Static functions: JSON.
/* A JSON array of the tags' keys, in text order. */
static Error CreateTagList(const DataMap* map, const GenericBuffer* order, JSONObjectPool* pool, JSONObjectValue* outJSON)
{
    JSONArray* Array = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &Array);
    for (size_t Index = 0U; (Index < order->_count) && (Result.Code == ErrorCode_Success); Index++)
    {
        const DataMapEntry* Entry = &map->_entries[((const uint32_t*)order->_data)[Index]];
        if (Entry->Value.Type != DataType_Tag)
        {
            continue;
        }
        JSONObjectValue Text;
        Result = GameJSON_CreateStringValue(pool, KeyRegistry_GetText(map->_keys, Entry->Key), &Text);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONArray_Add(Array, &Text);
            if (Result.Code != ErrorCode_Success)
            {
                Error ReturnResult = JSONObjectPool_ReturnValue(pool, &Text);
                Error_Deconstruct(&ReturnResult);
            }
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (Array != NULL)
        {
            Error ReturnResult = JSONObjectPool_ReturnArray(pool, Array, true);
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }
    *outJSON = JSONObjectValue_CreateArray(Array);
    return Error_CreateSuccess();
}

/* A JSON object of the values (every entry that is not a tag), keyed by the keys' text. */
static Error CreateValues(const DataMap* map, const GenericBuffer* order, JSONObjectPool* pool, JSONObjectValue* outJSON)
{
    JSONCompound* Compound = NULL;
    Error Result = JSONObjectPool_BorrowCompound(pool, &Compound);
    for (size_t Index = 0U; (Index < order->_count) && (Result.Code == ErrorCode_Success); Index++)
    {
        const DataMapEntry* Entry = &map->_entries[((const uint32_t*)order->_data)[Index]];
        if (Entry->Value.Type == DataType_Tag)
        {
            continue;
        }
        JSONObjectValue Value;
        Result = DataValueInternal_WriteJSON(&Entry->Value, map->_keys, pool, &Value);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONCompound_Set(Compound, KeyRegistry_GetText(map->_keys, Entry->Key), &Value);
            if (Result.Code != ErrorCode_Success)
            {
                Error ReturnResult = JSONObjectPool_ReturnValue(pool, &Value);
                Error_Deconstruct(&ReturnResult);
            }
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (Compound != NULL)
        {
            Error ReturnResult = JSONObjectPool_ReturnCompound(pool, Compound, true);
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }
    *outJSON = JSONObjectValue_CreateCompound(Compound);
    return Error_CreateSuccess();
}

/* Sets @p key of @p parent to @p value (returned to the pool on failure). */
static Error SetPart(JSONObjectPool* pool, JSONCompound* parent, const unsigned char* key, JSONObjectValue* value)
{
    Error Result = JSONCompound_Set(parent, key, value);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(pool, value);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

/* What every entry a reader adds needs: the map (created by the first entry), the registry, the limits. */
typedef struct MapReadStruct
{
    DataMap* Map;
    KeyRegistry* Keys;
    const DataMapLimits* Limits;
    DataPath* Path;
    ErrorCode FailureCode;
} MapRead;

/* Interns a key read from a file; refuses one the map already holds. */
static Error InternNewKey(MapRead* read, const unsigned char* text, size_t byteCount, const unsigned char* defaultNamespace,
    bool isFullFormRequired, uint32_t* outKey)
{
    Error Result = Error_CreateSuccess();
    if (isFullFormRequired)
    {
        // A GHDF name is terminated (its count includes the NUL), so it can be checked as a full key.
        Result = NamespacedKey_Validate(text);
        if (Result.Code == ErrorCode_Success)
        {
            Result = KeyRegistry_Intern(read->Keys, text, outKey);
        }
    }
    else
    {
        Result = KeyRegistry_InternText(read->Keys, text, byteCount, defaultNamespace, outKey);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return DataPath_WrapError(read->Path, read->FailureCode, &Result);
    }
    if (DataMap_Find(read->Map, *outKey) != NULL)
    {
        return DataPath_MakeError(read->Path, read->FailureCode, "the key appears twice");
    }
    return Error_CreateSuccess();
}

/* The budget for the value of the entry @p key (DataValueInternal_ReadJSON): the bytes the limits leave after the map
 * and the entry's key, in @p storage; NULL (no budget) without limits. */
static DataBudget* GetValueBudget(const MapRead* read, uint32_t key, DataBudget* storage)
{
    if (read->Limits == NULL)
    {
        return NULL;
    }
    size_t Used = DataMap_GetByteSize(read->Map) + DataMap_GetEntryByteSize(read->Keys, key, NULL);
    storage->Limit = read->Limits->MaxByteSize;
    storage->Remaining = (Used < storage->Limit) ? (storage->Limit - Used) : 0U;
    return storage;
}

/* Moves a read value into the map under @p key, creating the map first. The value is validated again, as a last check
 * behind the readers' own, so a map never holds a value DataValue_Validate refuses. */
static Error AddEntry(MapRead* read, uint32_t key, DataValue* value)
{
    Error Result = DataValueInternal_CheckRead(value, read->Keys, read->Path, read->FailureCode);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (read->Map == NULL)
    {
        Result = DataMap_Create(read->Keys, &read->Map);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataMap_Adopt(read->Map, key, value, read->Limits);
    }
    return (Result.Code == ErrorCode_Success) ? Result : DataPath_WrapError(read->Path, read->FailureCode, &Result);
}

static Error ReadTagList(MapRead* read, const JSONObjectValue* json, const unsigned char* defaultNamespace)
{
    if (json->Type != JSONValueType_Array)
    {
        return DataPath_MakeError(read->Path, ErrorCode_InvalidJSON, "the tags must be a list of keys");
    }
    size_t Count = JSONArray_GetElementCount(json->Value.Array);
    if ((read->Limits != NULL) && (Count > read->Limits->MaxEntryCount))
    {
        return DataPath_MakeError(read->Path, ErrorCode_InvalidJSON, "there are more tags than the data may hold");
    }
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        DataPath_PushIndex(read->Path, Index);
        JSONObjectValue Element;
        Result = JSONArray_Get(json->Value.Array, Index, &Element);
        if ((Result.Code == ErrorCode_Success) && ((Element.Type != JSONValueType_String) || (Element.Value.String == NULL)))
        {
            Result = DataPath_MakeError(read->Path, ErrorCode_InvalidJSON, "a tag must be a key, as text");
        }
        uint32_t Key = NAMESPACED_KEY_ID_NONE;
        if (Result.Code == ErrorCode_Success)
        {
            Result = InternNewKey(read, Element.Value.String->_data, Element.Value.String->_count, defaultNamespace,
                false, &Key);
        }
        if (Result.Code == ErrorCode_Success)
        {
            DataValue Tag = DataValue_CreateTag();
            Result = AddEntry(read, Key, &Tag);
        }
        DataPath_Pop(read->Path);
    }
    return Result;
}

static Error ReadValueEntries(MapRead* read, const JSONObjectValue* json, const unsigned char* defaultNamespace)
{
    if (json->Type != JSONValueType_Compound)
    {
        return DataPath_MakeError(read->Path, ErrorCode_InvalidJSON, "the data must be an object of keys to values");
    }
    if ((read->Limits != NULL) && (JSONCompound_GetEntryCount(json->Value.Compound) > read->Limits->MaxEntryCount))
    {
        return DataPath_MakeError(read->Path, ErrorCode_InvalidJSON, "there are more entries than the data may hold");
    }
    DataEnumeratorStorage Storage;
    CollectionEnumerator* Enumerator = DataValueInternal_BeginEnumeration(
        JSONCompound_GetEntryCollection(json->Value.Compound), &Storage);
    if (Enumerator == NULL)
    {
        return DataPath_MakeError(read->Path, ErrorCode_InvalidJSON, "the data's entries could not be enumerated");
    }
    bool HasNext = false;
    Error Result = Error_CreateSuccess();
    while (Result.Code == ErrorCode_Success)
    {
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code != ErrorCode_Success) || !HasNext)
        {
            break;
        }
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        size_t KeyLength = StringUTF8_GetByteLength(Entry.Key);
        DataPath_PushName(read->Path, Entry.Key, KeyLength);
        uint32_t Key = NAMESPACED_KEY_ID_NONE;
        DataValue Value = DataValue_CreateTag();
        Result = InternNewKey(read, Entry.Key, KeyLength, defaultNamespace, false, &Key);
        if (Result.Code == ErrorCode_Success)
        {
            DataBudget Budget = { .Remaining = 0U, .Limit = 0U };
            Result = DataValueInternal_ReadJSON(&Entry.Value, read->Keys, defaultNamespace, 1U, read->Path,
                GetValueBudget(read, Key, &Budget), &Value);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = AddEntry(read, Key, &Value);
        }
        DataValue_Deconstruct(&Value);
        DataPath_Pop(read->Path);
    }
    DataValueInternal_EndEnumeration(Enumerator, &Storage);
    return ((Result.Code == ErrorCode_Success) || (Result.Code == ErrorCode_InvalidJSON)) ? Result
        : DataPath_WrapError(read->Path, ErrorCode_InvalidJSON, &Result);
}


// Public functions: lifetime.
Error DataMap_Create(KeyRegistry* keys, DataMap** outMap)
{
    if ((keys == NULL) || (outMap == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataMap_Create: keys and outMap must not be NULL.");
    }
    DataMap* Map = Memory_Allocate(sizeof(DataMap));
    *Map = (DataMap){ ._keys = keys, ._entries = NULL, ._count = 0U, ._capacity = 0U, ._byteSize = 0U };
    *outMap = Map;
    return Error_CreateSuccess();
}

void DataMap_Destroy(DataMap* map)
{
    if (map == NULL)
    {
        return;
    }
    for (uint32_t Index = 0U; Index < map->_count; Index++)
    {
        DataValue_Deconstruct(&map->_entries[Index].Value);
    }
    Memory_Free(map->_entries);
    Memory_Free(map);
}

DataMap* DataMap_Clone(const DataMap* source)
{
    if (DataMap_GetCount(source) == 0U)
    {
        return NULL;
    }
    DataMap* Copy = Memory_Allocate(sizeof(DataMap));
    *Copy = (DataMap){ ._keys = source->_keys, ._count = source->_count, ._capacity = source->_count,
        ._byteSize = source->_byteSize };
    Copy->_entries = Memory_Allocate(source->_count * sizeof(DataMapEntry));
    for (uint32_t Index = 0U; Index < source->_count; Index++)
    {
        Copy->_entries[Index].Key = source->_entries[Index].Key;
        Copy->_entries[Index].Value = DataValue_Clone(&source->_entries[Index].Value);
    }
    return Copy;
}


// Public functions: reading.
const DataMapEntry* DataMap_GetEntry(const DataMap* map, size_t index)
{
    return (index < DataMap_GetCount(map)) ? &map->_entries[index] : NULL;
}

const DataValue* DataMap_Find(const DataMap* map, uint32_t key)
{
    bool IsFound = false;
    size_t Index = FindIndex(map, key, &IsFound);
    return IsFound ? &map->_entries[Index].Value : NULL;
}

bool DataMap_HasTag(const DataMap* map, uint32_t key)
{
    const DataValue* Value = DataMap_Find(map, key);
    return (Value != NULL) && (Value->Type == DataType_Tag);
}

bool DataMap_Equals(const DataMap* a, const DataMap* b)
{
    if (DataMap_GetCount(a) != DataMap_GetCount(b))
    {
        return false;
    }
    for (size_t Index = 0U; Index < DataMap_GetCount(a); Index++)
    {
        if ((a->_entries[Index].Key != b->_entries[Index].Key)
            || !DataValue_Equals(&a->_entries[Index].Value, &b->_entries[Index].Value))
        {
            return false;
        }
    }
    return true;
}

size_t DataMap_GetEntryByteSize(const KeyRegistry* keys, uint32_t key, const DataValue* value)
{
    const unsigned char* Text = KeyRegistry_GetText(keys, key);
    size_t KeyBytes = (Text != NULL) ? StringUTF8_GetByteLength(Text) : NAMESPACED_KEY_MAX_LENGTH;
    return KeyBytes + DATA_MAP_TYPE_BYTE + DataValue_GetByteSize(value, keys);
}

Error DataMap_CheckLimits(const DataMap* map, const DataMapLimits* limits)
{
    if (limits == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataMap_CheckLimits: limits must not be NULL.");
    }
    if (DataMap_GetCount(map) > limits->MaxEntryCount)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "The data holds at most %" PRIu64 " entries.",
            (uint64_t)limits->MaxEntryCount);
    }
    if (DataMap_GetByteSize(map) > limits->MaxByteSize)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "The data takes at most %" PRIu64 " bytes.",
            (uint64_t)limits->MaxByteSize);
    }
    return Error_CreateSuccess();
}


// Public functions: changing.
Error DataMap_Set(DataMap* map, uint32_t key, const DataValue* value, const DataMapLimits* limits)
{
    if ((map == NULL) || (value == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataMap_Set: map and value must not be NULL.");
    }
    Error Result = DataValue_Validate(value, map->_keys);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    DataValue Copy = DataValue_Clone(value);
    Result = DataMap_Adopt(map, key, &Copy, limits);
    DataValue_Deconstruct(&Copy);
    return Result;
}

Error DataMap_Adopt(DataMap* map, uint32_t key, DataValue* value, const DataMapLimits* limits)
{
    if ((map == NULL) || (value == NULL) || (KeyRegistry_GetText(map->_keys, key) == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataMap_Adopt: map and value must not be NULL, and key must be an id of the map's key registry.");
    }
    bool IsFound = false;
    size_t Index = FindIndex(map, key, &IsFound);
    size_t OldBytes = IsFound ? DataMap_GetEntryByteSize(map->_keys, key, &map->_entries[Index].Value) : 0U;
    size_t NewBytes = DataMap_GetEntryByteSize(map->_keys, key, value);
    Error Result = CheckRoom(map, limits, IsFound, OldBytes, NewBytes);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (IsFound)
    {
        DataValue_Deconstruct(&map->_entries[Index].Value);
        map->_entries[Index].Value = *value;
    }
    else
    {
        if (map->_count == map->_capacity)
        {
            size_t Start = (map->_capacity == 0U) ? DATA_MAP_FIRST_CAPACITY : map->_capacity;
            size_t Capacity = 0U;
            if ((map->_count == UINT32_MAX)
                || !Memory_TryGrowCapacity(Start, (size_t)map->_count + 1U, 2U, sizeof(DataMapEntry), &Capacity))
            {
                return Error_Construct2(ErrorCode_InvalidOperation, "DataMap_Adopt: the map cannot hold more entries.");
            }
            Capacity = (Capacity > UINT32_MAX) ? UINT32_MAX : Capacity;
            map->_entries = Memory_Reallocate(map->_entries, Capacity * sizeof(DataMapEntry));
            map->_capacity = (uint32_t)Capacity;
        }
        size_t After = map->_count - Index;
        if (After > 0U)
        {
            Memory_Move(&map->_entries[Index], &map->_entries[Index + 1U], After * sizeof(DataMapEntry));
        }
        map->_entries[Index] = (DataMapEntry){ .Key = key, .Value = *value };
        map->_count++;
    }
    map->_byteSize = (map->_byteSize - OldBytes) + NewBytes;
    *value = DataValue_CreateTag();
    return Error_CreateSuccess();
}

bool DataMap_Remove(DataMap* map, uint32_t key)
{
    bool IsFound = false;
    size_t Index = FindIndex(map, key, &IsFound);
    if (!IsFound)
    {
        return false;
    }
    map->_byteSize -= DataMap_GetEntryByteSize(map->_keys, key, &map->_entries[Index].Value);
    DataValue_Deconstruct(&map->_entries[Index].Value);
    size_t After = map->_count - Index - 1U;
    if (After > 0U)
    {
        Memory_Move(&map->_entries[Index + 1U], &map->_entries[Index], After * sizeof(DataMapEntry));
    }
    map->_count--;
    return true;
}


// Public functions: files.
Error DataMap_WriteJSON(const DataMap* map, JSONObjectPool* pool, JSONCompound* parent, const unsigned char* tagsKey,
    const unsigned char* valuesKey, bool areEmptyPartsWritten)
{
    if ((pool == NULL) || (parent == NULL) || (tagsKey == NULL) || (valuesKey == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataMap_WriteJSON: pool, parent, tagsKey and valuesKey must not be NULL.");
    }
    Error Result = ValidateValues(map);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t TagCount = 0U;
    for (size_t Index = 0U; Index < DataMap_GetCount(map); Index++)
    {
        TagCount += (map->_entries[Index].Value.Type == DataType_Tag) ? 1U : 0U;
    }
    size_t ValueCount = DataMap_GetCount(map) - TagCount;
    GenericBuffer Order;
    GenericBuffer_AllocateVariable(&Order, DataMap_GetCount(map), sizeof(uint32_t));
    SortByKeyText(map, &Order);

    JSONObjectValue Part;
    if ((TagCount > 0U) || areEmptyPartsWritten)
    {
        Result = CreateTagList(map, &Order, pool, &Part);
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetPart(pool, parent, tagsKey, &Part);
        }
    }
    if ((Result.Code == ErrorCode_Success) && ((ValueCount > 0U) || areEmptyPartsWritten))
    {
        Result = CreateValues(map, &Order, pool, &Part);
        if (Result.Code == ErrorCode_Success)
        {
            Result = SetPart(pool, parent, valuesKey, &Part);
        }
    }
    Memory_Free(Order._data);
    return Result;
}

Error DataMap_ReadJSON(JSONCompound* parent, const unsigned char* tagsKey, const unsigned char* valuesKey,
    KeyRegistry* keys, const unsigned char* defaultNamespace, const DataMapLimits* limits, DataMap** outMap,
    size_t* outFoundKeyCount)
{
    if (outMap != NULL)
    {
        *outMap = NULL;
    }
    if (outFoundKeyCount != NULL)
    {
        *outFoundKeyCount = 0U;
    }
    if ((parent == NULL) || (tagsKey == NULL) || (valuesKey == NULL) || (keys == NULL) || (outMap == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataMap_ReadJSON: parent, tagsKey, valuesKey, keys and outMap must not be NULL.");
    }

    JSONObjectValue Tags;
    JSONObjectValue Values;
    bool HasTags = false;
    bool HasValues = false;
    Error Result = JSONCompound_GetOptional(parent, tagsKey, &Tags, &HasTags);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(parent, valuesKey, &Values, &HasValues);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outFoundKeyCount != NULL)
    {
        *outFoundKeyCount = (HasTags ? 1U : 0U) + (HasValues ? 1U : 0U);
    }

    // All or nothing: a refused map forgets the keys it interned.
    size_t KeyCheckpoint = KeyRegistry_GetCount(keys);
    DataPath Path = DataPath_Create();
    MapRead Read = { .Map = NULL, .Keys = keys, .Limits = limits, .Path = &Path, .FailureCode = ErrorCode_InvalidJSON };
    if (HasTags)
    {
        DataPath_PushName(&Path, tagsKey, StringUTF8_GetByteLength(tagsKey));
        Result = ReadTagList(&Read, &Tags, defaultNamespace);
        DataPath_Pop(&Path);
    }
    if ((Result.Code == ErrorCode_Success) && HasValues)
    {
        DataPath_PushName(&Path, valuesKey, StringUTF8_GetByteLength(valuesKey));
        Result = ReadValueEntries(&Read, &Values, defaultNamespace);
        DataPath_Pop(&Path);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DataMap_Destroy(Read.Map);
        KeyRegistry_TruncateTo(keys, KeyCheckpoint);
        return Result;
    }
    if (DataMap_GetCount(Read.Map) == 0U)
    {
        DataMap_Destroy(Read.Map);
        Read.Map = NULL;
    }
    *outMap = Read.Map;
    return Error_CreateSuccess();
}

Error DataMap_WriteGHDF(const DataMap* map, GHDFObjectPool* pool, GHDFArray** outArray)
{
    if ((pool == NULL) || (outArray == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataMap_WriteGHDF: pool and outArray must not be NULL.");
    }
    *outArray = NULL;
    Error Result = ValidateValues(map);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    GenericBuffer Order;
    GenericBuffer_AllocateVariable(&Order, DataMap_GetCount(map), sizeof(uint32_t));
    SortByKeyText(map, &Order);
    GHDFArray* Array = NULL;
    Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_Compound, &Array);
    for (size_t Index = 0U; (Index < Order._count) && (Result.Code == ErrorCode_Success); Index++)
    {
        const DataMapEntry* Entry = &map->_entries[((const uint32_t*)Order._data)[Index]];
        GHDFCompound* Compound = NULL;
        Result = GHDFObjectPool_BorrowCompound(pool, &Compound);
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValueInternal_PutGHDFString(pool, Compound, DATA_GHDF_NAME,
                KeyRegistry_GetText(map->_keys, Entry->Key));
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValueInternal_WriteGHDF(&Entry->Value, map->_keys, pool, Compound);
        }
        if (Result.Code == ErrorCode_Success)
        {
            GHDFObjectValue Element = GHDFObjectValue_CreateCompound(Compound);
            Result = GHDFArray_AddValue(Array, &Element);
        }
        if ((Result.Code != ErrorCode_Success) && (Compound != NULL))
        {
            Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, Compound, true);
            Error_Deconstruct(&ReturnResult);
        }
    }
    Memory_Free(Order._data);
    if (Result.Code != ErrorCode_Success)
    {
        if (Array != NULL)
        {
            Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }
    *outArray = Array;
    return Error_CreateSuccess();
}

Error DataMap_ReadGHDF(GHDFArray* array, KeyRegistry* keys, const DataMapLimits* limits, DataMap** outMap)
{
    if (outMap != NULL)
    {
        *outMap = NULL;
    }
    if ((array == NULL) || (keys == NULL) || (outMap == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "DataMap_ReadGHDF: array, keys and outMap must not be NULL.");
    }
    size_t Count = GHDFArray_GetElementCount(array);
    if ((Count > 0U) && (GHDFArray_GetElementType(array) != GHDFValueType_Compound))
    {
        return Error_Construct2(ErrorCode_Deserialize, "The data must be a Compound array of entries.");
    }
    if ((limits != NULL) && (Count > limits->MaxEntryCount))
    {
        return Error_Construct2(ErrorCode_Deserialize, "There are more entries than the data may hold.");
    }

    // All or nothing: a refused map forgets the keys it interned.
    size_t KeyCheckpoint = KeyRegistry_GetCount(keys);
    DataPath Path = DataPath_Create();
    MapRead Read = { .Map = NULL, .Keys = keys, .Limits = limits, .Path = &Path, .FailureCode = ErrorCode_Deserialize };
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Element = { .Type = GHDFValueType_None };
        const unsigned char* Name = NULL;
        size_t NameLength = 0U;
        Result = GHDFArray_Get(array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValueInternal_GetGHDFName(Element.Value.Compound, &Name, &NameLength);
        }
        if (Result.Code != ErrorCode_Success)
        {
            DataPath_PushIndex(&Path, Index);
            Result = DataPath_WrapError(&Path, ErrorCode_Deserialize, &Result);
            DataPath_Pop(&Path);
            break;
        }
        DataPath_PushName(&Path, Name, NameLength);
        uint32_t Key = NAMESPACED_KEY_ID_NONE;
        DataValue Value = DataValue_CreateTag();
        Result = InternNewKey(&Read, Name, NameLength, NULL, true, &Key);
        if (Result.Code == ErrorCode_Success)
        {
            DataBudget Budget = { .Remaining = 0U, .Limit = 0U };
            Result = DataValueInternal_ReadGHDF(Element.Value.Compound, keys, 1U, &Path,
                GetValueBudget(&Read, Key, &Budget), &Value);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = AddEntry(&Read, Key, &Value);
        }
        DataValue_Deconstruct(&Value);
        DataPath_Pop(&Path);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DataMap_Destroy(Read.Map);
        KeyRegistry_TruncateTo(keys, KeyCheckpoint);
        return Result;
    }
    *outMap = Read.Map;
    return Error_CreateSuccess();
}
