#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameFont.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"


// Macros.
/* Pixel height fonts are rasterized at; drawing scales from this base. */
#define ASSET_FONT_BASE_SIZE 32


// Types.
typedef struct FontDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
    unsigned char* Format; // owned; extension hint for reference locations, else NULL
} FontDefinition;

typedef struct FontLoadedStruct
{
    GameFont Font; // Asset points here
} FontLoaded;


// Static functions.
static void FontLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    FontLoaded* Loaded = self->DestroyContext;
    UnloadFont(Loaded->Font._rayFont);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable FontLoadedVTable = { FontLoaded_Destroy };

static Error FontDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    FontDefinition* Definition = self;

    AssetLocation Location = OwnedAssetLocation_View(&Definition->Location);
    AssetResourcePath* PathHandle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, Definition->Base.Type, &Location, Definition->Format, &PathHandle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }

    Font RayFont = LoadFontEx((const char*)AssetResourcePath_Get(PathHandle), ASSET_FONT_BASE_SIZE, NULL, 0);
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);

    if (RayFont.texture.id == 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load font \"%s\".", Definition->Base.Name);
    }

    FontLoaded* Loaded = Memory_Allocate(sizeof(FontLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Font._rayFont = RayFont;

    outLoaded->Asset = &Loaded->Font;
    outLoaded->VTable = &FontLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void FontDefinition_Destroy(void* self)
{
    FontDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Format);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable FontDefinitionVTable = { FontDefinition_LoadAsset, FontDefinition_Destroy };


// Public functions.
Error FontDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    FontDefinition* Definition = Memory_Allocate(sizeof(FontDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &FontDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadLocation(RootCompound, (const unsigned char*)u8"location", sourceDescription, &Definition->Location);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalOwnedString(RootCompound, (const unsigned char*)u8"format", &Definition->Format, NULL);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        FontDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
