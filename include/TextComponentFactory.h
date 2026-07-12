#pragma once
#include <stdint.h>
#include <stdbool.h>
// Full component types (pool element sizes and accessors), the object/buffer pools held by value, and
// the number/codepoint helper argument types are all needed here.
#include "TextComponent.h"
#include "wr/WRObjectPool.h"
#include "wr/WRBufferPool.h"
#include "wr/WRNumber.h"
#include "wr/WRUnicode.h"
#include "wr/WRError.h"


/**
 * @file TextComponentFactory.h
 * @brief The one and only way to create, clone and release text components.
 *
 * The factory owns an object pool per component type (for the component structs) and a buffer pool for
 * the subcomponent list buffers, so building and tearing down components allocates little. It exposes
 * shorthand creators at varying levels of detail, helpers that turn numbers, codepoints and special
 * characters into string components, a deep clone, and the return operations that recycle a component's
 * pooled storage.
 *
 * OWNERSHIP. The factory owns only the component structs and their subcomponent buffers. It does NOT own
 * the strings, textures or sprite instances a component references; those stay owned by the caller and
 * must outlive the components that borrow them. A component may be referenced from many places, so the
 * caller decides when each component is returned. Every component obtained from the factory must
 * eventually be handed back with TextComponentFactory_ReturnComponent or
 * TextComponentFactory_ReturnComponentTree.
 *
 * GENERATED STRINGS. The number/codepoint helpers format their text into a caller-provided byte buffer
 * (element size 1) and the produced component borrows that text. The caller owns the buffer: it must
 * outlive the component and must not be reallocated or further mutated while the component references it
 * (use one buffer per generated string). The factory never owns these strings.
 *
 * Not thread-safe. Construct with TextComponentFactory_Construct and release with
 * TextComponentFactory_Deconstruct.
 */


// Types.
/**
 * @brief Pooled producer of text components. Underscore-prefixed fields are internal.
 *
 * Construct with TextComponentFactory_Construct and release with TextComponentFactory_Deconstruct.
 */
typedef struct TextComponentFactoryStruct
{
    /** @brief Pool of StringComponent structs. */
    ObjectPool _stringPool;
    /** @brief Pool of SpriteComponent structs. */
    ObjectPool _spritePool;
    /** @brief Pool of EmptyComponent structs. */
    ObjectPool _emptyPool;
    /** @brief Pool of reusable subcomponent list buffers (element size sizeof(TextComponent*)). */
    WRBufferPool _subComponentBuffers;
} TextComponentFactory;


// Lifecycle.
/**
 * @brief Initializes an empty factory with its component pools and subcomponent buffer pool.
 * @param self The factory to initialize; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_Construct(TextComponentFactory* self);

/**
 * @brief Releases the factory and all pooled storage.
 *
 * Deconstructs every component pool and the buffer pool. Any components still borrowed from the factory
 * have their storage released here; return components that reference owned resources first. Best-effort
 * teardown: the first error is returned and later errors are released so none leak. Safe on NULL.
 * @param self The factory to deconstruct, or NULL.
 * @returns Success (including the NULL case), or the first non-success Error encountered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_Deconstruct(TextComponentFactory* self);


// Creation.
/**
 * @brief Creates an empty component.
 * @param self The factory; must not be NULL.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateEmpty(TextComponentFactory* self, EmptyComponent** outComponent);

/**
 * @brief Creates a string component with default styling and the given text.
 * @param self The factory; must not be NULL.
 * @param text Borrowed NUL-terminated UTF-8 text, or NULL; not owned, must outlive the component.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL;
 *          ErrorCode_InvalidTextEncoding if @p text is non-NULL and not valid UTF-8; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateString(TextComponentFactory* self, const unsigned char* text, StringComponent** outComponent);

/**
 * @brief Creates a string component with the given text, font, color and size.
 * @param self The factory; must not be NULL.
 * @param text Borrowed NUL-terminated UTF-8 text, or NULL; not owned, must outlive the component.
 * @param font The font to render with.
 * @param color The text color; Brightness and Opacity must be finite.
 * @param size The render size; must be finite.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL;
 *          ErrorCode_InvalidTextEncoding if @p text is invalid UTF-8; ErrorCode_ArgumentOutOfRange if
 *          @p color or @p size is not finite; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateStringStyled(TextComponentFactory* self,
    const unsigned char* text,
    GameFont font,
    RenderColor color,
    float size,
    StringComponent** outComponent);

/**
 * @brief Creates a sprite component rendering the given animation instance (or nothing if NULL).
 * @param self The factory; must not be NULL.
 * @param animationInstance Borrowed animation instance, or NULL; not owned, must outlive the component.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateSprite(TextComponentFactory* self, SpriteAnimationInstance* animationInstance, SpriteComponent** outComponent);

/**
 * @brief Creates a sprite component with the given animation instance, color and size.
 * @param self The factory; must not be NULL.
 * @param animationInstance Borrowed animation instance, or NULL; not owned, must outlive the component.
 * @param color The render color; Brightness and Opacity must be finite.
 * @param size The render size; both components must be finite.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p color or @p size is not finite; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateSpriteSized(TextComponentFactory* self,
    SpriteAnimationInstance* animationInstance,
    RenderColor color,
    Vector2 size,
    SpriteComponent** outComponent);


// Special-character creators (backed by static string literals; no destination buffer needed).
/**
 * @brief Creates a string component containing a single space.
 * @param self The factory; must not be NULL.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateSpace(TextComponentFactory* self, StringComponent** outComponent);

/**
 * @brief Creates a string component containing a single tab.
 * @param self The factory; must not be NULL.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateTab(TextComponentFactory* self, StringComponent** outComponent);

/**
 * @brief Creates a string component containing a single newline.
 * @param self The factory; must not be NULL.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p outComponent is NULL; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateNewline(TextComponentFactory* self, StringComponent** outComponent);


// Number / codepoint creators (format into a caller-owned destination buffer, see the file comment).
/**
 * @brief Formats a signed 64-bit integer (base 10) into @p destinationString and wraps it in a component.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param destinationString [out] Caller-owned byte buffer (element size 1) the text is appended to and
 *        that the component borrows; must not be NULL and must outlive the component.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL or the buffer's element size
 *          is not 1; otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateInt64(TextComponentFactory* self, int64_t value, GenericBuffer* destinationString, StringComponent** outComponent);

/**
 * @brief Formats a signed 64-bit integer with an explicit base/prefix and styling into a component.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX] (NUMBER_BASE_AUTO_DETECT is invalid).
 * @param includePrefix When true, emits a "0x"/"0b" prefix for base 16/2.
 * @param font The font to render with.
 * @param color The text color; Brightness and Opacity must be finite.
 * @param size The render size; must be finite.
 * @param destinationString [out] Caller-owned byte buffer (element size 1); see TextComponentFactory_CreateInt64.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL, the buffer's element size is
 *          not 1, or @p base is invalid; ErrorCode_ArgumentOutOfRange if @p color/@p size is not finite;
 *          otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateInt64Styled(TextComponentFactory* self,
    int64_t value,
    int32_t base,
    bool includePrefix,
    GameFont font,
    RenderColor color,
    float size,
    GenericBuffer* destinationString,
    StringComponent** outComponent);

/**
 * @brief Formats an unsigned 64-bit integer (base 10) into @p destinationString and wraps it in a component.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param destinationString [out] Caller-owned byte buffer (element size 1); see TextComponentFactory_CreateInt64.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL or the buffer's element size
 *          is not 1; otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateUInt64(TextComponentFactory* self, uint64_t value, GenericBuffer* destinationString, StringComponent** outComponent);

/**
 * @brief Formats an unsigned 64-bit integer with an explicit base/prefix and styling into a component.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX] (NUMBER_BASE_AUTO_DETECT is invalid).
 * @param includePrefix When true, emits a "0x"/"0b" prefix for base 16/2.
 * @param font The font to render with.
 * @param color The text color; Brightness and Opacity must be finite.
 * @param size The render size; must be finite.
 * @param destinationString [out] Caller-owned byte buffer (element size 1); see TextComponentFactory_CreateInt64.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL, the buffer's element size is
 *          not 1, or @p base is invalid; ErrorCode_ArgumentOutOfRange if @p color/@p size is not finite;
 *          otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateUInt64Styled(TextComponentFactory* self,
    uint64_t value,
    int32_t base,
    bool includePrefix,
    GameFont font,
    RenderColor color,
    float size,
    GenericBuffer* destinationString,
    StringComponent** outComponent);

/**
 * @brief Formats a double (shortest round-trippable form) into @p destinationString and wraps it in a component.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param destinationString [out] Caller-owned byte buffer (element size 1); see TextComponentFactory_CreateInt64.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL or the buffer's element size
 *          is not 1; otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateDouble(TextComponentFactory* self, double value, GenericBuffer* destinationString, StringComponent** outComponent);

/**
 * @brief Formats a double with explicit format options and styling into a component.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param options The decimal format options (notation, digits, separator, case).
 * @param font The font to render with.
 * @param color The text color; Brightness and Opacity must be finite.
 * @param size The render size; must be finite.
 * @param destinationString [out] Caller-owned byte buffer (element size 1); see TextComponentFactory_CreateInt64.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL or the buffer's element size
 *          is not 1; ErrorCode_ArgumentOutOfRange if @p color/@p size is not finite; otherwise a
 *          formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateDoubleStyled(TextComponentFactory* self,
    double value,
    DecimalFormatOptions options,
    GameFont font,
    RenderColor color,
    float size,
    GenericBuffer* destinationString,
    StringComponent** outComponent);

/**
 * @brief Encodes a Unicode codepoint as UTF-8 into @p destinationString and wraps it in a component.
 * @param self The factory; must not be NULL.
 * @param codepoint The codepoint to encode; must be a valid Unicode scalar value.
 * @param destinationString [out] Caller-owned byte buffer (element size 1); see TextComponentFactory_CreateInt64.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL or the buffer's element size
 *          is not 1; ErrorCode_InvalidCodePoint if @p codepoint is not a valid Unicode scalar value;
 *          otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateCodepoint(TextComponentFactory* self, CodePoint codepoint, GenericBuffer* destinationString, StringComponent** outComponent);

/**
 * @brief Encodes a codepoint as UTF-8 with styling into a component.
 * @param self The factory; must not be NULL.
 * @param codepoint The codepoint to encode; must be a valid Unicode scalar value.
 * @param font The font to render with.
 * @param color The text color; Brightness and Opacity must be finite.
 * @param size The render size; must be finite.
 * @param destinationString [out] Caller-owned byte buffer (element size 1); see TextComponentFactory_CreateInt64.
 * @param outComponent [out] Receives the new component, or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a required pointer is NULL or the buffer's element size
 *          is not 1; ErrorCode_InvalidCodePoint if @p codepoint is invalid; ErrorCode_ArgumentOutOfRange
 *          if @p color/@p size is not finite; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CreateCodepointStyled(TextComponentFactory* self,
    CodePoint codepoint,
    GameFont font,
    RenderColor color,
    float size,
    GenericBuffer* destinationString,
    StringComponent** outComponent);


// Clone and return.
/**
 * @brief Deep-clones a component: a new component whose contents and whole subtree are duplicated.
 *
 * Child components are cloned recursively (deep). Non-factory-owned data referenced by the components
 * (text strings, sprite instances) is NOT cloned; the clones borrow the same pointers as the source, so
 * that data must outlive the clones too.
 * @param self The factory; must not be NULL.
 * @param source The component to clone; must not be NULL and must have been produced by this factory.
 * @param outComponent [out] Receives the cloned component (as a TextComponent*), or NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p source or @p outComponent is NULL; otherwise
 *          a pool error. On failure any partially-built clone is returned to the factory.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_CloneComponent(TextComponentFactory* self, const TextComponent* source, TextComponent** outComponent);

/**
 * @brief Returns a single component to the factory, recycling its struct and subcomponent buffer.
 *
 * Does NOT return the component's children (they are only referenced); return them separately. After this
 * call @p component must not be used again.
 * @param self The factory; must not be NULL.
 * @param component The component to return; must not be NULL and must have been produced by this factory.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p component is NULL; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_ReturnComponent(TextComponentFactory* self, TextComponent* component);

/**
 * @brief Returns a component and its whole subtree to the factory (children first, then the root).
 *
 * Use for a tree the caller exclusively owns. Do NOT use it when a child is shared with another parent
 * still in use, as that child would be returned while still referenced elsewhere. Best-effort: the first
 * error is returned and later errors are released so none leak; every reachable component is still
 * returned. After this call @p root and all its (transitive) children must not be used again.
 * @param self The factory; must not be NULL.
 * @param root The subtree root to return; must not be NULL and must have been produced by this factory.
 * @returns Success (including a fully-returned tree); ErrorCode_IllegalArgument if @p self or @p root is
 *          NULL; otherwise the first non-success Error encountered.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentFactory_ReturnComponentTree(TextComponentFactory* self, TextComponent* root);
