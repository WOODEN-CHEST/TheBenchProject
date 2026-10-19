// JSON reading and writing of data values (see DataValue.h for the forms).

#include <stdio.h>
#include "DataValueInternal.h"
#include "GameCalendar.h"
#include "GameJSON.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** Room for a problem text that names a type or a wrapper. */
#define JSON_PROBLEM_CAPACITY ((size_t)256)
/** Room for a date-time's text and its terminator. */
#define JSON_DATE_TIME_TEXT_CAPACITY (GAME_CALENDAR_DATE_TIME_TEXT_MAX_LENGTH + 1U)


// Types.
/* The wrappers: a JSON object with this one entry holds a value of a type JSON has no form for. */
typedef enum JSONWrapperEnum
{
    JSONWrapper_Key,
    JSONWrapper_Object,
    JSONWrapper_Vector3,
    JSONWrapper_Color,
    JSONWrapper_DateTime,
    JSONWrapper_Float,
    JSONWrapper_List,
    JSONWrapper_Count
} JSONWrapper;

/* What a read needs at every level. */
typedef struct JSONReadStruct
{
    KeyRegistry* Keys;
    const unsigned char* DefaultNamespace;
    DataPath* Path;
    /* The bytes the value may still count, or NULL for no budget. */
    DataBudget* Budget;
} JSONRead;


// Fields.
static const unsigned char* const WrapperNames[JSONWrapper_Count] =
{
    u8"$key", u8"$object", u8"$vector3", u8"$color", u8"$date_time", u8"$float", u8"$list",
};


// Static functions: reading helpers.
static Error ReadValue(JSONRead* read, const JSONObjectValue* json, size_t level, DataValue* outValue);

static Error PathError(const JSONRead* read, const char* problem)
{
    return DataPath_MakeError(read->Path, ErrorCode_InvalidJSON, problem);
}

static Error Charge(const JSONRead* read, size_t bytes)
{
    return DataValueInternal_Charge(read->Budget, bytes, read->Path, ErrorCode_InvalidJSON);
}

/* "<prefix>: <inner message>" at the read's path, as InvalidJSON; deconstructs @p inner. */
static Error PathErrorWithCause(const JSONRead* read, const char* prefix, Error* inner)
{
    Error Prefixed = Error_Construct4(ErrorCode_InvalidJSON, "%s: %s", prefix,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid value.");
    Error_Deconstruct(inner);
    return DataPath_WrapError(read->Path, ErrorCode_InvalidJSON, &Prefixed);
}

static bool TryFindWrapper(const unsigned char* name, JSONWrapper* outWrapper)
{
    for (int Index = 0; Index < (int)JSONWrapper_Count; Index++)
    {
        if (StringUTF8_GetByteLength(WrapperNames[Index]) == StringUTF8_GetByteLength(name)
            && Memory_IsEqual(WrapperNames[Index], name, StringUTF8_GetByteLength(name)))
        {
            *outWrapper = (JSONWrapper)Index;
            return true;
        }
    }
    return false;
}

/* The only entry of a compound holding one. */
static bool TryGetOnlyEntry(JSONCompound* compound, JSONCompoundEntry* outEntry)
{
    DataEnumeratorStorage Storage;
    CollectionEnumerator* Enumerator = DataValueInternal_BeginEnumeration(JSONCompound_GetEntryCollection(compound),
        &Storage);
    bool HasNext = false;
    bool IsFound = false;
    if (Enumerator != NULL)
    {
        Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code == ErrorCode_Success) && HasNext)
        {
            Result = CollectionEnumerator_NextByValue(Enumerator, outEntry);
            IsFound = Result.Code == ErrorCode_Success;
        }
        Error_Deconstruct(&Result);
    }
    DataValueInternal_EndEnumeration(Enumerator, &Storage);
    return IsFound;
}

static Error ReadKeyText(const JSONRead* read, const JSONObjectValue* json, const char* what, DataValue* outValue)
{
    if ((json->Type != JSONValueType_String) || (json->Value.String == NULL))
    {
        char Problem[JSON_PROBLEM_CAPACITY];
        snprintf(Problem, sizeof(Problem), "%s must be a namespaced key, as text (\"town:street_lamp\")", what);
        return PathError(read, Problem);
    }
    uint32_t Key = NAMESPACED_KEY_ID_NONE;
    Error Result = KeyRegistry_InternText(read->Keys, json->Value.String->_data, json->Value.String->_count,
        read->DefaultNamespace, &Key);
    if (Result.Code != ErrorCode_Success)
    {
        return PathErrorWithCause(read, what, &Result);
    }
    *outValue = DataValue_FromKey(Key);
    return Error_CreateSuccess();
}


// Static functions: wrappers.
static Error ReadWrapperText(const JSONRead* read, const JSONObjectValue* json, JSONWrapper wrapper, DataValue* outValue)
{
    const char* Name = (const char*)WrapperNames[wrapper];
    if ((json->Type != JSONValueType_String) || (json->Value.String == NULL))
    {
        char Problem[JSON_PROBLEM_CAPACITY];
        snprintf(Problem, sizeof(Problem), "%s must hold text", Name);
        return PathError(read, Problem);
    }
    const unsigned char* Text = json->Value.String->_data;
    size_t ByteCount = json->Value.String->_count;
    if (wrapper == JSONWrapper_DateTime)
    {
        int64_t DateTime = 0;
        if (!GameCalendar_TryParseDateTime(Text, ByteCount, &DateTime))
        {
            return PathError(read, "$date_time must be a date and time \"yyyy-mm-ddThh:mm:ss\" (with up to six digits "
                "of fractional seconds) in the years 1 to 9999");
        }
        *outValue = DataValue_FromDateTime(DateTime);
        return Error_CreateSuccess();
    }

    DataType ElementType = DataType_Tag;
    if (!DataType_TryParseName(Text, ByteCount, &ElementType) || (ElementType == DataType_Tag))
    {
        return PathError(read, "$list must name its element type: bool, int, float, string, key, object, vector3, "
            "color, date_time, list or compound");
    }
    Error Result = Charge(read, 1U); // an empty list's own byte (DataValue_GetByteSize)
    return (Result.Code == ErrorCode_Success) ? DataValue_CreateList(ElementType, outValue) : Result;
}

static Error ReadWrapper(JSONRead* read, const unsigned char* name, const JSONObjectValue* json, DataValue* outValue)
{
    JSONWrapper Wrapper = JSONWrapper_Key;
    if (!TryFindWrapper(name, &Wrapper))
    {
        return PathError(read, "an object with one entry starting with '$' is a wrapper, and this one is unknown (the "
            "wrappers are $key, $object, $vector3, $color, $date_time, $float and $list)");
    }

    Error Result = Error_CreateSuccess();
    switch (Wrapper)
    {
        case JSONWrapper_Key:
            return ReadKeyText(read, json, "$key", outValue);
        case JSONWrapper_DateTime:
        case JSONWrapper_List:
            return ReadWrapperText(read, json, Wrapper, outValue);
        case JSONWrapper_Object:
            *outValue = DataValue_FromObject(0U);
            Result = GameJSON_ValueToUInt64(json, &outValue->ObjectValue);
            break;
        case JSONWrapper_Vector3:
            *outValue = DataValue_FromVector3((Vector3){ 0.0f, 0.0f, 0.0f });
            Result = GameJSON_ParseVector3(json, &outValue->Vector3Value);
            break;
        case JSONWrapper_Color:
            *outValue = DataValue_FromColor((Color){ 0, 0, 0, 0 });
            Result = GameJSON_ParseColor(json, &outValue->ColorValue);
            break;
        case JSONWrapper_Float:
            *outValue = DataValue_FromFloat(0.0);
            Result = GameJSON_ValueToDouble(json, &outValue->FloatValue);
            break;
        case JSONWrapper_Count:
            break;
    }
    if (Result.Code != ErrorCode_Success)
    {
        *outValue = DataValue_CreateTag();
        return PathErrorWithCause(read, (const char*)WrapperNames[Wrapper], &Result);
    }
    return Error_CreateSuccess();
}


// Static functions: containers.
/* The name of a type for "the list holds int values": the type's file name. */
static Error MakeElementTypeError(const JSONRead* read, DataType found, DataType expected)
{
    char Problem[JSON_PROBLEM_CAPACITY];
    const char* Hint = ((found == DataType_Int) && (expected == DataType_Float))
        ? " (write a float with a fraction, like 2.0)" : "";
    snprintf(Problem, sizeof(Problem), "the element's type is %s, but the list holds %s values (its first element's type)%s",
        (const char*)DataType_GetName(found), (const char*)DataType_GetName(expected), Hint);
    return PathError(read, Problem);
}

static Error ReadArray(JSONRead* read, JSONArray* array, size_t level, DataValue* outValue)
{
    size_t Count = JSONArray_GetElementCount(array);
    if (Count == 0U)
    {
        return PathError(read, "an empty list needs its element type: write {\"$list\": \"int\"} (or the type it holds)");
    }
    if (Count > DATA_VALUE_MAX_ELEMENT_COUNT)
    {
        return PathError(read, "a list holds at most 4096 elements");
    }
    Error ChargeResult = Charge(read, 1U); // the list's own byte (DataValue_GetByteSize)
    if (ChargeResult.Code != ErrorCode_Success)
    {
        return ChargeResult;
    }

    DataValue List = DataValue_CreateTag();
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue ElementJSON;
        Error Result = JSONArray_Get(array, Index, &ElementJSON);
        DataValue Element = DataValue_CreateTag();
        DataPath_PushIndex(read->Path, Index);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadValue(read, &ElementJSON, level + 1U, &Element);
        }
        if ((Result.Code == ErrorCode_Success) && (Index == 0U))
        {
            Result = DataValue_CreateList(Element.Type, &List);
        }
        if ((Result.Code == ErrorCode_Success) && (Element.Type != DataValue_GetListElementType(&List)))
        {
            Result = MakeElementTypeError(read, Element.Type, DataValue_GetListElementType(&List));
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValue_ListAdd(&List, &Element);
        }
        if ((Result.Code != ErrorCode_Success) && (Result.Code != ErrorCode_InvalidJSON))
        {
            Result = DataPath_WrapError(read->Path, ErrorCode_InvalidJSON, &Result);
        }
        DataPath_Pop(read->Path);
        if (Result.Code != ErrorCode_Success)
        {
            DataValue_Deconstruct(&Element);
            DataValue_Deconstruct(&List);
            return Result;
        }
    }
    *outValue = List;
    return Error_CreateSuccess();
}

/* One entry of a JSON object that is a compound: checks the name, reads the value, moves it in. */
static Error ReadCompoundEntry(JSONRead* read, const JSONCompoundEntry* entry, size_t level, DataValue* compound)
{
    size_t NameLength = StringUTF8_GetByteLength(entry->Key);
    if (!DataValueInternal_IsNameValid(entry->Key, NameLength))
    {
        return PathError(read, (entry->Key[0] == (unsigned char)'$')
            ? "names starting with '$' are reserved: \"$type\" is the only such entry, and a wrapper like \"$key\" must "
              "be its object's only entry"
            : "an entry's name must be 1 to 256 bytes of UTF-8");
    }

    DataValue Value = DataValue_CreateTag();
    Error Result = Charge(read, NameLength + 1U); // the entry's name and type byte (DataValue_GetByteSize)
    if (Result.Code == ErrorCode_Success)
    {
        bool IsTypeEntry = DataValueInternal_IsTypeEntryName(entry->Key, NameLength);
        Result = IsTypeEntry ? ReadKeyText(read, &entry->Value, "$type", &Value)
            : ReadValue(read, &entry->Value, level + 1U, &Value);
        if ((Result.Code == ErrorCode_Success) && IsTypeEntry)
        {
            Result = Charge(read, DataValue_GetByteSize(&Value, read->Keys));
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataValue_CompoundSet(compound, entry->Key, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            Result = DataPath_WrapError(read->Path, ErrorCode_InvalidJSON, &Result);
        }
    }
    DataValue_Deconstruct(&Value);
    return Result;
}

static Error ReadCompound(JSONRead* read, JSONCompound* json, size_t level, DataValue* outValue)
{
    size_t Count = JSONCompound_GetEntryCount(json);
    if (Count == 1U)
    {
        JSONCompoundEntry Only;
        if (TryGetOnlyEntry(json, &Only) && (Only.Key[0] == (unsigned char)'$')
            && !DataValueInternal_IsTypeEntryName(Only.Key, StringUTF8_GetByteLength(Only.Key)))
        {
            return ReadWrapper(read, Only.Key, &Only.Value, outValue);
        }
    }
    if (Count > DATA_VALUE_MAX_ELEMENT_COUNT)
    {
        return PathError(read, "a compound holds at most 4096 entries");
    }

    DataValue Compound = DataValue_CreateTag();
    Error Result = Charge(read, 1U); // the compound's own byte (DataValue_GetByteSize)
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataValue_CreateCompound(&Compound);
    }
    DataEnumeratorStorage Storage;
    CollectionEnumerator* Enumerator = DataValueInternal_BeginEnumeration(JSONCompound_GetEntryCollection(json),
        &Storage);
    if ((Result.Code == ErrorCode_Success) && (Enumerator == NULL))
    {
        Result = PathError(read, "the object's entries could not be enumerated");
    }
    bool HasNext = false;
    while (Result.Code == ErrorCode_Success)
    {
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code != ErrorCode_Success) || !HasNext)
        {
            break;
        }
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            DataPath_PushName(read->Path, Entry.Key, StringUTF8_GetByteLength(Entry.Key));
            Result = ReadCompoundEntry(read, &Entry, level, &Compound);
            DataPath_Pop(read->Path);
        }
    }
    DataValueInternal_EndEnumeration(Enumerator, &Storage);
    if (Result.Code != ErrorCode_Success)
    {
        DataValue_Deconstruct(&Compound);
        return (Result.Code == ErrorCode_InvalidJSON) ? Result
            : DataPath_WrapError(read->Path, ErrorCode_InvalidJSON, &Result);
    }
    *outValue = Compound;
    return Error_CreateSuccess();
}

/* Reads any value; lists and compounds charge themselves as they are read, the others are charged by ReadValue. */
static Error ReadAnyValue(JSONRead* read, const JSONObjectValue* json, size_t level, DataValue* outValue)
{
    Error Result = Error_CreateSuccess();
    switch (json->Type)
    {
        case JSONValueType_Boolean:
            *outValue = DataValue_FromBool(json->Value.Boolean);
            return Error_CreateSuccess();
        case JSONValueType_Integer:
            *outValue = DataValue_FromInt(json->Value.Integer);
            return Error_CreateSuccess();
        case JSONValueType_RealNumber:
            if (Math_IsNaNDouble(json->Value.RealNumber) || Math_IsInfinityDouble(json->Value.RealNumber))
            {
                return PathError(read, "a float must be finite");
            }
            *outValue = DataValue_FromFloat(json->Value.RealNumber);
            return Error_CreateSuccess();
        case JSONValueType_String:
            Result = DataValue_CreateStringFromBytes((json->Value.String != NULL) ? json->Value.String->_data : NULL,
                (json->Value.String != NULL) ? json->Value.String->_count : 0U, outValue);
            return (Result.Code == ErrorCode_Success) ? Result
                : DataPath_WrapError(read->Path, ErrorCode_InvalidJSON, &Result);
        case JSONValueType_Null:
            return PathError(read, "null is not a value (leave the entry out instead)");
        case JSONValueType_Compound:
            return ReadCompound(read, json->Value.Compound, level, outValue);
        case JSONValueType_Array:
            return ReadArray(read, json->Value.Array, level, outValue);
        case JSONValueType_None:
            break;
    }
    return PathError(read, "expected a value");
}

static Error ReadValue(JSONRead* read, const JSONObjectValue* json, size_t level, DataValue* outValue)
{
    *outValue = DataValue_CreateTag();
    if (level > DATA_VALUE_MAX_DEPTH)
    {
        return PathError(read, "a value nests at most 16 levels deep");
    }
    Error Result = ReadAnyValue(read, json, level, outValue);
    if ((Result.Code == ErrorCode_Success) && (outValue->Type != DataType_List) && (outValue->Type != DataType_Compound))
    {
        Result = Charge(read, DataValue_GetByteSize(outValue, read->Keys));
        if (Result.Code != ErrorCode_Success)
        {
            DataValue_Deconstruct(outValue);
        }
    }
    return Result;
}


// Static functions: writing.
/* {"<name>": inner}, taking @p inner (returned to the pool on failure). */
static Error CreateWrapper(JSONObjectPool* pool, JSONWrapper wrapper, JSONObjectValue* inner, JSONObjectValue* outJSON)
{
    JSONCompound* Wrapper = NULL;
    Error Result = JSONObjectPool_BorrowCompound(pool, &Wrapper);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_Set(Wrapper, WrapperNames[wrapper], inner);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(pool, inner);
        Error_Deconstruct(&ReturnResult);
        if (Wrapper != NULL)
        {
            ReturnResult = JSONObjectPool_ReturnCompound(pool, Wrapper, true);
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }
    *outJSON = JSONObjectValue_CreateCompound(Wrapper);
    return Error_CreateSuccess();
}

/* A string value for @p text, wrapped as @p wrapper. */
static Error CreateTextWrapper(JSONObjectPool* pool, JSONWrapper wrapper, const unsigned char* text,
    JSONObjectValue* outJSON)
{
    JSONObjectValue Inner;
    Error Result = GameJSON_CreateStringValue(pool, text, &Inner);
    return (Result.Code == ErrorCode_Success) ? CreateWrapper(pool, wrapper, &Inner, outJSON) : Result;
}

static Error CreateDateTimeWrapper(JSONObjectPool* pool, int64_t dateTime, JSONObjectValue* outJSON)
{
    unsigned char Storage[JSON_DATE_TIME_TEXT_CAPACITY];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    Error Result = GameCalendar_FormatDateTime(dateTime, &Text);
    return (Result.Code == ErrorCode_Success) ? CreateTextWrapper(pool, JSONWrapper_DateTime, Storage, outJSON) : Result;
}

static Error CreateList(const DataValue* list, const KeyRegistry* keys, JSONObjectPool* pool, JSONObjectValue* outJSON)
{
    size_t Count = DataValue_GetListCount(list);
    if (Count == 0U)
    {
        return CreateTextWrapper(pool, JSONWrapper_List, DataType_GetName(DataValue_GetListElementType(list)), outJSON);
    }

    JSONArray* Array = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &Array);
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element;
        Result = DataValueInternal_WriteJSON(DataValue_GetListElement(list, Index), keys, pool, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONArray_Add(Array, &Element);
            if (Result.Code != ErrorCode_Success)
            {
                Error ReturnResult = JSONObjectPool_ReturnValue(pool, &Element);
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

static Error CreateCompound(const DataValue* compound, const KeyRegistry* keys, JSONObjectPool* pool,
    JSONObjectValue* outJSON)
{
    JSONCompound* Object = NULL;
    Error Result = JSONObjectPool_BorrowCompound(pool, &Object);
    size_t Count = DataValue_GetCompoundCount(compound);
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        const unsigned char* Name = DataValue_GetCompoundEntryName(compound, Index);
        const DataValue* Value = DataValue_GetCompoundEntryValue(compound, Index);
        JSONObjectValue Entry;
        // "$type" is written as the key's text: the name already says it is a key.
        Result = DataValueInternal_IsTypeEntryName(Name, StringUTF8_GetByteLength(Name))
            ? GameJSON_CreateStringValue(pool, KeyRegistry_GetText(keys, Value->KeyValue), &Entry)
            : DataValueInternal_WriteJSON(Value, keys, pool, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONCompound_Set(Object, Name, &Entry);
            if (Result.Code != ErrorCode_Success)
            {
                Error ReturnResult = JSONObjectPool_ReturnValue(pool, &Entry);
                Error_Deconstruct(&ReturnResult);
            }
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (Object != NULL)
        {
            Error ReturnResult = JSONObjectPool_ReturnCompound(pool, Object, true);
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }
    *outJSON = JSONObjectValue_CreateCompound(Object);
    return Error_CreateSuccess();
}


// Internal functions (DataValueInternal.h).
Error DataValueInternal_ReadJSON(const JSONObjectValue* json, KeyRegistry* keys, const unsigned char* defaultNamespace,
    size_t level, DataPath* path, DataBudget* budget, DataValue* outValue)
{
    JSONRead Read = { .Keys = keys, .DefaultNamespace = defaultNamespace, .Path = path, .Budget = budget };
    return ReadValue(&Read, json, level, outValue);
}

Error DataValueInternal_WriteJSON(const DataValue* value, const KeyRegistry* keys, JSONObjectPool* pool,
    JSONObjectValue* outJSON)
{
    JSONObjectValue Inner;
    Error Result = Error_CreateSuccess();
    switch (value->Type)
    {
        case DataType_Tag:
            return Error_Construct2(ErrorCode_IllegalArgument, "A tag has no JSON value (tags are written in a list).");
        case DataType_Bool:
            *outJSON = JSONObjectValue_CreateBoolean(value->BoolValue);
            return Error_CreateSuccess();
        case DataType_Int:
            *outJSON = JSONObjectValue_CreateInteger(value->IntValue);
            return Error_CreateSuccess();
        case DataType_Float:
            // JSON_Serialize keeps a whole value's fraction ("2.0"), so a float reads back as a float.
            *outJSON = JSONObjectValue_CreateRealNumber(value->FloatValue);
            return Error_CreateSuccess();
        case DataType_String:
            return GameJSON_CreateStringValue(pool, value->StringValue, outJSON);
        case DataType_Key:
            return CreateTextWrapper(pool, JSONWrapper_Key, KeyRegistry_GetText(keys, value->KeyValue), outJSON);
        case DataType_Object:
            Result = GameJSON_CreateUInt64Value(pool, value->ObjectValue, &Inner);
            return (Result.Code == ErrorCode_Success) ? CreateWrapper(pool, JSONWrapper_Object, &Inner, outJSON) : Result;
        case DataType_Vector3:
            Result = GameJSON_CreateVector3Value(pool, value->Vector3Value, &Inner);
            return (Result.Code == ErrorCode_Success) ? CreateWrapper(pool, JSONWrapper_Vector3, &Inner, outJSON) : Result;
        case DataType_Color:
            Result = GameJSON_CreateColorValue(pool, value->ColorValue, &Inner);
            return (Result.Code == ErrorCode_Success) ? CreateWrapper(pool, JSONWrapper_Color, &Inner, outJSON) : Result;
        case DataType_DateTime:
            return CreateDateTimeWrapper(pool, value->DateTimeValue, outJSON);
        case DataType_List:
            return CreateList(value, keys, pool, outJSON);
        case DataType_Compound:
            return CreateCompound(value, keys, pool, outJSON);
    }
    return Error_Construct2(ErrorCode_InvalidState, "The value has an unknown type.");
}


// Public functions.
Error DataValue_WriteJSON(const DataValue* value, const KeyRegistry* keys, JSONObjectPool* pool,
    JSONObjectValue* outJSON)
{
    if ((value == NULL) || (keys == NULL) || (pool == NULL) || (outJSON == NULL) || (value->Type == DataType_Tag))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataValue_WriteJSON: value (not a tag), keys, pool and outJSON must not be NULL.");
    }
    Error Result = DataValue_Validate(value, keys);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return DataValueInternal_WriteJSON(value, keys, pool, outJSON);
}

Error DataValue_ReadJSON(const JSONObjectValue* json, KeyRegistry* keys, const unsigned char* defaultNamespace,
    DataValue* outValue)
{
    if (outValue != NULL)
    {
        *outValue = DataValue_CreateTag();
    }
    if ((json == NULL) || (keys == NULL) || (outValue == NULL) || ((defaultNamespace != NULL)
        && !NamespacedKey_IsNamespaceValid(defaultNamespace, StringUTF8_GetByteLength(defaultNamespace))))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataValue_ReadJSON: json, keys and outValue must not be NULL, and defaultNamespace must be valid.");
    }
    // All or nothing: a refused value forgets the keys it interned.
    size_t KeyCheckpoint = KeyRegistry_GetCount(keys);
    DataPath Path = DataPath_Create();
    Error Result = DataValueInternal_ReadJSON(json, keys, defaultNamespace, 1U, &Path, NULL, outValue);
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataValueInternal_CheckRead(outValue, keys, &Path, ErrorCode_InvalidJSON);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DataValue_Deconstruct(outValue);
        KeyRegistry_TruncateTo(keys, KeyCheckpoint);
    }
    return Result;
}
