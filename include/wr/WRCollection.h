#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WRError.h"
#include "WRMemory.h"


// Types.
/**
 * @brief Capability flags advertised by a CollectionEnumerator.
 *
 * Describes optional features an enumerator may or may not support. A caller inspects these (via
 * CollectionEnumerator_IsReferenceReturningSupported) before attempting a capability-gated operation.
 */
typedef enum EnumeratorFlagsEnum
{
    /** @brief No optional capabilities; only by-value iteration is guaranteed. */
    EnumeratorFlags_None = 0,
    /**
     * @brief The enumerator can yield pointers directly into the underlying storage.
     *
     * When set, CollectionEnumerator_NextByReference (and the enumerator's _nextByReference slot) is
     * usable. When clear, only by-value iteration is supported and reference-returning calls fail.
     */
    EnumeratorFlags_CanReturnByReference = (1 << 0)
} EnumeratorFlags;

/**
 * @brief Function-pointer table defining the abstract iteration contract for a CollectionEnumerator.
 *
 * Every concrete enumerator implementation supplies these slots. @c Self is passed back as the
 * receiver to each slot. All conforming implementations must satisfy the per-slot contracts below.
 */
typedef struct CollectionEnumeratorVTableStruct
{
    /** @brief Opaque receiver passed as the @c self argument to every slot in this vtable. */
    void* Self;
    /**
     * @brief Report whether at least one more element remains before the current position.
     *
     * Must write @c true to @p outHasNext if a subsequent NextByValue/NextByReference would yield an
     * element, otherwise @c false. Must not advance the iteration. @p self and @p outHasNext are
     * guaranteed non-NULL by the public wrapper.
     * @param self The enumerator receiver (the vtable's @c Self).
     * @param outHasNext [out] Receives whether another element is available.
     * @returns Success when the availability was determined; otherwise an Error describing the failure.
     */
    Error (*_hasNext)(void* self, bool* outHasNext);
    /**
     * @brief Advance to the next element and copy it by value into the caller's buffer.
     *
     * On success, copies exactly @c _singleElementSize bytes of the current element into @p outEntryValue
     * and advances the cursor past it. @p outEntryValue must point to storage of at least that size.
     * Calling when no element remains must fail rather than read out of bounds.
     * @param self The enumerator receiver (the vtable's @c Self).
     * @param outEntryValue [out] Destination for a copy of the element; at least @c _singleElementSize bytes.
     * @returns Success when an element was copied and the cursor advanced; otherwise an Error (e.g. when
     *          iteration is exhausted).
     */
    Error (*_nextByValue)(void* self, void* outEntryValue);
    /**
     * @brief Advance to the next element and return a pointer to it within the underlying storage.
     *
     * Only required to function when the enumerator advertises EnumeratorFlags_CanReturnByReference.
     * On success, writes into @p outPointer a pointer aliasing the live element and advances the cursor.
     * The pointer remains valid only as long as the backing collection is not mutated and the
     * enumerator outlives its use. Calling when no element remains must fail.
     * @param self The enumerator receiver (the vtable's @c Self).
     * @param outPointer [out] Receives a pointer that aliases the current element in place.
     * @returns Success when a pointer was produced and the cursor advanced; otherwise an Error.
     */
    Error (*_nextByReference)(void* self, void** outPointer);
    /**
     * @brief Release any internal resources held by the enumerator.
     *
     * Must leave the enumerator inert. Implementations must NOT free the caller-owned buffer the
     * enumerator was constructed into (its lifetime is the caller's responsibility).
     * @param self The enumerator receiver (the vtable's @c Self).
     */
    void (*_deconstruct)(void* self);
} CollectionEnumeratorVTable;

/**
 * @brief A caller-owned, single-pass cursor over the elements of an ICollection.
 *
 * Constructed into a caller-supplied buffer (see ICollection_InitEnumerator). Iterate with
 * HasNext + NextByValue (always available) or NextByReference (only if the by-reference capability is
 * advertised), then Deconstruct. The enumerator does not own and must not outlive its backing buffer.
 */
typedef struct CollectionEnumeratorStruct
{
    /** @brief Size in bytes of one element, i.e. the number of bytes NextByValue copies per step. */
    size_t _singleElementSize;
    /** @brief Capability flags (see EnumeratorFlags) advertising optional features such as by-reference iteration. */
    EnumeratorFlags _flags;
    /** @brief Dispatch table implementing this enumerator's iteration behavior. */
    CollectionEnumeratorVTable _vtable;
} CollectionEnumerator;

/**
 * @brief Function-pointer table defining the abstract contract for an ICollection.
 *
 * Every concrete collection supplies these slots. @c Self is passed back as the receiver to each slot.
 */
typedef struct ICollectionVTable
{
    /** @brief Opaque receiver passed as the @c self argument to every slot in this vtable. */
    void* Self;
    /**
     * @brief Report the byte size of the buffer required to construct an enumerator for this collection.
     *
     * The returned value is the minimum number of bytes a caller must provide to _initEnumerator. It
     * must be a valid size for the current collection (callers should re-query if the collection type
     * changes). Must not allocate or mutate the collection.
     * @param self The collection receiver (the vtable's @c Self).
     * @returns The required enumerator buffer size in bytes.
     */
    size_t (*_getEnumeratorSize)(void* self);
    /**
     * @brief Construct an enumerator for this collection into the caller-provided buffer.
     *
     * @p buffer must be at least _getEnumeratorSize(self) bytes and must outlive the returned
     * enumerator. The enumerator begins positioned before the first element. The returned pointer is
     * the buffer reinterpreted as a CollectionEnumerator (typically equal to @p buffer); the caller
     * retains ownership of the buffer's storage.
     * @param self The collection receiver (the vtable's @c Self).
     * @param buffer Caller-owned storage of at least the required size; outlives the enumerator.
     * @returns A pointer to the initialized enumerator within @p buffer.
     */
    CollectionEnumerator* (*_initEnumerator)(void* self, void* buffer);
} ICollectionVtable;

/**
 * @brief Abstract, read-as-a-sequence view over a collection of fixed-size elements.
 *
 * An ICollection exposes its contents only through enumerators; it carries no count or direct indexing.
 * Maps expose their entries, keys, and values as separate ICollection views (see WRMap). The element
 * representation (key/value/entry) is defined by whichever collection produced the view.
 */
typedef struct ICollectionStruct
{
    /** @brief Dispatch table implementing this collection's enumerator-creation behavior. */
    ICollectionVtable _vtable;
} ICollection;


// Functions.

/**
 * @brief Return the byte size of the buffer required to construct an enumerator for this collection.
 *
 * Enumerators are caller-owned. The preferred iteration form queries this size, supplies a buffer of
 * at least this many bytes (which may be reused or stack-allocated to avoid per-iteration allocation)
 * to ICollection_InitEnumerator, iterates, then calls CollectionEnumerator_Deconstruct (which releases
 * internal resources but does NOT free the buffer). The buffer must outlive the enumerator.
 * @param self The collection to query; must be non-NULL.
 * @returns The minimum enumerator buffer size in bytes.
 */
static inline size_t ICollection_GetEnumeratorSize(ICollection* self)
{
    return (*self->_vtable._getEnumeratorSize)(self->_vtable.Self);
}

/**
 * @brief Construct an enumerator for this collection into a caller-provided buffer.
 *
 * @p buffer must be at least ICollection_GetEnumeratorSize(self) bytes and must remain valid for the
 * entire lifetime of the enumerator. The enumerator starts positioned before the first element. The
 * caller retains ownership of the buffer and must release the enumerator with
 * CollectionEnumerator_Deconstruct (which does not free the buffer).
 * @param self The collection to enumerate; must be non-NULL.
 * @param buffer Caller-owned storage of at least the required size; must outlive the enumerator.
 * @returns A pointer to the initialized enumerator located within @p buffer.
 */
static inline CollectionEnumerator* ICollection_InitEnumerator(ICollection* self, void* buffer)
{
    return (*self->_vtable._initEnumerator)(self->_vtable.Self, buffer);
}

/**
 * @brief Allocate a correctly-sized buffer and construct an enumerator into it.
 *
 * Convenience over the caller-owned form: allocates ICollection_GetEnumeratorSize(self) bytes and
 * initializes an enumerator there. The returned enumerator must be released with
 * CollectionEnumerator_Destroy (which both deconstructs it and frees the allocation). Prefer the
 * caller-owned InitEnumerator form on hot paths to avoid per-iteration allocation.
 * @param self The collection to enumerate; must be non-NULL.
 * @returns A pointer to a newly allocated, initialized enumerator.
 */
static inline CollectionEnumerator* ICollection_CreateEnumerator(ICollection* self)
{
    return ICollection_InitEnumerator(self, Memory_Allocate(ICollection_GetEnumeratorSize(self)));
}

/**
 * @brief Append every element of the collection to a GenericBuffer by value.
 *
 * Enumerates the collection and copies each element (by value) onto the tail of @p buffer, growing it
 * as needed. The destination's element size must equal CollectionEnumerator_GetSingleElementSize for
 * this collection.
 * @param self The collection to copy from; must be non-NULL.
 * @param buffer Destination buffer that receives the elements; must be non-NULL and its configured
 *        element size must match the collection's element size.
 * @returns Success when all elements were appended. Raises ErrorCode_IllegalArgument if @p self or
 *          @p buffer is NULL, or if the buffer's element size does not match; ErrorCode_BufferTooSmall
 *          if the destination cannot grow to hold the elements.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ICollection_WriteToBufferByValue(ICollection* self, GenericBuffer* buffer);

/**
 * @brief Append a pointer to every element of the collection to a GenericBuffer.
 *
 * Enumerates the collection by reference and appends each element's address (a @c void*) onto the tail
 * of @p buffer. The destination must be configured with an element size of @c sizeof(void*). The stored
 * pointers alias the collection's live storage and are valid only while the collection is unmodified.
 * Requires the collection's enumerator to support by-reference iteration.
 * @param self The collection to copy from; must be non-NULL.
 * @param buffer Destination buffer that receives element pointers; must be non-NULL with element size
 *        @c sizeof(void*).
 * @returns Success when all element pointers were appended. Raises ErrorCode_IllegalArgument if @p self
 *          or @p buffer is NULL, or if the buffer's element size is not @c sizeof(void*);
 *          ErrorCode_BufferTooSmall if the destination cannot grow; ErrorCode_InvalidOperation if the
 *          enumerator does not support by-reference iteration.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ICollection_WriteToBufferByReference(ICollection* self, GenericBuffer* buffer);


/**
 * @brief Report whether the enumerator has at least one more element to yield.
 *
 * Does not advance the cursor; a @c true result means a following NextByValue/NextByReference would
 * succeed.
 * @param self The enumerator to query; must be non-NULL.
 * @param outValue [out] Receives @c true if another element is available, otherwise @c false.
 * @returns Success when availability was determined; otherwise an Error from the underlying enumerator.
 */
static inline Error CollectionEnumerator_HasNext(CollectionEnumerator* self, bool* outValue)
{
    return (*self->_vtable._hasNext)(self->_vtable.Self, outValue);
}

/**
 * @brief Return the size in bytes of a single element, as copied by NextByValue.
 *
 * This is the minimum size a NextByValue destination buffer must have, and the element size a
 * by-value GenericBuffer destination must be configured with.
 * @param self The enumerator to query; must be non-NULL.
 * @returns The per-element size in bytes.
 */
static inline size_t CollectionEnumerator_GetSingleElementSize(CollectionEnumerator* self)
{
    return self->_singleElementSize;
}


/**
 * @brief Advance to the next element and copy it by value into the caller's buffer.
 *
 * Copies CollectionEnumerator_GetSingleElementSize(self) bytes of the current element into @p outValue
 * and advances the cursor. @p outValue must point to at least that many bytes. Call only when HasNext
 * reported @c true; calling past the end yields an error rather than out-of-bounds access.
 * @param self The enumerator to advance; must be non-NULL.
 * @param outValue [out] Destination for the element copy; at least the single-element size in bytes.
 * @returns Success when an element was copied and the cursor advanced; otherwise an Error (e.g. when
 *          iteration is exhausted).
 */
static inline Error CollectionEnumerator_NextByValue(CollectionEnumerator* self, void* outValue)
{
    return (*self->_vtable._nextByValue)(self->_vtable.Self, outValue);
}

/**
 * @brief Advance to the next element and return a pointer aliasing it in the collection's storage.
 *
 * On success, writes a pointer to the live element into @p outPointer and advances the cursor. The
 * pointer is valid only while the backing collection is unmodified and the enumerator remains alive.
 * Only usable when CollectionEnumerator_IsReferenceReturningSupported(self) is true.
 * @param self The enumerator to advance; must be non-NULL.
 * @param outPointer [out] Receives a pointer that aliases the current element; set to NULL on the
 *        unsupported-capability error.
 * @returns Success when a pointer was produced and the cursor advanced. Raises ErrorCode_IllegalArgument
 *          if @p self or @p outPointer is NULL; ErrorCode_InvalidOperation if this enumerator does not
 *          support by-reference iteration. May also return an Error from the underlying enumerator.
 */
Error CollectionEnumerator_NextByReference(CollectionEnumerator* self, void** outPointer);

/**
 * @brief Report whether the enumerator supports yielding elements by reference.
 *
 * Equivalent to testing EnumeratorFlags_CanReturnByReference. When false, only NextByValue may be used;
 * NextByReference will fail.
 * @param self The enumerator to query; must be non-NULL.
 * @returns @c true if by-reference iteration is supported, otherwise @c false.
 */
static inline bool CollectionEnumerator_IsReferenceReturningSupported(CollectionEnumerator* self)
{
    return ((self ->_flags) & EnumeratorFlags_CanReturnByReference) != 0;
}

/**
 * @brief Release the enumerator's internal resources without freeing its buffer.
 *
 * Use this to finish iterating an enumerator constructed into a caller-owned buffer (via
 * ICollection_InitEnumerator). After this call the enumerator is inert; the caller is responsible for
 * freeing or reusing the buffer. For enumerators obtained from ICollection_CreateEnumerator, use
 * CollectionEnumerator_Destroy instead so the allocation is freed too.
 * @param self The enumerator to deconstruct; must be non-NULL.
 */
static inline void CollectionEnumerator_Deconstruct(CollectionEnumerator* self)
{
    (*self->_vtable._deconstruct)(self->_vtable.Self);
}

/**
 * @brief Deconstruct the enumerator and free its backing buffer.
 *
 * Use only for enumerators returned by ICollection_CreateEnumerator, which allocated the buffer. For
 * an enumerator built into a caller-owned buffer, use CollectionEnumerator_Deconstruct and free/reuse
 * the buffer yourself.
 * @param self The enumerator to destroy; must be non-NULL and must have been created via
 *        ICollection_CreateEnumerator.
 */
static inline void CollectionEnumerator_Destroy(CollectionEnumerator* self)
{
    CollectionEnumerator_Deconstruct(self);
    Memory_Free(self);
}