#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRHashMap.h"
#include "wr/WRMemory.h"


/**
 * @file NamespacedKey.h
 * @brief Namespaced keys ("namespace:path", like Minecraft's identifiers) and the registry that interns them.
 *
 * A namespaced key names something across story packs without collisions: `town:street_lamp`, `game:office`. The
 * namespace is `[a-z0-9_.-]+`, the path `[a-z0-9_./-]+`, and the whole key, colon included, is at most
 * NAMESPACED_KEY_MAX_LENGTH bytes. Uppercase letters, spaces and every other character are refused, so a key always
 * has exactly one spelling. Tags and object data use keys today; asset names across packs, climate presets, actions
 * and events will use them later.
 *
 * DEFAULT NAMESPACE. A key written without a namespace (`office`) gets its context's default namespace: the story or
 * pack id once packs exist, NAMESPACED_KEY_DEFAULT_NAMESPACE (`game`) until then. NamespacedKey_Parse applies it.
 * Keys are stored, compared and written always in the full form.
 *
 * INTERNING. A KeyRegistry maps key text to a small integer id and back, so code compares keys as integers and an
 * object stores 4 bytes per key. There is one registry per program: main creates it, Services carries it, and every
 * World borrows it. Ids exist only at runtime; files always hold the text. Id 0 (NAMESPACED_KEY_ID_NONE) is never a
 * key. A registry only grows (up to KEY_REGISTRY_MAX_KEY_COUNT keys), and the text of a key stays at the same address
 * as long as the key is interned, except that a read which fails forgets the keys it interned
 * (KeyRegistry_TruncateTo), so hostile files that are refused leave nothing behind.
 *
 * Not thread-safe.
 */


// Macros.
/** @brief The most bytes a namespaced key may have, colon included, without the terminator. */
#define NAMESPACED_KEY_MAX_LENGTH ((size_t)128)
/** @brief Bytes of a buffer that holds any namespaced key and its terminator. */
#define NAMESPACED_KEY_BUFFER_SIZE (NAMESPACED_KEY_MAX_LENGTH + 1U)
/** @brief The byte between the namespace and the path. */
#define NAMESPACED_KEY_SEPARATOR ((unsigned char)':')
/** @brief The namespace of keys written without one, until story packs give each pack its own. */
#define NAMESPACED_KEY_DEFAULT_NAMESPACE ((const unsigned char*)u8"tbp")
/** @brief The id that is never a key ("no key"). */
#define NAMESPACED_KEY_ID_NONE ((uint32_t)0)
/** @brief The most keys one registry holds; interning more fails (content from other players cannot grow it without
 *         bound). */
#define KEY_REGISTRY_MAX_KEY_COUNT ((size_t)1 << 20)


// Types.
/**
 * @brief Where an interned key's text is stored: a text block of the registry and an offset in it (internal).
 */
typedef struct KeyRegistryLocationStruct
{
    /** @brief The index of the block in KeyRegistry._blocks. */
    uint32_t Block;
    /** @brief The byte offset of the text in the block. */
    uint32_t Offset;
} KeyRegistryLocation;

/**
 * @brief The interned keys of a program: key text <-> uint32 id.
 *
 * Construct in place with KeyRegistry_Construct and release with KeyRegistry_Deconstruct. Ids run from 1 to
 * KeyRegistry_GetCount in interning order. Underscore-prefixed fields are internal.
 */
typedef struct KeyRegistryStruct
{
    /** @brief Owned buffer of KeyRegistryLocation: entry id - 1 is where that key's text is (a block and an offset). */
    GenericBuffer _locations;
    /** @brief Owned buffer of uint32_t: entry id - 1 is the next id whose text has the same hash, or 0. */
    GenericBuffer _nextWithSameHash;
    /** @brief Owned buffer of `unsigned char*` text blocks; blocks never move, so key texts keep their address. */
    GenericBuffer _blocks;
    /** @brief Bytes used in the last block. */
    size_t _lastBlockUsed;
    /** @brief HashCode of a key's text -> uint32_t id of the most recently interned key with that hash. */
    HashMap _firstIdByHash;
} KeyRegistry;


// Functions: keys.
/**
 * @brief Reports whether text is a valid full key: "namespace:path" with the allowed characters, at most
 *        NAMESPACED_KEY_MAX_LENGTH bytes.
 * @param key The NUL-terminated text; NULL is not a key.
 * @returns true if @p key is a valid full key.
 */
bool NamespacedKey_IsValid(const unsigned char* key);

/**
 * @brief Checks that text is a valid full key, explaining what is wrong otherwise.
 * @param key The NUL-terminated text.
 * @returns Success; ErrorCode_IllegalArgument if @p key is NULL; ErrorCode_ArgumentOutOfRange naming the problem (an
 *          empty namespace or path, a character that is not allowed, uppercase, no colon, too long).
 */
Error NamespacedKey_Validate(const unsigned char* key);

/**
 * @brief Reports whether text is a valid namespace (`[a-z0-9_.-]+`, short enough to leave room for a path).
 * @param text The namespace's bytes; need not be NUL-terminated. May be NULL only when @p byteCount is 0.
 * @param byteCount The number of bytes.
 * @returns true if the bytes are a valid namespace.
 */
bool NamespacedKey_IsNamespaceValid(const unsigned char* text, size_t byteCount);

/**
 * @brief Reads a key as people write it, with or without a namespace, and appends its full form.
 *
 * "town:lamp" stays as it is; "lamp" becomes "<defaultNamespace>:lamp". The text must use the key characters only
 * (no spaces, no uppercase). The destination is one growing string: a trailing NUL terminator is dropped before
 * appending, and the result is NUL-terminated. A buffer of NAMESPACED_KEY_BUFFER_SIZE bytes always suffices (on the
 * stack through GenericBuffer_CreateConstant). Nothing is appended on failure.
 * @param text The key's bytes; need not be NUL-terminated. May be NULL only when @p byteCount is 0.
 * @param byteCount The number of bytes.
 * @param defaultNamespace The namespace given to a key without one; NULL means NAMESPACED_KEY_DEFAULT_NAMESPACE.
 * @param outKey The byte buffer to append the full key to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p outKey is NULL or @p defaultNamespace is not a valid namespace;
 *          ErrorCode_ArgumentOutOfRange naming the problem with the key (as NamespacedKey_Validate);
 *          ErrorCode_BufferTooSmall if @p outKey cannot hold the key.
 */
Error NamespacedKey_Parse(const unsigned char* text, size_t byteCount, const unsigned char* defaultNamespace,
    GenericBuffer* outKey);

/**
 * @brief Returns the length of a full key's namespace: the index of its colon.
 * @param key A valid full key.
 * @returns The namespace's byte count; 0 for NULL or a text without a colon.
 */
size_t NamespacedKey_GetNamespaceLength(const unsigned char* key);

/**
 * @brief Returns a full key's path: the text after its colon (points into @p key).
 * @param key A valid full key.
 * @returns The path; NULL for NULL or a text without a colon.
 */
const unsigned char* NamespacedKey_GetPath(const unsigned char* key);


// Functions: the registry.
/**
 * @brief Constructs an empty registry in place. Pair with KeyRegistry_Deconstruct.
 * @param self The registry; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error KeyRegistry_Construct(KeyRegistry* self);

/**
 * @brief Releases a registry's storage: every key text it handed out becomes invalid.
 * @param self The registry; NULL does nothing.
 */
void KeyRegistry_Deconstruct(KeyRegistry* self);

/**
 * @brief Returns the id of a full key, interning it first if it is new.
 *
 * Interning the same text again returns the same id. Lookups hash the text, so this is constant time on average.
 * @param self The registry; must not be NULL.
 * @param key The full key (NUL-terminated); it must be valid (see NamespacedKey_Validate). It is copied.
 * @param outId [out] Receives the id (at least 1). Must not be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_ArgumentOutOfRange for an invalid
 *          key (the message names the problem); ErrorCode_InvalidOperation when the registry already holds
 *          KEY_REGISTRY_MAX_KEY_COUNT keys.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error KeyRegistry_Intern(KeyRegistry* self, const unsigned char* key, uint32_t* outId);

/**
 * @brief Reads a key as people write it (see NamespacedKey_Parse) and interns its full form.
 * @param self The registry; must not be NULL.
 * @param text The key's bytes; need not be NUL-terminated. May be NULL only when @p byteCount is 0.
 * @param byteCount The number of bytes.
 * @param defaultNamespace The namespace of a key written without one; NULL means NAMESPACED_KEY_DEFAULT_NAMESPACE.
 * @param outId [out] Receives the id. Must not be NULL. Unchanged on failure.
 * @returns Success; the errors of NamespacedKey_Parse and KeyRegistry_Intern.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error KeyRegistry_InternText(KeyRegistry* self, const unsigned char* text, size_t byteCount,
    const unsigned char* defaultNamespace, uint32_t* outId);

/**
 * @brief Finds the id of a full key without interning it.
 *
 * A key that was never interned cannot be on any object, so a search for it can stop early.
 * @param self The registry; must not be NULL.
 * @param key The full key (NUL-terminated); any text is accepted (an invalid one is simply not found).
 * @param outId [out] Receives the id when found. Must not be NULL.
 * @returns true if the key is interned.
 */
bool KeyRegistry_TryFind(KeyRegistry* self, const unsigned char* key, uint32_t* outId);

/**
 * @brief Returns the text of an id.
 * @param self The registry; must not be NULL.
 * @param id The id.
 * @returns The full key (NUL-terminated, owned by the registry, valid as long as the key is interned: until the
 *          registry is deconstructed, or truncated below @p id), or NULL when @p id is not an id of this registry (0
 *          included).
 */
const unsigned char* KeyRegistry_GetText(const KeyRegistry* self, uint32_t id);

/**
 * @brief Returns how many keys the registry holds; the ids are 1 to this count.
 * @param self The registry; must not be NULL.
 * @returns The key count.
 */
static inline size_t KeyRegistry_GetCount(const KeyRegistry* self)
{
    return self->_locations._count;
}

/**
 * @brief Forgets every key interned after the registry held @p count keys: the readers' undo when a read fails.
 *
 * Take KeyRegistry_GetCount before a read and truncate to it when the read fails, so a refused file leaves no keys
 * behind (content from other players cannot fill the registry by being refused again and again). The forgotten ids
 * will be handed out again, so this is only safe when nothing holds an id or a text of those keys any more: a failed
 * read has released everything it built. The key texts' storage is reclaimed.
 * @param self The registry; NULL does nothing.
 * @param count The number of keys to keep; at least the current count does nothing.
 */
void KeyRegistry_TruncateTo(KeyRegistry* self, size_t count);

/**
 * @brief Appends the ids of every key whose text starts with @p prefix, in alphabetical (byte) order of their text.
 *
 * For autocomplete: "town:" lists the keys of the town namespace, "to" also finds "tools:hammer". A linear scan over
 * the keys, fast enough for every keystroke with thousands of keys. The empty prefix lists every key.
 * @param self The registry; must not be NULL.
 * @param prefix The prefix (NUL-terminated); must not be NULL.
 * @param outIds A buffer of uint32_t to append to (not cleared); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL or @p outIds does not hold uint32_t;
 *          ErrorCode_BufferTooSmall if @p outIds could not grow.
 */
Error KeyRegistry_FindByPrefix(const KeyRegistry* self, const unsigned char* prefix, GenericBuffer* outIds);
