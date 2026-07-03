#pragma once
#include "WRIO.h"


// Types.
/**
 * @brief Concrete IOStream backed by an in-memory byte buffer (a GenericBuffer).
 *
 * Implements the full IOStream contract against memory: reads/writes copy bytes to and from the
 * backing buffer, and writing past the current end grows the buffer (when it is growable). Memory
 * streams are always seekable and length-settable; IOStreamFlags_CanSeek and
 * IOStreamFlags_CanSetLength are added automatically at construction regardless of the flags passed.
 * The backing buffer is either owned and managed internally (MemoryStream_Construct1) or an external
 * buffer the stream merely wraps (MemoryStream_Construct2). The IOStream interface is embedded first
 * (`Base`); obtain an IOStream* via MemoryStream_AsIOStream. Release with MemoryStream_Deconstruct.
 */
typedef struct MemoryStreamStruct
{
    /** @brief Embedded IOStream interface; placed first so a MemoryStream* can act as an IOStream*. */
    IOStream Base;
    /** @brief Internally owned backing buffer used when the stream allocates its own storage; pointed to by `_buffer` in that case. */
    GenericBuffer _selfContainedBuffer;
    /** @brief The active backing buffer: either `&_selfContainedBuffer` (owned) or an external wrapped buffer. */
    GenericBuffer* _buffer;
    /** @brief Current read/write offset into the backing buffer, in bytes. */
    size_t _position;
    /** @brief Whether the stream owns `_selfContainedBuffer`'s storage and must free it on Deconstruct. */
    bool _ownsBuffer;
    /** @brief Whether the stream has been closed (operations fail afterward). */
    bool _isClosed;
} MemoryStream;


// Functions.
/**
 * @brief Returns the embedded IOStream interface for this memory stream.
 *
 * The returned pointer aliases the MemoryStream's first member and is valid only as long as the
 * MemoryStream itself; it must not be freed independently.
 * @param self The memory stream; must be non-NULL.
 * @returns A non-owning IOStream* through which the generic IOStream_* operations can be invoked.
 */
static inline IOStream* MemoryStream_AsIOStream(MemoryStream* self)
{
    return &self->Base;
}

/**
 * @brief Returns the backing GenericBuffer that holds the stream's bytes.
 *
 * Lets callers inspect or extract the accumulated/wrapped data (its `_count` bytes are the stream's
 * current contents). The buffer is owned by the stream (or by the external owner that supplied it via
 * MemoryStream_Construct2); the returned pointer must not be freed through this accessor, and it
 * becomes invalid once the stream is deconstructed (for an owned buffer, its storage is freed then).
 * @param self The memory stream; must be non-NULL.
 * @returns The active backing buffer pointer.
 */
static inline GenericBuffer* MemoryStream_GetBuffer(MemoryStream* self)
{
    return self->_buffer;
}

/**
 * @brief Initializes a memory stream over a fresh, internally owned, growable byte buffer.
 *
 * Zero-initializes `self`, creates an empty self-contained byte buffer that grows on demand as data
 * is written, and starts the position at 0. The stream owns this buffer and frees its storage on
 * Deconstruct. IOStreamFlags_CanSeek and IOStreamFlags_CanSetLength are added to `flags`
 * automatically; pass IOStreamFlags_CanWrite and/or IOStreamFlags_CanRead to enable those operations.
 * The object must later be released with MemoryStream_Deconstruct.
 * @param self The memory stream to initialize; must be non-NULL.
 * @param flags Capability bitmask (CanRead/CanWrite); seek and set-length are always enabled.
 * @returns Success, or ErrorCode_IllegalArgument if `self` is NULL.
 */
Error MemoryStream_Construct1(MemoryStream* self, IOStreamFlags flags);

/**
 * @brief Initializes a memory stream that wraps an existing, externally owned byte buffer.
 *
 * Zero-initializes `self` and binds it to `bufferToWrap` (which must be a byte buffer, element size
 * 1), starting the position at 0. The stream does NOT take ownership: the buffer's storage is not
 * freed on Deconstruct and the caller remains responsible for its lifetime, which must outlive the
 * stream. Existing buffer contents are visible to reads, and writes update the buffer in place
 * (growing it if it is growable). A read-only buffer cannot be wrapped writably. IOStreamFlags_CanSeek
 * and IOStreamFlags_CanSetLength are added to `flags` automatically. Release with
 * MemoryStream_Deconstruct.
 * @param self The memory stream to initialize; must be non-NULL.
 * @param bufferToWrap The external byte buffer to wrap; must be non-NULL with element size 1, and
 *        must outlive the stream.
 * @param flags Capability bitmask (CanRead/CanWrite); seek and set-length are always enabled.
 * @returns Success, or ErrorCode_IllegalArgument if `self` or `bufferToWrap` is NULL or
 *          `bufferToWrap` is not a byte buffer, or ErrorCode_InvalidOperation if CanWrite is
 *          requested over a read-only buffer.
 */
Error MemoryStream_Construct2(MemoryStream* self, GenericBuffer* bufferToWrap, IOStreamFlags flags);

/**
 * @brief Sets the stream's logical length, truncating or zero-extending the backing buffer.
 *
 * Shrinking discards trailing bytes and clamps the current position to the new length; extending
 * appends zero bytes; an unchanged length is a no-op. Fails if the stream is closed or its buffer is
 * read-only. This is the same operation reachable through IOStream_SetLength.
 * @param self The memory stream; may be NULL (reported as an error).
 * @param length The new length in bytes.
 * @returns Success, or ErrorCode_IllegalArgument if `self` is NULL, ErrorCode_InvalidOperation if the
 *          stream is closed or the buffer is read-only, or ErrorCode_BufferTooSmall if a
 *          fixed-capacity buffer cannot be grown to the requested length.
 */
Error MemoryStream_SetLength(MemoryStream* self, size_t length);

/**
 * @brief Tears the memory stream down: closes it, frees an owned buffer, and zeroes the object.
 *
 * Marks the stream closed and, if it owns its backing buffer (created via MemoryStream_Construct1),
 * frees that buffer's storage; a wrapped external buffer (MemoryStream_Construct2) is left untouched.
 * Safe to call on a NULL stream. After this call the object must not be used again.
 * @param self The memory stream to destroy; may be NULL (treated as a no-op).
 */
void MemoryStream_Deconstruct(MemoryStream* self);
