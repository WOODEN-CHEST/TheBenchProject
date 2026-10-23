// GHDF reading and writing of data values (see DataValue.h for the layout): a value is a compound with its type
// (entry 2), its value (entry 3) and a list's element type (entry 4), numbers in the smallest GHDF type that holds them.

#include <float.h>
#include <stdio.h>
#include "DataValueInternal.h"
#include "GameGHDF.h"
#include "GameCalendar.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** Components written per vector3 and channels per color. */
#define GHDF_VECTOR3_COMPONENTS 3U
#define GHDF_COLOR_CHANNELS 4U
/** Room for a problem text that names types. */
#define GHDF_PROBLEM_CAPACITY ((size_t)160)


// Types.
/* The entries of a value compound a read cares about (absent ones have HasX false). */
typedef struct GHDFValueEntriesStruct
{
    bool HasType;
    GHDFCompoundEntry Type;
    bool HasValue;
    GHDFCompoundEntry Value;
    bool HasElementType;
    GHDFCompoundEntry ElementType;
} GHDFValueEntries;

/* What a read needs at every level. */
typedef struct GHDFReadStruct
{
    KeyRegistry* Keys;
    DataPath* Path;
    /* The bytes the value may still count, or NULL for no budget. */
    DataBudget* Budget;
} GHDFRead;


// Static functions: writing.
static Error PutScalar(GHDFCompound* compound, GHDFEntryID id, GHDFObjectValue value)
{
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(value.Type), &value);
}

/* A pool string holding @p text and its terminator, or NULL (the pool's error in *outResult). */
static GenericBuffer* CreateString(GHDFObjectPool* pool, const unsigned char* text, Error* outResult)
{
    GenericBuffer* Buffer = NULL;
    *outResult = GHDFObjectPool_BorrowString(pool, &Buffer);
    if (outResult->Code != ErrorCode_Success)
    {
        return NULL;
    }
    if (!GenericBuffer_AppendString(Buffer, text) || !GenericBuffer_NullTerminate(Buffer))
    {
        Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        *outResult = Error_Construct2(ErrorCode_EncodeError, "A GHDF string could not be built.");
        return NULL;
    }
    return Buffer;
}

/* Sets an array entry; the array is returned to the pool on failure. */
static Error PutArray(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, GHDFArray* array,
    GHDFValueType elementType)
{
    GHDFObjectValue Value = GHDFObjectValue_CreateArray(array, elementType);
    Error Result = GHDFCompound_SetValue(compound, id, GHDF_CreateArrayType(elementType), &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, array, true);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

/* Adds the three components of @p vector to @p array (a Double array). */
static Error AddVector3(GHDFArray* array, Vector3Double vector)
{
    const double Components[GHDF_VECTOR3_COMPONENTS] = { vector.X, vector.Y, vector.Z };
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < GHDF_VECTOR3_COMPONENTS) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Element = GHDFObjectValue_CreateDouble(Components[Index]);
        Result = GHDFArray_AddValue(array, &Element);
    }
    return Result;
}

/* Adds the four channels of @p color to @p array. */
static Error AddChannels(GHDFArray* array, Color color)
{
    const uint8_t Channels[GHDF_COLOR_CHANNELS] = { color.r, color.g, color.b, color.a };
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < GHDF_COLOR_CHANNELS) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Element = GHDFObjectValue_CreateUInt8(Channels[Index]);
        Result = GHDFArray_AddValue(array, &Element);
    }
    return Result;
}

static bool IsExactFloat(double value)
{
    return (Math_AbsDouble(value) <= (double)FLT_MAX) && ((double)(float)value == value);
}

/* The smallest signed type that holds every element of an int list. */
static GHDFValueType GetIntListType(const DataValue* list)
{
    int64_t Minimum = 0;
    int64_t Maximum = 0;
    for (size_t Index = 0U; Index < DataValue_GetListCount(list); Index++)
    {
        int64_t Value = DataValue_GetListElement(list, Index)->IntValue;
        Minimum = (Value < Minimum) ? Value : Minimum;
        Maximum = (Value > Maximum) ? Value : Maximum;
    }
    return GameGHDF_GetSmallestSignedType(Minimum, Maximum);
}

/* The smallest unsigned type that holds every element of an object list. */
static GHDFValueType GetObjectListType(const DataValue* list)
{
    uint64_t Maximum = 0U;
    for (size_t Index = 0U; Index < DataValue_GetListCount(list); Index++)
    {
        uint64_t Value = DataValue_GetListElement(list, Index)->ObjectValue;
        Maximum = (Value > Maximum) ? Value : Maximum;
    }
    return GameGHDF_GetSmallestUnsignedType(Maximum);
}

/* Float when every element of a float list is exactly a float, Double otherwise. */
static GHDFValueType GetFloatListType(const DataValue* list)
{
    for (size_t Index = 0U; Index < DataValue_GetListCount(list); Index++)
    {
        if (!IsExactFloat(DataValue_GetListElement(list, Index)->FloatValue))
        {
            return GHDFValueType_Double;
        }
    }
    return GHDFValueType_Float;
}

/* The GHDF element type a list is written with, given its elements. */
static GHDFValueType GetListArrayType(const DataValue* list, DataType elementType)
{
    switch (elementType)
    {
        case DataType_Bool:
            return GHDFValueType_Boolean;
        case DataType_Int:
            return GetIntListType(list);
        case DataType_Float:
            return GetFloatListType(list);
        case DataType_Object:
            return GetObjectListType(list);
        case DataType_String:
        case DataType_Key:
            return GHDFValueType_String;
        case DataType_Vector3:
            return GHDFValueType_Double;
        case DataType_Color:
            return GHDFValueType_UInt8;
        case DataType_DateTime:
            return GHDFValueType_Int64;
        case DataType_List:
        case DataType_Compound:
            return GHDFValueType_Compound;
        case DataType_Tag:
            break;
    }
    return GHDFValueType_None;
}

/* One list element into @p array of @p arrayType. */
static Error AddListElement(const DataValue* element, const KeyRegistry* keys, GHDFObjectPool* pool, GHDFArray* array,
    GHDFValueType arrayType)
{
    GHDFObjectValue Value = { .Type = GHDFValueType_None };
    Error Result = Error_CreateSuccess();
    switch (element->Type)
    {
        case DataType_Bool:
            Value = GHDFObjectValue_CreateBoolean(element->BoolValue);
            break;
        case DataType_Int:
            Value = GameGHDF_CreateSigned(arrayType, element->IntValue);
            break;
        case DataType_Float:
            Value = (arrayType == GHDFValueType_Float) ? GHDFObjectValue_CreateFloat((float)element->FloatValue)
                : GHDFObjectValue_CreateDouble(element->FloatValue);
            break;
        case DataType_Object:
            Value = GameGHDF_CreateUnsigned(arrayType, element->ObjectValue);
            break;
        case DataType_DateTime:
            Value = GHDFObjectValue_CreateInt64(element->DateTimeValue);
            break;
        case DataType_String:
        case DataType_Key:
        {
            const unsigned char* Text = (element->Type == DataType_String) ? element->StringValue
                : KeyRegistry_GetText(keys, element->KeyValue);
            GenericBuffer* Buffer = CreateString(pool, Text, &Result);
            if (Buffer == NULL)
            {
                return Result;
            }
            Value = GHDFObjectValue_CreateString(Buffer);
            Result = GHDFArray_AddValue(array, &Value);
            if (Result.Code != ErrorCode_Success)
            {
                Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
                Error_Deconstruct(&ReturnResult);
            }
            return Result;
        }
        case DataType_Vector3:
            return AddVector3(array, element->Vector3Value);
        case DataType_Color:
            return AddChannels(array, element->ColorValue);
        case DataType_List:
        case DataType_Compound:
        {
            GHDFCompound* Compound = NULL;
            Result = GHDFObjectPool_BorrowCompound(pool, &Compound);
            if (Result.Code == ErrorCode_Success)
            {
                Result = DataValueInternal_WriteGHDF(element, keys, pool, Compound);
            }
            if (Result.Code == ErrorCode_Success)
            {
                Value = GHDFObjectValue_CreateCompound(Compound);
                Result = GHDFArray_AddValue(array, &Value);
            }
            if ((Result.Code != ErrorCode_Success) && (Compound != NULL))
            {
                Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, Compound, true);
                Error_Deconstruct(&ReturnResult);
            }
            return Result;
        }
        case DataType_Tag:
            return Error_Construct2(ErrorCode_InvalidState, "A list never holds tags.");
    }
    return GHDFArray_AddValue(array, &Value);
}

static Error PutList(const DataValue* list, const KeyRegistry* keys, GHDFObjectPool* pool, GHDFCompound* compound)
{
    DataType ElementType = DataValue_GetListElementType(list);
    Error Result = PutScalar(compound, DATA_GHDF_ELEMENT_TYPE, GHDFObjectValue_CreateUInt8((uint8_t)ElementType));
    GHDFValueType ArrayType = GetListArrayType(list, ElementType);
    GHDFArray* Array = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDFObjectPool_BorrowArray(pool, ArrayType, &Array);
    }
    for (size_t Index = 0U; (Index < DataValue_GetListCount(list)) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = AddListElement(DataValue_GetListElement(list, Index), keys, pool, Array, ArrayType);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (Array != NULL)
        {
            Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }
    return PutArray(pool, compound, DATA_GHDF_VALUE, Array, ArrayType);
}

/* A compound value: a Compound array of entry compounds (name, type, value, element type). */
static Error PutCompound(const DataValue* value, const KeyRegistry* keys, GHDFObjectPool* pool, GHDFCompound* compound)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_Compound, &Array);
    for (size_t Index = 0U; (Index < DataValue_GetCompoundCount(value)) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFCompound* Entry = NULL;
        Result = GHDFObjectPool_BorrowCompound(pool, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValueInternal_PutGHDFString(pool, Entry, DATA_GHDF_NAME,
                DataValue_GetCompoundEntryName(value, Index));
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValueInternal_WriteGHDF(DataValue_GetCompoundEntryValue(value, Index), keys, pool, Entry);
        }
        if (Result.Code == ErrorCode_Success)
        {
            GHDFObjectValue EntryValue = GHDFObjectValue_CreateCompound(Entry);
            Result = GHDFArray_AddValue(Array, &EntryValue);
        }
        if ((Result.Code != ErrorCode_Success) && (Entry != NULL))
        {
            Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, Entry, true);
            Error_Deconstruct(&ReturnResult);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (Array != NULL)
        {
            Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
            Error_Deconstruct(&ReturnResult);
        }
        return Result;
    }
    return PutArray(pool, compound, DATA_GHDF_VALUE, Array, GHDFValueType_Compound);
}

static Error PutFixedArray(GHDFObjectPool* pool, GHDFCompound* compound, const DataValue* value)
{
    GHDFValueType ElementType = (value->Type == DataType_Vector3) ? GHDFValueType_Double : GHDFValueType_UInt8;
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, ElementType, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (value->Type == DataType_Vector3)
    {
        Result = AddVector3(Array, value->Vector3Value);
    }
    else
    {
        Result = AddChannels(Array, value->ColorValue);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    return PutArray(pool, compound, DATA_GHDF_VALUE, Array, ElementType);
}


// Static functions: reading helpers.
static Error ReadValue(GHDFRead* read, GHDFCompound* compound, size_t level, DataValue* outValue);

static Error PathError(const GHDFRead* read, const char* problem)
{
    return DataPath_MakeError(read->Path, ErrorCode_Deserialize, problem);
}

static Error Charge(const GHDFRead* read, size_t bytes)
{
    return DataValueInternal_Charge(read->Budget, bytes, read->Path, ErrorCode_Deserialize);
}

/* Charges a value that is not a list or a compound (those charge their parts as they are read); deconstructs it when
 * it does not fit. */
static Error ChargeValue(const GHDFRead* read, DataValue* value)
{
    Error Result = Charge(read, DataValue_GetByteSize(value, read->Keys));
    if (Result.Code != ErrorCode_Success)
    {
        DataValue_Deconstruct(value);
    }
    return Result;
}

/* Whether a GHDF array type can hold vector3 components: Double since T33, Float as written before. */
static bool IsVector3ArrayType(GHDFValueType type)
{
    return (type == GHDFValueType_Double) || (type == GHDFValueType_Float);
}

/* One vector3 component from an element of a Double or Float array (a float widens exactly). */
static double GetVector3Component(const GHDFObjectValue* element)
{
    return (element->Type == GHDFValueType_Float) ? (double)element->Value.Float : element->Value.Double;
}

static Error CollectEntries(GHDFCompound* compound, GHDFValueEntries* outEntries)
{
    Memory_Zero(outEntries, sizeof(*outEntries));
    DataEnumeratorStorage Storage;
    CollectionEnumerator* Enumerator = DataValueInternal_BeginEnumeration(GHDFCompound_GetEntryCollection(compound),
        &Storage);
    if (Enumerator == NULL)
    {
        return Error_Construct2(ErrorCode_Deserialize, "The value's entries could not be enumerated.");
    }
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        GHDFCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        // Other ids (the name, ids from newer versions) are skipped.
        if (Entry.Id == DATA_GHDF_TYPE)
        {
            outEntries->HasType = true;
            outEntries->Type = Entry;
        }
        else if (Entry.Id == DATA_GHDF_VALUE)
        {
            outEntries->HasValue = true;
            outEntries->Value = Entry;
        }
        else if (Entry.Id == DATA_GHDF_ELEMENT_TYPE)
        {
            outEntries->HasElementType = true;
            outEntries->ElementType = Entry;
        }
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    }
    DataValueInternal_EndEnumeration(Enumerator, &Storage);
    return Result;
}

/* Reads a type entry (a UInt8 holding a DataType); false when absent or malformed. */
static bool TryReadType(bool isPresent, const GHDFCompoundEntry* entry, DataType* outType)
{
    if (!isPresent || entry->EntryType.IsArray || (entry->Value.Type != GHDFValueType_UInt8)
        || (entry->Value.Value.UInt8 >= DATA_TYPE_COUNT))
    {
        return false;
    }
    *outType = (DataType)entry->Value.Value.UInt8;
    return true;
}

/* A GHDF string's bytes without its terminator; false when it is not one string without inner NULs. */
static bool TryGetStringBytes(const GHDFObjectValue* value, const unsigned char** outBytes, size_t* outByteCount)
{
    GenericBuffer* Buffer = value->Value.String;
    if ((value->Type != GHDFValueType_String) || (Buffer == NULL) || (Buffer->_count == 0U)
        || (((const unsigned char*)Buffer->_data)[Buffer->_count - 1U] != (unsigned char)0)
        || (StringUTF8_GetByteLength(Buffer->_data) != (Buffer->_count - 1U)))
    {
        return false;
    }
    *outBytes = Buffer->_data;
    *outByteCount = Buffer->_count - 1U;
    return true;
}

/* A key from a GHDF string: always the full form there. */
static Error ReadKey(const GHDFRead* read, const GHDFObjectValue* value, DataValue* outValue)
{
    const unsigned char* Bytes = NULL;
    size_t ByteCount = 0U;
    if (!TryGetStringBytes(value, &Bytes, &ByteCount))
    {
        return PathError(read, "a key must be a string");
    }
    Error Result = NamespacedKey_Validate(Bytes);
    uint32_t Key = NAMESPACED_KEY_ID_NONE;
    if (Result.Code == ErrorCode_Success)
    {
        Result = KeyRegistry_Intern(read->Keys, Bytes, &Key);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
    }
    *outValue = DataValue_FromKey(Key);
    return Error_CreateSuccess();
}

static Error ReadString(const GHDFRead* read, const GHDFObjectValue* value, DataValue* outValue)
{
    const unsigned char* Bytes = NULL;
    size_t ByteCount = 0U;
    if (!TryGetStringBytes(value, &Bytes, &ByteCount))
    {
        return PathError(read, "a string must be one string without NUL characters");
    }
    Error Result = DataValue_CreateStringFromBytes(Bytes, ByteCount, outValue);
    return (Result.Code == ErrorCode_Success) ? Result : DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
}

/* A scalar from a non-array value entry of a type other than list and compound. */
static Error ReadScalar(const GHDFRead* read, DataType type, const GHDFObjectValue* value, DataValue* outValue)
{
    switch (type)
    {
        case DataType_Bool:
            if (value->Type != GHDFValueType_Boolean)
            {
                return PathError(read, "a bool must be a Boolean");
            }
            *outValue = DataValue_FromBool(value->Value.Boolean);
            return Error_CreateSuccess();
        case DataType_Int:
            *outValue = DataValue_FromInt(0);
            return GameGHDF_TryGetSigned(value, &outValue->IntValue) ? Error_CreateSuccess()
                : PathError(read, "an int must be an integer that fits 64 signed bits");
        case DataType_Float:
            if ((value->Type != GHDFValueType_Float) && (value->Type != GHDFValueType_Double))
            {
                return PathError(read, "a float must be a Float or a Double");
            }
            *outValue = DataValue_FromFloat((value->Type == GHDFValueType_Float) ? (double)value->Value.Float
                : value->Value.Double);
            return (!Math_IsNaNDouble(outValue->FloatValue) && !Math_IsInfinityDouble(outValue->FloatValue))
                ? Error_CreateSuccess() : PathError(read, "a float must be finite");
        case DataType_String:
            return ReadString(read, value, outValue);
        case DataType_Key:
            return ReadKey(read, value, outValue);
        case DataType_Object:
            *outValue = DataValue_FromObject(0U);
            return GameGHDF_TryGetUnsigned(value, &outValue->ObjectValue) ? Error_CreateSuccess()
                : PathError(read, "an object must be a non-negative integer");
        case DataType_DateTime:
            *outValue = DataValue_FromDateTime(0);
            return (GameGHDF_TryGetSigned(value, &outValue->DateTimeValue) && GameCalendar_IsDateTimeValid(outValue->DateTimeValue))
                ? Error_CreateSuccess() : PathError(read, "a date_time must be an integer inside the calendar");
        case DataType_Tag:
        case DataType_Vector3:
        case DataType_Color:
        case DataType_List:
        case DataType_Compound:
            break;
    }
    return PathError(read, "the value has the wrong shape for its type");
}

/* A vector3 or color: a Double (or Float) array of 3 or a UInt8 array of 4. */
static Error ReadFixedArray(const GHDFRead* read, DataType type, GHDFArray* array, DataValue* outValue)
{
    bool IsVector = type == DataType_Vector3;
    size_t Expected = IsVector ? GHDF_VECTOR3_COMPONENTS : GHDF_COLOR_CHANNELS;
    GHDFValueType ArrayType = GHDFArray_GetElementType(array);
    bool IsTypeValid = IsVector ? IsVector3ArrayType(ArrayType) : (ArrayType == GHDFValueType_UInt8);
    if (!IsTypeValid || (GHDFArray_GetElementCount(array) != Expected))
    {
        return PathError(read, IsVector ? "a vector3 must be a Double (or Float) array of 3"
            : "a color must be a UInt8 array of 4");
    }
    double Components[GHDF_VECTOR3_COMPONENTS] = { 0.0, 0.0, 0.0 };
    uint8_t Channels[GHDF_COLOR_CHANNELS] = { 0U, 0U, 0U, 0U };
    for (size_t Index = 0U; Index < Expected; Index++)
    {
        GHDFObjectValue Element = { .Type = GHDFValueType_None };
        Error Result = GHDFArray_Get(array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
        }
        if (IsVector)
        {
            Components[Index] = GetVector3Component(&Element);
        }
        else
        {
            Channels[Index] = Element.Value.UInt8;
        }
    }
    Vector3Double Vector = Vector3Double_Create(Components[0], Components[1], Components[2]);
    if (IsVector && !Vector3Double_IsFinite(Vector))
    {
        return PathError(read, "every component of a vector3 must be finite");
    }
    *outValue = IsVector ? DataValue_FromVector3(Vector)
        : DataValue_FromColor((Color){ Channels[0], Channels[1], Channels[2], Channels[3] });
    return Error_CreateSuccess();
}


// Static functions: reading lists.
/* Whether an array of @p arrayType can hold elements of @p elementType, and how many array elements one takes. */
static bool DoesArrayFit(DataType elementType, GHDFValueType arrayType, size_t* outStride)
{
    *outStride = 1U;
    switch (elementType)
    {
        case DataType_Bool:
            return arrayType == GHDFValueType_Boolean;
        case DataType_Int:
        case DataType_Object:
        case DataType_DateTime:
            return GameGHDF_IsIntegerType(arrayType);
        case DataType_Float:
            return (arrayType == GHDFValueType_Float) || (arrayType == GHDFValueType_Double);
        case DataType_String:
        case DataType_Key:
            return arrayType == GHDFValueType_String;
        case DataType_Vector3:
            *outStride = GHDF_VECTOR3_COMPONENTS;
            return IsVector3ArrayType(arrayType);
        case DataType_Color:
            *outStride = GHDF_COLOR_CHANNELS;
            return arrayType == GHDFValueType_UInt8;
        case DataType_List:
        case DataType_Compound:
            return arrayType == GHDFValueType_Compound;
        case DataType_Tag:
            break;
    }
    return false;
}

/* Reads list element @p index (taking @p stride array elements) as @p elementType. */
static Error ReadListElement(GHDFRead* read, GHDFArray* array, size_t index, size_t stride, DataType elementType,
    size_t level, DataValue* outElement)
{
    GHDFObjectValue First = { .Type = GHDFValueType_None };
    Error Result = GHDFArray_Get(array, index * stride, &First);
    if (Result.Code != ErrorCode_Success)
    {
        return DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
    }
    if ((elementType == DataType_List) || (elementType == DataType_Compound))
    {
        Result = ReadValue(read, First.Value.Compound, level, outElement);
        if ((Result.Code == ErrorCode_Success) && (outElement->Type != elementType))
        {
            DataValue_Deconstruct(outElement);
            return PathError(read, "the element's type is not the list's element type");
        }
        return Result;
    }
    if ((elementType != DataType_Vector3) && (elementType != DataType_Color))
    {
        Result = ReadScalar(read, elementType, &First, outElement);
        return (Result.Code == ErrorCode_Success) ? ChargeValue(read, outElement) : Result;
    }

    double Components[GHDF_VECTOR3_COMPONENTS] = { 0.0, 0.0, 0.0 };
    uint8_t Channels[GHDF_COLOR_CHANNELS] = { 0U, 0U, 0U, 0U };
    for (size_t Part = 0U; Part < stride; Part++)
    {
        GHDFObjectValue Element = { .Type = GHDFValueType_None };
        Result = GHDFArray_Get(array, (index * stride) + Part, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
        }
        if (elementType == DataType_Vector3)
        {
            Components[Part] = GetVector3Component(&Element);
        }
        else
        {
            Channels[Part] = Element.Value.UInt8;
        }
    }
    Vector3Double Vector = Vector3Double_Create(Components[0], Components[1], Components[2]);
    if ((elementType == DataType_Vector3) && !Vector3Double_IsFinite(Vector))
    {
        return PathError(read, "every component of a vector3 must be finite");
    }
    *outElement = (elementType == DataType_Vector3) ? DataValue_FromVector3(Vector)
        : DataValue_FromColor((Color){ Channels[0], Channels[1], Channels[2], Channels[3] });
    return ChargeValue(read, outElement);
}

static Error ReadList(GHDFRead* read, const GHDFValueEntries* entries, size_t level, DataValue* outValue)
{
    DataType ElementType = DataType_Tag;
    if (!TryReadType(entries->HasElementType, &entries->ElementType, &ElementType) || (ElementType == DataType_Tag))
    {
        return PathError(read, "a list needs its element type (entry 4: a UInt8 data type other than tag)");
    }
    size_t Stride = 1U;
    if (!entries->HasValue || !entries->Value.EntryType.IsArray
        || !DoesArrayFit(ElementType, entries->Value.EntryType.ValueType, &Stride))
    {
        char Problem[GHDF_PROBLEM_CAPACITY];
        snprintf(Problem, sizeof(Problem), "a list of %s needs an array of the matching GHDF type",
            (const char*)DataType_GetName(ElementType));
        return PathError(read, Problem);
    }
    GHDFArray* Array = entries->Value.Value.Value.Array;
    size_t ArrayCount = GHDFArray_GetElementCount(Array);
    if (((ArrayCount % Stride) != 0U) || ((ArrayCount / Stride) > DATA_VALUE_MAX_ELEMENT_COUNT))
    {
        return PathError(read, "a list holds at most 4096 elements, each whole");
    }

    DataValue List = DataValue_CreateTag();
    Error Result = Charge(read, 1U); // the list's own byte (DataValue_GetByteSize)
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataValue_CreateList(ElementType, &List);
    }
    for (size_t Index = 0U; (Index < (ArrayCount / Stride)) && (Result.Code == ErrorCode_Success); Index++)
    {
        DataValue Element = DataValue_CreateTag();
        DataPath_PushIndex(read->Path, Index);
        Result = ReadListElement(read, Array, Index, Stride, ElementType, level + 1U, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValue_ListAdd(&List, &Element);
            if (Result.Code != ErrorCode_Success)
            {
                Result = DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
            }
        }
        DataPath_Pop(read->Path);
        DataValue_Deconstruct(&Element);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DataValue_Deconstruct(&List);
        return Result;
    }
    *outValue = List;
    return Error_CreateSuccess();
}


// Static functions: reading compounds.
static Error ReadCompoundEntry(GHDFRead* read, GHDFCompound* entry, size_t level, DataValue* compound)
{
    const unsigned char* Name = NULL;
    size_t NameLength = 0U;
    Error Result = DataValueInternal_GetGHDFName(entry, &Name, &NameLength);
    if (Result.Code != ErrorCode_Success)
    {
        return DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
    }
    DataPath_PushName(read->Path, Name, NameLength);
    DataValue Value = DataValue_CreateTag();
    if (!DataValueInternal_IsNameValid(Name, NameLength))
    {
        Result = PathError(read, "an entry's name must be 1 to 256 bytes of UTF-8, starting with '$' only as \"$type\"");
    }
    else if (DataValue_FindCompoundEntry(compound, Name) != NULL)
    {
        Result = PathError(read, "the name appears twice");
    }
    else
    {
        Result = Charge(read, NameLength + 1U); // the entry's name and type byte (DataValue_GetByteSize)
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadValue(read, entry, level, &Value);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataValue_CompoundSet(compound, Name, &Value);
        if (Result.Code != ErrorCode_Success)
        {
            Result = DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
        }
    }
    DataPath_Pop(read->Path);
    DataValue_Deconstruct(&Value);
    return Result;
}

static Error ReadCompound(GHDFRead* read, const GHDFValueEntries* entries, size_t level, DataValue* outValue)
{
    if (!entries->HasValue || !entries->Value.EntryType.IsArray
        || (entries->Value.EntryType.ValueType != GHDFValueType_Compound))
    {
        return PathError(read, "a compound needs a Compound array of its entries");
    }
    GHDFArray* Array = entries->Value.Value.Value.Array;
    size_t Count = GHDFArray_GetElementCount(Array);
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
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Entry = { .Type = GHDFValueType_None };
        Result = GHDFArray_Get(Array, Index, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadCompoundEntry(read, Entry.Value.Compound, level + 1U, &Compound);
        }
        else
        {
            Result = DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        DataValue_Deconstruct(&Compound);
        return Result;
    }
    *outValue = Compound;
    return Error_CreateSuccess();
}

/* Reads a value compound of any type (ReadValue checks the depth first and charges the result). */
static Error ReadAnyValue(GHDFRead* read, GHDFCompound* compound, size_t level, DataValue* outValue)
{
    GHDFValueEntries Entries;
    Error Result = CollectEntries(compound, &Entries);
    if (Result.Code != ErrorCode_Success)
    {
        return DataPath_WrapError(read->Path, ErrorCode_Deserialize, &Result);
    }
    DataType Type = DataType_Tag;
    if (!TryReadType(Entries.HasType, &Entries.Type, &Type))
    {
        return PathError(read, "a value needs its type (entry 2: a UInt8 data type)");
    }

    switch (Type)
    {
        case DataType_Tag:
            return Entries.HasValue ? PathError(read, "a tag has no value") : Error_CreateSuccess();
        case DataType_List:
            return ReadList(read, &Entries, level, outValue);
        case DataType_Compound:
            return ReadCompound(read, &Entries, level, outValue);
        case DataType_Vector3:
        case DataType_Color:
            return (Entries.HasValue && Entries.Value.EntryType.IsArray)
                ? ReadFixedArray(read, Type, Entries.Value.Value.Value.Array, outValue)
                : PathError(read, (Type == DataType_Vector3) ? "a vector3 must be a Double (or Float) array of 3"
                    : "a color must be a UInt8 array of 4");
        case DataType_Bool:
        case DataType_Int:
        case DataType_Float:
        case DataType_String:
        case DataType_Key:
        case DataType_Object:
        case DataType_DateTime:
            break;
    }
    if (!Entries.HasValue || Entries.Value.EntryType.IsArray)
    {
        char Problem[GHDF_PROBLEM_CAPACITY];
        snprintf(Problem, sizeof(Problem), "a %s needs its value (entry 3)", (const char*)DataType_GetName(Type));
        return PathError(read, Problem);
    }
    return ReadScalar(read, Type, &Entries.Value.Value, outValue);
}

static Error ReadValue(GHDFRead* read, GHDFCompound* compound, size_t level, DataValue* outValue)
{
    *outValue = DataValue_CreateTag();
    if (level > DATA_VALUE_MAX_DEPTH)
    {
        return PathError(read, "a value nests at most 16 levels deep");
    }
    Error Result = ReadAnyValue(read, compound, level, outValue);
    if ((Result.Code == ErrorCode_Success) && (outValue->Type != DataType_List) && (outValue->Type != DataType_Compound))
    {
        Result = ChargeValue(read, outValue);
    }
    return Result;
}


// Internal functions (DataValueInternal.h).
Error DataValueInternal_PutGHDFString(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id,
    const unsigned char* text)
{
    Error Result = Error_CreateSuccess();
    GenericBuffer* Buffer = CreateString(pool, text, &Result);
    if (Buffer == NULL)
    {
        return Result;
    }
    GHDFObjectValue Value = GHDFObjectValue_CreateString(Buffer);
    Result = GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_String), &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

Error DataValueInternal_GetGHDFName(GHDFCompound* compound, const unsigned char** outBytes, size_t* outByteCount)
{
    GHDFObjectValue Value = { .Type = GHDFValueType_None };
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(compound, DATA_GHDF_NAME, GHDF_CreateRegularType(GHDFValueType_String),
        &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound || !TryGetStringBytes(&Value, outBytes, outByteCount))
    {
        Error_Deconstruct(&Result);
        return Error_Construct2(ErrorCode_Deserialize, "An entry needs its name (entry 1: a string without NULs).");
    }
    return Error_CreateSuccess();
}

Error DataValueInternal_ReadGHDF(GHDFCompound* compound, KeyRegistry* keys, size_t level, DataPath* path,
    DataBudget* budget, DataValue* outValue)
{
    GHDFRead Read = { .Keys = keys, .Path = path, .Budget = budget };
    return ReadValue(&Read, compound, level, outValue);
}

Error DataValueInternal_WriteGHDF(const DataValue* value, const KeyRegistry* keys, GHDFObjectPool* pool,
    GHDFCompound* compound)
{
    Error Result = PutScalar(compound, DATA_GHDF_TYPE, GHDFObjectValue_CreateUInt8((uint8_t)value->Type));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    switch (value->Type)
    {
        case DataType_Tag:
            return Error_CreateSuccess();
        case DataType_Bool:
            return PutScalar(compound, DATA_GHDF_VALUE, GHDFObjectValue_CreateBoolean(value->BoolValue));
        case DataType_Int:
            return PutScalar(compound, DATA_GHDF_VALUE,
                GameGHDF_CreateSigned(GameGHDF_GetSmallestSignedType(value->IntValue, value->IntValue), value->IntValue));
        case DataType_Float:
            return PutScalar(compound, DATA_GHDF_VALUE, IsExactFloat(value->FloatValue)
                ? GHDFObjectValue_CreateFloat((float)value->FloatValue) : GHDFObjectValue_CreateDouble(value->FloatValue));
        case DataType_String:
            return DataValueInternal_PutGHDFString(pool, compound, DATA_GHDF_VALUE, value->StringValue);
        case DataType_Key:
            return DataValueInternal_PutGHDFString(pool, compound, DATA_GHDF_VALUE,
                KeyRegistry_GetText(keys, value->KeyValue));
        case DataType_Object:
            return PutScalar(compound, DATA_GHDF_VALUE,
                GameGHDF_CreateUnsigned(GameGHDF_GetSmallestUnsignedType(value->ObjectValue), value->ObjectValue));
        case DataType_DateTime:
            return PutScalar(compound, DATA_GHDF_VALUE, GHDFObjectValue_CreateInt64(value->DateTimeValue));
        case DataType_Vector3:
        case DataType_Color:
            return PutFixedArray(pool, compound, value);
        case DataType_List:
            return PutList(value, keys, pool, compound);
        case DataType_Compound:
            return PutCompound(value, keys, pool, compound);
    }
    return Error_Construct2(ErrorCode_InvalidState, "The value has an unknown type.");
}


// Public functions.
Error DataValue_WriteGHDF(const DataValue* value, const KeyRegistry* keys, GHDFObjectPool* pool,
    GHDFCompound* compound)
{
    if ((value == NULL) || (keys == NULL) || (pool == NULL) || (compound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataValue_WriteGHDF: value, keys, pool and compound must not be NULL.");
    }
    Error Result = DataValue_Validate(value, keys);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return DataValueInternal_WriteGHDF(value, keys, pool, compound);
}

Error DataValue_ReadGHDF(GHDFCompound* compound, KeyRegistry* keys, DataValue* outValue)
{
    if (outValue != NULL)
    {
        *outValue = DataValue_CreateTag();
    }
    if ((compound == NULL) || (keys == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "DataValue_ReadGHDF: compound, keys and outValue must not be NULL.");
    }
    // All or nothing: a refused value forgets the keys it interned.
    size_t KeyCheckpoint = KeyRegistry_GetCount(keys);
    DataPath Path = DataPath_Create();
    Error Result = DataValueInternal_ReadGHDF(compound, keys, 1U, &Path, NULL, outValue);
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataValueInternal_CheckRead(outValue, keys, &Path, ErrorCode_Deserialize);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DataValue_Deconstruct(outValue);
        KeyRegistry_TruncateTo(keys, KeyCheckpoint);
    }
    return Result;
}
