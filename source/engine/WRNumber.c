#include "WRNumber.h"
#include "WRError.h"
#include "stdint.h"
#include "WRMemory.h"
#include <limits.h>
#include "WREnvironment.h"
#include <stdio.h>


// Fields.
static const unsigned char MINUS = '-';
static const unsigned char PLUS = '+';

const int32_t NUMBER_BASE_MAX = 16;
const int32_t NUMBER_BASE_MIN = 2;
const int32_t NUMBER_BASE_AUTO_DETECT = 0;
const int32_t NUMBER_BASE_10 = 10;
const int32_t NUMBER_BASE_2 = 2;
const int32_t NUMBER_BASE_16 = 16;

const int32_t DIGIT_INVALID_VALUE = -1;

const unsigned char PREFIX_BASE_16_A = 'x';
const unsigned char PREFIX_BASE_16_B = 'X';
const unsigned char PREFIX_BASE_2_A = 'b';
const unsigned char PREFIX_BASE_2_B = 'B';
const unsigned char BASE_INDICATOR_START = '0';

const size_t BASE_SPECIFIED_LENGTH = 2;

const int32_t DIGIT_VALUE_MAX_BASE_10 = 9;
const int32_t DIGIT_VALUE_MAX_BASE_16 = 15;

const size_t BITS_PER_BYTE = 8;
const uint64_t BIT_TO_SHIFT = 1;
const uint64_t MAX_BIT_COUNT = sizeof(uint64_t) * BITS_PER_BYTE;


// Static functions.
static Error CreateInvalidBaseError(ErrorMessagePool* errorPool, int32_t base)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Invalid number base %d. Min is %d, max is %d and base auto detect is %d.",
            base, NUMBER_BASE_MIN, NUMBER_BASE_MAX, NUMBER_BASE_AUTO_DETECT);
    }
    return Error_Construct5(Code);
}

static Error CreateMultipleSignSymbolsError(ErrorMessagePool* errorPool, unsigned char symbol)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Integers may only have 1 sign symbol (- or +, in this case the issue was caused by a '%c' symbol).",
            symbol);
    }
    return Error_Construct5(Code);
}

static inline bool IsBaseDigit(int32_t digitValue, int32_t base)
{
    return (digitValue >= 0) && (digitValue < base);
}

static inline int32_t DigitToValue(unsigned char digit)
{
    if (('0' <= digit) && (digit <= '9'))
    {
        return (int32_t)(digit - '0');
    }
    if (('a' <= digit) && (digit <= 'f'))
    {
        return (int32_t)(digit - 'a') + 10;
    }
    if (('A' <= digit) && (digit <= 'F'))
    {
        return (int32_t)(digit - 'A') + 10;
    }
    return DIGIT_INVALID_VALUE;
}

static bool TryWriteBaseFromChar(unsigned char prefix, int32_t* base)
{
    if ((prefix == PREFIX_BASE_16_A) || (prefix == PREFIX_BASE_16_B))
    {
        *base = NUMBER_BASE_16;
        return true;
    }
    if ((prefix == PREFIX_BASE_2_A) || (prefix == PREFIX_BASE_2_B))
    {
        *base = NUMBER_BASE_2;
        return true;
    }
    return false;
}

static Error CreateNoNumberBaseError(ErrorMessagePool* errorPool)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool, Code, u8"Unable to detect the given number's base.");
    }
    return Error_Construct5(Code);
}

static Error IsIntNegative(ErrorMessagePool* errorPool, const unsigned char* str, size_t* skipAmount, bool* isNegative)
{
    *skipAmount = 0;
    *isNegative = false;

    bool HadSign = false;
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        unsigned char Character = str[i];
        if ((Character == MINUS) || (Character == PLUS))
        {
            if (HadSign)
            {
                return CreateMultipleSignSymbolsError(errorPool, Character);
            }
            HadSign = true;
            *isNegative = Character == MINUS;
            *skipAmount = 1;
        }
        else
        {
            return Error_CreateSuccess();
        }
    }

    return Error_CreateSuccess();
}

static Error GetBase(ErrorMessagePool* errorPool, const unsigned char* str, int32_t givenBase, int32_t* finalBase, size_t* prefixSkipAmount)
{
    *prefixSkipAmount = 0;
    *finalBase = NUMBER_BASE_AUTO_DETECT;
    
    if ((givenBase != NUMBER_BASE_AUTO_DETECT) && ((givenBase < NUMBER_BASE_MIN) || (givenBase > NUMBER_BASE_MAX)))
    {
        return CreateInvalidBaseError(errorPool, givenBase);
    }
    if (givenBase != NUMBER_BASE_AUTO_DETECT)
    {
        *finalBase = givenBase;
        return Error_CreateSuccess();
    }

    bool IsPrefixPossible = false;
    int32_t DetectedBase = givenBase;
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        unsigned char CharAtIndex = str[i];
        if ((i == 0) && (CharAtIndex == BASE_INDICATOR_START))
        {
            DetectedBase = NUMBER_BASE_10;
            IsPrefixPossible = true;
            continue;
        }
        if (IsPrefixPossible && TryWriteBaseFromChar(CharAtIndex, &DetectedBase))
        {
            *prefixSkipAmount = BASE_SPECIFIED_LENGTH;
            break;
        }

        IsPrefixPossible = false;
        int32_t Value = DigitToValue(CharAtIndex);
        if (Value == DIGIT_INVALID_VALUE)
        {
            continue;
        }
        if ((Value <= DIGIT_VALUE_MAX_BASE_10) && (DetectedBase < NUMBER_BASE_10))
        {
            DetectedBase = NUMBER_BASE_10;
        }
        else if ((Value > DIGIT_VALUE_MAX_BASE_10) && (DetectedBase < NUMBER_BASE_16))
        {
            DetectedBase = NUMBER_BASE_16;
        }
    }

    if (DetectedBase == NUMBER_BASE_AUTO_DETECT)
    {
        return CreateNoNumberBaseError(errorPool);
    }
    *finalBase = DetectedBase;
    return Error_CreateSuccess();
}

static Error CreateInvalidCharacterError(ErrorMessagePool* errorPool, int32_t base, unsigned char character)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Invalid character '%c' for a base %d number.", character, base);
    }
    return Error_Construct5(Code);
}

static Error CreateNumberOverflowError(ErrorMessagePool* errorPool, const unsigned char* number, size_t byteSize)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Number %s is out of the valid range for a size of %zu bytes.",
            number, byteSize);
    }
    return Error_Construct5(Code);
}

static bool WillOverflow(uint64_t sourceValue, int32_t base, int32_t digitValue)
{
    uint64_t UBase = (uint64_t)base;
    uint64_t UDigitValue = (uint64_t)digitValue;

    if (sourceValue > (UINT64_MAX / UBase))
    {
        return true;
    }
    if ((UINT64_MAX - (sourceValue * UBase)) < UDigitValue)
    {
        return true;
    }

    return false;
}

static bool IsRangeValid(uint64_t bits, bool isSignAdded, size_t targetByteSize, bool isTargetSigned)
{
    StandardIntegers Value = { .UInt64 = bits };
    size_t BitCount = targetByteSize * BITS_PER_BYTE;

    if (isTargetSigned)
    {
        uint64_t Max = (UINT64_MAX >> (MAX_BIT_COUNT - BitCount + 1));
        int64_t Min =  -Max - 1;
        if (!isSignAdded)
        {
            return (Value.Int64 >= 0) && (Value.Int64 <= (int64_t)Max);
        }
        
        return (Value.UInt64 <= Max + 1) && (Min <= (-Value.Int64));
    }

    if (isSignAdded)
    {
        return false;
    }
    uint64_t Max = (UINT64_MAX >> (MAX_BIT_COUNT - BitCount));
    return Value.UInt64 <= Max;
}

static Error UInt64ToTargetInt(ErrorMessagePool* errorPool,
    const unsigned char* numberStr,
    uint64_t bits,
    bool isBitsSigned,
    size_t targetbyteSize,
    bool isTargetSigned,
    void* result)
{
    if (!IsRangeValid(bits, isBitsSigned, targetbyteSize, isTargetSigned))
    {
        return CreateNumberOverflowError(errorPool, numberStr, targetbyteSize);
    }

    size_t BitCount = targetbyteSize * BITS_PER_BYTE;
    uint64_t SizeMask = (UINT64_MAX >> (MAX_BIT_COUNT - BitCount));
    uint64_t FinalBits = bits;
    if (isBitsSigned)
    {
        FinalBits = (((~FinalBits) + 1) & (SizeMask));
    }

    if (Environment_GetEndianess() == MachineEndianess_BigEndian)
    {
        FinalBits <<= ((sizeof(uint64_t) - targetbyteSize) * BITS_PER_BYTE);
    }

    Memory_Copy(&FinalBits, result, targetbyteSize);
    return Error_CreateSuccess();
}

static Error ParseInteger(ErrorMessagePool* errorPool,
    const unsigned char* str,
    int32_t base,
    bool isTargetSigned,
    size_t targetByteSize,
    void* result)
{
    size_t SignSkipAmount;
    bool IsNegative;
    Error ErrorResult = IsIntNegative(errorPool, str, &SignSkipAmount, &IsNegative);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    int32_t FinalBase;
    size_t PrefixSkipAmount;
    ErrorResult = GetBase(errorPool, str + SignSkipAmount, base, &FinalBase, &PrefixSkipAmount);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    uint64_t Value = 0;
    for (size_t i = SignSkipAmount + PrefixSkipAmount; str[i] != '\0'; i++)
    {
        unsigned char Character = str[i];

        int32_t DigitValue = DigitToValue(Character);
        if ((DigitValue == DIGIT_INVALID_VALUE) || (DigitValue >= FinalBase))
        {
            return CreateInvalidCharacterError(errorPool, FinalBase, Character);
        }
        if (WillOverflow(Value, FinalBase, DigitValue))
        {
            return CreateNumberOverflowError(errorPool, str, targetByteSize);
        }

        Value = (Value * (uint64_t)FinalBase) + DigitValue;
    }

    return UInt64ToTargetInt(errorPool, str, Value, IsNegative, targetByteSize, isTargetSigned, result);
}

static bool WriteCharToGenericBuffer(GenericBuffer* buffer, size_t writtenCharCount, unsigned char character)
{
    if (buffer->_bufferSize <= writtenCharCount)
    {
        bool WasMemoryAllocated = buffer->_requestMoreSpaceCallback && (*buffer->_requestMoreSpaceCallback)(buffer);
        if (!WasMemoryAllocated || (buffer->_bufferSize <= writtenCharCount))
        {
            return false;
        }
    }

    unsigned char* CharBuffer = buffer->_buffer;
    CharBuffer[writtenCharCount] = character;
    return true;
}

static Error ValidateBaseForWriting(ErrorMessagePool* errorPool, int32_t base)
{
    if ((base < NUMBER_BASE_MIN) || (base > NUMBER_BASE_MAX))
    {
        ErrorCode Code = ErrorCode_IllegalArgument;
        if (errorPool)
        {
            return Error_Construct3(errorPool,
                Code,
                u8"Invalid base %d for writing an integer to a string conversion. Minimum base is %d and maximum base is %d.",
                base, NUMBER_BASE_MIN, NUMBER_BASE_MAX);
        }
        return Error_Construct5(Code);
    }
    return Error_CreateSuccess();
}

static unsigned char DigitValueToChar(uint64_t digitValue)
{
    if (digitValue <= DIGIT_VALUE_MAX_BASE_10)
    {
        return '0' + digitValue;
    }
    return 'a' + (digitValue - 10);
}

static void ReverseDigits(unsigned char* str, size_t charCount, size_t digitCount)
{
    size_t Offset = charCount - digitCount;
    for (size_t i = 0; i < (digitCount / 2); i++)
    {
        size_t IndexA = i + Offset;
        size_t IndexB = digitCount + Offset - i - 1;
        unsigned char DigitA = str[IndexA];
        unsigned char DigitB = str[IndexB];
        str[IndexA] = DigitB;
        str[IndexB] = DigitA;
    }
}

static Error WriteIntString(ErrorMessagePool* errorPool, uint64_t bits, bool isSigned, size_t numberSize, int32_t base, GenericBuffer* buffer)
{
    Error ErrorResult = ValidateBaseForWriting(errorPool, base);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    size_t WrittenCharCount = 0;
    size_t BitCount = numberSize * BITS_PER_BYTE;
    uint64_t SignBit = (BIT_TO_SHIFT << (BitCount - 1));
    uint64_t Mask = (UINT64_MAX >> (MAX_BIT_COUNT - BitCount));

    uint64_t BitsWithoutSign;
    bool HasMinusSign = isSigned && (bits & SignBit);
    if (HasMinusSign)
    {
        if (!WriteCharToGenericBuffer(buffer, WrittenCharCount, MINUS))
        {
            return Error_CreateSuccess();
        }
        WrittenCharCount++;
        BitsWithoutSign = ((~bits) & Mask) + 1;
    }
    else
    {
        BitsWithoutSign = bits;
    }

    for (size_t i = 0; (i == 0) || (BitsWithoutSign != 0); BitsWithoutSign /= (uint64_t)base, i++)
    {
        uint64_t DigitValue = BitsWithoutSign % (uint64_t)base;
        if (!WriteCharToGenericBuffer(buffer, WrittenCharCount, DigitValueToChar(DigitValue)))
        {
            return Error_CreateSuccess();
        }
        WrittenCharCount++;
    }

    ReverseDigits(buffer->_buffer, WrittenCharCount, WrittenCharCount - (HasMinusSign ? 1 : 0));
    return Error_CreateSuccess();
}


// Functions.
Error Number_Int8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int8_t* value)
{
    return ParseInteger(errorPool, str, base, true, sizeof(int8_t), value);
}

Error Number_Int8ToString(ErrorMessagePool* errorPool, int8_t value, int32_t base, GenericBuffer buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .Int8 = value }.UInt64,
        true,
        sizeof(int8_t),
        base,
        &buffer);
}


Error Number_UInt8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint8_t* value)
{
    return ParseInteger(errorPool, str, base, false, sizeof(uint8_t), value);
}

Error Number_UInt8ToString(ErrorMessagePool* errorPool, uint8_t value, int32_t base, GenericBuffer buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .UInt8 = value }.UInt64,
        false,
        sizeof(uint8_t),
        base,
        &buffer);
}


Error Number_Int16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int16_t* value)
{
    return ParseInteger(errorPool, str, base, true, sizeof(int16_t), value);
}

Error Number_Int16ToString(ErrorMessagePool* errorPool, int16_t value, int32_t base, GenericBuffer buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .Int16 = value }.UInt64,
        true,
        sizeof(int16_t),
        base,
        &buffer);
}


Error Number_UInt16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint16_t* value)
{
    return ParseInteger(errorPool, str, base, false, sizeof(uint16_t), value);
}

Error Number_UInt16ToString(ErrorMessagePool* errorPool, uint16_t value, int32_t base, GenericBuffer buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .UInt16 = value }.UInt64,
        false,
        sizeof(uint16_t),
        base,
        &buffer);
}


Error Number_Int32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int32_t* value)
{
    return ParseInteger(errorPool, str, base, true, sizeof(int32_t), value);
}

Error Number_Int32ToString(ErrorMessagePool* errorPool, int32_t value, int32_t base, GenericBuffer buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .Int32 = value }.UInt64,
        true,
        sizeof(int32_t),
        base,
        &buffer);
}


Error Number_UInt32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint32_t* value)
{
    return ParseInteger(errorPool, str, base, false, sizeof(uint32_t), value);
}

Error Number_UInt32ToString(ErrorMessagePool* errorPool, uint32_t value, int32_t base, GenericBuffer buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .UInt32 = value }.UInt64,
        false,
        sizeof(uint32_t),
        base,
        &buffer);
}


Error Number_Int64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int64_t* value)
{
    return ParseInteger(errorPool, str, base, true, sizeof(int64_t), value);
}

Error Number_Int64ToString(ErrorMessagePool* errorPool, int64_t value, int32_t base, GenericBuffer buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .Int64 = value }.UInt64,
        true,
        sizeof(int64_t),
        base,
        &buffer);
}


Error Number_UInt64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint64_t* value)
{
    return ParseInteger(errorPool, str, base, false, sizeof(uint64_t), value);
}

Error Number_UInt64ToString(ErrorMessagePool* errorPool, uint64_t value, int32_t base, GenericBuffer buffer)
{
    return WriteIntString(errorPool,
        value,
        false,
        sizeof(uint64_t),
        base,
        &buffer);
}


Error Number_FloatFromString(ErrorMessagePool* errorPool,
    const unsigned char* str,
    float* value,
    DecimalSeparator separator);

Error Number_FloatToString(ErrorMessagePool* errorPool,
    float value,
    DecimalSeparator separator,
    GenericBuffer buffer,
    const unsigned char* pattern);


Error Number_DoubleFromString(ErrorMessagePool* errorPool,
    const unsigned char* str,
    double* value,
    DecimalSeparator separator);

Error Number_DoubleToString(ErrorMessagePool* errorPool,
    double value,
    DecimalSeparator separator,
    GenericBuffer buffer,
    const unsigned char* pattern);