#pragma once
#include "WRUnicode.h"
#include "WRChar.h"
#include <stddef.h>
#include "WRComparator.h"
#include "WRMemory.h"


/**
 * @brief Sentinel "not found" / invalid byte index returned by search operations.
 *
 * Equal to (size_t)-1 (the maximum size_t value). Index out-parameters of search functions
 * (e.g. StringUTF8_IndexOf) are set to this value when no match exists, so callers must compare
 * the returned index against this macro before using it.
 */
#define STRING_INDEX_INVALID (~((size_t)0))


// Types.
/**
 * @brief Selects whether character comparisons honor or ignore letter case.
 *
 * Case-insensitive comparison requires a valid UnicodeData instance; functions that receive this
 * value reject a NULL UnicodeData with ErrorCode_IllegalArgument when StringCaseRule_CaseIgnore is used.
 */
typedef enum StringCaseRuleEnum
{
    /** @brief Compare code points exactly; case differences make strings unequal. */
    StringCaseRule_MatchCase,
    /** @brief Compare code points using Unicode case folding so letters differing only in case match. */
    StringCaseRule_CaseIgnore,
} StringCaseRule;

/**
 * @brief Bit flags controlling how StringUTF8_Split treats the segments it produces.
 *
 * Values are combinable with bitwise OR. Used through the _splitType field of StringSplitOptions.
 */
typedef enum StringSplitTypeEnum
{
    /** @brief No special handling: every segment between delimiters is emitted verbatim, including empty ones. */
    StringSplitType_None = 0,
    /** @brief Omit segments that are empty (after trimming, if StringSplitType_TrimEntries is also set). */
    StringSplitType_SkipEmptyEntries = (1 << 0),
    /** @brief Strip leading and trailing Unicode whitespace from each segment before emitting it; requires a valid UnicodeData. */
    StringSplitType_TrimEntries = (1 << 1)
} StringSplitType;

/**
 * @brief Configuration bundle passed by value to StringUTF8_Split.
 *
 * Construct instances with String_CreateSplitOptions / String_CreateSplitOptionsTyped /
 * String_CreateSplitOptionsNormal rather than initializing fields directly.
 */
typedef struct StringSplitOptionsStruct
{
    /** @brief Bit set of StringSplitType flags selecting empty-entry skipping and per-entry trimming. */
    StringSplitType _splitType;
    /** @brief Maximum number of segments to produce; once reached, the remainder of the string is emitted as the final segment. SIZE_MAX means unlimited; 0 emits the whole string as a single segment. */
    size_t _stringCountLimit;
    /** @brief Whether delimiter matching is case-sensitive or case-insensitive (the latter requires a valid UnicodeData). */
    StringCaseRule _caseRule;
} StringSplitOptions;

/**
 * @brief Direction in which StringUTF8_IndexOf scans for the target substring.
 *
 * The underlying integer values (-1 / +1) double as the per-character step sign.
 */
typedef enum StringMoveDirectionEnum
{
    /** @brief Search from the start index toward the beginning of the string (find the last/preceding match). */
    StringMoveDirection_Backwards = -1,
    /** @brief Search from the start index toward the end of the string (find the first/following match). */
    StringMoveDirection_Forwards = 1,
} StringMoveDirection;

/**
 * @brief Configuration bundle passed by value to StringUTF8_IndexOf.
 *
 * Construct instances with String_CreateIndexOptions / String_CreateIndexOptionsFromEnd /
 * String_CreateIndexOptionsNormal rather than initializing fields directly.
 */
typedef struct StringIndexOfOptionsStruct
{
    /** @brief Whether the target is matched case-sensitively or case-insensitively (the latter requires a valid UnicodeData). */
    StringCaseRule _caseRule;
    /** @brief Direction of the scan; also determines whether the first or last match is reported. */
    StringMoveDirection _direction;
    /** @brief Byte index at which scanning begins. A negative value counts back from the end in whole code points (-1 = last character); positive values are byte offsets and must fall on a character boundary. */
    int64_t _startIndex; 
}
StringIndexOfOptions;


// Fields.
/**
 * @brief Shared, read-only empty UTF-8 string ("").
 *
 * Points to a single null byte. Useful as a non-NULL "no text" argument to string functions.
 * @note Statically allocated and immutable; never free or write through this pointer.
 */
extern const unsigned char* const STRING_EMPTY;


// Functions.
/**
 * @brief Tests whether a NUL-terminated string is well-formed UTF-8.
 *
 * Examines the bytes up to (not including) the terminating NUL and verifies every byte sequence
 * decodes as a valid UTF-8 code point. This checks encoding form only; it does not check whether the
 * code points are assigned in any Unicode data (see StringUTF8_AreCodepointsValid for that).
 * @param str The string to inspect. May be NULL.
 * @returns true if str is non-NULL and contains only valid UTF-8 (an empty string is valid);
 *          false if str is NULL or contains any malformed byte sequence.
 */
bool StringUTF8_IsEncodingValid(const unsigned char* str);

/**
 * @brief Tests whether every code point in a UTF-8 string is defined in the given Unicode data.
 *
 * Decodes the string code point by code point and confirms each is a defined character according to
 * unicode. Decoding stops and the result is false on the first malformed byte sequence or undefined
 * code point.
 * @param str The string to inspect. May be NULL.
 * @param unicode Unicode data used to test whether each code point is defined. May be NULL.
 * @returns true only if both str and unicode are non-NULL, str is valid UTF-8, and every code point
 *          is defined in unicode; false otherwise (including when str or unicode is NULL). An empty
 *          string yields true.
 */
bool StringUTF8_AreCodepointsValid(const unsigned char* str, UnicodeData* unicode);

/**
 * @brief Tests whether a string pointer is NULL or refers to the empty string.
 *
 * @param str The string to test. May be NULL.
 * @returns true if str is NULL or its first byte is the NUL terminator; false otherwise.
 */
bool StringUTF8_IsNullOrEmpty(const unsigned char* str);

/**
 * @brief Determines whether a string is NULL, empty, or consists solely of Unicode whitespace.
 *
 * A NULL or empty string is reported as whitespace without consulting unicode. Otherwise the string
 * is trimmed of leading and trailing Unicode whitespace and reported as whitespace-only when nothing
 * remains; this trimming path requires a valid UnicodeData.
 * @param str The string to test. May be NULL.
 * @param unicode Unicode data used to classify whitespace. Must be non-NULL only when str is a
 *        non-empty string; ignored when str is NULL or empty.
 * @param outValue [out] Receives true if the string is NULL/empty/whitespace-only, false otherwise.
 *        Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if outValue is NULL (or, on the trimming path, if
 *          unicode is NULL).
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set. In particular, ErrorCode_InvalidTextEncoding is raised if str is not valid UTF-8.
 */
Error StringUTF8_IsNullOrWhitespace(const unsigned char* str, UnicodeData* unicode, bool* outValue);

/**
 * @brief Appends a lower-cased copy of a UTF-8 string to a byte buffer.
 *
 * Each code point is mapped to its Unicode lower-case form and the result is appended to destination,
 * which is then NUL-terminated. destination is treated as one growing string: an existing trailing
 * NUL terminator is dropped before appending, so calling this after other writer operations yields a
 * single continuous string. destination must be a byte buffer (element size 1).
 * @param str The source string. Must be non-NULL and valid UTF-8.
 * @param unicode Unicode data providing the lower-case mapping. Must not be NULL.
 * @param destination [out] Byte buffer the lower-cased text is appended to and NUL-terminated.
 *        Must not be NULL and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if str/unicode is NULL or destination is not a valid
 *          byte buffer; ErrorCode_InvalidTextEncoding if str is not valid UTF-8;
 *          ErrorCode_InvalidCodePoint if a code point has no lower-case mapping in unicode;
 *          ErrorCode_BufferTooSmall if destination cannot grow to hold the result.
 */
Error StringUTF8_ToLower(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination);

/**
 * @brief Appends an upper-cased copy of a UTF-8 string to a byte buffer.
 *
 * Each code point is mapped to its Unicode upper-case form and the result is appended to destination,
 * which is then NUL-terminated. destination is treated as one growing string: an existing trailing
 * NUL terminator is dropped before appending. destination must be a byte buffer (element size 1).
 * @param str The source string. Must be non-NULL and valid UTF-8.
 * @param unicode Unicode data providing the upper-case mapping. Must not be NULL.
 * @param destination [out] Byte buffer the upper-cased text is appended to and NUL-terminated.
 *        Must not be NULL and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if str/unicode is NULL or destination is not a valid
 *          byte buffer; ErrorCode_InvalidTextEncoding if str is not valid UTF-8;
 *          ErrorCode_InvalidCodePoint if a code point has no upper-case mapping in unicode;
 *          ErrorCode_BufferTooSmall if destination cannot grow to hold the result.
 */
Error StringUTF8_ToUpper(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination);

/**
 * @brief Appends a case-swapped copy of a UTF-8 string to a byte buffer.
 *
 * Upper-case code points are lowered, lower-case code points are raised, and all other code points
 * are copied unchanged; the result is appended to destination and NUL-terminated. destination is
 * treated as one growing string: an existing trailing NUL terminator is dropped before appending.
 * destination must be a byte buffer (element size 1).
 * @param str The source string. Must be non-NULL and valid UTF-8.
 * @param unicode Unicode data providing case classification and mappings. Must not be NULL.
 * @param destination [out] Byte buffer the case-swapped text is appended to and NUL-terminated.
 *        Must not be NULL and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if str/unicode is NULL or destination is not a valid
 *          byte buffer; ErrorCode_InvalidTextEncoding if str is not valid UTF-8;
 *          ErrorCode_InvalidCodePoint if a code point is undefined in unicode or has no case mapping;
 *          ErrorCode_BufferTooSmall if destination cannot grow to hold the result.
 */
Error StringUTF8_InvertCase(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination);

/**
 * @brief Returns the number of bytes in a UTF-8 string, excluding the NUL terminator.
 *
 * This is the encoded byte length (strlen semantics), not the number of code points or characters.
 * @param str The string to measure. May be NULL.
 * @returns The byte length up to the terminating NUL, or 0 if str is NULL.
 */
size_t StringUTF8_GetByteLength(const unsigned char* str);

/**
 * @brief Counts the number of Unicode code points in a UTF-8 string.
 *
 * Decodes the string and reports how many code points it contains (which may be fewer than the byte
 * length for multi-byte characters).
 * @param str The string to measure. Must not be NULL.
 * @param outLength [out] Receives the code-point count. Must not be NULL; set to 0 before validation
 *        and on error.
 * @returns Success, or ErrorCode_IllegalArgument if outLength or str is NULL;
 *          ErrorCode_InvalidTextEncoding if str is not valid UTF-8.
 */
Error StringUTF8_GetCodePointLength(const unsigned char* str, size_t* outLength);

/**
 * @brief Compares two UTF-8 strings for code-point equality, optionally ignoring case.
 *
 * Compares the strings code point by code point. NULL arguments are permitted and behave like empty
 * strings (so two NULLs, or a NULL and "", compare equal). When caseRule is StringCaseRule_CaseIgnore,
 * a valid UnicodeData is required and case-folded equality is used.
 * @param a First string. May be NULL (treated as empty).
 * @param b Second string. May be NULL (treated as empty).
 * @param caseRule Whether to compare exactly or with case folding.
 * @param unicode Unicode data for case folding. Required (non-NULL) only when caseRule is
 *        StringCaseRule_CaseIgnore; otherwise ignored.
 * @param outValue [out] Receives true if the strings are equal, false otherwise. Must not be NULL;
 *        set to false before validation.
 * @returns Success, or ErrorCode_IllegalArgument if outValue is NULL (or unicode is NULL with
 *          case-insensitive comparison); ErrorCode_InvalidTextEncoding if either string is not valid
 *          UTF-8.
 */
Error StringUTF8_Equals(const unsigned char* a,
    const unsigned char* b,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue);

/**
 * @brief Compares two UTF-8 strings for exact byte-for-byte equality.
 *
 * Performs a raw byte comparison without decoding or case folding: the strings are equal only if they
 * have the same byte length and identical bytes. Unlike StringUTF8_Equals, both arguments must be
 * non-NULL.
 * @param a First string. Must not be NULL.
 * @param b Second string. Must not be NULL.
 * @param outValue [out] Receives true if the byte contents are identical, false otherwise. Must not
 *        be NULL; set to false before the length/content check.
 * @returns Success, or ErrorCode_IllegalArgument if a, b, or outValue is NULL.
 */
Error StringUTF8_EqualsExact(const unsigned char* a,
    const unsigned char* b,
    bool* outValue);

/**
 * @brief Appends a whole UTF-8 string to a byte buffer.
 *
 * Convenience wrapper that copies all of source (up to its NUL terminator) via
 * StringUTF8_CopyToBySize. destination is treated as one growing string: an existing trailing NUL
 * terminator is dropped before the bytes are appended, and the buffer is NUL-terminated afterward.
 * destination must be a byte buffer (element size 1).
 * @param source The string to copy. Must not be NULL.
 * @param destination [out] Byte buffer the bytes are appended to and NUL-terminated. Must not be NULL
 *        and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if source is NULL or destination is not a valid byte
 *          buffer; ErrorCode_InvalidTextEncoding if source is not valid UTF-8;
 *          ErrorCode_BufferTooSmall if destination cannot grow; ErrorCode_ArgumentOutOfRange on size
 *          overflow.
 */
Error StringUTF8_CopyTo(const unsigned char* source, GenericBuffer* destination);

/**
 * @brief Appends a fixed number of bytes of a UTF-8 string to a byte buffer.
 *
 * Appends exactly size bytes from source, requiring that those bytes form valid UTF-8, then
 * NUL-terminates the buffer. destination is treated as one growing string: an existing trailing NUL
 * terminator is dropped before appending. When size is 0, source may be NULL and nothing is appended
 * (the buffer is still NUL-terminated). destination must be a byte buffer (element size 1).
 * @param source The source bytes. Must be non-NULL when size > 0; may be NULL when size is 0.
 * @param size Number of bytes to copy from source.
 * @param destination [out] Byte buffer the bytes are appended to and NUL-terminated. Must not be NULL
 *        and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if destination is not a valid byte buffer or source
 *          is NULL with size > 0; ErrorCode_InvalidTextEncoding if the size bytes are not valid UTF-8;
 *          ErrorCode_BufferTooSmall if destination cannot grow; ErrorCode_ArgumentOutOfRange on size
 *          overflow.
 */
Error StringUTF8_CopyToBySize(const unsigned char* source, size_t size, GenericBuffer* destination);

/**
 * @brief Builds default split options: case-sensitive, unlimited splits, no flags.
 *
 * Equivalent to String_CreateSplitOptions(StringSplitType_None, SIZE_MAX, StringCaseRule_MatchCase).
 * @returns A StringSplitOptions value with no flags set, no split limit, and case-sensitive matching.
 */
StringSplitOptions String_CreateSplitOptionsNormal();

/**
 * @brief Builds split options with the given flags, unlimited splits, and case-sensitive matching.
 *
 * @param type Bit set of StringSplitType flags (empty-entry skipping and/or entry trimming).
 * @returns A StringSplitOptions value carrying type with no split limit and case-sensitive matching.
 */
StringSplitOptions String_CreateSplitOptionsTyped(StringSplitType type);

/**
 * @brief Builds fully specified split options.
 *
 * @param type Bit set of StringSplitType flags controlling empty-entry skipping and entry trimming.
 * @param maxSplits Maximum number of segments to produce; SIZE_MAX means unlimited and 0 emits the
 *        whole string as a single segment. Once the limit is reached the remaining text becomes the
 *        last segment.
 * @param caseRule Whether delimiter matching is case-sensitive or case-insensitive.
 * @returns A StringSplitOptions value populated with the given settings.
 */
StringSplitOptions String_CreateSplitOptions(StringSplitType type, size_t maxSplits, StringCaseRule caseRule);

/**
 * @brief Splits a UTF-8 string on any of several delimiters, appending the segments and their offsets.
 *
 * Scans str left to right, splitting at the earliest match of any delimiter. Each produced segment is
 * appended to stringBuffer as its own NUL-terminated record (raw segment bytes followed by a NUL),
 * and the byte offset of that segment's first byte within stringBuffer is appended to resultIndices
 * (a size_t buffer). Byte offsets, rather than pointers, are reported so they stay valid even if
 * stringBuffer is later grown or reallocated; recover a segment with (stringBuffer->_data + offset).
 *
 * Because stringBuffer holds a sequence of records here, this function does NOT drop an existing
 * trailing NUL from stringBuffer and appends after whatever it already contains. Offsets in
 * resultIndices are absolute within stringBuffer, so they already account for any pre-existing
 * content. Behavior is shaped by splitOptions: StringSplitType_SkipEmptyEntries omits empty segments,
 * StringSplitType_TrimEntries strips Unicode whitespace from each segment first, _caseRule selects
 * case-sensitive or case-insensitive delimiter matching, and _stringCountLimit caps the segment count
 * (the remainder becomes the final segment). When the delimiter list is empty or the limit is 0, the
 * entire input is emitted as one segment.
 *
 * @param str The string to split. Must not be NULL and must be valid UTF-8 (validated lazily as the
 *        scan and any trimming proceed).
 * @param delimeters Array of delimiter strings; every entry must be non-NULL. Empty-string delimiters
 *        are ignored. May be unused when delimeterCount is 0.
 * @param delimeterCount Number of entries in delimeters.
 * @param splitOptions Split configuration (flags, count limit, case rule), passed by value.
 * @param stringBuffer [out] Byte buffer (element size 1) that receives the NUL-terminated segment
 *        records. Must not be NULL.
 * @param resultIndices [out] size_t buffer (element size sizeof(size_t)) that receives each segment's
 *        starting byte offset within stringBuffer. Must not be NULL.
 * @param unicode Unicode data; required (non-NULL) only when caseRule is StringCaseRule_CaseIgnore or
 *        StringSplitType_TrimEntries is set, otherwise ignored.
 * @returns Success, or ErrorCode_IllegalArgument if str or any delimiter is NULL, a buffer is NULL or
 *          has the wrong element size, or unicode is required but NULL; ErrorCode_InvalidTextEncoding
 *          if str is not valid UTF-8; ErrorCode_BufferTooSmall if either buffer cannot grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error StringUTF8_Split(const unsigned char* str,
    const unsigned char** delimeters,
    size_t delimeterCount,
    StringSplitOptions splitOptions,
    GenericBuffer* stringBuffer,
    GenericBuffer* resultIndices,
    UnicodeData* unicode);

/**
 * @brief Builds default index-of options: case-sensitive, forward search from the start.
 *
 * Equivalent to String_CreateIndexOptions(StringCaseRule_MatchCase, StringMoveDirection_Forwards, 0).
 * @returns A StringIndexOfOptions value that searches forward from byte 0 with case-sensitive matching.
 */
StringIndexOfOptions String_CreateIndexOptionsNormal(void);

/**
 * @brief Builds index-of options that search backward from the end: case-sensitive, last match.
 *
 * Equivalent to String_CreateIndexOptions(StringCaseRule_MatchCase, StringMoveDirection_Backwards, -1),
 * i.e. start at the final character and scan toward the beginning.
 * @returns A StringIndexOfOptions value that searches backward from the end with case-sensitive matching.
 */
StringIndexOfOptions String_CreateIndexOptionsFromEnd(void);

/**
 * @brief Builds fully specified index-of options.
 *
 * @param caseRule Whether the target is matched case-sensitively or case-insensitively.
 * @param direction Whether to scan forward (first match) or backward (last match) from the start index.
 * @param startIndex Where scanning begins. A non-negative value is a byte offset that must fall on a
 *        character boundary; a negative value counts back from the end in whole code points (-1 selects
 *        the last character).
 * @returns A StringIndexOfOptions value populated with the given settings.
 */
StringIndexOfOptions String_CreateIndexOptions(StringCaseRule caseRule,
    StringMoveDirection direction,
    int64_t startIndex);

/**
 * @brief Finds the byte index of a substring within a UTF-8 string.
 *
 * Searches str for target according to options (case rule, direction, and start index) and reports the
 * byte offset of the matched occurrence. The start index is normalized first: negative values count
 * code points back from the end, and an out-of-range or non-character-boundary start yields "not
 * found" rather than an error. An empty target matches at the (normalized) start index.
 * @param str The string to search. Must not be NULL.
 * @param target The substring to look for. Must not be NULL.
 * @param options Search configuration (case rule, direction, start index), passed by value.
 * @param unicode Unicode data for case folding. Required (non-NULL) only when options._caseRule is
 *        StringCaseRule_CaseIgnore; otherwise ignored.
 * @param outIndex [out] Receives the byte offset of the match, or STRING_INDEX_INVALID if target does
 *        not occur. Must not be NULL; set to STRING_INDEX_INVALID before searching.
 * @returns Success (whether or not a match was found), or ErrorCode_IllegalArgument if str, target, or
 *          outIndex is NULL (or unicode is NULL with case-insensitive matching);
 *          ErrorCode_InvalidTextEncoding if str is not valid UTF-8.
 */
Error StringUTF8_IndexOf(const unsigned char* str,
    const unsigned char* target,
    StringIndexOfOptions options,
    UnicodeData* unicode,
    size_t* outIndex);

/**
 * @brief Concatenates two UTF-8 strings and appends the result to a byte buffer.
 *
 * Appends the bytes of strA followed by the bytes of strB to destination, then NUL-terminates it. The
 * inputs are not required to be individually valid UTF-8 (no per-string decoding is performed; their
 * bytes are copied verbatim). destination is treated as one growing string: an existing trailing NUL
 * terminator is dropped before appending. destination must be a byte buffer (element size 1).
 * @param strA First string. Must not be NULL.
 * @param strB Second string. Must not be NULL.
 * @param destination [out] Byte buffer the concatenation is appended to and NUL-terminated. Must not
 *        be NULL and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if strA, strB is NULL or destination is not a valid
 *          byte buffer; ErrorCode_ArgumentOutOfRange if the combined length would overflow;
 *          ErrorCode_BufferTooSmall if destination cannot grow to hold the result.
 */
Error StringUTF8_Concat(const unsigned char* strA,
    const unsigned char* strB,
    GenericBuffer* destination);

/**
 * @brief Tests whether a UTF-8 string contains a substring at or after a given byte index.
 *
 * Searches str forward from startIndex for target. A start index that is past the end or not on a
 * character boundary results in "not contained" rather than an error. An empty target is always
 * considered contained (it matches at startIndex).
 * @param str The string to search. Must not be NULL.
 * @param target The substring to look for. Must not be NULL.
 * @param startIndex Byte offset at which to begin searching.
 * @param caseRule Whether matching is case-sensitive or case-insensitive.
 * @param unicode Unicode data for case folding. Required (non-NULL) only when caseRule is
 *        StringCaseRule_CaseIgnore; otherwise ignored.
 * @param outValue [out] Receives true if target occurs, false otherwise. Must not be NULL; set to
 *        false before searching.
 * @returns Success, or ErrorCode_IllegalArgument if str, target, or outValue is NULL (or unicode is
 *          NULL with case-insensitive matching); ErrorCode_InvalidTextEncoding if str is not valid
 *          UTF-8.
 */
Error StringUTF8_Contains(const unsigned char* str,
    const unsigned char* target,
    size_t startIndex,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue);

/**
 * @brief Counts the non-overlapping occurrences of a substring within a UTF-8 string.
 *
 * Scans str left to right, counting matches of target and resuming each search just past the previous
 * match (occurrences do not overlap). An empty target yields a count of 0.
 * @param str The string to search. Must not be NULL.
 * @param target The substring to count. Must not be NULL.
 * @param caseRule Whether matching is case-sensitive or case-insensitive.
 * @param unicode Unicode data for case folding. Required (non-NULL) only when caseRule is
 *        StringCaseRule_CaseIgnore; otherwise ignored.
 * @param count [out] Receives the number of non-overlapping occurrences. Must not be NULL; set to 0
 *        before counting.
 * @returns Success, or ErrorCode_IllegalArgument if str, target, or count is NULL (or unicode is NULL
 *          with case-insensitive matching); ErrorCode_InvalidTextEncoding if str is not valid UTF-8.
 */
Error StringUTF8_Count(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    size_t* count);

/**
 * @brief Tests whether a UTF-8 string ends with a given suffix.
 *
 * Compares the trailing portion of str against target. An empty target always matches. If target is
 * longer than str, or its aligned start in str is not on a character boundary, the result is false.
 * @param str The string to inspect. Must not be NULL.
 * @param target The candidate suffix. Must not be NULL.
 * @param caseRule Whether matching is case-sensitive or case-insensitive.
 * @param unicode Unicode data for case folding. Required (non-NULL) only when caseRule is
 *        StringCaseRule_CaseIgnore; otherwise ignored.
 * @param outValue [out] Receives true if str ends with target, false otherwise. Must not be NULL; set
 *        to false before checking.
 * @returns Success, or ErrorCode_IllegalArgument if str, target, or outValue is NULL (or unicode is
 *          NULL with case-insensitive matching); ErrorCode_InvalidTextEncoding if a compared region is
 *          not valid UTF-8.
 */
Error StringUTF8_EndsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue);

/**
 * @brief Tests whether a UTF-8 string begins with a given prefix.
 *
 * Compares the leading portion of str against target. An empty target always matches.
 * @param str The string to inspect. Must not be NULL.
 * @param target The candidate prefix. Must not be NULL.
 * @param caseRule Whether matching is case-sensitive or case-insensitive.
 * @param unicode Unicode data for case folding. Required (non-NULL) only when caseRule is
 *        StringCaseRule_CaseIgnore; otherwise ignored.
 * @param outValue [out] Receives true if str starts with target, false otherwise. Must not be NULL;
 *        set to false before checking.
 * @returns Success, or ErrorCode_IllegalArgument if str, target, or outValue is NULL (or unicode is
 *          NULL with case-insensitive matching); ErrorCode_InvalidTextEncoding if a compared region is
 *          not valid UTF-8.
 */
Error StringUTF8_StartsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue);

/**
 * @brief Formats a printf-style template and appends the result to a byte buffer.
 *
 * Renders str as a printf format string against the variadic arguments (using the C library's
 * vsnprintf) and appends the formatted text to destination, NUL-terminated. Because the result is
 * appended via the string copy path, destination is treated as one growing string: an existing
 * trailing NUL terminator is dropped before appending. destination must be a byte buffer
 * (element size 1).
 * @param str The printf-style format string. Must not be NULL. Standard format-string rules apply:
 *        conversion specifiers must match the supplied arguments.
 * @param destination [out] Byte buffer the formatted text is appended to and NUL-terminated. Must not
 *        be NULL and must have element size 1.
 * @param ... Arguments consumed by the conversion specifiers in str.
 * @returns Success, or ErrorCode_IllegalArgument if str is NULL or destination is not a valid byte
 *          buffer; ErrorCode_InvalidOperation if formatting fails; ErrorCode_ArgumentOutOfRange if the
 *          formatted length would overflow; ErrorCode_InvalidTextEncoding if the formatted bytes are
 *          not valid UTF-8; ErrorCode_BufferTooSmall if destination cannot grow.
 * @note The caller is responsible for argument/specifier correctness; a mismatch is undefined behavior
 *       as with any printf-family call.
 */
Error StringUTF8_Format(const unsigned char* str,
    GenericBuffer* destination,
    ...);

/**
 * @brief Joins an array of UTF-8 strings with a separator and appends the result to a byte buffer.
 *
 * Concatenates each entry of sources in order, inserting separator between consecutive entries (not
 * before the first or after the last), and appends the result to destination, NUL-terminated.
 * destination is treated as one growing string: an existing trailing NUL terminator is dropped before
 * appending. destination must be a byte buffer (element size 1).
 * @param separator The string placed between elements. Must not be NULL (may be the empty string).
 * @param sources Array of strings to join; every used entry must be non-NULL. May be NULL only when
 *        sourcesSize is 0.
 * @param sourcesSize Number of entries in sources.
 * @param destination [out] Byte buffer the joined text is appended to and NUL-terminated. Must not be
 *        NULL and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if separator is NULL, sources is NULL with
 *          sourcesSize > 0, any source entry is NULL, or destination is not a valid byte buffer;
 *          ErrorCode_BufferTooSmall if destination cannot grow; ErrorCode_ArgumentOutOfRange on size
 *          overflow.
 */
Error StringUTF8_Join(const unsigned char* separator,
    const unsigned char** sources,
    size_t sourcesSize,
    GenericBuffer* destination);

/**
 * @brief Replaces every occurrence of a substring with another and appends the result to a byte buffer.
 *
 * Copies str into destination, substituting each non-overlapping case-sensitive occurrence of
 * searchTarget with replaceValue, then NUL-terminates. If searchTarget is empty, str is copied
 * unchanged. destination is treated as one growing string: an existing trailing NUL terminator is
 * dropped before appending. destination must be a byte buffer (element size 1).
 * @param str The source string. Must not be NULL.
 * @param searchTarget The substring to find. Must not be NULL; an empty value copies str verbatim.
 * @param replaceValue The replacement substring. Must not be NULL (may be empty to delete matches).
 * @param destination [out] Byte buffer the result is appended to and NUL-terminated. Must not be NULL
 *        and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if str, searchTarget, or replaceValue is NULL or
 *          destination is not a valid byte buffer; ErrorCode_BufferTooSmall if destination cannot grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set (e.g. ErrorCode_InvalidTextEncoding when str is not valid UTF-8).
 */
Error StringUTF8_Replace(const unsigned char* str,
    const unsigned char* searchTarget,
    const unsigned char* replaceValue,
    GenericBuffer* destination);

/**
 * @brief Extracts a byte range of a UTF-8 string and appends it to a byte buffer.
 *
 * Appends the bytes of str in the half-open byte range [startIndex, endIndex) to destination and
 * NUL-terminates. Both indices are byte offsets and must each lie on a character boundary so the
 * substring is itself valid UTF-8. destination is treated as one growing string: an existing trailing
 * NUL terminator is dropped before appending. destination must be a byte buffer (element size 1).
 * @param str The source string. Must not be NULL.
 * @param startIndex Inclusive starting byte offset; must be <= endIndex and on a character boundary.
 * @param endIndex Exclusive ending byte offset; must be <= the byte length of str and on a character
 *        boundary.
 * @param destination [out] Byte buffer the substring is appended to and NUL-terminated. Must not be
 *        NULL and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if str is NULL or destination is not a valid byte
 *          buffer; ErrorCode_IndexOutOfBounds if startIndex > endIndex or endIndex exceeds the string
 *          length; ErrorCode_InvalidTextEncoding if either index is not on a character boundary;
 *          ErrorCode_BufferTooSmall if destination cannot grow.
 */
Error StringUTF8_Substring(const unsigned char* str,
    size_t startIndex,
    size_t endIndex,
    GenericBuffer* destination);

/**
 * @brief Trims Unicode whitespace from a UTF-8 string and appends the result to a byte buffer.
 *
 * Removes leading and/or trailing Unicode whitespace (as selected by the flags) and appends the
 * remaining substring to destination, NUL-terminated. If the string is all whitespace the appended
 * content is empty. destination is treated as one growing string: an existing trailing NUL terminator
 * is dropped before appending. destination must be a byte buffer (element size 1).
 * @param str The source string. Must not be NULL and must be valid UTF-8.
 * @param isStartTrimmed When true, strip leading whitespace.
 * @param isEndTrimmed When true, strip trailing whitespace.
 * @param destination [out] Byte buffer the trimmed text is appended to and NUL-terminated. Must not be
 *        NULL and must have element size 1.
 * @param unicode Unicode data used to classify whitespace. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if str/unicode is NULL or destination is not a valid
 *          byte buffer; ErrorCode_InvalidTextEncoding if str is not valid UTF-8;
 *          ErrorCode_BufferTooSmall if destination cannot grow.
 */
Error StringUTF8_Trim(const unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    GenericBuffer* destination,
    UnicodeData* unicode);

/**
 * @brief Computes the byte offset and length of a UTF-8 string after trimming whitespace, without copying.
 *
 * Reports where the trimmed content begins and how many bytes it spans, so callers can reference the
 * substring in place. When the string is entirely whitespace, the reported length is 0 and the start
 * index is the end of the string if leading whitespace was trimmed, or 0 otherwise.
 * @param str The source string. Must not be NULL and must be valid UTF-8.
 * @param isStartTrimmed When true, exclude leading whitespace from the reported range.
 * @param isEndTrimmed When true, exclude trailing whitespace from the reported range.
 * @param startIndex [out] Receives the byte offset of the first retained byte. Must not be NULL.
 * @param outLength [out] Receives the byte length of the retained range. Must not be NULL.
 * @param unicode Unicode data used to classify whitespace. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if str, startIndex, outLength, or unicode is NULL;
 *          ErrorCode_InvalidTextEncoding if str is not valid UTF-8.
 */
Error StringUTF8_GetTrimIndices(const unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    size_t* startIndex,
    size_t* outLength,
    UnicodeData* unicode);

/**
 * @brief Orders two UTF-8 strings by comparing their code points.
 *
 * Compares strA and strB code point by code point; at the first differing code point the numerically
 * smaller one orders first. If one string is a prefix of the other, the shorter string orders first.
 * The ordering is by code-point value, not a locale- or culture-aware collation.
 * @param strA First string. Must not be NULL.
 * @param strB Second string. Must not be NULL.
 * @param result [out] Receives ComparisonResult_ALessThanB, ComparisonResult_AEqualsB, or
 *        ComparisonResult_AGreaterThanB. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if strA, strB, or result is NULL;
 *          ErrorCode_InvalidTextEncoding if either string is not valid UTF-8.
 */
Error StringUTF8_Compare(const unsigned char* strA,
    const unsigned char* strB,
    ComparisonResult* result);

/**
 * @brief Removes every occurrence of a substring and appends the result to a byte buffer.
 *
 * Copies str into destination with each non-overlapping occurrence of target deleted, then
 * NUL-terminates. If target is empty, str is copied unchanged. destination is treated as one growing
 * string: an existing trailing NUL terminator is dropped before appending. destination must be a byte
 * buffer (element size 1).
 * @param str The source string. Must not be NULL.
 * @param target The substring to delete. Must not be NULL; an empty value copies str verbatim.
 * @param caseRule Whether matching is case-sensitive or case-insensitive.
 * @param unicode Unicode data for case folding. Required (non-NULL) only when caseRule is
 *        StringCaseRule_CaseIgnore; otherwise ignored.
 * @param destination [out] Byte buffer the result is appended to and NUL-terminated. Must not be NULL
 *        and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if str or target is NULL, destination is not a valid
 *          byte buffer, or unicode is NULL with case-insensitive matching; ErrorCode_BufferTooSmall if
 *          destination cannot grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set (e.g. ErrorCode_InvalidTextEncoding when str is not valid UTF-8).
 */
Error StringUTF8_Remove(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    GenericBuffer* destination);

/**
 * @brief Inserts a substring at a byte position within a UTF-8 string and appends the result to a byte buffer.
 *
 * Builds str with substring spliced in at byte offset index (text before index, then substring, then
 * the remainder) and appends it to destination, NUL-terminated. index is a byte offset and must lie on
 * a character boundary. destination is treated as one growing string: an existing trailing NUL
 * terminator is dropped before appending. destination must be a byte buffer (element size 1).
 * @param str The source string. Must not be NULL.
 * @param index Byte offset at which substring is inserted; must be <= the byte length of str and on a
 *        character boundary.
 * @param substring The text to insert. Must not be NULL (may be empty).
 * @param destination [out] Byte buffer the result is appended to and NUL-terminated. Must not be NULL
 *        and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if str or substring is NULL or destination is not a
 *          valid byte buffer; ErrorCode_IndexOutOfBounds if index exceeds the string length;
 *          ErrorCode_InvalidTextEncoding if index is not on a character boundary;
 *          ErrorCode_BufferTooSmall if destination cannot grow.
 */
Error StringUTF8_Insert(const unsigned char* str,
    size_t index,
    const unsigned char* substring,
    GenericBuffer* destination);

/**
 * @brief Reverses a UTF-8 string by character and appends the result to a byte buffer.
 *
 * Emits the code points of str in reverse order (whole multi-byte characters are kept intact, not
 * reversed byte by byte) and appends the result to destination, NUL-terminated. destination is treated
 * as one growing string: an existing trailing NUL terminator is dropped before appending. destination
 * must be a byte buffer (element size 1).
 * @param str The source string. Must not be NULL and must be valid UTF-8.
 * @param destination [out] Byte buffer the reversed text is appended to and NUL-terminated. Must not
 *        be NULL and must have element size 1.
 * @returns Success, or ErrorCode_IllegalArgument if str is NULL or destination is not a valid byte
 *          buffer; ErrorCode_InvalidTextEncoding if str is not valid UTF-8; ErrorCode_BufferTooSmall
 *          if destination cannot grow.
 */
Error StringUTF8_Reverse(const unsigned char* str, GenericBuffer* destination);

/**
 * @brief Appends a UTF-8 string repeated a given number of times to a byte buffer.
 *
 * Appends count back-to-back copies of str to destination, then NUL-terminates. A count of 0 appends
 * nothing (the buffer is still NUL-terminated). destination is treated as one growing string: an
 * existing trailing NUL terminator is dropped before appending. destination must be a byte buffer
 * (element size 1).
 * @param str The string to repeat. Must not be NULL.
 * @param destination [out] Byte buffer the repeated text is appended to and NUL-terminated. Must not
 *        be NULL and must have element size 1.
 * @param count Number of times to append str.
 * @returns Success, or ErrorCode_IllegalArgument if str is NULL or destination is not a valid byte
 *          buffer; ErrorCode_ArgumentOutOfRange if the total length would overflow;
 *          ErrorCode_BufferTooSmall if destination cannot grow.
 */
Error StringUTF8_Repeat(const unsigned char* str, GenericBuffer* destination, size_t count);

/**
 * @brief Appends the starting byte offset of each character of a UTF-8 string to an index buffer.
 *
 * For every code point in str, appends its starting byte offset (relative to the start of str) to
 * indexArray, in order. The result lets callers map character positions to byte positions. indexArray
 * must be a size_t buffer (element size sizeof(size_t)); offsets are appended after any existing
 * contents.
 * @param str The source string. Must not be NULL and must be valid UTF-8.
 * @param indexArray [out] size_t buffer that receives one starting byte offset per character. Must not
 *        be NULL and must have element size sizeof(size_t).
 * @returns Success, or ErrorCode_IllegalArgument if str is NULL or indexArray is not a valid size_t
 *          buffer; ErrorCode_InvalidTextEncoding if str is not valid UTF-8; ErrorCode_BufferTooSmall
 *          if indexArray cannot grow.
 */
Error StringUTF8_GetCharacterIndexArray(const unsigned char* str, GenericBuffer* indexArray);
