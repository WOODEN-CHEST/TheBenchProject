#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "AnimationGraph.h"
#include "AnimationLibrary.h"
#include "AssetDefinitionCommon.h"
#include "AssetManager.h"
#include "GameJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The animation graph's JSON reader (AnimationGraph_CreateFromJSON; references/asset_structure.md, "Animation graphs").
 * It reads the definition once, checking every rule as it goes, into a builder: one growing array per kind of record
 * and one of strings. The records hold indices and string offsets, never pointers, so once everything is read the block
 * is the arrays laid end to end behind the header. Definitions are hostile (downloaded content): every count has a
 * limit, every name a pattern and a length, every number a range, and nothing is nested deeper than the format's fixed
 * levels.
 */


// Macros.
/** Room for one message's text, a "where" in the definition and a name from the file. */
#define MESSAGE_CAPACITY ((size_t)512)
#define WHERE_CAPACITY ((size_t)256)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** The number of keys in a list of known keys. */
#define KEY_COUNT(keys) (sizeof(keys) / sizeof((keys)[0]))
/** Degrees to radians. */
#define DEGREES_TO_RADIANS 0.017453292519943295f
/** The largest yaw and pitch limits look-at takes, in degrees. */
#define LOOK_MAX_YAW_LIMIT 180.0f
#define LOOK_MAX_PITCH_LIMIT 90.0f
/** How far the look bones' shares may add up past 1 (their rounding in the file). */
#define LOOK_SHARE_TOLERANCE 1e-4f
/** The latest exit time a transition may wait for, in cycles of its state. */
#define MAX_EXIT_TIME 1000.0f
/** Every array of the block starts at a multiple of this (the records' alignment). */
#define BLOCK_ALIGNMENT ((size_t)8)
/** The name "from" uses for every state; no state may have it. */
#define ANY_STATE_NAME ((const unsigned char*)u8"any")


// Types.
typedef struct BuilderStruct
{
    char GraphName[NAME_PRINT_CAPACITY];
    float FadeSeconds;
    AnimationGraphLookAt LookAt;
    uint32_t ValueParameterCount;
    uint32_t TriggerCount;
    GenericBuffer Parameters;   // AnimationGraphParameter
    GenericBuffer Layers;       // AnimationGraphLayer
    GenericBuffer States;       // AnimationGraphState
    GenericBuffer Transitions;  // AnimationGraphTransition
    GenericBuffer Conditions;   // AnimationGraphCondition
    GenericBuffer Points;       // AnimationGraphBlendPoint
    GenericBuffer Events;       // AnimationGraphEventRecord
    GenericBuffer Sets;         // AnimationGraphParameterSet
    GenericBuffer Actions;      // AnimationGraphAction
    GenericBuffer ActionStates; // uint32_t
    GenericBuffer MaskBones;    // uint32_t string offsets
    GenericBuffer LookBones;    // AnimationGraphLookBone
    GenericBuffer Strings;      // bytes
} Builder;

/* What one condition's JSON object names. */
typedef struct ConditionKeysStruct
{
    const unsigned char* Key;
    AnimationGraphCompare Compare;
} ConditionKeys;


// Fields.
static const unsigned char* const RootKeys[] =
{
    u8"name", u8"fade_seconds", u8"parameters", u8"layers", u8"look_at", u8"actions"
};
static const unsigned char* const FloatParameterKeys[] = { u8"type", u8"default", u8"min", u8"max" };
static const unsigned char* const BoolParameterKeys[] = { u8"type", u8"default" };
static const unsigned char* const TriggerParameterKeys[] = { u8"type" };
static const unsigned char* const LayerKeys[] = { u8"name", u8"mask", u8"weight", u8"entry", u8"states", u8"transitions" };
static const unsigned char* const StateKeys[] =
{
    u8"clip", u8"blend", u8"speed", u8"speed_parameter", u8"match_speed", u8"min_rate", u8"max_rate", u8"is_looped",
    u8"look_at", u8"events"
};
static const unsigned char* const BlendKeys[] = { u8"parameter", u8"points" };
static const unsigned char* const PointKeys[] = { u8"clip", u8"at" };
static const unsigned char* const EventKeys[] = { u8"name", u8"time", u8"phase", u8"attach", u8"grip", u8"detach", u8"set" };
static const unsigned char* const TransitionKeys[] =
{
    u8"from", u8"to", u8"when", u8"exit_time", u8"fade_seconds", u8"restarts"
};
static const unsigned char* const ParameterConditionKeys[] =
{
    u8"parameter", u8"above", u8"below", u8"at_least", u8"at_most", u8"is"
};
static const unsigned char* const EventConditionKeys[] = { u8"event" };
static const unsigned char* const SocketConditionKeys[] = { u8"socket", u8"is_holding" };
static const unsigned char* const LookAtKeys[] =
{
    u8"bones", u8"max_yaw", u8"max_pitch_up", u8"max_pitch_down", u8"turn_speed"
};
static const unsigned char* const LookBoneKeys[] = { u8"bone", u8"share" };
static const unsigned char* const ActionKeys[] =
{
    u8"layer", u8"states", u8"fade_seconds", u8"movement", u8"target", u8"anchor"
};
static const unsigned char* const AnchorKeys[] = { u8"socket", u8"max_distance", u8"approach_seconds" };

static const ConditionKeys CompareKeys[] =
{
    { u8"above", AnimationGraphCompare_Above },
    { u8"below", AnimationGraphCompare_Below },
    { u8"at_least", AnimationGraphCompare_AtLeast },
    { u8"at_most", AnimationGraphCompare_AtMost },
};


// Static functions: messages.
/* snprintf for paths in messages: a path cut short only shortens a message. */
static void FormatPath(char* buffer, size_t capacity, const char* format, ...)
{
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(buffer, capacity, format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        buffer[0] = '\0';
    }
}

/* An InvalidAssetDefinition error: "Animation graph \"<name>\": " and the formatted text. */
static Error Fail(const Builder* builder, const char* format, ...)
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
    return Error_Construct4(ErrorCode_InvalidAssetDefinition, "Animation graph \"%s\": %s", builder->GraphName, Text);
}

/* The message of an error from a helper, wrapped with where it happened; the inner error is deconstructed. */
static Error FailWith(const Builder* builder, const char* where, Error* inner)
{
    char Inner[MESSAGE_CAPACITY];
    FormatPath(Inner, sizeof(Inner), "%s", (inner->Message != NULL) ? (const char*)inner->Message : "invalid value");
    Error_Deconstruct(inner);
    return Fail(builder, "%s: %s", where, Inner);
}

static const char* Printable(const unsigned char* text, char* buffer, size_t capacity)
{
    AssetJSON_CopyPrintable(text, buffer, capacity);
    return buffer;
}

/* "where" for a key under a place ("layers[0].states.walk" and ".speed"). */
static const char* At(char* buffer, size_t capacity, const char* where, const char* key)
{
    FormatPath(buffer, capacity, "%s.%s", where, key);
    return buffer;
}


// Static functions: the builder.
static void Builder_Construct(Builder* self, const unsigned char* graphName)
{
    Memory_Zero(self, sizeof(*self));
    AssetJSON_CopyPrintable(graphName, self->GraphName, sizeof(self->GraphName));
    self->FadeSeconds = ANIMATION_GRAPH_DEFAULT_FADE_SECONDS;
    GenericBuffer_AllocateVariable(&self->Parameters, 4U, sizeof(AnimationGraphParameter));
    GenericBuffer_AllocateVariable(&self->Layers, 2U, sizeof(AnimationGraphLayer));
    GenericBuffer_AllocateVariable(&self->States, 16U, sizeof(AnimationGraphState));
    GenericBuffer_AllocateVariable(&self->Transitions, 16U, sizeof(AnimationGraphTransition));
    GenericBuffer_AllocateVariable(&self->Conditions, 16U, sizeof(AnimationGraphCondition));
    GenericBuffer_AllocateVariable(&self->Points, 8U, sizeof(AnimationGraphBlendPoint));
    GenericBuffer_AllocateVariable(&self->Events, 8U, sizeof(AnimationGraphEventRecord));
    GenericBuffer_AllocateVariable(&self->Sets, 4U, sizeof(AnimationGraphParameterSet));
    GenericBuffer_AllocateVariable(&self->Actions, 4U, sizeof(AnimationGraphAction));
    GenericBuffer_AllocateVariable(&self->ActionStates, 8U, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&self->MaskBones, 4U, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&self->LookBones, 2U, sizeof(AnimationGraphLookBone));
    GenericBuffer_AllocateVariable(&self->Strings, 256U, sizeof(unsigned char));
}

static void Builder_Deconstruct(Builder* self)
{
    GenericBuffer* Buffers[] =
    {
        &self->Parameters, &self->Layers, &self->States, &self->Transitions, &self->Conditions, &self->Points,
        &self->Events, &self->Sets, &self->Actions, &self->ActionStates, &self->MaskBones, &self->LookBones,
        &self->Strings
    };
    for (size_t Index = 0U; Index < KEY_COUNT(Buffers); Index++)
    {
        Memory_Free(Buffers[Index]->_data);
    }
}

/* Appends a record; false when the buffer could not grow. */
static Error Add(const Builder* builder, GenericBuffer* buffer, void* record)
{
    if (!GenericBuffer_AddLast(buffer, record))
    {
        return Error_Construct4(ErrorCode_BufferTooSmall, "Animation graph \"%s\": could not grow a buffer.",
            builder->GraphName);
    }
    return Error_CreateSuccess();
}

/* Appends a NUL-terminated string to the string area and returns its offset. */
static Error AddString(Builder* self, const unsigned char* text, uint32_t* outOffset)
{
    size_t Length = StringUTF8_GetByteLength(text);
    *outOffset = (uint32_t)self->Strings._count;
    if (!GenericBuffer_AppendRangeBytes(&self->Strings, (unsigned char*)text, Length)
        || !GenericBuffer_AppendByte(&self->Strings, (unsigned char)u8'\0'))
    {
        return Error_Construct4(ErrorCode_BufferTooSmall, "Animation graph \"%s\": could not grow its strings.",
            self->GraphName);
    }
    return Error_CreateSuccess();
}

static const unsigned char* GetString(const Builder* self, uint32_t offset)
{
    return (const unsigned char*)self->Strings._data + offset;
}

static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}

static AnimationGraphState* GetState(Builder* self, uint32_t index)
{
    return GenericBuffer_GetPointerToElement(&self->States, index);
}


// Static functions: JSON values.
/* An optional value of one JSON type: found or not, refused when present with another type. */
static Error GetOptional(const Builder* builder, JSONCompound* compound, const char* key, JSONValueType type,
    const char* what, const char* where, JSONObjectValue* outValue, bool* outFound)
{
    Error Result = JSONCompound_GetOptionalVerified(compound, (const unsigned char*)key, type, outValue, outFound);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(builder, "%s.%s must be %s.", where, key, what);
    }
    return Error_CreateSuccess();
}

/* A required value of one JSON type. */
static Error GetRequired(const Builder* builder, JSONCompound* compound, const char* key, JSONValueType type,
    const char* what, const char* where, JSONObjectValue* outValue)
{
    bool Found = false;
    Error Result = GetOptional(builder, compound, key, type, what, where, outValue, &Found);
    if ((Result.Code == ErrorCode_Success) && !Found)
    {
        return Fail(builder, "%s needs \"%s\": %s.", where, key, what);
    }
    return Result;
}

/* A value as an owned string (Memory_Free it), or an error naming @p where. */
static Error ToOwnedString(const Builder* builder, const JSONObjectValue* value, const char* what, const char* where,
    unsigned char** outText)
{
    *outText = NULL;
    if (value->Type != JSONValueType_String)
    {
        return Fail(builder, "%s must be %s.", where, what);
    }
    Error Result = GameJSON_ValueToOwnedString(value, outText);
    if (Result.Code != ErrorCode_Success)
    {
        return FailWith(builder, where, &Result);
    }
    return Error_CreateSuccess();
}

/* Whether a text is a name of the graph's kind ([a-z][a-z0-9_.-]*, at most @p maxLength bytes). */
static bool IsValidName(const unsigned char* text, size_t maxLength)
{
    return AssetManager_IsValidAssetName(text) && (StringUTF8_GetByteLength(text) <= maxLength);
}

/* A value as a name of the graph's kind, appended to the strings: its offset. */
static Error ReadName(Builder* builder, const JSONObjectValue* value, size_t maxLength, const char* what,
    const char* where, uint32_t* outOffset)
{
    unsigned char* Text = NULL;
    Error Result = ToOwnedString(builder, value, what, where, &Text);
    if ((Result.Code == ErrorCode_Success) && !IsValidName(Text, maxLength))
    {
        char Shown[NAME_PRINT_CAPACITY];
        Result = Fail(builder, "%s \"%s\" is not %s ([a-z][a-z0-9_.-]*, at most %" PRIu64 " bytes).", where,
            Printable(Text, Shown, sizeof(Shown)), what, (uint64_t)maxLength);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddString(builder, Text, outOffset);
    }
    Memory_Free(Text);
    return Result;
}

/* A key's text checked as a name of the graph's kind (the keys of "parameters", "states" and "actions"). */
static Error CheckKeyName(const Builder* builder, const unsigned char* key, const char* what, const char* where)
{
    if (IsValidName(key, ANIMATION_GRAPH_NAME_MAX_LENGTH))
    {
        return Error_CreateSuccess();
    }
    char Shown[NAME_PRINT_CAPACITY];
    return Fail(builder, "%s: \"%s\" is not a valid %s name ([a-z][a-z0-9_.-]*, at most %" PRIu64 " bytes).", where,
        Printable(key, Shown, sizeof(Shown)), what, (uint64_t)ANIMATION_GRAPH_NAME_MAX_LENGTH);
}

/* A bone's name: UTF-8 of 1 to ANIMATION_GRAPH_BONE_NAME_MAX_LENGTH bytes, as skeletons keep them. */
static Error ReadBoneName(Builder* builder, const JSONObjectValue* value, const char* where, uint32_t* outOffset)
{
    unsigned char* Text = NULL;
    Error Result = ToOwnedString(builder, value, "a bone's name", where, &Text);
    if (Result.Code == ErrorCode_Success)
    {
        size_t Length = StringUTF8_GetByteLength(Text);
        if ((Length == 0U) || (Length > ANIMATION_GRAPH_BONE_NAME_MAX_LENGTH) || !StringUTF8_IsEncodingValid(Text))
        {
            Result = Fail(builder, "%s must be a bone's name: UTF-8 of 1 to %" PRIu64 " bytes (skeletons keep that many).",
                where, (uint64_t)ANIMATION_GRAPH_BONE_NAME_MAX_LENGTH);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddString(builder, Text, outOffset);
    }
    Memory_Free(Text);
    return Result;
}

/* An optional number within [minimum, maximum] (an open lower end when @p isAboveMinimum). */
static Error ReadNumber(const Builder* builder, JSONCompound* compound, const char* key, float defaultValue,
    float minimum, float maximum, bool isAboveMinimum, const char* where, float* outValue, bool* outFound)
{
    char Path[WHERE_CAPACITY];
    *outValue = defaultValue;
    bool Found = false;
    Error Result = AssetJSON_ReadOptionalFloat(compound, (const unsigned char*)key, defaultValue, outValue, &Found);
    if (outFound != NULL)
    {
        *outFound = Found;
    }
    if (Result.Code != ErrorCode_Success)
    {
        return FailWith(builder, At(Path, sizeof(Path), where, key), &Result);
    }
    bool IsBelow = isAboveMinimum ? !(*outValue > minimum) : !(*outValue >= minimum);
    if (Found && (IsBelow || (*outValue > maximum)))
    {
        return Fail(builder, "%s is %g; it must be %s %g and at most %g.", At(Path, sizeof(Path), where, key),
            (double)*outValue, isAboveMinimum ? "above" : "at least", (double)minimum, (double)maximum);
    }
    return Error_CreateSuccess();
}

/* An optional boolean. */
static Error ReadBoolean(const Builder* builder, JSONCompound* compound, const char* key, bool defaultValue,
    const char* where, bool* outValue, bool* outFound)
{
    JSONObjectValue Value;
    bool Found = false;
    *outValue = defaultValue;
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)key, &Value, &Found);
    if ((Result.Code == ErrorCode_Success) && Found)
    {
        Result = GameJSON_ValueToBoolean(&Value, outValue);
    }
    if (outFound != NULL)
    {
        *outFound = Found;
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Fail(builder, "%s.%s must be true or false.", where, key);
    }
    return Error_CreateSuccess();
}

/* Refuses keys outside a list. */
static Error RejectUnknown(JSONCompound* compound, const unsigned char* const* keys, size_t keyCount, const Builder* builder,
    const char* where)
{
    char Where[WHERE_CAPACITY];
    FormatPath(Where, sizeof(Where), "animation graph \"%s\", %s", builder->GraphName, where);
    return AssetJSON_RejectUnknownKeys(compound, keys, keyCount, Where);
}

/* Visits every entry of a compound in its order: (*visit)(context, key, value, index). */
typedef Error (*EntryVisitor)(Builder* builder, void* context, const unsigned char* key, const JSONObjectValue* value,
    size_t index);

static Error ForEachEntry(Builder* builder, JSONCompound* compound, const char* where, EntryVisitor visit, void* context)
{
    ICollection* Entries = JSONCompound_GetEntryCollection(compound);
    CollectionEnumerator* Enumerator = (Entries != NULL) ? ICollection_CreateEnumerator(Entries) : NULL;
    if (Enumerator == NULL)
    {
        return Error_Construct4(ErrorCode_BufferTooSmall, "Animation graph \"%s\": could not enumerate %s.",
            builder->GraphName, where);
    }
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && HasNext; Index++)
    {
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = visit(builder, context, Entry.Key, &Entry.Value, Index);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        }
    }
    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}


// Static functions: parameters.
/* Finds a parameter (built-in, or a custom float or boolean) named by a value: its index. */
static Error ResolveParameter(Builder* builder, const JSONObjectValue* value, const char* where, uint32_t* outIndex,
    AnimationGraphValueType* outType)
{
    unsigned char* Text = NULL;
    Error Result = ToOwnedString(builder, value, "a parameter's name", where, &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsFound = false;
    for (uint32_t Index = 0U; (Index < (uint32_t)AnimationGraphBuiltIn_Count) && !IsFound; Index++)
    {
        if (AreNamesEqual(AnimationGraph_GetBuiltInName((AnimationGraphBuiltIn)Index), Text))
        {
            *outIndex = Index;
            *outType = AnimationGraph_IsBuiltInBoolean((AnimationGraphBuiltIn)Index) ? AnimationGraphValueType_Bool
                : AnimationGraphValueType_Float;
            IsFound = true;
        }
    }
    for (size_t Index = 0U; (Index < builder->Parameters._count) && !IsFound; Index++)
    {
        const AnimationGraphParameter* Parameter = GenericBuffer_GetPointerToElement(&builder->Parameters, Index);
        if ((Parameter->_type != AnimationGraphValueType_Trigger) && AreNamesEqual(GetString(builder, Parameter->_name),
            Text))
        {
            *outIndex = Parameter->_index;
            *outType = Parameter->_type;
            IsFound = true;
        }
    }
    if (!IsFound)
    {
        char Shown[NAME_PRINT_CAPACITY];
        Result = Fail(builder, "%s names \"%s\", which is neither a built-in parameter nor a float or bool of "
            "\"parameters\".", where, Printable(Text, Shown, sizeof(Shown)));
    }
    Memory_Free(Text);
    return Result;
}

/* Whether a name is taken by a built-in parameter or event. */
static bool IsBuiltInName(const unsigned char* name)
{
    for (uint32_t Index = 0U; Index < (uint32_t)AnimationGraphBuiltIn_Count; Index++)
    {
        if (AreNamesEqual(AnimationGraph_GetBuiltInName((AnimationGraphBuiltIn)Index), name))
        {
            return true;
        }
    }
    for (uint32_t Index = 0U; Index < (uint32_t)AnimationGraphEvent_Count; Index++)
    {
        if (AreNamesEqual(AnimationGraph_GetBuiltInEventName((AnimationGraphEvent)Index), name))
        {
            return true;
        }
    }
    return false;
}

/* One entry of "parameters": "name": { "type": "float" | "bool" | "trigger", "default": ..., "min": ..., "max": ... }. */
static Error ParseParameter(Builder* builder, void* context, const unsigned char* key, const JSONObjectValue* value,
    size_t index)
{
    (void)context;
    (void)index;
    char Where[WHERE_CAPACITY];
    char Shown[NAME_PRINT_CAPACITY];
    FormatPath(Where, sizeof(Where), "parameters.%s", Printable(key, Shown, sizeof(Shown)));
    Error Result = CheckKeyName(builder, key, "parameter", "parameters");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (IsBuiltInName(key))
    {
        return Fail(builder, "%s: \"%s\" is a built-in parameter or event; a custom parameter needs a name of its own.",
            Where, Shown);
    }
    if (builder->Parameters._count >= ANIMATION_GRAPH_MAX_PARAMETERS)
    {
        return Fail(builder, "\"parameters\" declares more than %u parameters.", (unsigned)ANIMATION_GRAPH_MAX_PARAMETERS);
    }
    if (value->Type != JSONValueType_Compound)
    {
        return Fail(builder, "%s must be an object: { \"type\": \"float\" | \"bool\" | \"trigger\", ... }.", Where);
    }
    JSONCompound* Compound = value->Value.Compound;
    JSONObjectValue TypeValue;
    unsigned char* Type = NULL;
    Result = GetRequired(builder, Compound, "type", JSONValueType_String, "\"float\", \"bool\" or \"trigger\"", Where,
        &TypeValue);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ToOwnedString(builder, &TypeValue, "\"float\", \"bool\" or \"trigger\"", Where, &Type);
    }
    AnimationGraphParameter Parameter;
    Memory_Zero(&Parameter, sizeof(Parameter));
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddString(builder, key, &Parameter._name);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Type);
        return Result;
    }

    if (AreNamesEqual(Type, (const unsigned char*)u8"float"))
    {
        Parameter._type = AnimationGraphValueType_Float;
        Result = RejectUnknown(Compound, FloatParameterKeys, KEY_COUNT(FloatParameterKeys), builder, Where);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNumber(builder, Compound, "min", -ANIMATION_GRAPH_MAX_VALUE, -ANIMATION_GRAPH_MAX_VALUE,
                ANIMATION_GRAPH_MAX_VALUE, false, Where, &Parameter._min, NULL);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNumber(builder, Compound, "max", ANIMATION_GRAPH_MAX_VALUE, -ANIMATION_GRAPH_MAX_VALUE,
                ANIMATION_GRAPH_MAX_VALUE, false, Where, &Parameter._max, NULL);
        }
        if ((Result.Code == ErrorCode_Success) && (Parameter._min > Parameter._max))
        {
            Result = Fail(builder, "%s: min (%g) is above max (%g).", Where, (double)Parameter._min,
                (double)Parameter._max);
        }
        // Without a default it starts at 0, or at the bound nearest to 0.
        float Start = (0.0f < Parameter._min) ? Parameter._min : ((0.0f > Parameter._max) ? Parameter._max : 0.0f);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNumber(builder, Compound, "default", Start, Parameter._min, Parameter._max, false, Where,
                &Parameter._default, NULL);
        }
        Parameter._index = (uint32_t)AnimationGraphBuiltIn_Count + builder->ValueParameterCount;
        builder->ValueParameterCount++;
    }
    else if (AreNamesEqual(Type, (const unsigned char*)u8"bool"))
    {
        Parameter._type = AnimationGraphValueType_Bool;
        bool Default = false;
        Result = RejectUnknown(Compound, BoolParameterKeys, KEY_COUNT(BoolParameterKeys), builder, Where);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadBoolean(builder, Compound, "default", false, Where, &Default, NULL);
        }
        Parameter._default = Default ? 1.0f : 0.0f;
        Parameter._min = 0.0f;
        Parameter._max = 1.0f;
        Parameter._index = (uint32_t)AnimationGraphBuiltIn_Count + builder->ValueParameterCount;
        builder->ValueParameterCount++;
    }
    else if (AreNamesEqual(Type, (const unsigned char*)u8"trigger"))
    {
        Parameter._type = AnimationGraphValueType_Trigger;
        Result = RejectUnknown(Compound, TriggerParameterKeys, KEY_COUNT(TriggerParameterKeys), builder, Where);
        if ((Result.Code == ErrorCode_Success) && (builder->TriggerCount >= ANIMATION_GRAPH_MAX_TRIGGERS))
        {
            Result = Fail(builder, "\"parameters\" declares more than %u triggers.", (unsigned)ANIMATION_GRAPH_MAX_TRIGGERS);
        }
        Parameter._index = (uint32_t)AnimationGraphEvent_Count + builder->TriggerCount;
        builder->TriggerCount++;
    }
    else
    {
        Result = Fail(builder, "%s.type must be \"float\", \"bool\" or \"trigger\".", Where);
    }
    Memory_Free(Type);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Add(builder, &builder->Parameters, &Parameter);
    }
    return Result;
}

static Error ParseParameters(Builder* builder, JSONCompound* root)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = GetOptional(builder, root, "parameters", JSONValueType_Compound, "an object of parameters",
        "the definition", &Value, &Found);
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    return ForEachEntry(builder, Value.Value.Compound, "\"parameters\"", ParseParameter, NULL);
}


// Static functions: events.
/* A socket's name from a value: [a-z][a-z0-9_.-]*, appended to the strings. */
static Error ReadSocketName(Builder* builder, JSONCompound* compound, const char* key, const char* where,
    uint32_t* outOffset, bool* outFound)
{
    JSONObjectValue Value;
    char Path[WHERE_CAPACITY];
    *outOffset = ANIMATION_GRAPH_NONE;
    Error Result = GetOptional(builder, compound, key, JSONValueType_String, "a socket's name", where, &Value, outFound);
    if ((Result.Code == ErrorCode_Success) && *outFound)
    {
        Result = ReadName(builder, &Value, ANIMATION_GRAPH_SOCKET_NAME_MAX_LENGTH, "a socket's name",
            At(Path, sizeof(Path), where, key), outOffset);
    }
    return Result;
}

/* One entry of an event's "set": "parameter": value (a custom float within its bounds, or a custom bool). */
static Error ParseSet(Builder* builder, void* context, const unsigned char* key, const JSONObjectValue* value,
    size_t index)
{
    const char* Where = context;
    char Shown[NAME_PRINT_CAPACITY];
    Printable(key, Shown, sizeof(Shown));
    if (index >= ANIMATION_GRAPH_MAX_EVENT_SETS)
    {
        return Fail(builder, "%s.set sets more than %u parameters.", Where, (unsigned)ANIMATION_GRAPH_MAX_EVENT_SETS);
    }
    const AnimationGraphParameter* Parameter = NULL;
    for (size_t Position = 0U; (Position < builder->Parameters._count) && (Parameter == NULL); Position++)
    {
        const AnimationGraphParameter* Candidate = (const AnimationGraphParameter*)builder->Parameters._data + Position;
        if (AreNamesEqual(GetString(builder, Candidate->_name), key))
        {
            Parameter = Candidate;
        }
    }
    if ((Parameter == NULL) || (Parameter->_type == AnimationGraphValueType_Trigger))
    {
        return Fail(builder, "%s.set names \"%s\", which is not a custom float or bool parameter.", Where, Shown);
    }
    AnimationGraphParameterSet Set = { ._parameter = Parameter->_index, ._value = 0.0f };
    if (Parameter->_type == AnimationGraphValueType_Bool)
    {
        bool IsTrue = false;
        Error Result = GameJSON_ValueToBoolean(value, &IsTrue);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(builder, "%s.set.%s must be true or false.", Where, Shown);
        }
        Set._value = IsTrue ? 1.0f : 0.0f;
    }
    else
    {
        Error Result = AssetJSON_ValueToFloat(value, &Set._value);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(builder, "%s.set.%s must be a number.", Where, Shown);
        }
        if ((Set._value < Parameter->_min) || (Set._value > Parameter->_max))
        {
            return Fail(builder, "%s.set.%s is %g; the parameter takes %g to %g.", Where, Shown, (double)Set._value,
                (double)Parameter->_min, (double)Parameter->_max);
        }
    }
    return Add(builder, &builder->Sets, &Set);
}

/* One event of a state: { "name", "time" (seconds into a one-clip state's clip) or "phase" (a share of its cycle),
 * "attach", "grip", "detach", "set" }. */
static Error ParseEvent(Builder* builder, JSONCompound* compound, AnimationGraphStateKind kind, const char* where)
{
    Error Result = RejectUnknown(compound, EventKeys, KEY_COUNT(EventKeys), builder, where);
    AnimationGraphEventRecord Event;
    Memory_Zero(&Event, sizeof(Event));
    JSONObjectValue NameValue;
    char Path[WHERE_CAPACITY];
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, compound, "name", JSONValueType_String, "the event's name", where, &NameValue);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadName(builder, &NameValue, ANIMATION_GRAPH_NAME_MAX_LENGTH, "an event's name",
            At(Path, sizeof(Path), where, "name"), &Event._name);
    }
    bool HasTime = false;
    bool HasPhase = false;
    float Time = 0.0f;
    float Phase = 0.0f;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, compound, "time", 0.0f, 0.0f, ANIMATION_GRAPH_MAX_SECONDS, false, where, &Time,
            &HasTime);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, compound, "phase", 0.0f, 0.0f, 1.0f, false, where, &Phase, &HasPhase);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (HasTime == HasPhase)
    {
        return Fail(builder, "%s needs either \"time\" (seconds into the clip) or \"phase\" (a share of the cycle).", where);
    }
    if (HasPhase && !(Phase < 1.0f))
    {
        return Fail(builder, "%s.phase must be below 1 (a share of the cycle, 0 its start).", where);
    }
    if (HasTime && (kind != AnimationGraphStateKind_Clip))
    {
        return Fail(builder, "%s: \"time\" needs a one-clip state; a blend's events take a \"phase\".", where);
    }
    Event._isPhase = HasPhase;
    Event._at = HasPhase ? Phase : Time;

    bool HasAttach = false;
    bool HasGrip = false;
    bool HasDetach = false;
    Result = ReadSocketName(builder, compound, "attach", where, &Event._attachSocket, &HasAttach);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadSocketName(builder, compound, "grip", where, &Event._gripSocket, &HasGrip);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadSocketName(builder, compound, "detach", where, &Event._detachSocket, &HasDetach);
    }
    if ((Result.Code == ErrorCode_Success) && HasGrip && !HasAttach)
    {
        Result = Fail(builder, "%s: \"grip\" names the target's socket that lands on \"attach\"; it needs \"attach\".",
            where);
    }
    JSONObjectValue SetValue;
    bool HasSet = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptional(builder, compound, "set", JSONValueType_Compound, "an object of parameters and values", where,
            &SetValue, &HasSet);
    }
    Event._firstSet = (uint32_t)builder->Sets._count;
    if ((Result.Code == ErrorCode_Success) && HasSet)
    {
        Result = ForEachEntry(builder, SetValue.Value.Compound, where, ParseSet, (void*)where);
    }
    Event._setCount = (uint32_t)builder->Sets._count - Event._firstSet;
    if (Result.Code == ErrorCode_Success)
    {
        Result = Add(builder, &builder->Events, &Event);
    }
    return Result;
}

static Error ParseEvents(Builder* builder, JSONCompound* state, AnimationGraphState* record, const char* where)
{
    JSONObjectValue Value;
    bool Found = false;
    record->_firstEvent = (uint32_t)builder->Events._count;
    record->_eventCount = 0U;
    Error Result = GetOptional(builder, state, "events", JSONValueType_Array, "an array of events", where, &Value, &Found);
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    if (Count > ANIMATION_GRAPH_MAX_EVENTS)
    {
        return Fail(builder, "%s.events holds %" PRIu64 " events; a state fires at most %u.", where, (uint64_t)Count,
            (unsigned)ANIMATION_GRAPH_MAX_EVENTS);
    }
    if ((Count > 0U) && (record->_kind == AnimationGraphStateKind_Empty))
    {
        return Fail(builder, "%s: an empty state plays nothing, so it has no events.", where);
    }
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        char Path[WHERE_CAPACITY];
        FormatPath(Path, sizeof(Path), "%s.events[%" PRIu64 "]", where, (uint64_t)Index);
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Value.Value.Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(builder, "%s must be an object.", Path);
        }
        Result = ParseEvent(builder, Element.Value.Compound, record->_kind, Path);
    }
    record->_eventCount = (uint32_t)builder->Events._count - record->_firstEvent;
    return Result;
}


// Static functions: states.
/* A clip's name from a value: a valid clip name, appended to the strings. */
static Error ReadClipName(Builder* builder, const JSONObjectValue* value, const char* where, uint32_t* outOffset)
{
    return ReadName(builder, value, ANIMATION_CLIP_NAME_MAX_LENGTH, "a clip's name", where, outOffset);
}

/* "blend": { "parameter": ..., "points": [ { "clip": ..., "at": ... }, ... ] }, the points rising strictly. */
static Error ParseBlend(Builder* builder, JSONCompound* blend, AnimationGraphState* record, const char* where)
{
    Error Result = RejectUnknown(blend, BlendKeys, KEY_COUNT(BlendKeys), builder, where);
    JSONObjectValue Value;
    AnimationGraphValueType Type = AnimationGraphValueType_Float;
    char Path[WHERE_CAPACITY];
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, blend, "parameter", JSONValueType_String, "a parameter's name", where, &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ResolveParameter(builder, &Value, At(Path, sizeof(Path), where, "parameter"), &record->_blendParameter,
            &Type);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, blend, "points", JSONValueType_Array, "an array of { \"clip\", \"at\" }", where,
            &Value);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    if ((Count < ANIMATION_GRAPH_MIN_BLEND_POINTS) || (Count > ANIMATION_GRAPH_MAX_BLEND_POINTS))
    {
        return Fail(builder, "%s.points holds %" PRIu64 " clips; a blend has %u to %u.", where, (uint64_t)Count,
            (unsigned)ANIMATION_GRAPH_MIN_BLEND_POINTS, (unsigned)ANIMATION_GRAPH_MAX_BLEND_POINTS);
    }
    record->_firstPoint = (uint32_t)builder->Points._count;
    record->_pointCount = (uint32_t)Count;
    float Previous = 0.0f;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        FormatPath(Path, sizeof(Path), "%s.points[%" PRIu64 "]", where, (uint64_t)Index);
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Value.Value.Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(builder, "%s must be an object: { \"clip\": ..., \"at\": ... }.", Path);
        }
        JSONCompound* Point = Element.Value.Compound;
        AnimationGraphBlendPoint Record = { ._clip = 0U, ._at = 0.0f };
        JSONObjectValue ClipValue;
        bool HasAt = false;
        char ClipPath[WHERE_CAPACITY];
        Result = RejectUnknown(Point, PointKeys, KEY_COUNT(PointKeys), builder, Path);
        if (Result.Code == ErrorCode_Success)
        {
            Result = GetRequired(builder, Point, "clip", JSONValueType_String, "a clip's name", Path, &ClipValue);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadClipName(builder, &ClipValue, At(ClipPath, sizeof(ClipPath), Path, "clip"), &Record._clip);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNumber(builder, Point, "at", 0.0f, -ANIMATION_GRAPH_MAX_VALUE, ANIMATION_GRAPH_MAX_VALUE, false,
                Path, &Record._at, &HasAt);
        }
        if ((Result.Code == ErrorCode_Success) && !HasAt)
        {
            Result = Fail(builder, "%s needs \"at\": the parameter's value where the clip plays alone.", Path);
        }
        if ((Result.Code == ErrorCode_Success) && (Index > 0U) && !(Record._at > Previous))
        {
            Result = Fail(builder, "%s.at is %g; the points' values must rise (the one before is %g).", Path,
                (double)Record._at, (double)Previous);
        }
        Previous = Record._at;
        if (Result.Code == ErrorCode_Success)
        {
            Result = Add(builder, &builder->Points, &Record);
        }
    }
    return Result;
}

/* The playback rows of a state: speed, speed_parameter, match_speed with its rate limits, is_looped, look_at. */
static Error ParsePlayback(Builder* builder, JSONCompound* state, AnimationGraphState* record, const char* where)
{
    char Path[WHERE_CAPACITY];
    JSONObjectValue Value;
    bool Found = false;
    AnimationGraphValueType Type = AnimationGraphValueType_Float;
    Error Result = ReadNumber(builder, state, "speed", 1.0f, 0.0f, ANIMATION_GRAPH_MAX_SPEED, false, where,
        &record->_speed, NULL);
    record->_speedParameter = ANIMATION_GRAPH_NONE;
    record->_matchParameter = ANIMATION_GRAPH_NONE;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptional(builder, state, "speed_parameter", JSONValueType_String, "a parameter's name", where, &Value,
            &Found);
    }
    if ((Result.Code == ErrorCode_Success) && Found)
    {
        Result = ResolveParameter(builder, &Value, At(Path, sizeof(Path), where, "speed_parameter"),
            &record->_speedParameter, &Type);
    }
    bool HasMatch = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptional(builder, state, "match_speed", JSONValueType_String, "a parameter's name (metres per second)",
            where, &Value, &HasMatch);
    }
    if ((Result.Code == ErrorCode_Success) && HasMatch)
    {
        Result = (record->_kind == AnimationGraphStateKind_Empty)
            ? Fail(builder, "%s: an empty state plays no clip to match a speed with.", where)
            : ResolveParameter(builder, &Value, At(Path, sizeof(Path), where, "match_speed"), &record->_matchParameter,
                &Type);
    }
    bool HasMin = false;
    bool HasMax = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, state, "min_rate", ANIMATION_GRAPH_DEFAULT_MIN_RATE, 0.0f, ANIMATION_GRAPH_MAX_SPEED,
            false, where, &record->_minRate, &HasMin);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, state, "max_rate", ANIMATION_GRAPH_DEFAULT_MAX_RATE, 0.0f, ANIMATION_GRAPH_MAX_SPEED,
            false, where, &record->_maxRate, &HasMax);
    }
    if ((Result.Code == ErrorCode_Success) && (HasMin || HasMax) && !HasMatch)
    {
        Result = Fail(builder, "%s: \"min_rate\" and \"max_rate\" limit the rate \"match_speed\" sets; it has none.", where);
    }
    if ((Result.Code == ErrorCode_Success) && (record->_minRate > record->_maxRate))
    {
        Result = Fail(builder, "%s: min_rate (%g) is above max_rate (%g).", where, (double)record->_minRate,
            (double)record->_maxRate);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(builder, state, "is_looped", false, where, &record->_isLooped, &record->_overridesIsLooped);
    }
    if ((Result.Code == ErrorCode_Success) && record->_overridesIsLooped
        && (record->_kind == AnimationGraphStateKind_Empty))
    {
        Result = Fail(builder, "%s: an empty state plays no clip to loop.", where);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, state, "look_at", 1.0f, 0.0f, 1.0f, false, where, &record->_lookAtWeight, NULL);
    }
    return Result;
}

/* The layer being read: its index, where its states start, and its place in messages. */
typedef struct LayerContextStruct
{
    uint32_t FirstState;
    uint32_t StateCount;
    char Where[WHERE_CAPACITY];
} LayerContext;

/* One entry of a layer's "states": "name": { "clip": ... | "blend": { ... } | neither (empty), playback, "events" }. */
static Error ParseState(Builder* builder, void* context, const unsigned char* key, const JSONObjectValue* value,
    size_t index)
{
    (void)index;
    LayerContext* Layer = context;
    char Where[WHERE_CAPACITY];
    char Shown[NAME_PRINT_CAPACITY];
    FormatPath(Where, sizeof(Where), "%s.states.%s", Layer->Where, Printable(key, Shown, sizeof(Shown)));
    Error Result = CheckKeyName(builder, key, "state", Layer->Where);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (AreNamesEqual(key, ANY_STATE_NAME))
    {
        return Fail(builder, "%s: \"any\" means every state in a transition's \"from\"; no state may be called so.", Where);
    }
    if (Layer->StateCount >= ANIMATION_GRAPH_MAX_STATES)
    {
        return Fail(builder, "%s has more than %u states.", Layer->Where, (unsigned)ANIMATION_GRAPH_MAX_STATES);
    }
    if (value->Type != JSONValueType_Compound)
    {
        return Fail(builder, "%s must be an object ({} for an empty state).", Where);
    }
    JSONCompound* State = value->Value.Compound;
    AnimationGraphState Record;
    Memory_Zero(&Record, sizeof(Record));
    Record._kind = AnimationGraphStateKind_Empty;
    Record._blendParameter = ANIMATION_GRAPH_NONE;
    Result = RejectUnknown(State, StateKeys, KEY_COUNT(StateKeys), builder, Where);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddString(builder, key, &Record._name);
    }
    JSONObjectValue ClipValue;
    JSONObjectValue BlendValue;
    bool HasClip = false;
    bool HasBlend = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptional(builder, State, "clip", JSONValueType_String, "a clip's name", Where, &ClipValue, &HasClip);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptional(builder, State, "blend", JSONValueType_Compound, "an object { \"parameter\", \"points\" }",
            Where, &BlendValue, &HasBlend);
    }
    if ((Result.Code == ErrorCode_Success) && HasClip && HasBlend)
    {
        Result = Fail(builder, "%s plays either one \"clip\" or a \"blend\", not both.", Where);
    }
    char Path[WHERE_CAPACITY];
    if ((Result.Code == ErrorCode_Success) && HasClip)
    {
        Record._kind = AnimationGraphStateKind_Clip;
        Result = ReadClipName(builder, &ClipValue, At(Path, sizeof(Path), Where, "clip"), &Record._clip);
    }
    if ((Result.Code == ErrorCode_Success) && HasBlend)
    {
        Record._kind = AnimationGraphStateKind_Blend;
        Result = ParseBlend(builder, BlendValue.Value.Compound, &Record, At(Path, sizeof(Path), Where, "blend"));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParsePlayback(builder, State, &Record, Where);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseEvents(builder, State, &Record, Where);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Add(builder, &builder->States, &Record);
        Layer->StateCount++;
    }
    return Result;
}

/* Finds a state of the layer being read by a name value: its index in the layer. */
static Error ResolveState(Builder* builder, const LayerContext* layer, const JSONObjectValue* value, const char* where,
    uint32_t* outIndex)
{
    unsigned char* Text = NULL;
    Error Result = ToOwnedString(builder, value, "a state's name", where, &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (uint32_t Index = 0U; Index < layer->StateCount; Index++)
    {
        if (AreNamesEqual(GetString(builder, GetState(builder, layer->FirstState + Index)->_name), Text))
        {
            *outIndex = Index;
            Memory_Free(Text);
            return Error_CreateSuccess();
        }
    }
    char Shown[NAME_PRINT_CAPACITY];
    Result = Fail(builder, "%s names \"%s\", which is not a state of %s.", where, Printable(Text, Shown, sizeof(Shown)),
        layer->Where);
    Memory_Free(Text);
    return Result;
}


// Static functions: transitions.
/* One condition: { "parameter": ..., "above" | "below" | "at_least" | "at_most": number, or "is": true/false },
 * { "event": ... } or { "socket": ..., "is_holding": true/false }. */
static Error ParseCondition(Builder* builder, JSONCompound* compound, const char* where)
{
    AnimationGraphCondition Condition;
    Memory_Zero(&Condition, sizeof(Condition));
    JSONObjectValue Value;
    JSONObjectValue EventValue;
    JSONObjectValue SocketValue;
    bool HasParameter = false;
    bool HasEvent = false;
    bool HasSocket = false;
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"parameter", &Value, &HasParameter);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"event", &EventValue, &HasEvent);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"socket", &SocketValue, &HasSocket);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return FailWith(builder, where, &Result);
    }
    if (((HasParameter ? 1 : 0) + (HasEvent ? 1 : 0) + (HasSocket ? 1 : 0)) != 1)
    {
        return Fail(builder, "%s tests one thing: a \"parameter\", an \"event\" or a \"socket\".", where);
    }
    char Path[WHERE_CAPACITY];
    if (HasEvent)
    {
        Condition._kind = AnimationGraphConditionKind_Event;
        Result = RejectUnknown(compound, EventConditionKeys, KEY_COUNT(EventConditionKeys), builder, where);
        unsigned char* Text = NULL;
        if (Result.Code == ErrorCode_Success)
        {
            Result = ToOwnedString(builder, &EventValue, "an event's name", At(Path, sizeof(Path), where, "event"),
                &Text);
        }
        if (Result.Code == ErrorCode_Success)
        {
            bool IsFound = false;
            for (uint32_t Index = 0U; (Index < (uint32_t)AnimationGraphEvent_Count) && !IsFound; Index++)
            {
                if (AreNamesEqual(AnimationGraph_GetBuiltInEventName((AnimationGraphEvent)Index), Text))
                {
                    Condition._subject = Index;
                    IsFound = true;
                }
            }
            for (size_t Position = 0U; (Position < builder->Parameters._count) && !IsFound; Position++)
            {
                const AnimationGraphParameter* Parameter = (const AnimationGraphParameter*)builder->Parameters._data
                    + Position;
                if ((Parameter->_type == AnimationGraphValueType_Trigger)
                    && AreNamesEqual(GetString(builder, Parameter->_name), Text))
                {
                    Condition._subject = Parameter->_index;
                    IsFound = true;
                }
            }
            char Shown[NAME_PRINT_CAPACITY];
            Result = IsFound ? Error_CreateSuccess() : Fail(builder, "%s names \"%s\", which is neither a built-in event "
                "nor a trigger of \"parameters\".", Path, Printable(Text, Shown, sizeof(Shown)));
        }
        Memory_Free(Text);
    }
    else if (HasSocket)
    {
        Condition._kind = AnimationGraphConditionKind_Socket;
        bool Found = false;
        bool IsHolding = false;
        bool HasHolding = false;
        Result = RejectUnknown(compound, SocketConditionKeys, KEY_COUNT(SocketConditionKeys), builder, where);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadSocketName(builder, compound, "socket", where, &Condition._subject, &Found);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadBoolean(builder, compound, "is_holding", false, where, &IsHolding, &HasHolding);
        }
        if ((Result.Code == ErrorCode_Success) && !HasHolding)
        {
            Result = Fail(builder, "%s needs \"is_holding\": true or false.", where);
        }
        Condition._value = IsHolding ? 1.0f : 0.0f;
    }
    else
    {
        Condition._kind = AnimationGraphConditionKind_Parameter;
        AnimationGraphValueType Type = AnimationGraphValueType_Float;
        Result = RejectUnknown(compound, ParameterConditionKeys, KEY_COUNT(ParameterConditionKeys), builder, where);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ResolveParameter(builder, &Value, At(Path, sizeof(Path), where, "parameter"), &Condition._subject,
                &Type);
        }
        uint32_t TestCount = 0U;
        for (size_t Index = 0U; (Index < KEY_COUNT(CompareKeys)) && (Result.Code == ErrorCode_Success); Index++)
        {
            bool Found = false;
            float Number = 0.0f;
            Result = ReadNumber(builder, compound, (const char*)CompareKeys[Index].Key, 0.0f, -ANIMATION_GRAPH_MAX_VALUE,
                ANIMATION_GRAPH_MAX_VALUE, false, where, &Number, &Found);
            if (Found)
            {
                Condition._compare = CompareKeys[Index].Compare;
                Condition._value = Number;
                TestCount++;
            }
        }
        bool IsTrue = false;
        bool HasIs = false;
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadBoolean(builder, compound, "is", false, where, &IsTrue, &HasIs);
        }
        if ((Result.Code == ErrorCode_Success) && HasIs)
        {
            Condition._compare = AnimationGraphCompare_Is;
            Condition._value = IsTrue ? 1.0f : 0.0f;
            TestCount++;
            if (Type != AnimationGraphValueType_Bool)
            {
                Result = Fail(builder, "%s: \"is\" tests a bool parameter; compare a float with \"above\", \"below\", "
                    "\"at_least\" or \"at_most\".", where);
            }
        }
        if ((Result.Code == ErrorCode_Success) && (TestCount != 1U))
        {
            Result = Fail(builder, "%s needs exactly one test: \"above\", \"below\", \"at_least\", \"at_most\" or \"is\".",
                where);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Add(builder, &builder->Conditions, &Condition);
    }
    return Result;
}

static Error ParseConditions(Builder* builder, JSONCompound* transition, const char* where, uint32_t* outFirst,
    uint32_t* outCount)
{
    JSONObjectValue Value;
    bool Found = false;
    *outFirst = (uint32_t)builder->Conditions._count;
    *outCount = 0U;
    Error Result = GetOptional(builder, transition, "when", JSONValueType_Array, "an array of conditions", where, &Value,
        &Found);
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    if (Count > ANIMATION_GRAPH_MAX_CONDITIONS)
    {
        return Fail(builder, "%s.when holds %" PRIu64 " conditions; a transition has at most %u.", where, (uint64_t)Count,
            (unsigned)ANIMATION_GRAPH_MAX_CONDITIONS);
    }
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        char Path[WHERE_CAPACITY];
        FormatPath(Path, sizeof(Path), "%s.when[%" PRIu64 "]", where, (uint64_t)Index);
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Value.Value.Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(builder, "%s must be an object.", Path);
        }
        Result = ParseCondition(builder, Element.Value.Compound, Path);
    }
    *outCount = (uint32_t)Count;
    return Result;
}

/* The states a transition leaves: "any", a state, or a list of states (each once, "any" not among them). Writes up to
 * ANIMATION_GRAPH_MAX_STATES indices (ANIMATION_GRAPH_NONE for "any"). */
static Error ParseFrom(Builder* builder, const LayerContext* layer, const JSONObjectValue* value, const char* where,
    uint32_t* outStates, uint32_t* outCount)
{
    *outCount = 0U;
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error Result = ToOwnedString(builder, value, "a state's name", where, &Text);
        bool IsAny = (Result.Code == ErrorCode_Success) && AreNamesEqual(Text, ANY_STATE_NAME);
        Memory_Free(Text);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (IsAny)
        {
            outStates[0] = ANIMATION_GRAPH_NONE;
            *outCount = 1U;
            return Error_CreateSuccess();
        }
        Result = ResolveState(builder, layer, value, where, &outStates[0]);
        *outCount = (Result.Code == ErrorCode_Success) ? 1U : 0U;
        return Result;
    }
    if (value->Type != JSONValueType_Array)
    {
        return Fail(builder, "%s must be \"any\", a state's name or an array of them.", where);
    }
    size_t Count = JSONArray_GetElementCount(value->Value.Array);
    if ((Count == 0U) || (Count > layer->StateCount))
    {
        return Fail(builder, "%s lists %" PRIu64 " states; it needs 1 to the layer's %u.", where, (uint64_t)Count,
            (unsigned)layer->StateCount);
    }
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        char Path[WHERE_CAPACITY];
        FormatPath(Path, sizeof(Path), "%s[%" PRIu64 "]", where, (uint64_t)Index);
        JSONObjectValue Element;
        Result = JSONArray_Get(value->Value.Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return FailWith(builder, Path, &Result);
        }
        uint32_t State = 0U;
        Result = ResolveState(builder, layer, &Element, Path, &State);
        for (uint32_t Earlier = 0U; (Result.Code == ErrorCode_Success) && (Earlier < *outCount); Earlier++)
        {
            if (outStates[Earlier] == State)
            {
                Result = Fail(builder, "%s names a state the list already holds.", Path);
            }
        }
        if (Result.Code == ErrorCode_Success)
        {
            outStates[*outCount] = State;
            (*outCount)++;
        }
    }
    return Result;
}

/* One transition: { "from", "to", "when", "exit_time", "fade_seconds", "restarts" }; one record per state it leaves. */
static Error ParseTransition(Builder* builder, const LayerContext* layer, JSONCompound* compound, const char* where,
    uint32_t* inOutTransitionCount)
{
    Error Result = RejectUnknown(compound, TransitionKeys, KEY_COUNT(TransitionKeys), builder, where);
    AnimationGraphTransition Record;
    Memory_Zero(&Record, sizeof(Record));
    JSONObjectValue Value;
    char Path[WHERE_CAPACITY];
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, compound, "to", JSONValueType_String, "a state's name", where, &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ResolveState(builder, layer, &Value, At(Path, sizeof(Path), where, "to"), &Record._to);
    }
    uint32_t From[ANIMATION_GRAPH_MAX_STATES];
    uint32_t FromCount = 0U;
    bool HasFrom = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"from", &Value, &HasFrom);
        if (Result.Code != ErrorCode_Success)
        {
            Result = FailWith(builder, where, &Result);
        }
        else if (!HasFrom)
        {
            Result = Fail(builder, "%s needs \"from\": \"any\", a state's name or an array of them.", where);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseFrom(builder, layer, &Value, At(Path, sizeof(Path), where, "from"), From, &FromCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseConditions(builder, compound, where, &Record._firstCondition, &Record._conditionCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, compound, "exit_time", 0.0f, 0.0f, MAX_EXIT_TIME, false, where, &Record._exitTime,
            &Record._hasExitTime);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, compound, "fade_seconds", builder->FadeSeconds, 0.0f, ANIMATION_GRAPH_MAX_SECONDS,
            false, where, &Record._fadeSeconds, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBoolean(builder, compound, "restarts", false, where, &Record._isRestarted, NULL);
    }
    for (uint32_t Index = 0U; (Index < FromCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        Record._from = From[Index];
        if (Record._from != ANIMATION_GRAPH_NONE)
        {
            const AnimationGraphState* State = GetState(builder, layer->FirstState + Record._from);
            if (Record._hasExitTime && (State->_kind == AnimationGraphStateKind_Empty))
            {
                Result = Fail(builder, "%s: an exit time waits for the clips of the state it leaves; \"%s\" is empty.",
                    where, (const char*)GetString(builder, State->_name));
                break;
            }
            if ((Record._from == Record._to) && !Record._isRestarted)
            {
                Result = Fail(builder, "%s leads from \"%s\" to itself; that only restarts it (\"restarts\": true).", where,
                    (const char*)GetString(builder, State->_name));
                break;
            }
        }
        if (*inOutTransitionCount >= ANIMATION_GRAPH_MAX_TRANSITIONS)
        {
            Result = Fail(builder, "%s has more than %u transitions (a transition from a list counts once per state).",
                layer->Where, (unsigned)ANIMATION_GRAPH_MAX_TRANSITIONS);
            break;
        }
        Result = Add(builder, &builder->Transitions, &Record);
        (*inOutTransitionCount)++;
    }
    return Result;
}


// Static functions: layers.
static Error ParseMask(Builder* builder, JSONCompound* layer, const char* where, AnimationGraphLayer* record)
{
    JSONObjectValue Value;
    bool Found = false;
    record->_firstMaskBone = (uint32_t)builder->MaskBones._count;
    record->_maskBoneCount = 0U;
    Error Result = GetOptional(builder, layer, "mask", JSONValueType_Array, "an array of bone names", where, &Value,
        &Found);
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    if ((Count == 0U) || (Count > ANIMATION_GRAPH_MAX_MASK_BONES))
    {
        return Fail(builder, "%s.mask names %" PRIu64 " bones; a mask names 1 to %u (leave it out for every bone).", where,
            (uint64_t)Count, (unsigned)ANIMATION_GRAPH_MAX_MASK_BONES);
    }
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        char Path[WHERE_CAPACITY];
        FormatPath(Path, sizeof(Path), "%s.mask[%" PRIu64 "]", where, (uint64_t)Index);
        JSONObjectValue Element;
        uint32_t Offset = 0U;
        Result = JSONArray_Get(Value.Value.Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return FailWith(builder, Path, &Result);
        }
        Result = ReadBoneName(builder, &Element, Path, &Offset);
        if (Result.Code == ErrorCode_Success)
        {
            Result = Add(builder, &builder->MaskBones, &Offset);
        }
    }
    record->_maskBoneCount = (uint32_t)Count;
    return Result;
}

static Error ParseTransitions(Builder* builder, const LayerContext* layer, JSONCompound* compound,
    AnimationGraphLayer* record)
{
    JSONObjectValue Value;
    bool Found = false;
    record->_firstTransition = (uint32_t)builder->Transitions._count;
    record->_transitionCount = 0U;
    Error Result = GetOptional(builder, compound, "transitions", JSONValueType_Array, "an array of transitions",
        layer->Where, &Value, &Found);
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        char Path[WHERE_CAPACITY];
        FormatPath(Path, sizeof(Path), "%s.transitions[%" PRIu64 "]", layer->Where, (uint64_t)Index);
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Value.Value.Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(builder, "%s must be an object.", Path);
        }
        Result = ParseTransition(builder, layer, Element.Value.Compound, Path, &record->_transitionCount);
    }
    return Result;
}

/* One layer: { "name", "mask", "weight", "entry", "states", "transitions" }. */
static Error ParseLayer(Builder* builder, JSONCompound* compound, size_t index)
{
    LayerContext Layer;
    Memory_Zero(&Layer, sizeof(Layer));
    FormatPath(Layer.Where, sizeof(Layer.Where), "layers[%" PRIu64 "]", (uint64_t)index);
    Error Result = RejectUnknown(compound, LayerKeys, KEY_COUNT(LayerKeys), builder, Layer.Where);
    AnimationGraphLayer Record;
    Memory_Zero(&Record, sizeof(Record));
    JSONObjectValue Value;
    char Path[WHERE_CAPACITY];
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, compound, "name", JSONValueType_String, "the layer's name", Layer.Where, &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadName(builder, &Value, ANIMATION_GRAPH_NAME_MAX_LENGTH, "a layer's name",
            At(Path, sizeof(Path), Layer.Where, "name"), &Record._name);
    }
    for (size_t Earlier = 0U; (Result.Code == ErrorCode_Success) && (Earlier < builder->Layers._count); Earlier++)
    {
        const AnimationGraphLayer* Other = (const AnimationGraphLayer*)builder->Layers._data + Earlier;
        if (AreNamesEqual(GetString(builder, Other->_name), GetString(builder, Record._name)))
        {
            Result = Fail(builder, "%s is called \"%s\" as layers[%" PRIu64 "] is.", Layer.Where,
                (const char*)GetString(builder, Record._name), (uint64_t)Earlier);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        // The name in messages from now on.
        char Named[WHERE_CAPACITY];
        FormatPath(Named, sizeof(Named), "%s (\"%s\")", Layer.Where, (const char*)GetString(builder, Record._name));
        FormatPath(Layer.Where, sizeof(Layer.Where), "%s", Named);
        Result = ParseMask(builder, compound, Layer.Where, &Record);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, compound, "weight", 1.0f, 0.0f, 1.0f, false, Layer.Where, &Record._weight, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, compound, "states", JSONValueType_Compound, "an object of states", Layer.Where,
            &Value);
    }
    Layer.FirstState = (uint32_t)builder->States._count;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ForEachEntry(builder, Value.Value.Compound, Layer.Where, ParseState, &Layer);
    }
    if ((Result.Code == ErrorCode_Success) && (Layer.StateCount == 0U))
    {
        Result = Fail(builder, "%s.states holds no state.", Layer.Where);
    }
    Record._firstState = Layer.FirstState;
    Record._stateCount = Layer.StateCount;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, compound, "entry", JSONValueType_String, "the state it starts in", Layer.Where,
            &Value);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ResolveState(builder, &Layer, &Value, At(Path, sizeof(Path), Layer.Where, "entry"), &Record._entryState);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseTransitions(builder, &Layer, compound, &Record);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Add(builder, &builder->Layers, &Record);
    }
    return Result;
}

static Error ParseLayers(Builder* builder, JSONCompound* root)
{
    JSONObjectValue Value;
    Error Result = GetRequired(builder, root, "layers", JSONValueType_Array, "an array of layers", "the definition",
        &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    if ((Count == 0U) || (Count > ANIMATION_GRAPH_MAX_LAYERS))
    {
        return Fail(builder, "\"layers\" holds %" PRIu64 " layers; a graph has 1 to %u.", (uint64_t)Count,
            (unsigned)ANIMATION_GRAPH_MAX_LAYERS);
    }
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Value.Value.Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(builder, "layers[%" PRIu64 "] must be an object.", (uint64_t)Index);
        }
        Result = ParseLayer(builder, Element.Value.Compound, Index);
    }
    return Result;
}


// Static functions: look-at and actions.
/* "look_at": { "bones": [ { "bone", "share" }, ... ], "max_yaw", "max_pitch_up", "max_pitch_down", "turn_speed" }. */
static Error ParseLookAt(Builder* builder, JSONCompound* root)
{
    JSONObjectValue Value;
    bool Found = false;
    const char* Where = "look_at";
    Error Result = GetOptional(builder, root, "look_at", JSONValueType_Compound, "an object", "the definition", &Value,
        &Found);
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    JSONCompound* LookAt = Value.Value.Compound;
    AnimationGraphLookAt* Record = &builder->LookAt;
    Result = RejectUnknown(LookAt, LookAtKeys, KEY_COUNT(LookAtKeys), builder, Where);
    JSONObjectValue Bones;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, LookAt, "bones", JSONValueType_Array, "an array of { \"bone\", \"share\" }", Where,
            &Bones);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Count = JSONArray_GetElementCount(Bones.Value.Array);
    if ((Count == 0U) || (Count > ANIMATION_GRAPH_MAX_LOOK_BONES))
    {
        return Fail(builder, "look_at.bones names %" PRIu64 " bones; look-at turns 1 to %u.", (uint64_t)Count,
            (unsigned)ANIMATION_GRAPH_MAX_LOOK_BONES);
    }
    float ShareSum = 0.0f;
    Record->_firstBone = (uint32_t)builder->LookBones._count;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        char Path[WHERE_CAPACITY];
        char BonePath[WHERE_CAPACITY];
        FormatPath(Path, sizeof(Path), "look_at.bones[%" PRIu64 "]", (uint64_t)Index);
        JSONObjectValue Element;
        Result = JSONArray_GetVerified(Bones.Value.Array, Index, JSONValueType_Compound, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            return Fail(builder, "%s must be an object: { \"bone\": ..., \"share\": ... }.", Path);
        }
        AnimationGraphLookBone Bone = { ._bone = 0U, ._share = 1.0f };
        JSONObjectValue BoneValue;
        Result = RejectUnknown(Element.Value.Compound, LookBoneKeys, KEY_COUNT(LookBoneKeys), builder, Path);
        if (Result.Code == ErrorCode_Success)
        {
            Result = GetRequired(builder, Element.Value.Compound, "bone", JSONValueType_String, "a bone's name", Path,
                &BoneValue);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadBoneName(builder, &BoneValue, At(BonePath, sizeof(BonePath), Path, "bone"), &Bone._bone);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNumber(builder, Element.Value.Compound, "share", 1.0f, 0.0f, 1.0f, true, Path, &Bone._share, NULL);
        }
        ShareSum += Bone._share;
        if (Result.Code == ErrorCode_Success)
        {
            Result = Add(builder, &builder->LookBones, &Bone);
        }
    }
    if ((Result.Code == ErrorCode_Success) && (ShareSum > (1.0f + LOOK_SHARE_TOLERANCE)))
    {
        Result = Fail(builder, "look_at.bones' shares add up to %g; together they turn at most the whole way (1).",
            (double)ShareSum);
    }
    Record->_boneCount = (uint32_t)Count;
    float MaxYaw = ANIMATION_GRAPH_DEFAULT_LOOK_MAX_YAW;
    float MaxUp = ANIMATION_GRAPH_DEFAULT_LOOK_MAX_PITCH_UP;
    float MaxDown = ANIMATION_GRAPH_DEFAULT_LOOK_MAX_PITCH_DOWN;
    float TurnSpeed = ANIMATION_GRAPH_DEFAULT_LOOK_TURN_SPEED;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, LookAt, "max_yaw", MaxYaw, 0.0f, LOOK_MAX_YAW_LIMIT, true, Where, &MaxYaw, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, LookAt, "max_pitch_up", MaxUp, 0.0f, LOOK_MAX_PITCH_LIMIT, false, Where, &MaxUp,
            NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, LookAt, "max_pitch_down", MaxDown, 0.0f, LOOK_MAX_PITCH_LIMIT, false, Where,
            &MaxDown, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, LookAt, "turn_speed", TurnSpeed, 0.0f, ANIMATION_GRAPH_MAX_LOOK_TURN_SPEED, true,
            Where, &TurnSpeed, NULL);
    }
    Record->_isEnabled = true;
    Record->_maxYaw = MaxYaw * DEGREES_TO_RADIANS;
    Record->_maxPitchUp = MaxUp * DEGREES_TO_RADIANS;
    Record->_maxPitchDown = MaxDown * DEGREES_TO_RADIANS;
    Record->_turnSpeed = TurnSpeed * DEGREES_TO_RADIANS;
    return Result;
}

/* A word from a list ("movement", "target"): its position in @p words. */
static Error ReadWord(Builder* builder, JSONCompound* compound, const char* key, const unsigned char* const* words,
    size_t wordCount, uint32_t defaultValue, const char* what, const char* where, uint32_t* outValue, bool* outFound)
{
    JSONObjectValue Value;
    *outValue = defaultValue;
    Error Result = GetOptional(builder, compound, key, JSONValueType_String, what, where, &Value, outFound);
    if ((Result.Code != ErrorCode_Success) || !*outFound)
    {
        return Result;
    }
    unsigned char* Text = NULL;
    char Path[WHERE_CAPACITY];
    Result = ToOwnedString(builder, &Value, what, At(Path, sizeof(Path), where, key), &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (size_t Index = 0U; Index < wordCount; Index++)
    {
        if (AreNamesEqual(words[Index], Text))
        {
            *outValue = (uint32_t)Index;
            Memory_Free(Text);
            return Error_CreateSuccess();
        }
    }
    Memory_Free(Text);
    return Fail(builder, "%s must be %s.", Path, what);
}

/* Whether a state of a layer fires an event that attaches the action's target. */
static bool DoesStateAttach(const Builder* builder, uint32_t layerFirstState, uint32_t state)
{
    const AnimationGraphState* Record = (const AnimationGraphState*)builder->States._data + layerFirstState + state;
    for (uint32_t Index = 0U; Index < Record->_eventCount; Index++)
    {
        const AnimationGraphEventRecord* Event = (const AnimationGraphEventRecord*)builder->Events._data
            + Record->_firstEvent + Index;
        if (Event->_attachSocket != ANIMATION_GRAPH_NONE)
        {
            return true;
        }
    }
    return false;
}

/* An action's "anchor": { "socket", "max_distance", "approach_seconds" }. */
static Error ParseAnchor(Builder* builder, JSONCompound* compound, const char* where, AnimationGraphAction* record)
{
    Error Result = RejectUnknown(compound, AnchorKeys, KEY_COUNT(AnchorKeys), builder, where);
    bool HasSocket = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadSocketName(builder, compound, "socket", where, &record->_anchorSocket, &HasSocket);
    }
    if ((Result.Code == ErrorCode_Success) && !HasSocket)
    {
        Result = Fail(builder, "%s needs \"socket\": the target's socket the character goes to.", where);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, compound, "max_distance", ANIMATION_GRAPH_DEFAULT_ANCHOR_DISTANCE, 0.0f,
            ANIMATION_GRAPH_MAX_ANCHOR_DISTANCE, true, where, &record->_anchorDistance, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, compound, "approach_seconds", ANIMATION_GRAPH_DEFAULT_APPROACH_SECONDS, 0.0f,
            ANIMATION_GRAPH_MAX_SECONDS, false, where, &record->_approachSeconds, NULL);
    }
    record->_hasAnchor = true;
    return Result;
}

/* One entry of "actions": "name": { "layer", "states", "fade_seconds", "movement", "target", "anchor" }. */
static Error ParseAction(Builder* builder, void* context, const unsigned char* key, const JSONObjectValue* value,
    size_t index)
{
    (void)context;
    static const unsigned char* const MovementWords[] = { u8"free", u8"no_moving", u8"no_turning", u8"locked" };
    static const unsigned char* const TargetWords[] = { u8"none", u8"optional", u8"required" };
    char Where[WHERE_CAPACITY];
    char Shown[NAME_PRINT_CAPACITY];
    FormatPath(Where, sizeof(Where), "actions.%s", Printable(key, Shown, sizeof(Shown)));
    Error Result = CheckKeyName(builder, key, "action", "actions");
    if ((Result.Code == ErrorCode_Success) && (index >= ANIMATION_GRAPH_MAX_ACTIONS))
    {
        Result = Fail(builder, "\"actions\" holds more than %u actions.", (unsigned)ANIMATION_GRAPH_MAX_ACTIONS);
    }
    if ((Result.Code == ErrorCode_Success) && (value->Type != JSONValueType_Compound))
    {
        Result = Fail(builder, "%s must be an object.", Where);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    JSONCompound* Compound = value->Value.Compound;
    AnimationGraphAction Record;
    Memory_Zero(&Record, sizeof(Record));
    Record._anchorSocket = ANIMATION_GRAPH_NONE;
    JSONObjectValue Field;
    char Path[WHERE_CAPACITY];
    Result = RejectUnknown(Compound, ActionKeys, KEY_COUNT(ActionKeys), builder, Where);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AddString(builder, key, &Record._name);
    }
    unsigned char* LayerName = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetRequired(builder, Compound, "layer", JSONValueType_String, "a layer's name", Where, &Field);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ToOwnedString(builder, &Field, "a layer's name", At(Path, sizeof(Path), Where, "layer"), &LayerName);
    }
    bool IsLayerFound = false;
    for (size_t Layer = 0U; (Result.Code == ErrorCode_Success) && (Layer < builder->Layers._count) && !IsLayerFound;
        Layer++)
    {
        if (AreNamesEqual(GetString(builder, ((const AnimationGraphLayer*)builder->Layers._data + Layer)->_name),
            LayerName))
        {
            Record._layer = (uint32_t)Layer;
            IsLayerFound = true;
        }
    }
    if ((Result.Code == ErrorCode_Success) && !IsLayerFound)
    {
        Result = Fail(builder, "%s.layer names \"%s\", which is not a layer.", Where,
            Printable(LayerName, Shown, sizeof(Shown)));
    }
    Memory_Free(LayerName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    const AnimationGraphLayer* Layer = (const AnimationGraphLayer*)builder->Layers._data + Record._layer;
    LayerContext Context;
    Memory_Zero(&Context, sizeof(Context));
    Context.FirstState = Layer->_firstState;
    Context.StateCount = Layer->_stateCount;
    FormatPath(Context.Where, sizeof(Context.Where), "layer \"%s\"", (const char*)GetString(builder, Layer->_name));
    Result = GetRequired(builder, Compound, "states", JSONValueType_Array, "an array of the layer's states", Where, &Field);
    size_t Count = (Result.Code == ErrorCode_Success) ? JSONArray_GetElementCount(Field.Value.Array) : 0U;
    if ((Result.Code == ErrorCode_Success) && ((Count == 0U) || (Count > ANIMATION_GRAPH_MAX_ACTION_STATES)))
    {
        Result = Fail(builder, "%s.states names %" PRIu64 " states; an action has 1 to %u.", Where, (uint64_t)Count,
            (unsigned)ANIMATION_GRAPH_MAX_ACTION_STATES);
    }
    bool DoesAttach = false;
    Record._firstState = (uint32_t)builder->ActionStates._count;
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        FormatPath(Path, sizeof(Path), "%s.states[%" PRIu64 "]", Where, (uint64_t)Index);
        JSONObjectValue Element;
        uint32_t State = 0U;
        Result = JSONArray_Get(Field.Value.Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return FailWith(builder, Path, &Result);
        }
        Result = ResolveState(builder, &Context, &Element, Path, &State);
        for (size_t Earlier = 0U; (Result.Code == ErrorCode_Success) && (Earlier < Index); Earlier++)
        {
            if (((const uint32_t*)builder->ActionStates._data)[Record._firstState + Earlier] == State)
            {
                Result = Fail(builder, "%s names a state the list already holds.", Path);
            }
        }
        if (Result.Code == ErrorCode_Success)
        {
            DoesAttach = DoesAttach || DoesStateAttach(builder, Layer->_firstState, State);
            Result = Add(builder, &builder->ActionStates, &State);
        }
    }
    Record._stateCount = (uint32_t)Count;

    uint32_t Movement = 0U;
    uint32_t Target = (uint32_t)AnimationGraphTargetRule_Optional;
    bool HasMovement = false;
    bool HasTarget = false;
    bool HasAnchor = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(builder, Compound, "fade_seconds", builder->FadeSeconds, 0.0f, ANIMATION_GRAPH_MAX_SECONDS,
            false, Where, &Record._fadeSeconds, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadWord(builder, Compound, "movement", MovementWords, KEY_COUNT(MovementWords), 0U,
            "\"free\", \"no_moving\", \"no_turning\" or \"locked\"", Where, &Movement, &HasMovement);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadWord(builder, Compound, "target", TargetWords, KEY_COUNT(TargetWords),
            (uint32_t)AnimationGraphTargetRule_Optional, "\"none\", \"optional\" or \"required\"", Where, &Target,
            &HasTarget);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = GetOptional(builder, Compound, "anchor", JSONValueType_Compound, "an object { \"socket\", ... }", Where,
            &Field, &HasAnchor);
    }
    if ((Result.Code == ErrorCode_Success) && HasAnchor)
    {
        Result = ParseAnchor(builder, Field.Value.Compound, At(Path, sizeof(Path), Where, "anchor"), &Record);
        if ((Result.Code == ErrorCode_Success) && HasTarget && (Target != (uint32_t)AnimationGraphTargetRule_Required))
        {
            Result = Fail(builder, "%s has an anchor on its target, so its target is \"required\".", Where);
        }
        Target = (uint32_t)AnimationGraphTargetRule_Required;
    }
    if ((Result.Code == ErrorCode_Success) && DoesAttach && (Target == (uint32_t)AnimationGraphTargetRule_None))
    {
        Result = Fail(builder, "%s: a state of it attaches the action's target to a socket, so its target cannot be "
            "\"none\".", Where);
    }
    Record._movementLock = (AnimationGraphMovementLock)Movement;
    Record._targetRule = (AnimationGraphTargetRule)Target;
    if (Result.Code == ErrorCode_Success)
    {
        Result = Add(builder, &builder->Actions, &Record);
    }
    return Result;
}

static Error ParseActions(Builder* builder, JSONCompound* root)
{
    JSONObjectValue Value;
    bool Found = false;
    Error Result = GetOptional(builder, root, "actions", JSONValueType_Compound, "an object of actions", "the definition",
        &Value, &Found);
    if ((Result.Code != ErrorCode_Success) || !Found)
    {
        return Result;
    }
    return ForEachEntry(builder, Value.Value.Compound, "\"actions\"", ParseAction, NULL);
}


// Static functions: the block.
static size_t AlignUp(size_t value)
{
    return (value + (BLOCK_ALIGNMENT - 1U)) & ~(BLOCK_ALIGNMENT - 1U);
}

/* Lays the builder's arrays out behind the header in one block. */
static Error Pack(const Builder* builder, AnimationGraph** outGraph)
{
    const GenericBuffer* Arrays[] =
    {
        &builder->Parameters, &builder->Layers, &builder->States, &builder->Transitions, &builder->Conditions,
        &builder->Points, &builder->Events, &builder->Sets, &builder->Actions, &builder->ActionStates,
        &builder->MaskBones, &builder->LookBones, &builder->Strings
    };
    size_t Offsets[KEY_COUNT(Arrays)];
    size_t Offset = AlignUp(sizeof(AnimationGraph));
    for (size_t Index = 0U; Index < KEY_COUNT(Arrays); Index++)
    {
        // Every count is bounded by the limits (at most a few megabytes together), so these sums cannot overflow.
        Offsets[Index] = Offset;
        Offset = AlignUp(Offset + (Arrays[Index]->_count * Arrays[Index]->_elementSize));
    }
    if (Offset > (size_t)UINT32_MAX)
    {
        return Fail(builder, "the graph is too large to keep (%" PRIu64 " bytes).", (uint64_t)Offset);
    }
    unsigned char* Block = Memory_Allocate(Offset);
    Memory_Zero(Block, Offset);
    for (size_t Index = 0U; Index < KEY_COUNT(Arrays); Index++)
    {
        size_t Bytes = Arrays[Index]->_count * Arrays[Index]->_elementSize;
        if (Bytes > 0U)
        {
            Memory_Copy(Arrays[Index]->_data, Block + Offsets[Index], Bytes);
        }
    }
    AnimationGraph* Graph = (AnimationGraph*)(void*)Block;
    Graph->_byteCount = Offset;
    Graph->_fadeSeconds = builder->FadeSeconds;
    Graph->_lookAt = builder->LookAt;
    Graph->_parameterCount = (uint32_t)builder->Parameters._count;
    Graph->_valueParameterCount = builder->ValueParameterCount;
    Graph->_triggerCount = builder->TriggerCount;
    Graph->_layerCount = (uint32_t)builder->Layers._count;
    Graph->_stateCount = (uint32_t)builder->States._count;
    Graph->_transitionCount = (uint32_t)builder->Transitions._count;
    Graph->_conditionCount = (uint32_t)builder->Conditions._count;
    Graph->_pointCount = (uint32_t)builder->Points._count;
    Graph->_eventCount = (uint32_t)builder->Events._count;
    Graph->_setCount = (uint32_t)builder->Sets._count;
    Graph->_actionCount = (uint32_t)builder->Actions._count;
    Graph->_actionStateCount = (uint32_t)builder->ActionStates._count;
    Graph->_maskBoneCount = (uint32_t)builder->MaskBones._count;
    Graph->_lookBoneCount = (uint32_t)builder->LookBones._count;
    Graph->_stringByteCount = (uint32_t)builder->Strings._count;
    Graph->_parametersOffset = (uint32_t)Offsets[0];
    Graph->_layersOffset = (uint32_t)Offsets[1];
    Graph->_statesOffset = (uint32_t)Offsets[2];
    Graph->_transitionsOffset = (uint32_t)Offsets[3];
    Graph->_conditionsOffset = (uint32_t)Offsets[4];
    Graph->_pointsOffset = (uint32_t)Offsets[5];
    Graph->_eventsOffset = (uint32_t)Offsets[6];
    Graph->_setsOffset = (uint32_t)Offsets[7];
    Graph->_actionsOffset = (uint32_t)Offsets[8];
    Graph->_actionStatesOffset = (uint32_t)Offsets[9];
    Graph->_maskBonesOffset = (uint32_t)Offsets[10];
    Graph->_lookBonesOffset = (uint32_t)Offsets[11];
    Graph->_stringsOffset = (uint32_t)Offsets[12];
    *outGraph = Graph;
    return Error_CreateSuccess();
}


// Public functions.
Error AnimationGraph_CreateFromJSON(JSONCompound* root, const unsigned char* graphName, AnimationGraph** outGraph)
{
    if ((root == NULL) || (outGraph == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "AnimationGraph_CreateFromJSON: root and outGraph must not be "
            "NULL.");
    }
    *outGraph = NULL;
    Builder Reader;
    Builder_Construct(&Reader, graphName);
    Error Result = RejectUnknown(root, RootKeys, KEY_COUNT(RootKeys), &Reader, "the definition");
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadNumber(&Reader, root, "fade_seconds", ANIMATION_GRAPH_DEFAULT_FADE_SECONDS, 0.0f,
            ANIMATION_GRAPH_MAX_SECONDS, false, "the definition", &Reader.FadeSeconds, NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseParameters(&Reader, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseLayers(&Reader, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseLookAt(&Reader, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseActions(&Reader, root);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Pack(&Reader, outGraph);
    }
    Builder_Deconstruct(&Reader);
    return Result;
}
