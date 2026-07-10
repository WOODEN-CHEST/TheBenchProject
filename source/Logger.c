#include "Logger.h"
#include "wr/WRDateTime.h"
#include "wr/WRFileSystem.h"
#include "wr/WRIO.h"
#include "wr/WRMemory.h"
#include "wr/WRCompile.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>


// Macros.
/** Directory (relative to the working directory) that holds the log files. */
#define LOGGER_LOGS_DIR ((const unsigned char*)u8"logs")
/** Path of the active log file. */
#define LOGGER_LATEST_LOG_PATH ((const unsigned char*)u8"logs/latest.log")
/** Directory that holds rotated (archived) logs. */
#define LOGGER_ARCHIVE_DIR ((const unsigned char*)u8"logs/archived")
/** Prefix used when building an archived log path (archive directory plus separator). */
#define LOGGER_ARCHIVE_PREFIX ((const unsigned char*)u8"logs/archived/")
/** Extension appended to archived log file names. */
#define LOGGER_LOG_EXTENSION ((const unsigned char*)u8".log")

/** Initial capacity (bytes) of the reusable line-assembly buffer. */
#define LOGGER_LINE_BUFFER_INITIAL_CAPACITY ((size_t)256)
/** Initial capacity (bytes) of the reusable format buffer. */
#define LOGGER_FORMAT_BUFFER_INITIAL_CAPACITY ((size_t)256)
/** Initial capacity (bytes) of the temporary archive-path buffer. */
#define LOGGER_PATH_BUFFER_INITIAL_CAPACITY ((size_t)64)
/** Initial capacity (bytes) of the temporary buffer holding a log's contents during archival. */
#define LOGGER_CONTENTS_BUFFER_INITIAL_CAPACITY ((size_t)256)

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
        default: return (const unsigned char*)u8"Info";
    }
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
        default: return LOGGER_ANSI_WHITE;
    }
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

/* Ensures both "logs" and "logs/archived" exist (parent first). Idempotent. */
static Error Logger_EnsureLogDirectories(void)
{
    Error LogsResult = Logger_EnsureDirectory(LOGGER_LOGS_DIR);
    if (LogsResult.Code != ErrorCode_Success)
    {
        return LogsResult;
    }
    return Logger_EnsureDirectory(LOGGER_ARCHIVE_DIR);
}

/* Builds the first free archive path for a date into outPath (NUL-terminated): tries
   "logs/archived/<date>.log", then "...<date> 1.log", "...<date> 2.log", ... until one is unused. */
static Error Logger_BuildArchiveTargetPath(GenericBuffer* outPath, const DateTime* logDate)
{
    for (int32_t Index = 0; Index < INT32_MAX; Index++)
    {
        GenericBuffer_Clear(outPath);

        bool IsOk = true;
        IsOk = IsOk && GenericBuffer_AppendString(outPath, LOGGER_ARCHIVE_PREFIX);
        IsOk = IsOk && Logger_AppendDate(outPath, logDate);
        if (Index > 0)
        {
            IsOk = IsOk && GenericBuffer_AppendByte(outPath, (unsigned char)u8' ');
            IsOk = IsOk && Logger_AppendInt32(outPath, Index + 1);
        }
        IsOk = IsOk && GenericBuffer_AppendString(outPath, LOGGER_LOG_EXTENSION);
        IsOk = IsOk && GenericBuffer_NullTerminate(outPath);
        if (!IsOk)
        {
            return Error_Construct3(ErrorCode_BufferTooSmall,
                (const unsigned char*)u8"Logger: failed to build archive path.");
        }

        if (!Logger_EntryExists(outPath->_data))
        {
            return Error_CreateSuccess();
        }
    }

    return Error_Construct3(ErrorCode_IO, (const unsigned char*)u8"Logger: no free archive name available.");
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


// Public functions.
Error Logger_ArchiveLatestLog(void)
{
    // Nothing to move if there is no active log yet.
    FileSystemEntryInfo Info;
    Error InfoResult = FileSystem_GetEntryInfo(LOGGER_LATEST_LOG_PATH, &Info);
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

    Error DirResult = Logger_EnsureLogDirectories();
    if (DirResult.Code != ErrorCode_Success)
    {
        return DirResult;
    }

    GenericBuffer TargetPath;
    GenericBuffer_AllocateVariable(&TargetPath, LOGGER_PATH_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));
    Error PathResult = Logger_BuildArchiveTargetPath(&TargetPath, &LogDate);
    if (PathResult.Code != ErrorCode_Success)
    {
        Memory_Free(TargetPath._data);
        return PathResult;
    }

    GenericBuffer Contents;
    GenericBuffer_AllocateVariable(&Contents, LOGGER_CONTENTS_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));
    Error ReadResult = FileSystem_ReadAllBytes(LOGGER_LATEST_LOG_PATH, &Contents);
    if (ReadResult.Code != ErrorCode_Success)
    {
        Memory_Free(Contents._data);
        Memory_Free(TargetPath._data);
        return ReadResult;
    }

    Error WriteResult = FileSystem_WriteAllBytes(TargetPath._data, Contents._data, Contents._count);
    Memory_Free(Contents._data);
    Memory_Free(TargetPath._data);
    if (WriteResult.Code != ErrorCode_Success)
    {
        return WriteResult;
    }

    // The copy succeeded, so remove the original to complete the move.
    return FileSystem_DeleteEntry(LOGGER_LATEST_LOG_PATH);
}

Error Logger_Construct(Logger* self)
{
    if (self == NULL)
    {
        return Error_Construct5(ErrorCode_IllegalArgument);
    }

    Memory_Zero(self, sizeof(*self));

    Error DirResult = Logger_EnsureLogDirectories();
    if (DirResult.Code != ErrorCode_Success)
    {
        return DirResult;
    }

    // Move any previous session's log aside before opening a fresh one (this truncates latest.log).
    Error ArchiveResult = Logger_ArchiveLatestLog();
    if (ArchiveResult.Code != ErrorCode_Success)
    {
        return ArchiveResult;
    }

    Error OpenResult = FileSystem_OpenFileStream(LOGGER_LATEST_LOG_PATH, FileOpenMode_WriteText, &self->_logFileStream);
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

    GenericBuffer_AllocateVariable(&self->_lineBuffer, LOGGER_LINE_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->_formatBuffer, LOGGER_FORMAT_BUFFER_INITIAL_CAPACITY, sizeof(unsigned char));

    self->_isInitialized = true;
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
