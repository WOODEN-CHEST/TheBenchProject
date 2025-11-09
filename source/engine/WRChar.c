#include "WRChar.h"
#include "WRUnicode.h"
#include <stddef.h>
#include <limits.h>

// https://en.wikipedia.org/wiki/UTF-8


// Static fields.
static const unsigned char UTF8_1B_FIRST_BYTE_VALUE_MASK = 0b10000000;
static const unsigned char UTF8_1B_FIRST_BYTE_VALUE = 0b00000000;
static const CodePoint UTF8_1B_MAX_CODEPOINT = 127;

static const unsigned char UTF8_2B_FIRST_BYTE_VALUE_MASK = 0b11100000;
static const unsigned char UTF8_2B_FIRST_BYTE_VALUE = 0b11000000;
static const CodePoint UTF8_2B_MAX_CODEPOINT = UTF8_1B_MAX_CODEPOINT + 1920;

static const unsigned char UTF8_3B_FIRST_BYTE_VALUE_MASK = 0b11110000;
static const unsigned char UTF8_3B_FIRST_BYTE_VALUE = 0b11100000;
static const CodePoint UTF8_3B_MAX_CODEPOINT = UTF8_2B_MAX_CODEPOINT + 63488;

static const unsigned char UTF8_4B_FIRST_BYTE_VALUE_MASK = 0b11111000;
static const unsigned char UTF8_4B_FIRST_BYTE_VALUE = 0b11110000;

static const unsigned char UTF8_TRAIL_VALUE_MASK = 0b11000000;
static const unsigned char UTF8_TRAIL_VALUE = 0b10000000;
static const size_t UTF8_TRAIL_BIT_COUNT = 6;

static const CodePoint SURROGATE_LOW_MIN = 0xDC00;
static const CodePoint SURROGATE_LOW_MAX = 0xDFFF;
static const CodePoint SURROGATE_HIGH_MIN = 0xD800;
static const CodePoint SURROGATE_HIGH_MAX = 0xDBFF;

static const CodePoint CODEPOINT_MAX = 0x10FFFF;


// Static functions.
static inline bool Is1ByteUTF8(const unsigned char firstByte)
{
    return (firstByte & UTF8_1B_FIRST_BYTE_VALUE_MASK) == UTF8_1B_FIRST_BYTE_VALUE;
}

static inline bool Is2ByteUTF8(const unsigned char firstByte)
{
    return (firstByte & UTF8_2B_FIRST_BYTE_VALUE_MASK) == UTF8_2B_FIRST_BYTE_VALUE;
}

static inline bool Is3ByteUTF8(const unsigned char firstByte)
{
    return (firstByte & UTF8_3B_FIRST_BYTE_VALUE_MASK) == UTF8_3B_FIRST_BYTE_VALUE;
}

static inline bool Is4ByteUTF8(const unsigned char firstByte)
{
    return (firstByte & UTF8_4B_FIRST_BYTE_VALUE_MASK) == UTF8_4B_FIRST_BYTE_VALUE;
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
    return IsInLowSurrogateRange(codepoint) || IsInHighSurrogateRange(codepoint);
}

static inline bool IsInUnicodeRange(CodePoint codepoint)
{
    return codepoint <= CODEPOINT_MAX;
}


// Functions.
bool CharUTF8_IsCodePointValid(CodePoint codepoint)
{
    return !IsInSurrogateRange(codepoint) && IsInUnicodeRange(codepoint);
}

bool CharUTF8_IsCharValid(const unsigned char* character)
{
    return CharUTF8_IsCharBufferValid(character, SIZE_MAX);
}

bool CharUTF8_IsCharBufferValid(const unsigned char* character, size_t bufferLength)
{
    if (bufferLength == 0)
    {
        return false;
    }

    unsigned char FirstByte = *character;

    bool IsPatternValid = false;
    if (Is1ByteUTF8(FirstByte))
    {
        IsPatternValid = bufferLength >= 1;
    }
    else if (Is2ByteUTF8(FirstByte))
    {
        IsPatternValid = (bufferLength >= 2) 
        && ((character[1] & UTF8_TRAIL_VALUE_MASK) == UTF8_TRAIL_VALUE);
    }
    else if (Is3ByteUTF8(FirstByte))
    {
        IsPatternValid = (bufferLength >= 3)
        && ((character[1] & UTF8_TRAIL_VALUE_MASK) == UTF8_TRAIL_VALUE)
        && ((character[2] & UTF8_TRAIL_VALUE_MASK) == UTF8_TRAIL_VALUE);
    }
    else if (Is4ByteUTF8(FirstByte))
    {
        IsPatternValid = (bufferLength >= 4) 
        &&((character[1] & UTF8_TRAIL_VALUE_MASK) == UTF8_TRAIL_VALUE)
        && ((character[2] & UTF8_TRAIL_VALUE_MASK) == UTF8_TRAIL_VALUE)
        && ((character[3] & UTF8_TRAIL_VALUE_MASK) == UTF8_TRAIL_VALUE);
    }

    if (!IsPatternValid)
    {
        return false;
    }

    CodePoint CharCodepoint = CharUTF8_GetCodePoint(character);
    bool IsOverlongEncoding = CharUTF8_GetByteCountChar(character) > CharUTF8_GetByteCountCodepoint(CharCodepoint);
    if (IsOverlongEncoding || !CharUTF8_IsCodePointValid(CharCodepoint))
    {
        return false;
    }

    return true;
}

size_t CharUTF8_GetByteCountChar(const unsigned char* character)
{
    unsigned char FirstByte = *character;
    if (Is1ByteUTF8(FirstByte))
    {
        return 1;
    }
    else if (Is2ByteUTF8(FirstByte))
    {
        return 2;
    }
    else if (Is3ByteUTF8(FirstByte))
    {
        return 3;
    }
    else if (Is4ByteUTF8(FirstByte))
    {
        return 4;
    }

    return 0;
}

size_t CharUTF8_GetByteCountCodepoint(CodePoint codepoint)
{
    if (!IsInUnicodeRange(codepoint))
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

size_t CharUTF8_WriteCodePoint(unsigned char* character, CodePoint codepoint)
{
    if (IsInSurrogateRange(codepoint) || !IsInUnicodeRange(codepoint))
    {
        return 0;
    }

    if (codepoint <= UTF8_1B_MAX_CODEPOINT)
    {
        *character = (unsigned char)codepoint;
        return 1;
    }
    if (codepoint <= UTF8_2B_MAX_CODEPOINT)
    {
        character[1] = (unsigned char)((codepoint & (~UTF8_TRAIL_VALUE_MASK)) | UTF8_TRAIL_VALUE);
        character[0] = (unsigned char)(((codepoint >> UTF8_TRAIL_BIT_COUNT) & (~UTF8_2B_FIRST_BYTE_VALUE_MASK)) | UTF8_2B_FIRST_BYTE_VALUE);
        return 2;
    }
    if (codepoint <= UTF8_3B_MAX_CODEPOINT)
    {
        character[2] = (unsigned char)((codepoint & (~UTF8_TRAIL_VALUE_MASK)) | UTF8_TRAIL_VALUE);
        character[1] = (unsigned char)(((codepoint >> UTF8_TRAIL_BIT_COUNT) & (~UTF8_TRAIL_VALUE_MASK)) | UTF8_TRAIL_VALUE);
        character[0] = (unsigned char)(((codepoint >> (UTF8_TRAIL_BIT_COUNT * 2)) & (~UTF8_3B_FIRST_BYTE_VALUE_MASK)) | UTF8_3B_FIRST_BYTE_VALUE);
        return 3;
    }

    character[3] = (unsigned char)((codepoint & (~UTF8_TRAIL_VALUE_MASK)) | UTF8_TRAIL_VALUE);
    character[2] = (unsigned char)(((codepoint >> UTF8_TRAIL_BIT_COUNT) & (~UTF8_TRAIL_VALUE_MASK)) | UTF8_TRAIL_VALUE);
    character[1] = (unsigned char)(((codepoint >> (UTF8_TRAIL_BIT_COUNT * 2)) & (~UTF8_TRAIL_VALUE_MASK)) | UTF8_TRAIL_VALUE);
    character[0] = (unsigned char)(((codepoint >> (UTF8_TRAIL_BIT_COUNT * 3)) & (~UTF8_4B_FIRST_BYTE_VALUE_MASK)) | UTF8_4B_FIRST_BYTE_VALUE);
    return 4;
}

CodePoint CharUTF8_GetCodePoint(const unsigned char* character)
{
    unsigned char FirstByte = *character;

    if (Is1ByteUTF8(FirstByte))
    {
        return (CodePoint)(character[0] & (~UTF8_1B_FIRST_BYTE_VALUE_MASK));
    }
    if (Is2ByteUTF8(FirstByte))
    {
        return (CodePoint)(((character[0] & (~UTF8_2B_FIRST_BYTE_VALUE_MASK)) << UTF8_TRAIL_BIT_COUNT)
        | (character[1] & (~UTF8_TRAIL_VALUE_MASK)));
    }
    if (Is3ByteUTF8(FirstByte))
    {
        return (CodePoint)(((character[0] & (~UTF8_3B_FIRST_BYTE_VALUE_MASK)) << (UTF8_TRAIL_BIT_COUNT * 2))
        | ((character[1] & (~UTF8_TRAIL_VALUE_MASK)) << UTF8_TRAIL_BIT_COUNT)
        | (character[2] & (~UTF8_TRAIL_VALUE_MASK)));
    }
    if (Is4ByteUTF8(FirstByte))
    {
        return (CodePoint)(((character[0] & (~UTF8_4B_FIRST_BYTE_VALUE_MASK)) << (UTF8_TRAIL_BIT_COUNT * 3))
        | ((character[1] & (~UTF8_TRAIL_VALUE_MASK)) << (UTF8_TRAIL_BIT_COUNT * 2))
        | ((character[2] & (~UTF8_TRAIL_VALUE_MASK)) << UTF8_TRAIL_BIT_COUNT)
        | (character[3] & (~UTF8_TRAIL_VALUE_MASK)));
    }
    return CODEPOINT_NONE;
}