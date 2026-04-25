#pragma once
#include "WRError.h"
#include "WRMemory.h"
#include "WRFileStream.h"
#include <stdbool.h>
#include <time.h>
#include <stddef.h>


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
Error FileSystem_GetEntries(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

Error FileSystem_GetFiles(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

Error FileSystem_GetDirectories(const unsigned char* path,
    DirectorySearchOption searchOption,
    DirectoryEntryEnumerator** enumerator);

Error DirectoryEntryEnumerator_HasNext(DirectoryEntryEnumerator* enumerator, bool* hasNext);

Error DirectoryEntryEnumerator_Next(DirectoryEntryEnumerator* enumerator, FileSystemEntryInfo* outInfo);

Error DirectoryEntryEnumerator_Deconstruct(DirectoryEntryEnumerator* enumerator);

Error FileSystem_GetEntryInfo(const unsigned char* path, FileSystemEntryInfo* outInfo);

Error FileSystem_CreateDirectory(const unsigned char* path);

Error FileSystem_CreateAllDirectories(const unsigned char* path);

Error FileSystem_OpenFileStream(const unsigned char* path, FileOpenMode mode, FileStream* stream);

Error FileSystem_ReadAllText(const unsigned char* path, GenericBuffer* destination);

Error FileSystem_ReadAllBytes(const unsigned char* path, GenericBuffer* destination);

Error FileSystem_WriteAllText(const unsigned char* path, const unsigned char* text);

Error FileSystem_WriteAllBytes(const unsigned char* path, const unsigned char* bytes, size_t byteCount);

Error FileSystem_DeleteEntry(const unsigned char* path);

Error FileSystem_MoveEntry(const unsigned char* oldPath, const unsigned char* newPath);

Error FileSystem_RenameEntry(const unsigned char* path, const unsigned char* newName);

Error FileSystem_CreateFile(const unsigned char* path);

void FileSystemEntryInfo_Deconstruct(FileSystemEntryInfo* self);
