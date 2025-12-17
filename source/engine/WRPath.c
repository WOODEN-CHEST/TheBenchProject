#include "WRPath.h"
#include "WRMemory.h"
#include "WRError.h"
#include <stddef.h>
#include "WRUnicode.h"
#include "WREnvironment.h"
#include "WRChar.h"
#include "WRString.h"


#if defined __linux__
#define LINUX_PATH_IMPL
#elif defined _WIN32
#define WINDOWS_PATH_IMPL
#endif


// Types.
typedef struct PathSegmentStruct
{
    const unsigned char* _entry;
    const unsigned char* _extension;
    size_t _segmentIndex;
    size_t _sizeBytes;
    size_t _extensionSizeInBytes;
    size_t _startIndexInPath;
    bool _isFinalSegment;
} PathSegment;

typedef struct PathTraversalDataStruct
{
    const unsigned char* _path;
    PathSegment _currentSegment;
    size_t _curSegmentIndex;
    const unsigned char** _nextSegmentStart;
} PathTraversalData;


// Field.
static const unsigned char EXTENSION_INDICATOR = '.';


// Static functions.
static inline bool IsSeparator(unsigned char character)
{
    return (character == ENVIRONMENT_PATH_SEPARATOR_PRIMARY) || (character == ENVIRONMENT_PATH_SEPARATOR_SECONDARY);
}

static void BeginPathTraversal(const unsigned char* path, PathTraversalData* data)
{
    Memory_Zero(data, sizeof(data));
    data->_nextSegmentStart = path;
    data->_path = path;
}

static Error TraverseSegment(ErrorMessagePool* errorPool,
    PathTraversalData* traversalData)
{
    PathSegment ResultSegment;
    Memory_Zero(&ResultSegment, sizeof(ResultSegment));
    ResultSegment._segmentIndex = traversalData->_curSegmentIndex;
    ResultSegment._entry = traversalData->_nextSegmentStart;

    size_t ExtensionIndex = 0;
    size_t Index = 0;
    const unsigned char* Extension = NULL;
    const unsigned char* Path = traversalData->_nextSegmentStart;
    while ((Path[Index] != '\0') && !IsSeparator(Path[Index]))
    {
        if (Path[Index] == EXTENSION_INDICATOR)
        {
            Extension = Path + Index;
            ExtensionIndex = Index;
        }

        size_t CharSize = CharUTF8_GetByteCountChar(Path + Index);
        if (CharSize == 0)
        {
            return Error_Construct3(errorPool,
                ErrorCode_InvalidTextEncoding,
                u8"Found invalid character data in the path segment at segment index %zu, character index %zu; the byte has a value of %d.",
                traversalData->_curSegmentIndex, Index, (int32_t)Path[Index]);
        }
        Index += CharSize;
    }

    ResultSegment._sizeBytes = Index;
    ResultSegment._isFinalSegment = Path[Index] == '\0';
    ResultSegment._extension = Extension;
    ResultSegment._extensionSizeInBytes = Extension ? (Index - ExtensionIndex - 1) : 0;
    size_t NextSegmentIndex = Index;
    if (!ResultSegment._isFinalSegment)
    {
        NextSegmentIndex++;
    }
    ResultSegment._startIndexInPath = traversalData->_currentSegment._startIndexInPath + NextSegmentIndex;

    traversalData->_curSegmentIndex++;
    traversalData->_currentSegment = ResultSegment;
    traversalData->_nextSegmentStart = Path + NextSegmentIndex;
    return Error_CreateSuccess();
}

static Error GetLastSegment(ErrorMessagePool* errorPool, const unsigned char* path, PathSegment* lastSegment)
{
    PathTraversalData TraversalData;
    BeginPathTraversal(path, &TraversalData);

    do
    {
        Error ErrorResult = TraverseSegment(errorPool, &TraversalData);
        if (ErrorResult.Code != ErrorCode_Success)
        {
            return ErrorResult;
        }
    } while (!TraversalData._currentSegment._isFinalSegment);

    *lastSegment = TraversalData._currentSegment;
    return Error_CreateSuccess();
}

static Error CreateDestBufferTooSmallError(ErrorMessagePool* errorPool)
{
    return Error_Construct3(errorPool,
        ErrorCode_BufferTooSmall,
        u8"Destination buffer is too small to hold the given path.");
}


// Functions.
Error Path_ChangeExtension(ErrorMessagePool* errorPool, const unsigned char* path, const unsigned char* newExtension, GenericBuffer* result)
{
    PathSegment LastSegment;
    Error ErrorResult = GetLastSegment(errorPool, path, &LastSegment);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    size_t PathLengthWithoutExtension = LastSegment._startIndexInPath + LastSegment._sizeBytes;
    if (LastSegment._extension)
    {
        PathLengthWithoutExtension -= LastSegment._extensionSizeInBytes + 1;
    }
    ErrorResult = StringUTF8_Substring(path, 0, PathLengthWithoutExtension, result, errorPool);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    if (StringUTF8_IsNullOrEmpty(newExtension))
    {
        return Error_CreateSuccess();
    }

    if ((newExtension[0] != EXTENSION_INDICATOR) && !GenericBuffer_WriteUChar(result, EXTENSION_INDICATOR))
    {
        return CreateDestBufferTooSmallError(errorPool);
    }
    if (!GenericBuffer_WriteString(result, newExtension))
    {
        return CreateDestBufferTooSmallError(errorPool);
    }

    return Error_CreateSuccess();
}

Error Path_RemoveExtension(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result)
{
    return Path_ChangeExtension(errorPool, path, NULL, result);
}

Error Path_GetExtension(ErrorMessagePool* errorPool, const unsigned char* path, const unsigned char** extension)
{
    PathSegment LastSegment;
    Error ErrorResult = GetLastSegment(errorPool, path, &LastSegment);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    *extension = LastSegment._extension;
    return Error_CreateSuccess();
}

Error Path_HasExtension(ErrorMessagePool* errorPool, const unsigned char* path, bool* hasExtension)
{
    const unsigned char* Extension;
    Error ErrorResult = Path_GetExtension(errorPool, path, &Extension);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }
    *hasExtension = Extension != NULL;
    return ErrorResult;
}

Error Path_Combine(ErrorMessagePool* errorPool, const unsigned char** paths, size_t pathCount, GenericBuffer* result)
{
    bool EndsWithSeparatorPrevious = false;
    bool IsAnythingWritten = false;

    for (size_t i = 0; i < pathCount; i++)
    {
        const unsigned char* TargetPath = paths[i];
        size_t PathLength = StringUTF8_GetByteLength(TargetPath);
        if (PathLength == 0)
        {
            continue;
        }
        bool StartWithSeparatorCurrent = IsSeparator(TargetPath[0]);

        if (IsAnythingWritten && Path_IsFullyQualified(TargetPath))
        {
            return Error_Construct3(errorPool,
                ErrorCode_IllegalArgument,
                u8"Cannot append path; found fully qualified path \"%s\" at index %zu, it should've been the first non-empty path.",
                TargetPath, i);
        }

        if (!StartWithSeparatorCurrent
            && !EndsWithSeparatorPrevious
            && !GenericBuffer_WriteUChar(result, ENVIRONMENT_PATH_SEPARATOR_PRIMARY))
        {
            return CreateDestBufferTooSmallError(errorPool);  
        }

        size_t PathStartIndex = (StartWithSeparatorCurrent && EndsWithSeparatorPrevious) ? 1 : 0;
        if (!GenericBuffer_WriteStringBySize(result, TargetPath + PathStartIndex, PathLength - PathStartIndex))
        {
            return CreateDestBufferTooSmallError(errorPool);
        }

        EndsWithSeparatorPrevious = IsSeparator(TargetPath[PathLength - 1]);
        IsAnythingWritten = true;
    }

    return Error_CreateSuccess();
}

Error Path_Append(ErrorMessagePool* errorPool, const unsigned char* pathA, const unsigned char* pathB, GenericBuffer* result)
{
    const unsigned char* PathArray[] = { pathA, pathB };
    return Path_Combine(errorPool, PathArray, sizeof(PathArray) / sizeof(PathArray[0]), result);
}

bool Path_EndsInDirectorySeparator(const unsigned char* path)
{
    size_t StrLength = StringUTF8_GetByteLength(path);
    return (StrLength > 0) && IsSeparator(path[StrLength - 1]);
}

Error Path_GetParentPath(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result)
{
    PathSegment PreviousSegment;
    Memory_Zero(&PreviousSegment, sizeof(PreviousSegment));

    PathTraversalData TraversalData;
    BeginPathTraversal(path, &TraversalData);
    do
    {
        PreviousSegment = TraversalData._currentSegment;
        TraverseSegment(errorPool, &TraversalData);
    } while (!TraversalData._currentSegment._isFinalSegment);

    size_t PathLength = PreviousSegment._startIndexInPath + PreviousSegment._sizeBytes;
    StringUTF8_Substring(path, 0, PathLength, result, errorPool);
    return Error_CreateSuccess();
}


/* Platform-specific. */

#if defined LINUX_PATH_IMPL
bool Path_IsRooted(const unsigned char* path)
{
    return path[0] == ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
}

bool Path_IsFullyQualified(const unsigned char* path)
{
    return Path_IsRooted(path);
}

Error Path_GetRoot(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result)
{
    if (!Path_IsRooted(path))
    {
        return Error_Construct3(errorPool,
            ErrorCode_InvalidPath,
            u8"Cannot get root of path because it is not rooted.");
    }
    return StringUTF8_Substring(path, 0, 1, result, errorPool);
}

#elif defined WINDOWS_PATH_IMPL
#define VOLUME_SEPARATOR ':'
#define SHARE_DRIVE_SEPARATOR '$'

static bool GetUNCPathLength(const unsigned char* path, size_t* length)
{
    *length = 0;

    size_t Index = 0;
    if ((path[Index] != ENVIRONMENT_PATH_SEPARATOR_PRIMARY) || (path[Index + 1] != ENVIRONMENT_PATH_SEPARATOR_PRIMARY))
    {
        return false;
    }
    Index += 2;

    size_t HostNameLength = 0;
    while ((path[Index] != '\0') && (path[Index] != ENVIRONMENT_PATH_SEPARATOR_PRIMARY))
    {
        size_t CharSize = CharUTF8_GetByteCountChar(path + Index);
        if (CharSize == 0)
        {
            return false;
        }
        Index += CharSize;
        HostNameLength += CharSize;
    }
    if ((HostNameLength == 0) || (path[Index] == '\0'))
    {
        return false;
    }
    Index++;

    size_t ShareNameLength = 0;
    bool IsDriveName = false;
    while ((path[Index] != '\0') && (path[Index] != ENVIRONMENT_PATH_SEPARATOR_PRIMARY))
    {
        if (IsDriveName && (!IsDriveLetter(path[Index]) || (ShareNameLength > 1)))
        {
            return false;
        }
        IsDriveName |= (path[Index] == SHARE_DRIVE_SEPARATOR);

        size_t CharSize = CharUTF8_GetByteCountChar(path + Index);
        if (CharSize == 0)
        {
            return false;
        }
        Index += CharSize;
        ShareNameLength += CharSize;
    }
    if (ShareNameLength == 0)
    {
        return false;
    }

    if (path[Index] == ENVIRONMENT_PATH_SEPARATOR_PRIMARY)
    {
        Index++;
    }
    *length = Index;
    return true;
}

static inline bool IsDriveLetter(unsigned char letter)
{
    return (('a' <= letter) && (letter <= 'z')) || (('A' <= letter) && (letter <= 'Z'));
}

static bool GetTradDOSPathLength(const unsigned char* path, size_t* length)
{
    *length = 0;
    size_t Index = 0;
    if (!IsDriveLetter(path[Index]))
    {
        return false;
    }

    Index++;
    if (path[Index] != VOLUME_SEPARATOR)
    {
        return false;
    }

    Index++;
    if (IsSeparator(path[Index]))
    {
        Index++;
    }
    *length = Index;
    return true;
}

static bool GetAbsRootLength(const unsigned char* path, size_t* length)
{
    if (path[0] == ENVIRONMENT_PATH_SEPARATOR_PRIMARY)
    {
        return GetUNCPathLength(path, length);
    }
    return GetTradDOSPathLength(path, length);
}

bool Path_IsRooted(const unsigned char* path)
{
    bool IsFullyQualified = Path_IsFullyQualified(path);
    return IsFullyQualified || IsSeparator(path[0]);
}

bool Path_IsFullyQualified(const unsigned char* path)
{
    size_t RootLength;
    return GetAbsRootLength(path, &RootLength);
}

Error Path_GetRoot(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result)
{
    size_t RootLength;
    if (GetAbsRootLength(path, &RootLength))
    {
        return StringUTF8_Substring(path, 0, RootLength, result, errorPool);
    }

    if (!IsSeparator(path[0]))
    {
        return Error_Construct3(errorPool,
            ErrorCode_IllegalArgument,
            u8"The given path does not have a root.");
    }

    GenericBuffer_WriteUChar(result, path[0]);
    if (!GenericBuffer_TryNullTerminate(result))
    {
        return CreateDestBufferTooSmallError(errorPool);
    }
    return Error_CreateSuccess();
}

#endif