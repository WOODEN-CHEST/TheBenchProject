#include <stdarg.h>
#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "CharacterAsset.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"

/*
 * The "character" asset type (references/asset_structure.md, "Characters"): the definition's parser, which checks every
 * rule while the definitions are read (a bad file is skipped at startup, not at load), and the loader.
 *
 * THE SETTINGS are read through the character's property table (CharacterAsset.c), whose rows the character component's
 * table shares (CharacterInternal.h): every key, default and rule exists once. Unknown keys are refused, naming the
 * keys allowed. The rules across settings (the heights against the radius) are checked after the table's.
 *
 * A character has no dependencies: the loaded Character is a copy of the definition's settings, and nothing in it
 * points into the definition, which may be replaced while the asset is loaded.
 */


// Macros.
/** How many bytes of a name from the file a message shows, and room for a message's "where". */
#define NAME_PRINT_CAPACITY ((size_t)129)
#define WHERE_CAPACITY ((size_t)256)
/** The most keys the definition may hold: its table's and the parser's own ("name"). */
#define MAX_KNOWN_KEYS ((size_t)48)


// Types.
typedef struct CharacterDefinitionStruct
{
    AssetDefinition Base;
    CharacterSettings Settings;
} CharacterDefinition;


// Fields.
/* The root's keys that are not settings (those come from the settings table). */
static const unsigned char* const RootOwnKeys[] = { u8"name" };


// Static functions.
/* The inner error with "Character \"<name>\": " before its message (its code kept); releases it. */
static Error WrapError(const char* name, Error* inner)
{
    Error Wrapped = Error_Construct4(inner->Code, "Character \"%s\": %s", name,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Refuses a root key that is neither "name" nor a setting's. */
static Error RejectUnknownKeys(const char* name, JSONCompound* root)
{
    const PropertyTable* Table = CharacterSettings_GetPropertyTable();
    const unsigned char* Keys[MAX_KNOWN_KEYS];
    size_t OwnKeyCount = sizeof(RootOwnKeys) / sizeof(RootOwnKeys[0]);
    size_t KeyCount = OwnKeyCount + Table->DescriptorCount;
    if (KeyCount > MAX_KNOWN_KEYS)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Character \"%s\": the table has more keys than the parser holds.",
            name);
    }
    for (size_t Index = 0U; Index < OwnKeyCount; Index++)
    {
        Keys[Index] = RootOwnKeys[Index];
    }
    for (size_t Index = 0U; Index < Table->DescriptorCount; Index++)
    {
        Keys[OwnKeyCount + Index] = Table->Descriptors[Index].Key;
    }
    char Where[WHERE_CAPACITY];
    snprintf(Where, sizeof(Where), "character \"%s\"", name);
    return AssetJSON_RejectUnknownKeys(root, Keys, KeyCount, Where);
}

/* Reads everything after the name: the settings through their table (a key left out keeps its default), then the rules
 * across settings. */
static Error ParseDefinition(CharacterDefinition* definition, JSONCompound* root)
{
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(definition->Base.Name, Name, sizeof(Name));
    definition->Settings = CharacterSettings_CreateDefault();

    Error Result = RejectUnknownKeys(Name, root);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
    Options.UnknownKeyMode = PropertyUnknownKeyMode_Ignore;
    Result = Properties_ReadJSON(CharacterSettings_GetPropertyTable(), root, &definition->Settings, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(Name, &Result);
    }

    Result = CharacterSettings_CheckBody(&definition->Settings);
    if (Result.Code != ErrorCode_Success)
    {
        // A broken rule is a broken definition, like the table's own rules.
        Error Wrapped = Error_Construct4(ErrorCode_InvalidAssetDefinition, "Character \"%s\": %s", Name,
            (Result.Message != NULL) ? (const char*)Result.Message : "the body's heights do not fit its radius.");
        Error_Deconstruct(&Result);
        return Wrapped;
    }
    return Error_CreateSuccess();
}

static void CharacterLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    Memory_Free(self->DestroyContext);
}

static const LoadedAssetVTable CharacterLoadedVTable = { CharacterLoaded_Destroy };

static Error CharacterDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    (void)manager;
    (void)dependencyUser;
    CharacterDefinition* Definition = self;

    CharacterAsset* Loaded = Memory_Allocate(sizeof(CharacterAsset));
    Loaded->_settings = Definition->Settings;
    outLoaded->Asset = Loaded;
    outLoaded->VTable = &CharacterLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void CharacterDefinition_Destroy(void* self)
{
    CharacterDefinition* Definition = self;
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable CharacterDefinitionVTable =
{
    CharacterDefinition_LoadAsset, CharacterDefinition_Destroy
};


// Public functions.
Error CharacterDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    CharacterDefinition* Definition = Memory_Allocate(sizeof(CharacterDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &CharacterDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDefinition(Definition, RootCompound);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        CharacterDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
