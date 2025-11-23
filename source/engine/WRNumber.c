#include "WRNumber.h"
#include "WRError.h"
#include "stdint.h"
#include "WRMemory.h"
#include <limits.h>
#include "WREnvironment.h"
#include <stdio.h>
#include <string.h>
#include <math.h>


// Fields.
const int32_t NUMBER_BASE_MAX = 16;
const int32_t NUMBER_BASE_MIN = 2;
const int32_t NUMBER_BASE_AUTO_DETECT = 0;
const int32_t NUMBER_BASE_10 = 10;
const int32_t NUMBER_BASE_2 = 2;
const int32_t NUMBER_BASE_16 = 16;

const int32_t DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST = -1;
const int32_t DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED = -2;

static const unsigned char MINUS = '-';
static const unsigned char PLUS = '+';

static const int32_t DIGIT_INVALID_VALUE = -1;

static const unsigned char PREFIX_BASE_16_A = 'x';
static const unsigned char PREFIX_BASE_16_B = 'X';
static const unsigned char PREFIX_BASE_2_A = 'b';
static const unsigned char PREFIX_BASE_2_B = 'B';
static const unsigned char BASE_INDICATOR_START = '0';

static const size_t BASE_SPECIFIED_LENGTH = 2;

static const int32_t DIGIT_VALUE_MAX_BASE_10 = 9;

static const size_t BITS_PER_BYTE = 8;
static const uint64_t BIT_TO_SHIFT = 1;
static const uint64_t MAX_BIT_COUNT = sizeof(uint64_t) * BITS_PER_BYTE;

static const unsigned char* STRING_NAN = u8"nan";
static const unsigned char* STRING_INF_POS = u8"infinity";
static const unsigned char* STRING_INF_NEG = u8"-infinity";

static const unsigned char SEPARATOR_PERIOD = '.';
static const unsigned char SEPARATOR_COMMA = ',';
static const unsigned char EXPONENT_INDICATOR = 'e';


// Static functions.
static Error CreateBufferOutOfSpaceError(ErrorMessagePool* errorPool, GenericBuffer* buffer)
{
    ErrorCode Code = ErrorCode_BufferTooSmall;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Passed in generic buffer with size of %zu is too small to fit the number converter to a string.",
            buffer->_capacity * buffer->_elementSize);
    }
    return Error_Construct5(Code);
}

static unsigned char GetCharToLower(unsigned char value)
{
    if (('A' <= value) && (value <= 'Z'))
    {
        return value + 32;
    }
    return value;
}

static bool StringEqualsIgnoreCase(const unsigned char* strA, const unsigned char* strB)
{
    size_t Index;
    for (Index = 0; (strA[Index] != '\0') && (strB[Index] != '\0'); Index++)
    {
        unsigned char CharA = GetCharToLower(strA[Index]);
        unsigned char CharB = GetCharToLower(strB[Index]);

        if (CharA != CharB)
        {
            return false;
        }
    }

    return strA[Index] == strB[Index];
}

static Error CreateAtLeast1DigitRequiredError(ErrorMessagePool* errorPool)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"At least 1 digit is required in a number.");
    }
    return Error_Construct5(Code);
}

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
            u8"Numbers may only have 1 sign symbol (- or +, in this case the issue was caused by a '%c' symbol).",
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

    unsigned char DigitLower = GetCharToLower(digit);
    if (('a' <= DigitLower) && (DigitLower <= 'f'))
    {
        return (int32_t)(DigitLower - 'a') + 10;
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
    bool IsDigitFound = false;
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
        IsDigitFound = true;
    }

    if (!IsDigitFound)
    {
        return CreateAtLeast1DigitRequiredError(errorPool);
    }

    return UInt64ToTargetInt(errorPool, str, Value, IsNegative, targetByteSize, isTargetSigned, result);
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
    if (digitValue <= (uint64_t)DIGIT_VALUE_MAX_BASE_10)
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

    size_t BitCount = numberSize * BITS_PER_BYTE;
    uint64_t SignBit = (BIT_TO_SHIFT << (BitCount - 1));
    uint64_t Mask = (UINT64_MAX >> (MAX_BIT_COUNT - BitCount));

    uint64_t BitsWithoutSign;
    bool HasMinusSign = isSigned && (bits & SignBit);
    if (HasMinusSign)
    {
        if (!GenericBuffer_WriteUChar(buffer, MINUS))
        {
            return Error_CreateSuccess();
        }
        BitsWithoutSign = ((~bits) & Mask) + 1;
    }
    else
    {
        BitsWithoutSign = bits;
    }

    for (size_t i = 0; (i == 0) || (BitsWithoutSign != 0); BitsWithoutSign /= (uint64_t)base, i++)
    {
        uint64_t DigitValue = BitsWithoutSign % (uint64_t)base;
        if (!GenericBuffer_WriteUChar(buffer, DigitValueToChar(DigitValue)))
        {
            break;
        }
    }

    ReverseDigits(buffer->_data, buffer->_count, buffer->_count - (HasMinusSign ? 1 : 0));
    GenericBuffer_TryNullTerminate(buffer);
    return Error_CreateSuccess();
}

static bool IsStringInf(const unsigned char* str, bool* isNegative)
{
    *isNegative = false;
    if (StringEqualsIgnoreCase(str, STRING_INF_POS))
    {
        return true;
    }
    if (StringEqualsIgnoreCase(str, STRING_INF_NEG))
    {
        *isNegative = true;
        return true;
    }
    return false;
}

static bool IsStringNan(const unsigned char* str)
{
    return StringEqualsIgnoreCase(str, STRING_NAN);
}

static bool IsCharSeparator(unsigned char value, DecimalSeparator allowedSeparator)
{
    if ((value == SEPARATOR_COMMA) | (value == SEPARATOR_PERIOD))
    {
        return (allowedSeparator == DecimalSeparator_Any)
            || ((allowedSeparator == DecimalSeparator_Period) && (value == SEPARATOR_PERIOD))
            || ((allowedSeparator == DecimalSeparator_Comma) && (value == SEPARATOR_COMMA));
    }
    return false;
}

static Error CreateTooManySeparatorsError(ErrorMessagePool* pool, unsigned char separator)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (pool)
    {
        return Error_Construct3(pool,
            Code,
            u8"Found duplicate number decimal separator '%c'.",
            separator);
    }
    return Error_Construct5(Code);
}

static Error GetDoubleMantissa(ErrorMessagePool* errorPool,
    const unsigned char* str,
    double* value,
    DecimalSeparator separator,
    size_t* parsedCharCount)
{
    *value = 0.0;
    double Mantissa = 0.0;
    double AfterDecimalDivider = 10.0;
    bool IsSpearatorFound = false;
    bool IsNegative = false;
    bool IsPositive = false;
    bool IsDigitFound = false;
    *parsedCharCount = 0;

    size_t Index;
    for (Index = 0; str[Index] != '\0'; Index++)
    {
        unsigned char Character = str[Index];
        if (IsCharSeparator(Character, separator))
        {
            if (IsSpearatorFound)
            {
                return CreateTooManySeparatorsError(errorPool, Character);
            }
            IsSpearatorFound = true;
            continue;
        }
        if ((Character == MINUS) || (Character == PLUS))
        {
            if (IsNegative || IsPositive)
            {
                return CreateMultipleSignSymbolsError(errorPool, Character);
            }
            IsNegative = Character == MINUS;
            IsPositive = Character == PLUS;
            continue;
        }

        int32_t DigitValue = DigitToValue(Character);
        if (isinf(Mantissa) || (DigitValue == DIGIT_INVALID_VALUE) || (DigitValue > DIGIT_VALUE_MAX_BASE_10))
        {
            break;
        }

        if (IsSpearatorFound)
        {
            Mantissa += (double)DigitValue / AfterDecimalDivider;
            AfterDecimalDivider *= 10.0;
        }
        else
        {
            Mantissa *= 10.0;
            Mantissa += (double)DigitValue;
        }
        IsDigitFound = true;
    }

    if (!IsDigitFound)
    {
        return CreateAtLeast1DigitRequiredError(errorPool);
    }
    if (IsNegative)
    {
        Mantissa = -Mantissa;
    }

    *parsedCharCount = Index;
    *value = Mantissa;
    return Error_CreateSuccess();
}

static Error CreateExpectedExponentIndicator(ErrorMessagePool* errorPool, unsigned char recievedChar)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Expected exponent indicator '%c' or end of decimal number mantissa, got '%c'.",
            EXPONENT_INDICATOR, recievedChar);
    }
    return Error_Construct5(Code);
}

static Error CreateInvalidExponentError(ErrorMessagePool* errorPool, const unsigned char* exponent)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Expected signed integer as exponent, got '%s'.",
            exponent);
    }
    return Error_Construct5(Code);
}

static Error TryParseExponent(ErrorMessagePool* errorPool,
    const unsigned char* str,
    double* exponent)
{
    *exponent = 0.0;
    if (GetCharToLower(str[0]) != EXPONENT_INDICATOR)
    {
        return CreateExpectedExponentIndicator(errorPool, str[0]);
    }

    const unsigned char* RawExponentStr = str + 1;
    int64_t ParsedExponent = 0;
    Error ExponentParseResult = ParseInteger(NULL, RawExponentStr, NUMBER_BASE_10, true, sizeof(int64_t), &ParsedExponent);
    if (ExponentParseResult.Code != ErrorCode_Success)
    {
        return CreateInvalidExponentError(errorPool, RawExponentStr);
    }

    *exponent = (double)ParsedExponent;

    return Error_CreateSuccess();
}

static bool TryWriteDecimalEdgeCase(const unsigned char* str, double* outValue)
{
    if (IsStringNan(str))
    {
        *outValue = NAN;
        return true;
    }
    else
    {
        bool IsNegative = false;
        if (IsStringInf(str, &IsNegative))
        {
            *outValue = IsNegative ? (-INFINITY) : (INFINITY);
            return true;
        }
    }
    return false;
}

static Error ParseDecimalNormal(ErrorMessagePool* errorPool,
    const unsigned char* str,
    double* value,
    DecimalSeparator separator)
{
    double FinalValue;
    size_t ParsedCharAmount = 0;
    Error ErrorResult = GetDoubleMantissa(errorPool, str, &FinalValue, separator, &ParsedCharAmount);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    if (str[ParsedCharAmount] != '\0')
    {
        double Exponent;
        ErrorResult = TryParseExponent(errorPool, str + ParsedCharAmount, &Exponent);
        if (ErrorResult.Code != ErrorCode_Success)
        {
            return ErrorResult;
        }
        FinalValue = FinalValue * pow(10.0, Exponent);
    }

    *value = FinalValue;
    return Error_CreateSuccess();
}

static bool TryWriteNanOrInfString(double value, GenericBuffer* buffer)
{
    if (isinf(value))
    {
        GenericBuffer_WriteString(buffer, (value > 0.0f) ? STRING_INF_POS : STRING_INF_NEG);
    }
    else if (isnan(value))
    {
        GenericBuffer_WriteString(buffer, STRING_NAN);
    }
    else
    {
        return false;
    }
    return true;
}

static void CreateSpecifierFormat(DecimalFormatOptions formatOptions, unsigned char* specifier, size_t specifierSize)
{
    if (specifierSize == 0)
    {
        return;
    }

    if (formatOptions._digitCountAfterSeparator == DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST)
    {
        specifier[0] = formatOptions._isUpperCase ? 'G' : 'g';
        return;
    }

    char SpecifierLetter = formatOptions._isUpperCase ? 'F' : 'f';
    if (formatOptions._digitCountAfterSeparator == DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED)
    {
        specifier[0] = SpecifierLetter;
    }
    else
    {
        snprintf((char*)specifier, specifierSize, ".%d%c", formatOptions._digitCountAfterSeparator, SpecifierLetter);
    }
}

static void CreateDecimalPrintfFormat(DecimalFormatOptions formatOptions, unsigned char* format, size_t formatSize)
{
    if (formatSize <= 1)
    {
        return;
    }

    // https://cplusplus.com/reference/cstdio/printf/
    Memory_Set(format, 0, formatSize);
    format[0] = '%';
    if (formatOptions._isScientificNotation)
    {
        format[1] = formatOptions._isUpperCase ? 'E' : 'e';
    }
    else
    {
        CreateSpecifierFormat(formatOptions, format + 1, formatSize);
    }
}

static void EnsureDecimalSepratorInString(unsigned char* str, DecimalSeparator separator)
{
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        if (!IsCharSeparator(str[i], DecimalSeparator_Any))
        {
            continue;
        }

        if ((separator == DecimalSeparator_Any) || (separator == DecimalSeparator_Period))
        {
            str[i] = SEPARATOR_PERIOD;
        }
        else
        {
            str[i] = SEPARATOR_COMMA;
        }
    }
}

static Error CreateInternalPrintfFromatError(ErrorMessagePool* errorPool, double number, int code, const unsigned char* format)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"Intenral error converting a decimal number to a string. (number '%f', code '%d', format '%d').",
            number, code, format);
    }
    return Error_Construct5(Code);
}


// Functions.
Error Number_Int8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int8_t* value)
{
    return ParseInteger(errorPool, str, base, true, sizeof(int8_t), value);
}

Error Number_Int8ToString(ErrorMessagePool* errorPool, int8_t value, int32_t base, GenericBuffer* buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .Int8 = value }.UInt64,
        true,
        sizeof(int8_t),
        base,
        buffer);
}


Error Number_UInt8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint8_t* value)
{
    return ParseInteger(errorPool, str, base, false, sizeof(uint8_t), value);
}

Error Number_UInt8ToString(ErrorMessagePool* errorPool, uint8_t value, int32_t base, GenericBuffer* buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .UInt8 = value }.UInt64,
        false,
        sizeof(uint8_t),
        base,
        buffer);
}


Error Number_Int16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int16_t* value)
{
    return ParseInteger(errorPool, str, base, true, sizeof(int16_t), value);
}

Error Number_Int16ToString(ErrorMessagePool* errorPool, int16_t value, int32_t base, GenericBuffer* buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .Int16 = value }.UInt64,
        true,
        sizeof(int16_t),
        base,
        buffer);
}


Error Number_UInt16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint16_t* value)
{
    return ParseInteger(errorPool, str, base, false, sizeof(uint16_t), value);
}

Error Number_UInt16ToString(ErrorMessagePool* errorPool, uint16_t value, int32_t base, GenericBuffer* buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .UInt16 = value }.UInt64,
        false,
        sizeof(uint16_t),
        base,
        buffer);
}


Error Number_Int32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int32_t* value)
{
    return ParseInteger(errorPool, str, base, true, sizeof(int32_t), value);
}

Error Number_Int32ToString(ErrorMessagePool* errorPool, int32_t value, int32_t base, GenericBuffer* buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .Int32 = value }.UInt64,
        true,
        sizeof(int32_t),
        base,
        buffer);
}


Error Number_UInt32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint32_t* value)
{
    return ParseInteger(errorPool, str, base, false, sizeof(uint32_t), value);
}

Error Number_UInt32ToString(ErrorMessagePool* errorPool, uint32_t value, int32_t base, GenericBuffer* buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .UInt32 = value }.UInt64,
        false,
        sizeof(uint32_t),
        base,
        buffer);
}


Error Number_Int64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int64_t* value)
{
    return ParseInteger(errorPool, str, base, true, sizeof(int64_t), value);
}

Error Number_Int64ToString(ErrorMessagePool* errorPool, int64_t value, int32_t base, GenericBuffer* buffer)
{
    return WriteIntString(errorPool,
        (StandardIntegers){ .Int64 = value }.UInt64,
        true,
        sizeof(int64_t),
        base,
        buffer);
}


Error Number_UInt64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint64_t* value)
{
    return ParseInteger(errorPool, str, base, false, sizeof(uint64_t), value);
}

Error Number_UInt64ToString(ErrorMessagePool* errorPool, uint64_t value, int32_t base, GenericBuffer* buffer)
{
    return WriteIntString(errorPool,
        value,
        false,
        sizeof(uint64_t),
        base,
        buffer);
}


Error Number_FloatFromString(ErrorMessagePool* errorPool,
    const unsigned char* str,
    float* value,
    DecimalSeparator separator)
{
    double DoubleValue;
    Error ErrorResult = Number_DoubleFromString(errorPool, str, &DoubleValue, separator);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    *value = (float)DoubleValue;
    return Error_CreateSuccess();
}

Error Number_FloatToString(ErrorMessagePool* errorPool,
    float value,
    GenericBuffer* buffer,
    DecimalFormatOptions options)
{
    return Number_DoubleToString(errorPool, value, buffer, options);
}


Error Number_DoubleFromString(ErrorMessagePool* errorPool,
    const unsigned char* str,
    double* value,
    DecimalSeparator separator)
{
    if (TryWriteDecimalEdgeCase(str, value))
    {
        return Error_CreateSuccess();
    }
    return ParseDecimalNormal(errorPool, str, value, separator);
}

Error Number_DoubleToString(ErrorMessagePool* errorPool,
    double value,
    GenericBuffer* buffer,
    DecimalFormatOptions options)
{
    if (TryWriteNanOrInfString(value, buffer))
    {
        return Error_CreateSuccess();
    }

    bool IsFullyWritten = false;
    unsigned char Format[64];
    CreateDecimalPrintfFormat(options, Format, sizeof(Format));
    size_t AttemptCount = 0;
    const size_t MAX_ATTEMPT_COUNT = 2;
    do
    {
        AttemptCount++;

        int PrintResult = snprintf(buffer->_data, buffer->_capacity, (char*)Format, value);
        if (PrintResult < 0)
        {
            return CreateInternalPrintfFromatError(errorPool, value, PrintResult, Format);
        }

        size_t RequiredBufferSize = (size_t)PrintResult + 1;
        if (RequiredBufferSize <= buffer->_capacity)
        {
            IsFullyWritten = true;
        }
        else if ((AttemptCount < MAX_ATTEMPT_COUNT) && !GenericBuffer_ReserveCapacity(buffer, RequiredBufferSize))
        {
            return CreateBufferOutOfSpaceError(errorPool, buffer);
        }
    } while (!IsFullyWritten && (AttemptCount < MAX_ATTEMPT_COUNT));

    EnsureDecimalSepratorInString(buffer->_data, options._separator);
    return Error_CreateSuccess();
}

DecimalFormatOptions DecimalFormatOptions_CreateScientific(DecimalSeparator separator, bool isUpperCase)
{
    return (DecimalFormatOptions)
    {
        ._isUpperCase = isUpperCase,
        ._separator = separator,
        ._isScientificNotation = true,
        ._digitCountAfterSeparator = DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED
    };
}

DecimalFormatOptions DecimalFormatOptions_CreateFixed(DecimalSeparator separator, int32_t digitCountAfterDecimal, bool isUpperCase)
{
    int32_t ClampedDigitCount = (digitCountAfterDecimal < 0) ? 0 : digitCountAfterDecimal;

    return (DecimalFormatOptions)
    {
        ._isUpperCase = isUpperCase,
        ._separator = separator,
        ._isScientificNotation = false,
        ._digitCountAfterSeparator = ClampedDigitCount
    };
}


DecimalFormatOptions DecimalFormatOptions_CreateShortest(DecimalSeparator separator, bool isUpperCase)
{
    return (DecimalFormatOptions)
    {
        ._isUpperCase = isUpperCase,
        ._separator = separator,
        ._isScientificNotation = false,
        ._digitCountAfterSeparator = DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST
    };
}


DecimalFormatOptions DecimalFormatOptions_CreateFull(DecimalSeparator separator, bool isUpperCase)
{
    return (DecimalFormatOptions)
    {
        ._isUpperCase = isUpperCase,
        ._separator = separator,
        ._isScientificNotation = false,
        ._digitCountAfterSeparator = DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED
    };
}