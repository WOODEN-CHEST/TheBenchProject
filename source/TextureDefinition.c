#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameTexture.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"
#include "wr/WRString.h"

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

/* Decodes the definition's image file and checks its size. On success the caller unloads *outImage with UnloadImage. */
static Error LoadTextureImage(const TextureDefinition* definition, AssetManager* manager, Image* outImage)
{
    AssetLocation Location = OwnedAssetLocation_View(&definition->Location);
    AssetResourcePath* Handle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, definition->Base.Type, &Location,
        &ASSET_EXTENSIONS_TEXTURE, NULL, &Handle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }
    Image Decoded = LoadImage((const char*)AssetResourcePath_Get(Handle));
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, Handle);
    Error_Deconstruct(&ReleaseResult);

    if (Decoded.data == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to decode the image \"%s\" of texture \"%s\".",
            definition->Location.Value, definition->Base.Name);
    }
    if ((Decoded.width > TEXTURE_MAX_SIDE) || (Decoded.height > TEXTURE_MAX_SIDE))
    {
        Error SizeError = Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Texture \"%s\" (\"%s\") is %d x %d pixels; at most %d per side are allowed.", definition->Base.Name,
            definition->Location.Value, Decoded.width, Decoded.height, TEXTURE_MAX_SIDE);
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
    Error Result = LoadTextureImage(Definition, manager, &Decoded);
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
