#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"
#include "WRCollection.h"
#include "WRComparator.h"
#include "WRMemory.h"
#include "WRUserData.h"


/**
 * @brief Sentinel index meaning "no such element" / "not found".
 *
 * The maximum size_t value (all bits set). Index-returning search operations
 * (IList_FirstIndexOf / IList_LastIndexOf) write this to their out-parameter when
 * no element matches. It is never a valid element position.
 */
#define LIST_INDEX_INVALID (~((size_t)0))


// Types.
/**
 * @brief Bit flags describing properties of an IList instance.
 *
 * Stored in IList._flags and queried with IList_GetFlags / IList_IsReadOnly.
 */
typedef enum IListFlagsEnum
{
    /** @brief No flags set. */
    IListFlags_None = 0,
    /** @brief The list rejects all mutating operations (insert, remove, replace, clear, sort, etc.). */
    IListFlags_IsReadOnly = (1 << 0),
} IListFlags;

/**
 * @brief Dispatch table that a concrete list implementation fills in to provide the IList interface.
 *
 * Each function pointer is the abstract contract every conforming implementation must satisfy; the
 * caller-facing IList_* functions validate their arguments and then delegate to these slots, passing
 * the Self pointer as the `self` argument. All slots must be non-NULL on a fully constructed list.
 * Index/range bounds checking is the implementation's responsibility: out-of-range positions must be
 * reported (typically ErrorCode_IndexOutOfBounds), not treated as undefined behavior.
 */
typedef struct IListVTableStruct
{
    /** @brief Opaque pointer to the concrete implementation instance; passed as `self` to every slot below. */
    void* Self;

    /**
     * @brief Returns the number of elements currently in the list.
     * @param self The implementation instance (the vtable's Self).
     * @returns The element count; 0 for an empty list. Must not fail.
     */
    size_t (*_getElementCount)(void* self);

    /**
     * @brief Inserts one element at the given index, shifting later elements up by one.
     * @param self The implementation instance.
     * @param index Position at which to insert. Valid range is 0..count inclusive (count appends at the end).
     * @param element Pointer to the source element to copy in; never NULL (the wrapper guarantees this). Element-sized bytes are copied.
     * @returns Success on insertion. Must fail if the list is read-only or if index exceeds count
     *          (e.g. ErrorCode_IndexOutOfBounds), and may fail if storage could not be grown.
     */
    Error (*_insert)(void* self, size_t index, void* element);

    /**
     * @brief Removes the element at the given index, shifting later elements down by one.
     * @param self The implementation instance.
     * @param index Position to remove. Valid range is 0..count-1.
     * @returns Success on removal. Must fail if the list is read-only or if index is out of range.
     */
    Error (*_remove)(void* self, size_t index);

    /**
     * @brief Overwrites the element at the given index with a copy of the supplied element.
     * @param self The implementation instance.
     * @param index Position to overwrite. Valid range is 0..count-1.
     * @param element Pointer to the replacement element; never NULL (the wrapper guarantees this). Element-sized bytes are copied.
     * @returns Success on replacement. Must fail if the list is read-only or if index is out of range.
     */
    Error (*_replace)(void* self, size_t index, void* element);

    /**
     * @brief Inserts a contiguous run of elements starting at the given index, shifting later elements up.
     * @param self The implementation instance.
     * @param index Position at which to begin inserting. Valid range is 0..count inclusive.
     * @param elements Pointer to the first source element of a contiguous, element-sized array. May be NULL only when elementCount is 0.
     * @param elementCount Number of elements to insert; 0 is a valid no-op.
     * @returns Success on insertion. Must fail if the list is read-only or if index exceeds count, and
     *          may fail if storage could not be grown.
     */
    Error (*_insertRange)(void* self, size_t index, void* elements, size_t elementCount);

    /**
     * @brief Removes elementCount consecutive elements starting at the given index, shifting later elements down.
     * @param self The implementation instance.
     * @param index Position of the first element to remove.
     * @param elementCount Number of elements to remove. The range [index, index+elementCount) must lie within the list; 0 is a valid no-op.
     * @returns Success on removal. Must fail if the list is read-only or if the range is out of bounds.
     */
    Error (*_removeRange)(void* self, size_t index, size_t elementCount);

    /**
     * @brief Overwrites elementCount consecutive elements starting at the given index with copies of the supplied elements.
     * @param self The implementation instance.
     * @param index Position of the first element to overwrite. The range [index, index+elementCount) must lie within the list.
     * @param elements Pointer to the first replacement element of a contiguous, element-sized array. May be NULL only when elementCount is 0.
     * @param elementCount Number of elements to overwrite; 0 is a valid no-op.
     * @returns Success on replacement. Must fail if the list is read-only or if the range is out of bounds.
     */
    Error (*_replaceRange)(void* self, size_t index, void* elements, size_t elementCount);

    /**
     * @brief Removes all elements, leaving an empty list.
     * @param self The implementation instance.
     * @returns Success once the list is empty. Must fail if the list is read-only.
     */
    Error (*_clear)(void* self);

    /**
     * @brief Copies the element at the given index into a caller-provided buffer (access by value).
     * @param self The implementation instance.
     * @param index Position to read. Valid range is 0..count-1.
     * @param outElement [out] Destination buffer of at least element-size bytes; never NULL (the wrapper guarantees this). On success it receives a copy of the element.
     * @returns Success after writing outElement. Must fail if index is out of range, in which case outElement is left unspecified.
     */
    Error (*_getElement)(void* self, size_t index, void* outElement);

    /**
     * @brief Obtains a direct pointer to the element at the given index (access by reference), when supported.
     * @param self The implementation instance.
     * @param index Position to address. Valid range is 0..count-1.
     * @param outElementPointer [out] Receives a borrowed pointer to the element's storage; never NULL itself (the wrapper guarantees this). The pointer is owned by the list and may be invalidated by any mutating operation.
     * @returns Success after writing outElementPointer. May fail if index is out of range, or with an
     *          error indicating that this implementation cannot return elements by reference (callers
     *          that can fall back to _getElement use this signal to copy instead).
     */
    Error (*_getPointerToElement)(void* self, size_t index, void** outElementPointer);

    /**
     * @brief Releases any resources owned by the concrete implementation.
     * @param self The implementation instance.
     */
    void (*_deconstruct)(void* self);
} IListVTable;

/**
 * @brief The generic list interface: an element descriptor plus a vtable that dispatches to a concrete implementation.
 *
 * Callers operate on lists exclusively through IList_* functions, which validate arguments and then
 * delegate to the vtable slots. An IList is normally embedded in and initialized by a concrete list
 * type (for example ArrayList); its lifetime is tied to that owner. It also exposes an embedded
 * ICollection so the list can be enumerated through the collection interface.
 */
typedef struct IListStruct
{
    /** @brief Size in bytes of each element; the stride used by all by-value copies and scratch sizing. */
    size_t _elementSize;
    /** @brief Property flags for this list (notably whether it is read-only). */
    IListFlags _flags;
    /** @brief Dispatch table whose slots implement the actual list operations. */
    IListVTable _vtable;
    /** @brief Embedded collection interface used to enumerate the list (see IList_AsCollection). */
    ICollection _collection;
} IList;

/**
 * @brief A single element handed to a list callback: its location and its position in the list.
 *
 * Passed by value to predicates, comparators, mappers, and extractors so they can inspect the element
 * and know where it sits.
 */
typedef struct IListElementDataStruct
{
    /** @brief Borrowed pointer to the element's bytes (either into list storage or into a scratch copy). Valid only for the duration of the callback. */
    void* _element;
    /** @brief Zero-based index of this element within the list. */
    size_t _index;
} IListElementData;

/**
 * @brief Callback that tests whether a list element satisfies some condition.
 *
 * Used by filter / contains / count / index-of operations. Implementations must not mutate the list.
 * @param list The list being scanned.
 * @param element The current element (pointer and index).
 * @param userData Caller-attached context, or NULL if none was supplied.
 * @returns true if the element matches the predicate, false otherwise.
 */
typedef bool (*IListPredicate)(IList* list, IListElementData element, const UserData* userData);

/**
 * @brief Callback that orders two list elements relative to each other.
 *
 * Used by the sort operations to impose an ordering. Implementations must not mutate the list and
 * should be a consistent total order for well-defined sorting.
 * @param list The list being sorted.
 * @param a The first element (pointer and index).
 * @param b The second element (pointer and index).
 * @param userData Caller-attached context, or NULL if none was supplied.
 * @returns A ComparisonResult describing whether `a` is less than, equal to, or greater than `b`.
 */
typedef ComparisonResult (*IListComparator)(IList* list, IListElementData a, IListElementData b, const UserData* userData);

/**
 * @brief Callback that transforms a source list element into a destination element.
 *
 * Used by the map operations. The callback must write a fully-formed destination element to
 * destinationElement; it must not mutate the source list.
 * @param list The source list.
 * @param sourceElement The current source element (pointer and index).
 * @param destinationElement [out] Buffer (one destination-element wide) into which the mapped result must be written.
 * @param userData Caller-attached context, or NULL if none was supplied.
 */
typedef void (*IListMapper)(IList* list, IListElementData sourceElement, void* destinationElement, const UserData* userData);

/**
 * @brief Callback that extracts a signed integer key from a list element.
 *
 * Used by the integer sum / max / min reductions. Implementations must not mutate the list.
 * @param list The list being reduced.
 * @param sourceElement The current element (pointer and index).
 * @param userData Caller-attached context, or NULL if none was supplied.
 * @returns The 64-bit signed value derived from the element.
 */
typedef int64_t (*IListIntExtractor)(IList* list, IListElementData sourceElement, const UserData* userData);

/**
 * @brief Callback that extracts a floating-point key from a list element.
 *
 * Used by the floating-point sum / max / min reductions. Implementations must not mutate the list.
 * @param list The list being reduced.
 * @param sourceElement The current element (pointer and index).
 * @param userData Caller-attached context, or NULL if none was supplied.
 * @returns The double value derived from the element.
 */
typedef double (*IListDoubleExtractor)(IList* list, IListElementData sourceElement, const UserData* userData);



// Functions.
/**
 * @brief Returns the list viewed as its embedded collection interface.
 *
 * @param self The list. Must not be NULL.
 * @returns A borrowed pointer to the list's embedded ICollection (the &_collection field). The
 *          pointer is owned by the list and valid for the list's lifetime; do not free it.
 */
static inline ICollection* IList_AsCollection(IList* self)
{
    return &self->_collection;
}

/**
 * @brief Returns the byte size of an enumerator buffer for iterating this list.
 *
 * Forwards to the embedded collection. Use this to size a caller-owned buffer for IList_InitEnumerator.
 * @param self The list. Must not be NULL.
 * @returns The minimum number of bytes a buffer passed to IList_InitEnumerator must have.
 */
static inline size_t IList_GetEnumeratorSize(IList* self)
{
    return ICollection_GetEnumeratorSize(IList_AsCollection(self));
}

/**
 * @brief Constructs an enumerator over the list into a caller-provided buffer.
 *
 * Forwards to the embedded collection. Prefer this form on hot paths: the buffer may be reused or
 * stack-allocated to avoid per-iteration allocation.
 * @param self The list. Must not be NULL.
 * @param buffer Caller-owned storage of at least IList_GetEnumeratorSize(self) bytes. Must outlive the enumerator.
 * @returns A pointer to the initialized CollectionEnumerator (typically located within buffer). Release
 *          it with CollectionEnumerator_Deconstruct; the buffer itself is not freed by that call.
 */
static inline CollectionEnumerator* IList_InitEnumerator(IList* self, void* buffer)
{
    return ICollection_InitEnumerator(IList_AsCollection(self), buffer);
}

/**
 * @brief Allocates a correctly sized enumerator buffer and constructs an enumerator over the list into it.
 *
 * Forwards to the embedded collection. Convenience for callers without a reusable buffer; prefer
 * IList_InitEnumerator on hot paths.
 * @param self The list. Must not be NULL.
 * @returns A pointer to a newly allocated, initialized CollectionEnumerator. The caller must release it
 *          with CollectionEnumerator_Destroy (or CollectionEnumerator_Deconstruct followed by freeing the pointer).
 */
static inline CollectionEnumerator* IList_CreateEnumerator(IList* self)
{
    return ICollection_CreateEnumerator(IList_AsCollection(self));
}

/**
 * @brief Returns the number of elements currently in the list.
 *
 * Dispatches to the vtable's element-count slot.
 * @param self The list. Must not be NULL.
 * @returns The current element count; 0 if the list is empty.
 */
static inline size_t IList_GetElementCount(IList* self)
{
    return self->_vtable._getElementCount(self->_vtable.Self);
}

/**
 * @brief Returns the size in bytes of each element stored in the list.
 *
 * @param self The list. Must not be NULL.
 * @returns The per-element byte size (the stride used for by-value copies).
 */
static inline size_t IList_GetElementSize(IList* self)
{
    return self->_elementSize;
}

/**
 * @brief Returns the list's property flags.
 *
 * @param self The list. Must not be NULL.
 * @returns The IListFlags bit set for this list.
 */
static inline IListFlags IList_GetFlags(IList* self)
{
    return self->_flags;
}

/**
 * @brief Reports whether the list rejects mutating operations.
 *
 * @param self The list. Must not be NULL.
 * @returns true if the IListFlags_IsReadOnly flag is set, false otherwise.
 */
static inline bool IList_IsReadOnly(IList* self)
{
    return (IList_GetFlags(self) & IListFlags_IsReadOnly) != 0;
}

/**
 * @brief Appends an element to the end of the list.
 *
 * Copies element-size bytes from `element` into a new slot after the current last element.
 * @param self The list. Must not be NULL.
 * @param element Pointer to the source element to copy. Must not be NULL.
 * @returns Success on insertion. Raises ErrorCode_IllegalArgument if self or element is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_AddLast(IList* self, void* element);

/**
 * @brief Inserts an element at the front of the list, shifting existing elements up by one.
 *
 * Copies element-size bytes from `element` into a new slot at index 0.
 * @param self The list. Must not be NULL.
 * @param element Pointer to the source element to copy. Must not be NULL.
 * @returns Success on insertion. Raises ErrorCode_IllegalArgument if self or element is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_AddFirst(IList* self, void* element);

/**
 * @brief Inserts an element at the given index, shifting elements at and after it up by one.
 *
 * Copies element-size bytes from `element` into a new slot at `index`.
 * @param self The list. Must not be NULL.
 * @param index Position at which to insert. Valid range is 0..count inclusive (count appends at the end); out-of-range values are rejected by the implementation.
 * @param element Pointer to the source element to copy. Must not be NULL.
 * @returns Success on insertion. Raises ErrorCode_IllegalArgument if self or element is NULL; the
 *          implementation reports an out-of-range index (typically ErrorCode_IndexOutOfBounds) and read-only lists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_Insert(IList* self, size_t index, void* element);

/**
 * @brief Removes the first element, shifting the remaining elements down by one.
 *
 * @param self The list. Must not be NULL.
 * @returns Success on removal. Raises ErrorCode_IllegalArgument if self is NULL. If the list is empty,
 *          the implementation's remove(index 0) reports the failure (typically ErrorCode_IndexOutOfBounds).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_RemoveFirst(IList* self);

/**
 * @brief Removes the last element.
 *
 * @param self The list. Must not be NULL.
 * @returns Success on removal. Raises ErrorCode_IllegalArgument if self is NULL, and
 *          ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_RemoveLast(IList* self);

/**
 * @brief Removes the element at the given index, shifting later elements down by one.
 *
 * @param self The list. Must not be NULL.
 * @param index Position to remove. Valid range is 0..count-1; out-of-range values are rejected by the implementation.
 * @returns Success on removal. Raises ErrorCode_IllegalArgument if self is NULL; the implementation
 *          reports an out-of-range index (typically ErrorCode_IndexOutOfBounds) and read-only lists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_RemoveAt(IList* self, size_t index);

/**
 * @brief Overwrites the element at the given index with a copy of the supplied element.
 *
 * Copies element-size bytes from `element` over the existing element at `index`; the element count is unchanged.
 * @param self The list. Must not be NULL.
 * @param index Position to overwrite. Valid range is 0..count-1; out-of-range values are rejected by the implementation.
 * @param element Pointer to the replacement element to copy. Must not be NULL.
 * @returns Success on replacement. Raises ErrorCode_IllegalArgument if self or element is NULL; the
 *          implementation reports an out-of-range index (typically ErrorCode_IndexOutOfBounds) and read-only lists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_Replace(IList* self, size_t index, void* element);

/**
 * @brief Appends a contiguous run of elements to the end of the list.
 *
 * Copies `elementCount` consecutive element-size records from `elements` after the current last element.
 * @param self The list. Must not be NULL.
 * @param elements Pointer to the first source element of a contiguous array. May be NULL only when elementCount is 0.
 * @param elementCount Number of elements to append; 0 is a valid no-op.
 * @returns Success on insertion. Raises ErrorCode_IllegalArgument if self is NULL, or if elementCount > 0 and elements is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_AddRangeLast(IList* self, void* elements, size_t elementCount);

/**
 * @brief Inserts a contiguous run of elements at the front of the list, shifting existing elements up.
 *
 * Copies `elementCount` consecutive element-size records from `elements` starting at index 0.
 * @param self The list. Must not be NULL.
 * @param elements Pointer to the first source element of a contiguous array. May be NULL only when elementCount is 0.
 * @param elementCount Number of elements to insert; 0 is a valid no-op.
 * @returns Success on insertion. Raises ErrorCode_IllegalArgument if self is NULL, or if elementCount > 0 and elements is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_AddRangeFirst(IList* self, void* elements, size_t elementCount);

/**
 * @brief Inserts a contiguous run of elements starting at the given index, shifting later elements up.
 *
 * Copies `elementCount` consecutive element-size records from `elements` starting at `index`.
 * @param self The list. Must not be NULL.
 * @param index Position at which to begin inserting. Valid range is 0..count inclusive; out-of-range values are rejected by the implementation.
 * @param elements Pointer to the first source element of a contiguous array. May be NULL only when elementCount is 0.
 * @param elementCount Number of elements to insert; 0 is a valid no-op.
 * @returns Success on insertion. Raises ErrorCode_IllegalArgument if self is NULL, or if elementCount > 0
 *          and elements is NULL; the implementation reports an out-of-range index and read-only lists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_InsertRange(IList* self, size_t index, void* elements, size_t elementCount);

/**
 * @brief Removes a run of consecutive elements starting at the given index, shifting later elements down.
 *
 * @param self The list. Must not be NULL.
 * @param index Position of the first element to remove.
 * @param elementCount Number of elements to remove; the range [index, index+elementCount) must lie within the list. 0 is a valid no-op.
 * @returns Success on removal. Raises ErrorCode_IllegalArgument if self is NULL; the implementation
 *          reports an out-of-range range (typically ErrorCode_IndexOutOfBounds) and read-only lists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_RemoveRange(IList* self, size_t index, size_t elementCount);

/**
 * @brief Overwrites a run of consecutive elements starting at the given index with copies of the supplied elements.
 *
 * Copies `elementCount` consecutive element-size records from `elements` over the existing elements; the count is unchanged.
 * @param self The list. Must not be NULL.
 * @param index Position of the first element to overwrite; the range [index, index+elementCount) must lie within the list.
 * @param elements Pointer to the first replacement element of a contiguous array. May be NULL only when elementCount is 0.
 * @param elementCount Number of elements to overwrite; 0 is a valid no-op.
 * @returns Success on replacement. Raises ErrorCode_IllegalArgument if self is NULL, or if elementCount > 0
 *          and elements is NULL; the implementation reports an out-of-range range and read-only lists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_ReplaceRange(IList* self, size_t index, void* elements, size_t elementCount);

/**
 * @brief Removes all elements, leaving the list empty.
 *
 * @param self The list. Must not be NULL.
 * @returns Success once the list is empty. Raises ErrorCode_IllegalArgument if self is NULL; the
 *          implementation rejects read-only lists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_Clear(IList* self);



/**
 * @brief Copies the element at the given index into a caller-provided buffer (access by value).
 *
 * @param self The list. Must not be NULL.
 * @param index Position to read. Valid range is 0..count-1; out-of-range values are rejected by the implementation.
 * @param outElement [out] Destination buffer of at least IList_GetElementSize(self) bytes. Must not be NULL. On success it receives a copy of the element.
 * @returns Success after writing outElement. Raises ErrorCode_IllegalArgument if self or outElement is
 *          NULL; the implementation reports an out-of-range index (typically ErrorCode_IndexOutOfBounds).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_GetElement(IList* self, size_t index, void* outElement);

/**
 * @brief Obtains a direct pointer to the element at the given index (access by reference), when the implementation supports it.
 *
 * Avoids a copy by returning a pointer into the list's storage.
 * @param self The list. Must not be NULL.
 * @param index Position to address. Valid range is 0..count-1; out-of-range values are rejected by the implementation.
 * @param outElementPointer [out] Receives a borrowed pointer to the element. Must not be NULL. The
 *        returned pointer is owned by the list and may be invalidated by any subsequent mutating operation; do not free it.
 * @returns Success after writing outElementPointer. Raises ErrorCode_IllegalArgument if self or
 *          outElementPointer is NULL; the implementation reports an out-of-range index, or an error
 *          indicating it cannot return elements by reference (in which case use IList_GetElement to copy).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_GetPointerToElement(IList* self, size_t index, void** outElementPointer);

/**
 * @brief Copies the first element into a caller-provided buffer.
 *
 * @param self The list. Must not be NULL.
 * @param outElement [out] Destination buffer of at least IList_GetElementSize(self) bytes. Must not be NULL. On success it receives a copy of the first element.
 * @returns Success after writing outElement. Raises ErrorCode_IllegalArgument if self or outElement is
 *          NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_GetFirst(IList* self, void* outElement);

/**
 * @brief Copies the last element into a caller-provided buffer.
 *
 * @param self The list. Must not be NULL.
 * @param outElement [out] Destination buffer of at least IList_GetElementSize(self) bytes. Must not be NULL. On success it receives a copy of the last element.
 * @returns Success after writing outElement. Raises ErrorCode_IllegalArgument if self or outElement is
 *          NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_GetLast(IList* self, void* outElement);



/**
 * @brief Returns the scratch-buffer size, in bytes, required by the scan / sort / reduce operations.
 *
 * The scan / sort / reduce operations below (filter, map, sum/max/min, contains, count, index-of,
 * reverse, copy-to, sort) need a small element-sized scratch buffer, because list elements may only
 * be reachable by value through the interface. Each such operation comes in two forms:
 *
 *   - The plain form (e.g. IList_Filter) takes a caller-owned `scratch` buffer. PREFER THIS FORM:
 *     allocate one scratch buffer of IList_GetScratchSize(self) bytes, reuse it across many calls, and
 *     these operations perform no per-call allocation. Passing NULL for `scratch` makes the call
 *     allocate and free internally (a convenience, but it hides an allocation).
 *   - The ...Allocating form (e.g. IList_FilterAllocating) always allocates and frees the scratch for
 *     you. Use it only when you have no reusable scratch buffer to provide.
 *
 * A `scratch` buffer of this size is safe for every operation here (it is sized for the largest case,
 * which holds two elements; some operations use only one element's worth).
 * @param self The list. Must not be NULL.
 * @returns The minimum number of bytes a caller-owned `scratch` buffer must have for these operations
 *          (two elements' worth: IList_GetElementSize(self) * 2).
 */
static inline size_t IList_GetScratchSize(IList* self)
{
    return self->_elementSize * 2;
}

/**
 * @brief Sorts the list in ascending order according to a comparator, in place.
 *
 * Reorders the list so that, per `comparator`, smaller elements come first. The sort is not stable. A
 * list with fewer than two elements is left unchanged and succeeds trivially.
 * @param self The list. Must not be NULL and must not be read-only.
 * @param comparator Ordering callback. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the comparator, or NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes, reused
 *        across calls with no per-call allocation. May be NULL to allocate and free internally.
 * @returns Success when sorted. Raises ErrorCode_IllegalArgument if self or comparator is NULL, and
 *          ErrorCode_InvalidOperation if the list is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_SortAscending(IList* self, IListComparator comparator, const UserData* userData, void* scratch);

/**
 * @brief Sorts the list in ascending order according to a comparator, allocating scratch internally.
 *
 * Equivalent to IList_SortAscending with a self-managed scratch buffer; use only when you have no
 * reusable scratch buffer to supply.
 * @param self The list. Must not be NULL and must not be read-only.
 * @param comparator Ordering callback. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the comparator, or NULL.
 * @returns Success when sorted. Raises ErrorCode_IllegalArgument if self or comparator is NULL, and
 *          ErrorCode_InvalidOperation if the list is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_SortAscendingAllocating(IList* self, IListComparator comparator, const UserData* userData);

/**
 * @brief Sorts the list in descending order according to a comparator, in place.
 *
 * Reorders the list so that, per `comparator`, larger elements come first. The sort is not stable. A
 * list with fewer than two elements is left unchanged and succeeds trivially.
 * @param self The list. Must not be NULL and must not be read-only.
 * @param comparator Ordering callback. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the comparator, or NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes, reused
 *        across calls with no per-call allocation. May be NULL to allocate and free internally.
 * @returns Success when sorted. Raises ErrorCode_IllegalArgument if self or comparator is NULL, and
 *          ErrorCode_InvalidOperation if the list is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_SortDescending(IList* self, IListComparator comparator, const UserData* userData, void* scratch);

/**
 * @brief Sorts the list in descending order according to a comparator, allocating scratch internally.
 *
 * Equivalent to IList_SortDescending with a self-managed scratch buffer; use only when you have no
 * reusable scratch buffer to supply.
 * @param self The list. Must not be NULL and must not be read-only.
 * @param comparator Ordering callback. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the comparator, or NULL.
 * @returns Success when sorted. Raises ErrorCode_IllegalArgument if self or comparator is NULL, and
 *          ErrorCode_InvalidOperation if the list is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_SortDescendingAllocating(IList* self, IListComparator comparator, const UserData* userData);

/**
 * @brief Keeps only the elements that satisfy a predicate, removing the rest, in place.
 *
 * Retained elements are compacted toward the front in their original relative order, and the trailing
 * non-matching elements are removed so the list shrinks to the number of matches.
 * @param self The list. Must not be NULL and must not be read-only.
 * @param predicate Callback returning true for elements to keep. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes, reused
 *        across calls with no per-call allocation. May be NULL to allocate and free internally.
 * @returns Success when filtering completes. Raises ErrorCode_IllegalArgument if self or predicate is
 *          NULL, and ErrorCode_InvalidOperation if the list is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_Filter(IList* self, IListPredicate predicate, const UserData* userData, void* scratch);

/**
 * @brief Filters the list in place, allocating scratch internally.
 *
 * Equivalent to IList_Filter with a self-managed scratch buffer; use only when you have no reusable
 * scratch buffer to supply.
 * @param self The list. Must not be NULL and must not be read-only.
 * @param predicate Callback returning true for elements to keep. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @returns Success when filtering completes. Raises ErrorCode_IllegalArgument if self or predicate is
 *          NULL, and ErrorCode_InvalidOperation if the list is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_FilterAllocating(IList* self, IListPredicate predicate, const UserData* userData);

/**
 * @brief Transforms each element via a mapper and appends the results to a destination buffer.
 *
 * For every source element, calls `mapper` to produce one destination-element-wide record and appends
 * it to `destination`. Existing contents of `destination` are preserved; the mapped elements are added
 * after them. The destination's element size need not match the list's element size (the mapper bridges them).
 * @param self The source list. Must not be NULL.
 * @param destination [out] Buffer the mapped elements are appended to. Must not be NULL. Must already be sized for destination-shaped elements.
 * @param mapper Transformation callback invoked once per source element. Must not be NULL.
 * @param userData Caller-attached context passed through to the mapper, or NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes, reused
 *        across calls with no per-call allocation. May be NULL to allocate and free internally.
 * @returns Success when all elements are mapped and appended. Raises ErrorCode_IllegalArgument if self,
 *          destination, or mapper is NULL, and ErrorCode_InvalidOperation if the destination buffer could
 *          not be grown to hold the mapped elements.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_Map(IList* self, GenericBuffer* destination, IListMapper mapper, const UserData* userData, void* scratch);

/**
 * @brief Maps the list into a destination buffer, allocating scratch internally.
 *
 * Equivalent to IList_Map with a self-managed scratch buffer; use only when you have no reusable
 * scratch buffer to supply.
 * @param self The source list. Must not be NULL.
 * @param destination [out] Buffer the mapped elements are appended to. Must not be NULL.
 * @param mapper Transformation callback invoked once per source element. Must not be NULL.
 * @param userData Caller-attached context passed through to the mapper, or NULL.
 * @returns Success when all elements are mapped and appended. Raises ErrorCode_IllegalArgument if self,
 *          destination, or mapper is NULL, and ErrorCode_InvalidOperation if the destination buffer could not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MapAllocating(IList* self, GenericBuffer* destination, IListMapper mapper, const UserData* userData);

/**
 * @brief Sums a signed-integer key extracted from every element.
 *
 * Calls `extractor` once per element and accumulates the returned int64 values. An empty list yields 0.
 * @param self The list. Must not be NULL.
 * @param extractor Callback producing an int64 key per element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the sum. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_SumInt(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue, void* scratch);

/**
 * @brief Sums a signed-integer key over the list, allocating scratch internally.
 *
 * Equivalent to IList_SumInt with a self-managed scratch buffer.
 * @param self The list. Must not be NULL.
 * @param extractor Callback producing an int64 key per element. Must not be NULL.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the sum. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_SumIntAllocating(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue);

/**
 * @brief Sums a floating-point key extracted from every element.
 *
 * Calls `extractor` once per element and accumulates the returned double values. An empty list yields 0.0.
 * @param self The list. Must not be NULL.
 * @param extractor Callback producing a double key per element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the sum. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_SumDouble(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue, void* scratch);

/**
 * @brief Sums a floating-point key over the list, allocating scratch internally.
 *
 * Equivalent to IList_SumDouble with a self-managed scratch buffer.
 * @param self The list. Must not be NULL.
 * @param extractor Callback producing a double key per element. Must not be NULL.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the sum. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_SumDoubleAllocating(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue);

/**
 * @brief Finds the maximum of a signed-integer key extracted from the elements.
 *
 * Calls `extractor` once per element and returns the largest int64 value.
 * @param self The list. Must not be NULL and must not be empty.
 * @param extractor Callback producing an int64 key per element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the maximum value. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or
 *          outValue is NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MaxInt(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue, void* scratch);

/**
 * @brief Finds the maximum of a signed-integer key, allocating scratch internally.
 *
 * Equivalent to IList_MaxInt with a self-managed scratch buffer.
 * @param self The list. Must not be NULL and must not be empty.
 * @param extractor Callback producing an int64 key per element. Must not be NULL.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the maximum value. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or
 *          outValue is NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MaxIntAllocating(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue);

/**
 * @brief Finds the maximum of a floating-point key extracted from the elements.
 *
 * Calls `extractor` once per element and returns the largest double value.
 * @param self The list. Must not be NULL and must not be empty.
 * @param extractor Callback producing a double key per element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the maximum value. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or
 *          outValue is NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MaxDouble(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue, void* scratch);

/**
 * @brief Finds the maximum of a floating-point key, allocating scratch internally.
 *
 * Equivalent to IList_MaxDouble with a self-managed scratch buffer.
 * @param self The list. Must not be NULL and must not be empty.
 * @param extractor Callback producing a double key per element. Must not be NULL.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the maximum value. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or
 *          outValue is NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MaxDoubleAllocating(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue);

/**
 * @brief Finds the minimum of a signed-integer key extracted from the elements.
 *
 * Calls `extractor` once per element and returns the smallest int64 value.
 * @param self The list. Must not be NULL and must not be empty.
 * @param extractor Callback producing an int64 key per element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the minimum value. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or
 *          outValue is NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MinInt(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue, void* scratch);

/**
 * @brief Finds the minimum of a signed-integer key, allocating scratch internally.
 *
 * Equivalent to IList_MinInt with a self-managed scratch buffer.
 * @param self The list. Must not be NULL and must not be empty.
 * @param extractor Callback producing an int64 key per element. Must not be NULL.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the minimum value. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or
 *          outValue is NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MinIntAllocating(IList* self, IListIntExtractor extractor, const UserData* userData, int64_t* outValue);

/**
 * @brief Finds the minimum of a floating-point key extracted from the elements.
 *
 * Calls `extractor` once per element and returns the smallest double value.
 * @param self The list. Must not be NULL and must not be empty.
 * @param extractor Callback producing a double key per element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the minimum value. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or
 *          outValue is NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MinDouble(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue, void* scratch);

/**
 * @brief Finds the minimum of a floating-point key, allocating scratch internally.
 *
 * Equivalent to IList_MinDouble with a self-managed scratch buffer.
 * @param self The list. Must not be NULL and must not be empty.
 * @param extractor Callback producing a double key per element. Must not be NULL.
 * @param userData Caller-attached context passed through to the extractor, or NULL.
 * @param outValue [out] Receives the minimum value. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, extractor, or
 *          outValue is NULL, and ErrorCode_InvalidOperation if the list is empty.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_MinDoubleAllocating(IList* self, IListDoubleExtractor extractor, const UserData* userData, double* outValue);

/**
 * @brief Reports whether any element satisfies a predicate.
 *
 * Scans from the front and stops at the first match.
 * @param self The list. Must not be NULL.
 * @param predicate Callback returning true for a matching element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param outValue [out] Receives true if at least one element matches, false otherwise. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, predicate, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_Contains(IList* self, IListPredicate predicate, const UserData* userData, bool* outValue, void* scratch);

/**
 * @brief Reports whether any element satisfies a predicate, allocating scratch internally.
 *
 * Equivalent to IList_Contains with a self-managed scratch buffer.
 * @param self The list. Must not be NULL.
 * @param predicate Callback returning true for a matching element. Must not be NULL.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param outValue [out] Receives true if at least one element matches, false otherwise. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, predicate, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_ContainsAllocating(IList* self, IListPredicate predicate, const UserData* userData, bool* outValue);

/**
 * @brief Counts how many elements satisfy a predicate.
 *
 * @param self The list. Must not be NULL.
 * @param predicate Callback returning true for elements to count. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param outValue [out] Receives the number of matching elements. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, predicate, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_CountWhere(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue, void* scratch);

/**
 * @brief Counts how many elements satisfy a predicate, allocating scratch internally.
 *
 * Equivalent to IList_CountWhere with a self-managed scratch buffer.
 * @param self The list. Must not be NULL.
 * @param predicate Callback returning true for elements to count. Must not be NULL.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param outValue [out] Receives the number of matching elements. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, predicate, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_CountWhereAllocating(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue);

/**
 * @brief Finds the index of the first element satisfying a predicate.
 *
 * Scans from the front and stops at the first match.
 * @param self The list. Must not be NULL.
 * @param predicate Callback returning true for a matching element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param outValue [out] Receives the zero-based index of the first match, or LIST_INDEX_INVALID if none matches. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, predicate, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_FirstIndexOf(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue, void* scratch);

/**
 * @brief Finds the index of the first matching element, allocating scratch internally.
 *
 * Equivalent to IList_FirstIndexOf with a self-managed scratch buffer.
 * @param self The list. Must not be NULL.
 * @param predicate Callback returning true for a matching element. Must not be NULL.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param outValue [out] Receives the zero-based index of the first match, or LIST_INDEX_INVALID if none matches. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, predicate, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_FirstIndexOfAllocating(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue);

/**
 * @brief Finds the index of the last element satisfying a predicate.
 *
 * Scans from the back and stops at the first match encountered (the highest matching index).
 * @param self The list. Must not be NULL.
 * @param predicate Callback returning true for a matching element. Must not be NULL; it must not mutate the list.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param outValue [out] Receives the zero-based index of the last match, or LIST_INDEX_INVALID if none matches. Must not be NULL.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, predicate, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_LastIndexOf(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue, void* scratch);

/**
 * @brief Finds the index of the last matching element, allocating scratch internally.
 *
 * Equivalent to IList_LastIndexOf with a self-managed scratch buffer.
 * @param self The list. Must not be NULL.
 * @param predicate Callback returning true for a matching element. Must not be NULL.
 * @param userData Caller-attached context passed through to the predicate, or NULL.
 * @param outValue [out] Receives the zero-based index of the last match, or LIST_INDEX_INVALID if none matches. Must not be NULL.
 * @returns Success after writing outValue. Raises ErrorCode_IllegalArgument if self, predicate, or outValue is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_LastIndexOfAllocating(IList* self, IListPredicate predicate, const UserData* userData, size_t* outValue);

/**
 * @brief Reverses the order of the list's elements in place.
 *
 * Swaps elements end-for-end. A list with fewer than two elements is left unchanged and succeeds trivially.
 * @param self The list. Must not be NULL and must not be read-only.
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes (this
 *        operation uses two elements' worth). May be NULL to allocate and free internally.
 * @returns Success when reversed. Raises ErrorCode_IllegalArgument if self is NULL, and
 *          ErrorCode_InvalidOperation if the list is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_Reverse(IList* self, void* scratch);

/**
 * @brief Reverses the list in place, allocating scratch internally.
 *
 * Equivalent to IList_Reverse with a self-managed scratch buffer; use only when you have no reusable
 * scratch buffer to supply.
 * @param self The list. Must not be NULL and must not be read-only.
 * @returns Success when reversed. Raises ErrorCode_IllegalArgument if self is NULL, and
 *          ErrorCode_InvalidOperation if the list is read-only.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_ReverseAllocating(IList* self);

/**
 * @brief Copies every element of the list to the end of a destination buffer.
 *
 * Appends element-for-element (raw byte copy) after the destination's existing contents, which are
 * preserved. The destination's element size must equal this list's element size.
 * @param self The list. Must not be NULL.
 * @param destination [out] Buffer the elements are appended to. Must not be NULL and its element size must match IList_GetElementSize(self).
 * @param scratch Caller-owned scratch buffer of at least IList_GetScratchSize(self) bytes; may be NULL to allocate internally.
 * @returns Success when all elements are appended. Raises ErrorCode_IllegalArgument if self or
 *          destination is NULL, or if the destination's element size differs from the list's; and
 *          ErrorCode_InvalidOperation if the destination buffer could not be grown to hold the elements.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_CopyTo(IList* self, GenericBuffer* destination, void* scratch);

/**
 * @brief Copies every element of the list into a destination buffer, allocating scratch internally.
 *
 * Equivalent to IList_CopyTo with a self-managed scratch buffer.
 * @param self The list. Must not be NULL.
 * @param destination [out] Buffer the elements are appended to. Must not be NULL and its element size must match IList_GetElementSize(self).
 * @returns Success when all elements are appended. Raises ErrorCode_IllegalArgument if self or
 *          destination is NULL, or if the destination's element size differs from the list's; and
 *          ErrorCode_InvalidOperation if the destination buffer could not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error IList_CopyToAllocating(IList* self, GenericBuffer* destination);


/**
 * @brief Releases the resources owned by the list's underlying implementation.
 *
 * Dispatches to the vtable's deconstruct slot. After this call the implementation's resources are
 * freed; the IList itself is not freed (it is typically embedded in an owning object).
 * @param self The list. Must not be NULL.
 * @returns Success once the implementation has been released. Raises ErrorCode_IllegalArgument if self is NULL.
 */
Error IList_Deconstruct(IList* self);
