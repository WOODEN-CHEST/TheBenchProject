#pragma once
#include "WRFileSystem.h"


// Functions.
Error FileSystemPlatform_OpenFileStream(const unsigned char* path, FileOpenMode mode, FileStream* stream);

Error FileSystemPlatform_GetEntryInfo(const unsigned char* path, FileSystemEntryInfo* outInfo);

Error FileSystemPlatform_CreateDirectory(const unsigned char* path);

Error FileSystemPlatform_OpenDirectory(const unsigned char* path, void** handle);

Error FileSystemPlatform_ReadDirectoryEntry(void* handle,
    const unsigned char* directoryPath,
    FileSystemEntryInfo* outInfo,
    bool* hasEntry);

void FileSystemPlatform_CloseDirectory(void* handle);

Error FileSystemPlatform_DeleteEntry(const unsigned char* path);

Error FileSystemPlatform_MoveEntry(const unsigned char* oldPath, const unsigned char* newPath);
