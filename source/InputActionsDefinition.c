#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "InputActions.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"

/*
 * The "input_actions" asset type (references/asset_structure.md, "Input actions"): the definition's parser, which checks
 * every rule while the definitions are read (a bad file is skipped at startup, not at load: InputActions_ReadJSON), and
 * the loader.
 *
 * The asset has no dependencies: the loaded InputActions is a copy of the definition's, so nothing in it points into
 * the definition, which may be replaced while the asset is loaded.
 */


// Types.
typedef struct InputActionsDefinitionStruct
{
    AssetDefinition Base;
    InputActions* Actions;
} InputActionsDefinition;


// Static functions.
static void InputActionsLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    InputActions_Destroy(self->DestroyContext);
}

static const LoadedAssetVTable InputActionsLoadedVTable = { InputActionsLoaded_Destroy };

static Error InputActionsDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    (void)manager;
    (void)dependencyUser;
    InputActionsDefinition* Definition = self;

    InputActions* Loaded = InputActions_Clone(Definition->Actions);
    outLoaded->Asset = Loaded;
    outLoaded->VTable = &InputActionsLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void InputActionsDefinition_Destroy(void* self)
{
    InputActionsDefinition* Definition = self;
    InputActions_Destroy(Definition->Actions);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable InputActionsDefinitionVTable =
{
    InputActionsDefinition_LoadAsset, InputActionsDefinition_Destroy
};


// Public functions.
Error InputActionsDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    InputActionsDefinition* Definition = Memory_Allocate(sizeof(InputActionsDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &InputActionsDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = InputActions_ReadJSON(Definition->Base.Name, RootCompound, &Definition->Actions);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        InputActionsDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
