#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "WRUnicode.h"
#include "WRNumber.h"


/**
 * @brief A growable UTF-8 string composer backed by a primary content buffer and a scratch buffer.
 *
 * The builder accumulates UTF-8 bytes in a primary string buffer and uses a secondary temporary
 * buffer to stage encodings (numbers, code points, substrings) before splicing them in. The
 * managed string is always kept null-terminated, but the logical length (see
 * @ref StringBuilder_GetLength) counts only the content bytes, not the terminator. All mutators
 * operate on UTF-8 byte indices that must fall on code-point boundaries.
 *
 * Construct an instance with @ref StringBuilder_Construct1 (self-owned buffers) or
 * @ref StringBuilder_Construct2 (caller-owned buffers), and release it with
 * @ref StringBuilder_Deconstruct. The fields are internal: code outside this module must not read
 * or write the buffers directly and should use the accessor functions instead.
 */
typedef struct StringBuilderStruct
{
    /* Code outside this module should NOT touch the buffers directly. */
    /** Inline content buffer used when the builder owns its storage (see @c _areBuffersOwned). */
    GenericBuffer _selfString;
    /** Inline scratch buffer used when the builder owns its storage; holds staged encodings. */
    GenericBuffer _selfTempBuffer;
    /**
     * Points at the buffer currently holding the composed string: @c &_selfString when self-owned,
     * or the caller-supplied string buffer when wrapping external storage.
     */
    GenericBuffer* _activeStringBuffer;
    /** Points at the active scratch buffer used to stage bytes before insertion. */
    GenericBuffer* _activeTempBuffer;
    /**
     * True when this builder allocated and owns the two inline buffers (and must free them on
     * deconstruct); false when it wraps caller-provided buffers that it must not free.
     */
    bool _areBuffersOwned;
} StringBuilder;


// Functions.
/**
 * @brief Constructs a StringBuilder that owns and manages its own internal buffers.
 *
 * Allocates the content and scratch buffers (each sized to hold @p initialCapacity content bytes
 * plus the null terminator) and initializes the builder to an empty, null-terminated string. The
 * buffers grow automatically as content is added. The resulting object must later be released with
 * @ref StringBuilder_Deconstruct.
 * @param self [out] Builder to initialize. Must not be NULL.
 * @param initialCapacity Number of content bytes to pre-reserve (excluding the null terminator).
 * @returns Success, or: ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange
 *          if @p initialCapacity is so large that adding the terminator overflows @c size_t.
 * @note Underlying allocation aborts the process on out-of-memory rather than returning an error.
 */
Error StringBuilder_Construct1(StringBuilder* self, size_t initialCapacity);

/**
 * @brief Constructs a StringBuilder that wraps two caller-provided buffers instead of allocating.
 *
 * Adopts @p stringBuffer as the content buffer and @p tempBuffer as the scratch buffer without
 * taking ownership; the caller retains responsibility for the buffers' lifetime and they are NOT
 * freed by @ref StringBuilder_Deconstruct. Both buffers must be writable and have a byte element
 * size (1). The current contents of @p stringBuffer are treated as the builder's initial string:
 * a single trailing null terminator (if present) is dropped from the count and the remaining bytes
 * must form valid UTF-8. The scratch buffer's contents are cleared. Both are then re-null-terminated.
 * @param self [out] Builder to initialize. Must not be NULL.
 * @param stringBuffer [in,out] Writable byte buffer providing initial content and receiving future
 *        composition. Must not be NULL, must have element size 1, must be writable, and its content
 *        must be valid UTF-8.
 * @param tempBuffer [in,out] Writable scratch byte buffer; its existing contents are discarded.
 *        Must not be NULL, must have element size 1, and must be writable.
 * @returns Success, or: ErrorCode_IllegalArgument if @p self or either buffer is NULL or a buffer
 *          has the wrong element size; ErrorCode_InvalidOperation if a buffer is read-only;
 *          ErrorCode_InvalidTextEncoding if @p stringBuffer does not contain valid UTF-8;
 *          ErrorCode_BufferTooSmall if a buffer cannot be grown to hold its null terminator.
 */
Error StringBuilder_Construct2(StringBuilder* self, GenericBuffer* stringBuffer, GenericBuffer* tempBuffer);

/**
 * @brief Releases a StringBuilder and resets it to an uninitialized state.
 *
 * If the builder owns its buffers (created via @ref StringBuilder_Construct1), their backing memory
 * is freed; buffers wrapped via @ref StringBuilder_Construct2 are left untouched for the caller to
 * manage. All internal pointers are cleared so the object can no longer be used until reconstructed.
 * @param self Builder to release. Must not be NULL. Safe to call on an already-deconstructed builder.
 * @returns Success, or ErrorCode_IllegalArgument if @p self is NULL.
 * @note Does not free the @c StringBuilder struct itself (which may be stack-allocated).
 */
Error StringBuilder_Deconstruct(StringBuilder* self);



/**
 * @brief Inserts the entire string contained in another StringBuilder at a byte offset.
 *
 * Splices the source builder's current content into this builder so existing content at and after
 * @p index is shifted right. @p index is a byte offset that must lie on a UTF-8 code-point boundary
 * (0 and the current length are valid). The managed string remains null-terminated afterward.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param other Source builder whose content is copied in. Must not be NULL and must be initialized.
 *        It is read only and left unchanged; passing @p self is not supported.
 * @returns Success, or: ErrorCode_IllegalArgument if a builder argument is NULL;
 *          ErrorCode_InvalidState if a builder is not initialized; ErrorCode_IndexOutOfBounds if
 *          @p index exceeds the length; ErrorCode_InvalidTextEncoding if @p index is not on a
 *          code-point boundary; ErrorCode_ArgumentOutOfRange / ErrorCode_BufferTooSmall on size
 *          overflow or insufficient (non-owned) capacity.
 */
Error StringBuilder_InsertStringFromBuilder(StringBuilder* self, size_t index, StringBuilder* other);

/**
 * @brief Inserts a null-terminated UTF-8 string at a byte offset.
 *
 * The bytes up to (but excluding) the source's null terminator are spliced in at @p index, shifting
 * later content right. @p index must lie on a code-point boundary of the existing string. The
 * inserted bytes must themselves form valid UTF-8. The result stays null-terminated.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param str Null-terminated UTF-8 source string. Must not be NULL.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL argument); ErrorCode_InvalidState
 *          (uninitialized); ErrorCode_IndexOutOfBounds (@p index past the end);
 *          ErrorCode_InvalidTextEncoding (@p index off a boundary, or @p str is not valid UTF-8);
 *          ErrorCode_ArgumentOutOfRange / ErrorCode_BufferTooSmall on overflow / capacity failure.
 */
Error StringBuilder_InsertString(StringBuilder* self, size_t index, const unsigned char* str);

/**
 * @brief Inserts the first @p length bytes of a UTF-8 buffer at a byte offset.
 *
 * Like @ref StringBuilder_InsertString but takes an explicit byte length instead of relying on a
 * terminator, so embedded null bytes are inserted verbatim. The @p length bytes must form valid
 * UTF-8 and @p index must lie on a code-point boundary. The result stays null-terminated.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param str Source byte buffer. Must not be NULL when @p length is greater than 0.
 * @param length Number of bytes from @p str to insert.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL @p str with non-zero @p length, or a NULL
 *          builder); ErrorCode_InvalidState (uninitialized); ErrorCode_IndexOutOfBounds (@p index
 *          past the end); ErrorCode_InvalidTextEncoding (invalid UTF-8 region or @p index off a
 *          boundary); ErrorCode_ArgumentOutOfRange / ErrorCode_BufferTooSmall on overflow/capacity.
 */
Error StringBuilder_InsertSubstring(StringBuilder* self, size_t index, const unsigned char* str, size_t length);

/**
 * @brief Inserts the UTF-8 encoding of a single Unicode code point at a byte offset.
 *
 * Encodes @p codePoint to its 1–4 byte UTF-8 form and splices it in at @p index, which must lie on
 * a code-point boundary. The result stays null-terminated.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param codePoint Unicode scalar value to encode and insert.
 * @returns Success, or: ErrorCode_InvalidCodePoint if @p codePoint is not a valid UTF-8-encodable
 *          scalar; ErrorCode_IllegalArgument (NULL builder); ErrorCode_InvalidState (uninitialized);
 *          ErrorCode_IndexOutOfBounds (@p index past the end); ErrorCode_InvalidTextEncoding
 *          (@p index off a boundary); ErrorCode_ArgumentOutOfRange / ErrorCode_BufferTooSmall on
 *          overflow / capacity failure.
 */
Error StringBuilder_InsertCodePoint(StringBuilder* self, size_t index, CodePoint codePoint);

/**
 * @brief Inserts the textual representation of a signed 8-bit integer at a byte offset.
 *
 * Formats @p value in the given @p base (optionally with a base prefix) into the scratch buffer,
 * then splices the digits in at @p index. @p index must lie on a code-point boundary. The result
 * stays null-terminated.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL builder or invalid @p base);
 *          ErrorCode_InvalidState (uninitialized); ErrorCode_IndexOutOfBounds (@p index past the
 *          end); ErrorCode_InvalidTextEncoding (@p index off a boundary); ErrorCode_BufferTooSmall
 *          / ErrorCode_ArgumentOutOfRange on capacity / overflow failure.
 */
Error StringBuilder_InsertInt8(StringBuilder* self, size_t index, int8_t value, int32_t base, bool includePrefix);

/**
 * @brief Inserts the textual representation of an unsigned 8-bit integer at a byte offset.
 *
 * Identical contract to @ref StringBuilder_InsertInt8 but never emits a sign.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns See @ref StringBuilder_InsertInt8.
 */
Error StringBuilder_InsertUInt8(StringBuilder* self, size_t index, uint8_t value, int32_t base, bool includePrefix);

/**
 * @brief Inserts the textual representation of a signed 16-bit integer at a byte offset.
 *
 * Identical contract to @ref StringBuilder_InsertInt8, for the 16-bit signed type.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns See @ref StringBuilder_InsertInt8.
 */
Error StringBuilder_InsertInt16(StringBuilder* self, size_t index, int16_t value, int32_t base, bool includePrefix);

/**
 * @brief Inserts the textual representation of an unsigned 16-bit integer at a byte offset.
 *
 * Identical contract to @ref StringBuilder_InsertInt8, for the 16-bit unsigned type (no sign).
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns See @ref StringBuilder_InsertInt8.
 */
Error StringBuilder_InsertUInt16(StringBuilder* self, size_t index, uint16_t value, int32_t base, bool includePrefix);

/**
 * @brief Inserts the textual representation of a signed 32-bit integer at a byte offset.
 *
 * Identical contract to @ref StringBuilder_InsertInt8, for the 32-bit signed type.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns See @ref StringBuilder_InsertInt8.
 */
Error StringBuilder_InsertInt32(StringBuilder* self, size_t index, int32_t value, int32_t base, bool includePrefix);

/**
 * @brief Inserts the textual representation of an unsigned 32-bit integer at a byte offset.
 *
 * Identical contract to @ref StringBuilder_InsertInt8, for the 32-bit unsigned type (no sign).
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns See @ref StringBuilder_InsertInt8.
 */
Error StringBuilder_InsertUInt32(StringBuilder* self, size_t index, uint32_t value, int32_t base, bool includePrefix);

/**
 * @brief Inserts the textual representation of a signed 64-bit integer at a byte offset.
 *
 * Identical contract to @ref StringBuilder_InsertInt8, for the 64-bit signed type.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns See @ref StringBuilder_InsertInt8.
 */
Error StringBuilder_InsertInt64(StringBuilder* self, size_t index, int64_t value, int32_t base, bool includePrefix);

/**
 * @brief Inserts the textual representation of an unsigned 64-bit integer at a byte offset.
 *
 * Identical contract to @ref StringBuilder_InsertInt8, for the 64-bit unsigned type (no sign).
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns See @ref StringBuilder_InsertInt8.
 */
Error StringBuilder_InsertUInt64(StringBuilder* self, size_t index, uint64_t value, int32_t base, bool includePrefix);

/**
 * @brief Inserts the textual representation of a single-precision float at a byte offset.
 *
 * Formats @p value per @p options (fixed/scientific/shortest/full, with the configured separator
 * and case; NaN and infinities render as "NaN"/"infinity"/"-infinity") into the scratch buffer and
 * splices the result in at @p index, which must lie on a code-point boundary. Stays null-terminated.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Floating-point value to format.
 * @param options Decimal formatting options (see @ref DecimalFormatOptions).
 * @returns Success, or: ErrorCode_IllegalArgument (NULL builder, or invalid internal format);
 *          ErrorCode_InvalidState (uninitialized); ErrorCode_IndexOutOfBounds (@p index past the
 *          end); ErrorCode_InvalidTextEncoding (@p index off a boundary); ErrorCode_BufferTooSmall
 *          / ErrorCode_ArgumentOutOfRange on capacity / overflow failure.
 */
Error StringBuilder_InsertFloat(StringBuilder* self, size_t index, float value, DecimalFormatOptions options);

/**
 * @brief Inserts the textual representation of a double-precision value at a byte offset.
 *
 * Identical contract to @ref StringBuilder_InsertFloat, for a @c double.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Floating-point value to format.
 * @param options Decimal formatting options (see @ref DecimalFormatOptions).
 * @returns See @ref StringBuilder_InsertFloat.
 */
Error StringBuilder_InsertDouble(StringBuilder* self, size_t index, double value, DecimalFormatOptions options);

/**
 * @brief Inserts the literal text "true" or "false" at a byte offset.
 *
 * Splices "true" (when @p value is true) or "false" (when false) in at @p index, which must lie on
 * a code-point boundary. The result stays null-terminated.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param index Byte offset at which to insert, in [0, length]. Must fall on a code-point boundary.
 * @param value Boolean to render.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL builder); ErrorCode_InvalidState
 *          (uninitialized); ErrorCode_IndexOutOfBounds (@p index past the end);
 *          ErrorCode_InvalidTextEncoding (@p index off a boundary); ErrorCode_BufferTooSmall /
 *          ErrorCode_ArgumentOutOfRange on capacity / overflow failure.
 */
Error StringBuilder_InsertBoolean(StringBuilder* self, size_t index, bool value);

/**
 * @brief Removes a contiguous byte range from the composed string.
 *
 * Deletes @p length bytes starting at @p startIndex and shifts the remaining content left. Both
 * @p startIndex and @p startIndex + @p length must lie on code-point boundaries so that whole
 * characters are removed. The result stays null-terminated.
 * @param self Builder to modify. Must not be NULL and must be initialized.
 * @param startIndex Byte offset of the first byte to remove, in [0, length].
 * @param length Number of bytes to remove; @p startIndex + @p length must not exceed the length.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL builder); ErrorCode_InvalidState
 *          (uninitialized); ErrorCode_IndexOutOfBounds (range outside the string);
 *          ErrorCode_InvalidTextEncoding (an endpoint is not on a code-point boundary).
 */
Error StringBuilder_RemoveRange(StringBuilder* self, size_t startIndex, size_t length);

/**
 * @brief Shortens the composed string to a given byte length, discarding the trailing bytes.
 *
 * Equivalent to removing everything from @p newLength to the current end. @p newLength must not
 * exceed the current length and must fall on a code-point boundary. The result stays null-terminated.
 * @param self Builder to modify. Must not be NULL and must be initialized.
 * @param newLength Target byte length in [0, current length]. Must fall on a code-point boundary.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL builder); ErrorCode_InvalidState
 *          (uninitialized); ErrorCode_IndexOutOfBounds if @p newLength exceeds the current length;
 *          ErrorCode_InvalidTextEncoding if @p newLength is not on a code-point boundary.
 */
Error StringBuilder_Truncate(StringBuilder* self, size_t newLength);

/**
 * @brief Returns the current length of the composed string in bytes (excluding the null terminator).
 *
 * This is a UTF-8 byte count, not a character/code-point count. It is also the byte offset of the
 * end of the string, i.e. the valid upper bound for an insert @c index.
 * @param self Builder to query. Must not be NULL and must be initialized.
 * @returns The number of content bytes currently held.
 */
static inline size_t StringBuilder_GetLength(StringBuilder* self)
{
    return self->_activeStringBuffer->_count;
}

/**
 * @brief Ensures the content buffer can hold at least @p requiredTotalCapacity content bytes.
 *
 * Grows the underlying buffer if necessary (reserving room for the null terminator as well) so that
 * subsequent appends up to that length do not reallocate. Never shrinks the buffer and does not
 * change the string contents or length.
 * @param self Builder to modify. Must not be NULL and must be initialized.
 * @param requiredTotalCapacity Minimum content-byte capacity to guarantee.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL builder); ErrorCode_InvalidState
 *          (uninitialized); ErrorCode_ArgumentOutOfRange if reserving the terminator overflows;
 *          ErrorCode_BufferTooSmall if a non-owned buffer cannot grow to the requested capacity.
 */
Error StringBuilder_EnsureTotalCapacity(StringBuilder* self, size_t requiredTotalCapacity);

/**
 * @brief Ensures capacity for at least @p addedCapacity more content bytes beyond the current length.
 *
 * Convenience over @ref StringBuilder_EnsureTotalCapacity that adds the requested amount to the
 * present length. Never shrinks the buffer and does not change the contents or length.
 * @param self Builder to modify. Must not be NULL and must be initialized.
 * @param addedCapacity Number of additional content bytes to reserve room for.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL builder); ErrorCode_InvalidState
 *          (uninitialized); ErrorCode_ArgumentOutOfRange if current length + @p addedCapacity
 *          overflows @c size_t; ErrorCode_BufferTooSmall if a non-owned buffer cannot grow.
 */
Error StringBuilder_ReserveMoreCapacity(StringBuilder* self, size_t addedCapacity);

/**
 * @brief Returns a read-only pointer to the builder's internal, null-terminated UTF-8 string.
 *
 * The returned pointer aliases the builder's internal storage: it is valid only until the next
 * mutating call (which may reallocate or move the buffer) and must not be modified or freed by the
 * caller. The bytes are null-terminated, with the logical length given by @ref StringBuilder_GetLength.
 * @param self Builder to query. Must not be NULL and must be initialized.
 * @returns Pointer to the internal UTF-8 byte string (read-only, internally owned).
 */
static inline const unsigned char* StringBuilder_GetStringBacked(StringBuilder* self)
{
    return self->_activeStringBuffer->_data;
}

/**
 * @brief Copies the composed string into a destination buffer.
 *
 * Writes the builder's current UTF-8 content (and a trailing null terminator) into @p destination
 * via the string-copy routine, producing an independent copy that is unaffected by later mutations
 * of the builder.
 * @param self Source builder. Must not be NULL and must be initialized.
 * @param destination [out] Buffer to receive the copied string. Constraints (element size, writability,
 *        capacity) are those of the underlying string copy operation.
 * @returns Success, or: ErrorCode_IllegalArgument (NULL builder); ErrorCode_InvalidState
 *          (uninitialized).
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error StringBuilder_CopyTo(StringBuilder* self, GenericBuffer* destination);



/**
 * @brief Appends another StringBuilder's entire string to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertStringFromBuilder at the current length; the
 * end offset is always a code-point boundary so no boundary error can arise from the index.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param other Source builder, read unchanged. Must not be NULL and must be initialized.
 * @returns Success, or the errors of @ref StringBuilder_InsertStringFromBuilder (excluding the
 *          index-boundary cases).
 */
static inline Error StringBuilder_AppendStringFromBuilder(StringBuilder* self, StringBuilder* other)
{
    return StringBuilder_InsertStringFromBuilder(self, StringBuilder_GetLength(self), other);
}

/**
 * @brief Appends a null-terminated UTF-8 string to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertString at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param str Null-terminated UTF-8 source string. Must not be NULL.
 * @returns Success, or the errors of @ref StringBuilder_InsertString (the inserted bytes must be
 *          valid UTF-8).
 */
static inline Error StringBuilder_AppendString(StringBuilder* self, const unsigned char* str)
{
    return StringBuilder_InsertString(self, StringBuilder_GetLength(self), str);
}

/**
 * @brief Appends @p length bytes of a UTF-8 buffer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertSubstring at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param str Source byte buffer. Must not be NULL when @p length is greater than 0.
 * @param length Number of bytes from @p str to append; must form valid UTF-8.
 * @returns Success, or the errors of @ref StringBuilder_InsertSubstring.
 */
static inline Error StringBuilder_AppendSubstring(StringBuilder* self, const unsigned char* str, size_t length)
{
    return StringBuilder_InsertSubstring(self, StringBuilder_GetLength(self), str, length);
}

/**
 * @brief Appends the UTF-8 encoding of a single code point to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertCodePoint at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param codePoint Unicode scalar value to encode and append.
 * @returns Success, or the errors of @ref StringBuilder_InsertCodePoint (including
 *          ErrorCode_InvalidCodePoint for an invalid scalar).
 */
static inline Error StringBuilder_AppendCodePoint(StringBuilder* self, CodePoint codePoint)
{
    return StringBuilder_InsertCodePoint(self, StringBuilder_GetLength(self), codePoint);
}

/**
 * @brief Appends the textual representation of a signed 8-bit integer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertInt8 at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or the errors of @ref StringBuilder_InsertInt8.
 */
static inline Error StringBuilder_AppendInt8(StringBuilder* self, int8_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertInt8(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

/**
 * @brief Appends the textual representation of an unsigned 8-bit integer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertUInt8 at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or the errors of @ref StringBuilder_InsertUInt8.
 */
static inline Error StringBuilder_AppendUInt8(StringBuilder* self, uint8_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertUInt8(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

/**
 * @brief Appends the textual representation of a signed 16-bit integer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertInt16 at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or the errors of @ref StringBuilder_InsertInt16.
 */
static inline Error StringBuilder_AppendInt16(StringBuilder* self, int16_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertInt16(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

/**
 * @brief Appends the textual representation of an unsigned 16-bit integer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertUInt16 at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or the errors of @ref StringBuilder_InsertUInt16.
 */
static inline Error StringBuilder_AppendUInt16(StringBuilder* self, uint16_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertUInt16(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

/**
 * @brief Appends the textual representation of a signed 32-bit integer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertInt32 at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or the errors of @ref StringBuilder_InsertInt32.
 */
static inline Error StringBuilder_AppendInt32(StringBuilder* self, int32_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertInt32(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

/**
 * @brief Appends the textual representation of an unsigned 32-bit integer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertUInt32 at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or the errors of @ref StringBuilder_InsertUInt32.
 */
static inline Error StringBuilder_AppendUInt32(StringBuilder* self, uint32_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertUInt32(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

/**
 * @brief Appends the textual representation of a signed 64-bit integer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertInt64 at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or the errors of @ref StringBuilder_InsertInt64.
 */
static inline Error StringBuilder_AppendInt64(StringBuilder* self, int64_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertInt64(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

/**
 * @brief Appends the textual representation of an unsigned 64-bit integer to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertUInt64 at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Integer value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, prepends "0x" for base 16 or "0b" for base 2.
 * @returns Success, or the errors of @ref StringBuilder_InsertUInt64.
 */
static inline Error StringBuilder_AppendUInt64(StringBuilder* self, uint64_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertUInt64(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

/**
 * @brief Appends the textual representation of a single-precision float to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertFloat at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Floating-point value to format.
 * @param options Decimal formatting options (see @ref DecimalFormatOptions).
 * @returns Success, or the errors of @ref StringBuilder_InsertFloat.
 */
static inline Error StringBuilder_AppendFloat(StringBuilder* self, float value, DecimalFormatOptions options)
{
    return StringBuilder_InsertFloat(self, StringBuilder_GetLength(self), value, options);
}

/**
 * @brief Appends the textual representation of a double-precision value to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertDouble at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Floating-point value to format.
 * @param options Decimal formatting options (see @ref DecimalFormatOptions).
 * @returns Success, or the errors of @ref StringBuilder_InsertDouble.
 */
static inline Error StringBuilder_AppendDouble(StringBuilder* self, double value, DecimalFormatOptions options)
{
    return StringBuilder_InsertDouble(self, StringBuilder_GetLength(self), value, options);
}

/**
 * @brief Appends the literal text "true" or "false" to the end of this builder.
 *
 * Convenience wrapper over @ref StringBuilder_InsertBoolean at the current length.
 * @param self Destination builder. Must not be NULL and must be initialized.
 * @param value Boolean to render.
 * @returns Success, or the errors of @ref StringBuilder_InsertBoolean.
 */
static inline Error StringBuilder_AppendBoolean(StringBuilder* self, bool value)
{
    return StringBuilder_InsertBoolean(self, StringBuilder_GetLength(self), value);
}
