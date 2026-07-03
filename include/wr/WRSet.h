#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WRCollection.h"
#include "WRCompile.h"
#include "WRError.h"
#include "WRMemory.h"
#include "WRUserData.h"


/**
 * Set module provides the generic ISet interface: an unordered collection of distinct fixed-size
 * elements with membership testing, mirroring how WRMap provides the IMap interface. Concrete
 * implementations (such as WRHashSet's HashSet) embed and initialize an ISet; callers operate on
 * sets exclusively through the ISet_* functions. Besides element-level operations, the module
 * offers set-algebra functions (union, intersection, difference, subset tests, etc.) that work on
 * any ISet implementation through the generic interface.
 */


// Types.
/** @brief Opaque handle to a set interface instance; see the ISetStruct definition below. */
typedef struct ISetStruct ISet;

/**
 * @brief Bit flags describing properties of a set.
 *
 * Combined as a bitmask in ISet._flags and inspected via ISet_GetFlags / ISet_IsReadOnly.
 */
typedef enum SetFlagsEnum
{
    /** @brief No flags set; the set is mutable with no special properties. */
    SetFlags_None = 0,
    /**
     * @brief The set is read-only; mutating operations are rejected.
     *
     * When set, Add, Remove, Clear, and the mutating set-algebra functions fail with
     * ErrorCode_InvalidOperation instead of modifying the set.
     */
    SetFlags_IsReadOnly = (1 << 0),
} SetFlags;

/**
 * @brief Predicate that decides whether two elements are equal for a given set.
 *
 * Used by implementations to test element equality. A default (SetElementComparator_Default)
 * compares the raw element bytes. Implementations must return @c true when the two elements are
 * considered equal, and the relation must be a consistent equivalence (symmetric and transitive).
 * @param set The set whose element semantics apply (e.g. for querying the element size); non-NULL.
 * @param element1 Pointer to the first element; non-NULL.
 * @param element2 Pointer to the second element; non-NULL.
 * @param userData Caller-attached context for the comparison; may be unused.
 * @returns @c true if the elements are equal, otherwise @c false.
 */
typedef bool (*SetElementComparator)(ISet* set, const void* element1, const void* element2, const UserData* userData);

/**
 * @brief Function-pointer table defining the abstract contract for an ISet.
 *
 * Every concrete set supplies these slots. @c Self is passed back as the receiver to each slot. The
 * public ISet_* wrappers validate arguments (and enforce read-only/null rules) before dispatching
 * here, so slots may assume non-NULL pointers and a writable set where applicable. All conforming
 * implementations must satisfy the per-slot contracts below.
 */
typedef struct SetVTableStruct
{
    /** @brief Opaque receiver passed as the @c self argument to every slot in this vtable. */
    void* Self;
    /**
     * @brief Insert an element if no equal element is present.
     *
     * Copies element-size bytes of @p element into the set's storage when it is not already a
     * member. Writes @c true to @p outWasAdded when a new element was inserted, or @c false when an
     * equal element was already present (which is not an error). Only invoked on a writable set.
     * @param self The set receiver (the vtable's @c Self).
     * @param element Pointer to the element to insert; non-NULL.
     * @param outWasAdded [out] Receives whether a new element was inserted.
     * @returns Success when the operation completed; otherwise an Error.
     */
    Error (*_add)(void* self, const void* element, bool* outWasAdded);
    /**
     * @brief Remove the element equal to the given one, if present.
     *
     * Writes @c true to @p outWasRemoved if an element was removed, or @c false if no equal element
     * was present (which is not an error). Only invoked on a writable set.
     * @param self The set receiver (the vtable's @c Self).
     * @param element Pointer to the element to remove; non-NULL.
     * @param outWasRemoved [out] Receives whether an element was actually removed.
     * @returns Success when the operation completed; otherwise an Error.
     */
    Error (*_remove)(void* self, const void* element, bool* outWasRemoved);
    /**
     * @brief Remove all elements, leaving the set empty.
     *
     * After success the element count is zero. Only invoked on a writable set.
     * @param self The set receiver (the vtable's @c Self).
     * @returns Success when the set was cleared; otherwise an Error.
     */
    Error (*_clear)(void* self);
    /**
     * @brief Report whether an element equal to the given one is present in the set.
     *
     * Must not mutate the set.
     * @param self The set receiver (the vtable's @c Self).
     * @param element Pointer to the element to test; non-NULL.
     * @param outContains [out] Receives @c true if an equal element exists, otherwise @c false.
     * @returns Success when membership was determined; otherwise an Error.
     */
    Error (*_contains)(void* self, const void* element, bool* outContains);
    /**
     * @brief Return the number of elements currently stored in the set.
     *
     * Must not mutate the set.
     * @param self The set receiver (the vtable's @c Self).
     * @returns The current element count.
     */
    size_t (*_getElementCount)(void* self);
    /**
     * @brief Release all resources owned by the set instance.
     *
     * After this call the set must not be used again. Implementations should make this safe to
     * invoke once on a constructed set.
     * @param self The set receiver (the vtable's @c Self).
     * @returns Success when teardown completed; otherwise an Error.
     */
    Error (*_deconstruct)(void* self);
} SetVTable;

/**
 * @brief Abstract unordered collection of distinct fixed-size elements.
 *
 * An ISet stores copies of elements (each @c _elementSize bytes); no two stored elements compare
 * equal under the implementation's element comparator. It is driven through its vtable and exposes
 * a read-only ICollection view of its elements (in no guaranteed order). Callers should use the
 * ISet_* wrapper functions rather than touching these fields directly.
 */
typedef struct ISetStruct
{
    /** @brief Size in bytes of each stored element. */
    size_t _elementSize;
    /** @brief Dispatch table implementing this set's behavior. */
    SetVTable _vtable;
    /** @brief Read-only collection view over the set's elements; enumeration order is unspecified. */
    ICollection _elementCollection;
    /** @brief Property flags for this set (see SetFlags), e.g. the read-only bit. */
    SetFlags _flags;
} ISet;


// Functions.
/**
 * @brief Return the size in bytes of each element stored in the set.
 * @param set The set to query; must be non-NULL.
 * @returns The configured element size in bytes.
 */
static inline size_t ISet_GetElementSize(ISet* set)
{
    return set->_elementSize;
}

/**
 * @brief Default element-equality predicate that compares the raw element bytes.
 *
 * Conforms to SetElementComparator. Returns whether the first ISet_GetElementSize(set) bytes of the
 * two elements are identical; @p userData is ignored.
 * @param set The set supplying the element size; must be non-NULL.
 * @param element1 Pointer to the first element; non-NULL with at least element-size bytes.
 * @param element2 Pointer to the second element; non-NULL with at least element-size bytes.
 * @param userData Unused.
 * @returns @c true if the element bytes are equal, otherwise @c false.
 */
static inline bool SetElementComparator_Default(ISet* set, const void* element1, const void* element2, const UserData* userData)
{
    UNUSED(userData);
    return Memory_IsEqual(element1, element2, ISet_GetElementSize(set));
}

/**
 * @brief Obtain a read-only collection view over the set's elements.
 *
 * The returned ICollection enumerates the set's elements in an unspecified order; no ordering may
 * be assumed between enumerations or after mutations. It aliases storage inside @p set and remains
 * valid only as long as the set does; do not free it. Enumerators over the view are invalidated by
 * any mutation of the set.
 * @param set The set to view; must be non-NULL.
 * @returns A pointer to the set's element collection view.
 */
static inline ICollection* ISet_AsCollection(ISet* set)
{
    return &set->_elementCollection;
}

/**
 * @brief Return the number of elements currently stored in the set.
 * @param set The set to query; must be non-NULL.
 * @returns The current element count.
 */
static inline size_t ISet_GetElementCount(ISet* set)
{
    return set->_vtable._getElementCount(set->_vtable.Self);
}

/**
 * @brief Return the property flags set on the set.
 * @param set The set to query; must be non-NULL.
 * @returns The set's SetFlags bitmask.
 */
static inline SetFlags ISet_GetFlags(ISet* set)
{
    return set->_flags;
}

/**
 * @brief Report whether the set is read-only.
 *
 * Tests the SetFlags_IsReadOnly bit. When true, Add, Remove, Clear, and the mutating set-algebra
 * functions are rejected.
 * @param set The set to query; must be non-NULL.
 * @returns @c true if the set is read-only, otherwise @c false.
 */
static inline bool ISet_IsReadOnly(ISet* set)
{
    return (ISet_GetFlags(set) & SetFlags_IsReadOnly) != 0;
}

/**
 * @brief Insert an element if no equal element is present, reporting whether it was inserted.
 *
 * Validates arguments and rejects read-only sets, then dispatches to the implementation, which
 * copies the element into the set. Adding an element that is already present is not an error:
 * @p outWasAdded receives @c false in that case and @c true when a new element was inserted.
 * @param self The set to modify; must be non-NULL and not read-only.
 * @param element Pointer to the element to insert; must be non-NULL.
 * @param outWasAdded [out] Receives @c true if a new element was inserted; must be non-NULL.
 * @returns Success when the operation completed. Raises ErrorCode_IllegalArgument if @p self,
 *          @p element, or @p outWasAdded is NULL; ErrorCode_InvalidOperation if the set is
 *          read-only; otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_Add(ISet* self, const void* element, bool* outWasAdded);

/**
 * @brief Remove the element equal to the given one if present, reporting whether one was removed.
 *
 * Validates arguments and rejects read-only sets, then dispatches to the implementation. A missing
 * element is not an error: @p outWasRemoved receives @c false in that case and @c true when an
 * element was deleted.
 * @param self The set to modify; must be non-NULL and not read-only.
 * @param element Pointer to the element to remove; must be non-NULL.
 * @param outWasRemoved [out] Receives whether an element was actually removed; must be non-NULL.
 * @returns Success when the operation completed. Raises ErrorCode_IllegalArgument if @p self,
 *          @p element, or @p outWasRemoved is NULL; ErrorCode_InvalidOperation if the set is
 *          read-only; otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_Remove(ISet* self, const void* element, bool* outWasRemoved);

/**
 * @brief Remove all elements from the set, leaving it empty.
 *
 * Rejects read-only sets, then dispatches to the implementation. After success the element count is
 * zero.
 * @param self The set to clear; must be non-NULL and not read-only.
 * @returns Success when the set was cleared. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_InvalidOperation if the set is read-only; otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_Clear(ISet* self);

/**
 * @brief Report whether an element equal to the given one is present in the set.
 *
 * Validates arguments, then dispatches to the implementation.
 * @param self The set to query; must be non-NULL.
 * @param element Pointer to the element to test; must be non-NULL.
 * @param outContains [out] Receives @c true if an equal element exists, otherwise @c false; must be non-NULL.
 * @returns Success when membership was determined. Raises ErrorCode_IllegalArgument if @p self,
 *          @p element, or @p outContains is NULL; otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_Contains(ISet* self, const void* element, bool* outContains);

/**
 * @brief Release all resources owned by the set.
 *
 * Validates @p self, then dispatches to the implementation's teardown. After this call the set must
 * not be used again.
 * @param self The set to deconstruct; must be non-NULL.
 * @returns Success when teardown completed. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          otherwise an Error from the implementation.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_Deconstruct(ISet* self);


/*
 * Set-algebra functions. These operate purely through the generic interfaces, so they work with any
 * ISet implementation. Where a parameter only needs to be enumerated it is typed ICollection*
 * (any collection works, duplicates are harmless); where the operation needs membership queries or
 * relies on the elements being distinct it is typed ISet*. Both sides must store elements of the
 * same size, and meaningful results require both sides to use compatible element-equality
 * semantics. Unless stated otherwise, @p other must not be @p self or a collection view of
 * @p self; the documented aliasing shortcuts are the only supported same-object cases.
 */

/**
 * @brief Adds every element of a collection to the set (in-place set union).
 *
 * Enumerates @p other and adds each element to @p self; elements already present are skipped.
 * Duplicate elements in @p other are harmless. As a shortcut, when @p other is @p self's own
 * element collection view the call is a successful no-op.
 * @param self The set to modify; must be non-NULL and not read-only.
 * @param other The collection whose elements are added; must be non-NULL, with the same element
 *        size as @p self, and (aside from the documented shortcut) must not be a view of @p self.
 * @returns Success when all elements were processed. Raises ErrorCode_IllegalArgument if @p self or
 *          @p other is NULL, or if the element sizes differ; ErrorCode_InvalidOperation if the set
 *          is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_UnionWith(ISet* self, ICollection* other);

/**
 * @brief Removes every element of a collection from the set (in-place set difference).
 *
 * Enumerates @p other and removes each element from @p self; elements not present are skipped.
 * Duplicate elements in @p other are harmless. As a shortcut, when @p other is @p self's own
 * element collection view the set is cleared.
 * @param self The set to modify; must be non-NULL and not read-only.
 * @param other The collection whose elements are removed; must be non-NULL, with the same element
 *        size as @p self, and (aside from the documented shortcut) must not be a view of @p self.
 * @returns Success when all elements were processed. Raises ErrorCode_IllegalArgument if @p self or
 *          @p other is NULL, or if the element sizes differ; ErrorCode_InvalidOperation if the set
 *          is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_ExceptWith(ISet* self, ICollection* other);

/**
 * @brief Keeps only the elements that are also present in another set (in-place set intersection).
 *
 * Removes from @p self every element that @p other does not contain. Requires an ISet (not a plain
 * collection) so membership tests are efficient. When @p other is @p self the call is a successful
 * no-op. Uses internal scratch storage sized up to the number of removed elements (no allocation
 * occurs when nothing is removed).
 * @param self The set to modify; must be non-NULL and not read-only.
 * @param other The set to intersect with; must be non-NULL and have the same element size as @p self.
 * @returns Success when the intersection was applied. Raises ErrorCode_IllegalArgument if @p self
 *          or @p other is NULL, or if the element sizes differ; ErrorCode_InvalidOperation if
 *          @p self is read-only, or if the internal scratch storage could not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_IntersectWith(ISet* self, ISet* other);

/**
 * @brief Toggles membership of every element of another set (in-place symmetric difference).
 *
 * For each element of @p other: if @p self contains it, it is removed, otherwise it is added.
 * Afterwards @p self holds exactly the elements that were in precisely one of the two sets.
 * Requires an ISet (not a plain collection) because duplicate elements would toggle twice. When
 * @p other is @p self the set is cleared.
 * @param self The set to modify; must be non-NULL and not read-only.
 * @param other The set whose elements are toggled; must be non-NULL and have the same element size
 *        as @p self.
 * @returns Success when all elements were processed. Raises ErrorCode_IllegalArgument if @p self or
 *          @p other is NULL, or if the element sizes differ; ErrorCode_InvalidOperation if @p self
 *          is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_SymmetricExceptWith(ISet* self, ISet* other);

/**
 * @brief Reports whether the set and a collection share at least one element.
 *
 * Enumerates @p other and tests each element for membership in @p self, stopping at the first hit.
 * As a shortcut, when @p other is @p self's own element collection view the result is whether the
 * set is non-empty. An empty @p other never overlaps.
 * @param self The set to query; must be non-NULL.
 * @param other The collection to test against; must be non-NULL and have the same element size as @p self.
 * @param outOverlaps [out] Receives @c true if any element of @p other is in @p self; must be non-NULL.
 * @returns Success when the result was determined. Raises ErrorCode_IllegalArgument if @p self,
 *          @p other, or @p outOverlaps is NULL, or if the element sizes differ.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_Overlaps(ISet* self, ICollection* other, bool* outOverlaps);

/**
 * @brief Reports whether every element of the set is contained in another set.
 *
 * Tests the subset relation (not necessarily proper): an empty set is a subset of everything, and a
 * set is always a subset of itself. Requires an ISet for the right-hand side so membership tests
 * are efficient and the element-count shortcut is valid.
 * @param self The candidate subset; must be non-NULL.
 * @param other The candidate superset; must be non-NULL and have the same element size as @p self.
 * @param outIsSubset [out] Receives @c true if @p self is a subset of @p other; must be non-NULL.
 * @returns Success when the result was determined. Raises ErrorCode_IllegalArgument if @p self,
 *          @p other, or @p outIsSubset is NULL, or if the element sizes differ.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_IsSubsetOf(ISet* self, ISet* other, bool* outIsSubset);

/**
 * @brief Reports whether the set contains every element of a collection.
 *
 * Tests the superset relation (not necessarily proper): every set is a superset of an empty
 * collection. Duplicate elements in @p other are harmless. As a shortcut, when @p other is
 * @p self's own element collection view the result is @c true.
 * @param self The candidate superset; must be non-NULL.
 * @param other The collection whose elements must all be present; must be non-NULL and have the
 *        same element size as @p self.
 * @param outIsSuperset [out] Receives @c true if @p self contains all of @p other; must be non-NULL.
 * @returns Success when the result was determined. Raises ErrorCode_IllegalArgument if @p self,
 *          @p other, or @p outIsSuperset is NULL, or if the element sizes differ.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_IsSupersetOf(ISet* self, ICollection* other, bool* outIsSuperset);

/**
 * @brief Reports whether two sets contain exactly the same elements.
 *
 * Equal when the element counts match and every element of @p self is contained in @p other. A set
 * always equals itself. Meaningful results require both sets to use compatible element-equality
 * semantics.
 * @param self The first set; must be non-NULL.
 * @param other The second set; must be non-NULL and have the same element size as @p self.
 * @param outEquals [out] Receives @c true if the sets are equal; must be non-NULL.
 * @returns Success when the result was determined. Raises ErrorCode_IllegalArgument if @p self,
 *          @p other, or @p outEquals is NULL, or if the element sizes differ.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ISet_SetEquals(ISet* self, ISet* other, bool* outEquals);
