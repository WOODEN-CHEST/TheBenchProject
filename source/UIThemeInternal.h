#pragma once
#include "NamespacedKey.h"
#include "UITheme.h"


/*
 * Private to the theme's module (UITheme.c, the lookups; UIThemeDefinition.c, the asset type): the data behind the opaque
 * types. A definition holds one of these as read, a loaded asset a deep copy with its textures resolved.
 *
 * A style's custom values are its one owned block: whatever copies a style (a variant made from its kind, the loaded copy
 * of a definition) gives the copy a block of its own at once, so no two styles ever share one.
 */


// Types.
/* One value of a style: a flag saying a theme gave it, and the value by its kind. */
typedef struct UIThemeValueStruct
{
    bool IsSet;
    /** The part's kind: which of the union's fields it holds. */
    UIThemePartKind Kind;
    union
    {
        UIBoxStyle Box;
        UIStateColors Colors;
        Vector2 Vector;
        float Number;
        uint32_t Character;
    } Data;
} UIThemeValue;

/* What a custom value is, told by the form it was written in. */
typedef enum UIThemeCustomKindEnum
{
    UIThemeCustomKind_Number,
    UIThemeCustomKind_Vector,
    UIThemeCustomKind_Color,
    UIThemeCustomKind_Box
} UIThemeCustomKind;

/* One custom value of a style: its key and its value by its kind. */
typedef struct UIThemeCustomValueStruct
{
    unsigned char Key[UI_THEME_CUSTOM_KEY_CAPACITY];
    /** Which of the union's fields it holds. */
    UIThemeCustomKind Kind;
    union
    {
        float Number;
        Vector2 Vector;
        Color Color;
        UIBoxStyle Box;
    } Data;
} UIThemeCustomValue;

struct UIThemeStyleStruct
{
    UIThemeValue Values[UI_THEME_MAX_PARTS];
    UIThemeCustomValue* CustomValues; /* Owned, CustomValueCount of them; NULL for none. */
    size_t CustomValueCount;
};

/* A named variant: the kind's values with what it names on top, merged when read. */
typedef struct UIThemeVariantStruct
{
    unsigned char Name[UI_THEME_VARIANT_NAME_CAPACITY];
    UIThemeStyle Style;
} UIThemeVariant;

typedef struct UIThemeElementStruct
{
    bool IsPresent;
    bool HasLook;
    unsigned char LookName[NAMESPACED_KEY_BUFFER_SIZE]; /* The look's full name, when HasLook. */
    UIThemeStyle Base;
    UIThemeVariant* Variants; /* Owned; VariantCount of them. */
    size_t VariantCount;
} UIThemeElement;

struct UIThemeStruct
{
    unsigned char* Name;     /* Owned by a loaded theme; a definition's is its asset name, not kept here. */
    unsigned char* FontName; /* Owned, NULL for none. */
    float TextSize;
    float TransitionSeconds;
    float TooltipDelaySeconds;
    UIThemeElement Elements[UIElementKind_Count];
    unsigned char** TextureNames; /* Owned, each and the list; a box's TextureIndex points here. */
    size_t TextureCount;
};
