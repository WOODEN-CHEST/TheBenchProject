#include "AssetDefinitionCommon.h"
#include "GameJSON.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Macros.
/** Number of entries in a fixed array. */
#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))


// Fields.
/* The accepted extension sets (see AssetDefinitionCommon.h). Each lists exactly what the shipped raylib's
   loader for that kind decodes, so a file it cannot read is rejected with a clear message at resolution. */
static const unsigned char* const TextureExtensionList[] = { u8"png", u8"bmp", u8"gif", u8"qoi", u8"dds" };
static const unsigned char* const ModelExtensionList[] = { u8"obj", u8"gltf", u8"glb", u8"iqm", u8"vox", u8"m3d" };
static const unsigned char* const FontExtensionList[] = { u8"ttf", u8"otf" };
static const unsigned char* const SoundExtensionList[] = { u8"wav", u8"ogg", u8"mp3", u8"qoa" };
static const unsigned char* const ShaderStageExtensionList[] = { u8"vert", u8"frag", u8"vs", u8"fs", u8"glsl" };

const AssetExtensionFilter ASSET_EXTENSIONS_TEXTURE = { TextureExtensionList, ARRAY_LENGTH(TextureExtensionList) };
const AssetExtensionFilter ASSET_EXTENSIONS_MODEL = { ModelExtensionList, ARRAY_LENGTH(ModelExtensionList) };
const AssetExtensionFilter ASSET_EXTENSIONS_FONT = { FontExtensionList, ARRAY_LENGTH(FontExtensionList) };
const AssetExtensionFilter ASSET_EXTENSIONS_SOUND = { SoundExtensionList, ARRAY_LENGTH(SoundExtensionList) };
const AssetExtensionFilter ASSET_EXTENSIONS_SHADER_STAGE = { ShaderStageExtensionList, ARRAY_LENGTH(ShaderStageExtensionList) };


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

Error AssetJSON_ReadOptionalFormat(JSONCompound* root, const AssetExtensionFilter* accepted,
    const unsigned char* assetKindName, unsigned char** outFormat)
{
    *outFormat = NULL;
    unsigned char* Format = NULL;
    Error ReadResult = AssetJSON_ReadOptionalOwnedString(root, (const unsigned char*)u8"format", &Format, NULL);
    if (ReadResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&ReadResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"The %s \"format\" must be a string.", assetKindName);
    }
    if ((Format == NULL) || AssetExtensionFilter_Accepts(accepted, Format))
    {
        *outFormat = Format;
        return Error_CreateSuccess();
    }

    GenericBuffer AcceptedList;
    GenericBuffer_AllocateVariable(&AcceptedList, 0U, 1U);
    Error Result = AssetExtensionFilter_AppendList(accepted, &AcceptedList);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Unsupported %s format \"%s\" (write the extension without the dot); supported: %s.",
            assetKindName, Format, AcceptedList._data);
    }
    Memory_Free(AcceptedList._data);
    Memory_Free(Format);
    return Result;
}

Error OwnedAssetLocation_ValidateReferenceFormat(const OwnedAssetLocation* self, const unsigned char* format,
    const AssetExtensionFilter* accepted, const unsigned char* assetKindName)
{
    if ((self->Type != AssetLocationType_Reference) || (format != NULL))
    {
        return Error_CreateSuccess();
    }

    GenericBuffer AcceptedList;
    GenericBuffer_AllocateVariable(&AcceptedList, 0U, 1U);
    Error Result = AssetExtensionFilter_AppendList(accepted, &AcceptedList);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"The %s location is a reference (\"%s\"), so the definition needs a \"format\" naming its file type (one of: %s).",
            assetKindName, (self->Value != NULL) ? self->Value : (const unsigned char*)u8"", AcceptedList._data);
    }
    Memory_Free(AcceptedList._data);
    return Result;
}

Error OwnedAssetLocation_RequireFile(const OwnedAssetLocation* self, const unsigned char* resourceKindName)
{
    if (self->Type != AssetLocationType_Reference)
    {
        return Error_CreateSuccess();
    }
    return Error_Construct3(ErrorCode_InvalidAssetDefinition,
        u8"The %s location must be a file path; the reference \"%s\" cannot be loaded because nothing names its file type.",
        resourceKindName, (self->Value != NULL) ? self->Value : (const unsigned char*)u8"");
}

void OwnedAssetLocation_Deconstruct(OwnedAssetLocation* self)
{
    if (self == NULL) { return; }
    Memory_Free(self->Value);
    self->Value = NULL;
}
