#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
// Full component types (pool element sizes), the object/buffer pools held by value, and the number/codepoint helper
// argument types are all needed here.
#include "TextComponent.h"
#include "wr/WRObjectPool.h"
#include "wr/WRBufferPool.h"
#include "wr/WRNumber.h"
#include "wr/WRUnicode.h"
#include "wr/WRError.h"


/**
 * @file TextComponentFactory.h
 * @brief The only way to create, clone and return text components.
 *
 * The factory owns an object pool per component type and one buffer pool for everything the components own: their
 * strings and their child lists. Building and tearing down components therefore allocates little once the pools
 * are warm. It offers creators for every type, shorthands that turn numbers, codepoints and special characters into
 * string components, a deep clone, and the return operation.
 *
 * OWNERSHIP. Every created component is a root owned by the caller until it is added to a parent (which then owns
 * it), handed to a TextDocument (which then owns the tree), or returned with TextComponentFactory_ReturnTree.
 * Components copy every string they are given, so callers never keep strings alive for them.
 *
 * The factory must outlive every component it created; TextComponentFactory_Deconstruct releases all pooled storage,
 * including that of components still alive (TextComponentFactory_GetLiveComponentCount reports them). Not
 * thread-safe. Construct with TextComponentFactory_Construct and release with TextComponentFactory_Deconstruct.
 */


// Types.
/**
 * @brief Pooled producer of text components. All fields are internal.
 */
typedef struct TextComponentFactoryStruct
{
    /** @brief Pool of EmptyComponent slots. */
    ObjectPool _emptyPool;
    /** @brief Pool of StringComponent slots. */
    ObjectPool _stringPool;
    /** @brief Pool of SpriteComponent slots. */
    ObjectPool _spritePool;
    /** @brief Pool of TranslateComponent slots. */
    ObjectPool _translatePool;
    /** @brief Buffers the components own: strings (element size 1) and child lists (element size sizeof(TextComponent*)). */
    WRBufferPool _buffers;
    /** @brief Reusable byte buffer the number shorthands format into before the text is copied into the component. */
    GenericBuffer _formatScratch;
    /** @brief The last revision number handed out; revisions are unique across the factory. */
    uint64_t _revisionCounter;
    /** @brief Number of components created and not yet returned. */
    size_t _liveComponentCount;
} TextComponentFactory;


// Lifecycle.
/**
 * @brief Initializes an empty factory (no storage is allocated until the first component is created).
 * @param self The factory to initialize; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_Construct(TextComponentFactory* self);

/**
 * @brief Releases the factory and all pooled storage, including that of components still alive (they must not be
 *        used afterwards). Best-effort: the first error is returned and later ones are released. Safe on NULL.
 * @param self The factory to deconstruct, or NULL.
 * @returns Success (including the NULL case), or the first error met while tearing down.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_Deconstruct(TextComponentFactory* self);

/**
 * @brief Returns the number of components created by this factory and not yet returned (a leak check for tests).
 * @param self The factory; must not be NULL.
 * @returns The live component count.
 */
static inline size_t TextComponentFactory_GetLiveComponentCount(const TextComponentFactory* self)
{
    return self->_liveComponentCount;
}


// Creation. Every created component is a root owned by the caller (see the file comment).
/**
 * @brief Creates an empty component.
 * @param self The factory; must not be NULL.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateEmpty(TextComponentFactory* self, EmptyComponent** outComponent);

/**
 * @brief Creates a string component with default styling and a copy of @p text.
 * @param self The factory; must not be NULL.
 * @param text NUL-terminated UTF-8 text, or NULL for an empty text. Copied.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidTextEncoding if @p text is not
 *          valid UTF-8; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateString(TextComponentFactory* self, const unsigned char* text, StringComponent** outComponent);

/**
 * @brief Creates a sprite component referring to an animation by name (default color, size (0, 0)).
 * @param self The factory; must not be NULL.
 * @param animationName A sprite-animation asset name, or NULL for a sprite that draws nothing. Copied.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p animationName is not a valid asset name; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateSprite(TextComponentFactory* self, const unsigned char* animationName,
    SpriteComponent** outComponent);

/**
 * @brief Creates a translate component with default styling.
 * @param self The factory; must not be NULL.
 * @param key The translation key (TextComponent_IsValidTranslationKey); must not be NULL. Copied.
 * @param fallback The text shown while the key has no translation, or NULL to show the key. Copied.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer argument other than @p fallback is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p key is not a valid key; ErrorCode_InvalidTextEncoding if @p fallback
 *          is not valid UTF-8; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateTranslate(TextComponentFactory* self, const unsigned char* key,
    const unsigned char* fallback, TranslateComponent** outComponent);


// Shorthands for special characters, numbers and codepoints (string components with default styling).
/**
 * @brief Creates a string component containing a single space.
 * @param self The factory; must not be NULL.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns As TextComponentFactory_CreateString.
 */
Error TextComponentFactory_CreateSpace(TextComponentFactory* self, StringComponent** outComponent);

/**
 * @brief Creates a string component containing a single tab (laid out as a fixed-width gap, see TextLayout.h).
 * @param self The factory; must not be NULL.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns As TextComponentFactory_CreateString.
 */
Error TextComponentFactory_CreateTab(TextComponentFactory* self, StringComponent** outComponent);

/**
 * @brief Creates a string component containing a single newline.
 * @param self The factory; must not be NULL.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns As TextComponentFactory_CreateString.
 */
Error TextComponentFactory_CreateNewline(TextComponentFactory* self, StringComponent** outComponent);

/**
 * @brief Creates a string component holding a signed 64-bit integer in base 10.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateInt64(TextComponentFactory* self, int64_t value, StringComponent** outComponent);

/**
 * @brief Creates a string component holding a signed 64-bit integer in the given base.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX] (NUMBER_BASE_AUTO_DETECT is invalid).
 * @param includePrefix When true, emits a "0x"/"0b" prefix for base 16/2.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL or @p base is invalid; otherwise a formatting or
 *          pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateInt64Formatted(TextComponentFactory* self, int64_t value, int32_t base,
    bool includePrefix, StringComponent** outComponent);

/**
 * @brief Creates a string component holding an unsigned 64-bit integer in base 10.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateUInt64(TextComponentFactory* self, uint64_t value, StringComponent** outComponent);

/**
 * @brief Creates a string component holding an unsigned 64-bit integer in the given base.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX] (NUMBER_BASE_AUTO_DETECT is invalid).
 * @param includePrefix When true, emits a "0x"/"0b" prefix for base 16/2.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL or @p base is invalid; otherwise a formatting or
 *          pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateUInt64Formatted(TextComponentFactory* self, uint64_t value, int32_t base,
    bool includePrefix, StringComponent** outComponent);

/**
 * @brief Creates a string component holding a double in its shortest round-trippable form.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateDouble(TextComponentFactory* self, double value, StringComponent** outComponent);

/**
 * @brief Creates a string component holding a double formatted with explicit options.
 * @param self The factory; must not be NULL.
 * @param value The value to format.
 * @param options The decimal format options (notation, digits, separator, case).
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise a formatting or pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateDoubleFormatted(TextComponentFactory* self, double value, DecimalFormatOptions options,
    StringComponent** outComponent);

/**
 * @brief Creates a string component holding one Unicode codepoint.
 * @param self The factory; must not be NULL.
 * @param codepoint A valid Unicode scalar value other than U+0000 (text cannot hold NUL).
 * @param outComponent [out] Receives the new component, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidCodePoint if @p codepoint is
 *          not a valid Unicode scalar value or is U+0000; otherwise a pool error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CreateCodepoint(TextComponentFactory* self, CodePoint codepoint, StringComponent** outComponent);


// Clone and return.
/**
 * @brief Deep-copies a component and its whole subtree into new components of this factory.
 *
 * Every property and string is copied; the source is unchanged and may come from another factory. The clone is a
 * root owned by the caller.
 * @param self The factory that creates the clone; must not be NULL.
 * @param source The component to copy; must not be NULL.
 * @param outRoot [out] Receives the cloned root, or NULL on failure; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; otherwise a pool error (a partial clone is
 *          returned before failing).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_CloneTree(TextComponentFactory* self, const TextComponent* source, TextComponent** outRoot);

/**
 * @brief Returns a root component and its whole subtree to the factory.
 *
 * Every component's strings and child lists go back to the buffer pool and the slots to their object pools. After
 * the call no component of the tree may be used. Refused for a component that still has a parent (detach it first,
 * TextComponent_Detach, or destroy it through the parent, TextComponent_DestroySubComponentAt) and for the tree of a
 * text document (the document returns it; TextDocument_TakeRoot takes it out).
 * @param self The factory that created the tree; must not be NULL.
 * @param root The root to return; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL or @p root comes from another factory;
 *          ErrorCode_InvalidOperation if @p root has a parent or is held by a document; otherwise the first error met
 *          while returning (the whole tree is still returned).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextComponentFactory_ReturnTree(TextComponentFactory* self, TextComponent* root);
