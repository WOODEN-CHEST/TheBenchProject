#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector2/Color come from Raylib and RenderColor from the renderer: both are part of the components' public
// property data. GenericBuffer backs the subcomponent lists and the owned strings.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRMemory.h"
#include "wr/WRError.h"


/**
 * @file TextComponent.h
 * @brief Renderer-independent, Minecraft-style rich text components.
 *
 * A text component is a mutable node that can be rendered inside text: a string, a translatable string, a sprite
 * animation, or nothing. Components are not tied to the UI; they are used wherever the game shows rich text.
 * Every component embeds the TextComponent base as its first member, so a pointer to any concrete component is a
 * valid TextComponent*. The Type discriminator selects the concrete kind (use the TextComponent_As* casts).
 *
 * A STRICT TREE. Every component has at most one parent, and the parent OWNS its children: destroying a component
 * destroys its subtree. A component that already has a parent cannot be added anywhere else (detach it first),
 * and a component can never be added inside its own subtree. A component without a parent is a ROOT: whoever
 * created it (or detached it) owns it and must eventually hand it to a TextDocument or return it with
 * TextComponentFactory_ReturnTree, which refuses a component that is still attached. A root given to a TextDocument is
 * HELD by it (TextComponent_IsHeld): it can be edited in place, but adding it to a parent, giving it to another
 * document or returning it is refused until the document lets it go (TextDocument_TakeRoot).
 *
 * DEPTH LIMIT. A tree has at most TEXT_COMPONENT_MAX_DEPTH levels (a root alone is one level). Adding a child that
 * would exceed it fails, and both deserializers enforce the same limit, so every walk over a tree is bounded.
 *
 * OWNED STRINGS. Components copy every string they are given (text, font and animation names, translation keys)
 * into buffers borrowed from their factory, and give them back when they are destroyed. Callers never have to keep
 * a string alive for a component. String getters return pointers into that storage: valid until the property is
 * set again or the component is destroyed.
 *
 * ASSETS BY NAME ONLY. Fonts and sprite animations are referred to by asset name. A component never holds a font or
 * an animation; the TextDocument that owns the tree binds names to loaded assets when it lays the text out. A NULL
 * font name means the document's default font.
 *
 * REVISIONS. Every change to a component (a property, a child added, detached or destroyed) gives it and all its
 * ancestors a new revision number, unique across the factory. Comparing a root's revision with a stored one tells
 * whether anything in the tree changed (TextLayout uses this to know when to lay the text out again).
 *
 * LIFECYCLE. Components are created, cloned and returned only through the TextComponentFactory
 * (TextComponentFactory.h). Not thread-safe; use from the main thread.
 */


// Macros.
/** @brief Maximum number of levels in a component tree; a root without children is one level. */
#define TEXT_COMPONENT_MAX_DEPTH (64)
/** @brief Largest accepted string size and sprite size component (normalized-fitted units, see TextLayout.h). */
#define TEXT_COMPONENT_SIZE_MAX (100.0f)
/** @brief Default render size (line height) of a string component (normalized-fitted units). */
#define STRING_COMPONENT_DEFAULT_SIZE (0.1f)
/** @brief Largest accepted magnitude of each shadow offset component (fraction of the component size). */
#define STRING_COMPONENT_SHADOW_OFFSET_LIMIT (10.0f)
/** @brief Default character spacing of a string component (multiplier of the size). */
#define STRING_COMPONENT_DEFAULT_SPACING (0.0f)
/** @brief Smallest accepted character spacing (multiplier of the size). */
#define STRING_COMPONENT_SPACING_MIN (-1.0f)
/** @brief Largest accepted character spacing (multiplier of the size). */
#define STRING_COMPONENT_SPACING_MAX (10.0f)
/** @brief Default shadow-active state of a string component. */
#define STRING_COMPONENT_DEFAULT_IS_SHADOW_ACTIVE (true)


// Types.
/** @brief The factory that creates and recycles components; full type in TextComponentFactory.h. */
typedef struct TextComponentFactoryStruct TextComponentFactory;

/**
 * @brief Discriminates the concrete kind of a TextComponent.
 *
 * Set once at creation and never changed. Switches over it handle every value without a `default:` branch, so
 * adding a type fails the build wherever it is not handled yet.
 */
typedef enum TextComponentTypeEnum
{
    /** @brief A component with no own content (EmptyComponent); it still lays out its children. */
    TextComponentType_Empty,
    /** @brief A component carrying a string (StringComponent). */
    TextComponentType_String,
    /** @brief A component rendering a sprite animation inline (SpriteComponent). */
    TextComponentType_Sprite,
    /** @brief A string looked up by translation key, with a fallback text (TranslateComponent). */
    TextComponentType_Translate
} TextComponentType;

/**
 * @brief Selects where a string component's shadow color comes from.
 */
typedef enum TextShadowColorTypeEnum
{
    /** @brief Derive the shadow color from the component color (its brightness reduced) when drawing. */
    TextShadowColorType_Default,
    /** @brief Use the component's explicit shadow color. */
    TextShadowColorType_Custom
} TextShadowColorType;

/**
 * @brief Selects where a string component's shadow offset comes from.
 */
typedef enum TextShadowOffsetTypeEnum
{
    /** @brief Use the shadow offset of the font the text is drawn in (its definition's "shadow_offset", see
     *         GameFont_GetShadowOffset), which suits that font's stroke weight. */
    TextShadowOffsetType_Default,
    /** @brief Use the component's explicit shadow offset. */
    TextShadowOffsetType_Custom
} TextShadowOffsetType;

/**
 * @brief Virtual table of a concrete component type.
 *
 * Each concrete type supplies one static instance; self is the concrete component.
 */
typedef struct TextComponentVTableStruct
{
    /**
     * @brief Releases what the concrete component owns: its strings and its subcomponent list buffer (not the
     *        children themselves, and not the component's pool slot, which the factory reclaims).
     * @param self The concrete component (its base is the first member).
     */
    void (*Destroy)(void* self);
} TextComponentVTable;

/**
 * @brief Abstract base of every text component: the type, the owning factory, the tree links and the revision.
 *
 * Embed as the first member of a concrete component. All fields are read-only outside this module; use the
 * TextComponent_* functions.
 */
typedef struct TextComponentStruct
{
    /** @brief Behavior table of the concrete type; never NULL for a live component. */
    const TextComponentVTable* VTable;
    /** @brief The concrete kind; set at creation, never changed. */
    TextComponentType _type;
    /** @brief The factory that created the component and owns its storage; borrowed. */
    TextComponentFactory* _factory;
    /** @brief The owning parent, or NULL for a root. */
    struct TextComponentStruct* _parent;
    /** @brief Ordered child pointers (element size sizeof(TextComponent*)), borrowed from the factory's buffer pool
     *         on the first add; NULL while the component never had children. */
    GenericBuffer* _subComponents;
    /** @brief Changes (to a new factory-wide unique value) whenever the component or anything below it changes. */
    uint64_t _revision;
    /** @brief Number of levels in the component's subtree: 1 without children, at most TEXT_COMPONENT_MAX_DEPTH. */
    uint32_t _height;
    /** @brief true while a TextDocument holds this root as its tree. */
    bool _isHeld;
} TextComponent;

/**
 * @brief A component carrying a UTF-8 string plus its font name, color, size, shadow, decorations and spacing.
 *
 * All strings are owned (copied on set). Create with TextComponentFactory_CreateString or one of the number and
 * special-character shorthands.
 */
typedef struct StringComponentStruct
{
    /** @brief Abstract base; must be the first member. */
    TextComponent Base;

    /** @brief Owned NUL-terminated UTF-8 text (factory buffer), or NULL for an empty text. */
    GenericBuffer* _text;
    /** @brief Owned NUL-terminated font asset name (factory buffer), or NULL for the document's default font. */
    GenericBuffer* _fontName;
    /** @brief Text color; defaults to opaque, full-brightness white. */
    RenderColor _color;
    /** @brief Line height in normalized-fitted units, in [0, TEXT_COMPONENT_SIZE_MAX]; defaults to
     *         STRING_COMPONENT_DEFAULT_SIZE. */
    float _size;
    /** @brief Whether a shadow is drawn behind the text; defaults to STRING_COMPONENT_DEFAULT_IS_SHADOW_ACTIVE. */
    bool _isShadowActive;
    /** @brief Where the shadow color comes from; defaults to TextShadowColorType_Default. */
    TextShadowColorType _shadowColorType;
    /** @brief Explicit shadow color; used only when @c _shadowColorType is TextShadowColorType_Custom. */
    RenderColor _shadowColor;
    /** @brief Where the shadow offset comes from; defaults to TextShadowOffsetType_Default (the font's). */
    TextShadowOffsetType _shadowOffsetType;
    /** @brief Explicit shadow offset as a fraction of the size, each component within
     *         +-STRING_COMPONENT_SHADOW_OFFSET_LIMIT; used only when @c _shadowOffsetType is TextShadowOffsetType_Custom. */
    Vector2 _shadowOffset;
    /** @brief Whether the text is underlined; defaults to false. */
    bool _isUnderlined;
    /** @brief Whether the text is struck through; defaults to false. */
    bool _isStrikethrough;
    /** @brief Extra space after each character as a multiplier of the size, in
     *         [STRING_COMPONENT_SPACING_MIN, STRING_COMPONENT_SPACING_MAX]; defaults to STRING_COMPONENT_DEFAULT_SPACING. */
    float _spacing;
} StringComponent;

/**
 * @brief A component that renders a sprite animation inline, referred to by its asset name.
 *
 * It always takes up its size in the layout; it draws the current frame of its document's instance of the named
 * animation, or nothing when there is no name or the animation cannot be loaded. Create with
 * TextComponentFactory_CreateSprite.
 */
typedef struct SpriteComponentStruct
{
    /** @brief Abstract base; must be the first member. */
    TextComponent Base;

    /** @brief Owned NUL-terminated sprite-animation asset name (factory buffer), or NULL to draw nothing. */
    GenericBuffer* _animationName;
    /** @brief Render color; defaults to opaque, full-brightness white. */
    RenderColor _color;
    /** @brief Box size in normalized-fitted units, each component in [0, TEXT_COMPONENT_SIZE_MAX]; defaults to (0, 0). */
    Vector2 _size;
} SpriteComponent;

/**
 * @brief A string looked up by translation key, like Minecraft's translate component.
 *
 * It embeds a StringComponent as its first member: that string part carries all the styling, and its text is the
 * fallback, shown when the document has no translation for the key. Without a fallback the key itself is shown.
 * The StringComponent_* functions work on the string part (TranslateComponent_AsString). Create with
 * TextComponentFactory_CreateTranslate.
 */
typedef struct TranslateComponentStruct
{
    /** @brief The string part (styling plus the fallback text); must be the first member. */
    StringComponent Base;
    /** @brief Owned NUL-terminated translation key (factory buffer); never NULL for a live component. */
    GenericBuffer* _key;
} TranslateComponent;

/**
 * @brief A component with no own content; it renders nothing itself but lays out its children.
 *
 * Create with TextComponentFactory_CreateEmpty.
 */
typedef struct EmptyComponentStruct
{
    /** @brief Abstract base; must be the first member. */
    TextComponent Base;
} EmptyComponent;


// Identity and tree queries.
/**
 * @brief Returns the component's concrete kind.
 * @param self The component; must not be NULL.
 * @returns The type discriminator.
 */
static inline TextComponentType TextComponent_GetType(const TextComponent* self)
{
    return self->_type;
}

/**
 * @brief Returns the factory that created the component.
 * @param self The component; must not be NULL.
 * @returns The borrowed factory.
 */
static inline TextComponentFactory* TextComponent_GetFactory(const TextComponent* self)
{
    return self->_factory;
}

/**
 * @brief Returns the component's parent.
 * @param self The component; must not be NULL.
 * @returns The borrowed parent, or NULL for a root.
 */
static inline TextComponent* TextComponent_GetParent(const TextComponent* self)
{
    return self->_parent;
}

/**
 * @brief Returns the component's revision: a factory-wide unique number that changes whenever the component or
 *        anything in its subtree changes.
 * @param self The component; must not be NULL.
 * @returns The current revision.
 */
static inline uint64_t TextComponent_GetRevision(const TextComponent* self)
{
    return self->_revision;
}

/**
 * @brief Reports whether a TextDocument holds this root as its tree. A held root can be edited in place, but adding it
 *        to a parent, giving it to another document and returning it to the factory are refused.
 * @param self The component; must not be NULL.
 * @returns true while a document holds the component.
 */
static inline bool TextComponent_IsHeld(const TextComponent* self)
{
    return self->_isHeld;
}

/**
 * @brief Returns the number of levels in the component's subtree (1 without children).
 * @param self The component; must not be NULL.
 * @returns The subtree height, at most TEXT_COMPONENT_MAX_DEPTH.
 */
static inline size_t TextComponent_GetHeight(const TextComponent* self)
{
    return (size_t)self->_height;
}

/**
 * @brief Returns the root of the tree the component belongs to (the component itself for a root). O(depth).
 * @param self The component; must not be NULL.
 * @returns The borrowed root.
 */
TextComponent* TextComponent_GetRoot(TextComponent* self);

/**
 * @brief Returns the component's level in its tree: 1 for a root, 2 for its children, and so on. O(depth).
 * @param self The component; must not be NULL.
 * @returns The depth, at most TEXT_COMPONENT_MAX_DEPTH.
 */
size_t TextComponent_GetDepth(const TextComponent* self);

/**
 * @brief Reports whether @p self is @p other or one of its ancestors. O(depth).
 * @param self The candidate ancestor; may be NULL.
 * @param other The component to test; may be NULL.
 * @returns true if @p other lies in @p self's subtree (including @p self itself); false otherwise or for NULL.
 */
bool TextComponent_IsAncestorOf(const TextComponent* self, const TextComponent* other);


// Subcomponents.
/**
 * @brief Appends a root component as the last child of @p self, which then owns it.
 *
 * @p child must be a root (no parent) from the same factory that no document holds, must not be @p self or one of its
 * ancestors, and the tree must stay within TEXT_COMPONENT_MAX_DEPTH levels: depth(@p self) + height(@p child) <= the
 * limit. O(depth) plus the list append.
 * @param self The parent; must not be NULL.
 * @param child The root to add; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL or @p child comes from another factory;
 *          ErrorCode_InvalidOperation if @p child already has a parent, is held by a document, the addition would
 *          create a cycle or the tree would exceed TEXT_COMPONENT_MAX_DEPTH levels; ErrorCode_BufferTooLarge if the
 *          list could not grow. On failure @p child stays where it was.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponent_AddSubComponent(TextComponent* self, TextComponent* child);

/**
 * @brief Inserts a root component as the child at @p index, shifting later children right.
 *
 * Same rules as TextComponent_AddSubComponent.
 * @param self The parent; must not be NULL.
 * @param index Insertion index in [0, count]; equal to the count appends.
 * @param child The root to insert; must not be NULL.
 * @returns Success; ErrorCode_IndexOutOfBounds if @p index exceeds the child count; otherwise as
 *          TextComponent_AddSubComponent. On failure @p child stays a root owned by the caller.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponent_InsertSubComponent(TextComponent* self, size_t index, TextComponent* child);

/**
 * @brief Removes the child at @p index from @p self and hands it to the caller as a root.
 *
 * The caller then owns the detached subtree: add it elsewhere, give it to a document, or return it with
 * TextComponentFactory_ReturnTree.
 * @param self The parent; must not be NULL.
 * @param index Index of the child; must be less than the child count.
 * @param outChild [out] Receives the detached child; must not be NULL. Set to NULL on failure.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_IndexOutOfBounds if @p index is not
 *          less than the child count.
 */
Error TextComponent_DetachSubComponentAt(TextComponent* self, size_t index, TextComponent** outChild);

/**
 * @brief Detaches the component from its parent, making it a root owned by the caller.
 *
 * A root is left unchanged (success). O(child count of the parent).
 * @param self The component; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidState if the parent's child list
 *          does not contain the component (a corrupted tree).
 */
Error TextComponent_Detach(TextComponent* self);

/**
 * @brief Removes the child at @p index and returns its whole subtree to the factory.
 * @param self The parent; must not be NULL.
 * @param index Index of the child; must be less than the child count.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_IndexOutOfBounds if @p index is not
 *          less than the child count.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponent_DestroySubComponentAt(TextComponent* self, size_t index);

/**
 * @brief Removes every child and returns their subtrees to the factory.
 * @param self The parent; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; otherwise the first error met while returning
 *          (every child is still removed and returned).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponent_ClearSubComponents(TextComponent* self);

/**
 * @brief Returns the number of direct children.
 * @param self The component; may be NULL.
 * @returns The child count, or 0 for NULL.
 */
size_t TextComponent_GetSubComponentCount(const TextComponent* self);

/**
 * @brief Returns the child at @p index.
 * @param self The component; may be NULL.
 * @param index Index of the child.
 * @returns The borrowed child (owned by @p self), or NULL if @p self is NULL or @p index is out of range.
 */
TextComponent* TextComponent_GetSubComponentAt(const TextComponent* self, size_t index);


// Casts.
/**
 * @brief Returns the string part of a string or translate component.
 * @param self The component; must not be NULL.
 * @returns The StringComponent (the component itself, or a translate component's string part), or NULL for other
 *          types.
 */
static inline StringComponent* TextComponent_AsStringComponent(TextComponent* self)
{
    if ((self->_type == TextComponentType_String) || (self->_type == TextComponentType_Translate))
    {
        return (StringComponent*)self;
    }
    return NULL;
}

/**
 * @brief Read-only variant of TextComponent_AsStringComponent.
 * @param self The component; must not be NULL.
 * @returns The string part, or NULL for types without one.
 */
static inline const StringComponent* TextComponent_AsConstStringComponent(const TextComponent* self)
{
    if ((self->_type == TextComponentType_String) || (self->_type == TextComponentType_Translate))
    {
        return (const StringComponent*)self;
    }
    return NULL;
}

/**
 * @brief Downcasts to a sprite component.
 * @param self The component; must not be NULL.
 * @returns The SpriteComponent, or NULL if the component is not a sprite.
 */
static inline SpriteComponent* TextComponent_AsSpriteComponent(TextComponent* self)
{
    return (self->_type == TextComponentType_Sprite) ? (SpriteComponent*)self : NULL;
}

/**
 * @brief Read-only variant of TextComponent_AsSpriteComponent.
 * @param self The component; must not be NULL.
 * @returns The SpriteComponent, or NULL if the component is not a sprite.
 */
static inline const SpriteComponent* TextComponent_AsConstSpriteComponent(const TextComponent* self)
{
    return (self->_type == TextComponentType_Sprite) ? (const SpriteComponent*)self : NULL;
}

/**
 * @brief Downcasts to a translate component.
 * @param self The component; must not be NULL.
 * @returns The TranslateComponent, or NULL if the component is not a translate component.
 */
static inline TranslateComponent* TextComponent_AsTranslateComponent(TextComponent* self)
{
    return (self->_type == TextComponentType_Translate) ? (TranslateComponent*)self : NULL;
}

/**
 * @brief Read-only variant of TextComponent_AsTranslateComponent.
 * @param self The component; must not be NULL.
 * @returns The TranslateComponent, or NULL if the component is not a translate component.
 */
static inline const TranslateComponent* TextComponent_AsConstTranslateComponent(const TextComponent* self)
{
    return (self->_type == TextComponentType_Translate) ? (const TranslateComponent*)self : NULL;
}

/**
 * @brief Returns a string component's base.
 * @param self The string component; must not be NULL.
 * @returns The embedded TextComponent.
 */
static inline TextComponent* StringComponent_AsComponent(StringComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns a sprite component's base.
 * @param self The sprite component; must not be NULL.
 * @returns The embedded TextComponent.
 */
static inline TextComponent* SpriteComponent_AsComponent(SpriteComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns an empty component's base.
 * @param self The empty component; must not be NULL.
 * @returns The embedded TextComponent.
 */
static inline TextComponent* EmptyComponent_AsComponent(EmptyComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns a translate component's base.
 * @param self The translate component; must not be NULL.
 * @returns The embedded TextComponent.
 */
static inline TextComponent* TranslateComponent_AsComponent(TranslateComponent* self)
{
    return &self->Base.Base;
}

/**
 * @brief Returns a translate component's string part (styling and fallback text).
 * @param self The translate component; must not be NULL.
 * @returns The embedded StringComponent.
 */
static inline StringComponent* TranslateComponent_AsString(TranslateComponent* self)
{
    return &self->Base;
}


// StringComponent properties (also the string part of a translate component).
/**
 * @brief Returns the component's text (a translate component's fallback text).
 * @param self The string component; must not be NULL.
 * @returns The owned NUL-terminated UTF-8 text, never NULL (an empty string when unset). Valid until the text is
 *          set again or the component is destroyed.
 */
static inline const unsigned char* StringComponent_GetText(const StringComponent* self)
{
    return (self->_text != NULL) ? self->_text->_data : (const unsigned char*)u8"";
}

/**
 * @brief Sets the component's text (a translate component's fallback text) to a copy of @p text.
 * @param self The string component; must not be NULL.
 * @param text NUL-terminated UTF-8 text, or NULL for an empty text. Copied; the caller keeps ownership.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidTextEncoding if @p text is not
 *          valid UTF-8. On failure the old text is kept.
 */
Error StringComponent_SetText(StringComponent* self, const unsigned char* text);

/**
 * @brief Returns the component's font asset name.
 * @param self The string component; must not be NULL.
 * @returns The owned NUL-terminated name, or NULL when the component uses the document's default font.
 */
static inline const unsigned char* StringComponent_GetFontName(const StringComponent* self)
{
    return (self->_fontName != NULL) ? self->_fontName->_data : NULL;
}

/**
 * @brief Sets the component's font asset name to a copy of @p fontName.
 * @param self The string component; must not be NULL.
 * @param fontName A font asset name (matching AssetManager_IsValidAssetName), or NULL for the document's default
 *        font. Copied.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p fontName is
 *          not a valid asset name.
 */
Error StringComponent_SetFontName(StringComponent* self, const unsigned char* fontName);

/**
 * @brief Returns the component's text color.
 * @param self The string component; must not be NULL.
 * @returns The color.
 */
static inline RenderColor StringComponent_GetColor(const StringComponent* self)
{
    return self->_color;
}

/**
 * @brief Sets the component's text color.
 * @param self The string component; must not be NULL.
 * @param color The color; Brightness and Opacity must be finite and within [0, 1].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if Brightness or
 *          Opacity is out of range.
 */
Error StringComponent_SetColor(StringComponent* self, RenderColor color);

/**
 * @brief Returns the component's size (line height, normalized-fitted units).
 * @param self The string component; must not be NULL.
 * @returns The size.
 */
static inline float StringComponent_GetSize(const StringComponent* self)
{
    return self->_size;
}

/**
 * @brief Sets the component's size (line height, normalized-fitted units).
 * @param self The string component; must not be NULL.
 * @param size The size; finite and within [0, TEXT_COMPONENT_SIZE_MAX].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p size is out of
 *          range.
 */
Error StringComponent_SetSize(StringComponent* self, float size);

/**
 * @brief Reports whether a shadow is drawn behind the text.
 * @param self The string component; must not be NULL.
 * @returns true if the shadow is active.
 */
static inline bool StringComponent_IsShadowActive(const StringComponent* self)
{
    return self->_isShadowActive;
}

/**
 * @brief Turns the shadow behind the text on or off.
 * @param self The string component; must not be NULL.
 * @param isShadowActive Whether to draw the shadow.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error StringComponent_SetIsShadowActive(StringComponent* self, bool isShadowActive);

/**
 * @brief Returns where the component's shadow color comes from.
 * @param self The string component; must not be NULL.
 * @returns The shadow color type.
 */
static inline TextShadowColorType StringComponent_GetShadowColorType(const StringComponent* self)
{
    return self->_shadowColorType;
}

/**
 * @brief Returns the component's explicit shadow color (meaningful only when the type is Custom).
 * @param self The string component; must not be NULL.
 * @returns The stored custom shadow color.
 */
static inline RenderColor StringComponent_GetShadowColor(const StringComponent* self)
{
    return self->_shadowColor;
}

/**
 * @brief Sets an explicit shadow color and switches the shadow color type to Custom.
 * @param self The string component; must not be NULL.
 * @param color The shadow color; Brightness and Opacity must be finite and within [0, 1].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if Brightness or
 *          Opacity is out of range.
 */
Error StringComponent_SetShadowColorCustom(StringComponent* self, RenderColor color);

/**
 * @brief Switches the shadow color type back to Default (derived from the component color when drawing).
 * @param self The string component; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error StringComponent_SetShadowColorDefault(StringComponent* self);

/**
 * @brief Returns where the component's shadow offset comes from.
 * @param self The string component; must not be NULL.
 * @returns The shadow offset type.
 */
static inline TextShadowOffsetType StringComponent_GetShadowOffsetType(const StringComponent* self)
{
    return self->_shadowOffsetType;
}

/**
 * @brief Returns the component's explicit shadow offset (meaningful only when the type is Custom).
 * @param self The string component; must not be NULL.
 * @returns The stored custom offset, as a fraction of the component size.
 */
static inline Vector2 StringComponent_GetShadowOffset(const StringComponent* self)
{
    return self->_shadowOffset;
}

/**
 * @brief Returns the offset the shadow is drawn at: the component's own when the type is Custom, else @p fontOffset.
 * @param self The string component; must not be NULL.
 * @param fontOffset The shadow offset of the font the text is drawn in (GameFont_GetShadowOffset).
 * @returns The offset, as a fraction of the component size.
 */
static inline Vector2 StringComponent_ResolveShadowOffset(const StringComponent* self, Vector2 fontOffset)
{
    return (self->_shadowOffsetType == TextShadowOffsetType_Custom) ? self->_shadowOffset : fontOffset;
}

/**
 * @brief Sets an explicit shadow offset and switches the shadow offset type to Custom.
 * @param self The string component; must not be NULL.
 * @param offset The offset, as a fraction of the component size; each component finite and within
 *        +-STRING_COMPONENT_SHADOW_OFFSET_LIMIT.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a component is out
 *          of range.
 */
Error StringComponent_SetShadowOffsetCustom(StringComponent* self, Vector2 offset);

/**
 * @brief Switches the shadow offset type back to Default (the offset of the font the text is drawn in).
 * @param self The string component; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error StringComponent_SetShadowOffsetDefault(StringComponent* self);

/**
 * @brief Reports whether the text is underlined.
 * @param self The string component; must not be NULL.
 * @returns true if underlined.
 */
static inline bool StringComponent_IsUnderlined(const StringComponent* self)
{
    return self->_isUnderlined;
}

/**
 * @brief Turns underlining on or off.
 * @param self The string component; must not be NULL.
 * @param isUnderlined Whether the text is underlined.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error StringComponent_SetIsUnderlined(StringComponent* self, bool isUnderlined);

/**
 * @brief Reports whether the text is struck through.
 * @param self The string component; must not be NULL.
 * @returns true if struck through.
 */
static inline bool StringComponent_IsStrikethrough(const StringComponent* self)
{
    return self->_isStrikethrough;
}

/**
 * @brief Turns the strikethrough on or off.
 * @param self The string component; must not be NULL.
 * @param isStrikethrough Whether the text is struck through.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error StringComponent_SetIsStrikethrough(StringComponent* self, bool isStrikethrough);

/**
 * @brief Returns the character spacing (multiplier of the size).
 * @param self The string component; must not be NULL.
 * @returns The spacing.
 */
static inline float StringComponent_GetSpacing(const StringComponent* self)
{
    return self->_spacing;
}

/**
 * @brief Sets the character spacing (multiplier of the size).
 * @param self The string component; must not be NULL.
 * @param spacing The spacing; finite and within [STRING_COMPONENT_SPACING_MIN, STRING_COMPONENT_SPACING_MAX].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p spacing is out
 *          of range.
 */
Error StringComponent_SetSpacing(StringComponent* self, float spacing);


// SpriteComponent properties.
/**
 * @brief Returns the sprite's animation asset name.
 * @param self The sprite component; must not be NULL.
 * @returns The owned NUL-terminated name, or NULL when the sprite draws nothing.
 */
static inline const unsigned char* SpriteComponent_GetAnimationName(const SpriteComponent* self)
{
    return (self->_animationName != NULL) ? self->_animationName->_data : NULL;
}

/**
 * @brief Sets the sprite's animation asset name to a copy of @p animationName.
 * @param self The sprite component; must not be NULL.
 * @param animationName A sprite-animation asset name (matching AssetManager_IsValidAssetName), or NULL to draw
 *        nothing. Copied.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p animationName is not a valid asset name.
 */
Error SpriteComponent_SetAnimationName(SpriteComponent* self, const unsigned char* animationName);

/**
 * @brief Returns the sprite's render color.
 * @param self The sprite component; must not be NULL.
 * @returns The color.
 */
static inline RenderColor SpriteComponent_GetColor(const SpriteComponent* self)
{
    return self->_color;
}

/**
 * @brief Sets the sprite's render color.
 * @param self The sprite component; must not be NULL.
 * @param color The color; Brightness and Opacity must be finite and within [0, 1].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if Brightness or
 *          Opacity is out of range.
 */
Error SpriteComponent_SetColor(SpriteComponent* self, RenderColor color);

/**
 * @brief Returns the sprite's box size (normalized-fitted units).
 * @param self The sprite component; must not be NULL.
 * @returns The size.
 */
static inline Vector2 SpriteComponent_GetSize(const SpriteComponent* self)
{
    return self->_size;
}

/**
 * @brief Sets the sprite's box size (normalized-fitted units).
 * @param self The sprite component; must not be NULL.
 * @param size The size; each component finite and within [0, TEXT_COMPONENT_SIZE_MAX].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a component is
 *          out of range.
 */
Error SpriteComponent_SetSize(SpriteComponent* self, Vector2 size);


// TranslateComponent properties (styling and the fallback text live in the string part).
/**
 * @brief Returns the translation key.
 * @param self The translate component; must not be NULL.
 * @returns The owned NUL-terminated key, never NULL.
 */
static inline const unsigned char* TranslateComponent_GetKey(const TranslateComponent* self)
{
    return self->_key->_data;
}

/**
 * @brief Sets the translation key to a copy of @p key.
 * @param self The translate component; must not be NULL.
 * @param key The key; must not be NULL and must match TextComponent_IsValidTranslationKey. Copied.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p key is NULL; ErrorCode_ArgumentOutOfRange if @p key
 *          is not a valid key.
 */
Error TranslateComponent_SetKey(TranslateComponent* self, const unsigned char* key);

/**
 * @brief Returns the fallback text shown when the key has no translation (the string part's text).
 * @param self The translate component; must not be NULL.
 * @returns The owned NUL-terminated fallback, never NULL (empty when unset: the key is shown instead).
 */
static inline const unsigned char* TranslateComponent_GetFallback(const TranslateComponent* self)
{
    return StringComponent_GetText(&self->Base);
}

/**
 * @brief Sets the fallback text (the string part's text) to a copy of @p fallback.
 * @param self The translate component; must not be NULL.
 * @param fallback NUL-terminated UTF-8 text, or NULL for none (the key is shown instead).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidTextEncoding if @p fallback is
 *          not valid UTF-8.
 */
Error TranslateComponent_SetFallback(TranslateComponent* self, const unsigned char* fallback);


// Validation.
/**
 * @brief Reports whether @p key is a valid translation key: the asset-name grammar `[a-z][a-z0-9_.-]*`, so keys can
 *        be namespaced the same way as asset names later (for example "paper.form_17.title").
 * @param key The key; may be NULL (invalid).
 * @returns true if valid.
 */
bool TextComponent_IsValidTranslationKey(const unsigned char* key);
