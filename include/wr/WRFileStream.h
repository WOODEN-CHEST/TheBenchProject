#pragma once
#include "WRIO.h"


// Types.
/**
 * @brief Concrete IOStream backed by a C standard-library FILE handle.
 *
 * Wraps a native `FILE*` and implements the full IOStream contract (read/write/seek/flush/length/
 * close) on top of stdio. The actual capabilities exposed are those passed in the construction
 * flags. The IOStream interface is embedded as the first member (`Base`); obtain an IOStream* via
 * FileStream_AsIOStream. Must be torn down with FileStream_Deconstruct (or IOStream_Deconstruct).
 */
typedef struct FileStreamStruct
{
    /** @brief Embedded IOStream interface; placed first so a FileStream* can act as an IOStream*. */
    IOStream Base;
    /** @brief The underlying C standard-library `FILE*`; NULL once the stream has been closed. */
    void* _nativeHandle;
    /** @brief Whether this stream owns the handle and must close it on Close/Deconstruct. */
    bool _ownsHandle;
    /** @brief Whether the stream has been closed (operations fail afterward). */
    bool _isClosed;
} FileStream;


// Functions.
/**
 * @brief Returns the embedded IOStream interface for this file stream.
 *
 * The returned pointer aliases the FileStream's first member and remains valid only as long as the
 * FileStream itself; it must not be freed independently.
 * @param self The file stream; must be non-NULL.
 * @returns A non-owning IOStream* through which the generic IOStream_* operations can be invoked.
 */
static inline IOStream* FileStream_AsIOStream(FileStream* self)
{
    return &self->Base;
}

/**
 * @brief Initializes a file stream that wraps an already-open native `FILE*` handle.
 *
 * Zero-initializes `self` and binds it to `nativeHandle` with the given capability flags. The flags
 * determine which IOStream operations the resulting stream permits (e.g. pass IOStreamFlags_CanRead
 * and/or IOStreamFlags_CanWrite, IOStreamFlags_CanSeek as appropriate for how the handle was
 * opened). If `ownsHandle` is true the stream will flush (when writable) and `fclose` the handle on
 * Close/Deconstruct; if false the caller retains responsibility for closing the handle. The object
 * must later be released with FileStream_Deconstruct.
 * @param self The file stream to initialize; must be non-NULL.
 * @param nativeHandle An open `FILE*` (passed as void*); must be non-NULL.
 * @param flags Capability bitmask advertising the operations this stream supports; should match how
 *        the handle was opened.
 * @param ownsHandle true to transfer ownership of the handle (stream closes it), false to leave
 *        ownership with the caller.
 * @returns Success, or ErrorCode_IllegalArgument if `self` or `nativeHandle` is NULL.
 */
Error FileStream_ConstructFromHandle(FileStream* self, void* nativeHandle, IOStreamFlags flags, bool ownsHandle);

/**
 * @brief Tears the file stream down: closes it (flushing and closing an owned handle) and zeroes it.
 *
 * Closing flushes buffered writes and `fclose`s the handle only if the stream owns it; a non-owned
 * handle (e.g. stdin/stdout) is left open. Idempotent and safe to call on an already-closed or NULL
 * stream. After this call the object must not be used again.
 * @param self The file stream to destroy; may be NULL (treated as a successful no-op).
 * @returns Success, or an error propagated from the underlying close (e.g. ErrorCode_IO if flushing
 *          or closing the handle fails).
 */
Error FileStream_Deconstruct(FileStream* self);
