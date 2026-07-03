#pragma once
#include "WRIO.h"
#include "WRUnicode.h"


// Functions.

/**
 * @brief Loads a Unicode database from a UnicodeData.txt file on disk into @p data.
 *
 * Reads the entire file at @p dataBaseFilePath, parses it as the Unicode Character Database UnicodeData.txt
 * format (one record per line, fields separated by ';'), and builds a codepoint-indexed table in @p data.
 * Each parsed line must contain the expected number of semicolon-separated sections; the parser consumes the
 * codepoint, general category, decimal-digit value, numeric value, and the simple uppercase/lowercase mappings,
 * and skips the remaining fields. Blank lines (only spaces/newlines) are ignored. The resulting table spans
 * codepoint 0 up to the largest codepoint seen (capped at an internal maximum), with codepoint 0x0 always
 * populated as a defined entry.
 *
 * On success, @p data takes ownership of a freshly heap-allocated character table; the caller must later
 * release it with UnicodeData_Deconstruct. On failure @p data is left unmodified and any intermediate
 * allocations are released internally.
 * @param dataBaseFilePath UTF-8 path to the UnicodeData.txt file. Must not be NULL.
 * @param data [out] Destination database to populate. Must not be NULL. Pass a fresh/uninitialized
 *        UnicodeData; on success its previous contents (if any) are overwritten, not released.
 * @returns A success Error on success. Raises ErrorCode_IllegalArgument if @p dataBaseFilePath or @p data is
 *          NULL, and ErrorCode_InvalidUnicodeData for malformed input (wrong section count, unrecognized
 *          category, non-hexadecimal codepoint/mapping, bad numeric or digit value, out-of-range digit value).
 *          The error message names the offending line and, when available, the file path.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UnicodeData_Load(const unsigned char* dataBaseFilePath, UnicodeData* data);

/**
 * @brief Loads a Unicode database by reading UnicodeData.txt content from an IO stream into @p data.
 *
 * Behaves like UnicodeData_Load but takes its input from @p stream: it reads the stream to end, null-terminates
 * the buffered text, and parses it with the same UnicodeData.txt rules and ownership semantics. Error messages
 * reference line numbers only (no file path, since the source is a stream).
 *
 * On success @p data takes ownership of a freshly heap-allocated character table that the caller must later
 * release with UnicodeData_Deconstruct. On failure @p data is left unmodified.
 * @param stream The input stream to read the Unicode data from. Must not be NULL; positioned at the data to read.
 * @param data [out] Destination database to populate. Must not be NULL. Pass a fresh/uninitialized UnicodeData.
 * @returns A success Error on success. Raises ErrorCode_IllegalArgument if @p stream or @p data is NULL,
 *          ErrorCode_BufferTooSmall if the read text cannot be null-terminated, and ErrorCode_InvalidUnicodeData
 *          for malformed Unicode content (same conditions as UnicodeData_Load).
 * @note May propagate errors from internal calls (e.g. reading the stream); consult the documentation of called
 *       functions for the full set.
 */
Error UnicodeData_LoadFromStream(IOStream* stream, UnicodeData* data);

/**
 * @brief Initializes @p data as a valid but essentially empty Unicode database.
 *
 * Produces a minimal table containing only the always-defined codepoint 0x0; all other codepoints report as
 * undefined. Useful as a placeholder or default when no UnicodeData.txt source is available. The allocated
 * table is owned by @p data and must be released with UnicodeData_Deconstruct.
 * @param data [out] Destination database to initialize. Must not be NULL. Pass a fresh/uninitialized UnicodeData.
 * @returns A success Error on success, or ErrorCode_IllegalArgument if @p data is NULL.
 */
Error UnicodeData_CreateEmpty(UnicodeData* data);

/**
 * @brief Releases the resources held by a Unicode database and resets it to an empty state.
 *
 * Frees the character table (if any) and sets UnicodeData::_characters to NULL and UnicodeData::_characterCount
 * to 0. Safe to call on a NULL pointer and idempotent: calling it again, or on an already-empty database, has
 * no further effect. After this call @p data must not be used with the Unicode_* query functions until it is
 * re-initialized via one of the load/create entry points.
 * @param data The database to release. May be NULL.
 */
void UnicodeData_Deconstruct(UnicodeData* data);
