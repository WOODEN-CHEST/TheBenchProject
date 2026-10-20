// JSON reading and writing of property tables (see PropertyDescriptor.h for the value forms).

#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "PropertyDescriptorInternal.h"
#include "DataMap.h"
#include "NamespacedKey.h"
#include "GameCalendar.h"
#include "GameJSON.h"
#include "Logger.h"
#include "wr/WRCollection.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
#define PROPERTY_JSON_PI 3.14159265358979323846
/** Angles are radians in memory and degrees in JSON. */
#define PROPERTY_JSON_DEGREES_PER_RADIAN (180.0 / PROPERTY_JSON_PI)
#define PROPERTY_JSON_RADIANS_PER_DEGREE (PROPERTY_JSON_PI / 180.0)
/** The %g precisions tried for an angle's shortest degrees: printf's default first, DBL_DECIMAL_DIG always reads back. */
#define PROPERTY_JSON_ANGLE_PRECISION_START 6
#define PROPERTY_JSON_ANGLE_TEXT_SIZE 40
/** Room for a date's text "yyyy-mm-dd" and its terminator. */
#define PROPERTY_JSON_DATE_TEXT_CAPACITY (GAME_CALENDAR_DATE_TEXT_LENGTH + 1U)
/** How many unknown keys one message names (the rest are counted), and how many bytes of each it prints. */
#define PROPERTY_JSON_UNKNOWN_KEY_LIST_LIMIT ((size_t)8)
#define PROPERTY_JSON_UNKNOWN_KEY_PRINT_LIMIT ((size_t)64)
/** The bits that mark a UTF-8 continuation byte (10xxxxxx). */
#define PROPERTY_JSON_UTF8_CONTINUATION_MASK 0xC0U
#define PROPERTY_JSON_UTF8_CONTINUATION_BITS 0x80U


// Static functions: values.
static double DegreesToRadians(double degrees)
{
    return degrees * PROPERTY_JSON_RADIANS_PER_DEGREE;
}

static bool IsFiniteFloatRange(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value) && (Math_AbsDouble(value) <= (double)FLT_MAX);
}

/* The shortest decimal degrees that read back (through DegreesToRadians and a float) as exactly the same radians. */
static JSONObjectValue CreateAngleValue(float radians)
{
    double Degrees = (double)radians * PROPERTY_JSON_DEGREES_PER_RADIAN;
    char Text[PROPERTY_JSON_ANGLE_TEXT_SIZE];
    for (int Precision = PROPERTY_JSON_ANGLE_PRECISION_START; Precision <= DBL_DECIMAL_DIG; Precision++)
    {
        int Count = snprintf(Text, sizeof(Text), "%.*g", Precision, Degrees);
        if ((Count < 0) || ((size_t)Count >= sizeof(Text)))
        {
            break;
        }
        double ReadBack = strtod(Text, NULL);
        double Radians = DegreesToRadians(ReadBack);
        if (IsFiniteFloatRange(Radians) && ((float)Radians == radians))
        {
            return JSONObjectValue_CreateRealNumber(ReadBack);
        }
    }
    return JSONObjectValue_CreateRealNumber(Degrees);
}

/* Three angles as an array of their shortest degrees, or the array returned to the pool on failure. */
static Error CreateAngleVector3Value(JSONObjectPool* pool, Vector3 radians, JSONObjectValue* outValue)
{
    JSONArray* Array = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const float Components[3] = { radians.x, radians.y, radians.z };
    for (size_t Index = 0; (Index < 3U) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element = CreateAngleValue(Components[Index]);
        Result = JSONArray_Add(Array, &Element);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    *outValue = JSONObjectValue_CreateArray(Array);
    return Error_CreateSuccess();
}

/* The date text "yyyy-mm-dd" as a JSON string, formatted on the stack. */
static Error CreateDateValue(JSONObjectPool* pool, int64_t dayNumber, JSONObjectValue* outValue)
{
    unsigned char Storage[PROPERTY_JSON_DATE_TEXT_CAPACITY];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    Error Result = GameCalendar_FormatDate(dayNumber, &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return GameJSON_CreateStringValue(pool, Storage, outValue);
}

static Error CreateValue(JSONObjectPool* pool, const PropertyDescriptor* descriptor, const PropertyData* data,
    JSONObjectValue* outValue)
{
    switch (descriptor->Type)
    {
        case PropertyType_Bool:
            *outValue = JSONObjectValue_CreateBoolean(data->BoolValue);
            return Error_CreateSuccess();
        case PropertyType_Int32:
            *outValue = JSONObjectValue_CreateInteger((int64_t)data->Int32Value);
            return Error_CreateSuccess();
        case PropertyType_UInt32:
            *outValue = JSONObjectValue_CreateInteger((int64_t)data->UInt32Value);
            return Error_CreateSuccess();
        case PropertyType_UInt64:
            return GameJSON_CreateUInt64Value(pool, data->UInt64Value, outValue);
        case PropertyType_ObjectId:
            return GameJSON_CreateUInt64Value(pool, data->ObjectIdValue, outValue);
        case PropertyType_Float:
            *outValue = GameJSON_CreateFloatValue(data->FloatValue);
            return Error_CreateSuccess();
        case PropertyType_Double:
            // JSON_Serialize writes a real number as the shortest text that reads back to the same double.
            *outValue = JSONObjectValue_CreateRealNumber(data->DoubleValue);
            return Error_CreateSuccess();
        case PropertyType_Angle:
            *outValue = CreateAngleValue(data->FloatValue);
            return Error_CreateSuccess();
        case PropertyType_Vector2:
            return GameJSON_CreateVector2Value(pool, data->Vector2Value, outValue);
        case PropertyType_Vector3:
            return GameJSON_CreateVector3Value(pool, data->Vector3Value, outValue);
        case PropertyType_AngleVector3:
            return CreateAngleVector3Value(pool, data->Vector3Value, outValue);
        case PropertyType_Color:
            return GameJSON_CreateColorValue(pool, data->ColorValue, outValue);
        case PropertyType_RenderColor:
            return GameJSON_CreateRenderColorValue(pool, data->RenderColorValue, outValue);
        case PropertyType_String:
        case PropertyType_AssetName:
            if (data->StringValue == NULL)
            {
                *outValue = JSONObjectValue_CreateNull();
                return Error_CreateSuccess();
            }
            return GameJSON_CreateStringValue(pool, data->StringValue, outValue);
        case PropertyType_Enum:
        {
            const PropertyEnumEntry* Entry = PropertyInternal_FindEnumByValue(descriptor->EnumTable, data->EnumValue);
            if (Entry == NULL)
            {
                return Error_Construct2(ErrorCode_InvalidState, "The enum value has no name.");
            }
            return GameJSON_CreateStringValue(pool, Entry->Name, outValue);
        }
        case PropertyType_Date:
            return CreateDateValue(pool, data->DateValue, outValue);
        case PropertyType_DataMap:
            return Error_Construct2(ErrorCode_InvalidState, "Data maps are written separately.");
    }
    return Error_Construct2(ErrorCode_InvalidState, "The property has an unknown type.");
}

static Error ReadInteger(const JSONObjectValue* value, int64_t minimum, int64_t maximum, int64_t* outValue)
{
    int64_t Wide = 0;
    Error Result = GameJSON_ValueToInt64(value, &Wide);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((Wide < minimum) || (Wide > maximum))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "The integer does not fit the property's type.");
    }
    *outValue = Wide;
    return Error_CreateSuccess();
}

/* A float, or an angle given in degrees (stored in radians). */
static Error ReadFloat(const JSONObjectValue* value, bool isAngle, float* outValue)
{
    double Decimal = 0.0;
    Error Result = GameJSON_ValueToDouble(value, &Decimal);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (isAngle)
    {
        Decimal = DegreesToRadians(Decimal);
    }
    if (!IsFiniteFloatRange(Decimal))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "The number does not fit a float.");
    }
    *outValue = (float)Decimal;
    return Error_CreateSuccess();
}

/* Three angles given in degrees (each read as a double, so it converts to the same radians it was written from). */
static Error ReadAngleVector3(const JSONObjectValue* value, Vector3* outValue)
{
    double Degrees[3] = { 0.0, 0.0, 0.0 };
    Error Result = GameJSON_ParseVector3Doubles(value, Degrees);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Radians[3] = { 0.0f, 0.0f, 0.0f };
    for (size_t Index = 0; Index < 3U; Index++)
    {
        double Converted = DegreesToRadians(Degrees[Index]);
        if (!IsFiniteFloatRange(Converted))
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "The angle does not fit a float.");
        }
        Radians[Index] = (float)Converted;
    }
    *outValue = (Vector3){ .x = Radians[0], .y = Radians[1], .z = Radians[2] };
    return Error_CreateSuccess();
}

/* GameJSON_ParseVector2 reads the first two elements of a longer array; a Vector2 property wants exactly two. */
static Error ReadVector2(const JSONObjectValue* value, Vector2* outValue)
{
    if ((value->Type == JSONValueType_Array) && (JSONArray_GetElementCount(value->Value.Array) != 2U))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Vector array requires exactly two elements.");
    }
    return GameJSON_ParseVector2(value, outValue);
}

static Error ReadEnum(const PropertyDescriptor* descriptor, const JSONObjectValue* value, int32_t* outValue)
{
    const PropertyEnumTable* Enum = descriptor->EnumTable;
    if (value->Type == JSONValueType_String)
    {
        GenericBuffer* Buffer = value->Value.String;
        size_t ByteCount = (Buffer != NULL) ? Buffer->_count : 0U;
        const PropertyEnumEntry* Entry = PropertyInternal_FindEnumByName(Enum, (Buffer != NULL) ? Buffer->_data : NULL,
            ByteCount);
        if (Entry != NULL)
        {
            *outValue = Entry->Value;
            return Error_CreateSuccess();
        }
    }

    GenericBuffer Message;
    GenericBuffer_AllocateVariable(&Message, 0U, sizeof(unsigned char));
    Error AppendResult = StringUTF8_Format(u8"Expected the %s name, one of ", &Message, (const char*)Enum->Name);
    if (AppendResult.Code == ErrorCode_Success)
    {
        AppendResult = PropertyInternal_AppendEnumNames(Enum, &Message);
    }
    Error_Deconstruct(&AppendResult);
    Error Result = Error_Construct4(ErrorCode_InvalidJSON, "%s.",
        (Message._count > 0U) ? (const char*)Message._data : "Expected an enum name");
    Memory_Free(Message._data);
    return Result;
}

/* Only the text "yyyy-mm-dd": a bare day number would be a count of days no person could check. */
static Error ReadDate(const JSONObjectValue* value, int64_t* outDayNumber)
{
    if (value->Type == JSONValueType_String)
    {
        GenericBuffer* Buffer = value->Value.String;
        if ((Buffer != NULL) && GameCalendar_TryParseDate(Buffer->_data, Buffer->_count, outDayNumber))
        {
            return Error_CreateSuccess();
        }
    }
    return Error_Construct2(ErrorCode_InvalidJSON, "Expected a date \"yyyy-mm-dd\" in the years 1 to 9999.");
}

/* The scalar and structured types; strings are read by ReadString (they are owned). */
static Error ParseValue(const PropertyDescriptor* descriptor, const JSONObjectValue* value, PropertyData* outData)
{
    int64_t Wide = 0;
    Error Result = Error_CreateSuccess();
    switch (descriptor->Type)
    {
        case PropertyType_Bool:
            return GameJSON_ValueToBoolean(value, &outData->BoolValue);
        case PropertyType_Int32:
            Result = ReadInteger(value, (int64_t)INT32_MIN, (int64_t)INT32_MAX, &Wide);
            outData->Int32Value = (int32_t)Wide;
            return Result;
        case PropertyType_UInt32:
            Result = ReadInteger(value, 0, (int64_t)UINT32_MAX, &Wide);
            outData->UInt32Value = (uint32_t)Wide;
            return Result;
        case PropertyType_UInt64:
            return GameJSON_ValueToUInt64(value, &outData->UInt64Value);
        case PropertyType_ObjectId:
            return GameJSON_ValueToUInt64(value, &outData->ObjectIdValue);
        case PropertyType_Float:
            return ReadFloat(value, false, &outData->FloatValue);
        case PropertyType_Double:
            return GameJSON_ValueToDouble(value, &outData->DoubleValue);
        case PropertyType_Angle:
            return ReadFloat(value, true, &outData->FloatValue);
        case PropertyType_Vector2:
            return ReadVector2(value, &outData->Vector2Value);
        case PropertyType_Vector3:
            return GameJSON_ParseVector3(value, &outData->Vector3Value);
        case PropertyType_AngleVector3:
            return ReadAngleVector3(value, &outData->Vector3Value);
        case PropertyType_Color:
            return GameJSON_ParseColor(value, &outData->ColorValue);
        case PropertyType_RenderColor:
            Result = GameJSON_ParseRenderColor(value, &outData->RenderColorValue);
            outData->RenderColorValue.Tint.a = (unsigned char)255U; // not part of a render color's value
            return Result;
        case PropertyType_String:
        case PropertyType_AssetName:
            return Error_Construct2(ErrorCode_InvalidState, "Strings are read separately.");
        case PropertyType_Enum:
            return ReadEnum(descriptor, value, &outData->EnumValue);
        case PropertyType_Date:
            return ReadDate(value, &outData->DateValue);
        case PropertyType_DataMap:
            return Error_Construct2(ErrorCode_InvalidState, "Data maps are read separately.");
    }
    return Error_Construct2(ErrorCode_InvalidState, "The property has an unknown type.");
}

/* A string or null, length-checked before it is copied (the rest of its rules are checked when the read commits). */
static Error ReadString(const PropertyDescriptor* descriptor, const JSONObjectValue* value, void* target)
{
    if (value->Type == JSONValueType_Null)
    {
        PropertyInternal_AdoptOwned(descriptor, target, NULL);
        return Error_CreateSuccess();
    }
    if (value->Type != JSONValueType_String)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Expected a string or null.");
    }

    size_t ByteCount = (value->Value.String != NULL) ? value->Value.String->_count : 0U;
    if (ByteCount > descriptor->Rules.MaxLength)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "The string is longer than the %" PRIu64 " bytes allowed.",
            (uint64_t)descriptor->Rules.MaxLength);
    }
    unsigned char* Text = NULL;
    Error Result = GameJSON_ValueToOwnedString(value, &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (StringUTF8_GetByteLength(Text) != ByteCount)
    {
        Memory_Free(Text);
        return Error_Construct2(ErrorCode_InvalidJSON, "The string must not contain NUL characters.");
    }
    PropertyInternal_AdoptOwned(descriptor, target, Text);
    return Error_CreateSuccess();
}

static Error ReadValue(const PropertyTable* table, const PropertyDescriptor* descriptor, const JSONObjectValue* value,
    void* target)
{
    Error Result = Error_CreateSuccess();
    if (PropertyInternal_IsStringType(descriptor->Type))
    {
        Result = ReadString(descriptor, value, target);
    }
    else
    {
        PropertyData Data;
        Memory_Zero(&Data, sizeof(Data));
        Result = ParseValue(descriptor, value, &Data);
        if (Result.Code == ErrorCode_Success)
        {
            PropertyInternal_WriteField(descriptor, target, &Data);
        }
    }
    return (Result.Code == ErrorCode_Success) ? Result
        : PropertyInternal_WrapError(ErrorCode_InvalidJSON, table, descriptor, &Result);
}


/* A data map from its two keys (either may be absent). The map's messages name where they are ("tags[2]: ...",
 * "data.town:x: ..."), so they are prefixed with the table only. */
static Error ReadDataMap(const PropertyTable* table, const PropertyDescriptor* descriptor, JSONCompound* compound,
    const PropertyReadOptions* options, void* target, size_t* inOutFoundCount)
{
    if (options->Keys == NULL)
    {
        JSONObjectValue Ignored;
        bool HasTags = false;
        bool HasValues = false;
        Error TagsResult = JSONCompound_GetOptional(compound, descriptor->TagsKey, &Ignored, &HasTags);
        Error ValuesResult = JSONCompound_GetOptional(compound, descriptor->Key, &Ignored, &HasValues);
        Error_Deconstruct(&TagsResult);
        Error_Deconstruct(&ValuesResult);
        if (HasTags || HasValues)
        {
            return Error_Construct4(ErrorCode_InvalidJSON,
                "%s.%s: reading tags and data needs the key registry (PropertyReadOptions.Keys).",
                (const char*)table->Name, (const char*)descriptor->Key);
        }
        return Error_CreateSuccess();
    }

    DataMapLimits Limits = { .MaxEntryCount = descriptor->Rules.MaxCount, .MaxByteSize = descriptor->Rules.MaxLength };
    DataMap* Map = NULL;
    size_t FoundCount = 0U;
    Error Result = DataMap_ReadJSON(compound, descriptor->TagsKey, descriptor->Key, options->Keys,
        options->DefaultNamespace, &Limits, &Map, &FoundCount);
    if (Result.Code != ErrorCode_Success)
    {
        return PropertyInternal_WrapError(ErrorCode_InvalidJSON, table, NULL, &Result);
    }
    *inOutFoundCount += FoundCount;
    PropertyInternal_AdoptOwned(descriptor, target, Map);
    return Error_CreateSuccess();
}

/* The persistent descriptor that reads @p key: its Key, or a data map's TagsKey; NULL for none. */
const PropertyDescriptor* PropertyTable_FindByJSONKey(const PropertyTable* table, const unsigned char* key)
{
    if ((table == NULL) || (key == NULL))
    {
        return NULL;
    }
    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (!PropertyInternal_HasFlags(Descriptor, PropertyFlags_Persistent))
        {
            continue;
        }
        bool IsTagsKey = (Descriptor->TagsKey != NULL)
            && (StringUTF8_GetByteLength(Descriptor->TagsKey) == StringUTF8_GetByteLength(key))
            && Memory_IsEqual(Descriptor->TagsKey, key, StringUTF8_GetByteLength(key));
        bool IsKey = (StringUTF8_GetByteLength(Descriptor->Key) == StringUTF8_GetByteLength(key))
            && Memory_IsEqual(Descriptor->Key, key, StringUTF8_GetByteLength(key));
        if (IsKey || IsTagsKey)
        {
            return Descriptor;
        }
    }
    return NULL;
}


// Static functions: unknown keys.
/* Appends ", \"key\"" (the first without the comma), cut to the print limit on a UTF-8 boundary. A failed append only
 * shortens the message. */
static void AppendKey(GenericBuffer* list, const unsigned char* key, bool isFirst)
{
    size_t ByteCount = StringUTF8_GetByteLength(key);
    size_t PrintCount = ByteCount;
    if (PrintCount > PROPERTY_JSON_UNKNOWN_KEY_PRINT_LIMIT)
    {
        PrintCount = PROPERTY_JSON_UNKNOWN_KEY_PRINT_LIMIT;
        while ((PrintCount > 0U)
            && ((key[PrintCount] & PROPERTY_JSON_UTF8_CONTINUATION_MASK) == PROPERTY_JSON_UTF8_CONTINUATION_BITS))
        {
            PrintCount--;
        }
    }
    Error Result = StringUTF8_Format(u8"%s\"%.*s%s\"", list, isFirst ? "" : ", ", (int)PrintCount, (const char*)key,
        (PrintCount < ByteCount) ? "..." : "");
    Error_Deconstruct(&Result);
}

/* Lists the compound's keys that are not persistent properties of the table, counting them in @p outCount. */
static Error CollectUnknownKeys(const PropertyTable* table, JSONCompound* compound, GenericBuffer* list, size_t* outCount)
{
    *outCount = 0U;
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(JSONCompound_GetEntryCollection(compound));
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
        if (PropertyTable_FindByJSONKey(table, Entry.Key) == NULL)
        {
            if (*outCount < PROPERTY_JSON_UNKNOWN_KEY_LIST_LIMIT)
            {
                AppendKey(list, Entry.Key, *outCount == 0U);
            }
            (*outCount)++;
        }
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    }
    CollectionEnumerator_Destroy(Enumerator);

    if ((Result.Code == ErrorCode_Success) && (*outCount > PROPERTY_JSON_UNKNOWN_KEY_LIST_LIMIT))
    {
        Error AppendResult = StringUTF8_Format(u8" and %u more", list,
            (unsigned int)(*outCount - PROPERTY_JSON_UNKNOWN_KEY_LIST_LIMIT));
        Error_Deconstruct(&AppendResult);
    }
    return Result;
}

/* Error mode fails the read; Warn mode logs one warning naming the keys; Ignore (or Warn without a logger) does
 * nothing. */
static Error HandleUnknownKeys(const PropertyTable* table, JSONCompound* compound, const PropertyReadOptions* options)
{
    bool IsLogged = (options->UnknownKeyMode == PropertyUnknownKeyMode_Warn) && (options->Logger != NULL);
    bool IsError = (options->UnknownKeyMode != PropertyUnknownKeyMode_Warn)
        && (options->UnknownKeyMode != PropertyUnknownKeyMode_Ignore);
    if (!IsLogged && !IsError)
    {
        return Error_CreateSuccess();
    }

    GenericBuffer List;
    GenericBuffer_AllocateVariable(&List, 0U, sizeof(unsigned char));
    size_t UnknownCount = 0U;
    Error Result = CollectUnknownKeys(table, compound, &List, &UnknownCount);
    if ((Result.Code == ErrorCode_Success) && (UnknownCount > 0U))
    {
        const char* Keys = (List._count > 0U) ? (const char*)List._data : "?";
        const char* Plural = (UnknownCount > 1U) ? "s" : "";
        if (IsError)
        {
            Result = Error_Construct4(ErrorCode_InvalidJSON, "%s: unknown key%s %s.", (const char*)table->Name, Plural,
                Keys);
        }
        else
        {
            Error LogResult = Logger_LogWarningFormatted(options->Logger, u8"%s: ignoring unknown key%s %s.",
                (const char*)table->Name, Plural, Keys);
            Error_Deconstruct(&LogResult);
        }
    }
    Memory_Free(List._data);
    return Result;
}


// Public functions.
Error Properties_WriteJSON(const PropertyTable* table, const void* instance, JSONObjectPool* pool, JSONCompound* compound,
    const PropertyWriteOptions* options)
{
    if ((table == NULL) || (instance == NULL) || (pool == NULL) || (compound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_WriteJSON: table, instance, pool and compound must not be NULL.");
    }
    PropertyWriteOptions Options = (options != NULL) ? *options : PropertyWriteOptions_CreateDefault();
    Error Result = Properties_Validate(table, instance);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (!PropertyInternal_HasFlags(Descriptor, PropertyFlags_Persistent)
            || (!Options.AreDefaultsWritten && PropertyInternal_IsDefault(Descriptor, instance)))
        {
            continue;
        }

        PropertyData Data = PropertyInternal_ReadField(Descriptor, instance);
        if (Descriptor->Type == PropertyType_DataMap)
        {
            // Two keys: its tags and its values.
            Result = DataMap_WriteJSON(Data.DataMapValue, pool, compound, Descriptor->TagsKey, Descriptor->Key,
                Options.AreDefaultsWritten);
            if (Result.Code != ErrorCode_Success)
            {
                return PropertyInternal_WrapError(Result.Code, table, NULL, &Result);
            }
            continue;
        }
        JSONObjectValue Value;
        Result = CreateValue(pool, Descriptor, &Data, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            return PropertyInternal_WrapError(Result.Code, table, Descriptor, &Result);
        }
        Result = JSONCompound_Set(compound, Descriptor->Key, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = JSONObjectPool_ReturnValue(pool, &Value);
            Error_Deconstruct(&ReturnResult);
            return PropertyInternal_WrapError(Result.Code, table, Descriptor, &Result);
        }
    }
    return Error_CreateSuccess();
}

Error Properties_ReadJSON(const PropertyTable* table, JSONCompound* compound, void* instance,
    const PropertyReadOptions* options)
{
    if ((table == NULL) || (compound == NULL) || (instance == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_ReadJSON: table, compound and instance must not be NULL.");
    }
    PropertyReadOptions Options = (options != NULL) ? *options : PropertyReadOptions_CreateDefault();
    if ((Options.DataVersion != 0U) && (Options.DataVersion < table->Version) && (table->Migrate != NULL))
    {
        Error MigrateResult = table->Migrate(Options.DataVersion, compound, Options.Pool);
        if (MigrateResult.Code != ErrorCode_Success)
        {
            return PropertyInternal_WrapError(MigrateResult.Code, table, NULL, &MigrateResult);
        }
    }

    // All or nothing, for the key registry too: a refused read forgets the keys its data rows interned.
    size_t KeyCheckpoint = (Options.Keys != NULL) ? KeyRegistry_GetCount(Options.Keys) : 0U;
    PropertyScratch Scratch;
    PropertyScratch_Begin(&Scratch, table, instance);
    size_t FoundCount = 0U;
    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (!PropertyInternal_HasFlags(Descriptor, PropertyFlags_Persistent))
        {
            continue;
        }

        JSONObjectValue Value;
        bool IsFound = false;
        Error Result = Error_CreateSuccess();
        if (Descriptor->Type == PropertyType_DataMap)
        {
            Result = ReadDataMap(table, Descriptor, compound, &Options, Scratch._instance, &FoundCount);
        }
        else
        {
            Result = JSONCompound_GetOptional(compound, Descriptor->Key, &Value, &IsFound);
        }
        if ((Result.Code == ErrorCode_Success) && IsFound)
        {
            FoundCount++;
            Result = ReadValue(table, Descriptor, &Value, Scratch._instance);
        }
        if (Result.Code != ErrorCode_Success)
        {
            PropertyScratch_Abort(&Scratch, table);
            KeyRegistry_TruncateTo(Options.Keys, KeyCheckpoint);
            return Result;
        }
    }

    Error Result = Error_CreateSuccess();
    if (JSONCompound_GetEntryCount(compound) > FoundCount)
    {
        Result = HandleUnknownKeys(table, compound, &Options);
        if (Result.Code != ErrorCode_Success)
        {
            PropertyScratch_Abort(&Scratch, table);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PropertyScratch_Commit(&Scratch, table, instance, ErrorCode_InvalidJSON);
    }
    if (Result.Code != ErrorCode_Success)
    {
        KeyRegistry_TruncateTo(Options.Keys, KeyCheckpoint);
    }
    return Result;
}
