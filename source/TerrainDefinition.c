#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "CollisionLayer.h"
#include "TerrainAsset.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The "terrain" asset type (references/asset_structure.md, "Terrains"; TerrainAsset.h): the definition's parser, which
 * checks every rule while the definitions are read (unknown keys refused everywhere, the palette's and the channels'
 * names, the attribute mappings against them, the chunk size, the render and collision blocks), and the loader, which
 * imports the .glb (source "model": TerrainMesh_CreateFromGltfFile, the file treated as hostile) or reads the binary form
 * (source "data": TerrainMesh_LoadGHDF) and checks it against the definition. The loaded asset owns its mesh and the
 * names; it has no dependencies (B02 makes the palette's materials its dependencies).
 */


// Macros.
/** What a terrain definition is called in messages. */
#define TERRAIN_KIND ((const unsigned char*)u8"terrain")
/** Room for a message's text, a "where" for unknown keys and a name in printable form. */
#define MESSAGE_CAPACITY ((size_t)384)
#define WHERE_CAPACITY ((size_t)256)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))
/** The longest attribute name a mapping may give, in bytes. */
#define ATTRIBUTE_NAME_MAX_LENGTH ((size_t)128)
/** The most attribute mappings of each kind (one per palette material or channel at most). */
#define MAX_MAPPINGS TERRAIN_MAX_MATERIALS
/** The binary form's extension, which a reference location's temp file takes. */
#define DATA_EXTENSION ((const unsigned char*)u8"ghdf")


// Types.
/* Where the mesh comes from. */
typedef enum TerrainSourceEnum
{
    TerrainSource_Model,
    TerrainSource_Data
} TerrainSource;

typedef struct TerrainDefinitionStruct
{
    AssetDefinition Base;
    TerrainSource Source;
    /** The .glb (source model) or the binary file (source data). */
    OwnedAssetLocation Location;
    /** Source model: the validated extension, or NULL. */
    unsigned char* Format;
    /** Source model: the attribute mappings, their names in Names (owned arrays). */
    TerrainAttributeMapping Weights[MAX_MAPPINGS];
    size_t WeightCount;
    TerrainAttributeMapping Densities[MAX_MAPPINGS];
    size_t DensityCount;
    TerrainNormalMode NormalMode;
    /** Source model: the blend slot's name (owned; NULL: TERRAIN_DEFAULT_BLEND_SLOT). */
    unsigned char* BlendSlot;
    /** The palette's and the channels' names, and the mappings' attribute names: owned copies each. */
    unsigned char* MaterialNames[TERRAIN_MAX_MATERIALS];
    uint32_t MaterialCount;
    unsigned char* ChannelNames[TERRAIN_MAX_DENSITY_CHANNELS];
    uint32_t ChannelCount;
    unsigned char* AttributeNames[2U * MAX_MAPPINGS];
    size_t AttributeNameCount;
    float ChunkSize;
    TerrainSettings Settings;
} TerrainDefinition;


// Fields.
static const unsigned char* const RootKeys[] =
{
    u8"name", u8"source", u8"model", u8"format", u8"data", u8"weights", u8"densities", u8"normals", u8"blend_slot",
    u8"materials", u8"density_channels", u8"chunk_size", u8"render", u8"collision"
};
static const unsigned char* const ModelOnlyKeys[] =
{
    u8"model", u8"format", u8"weights", u8"densities", u8"normals", u8"blend_slot"
};
static const unsigned char* const MappingKeys[] = { u8"attribute", u8"r", u8"g", u8"b", u8"a" };
static const unsigned char* const RenderKeys[] =
{
    u8"has_outline", u8"shadow_tier", u8"casts_shadows", u8"draw_distance", u8"is_draw_distance_fixed"
};
static const unsigned char* const CollisionKeys[] = { u8"is_enabled", u8"layer", u8"collides_with" };
static const unsigned char* const ChannelKeys[4] = { u8"r", u8"g", u8"b", u8"a" };


// Static functions: helpers.
static Error Fail(const TerrainDefinition* definition, const char* format, ...)
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
    AssetJSON_CopyPrintable(definition->Base.Name, Printable, sizeof(Printable));
    return Error_Construct4(ErrorCode_InvalidAssetDefinition, "Terrain \"%s\": %s", Printable, Text);
}

static const char* FormatWhere(const TerrainDefinition* definition, char* buffer, size_t capacity, const char* suffix)
{
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(definition->Base.Name, Printable, sizeof(Printable));
    snprintf(buffer, capacity, "terrain \"%s\"%s", Printable, suffix);
    return buffer;
}

static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}

/* [a-z][a-z0-9_]*: a density channel's name. */
static bool IsSnakeCase(const unsigned char* name)
{
    if ((name == NULL) || (name[0] < (unsigned char)'a') || (name[0] > (unsigned char)'z'))
    {
        return false;
    }
    for (size_t Index = 1U; name[Index] != 0U; Index++)
    {
        unsigned char Byte = name[Index];
        bool IsLetter = (Byte >= (unsigned char)'a') && (Byte <= (unsigned char)'z');
        bool IsDigit = (Byte >= (unsigned char)'0') && (Byte <= (unsigned char)'9');
        if (!IsLetter && !IsDigit && (Byte != (unsigned char)'_'))
        {
            return false;
        }
    }
    return true;
}

/* An optional compound under @p key (NULL when absent). */
static Error GetOptionalCompound(const TerrainDefinition* definition, JSONCompound* parent, const unsigned char* key,
    JSONCompound** outCompound)
{
    *outCompound = NULL;
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptionalVerified(parent, key, JSONValueType_Compound, &Value, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "\"%s\" must be a JSON object.", (const char*)key);
    }
    *outCompound = IsFound ? Value.Value.Compound : NULL;
    return Error_CreateSuccess();
}

/* An optional array under @p key (NULL when absent). */
static Error GetOptionalArray(const TerrainDefinition* definition, JSONCompound* parent, const unsigned char* key,
    JSONArray** outArray)
{
    *outArray = NULL;
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptionalVerified(parent, key, JSONValueType_Array, &Value, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "\"%s\" must be an array.", (const char*)key);
    }
    *outArray = IsFound ? Value.Value.Array : NULL;
    return Error_CreateSuccess();
}

/* The strings of an array, each an owned copy into @p outNames (at most @p maximum, each at most
 * TERRAIN_NAME_MAX_LENGTH bytes, unique, valid by @p isValid). */
static Error ReadNameList(TerrainDefinition* definition, JSONArray* array, const char* key, size_t maximum,
    bool (*isValid)(const unsigned char* name), const char* rule, unsigned char** outNames, uint32_t* outCount)
{
    size_t Count = JSONArray_GetElementCount(array);
    if (Count > maximum)
    {
        return Fail(definition, "\"%s\" holds %" PRIu64 " names; at most %" PRIu64 " are allowed.", key, (uint64_t)Count,
            (uint64_t)maximum);
    }
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Error Result = JSONArray_GetVerified(array, Index, JSONValueType_String, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(definition, "%s[%" PRIu64 "] must be a string.", key, (uint64_t)Index);
        }
        Result = AssetJSON_ValueToOwnedString(&Element, &outNames[Index]);
        // Counted before it is checked, so the definition's teardown frees it.
        *outCount = (uint32_t)(Index + 1U);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(outNames[Index], Printable, sizeof(Printable));
        if (!isValid(outNames[Index]) || (StringUTF8_GetByteLength(outNames[Index]) > TERRAIN_NAME_MAX_LENGTH))
        {
            return Fail(definition, "%s[%" PRIu64 "] is \"%s\"; names are %s, at most %" PRIu64 " bytes.", key,
                (uint64_t)Index, Printable, rule, (uint64_t)TERRAIN_NAME_MAX_LENGTH);
        }
        for (size_t Earlier = 0U; Earlier < Index; Earlier++)
        {
            if (AreNamesEqual(outNames[Earlier], outNames[Index]))
            {
                return Fail(definition, "%s[%" PRIu64 "] and %s[%" PRIu64 "] are both \"%s\".", key, (uint64_t)Earlier,
                    key, (uint64_t)Index, Printable);
            }
        }
    }
    return Error_CreateSuccess();
}


// Static functions: the parts.
/* "source": "model" or "data" (required: which key carries the mesh is said, not guessed). */
static Error ParseSource(TerrainDefinition* definition, JSONCompound* root)
{
    unsigned char* Text = NULL;
    bool IsFound = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(root, (const unsigned char*)u8"source", &Text, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        IsFound = false;
    }
    bool IsModel = IsFound && AreNamesEqual(Text, (const unsigned char*)u8"model");
    bool IsData = IsFound && AreNamesEqual(Text, (const unsigned char*)u8"data");
    Memory_Free(Text);
    if (!IsModel && !IsData)
    {
        return Fail(definition, "\"source\" must be \"model\" (a .glb imported when it loads) or \"data\" (its binary "
            "form).");
    }
    definition->Source = IsModel ? TerrainSource_Model : TerrainSource_Data;
    if (IsData)
    {
        for (size_t Index = 0U; Index < KEY_COUNT(ModelOnlyKeys); Index++)
        {
            JSONObjectValue Value;
            bool IsThere = false;
            Error Lookup = JSONCompound_GetOptional(root, ModelOnlyKeys[Index], &Value, &IsThere);
            Error_Deconstruct(&Lookup);
            if (IsThere)
            {
                return Fail(definition, "\"%s\" is read only with \"source\": \"model\".",
                    (const char*)ModelOnlyKeys[Index]);
            }
        }
    }
    else
    {
        JSONObjectValue Value;
        bool IsThere = false;
        Error Lookup = JSONCompound_GetOptional(root, (const unsigned char*)u8"data", &Value, &IsThere);
        Error_Deconstruct(&Lookup);
        if (IsThere)
        {
            return Fail(definition, "\"data\" is read only with \"source\": \"data\".");
        }
    }
    return Error_CreateSuccess();
}

/* "materials" (required, 1 to 16 asset names) and "density_channels" (up to 8 snake_case names). */
static Error ParseNames(TerrainDefinition* definition, JSONCompound* root)
{
    JSONArray* Materials = NULL;
    Error Result = GetOptionalArray(definition, root, (const unsigned char*)u8"materials", &Materials);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((Materials == NULL) || (JSONArray_GetElementCount(Materials) == 0U))
    {
        return Fail(definition, "\"materials\" must name 1 to %u terrain materials: the palette.", TERRAIN_MAX_MATERIALS);
    }
    Result = ReadNameList(definition, Materials, "materials", TERRAIN_MAX_MATERIALS, AssetManager_IsValidAssetName,
        "asset names ([a-z][a-z0-9_.-]*)", definition->MaterialNames, &definition->MaterialCount);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    JSONArray* Channels = NULL;
    Result = GetOptionalArray(definition, root, (const unsigned char*)u8"density_channels", &Channels);
    if ((Result.Code != ErrorCode_Success) || (Channels == NULL))
    {
        return Result;
    }
    return ReadNameList(definition, Channels, "density_channels", TERRAIN_MAX_DENSITY_CHANNELS, IsSnakeCase,
        "[a-z][a-z0-9_]*", definition->ChannelNames, &definition->ChannelCount);
}

/* The index of a palette material or a density channel by name, or -1. */
static int32_t FindName(unsigned char* const* names, uint32_t count, const unsigned char* name)
{
    for (uint32_t Index = 0U; Index < count; Index++)
    {
        if (AreNamesEqual(names[Index], name))
        {
            return (int32_t)Index;
        }
    }
    return -1;
}

/* One mapping { "attribute": ..., "r": name, "g": ..., "b": ..., "a": ... }: each channel names a palette material
 * (weights) or a density channel (densities); @p isUsed marks what earlier channels fed, so nothing is fed twice. */
static Error ParseMapping(TerrainDefinition* definition, JSONCompound* compound, const char* key, size_t index,
    bool isWeights, bool* isUsed, TerrainAttributeMapping* outMapping)
{
    char Where[WHERE_CAPACITY];
    char Suffix[64];
    snprintf(Suffix, sizeof(Suffix), ", %s[%" PRIu64 "]", key, (uint64_t)index);
    Error Result = AssetJSON_RejectUnknownKeys(compound, MappingKeys, KEY_COUNT(MappingKeys),
        FormatWhere(definition, Where, sizeof(Where), Suffix));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    unsigned char* Attribute = NULL;
    bool IsFound = false;
    Result = AssetJSON_ReadOptionalOwnedString(compound, (const unsigned char*)u8"attribute", &Attribute, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        IsFound = false;
    }
    if (Attribute != NULL)
    {
        definition->AttributeNames[definition->AttributeNameCount++] = Attribute;
    }
    if (!IsFound || (StringUTF8_GetByteLength(Attribute) == 0U)
        || (StringUTF8_GetByteLength(Attribute) > ATTRIBUTE_NAME_MAX_LENGTH) || !StringUTF8_IsEncodingValid(Attribute))
    {
        return Fail(definition, "%s[%" PRIu64 "] needs an \"attribute\": the name of a colour or custom attribute of the "
            "file (\"COLOR_0\", \"_DENSITY_0\"), 1 to %" PRIu64 " bytes.", key, (uint64_t)index,
            (uint64_t)ATTRIBUTE_NAME_MAX_LENGTH);
    }
    outMapping->Attribute = Attribute;
    bool HasChannel = false;
    for (size_t Channel = 0U; Channel < 4U; Channel++)
    {
        outMapping->Targets[Channel] = -1;
        unsigned char* Name = NULL;
        bool IsThere = false;
        Result = AssetJSON_ReadOptionalOwnedString(compound, ChannelKeys[Channel], &Name, &IsThere);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(definition, "%s[%" PRIu64 "].%s must be a name.", key, (uint64_t)index,
                (const char*)ChannelKeys[Channel]);
        }
        if (!IsThere)
        {
            continue;
        }
        int32_t Target = isWeights ? FindName(definition->MaterialNames, definition->MaterialCount, Name)
            : FindName(definition->ChannelNames, definition->ChannelCount, Name);
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(Name, Printable, sizeof(Printable));
        Memory_Free(Name);
        if (Target < 0)
        {
            return Fail(definition, "%s[%" PRIu64 "].%s is \"%s\", which is not one of its %s.", key, (uint64_t)index,
                (const char*)ChannelKeys[Channel], Printable, isWeights ? "\"materials\"" : "\"density_channels\"");
        }
        if (isUsed[Target])
        {
            return Fail(definition, "%s[%" PRIu64 "].%s feeds \"%s\", which another channel feeds already.", key,
                (uint64_t)index, (const char*)ChannelKeys[Channel], Printable);
        }
        isUsed[Target] = true;
        outMapping->Targets[Channel] = Target;
        HasChannel = true;
    }
    if (!HasChannel)
    {
        return Fail(definition, "%s[%" PRIu64 "] maps none of its channels (\"r\", \"g\", \"b\", \"a\").", key,
            (uint64_t)index);
    }
    return Error_CreateSuccess();
}

static Error ParseMappings(TerrainDefinition* definition, JSONCompound* root, bool isWeights)
{
    const char* Key = isWeights ? "weights" : "densities";
    JSONArray* Array = NULL;
    Error Result = GetOptionalArray(definition, root, (const unsigned char*)Key, &Array);
    if ((Result.Code != ErrorCode_Success) || (Array == NULL))
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Array);
    uint32_t Limit = isWeights ? definition->MaterialCount : definition->ChannelCount;
    if (Count > Limit)
    {
        return Fail(definition, "\"%s\" holds %" PRIu64 " mappings; it can have one per %s at most (%" PRIu32 ").", Key,
            (uint64_t)Count, isWeights ? "palette material" : "density channel", Limit);
    }
    bool IsUsed[TERRAIN_MAX_MATERIALS] = { false };
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(definition, "%s[%" PRIu64 "] must be a JSON object.", Key, (uint64_t)Index);
        }
        TerrainAttributeMapping* Mapping = isWeights ? &definition->Weights[Index] : &definition->Densities[Index];
        Result = ParseMapping(definition, Element.Value.Compound, Key, Index, isWeights, IsUsed, Mapping);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (isWeights)
        {
            definition->WeightCount = Index + 1U;
        }
        else
        {
            definition->DensityCount = Index + 1U;
        }
    }
    return Error_CreateSuccess();
}

/* "normals" ("smooth", the default, or "imported") and "blend_slot" (a name, default "blend"). */
static Error ParseModelOptions(TerrainDefinition* definition, JSONCompound* root)
{
    unsigned char* Text = NULL;
    bool IsFound = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(root, (const unsigned char*)u8"normals", &Text, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "\"normals\" must be \"smooth\" or \"imported\".");
    }
    bool IsImported = IsFound && AreNamesEqual(Text, (const unsigned char*)u8"imported");
    bool IsSmooth = !IsFound || AreNamesEqual(Text, (const unsigned char*)u8"smooth");
    Memory_Free(Text);
    if (!IsImported && !IsSmooth)
    {
        return Fail(definition, "\"normals\" must be \"smooth\" (computed from the welded mesh) or \"imported\" (the "
            "file's, so hard edges stay hard).");
    }
    definition->NormalMode = IsImported ? TerrainNormalMode_Imported : TerrainNormalMode_Smooth;
    Result = AssetJSON_ReadOptionalOwnedString(root, (const unsigned char*)u8"blend_slot", &definition->BlendSlot,
        &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "\"blend_slot\" must be a material slot's name.");
    }
    if (IsFound && ((StringUTF8_GetByteLength(definition->BlendSlot) == 0U)
        || (StringUTF8_GetByteLength(definition->BlendSlot) > TERRAIN_NAME_MAX_LENGTH)
        || !StringUTF8_IsEncodingValid(definition->BlendSlot)
        || AreNamesEqual(definition->BlendSlot, (const unsigned char*)u8"hole")
        || (FindName(definition->MaterialNames, definition->MaterialCount, definition->BlendSlot) >= 0)))
    {
        return Fail(definition, "\"blend_slot\" must be 1 to %" PRIu64 " bytes of UTF-8, and neither \"hole\" nor a "
            "palette material.", (uint64_t)TERRAIN_NAME_MAX_LENGTH);
    }
    // A palette material's slot gives its faces that material, but the import reads the slots "hole" and the blend
    // slot first: a material of either name could never be a face's.
    const unsigned char* Slot = IsFound ? definition->BlendSlot : TERRAIN_DEFAULT_BLEND_SLOT;
    for (uint32_t Index = 0U; Index < definition->MaterialCount; Index++)
    {
        bool IsHole = AreNamesEqual(definition->MaterialNames[Index], (const unsigned char*)u8"hole");
        if (IsHole || AreNamesEqual(definition->MaterialNames[Index], Slot))
        {
            return Fail(definition, "materials[%" PRIu32 "] is \"%s\", the name of the material slot that %s: a face "
                "could never take that material. Rename the material%s.", Index, IsHole ? "hole" : (const char*)Slot,
                IsHole ? "makes holes" : "uses the painted weights", IsHole ? "" : " or set another \"blend_slot\"");
        }
    }
    return Error_CreateSuccess();
}

static Error ReadBoolean(const TerrainDefinition* definition, JSONCompound* compound, const char* block,
    const unsigned char* key, bool* ioValue)
{
    Error Result = AssetJSON_ReadOptionalBoolean(compound, key, *ioValue, ioValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "%s.%s must be true or false.", block, (const char*)key);
    }
    return Result;
}

/* An enum by its names, from a table. */
static Error ReadEnum(const TerrainDefinition* definition, JSONCompound* compound, const char* block,
    const unsigned char* key, const PropertyEnumTable* table, int32_t* ioValue)
{
    unsigned char* Text = NULL;
    bool IsFound = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(compound, key, &Text, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        IsFound = true;
    }
    if (!IsFound)
    {
        return Error_CreateSuccess();
    }
    for (size_t Entry = 0U; (Text != NULL) && (Entry < table->EntryCount); Entry++)
    {
        if (AreNamesEqual(Text, table->Entries[Entry].Name))
        {
            *ioValue = table->Entries[Entry].Value;
            Memory_Free(Text);
            return Error_CreateSuccess();
        }
    }
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Text, Printable, sizeof(Printable));
    Memory_Free(Text);
    return Fail(definition, "%s.%s is \"%s\"; it must be one of the %s names.", block, (const char*)key, Printable,
        (const char*)table->Name);
}

/* "render": the placements' render defaults. */
static Error ParseRender(TerrainDefinition* definition, JSONCompound* root)
{
    JSONCompound* Render = NULL;
    Error Result = GetOptionalCompound(definition, root, (const unsigned char*)u8"render", &Render);
    if ((Result.Code != ErrorCode_Success) || (Render == NULL))
    {
        return Result;
    }
    char Where[WHERE_CAPACITY];
    Result = AssetJSON_RejectUnknownKeys(Render, RenderKeys, KEY_COUNT(RenderKeys),
        FormatWhere(definition, Where, sizeof(Where), ", render"));
    TerrainRenderSettings* Settings = &definition->Settings.Render;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(definition, Render, "render", (const unsigned char*)u8"has_outline", &Settings->HasOutline);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(definition, Render, "render", (const unsigned char*)u8"casts_shadows",
            &Settings->CastsShadows);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(definition, Render, "render", (const unsigned char*)u8"is_draw_distance_fixed",
            &Settings->IsDrawDistanceFixed);
    }
    int32_t Tier = (int32_t)Settings->ShadowTier;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadEnum(definition, Render, "render", (const unsigned char*)u8"shadow_tier", &WORLD_SHADOW_TIER_ENUM,
            &Tier);
        Settings->ShadowTier = (WorldShadowTier)Tier;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalFloat(Render, (const unsigned char*)u8"draw_distance", Settings->DrawDistance,
            &Settings->DrawDistance, NULL);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(definition, "render.draw_distance must be a number of metres.");
        }
        if (!(Settings->DrawDistance > 0.0f))
        {
            return Fail(definition, "render.draw_distance is %g; it must be above 0.", (double)Settings->DrawDistance);
        }
    }
    return Result;
}

/* "collision": the placements' collision defaults; "collides_with" left out is the layer's default mask. */
static Error ParseCollision(TerrainDefinition* definition, JSONCompound* root)
{
    JSONCompound* Collision = NULL;
    Error Result = GetOptionalCompound(definition, root, (const unsigned char*)u8"collision", &Collision);
    if ((Result.Code != ErrorCode_Success) || (Collision == NULL))
    {
        return Result;
    }
    char Where[WHERE_CAPACITY];
    Result = AssetJSON_RejectUnknownKeys(Collision, CollisionKeys, KEY_COUNT(CollisionKeys),
        FormatWhere(definition, Where, sizeof(Where), ", collision"));
    TerrainCollisionSettings* Settings = &definition->Settings.Collision;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(definition, Collision, "collision", (const unsigned char*)u8"is_enabled",
            &Settings->IsEnabled);
    }
    int32_t Layer = (int32_t)Settings->Layer;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadEnum(definition, Collision, "collision", (const unsigned char*)u8"layer", &COLLISION_LAYER_ENUM,
            &Layer);
        Settings->Layer = (CollisionLayer)Layer;
    }
    JSONArray* Mask = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptionalArray(definition, Collision, (const unsigned char*)u8"collides_with", &Mask);
    }
    if ((Result.Code != ErrorCode_Success) || (Mask == NULL))
    {
        return Result;
    }
    Settings->HasCollidesWith = true;
    Settings->CollidesWith = 0U;
    for (size_t Index = 0U; Index < JSONArray_GetElementCount(Mask); Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Mask, Index, JSONValueType_String, &Element);
        unsigned char* Name = NULL;
        if (Result.Code == ErrorCode_Success)
        {
            Result = AssetJSON_ValueToOwnedString(&Element, &Name);
        }
        CollisionLayer Entry = CollisionLayer_World;
        bool IsLayer = (Result.Code == ErrorCode_Success) && CollisionLayer_TryParse(Name, &Entry);
        Error_Deconstruct(&Result);
        Memory_Free(Name);
        if (!IsLayer || ((Settings->CollidesWith & COLLISION_LAYER_BIT(Entry)) != 0U))
        {
            return Fail(definition, "collision.collides_with[%" PRIu64 "] must be a layer's name, each named once.",
                (uint64_t)Index);
        }
        Settings->CollidesWith |= COLLISION_LAYER_BIT(Entry);
    }
    return Error_CreateSuccess();
}

static Error ParseDefinition(TerrainDefinition* definition, JSONCompound* root, const unsigned char* sourceDescription)
{
    char Where[WHERE_CAPACITY];
    Error Result = AssetJSON_RejectUnknownKeys(root, RootKeys, KEY_COUNT(RootKeys),
        FormatWhere(definition, Where, sizeof(Where), ""));
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseSource(definition, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadLocation(root, (definition->Source == TerrainSource_Model) ? (const unsigned char*)u8"model"
            : (const unsigned char*)u8"data", sourceDescription, &definition->Location);
    }
    if ((Result.Code == ErrorCode_Success) && (definition->Source == TerrainSource_Model))
    {
        Result = AssetJSON_ReadOptionalFormat(root, &ASSET_EXTENSIONS_TERRAIN_MODEL, TERRAIN_KIND, &definition->Format);
        if (Result.Code == ErrorCode_Success)
        {
            Result = OwnedAssetLocation_ValidateReferenceFormat(&definition->Location, definition->Format,
                &ASSET_EXTENSIONS_TERRAIN_MODEL, TERRAIN_KIND);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseNames(definition, root);
    }
    if ((Result.Code == ErrorCode_Success) && (definition->Source == TerrainSource_Model))
    {
        Result = ParseMappings(definition, root, true);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseMappings(definition, root, false);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseModelOptions(definition, root);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalFloat(root, (const unsigned char*)u8"chunk_size", TERRAIN_DEFAULT_CHUNK_SIZE,
            &definition->ChunkSize, NULL);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            Result = Fail(definition, "\"chunk_size\" must be a number of metres.");
        }
        else if (!(definition->ChunkSize >= TERRAIN_MIN_CHUNK_SIZE) || !(definition->ChunkSize <= TERRAIN_MAX_CHUNK_SIZE))
        {
            Result = Fail(definition, "\"chunk_size\" is %g; it must be %g to %g metres.", (double)definition->ChunkSize,
                (double)TERRAIN_MIN_CHUNK_SIZE, (double)TERRAIN_MAX_CHUNK_SIZE);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseRender(definition, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseCollision(definition, root);
    }
    return Result;
}


// Static functions: the asset.
static void DestroyAsset(TerrainAsset* asset)
{
    if (asset == NULL)
    {
        return;
    }
    TerrainMesh_Destroy(asset->_mesh);
    Memory_Free(asset->_names);
    Memory_Free(asset);
}

static void TerrainLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    DestroyAsset(self->DestroyContext);
}

static const LoadedAssetVTable TerrainLoadedVTable = { TerrainLoaded_Destroy };

/* The asset around a loaded mesh: the definition's settings and a copy of its names in one block. */
static TerrainAsset* BuildAsset(const TerrainDefinition* definition, TerrainMesh* mesh)
{
    size_t Bytes = 0U;
    for (uint32_t Index = 0U; Index < definition->MaterialCount; Index++)
    {
        Bytes += StringUTF8_GetByteLength(definition->MaterialNames[Index]) + 1U;
    }
    for (uint32_t Index = 0U; Index < definition->ChannelCount; Index++)
    {
        Bytes += StringUTF8_GetByteLength(definition->ChannelNames[Index]) + 1U;
    }
    TerrainAsset* Asset = Memory_Allocate(sizeof(TerrainAsset));
    Memory_Zero(Asset, sizeof(*Asset));
    Asset->_mesh = mesh;
    Asset->_settings = definition->Settings;
    Asset->_names = Memory_Allocate((Bytes > 0U) ? Bytes : 1U);
    unsigned char* Cursor = Asset->_names;
    for (uint32_t Index = 0U; Index < definition->MaterialCount; Index++)
    {
        size_t Length = StringUTF8_GetByteLength(definition->MaterialNames[Index]) + 1U;
        Memory_Copy(definition->MaterialNames[Index], Cursor, Length);
        Asset->_materialNames[Index] = Cursor;
        Cursor += Length;
    }
    for (uint32_t Index = 0U; Index < definition->ChannelCount; Index++)
    {
        size_t Length = StringUTF8_GetByteLength(definition->ChannelNames[Index]) + 1U;
        Memory_Copy(definition->ChannelNames[Index], Cursor, Length);
        Asset->_densityChannelNames[Index] = Cursor;
        Cursor += Length;
    }
    Asset->_materialCount = definition->MaterialCount;
    Asset->_densityChannelCount = definition->ChannelCount;
    return Asset;
}

static Error TerrainDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    (void)dependencyUser;
    TerrainDefinition* Definition = self;
    bool IsModel = Definition->Source == TerrainSource_Model;
    const unsigned char* Preferred = IsModel ? Definition->Format : DATA_EXTENSION;
    AssetExtensionFilter Extensions = IsModel ? AssetFormat_SelectFilter(&ASSET_EXTENSIONS_TERRAIN_MODEL, &Preferred)
        : ASSET_EXTENSIONS_TERRAIN_DATA;
    AssetLocation Location = OwnedAssetLocation_View(&Definition->Location);
    AssetResourcePath* PathHandle = NULL;
    Error Result = AssetManager_AcquireResourcePath(manager, Definition->Base.Type, &Location, &Extensions, Preferred,
        &PathHandle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Definition->Base.Name, Printable, sizeof(Printable));
    char Description[NAME_PRINT_CAPACITY + 32U];
    snprintf(Description, sizeof(Description), "Terrain \"%s\"", Printable);
    TerrainMesh* Loaded = NULL;
    if (IsModel)
    {
        TerrainGltfOptions Options =
        {
            .Weights = Definition->Weights, .WeightMappingCount = Definition->WeightCount,
            .Densities = Definition->Densities, .DensityMappingCount = Definition->DensityCount,
            .MaterialNames = (const unsigned char* const*)Definition->MaterialNames,
            .MaterialCount = Definition->MaterialCount, .DensityChannelCount = Definition->ChannelCount,
            .BlendSlotName = Definition->BlendSlot, .NormalMode = Definition->NormalMode,
            .ChunkSize = Definition->ChunkSize, .FilePath = NULL, .Logger = AssetManager_GetLogger(manager),
            .Description = (const unsigned char*)Description
        };
        Result = TerrainMesh_CreateFromGltfFile(AssetResourcePath_Get(PathHandle), &Options, &Loaded);
    }
    else
    {
        Result = TerrainMesh_LoadGHDF(AssetResourcePath_Get(PathHandle), Definition->ChunkSize,
            (const unsigned char*)Description, &Loaded);
    }
    Error ReleaseResult = AssetManager_ReleaseResourcePath(manager, PathHandle);
    Error_Deconstruct(&ReleaseResult);
    if ((Result.Code == ErrorCode_Success) && ((TerrainMesh_GetMaterialCount(Loaded) != Definition->MaterialCount)
        || (TerrainMesh_GetDensityChannelCount(Loaded) != Definition->ChannelCount)))
    {
        Result = Error_Construct4(ErrorCode_InvalidAssetData, "%s: its data holds %" PRIu32 " materials and %" PRIu32
            " density channels; the definition names %" PRIu32 " and %" PRIu32 ".", Description,
            TerrainMesh_GetMaterialCount(Loaded), TerrainMesh_GetDensityChannelCount(Loaded), Definition->MaterialCount,
            Definition->ChannelCount);
    }
    if (Result.Code != ErrorCode_Success)
    {
        TerrainMesh_Destroy(Loaded);
        return Result;
    }
    TerrainAsset* Asset = BuildAsset(Definition, Loaded);
    outLoaded->Asset = Asset;
    outLoaded->VTable = &TerrainLoadedVTable;
    outLoaded->DestroyContext = Asset;
    return Error_CreateSuccess();
}

static void TerrainDefinition_Destroy(void* self)
{
    TerrainDefinition* Definition = self;
    OwnedAssetLocation_Deconstruct(&Definition->Location);
    Memory_Free(Definition->Format);
    Memory_Free(Definition->BlendSlot);
    for (uint32_t Index = 0U; Index < Definition->MaterialCount; Index++)
    {
        Memory_Free(Definition->MaterialNames[Index]);
    }
    for (uint32_t Index = 0U; Index < Definition->ChannelCount; Index++)
    {
        Memory_Free(Definition->ChannelNames[Index]);
    }
    for (size_t Index = 0U; Index < Definition->AttributeNameCount; Index++)
    {
        Memory_Free(Definition->AttributeNames[Index]);
    }
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable TerrainDefinitionVTable = { TerrainDefinition_LoadAsset, TerrainDefinition_Destroy };


// Public functions.
Error TerrainDefinition_Construct(AssetManager* manager, const UserData* userData,
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
    TerrainDefinition* Definition = Memory_Allocate(sizeof(TerrainDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &TerrainDefinitionVTable;
    Definition->Settings = TerrainSettings_CreateDefault();
    Definition->ChunkSize = TERRAIN_DEFAULT_CHUNK_SIZE;
    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDefinition(Definition, RootCompound, sourceDescription);
    }
    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);
    if (Result.Code != ErrorCode_Success)
    {
        TerrainDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
