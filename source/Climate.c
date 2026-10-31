#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "Climate.h"
#include "AssetDefinitionCommon.h"
#include "AssetManager.h"
#include "GameJSON.h"
#include "WorldEnvironment.h"
#include "NamespacedKey.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The climate asset's data and its reader (references/asset_structure.md, "Climates"). The reader treats a definition
 * as hostile: every value is checked against its rule while it is read, unknown keys are refused everywhere, and every
 * message names the path to the bad value. Presets are read in two passes, their names first, so a link may name a
 * preset that comes later in the file.
 *
 * The climate is one block of plain data with no pointers inside (links are indices), so the loaded asset is a copy of
 * the definition's and nothing points into a definition that may be replaced while the asset is loaded.
 */


// Macros.
/** How many bytes of a name from the file a message shows, and room for a message's path and its text. */
#define NAME_PRINT_CAPACITY ((size_t)129)
#define WHERE_CAPACITY ((size_t)256)
#define MESSAGE_CAPACITY ((size_t)384)
/** The most keys a compound of the definition may hold (a preset's own and the quantities'). */
#define MAX_KNOWN_KEYS ((size_t)40)
/** Degrees to radians, for the angles (degrees in JSON). */
#define DEGREES_TO_RADIANS 0.017453292519943295
/** The default rate of the wind's direction, 30 degrees per game hour, in radians. */
#define DEFAULT_WIND_DIRECTION_RATE 0.5235988f
/** The largest month weight or link weight a climate may give (a bound for hostile content). */
#define MAX_WEIGHT 1.0e6f
/** A preset's gusts unless it says otherwise: how many per real minute. */
#define DEFAULT_GUSTS_PER_MINUTE 3.0f
/** A preset's lightning unless it says otherwise: the nearest and farthest strikes, metres. */
#define DEFAULT_STRIKE_MIN_DISTANCE 300.0f
#define DEFAULT_STRIKE_MAX_DISTANCE 8000.0f
/** The start key's word for a random first preset, and the seed key's for a new seed per save. */
#define START_RANDOM ((const unsigned char*)u8"random")
#define SEED_PER_SAVE ((const unsigned char*)u8"per_save")


// Types.
struct ClimateStruct
{
    ClimateSettings _settings;
    uint16_t _presetCount;
    uint16_t _linkCount;
    ClimatePreset _presets[CLIMATE_MAX_PRESETS];
    ClimateLink _links[CLIMATE_MAX_LINKS];
};

/* The reader's context: the climate's printable name for messages, and the climate being filled. */
typedef struct ClimateReaderStruct
{
    char Name[NAME_PRINT_CAPACITY];
    Climate* Climate;
} ClimateReader;


// Fields.
static const WeatherQuantityInfo QuantityInfos[WeatherQuantity_Count] =
{
    [WeatherQuantity_CloudCoverage] = { u8"cloud_coverage", 0.0f, 1.0f, 0.25f, false },
    [WeatherQuantity_CloudType] = { u8"cloud_type", 0.0f, 1.0f, 0.2f, false },
    [WeatherQuantity_CloudDensity] = { u8"cloud_density", 0.0f, 1.0f, 0.3f, false },
    [WeatherQuantity_Precipitation] = { u8"precipitation", 0.0f, 1.0f, 0.6f, false },
    [WeatherQuantity_WindDirection] = { u8"wind_direction", -FLT_MAX, FLT_MAX, DEFAULT_WIND_DIRECTION_RATE, true },
    [WeatherQuantity_WindSpeed] = { u8"wind_speed", 0.0f, (float)WORLD_ENVIRONMENT_MAX_WIND_SPEED, 4.0f, false },
    [WeatherQuantity_FogDensity] =
        { u8"fog_density", 0.0f, (float)WORLD_ENVIRONMENT_MAX_GROUND_HAZE_DENSITY, 0.01f, false },
    [WeatherQuantity_FogHeight] = { u8"fog_height", (float)WORLD_ENVIRONMENT_MIN_GROUND_HAZE_THICKNESS,
        (float)WORLD_ENVIRONMENT_MAX_GROUND_HAZE_THICKNESS, 10.0f, false },
    [WeatherQuantity_Temperature] = { u8"temperature", (float)WORLD_ENVIRONMENT_MIN_TEMPERATURE,
        (float)WORLD_ENVIRONMENT_MAX_TEMPERATURE, 2.0f, false },
};

static const unsigned char* const ModeNames[] =
{
    [WeatherMode_Simulated] = u8"simulated",
    [WeatherMode_Fixed] = u8"fixed",
    [WeatherMode_Scripted] = u8"scripted",
    [WeatherMode_Off] = u8"off",
};

/* The keys of each compound besides the quantities' (which presets and fixed_values take too). */
static const unsigned char* const RootKeys[] =
{
    u8"name", u8"mode", u8"seed", u8"start", u8"speed", u8"presets", u8"rates_per_hour", u8"wetting_per_hour",
    u8"drying_per_hour", u8"forecast_accuracy", u8"rain_cloud_range", u8"storm_cloud_range", u8"fixed_preset",
    u8"fixed_values"
};
static const unsigned char* const PresetOwnKeys[] =
{
    u8"name", u8"duration_hours", u8"next", u8"month_weights", u8"start_hours", u8"transition_hours",
    u8"rates_per_hour", u8"gusts", u8"lightning"
};
static const unsigned char* const LinkKeys[] = { u8"preset", u8"weight" };
static const unsigned char* const GustKeys[] = { u8"strength", u8"per_minute" };
static const unsigned char* const LightningKeys[] = { u8"per_minute", u8"per_hour", u8"min_distance", u8"max_distance" };


// Static functions.
/* Whether two NUL-terminated names are the same bytes. */
static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}

/* Whether a JSON value is the string `word` (a NUL-terminated word; the JSON's bytes carry no terminator). */
static bool IsStringValue(const JSONObjectValue* value, const unsigned char* word)
{
    if ((value->Type != JSONValueType_String) || (value->Value.String == NULL))
    {
        return false;
    }
    size_t Length = StringUTF8_GetByteLength(word);
    return (value->Value.String->_count == Length) && Memory_IsEqual(value->Value.String->_data, word, Length);
}

/* An error with "Climate \"<name>\": <where>: <message>". */
static Error Fail(const ClimateReader* reader, ErrorCode code, const char* where, const char* format, ...)
{
    char Message[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    vsnprintf(Message, sizeof(Message), format, Arguments);
    va_end(Arguments);
    return Error_Construct4(code, "Climate \"%s\": %s: %s", reader->Name, where, Message);
}

/* The inner error with "Climate \"<name>\": <where>: " before its message (its code kept); releases it. */
static Error Wrap(const ClimateReader* reader, const char* where, Error* inner)
{
    Error Wrapped = Error_Construct4(inner->Code, "Climate \"%s\": %s: %s", reader->Name, where,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Formats a message's path into a buffer, cut to fit (a path only names where a bad value is). */
static void FormatWhere(char* buffer, size_t capacity, const char* format, ...)
{
    va_list Arguments;
    va_start(Arguments, format);
    int Written = vsnprintf(buffer, capacity, format, Arguments);
    va_end(Arguments);
    if (Written < 0)
    {
        buffer[0] = '\0';
    }
}

/* "<parent>.<key>" (or "<key>" at the root) into a buffer. */
static void JoinWhere(char* buffer, size_t capacity, const char* parent, const char* key)
{
    if ((parent == NULL) || (parent[0] == '\0'))
    {
        FormatWhere(buffer, capacity, "%s", key);
        return;
    }
    FormatWhere(buffer, capacity, "%s.%s", parent, key);
}

/* Refuses a key outside the compound's own keys, plus the quantities' when they are allowed there. */
static Error RejectUnknownKeys(const ClimateReader* reader, JSONCompound* compound, const unsigned char* const* ownKeys,
    size_t ownKeyCount, bool areQuantitiesAllowed, const char* where)
{
    const unsigned char* Keys[MAX_KNOWN_KEYS];
    size_t Count = 0U;
    for (size_t Index = 0U; (Index < ownKeyCount) && (Count < MAX_KNOWN_KEYS); Index++)
    {
        Keys[Count++] = ownKeys[Index];
    }
    for (int Quantity = 0; areQuantitiesAllowed && (Quantity < (int)WeatherQuantity_Count) && (Count < MAX_KNOWN_KEYS);
        Quantity++)
    {
        Keys[Count++] = QuantityInfos[Quantity].Key;
    }
    char Where[WHERE_CAPACITY];
    FormatWhere(Where, sizeof(Where), "climate \"%s\", %s", reader->Name, ((where != NULL) && (where[0] != '\0'))
        ? where : "its root");
    return AssetJSON_RejectUnknownKeys(compound, Keys, Count, Where);
}

/* The compound a value holds, or an error naming where it should be one. */
static Error RequireCompound(const ClimateReader* reader, const JSONObjectValue* value, const char* where,
    JSONCompound** outCompound)
{
    if (value->Type != JSONValueType_Compound)
    {
        return Fail(reader, ErrorCode_InvalidJSON, where, "expected an object.");
    }
    *outCompound = value->Value.Compound;
    return Error_CreateSuccess();
}

/* The array a value holds, or an error naming where it should be one. */
static Error RequireArray(const ClimateReader* reader, const JSONObjectValue* value, const char* where,
    JSONArray** outArray)
{
    if (value->Type != JSONValueType_Array)
    {
        return Fail(reader, ErrorCode_InvalidJSON, where, "expected an array.");
    }
    *outArray = value->Value.Array;
    return Error_CreateSuccess();
}

/* A key's value when the compound has it. */
static Error GetOptional(const ClimateReader* reader, JSONCompound* compound, const unsigned char* key,
    const char* where, JSONObjectValue* outValue, bool* outFound)
{
    Error Result = JSONCompound_GetOptional(compound, key, outValue, outFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Wrap(reader, where, &Result);
    }
    return Error_CreateSuccess();
}

/* A finite number in [minimum, maximum] (the minimum itself excluded when isMinimumExclusive). */
static Error ReadNumber(const ClimateReader* reader, const JSONObjectValue* value, const char* where, float minimum,
    float maximum, bool isMinimumExclusive, float* outValue)
{
    float Value = 0.0f;
    Error Result = AssetJSON_ValueToFloat(value, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Wrap(reader, where, &Result);
    }
    bool IsAboveMinimum = isMinimumExclusive ? (Value > minimum) : (Value >= minimum);
    if (!IsAboveMinimum || (Value > maximum))
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "%g must be in %s%g, %g].", (double)Value,
            isMinimumExclusive ? "(" : "[", (double)minimum, (double)maximum);
    }
    *outValue = Value;
    return Error_CreateSuccess();
}

/* A number, or a [min, max] pair with min <= max, each in [minimum, maximum] (exclusive at the minimum when asked). */
static Error ReadRange(const ClimateReader* reader, const JSONObjectValue* value, const char* where, float minimum,
    float maximum, bool isMinimumExclusive, ClimateRange* outRange)
{
    if (value->Type != JSONValueType_Array)
    {
        float Single = 0.0f;
        Error Result = ReadNumber(reader, value, where, minimum, maximum, isMinimumExclusive, &Single);
        if (Result.Code == ErrorCode_Success)
        {
            *outRange = (ClimateRange){ .Minimum = Single, .Maximum = Single };
        }
        return Result;
    }
    JSONArray* Array = value->Value.Array;
    if (JSONArray_GetElementCount(Array) != 2U)
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "expected a number or [min, max].");
    }
    float Ends[2] = { 0.0f, 0.0f };
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        JSONObjectValue Element;
        Error Result = JSONArray_Get(Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return Wrap(reader, where, &Result);
        }
        char Where[WHERE_CAPACITY];
        FormatWhere(Where, sizeof(Where), "%s[%zu]", where, Index);
        Result = ReadNumber(reader, &Element, Where, minimum, maximum, isMinimumExclusive, &Ends[Index]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if (Ends[0] > Ends[1])
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "the range [%g, %g] is not in order.",
            (double)Ends[0], (double)Ends[1]);
    }
    *outRange = (ClimateRange){ .Minimum = Ends[0], .Maximum = Ends[1] };
    return Error_CreateSuccess();
}

/* A quantity's value or range: in its range, or for an angle any finite number of degrees, read into radians. */
static Error ReadQuantity(const ClimateReader* reader, WeatherQuantity quantity, const JSONObjectValue* value,
    const char* where, bool isRangeAllowed, ClimateRange* outRange)
{
    const WeatherQuantityInfo* Info = &QuantityInfos[quantity];
    if (!isRangeAllowed && (value->Type == JSONValueType_Array))
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "expected a number.");
    }
    ClimateRange Range;
    Error Result = ReadRange(reader, value, where, Info->Minimum, Info->Maximum, false, &Range);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Info->IsAngle)
    {
        Range.Minimum = (float)((double)Range.Minimum * DEGREES_TO_RADIANS);
        Range.Maximum = (float)((double)Range.Maximum * DEGREES_TO_RADIANS);
    }
    *outRange = Range;
    return Error_CreateSuccess();
}

/* A compound of quantity keys to rates (>= 0, per game hour; an angle's in degrees per hour, read into radians). */
static Error ReadRates(const ClimateReader* reader, const JSONObjectValue* value, const char* where, uint32_t* outMask,
    float rates[WeatherQuantity_Count])
{
    JSONCompound* Compound = NULL;
    Error Result = RequireCompound(reader, value, where, &Compound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = RejectUnknownKeys(reader, Compound, NULL, 0U, true, where);
    }
    for (int Quantity = 0; (Result.Code == ErrorCode_Success) && (Quantity < (int)WeatherQuantity_Count); Quantity++)
    {
        const WeatherQuantityInfo* Info = &QuantityInfos[Quantity];
        char Where[WHERE_CAPACITY];
        JoinWhere(Where, sizeof(Where), where, (const char*)Info->Key);
        JSONObjectValue Rate;
        bool IsFound = false;
        Result = GetOptional(reader, Compound, Info->Key, Where, &Rate, &IsFound);
        if ((Result.Code != ErrorCode_Success) || !IsFound)
        {
            continue;
        }
        float Value = 0.0f;
        Result = ReadNumber(reader, &Rate, Where, 0.0f, CLIMATE_MAX_RATE, false, &Value);
        if (Result.Code == ErrorCode_Success)
        {
            rates[Quantity] = Info->IsAngle ? (float)((double)Value * DEGREES_TO_RADIANS) : Value;
            *outMask |= (uint32_t)1U << Quantity;
        }
    }
    return Result;
}

/* A string value that fits a buffer (at most capacity - 1 bytes). */
static Error ReadName(const ClimateReader* reader, const JSONObjectValue* value, const char* where,
    unsigned char* buffer, size_t capacity)
{
    if (value->Type != JSONValueType_String)
    {
        return Fail(reader, ErrorCode_InvalidJSON, where, "expected a string.");
    }
    const GenericBuffer* Text = value->Value.String;
    size_t Length = (Text != NULL) ? Text->_count : 0U;
    if ((Length == 0U) || (Length >= capacity))
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "a name must have 1 to %zu bytes.", capacity - 1U);
    }
    for (size_t Index = 0U; Index < Length; Index++)
    {
        if (Text->_data[Index] == 0U)
        {
            return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "a name must not hold a NUL.");
        }
    }
    Memory_Copy(Text->_data, buffer, Length);
    buffer[Length] = 0U;
    return Error_CreateSuccess();
}

/* A value naming one of the climate's presets (by its name). */
static Error ReadPresetReference(const ClimateReader* reader, const JSONObjectValue* value, const char* where,
    uint16_t* outIndex)
{
    unsigned char Name[CLIMATE_PRESET_NAME_MAX_LENGTH + 1];
    Error Result = ReadName(reader, value, where, Name, sizeof(Name));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Climate_TryFindPreset(reader->Climate, Name, outIndex))
    {
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(Name, Printable, sizeof(Printable));
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "no preset is named \"%s\".", Printable);
    }
    return Error_CreateSuccess();
}

/* The presets' names (the first pass): each a valid, unique name; "random" is the start key's word. */
static Error ReadPresetNames(const ClimateReader* reader, JSONArray* presets)
{
    Climate* Self = reader->Climate;
    size_t Count = JSONArray_GetElementCount(presets);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        char Where[WHERE_CAPACITY];
        FormatWhere(Where, sizeof(Where), "presets[%zu]", Index);
        JSONObjectValue Value;
        Error Result = JSONArray_Get(presets, Index, &Value);
        JSONCompound* Preset = NULL;
        if (Result.Code != ErrorCode_Success)
        {
            return Wrap(reader, Where, &Result);
        }
        Result = RequireCompound(reader, &Value, Where, &Preset);
        JSONObjectValue NameValue;
        bool IsFound = false;
        if (Result.Code == ErrorCode_Success)
        {
            Result = GetOptional(reader, Preset, u8"name", Where, &NameValue, &IsFound);
        }
        if ((Result.Code == ErrorCode_Success) && !IsFound)
        {
            Result = Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "a preset needs a \"name\".");
        }
        char NameWhere[WHERE_CAPACITY];
        JoinWhere(NameWhere, sizeof(NameWhere), Where, "name");
        unsigned char* Name = Self->_presets[Index].Name;
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadName(reader, &NameValue, NameWhere, Name, CLIMATE_PRESET_NAME_MAX_LENGTH + 1U);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(Name, Printable, sizeof(Printable));
        if (!NamespacedKey_IsValidLocalName(Name))
        {
            return Fail(reader, ErrorCode_InvalidAssetDefinition, NameWhere,
                "\"%s\" is not a valid name ([a-z][a-z0-9_.-]*).", Printable);
        }
        if (AreNamesEqual(Name, START_RANDOM))
        {
            return Fail(reader, ErrorCode_InvalidAssetDefinition, NameWhere,
                "\"random\" is the start key's word for a random preset, not a preset's name.");
        }
        uint16_t Existing = 0U;
        Self->_presetCount = (uint16_t)Index; // search only the names read so far
        if (Climate_TryFindPreset(Self, Name, &Existing))
        {
            return Fail(reader, ErrorCode_InvalidAssetDefinition, NameWhere, "the name \"%s\" is used by presets[%u] too.",
                Printable, (unsigned int)Existing);
        }
    }
    Self->_presetCount = (uint16_t)Count;
    return Error_CreateSuccess();
}

/* A preset's links: each a known preset with a weight above 0 (1 when left out). */
static Error ReadLinks(const ClimateReader* reader, ClimatePreset* preset, const JSONObjectValue* value,
    const char* where)
{
    Climate* Self = reader->Climate;
    JSONArray* Links = NULL;
    Error Result = RequireArray(reader, value, where, &Links);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Links);
    if (Count > (size_t)(CLIMATE_MAX_LINKS - Self->_linkCount))
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "a climate holds at most %d links in all.",
            CLIMATE_MAX_LINKS);
    }
    preset->FirstLink = Self->_linkCount;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        char Where[WHERE_CAPACITY];
        FormatWhere(Where, sizeof(Where), "%s[%zu]", where, Index);
        JSONObjectValue LinkValue;
        Result = JSONArray_Get(Links, Index, &LinkValue);
        if (Result.Code != ErrorCode_Success)
        {
            return Wrap(reader, Where, &Result);
        }
        JSONCompound* Link = NULL;
        Result = RequireCompound(reader, &LinkValue, Where, &Link);
        if (Result.Code == ErrorCode_Success)
        {
            Result = RejectUnknownKeys(reader, Link, LinkKeys, sizeof(LinkKeys) / sizeof(LinkKeys[0]), false, Where);
        }
        JSONObjectValue Target;
        bool IsFound = false;
        if (Result.Code == ErrorCode_Success)
        {
            Result = GetOptional(reader, Link, u8"preset", Where, &Target, &IsFound);
        }
        if ((Result.Code == ErrorCode_Success) && !IsFound)
        {
            Result = Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "a link needs a \"preset\".");
        }
        char TargetWhere[WHERE_CAPACITY];
        JoinWhere(TargetWhere, sizeof(TargetWhere), Where, "preset");
        ClimateLink* Entry = &Self->_links[Self->_linkCount];
        *Entry = (ClimateLink){ .Preset = 0U, .Weight = 1.0f };
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadPresetReference(reader, &Target, TargetWhere, &Entry->Preset);
        }
        JSONObjectValue Weight;
        if (Result.Code == ErrorCode_Success)
        {
            Result = GetOptional(reader, Link, u8"weight", Where, &Weight, &IsFound);
        }
        if ((Result.Code == ErrorCode_Success) && IsFound)
        {
            char WeightWhere[WHERE_CAPACITY];
            JoinWhere(WeightWhere, sizeof(WeightWhere), Where, "weight");
            Result = ReadNumber(reader, &Weight, WeightWhere, 0.0f, MAX_WEIGHT, true, &Entry->Weight);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Self->_linkCount++;
    }
    preset->LinkCount = (uint16_t)Count;
    return Error_CreateSuccess();
}

/* A preset's month weights: exactly twelve numbers, each >= 0. */
static Error ReadMonthWeights(const ClimateReader* reader, ClimatePreset* preset, const JSONObjectValue* value,
    const char* where)
{
    JSONArray* Weights = NULL;
    Error Result = RequireArray(reader, value, where, &Weights);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Weights);
    if (Count != (size_t)CLIMATE_MONTH_COUNT)
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "expected %d month weights, one per month; found %zu.",
            CLIMATE_MONTH_COUNT, Count);
    }
    for (size_t Month = 0U; Month < Count; Month++)
    {
        char Where[WHERE_CAPACITY];
        FormatWhere(Where, sizeof(Where), "%s[%zu]", where, Month);
        JSONObjectValue Weight;
        Result = JSONArray_Get(Weights, Month, &Weight);
        if (Result.Code != ErrorCode_Success)
        {
            return Wrap(reader, Where, &Result);
        }
        Result = ReadNumber(reader, &Weight, Where, 0.0f, MAX_WEIGHT, false, &preset->MonthWeights[Month]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    preset->HasMonthWeights = true;
    return Error_CreateSuccess();
}

/* A preset's start hours: [from, to) with from in [0, 24) and to in [0, 24], to below from wrapping past midnight, never
 * empty. */
static Error ReadStartHours(const ClimateReader* reader, ClimatePreset* preset, const JSONObjectValue* value,
    const char* where)
{
    JSONArray* Hours = NULL;
    Error Result = RequireArray(reader, value, where, &Hours);
    if ((Result.Code == ErrorCode_Success) && (JSONArray_GetElementCount(Hours) != 2U))
    {
        Result = Fail(reader, ErrorCode_InvalidAssetDefinition, where, "expected [from, to] in hours of the day.");
    }
    float Ends[2] = { 0.0f, 0.0f };
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < 2U); Index++)
    {
        char Where[WHERE_CAPACITY];
        FormatWhere(Where, sizeof(Where), "%s[%zu]", where, Index);
        JSONObjectValue Hour;
        Result = JSONArray_Get(Hours, Index, &Hour);
        if (Result.Code != ErrorCode_Success)
        {
            return Wrap(reader, Where, &Result);
        }
        Result = ReadNumber(reader, &Hour, Where, 0.0f, CLIMATE_HOURS_PER_DAY, false, &Ends[Index]);
    }
    if ((Result.Code == ErrorCode_Success) && (Ends[0] >= CLIMATE_HOURS_PER_DAY))
    {
        // 24:00 is the next day's 0: [24, x] would start a window no hour of the day is in.
        char Where[WHERE_CAPACITY];
        FormatWhere(Where, sizeof(Where), "%s[0]", where);
        Result = Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "a window starts below %g (midnight is 0).",
            (double)CLIMATE_HOURS_PER_DAY);
    }
    if ((Result.Code == ErrorCode_Success) && (Ends[0] == Ends[1]))
    {
        Result = Fail(reader, ErrorCode_InvalidAssetDefinition, where, "[%g, %g] is an empty window.", (double)Ends[0],
            (double)Ends[1]);
    }
    if (Result.Code == ErrorCode_Success)
    {
        preset->HasStartHours = true;
        preset->StartHours = (ClimateRange){ .Minimum = Ends[0], .Maximum = Ends[1] };
    }
    return Result;
}

/* A preset's gusts: strength (>= 0, default 0) and gusts per real minute (above 0, default 3). */
static Error ReadGusts(const ClimateReader* reader, ClimatePreset* preset, const JSONObjectValue* value,
    const char* where)
{
    JSONCompound* Gusts = NULL;
    Error Result = RequireCompound(reader, value, where, &Gusts);
    if (Result.Code == ErrorCode_Success)
    {
        Result = RejectUnknownKeys(reader, Gusts, GustKeys, sizeof(GustKeys) / sizeof(GustKeys[0]), false, where);
    }
    preset->Gusts = (ClimateGusts){ .Strength = 0.0f, .PerMinute = DEFAULT_GUSTS_PER_MINUTE };
    JSONObjectValue Field;
    bool IsFound = false;
    char Where[WHERE_CAPACITY];
    JoinWhere(Where, sizeof(Where), where, "strength");
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptional(reader, Gusts, u8"strength", Where, &Field, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadNumber(reader, &Field, Where, 0.0f, (float)WORLD_ENVIRONMENT_MAX_WIND_SPEED, false,
            &preset->Gusts.Strength);
    }
    JoinWhere(Where, sizeof(Where), where, "per_minute");
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptional(reader, Gusts, u8"per_minute", Where, &Field, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadNumber(reader, &Field, Where, 0.0f, CLIMATE_MAX_PER_MINUTE, true, &preset->Gusts.PerMinute);
    }
    preset->HasGusts = Result.Code == ErrorCode_Success;
    return Result;
}

/* One optional number of a compound into *field (which keeps its default when the key is absent). */
static Error ReadOptionalNumber(const ClimateReader* reader, JSONCompound* compound, const unsigned char* key,
    const char* parent, float minimum, float maximum, bool isMinimumExclusive, float* field)
{
    char Where[WHERE_CAPACITY];
    JoinWhere(Where, sizeof(Where), parent, (const char*)key);
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = GetOptional(reader, compound, key, Where, &Value, &IsFound);
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadNumber(reader, &Value, Where, minimum, maximum, isMinimumExclusive, field);
    }
    return Result;
}

/* A preset's lightning: strikes per real minute and per game hour (>= 0, default 0) and their distances. */
static Error ReadLightning(const ClimateReader* reader, ClimatePreset* preset, const JSONObjectValue* value,
    const char* where)
{
    JSONCompound* Lightning = NULL;
    Error Result = RequireCompound(reader, value, where, &Lightning);
    if (Result.Code == ErrorCode_Success)
    {
        Result = RejectUnknownKeys(reader, Lightning, LightningKeys, sizeof(LightningKeys) / sizeof(LightningKeys[0]),
            false, where);
    }
    ClimateLightning* Out = &preset->Lightning;
    *Out = (ClimateLightning){ .PerMinute = 0.0f, .PerHour = 0.0f, .MinDistance = DEFAULT_STRIKE_MIN_DISTANCE,
        .MaxDistance = DEFAULT_STRIKE_MAX_DISTANCE };
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadOptionalNumber(reader, Lightning, u8"per_minute", where, 0.0f, CLIMATE_MAX_PER_MINUTE, false,
            &Out->PerMinute);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadOptionalNumber(reader, Lightning, u8"per_hour", where, 0.0f, CLIMATE_MAX_PER_HOUR, false,
            &Out->PerHour);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadOptionalNumber(reader, Lightning, u8"min_distance", where, 0.0f, CLIMATE_MAX_STRIKE_DISTANCE, false,
            &Out->MinDistance);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadOptionalNumber(reader, Lightning, u8"max_distance", where, 0.0f, CLIMATE_MAX_STRIKE_DISTANCE, false,
            &Out->MaxDistance);
    }
    if ((Result.Code == ErrorCode_Success) && (Out->MinDistance > Out->MaxDistance))
    {
        Result = Fail(reader, ErrorCode_InvalidAssetDefinition, where,
            "min_distance (%g) must not be past max_distance (%g).", (double)Out->MinDistance, (double)Out->MaxDistance);
    }
    preset->HasLightning = Result.Code == ErrorCode_Success;
    return Result;
}

/* A preset's targets: each quantity key it holds, a number or a [min, max] range. */
static Error ReadTargets(const ClimateReader* reader, ClimatePreset* preset, JSONCompound* compound, const char* where)
{
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        char Where[WHERE_CAPACITY];
        JoinWhere(Where, sizeof(Where), where, (const char*)QuantityInfos[Quantity].Key);
        JSONObjectValue Value;
        bool IsFound = false;
        Error Result = GetOptional(reader, compound, QuantityInfos[Quantity].Key, Where, &Value, &IsFound);
        if ((Result.Code == ErrorCode_Success) && IsFound)
        {
            Result = ReadQuantity(reader, (WeatherQuantity)Quantity, &Value, Where, true, &preset->Targets[Quantity]);
            preset->TargetMask |= (Result.Code == ErrorCode_Success) ? ((uint32_t)1U << Quantity) : 0U;
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* One preset (the second pass: its name was read already). */
static Error ReadPreset(const ClimateReader* reader, ClimatePreset* preset, JSONCompound* compound, const char* where)
{
    preset->DurationHours = (ClimateRange){ .Minimum = CLIMATE_DEFAULT_DURATION_MIN_HOURS,
        .Maximum = CLIMATE_DEFAULT_DURATION_MAX_HOURS };
    Error Result = RejectUnknownKeys(reader, compound, PresetOwnKeys, sizeof(PresetOwnKeys) / sizeof(PresetOwnKeys[0]),
        true, where);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadTargets(reader, preset, compound, where);
    }

    static const unsigned char* const Keys[] =
    {
        u8"duration_hours", u8"next", u8"month_weights", u8"start_hours", u8"transition_hours", u8"rates_per_hour",
        u8"gusts", u8"lightning"
    };
    for (size_t KeyIndex = 0U; (Result.Code == ErrorCode_Success) && (KeyIndex < (sizeof(Keys) / sizeof(Keys[0])));
        KeyIndex++)
    {
        char Where[WHERE_CAPACITY];
        JoinWhere(Where, sizeof(Where), where, (const char*)Keys[KeyIndex]);
        JSONObjectValue Value;
        bool IsFound = false;
        Result = GetOptional(reader, compound, Keys[KeyIndex], Where, &Value, &IsFound);
        if ((Result.Code != ErrorCode_Success) || !IsFound)
        {
            continue;
        }
        switch (KeyIndex)
        {
            case 0U:
                Result = ReadRange(reader, &Value, Where, CLIMATE_MIN_DURATION_HOURS, CLIMATE_MAX_HOURS, false,
                    &preset->DurationHours);
                break;
            case 1U:
                Result = ReadLinks(reader, preset, &Value, Where);
                break;
            case 2U:
                Result = ReadMonthWeights(reader, preset, &Value, Where);
                break;
            case 3U:
                Result = ReadStartHours(reader, preset, &Value, Where);
                break;
            case 4U:
                Result = ReadNumber(reader, &Value, Where, 0.0f, CLIMATE_MAX_HOURS, false, &preset->TransitionHours);
                preset->HasTransitionHours = Result.Code == ErrorCode_Success;
                break;
            case 5U:
                Result = ReadRates(reader, &Value, Where, &preset->RateMask, preset->RatesPerHour);
                break;
            case 6U:
                Result = ReadGusts(reader, preset, &Value, Where);
                break;
            default:
                Result = ReadLightning(reader, preset, &Value, Where);
                break;
        }
    }
    return Result;
}

/* Every preset: the names first, then each preset's keys (links may name presets later in the file). */
static Error ReadPresets(const ClimateReader* reader, JSONCompound* root)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = GetOptional(reader, root, u8"presets", "presets", &Value, &IsFound);
    if ((Result.Code == ErrorCode_Success) && !IsFound)
    {
        Result = Fail(reader, ErrorCode_InvalidAssetDefinition, "presets", "a climate needs at least one preset.");
    }
    JSONArray* Presets = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = RequireArray(reader, &Value, "presets", &Presets);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Presets);
    if ((Count == 0U) || (Count > (size_t)CLIMATE_MAX_PRESETS))
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, "presets", "a climate holds 1 to %d presets; this has %zu.",
            CLIMATE_MAX_PRESETS, Count);
    }
    Result = ReadPresetNames(reader, Presets);
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < Count); Index++)
    {
        char PrintableName[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(reader->Climate->_presets[Index].Name, PrintableName, sizeof(PrintableName));
        char Where[WHERE_CAPACITY];
        FormatWhere(Where, sizeof(Where), "presets[%zu] (\"%s\")", Index, PrintableName);
        JSONObjectValue PresetValue;
        Result = JSONArray_Get(Presets, Index, &PresetValue);
        if (Result.Code != ErrorCode_Success)
        {
            return Wrap(reader, Where, &Result);
        }
        Result = ReadPreset(reader, &reader->Climate->_presets[Index], PresetValue.Value.Compound, Where);
    }
    return Result;
}

/* The mode's name. */
static Error ReadMode(const ClimateReader* reader, const JSONObjectValue* value, WeatherMode* outMode)
{
    unsigned char Name[CLIMATE_PRESET_NAME_MAX_LENGTH + 1];
    Error Result = ReadName(reader, value, "mode", Name, sizeof(Name));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (int Mode = 0; Mode < (int)(sizeof(ModeNames) / sizeof(ModeNames[0])); Mode++)
    {
        if (AreNamesEqual(Name, ModeNames[Mode]))
        {
            *outMode = (WeatherMode)Mode;
            return Error_CreateSuccess();
        }
    }
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Name, Printable, sizeof(Printable));
    return Fail(reader, ErrorCode_InvalidAssetDefinition, "mode",
        "\"%s\" is no mode (simulated, fixed, scripted or off).", Printable);
}

/* The seed: "per_save", or a whole number >= 0. */
static Error ReadSeed(const ClimateReader* reader, const JSONObjectValue* value, ClimateSettings* settings)
{
    if (IsStringValue(value, SEED_PER_SAVE))
    {
        settings->IsSeedPerSave = true;
        return Error_CreateSuccess();
    }
    Error Result = GameJSON_ValueToUInt64(value, &settings->Seed);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(reader, ErrorCode_InvalidAssetDefinition, "seed", "expected \"per_save\" or a whole number >= 0.");
    }
    settings->IsSeedPerSave = false;
    return Error_CreateSuccess();
}

/* The start: "random", or a preset's name. */
static Error ReadStart(const ClimateReader* reader, const JSONObjectValue* value, ClimateSettings* settings)
{
    if (IsStringValue(value, START_RANDOM))
    {
        settings->IsStartRandom = true;
        return Error_CreateSuccess();
    }
    settings->IsStartRandom = false;
    return ReadPresetReference(reader, value, "start", &settings->StartPreset);
}

/* The fixed values: quantity keys to single numbers. */
static Error ReadFixedValues(const ClimateReader* reader, const JSONObjectValue* value, ClimateSettings* settings)
{
    JSONCompound* Values = NULL;
    Error Result = RequireCompound(reader, value, "fixed_values", &Values);
    if (Result.Code == ErrorCode_Success)
    {
        Result = RejectUnknownKeys(reader, Values, NULL, 0U, true, "fixed_values");
    }
    for (int Quantity = 0; (Result.Code == ErrorCode_Success) && (Quantity < (int)WeatherQuantity_Count); Quantity++)
    {
        char Where[WHERE_CAPACITY];
        JoinWhere(Where, sizeof(Where), "fixed_values", (const char*)QuantityInfos[Quantity].Key);
        JSONObjectValue Field;
        bool IsFound = false;
        Result = GetOptional(reader, Values, QuantityInfos[Quantity].Key, Where, &Field, &IsFound);
        if ((Result.Code != ErrorCode_Success) || !IsFound)
        {
            continue;
        }
        ClimateRange Range;
        Result = ReadQuantity(reader, (WeatherQuantity)Quantity, &Field, Where, false, &Range);
        if (Result.Code == ErrorCode_Success)
        {
            settings->FixedValues[Quantity] = Range.Minimum;
            settings->FixedValueMask |= (uint32_t)1U << Quantity;
        }
    }
    return Result;
}

/* A root key's value into the settings (presets are read before, so names resolve). */
static Error ReadRootKey(const ClimateReader* reader, JSONCompound* root, const unsigned char* key,
    ClimateSettings* settings)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = GetOptional(reader, root, key, (const char*)key, &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    const char* Where = (const char*)key;
    if (AreNamesEqual(key, u8"mode"))
    {
        return ReadMode(reader, &Value, &settings->Mode);
    }
    if (AreNamesEqual(key, u8"seed"))
    {
        return ReadSeed(reader, &Value, settings);
    }
    if (AreNamesEqual(key, u8"start"))
    {
        return ReadStart(reader, &Value, settings);
    }
    if (AreNamesEqual(key, u8"speed"))
    {
        return ReadNumber(reader, &Value, Where, 0.0f, CLIMATE_MAX_SPEED, true, &settings->Speed);
    }
    if (AreNamesEqual(key, u8"rates_per_hour"))
    {
        uint32_t Mask = 0U;
        return ReadRates(reader, &Value, Where, &Mask, settings->RatesPerHour);
    }
    if (AreNamesEqual(key, u8"wetting_per_hour"))
    {
        return ReadNumber(reader, &Value, Where, 0.0f, CLIMATE_MAX_RATE, false, &settings->WettingPerHour);
    }
    if (AreNamesEqual(key, u8"drying_per_hour"))
    {
        return ReadNumber(reader, &Value, Where, 0.0f, CLIMATE_MAX_RATE, false, &settings->DryingPerHour);
    }
    if (AreNamesEqual(key, u8"forecast_accuracy"))
    {
        return ReadNumber(reader, &Value, Where, 0.0f, 1.0f, false, &settings->ForecastAccuracy);
    }
    if (AreNamesEqual(key, u8"rain_cloud_range"))
    {
        return ReadRange(reader, &Value, Where, 0.0f, 1.0f, false, &settings->RainCloudRange);
    }
    if (AreNamesEqual(key, u8"storm_cloud_range"))
    {
        return ReadRange(reader, &Value, Where, 0.0f, 1.0f, false, &settings->StormCloudRange);
    }
    if (AreNamesEqual(key, u8"fixed_preset"))
    {
        Result = ReadPresetReference(reader, &Value, Where, &settings->FixedPreset);
        settings->HasFixedPreset = Result.Code == ErrorCode_Success;
        return Result;
    }
    if (AreNamesEqual(key, u8"fixed_values"))
    {
        return ReadFixedValues(reader, &Value, settings);
    }
    return Error_CreateSuccess(); // "name" and "presets": read elsewhere
}

/* The settings every key leaves out: simulated, a seed per save, the first preset, Earth's rates. */
static ClimateSettings CreateDefaultSettings(void)
{
    ClimateSettings Settings;
    Memory_Zero(&Settings, sizeof(Settings));
    Settings.Mode = WeatherMode_Simulated;
    Settings.IsSeedPerSave = true;
    Settings.IsStartRandom = false;
    Settings.StartPreset = 0U;
    Settings.Speed = 1.0f;
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        Settings.RatesPerHour[Quantity] = QuantityInfos[Quantity].DefaultRatePerHour;
    }
    Settings.WettingPerHour = CLIMATE_DEFAULT_WETTING_PER_HOUR;
    Settings.DryingPerHour = CLIMATE_DEFAULT_DRYING_PER_HOUR;
    Settings.ForecastAccuracy = 1.0f;
    Settings.RainCloudRange = (ClimateRange){ .Minimum = CLIMATE_DEFAULT_RAIN_CLOUD_MIN,
        .Maximum = CLIMATE_DEFAULT_RAIN_CLOUD_MAX };
    Settings.StormCloudRange = (ClimateRange){ .Minimum = CLIMATE_DEFAULT_STORM_CLOUD_MIN,
        .Maximum = CLIMATE_DEFAULT_STORM_CLOUD_MAX };
    return Settings;
}


// Public functions.
const WeatherQuantityInfo* WeatherQuantity_GetInfo(WeatherQuantity quantity)
{
    int Index = (int)quantity;
    return ((Index >= 0) && (Index < (int)WeatherQuantity_Count)) ? &QuantityInfos[Index] : &QuantityInfos[0];
}

bool WeatherQuantity_TryParseKey(const unsigned char* key, WeatherQuantity* outQuantity)
{
    if (key == NULL)
    {
        return false;
    }
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        if (AreNamesEqual(key, QuantityInfos[Quantity].Key))
        {
            *outQuantity = (WeatherQuantity)Quantity;
            return true;
        }
    }
    return false;
}

const unsigned char* WeatherMode_GetName(WeatherMode mode)
{
    int Index = (int)mode;
    return ((Index >= 0) && (Index < (int)(sizeof(ModeNames) / sizeof(ModeNames[0])))) ? ModeNames[Index] : ModeNames[0];
}

const ClimateSettings* Climate_GetSettings(const Climate* self)
{
    return &self->_settings;
}

uint16_t Climate_GetPresetCount(const Climate* self)
{
    return self->_presetCount;
}

const ClimatePreset* Climate_GetPreset(const Climate* self, uint16_t index)
{
    return (index < self->_presetCount) ? &self->_presets[index] : NULL;
}

const ClimateLink* Climate_GetLinks(const Climate* self, const ClimatePreset* preset, uint16_t* outCount)
{
    *outCount = 0U;
    if ((preset->LinkCount == 0U) || (preset->FirstLink >= self->_linkCount)
        || (preset->LinkCount > (uint16_t)(self->_linkCount - preset->FirstLink)))
    {
        return NULL;
    }
    *outCount = preset->LinkCount;
    return &self->_links[preset->FirstLink];
}

bool Climate_TryFindPreset(const Climate* self, const unsigned char* name, uint16_t* outIndex)
{
    if (name == NULL)
    {
        return false;
    }
    for (uint16_t Index = 0U; Index < self->_presetCount; Index++)
    {
        if (AreNamesEqual(name, self->_presets[Index].Name))
        {
            *outIndex = Index;
            return true;
        }
    }
    return false;
}

Error Climate_ReadJSON(const unsigned char* name, JSONCompound* root, Climate** outClimate)
{
    *outClimate = NULL;
    Climate* Self = Memory_Allocate(sizeof(Climate));
    Memory_Zero(Self, sizeof(*Self));
    Self->_settings = CreateDefaultSettings();

    ClimateReader Reader;
    AssetJSON_CopyPrintable(name, Reader.Name, sizeof(Reader.Name));
    Reader.Climate = Self;

    Error Result = RejectUnknownKeys(&Reader, root, RootKeys, sizeof(RootKeys) / sizeof(RootKeys[0]), false, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadPresets(&Reader, root);
    }
    for (size_t KeyIndex = 0U; (Result.Code == ErrorCode_Success) && (KeyIndex < (sizeof(RootKeys) / sizeof(RootKeys[0])));
        KeyIndex++)
    {
        Result = ReadRootKey(&Reader, root, RootKeys[KeyIndex], &Self->_settings);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Self);
        return Result;
    }
    *outClimate = Self;
    return Error_CreateSuccess();
}

Climate* Climate_Clone(const Climate* self)
{
    Climate* Copy = Memory_Allocate(sizeof(Climate));
    Memory_Copy(self, Copy, sizeof(Climate));
    return Copy;
}

void Climate_Destroy(Climate* self)
{
    if (self != NULL)
    {
        Memory_Free(self);
    }
}
