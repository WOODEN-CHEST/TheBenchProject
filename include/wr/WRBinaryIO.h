#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include "WREnvironment.h"
#include <stdint.h>
#include <stddef.h>
#include "WRIO.h"

/**
 * @brief Size, in bytes, of the per-stream scratch buffer used to stage a single value during
 *        binary conversion.
 *
 * Bounds the largest value (or encoded-integer span) a BinaryIOStream can read or write in one
 * operation; 16 comfortably accommodates every fixed-width scalar (up to 8 bytes) and every
 * variable-length encoded integer (up to 10 bytes).
 */
#define BINARY_STREAM_TEMP_BUFFER_SIZE 16


// Types.
/**
 * @brief Stateless byte-order converter that serializes scalar values to, and deserializes them
 *        from, a configurable target byte order.
 *
 * A BinaryConverter holds only the desired target endianness. Its writer functions append the
 * raw little/big-endian byte representation of a value to a caller-supplied byte buffer, and its
 * reader functions parse such bytes back into a typed value. Whenever the host machine's native
 * endianness (as reported by Environment_GetEndianess()) differs from the configured target, the
 * byte order is reversed during conversion; otherwise bytes are copied verbatim. Must be
 * initialized with one of the BinaryConverter_Construct overloads before use and released with
 * BinaryConverter_Deconstruct.
 */
typedef struct BinaryConverterStruct
{
    /** Target byte order for serialized data; must be MachineEndianess_LittleEndian or
     *  MachineEndianess_BigEndian. Conversion reverses bytes only when this differs from the
     *  host's native endianness. */
    MachineEndianess _targetEndianness;
} BinaryConverter;

/**
 * @brief Dispatch table of typed binary read/write operations exposed by a BinaryIOStream.
 *
 * Each slot performs one strongly-typed serialization or deserialization against the stream
 * referenced by @ref Self, delegating to the wrapped IOStream and applying the stream's configured
 * endianness (and, for the encoded variants, LEB128-style variable-length encoding). The table is
 * populated by the BinaryIOStream constructors; the slots are not intended to be invoked directly
 * by callers, who should instead use the corresponding BinaryIOStream_* wrapper functions. The
 * abstract contract each slot must honor is described per slot below; every slot returns a success
 * Error on success or a non-success Error (carrying a heap-allocated message that the caller must
 * release with Error_Deconstruct) on failure, and every slot requires the stream to be open with a
 * live wrapped stream.
 */
typedef struct BinaryIOStreamVTableStruct
{
    /** Owning object passed back as the @c self argument of every slot; points to the
     *  BinaryIOStream that produced this table. */
    void* Self;
    /** Write one signed 8-bit value to the wrapped stream. Implementations must serialize the
     *  value in the stream's target byte order and return an Error reporting any I/O or state
     *  failure. */
    Error (*_writeInt8)(void* self, int8_t value);
    /** Write one unsigned 8-bit value to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeUInt8)(void* self, uint8_t value);
    /** Write one signed 16-bit value to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeInt16)(void* self, int16_t value);
    /** Write one unsigned 16-bit value to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeUInt16)(void* self, uint16_t value);
    /** Write one signed 32-bit value to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeInt32)(void* self, int32_t value);
    /** Write one unsigned 32-bit value to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeUInt32)(void* self, uint32_t value);
    /** Write one signed 64-bit value to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeInt64)(void* self, int64_t value);
    /** Write one unsigned 64-bit value to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeUInt64)(void* self, uint64_t value);
    /** Write one 32-bit IEEE-754 float to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeFloat)(void* self, float value);
    /** Write one 64-bit IEEE-754 double to the wrapped stream in the stream's target byte order;
     *  return an Error on failure. */
    Error (*_writeDouble)(void* self, double value);
    /** Write one boolean as a single byte (0 or 1) to the wrapped stream; return an Error on
     *  failure. */
    Error (*_writeBoolean)(void* self, bool value);
    /** Write one signed 32-bit value using variable-length encoding (1-5 bytes, endianness
     *  independent); return an Error on failure. */
    Error (*_writeEncodedInt32)(void* self, int32_t value);
    /** Write one unsigned 32-bit value using variable-length encoding (1-5 bytes, endianness
     *  independent); return an Error on failure. */
    Error (*_writeEncodedUInt32)(void* self, uint32_t value);
    /** Write one signed 64-bit value using variable-length encoding (1-10 bytes, endianness
     *  independent); return an Error on failure. */
    Error (*_writeEncodedInt64)(void* self, int64_t value);
    /** Write one unsigned 64-bit value using variable-length encoding (1-10 bytes, endianness
     *  independent); return an Error on failure. */
    Error (*_writeEncodedUInt64)(void* self, uint64_t value);

    /** Read one signed 8-bit value from the wrapped stream. Implementations must consume the
     *  fixed-width representation, apply the stream's byte order, store the result in [out] @p value
     *  (which must be non-NULL), and return an Error if the bytes are unavailable. */
    Error (*_readInt8)(void* self, int8_t* value);
    /** Read one unsigned 8-bit value into [out] @p value (non-NULL) using the stream's byte order;
     *  return an Error on failure. */
    Error (*_readUInt8)(void* self, uint8_t* value);
    /** Read one signed 16-bit value into [out] @p value (non-NULL) using the stream's byte order;
     *  return an Error on failure. */
    Error (*_readInt16)(void* self, int16_t* value);
    /** Read one unsigned 16-bit value into [out] @p value (non-NULL) using the stream's byte order;
     *  return an Error on failure. */
    Error (*_readUInt16)(void* self, uint16_t* value);
    /** Read one signed 32-bit value into [out] @p value (non-NULL) using the stream's byte order;
     *  return an Error on failure. */
    Error (*_readInt32)(void* self, int32_t* value);
    /** Read one unsigned 32-bit value into [out] @p value (non-NULL) using the stream's byte order;
     *  return an Error on failure. */
    Error (*_readUInt32)(void* self, uint32_t* value);
    /** Read one signed 64-bit value into [out] @p value (non-NULL) using the stream's byte order;
     *  return an Error on failure. */
    Error (*_readInt64)(void* self, int64_t* value);
    /** Read one unsigned 64-bit value into [out] @p value (non-NULL) using the stream's byte order;
     *  return an Error on failure. */
    Error (*_readUInt64)(void* self, uint64_t* value);
    /** Read one 32-bit IEEE-754 float into [out] @p value (non-NULL) using the stream's byte order;
     *  return an Error on failure. */
    Error (*_readFloat)(void* self, float* value);
    /** Read one 64-bit IEEE-754 double into [out] @p value (non-NULL) using the stream's byte
     *  order; return an Error on failure. */
    Error (*_readDouble)(void* self, double* value);
    /** Read one boolean byte into [out] @p value (non-NULL). Implementations must reject any byte
     *  other than 0 or 1 with an Error. */
    Error (*_readBoolean)(void* self, bool* value);
    /** Read one variable-length-encoded signed 32-bit value into [out] @p value (non-NULL);
     *  return an Error if the encoding is truncated or malformed. */
    Error (*_readEncodedInt32)(void* self, int32_t* value);
    /** Read one variable-length-encoded unsigned 32-bit value into [out] @p value (non-NULL);
     *  return an Error if the encoding is truncated or malformed. */
    Error (*_readEncodedUInt32)(void* self, uint32_t* value);
    /** Read one variable-length-encoded signed 64-bit value into [out] @p value (non-NULL);
     *  return an Error if the encoding is truncated or malformed. */
    Error (*_readEncodedInt64)(void* self, int64_t* value);
    /** Read one variable-length-encoded unsigned 64-bit value into [out] @p value (non-NULL);
     *  return an Error if the encoding is truncated or malformed. */
    Error (*_readEncodedUInt64)(void* self, uint64_t* value);
} BinaryIOStreamVTable;

/**
 * @brief An IOStream decorator that adds typed binary (de)serialization over an underlying stream.
 *
 * A BinaryIOStream wraps another IOStream and layers endianness-aware reading and writing of
 * fixed-width scalars, booleans, and variable-length-encoded integers on top of it. It is itself
 * an IOStream: its @ref Base sub-object forwards the generic stream operations (position, length,
 * raw byte/buffer read and write, flush, EOF, close) to the wrapped stream, so a BinaryIOStream
 * can be passed wherever an IOStream* is expected. All typed and raw operations fail once the
 * stream has been closed. Initialize with a BinaryIOStream_Construct overload and release with
 * BinaryIOStream_Deconstruct; depending on the @ref _ownsWrappedStream flag chosen at construction,
 * closing or deconstructing may also close/deconstruct the wrapped stream.
 */
typedef struct BinaryIOStreamStruct
{
    /** Embedded IOStream interface for this object; its vtable forwards generic stream calls to the
     *  wrapped stream. Allows a BinaryIOStream* to be used as an IOStream*. */
    IOStream Base;
    /** The underlying stream that actually stores or supplies the bytes. Not owned unless
     *  @ref _ownsWrappedStream is true. Must remain valid for the lifetime of this object. */
    IOStream* _wrappedStream;
    /** Byte-order converter used to serialize/deserialize fixed-width values; its target endianness
     *  determines the on-wire byte order. */
    BinaryConverter _converter;
    /** Dispatch table of typed read/write operations bound to this stream. */
    BinaryIOStreamVTable _vtable;
    /** Scratch buffer used to stage the bytes of a single value during conversion; sized by
     *  BINARY_STREAM_TEMP_BUFFER_SIZE, which bounds the largest value that can pass through. */
    unsigned char _tempBuffer[BINARY_STREAM_TEMP_BUFFER_SIZE];
    /** True once the stream has been closed; further typed or raw operations then fail with
     *  ErrorCode_InvalidOperation. */
    bool _isClosed;
    /** When true, this stream takes ownership of the wrapped stream and will close it on close and
     *  deconstruct it on deconstruction; when false, the wrapped stream's lifetime is the caller's
     *  responsibility. */
    bool _ownsWrappedStream;
} BinaryIOStream;



// Functions.

/**
 * @brief Initializes a converter whose target byte order is the host machine's native endianness.
 *
 * Because the target then matches the host, conversions performed by this converter copy bytes
 * without reordering. The converter must later be released with BinaryConverter_Deconstruct.
 * @param self Converter to initialize. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL.
 */
Error BinaryConverter_Construct1(BinaryConverter* self);

/**
 * @brief Initializes a converter with an explicit target byte order.
 *
 * Conversions reverse bytes whenever @p targetEndianness differs from the host's native
 * endianness. The converter must later be released with BinaryConverter_Deconstruct.
 * @param self Converter to initialize. Must not be NULL.
 * @param targetEndianness Desired on-wire byte order; must be MachineEndianess_LittleEndian or
 *        MachineEndianess_BigEndian.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL or if @p targetEndianness
 *          is not a recognized endianness value.
 */
Error BinaryConverter_Construct2(BinaryConverter* self, MachineEndianess targetEndianness);

/**
 * @brief Changes the converter's target byte order for subsequent conversions.
 *
 * The new value takes effect on the next read/write; it is not validated here, so passing an
 * unrecognized value defers the resulting ErrorCode_IllegalArgument to the next conversion call.
 * @param self Converter to modify. Must not be NULL (not checked).
 * @param targetEndianness New target byte order; should be MachineEndianess_LittleEndian or
 *        MachineEndianess_BigEndian.
 * @returns Always success.
 */
static inline Error BinaryConverter_SetTargetEndianness(BinaryConverter* self, MachineEndianess targetEndianness)
{
    self->_targetEndianness = targetEndianness;
    return Error_CreateSuccess();
}

/**
 * @brief Appends the 1-byte representation of a signed 8-bit value to a byte buffer.
 *
 * Stages the value's bytes (reordered to the target endianness when it differs from the host),
 * reserves space at the buffer's writable tail, copies them in, and commits the new bytes so the
 * buffer's count grows by sizeof(value). For a single byte no reordering is observable.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must have an element
 *        size of 1 (a byte buffer).
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self or @p destination is NULL or the
 *          converter endianness is invalid, ErrorCode_IllegalArgument if @p destination is not a
 *          byte buffer, or ErrorCode_BufferTooSmall if the buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteInt8(BinaryConverter* self, GenericBuffer* destination, int8_t value);

/**
 * @brief Appends the 1-byte representation of an unsigned 8-bit value to a byte buffer.
 *
 * Behaves like BinaryConverter_WriteInt8 but for an unsigned 8-bit value; grows @p destination by
 * one byte on success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteUInt8(BinaryConverter* self, GenericBuffer* destination, uint8_t value);

/**
 * @brief Appends the 2-byte representation of a signed 16-bit value to a byte buffer.
 *
 * Serializes the value in the converter's target byte order (bytes reversed when the target differs
 * from the host) and grows @p destination by 2 bytes on success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteInt16(BinaryConverter* self, GenericBuffer* destination, int16_t value);

/**
 * @brief Appends the 2-byte representation of an unsigned 16-bit value to a byte buffer.
 *
 * Serializes the value in the converter's target byte order and grows @p destination by 2 bytes on
 * success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteUInt16(BinaryConverter* self, GenericBuffer* destination, uint16_t value);

/**
 * @brief Appends the 4-byte representation of a signed 32-bit value to a byte buffer.
 *
 * Serializes the value in the converter's target byte order and grows @p destination by 4 bytes on
 * success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteInt32(BinaryConverter* self, GenericBuffer* destination, int32_t value);

/**
 * @brief Appends the 4-byte representation of an unsigned 32-bit value to a byte buffer.
 *
 * Serializes the value in the converter's target byte order and grows @p destination by 4 bytes on
 * success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteUInt32(BinaryConverter* self, GenericBuffer* destination, uint32_t value);

/**
 * @brief Appends the 8-byte representation of a signed 64-bit value to a byte buffer.
 *
 * Serializes the value in the converter's target byte order and grows @p destination by 8 bytes on
 * success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteInt64(BinaryConverter* self, GenericBuffer* destination, int64_t value);

/**
 * @brief Appends the 8-byte representation of an unsigned 64-bit value to a byte buffer.
 *
 * Serializes the value in the converter's target byte order and grows @p destination by 8 bytes on
 * success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteUInt64(BinaryConverter* self, GenericBuffer* destination, uint64_t value);

/**
 * @brief Appends the 4-byte IEEE-754 representation of a float to a byte buffer.
 *
 * Serializes the float's raw bytes in the converter's target byte order and grows @p destination by
 * 4 bytes on success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteFloat(BinaryConverter* self, GenericBuffer* destination, float value);

/**
 * @brief Appends the 8-byte IEEE-754 representation of a double to a byte buffer.
 *
 * Serializes the double's raw bytes in the converter's target byte order and grows @p destination
 * by 8 bytes on success.
 * @param self Converter supplying the target byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the value.
 */
Error BinaryConverter_WriteDouble(BinaryConverter* self, GenericBuffer* destination, double value);

/**
 * @brief Appends a single byte encoding a boolean (1 for true, 0 for false) to a byte buffer.
 *
 * Always writes exactly one byte whose value is 0 or 1; grows @p destination by 1 byte on success.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The boolean to serialize.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if the
 *          buffer cannot grow to hold the byte.
 */
Error BinaryConverter_WriteBoolean(BinaryConverter* self, GenericBuffer* destination, bool value);

/**
 * @brief Appends a signed 32-bit value to a byte buffer using variable-length (LEB128-style)
 *        encoding.
 *
 * The value's raw 32-bit pattern is reinterpreted as unsigned (no zig-zag transform) and emitted
 * 7 bits per byte, least-significant group first, with the high bit of each byte set while more
 * groups remain; this consumes 1 to 5 bytes. The encoding is byte-order independent, so the
 * converter's target endianness does not affect the output. Note that negative values therefore
 * always encode to the full 5 bytes.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to encode.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if a
 *          byte cannot be appended to @p destination.
 */
Error BinaryConverter_EncodeInt32(BinaryConverter* self, GenericBuffer* destination, int32_t value);

/**
 * @brief Appends an unsigned 32-bit value to a byte buffer using variable-length (LEB128-style)
 *        encoding.
 *
 * Emits 7 bits per byte, least-significant group first, with a continuation high bit while more
 * groups remain, consuming 1 to 5 bytes. The encoding is byte-order independent.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to encode.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if a
 *          byte cannot be appended to @p destination.
 */
Error BinaryConverter_EncodeUInt32(BinaryConverter* self, GenericBuffer* destination, uint32_t value);

/**
 * @brief Appends a signed 64-bit value to a byte buffer using variable-length (LEB128-style)
 *        encoding.
 *
 * The value's raw 64-bit pattern is reinterpreted as unsigned (no zig-zag transform) and emitted
 * 7 bits per byte, least-significant group first, with a continuation high bit while more groups
 * remain, consuming 1 to 10 bytes. The encoding is byte-order independent, so negative values
 * always encode to the full 10 bytes.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to encode.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if a
 *          byte cannot be appended to @p destination.
 */
Error BinaryConverter_EncodeInt64(BinaryConverter* self, GenericBuffer* destination, int64_t value);

/**
 * @brief Appends an unsigned 64-bit value to a byte buffer using variable-length (LEB128-style)
 *        encoding.
 *
 * Emits 7 bits per byte, least-significant group first, with a continuation high bit while more
 * groups remain, consuming 1 to 10 bytes. The encoding is byte-order independent.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param destination Growable byte buffer to append to. Must not be NULL and must be a byte buffer.
 * @param value The value to encode.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p destination, invalid
 *          converter endianness, or a non-byte @p destination, and ErrorCode_BufferTooSmall if a
 *          byte cannot be appended to @p destination.
 */
Error BinaryConverter_EncodeUInt64(BinaryConverter* self, GenericBuffer* destination, uint64_t value);

/**
 * @brief Deserializes a signed 8-bit value from the front of a byte buffer.
 *
 * Reads the leading 1 byte of @p source, applies the converter's target byte order (reversing when
 * it differs from the host), and stores the result in [out] @p value. Exactly sizeof(value) bytes
 * are consumed from the start of @p source; trailing bytes are ignored and no consumed-count is
 * reported (it is fixed by the type).
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self, @p source, or @p value is NULL or
 *          the converter endianness is invalid, or ErrorCode_BufferTooSmall if @p maxSourceLength
 *          is less than the value size.
 */
Error BinaryConverter_ReadInt8(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int8_t* value);

/**
 * @brief Deserializes an unsigned 8-bit value from the front of a byte buffer.
 *
 * Consumes the leading 1 byte of @p source (byte order applied) and stores it in [out] @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadUInt8(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint8_t* value);

/**
 * @brief Deserializes a signed 16-bit value from the front of a byte buffer.
 *
 * Consumes the leading 2 bytes of @p source (byte order applied) and stores the result in [out]
 * @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadInt16(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int16_t* value);

/**
 * @brief Deserializes an unsigned 16-bit value from the front of a byte buffer.
 *
 * Consumes the leading 2 bytes of @p source (byte order applied) and stores the result in [out]
 * @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadUInt16(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint16_t* value);

/**
 * @brief Deserializes a signed 32-bit value from the front of a byte buffer.
 *
 * Consumes the leading 4 bytes of @p source (byte order applied) and stores the result in [out]
 * @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadInt32(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int32_t* value);

/**
 * @brief Deserializes an unsigned 32-bit value from the front of a byte buffer.
 *
 * Consumes the leading 4 bytes of @p source (byte order applied) and stores the result in [out]
 * @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadUInt32(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint32_t* value);

/**
 * @brief Deserializes a signed 64-bit value from the front of a byte buffer.
 *
 * Consumes the leading 8 bytes of @p source (byte order applied) and stores the result in [out]
 * @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadInt64(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int64_t* value);

/**
 * @brief Deserializes an unsigned 64-bit value from the front of a byte buffer.
 *
 * Consumes the leading 8 bytes of @p source (byte order applied) and stores the result in [out]
 * @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadUInt64(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint64_t* value);

/**
 * @brief Deserializes a 32-bit IEEE-754 float from the front of a byte buffer.
 *
 * Consumes the leading 4 bytes of @p source (byte order applied) and stores the reconstructed float
 * in [out] @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadFloat(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, float* value);

/**
 * @brief Deserializes a 64-bit IEEE-754 double from the front of a byte buffer.
 *
 * Consumes the leading 8 bytes of @p source (byte order applied) and stores the reconstructed
 * double in [out] @p value.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least the value
 *        size.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value or
 *          invalid converter endianness, or ErrorCode_BufferTooSmall if @p maxSourceLength is too
 *          small.
 */
Error BinaryConverter_ReadDouble(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, double* value);

/**
 * @brief Deserializes a boolean from the leading byte of a byte buffer, accepting only 0 or 1.
 *
 * Consumes the leading 1 byte of @p source and stores false for 0 or true for 1 in [out] @p value;
 * any other byte value is rejected.
 * @param self Converter supplying the source byte order. Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source; must be at least 1.
 * @param value [out] Receives the decoded boolean. Must not be NULL (checked first).
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p value/@p self/@p source or
 *          invalid converter endianness, ErrorCode_BufferTooSmall if @p maxSourceLength is 0, or
 *          ErrorCode_Deserialize if the byte read is neither 0 nor 1.
 */
Error BinaryConverter_ReadBoolean(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, bool* value);

/**
 * @brief Decodes a variable-length-encoded signed 32-bit value from the front of a byte buffer.
 *
 * Reads continuation-flagged 7-bit groups (least-significant first) starting at @p source until a
 * byte without the high bit set is found, reconstructs the 32-bit pattern (reinterpreted as the
 * signed result, inverse of BinaryConverter_EncodeInt32), and reports how many bytes were consumed.
 * At most 5 bytes are examined.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read the encoded integer from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source.
 * @param value [out] Receives the decoded value. Must not be NULL (checked first).
 * @param outBytesRead [out] Receives the number of bytes consumed (1-5). Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p value/@p self/@p source/
 *          @p outBytesRead or invalid converter endianness; ErrorCode_DecodeError if the encoding
 *          is truncated (source ends before a terminating byte) or malformed/overlong for 32 bits.
 */
Error BinaryConverter_DecodeInt32(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    int32_t* value,
    size_t* outBytesRead);

/**
 * @brief Decodes a variable-length-encoded unsigned 32-bit value from the front of a byte buffer.
 *
 * Reads continuation-flagged 7-bit groups (least-significant first) until a terminating byte,
 * reconstructs the unsigned value, and reports the number of bytes consumed. At most 5 bytes are
 * examined.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read the encoded integer from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source.
 * @param value [out] Receives the decoded value. Must not be NULL (checked first).
 * @param outBytesRead [out] Receives the number of bytes consumed (1-5). Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p value/@p self/@p source/
 *          @p outBytesRead or invalid converter endianness; ErrorCode_DecodeError if the encoding
 *          is truncated or malformed/overlong for 32 bits.
 */
Error BinaryConverter_DecodeUInt32(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    uint32_t* value,
    size_t* outBytesRead);

/**
 * @brief Decodes a variable-length-encoded signed 64-bit value from the front of a byte buffer.
 *
 * Reads continuation-flagged 7-bit groups (least-significant first) until a terminating byte,
 * reconstructs the 64-bit pattern (reinterpreted as the signed result, inverse of
 * BinaryConverter_EncodeInt64), and reports the number of bytes consumed. At most 10 bytes are
 * examined.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read the encoded integer from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source.
 * @param value [out] Receives the decoded value. Must not be NULL (checked first).
 * @param outBytesRead [out] Receives the number of bytes consumed (1-10). Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p value/@p self/@p source/
 *          @p outBytesRead or invalid converter endianness; ErrorCode_DecodeError if the encoding
 *          is truncated or malformed/overlong for 64 bits.
 */
Error BinaryConverter_DecodeInt64(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    int64_t* value,
    size_t* outBytesRead);

/**
 * @brief Decodes a variable-length-encoded unsigned 64-bit value from the front of a byte buffer.
 *
 * Reads continuation-flagged 7-bit groups (least-significant first) until a terminating byte,
 * reconstructs the unsigned value, and reports the number of bytes consumed. At most 10 bytes are
 * examined.
 * @param self Converter (validated for endianness). Must not be NULL and must have a valid
 *        endianness.
 * @param source Buffer to read the encoded integer from. Must not be NULL.
 * @param maxSourceLength Number of valid bytes available at @p source.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @param outBytesRead [out] Receives the number of bytes consumed (1-10). Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument for a NULL @p self/@p source/@p value/
 *          @p outBytesRead or invalid converter endianness; ErrorCode_DecodeError if the encoding
 *          is truncated or malformed/overlong for 64 bits.
 */
Error BinaryConverter_DecodeUInt64(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    uint64_t* value,
    size_t* outBytesRead);

/**
 * @brief Releases a converter, zeroing its state.
 *
 * The converter holds no owned resources, so this simply clears its fields. Safe to call with a
 * NULL @p self (no-op) and idempotent.
 * @param self Converter to release, or NULL.
 */
void BinaryConverter_Deconstruct(BinaryConverter* self);



/**
 * @brief Initializes a binary stream wrapping an existing IOStream, using the host's native
 *        endianness as the target byte order.
 *
 * Zero-initializes @p self, sets up its embedded converter (native endianness) and dispatch tables,
 * inherits the wrapped stream's type and flags into the embedded IOStream interface, and records
 * the wrapped stream and its ownership. After construction @p self may be used both as a
 * BinaryIOStream and (through its @ref BinaryIOStream::Base) as a generic IOStream. Must be released
 * with BinaryIOStream_Deconstruct.
 * @param self Stream object to initialize. Must not be NULL.
 * @param wrappedStream The underlying stream to read from / write to. Must not be NULL and must
 *        remain valid until this stream is closed/deconstructed.
 * @param ownsWrappedStream If true, this stream will close @p wrappedStream when closed and
 *        deconstruct it when deconstructed; if false, the caller retains responsibility for the
 *        wrapped stream's lifetime.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self or @p wrappedStream is NULL.
 */
Error BinaryIOStream_Construct1(BinaryIOStream* self, IOStream* wrappedStream, bool ownsWrappedStream);

/**
 * @brief Initializes a binary stream wrapping an existing IOStream with an explicit target byte
 *        order.
 *
 * Like BinaryIOStream_Construct1 but the embedded converter is configured with @p targetEndianness,
 * so all fixed-width values are serialized/deserialized in that byte order. Must be released with
 * BinaryIOStream_Deconstruct.
 * @param self Stream object to initialize. Must not be NULL.
 * @param wrappedStream The underlying stream to read from / write to. Must not be NULL and must
 *        remain valid until this stream is closed/deconstructed.
 * @param targetEndianness On-wire byte order for fixed-width values; must be
 *        MachineEndianess_LittleEndian or MachineEndianess_BigEndian.
 * @param ownsWrappedStream If true, this stream will close @p wrappedStream when closed and
 *        deconstruct it when deconstructed; if false, the caller retains ownership.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self or @p wrappedStream is NULL, or if
 *          @p targetEndianness is not a recognized endianness value.
 */
Error BinaryIOStream_Construct2(BinaryIOStream* self, IOStream* wrappedStream, MachineEndianess targetEndianness, bool ownsWrappedStream);

/**
 * @brief Changes the byte order used for subsequent fixed-width reads and writes on this stream.
 *
 * Forwards to the embedded converter; the new value is not validated here, so an unrecognized value
 * defers the resulting error to the next conversion. Does not affect variable-length encoded
 * integers, which are byte-order independent.
 * @param self Stream to modify. Must not be NULL (not checked; @p self->_converter is accessed).
 * @param targetEndianness New target byte order; should be MachineEndianess_LittleEndian or
 *        MachineEndianess_BigEndian.
 * @returns Always success.
 */
static inline Error BinaryIOStream_SetTargetEndianness(BinaryIOStream* self, MachineEndianess targetEndianness)
{
    return BinaryConverter_SetTargetEndianness(&self->_converter, targetEndianness);
}

/**
 * @brief Writes a signed 8-bit value to the wrapped stream in the stream's target byte order.
 *
 * Serializes the value (1 byte) and forwards it to the underlying stream. Requires the stream to be
 * open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if the stream is closed, or ErrorCode_InvalidState if it has no wrapped stream.
 * @note May propagate errors from the wrapped stream's write; consult IOStream_Write for the full
 *       set.
 */
Error BinaryIOStream_WriteInt8(BinaryIOStream* self, int8_t value);

/**
 * @brief Writes an unsigned 8-bit value (1 byte) to the wrapped stream in the stream's target byte
 *        order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteUInt8(BinaryIOStream* self, uint8_t value);

/**
 * @brief Writes a signed 16-bit value (2 bytes) to the wrapped stream in the stream's target byte
 *        order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteInt16(BinaryIOStream* self, int16_t value);

/**
 * @brief Writes an unsigned 16-bit value (2 bytes) to the wrapped stream in the stream's target
 *        byte order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteUInt16(BinaryIOStream* self, uint16_t value);

/**
 * @brief Writes a signed 32-bit value (4 bytes) to the wrapped stream in the stream's target byte
 *        order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteInt32(BinaryIOStream* self, int32_t value);

/**
 * @brief Writes an unsigned 32-bit value (4 bytes) to the wrapped stream in the stream's target
 *        byte order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteUInt32(BinaryIOStream* self, uint32_t value);

/**
 * @brief Writes a signed 64-bit value (8 bytes) to the wrapped stream in the stream's target byte
 *        order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteInt64(BinaryIOStream* self, int64_t value);

/**
 * @brief Writes an unsigned 64-bit value (8 bytes) to the wrapped stream in the stream's target
 *        byte order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteUInt64(BinaryIOStream* self, uint64_t value);

/**
 * @brief Writes a 32-bit IEEE-754 float (4 bytes) to the wrapped stream in the stream's target byte
 *        order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteFloat(BinaryIOStream* self, float value);

/**
 * @brief Writes a 64-bit IEEE-754 double (8 bytes) to the wrapped stream in the stream's target
 *        byte order.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The value to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteDouble(BinaryIOStream* self, double value);

/**
 * @brief Writes a boolean as a single byte (1 for true, 0 for false) to the wrapped stream.
 *
 * Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value The boolean to write.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation
 *          if closed, or ErrorCode_InvalidState if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteBoolean(BinaryIOStream* self, bool value);

/**
 * @brief Writes a signed 32-bit value to the wrapped stream using variable-length encoding.
 *
 * Encodes the value into 1-5 bytes (7 bits per byte with continuation flags, byte-order
 * independent, inverse-decodable with BinaryIOStream_ReadEncodedInt32) and forwards the encoded
 * bytes to the underlying stream. The value's bit pattern is reinterpreted as unsigned, so negative
 * values occupy the full 5 bytes.
 * @param self Binary stream. Must not be NULL (dereferenced to encode) and must be open with a live
 *        wrapped stream.
 * @param value The value to encode and write.
 * @returns Success. Raises ErrorCode_IllegalArgument if the embedded converter's endianness is
 *          invalid, ErrorCode_InvalidOperation if the stream is closed, or ErrorCode_InvalidState
 *          if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteEncodedInt32(BinaryIOStream* self, int32_t value);

/**
 * @brief Writes an unsigned 32-bit value to the wrapped stream using variable-length encoding.
 *
 * Encodes the value into 1-5 bytes (byte-order independent) and forwards them to the underlying
 * stream.
 * @param self Binary stream. Must not be NULL (dereferenced to encode) and must be open with a live
 *        wrapped stream.
 * @param value The value to encode and write.
 * @returns Success. Raises ErrorCode_IllegalArgument if the embedded converter's endianness is
 *          invalid, ErrorCode_InvalidOperation if the stream is closed, or ErrorCode_InvalidState
 *          if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteEncodedUInt32(BinaryIOStream* self, uint32_t value);

/**
 * @brief Writes a signed 64-bit value to the wrapped stream using variable-length encoding.
 *
 * Encodes the value into 1-10 bytes (byte-order independent, inverse-decodable with
 * BinaryIOStream_ReadEncodedInt64) and forwards them to the underlying stream. The value's bit
 * pattern is reinterpreted as unsigned, so negative values occupy the full 10 bytes.
 * @param self Binary stream. Must not be NULL (dereferenced to encode) and must be open with a live
 *        wrapped stream.
 * @param value The value to encode and write.
 * @returns Success. Raises ErrorCode_IllegalArgument if the embedded converter's endianness is
 *          invalid, ErrorCode_InvalidOperation if the stream is closed, or ErrorCode_InvalidState
 *          if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteEncodedInt64(BinaryIOStream* self, int64_t value);

/**
 * @brief Writes an unsigned 64-bit value to the wrapped stream using variable-length encoding.
 *
 * Encodes the value into 1-10 bytes (byte-order independent) and forwards them to the underlying
 * stream.
 * @param self Binary stream. Must not be NULL (dereferenced to encode) and must be open with a live
 *        wrapped stream.
 * @param value The value to encode and write.
 * @returns Success. Raises ErrorCode_IllegalArgument if the embedded converter's endianness is
 *          invalid, ErrorCode_InvalidOperation if the stream is closed, or ErrorCode_InvalidState
 *          if there is no wrapped stream.
 * @note May propagate errors from the wrapped stream's write.
 */
Error BinaryIOStream_WriteEncodedUInt64(BinaryIOStream* self, uint64_t value);

/**
 * @brief Reads a signed 8-bit value (1 byte) from the wrapped stream in the stream's target byte
 *        order.
 *
 * Consumes exactly the value's width from the underlying stream and stores the decoded result in
 * [out] @p value. Requires the stream to be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if the stream is closed, ErrorCode_InvalidState if there is
 *          no wrapped stream, or ErrorCode_IO if fewer than the required bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadInt8(BinaryIOStream* self, int8_t* value);

/**
 * @brief Reads an unsigned 8-bit value (1 byte) from the wrapped stream in the stream's target byte
 *        order.
 *
 * Stores the decoded result in [out] @p value. Requires the stream to be open with a live wrapped
 * stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadUInt8(BinaryIOStream* self, uint8_t* value);

/**
 * @brief Reads a signed 16-bit value (2 bytes) from the wrapped stream in the stream's target byte
 *        order.
 *
 * Stores the decoded result in [out] @p value. Requires the stream to be open with a live wrapped
 * stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadInt16(BinaryIOStream* self, int16_t* value);

/**
 * @brief Reads an unsigned 16-bit value (2 bytes) from the wrapped stream in the stream's target
 *        byte order.
 *
 * Stores the decoded result in [out] @p value. Requires the stream to be open with a live wrapped
 * stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadUInt16(BinaryIOStream* self, uint16_t* value);

/**
 * @brief Reads a signed 32-bit value (4 bytes) from the wrapped stream in the stream's target byte
 *        order.
 *
 * Stores the decoded result in [out] @p value. Requires the stream to be open with a live wrapped
 * stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadInt32(BinaryIOStream* self, int32_t* value);

/**
 * @brief Reads an unsigned 32-bit value (4 bytes) from the wrapped stream in the stream's target
 *        byte order.
 *
 * Stores the decoded result in [out] @p value. Requires the stream to be open with a live wrapped
 * stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadUInt32(BinaryIOStream* self, uint32_t* value);

/**
 * @brief Reads a signed 64-bit value (8 bytes) from the wrapped stream in the stream's target byte
 *        order.
 *
 * Stores the decoded result in [out] @p value. Requires the stream to be open with a live wrapped
 * stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadInt64(BinaryIOStream* self, int64_t* value);

/**
 * @brief Reads an unsigned 64-bit value (8 bytes) from the wrapped stream in the stream's target
 *        byte order.
 *
 * Stores the decoded result in [out] @p value. Requires the stream to be open with a live wrapped
 * stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadUInt64(BinaryIOStream* self, uint64_t* value);

/**
 * @brief Reads a 32-bit IEEE-754 float (4 bytes) from the wrapped stream in the stream's target
 *        byte order.
 *
 * Stores the reconstructed float in [out] @p value. Requires the stream to be open with a live
 * wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadFloat(BinaryIOStream* self, float* value);

/**
 * @brief Reads a 64-bit IEEE-754 double (8 bytes) from the wrapped stream in the stream's target
 *        byte order.
 *
 * Stores the reconstructed double in [out] @p value. Requires the stream to be open with a live
 * wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL.
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_IO if too few bytes are available.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadDouble(BinaryIOStream* self, double* value);

/**
 * @brief Reads a boolean from a single byte of the wrapped stream, accepting only 0 or 1.
 *
 * Consumes one byte and stores false for 0 or true for 1 in [out] @p value. Requires the stream to
 * be open with a live wrapped stream.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded boolean. Must not be NULL (checked first).
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, ErrorCode_IO if no byte is available, or ErrorCode_Deserialize if the byte read
 *          is neither 0 nor 1.
 * @note May propagate errors from the wrapped stream's read.
 */
Error BinaryIOStream_ReadBoolean(BinaryIOStream* self, bool* value);

/**
 * @brief Reads a variable-length-encoded signed 32-bit value from the wrapped stream.
 *
 * Consumes one byte at a time from the underlying stream, accumulating 7-bit groups until a byte
 * without the continuation flag is found (at most 5 bytes), and reconstructs the signed result
 * (inverse of BinaryIOStream_WriteEncodedInt32). Only the bytes of this one integer are consumed.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL (checked first).
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if the stream is closed, ErrorCode_InvalidState if there is
 *          no wrapped stream, or ErrorCode_DecodeError if the stream ends before a terminating byte
 *          or the encoding is malformed/overlong for 32 bits.
 * @note May propagate errors from the wrapped stream's per-byte read.
 */
Error BinaryIOStream_ReadEncodedInt32(BinaryIOStream* self, int32_t* value);

/**
 * @brief Reads a variable-length-encoded unsigned 32-bit value from the wrapped stream.
 *
 * Consumes 7-bit groups one byte at a time (at most 5 bytes) until a terminating byte and
 * reconstructs the unsigned result. Only this integer's bytes are consumed.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL (checked first).
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_DecodeError if the stream ends before a terminating byte or the
 *          encoding is malformed/overlong for 32 bits.
 * @note May propagate errors from the wrapped stream's per-byte read.
 */
Error BinaryIOStream_ReadEncodedUInt32(BinaryIOStream* self, uint32_t* value);

/**
 * @brief Reads a variable-length-encoded signed 64-bit value from the wrapped stream.
 *
 * Consumes 7-bit groups one byte at a time (at most 10 bytes) until a terminating byte and
 * reconstructs the signed result (inverse of BinaryIOStream_WriteEncodedInt64). Only this integer's
 * bytes are consumed.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL (checked first).
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_DecodeError if the stream ends before a terminating byte or the
 *          encoding is malformed/overlong for 64 bits.
 * @note May propagate errors from the wrapped stream's per-byte read.
 */
Error BinaryIOStream_ReadEncodedInt64(BinaryIOStream* self, int64_t* value);

/**
 * @brief Reads a variable-length-encoded unsigned 64-bit value from the wrapped stream.
 *
 * Consumes 7-bit groups one byte at a time (at most 10 bytes) until a terminating byte and
 * reconstructs the unsigned result. Only this integer's bytes are consumed.
 * @param self Binary stream. Must not be NULL and must be open.
 * @param value [out] Receives the decoded value. Must not be NULL (checked first).
 * @returns Success. Raises ErrorCode_IllegalArgument if @p value or @p self is NULL,
 *          ErrorCode_InvalidOperation if closed, ErrorCode_InvalidState if there is no wrapped
 *          stream, or ErrorCode_DecodeError if the stream ends before a terminating byte or the
 *          encoding is malformed/overlong for 64 bits.
 * @note May propagate errors from the wrapped stream's per-byte read.
 */
Error BinaryIOStream_ReadEncodedUInt64(BinaryIOStream* self, uint64_t* value);

/**
 * @brief Releases a binary stream: closes it, tears down the converter, and optionally disposes the
 *        wrapped stream.
 *
 * First closes the stream (closing the wrapped stream too if it is owned and not already closed),
 * then deconstructs the embedded converter, then—if the wrapped stream was owned—deconstructs the
 * wrapped stream, and finally zeroes @p self. Safe to call with a NULL @p self (no-op). Because
 * closing is idempotent, calling this on an already-closed stream still completes the converter
 * teardown, owned-stream deconstruction, and zeroing.
 * @param self Stream to release, or NULL.
 * @returns Success. Propagates any error returned while closing or deconstructing the wrapped
 *          stream (in which case @p self is left un-zeroed).
 */
Error BinaryIOStream_Deconstruct(BinaryIOStream* self);
