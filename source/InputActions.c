#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "InputActions.h"
#include "AssetDefinitionCommon.h"
#include "NamespacedKey.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The input_actions asset's data, its reader and its registration with the input (references/asset_structure.md,
 * "Input actions"). The reader treats a definition as hostile: every value is checked against its rule while it is read
 * (the display names and descriptions with the input's own rules), unknown keys are refused everywhere, and every
 * message names the path to the bad value.
 */


// Macros.
/** How many bytes of a name from the file a message shows, and room for a message's path and its text. */
#define NAME_PRINT_CAPACITY ((size_t)129)
#define WHERE_CAPACITY ((size_t)256)
#define MESSAGE_CAPACITY ((size_t)384)
/** The only kind an action can have today. */
#define KIND_BUTTON ((const unsigned char*)u8"button")


// Types.
typedef struct ReaderStruct
{
    /** The definition's name, printable, for messages. */
    char Name[NAME_PRINT_CAPACITY];
    /** The definition's full name: the category's key. */
    const unsigned char* FullName;
    /** Bytes of the category's namespace (before the colon). */
    size_t NamespaceLength;
} Reader;

/** Visits one entry of a compound: its key, its value and its place. */
typedef Error (*EntryVisitor)(const Reader* reader, void* context, const unsigned char* key,
    const JSONObjectValue* value, size_t index);


// Fields.
static const unsigned char* const RootKeys[] =
{
    u8"name", u8"display_name", u8"description", u8"order", u8"actions", u8"overrides", u8"category_overrides",
};
static const unsigned char* const ActionKeys[] =
{
    u8"display_name", u8"description", u8"default_bindings", u8"keeps_a_binding", u8"kind",
};
static const unsigned char* const OverrideKeys[] = { u8"display_name", u8"description", u8"default_bindings" };
static const unsigned char* const CategoryOverrideKeys[] = { u8"display_name", u8"description", u8"order" };


// Static functions: messages.
/* An error with "Input actions \"<name>\": <where>: <message>". */
static Error Fail(const Reader* reader, ErrorCode code, const char* where, const char* format, ...)
{
    char Message[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    vsnprintf(Message, sizeof(Message), format, Arguments);
    va_end(Arguments);
    return Error_Construct4(code, "Input actions \"%s\": %s: %s", reader->Name, where, Message);
}

/* The inner error with "Input actions \"<name>\": <where>: " before its message (its code kept); releases it. */
static Error Wrap(const Reader* reader, const char* where, Error* inner)
{
    Error Wrapped = Error_Construct4(inner->Code, "Input actions \"%s\": %s: %s", reader->Name, where,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* Formats a message's path into a buffer, cut to fit. */
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

static Error RejectUnknownKeys(const Reader* reader, JSONCompound* compound, const unsigned char* const* keys,
    size_t keyCount, const char* where)
{
    char Where[WHERE_CAPACITY];
    FormatWhere(Where, sizeof(Where), "input actions \"%s\", %s", reader->Name, where);
    return AssetJSON_RejectUnknownKeys(compound, keys, keyCount, Where);
}

/* Who a source is, for messages: "the engine" or its key. */
static const char* DescribeSource(const GameInput* input, uint32_t sourceKeyId)
{
    if (sourceKeyId == NAMESPACED_KEY_ID_NONE)
    {
        return "the engine";
    }
    const unsigned char* Text = KeyRegistry_GetText(GameInput_GetKeyRegistry(input), sourceKeyId);
    return (Text != NULL) ? (const char*)Text : "an unknown source";
}


// Static functions: values.
/* Visits every entry of a compound in its order. */
static Error ForEachEntry(const Reader* reader, JSONCompound* compound, const char* where, EntryVisitor visit,
    void* context)
{
    ICollection* Entries = JSONCompound_GetEntryCollection(compound);
    CollectionEnumerator* Enumerator = (Entries != NULL) ? ICollection_CreateEnumerator(Entries) : NULL;
    if (Enumerator == NULL)
    {
        return Fail(reader, ErrorCode_BufferTooSmall, where, "could not enumerate its entries.");
    }
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && HasNext; Index++)
    {
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = visit(reader, context, Entry.Key, &Entry.Value, Index);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        }
    }
    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}

/* The compound under an optional key: NULL when the key is absent. */
static Error GetOptionalCompound(const Reader* reader, JSONCompound* compound, const unsigned char* key,
    const char* where, JSONCompound** outCompound)
{
    *outCompound = NULL;
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(compound, key, &Value, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Wrap(reader, where, &Result);
    }
    if (!IsFound)
    {
        return Error_CreateSuccess();
    }
    if (Value.Type != JSONValueType_Compound)
    {
        return Fail(reader, ErrorCode_InvalidJSON, where, "expected an object.");
    }
    *outCompound = Value.Value.Compound;
    return Error_CreateSuccess();
}

/* A display name or a description under an optional key, owned, checked with the input's rules; NULL when absent. */
static Error ReadText(const Reader* reader, JSONCompound* compound, const unsigned char* key, const char* where,
    bool isDisplayName, unsigned char** outText)
{
    *outText = NULL;
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(compound, key, &Value, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Wrap(reader, where, &Result);
    }
    if (!IsFound)
    {
        return Error_CreateSuccess();
    }
    if (Value.Type != JSONValueType_String)
    {
        return Fail(reader, ErrorCode_InvalidJSON, where, "\"%s\" must be a string.", (const char*)key);
    }
    unsigned char* Text = NULL;
    Result = AssetJSON_ValueToOwnedString(&Value, &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Wrap(reader, where, &Result);
    }
    const char* Problem = (StringUTF8_GetByteLength(Text) != Value.Value.String->_count) ? "holds a NUL character"
        : (isDisplayName ? GameInput_DescribeDisplayNameProblem(Text) : GameInput_DescribeDescriptionProblem(Text));
    if (Problem != NULL)
    {
        Memory_Free(Text);
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "\"%s\" %s (at most %" PRIu64 " bytes of UTF-8 "
            "without control characters%s).", (const char*)key, Problem, (uint64_t)(isDisplayName
            ? GAME_INPUT_MAX_DISPLAY_NAME_BYTES : GAME_INPUT_MAX_DESCRIPTION_BYTES), isDisplayName ? ", not empty" : "");
    }
    *outText = Text;
    return Error_CreateSuccess();
}

/* A required display name. */
static Error ReadDisplayName(const Reader* reader, JSONCompound* compound, const char* where, unsigned char** outText)
{
    Error Result = ReadText(reader, compound, (const unsigned char*)u8"display_name", where, true, outText);
    if ((Result.Code == ErrorCode_Success) && (*outText == NULL))
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "\"display_name\" is required.");
    }
    return Result;
}

/* An optional description: an empty one when absent. */
static Error ReadDescription(const Reader* reader, JSONCompound* compound, const char* where, unsigned char** outText)
{
    Error Result = ReadText(reader, compound, (const unsigned char*)u8"description", where, false, outText);
    if ((Result.Code == ErrorCode_Success) && (*outText == NULL))
    {
        *outText = Memory_Allocate(1U);
        (*outText)[0] = 0;
    }
    return Result;
}

/* An optional order: a whole number in the int32 range. */
static Error ReadOrder(const Reader* reader, JSONCompound* compound, const char* where, int32_t* outOrder,
    bool* outFound)
{
    JSONObjectValue Value;
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"order", &Value, outFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Wrap(reader, where, &Result);
    }
    if (!*outFound)
    {
        return Error_CreateSuccess();
    }
    int64_t Order = 0;
    Result = AssetJSON_ReadOptionalInteger(compound, (const unsigned char*)u8"order", 0, &Order);
    if (Result.Code != ErrorCode_Success)
    {
        return Wrap(reader, where, &Result);
    }
    if ((Order < (int64_t)INT32_MIN) || (Order > (int64_t)INT32_MAX))
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "\"order\" %" PRId64 " is out of range (%" PRId32
            " to %" PRId32 ").", Order, INT32_MIN, INT32_MAX);
    }
    *outOrder = (int32_t)Order;
    return Error_CreateSuccess();
}

/* Optional default bindings: a list of at most GAME_INPUT_MAX_BINDINGS_PER_ACTION binding texts. */
static Error ReadBindings(const Reader* reader, JSONCompound* compound, const char* where,
    GameInputBinding outBindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION], size_t* outCount, bool* outFound)
{
    *outCount = 0U;
    JSONObjectValue Value;
    Error Result = JSONCompound_GetOptional(compound, (const unsigned char*)u8"default_bindings", &Value, outFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Wrap(reader, where, &Result);
    }
    if (!*outFound)
    {
        return Error_CreateSuccess();
    }
    if (Value.Type != JSONValueType_Array)
    {
        return Fail(reader, ErrorCode_InvalidJSON, where, "\"default_bindings\" must be a list of bindings.");
    }
    size_t Count = JSONArray_GetElementCount(Value.Value.Array);
    if (Count > GAME_INPUT_MAX_BINDINGS_PER_ACTION)
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "%" PRIu64 " default bindings; at most %" PRIu64
            " are allowed.", (uint64_t)Count, (uint64_t)GAME_INPUT_MAX_BINDINGS_PER_ACTION);
    }
    for (size_t Index = 0U; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Result = JSONArray_Get(Value.Value.Array, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return Wrap(reader, where, &Result);
        }
        const GenericBuffer* Source = (Element.Type == JSONValueType_String) ? Element.Value.String : NULL;
        unsigned char Text[GAME_INPUT_MAX_BINDING_TEXT_BYTES + 1];
        bool IsRead = (Source != NULL) && (Source->_count <= GAME_INPUT_MAX_BINDING_TEXT_BYTES);
        if (IsRead)
        {
            if (Source->_count > 0U)
            {
                Memory_Copy(Source->_data, Text, Source->_count);
            }
            Text[Source->_count] = 0;
            IsRead = (StringUTF8_GetByteLength(Text) == Source->_count)
                && GameInputBinding_TryParse(Text, &outBindings[Index]);
        }
        if (!IsRead)
        {
            return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "default binding %" PRIu64 " is not a binding "
                "(modifiers then a key id, joined by '+'; see references/controls_structure.md).", (uint64_t)Index);
        }
    }
    *outCount = Count;
    return Error_CreateSuccess();
}

/* A key as written in an override ("tbp:use", or "use" for the default namespace's), in full. */
static Error ReadTargetKey(const Reader* reader, const unsigned char* key, const char* where, const char* what,
    unsigned char** outKey)
{
    size_t Length = StringUTF8_GetByteLength(key);
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer FullKey;
    GenericBuffer_CreateConstant(&FullKey, Storage, sizeof(Storage), sizeof(unsigned char), 0);
    Error Result = NamespacedKey_Parse(key, Length, NAMESPACED_KEY_DEFAULT_NAMESPACE, &FullKey);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        const char* Problem = NamespacedKey_DescribeProblem(key, Length, NAMESPACED_KEY_DEFAULT_NAMESPACE);
        return Fail(reader, ErrorCode_InvalidAssetDefinition, where, "it is not %s key: %s.", what,
            (Problem != NULL) ? Problem : "it could not be read");
    }
    size_t FullLength = FullKey._count - 1U;
    *outKey = Memory_Allocate(FullLength + 1U);
    Memory_Copy(Storage, *outKey, FullLength + 1U);
    return Error_CreateSuccess();
}


// Static functions: the parts of a definition.
static Error ReadAction(const Reader* reader, void* context, const unsigned char* key, const JSONObjectValue* value,
    size_t index)
{
    InputActionsAction* Action = &((InputActions*)context)->Actions[index];
    char PrintableName[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(key, PrintableName, sizeof(PrintableName));
    char Where[WHERE_CAPACITY];
    FormatWhere(Where, sizeof(Where), "actions.%s", PrintableName);

    // The action's key: the category's namespace, a colon and its name.
    size_t NameLength = StringUTF8_GetByteLength(key);
    size_t KeyLength = reader->NamespaceLength + 1U + NameLength;
    if (KeyLength > NAMESPACED_KEY_MAX_LENGTH)
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "the action's key would have %" PRIu64
            " bytes; at most %" PRIu64 " are allowed.", (uint64_t)KeyLength, (uint64_t)NAMESPACED_KEY_MAX_LENGTH);
    }
    unsigned char* Key = Memory_Allocate(KeyLength + 1U);
    Memory_Copy(reader->FullName, Key, reader->NamespaceLength + 1U);
    Memory_Copy(key, Key + reader->NamespaceLength + 1U, NameLength + 1U);
    Action->Key = Key;
    const char* Problem = NamespacedKey_DescribeProblem(Key, KeyLength, NULL);
    if (Problem != NULL)
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "the name is not a key path (%s; names use "
            "a-z, 0-9, _ . / -).", Problem);
    }

    if (value->Type != JSONValueType_Compound)
    {
        return Fail(reader, ErrorCode_InvalidJSON, Where, "expected an object.");
    }
    JSONCompound* Compound = value->Value.Compound;
    Error Result = RejectUnknownKeys(reader, Compound, ActionKeys, sizeof(ActionKeys) / sizeof(ActionKeys[0]), Where);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadDisplayName(reader, Compound, Where, &Action->DisplayName);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadDescription(reader, Compound, Where, &Action->Description);
    }
    bool HasBindings = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBindings(reader, Compound, Where, Action->DefaultBindings, &Action->DefaultBindingCount,
            &HasBindings);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadOptionalBoolean(Compound, (const unsigned char*)u8"keeps_a_binding", false,
            &Action->KeepsABinding);
        if (Result.Code != ErrorCode_Success)
        {
            Result = Wrap(reader, Where, &Result);
        }
    }
    if ((Result.Code == ErrorCode_Success) && Action->KeepsABinding && (Action->DefaultBindingCount == 0U))
    {
        Result = Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "an action that keeps a binding needs a default "
            "binding.");
    }

    Action->Kind = GameActionKind_Button;
    JSONObjectValue KindValue;
    bool HasKind = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(Compound, (const unsigned char*)u8"kind", &KindValue, &HasKind);
        if (Result.Code != ErrorCode_Success)
        {
            Result = Wrap(reader, Where, &Result);
        }
    }
    if ((Result.Code == ErrorCode_Success) && HasKind)
    {
        size_t KindLength = StringUTF8_GetByteLength(KIND_BUTTON);
        bool IsButton = (KindValue.Type == JSONValueType_String) && (KindValue.Value.String->_count == KindLength)
            && Memory_IsEqual(KindValue.Value.String->_data, KIND_BUTTON, KindLength);
        if (!IsButton)
        {
            Result = Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "\"kind\" must be \"button\".");
        }
    }
    return Result;
}

static Error ReadOverride(const Reader* reader, void* context, const unsigned char* key, const JSONObjectValue* value,
    size_t index)
{
    InputActionsOverride* Override = &((InputActions*)context)->Overrides[index];
    char PrintableKey[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(key, PrintableKey, sizeof(PrintableKey));
    char Where[WHERE_CAPACITY];
    FormatWhere(Where, sizeof(Where), "overrides.%s", PrintableKey);

    Error Result = ReadTargetKey(reader, key, Where, "an action", &Override->Key);
    if ((Result.Code == ErrorCode_Success) && (value->Type != JSONValueType_Compound))
    {
        Result = Fail(reader, ErrorCode_InvalidJSON, Where, "expected an object.");
    }
    JSONCompound* Compound = (Result.Code == ErrorCode_Success) ? value->Value.Compound : NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = RejectUnknownKeys(reader, Compound, OverrideKeys, sizeof(OverrideKeys) / sizeof(OverrideKeys[0]), Where);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadText(reader, Compound, (const unsigned char*)u8"display_name", Where, true, &Override->DisplayName);
        Override->OverridesDisplayName = (Override->DisplayName != NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadText(reader, Compound, (const unsigned char*)u8"description", Where, false, &Override->Description);
        Override->OverridesDescription = (Override->Description != NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadBindings(reader, Compound, Where, Override->DefaultBindings, &Override->DefaultBindingCount,
            &Override->OverridesDefaultBindings);
    }
    if ((Result.Code == ErrorCode_Success) && !Override->OverridesDisplayName && !Override->OverridesDescription
        && !Override->OverridesDefaultBindings)
    {
        Result = Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "the override changes nothing (give "
            "\"display_name\", \"description\" or \"default_bindings\").");
    }
    return Result;
}

static Error ReadCategoryOverride(const Reader* reader, void* context, const unsigned char* key,
    const JSONObjectValue* value, size_t index)
{
    InputActionsCategoryOverride* Override = &((InputActions*)context)->CategoryOverrides[index];
    char PrintableKey[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(key, PrintableKey, sizeof(PrintableKey));
    char Where[WHERE_CAPACITY];
    FormatWhere(Where, sizeof(Where), "category_overrides.%s", PrintableKey);

    Error Result = ReadTargetKey(reader, key, Where, "a category", &Override->Key);
    if ((Result.Code == ErrorCode_Success) && (value->Type != JSONValueType_Compound))
    {
        Result = Fail(reader, ErrorCode_InvalidJSON, Where, "expected an object.");
    }
    JSONCompound* Compound = (Result.Code == ErrorCode_Success) ? value->Value.Compound : NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = RejectUnknownKeys(reader, Compound, CategoryOverrideKeys,
            sizeof(CategoryOverrideKeys) / sizeof(CategoryOverrideKeys[0]), Where);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadText(reader, Compound, (const unsigned char*)u8"display_name", Where, true, &Override->DisplayName);
        Override->OverridesDisplayName = (Override->DisplayName != NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadText(reader, Compound, (const unsigned char*)u8"description", Where, false, &Override->Description);
        Override->OverridesDescription = (Override->Description != NULL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadOrder(reader, Compound, Where, &Override->Order, &Override->OverridesOrder);
    }
    if ((Result.Code == ErrorCode_Success) && !Override->OverridesDisplayName && !Override->OverridesDescription
        && !Override->OverridesOrder)
    {
        Result = Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "the override changes nothing (give "
            "\"display_name\", \"description\" or \"order\").");
    }
    return Result;
}

/* Reads one of the three sections (an object of entries) into a new zeroed array of @p elementSize. */
static Error ReadSection(const Reader* reader, JSONCompound* root, const unsigned char* key, size_t maxCount,
    size_t elementSize, EntryVisitor visit, InputActions* self, void** outArray, size_t* outCount)
{
    const char* Where = (const char*)key;
    JSONCompound* Section = NULL;
    Error Result = GetOptionalCompound(reader, root, key, Where, &Section);
    if ((Result.Code != ErrorCode_Success) || (Section == NULL))
    {
        return Result;
    }
    size_t Count = JSONCompound_GetEntryCount(Section);
    if (Count > maxCount)
    {
        return Fail(reader, ErrorCode_InvalidAssetDefinition, Where, "%" PRIu64 " entries; at most %" PRIu64
            " are allowed.", (uint64_t)Count, (uint64_t)maxCount);
    }
    if (Count == 0U)
    {
        return Error_CreateSuccess();
    }
    *outArray = Memory_Allocate(Count * elementSize);
    Memory_Zero(*outArray, Count * elementSize);
    *outCount = Count;
    return ForEachEntry(reader, Section, Where, visit, self);
}

/* Whether two NUL-terminated keys are the same bytes. */
static bool AreKeysEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}

/* Refuses two overrides of one target ("use" and "tbp:use" in one file). */
static Error RejectRepeatedTargets(const Reader* reader, const InputActions* self)
{
    for (size_t First = 0U; First < self->OverrideCount; First++)
    {
        for (size_t Second = First + 1U; Second < self->OverrideCount; Second++)
        {
            if (AreKeysEqual(self->Overrides[First].Key, self->Overrides[Second].Key))
            {
                return Fail(reader, ErrorCode_InvalidAssetDefinition, "overrides", "\"%s\" is overridden twice.",
                    (const char*)self->Overrides[Second].Key);
            }
        }
    }
    for (size_t First = 0U; First < self->CategoryOverrideCount; First++)
    {
        for (size_t Second = First + 1U; Second < self->CategoryOverrideCount; Second++)
        {
            if (AreKeysEqual(self->CategoryOverrides[First].Key, self->CategoryOverrides[Second].Key))
            {
                return Fail(reader, ErrorCode_InvalidAssetDefinition, "category_overrides", "\"%s\" is overridden "
                    "twice.", (const char*)self->CategoryOverrides[Second].Key);
            }
        }
    }
    return Error_CreateSuccess();
}

static unsigned char* DuplicateText(const unsigned char* text)
{
    if (text == NULL)
    {
        return NULL;
    }
    size_t Length = StringUTF8_GetByteLength(text);
    unsigned char* Copy = Memory_Allocate(Length + 1U);
    Memory_Copy(text, Copy, Length + 1U);
    return Copy;
}


// Public functions: the asset.
Error InputActions_ReadJSON(const unsigned char* name, JSONCompound* root, InputActions** outActions)
{
    *outActions = NULL;
    InputActions* Self = Memory_Allocate(sizeof(InputActions));
    Memory_Zero(Self, sizeof(*Self));
    Self->Name = DuplicateText(name);
    Self->Order = INPUT_ACTIONS_DEFAULT_ORDER;

    Reader Reading;
    AssetJSON_CopyPrintable(name, Reading.Name, sizeof(Reading.Name));
    Reading.FullName = Self->Name;
    Reading.NamespaceLength = 0U;
    while ((name[Reading.NamespaceLength] != 0) && (name[Reading.NamespaceLength] != NAMESPACED_KEY_SEPARATOR))
    {
        Reading.NamespaceLength++;
    }

    Error Result = RejectUnknownKeys(&Reading, root, RootKeys, sizeof(RootKeys) / sizeof(RootKeys[0]), "its root");
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadDisplayName(&Reading, root, "its root", &Self->DisplayName);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadDescription(&Reading, root, "its root", &Self->Description);
    }
    bool HasOrder = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadOrder(&Reading, root, "its root", &Self->Order, &HasOrder);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadSection(&Reading, root, (const unsigned char*)u8"actions", INPUT_ACTIONS_MAX_ACTIONS,
            sizeof(InputActionsAction), ReadAction, Self, (void**)&Self->Actions, &Self->ActionCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadSection(&Reading, root, (const unsigned char*)u8"overrides", INPUT_ACTIONS_MAX_OVERRIDES,
            sizeof(InputActionsOverride), ReadOverride, Self, (void**)&Self->Overrides, &Self->OverrideCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadSection(&Reading, root, (const unsigned char*)u8"category_overrides",
            INPUT_ACTIONS_MAX_CATEGORY_OVERRIDES, sizeof(InputActionsCategoryOverride), ReadCategoryOverride, Self,
            (void**)&Self->CategoryOverrides, &Self->CategoryOverrideCount);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = RejectRepeatedTargets(&Reading, Self);
    }
    if (Result.Code != ErrorCode_Success)
    {
        InputActions_Destroy(Self);
        return Result;
    }
    *outActions = Self;
    return Error_CreateSuccess();
}

InputActions* InputActions_Clone(const InputActions* self)
{
    InputActions* Copy = Memory_Allocate(sizeof(InputActions));
    *Copy = *self;
    Copy->Name = DuplicateText(self->Name);
    Copy->DisplayName = DuplicateText(self->DisplayName);
    Copy->Description = DuplicateText(self->Description);
    Copy->Actions = (self->ActionCount > 0U) ? Memory_Allocate(self->ActionCount * sizeof(InputActionsAction)) : NULL;
    for (size_t Index = 0U; Index < self->ActionCount; Index++)
    {
        Copy->Actions[Index] = self->Actions[Index];
        Copy->Actions[Index].Key = DuplicateText(self->Actions[Index].Key);
        Copy->Actions[Index].DisplayName = DuplicateText(self->Actions[Index].DisplayName);
        Copy->Actions[Index].Description = DuplicateText(self->Actions[Index].Description);
    }
    Copy->Overrides = (self->OverrideCount > 0U) ? Memory_Allocate(self->OverrideCount * sizeof(InputActionsOverride))
        : NULL;
    for (size_t Index = 0U; Index < self->OverrideCount; Index++)
    {
        Copy->Overrides[Index] = self->Overrides[Index];
        Copy->Overrides[Index].Key = DuplicateText(self->Overrides[Index].Key);
        Copy->Overrides[Index].DisplayName = DuplicateText(self->Overrides[Index].DisplayName);
        Copy->Overrides[Index].Description = DuplicateText(self->Overrides[Index].Description);
    }
    Copy->CategoryOverrides = (self->CategoryOverrideCount > 0U)
        ? Memory_Allocate(self->CategoryOverrideCount * sizeof(InputActionsCategoryOverride)) : NULL;
    for (size_t Index = 0U; Index < self->CategoryOverrideCount; Index++)
    {
        Copy->CategoryOverrides[Index] = self->CategoryOverrides[Index];
        Copy->CategoryOverrides[Index].Key = DuplicateText(self->CategoryOverrides[Index].Key);
        Copy->CategoryOverrides[Index].DisplayName = DuplicateText(self->CategoryOverrides[Index].DisplayName);
        Copy->CategoryOverrides[Index].Description = DuplicateText(self->CategoryOverrides[Index].Description);
    }
    return Copy;
}

void InputActions_Destroy(InputActions* self)
{
    if (self == NULL)
    {
        return;
    }
    for (size_t Index = 0U; Index < self->ActionCount; Index++)
    {
        Memory_Free(self->Actions[Index].Key);
        Memory_Free(self->Actions[Index].DisplayName);
        Memory_Free(self->Actions[Index].Description);
    }
    for (size_t Index = 0U; Index < self->OverrideCount; Index++)
    {
        Memory_Free(self->Overrides[Index].Key);
        Memory_Free(self->Overrides[Index].DisplayName);
        Memory_Free(self->Overrides[Index].Description);
    }
    for (size_t Index = 0U; Index < self->CategoryOverrideCount; Index++)
    {
        Memory_Free(self->CategoryOverrides[Index].Key);
        Memory_Free(self->CategoryOverrides[Index].DisplayName);
        Memory_Free(self->CategoryOverrides[Index].Description);
    }
    Memory_Free(self->Actions);
    Memory_Free(self->Overrides);
    Memory_Free(self->CategoryOverrides);
    Memory_Free(self->Name);
    Memory_Free(self->DisplayName);
    Memory_Free(self->Description);
    Memory_Free(self);
}


// Public functions: registration.
Error InputActions_RegisterCategoryAndActions(const InputActions* self, GameInput* input)
{
    if ((self == NULL) || (input == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "InputActions_RegisterCategoryAndActions: self and input must not be NULL.");
    }
    KeyRegistry* Keys = GameInput_GetKeyRegistry(input);
    uint32_t Source = NAMESPACED_KEY_ID_NONE;
    Error Result = KeyRegistry_Intern(Keys, self->Name, &Source);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // Nothing of the definition may be registered now; checked before anything registers.
    GameActionCategoryId CategoryId = 0U;
    if (GameInput_FindCategory(input, Source, &CategoryId))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "Input actions \"%s\": the category is registered already "
            "(by %s).", (const char*)self->Name, DescribeSource(input, GameInput_GetCategorySource(input, CategoryId)));
    }
    uint32_t* ActionKeyIds = (self->ActionCount > 0U) ? Memory_Allocate(self->ActionCount * sizeof(uint32_t)) : NULL;
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < self->ActionCount); Index++)
    {
        Result = KeyRegistry_Intern(Keys, self->Actions[Index].Key, &ActionKeyIds[Index]);
        GameActionId Existing = 0U;
        if ((Result.Code == ErrorCode_Success) && GameInput_FindAction(input, ActionKeyIds[Index], &Existing))
        {
            Result = Error_Construct4(ErrorCode_InvalidOperation, "Input actions \"%s\": the action \"%s\" is registered "
                "already (by %s).", (const char*)self->Name, (const char*)self->Actions[Index].Key,
                DescribeSource(input, GameInput_GetActionSource(input, Existing)));
        }
    }

    GameActionCategoryInfo CategoryInfo = { .DisplayName = self->DisplayName, .Description = self->Description,
        .Order = self->Order };
    if (Result.Code == ErrorCode_Success)
    {
        Result = GameInput_RegisterCategory(input, Source, &CategoryInfo, Source, &CategoryId);
    }
    bool IsCategoryRegistered = (Result.Code == ErrorCode_Success);
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < self->ActionCount); Index++)
    {
        const InputActionsAction* Action = &self->Actions[Index];
        GameActionInfo Info = { .DisplayName = Action->DisplayName, .Description = Action->Description,
            .Category = CategoryId, .DefaultBindingCount = Action->DefaultBindingCount, .Kind = Action->Kind,
            .KeepsABinding = Action->KeepsABinding };
        Memory_Copy(Action->DefaultBindings, Info.DefaultBindings, sizeof(Info.DefaultBindings));
        Result = GameInput_RegisterAction(input, ActionKeyIds[Index], &Info, Source, NULL);
    }
    if ((Result.Code != ErrorCode_Success) && IsCategoryRegistered)
    {
        GameInput_WithdrawSource(input, Source); // All or nothing: what registered goes again.
    }
    Memory_Free(ActionKeyIds);
    return Result;
}

Error InputActions_ApplyOverrides(const InputActions* self, GameInput* input)
{
    if ((self == NULL) || (input == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "InputActions_ApplyOverrides: self and input must not be NULL.");
    }
    if ((self->OverrideCount == 0U) && (self->CategoryOverrideCount == 0U))
    {
        return Error_CreateSuccess();
    }
    uint32_t Source = NAMESPACED_KEY_ID_NONE;
    Error Result = KeyRegistry_Intern(GameInput_GetKeyRegistry(input), self->Name, &Source);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // Every target is checked before anything changes.
    GameActionId* ActionIds = (self->OverrideCount > 0U) ? Memory_Allocate(self->OverrideCount * sizeof(GameActionId))
        : NULL;
    GameActionCategoryId* CategoryIds = (self->CategoryOverrideCount > 0U)
        ? Memory_Allocate(self->CategoryOverrideCount * sizeof(GameActionCategoryId)) : NULL;
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < self->OverrideCount); Index++)
    {
        const InputActionsOverride* Override = &self->Overrides[Index];
        uint32_t Holder = NAMESPACED_KEY_ID_NONE;
        GameActionInfo Info;
        if (!GameInput_FindActionText(input, Override->Key, &ActionIds[Index]))
        {
            Result = Error_Construct4(ErrorCode_InvalidOperation, "Input actions \"%s\": the action \"%s\" it changes is "
                "not registered.", (const char*)self->Name, (const char*)Override->Key);
        }
        else if (GameInput_TryGetActionOverrideSource(input, ActionIds[Index], &Holder) && (Holder != Source))
        {
            Result = Error_Construct4(ErrorCode_InvalidOperation, "Input actions \"%s\": \"%s\" is changed by %s "
                "already.", (const char*)self->Name, (const char*)Override->Key, DescribeSource(input, Holder));
        }
        else if (Override->OverridesDefaultBindings && (Override->DefaultBindingCount == 0U)
            && GameInput_TryGetActionInfo(input, ActionIds[Index], &Info) && Info.KeepsABinding)
        {
            Result = Error_Construct4(ErrorCode_InvalidOperation, "Input actions \"%s\": \"%s\" always keeps a binding, "
                "so its defaults cannot be empty.", (const char*)self->Name, (const char*)Override->Key);
        }
    }
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < self->CategoryOverrideCount); Index++)
    {
        const InputActionsCategoryOverride* Override = &self->CategoryOverrides[Index];
        uint32_t Holder = NAMESPACED_KEY_ID_NONE;
        if (!GameInput_FindCategoryText(input, Override->Key, &CategoryIds[Index]))
        {
            Result = Error_Construct4(ErrorCode_InvalidOperation, "Input actions \"%s\": the category \"%s\" it changes "
                "is not registered.", (const char*)self->Name, (const char*)Override->Key);
        }
        else if (GameInput_TryGetCategoryOverrideSource(input, CategoryIds[Index], &Holder) && (Holder != Source))
        {
            Result = Error_Construct4(ErrorCode_InvalidOperation, "Input actions \"%s\": \"%s\" is changed by %s "
                "already.", (const char*)self->Name, (const char*)Override->Key, DescribeSource(input, Holder));
        }
    }

    // The checks leave nothing for the input to refuse.
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < self->OverrideCount); Index++)
    {
        const InputActionsOverride* Override = &self->Overrides[Index];
        GameActionOverride Change = { .OverridesDisplayName = Override->OverridesDisplayName,
            .DisplayName = Override->DisplayName, .OverridesDescription = Override->OverridesDescription,
            .Description = Override->Description, .OverridesDefaultBindings = Override->OverridesDefaultBindings,
            .DefaultBindingCount = Override->DefaultBindingCount };
        Memory_Copy(Override->DefaultBindings, Change.DefaultBindings, sizeof(Change.DefaultBindings));
        Result = GameInput_OverrideAction(input, ActionIds[Index], &Change, Source);
    }
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < self->CategoryOverrideCount); Index++)
    {
        const InputActionsCategoryOverride* Override = &self->CategoryOverrides[Index];
        GameActionCategoryOverride Change = { .OverridesDisplayName = Override->OverridesDisplayName,
            .DisplayName = Override->DisplayName, .OverridesDescription = Override->OverridesDescription,
            .Description = Override->Description, .OverridesOrder = Override->OverridesOrder, .Order = Override->Order };
        Result = GameInput_OverrideCategory(input, CategoryIds[Index], &Change, Source);
    }
    Memory_Free(ActionIds);
    Memory_Free(CategoryIds);
    return Result;
}

Error InputActions_RegisterDefinitions(AssetManager* assets, GameInput* input, Logger* logger)
{
    if ((assets == NULL) || (input == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "InputActions_RegisterDefinitions: assets and input must not be NULL.");
    }
    AssetTypeID Type = ASSET_TYPE_ID_INVALID;
    Error Result = AssetManager_GetAssetTypeByName(assets, (const unsigned char*)u8"input_actions", &Type);
    GenericBuffer Names;
    GenericBuffer_AllocateVariable(&Names, 256U, sizeof(unsigned char));
    GenericBuffer Offsets;
    GenericBuffer_AllocateVariable(&Offsets, 16U, sizeof(size_t));
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetManager_GetDefinitionNames(assets, Type, (const unsigned char*)u8"", &Names, &Offsets);
    }
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetManager_GetNewUserID(assets, &User);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Names._data);
        Memory_Free(Offsets._data);
        return Result;
    }

    size_t Count = Offsets._count;
    const size_t* NameOffsets = (const size_t*)Offsets._data;
    InputActions** Loaded = (Count > 0U) ? Memory_Allocate(Count * sizeof(InputActions*)) : NULL;
    size_t RegisteredCount = 0U;
    size_t ActionCount = 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const unsigned char* Name = Names._data + NameOffsets[Index];
        Loaded[Index] = NULL;
        Error LoadResult = AssetManager_LoadInputActions(assets, Name, User, &Loaded[Index]);
        if (LoadResult.Code == ErrorCode_Success)
        {
            LoadResult = InputActions_RegisterCategoryAndActions(Loaded[Index], input);
            if (LoadResult.Code != ErrorCode_Success)
            {
                Loaded[Index] = NULL; // Registered nothing: no overrides either.
            }
        }
        if (LoadResult.Code != ErrorCode_Success)
        {
            if (logger != NULL)
            {
                Error LogResult = Logger_LogWarningFormatted(logger, (const unsigned char*)u8"Input actions \"%s\" "
                    "registered nothing: %s", (const char*)Name, (LoadResult.Message != NULL)
                    ? (const char*)LoadResult.Message : "no details");
                Error_Deconstruct(&LogResult);
            }
            Error_Deconstruct(&LoadResult);
            continue;
        }
        RegisteredCount++;
        ActionCount += Loaded[Index]->ActionCount;
    }

    // Every definition's actions exist now, so an override may name any of them.
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if (Loaded[Index] == NULL)
        {
            continue;
        }
        Error OverrideResult = InputActions_ApplyOverrides(Loaded[Index], input);
        if ((OverrideResult.Code != ErrorCode_Success) && (logger != NULL))
        {
            Error LogResult = Logger_LogWarningFormatted(logger, (const unsigned char*)u8"Input actions \"%s\": its "
                "overrides were not applied: %s", (const char*)Loaded[Index]->Name, (OverrideResult.Message != NULL)
                ? (const char*)OverrideResult.Message : "no details");
            Error_Deconstruct(&LogResult);
        }
        Error_Deconstruct(&OverrideResult);
    }

    if (logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(logger, (const unsigned char*)u8"Input actions: %" PRIu64 " of %" PRIu64
            " definitions registered, with %" PRIu64 " actions.", (uint64_t)RegisteredCount, (uint64_t)Count,
            (uint64_t)ActionCount);
        Error_Deconstruct(&LogResult);
    }
    Error RetireResult = AssetManager_RetireUser(assets, User);
    Error_Deconstruct(&RetireResult);
    Memory_Free(Loaded);
    Memory_Free(Names._data);
    Memory_Free(Offsets._data);
    return Error_CreateSuccess();
}
