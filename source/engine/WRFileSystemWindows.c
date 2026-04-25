#include "WRFileSystem.h"
#include "WRFileSystemInternal.h"
#include "WRMemory.h"
#include "WREnvironment.h"
#include "WRPath.h"
#include <stdint.h>

#if defined(_WIN32)

#include <stdio.h>
#include <wchar.h>
#include <windows.h>


// Macros.
#define WINDOWS_TICKS_PER_SECOND (10000000ULL)
#define WINDOWS_TO_UNIX_SECONDS (11644473600ULL)


// Types.
typedef struct NativeDirectoryEnumeratorStruct
{
    HANDLE _handle;
    WIN32_FIND_DATAW _findData;
    bool _hasBufferedResult;
} NativeDirectoryEnumerator;


// Fields.
static const size_t FILE_SYSTEM_PLATFORM_TEMP_BUFFER_INITIAL_CAPACITY = 256;


// Static functions.
static size_t GetStringLength(const unsigned char* text)
{
    size_t Length = 0;

    while ((text != NULL) && (text[Length] != 0))
    {
        Length++;
    }

    return Length;
}

static bool IsDirectorySeparator(unsigned char character)
{
    return (character == ENVIRONMENT_PATH_SEPARATOR_PRIMARY)
        || (character == ENVIRONMENT_PATH_SEPARATOR_SECONDARY);
}

static bool FileSystemPlatformBufferAllocate(GenericBuffer* destination, size_t requestedCapacity)
{
    unsigned char* NewData = Memory_Reallocate(destination->_data, requestedCapacity);

    if (NewData == NULL)
    {
        return false;
    }

    destination->_data = NewData;
    destination->_capacity = requestedCapacity;
    return true;
}

static void CreateGrowableByteBuffer(GenericBuffer* buffer)
{
    unsigned char* Data = Memory_Allocate(FILE_SYSTEM_PLATFORM_TEMP_BUFFER_INITIAL_CAPACITY);

    GenericBuffer_CreateVariable(buffer,
        Data,
        FILE_SYSTEM_PLATFORM_TEMP_BUFFER_INITIAL_CAPACITY,
        sizeof(unsigned char),
        0,
        NULL,
        &FileSystemPlatformBufferAllocate);
}

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Windows file system argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateWin32Error(const unsigned char* operationName, const unsigned char* path, DWORD nativeError)
{
    ErrorCode Code = ErrorCode_IO;

    if ((nativeError == ERROR_FILE_NOT_FOUND) || (nativeError == ERROR_PATH_NOT_FOUND))
    {
        Code = ErrorCode_FileNotFound;
    }
    else if (nativeError == ERROR_DIRECTORY)
    {
        Code = ErrorCode_DirectoryNotFound;
    }
    else if ((nativeError == ERROR_INVALID_NAME) || (nativeError == ERROR_BAD_PATHNAME))
    {
        Code = ErrorCode_InvalidPath;
    }

    return Error_Construct3(Code,
        u8"Failed to %s \"%s\". Native error code %lu.",
        operationName,
        path,
        (unsigned long)nativeError);
}

static Error DuplicateStringBySize(const unsigned char* text, size_t length, unsigned char** outText)
{
    unsigned char* Copy = NULL;

    if (outText == NULL)
    {
        return CreateNullArgumentError(u8"outText");
    }

    *outText = NULL;
    if (length > (SIZE_MAX - 1))
    {
        return Error_Construct1(ErrorCode_ArgumentOutOfRange,
            u8"Windows file system string duplication overflowed.");
    }

    Copy = Memory_Allocate(length + 1);
    if (length > 0)
    {
        Memory_Copy(text, Copy, length);
    }

    Copy[length] = 0;
    *outText = Copy;
    return Error_CreateSuccess();
}

static Error DuplicateString(const unsigned char* text, unsigned char** outText)
{
    return DuplicateStringBySize(text, GetStringLength(text), outText);
}

static Error CombinePaths(const unsigned char* leftPath, const unsigned char* rightPath, unsigned char** outPath)
{
    GenericBuffer Buffer;
    Error Result = Error_CreateSuccess();

    if (outPath == NULL)
    {
        return CreateNullArgumentError(u8"outPath");
    }

    *outPath = NULL;
    CreateGrowableByteBuffer(&Buffer);
    Result = Path_Append(leftPath, rightPath, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Buffer._data);
        return Result;
    }

    Result = DuplicateString((const unsigned char*)Buffer._data, outPath);
    Memory_Free(Buffer._data);
    return Result;
}

static Error CopyLastEntryName(const unsigned char* path, unsigned char** outName)
{
    GenericBuffer Buffer;
    Error Result = Error_CreateSuccess();

    if (outName == NULL)
    {
        return CreateNullArgumentError(u8"outName");
    }

    *outName = NULL;
    CreateGrowableByteBuffer(&Buffer);
    Result = Path_GetLastEntryName(path, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Buffer._data);
        return Result;
    }

    Result = DuplicateString((const unsigned char*)Buffer._data, outName);
    Memory_Free(Buffer._data);
    return Result;
}

static time_t ConvertFileTimeToUnixTime(FILETIME fileTime)
{
    ULARGE_INTEGER Value;
    uint64_t Seconds = 0;

    Value.LowPart = fileTime.dwLowDateTime;
    Value.HighPart = fileTime.dwHighDateTime;
    if (Value.QuadPart == 0)
    {
        return 0;
    }

    Seconds = Value.QuadPart / WINDOWS_TICKS_PER_SECOND;
    if (Seconds <= WINDOWS_TO_UNIX_SECONDS)
    {
        return 0;
    }

    return (time_t)(Seconds - WINDOWS_TO_UNIX_SECONDS);
}

static FILETIME ConvertLargeIntegerToFileTime(LARGE_INTEGER value)
{
    FILETIME Result;
    ULARGE_INTEGER UnsignedValue;

    UnsignedValue.QuadPart = (ULONGLONG)value.QuadPart;
    Result.dwLowDateTime = UnsignedValue.LowPart;
    Result.dwHighDateTime = UnsignedValue.HighPart;
    return Result;
}

static Error ConvertUtf8ToWide(const unsigned char* text, wchar_t** outWideText)
{
    int RequiredLength = 0;
    wchar_t* Result = NULL;

    if (outWideText == NULL)
    {
        return CreateNullArgumentError(u8"outWideText");
    }

    *outWideText = NULL;
    RequiredLength = MultiByteToWideChar(CP_UTF8,
        MB_ERR_INVALID_CHARS,
        (const char*)text,
        -1,
        NULL,
        0);
    if (RequiredLength == 0)
    {
        return CreateWin32Error(u8"convert UTF-8 path to UTF-16", text, GetLastError());
    }

    Result = Memory_Allocate(sizeof(*Result) * (size_t)RequiredLength);
    if (MultiByteToWideChar(CP_UTF8,
        MB_ERR_INVALID_CHARS,
        (const char*)text,
        -1,
        Result,
        RequiredLength) == 0)
    {
        Memory_Free(Result);
        return CreateWin32Error(u8"convert UTF-8 path to UTF-16", text, GetLastError());
    }

    *outWideText = Result;
    return Error_CreateSuccess();
}

static Error ConvertWideToUtf8(const wchar_t* text, unsigned char** outUtf8Text)
{
    int RequiredLength = 0;
    unsigned char* Result = NULL;

    if (outUtf8Text == NULL)
    {
        return CreateNullArgumentError(u8"outUtf8Text");
    }

    *outUtf8Text = NULL;
    RequiredLength = WideCharToMultiByte(CP_UTF8,
        0,
        text,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    if (RequiredLength == 0)
    {
        return Error_Construct3(ErrorCode_InvalidPath,
            u8"Failed to convert a UTF-16 file system path to UTF-8. Native error code %lu.",
            (unsigned long)GetLastError());
    }

    Result = Memory_Allocate((size_t)RequiredLength);
    if (WideCharToMultiByte(CP_UTF8,
        0,
        text,
        -1,
        (char*)Result,
        RequiredLength,
        NULL,
        NULL) == 0)
    {
        Memory_Free(Result);
        return Error_Construct3(ErrorCode_InvalidPath,
            u8"Failed to convert a UTF-16 file system path to UTF-8. Native error code %lu.",
            (unsigned long)GetLastError());
    }

    *outUtf8Text = Result;
    return Error_CreateSuccess();
}

static const wchar_t* GetFileModeString(FileOpenMode mode)
{
    switch (mode)
    {
        case FileOpenMode_ReadText:
            return L"r";

        case FileOpenMode_WriteText:
            return L"w";

        case FileOpenMode_AppendText:
            return L"a";

        case FileOpenMode_ReadBinary:
            return L"rb";

        case FileOpenMode_WriteBinary:
            return L"wb";

        case FileOpenMode_AppendBinary:
            return L"ab";

        default:
            return NULL;
    }
}

static Error PopulateEntryInfoFromHandle(const unsigned char* path, HANDLE handle, FileSystemEntryInfo* outInfo)
{
    FILE_BASIC_INFO BasicInfo;
    BY_HANDLE_FILE_INFORMATION HandleInfo;
    ULARGE_INTEGER FileSize;
    Error Result = Error_CreateSuccess();

    Memory_Zero(&BasicInfo, sizeof(BasicInfo));
    Memory_Zero(&HandleInfo, sizeof(HandleInfo));
    if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &BasicInfo, sizeof(BasicInfo)))
    {
        return CreateWin32Error(u8"get file system entry info for", path, GetLastError());
    }
    if (!GetFileInformationByHandle(handle, &HandleInfo))
    {
        return CreateWin32Error(u8"get file system entry info for", path, GetLastError());
    }

    Result = DuplicateString(path, (unsigned char**)&outInfo->_path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = CopyLastEntryName(path, (unsigned char**)&outInfo->_name);
    if (Result.Code != ErrorCode_Success)
    {
        FileSystemEntryInfo_Deconstruct(outInfo);
        return Result;
    }

    if ((BasicInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        outInfo->_entryType = FileSystemEntryType_SymbolicLink;
    }
    else if ((BasicInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        outInfo->_entryType = FileSystemEntryType_Directory;
    }
    else
    {
        outInfo->_entryType = FileSystemEntryType_File;
    }

    FileSize.LowPart = HandleInfo.nFileSizeLow;
    FileSize.HighPart = HandleInfo.nFileSizeHigh;
    if (FileSize.QuadPart > (ULONGLONG)SIZE_MAX)
    {
        FileSystemEntryInfo_Deconstruct(outInfo);
        return Error_Construct3(ErrorCode_ArgumentOutOfRange,
            u8"Windows file system entry \"%s\" exceeds the supported size range.",
            path);
    }

    outInfo->_sizeInBytes = (size_t)FileSize.QuadPart;
    outInfo->_lastAccessTime = ConvertFileTimeToUnixTime(ConvertLargeIntegerToFileTime(BasicInfo.LastAccessTime));
    outInfo->_lastModificationTime = ConvertFileTimeToUnixTime(ConvertLargeIntegerToFileTime(BasicInfo.LastWriteTime));
    outInfo->_creationTime = ConvertFileTimeToUnixTime(ConvertLargeIntegerToFileTime(BasicInfo.CreationTime));
    outInfo->_statusChangeTime = ConvertFileTimeToUnixTime(ConvertLargeIntegerToFileTime(BasicInfo.ChangeTime));
    outInfo->_isHidden = (BasicInfo.FileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    return Error_CreateSuccess();
}


// Public functions.
Error FileSystemPlatform_OpenFileStream(const unsigned char* path, FileOpenMode mode, FileStream* stream)
{
    wchar_t* WidePath = NULL;
    FILE* NativeHandle = NULL;
    const wchar_t* NativeMode = NULL;
    Error Result = Error_CreateSuccess();

    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }

    NativeMode = GetFileModeString(mode);
    if (NativeMode == NULL)
    {
        return Error_Construct3(ErrorCode_IllegalArgument,
            u8"Unsupported Windows file open mode %d.",
            (int)mode);
    }

    Result = ConvertUtf8ToWide(path, &WidePath);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    NativeHandle = _wfopen(WidePath, NativeMode);
    if (NativeHandle == NULL)
    {
        DWORD ErrorCode = GetLastError();
        Memory_Free(WidePath);
        return CreateWin32Error(u8"open file stream for", path, ErrorCode);
    }

    Memory_Free(WidePath);
    return FileStream_ConstructFromHandle(stream,
        NativeHandle,
        (mode == FileOpenMode_ReadText) || (mode == FileOpenMode_ReadBinary) ? IOStreamFlags_CanRead | IOStreamFlags_CanSeek : IOStreamFlags_CanWrite | IOStreamFlags_CanSeek,
        true);
}

Error FileSystemPlatform_GetEntryInfo(const unsigned char* path, FileSystemEntryInfo* outInfo)
{
    wchar_t* WidePath = NULL;
    HANDLE Handle = INVALID_HANDLE_VALUE;
    Error Result = Error_CreateSuccess();

    Result = ConvertUtf8ToWide(path, &WidePath);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Handle = CreateFileW(WidePath,
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        DWORD NativeError = GetLastError();
        Memory_Free(WidePath);
        return CreateWin32Error(u8"get file system entry info for", path, NativeError);
    }

    Result = PopulateEntryInfoFromHandle(path, Handle, outInfo);
    CloseHandle(Handle);
    Memory_Free(WidePath);
    return Result;
}

Error FileSystemPlatform_CreateDirectory(const unsigned char* path)
{
    wchar_t* WidePath = NULL;
    Error Result = ConvertUtf8ToWide(path, &WidePath);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!CreateDirectoryW(WidePath, NULL))
    {
        DWORD NativeError = GetLastError();
        Memory_Free(WidePath);
        return CreateWin32Error(u8"create directory", path, NativeError);
    }

    Memory_Free(WidePath);
    return Error_CreateSuccess();
}

Error FileSystemPlatform_OpenDirectory(const unsigned char* path, void** handle)
{
    wchar_t* WidePath = NULL;
    wchar_t* SearchPattern = NULL;
    size_t WideLength = 0;
    NativeDirectoryEnumerator* Enumerator = NULL;
    Error Result = Error_CreateSuccess();

    if (handle == NULL)
    {
        return CreateNullArgumentError(u8"handle");
    }

    *handle = NULL;
    Result = ConvertUtf8ToWide(path, &WidePath);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    WideLength = wcslen(WidePath);
    SearchPattern = Memory_Allocate(sizeof(*SearchPattern) * (WideLength + 3));
    Memory_Copy(WidePath, SearchPattern, sizeof(*SearchPattern) * WideLength);
    if ((WideLength > 0) && !IsDirectorySeparator((unsigned char)WidePath[WideLength - 1]))
    {
        SearchPattern[WideLength] = (wchar_t)ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
        WideLength++;
    }

    SearchPattern[WideLength] = L'*';
    SearchPattern[WideLength + 1] = 0;

    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    Enumerator->_handle = FindFirstFileW(SearchPattern, &Enumerator->_findData);
    Enumerator->_hasBufferedResult = true;
    Memory_Free(SearchPattern);
    Memory_Free(WidePath);

    if (Enumerator->_handle == INVALID_HANDLE_VALUE)
    {
        DWORD NativeError = GetLastError();
        Memory_Free(Enumerator);
        return CreateWin32Error(u8"open directory", path, NativeError);
    }

    *handle = Enumerator;
    return Error_CreateSuccess();
}

Error FileSystemPlatform_ReadDirectoryEntry(void* handle, const unsigned char* directoryPath, FileSystemEntryInfo* outInfo, bool* hasEntry)
{
    NativeDirectoryEnumerator* Enumerator = handle;
    unsigned char* EntryName = NULL;
    unsigned char* FullPath = NULL;
    Error Result = Error_CreateSuccess();

    if (Enumerator == NULL)
    {
        return CreateNullArgumentError(u8"handle");
    }
    if (outInfo == NULL)
    {
        return CreateNullArgumentError(u8"outInfo");
    }
    if (hasEntry == NULL)
    {
        return CreateNullArgumentError(u8"hasEntry");
    }

    *hasEntry = false;
    if (!Enumerator->_hasBufferedResult)
    {
        if (!FindNextFileW(Enumerator->_handle, &Enumerator->_findData))
        {
            DWORD NativeError = GetLastError();
            if (NativeError == ERROR_NO_MORE_FILES)
            {
                return Error_CreateSuccess();
            }

            return CreateWin32Error(u8"read directory entry from", directoryPath, NativeError);
        }
    }

    Enumerator->_hasBufferedResult = false;
    Result = ConvertWideToUtf8(Enumerator->_findData.cFileName, &EntryName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = CombinePaths(directoryPath, EntryName, &FullPath);
    Memory_Free(EntryName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = FileSystemPlatform_GetEntryInfo(FullPath, outInfo);
    Memory_Free(FullPath);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *hasEntry = true;
    return Error_CreateSuccess();
}

void FileSystemPlatform_CloseDirectory(void* handle)
{
    NativeDirectoryEnumerator* Enumerator = handle;

    if (Enumerator == NULL)
    {
        return;
    }

    if (Enumerator->_handle != INVALID_HANDLE_VALUE)
    {
        (void)FindClose(Enumerator->_handle);
    }

    Memory_Zero(Enumerator, sizeof(*Enumerator));
    Memory_Free(Enumerator);
}

Error FileSystemPlatform_DeleteEntry(const unsigned char* path)
{
    FileSystemEntryInfo EntryInfo;
    wchar_t* WidePath = NULL;
    Error Result = Error_CreateSuccess();

    Memory_Zero(&EntryInfo, sizeof(EntryInfo));
    Result = FileSystemPlatform_GetEntryInfo(path, &EntryInfo);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ConvertUtf8ToWide(path, &WidePath);
    if (Result.Code != ErrorCode_Success)
    {
        FileSystemEntryInfo_Deconstruct(&EntryInfo);
        return Result;
    }

    if (EntryInfo._entryType == FileSystemEntryType_Directory)
    {
        if (!RemoveDirectoryW(WidePath))
        {
            DWORD NativeError = GetLastError();
            Memory_Free(WidePath);
            FileSystemEntryInfo_Deconstruct(&EntryInfo);
            return CreateWin32Error(u8"delete directory", path, NativeError);
        }
    }
    else
    {
        if (!DeleteFileW(WidePath))
        {
            DWORD NativeError = GetLastError();
            Memory_Free(WidePath);
            FileSystemEntryInfo_Deconstruct(&EntryInfo);
            return CreateWin32Error(u8"delete file system entry", path, NativeError);
        }
    }

    Memory_Free(WidePath);
    FileSystemEntryInfo_Deconstruct(&EntryInfo);
    return Error_CreateSuccess();
}

Error FileSystemPlatform_MoveEntry(const unsigned char* oldPath, const unsigned char* newPath)
{
    wchar_t* WideOldPath = NULL;
    wchar_t* WideNewPath = NULL;
    Error Result = ConvertUtf8ToWide(oldPath, &WideOldPath);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ConvertUtf8ToWide(newPath, &WideNewPath);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(WideOldPath);
        return Result;
    }

    if (!MoveFileExW(WideOldPath, WideNewPath, MOVEFILE_COPY_ALLOWED))
    {
        DWORD NativeError = GetLastError();
        Memory_Free(WideOldPath);
        Memory_Free(WideNewPath);
        return CreateWin32Error(u8"move file system entry", oldPath, NativeError);
    }

    Memory_Free(WideOldPath);
    Memory_Free(WideNewPath);
    return Error_CreateSuccess();
}

#endif
