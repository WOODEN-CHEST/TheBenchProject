#include <stdarg.h>
#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "WorldSprite.h"
#include "wr/WRMemory.h"
#include "wr/WRJSON.h"

/*
 * The "world_sprite" asset type (references/asset_structure.md, "World sprites"): the definition's parser, which checks
 * every rule while the definitions are read (a bad file is skipped at startup, not at load), and the loader.
 *
 * THE SETTINGS are read through the world sprite's property tables (WorldSprite.c), whose rows the sprite component's
 * table shares (WorldSpriteInternal.h): every key, default and rule exists once. Unknown keys are refused in the root
 * and in "render", naming the keys allowed there.
 *
 * DEPENDENCY. A world sprite is built on its sprite animation, as a world model is built on its models: the animation is
 * loaded under the world sprite's dependency user, so it stays loaded exactly as long as the world sprite does and
 * unloads right after it (the manager releases that user; this loader never releases it). A failed animation fails the
 * world sprite. The loaded WorldSprite is pure data: its animation pointer is borrowed from that dependency, and nothing
 * in it points into the definition, which may be replaced while the asset is loaded.
 */


// Macros.
/** How many bytes of a name from the file a message shows, room for a message's "where" and for one message's text. */
#define NAME_PRINT_CAPACITY ((size_t)129)
#define WHERE_CAPACITY ((size_t)256)
#define MESSAGE_CAPACITY ((size_t)384)
/** The most keys one object of the definition may hold: its table's and the parser's own. */
#define MAX_KNOWN_KEYS ((size_t)32)
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))


// Types.
typedef struct WorldSpriteDefinitionStruct
{
    AssetDefinition Base;
    unsigned char* SpriteAnimation; // owned asset name
    WorldSpriteSettings Settings;
} WorldSpriteDefinition;

/* The definition being read and its name in printable form, for messages. */
typedef struct ParseContextStruct
{
    WorldSpriteDefinition* Definition;
    char Name[NAME_PRINT_CAPACITY];
} ParseContext;


// Fields.
/* The root's keys that are not settings (those come from the settings table). */
static const unsigned char* const RootOwnKeys[] = { u8"name", u8"sprite_animation", u8"render" };


// Static functions: messages.
/* An InvalidAssetDefinition error: "World sprite \"<name>\": " and the formatted text. */
static Error Fail(const ParseContext* context, const char* format, ...)
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
    return Error_Construct4(ErrorCode_InvalidAssetDefinition, "World sprite \"%s\": %s", context->Name, Text);
}

/* The inner error with "World sprite \"<name>\": " before its message (its code kept); releases it. */
static Error WrapError(const ParseContext* context, Error* inner)
{
    Error Wrapped = Error_Construct4(inner->Code, "World sprite \"%s\": %s", context->Name,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}


// Static functions: reading.
/* Refuses a key of @p compound that is neither one of @p ownKeys nor a key of @p table; @p whereSuffix follows
 * "world sprite \"<name>\"" in the message. */
static Error RejectUnknownKeys(const ParseContext* context, JSONCompound* compound, const PropertyTable* table,
    const unsigned char* const* ownKeys, size_t ownKeyCount, const char* whereSuffix)
{
    const unsigned char* Keys[MAX_KNOWN_KEYS];
    size_t KeyCount = ownKeyCount + table->DescriptorCount;
    if (KeyCount > MAX_KNOWN_KEYS)
    {
        return Error_Construct4(ErrorCode_InvalidState, "World sprite \"%s\": %s has more keys than the parser holds.",
            context->Name, (const char*)table->Name);
    }
    for (size_t Index = 0U; Index < ownKeyCount; Index++)
    {
        Keys[Index] = ownKeys[Index];
    }
    for (size_t Index = 0U; Index < table->DescriptorCount; Index++)
    {
        Keys[ownKeyCount + Index] = table->Descriptors[Index].Key;
    }
    char Where[WHERE_CAPACITY];
    snprintf(Where, sizeof(Where), "world sprite \"%s\"%s", context->Name, whereSuffix);
    return AssetJSON_RejectUnknownKeys(compound, Keys, KeyCount, Where);
}

/* "sprite_animation": the required name of the sprite animation to play. */
static Error ReadSpriteAnimation(const ParseContext* context, JSONCompound* root)
{
    unsigned char** Name = &context->Definition->SpriteAnimation;
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(root, (const unsigned char*)u8"sprite_animation", Name, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "sprite_animation must be a string: the name of a sprite animation asset.");
    }
    if (!Found)
    {
        return Fail(context, "the definition needs a \"sprite_animation\": the name of the sprite animation to play.");
    }
    if (!AssetManager_IsValidAssetName(*Name))
    {
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(*Name, Printable, sizeof(Printable));
        return Fail(context, "sprite_animation \"%s\" is not a valid asset name ([a-z][a-z0-9_.-]*).", Printable);
    }
    return Error_CreateSuccess();
}

/* Whether the root holds @p key. */
static Error HasKey(const ParseContext* context, JSONCompound* root, const unsigned char* key, bool* outFound)
{
    JSONObjectValue Value;
    Error Result = JSONCompound_GetOptional(root, key, &Value, outFound);
    return (Result.Code == ErrorCode_Success) ? Result : WrapError(context, &Result);
}

/* The settings at the root, through the settings table (a key left out keeps its default; the root's own keys were
 * checked already). A draw_distance given without has_draw_distance is the world sprite's own (the hand-written form);
 * an explicit has_draw_distance wins, so a value kept while the flag is off reads back as kept. */
static Error ReadSettings(const ParseContext* context, JSONCompound* root)
{
    WorldSpriteSettings* Settings = &context->Definition->Settings;
    PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
    Options.UnknownKeyMode = PropertyUnknownKeyMode_Ignore;
    Error Result = Properties_ReadJSON(WorldSprite_GetSettingsPropertyTable(), root, Settings, &Options);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(context, &Result);
    }

    bool HasFlag = false;
    bool HasValue = false;
    Result = HasKey(context, root, (const unsigned char*)u8"has_draw_distance", &HasFlag);
    if (Result.Code == ErrorCode_Success)
    {
        Result = HasKey(context, root, (const unsigned char*)u8"draw_distance", &HasValue);
    }
    if ((Result.Code == ErrorCode_Success) && !HasFlag && HasValue)
    {
        Settings->HasDrawDistance = true;
    }
    return Result;
}

/* "render": the render flags, through the render table (a key left out keeps its default). */
static Error ReadRender(const ParseContext* context, JSONCompound* root)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"render", JSONValueType_Compound,
        &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "render must be a JSON object.");
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }

    const PropertyTable* Table = WorldSprite_GetRenderPropertyTable();
    JSONCompound* Render = Value.Value.Compound;
    Result = RejectUnknownKeys(context, Render, Table, NULL, 0U, ", render");
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_ReadJSON(Table, Render, &context->Definition->Settings.Render, NULL);
        if (Result.Code != ErrorCode_Success)
        {
            Result = WrapError(context, &Result);
        }
    }
    return Result;
}

/* Reads everything after the name, in the reference's order. */
static Error ParseDefinition(WorldSpriteDefinition* definition, JSONCompound* root)
{
    ParseContext Context;
    Context.Definition = definition;
    AssetJSON_CopyPrintable(definition->Base.Name, Context.Name, sizeof(Context.Name));
    definition->Settings = WorldSpriteSettings_CreateDefault();

    Error Result = RejectUnknownKeys(&Context, root, WorldSprite_GetSettingsPropertyTable(), RootOwnKeys,
        KEY_COUNT(RootOwnKeys), "");
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadSpriteAnimation(&Context, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadSettings(&Context, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRender(&Context, root);
    }
    return Result;
}


// Static functions: loading.
static void WorldSpriteLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    Memory_Free(self->DestroyContext);
}

static const LoadedAssetVTable WorldSpriteLoadedVTable = { WorldSpriteLoaded_Destroy };

static Error WorldSpriteDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    WorldSpriteDefinition* Definition = self;

    // A failure returns before anything is allocated: the manager releases the dependency user's holds.
    SpriteAnimation* Animation = NULL;
    Error Result = AssetManager_LoadSpriteAnimation(manager, Definition->SpriteAnimation, dependencyUser, &Animation);
    if (Result.Code != ErrorCode_Success)
    {
        Error Wrapped = Error_Construct3(Result.Code, u8"World sprite \"%s\": sprite animation \"%s\": %s",
            Definition->Base.Name, Definition->SpriteAnimation,
            (Result.Message != NULL) ? Result.Message : (const unsigned char*)u8"it did not load.");
        Error_Deconstruct(&Result);
        return Wrapped;
    }

    WorldSprite* Sprite = Memory_Allocate(sizeof(WorldSprite));
    Sprite->_animation = Animation;
    Sprite->_settings = Definition->Settings;
    outLoaded->Asset = Sprite;
    outLoaded->VTable = &WorldSpriteLoadedVTable;
    outLoaded->DestroyContext = Sprite;
    return Error_CreateSuccess();
}

static void WorldSpriteDefinition_Destroy(void* self)
{
    WorldSpriteDefinition* Definition = self;
    Memory_Free(Definition->SpriteAnimation);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable WorldSpriteDefinitionVTable =
{
    WorldSpriteDefinition_LoadAsset, WorldSpriteDefinition_Destroy
};


// Public functions.
Error WorldSpriteDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    WorldSpriteDefinition* Definition = Memory_Allocate(sizeof(WorldSpriteDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &WorldSpriteDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDefinition(Definition, RootCompound);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        WorldSpriteDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
