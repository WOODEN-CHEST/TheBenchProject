#include "AssetDefinitionCommon.h"
#include "wr/WRString.h"
#include "wr/WRNumber.h"
#include "wr/WRMemory.h"


// Static functions.
static bool StringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool Equal = false;
    Error Result = StringUTF8_EqualsExact(a, b, &Equal);
    if (Result.Code != ErrorCode_Success) { Error_Deconstruct(&Result); return false; }
    return Equal;
}

static Error ValueToInt64(const JSONObjectValue* value, int64_t* outValue)
{
    if (value->Type == JSONValueType_Integer)
    {
        *outValue = value->Value.Integer;
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_RealNumber)
    {
        *outValue = (int64_t)value->Value.RealNumber;
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error ConvertResult = AssetJSON_ValueToOwnedString(value, &Text);
        if (ConvertResult.Code != ErrorCode_Success) { return ConvertResult; }
        int64_t Parsed = 0;
        Error ParseResult = Number_Int64FromString(Text, NUMBER_BASE_AUTO_DETECT, &Parsed);
        Memory_Free(Text);
        if (ParseResult.Code != ErrorCode_Success) { return ParseResult; }
        *outValue = Parsed;
        return Error_CreateSuccess();
    }
    return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Expected an integer value.");
}

static unsigned char ClampChannel(int64_t value)
{
    if (value < 0) { return 0U; }
    if (value > 255) { return 255U; }
    return (unsigned char)value;
}

static Error ParseColorHexString(const unsigned char* text, Color* outColor)
{
    const unsigned char* Position = text;
    if (Position[0] == (unsigned char)'#') { Position++; }
    if ((Position[0] == (unsigned char)'0') && ((Position[1] == (unsigned char)'x') || (Position[1] == (unsigned char)'X')))
    {
        Position += 2;
    }

    size_t DigitCount = StringUTF8_GetByteLength(Position);
    if ((DigitCount != 6U) && (DigitCount != 8U))
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Hex color must have 6 or 8 digits.");
    }

    uint32_t Value = 0U;
    Error ParseResult = Number_UInt32FromString(Position, NUMBER_BASE_16, &Value);
    if (ParseResult.Code != ErrorCode_Success) { return ParseResult; }

    if (DigitCount == 6U)
    {
        outColor->r = (unsigned char)((Value >> 16) & 0xFFU);
        outColor->g = (unsigned char)((Value >> 8) & 0xFFU);
        outColor->b = (unsigned char)(Value & 0xFFU);
        outColor->a = 255U;
    }
    else
    {
        outColor->r = (unsigned char)((Value >> 24) & 0xFFU);
        outColor->g = (unsigned char)((Value >> 16) & 0xFFU);
        outColor->b = (unsigned char)((Value >> 8) & 0xFFU);
        outColor->a = (unsigned char)(Value & 0xFFU);
    }
    return Error_CreateSuccess();
}

static Error ParseColorArray(JSONArray* array, Color* outColor)
{
    size_t Count = JSONArray_GetElementCount(array);
    if ((Count != 3U) && (Count != 4U))
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Color array must have 3 or 4 elements.");
    }

    int64_t Channels[4] = { 0, 0, 0, 255 };
    for (size_t i = 0; i < Count; i++)
    {
        JSONObjectValue Element;
        Error GetResult = JSONArray_Get(array, i, &Element);
        if (GetResult.Code != ErrorCode_Success) { return GetResult; }
        Error IntResult = ValueToInt64(&Element, &Channels[i]);
        if (IntResult.Code != ErrorCode_Success) { return IntResult; }
    }

    outColor->r = ClampChannel(Channels[0]);
    outColor->g = ClampChannel(Channels[1]);
    outColor->b = ClampChannel(Channels[2]);
    outColor->a = ClampChannel(Channels[3]);
    return Error_CreateSuccess();
}

static Error ParseColorCompound(JSONCompound* compound, Color* outColor)
{
    int64_t Channels[4] = { 0, 0, 0, 255 };
    const unsigned char* Keys[4] = { (const unsigned char*)u8"r", (const unsigned char*)u8"g",
        (const unsigned char*)u8"b", (const unsigned char*)u8"a" };

    for (size_t i = 0; i < 4U; i++)
    {
        JSONObjectValue Value;
        bool Found = false;
        Error GetResult = JSONCompound_GetOptional(compound, Keys[i], &Value, &Found);
        if (GetResult.Code != ErrorCode_Success) { return GetResult; }
        if (!Found)
        {
            if (i < 3U)
            {
                return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Color compound requires r, g and b.");
            }
            continue;
        }
        Error IntResult = ValueToInt64(&Value, &Channels[i]);
        if (IntResult.Code != ErrorCode_Success) { return IntResult; }
    }

    outColor->r = ClampChannel(Channels[0]);
    outColor->g = ClampChannel(Channels[1]);
    outColor->b = ClampChannel(Channels[2]);
    outColor->a = ClampChannel(Channels[3]);
    return Error_CreateSuccess();
}


// Public functions.
Error AssetJSON_DeserializeRoot(JSONObjectPool* pool, const GenericBuffer* rawData,
    const unsigned char* sourceDescription, JSONObjectValue* outRootValue, JSONCompound** outRoot)
{
    *outRoot = NULL;
    *outRootValue = JSONObjectValue_CreateNull();

    // JSON_Deserialize wants a mutable GenericBuffer*; it only reads it.
    Error DeserializeResult = JSON_Deserialize(pool, (GenericBuffer*)rawData, outRootValue);
    if (DeserializeResult.Code != ErrorCode_Success)
    {
        return DeserializeResult;
    }
    if (outRootValue->Type != JSONValueType_Compound)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Asset definition root must be a JSON object (\"%s\").",
            (sourceDescription != NULL) ? sourceDescription : (const unsigned char*)u8"?");
    }
    *outRoot = outRootValue->Value.Compound;
    return Error_CreateSuccess();
}

Error AssetJSON_ValueToOwnedString(const JSONObjectValue* value, unsigned char** outString)
{
    if (value->Type != JSONValueType_String)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Expected a string value.");
    }

    GenericBuffer* Buffer = value->Value.String;
    size_t Length = (Buffer != NULL) ? Buffer->_count : 0U;
    unsigned char* Copy = Memory_Allocate(Length + 1U);
    if (Length > 0U)
    {
        Memory_Copy(Buffer->_data, Copy, Length);
    }
    Copy[Length] = (unsigned char)0;
    *outString = Copy;
    return Error_CreateSuccess();
}

Error AssetJSON_ReadName(JSONCompound* root, const unsigned char* sourceDescription, unsigned char** outName)
{
    JSONObjectValue Value;
    Error GetResult = JSONCompound_GetVerified(root, (const unsigned char*)u8"name", JSONValueType_String, &Value);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Asset definition is missing a string \"name\" (\"%s\").",
            (sourceDescription != NULL) ? sourceDescription : (const unsigned char*)u8"?");
    }
    return AssetJSON_ValueToOwnedString(&Value, outName);
}

Error AssetJSON_ParseLocation(const JSONObjectValue* value, const unsigned char* sourceDescription,
    OwnedAssetLocation* outLocation)
{
    if (value->Type == JSONValueType_String)
    {
        outLocation->Type = AssetLocationType_File;
        return AssetJSON_ValueToOwnedString(value, &outLocation->Value);
    }

    if (value->Type == JSONValueType_Compound)
    {
        JSONCompound* Compound = value->Value.Compound;

        JSONObjectValue TypeValue;
        Error TypeResult = JSONCompound_GetVerified(Compound, (const unsigned char*)u8"type", JSONValueType_String, &TypeValue);
        if (TypeResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&TypeResult);
            return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Location compound requires a string \"type\".");
        }
        JSONObjectValue ValueValue;
        Error ValueResult = JSONCompound_GetVerified(Compound, (const unsigned char*)u8"value", JSONValueType_String, &ValueValue);
        if (ValueResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&ValueResult);
            return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Location compound requires a string \"value\".");
        }

        unsigned char* TypeText = NULL;
        Error TypeTextResult = AssetJSON_ValueToOwnedString(&TypeValue, &TypeText);
        if (TypeTextResult.Code != ErrorCode_Success) { return TypeTextResult; }

        AssetLocationType LocationType;
        if (StringsEqual(TypeText, (const unsigned char*)u8"file"))
        {
            LocationType = AssetLocationType_File;
        }
        else if (StringsEqual(TypeText, (const unsigned char*)u8"reference"))
        {
            LocationType = AssetLocationType_Reference;
        }
        else
        {
            Memory_Free(TypeText);
            return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Location type must be \"file\" or \"reference\".");
        }
        Memory_Free(TypeText);

        outLocation->Type = LocationType;
        return AssetJSON_ValueToOwnedString(&ValueValue, &outLocation->Value);
    }

    return Error_Construct3(ErrorCode_InvalidAssetDefinition,
        u8"A location must be a string or a compound (\"%s\").",
        (sourceDescription != NULL) ? sourceDescription : (const unsigned char*)u8"?");
}

Error AssetJSON_ReadLocation(JSONCompound* compound, const unsigned char* key,
    const unsigned char* sourceDescription, OwnedAssetLocation* outLocation)
{
    JSONObjectValue Value;
    Error GetResult = JSONCompound_Get(compound, key, &Value);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Missing required location \"%s\".", key);
    }
    return AssetJSON_ParseLocation(&Value, sourceDescription, outLocation);
}

Error AssetJSON_ReadOptionalOwnedString(JSONCompound* compound, const unsigned char* key,
    unsigned char** outString, bool* outFound)
{
    *outString = NULL;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptionalVerified(compound, key, JSONValueType_String, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (outFound != NULL) { *outFound = Found; }
    if (!Found) { return Error_CreateSuccess(); }
    return AssetJSON_ValueToOwnedString(&Value, outString);
}

Error AssetJSON_ReadTextureProperties(JSONCompound* compound, const unsigned char* key, TextureProperties* outProps)
{
    outProps->Filter = TEXTURE_FILTER_BILINEAR;

    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptionalVerified(compound, key, JSONValueType_Compound, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }

    JSONObjectValue FilterValue;
    bool FilterFound = false;
    Error FilterResult = JSONCompound_GetOptionalVerified(Value.Value.Compound,
        (const unsigned char*)u8"filtering", JSONValueType_String, &FilterValue, &FilterFound);
    if (FilterResult.Code != ErrorCode_Success) { return FilterResult; }
    if (!FilterFound) { return Error_CreateSuccess(); }

    unsigned char* FilterText = NULL;
    Error TextResult = AssetJSON_ValueToOwnedString(&FilterValue, &FilterText);
    if (TextResult.Code != ErrorCode_Success) { return TextResult; }

    Error Result = Error_CreateSuccess();
    if (StringsEqual(FilterText, (const unsigned char*)u8"point"))
    {
        outProps->Filter = TEXTURE_FILTER_POINT;
    }
    else if (StringsEqual(FilterText, (const unsigned char*)u8"bilinear"))
    {
        outProps->Filter = TEXTURE_FILTER_BILINEAR;
    }
    else
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Texture filtering must be \"point\" or \"bilinear\".");
    }
    Memory_Free(FilterText);
    return Result;
}

Error AssetJSON_ReadOptionalInteger(JSONCompound* compound, const unsigned char* key,
    int64_t defaultValue, int64_t* outValue)
{
    *outValue = defaultValue;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }
    return ValueToInt64(&Value, outValue);
}

Error AssetJSON_ReadOptionalBoolean(JSONCompound* compound, const unsigned char* key,
    bool defaultValue, bool* outValue)
{
    *outValue = defaultValue;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }

    if (Value.Type == JSONValueType_Boolean)
    {
        *outValue = Value.Value.Boolean;
        return Error_CreateSuccess();
    }
    if (Value.Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error TextResult = AssetJSON_ValueToOwnedString(&Value, &Text);
        if (TextResult.Code != ErrorCode_Success) { return TextResult; }
        Error Result = Error_CreateSuccess();
        if (StringsEqual(Text, (const unsigned char*)u8"true"))
        {
            *outValue = true;
        }
        else if (StringsEqual(Text, (const unsigned char*)u8"false"))
        {
            *outValue = false;
        }
        else
        {
            Result = Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Boolean must be true or false.");
        }
        Memory_Free(Text);
        return Result;
    }
    return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Expected a boolean value for \"%s\".", key);
}

Error AssetJSON_ReadOptionalVectorInt(JSONCompound* compound, const unsigned char* key,
    int64_t defaultX, int64_t defaultY, int64_t* outX, int64_t* outY)
{
    *outX = defaultX;
    *outY = defaultY;

    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }

    if (Value.Type == JSONValueType_Compound)
    {
        JSONObjectValue Component;
        Error XResult = JSONCompound_Get(Value.Value.Compound, (const unsigned char*)u8"x", &Component);
        if (XResult.Code != ErrorCode_Success) { Error_Deconstruct(&XResult); return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Vector compound requires \"x\"."); }
        Error XIntResult = ValueToInt64(&Component, outX);
        if (XIntResult.Code != ErrorCode_Success) { return XIntResult; }

        Error YResult = JSONCompound_Get(Value.Value.Compound, (const unsigned char*)u8"y", &Component);
        if (YResult.Code != ErrorCode_Success) { Error_Deconstruct(&YResult); return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Vector compound requires \"y\"."); }
        return ValueToInt64(&Component, outY);
    }

    if (Value.Type == JSONValueType_Array)
    {
        JSONArray* Array = Value.Value.Array;
        if (JSONArray_GetElementCount(Array) < 2U)
        {
            return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Vector array requires two elements.");
        }
        JSONObjectValue Element;
        Error XResult = JSONArray_Get(Array, 0U, &Element);
        if (XResult.Code != ErrorCode_Success) { return XResult; }
        Error XIntResult = ValueToInt64(&Element, outX);
        if (XIntResult.Code != ErrorCode_Success) { return XIntResult; }

        Error YResult = JSONArray_Get(Array, 1U, &Element);
        if (YResult.Code != ErrorCode_Success) { return YResult; }
        return ValueToInt64(&Element, outY);
    }

    return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Expected a vector for \"%s\".", key);
}

Error AssetJSON_ParseColor(const JSONObjectValue* value, Color* outColor)
{
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error TextResult = AssetJSON_ValueToOwnedString(value, &Text);
        if (TextResult.Code != ErrorCode_Success) { return TextResult; }
        Error Result = ParseColorHexString(Text, outColor);
        Memory_Free(Text);
        return Result;
    }
    if (value->Type == JSONValueType_Array)
    {
        return ParseColorArray(value->Value.Array, outColor);
    }
    if (value->Type == JSONValueType_Compound)
    {
        return ParseColorCompound(value->Value.Compound, outColor);
    }
    return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"A color must be a string, array or compound.");
}

void OwnedAssetLocation_Deconstruct(OwnedAssetLocation* self)
{
    if (self == NULL) { return; }
    Memory_Free(self->Value);
    self->Value = NULL;
}
