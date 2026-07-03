#pragma once
#include "WRMap.h"


// Types.
/**
 * @brief Caller-supplied function that computes the hash code of a key for a HashMap.
 *
 * Provided through HashMapConstructOptions and invoked by the map whenever it needs to locate the
 * bucket for a key (on get, add, remove, and contains-key). For any two keys the map's key
 * comparator treats as equal, this function MUST return the same HashCode; otherwise lookups may
 * fail to find stored entries. It should be a pure function of the key bytes (and any fixed context
 * carried in userData) and must not mutate the map.
 * @param map The owning map, as an IMap*. Useful for querying the key size via IMap_GetKeySize.
 *        Never NULL when called by the map.
 * @param key Pointer to the key to hash; reads exactly the map's key size bytes. Never NULL when
 *        called by the map.
 * @param userData Pointer to the KeyHashFunctionUserData copy stored in the map at construction.
 *        Read-only context; never NULL (points to an empty UserData if none was supplied).
 * @returns The 64-bit hash code of the key.
 */
typedef HashCode (*HashMapKeyHashFunction)(IMap* map, const void* key, const UserData* userData);

/**
 * @brief An open-addressing hash map that stores keys and values BY VALUE in a single contiguous
 *        block and is driven by caller-supplied hash and comparison callbacks.
 *
 * Keys and values are fixed-size (set at construction); each insertion copies the caller's key and
 * value bytes into the map's own storage. The map implements the IMap interface (see HashMap_AsMap)
 * and exposes entry, key, and value collections through that interface. Construct an instance with
 * HashMap_Construct1 and release it with HashMap_Deconstruct.
 *
 * Pointer stability: HashMap_GetPointerToElement (via the IMap interface) returns a pointer directly
 * into the map's storage block. Any operation that rehashes the table - growth on insertion, or a
 * rebuild to clear accumulated tombstones - reallocates that block and invalidates ALL previously
 * returned element pointers. Treat such pointers as valid only until the next add or remove.
 * @note Not thread-safe; external synchronization is required for concurrent access.
 */
typedef struct HashMapStruct
{
    /** Embedded IMap interface; obtain a pointer to it with HashMap_AsMap. */
    IMap _map;
    /** Callback used to hash keys; never NULL after successful construction. */
    HashMapKeyHashFunction _keyHashFunction;
    /** Stored copy of the user context passed to the key hash function on every call. */
    UserData _keyHashFunctionUserData;
    /** Callback used to test two keys for equality; defaults to a byte-wise comparison if none was supplied. */
    MapKeyComparator _keyComparator;
    /** Stored copy of the user context passed to the key comparator on every call. */
    UserData _keyComparatorUserData;
    /** Callback used to test two values for equality (used by contains-value); defaults to a byte-wise comparison if none was supplied. */
    MapValueComparator _valueComparator;
    /** Stored copy of the user context passed to the value comparator on every call. */
    UserData _valueComparatorUserData;
    /** Backing storage for the single contiguous block holding bucket metadata, then keys, then values. Empty until the first allocation. */
    GenericBuffer _dataBuffer;
    /** True when the map allocated and owns the active storage block and must free it on deconstruction. */
    bool _isActiveBufferOwned;
    /** Number of live key/value entries currently stored (excludes tombstones); this is the value reported by IMap_GetEntryCount. */
    size_t _entryCount;
    /** Number of buckets currently marked as deleted (tombstones); these still occupy slots until the table is rebuilt. */
    size_t _tombstoneCount;
    /** Total number of buckets in the current storage block (always a power of two, or 0 when no storage is allocated). */
    size_t _capacity;
} HashMap;

/**
 * @brief Parameters that configure a HashMap when passed to HashMap_Construct1.
 *
 * Populate this directly or start from HashMapConstructOptions_CreateDefault and override fields.
 * KeySize, ValueSize, and KeyHashFunction are required; the remaining fields may be left zero/NULL
 * to accept defaults.
 */
typedef struct HashMapConstructOptionsStruct
{
    /** Size in bytes of each key; must be greater than zero. Every key copied into the map is read/stored at this exact size. */
    size_t KeySize;
    /** Size in bytes of each value; must be greater than zero. Every value copied into the map is read/stored at this exact size. */
    size_t ValueSize;
    /** Required hash callback for keys; must not be NULL. Must agree with KeyComparator (equal keys hash equally). */
    HashMapKeyHashFunction KeyHashFunction;
    /** Context value copied into the map and handed to KeyHashFunction on every call. Leave empty if unused. */
    UserData KeyHashFunctionUserData;
    /** Optional key equality callback; if NULL the map uses a default byte-wise key comparison. */
    MapKeyComparator KeyComparator;
    /** Context value copied into the map and handed to KeyComparator on every call. Leave empty if unused. */
    UserData KeyComparatorUserData;
    /** Optional value equality callback (used by contains-value); if NULL the map uses a default byte-wise value comparison. */
    MapValueComparator ValueComparator;
    /** Context value copied into the map and handed to ValueComparator on every call. Leave empty if unused. */
    UserData ValueComparatorUserData;
    /** Hint for the number of buckets to pre-allocate. 0 defers allocation until the first insertion; non-zero values are rounded up to a power of two with a fixed minimum applied. */
    size_t InitialCapacity;
} HashMapConstructOptions;


// Functions.
/**
 * @brief Builds a HashMapConstructOptions populated with the required fields and sensible defaults.
 *
 * Sets KeySize, ValueSize, and KeyHashFunction from the arguments; clears all UserData fields to
 * empty; leaves KeyComparator and ValueComparator NULL (so the map selects its default byte-wise
 * comparators); and sets InitialCapacity to 0 (allocation deferred to the first insertion). Override
 * any field on the returned value before passing it to HashMap_Construct1.
 * @param keySize Size in bytes of each key; should be greater than zero (validated by HashMap_Construct1).
 * @param valueSize Size in bytes of each value; should be greater than zero (validated by HashMap_Construct1).
 * @param keyHashFunction Hash callback for keys; should not be NULL (validated by HashMap_Construct1).
 * @returns A fully initialized options value ready to be customized and/or passed to HashMap_Construct1.
 */
static inline HashMapConstructOptions HashMapConstructOptions_CreateDefault(size_t keySize, 
    size_t valueSize,
    HashMapKeyHashFunction keyHashFunction)
{
    return (HashMapConstructOptions) {
        .KeySize = keySize,
        .ValueSize = valueSize,
        .KeyHashFunction = keyHashFunction,
        .KeyHashFunctionUserData = UserData_CreateEmpty(),
        .KeyComparator = NULL,
        .KeyComparatorUserData = UserData_CreateEmpty(),
        .ValueComparator = NULL,
        .ValueComparatorUserData = UserData_CreateEmpty(),
        .InitialCapacity = 0,
    };
}

/**
 * @brief Returns a pointer to the map's embedded IMap interface.
 *
 * Use the returned interface to perform all map operations (get, get-pointer, add, remove, clear,
 * contains-key, contains-value, entry/key/value enumeration) via the IMap_* functions. The pointer
 * refers to storage inside self and is valid for as long as self is alive and not deconstructed; it
 * does not transfer ownership.
 * @param self The hash map. Must not be NULL and should be successfully constructed.
 * @returns A non-owning pointer to self's IMap interface.
 */
static inline IMap* HashMap_AsMap(HashMap* self)
{
    return &self->_map;
}

/**
 * @brief Constructs a hash map in place from the given options, leaving it ready for use.
 *
 * Initializes the map's state and IMap interface, records the key/value sizes, hash function, and
 * comparators (substituting default byte-wise comparators when KeyComparator or ValueComparator is
 * NULL), and stores copies of the supplied UserData values. If options.InitialCapacity is greater
 * than zero, the backing storage is allocated immediately (capacity rounded up to a power of two
 * with a fixed minimum); otherwise allocation is deferred until the first insertion. The resulting
 * map is empty regardless of any pre-allocated capacity. The object must later be released with
 * HashMap_Deconstruct.
 * @param self Pointer to uninitialized HashMap storage to construct. Must not be NULL.
 * @param options Configuration for the map (passed by value). KeySize and ValueSize must each be
 *        greater than zero and KeyHashFunction must not be NULL.
 * @returns Success on construction. Raises ErrorCode_IllegalArgument if self is NULL, if KeySize or
 *          ValueSize is zero, or if KeyHashFunction is NULL; raises ErrorCode_BufferTooLarge if a
 *          requested non-zero InitialCapacity cannot be represented or allocated.
 */
Error HashMap_Construct1(HashMap* self, HashMapConstructOptions options);

/**
 * @brief Releases a hash map's backing storage and resets it to an empty, unconfigured state.
 *
 * Frees the storage block if the map owns it, then reinitializes the structure (zero entries, zero
 * capacity, default comparators, empty buffer). Because keys and values are stored by value and the
 * map owns no key/value pointers, only the single storage block is freed; any objects referenced by
 * stored keys or values are the caller's responsibility. Safe to call on an already-deconstructed or
 * freshly default-state map (it simply re-clears it).
 * @param self The hash map to deconstruct. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if self is NULL.
 */
Error HashMap_Deconstruct(HashMap* self);
