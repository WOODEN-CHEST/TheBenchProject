#include "WRPath.h"
#include "WRMemory.h"
#include "WRError.h"
#include <stddef.h>
#include "WRUnicode.h"
#include "WREnvironment.h"
#include "WRChar.h"
#include "WRString.h"


// DOS paths in the style "\\.\C:\Test\Foo.txt" for Windows are not supported.

#if defined __linux__
#define LINUX_PATH_IMPL
#elif defined _WIN32
#define WINDOWS_PATH_IMPL
#endif


// Types.
typedef struct PathSegmentExtensionStruct
{
    const unsigned char* _value;
    size_t _startIndexInIdentifier;
} PathIdentifierExtension;

typedef struct PathIdentifierStruct
{
    const unsigned char* _entry;
    PathIdentifierExtension _extensionLast;
    PathIdentifierExtension _extensionFull;
    size_t _identifierIndex;
    size_t _sizeBytes;
    size_t _startIndexInPath;
    bool _isFinalIdentifier;
} PathIdentifier;

typedef enum PathRootTypeEnum
{
    PathRootType_None,
    PathRootType_Unix,
    PathRootType_WinDriveAbsolute,
    PathRootType_WinDriveRelative,
    PathRootType_WinUNC,
} PathRootType;

typedef struct PathRootStruct
{
    size_t _sizeBytes;
} PathRoot;

typedef struct PathTraversalDataStruct
{
    const unsigned char* _path;
    PathIdentifier _currentIdentifier;
    size_t _nextSegmentIndex;
    const unsigned char** _nextSegmentStart;
    PathRootType _rootType;
} PathTraversalData;


// Fields.
static const unsigned char EXTENSION_INDICATOR = '.';


// OS-specific.
static PathRootType TryTraverseSegmentAsRoot(PathTraversalData* traversalData, PathIdentifier* segment, size_t* segmentStartIndex);

static unsigned char ASCIICharToLower(CodePoint codePoint)
{
    if (('A' <= codePoint) && (codePoint <= 'A'))
    {
        return codePoint + ('a' - 'A');
    }
    return codePoint;
}

#if defined LINUX_PATH_IMPL
static const unsigned char* const ILLEGAL_FILENAMES[] = 
{
    u8".", u8".."
};

static const CodePoint ILLEGAL_CHARACTERS[] = { };

static size_t SkipUntilIdentifierStart(const unsigned char* str);

static PathRootType TryTraverseSegmentAsRoot(PathTraversalData* traversalData, PathIdentifier* segment, size_t* segmentStartIndex)
{
    const unsigned char* SegmentStr = traversalData->_nextSegmentStart;
    size_t SkippedSeparatorCount = SkipUntilIdentifierStart(SegmentStr);
    if (SkippedSeparatorCount > 0)
    {
        segment->_entry = SegmentStr;
        segment->_isFinalIdentifier = SegmentStr[SkippedSeparatorCount] == '\0';
        segment->_sizeBytes = SkippedSeparatorCount;
        segment->_startIndexInPath = 0;

        *segmentStartIndex = SkippedSeparatorCount;

        traversalData->_nextSegmentStart = SegmentStr + SkippedSeparatorCount;

        return PathRootType_Unix;
    }
    *segmentStartIndex = 0;
    return PathRootType_None;
}


#elif defined WINDOWS_PATH_IMPL
static const unsigned char* const ILLEGAL_FILENAMES[] = 
{
    u8"CON", u8"PRN", "AUX", "NUL",
    u8"COM1", u8"COM2", u8"COM3", u8"COM4", u8"COM5", u8"COM6", u8"COM7", u8"COM8", u8"COM9",
    u8"COM¹", u8"COM²", u8"COM³",
    u8"LPT1", u8"LPT2", u8"LPT3", u8"LPT4", u8"LPT5", u8"LPT6", u8"LPT7", u8"LPT8", u8"LPT9",
    u8"LPT¹", u8"LPT²", u8"LPT³"
}

static const CodePoint ILLEGAL_CHARACTERS[] = 
{
    '<', '>', ':', '"', '|', '?', '*',
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
}
#endif


// Static functions.
static Error CreateDestBufferTooSmallError(ErrorMessagePool* errorPool)
{
    return Error_Construct3(errorPool,
        ErrorCode_BufferTooSmall,
        u8"Destination buffer is too small to hold the given path.");
}

static inline bool IsSeparator(CodePoint codePoint)
{
    return (codePoint == ENVIRONMENT_PATH_SEPARATOR_PRIMARY) || (codePoint == ENVIRONMENT_PATH_SEPARATOR_SECONDARY);
}

static size_t SkipUntilIdentifierStart(const unsigned char* str)
{
    size_t Index = 0;
    while ((str[Index] != '\0') && IsSeparator(str[Index]))
    {
        Index++;
    }
    return Index;
}

static void BeginPathTraversal(const unsigned char* path, PathTraversalData* data)
{
    Memory_Zero(data, sizeof(data));
    data->_nextSegmentStart = path;
    data->_path = path;
}

static bool IsIllegalCharacter(CodePoint charCodePoint)
{
    for (size_t i = 0; i < (sizeof(ILLEGAL_CHARACTERS) / sizeof(*ILLEGAL_CHARACTERS)); i++)
    {
        if (ILLEGAL_CHARACTERS[i] == charCodePoint)
        {
            return true;
        }
    }
    return false;
}

static Error TraverseIdentifier(ErrorMessagePool* errorPool,
    PathTraversalData* traversalData,
    PathIdentifier* segment,
    const unsigned char* identifierStart)
{
    size_t Index = 0;
    segment->_entry = identifierStart;

    while ((identifierStart[Index] != '\0') && !IsSeparator(identifierStart[Index]))
    {
        if (identifierStart[Index] == EXTENSION_INDICATOR)
        {
            PathIdentifierExtension CurrentExtension = (PathIdentifierExtension)
            {
                ._value = identifierStart + Index,
                ._startIndexInIdentifier = Index 
            };

            if (!segment->_extensionFull._value)
            {
                segment->_extensionFull = CurrentExtension;
            }
            segment->_extensionLast = CurrentExtension;
        }

        size_t CharSize = CharUTF8_GetByteCountChar(identifierStart + Index);
        if (CharSize == 0)
        {
            return Error_Construct3(errorPool,
                ErrorCode_InvalidTextEncoding,
                u8"Found invalid character data in the path segment at segment index %zu and "
                "character index %zu; the byte has a value of %d.",
                traversalData->_nextSegmentIndex, Index, (int)identifierStart[Index]);
        }

        Index += CharSize;
    }

    segment->_isFinalIdentifier = (identifierStart[Index] == '\0');
    segment->_sizeBytes = Index;
    traversalData->_nextSegmentStart = identifierStart + Index;
    return Error_CreateSuccess();
}

static Error TraverseSegment(ErrorMessagePool* errorPool, PathTraversalData* traversalData)
{
    PathIdentifier CurrentIdentifier;
    Memory_Zero(&CurrentIdentifier, sizeof(CurrentIdentifier));
    CurrentIdentifier._identifierIndex = traversalData->_nextSegmentIndex;

    bool WasRootTraversed = false;
    if (traversalData->_rootType == PathRootType_None)
    {
        size_t IdentifierStartIndexInSegment;

        PathRootType RootType = TryTraverseSegmentAsRoot(traversalData,
            &CurrentIdentifier,
            &IdentifierStartIndexInSegment);
        
        if (RootType != PathRootType_None)
        {
            WasRootTraversed = true;
            traversalData->_rootType = RootType;
        }
    }
    
    if (!WasRootTraversed)
    {
        size_t SkipAmount = SkipUntilIdentifierStart(traversalData->_nextSegmentStart);
        TraverseIdentifier(errorPool, traversalData, &CurrentIdentifier, traversalData->_nextSegmentStart + SkipAmount);
    }

    traversalData->_nextSegmentIndex++;
    traversalData->_currentIdentifier = CurrentIdentifier;

    return Error_CreateSuccess();
}

static Error GetLastSegment(ErrorMessagePool* errorPool, const unsigned char* path, PathIdentifier* lastSegment)
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
    } while (!TraversalData._currentIdentifier._isFinalIdentifier);

    *lastSegment = TraversalData._currentIdentifier;
    return Error_CreateSuccess();
}

static bool IsEntryNameValidAgainst(const unsigned char* entryName, const unsigned char* illegalName)
{
    size_t IllegalNameIndex = 0;
    for (size_t i = 0; (entryName[i] != '\0') && (illegalName[i] != '\0') && !IsSeparator(entryName + i);)
    {
        CodePoint EntryCodePoint = CharUTF8_GetCodePoint(entryName + i);
        CodePoint IllegalCodePoint = CharUTF8_GetCodePoint(entryName + i);
        if ((EntryCodePoint == CODEPOINT_NONE) || (IllegalCodePoint == CODEPOINT_NONE))
        {
            return false;
        }

        if (ASCIICharToLower(EntryCodePoint) != ASCIICharToLower(IllegalCodePoint))
        {
            return true;
        }

        IllegalNameIndex += CharUTF8_GetByteCountCodepoint(IllegalCodePoint);
        if (illegalName[IllegalNameIndex] == '\0')
        {
            return false;
        }
        i += CharUTF8_GetByteCountCodepoint(EntryCodePoint);
    }
    return true;
}


// Functions.
Error Path_ChangeExtension(ErrorMessagePool* errorPool, const unsigned char* path, const unsigned char* newExtension, GenericBuffer* result)
{
    PathIdentifier LastSegment;
    Error ErrorResult = GetLastSegment(errorPool, path, &LastSegment);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    size_t PathSizeWithoutLastExtension = LastSegment._sizeBytes;
    if (LastSegment._extensionLast._value)
    {
        PathSizeWithoutLastExtension = LastSegment._extensionLast._startIndexInIdentifier;
    }
    
    if (!GenericBuffer_WriteStringBySize(path, path, PathSizeWithoutLastExtension))
    {
        return CreateDestBufferTooSmallError(errorPool);
    }

    if (!StringUTF8_IsNullOrEmpty(newExtension))
    {
        if ((newExtension[0] != EXTENSION_INDICATOR) && !GenericBuffer_WriteUChar(result, EXTENSION_INDICATOR))
        {
            return CreateDestBufferTooSmallError(errorPool);
        }
        if (!GenericBuffer_WriteString(result, newExtension))
        {
            return CreateDestBufferTooSmallError(errorPool);
        }
    }

    if (!GenericBuffer_TryNullTerminate(result))
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
    PathIdentifier LastSegment;
    *extension = NULL;
    Error ErrorResult = GetLastSegment(errorPool, path, &LastSegment);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    *extension = LastSegment._extensionLast._value;
    return Error_CreateSuccess();
}

Error Path_HasExtension(ErrorMessagePool* errorPool, const unsigned char* path, bool* hasExtension)
{
    const unsigned char* Extension = NULL;
    Error ErrorResult = Path_GetExtension(errorPool, path, &Extension);
    *hasExtension = Extension != NULL;
    return ErrorResult;
}

Error Path_Combine(ErrorMessagePool* errorPool, const unsigned char** paths, size_t pathCount, GenericBuffer* result)
{
    bool IsAnythingWritten = false;
    bool EndsWithSeparatorPrevious = false;

    for (size_t i = 0; i < pathCount; i++)
    {
        const unsigned char* TargetPath = paths[i];
        size_t PathLength = StringUTF8_GetByteLength(TargetPath);
        if (PathLength == 0)
        {
            continue;
        }

        if (Path_IsRooted(TargetPath) && IsAnythingWritten)
        {
            return Error_Construct3(errorPool,
                ErrorCode_IllegalArgument,
                u8"Cannot append path; found rooted path \"%s\" at index %zu, it should've been the first non-empty path.",
                TargetPath, i);
        }

        bool StartWithSeparatorCurrent = IsSeparator(TargetPath[0]);

        if (!StartWithSeparatorCurrent && !EndsWithSeparatorPrevious
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

    if (!GenericBuffer_TryNullTerminate(result))
    {
        return CreateDestBufferTooSmallError(errorPool);
    }

    return Error_CreateSuccess();
}

Error Path_Append(ErrorMessagePool* errorPool, const unsigned char* pathA, const unsigned char* pathB, GenericBuffer* result)
{
    const unsigned char* PathArray[] = { pathA, pathB };
    return Path_Combine(errorPool, PathArray, sizeof(PathArray) / sizeof(*PathArray), result);
}

bool Path_EndsInDirectorySeparator(const unsigned char* path)
{
    size_t StrLength = StringUTF8_GetByteLength(path);
    return (StrLength > 0) && IsSeparator(path[StrLength - 1]);
}

Error Path_GetParentPath(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result)
{
    PathIdentifier PreviousIdentifier;
    Memory_Zero(&PreviousIdentifier, sizeof(PreviousIdentifier));

    PathTraversalData TraversalData;
    BeginPathTraversal(path, &TraversalData);
    do
    {
        PreviousIdentifier = TraversalData._currentIdentifier;
        TraverseSegment(errorPool, &TraversalData);
    } while (!TraversalData._currentIdentifier._isFinalIdentifier);

    size_t PathLength = PreviousIdentifier._startIndexInPath + PreviousIdentifier._sizeBytes;
    return StringUTF8_Substring(path, 0, PathLength, result, errorPool);
}

Error Path_GetLastEntryName(ErrorMessagePool* errorPool, const unsigned char* path, const unsigned char** lastEntryName)
{
    *lastEntryName = NULL;
    PathTraversalData TraversalData;
    BeginPathTraversal(path, &TraversalData);
    do
    {
        Error ErrorResult = TraverseSegment(errorPool, &TraversalData);
        if (ErrorResult.Code != ErrorCode_Success)
        {
            return ErrorResult;
        }
    } while (TraversalData._currentIdentifier._isFinalIdentifier);

    *lastEntryName = TraversalData._currentIdentifier._entry;

    return Error_CreateSuccess();
}

Error Path_GetLastEntryStem(ErrorMessagePool* errorPool, const unsigned char* path, GenericBuffer* result)
{
    const unsigned char* LastEntryName = NULL;
    Error ErrorResult = Path_GetLastEntryName(errorPool, path, &LastEntryName);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    const unsigned char Indicator[] = { EXTENSION_INDICATOR, '\0'};
    size_t FirstIndicatorIndex;
    ErrorResult = StringUTF8_IndexOf(path, Indicator, String_CreateIndexOptionsNormal(), errorPool, &FirstIndicatorIndex);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    if (FirstIndicatorIndex != STRING_INDEX_INVALID)
    {
        return StringUTF8_Substring(LastEntryName, 0, FirstIndicatorIndex, result, errorPool);
    }
    if (!GenericBuffer_WriteString(result, LastEntryName) || !GenericBuffer_TryNullTerminate(result))
    {
        return CreateDestBufferTooSmallError(errorPool);
    }

    return Error_CreateSuccess();
}

bool Path_IsEntryNameValid(const unsigned char* entryName)
{
    return Path_ValidateEntryName(NULL, entryName).Code == ErrorCode_Success;
}

Error Path_ValidateEntryName(ErrorMessagePool* errorPool, const unsigned char* entryName)
{
    const ErrorCode FailCode = ErrorCode_IllegalArgument;

    for (size_t i = 0; entryName[i] != '\0';)
    {
        if (IsIllegalCharacter(entryName + i))
        {
            return Error_Construct3(errorPool,
                FailCode,
                u8"Found illegal character %d at index %zu.",
                entryName[i], i);
        }
    }

    const size_t IllegalNameCount = sizeof(ILLEGAL_FILENAMES) / sizeof(*ILLEGAL_FILENAMES);
    for (size_t IllegalNameIndex = 0; IllegalNameIndex < ILLEGAL_FILENAMES; IllegalNameIndex++)
    {
        const unsigned char* IllegalName = ILLEGAL_FILENAMES[IllegalNameIndex];
        if (!IsEntryNameValidAgainst(entryName, IllegalName))
        {
            return Error_Construct3(errorPool,
                FailCode,
                u8"The entry name \"%s\" or its variations is illegal.",
                IllegalName);
        }
    }

    return Error_CreateSuccess();
}