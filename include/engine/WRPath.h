#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include <stddef.h>
#include "WRUnicode.h"


// Types.
typedef enum PathTypeEnum
{
    FilePath_None,
    FilePathType_Absolute,
    FilePathType_Relative
} PathType;

typedef enum PathNormalizeConditionsEnum
{
    PathNormalizeConditions_None,
    PathNormalizeConditions_Separator,
    PathNormalizeConditions_DirectorySegment,
} PathNormalizeConditions;


// Functions.
Error Path_ChangeExtension(ErrorMessagePool* errorPool, const unsigned char* path, const unsigned char* newExtension, GenericBuffer* result);

Error Path_RemoveExtension(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result);

Error Path_GetExtension(ErrorMessagePool* errorPool, const unsigned char* path, const unsigned char** extension);

Error Path_HasExtension(ErrorMessagePool* errorPool, const unsigned char* path, bool* hasExtension);

Error Path_Combine(ErrorMessagePool* errorPool, const unsigned char** paths, size_t pathCount, GenericBuffer* result);

Error Path_Append(ErrorMessagePool* errorPool, const unsigned char* pathA, const unsigned char* pathB, GenericBuffer* result);

bool Path_EndsInDirectorySeparator(const unsigned char* path);

Error Path_GetParentPath(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result);

Error Path_GetLastEntryName(ErrorMessagePool* errorPool, const unsigned char* path, const unsigned char** lastEntryName);

Error Path_GetLastEntryStem(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result);

bool Path_IsEntryNameValid(const unsigned char* entryName);

Error Path_ValidateEntryName(ErrorMessagePool* errorPool, const unsigned char* entryName);

bool Path_IsValid(const unsigned char* path);

Error Path_Validate(ErrorMessagePool* errorPool, const unsigned char* path);

PathType Path_GetPathType(const unsigned char* path);

Error Path_Normalize(ErrorMessagePool* errorPool, const unsigned char* path, PathNormalizeConditions conditions, GenericBuffer* buffer);

bool Path_IsNormalized(const unsigned char* path, PathNormalizeConditions conditions);

bool Path_IsRooted(const unsigned char* path);

bool Path_IsFullyQualified(const unsigned char* path);

Error Path_GetRoot(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result);

bool Path_IsSubPath(const unsigned char* parentPath, const unsigned char* childPath);

Error Path_TrimTrailingSeparator(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result);

Error Path_EnsureTrailingSeparator(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result);

bool Path_ContainsDirectorySegments(const unsigned char* path);

Error Path_Split(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* strBuffer, GenericBuffer* segmentPtrBuffer);

Error Path_IsEqual(ErrorMessagePool* errorPool, const unsigned char* pathA, const unsigned char* pathB, UnicodeData* unicode);