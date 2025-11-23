#include "WRString.h"
#include "WRMemory.h"
#include "WRError.h"
#include "WRChar.h"
#include "WRUnicode.h"
#include "string.h"
#include "limits.h"


// Types.
typedef CodePoint (*CaseTransformer)(UnicodeData* unicode, CodePoint codePoint);


// Static functions.
static Error CreateBufferTooSmallError(ErrorMessagePool* errorPool, GenericBuffer* buffer)
{
    return Error_Construct3(errorPool,
        ErrorCode_BufferTooSmall,
        u8"The destination buffer of size %zu is too small to fit the resulting string.",
        buffer->_capacity * buffer->_elementSize);
}

static Error CreateBufferTooSmallForPointersError(ErrorMessagePool* errorPool, GenericBuffer* buffer)
{
    return Error_Construct3(errorPool,
        ErrorCode_BufferTooSmall,
        u8"The destination buffer of size %zu (capacity %zu) is too small to fit the resulting pointers.",
        buffer->_capacity * buffer->_elementSize, buffer->_capacity);
}

// static Error CreateCodePointNotDefinedError(ErrorMessagePool* errorPool, CodePoint codePoint)
// {
//     return Error_Construct3(errorPool,
//         ErrorCode_InvalidCodePoint,
//         u8"Codepoint '%d' in the string is not defined in the given Unicode database.",
//         codePoint);
// }

static Error CreateCodePointInvalidError(ErrorMessagePool* errorPool, CodePoint codePoint)
{
    return Error_Construct3(errorPool,
        ErrorCode_InvalidCodePoint,
        u8"Invalid codepoint '%d' in the string (possibly defined, but not legal in a string in any context).",
        codePoint);
}

static CodePoint TransformCaseToUpper(UnicodeData* unicode, CodePoint codePoint)
{
    return Unicode_ToUpper(unicode, codePoint);
}

static CodePoint TransformCaseToLower(UnicodeData* unicode, CodePoint codePoint)
{
    return Unicode_ToLower(unicode, codePoint);
}

static CodePoint TransformCaseToInverted(UnicodeData* unicode, CodePoint codePoint)
{
    return Unicode_IsLower(unicode, codePoint) ? Unicode_ToUpper(unicode, codePoint) : Unicode_ToLower(unicode, codePoint);
}

static Error TransformCase(const unsigned char* str,
    UnicodeData* unicode,
    GenericBuffer* destination,
    ErrorMessagePool* errorPool,
    CaseTransformer transformer)
{
    for (size_t i = 0; str[i] != '\0'; i += CharUTF8_GetByteCountChar(str))
    {
        CodePoint SourceChar = CharUTF8_GetCodePoint(str + i);
        CodePoint TransformedChar = (*transformer)(unicode, SourceChar);
        if (SourceChar == CODEPOINT_NONE)
        {
            return CreateCodePointInvalidError(errorPool, SourceChar);
        }

        if (!StringUTF8_WriteCodePointToBuffer(destination, TransformedChar))
        {
            return CreateBufferTooSmallError(errorPool, destination);
        }
    }

    if (!GenericBuffer_TryNullTerminate(destination))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }

    return Error_CreateSuccess();
}

static Error AddSplitString(unsigned char* fullStr,
    unsigned char* partStart,
    size_t delimeterStartIndex,
    StringSplitOptions splitOptions,
    GenericBuffer* resultPointers,
    ErrorMessagePool* errorPool,
    bool shouldBeTerminated,
    UnicodeData* unicode)
{
    if (shouldBeTerminated)
    {
        fullStr[delimeterStartIndex] = '\0';
    }

    unsigned char* TrimmedPartStart = partStart;
    if (splitOptions._splitType & StringSplitType_Trim)
    {
        size_t StartIndex, EndIndex;
        StringUTF8_GetTrimIndices(partStart, true, true, &StartIndex, &EndIndex, unicode, errorPool);
        TrimmedPartStart[EndIndex] = '\0';
        TrimmedPartStart = TrimmedPartStart + StartIndex;
    }

    if ((splitOptions._splitType & StringSplitType_SkipEmpty) && (TrimmedPartStart[0] == '\0'))
    {
        return Error_CreateSuccess();
    }

    if (!GenericBuffer_WriteVoidPtr(resultPointers, TrimmedPartStart))
    {
        return CreateBufferTooSmallForPointersError(errorPool, resultPointers);
    }

    return Error_CreateSuccess();
}

static Error CreateIndexOutOfRangeError(ErrorMessagePool* errorPool, size_t index, size_t strSize)
{
    return Error_Construct3(errorPool,
        ErrorCode_IndexOutOfBounds,
        u8"Index %zu out of range for a string of byte length %zu.",
        index, strSize);
}

static Error GetIndexOfIndices(const unsigned char* str,
    StringIndexOfOptions options,
    ErrorMessagePool* errorPool,
    size_t* outStartIndex,
    size_t* outEndIndex)
{
    *outStartIndex = 0;
    *outEndIndex = 0;

    size_t StringByteLength = StringUTF8_GetByteLength(str);
    if (options._startIndex >= StringByteLength)
    {
        return CreateIndexOutOfRangeError(errorPool, options._startIndex, StringByteLength);
    }
    
    if (!options._isStartIndexFromEnd)
    {
        *outStartIndex = options._startIndex;
        
    }
    else
    {
        *outStartIndex = StringByteLength - options._startIndex;
    }

    if (options._direction == StringMoveDirection_Forwards)
    {
        *outEndIndex = StringByteLength;
    }
    else
    {
        *outEndIndex = 0;
    }

    return Error_CreateSuccess();
}

static Error StringEqualsCaseSensitive(const unsigned char* a,
    const unsigned char* b,
    bool* outValue,
    ErrorMessagePool* errorPool)
{
    *outValue = false;
    size_t Index = 0;
    while (a[Index] != '\0')
    {
        CodePoint CodePointA = CharUTF8_GetCodePoint(a + Index);
        CodePoint CodePointB = CharUTF8_GetCodePoint(b + Index);
        if ((CodePointA == CODEPOINT_NONE) || (CodePointB == CODEPOINT_NONE))
        {
            return CreateCodePointInvalidError(errorPool, CodePointA == CODEPOINT_NONE ? CodePointA : CodePointB);
        }

        if (CodePointA != CodePointB)
        {
            return Error_CreateSuccess();
        }

        size_t CodePointSize = CharUTF8_GetByteCountCodepoint(CodePointA);
        if (CodePointSize == 0)
        {
            return CreateCodePointInvalidError(errorPool, CodePointA);
        }
        Index += CodePointSize;
    }

    *outValue = (a[Index] == '\0') && (b[Index] == '\0');
    return Error_CreateSuccess();
}

static Error StringEqualsCaseInsensitive(const unsigned char* a,
    const unsigned char* b,
    UnicodeData* unicode,
    bool* outValue,
    ErrorMessagePool* errorPool)
{
    *outValue = false;
    size_t IndexA = 0;
    size_t IndexB = 0;

    while ((a[IndexA] != '\0') && (b[IndexB] != '\0'))
    {
        CodePoint CodePointA = CharUTF8_GetCodePoint(a + IndexA);
        CodePoint CodePointB = CharUTF8_GetCodePoint(b + IndexB);
        if ((CodePointA == CODEPOINT_NONE) || (CodePointB == CODEPOINT_NONE))
        {
            return CreateCodePointInvalidError(errorPool, CodePointA == CODEPOINT_NONE ? CodePointA : CodePointB);
        }

        if (!Unicode_EqualsCaseIgnore(unicode, CodePointA, CodePointB))
        {
            return Error_CreateSuccess();
        }

        size_t SizeA = CharUTF8_GetByteCountCodepoint(CodePointA);
        size_t SizeB = CharUTF8_GetByteCountCodepoint(CodePointB);
        if ((SizeA == 0) || (SizeB == 0))
        {
            return CreateCodePointInvalidError(errorPool, SizeA == 0 ? CodePointA : CodePointB);
        }
        IndexA += SizeA;
        IndexB += SizeB;
    }

    *outValue = (a[IndexA] == '\0') && (b[IndexB] == '\0');
    return Error_CreateSuccess();
}

// Functions.
bool StringUTF8_IsEncodingValid(const unsigned char* str)
{
    size_t Index = 0;
    while (str[Index] != '\0')
    {
        const unsigned char* Character = str + Index;
        if (!CharUTF8_IsCharValid(Character))
        {
            return false;
        }

        size_t BytesInChar = CharUTF8_GetByteCountChar(Character);
        if (BytesInChar == 0)
        {
            return false;
        }

        Index += BytesInChar;
    }
    return true;
}

bool StringUTF8_IsCodepointsValid(const unsigned char* str, UnicodeData* unicode)
{
    size_t Index=  0;
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(str + i);
        if (!Unicode_IsDefined(unicode, TargetCodePoint))
        {
            return false;
        }

        size_t BytesInChar = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if (BytesInChar == 0)
        {
            return false;
        }

        Index += BytesInChar;
    }
    return true;
}

bool StringUTF8_IsValid(const unsigned char* str, UnicodeData* unicode)
{
    size_t Index=  0;
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        if (!CharUTF8_IsCharValid(str + i))
        {
            return false;
        }
        
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(str + i);
        if (!Unicode_IsDefined(unicode, TargetCodePoint))
        {
            return false;
        }

        size_t BytesInChar = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if (BytesInChar == 0)
        {
            return false;
        }

        Index += BytesInChar;
    }
    return true;
}

bool StringUTF8_IsNullOrEmpty(const unsigned char* str)
{
    return !str || (str[0] == '\0');
}

Error StringUTF8_IsNullOrWhitespace(const unsigned char* str, UnicodeData* unicode, ErrorMessagePool* errorPool, bool* outValue)
{
    *outValue = true;
    if (!str)
    {
        return Error_CreateSuccess();
    }

    size_t Index = 0;
    while (str[Index] != '\0')
    {
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(str + Index);
        if (TargetCodePoint == CODEPOINT_NONE)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }
        if (!Unicode_IsWhitespace(unicode, TargetCodePoint))
        {
            *outValue = false;
            break;
        }
        size_t ByteCountCodePoint = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if (ByteCountCodePoint == 0)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }
        Index += ByteCountCodePoint;
    }

    return Error_CreateSuccess();
}

Error StringUTF8_ToLower(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination, ErrorMessagePool* errorPool)
{
    return TransformCase(str, unicode, destination, errorPool, &TransformCaseToLower);
}

Error StringUTF8_ToUpper(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination, ErrorMessagePool* errorPool)
{
    return TransformCase(str, unicode, destination, errorPool, &TransformCaseToUpper);
}

Error StringUTF8_InvertCase(const unsigned char* str, UnicodeData* unicode, GenericBuffer* destination, ErrorMessagePool* errorPool)
{
    return TransformCase(str, unicode, destination, errorPool, &TransformCaseToInverted);
}

bool StringUTF8_WriteCodePointToBuffer(GenericBuffer* buffer, CodePoint codePoint)
{
    unsigned char ConvertedChar[CODEPOINT_BYTE_COUNT_MAX];
    size_t ByteCount = CharUTF8_WriteCodePoint(ConvertedChar, codePoint);
    if (ByteCount == 0)
    {
        return false;
    }
    return GenericBuffer_WriteStringBySize(buffer, ConvertedChar, ByteCount);
}

size_t StringUTF8_GetByteLength(const unsigned char* str)
{
    return strlen((char*)str);
}

size_t StringUTF8_GetCodePointLength(const unsigned char* str)
{
    size_t Length = 0;
    size_t Index = 0;
    while (str[Index] != '\0')
    {
        Index += CharUTF8_GetByteCountChar(str + Index);
        Length++;
    }
    return Length;
}

Error StringUTF8_Equals(const unsigned char* a,
    const unsigned char* b,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    bool* outValue)
{
    if (caseRule == StringCaseRule_MatchCase)
    {
        return StringEqualsCaseSensitive(a, b, outValue, errorPool);
    }
    return StringEqualsCaseInsensitive(a, b, unicode, outValue, errorPool);
}

Error StringUTF8_CopyTo(const unsigned char* source, GenericBuffer* destination, ErrorMessagePool* errorPool)
{
    if (!GenericBuffer_WriteString(destination, source) || !GenericBuffer_TryNullTerminate(destination))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }
    return Error_CreateSuccess();
}

Error StringUTF8_CopyToBySize(const unsigned char* source, size_t size, GenericBuffer* destination, ErrorMessagePool* errorPool)
{
    if (!GenericBuffer_WriteStringBySize(destination, source, size) || !GenericBuffer_TryNullTerminate(destination))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }
    return Error_CreateSuccess();
}

StringSplitOptions String_CreateSplitOptionsNormal()
{
    return String_CreateSplitOptionsAll(StringSplitType_None, SIZE_MAX);
}

StringSplitOptions String_CreateSplitOptionsTyped(StringSplitType type)
{
    return String_CreateSplitOptionsAll(type, SIZE_MAX);
}

StringSplitOptions String_CreateSplitOptionsAll(StringSplitType type, size_t maxSplits)
{
    return (StringSplitOptions) { ._splitType = type, ._stringCountLimit = maxSplits };
}

Error StringUTF8_Split(unsigned char* str,
    const unsigned char* delimeter,
    StringSplitOptions splitOptions,
    GenericBuffer* resultPointers,
    ErrorMessagePool* errorPool,
    UnicodeData* unicode)
{
    if ((delimeter[0] == '\0') || (splitOptions._stringCountLimit <= 1))
    {
        if ((splitOptions._stringCountLimit == 1) && !GenericBuffer_WriteVoidPtr(resultPointers, str))
        {
            return CreateBufferTooSmallForPointersError(errorPool, resultPointers);
        }
        return Error_CreateSuccess();
    }

    unsigned char* PartStart = str;
    size_t DelimeterIndex = 0;
    size_t DelimeterStartIndex = 0;
    size_t Index = 0;
    while (str[Index] != '\0')
    {
        CodePoint SourceCodePoint = CharUTF8_GetCodePoint(str + Index);
        CodePoint DelimeterCodePoint = CharUTF8_GetCodePoint(delimeter + DelimeterIndex);
        if ((SourceCodePoint == CODEPOINT_NONE) || (DelimeterCodePoint == CODEPOINT_NONE))
        {
            return CreateCodePointInvalidError(errorPool, SourceCodePoint == CODEPOINT_NONE ? SourceCodePoint : DelimeterCodePoint);
        }

        size_t SourceCodePointSize = CharUTF8_GetByteCountCodepoint(SourceCodePoint);
        if (SourceCodePointSize == 0)
        {
            return CreateCodePointInvalidError(errorPool, SourceCodePoint);
        }

        if (SourceCodePoint != DelimeterCodePoint)
        {
            DelimeterIndex = 0;
        }
        else
        {
            DelimeterStartIndex = (DelimeterIndex == 0) ? Index : DelimeterStartIndex;
            DelimeterIndex += SourceCodePointSize;
            if (delimeter[DelimeterIndex] == '\0')
            {
                Error Result = AddSplitString(str, PartStart, DelimeterStartIndex,
                    splitOptions, resultPointers, errorPool, true, unicode);
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
                str[DelimeterStartIndex] = '\0';
                PartStart = str + Index + SourceCodePointSize;
                continue;
            }
        }
        Index += SourceCodePointSize;
    }

    return AddSplitString(str, PartStart, DelimeterStartIndex,
        splitOptions, resultPointers, errorPool, false, unicode);
}

StringIndexOfOptions String_CreateIndexOptionsNormal()
{
    return String_CreateIndexOptions(StringCaseRule_MatchCase, StringMoveDirection_Forwards, 0, false);
}

StringIndexOfOptions String_CreateIndexOptionsFromEnd()
{
    return String_CreateIndexOptions(StringCaseRule_MatchCase, StringMoveDirection_Backwards, 0, true);
}

StringIndexOfOptions String_CreateIndexOptions(StringCaseRule caseRule,
    StringMoveDirection direction,
    size_t startIndex,
    bool isStartIndexFromEnd)
{
    return (StringIndexOfOptions)
    {
        ._caseRule = caseRule,
        ._direction = direction,
        ._isStartIndexFromEnd = isStartIndexFromEnd,
        ._startIndex = startIndex
    };
}

Error StringUTF8_IndexOf(const unsigned char* str,
    const unsigned char* target,
    StringIndexOfOptions options,
    ErrorMessagePool* errorPool,
    size_t* outIndex)
{
    *outIndex = STRING_INDEX_INVALID;
    if (target[0] == '\0')
    {
        return Error_CreateSuccess();
    }

    int32_t Step = options._direction == StringMoveDirection_Forwards ? 1 : -1;
    int32_t Offset = options._direction == StringMoveDirection_Forwards ? 0 : -1;

    size_t StrStartIndex, StrEndIndex;
    Error ErrorResult = GetIndexOfIndices(str, options, errorPool, &StrStartIndex, &StrEndIndex);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    size_t TargetStartIndex, TargetEndIndex;
    ErrorResult = GetIndexOfIndices(target, options, errorPool, &TargetStartIndex, &TargetEndIndex);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    size_t TargetStrIndex = TargetStartIndex;
    size_t FoundIndex = 0;
    for (size_t i = StrStartIndex; i != StrEndIndex;)
    {
        const unsigned char* CharPtr = str + i + Offset;
        CodePoint SourceCodePoint = options._direction == StringMoveDirection_Forwards ?
            CharUTF8_GetCodePoint(CharPtr) :
            CharUTF8_GetCodePointFromEnd(CharPtr, i);
        if (SourceCodePoint == CODEPOINT_NONE)
        {
            return CreateCodePointInvalidError(errorPool, SourceCodePoint);
        }

        const unsigned char* TargetPtr = TargetPtr + TargetStrIndex + Offset;
        CodePoint TargetCodePoint = options._direction == StringMoveDirection_Forwards ?
            CharUTF8_GetCodePoint(CharPtr) :
            CharUTF8_GetCodePointFromEnd(CharPtr, i);
        if (TargetCodePoint == CODEPOINT_NONE)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }
        
        size_t SourceCharSize = CharUTF8_GetByteCountCodepoint(SourceCodePoint);
        size_t TargetCharSize = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if ((SourceCharSize == 0) || (TargetCharSize == 0))
        {
            return CreateCodePointInvalidError(errorPool, SourceCharSize == 0 ? SourceCodePoint : TargetCodePoint);
        }

        i += SourceCharSize * Step;
        if (SourceCodePoint != TargetCodePoint)
        {
            TargetStrIndex = TargetStartIndex;
        }
        else
        {
            if ((i < FoundIndex) || (TargetStrIndex == TargetStartIndex))
            {
                FoundIndex = i;
            }
            TargetStrIndex += TargetCharSize * Step;
            if ((TargetStrIndex == TargetEndIndex))
            {
                *outIndex = FoundIndex;
                return Error_CreateSuccess();
            }
        }
    }

    return Error_CreateSuccess();
}

Error StringUTF8_Concat(const unsigned char* strA,
    const unsigned char* strB,
    GenericBuffer* destination,
    ErrorMessagePool* errorPool)
{
    if (!GenericBuffer_WriteString(destination, strA) || !GenericBuffer_WriteString(destination, strB))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }
    return Error_CreateSuccess();
}

Error StringUTF8_Contains(const unsigned char* str,
    const unsigned char* target,
    size_t startIndex,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    bool* outValue)
{
    (void)startIndex;

    *outValue = false;
    if (target[0] == '\0')
    {
        *outValue = true;
        return Error_CreateSuccess();
    }

    size_t Index = 0;
    size_t TargetIndex = 0;
    while (str[Index] != '\0')
    {
        CodePoint SourceCodePoint = CharUTF8_GetCodePoint(str + Index);
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(target + TargetIndex);

        bool IsEqual = (caseRule == StringCaseRule_MatchCase) ?
            (SourceCodePoint == TargetCodePoint) :
            Unicode_EqualsCaseIgnore(unicode, SourceCodePoint, TargetCodePoint);

        size_t SourceCharSize = CharUTF8_GetByteCountCodepoint(SourceCodePoint);
        size_t TargetCharSize = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if ((SourceCharSize == 0) || (TargetCharSize == 0))
        {
            return CreateCodePointInvalidError(errorPool, SourceCharSize == 0 ? SourceCodePoint : TargetCodePoint);
        }

        if (!IsEqual)
        {
            TargetIndex = 0;
        }
        else
        {
            TargetIndex += TargetCharSize;
            if (target[TargetIndex] == '\0')
            {
                *outValue = true;
                return Error_CreateSuccess();
            }
        }

        Index += SourceCharSize;
    }

    return Error_CreateSuccess();
}

Error StringUTF8_Count(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    size_t* count)
{
    *count = 0;

    if (target[0] == '\0')
    {
        return Error_CreateSuccess();   
    }

    size_t Index = 0;
    size_t TargetIndex = 0;
    size_t FoundCount = 0;
    while (str[Index] != '\0')
    {
        CodePoint SourceCodePoint = CharUTF8_GetCodePoint(str + Index);
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(target + TargetIndex);

        bool IsEqual = (caseRule == StringCaseRule_MatchCase) ?
            (SourceCodePoint == TargetCodePoint) :
            Unicode_EqualsCaseIgnore(unicode, SourceCodePoint, TargetCodePoint);

        size_t SourceCharSize = CharUTF8_GetByteCountCodepoint(SourceCodePoint);
        size_t TargetCharSize = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if ((SourceCharSize == 0) || (TargetCharSize == 0))
        {
            return CreateCodePointInvalidError(errorPool, SourceCharSize == 0 ? SourceCodePoint : TargetCodePoint);
        }

        if (!IsEqual)
        {
            TargetIndex = 0;
        }
        else
        {
            TargetIndex += TargetCharSize;
            if (target[TargetIndex] == '\0')
            {
                FoundCount++;
                TargetIndex = 0;
            }
        }

        Index += SourceCharSize;
    }

    *count = FoundCount;
    return Error_CreateSuccess();
}

Error StringUTF8_StartsWith(const unsigned char* str,
    const unsigned char* target,
    StringCaseRule caseRule,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool,
    bool* outValue)
{
    *outValue = false;
    if ((str[0] == '\0') || (target[0] == '\0'))
    {
        *outValue = true;
        return Error_CreateSuccess();
    }

    size_t Index = 0;
    while (str[Index] != '\0')
    {
        CodePoint SourceCodePoint = CharUTF8_GetCodePoint(str + Index);
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(target + Index);
        if ((SourceCodePoint == CODEPOINT_NONE) || (TargetCodePoint == CODEPOINT_NONE))
        {
            return CreateCodePointInvalidError(errorPool, SourceCodePoint == CODEPOINT_NONE ? SourceCodePoint : TargetCodePoint);
        }

        bool IsEqual = (caseRule == StringCaseRule_MatchCase) ?
            (SourceCodePoint == TargetCodePoint) :
            Unicode_EqualsCaseIgnore(unicode, SourceCodePoint, TargetCodePoint);

        if (!IsEqual)
        {
            *outValue = false;
            return Error_CreateSuccess();
        }


    }

    return Error_CreateSuccess();
}

Error StringUTF8_Reverse(const unsigned char* str, GenericBuffer* destination, ErrorMessagePool* errorPool)
{
    GenericBuffer_Clear(destination);

    size_t StringLength = StringUTF8_GetByteLength(str);
    if (!GenericBuffer_ReserveCapacity(destination, StringLength + 1))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }

    unsigned char* DestBuffer = destination->_data;

    for (size_t i = 0; i < StringLength;)
    {
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(str + i);
        if (TargetCodePoint == CODEPOINT_NONE)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }
        size_t CodePointSize = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if (CodePointSize == 0)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }
        
        CharUTF8_WriteCodePoint(DestBuffer + (StringLength - i - CodePointSize), TargetCodePoint);
        i += CodePointSize;
    }

    if (!GenericBuffer_TryNullTerminate(destination))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }

    return Error_CreateSuccess();
}


Error StringUTF8_Repeat(const unsigned char* str, GenericBuffer* destination, size_t count, ErrorMessagePool* errorPool)
{
    size_t StrSize = StringUTF8_GetByteLength(str);
    if (StrSize != 0)
    {
        for (size_t i = 0; i < count; i++)
        {
            if (!GenericBuffer_WriteStringBySize(destination, str, StrSize))
            {
                return CreateBufferTooSmallError(errorPool, destination);
            }
        }
    }

    if (!GenericBuffer_TryNullTerminate(destination))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }
    return Error_CreateSuccess();
}

Error StringUTF8_GetCharacterIndexArray(const unsigned char* str, GenericBuffer* indexArray, ErrorMessagePool* errorPool)
{
    size_t IndexArrayIndex = 0;
    size_t StrIndex = 0;
    while (str[StrIndex] != '\0')
    {
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(str + StrIndex);
        if (TargetCodePoint == CODEPOINT_NONE)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }
        size_t CodePointSize = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if (CodePointSize == 0)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }
        if (!GenericBuffer_WriteSizeT(indexArray, IndexArrayIndex))
        {
            return CreateBufferTooSmallError(errorPool, indexArray);
        }
        StrIndex += CodePointSize;
        IndexArrayIndex++;
    }

    return Error_CreateSuccess();
}

Error StringUTF8_GetTrimIndices(unsigned char* str,
    bool isStartTrimmed,
    bool isEndTrimmed,
    size_t* startIndex,
    size_t* endIndex,
    UnicodeData* unicode,
    ErrorMessagePool* errorPool)
{
    *startIndex = 0;
    *endIndex = 0;

    size_t Index = 0;
    size_t PreTrimEndIndex = 0;
    size_t PostTrimStartIndex = 0;
    while (str[Index] != '\0')
    {
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(str + Index);
        if (TargetCodePoint == CODEPOINT_NONE)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }

        size_t CodePointSize = CharUTF8_GetByteCountCodepoint(TargetCodePoint);
        if (CodePointSize == 0)
        {
            return CreateCodePointInvalidError(errorPool, TargetCodePoint);
        }

        if (!Unicode_IsWhitespace(unicode, TargetCodePoint))
        {
            if (PreTrimEndIndex == STRING_INDEX_INVALID)
            {
                PreTrimEndIndex = Index;
            }
            PostTrimStartIndex = Index + CodePointSize;
        }
        Index += CodePointSize;
    }

    *startIndex = isStartTrimmed ? PreTrimEndIndex : 0;
    *endIndex = isEndTrimmed ? PostTrimStartIndex : Index;
    return Error_CreateSuccess();
}