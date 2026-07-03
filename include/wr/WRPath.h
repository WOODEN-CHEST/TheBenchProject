#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include <stddef.h>


/**
 * @brief Classifies a path as having no type, being absolute (rooted), or being relative.
 *
 * Returned by Path_GetPathType. The distinction is based on whether the path is rooted on the
 * current platform (see Path_IsRooted), not on whether it is fully qualified.
 */
typedef enum PathTypeEnum
{
    /** @brief The path is NULL or empty and therefore has no classifiable type. */
    PathType_None,
    /** @brief The path is rooted (absolute) on the current platform. */
    FilePathType_Absolute,
    /** @brief The path is non-empty but not rooted, i.e. interpreted relative to a working directory. */
    FilePathType_Relative
} PathType;

/**
 * @brief Bit flags selecting which transformations Path_Normalize / Path_IsNormalized apply.
 *
 * Combine flags with bitwise OR. With no flags set the path is reproduced verbatim (after
 * validation), so Path_IsNormalized then reports true only for paths that are already byte-identical
 * to their validated form.
 */
typedef enum PathNormalizeConditionsEnum
{
    /** @brief Apply no transformation; the path is copied through unchanged after validation. */
    PathNormalizeConditions_None = 0,
    /** @brief Rewrite every directory separator to the platform's primary separator. */
    PathNormalizeConditions_FixSeparators = (1 << 0),
    /**
     * @brief Collapse "." segments and resolve ".." against the preceding segment where possible.
     *
     * A "." segment is removed; a ".." segment removes the preceding non-".." segment, and is
     * dropped entirely (rather than retained) when it would climb above a rooted path's root.
     * Leading ".." segments in a relative path are preserved.
     */
    PathNormalizeConditions_CollapseDirectorySegment = (1 << 1),
} PathNormalizeConditions;


/**
 * @brief Produces a copy of @p path whose last entry has its extension replaced by @p newExtension.
 *
 * The destination is treated as a growing UTF-8 string: any existing trailing null terminator is
 * dropped, the new path bytes are appended, and a null terminator is written. On success @p result
 * holds a single continuous null-terminated path. The operation works on the last path entry: if it
 * already has an extension that suffix is removed first, then @p newExtension is appended. A
 * separating '.' is inserted only when @p newExtension is non-empty and does not already begin with
 * '.'. If @p newExtension is NULL this is equivalent to Path_RemoveExtension; if @p newExtension is
 * the empty string the existing extension (and its dot) is removed. When the path has no last entry
 * (it is only a root or trailing separators) or its last entry is "." or "..", the path is copied
 * through unchanged.
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param newExtension New extension with or without a leading dot; may be NULL to mean "remove the
 *        extension", or the empty string for the same effect. No leading dot is required.
 * @param result [out] Destination byte buffer (element size 1). Receives the resulting path appended
 *        as a null-terminated UTF-8 string. Must be a valid byte buffer; must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails path validation;
 *          ErrorCode_ArgumentOutOfRange if the resulting length would overflow; ErrorCode_BufferTooSmall
 *          if the destination cannot be grown to hold the result.
 */
Error Path_ChangeExtension(const unsigned char* path, const unsigned char* newExtension, GenericBuffer* result);

/**
 * @brief Produces a copy of @p path with the extension of its last entry removed.
 *
 * Equivalent to Path_ChangeExtension with an empty new extension. The destination is treated as a
 * growing UTF-8 string (existing trailing null dropped, result appended, null-terminated). If the
 * last entry has no extension, or there is no eligible last entry, the path is copied through
 * unchanged. The trailing '.' that separated the removed extension is also removed.
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param result [out] Destination byte buffer (element size 1). Receives the resulting path as a
 *        null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_ArgumentOutOfRange on length overflow; ErrorCode_BufferTooSmall if the
 *          destination cannot be grown.
 */
Error Path_RemoveExtension(const unsigned char* path, GenericBuffer* result);

/**
 * @brief Extracts the extension of the path's last entry, including its leading dot.
 *
 * An extension is the substring from the last interior '.' of the last entry to its end. A dot only
 * counts when it is neither the first nor the last byte of the entry, so dotfiles such as ".gitignore"
 * and trailing-dot names report no extension. The destination is treated as a growing UTF-8 string
 * (existing trailing null dropped, result appended, null-terminated). When there is no extension an
 * empty string is appended (still null-terminated).
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param result [out] Destination byte buffer (element size 1). Receives the extension (e.g. ".txt")
 *        or an empty string, as a null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_GetExtension(const unsigned char* path, GenericBuffer* result);

/**
 * @brief Reports whether the path's last entry has a file extension.
 *
 * Uses the same rule as Path_GetExtension: a '.' that is neither the first nor last byte of the last
 * entry. Writes the boolean result through @p hasExtension only on success.
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param hasExtension [out] Receives true if the last entry has an extension, false otherwise. Must
 *        not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path or @p hasExtension is
 *          NULL; ErrorCode_InvalidPath if @p path is empty or fails validation.
 */
Error Path_HasExtension(const unsigned char* path, bool* hasExtension);

/**
 * @brief Joins an array of path fragments into a single path, inserting separators as needed.
 *
 * Fragments are concatenated in order. A platform primary separator is inserted between two
 * fragments only when the left fragment does not already end with a separator and the right fragment
 * does not already begin with one, so existing separators are never doubled. Unlike some platform
 * APIs, a rooted fragment after the first is rejected rather than discarding earlier fragments. The
 * destination is treated as a growing UTF-8 string (existing trailing null dropped, result appended,
 * null-terminated). With @p pathCount of 0 an empty string is appended.
 * @param paths Array of @p pathCount UTF-8 path fragments. Must not be NULL when @p pathCount > 0.
 *        Each fragment must be non-NULL, non-empty, and a valid path.
 * @param pathCount Number of fragments in @p paths. May be 0.
 * @param result [out] Destination byte buffer (element size 1). Receives the combined path as a
 *        null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p paths is NULL (with
 *          @p pathCount > 0), if any fragment is NULL, or if @p result is not a byte buffer;
 *          ErrorCode_InvalidPath if any fragment is empty, fails validation, or if any fragment after
 *          the first is rooted; ErrorCode_ArgumentOutOfRange if the combined length would overflow;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_Combine(const unsigned char** paths, size_t pathCount, GenericBuffer* result);

/**
 * @brief Joins exactly two path fragments into one path.
 *
 * Convenience wrapper over Path_Combine for the two-fragment case; the same separator, validation,
 * rooting, and destination-buffer rules apply.
 * @param pathA First (possibly rooted) fragment, UTF-8. Must be non-NULL, non-empty, and valid.
 * @param pathB Second fragment, UTF-8. Must be non-NULL, non-empty, valid, and must not be rooted.
 * @param result [out] Destination byte buffer (element size 1). Receives the combined path as a
 *        null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises the same error codes as Path_Combine (ErrorCode_IllegalArgument,
 *          ErrorCode_InvalidPath, ErrorCode_ArgumentOutOfRange, ErrorCode_BufferTooSmall) under the
 *          same conditions.
 */
Error Path_Append(const unsigned char* pathA, const unsigned char* pathB, GenericBuffer* result);

/**
 * @brief Reports whether the path's final byte is a directory separator.
 *
 * Recognizes either the platform primary or secondary separator (on Linux both are '/'; on Windows
 * '\\' or '/'). This is a pure inspection that performs no validation and allocates nothing.
 * @param path UTF-8 path to inspect. May be NULL or empty.
 * @returns true if @p path is non-empty and ends with a directory separator; false otherwise
 *          (including when @p path is NULL or empty).
 */
bool Path_EndsInDirectorySeparator(const unsigned char* path);

/**
 * @brief Produces the parent (containing directory) path of @p path.
 *
 * Trailing separators are ignored, then the last entry and the separators preceding it are removed.
 * For a rooted path that has no entries beyond the root, the root itself is returned. The result has
 * no trailing separator except where it is the root. The destination is treated as a growing UTF-8
 * string (existing trailing null dropped, result appended, null-terminated); when the path has no
 * parent the appended content may be empty.
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param result [out] Destination byte buffer (element size 1). Receives the parent path as a
 *        null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_GetParentPath(const unsigned char* path, GenericBuffer* result);

/**
 * @brief Produces the name of the path's last entry (its final file or directory component).
 *
 * Trailing separators are ignored; the substring after the last separator (and past the root) is
 * returned, including any extension. For a path consisting only of a root the appended content is
 * empty. The destination is treated as a growing UTF-8 string (existing trailing null dropped,
 * result appended, null-terminated).
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param result [out] Destination byte buffer (element size 1). Receives the last entry name as a
 *        null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_GetLastEntryName(const unsigned char* path, GenericBuffer* result);

/**
 * @brief Produces the stem of the path's last entry: its name with the extension removed.
 *
 * Behaves like Path_GetLastEntryName but, when the last entry has an extension (per the
 * Path_GetExtension rule), the extension and its separating dot are excluded. If there is no
 * extension the whole entry name is returned. The destination is treated as a growing UTF-8 string
 * (existing trailing null dropped, result appended, null-terminated).
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param result [out] Destination byte buffer (element size 1). Receives the entry stem as a
 *        null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_GetLastEntryStem(const unsigned char* path, GenericBuffer* result);

/**
 * @brief Reports whether @p entryName is a valid single path entry name.
 *
 * A valid entry name is non-empty, valid UTF-8, contains no null byte and no directory separator, is
 * not "." or "..", and (on Windows) avoids reserved characters, reserved device names, and a trailing
 * space or dot. Because a separator is disallowed, only a single component may be passed, not a
 * multi-segment path. This is the non-throwing counterpart of Path_ValidateEntryName.
 * @param entryName Candidate entry name, UTF-8. May be NULL or empty (both report false).
 * @returns true if @p entryName is a valid entry name; false otherwise.
 */
bool Path_IsEntryNameValid(const unsigned char* entryName);

/**
 * @brief Validates a single path entry name, returning a descriptive error when it is invalid.
 *
 * Applies the same rules as Path_IsEntryNameValid but reports the specific failure as an Error rather
 * than a boolean.
 * @param entryName Candidate entry name, UTF-8. May be NULL.
 * @returns Success if @p entryName is valid. Raises ErrorCode_IllegalArgument if @p entryName is NULL;
 *          ErrorCode_InvalidPath if it is empty, is not valid UTF-8, is "." or "..", contains a null
 *          byte or directory separator, or (on Windows) violates a platform-specific name rule. The
 *          error message identifies the offending name and reason.
 */
Error Path_ValidateEntryName(const unsigned char* entryName);

/**
 * @brief Reports whether @p path is a structurally valid path on the current platform.
 *
 * Validity requires non-NULL, non-empty, valid UTF-8 text whose every segment is a valid entry name
 * ("." and ".." are permitted as whole segments), plus any platform root rules (on Windows: device
 * paths beginning with \\.\ or \\?\ are rejected, and UNC paths must name both a server and a share).
 * This is the non-throwing counterpart of Path_Validate.
 * @param path Candidate path, UTF-8. May be NULL or empty (both report false).
 * @returns true if @p path is valid; false otherwise.
 */
bool Path_IsValid(const unsigned char* path);

/**
 * @brief Validates a path, returning a descriptive error when it is invalid.
 *
 * Applies the same rules as Path_IsValid but reports the specific failure as an Error.
 * @param path Candidate path, UTF-8. May be NULL.
 * @returns Success if @p path is valid. Raises ErrorCode_IllegalArgument if @p path is NULL;
 *          ErrorCode_InvalidPath if it is empty, is not valid UTF-8, violates a platform root rule, or
 *          contains an invalid segment. The error message identifies the path and reason.
 */
Error Path_Validate(const unsigned char* path);

/**
 * @brief Classifies @p path as none, absolute, or relative.
 *
 * Returns PathType_None for a NULL or empty path; otherwise FilePathType_Absolute when the path is
 * rooted on the current platform (see Path_IsRooted) and FilePathType_Relative when it is not. No
 * validation is performed beyond the NULL/empty check.
 * @param path UTF-8 path to classify. May be NULL or empty.
 * @returns The PathType classification of @p path.
 */
PathType Path_GetPathType(const unsigned char* path);

/**
 * @brief Produces a normalized form of @p path according to @p conditions.
 *
 * The transformations applied are selected by @p conditions: PathNormalizeConditions_FixSeparators
 * rewrites every separator to the platform primary separator (otherwise the path's existing
 * separator style is preserved), and PathNormalizeConditions_CollapseDirectorySegment removes "."
 * segments and resolves ".." against preceding segments. With no flags the validated path is copied
 * through unchanged. A purely relative path that collapses to nothing normalizes to ".". The
 * destination is treated as a growing UTF-8 string (existing trailing null dropped, result appended,
 * null-terminated).
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param conditions Bit flags selecting the transformations to apply.
 * @param buffer [out] Destination byte buffer (element size 1). Receives the normalized path as a
 *        null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p buffer is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_Normalize(const unsigned char* path, PathNormalizeConditions conditions, GenericBuffer* buffer);

/**
 * @brief Reports whether @p path already equals its normalized form for the given @p conditions.
 *
 * Equivalent to normalizing @p path under @p conditions and comparing the result to the original; on
 * Windows the comparison is case-insensitive, elsewhere it is exact. An invalid path reports false.
 * @param path UTF-8 path to test. May be NULL or empty (reported as not normalized).
 * @param conditions Bit flags describing the normalization to test against.
 * @returns true if @p path is valid and already in the normalized form; false otherwise.
 */
bool Path_IsNormalized(const unsigned char* path, PathNormalizeConditions conditions);

/**
 * @brief Reports whether @p path begins with a root on the current platform.
 *
 * "Rooted" means the path starts from a root rather than being interpreted relative to the current
 * directory, but does not by itself imply the path is fully qualified. On Linux a path is rooted when
 * it begins with '/'. On Windows a leading "X:\\", a leading separator (current-drive root), a UNC
 * prefix, or a \\.\ / \\?\ device prefix are rooted, whereas a drive-relative "X:foo" is not. No
 * validation is performed.
 * @param path UTF-8 path to inspect. May be NULL or empty (reported as not rooted).
 * @returns true if @p path is rooted; false otherwise.
 */
bool Path_IsRooted(const unsigned char* path);

/**
 * @brief Reports whether @p path is fully qualified (rooted and not dependent on any current directory).
 *
 * A fully qualified path needs no current-directory or current-drive context to resolve. On Linux any
 * '/'-rooted path qualifies. On Windows "X:\\..." and a UNC path naming both server and share qualify,
 * while a bare-separator current-drive path, a drive-relative "X:foo", and \\.\ / \\?\ device paths do
 * not. No validation is performed.
 * @param path UTF-8 path to inspect. May be NULL or empty (reported as not fully qualified).
 * @returns true if @p path is fully qualified; false otherwise.
 */
bool Path_IsFullyQualified(const unsigned char* path);

/**
 * @brief Produces the root portion of @p path (the prefix that establishes its root).
 *
 * The root is the leading run that roots the path: "/" on Linux; on Windows "X:\\", "X:", a leading
 * separator, or the server/share portion of a UNC path, as applicable. For a relative path with no
 * root the appended content is empty. The destination is treated as a growing UTF-8 string (existing
 * trailing null dropped, result appended, null-terminated).
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param result [out] Destination byte buffer (element size 1). Receives the root as a null-terminated
 *        UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_GetRoot(const unsigned char* path, GenericBuffer* result);

/**
 * @brief Reports whether @p childPath lies within @p parentPath.
 *
 * Both paths are first normalized with separator fixing and directory-segment collapsing, then
 * compared. They must share the same path type (both absolute or both relative); an empty normalized
 * parent never matches. A child equal to the parent counts as a sub-path. Otherwise the child must
 * begin with the parent followed by a directory separator. On Windows the comparison is
 * case-insensitive; elsewhere it is exact. If either path fails to normalize (e.g. it is invalid),
 * the result is false.
 * @param parentPath Candidate ancestor path, UTF-8. May be NULL or invalid (yields false).
 * @param childPath Candidate descendant path, UTF-8. May be NULL or invalid (yields false).
 * @returns true if @p childPath is equal to or nested under @p parentPath; false otherwise.
 */
bool Path_IsSubPath(const unsigned char* parentPath, const unsigned char* childPath);

/**
 * @brief Produces a copy of @p path with any trailing directory separators removed.
 *
 * Separators belonging to the path's root are preserved; only separators past the root are trimmed,
 * so a bare root such as "/" is returned unchanged. The destination is treated as a growing UTF-8
 * string (existing trailing null dropped, result appended, null-terminated).
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param result [out] Destination byte buffer (element size 1). Receives the trimmed path as a
 *        null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_TrimTrailingSeparator(const unsigned char* path, GenericBuffer* result);

/**
 * @brief Produces a copy of @p path that ends with exactly one directory separator.
 *
 * If @p path already ends in a separator it is copied unchanged; otherwise the platform primary
 * separator is appended. The destination is treated as a growing UTF-8 string (existing trailing null
 * dropped, result appended, null-terminated).
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param result [out] Destination byte buffer (element size 1). Receives the path with a trailing
 *        separator as a null-terminated UTF-8 string. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL or @p result is
 *          not a byte buffer; ErrorCode_InvalidPath if @p path is empty or fails validation;
 *          ErrorCode_BufferTooSmall if the destination cannot be grown.
 */
Error Path_EnsureTrailingSeparator(const unsigned char* path, GenericBuffer* result);

/**
 * @brief Reports whether @p path contains any "." or ".." directory segment past its root.
 *
 * Scans the segments after the root and returns true if any is the current-directory "." or
 * parent-directory ".." segment. Performs no validation and allocates nothing.
 * @param path UTF-8 path to inspect. May be NULL or empty (reported as containing none).
 * @returns true if a "." or ".." segment is present beyond the root; false otherwise.
 */
bool Path_ContainsDirectorySegments(const unsigned char* path);

/**
 * @brief Splits @p path into its directory segments past the root.
 *
 * Each segment (the text between separators, with the root prefix and all separators skipped) is
 * appended to @p strBuffer as its own null-terminated UTF-8 string, and the byte offset of that
 * segment's first byte within @p strBuffer is appended to @p segmentIndexBuffer (an element-size
 * sizeof(size_t) buffer). Recover the i-th segment with `strBuffer->_data + offset[i]`. Offsets,
 * rather than pointers, are returned so they remain valid across any later reallocation of
 * @p strBuffer.
 *
 * Both destinations are populated together: on success @p strBuffer holds the concatenated
 * null-terminated segment strings and @p segmentIndexBuffer holds one offset per segment, with each
 * buffer's count grown by the number of bytes / segments written respectively. The new data is
 * appended after any prior contents of the destination buffers, which are preserved, so callers may
 * accumulate the segments of several paths across multiple calls; each recorded offset indexes from
 * the start of @p strBuffer's data and so stays valid for `strBuffer->_data + offset` regardless of
 * what came before. A path with no segments beyond the root leaves both buffers unchanged.
 * @param path Source path, UTF-8, must not be NULL or empty and must be a valid path.
 * @param strBuffer [out] Destination byte buffer (element size 1). Receives the segment bytes as
 *        consecutive null-terminated UTF-8 strings. Must not be NULL.
 * @param segmentIndexBuffer [out] Destination size_t buffer (element size sizeof(size_t)). Receives
 *        one byte offset per segment, indexing into @p strBuffer. Must not be NULL.
 * @returns Success on completion. Raises ErrorCode_IllegalArgument if @p path is NULL, if @p strBuffer
 *          is not a byte buffer, or if @p segmentIndexBuffer is not a size_t index buffer;
 *          ErrorCode_InvalidPath if @p path is empty or fails validation; ErrorCode_ArgumentOutOfRange
 *          if the required string size would overflow; ErrorCode_BufferTooSmall if either destination
 *          cannot be grown to the required capacity.
 */
Error Path_Split(const unsigned char* path, GenericBuffer* strBuffer, GenericBuffer* segmentIndexBuffer);