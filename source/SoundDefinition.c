#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameSound.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"

/*
 * The sound loader produces a fully-decoded Raylib Sound (not a streamed Music). It requires the Raylib
 * audio device to already be initialized (the game initializes it via the audio engine); this loader does
 * not manage the device's lifetime.
 */


// Types.
typedef struct SoundDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
    unsigned char* Format; // owned; extension hint for reference locations, else NULL
} SoundDefinition;

typedef struct SoundLoadedStruct
{
    GameSound Sound; // Asset points here
} SoundLoaded;


// Static functions.
static void SoundLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    SoundLoaded* Loaded = self->DestroyContext;
    UnloadSound(Loaded->Sound._raySound);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable SoundLoadedVTable = { SoundLoaded_Destroy };

static Error SoundDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    SoundDefinition* Definition = self;

    AssetLocation Location = OwnedAssetLocation_View(&Definition->Location);
    AssetResourcePath* PathHandle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, Definition->Base.Type, &Location, Definition->Format, &PathHandle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }

    Sound RaySound = LoadSound((const char*)AssetResourcePath_Get(PathHandle));
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);

    if (RaySound.frameCount == 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load sound \"%s\".", Definition->Base.Name);
    }

    SoundLoaded* Loaded = Memory_Allocate(sizeof(SoundLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Sound._raySound = RaySound;

    outLoaded->Asset = &Loaded->Sound;
    outLoaded->VTable = &SoundLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void SoundDefinition_Destroy(void* self)
{
    SoundDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Format);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable SoundDefinitionVTable = { SoundDefinition_LoadAsset, SoundDefinition_Destroy };


// Public functions.
Error SoundDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    SoundDefinition* Definition = Memory_Allocate(sizeof(SoundDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &SoundDefinitionVTable;

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
        SoundDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
