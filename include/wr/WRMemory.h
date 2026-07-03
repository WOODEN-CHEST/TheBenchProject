#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRComparator.h"
#include "WRUserData.h"


/**
 * @brief Sentinel index meaning "no such element / not found".
 *
 * Equal to (size_t)-1 (the maximum size_t value). Index-returning search functions
 * (GenericBuffer_FirstIndexOf / GenericBuffer_LastIndexOf) return this when no element
 * matches; it can never be a valid element index because no buffer can hold SIZE_MAX elements.
 */
#define GENERIC_BUFFER_INDEX_INVALID (~((size_t)0))


// Types.
/**
 * @brief A growable, element-typed byte buffer with validated, optionally bounded mutation.
 *
 * Opaque forward declaration; the full layout is GenericBufferStruct below. A GenericBuffer
 * stores a contiguous array of fixed-size elements together with a capacity, a live element
 * count, the per-element size, and flags that decide whether it may grow and whether it may be
 * mutated at all. Depending on how it is constructed it can own a heap allocation that grows on
 * demand, wrap caller-provided storage that is writable but fixed in capacity, or wrap
 * caller-provided storage as an immutable read-only view.
 */
typedef struct GenericBufferStruct GenericBuffer;

/**
 * @brief Growth hook invoked when a growable buffer needs more capacity.
 *
 * Called by the buffer (via GenericBuffer_EnsureTotalCapacity) when the current capacity is
 * smaller than a requested capacity. A conforming implementation must enlarge @p destination so
 * that its `_capacity` becomes at least @p requestedCapacity, updating `_data` and `_capacity`
 * accordingly, and return true on success. It must return false (leaving the buffer usable and
 * its existing contents intact) if it cannot satisfy the request. The default implementation
 * grows geometrically via Memory_Reallocate.
 * @param destination The buffer to enlarge; never NULL. Its `_data`/`_capacity` may be updated.
 * @param requestedCapacity The minimum required capacity, in ELEMENTS.
 * @returns true if the buffer now has at least @p requestedCapacity capacity; false on failure.
 */
typedef bool (*GenericBufferAllocateCallback)(GenericBuffer* destination, size_t requestedCapacity);

/**
 * @brief Bit flags describing a buffer's mutation and growth policy.
 *
 * Stored in GenericBufferStruct::_flags and combined with bitwise OR.
 */
typedef enum GenericBufferFlagsEnum
{
    /** @brief No restrictions: the buffer is mutable and may grow via its allocate callback. */
    GenericBufferFlags_None = 0,
    /** @brief The buffer may not grow; its element count can still change up to its fixed capacity. */
    GenericBufferFlags_FixedCapacity = (1 << 0),
    /** @brief The buffer rejects every mutating operation; only reads and pointer access are allowed. */
    GenericBufferFlags_ReadOnly = (1 << 1),
} GenericBufferFlags;

/**
 * @brief Concrete layout of a GenericBuffer: storage plus its element/capacity bookkeeping and policy.
 *
 * @note `capacity` and `count` are measured in ELEMENTS, not bytes; the byte size of an element
 *       is `_elementSize`. Underscore-prefixed fields are read-only to code outside this module;
 *       in particular `_count` must never be assigned directly (use GenericBuffer_SetCount /
 *       GenericBuffer_CommitCount), and `_data`/`_capacity` are maintained by the allocate callback.
 */
struct GenericBufferStruct
{
    /** @brief Pointer to the contiguous element storage (capacity * _elementSize bytes), or NULL when empty/unallocated. */
    unsigned char* _data;
    /** @brief Number of elements the storage can hold before it must grow. In ELEMENTS, not bytes. */
    size_t _capacity;
    /** @brief Number of elements currently present (always <= _capacity). In ELEMENTS, not bytes. */
    size_t _count;
    /** @brief Size in bytes of one element; all elements have this size. Must be > 0 for growth. */
    size_t _elementSize;
    /** @brief Opaque context pointer passed back to the allocate callback; managed via Set/ClearCallback. */
    void* _userData;
    /** @brief Hook used to grow the buffer when more capacity is needed; NULL means the buffer cannot grow. */
    GenericBufferAllocateCallback _requestMoreSpaceCallback;
    /** @brief Mutation/growth policy flags (read-only, fixed-capacity). */
    GenericBufferFlags _flags;
};

/**
 * @brief An element handed to a callback: its in-place address together with its index.
 *
 * Passed by value to predicates, comparators, mappers, and extractors so a callback can both
 * read/modify the element through `_element` and know its position via `_index`.
 */
typedef struct GenericBufferElementDataStruct
{
    /** @brief Pointer to the element's bytes inside the buffer's storage (`_elementSize` bytes wide). */
    void* _element;
    /** @brief Zero-based index of this element within the buffer. */
    size_t _index;
} GenericBufferElementData;

/**
 * @brief Caller-supplied test applied to a single element.
 *
 * Used by Contains, FirstIndexOf, LastIndexOf, Filter, and CountWhere to decide whether an
 * element matches. Must not change the buffer's structure (count/capacity); reading or rewriting
 * the element's bytes in place is allowed.
 * @param buffer The buffer being iterated.
 * @param element The current element (in-place address and index).
 * @param userData Caller context, or NULL if none was supplied. Received as a const pointer.
 * @returns true if the element satisfies the predicate, false otherwise.
 */
typedef bool (*GenericBufferPredicate)(GenericBuffer* buffer, GenericBufferElementData element, const UserData* userData);

/**
 * @brief Caller-supplied three-way ordering of two elements, used by the sort operations.
 *
 * @param buffer The buffer being sorted.
 * @param a The left element (in-place address and index).
 * @param b The right element (in-place address and index).
 * @param userData Caller context, or NULL. Received as a const pointer.
 * @returns ComparisonResult_ALessThanB, ComparisonResult_AEqualsB, or ComparisonResult_AGreaterThanB
 *          describing the order of @p a relative to @p b. Sort direction (ascending/descending) is
 *          chosen by the calling sort function, not by the comparator.
 */
typedef ComparisonResult (*GenericBufferComparator)(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, const UserData* userData);

/**
 * @brief Caller-supplied transform that writes a derived element into a destination buffer.
 *
 * Invoked by GenericBuffer_Map once per source element. The implementation must write exactly one
 * destination element's worth of bytes to @p destinationElement (its element size, which may
 * differ from the source's). The destination slot is uninitialized on entry.
 * @param buffer The source buffer.
 * @param sourceElement The source element (in-place address and index).
 * @param destinationElement [out] Address of the destination slot to populate.
 * @param userData Caller context, or NULL. Received as a const pointer.
 */
typedef void (*GenericBufferMapper)(GenericBuffer* buffer, GenericBufferElementData sourceElement, void* destinationElement, const UserData* userData);

/**
 * @brief Caller-supplied projection from an element to a signed 64-bit integer.
 *
 * Used by the integer Sum/Min/Max reductions to obtain a numeric value from each element.
 * @param buffer The buffer being reduced.
 * @param sourceElement The current element (in-place address and index).
 * @param userData Caller context, or NULL. Received as a const pointer.
 * @returns The integer value extracted from the element.
 */
typedef int64_t (*GenericBufferIntExtractor)(GenericBuffer* buffer, GenericBufferElementData sourceElement, const UserData* userData);

/**
 * @brief Caller-supplied projection from an element to a double.
 *
 * Used by the floating-point Sum/Min/Max reductions to obtain a numeric value from each element.
 * @param buffer The buffer being reduced.
 * @param sourceElement The current element (in-place address and index).
 * @param userData Caller context, or NULL. Received as a const pointer.
 * @returns The double value extracted from the element.
 */
typedef double (*GenericBufferDoubleExtractor)(GenericBuffer* buffer, GenericBufferElementData sourceElement, const UserData* userData);


// Functions.
/**
 * @brief Initializes a growable buffer over caller-provided storage and growth policy.
 *
 * Sets up @p buffer as a variable (growable, mutable) buffer that initially wraps @p destination
 * with the given capacity, element size, and live element count, and uses @p callback to obtain
 * more space when needed. No allocation is performed here; the caller owns @p destination and the
 * growth strategy. To instead have the buffer allocate and own a default growable heap block, use
 * GenericBuffer_AllocateVariable.
 * @param buffer [out] The buffer object to initialize; never NULL.
 * @param destination Initial element storage of at least @p bufferCapacity * @p elementSize bytes,
 *        or NULL to start empty and rely on @p callback to allocate.
 * @param bufferCapacity Initial capacity in ELEMENTS that @p destination can hold.
 * @param elementSize Size in bytes of one element; must be > 0 for the buffer to grow.
 * @param elementCount Number of elements already present in @p destination (<= @p bufferCapacity).
 * @param userData Opaque context passed to @p callback on each growth; may be NULL.
 * @param callback Growth hook used to enlarge the buffer; NULL means the buffer cannot grow.
 */
void GenericBuffer_CreateVariable(GenericBuffer* buffer, 
    void* destination,
    size_t bufferCapacity,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback);

/**
 * @brief Initializes a growable buffer that owns a freshly allocated heap block.
 *
 * Creates an empty (count 0), mutable, growable buffer wired to the built-in geometric-growth
 * callback. If @p initialCapacity and @p elementSize are both nonzero and their product does not
 * overflow, an initial block of @p initialCapacity * @p elementSize bytes is allocated; otherwise
 * the buffer starts with NULL storage and zero capacity and will allocate on first growth. The
 * allocation never returns NULL on failure (it aborts the process). The owned block is later grown
 * via Memory_Reallocate and must ultimately be released with Memory_Free on `_data`.
 * @param buffer [out] The buffer object to initialize; never NULL.
 * @param initialCapacity Desired initial capacity in ELEMENTS; 0 starts with no allocation.
 * @param elementSize Size in bytes of one element. 0 disables allocation and growth.
 */
void GenericBuffer_AllocateVariable(GenericBuffer* buffer, 
     size_t initialCapacity,
     size_t elementSize);

/**
 * @brief Initializes a fixed-capacity, in-place-writable buffer over existing storage.
 *
 * The buffer wraps @p destination and may be mutated (add/insert/replace/remove up to its
 * capacity) but can never grow: any operation that would exceed @p bufferCapacity is rejected. No
 * allocate callback is attached. For a buffer that also rejects all mutation, use
 * GenericBuffer_CreateReadOnly.
 * @param buffer [out] The buffer object to initialize; never NULL.
 * @param destination Element storage of at least @p bufferCapacity * @p elementSize bytes; owned by the caller.
 * @param bufferCapacity Fixed capacity in ELEMENTS.
 * @param elementSize Size in bytes of one element.
 * @param elementCount Number of elements already present (<= @p bufferCapacity).
 */
void GenericBuffer_CreateConstant(GenericBuffer* buffer, void* destination, size_t bufferCapacity, size_t elementSize, size_t elementCount);

/**
 * @brief Initializes a read-only, fixed-capacity view over existing storage.
 *
 * The buffer wraps @p destination and rejects every mutating operation (add/insert/replace/remove/
 * clear/sort/reverse/filter/map-into/append/set-count all return false); reading values and
 * obtaining element pointers remain valid. The caller owns @p destination.
 * @param buffer [out] The buffer object to initialize; never NULL.
 * @param destination Element storage of at least @p bufferCapacity * @p elementSize bytes; owned by the caller.
 * @param bufferCapacity Capacity in ELEMENTS of the viewed storage.
 * @param elementSize Size in bytes of one element.
 * @param elementCount Number of valid elements in the view (<= @p bufferCapacity).
 */
void GenericBuffer_CreateReadOnly(GenericBuffer* buffer, void* destination, size_t bufferCapacity, size_t elementSize, size_t elementCount);

/**
 * @brief Sets (or replaces) the buffer's growth callback and its associated context.
 *
 * Overwrites both `_requestMoreSpaceCallback` and `_userData`. Use this to attach a custom growth
 * strategy or to change the context handed to it. Does not validate flags; a fixed-capacity buffer
 * still will not grow regardless of the callback.
 * @param buffer The buffer to configure; never NULL.
 * @param callback The new growth hook, or NULL to make the buffer non-growable.
 * @param userData Opaque context passed to @p callback; may be NULL.
 */
void GenericBuffer_SetCallback(GenericBuffer* buffer, GenericBufferAllocateCallback callback, void* userData);

/**
 * @brief Detaches the growth callback and clears its context.
 *
 * Sets `_requestMoreSpaceCallback` and `_userData` to NULL, after which the buffer can no longer
 * grow (capacity-increasing operations will fail) until a new callback is set.
 * @param buffer The buffer to modify; never NULL.
 */
void GenericBuffer_ClearCallback(GenericBuffer* buffer);

/**
 * @brief Ensures the buffer's total capacity is at least @p requiredSize elements, growing if needed.
 *
 * If the current capacity already meets @p requiredSize, succeeds without doing anything. Otherwise
 * the buffer must be growable: it must not be fixed-capacity and must have a growth callback, which
 * is invoked to enlarge it. @p requiredSize is an ABSOLUTE total capacity (in elements), not an
 * increment; to reserve room relative to the current count use GenericBuffer_ReserveMoreCapacity.
 * @param buffer The buffer to grow; may be NULL (returns false).
 * @param requiredSize The minimum total capacity required, in ELEMENTS.
 * @returns true if the capacity is now at least @p requiredSize; false if @p buffer is NULL, the
 *          buffer is fixed-capacity, it has no growth callback, or the callback failed to provide
 *          enough capacity.
 */
bool GenericBuffer_EnsureTotalCapacity(GenericBuffer* buffer, size_t requiredSize);

/**
 * @brief Ensures there is room for @p requiredSize MORE elements beyond the current count.
 *
 * Equivalent to ensuring a total capacity of `count + requiredSize`. Used before appends and
 * manual writes to guarantee free space at the tail.
 * @param buffer The buffer to grow; may be NULL (returns false).
 * @param requiredSize Number of additional elements to make room for, beyond the current count.
 * @returns true if at least @p requiredSize free element slots are available; false if @p buffer is
 *          NULL, the addition `count + requiredSize` would overflow size_t, or growth failed (e.g.
 *          fixed-capacity, no callback, allocation failure).
 */
bool GenericBuffer_ReserveMoreCapacity(GenericBuffer* buffer, size_t requiredSize);

/**
 * @brief Returns the number of unused element slots (capacity minus count).
 *
 * @param buffer The buffer to query; may be NULL (returns 0).
 * @returns The count of free element slots currently available without growing, or 0 if @p buffer is NULL.
 */
size_t GenericBuffer_GetCapacityRemaining(GenericBuffer* buffer);

/**
 * @brief Reports whether the buffer rejects all mutation (read-only).
 *
 * @param buffer The buffer to query; must not be NULL (the flags are dereferenced).
 * @returns true if the read-only flag is set, false otherwise.
 */
static inline bool GenericBuffer_IsReadOnly(GenericBuffer* buffer)
{
    return buffer->_flags & GenericBufferFlags_ReadOnly;
}

/**
 * @brief Reports whether the buffer cannot grow (fixed capacity).
 *
 * A fixed-capacity buffer may still be mutated in place (unless it is also read-only); it simply
 * cannot increase its capacity.
 * @param buffer The buffer to query; must not be NULL (the flags are dereferenced).
 * @returns true if the fixed-capacity flag is set, false otherwise.
 */
static inline bool GenericBuffer_IsFixedCapacity(GenericBuffer* buffer)
{
    return buffer->_flags & GenericBufferFlags_FixedCapacity;
}



/**
 * @brief Appends one element to the end of the buffer.
 *
 * Copies @p elementSize bytes from @p item into a new slot after the last element, growing the
 * buffer if necessary. @p item may alias storage inside the buffer (this is handled safely).
 * @param buffer The buffer to append to; may be NULL (returns false).
 * @param item Pointer to the element value to copy in; must not be NULL.
 * @returns true on success; false if @p buffer or @p item is NULL, the buffer is read-only, or it
 *          could not be grown to hold one more element (fixed-capacity overflow or allocation failure).
 */
bool GenericBuffer_AddLast(GenericBuffer* buffer, void* item);

/**
 * @brief Inserts one element at the front of the buffer, shifting existing elements right.
 *
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @param item Pointer to the element value to copy in; must not be NULL. May alias buffer storage.
 * @returns true on success; false if @p buffer or @p item is NULL, the buffer is read-only, or it
 *          could not be grown to hold one more element.
 */
bool GenericBuffer_AddFirst(GenericBuffer* buffer, void* item);

/**
 * @brief Inserts one element at @p index, shifting elements at and after @p index right by one.
 *
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @param item Pointer to the element value to copy in; must not be NULL. May alias buffer storage
 *        (the source is relocated correctly even if a reallocation occurs).
 * @param index Insertion position in [0, count]; equal to count appends at the end.
 * @returns true on success; false if @p buffer or @p item is NULL, @p index > count, the buffer is
 *          read-only, or it could not be grown to hold one more element.
 */
bool GenericBuffer_Insert(GenericBuffer* buffer, void* item, size_t index);

/**
 * @brief Overwrites the element at @p index with a new value (no shifting, no growth).
 *
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @param item Pointer to the replacement element value to copy in; must not be NULL.
 * @param index Index of an existing element; must be in [0, count).
 * @returns true on success; false if @p buffer or @p item is NULL, the buffer is read-only, or
 *          @p index is out of range.
 */
bool GenericBuffer_Replace(GenericBuffer* buffer, void* item, size_t index);

/**
 * @brief Removes the element at @p index, shifting later elements left to close the gap.
 *
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @param index Index of the element to remove; must be in [0, count).
 * @returns true on success; false if @p buffer is NULL, @p index is out of range, or the buffer is read-only.
 */
bool GenericBuffer_RemoveAt(GenericBuffer* buffer, size_t index);

/**
 * @brief Removes the first element, shifting the remaining elements left.
 *
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @returns true on success; false if @p buffer is NULL, the buffer is empty, or it is read-only.
 */
bool GenericBuffer_RemoveFirst(GenericBuffer* buffer);

/**
 * @brief Removes the last element.
 *
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @returns true on success; false if @p buffer is NULL, the buffer is empty, or it is read-only.
 */
bool GenericBuffer_RemoveLast(GenericBuffer* buffer);

/**
 * @brief Appends @p itemCount contiguous elements to the end of the buffer.
 *
 * @param buffer The buffer to append to; may be NULL (returns false).
 * @param items Pointer to @p itemCount contiguous elements to copy. May alias buffer storage.
 *        Ignored when @p itemCount is 0.
 * @param itemCount Number of elements to append; 0 is a successful no-op.
 * @returns true on success; false if @p buffer is NULL, @p items is NULL while @p itemCount > 0,
 *          the buffer is read-only, or it could not be grown to hold the new elements.
 */
bool GenericBuffer_AddLastRange(GenericBuffer* buffer, void* items, size_t itemCount);

/**
 * @brief Inserts @p itemCount contiguous elements at the front, shifting existing elements right.
 *
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @param items Pointer to @p itemCount contiguous elements to copy. May alias buffer storage.
 *        Ignored when @p itemCount is 0.
 * @param itemCount Number of elements to insert; 0 is a successful no-op.
 * @returns true on success; false if @p buffer is NULL, @p items is NULL while @p itemCount > 0,
 *          the buffer is read-only, or it could not be grown.
 */
bool GenericBuffer_AddFirstRange(GenericBuffer* buffer, void* items, size_t itemCount);

/**
 * @brief Inserts @p itemCount contiguous elements at @p index, shifting elements at/after it right.
 *
 * Safe even when @p items points into this buffer's own storage: aliasing is detected before any
 * reallocation and the source bytes are relocated correctly after the gap is opened.
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @param items Pointer to @p itemCount contiguous elements to copy. May alias buffer storage.
 *        Ignored when @p itemCount is 0.
 * @param itemCount Number of elements to insert; 0 is a successful no-op.
 * @param index Insertion position in [0, count]; equal to count appends at the end.
 * @returns true on success; false if @p buffer is NULL, @p items is NULL while @p itemCount > 0,
 *          @p index > count, the buffer is read-only, or it could not be grown.
 */
bool GenericBuffer_InsertRange(GenericBuffer* buffer, void* items, size_t itemCount, size_t index);

/**
 * @brief Overwrites @p itemCount existing elements starting at @p index (no shifting, no growth).
 *
 * The range [@p index, @p index + @p itemCount) must lie entirely within the current elements.
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @param items Pointer to @p itemCount contiguous replacement elements to copy. Ignored when
 *        @p itemCount is 0.
 * @param itemCount Number of elements to overwrite; 0 is a successful no-op.
 * @param index Start index of the region to overwrite; in [0, count].
 * @returns true on success; false if @p buffer is NULL, @p items is NULL while @p itemCount > 0,
 *          the buffer is read-only, @p index > count, or the region would extend past the current count.
 */
bool GenericBuffer_ReplaceRange(GenericBuffer* buffer, void* items, size_t itemCount, size_t index);

/**
 * @brief Removes the half-open range [@p startIndexInclusive, @p endIndexExclusive), closing the gap.
 *
 * Elements after the removed range shift left to fill it; the count drops by the range length.
 * @param buffer The buffer to modify; may be NULL (returns false).
 * @param startIndexInclusive First index to remove.
 * @param endIndexExclusive One past the last index to remove; must be >= @p startIndexInclusive and <= count.
 * @returns true on success (including the empty range start == end, which removes nothing); false if
 *          @p buffer is NULL, the buffer is read-only, @p startIndexInclusive > @p endIndexExclusive,
 *          or @p endIndexExclusive > count.
 */
bool GenericBuffer_RemoveRange(GenericBuffer* buffer, size_t startIndexInclusive, size_t endIndexExclusive);


/**
 * @brief Returns a direct pointer to the element at @p index inside the buffer's storage.
 *
 * The returned pointer aliases live storage and is invalidated by any operation that grows or
 * relocates the buffer (inserts, appends, capacity changes). Do not free it.
 * @param buffer The buffer to query; may be NULL (returns NULL).
 * @param index Index of an existing element; must be in [0, count).
 * @returns A pointer to the element's bytes, or NULL if @p buffer is NULL or @p index is out of range.
 */
void* GenericBuffer_GetPointerToElement(GenericBuffer* buffer, size_t index);

/**
 * @brief Returns a direct pointer to the first element.
 *
 * @param buffer The buffer to query; may be NULL (returns NULL).
 * @returns A pointer to the first element, or NULL if @p buffer is NULL or the buffer is empty.
 * @note The pointer is invalidated by operations that grow or relocate the buffer.
 */
void* GenericBuffer_GetPointerToFirst(GenericBuffer* buffer);

/**
 * @brief Returns a direct pointer to the last element.
 *
 * @param buffer The buffer to query; may be NULL (returns NULL).
 * @returns A pointer to the last element, or NULL if @p buffer is NULL or the buffer is empty.
 * @note The pointer is invalidated by operations that grow or relocate the buffer.
 */
void* GenericBuffer_GetPointerToLast(GenericBuffer* buffer);

/**
 * @brief Copies the element at @p index out into caller-provided storage.
 *
 * @param buffer The buffer to read from; may be NULL (returns false).
 * @param index Index of an existing element; must be in [0, count).
 * @param out [out] Destination of at least @p elementSize bytes; must not be NULL.
 * @returns true on success; false if @p buffer or @p out is NULL or @p index is out of range.
 */
bool GenericBuffer_GetAt(GenericBuffer* buffer, size_t index, void* out);

/**
 * @brief Copies the first element out into caller-provided storage.
 *
 * @param buffer The buffer to read from; may be NULL (returns false).
 * @param out [out] Destination of at least @p elementSize bytes; must not be NULL.
 * @returns true on success; false if @p buffer or @p out is NULL or the buffer is empty.
 */
bool GenericBuffer_GetFirst(GenericBuffer* buffer, void* out);

/**
 * @brief Copies the last element out into caller-provided storage.
 *
 * @param buffer The buffer to read from; may be NULL (returns false).
 * @param out [out] Destination of at least @p elementSize bytes; must not be NULL.
 * @returns true on success; false if @p buffer or @p out is NULL or the buffer is empty.
 */
bool GenericBuffer_GetLast(GenericBuffer* buffer, void* out);

/**
 * @brief Empties the buffer by setting its element count to zero.
 *
 * Capacity and storage are left unchanged; existing element bytes are not cleared, only logically
 * discarded.
 * @param buffer The buffer to clear; may be NULL (returns false).
 * @returns true on success; false if @p buffer is NULL or the buffer is read-only.
 */
bool GenericBuffer_Clear(GenericBuffer* buffer);


/**
 * @brief Reports whether any element satisfies @p predicate.
 *
 * @param buffer The buffer to search; may be NULL (returns false).
 * @param predicate Test applied to each element until one matches; must not be NULL (NULL returns false).
 * @param userData Caller context passed to @p predicate, or NULL.
 * @returns true if at least one element matches, false otherwise (including invalid arguments).
 */
bool GenericBuffer_Contains(GenericBuffer* buffer, GenericBufferPredicate predicate, const UserData* userData);

/**
 * @brief Returns the index of the first (lowest-index) element satisfying @p predicate.
 *
 * @param buffer The buffer to search; may be NULL.
 * @param predicate Test applied to each element from front to back; must not be NULL.
 * @param userData Caller context passed to @p predicate, or NULL.
 * @returns The zero-based index of the first match, or GENERIC_BUFFER_INDEX_INVALID if none matches
 *          or if @p buffer or @p predicate is NULL.
 */
size_t GenericBuffer_FirstIndexOf(GenericBuffer* buffer, GenericBufferPredicate predicate, const UserData* userData);

/**
 * @brief Returns the index of the last (highest-index) element satisfying @p predicate.
 *
 * @param buffer The buffer to search; may be NULL.
 * @param predicate Test applied to each element from back to front; must not be NULL.
 * @param userData Caller context passed to @p predicate, or NULL.
 * @returns The zero-based index of the last match, or GENERIC_BUFFER_INDEX_INVALID if none matches
 *          or if @p buffer or @p predicate is NULL.
 */
size_t GenericBuffer_LastIndexOf(GenericBuffer* buffer, GenericBufferPredicate predicate, const UserData* userData);

/**
 * @brief Returns the number of scratch bytes required by the plain Sort/Reverse operations.
 *
 * The plain (non-allocating) sort and reverse functions take a caller-owned scratch buffer; this
 * returns the minimum size, in bytes, it must have. Allocate at least this many bytes (sized for
 * the largest element size in use) and reuse the same buffer across calls to avoid per-call
 * allocation.
 * @param buffer The buffer whose element size determines the requirement; must not be NULL.
 * @returns The required scratch size in bytes (two elements' worth).
 */
static inline size_t GenericBuffer_GetSortScratchSize(GenericBuffer* buffer)
{
    return buffer->_elementSize * 2;
}

/**
 * @brief Reverses the order of the buffer's elements in place.
 *
 * Uses one element of scratch space to swap pairs. Pass a caller-owned scratch buffer of at least
 * GenericBuffer_GetSortScratchSize(buffer) bytes (reuse it to avoid per-call allocation), or NULL
 * to have the function allocate and free scratch internally.
 * @param buffer The buffer to reverse; may be NULL (returns false).
 * @param scratch Caller-owned scratch of at least GenericBuffer_GetSortScratchSize(buffer) bytes,
 *        or NULL to allocate internally.
 * @returns true on success; false if @p buffer is NULL, it has fewer than 2 elements, or it is read-only.
 */
bool GenericBuffer_Reverse(GenericBuffer* buffer, void* scratch);

/**
 * @brief Reverses the buffer in place, always allocating and freeing its own scratch.
 *
 * Convenience wrapper over GenericBuffer_Reverse with NULL scratch.
 * @param buffer The buffer to reverse; may be NULL (returns false).
 * @returns true on success; false if @p buffer is NULL, it has fewer than 2 elements, or it is read-only.
 */
bool GenericBuffer_ReverseAllocating(GenericBuffer* buffer);


/**
 * @brief Sorts the buffer in place into ascending order as defined by @p comparator.
 *
 * Ordering is not stable. Pass a caller-owned scratch buffer of at least
 * GenericBuffer_GetSortScratchSize(buffer) bytes (reuse it to avoid per-call allocation), or NULL
 * to allocate internally. A buffer with fewer than 2 elements is already sorted (succeeds, no-op).
 * @param buffer The buffer to sort; may be NULL (returns false).
 * @param comparator Three-way element ordering; must not be NULL.
 * @param userData Caller context passed to @p comparator, or NULL.
 * @param scratch Caller-owned scratch of at least GenericBuffer_GetSortScratchSize(buffer) bytes,
 *        or NULL to allocate internally.
 * @returns true on success; false if @p buffer or @p comparator is NULL, the buffer is read-only,
 *          or (with NULL scratch) the internal scratch allocation could not be sized.
 */
bool GenericBuffer_SortAscending(GenericBuffer* buffer, GenericBufferComparator comparator, const UserData* userData, void* scratch);

/**
 * @brief Sorts the buffer ascending, always allocating and freeing its own scratch.
 *
 * Convenience wrapper over GenericBuffer_SortAscending with NULL scratch.
 * @param buffer The buffer to sort; may be NULL (returns false).
 * @param comparator Three-way element ordering; must not be NULL.
 * @param userData Caller context passed to @p comparator, or NULL.
 * @returns true on success; false if @p buffer or @p comparator is NULL or the buffer is read-only.
 */
bool GenericBuffer_SortAscendingAllocating(GenericBuffer* buffer, GenericBufferComparator comparator, const UserData* userData);

/**
 * @brief Sorts the buffer in place into descending order as defined by @p comparator.
 *
 * Ordering is not stable. Pass a caller-owned scratch buffer of at least
 * GenericBuffer_GetSortScratchSize(buffer) bytes, or NULL to allocate internally. A buffer with
 * fewer than 2 elements succeeds as a no-op.
 * @param buffer The buffer to sort; may be NULL (returns false).
 * @param comparator Three-way element ordering; must not be NULL.
 * @param userData Caller context passed to @p comparator, or NULL.
 * @param scratch Caller-owned scratch of at least GenericBuffer_GetSortScratchSize(buffer) bytes,
 *        or NULL to allocate internally.
 * @returns true on success; false if @p buffer or @p comparator is NULL, the buffer is read-only,
 *          or (with NULL scratch) the internal scratch allocation could not be sized.
 */
bool GenericBuffer_SortDescending(GenericBuffer* buffer, GenericBufferComparator comparator, const UserData* userData, void* scratch);

/**
 * @brief Sorts the buffer descending, always allocating and freeing its own scratch.
 *
 * Convenience wrapper over GenericBuffer_SortDescending with NULL scratch.
 * @param buffer The buffer to sort; may be NULL (returns false).
 * @param comparator Three-way element ordering; must not be NULL.
 * @param userData Caller context passed to @p comparator, or NULL.
 * @returns true on success; false if @p buffer or @p comparator is NULL or the buffer is read-only.
 */
bool GenericBuffer_SortDescendingAllocating(GenericBuffer* buffer, GenericBufferComparator comparator, const UserData* userData);

/**
 * @brief Removes in place every element for which @p predicate returns false, keeping order.
 *
 * Surviving elements are compacted toward the front and the count is reduced to the number kept.
 * Capacity is unchanged.
 * @param buffer The buffer to filter; may be NULL (returns false).
 * @param predicate Test deciding which elements to keep (true = keep); must not be NULL.
 * @param userData Caller context passed to @p predicate, or NULL.
 * @returns true on success; false if @p buffer or @p predicate is NULL or the buffer is read-only.
 */
bool GenericBuffer_Filter(GenericBuffer* buffer, GenericBufferPredicate predicate, const UserData* userData);

/**
 * @brief Transforms each source element via @p mapper and APPENDS the results to @p destination.
 *
 * For every element of @p buffer, @p mapper is invoked to populate a freshly reserved slot at the
 * tail of @p destination; @p destination grows to fit and its count increases by the source count.
 * Existing elements of @p destination are preserved. The source and destination element sizes may
 * differ. @p buffer and @p destination must be different objects.
 * @param buffer The source buffer to read; may be NULL (returns false).
 * @param destination The buffer that receives the mapped elements; may be NULL (returns false).
 * @param mapper Transform invoked once per source element; must not be NULL.
 * @param userData Caller context passed to @p mapper, or NULL.
 * @returns true on success; false if any of @p buffer, @p destination, @p mapper is NULL, the
 *          destination is read-only, @p buffer == @p destination, the resulting count would overflow
 *          size_t, or the destination could not be grown to hold the mapped elements.
 */
bool GenericBuffer_Map(GenericBuffer* buffer, GenericBuffer* destination, GenericBufferMapper mapper, const UserData* userData);

/**
 * @brief Sums the integer projection of every element into @p outValue.
 *
 * Adds up @p extractor applied to each element. An empty buffer yields 0. Accumulation uses signed
 * 64-bit arithmetic and is not overflow-checked.
 * @param buffer The buffer to reduce; may be NULL (returns false).
 * @param extractor Projection from element to int64_t; must not be NULL.
 * @param userData Caller context passed to @p extractor, or NULL.
 * @param outValue [out] Receives the sum; must not be NULL. Left unchanged on failure.
 * @returns true on success; false if any of @p buffer, @p extractor, @p outValue is NULL.
 */
bool GenericBuffer_SumInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, const UserData* userData, int64_t* outValue);

/**
 * @brief Sums the double projection of every element into @p outValue.
 *
 * Adds up @p extractor applied to each element. An empty buffer yields 0.0.
 * @param buffer The buffer to reduce; may be NULL (returns false).
 * @param extractor Projection from element to double; must not be NULL.
 * @param userData Caller context passed to @p extractor, or NULL.
 * @param outValue [out] Receives the sum; must not be NULL. Left unchanged on failure.
 * @returns true on success; false if any of @p buffer, @p extractor, @p outValue is NULL.
 */
bool GenericBuffer_SumDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, const UserData* userData, double* outValue);

/**
 * @brief Finds the maximum integer projection over all elements into @p outValue.
 *
 * @param buffer The buffer to reduce; may be NULL (returns false).
 * @param extractor Projection from element to int64_t; must not be NULL.
 * @param userData Caller context passed to @p extractor, or NULL.
 * @param outValue [out] Receives the maximum; must not be NULL. Left unchanged on failure.
 * @returns true on success; false if any of @p buffer, @p extractor, @p outValue is NULL, or the
 *          buffer is empty (no maximum exists).
 */
bool GenericBuffer_MaxInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, const UserData* userData, int64_t* outValue);

/**
 * @brief Finds the maximum double projection over all elements into @p outValue.
 *
 * @param buffer The buffer to reduce; may be NULL (returns false).
 * @param extractor Projection from element to double; must not be NULL.
 * @param userData Caller context passed to @p extractor, or NULL.
 * @param outValue [out] Receives the maximum; must not be NULL. Left unchanged on failure.
 * @returns true on success; false if any of @p buffer, @p extractor, @p outValue is NULL, or the
 *          buffer is empty.
 */
bool GenericBuffer_MaxDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, const UserData* userData, double* outValue);

/**
 * @brief Finds the minimum integer projection over all elements into @p outValue.
 *
 * @param buffer The buffer to reduce; may be NULL (returns false).
 * @param extractor Projection from element to int64_t; must not be NULL.
 * @param userData Caller context passed to @p extractor, or NULL.
 * @param outValue [out] Receives the minimum; must not be NULL. Left unchanged on failure.
 * @returns true on success; false if any of @p buffer, @p extractor, @p outValue is NULL, or the
 *          buffer is empty.
 */
bool GenericBuffer_MinInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, const UserData* userData, int64_t* outValue);

/**
 * @brief Finds the minimum double projection over all elements into @p outValue.
 *
 * @param buffer The buffer to reduce; may be NULL (returns false).
 * @param extractor Projection from element to double; must not be NULL.
 * @param userData Caller context passed to @p extractor, or NULL.
 * @param outValue [out] Receives the minimum; must not be NULL. Left unchanged on failure.
 * @returns true on success; false if any of @p buffer, @p extractor, @p outValue is NULL, or the
 *          buffer is empty.
 */
bool GenericBuffer_MinDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, const UserData* userData, double* outValue);

/**
 * @brief Counts how many elements satisfy @p predicate.
 *
 * @param buffer The buffer to scan; may be NULL (returns 0).
 * @param predicate Test applied to each element; must not be NULL (NULL returns 0).
 * @param userData Caller context passed to @p predicate, or NULL.
 * @returns The number of matching elements, or 0 if @p buffer or @p predicate is NULL.
 */
size_t GenericBuffer_CountWhere(GenericBuffer* buffer, GenericBufferPredicate predicate, const UserData* userData);


/**
 * @brief Appends a single byte to a byte buffer.
 *
 * Valid only when the buffer's element size is 1 (a byte buffer); rejected otherwise.
 * @param buffer The byte buffer to append to; may be NULL (returns false).
 * @param byte The byte value to append.
 * @returns true on success; false if @p buffer is NULL, its element size is not 1, the buffer is
 *          read-only, or it could not be grown by one byte.
 */
bool GenericBuffer_AppendByte(GenericBuffer* buffer, unsigned char byte);

/**
 * @brief Appends @p size raw bytes to a byte buffer.
 *
 * Valid only when the buffer's element size is 1. Unlike the string writer, this performs no null-
 * terminator handling; it is a plain byte append.
 * @param buffer The byte buffer to append to; may be NULL (returns false).
 * @param bytes Pointer to @p size bytes to copy. Ignored when @p size is 0; otherwise must not be NULL.
 * @param size Number of bytes to append; 0 is a successful no-op.
 * @returns true on success; false if @p buffer is NULL or not a byte buffer, @p bytes is NULL while
 *          @p size > 0, the buffer is read-only, or it could not be grown.
 */
bool GenericBuffer_AppendRangeBytes(GenericBuffer* buffer, unsigned char* bytes, size_t size);

/**
 * @brief Appends the bytes of a NUL-terminated UTF-8 string (excluding the terminator) to a byte buffer.
 *
 * Valid only when the buffer's element size is 1. The string's `strlen` bytes are appended; the
 * terminating NUL is not stored. To produce a NUL-terminated result, follow with
 * GenericBuffer_NullTerminate.
 * @param buffer The byte buffer to append to; may be NULL (returns false).
 * @param str NUL-terminated UTF-8 string to append; must not be NULL.
 * @returns true on success (including an empty string, which appends nothing); false if @p buffer
 *          is NULL or not a byte buffer, @p str is NULL, the buffer is read-only, or it could not be grown.
 */
bool GenericBuffer_AppendString(GenericBuffer* buffer, const unsigned char* str);

/**
 * @brief Ensures a byte buffer ends with a single NUL terminator.
 *
 * If the buffer is non-empty and its last byte is already 0, this is a no-op success; otherwise a
 * 0 byte is appended (growing the buffer). Valid only when the buffer's element size is 1.
 * @param buffer The byte buffer to terminate; may be NULL (returns false).
 * @returns true on success; false if @p buffer is NULL or not a byte buffer, or (when a byte must
 *          be added) the buffer is read-only or could not be grown.
 */
bool GenericBuffer_NullTerminate(GenericBuffer* buffer);

/**
 * @brief Overwrites the byte at @p index in a byte buffer.
 *
 * Valid only when the buffer's element size is 1. Does not change the count.
 * @param buffer The byte buffer to modify; may be NULL (returns false).
 * @param index Index of an existing byte; must be in [0, count).
 * @param byte The replacement byte value.
 * @returns true on success; false if @p buffer is NULL or not a byte buffer, the buffer is read-only,
 *          or @p index is out of range.
 */
bool GenericBuffer_SetByte(GenericBuffer* buffer, size_t index, unsigned char byte);

/**
 * @brief Reserves capacity for @p addedElementCount more elements ahead of a manual write.
 *
 * Part of the manual-mutation API: call this to guarantee free space, write directly into the
 * reserved tail (e.g. via GenericBuffer_GetWritableTail or a pointer past the last element), then
 * publish the new elements with GenericBuffer_CommitCount or GenericBuffer_SetCount. This call does
 * NOT change the count.
 * @param buffer The buffer to prepare; may be NULL (returns false).
 * @param addedElementCount Number of additional elements the caller intends to write.
 * @returns true if the reservation succeeded; false if @p buffer is NULL, the buffer is read-only,
 *          or it could not be grown to hold @p addedElementCount more elements.
 */
bool GenericBuffer_TryPrepareForManualMutation(GenericBuffer* buffer, size_t addedElementCount);

/**
 * @brief Sets the element count directly after the caller has written into reserved capacity.
 *
 * This is the supported way to update the count after a manual write; never assign `_count`
 * directly. The new count must not exceed the current capacity (reserve first via
 * GenericBuffer_TryPrepareForManualMutation / GenericBuffer_ReserveMoreCapacity).
 * @param buffer The buffer to update; may be NULL (returns false).
 * @param newCount The new element count; must be <= current capacity.
 * @returns true on success; false if @p buffer is NULL, the buffer is read-only, or @p newCount
 *          exceeds the current capacity.
 */
bool GenericBuffer_SetCount(GenericBuffer* buffer, size_t newCount);

/**
 * @brief Increases the element count by @p addedCount (the "I just appended N elements" case).
 *
 * Convenience over GenericBuffer_SetCount that adds to the current count. The resulting count must
 * not exceed the current capacity.
 * @param buffer The buffer to update; may be NULL (returns false).
 * @param addedCount Number of newly written elements to publish.
 * @returns true on success; false if @p buffer is NULL, the addition `count + addedCount` would
 *          overflow size_t, the buffer is read-only, or the resulting count would exceed capacity.
 */
bool GenericBuffer_CommitCount(GenericBuffer* buffer, size_t addedCount);

/**
 * @brief Reserves room for @p requestedCount more elements and returns a writable pointer to the tail.
 *
 * The returned pointer addresses the slot one past the last current element, with at least
 * @p requestedCount elements of contiguous writable space. After writing, publish the elements with
 * GenericBuffer_CommitCount or GenericBuffer_SetCount. The pointer is invalidated by any later
 * operation that grows or relocates the buffer.
 * @param buffer The buffer to extend; may be NULL (returns false).
 * @param requestedCount Number of additional elements to make writable at the tail.
 * @param outTail [out] Receives the tail pointer on success; set to NULL on failure. Must not be NULL.
 * @returns true on success; false (with *outTail set to NULL) if @p buffer or @p outTail is NULL,
 *          the buffer is read-only, or it could not be grown to hold @p requestedCount more elements.
 */
bool GenericBuffer_GetWritableTail(GenericBuffer* buffer, size_t requestedCount, void** outTail);



/**
 * @brief Allocates an uninitialized memory block of at least @p size bytes.
 *
 * The contents are indeterminate. A request of 0 bytes still returns a valid, unique, freeable
 * pointer (at least 1 byte is allocated). Increments the total and current allocation counters.
 * @param size Number of bytes to allocate; 0 is rounded up to 1.
 * @returns A non-NULL pointer to the block. Never returns NULL: on allocation failure the process is aborted.
 * @note Because failure aborts, callers do not need to null-check the result. Release with Memory_Free.
 */
void* Memory_Allocate(size_t size);

/**
 * @brief Resizes a previously allocated block (or allocates a new one) to at least @p size bytes.
 *
 * Behaves like realloc: preserves existing contents up to the smaller of the old and new sizes and
 * may move the block. Passing NULL for @p ptr is equivalent to a fresh allocation and bumps the
 * total/current allocation counters; otherwise only the reallocation counter is bumped. A @p size of
 * 0 is rounded up to 1.
 * @param ptr Block previously returned by Memory_Allocate/Memory_Reallocate, or NULL to allocate anew.
 * @param size New size in bytes; 0 is rounded up to 1.
 * @returns A non-NULL pointer to the (possibly moved) block. Never returns NULL: failure aborts the process.
 * @note The old pointer must not be used after a successful call. Release the result with Memory_Free.
 */
void* Memory_Reallocate(void* ptr, size_t size);

/**
 * @brief Frees a block previously returned by Memory_Allocate or Memory_Reallocate.
 *
 * Passing NULL is a safe no-op. On an actual free, increments the free counter and decrements the
 * current allocation counter.
 * @param ptr The block to free, or NULL.
 */
void Memory_Free(void* ptr);

/**
 * @brief Fills @p size bytes at @p ptr with the byte value @p value.
 *
 * Thin wrapper over memset.
 * @param ptr Destination region of at least @p size bytes.
 * @param value Byte value to write (the low 8 bits are used).
 * @param size Number of bytes to set.
 */
void Memory_Set(void* ptr, int8_t value, size_t size);

/**
 * @brief Sets @p size bytes at @p ptr to zero.
 *
 * @param ptr Destination region of at least @p size bytes.
 * @param size Number of bytes to zero.
 */
void Memory_Zero(void* ptr, size_t size);

/**
 * @brief Tests whether two memory regions hold identical bytes.
 *
 * Compares exactly @p size bytes (a byte-wise equality test, not ordering).
 * @param regionA First region of at least @p size bytes.
 * @param regionB Second region of at least @p size bytes.
 * @param size Number of bytes to compare.
 * @returns true if all @p size bytes are equal, false otherwise.
 */
bool Memory_IsEqual(const void* regionA, const void* regionB, size_t size);

/**
 * @brief Copies @p size bytes from @p source to @p destination.
 *
 * The regions must NOT overlap (memcpy semantics); use Memory_Move when they may overlap.
 * @param source Source region of at least @p size bytes.
 * @param destination Destination region of at least @p size bytes.
 * @param size Number of bytes to copy.
 */
void Memory_Copy(const void* source, void* destination, size_t size);

/**
 * @brief Copies @p size bytes from @p source to @p destination, handling overlap correctly.
 *
 * Safe when the regions overlap (memmove semantics).
 * @param source Source region of at least @p size bytes.
 * @param destination Destination region of at least @p size bytes.
 * @param size Number of bytes to copy.
 */
void Memory_Move(void* source, void* destination, size_t size);

/**
 * @brief Multiplies two sizes with overflow checking.
 *
 * Use before any allocation whose byte size is a product of two values.
 * @param a First factor.
 * @param b Second factor.
 * @param outResult [out] Receives the product on success; left unchanged on overflow. Must not be NULL.
 * @returns true if `a * b` fits in size_t (product written to *outResult); false on overflow or if
 *          @p outResult is NULL.
 */
bool Memory_TryMultiplySize(size_t a, size_t b, size_t* outResult);

/**
 * @brief Adds two sizes with overflow checking.
 *
 * @param a First addend.
 * @param b Second addend.
 * @param outResult [out] Receives the sum on success; left unchanged on overflow. Must not be NULL.
 * @returns true if `a + b` fits in size_t (sum written to *outResult); false on overflow or if
 *          @p outResult is NULL.
 */
bool Memory_TryAddSize(size_t a, size_t b, size_t* outResult);

/**
 * @brief Computes a geometric growth capacity that reaches @p requiredCapacity without byte overflow.
 *
 * Starting from @p startCapacity (treated as at least 1), repeatedly multiplies by
 * @p growthMultiplier until the value is at least @p requiredCapacity, while ensuring the resulting
 * byte size (`capacity * elementSize`) never overflows size_t. If a further multiply would overflow,
 * the result is clamped to exactly @p requiredCapacity. Shared by every growable allocation callback.
 * @param startCapacity Current capacity to grow from, in elements; 0 is treated as 1.
 * @param requiredCapacity Minimum capacity the result must reach, in elements.
 * @param growthMultiplier Geometric growth factor; must be >= 2.
 * @param elementSize Size in bytes of one element; must be > 0.
 * @param outCapacity [out] Receives the chosen capacity on success; left unchanged on failure. Must not be NULL.
 * @returns true on success; false if @p outCapacity is NULL, @p elementSize is 0, @p growthMultiplier
 *          is < 2, or @p requiredCapacity is itself too large for its byte size to fit in size_t.
 */
bool Memory_TryGrowCapacity(size_t startCapacity,
    size_t requiredCapacity,
    size_t growthMultiplier,
    size_t elementSize,
    size_t* outCapacity);

/**
 * @brief Returns the cumulative number of allocations performed since program start.
 *
 * Counts every Memory_Allocate, plus every Memory_Reallocate called with a NULL pointer. Monotonic,
 * process-wide, and thread-safe (relaxed atomic load).
 * @returns The total allocation count.
 */
size_t Memory_GetTotalAllocationCount(void);

/**
 * @brief Returns the cumulative number of reallocations performed since program start.
 *
 * Counts every Memory_Reallocate call (whether or not @p ptr was NULL). Monotonic, process-wide,
 * thread-safe.
 * @returns The total reallocation count.
 */
size_t Memory_GetTotalReallocationCount(void);

/**
 * @brief Returns the cumulative number of frees performed since program start.
 *
 * Counts every Memory_Free call that actually freed a non-NULL pointer. Monotonic, process-wide,
 * thread-safe.
 * @returns The total free count.
 */
size_t Memory_GetTotalFreeCount(void);

/**
 * @brief Returns the number of allocations currently outstanding (allocated but not yet freed).
 *
 * Increases on each allocation (including realloc-from-NULL) and decreases on each actual free; a
 * nonzero value at shutdown indicates leaked blocks. Process-wide, thread-safe.
 * @returns The current live allocation count.
 */
size_t Memory_GetCurrentAllocationCount(void);
