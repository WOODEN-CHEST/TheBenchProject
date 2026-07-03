#pragma once
#include <stddef.h>
#include <stdint.h>
#include "WRHash.h"
#include "WRMemory.h"
#include "WRCollection.h"
#include "WRError.h"
#include "WRCompile.h"
#include "WRUserData.h"


// Types.
/** @brief Opaque handle to a map interface instance; see the IMapStruct definition below. */
typedef struct IMapStruct IMap;

/**
 * @brief Bit flags describing properties of a map.
 *
 * Combined as a bitmask in IMap._flags and inspected via IMap_GetFlags / IMap_IsReadOnly.
 */
typedef enum MapFlagsEnum
{
    /** @brief No flags set; the map is mutable with no special properties. */
    MapFlags_None = 0,
    /**
     * @brief The map is read-only; mutating operations are rejected.
     *
     * When set, Add, Remove, and Clear fail with ErrorCode_InvalidOperation instead of modifying the map.
     */
    MapFlags_IsReadOnly = (1 << 0),
} MapFlags;

/**
 * @brief Predicate that decides whether two keys are equal for a given map.
 *
 * Used by implementations to test key equality. A default (MapKeyComparator_Default) compares the raw
 * key bytes. Implementations must return @c true when the two keys are considered equal.
 * @param map The map whose key semantics apply (e.g. for querying the key size); non-NULL.
 * @param key1 Pointer to the first key; non-NULL.
 * @param key2 Pointer to the second key; non-NULL.
 * @param userData Caller-attached context for the comparison; may be unused.
 * @returns @c true if the keys are equal, otherwise @c false.
 */
typedef bool (*MapKeyComparator)(IMap* map, const void* key1, const void* key2, const UserData* userData);

/**
 * @brief Predicate that decides whether two values are equal for a given map.
 *
 * Used (e.g. by ContainsValue) to test value equality. A default (MapValueComparator_Default) compares
 * the raw value bytes. Implementations must return @c true when the two values are considered equal.
 * @param map The map whose value semantics apply (e.g. for querying the value size); non-NULL.
 * @param value1 Pointer to the first value; non-NULL.
 * @param value2 Pointer to the second value; non-NULL.
 * @param userData Caller-attached context for the comparison; may be unused.
 * @returns @c true if the values are equal, otherwise @c false.
 */
typedef bool (*MapValueComparator)(IMap* map, const void* value1, const void* value2, const UserData* userData);

/**
 * @brief A non-owning view of a single map entry as a (key, value) pointer pair.
 *
 * The pointers alias storage owned by the map; they are valid only while that entry exists and the map
 * is not mutated. Used when iterating a map's entries.
 */
typedef struct MapEntryViewStruct
{
    /** @brief Pointer to the entry's key, aliasing map-owned storage; not owned by the view. */
    void* _key;
    /** @brief Pointer to the entry's value, aliasing map-owned storage; not owned by the view. */
    void* _value;
} MapEntryView;

/**
 * @brief Function-pointer table defining the abstract contract for an IMap.
 *
 * Every concrete map supplies these slots. @c Self is passed back as the receiver to each slot. The
 * public IMap_* wrappers validate arguments (and enforce read-only/null rules) before dispatching here,
 * so slots may assume non-NULL pointers and a writable map where applicable. All conforming
 * implementations must satisfy the per-slot contracts below.
 */
typedef struct MapVTableStruct
{
    /** @brief Opaque receiver passed as the @c self argument to every slot in this vtable. */
    void* Self;
    /**
     * @brief Look up a key and copy its associated value out by value.
     *
     * On a hit, copies the value (value-size bytes) into @p outValue. If the key is absent, must report
     * a not-found condition via the returned Error and must not write a value.
     * @param self The map receiver (the vtable's @c Self).
     * @param key Pointer to the lookup key; non-NULL.
     * @param outValue [out] Destination for a copy of the found value; at least value-size bytes.
     * @returns Success when the value was copied; otherwise an Error, including the case where the key
     *          is not present.
     */
    Error (*_getElement)(void* self, const void* key, void* outValue);
    /**
     * @brief Look up a key and return a pointer to its value within the map's storage.
     *
     * On a hit, writes into @p outValuePointer a pointer aliasing the stored value; the pointer is valid
     * only while the entry exists and the map is not mutated. If the key is absent, must report a
     * not-found condition via the returned Error.
     * @param self The map receiver (the vtable's @c Self).
     * @param key Pointer to the lookup key; non-NULL.
     * @param outValuePointer [out] Receives a pointer aliasing the stored value.
     * @returns Success when a pointer was produced; otherwise an Error, including key-not-present.
     */
    Error (*_getPointerToElement)(void* self, const void* key, void** outValuePointer);
    /**
     * @brief Insert or update the entry for a key.
     *
     * Associates @p value with @p key, copying both into the map's storage. Implementations define
     * whether an existing key is overwritten or left unchanged, and report which occurred via
     * @p outWasAdded (@c true when a new entry was created). Only invoked on a writable map.
     * @param self The map receiver (the vtable's @c Self).
     * @param key Pointer to the key to insert; non-NULL.
     * @param value Pointer to the value to associate; non-NULL.
     * @param outWasAdded [out] Receives @c true if a new entry was added, @c false otherwise.
     * @returns Success when the operation completed; otherwise an Error.
     */
    Error (*_add)(void* self, const void* key, const void* value, bool* outWasAdded);
    /**
     * @brief Remove the entry for a key if present.
     *
     * Deletes the entry associated with @p key. Writes @c true to @p outWasRemoved if an entry was
     * removed, or @c false if the key was absent (which is not an error). Only invoked on a writable map.
     * @param self The map receiver (the vtable's @c Self).
     * @param key Pointer to the key to remove; non-NULL.
     * @param outWasRemoved [out] Receives whether an entry was actually removed.
     * @returns Success when the operation completed; otherwise an Error.
     */
    Error (*_remove)(void* self, const void* key, bool* outWasRemoved);
    /**
     * @brief Remove all entries, leaving the map empty.
     *
     * After success the entry count is zero. Only invoked on a writable map.
     * @param self The map receiver (the vtable's @c Self).
     * @returns Success when the map was cleared; otherwise an Error.
     */
    Error (*_clear)(void* self);
    /**
     * @brief Report whether a key is present in the map.
     *
     * @param self The map receiver (the vtable's @c Self).
     * @param key Pointer to the key to test; non-NULL.
     * @param outContainsKey [out] Receives @c true if the key exists, otherwise @c false.
     * @returns Success when membership was determined; otherwise an Error.
     */
    Error (*_containsKey)(void* self, const void* key, bool* outContainsKey);
    /**
     * @brief Report whether at least one entry holds the given value.
     *
     * Tests value membership using the map's value-equality semantics (which may require scanning entries).
     * @param self The map receiver (the vtable's @c Self).
     * @param value Pointer to the value to test; non-NULL.
     * @param outContainsValue [out] Receives @c true if some entry has this value, otherwise @c false.
     * @returns Success when membership was determined; otherwise an Error.
     */
    Error (*_containsValue)(void* self, const void* value, bool* outContainsValue);
    /**
     * @brief Return the number of entries currently stored in the map.
     *
     * Must not mutate the map.
     * @param self The map receiver (the vtable's @c Self).
     * @returns The current entry count.
     */
    size_t (*_getEntryCount)(void* self);
    /**
     * @brief Release all resources owned by the map instance.
     *
     * After this call the map must not be used again. Implementations should make this safe to invoke
     * once on a constructed map.
     * @param self The map receiver (the vtable's @c Self).
     * @returns Success when teardown completed; otherwise an Error.
     */
    Error (*_deconstruct)(void* self);
} MapVTable;

/**
 * @brief Abstract associative container mapping fixed-size keys to fixed-size values.
 *
 * An IMap stores copies of keys (each @c _keySize bytes) and values (each @c _valueSize bytes) and is
 * driven through its vtable. It also exposes three read-only ICollection views of its contents: the
 * entries (as MapEntryView), the keys, and the values. Callers should generally use the IMap_* wrapper
 * functions rather than touching these fields directly.
 */
typedef struct IMapStruct
{
    /** @brief Size in bytes of each stored value. */
    size_t _valueSize;
    /** @brief Size in bytes of each stored key. */
    size_t _keySize;
    /** @brief Dispatch table implementing this map's behavior. */
    MapVTable _vtable;
    /** @brief Read-only collection view over the map's entries, yielding MapEntryView elements. */
    ICollection _entryCollection;
    /** @brief Read-only collection view over the map's keys. */
    ICollection _keyCollection;
    /** @brief Read-only collection view over the map's values. */
    ICollection _valueCollection;
    /** @brief Property flags for this map (see MapFlags), e.g. the read-only bit. */
    MapFlags _flags;
} IMap;


// Functions.
/**
 * @brief Return the size in bytes of each key stored in the map.
 * @param map The map to query; must be non-NULL.
 * @returns The configured key size in bytes.
 */
static inline size_t IMap_GetKeySize(IMap* map);

/**
 * @brief Return the size in bytes of each value stored in the map.
 * @param map The map to query; must be non-NULL.
 * @returns The configured value size in bytes.
 */
static inline size_t IMap_GetValueSize(IMap* map);

/**
 * @brief Default key-equality predicate that compares the raw key bytes.
 *
 * Conforms to MapKeyComparator. Returns whether the first IMap_GetKeySize(map) bytes of the two keys
 * are identical; @p userData is ignored.
 * @param map The map supplying the key size; must be non-NULL.
 * @param key1 Pointer to the first key; non-NULL with at least key-size bytes.
 * @param key2 Pointer to the second key; non-NULL with at least key-size bytes.
 * @param userData Unused.
 * @returns @c true if the key bytes are equal, otherwise @c false.
 */
static inline bool MapKeyComparator_Default(IMap* map, const void* key1, const void* key2, const UserData* userData)
{
    UNUSED(userData);
    return Memory_IsEqual(key1, key2, IMap_GetKeySize(map));
}

/**
 * @brief Default value-equality predicate that compares the raw value bytes.
 *
 * Conforms to MapValueComparator. Returns whether the first IMap_GetValueSize(map) bytes of the two
 * values are identical; @p userData is ignored.
 * @param map The map supplying the value size; must be non-NULL.
 * @param value1 Pointer to the first value; non-NULL with at least value-size bytes.
 * @param value2 Pointer to the second value; non-NULL with at least value-size bytes.
 * @param userData Unused.
 * @returns @c true if the value bytes are equal, otherwise @c false.
 */
static inline bool MapValueComparator_Default(IMap* map, const void* value1, const void* value2, const UserData* userData)
{
    UNUSED(userData);
    return Memory_IsEqual(value1, value2, IMap_GetValueSize(map));
}

/**
 * @brief Return the size in bytes of each key stored in the map.
 * @param map The map to query; must be non-NULL.
 * @returns The configured key size in bytes.
 */
static inline size_t IMap_GetKeySize(IMap* map)
{
    return map->_keySize;
}

/**
 * @brief Return the size in bytes of each value stored in the map.
 * @param map The map to query; must be non-NULL.
 * @returns The configured value size in bytes.
 */
static inline size_t IMap_GetValueSize(IMap* map)
{
    return map->_valueSize;
}

/**
 * @brief Obtain a read-only collection view over the map's entries.
 *
 * The returned ICollection enumerates the map's entries as MapEntryView elements. It aliases storage
 * inside @p map and remains valid only as long as the map does; do not free it separately.
 * @param map The map to view; must be non-NULL.
 * @returns A pointer to the map's entry collection view.
 */
static inline ICollection* IMap_AsEntryCollection(IMap* map)
{
    return &map->_entryCollection;
}

/**
 * @brief Obtain a read-only collection view over the map's keys.
 *
 * The returned ICollection enumerates the map's keys. It aliases storage inside @p map and remains
 * valid only as long as the map does; do not free it separately.
 * @param map The map to view; must be non-NULL.
 * @returns A pointer to the map's key collection view.
 */
static inline ICollection* IMap_AsKeyCollection(IMap* map)
{
    return &map->_keyCollection;
}

/**
 * @brief Obtain a read-only collection view over the map's values.
 *
 * The returned ICollection enumerates the map's values. It aliases storage inside @p map and remains
 * valid only as long as the map does; do not free it separately.
 * @param map The map to view; must be non-NULL.
 * @returns A pointer to the map's value collection view.
 */
static inline ICollection* IMap_AsValueCollection(IMap* map)
{
    return &map->_valueCollection;
}

/**
 * @brief Return the number of entries currently stored in the map.
 * @param map The map to query; must be non-NULL.
 * @returns The current entry count.
 */
static inline size_t IMap_GetEntryCount(IMap* map)
{
    return map->_vtable._getEntryCount(map->_vtable.Self);
}

/**
 * @brief Return the property flags set on the map.
 * @param map The map to query; must be non-NULL.
 * @returns The map's MapFlags bitmask.
 */
static inline MapFlags IMap_GetFlags(IMap* map)
{
    return map->_flags;
}

/**
 * @brief Report whether the map is read-only.
 *
 * Tests the MapFlags_IsReadOnly bit. When true, Add, Remove, and Clear are rejected.
 * @param map The map to query; must be non-NULL.
 * @returns @c true if the map is read-only, otherwise @c false.
 */
static inline bool IMap_IsReadOnly(IMap* map)
{
    return (IMap_GetFlags(map) & MapFlags_IsReadOnly) != 0;
}

/**
 * @brief Look up a key and copy its associated value out by value.
 *
 * Validates arguments, then dispatches to the map implementation. On success copies value-size bytes of
 * the found value into @p outValue. If the key is absent, the implementation reports a not-found Error
 * and @p outValue is not written.
 * @param self The map to query; must be non-NULL.
 * @param key Pointer to the lookup key; must be non-NULL.
 * @param outValue [out] Destination for a copy of the value; must be non-NULL and at least value-size bytes.
 * @returns Success when the value was copied. Raises ErrorCode_IllegalArgument if @p self, @p key, or
 *          @p outValue is NULL; otherwise an Error from the implementation, including the key-not-present case.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IMap_GetElement(IMap* self, const void* key, void* outValue);

/**
 * @brief Look up a key and return a pointer to its value within the map's storage.
 *
 * Validates arguments, then dispatches to the map implementation. On success writes into @p outValue a
 * pointer aliasing the stored value, valid only while the entry exists and the map is not mutated. If
 * the key is absent, the implementation reports a not-found Error.
 * @param self The map to query; must be non-NULL.
 * @param key Pointer to the lookup key; must be non-NULL.
 * @param outValue [out] Receives a pointer aliasing the stored value; the @c void** itself must be non-NULL.
 * @returns Success when a pointer was produced. Raises ErrorCode_IllegalArgument if @p self, @p key, or
 *          @p outValue is NULL; otherwise an Error from the implementation, including the key-not-present case.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IMap_GetPointerToElement(IMap* self, const void* key, void** outValue);

/**
 * @brief Insert or update the entry for a key, reporting whether a new entry was created.
 *
 * Validates arguments and rejects read-only maps, then dispatches to the implementation, which copies
 * the key and value into the map. Whether an existing key is overwritten or left unchanged is defined
 * by the implementation; @p outWasAdded distinguishes a fresh insertion (@c true) from an existing-key
 * case (@c false).
 * @param self The map to modify; must be non-NULL and not read-only.
 * @param key Pointer to the key to insert; must be non-NULL.
 * @param value Pointer to the value to associate; must be non-NULL.
 * @param outWasAdded [out] Receives @c true if a new entry was added; must be non-NULL.
 * @returns Success when the operation completed. Raises ErrorCode_IllegalArgument if @p self, @p key,
 *          @p value, or @p outWasAdded is NULL; ErrorCode_InvalidOperation if the map is read-only;
 *          otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IMap_Add(IMap* self, const void* key, const void* value, bool* outWasAdded);

/**
 * @brief Remove the entry for a key if present, reporting whether one was removed.
 *
 * Validates arguments and rejects read-only maps, then dispatches to the implementation. A missing key
 * is not an error: @p outWasRemoved receives @c false in that case and @c true when an entry was deleted.
 * @param self The map to modify; must be non-NULL and not read-only.
 * @param key Pointer to the key to remove; must be non-NULL.
 * @param outWasRemoved [out] Receives whether an entry was actually removed; must be non-NULL.
 * @returns Success when the operation completed. Raises ErrorCode_IllegalArgument if @p self, @p key, or
 *          @p outWasRemoved is NULL; ErrorCode_InvalidOperation if the map is read-only; otherwise an
 *          Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IMap_Remove(IMap* self, const void* key, bool* outWasRemoved);

/**
 * @brief Remove all entries from the map, leaving it empty.
 *
 * Rejects read-only maps, then dispatches to the implementation. After success the entry count is zero.
 * @param self The map to clear; must be non-NULL and not read-only.
 * @returns Success when the map was cleared. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_InvalidOperation if the map is read-only; otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IMap_Clear(IMap* self);

/**
 * @brief Report whether a key is present in the map.
 *
 * Validates arguments, then dispatches to the implementation.
 * @param self The map to query; must be non-NULL.
 * @param key Pointer to the key to test; must be non-NULL.
 * @param outContainsKey [out] Receives @c true if the key exists, otherwise @c false; must be non-NULL.
 * @returns Success when membership was determined. Raises ErrorCode_IllegalArgument if @p self, @p key,
 *          or @p outContainsKey is NULL; otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IMap_ContainsKey(IMap* self, const void* key, bool* outContainsKey);

/**
 * @brief Report whether at least one entry in the map holds the given value.
 *
 * Validates arguments, then dispatches to the implementation, which tests value membership using the
 * map's value-equality semantics (this may require scanning entries).
 * @param self The map to query; must be non-NULL.
 * @param value Pointer to the value to test; must be non-NULL.
 * @param outContainsValue [out] Receives @c true if some entry has this value, otherwise @c false; must be non-NULL.
 * @returns Success when membership was determined. Raises ErrorCode_IllegalArgument if @p self, @p value,
 *          or @p outContainsValue is NULL; otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IMap_ContainsValue(IMap* self, const void* value, bool* outContainsValue);

/**
 * @brief Release all resources owned by the map.
 *
 * Validates @p self, then dispatches to the implementation's teardown. After this call the map must not
 * be used again.
 * @param self The map to deconstruct; must be non-NULL.
 * @returns Success when teardown completed. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IMap_Deconstruct(IMap* self);
