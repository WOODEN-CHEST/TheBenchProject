#pragma once
#include "UITheme.h"


/*
 * Private to the theme's module (UITheme.c, the lookups; UIThemeDefinition.c, the asset type): the data behind the opaque
 * types. A definition holds one of these as read, a loaded asset a deep copy with its textures resolved.
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
    } Data;
} UIThemeValue;

struct UIThemeStyleStruct
{
    UIThemeValue Values[UI_THEME_MAX_PARTS];
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
    UIThemeElement Elements[UIElementKind_Count];
    unsigned char** TextureNames; /* Owned, each and the list; a box's TextureIndex points here. */
    size_t TextureCount;
};
