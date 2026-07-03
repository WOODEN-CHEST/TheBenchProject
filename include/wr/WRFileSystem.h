#pragma once
#include "WRError.h"
#include "WRMemory.h"
#include "WRFileStream.h"
#include <stdbool.h>
#include <time.h>
#include <stddef.h>


// Types.
/**
 * @brief Opaque, forward-only iterator over the entries of a directory.
 *
 * Created by one of the FileSystem_GetEntries / FileSystem_GetFiles / FileSystem_GetDirectories
 * functions, which allocate it on the heap and return ownership to the caller through an out-pointer.
 * The caller advances it with DirectoryEntryEnumerator_HasNext / DirectoryEntryEnumerator_Next and
 * MUST release it with DirectoryEntryEnumerator_Deconstruct exactly once when finished, whether or
 * not enumeration was carried to completion. The enumerator does not support rewinding: each entry is
 * produced at most once and only forward progress is possible. It is not thread-safe; a single
 * enumerator must not be used concurrently from multiple threads.
 */
typedef struct DirectoryEntryEnumeratorStruct DirectoryEntryEnumerator;

/**
 * @brief Selects how deep a directory enumeration descends.
 */
typedef enum DirectorySearchOptionEnum
{
    /** @brief Enumerate only the immediate children of the requested directory; do not descend into subdirectories. */
    DirectorySearchOption_TopLevel,
    /** @brief Recurse through the entire subtree, enumerating entries in every nested subdirectory. */
    DirectorySearchOption_All
} DirectorySearchOption;

/**
 * @brief Classifies a file system entry as a regular file, a directory, or a symbolic link.
 *
 * The type is determined without following symbolic links: a link is reported as
 * FileSystemEntryType_SymbolicLink rather than as the type of its target.
 */
typedef enum FileSystemEntryTypeEnum
{
    /** @brief A regular file (anything that is neither a directory nor a symbolic link). */
    FileSystemEntryType_File,
    /** @brief A directory. */
    FileSystemEntryType_Directory,
    /** @brief A symbolic link / reparse point, reported without resolving its target. */
    FileSystemEntryType_SymbolicLink
} FileSystemEntryType;

/**
 * @brief Describes a single file system entry: its location, name, type, timestamps, and size.
 *
 * Populated by FileSystem_GetEntryInfo and by DirectoryEntryEnumerator_Next. The _path and _name
 * members are heap-allocated UTF-8 strings owned by this struct; when the struct is no longer needed
 * the owner MUST call FileSystemEntryInfo_Deconstruct to free them (DirectoryEntryEnumerator_Next
 * transfers ownership of a populated info to the caller, who then owns this cleanup obligation).
 * Timestamp fields are seconds since the Unix epoch; a field is 0 when the platform does not provide
 * that timestamp.
 */
typedef struct FileSystemEntryInfoStruct
{
    /** @brief Heap-allocated, null-terminated UTF-8 full path to the entry as supplied to / built by the query; owned by this struct. */
    const unsigned char* _path;
    /** @brief Heap-allocated, null-terminated UTF-8 final path component (the entry's own name) of the entry; owned by this struct. */
    const unsigned char* _name;
    /** @brief Whether the entry is a file, directory, or symbolic link (links are not resolved). */
    FileSystemEntryType _entryType;
    /** @brief Last-access time in seconds since the Unix epoch, or 0 if unavailable. */
    time_t _lastAccessTime;
    /** @brief Last-modification (content write) time in seconds since the Unix epoch, or 0 if unavailable. */
    time_t _lastModificationTime;
    /** @brief Creation time in seconds since the Unix epoch, or 0 if the platform does not record it (e.g. typical Linux). */
    time_t _creationTime;
    /** @brief Metadata/status-change time in seconds since the Unix epoch, or 0 if unavailable. */
    time_t _statusChangeTime;
    /** @brief Size of the entry's contents in bytes. */
    size_t _sizeInBytes;
    /** @brief Whether the entry is considered hidden by the platform's conventions (e.g. a leading-dot name, or the hidden attribute). */
    bool _isHidden;
} FileSystemEntryInfo;

/**
 * @brief Mode in which a file stream is opened, combining access intent (read/write/append) with
 *        text vs. binary handling.
 *
 * Read modes open an existing file for reading; write modes create the file or truncate an existing
 * one; append modes create the file if missing and position writes at the end. Text modes may apply
 * platform newline translation, while binary modes transfer bytes verbatim.
 */
typedef enum FileOpenModeEnum
{
    /** @brief Open an existing file for reading in text mode. */
    FileOpenMode_ReadText,
    /** @brief Create or truncate a file for writing in text mode. */
    FileOpenMode_WriteText,
    /** @brief Open or create a file for appending in text mode (writes go to the end). */
    FileOpenMode_AppendText,
    /** @brief Open an existing file for reading in binary mode (bytes transferred verbatim). */
    FileOpenMode_ReadBinary,
    /** @brief Create or truncate a file for writing in binary mode (bytes transferred verbatim). */
    FileOpenMode_WriteBinary,
    /** @brief Open or create a file for appending in binary mode (writes go to the end). */
    FileOpenMode_AppendBinary
} FileOpenMode;




// Functions.
/**
 * @brief Begins enumerating ALL entries (files, directories, and symbolic links) under a directory.
 *
 * Allocates a new DirectoryEntryEnumerator rooted at @p path and returns it through @p enumerator.
 * The "." and ".." pseudo-entries are never reported. When @p searchOption is
 * DirectorySearchOption_All the enumerator descends into every subdirectory; with
 * DirectorySearchOption_TopLevel only the immediate children are reported. No directory contents are
 * read yet at construction time beyond opening the root directory; entries are produced lazily as the
 * enumerator is advanced. On success the caller owns the returned enumerator and MUST release it with
 * DirectoryEntryEnumerator_Deconstruct.
 * @param path Null-terminated UTF-8 path of the directory to enumerate. Must not be NULL or empty and
 *        must be a valid path; must refer to an existing directory.
 * @param searchOption Whether to enumerate only the top level or recurse through the whole subtree.
 * @param enumerator [out] Receives the newly allocated enumerator on success, or NULL on failure.
 *        Must not be NULL.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p enumerator is NULL or @p path
 *          is NULL; ErrorCode_InvalidPath if @p path is empty or otherwise invalid;
 *          ErrorCode_FileNotFound / ErrorCode_DirectoryNotFound if the directory cannot be opened
 *          because it does not exist; ErrorCode_IO for other failures opening the directory.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_GetEntries(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

/**
 * @brief Begins enumerating only the regular-file entries under a directory.
 *
 * Identical to FileSystem_GetEntries except that the produced sequence is filtered to entries of type
 * FileSystemEntryType_File; directories and symbolic links are skipped. When @p searchOption is
 * DirectorySearchOption_All the traversal still descends into subdirectories (the directories
 * themselves are simply not reported) so that files in nested directories are produced. On success the
 * caller owns the returned enumerator and MUST release it with DirectoryEntryEnumerator_Deconstruct.
 * @param path Null-terminated UTF-8 path of the directory to enumerate. Must not be NULL or empty and
 *        must be a valid path; must refer to an existing directory.
 * @param searchOption Whether to enumerate only the top level or recurse through the whole subtree.
 * @param enumerator [out] Receives the newly allocated enumerator on success, or NULL on failure.
 *        Must not be NULL.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p enumerator is NULL or @p path
 *          is NULL; ErrorCode_InvalidPath if @p path is empty or otherwise invalid;
 *          ErrorCode_FileNotFound / ErrorCode_DirectoryNotFound if the directory cannot be opened
 *          because it does not exist; ErrorCode_IO for other failures opening the directory.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_GetFiles(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

/**
 * @brief Begins enumerating only the directory entries under a directory.
 *
 * Identical to FileSystem_GetEntries except that the produced sequence is filtered to entries of type
 * FileSystemEntryType_Directory; files and symbolic links are skipped. When @p searchOption is
 * DirectorySearchOption_All the traversal descends into every subdirectory and reports each one. On
 * success the caller owns the returned enumerator and MUST release it with
 * DirectoryEntryEnumerator_Deconstruct.
 * @param path Null-terminated UTF-8 path of the directory to enumerate. Must not be NULL or empty and
 *        must be a valid path; must refer to an existing directory.
 * @param searchOption Whether to enumerate only the top level or recurse through the whole subtree.
 * @param enumerator [out] Receives the newly allocated enumerator on success, or NULL on failure.
 *        Must not be NULL.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p enumerator is NULL or @p path
 *          is NULL; ErrorCode_InvalidPath if @p path is empty or otherwise invalid;
 *          ErrorCode_FileNotFound / ErrorCode_DirectoryNotFound if the directory cannot be opened
 *          because it does not exist; ErrorCode_IO for other failures opening the directory.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_GetDirectories(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

/**
 * @brief Reports whether the enumerator has at least one more matching entry, without consuming it.
 *
 * Drives the enumeration forward enough to determine whether another entry that satisfies the
 * enumerator's filter and search option exists, buffering that entry internally so a subsequent
 * DirectoryEntryEnumerator_Next returns it. Calling this repeatedly without an intervening
 * DirectoryEntryEnumerator_Next does not advance past the buffered entry (it stays available). Because
 * it may need to read directory contents to find the next match, it can fail with I/O errors.
 * @param enumerator The enumerator to query. Must not be NULL.
 * @param hasNext [out] Set to true if another matching entry is available, false if enumeration is
 *        exhausted. Must not be NULL. Set to false before any work is done.
 * @returns A success Error on success (with @p hasNext set). ErrorCode_IllegalArgument if @p enumerator
 *          or @p hasNext is NULL; an I/O-related error (e.g. ErrorCode_IO) if reading the underlying
 *          directory fails.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error DirectoryEntryEnumerator_HasNext(DirectoryEntryEnumerator* enumerator, bool* hasNext);

/**
 * @brief Retrieves the next matching entry and advances the enumerator past it.
 *
 * Returns the next entry satisfying the enumerator's filter and search option (consuming any entry
 * previously buffered by DirectoryEntryEnumerator_HasNext, otherwise advancing to find one). On
 * success @p outInfo is filled in and OWNERSHIP of its heap-allocated _path and _name strings transfers
 * to the caller, who MUST eventually call FileSystemEntryInfo_Deconstruct on @p outInfo to release
 * them. It is an error to call this when no further entries remain.
 * @param enumerator The enumerator to advance. Must not be NULL.
 * @param outInfo [out] Receives the next entry's information on success. Must not be NULL. Its prior
 *        contents are overwritten (not freed), so it should be a fresh/empty struct.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p enumerator or @p outInfo is
 *          NULL; ErrorCode_InvalidOperation if the enumerator has no more matching entries; an
 *          I/O-related error (e.g. ErrorCode_IO) if reading the underlying directory fails.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error DirectoryEntryEnumerator_Next(DirectoryEntryEnumerator* enumerator, FileSystemEntryInfo* outInfo);

/**
 * @brief Releases an enumerator and all resources it holds, including any open native directory handles.
 *
 * Closes every directory level still open in the traversal, frees any entry that was buffered but not
 * yet consumed (including its strings), and frees the enumerator object itself. After this call the
 * pointer must not be used again. Passing NULL is allowed and is a no-op. Intended to be called exactly
 * once per enumerator obtained from FileSystem_GetEntries / FileSystem_GetFiles /
 * FileSystem_GetDirectories.
 * @param enumerator The enumerator to release. May be NULL, in which case the call does nothing.
 * @returns Always a success Error.
 */
Error DirectoryEntryEnumerator_Deconstruct(DirectoryEntryEnumerator* enumerator);

/**
 * @brief Queries metadata for the file system entry at a path (without following symbolic links).
 *
 * Fills @p outInfo with the entry's full path, name, type, timestamps, size, and hidden flag. A
 * symbolic link is described as a link itself, not as its target. On success @p outInfo owns
 * heap-allocated _path and _name strings; the caller MUST release them with
 * FileSystemEntryInfo_Deconstruct. @p outInfo is fully overwritten (its prior contents are not freed),
 * so pass a fresh/empty struct.
 * @param path Null-terminated UTF-8 path to query. Must not be NULL or empty and must be a valid path.
 * @param outInfo [out] Receives the entry information on success. Must not be NULL.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p outInfo or @p path is NULL;
 *          ErrorCode_InvalidPath if @p path is empty or otherwise invalid; ErrorCode_FileNotFound or
 *          ErrorCode_DirectoryNotFound if no entry exists at @p path; ErrorCode_ArgumentOutOfRange if
 *          the entry's size exceeds the supported range; ErrorCode_IO for other failures retrieving
 *          metadata.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_GetEntryInfo(const unsigned char* path, FileSystemEntryInfo* outInfo);

/**
 * @brief Creates a single directory at the given path.
 *
 * Creates exactly one directory; the parent directory must already exist (use
 * FileSystem_CreateAllDirectories to create missing parents). It is an error if an entry already
 * exists at @p path.
 * @param path Null-terminated UTF-8 path of the directory to create. Must not be NULL or empty and
 *        must be a valid path.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p path is NULL;
 *          ErrorCode_InvalidPath if @p path is empty or otherwise invalid; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if a parent directory does not exist; ErrorCode_IO for other
 *          failures (including when something already exists at @p path).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_CreateDirectory(const unsigned char* path);

/**
 * @brief Creates a directory and any missing parent directories along its path.
 *
 * Walks @p path from its root, creating each directory segment that does not already exist, so that on
 * success the full directory chain exists. Segments that already exist as directories are left alone; it
 * is NOT an error for parts (or all) of the path to already exist. If, however, a path component already
 * exists as a non-directory entry, the operation fails. Succeeds (creating nothing further) if the whole
 * path already exists as a directory.
 * @param path Null-terminated UTF-8 path of the directory chain to create. Must not be NULL or empty
 *        and must be a valid path.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p path is NULL;
 *          ErrorCode_InvalidPath if @p path is empty or otherwise invalid; ErrorCode_InvalidOperation
 *          if a path component already exists as a non-directory entry; ErrorCode_IO for other I/O
 *          failures while creating a directory.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_CreateAllDirectories(const unsigned char* path);

/**
 * @brief Opens a file as a FileStream in the requested mode.
 *
 * Initializes @p stream as a stream over the file at @p path. The capabilities of the resulting stream
 * follow @p mode: read modes yield a readable, seekable stream; write and append modes yield a writable,
 * seekable stream. Write modes truncate/create the file, append modes create it if absent and direct
 * writes to the end, and read modes require the file to already exist. @p stream is fully overwritten,
 * so its prior contents are ignored. On success the caller owns the stream and MUST release it with
 * FileStream_Deconstruct (which closes the underlying handle).
 * @param path Null-terminated UTF-8 path of the file. Must not be NULL or empty and must be a valid
 *        path.
 * @param mode The access/text mode to open with.
 * @param stream [out] Receives the opened stream on success. Must not be NULL.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p stream or @p path is NULL, or if
 *          @p mode is not a recognized FileOpenMode; ErrorCode_InvalidPath if @p path is empty or
 *          otherwise invalid; ErrorCode_FileNotFound / ErrorCode_DirectoryNotFound when the file (or a
 *          parent) does not exist for a mode that requires it; ErrorCode_IO for other failures opening
 *          the file.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_OpenFileStream(const unsigned char* path, FileOpenMode mode, FileStream* stream);

/**
 * @brief Reads the entire contents of a file as text into a byte buffer and null-terminates it.
 *
 * Opens @p path for binary reading, grows @p destination to hold the whole file plus a terminating null
 * byte, appends the file's bytes, and writes a trailing null terminator so the result can be used as a
 * C string. The bytes are read verbatim (no newline translation). @p destination must be a byte buffer
 * (element size 1); the data is appended at the buffer's current count, and the buffer's allocator is
 * used to grow it as needed.
 * @param path Null-terminated UTF-8 path of the file to read. Must not be NULL or empty and must be a
 *        valid path.
 * @param destination [out] Byte buffer that receives the file contents followed by a null terminator.
 *        Must not be NULL and must have element size 1.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p path is NULL or @p destination is
 *          NULL; ErrorCode_IllegalArgument if @p destination is not a byte buffer; ErrorCode_InvalidPath
 *          if @p path is empty or otherwise invalid; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if the file does not exist; ErrorCode_ArgumentOutOfRange if the
 *          file size plus the terminator exceeds the supported range; ErrorCode_BufferTooSmall if the
 *          buffer cannot be grown to the required capacity; ErrorCode_IO for other read failures.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_ReadAllText(const unsigned char* path, GenericBuffer* destination);

/**
 * @brief Reads the entire contents of a file as raw bytes into a byte buffer.
 *
 * Opens @p path for binary reading, grows @p destination to hold the whole file, and appends the file's
 * bytes verbatim. Unlike FileSystem_ReadAllText no null terminator is added. @p destination must be a
 * byte buffer (element size 1); data is appended at the buffer's current count and the buffer's
 * allocator is used to grow it as needed.
 * @param path Null-terminated UTF-8 path of the file to read. Must not be NULL or empty and must be a
 *        valid path.
 * @param destination [out] Byte buffer that receives the file contents. Must not be NULL and must have
 *        element size 1.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p path is NULL or @p destination is
 *          NULL; ErrorCode_IllegalArgument if @p destination is not a byte buffer; ErrorCode_InvalidPath
 *          if @p path is empty or otherwise invalid; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if the file does not exist; ErrorCode_BufferTooSmall if the
 *          buffer cannot be grown to the required capacity; ErrorCode_IO for other read failures.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_ReadAllBytes(const unsigned char* path, GenericBuffer* destination);

/**
 * @brief Writes a UTF-8 string to a file as text, replacing the file's previous contents.
 *
 * Creates the file if it does not exist or truncates it if it does (text write mode), then writes the
 * bytes of @p text up to but not including its terminating null. The file is closed before returning.
 * @param path Null-terminated UTF-8 path of the file to write. Must not be NULL or empty and must be a
 *        valid path.
 * @param text Null-terminated UTF-8 text to write. Must not be NULL (an empty string writes an empty
 *        file).
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p text or @p path is NULL;
 *          ErrorCode_InvalidPath if @p path is empty or otherwise invalid; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if a parent directory does not exist; ErrorCode_IO for other
 *          failures opening or writing the file.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_WriteAllText(const unsigned char* path, const unsigned char* text);

/**
 * @brief Writes a block of raw bytes to a file, replacing the file's previous contents.
 *
 * Creates the file if it does not exist or truncates it if it does (binary write mode), then writes
 * exactly @p byteCount bytes from @p bytes verbatim. The file is closed before returning.
 * @param path Null-terminated UTF-8 path of the file to write. Must not be NULL or empty and must be a
 *        valid path.
 * @param bytes Pointer to the bytes to write. May be NULL only if @p byteCount is 0; otherwise must not
 *        be NULL.
 * @param byteCount Number of bytes to write from @p bytes. May be 0 to produce an empty file.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p path is NULL, or if @p bytes is
 *          NULL while @p byteCount is greater than 0; ErrorCode_InvalidPath if @p path is empty or
 *          otherwise invalid; ErrorCode_FileNotFound / ErrorCode_DirectoryNotFound if a parent
 *          directory does not exist; ErrorCode_IO for other failures opening or writing the file.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_WriteAllBytes(const unsigned char* path, const unsigned char* bytes, size_t byteCount);

/**
 * @brief Deletes the file system entry at a path.
 *
 * Determines the entry's type and removes it: a directory is removed (and must be empty for this to
 * succeed), while a file or symbolic link is unlinked. The entry must exist. The link itself is removed
 * rather than its target.
 * @param path Null-terminated UTF-8 path of the entry to delete. Must not be NULL or empty and must be
 *        a valid path.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p path is NULL;
 *          ErrorCode_InvalidPath if @p path is empty or otherwise invalid; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if no entry exists at @p path; ErrorCode_IO for other failures
 *          (for example attempting to remove a non-empty directory).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_DeleteEntry(const unsigned char* path);

/**
 * @brief Moves or renames an entry from one path to another.
 *
 * Relocates the entry at @p oldPath to @p newPath. Because this maps onto a native move/rename, it can
 * also serve as a rename within the same directory. Replacement behavior when an entry already exists at
 * @p newPath is platform-defined and is not guaranteed by this contract.
 * @param oldPath Null-terminated UTF-8 path of the existing entry. Must not be NULL or empty and must be
 *        a valid path.
 * @param newPath Null-terminated UTF-8 destination path. Must not be NULL or empty and must be a valid
 *        path.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p oldPath or @p newPath is NULL;
 *          ErrorCode_InvalidPath if either path is empty or otherwise invalid; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if the source does not exist or a destination parent is missing;
 *          ErrorCode_IO for other failures performing the move.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_MoveEntry(const unsigned char* oldPath, const unsigned char* newPath);

/**
 * @brief Renames an entry in place, keeping it in its current parent directory.
 *
 * Computes the parent directory of @p path and moves the entry to a sibling whose name is @p newName,
 * effectively changing only the final path component. @p newName is a bare entry name, not a path, and
 * is validated as such (it must not contain path separators or be otherwise invalid as a name).
 * @param path Null-terminated UTF-8 path of the existing entry to rename. Must not be NULL or empty and
 *        must be a valid path.
 * @param newName Null-terminated UTF-8 new name for the entry (a single path component, not a path).
 *        Must not be NULL or empty and must be a valid entry name.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p path or @p newName is NULL;
 *          ErrorCode_InvalidPath if @p path is empty/invalid or if @p newName is empty/invalid as a
 *          name; ErrorCode_FileNotFound / ErrorCode_DirectoryNotFound if the entry does not exist;
 *          ErrorCode_IO for other failures performing the rename.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_RenameEntry(const unsigned char* path, const unsigned char* newName);

/**
 * @brief Creates an empty file at a path, truncating any existing file there.
 *
 * Opens the path for binary writing (which creates the file if absent or truncates it to zero length if
 * present) and immediately closes it, leaving an empty file. Any parent directory must already exist.
 * @param path Null-terminated UTF-8 path of the file to create. Must not be NULL or empty and must be a
 *        valid path.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p path is NULL;
 *          ErrorCode_InvalidPath if @p path is empty or otherwise invalid; ErrorCode_FileNotFound /
 *          ErrorCode_DirectoryNotFound if a parent directory does not exist; ErrorCode_IO for other
 *          failures creating the file.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error FileSystem_CreateFile(const unsigned char* path);

/**
 * @brief Releases the heap-allocated strings owned by a FileSystemEntryInfo and clears it.
 *
 * Frees the _path and _name strings (if any) held by @p self and zeroes the struct so it holds no
 * dangling pointers. Call this exactly once on every FileSystemEntryInfo populated by
 * FileSystem_GetEntryInfo or handed back by DirectoryEntryEnumerator_Next. Passing NULL is allowed and
 * is a no-op; calling it on an already-cleared (all-zero) info is also safe.
 * @param self The entry info to release. May be NULL, in which case the call does nothing.
 */
void FileSystemEntryInfo_Deconstruct(FileSystemEntryInfo* self);
