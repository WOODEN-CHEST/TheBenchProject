#pragma once
#include <stdbool.h>
#include "WRError.h"
#include "stdint.h"
#include "WRMemory.h"



// Types.
/**
 * @brief Selects which character(s) are accepted as the decimal point when parsing, and which
 *        character is emitted as the decimal point when formatting a floating-point value.
 *
 * When parsing, this constrains which separator character a string may legally contain.
 * When formatting, it selects the separator character written into the output.
 */
typedef enum DecimalSeparatorEnum
{
    /** Use/accept only the ASCII period '.' as the decimal separator. */
    DecimalSeparator_Period,
    /** Use/accept only the ASCII comma ',' as the decimal separator. */
    DecimalSeparator_Comma,
    /**
     * Parsing only: accept either '.' or ',' as the decimal separator. Not intended for
     * formatting (a single concrete separator character is required to write output).
     */
    DecimalSeparator_Any
} DecimalSeparator;

/**
 * @brief Overlapping storage for any one of the eight standard fixed-width integer types.
 *
 * A tagless union holding exactly one active member at a time; the caller is responsible for
 * tracking which member was written and reading back through that same member.
 */
typedef union StandardIntegersUnion
{
    int8_t Int8;
    uint8_t UInt8;
    int16_t Int16;
    uint16_t UInt16;
    int32_t Int32;
    uint32_t UInt32;
    int64_t Int64;
    uint64_t UInt64;
} StandardIntegers;

/**
 * @brief Controls how a float/double is rendered to text: separator character, number of
 *        fractional digits, letter case, and fixed vs. scientific notation.
 *
 * Construct instances with the @c DecimalFormatOptions_Create* helpers rather than by hand; the
 * helpers set the fields into valid, self-consistent combinations. The fields are documented for
 * understanding only.
 */
typedef struct DecimalFormatOptionsStruct
{
    /** Decimal separator character to emit in the output (period or comma). */
    DecimalSeparator _separator;
    /**
     * Number of digits after the decimal separator, or one of the special sentinels
     * @c DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST / @c DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED.
     * A non-negative value requests exactly that many fractional digits (fixed notation) or that
     * many significant fractional digits in scientific notation.
     */
    int32_t _digitCountAfterSeparator;
    /** When true, scientific-notation exponent / shortest-form output uses an uppercase 'E'/'G'. */
    bool _isUpperCase;
    /** When true, the value is rendered in scientific (exponent) notation instead of fixed-point. */
    bool _isScientificNotation;
} DecimalFormatOptions;



// Fields.
/** @brief Largest integer base supported by parse/format functions (16). */
extern const int32_t NUMBER_BASE_MAX;
/** @brief Smallest integer base supported by parse/format functions (2). */
extern const int32_t NUMBER_BASE_MIN;
/**
 * @brief Sentinel base (0) requesting automatic base detection during parsing.
 *
 * Valid only as the @c base argument to the @c *FromString integer parsers, where a leading
 * "0x"/"0X" selects base 16, a leading "0b"/"0B" selects base 2, and anything else selects base 10.
 * Passing this value to a @c *ToString formatter is rejected as an invalid base.
 */
extern const int32_t NUMBER_BASE_AUTO_DETECT;
/** @brief Convenience constant for decimal base 10. */
extern const int32_t NUMBER_BASE_10;
/** @brief Convenience constant for binary base 2 (recognizes/emits the "0b" prefix). */
extern const int32_t NUMBER_BASE_2;
/** @brief Convenience constant for hexadecimal base 16 (recognizes/emits the "0x" prefix). */
extern const int32_t NUMBER_BASE_16;

/**
 * @brief Sentinel for @c DecimalFormatOptions::_digitCountAfterSeparator requesting the shortest
 *        round-trippable representation (printf %g-style; trailing zeros trimmed).
 */
extern const int32_t DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST;
/**
 * @brief Sentinel for @c DecimalFormatOptions::_digitCountAfterSeparator requesting the full,
 *        untrimmed fixed-point representation (printf %f-style default precision).
 */
extern const int32_t DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED;


// Functions.
/**
 * @brief Parses a UTF-8/ASCII numeric string into a signed 8-bit integer.
 *
 * The string may begin with an optional '+' or '-' sign, followed by an optional base prefix
 * ("0x"/"0X" for base 16, "0b"/"0B" for base 2) that is only consumed when it matches the
 * effective base, followed by one or more digits. At least one digit is required. Letters
 * 'a'..'f'/'A'..'F' are accepted as digit values 10..15 subject to the base. No surrounding
 * whitespace, grouping separators, or trailing characters are permitted. The full magnitude range
 * of the target type is accepted, including the most-negative value.
 * @param str Null-terminated source string. Must not be NULL.
 * @param base Numeric base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX], or NUMBER_BASE_AUTO_DETECT to
 *        infer the base from a leading "0x"/"0b" prefix (defaulting to base 10).
 * @param value [out] Receives the parsed value on success; unchanged on failure. Must not be NULL.
 * @returns Success, or: ErrorCode_IllegalArgument if @p str or @p value is NULL, @p base is out of
 *          range, no digit is present, or a character is not a valid digit for the base;
 *          ErrorCode_ArgumentOutOfRange if the parsed value does not fit the target type.
 */
Error Number_Int8FromString(const unsigned char* str, int32_t base, int8_t* value);

/**
 * @brief Formats a signed 8-bit integer as text and appends it to a byte buffer.
 *
 * Writes an optional leading '-' for negative values, an optional base prefix, the magnitude's
 * digits (using lowercase 'a'..'f' for bases above 10), and a trailing null terminator. The
 * content is appended at the buffer's current end; the buffer's count is advanced past the digits
 * but not past the null terminator, so successive appends form one continuous string.
 * @param value Value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]. NUMBER_BASE_AUTO_DETECT is NOT
 *        valid here and is rejected.
 * @param includePrefix When true, emits a "0x" prefix for base 16 or "0b" prefix for base 2 (no
 *        prefix is added for any other base).
 * @param buffer [out] Destination byte buffer (element size 1) to append to. Must not be NULL and
 *        must be writable; it grows on demand if it owns a reallocator.
 * @returns Success, or: ErrorCode_IllegalArgument if @p buffer is NULL or @p base is invalid;
 *          ErrorCode_BufferTooSmall if the buffer cannot accommodate the formatted text.
 */
Error Number_Int8ToString(int8_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


/**
 * @brief Parses a UTF-8/ASCII numeric string into an unsigned 8-bit integer.
 *
 * Behaves like @ref Number_Int8FromString but rejects a leading '-' sign (a negative value is
 * out of range) and accepts the full unsigned magnitude range of the target type.
 * @param str Null-terminated source string. Must not be NULL.
 * @param base Numeric base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX], or NUMBER_BASE_AUTO_DETECT.
 * @param value [out] Receives the parsed value on success; unchanged on failure. Must not be NULL.
 * @returns Success, or: ErrorCode_IllegalArgument if @p str or @p value is NULL, @p base is out of
 *          range, no digit is present, or a character is invalid for the base;
 *          ErrorCode_ArgumentOutOfRange if the value is negative or does not fit the target type.
 */
Error Number_UInt8FromString(const unsigned char* str, int32_t base, uint8_t* value);

/**
 * @brief Formats an unsigned 8-bit integer as text and appends it to a byte buffer.
 *
 * Behaves like @ref Number_Int8ToString but never emits a sign. See that function for buffer
 * growth, null-termination, prefix, and error semantics.
 * @param value Value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, emits "0x" for base 16 or "0b" for base 2.
 * @param buffer [out] Destination writable byte buffer. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument (NULL buffer / invalid base) or
 *          ErrorCode_BufferTooSmall.
 */
Error Number_UInt8ToString(uint8_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


/**
 * @brief Parses a UTF-8/ASCII numeric string into a signed 16-bit integer.
 *
 * Identical contract to @ref Number_Int8FromString, ranged to the 16-bit signed type.
 * @param str Null-terminated source string. Must not be NULL.
 * @param base Numeric base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX], or NUMBER_BASE_AUTO_DETECT.
 * @param value [out] Receives the parsed value on success; unchanged on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument (NULL/invalid base/no digit/bad character) or
 *          ErrorCode_ArgumentOutOfRange (value does not fit the target type).
 */
Error Number_Int16FromString(const unsigned char* str, int32_t base, int16_t* value);

/**
 * @brief Formats a signed 16-bit integer as text and appends it to a byte buffer.
 *
 * Identical contract to @ref Number_Int8ToString, for the 16-bit signed type.
 * @param value Value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, emits "0x" for base 16 or "0b" for base 2.
 * @param buffer [out] Destination writable byte buffer. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_BufferTooSmall.
 */
Error Number_Int16ToString(int16_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


/**
 * @brief Parses a UTF-8/ASCII numeric string into an unsigned 16-bit integer.
 *
 * Identical contract to @ref Number_UInt8FromString, ranged to the 16-bit unsigned type.
 * @param str Null-terminated source string. Must not be NULL.
 * @param base Numeric base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX], or NUMBER_BASE_AUTO_DETECT.
 * @param value [out] Receives the parsed value on success; unchanged on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_ArgumentOutOfRange (negative or
 *          does not fit).
 */
Error Number_UInt16FromString(const unsigned char* str, int32_t base, uint16_t* value);

/**
 * @brief Formats an unsigned 16-bit integer as text and appends it to a byte buffer.
 *
 * Identical contract to @ref Number_UInt8ToString, for the 16-bit unsigned type.
 * @param value Value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, emits "0x" for base 16 or "0b" for base 2.
 * @param buffer [out] Destination writable byte buffer. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_BufferTooSmall.
 */
Error Number_UInt16ToString(uint16_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


/**
 * @brief Parses a UTF-8/ASCII numeric string into a signed 32-bit integer.
 *
 * Identical contract to @ref Number_Int8FromString, ranged to the 32-bit signed type.
 * @param str Null-terminated source string. Must not be NULL.
 * @param base Numeric base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX], or NUMBER_BASE_AUTO_DETECT.
 * @param value [out] Receives the parsed value on success; unchanged on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_ArgumentOutOfRange.
 */
Error Number_Int32FromString(const unsigned char* str, int32_t base, int32_t* value);

/**
 * @brief Formats a signed 32-bit integer as text and appends it to a byte buffer.
 *
 * Identical contract to @ref Number_Int8ToString, for the 32-bit signed type.
 * @param value Value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, emits "0x" for base 16 or "0b" for base 2.
 * @param buffer [out] Destination writable byte buffer. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_BufferTooSmall.
 */
Error Number_Int32ToString(int32_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


/**
 * @brief Parses a UTF-8/ASCII numeric string into an unsigned 32-bit integer.
 *
 * Identical contract to @ref Number_UInt8FromString, ranged to the 32-bit unsigned type.
 * @param str Null-terminated source string. Must not be NULL.
 * @param base Numeric base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX], or NUMBER_BASE_AUTO_DETECT.
 * @param value [out] Receives the parsed value on success; unchanged on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_ArgumentOutOfRange (negative or
 *          does not fit).
 */
Error Number_UInt32FromString(const unsigned char* str, int32_t base, uint32_t* value);

/**
 * @brief Formats an unsigned 32-bit integer as text and appends it to a byte buffer.
 *
 * Identical contract to @ref Number_UInt8ToString, for the 32-bit unsigned type.
 * @param value Value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, emits "0x" for base 16 or "0b" for base 2.
 * @param buffer [out] Destination writable byte buffer. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_BufferTooSmall.
 */
Error Number_UInt32ToString(uint32_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


/**
 * @brief Parses a UTF-8/ASCII numeric string into a signed 64-bit integer.
 *
 * Identical contract to @ref Number_Int8FromString, ranged to the 64-bit signed type. The
 * underlying magnitude accumulator is 64-bit, so the full signed range including INT64_MIN is
 * representable.
 * @param str Null-terminated source string. Must not be NULL.
 * @param base Numeric base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX], or NUMBER_BASE_AUTO_DETECT.
 * @param value [out] Receives the parsed value on success; unchanged on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_ArgumentOutOfRange.
 */
Error Number_Int64FromString(const unsigned char* str, int32_t base, int64_t* value);

/**
 * @brief Formats a signed 64-bit integer as text and appends it to a byte buffer.
 *
 * Identical contract to @ref Number_Int8ToString, for the 64-bit signed type.
 * @param value Value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, emits "0x" for base 16 or "0b" for base 2.
 * @param buffer [out] Destination writable byte buffer. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_BufferTooSmall.
 */
Error Number_Int64ToString(int64_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


/**
 * @brief Parses a UTF-8/ASCII numeric string into an unsigned 64-bit integer.
 *
 * Identical contract to @ref Number_UInt8FromString, ranged to the full 64-bit unsigned type.
 * @param str Null-terminated source string. Must not be NULL.
 * @param base Numeric base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX], or NUMBER_BASE_AUTO_DETECT.
 * @param value [out] Receives the parsed value on success; unchanged on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_ArgumentOutOfRange (negative or
 *          exceeds UINT64_MAX).
 */
Error Number_UInt64FromString(const unsigned char* str, int32_t base, uint64_t* value);

/**
 * @brief Formats an unsigned 64-bit integer as text and appends it to a byte buffer.
 *
 * Identical contract to @ref Number_UInt8ToString, for the 64-bit unsigned type.
 * @param value Value to format.
 * @param base Output base in [NUMBER_BASE_MIN, NUMBER_BASE_MAX]; NUMBER_BASE_AUTO_DETECT is invalid.
 * @param includePrefix When true, emits "0x" for base 16 or "0b" for base 2.
 * @param buffer [out] Destination writable byte buffer. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument or ErrorCode_BufferTooSmall.
 */
Error Number_UInt64ToString(uint64_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


/**
 * @brief Parses a decimal string into a single-precision float.
 *
 * Recognizes the case-insensitive special tokens "nan", "infinity", and "-infinity" first. Other
 * input must be a decimal numeral: optional leading sign, decimal digits with at most one decimal
 * separator, and an optional exponent ('e'/'E' with an optional signed integer). At least one
 * digit is required; grouping separators, surrounding whitespace, and trailing characters are not
 * allowed. The accepted separator character is constrained by @p separator. Parsing is performed
 * in double precision and then narrowed to float; finite magnitudes exceeding the float range are
 * rejected as overflow.
 * @param str Null-terminated source string. Must not be NULL (a NULL @p str surfaces as an invalid
 *        decimal string error).
 * @param value [out] Receives the parsed float on success; unchanged on failure. Must not be NULL.
 * @param separator Which decimal separator character(s) are accepted (period, comma, or either).
 * @returns Success, or: ErrorCode_IllegalArgument if @p value is NULL, the string is not a valid
 *          decimal numeral, it contains a disallowed separator, or it has no digits;
 *          ErrorCode_ArgumentOutOfRange if a finite parsed magnitude does not fit a float.
 */
Error Number_FloatFromString(const unsigned char* str,
    float* value,
    DecimalSeparator separator);

/**
 * @brief Formats a single-precision float as text and appends it to a byte buffer.
 *
 * Equivalent to widening to double and calling @ref Number_DoubleToString: NaN renders as "NaN",
 * positive/negative infinity as "infinity"/"-infinity", and finite values per @p options (fixed,
 * scientific, shortest, or full). The chosen decimal separator is substituted into the output, and
 * a trailing null terminator is written; the buffer count advances past the digits but not the
 * terminator so appends remain continuous.
 * @param value Value to format.
 * @param buffer [out] Destination writable byte buffer (element size 1). Must not be NULL.
 * @param options Formatting options controlling notation, fractional digit count, case, and the
 *        emitted separator character.
 * @returns Success, or: ErrorCode_IllegalArgument if @p buffer is NULL or the internal format
 *          string is invalid; ErrorCode_BufferTooSmall if the buffer cannot hold the output.
 */
Error Number_FloatToString(float value,
    GenericBuffer* buffer,
    DecimalFormatOptions options);


/**
 * @brief Parses a decimal string into a double-precision value.
 *
 * Same grammar and special-token handling as @ref Number_FloatFromString, but the result is kept
 * in full double precision. A finite value that overflows the double range (reported via the C
 * library as a range error) is rejected as an invalid decimal string rather than producing
 * infinity.
 * @param str Null-terminated source string. Must not be NULL.
 * @param value [out] Receives the parsed double on success; unchanged on failure. Must not be NULL.
 * @param separator Which decimal separator character(s) are accepted.
 * @returns Success, or ErrorCode_IllegalArgument if @p value is NULL, the string is not a valid
 *          decimal numeral (including out-of-range magnitudes), it uses a disallowed separator, or
 *          it has no digits.
 */
Error Number_DoubleFromString(const unsigned char* str,
    double* value,
    DecimalSeparator separator);

/**
 * @brief Formats a double-precision value as text and appends it to a byte buffer.
 *
 * NaN renders as "NaN", positive infinity as "infinity", and negative infinity as "-infinity".
 * Finite values are rendered according to @p options: a non-negative fractional digit count
 * produces fixed-point with exactly that many fractional digits; @c DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST
 * produces the shortest %g-style form; @c DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED produces the full
 * %f-style form; and scientific notation is used when requested. The selected separator character
 * is substituted, and a trailing null terminator is written (the count advances past the digits
 * only, so consecutive appends concatenate cleanly).
 * @param value Value to format.
 * @param buffer [out] Destination writable byte buffer (element size 1). Must not be NULL.
 * @param options Formatting options controlling notation, fractional digit count, case, and the
 *        emitted separator character.
 * @returns Success, or: ErrorCode_IllegalArgument if @p buffer is NULL or the internal format
 *          string is invalid; ErrorCode_BufferTooSmall if the buffer cannot hold the output.
 */
Error Number_DoubleToString(double value,
    GenericBuffer* buffer,
    DecimalFormatOptions options);

/**
 * @brief Builds formatting options that render a value in scientific (exponent) notation.
 *
 * The resulting options use the full (untrimmed) mantissa precision; combine via the fields if a
 * fixed scientific precision is required.
 * @param separator Decimal separator character to emit in the output.
 * @param isUpperCase When true, the exponent marker is an uppercase 'E'; otherwise lowercase 'e'.
 * @returns A populated @ref DecimalFormatOptions value (returned by value; nothing to release).
 */
DecimalFormatOptions DecimalFormatOptions_CreateScientific(DecimalSeparator separator, bool isUpperCase);

/**
 * @brief Builds formatting options that render a value in fixed-point notation with an exact
 *        number of digits after the decimal separator.
 * @param separator Decimal separator character to emit in the output.
 * @param digitCountAfterDecimal Number of digits after the decimal separator; negative values are
 *        clamped to 0.
 * @returns A populated @ref DecimalFormatOptions value (returned by value; nothing to release).
 */
DecimalFormatOptions DecimalFormatOptions_CreateFixed(DecimalSeparator separator, int32_t digitCountAfterDecimal);

/**
 * @brief Builds formatting options that render the shortest fixed-point representation which still
 *        round-trips to the same value (trailing zeros trimmed, %g-style).
 * @param separator Decimal separator character to emit in the output.
 * @returns A populated @ref DecimalFormatOptions value (returned by value; nothing to release).
 */
DecimalFormatOptions DecimalFormatOptions_CreateShortest(DecimalSeparator separator);

/**
 * @brief Builds formatting options that render the full, non-trimmed fixed-point representation
 *        with the default (unlimited) number of fractional digits.
 * @param separator Decimal separator character to emit in the output.
 * @returns A populated @ref DecimalFormatOptions value (returned by value; nothing to release).
 */
DecimalFormatOptions DecimalFormatOptions_CreateFull(DecimalSeparator separator);
