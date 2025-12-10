#include "WRPath.h"
#include "WRMemory.h"
#include "WRError.h"
#include <stddef.h>
#include "WRUnicode.h"
#include "WREnvironment.h"
#include "WRChar.h"
#include "WRString.h"


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
        TraverseSegment(errorPool, &TraversalData);
    } while (!TraversalData._currentSegment._isFinalSegment);

    *lastSegment = TraversalData._currentSegment;
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


#if defined __linux__


#elif defined _WIN32


#endif