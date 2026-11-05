#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "Prefab.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"

/*
 * The "prefab" asset type (references/asset_structure.md, "Prefabs"; Prefab.h). A definition keeps its file's bytes and checks them
 * when it is read, so a bad file is skipped at startup; loading reads them again into a prefab of its own, which shares nothing with
 * the definition: it may be replaced while the prefab is loaded. No dependencies: what a prefab's objects name is loaded by those
 * objects when a copy makes them.
 */


// Macros.
/** How many bytes of a name from the file a message shows. */
#define NAME_PRINT_CAPACITY ((size_t)129)
/** How many bytes of a message that may quote the file a refusal keeps. */
#define MESSAGE_PRINT_CAPACITY ((size_t)1024)


// Types.
typedef struct PrefabDefinitionStruct
{
    AssetDefinition Base;
    /* The file, kept. */
    GenericBuffer Bytes;
    /* The JSON pool the type was registered with (borrowed: it outlives the manager's definitions). */
    JSONObjectPool* Pool;
} PrefabDefinition;


// Static functions.
/* "Prefab \"<name>\": <inner>", under InvalidAssetDefinition for what the file says wrong; releases @p inner. */
static Error WrapError(const unsigned char* name, Error* inner)
{
    char Name[NAME_PRINT_CAPACITY];
    char Message[MESSAGE_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(name, Name, sizeof(Name));
    AssetJSON_CopyPrintable((inner->Message != NULL) ? inner->Message : (const unsigned char*)u8"invalid.", Message, sizeof(Message));
    ErrorCode Code = (inner->Code == ErrorCode_InvalidJSON) ? ErrorCode_InvalidAssetDefinition : inner->Code;
    Error Wrapped = Error_Construct4(Code, "Prefab \"%s\": %s", Name, Message);
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Reads a file's bytes into a prefab; the JSON goes back to the pool either way. */
static Error ReadPrefab(AssetManager* manager, JSONObjectPool* pool, const GenericBuffer* bytes, const unsigned char* source,
    Prefab** outPrefab)
{
    JSONObjectValue Root;
    JSONCompound* RootCompound = NULL;
    Error Result = AssetJSON_DeserializeRoot(pool, bytes, source, &Root, &RootCompound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Prefab_Read(RootCompound, bytes->_count, pool, manager, outPrefab);
    }
    Error ReturnResult = JSONObjectPool_ReturnValue(pool, &Root);
    Error_Deconstruct(&ReturnResult);
    return Result;
}

static void PrefabLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    Prefab_Destroy(self->DestroyContext);
}

static const LoadedAssetVTable PrefabLoadedVTable = { PrefabLoaded_Destroy };

static Error PrefabDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    PrefabDefinition* Definition = self;
    Prefab* Loaded = NULL;
    Error Result = ReadPrefab(manager, Definition->Pool, &Definition->Bytes, Definition->Base.Name, &Loaded);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(Definition->Base.Name, &Result);
    }
    outLoaded->Asset = Loaded;
    outLoaded->VTable = &PrefabLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void PrefabDefinition_Destroy(void* self)
{
    PrefabDefinition* Definition = self;
    Memory_Free(Definition->Bytes._data);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable PrefabDefinitionVTable = { PrefabDefinition_LoadAsset, PrefabDefinition_Destroy };


// Public functions.
Error PrefabDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
    const unsigned char* sourceDescription, AssetDefinition** outDefinition)
{
    *outDefinition = NULL;
    JSONObjectPool* Pool = UserData_GetPointer(userData);
    PrefabDefinition* Definition = Memory_Allocate(sizeof(PrefabDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &PrefabDefinitionVTable;
    Definition->Pool = Pool;
    GenericBuffer_AllocateVariable(&Definition->Bytes, rawData->_count, sizeof(unsigned char));
    GenericBuffer_AppendRangeBytes(&Definition->Bytes, rawData->_data, rawData->_count);

    JSONObjectValue Root;
    JSONCompound* RootCompound = NULL;
    Error Result = AssetJSON_DeserializeRoot(Pool, rawData, sourceDescription, &Root, &RootCompound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    }
    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);
    if (Result.Code == ErrorCode_Success)
    {
        Prefab* Checked = NULL;
        Result = ReadPrefab(manager, Pool, rawData, sourceDescription, &Checked);
        Prefab_Destroy(Checked);
        if (Result.Code != ErrorCode_Success)
        {
            Result = WrapError(Definition->Base.Name, &Result);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        PrefabDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
