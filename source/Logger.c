// Logger.h wraps the *Formatted functions in same-named format-checking macros; this file defines the
// functions themselves, so it opts out of those macros.
#define LOGGER_IMPLEMENTATION
#include "Logger.h"
#include "wr/WRDateTime.h"
#include "wr/WRFileSystem.h"
#include "wr/WRIO.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"
#include "wr/WRCompile.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>


// Macros.
/** Directory that holds the log files, relative to the logger's base directory. */
#define LOGGER_LOGS_RELATIVE_PATH ((const unsigned char*)u8"logs")
/** Path of the active log file, relative to the logger's base directory. */
#define LOGGER_LATEST_LOG_RELATIVE_PATH ((const unsigned char*)u8"logs/latest.log")
/** Directory that holds rotated (archived) logs, relative to the logger's base directory. */
#define LOGGER_ARCHIVE_RELATIVE_PATH ((const unsigned char*)u8"logs/archived")
/** Extension appended to archived log file names. */
#define LOGGER_LOG_EXTENSION ((const unsigned char*)u8".log")

/** Initial capacity (bytes) of the reusable line-assembly buffer. */
#define LOGGER_LINE_BUFFER_INITIAL_CAPACITY ((size_t)256)
/** Initial capacity (bytes) of the reusable format buffer. */
#define LOGGER_FORMAT_BUFFER_INITIAL_CAPACITY ((size_t)256)
/** Initial capacity (bytes) of the temporary archive-path buffer. */
#define LOGGER_PATH_BUFFER_INITIAL_CAPACITY ((size_t)64)
/** Most archived logs kept in "logs/archived"; Logger_Construct deletes the oldest (by modification time)
 *  until no more than this many remain. */
#define LOGGER_MAX_ARCHIVED_LOGS ((size_t)30)

// ANSI SGR color escape sequences (Linux stdout only; see Logger_WriteToStdout).
#ifdef __linux__
/** Resets stdout back to the terminal's default colors. */
#define LOGGER_ANSI_RESET ((const unsigned char*)u8"\x1b[0m")
/** White, used for Info. */
#define LOGGER_ANSI_WHITE ((const unsigned char*)u8"\x1b[37m")
/** Yellow, used for Warning. */
#define LOGGER_ANSI_YELLOW ((const unsigned char*)u8"\x1b[33m")
/** Bright red, used for Error. */
#define LOGGER_ANSI_BRIGHT_RED ((const unsigned char*)u8"\x1b[91m")
/** Red (the closest cross-platform "dark red"), used for Critical. */
#define LOGGER_ANSI_RED ((const unsigned char*)u8"\x1b[31m")
#endif


// Types.
/* The log paths, resolved once against the logger's base directory (each a NUL-terminated owned buffer). */
typedef struct LoggerPathsStruct
{
    GenericBuffer LogsDirectory;    // "<base>/logs"
    GenericBuffer LatestLog;        // "<base>/logs/latest.log"
    GenericBuffer ArchiveDirectory; // "<base>/logs/archived"
} LoggerPaths;


// Static functions.
/* Appends the base-10 text of a (possibly negative) 32-bit integer to a byte buffer. */
static bool Logger_AppendInt32(GenericBuffer* buffer, int32_t value)
{
    unsigned char Temp[12];
    size_t Index = sizeof(Temp);
    bool Negative = (value < 0);
    uint32_t Magnitude = Negative ? (uint32_t)(-(int64_t)value) : (uint32_t)value;

    do
    {
        Temp[--Index] = (unsigned char)(u8'0' + (Magnitude % 10u));
        Magnitude /= 10u;
    } while (Magnitude > 0u);

    if (Negative)
    {
        Temp[--Index] = (unsigned char)u8'-';
    }
    return GenericBuffer_AppendRangeBytes(buffer, Temp + Index, sizeof(Temp) - Index);
}

/* Appends a calendar field as exactly two digits (zero-padded), e.g. 9 -> "09". */
static bool Logger_AppendTwoDigit(GenericBuffer* buffer, int32_t value)
{
    // Calendar fields are always in [0, 60] here; clamp defensively so the output stays two digits.
    if (value < 0) { value = 0; }
    value = value % 100;

    unsigned char Digits[2];
    Digits[0] = (unsigned char)(u8'0' + (value / 10));
    Digits[1] = (unsigned char)(u8'0' + (value % 10));
    return GenericBuffer_AppendRangeBytes(buffer, Digits, sizeof(Digits));
}

/* Appends the date as "<year>y,<month>m,<day>d" (month/day zero-padded to two digits). */
static bool Logger_AppendDate(GenericBuffer* buffer, const DateTime* dateTime)
{
    bool Ok = true;
    Ok = Ok && Logger_AppendInt32(buffer, dateTime->Year);
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8'y');
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8';');
    Ok = Ok && Logger_AppendTwoDigit(buffer, dateTime->Month);
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8'm');
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8';');
    Ok = Ok && Logger_AppendTwoDigit(buffer, dateTime->Day);
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8'd');
    return Ok;
}

/* Appends the time as "<hour>h:<minute>m:<second>s" (all fields zero-padded to two digits). */
static bool Logger_AppendTime(GenericBuffer* buffer, const DateTime* dateTime)
{
    bool Ok = true;
    Ok = Ok && Logger_AppendTwoDigit(buffer, dateTime->Hour);
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8'h');
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8':');
    Ok = Ok && Logger_AppendTwoDigit(buffer, dateTime->Minute);
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8'm');
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8':');
    Ok = Ok && Logger_AppendTwoDigit(buffer, dateTime->Second);
    Ok = Ok && GenericBuffer_AppendByte(buffer, (unsigned char)u8's');
    return Ok;
}

/* Returns the fixed prefix string for a level. */
static const unsigned char* Logger_GetLevelString(LogLevel level)
{
    switch (level)
    {
        case LogLevel_Info: return (const unsigned char*)u8"Info";
        case LogLevel_Warning: return (const unsigned char*)u8"Warning";
        case LogLevel_Error: return (const unsigned char*)u8"Error";
        case LogLevel_Critical: return (const unsigned char*)u8"CRITICAL";
    }
    // Only reachable with a corrupted level value; tag it as Info rather than fail the log call.
    return (const unsigned char*)u8"Info";
}

#ifdef __linux__
/* Returns the ANSI color escape sequence for a level (Linux stdout only). */
static const unsigned char* Logger_GetLevelColor(LogLevel level)
{
    switch (level)
    {
        case LogLevel_Info: return LOGGER_ANSI_WHITE;
        case LogLevel_Warning: return LOGGER_ANSI_YELLOW;
        case LogLevel_Error: return LOGGER_ANSI_BRIGHT_RED;
        case LogLevel_Critical: return LOGGER_ANSI_RED;
    }
    // Only reachable with a corrupted level value.
    return LOGGER_ANSI_WHITE;
}
#endif

/* Reports whether a file system entry exists at the given path. */
static bool Logger_EntryExists(const unsigned char* path)
{
    FileSystemEntryInfo Info;
    Error Result = FileSystem_GetEntryInfo(path, &Info);
    bool Exists = (Result.Code == ErrorCode_Success);
    if (Exists)
    {
        FileSystemEntryInfo_Deconstruct(&Info);
    }
    Error_Deconstruct(&Result);
    return Exists;
}

static Error Logger_EnsureDirectory(const unsigned char* path)
{
    FileSystemEntryInfo Info;
    Error InfoResult = FileSystem_GetEntryInfo(path, &Info);
    if (InfoResult.Code == ErrorCode_Success)
    {
        FileSystemEntryType Type = Info._entryType;
        FileSystemEntryInfo_Deconstruct(&Info);
        if (Type == FileSystemEntryType_Directory)
        {
            return Error_CreateSuccess();
        }
        return Error_Construct3(ErrorCode_InvalidOperation,
            (const unsigned char*)u8"Logger: a log path exists but is not a directory.");
    }
    if ((InfoResult.Code == ErrorCode_FileNotFound) || (InfoResult.Code == ErrorCode_DirectoryNotFound))
    {
        Error_Deconstruct(&InfoResult);
        return FileSystem_CreateAllDirectories(path);
    }
    return InfoResult;
}

/* Resolves @p relativePath against @p baseDirectory into a freshly allocated NUL-terminated buffer, which the
   caller frees (outPath->_data) whatever the result. A NULL base leaves the path relative to the working
   directory. */
static Error Logger_ResolvePath(const unsigned char* baseDirectory, const unsigned char* relativePath,
    GenericBuffer* outPath)
{
    GenericBuffer_AllocateVariable(outPath, LOGGER_PATH_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));
    if (baseDirectory != NULL)
    {
        return Path_Append(baseDirectory, relativePath, outPath);
    }
    if (!GenericBuffer_AppendString(outPath, relativePath) || !GenericBuffer_NullTerminate(outPath))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall, (const unsigned char*)u8"Logger: failed to build a log path.");
    }
    return Error_CreateSuccess();
}

static void LoggerPaths_Deconstruct(LoggerPaths* self)
{
    Memory_Free(self->LogsDirectory._data);
    Memory_Free(self->LatestLog._data);
    Memory_Free(self->ArchiveDirectory._data);
    Memory_Zero(self, sizeof(*self));
}

/* Resolves every log path against @p baseDirectory (NULL = the working directory). On failure nothing is
   left for the caller to release. */
static Error LoggerPaths_Construct(LoggerPaths* self, const unsigned char* baseDirectory)
{
    Memory_Zero(self, sizeof(*self));
    Error Result = Logger_ResolvePath(baseDirectory, LOGGER_LOGS_RELATIVE_PATH, &self->LogsDirectory);
    if (Result.Code == ErrorCode_Success)
    {
        Result = Logger_ResolvePath(baseDirectory, LOGGER_LATEST_LOG_RELATIVE_PATH, &self->LatestLog);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Logger_ResolvePath(baseDirectory, LOGGER_ARCHIVE_RELATIVE_PATH, &self->ArchiveDirectory);
    }
    if (Result.Code != ErrorCode_Success)
    {
        LoggerPaths_Deconstruct(self);
    }
    return Result;
}

/* Ensures both the logs directory and the archive directory exist (parent first). Idempotent. */
static Error Logger_EnsureLogDirectories(const LoggerPaths* paths)
{
    Error LogsResult = Logger_EnsureDirectory(paths->LogsDirectory._data);
    if (LogsResult.Code != ErrorCode_Success)
    {
        return LogsResult;
    }
    return Logger_EnsureDirectory(paths->ArchiveDirectory._data);
}

/* Builds the first free archive path for a date, REPLACING outPath's contents (NUL-terminated): tries
   "<archive>/<date>.log", then "<archive>/<date> 2.log", "<archive>/<date> 3.log", ... until one is unused. */
static Error Logger_BuildArchiveTargetPath(const unsigned char* archiveDirectory, GenericBuffer* outPath,
    const DateTime* logDate)
{
    GenericBuffer FileName;
    GenericBuffer_AllocateVariable(&FileName, LOGGER_PATH_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));

    for (int32_t Index = 0; Index < INT32_MAX; Index++)
    {
        GenericBuffer_Clear(&FileName);
        GenericBuffer_Clear(outPath);

        bool IsOk = Logger_AppendDate(&FileName, logDate);
        if (Index > 0)
        {
            IsOk = IsOk && GenericBuffer_AppendByte(&FileName, (unsigned char)u8' ');
            IsOk = IsOk && Logger_AppendInt32(&FileName, Index + 1);
        }
        IsOk = IsOk && GenericBuffer_AppendString(&FileName, LOGGER_LOG_EXTENSION);
        IsOk = IsOk && GenericBuffer_NullTerminate(&FileName);
        if (!IsOk)
        {
            Memory_Free(FileName._data);
            return Error_Construct3(ErrorCode_BufferTooSmall,
                (const unsigned char*)u8"Logger: failed to build archive path.");
        }

        Error JoinResult = Path_Append(archiveDirectory, FileName._data, outPath);
        if (JoinResult.Code != ErrorCode_Success)
        {
            Memory_Free(FileName._data);
            return JoinResult;
        }
        if (!Logger_EntryExists(outPath->_data))
        {
            Memory_Free(FileName._data);
            return Error_CreateSuccess();
        }
    }

    Memory_Free(FileName._data);
    return Error_Construct3(ErrorCode_IO, (const unsigned char*)u8"Logger: no free archive name available.");
}

/* Reports whether an archive-directory file is an archived log (its name ends in ".log"). */
static bool Logger_IsArchivedLog(const FileSystemEntryInfo* info)
{
    bool HasLogExtension = false;
    Error Result = StringUTF8_EndsWith(info->_name, LOGGER_LOG_EXTENSION, StringCaseRule_MatchCase, NULL,
        &HasLogExtension);
    Error_Deconstruct(&Result);
    return HasLogExtension;
}

/* Counts the archived logs and finds the oldest one (by modification time) in one pass over the archive
   directory. When *outCount ends up non-zero, outOldest holds the oldest entry and the caller must
   FileSystemEntryInfo_Deconstruct it; on failure nothing is left for the caller to release. */
static Error Logger_FindOldestArchivedLog(const unsigned char* archiveDirectory, size_t* outCount,
    FileSystemEntryInfo* outOldest)
{
    *outCount = 0;

    DirectoryEntryEnumerator* Enumerator = NULL;
    Error Result = FileSystem_GetFiles(archiveDirectory, DirectorySearchOption_TopLevel, &Enumerator);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    while (true)
    {
        bool HasNext = false;
        Result = DirectoryEntryEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code != ErrorCode_Success) || !HasNext)
        {
            break;
        }

        FileSystemEntryInfo Entry;
        Result = DirectoryEntryEnumerator_Next(Enumerator, &Entry);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (!Logger_IsArchivedLog(&Entry))
        {
            FileSystemEntryInfo_Deconstruct(&Entry);
            continue;
        }

        if ((*outCount == 0) || (Entry._lastModificationTime < outOldest->_lastModificationTime))
        {
            if (*outCount > 0)
            {
                FileSystemEntryInfo_Deconstruct(outOldest);
            }
            *outOldest = Entry;
        }
        else
        {
            FileSystemEntryInfo_Deconstruct(&Entry);
        }
        (*outCount)++;
    }

    Error CloseResult = DirectoryEntryEnumerator_Deconstruct(Enumerator);
    Error_Deconstruct(&CloseResult);
    if ((Result.Code != ErrorCode_Success) && (*outCount > 0))
    {
        FileSystemEntryInfo_Deconstruct(outOldest);
        *outCount = 0;
    }
    return Result;
}

/* Deletes the oldest archived logs until at most LOGGER_MAX_ARCHIVED_LOGS remain. Each pass rescans the
   directory and deletes one file; the number of passes is bounded by the excess found on the first scan
   (normally one, since each session adds one archive). */
static Error Logger_PruneArchivedLogs(const unsigned char* archiveDirectory)
{
    size_t Excess = SIZE_MAX; // Unknown until the first scan.
    while (Excess > 0)
    {
        size_t Count = 0;
        FileSystemEntryInfo Oldest;
        Memory_Zero(&Oldest, sizeof(Oldest));
        Error FindResult = Logger_FindOldestArchivedLog(archiveDirectory, &Count, &Oldest);
        if (FindResult.Code != ErrorCode_Success)
        {
            return FindResult;
        }
        if (Count <= LOGGER_MAX_ARCHIVED_LOGS)
        {
            if (Count > 0)
            {
                FileSystemEntryInfo_Deconstruct(&Oldest);
            }
            return Error_CreateSuccess();
        }
        if (Excess == SIZE_MAX)
        {
            Excess = Count - LOGGER_MAX_ARCHIVED_LOGS;
        }

        Error DeleteResult = FileSystem_DeleteEntry(Oldest._path);
        FileSystemEntryInfo_Deconstruct(&Oldest);
        if (DeleteResult.Code != ErrorCode_Success)
        {
            return DeleteResult;
        }
        Excess--;
    }
    return Error_CreateSuccess();
}

/* Writes a completed line (without trailing newline) plus a newline to the log file, then flushes. */
static Error Logger_WriteToFile(Logger* self, const unsigned char* lineData, size_t lineCount)
{
    IOStream* Stream = FileStream_AsIOStream(&self->_logFileStream);

    Error Result = IOStream_Write(Stream, lineData, lineCount);
    if (Result.Code != ErrorCode_Success) { return Result; }

    Result = IOStream_WriteString(Stream, (const unsigned char*)u8"\n");
    if (Result.Code != ErrorCode_Success) { return Result; }

    return IOStream_Flush(Stream);
}

/* Mirrors a completed line to stdout, wrapping it in the level's ANSI color on Linux, then flushes. */
static Error Logger_WriteToStdout(Logger* self, LogLevel level, const unsigned char* lineData, size_t lineCount)
{
    IOStream* Stream = StandardStream_AsIOStream(&self->_stdoutStream);

#ifdef __linux__
    Error ColorResult = IOStream_WriteString(Stream, Logger_GetLevelColor(level));
    if (ColorResult.Code != ErrorCode_Success) { return ColorResult; }
#else
    UNUSED(level);
#endif

    Error Result = IOStream_Write(Stream, lineData, lineCount);
    if (Result.Code != ErrorCode_Success) { return Result; }

#ifdef __linux__
    Result = IOStream_WriteString(Stream, LOGGER_ANSI_RESET);
    if (Result.Code != ErrorCode_Success) { return Result; }
#endif

    Result = IOStream_WriteString(Stream, (const unsigned char*)u8"\n");
    if (Result.Code != ErrorCode_Success) { return Result; }

    return IOStream_Flush(Stream);
}

/* Renders a printf-style message into the logger's format buffer (cleared by the caller), leaving it
   NUL-terminated so it can be passed on as a C string. */
static Error Logger_BuildFormatted(Logger* self, const unsigned char* format, va_list args)
{
    va_list Probe;
    va_copy(Probe, args);
    int Needed = vsnprintf(NULL, 0, (const char*)format, Probe);
    va_end(Probe);
    if (Needed < 0)
    {
        return Error_Construct3(ErrorCode_InvalidOperation,
            (const unsigned char*)u8"Logger: failed to format message.");
    }

    size_t Required = (size_t)Needed + 1u; // +1 for the NUL vsnprintf writes.
    void* Tail = NULL;
    if (!GenericBuffer_GetWritableTail(&self->_formatBuffer, Required, &Tail))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            (const unsigned char*)u8"Logger: failed to reserve format buffer.");
    }

    int Written = vsnprintf((char*)Tail, Required, (const char*)format, args);
    if (Written < 0)
    {
        return Error_Construct3(ErrorCode_InvalidOperation,
            (const unsigned char*)u8"Logger: failed to format message.");
    }

    if (!GenericBuffer_CommitCount(&self->_formatBuffer, (size_t)Written)
        || !GenericBuffer_NullTerminate(&self->_formatBuffer))
    {
        return Error_Construct3(ErrorCode_InvalidState,
            (const unsigned char*)u8"Logger: failed to finalize formatted message.");
    }
    return Error_CreateSuccess();
}

/* Shared implementation for the *Formatted log methods: renders then logs. */
static Error Logger_LogFormattedImpl(Logger* self, LogLevel level, const unsigned char* format, va_list args)
{
    if ((self == NULL) || (format == NULL))
    {
        return Error_Construct5(ErrorCode_IllegalArgument);
    }
    if (!self->_isInitialized)
    {
        return Error_Construct5(ErrorCode_InvalidState);
    }

    GenericBuffer_Clear(&self->_formatBuffer);
    Error BuildResult = Logger_BuildFormatted(self, format, args);
    if (BuildResult.Code != ErrorCode_Success)
    {
        return BuildResult;
    }
    return Logger_Log(self, level, self->_formatBuffer._data);
}

/* Moves an existing latest log into the archive directory, named after its own last-modification date. A
   missing latest log is a successful no-op. */
static Error Logger_MoveLatestLogToArchive(const LoggerPaths* paths)
{
    // Nothing to move if there is no active log yet.
    FileSystemEntryInfo Info;
    Error InfoResult = FileSystem_GetEntryInfo(paths->LatestLog._data, &Info);
    if ((InfoResult.Code == ErrorCode_FileNotFound) || (InfoResult.Code == ErrorCode_DirectoryNotFound))
    {
        Error_Deconstruct(&InfoResult);
        return Error_CreateSuccess();
    }
    if (InfoResult.Code != ErrorCode_Success)
    {
        return InfoResult;
    }

    // The archive is named after the old log's own date, which the filesystem reports as a Unix time.
    // Render it in local time to match the local timestamps written into the log lines.
    DateTime LogDate;
    Error DateResult = DateTime_FromUnixSeconds((int64_t)Info._lastModificationTime, DateTimeKind_Local, &LogDate);
    FileSystemEntryInfo_Deconstruct(&Info);
    if (DateResult.Code != ErrorCode_Success)
    {
        return DateResult;
    }

    Error DirResult = Logger_EnsureLogDirectories(paths);
    if (DirResult.Code != ErrorCode_Success)
    {
        return DirResult;
    }

    GenericBuffer TargetPath;
    GenericBuffer_AllocateVariable(&TargetPath, LOGGER_PATH_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));
    Error PathResult = Logger_BuildArchiveTargetPath(paths->ArchiveDirectory._data, &TargetPath, &LogDate);
    if (PathResult.Code != ErrorCode_Success)
    {
        Memory_Free(TargetPath._data);
        return PathResult;
    }

    // One filesystem move. The target name was chosen to be unused, so the platform-defined "replace an
    // existing entry" behaviour of FileSystem_MoveEntry never comes into play.
    Error MoveResult = FileSystem_MoveEntry(paths->LatestLog._data, TargetPath._data);
    Memory_Free(TargetPath._data);
    return MoveResult;
}

/* Prepares the log directories, archives the previous session's log, opens a fresh latest log and binds
   stdout. On failure nothing is left open. */
static Error Logger_OpenStreams(Logger* self, const LoggerPaths* paths)
{
    Error DirResult = Logger_EnsureLogDirectories(paths);
    if (DirResult.Code != ErrorCode_Success)
    {
        return DirResult;
    }

    // Move any previous session's log aside before opening a fresh one (this truncates latest.log).
    Error ArchiveResult = Logger_MoveLatestLogToArchive(paths);
    if (ArchiveResult.Code != ErrorCode_Success)
    {
        return ArchiveResult;
    }

    Error OpenResult = FileSystem_OpenFileStream(paths->LatestLog._data, FileOpenMode_WriteText, &self->_logFileStream);
    if (OpenResult.Code != ErrorCode_Success)
    {
        return OpenResult;
    }

    Error StdoutResult = StandardStream_CreateFromStandardOutput(&self->_stdoutStream);
    if (StdoutResult.Code != ErrorCode_Success)
    {
        Error CloseResult = FileStream_Deconstruct(&self->_logFileStream);
        Error_Deconstruct(&CloseResult);
        return StdoutResult;
    }
    return Error_CreateSuccess();
}


// Public functions.
Error Logger_ArchiveLatestLog(const unsigned char* baseDirectory)
{
    LoggerPaths Paths;
    Error PathsResult = LoggerPaths_Construct(&Paths, baseDirectory);
    if (PathsResult.Code != ErrorCode_Success)
    {
        return PathsResult;
    }

    Error Result = Logger_MoveLatestLogToArchive(&Paths);
    LoggerPaths_Deconstruct(&Paths);
    return Result;
}

Error Logger_Construct(Logger* self, const unsigned char* baseDirectory)
{
    if (self == NULL)
    {
        return Error_Construct5(ErrorCode_IllegalArgument);
    }

    Memory_Zero(self, sizeof(*self));

    LoggerPaths Paths;
    Error PathsResult = LoggerPaths_Construct(&Paths, baseDirectory);
    if (PathsResult.Code != ErrorCode_Success)
    {
        return PathsResult;
    }

    Error OpenResult = Logger_OpenStreams(self, &Paths);
    if (OpenResult.Code != ErrorCode_Success)
    {
        LoggerPaths_Deconstruct(&Paths);
        return OpenResult;
    }

    GenericBuffer_AllocateVariable(&self->_lineBuffer, LOGGER_LINE_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->_formatBuffer, LOGGER_FORMAT_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));

    self->_isInitialized = true;

    // Prune only now that the fresh log can report a failure: losing old archives is not worth failing
    // startup over, so a prune error is logged and otherwise ignored.
    Error PruneResult = Logger_PruneArchivedLogs(Paths.ArchiveDirectory._data);
    LoggerPaths_Deconstruct(&Paths);
    if (PruneResult.Code != ErrorCode_Success)
    {
        Error LogResult = Logger_LogWarningFormatted(self, (const unsigned char*)u8"Could not prune archived logs: %s",
            (PruneResult.Message != NULL) ? (const char*)PruneResult.Message : "no details");
        Error_Deconstruct(&LogResult);
        Error_Deconstruct(&PruneResult);
    }
    return Error_CreateSuccess();
}

Error Logger_Log(Logger* self, LogLevel level, const unsigned char* message)
{
    if ((self == NULL) || (message == NULL))
    {
        return Error_Construct5(ErrorCode_IllegalArgument);
    }
    if (!self->_isInitialized)
    {
        return Error_Construct5(ErrorCode_InvalidState);
    }

    DateTime Now = DateTime_Now();
    GenericBuffer* Line = &self->_lineBuffer;
    GenericBuffer_Clear(Line);

    // Assemble "[<date>][<time>][<level>] <message>" (the trailing newline is added on write).
    bool Ok = true;
    Ok = Ok && GenericBuffer_AppendByte(Line, (unsigned char)u8'[');
    Ok = Ok && Logger_AppendDate(Line, &Now);
    Ok = Ok && GenericBuffer_AppendByte(Line, (unsigned char)u8']');
    Ok = Ok && GenericBuffer_AppendByte(Line, (unsigned char)u8'[');
    Ok = Ok && Logger_AppendTime(Line, &Now);
    Ok = Ok && GenericBuffer_AppendByte(Line, (unsigned char)u8']');
    Ok = Ok && GenericBuffer_AppendByte(Line, (unsigned char)u8'[');
    Ok = Ok && GenericBuffer_AppendString(Line, Logger_GetLevelString(level));
    Ok = Ok && GenericBuffer_AppendByte(Line, (unsigned char)u8']');
    Ok = Ok && GenericBuffer_AppendByte(Line, (unsigned char)u8' ');
    Ok = Ok && GenericBuffer_AppendString(Line, message);
    if (!Ok)
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            (const unsigned char*)u8"Logger: failed to assemble log line.");
    }

    Error FileResult = Logger_WriteToFile(self, Line->_data, Line->_count);
    if (FileResult.Code != ErrorCode_Success)
    {
        return FileResult;
    }
    return Logger_WriteToStdout(self, level, Line->_data, Line->_count);
}

Error Logger_LogFormatted(Logger* self, LogLevel level, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    Error Result = Logger_LogFormattedImpl(self, level, format, Args);
    va_end(Args);
    return Result;
}

Error Logger_LogInfo(Logger* self, const unsigned char* message)
{
    return Logger_Log(self, LogLevel_Info, message);
}

Error Logger_LogInfoFormatted(Logger* self, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    Error Result = Logger_LogFormattedImpl(self, LogLevel_Info, format, Args);
    va_end(Args);
    return Result;
}

Error Logger_LogWarning(Logger* self, const unsigned char* message)
{
    return Logger_Log(self, LogLevel_Warning, message);
}

Error Logger_LogWarningFormatted(Logger* self, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    Error Result = Logger_LogFormattedImpl(self, LogLevel_Warning, format, Args);
    va_end(Args);
    return Result;
}

Error Logger_LogError(Logger* self, const unsigned char* message)
{
    return Logger_Log(self, LogLevel_Error, message);
}

Error Logger_LogErrorFormatted(Logger* self, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    Error Result = Logger_LogFormattedImpl(self, LogLevel_Error, format, Args);
    va_end(Args);
    return Result;
}

Error Logger_LogCritical(Logger* self, const unsigned char* message)
{
    return Logger_Log(self, LogLevel_Critical, message);
}

Error Logger_LogCriticalFormatted(Logger* self, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    Error Result = Logger_LogFormattedImpl(self, LogLevel_Critical, format, Args);
    va_end(Args);
    return Result;
}

Error Logger_Deconstruct(Logger* self)
{
    if ((self == NULL) || !self->_isInitialized)
    {
        return Error_CreateSuccess();
    }

    // Best-effort teardown: keep the first failure, release every resource regardless.
    Error FirstError = Error_CreateSuccess();

    Error FileError = FileStream_Deconstruct(&self->_logFileStream);
    if ((FileError.Code != ErrorCode_Success) && (FirstError.Code == ErrorCode_Success))
    {
        FirstError = FileError;
    }
    else
    {
        Error_Deconstruct(&FileError);
    }

    Error StdoutError = StandardStream_Deconstruct(&self->_stdoutStream);
    if ((StdoutError.Code != ErrorCode_Success) && (FirstError.Code == ErrorCode_Success))
    {
        FirstError = StdoutError;
    }
    else
    {
        Error_Deconstruct(&StdoutError);
    }

    Memory_Free(self->_lineBuffer._data);
    Memory_Free(self->_formatBuffer._data);
    self->_isInitialized = false;
    return FirstError;
}
