#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRMemory.h"
#include "WRError.h"


// Types.
/**
 * @brief Identifies the kind of backing resource an IOStream is bound to.
 *
 * Used for diagnostics (e.g. composing error messages) and lets callers branch on the
 * concrete stream family behind the abstract IOStream interface.
 */
typedef enum IOStreamTypeEnum
{
    /** @brief The stream's backing resource is unknown or unspecified. */
    IOStreamType_Unknown = 0,
    /** @brief The stream is backed by a file or OS file handle. */
    IOStreamType_File,
    /** @brief The stream is backed by an in-memory byte buffer. */
    IOStreamType_Memory,
    /** @brief The stream is backed by a network socket. */
    IOStreamType_Socket,
} IOStreamType;

/**
 * @brief Capability bit flags advertising which operations a stream supports.
 *
 * The flags are a bitmask stored on the stream. Each public IOStream operation that needs a
 * capability checks the corresponding flag first and fails with ErrorCode_InvalidOperation if it
 * is absent; an implementation must therefore set every flag for the operations it actually
 * provides, and callers must respect them (test via IOStream_IsReadable / IOStream_IsWritable /
 * IOStream_IsSeekable / IOStream_IsLengthSettable). Combine with bitwise OR.
 */
typedef enum IOStreamFlagsEnum
{
    /** @brief No capabilities. */
    IOStreamFlags_None = 0,
    /** @brief The stream accepts writes (WriteByte / Write / WriteString / Flush / SetLength). */
    IOStreamFlags_CanWrite = (1 << 0),
    /** @brief The stream supports reads (ReadByte / Read / ReadAll). */
    IOStreamFlags_CanRead = (1 << 1),
    /** @brief The stream supports random access: querying and changing the current position. */
    IOStreamFlags_CanSeek = (1 << 2),
    /** @brief The stream's length can be explicitly set / truncated / extended via SetLength. */
    IOStreamFlags_CanSetLength = (1 << 3),
} IOStreamFlags;

/**
 * @brief Reference point for an absolute seek performed by IOStream_SetPositionSpecial.
 *
 * Selects which boundary of the stream the position is moved to.
 */
typedef enum IOStreamSeekOriginEnum
{
    /** @brief Seek to the beginning of the stream (position 0). */
    IOStreamSeekOrigin_Start,
    /** @brief Seek to the end of the stream (one past the last byte). */
    IOStreamSeekOrigin_End,
} IOStreamSeekOrigin;

/**
 * @brief Abstract byte-oriented stream: an interface over any seekable/readable/writable resource.
 *
 * Opaque handle. A concrete stream (FileStream, MemoryStream, ...) embeds an IOStream as its first
 * member and fills in the vtable; callers obtain an IOStream* via the concrete type's
 * *_AsIOStream accessor and then operate on it through the IOStream_* free functions. The exact set
 * of permitted operations is governed by the capability flags.
 */
typedef struct IOStreamStruct IOStream;

/**
 * @brief Function-pointer table defining the behavioral contract every concrete IOStream supplies.
 *
 * Each slot is the operation an implementation provides; the public IOStream_* functions dispatch
 * through these after validating arguments and capability flags. A slot may be left NULL when the
 * stream does not implement that operation, in which case the corresponding public call fails with
 * ErrorCode_InvalidState ("vtable is incomplete"). Every function pointer receives Self (the
 * concrete stream instance) as its first argument.
 */
typedef struct IOStreamVTableStruct
{
    /** @brief Opaque pointer to the concrete stream instance; passed as the `self` argument to every slot below. */
    void* Self;
    /**
     * @brief Reports the stream's current byte offset from the start.
     * @param self The concrete stream instance (the vtable's Self).
     * @param position [out] Receives the current 0-based position; must be non-NULL.
     * @returns Success and writes the position, or an error if the position cannot be determined
     *          (e.g. the underlying resource is closed or a native query fails).
     */
    Error (*_getPosition)(void* self, size_t* position);
    /**
     * @brief Sets the stream's current position to an absolute offset from the start.
     *
     * Only meaningful for seekable streams. Implementations may clamp an out-of-bounds offset to a
     * valid value or reject it, but must leave the stream in a consistent state on failure.
     * @param self The concrete stream instance (the vtable's Self).
     * @param position The target 0-based offset from the start of the stream.
     * @returns Success, or an error if the seek cannot be performed (e.g. closed stream, offset
     *          beyond the range the backing resource supports).
     */
    Error (*_setPosition)(void* self, size_t position);
    /**
     * @brief Sets the position to a stream boundary (start or end) named by a seek origin.
     * @param self The concrete stream instance (the vtable's Self).
     * @param origin The boundary to seek to; see IOStreamSeekOrigin.
     * @returns Success, or an error if the seek fails or `origin` is not a recognized value
     *          (ErrorCode_IllegalArgument).
     */
    Error (*_setPositionSpecial)(void* self, IOStreamSeekOrigin origin);
    /**
     * @brief Sets the logical length of the stream, truncating or zero-extending as needed.
     *
     * Growing the stream appends zero bytes; shrinking discards bytes beyond the new length and
     * must clamp the current position so it does not exceed the new length.
     * @param self The concrete stream instance (the vtable's Self).
     * @param length The new length in bytes.
     * @returns Success, or an error if the length cannot be changed (e.g. read-only backing store,
     *          closed stream, or capacity cannot be grown).
     */
    Error (*_setLength)(void* self, size_t length);
    /**
     * @brief Flushes any buffered written data to the backing resource.
     *
     * For non-writable streams this is a no-op that succeeds.
     * @param self The concrete stream instance (the vtable's Self).
     * @returns Success once buffered data is committed, or an error if the flush fails.
     */
    Error (*_flush)(void* self);
    /**
     * @brief Writes a single byte at the current position and advances the position by one.
     * @param self The concrete stream instance (the vtable's Self).
     * @param byte The byte value to write.
     * @returns Success, or an error if the write fails (e.g. closed stream, read-only buffer, I/O error).
     */
    Error (*_writeByte)(void* self, unsigned char byte);
    /**
     * @brief Writes `bufferSize` bytes from `buffer` at the current position and advances by that many.
     *
     * A `bufferSize` of 0 must succeed without dereferencing `buffer`. The implementation must
     * either write all bytes or return an error.
     * @param self The concrete stream instance (the vtable's Self).
     * @param buffer Source bytes; may be NULL only when `bufferSize` is 0.
     * @param bufferSize Number of bytes to write.
     * @returns Success when all bytes are written, or an error (e.g. closed stream, partial/failed I/O).
     */
    Error (*_write)(void* self, const unsigned char* buffer, size_t bufferSize);
    /**
     * @brief Reads a single byte from the current position and advances the position by one.
     * @param self The concrete stream instance (the vtable's Self).
     * @param byte [out] Receives the byte read; must be non-NULL.
     * @returns Success and writes the byte, or ErrorCode_IO when the read is at/past end-of-stream,
     *          or another error if the underlying read fails.
     */
    Error (*_readByte)(void* self, unsigned char* byte);
    /**
     * @brief Reads up to `readSize` bytes from the current position and APPENDS them to `dest`.
     *
     * `dest` must be a byte buffer (element size 1). Bytes are appended after the buffer's existing
     * contents (its `_count` grows by the number of bytes actually read); existing data is not
     * overwritten. Fewer than `readSize` bytes may be appended at or near end-of-stream, and reading
     * at end-of-stream appends nothing and still succeeds. The position advances by the number of
     * bytes actually read.
     * @param self The concrete stream instance (the vtable's Self).
     * @param dest [out] Destination byte buffer that read bytes are appended to; must be non-NULL.
     * @param readSize Maximum number of bytes to read.
     * @returns Success, or ErrorCode_IllegalArgument if `dest` is not a byte buffer,
     *          ErrorCode_BufferTooSmall if a fixed-capacity destination cannot hold the data, or an
     *          I/O error from the underlying read.
     */
    Error (*_read)(void* self, GenericBuffer* dest, size_t readSize);
    /**
     * @brief Closes the stream, flushing pending writes and releasing the backing resource if owned.
     *
     * Must be idempotent: closing an already-closed stream succeeds and does nothing. After a close,
     * read/write/seek operations on the stream fail.
     * @param self The concrete stream instance (the vtable's Self).
     * @returns Success, or an error if flushing or releasing the resource fails.
     */
    Error (*_close)(void* self);
    /**
     * @brief Reports whether the stream is positioned at (or past) the end of its data.
     * @param self The concrete stream instance (the vtable's Self).
     * @returns true if no further bytes can be read (including when the stream is closed), false otherwise.
     */
    bool (*_isEOF)(void* self);
    /**
     * @brief Tears the stream down: closes it and releases all resources it owns.
     *
     * Typically closes the stream and then zeroes/invalidates the instance. After this call the
     * instance must not be used again.
     * @param self The concrete stream instance (the vtable's Self).
     * @returns Success, or an error if the underlying close fails.
     */
    Error (*_deconstruct)(void* self);
} IOStreamVTable;

/**
 * @brief Concrete storage behind the opaque IOStream handle: stream type, capabilities, and vtable.
 *
 * Embedded as the first member of every concrete stream type so that a pointer to the stream can be
 * used as an IOStream*.
 */
struct IOStreamStruct
{
    /** @brief The kind of backing resource this stream represents. */
    IOStreamType _type;
    /** @brief Capability bitmask controlling which operations are permitted on this stream. */
    IOStreamFlags _flags;
    /** @brief Dispatch table plus the Self pointer used to invoke the concrete implementation. */
    IOStreamVTable _vtable;
};


// Functions.
/**
 * @brief Returns the stream's current byte offset from the start.
 *
 * Thin wrapper dispatching to the stream's `_getPosition` slot. Does not validate `stream` or the
 * capability flags; `stream` must be a valid, open stream.
 * @param stream The stream to query; must be non-NULL with a populated vtable.
 * @param position [out] Receives the current 0-based position; must be non-NULL.
 * @returns Success and writes the position, or an error propagated from the implementation.
 */
static inline Error IOStream_GetPosition(IOStream* stream, size_t* position)
{
    return (*stream->_vtable._getPosition)(stream->_vtable.Self, position);
}

/**
 * @brief Flushes any buffered written data to the stream's backing resource.
 *
 * Thin wrapper dispatching to the stream's `_flush` slot. Does not validate `stream`. For
 * non-writable streams the operation is a successful no-op.
 * @param stream The stream to flush; must be non-NULL with a populated vtable.
 * @returns Success once buffered data is committed, or an error propagated from the implementation.
 */
static inline Error IOStream_Flush(IOStream* stream)
{
    return (*stream->_vtable._flush)(stream->_vtable.Self);
}

/**
 * @brief Closes the stream, flushing pending writes and releasing the backing resource if owned.
 *
 * Thin wrapper dispatching to the stream's `_close` slot. Idempotent: closing an already-closed
 * stream succeeds. Does not validate `stream`. The stream object itself is not destroyed; use
 * IOStream_Deconstruct (or the concrete type's Deconstruct) to release the object.
 * @param stream The stream to close; must be non-NULL with a populated vtable.
 * @returns Success, or an error propagated from the implementation if flush/release fails.
 */
static inline Error IOStream_Close(IOStream* stream)
{
    return (*stream->_vtable._close)(stream->_vtable.Self);
}

/**
 * @brief Reports whether the stream supports random access (seeking / position changes).
 * @param stream The stream to inspect; must be non-NULL.
 * @returns true if the IOStreamFlags_CanSeek capability is set, false otherwise.
 */
static inline bool IOStream_IsSeekable(IOStream* stream)
{
    return ((stream->_flags & IOStreamFlags_CanSeek) != 0);
}

/**
 * @brief Reports whether the stream accepts writes.
 * @param stream The stream to inspect; must be non-NULL.
 * @returns true if the IOStreamFlags_CanWrite capability is set, false otherwise.
 */
static inline bool IOStream_IsWritable(IOStream* stream)
{
    return ((stream->_flags & IOStreamFlags_CanWrite) != 0);
}

/**
 * @brief Reports whether the stream supports reads.
 * @param stream The stream to inspect; must be non-NULL.
 * @returns true if the IOStreamFlags_CanRead capability is set, false otherwise.
 */
static inline bool IOStream_IsReadable(IOStream* stream)
{
    return ((stream->_flags & IOStreamFlags_CanRead) != 0);
}

/**
 * @brief Reports whether the stream's length can be explicitly set via IOStream_SetLength.
 * @param stream The stream to inspect; must be non-NULL.
 * @returns true if the IOStreamFlags_CanSetLength capability is set, false otherwise.
 */
static inline bool IOStream_IsLengthSettable(IOStream* stream)
{
    return ((stream->_flags & IOStreamFlags_CanSetLength) != 0);
}

/**
 * @brief Reports whether the stream is positioned at (or past) the end of its data.
 *
 * Thin wrapper dispatching to the stream's `_isEOF` slot. Does not validate `stream`.
 * @param stream The stream to inspect; must be non-NULL with a populated vtable.
 * @returns true if no further bytes can be read (including when the stream is closed), false otherwise.
 */
static inline bool IOStream_IsEndOfStream(IOStream* stream)
{
    return (*stream->_vtable._isEOF)(stream->_vtable.Self);
}

/**
 * @brief Sets the stream's current position to an absolute offset from the start.
 *
 * Validates the stream, requires the CanSeek capability, and requires the `_setPosition` slot to be
 * present before dispatching.
 * @param stream The stream to seek; may be NULL (reported as an error).
 * @param position The target 0-based offset from the start of the stream.
 * @returns Success, or ErrorCode_IllegalArgument if `stream` is NULL, ErrorCode_InvalidOperation if
 *          the stream is not seekable, ErrorCode_InvalidState if no seek operation is provided, or an
 *          error propagated from the implementation.
 */
Error IOStream_SetPosition(IOStream* stream, size_t position);

/**
 * @brief Seeks the stream's position to a named boundary (start or end).
 *
 * Validates the stream, requires the CanSeek capability, and requires the `_setPositionSpecial` slot
 * before dispatching.
 * @param stream The stream to seek; may be NULL (reported as an error).
 * @param origin The boundary to seek to; see IOStreamSeekOrigin.
 * @returns Success, or ErrorCode_IllegalArgument if `stream` is NULL, ErrorCode_InvalidOperation if
 *          the stream is not seekable, ErrorCode_InvalidState if no operation is provided, or an
 *          error propagated from the implementation (including ErrorCode_IllegalArgument for an
 *          unrecognized `origin`).
 */
Error IOStream_SetPositionSpecial(IOStream* stream, IOStreamSeekOrigin origin);

/**
 * @brief Sets the stream's logical length, truncating or zero-extending the data as needed.
 *
 * Validates the stream, requires the CanSetLength capability, and requires the `_setLength` slot
 * before dispatching. Shrinking discards trailing bytes (and clamps the position); growing appends
 * zero bytes.
 * @param stream The stream whose length to set; may be NULL (reported as an error).
 * @param length The new length in bytes.
 * @returns Success, or ErrorCode_IllegalArgument if `stream` is NULL, ErrorCode_InvalidOperation if
 *          the stream is not length-settable, ErrorCode_InvalidState if no operation is provided, or
 *          an error propagated from the implementation.
 */
Error IOStream_SetLength(IOStream* stream, size_t length);

/**
 * @brief Writes a single byte at the current position and advances the position by one.
 *
 * Validates the stream, requires the CanWrite capability, and requires the `_writeByte` slot before
 * dispatching.
 * @param stream The stream to write to; may be NULL (reported as an error).
 * @param byte The byte value to write.
 * @returns Success, or ErrorCode_IllegalArgument if `stream` is NULL, ErrorCode_InvalidOperation if
 *          the stream is not writable, ErrorCode_InvalidState if no operation is provided, or an I/O
 *          error propagated from the implementation.
 */
Error IOStream_WriteByte(IOStream* stream, unsigned char byte);

/**
 * @brief Writes `dataSize` bytes from `data` at the current position and advances by that many.
 *
 * Validates the stream and arguments, requires the CanWrite capability, and requires the `_write`
 * slot before dispatching. A `dataSize` of 0 is permitted with `data` NULL. Either all bytes are
 * written or an error is returned.
 * @param stream The stream to write to; may be NULL (reported as an error).
 * @param data Source bytes; may be NULL only when `dataSize` is 0 (otherwise reported as an error).
 * @param dataSize Number of bytes to write.
 * @returns Success when all bytes are written, or ErrorCode_IllegalArgument if `stream` is NULL or
 *          `data` is NULL with a non-zero size, ErrorCode_InvalidOperation if the stream is not
 *          writable, ErrorCode_InvalidState if no operation is provided, or an I/O error from the
 *          implementation.
 */
Error IOStream_Write(IOStream* stream, const unsigned char* data, size_t dataSize);

/**
 * @brief Reads a single byte from the current position and advances the position by one.
 *
 * Validates the stream and out-parameter, requires the CanRead capability, and requires the
 * `_readByte` slot before dispatching.
 * @param stream The stream to read from; may be NULL (reported as an error).
 * @param byte [out] Receives the byte read; must be non-NULL.
 * @returns Success and writes the byte, or ErrorCode_IllegalArgument if `stream` or `byte` is NULL,
 *          ErrorCode_InvalidOperation if the stream is not readable, ErrorCode_InvalidState if no
 *          operation is provided, ErrorCode_IO at end-of-stream, or another error from the
 *          implementation.
 */
Error IOStream_ReadByte(IOStream* stream, unsigned char* byte);

/**
 * @brief Reads up to `bytesToRead` bytes from the current position and APPENDS them to `outBuffer`.
 *
 * Validates the stream and destination, requires the CanRead capability, and requires the `_read`
 * slot before dispatching. Read bytes are appended after the buffer's existing contents (its
 * `_count` grows by the number of bytes actually read); existing data is preserved. Fewer than
 * `bytesToRead` bytes may be appended near end-of-stream, and reading at end-of-stream appends
 * nothing and still succeeds. The destination must be a byte buffer (element size 1).
 * @param stream The stream to read from; may be NULL (reported as an error).
 * @param bytesToRead Maximum number of bytes to read.
 * @param outBuffer [out] Destination byte buffer that read bytes are appended to; must be non-NULL.
 * @returns Success, or ErrorCode_IllegalArgument if `stream` or `outBuffer` is NULL (or the
 *          destination is not a byte buffer), ErrorCode_InvalidOperation if the stream is not
 *          readable, ErrorCode_InvalidState if no operation is provided, ErrorCode_BufferTooSmall if
 *          a fixed-capacity destination cannot hold the data, or an I/O error from the implementation.
 */
Error IOStream_Read(IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer);

/**
 * @brief Computes the total size of the stream in bytes (its full length, independent of position).
 *
 * Requires a seekable stream: works by recording the current position, seeking to the end to read
 * the length, then restoring the original position. The position is left unchanged on success.
 * @param stream The stream to measure; may be NULL (reported as an error).
 * @param sizeBytes [out] Receives the total byte length; must be non-NULL.
 * @returns Success and writes the size, or ErrorCode_IllegalArgument if `stream` or `sizeBytes` is
 *          NULL, ErrorCode_InvalidOperation if the stream is not seekable, or an error propagated
 *          from the seek/position operations used internally.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error IOStream_GetStreamSizeTotal(IOStream* stream, size_t* sizeBytes);

/**
 * @brief Computes the number of bytes between the current position and the end of the stream.
 *
 * Requires a seekable stream. Equals total size minus current position.
 * @param stream The stream to measure; may be NULL (reported as an error).
 * @param sizeBytes [out] Receives the remaining byte count; must be non-NULL.
 * @returns Success and writes the remaining size, or ErrorCode_IllegalArgument if `stream` or
 *          `sizeBytes` is NULL, ErrorCode_InvalidOperation if the stream is not seekable,
 *          ErrorCode_InvalidState if the current position somehow exceeds the total size, or an error
 *          propagated from the operations used internally.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error IOStream_GetStreamSizeRemaining(IOStream* stream, size_t* sizeBytes);

/**
 * @brief Moves the stream's position by a signed relative amount (forward or backward).
 *
 * Reads the current position and seeks to position + `amount`. A negative move that would go before
 * the start is clamped to position 0 (it does not error). A positive move whose target would
 * overflow size_t is rejected.
 * @param stream The stream to move; may be NULL (reported as an error).
 * @param amount Signed byte delta to apply to the current position; negative moves backward.
 * @returns Success, or ErrorCode_IllegalArgument if `stream` is NULL, ErrorCode_ArgumentOutOfRange
 *          if a forward move would overflow, or an error propagated from the get/set position
 *          operations (e.g. the stream is not seekable).
 */
Error IOStream_Move(IOStream* stream, int64_t amount);

/**
 * @brief Writes a NUL-terminated UTF-8 string to the stream, excluding the terminator.
 *
 * Measures the string up to (but not including) its terminating null byte and writes those bytes via
 * IOStream_Write. The trailing null is not written.
 * @param stream The stream to write to; forwarded to IOStream_Write and validated there.
 * @param str NUL-terminated UTF-8 string; must be non-NULL.
 * @returns Success, or ErrorCode_IllegalArgument if `str` is NULL, plus any error IOStream_Write
 *          raises (NULL stream, not writable, missing operation, I/O error).
 */
Error IOStream_WriteString(IOStream* stream, const unsigned char* str);

/**
 * @brief Reads the entire remaining stream content and APPENDS it to `buffer`.
 *
 * Requires the CanRead capability. For seekable streams the remaining size is queried and read in
 * one shot; for non-seekable streams the stream is drained in chunks until a read returns no further
 * bytes. In both cases bytes are appended after the buffer's existing contents (its `_count` grows);
 * existing data is preserved. The destination must be a byte buffer (element size 1).
 * @param stream The stream to drain; may be NULL (reported as an error).
 * @param buffer [out] Destination byte buffer that all read bytes are appended to; must be non-NULL.
 * @returns Success, or ErrorCode_IllegalArgument if `stream` or `buffer` is NULL,
 *          ErrorCode_InvalidOperation if the stream is not readable, or an error propagated from the
 *          underlying size/read operations.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error IOStream_ReadAll(IOStream* stream, GenericBuffer* buffer);

/**
 * @brief Tears the stream down: closes it and releases all resources it owns.
 *
 * Dispatches to the stream's `_deconstruct` slot. Safe to call with a NULL `stream` or a stream
 * whose deconstruct slot is unset — both are treated as a successful no-op. After this call the
 * stream object must not be used again.
 * @param stream The stream to destroy; may be NULL.
 * @returns Success (including the NULL / no-op cases), or an error propagated from the implementation.
 */
Error IOStream_Deconstruct(IOStream* stream);
