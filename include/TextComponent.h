#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector2/Color come from Raylib; RenderColor from the renderer; GameFont and SpriteAnimationInstance
// are component payloads. GenericBuffer backs the subcomponent list. All are part of the public data.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "GameFont.h"
#include "SpriteAnimation.h"
#include "wr/WRMemory.h"
#include "wr/WRError.h"


/**
 * @file TextComponent.h
 * @brief Renderer-independent, Minecraft-style rich text components.
 *
 * A text component is an abstract, mutable node that can be rendered inside text. It is NOT limited to
 * strings — a component may carry a string, a sprite animation, or nothing — and it is NOT tied to the UI;
 * components are used across the game (UI, world, logging, etc.). Every component embeds the abstract
 * TextComponent base as its FIRST member, so a pointer to any concrete component is a valid
 * TextComponent*; a Type discriminator supports rendering and serialization dispatch.
 *
 * COMPOSITION. Each component owns an ordered list of subcomponents (child components), so components
 * chain into a tree. Children are referenced, not owned: adding a component as a subcomponent stores a
 * borrowed pointer to it. A component's own properties are per-component and are NOT inherited by its
 * children. A component may not be added as a subcomponent of itself or of any component within its own
 * subtree (cycles are rejected).
 *
 * LIFECYCLE. Components are created, cloned and released exclusively through the TextComponentFactory
 * (see TextComponentFactory.h), which pools the component structs and the subcomponent list buffers. The
 * constructors declared here are the factory's building blocks and are not meant to be called by end
 * users. A component does NOT own the strings, textures or sprite instances handed to it; those remain
 * owned by the caller and must outlive the component.
 *
 * FUTURE TYPES. More component types are expected over time; each new type embeds the base and registers
 * with the factory. The current set is String, Sprite and Empty.
 */


// Macros.
/** @brief Default render size of a string component (normalized, see TextComponentRenderer). */
#define STRING_COMPONENT_DEFAULT_SIZE (0.1f)
/** @brief Default relative shadow offset on the X axis (fraction of the component size). */
#define STRING_COMPONENT_DEFAULT_SHADOW_OFFSET_X (-0.1f)
/** @brief Default relative shadow offset on the Y axis (fraction of the component size). */
#define STRING_COMPONENT_DEFAULT_SHADOW_OFFSET_Y (0.1f)
/** @brief Default character spacing of a string component (multiplier of the size). */
#define STRING_COMPONENT_DEFAULT_SPACING (0.0f)
/** @brief Default shadow-active state of a string component. */
#define STRING_COMPONENT_DEFAULT_IS_SHADOW_ACTIVE (true)


// Types.
/**
 * @brief Discriminates the concrete kind of a TextComponent.
 *
 * Set once at construction and never changed; used to downcast a TextComponent* to its concrete type and
 * to drive rendering/serialization.
 */
typedef enum TextComponentTypeEnum
{
    /** @brief An empty component with no own content (EmptyComponent). */
    TextComponentType_Empty,
    /** @brief A component carrying a string (StringComponent). */
    TextComponentType_String,
    /** @brief A component carrying a sprite animation (SpriteComponent). */
    TextComponentType_Sprite
} TextComponentType;

/**
 * @brief Selects where a string component's shadow color comes from.
 *
 * Default derives the shadow color from the component's own color at render time; Custom uses the
 * explicit shadow color stored on the component.
 */
typedef enum TextShadowColorTypeEnum
{
    /** @brief Derive the shadow color from the component color (its brightness reduced) at render time. */
    TextShadowColorType_Default,
    /** @brief Use the component's explicitly-set shadow color. */
    TextShadowColorType_Custom
} TextShadowColorType;

/**
 * @brief Virtual table of behavior for a concrete text component.
 *
 * A concrete type supplies one static instance. The self parameter is the concrete component (recovered
 * from void* without a cast, per the project's OOP convention).
 */
typedef struct TextComponentVTableStruct
{
    /**
     * @brief Releases the resources a concrete component owns and deconstructs the embedded base.
     *
     * Contract for text components: this frees only what the concrete component itself owns (the current
     * types own nothing beyond borrowed references) and clears the base fields. It does NOT free the
     * component's pooled storage or its subcomponent list buffer — the owning TextComponentFactory
     * reclaims those when the component is returned. Invoked by the factory during a return.
     * @param self The concrete component (its Base is the first member).
     */
    void (*Destroy)(void* self);
} TextComponentVTable;

/**
 * @brief Abstract base of every text component: type discriminator plus the ordered subcomponent list.
 *
 * Embed as the FIRST member of a concrete component. The subcomponent buffer is borrowed from the owning
 * factory (element type TextComponent*) and is not owned by the component. Underscore-prefixed fields are
 * read-only to code outside this module; use the TextComponent_* helpers.
 */
typedef struct TextComponentStruct
{
    /** @brief Behavior table for this component; set by TextComponent_Construct. Never NULL afterwards. */
    const TextComponentVTable* VTable;
    /** @brief The concrete kind of this component; set at construction, never changed. */
    TextComponentType Type;
    /** @brief Ordered list of child TextComponent* (element size sizeof(TextComponent*)); borrowed from the factory. */
    GenericBuffer* _subComponents;
} TextComponent;

/**
 * @brief A text component carrying a UTF-8 string plus its font, color, size, shadow and decoration.
 *
 * The text is a borrowed, NUL-terminated UTF-8 string validated for encoding on assignment; the component
 * never owns it. Embeds the abstract base first. Create through the TextComponentFactory.
 */
typedef struct StringComponentStruct
{
    /** @brief Abstract base; must be the first member. */
    TextComponent Base;

    /** @brief Borrowed, NUL-terminated UTF-8 text; not owned, must outlive the component. May be NULL. */
    const unsigned char* _text;
    /** @brief Font to render the text with; defaults to the raylib default font. */
    GameFont _font;
    /** @brief Borrowed, NUL-terminated UTF-8 font asset reference name for serialization/binding; not owned,
     *         may be NULL. Records which font asset @c _font came from (a GameFont cannot report its name).
     *         Bound to a live @c _font by the TextComponentResolver. */
    const unsigned char* _fontName;
    /** @brief Text color; defaults to opaque, full-brightness white. */
    RenderColor _color;
    /** @brief Render size of the text (see TextComponentRenderer); defaults to STRING_COMPONENT_DEFAULT_SIZE. */
    float _size;

    /** @brief Whether a shadow is drawn behind the text; defaults to STRING_COMPONENT_DEFAULT_IS_SHADOW_ACTIVE. */
    bool IsShadowActive;
    /** @brief Where the shadow color comes from; defaults to TextShadowColorType_Default. */
    TextShadowColorType _shadowColorType;
    /** @brief Explicit shadow color; used only when @c _shadowColorType is TextShadowColorType_Custom. */
    RenderColor _shadowColor;
    /** @brief Relative shadow offset (fraction of the component size); defaults to (-0.1, 0.1). */
    Vector2 _shadowOffset;

    /** @brief Whether the text is underlined; defaults to false. */
    bool IsUnderlined;
    /** @brief Whether the text is struck through; defaults to false. */
    bool IsStrikethrough;
    /** @brief Character spacing as a multiplier of the size; defaults to STRING_COMPONENT_DEFAULT_SPACING. */
    float _spacing;
} StringComponent;

/**
 * @brief A text component that renders a sprite animation inline.
 *
 * The animation instance is borrowed (not owned) and may be NULL, in which case the component renders
 * nothing (behaving like an empty component). Embeds the abstract base first. Create through the
 * TextComponentFactory.
 */
typedef struct SpriteComponentStruct
{
    /** @brief Abstract base; must be the first member. */
    TextComponent Base;

    /** @brief Borrowed sprite animation instance to render; not owned, may be NULL (renders nothing). */
    SpriteAnimationInstance* _animationInstance;
    /** @brief Borrowed, NUL-terminated UTF-8 sprite-animation asset reference name for serialization/binding;
     *         not owned, may be NULL. Records which animation asset to use (an instance cannot report its
     *         name). Bound to a live @c _animationInstance by the TextComponentResolver. */
    const unsigned char* _animationName;
    /** @brief Render color/tint; defaults to opaque, full-brightness white. */
    RenderColor _color;
    /** @brief Render size of the sprite (see TextComponentRenderer for units); defaults to (0, 0). */
    Vector2 _size;
} SpriteComponent;

/**
 * @brief A text component with no own content.
 *
 * Renders nothing itself but can still hold subcomponents. Embeds the abstract base first. Create through
 * the TextComponentFactory.
 */
typedef struct EmptyComponentStruct
{
    /** @brief Abstract base; must be the first member. */
    TextComponent Base;
} EmptyComponent;


// Base lifecycle (called by the factory, not end users).
/**
 * @brief Initializes the abstract base of a text component.
 *
 * Sets the vtable and type and stores the borrowed subcomponent buffer (which must be empty and have an
 * element size of sizeof(TextComponent*)). Call first from a concrete constructor, then fill concrete
 * fields. Release through the factory (which invokes the vtable Destroy and reclaims the buffer).
 * @param self The base to initialize (usually &concrete->Base); must not be NULL.
 * @param vtable The concrete type's behavior table; must not be NULL and must have a non-NULL Destroy.
 * @param type The concrete kind being constructed.
 * @param subComponentBuffer Borrowed buffer for child pointers; must not be NULL and its element size
 *        must equal sizeof(TextComponent*). Borrowed; not owned by the component.
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p vtable, its Destroy, or
 *          @p subComponentBuffer is NULL; ErrorCode_ArgumentOutOfRange if the buffer's element size is wrong.
 */
Error TextComponent_Construct(TextComponent* self,
    const TextComponentVTable* vtable,
    TextComponentType type,
    GenericBuffer* subComponentBuffer);

/**
 * @brief Clears the base's fields without freeing the borrowed subcomponent buffer.
 *
 * Drops the subcomponent references and resets the base. Does NOT return the buffer to the factory (the
 * factory does that around the vtable Destroy) and does NOT touch the referenced child components. Safe on NULL.
 * @param self The base to deconstruct, or NULL.
 */
void TextComponent_Deconstruct(TextComponent* self);


// Type / identity.
/**
 * @brief Returns the component's concrete kind.
 * @param self The component; must not be NULL.
 * @returns The type discriminator.
 */
static inline TextComponentType TextComponent_GetType(const TextComponent* self)
{
    return self->Type;
}


// Subcomponent management.
/**
 * @brief Appends a child to the end of the component's subcomponent list.
 *
 * Stores a borrowed pointer to @p child; @p child is not owned or copied. Rejected if it would create a
 * cycle: @p child must not be @p self, and @p self must not lie anywhere within @p child's subtree.
 * @param self The parent component; must not be NULL.
 * @param child The component to add; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p child is NULL; ErrorCode_InvalidOperation
 *          if the addition would create a cycle; ErrorCode_BufferTooLarge if the list could not grow.
 */
Error TextComponent_AddSubComponent(TextComponent* self, TextComponent* child);

/**
 * @brief Inserts a child at the given index, shifting later children right.
 *
 * Stores a borrowed pointer to @p child. Rejected if it would create a cycle (see
 * TextComponent_AddSubComponent).
 * @param self The parent component; must not be NULL.
 * @param index Insertion index in [0, count]; equal to the count appends at the end.
 * @param child The component to insert; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p child is NULL; ErrorCode_IndexOutOfBounds
 *          if @p index exceeds the count; ErrorCode_InvalidOperation on a cycle; ErrorCode_BufferTooLarge
 *          if the list could not grow.
 */
Error TextComponent_InsertSubComponent(TextComponent* self, size_t index, TextComponent* child);

/**
 * @brief Removes the child at the given index, shifting later children left.
 *
 * Drops only the reference; the child component itself is not returned to the factory or freed.
 * @param self The parent component; must not be NULL.
 * @param index Index of the child to remove; must be less than the count.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_IndexOutOfBounds if @p index
 *          is not less than the count.
 */
Error TextComponent_RemoveSubComponentAt(TextComponent* self, size_t index);

/**
 * @brief Removes the first child equal (by pointer identity) to @p child.
 *
 * Drops only the reference; the child component itself is not returned to the factory or freed.
 * @param self The parent component; must not be NULL.
 * @param child The child to remove; must not be NULL.
 * @returns Success if a matching child was removed; ErrorCode_IllegalArgument if @p self or @p child is
 *          NULL; ErrorCode_InvalidOperation if @p child is not a direct child of @p self.
 */
Error TextComponent_RemoveSubComponent(TextComponent* self, const TextComponent* child);

/**
 * @brief Removes all children, leaving the component with an empty subcomponent list.
 *
 * Drops only the references; the child components are not returned to the factory or freed.
 * @param self The parent component; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error TextComponent_ClearSubComponents(TextComponent* self);

/**
 * @brief Returns the number of direct children.
 * @param self The component; may be NULL.
 * @returns The child count, or 0 if @p self is NULL or has no list.
 */
size_t TextComponent_GetSubComponentCount(const TextComponent* self);

/**
 * @brief Returns the child at the given index.
 * @param self The component; must not be NULL.
 * @param index Index of the child; must be less than the count.
 * @returns The borrowed child pointer, or NULL if @p self is NULL or @p index is out of range.
 */
TextComponent* TextComponent_GetSubComponentAt(const TextComponent* self, size_t index);

/**
 * @brief Reports whether @p target is @p root or appears anywhere within @p root's subtree.
 *
 * Used for cycle detection; also useful to callers. A NULL argument yields false.
 * @param root The subtree root to search; may be NULL.
 * @param target The component to search for; may be NULL.
 * @returns true if @p target equals @p root or is a (transitive) child of @p root, false otherwise.
 */
bool TextComponent_IsInSubtree(const TextComponent* root, const TextComponent* target);


// Concrete constructors (called by the factory, not end users).
/**
 * @brief Constructs a string component in place with default styling and the given text.
 * @param self The string component storage to initialize; must not be NULL.
 * @param subComponentBuffer Borrowed subcomponent buffer (see TextComponent_Construct); must not be NULL.
 * @param text Borrowed NUL-terminated UTF-8 text, or NULL for no text; validated for UTF-8 encoding.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p subComponentBuffer is NULL;
 *          ErrorCode_InvalidTextEncoding if @p text is non-NULL and not valid UTF-8; otherwise a base error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error StringComponent_Construct(StringComponent* self, GenericBuffer* subComponentBuffer, const unsigned char* text);

/**
 * @brief Constructs a sprite component in place with default color/size and the given animation instance.
 * @param self The sprite component storage to initialize; must not be NULL.
 * @param subComponentBuffer Borrowed subcomponent buffer (see TextComponent_Construct); must not be NULL.
 * @param animationInstance Borrowed animation instance to render, or NULL for nothing.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p subComponentBuffer is NULL; otherwise a base error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error SpriteComponent_Construct(SpriteComponent* self, GenericBuffer* subComponentBuffer, SpriteAnimationInstance* animationInstance);

/**
 * @brief Constructs an empty component in place.
 * @param self The empty component storage to initialize; must not be NULL.
 * @param subComponentBuffer Borrowed subcomponent buffer (see TextComponent_Construct); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p subComponentBuffer is NULL; otherwise a base error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error EmptyComponent_Construct(EmptyComponent* self, GenericBuffer* subComponentBuffer);


// StringComponent accessors.
/**
 * @brief Returns the component's borrowed text (may be NULL).
 * @param self The string component; must not be NULL.
 * @returns The NUL-terminated UTF-8 text, or NULL if unset.
 */
static inline const unsigned char* StringComponent_GetText(const StringComponent* self)
{
    return self->_text;
}

/**
 * @brief Sets the component's text to a borrowed UTF-8 string (or NULL).
 *
 * The string is not copied; the caller retains ownership and must keep it alive while the component
 * references it. Only the UTF-8 encoding is validated (codepoint existence is not checked).
 * @param self The string component; must not be NULL.
 * @param text Borrowed NUL-terminated UTF-8 text, or NULL to clear.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidTextEncoding if
 *          @p text is non-NULL and not valid UTF-8.
 */
Error StringComponent_SetText(StringComponent* self, const unsigned char* text);

/**
 * @brief Returns the component's font.
 * @param self The string component; must not be NULL.
 * @returns The font.
 */
static inline GameFont StringComponent_GetFont(const StringComponent* self)
{
    return self->_font;
}

/**
 * @brief Sets the component's font.
 * @param self The string component; must not be NULL.
 * @param font The font to use.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error StringComponent_SetFont(StringComponent* self, GameFont font);

/**
 * @brief Returns the component's borrowed font asset reference name (may be NULL).
 * @param self The string component; must not be NULL.
 * @returns The NUL-terminated UTF-8 font name, or NULL if unset.
 */
static inline const unsigned char* StringComponent_GetFontName(const StringComponent* self)
{
    return self->_fontName;
}

/**
 * @brief Sets the component's font asset reference name to a borrowed UTF-8 string (or NULL).
 *
 * The string is not copied or validated; the caller retains ownership and must keep it alive while the
 * component references it. This only records the name for serialization/binding and does not change the
 * live @c _font (bind it with the TextComponentResolver).
 * @param self The string component; must not be NULL.
 * @param fontName Borrowed NUL-terminated UTF-8 font name, or NULL to clear.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
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
 * @param color The color; its Brightness and Opacity must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          Brightness or Opacity is not finite.
 */
Error StringComponent_SetColor(StringComponent* self, RenderColor color);

/**
 * @brief Returns the component's render size.
 * @param self The string component; must not be NULL.
 * @returns The size.
 */
static inline float StringComponent_GetSize(const StringComponent* self)
{
    return self->_size;
}

/**
 * @brief Sets the component's render size.
 * @param self The string component; must not be NULL.
 * @param size The size; must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p size is not finite.
 */
Error StringComponent_SetSize(StringComponent* self, float size);

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
 * @brief Returns the component's explicit shadow color (valid only when the type is Custom).
 * @param self The string component; must not be NULL.
 * @returns The stored custom shadow color.
 */
static inline RenderColor StringComponent_GetShadowColor(const StringComponent* self)
{
    return self->_shadowColor;
}

/**
 * @brief Sets an explicit custom shadow color and switches the shadow color type to Custom.
 * @param self The string component; must not be NULL.
 * @param color The shadow color; its Brightness and Opacity must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          Brightness or Opacity is not finite.
 */
Error StringComponent_SetShadowColorCustom(StringComponent* self, RenderColor color);

/**
 * @brief Switches the shadow color type back to Default (derived from the component color at render time).
 * @param self The string component; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error StringComponent_SetShadowColorDefault(StringComponent* self);

/**
 * @brief Returns the component's relative shadow offset.
 * @param self The string component; must not be NULL.
 * @returns The shadow offset (fraction of the component size).
 */
static inline Vector2 StringComponent_GetShadowOffset(const StringComponent* self)
{
    return self->_shadowOffset;
}

/**
 * @brief Sets the component's relative shadow offset.
 * @param self The string component; must not be NULL.
 * @param offset The offset (fraction of the component size); both components must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a
 *          component is not finite.
 */
Error StringComponent_SetShadowOffset(StringComponent* self, Vector2 offset);

/**
 * @brief Returns the component's character spacing (multiplier of the size).
 * @param self The string component; must not be NULL.
 * @returns The spacing.
 */
static inline float StringComponent_GetSpacing(const StringComponent* self)
{
    return self->_spacing;
}

/**
 * @brief Sets the component's character spacing (multiplier of the size).
 * @param self The string component; must not be NULL.
 * @param spacing The spacing; must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p spacing is not finite.
 */
Error StringComponent_SetSpacing(StringComponent* self, float spacing);


// SpriteComponent accessors.
/**
 * @brief Returns the component's borrowed animation instance (may be NULL).
 * @param self The sprite component; must not be NULL.
 * @returns The animation instance, or NULL.
 */
static inline SpriteAnimationInstance* SpriteComponent_GetAnimationInstance(const SpriteComponent* self)
{
    return self->_animationInstance;
}

/**
 * @brief Sets the component's borrowed animation instance (or NULL to render nothing).
 * @param self The sprite component; must not be NULL.
 * @param animationInstance Borrowed animation instance, or NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error SpriteComponent_SetAnimationInstance(SpriteComponent* self, SpriteAnimationInstance* animationInstance);

/**
 * @brief Returns the component's borrowed sprite-animation asset reference name (may be NULL).
 * @param self The sprite component; must not be NULL.
 * @returns The NUL-terminated UTF-8 animation name, or NULL if unset.
 */
static inline const unsigned char* SpriteComponent_GetAnimationName(const SpriteComponent* self)
{
    return self->_animationName;
}

/**
 * @brief Sets the component's sprite-animation asset reference name to a borrowed UTF-8 string (or NULL).
 *
 * The string is not copied or validated; the caller retains ownership and must keep it alive while the
 * component references it. This only records the name for serialization/binding and does not change the
 * live @c _animationInstance (bind it with the TextComponentResolver).
 * @param self The sprite component; must not be NULL.
 * @param animationName Borrowed NUL-terminated UTF-8 animation name, or NULL to clear.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error SpriteComponent_SetAnimationName(SpriteComponent* self, const unsigned char* animationName);

/**
 * @brief Returns the component's render color.
 * @param self The sprite component; must not be NULL.
 * @returns The color.
 */
static inline RenderColor SpriteComponent_GetColor(const SpriteComponent* self)
{
    return self->_color;
}

/**
 * @brief Sets the component's render color.
 * @param self The sprite component; must not be NULL.
 * @param color The color; its Brightness and Opacity must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if
 *          Brightness or Opacity is not finite.
 */
Error SpriteComponent_SetColor(SpriteComponent* self, RenderColor color);

/**
 * @brief Returns the component's render size.
 * @param self The sprite component; must not be NULL.
 * @returns The size.
 */
static inline Vector2 SpriteComponent_GetSize(const SpriteComponent* self)
{
    return self->_size;
}

/**
 * @brief Sets the component's render size.
 * @param self The sprite component; must not be NULL.
 * @param size The size; both components must be finite.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if a
 *          component is not finite.
 */
Error SpriteComponent_SetSize(SpriteComponent* self, Vector2 size);
