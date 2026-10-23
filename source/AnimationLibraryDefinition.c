#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "AnimationLibrary.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The "animation_library" asset type (references/asset_structure.md, "Animation libraries"): the definition's parser,
 * which checks every rule while the definitions are read, and the loader, which reads the clips of the file the
 * definition names (AnimationLibrary_CreateFromFile; the file is hostile, see source/AnimationLibraryGltf.c). The loaded
 * asset is the library's one block; it has no dependencies.
 */


// Macros.
/** What an animation library definition is called in messages. */
#define LIBRARY_KIND ((const unsigned char*)u8"animation library")
/** Room for a message's text, a "where" for unknown keys and a name from the file. */
#define MESSAGE_CAPACITY ((size_t)384)
#define WHERE_CAPACITY ((size_t)256)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))


// Types.
/* One clip setting as the definition owns it (the reader takes AnimationClipSettings, which borrow these strings). */
typedef struct ClipDefinitionStruct
{
    unsigned char* Name;   // owned
    unsigned char* Source; // owned
    bool IsLooped;
    float Speed;
} ClipDefinition;

typedef struct AnimationLibraryDefinitionStruct
{
    AssetDefinition Base;
    OwnedAssetLocation Location;
    unsigned char* Format;  // owned; validated extension, or NULL
    ClipDefinition* Clips;  // owned array
    size_t ClipCount;
} AnimationLibraryDefinition;


// Fields.
static const unsigned char* const RootKeys[] = { u8"name", u8"location", u8"format", u8"clips" };
static const unsigned char* const ClipKeys[] = { u8"name", u8"source", u8"is_looped", u8"speed" };


// Static functions.
static Error Fail(const unsigned char* name, const char* format, ...)
{
    char Text[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(name, Printable, sizeof(Printable));
    return Error_Construct4(ErrorCode_InvalidAssetDefinition, "Animation library \"%s\": %s", Printable, Text);
}

static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}

/* One clip setting: { "name": the game name, "source": its name in the file (default: the name), "is_looped": false,
 * "speed": 1 }. */
static Error ParseClip(const unsigned char* libraryName, JSONCompound* compound, size_t index,
    ClipDefinition* outClip)
{
    char Where[WHERE_CAPACITY];
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(libraryName, Printable, sizeof(Printable));
    snprintf(Where, sizeof(Where), "animation library \"%s\", clips[%" PRIu64 "]", Printable, (uint64_t)index);
    Error Result = AssetJSON_RejectUnknownKeys(compound, ClipKeys, KEY_COUNT(ClipKeys), Where);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    unsigned char* Name = NULL;
    bool HasName = false;
    Result = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"name", &Name, &HasName);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(libraryName, "clips[%" PRIu64 "].name must be a string.", (uint64_t)index);
    }
    outClip->Name = Name;
    if (!HasName || !AssetManager_IsValidAssetName(Name) || (StringUTF8_GetByteLength(Name) > ANIMATION_CLIP_NAME_MAX_LENGTH))
    {
        return Fail(libraryName, "clips[%" PRIu64 "] needs a \"name\" of [a-z][a-z0-9_.-]*, at most %" PRIu64 " bytes: the "
            "name the clip plays under.", (uint64_t)index, (uint64_t)ANIMATION_CLIP_NAME_MAX_LENGTH);
    }

    unsigned char* Source = NULL;
    bool HasSource = false;
    Result = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"source", &Source, &HasSource);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(libraryName, "clips[%" PRIu64 "].source must be a string.", (uint64_t)index);
    }
    if (!HasSource)
    {
        size_t Bytes = StringUTF8_GetByteLength(Name) + 1U;
        Source = Memory_Allocate(Bytes);
        Memory_Copy(Name, Source, Bytes);
    }
    outClip->Source = Source;
    if ((StringUTF8_GetByteLength(Source) == 0U) || (StringUTF8_GetByteLength(Source) > ANIMATION_CLIP_SOURCE_MAX_LENGTH)
        || !StringUTF8_IsEncodingValid(Source))
    {
        return Fail(libraryName, "clips[%" PRIu64 "].source must be the clip's name in the file: UTF-8, 1 to %" PRIu64
            " bytes.", (uint64_t)index, (uint64_t)ANIMATION_CLIP_SOURCE_MAX_LENGTH);
    }

    Result = AssetJSON_ReadOptionalBoolean(compound, (const unsigned char*)u8"is_looped", false, &outClip->IsLooped);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(libraryName, "clips[%" PRIu64 "].is_looped must be true or false.", (uint64_t)index);
    }
    bool HasSpeed = false;
    Result = AssetJSON_ReadOptionalFloat(compound, (const unsigned char*)u8"speed", ANIMATION_CLIP_DEFAULT_SPEED,
        &outClip->Speed, &HasSpeed);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(libraryName, "clips[%" PRIu64 "].speed must be a number.", (uint64_t)index);
    }
    if (!(outClip->Speed > 0.0f) || (outClip->Speed > ANIMATION_CLIP_MAX_SPEED))
    {
        return Fail(libraryName, "clips[%" PRIu64 "].speed is %g; it must be above 0 and at most %g.", (uint64_t)index,
            (double)outClip->Speed, (double)ANIMATION_CLIP_MAX_SPEED);
    }
    return Error_CreateSuccess();
}

/* "clips": the settings of the file's clips, each named once and each source once. */
static Error ParseClips(AnimationLibraryDefinition* definition, JSONCompound* root)
{
    const unsigned char* Name = definition->Base.Name;
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"clips", JSONValueType_Array, &Value,
        &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(Name, "\"clips\" must be an array of { \"name\": ..., \"source\": ..., \"is_looped\": ..., "
            "\"speed\": ... }.");
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    JSONArray* Array = Value.Value.Array;
    size_t Count = JSONArray_GetElementCount(Array);
    if (Count > ANIMATION_LIBRARY_MAX_CLIPS)
    {
        return Fail(Name, "\"clips\" holds %" PRIu64 " entries; a library has at most %" PRIu64 " clips.",
            (uint64_t)Count, (uint64_t)ANIMATION_LIBRARY_MAX_CLIPS);
    }
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }
    definition->Clips = Memory_Allocate(Count * sizeof(ClipDefinition));
    Memory_Zero(definition->Clips, Count * sizeof(ClipDefinition));
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(Name, "clips[%" PRIu64 "] must be a JSON object.", (uint64_t)Index);
        }
        // Counted before it is read, so the definition's teardown frees what a failed read kept.
        definition->ClipCount = Index + 1U;
        Result = ParseClip(Name, Element.Value.Compound, Index, &definition->Clips[Index]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        for (size_t Earlier = 0U; Earlier < Index; Earlier++)
        {
            char Printable[NAME_PRINT_CAPACITY];
            if (AreNamesEqual(definition->Clips[Earlier].Name, definition->Clips[Index].Name))
            {
                AssetJSON_CopyPrintable(definition->Clips[Index].Name, Printable, sizeof(Printable));
                return Fail(Name, "clips[%" PRIu64 "] and clips[%" PRIu64 "] are both called \"%s\".",
                    (uint64_t)Earlier, (uint64_t)Index, Printable);
            }
            if (AreNamesEqual(definition->Clips[Earlier].Source, definition->Clips[Index].Source))
            {
                AssetJSON_CopyPrintable(definition->Clips[Index].Source, Printable, sizeof(Printable));
                return Fail(Name, "clips[%" PRIu64 "] and clips[%" PRIu64 "] both name the file's clip \"%s\".",
                    (uint64_t)Earlier, (uint64_t)Index, Printable);
            }
        }
    }
    return Error_CreateSuccess();
}

static void LibraryLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    AnimationLibrary_Destroy(self->DestroyContext);
}

static const LoadedAssetVTable LibraryLoadedVTable = { LibraryLoaded_Destroy };

static Error AnimationLibraryDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    AnimationLibraryDefinition* Definition = self;
    const unsigned char* FormatExtension = Definition->Format;
    AssetExtensionFilter Extensions = AssetFormat_SelectFilter(&ASSET_EXTENSIONS_ANIMATION_LIBRARY, &FormatExtension);
    AssetLocation Location = OwnedAssetLocation_View(&Definition->Location);
    AssetResourcePath* PathHandle = NULL;
    Error Result = AssetManager_AcquireResourcePath(manager, Definition->Base.Type, &Location, &Extensions,
        Definition->Format, &PathHandle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Definition->Base.Name, Printable, sizeof(Printable));
    char Description[NAME_PRINT_CAPACITY + 32U];
    snprintf(Description, sizeof(Description), "Animation library \"%s\"", Printable);
    AnimationClipSettings* Settings = NULL;
    if (Definition->ClipCount > 0U)
    {
        Settings = Memory_Allocate(Definition->ClipCount * sizeof(AnimationClipSettings));
        for (size_t Index = 0U; Index < Definition->ClipCount; Index++)
        {
            Settings[Index] = (AnimationClipSettings)
            {
                .Source = Definition->Clips[Index].Source, .Name = Definition->Clips[Index].Name,
                .IsLooped = Definition->Clips[Index].IsLooped, .Speed = Definition->Clips[Index].Speed
            };
        }
    }
    AnimationLibraryReadOptions Options =
    {
        .Clips = Settings,
        .ClipCount = Definition->ClipCount,
        .FilePath = NULL,
        .Logger = AssetManager_GetLogger(manager),
        .Description = (const unsigned char*)Description
    };
    AnimationLibrary* Library = NULL;
    Result = AnimationLibrary_CreateFromFile(AssetResourcePath_Get(PathHandle), &Options, &Library);
    Memory_Free(Settings);
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    outLoaded->Asset = Library;
    outLoaded->VTable = &LibraryLoadedVTable;
    outLoaded->DestroyContext = Library;
    return Error_CreateSuccess();
}

static void AnimationLibraryDefinition_Destroy(void* self)
{
    AnimationLibraryDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Format);
    for (size_t Index = 0U; Index < Definition->ClipCount; Index++)
    {
        Memory_Free(Definition->Clips[Index].Name);
        Memory_Free(Definition->Clips[Index].Source);
    }
    Memory_Free(Definition->Clips);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable AnimationLibraryDefinitionVTable =
{
    AnimationLibraryDefinition_LoadAsset, AnimationLibraryDefinition_Destroy
};

static Error ParseDefinition(AnimationLibraryDefinition* definition, JSONCompound* root,
    const unsigned char* sourceDescription)
{
    char Where[WHERE_CAPACITY];
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(definition->Base.Name, Printable, sizeof(Printable));
    snprintf(Where, sizeof(Where), "animation library \"%s\"", Printable);
    Error Result = AssetJSON_RejectUnknownKeys(root, RootKeys, KEY_COUNT(RootKeys), Where);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadLocation(root, (const unsigned char*)u8"location", sourceDescription,
            &definition->Location);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalFormat(root, &ASSET_EXTENSIONS_ANIMATION_LIBRARY, LIBRARY_KIND,
            &definition->Format);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = OwnedAssetLocation_ValidateReferenceFormat(&definition->Location, definition->Format,
            &ASSET_EXTENSIONS_ANIMATION_LIBRARY, LIBRARY_KIND);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseClips(definition, root);
    }
    return Result;
}


// Public functions.
Error AnimationLibraryDefinition_Construct(AssetManager* manager, const UserData* userData,
    const GenericBuffer* rawData, const unsigned char* sourceDescription, AssetDefinition** outDefinition)
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

    AnimationLibraryDefinition* Definition = Memory_Allocate(sizeof(AnimationLibraryDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &AnimationLibraryDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDefinition(Definition, RootCompound, sourceDescription);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        AnimationLibraryDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
