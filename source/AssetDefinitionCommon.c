#include "AssetDefinitionCommon.h"
#include "GameJSON.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Static functions.
static bool StringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool Equal = false;
    Error Result = StringUTF8_EqualsExact(a, b, &Equal);
    if (Result.Code != ErrorCode_Success) { Error_Deconstruct(&Result); return false; }
    return Equal;
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
    return GameJSON_ValueToOwnedString(value, outString);
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
    return GameJSON_ReadOptionalInteger(compound, key, defaultValue, outValue);
}

Error AssetJSON_ReadOptionalBoolean(JSONCompound* compound, const unsigned char* key,
    bool defaultValue, bool* outValue)
{
    return GameJSON_ReadOptionalBoolean(compound, key, defaultValue, outValue);
}

Error AssetJSON_ReadOptionalVectorInt(JSONCompound* compound, const unsigned char* key,
    int64_t defaultX, int64_t defaultY, int64_t* outX, int64_t* outY)
{
    return GameJSON_ReadOptionalVector2Int(compound, key, defaultX, defaultY, outX, outY);
}

Error AssetJSON_ParseColor(const JSONObjectValue* value, Color* outColor)
{
    return GameJSON_ParseColor(value, outColor);
}

void OwnedAssetLocation_Deconstruct(OwnedAssetLocation* self)
{
    if (self == NULL) { return; }
    Memory_Free(self->Value);
    self->Value = NULL;
}
