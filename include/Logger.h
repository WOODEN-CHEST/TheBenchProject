#pragma once
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRStandardStream.h"
#include "wr/WRFileStream.h"


/**
 * The logging module.
 *
 * A Logger writes timestamped, level-tagged messages to both a log file and standard output. It has
 * two responsibilities:
 *
 *  - Log rotation ("old log moving"): before a fresh session begins, any existing "logs/latest.log"
 *    is moved into "logs/archived/" under a name derived from that old log's own date (see
 *    Logger_ArchiveLatestLog). This is done automatically by Logger_Construct, but is also exposed as
 *    a standalone function.
 *  - Logging ("new log creating and using"): the active session writes to "logs/latest.log". Every
 *    message is prefixed with "[<date>][<time>][<level>] " and terminated with a newline, and is also
 *    echoed to stdout. On Linux the stdout copy is colored per level via ANSI escape sequences; on
 *    other platforms the stdout copy is written without color.
 *
 * A Logger is a value type the caller owns: construct one with Logger_Construct, keep it alive for the
 * lifetime of the session, pass a pointer to it wherever logging is needed, and release it with
 * Logger_Deconstruct. It holds an open file handle and reusable scratch buffers, so it is not copied
 * by value and is not thread-safe (guard it externally if logged from multiple threads).
 *
 * Paths are relative to the process working directory: "logs/latest.log" for the active log and
 * "logs/archived/" for rotated logs.
 */


// Types.
/**
 * @brief Severity of a logged message.
 *
 * The level is rendered into the message prefix (as "Info", "Warning", "Error" or "CRITICAL") and, on
 * Linux, selects the ANSI color used for the stdout copy.
 */
typedef enum LogLevelEnum
{
    /** @brief Informational message about normal operation. */
    LogLevel_Info,
    /** @brief Something unexpected that is recoverable and does not stop the program. */
    LogLevel_Warning,
    /** @brief A failure that affects an operation but not necessarily the whole program. */
    LogLevel_Error,
    /** @brief A severe failure; typically the program cannot sensibly continue. */
    LogLevel_Critical
} LogLevel;

/**
 * @brief A logger that writes to "logs/latest.log" and mirrors output to stdout.
 *
 * A value type owning an open log-file stream, a (non-owning) stdout stream, and reusable scratch
 * buffers used to build each line without allocating per call. Initialize with Logger_Construct and
 * release with Logger_Deconstruct. All underscore-prefixed fields are internal and read-only to code
 * outside this module. Not safe to copy by value; not thread-safe.
 */
typedef struct LoggerStruct
{
    /** @brief Open stream over "logs/latest.log"; owned and closed by Logger_Deconstruct. */
    FileStream _logFileStream;
    /** @brief Stream bound to the process stdout; the underlying handle is not owned. */
    StandardStream _stdoutStream;
    /** @brief Reusable byte buffer used to assemble one formatted log line. */
    GenericBuffer _lineBuffer;
    /** @brief Reusable byte buffer used to render printf-style messages before logging. */
    GenericBuffer _formatBuffer;
    /** @brief Whether the logger has been successfully constructed and is ready to use. */
    bool _isInitialized;
} Logger;


// Functions.
/**
 * @brief Moves an existing "logs/latest.log" into the archive directory, if one exists.
 *
 * If "logs/latest.log" exists, this reads its last-modification date, ensures "logs/archived/" exists,
 * copies the log's contents to "logs/archived/<date>.log" (where <date> is formatted like the log
 * timestamps, e.g. "2026y,07m,05d"), and then deletes the original so the move is complete. If a file
 * with that name already exists, a space and the lowest free number are appended
 * ("...<date> 1.log", "...<date> 2.log", ...) until an unused name is found. If "logs/latest.log" does
 * not exist, this is a successful no-op (nothing to move).
 *
 * This is normally called for you by Logger_Construct; it is exposed so a log can be rotated on demand.
 * @returns A success Error on success (including the "nothing to archive" case). Propagates
 *          filesystem errors from querying, reading, writing or deleting the logs (e.g.
 *          ErrorCode_IO, ErrorCode_DirectoryNotFound), and ErrorCode_BufferTooSmall if a path could
 *          not be built.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error Logger_ArchiveLatestLog(void);

/**
 * @brief Initializes a logger, rotating any previous log and opening a fresh "logs/latest.log".
 *
 * Ensures the "logs" directory exists, archives any existing "logs/latest.log" via
 * Logger_ArchiveLatestLog, opens a fresh "logs/latest.log" for writing, binds stdout, and allocates
 * the reusable scratch buffers. On failure @p self is left uninitialized (safe to pass to
 * Logger_Deconstruct, which will do nothing).
 * @param self [out] The logger to initialize. Must not be NULL. Fully overwritten.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p self is NULL; propagates
 *          filesystem/stream errors from directory creation, archiving, or opening the log/stdout
 *          streams.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error Logger_Construct(Logger* self);

/**
 * @brief Logs a message at the given level.
 *
 * Builds "[<date>][<time>][<level>] <message>", appends a newline, writes it to the log file (flushed)
 * and mirrors it to stdout (colored per level on Linux). @p message is treated as literal text; use
 * Logger_LogFormatted for printf-style formatting.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param level The severity level to tag the message with.
 * @param message Null-terminated UTF-8 message text. Must not be NULL.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p self or @p message is NULL;
 *          ErrorCode_InvalidState if the logger is not initialized; ErrorCode_BufferTooSmall if the
 *          line could not be assembled; propagates ErrorCode_IO and related errors from writing to the
 *          file or stdout stream.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error Logger_Log(Logger* self, LogLevel level, const unsigned char* message);

/**
 * @brief Logs a printf-style formatted message at the given level.
 *
 * Renders @p format against the variadic arguments (C library vsnprintf semantics) and logs the result
 * exactly as Logger_Log would. The caller is responsible for matching conversion specifiers to
 * arguments, as with any printf-family call.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param level The severity level to tag the message with.
 * @param format Null-terminated UTF-8 printf-style format string. Must not be NULL.
 * @param ... Arguments consumed by the conversion specifiers in @p format.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p self or @p format is NULL;
 *          ErrorCode_InvalidState if the logger is not initialized; ErrorCode_InvalidOperation if
 *          formatting fails; ErrorCode_BufferTooSmall if a buffer could not grow; propagates
 *          ErrorCode_IO and related errors from writing.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error Logger_LogFormatted(Logger* self, LogLevel level, const unsigned char* format, ...);

/**
 * @brief Logs a literal message at LogLevel_Info. See Logger_Log.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param message Null-terminated UTF-8 message text. Must not be NULL.
 * @returns The result of logging at LogLevel_Info; see Logger_Log.
 */
Error Logger_LogInfo(Logger* self, const unsigned char* message);

/**
 * @brief Logs a printf-style message at LogLevel_Info. See Logger_LogFormatted.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param format Null-terminated UTF-8 printf-style format string. Must not be NULL.
 * @param ... Arguments consumed by the conversion specifiers in @p format.
 * @returns The result of logging at LogLevel_Info; see Logger_LogFormatted.
 */
Error Logger_LogInfoFormatted(Logger* self, const unsigned char* format, ...);

/**
 * @brief Logs a literal message at LogLevel_Warning. See Logger_Log.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param message Null-terminated UTF-8 message text. Must not be NULL.
 * @returns The result of logging at LogLevel_Warning; see Logger_Log.
 */
Error Logger_LogWarning(Logger* self, const unsigned char* message);

/**
 * @brief Logs a printf-style message at LogLevel_Warning. See Logger_LogFormatted.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param format Null-terminated UTF-8 printf-style format string. Must not be NULL.
 * @param ... Arguments consumed by the conversion specifiers in @p format.
 * @returns The result of logging at LogLevel_Warning; see Logger_LogFormatted.
 */
Error Logger_LogWarningFormatted(Logger* self, const unsigned char* format, ...);

/**
 * @brief Logs a literal message at LogLevel_Error. See Logger_Log.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param message Null-terminated UTF-8 message text. Must not be NULL.
 * @returns The result of logging at LogLevel_Error; see Logger_Log.
 */
Error Logger_LogError(Logger* self, const unsigned char* message);

/**
 * @brief Logs a printf-style message at LogLevel_Error. See Logger_LogFormatted.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param format Null-terminated UTF-8 printf-style format string. Must not be NULL.
 * @param ... Arguments consumed by the conversion specifiers in @p format.
 * @returns The result of logging at LogLevel_Error; see Logger_LogFormatted.
 */
Error Logger_LogErrorFormatted(Logger* self, const unsigned char* format, ...);

/**
 * @brief Logs a literal message at LogLevel_Critical. See Logger_Log.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param message Null-terminated UTF-8 message text. Must not be NULL.
 * @returns The result of logging at LogLevel_Critical; see Logger_Log.
 */
Error Logger_LogCritical(Logger* self, const unsigned char* message);

/**
 * @brief Logs a printf-style message at LogLevel_Critical. See Logger_LogFormatted.
 * @param self The logger. Must not be NULL and must be initialized.
 * @param format Null-terminated UTF-8 printf-style format string. Must not be NULL.
 * @param ... Arguments consumed by the conversion specifiers in @p format.
 * @returns The result of logging at LogLevel_Critical; see Logger_LogFormatted.
 */
Error Logger_LogCriticalFormatted(Logger* self, const unsigned char* format, ...);

/**
 * @brief Releases a logger: flushes and closes the log file and frees its buffers.
 *
 * Closes the owned log-file handle (flushing buffered writes), releases the stdout wrapper without
 * closing stdout, and frees the scratch buffers. Safe to call on a NULL logger or one that was never
 * successfully constructed (treated as a no-op). After this call the logger must not be used again.
 * @param self The logger to release. May be NULL.
 * @returns A success Error on success; the first error encountered while closing the streams
 *          otherwise (all resources are still released).
 */
Error Logger_Deconstruct(Logger* self);
