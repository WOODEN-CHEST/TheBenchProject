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
 * VARIANTS MERGE WHEN READ. A variant starts as a copy of its kind's values and what it names is read on top, down to one
 * state's colour, so it changes only what it names and a lookup needs no fallback chain.
 *
 * WHAT A THEME SURVIVES. A texture that does not load, or whose art does not fit its margins, only makes its parts plain;
 * a font that does not load only leaves the built-in one. Each is logged once, when the theme loads. A broken art file in a
 * pack should not take the whole UI's look away.
 */


// Macros.
/** How many bytes of a name from the file a message shows, and room for a message's "where". */
#define NAME_PRINT_CAPACITY ((size_t)129)
#define WHERE_CAPACITY ((size_t)320)
/** The elements' known keys: a kind's parts and "variants". */
#define MAX_ELEMENT_KEYS (UI_THEME_MAX_PARTS + 1U)
/** The largest number a nine-slice's rectangle or margin may hold, in texels. */
#define MAX_TEXELS 16384.0f
/** The size of a margins array: left, top, right, bottom. */
#define MARGIN_COUNT 4U


// Types.
typedef struct UIThemeDefinitionStruct
{
    AssetDefinition Base;
    UITheme Data;
} UIThemeDefinition;

/* What a parse carries down the theme's blocks: the theme, whose table of texture names grows as boxes name them. */
typedef struct ThemeReadStruct
{
    UITheme* Theme;
} ThemeRead;


// Fields.
static const unsigned char* const RootKeys[] = { u8"name", u8"font", u8"text_size", u8"transition_time", u8"elements" };
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

/* A colour per state, on top of what @p colors already holds (a variant's merge). */
static Error ReadStateColors(const JSONObjectValue* value, const char* where, UIStateColors* colors)
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
            Color Parsed = WHITE;
            Result = AssetJSON_ParseColor(&Entry.Value, &Parsed);
            if (Result.Code != ErrorCode_Success)
            {
                Result = WrapError(where, &Result);
            }
            else
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
static Error ReadOutline(const JSONObjectValue* value, const char* where, UIBoxStyle* box)
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
        Result = ReadStateColors(&Inner, where, &box->OutlineColors);
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
        Result = ReadStateColors(&Inner, where, &Box->Colors);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(Compound, u8"outline", &Inner, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = (Box->Kind == UIBoxKind_Flat) ? ReadOutline(&Inner, where, Box)
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


// Static functions: an element.
/* The known keys of an element's block: its kind's parts, and "variants" in a kind's own block. */
static Error RejectUnknownStyleKeys(JSONCompound* compound, UIElementKind kind, bool isVariant, const char* where)
{
    const unsigned char* Keys[MAX_ELEMENT_KEYS];
    size_t Count = UIElementKind_GetPartCount(kind);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Keys[Index] = UIElementKind_GetPart(kind, (int32_t)Index)->Key;
    }
    if (!isVariant)
    {
        Keys[Count] = u8"variants";
        Count++;
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
            Error Result = ReadStateColors(value, where, &outValue->Data.Colors);
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
            if (!(Vector.x >= 0.0f) || !(Vector.x <= UI_THEME_MAX_METRIC) || !(Vector.y >= 0.0f) || !(Vector.y <= UI_THEME_MAX_METRIC))
            {
                return Refuse(where, "each component must be within [0, 4096].");
            }
            outValue->Data.Vector = Vector;
            outValue->IsSet = true;
            return Error_CreateSuccess();
        }
        case UIThemePartKind_Number:
        {
            Error Result = ReadNumber(value, 0.0f, UI_THEME_MAX_METRIC, where, &outValue->Data.Number);
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
        size_t Length = StringUTF8_GetByteLength(Entry.Key);
        JSONCompound* Block = NULL;
        if (!NamespacedKey_IsValidLocalName(Entry.Key) || (Length >= UI_THEME_VARIANT_NAME_CAPACITY))
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
            Memory_Copy(Entry.Key, Variant->Name, Length + 1U);
            Variant->Style = element->Base;
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
    JSONObjectValue Variants;
    bool IsFound = false;
    Result = JSONCompound_GetOptional(Compound, u8"variants", &Variants, &IsFound);
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        char VariantsWhere[WHERE_CAPACITY];
        NestWhere(VariantsWhere, sizeof(VariantsWhere), where, "variants");
        Result = ReadVariants(read, &Variants, kind, VariantsWhere, element);
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

/* Everything after the name. */
static Error ParseDefinition(UITheme* theme, const unsigned char* name, JSONCompound* root)
{
    char Name[NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable(name, Name, sizeof(Name));
    char Where[WHERE_CAPACITY];
    snprintf(Where, sizeof(Where), "UI theme \"%s\"", Name);
    Error Result = AssetJSON_RejectUnknownKeys(root, RootKeys, sizeof(RootKeys) / sizeof(RootKeys[0]), Where);

    JSONObjectValue Value;
    bool IsFound = false;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(root, u8"font", &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = AssetJSON_ValueToOwnedString(&Value, &theme->FontName);
        if (Result.Code != ErrorCode_Success)
        {
            return WrapError(Where, &Result);
        }
        const char* Problem = AssetJSON_MakeFullAssetName(&theme->FontName);
        if (Problem != NULL)
        {
            return Refuse(Where, Problem);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(root, u8"text_size", &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadNumber(&Value, UI_THEME_MIN_TEXT_SIZE, UI_THEME_MAX_TEXT_SIZE, Where, &theme->TextSize);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(root, u8"transition_time", &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        Result = ReadNumber(&Value, 0.0f, UI_THEME_MAX_TRANSITION_SECONDS, Where, &theme->TransitionSeconds);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONCompound_GetOptional(root, u8"elements", &Value, &IsFound);
    }
    if ((Result.Code == ErrorCode_Success) && IsFound)
    {
        char ElementsWhere[WHERE_CAPACITY];
        snprintf(ElementsWhere, sizeof(ElementsWhere), "UI theme \"%s\", elements", Name);
        ThemeRead Read = { .Theme = theme };
        Result = ReadElements(&Read, &Value, ElementsWhere);
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
        Memory_Free(theme->Elements[Kind].Variants);
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

/* A deep copy of what a definition read, for the loaded theme; the texture names stay with the definition. */
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
        copy->Elements[Kind].Variants = NULL;
        if (Element->VariantCount > 0U)
        {
            size_t Bytes = Element->VariantCount * sizeof(UIThemeVariant);
            copy->Elements[Kind].Variants = Memory_Allocate(Bytes);
            Memory_Copy(Element->Variants, copy->Elements[Kind].Variants, Bytes);
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

/* Points every nine-slice of a style at its texture. A part whose texture is missing or whose art does not fit stays
 * without one, and draws plain. A kind's values hold no pointer from the definition, so the copy starts clean. */
static void ResolveStyleTextures(AssetManager* manager, const char* themeName, UIThemeStyle* style,
    const UITheme* definition, const GameTexture* const* textures, UIElementKind kind)
{
    size_t Count = UIElementKind_GetPartCount(kind);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const UIThemePartDescriptor* Descriptor = UIElementKind_GetPart(kind, (int32_t)Index);
        if ((Descriptor->Kind != UIThemePartKind_Box) || !style->Values[Index].IsSet)
        {
            continue;
        }
        UIBoxStyle* Box = &style->Values[Index].Data.Box;
        if ((Box->Kind != UIBoxKind_NineSlice) || (textures[Box->TextureIndex] == NULL))
        {
            continue;
        }
        if (ResolveNineSliceArt(Box, textures[Box->TextureIndex]))
        {
            Box->Texture = textures[Box->TextureIndex];
        }
        else
        {
            LogLoadProblem(manager, themeName, "texture", definition->TextureNames[Box->TextureIndex], NULL);
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
