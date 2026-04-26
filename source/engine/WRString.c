#include "WRString.h"
#include "WRError.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


// Macros.


// Types.
typedef struct StringCodePointDataStruct
{
    CodePoint CodePoint;
    size_t StartIndex;
    size_t ByteCount;
} StringCodePointData;

typedef struct StringIndexWriteContextStruct
{
    size_t* Indices;
    size_t IndexCount;
} StringIndexWriteContext;


// Fields.
const unsigned char* const STRING_EMPTY = u8"";


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"String argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateUnicodeArgumentError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument,
        u8"String operations requiring Unicode data must be given a valid UnicodeData instance.");
}

static Error CreateByteBufferTypeError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"String argument \"%s\" must be a byte buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreatePointerBufferTypeError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"String argument \"%s\" must be a pointer buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreateIndexBufferTypeError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"String argument \"%s\" must be a size_t buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreateInvalidEncodingError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidTextEncoding,
        u8"Cannot %s because the string does not contain valid UTF-8.",
        operationName);
}

static Error CreateInvalidCodePointError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidCodePoint,
        u8"Cannot %s because the string contains a codepoint that is not defined in the Unicode data.",
        operationName);
}

static Error CreateBufferTooSmallError(const unsigned char* operationName, size_t requiredCapacity)
{
    return Error_Construct3(ErrorCode_BufferTooSmall,
        u8"Cannot %s because the destination buffer requires at least %zu elements of capacity.",
        operationName,
        requiredCapacity);
}

static Error CreateOverflowError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Cannot %s because the required size exceeds the supported range.",
        operationName);
}

static Error CreateIndexOutOfBoundsError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_IndexOutOfBounds,
        u8"Cannot %s because the given string index is outside the valid range.",
        operationName);
}

static bool StringBufferAllocate(GenericBuffer* destination, size_t requestedCapacity)
{
    void* NewData = Memory_Reallocate(destination->_data, requestedCapacity * destination->_elementSize);

    if (NewData == NULL)
    {
        return false;
    }

    destination->_data = NewData;
    destination->_capacity = requestedCapacity;
    return true;
}

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

static Error ValidateStringArgument(const unsigned char* str, const unsigned char* argumentName)
{
    if (str == NULL)
    {
        return CreateNullArgumentError(argumentName);
    }

    return Error_CreateSuccess();
}

static Error ValidateStringPairArguments(const unsigned char* a, const unsigned char* b)
{
    Error Result = ValidateStringArgument(a, u8"a");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return ValidateStringArgument(b, u8"b");
}

static Error ValidateUnicode(UnicodeData* unicode)
{
    if (unicode == NULL)
    {
        return CreateUnicodeArgumentError();
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
        return CreateByteBufferTypeError(argumentName, buffer->_elementSize);
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

static Error ValidateIndexBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    if (buffer == NULL)
    {
        return CreateNullArgumentError(argumentName);
    }
    if (buffer->_elementSize != sizeof(size_t))
    {
        return CreateIndexBufferTypeError(argumentName, buffer->_elementSize);
    }

    return Error_CreateSuccess();
}

static Error PrepareByteBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    return ValidateByteBuffer(buffer, argumentName);
}

static Error PreparePointerBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    return ValidatePointerBuffer(buffer, argumentName);
}

static Error PrepareIndexBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    return ValidateIndexBuffer(buffer, argumentName);
}

static Error EnsureBufferCapacity(GenericBuffer* buffer, size_t requiredCapacity, const unsigned char* operationName)
{
    if (!GenericBuffer_EnsureTotalCapacity(buffer, requiredCapacity))
    {
        return CreateBufferTooSmallError(operationName, requiredCapacity);
    }

    return Error_CreateSuccess();
}

static Error AppendBytes(GenericBuffer* destination, const unsigned char* bytes, size_t byteCount, const unsigned char* operationName)
{
    size_t RequiredCapacity = 0;

    if (byteCount == 0)
    {
        return Error_CreateSuccess();
    }
    if (bytes == NULL)
    {
        return CreateNullArgumentError(u8"bytes");
    }
    if (destination->_count > (SIZE_MAX - byteCount))
    {
        return CreateOverflowError(operationName);
    }

    RequiredCapacity = destination->_count + byteCount;
    if (!GenericBuffer_AddLastRange(destination, (void*)bytes, byteCount))
    {
        return CreateBufferTooSmallError(operationName, RequiredCapacity);
    }

    return Error_CreateSuccess();
}

static Error WriteBytesToBuffer(GenericBuffer* destination,
    const unsigned char* bytes,
    size_t byteCount,
    const unsigned char* operationName)
{
    Error Result = PrepareByteBuffer(destination, u8"destination");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (byteCount == SIZE_MAX)
    {
        return CreateOverflowError(operationName);
    }

    if (destination->_count > (SIZE_MAX - byteCount - 1))
    {
        return CreateOverflowError(operationName);
    }

    Result = EnsureBufferCapacity(destination, destination->_count + byteCount + 1, operationName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = AppendBytes(destination, bytes, byteCount, operationName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(operationName, destination->_count + 1);
    }

    return Error_CreateSuccess();
}

static Error AppendCodePoint(GenericBuffer* destination, CodePoint codePoint, const unsigned char* operationName)
{
    unsigned char Character[CODEPOINT_BYTE_COUNT_MAX];
    size_t ByteCount = CharUTF8_WriteCodePoint(Character, codePoint);

    if (ByteCount == 0)
    {
        return CreateInvalidCodePointError(operationName);
    }
    if (!GenericBuffer_AddLastRange(destination, Character, ByteCount))
    {
        return CreateBufferTooSmallError(operationName, destination->_count + ByteCount);
    }

    return Error_CreateSuccess();
}

static bool TryReadCodePointAt(const unsigned char* str,
    size_t byteLength,
    size_t index,
    StringCodePointData* outData)
{
    size_t ByteCount = 0;
    CodePoint Code = CODEPOINT_NONE;

    if ((str == NULL) || (index >= byteLength))
    {
        return false;
    }

    ByteCount = CharUTF8_GetByteCountChar(str + index);
    if ((ByteCount == 0) || ((index + ByteCount) > byteLength))
    {
        return false;
    }
    if (!CharUTF8_IsCharBufferValid(str + index, byteLength - index))
    {
        return false;
    }

    Code = CharUTF8_GetCodePoint(str + index);
    if (Code == CODEPOINT_NONE)
    {
        return false;
    }

    if (outData != NULL)
    {
        *outData = (StringCodePointData)
        {
            .CodePoint = Code,
            .StartIndex = index,
            .ByteCount = ByteCount,
        };
    }

    return true;
}

static bool IsRegionEncodingValid(const unsigned char* str, size_t byteLength)
{
    size_t Index = 0;

    if (str == NULL)
    {
        return false;
    }

    while (Index < byteLength)
    {
        StringCodePointData CharacterData;

        if (!TryReadCodePointAt(str, byteLength, Index, &CharacterData))
        {
            return false;
        }

        Index += CharacterData.ByteCount;
    }

    return true;
}

static Error GetCharacterIndicesInternal(const unsigned char* str, GenericBuffer* indexBuffer)
{
    size_t ByteLength = StringUTF8_GetByteLength(str);
    size_t Index = 0;
    Error Result = PrepareIndexBuffer(indexBuffer, u8"indexArray");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    while (Index < ByteLength)
    {
        StringCodePointData CharacterData;

        if (!TryReadCodePointAt(str, ByteLength, Index, &CharacterData))
        {
            return CreateInvalidEncodingError(u8"get the character index array");
        }
        if (!GenericBuffer_AddLast(indexBuffer, &Index))
        {
            return CreateBufferTooSmallError(u8"get the character index array", indexBuffer->_count + 1);
        }

        Index += CharacterData.ByteCount;
    }

    return Error_CreateSuccess();
}

static void CreateGrowableIndexBuffer(GenericBuffer* buffer)
{
    size_t InitialCapacity = 32;
    size_t* Data = Memory_Allocate(sizeof(*Data) * InitialCapacity);

    GenericBuffer_CreateVariable(buffer,
        Data,
        InitialCapacity,
        sizeof(size_t),
        0,
        NULL,
        &StringBufferAllocate);
}

static bool IsCharacterBoundary(const unsigned char* str, size_t byteLength, size_t index)
{
    size_t CurrentIndex = 0;

    if (index > byteLength)
    {
        return false;
    }
    if (index == byteLength)
    {
        return true;
    }

    while (CurrentIndex < byteLength)
    {
        StringCodePointData CharacterData;

        if (!TryReadCodePointAt(str, byteLength, CurrentIndex, &CharacterData))
        {
            return false;
        }
        if (CurrentIndex == index)
        {
            return true;
        }

        CurrentIndex += CharacterData.ByteCount;
    }

    return false;
}

static Error NormalizeStartIndex(const unsigned char* str,
    size_t byteLength,
    StringIndexOfOptions options,
    size_t* outIndex)
{
    GenericBuffer CharacterIndices;
    Error Result = Error_CreateSuccess();

    CreateGrowableIndexBuffer(&CharacterIndices);

    if (outIndex == NULL)
    {
        return CreateNullArgumentError(u8"outIndex");
    }

    *outIndex = 0;
    if (options._startIndex >= 0)
    {
        if ((uint64_t)options._startIndex > (uint64_t)byteLength)
        {
            return Error_CreateSuccess();
        }

        *outIndex = (size_t)options._startIndex;
        return Error_CreateSuccess();
    }

    Result = GetCharacterIndicesInternal(str, &CharacterIndices);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(CharacterIndices._data);
        return Result;
    }

    if (CharacterIndices._count == 0)
    {
        Memory_Free(CharacterIndices._data);
        *outIndex = 0;
        return Error_CreateSuccess();
    }

    {
        int64_t CharacterOffsetFromEnd = -options._startIndex;

        if (CharacterOffsetFromEnd > (int64_t)CharacterIndices._count)
        {
            Memory_Free(CharacterIndices._data);
            return Error_CreateSuccess();
        }

        *outIndex = ((size_t*)CharacterIndices._data)[CharacterIndices._count - (size_t)CharacterOffsetFromEnd];
    }

    Memory_Free(CharacterIndices._data);
    return Error_CreateSuccess();
}

static bool AreCodePointsEqual(CodePoint a, CodePoint b, StringCaseRule caseRule, UnicodeData* unicode)
{
    if (caseRule == StringCaseRule_MatchCase)
    {
        return a == b;
    }

    return Unicode_EqualsCaseIgnore(unicode, a, b);
}

static Error MatchStringAt(const unsigned char* str,
    size_t strLength,
    size_t startIndex,
    const unsigned char* target,
    size_t targetLength,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outMatches)
{
    size_t LeftIndex = startIndex;
    size_t RightIndex = 0;

    if (outMatches == NULL)
    {
        return CreateNullArgumentError(u8"outMatches");
    }

    *outMatches = false;
    if (startIndex > strLength)
    {
        return Error_CreateSuccess();
    }
    if ((strLength - startIndex) < targetLength)
    {
        return Error_CreateSuccess();
    }

    while (RightIndex < targetLength)
    {
        StringCodePointData LeftCharacter;
        StringCodePointData RightCharacter;

        if (!TryReadCodePointAt(str, strLength, LeftIndex, &LeftCharacter))
        {
            return CreateInvalidEncodingError(u8"compare strings");
        }
        if (!TryReadCodePointAt(target, targetLength, RightIndex, &RightCharacter))
        {
            return CreateInvalidEncodingError(u8"compare strings");
        }
        if ((caseRule == StringCaseRule_MatchCase) && (LeftCharacter.CodePoint != RightCharacter.CodePoint))
        {
            return Error_CreateSuccess();
        }
        if ((caseRule != StringCaseRule_MatchCase)
            && !AreCodePointsEqual(LeftCharacter.CodePoint, RightCharacter.CodePoint, caseRule, unicode))
        {
            return Error_CreateSuccess();
        }

        LeftIndex += LeftCharacter.ByteCount;
        RightIndex += RightCharacter.ByteCount;
    }

    *outMatches = true;
    return Error_CreateSuccess();
}

static Error FindNextOccurrence(const unsigned char* str,
    size_t strLength,
    const unsigned char* target,
    size_t targetLength,
    size_t startIndex,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    size_t* outIndex)
{
    size_t Index = startIndex;

    if (outIndex == NULL)
    {
        return CreateNullArgumentError(u8"outIndex");
    }

    *outIndex = STRING_INDEX_INVALID;
    if (targetLength == 0)
    {
        *outIndex = startIndex;
        return Error_CreateSuccess();
    }

    while (Index < strLength)
    {
        bool Matches = false;
        StringCodePointData CharacterData;
        Error Result = MatchStringAt(str,
            strLength,
            Index,
            target,
            targetLength,
            caseRule,
            unicode,
            &Matches);

        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (Matches)
        {
            *outIndex = Index;
            return Error_CreateSuccess();
        }

        if (!TryReadCodePointAt(str, strLength, Index, &CharacterData))
        {
            return CreateInvalidEncodingError(u8"search the string");
        }

        Index += CharacterData.ByteCount;
    }

    return Error_CreateSuccess();
}

static Error FindPreviousOccurrence(const unsigned char* str,
    size_t strLength,
    const unsigned char* target,
    size_t targetLength,
    size_t startIndex,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    size_t* outIndex)
{
    GenericBuffer CharacterIndices;
    Error Result = Error_CreateSuccess();

    CreateGrowableIndexBuffer(&CharacterIndices);

    if (outIndex == NULL)
    {
        return CreateNullArgumentError(u8"outIndex");
    }

    *outIndex = STRING_INDEX_INVALID;
    if (targetLength == 0)
    {
        *outIndex = startIndex;
        return Error_CreateSuccess();
    }

    Result = GetCharacterIndicesInternal(str, &CharacterIndices);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(CharacterIndices._data);
        return Result;
    }

    for (size_t ArrayIndex = CharacterIndices._count; ArrayIndex > 0; ArrayIndex--)
    {
        size_t CandidateIndex = ((size_t*)CharacterIndices._data)[ArrayIndex - 1];
        bool Matches = false;

        if (CandidateIndex > startIndex)
        {
            continue;
        }

        Result = MatchStringAt(str,
            strLength,
            CandidateIndex,
            target,
            targetLength,
            caseRule,
            unicode,
            &Matches);
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(CharacterIndices._data);
            return Result;
        }
        if (Matches)
        {
            *outIndex = CandidateIndex;
            Memory_Free(CharacterIndices._data);
            return Error_CreateSuccess();
        }
    }

    Memory_Free(CharacterIndices._data);
    return Error_CreateSuccess();
}

static Error GetTrimIndicesInRange(const unsigned char* str,
    size_t rangeStart,
    size_t rangeEnd,
    bool isStartTrimmed,
    bool isEndTrimmed,
    UnicodeData* unicode,
    size_t* startIndex,
    size_t* outLength)
{
    size_t Index = rangeStart;
    size_t TrimStart = rangeStart;
    size_t TrimEnd = rangeEnd;
    bool HasNonWhitespace = false;

    if (startIndex == NULL)
    {
        return CreateNullArgumentError(u8"startIndex");
    }
    if (outLength == NULL)
    {
        return CreateNullArgumentError(u8"outLength");
    }

    while (Index < rangeEnd)
    {
        StringCodePointData CharacterData;
        bool IsWhitespace = false;

        if (!TryReadCodePointAt(str, rangeEnd, Index, &CharacterData))
        {
            return CreateInvalidEncodingError(u8"trim the string");
        }

        IsWhitespace = Unicode_IsWhitespace(unicode, CharacterData.CodePoint);
        if (!IsWhitespace)
        {
            if (!HasNonWhitespace)
            {
                TrimStart = Index;
                HasNonWhitespace = true;
            }

            TrimEnd = Index + CharacterData.ByteCount;
        }

        Index += CharacterData.ByteCount;
    }

    if (!HasNonWhitespace)
    {
        *startIndex = isStartTrimmed ? rangeEnd : rangeStart;
        *outLength = 0;
        return Error_CreateSuccess();
    }

    *startIndex = isStartTrimmed ? TrimStart : rangeStart;
    *outLength = (isEndTrimmed ? TrimEnd : rangeEnd) - *startIndex;
    return Error_CreateSuccess();
}


// Public functions.
bool StringUTF8_IsEncodingValid(const unsigned char* str)
{
    size_t ByteLength = 0;

    if (str == NULL)
    {
        return false;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    return IsRegionEncodingValid(str, ByteLength);
}

bool StringUTF8_AreCodepointsValid(const unsigned char* str, UnicodeData* unicode)
{
    size_t ByteLength = 0;
    size_t Index = 0;

    if ((str == NULL) || (unicode == NULL))
    {
        return false;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    while (Index < ByteLength)
    {
        StringCodePointData CharacterData;

        if (!TryReadCodePointAt(str, ByteLength, Index, &CharacterData))
        {
            return false;
        }
        if (!Unicode_IsDefined(unicode, CharacterData.CodePoint))
        {
            return false;
        }

        Index += CharacterData.ByteCount;
    }

    return true;
}

bool StringUTF8_IsNullOrEmpty(const unsigned char* str)
{
    return (str == NULL) || (str[0] == 0);
}

Error StringUTF8_IsNullOrWhitespace(const unsigned char* str, UnicodeData* unicode, bool* outValue)
{
    size_t StartIndex = 0;
    size_t Length = 0;

    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    if (str == NULL)
    {
        *outValue = true;
        return Error_CreateSuccess();
    }

    *outValue = false;
    if (str[0] == 0)
    {
        *outValue = true;
        return Error_CreateSuccess();
    }

    Error Result = StringUTF8_GetTrimIndices(str, true, true, &StartIndex, &Length, unicode);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outValue = (Length == 0);
    return Error_CreateSuccess();
}

Error StringUTF8_ToLower(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination)
{
    size_t ByteLength = 0;
    size_t Index = 0;
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidateUnicode(unicode);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    while (Index < ByteLength)
    {
        StringCodePointData CharacterData;
        CodePoint LowerCodePoint = CODEPOINT_NONE;

        if (!TryReadCodePointAt(str, ByteLength, Index, &CharacterData))
        {
            return CreateInvalidEncodingError(u8"convert the string to lower case");
        }

        LowerCodePoint = Unicode_ToLower(unicode, CharacterData.CodePoint);
        if (LowerCodePoint == CODEPOINT_NONE)
        {
            return CreateInvalidCodePointError(u8"convert the string to lower case");
        }

        Result = AppendCodePoint(destination, LowerCodePoint, u8"convert the string to lower case");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Index += CharacterData.ByteCount;
    }

    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"convert the string to lower case", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_ToUpper(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination)
{
    size_t ByteLength = 0;
    size_t Index = 0;
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidateUnicode(unicode);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    while (Index < ByteLength)
    {
        StringCodePointData CharacterData;
        CodePoint UpperCodePoint = CODEPOINT_NONE;

        if (!TryReadCodePointAt(str, ByteLength, Index, &CharacterData))
        {
            return CreateInvalidEncodingError(u8"convert the string to upper case");
        }

        UpperCodePoint = Unicode_ToUpper(unicode, CharacterData.CodePoint);
        if (UpperCodePoint == CODEPOINT_NONE)
        {
            return CreateInvalidCodePointError(u8"convert the string to upper case");
        }

        Result = AppendCodePoint(destination, UpperCodePoint, u8"convert the string to upper case");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Index += CharacterData.ByteCount;
    }

    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"convert the string to upper case", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_InvertCase(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination)
{
    size_t ByteLength = 0;
    size_t Index = 0;
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidateUnicode(unicode);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    while (Index < ByteLength)
    {
        StringCodePointData CharacterData;
        CodePoint ResultCodePoint = CODEPOINT_NONE;

        if (!TryReadCodePointAt(str, ByteLength, Index, &CharacterData))
        {
            return CreateInvalidEncodingError(u8"invert the string case");
        }
        if (!Unicode_IsDefined(unicode, CharacterData.CodePoint))
        {
            return CreateInvalidCodePointError(u8"invert the string case");
        }

        ResultCodePoint = CharacterData.CodePoint;
        if (Unicode_IsUpper(unicode, CharacterData.CodePoint))
        {
            ResultCodePoint = Unicode_ToLower(unicode, CharacterData.CodePoint);
        }
        else if (Unicode_IsLower(unicode, CharacterData.CodePoint))
        {
            ResultCodePoint = Unicode_ToUpper(unicode, CharacterData.CodePoint);
        }

        if (ResultCodePoint == CODEPOINT_NONE)
        {
            return CreateInvalidCodePointError(u8"invert the string case");
        }

        Result = AppendCodePoint(destination, ResultCodePoint, u8"invert the string case");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        Index += CharacterData.ByteCount;
    }

    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"invert the string case", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

size_t StringUTF8_GetByteLength(const unsigned char* str)
{
    if (str == NULL)
    {
        return 0;
    }

    return GetStringLength(str);
}

Error StringUTF8_GetCodePointLength(const unsigned char* str, size_t* outLength)
{
    size_t ByteLength = 0;
    size_t Index = 0;
    size_t Length = 0;

    if (outLength == NULL)
    {
        return CreateNullArgumentError(u8"outLength");
    }

    *outLength = 0;
    if (str == NULL)
    {
        return CreateNullArgumentError(u8"str");
    }

    ByteLength = StringUTF8_GetByteLength(str);
    while (Index < ByteLength)
    {
        StringCodePointData CharacterData;

        if (!TryReadCodePointAt(str, ByteLength, Index, &CharacterData))
        {
            return CreateInvalidEncodingError(u8"get the string codepoint length");
        }

        Length++;
        Index += CharacterData.ByteCount;
    }

    *outLength = Length;
    return Error_CreateSuccess();
}

Error StringUTF8_Equals(const unsigned char* a,
    const unsigned char* b,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue)
{
    size_t IndexA = 0;
    size_t IndexB = 0;
    size_t LengthA = 0;
    size_t LengthB = 0;
    Error Result = Error_CreateSuccess();

    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    *outValue = false;
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (caseRule == StringCaseRule_CaseIgnore)
    {
        Result = ValidateUnicode(unicode);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    LengthA = StringUTF8_GetByteLength(a);
    LengthB = StringUTF8_GetByteLength(b);

    while ((IndexA < LengthA) && (IndexB < LengthB))
    {
        StringCodePointData LeftCharacter;
        StringCodePointData RightCharacter;

        if (!TryReadCodePointAt(a, LengthA, IndexA, &LeftCharacter)
            || !TryReadCodePointAt(b, LengthB, IndexB, &RightCharacter))
        {
            return CreateInvalidEncodingError(u8"compare strings");
        }
        if (!AreCodePointsEqual(LeftCharacter.CodePoint, RightCharacter.CodePoint, caseRule, unicode))
        {
            return Error_CreateSuccess();
        }

        IndexA += LeftCharacter.ByteCount;
        IndexB += RightCharacter.ByteCount;
    }

    *outValue = (IndexA == LengthA) && (IndexB == LengthB);
    return Error_CreateSuccess();
}

Error StringUTF8_CopyTo(const unsigned char* source, GenericBuffer* destination)
{
    if (source == NULL)
    {
        return CreateNullArgumentError(u8"source");
    }

    return StringUTF8_CopyToBySize(source, StringUTF8_GetByteLength(source), destination);
}

Error StringUTF8_CopyToBySize(const unsigned char* source, size_t size, GenericBuffer* destination)
{
    Error Result = ValidateByteBuffer(destination, u8"destination");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((source == NULL) && (size > 0))
    {
        return CreateNullArgumentError(u8"source");
    }
    if ((size > 0) && !IsRegionEncodingValid(source, size))
    {
        return CreateInvalidEncodingError(u8"copy the string");
    }

    return WriteBytesToBuffer(destination, source, size, u8"copy the string");
}

StringSplitOptions String_CreateSplitOptionsNormal()
{
    return String_CreateSplitOptions(StringSplitType_None, SIZE_MAX, StringCaseRule_MatchCase);
}

StringSplitOptions String_CreateSplitOptionsTyped(StringSplitType type)
{
    return String_CreateSplitOptions(type, SIZE_MAX, StringCaseRule_MatchCase);
}

StringSplitOptions String_CreateSplitOptions(StringSplitType type, size_t maxSplits, StringCaseRule caseRule)
{
    return (StringSplitOptions)
    {
        ._splitType = type,
        ._stringCountLimit = maxSplits,
        ._caseRule = caseRule,
    };
}

Error StringUTF8_Split(const unsigned char* str,
    const unsigned char** delimeters,
    size_t delimeterCount,
    StringSplitOptions splitOptions,
    GenericBuffer* stringBuffer,
    GenericBuffer* resultPointers,
    UnicodeData* unicode)
{
    size_t StringLength = 0;
    size_t SegmentStart = 0;
    size_t SearchIndex = 0;
    size_t ResultCount = 0;
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PrepareByteBuffer(stringBuffer, u8"stringBuffer");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PreparePointerBuffer(resultPointers, u8"resultPointers");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((splitOptions._caseRule == StringCaseRule_CaseIgnore) || (splitOptions._splitType & StringSplitType_TrimEntries))
    {
        Result = ValidateUnicode(unicode);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    for (size_t Index = 0; Index < delimeterCount; Index++)
    {
        Result = ValidateStringArgument(delimeters[Index], u8"delimeters");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    StringLength = StringUTF8_GetByteLength(str);
    if ((splitOptions._stringCountLimit == 0) || (delimeterCount == 0))
    {
        unsigned char* SegmentPointer = NULL;
        size_t PreviousCount = stringBuffer->_count;

        Result = StringUTF8_CopyTo(str, stringBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        SegmentPointer = (unsigned char*)stringBuffer->_data + PreviousCount;
        if (!GenericBuffer_AddLast(resultPointers, &SegmentPointer))
        {
            return CreateBufferTooSmallError(u8"split the string", resultPointers->_count + 1);
        }

        return Error_CreateSuccess();
    }

    while (SearchIndex <= StringLength)
    {
        size_t FoundIndex = STRING_INDEX_INVALID;
        size_t FoundDelimiterLength = 0;

        if ((splitOptions._stringCountLimit != SIZE_MAX)
            && ((ResultCount + 1) >= splitOptions._stringCountLimit))
        {
            SearchIndex = StringLength;
        }
        else
        {
            for (size_t DelimiterIndex = 0; DelimiterIndex < delimeterCount; DelimiterIndex++)
            {
                const unsigned char* Delimiter = delimeters[DelimiterIndex];
                size_t DelimiterLength = StringUTF8_GetByteLength(Delimiter);
                size_t CandidateIndex = STRING_INDEX_INVALID;

                if (DelimiterLength == 0)
                {
                    continue;
                }

                Result = FindNextOccurrence(str,
                    StringLength,
                    Delimiter,
                    DelimiterLength,
                    SearchIndex,
                    splitOptions._caseRule,
                    unicode,
                    &CandidateIndex);
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
                if ((CandidateIndex != STRING_INDEX_INVALID)
                    && ((FoundIndex == STRING_INDEX_INVALID) || (CandidateIndex < FoundIndex)))
                {
                    FoundIndex = CandidateIndex;
                    FoundDelimiterLength = DelimiterLength;
                }
            }
        }

        {
            size_t SegmentEnd = (FoundIndex == STRING_INDEX_INVALID) ? StringLength : FoundIndex;
            size_t TrimStart = SegmentStart;
            size_t TrimLength = SegmentEnd - SegmentStart;
            unsigned char* SegmentPointer = NULL;

            if (splitOptions._splitType & StringSplitType_TrimEntries)
            {
                Result = GetTrimIndicesInRange(str,
                    SegmentStart,
                    SegmentEnd,
                    true,
                    true,
                    unicode,
                    &TrimStart,
                    &TrimLength);
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
            }

            if (!((splitOptions._splitType & StringSplitType_SkipEmptyEntries) && (TrimLength == 0)))
            {
                size_t PreviousCount = stringBuffer->_count;

                Result = AppendBytes(stringBuffer, str + TrimStart, TrimLength, u8"split the string");
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
                if (!GenericBuffer_AppendByte(stringBuffer, 0))
                {
                    return CreateBufferTooSmallError(u8"split the string", stringBuffer->_count + 1);
                }

                SegmentPointer = (unsigned char*)stringBuffer->_data + PreviousCount;
                if (!GenericBuffer_AddLast(resultPointers, &SegmentPointer))
                {
                    return CreateBufferTooSmallError(u8"split the string", resultPointers->_count + 1);
                }

                ResultCount++;
            }
        }

        if (FoundIndex == STRING_INDEX_INVALID)
        {
            break;
        }

        SegmentStart = FoundIndex + FoundDelimiterLength;
        SearchIndex = SegmentStart;
    }

    return Error_CreateSuccess();
}

StringIndexOfOptions String_CreateIndexOptionsNormal(void)
{
    return String_CreateIndexOptions(StringCaseRule_MatchCase, StringMoveDirection_Forwards, 0);
}

StringIndexOfOptions String_CreateIndexOptionsFromEnd(void)
{
    return String_CreateIndexOptions(StringCaseRule_MatchCase, StringMoveDirection_Backwards, -1);
}

StringIndexOfOptions String_CreateIndexOptions(StringCaseRule caseRule,
    StringMoveDirection direction,
    int64_t startIndex)
{
    return (StringIndexOfOptions)
    {
        ._caseRule = caseRule,
        ._direction = direction,
        ._startIndex = startIndex,
    };
}

Error StringUTF8_IndexOf(const unsigned char* str,
    const unsigned char* target,
    StringIndexOfOptions options,
    UnicodeData* unicode,
    size_t* outIndex)
{
    size_t StringLength = 0;
    size_t TargetLength = 0;
    size_t StartIndex = 0;
    Error Result = ValidateStringPairArguments(str, target);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outIndex == NULL)
    {
        return CreateNullArgumentError(u8"outIndex");
    }

    *outIndex = STRING_INDEX_INVALID;
    if (options._caseRule == StringCaseRule_CaseIgnore)
    {
        Result = ValidateUnicode(unicode);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    StringLength = StringUTF8_GetByteLength(str);
    TargetLength = StringUTF8_GetByteLength(target);

    Result = NormalizeStartIndex(str, StringLength, options, &StartIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((StartIndex > StringLength) || !IsCharacterBoundary(str, StringLength, StartIndex))
    {
        return Error_CreateSuccess();
    }

    if (options._direction == StringMoveDirection_Backwards)
    {
        return FindPreviousOccurrence(str,
            StringLength,
            target,
            TargetLength,
            StartIndex,
            options._caseRule,
            unicode,
            outIndex);
    }

    return FindNextOccurrence(str,
        StringLength,
        target,
        TargetLength,
        StartIndex,
        options._caseRule,
        unicode,
        outIndex);
}

Error StringUTF8_Concat(const unsigned char* strA,
    const unsigned char* strB,
    GenericBuffer* destination)
{
    size_t LengthA = 0;
    size_t LengthB = 0;
    Error Result = ValidateStringPairArguments(strA, strB);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    LengthA = StringUTF8_GetByteLength(strA);
    LengthB = StringUTF8_GetByteLength(strB);
    if (LengthA > (SIZE_MAX - LengthB - 1))
    {
        return CreateOverflowError(u8"concatenate strings");
    }

    Result = EnsureBufferCapacity(destination, LengthA + LengthB + 1, u8"concatenate strings");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = AppendBytes(destination, strA, LengthA, u8"concatenate strings");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = AppendBytes(destination, strB, LengthB, u8"concatenate strings");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"concatenate strings", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_Contains(const unsigned char* str,
    const unsigned char* target,
    size_t startIndex,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue)
{
    size_t FoundIndex = STRING_INDEX_INVALID;
    StringIndexOfOptions Options = String_CreateIndexOptions(caseRule, StringMoveDirection_Forwards, (int64_t)startIndex);
    Error Result = Error_CreateSuccess();

    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    *outValue = false;
    Result = ValidateStringPairArguments(str, target);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (caseRule == StringCaseRule_CaseIgnore)
    {
        Result = ValidateUnicode(unicode);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    if (caseRule == StringCaseRule_CaseIgnore)
    {
        size_t StringLength = StringUTF8_GetByteLength(str);
        size_t TargetLength = StringUTF8_GetByteLength(target);

        if (!IsCharacterBoundary(str, StringLength, startIndex))
        {
            return Error_CreateSuccess();
        }

        Result = FindNextOccurrence(str,
            StringLength,
            target,
            TargetLength,
            startIndex,
            caseRule,
            unicode,
            &FoundIndex);
    }
    else
    {
        Result = StringUTF8_IndexOf(str, target, Options, unicode, &FoundIndex);
    }

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outValue = (FoundIndex != STRING_INDEX_INVALID);
    return Error_CreateSuccess();
}

Error StringUTF8_Count(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    size_t* count)
{
    size_t StringLength = 0;
    size_t TargetLength = 0;
    size_t SearchIndex = 0;
    size_t MatchCount = 0;
    Error Result = ValidateStringPairArguments(str, target);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (count == NULL)
    {
        return CreateNullArgumentError(u8"count");
    }

    *count = 0;
    if (caseRule == StringCaseRule_CaseIgnore)
    {
        Result = ValidateUnicode(unicode);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    TargetLength = StringUTF8_GetByteLength(target);
    if (TargetLength == 0)
    {
        return Error_CreateSuccess();
    }

    StringLength = StringUTF8_GetByteLength(str);
    while (SearchIndex < StringLength)
    {
        size_t MatchIndex = STRING_INDEX_INVALID;

        Result = FindNextOccurrence(str,
            StringLength,
            target,
            TargetLength,
            SearchIndex,
            caseRule,
            unicode,
            &MatchIndex);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (MatchIndex == STRING_INDEX_INVALID)
        {
            break;
        }

        MatchCount++;
        SearchIndex = MatchIndex + TargetLength;
    }

    *count = MatchCount;
    return Error_CreateSuccess();
}

Error StringUTF8_EndsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue)
{
    size_t StringLength = 0;
    size_t TargetLength = 0;
    bool Matches = false;
    Error Result = ValidateStringPairArguments(str, target);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    *outValue = false;
    if (caseRule == StringCaseRule_CaseIgnore)
    {
        Result = ValidateUnicode(unicode);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    StringLength = StringUTF8_GetByteLength(str);
    TargetLength = StringUTF8_GetByteLength(target);
    if (TargetLength == 0)
    {
        *outValue = true;
        return Error_CreateSuccess();
    }
    if (TargetLength > StringLength)
    {
        return Error_CreateSuccess();
    }
    if (!IsCharacterBoundary(str, StringLength, StringLength - TargetLength))
    {
        return Error_CreateSuccess();
    }

    Result = MatchStringAt(str,
        StringLength,
        StringLength - TargetLength,
        target,
        TargetLength,
        caseRule,
        unicode,
        &Matches);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outValue = Matches;
    return Error_CreateSuccess();
}

Error StringUTF8_StartsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    bool* outValue)
{
    size_t StringLength = 0;
    size_t TargetLength = 0;
    bool Matches = false;
    Error Result = ValidateStringPairArguments(str, target);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    *outValue = false;
    if (caseRule == StringCaseRule_CaseIgnore)
    {
        Result = ValidateUnicode(unicode);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    StringLength = StringUTF8_GetByteLength(str);
    TargetLength = StringUTF8_GetByteLength(target);
    if (TargetLength == 0)
    {
        *outValue = true;
        return Error_CreateSuccess();
    }

    Result = MatchStringAt(str, StringLength, 0, target, TargetLength, caseRule, unicode, &Matches);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outValue = Matches;
    return Error_CreateSuccess();
}

Error StringUTF8_Format(const unsigned char* str,
    GenericBuffer* destination,
    ...)
{
    va_list ArgumentList;
    va_list ArgumentListCopy;
    int CharacterCount = 0;
    char* Buffer = NULL;
    Error Result = Error_CreateSuccess();

    if (str == NULL)
    {
        return CreateNullArgumentError(u8"str");
    }

    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    va_start(ArgumentList, destination);
    va_copy(ArgumentListCopy, ArgumentList);
    CharacterCount = vsnprintf(NULL, 0, (const char*)str, ArgumentListCopy);
    va_end(ArgumentListCopy);

    if (CharacterCount < 0)
    {
        va_end(ArgumentList);
        return Error_Construct1(ErrorCode_InvalidOperation, u8"Cannot format the string.");
    }
    if ((size_t)CharacterCount == SIZE_MAX)
    {
        va_end(ArgumentList);
        return CreateOverflowError(u8"format the string");
    }

    Buffer = Memory_Allocate((size_t)CharacterCount + 1);
    (void)vsnprintf(Buffer, (size_t)CharacterCount + 1, (const char*)str, ArgumentList);
    va_end(ArgumentList);

    Result = StringUTF8_CopyTo((const unsigned char*)Buffer, destination);
    Memory_Free(Buffer);
    return Result;
}

Error StringUTF8_Join(const unsigned char* separator,
    const unsigned char** sources,
    size_t sourcesSize,
    GenericBuffer* destination)
{
    size_t SeparatorLength = 0;
    Error Result = ValidateStringArgument(separator, u8"separator");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((sources == NULL) && (sourcesSize > 0))
    {
        return CreateNullArgumentError(u8"sources");
    }

    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    SeparatorLength = StringUTF8_GetByteLength(separator);
    for (size_t Index = 0; Index < sourcesSize; Index++)
    {
        size_t SourceLength = 0;

        Result = ValidateStringArgument(sources[Index], u8"sources");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        if (Index > 0)
        {
            Result = AppendBytes(destination, separator, SeparatorLength, u8"join strings");
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }

        SourceLength = StringUTF8_GetByteLength(sources[Index]);
        Result = AppendBytes(destination, sources[Index], SourceLength, u8"join strings");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"join strings", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_Replace(const unsigned char* str,
    const unsigned char* searchTarget,
    const unsigned char* replaceValue,
    GenericBuffer* destination)
{
    size_t StringLength = 0;
    size_t SearchLength = 0;
    size_t SearchIndex = 0;
    Error Result = ValidateStringPairArguments(str, searchTarget);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidateStringArgument(replaceValue, u8"replaceValue");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    SearchLength = StringUTF8_GetByteLength(searchTarget);
    if (SearchLength == 0)
    {
        return StringUTF8_CopyTo(str, destination);
    }

    StringLength = StringUTF8_GetByteLength(str);
    while (SearchIndex < StringLength)
    {
        size_t MatchIndex = STRING_INDEX_INVALID;
        size_t ReplaceLength = StringUTF8_GetByteLength(replaceValue);

        Result = FindNextOccurrence(str,
            StringLength,
            searchTarget,
            SearchLength,
            SearchIndex,
            StringCaseRule_MatchCase,
            NULL,
            &MatchIndex);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (MatchIndex == STRING_INDEX_INVALID)
        {
            Result = AppendBytes(destination, str + SearchIndex, StringLength - SearchIndex, u8"replace string content");
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            break;
        }

        Result = AppendBytes(destination, str + SearchIndex, MatchIndex - SearchIndex, u8"replace string content");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Result = AppendBytes(destination, replaceValue, ReplaceLength, u8"replace string content");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        SearchIndex = MatchIndex + SearchLength;
    }

    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"replace string content", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_Substring(const unsigned char* str,
    size_t startIndex,
    size_t endIndex,
    GenericBuffer* destination)
{
    size_t ByteLength = 0;
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    if ((startIndex > endIndex) || (endIndex > ByteLength))
    {
        return CreateIndexOutOfBoundsError(u8"take the string substring");
    }
    if (!IsCharacterBoundary(str, ByteLength, startIndex) || !IsCharacterBoundary(str, ByteLength, endIndex))
    {
        return CreateInvalidEncodingError(u8"take the string substring");
    }

    return StringUTF8_CopyToBySize(str + startIndex, endIndex - startIndex, destination);
}

Error StringUTF8_Trim(const unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    GenericBuffer* destination,
    UnicodeData* unicode)
{
    size_t StartIndex = 0;
    size_t Length = 0;
    Error Result = StringUTF8_GetTrimIndices(str, isStartTrimmed, isEndTrimmed, &StartIndex, &Length, unicode);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return StringUTF8_CopyToBySize(str + StartIndex, Length, destination);
}

Error StringUTF8_GetTrimIndices(const unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    size_t* startIndex,
    size_t* outLength,
    UnicodeData* unicode)
{
    size_t ByteLength = 0;
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ValidateUnicode(unicode);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (startIndex == NULL)
    {
        return CreateNullArgumentError(u8"startIndex");
    }
    if (outLength == NULL)
    {
        return CreateNullArgumentError(u8"outLength");
    }

    ByteLength = StringUTF8_GetByteLength(str);
    return GetTrimIndicesInRange(str,
        0,
        ByteLength,
        isStartTrimmed,
        isEndTrimmed,
        unicode,
        startIndex,
        outLength);
}

Error StringUTF8_Compare(const unsigned char* strA,
    const unsigned char* strB,
    ComparisonResult* result)
{
    size_t IndexA = 0;
    size_t IndexB = 0;
    size_t LengthA = 0;
    size_t LengthB = 0;
    Error ErrorResult = ValidateStringPairArguments(strA, strB);

    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }
    if (result == NULL)
    {
        return CreateNullArgumentError(u8"result");
    }

    LengthA = StringUTF8_GetByteLength(strA);
    LengthB = StringUTF8_GetByteLength(strB);

    while ((IndexA < LengthA) && (IndexB < LengthB))
    {
        StringCodePointData LeftCharacter;
        StringCodePointData RightCharacter;

        if (!TryReadCodePointAt(strA, LengthA, IndexA, &LeftCharacter)
            || !TryReadCodePointAt(strB, LengthB, IndexB, &RightCharacter))
        {
            return CreateInvalidEncodingError(u8"compare strings");
        }
        if (LeftCharacter.CodePoint != RightCharacter.CodePoint)
        {
            *result = Comparator_CompareInt32(LeftCharacter.CodePoint, RightCharacter.CodePoint);
            return Error_CreateSuccess();
        }

        IndexA += LeftCharacter.ByteCount;
        IndexB += RightCharacter.ByteCount;
    }

    *result = Comparator_CompareSizeT(LengthA, LengthB);
    return Error_CreateSuccess();
}

Error StringUTF8_Remove(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    GenericBuffer* destination)
{
    size_t StringLength = 0;
    size_t TargetLength = 0;
    size_t SearchIndex = 0;
    Error Result = ValidateStringPairArguments(str, target);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (caseRule == StringCaseRule_CaseIgnore)
    {
        Result = ValidateUnicode(unicode);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    TargetLength = StringUTF8_GetByteLength(target);
    if (TargetLength == 0)
    {
        return StringUTF8_CopyTo(str, destination);
    }

    StringLength = StringUTF8_GetByteLength(str);
    while (SearchIndex < StringLength)
    {
        size_t MatchIndex = STRING_INDEX_INVALID;

        Result = FindNextOccurrence(str,
            StringLength,
            target,
            TargetLength,
            SearchIndex,
            caseRule,
            unicode,
            &MatchIndex);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (MatchIndex == STRING_INDEX_INVALID)
        {
            Result = AppendBytes(destination, str + SearchIndex, StringLength - SearchIndex, u8"remove string content");
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            break;
        }

        Result = AppendBytes(destination, str + SearchIndex, MatchIndex - SearchIndex, u8"remove string content");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        SearchIndex = MatchIndex + TargetLength;
    }

    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"remove string content", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_Insert(const unsigned char* str,
    size_t index,
    const unsigned char* substring,
    GenericBuffer* destination)
{
    size_t ByteLength = 0;
    size_t SubstringLength = 0;
    Error Result = ValidateStringPairArguments(str, substring);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    SubstringLength = StringUTF8_GetByteLength(substring);
    if (index > ByteLength)
    {
        return CreateIndexOutOfBoundsError(u8"insert the substring");
    }
    if (!IsCharacterBoundary(str, ByteLength, index))
    {
        return CreateInvalidEncodingError(u8"insert the substring");
    }

    Result = AppendBytes(destination, str, index, u8"insert the substring");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = AppendBytes(destination, substring, SubstringLength, u8"insert the substring");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = AppendBytes(destination, str + index, ByteLength - index, u8"insert the substring");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"insert the substring", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_Reverse(const unsigned char* str, GenericBuffer* destination)
{
    GenericBuffer CharacterIndices;
    size_t ByteLength = 0;
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    CreateGrowableIndexBuffer(&CharacterIndices);

    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    Result = GetCharacterIndicesInternal(str, &CharacterIndices);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(CharacterIndices._data);
        return Result;
    }

    for (size_t Index = CharacterIndices._count; Index > 0; Index--)
    {
        size_t CharacterStart = ((size_t*)CharacterIndices._data)[Index - 1];
        size_t CharacterEnd = (Index == CharacterIndices._count)
            ? ByteLength
            : ((size_t*)CharacterIndices._data)[Index];

        Result = AppendBytes(destination,
            str + CharacterStart,
            CharacterEnd - CharacterStart,
            u8"reverse the string");
        if (Result.Code != ErrorCode_Success)
        {
            Memory_Free(CharacterIndices._data);
            return Result;
        }
    }

    Memory_Free(CharacterIndices._data);

    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"reverse the string", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_Repeat(const unsigned char* str, GenericBuffer* destination, size_t count)
{
    size_t ByteLength = 0;
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = PrepareByteBuffer(destination, u8"destination");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringUTF8_GetByteLength(str);
    if ((count > 0) && (ByteLength > ((SIZE_MAX - 1) / count)))
    {
        return CreateOverflowError(u8"repeat the string");
    }

    for (size_t Index = 0; Index < count; Index++)
    {
        Result = AppendBytes(destination, str, ByteLength, u8"repeat the string");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    if (!GenericBuffer_NullTerminate(destination))
    {
        return CreateBufferTooSmallError(u8"repeat the string", destination->_count + 1);
    }

    return Error_CreateSuccess();
}

Error StringUTF8_GetCharacterIndexArray(const unsigned char* str, GenericBuffer* indexArray)
{
    Error Result = ValidateStringArgument(str, u8"str");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return GetCharacterIndicesInternal(str, indexArray);
}
