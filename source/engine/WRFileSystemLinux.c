#define _POSIX_C_SOURCE 200809L
#include "WRFileSystem.h"
#include "WRFileSystemInternal.h"
#include "WRMemory.h"
#include "WREnvironment.h"
#include "WRPath.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)




// Types.
typedef struct NativeDirectoryEnumeratorStruct
{
    DIR* Directory;
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
        u8"Linux file system argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateIOError(const unsigned char* operationName, const unsigned char* path, int errorCode)
{
    ErrorCode Code = ErrorCode_IO;

    if (errorCode == ENOENT)
    {
        Code = ErrorCode_FileNotFound;
    }
    else if (errorCode == ENOTDIR)
    {
        Code = ErrorCode_DirectoryNotFound;
    }
    else if (errorCode == EINVAL)
    {
        Code = ErrorCode_InvalidPath;
    }

    return Error_Construct3(Code,
        u8"Failed to %s \"%s\". Native error code %d.",
        operationName,
        path,
        errorCode);
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
            u8"Linux file system string duplication overflowed.");
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

static Error ConvertStatToEntryInfo(const unsigned char* path,
    const struct stat* statData,
    FileSystemEntryInfo* outInfo)
{
    uintmax_t FileSize = 0;
    Error Result = Error_CreateSuccess();

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

    if (S_ISLNK(statData->st_mode))
    {
        outInfo->_entryType = FileSystemEntryType_SymbolicLink;
    }
    else if (S_ISDIR(statData->st_mode))
    {
        outInfo->_entryType = FileSystemEntryType_Directory;
    }
    else
    {
        outInfo->_entryType = FileSystemEntryType_File;
    }

    if (statData->st_size < 0)
    {
        outInfo->_sizeInBytes = 0;
    }
    else
    {
        FileSize = (uintmax_t)statData->st_size;
        if (FileSize > (uintmax_t)SIZE_MAX)
        {
            FileSystemEntryInfo_Deconstruct(outInfo);
            return Error_Construct3(ErrorCode_ArgumentOutOfRange,
                u8"Linux file system entry \"%s\" exceeds the supported size range.",
                path);
        }

        outInfo->_sizeInBytes = (size_t)FileSize;
    }

    outInfo->_lastAccessTime = statData->st_atime;
    outInfo->_lastModificationTime = statData->st_mtime;
    outInfo->_creationTime = 0;
    outInfo->_statusChangeTime = statData->st_ctime;
    outInfo->_isHidden = (outInfo->_name[0] == u8'.') && (outInfo->_name[1] != 0);
    return Error_CreateSuccess();
}

static const char* GetFileModeString(FileOpenMode mode)
{
    switch (mode)
    {
        case FileOpenMode_ReadText:
            return "r";

        case FileOpenMode_WriteText:
            return "w";

        case FileOpenMode_AppendText:
            return "a";

        case FileOpenMode_ReadBinary:
            return "rb";

        case FileOpenMode_WriteBinary:
            return "wb";

        case FileOpenMode_AppendBinary:
            return "ab";

        default:
            return NULL;
    }
}


// Public functions.
Error FileSystemPlatform_OpenFileStream(const unsigned char* path, FileOpenMode mode, FileStream* stream)
{
    const char* NativeMode = NULL;
    FILE* NativeHandle = NULL;

    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }

    NativeMode = GetFileModeString(mode);
    if (NativeMode == NULL)
    {
        return Error_Construct3(ErrorCode_IllegalArgument,
            u8"Unsupported Linux file open mode %d.",
            (int)mode);
    }

    errno = 0;
    NativeHandle = fopen((const char*)path, NativeMode);
    if (NativeHandle == NULL)
    {
        return CreateIOError(u8"open file stream for", path, errno);
    }

    return FileStream_ConstructFromHandle(stream,
        NativeHandle,
        (mode == FileOpenMode_ReadText) || (mode == FileOpenMode_ReadBinary) ? IOStreamFlags_CanRead | IOStreamFlags_CanSeek : IOStreamFlags_CanWrite | IOStreamFlags_CanSeek,
        true);
}

Error FileSystemPlatform_GetEntryInfo(const unsigned char* path, FileSystemEntryInfo* outInfo)
{
    struct stat StatData;

    errno = 0;
    if (lstat((const char*)path, &StatData) != 0)
    {
        return CreateIOError(u8"get file system entry info for", path, errno);
    }

    return ConvertStatToEntryInfo(path, &StatData, outInfo);
}

Error FileSystemPlatform_CreateDirectory(const unsigned char* path)
{
    errno = 0;
    if (mkdir((const char*)path, 0777) != 0)
    {
        return CreateIOError(u8"create directory", path, errno);
    }

    return Error_CreateSuccess();
}

Error FileSystemPlatform_OpenDirectory(const unsigned char* path, void** handle)
{
    NativeDirectoryEnumerator* Enumerator = NULL;

    if (handle == NULL)
    {
        return CreateNullArgumentError(u8"handle");
    }

    *handle = NULL;
    errno = 0;
    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    Enumerator->Directory = opendir((const char*)path);
    if (Enumerator->Directory == NULL)
    {
        Memory_Free(Enumerator);
        return CreateIOError(u8"open directory", path, errno);
    }

    *handle = Enumerator;
    return Error_CreateSuccess();
}

Error FileSystemPlatform_ReadDirectoryEntry(void* handle, const unsigned char* directoryPath, FileSystemEntryInfo* outInfo, bool* hasEntry)
{
    NativeDirectoryEnumerator* Enumerator = handle;
    struct dirent* Entry = NULL;
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
    errno = 0;
    Entry = readdir(Enumerator->Directory);
    if (Entry == NULL)
    {
        if (errno != 0)
        {
            return CreateIOError(u8"read directory entry from", directoryPath, errno);
        }

        return Error_CreateSuccess();
    }

    Result = CombinePaths(directoryPath, (const unsigned char*)Entry->d_name, &FullPath);
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

    if (Enumerator->Directory != NULL)
    {
        (void)closedir(Enumerator->Directory);
    }

    Memory_Zero(Enumerator, sizeof(*Enumerator));
    Memory_Free(Enumerator);
}

Error FileSystemPlatform_DeleteEntry(const unsigned char* path)
{
    FileSystemEntryInfo EntryInfo;
    Error Result = Error_CreateSuccess();

    Memory_Zero(&EntryInfo, sizeof(EntryInfo));
    Result = FileSystemPlatform_GetEntryInfo(path, &EntryInfo);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    errno = 0;
    if (EntryInfo._entryType == FileSystemEntryType_Directory)
    {
        if (rmdir((const char*)path) != 0)
        {
            FileSystemEntryInfo_Deconstruct(&EntryInfo);
            return CreateIOError(u8"delete directory", path, errno);
        }
    }
    else
    {
        if (unlink((const char*)path) != 0)
        {
            FileSystemEntryInfo_Deconstruct(&EntryInfo);
            return CreateIOError(u8"delete file system entry", path, errno);
        }
    }

    FileSystemEntryInfo_Deconstruct(&EntryInfo);
    return Error_CreateSuccess();
}

Error FileSystemPlatform_MoveEntry(const unsigned char* oldPath, const unsigned char* newPath)
{
    errno = 0;
    if (rename((const char*)oldPath, (const char*)newPath) != 0)
    {
        return CreateIOError(u8"move file system entry", oldPath, errno);
    }

    return Error_CreateSuccess();
}

#endif
