#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "PathObject.h"
#include "PathObjectInternal.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The "path_object" asset type (references/asset_structure.md, "Path objects"; PathObject.h): the definition's parser, which
 * reads the settings through their tables (unknown keys refused, every rule checked when the definitions are read), and the
 * loader, which loads the world models the slots name as the asset's DEPENDENCIES (under its dependency user: they stay
 * loaded exactly as long as it does). A world model that does not load fails the asset, naming it.
 *
 * WRITING A SETTING SETS IT. Every setting of a slot or of the whole object is two rows, a flag and a value, as a placement
 * writes them. A definition only says what it wants: a value written without its flag has the flag put in before the tables
 * read it, so a fence's file stays short, and a flag written false still leaves the value out of effect.
 *
 * The loaded asset keeps no pointer into the definition, which may be replaced while it is loaded: its settings are a copy.
 */


// Macros.
/** How many bytes of a name from the file a message shows. */
#define NAME_PRINT_CAPACITY ((size_t)129)
/** The longest key of a table's flag row. */
#define FLAG_KEY_CAPACITY ((size_t)96)
/** The key every definition starts with, which is not a setting. */
#define NAME_KEY ((const unsigned char*)u8"name")


// Types.
typedef struct PathObjectDefinitionStruct
{
    AssetDefinition Base;
    PathObject Data;
} PathObjectDefinition;


// Static functions: the definition.
/* The inner error with "Path object \"<name>\": " before its message, under @p code; releases it. */
static Error WrapError(const char* name, ErrorCode code, Error* inner)
{
    Error Wrapped = Error_Construct4(code, "Path object \"%s\": %s", name,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Puts in the flag of every setting @p compound writes without one. */
static Error AddFlags(const PropertyTable* table, JSONCompound* compound)
{
    size_t PrefixLength = StringUTF8_GetByteLength(PROPERTY_OVERRIDE_FLAG_PREFIX);
    Error Result = Error_CreateSuccess();
    for (size_t Row = 0U; (Row < table->DescriptorCount) && (Result.Code == ErrorCode_Success); Row++)
    {
        const unsigned char* FlagKey = table->Descriptors[Row].Key;
        size_t Length = StringUTF8_GetByteLength(FlagKey);
        if ((Length <= PrefixLength) || !Memory_IsEqual(FlagKey, PROPERTY_OVERRIDE_FLAG_PREFIX, PrefixLength))
        {
            continue;
        }
        JSONObjectValue Unused;
        bool IsValueWritten = false;
        bool IsFlagWritten = false;
        Result = JSONCompound_GetOptional(compound, FlagKey + PrefixLength, &Unused, &IsValueWritten);
        if ((Result.Code == ErrorCode_Success) && IsValueWritten)
        {
            Result = JSONCompound_GetOptional(compound, FlagKey, &Unused, &IsFlagWritten);
        }
        if ((Result.Code == ErrorCode_Success) && IsValueWritten && !IsFlagWritten)
        {
            JSONObjectValue Flag = JSONObjectValue_CreateBoolean(true);
            Result = JSONCompound_Set(compound, FlagKey, &Flag);
        }
    }
    return Result;
}

/* Puts in the flags of the root's settings and of every slot record's. */
static Error AddDefinitionFlags(JSONCompound* root)
{
    Error Result = AddFlags(PathObject_GetPropertyTable(), root);
    JSONObjectValue Slots;
    bool HasSlots = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(root, (const unsigned char*)u8"slots", &Slots, &HasSlots);
    }
    if ((Result.Code != ErrorCode_Success) || !HasSlots || (Slots.Type != JSONValueType_Array))
    {
        return Result; // an array that is no array is the table's to refuse
    }
    size_t Count = JSONArray_GetElementCount(Slots.Value.Array);
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_Get(Slots.Value.Array, Index, &Element);
        if ((Result.Code == ErrorCode_Success) && (Element.Type == JSONValueType_Compound))
        {
            Result = AddFlags(PathObject_GetSlotPropertyTable(), Element.Value.Compound);
        }
    }
    return Result;
}

/* What a slot is called in messages. */
static const char* GetSlotName(PathObjectSlot slot)
{
    const PropertyEnumTable* Table = PathObject_GetSlotEnumTable();
    return ((size_t)slot < Table->EntryCount) ? (const char*)Table->Entries[slot].Name : "?";
}

/* What no table's rule can say: a middle, a model for every slot the flags use, and a record per slot at most. */
static Error CheckSettings(const char* name, const PathObjectSettings* settings)
{
    PathObjectSettings InEffect = PathObject_ResolveSettings(settings, NULL);
    for (uint32_t Slot = 0U; Slot < (uint32_t)PathObjectSlot_Count; Slot++)
    {
        if (PathObjectSettings_IsSlotUsed(&InEffect, (PathObjectSlot)Slot)
            && (PathObject_GetSlotModels((PathObjectSlot)Slot, settings, NULL, NULL, 0U) == 0U))
        {
            return Error_Construct4(ErrorCode_InvalidAssetDefinition, "Path object \"%s\": the slot \"%s\" is in use but "
                "models holds no model of it.", name, GetSlotName((PathObjectSlot)Slot));
        }
    }
    const PathObjectSlotRecord* Records = PropertyRecordList_GetRecords(settings->Slots);
    size_t Count = PropertyRecordList_GetCount(settings->Slots);
    for (size_t Index = 1U; Index < Count; Index++)
    {
        for (size_t Earlier = 0U; Earlier < Index; Earlier++)
        {
            if (Records[Index].Slot == Records[Earlier].Slot)
            {
                return Error_Construct4(ErrorCode_InvalidAssetDefinition, "Path object \"%s\": slots holds the slot \"%s\" "
                    "twice.", name, GetSlotName(Records[Index].Slot));
            }
        }
    }
    const PathObjectModelRecord* Models = PropertyRecordList_GetRecords(settings->Models);
    for (size_t Index = 0U; Index < PropertyRecordList_GetCount(settings->Models); Index++)
    {
        if ((Models[Index].WorldModel == NULL) == (Models[Index].Prefab == NULL))
        {
            return Error_Construct4(ErrorCode_InvalidAssetDefinition,
                "Path object \"%s\": models[%u] names exactly one of a world model and a prefab.", name, (unsigned int)Index);
        }
    }
    return Error_CreateSuccess();
}

/* Reads everything after the name through the tables: a key left out keeps its default, any other key is refused. */
static Error ParseDefinition(PathObjectDefinition* definition, JSONObjectPool* pool, JSONCompound* root)
{
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(definition->Base.Name, Name, sizeof(Name));

    Error Result = AddDefinitionFlags(root);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_Remove(root, NAME_KEY);
    }
    if (Result.Code == ErrorCode_Success)
    {
        PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
        Options.UnknownKeyMode = PropertyUnknownKeyMode_Error;
        Options.Pool = pool;
        Result = Properties_ReadJSON(PathObject_GetPropertyTable(), root, &definition->Data, &Options);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(Name, (Result.Code == ErrorCode_InvalidJSON) ? ErrorCode_InvalidAssetDefinition : Result.Code,
            &Result);
    }
    return CheckSettings(Name, &definition->Data._settings);
}


// Static functions: the asset.
static void DestroyPathObject(PathObject* asset)
{
    if (asset == NULL)
    {
        return;
    }
    Properties_Deconstruct(PathObject_GetPropertyTable(), asset);
    Memory_Free(asset);
}

static void PathObjectLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    DestroyPathObject(self->DestroyContext); // its world models are dependencies: the manager releases them
}

static const LoadedAssetVTable PathObjectLoadedVTable = { PathObjectLoaded_Destroy };

static Error PathObjectDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    PathObjectDefinition* Definition = self;
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Definition->Base.Name, Name, sizeof(Name));
    AssetTypeID WorldModelType = ASSET_TYPE_ID_INVALID;
    Error Result = AssetManager_GetAssetTypeByName(manager, ASSET_TYPE_NAME_WORLD_MODEL, &WorldModelType);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(Name, Result.Code, &Result);
    }
    if (WorldModelType == ASSET_TYPE_ID_INVALID)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Path object \"%s\": the world model asset type is not registered.",
            Name);
    }

    PathObject* Loaded = Memory_Allocate(sizeof(PathObject));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Result = Properties_Construct(PathObject_GetPropertyTable(), Loaded);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Properties_CopyTo(PathObject_GetPropertyTable(), &Definition->Data, Loaded);
    }
    const PathObjectModelRecord* Models = PropertyRecordList_GetRecords(Definition->Data._settings.Models);
    size_t ModelCount = PropertyRecordList_GetCount(Definition->Data._settings.Models);
    for (size_t Index = 0U; (Index < ModelCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        if (Models[Index].WorldModel == NULL)
        {
            continue; // a prefab: it may hold this very path object, so it is loaded when a piece is made, never as a dependency
        }
        void* Asset = NULL;
        Result = AssetManager_LoadAssetSingle(manager, WorldModelType, Models[Index].WorldModel, dependencyUser, &Asset);
        if (Result.Code != ErrorCode_Success)
        {
            char Printable[NAME_PRINT_CAPACITY];
            AssetJSON_CopyPrintable(Models[Index].WorldModel, Printable, sizeof(Printable));
            Error Wrapped = Error_Construct4(Result.Code, "Path object \"%s\": its world model \"%s\": %s", Name, Printable,
                (Result.Message != NULL) ? (const char*)Result.Message : "it did not load.");
            Error_Deconstruct(&Result);
            Result = Wrapped;
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        DestroyPathObject(Loaded); // the manager releases the world models that loaded
        return Result;
    }
    outLoaded->Asset = Loaded;
    outLoaded->VTable = &PathObjectLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void PathObjectDefinition_Destroy(void* self)
{
    PathObjectDefinition* Definition = self;
    Properties_Deconstruct(PathObject_GetPropertyTable(), &Definition->Data);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable PathObjectDefinitionVTable =
{
    PathObjectDefinition_LoadAsset, PathObjectDefinition_Destroy
};


// Public functions.
Error PathObjectDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    PathObjectDefinition* Definition = Memory_Allocate(sizeof(PathObjectDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &PathObjectDefinitionVTable;
    Result = Properties_Construct(PathObject_GetPropertyTable(), &Definition->Data);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDefinition(Definition, Pool, RootCompound);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        PathObjectDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
