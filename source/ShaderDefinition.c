#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameShader.h"
#include "wr/WRMemory.h"
#include "wr/WRIO.h"
#include "wr/WRJSON.h"

/*
 * The shader definition currently loads its location as a FRAGMENT shader; the vertex stage uses Raylib's
 * default. If paired vertex/fragment shaders are needed later, the definition can gain separate
 * vertex/fragment locations. Shader source is loaded as text (no file extension is required), so this
 * loader uses the streaming resource API rather than a temp file.
 */


// Types.
typedef struct ShaderDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
} ShaderDefinition;

typedef struct ShaderLoadedStruct
{
    GameShader Shader; // Asset points here
} ShaderLoaded;


// Static functions.
static void ShaderLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    ShaderLoaded* Loaded = self->DestroyContext;
    UnloadShader(Loaded->Shader._rayShader);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable ShaderLoadedVTable = { ShaderLoaded_Destroy };

static Error ShaderDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    ShaderDefinition* Definition = self;

    AssetLocation Location = OwnedAssetLocation_View(&Definition->Location);
    IOStream* Stream = NULL;
    Error OpenResult = AssetManager_OpenResource(manager, Definition->Base.Type, &Location, &Stream);
    if (OpenResult.Code != ErrorCode_Success) { return OpenResult; }

    GenericBuffer* Code = NULL;
    Error BorrowResult = AssetManager_BorrowGenericBuffer(manager, 1U, &Code);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        Error CloseResult = AssetManager_CloseResource(manager, Stream);
        Error_Deconstruct(&CloseResult);
        return BorrowResult;
    }

    Error ReadResult = IOStream_ReadAll(Stream, Code);
    Error CloseResult = AssetManager_CloseResource(manager, Stream);
    Error_Deconstruct(&CloseResult);
    if (ReadResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Code);
        Error_Deconstruct(&ReturnResult);
        return ReadResult;
    }

    Error Result = Error_CreateSuccess();
    if (!GenericBuffer_NullTerminate(Code))
    {
        Result = Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to terminate shader source.");
    }

    Shader RayShader = { 0 };
    if (Result.Code == ErrorCode_Success)
    {
        RayShader = LoadShaderFromMemory(NULL, (const char*)Code->_data);
    }

    Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Code);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success) { return Result; }
    if (RayShader.id == 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to compile shader \"%s\".", Definition->Base.Name);
    }

    ShaderLoaded* Loaded = Memory_Allocate(sizeof(ShaderLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Shader._rayShader = RayShader;

    outLoaded->Asset = &Loaded->Shader;
    outLoaded->VTable = &ShaderLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void ShaderDefinition_Destroy(void* self)
{
    ShaderDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable ShaderDefinitionVTable = { ShaderDefinition_LoadAsset, ShaderDefinition_Destroy };


// Public functions.
Error ShaderDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    ShaderDefinition* Definition = Memory_Allocate(sizeof(ShaderDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &ShaderDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadLocation(RootCompound, (const unsigned char*)u8"location", sourceDescription, &Definition->Location);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        ShaderDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
