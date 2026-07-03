#pragma once
#include "WRError.h"
#include "WRMemory.h"
#include "WRHashMap.h"

/**
 * @brief A pool of reusable GenericBuffers, keyed by element size, that avoids repeated allocation.
 *
 * Borrow a buffer for a given element size with BufferPool_Borrow and return it with
 * BufferPool_Return; returned buffers are kept (with their grown capacity) for reuse by later
 * borrows of the same element size instead of being freed. Buffers of different element sizes are
 * pooled separately. A borrowed buffer is owned by the caller only for the duration of the loan: it
 * must be returned to the same pool (and not freed or deconstructed by the caller) and must not be
 * used after it is returned. The pool itself owns the buffers' backing storage, which is released
 * when the pool is deconstructed. Initialize with BufferPool_Construct1 and release with
 * BufferPool_Deconstruct. Not thread-safe; external synchronization is required for concurrent use.
 */
typedef struct WRBufferPoolStruct
{
    /** @brief Map from element size to the per-size pool of reusable GenericBuffers backing that size. */
    HashMap _entries;
} WRBufferPool;


// Functions.
/**
 * @brief Initializes an empty buffer pool ready to hand out reusable buffers.
 *
 * Sets up the internal per-size storage; no buffers are allocated until the first borrow. Must later
 * be released with BufferPool_Deconstruct.
 * @param self The pool to initialize. Must not be NULL.
 * @returns A success Error on success. Raises ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error BufferPool_Construct1(WRBufferPool* self);

/**
 * @brief Releases all buffers and storage owned by the pool and resets it to an empty state.
 *
 * Deconstructs every pooled buffer (freeing each buffer's backing data) and the internal map.
 * After the call the pool structure is zeroed; any buffer pointers previously obtained from this
 * pool are dangling and must not be used. Teardown is best-effort: an error does not stop the
 * process; the first error encountered is returned and every later error's message is released so
 * none leak. Safe to call on a NULL @p self (returns success).
 * @param self The pool to deconstruct. May be NULL.
 * @returns A success Error if teardown reported no failure (including the NULL case); otherwise the
 *          first non-success Error encountered while tearing down.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error BufferPool_Deconstruct(WRBufferPool* self);

/**
 * @brief Borrows a growable GenericBuffer with the requested element size from the pool.
 *
 * Reuses a previously returned buffer of the same @p elementSize when one is available, otherwise
 * provides a freshly initialized empty growable buffer. The borrowed buffer is always handed back
 * cleared (element count zero), but a reused buffer retains the capacity it grew to on prior use, so
 * a buffer that previously held many elements may already have spare capacity allocated. The buffer
 * remains owned by the pool: the caller may read and mutate it for the duration of the loan but must
 * return it with BufferPool_Return rather than freeing or deconstructing it, and must not use it
 * after returning. On any failure @p *outBuffer is set to NULL.
 * @param self The pool to borrow from. Must not be NULL.
 * @param elementSize Size in bytes of each element the borrowed buffer will store. Must be greater
 *        than zero.
 * @param outBuffer [out] Receives the borrowed buffer pointer on success, or NULL on failure. Must
 *        not be NULL.
 * @returns A success Error with @p *outBuffer pointing at the borrowed buffer. Raises
 *          ErrorCode_IllegalArgument if @p self or @p outBuffer is NULL, ErrorCode_ArgumentOutOfRange
 *          if @p elementSize is zero, and ErrorCode_BufferTooLarge if the backing pool would have to
 *          grow beyond addressable capacity.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error BufferPool_Borrow(WRBufferPool* self, size_t elementSize, GenericBuffer** outBuffer);

/**
 * @brief Returns a previously borrowed buffer to the pool, retaining its capacity for reuse.
 *
 * The buffer must be one obtained from this same pool via BufferPool_Borrow; it is matched back to
 * the per-size pool by its element size and marked available. The buffer is cleared (count reset to
 * zero) but its allocated capacity is kept so a later borrow of the same element size can reuse it
 * without reallocating. After a successful return the @p bufferToReturn pointer must not be used
 * until borrowed again. A buffer that does not belong to this pool is rejected.
 * @param self The pool to return to. Must not be NULL.
 * @param bufferToReturn The buffer to return; must be a pointer previously obtained from this pool.
 *        Must not be NULL.
 * @returns A success Error on success. Raises ErrorCode_IllegalArgument if @p self or
 *          @p bufferToReturn is NULL, or if @p bufferToReturn does not belong to this pool (foreign
 *          or never-borrowed buffer).
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error BufferPool_Return(WRBufferPool* self, GenericBuffer* bufferToReturn);