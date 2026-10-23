// GHDF reading and writing of property tables (see PropertyDescriptor.h for the entry types). The entry ids are the
// descriptors' wire ids; the layout of colors and vectors matches what the hand-written world encoder wrote. Integers
// are written in the smallest GHDF type that holds them and read from any integer type that holds the value (GameGHDF).

#include <inttypes.h>
#include "PropertyDescriptorInternal.h"
#include "DataMap.h"
#include "GameGHDF.h"
#include "NamespacedKey.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** Entry ids inside a render color's compound. */
#define PROPERTY_GHDF_RENDER_COLOR_TINT ((GHDFEntryID)1)
#define PROPERTY_GHDF_RENDER_COLOR_BRIGHTNESS ((GHDFEntryID)2)
#define PROPERTY_GHDF_RENDER_COLOR_OPACITY ((GHDFEntryID)3)
/** Channels written per color: RGBA for a color, RGB for a render color's tint (its alpha is not part of it). */
#define PROPERTY_GHDF_COLOR_CHANNEL_COUNT 4U
#define PROPERTY_GHDF_TINT_CHANNEL_COUNT 3U
/** The most components of a written float or double array (a Vector3, a Vector3Double). */
#define PROPERTY_GHDF_MAX_VECTOR_COMPONENTS 3U


// Static functions: writing.
static Error PutScalar(GHDFCompound* compound, GHDFEntryID id, GHDFObjectValue value)
{
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(value.Type), &value);
}

/* An array entry of @p count elements of @p elementType, built from values the caller made. */
static Error PutArray(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, GHDFValueType elementType,
    const GHDFObjectValue* elements, size_t count)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, elementType, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (size_t Index = 0; (Index < count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = GHDFArray_AddValue(Array, &elements[Index]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Array, elementType);
        Result = GHDFCompound_SetValue(compound, id, GHDF_CreateArrayType(elementType), &ArrayValue);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

static Error PutFloats(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const float* components, size_t count)
{
    GHDFObjectValue Elements[PROPERTY_GHDF_MAX_VECTOR_COMPONENTS];
    for (size_t Index = 0; Index < count; Index++)
    {
        Elements[Index] = GHDFObjectValue_CreateFloat(components[Index]);
    }
    return PutArray(pool, compound, id, GHDFValueType_Float, Elements, count);
}

static Error PutDoubles(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const double* components,
    size_t count)
{
    GHDFObjectValue Elements[PROPERTY_GHDF_MAX_VECTOR_COMPONENTS];
    for (size_t Index = 0; Index < count; Index++)
    {
        Elements[Index] = GHDFObjectValue_CreateDouble(components[Index]);
    }
    return PutArray(pool, compound, id, GHDFValueType_Double, Elements, count);
}

static Error PutChannels(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, Color color, size_t count)
{
    const unsigned char Channels[PROPERTY_GHDF_COLOR_CHANNEL_COUNT] = { color.r, color.g, color.b, color.a };
    GHDFObjectValue Elements[PROPERTY_GHDF_COLOR_CHANNEL_COUNT];
    for (size_t Index = 0; Index < count; Index++)
    {
        Elements[Index] = GHDFObjectValue_CreateUInt8(Channels[Index]);
    }
    return PutArray(pool, compound, id, GHDFValueType_UInt8, Elements, count);
}

/* { 1: UInt8[3] tint, 2: Float brightness, 3: Float opacity } */
static Error PutRenderColor(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, RenderColor color)
{
    GHDFCompound* ColorCompound = NULL;
    Error Result = GHDFObjectPool_BorrowCompound(pool, &ColorCompound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PutChannels(pool, ColorCompound, PROPERTY_GHDF_RENDER_COLOR_TINT, color.Tint, PROPERTY_GHDF_TINT_CHANNEL_COUNT);
    if (Result.Code == ErrorCode_Success)
    {
        Result = PutScalar(ColorCompound, PROPERTY_GHDF_RENDER_COLOR_BRIGHTNESS, GHDFObjectValue_CreateFloat(color.Brightness));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PutScalar(ColorCompound, PROPERTY_GHDF_RENDER_COLOR_OPACITY, GHDFObjectValue_CreateFloat(color.Opacity));
    }
    if (Result.Code == ErrorCode_Success)
    {
        GHDFObjectValue Value = GHDFObjectValue_CreateCompound(ColorCompound);
        Result = GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &Value);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, ColorCompound, true);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

/* A Float array of the list's values (empty for the empty list). */
static Error PutFloatList(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const PropertyFloatList* list)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_Float, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const float* Values = PropertyFloatList_GetValues(list);
    for (size_t Index = 0; (Index < PropertyFloatList_GetCount(list)) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Element = GHDFObjectValue_CreateFloat(Values[Index]);
        Result = GHDFArray_AddValue(Array, &Element);
    }
    if (Result.Code == ErrorCode_Success)
    {
        GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Array, GHDFValueType_Float);
        Result = GHDFCompound_SetValue(compound, id, GHDF_CreateArrayType(GHDFValueType_Float), &ArrayValue);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

/* A Compound array of the map's entries (see DataMap.h). */
static Error PutDataMap(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const DataMap* map)
{
    GHDFArray* Array = NULL;
    Error Result = DataMap_WriteGHDF(map, pool, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFObjectValue Value = GHDFObjectValue_CreateArray(Array, GHDFValueType_Compound);
    Result = GHDFCompound_SetValue(compound, id, GHDF_CreateArrayType(GHDFValueType_Compound), &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
    }
    return Result;
}

static Error PutValue(GHDFObjectPool* pool, GHDFCompound* compound, const PropertyDescriptor* descriptor,
    const PropertyData* data)
{
    GHDFEntryID Id = (GHDFEntryID)descriptor->WireId;
    switch (descriptor->Type)
    {
        case PropertyType_Bool:
            return PutScalar(compound, Id, GHDFObjectValue_CreateBoolean(data->BoolValue));
        case PropertyType_Int32:
            return GameGHDF_PutSigned(compound, Id, (int64_t)data->Int32Value);
        case PropertyType_UInt32:
            return GameGHDF_PutUnsigned(compound, Id, (uint64_t)data->UInt32Value);
        case PropertyType_UInt64:
            return GameGHDF_PutUnsigned(compound, Id, data->UInt64Value);
        case PropertyType_ObjectId:
            return GameGHDF_PutUnsigned(compound, Id, data->ObjectIdValue);
        case PropertyType_Float:
        case PropertyType_Angle:
            return PutScalar(compound, Id, GHDFObjectValue_CreateFloat(data->FloatValue));
        case PropertyType_Double:
            return PutScalar(compound, Id, GHDFObjectValue_CreateDouble(data->DoubleValue));
        case PropertyType_Date:
            // Variable length: a date of this era takes three bytes instead of eight.
            return PutScalar(compound, Id, GHDFObjectValue_CreateEncodedInteger(data->DateValue));
        case PropertyType_Vector2:
        {
            const float Components[2] = { data->Vector2Value.x, data->Vector2Value.y };
            return PutFloats(pool, compound, Id, Components, 2U);
        }
        case PropertyType_Vector3:
        case PropertyType_AngleVector3:
        {
            const float Components[3] = { data->Vector3Value.x, data->Vector3Value.y, data->Vector3Value.z };
            return PutFloats(pool, compound, Id, Components, 3U);
        }
        case PropertyType_Vector3Double:
        {
            const double Components[3] = { data->Vector3DoubleValue.X, data->Vector3DoubleValue.Y,
                data->Vector3DoubleValue.Z };
            return PutDoubles(pool, compound, Id, Components, 3U);
        }
        case PropertyType_Color:
            return PutChannels(pool, compound, Id, data->ColorValue, PROPERTY_GHDF_COLOR_CHANNEL_COUNT);
        case PropertyType_RenderColor:
            return PutRenderColor(pool, compound, Id, data->RenderColorValue);
        case PropertyType_String:
        case PropertyType_AssetName:
            // GHDF has no null: a NULL string is left out, which reads back as its NULL default.
            return (data->StringValue != NULL) ? GameGHDF_PutString(pool, compound, Id, data->StringValue) : Error_CreateSuccess();
        case PropertyType_Enum:
            return PutScalar(compound, Id, GHDFObjectValue_CreateEncodedInteger((int64_t)data->EnumValue));
        case PropertyType_DataMap:
            return PutDataMap(pool, compound, Id, data->DataMapValue);
        case PropertyType_FloatList:
            return PutFloatList(pool, compound, Id, data->FloatListValue);
        case PropertyType_EnumSet:
            return GameGHDF_PutUnsigned(compound, Id, (uint64_t)data->EnumSetValue);
    }
    return Error_Construct2(ErrorCode_InvalidState, "The property has an unknown type.");
}


// Static functions: reading.
/* An array entry with exactly @p count elements of @p elementType; *outFound false when the entry is absent. */
static Error GetArray(GHDFCompound* compound, GHDFEntryID id, GHDFValueType elementType, size_t count, GHDFArray** outArray,
    bool* outFound)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateArrayType(elementType), &Entry, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }
    if (GHDFArray_GetElementCount(Entry.Value.Array) != count)
    {
        return Error_Construct4(ErrorCode_Deserialize, "Expected an array of %u elements.", (unsigned int)count);
    }
    *outArray = Entry.Value.Array;
    return Error_CreateSuccess();
}

static Error GetFloats(GHDFCompound* compound, GHDFEntryID id, float* outComponents, size_t count, bool* outFound)
{
    GHDFArray* Array = NULL;
    Error Result = GetArray(compound, id, GHDFValueType_Float, count, &Array, outFound);
    for (size_t Index = 0; (Index < count) && (Result.Code == ErrorCode_Success) && *outFound; Index++)
    {
        GHDFObjectValue Element = { .Type = GHDFValueType_None };
        Result = GHDFArray_Get(Array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            outComponents[Index] = Element.Value.Float;
        }
    }
    return Result;
}

/* A Double array of @p count elements, or a Float array of them (written by a float row before the property became
 * double: each float widens exactly). */
static Error GetDoubles(GHDFCompound* compound, GHDFEntryID id, double* outComponents, size_t count, bool* outFound)
{
    GHDFObjectValue Entry = { .Type = GHDFValueType_None };
    Error Result = GHDFCompound_GetOptional(compound, id, &Entry, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }
    bool IsFloat = Entry.Type == GHDFValueType_Float;
    GHDFArray* Array = NULL;
    Result = GetArray(compound, id, IsFloat ? GHDFValueType_Float : GHDFValueType_Double, count, &Array, outFound);
    for (size_t Index = 0; (Index < count) && (Result.Code == ErrorCode_Success) && *outFound; Index++)
    {
        GHDFObjectValue Element = { .Type = GHDFValueType_None };
        Result = GHDFArray_Get(Array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            outComponents[Index] = IsFloat ? (double)Element.Value.Float : Element.Value.Double;
        }
    }
    return Result;
}

static Error GetChannels(GHDFCompound* compound, GHDFEntryID id, size_t count, Color* outColor, bool* outFound)
{
    GHDFArray* Array = NULL;
    Error Result = GetArray(compound, id, GHDFValueType_UInt8, count, &Array, outFound);
    unsigned char Channels[PROPERTY_GHDF_COLOR_CHANNEL_COUNT] = { 0U, 0U, 0U, 255U };
    for (size_t Index = 0; (Index < count) && (Result.Code == ErrorCode_Success) && *outFound; Index++)
    {
        GHDFObjectValue Element = { .Type = GHDFValueType_None };
        Result = GHDFArray_Get(Array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Channels[Index] = Element.Value.UInt8;
        }
    }
    *outColor = (Color){ .r = Channels[0], .g = Channels[1], .b = Channels[2], .a = Channels[3] };
    return Result;
}

static Error GetRequiredFloat(GHDFCompound* compound, GHDFEntryID id, float* outValue)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Float), &Entry);
    if (Result.Code == ErrorCode_Success)
    {
        *outValue = Entry.Value.Float;
    }
    return Result;
}

static Error GetRenderColor(GHDFCompound* compound, GHDFEntryID id, RenderColor* outColor, bool* outFound)
{
    GHDFObjectValue Entry;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &Entry,
        outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }

    bool IsTintFound = false;
    Result = GetChannels(Entry.Value.Compound, PROPERTY_GHDF_RENDER_COLOR_TINT, PROPERTY_GHDF_TINT_CHANNEL_COUNT,
        &outColor->Tint, &IsTintFound);
    if ((Result.Code == ErrorCode_Success) && !IsTintFound)
    {
        return Error_Construct2(ErrorCode_Deserialize, "The render color has no tint.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequiredFloat(Entry.Value.Compound, PROPERTY_GHDF_RENDER_COLOR_BRIGHTNESS, &outColor->Brightness);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequiredFloat(Entry.Value.Compound, PROPERTY_GHDF_RENDER_COLOR_OPACITY, &outColor->Opacity);
    }
    return Result;
}

/* A GHDF string's count includes its terminator; a NUL before it would cut the text short. The other string rules are
 * checked when the read commits. */
static Error ReadString(GHDFCompound* compound, const PropertyDescriptor* descriptor, void* target)
{
    const unsigned char* Source = NULL;
    size_t ByteCount = 0U;
    bool IsFound = false;
    Error Result = GameGHDF_GetOptionalString(compound, (GHDFEntryID)descriptor->WireId, &Source, &ByteCount, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    if (ByteCount > descriptor->Rules.MaxLength)
    {
        return Error_Construct4(ErrorCode_Deserialize, "The string is longer than the %" PRIu64 " bytes allowed.",
            (uint64_t)descriptor->Rules.MaxLength);
    }
    unsigned char* Text = Memory_Allocate(ByteCount + 1U);
    Memory_Copy(Source, Text, ByteCount + 1U);
    PropertyInternal_AdoptOwned(descriptor, target, Text);
    return Error_CreateSuccess();
}

/* A data map from its Compound array, interning its keys in the options' registry. */
static Error ReadDataMap(GHDFCompound* compound, const PropertyDescriptor* descriptor, const PropertyReadOptions* options,
    void* target)
{
    GHDFObjectValue Entry;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(compound, (GHDFEntryID)descriptor->WireId,
        GHDF_CreateArrayType(GHDFValueType_Compound), &Entry, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    if (options->Keys == NULL)
    {
        return Error_Construct2(ErrorCode_Deserialize,
            "Reading tags and data needs the key registry (PropertyReadOptions.Keys).");
    }
    DataMapLimits Limits = { .MaxEntryCount = descriptor->Rules.MaxCount, .MaxByteSize = descriptor->Rules.MaxLength };
    DataMap* Map = NULL;
    Result = DataMap_ReadGHDF(Entry.Value.Array, options->Keys, &Limits, &Map);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    PropertyInternal_AdoptOwned(descriptor, target, Map);
    return Error_CreateSuccess();
}

/* A float list from its Float array, its length checked before anything is allocated (the values' rules are checked when
 * the read commits); an empty array is the empty list. */
static Error ReadFloatList(GHDFCompound* compound, const PropertyDescriptor* descriptor, void* target)
{
    GHDFObjectValue Entry;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(compound, (GHDFEntryID)descriptor->WireId,
        GHDF_CreateArrayType(GHDFValueType_Float), &Entry, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    size_t Count = GHDFArray_GetElementCount(Entry.Value.Array);
    if (Count > descriptor->Rules.MaxCount)
    {
        return Error_Construct4(ErrorCode_Deserialize, "The list has %" PRIu64 " values; at most %" PRIu64
            " are allowed.", (uint64_t)Count, (uint64_t)descriptor->Rules.MaxCount);
    }
    if (Count == 0U)
    {
        PropertyInternal_AdoptOwned(descriptor, target, NULL);
        return Error_CreateSuccess();
    }
    PropertyFloatList* List = PropertyInternal_AllocateFloatList(Count);
    for (size_t Index = 0; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Element = { .Type = GHDFValueType_None };
        Result = GHDFArray_Get(Entry.Value.Array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            List->_values[Index] = Element.Value.Float;
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        PropertyFloatList_Destroy(List);
        return Result;
    }
    PropertyInternal_AdoptOwned(descriptor, target, List);
    return Error_CreateSuccess();
}

/* Reads a scalar entry of @p type into *outEntry; *outFound false when absent, and on an error. */
static Error GetScalar(GHDFCompound* compound, GHDFEntryID id, GHDFValueType type, GHDFObjectValue* outEntry,
    bool* outFound)
{
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(type), outEntry, outFound);
    if (Result.Code != ErrorCode_Success)
    {
        // An entry of another type is found but refused, and *outEntry holds that type's value: reading it as this
        // type would be wrong (a Boolean that is not 0 or 1 is undefined behaviour).
        *outFound = false;
    }
    return Result;
}

/* The non-string types; leaves *outFound false when the entry is absent (the property keeps its default). */
static Error ParseEntry(GHDFCompound* compound, const PropertyDescriptor* descriptor, PropertyData* outData, bool* outFound)
{
    GHDFEntryID Id = (GHDFEntryID)descriptor->WireId;
    GHDFObjectValue Entry = { .Type = GHDFValueType_None };
    Error Result = Error_CreateSuccess();
    switch (descriptor->Type)
    {
        case PropertyType_Bool:
            Result = GetScalar(compound, Id, GHDFValueType_Boolean, &Entry, outFound);
            outData->BoolValue = *outFound && Entry.Value.Boolean;
            return Result;
        case PropertyType_Int32:
        {
            int64_t Value = 0;
            Result = GameGHDF_GetOptionalSigned(compound, Id, (int64_t)INT32_MIN, (int64_t)INT32_MAX, &Value, outFound);
            outData->Int32Value = *outFound ? (int32_t)Value : 0;
            return Result;
        }
        case PropertyType_UInt32:
        {
            uint64_t Value = 0U;
            Result = GameGHDF_GetOptionalUnsigned(compound, Id, (uint64_t)UINT32_MAX, &Value, outFound);
            outData->UInt32Value = *outFound ? (uint32_t)Value : 0U;
            return Result;
        }
        case PropertyType_UInt64:
            outData->UInt64Value = 0U;
            return GameGHDF_GetOptionalUnsigned(compound, Id, UINT64_MAX, &outData->UInt64Value, outFound);
        case PropertyType_ObjectId:
            outData->ObjectIdValue = 0U;
            return GameGHDF_GetOptionalUnsigned(compound, Id, UINT64_MAX, &outData->ObjectIdValue, outFound);
        case PropertyType_Float:
        case PropertyType_Angle:
            Result = GetScalar(compound, Id, GHDFValueType_Float, &Entry, outFound);
            outData->FloatValue = *outFound ? Entry.Value.Float : 0.0f;
            return Result;
        case PropertyType_Double:
            Result = GetScalar(compound, Id, GHDFValueType_Double, &Entry, outFound);
            outData->DoubleValue = *outFound ? Entry.Value.Double : 0.0;
            return Result;
        case PropertyType_Date:
            // Its range is checked when the read commits, like every rule.
            Result = GetScalar(compound, Id, GHDFValueType_EncodedInteger, &Entry, outFound);
            outData->DateValue = *outFound ? Entry.Value.EncodedInteger : 0;
            return Result;
        case PropertyType_Vector2:
        {
            float Components[2] = { 0.0f, 0.0f };
            Result = GetFloats(compound, Id, Components, 2U, outFound);
            outData->Vector2Value = (Vector2){ .x = Components[0], .y = Components[1] };
            return Result;
        }
        case PropertyType_Vector3:
        case PropertyType_AngleVector3:
        {
            float Components[3] = { 0.0f, 0.0f, 0.0f };
            Result = GetFloats(compound, Id, Components, 3U, outFound);
            outData->Vector3Value = (Vector3){ .x = Components[0], .y = Components[1], .z = Components[2] };
            return Result;
        }
        case PropertyType_Vector3Double:
        {
            double Components[3] = { 0.0, 0.0, 0.0 };
            Result = GetDoubles(compound, Id, Components, 3U, outFound);
            outData->Vector3DoubleValue = Vector3Double_Create(Components[0], Components[1], Components[2]);
            return Result;
        }
        case PropertyType_Color:
            return GetChannels(compound, Id, PROPERTY_GHDF_COLOR_CHANNEL_COUNT, &outData->ColorValue, outFound);
        case PropertyType_RenderColor:
            return GetRenderColor(compound, Id, &outData->RenderColorValue, outFound);
        case PropertyType_String:
        case PropertyType_AssetName:
            return Error_Construct2(ErrorCode_InvalidState, "Strings are read separately.");
        case PropertyType_Enum:
            Result = GetScalar(compound, Id, GHDFValueType_EncodedInteger, &Entry, outFound);
            if ((Result.Code == ErrorCode_Success) && *outFound
                && ((Entry.Value.EncodedInteger < (int64_t)INT32_MIN) || (Entry.Value.EncodedInteger > (int64_t)INT32_MAX)))
            {
                return Error_Construct2(ErrorCode_Deserialize, "The enum value is out of range.");
            }
            outData->EnumValue = *outFound ? (int32_t)Entry.Value.EncodedInteger : 0;
            return Result;
        case PropertyType_DataMap:
            return Error_Construct2(ErrorCode_InvalidState, "Data maps are read separately.");
        case PropertyType_FloatList:
            return Error_Construct2(ErrorCode_InvalidState, "Float lists are read separately.");
        case PropertyType_EnumSet:
        {
            // Bits outside the enum are refused when the read commits, like every rule.
            uint64_t Value = 0U;
            Result = GameGHDF_GetOptionalUnsigned(compound, Id, (uint64_t)UINT32_MAX, &Value, outFound);
            outData->EnumSetValue = *outFound ? (uint32_t)Value : 0U;
            return Result;
        }
    }
    return Error_Construct2(ErrorCode_InvalidState, "The property has an unknown type.");
}

static Error ReadEntry(const PropertyTable* table, GHDFCompound* compound, const PropertyDescriptor* descriptor,
    const PropertyReadOptions* options, void* target)
{
    Error Result = Error_CreateSuccess();
    if (descriptor->Type == PropertyType_DataMap)
    {
        Result = ReadDataMap(compound, descriptor, options, target);
    }
    else if (descriptor->Type == PropertyType_FloatList)
    {
        Result = ReadFloatList(compound, descriptor, target);
    }
    else if (PropertyInternal_IsStringType(descriptor->Type))
    {
        Result = ReadString(compound, descriptor, target);
    }
    else
    {
        PropertyData Data;
        Memory_Zero(&Data, sizeof(Data));
        bool IsFound = false;
        Result = ParseEntry(compound, descriptor, &Data, &IsFound);
        if ((Result.Code == ErrorCode_Success) && IsFound)
        {
            PropertyInternal_WriteField(descriptor, target, &Data);
        }
    }
    return (Result.Code == ErrorCode_Success) ? Result
        : PropertyInternal_WrapError(ErrorCode_Deserialize, table, descriptor, &Result);
}


// Public functions.
Error Properties_WriteGHDF(const PropertyTable* table, const void* instance, GHDFObjectPool* pool, GHDFCompound* compound,
    const PropertyWriteOptions* options)
{
    if ((table == NULL) || (instance == NULL) || (pool == NULL) || (compound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_WriteGHDF: table, instance, pool and compound must not be NULL.");
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
        Result = PutValue(pool, compound, Descriptor, &Data);
        if (Result.Code != ErrorCode_Success)
        {
            return PropertyInternal_WrapError(Result.Code, table, Descriptor, &Result);
        }
    }
    return Error_CreateSuccess();
}

Error Properties_ReadGHDF(const PropertyTable* table, GHDFCompound* compound, void* instance,
    const PropertyReadOptions* options)
{
    // TODO: no GHDF migration exists yet; when a table first needs one, give PropertyTable a GHDF Migrate slot and run
    // it here for an older options->DataVersion, as Properties_ReadJSON does.
    if ((table == NULL) || (compound == NULL) || (instance == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_ReadGHDF: table, compound and instance must not be NULL.");
    }
    PropertyReadOptions Options = (options != NULL) ? *options : PropertyReadOptions_CreateDefault();

    // All or nothing, for the key registry too: a refused read forgets the keys its data rows interned.
    size_t KeyCheckpoint = (Options.Keys != NULL) ? KeyRegistry_GetCount(Options.Keys) : 0U;
    PropertyScratch Scratch;
    PropertyScratch_Begin(&Scratch, table, instance);
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0; (Index < table->DescriptorCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (PropertyInternal_HasFlags(Descriptor, PropertyFlags_Persistent))
        {
            Result = ReadEntry(table, compound, Descriptor, &Options, Scratch._instance);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PropertyScratch_Commit(&Scratch, table, instance, ErrorCode_Deserialize);
    }
    else
    {
        PropertyScratch_Abort(&Scratch, table);
    }
    if (Result.Code != ErrorCode_Success)
    {
        KeyRegistry_TruncateTo(Options.Keys, KeyCheckpoint);
    }
    return Result;
}
