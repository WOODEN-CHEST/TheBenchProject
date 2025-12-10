#pragma once
#include "WRError.h"
#include "WRMemory.h"
#include <time.h>
#include <stddef.h>
#include "WRIO.h"


// Types.
typedef struct DirectoryEntryEnumeratorStruct DirectoryEntryEnumerator;

typedef enum DirectorySearchOptionEnum
{
    DirectorySearchOption_TopLevel,
    DirectorySearchOption_All
} DirectorySearchOption;

typedef enum FileSystemEntryTypeEnum
{
    FileSystemEntryType_File,
    FileSystemEntryType_Directory,
    FileSystemEntryType_SymbolicLink
} FileSystemEntryType;

typedef struct FileSystemEntryInfoStruct
{
    const unsigned char* _path;
    const unsigned char* _name;
    FileSystemEntryType _entryType;
    time_t _lastAccessTime;
    time_t _lastModificationTime;
    time_t _creationTime;
    time_t _statusChangeTime;
    size_t _sizeInBytes;
    bool _isHidden;
} FileSystemEntryInfo;

typedef enum FileOpenModeEnum
{
    FileOpenMode_ReadText,
    FileOpenMode_WriteText,
    FileOpenMode_AppendText,
    FileOpenMode_ReadBinary,
    FileOpenMode_WriteBinary,
    FileOpenMode_AppendBinary
} FileOpenMode;




// Functions.
Error FileSystem_GetEntries(ErrorMessagePool* errorPool,
    const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

Error FileSystem_GetFiles(ErrorMessagePool* errorPool, 
    const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

Error FileSystem_GetDirectories(ErrorMessagePool* errorPool,
    const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

Error FileSystem_GetEntryInfo(ErrorMessagePool* errorPool, const unsigned char* path, FileSystemEntryInfo* info);

Error FileSystem_CreateLastDirectory(ErrorMessagePool* errorPool, const unsigned char* path);

Error FileSystem_CreateAllDirectories(ErrorMessagePool* errorPool, const unsigned char* path);

Error FileSystem_OpenFileStream(ErrorMessagePool* errorPool, const unsigned char* path, FileOpenMode mode, IOStream** stream);

Error FileSystem_DeleteEntry(ErrorMessagePool* errorPool, const unsigned char* path);

Error FileSystem_MoveEntry(ErrorMessagePool* errorPool, const unsigned char* oldPath, const unsigned char* newPath);

Error FileSystem_RenameEntry(ErrorMessagePool* errorPool, const unsigned char* path, const unsigned char* newName);