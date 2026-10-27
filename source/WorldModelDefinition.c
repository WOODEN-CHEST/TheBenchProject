#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "AnimationLibrary.h"
#include "GameJSON.h"
#include "GameModel.h"
#include "GameTexture.h"
#include "Logger.h"
#include "WorldModel.h"
#include "WorldModelCollisionInternal.h"
#include "WorldModelSocketInternal.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRJSON.h"

/*
 * The "world_model" asset type (references/asset_structure.md, "World models"): the definition's parser, which checks
 * every rule while the definitions are read (a bad file is skipped at startup, not at load), and the loader.
 *
 * DEPENDENCIES. A world model is built on other assets, as a sprite animation is built on sprite sheets: its LOD models
 * and the texture assets its variants name are loaded under the world model's dependency user, so they stay loaded
 * exactly as long as it does and unload right after it (the manager releases that user; this loader never releases
 * them). A failed dependency fails the world model, and the manager releases what had loaded.
 *
 * The loaded WorldModel is pure data: the LODs' models and the variants' textures are borrowed pointers into those
 * dependencies, and the variants, their overrides and their names live in one allocation the asset owns. Nothing in it
 * points into the definition, which may be replaced while the asset is loaded. The "collision" section is parsed
 * and loaded by source/WorldModelCollisionDefinition.c: its sets live in a second block the asset owns, with the hulls
 * and meshes built from models that load as further dependencies. The animation libraries are dependencies too;
 * the skeleton is the first skinned LOD's, which every other skinned LOD must share. The animation graph is a
 * dependency as well; the "sockets" and "first_person" sections are parsed and loaded by
 * source/WorldModelSocketDefinition.c into a third block the asset owns.
 */


// Macros.
/** Room for a path in the definition ("variants[3]"), for an element's ("variants[3].material_overrides[1]"), for a
 *  message's "where" ("world model \"desk\", " and a path) and for one message's text. */
#define PATH_CAPACITY ((size_t)48)
#define ELEMENT_PATH_CAPACITY ((size_t)96)
#define WHERE_CAPACITY ((size_t)256)
#define MESSAGE_CAPACITY ((size_t)384)
/** How many bytes of a name from the file a message shows. */
#define NAME_PRINT_CAPACITY ((size_t)129)
/** What a world model definition is called in messages. */
#define WORLD_MODEL_KIND "world model"
/** The LOD hysteresis must stay below 1: at 1 a LOD would switch back to a finer one only at distance 0. */
#define LOD_HYSTERESIS_LIMIT 1.0f
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))
/** The texture keys of a variant's material override (albedo, normal, mra, emissive). */
#define OVERRIDE_TEXTURE_KEY_COUNT 4U


// Types.
typedef struct WorldModelLodDefinitionStruct
{
    unsigned char* Model; // owned model asset name
    float MaxDistance;
} WorldModelLodDefinition;

typedef struct WorldModelOverrideDefinitionStruct
{
    int32_t Slot;
    unsigned char* Albedo;   // owned texture asset names, NULL when the override keeps the model's map
    unsigned char* Normal;
    unsigned char* Mra;
    unsigned char* Emissive;
    GameMaterialFactors Factors;
} WorldModelOverrideDefinition;

typedef struct WorldModelVariantDefinitionStruct
{
    unsigned char* Name;                     // owned
    WorldModelOverrideDefinition* Overrides; // owned array
    size_t OverrideCount;
} WorldModelVariantDefinition;

typedef struct WorldModelDefinitionStruct
{
    AssetDefinition Base;
    WorldModelLodDefinition Lods[WORLD_MODEL_MAX_LODS];
    size_t LodCount;
    WorldModelSettings Settings;
    float LodFadeSeconds;
    float LodHysteresis;
    bool HasBounds;
    Vector3 BoundsCenter;
    float BoundsRadius;
    WorldModelVariantDefinition* Variants; // owned array
    size_t VariantCount;
    WorldModelCollisionDefinition* Collision; // owned, NULL without a "collision" section
    unsigned char* AnimationLibraries[WORLD_MODEL_MAX_ANIMATION_LIBRARIES]; // owned asset names
    size_t AnimationLibraryCount;
    unsigned char* AnimationGraph;       // owned asset name, NULL without one
    WorldModelSocketDefinition* Sockets; // owned, NULL without "sockets" and "first_person"
} WorldModelDefinition;

/* The definition being read and its name in printable form, for messages. */
typedef struct ParseContextStruct
{
    WorldModelDefinition* Definition;
    char Name[NAME_PRINT_CAPACITY];
} ParseContext;

typedef struct WorldModelLoadedStruct
{
    WorldModel Model;     // Asset points here
    void* VariantBlock;   // owned: the variants, their overrides and their names in one block (NULL without variants)
    void* CollisionBlock; // owned: the collision sets, shapes, hulls and meshes (NULL without collision)
    void* LibraryBlock;   // owned: the animation libraries' names in one block (NULL without libraries)
    void* SocketBlock;    // owned: the sockets, their names and the hidden bones (NULL without them)
    unsigned char* GraphName; // owned copy of the animation graph's name (NULL without one)
} WorldModelLoaded;


// Fields.
static const unsigned char* const RootKeys[] =
{
    u8"name", u8"lods", u8"draw_distance", u8"is_draw_distance_fixed", u8"lod_fade_seconds", u8"lod_hysteresis",
    u8"render", u8"variants", u8"bounds", u8"collision", u8"animation_libraries", u8"animation_graph", u8"sockets",
    u8"first_person"
};
static const unsigned char* const LodKeys[] = { u8"model", u8"max_distance" };
static const unsigned char* const RenderKeys[] =
{
    u8"has_outline", u8"omit_pixelation", u8"shadow_tier", u8"casts_shadows", u8"haze"
};
static const unsigned char* const VariantKeys[] = { u8"name", u8"material_overrides" };
static const unsigned char* const OverrideKeys[] =
{
    u8"slot", u8"albedo", u8"normal", u8"mra", u8"emissive", u8"tint", u8"metallic", u8"roughness", u8"emissive_color",
    u8"emissive_intensity"
};
static const unsigned char* const BoundsKeys[] = { u8"center", u8"radius" };
static const unsigned char* const OverrideTextureKeys[OVERRIDE_TEXTURE_KEY_COUNT] =
{
    u8"albedo", u8"normal", u8"mra", u8"emissive"
};


// Static functions: messages.
/* An InvalidAssetDefinition error: "World model \"<name>\": " and the formatted text. */
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
    return Error_Construct4(ErrorCode_InvalidAssetDefinition, "World model \"%s\": %s", context->Name, Text);
}

/* The inner error with "World model \"<name>\": <where>: " before its message (its code kept); releases it. */
static Error WrapError(const ParseContext* context, const char* where, Error* inner)
{
    Error Wrapped = Error_Construct4(inner->Code, "World model \"%s\": %s: %s", context->Name, where,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Writes "world model \"<name>\"" and the formatted suffix into @p buffer, for AssetJSON_RejectUnknownKeys. */
static const char* FormatWhere(const ParseContext* context, char* buffer, size_t capacity, const char* format, ...)
{
    int Length = snprintf(buffer, capacity, WORLD_MODEL_KIND " \"%s\"", context->Name);
    if ((Length < 0) || ((size_t)Length >= capacity) || (format == NULL))
    {
        return buffer;
    }
    va_list Arguments;
    va_start(Arguments, format);
    vsnprintf(buffer + Length, capacity - (size_t)Length, format, Arguments);
    va_end(Arguments);
    return buffer;
}

/* A name from the file in printable form, for a message (asset names are checked before they are kept). */
static const char* Printable(const unsigned char* text, char* buffer, size_t capacity)
{
    AssetJSON_CopyPrintable(text, buffer, capacity);
    return buffer;
}


// Static functions: reading.
/* Reads a required string key that must be a valid asset name (an owned copy into *outName). */
static Error ReadAssetName(const ParseContext* context, JSONCompound* compound, const unsigned char* key,
    bool isRequired, const char* where, unsigned char** outName)
{
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(compound, key, outName, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "%s.%s must be a string.", where, (const char*)key);
    }
    if (!Found)
    {
        return isRequired ? Fail(context, "%s needs a \"%s\".", where, (const char*)key) : Error_CreateSuccess();
    }
    if (!AssetManager_IsValidAssetName(*outName))
    {
        char Name[NAME_PRINT_CAPACITY];
        return Fail(context, "%s.%s \"%s\" is not a valid asset name ([a-z][a-z0-9_.-]*).", where, (const char*)key,
            Printable(*outName, Name, sizeof(Name)));
    }
    return Error_CreateSuccess();
}

/* Reads an optional float key; a value that is not a finite number is wrapped with @p where. */
static Error ReadFloat(const ParseContext* context, JSONCompound* compound, const unsigned char* key,
    float defaultValue, const char* where, float* outValue, bool* outFound)
{
    Error Result = AssetJSON_ReadOptionalFloat(compound, key, defaultValue, outValue, outFound);
    return (Result.Code == ErrorCode_Success) ? Result : WrapError(context, where, &Result);
}

/* Reads an optional boolean key; a value that is not a boolean is wrapped with @p where. */
static Error ReadBoolean(const ParseContext* context, JSONCompound* compound, const unsigned char* key,
    bool defaultValue, const char* where, bool* outValue)
{
    Error Result = AssetJSON_ReadOptionalBoolean(compound, key, defaultValue, outValue);
    if (Result.Code == ErrorCode_Success)
    {
        return Result;
    }
    Error_Deconstruct(&Result);
    return Fail(context, "%s.%s must be true or false.", where, (const char*)key);
}

/* Gets an optional key that must be a compound (Found false when absent). */
static Error GetOptionalCompound(const ParseContext* context, JSONCompound* compound, const unsigned char* key,
    const char* where, JSONCompound** outCompound)
{
    *outCompound = NULL;
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(compound, key, JSONValueType_Compound, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "%s.%s must be a JSON object.", where, (const char*)key);
    }
    *outCompound = Found ? Value.Value.Compound : NULL;
    return Error_CreateSuccess();
}

/* Gets an optional key that must be an array (NULL when absent). */
static Error GetOptionalArray(const ParseContext* context, JSONCompound* compound, const unsigned char* key,
    const char* where, JSONArray** outArray)
{
    *outArray = NULL;
    JSONObjectValue Value;
    bool Found = false;
    Error Result = JSONCompound_GetOptionalVerified(compound, key, JSONValueType_Array, &Value, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "%s.%s must be an array.", where, (const char*)key);
    }
    *outArray = Found ? Value.Value.Array : NULL;
    return Error_CreateSuccess();
}

/* Gets an array element that must be a compound. */
static Error GetCompoundElement(const ParseContext* context, JSONArray* array, size_t index, const char* where,
    JSONCompound** outCompound)
{
    JSONObjectValue Element;
    Error Result = JSONArray_GetVerified(array, index, JSONValueType_Compound, &Element);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "%s must be a JSON object.", where);
    }
    *outCompound = Element.Value.Compound;
    return Error_CreateSuccess();
}

/* One LOD: { "model": name, "max_distance": metres }, the distances strictly increasing. */
static Error ParseLod(const ParseContext* context, JSONCompound* compound, size_t index)
{
    char Where[WHERE_CAPACITY];
    char Path[PATH_CAPACITY];
    snprintf(Path, sizeof(Path), "lods[%" PRIu64 "]", (uint64_t)index);
    Error Result = AssetJSON_RejectUnknownKeys(compound, LodKeys, KEY_COUNT(LodKeys),
        FormatWhere(context, Where, sizeof(Where), ", %s", Path));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WorldModelLodDefinition* Lod = &context->Definition->Lods[index];
    Result = ReadAssetName(context, compound, (const unsigned char*)u8"model", true, Path, &Lod->Model);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool HasMaxDistance = false;
    Result = ReadFloat(context, compound, (const unsigned char*)u8"max_distance", 0.0f, Path, &Lod->MaxDistance,
        &HasMaxDistance);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasMaxDistance)
    {
        return Fail(context, "%s needs a \"max_distance\": how far, in metres, the LOD is used.", Path);
    }
    if (!(Lod->MaxDistance > 0.0f))
    {
        return Fail(context, "%s.max_distance is %g; it must be above 0.", Path, (double)Lod->MaxDistance);
    }
    if ((index > 0U) && !(Lod->MaxDistance > context->Definition->Lods[index - 1U].MaxDistance))
    {
        return Fail(context, "%s.max_distance (%g) must be above lods[%" PRIu64 "].max_distance (%g): the distances "
            "rise from the finest LOD to the coarsest.", Path, (double)Lod->MaxDistance, (uint64_t)(index - 1U),
            (double)context->Definition->Lods[index - 1U].MaxDistance);
    }
    return Error_CreateSuccess();
}

/* "lods": 1 to WORLD_MODEL_MAX_LODS LODs, finest first. */
static Error ParseLods(const ParseContext* context, JSONCompound* root)
{
    JSONArray* Lods = NULL;
    Error Result = GetOptionalArray(context, root, (const unsigned char*)u8"lods", "the definition", &Lods);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = (Lods != NULL) ? JSONArray_GetElementCount(Lods) : 0U;
    if ((Count == 0U) || (Count > WORLD_MODEL_MAX_LODS))
    {
        return Fail(context, "\"lods\" must hold 1 to %" PRIu64 " LODs (each { \"model\": name, \"max_distance\": "
            "metres }); it holds %" PRIu64 ".", (uint64_t)WORLD_MODEL_MAX_LODS, (uint64_t)Count);
    }

    for (size_t Index = 0U; Index < Count; Index++)
    {
        char Path[PATH_CAPACITY];
        snprintf(Path, sizeof(Path), "lods[%" PRIu64 "]", (uint64_t)Index);
        JSONCompound* Lod = NULL;
        Result = GetCompoundElement(context, Lods, Index, Path, &Lod);
        if (Result.Code == ErrorCode_Success)
        {
            // Counted before it is read, so the definition's teardown frees what a failed read kept.
            context->Definition->LodCount = Index + 1U;
            Result = ParseLod(context, Lod, Index);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* The draw distance (default: the last LOD's max distance, never below it), its fixed flag, the LOD fade time and the
 * hysteresis. */
static Error ParseDistances(const ParseContext* context, JSONCompound* root)
{
    WorldModelDefinition* Definition = context->Definition;
    float LastMaxDistance = Definition->Lods[Definition->LodCount - 1U].MaxDistance;
    bool HasDrawDistance = false;
    Error Result = ReadFloat(context, root, (const unsigned char*)u8"draw_distance", LastMaxDistance, "the definition",
        &Definition->Settings.DrawDistance, &HasDrawDistance);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (HasDrawDistance && !(Definition->Settings.DrawDistance >= LastMaxDistance))
    {
        return Fail(context, "draw_distance (%g) must be at least the last LOD's max_distance (%g), or left out (then "
            "it is that distance).", (double)Definition->Settings.DrawDistance, (double)LastMaxDistance);
    }
    Result = ReadBoolean(context, root, (const unsigned char*)u8"is_draw_distance_fixed", false, "the definition",
        &Definition->Settings.IsDrawDistanceFixed);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ReadFloat(context, root, (const unsigned char*)u8"lod_fade_seconds", WORLD_MODEL_DEFAULT_LOD_FADE_SECONDS,
        "the definition", &Definition->LodFadeSeconds, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!(Definition->LodFadeSeconds >= 0.0f))
    {
        return Fail(context, "lod_fade_seconds is %g; it must be at least 0 (0 swaps LODs at once).",
            (double)Definition->LodFadeSeconds);
    }
    Result = ReadFloat(context, root, (const unsigned char*)u8"lod_hysteresis", WORLD_MODEL_DEFAULT_LOD_HYSTERESIS,
        "the definition", &Definition->LodHysteresis, NULL);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!(Definition->LodHysteresis >= 0.0f) || !(Definition->LodHysteresis < LOD_HYSTERESIS_LIMIT))
    {
        return Fail(context, "lod_hysteresis is %g; it must be at least 0 and below 1 (a share of each switch "
            "distance).", (double)Definition->LodHysteresis);
    }
    return Error_CreateSuccess();
}

/* "shadow_tier": one of WORLD_SHADOW_TIER_ENUM's names. */
static Error ParseShadowTier(const ParseContext* context, JSONCompound* render, WorldShadowTier* outTier)
{
    unsigned char* Text = NULL;
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalOwnedString(render, (const unsigned char*)u8"shadow_tier", &Text, &Found);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(context, "render.shadow_tier must be \"both\", \"near\" or \"far\".");
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    for (size_t Index = 0U; Index < WORLD_SHADOW_TIER_ENUM.EntryCount; Index++)
    {
        bool IsEqual = false;
        Error CompareResult = StringUTF8_EqualsExact(Text, WORLD_SHADOW_TIER_ENUM.Entries[Index].Name, &IsEqual);
        Error_Deconstruct(&CompareResult);
        if (IsEqual)
        {
            *outTier = (WorldShadowTier)WORLD_SHADOW_TIER_ENUM.Entries[Index].Value;
            Memory_Free(Text);
            return Error_CreateSuccess();
        }
    }
    char Name[NAME_PRINT_CAPACITY];
    Result = Fail(context, "render.shadow_tier is \"%s\"; it must be \"both\", \"near\" or \"far\".",
        Printable(Text, Name, sizeof(Name)));
    Memory_Free(Text);
    return Result;
}

/* "render": the default render settings of the placements (outline, crisp, shadow tier, casts shadows, haze). */
static Error ParseRender(const ParseContext* context, JSONCompound* root)
{
    JSONCompound* Render = NULL;
    Error Result = GetOptionalCompound(context, root, (const unsigned char*)u8"render", "the definition", &Render);
    if ((Result.Code != ErrorCode_Success) || (Render == NULL))
    {
        return Result;
    }
    char Where[WHERE_CAPACITY];
    Result = AssetJSON_RejectUnknownKeys(Render, RenderKeys, KEY_COUNT(RenderKeys),
        FormatWhere(context, Where, sizeof(Where), ", render"));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WorldModelSettings* Settings = &context->Definition->Settings;
    Result = ReadBoolean(context, Render, (const unsigned char*)u8"has_outline", Settings->HasOutline, "render",
        &Settings->HasOutline);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(context, Render, (const unsigned char*)u8"omit_pixelation", Settings->OmitsPixelation,
            "render", &Settings->OmitsPixelation);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(context, Render, (const unsigned char*)u8"casts_shadows", Settings->CastsShadows, "render",
            &Settings->CastsShadows);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseShadowTier(context, Render, &Settings->ShadowTier);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadFloat(context, Render, (const unsigned char*)u8"haze", Settings->Haze, "render", &Settings->Haze,
            NULL);
    }
    if ((Result.Code == ErrorCode_Success) && !((Settings->Haze >= 0.0f) && (Settings->Haze <= WORLD_MODEL_MAX_HAZE)))
    {
        Result = Fail(context, "render.haze is %g; it must be at least 0 and at most %g (1 the real air's haze, 0 none).",
            (double)Settings->Haze, (double)WORLD_MODEL_MAX_HAZE);
    }
    return Result;
}

/* One material override of a variant: its slot (unique within the variant), texture asset names and factors. */
static Error ParseOverride(const ParseContext* context, JSONCompound* compound, const char* path,
    const WorldModelVariantDefinition* variant, size_t index)
{
    char Where[WHERE_CAPACITY];
    Error Result = AssetJSON_RejectUnknownKeys(compound, OverrideKeys, KEY_COUNT(OverrideKeys),
        FormatWhere(context, Where, sizeof(Where), ", %s", path));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WorldModelOverrideDefinition* Override = &variant->Overrides[index];
    Result = AssetJSON_ReadMaterialSlot(compound, &Override->Slot);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(context, path, &Result);
    }
    for (size_t Earlier = 0U; Earlier < index; Earlier++)
    {
        if (variant->Overrides[Earlier].Slot == Override->Slot)
        {
            return Fail(context, "%s overrides material slot %" PRId32 " a second time; give each slot one override.",
                path, Override->Slot);
        }
    }

    unsigned char** const TextureNames[OVERRIDE_TEXTURE_KEY_COUNT] =
    {
        &Override->Albedo, &Override->Normal, &Override->Mra, &Override->Emissive
    };
    for (size_t Key = 0U; Key < OVERRIDE_TEXTURE_KEY_COUNT; Key++)
    {
        Result = ReadAssetName(context, compound, OverrideTextureKeys[Key], false, path, TextureNames[Key]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Result = AssetJSON_ReadMaterialFactors(compound, &Override->Factors);
    return (Result.Code == ErrorCode_Success) ? Result : WrapError(context, path, &Result);
}

/* A variant's name: a valid asset name of at most WORLD_MODEL_VARIANT_NAME_MAX_LENGTH bytes, unique in the world
 * model. */
static Error ParseVariantName(const ParseContext* context, JSONCompound* compound, const char* path, size_t index)
{
    WorldModelVariantDefinition* Variant = &context->Definition->Variants[index];
    Error Result = ReadAssetName(context, compound, (const unsigned char*)u8"name", true, path, &Variant->Name);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (StringUTF8_GetByteLength(Variant->Name) > WORLD_MODEL_VARIANT_NAME_MAX_LENGTH)
    {
        return Fail(context, "%s.name is longer than %" PRIu64 " bytes.", path,
            (uint64_t)WORLD_MODEL_VARIANT_NAME_MAX_LENGTH);
    }
    for (size_t Earlier = 0U; Earlier < index; Earlier++)
    {
        bool IsEqual = false;
        Error CompareResult = StringUTF8_EqualsExact(context->Definition->Variants[Earlier].Name, Variant->Name,
            &IsEqual);
        Error_Deconstruct(&CompareResult);
        if (IsEqual)
        {
            return Fail(context, "%s.name \"%s\" is used by variants[%" PRIu64 "] too; variant names must be unique.",
                path, (const char*)Variant->Name, (uint64_t)Earlier);
        }
    }
    return Error_CreateSuccess();
}

/* One variant: { "name": name, "material_overrides": [ ... ] } (no overrides: the model's own look under a name). */
static Error ParseVariant(const ParseContext* context, JSONCompound* compound, size_t index)
{
    char Where[WHERE_CAPACITY];
    char Path[PATH_CAPACITY];
    snprintf(Path, sizeof(Path), "variants[%" PRIu64 "]", (uint64_t)index);
    Error Result = AssetJSON_RejectUnknownKeys(compound, VariantKeys, KEY_COUNT(VariantKeys),
        FormatWhere(context, Where, sizeof(Where), ", %s", Path));
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseVariantName(context, compound, Path, index);
    }
    JSONArray* Overrides = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptionalArray(context, compound, (const unsigned char*)u8"material_overrides", Path, &Overrides);
    }
    if ((Result.Code != ErrorCode_Success) || (Overrides == NULL))
    {
        return Result;
    }

    size_t Count = JSONArray_GetElementCount(Overrides);
    if (Count > WORLD_MODEL_MAX_VARIANT_OVERRIDES)
    {
        return Fail(context, "%s has %" PRIu64 " material overrides; at most %" PRIu64 " are allowed.", Path,
            (uint64_t)Count, (uint64_t)WORLD_MODEL_MAX_VARIANT_OVERRIDES);
    }
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }
    WorldModelVariantDefinition* Variant = &context->Definition->Variants[index];
    Variant->Overrides = Memory_Allocate(Count * sizeof(WorldModelOverrideDefinition));
    Memory_Zero(Variant->Overrides, Count * sizeof(WorldModelOverrideDefinition));
    Variant->OverrideCount = Count;
    for (size_t Element = 0U; (Element < Count) && (Result.Code == ErrorCode_Success); Element++)
    {
        char ElementPath[ELEMENT_PATH_CAPACITY];
        snprintf(ElementPath, sizeof(ElementPath), "%s.material_overrides[%" PRIu64 "]", Path, (uint64_t)Element);
        JSONCompound* Override = NULL;
        Result = GetCompoundElement(context, Overrides, Element, ElementPath, &Override);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseOverride(context, Override, ElementPath, Variant, Element);
        }
    }
    return Result;
}

/* "variants": at most WORLD_MODEL_MAX_VARIANTS named looks. */
static Error ParseVariants(const ParseContext* context, JSONCompound* root)
{
    JSONArray* Variants = NULL;
    Error Result = GetOptionalArray(context, root, (const unsigned char*)u8"variants", "the definition", &Variants);
    if ((Result.Code != ErrorCode_Success) || (Variants == NULL))
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Variants);
    if (Count > WORLD_MODEL_MAX_VARIANTS)
    {
        return Fail(context, "\"variants\" holds %" PRIu64 " variants; at most %" PRIu64 " are allowed.",
            (uint64_t)Count, (uint64_t)WORLD_MODEL_MAX_VARIANTS);
    }
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }

    WorldModelDefinition* Definition = context->Definition;
    Definition->Variants = Memory_Allocate(Count * sizeof(WorldModelVariantDefinition));
    Memory_Zero(Definition->Variants, Count * sizeof(WorldModelVariantDefinition));
    Definition->VariantCount = Count;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        char Path[PATH_CAPACITY];
        snprintf(Path, sizeof(Path), "variants[%" PRIu64 "]", (uint64_t)Index);
        JSONCompound* Variant = NULL;
        Result = GetCompoundElement(context, Variants, Index, Path, &Variant);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ParseVariant(context, Variant, Index);
        }
    }
    return Result;
}

/* "bounds": { "center": [x, y, z], "radius": metres }, both required when it is given. */
static Error ParseBounds(const ParseContext* context, JSONCompound* root)
{
    JSONCompound* Bounds = NULL;
    Error Result = GetOptionalCompound(context, root, (const unsigned char*)u8"bounds", "the definition", &Bounds);
    if ((Result.Code != ErrorCode_Success) || (Bounds == NULL))
    {
        return Result;
    }
    char Where[WHERE_CAPACITY];
    Result = AssetJSON_RejectUnknownKeys(Bounds, BoundsKeys, KEY_COUNT(BoundsKeys),
        FormatWhere(context, Where, sizeof(Where), ", bounds"));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WorldModelDefinition* Definition = context->Definition;
    JSONObjectValue Center;
    bool HasCenter = false;
    Result = JSONCompound_GetOptional(Bounds, (const unsigned char*)u8"center", &Center, &HasCenter);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasCenter)
    {
        return Fail(context, "bounds needs a \"center\": [x, y, z] in the object's own space.");
    }
    Result = GameJSON_ParseVector3(&Center, &Definition->BoundsCenter);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(context, "bounds.center", &Result);
    }
    bool HasRadius = false;
    Result = ReadFloat(context, Bounds, (const unsigned char*)u8"radius", 0.0f, "bounds", &Definition->BoundsRadius,
        &HasRadius);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasRadius || !(Definition->BoundsRadius >= 0.0f))
    {
        return Fail(context, "bounds needs a \"radius\" of at least 0 metres.");
    }
    Definition->HasBounds = true;
    return Error_CreateSuccess();
}

/* "animation_libraries": up to WORLD_MODEL_MAX_ANIMATION_LIBRARIES animation library asset names, each once. */
static Error ParseAnimationLibraries(const ParseContext* context, JSONCompound* root)
{
    JSONArray* Libraries = NULL;
    Error Result = GetOptionalArray(context, root, (const unsigned char*)u8"animation_libraries", "the definition",
        &Libraries);
    if ((Result.Code != ErrorCode_Success) || (Libraries == NULL))
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Libraries);
    if (Count > WORLD_MODEL_MAX_ANIMATION_LIBRARIES)
    {
        return Fail(context, "\"animation_libraries\" names %" PRIu64 " libraries; at most %" PRIu64 " are allowed.",
            (uint64_t)Count, (uint64_t)WORLD_MODEL_MAX_ANIMATION_LIBRARIES);
    }
    WorldModelDefinition* Definition = context->Definition;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Element;
        unsigned char* Name = NULL;
        Result = JSONArray_GetVerified(Libraries, Index, JSONValueType_String, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = AssetJSON_ValueToOwnedString(&Element, &Name);
        }
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(context, "animation_libraries[%" PRIu64 "] must be the name of an animation library.",
                (uint64_t)Index);
        }
        // Kept before it is checked, so the definition's teardown frees it.
        Definition->AnimationLibraries[Index] = Name;
        Definition->AnimationLibraryCount = Index + 1U;
        char PrintableName[NAME_PRINT_CAPACITY];
        if (!AssetManager_IsValidAssetName(Name))
        {
            return Fail(context, "animation_libraries[%" PRIu64 "] \"%s\" is not a valid asset name ([a-z][a-z0-9_.-]*).",
                (uint64_t)Index, Printable(Name, PrintableName, sizeof(PrintableName)));
        }
        for (size_t Earlier = 0U; Earlier < Index; Earlier++)
        {
            if (StringUTF8_GetByteLength(Definition->AnimationLibraries[Earlier]) == StringUTF8_GetByteLength(Name)
                && Memory_IsEqual(Definition->AnimationLibraries[Earlier], Name, StringUTF8_GetByteLength(Name)))
            {
                return Fail(context, "animation_libraries names \"%s\" twice.",
                    Printable(Name, PrintableName, sizeof(PrintableName)));
            }
        }
    }
    return Error_CreateSuccess();
}

/* Reads everything after the name, in the reference's order. */
static Error ParseDefinition(WorldModelDefinition* definition, JSONCompound* root)
{
    ParseContext Context;
    Context.Definition = definition;
    AssetJSON_CopyPrintable(definition->Base.Name, Context.Name, sizeof(Context.Name));
    definition->Settings = WorldModelSettings_CreateDefault();
    definition->LodFadeSeconds = WORLD_MODEL_DEFAULT_LOD_FADE_SECONDS;
    definition->LodHysteresis = WORLD_MODEL_DEFAULT_LOD_HYSTERESIS;

    char Where[WHERE_CAPACITY];
    Error Result = AssetJSON_RejectUnknownKeys(root, RootKeys, KEY_COUNT(RootKeys),
        FormatWhere(&Context, Where, sizeof(Where), NULL));
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseLods(&Context, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDistances(&Context, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseRender(&Context, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseVariants(&Context, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseBounds(&Context, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldModelCollisionDefinition_Parse(Context.Name, root, &definition->Collision);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseAnimationLibraries(&Context, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadAssetName(&Context, root, (const unsigned char*)u8"animation_graph", false, "the definition",
            &definition->AnimationGraph);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldModelSocketDefinition_Parse(Context.Name, root, &definition->Sockets);
    }
    return Result;
}


// Static functions: loading.
static void WorldModelLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    WorldModelLoaded* Loaded = self->DestroyContext;
    Memory_Free(Loaded->VariantBlock);
    WorldModelCollision_DestroyBlock(Loaded->CollisionBlock);
    Memory_Free(Loaded->LibraryBlock);
    Memory_Free(Loaded->SocketBlock);
    Memory_Free(Loaded->GraphName);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable WorldModelLoadedVTable = { WorldModelLoaded_Destroy };

/* A standard type's id by name; InvalidState when it is not registered. */
static Error GetTypeId(AssetManager* manager, const unsigned char* name, AssetTypeID* outType)
{
    Error Result = AssetManager_GetAssetTypeByName(manager, name, outType);
    if ((Result.Code == ErrorCode_Success) && (*outType == ASSET_TYPE_ID_INVALID))
    {
        return Error_Construct3(ErrorCode_InvalidState, u8"The %s asset type is not registered.", name);
    }
    return Result;
}

/* Loads the LOD models as dependencies. A failure names the LOD; the manager releases what loaded before it. */
static Error LoadLods(const WorldModelDefinition* definition, AssetManager* manager, AssetUserID dependencyUser,
    WorldModel* model)
{
    AssetTypeID ModelType = ASSET_TYPE_ID_INVALID;
    Error Result = GetTypeId(manager, ASSET_TYPE_NAME_MODEL, &ModelType);
    for (size_t Index = 0U; (Index < definition->LodCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        void* Asset = NULL;
        Result = AssetManager_LoadAssetSingle(manager, ModelType, definition->Lods[Index].Model, dependencyUser,
            &Asset);
        if (Result.Code != ErrorCode_Success)
        {
            Error Wrapped = Error_Construct3(Result.Code, u8"World model \"%s\": LOD %" PRIu64 u8" (model \"%s\"): %s",
                definition->Base.Name, (uint64_t)Index, definition->Lods[Index].Model,
                (Result.Message != NULL) ? Result.Message : (const unsigned char*)u8"it did not load.");
            Error_Deconstruct(&Result);
            return Wrapped;
        }
        model->_lods[Index]._model = Asset;
        model->_lods[Index]._maxDistance = definition->Lods[Index].MaxDistance;
    }
    model->_lodCount = definition->LodCount;
    return Result;
}

/* The skeleton: the first LOD whose model has one; every other LOD with one must have the same bones bound the same
 * way, so one pose's palette skins them all. */
static Error FindSkeleton(const WorldModelDefinition* definition, WorldModel* model)
{
    model->_skeleton = NULL;
    model->_skeletonLod = 0U;
    for (size_t Index = 0U; Index < model->_lodCount; Index++)
    {
        const GameSkeleton* Skeleton = GameModel_GetSkeleton(model->_lods[Index]._model);
        if (Skeleton == NULL)
        {
            continue;
        }
        if (model->_skeleton == NULL)
        {
            model->_skeleton = Skeleton;
            model->_skeletonLod = Index;
            continue;
        }
        if (!GameSkeleton_HasSameBindPoses(model->_skeleton, Skeleton))
        {
            return Error_Construct3(ErrorCode_InvalidAssetData, u8"World model \"%s\": LOD %" PRIu64 u8" (model \"%s\") "
                u8"has another skeleton than LOD %" PRIu64 u8" (model \"%s\"): skinned LODs share one skeleton, the same "
                u8"bones bound the same way.", definition->Base.Name, (uint64_t)Index, definition->Lods[Index].Model,
                (uint64_t)model->_skeletonLod, definition->Lods[model->_skeletonLod].Model);
        }
    }
    return Error_CreateSuccess();
}

/* Copies the animation libraries' names into one block (*outBlock, owned by the caller) the world model points into. */
static void CopyAnimationLibraryNames(const WorldModelDefinition* definition, WorldModel* model, void** outBlock)
{
    // At most WORLD_MODEL_MAX_ANIMATION_LIBRARIES names of at most an asset name's length: no overflow.
    size_t ByteCount = 0U;
    for (size_t Index = 0U; Index < definition->AnimationLibraryCount; Index++)
    {
        ByteCount += StringUTF8_GetByteLength(definition->AnimationLibraries[Index]) + 1U;
    }
    unsigned char* Block = Memory_Allocate(ByteCount);
    unsigned char* Cursor = Block;
    for (size_t Index = 0U; Index < definition->AnimationLibraryCount; Index++)
    {
        size_t Length = StringUTF8_GetByteLength(definition->AnimationLibraries[Index]) + 1U;
        Memory_Copy(definition->AnimationLibraries[Index], Cursor, Length);
        model->_animationLibraryNames[Index] = Cursor;
        Cursor += Length;
    }
    *outBlock = Block;
}

/* Loads the animation libraries as dependencies and checks that no clip name repeats across them; on success
 * *outNameBlock holds their names (owned by the caller; NULL without libraries). */
static Error LoadAnimationLibraries(const WorldModelDefinition* definition, AssetManager* manager,
    AssetUserID dependencyUser, WorldModel* model, void** outNameBlock)
{
    *outNameBlock = NULL;
    model->_animationLibraryCount = 0U;
    if (definition->AnimationLibraryCount == 0U)
    {
        return Error_CreateSuccess();
    }
    AssetTypeID LibraryType = ASSET_TYPE_ID_INVALID;
    Error Result = GetTypeId(manager, ASSET_TYPE_NAME_ANIMATION_LIBRARY, &LibraryType);
    for (size_t Index = 0U; (Index < definition->AnimationLibraryCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        void* Asset = NULL;
        Result = AssetManager_LoadAssetSingle(manager, LibraryType, definition->AnimationLibraries[Index], dependencyUser,
            &Asset);
        if (Result.Code != ErrorCode_Success)
        {
            Error Wrapped = Error_Construct3(Result.Code, u8"World model \"%s\": animation library \"%s\": %s",
                definition->Base.Name, definition->AnimationLibraries[Index],
                (Result.Message != NULL) ? Result.Message : (const unsigned char*)u8"it did not load.");
            Error_Deconstruct(&Result);
            return Wrapped;
        }
        model->_animationLibraries[Index] = Asset;
        model->_animationLibraryCount = Index + 1U;
    }
    for (size_t First = 0U; (First < model->_animationLibraryCount) && (Result.Code == ErrorCode_Success); First++)
    {
        const AnimationLibrary* Library = model->_animationLibraries[First];
        for (uint32_t Clip = 0U; Clip < AnimationLibrary_GetClipCount(Library); Clip++)
        {
            const unsigned char* Name = AnimationClip_GetName(AnimationLibrary_GetClip(Library, Clip));
            for (size_t Second = First + 1U; Second < model->_animationLibraryCount; Second++)
            {
                if (AnimationLibrary_TryFindClip(model->_animationLibraries[Second], Name, NULL))
                {
                    return Error_Construct3(ErrorCode_InvalidAssetData, u8"World model \"%s\": clip \"%s\" is in both "
                        u8"animation library \"%s\" and \"%s\"; a world model's clips need names of their own.",
                        definition->Base.Name, Name, definition->AnimationLibraries[First],
                        definition->AnimationLibraries[Second]);
                }
            }
        }
    }
    if ((Result.Code == ErrorCode_Success) && (model->_skeleton == NULL) && (AssetManager_GetLogger(manager) != NULL))
    {
        Error LogResult = Logger_LogWarningFormatted(AssetManager_GetLogger(manager), (const unsigned char*)u8"World "
            u8"model \"%s\" names animation libraries, but none of its LOD models has a skeleton: its placements play "
            u8"nothing.", (const char*)definition->Base.Name);
        Error_Deconstruct(&LogResult);
    }
    if (Result.Code == ErrorCode_Success)
    {
        CopyAnimationLibraryNames(definition, model, outNameBlock);
    }
    return Result;
}

/* Loads the animation graph as a dependency; on success *outName holds a copy of its name (owned by the caller;
 * NULL without a graph). */
static Error LoadAnimationGraph(const WorldModelDefinition* definition, AssetManager* manager, AssetUserID dependencyUser,
    WorldModel* model, unsigned char** outName)
{
    *outName = NULL;
    model->_animationGraph = NULL;
    model->_animationGraphName = NULL;
    if (definition->AnimationGraph == NULL)
    {
        return Error_CreateSuccess();
    }
    AssetTypeID GraphType = ASSET_TYPE_ID_INVALID;
    Error Result = GetTypeId(manager, ASSET_TYPE_NAME_ANIMATION_GRAPH, &GraphType);
    void* Asset = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetManager_LoadAssetSingle(manager, GraphType, definition->AnimationGraph, dependencyUser, &Asset);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Wrapped = Error_Construct3(Result.Code, u8"World model \"%s\": animation graph \"%s\": %s",
            definition->Base.Name, definition->AnimationGraph,
            (Result.Message != NULL) ? Result.Message : (const unsigned char*)u8"it did not load.");
        Error_Deconstruct(&Result);
        return Wrapped;
    }
    if ((model->_skeleton == NULL) && (AssetManager_GetLogger(manager) != NULL))
    {
        Error LogResult = Logger_LogWarningFormatted(AssetManager_GetLogger(manager), (const unsigned char*)u8"World "
            u8"model \"%s\" names an animation graph, but none of its LOD models has a skeleton: its placements play "
            u8"nothing.", (const char*)definition->Base.Name);
        Error_Deconstruct(&LogResult);
    }
    size_t Bytes = StringUTF8_GetByteLength(definition->AnimationGraph) + 1U;
    *outName = Memory_Allocate(Bytes);
    Memory_Copy(definition->AnimationGraph, *outName, Bytes);
    model->_animationGraph = Asset;
    model->_animationGraphName = *outName;
    return Error_CreateSuccess();
}

/* The bounding sphere: the definition's, in the object's own space, or the sphere around LOD 0's mesh bounds in its
 * mesh space with LOD 0's import transform to take it to the object's (the user composes that with the world matrix
 * before taking the stretch, so an uneven import scale and an uneven placement scale do not multiply up). */
static void SetBounds(const WorldModelDefinition* definition, WorldModel* model)
{
    model->_hasOwnBounds = definition->HasBounds;
    if (definition->HasBounds)
    {
        model->_boundsCenter = definition->BoundsCenter;
        model->_boundsRadius = definition->BoundsRadius;
        model->_boundsTransform = MatrixIdentity();
        return;
    }
    GameModel* Lod = model->_lods[0]._model;
    BoundingBox Bounds = GameModel_GetLocalBounds(Lod);
    Vector3 Half = Vector3Scale(Vector3Subtract(Bounds.max, Bounds.min), 0.5f);
    model->_boundsCenter = Vector3Scale(Vector3Add(Bounds.min, Bounds.max), 0.5f);
    model->_boundsRadius = Vector3Length(Half);
    model->_boundsTransform = GameModel_GetRaylibModel(Lod).transform;
}

/* The size of the block that holds the variants, their overrides and their names (each name with its terminator). */
static bool TryGetVariantBlockSize(const WorldModelDefinition* definition, size_t* outOverrideCount,
    size_t* outByteCount)
{
    size_t OverrideCount = 0U;
    size_t NameBytes = 0U;
    for (size_t Index = 0U; Index < definition->VariantCount; Index++)
    {
        const WorldModelVariantDefinition* Variant = &definition->Variants[Index];
        if (!Memory_TryAddSize(OverrideCount, Variant->OverrideCount, &OverrideCount)
            || !Memory_TryAddSize(NameBytes, StringUTF8_GetByteLength(Variant->Name) + 1U, &NameBytes))
        {
            return false;
        }
    }
    size_t VariantBytes = 0U;
    size_t OverrideBytes = 0U;
    size_t Total = 0U;
    bool IsSized = Memory_TryMultiplySize(definition->VariantCount, sizeof(WorldModelVariant), &VariantBytes)
        && Memory_TryMultiplySize(OverrideCount, sizeof(WorldModelMaterialOverride), &OverrideBytes)
        && Memory_TryAddSize(VariantBytes, OverrideBytes, &Total)
        && Memory_TryAddSize(Total, NameBytes, &Total);
    *outOverrideCount = OverrideCount;
    *outByteCount = Total;
    return IsSized;
}

/* Loads one texture a variant names as a dependency (NULL name: the override keeps the model's map). */
static Error LoadVariantTexture(AssetManager* manager, AssetTypeID textureType, AssetUserID dependencyUser,
    const WorldModelDefinition* definition, const WorldModelVariantDefinition* variant, int32_t slot,
    const unsigned char* name, const GameTexture** outTexture)
{
    *outTexture = NULL;
    if (name == NULL)
    {
        return Error_CreateSuccess();
    }
    void* Asset = NULL;
    Error Result = AssetManager_LoadAssetSingle(manager, textureType, name, dependencyUser, &Asset);
    if (Result.Code != ErrorCode_Success)
    {
        Error Wrapped = Error_Construct3(Result.Code,
            u8"World model \"%s\": variant \"%s\", slot %" PRId32 u8": texture \"%s\": %s", definition->Base.Name,
            variant->Name, slot, name,
            (Result.Message != NULL) ? Result.Message : (const unsigned char*)u8"it did not load.");
        Error_Deconstruct(&Result);
        return Wrapped;
    }
    *outTexture = Asset;
    return Error_CreateSuccess();
}

/* Fills one override of the loaded asset: the slot, the textures (loaded as dependencies) and the factors. */
static Error LoadOverride(AssetManager* manager, AssetTypeID textureType, AssetUserID dependencyUser,
    const WorldModelDefinition* definition, const WorldModelVariantDefinition* variant,
    const WorldModelOverrideDefinition* source, WorldModelMaterialOverride* target)
{
    target->_slot = source->Slot;
    target->_factors = source->Factors;
    Error Result = LoadVariantTexture(manager, textureType, dependencyUser, definition, variant, source->Slot,
        source->Albedo, &target->_albedo);
    if (Result.Code == ErrorCode_Success)
    {
        Result = LoadVariantTexture(manager, textureType, dependencyUser, definition, variant, source->Slot,
            source->Normal, &target->_normal);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = LoadVariantTexture(manager, textureType, dependencyUser, definition, variant, source->Slot,
            source->Mra, &target->_mra);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = LoadVariantTexture(manager, textureType, dependencyUser, definition, variant, source->Slot,
            source->Emissive, &target->_emissive);
    }
    return Result;
}

/* Builds the variants into one block (*outBlock, NULL without variants), loading their textures as dependencies. On
 * failure the block is freed (the manager releases the textures that loaded). */
static Error LoadVariants(const WorldModelDefinition* definition, AssetManager* manager, AssetUserID dependencyUser,
    WorldModel* model, void** outBlock)
{
    *outBlock = NULL;
    if (definition->VariantCount == 0U)
    {
        return Error_CreateSuccess();
    }
    AssetTypeID TextureType = ASSET_TYPE_ID_INVALID;
    Error Result = GetTypeId(manager, ASSET_TYPE_NAME_TEXTURE, &TextureType);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t OverrideCount = 0U;
    size_t ByteCount = 0U;
    if (!TryGetVariantBlockSize(definition, &OverrideCount, &ByteCount))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"World model \"%s\": its variants are too large.",
            definition->Base.Name);
    }

    unsigned char* Block = Memory_Allocate(ByteCount);
    Memory_Zero(Block, ByteCount);
    WorldModelVariant* Variants = (WorldModelVariant*)(void*)Block;
    WorldModelMaterialOverride* Overrides =
        (WorldModelMaterialOverride*)(void*)(Block + (definition->VariantCount * sizeof(WorldModelVariant)));
    unsigned char* Names = (unsigned char*)(Overrides + OverrideCount);
    for (size_t Index = 0U; (Index < definition->VariantCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        const WorldModelVariantDefinition* Source = &definition->Variants[Index];
        size_t NameBytes = StringUTF8_GetByteLength(Source->Name) + 1U;
        Memory_Copy(Source->Name, Names, NameBytes);
        Variants[Index]._name = Names;
        Variants[Index]._overrides = Overrides;
        Variants[Index]._overrideCount = Source->OverrideCount;
        for (size_t Override = 0U; (Override < Source->OverrideCount) && (Result.Code == ErrorCode_Success); Override++)
        {
            Result = LoadOverride(manager, TextureType, dependencyUser, definition, Source,
                &Source->Overrides[Override], &Overrides[Override]);
        }
        Names += NameBytes;
        Overrides += Source->OverrideCount;
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Block);
        return Result;
    }
    model->_variants = Variants;
    model->_variantCount = definition->VariantCount;
    *outBlock = Block;
    return Error_CreateSuccess();
}

/* Logs (once, when the world model loads) every variant override whose slot no LOD model has: it changes nothing. */
static void WarnAboutUnusedSlots(AssetManager* manager, const WorldModel* model, const unsigned char* name)
{
    Logger* Log = AssetManager_GetLogger(manager);
    if (Log == NULL)
    {
        return;
    }
    int MostMaterials = 0;
    for (size_t Lod = 0U; Lod < model->_lodCount; Lod++)
    {
        int Count = GameModel_GetRaylibModel(model->_lods[Lod]._model).materialCount;
        MostMaterials = (Count > MostMaterials) ? Count : MostMaterials;
    }
    for (size_t Variant = 0U; Variant < model->_variantCount; Variant++)
    {
        for (size_t Override = 0U; Override < model->_variants[Variant]._overrideCount; Override++)
        {
            int32_t Slot = model->_variants[Variant]._overrides[Override]._slot;
            if (Slot < MostMaterials)
            {
                continue;
            }
            Error LogResult = Logger_LogWarningFormatted(Log, (const unsigned char*)u8"World model \"%s\": variant "
                u8"\"%s\" overrides material slot %" PRId32 u8", but its LOD models have %d material(s); the override "
                u8"changes nothing.", (const char*)name, (const char*)model->_variants[Variant]._name, Slot,
                MostMaterials);
            Error_Deconstruct(&LogResult);
        }
    }
}

static Error WorldModelDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    WorldModelDefinition* Definition = self;
    WorldModel Built;
    Memory_Zero(&Built, sizeof(Built));

    // Every failure frees what this call allocated before it returns (the name and variant blocks when a later step
    // fails); the manager releases the dependency user's holds when the load fails.
    Error Result = LoadLods(Definition, manager, dependencyUser, &Built);
    if (Result.Code == ErrorCode_Success)
    {
        Result = FindSkeleton(Definition, &Built);
    }
    void* LibraryBlock = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = LoadAnimationLibraries(Definition, manager, dependencyUser, &Built, &LibraryBlock);
    }
    unsigned char* GraphName = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = LoadAnimationGraph(Definition, manager, dependencyUser, &Built, &GraphName);
    }
    void* SocketBlock = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldModelSocketDefinition_Load(Definition->Sockets, Definition->Base.Name, &Built, &SocketBlock);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(LibraryBlock);
        Memory_Free(GraphName);
        return Result;
    }
    void* VariantBlock = NULL;
    Result = LoadVariants(Definition, manager, dependencyUser, &Built, &VariantBlock);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(SocketBlock);
        Memory_Free(GraphName);
        Memory_Free(LibraryBlock);
        return Result;
    }
    void* CollisionBlock = NULL;
    if (Definition->Collision != NULL)
    {
        Result = WorldModelCollisionDefinition_Load(Definition->Collision, Definition->Base.Name, manager, dependencyUser,
            Built._lods[0]._model, &CollisionBlock, &Built._collision);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(VariantBlock);
            Memory_Free(SocketBlock);
            Memory_Free(GraphName);
            Memory_Free(LibraryBlock);
            return Result;
        }
    }
    Built._settings = Definition->Settings;
    Built._lodFadeSeconds = Definition->LodFadeSeconds;
    Built._lodHysteresis = Definition->LodHysteresis;
    SetBounds(Definition, &Built);
    WarnAboutUnusedSlots(manager, &Built, Definition->Base.Name);

    WorldModelLoaded* Loaded = Memory_Allocate(sizeof(WorldModelLoaded));
    Loaded->Model = Built;
    Loaded->VariantBlock = VariantBlock;
    Loaded->CollisionBlock = CollisionBlock;
    Loaded->LibraryBlock = LibraryBlock;
    Loaded->SocketBlock = SocketBlock;
    Loaded->GraphName = GraphName;
    outLoaded->Asset = &Loaded->Model;
    outLoaded->VTable = &WorldModelLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void WorldModelDefinition_Destroy(void* self)
{
    WorldModelDefinition* Definition = self;
    for (size_t Index = 0U; Index < Definition->LodCount; Index++)
    {
        Memory_Free(Definition->Lods[Index].Model);
    }
    for (size_t Index = 0U; Index < Definition->VariantCount; Index++)
    {
        WorldModelVariantDefinition* Variant = &Definition->Variants[Index];
        for (size_t Override = 0U; Override < Variant->OverrideCount; Override++)
        {
            Memory_Free(Variant->Overrides[Override].Albedo);
            Memory_Free(Variant->Overrides[Override].Normal);
            Memory_Free(Variant->Overrides[Override].Mra);
            Memory_Free(Variant->Overrides[Override].Emissive);
        }
        Memory_Free(Variant->Overrides);
        Memory_Free(Variant->Name);
    }
    Memory_Free(Definition->Variants);
    WorldModelCollisionDefinition_Destroy(Definition->Collision);
    for (size_t Index = 0U; Index < Definition->AnimationLibraryCount; Index++)
    {
        Memory_Free(Definition->AnimationLibraries[Index]);
    }
    Memory_Free(Definition->AnimationGraph);
    WorldModelSocketDefinition_Destroy(Definition->Sockets);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable WorldModelDefinitionVTable =
{
    WorldModelDefinition_LoadAsset, WorldModelDefinition_Destroy
};


// Public functions.
Error WorldModelDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    WorldModelDefinition* Definition = Memory_Allocate(sizeof(WorldModelDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &WorldModelDefinitionVTable;

    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDefinition(Definition, RootCompound);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        WorldModelDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
