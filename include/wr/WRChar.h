#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "WRUnicode.h"


// Macros.

/** @brief Maximum number of bytes a single codepoint can occupy when encoded as UTF-8 (4). */
#define CODEPOINT_BYTE_COUNT_MAX 4
/** @brief Maximum number of 16-bit words a single codepoint can occupy when encoded as UTF-16 (2, i.e. a surrogate pair). */
#define CODEPOINT_WORD16_COUNT_MAX 2


// Functions.

/**
 * @brief Tests whether the bytes at @p character form a single valid UTF-8 encoded codepoint.
 *
 * Equivalent to CharUTF8_IsCharBufferValid with a buffer length of CODEPOINT_BYTE_COUNT_MAX; the caller must
 * therefore guarantee that up to CODEPOINT_BYTE_COUNT_MAX (4) bytes starting at @p character are readable.
 * Validity requires a well-formed lead byte, the correct number of continuation bytes, a non-overlong encoding,
 * and a resulting codepoint that is in the Unicode range and not a surrogate.
 * @param character Pointer to the first byte of the candidate character. May be NULL (returns false).
 * @returns true if the bytes encode exactly one valid UTF-8 codepoint; false otherwise.
 */
bool CharUTF8_IsCharValid(const unsigned char* character);

/**
 * @brief Tests whether the first character in a length-bounded UTF-8 byte buffer is valid.
 *
 * Like CharUTF8_IsCharValid but never reads past @p bufferLength bytes: if the encoding implied by the lead
 * byte needs more bytes than are available, the character is reported invalid. Only the first encoded
 * character is examined.
 * @param character Pointer to the first byte. May be NULL (returns false).
 * @param bufferLength Number of readable bytes available at @p character. 0 yields false.
 * @returns true if a complete, well-formed, non-overlong, in-range UTF-8 codepoint fits within the buffer; false otherwise.
 */
bool CharUTF8_IsCharBufferValid(const unsigned char* character, size_t bufferLength);

/**
 * @brief Tests whether a codepoint is encodable as UTF-8.
 *
 * A codepoint is valid when it lies in the Unicode range [0x0, 0x10FFFF] and is not in the surrogate range
 * [0xD800, 0xDFFF].
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a valid Unicode scalar value; false otherwise.
 */
bool CharUTF8_IsCodePointValid(CodePoint codepoint);

/**
 * @brief Returns how many bytes the UTF-8 character at @p character occupies.
 *
 * Decodes the character (reading up to CODEPOINT_BYTE_COUNT_MAX bytes, which the caller must ensure are
 * readable) and reports its length only if it is fully valid.
 * @param character Pointer to the first byte of the character. May be NULL.
 * @returns The byte length (1..4) of the encoded character, or 0 if @p character is NULL or does not begin a
 *          valid UTF-8 codepoint.
 */
size_t CharUTF8_GetByteCountChar(const unsigned char* character);

/**
 * @brief Returns the byte length of the UTF-8 character that ends at @p characterLastByte.
 *
 * Scans backward from the final byte to locate the lead byte of the character, considering at most
 * CODEPOINT_BYTE_COUNT_MAX-1 preceding bytes (further limited by @p remainingPreBytes), and validates that a
 * complete codepoint ends exactly at @p characterLastByte.
 * @param characterLastByte Pointer to the last byte of the character. May be NULL (returns 0).
 * @param remainingPreBytes Number of bytes available before @p characterLastByte that may be inspected while
 *        backtracking. Effectively capped at CODEPOINT_BYTE_COUNT_MAX-1.
 * @returns The byte length (1..4) of the character ending at @p characterLastByte, or 0 if no valid character
 *          ends there within the allowed backtracking window.
 */
size_t CharUTF8_GetByteCountCharFromEnd(const unsigned char* characterLastByte, size_t remainingPreBytes);

/**
 * @brief Returns how many bytes @p codepoint would occupy when encoded as UTF-8.
 * @param codepoint The codepoint to measure.
 * @returns The encoded length: 1, 2, 3, or 4 bytes for a valid codepoint, or 0 if the codepoint is not a valid
 *          Unicode scalar value (out of range or a surrogate).
 */
size_t CharUTF8_GetByteCountCodepoint(CodePoint codepoint);

/**
 * @brief Decodes the codepoint of the UTF-8 character that ends at @p characterLastByte.
 *
 * Scans backward (as in CharUTF8_GetByteCountCharFromEnd) to find the character whose final byte is
 * @p characterLastByte, then decodes it.
 * @param characterLastByte Pointer to the last byte of the character. May be NULL (returns CODEPOINT_NONE).
 * @param remainingPreBytes Number of inspectable bytes before @p characterLastByte, capped at
 *        CODEPOINT_BYTE_COUNT_MAX-1.
 * @returns The decoded codepoint, or CODEPOINT_NONE if no valid character ends at @p characterLastByte within
 *          the allowed backtracking window.
 */
CodePoint CharUTF8_GetCodePointFromEnd(const unsigned char* characterLastByte, size_t remainingPreBytes);

/**
 * @brief Encodes @p codepoint as UTF-8 into the buffer at @p character.
 *
 * Writes the 1-4 byte UTF-8 sequence for the codepoint. No null terminator is written. The caller must ensure
 * the destination has room for the full encoding (up to CODEPOINT_BYTE_COUNT_MAX bytes); use
 * CharUTF8_GetByteCountCodepoint to size it precisely.
 * @param character [out] Destination buffer for the encoded bytes. If NULL, nothing is written and 0 is returned.
 * @param codepoint The codepoint to encode.
 * @returns The number of bytes written (1..4), or 0 if @p character is NULL or @p codepoint is not a valid
 *          Unicode scalar value (in which case nothing is written).
 */
size_t CharUTF8_WriteCodePoint(unsigned char* character, CodePoint codepoint);

/**
 * @brief Decodes the codepoint of the UTF-8 character beginning at @p character.
 *
 * Reads up to CODEPOINT_BYTE_COUNT_MAX bytes (which the caller must ensure are readable) and decodes the first
 * character, rejecting overlong, truncated, surrogate, or out-of-range encodings.
 * @param character Pointer to the first byte of the character. May be NULL.
 * @returns The decoded codepoint, or CODEPOINT_NONE if @p character is NULL or does not begin a valid UTF-8 codepoint.
 */
CodePoint CharUTF8_GetCodePoint(const unsigned char* character);



/**
 * @brief Tests whether the words at @p character form a single valid UTF-16 encoded codepoint.
 *
 * Equivalent to CharUTF16_IsCharBufferValid with a buffer length of CODEPOINT_WORD16_COUNT_MAX; the caller must
 * therefore guarantee that up to CODEPOINT_WORD16_COUNT_MAX (2) 16-bit words starting at @p character are
 * readable. A lone low surrogate, an unpaired high surrogate, or an out-of-range result is rejected.
 * @param character Pointer to the first 16-bit word of the candidate character. May be NULL (returns false).
 * @returns true if the words encode exactly one valid UTF-16 codepoint; false otherwise.
 */
bool CharUTF16_IsCharValid(const uint16_t* character);

/**
 * @brief Tests whether the first character in a length-bounded UTF-16 word buffer is valid.
 *
 * Like CharUTF16_IsCharValid but never reads past @p bufferLength words: if a high surrogate is found but no
 * second word is available, the character is reported invalid. Only the first encoded character is examined.
 * @param character Pointer to the first 16-bit word. May be NULL (returns false).
 * @param bufferLength Number of readable 16-bit words available at @p character. 0 yields false.
 * @returns true if a complete, well-formed, in-range UTF-16 codepoint fits within the buffer; false otherwise.
 */
bool CharUTF16_IsCharBufferValid(const uint16_t* character, size_t bufferLength);

/**
 * @brief Tests whether a codepoint is encodable as UTF-16.
 *
 * A codepoint is valid when it lies in the Unicode range [0x0, 0x10FFFF] and is not in the surrogate range
 * [0xD800, 0xDFFF].
 * @param codepoint The codepoint to test.
 * @returns true if the codepoint is a valid Unicode scalar value; false otherwise.
 */
bool CharUTF16_IsCodePointValid(CodePoint codepoint);

/**
 * @brief Returns how many 16-bit words the UTF-16 character at @p character occupies.
 *
 * Decodes the character (reading up to CODEPOINT_WORD16_COUNT_MAX words, which the caller must ensure are
 * readable) and reports its length only if it is fully valid.
 * @param character Pointer to the first word of the character. May be NULL.
 * @returns The word length (1 for a BMP unit, 2 for a surrogate pair), or 0 if @p character is NULL or does not
 *          begin a valid UTF-16 codepoint.
 */
size_t CharUTF16_GetWordCountChar(const uint16_t* character);

/**
 * @brief Returns the word length of the UTF-16 character that ends at @p characterLastWord.
 *
 * Scans backward from the final word to locate the start of the character, considering at most
 * CODEPOINT_WORD16_COUNT_MAX-1 preceding words (further limited by @p remainingPreWords), and validates that a
 * complete codepoint ends exactly at @p characterLastWord.
 * @param characterLastWord Pointer to the last 16-bit word of the character. May be NULL (returns 0).
 * @param remainingPreWords Number of words available before @p characterLastWord that may be inspected while
 *        backtracking. Effectively capped at CODEPOINT_WORD16_COUNT_MAX-1.
 * @returns The word length (1 or 2) of the character ending at @p characterLastWord, or 0 if no valid character
 *          ends there within the allowed backtracking window.
 */
size_t CharUTF16_GetWordCountCharFromEnd(const uint16_t* characterLastWord, size_t remainingPreWords);

/**
 * @brief Returns how many 16-bit words @p codepoint would occupy when encoded as UTF-16.
 * @param codepoint The codepoint to measure.
 * @returns 1 for a BMP codepoint, 2 for a supplementary codepoint, or 0 if the codepoint is not a valid Unicode
 *          scalar value (out of range or a surrogate).
 */
size_t CharUTF16_GetWordCountCodepoint(CodePoint codepoint);

/**
 * @brief Decodes the codepoint of the UTF-16 character that ends at @p characterLastWord.
 *
 * Scans backward (as in CharUTF16_GetWordCountCharFromEnd) to find the character whose final word is
 * @p characterLastWord, then decodes it.
 * @param characterLastWord Pointer to the last 16-bit word of the character. May be NULL (returns CODEPOINT_NONE).
 * @param remainingPreWords Number of inspectable words before @p characterLastWord, capped at
 *        CODEPOINT_WORD16_COUNT_MAX-1.
 * @returns The decoded codepoint, or CODEPOINT_NONE if no valid character ends at @p characterLastWord within
 *          the allowed backtracking window.
 */
CodePoint CharUTF16_GetCodePointFromEnd(const uint16_t* characterLastWord, size_t remainingPreWords);

/**
 * @brief Encodes @p codepoint as UTF-16 into the buffer at @p character.
 *
 * Writes either one word (BMP codepoint) or a high/low surrogate pair (supplementary codepoint). No null
 * terminator is written. The caller must ensure the destination has room for up to CODEPOINT_WORD16_COUNT_MAX
 * words; use CharUTF16_GetWordCountCodepoint to size it precisely.
 * @param character [out] Destination buffer for the encoded words. If NULL, nothing is written and 0 is returned.
 * @param codepoint The codepoint to encode.
 * @returns The number of 16-bit words written (1 or 2), or 0 if @p character is NULL or @p codepoint is not a
 *          valid Unicode scalar value (in which case nothing is written).
 */
size_t CharUTF16_WriteCodePoint(uint16_t* character, CodePoint codepoint);

/**
 * @brief Decodes the codepoint of the UTF-16 character beginning at @p character.
 *
 * Reads up to CODEPOINT_WORD16_COUNT_MAX words (which the caller must ensure are readable) and decodes the first
 * character, rejecting lone/unpaired surrogates and out-of-range results.
 * @param character Pointer to the first 16-bit word of the character. May be NULL.
 * @returns The decoded codepoint, or CODEPOINT_NONE if @p character is NULL or does not begin a valid UTF-16 codepoint.
 */
CodePoint CharUTF16_GetCodePoint(const uint16_t* character);
