#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "AnimationGraph.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"

/*
 * The "animation_graph" asset type (references/asset_structure.md, "Animation graphs"): the definition is the graph
 * itself, read and checked once when the definitions are read (AnimationGraph_CreateFromJSON), so a bad graph is
 * skipped at startup, not when an animator first plays it. A graph holds no pointers, so loading it copies the
 * definition's block: the asset never points into the definition, which may be replaced while the asset is loaded. It
 * has no dependencies (it names clips; the animator's libraries supply them).
 */


// Types.
typedef struct AnimationGraphDefinitionStruct
{
    AssetDefinition Base;
    AnimationGraph* Graph; // owned: the checked graph, copied by every load
} AnimationGraphDefinition;


// Static functions.
static void GraphLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    AnimationGraph_Destroy(self->DestroyContext);
}

static const LoadedAssetVTable GraphLoadedVTable = { GraphLoaded_Destroy };

static Error AnimationGraphDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    (void)manager;
    (void)dependencyUser;
    AnimationGraphDefinition* Definition = self;
    AnimationGraph* Graph = AnimationGraph_Clone(Definition->Graph);
    outLoaded->Asset = Graph;
    outLoaded->VTable = &GraphLoadedVTable;
    outLoaded->DestroyContext = Graph;
    return Error_CreateSuccess();
}

static void AnimationGraphDefinition_Destroy(void* self)
{
    AnimationGraphDefinition* Definition = self;
    AnimationGraph_Destroy(Definition->Graph);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable AnimationGraphDefinitionVTable =
{
    AnimationGraphDefinition_LoadAsset, AnimationGraphDefinition_Destroy
};


// Public functions.
Error AnimationGraphDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    AnimationGraphDefinition* Definition = Memory_Allocate(sizeof(AnimationGraphDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &AnimationGraphDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AnimationGraph_CreateFromJSON(RootCompound, Definition->Base.Name, &Definition->Graph);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        AnimationGraphDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
