#pragma once
#include <stdbool.h>
// Style values reuse the component property types (colors, fonts, sizes) and the shadow color enum.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "GameFont.h"
#include "TextComponent.h"
#include "wr/WRError.h"


/**
 * @file TextStyle.h
 * @brief A reusable, presence-guarded bundle of text-component style properties.
 *
 * A TextStyle carries every style-related property a component might have (a superset across all
 * component types, NOT a union), each guarded by a presence flag. Applying a style copies only the
 * present properties onto a component, and only the ones relevant to that component's concrete type
 * (e.g. the string size is ignored by a sprite component). Absent properties leave the component
 * unchanged, so styles compose and can express partial overrides.
 *
 * Construct with TextStyle_Construct (everything absent), configure with the Set/Clear helpers, then
 * apply with TextStyle_ApplyToComponent. The struct owns nothing and needs no deconstruction.
 */


// Types.
/**
 * @brief A set of optional text style properties, each with a presence flag.
 *
 * Underscore-prefixed fields are internal; use the TextStyle_* helpers. A property is applied only when
 * its presence flag is set.
 */
typedef struct TextStyleStruct
{
    /** @brief Whether the font property is present. */
    bool _hasFont;
    /** @brief Font to apply (string components). */
    GameFont _font;

    /** @brief Whether the color property is present. */
    bool _hasColor;
    /** @brief Color to apply (string and sprite components). */
    RenderColor _color;

    /** @brief Whether the string-size property is present. */
    bool _hasStringSize;
    /** @brief Render size to apply to string components. */
    float _stringSize;

    /** @brief Whether the sprite-size property is present. */
    bool _hasSpriteSize;
    /** @brief Render size to apply to sprite components. */
    Vector2 _spriteSize;

    /** @brief Whether the shadow-active property is present. */
    bool _hasIsShadowActive;
    /** @brief Shadow-active value to apply (string components). */
    bool _isShadowActive;

    /** @brief Whether the shadow-color directive is present. */
    bool _hasShadowColor;
    /** @brief Which shadow color mode to apply when present (Default or Custom). */
    TextShadowColorType _shadowColorType;
    /** @brief Custom shadow color to apply when the directive type is Custom. */
    RenderColor _shadowColor;

    /** @brief Whether the shadow-offset property is present. */
    bool _hasShadowOffset;
    /** @brief Shadow offset to apply (string components). */
    Vector2 _shadowOffset;

    /** @brief Whether the underline property is present. */
    bool _hasIsUnderlined;
    /** @brief Underline value to apply (string components). */
    bool _isUnderlined;

    /** @brief Whether the strikethrough property is present. */
    bool _hasIsStrikethrough;
    /** @brief Strikethrough value to apply (string components). */
    bool _isStrikethrough;

    /** @brief Whether the spacing property is present. */
    bool _hasSpacing;
    /** @brief Spacing value to apply (string components). */
    float _spacing;
} TextStyle;


// Lifecycle.
/**
 * @brief Initializes a style with every property absent.
 * @param self The style to initialize; must not be NULL.
 */
void TextStyle_Construct(TextStyle* self);


// Font.
/**
 * @brief Sets the style's font property (marks it present).
 * @param self The style; must not be NULL.
 * @param font The font to store.
 */
void TextStyle_SetFont(TextStyle* self, GameFont font);
/**
 * @brief Reads the style's font property.
 * @param self The style; must not be NULL.
 * @param outFont [out] Receives the font when present; must not be NULL.
 * @returns true if the property is present (and written), false otherwise.
 */
bool TextStyle_GetFont(const TextStyle* self, GameFont* outFont);
/**
 * @brief Clears the style's font property (marks it absent).
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearFont(TextStyle* self);


// Color (string and sprite components).
/**
 * @brief Sets the style's color property (marks it present).
 * @param self The style; must not be NULL.
 * @param color The color to store.
 */
void TextStyle_SetColor(TextStyle* self, RenderColor color);
/**
 * @brief Reads the style's color property.
 * @param self The style; must not be NULL.
 * @param outColor [out] Receives the color when present; must not be NULL.
 * @returns true if present, false otherwise.
 */
bool TextStyle_GetColor(const TextStyle* self, RenderColor* outColor);
/**
 * @brief Clears the style's color property.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearColor(TextStyle* self);


// String size.
/**
 * @brief Sets the style's string-size property (marks it present).
 * @param self The style; must not be NULL.
 * @param size The size to store.
 */
void TextStyle_SetStringSize(TextStyle* self, float size);
/**
 * @brief Reads the style's string-size property.
 * @param self The style; must not be NULL.
 * @param outSize [out] Receives the size when present; must not be NULL.
 * @returns true if present, false otherwise.
 */
bool TextStyle_GetStringSize(const TextStyle* self, float* outSize);
/**
 * @brief Clears the style's string-size property.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearStringSize(TextStyle* self);


// Sprite size.
/**
 * @brief Sets the style's sprite-size property (marks it present).
 * @param self The style; must not be NULL.
 * @param size The size to store.
 */
void TextStyle_SetSpriteSize(TextStyle* self, Vector2 size);
/**
 * @brief Reads the style's sprite-size property.
 * @param self The style; must not be NULL.
 * @param outSize [out] Receives the size when present; must not be NULL.
 * @returns true if present, false otherwise.
 */
bool TextStyle_GetSpriteSize(const TextStyle* self, Vector2* outSize);
/**
 * @brief Clears the style's sprite-size property.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearSpriteSize(TextStyle* self);


// Shadow active.
/**
 * @brief Sets the style's shadow-active property (marks it present).
 * @param self The style; must not be NULL.
 * @param isShadowActive The value to store.
 */
void TextStyle_SetIsShadowActive(TextStyle* self, bool isShadowActive);
/**
 * @brief Reads the style's shadow-active property.
 * @param self The style; must not be NULL.
 * @param outIsShadowActive [out] Receives the value when present; must not be NULL.
 * @returns true if present, false otherwise.
 */
bool TextStyle_GetIsShadowActive(const TextStyle* self, bool* outIsShadowActive);
/**
 * @brief Clears the style's shadow-active property.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearIsShadowActive(TextStyle* self);


// Shadow color directive.
/**
 * @brief Sets the style to apply a custom shadow color (marks the shadow-color directive present).
 * @param self The style; must not be NULL.
 * @param color The custom shadow color to store.
 */
void TextStyle_SetShadowColorCustom(TextStyle* self, RenderColor color);
/**
 * @brief Sets the style to apply the default (derived) shadow color (marks the directive present).
 * @param self The style; must not be NULL.
 */
void TextStyle_SetShadowColorDefault(TextStyle* self);
/**
 * @brief Reads the style's shadow-color directive.
 * @param self The style; must not be NULL.
 * @param outType [out] Receives the shadow color type when present; must not be NULL.
 * @param outColor [out] Receives the custom color (valid when the type is Custom); must not be NULL.
 * @returns true if the directive is present, false otherwise.
 */
bool TextStyle_GetShadowColor(const TextStyle* self, TextShadowColorType* outType, RenderColor* outColor);
/**
 * @brief Clears the style's shadow-color directive.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearShadowColor(TextStyle* self);


// Shadow offset.
/**
 * @brief Sets the style's shadow-offset property (marks it present).
 * @param self The style; must not be NULL.
 * @param offset The offset to store.
 */
void TextStyle_SetShadowOffset(TextStyle* self, Vector2 offset);
/**
 * @brief Reads the style's shadow-offset property.
 * @param self The style; must not be NULL.
 * @param outOffset [out] Receives the offset when present; must not be NULL.
 * @returns true if present, false otherwise.
 */
bool TextStyle_GetShadowOffset(const TextStyle* self, Vector2* outOffset);
/**
 * @brief Clears the style's shadow-offset property.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearShadowOffset(TextStyle* self);


// Underline.
/**
 * @brief Sets the style's underline property (marks it present).
 * @param self The style; must not be NULL.
 * @param isUnderlined The value to store.
 */
void TextStyle_SetIsUnderlined(TextStyle* self, bool isUnderlined);
/**
 * @brief Reads the style's underline property.
 * @param self The style; must not be NULL.
 * @param outIsUnderlined [out] Receives the value when present; must not be NULL.
 * @returns true if present, false otherwise.
 */
bool TextStyle_GetIsUnderlined(const TextStyle* self, bool* outIsUnderlined);
/**
 * @brief Clears the style's underline property.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearIsUnderlined(TextStyle* self);


// Strikethrough.
/**
 * @brief Sets the style's strikethrough property (marks it present).
 * @param self The style; must not be NULL.
 * @param isStrikethrough The value to store.
 */
void TextStyle_SetIsStrikethrough(TextStyle* self, bool isStrikethrough);
/**
 * @brief Reads the style's strikethrough property.
 * @param self The style; must not be NULL.
 * @param outIsStrikethrough [out] Receives the value when present; must not be NULL.
 * @returns true if present, false otherwise.
 */
bool TextStyle_GetIsStrikethrough(const TextStyle* self, bool* outIsStrikethrough);
/**
 * @brief Clears the style's strikethrough property.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearIsStrikethrough(TextStyle* self);


// Spacing.
/**
 * @brief Sets the style's spacing property (marks it present).
 * @param self The style; must not be NULL.
 * @param spacing The value to store.
 */
void TextStyle_SetSpacing(TextStyle* self, float spacing);
/**
 * @brief Reads the style's spacing property.
 * @param self The style; must not be NULL.
 * @param outSpacing [out] Receives the value when present; must not be NULL.
 * @returns true if present, false otherwise.
 */
bool TextStyle_GetSpacing(const TextStyle* self, float* outSpacing);
/**
 * @brief Clears the style's spacing property.
 * @param self The style; must not be NULL.
 */
void TextStyle_ClearSpacing(TextStyle* self);


// Apply.
/**
 * @brief Applies the present, type-relevant style properties onto a component.
 *
 * Properties that are absent, or that do not apply to the component's concrete type, are skipped. The
 * component's validating setters are used, so an invalid stored value (e.g. a non-finite size) surfaces
 * as an error; application is best-effort and continues past a failing property, returning the first
 * error encountered.
 * @param self The style to apply; must not be NULL.
 * @param component The component to style; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p component is NULL; otherwise the first
 *          non-success Error raised by a component setter.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextStyle_ApplyToComponent(const TextStyle* self, TextComponent* component);
