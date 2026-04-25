#include "WRFileSystem.h"
#include "WRIO.h"
#include "WRMemory.h"
#include <stdint.h>


// Macros.


// Types.
typedef enum FileSystemEntryFilterEnum
{
    FileSystemEntryFilter_All,
    FileSystemEntryFilter_Files,
    FileSystemEntryFilter_Directories,
} FileSystemEntryFilter;

typedef struct DirectoryEnumerationLevelStruct
{
    void* _nativeHandle;
    unsigned char* _directoryPath;
} DirectoryEnumerationLevel;

struct DirectoryEntryEnumeratorStruct
{
    DirectorySearchOption _searchOption;
    FileSystemEntryFilter _filter;
    DirectoryEnumerationLevel* _levels;
    size_t _levelCount;
    size_t _levelCapacity;
    bool _hasBufferedEntry;
    FileSystemEntryInfo _bufferedEntry;
};


// Fields.
static const size_t DIRECTORY_ENUMERATOR_LEVEL_GROWTH = 4;


// Static functions.
Error FileSystemPlatform_OpenFileStream(const unsigned char* path, FileOpenMode mode, FileStream* stream);

Error FileSystemPlatform_GetEntryInfo(const unsigned char* path, FileSystemEntryInfo* outInfo);

Error FileSystemPlatform_CreateDirectory(const unsigned char* path);

Error FileSystemPlatform_OpenDirectory(const unsigned char* path, void** handle);

Error FileSystemPlatform_ReadDirectoryEntry(void* handle, const unsigned char* directoryPath, FileSystemEntryInfo* outInfo, bool* hasEntry);

void FileSystemPlatform_CloseDirectory(void* handle);

Error FileSystemPlatform_DeleteEntry(const unsigned char* path);

Error FileSystemPlatform_MoveEntry(const unsigned char* oldPath, const unsigned char* newPath);

static size_t GetStringLength(const unsigned char* text)
{
    size_t Length = 0;

    if (text == NULL)
    {
        return 0;
    }

    while (text[Length] != 0)
    {
        Length++;
    }

    return Length;
}

static bool IsDirectorySeparator(unsigned char character)
{
    return (character == u8'/') || (character == u8'\\');
}

static bool IsAsciiLetter(unsigned char character)
{
    return ((character >= u8'a') && (character <= u8'z'))
        || ((character >= u8'A') && (character <= u8'Z'));
}

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"File system argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateEmptyPathError(void)
{
    return Error_Construct1(ErrorCode_InvalidPath,
        u8"File system paths must not be empty.");
}

static Error CreateBufferTypeError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"File system argument \"%s\" must be a byte buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreateBufferTooSmallError(const unsigned char* operationName, size_t requiredCapacity)
{
    return Error_Construct3(ErrorCode_BufferTooSmall,
        u8"Cannot %s because the destination buffer requires at least %zu bytes of capacity.",
        operationName,
        requiredCapacity);
}

static Error CreateOverflowError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Cannot %s because the required size exceeds the supported range.",
        operationName);
}

static Error CreateNoMoreEntriesError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The directory entry enumerator has no more matching entries.");
}

static Error ValidatePathArgument(const unsigned char* path, const unsigned char* argumentName)
{
    if (path == NULL)
    {
        return CreateNullArgumentError(argumentName);
    }
    if (path[0] == 0)
    {
        return CreateEmptyPathError();
    }

    return Error_CreateSuccess();
}

static Error ValidateByteBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    if (buffer == NULL)
    {
        return CreateNullArgumentError(argumentName);
    }
    if (buffer->_elementSize != sizeof(unsigned char))
    {
        return CreateBufferTypeError(argumentName, buffer->_elementSize);
    }

    return Error_CreateSuccess();
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
        return CreateOverflowError(u8"duplicate the file system string");
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
    if (text == NULL)
    {
        if (outText == NULL)
        {
            return CreateNullArgumentError(u8"outText");
        }

        *outText = NULL;
        return Error_CreateSuccess();
    }

    return DuplicateStringBySize(text, GetStringLength(text), outText);
}

static size_t GetRootLength(const unsigned char* path)
{
    size_t Index = 0;

    if ((path == NULL) || (path[0] == 0))
    {
        return 0;
    }

    if (IsDirectorySeparator(path[0]))
    {
        if (!IsDirectorySeparator(path[1]))
        {
            return 1;
        }

        Index = 2;
        while ((path[Index] != 0) && !IsDirectorySeparator(path[Index]))
        {
            Index++;
        }
        while (IsDirectorySeparator(path[Index]))
        {
            Index++;
        }
        while ((path[Index] != 0) && !IsDirectorySeparator(path[Index]))
        {
            Index++;
        }
        while (IsDirectorySeparator(path[Index]))
        {
            Index++;
        }

        return Index;
    }

    if (IsAsciiLetter(path[0]) && (path[1] == u8':'))
    {
        if (IsDirectorySeparator(path[2]))
        {
            return 3;
        }

        return 2;
    }

    return 0;
}

static size_t GetTrimmedPathLength(const unsigned char* path)
{
    size_t Length = GetStringLength(path);
    size_t RootLength = GetRootLength(path);

    while ((Length > RootLength) && IsDirectorySeparator(path[Length - 1]))
    {
        Length--;
    }

    return Length;
}

static size_t GetParentPathLength(const unsigned char* path)
{
    size_t Length = GetTrimmedPathLength(path);
    size_t RootLength = GetRootLength(path);

    if (Length <= RootLength)
    {
        return RootLength;
    }

    while ((Length > RootLength) && !IsDirectorySeparator(path[Length - 1]))
    {
        Length--;
    }
    while ((Length > RootLength) && IsDirectorySeparator(path[Length - 1]))
    {
        Length--;
    }

    if ((Length == 0) && (RootLength > 0))
    {
        return RootLength;
    }

    return Length;
}

static Error CombinePaths(const unsigned char* leftPath, const unsigned char* rightPath, unsigned char** outPath)
{
    size_t LeftLength = 0;
    size_t RightLength = 0;
    bool NeedsSeparator = false;
    size_t TotalLength = 0;
    unsigned char* Result = NULL;

    if (outPath == NULL)
    {
        return CreateNullArgumentError(u8"outPath");
    }

    *outPath = NULL;
    LeftLength = GetStringLength(leftPath);
    RightLength = GetStringLength(rightPath);
    NeedsSeparator = (LeftLength > 0)
        && (RightLength > 0)
        && !IsDirectorySeparator(leftPath[LeftLength - 1])
        && !IsDirectorySeparator(rightPath[0]);

    if (LeftLength > SIZE_MAX - RightLength)
    {
        return CreateOverflowError(u8"combine file system paths");
    }

    TotalLength = LeftLength + RightLength;
    if (NeedsSeparator)
    {
        if (TotalLength == SIZE_MAX)
        {
            return CreateOverflowError(u8"combine file system paths");
        }

        TotalLength++;
    }

    Result = Memory_Allocate(TotalLength + 1);
    if (LeftLength > 0)
    {
        Memory_Copy(leftPath, Result, LeftLength);
    }

    if (NeedsSeparator)
    {
        Result[LeftLength] = u8'/';
        LeftLength++;
    }
    if (RightLength > 0)
    {
        Memory_Copy(rightPath, Result + LeftLength, RightLength);
    }

    Result[TotalLength] = 0;
    *outPath = Result;
    return Error_CreateSuccess();
}

static bool IsSpecialDirectoryName(const unsigned char* name)
{
    if (name == NULL)
    {
        return false;
    }

    if ((name[0] == u8'.') && (name[1] == 0))
    {
        return true;
    }

    return (name[0] == u8'.') && (name[1] == u8'.') && (name[2] == 0);
}

static bool DoesEntryMatchFilter(FileSystemEntryType entryType, FileSystemEntryFilter filter)
{
    switch (filter)
    {
        case FileSystemEntryFilter_All:
            return true;

        case FileSystemEntryFilter_Files:
            return entryType == FileSystemEntryType_File;

        case FileSystemEntryFilter_Directories:
            return entryType == FileSystemEntryType_Directory;

        default:
            return false;
    }
}

static void CloseDirectoryLevel(DirectoryEnumerationLevel* level)
{
    if (level == NULL)
    {
        return;
    }

    if (level->_nativeHandle != NULL)
    {
        FileSystemPlatform_CloseDirectory(level->_nativeHandle);
        level->_nativeHandle = NULL;
    }
    if (level->_directoryPath != NULL)
    {
        Memory_Free(level->_directoryPath);
        level->_directoryPath = NULL;
    }
}

static Error EnsureEnumeratorLevelCapacity(DirectoryEntryEnumerator* enumerator, size_t requiredCount)
{
    size_t NewCapacity = 0;
    DirectoryEnumerationLevel* NewLevels = NULL;

    if (requiredCount <= enumerator->_levelCapacity)
    {
        return Error_CreateSuccess();
    }

    NewCapacity = enumerator->_levelCapacity;
    if (NewCapacity == 0)
    {
        NewCapacity = DIRECTORY_ENUMERATOR_LEVEL_GROWTH;
    }

    while (NewCapacity < requiredCount)
    {
        if (NewCapacity > (SIZE_MAX - DIRECTORY_ENUMERATOR_LEVEL_GROWTH))
        {
            return CreateOverflowError(u8"grow the directory enumerator");
        }

        NewCapacity += DIRECTORY_ENUMERATOR_LEVEL_GROWTH;
    }

    NewLevels = Memory_Reallocate(enumerator->_levels, sizeof(*NewLevels) * NewCapacity);
    enumerator->_levels = NewLevels;
    enumerator->_levelCapacity = NewCapacity;
    return Error_CreateSuccess();
}

static Error PushDirectoryLevel(DirectoryEntryEnumerator* enumerator, const unsigned char* path)
{
    Error Result = Error_CreateSuccess();
    void* NativeHandle = NULL;
    unsigned char* CopiedPath = NULL;

    Result = EnsureEnumeratorLevelCapacity(enumerator, enumerator->_levelCount + 1);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = FileSystemPlatform_OpenDirectory(path, &NativeHandle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = DuplicateString(path, &CopiedPath);
    if (Result.Code != ErrorCode_Success)
    {
        FileSystemPlatform_CloseDirectory(NativeHandle);
        return Result;
    }

    enumerator->_levels[enumerator->_levelCount] = (DirectoryEnumerationLevel)
    {
        ._nativeHandle = NativeHandle,
        ._directoryPath = CopiedPath,
    };
    enumerator->_levelCount++;
    return Error_CreateSuccess();
}

static void PopDirectoryLevel(DirectoryEntryEnumerator* enumerator)
{
    if ((enumerator == NULL) || (enumerator->_levelCount == 0))
    {
        return;
    }

    enumerator->_levelCount--;
    CloseDirectoryLevel(&enumerator->_levels[enumerator->_levelCount]);
}

static Error PrefetchNextEntry(DirectoryEntryEnumerator* enumerator, bool* hasEntry)
{
    if (hasEntry == NULL)
    {
        return CreateNullArgumentError(u8"hasEntry");
    }

    *hasEntry = false;
    if (enumerator->_hasBufferedEntry)
    {
        *hasEntry = true;
        return Error_CreateSuccess();
    }

    while (enumerator->_levelCount > 0)
    {
        DirectoryEnumerationLevel* CurrentLevel = &enumerator->_levels[enumerator->_levelCount - 1];
        FileSystemEntryInfo EntryInfo;
        bool NativeHasEntry = false;
        Error Result = Error_CreateSuccess();

        Memory_Zero(&EntryInfo, sizeof(EntryInfo));
        Result = FileSystemPlatform_ReadDirectoryEntry(CurrentLevel->_nativeHandle,
            CurrentLevel->_directoryPath,
            &EntryInfo,
            &NativeHasEntry);
        if (Result.Code != ErrorCode_Success)
        {
            FileSystemEntryInfo_Deconstruct(&EntryInfo);
            return Result;
        }
        if (!NativeHasEntry)
        {
            PopDirectoryLevel(enumerator);
            continue;
        }
        if (IsSpecialDirectoryName(EntryInfo._name))
        {
            FileSystemEntryInfo_Deconstruct(&EntryInfo);
            continue;
        }

        if ((enumerator->_searchOption == DirectorySearchOption_All)
            && (EntryInfo._entryType == FileSystemEntryType_Directory))
        {
            Result = PushDirectoryLevel(enumerator, EntryInfo._path);
            if (Result.Code != ErrorCode_Success)
            {
                FileSystemEntryInfo_Deconstruct(&EntryInfo);
                return Result;
            }
        }

        if (!DoesEntryMatchFilter(EntryInfo._entryType, enumerator->_filter))
        {
            FileSystemEntryInfo_Deconstruct(&EntryInfo);
            continue;
        }

        enumerator->_bufferedEntry = EntryInfo;
        enumerator->_hasBufferedEntry = true;
        *hasEntry = true;
        return Error_CreateSuccess();
    }

    return Error_CreateSuccess();
}

static Error CreateEnumerator(const unsigned char* path,
    DirectorySearchOption searchOption,
    FileSystemEntryFilter filter,
    DirectoryEntryEnumerator** outEnumerator)
{
    DirectoryEntryEnumerator* Enumerator = NULL;
    Error Result = Error_CreateSuccess();

    if (outEnumerator == NULL)
    {
        return CreateNullArgumentError(u8"enumerator");
    }

    *outEnumerator = NULL;
    Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    Memory_Zero(Enumerator, sizeof(*Enumerator));
    Enumerator->_searchOption = searchOption;
    Enumerator->_filter = filter;

    Result = PushDirectoryLevel(Enumerator, path);
    if (Result.Code != ErrorCode_Success)
    {
        DirectoryEntryEnumerator_Deconstruct(Enumerator);
        return Result;
    }

    *outEnumerator = Enumerator;
    return Error_CreateSuccess();
}

static Error TryReadEntireFile(const unsigned char* path, GenericBuffer* destination, bool nullTerminate)
{
    FileStream Stream;
    size_t StreamSize = 0;
    size_t RequiredCapacity = 0;
    IOStream* StreamBase = NULL;
    Error Result = Error_CreateSuccess();

    Memory_Zero(&Stream, sizeof(Stream));
    Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    GenericBuffer_Clear(destination);
    Result = FileSystem_OpenFileStream(path, FileOpenMode_ReadBinary, &Stream);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    StreamBase = FileStream_AsIOStream(&Stream);
    Result = IOStream_GetStreamSizeTotal(StreamBase, &StreamSize);
    if (Result.Code != ErrorCode_Success)
    {
        FileStream_Deconstruct(&Stream);
        return Result;
    }

    RequiredCapacity = StreamSize;
    if (nullTerminate)
    {
        if (RequiredCapacity == SIZE_MAX)
        {
            FileStream_Deconstruct(&Stream);
            return CreateOverflowError(u8"read the file text");
        }

        RequiredCapacity++;
    }

    if (!GenericBuffer_EnsureTotalCapacity(destination, RequiredCapacity))
    {
        FileStream_Deconstruct(&Stream);
        return CreateBufferTooSmallError(nullTerminate ? u8"read the file text" : u8"read the file bytes", RequiredCapacity);
    }

    Result = IOStream_SetPosition(StreamBase, 0);
    if (Result.Code != ErrorCode_Success)
    {
        FileStream_Deconstruct(&Stream);
        return Result;
    }

    Result = IOStream_Read(StreamBase, StreamSize, destination);
    if (Result.Code != ErrorCode_Success)
    {
        FileStream_Deconstruct(&Stream);
        return Result;
    }

    if (nullTerminate && (destination->_data != NULL))
    {
        destination->_data[destination->_count] = 0;
    }

    FileStream_Deconstruct(&Stream);
    return Error_CreateSuccess();
}

static Error WriteEntireFile(const unsigned char* path, FileOpenMode mode, const unsigned char* bytes, size_t byteCount)
{
    FileStream Stream;
    IOStream* StreamBase = NULL;
    Error Result = Error_CreateSuccess();

    Memory_Zero(&Stream, sizeof(Stream));
    Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((bytes == NULL) && (byteCount > 0))
    {
        return CreateNullArgumentError(u8"bytes");
    }

    Result = FileSystem_OpenFileStream(path, mode, &Stream);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    StreamBase = FileStream_AsIOStream(&Stream);
    Result = IOStream_Write(StreamBase, bytes, byteCount);
    FileStream_Deconstruct(&Stream);
    return Result;
}

static Error CreateDirectoryIfMissing(const unsigned char* path)
{
    FileSystemEntryInfo EntryInfo;
    Error Result = Error_CreateSuccess();

    Memory_Zero(&EntryInfo, sizeof(EntryInfo));
    Result = FileSystem_GetEntryInfo(path, &EntryInfo);
    if (Result.Code == ErrorCode_Success)
    {
        if (EntryInfo._entryType != FileSystemEntryType_Directory)
        {
            FileSystemEntryInfo_Deconstruct(&EntryInfo);
            return Error_Construct3(ErrorCode_InvalidOperation,
                u8"Cannot create the directory \"%s\" because a non-directory entry already exists there.",
                path);
        }

        FileSystemEntryInfo_Deconstruct(&EntryInfo);
        return Error_CreateSuccess();
    }

    if ((Result.Code != ErrorCode_FileNotFound) && (Result.Code != ErrorCode_DirectoryNotFound))
    {
        return Result;
    }

    return FileSystem_CreateDirectory(path);
}


// Public functions.
Error FileSystem_GetEntries(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator)
{
    return CreateEnumerator(path, searchOption, FileSystemEntryFilter_All, enumerator);
}

Error FileSystem_GetFiles(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator)
{
    return CreateEnumerator(path, searchOption, FileSystemEntryFilter_Files, enumerator);
}

Error FileSystem_GetDirectories(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator)
{
    return CreateEnumerator(path, searchOption, FileSystemEntryFilter_Directories, enumerator);
}

Error DirectoryEntryEnumerator_HasNext(DirectoryEntryEnumerator* enumerator, bool* hasNext)
{
    if (enumerator == NULL)
    {
        return CreateNullArgumentError(u8"enumerator");
    }
    if (hasNext == NULL)
    {
        return CreateNullArgumentError(u8"hasNext");
    }

    return PrefetchNextEntry(enumerator, hasNext);
}

Error DirectoryEntryEnumerator_Next(DirectoryEntryEnumerator* enumerator, FileSystemEntryInfo* outInfo)
{
    bool HasEntry = false;
    Error Result = Error_CreateSuccess();

    if (enumerator == NULL)
    {
        return CreateNullArgumentError(u8"enumerator");
    }
    if (outInfo == NULL)
    {
        return CreateNullArgumentError(u8"outInfo");
    }

    Result = PrefetchNextEntry(enumerator, &HasEntry);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HasEntry)
    {
        return CreateNoMoreEntriesError();
    }

    *outInfo = enumerator->_bufferedEntry;
    Memory_Zero(&enumerator->_bufferedEntry, sizeof(enumerator->_bufferedEntry));
    enumerator->_hasBufferedEntry = false;
    return Error_CreateSuccess();
}

Error DirectoryEntryEnumerator_Deconstruct(DirectoryEntryEnumerator* enumerator)
{
    if (enumerator == NULL)
    {
        return Error_CreateSuccess();
    }

    if (enumerator->_hasBufferedEntry)
    {
        FileSystemEntryInfo_Deconstruct(&enumerator->_bufferedEntry);
        enumerator->_hasBufferedEntry = false;
    }

    while (enumerator->_levelCount > 0)
    {
        PopDirectoryLevel(enumerator);
    }

    if (enumerator->_levels != NULL)
    {
        Memory_Free(enumerator->_levels);
    }

    Memory_Zero(enumerator, sizeof(*enumerator));
    Memory_Free(enumerator);
    return Error_CreateSuccess();
}

Error FileSystem_GetEntryInfo(const unsigned char* path, FileSystemEntryInfo* outInfo)
{
    Error Result = Error_CreateSuccess();

    if (outInfo == NULL)
    {
        return CreateNullArgumentError(u8"outInfo");
    }

    Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Zero(outInfo, sizeof(*outInfo));
    return FileSystemPlatform_GetEntryInfo(path, outInfo);
}

Error FileSystem_CreateDirectory(const unsigned char* path)
{
    Error Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return FileSystemPlatform_CreateDirectory(path);
}

Error FileSystem_CreateAllDirectories(const unsigned char* path)
{
    size_t PathLength = 0;
    size_t RootLength = 0;
    size_t PrefixLength = 0;
    size_t Index = 0;
    Error Result = Error_CreateSuccess();

    Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    PathLength = GetTrimmedPathLength(path);
    RootLength = GetRootLength(path);
    if (PathLength == 0)
    {
        return Error_CreateSuccess();
    }

    PrefixLength = RootLength;
    while (PrefixLength < PathLength)
    {
        while ((PrefixLength < PathLength) && IsDirectorySeparator(path[PrefixLength]))
        {
            PrefixLength++;
        }

        Index = PrefixLength;
        while ((Index < PathLength) && !IsDirectorySeparator(path[Index]))
        {
            Index++;
        }

        if (Index > 0)
        {
            unsigned char* PrefixPath = NULL;

            Result = DuplicateStringBySize(path, Index, &PrefixPath);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }

            Result = CreateDirectoryIfMissing(PrefixPath);
            Memory_Free(PrefixPath);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }

        PrefixLength = Index + 1;
    }

    return Error_CreateSuccess();
}

Error FileSystem_OpenFileStream(const unsigned char* path, FileOpenMode mode, FileStream* stream)
{
    Error Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }

    Memory_Zero(stream, sizeof(*stream));
    return FileSystemPlatform_OpenFileStream(path, mode, stream);
}

Error FileSystem_ReadAllText(const unsigned char* path, GenericBuffer* destination)
{
    return TryReadEntireFile(path, destination, true);
}

Error FileSystem_ReadAllBytes(const unsigned char* path, GenericBuffer* destination)
{
    return TryReadEntireFile(path, destination, false);
}

Error FileSystem_WriteAllText(const unsigned char* path, const unsigned char* text)
{
    if (text == NULL)
    {
        return CreateNullArgumentError(u8"text");
    }

    return WriteEntireFile(path, FileOpenMode_WriteText, text, GetStringLength(text));
}

Error FileSystem_WriteAllBytes(const unsigned char* path, const unsigned char* bytes, size_t byteCount)
{
    return WriteEntireFile(path, FileOpenMode_WriteBinary, bytes, byteCount);
}

Error FileSystem_DeleteEntry(const unsigned char* path)
{
    Error Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return FileSystemPlatform_DeleteEntry(path);
}

Error FileSystem_MoveEntry(const unsigned char* oldPath, const unsigned char* newPath)
{
    Error Result = ValidatePathArgument(oldPath, u8"oldPath");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidatePathArgument(newPath, u8"newPath");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return FileSystemPlatform_MoveEntry(oldPath, newPath);
}

Error FileSystem_RenameEntry(const unsigned char* path, const unsigned char* newName)
{
    size_t ParentLength = 0;
    unsigned char* NewPath = NULL;
    Error Result = Error_CreateSuccess();

    Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathArgument(newName, u8"newName");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    for (size_t Index = 0; newName[Index] != 0; Index++)
    {
        if (IsDirectorySeparator(newName[Index]))
        {
            return Error_Construct3(ErrorCode_InvalidPath,
                u8"Cannot rename the file system entry because the new name \"%s\" contains directory separators.",
                newName);
        }
    }

    ParentLength = GetParentPathLength(path);
    if (ParentLength == 0)
    {
        Result = DuplicateString(newName, &NewPath);
    }
    else
    {
        unsigned char* ParentPath = NULL;
        Result = DuplicateStringBySize(path, ParentLength, &ParentPath);
        if (Result.Code == ErrorCode_Success)
        {
            Result = CombinePaths(ParentPath, newName, &NewPath);
        }

        if (ParentPath != NULL)
        {
            Memory_Free(ParentPath);
        }
    }

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = FileSystem_MoveEntry(path, NewPath);
    Memory_Free(NewPath);
    return Result;
}

Error FileSystem_CreateFile(const unsigned char* path)
{
    FileStream Stream;
    Error Result = Error_CreateSuccess();

    Memory_Zero(&Stream, sizeof(Stream));
    Result = FileSystem_OpenFileStream(path, FileOpenMode_WriteBinary, &Stream);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    FileStream_Deconstruct(&Stream);
    return Error_CreateSuccess();
}

void FileSystemEntryInfo_Deconstruct(FileSystemEntryInfo* self)
{
    if (self == NULL)
    {
        return;
    }

    if (self->_path != NULL)
    {
        Memory_Free((void*)self->_path);
    }
    if (self->_name != NULL)
    {
        Memory_Free((void*)self->_name);
    }

    Memory_Zero(self, sizeof(*self));
}
