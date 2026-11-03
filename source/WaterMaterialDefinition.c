#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "WaterMaterial.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"

/*
 * The "water_material" asset type (references/asset_structure.md, "Water materials"; WaterMaterial.h): the definition's
 * parser, which reads the settings through their property table (unknown keys refused, every rule checked when the
 * definitions are read), and the loader, which loads the texture assets the maps name as the material's DEPENDENCIES
 * (under its dependency user: they stay loaded exactly as long as it does). A map that does not load fails the material,
 * naming it; the manager then releases what had loaded.
 *
 * The loaded material keeps no pointer into the definition, which may be replaced while it is loaded: its settings are
 * a copy, its textures borrowed from the dependencies.
 */


// Macros.
/** How many bytes of a name from the file a message shows, and room for a message's "where". */
#define NAME_PRINT_CAPACITY ((size_t)129)
#define WHERE_CAPACITY ((size_t)256)
/** The most keys the definition may hold: its table's and the parser's own ("name"). */
#define MAX_KNOWN_KEYS ((size_t)32)


// Types.
typedef struct WaterMaterialDefinitionStruct
{
    AssetDefinition Base;
    WaterMaterialSettings Settings;
} WaterMaterialDefinition;


// Fields.
/* The root's keys that are not settings (those come from the settings table). */
static const unsigned char* const RootOwnKeys[] = { u8"name" };
/* What each map is called in messages: its key, in WaterMaterialMap's order. */
static const char* const MapKeys[WaterMaterialMap_Count] = { "normal_a", "normal_b", "foam" };


// Static functions: the definition.
/* The inner error with "Water material \"<name>\": " before its message, under @p code; releases it. */
static Error WrapError(const char* name, ErrorCode code, Error* inner)
{
    Error Wrapped = Error_Construct4(code, "Water material \"%s\": %s", name,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Refuses a root key that is neither "name" nor a setting's. */
static Error RejectUnknownKeys(const char* name, JSONCompound* root)
{
    const PropertyTable* Table = WaterMaterialSettings_GetPropertyTable();
    const unsigned char* Keys[MAX_KNOWN_KEYS];
    size_t OwnKeyCount = sizeof(RootOwnKeys) / sizeof(RootOwnKeys[0]);
    size_t KeyCount = OwnKeyCount + Table->DescriptorCount;
    if (KeyCount > MAX_KNOWN_KEYS)
    {
        return Error_Construct4(ErrorCode_InvalidState,
            "Water material \"%s\": the table has more keys than the parser holds.", name);
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
    snprintf(Where, sizeof(Where), "water material \"%s\"", name);
    return AssetJSON_RejectUnknownKeys(root, Keys, KeyCount, Where);
}

/* Reads everything after the name: the settings through their table (a key left out keeps its default). */
static Error ParseDefinition(WaterMaterialDefinition* definition, JSONCompound* root)
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
    Result = Properties_ReadJSON(WaterMaterialSettings_GetPropertyTable(), root, &definition->Settings, &Options);
    return (Result.Code == ErrorCode_Success) ? Result : WrapError(Name, Result.Code, &Result);
}


// Static functions: the asset.
static void DestroyMaterial(WaterMaterial* material)
{
    if (material == NULL)
    {
        return;
    }
    WaterMaterialSettings_Deconstruct(&material->_settings);
    Memory_Free(material);
}

static void WaterMaterialLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    DestroyMaterial(self->DestroyContext); // its maps are dependencies: the manager releases them
}

static const LoadedAssetVTable WaterMaterialLoadedVTable = { WaterMaterialLoaded_Destroy };

/* Loads one named map as a dependency (nothing for a map the definition leaves out). */
static Error LoadMap(const char* name, AssetManager* manager, AssetTypeID textureType, AssetUserID dependencyUser,
    const unsigned char* texture, const char* key, const GameTexture** outTexture)
{
    *outTexture = NULL;
    if (texture == NULL)
    {
        return Error_CreateSuccess();
    }
    void* Asset = NULL;
    Error Result = AssetManager_LoadAssetSingle(manager, textureType, texture, dependencyUser, &Asset);
    if (Result.Code != ErrorCode_Success)
    {
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(texture, Printable, sizeof(Printable));
        Error Wrapped = Error_Construct4(Result.Code, "Water material \"%s\": its %s \"%s\": %s", name, key, Printable,
            (Result.Message != NULL) ? (const char*)Result.Message : "it did not load.");
        Error_Deconstruct(&Result);
        return Wrapped;
    }
    *outTexture = Asset;
    return Error_CreateSuccess();
}

static Error WaterMaterialDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    WaterMaterialDefinition* Definition = self;
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Definition->Base.Name, Name, sizeof(Name));
    AssetTypeID TextureType = ASSET_TYPE_ID_INVALID;
    Error Result = AssetManager_GetAssetTypeByName(manager, ASSET_TYPE_NAME_TEXTURE, &TextureType);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(Name, Result.Code, &Result);
    }
    if (TextureType == ASSET_TYPE_ID_INVALID)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Water material \"%s\": the texture asset type is not "
            "registered.", Name);
    }

    WaterMaterial* Loaded = Memory_Allocate(sizeof(WaterMaterial));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Result = Properties_CopyTo(WaterMaterialSettings_GetPropertyTable(), &Definition->Settings, &Loaded->_settings);
    for (uint32_t Map = 0U; (Map < (uint32_t)WaterMaterialMap_Count) && (Result.Code == ErrorCode_Success); Map++)
    {
        Result = LoadMap(Name, manager, TextureType, dependencyUser,
            WaterMaterialSettings_GetMapName(&Definition->Settings, (WaterMaterialMap)Map), MapKeys[Map],
            &Loaded->_maps[Map]);
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyMaterial(Loaded); // the manager releases the maps that loaded
        return Result;
    }
    outLoaded->Asset = Loaded;
    outLoaded->VTable = &WaterMaterialLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void WaterMaterialDefinition_Destroy(void* self)
{
    WaterMaterialDefinition* Definition = self;
    WaterMaterialSettings_Deconstruct(&Definition->Settings);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable WaterMaterialDefinitionVTable =
{
    WaterMaterialDefinition_LoadAsset, WaterMaterialDefinition_Destroy
};


// Public functions.
Error WaterMaterialDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    WaterMaterialDefinition* Definition = Memory_Allocate(sizeof(WaterMaterialDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &WaterMaterialDefinitionVTable;
    Result = WaterMaterialSettings_SetDefaults(&Definition->Settings);
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
        WaterMaterialDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
