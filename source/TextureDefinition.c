#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameTexture.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"
#include "wr/WRString.h"
#include <ctype.h>
#include <string.h>

/*
 * Standalone texture loader: one image file (png, bmp, gif, qoi or dds) uploaded as a GPU texture of at most
 * TEXTURE_MAX_SIDE pixels per side, with the definition's texture_properties (filtering, wrap, mipmaps) applied and its
 * "color_space" recorded on the wrapper. The image is decoded on the CPU first, so an oversized file fails before
 * anything reaches the GPU. Locations must be file paths: nothing would name a reference's file type.
 */


// Macros.
/** Largest width or height of a texture, in pixels (the sprite sheet atlas limit too). */
#define TEXTURE_MAX_SIDE 8192
/** What a texture location points at, for parse-time messages. */
#define TEXTURE_RESOURCE_KIND ((const unsigned char*)u8"texture")


// Types.
typedef struct TextureDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
    TextureProperties Properties;
    bool IsSrgb;
} TextureDefinition;

typedef struct TextureLoadedStruct
{
    GameTexture Texture; // Asset points here
} TextureLoaded;


// Static functions.
static bool StringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool Equal = false;
    Error Result = StringUTF8_EqualsExact(a, b, &Equal);
    if (Result.Code != ErrorCode_Success) { Error_Deconstruct(&Result); return false; }
    return Equal;
}

static void TextureLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    TextureLoaded* Loaded = self->DestroyContext;
    UnloadTexture(Loaded->Texture._rayTexture);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable TextureLoadedVTable = { TextureLoaded_Destroy };

/* Whether a file path ends in ".dds", in any case (raylib picks its DDS reader by the extension). */
static bool IsDdsPath(const char* path)
{
    const char* Extension = strrchr(path, '.');
    return (Extension != NULL) && (strlen(Extension) == 4U) && (tolower((unsigned char)Extension[1]) == 'd')
        && (tolower((unsigned char)Extension[2]) == 'd') && (tolower((unsigned char)Extension[3]) == 's');
}

/* Decodes the definition's image file and checks its size: 1 to maxSide pixels per side. A DDS file is refused before it
   is decoded unless readsDds is set. On success the caller unloads *outImage with UnloadImage. */
static Error LoadTextureImage(const TextureDefinition* definition, AssetManager* manager, int32_t maxSide, bool readsDds,
    Image* outImage)
{
    AssetLocation Location = OwnedAssetLocation_View(&definition->Location);
    AssetResourcePath* Handle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, definition->Base.Type, &Location,
        &ASSET_EXTENSIONS_TEXTURE, NULL, &Handle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }
    const char* Path = (const char*)AssetResourcePath_Get(Handle);
    Image Decoded;
    Memory_Zero(&Decoded, sizeof(Decoded));
    bool IsRefusedDds = !readsDds && IsDdsPath(Path);
    if (!IsRefusedDds)
    {
        Decoded = LoadImage(Path);
    }
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, Handle);
    Error_Deconstruct(&ReleaseResult);

    if (IsRefusedDds)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Texture \"%s\" (\"%s\") is a DDS file, whose pixels are "
            u8"not read (raylib's DDS reader trusts the sizes its header claims); use a png, bmp, gif or qoi image.",
            definition->Base.Name, definition->Location.Value);
    }
    if (Decoded.data == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to decode the image \"%s\" of texture \"%s\".",
            definition->Location.Value, definition->Base.Name);
    }
    if ((Decoded.width < 1) || (Decoded.height < 1))
    {
        Error SizeError = Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Texture \"%s\" (\"%s\") is %d x %d pixels; an image has at least one per side.", definition->Base.Name,
            definition->Location.Value, Decoded.width, Decoded.height);
        UnloadImage(Decoded);
        return SizeError;
    }
    if ((Decoded.width > maxSide) || (Decoded.height > maxSide))
    {
        Error SizeError = Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Texture \"%s\" (\"%s\") is %d x %d pixels; at most %d per side are allowed.", definition->Base.Name,
            definition->Location.Value, Decoded.width, Decoded.height, (int)maxSide);
        UnloadImage(Decoded);
        return SizeError;
    }
    *outImage = Decoded;
    return Error_CreateSuccess();
}

static Error TextureDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    TextureDefinition* Definition = self;

    Image Decoded;
    Memory_Zero(&Decoded, sizeof(Decoded));
    Error Result = LoadTextureImage(Definition, manager, TEXTURE_MAX_SIDE, true, &Decoded);
    if (Result.Code != ErrorCode_Success) { return Result; }

    Texture2D RayTexture = LoadTextureFromImage(Decoded);
    UnloadImage(Decoded);
    if (RayTexture.id == 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to upload texture \"%s\" to the GPU.",
            Definition->Base.Name);
    }

    Result = TextureProperties_Apply(&Definition->Properties, &RayTexture, Definition->Location.Value);
    if (Result.Code != ErrorCode_Success)
    {
        UnloadTexture(RayTexture);
        return Result;
    }

    TextureLoaded* Loaded = Memory_Allocate(sizeof(TextureLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Texture._rayTexture = RayTexture;
    Loaded->Texture._isSrgb = Definition->IsSrgb;

    outLoaded->Asset = &Loaded->Texture;
    outLoaded->VTable = &TextureLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void TextureDefinition_Destroy(void* self)
{
    TextureDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable TextureDefinitionVTable = { TextureDefinition_LoadAsset, TextureDefinition_Destroy };

/* Converts a decoded image to 8-bit RGBA in place (raylib's ImageFormat, which leaves a compressed image as it is). */
static Error ConvertToRgba(const TextureDefinition* definition, Image* image)
{
    if (image->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    {
        ImageFormat(image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    if ((image->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) || (image->data == NULL))
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"The image \"%s\" of texture \"%s\" is compressed (a "
            u8"DDS's block format); its pixels cannot be read. Save it uncompressed (png, bmp, qoi).",
            definition->Location.Value, definition->Base.Name);
    }
    return Error_CreateSuccess();
}

/* Reads the optional "color_space": "srgb" (the default) or "linear". */
static Error ReadColorSpace(JSONCompound* root, bool* outIsSrgb)
{
    *outIsSrgb = true;
    unsigned char* Text = NULL;
    bool Found = false;
    Error ReadResult = AssetJSON_ReadOptionalOwnedString(root, (const unsigned char*)u8"color_space", &Text, &Found);
    if (ReadResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&ReadResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"\"color_space\" must be \"srgb\" or \"linear\".");
    }
    if (!Found) { return Error_CreateSuccess(); }

    Error Result = Error_CreateSuccess();
    if (StringsEqual(Text, (const unsigned char*)u8"linear"))
    {
        *outIsSrgb = false;
    }
    else if (!StringsEqual(Text, (const unsigned char*)u8"srgb"))
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"\"color_space\" is \"%s\"; it must be \"srgb\" or \"linear\".", Text);
    }
    Memory_Free(Text);
    return Result;
}


// Public functions.
Error TextureDefinition_LoadPixels(const AssetDefinition* definition, AssetManager* manager, int32_t maxSide,
    TexturePixels* outPixels)
{
    Memory_Zero(outPixels, sizeof(*outPixels));
    if (definition->VTable != &TextureDefinitionVTable)
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"Texture \"%s\" is not read from an image file, so its "
            u8"pixels cannot be read.", definition->Name);
    }
    const TextureDefinition* Definition = (const TextureDefinition*)(const void*)definition;
    Image Decoded;
    Memory_Zero(&Decoded, sizeof(Decoded));
    // The size is checked before the conversion, which goes through floats (16 bytes a texel), and DDS files are never
    // decoded: raylib's DDS reader copies as many bytes as the header claims, past the end of a short file.
    int32_t Limit = (maxSide < TEXTURE_MAX_SIDE) ? maxSide : TEXTURE_MAX_SIDE;
    Error Result = LoadTextureImage(Definition, manager, Limit, false, &Decoded);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ConvertToRgba(Definition, &Decoded);
    if (Result.Code != ErrorCode_Success)
    {
        UnloadImage(Decoded);
        return Result;
    }
    // raylib allocated the image's data; the caller frees ours (Memory_Free), so the pixels are copied once.
    size_t ByteCount = (size_t)Decoded.width * (size_t)Decoded.height * 4U;
    outPixels->Pixels = Memory_Allocate(ByteCount);
    Memory_Copy(Decoded.data, outPixels->Pixels, ByteCount);
    outPixels->Width = Decoded.width;
    outPixels->Height = Decoded.height;
    outPixels->IsSrgb = Definition->IsSrgb;
    UnloadImage(Decoded);
    return Error_CreateSuccess();
}

Error TextureDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
    const unsigned char* sourceDescription, AssetDefinition** outDefinition)
{
    (void)manager;
    *outDefinition = NULL;

    JSONObjectPool* Pool = UserData_GetPointer(userData);
    JSONObjectValue Root;
    JSONCompound* RootCompound = NULL;
    Error Result = AssetJSON_DeserializeRoot(Pool, rawData, sourceDescription, &Root, &RootCompound);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    TextureDefinition* Definition = Memory_Allocate(sizeof(TextureDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &TextureDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadLocation(RootCompound, (const unsigned char*)u8"location", sourceDescription,
            &Definition->Location);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = OwnedAssetLocation_RequireFile(&Definition->Location, TEXTURE_RESOURCE_KIND);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadTextureProperties(RootCompound, (const unsigned char*)u8"texture_properties", true,
            &Definition->Properties);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadColorSpace(RootCompound, &Definition->IsSrgb);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        TextureDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
