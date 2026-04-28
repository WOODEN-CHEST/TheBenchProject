#include "WRPath.h"
#include "WRChar.h"
#include "WRString.h"
#include "WREnvironment.h"
#include <stdint.h>


// Macros.
#define PATH_WINDOWS_UNC_PREFIX_SEPARATOR_COUNT 2


// Types.
typedef enum PathRootKindEnum
{
    PathRootKind_None,
    PathRootKind_UnixRoot,
    PathRootKind_WindowsCurrentDrive,
    PathRootKind_WindowsDriveRelative,
    PathRootKind_WindowsDriveAbsolute,
    PathRootKind_WindowsUNC,
    PathRootKind_WindowsUnsupportedDevice,
} PathRootKind;

typedef struct PathRootInfoStruct
{
    PathRootKind Kind;
    size_t Length;
    bool IsRooted;
    bool IsFullyQualified;
} PathRootInfo;

typedef struct PathSegmentViewStruct
{
    const unsigned char* Start;
    size_t Length;
} PathSegmentView;


// Fields.
#if defined(_WIN32)
static const unsigned char* const WINDOWS_RESERVED_NAMES[] =
{
    u8"CON",
    u8"PRN",
    u8"AUX",
    u8"NUL",
    u8"COM1",
    u8"COM2",
    u8"COM3",
    u8"COM4",
    u8"COM5",
    u8"COM6",
    u8"COM7",
    u8"COM8",
    u8"COM9",
    u8"LPT1",
    u8"LPT2",
    u8"LPT3",
    u8"LPT4",
    u8"LPT5",
    u8"LPT6",
    u8"LPT7",
    u8"LPT8",
    u8"LPT9",
};
#endif


// Static functions.
static size_t GetStringLength(const unsigned char* text)
{
    return StringUTF8_GetByteLength(text);
}

static bool IsDirectorySeparatorByte(unsigned char character)
{
    return (character == ENVIRONMENT_PATH_SEPARATOR_PRIMARY) || (character == ENVIRONMENT_PATH_SEPARATOR_SECONDARY);
}

#if defined(_WIN32)
static bool IsAsciiLetter(unsigned char character)
{
    return (((character >= u8'a') && (character <= u8'z'))
        || ((character >= u8'A') && (character <= u8'Z')));
}

static unsigned char ToAsciiLower(unsigned char character)
{
    if ((character >= u8'A') && (character <= u8'Z'))
    {
        return (unsigned char)(character + (u8'a' - u8'A'));
    }

    return character;
}
#endif

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Path argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateEmptyPathError(void)
{
    return Error_Construct1(ErrorCode_InvalidPath,
        u8"Path arguments must not be empty.");
}

static Error CreateBufferTypeError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Path argument \"%s\" must be a byte buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreatePointerBufferTypeError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Path argument \"%s\" must be a pointer buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreateOverflowError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Cannot %s because the required size exceeds the supported range.",
        operationName);
}

static Error CreateInvalidPathError(const unsigned char* path, const unsigned char* reason)
{
    return Error_Construct3(ErrorCode_InvalidPath,
        u8"Invalid path \"%s\": %s",
        path,
        reason);
}

static Error CreateInvalidEntryNameError(const unsigned char* entryName, const unsigned char* reason)
{
    return Error_Construct3(ErrorCode_InvalidPath,
        u8"Invalid path entry name \"%s\": %s",
        entryName,
        reason);
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

static Error ValidatePointerBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    if (buffer == NULL)
    {
        return CreateNullArgumentError(argumentName);
    }
    if (buffer->_elementSize != sizeof(unsigned char*))
    {
        return CreatePointerBufferTypeError(argumentName, buffer->_elementSize);
    }

    return Error_CreateSuccess();
}

static Error PrepareByteBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    return ValidateByteBuffer(buffer, argumentName);
}

static Error EnsureByteBufferCapacity(GenericBuffer* buffer, size_t requiredCapacity, const unsigned char* operationName)
{
    size_t AddedElementCount = 0;

    if (requiredCapacity > buffer->_count)
    {
        AddedElementCount = requiredCapacity - buffer->_count;
    }

    if (!GenericBuffer_TryPrepareForManualMutation(buffer, AddedElementCount))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Cannot %s because the destination buffer requires at least %zu bytes of capacity.",
            operationName,
            requiredCapacity);
    }

    return Error_CreateSuccess();
}

static Error WriteBufferBytes(GenericBuffer* buffer,
    const unsigned char* bytes,
    size_t length,
    bool nullTerminate,
    const unsigned char* operationName)
{
    if (nullTerminate)
    {
        return StringUTF8_CopyToBySize(bytes, length, buffer);
    }

    if (length == 0)
    {
        return Error_CreateSuccess();
    }

    if (bytes == NULL)
    {
        return CreateNullArgumentError(u8"bytes");
    }
    if (buffer->_count > (SIZE_MAX - length))
    {
        return Error_Construct1(ErrorCode_ArgumentOutOfRange,
            u8"Cannot write the path bytes because the required size exceeds the supported range.");
    }
    if (!GenericBuffer_EnsureTotalCapacity(buffer, buffer->_count + length))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Cannot %s because the destination buffer requires at least %zu bytes of capacity.",
            operationName,
            buffer->_count + length);
    }
    if (!GenericBuffer_AddLastRange(buffer, (void*)bytes, length))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Cannot %s because the destination buffer requires at least %zu bytes of capacity.",
            operationName,
            buffer->_count + length);
    }

    return Error_CreateSuccess();
}

static bool IsSpecialDirectorySegment(const unsigned char* start, size_t length)
{
    if ((start == NULL) || (length == 0))
    {
        return false;
    }

    if ((length == 1) && (start[0] == u8'.'))
    {
        return true;
    }

    return (length == 2) && (start[0] == u8'.') && (start[1] == u8'.');
}

static bool IsParentDirectorySegment(const unsigned char* start, size_t length)
{
    return (length == 2) && (start[0] == u8'.') && (start[1] == u8'.');
}

static bool IsCurrentDirectorySegment(const unsigned char* start, size_t length)
{
    return (length == 1) && (start[0] == u8'.');
}

static bool SegmentHasExtension(const unsigned char* start, size_t length, size_t* extensionIndex)
{
    size_t DotIndex = SIZE_MAX;

    if ((start == NULL) || (length == 0))
    {
        return false;
    }

    for (size_t Index = 0; Index < length; Index++)
    {
        if (start[Index] == u8'.')
        {
            DotIndex = Index;
        }
    }

    if ((DotIndex == SIZE_MAX) || (DotIndex == 0) || (DotIndex == (length - 1)))
    {
        return false;
    }

    if (extensionIndex != NULL)
    {
        *extensionIndex = DotIndex;
    }

    return true;
}

static size_t GetTrimmedPathLength(const unsigned char* path, size_t rootLength)
{
    size_t Length = GetStringLength(path);

    while ((Length > rootLength) && IsDirectorySeparatorByte(path[Length - 1]))
    {
        Length--;
    }

    return Length;
}

static PathRootInfo GetPathRootInfo(const unsigned char* path)
{
    PathRootInfo Result =
    {
        .Kind = PathRootKind_None,
        .Length = 0,
        .IsRooted = false,
        .IsFullyQualified = false,
    };

    if ((path == NULL) || (path[0] == 0))
    {
        return Result;
    }

#if defined(__linux__)
    if (IsDirectorySeparatorByte(path[0]))
    {
        Result.Kind = PathRootKind_UnixRoot;
        Result.Length = 1;
        Result.IsRooted = true;
        Result.IsFullyQualified = true;
    }
#elif defined(_WIN32)
    if ((path[0] == u8'\\')
        && (path[1] == u8'\\')
        && (((path[2] == u8'.') || (path[2] == u8'?')))
        && (path[3] == u8'\\'))
    {
        Result.Kind = PathRootKind_WindowsUnsupportedDevice;
        Result.Length = 4;
        Result.IsRooted = true;
        Result.IsFullyQualified = false;
        return Result;
    }

    if (IsAsciiLetter(path[0]) && (path[1] == u8':'))
    {
        Result.Length = 2;
        if (IsDirectorySeparatorByte(path[2]))
        {
            Result.Kind = PathRootKind_WindowsDriveAbsolute;
            Result.Length = 3;
            Result.IsRooted = true;
            Result.IsFullyQualified = true;
            return Result;
        }

        Result.Kind = PathRootKind_WindowsDriveRelative;
        Result.IsRooted = false;
        Result.IsFullyQualified = false;
        return Result;
    }

    if (IsDirectorySeparatorByte(path[0]))
    {
        if (IsDirectorySeparatorByte(path[1]))
        {
            size_t Index = PATH_WINDOWS_UNC_PREFIX_SEPARATOR_COUNT;
            bool HasServer = false;
            bool HasShare = false;

            while ((path[Index] != 0) && !IsDirectorySeparatorByte(path[Index]))
            {
                HasServer = true;
                Index++;
            }
            while (IsDirectorySeparatorByte(path[Index]))
            {
                Index++;
            }
            while ((path[Index] != 0) && !IsDirectorySeparatorByte(path[Index]))
            {
                HasShare = true;
                Index++;
            }
            while (IsDirectorySeparatorByte(path[Index]))
            {
                Index++;
            }

            Result.Kind = PathRootKind_WindowsUNC;
            Result.Length = Index;
            Result.IsRooted = true;
            Result.IsFullyQualified = HasServer && HasShare;
            return Result;
        }

        Result.Kind = PathRootKind_WindowsCurrentDrive;
        Result.Length = 1;
        Result.IsRooted = true;
        Result.IsFullyQualified = false;
    }
#endif

    return Result;
}

static bool PathContainsInvalidUtf8(const unsigned char* path)
{
    size_t Index = 0;

    if (path == NULL)
    {
        return true;
    }

    while (path[Index] != 0)
    {
        size_t CharacterByteCount = 0;

        if (!CharUTF8_IsCharValid(path + Index))
        {
            return true;
        }

        CharacterByteCount = CharUTF8_GetByteCountChar(path + Index);
        if (CharacterByteCount == 0)
        {
            return true;
        }

        Index += CharacterByteCount;
    }

    return false;
}

#if defined(_WIN32)
static bool IsWindowsReservedName(const unsigned char* start, size_t length)
{
    size_t CompareLength = length;

    for (size_t Index = 0; Index < length; Index++)
    {
        if (start[Index] == u8'.')
        {
            CompareLength = Index;
            break;
        }
    }

    if (CompareLength == 0)
    {
        return false;
    }

    for (size_t Index = 0; Index < (sizeof(WINDOWS_RESERVED_NAMES) / sizeof(WINDOWS_RESERVED_NAMES[0])); Index++)
    {
        const unsigned char* Candidate = WINDOWS_RESERVED_NAMES[Index];
        size_t CandidateLength = GetStringLength(Candidate);
        bool Matches = CandidateLength == CompareLength;

        if (!Matches)
        {
            continue;
        }

        for (size_t CharacterIndex = 0; CharacterIndex < CompareLength; CharacterIndex++)
        {
            if (ToAsciiLower(start[CharacterIndex]) != ToAsciiLower(Candidate[CharacterIndex]))
            {
                Matches = false;
                break;
            }
        }

        if (Matches)
        {
            return true;
        }
    }

    return false;
}
#endif

static Error ValidateEntrySegment(const unsigned char* start, size_t length, bool allowDirectorySegments)
{
    if (length == 0)
    {
        return Error_Construct1(ErrorCode_InvalidPath,
            u8"Path segments must not be empty.");
    }

    if (allowDirectorySegments && IsSpecialDirectorySegment(start, length))
    {
        return Error_CreateSuccess();
    }

    if ((length == 1) && (start[0] == u8'.'))
    {
        return Error_Construct1(ErrorCode_InvalidPath,
            u8"Path entry names must not be \".\".");
    }
    if ((length == 2) && (start[0] == u8'.') && (start[1] == u8'.'))
    {
        return Error_Construct1(ErrorCode_InvalidPath,
            u8"Path entry names must not be \"..\".");
    }

    for (size_t Index = 0; Index < length; Index++)
    {
        unsigned char Character = start[Index];

        if (Character == 0)
        {
            return Error_Construct1(ErrorCode_InvalidPath,
                u8"Path entry names must not contain null bytes.");
        }
        if (IsDirectorySeparatorByte(Character))
        {
            return Error_Construct1(ErrorCode_InvalidPath,
                u8"Path entry names must not contain directory separators.");
        }

#if defined(_WIN32)
        if ((Character < 32)
            || (Character == u8'<')
            || (Character == u8'>')
            || (Character == u8':')
            || (Character == u8'"')
            || (Character == u8'|')
            || (Character == u8'?')
            || (Character == u8'*'))
        {
            return Error_Construct1(ErrorCode_InvalidPath,
                u8"Windows path entry names contain a reserved character.");
        }
#endif
    }

#if defined(_WIN32)
    if ((start[length - 1] == u8' ') || (start[length - 1] == u8'.'))
    {
        return Error_Construct1(ErrorCode_InvalidPath,
            u8"Windows path entry names must not end with a space or dot.");
    }
    if (IsWindowsReservedName(start, length))
    {
        return Error_Construct1(ErrorCode_InvalidPath,
            u8"Windows path entry names must not use reserved device names.");
    }
#endif

    return Error_CreateSuccess();
}

static bool TryGetNextSegmentView(const unsigned char* path, size_t* index, PathSegmentView* outSegment)
{
    size_t SegmentStart = 0;

    if ((path == NULL) || (index == NULL) || (outSegment == NULL))
    {
        return false;
    }

    while (IsDirectorySeparatorByte(path[*index]))
    {
        (*index)++;
    }
    if (path[*index] == 0)
    {
        outSegment->Start = NULL;
        outSegment->Length = 0;
        return false;
    }

    SegmentStart = *index;
    while ((path[*index] != 0) && !IsDirectorySeparatorByte(path[*index]))
    {
        (*index)++;
    }

    outSegment->Start = path + SegmentStart;
    outSegment->Length = *index - SegmentStart;
    return true;
}

static Error ValidatePathRootRules(const unsigned char* path, PathRootInfo rootInfo)
{
#if !defined(_WIN32)
    (void)path;
    (void)rootInfo;
#endif

#if defined(_WIN32)
    if (rootInfo.Kind == PathRootKind_WindowsUnsupportedDevice)
    {
        return CreateInvalidPathError(path, u8"Windows device paths beginning with \\\\.\\ or \\\\?\\ are not supported.");
    }
    if ((rootInfo.Kind == PathRootKind_WindowsUNC) && !rootInfo.IsFullyQualified)
    {
        return CreateInvalidPathError(path, u8"UNC paths must include both a server and share name.");
    }
#endif

    return Error_CreateSuccess();
}

static Error ValidatePathSegmentViews(const unsigned char* path, size_t startIndex)
{
    size_t Index = startIndex;
    PathSegmentView Segment;

    while (TryGetNextSegmentView(path, &Index, &Segment))
    {
        Error Result = ValidateEntrySegment(Segment.Start, Segment.Length, true);

        if (Result.Code != ErrorCode_Success)
        {
            Error MappedError = CreateInvalidPathError(path,
                (Result.Message != NULL) ? Result.Message : u8"path contains an invalid segment.");
            Error_Deconstruct(&Result);
            return MappedError;
        }
    }

    return Error_CreateSuccess();
}

static Error ValidatePathInternal(const unsigned char* path)
{
    PathRootInfo RootInfo;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (PathContainsInvalidUtf8(path))
    {
        return CreateInvalidPathError(path, u8"path text is not valid UTF-8.");
    }

    RootInfo = GetPathRootInfo(path);
    Result = ValidatePathRootRules(path, RootInfo);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return ValidatePathSegmentViews(path, RootInfo.Length);
}

static size_t CountSegments(const unsigned char* path)
{
    PathRootInfo RootInfo = GetPathRootInfo(path);
    size_t Index = RootInfo.Length;
    size_t Count = 0;
    PathSegmentView Segment;

    while (TryGetNextSegmentView(path, &Index, &Segment))
    {
        Count++;
    }

    return Count;
}

static size_t GetLastEntryStart(const unsigned char* path, size_t trimmedLength, size_t rootLength)
{
    size_t StartIndex = trimmedLength;

    while ((StartIndex > rootLength) && !IsDirectorySeparatorByte(path[StartIndex - 1]))
    {
        StartIndex--;
    }

    return StartIndex;
}

static bool TryGetLastEntryView(const unsigned char* path, PathSegmentView* outView)
{
    PathRootInfo RootInfo = GetPathRootInfo(path);
    size_t TrimmedLength = GetTrimmedPathLength(path, RootInfo.Length);
    size_t StartIndex = 0;

    if (outView == NULL)
    {
        return false;
    }

    outView->Start = NULL;
    outView->Length = 0;
    if (TrimmedLength <= RootInfo.Length)
    {
        return true;
    }

    StartIndex = GetLastEntryStart(path, TrimmedLength, RootInfo.Length);
    outView->Start = path + StartIndex;
    outView->Length = TrimmedLength - StartIndex;
    return true;
}

static size_t GetParentPathLength(const unsigned char* path)
{
    PathRootInfo RootInfo = GetPathRootInfo(path);
    size_t TrimmedLength = GetTrimmedPathLength(path, RootInfo.Length);

    if (TrimmedLength <= RootInfo.Length)
    {
        return RootInfo.Length;
    }

    while ((TrimmedLength > RootInfo.Length) && !IsDirectorySeparatorByte(path[TrimmedLength - 1]))
    {
        TrimmedLength--;
    }
    while ((TrimmedLength > RootInfo.Length) && IsDirectorySeparatorByte(path[TrimmedLength - 1]))
    {
        TrimmedLength--;
    }

    if ((TrimmedLength == 0) && (RootInfo.Length > 0))
    {
        return RootInfo.Length;
    }

    return TrimmedLength;
}

static bool PathsEqualNormalized(const unsigned char* leftPath, const unsigned char* rightPath)
{
    size_t Index = 0;

    while ((leftPath[Index] != 0) && (rightPath[Index] != 0))
    {
#if defined(_WIN32)
        if (ToAsciiLower(leftPath[Index]) != ToAsciiLower(rightPath[Index]))
        {
            return false;
        }
#else
        if (leftPath[Index] != rightPath[Index])
        {
            return false;
        }
#endif
        Index++;
    }

    return (leftPath[Index] == 0) && (rightPath[Index] == 0);
}

static bool HasTrailingSeparatorPastRoot(const unsigned char* path, PathRootInfo rootInfo)
{
    size_t PathLength = GetStringLength(path);
    size_t TrimmedLength = GetTrimmedPathLength(path, rootInfo.Length);

    return (TrimmedLength < PathLength) && (TrimmedLength > 0);
}

static unsigned char GetNormalizationSeparator(const unsigned char* path, bool fixSeparators)
{
    if (fixSeparators)
    {
        return ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
    }

    for (size_t Index = 0; path[Index] != 0; Index++)
    {
        if (IsDirectorySeparatorByte(path[Index]))
        {
            return path[Index];
        }
    }

    return ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
}

static void WriteNormalizedRootPrefix(const unsigned char* path,
    PathRootInfo rootInfo,
    unsigned char* output,
    size_t* writeIndex)
{
    *writeIndex = 0;

#if defined(__linux__)
    (void)path;
    if (rootInfo.Kind == PathRootKind_UnixRoot)
    {
        output[(*writeIndex)++] = ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
    }
#elif defined(_WIN32)
    if (rootInfo.Kind == PathRootKind_WindowsDriveAbsolute)
    {
        output[(*writeIndex)++] = path[0];
        output[(*writeIndex)++] = u8':';
        output[(*writeIndex)++] = ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
    }
    else if (rootInfo.Kind == PathRootKind_WindowsDriveRelative)
    {
        output[(*writeIndex)++] = path[0];
        output[(*writeIndex)++] = u8':';
    }
    else if (rootInfo.Kind == PathRootKind_WindowsCurrentDrive)
    {
        output[(*writeIndex)++] = ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
    }
    else if (rootInfo.Kind == PathRootKind_WindowsUNC)
    {
        size_t Index = PATH_WINDOWS_UNC_PREFIX_SEPARATOR_COUNT;
        size_t ServerStart = Index;
        size_t ShareStart = 0;
        size_t ShareLength = 0;

        while ((path[Index] != 0) && !IsDirectorySeparatorByte(path[Index]))
        {
            Index++;
        }

        output[(*writeIndex)++] = ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
        output[(*writeIndex)++] = ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
        Memory_Copy(path + ServerStart, output + *writeIndex, Index - ServerStart);
        *writeIndex += Index - ServerStart;
        output[(*writeIndex)++] = ENVIRONMENT_PATH_SEPARATOR_PRIMARY;

        while (IsDirectorySeparatorByte(path[Index]))
        {
            Index++;
        }

        ShareStart = Index;
        while ((path[Index] != 0) && !IsDirectorySeparatorByte(path[Index]))
        {
            Index++;
        }

        ShareLength = Index - ShareStart;
        Memory_Copy(path + ShareStart, output + *writeIndex, ShareLength);
        *writeIndex += ShareLength;
    }
#endif
}

static size_t CollectNormalizedSegments(const unsigned char* path,
    PathRootInfo rootInfo,
    bool collapseSegments,
    PathSegmentView* segments)
{
    size_t Index = rootInfo.Length;
    size_t SegmentCount = 0;
    PathSegmentView Segment;

    while (TryGetNextSegmentView(path, &Index, &Segment))
    {
        if (!collapseSegments)
        {
            segments[SegmentCount++] = Segment;
            continue;
        }
        if (IsCurrentDirectorySegment(Segment.Start, Segment.Length))
        {
            continue;
        }
        if (IsParentDirectorySegment(Segment.Start, Segment.Length))
        {
            if ((SegmentCount > 0) && !IsParentDirectorySegment(segments[SegmentCount - 1].Start, segments[SegmentCount - 1].Length))
            {
                SegmentCount--;
                continue;
            }
            if (rootInfo.IsRooted)
            {
                continue;
            }
        }

        segments[SegmentCount++] = Segment;
    }

    return SegmentCount;
}

static bool NormalizedSegmentNeedsSeparator(const unsigned char* output, size_t writeIndex, PathRootInfo rootInfo)
{
    if (writeIndex == 0)
    {
        return false;
    }

#if defined(_WIN32)
    if ((writeIndex == 2) && (rootInfo.Kind == PathRootKind_WindowsDriveRelative))
    {
        return false;
    }
#else
    (void)rootInfo;
#endif

    return !IsDirectorySeparatorByte(output[writeIndex - 1]);
}

static size_t WriteNormalizedSegments(unsigned char* output,
    size_t writeIndex,
    PathRootInfo rootInfo,
    const PathSegmentView* segments,
    size_t segmentCount,
    unsigned char separator)
{
    for (size_t Index = 0; Index < segmentCount; Index++)
    {
        if (NormalizedSegmentNeedsSeparator(output, writeIndex, rootInfo))
        {
            output[writeIndex++] = separator;
        }

        Memory_Copy(segments[Index].Start, output + writeIndex, segments[Index].Length);
        writeIndex += segments[Index].Length;
    }

    return writeIndex;
}

static size_t FinalizeNormalizedPath(const unsigned char* path,
    PathRootInfo rootInfo,
    unsigned char* output,
    size_t writeIndex,
    size_t segmentCount,
    unsigned char separator)
{
    if ((HasTrailingSeparatorPastRoot(path, rootInfo) || (rootInfo.IsRooted && (segmentCount == 0)))
        && (writeIndex > 0)
        && !IsDirectorySeparatorByte(output[writeIndex - 1]))
    {
        output[writeIndex++] = separator;
    }

    if (writeIndex == 0)
    {
        if (rootInfo.Kind == PathRootKind_WindowsDriveRelative)
        {
            output[writeIndex++] = path[0];
            output[writeIndex++] = u8':';
        }
        else if (!rootInfo.IsRooted)
        {
            output[writeIndex++] = u8'.';
        }
    }

    output[writeIndex] = 0;
    return writeIndex;
}

static Error DuplicatePathString(const unsigned char* path, unsigned char** outNormalizedPath)
{
    size_t PathLength = GetStringLength(path);
    unsigned char* Output = Memory_Allocate(PathLength + 1);

    Memory_Copy(path, Output, PathLength + 1);
    *outNormalizedPath = Output;
    return Error_CreateSuccess();
}

static Error BuildNormalizedPathString(const unsigned char* path,
    PathNormalizeConditions conditions,
    unsigned char** outNormalizedPath)
{
    PathRootInfo RootInfo = GetPathRootInfo(path);
    size_t PathLength = GetStringLength(path);
    size_t SegmentCapacity = CountSegments(path);
    size_t SegmentCount = 0;
    size_t WriteIndex = 0;
    unsigned char Separator = GetNormalizationSeparator(path,
        (conditions & PathNormalizeConditions_FixSeparators) != 0);
    PathSegmentView* Segments = NULL;
    unsigned char* Output = Memory_Allocate(PathLength + 1);

    if (SegmentCapacity > 0)
    {
        Segments = Memory_Allocate(sizeof(*Segments) * SegmentCapacity);
    }

    WriteNormalizedRootPrefix(path, RootInfo, Output, &WriteIndex);
    SegmentCount = CollectNormalizedSegments(path,
        RootInfo,
        (conditions & PathNormalizeConditions_CollapseDirectorySegment) != 0,
        Segments);
    WriteIndex = WriteNormalizedSegments(Output, WriteIndex, RootInfo, Segments, SegmentCount, Separator);
    WriteIndex = FinalizeNormalizedPath(path, RootInfo, Output, WriteIndex, SegmentCount, Separator);

    Memory_Free(Segments);
    *outNormalizedPath = Output;
    return Error_CreateSuccess();
}

static Error NormalizePathToOwnedString(const unsigned char* path,
    PathNormalizeConditions conditions,
    unsigned char** outNormalizedPath)
{
    Error Result = Error_CreateSuccess();

    if (outNormalizedPath == NULL)
    {
        return CreateNullArgumentError(u8"outNormalizedPath");
    }

    *outNormalizedPath = NULL;
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (conditions == PathNormalizeConditions_None)
    {
        return DuplicatePathString(path, outNormalizedPath);
    }

    return BuildNormalizedPathString(path, conditions, outNormalizedPath);
}

static Error MeasureCombinedPathLength(const unsigned char** paths, size_t pathCount, size_t* totalLength)
{
    size_t TotalLength = 0;

    for (size_t Index = 0; Index < pathCount; Index++)
    {
        PathRootInfo RootInfo;
        size_t Length = 0;
        Error Result = ValidatePathArgument(paths[Index], u8"paths[index]");

        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Result = ValidatePathInternal(paths[Index]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        RootInfo = GetPathRootInfo(paths[Index]);
        if ((Index > 0) && (RootInfo.Length > 0))
        {
            return Error_Construct1(ErrorCode_InvalidPath,
                u8"Only the first path in a path combination may be rooted.");
        }

        Length = GetStringLength(paths[Index]);
        if ((Index > 0)
            && !Path_EndsInDirectorySeparator(paths[Index - 1])
            && (paths[Index][0] != 0)
            && !IsDirectorySeparatorByte(paths[Index][0]))
        {
            if (TotalLength == SIZE_MAX)
            {
                return CreateOverflowError(u8"combine paths");
            }

            TotalLength++;
        }
        if (TotalLength > (SIZE_MAX - Length))
        {
            return CreateOverflowError(u8"combine paths");
        }

        TotalLength += Length;
    }

    *totalLength = TotalLength;
    return Error_CreateSuccess();
}

static size_t WriteCombinedPath(const unsigned char** paths, size_t pathCount, unsigned char* output)
{
    size_t WrittenLength = 0;
    bool HasWrittenAny = false;

    for (size_t Index = 0; Index < pathCount; Index++)
    {
        size_t Length = GetStringLength(paths[Index]);

        if ((HasWrittenAny)
            && (WrittenLength > 0)
            && !IsDirectorySeparatorByte(output[WrittenLength - 1])
            && (Length > 0)
            && !IsDirectorySeparatorByte(paths[Index][0]))
        {
            output[WrittenLength++] = ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
        }
        if (Length > 0)
        {
            Memory_Copy(paths[Index], output + WrittenLength, Length);
            WrittenLength += Length;
        }

        HasWrittenAny = true;
    }

    output[WrittenLength] = 0;
    return WrittenLength;
}

static Error MeasureSplitRequirements(const unsigned char* path,
    size_t startIndex,
    size_t* segmentCount,
    size_t* totalStringBytes)
{
    size_t SegmentCount = 0;
    size_t TotalStringBytes = 0;
    size_t Index = startIndex;
    PathSegmentView Segment;

    while (TryGetNextSegmentView(path, &Index, &Segment))
    {
        if (TotalStringBytes > (SIZE_MAX - Segment.Length - 1))
        {
            return CreateOverflowError(u8"split the path");
        }

        TotalStringBytes += Segment.Length + 1;
        SegmentCount++;
    }

    *segmentCount = SegmentCount;
    *totalStringBytes = TotalStringBytes;
    return Error_CreateSuccess();
}

static Error EnsureSplitBufferCapacity(GenericBuffer* strBuffer, GenericBuffer* segmentPtrBuffer, size_t segmentCount, size_t totalStringBytes)
{
    Error Result = EnsureByteBufferCapacity(strBuffer, totalStringBytes, u8"split the path");
    size_t AddedSegmentCount = 0;

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (segmentCount > segmentPtrBuffer->_count)
    {
        AddedSegmentCount = segmentCount - segmentPtrBuffer->_count;
    }
    if (!GenericBuffer_TryPrepareForManualMutation(segmentPtrBuffer, AddedSegmentCount))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Cannot split the path because the segment pointer buffer requires at least %zu elements of capacity.",
            segmentCount);
    }

    return Error_CreateSuccess();
}

static void WriteSplitBuffers(const unsigned char* path,
    size_t startIndex,
    GenericBuffer* strBuffer,
    GenericBuffer* segmentPtrBuffer)
{
    size_t Index = startIndex;
    size_t StringWriteIndex = 0;
    size_t PointerWriteIndex = 0;
    PathSegmentView Segment;

    while (TryGetNextSegmentView(path, &Index, &Segment))
    {
        unsigned char* SegmentText = strBuffer->_data + StringWriteIndex;

        if (Segment.Length > 0)
        {
            Memory_Copy(Segment.Start, SegmentText, Segment.Length);
        }

        SegmentText[Segment.Length] = 0;
        StringWriteIndex += Segment.Length + 1;
        ((unsigned char**)segmentPtrBuffer->_data)[PointerWriteIndex++] = SegmentText;
    }

    strBuffer->_count = StringWriteIndex;
    segmentPtrBuffer->_count = PointerWriteIndex;
}


// Public functions.
Error Path_ChangeExtension(const unsigned char* path, const unsigned char* newExtension, GenericBuffer* result)
{
    if (newExtension == NULL)
    {
        return Path_RemoveExtension(path, result);
    }

    PathSegmentView LastEntry;
    size_t ExtensionIndex = 0;
    size_t PathLength = 0;
    size_t LastEntryOffset = 0;
    size_t PrefixLength = 0;
    size_t NewExtensionLength = 0;
    bool HasExtension = false;
    bool NeedsDot = false;
    size_t OutputLength = 0;
    Error Result = Error_CreateSuccess();

    Result = ValidatePathArgument(path, u8"path");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (newExtension == NULL)
    {
        return CreateNullArgumentError(u8"newExtension");
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    PathLength = GetStringLength(path);
    TryGetLastEntryView(path, &LastEntry);
    if ((LastEntry.Length == 0) || IsSpecialDirectorySegment(LastEntry.Start, LastEntry.Length))
    {
        return WriteBufferBytes(result, path, PathLength, true, u8"change the path extension");
    }

    LastEntryOffset = (size_t)(LastEntry.Start - path);
    PrefixLength = PathLength;
    HasExtension = SegmentHasExtension(LastEntry.Start, LastEntry.Length, &ExtensionIndex);
    if (HasExtension)
    {
        PrefixLength = LastEntryOffset + ExtensionIndex;
    }

    NewExtensionLength = GetStringLength(newExtension);
    NeedsDot = (NewExtensionLength > 0) && (newExtension[0] != u8'.');
    OutputLength = PrefixLength + NewExtensionLength + (NeedsDot ? 1U : 0U);
    if (OutputLength < PrefixLength)
    {
        return CreateOverflowError(u8"change the path extension");
    }

    Result = EnsureByteBufferCapacity(result, OutputLength + 1, u8"change the path extension");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (PrefixLength > 0)
    {
        Memory_Copy(path, result->_data, PrefixLength);
    }

    result->_count = PrefixLength;
    if (NeedsDot)
    {
        result->_data[result->_count++] = u8'.';
    }
    if (NewExtensionLength > 0)
    {
        Memory_Copy(newExtension, result->_data + result->_count, NewExtensionLength);
        result->_count += NewExtensionLength;
    }

    result->_data[result->_count++] = 0;
    return Error_CreateSuccess();
}

Error Path_RemoveExtension(const unsigned char* path, GenericBuffer* result)
{
    return Path_ChangeExtension(path, u8"", result);
}

Error Path_GetExtension(const unsigned char* path, GenericBuffer* result)
{
    PathSegmentView LastEntry;
    size_t ExtensionIndex = 0;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    TryGetLastEntryView(path, &LastEntry);
    if (!SegmentHasExtension(LastEntry.Start, LastEntry.Length, &ExtensionIndex))
    {
        return WriteBufferBytes(result, u8"", 0, true, u8"get the path extension");
    }

    return WriteBufferBytes(result,
        LastEntry.Start + ExtensionIndex,
        LastEntry.Length - ExtensionIndex,
        true,
        u8"get the path extension");
}

Error Path_HasExtension(const unsigned char* path, bool* hasExtension)
{
    PathSegmentView LastEntry;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (hasExtension == NULL)
    {
        return CreateNullArgumentError(u8"hasExtension");
    }

    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    TryGetLastEntryView(path, &LastEntry);
    *hasExtension = SegmentHasExtension(LastEntry.Start, LastEntry.Length, NULL);
    return Error_CreateSuccess();
}

Error Path_Combine(const unsigned char** paths, size_t pathCount, GenericBuffer* result)
{
    Error Result = ValidateByteBuffer(result, u8"result");
    size_t TotalLength = 0;

    if (paths == NULL)
    {
        return CreateNullArgumentError(u8"paths");
    }
    if (pathCount == 0)
    {
        return WriteBufferBytes(result, u8"", 0, true, u8"combine paths");
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = MeasureCombinedPathLength(paths, pathCount, &TotalLength);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = EnsureByteBufferCapacity(result, TotalLength + 1, u8"combine paths");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    result->_count = WriteCombinedPath(paths, pathCount, result->_data) + 1;
    return Error_CreateSuccess();
}

Error Path_Append(const unsigned char* pathA, const unsigned char* pathB, GenericBuffer* result)
{
    const unsigned char* Paths[2] = { pathA, pathB };

    return Path_Combine(Paths, 2, result);
}

bool Path_EndsInDirectorySeparator(const unsigned char* path)
{
    size_t Length = GetStringLength(path);

    if (Length == 0)
    {
        return false;
    }

    return IsDirectorySeparatorByte(path[Length - 1]);
}

Error Path_GetParentPath(const unsigned char* path, GenericBuffer* result)
{
    size_t ParentLength = 0;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ParentLength = GetParentPathLength(path);
    return WriteBufferBytes(result, path, ParentLength, true, u8"get the parent path");
}

Error Path_GetLastEntryName(const unsigned char* path, GenericBuffer* result)
{
    PathSegmentView LastEntry;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    TryGetLastEntryView(path, &LastEntry);
    return WriteBufferBytes(result, LastEntry.Start, LastEntry.Length, true, u8"get the last path entry");
}

Error Path_GetLastEntryStem(const unsigned char* path, GenericBuffer* result)
{
    PathSegmentView LastEntry;
    size_t ExtensionIndex = 0;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    TryGetLastEntryView(path, &LastEntry);
    if (!SegmentHasExtension(LastEntry.Start, LastEntry.Length, &ExtensionIndex))
    {
        return WriteBufferBytes(result, LastEntry.Start, LastEntry.Length, true, u8"get the last path entry stem");
    }

    return WriteBufferBytes(result, LastEntry.Start, ExtensionIndex, true, u8"get the last path entry stem");
}

bool Path_IsEntryNameValid(const unsigned char* entryName)
{
    Error Result;

    if ((entryName == NULL) || (entryName[0] == 0))
    {
        return false;
    }
    if (PathContainsInvalidUtf8(entryName))
    {
        return false;
    }

    Result = ValidateEntrySegment(entryName, GetStringLength(entryName), false);

    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false;
    }

    return true;
}

Error Path_ValidateEntryName(const unsigned char* entryName)
{
    Error Result;

    if (entryName == NULL)
    {
        return CreateNullArgumentError(u8"entryName");
    }
    if (entryName[0] == 0)
    {
        return CreateInvalidEntryNameError(entryName, u8"entry names must not be empty.");
    }
    if (PathContainsInvalidUtf8(entryName))
    {
        return CreateInvalidEntryNameError(entryName, u8"entry names must be valid UTF-8.");
    }

    Result = ValidateEntrySegment(entryName, GetStringLength(entryName), false);
    if (Result.Code != ErrorCode_Success)
    {
        Error MappedError = CreateInvalidEntryNameError(entryName,
            Result.Message != NULL ? Result.Message : u8"entry name is invalid.");
        Error_Deconstruct(&Result);
        return MappedError;
    }

    return Error_CreateSuccess();
}

bool Path_IsValid(const unsigned char* path)
{
    Error Result = ValidatePathInternal(path);

    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false;
    }

    return true;
}

Error Path_Validate(const unsigned char* path)
{
    return ValidatePathInternal(path);
}

PathType Path_GetPathType(const unsigned char* path)
{
    PathRootInfo RootInfo = GetPathRootInfo(path);

    if ((path == NULL) || (path[0] == 0))
    {
        return PathType_None;
    }

    return RootInfo.IsRooted ? FilePathType_Absolute : FilePathType_Relative;
}

Error Path_Normalize(const unsigned char* path, PathNormalizeConditions conditions, GenericBuffer* buffer)
{
    unsigned char* NormalizedPath = NULL;
    Error Result = PrepareByteBuffer(buffer, u8"buffer");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = NormalizePathToOwnedString(path, conditions, &NormalizedPath);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = WriteBufferBytes(buffer,
        NormalizedPath,
        GetStringLength(NormalizedPath),
        true,
        u8"normalize the path");
    Memory_Free(NormalizedPath);
    return Result;
}

bool Path_IsNormalized(const unsigned char* path, PathNormalizeConditions conditions)
{
    unsigned char* NormalizedPath = NULL;
    bool IsEqual = false;
    Error Result = NormalizePathToOwnedString(path, conditions, &NormalizedPath);

    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false;
    }

    IsEqual = PathsEqualNormalized(path, NormalizedPath);
    Memory_Free(NormalizedPath);
    return IsEqual;
}

bool Path_IsRooted(const unsigned char* path)
{
    return GetPathRootInfo(path).IsRooted;
}

bool Path_IsFullyQualified(const unsigned char* path)
{
    return GetPathRootInfo(path).IsFullyQualified;
}

Error Path_GetRoot(const unsigned char* path, GenericBuffer* result)
{
    PathRootInfo RootInfo;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    RootInfo = GetPathRootInfo(path);
    return WriteBufferBytes(result, path, RootInfo.Length, true, u8"get the path root");
}

bool Path_IsSubPath(const unsigned char* parentPath, const unsigned char* childPath)
{
    unsigned char* NormalizedParent = NULL;
    unsigned char* NormalizedChild = NULL;
    size_t ParentLength = 0;
    bool IsMatch = false;
    Error Result = NormalizePathToOwnedString(parentPath,
        PathNormalizeConditions_FixSeparators | PathNormalizeConditions_CollapseDirectorySegment,
        &NormalizedParent);

    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false;
    }

    Result = NormalizePathToOwnedString(childPath,
        PathNormalizeConditions_FixSeparators | PathNormalizeConditions_CollapseDirectorySegment,
        &NormalizedChild);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(NormalizedParent);
        Error_Deconstruct(&Result);
        return false;
    }

    if (Path_GetPathType(NormalizedParent) != Path_GetPathType(NormalizedChild))
    {
        Memory_Free(NormalizedParent);
        Memory_Free(NormalizedChild);
        return false;
    }

    ParentLength = GetStringLength(NormalizedParent);
    if (ParentLength == 0)
    {
        Memory_Free(NormalizedParent);
        Memory_Free(NormalizedChild);
        return false;
    }

    if (PathsEqualNormalized(NormalizedParent, NormalizedChild))
    {
        Memory_Free(NormalizedParent);
        Memory_Free(NormalizedChild);
        return true;
    }
    if (GetStringLength(NormalizedChild) <= ParentLength)
    {
        Memory_Free(NormalizedParent);
        Memory_Free(NormalizedChild);
        return false;
    }

    IsMatch = true;
    for (size_t Index = 0; Index < ParentLength; Index++)
    {
#if defined(_WIN32)
        if (ToAsciiLower(NormalizedParent[Index]) != ToAsciiLower(NormalizedChild[Index]))
        {
            IsMatch = false;
            break;
        }
#else
        if (NormalizedParent[Index] != NormalizedChild[Index])
        {
            IsMatch = false;
            break;
        }
#endif
    }

    if (IsMatch && !IsDirectorySeparatorByte(NormalizedChild[ParentLength]))
    {
        IsMatch = false;
    }

    Memory_Free(NormalizedParent);
    Memory_Free(NormalizedChild);
    return IsMatch;
}

Error Path_TrimTrailingSeparator(const unsigned char* path, GenericBuffer* result)
{
    PathRootInfo RootInfo;
    size_t TrimmedLength = 0;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    RootInfo = GetPathRootInfo(path);
    TrimmedLength = GetTrimmedPathLength(path, RootInfo.Length);
    return WriteBufferBytes(result, path, TrimmedLength, true, u8"trim the trailing path separator");
}

Error Path_EnsureTrailingSeparator(const unsigned char* path, GenericBuffer* result)
{
    size_t Length = 0;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareByteBuffer(result, u8"result");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (Path_EndsInDirectorySeparator(path))
    {
        return WriteBufferBytes(result, path, GetStringLength(path), true, u8"ensure the trailing path separator");
    }

    Length = GetStringLength(path);
    Result = EnsureByteBufferCapacity(result, Length + 2, u8"ensure the trailing path separator");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Copy(path, result->_data, Length);
    result->_count = Length;
    result->_data[result->_count++] = ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
    result->_data[result->_count++] = 0;
    return Error_CreateSuccess();
}

bool Path_ContainsDirectorySegments(const unsigned char* path)
{
    PathRootInfo RootInfo;
    size_t Index = 0;

    if ((path == NULL) || (path[0] == 0))
    {
        return false;
    }

    RootInfo = GetPathRootInfo(path);
    Index = RootInfo.Length;
    while (path[Index] != 0)
    {
        size_t SegmentStart = 0;
        size_t SegmentLength = 0;

        while (IsDirectorySeparatorByte(path[Index]))
        {
            Index++;
        }
        if (path[Index] == 0)
        {
            break;
        }

        SegmentStart = Index;
        while ((path[Index] != 0) && !IsDirectorySeparatorByte(path[Index]))
        {
            Index++;
        }

        SegmentLength = Index - SegmentStart;
        if (IsSpecialDirectorySegment(path + SegmentStart, SegmentLength))
        {
            return true;
        }
    }

    return false;
}

Error Path_Split(const unsigned char* path, GenericBuffer* strBuffer, GenericBuffer* segmentPtrBuffer)
{
    PathRootInfo RootInfo;
    size_t SegmentCount = 0;
    size_t TotalStringBytes = 0;
    Error Result = ValidatePathArgument(path, u8"path");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateByteBuffer(strBuffer, u8"strBuffer");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePointerBuffer(segmentPtrBuffer, u8"segmentPtrBuffer");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidatePathInternal(path);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    RootInfo = GetPathRootInfo(path);
    Result = MeasureSplitRequirements(path, RootInfo.Length, &SegmentCount, &TotalStringBytes);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = EnsureSplitBufferCapacity(strBuffer, segmentPtrBuffer, SegmentCount, TotalStringBytes);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    WriteSplitBuffers(path, RootInfo.Length, strBuffer, segmentPtrBuffer);
    return Error_CreateSuccess();
}
