#include <math.h>
#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameTexture.h"
#include "NamespacedKey.h"
#include "TerrainMaterial.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The "terrain_material" asset type (references/asset_structure.md, "Terrain materials"; TerrainMaterial.h): the
 * definition's parser, which reads the settings through their property table (unknown keys refused, every rule checked
 * when the definitions are read, the albedo required, the surface key put into its full form), and the loader, which
 * reads the pixels of the texture assets the maps name (AssetManager_LoadTexturePixels: their image files, never a GPU
 * texture) and packs them as the terrain's texture arrays take them: the albedo with the height in its alpha, the normal
 * map's x and y, the roughness and the AO. A map's size is checked when it loads (at most TERRAIN_MATERIAL_MAX_SIDE per
 * side; the height the albedo's size, since it goes into the albedo's map); a terrain checks that its materials' maps
 * share one size per kind.
 *
 * The loaded material keeps no pointer into the definition, which may be replaced while it is loaded, and has no
 * dependencies.
 */


// Macros.
/** How many bytes of a name from the file a message shows, and room for a message's "where". */
#define NAME_PRINT_CAPACITY ((size_t)129)
#define WHERE_CAPACITY ((size_t)256)
/** The most keys the definition may hold: its table's and the parser's own ("name"). */
#define MAX_KNOWN_KEYS ((size_t)24)
/** The sRGB gamma the mean colour is linearized with (the shaders' own). */
#define SRGB_GAMMA 2.2f


// Types.
typedef struct TerrainMaterialDefinitionStruct
{
    AssetDefinition Base;
    TerrainMaterialSettings Settings;
} TerrainMaterialDefinition;

/* One map as it is read: the texture's pixels (RGBA), and whether the definition names one. */
typedef struct SourceMapStruct
{
    TexturePixels Pixels;
    bool IsPresent;
} SourceMap;


// Fields.
/* The root's keys that are not settings (those come from the settings table). */
static const unsigned char* const RootOwnKeys[] = { u8"name" };
/* What each map slot is called in messages, in TerrainMaterialSettings' order of the names. */
static const char* const SlotNames[4] = { "albedo", "height", "normal", "roughness_ao" };


// Static functions: the definition.
/* The inner error with "Terrain material \"<name>\": " before its message, under @p code; releases it. */
static Error WrapError(const char* name, ErrorCode code, Error* inner)
{
    Error Wrapped = Error_Construct4(code, "Terrain material \"%s\": %s", name,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Refuses a root key that is neither "name" nor a setting's. */
static Error RejectUnknownKeys(const char* name, JSONCompound* root)
{
    const PropertyTable* Table = TerrainMaterialSettings_GetPropertyTable();
    const unsigned char* Keys[MAX_KNOWN_KEYS];
    size_t OwnKeyCount = sizeof(RootOwnKeys) / sizeof(RootOwnKeys[0]);
    size_t KeyCount = OwnKeyCount + Table->DescriptorCount;
    if (KeyCount > MAX_KNOWN_KEYS)
    {
        return Error_Construct4(ErrorCode_InvalidState,
            "Terrain material \"%s\": the table has more keys than the parser holds.", name);
    }
    for (size_t Index = 0U; Index < OwnKeyCount; Index++)
    {
        Keys[Index] = RootOwnKeys[Index];
    }
    for (size_t Index = 0U; Index < Table->DescriptorCount; Index++)
    {
        Keys[OwnKeyCount + Index] = Table->Descriptors[Index].Key;
    }
    char Where[WHERE_CAPACITY];
    snprintf(Where, sizeof(Where), "terrain material \"%s\"", name);
    return AssetJSON_RejectUnknownKeys(root, Keys, KeyCount, Where);
}

/* The surface key in its full form ("ground" becomes "tbp:ground"), replacing the settings' copy. */
static Error NormalizeSurface(const char* name, TerrainMaterialSettings* settings)
{
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer Key;
    GenericBuffer_CreateConstant(&Key, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    Error Result = NamespacedKey_Parse(settings->Surface, StringUTF8_GetByteLength(settings->Surface), NULL, &Key);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(name, ErrorCode_InvalidAssetDefinition, &Result);
    }
    size_t Length = StringUTF8_GetByteLength(Storage);
    unsigned char* Full = Memory_Allocate(Length + 1U);
    Memory_Copy(Storage, Full, Length + 1U);
    Memory_Free(settings->Surface);
    settings->Surface = Full;
    return Error_CreateSuccess();
}

/* Reads everything after the name: the settings through their table (a key left out keeps its default), then the rules
 * the table cannot say (the albedo is required; the surface is a namespaced key). */
static Error ParseDefinition(TerrainMaterialDefinition* definition, JSONCompound* root)
{
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(definition->Base.Name, Name, sizeof(Name));

    Error Result = RejectUnknownKeys(Name, root);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
    Options.UnknownKeyMode = PropertyUnknownKeyMode_Ignore;
    Result = Properties_ReadJSON(TerrainMaterialSettings_GetPropertyTable(), root, &definition->Settings, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(Name, Result.Code, &Result);
    }
    if (definition->Settings.Albedo == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidAssetDefinition, "Terrain material \"%s\": \"albedo\" must name the "
            "texture of its colour.", Name);
    }
    return NormalizeSurface(Name, &definition->Settings);
}


// Static functions: the asset.
static void DestroyMaterial(TerrainMaterial* material)
{
    if (material == NULL)
    {
        return;
    }
    TerrainMaterialSettings_Deconstruct(&material->_settings);
    Memory_Free(material->_pixels);
    Memory_Free(material);
}

static void TerrainMaterialLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    DestroyMaterial(self->DestroyContext);
}

static const LoadedAssetVTable TerrainMaterialLoadedVTable = { TerrainMaterialLoaded_Destroy };

/* Reads one named map's pixels (nothing for a slot the definition leaves empty), at most TERRAIN_MATERIAL_MAX_SIDE per
 * side (the reader refuses a larger image before converting it). */
static Error ReadMap(const char* name, AssetManager* manager, const unsigned char* texture, const char* slot,
    SourceMap* outMap)
{
    Memory_Zero(outMap, sizeof(*outMap));
    if (texture == NULL)
    {
        return Error_CreateSuccess();
    }
    Error Result = AssetManager_LoadTexturePixels(manager, texture, TERRAIN_MATERIAL_MAX_SIDE, &outMap->Pixels);
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(texture, Printable, sizeof(Printable));
    if (Result.Code != ErrorCode_Success)
    {
        Error Wrapped = Error_Construct4(Result.Code, "Terrain material \"%s\": its %s \"%s\" could not be read: %s", name,
            slot, Printable, (Result.Message != NULL) ? (const char*)Result.Message : "an error.");
        Error_Deconstruct(&Result);
        return Wrapped;
    }
    outMap->IsPresent = true;
    return Error_CreateSuccess();
}

static float ToLinear(uint8_t value)
{
    return powf((float)value / 255.0f, SRGB_GAMMA);
}

/* Packs the read maps into the material's block (the albedo with the height in alpha, the normal's and the roughness
 * map's first two channels) and works out the mean colour. */
static void PackMaps(TerrainMaterial* material, const SourceMap* maps)
{
    const TexturePixels* Albedo = &maps[0].Pixels;
    const TexturePixels* Height = maps[1].IsPresent ? &maps[1].Pixels : NULL;
    const TexturePixels* Sources[TerrainMaterialMapKind_Count] =
    {
        Albedo, maps[2].IsPresent ? &maps[2].Pixels : NULL, maps[3].IsPresent ? &maps[3].Pixels : NULL
    };
    size_t Offsets[TerrainMaterialMapKind_Count] = { 0U, 0U, 0U };
    size_t Total = 0U;
    for (uint32_t Kind = 0U; Kind < (uint32_t)TerrainMaterialMapKind_Count; Kind++)
    {
        Offsets[Kind] = Total;
        if (Sources[Kind] != NULL)
        {
            Total += (size_t)Sources[Kind]->Width * (size_t)Sources[Kind]->Height
                * TerrainMaterial_GetMapTexelSize((TerrainMaterialMapKind)Kind);
        }
    }
    material->_pixels = Memory_Allocate(Total);
    for (uint32_t Kind = 0U; Kind < (uint32_t)TerrainMaterialMapKind_Count; Kind++)
    {
        const TexturePixels* Source = Sources[Kind];
        if (Source == NULL)
        {
            continue;
        }
        uint8_t* Target = material->_pixels + Offsets[Kind];
        size_t TexelCount = (size_t)Source->Width * (size_t)Source->Height;
        for (size_t Texel = 0U; Texel < TexelCount; Texel++)
        {
            const uint8_t* From = &Source->Pixels[Texel * 4U];
            if (Kind == (uint32_t)TerrainMaterialMapKind_AlbedoHeight)
            {
                uint8_t* To = &Target[Texel * 4U];
                To[0] = From[0];
                To[1] = From[1];
                To[2] = From[2];
                To[3] = (Height != NULL) ? Height->Pixels[Texel * 4U] : TERRAIN_MATERIAL_FLAT_HEIGHT;
            }
            else
            {
                Target[Texel * 2U] = From[0];
                Target[(Texel * 2U) + 1U] = From[1];
            }
        }
        material->_maps[Kind]._pixels = Target;
        material->_maps[Kind]._width = Source->Width;
        material->_maps[Kind]._height = Source->Height;
    }

    // The albedo's mean in linear light (summed in double: up to a million texels), times the tint.
    double Sums[3] = { 0.0, 0.0, 0.0 };
    size_t TexelCount = (size_t)Albedo->Width * (size_t)Albedo->Height;
    for (size_t Texel = 0U; Texel < TexelCount; Texel++)
    {
        for (size_t Channel = 0U; Channel < 3U; Channel++)
        {
            Sums[Channel] += (double)ToLinear(Albedo->Pixels[(Texel * 4U) + Channel]);
        }
    }
    Color Tint = material->_settings.AlbedoTint;
    double Count = (TexelCount > 0U) ? (double)TexelCount : 1.0;
    material->_meanColor = (Vector3){ (float)(Sums[0] / Count) * ToLinear(Tint.r),
        (float)(Sums[1] / Count) * ToLinear(Tint.g), (float)(Sums[2] / Count) * ToLinear(Tint.b) };
}

static Error TerrainMaterialDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    TerrainMaterialDefinition* Definition = self;
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Definition->Base.Name, Name, sizeof(Name));
    const unsigned char* Textures[4] =
    {
        Definition->Settings.Albedo, Definition->Settings.Height, Definition->Settings.Normal,
        Definition->Settings.RoughnessAo
    };
    SourceMap Maps[4];
    Memory_Zero(Maps, sizeof(Maps));
    Error Result = Error_CreateSuccess();
    for (size_t Slot = 0U; (Slot < 4U) && (Result.Code == ErrorCode_Success); Slot++)
    {
        Result = ReadMap(Name, manager, Textures[Slot], SlotNames[Slot], &Maps[Slot]);
    }
    if ((Result.Code == ErrorCode_Success) && Maps[1].IsPresent && ((Maps[1].Pixels.Width != Maps[0].Pixels.Width)
        || (Maps[1].Pixels.Height != Maps[0].Pixels.Height)))
    {
        char Height[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(Definition->Settings.Height, Height, sizeof(Height));
        Result = Error_Construct4(ErrorCode_InvalidAssetData, "Terrain material \"%s\": its height \"%s\" is %d x %d "
            "pixels and its albedo %d x %d; they must be the same size (the height goes into the albedo's map).", Name,
            Height, (int)Maps[1].Pixels.Width, (int)Maps[1].Pixels.Height, (int)Maps[0].Pixels.Width,
            (int)Maps[0].Pixels.Height);
    }
    TerrainMaterial* Loaded = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Loaded = Memory_Allocate(sizeof(TerrainMaterial));
        Memory_Zero(Loaded, sizeof(*Loaded));
        Result = Properties_CopyTo(TerrainMaterialSettings_GetPropertyTable(), &Definition->Settings,
            &Loaded->_settings);
    }
    if (Result.Code == ErrorCode_Success)
    {
        PackMaps(Loaded, Maps);
    }
    for (size_t Slot = 0U; Slot < 4U; Slot++)
    {
        TexturePixels_Deconstruct(&Maps[Slot].Pixels);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyMaterial(Loaded);
        return Result;
    }
    outLoaded->Asset = Loaded;
    outLoaded->VTable = &TerrainMaterialLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void TerrainMaterialDefinition_Destroy(void* self)
{
    TerrainMaterialDefinition* Definition = self;
    TerrainMaterialSettings_Deconstruct(&Definition->Settings);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable TerrainMaterialDefinitionVTable =
{
    TerrainMaterialDefinition_LoadAsset, TerrainMaterialDefinition_Destroy
};


// Public functions.
Error TerrainMaterialDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    TerrainMaterialDefinition* Definition = Memory_Allocate(sizeof(TerrainMaterialDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &TerrainMaterialDefinitionVTable;
    Result = TerrainMaterialSettings_SetDefaults(&Definition->Settings);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDefinition(Definition, RootCompound);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        TerrainMaterialDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
