#include "WRChar.h"
#include <stddef.h>

// https://en.wikipedia.org/wiki/UTF-8
// https://en.wikipedia.org/wiki/UTF-16


// Fields.
static const unsigned char UTF8_1B_FIRST_BYTE_VALUE_MASK = 0x80;
static const unsigned char UTF8_1B_FIRST_BYTE_VALUE = 0x00;
static const CodePoint UTF8_1B_MAX_CODEPOINT = 0x7F;

static const unsigned char UTF8_2B_FIRST_BYTE_VALUE_MASK = 0xE0;
static const unsigned char UTF8_2B_FIRST_BYTE_VALUE = 0xC0;
static const CodePoint UTF8_2B_MAX_CODEPOINT = 0x7FF;

static const unsigned char UTF8_3B_FIRST_BYTE_VALUE_MASK = 0xF0;
static const unsigned char UTF8_3B_FIRST_BYTE_VALUE = 0xE0;
static const CodePoint UTF8_3B_MAX_CODEPOINT = 0xFFFF;

static const unsigned char UTF8_4B_FIRST_BYTE_VALUE_MASK = 0xF8;
static const unsigned char UTF8_4B_FIRST_BYTE_VALUE = 0xF0;
static const unsigned char UTF8_TRAIL_VALUE_MASK = 0xC0;
static const unsigned char UTF8_TRAIL_VALUE = 0x80;
static const size_t UTF8_TRAIL_BIT_COUNT = 6;
static const CodePoint UTF8_PAYLOAD_MASK = 0x3F;

static const CodePoint SURROGATE_HIGH_MIN = 0xD800;
static const CodePoint SURROGATE_HIGH_MAX = 0xDBFF;
static const CodePoint SURROGATE_LOW_MIN = 0xDC00;
static const CodePoint SURROGATE_LOW_MAX = 0xDFFF;
static const CodePoint UTF16_BMP_MAX_CODEPOINT = 0xFFFF;
static const CodePoint CODEPOINT_MIN = 0x0;
static const CodePoint CODEPOINT_MAX = 0x10FFFF;
static const CodePoint UTF16_SUPPLEMENTARY_OFFSET = 0x10000;


// Static functions.
static inline bool Is1ByteUTF8(unsigned char firstByte)
{
    return (firstByte & UTF8_1B_FIRST_BYTE_VALUE_MASK) == UTF8_1B_FIRST_BYTE_VALUE;
}

static inline bool Is2ByteUTF8(unsigned char firstByte)
{
    return (firstByte & UTF8_2B_FIRST_BYTE_VALUE_MASK) == UTF8_2B_FIRST_BYTE_VALUE;
}

static inline bool Is3ByteUTF8(unsigned char firstByte)
{
    return (firstByte & UTF8_3B_FIRST_BYTE_VALUE_MASK) == UTF8_3B_FIRST_BYTE_VALUE;
}

static inline bool Is4ByteUTF8(unsigned char firstByte)
{
    return (firstByte & UTF8_4B_FIRST_BYTE_VALUE_MASK) == UTF8_4B_FIRST_BYTE_VALUE;
}

static inline bool IsUTF8TrailByte(unsigned char byte)
{
    return (byte & UTF8_TRAIL_VALUE_MASK) == UTF8_TRAIL_VALUE;
}

static inline bool IsInLowSurrogateRange(CodePoint codepoint)
{
    return (SURROGATE_LOW_MIN <= codepoint) && (codepoint <= SURROGATE_LOW_MAX);
}

static inline bool IsInHighSurrogateRange(CodePoint codepoint)
{
    return (SURROGATE_HIGH_MIN <= codepoint) && (codepoint <= SURROGATE_HIGH_MAX);
}

static inline bool IsInSurrogateRange(CodePoint codepoint)
{
    return IsInHighSurrogateRange(codepoint) || IsInLowSurrogateRange(codepoint);
}

static inline bool IsInUnicodeRange(CodePoint codepoint)
{
    return (CODEPOINT_MIN <= codepoint) && (codepoint <= CODEPOINT_MAX);
}

static inline bool IsUTF16HighSurrogate(uint16_t value)
{
    return ((CodePoint)value >= SURROGATE_HIGH_MIN) && ((CodePoint)value <= SURROGATE_HIGH_MAX);
}

static inline bool IsUTF16LowSurrogate(uint16_t value)
{
    return ((CodePoint)value >= SURROGATE_LOW_MIN) && ((CodePoint)value <= SURROGATE_LOW_MAX);
}

static size_t GetUTF8SequenceLength(const unsigned char* character)
{
    if (character == NULL)
    {
        return 0;
    }

    unsigned char FirstByte = character[0];
    if (Is1ByteUTF8(FirstByte))
    {
        return 1;
    }
    if (Is2ByteUTF8(FirstByte))
    {
        return 2;
    }
    if (Is3ByteUTF8(FirstByte))
    {
        return 3;
    }
    if (Is4ByteUTF8(FirstByte))
    {
        return 4;
    }

    return 0;
}

static bool TryDecodeUTF8(const unsigned char* character, size_t bufferLength, CodePoint* codepointOut, size_t* byteCountOut)
{
    if ((character == NULL) || (bufferLength == 0))
    {
        return false;
    }

    size_t ByteCount = GetUTF8SequenceLength(character);
    if ((ByteCount == 0) || (ByteCount > bufferLength))
    {
        return false;
    }

    for (size_t i = 1; i < ByteCount; i++)
    {
        if (!IsUTF8TrailByte(character[i]))
        {
            return false;
        }
    }

    CodePoint CreatedCodePoint = CODEPOINT_NONE;
    if (ByteCount == 1)
    {
        CreatedCodePoint = (CodePoint)character[0];
    }
    else if (ByteCount == 2)
    {
        CreatedCodePoint = (CodePoint)(
            (((CodePoint)(character[0] & 0x1F)) << UTF8_TRAIL_BIT_COUNT)
            | ((CodePoint)(character[1] & UTF8_PAYLOAD_MASK)));
    }
    else if (ByteCount == 3)
    {
        CreatedCodePoint = (CodePoint)(
            (((CodePoint)(character[0] & 0x0F)) << (UTF8_TRAIL_BIT_COUNT * 2))
            | (((CodePoint)(character[1] & UTF8_PAYLOAD_MASK)) << UTF8_TRAIL_BIT_COUNT)
            | ((CodePoint)(character[2] & UTF8_PAYLOAD_MASK)));
    }
    else
    {
        CreatedCodePoint = (CodePoint)(
            (((CodePoint)(character[0] & 0x07)) << (UTF8_TRAIL_BIT_COUNT * 3))
            | (((CodePoint)(character[1] & UTF8_PAYLOAD_MASK)) << (UTF8_TRAIL_BIT_COUNT * 2))
            | (((CodePoint)(character[2] & UTF8_PAYLOAD_MASK)) << UTF8_TRAIL_BIT_COUNT)
            | ((CodePoint)(character[3] & UTF8_PAYLOAD_MASK)));
    }

    if (!CharUTF8_IsCodePointValid(CreatedCodePoint))
    {
        return false;
    }

    if (ByteCount != CharUTF8_GetByteCountCodepoint(CreatedCodePoint))
    {
        return false;
    }

    if (codepointOut != NULL)
    {
        *codepointOut = CreatedCodePoint;
    }

    if (byteCountOut != NULL)
    {
        *byteCountOut = ByteCount;
    }

    return true;
}

static size_t GetUTF16SequenceLength(const uint16_t* character)
{
    if (character == NULL)
    {
        return 0;
    }

    if (IsUTF16HighSurrogate(character[0]))
    {
        return 2;
    }

    if (IsUTF16LowSurrogate(character[0]))
    {
        return 0;
    }

    return 1;
}

static CodePoint DecodeUTF16SurrogatePair(uint16_t highSurrogate, uint16_t lowSurrogate)
{
    return (CodePoint)(
        ((((CodePoint)highSurrogate) - SURROGATE_HIGH_MIN) << 10)
        | (((CodePoint)lowSurrogate) - SURROGATE_LOW_MIN))
        + UTF16_SUPPLEMENTARY_OFFSET;
}

static bool TryDecodeUTF16(const uint16_t* character, size_t bufferLength, CodePoint* codepointOut, size_t* wordCountOut)
{
    if ((character == NULL) || (bufferLength == 0))
    {
        return false;
    }

    size_t WordCount = GetUTF16SequenceLength(character);
    if ((WordCount == 0) || (WordCount > bufferLength))
    {
        return false;
    }

    CodePoint CreatedCodePoint = CODEPOINT_NONE;
    if (WordCount == 1)
    {
        CreatedCodePoint = (CodePoint)character[0];
    }
    else
    {
        if (!IsUTF16LowSurrogate(character[1]))
        {
            return false;
        }

        CreatedCodePoint = DecodeUTF16SurrogatePair(character[0], character[1]);
    }

    if (!CharUTF16_IsCodePointValid(CreatedCodePoint))
    {
        return false;
    }

    if (WordCount != CharUTF16_GetWordCountCodepoint(CreatedCodePoint))
    {
        return false;
    }

    if (codepointOut != NULL)
    {
        *codepointOut = CreatedCodePoint;
    }

    if (wordCountOut != NULL)
    {
        *wordCountOut = WordCount;
    }

    return true;
}


// Public functions.
bool CharUTF8_IsCodePointValid(CodePoint codepoint)
{
    return !IsInSurrogateRange(codepoint) && IsInUnicodeRange(codepoint);
}

bool CharUTF8_IsCharValid(const unsigned char* character)
{
    return CharUTF8_IsCharBufferValid(character, CODEPOINT_BYTE_COUNT_MAX);
}

bool CharUTF8_IsCharBufferValid(const unsigned char* character, size_t bufferLength)
{
    return TryDecodeUTF8(character, bufferLength, NULL, NULL);
}

size_t CharUTF8_GetByteCountChar(const unsigned char* character)
{
    size_t ByteCount = 0;
    if (!TryDecodeUTF8(character, CODEPOINT_BYTE_COUNT_MAX, NULL, &ByteCount))
    {
        return 0;
    }

    return ByteCount;
}

size_t CharUTF8_GetByteCountCharFromEnd(const unsigned char* characterLastByte, size_t remainingPreBytes)
{
    if (characterLastByte == NULL)
    {
        return 0;
    }

    size_t MaxBacktrack = remainingPreBytes;
    if (MaxBacktrack > (CODEPOINT_BYTE_COUNT_MAX - 1))
    {
        MaxBacktrack = CODEPOINT_BYTE_COUNT_MAX - 1;
    }

    for (size_t StartOffset = 0; StartOffset <= MaxBacktrack; StartOffset++)
    {
        const unsigned char* CharacterStart = characterLastByte - StartOffset;
        size_t ByteCount = 0;
        if (!TryDecodeUTF8(CharacterStart, StartOffset + 1, NULL, &ByteCount))
        {
            continue;
        }

        if ((ByteCount - 1) == StartOffset)
        {
            return ByteCount;
        }
    }

    return 0;
}

size_t CharUTF8_GetByteCountCodepoint(CodePoint codepoint)
{
    if (!CharUTF8_IsCodePointValid(codepoint))
    {
        return 0;
    }

    if (codepoint <= UTF8_1B_MAX_CODEPOINT)
    {
        return 1;
    }

    if (codepoint <= UTF8_2B_MAX_CODEPOINT)
    {
        return 2;
    }

    if (codepoint <= UTF8_3B_MAX_CODEPOINT)
    {
        return 3;
    }

    return 4;
}

CodePoint CharUTF8_GetCodePointFromEnd(const unsigned char* characterLastByte, size_t remainingPreBytes)
{
    if (characterLastByte == NULL)
    {
        return CODEPOINT_NONE;
    }

    size_t MaxBacktrack = remainingPreBytes;
    if (MaxBacktrack > (CODEPOINT_BYTE_COUNT_MAX - 1))
    {
        MaxBacktrack = CODEPOINT_BYTE_COUNT_MAX - 1;
    }

    for (size_t StartOffset = 0; StartOffset <= MaxBacktrack; StartOffset++)
    {
        const unsigned char* CharacterStart = characterLastByte - StartOffset;
        CodePoint Result = CODEPOINT_NONE;
        size_t ByteCount = 0;
        if (!TryDecodeUTF8(CharacterStart, StartOffset + 1, &Result, &ByteCount))
        {
            continue;
        }

        if ((ByteCount - 1) == StartOffset)
        {
            return Result;
        }
    }

    return CODEPOINT_NONE;
}

size_t CharUTF8_WriteCodePoint(unsigned char* character, CodePoint codepoint)
{
    if ((character == NULL) || !CharUTF8_IsCodePointValid(codepoint))
    {
        return 0;
    }

    if (codepoint <= UTF8_1B_MAX_CODEPOINT)
    {
        character[0] = (unsigned char)codepoint;
        return 1;
    }

    if (codepoint <= UTF8_2B_MAX_CODEPOINT)
    {
        character[0] = (unsigned char)(UTF8_2B_FIRST_BYTE_VALUE | ((unsigned char)(codepoint >> UTF8_TRAIL_BIT_COUNT)));
        character[1] = (unsigned char)(UTF8_TRAIL_VALUE | ((unsigned char)codepoint & UTF8_PAYLOAD_MASK));
        return 2;
    }

    if (codepoint <= UTF8_3B_MAX_CODEPOINT)
    {
        character[0] = (unsigned char)(UTF8_3B_FIRST_BYTE_VALUE | ((unsigned char)(codepoint >> (UTF8_TRAIL_BIT_COUNT * 2))));
        character[1] = (unsigned char)(UTF8_TRAIL_VALUE | ((unsigned char)(codepoint >> UTF8_TRAIL_BIT_COUNT) & UTF8_PAYLOAD_MASK));
        character[2] = (unsigned char)(UTF8_TRAIL_VALUE | ((unsigned char)codepoint & UTF8_PAYLOAD_MASK));
        return 3;
    }

    character[0] = (unsigned char)(UTF8_4B_FIRST_BYTE_VALUE | ((unsigned char)(codepoint >> (UTF8_TRAIL_BIT_COUNT * 3))));
    character[1] = (unsigned char)(UTF8_TRAIL_VALUE | ((unsigned char)(codepoint >> (UTF8_TRAIL_BIT_COUNT * 2)) & UTF8_PAYLOAD_MASK));
    character[2] = (unsigned char)(UTF8_TRAIL_VALUE | ((unsigned char)(codepoint >> UTF8_TRAIL_BIT_COUNT) & UTF8_PAYLOAD_MASK));
    character[3] = (unsigned char)(UTF8_TRAIL_VALUE | ((unsigned char)codepoint & UTF8_PAYLOAD_MASK));
    return 4;
}

CodePoint CharUTF8_GetCodePoint(const unsigned char* character)
{
    CodePoint Result = CODEPOINT_NONE;
    if (!TryDecodeUTF8(character, CODEPOINT_BYTE_COUNT_MAX, &Result, NULL))
    {
        return CODEPOINT_NONE;
    }

    return Result;
}

bool CharUTF16_IsCodePointValid(CodePoint codepoint)
{
    return !IsInSurrogateRange(codepoint) && IsInUnicodeRange(codepoint);
}

bool CharUTF16_IsCharValid(const uint16_t* character)
{
    return CharUTF16_IsCharBufferValid(character, CODEPOINT_WORD16_COUNT_MAX);
}

bool CharUTF16_IsCharBufferValid(const uint16_t* character, size_t bufferLength)
{
    return TryDecodeUTF16(character, bufferLength, NULL, NULL);
}

size_t CharUTF16_GetWordCountChar(const uint16_t* character)
{
    size_t WordCount = 0;
    if (!TryDecodeUTF16(character, CODEPOINT_WORD16_COUNT_MAX, NULL, &WordCount))
    {
        return 0;
    }

    return WordCount;
}

size_t CharUTF16_GetWordCountCharFromEnd(const uint16_t* characterLastWord, size_t remainingPreWords)
{
    if (characterLastWord == NULL)
    {
        return 0;
    }

    size_t MaxBacktrack = remainingPreWords;
    if (MaxBacktrack > (CODEPOINT_WORD16_COUNT_MAX - 1))
    {
        MaxBacktrack = CODEPOINT_WORD16_COUNT_MAX - 1;
    }

    for (size_t StartOffset = 0; StartOffset <= MaxBacktrack; StartOffset++)
    {
        const uint16_t* CharacterStart = characterLastWord - StartOffset;
        size_t WordCount = 0;
        if (!TryDecodeUTF16(CharacterStart, StartOffset + 1, NULL, &WordCount))
        {
            continue;
        }

        if ((WordCount - 1) == StartOffset)
        {
            return WordCount;
        }
    }

    return 0;
}

size_t CharUTF16_GetWordCountCodepoint(CodePoint codepoint)
{
    if (!CharUTF16_IsCodePointValid(codepoint))
    {
        return 0;
    }

    return (codepoint <= UTF16_BMP_MAX_CODEPOINT) ? 1 : 2;
}

CodePoint CharUTF16_GetCodePointFromEnd(const uint16_t* characterLastWord, size_t remainingPreWords)
{
    if (characterLastWord == NULL)
    {
        return CODEPOINT_NONE;
    }

    size_t MaxBacktrack = remainingPreWords;
    if (MaxBacktrack > (CODEPOINT_WORD16_COUNT_MAX - 1))
    {
        MaxBacktrack = CODEPOINT_WORD16_COUNT_MAX - 1;
    }

    for (size_t StartOffset = 0; StartOffset <= MaxBacktrack; StartOffset++)
    {
        const uint16_t* CharacterStart = characterLastWord - StartOffset;
        CodePoint Result = CODEPOINT_NONE;
        size_t WordCount = 0;
        if (!TryDecodeUTF16(CharacterStart, StartOffset + 1, &Result, &WordCount))
        {
            continue;
        }

        if ((WordCount - 1) == StartOffset)
        {
            return Result;
        }
    }

    return CODEPOINT_NONE;
}

size_t CharUTF16_WriteCodePoint(uint16_t* character, CodePoint codepoint)
{
    if ((character == NULL) || !CharUTF16_IsCodePointValid(codepoint))
    {
        return 0;
    }

    if (codepoint <= UTF16_BMP_MAX_CODEPOINT)
    {
        character[0] = (uint16_t)codepoint;
        return 1;
    }

    CodePoint EncodedValue = codepoint - UTF16_SUPPLEMENTARY_OFFSET;
    character[0] = (uint16_t)(SURROGATE_HIGH_MIN + (EncodedValue >> 10));
    character[1] = (uint16_t)(SURROGATE_LOW_MIN + (EncodedValue & 0x3FF));
    return 2;
}

CodePoint CharUTF16_GetCodePoint(const uint16_t* character)
{
    CodePoint Result = CODEPOINT_NONE;
    if (!TryDecodeUTF16(character, CODEPOINT_WORD16_COUNT_MAX, &Result, NULL))
    {
        return CODEPOINT_NONE;
    }

    return Result;
}
