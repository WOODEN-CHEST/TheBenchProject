#pragma once
#include "WRFileStream.h"


// Types.
/**
 * @brief IOStream bound to one of the process's standard streams (stdin, stdout, or stderr).
 *
 * A thin specialization of FileStream whose handle is a standard C stream and is NOT owned: the
 * underlying `stdin`/`stdout`/`stderr` is never closed by this stream. The embedded FileStream
 * (`Base`) carries the implementation; obtain an IOStream* via StandardStream_AsIOStream. Create with
 * one of the StandardStream_CreateFrom* factory functions and release with StandardStream_Deconstruct.
 */
typedef struct StandardStreamStruct
{
    /** @brief Embedded FileStream wrapping the (non-owned) standard C stream handle. */
    FileStream Base;
} StandardStream;


// Functions.
/**
 * @brief Returns the embedded IOStream interface for this standard stream.
 *
 * The returned pointer aliases the stream's storage and is valid only as long as the StandardStream
 * itself; it must not be freed independently.
 * @param self The standard stream; must be non-NULL.
 * @returns A non-owning IOStream* through which the generic IOStream_* operations can be invoked.
 */
static inline IOStream* StandardStream_AsIOStream(StandardStream* self)
{
    return FileStream_AsIOStream(&self->Base);
}

/**
 * @brief Initializes a read-only standard stream bound to the process's standard input (stdin).
 *
 * Wraps `stdin` with the IOStreamFlags_CanRead capability and does not take ownership of the handle
 * (stdin is never closed). The object must later be released with StandardStream_Deconstruct.
 * @param self The standard stream to initialize; must be non-NULL.
 * @returns Success, or ErrorCode_IllegalArgument if `self` is NULL.
 */
Error StandardStream_CreateFromStandardInput(StandardStream* self);

/**
 * @brief Initializes a write-only standard stream bound to the process's standard output (stdout).
 *
 * Wraps `stdout` with the IOStreamFlags_CanWrite capability and does not take ownership of the handle
 * (stdout is never closed). The object must later be released with StandardStream_Deconstruct.
 * @param self The standard stream to initialize; must be non-NULL.
 * @returns Success, or ErrorCode_IllegalArgument if `self` is NULL.
 */
Error StandardStream_CreateFromStandardOutput(StandardStream* self);

/**
 * @brief Initializes a write-only standard stream bound to the process's standard error (stderr).
 *
 * Wraps `stderr` with the IOStreamFlags_CanWrite capability and does not take ownership of the handle
 * (stderr is never closed). The object must later be released with StandardStream_Deconstruct.
 * @param self The standard stream to initialize; must be non-NULL.
 * @returns Success, or ErrorCode_IllegalArgument if `self` is NULL.
 */
Error StandardStream_CreateFromStandardError(StandardStream* self);

/**
 * @brief Tears the standard stream down, releasing the wrapper without closing the standard handle.
 *
 * Delegates to the embedded FileStream's teardown: the stream is closed and zeroed, but because the
 * standard handle is not owned, the underlying `stdin`/`stdout`/`stderr` is left open. Idempotent and
 * safe to call on a NULL stream.
 * @param self The standard stream to destroy; may be NULL (treated as a successful no-op).
 * @returns Success, or an error propagated from the underlying file-stream teardown.
 */
Error StandardStream_Deconstruct(StandardStream* self);
