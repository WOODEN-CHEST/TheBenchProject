#include <inttypes.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameShader.h"
#include "ShaderSource.h"
// rlgl provides rlGetShaderIdDefault, needed to detect raylib's silent default-shader fallback.
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"
#include "wr/WRIO.h"
#include "wr/WRJSON.h"
#include "wr/WRString.h"

/*
 * The shader definition loads an optional vertex stage and an optional fragment stage, either of which may
 * be omitted to fall back to Raylib's built-in stage. The definition keys are "vertex_location" and
 * "fragment_location"; a bare "location" is accepted as a back-compat alias for the fragment stage (when there is
 * no "fragment_location"). At least one stage must be specified. A location names a file without
 * its extension, and both stage extensions (vert, frag, vs, fs, glsl) are accepted for either stage, so the
 * two source files must have DISTINCT stems (e.g. "pbr/vertex" + "pbr/fragment"); two files sharing a stem
 * would make the location ambiguous. Shader source is loaded as text, so this loader uses the streaming
 * resource API rather than a temp file, and a reference location needs no format. Each stage's #include lines
 * are expanded before it is compiled (ShaderSource.h).
 *
 * An optional "defines" array names preprocessor symbols for both stages: ["WORLD_INSTANCED"] makes a twin of a
 * shader from the same files, the shared files testing the symbol with #ifdef. Each name is a GLSL identifier that is not
 * reserved (ShaderSource_IsDefineName), at most once per definition.
 */


// Macros.
/** @brief The most defines one shader definition may name. */
#define SHADER_DEFINITION_MAX_DEFINES ((size_t)16)


// Types.
typedef struct ShaderDefinitionStruct
{
    AssetDefinition Base;
    bool HasVertex;
    OwnedAssetLocation VertexLocation;
    bool HasFragment;
    OwnedAssetLocation FragmentLocation;
    unsigned char** Defines; // DefineCount owned names, NULL when there are none
    size_t DefineCount;
} ShaderDefinition;

typedef struct ShaderLoadedStruct
{
    GameShader Shader; // Asset points here
} ShaderLoaded;

/* What one load borrows from the manager: each stage's expanded source (NULL for a stage the definition omits), and
 * each stage's source-string numbers for the compile-failure message (empty when the stage included nothing). */
typedef struct ShaderStageBuffersStruct
{
    GenericBuffer* VertexText;
    GenericBuffer* FragmentText;
    GenericBuffer* VertexSources;
    GenericBuffer* FragmentSources;
} ShaderStageBuffers;


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

static void ReturnBuffer(AssetManager* manager, GenericBuffer* buffer)
{
    if (buffer == NULL) { return; }
    Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, buffer);
    Error_Deconstruct(&ReturnResult);
}

static void ReturnStageBuffers(AssetManager* manager, ShaderStageBuffers* buffers)
{
    ReturnBuffer(manager, buffers->VertexText);
    ReturnBuffer(manager, buffers->FragmentText);
    ReturnBuffer(manager, buffers->VertexSources);
    ReturnBuffer(manager, buffers->FragmentSources);
    Memory_Zero(buffers, sizeof(*buffers));
}

/* Opens a shader stage's resource, reads its source text, expands its #include lines and NUL-terminates the result.
 * On success *outText is borrowed from @p manager and the caller MUST return it with AssetManager_ReturnGenericBuffer;
 * on failure *outText is NULL (nothing to return). When the stage included files, their source-string numbers are
 * appended to @p sourceList. */
static Error ReadStageSource(AssetManager* manager, const ShaderDefinition* definition,
    const OwnedAssetLocation* location, GenericBuffer* sourceList, GenericBuffer** outText)
{
    AssetTypeID type = definition->Base.Type;
    *outText = NULL;

    AssetLocation View = OwnedAssetLocation_View(location);
    IOStream* Stream = NULL;
    Error OpenResult = AssetManager_OpenResource(manager, type, &View, &ASSET_EXTENSIONS_SHADER_STAGE, &Stream);
    if (OpenResult.Code != ErrorCode_Success) { return OpenResult; }

    GenericBuffer* RawText = NULL;
    Error BorrowResult = AssetManager_BorrowGenericBuffer(manager, 1U, &RawText);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        Error CloseResult = AssetManager_CloseResource(manager, Stream);
        Error_Deconstruct(&CloseResult);
        return BorrowResult;
    }

    Error Result = IOStream_ReadAll(Stream, RawText);
    Error CloseResult = AssetManager_CloseResource(manager, Stream);
    Error_Deconstruct(&CloseResult);

    GenericBuffer* ExpandedText = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetManager_BorrowGenericBuffer(manager, 1U, &ExpandedText);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ShaderSource_ExpandIncludes(manager, type, &View, RawText->_data, RawText->_count,
            (const unsigned char* const*)definition->Defines, definition->DefineCount, ExpandedText, sourceList);
    }
    ReturnBuffer(manager, RawText);
    if (Result.Code != ErrorCode_Success)
    {
        ReturnBuffer(manager, ExpandedText);
        return Result;
    }

    *outText = ExpandedText;
    return Error_CreateSuccess();
}

/* The load error for a program raylib could not compile or link. When a stage included files, the message lists which
 * source-string number in the compiler output is which file. */
static Error CreateCompileError(const ShaderDefinition* definition, const ShaderStageBuffers* buffers)
{
    const unsigned char* VertexSources = (buffers->VertexSources->_count > 0U) ? buffers->VertexSources->_data : NULL;
    const unsigned char* FragmentSources = (buffers->FragmentSources->_count > 0U) ? buffers->FragmentSources->_data : NULL;
    if ((VertexSources == NULL) && (FragmentSources == NULL))
    {
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Failed to compile or link shader \"%s\" (raylib's log has the compiler output).", definition->Base.Name);
    }
    if (FragmentSources == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Failed to compile or link shader \"%s\". raylib's log has the compiler output, where a location such as "
            u8"2(14) or 2:14 means line 14 of file 2; the files of the vertex stage are %s.",
            definition->Base.Name, VertexSources);
    }
    if (VertexSources == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Failed to compile or link shader \"%s\". raylib's log has the compiler output, where a location such as "
            u8"2(14) or 2:14 means line 14 of file 2; the files of the fragment stage are %s.",
            definition->Base.Name, FragmentSources);
    }
    return Error_Construct3(ErrorCode_InvalidAssetData,
        u8"Failed to compile or link shader \"%s\". raylib's log has the compiler output, where a location such as "
        u8"2(14) or 2:14 means line 14 of file 2; the files of the vertex stage are %s, and those of the fragment stage "
        u8"are %s.", definition->Base.Name, VertexSources, FragmentSources);
}

static Error CompileShader(const ShaderDefinition* definition, const ShaderStageBuffers* buffers,
    LoadedAsset* outLoaded)
{
    const char* VertexCode = (buffers->VertexText != NULL) ? (const char*)buffers->VertexText->_data : NULL;
    const char* FragmentCode = (buffers->FragmentText != NULL) ? (const char*)buffers->FragmentText->_data : NULL;
    Shader RayShader = LoadShaderFromMemory(VertexCode, FragmentCode);

    // raylib reports a stage that fails to compile with id 0 (still allocating the location array) and a program
    // that fails to link by silently substituting its default shader. Both are failures here, never a quiet
    // default shader. UnloadShader frees the array for id 0 and leaves the default shader alone.
    if ((RayShader.id == 0) || (RayShader.id == rlGetShaderIdDefault()))
    {
        UnloadShader(RayShader);
        return CreateCompileError(definition, buffers);
    }

    ShaderLoaded* Loaded = Memory_Allocate(sizeof(ShaderLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Shader._rayShader = RayShader;

    outLoaded->Asset = &Loaded->Shader;
    outLoaded->VTable = &ShaderLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static Error ShaderDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    ShaderDefinition* Definition = self;

    // Both stage sources must stay resident until LoadShaderFromMemory has consumed them, so they are read into
    // separate borrowed buffers that are only returned once the shader is compiled.
    ShaderStageBuffers Buffers;
    Memory_Zero(&Buffers, sizeof(Buffers));
    Error Result = AssetManager_BorrowGenericBuffer(manager, 1U, &Buffers.VertexSources);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetManager_BorrowGenericBuffer(manager, 1U, &Buffers.FragmentSources);
    }
    if ((Result.Code == ErrorCode_Success) && Definition->HasVertex)
    {
        Result = ReadStageSource(manager, Definition, &Definition->VertexLocation, Buffers.VertexSources,
            &Buffers.VertexText);
    }
    if ((Result.Code == ErrorCode_Success) && Definition->HasFragment)
    {
        Result = ReadStageSource(manager, Definition, &Definition->FragmentLocation, Buffers.FragmentSources,
            &Buffers.FragmentText);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = CompileShader(Definition, &Buffers, outLoaded);
    }
    ReturnStageBuffers(manager, &Buffers);
    return Result;
}

static void ShaderDefinition_Destroy(void* self)
{
    ShaderDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->VertexLocation);
    OwnedAssetLocation_Deconstruct(&Definition->FragmentLocation);
    for (size_t Index = 0U; Index < Definition->DefineCount; Index++)
    {
        Memory_Free(Definition->Defines[Index]);
    }
    Memory_Free(Definition->Defines);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable ShaderDefinitionVTable = { ShaderDefinition_LoadAsset, ShaderDefinition_Destroy };

/* Reads the optional "defines": an array of at most SHADER_DEFINITION_MAX_DEFINES distinct define names. */
static Error ReadDefines(JSONCompound* root, const unsigned char* sourceDescription, ShaderDefinition* definition)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"defines", JSONValueType_Array, &Value,
        &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Shader definition %s: \"defines\" must be an array of names.", sourceDescription);
    }
    size_t Count = Found ? JSONArray_GetElementCount(Value.Value.Array) : 0U;
    if (Count > SHADER_DEFINITION_MAX_DEFINES)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Shader definition %s: \"defines\" holds %" PRIu64 u8" names; a shader has at most %" PRIu64 u8".",
            sourceDescription, (uint64_t)Count, (uint64_t)SHADER_DEFINITION_MAX_DEFINES);
    }
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }
    definition->Defines = Memory_Allocate(Count * sizeof(unsigned char*));
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Value.Value.Array, Index, JSONValueType_String, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = AssetJSON_ValueToOwnedString(&Element, &definition->Defines[Index]);
        }
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Error_Construct3(ErrorCode_InvalidAssetDefinition,
                u8"Shader definition %s: defines[%" PRIu64 u8"] must be a string.", sourceDescription, (uint64_t)Index);
        }
        // Counted once owned, so the definition's teardown frees what a failed check kept.
        definition->DefineCount = Index + 1U;
        const unsigned char* Name = definition->Defines[Index];
        if (!ShaderSource_IsDefineName(Name))
        {
            return Error_Construct3(ErrorCode_InvalidAssetDefinition,
                u8"Shader definition %s: defines[%" PRIu64 u8"] is not a name GLSL lets a shader define (a letter or '_', "
                u8"then letters, digits or '_', not starting with \"GL_\" and without \"__\").", sourceDescription,
                (uint64_t)Index);
        }
        for (size_t Earlier = 0U; Earlier < Index; Earlier++)
        {
            size_t Length = StringUTF8_GetByteLength(Name);
            if ((StringUTF8_GetByteLength(definition->Defines[Earlier]) == Length)
                && Memory_IsEqual(definition->Defines[Earlier], Name, Length))
            {
                return Error_Construct3(ErrorCode_InvalidAssetDefinition,
                    u8"Shader definition %s: defines[%" PRIu64 u8"] and defines[%" PRIu64 u8"] are both \"%s\".",
                    sourceDescription, (uint64_t)Earlier, (uint64_t)Index, Name);
            }
        }
    }
    return Error_CreateSuccess();
}


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
        // Back-compat: a bare "location" is the fragment shader.
        Result = ReadOptionalLocation(RootCompound, (const unsigned char*)u8"location", sourceDescription,
            &Definition->HasFragment, &Definition->FragmentLocation);
    }
    if ((Result.Code == ErrorCode_Success) && !Definition->HasVertex && !Definition->HasFragment)
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Shader definition must specify \"vertex_location\" and/or \"fragment_location\" (or \"location\").");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadDefines(RootCompound, sourceDescription, Definition);
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
