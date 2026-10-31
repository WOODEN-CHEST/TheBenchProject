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
/** Entry ids inside an instance list's compound: parallel arrays, one group of elements per instance in the list's
 *  order. The positions (three doubles each) are always there; the rotations (three floats, radians), the scales (three
 *  floats) and the tints (four channel bytes, RGBA) only when some instance differs from the default. */
#define PROPERTY_GHDF_INSTANCE_POSITIONS ((GHDFEntryID)1)
#define PROPERTY_GHDF_INSTANCE_ROTATIONS ((GHDFEntryID)2)
#define PROPERTY_GHDF_INSTANCE_SCALES ((GHDFEntryID)3)
#define PROPERTY_GHDF_INSTANCE_TINTS ((GHDFEntryID)4)
/** Elements per instance in each of those arrays. */
#define PROPERTY_GHDF_INSTANCE_VECTOR_ELEMENTS 3U
#define PROPERTY_GHDF_INSTANCE_TINT_ELEMENTS 4U


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

/* A String array of the list's names (empty for the empty list). */
static Error PutAssetNameList(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id,
    const PropertyAssetNameList* list)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, GHDFValueType_String, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (size_t Index = 0; (Index < PropertyAssetNameList_GetCount(list)) && (Result.Code == ErrorCode_Success); Index++)
    {
        GenericBuffer* Buffer = NULL;
        Result = GHDFObjectPool_BorrowString(pool, &Buffer);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (!GenericBuffer_AppendString(Buffer, PropertyAssetNameList_GetName(list, Index))
            || !GenericBuffer_NullTerminate(Buffer))
        {
            Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
            Error_Deconstruct(&ReturnResult);
            Result = Error_Construct2(ErrorCode_EncodeError, "Could not build a string buffer.");
            break;
        }
        GHDFObjectValue Element = GHDFObjectValue_CreateString(Buffer);
        Result = GHDFArray_AddValue(Array, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error ReturnResult = GHDFObjectPool_ReturnString(pool, Buffer);
            Error_Deconstruct(&ReturnResult);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Array, GHDFValueType_String);
        Result = GHDFCompound_SetValue(compound, id, GHDF_CreateArrayType(GHDFValueType_String), &ArrayValue);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnArray(pool, Array, true);
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

/* Which of an instance list's optional arrays differ from their defaults somewhere in the list. */
typedef struct InstanceArrayNeedsStruct
{
    bool HasRotations;
    bool HasScales;
    bool HasTints;
} InstanceArrayNeeds;

static InstanceArrayNeeds GetInstanceArrayNeeds(const PropertyInstanceList* list)
{
    InstanceArrayNeeds Needs = { .HasRotations = false, .HasScales = false, .HasTints = false };
    const PropertyInstanceEntry* Entries = PropertyInstanceList_GetEntries(list);
    for (size_t Index = 0; Index < PropertyInstanceList_GetCount(list); Index++)
    {
        const PropertyInstanceEntry* Entry = &Entries[Index];
        Needs.HasRotations = Needs.HasRotations || (Entry->Rotation.x != 0.0f) || (Entry->Rotation.y != 0.0f)
            || (Entry->Rotation.z != 0.0f);
        Needs.HasScales = Needs.HasScales || (Entry->Scale.x != 1.0f) || (Entry->Scale.y != 1.0f)
            || (Entry->Scale.z != 1.0f);
        Needs.HasTints = Needs.HasTints || (Entry->Tint.r != 255U) || (Entry->Tint.g != 255U) || (Entry->Tint.b != 255U)
            || (Entry->Tint.a != 255U);
    }
    return Needs;
}

/* Makes one instance's elements of one of an instance list's parallel arrays. */
typedef void (*InstanceElementMaker)(const PropertyInstanceEntry* entry, GHDFObjectValue* outElements);

static void MakePositionElements(const PropertyInstanceEntry* entry, GHDFObjectValue* outElements)
{
    outElements[0] = GHDFObjectValue_CreateDouble(entry->Position.X);
    outElements[1] = GHDFObjectValue_CreateDouble(entry->Position.Y);
    outElements[2] = GHDFObjectValue_CreateDouble(entry->Position.Z);
}

static void MakeRotationElements(const PropertyInstanceEntry* entry, GHDFObjectValue* outElements)
{
    outElements[0] = GHDFObjectValue_CreateFloat(entry->Rotation.x);
    outElements[1] = GHDFObjectValue_CreateFloat(entry->Rotation.y);
    outElements[2] = GHDFObjectValue_CreateFloat(entry->Rotation.z);
}

static void MakeScaleElements(const PropertyInstanceEntry* entry, GHDFObjectValue* outElements)
{
    outElements[0] = GHDFObjectValue_CreateFloat(entry->Scale.x);
    outElements[1] = GHDFObjectValue_CreateFloat(entry->Scale.y);
    outElements[2] = GHDFObjectValue_CreateFloat(entry->Scale.z);
}

static void MakeTintElements(const PropertyInstanceEntry* entry, GHDFObjectValue* outElements)
{
    outElements[0] = GHDFObjectValue_CreateUInt8(entry->Tint.r);
    outElements[1] = GHDFObjectValue_CreateUInt8(entry->Tint.g);
    outElements[2] = GHDFObjectValue_CreateUInt8(entry->Tint.b);
    outElements[3] = GHDFObjectValue_CreateUInt8(entry->Tint.a);
}

/* One of an instance list's parallel arrays: @p perInstance elements of @p elementType per instance, made by
 * @p makeElements. */
static Error PutInstanceArray(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, GHDFValueType elementType,
    const PropertyInstanceList* list, size_t perInstance, InstanceElementMaker makeElements)
{
    GHDFArray* Array = NULL;
    Error Result = GHDFObjectPool_BorrowArray(pool, elementType, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const PropertyInstanceEntry* Entries = PropertyInstanceList_GetEntries(list);
    GHDFObjectValue Elements[PROPERTY_GHDF_INSTANCE_TINT_ELEMENTS];
    for (size_t Index = 0; (Index < PropertyInstanceList_GetCount(list)) && (Result.Code == ErrorCode_Success); Index++)
    {
        makeElements(&Entries[Index], Elements);
        for (size_t Element = 0; (Element < perInstance) && (Result.Code == ErrorCode_Success); Element++)
        {
            Result = GHDFArray_AddValue(Array, &Elements[Element]);
        }
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

/* A compound of the list's parallel arrays: its positions always (an empty array for the empty list), its rotations,
 * scales and tints when some instance has one other than the default. */
static Error PutInstanceList(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id,
    const PropertyInstanceList* list)
{
    GHDFCompound* ListCompound = NULL;
    Error Result = GHDFObjectPool_BorrowCompound(pool, &ListCompound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    InstanceArrayNeeds Needs = GetInstanceArrayNeeds(list);
    Result = PutInstanceArray(pool, ListCompound, PROPERTY_GHDF_INSTANCE_POSITIONS, GHDFValueType_Double, list,
        PROPERTY_GHDF_INSTANCE_VECTOR_ELEMENTS, MakePositionElements);
    if ((Result.Code == ErrorCode_Success) && Needs.HasRotations)
    {
        Result = PutInstanceArray(pool, ListCompound, PROPERTY_GHDF_INSTANCE_ROTATIONS, GHDFValueType_Float, list,
            PROPERTY_GHDF_INSTANCE_VECTOR_ELEMENTS, MakeRotationElements);
    }
    if ((Result.Code == ErrorCode_Success) && Needs.HasScales)
    {
        Result = PutInstanceArray(pool, ListCompound, PROPERTY_GHDF_INSTANCE_SCALES, GHDFValueType_Float, list,
            PROPERTY_GHDF_INSTANCE_VECTOR_ELEMENTS, MakeScaleElements);
    }
    if ((Result.Code == ErrorCode_Success) && Needs.HasTints)
    {
        Result = PutInstanceArray(pool, ListCompound, PROPERTY_GHDF_INSTANCE_TINTS, GHDFValueType_UInt8, list,
            PROPERTY_GHDF_INSTANCE_TINT_ELEMENTS, MakeTintElements);
    }
    if (Result.Code == ErrorCode_Success)
    {
        GHDFObjectValue Value = GHDFObjectValue_CreateCompound(ListCompound);
        Result = GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_Compound), &Value);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = GHDFObjectPool_ReturnCompound(pool, ListCompound, true);
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
        case PropertyType_LocalName:
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
        case PropertyType_AssetNameList:
            return PutAssetNameList(pool, compound, Id, data->AssetNameListValue);
        case PropertyType_InstanceList:
            return PutInstanceList(pool, compound, Id, data->InstanceListValue);
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

/* An asset name list from its String array, its length and each name's checked before the list is built (the names'
 * rules are checked when the read commits); an empty array is the empty list. */
static Error ReadAssetNameList(GHDFCompound* compound, const PropertyDescriptor* descriptor, void* target)
{
    GHDFObjectValue Entry;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(compound, (GHDFEntryID)descriptor->WireId,
        GHDF_CreateArrayType(GHDFValueType_String), &Entry, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    size_t Count = GHDFArray_GetElementCount(Entry.Value.Array);
    if (Count > descriptor->Rules.MaxCount)
    {
        return Error_Construct4(ErrorCode_Deserialize, "The list has %" PRIu64 " names; at most %" PRIu64
            " are allowed.", (uint64_t)Count, (uint64_t)descriptor->Rules.MaxCount);
    }
    const unsigned char* Names[PROPERTY_ASSET_NAME_LIST_MAX_COUNT];
    for (size_t Index = 0; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        GHDFObjectValue Element = { .Type = GHDFValueType_None };
        Result = GHDFArray_Get(Entry.Value.Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        // The reader leaves its terminator past the bytes it read, counted; a NUL before it would cut the name short.
        GenericBuffer* Buffer = Element.Value.String;
        if ((Buffer == NULL) || (Buffer->_count == 0U) || (StringUTF8_GetByteLength(Buffer->_data) != (Buffer->_count - 1U))
            || ((Buffer->_count - 1U) > descriptor->Rules.MaxLength))
        {
            return Error_Construct4(ErrorCode_Deserialize, "An asset name is not a string of at most %" PRIu64
                " bytes without NUL characters.", (uint64_t)descriptor->Rules.MaxLength);
        }
        Names[Index] = Buffer->_data;
    }
    PropertyAssetNameList* List = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = PropertyAssetNameList_Create(Names, Count, &List);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    PropertyInternal_AdoptOwned(descriptor, target, List);
    return Error_CreateSuccess();
}

/* One of an instance list's optional arrays, with exactly @p count elements of @p elementType when it is there. */
static Error GetInstanceArray(GHDFCompound* compound, GHDFEntryID id, GHDFValueType elementType, size_t count,
    const char* name, GHDFArray** outArray)
{
    GHDFObjectValue Entry;
    bool IsFound = false;
    *outArray = NULL;
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateArrayType(elementType), &Entry, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    if (GHDFArray_GetElementCount(Entry.Value.Array) != count)
    {
        return Error_Construct4(ErrorCode_Deserialize, "The %s array has %" PRIu64 " elements; the positions call for %"
            PRIu64 ".", name, (uint64_t)GHDFArray_GetElementCount(Entry.Value.Array), (uint64_t)count);
    }
    *outArray = Entry.Value.Array;
    return Error_CreateSuccess();
}

/* Element @p index of an array known to hold it. */
static GHDFObjectValue GetElement(GHDFArray* array, size_t index, Error* inOutResult)
{
    GHDFObjectValue Element = { .Type = GHDFValueType_None };
    if (inOutResult->Code == ErrorCode_Success)
    {
        *inOutResult = GHDFArray_Get(array, index, &Element);
    }
    return Element;
}

/* An instance list from its compound of parallel arrays, the count (the positions' length over three) checked before
 * anything is allocated and every other array's length against it (the instances' rules are checked when the read
 * commits); a compound with an empty position array is the empty list. */
static Error ReadInstanceList(GHDFCompound* compound, const PropertyDescriptor* descriptor, void* target)
{
    GHDFObjectValue Entry;
    bool IsFound = false;
    Error Result = GHDFCompound_GetOptionalVerified(compound, (GHDFEntryID)descriptor->WireId,
        GHDF_CreateRegularType(GHDFValueType_Compound), &Entry, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    GHDFCompound* ListCompound = Entry.Value.Compound;
    GHDFObjectValue PositionsEntry;
    Result = GHDFCompound_GetVerified(ListCompound, PROPERTY_GHDF_INSTANCE_POSITIONS,
        GHDF_CreateArrayType(GHDFValueType_Double), &PositionsEntry);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    GHDFArray* Positions = PositionsEntry.Value.Array;
    size_t ElementCount = GHDFArray_GetElementCount(Positions);
    if ((ElementCount % PROPERTY_GHDF_INSTANCE_VECTOR_ELEMENTS) != 0U)
    {
        return Error_Construct4(ErrorCode_Deserialize, "The position array has %" PRIu64 " elements, not three per "
            "instance.", (uint64_t)ElementCount);
    }
    size_t Count = ElementCount / PROPERTY_GHDF_INSTANCE_VECTOR_ELEMENTS;
    if (Count > descriptor->Rules.MaxCount)
    {
        return Error_Construct4(ErrorCode_Deserialize, "The list has %" PRIu64 " instances; at most %" PRIu64
            " are allowed.", (uint64_t)Count, (uint64_t)descriptor->Rules.MaxCount);
    }
    GHDFArray* Rotations = NULL;
    GHDFArray* Scales = NULL;
    GHDFArray* Tints = NULL;
    size_t VectorElements = Count * PROPERTY_GHDF_INSTANCE_VECTOR_ELEMENTS;
    Result = GetInstanceArray(ListCompound, PROPERTY_GHDF_INSTANCE_ROTATIONS, GHDFValueType_Float, VectorElements,
        "rotation", &Rotations);
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetInstanceArray(ListCompound, PROPERTY_GHDF_INSTANCE_SCALES, GHDFValueType_Float, VectorElements,
            "scale", &Scales);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetInstanceArray(ListCompound, PROPERTY_GHDF_INSTANCE_TINTS, GHDFValueType_UInt8,
            Count * PROPERTY_GHDF_INSTANCE_TINT_ELEMENTS, "tint", &Tints);
    }
    if ((Result.Code != ErrorCode_Success) || (Count == 0U))
    {
        if (Result.Code == ErrorCode_Success)
        {
            PropertyInternal_AdoptOwned(descriptor, target, NULL);
        }
        return Result;
    }

    PropertyInstanceList* List = PropertyInternal_AllocateInstanceList(Count);
    for (size_t Index = 0; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        PropertyInstanceEntry* Instance = &List->_entries[Index];
        size_t First = Index * PROPERTY_GHDF_INSTANCE_VECTOR_ELEMENTS;
        Instance->Position.X = GetElement(Positions, First, &Result).Value.Double;
        Instance->Position.Y = GetElement(Positions, First + 1U, &Result).Value.Double;
        Instance->Position.Z = GetElement(Positions, First + 2U, &Result).Value.Double;
        if (Rotations != NULL)
        {
            Instance->Rotation.x = GetElement(Rotations, First, &Result).Value.Float;
            Instance->Rotation.y = GetElement(Rotations, First + 1U, &Result).Value.Float;
            Instance->Rotation.z = GetElement(Rotations, First + 2U, &Result).Value.Float;
        }
        if (Scales != NULL)
        {
            Instance->Scale.x = GetElement(Scales, First, &Result).Value.Float;
            Instance->Scale.y = GetElement(Scales, First + 1U, &Result).Value.Float;
            Instance->Scale.z = GetElement(Scales, First + 2U, &Result).Value.Float;
        }
        if (Tints != NULL)
        {
            size_t FirstChannel = Index * PROPERTY_GHDF_INSTANCE_TINT_ELEMENTS;
            Instance->Tint.r = GetElement(Tints, FirstChannel, &Result).Value.UInt8;
            Instance->Tint.g = GetElement(Tints, FirstChannel + 1U, &Result).Value.UInt8;
            Instance->Tint.b = GetElement(Tints, FirstChannel + 2U, &Result).Value.UInt8;
            Instance->Tint.a = GetElement(Tints, FirstChannel + 3U, &Result).Value.UInt8;
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        PropertyInstanceList_Destroy(List);
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
        case PropertyType_LocalName:
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
        case PropertyType_AssetNameList:
            return Error_Construct2(ErrorCode_InvalidState, "Asset name lists are read separately.");
        case PropertyType_InstanceList:
            return Error_Construct2(ErrorCode_InvalidState, "Instance lists are read separately.");
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
    else if (descriptor->Type == PropertyType_AssetNameList)
    {
        Result = ReadAssetNameList(compound, descriptor, target);
    }
    else if (descriptor->Type == PropertyType_InstanceList)
    {
        Result = ReadInstanceList(compound, descriptor, target);
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

Error Properties_MigrateGHDF(const PropertyTable* table, GHDFCompound* compound, const PropertyReadOptions* options)
{
    if ((table == NULL) || (compound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Properties_MigrateGHDF: table and compound must not be NULL.");
    }
    PropertyReadOptions Options = (options != NULL) ? *options : PropertyReadOptions_CreateDefault();
    if ((Options.DataVersion == 0U) || (Options.DataVersion >= table->Version) || (table->MigrateGHDF == NULL))
    {
        return Error_CreateSuccess();
    }
    Error MigrateResult = table->MigrateGHDF(Options.DataVersion, compound, Options.GHDFPool, Options.Logger);
    return (MigrateResult.Code == ErrorCode_Success) ? MigrateResult
        : PropertyInternal_WrapError(MigrateResult.Code, table, NULL, &MigrateResult);
}

Error Properties_ReadGHDF(const PropertyTable* table, GHDFCompound* compound, void* instance,
    const PropertyReadOptions* options)
{
    if ((table == NULL) || (compound == NULL) || (instance == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Properties_ReadGHDF: table, compound and instance must not be NULL.");
    }
    PropertyReadOptions Options = (options != NULL) ? *options : PropertyReadOptions_CreateDefault();
    Error MigrateResult = Properties_MigrateGHDF(table, compound, &Options);
    if (MigrateResult.Code != ErrorCode_Success)
    {
        return MigrateResult;
    }

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
