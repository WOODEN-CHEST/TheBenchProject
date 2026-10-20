#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include "AssetDefinitionCommon.h"
#include "GameJSON.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Macros.
/** Number of entries in a fixed array. */
#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))
/** Room for a collection enumerator on the stack, so checking a compound's keys allocates nothing. */
#define ENUMERATOR_STORAGE_SIZE ((size_t)256)
/** How many bytes of an unknown key its message shows. */
#define UNKNOWN_KEY_PRINT_CAPACITY ((size_t)65)
/** The printable ASCII range [first, last]: AssetJSON_CopyPrintable shows every other byte as '?'. */
#define FIRST_PRINTABLE_ASCII 0x20U
#define LAST_PRINTABLE_ASCII 0x7EU
/** The bounds of the material factors in [0, 1] (metallic, roughness) and of the emissive intensity (>= 0). */
#define MATERIAL_FACTOR_MINIMUM 0.0f
#define MATERIAL_FACTOR_MAXIMUM 1.0f


// Types.
/* One accepted string value of a texture property and the raylib constant it stands for. */
typedef struct TexturePropertyNameStruct
{
    const unsigned char* Name;
    int Value;
} TexturePropertyName;

/* Stack storage for a collection enumerator. */
typedef union EnumeratorStorageUnion
{
    max_align_t Alignment;
    unsigned char Bytes[ENUMERATOR_STORAGE_SIZE];
} EnumeratorStorage;


// Fields.
static const TexturePropertyName TextureFilterNames[] =
{
    { u8"point", TEXTURE_FILTER_POINT },
    { u8"bilinear", TEXTURE_FILTER_BILINEAR },
    { u8"trilinear", TEXTURE_FILTER_TRILINEAR }
};

static const TexturePropertyName TextureWrapNames[] =
{
    { u8"repeat", TEXTURE_WRAP_REPEAT },
    { u8"clamp", TEXTURE_WRAP_CLAMP },
    { u8"mirror_repeat", TEXTURE_WRAP_MIRROR_REPEAT },
    { u8"mirror_clamp", TEXTURE_WRAP_MIRROR_CLAMP }
};

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

/* Reads the optional string key @p key of a texture_properties compound, which must be one of @p names (listed in
 * @p expected for the message). *outValue keeps its value when the key is absent. */
static Error ReadTexturePropertyName(JSONCompound* properties, const unsigned char* key,
    const TexturePropertyName* names, size_t nameCount, const unsigned char* expected, int* outValue)
{
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptionalVerified(properties, key, JSONValueType_String, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Texture \"%s\" must be %s.", key, expected);
    }
    if (!Found) { return Error_CreateSuccess(); }

    unsigned char* Text = NULL;
    Error TextResult = AssetJSON_ValueToOwnedString(&Value, &Text);
    if (TextResult.Code != ErrorCode_Success) { return TextResult; }
    for (size_t Index = 0U; Index < nameCount; Index++)
    {
        if (StringsEqual(Text, names[Index].Name))
        {
            *outValue = names[Index].Value;
            Memory_Free(Text);
            return Error_CreateSuccess();
        }
    }
    Error Result = Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Texture \"%s\" is \"%s\"; it must be %s.", key,
        Text, expected);
    Memory_Free(Text);
    return Result;
}

/* Whether @p key is one of @p knownKeys. */
static bool IsKnownKey(const unsigned char* key, const unsigned char* const* knownKeys, size_t knownKeyCount)
{
    for (size_t Index = 0U; Index < knownKeyCount; Index++)
    {
        if (StringsEqual(key, knownKeys[Index]))
        {
            return true;
        }
    }
    return false;
}

/* The error for an unknown key: its printable form, where it is and the keys allowed there. */
static Error CreateUnknownKeyError(const unsigned char* key, const unsigned char* const* knownKeys,
    size_t knownKeyCount, const char* where)
{
    char PrintableKey[UNKNOWN_KEY_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(key, PrintableKey, sizeof(PrintableKey));

    GenericBuffer KnownList;
    GenericBuffer_AllocateVariable(&KnownList, 0U, sizeof(unsigned char));
    bool IsListed = true;
    for (size_t Index = 0U; Index < knownKeyCount; Index++)
    {
        if (Index > 0U)
        {
            IsListed = IsListed && GenericBuffer_AppendString(&KnownList, (const unsigned char*)u8", ");
        }
        IsListed = IsListed && GenericBuffer_AppendString(&KnownList, knownKeys[Index]);
    }
    IsListed = IsListed && GenericBuffer_NullTerminate(&KnownList);

    Error Result = Error_Construct4(ErrorCode_InvalidAssetDefinition,
        "Unknown key \"%s\" in %s; the keys allowed there are: %s.", PrintableKey, where,
        IsListed ? (const char*)KnownList._data : "(the list could not be built)");
    Memory_Free(KnownList._data);
    return Result;
}

/* Checks that a number read from a definition lies in [minimum, maximum] (a NaN never does); the message names the
 * key. */
static Error CheckFactorRange(const unsigned char* key, float value, float minimum, float maximum)
{
    if ((value >= minimum) && (value <= maximum))
    {
        return Error_CreateSuccess();
    }
    if (isinf(maximum))
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"\"%s\" is %g; it must be at least %g.", key,
            (double)value, (double)minimum);
    }
    return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"\"%s\" is %g; it must be in [%g, %g].", key,
        (double)value, (double)minimum, (double)maximum);
}

/* Reads one optional factor in [minimum, maximum]: absent leaves *outHas false. */
static Error ReadFactorKey(JSONCompound* compound, const unsigned char* key, float minimum, float maximum, bool* outHas,
    float* outValue)
{
    Error Result = AssetJSON_ReadOptionalFloat(compound, key, 0.0f, outValue, outHas);
    if ((Result.Code != ErrorCode_Success) || !*outHas)
    {
        return Result;
    }
    return CheckFactorRange(key, *outValue, minimum, maximum);
}

/* Reads one optional colour: absent leaves *outHas false. */
static Error ReadColorKey(JSONCompound* compound, const unsigned char* key, bool* outHas, Color* outColor)
{
    *outHas = false;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if ((GetResult.Code != ErrorCode_Success) || !Found)
    {
        return GetResult;
    }
    Error ColorResult = AssetJSON_ParseColor(&Value, outColor);
    if (ColorResult.Code != ErrorCode_Success)
    {
        Error Wrapped = Error_Construct3(ColorResult.Code, u8"\"%s\": %s", key,
            (ColorResult.Message != NULL) ? ColorResult.Message : (const unsigned char*)u8"not a colour.");
        Error_Deconstruct(&ColorResult);
        return Wrapped;
    }
    *outHas = true;
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

Error AssetJSON_ReadTextureProperties(JSONCompound* compound, const unsigned char* key, bool areMipmapsSupported,
    TextureProperties* outProps)
{
    outProps->Filter = TEXTURE_FILTER_BILINEAR;
    outProps->Wrap = TEXTURE_WRAP_REPEAT;
    outProps->AreMipmapsEnabled = false;

    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptionalVerified(compound, key, JSONValueType_Compound, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"\"%s\" must be a JSON object.", key);
    }
    if (!Found) { return Error_CreateSuccess(); }
    JSONCompound* Properties = Value.Value.Compound;

    Error Result = ReadTexturePropertyName(Properties, u8"filtering", TextureFilterNames, ARRAY_LENGTH(TextureFilterNames),
        u8"\"point\", \"bilinear\" or \"trilinear\"", &outProps->Filter);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = ReadTexturePropertyName(Properties, u8"wrap", TextureWrapNames, ARRAY_LENGTH(TextureWrapNames),
        u8"\"repeat\", \"clamp\", \"mirror_repeat\" or \"mirror_clamp\"", &outProps->Wrap);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = AssetJSON_ReadOptionalBoolean(Properties, u8"mipmaps", false, &outProps->AreMipmapsEnabled);
    if (Result.Code != ErrorCode_Success) { return Result; }

    if (outProps->Filter != TEXTURE_FILTER_TRILINEAR)
    {
        return Error_CreateSuccess();
    }
    if (!areMipmapsSupported)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Texture filtering \"trilinear\" blends mip levels, which this asset's textures do not have; use "
            u8"\"point\" or \"bilinear\".");
    }
    if (!outProps->AreMipmapsEnabled)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Texture filtering \"trilinear\" blends mip levels, so it needs \"mipmaps\": true.");
    }
    return Error_CreateSuccess();
}

Error TextureProperties_Apply(const TextureProperties* self, Texture2D* texture, const unsigned char* description)
{
    bool NeedsMipmaps = self->AreMipmapsEnabled && (texture->mipmaps <= 1)
        && ((texture->width > 1) || (texture->height > 1));
    if (NeedsMipmaps)
    {
        // raylib would call glGenerateMipmap on a compressed texture too, which fails, yet still count a full mip chain.
        if (texture->format >= PIXELFORMAT_COMPRESSED_DXT1_RGB)
        {
            return Error_Construct3(ErrorCode_InvalidAssetData,
                u8"Cannot generate mipmaps for the compressed texture \"%s\": store its mip levels in the file, or set "
                u8"\"mipmaps\" to false.", description);
        }
        GenTextureMipmaps(texture);
        if (texture->mipmaps <= 1)
        {
            return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to generate mipmaps for the texture \"%s\".",
                description);
        }
    }
    // The filter goes last: raylib picks the mipmapped GL filter from the texture's mipmap count.
    SetTextureWrap(*texture, self->Wrap);
    SetTextureFilter(*texture, self->Filter);
    return Error_CreateSuccess();
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

Error AssetJSON_ValueToFloat(const JSONObjectValue* value, float* outValue)
{
    double Value = 0.0;
    Error Result = GameJSON_ValueToDouble(value, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (fabs(Value) > (double)FLT_MAX)
    {
        return Error_Construct3(ErrorCode_InvalidJSON, u8"The number %g is beyond the float range.", Value);
    }
    *outValue = (float)Value;
    return Error_CreateSuccess();
}

Error AssetJSON_ReadOptionalFloat(JSONCompound* compound, const unsigned char* key, float defaultValue,
    float* outValue, bool* outFound)
{
    *outValue = defaultValue;
    if (outFound != NULL)
    {
        *outFound = false;
    }
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if ((GetResult.Code != ErrorCode_Success) || !Found)
    {
        return GetResult;
    }

    float Parsed = 0.0f;
    Error Result = AssetJSON_ValueToFloat(&Value, &Parsed);
    if (Result.Code != ErrorCode_Success)
    {
        Error Wrapped = Error_Construct3(Result.Code, u8"\"%s\": %s", key,
            (Result.Message != NULL) ? Result.Message : (const unsigned char*)u8"not a number.");
        Error_Deconstruct(&Result);
        return Wrapped;
    }
    *outValue = Parsed;
    if (outFound != NULL)
    {
        *outFound = true;
    }
    return Error_CreateSuccess();
}

void AssetJSON_CopyPrintable(const unsigned char* text, char* buffer, size_t capacity)
{
    size_t Length = 0U;
    if (text != NULL)
    {
        while ((text[Length] != 0U) && ((Length + 1U) < capacity))
        {
            unsigned char Byte = text[Length];
            bool IsPrintable = (Byte >= FIRST_PRINTABLE_ASCII) && (Byte <= LAST_PRINTABLE_ASCII);
            buffer[Length] = IsPrintable ? (char)Byte : '?';
            Length++;
        }
    }
    buffer[Length] = '\0';
}

Error AssetJSON_RejectUnknownKeys(JSONCompound* compound, const unsigned char* const* knownKeys, size_t knownKeyCount,
    const char* where)
{
    EnumeratorStorage Storage;
    ICollection* Entries = JSONCompound_GetEntryCollection(compound);
    bool IsOnStack = (Entries != NULL) && (ICollection_GetEnumeratorSize(Entries) <= sizeof(Storage.Bytes));
    CollectionEnumerator* Enumerator = NULL;
    if (Entries != NULL)
    {
        Enumerator = IsOnStack ? ICollection_InitEnumerator(Entries, Storage.Bytes)
            : ICollection_CreateEnumerator(Entries);
    }
    if (Enumerator == NULL)
    {
        return Error_Construct4(ErrorCode_BufferTooSmall, "Could not enumerate the keys of %s.", where);
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
        if (!IsKnownKey(Entry.Key, knownKeys, knownKeyCount))
        {
            Result = CreateUnknownKeyError(Entry.Key, knownKeys, knownKeyCount, where);
            break;
        }
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    }

    if (IsOnStack)
    {
        CollectionEnumerator_Deconstruct(Enumerator);
    }
    else
    {
        CollectionEnumerator_Destroy(Enumerator);
    }
    return Result;
}

Error AssetJSON_ReadMaterialSlot(JSONCompound* compound, int32_t* outSlot)
{
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, (const unsigned char*)u8"slot", &Value, &Found);
    if (GetResult.Code != ErrorCode_Success)
    {
        return GetResult;
    }
    int64_t Slot = -1;
    if (Found)
    {
        Error IntegerResult = GameJSON_ValueToInt64(&Value, &Slot);
        if (IntegerResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&IntegerResult);
            Slot = -1;
        }
    }
    if ((Slot < 0) || (Slot > (int64_t)INT32_MAX))
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"A material override requires a \"slot\": a whole number from 0 to %" PRId32 u8".", INT32_MAX);
    }
    *outSlot = (int32_t)Slot;
    return Error_CreateSuccess();
}

Error AssetJSON_ReadMaterialFactors(JSONCompound* compound, GameMaterialFactors* outFactors)
{
    Memory_Zero(outFactors, sizeof(*outFactors));
    outFactors->Tint = WHITE;
    outFactors->EmissiveColor = WHITE;

    Error Result = ReadColorKey(compound, (const unsigned char*)u8"tint", &outFactors->HasTint, &outFactors->Tint);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadColorKey(compound, (const unsigned char*)u8"emissive_color", &outFactors->HasEmissiveColor,
            &outFactors->EmissiveColor);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadFactorKey(compound, (const unsigned char*)u8"metallic", MATERIAL_FACTOR_MINIMUM,
            MATERIAL_FACTOR_MAXIMUM, &outFactors->HasMetallic, &outFactors->Metallic);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadFactorKey(compound, (const unsigned char*)u8"roughness", MATERIAL_FACTOR_MINIMUM,
            MATERIAL_FACTOR_MAXIMUM, &outFactors->HasRoughness, &outFactors->Roughness);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadFactorKey(compound, (const unsigned char*)u8"emissive_intensity", MATERIAL_FACTOR_MINIMUM,
            INFINITY, &outFactors->HasEmissiveIntensity, &outFactors->EmissiveIntensity);
    }
    return Result;
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
