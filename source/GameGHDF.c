#include <inttypes.h>
#include "GameGHDF.h"
#include "wr/WRString.h"


// Static functions.
static bool IsInt8(int64_t value)
{
    return (value >= INT8_MIN) && (value <= INT8_MAX);
}

static bool IsInt16(int64_t value)
{
    return (value >= INT16_MIN) && (value <= INT16_MAX);
}

static bool IsInt32(int64_t value)
{
    return (value >= INT32_MIN) && (value <= INT32_MAX);
}

/* Finds an optional entry and tells whether it is a single integer: a GHDF value of an array carries its element type,
 * so the verified read (a regular entry of that type) is what refuses an array of integers. */
static Error GetIntegerEntry(GHDFCompound* compound, GHDFEntryID id, GHDFObjectValue* outEntry, bool* outFound,
    bool* outIsInteger)
{
    *outIsInteger = false;
    Error Result = GHDFCompound_GetOptional(compound, id, outEntry, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound || !GameGHDF_IsIntegerType(outEntry->Type))
    {
        return Result;
    }
    bool IsFound = false;
    Error VerifyResult = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(outEntry->Type), outEntry,
        &IsFound);
    *outIsInteger = (VerifyResult.Code == ErrorCode_Success) && IsFound;
    Error_Deconstruct(&VerifyResult);
    return Error_CreateSuccess();
}


// Public functions: types.
GHDFValueType GameGHDF_GetSmallestSignedType(int64_t minimum, int64_t maximum)
{
    if (IsInt8(minimum) && IsInt8(maximum))
    {
        return GHDFValueType_Int8;
    }
    if (IsInt16(minimum) && IsInt16(maximum))
    {
        return GHDFValueType_Int16;
    }
    return (IsInt32(minimum) && IsInt32(maximum)) ? GHDFValueType_Int32 : GHDFValueType_Int64;
}

GHDFValueType GameGHDF_GetSmallestUnsignedType(uint64_t maximum)
{
    if (maximum <= UINT8_MAX)
    {
        return GHDFValueType_UInt8;
    }
    if (maximum <= UINT16_MAX)
    {
        return GHDFValueType_UInt16;
    }
    return (maximum <= UINT32_MAX) ? GHDFValueType_UInt32 : GHDFValueType_UInt64;
}

bool GameGHDF_IsIntegerType(GHDFValueType type)
{
    return (type == GHDFValueType_UInt8) || (type == GHDFValueType_Int8) || (type == GHDFValueType_Int16)
        || (type == GHDFValueType_UInt16) || (type == GHDFValueType_Int32) || (type == GHDFValueType_UInt32)
        || (type == GHDFValueType_Int64) || (type == GHDFValueType_UInt64) || (type == GHDFValueType_EncodedInteger);
}


// Public functions: values.
GHDFObjectValue GameGHDF_CreateSigned(GHDFValueType type, int64_t value)
{
    switch (type)
    {
        case GHDFValueType_Int8:
            return GHDFObjectValue_CreateInt8((int8_t)value);
        case GHDFValueType_Int16:
            return GHDFObjectValue_CreateInt16((int16_t)value);
        case GHDFValueType_Int32:
            return GHDFObjectValue_CreateInt32((int32_t)value);
        default:
            return GHDFObjectValue_CreateInt64(value);
    }
}

GHDFObjectValue GameGHDF_CreateUnsigned(GHDFValueType type, uint64_t value)
{
    switch (type)
    {
        case GHDFValueType_UInt8:
            return GHDFObjectValue_CreateUInt8((uint8_t)value);
        case GHDFValueType_UInt16:
            return GHDFObjectValue_CreateUInt16((uint16_t)value);
        case GHDFValueType_UInt32:
            return GHDFObjectValue_CreateUInt32((uint32_t)value);
        default:
            return GHDFObjectValue_CreateUInt64(value);
    }
}

bool GameGHDF_TryGetSigned(const GHDFObjectValue* value, int64_t* outValue)
{
    switch (value->Type)
    {
        case GHDFValueType_UInt8:
            *outValue = (int64_t)value->Value.UInt8;
            return true;
        case GHDFValueType_Int8:
            *outValue = (int64_t)value->Value.Int8;
            return true;
        case GHDFValueType_Int16:
            *outValue = (int64_t)value->Value.Int16;
            return true;
        case GHDFValueType_UInt16:
            *outValue = (int64_t)value->Value.UInt16;
            return true;
        case GHDFValueType_Int32:
            *outValue = (int64_t)value->Value.Int32;
            return true;
        case GHDFValueType_UInt32:
            *outValue = (int64_t)value->Value.UInt32;
            return true;
        case GHDFValueType_Int64:
            *outValue = value->Value.Int64;
            return true;
        case GHDFValueType_UInt64:
            *outValue = (int64_t)value->Value.UInt64;
            return value->Value.UInt64 <= (uint64_t)INT64_MAX;
        case GHDFValueType_EncodedInteger:
            *outValue = value->Value.EncodedInteger;
            return true;
        default:
            return false;
    }
}

bool GameGHDF_TryGetUnsigned(const GHDFObjectValue* value, uint64_t* outValue)
{
    if (value->Type == GHDFValueType_UInt64)
    {
        *outValue = value->Value.UInt64;
        return true;
    }
    int64_t Signed = 0;
    if (!GameGHDF_TryGetSigned(value, &Signed) || (Signed < 0))
    {
        return false;
    }
    *outValue = (uint64_t)Signed;
    return true;
}


// Public functions: compound entries.
Error GameGHDF_PutSigned(GHDFCompound* compound, GHDFEntryID id, int64_t value)
{
    GHDFObjectValue Value = GameGHDF_CreateSigned(GameGHDF_GetSmallestSignedType(value, value), value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(Value.Type), &Value);
}

Error GameGHDF_PutUnsigned(GHDFCompound* compound, GHDFEntryID id, uint64_t value)
{
    GHDFObjectValue Value = GameGHDF_CreateUnsigned(GameGHDF_GetSmallestUnsignedType(value), value);
    return GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(Value.Type), &Value);
}

Error GameGHDF_GetOptionalSigned(GHDFCompound* compound, GHDFEntryID id, int64_t minimum, int64_t maximum,
    int64_t* outValue, bool* outFound)
{
    if ((compound == NULL) || (outValue == NULL) || (outFound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameGHDF_GetOptionalSigned: compound, outValue and outFound must not be NULL.");
    }
    GHDFObjectValue Entry = { .Type = GHDFValueType_None };
    bool IsInteger = false;
    Error Result = GetIntegerEntry(compound, id, &Entry, outFound, &IsInteger);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }
    int64_t Value = 0;
    if (!IsInteger || !GameGHDF_TryGetSigned(&Entry, &Value) || (Value < minimum) || (Value > maximum))
    {
        return Error_Construct4(ErrorCode_Deserialize, "The entry must be an integer from %" PRId64 " to %" PRId64 ".",
            minimum, maximum);
    }
    *outValue = Value;
    return Error_CreateSuccess();
}

Error GameGHDF_GetOptionalUnsigned(GHDFCompound* compound, GHDFEntryID id, uint64_t maximum, uint64_t* outValue,
    bool* outFound)
{
    if ((compound == NULL) || (outValue == NULL) || (outFound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameGHDF_GetOptionalUnsigned: compound, outValue and outFound must not be NULL.");
    }
    GHDFObjectValue Entry = { .Type = GHDFValueType_None };
    bool IsInteger = false;
    Error Result = GetIntegerEntry(compound, id, &Entry, outFound, &IsInteger);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }
    uint64_t Value = 0U;
    if (!IsInteger || !GameGHDF_TryGetUnsigned(&Entry, &Value) || (Value > maximum))
    {
        return Error_Construct4(ErrorCode_Deserialize, "The entry must be an integer from 0 to %" PRIu64 ".", maximum);
    }
    *outValue = Value;
    return Error_CreateSuccess();
}

Error GameGHDF_PutString(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const unsigned char* text)
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

Error GameGHDF_GetOptionalString(GHDFCompound* compound, GHDFEntryID id, const unsigned char** outText,
    size_t* outByteCount, bool* outFound)
{
    if ((compound == NULL) || (outText == NULL) || (outByteCount == NULL) || (outFound == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameGHDF_GetOptionalString: compound, outText, outByteCount and outFound must not be NULL.");
    }
    GHDFObjectValue Entry = { .Type = GHDFValueType_None };
    Error Result = GHDFCompound_GetOptionalVerified(compound, id, GHDF_CreateRegularType(GHDFValueType_String), &Entry,
        outFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct2(ErrorCode_Deserialize, "Expected a string.");
    }
    if (!*outFound)
    {
        return Error_CreateSuccess();
    }

    // The reader leaves its terminator past the bytes it read, counted; a NUL before it would cut the text short.
    GenericBuffer* Buffer = Entry.Value.String;
    if ((Buffer == NULL) || (Buffer->_count == 0U) || (StringUTF8_GetByteLength(Buffer->_data) != (Buffer->_count - 1U)))
    {
        return Error_Construct2(ErrorCode_Deserialize, "Expected a string without NUL characters.");
    }
    *outText = Buffer->_data;
    *outByteCount = Buffer->_count - 1U;
    return Error_CreateSuccess();
}
