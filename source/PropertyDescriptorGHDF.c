// GHDF reading and writing of property tables (see PropertyDescriptor.h for the entry types). The entry ids are the
// descriptors' wire ids; the layout of colors and vectors matches what the hand-written world encoder wrote.

#include <inttypes.h>
#include "PropertyDescriptorInternal.h"
#include "wr/WRCompile.h"
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
/** The most components of a written float array (a Vector3). */
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

static Error PutString(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const unsigned char* text)
{
    GenericBuffer* Buffer = NULL;
    Error Result = GHDFObjectPool_BorrowString(pool, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_AppendString(Buffer, text) || !GenericBuffer_NullTerminate(Buffer))
    {
        Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct2(ErrorCode_EncodeError, "Could not build a string buffer.");
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

static Error PutValue(GHDFObjectPool* pool, GHDFCompound* compound, const PropertyDescriptor* descriptor,
    const PropertyData* data)
{
    GHDFEntryID Id = (GHDFEntryID)descriptor->WireId;
    switch (descriptor->Type)
    {
        case PropertyType_Bool:
            return PutScalar(compound, Id, GHDFObjectValue_CreateBoolean(data->BoolValue));
        case PropertyType_Int32:
            return PutScalar(compound, Id, GHDFObjectValue_CreateInt32(data->Int32Value));
        case PropertyType_UInt32:
            return PutScalar(compound, Id, GHDFObjectValue_CreateUInt32(data->UInt32Value));
        case PropertyType_UInt64:
            return PutScalar(compound, Id, GHDFObjectValue_CreateUInt64(data->UInt64Value));
        case PropertyType_ObjectId:
            return PutScalar(compound, Id, GHDFObjectValue_CreateUInt64(data->ObjectIdValue));
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
        {
            const float Components[3] = { data->Vector3Value.x, data->Vector3Value.y, data->Vector3Value.z };
            return PutFloats(pool, compound, Id, Components, 3U);
        }
        case PropertyType_Color:
            return PutChannels(pool, compound, Id, data->ColorValue, PROPERTY_GHDF_COLOR_CHANNEL_COUNT);
        case PropertyType_RenderColor:
            return PutRenderColor(pool, compound, Id, data->RenderColorValue);
        case PropertyType_String:
        case PropertyType_AssetName:
            // GHDF has no null: a NULL string is left out, which reads back as its NULL default.
            return (data->StringValue != NULL) ? PutString(pool, compound, Id, data->StringValue) : Error_CreateSuccess();
        case PropertyType_Enum:
            return PutScalar(compound, Id, GHDFObjectValue_CreateEncodedInteger((int64_t)data->EnumValue));
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
    GHDFObjectValue Entry;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(compound, (GHDFEntryID)descriptor->WireId,
        GHDF_CreateRegularType(GHDFValueType_String), &Entry, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }

    GenericBuffer* Buffer = Entry.Value.String;
    if ((Buffer == NULL) || (Buffer->_count == 0U) || (StringUTF8_GetByteLength(Buffer->_data) != (Buffer->_count - 1U)))
    {
        return Error_Construct2(ErrorCode_Deserialize, "Expected a string without NUL characters.");
    }
    size_t ByteCount = Buffer->_count - 1U;
    if (ByteCount > descriptor->Rules.MaxLength)
    {
        return Error_Construct4(ErrorCode_Deserialize, "The string is longer than the %" PRIu64 " bytes allowed.",
            (uint64_t)descriptor->Rules.MaxLength);
    }
    unsigned char* Text = Memory_Allocate(Buffer->_count);
    Memory_Copy(Buffer->_data, Text, Buffer->_count);
    PropertyInternal_AdoptString(descriptor, target, Text);
    return Error_CreateSuccess();
}

/* Reads a scalar entry of @p type into *outEntry; *outFound false when absent. */
static Error GetScalar(GHDFCompound* compound, GHDFEntryID id, GHDFValueType type, GHDFObjectValue* outEntry,
    bool* outFound)
{
    return GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(type), outEntry, outFound);
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
            Result = GetScalar(compound, Id, GHDFValueType_Int32, &Entry, outFound);
            outData->Int32Value = *outFound ? Entry.Value.Int32 : 0;
            return Result;
        case PropertyType_UInt32:
            Result = GetScalar(compound, Id, GHDFValueType_UInt32, &Entry, outFound);
            outData->UInt32Value = *outFound ? Entry.Value.UInt32 : 0U;
            return Result;
        case PropertyType_UInt64:
            Result = GetScalar(compound, Id, GHDFValueType_UInt64, &Entry, outFound);
            outData->UInt64Value = *outFound ? Entry.Value.UInt64 : 0U;
            return Result;
        case PropertyType_ObjectId:
            Result = GetScalar(compound, Id, GHDFValueType_UInt64, &Entry, outFound);
            outData->ObjectIdValue = *outFound ? Entry.Value.UInt64 : 0U;
            return Result;
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
        {
            float Components[3] = { 0.0f, 0.0f, 0.0f };
            Result = GetFloats(compound, Id, Components, 3U, outFound);
            outData->Vector3Value = (Vector3){ .x = Components[0], .y = Components[1], .z = Components[2] };
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
    }
    return Error_Construct2(ErrorCode_InvalidState, "The property has an unknown type.");
}

static Error ReadEntry(const PropertyTable* table, GHDFCompound* compound, const PropertyDescriptor* descriptor,
    void* target)
{
    Error Result = Error_CreateSuccess();
    if (PropertyInternal_IsStringType(descriptor->Type))
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
    UNUSED(options);
    if ((table == NULL) || (compound == NULL) || (instance == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_ReadGHDF: table, compound and instance must not be NULL.");
    }

    PropertyScratch Scratch;
    PropertyScratch_Begin(&Scratch, table, instance);
    for (size_t Index = 0; Index < table->DescriptorCount; Index++)
    {
        const PropertyDescriptor* Descriptor = &table->Descriptors[Index];
        if (!PropertyInternal_HasFlags(Descriptor, PropertyFlags_Persistent))
        {
            continue;
        }
        Error Result = ReadEntry(table, compound, Descriptor, Scratch._instance);
        if (Result.Code != ErrorCode_Success)
        {
            PropertyScratch_Abort(&Scratch, table);
            return Result;
        }
    }
    return PropertyScratch_Commit(&Scratch, table, instance, ErrorCode_Deserialize);
}
