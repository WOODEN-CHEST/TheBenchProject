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
 * by value.
 *
 * Usage rules:
 *  - Main thread only. The logger shares its line and format buffers between calls and is not
 *    thread-safe; code running on another thread (e.g. an audio callback) must never log.
 *  - Never log per tick from the update loop. Every line is flushed to the file and to stdout
 *    immediately (so a crash never loses it), which makes logging far too slow for hot paths. Log
 *    state changes instead: a recoverable failure is logged once, when it first happens.
 *  - The *Formatted functions are printf-style. With GCC their arguments are checked against the
 *    format at compile time (see LOGGER_CHECK_FORMAT). Stick to %s, %d and %f, and print 64-bit or
 *    size_t values as (uint64_t)/(int64_t) with the PRIu64/PRId64 macros from <inttypes.h>, because
 *    "long" is 32-bit on Windows.
 *
 * Paths live under a base directory passed to Logger_Construct: "<base>/logs/latest.log" for the active
 * log and "<base>/logs/archived/" for rotated logs. The game passes the executable's directory, so logs
 * do not depend on where the game was launched from; NULL keeps the paths relative to the process working
 * directory. Only the newest archives are kept (see Logger_Construct).
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
 * @brief Moves an existing "<base>/logs/latest.log" into the archive directory, if one exists.
 *
 * If the latest log exists, this reads its last-modification date, ensures "<base>/logs/archived/"
 * exists, and moves the log (a single filesystem rename) to "<base>/logs/archived/<date>.log", where
 * <date> is formatted like the log timestamps, e.g. "2026y;07m;05d". If a file with that name already
 * exists, a space and the lowest free number from 2 upward are appended ("...<date> 2.log",
 * "...<date> 3.log", ...) until an unused name is found. If there is no latest log, this is a successful
 * no-op (nothing to move). It does not prune old archives; Logger_Construct does that.
 *
 * This is normally called for you by Logger_Construct; it is exposed so a log can be rotated on demand.
 * @param baseDirectory Null-terminated UTF-8 directory that contains "logs/" (the same one given to
 *        Logger_Construct), or NULL for the process working directory.
 * @returns A success Error on success (including the "nothing to archive" case). Propagates
 *          filesystem errors from querying or moving the log (e.g. ErrorCode_IO,
 *          ErrorCode_DirectoryNotFound), path errors from joining @p baseDirectory (e.g.
 *          ErrorCode_InvalidPath), and ErrorCode_BufferTooSmall if a path could not be built.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error Logger_ArchiveLatestLog(const unsigned char* baseDirectory);

/**
 * @brief Initializes a logger, rotating any previous log and opening a fresh "<base>/logs/latest.log".
 *
 * Ensures "<base>/logs" and "<base>/logs/archived" exist, archives any existing latest log (see
 * Logger_ArchiveLatestLog), opens a fresh latest log for writing, binds stdout, and allocates the
 * reusable scratch buffers. Finally it deletes the oldest archived logs (by modification time) until at
 * most 30 remain; a failure there is logged as a warning and does not fail construction. The paths are
 * resolved once here; @p baseDirectory is not kept. On failure @p self is left uninitialized (safe to pass
 * to Logger_Deconstruct, which will do nothing).
 * @param self [out] The logger to initialize. Must not be NULL. Fully overwritten.
 * @param baseDirectory Null-terminated UTF-8 directory under which "logs/" is created (the game passes the
 *        executable's directory), or NULL for the process working directory.
 * @returns A success Error on success. ErrorCode_IllegalArgument if @p self is NULL; propagates path errors
 *          from joining @p baseDirectory (e.g. ErrorCode_InvalidPath) and filesystem/stream errors from
 *          directory creation, archiving, or opening the log/stdout streams.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error Logger_Construct(Logger* self, const unsigned char* baseDirectory);

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


// Compile-time format checking.
#if defined(__GNUC__)
/**
 * @brief Compile-time printf format checker used by LOGGER_CHECK_FORMAT; never called at run time.
 *
 * It only ever appears inside sizeof, which does not evaluate its operand, so no code is generated.
 * The format attribute still makes GCC type-check the arguments against the format. The attribute
 * only accepts a plain `const char*` format parameter, which is why LOGGER_CHECK_FORMAT casts the
 * project's `unsigned char` UTF-8 formats.
 * @param format The printf-style format.
 * @param ... The arguments to check against @p format.
 * @returns Always 0.
 */
__attribute__((format(printf, 1, 2))) static inline int Logger_CheckFormat(const char* format, ...)
{
    (void)format;
    return 0;
}

/**
 * @brief Type-checks a Logger format string and its arguments at compile time; generates no code.
 *
 * A mismatch (e.g. "%d" given a string) is a -Wformat warning, which -Werror turns into a build error.
 * Only active with GCC-compatible compilers; elsewhere it expands to nothing.
 */
#define LOGGER_CHECK_FORMAT(format, ...) \
    ((void)sizeof(Logger_CheckFormat((const char*)(format) __VA_OPT__(,) __VA_ARGS__)))
#else
#define LOGGER_CHECK_FORMAT(format, ...) ((void)0)
#endif

// The *Formatted functions are wrapped in same-named macros so every call site is format-checked
// without changing how it is written. A macro is not re-expanded inside its own expansion, so the inner
// name still calls the function. Logger.c defines LOGGER_IMPLEMENTATION to opt out while it defines the
// functions.
#ifndef LOGGER_IMPLEMENTATION
/** @brief Format-checked call of Logger_LogFormatted (see LOGGER_CHECK_FORMAT). */
#define Logger_LogFormatted(self, level, format, ...) \
    (LOGGER_CHECK_FORMAT(format __VA_OPT__(,) __VA_ARGS__), \
    Logger_LogFormatted((self), (level), (format) __VA_OPT__(,) __VA_ARGS__))
/** @brief Format-checked call of Logger_LogInfoFormatted (see LOGGER_CHECK_FORMAT). */
#define Logger_LogInfoFormatted(self, format, ...) \
    (LOGGER_CHECK_FORMAT(format __VA_OPT__(,) __VA_ARGS__), \
    Logger_LogInfoFormatted((self), (format) __VA_OPT__(,) __VA_ARGS__))
/** @brief Format-checked call of Logger_LogWarningFormatted (see LOGGER_CHECK_FORMAT). */
#define Logger_LogWarningFormatted(self, format, ...) \
    (LOGGER_CHECK_FORMAT(format __VA_OPT__(,) __VA_ARGS__), \
    Logger_LogWarningFormatted((self), (format) __VA_OPT__(,) __VA_ARGS__))
/** @brief Format-checked call of Logger_LogErrorFormatted (see LOGGER_CHECK_FORMAT). */
#define Logger_LogErrorFormatted(self, format, ...) \
    (LOGGER_CHECK_FORMAT(format __VA_OPT__(,) __VA_ARGS__), \
    Logger_LogErrorFormatted((self), (format) __VA_OPT__(,) __VA_ARGS__))
/** @brief Format-checked call of Logger_LogCriticalFormatted (see LOGGER_CHECK_FORMAT). */
#define Logger_LogCriticalFormatted(self, format, ...) \
    (LOGGER_CHECK_FORMAT(format __VA_OPT__(,) __VA_ARGS__), \
    Logger_LogCriticalFormatted((self), (format) __VA_OPT__(,) __VA_ARGS__))
#endif
