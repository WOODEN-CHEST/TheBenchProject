#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "Climate.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"

/*
 * The "climate" asset type (references/asset_structure.md, "Climates"): the definition's parser, which checks every rule
 * while the definitions are read (a bad file is skipped at startup, not at load: Climate_ReadJSON), and the loader.
 *
 * A climate has no dependencies: the loaded Climate is a copy of the definition's (one block, no pointers inside), so
 * nothing in it points into the definition, which may be replaced while the asset is loaded.
 */


// Types.
typedef struct ClimateDefinitionStruct
{
    AssetDefinition Base;
    Climate* Climate;
} ClimateDefinition;


// Static functions.
static void ClimateLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    Climate_Destroy(self->DestroyContext);
}

static const LoadedAssetVTable ClimateLoadedVTable = { ClimateLoaded_Destroy };

static Error ClimateDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    (void)manager;
    (void)dependencyUser;
    ClimateDefinition* Definition = self;

    Climate* Loaded = Climate_Clone(Definition->Climate);
    outLoaded->Asset = Loaded;
    outLoaded->VTable = &ClimateLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void ClimateDefinition_Destroy(void* self)
{
    ClimateDefinition* Definition = self;
    Climate_Destroy(Definition->Climate);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable ClimateDefinitionVTable =
{
    ClimateDefinition_LoadAsset, ClimateDefinition_Destroy
};


// Public functions.
Error ClimateDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    ClimateDefinition* Definition = Memory_Allocate(sizeof(ClimateDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &ClimateDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Climate_ReadJSON(Definition->Base.Name, RootCompound, &Definition->Climate);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        ClimateDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
