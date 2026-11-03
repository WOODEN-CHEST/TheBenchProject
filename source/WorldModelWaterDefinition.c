#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldModelWaterInternal.h"
#include "AssetDefinitionCommon.h"
#include "AssetTypesStandard.h"
#include "WaterMaterial.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * A world model definition's "water" section (source/WorldModelWaterInternal.h):
 *   "water": [ { "material_slot": 1, "water_material": "tbp:pond" } ]
 * Both keys are required; a material slot is drawn as water once.
 */


// Macros.
/** Room for one message's text, a path in the section ("water[3]"), a "where" for unknown keys and a name from the
 *  file. */
#define MESSAGE_CAPACITY ((size_t)384)
#define PATH_CAPACITY ((size_t)48)
#define WHERE_CAPACITY ((size_t)256)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))


// Types.
typedef struct WaterEntryStruct
{
    int32_t MaterialSlot;
    unsigned char* Material; // owned, the water material's full name
} WaterEntry;

struct WorldModelWaterDefinitionStruct
{
    WaterEntry Waters[WORLD_MODEL_MAX_WATERS];
    size_t WaterCount;
};


// Fields.
static const unsigned char* const WaterKeys[] = { u8"material_slot", u8"water_material" };


// Static functions.
static Error Fail(const char* name, ErrorCode code, const char* format, ...)
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
    return Error_Construct4(code, "World model \"%s\": %s", name, Text);
}

/* "material_slot": a whole number from 0 to INT32_MAX that no earlier water slot draws on. */
static Error ParseMaterialSlot(const char* name, JSONCompound* compound, const char* path,
    WorldModelWaterDefinition* definition, size_t index)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"material_slot", &Value, &Found);
    int64_t Slot = -1;
    if ((Result.Code == ErrorCode_Success) && Found)
    {
        Result = AssetJSON_ReadOptionalInteger(compound, (const unsigned char*)u8"material_slot", -1, &Slot);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        Slot = -1;
    }
    if ((Slot < 0) || (Slot > (int64_t)INT32_MAX))
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s needs a \"material_slot\": the index of the material "
            "whose meshes are drawn as water, a whole number from 0 to %" PRId32 ".", path, INT32_MAX);
    }
    WaterEntry* Entry = &definition->Waters[index];
    Entry->MaterialSlot = (int32_t)Slot;
    for (size_t Earlier = 0U; Earlier < index; Earlier++)
    {
        if (definition->Waters[Earlier].MaterialSlot == Entry->MaterialSlot)
        {
            return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.material_slot %" PRId32 " is water[%" PRIu64 "]'s "
                "too; a material slot is drawn as water once.", path, Entry->MaterialSlot, (uint64_t)Earlier);
        }
    }
    return Error_CreateSuccess();
}

/* "water_material": the water material's asset name, kept in full ("pond" is "tbp:pond"). */
static Error ParseMaterial(const char* name, JSONCompound* compound, const char* path, WaterEntry* entry)
{
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"water_material",
        &entry->Material, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.water_material must be a string.", path);
    }
    if (!Found)
    {
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s needs a \"water_material\": the water material its "
            "meshes are drawn with.", path);
    }
    const char* Problem = AssetJSON_MakeFullAssetName(&entry->Material);
    if (Problem != NULL)
    {
        char Shown[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(entry->Material, Shown, sizeof(Shown));
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s.water_material \"%s\" is not a valid asset name: %s.",
            path, Shown, Problem);
    }
    return Error_CreateSuccess();
}

static Error ParseWater(const char* name, JSONArray* waters, size_t index, WorldModelWaterDefinition* definition)
{
    char Path[PATH_CAPACITY];
    snprintf(Path, sizeof(Path), "water[%" PRIu64 "]", (uint64_t)index);
    JSONObjectValue Element;
    Error Result = JSONArray_GetVerified(waters, index, JSONValueType_Compound, &Element);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(name, ErrorCode_InvalidAssetDefinition, "%s must be a JSON object.", Path);
    }
    JSONCompound* Compound = Element.Value.Compound;
    definition->WaterCount = index + 1U; // what this entry holds is released with the section, even on failure
    char Where[WHERE_CAPACITY];
    snprintf(Where, sizeof(Where), "world model \"%s\", %s", name, Path);
    Result = AssetJSON_RejectUnknownKeys(Compound, WaterKeys, KEY_COUNT(WaterKeys), Where);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseMaterialSlot(name, Compound, Path, definition, index);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseMaterial(name, Compound, Path, &definition->Waters[index]);
    }
    return Result;
}

/* Whether LOD 0 has a mesh of a material slot. */
static bool HasMeshOfSlot(const Model* lod0, int32_t slot)
{
    for (int MeshIndex = 0; MeshIndex < lod0->meshCount; MeshIndex++)
    {
        if ((lod0->meshMaterial != NULL) && (lod0->meshMaterial[MeshIndex] == slot))
        {
            return true;
        }
    }
    return false;
}


// Functions (declared in WorldModelWaterInternal.h).
Error WorldModelWaterDefinition_Parse(const char* printableName, JSONCompound* root,
    WorldModelWaterDefinition** outDefinition)
{
    *outDefinition = NULL;
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(root, (const unsigned char*)u8"water", JSONValueType_Array, &Value,
        &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(printableName, ErrorCode_InvalidAssetDefinition, "\"water\" must be an array of water slots.");
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    if (Count > WORLD_MODEL_MAX_WATERS)
    {
        return Fail(printableName, ErrorCode_InvalidAssetDefinition, "\"water\" holds %" PRIu64 " water slots; at most %"
            PRIu64 " are allowed.", (uint64_t)Count, (uint64_t)WORLD_MODEL_MAX_WATERS);
    }
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }
    WorldModelWaterDefinition* Definition = Memory_Allocate(sizeof(WorldModelWaterDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        Result = ParseWater(printableName, Value.Value.Array, Index, Definition);
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldModelWaterDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = Definition;
    return Error_CreateSuccess();
}

bool WorldModelWaterDefinition_HasSlot(const WorldModelWaterDefinition* self, int32_t materialSlot)
{
    if (self == NULL)
    {
        return false;
    }
    for (size_t Index = 0U; Index < self->WaterCount; Index++)
    {
        if (self->Waters[Index].MaterialSlot == materialSlot)
        {
            return true;
        }
    }
    return false;
}

uint32_t WorldModelWaterDefinition_GetCount(const WorldModelWaterDefinition* self)
{
    return (self != NULL) ? (uint32_t)self->WaterCount : 0U;
}

int32_t WorldModelWaterDefinition_GetSlot(const WorldModelWaterDefinition* self, uint32_t index)
{
    return ((self != NULL) && (index < self->WaterCount)) ? self->Waters[index].MaterialSlot : -1;
}

void WorldModelWaterDefinition_Destroy(WorldModelWaterDefinition* self)
{
    if (self == NULL)
    {
        return;
    }
    for (size_t Index = 0U; Index < self->WaterCount; Index++)
    {
        Memory_Free(self->Waters[Index].Material);
    }
    Memory_Free(self);
}

Error WorldModelWaterDefinition_Load(const WorldModelWaterDefinition* self, const unsigned char* worldModelName,
    AssetManager* manager, AssetUserID dependencyUser, GameModel* lod0, WorldModel* model, void** outBlock)
{
    *outBlock = NULL;
    model->_waters = NULL;
    model->_waterCount = 0U;
    if ((self == NULL) || (self->WaterCount == 0U))
    {
        return Error_CreateSuccess();
    }
    const char* Name = (const char*)worldModelName;
    Model Lod0 = GameModel_GetRaylibModel(lod0);
    for (size_t Index = 0U; Index < self->WaterCount; Index++)
    {
        if (!HasMeshOfSlot(&Lod0, self->Waters[Index].MaterialSlot))
        {
            return Fail(Name, ErrorCode_InvalidAssetData, "water[%" PRIu64 "]: no mesh of LOD 0 uses material slot %"
                PRId32 ", so nothing would be drawn as water there.", (uint64_t)Index, self->Waters[Index].MaterialSlot);
        }
    }
    AssetTypeID WaterType = ASSET_TYPE_ID_INVALID;
    Error Result = AssetManager_GetAssetTypeByName(manager, ASSET_TYPE_NAME_WATER_MATERIAL, &WaterType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (WaterType == ASSET_TYPE_ID_INVALID)
    {
        return Fail(Name, ErrorCode_InvalidState, "the water material asset type is not registered.");
    }

    // One block: the water slots, then their materials' names.
    size_t NameBytes = 0U;
    for (size_t Index = 0U; Index < self->WaterCount; Index++)
    {
        NameBytes += StringUTF8_GetByteLength(self->Waters[Index].Material) + 1U; // at most 16 names of 128 bytes
    }
    size_t ByteCount = (self->WaterCount * sizeof(WorldModelWater)) + NameBytes;
    unsigned char* Block = Memory_Allocate(ByteCount);
    Memory_Zero(Block, ByteCount);
    WorldModelWater* Waters = (WorldModelWater*)(void*)Block;
    unsigned char* Names = Block + (self->WaterCount * sizeof(WorldModelWater));
    for (size_t Index = 0U; (Index < self->WaterCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const WaterEntry* Entry = &self->Waters[Index];
        size_t Bytes = StringUTF8_GetByteLength(Entry->Material) + 1U;
        Memory_Copy(Entry->Material, Names, Bytes);
        Waters[Index]._materialSlot = Entry->MaterialSlot;
        Waters[Index]._materialName = Names;
        Names += Bytes;
        void* Asset = NULL;
        Result = AssetManager_LoadAssetSingle(manager, WaterType, Entry->Material, dependencyUser, &Asset);
        if (Result.Code != ErrorCode_Success)
        {
            Error Wrapped = Error_Construct4(Result.Code, "World model \"%s\": water[%" PRIu64 "] (slot %" PRId32 "): "
                "water material \"%s\": %s", Name, (uint64_t)Index, Entry->MaterialSlot, (const char*)Entry->Material,
                (Result.Message != NULL) ? (const char*)Result.Message : "it did not load.");
            Error_Deconstruct(&Result);
            Result = Wrapped;
            break;
        }
        Waters[Index]._material = Asset;
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Block); // the manager releases the materials that loaded
        return Result;
    }
    model->_waters = Waters;
    model->_waterCount = (uint32_t)self->WaterCount;
    *outBlock = Block;
    return Error_CreateSuccess();
}
