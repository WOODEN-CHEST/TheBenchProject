#include "WRString.h"
#include "WRMemory.h"
#include "WRError.h"
#include "WRChar.h"
#include "WRUnicode.h"
#include "string.h"


// Types.
typedef CodePoint (*CaseTransformer)(UnicodeData* unicode, CodePoint* codePoint);


// Static functions.
static Error CreateBufferTooSmallError(ErrorMessagePool* errorPool, GenericBuffer* buffer)
{
    ErrorCode Code = ErrorCode_BufferTooSmall;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"The destination buffer of size %zu is too small to fit the resulting string.",
            buffer->_capacity * buffer->_elementSize);
    }
    return Error_Construct5(Code);
}

static Error CreateCodePointNotDefinedError(ErrorMessagePool* errorPool, CodePoint codePoint)
{
    ErrorCode Code = ErrorCode_InvalidCodePoint;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Undefined codepoint '%d'.",
            codePoint);
    }
    return Error_Construct5(Code);
}

static CodePoint TransformCaseToUpper(UnicodeData* unicode, CodePoint* codePoint)
{
    return Unicode_ToUpper(unicode, codePoint);
}

static CodePoint TransformCaseToLower(UnicodeData* unicode, CodePoint* codePoint)
{
    return Unicode_ToLower(unicode, codePoint);
}

static CodePoint TransformCaseToInverted(UnicodeData* unicode, CodePoint* codePoint)
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
            return CreateCodePointNotDefinedError(errorPool, SourceChar);
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

bool StringUTF8_IsNullOrWhitespace(const unsigned char* str, UnicodeData* unicode)
{
    if (!str)
    {
        return true;
    }

    size_t Index = 0;
    while (str[Index] != '\0')
    {
        CodePoint TargetCodePoint = CharUTF8_GetCodePoint(str + Index);
        if (!Unicode_IsWhitespace(unicode, TargetCodePoint))
        {
            return false;
        }
        Index += CharUTF8_GetByteCountCodepoint(TargetCodePoint);
    }
    return true;
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

bool WriteCodePointToBuffer(GenericBuffer* buffer, CodePoint* codePoint)
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
    return strlen(str);
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

bool StringUTF8_Equals(const unsigned char* a, const unsigned char* b, StringCaseRule caseRule, UnicodeData* unicode)
{
    if (caseRule == StringCaseRule_MatchCase)
    {
        return !strcmp(a, b);
    }

    size_t IndexA = 0;
    size_t IndexB = 0;

    while ((a[IndexA] != '\0') && (b[IndexB] != '\0'))
    {
        CodePoint CodePointA = CharUTF8_GetCodePoint(a + IndexA);
        CodePoint CodePointB = CharUTF8_GetCodePoint(b + IndexB);

        CodePoint LowerA = Unicode_ToLower(unicode, CodePointA);
        CodePoint LowerB = Unicode_ToLower(unicode, CodePointB);
        if (LowerA != LowerB)
        {
            return false;
        }

        IndexA += CharUTF8_GetByteCountCodepoint(CodePointA);
        IndexB += CharUTF8_GetByteCountCodepoint(CodePointB);
    }

    return (a[IndexA] == '\0') && (b[IndexB] == '\0');
}

Error StringUTF8_CopyTo(const unsigned char* source, GenericBuffer* destination, ErrorMessagePool* errorPool)
{
    if (!GenericBuffer_WriteString(destination, source) || GenericBuffer_TryNullTerminate(destination))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }
    return Error_CreateSuccess();
}

Error StringUTF8_CopyToBySize(const unsigned char* source, size_t size, GenericBuffer* destination, ErrorMessagePool* errorPool)
{
    if (!GenericBuffer_WriteStringBySize(destination, source, size) || GenericBuffer_TryNullTerminate(destination))
    {
        return CreateBufferTooSmallError(errorPool, destination);
    }
    return Error_CreateSuccess();
}