#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "CollisionLayer.h"
#include "Logger.h"
#include "TerrainAsset.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The "terrain" asset type (references/asset_structure.md, "Terrains"; TerrainAsset.h): the definition's parser, which
 * checks every rule while the definitions are read (unknown keys refused everywhere, the palette's and the channels'
 * names, the attribute mappings against them, the chunk size, the render and collision blocks), and the loader, which
 * imports the .glb (source "model": TerrainMesh_CreateFromGltfFile, the file treated as hostile) or reads the binary form
 * (source "data": TerrainMesh_LoadGHDF) and checks it against the definition, then loads the palette's terrain materials
 * as its dependencies and checks that their maps of one kind share one size (a texture array holds one). The
 * definition's "rules" are read with the palette's names and copied into the asset. The loaded asset owns its
 * mesh, the names and the rules.
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
/** A rule's ranges: the slope in degrees (0 flat to 180, an overhang's underside), the height in metres (the world's
 *  position limit), and the widest ramps. */
#define RULE_SLOPE_MAX_DEGREES 180.0
#define RULE_SLOPE_MAX_BLEND 90.0
#define RULE_HEIGHT_LIMIT 1.0e7
#define RULE_HEIGHT_MAX_BLEND 10000.0
/** Room for a rule's place in messages ("rules[7]"), a block's ("rules[7].height") and a "where" suffix built of it. */
#define RULE_WHERE_CAPACITY ((size_t)32)
#define RULE_BLOCK_WHERE_CAPACITY ((size_t)48)
#define RULE_SUFFIX_CAPACITY ((size_t)56)
/** A rule's noise: its default cell size (metres), and the largest one. */
#define RULE_NOISE_DEFAULT_SCALE 4.0f
#define RULE_NOISE_MAX_SCALE 10000.0


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
    /** The rules, as the asset gets them. */
    TerrainRule Rules[TERRAIN_MAX_RULES];
    uint32_t RuleCount;
} TerrainDefinition;


// Fields.
static const unsigned char* const RootKeys[] =
{
    u8"name", u8"source", u8"model", u8"format", u8"data", u8"weights", u8"densities", u8"normals", u8"blend_slot",
    u8"materials", u8"density_channels", u8"chunk_size", u8"render", u8"collision", u8"rules"
};
static const unsigned char* const ModelOnlyKeys[] =
{
    u8"model", u8"format", u8"weights", u8"densities", u8"normals", u8"blend_slot"
};
static const unsigned char* const MappingKeys[] = { u8"attribute", u8"r", u8"g", u8"b", u8"a" };
static const unsigned char* const RenderKeys[] =
{
    u8"has_outline", u8"casts_shadows", u8"has_shadow_distance", u8"shadow_distance", u8"is_shadow_distance_fixed",
    u8"draw_distance", u8"is_draw_distance_fixed", u8"haze", u8"shadow_tier"
};
/* The names an older definition's render.shadow_tier may hold: which of two shadow maps the terrain cast into. */
static const unsigned char* const LegacyShadowTierNames[] = { u8"both", u8"near", u8"far" };
static const unsigned char* const CollisionKeys[] = { u8"is_enabled", u8"layer", u8"collides_with" };
static const unsigned char* const RuleKeys[] =
{
    u8"material", u8"opacity", u8"only_on", u8"slope", u8"height", u8"noise"
};
static const unsigned char* const RangeKeys[] = { u8"min", u8"max", u8"blend" };
static const unsigned char* const NoiseKeys[] = { u8"scale", u8"strength", u8"seed" };
/* What the materials' map kinds are called in messages, by TerrainMaterialMapKind. */
static const char* const MapKindNames[TerrainMaterialMapKind_Count] = { "albedo", "normal", "roughness_ao" };
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

/* An older definition's "shadow_tier" (which of two shadow maps the terrain cast into), read into the shadow distance
 * with one warning to @p log (may be NULL) naming what replaces it: "near" cast within WORLD_MODEL_NEAR_TIER_SHADOW_DISTANCE
 * metres of the camera (that shadow distance, unless the definition gives its own), "both" is what every terrain does,
 * "far" (only the coarse shadow far out) is no setting. Anything else is refused, as before. */
static Error ParseLegacyShadowTier(TerrainDefinition* definition, JSONCompound* render, bool hasOwnShadowDistance,
    Logger* log)
{
    unsigned char* Text = NULL;
    bool IsFound = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(render, (const unsigned char*)u8"shadow_tier", &Text, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "render.shadow_tier must be \"both\", \"near\" or \"far\".");
    }
    if (!IsFound)
    {
        return Error_CreateSuccess();
    }
    size_t Tier = KEY_COUNT(LegacyShadowTierNames);
    for (size_t Index = 0U; (Text != NULL) && (Index < KEY_COUNT(LegacyShadowTierNames)); Index++)
    {
        if (AreNamesEqual(Text, LegacyShadowTierNames[Index]))
        {
            Tier = Index;
        }
    }
    if (Tier >= KEY_COUNT(LegacyShadowTierNames))
    {
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(Text, Printable, sizeof(Printable));
        Memory_Free(Text);
        return Fail(definition, "render.shadow_tier is \"%s\"; it must be \"both\", \"near\" or \"far\".", Printable);
    }
    Memory_Free(Text);

    TerrainRenderSettings* Settings = &definition->Settings.Render;
    const char* Warning = "render.shadow_tier \"both\" is what every terrain does; leave it out.";
    if (Tier == 1U)
    {
        if (!hasOwnShadowDistance)
        {
            Settings->HasShadowDistance = true;
            Settings->ShadowDistance = WORLD_MODEL_NEAR_TIER_SHADOW_DISTANCE;
        }
        Warning = "render.shadow_tier \"near\" is read as render.shadow_distance; write that instead.";
    }
    else if (Tier == 2U)
    {
        Warning = "render.shadow_tier \"far\" is not read: every caster casts from the camera out to its shadow "
            "distance; leave it out.";
    }
    if (log != NULL)
    {
        char Name[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(definition->Base.Name, Name, sizeof(Name));
        Error LogResult = Logger_LogWarningFormatted(log, (const unsigned char*)u8"Terrain \"%s\": %s", Name, Warning);
        Error_Deconstruct(&LogResult);
    }
    return Error_CreateSuccess();
}

/* The render block's shadow distance: a value given without has_shadow_distance turns it on (the hand-written form; an
 * explicit has_shadow_distance wins), its fixed flag, then an older definition's shadow tier. */
static Error ParseRenderShadowDistance(TerrainDefinition* definition, JSONCompound* render, Logger* log)
{
    TerrainRenderSettings* Settings = &definition->Settings.Render;
    bool HasDistance = false;
    Error Result = AssetJSON_ReadOptionalFloat(render, (const unsigned char*)u8"shadow_distance", Settings->ShadowDistance,
        &Settings->ShadowDistance, &HasDistance);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "render.shadow_distance must be a number of metres.");
    }
    if (!(Settings->ShadowDistance > 0.0f))
    {
        return Fail(definition, "render.shadow_distance is %g; it must be above 0 (metres from the camera).",
            (double)Settings->ShadowDistance);
    }
    Settings->HasShadowDistance = HasDistance;
    Result = ReadBoolean(definition, render, "render", (const unsigned char*)u8"has_shadow_distance",
        &Settings->HasShadowDistance);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(definition, render, "render", (const unsigned char*)u8"is_shadow_distance_fixed",
            &Settings->IsShadowDistanceFixed);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseLegacyShadowTier(definition, render, HasDistance, log);
    }
    return Result;
}

/* "render": the placements' render defaults; warnings go to @p log (may be NULL). */
static Error ParseRender(TerrainDefinition* definition, JSONCompound* root, Logger* log)
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
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseRenderShadowDistance(definition, Render, log);
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
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalFloat(Render, (const unsigned char*)u8"haze", Settings->Haze, &Settings->Haze,
            NULL);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(definition, "render.haze must be a number (1 the real air's haze, 0 none).");
        }
        if (!((Settings->Haze >= 0.0f) && (Settings->Haze <= WORLD_MODEL_MAX_HAZE)))
        {
            return Fail(definition, "render.haze is %g; it must be at least 0 and at most %g (1 the real air's haze, 0 "
                "none).", (double)Settings->Haze, (double)WORLD_MODEL_MAX_HAZE);
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

/* An optional float of a rule's block within [lowest, highest]. */
static Error ReadRuleFloat(const TerrainDefinition* definition, JSONCompound* compound, const char* where,
    const unsigned char* key, double lowest, double highest, float defaultValue, float* outValue, bool* outFound)
{
    Error Result = AssetJSON_ReadOptionalFloat(compound, key, defaultValue, outValue, outFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "%s.%s must be a number.", where, (const char*)key);
    }
    if (((double)*outValue < lowest) || ((double)*outValue > highest))
    {
        return Fail(definition, "%s.%s is %g; it must be %g to %g.", where, (const char*)key, (double)*outValue, lowest,
            highest);
    }
    return Error_CreateSuccess();
}

/* A rule's range block ("slope" or "height"): { "min", "max", "blend" }, at least one bound, the bounds in order. */
static Error ParseRuleRange(const TerrainDefinition* definition, JSONCompound* rule, const char* where,
    const unsigned char* key, double lowest, double highest, double maxBlend, TerrainRuleRange* outRange)
{
    Memory_Zero(outRange, sizeof(*outRange));
    JSONCompound* Block = NULL;
    Error Result = GetOptionalCompound(definition, rule, key, &Block);
    if ((Result.Code != ErrorCode_Success) || (Block == NULL))
    {
        return Result;
    }
    char Inner[RULE_BLOCK_WHERE_CAPACITY];
    snprintf(Inner, sizeof(Inner), "%s.%s", where, (const char*)key);
    char Where[WHERE_CAPACITY];
    char Suffix[RULE_SUFFIX_CAPACITY];
    snprintf(Suffix, sizeof(Suffix), ", %s", Inner);
    Result = AssetJSON_RejectUnknownKeys(Block, RangeKeys, KEY_COUNT(RangeKeys),
        FormatWhere(definition, Where, sizeof(Where), Suffix));
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRuleFloat(definition, Block, Inner, (const unsigned char*)u8"min", lowest, highest, 0.0f,
            &outRange->Minimum, &outRange->HasMinimum);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRuleFloat(definition, Block, Inner, (const unsigned char*)u8"max", lowest, highest, 0.0f,
            &outRange->Maximum, &outRange->HasMaximum);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRuleFloat(definition, Block, Inner, (const unsigned char*)u8"blend", 0.0, maxBlend, 0.0f,
            &outRange->Blend, NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!outRange->HasMinimum && !outRange->HasMaximum)
    {
        return Fail(definition, "%s needs a \"min\" or a \"max\".", Inner);
    }
    if (outRange->HasMinimum && outRange->HasMaximum && (outRange->Minimum > outRange->Maximum))
    {
        return Fail(definition, "%s's min (%g) is above its max (%g).", Inner, (double)outRange->Minimum,
            (double)outRange->Maximum);
    }
    outRange->IsEnabled = true;
    return Error_CreateSuccess();
}

/* A rule's "noise": { "scale", "strength", "seed" }, each optional. */
static Error ParseRuleNoise(const TerrainDefinition* definition, JSONCompound* rule, const char* where,
    TerrainRuleNoise* outNoise)
{
    Memory_Zero(outNoise, sizeof(*outNoise));
    JSONCompound* Block = NULL;
    Error Result = GetOptionalCompound(definition, rule, (const unsigned char*)u8"noise", &Block);
    if ((Result.Code != ErrorCode_Success) || (Block == NULL))
    {
        return Result;
    }
    char Inner[RULE_BLOCK_WHERE_CAPACITY];
    snprintf(Inner, sizeof(Inner), "%s.noise", where);
    char Where[WHERE_CAPACITY];
    char Suffix[RULE_SUFFIX_CAPACITY];
    snprintf(Suffix, sizeof(Suffix), ", %s", Inner);
    Result = AssetJSON_RejectUnknownKeys(Block, NoiseKeys, KEY_COUNT(NoiseKeys),
        FormatWhere(definition, Where, sizeof(Where), Suffix));
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRuleFloat(definition, Block, Inner, (const unsigned char*)u8"scale", 0.0, RULE_NOISE_MAX_SCALE,
            RULE_NOISE_DEFAULT_SCALE, &outNoise->Scale, NULL);
    }
    if ((Result.Code == ErrorCode_Success) && !(outNoise->Scale > 0.0f))
    {
        Result = Fail(definition, "%s.scale must be above 0 metres.", Inner);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRuleFloat(definition, Block, Inner, (const unsigned char*)u8"strength", 0.0, 1.0, 1.0f,
            &outNoise->Strength, NULL);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    int64_t Seed = 0;
    Result = AssetJSON_ReadOptionalInteger(Block, (const unsigned char*)u8"seed", 0, &Seed);
    if ((Result.Code != ErrorCode_Success) || (Seed < 0) || (Seed > (int64_t)UINT32_MAX))
    {
        Error_Deconstruct(&Result);
        return Fail(definition, "%s.seed must be a whole number from 0 to %" PRIu32 ".", Inner, UINT32_MAX);
    }
    outNoise->Seed = (uint32_t)Seed;
    outNoise->IsEnabled = true;
    return Error_CreateSuccess();
}

/* A rule's "material" (a palette name, required) and "only_on" (palette names, each once). */
static Error ParseRuleMaterials(const TerrainDefinition* definition, JSONCompound* rule, const char* where,
    TerrainRule* outRule)
{
    unsigned char* Name = NULL;
    bool IsFound = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(rule, (const unsigned char*)u8"material", &Name, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        IsFound = false;
    }
    int32_t Placed = IsFound ? FindName(definition->MaterialNames, definition->MaterialCount, Name) : -1;
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Name, Printable, sizeof(Printable));
    Memory_Free(Name);
    if (Placed < 0)
    {
        return IsFound ? Fail(definition, "%s.material is \"%s\", which is not one of its \"materials\".", where,
            Printable) : Fail(definition, "%s needs a \"material\": the palette material it puts down.", where);
    }
    outRule->Material = (uint32_t)Placed;
    outRule->CoveredMaterials = TERRAIN_ALL_MATERIALS;
    JSONArray* OnlyOn = NULL;
    Result = GetOptionalArray(definition, rule, (const unsigned char*)u8"only_on", &OnlyOn);
    if ((Result.Code != ErrorCode_Success) || (OnlyOn == NULL))
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(OnlyOn);
    if (Count == 0U)
    {
        return Fail(definition, "%s.only_on must name at least one palette material.", where);
    }
    outRule->CoveredMaterials = 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Element;
        unsigned char* Entry = NULL;
        Result = JSONArray_GetVerified(OnlyOn, Index, JSONValueType_String, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = AssetJSON_ValueToOwnedString(&Element, &Entry);
        }
        bool IsString = Result.Code == ErrorCode_Success;
        Error_Deconstruct(&Result);
        int32_t Covered = IsString ? FindName(definition->MaterialNames, definition->MaterialCount, Entry) : -1;
        AssetJSON_CopyPrintable(Entry, Printable, sizeof(Printable));
        Memory_Free(Entry);
        if (Covered < 0)
        {
            return IsString ? Fail(definition, "%s.only_on[%" PRIu64 "] is \"%s\", which is not one of its "
                "\"materials\".", where, (uint64_t)Index, Printable) : Fail(definition, "%s.only_on[%" PRIu64 "] must "
                "be a palette material's name.", where, (uint64_t)Index);
        }
        if ((outRule->CoveredMaterials & (1U << (uint32_t)Covered)) != 0U)
        {
            return Fail(definition, "%s.only_on names \"%s\" twice.", where, Printable);
        }
        outRule->CoveredMaterials |= 1U << (uint32_t)Covered;
    }
    return Error_CreateSuccess();
}

/* "rules": at most TERRAIN_MAX_RULES, each { "material", "opacity", "only_on", "slope", "height", "noise" }. */
static Error ParseRules(TerrainDefinition* definition, JSONCompound* root)
{
    JSONArray* Rules = NULL;
    Error Result = GetOptionalArray(definition, root, (const unsigned char*)u8"rules", &Rules);
    if ((Result.Code != ErrorCode_Success) || (Rules == NULL))
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Rules);
    if (Count > TERRAIN_MAX_RULES)
    {
        return Fail(definition, "\"rules\" holds %" PRIu64 " rules; at most %u are allowed.", (uint64_t)Count,
            TERRAIN_MAX_RULES);
    }
    for (size_t Index = 0U; Index < Count; Index++)
    {
        char RuleWhere[RULE_WHERE_CAPACITY];
        snprintf(RuleWhere, sizeof(RuleWhere), "rules[%" PRIu64 "]", (uint64_t)Index);
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Rules, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(definition, "%s must be a JSON object.", RuleWhere);
        }
        JSONCompound* Rule = Element.Value.Compound;
        char Where[WHERE_CAPACITY];
        char Suffix[RULE_SUFFIX_CAPACITY];
        snprintf(Suffix, sizeof(Suffix), ", %s", RuleWhere);
        TerrainRule* Out = &definition->Rules[Index];
        Memory_Zero(Out, sizeof(*Out));
        Result = AssetJSON_RejectUnknownKeys(Rule, RuleKeys, KEY_COUNT(RuleKeys),
            FormatWhere(definition, Where, sizeof(Where), Suffix));
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseRuleMaterials(definition, Rule, RuleWhere, Out);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadRuleFloat(definition, Rule, RuleWhere, (const unsigned char*)u8"opacity", 0.0, 1.0, 1.0f,
                &Out->Opacity, NULL);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseRuleRange(definition, Rule, RuleWhere, (const unsigned char*)u8"slope", 0.0,
                RULE_SLOPE_MAX_DEGREES, RULE_SLOPE_MAX_BLEND, &Out->Slope);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseRuleRange(definition, Rule, RuleWhere, (const unsigned char*)u8"height", -RULE_HEIGHT_LIMIT,
                RULE_HEIGHT_LIMIT, RULE_HEIGHT_MAX_BLEND, &Out->Height);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseRuleNoise(definition, Rule, RuleWhere, &Out->Noise);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        definition->RuleCount = (uint32_t)(Index + 1U);
    }
    return Error_CreateSuccess();
}

static Error ParseDefinition(TerrainDefinition* definition, JSONCompound* root, const unsigned char* sourceDescription,
    Logger* log)
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
        Result = ParseRender(definition, root, log);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseCollision(definition, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseRules(definition, root);
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
    size_t Bytes = StringUTF8_GetByteLength(definition->Base.Name) + 1U;
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
    Asset->_names = Memory_Allocate(Bytes);
    unsigned char* Cursor = Asset->_names;
    size_t NameLength = StringUTF8_GetByteLength(definition->Base.Name) + 1U;
    Memory_Copy(definition->Base.Name, Cursor, NameLength);
    Asset->_name = Cursor;
    Cursor += NameLength;
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
    Memory_Copy(definition->Rules, Asset->_rules, sizeof(definition->Rules));
    Asset->_ruleCount = definition->RuleCount;
    return Asset;
}

/* Loads the palette's materials under the terrain's dependency user (the manager releases them with it, also when the
 * load fails), then checks that their maps of each kind share one size. */
static Error LoadPaletteMaterials(const TerrainDefinition* definition, AssetManager* manager,
    AssetUserID dependencyUser, const char* description, TerrainAsset* asset)
{
    for (uint32_t Index = 0U; Index < definition->MaterialCount; Index++)
    {
        TerrainMaterial* Loaded = NULL;
        Error Result = AssetManager_LoadTerrainMaterial(manager, definition->MaterialNames[Index], dependencyUser,
            &Loaded);
        if (Result.Code != ErrorCode_Success)
        {
            char Printable[NAME_PRINT_CAPACITY];
            AssetJSON_CopyPrintable(definition->MaterialNames[Index], Printable, sizeof(Printable));
            Error Wrapped = Error_Construct4(Result.Code, "%s: its material \"%s\" (materials[%" PRIu32 "]) could not be "
                "loaded: %s", description, Printable, Index, (Result.Message != NULL) ? (const char*)Result.Message
                : "an error.");
            Error_Deconstruct(&Result);
            return Wrapped;
        }
        asset->_materials[Index] = Loaded;
    }
    for (uint32_t Kind = 0U; Kind < (uint32_t)TerrainMaterialMapKind_Count; Kind++)
    {
        int32_t First = -1;
        int32_t FirstWidth = 0;
        int32_t FirstHeight = 0;
        for (uint32_t Index = 0U; Index < definition->MaterialCount; Index++)
        {
            int32_t Width = 0;
            int32_t Height = 0;
            if (TerrainMaterial_GetMap(asset->_materials[Index], (TerrainMaterialMapKind)Kind, &Width, &Height) == NULL)
            {
                continue;
            }
            if (First < 0)
            {
                First = (int32_t)Index;
                FirstWidth = Width;
                FirstHeight = Height;
                continue;
            }
            if ((Width != FirstWidth) || (Height != FirstHeight))
            {
                char FirstName[NAME_PRINT_CAPACITY];
                char OtherName[NAME_PRINT_CAPACITY];
                AssetJSON_CopyPrintable(definition->MaterialNames[First], FirstName, sizeof(FirstName));
                AssetJSON_CopyPrintable(definition->MaterialNames[Index], OtherName, sizeof(OtherName));
                return Error_Construct4(ErrorCode_InvalidAssetData, "%s: its materials' %s maps must share one size (a "
                    "texture array holds one): \"%s\" has %d x %d pixels, \"%s\" %d x %d.", description,
                    MapKindNames[Kind], FirstName, (int)FirstWidth, (int)FirstHeight, OtherName, (int)Width, (int)Height);
            }
        }
    }
    return Error_CreateSuccess();
}

static Error TerrainDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
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
    Result = LoadPaletteMaterials(Definition, manager, dependencyUser, Description, Asset);
    if (Result.Code != ErrorCode_Success)
    {
        DestroyAsset(Asset);
        return Result;
    }
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
        Result = ParseDefinition(Definition, RootCompound, sourceDescription,
            (manager != NULL) ? AssetManager_GetLogger(manager) : NULL);
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
