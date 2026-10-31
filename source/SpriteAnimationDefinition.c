#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "SpriteAnimation.h"
#include "SpriteSheet.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRJSON.h"

/*
 * Loads a 2D sprite animation. Each frame is sourced either from a standalone texture (owned by the
 * animation) or from an image within a sprite sheet asset. Sprite-sheet frames load that sheet as a
 * DEPENDENCY (attributed to the animation's dependency user, so it stays alive for the animation's
 * lifetime and unloads with it). A sprite-sheet frame names its sheet with "sprite_sheet", an asset name kept in
 * full, and its "name" selects the image within that sheet; it has no "location". A texture frame's location must be
 * a file path (nothing could name a reference's image format). Every failure path unloads the standalone
 * frame textures loaded so far, so a failed load leaks nothing.
 */


// Macros.
/** What a texture frame's location points at, for parse-time messages. */
#define ANIMATION_FRAME_TEXTURE_KIND ((const unsigned char*)u8"animation frame texture")
/** Room for a sheet's name quoted in a message (AssetJSON_CopyPrintable shortens longer ones). */
#define SHEET_NAME_PRINT_CAPACITY ((size_t)65)


// Types.
typedef enum AnimationFrameSourceEnum
{
    AnimationFrameSource_Texture,
    AnimationFrameSource_SpriteSheet
} AnimationFrameSource;

typedef struct AnimationFrameDefStruct
{
    AnimationFrameSource Source;
    OwnedAssetLocation Location; // the image file (texture source); empty for a sprite-sheet frame
    unsigned char* SheetName; // owned; the sprite sheet's full asset name (spritesheet source), else NULL
    unsigned char* ImageName; // owned; sprite-sheet image name (spritesheet source), else NULL
    int Filter;               // texture source filtering
} AnimationFrameDef;

typedef struct SpriteAnimationDefinitionStruct
{
    AssetDefinition Base;
    AnimationFrameDef* Frames; // owned array
    size_t FrameCount;
    int64_t FPS;
    int64_t FrameStep;
    bool IsRunning;
    bool IsLooped;
} SpriteAnimationDefinition;

typedef struct SpriteAnimationLoadedStruct
{
    SpriteAnimation Animation;     // Asset points here
    GenericBuffer* Frames;         // borrowed from the manager pool (SpriteAnimationFrame elements)
    Texture2D* StandaloneTextures; // owned standalone frame textures
    size_t StandaloneCount;
} SpriteAnimationLoaded;


// Static functions.
static bool StringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool Equal = false;
    Error Result = StringUTF8_EqualsExact(a, b, &Equal);
    if (Result.Code != ErrorCode_Success) { Error_Deconstruct(&Result); return false; }
    return Equal;
}

/* Unloads the first @p count standalone frame textures of @p textures. */
static void UnloadStandaloneTextures(const Texture2D* textures, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        UnloadTexture(textures[i]);
    }
}

static void SpriteAnimationLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    SpriteAnimationLoaded* Loaded = self->DestroyContext;
    UnloadStandaloneTextures(Loaded->StandaloneTextures, Loaded->StandaloneCount);
    Memory_Free(Loaded->StandaloneTextures);
    Error DeconstructResult = SpriteAnimation_Deconstruct(&Loaded->Animation);
    Error_Deconstruct(&DeconstructResult);
    Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Loaded->Frames);
    Error_Deconstruct(&ReturnResult);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable SpriteAnimationLoadedVTable = { SpriteAnimationLoaded_Destroy };

/* Builds one standalone-texture frame, appending its texture to @p standaloneTextures (and counting it) as
   soon as it is loaded so the caller can unload it on any later failure. */
static Error LoadTextureFrame(SpriteAnimationDefinition* definition, AssetManager* manager, const AnimationFrameDef* frameDef,
    Texture2D* standaloneTextures, size_t* standaloneCount, SpriteAnimationFrame* outFrame)
{
    AssetLocation Location = OwnedAssetLocation_View(&frameDef->Location);
    AssetResourcePath* PathHandle = NULL;
    Error PathResult = AssetManager_AcquireResourcePath(manager, definition->Base.Type, &Location,
        &ASSET_EXTENSIONS_TEXTURE, NULL, &PathHandle);
    if (PathResult.Code != ErrorCode_Success) { return PathResult; }
    Texture2D FrameTexture = LoadTexture((const char*)AssetResourcePath_Get(PathHandle));
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);
    if (FrameTexture.id == 0)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData, u8"Failed to load animation frame texture \"%s\" in \"%s\".",
            frameDef->Location.Value, definition->Base.Name);
    }
    SetTextureFilter(FrameTexture, frameDef->Filter);
    standaloneTextures[*standaloneCount] = FrameTexture;
    (*standaloneCount)++;

    outFrame->_texture = FrameTexture;
    outFrame->_areaInTexture = (Rectangle){ 0.0f, 0.0f, (float)FrameTexture.width, (float)FrameTexture.height };
    outFrame->_isTextureStandalone = true;
    return Error_CreateSuccess();
}

/* Builds one sprite-sheet frame, loading the sheet as a dependency under @p dependencyUser. */
static Error LoadSheetFrame(AssetManager* manager, AssetUserID dependencyUser, AssetTypeID sheetType,
    const AnimationFrameDef* frameDef, SpriteAnimationFrame* outFrame)
{
    if (sheetType == ASSET_TYPE_ID_INVALID)
    {
        return Error_Construct3(ErrorCode_InvalidState, u8"The sprite sheet type is not registered.");
    }
    void* SheetAsset = NULL;
    Error LoadResult = AssetManager_LoadAssetSingle(manager, sheetType, frameDef->SheetName, dependencyUser, &SheetAsset);
    if (LoadResult.Code != ErrorCode_Success) { return LoadResult; }

    SpriteSheet* Sheet = SheetAsset;
    Rectangle Area;
    Error AreaResult = SpriteSheet_GetTextureArea(Sheet, frameDef->ImageName, &Area);
    if (AreaResult.Code != ErrorCode_Success) { return AreaResult; }

    outFrame->_texture = Sheet->_texture; // borrowed; the sheet dependency keeps it alive
    outFrame->_areaInTexture = Area;
    outFrame->_isTextureStandalone = false;
    return Error_CreateSuccess();
}

/* Builds all frames into @p frames, loading standalone textures (tracked in @p standaloneTextures) and
   sprite-sheet dependencies (under @p dependencyUser). On failure it unloads every standalone texture it
   loaded and reports a count of 0, so the caller only frees the array; the manager releases the sheet
   dependencies with the failed load's dependency user. */
static Error LoadFrames(SpriteAnimationDefinition* definition, AssetManager* manager, AssetUserID dependencyUser,
    AssetTypeID sheetType, GenericBuffer* frames, Texture2D* standaloneTextures, size_t* outStandaloneCount)
{
    size_t StandaloneCount = 0;
    Error Result = Error_CreateSuccess();

    for (size_t i = 0; (i < definition->FrameCount) && (Result.Code == ErrorCode_Success); i++)
    {
        AnimationFrameDef* FrameDef = &definition->Frames[i];
        SpriteAnimationFrame Frame;
        Memory_Zero(&Frame, sizeof(Frame));

        if (FrameDef->Source == AnimationFrameSource_Texture)
        {
            Result = LoadTextureFrame(definition, manager, FrameDef, standaloneTextures, &StandaloneCount, &Frame);
        }
        else
        {
            Result = LoadSheetFrame(manager, dependencyUser, sheetType, FrameDef, &Frame);
        }

        if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AddLast(frames, &Frame))
        {
            Result = Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to store animation frame.");
        }
    }

    if (Result.Code != ErrorCode_Success)
    {
        UnloadStandaloneTextures(standaloneTextures, StandaloneCount);
        StandaloneCount = 0;
    }
    *outStandaloneCount = StandaloneCount;
    return Result;
}

static Error SpriteAnimationDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    SpriteAnimationDefinition* Definition = self;

    AssetTypeID SheetType = ASSET_TYPE_ID_INVALID;
    Error TypeResult = AssetManager_GetAssetTypeByName(manager, ASSET_TYPE_NAME_SPRITE_SHEET, &SheetType);
    if (TypeResult.Code != ErrorCode_Success) { return TypeResult; }

    GenericBuffer* Frames = NULL;
    Error BorrowResult = AssetManager_BorrowGenericBuffer(manager, sizeof(SpriteAnimationFrame), &Frames);
    if (BorrowResult.Code != ErrorCode_Success) { return BorrowResult; }

    size_t ArrayByteCount = 0;
    if (!Memory_TryMultiplySize(Definition->FrameCount, sizeof(Texture2D), &ArrayByteCount))
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Frames);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Too many animation frames.");
    }
    Texture2D* StandaloneTextures = Memory_Allocate(ArrayByteCount);
    Memory_Zero(StandaloneTextures, ArrayByteCount);

    size_t StandaloneCount = 0;
    Error FramesResult = LoadFrames(Definition, manager, dependencyUser, SheetType, Frames, StandaloneTextures, &StandaloneCount);
    if (FramesResult.Code != ErrorCode_Success)
    {
        // LoadFrames already unloaded the textures it loaded.
        Memory_Free(StandaloneTextures);
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Frames);
        Error_Deconstruct(&ReturnResult);
        return FramesResult;
    }

    SpriteAnimationLoaded* Loaded = Memory_Allocate(sizeof(SpriteAnimationLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Frames = Frames;
    Loaded->StandaloneTextures = StandaloneTextures;
    Loaded->StandaloneCount = StandaloneCount;

    Error ConstructResult = SpriteAnimation_Construct2(&Loaded->Animation, Frames,
        (double)Definition->FPS, Definition->FrameStep, Definition->IsRunning, Definition->IsLooped);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        UnloadStandaloneTextures(StandaloneTextures, StandaloneCount);
        Memory_Free(StandaloneTextures);
        Error ReturnResult = AssetManager_ReturnGenericBuffer(manager, Frames);
        Error_Deconstruct(&ReturnResult);
        Memory_Free(Loaded);
        return ConstructResult;
    }

    outLoaded->Asset = &Loaded->Animation;
    outLoaded->VTable = &SpriteAnimationLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void SpriteAnimationDefinition_Destroy(void* self)
{
    SpriteAnimationDefinition* Definition = self;
    for (size_t i = 0; i < Definition->FrameCount; i++)
    {
        OwnedAssetLocation_Deconstruct(&Definition->Frames[i].Location);
        Memory_Free(Definition->Frames[i].SheetName);
        Memory_Free(Definition->Frames[i].ImageName);
    }
    Memory_Free(Definition->Frames);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable SpriteAnimationDefinitionVTable = { SpriteAnimationDefinition_LoadAsset, SpriteAnimationDefinition_Destroy };

/* A sprite-sheet frame: "sprite_sheet" (the sheet's asset name, kept in full) and "name" (the image in it). */
static Error ParseSheetFrame(JSONCompound* compound, AnimationFrameDef* outFrame)
{
    JSONObjectValue Location;
    bool HasLocation = false;
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"location", &Location, &HasLocation);
    if (Result.Code != ErrorCode_Success) { return Result; }
    if (HasLocation)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"A sprite-sheet frame names its sheet with \"sprite_sheet\" (an asset name), not \"location\".");
    }

    bool Found = false;
    Result = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"sprite_sheet", &outFrame->SheetName,
        &Found);
    if (Result.Code != ErrorCode_Success) { return Result; }
    if (!Found)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"A sprite-sheet frame requires a \"sprite_sheet\": the name of the sprite sheet asset.");
    }
    const char* Problem = AssetJSON_MakeFullAssetName(&outFrame->SheetName);
    if (Problem != NULL)
    {
        char Printable[SHEET_NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(outFrame->SheetName, Printable, sizeof(Printable));
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"A sprite-sheet frame's sprite_sheet \"%s\" is not a "
            u8"valid asset name: %s.", Printable, Problem);
    }

    Result = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"name", &outFrame->ImageName, &Found);
    if (Result.Code != ErrorCode_Success) { return Result; }
    if (!Found)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"A sprite-sheet frame requires an image \"name\".");
    }
    return Error_CreateSuccess();
}

static Error ParseFrame(JSONCompound* compound, const unsigned char* sourceDescription, AnimationFrameDef* outFrame)
{
    Memory_Zero(outFrame, sizeof(*outFrame));
    outFrame->Filter = TEXTURE_FILTER_BILINEAR;

    unsigned char* SourceText = NULL;
    Error SourceResult = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"source", &SourceText, NULL);
    if (SourceResult.Code != ErrorCode_Success) { return SourceResult; }
    if (SourceText == NULL)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"An animation frame requires a string \"source\".");
    }

    Error Result = Error_CreateSuccess();
    if (StringsEqual(SourceText, (const unsigned char*)u8"texture"))
    {
        outFrame->Source = AnimationFrameSource_Texture;
    }
    else if (StringsEqual(SourceText, (const unsigned char*)u8"spritesheet"))
    {
        outFrame->Source = AnimationFrameSource_SpriteSheet;
    }
    else
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Frame source must be \"texture\" or \"spritesheet\".");
    }
    Memory_Free(SourceText);
    if (Result.Code != ErrorCode_Success) { return Result; }

    if (outFrame->Source == AnimationFrameSource_SpriteSheet)
    {
        return ParseSheetFrame(compound, outFrame);
    }

    Result = AssetJSON_ReadLocation(compound, (const unsigned char*)u8"location", sourceDescription, &outFrame->Location);
    if (Result.Code != ErrorCode_Success) { return Result; }
    Result = OwnedAssetLocation_RequireFile(&outFrame->Location, ANIMATION_FRAME_TEXTURE_KIND);
    if (Result.Code != ErrorCode_Success) { return Result; }

    TextureProperties Properties;
    Result = AssetJSON_ReadTextureProperties(compound, (const unsigned char*)u8"texture_properties", false, &Properties);
    if (Result.Code != ErrorCode_Success) { return Result; }
    outFrame->Filter = Properties.Filter;
    return Error_CreateSuccess();
}

static Error ParseFrames(SpriteAnimationDefinition* definition, JSONCompound* root, const unsigned char* sourceDescription)
{
    JSONObjectValue FramesValue;
    Error GetResult = JSONCompound_GetVerified(root, (const unsigned char*)u8"frames", JSONValueType_Array, &FramesValue);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"A sprite animation requires a \"frames\" array.");
    }

    JSONArray* Array = FramesValue.Value.Array;
    size_t Count = JSONArray_GetElementCount(Array);
    if (Count == 0U) { return Error_CreateSuccess(); }

    size_t ByteCount = 0;
    if (!Memory_TryMultiplySize(Count, sizeof(AnimationFrameDef), &ByteCount))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Too many animation frames.");
    }
    definition->Frames = Memory_Allocate(ByteCount);
    Memory_Zero(definition->Frames, ByteCount);
    definition->FrameCount = Count;

    for (size_t i = 0; i < Count; i++)
    {
        JSONObjectValue Element;
        Error ElementResult = JSONArray_GetVerified(Array, i, JSONValueType_Compound, &Element);
        if (ElementResult.Code != ErrorCode_Success) { return ElementResult; }
        Error FrameResult = ParseFrame(Element.Value.Compound, sourceDescription, &definition->Frames[i]);
        if (FrameResult.Code != ErrorCode_Success) { return FrameResult; }
    }
    return Error_CreateSuccess();
}


// Public functions.
Error SpriteAnimationDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    SpriteAnimationDefinition* Definition = Memory_Allocate(sizeof(SpriteAnimationDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &SpriteAnimationDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseFrames(Definition, RootCompound, sourceDescription);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalInteger(RootCompound, (const unsigned char*)u8"fps", 0, &Definition->FPS);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalInteger(RootCompound, (const unsigned char*)u8"frame_step", 0, &Definition->FrameStep);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalBoolean(RootCompound, (const unsigned char*)u8"is_running", false, &Definition->IsRunning);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalBoolean(RootCompound, (const unsigned char*)u8"is_looped", false, &Definition->IsLooped);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        SpriteAnimationDefinition_Destroy(Definition);
        return Result;
    }

    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
