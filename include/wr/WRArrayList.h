#pragma once
#include "WRList.h"
#include "WRCollection.h"
#include "WRMemory.h"
#include "WRError.h"
#include <stddef.h>

// Types.
/**
 * @brief A growable, contiguous list of fixed-size elements that exposes the generic IList interface.
 *
 * An ArrayList stores its elements in a single contiguous GenericBuffer. It can either own its
 * storage (created with ArrayList_Construct1 / ArrayList_Construct2) or wrap an externally supplied
 * buffer (ArrayList_Construct3), in which case ownership of that buffer's memory stays with the
 * caller. List operations (insert, remove, replace, clear, element access, enumeration) are reached
 * through the embedded IList (see ArrayList_GetActiveBuffer / the _list field), and respect the
 * read-only flag of the underlying buffer. Every successfully constructed ArrayList must eventually
 * be released with ArrayList_Deconstruct.
 */
typedef struct ArrayListStruct
{
    /**
     * @brief Storage owned by this ArrayList, used when the list manages its own memory.
     *
     * Populated by the constructors when no external buffer is wrapped; in that case _activeBuffer
     * points at this field. When an external buffer is wrapped successfully it is left empty.
     */
    GenericBuffer _selfContainedBuffer;
    /**
     * @brief The buffer currently backing the list (either &_selfContainedBuffer or a wrapped buffer).
     *
     * All element storage and list operations act on this buffer. Borrowed pointer; not freed here
     * unless _isActiveBufferOwned is true.
     */
    GenericBuffer* _activeBuffer;
    /**
     * @brief The generic IList interface implementation backed by this ArrayList.
     *
     * Use this to perform list operations (insert/remove/replace/clear/get/enumerate) and to obtain
     * the list as an ICollection. Its lifetime is tied to the ArrayList.
     */
    IList _list;
    /**
     * @brief Whether the active buffer's underlying data is owned by this ArrayList.
     *
     * True when the list allocated its own storage; false when wrapping a caller-owned buffer.
     * Determines whether ArrayList_Deconstruct frees the active buffer's data.
     */
    bool _isActiveBufferOwned;
} ArrayList;


// Functions.
/**
 * @brief Constructs an empty, self-owning ArrayList for elements of the given size.
 *
 * Initializes the list to wrap its own internally managed, growable buffer with zero elements and
 * zero initial capacity; storage is allocated lazily as elements are added or capacity is reserved.
 * The list owns its storage and is writable.
 * @param self The ArrayList to initialize. If NULL, the call is a no-op.
 * @param elementSize Size in bytes of each element. Determines the stride of the backing buffer.
 * @note The list must later be released with ArrayList_Deconstruct.
 */
void ArrayList_Construct1(ArrayList* self, size_t elementSize);

/**
 * @brief Constructs an empty, self-owning ArrayList and reserves an initial capacity.
 *
 * Behaves like ArrayList_Construct1 and then attempts to grow the backing buffer to hold at least
 * initialCapacity elements (no elements are added; the count stays zero).
 * @param self The ArrayList to initialize. If NULL, the call is a no-op.
 * @param elementSize Size in bytes of each element.
 * @param initialCapacity Number of elements to reserve up front. If 0, no capacity is reserved. If
 *        the reservation fails, the list is still left as a valid empty list (the failure is ignored).
 * @note The list must later be released with ArrayList_Deconstruct.
 */
void ArrayList_Construct2(ArrayList* self, size_t elementSize, size_t initialCapacity);

/**
 * @brief Constructs an ArrayList that wraps an externally supplied buffer.
 *
 * The list operates on bufferToWrap in place, exposing its current contents through the IList
 * interface and honoring its read-only / fixed-capacity flags. Ownership of bufferToWrap and its
 * memory remains with the caller: ArrayList_Deconstruct will not free it. If bufferToWrap is NULL,
 * the list falls back to an empty, self-owning internal buffer (as if constructed empty), so the
 * resulting list is always valid.
 * @param self The ArrayList to initialize. If NULL, the call is a no-op.
 * @param bufferToWrap The buffer to wrap. May be NULL (see above). Must remain valid and not be freed
 *        by the caller for the lifetime of the wrapping ArrayList (unless NULL was passed).
 * @note The list must later be released with ArrayList_Deconstruct.
 */
void ArrayList_Construct3(ArrayList* self, GenericBuffer* bufferToWrap);

/**
 * @brief Ensures the backing buffer can hold at least totalCapacity elements without reallocating.
 *
 * Grows the active buffer's capacity if it is currently smaller than totalCapacity; the element
 * count is unchanged. A request that is already satisfied succeeds without growing. On success the
 * embedded IList is refreshed to reflect the buffer state.
 * @param self The ArrayList. Must not be NULL.
 * @param totalCapacity The minimum total element capacity required.
 * @returns Error_CreateSuccess on success. Raises ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the list is read-only, or if the capacity could not be
 *          ensured (for example the allocation failed).
 */
Error ArrayList_EnsureTotalCapacity(ArrayList* self, size_t totalCapacity);

/**
 * @brief Ensures the backing buffer has room for requiredExtraSize additional elements.
 *
 * Reserves capacity beyond the current element count so that at least requiredExtraSize more
 * elements can be added without reallocating; the element count is unchanged. On success the
 * embedded IList is refreshed to reflect the buffer state.
 * @param self The ArrayList. Must not be NULL.
 * @param requiredExtraSize The number of additional elements to make room for.
 * @returns Error_CreateSuccess on success. Raises ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the required total capacity would overflow size_t, if the
 *          list is read-only, or if the capacity could not be reserved (for example the allocation
 *          failed).
 */
Error ArrayList_ReserveMoreCapacity(ArrayList* self, size_t requiredExtraSize);

/**
 * @brief Returns the buffer currently backing the list.
 *
 * @param self The ArrayList. Must not be NULL.
 * @returns A borrowed pointer to the active GenericBuffer (the internally owned buffer or the wrapped
 *          one). The pointer is owned by the ArrayList; do not free it. It remains valid until the
 *          list is deconstructed; note that operations which grow the buffer may relocate the
 *          buffer's element data, though this returned GenericBuffer pointer itself stays valid.
 */
static inline GenericBuffer* ArrayList_GetActiveBuffer(ArrayList* self)
{
    return self->_activeBuffer;
}

/**
 * @brief Releases the ArrayList and resets it to a valid empty state.
 *
 * If the list owns its storage, the active buffer's element data is freed. If the list wraps a
 * caller-owned buffer, that buffer's memory is left untouched. After the call the list is reset to
 * an empty, self-owning internal buffer, so it is safe to call again and the object may be reused or
 * discarded.
 * @param self The ArrayList to release. If NULL, the call is a no-op.
 */
void ArrayList_Deconstruct(ArrayList* self);
