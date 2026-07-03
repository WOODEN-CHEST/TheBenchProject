#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WRError.h"
#include "WRHash.h"
#include "WRMemory.h"
#include "WRSet.h"
#include "WRUserData.h"


// Types.
/**
 * @brief Caller-supplied function that computes the hash code of an element for a HashSet.
 *
 * Provided through HashSetConstructOptions and invoked by the set whenever it needs to locate the
 * bucket for an element (on add, remove, and contains). For any two elements the set's element
 * comparator treats as equal, this function MUST return the same HashCode; otherwise lookups may
 * fail to find stored elements. It should be a pure function of the element bytes (and any fixed
 * context carried in userData) and must not mutate the set.
 * @param set The owning set, as an ISet*. Useful for querying the element size via
 *        ISet_GetElementSize. Never NULL when called by the set.
 * @param element Pointer to the element to hash; reads exactly the set's element size bytes. Never
 *        NULL when called by the set.
 * @param userData Pointer to the ElementHashFunctionUserData copy stored in the set at
 *        construction. Read-only context; never NULL (points to an empty UserData if none was supplied).
 * @returns The 64-bit hash code of the element.
 */
typedef HashCode (*HashSetElementHashFunction)(ISet* set, const void* element, const UserData* userData);

/**
 * @brief An open-addressing hash set that stores distinct fixed-size elements BY VALUE in a single
 *        contiguous block and is driven by caller-supplied hash and comparison callbacks.
 *
 * Elements are fixed-size (set at construction); each insertion copies the caller's element bytes
 * into the set's own storage, and no two stored elements compare equal under the element
 * comparator. The set implements the ISet interface (see HashSet_AsSet) and exposes its elements
 * as an ICollection through that interface. Construct an instance with HashSet_Construct1 and
 * release it with HashSet_Deconstruct.
 *
 * Enumeration pointer stability: enumerating the element collection by reference yields pointers
 * directly into the set's storage block. Any operation that rehashes the table - growth on
 * insertion, or a rebuild to clear accumulated tombstones - reallocates that block and invalidates
 * ALL previously returned element pointers and live enumerators. Treat such pointers as valid only
 * until the next add or remove.
 * @note Not thread-safe; external synchronization is required for concurrent access.
 */
typedef struct HashSetStruct
{
    /** @brief Embedded ISet interface; obtain a pointer to it with HashSet_AsSet. */
    ISet _set;
    /** @brief Callback used to hash elements; never NULL after successful construction. */
    HashSetElementHashFunction _elementHashFunction;
    /** @brief Stored copy of the user context passed to the element hash function on every call. */
    UserData _elementHashFunctionUserData;
    /** @brief Callback used to test two elements for equality; defaults to a byte-wise comparison if none was supplied. */
    SetElementComparator _elementComparator;
    /** @brief Stored copy of the user context passed to the element comparator on every call. */
    UserData _elementComparatorUserData;
    /** @brief Backing storage for the single contiguous block holding bucket metadata, then elements. Empty until the first allocation. */
    GenericBuffer _dataBuffer;
    /** @brief True when the set allocated and owns the active storage block and must free it on deconstruction. */
    bool _isActiveBufferOwned;
    /** @brief Number of live elements currently stored (excludes tombstones); this is the value reported by ISet_GetElementCount. */
    size_t _elementCount;
    /** @brief Number of buckets currently marked as deleted (tombstones); these still occupy slots until the table is rebuilt. */
    size_t _tombstoneCount;
    /** @brief Total number of buckets in the current storage block (always a power of two, or 0 when no storage is allocated). */
    size_t _capacity;
} HashSet;

/**
 * @brief Parameters that configure a HashSet when passed to HashSet_Construct1.
 *
 * Populate this directly or start from HashSetConstructOptions_CreateDefault and override fields.
 * ElementSize and ElementHashFunction are required; the remaining fields may be left zero/NULL to
 * accept defaults.
 */
typedef struct HashSetConstructOptionsStruct
{
    /** @brief Size in bytes of each element; must be greater than zero. Every element copied into the set is read/stored at this exact size. */
    size_t ElementSize;
    /** @brief Required hash callback for elements; must not be NULL. Must agree with ElementComparator (equal elements hash equally). */
    HashSetElementHashFunction ElementHashFunction;
    /** @brief Context value copied into the set and handed to ElementHashFunction on every call. Leave empty if unused. */
    UserData ElementHashFunctionUserData;
    /** @brief Optional element equality callback; if NULL the set uses a default byte-wise element comparison. */
    SetElementComparator ElementComparator;
    /** @brief Context value copied into the set and handed to ElementComparator on every call. Leave empty if unused. */
    UserData ElementComparatorUserData;
    /** @brief Hint for the number of buckets to pre-allocate. 0 defers allocation until the first insertion; non-zero values are rounded up to a power of two with a fixed minimum applied. */
    size_t InitialCapacity;
} HashSetConstructOptions;


// Functions.
/**
 * @brief Builds a HashSetConstructOptions populated with the required fields and sensible defaults.
 *
 * Sets ElementSize and ElementHashFunction from the arguments; clears all UserData fields to empty;
 * leaves ElementComparator NULL (so the set selects its default byte-wise comparator); and sets
 * InitialCapacity to 0 (allocation deferred to the first insertion). Override any field on the
 * returned value before passing it to HashSet_Construct1.
 * @param elementSize Size in bytes of each element; should be greater than zero (validated by HashSet_Construct1).
 * @param elementHashFunction Hash callback for elements; should not be NULL (validated by HashSet_Construct1).
 * @returns A fully initialized options value ready to be customized and/or passed to HashSet_Construct1.
 */
static inline HashSetConstructOptions HashSetConstructOptions_CreateDefault(size_t elementSize,
    HashSetElementHashFunction elementHashFunction)
{
    return (HashSetConstructOptions) {
        .ElementSize = elementSize,
        .ElementHashFunction = elementHashFunction,
        .ElementHashFunctionUserData = UserData_CreateEmpty(),
        .ElementComparator = NULL,
        .ElementComparatorUserData = UserData_CreateEmpty(),
        .InitialCapacity = 0,
    };
}

/**
 * @brief Returns a pointer to the set's embedded ISet interface.
 *
 * Use the returned interface to perform all set operations (add, remove, clear, contains, element
 * enumeration, set algebra) via the ISet_* functions. The pointer refers to storage inside self and
 * is valid for as long as self is alive and not deconstructed; it does not transfer ownership.
 * @param self The hash set. Must not be NULL and should be successfully constructed.
 * @returns A non-owning pointer to self's ISet interface.
 */
static inline ISet* HashSet_AsSet(HashSet* self)
{
    return &self->_set;
}

/**
 * @brief Constructs a hash set in place from the given options, leaving it ready for use.
 *
 * Initializes the set's state and ISet interface, records the element size, hash function, and
 * comparator (substituting a default byte-wise comparator when ElementComparator is NULL), and
 * stores copies of the supplied UserData values. If options.InitialCapacity is greater than zero,
 * the backing storage is allocated immediately (capacity rounded up to a power of two with a fixed
 * minimum); otherwise allocation is deferred until the first insertion. The resulting set is empty
 * regardless of any pre-allocated capacity. The object must later be released with
 * HashSet_Deconstruct.
 * @param self Pointer to uninitialized HashSet storage to construct. Must not be NULL.
 * @param options Configuration for the set (passed by value). ElementSize must be greater than zero
 *        and ElementHashFunction must not be NULL.
 * @returns Success on construction. Raises ErrorCode_IllegalArgument if self is NULL, if
 *          ElementSize is zero, or if ElementHashFunction is NULL; raises ErrorCode_BufferTooLarge
 *          if a requested non-zero InitialCapacity cannot be represented or allocated.
 */
Error HashSet_Construct1(HashSet* self, HashSetConstructOptions options);

/**
 * @brief Releases a hash set's backing storage and resets it to an empty, unconfigured state.
 *
 * Frees the storage block if the set owns it, then reinitializes the structure (zero elements, zero
 * capacity, default comparator, empty buffer). Because elements are stored by value and the set
 * owns no element pointers, only the single storage block is freed; any objects referenced by
 * stored elements are the caller's responsibility. Safe to call on an already-deconstructed or
 * freshly default-state set (it simply re-clears it).
 * @param self The hash set to deconstruct. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if self is NULL.
 */
Error HashSet_Deconstruct(HashSet* self);
