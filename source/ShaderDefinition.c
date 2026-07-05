#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameShader.h"
#include "wr/WRMemory.h"
#include "wr/WRIO.h"
#include "wr/WRJSON.h"

/*
 * The shader definition loads an optional vertex stage and an optional fragment stage, either of which may
 * be omitted to fall back to Raylib's built-in stage. The definition keys are "vertex_location" and
 * "fragment_location"; a bare "location" is accepted as a back-compat alias for the fragment stage (the
 * original single-stage behavior). At least one stage must be specified. Because two stages sharing a
 * definition would otherwise collide, the two source files must have DISTINCT stems (the resource resolver
 * matches by stem, ignoring extension). Shader source is loaded as text, so this loader uses the streaming
 * resource API rather than a temp file.
 */


// Types.
typedef struct ShaderDefinitionStruct
{
    AssetDefinition Base;
    bool HasVertex;
    OwnedAssetLocation VertexLocation;
    bool HasFragment;
    OwnedAssetLocation FragmentLocation;
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

/* Reads an optional location-valued key. When present, parses it into @p outLocation and sets *outHas to
 * true; when absent, leaves *outHas false and the location untouched. */
static Error ReadOptionalLocation(JSONCompound* compound, const unsigned char* key,
    const unsigned char* sourceDescription, bool* outHas, OwnedAssetLocation* outLocation)
{
    *outHas = false;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    if (!Found) { return Error_CreateSuccess(); }

    Error ParseResult = AssetJSON_ParseLocation(&Value, sourceDescription, outLocation);
    if (ParseResult.Code != ErrorCode_Success) { return ParseResult; }
    *outHas = true;
    return Error_CreateSuccess();
}

/* Opens a shader stage's resource, reads its source text into a freshly borrowed manager buffer and
 * NUL-terminates it. On success *outBuffer is borrowed from @p manager and the caller MUST return it with
 * AssetManager_ReturnGenericBuffer; on failure *outBuffer is NULL (nothing to return). */
static Error ReadStageSource(AssetManager* manager, AssetTypeID type, const OwnedAssetLocation* location,
    GenericBuffer** outBuffer)
{
    *outBuffer = NULL;

    AssetLocation View = OwnedAssetLocation_View(location);
    IOStream* Stream = NULL;
    Error OpenResult = AssetManager_OpenResource(manager, type, &View, &Stream);
    if (OpenResult.Code != ErrorCode_Success) { return OpenResult; }

    GenericBuffer* Buffer = NULL;
    Error BorrowResult = AssetManager_BorrowGenericBuffer(manager, 1U, &Buffer);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        Error CloseResult = AssetManager_CloseResource(manager, Stream);
        Error_Deconstruct(&CloseResult);
        return BorrowResult;
    }

    Error ReadResult = IOStream_ReadAll(Stream, Buffer);
    Error CloseResult = AssetManager_CloseResource(manager, Stream);
    Error_Deconstruct(&CloseResult);
    if (ReadResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Buffer);
        Error_Deconstruct(&ReturnResult);
        return ReadResult;
    }

    if (!GenericBuffer_NullTerminate(Buffer))
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Buffer);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to terminate shader source.");
    }

    *outBuffer = Buffer;
    return Error_CreateSuccess();
}

static Error ShaderDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    ShaderDefinition* Definition = self;

    // Both stage sources must stay resident until LoadShaderFromMemory has consumed them, so read them into
    // separate borrowed buffers and only return the buffers once the shader is compiled.
    GenericBuffer* VertexBuffer = NULL;
    GenericBuffer* FragmentBuffer = NULL;

    if (Definition->HasVertex)
    {
        Error VertexResult = ReadStageSource(manager, Definition->Base.Type, &Definition->VertexLocation, &VertexBuffer);
        if (VertexResult.Code != ErrorCode_Success) { return VertexResult; }
    }

    if (Definition->HasFragment)
    {
        Error FragmentResult = ReadStageSource(manager, Definition->Base.Type, &Definition->FragmentLocation, &FragmentBuffer);
        if (FragmentResult.Code != ErrorCode_Success)
        {
            if (VertexBuffer != NULL)
            {
                Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, VertexBuffer);
                Error_Deconstruct(&ReturnResult);
            }
            return FragmentResult;
        }
    }

    const char* VertexCode = (VertexBuffer != NULL) ? (const char*)VertexBuffer->_data : NULL;
    const char* FragmentCode = (FragmentBuffer != NULL) ? (const char*)FragmentBuffer->_data : NULL;
    Shader RayShader = LoadShaderFromMemory(VertexCode, FragmentCode);

    if (VertexBuffer != NULL)
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, VertexBuffer);
        Error_Deconstruct(&ReturnResult);
    }
    if (FragmentBuffer != NULL)
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, FragmentBuffer);
        Error_Deconstruct(&ReturnResult);
    }

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
    OwnedAssetLocation_Deconstruct(&Definition->VertexLocation);
    OwnedAssetLocation_Deconstruct(&Definition->FragmentLocation);
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
        Result = ReadOptionalLocation(RootCompound, (const unsigned char*)u8"vertex_location", sourceDescription,
            &Definition->HasVertex, &Definition->VertexLocation);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadOptionalLocation(RootCompound, (const unsigned char*)u8"fragment_location", sourceDescription,
            &Definition->HasFragment, &Definition->FragmentLocation);
    }
    if ((Result.Code == ErrorCode_Success) && !Definition->HasFragment)
    {
        // Back-compat: a bare "location" is the fragment shader (the original single-stage behavior).
        Result = ReadOptionalLocation(RootCompound, (const unsigned char*)u8"location", sourceDescription,
            &Definition->HasFragment, &Definition->FragmentLocation);
    }
    if ((Result.Code == ErrorCode_Success) && !Definition->HasVertex && !Definition->HasFragment)
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Shader definition must specify \"vertex_location\" and/or \"fragment_location\" (or \"location\").");
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
