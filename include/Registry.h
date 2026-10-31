#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "NamespacedKey.h"
#include "wr/WRError.h"
#include "wr/WRHashMap.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


/**
 * @file Registry.h
 * @brief Registries: namespaced keys mapped to entries of one kind, and the root registry that finds every registry by
 *        its key.
 *
 * A registry holds the named registrations of one kind (input actions and their categories; commands, events and
 * script modules once they exist): entries registered by engine code, by an asset type's loader from its
 * definitions, and by scripts, each under a namespaced key of the program's KeyRegistry. A registry is not a resource
 * manager: assets stay in the asset manager (lazy loading, users, dependencies, overrides by search root), and the
 * world component types, the UI widget types and GameKey keep their own machinery.
 *
 * STABLE INDICES. Every entry gets an index (0, 1, 2, ... in registration order) that it keeps for the registry's life:
 * slots are never removed. Withdrawing an entry (its source unloads) deconstructs its element and hides it from finds
 * and lists; registering the same key again revives the same slot and index. So an index is a safe handle to keep (a
 * binding, a cached lookup), and the engine's own entries can be indices 0 to N-1 in their enum's order. Indices exist
 * only at runtime: files hold key texts.
 *
 * NO SILENT REPLACEMENT. Registering a key that is active fails, naming the key and both sources. Packs override one
 * another's definitions in the asset manager's search roots, before anything registers.
 *
 * SOURCES. Every entry records who registered it, as a key id: NAMESPACED_KEY_ID_NONE for the engine, a pack's or a
 * story's key otherwise. A source's entries withdraw together (Registry_WithdrawSource).
 *
 * ELEMENTS. A registry stores elements of one fixed size by value, in blocks that never move, so an active element
 * keeps its address until it is withdrawn. Registering moves an element in (from then on the registry owns what the
 * element owns), and the optional element deconstructor releases it on withdrawal and at teardown. C has no generics:
 * each user wraps the functions it needs in small inline functions over its element type, so its callers never cast.
 *
 * THE ROOT. A RegistryRoot is a registry of borrowed Registry pointers under the key REGISTRY_ROOT_KEY. main owns the
 * program's root and Services.Registries carries it: systems add their registries to it once at startup and withdraw
 * them before they deconstruct them, and tools (the terminal, autocomplete for "a key of registry X", the editor's
 * pickers, a debug dump) find any registry through it. The root never owns a registry and does not list itself.
 * Systems keep direct pointers to their own registries and never look them up per tick.
 *
 * Main thread only, like the key registry.
 */


// Macros.
/** @brief The index that is never an entry's ("no entry"). */
#define REGISTRY_INDEX_NONE ((uint32_t)UINT32_MAX)
/** @brief The most entries a registry can be constructed to hold: one per key a key registry can hold. */
#define REGISTRY_MAX_ENTRY_COUNT ((uint32_t)KEY_REGISTRY_MAX_KEY_COUNT)
/** @brief The root registry's own key. */
#define REGISTRY_ROOT_KEY ((const unsigned char*)u8"tbp:registry")
/** @brief The most registries a root holds. */
#define REGISTRY_ROOT_MAX_ENTRY_COUNT ((uint32_t)256)


// Types.
/**
 * @brief Releases what one element owns (its strings, its buffers).
 *
 * Called once for every registered element: when it is withdrawn, or when the registry is deconstructed while it is
 * active. The registry zeroes the element's bytes afterwards. It must not call back into the registry.
 * @param element The element, in the registry's storage. Never NULL.
 * @param userData The context given at construction (the registry's stored copy). Never NULL.
 */
typedef void (*RegistryElementDeconstructor)(void* element, const UserData* userData);

/**
 * @brief How to construct a registry (Registry_Construct).
 *
 * Start from RegistryConstructOptions_CreateDefault and set the deconstructor when elements own memory.
 */
typedef struct RegistryConstructOptionsStruct
{
    /** @brief The key registry the registry borrows: every entry's key and source is one of its ids. Must outlive the
     *  registry. */
    KeyRegistry* Keys;
    /** @brief The registry's own key: an id of Keys (the root lists the registry under it). */
    uint32_t KeyId;
    /** @brief Bytes of one element: the sizeof of the element type, so every element is aligned. At least 1. */
    size_t ElementSize;
    /** @brief The most entries, withdrawn ones included (slots are never removed): 1 to REGISTRY_MAX_ENTRY_COUNT.
     *  Content from other players cannot grow a registry past it. */
    uint32_t MaxEntryCount;
    /** @brief Releases what an element owns; NULL when elements own nothing. */
    RegistryElementDeconstructor DeconstructElement;
    /** @brief Context copied into the registry and handed to DeconstructElement. */
    UserData DeconstructElementUserData;
} RegistryConstructOptions;

/**
 * @brief Entries of one kind under namespaced keys, with stable indices.
 *
 * Construct in place with Registry_Construct and release with Registry_Deconstruct. Underscore-prefixed fields are
 * internal: read the registry through its functions.
 */
typedef struct RegistryStruct
{
    /** @brief The borrowed key registry. */
    KeyRegistry* _keys;
    /** @brief The registry's own key id. */
    uint32_t _keyId;
    /** @brief The most slots. */
    uint32_t _maxEntryCount;
    /** @brief How many entries are active. */
    uint32_t _activeCount;
    /** @brief Bytes of one element. */
    size_t _elementSize;
    /** @brief Advances on every registration and withdrawal. */
    uint64_t _revision;
    /** @brief Releases what an element owns, or NULL. */
    RegistryElementDeconstructor _deconstructElement;
    /** @brief The context handed to _deconstructElement. */
    UserData _deconstructElementUserData;
    /** @brief Owned buffer of the slots' records (key, source, active), one per index. */
    GenericBuffer _slots;
    /** @brief Owned buffer of `unsigned char*` element blocks; blocks never move, so active elements keep their
     *  address. */
    GenericBuffer _blocks;
    /** @brief Key id (uint32_t) -> index (uint32_t) of every slot, active or withdrawn. */
    HashMap _indexByKey;
} Registry;

/**
 * @brief The registry of registries: borrowed Registry pointers under their own keys, for tools that work on every
 *        kind.
 *
 * Construct in place with RegistryRoot_Construct and release with RegistryRoot_Deconstruct, after every system that
 * added a registry to it has withdrawn it. Underscore-prefixed fields are internal.
 */
typedef struct RegistryRootStruct
{
    /** @brief The registry whose elements are borrowed `Registry*` pointers, under REGISTRY_ROOT_KEY. */
    Registry _registry;
} RegistryRoot;


// Functions: the registry.
/**
 * @brief Returns construction options for a registry whose elements own nothing (no deconstructor).
 * @param keys The key registry to borrow.
 * @param keyId The registry's own key (an id of @p keys).
 * @param elementSize Bytes of one element (sizeof the element type).
 * @param maxEntryCount The most entries, withdrawn ones included.
 * @returns The options; set DeconstructElement (and its user data) when elements own memory.
 */
static inline RegistryConstructOptions RegistryConstructOptions_CreateDefault(KeyRegistry* keys, uint32_t keyId,
    size_t elementSize, uint32_t maxEntryCount)
{
    return (RegistryConstructOptions){
        .Keys = keys,
        .KeyId = keyId,
        .ElementSize = elementSize,
        .MaxEntryCount = maxEntryCount,
        .DeconstructElement = NULL,
        .DeconstructElementUserData = UserData_CreateEmpty(),
    };
}

/**
 * @brief Constructs an empty registry in place. Pair with Registry_Deconstruct.
 * @param self The registry; must not be NULL.
 * @param options How to construct it; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument or options->Keys is NULL;
 *          ErrorCode_ArgumentOutOfRange if options->KeyId is not a key of options->Keys, options->ElementSize is 0 or
 *          too large for a block, or options->MaxEntryCount is 0 or above REGISTRY_MAX_ENTRY_COUNT.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Registry_Construct(Registry* self, const RegistryConstructOptions* options);

/**
 * @brief Deconstructs every active element and releases the registry's storage: every index and element pointer it
 *        handed out becomes invalid.
 * @param self The registry; NULL does nothing.
 */
void Registry_Deconstruct(Registry* self);

/**
 * @brief Registers an element under a key, or revives the key's withdrawn slot.
 *
 * The element's bytes are copied in, which moves it: on success the registry owns what the element owns (the caller
 * must neither deconstruct nor use its copy's owned memory); on failure the caller still owns it and nothing changed.
 * A new key takes the next index. A key that was withdrawn revives its own slot and index with the new element and
 * source, so an index someone kept means the same entry again. A key that is active refuses: a registry never replaces
 * an entry. Advances the revision.
 * @param self The registry; must not be NULL.
 * @param keyId The entry's key: an id of the registry's key registry.
 * @param element ElementSize bytes to move in; must not be NULL.
 * @param sourceKeyId Who registers it: NAMESPACED_KEY_ID_NONE for the engine, otherwise an id of the key registry (a
 *        pack's or a story's key).
 * @param outIndex [out] Receives the entry's index; may be NULL. Unchanged on failure.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p element is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p keyId or @p sourceKeyId is not a key of the key registry; ErrorCode_InvalidOperation if @p keyId is
 *          registered and active (the message names the key and both sources) or the registry holds its maximum of
 *          entries and @p keyId is new.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Registry_Register(Registry* self, uint32_t keyId, const void* element, uint32_t sourceKeyId, uint32_t* outIndex);

/**
 * @brief Withdraws one entry: deconstructs its element and hides it from finds and lists, keeping its slot, key and
 *        index for a later registration of the same key. Advances the revision.
 * @param self The registry; must not be NULL.
 * @param index The entry's index.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_IndexOutOfBounds if @p index is not an
 *          index of the registry; ErrorCode_InvalidOperation if the entry is withdrawn already.
 */
Error Registry_Withdraw(Registry* self, uint32_t index);

/**
 * @brief Withdraws every active entry a source registered (a pack or a story unloading), as Registry_Withdraw does
 *        each; other sources' entries and every index stay as they are.
 * @param self The registry; NULL does nothing.
 * @param sourceKeyId The source: NAMESPACED_KEY_ID_NONE for the engine's entries.
 * @returns How many entries were withdrawn.
 */
uint32_t Registry_WithdrawSource(Registry* self, uint32_t sourceKeyId);

/**
 * @brief Finds the active entry of a key. Constant time on average.
 * @param self The registry; must not be NULL.
 * @param keyId The key id.
 * @param outIndex [out] Receives the index when found; must not be NULL.
 * @returns true if an active entry has the key (false for NULL arguments, unknown keys and withdrawn entries).
 */
bool Registry_Find(Registry* self, uint32_t keyId, uint32_t* outIndex);

/**
 * @brief Finds the active entry of a key's full text, without interning it: a text the key registry does not hold is
 *        not found.
 * @param self The registry; must not be NULL.
 * @param key The full key ("namespace:path", NUL-terminated); any text is accepted (an invalid one is not found). A
 *        key as people write it, perhaps without its namespace, goes through NamespacedKey_Parse first.
 * @param outIndex [out] Receives the index when found; must not be NULL.
 * @returns true if an active entry has the key.
 */
bool Registry_FindText(Registry* self, const unsigned char* key, uint32_t* outIndex);

/**
 * @brief Appends the indices of the active entries whose key text starts with @p prefix, in byte order of their
 *        text.
 *
 * For autocomplete: "mymod:" lists a mod's entries, the empty prefix every active entry. A linear scan over the
 * registry's own entries (not the whole key registry). The appended range is sorted in place, so the buffer's earlier
 * contents are untouched.
 * @param self The registry; must not be NULL.
 * @param prefix The prefix (NUL-terminated); must not be NULL.
 * @param outIndices A buffer of uint32_t to append to (not cleared); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL or @p outIndices does not hold uint32_t;
 *          ErrorCode_BufferTooSmall if @p outIndices could not grow (nothing appended then).
 */
Error Registry_FindByPrefix(const Registry* self, const unsigned char* prefix, GenericBuffer* outIndices);

/**
 * @brief Reports whether an index's entry is registered now.
 * @param self The registry; must not be NULL.
 * @param index The index.
 * @returns true for an active entry; false for a withdrawn one and an index the registry does not have.
 */
bool Registry_IsActive(const Registry* self, uint32_t index);

/**
 * @brief Returns an index's key id, also while the entry is withdrawn (the key that revives it).
 * @param self The registry; must not be NULL.
 * @param index The index.
 * @returns The key id; NAMESPACED_KEY_ID_NONE for an index the registry does not have.
 */
uint32_t Registry_GetKey(const Registry* self, uint32_t index);

/**
 * @brief Returns an index's full key text, also while the entry is withdrawn.
 * @param self The registry; must not be NULL.
 * @param index The index.
 * @returns The key (owned by the key registry, valid while the key is interned); NULL for an index the registry does
 *          not have.
 */
const unsigned char* Registry_GetKeyText(const Registry* self, uint32_t index);

/**
 * @brief Returns who registered an index's entry last (also while it is withdrawn).
 * @param self The registry; must not be NULL.
 * @param index The index; check it with Registry_GetCount first, as the engine's source is NAMESPACED_KEY_ID_NONE too.
 * @returns The source's key id: NAMESPACED_KEY_ID_NONE for the engine and for an index the registry does not have.
 */
uint32_t Registry_GetSource(const Registry* self, uint32_t index);

/**
 * @brief Returns an active entry's element.
 * @param self The registry; must not be NULL.
 * @param index The index.
 * @returns The element (owned by the registry; it keeps its address until the entry is withdrawn or the registry is
 *          deconstructed); NULL while the entry is withdrawn and for an index the registry does not have.
 */
const void* Registry_Get(const Registry* self, uint32_t index);

/**
 * @brief Returns how many slots the registry has, withdrawn ones included: the indices are 0 to this count - 1.
 * @param self The registry; must not be NULL.
 * @returns The slot count.
 */
static inline uint32_t Registry_GetCount(const Registry* self)
{
    return (uint32_t)self->_slots._count;
}

/**
 * @brief Returns how many entries are active.
 * @param self The registry; must not be NULL.
 * @returns The active entry count.
 */
static inline uint32_t Registry_GetActiveCount(const Registry* self)
{
    return self->_activeCount;
}

/**
 * @brief Returns the most entries the registry holds, withdrawn ones included.
 * @param self The registry; must not be NULL.
 * @returns The maximum entry count.
 */
static inline uint32_t Registry_GetMaxEntryCount(const Registry* self)
{
    return self->_maxEntryCount;
}

/**
 * @brief Returns the registry's revision: it advances on every registration and withdrawal (never on reads), so a
 *        consumer that caches data derived from the entries rebuilds it when the revision differs.
 * @param self The registry; must not be NULL.
 * @returns The revision.
 */
static inline uint64_t Registry_GetRevision(const Registry* self)
{
    return self->_revision;
}

/**
 * @brief Returns the registry's own key id (the root lists the registry under it).
 * @param self The registry; must not be NULL.
 * @returns The key id.
 */
static inline uint32_t Registry_GetRegistryKey(const Registry* self)
{
    return self->_keyId;
}


// Functions: the root.
/**
 * @brief Constructs an empty root in place, interning REGISTRY_ROOT_KEY as its own key. Pair with
 *        RegistryRoot_Deconstruct.
 * @param self The root; must not be NULL.
 * @param keys The key registry to borrow; every registry added must borrow the same one. Must outlive the root.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RegistryRoot_Construct(RegistryRoot* self, KeyRegistry* keys);

/**
 * @brief Releases the root's storage. It never owns a registry, so the registries it lists are not touched; every
 *        system should have withdrawn its registries before.
 * @param self The root; NULL does nothing.
 */
void RegistryRoot_Deconstruct(RegistryRoot* self);

/**
 * @brief Adds a registry to the root under the registry's own key, as the engine's.
 *
 * The root borrows the pointer: withdraw it (RegistryRoot_Withdraw) before the registry is deconstructed. A registry
 * withdrawn before is added again in its old slot.
 * @param self The root; must not be NULL.
 * @param registry The registry; must not be NULL and must borrow the root's key registry.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL or the registry borrows another key registry;
 *          ErrorCode_InvalidOperation if a registry under the same key is in the root, or the root holds
 *          REGISTRY_ROOT_MAX_ENTRY_COUNT registries.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RegistryRoot_Add(RegistryRoot* self, Registry* registry);

/**
 * @brief Withdraws a registry from the root (before the registry is deconstructed).
 * @param self The root; must not be NULL.
 * @param registry The registry; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_InvalidOperation if the root does not
 *          list this registry under its key.
 */
Error RegistryRoot_Withdraw(RegistryRoot* self, const Registry* registry);

/**
 * @brief Finds a registry by its key.
 * @param self The root; must not be NULL.
 * @param keyId The registry's key id.
 * @returns The registry (borrowed), or NULL when the root lists none under the key.
 */
Registry* RegistryRoot_Find(RegistryRoot* self, uint32_t keyId);

/**
 * @brief Finds a registry by its key's full text, without interning it.
 * @param self The root; must not be NULL.
 * @param key The full key (NUL-terminated); any text is accepted (an invalid one is not found).
 * @returns The registry (borrowed), or NULL when the root lists none under the key.
 */
Registry* RegistryRoot_FindText(RegistryRoot* self, const unsigned char* key);

/**
 * @brief Returns the registry at one of the root's indices (0 to RegistryRoot_GetCount - 1).
 * @param self The root; must not be NULL.
 * @param index The index.
 * @returns The registry (borrowed), or NULL while it is withdrawn and for an index the root does not have.
 */
Registry* RegistryRoot_Get(const RegistryRoot* self, uint32_t index);

/**
 * @brief Returns how many slots the root has, withdrawn registries included.
 * @param self The root; must not be NULL.
 * @returns The slot count.
 */
static inline uint32_t RegistryRoot_GetCount(const RegistryRoot* self)
{
    return Registry_GetCount(&self->_registry);
}

/**
 * @brief Returns the root as a registry of registry pointers, for the generic reads (prefix search over the
 *        registries' keys, revision, key texts). Change it only through the RegistryRoot functions.
 * @param self The root; must not be NULL.
 * @returns The root's registry.
 */
static inline const Registry* RegistryRoot_AsRegistry(const RegistryRoot* self)
{
    return &self->_registry;
}
