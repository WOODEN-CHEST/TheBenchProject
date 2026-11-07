#include <stdio.h>
#include "AssetTypesStandard.h"
#include "AssetDefinitionCommon.h"
#include "GameJSON.h"
#include "GameTexture.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "UITheme.h"
#include "UIThemeInternal.h"
#include "wr/WRJSON.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * The "ui_theme" asset type (references/asset_structure.md, "UI themes"; UITheme.h): the definition's parser, hand-written
 * because a theme's blocks nest (property tables are flat), with unknown keys refused and every rule checked when the
 * definitions are read, and the loader, which loads the textures and the font a theme names as its DEPENDENCIES.
 *
 * PALETTE REFERENCES RESOLVE WHEN READ. The palette is read before anything that may name it, whatever the file's order,
 * and a "@name" colour becomes the palette's colour on the spot: a definition and a loaded theme hold plain colours only,
 * so lookups and fades know nothing of palettes. A palette colour is never a reference itself, so references cannot loop.
 *
 * VARIANTS MERGE WHEN READ. A variant starts as a copy of its kind's values and what it names is read on top, down to one
 * state's colour, so it changes only what it names and a lookup needs no fallback chain. Custom values merge by key: a
 * variant's entry replaces its kind's entry of the same key whole, or adds one.
 *
 * WHAT A THEME SURVIVES. A texture that does not load, or whose art does not fit its margins, only makes the boxes using it
 * plain (parts and custom boxes); a font that does not load only leaves the built-in one. Each is logged once, when the theme loads. A broken art file in a
 * pack should not take the whole UI's look away.
 */


// Macros.
/** How many bytes of a name from the file a message shows, and room for a message's "where". */
#define NAME_PRINT_CAPACITY ((size_t)129)
#define WHERE_CAPACITY ((size_t)320)
/** The elements' known keys: a kind's parts, "custom", "look" and "variants". */
#define MAX_ELEMENT_KEYS (UI_THEME_MAX_PARTS + 3U)
/** The largest number a nine-slice's rectangle or margin may hold, in texels. */
#define MAX_TEXELS 16384.0f
/** The size of a margins array: left, top, right, bottom. */
#define MARGIN_COUNT 4U
/** The size of a custom vector's array. */
#define VECTOR_COMPONENT_COUNT 2U
/** The first byte of a colour that names a palette colour: "@accent". */
#define PALETTE_REFERENCE_MARK ((unsigned char)'@')


// Types.
typedef struct UIThemeDefinitionStruct
{
    AssetDefinition Base;
    UITheme Data;
} UIThemeDefinition;

/* One colour of the palette while a definition is read. */
typedef struct PaletteColorStruct
{
    unsigned char Name[UI_THEME_PALETTE_NAME_CAPACITY];
    Color Color;
} PaletteColor;

/* What a parse carries down the theme's blocks: the theme, whose table of texture names grows as boxes name them, and the
 * palette "@name" colours are resolved from. */
typedef struct ThemeReadStruct
{
    UITheme* Theme;
    PaletteColor Palette[UI_THEME_MAX_PALETTE_COLORS];
    size_t PaletteCount;
    /** Whether the theme has a "palette" at all, for the message of a reference without one. */
    bool HasPalette;
} ThemeRead;

/* Reads one block of the root ("palette", "elements") into the theme being read; @p where names the block. */
typedef Error (*RootBlockReader)(ThemeRead* read, const JSONObjectValue* value, const char* where);


// Fields.
static const unsigned char* const RootKeys[] =
{
    u8"name", u8"font", u8"text_size", u8"transition_time", u8"tooltip_delay", u8"palette", u8"elements"
};
static const unsigned char* const BoxKeys[] =
{
    u8"kind", u8"colors", u8"outline", u8"texture", u8"margins", u8"pixel_scale", u8"source", u8"sources"
};
static const unsigned char* const OutlineKeys[] = { u8"thickness", u8"colors" };
static const unsigned char* const RectangleKeys[] = { u8"x", u8"y", u8"width", u8"height" };


// Static functions: small readers.
/* "<where>.<key>" for the message of a nested block; only a message's text is cut when it does not fit. */
static void NestWhere(char* outWhere, size_t capacity, const char* where, const char* key)
{
    int Written = snprintf(outWhere, capacity, "%s.%s", where, key);
    if (Written < 0)
    {
        outWhere[0] = 0;
    }
}

/* "UI theme \"<name>\", <key>" for the message of a key of the root. */
static void RootWhere(char* outWhere, size_t capacity, const char* themeName, const char* key)
{
    int Written = snprintf(outWhere, capacity, "UI theme \"%s\", %s", themeName, key);
    if (Written < 0)
    {
        outWhere[0] = 0;
    }
}

static Error Refuse(const char* where, const char* what)
{
    return Error_Construct4(ErrorCode_InvalidAssetDefinition, "%s: %s", where, what);
}

/* The inner error as a refusal naming where it is; releases it. */
static Error WrapError(const char* where, Error* inner)
{
    Error Wrapped = Error_Construct4(ErrorCode_InvalidAssetDefinition, "%s: %s", where,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid.");
    Error_Deconstruct(inner);
    return Wrapped;
}

/* A value's compound, or a refusal. */
static Error GetCompound(const JSONObjectValue* value, const char* where, JSONCompound** outCompound)
{
    if (value->Type != JSONValueType_Compound)
    {
        return Refuse(where, "must be a compound.");
    }
    *outCompound = value->Value.Compound;
    return Error_CreateSuccess();
}

/* A number within bounds, or a refusal. */
static Error ReadNumber(const JSONObjectValue* value, float minimum, float maximum, const char* where, float* outValue)
{
    float Number = 0.0f;
    Error Result = AssetJSON_ValueToFloat(value, &Number);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(where, &Result);
    }
    if (!(Number >= minimum) || !(Number <= maximum))
    {
        return Error_Construct4(ErrorCode_InvalidAssetDefinition, "%s: %g is outside [%g, %g].", where, (double)Number,
            (double)minimum, (double)maximum);
    }
    *outValue = Number;
    return Error_CreateSuccess();
}

/* A rectangle compound ("x", "y", "width", "height") within the art's bounds. */
static Error ReadRectangle(const JSONObjectValue* value, const char* where, Rectangle* outRectangle)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = AssetJSON_RejectUnknownKeys(Compound, RectangleKeys, sizeof(RectangleKeys) / sizeof(RectangleKeys[0]), where);
    float Numbers[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (size_t Index = 0U; (Index < 4U) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Number;
        bool IsFound = false;
        Result = JSONCompound_GetOptional(Compound, RectangleKeys[Index], &Number, &IsFound);
        if ((Result.Code == ErrorCode_Success) && !IsFound)
        {
            return Refuse(where, "a rectangle needs its x, y, width and height.");
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNumber(&Number, 0.0f, MAX_TEXELS, where, &Numbers[Index]);
        }
    }
    if ((Result.Code == ErrorCode_Success) && (!(Numbers[2] > 0.0f) || !(Numbers[3] > 0.0f)))
    {
        return Refuse(where, "a rectangle's width and height must be above 0.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        *outRectangle = (Rectangle){ .x = Numbers[0], .y = Numbers[1], .width = Numbers[2], .height = Numbers[3] };
    }
    return Result;
}

/* A state's index from its name, or a refusal naming the key. */
static Error FindState(const unsigned char* key, const char* where, UIElementState* outState)
{
    for (int32_t State = 0; State < (int32_t)UIElementState_Count; State++)
    {
        bool IsSame = false;
        Error Compare = StringUTF8_EqualsExact(UIElementState_GetName((UIElementState)State), key, &IsSame);
        Error_Deconstruct(&Compare);
        if (IsSame)
        {
            *outState = (UIElementState)State;
            return Error_CreateSuccess();
        }
    }
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(key, Printable, sizeof(Printable));
    return Error_Construct4(ErrorCode_InvalidAssetDefinition,
        "%s: unknown state \"%s\" (normal, hovered, pressed, focused, disabled, selected, invalid, entered).", where, Printable);
}

/* The next entry of a compound's enumerator; *outHasEntry says whether there was one. */
static Error NextEntry(CollectionEnumerator* enumerator, JSONCompoundEntry* outEntry, bool* outHasEntry)
{
    Error Result = CollectionEnumerator_HasNext(enumerator, outHasEntry);
    if ((Result.Code == ErrorCode_Success) && *outHasEntry)
    {
        Result = CollectionEnumerator_NextByValue(enumerator, outEntry);
    }
    return Result;
}

/* Whether a local name from the file fits a buffer of @p capacity bytes: `[a-z][a-z0-9_.-]*`, its NUL included. */
static bool IsLocalNameOfCapacity(const unsigned char* name, size_t capacity)
{
    return NamespacedKey_IsValidLocalName(name) && (StringUTF8_GetByteLength(name) < capacity);
}


// Static functions: colours and the palette.
/* Whether a value names a palette colour: a string starting with '@'. */
static bool IsPaletteReference(const JSONObjectValue* value)
{
    const GenericBuffer* Text = (value->Type == JSONValueType_String) ? value->Value.String : NULL;
    return (Text != NULL) && (Text->_count > 0U) && (Text->_data[0] == PALETTE_REFERENCE_MARK);
}

/* The palette colour a reference names, or a refusal naming the reference and where it is. Compared by length and bytes,
 * so a name with a NUL inside matches nothing. */
static Error ResolvePaletteReference(const ThemeRead* read, const JSONObjectValue* value, const char* where, Color* outColor)
{
    const GenericBuffer* Text = value->Value.String;
    const unsigned char* Name = Text->_data + 1;
    size_t Length = Text->_count - 1U;
    for (size_t Index = 0U; Index < read->PaletteCount; Index++)
    {
        const PaletteColor* Entry = &read->Palette[Index];
        if ((StringUTF8_GetByteLength(Entry->Name) == Length) && Memory_IsEqual(Entry->Name, Name, Length))
        {
            *outColor = Entry->Color;
            return Error_CreateSuccess();
        }
    }
    unsigned char* Reference = NULL;
    Error TextResult = AssetJSON_ValueToOwnedString(value, &Reference);
    Error_Deconstruct(&TextResult); // a string: it cannot fail
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Reference, Printable, sizeof(Printable));
    Memory_Free(Reference);
    if (!read->HasPalette)
    {
        return Error_Construct4(ErrorCode_InvalidAssetDefinition, "%s: \"%s\" names a palette colour, but the theme has no \"palette\".",
            where, Printable);
    }
    return Error_Construct4(ErrorCode_InvalidAssetDefinition, "%s: \"%s\" names no colour of the theme's palette.", where, Printable);
}

/* A colour: any AssetJSON_ParseColor form, or "@<name>", a colour of the theme's palette. */
static Error ReadColor(const ThemeRead* read, const JSONObjectValue* value, const char* where, Color* outColor)
{
    if (IsPaletteReference(value))
    {
        return ResolvePaletteReference(read, value, where, outColor);
    }
    Error Result = AssetJSON_ParseColor(value, outColor);
    return (Result.Code == ErrorCode_Success) ? Result : WrapError(where, &Result);
}

/* A colour per state, on top of what @p colors already holds (a variant's merge). @p where is the colours' own. */
static Error ReadStateColors(const ThemeRead* read, const JSONObjectValue* value, const char* where, UIStateColors* colors)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    CollectionEnumerator* Entries = ICollection_CreateEnumerator(JSONCompound_GetEntryCollection(Compound));
    if (Entries == NULL)
    {
        return Refuse(where, "could not read its entries.");
    }
    JSONCompoundEntry Entry;
    bool HasEntry = false;
    Result = NextEntry(Entries, &Entry, &HasEntry);
    while ((Result.Code == ErrorCode_Success) && HasEntry)
    {
        UIElementState State = UIElementState_Normal;
        Result = FindState(Entry.Key, where, &State);
        if (Result.Code == ErrorCode_Success)
        {
            char StateWhere[WHERE_CAPACITY];
            NestWhere(StateWhere, sizeof(StateWhere), where, (const char*)UIElementState_GetName(State));
            Color Parsed = WHITE;
            Result = ReadColor(read, &Entry.Value, StateWhere, &Parsed);
            if (Result.Code == ErrorCode_Success)
            {
                colors->Colors[State] = Parsed;
                colors->HasColor[State] = true;
            }
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = NextEntry(Entries, &Entry, &HasEntry);
        }
    }
    CollectionEnumerator_Destroy(Entries);
    return Result;
}

/* One colour of the palette: a name and a plain colour, never a reference to another (so references cannot loop). */
static Error ReadPaletteColor(ThemeRead* read, const JSONCompoundEntry* entry, const char* where)
{
    char EntryWhere[WHERE_CAPACITY];
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(entry->Key, Printable, sizeof(Printable));
    NestWhere(EntryWhere, sizeof(EntryWhere), where, Printable);
    if (!IsLocalNameOfCapacity(entry->Key, UI_THEME_PALETTE_NAME_CAPACITY))
    {
        return Refuse(EntryWhere, "a palette colour's name is 1 to 31 of a-z, 0-9, _, . and -, starting with a letter.");
    }
    if (IsPaletteReference(&entry->Value))
    {
        return Refuse(EntryWhere, "a palette colour is a colour, never a reference to another (\"@...\").");
    }
    PaletteColor* Slot = &read->Palette[read->PaletteCount];
    Error Result = AssetJSON_ParseColor(&entry->Value, &Slot->Color);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(EntryWhere, &Result);
    }
    Memory_Copy(entry->Key, Slot->Name, StringUTF8_GetByteLength(entry->Key) + 1U);
    read->PaletteCount++;
    return Error_CreateSuccess();
}

/* "palette": colours by name, which every other colour of the theme may name as "@<name>". */
static Error ReadPalette(ThemeRead* read, const JSONObjectValue* value, const char* where)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (JSONCompound_GetEntryCount(Compound) > UI_THEME_MAX_PALETTE_COLORS)
    {
        return Refuse(where, "a palette may name at most 64 colours.");
    }
    CollectionEnumerator* Entries = ICollection_CreateEnumerator(JSONCompound_GetEntryCollection(Compound));
    if (Entries == NULL)
    {
        return Refuse(where, "could not read its entries.");
    }
    JSONCompoundEntry Entry;
    bool HasEntry = false;
    Result = NextEntry(Entries, &Entry, &HasEntry);
    while ((Result.Code == ErrorCode_Success) && HasEntry)
    {
        Result = ReadPaletteColor(read, &Entry, where);
        if (Result.Code == ErrorCode_Success)
        {
            Result = NextEntry(Entries, &Entry, &HasEntry);
        }
    }
    CollectionEnumerator_Destroy(Entries);
    read->HasPalette = (Result.Code == ErrorCode_Success);
    return Result;
}


// Static functions: a box.
/* The index of a texture name in the theme's table, added when new. */
static Error InternTextureName(UITheme* theme, const JSONObjectValue* value, const char* where, uint32_t* outIndex)
{
    unsigned char* Name = NULL;
    Error Result = AssetJSON_ValueToOwnedString(value, &Name);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(where, &Result);
    }
    const char* Problem = AssetJSON_MakeFullAssetName(&Name);
    if (Problem != NULL)
    {
        Memory_Free(Name);
        return Refuse(where, Problem);
    }
    for (size_t Index = 0U; Index < theme->TextureCount; Index++)
    {
        bool IsSame = false;
        Error Compare = StringUTF8_EqualsExact(theme->TextureNames[Index], Name, &IsSame);
        Error_Deconstruct(&Compare);
        if (IsSame)
        {
            Memory_Free(Name);
            *outIndex = (uint32_t)Index;
            return Error_CreateSuccess();
        }
    }
    if (theme->TextureCount >= UI_THEME_MAX_TEXTURES)
    {
        Memory_Free(Name);
        return Refuse(where, "a theme may name at most 64 different textures.");
    }
    if (theme->TextureNames == NULL)
    {
        theme->TextureNames = Memory_Allocate(UI_THEME_MAX_TEXTURES * sizeof(unsigned char*));
    }
    theme->TextureNames[theme->TextureCount] = Name;
    *outIndex = (uint32_t)theme->TextureCount;
    theme->TextureCount++;
    return Error_CreateSuccess();
}

static Error ReadBoxKind(const JSONObjectValue* value, const char* where, UIBoxKind* outKind)
{
    static const struct { const unsigned char* Name; UIBoxKind Kind; } Names[] =
    {
        { u8"none", UIBoxKind_None }, { u8"flat", UIBoxKind_Flat }, { u8"nine_slice", UIBoxKind_NineSlice }
    };
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error Result = AssetJSON_ValueToOwnedString(value, &Text);
        for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < (sizeof(Names) / sizeof(Names[0]))); Index++)
        {
            bool IsSame = false;
            Error Compare = StringUTF8_EqualsExact(Names[Index].Name, Text, &IsSame);
            Error_Deconstruct(&Compare);
            if (IsSame)
            {
                *outKind = Names[Index].Kind;
                Memory_Free(Text);
                return Error_CreateSuccess();
            }
        }
        Memory_Free(Text);
        Error_Deconstruct(&Result);
    }
    return Refuse(where, "its \"kind\" must be \"none\", \"flat\" or \"nine_slice\".");
}

/* "outline": its thickness and colours, for a flat box. */
static Error ReadOutline(const ThemeRead* read, const JSONObjectValue* value, const char* where, UIBoxStyle* box)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_RejectUnknownKeys(Compound, OutlineKeys, sizeof(OutlineKeys) / sizeof(OutlineKeys[0]), where);
    }
    JSONObjectValue Inner;
    bool IsFound = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(Compound, u8"thickness", &Inner, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadNumber(&Inner, 0.0f, UI_THEME_MAX_OUTLINE, where, &box->OutlineThickness);
    }
    if ((Result.Code == ErrorCode_Success) && !IsFound && !box->HasOutline)
    {
        box->OutlineThickness = 1.0f;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(Compound, u8"colors", &Inner, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        char ColorsWhere[WHERE_CAPACITY];
        NestWhere(ColorsWhere, sizeof(ColorsWhere), where, "outline.colors");
        Result = ReadStateColors(read, &Inner, ColorsWhere, &box->OutlineColors);
    }
    if (Result.Code == ErrorCode_Success)
    {
        box->HasOutline = box->OutlineThickness > 0.0f;
    }
    return Result;
}

/* "margins": one number for all four, or [left, top, right, bottom], in texels. */
static Error ReadMargins(const JSONObjectValue* value, const char* where, UIInsets* outMargins)
{
    float Numbers[MARGIN_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (value->Type != JSONValueType_Array)
    {
        Error Result = ReadNumber(value, 0.0f, MAX_TEXELS, where, &Numbers[0]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        *outMargins = UIInsets_All(Numbers[0]);
        return Error_CreateSuccess();
    }
    if (JSONArray_GetElementCount(value->Value.Array) != MARGIN_COUNT)
    {
        return Refuse(where, "its \"margins\" must be one number or four: left, top, right, bottom.");
    }
    for (size_t Index = 0U; Index < MARGIN_COUNT; Index++)
    {
        JSONObjectValue Element;
        Error Result = JSONArray_Get(value->Value.Array, Index, &Element);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNumber(&Element, 0.0f, MAX_TEXELS, where, &Numbers[Index]);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    *outMargins = (UIInsets){ .Left = Numbers[0], .Top = Numbers[1], .Right = Numbers[2], .Bottom = Numbers[3] };
    return Error_CreateSuccess();
}

/* "sources": a rectangle per state. */
static Error ReadSources(const JSONObjectValue* value, const char* where, UIBoxStyle* box)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    CollectionEnumerator* Entries = ICollection_CreateEnumerator(JSONCompound_GetEntryCollection(Compound));
    if (Entries == NULL)
    {
        return Refuse(where, "could not read its entries.");
    }
    JSONCompoundEntry Entry;
    bool HasEntry = false;
    Result = NextEntry(Entries, &Entry, &HasEntry);
    while ((Result.Code == ErrorCode_Success) && HasEntry)
    {
        UIElementState State = UIElementState_Normal;
        Result = FindState(Entry.Key, where, &State);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadRectangle(&Entry.Value, where, &box->Sources[State]);
        }
        if (Result.Code == ErrorCode_Success)
        {
            box->HasSource[State] = true;
            Result = NextEntry(Entries, &Entry, &HasEntry);
        }
    }
    CollectionEnumerator_Destroy(Entries);
    return Result;
}

/* The keys only a nine-slice has; a box of another kind refuses them. */
static Error ReadNineSliceKeys(ThemeRead* read, JSONCompound* compound, const char* where, UIBoxStyle* box, bool* inOutHasTexture,
    bool* inOutHasMargins)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(compound, u8"texture", &Value, &IsFound);
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = InternTextureName(read->Theme, &Value, where, &box->TextureIndex);
        *inOutHasTexture = true;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(compound, u8"margins", &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadMargins(&Value, where, &box->Margins);
        *inOutHasMargins = true;
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(compound, u8"pixel_scale", &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadNumber(&Value, UI_THEME_MIN_PIXEL_SCALE, UI_THEME_MAX_PIXEL_SCALE, where, &box->PixelScale);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(compound, u8"source", &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadRectangle(&Value, where, &box->Sources[UIElementState_Normal]);
        box->HasSource[UIElementState_Normal] = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(compound, u8"sources", &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadSources(&Value, where, box);
    }
    return Result;
}

/* Whether the compound holds any key of a list. */
static bool HasAnyKey(JSONCompound* compound, const unsigned char* const* keys, size_t keyCount)
{
    for (size_t Index = 0U; Index < keyCount; Index++)
    {
        JSONObjectValue Value;
        bool IsFound = false;
        Error Result = JSONCompound_GetOptional(compound, keys[Index], &Value, &IsFound);
        Error_Deconstruct(&Result);
        if (IsFound)
        {
            return true;
        }
    }
    return false;
}

/* A box block, on top of @p value when it is set already (a variant's merge). */
static Error ReadBox(ThemeRead* read, const JSONObjectValue* value, const char* where, UIThemeValue* outValue)
{
    static const unsigned char* const NineSliceKeys[] = { u8"texture", u8"margins", u8"pixel_scale", u8"source", u8"sources" };
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_RejectUnknownKeys(Compound, BoxKeys, sizeof(BoxKeys) / sizeof(BoxKeys[0]), where);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    UIBoxStyle* Box = &outValue->Data.Box;
    bool WasNineSlice = outValue->IsSet && (Box->Kind == UIBoxKind_NineSlice);
    bool HasTexture = WasNineSlice;
    bool HasMargins = WasNineSlice;
    if (!outValue->IsSet)
    {
        Memory_Zero(Box, sizeof(*Box));
        Box->PixelScale = 1.0f;
    }

    JSONObjectValue Inner;
    bool IsFound = false;
    Result = JSONCompound_GetOptional(Compound, u8"kind", &Inner, &IsFound);
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadBoxKind(&Inner, where, &Box->Kind);
    }
    else if ((Result.Code == ErrorCode_Success) && !outValue->IsSet)
    {
        return Refuse(where, "it needs a \"kind\": \"none\", \"flat\" or \"nine_slice\".");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(Compound, u8"colors", &Inner, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        char ColorsWhere[WHERE_CAPACITY];
        NestWhere(ColorsWhere, sizeof(ColorsWhere), where, "colors");
        Result = ReadStateColors(read, &Inner, ColorsWhere, &Box->Colors);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(Compound, u8"outline", &Inner, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = (Box->Kind == UIBoxKind_Flat) ? ReadOutline(read, &Inner, where, Box)
            : Refuse(where, "only a \"flat\" box has an \"outline\".");
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Box->Kind != UIBoxKind_NineSlice)
    {
        if (HasAnyKey(Compound, NineSliceKeys, sizeof(NineSliceKeys) / sizeof(NineSliceKeys[0])))
        {
            return Refuse(where, "only a \"nine_slice\" box has a \"texture\", \"margins\", \"pixel_scale\", \"source\" or \"sources\".");
        }
        outValue->IsSet = true;
        return Error_CreateSuccess();
    }
    Result = ReadNineSliceKeys(read, Compound, where, Box, &HasTexture, &HasMargins);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasTexture || !HasMargins)
    {
        return Refuse(where, "a \"nine_slice\" box needs a \"texture\" and \"margins\".");
    }
    outValue->IsSet = true;
    return Error_CreateSuccess();
}


// Static functions: custom values.
/* An owned copy of @p count custom values; NULL for none. Every copy of a style takes one, so no two styles share a block. */
static UIThemeCustomValue* CopyCustomValues(const UIThemeCustomValue* values, size_t count)
{
    if (count == 0U)
    {
        return NULL;
    }
    UIThemeCustomValue* Copy = Memory_Allocate(count * sizeof(UIThemeCustomValue));
    Memory_Copy(values, Copy, count * sizeof(UIThemeCustomValue));
    return Copy;
}

/* The index of @p key among @p count custom values; @p count when it is not there. */
static size_t FindCustomIndex(const UIThemeCustomValue* values, size_t count, const unsigned char* key)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        bool IsSame = false;
        Error Compare = StringUTF8_EqualsExact(values[Index].Key, key, &IsSame);
        Error_Deconstruct(&Compare);
        if (IsSame)
        {
            return Index;
        }
    }
    return count;
}

/* A custom vector: an array of exactly two JSON numbers, each within the custom bounds. */
static Error ReadCustomVector(const JSONObjectValue* value, const char* where, Vector2* outVector)
{
    JSONArray* Array = value->Value.Array;
    if (JSONArray_GetElementCount(Array) != VECTOR_COMPONENT_COUNT)
    {
        return Refuse(where, "a custom vector is an array of exactly two numbers.");
    }
    float Components[VECTOR_COMPONENT_COUNT] = { 0.0f, 0.0f };
    for (size_t Index = 0U; Index < VECTOR_COMPONENT_COUNT; Index++)
    {
        JSONObjectValue Element;
        Error Result = JSONArray_Get(Array, Index, &Element);
        if ((Result.Code == ErrorCode_Success) && (Element.Type != JSONValueType_Integer) && (Element.Type != JSONValueType_RealNumber))
        {
            Result = Refuse(where, "a custom vector is an array of exactly two numbers.");
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadNumber(&Element, -UI_THEME_MAX_CUSTOM_NUMBER, UI_THEME_MAX_CUSTOM_NUMBER, where, &Components[Index]);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    *outVector = (Vector2){ .x = Components[0], .y = Components[1] };
    return Error_CreateSuccess();
}

/* A custom value, its kind told by its form: a JSON number, an array of two numbers (a vector), a string (a colour) or a
 * compound (a box, read as a box part is). */
static Error ReadCustomValue(ThemeRead* read, const JSONObjectValue* value, const char* where, UIThemeCustomValue* outValue)
{
    switch (value->Type)
    {
        case JSONValueType_Integer:
        case JSONValueType_RealNumber:
            outValue->Kind = UIThemeCustomKind_Number;
            return ReadNumber(value, -UI_THEME_MAX_CUSTOM_NUMBER, UI_THEME_MAX_CUSTOM_NUMBER, where, &outValue->Data.Number);
        case JSONValueType_Array:
            outValue->Kind = UIThemeCustomKind_Vector;
            return ReadCustomVector(value, where, &outValue->Data.Vector);
        case JSONValueType_String:
            outValue->Kind = UIThemeCustomKind_Color;
            return ReadColor(read, value, where, &outValue->Data.Color);
        case JSONValueType_Compound:
        {
            UIThemeValue Box;
            Memory_Zero(&Box, sizeof(Box));
            Box.Kind = UIThemePartKind_Box;
            Error Result = ReadBox(read, value, where, &Box);
            if (Result.Code == ErrorCode_Success)
            {
                outValue->Kind = UIThemeCustomKind_Box;
                outValue->Data.Box = Box.Data.Box;
            }
            return Result;
        }
        case JSONValueType_None:
        case JSONValueType_Boolean:
        case JSONValueType_Null:
            break;
    }
    return Refuse(where, "a custom value is a number, an array of two numbers (a vector), a string (a colour: \"#rrggbb\", "
        "\"#rrggbbaa\" or \"@<palette colour>\") or a compound (a box).");
}

/* One entry of a "custom" block into @p merged: in place of the entry of the same key, or after the others. */
static Error ReadCustomEntry(ThemeRead* read, const JSONCompoundEntry* entry, const char* where, UIThemeCustomValue* merged,
    size_t* inOutCount)
{
    char EntryWhere[WHERE_CAPACITY];
    char Printable[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(entry->Key, Printable, sizeof(Printable));
    NestWhere(EntryWhere, sizeof(EntryWhere), where, Printable);
    if (!IsLocalNameOfCapacity(entry->Key, UI_THEME_CUSTOM_KEY_CAPACITY))
    {
        return Refuse(EntryWhere, "a custom value's key is 1 to 31 of a-z, 0-9, _, . and -, starting with a letter.");
    }
    size_t Index = FindCustomIndex(merged, *inOutCount, entry->Key);
    if ((Index == *inOutCount) && (*inOutCount >= UI_THEME_MAX_CUSTOM_VALUES))
    {
        return Refuse(EntryWhere, "a style may have at most 16 custom values, a variant's own and its kind's together.");
    }
    UIThemeCustomValue Value;
    Memory_Zero(&Value, sizeof(Value));
    Memory_Copy(entry->Key, Value.Key, StringUTF8_GetByteLength(entry->Key) + 1U);
    Error Result = ReadCustomValue(read, &entry->Value, EntryWhere, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    merged[Index] = Value;
    if (Index == *inOutCount)
    {
        (*inOutCount)++;
    }
    return Error_CreateSuccess();
}

/* "custom": values a look reads by key, on top of what @p style holds (a variant's, its kind's): each entry replaces the
 * one of its key whole, or is added. The style keeps what it had when the block is refused. */
static Error ReadCustomValues(ThemeRead* read, const JSONObjectValue* value, const char* where, UIThemeStyle* style)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (JSONCompound_GetEntryCount(Compound) > UI_THEME_MAX_CUSTOM_VALUES)
    {
        return Refuse(where, "a style may have at most 16 custom values.");
    }
    CollectionEnumerator* Entries = ICollection_CreateEnumerator(JSONCompound_GetEntryCollection(Compound));
    if (Entries == NULL)
    {
        return Refuse(where, "could not read its entries.");
    }
    UIThemeCustomValue Merged[UI_THEME_MAX_CUSTOM_VALUES];
    size_t Count = style->CustomValueCount;
    if (Count > 0U)
    {
        Memory_Copy(style->CustomValues, Merged, Count * sizeof(UIThemeCustomValue));
    }
    JSONCompoundEntry Entry;
    bool HasEntry = false;
    Result = NextEntry(Entries, &Entry, &HasEntry);
    while ((Result.Code == ErrorCode_Success) && HasEntry)
    {
        Result = ReadCustomEntry(read, &Entry, where, Merged, &Count);
        if (Result.Code == ErrorCode_Success)
        {
            Result = NextEntry(Entries, &Entry, &HasEntry);
        }
    }
    CollectionEnumerator_Destroy(Entries);
    if (Result.Code == ErrorCode_Success)
    {
        Memory_Free(style->CustomValues);
        style->CustomValues = CopyCustomValues(Merged, Count);
        style->CustomValueCount = Count;
    }
    return Result;
}


// Static functions: an element.
/* The known keys of an element's block: its kind's parts and "custom", and "look" and "variants" in a kind's own block. */
static Error RejectUnknownStyleKeys(JSONCompound* compound, UIElementKind kind, bool isVariant, const char* where)
{
    const unsigned char* Keys[MAX_ELEMENT_KEYS];
    size_t Count = UIElementKind_GetPartCount(kind);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Keys[Index] = UIElementKind_GetPart(kind, (int32_t)Index)->Key;
    }
    Keys[Count] = u8"custom";
    Count++;
    if (!isVariant)
    {
        Keys[Count] = u8"look";
        Keys[Count + 1U] = u8"variants";
        Count += 2U;
    }
    return AssetJSON_RejectUnknownKeys(compound, Keys, Count, where);
}

/* One value of a style, on top of what is there. */
static Error ReadPart(ThemeRead* read, const UIThemePartDescriptor* descriptor, const JSONObjectValue* value, const char* where,
    UIThemeValue* outValue)
{
    outValue->Kind = descriptor->Kind;
    switch (descriptor->Kind)
    {
        case UIThemePartKind_Box:
            return ReadBox(read, value, where, outValue);
        case UIThemePartKind_Colors:
        {
            if (!outValue->IsSet)
            {
                Memory_Zero(&outValue->Data.Colors, sizeof(outValue->Data.Colors));
            }
            Error Result = ReadStateColors(read, value, where, &outValue->Data.Colors);
            outValue->IsSet = (Result.Code == ErrorCode_Success);
            return Result;
        }
        case UIThemePartKind_Vector:
        {
            Vector2 Vector = { 0 };
            Error Result = GameJSON_ParseVector2(value, &Vector);
            if (Result.Code != ErrorCode_Success)
            {
                return WrapError(where, &Result);
            }
            float Minimum = descriptor->Minimum;
            float Maximum = descriptor->Maximum;
            if (!(Vector.x >= Minimum) || !(Vector.x <= Maximum) || !(Vector.y >= Minimum) || !(Vector.y <= Maximum))
            {
                return Error_Construct4(ErrorCode_InvalidAssetDefinition, "%s: each component must be within [%g, %g].", where,
                    (double)Minimum, (double)Maximum);
            }
            outValue->Data.Vector = Vector;
            outValue->IsSet = true;
            return Error_CreateSuccess();
        }
        case UIThemePartKind_Number:
        {
            Error Result = ReadNumber(value, descriptor->Minimum, descriptor->Maximum, where, &outValue->Data.Number);
            outValue->IsSet = (Result.Code == ErrorCode_Success);
            return Result;
        }
    }
    return Refuse(where, "a part of an unknown kind.");
}

/* A kind's or a variant's values, on top of what @p style holds. */
static Error ReadStyle(ThemeRead* read, JSONCompound* compound, UIElementKind kind, bool isVariant, const char* where,
    UIThemeStyle* style)
{
    static const unsigned char* const LookKey[] = { u8"look" };
    if (isVariant && HasAnyKey(compound, LookKey, 1U))
    {
        char LookWhere[WHERE_CAPACITY];
        NestWhere(LookWhere, sizeof(LookWhere), where, "look");
        return Refuse(LookWhere, "only a kind's own block names a look: its variants draw with it.");
    }
    Error Result = RejectUnknownStyleKeys(compound, kind, isVariant, where);
    size_t Count = UIElementKind_GetPartCount(kind);
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        const UIThemePartDescriptor* Descriptor = UIElementKind_GetPart(kind, (int32_t)Index);
        JSONObjectValue Value;
        bool IsFound = false;
        Result = JSONCompound_GetOptional(compound, Descriptor->Key, &Value, &IsFound);
        if ((Result.Code == ErrorCode_Success) && IsFound)
        {
            char PartWhere[WHERE_CAPACITY];
            NestWhere(PartWhere, sizeof(PartWhere), where, (const char*)Descriptor->Key);
            Result = ReadPart(read, Descriptor, &Value, PartWhere, &style->Values[Index]);
        }
    }
    JSONObjectValue Custom;
    bool IsFound = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(compound, u8"custom", &Custom, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        char CustomWhere[WHERE_CAPACITY];
        NestWhere(CustomWhere, sizeof(CustomWhere), where, "custom");
        Result = ReadCustomValues(read, &Custom, CustomWhere, style);
    }
    return Result;
}

/* "variants": each a name and a block read on top of a copy of the kind's values. */
static Error ReadVariants(ThemeRead* read, const JSONObjectValue* value, UIElementKind kind, const char* where,
    UIThemeElement* element)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (JSONCompound_GetEntryCount(Compound) > UI_THEME_MAX_VARIANTS)
    {
        return Refuse(where, "an element may have at most 16 variants.");
    }
    CollectionEnumerator* Entries = ICollection_CreateEnumerator(JSONCompound_GetEntryCollection(Compound));
    if (Entries == NULL)
    {
        return Refuse(where, "could not read its entries.");
    }
    element->Variants = Memory_Allocate(UI_THEME_MAX_VARIANTS * sizeof(UIThemeVariant));
    JSONCompoundEntry Entry;
    bool HasEntry = false;
    Result = NextEntry(Entries, &Entry, &HasEntry);
    while ((Result.Code == ErrorCode_Success) && HasEntry)
    {
        char VariantWhere[WHERE_CAPACITY];
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(Entry.Key, Printable, sizeof(Printable));
        NestWhere(VariantWhere, sizeof(VariantWhere), where, Printable);
        JSONCompound* Block = NULL;
        if (!IsLocalNameOfCapacity(Entry.Key, UI_THEME_VARIANT_NAME_CAPACITY))
        {
            Result = Refuse(VariantWhere, "a variant's name is 1 to 31 of a-z, 0-9, _, . and -, starting with a letter.");
        }
        else
        {
            Result = GetCompound(&Entry.Value, VariantWhere, &Block);
        }
        if (Result.Code == ErrorCode_Success)
        {
            UIThemeVariant* Variant = &element->Variants[element->VariantCount];
            Memory_Copy(Entry.Key, Variant->Name, StringUTF8_GetByteLength(Entry.Key) + 1U);
            Variant->Style = element->Base;
            Variant->Style.CustomValues = CopyCustomValues(element->Base.CustomValues, element->Base.CustomValueCount);
            element->VariantCount++;
            Result = ReadStyle(read, Block, kind, true, VariantWhere, &Variant->Style);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = NextEntry(Entries, &Entry, &HasEntry);
        }
    }
    CollectionEnumerator_Destroy(Entries);
    return Result;
}

/* "look": the name of the registered look that draws the kind, completed with the default namespace. Only its form is
 * checked: looks are registered in code, so a theme loads whether one of that name exists or not. */
static Error ReadLookName(const JSONObjectValue* value, const char* where, UIThemeElement* element)
{
    unsigned char* Name = NULL;
    Error Result = AssetJSON_ValueToOwnedString(value, &Name);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(where, &Result);
    }
    const char* Problem = AssetJSON_MakeFullAssetName(&Name);
    size_t Size = StringUTF8_GetByteLength(Name) + 1U;
    if ((Problem == NULL) && (Size > sizeof(element->LookName)))
    {
        Problem = "a look's name is at most 128 bytes.";
    }
    if (Problem != NULL)
    {
        Memory_Free(Name);
        return Refuse(where, Problem);
    }
    Memory_Copy(Name, element->LookName, Size);
    element->HasLook = true;
    Memory_Free(Name);
    return Error_CreateSuccess();
}

/* One element's block. */
static Error ReadElement(ThemeRead* read, const JSONObjectValue* value, UIElementKind kind, const char* where,
    UIThemeElement* element)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadStyle(read, Compound, kind, false, where, &element->Base);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    element->IsPresent = true;
    JSONObjectValue Inner;
    bool IsFound = false;
    Result = JSONCompound_GetOptional(Compound, u8"look", &Inner, &IsFound);
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        char LookWhere[WHERE_CAPACITY];
        NestWhere(LookWhere, sizeof(LookWhere), where, "look");
        Result = ReadLookName(&Inner, LookWhere, element);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(Compound, u8"variants", &Inner, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        char VariantsWhere[WHERE_CAPACITY];
        NestWhere(VariantsWhere, sizeof(VariantsWhere), where, "variants");
        Result = ReadVariants(read, &Inner, kind, VariantsWhere, element);
    }
    return Result;
}

/* "elements": a block per kind; an unknown kind is refused. */
static Error ReadElements(ThemeRead* read, const JSONObjectValue* value, const char* where)
{
    JSONCompound* Compound = NULL;
    Error Result = GetCompound(value, where, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const unsigned char* Names[UIElementKind_Count];
    for (int32_t Kind = 0; Kind < (int32_t)UIElementKind_Count; Kind++)
    {
        Names[Kind] = UIElementKind_GetName((UIElementKind)Kind);
    }
    Result = AssetJSON_RejectUnknownKeys(Compound, Names, (size_t)UIElementKind_Count, where);
    for (int32_t Kind = 0; (Kind < (int32_t)UIElementKind_Count) && (Result.Code == ErrorCode_Success); Kind++)
    {
        JSONObjectValue Element;
        bool IsFound = false;
        Result = JSONCompound_GetOptional(Compound, Names[Kind], &Element, &IsFound);
        if ((Result.Code == ErrorCode_Success) && IsFound)
        {
            char ElementWhere[WHERE_CAPACITY];
            NestWhere(ElementWhere, sizeof(ElementWhere), where, (const char*)Names[Kind]);
            Result = ReadElement(read, &Element, (UIElementKind)Kind, ElementWhere, &read->Theme->Elements[Kind]);
        }
    }
    return Result;
}


// Static functions: the root.
/* "font": the text's font, completed with the default namespace. */
static Error ReadFontName(JSONCompound* root, const char* themeName, UITheme* theme)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(root, u8"font", &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    char Where[WHERE_CAPACITY];
    RootWhere(Where, sizeof(Where), themeName, "font");
    Result = AssetJSON_ValueToOwnedString(&Value, &theme->FontName);
    if (Result.Code != ErrorCode_Success)
    {
        return WrapError(Where, &Result);
    }
    const char* Problem = AssetJSON_MakeFullAssetName(&theme->FontName);
    return (Problem != NULL) ? Refuse(Where, Problem) : Error_CreateSuccess();
}

/* A number of the root within bounds, when the file gives it; @p outValue keeps its default otherwise. */
static Error ReadRootNumber(JSONCompound* root, const char* themeName, const unsigned char* key, float minimum, float maximum,
    float* outValue)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(root, key, &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    char Where[WHERE_CAPACITY];
    RootWhere(Where, sizeof(Where), themeName, (const char*)key);
    return ReadNumber(&Value, minimum, maximum, Where, outValue);
}

/* A block of the root read by @p reader, when the file gives it. */
static Error ReadRootBlock(ThemeRead* read, JSONCompound* root, const char* themeName, const unsigned char* key,
    RootBlockReader reader)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(root, key, &Value, &IsFound);
    if ((Result.Code != ErrorCode_Success) || !IsFound)
    {
        return Result;
    }
    char Where[WHERE_CAPACITY];
    RootWhere(Where, sizeof(Where), themeName, (const char*)key);
    return reader(read, &Value, Where);
}

/* Everything after the name. The palette comes before the elements whatever the file's order: their colours may name it. */
static Error ParseDefinition(UITheme* theme, const unsigned char* name, JSONCompound* root)
{
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(name, Name, sizeof(Name));
    char Where[WHERE_CAPACITY];
    snprintf(Where, sizeof(Where), "UI theme \"%s\"", Name);
    Error Result = AssetJSON_RejectUnknownKeys(root, RootKeys, sizeof(RootKeys) / sizeof(RootKeys[0]), Where);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadFontName(root, Name, theme);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRootNumber(root, Name, u8"text_size", UI_THEME_MIN_TEXT_SIZE, UI_THEME_MAX_TEXT_SIZE, &theme->TextSize);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRootNumber(root, Name, u8"transition_time", 0.0f, UI_THEME_MAX_TRANSITION_SECONDS, &theme->TransitionSeconds);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRootNumber(root, Name, u8"tooltip_delay", 0.0f, UI_THEME_MAX_TOOLTIP_DELAY_SECONDS, &theme->TooltipDelaySeconds);
    }
    ThemeRead Read = { .Theme = theme };
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRootBlock(&Read, root, Name, u8"palette", ReadPalette);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadRootBlock(&Read, root, Name, u8"elements", ReadElements);
    }
    return Result;
}


// Static functions: the data's lifetime.
static void ReleaseThemeData(UITheme* theme)
{
    Memory_Free(theme->Name);
    Memory_Free(theme->FontName);
    for (int32_t Kind = 0; Kind < (int32_t)UIElementKind_Count; Kind++)
    {
        UIThemeElement* Element = &theme->Elements[Kind];
        Memory_Free(Element->Base.CustomValues);
        for (size_t Index = 0U; Index < Element->VariantCount; Index++)
        {
            Memory_Free(Element->Variants[Index].Style.CustomValues);
        }
        Memory_Free(Element->Variants);
    }
    for (size_t Index = 0U; Index < theme->TextureCount; Index++)
    {
        Memory_Free(theme->TextureNames[Index]);
    }
    Memory_Free(theme->TextureNames);
}

static unsigned char* CopyString(const unsigned char* text)
{
    if (text == NULL)
    {
        return NULL;
    }
    size_t Size = StringUTF8_GetByteLength(text) + 1U;
    unsigned char* Copy = Memory_Allocate(Size);
    Memory_Copy(text, Copy, Size);
    return Copy;
}

/* A deep copy of what a definition read, for the loaded theme: its variants and every style's custom values are blocks of
 * its own. The texture names stay with the definition. */
static void CopyThemeData(const UITheme* source, UITheme* copy)
{
    *copy = *source;
    copy->Name = NULL;
    copy->FontName = CopyString(source->FontName);
    copy->TextureNames = NULL;
    copy->TextureCount = 0U;
    for (int32_t Kind = 0; Kind < (int32_t)UIElementKind_Count; Kind++)
    {
        const UIThemeElement* Element = &source->Elements[Kind];
        UIThemeElement* Copied = &copy->Elements[Kind];
        Copied->Base.CustomValues = CopyCustomValues(Element->Base.CustomValues, Element->Base.CustomValueCount);
        Copied->Variants = NULL;
        if (Element->VariantCount == 0U)
        {
            continue;
        }
        size_t Bytes = Element->VariantCount * sizeof(UIThemeVariant);
        Copied->Variants = Memory_Allocate(Bytes);
        Memory_Copy(Element->Variants, Copied->Variants, Bytes);
        for (size_t Index = 0U; Index < Element->VariantCount; Index++)
        {
            const UIThemeStyle* Style = &Element->Variants[Index].Style;
            Copied->Variants[Index].Style.CustomValues = CopyCustomValues(Style->CustomValues, Style->CustomValueCount);
        }
    }
}


// Static functions: loading.
static void LogLoadProblem(AssetManager* manager, const char* themeName, const char* what, const unsigned char* name, Error* cause)
{
    Logger* Log = AssetManager_GetLogger(manager);
    if (Log != NULL)
    {
        char Printable[NAME_PRINT_CAPACITY];
        AssetJSON_CopyPrintable(name, Printable, sizeof(Printable));
        Error LogResult = Logger_LogWarningFormatted(Log, (const unsigned char*)u8"UI theme \"%s\": its %s \"%s\": %s", themeName,
            what, Printable, ((cause != NULL) && (cause->Message != NULL)) ? (const char*)cause->Message
                : "it does not fit: the parts using it draw as plain boxes.");
        Error_Deconstruct(&LogResult);
    }
    if (cause != NULL)
    {
        Error_Deconstruct(cause);
    }
}

/* Whether a nine-slice's art fits its texture and every rectangle its margins, once the texture's size is known. Fills in
 * the whole texture as normal's rectangle when the theme gives none. */
static bool ResolveNineSliceArt(UIBoxStyle* box, const GameTexture* texture)
{
    Texture2D Raylib = GameTexture_GetRaylibTexture(texture);
    float Width = (float)Raylib.width;
    float Height = (float)Raylib.height;
    if (!box->HasSource[UIElementState_Normal])
    {
        box->Sources[UIElementState_Normal] = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = Width, .height = Height };
        box->HasSource[UIElementState_Normal] = true;
    }
    for (int32_t State = 0; State < (int32_t)UIElementState_Count; State++)
    {
        if (!box->HasSource[State])
        {
            continue;
        }
        Rectangle Source = box->Sources[State];
        bool IsInside = ((Source.x + Source.width) <= Width) && ((Source.y + Source.height) <= Height);
        bool DoMarginsFit = ((box->Margins.Left + box->Margins.Right) <= Source.width)
            && ((box->Margins.Top + box->Margins.Bottom) <= Source.height);
        if (!IsInside || !DoMarginsFit)
        {
            return false;
        }
    }
    return true;
}

/* Points a nine-slice box at its texture. One whose texture is missing or whose art does not fit stays without one, and
 * draws plain. */
static void ResolveBoxTexture(AssetManager* manager, const char* themeName, UIBoxStyle* box, const UITheme* definition,
    const GameTexture* const* textures)
{
    if ((box->Kind != UIBoxKind_NineSlice) || (textures[box->TextureIndex] == NULL))
    {
        return;
    }
    if (ResolveNineSliceArt(box, textures[box->TextureIndex]))
    {
        box->Texture = textures[box->TextureIndex];
        return;
    }
    LogLoadProblem(manager, themeName, "texture", definition->TextureNames[box->TextureIndex], NULL);
}

/* Points every nine-slice of a style, its parts' and its custom boxes', at its texture. A kind's values hold no pointer
 * from the definition, so the copy starts clean. */
static void ResolveStyleTextures(AssetManager* manager, const char* themeName, UIThemeStyle* style,
    const UITheme* definition, const GameTexture* const* textures, UIElementKind kind)
{
    size_t Count = UIElementKind_GetPartCount(kind);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const UIThemePartDescriptor* Descriptor = UIElementKind_GetPart(kind, (int32_t)Index);
        if ((Descriptor->Kind == UIThemePartKind_Box) && style->Values[Index].IsSet)
        {
            ResolveBoxTexture(manager, themeName, &style->Values[Index].Data.Box, definition, textures);
        }
    }
    for (size_t Index = 0U; Index < style->CustomValueCount; Index++)
    {
        if (style->CustomValues[Index].Kind == UIThemeCustomKind_Box)
        {
            ResolveBoxTexture(manager, themeName, &style->CustomValues[Index].Data.Box, definition, textures);
        }
    }
}

static void UIThemeLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    (void)manager;
    UITheme* Loaded = self->DestroyContext;
    ReleaseThemeData(Loaded); // its textures and font are dependencies: the manager releases them
    Memory_Free(Loaded);
}

static const LoadedAssetVTable UIThemeLoadedVTable = { UIThemeLoaded_Destroy };

/* Loads the font a theme names; one that does not load leaves the built-in font. */
static void LoadThemeFont(AssetManager* manager, AssetUserID dependencyUser, const char* themeName, UITheme* loaded)
{
    if (loaded->FontName == NULL)
    {
        return;
    }
    AssetTypeID FontTypeId = ASSET_TYPE_ID_INVALID;
    Error Result = AssetManager_GetAssetTypeByName(manager, ASSET_TYPE_NAME_FONT, &FontTypeId);
    void* Asset = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetManager_LoadAssetSingle(manager, FontTypeId, loaded->FontName, dependencyUser, &Asset);
    }
    if (Result.Code != ErrorCode_Success)
    {
        LogLoadProblem(manager, themeName, "font", loaded->FontName, &Result);
        Memory_Free(loaded->FontName);
        loaded->FontName = NULL;
    }
}

static Error UIThemeDefinition_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    UIThemeDefinition* Definition = self;
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(Definition->Base.Name, Name, sizeof(Name));

    AssetTypeID TextureType = ASSET_TYPE_ID_INVALID;
    Error TypeResult = AssetManager_GetAssetTypeByName(manager, ASSET_TYPE_NAME_TEXTURE, &TextureType);
    if (TypeResult.Code != ErrorCode_Success)
    {
        return TypeResult;
    }

    UITheme* Loaded = Memory_Allocate(sizeof(UITheme));
    CopyThemeData(&Definition->Data, Loaded);
    Loaded->Name = CopyString(Definition->Base.Name);
    LoadThemeFont(manager, dependencyUser, Name, Loaded);

    const GameTexture* Textures[UI_THEME_MAX_TEXTURES];
    for (size_t Index = 0U; Index < Definition->Data.TextureCount; Index++)
    {
        void* Asset = NULL;
        Error Result = AssetManager_LoadAssetSingle(manager, TextureType, Definition->Data.TextureNames[Index], dependencyUser, &Asset);
        if (Result.Code != ErrorCode_Success)
        {
            LogLoadProblem(manager, Name, "texture", Definition->Data.TextureNames[Index], &Result);
        }
        Textures[Index] = Asset;
    }
    for (int32_t Kind = 0; Kind < (int32_t)UIElementKind_Count; Kind++)
    {
        UIThemeElement* Element = &Loaded->Elements[Kind];
        ResolveStyleTextures(manager, Name, &Element->Base, &Definition->Data, Textures, (UIElementKind)Kind);
        for (size_t Variant = 0U; Variant < Element->VariantCount; Variant++)
        {
            ResolveStyleTextures(manager, Name, &Element->Variants[Variant].Style, &Definition->Data, Textures, (UIElementKind)Kind);
        }
    }

    outLoaded->Asset = Loaded;
    outLoaded->VTable = &UIThemeLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static void UIThemeDefinition_Destroy(void* self)
{
    UIThemeDefinition* Definition = self;
    ReleaseThemeData(&Definition->Data);
    Memory_Free(Definition->Base.Name);
    Memory_Free(Definition);
}

static const AssetDefinitionVTable UIThemeDefinitionVTable = { UIThemeDefinition_LoadAsset, UIThemeDefinition_Destroy };


// Public functions.
Error UIThemeDefinition_Construct(AssetManager* manager, const UserData* userData, const GenericBuffer* rawData,
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

    UIThemeDefinition* Definition = Memory_Allocate(sizeof(UIThemeDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &UIThemeDefinitionVTable;
    Definition->Data.TextSize = UI_THEME_DEFAULT_TEXT_SIZE;
    Definition->Data.TooltipDelaySeconds = UI_THEME_DEFAULT_TOOLTIP_DELAY_SECONDS;
    Result = AssetJSON_ReadName(RootCompound, sourceDescription, &Definition->Base.Name);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ParseDefinition(&Definition->Data, Definition->Base.Name, RootCompound);
    }

    Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Root);
    Error_Deconstruct(&ReturnResult);

    if (Result.Code != ErrorCode_Success)
    {
        UIThemeDefinition_Destroy(Definition);
        return Result;
    }
    *outDefinition = &Definition->Base;
    return Error_CreateSuccess();
}
