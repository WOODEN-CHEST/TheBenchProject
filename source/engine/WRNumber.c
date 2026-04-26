#include "WRNumber.h"
#include "WRMath.h"
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>


// Macros.
#define INTEGER_TO_STRING_BUFFER_SIZE 66
#define DECIMAL_FORMAT_BUFFER_SIZE 32


// Types.
typedef struct ParsedIntegerStruct
{
    uint64_t Magnitude;
    bool IsNegative;
    int32_t Base;
} ParsedInteger;


// Fields.
const int32_t NUMBER_BASE_MAX = 16;
const int32_t NUMBER_BASE_MIN = 2;
const int32_t NUMBER_BASE_AUTO_DETECT = 0;
const int32_t NUMBER_BASE_10 = 10;
const int32_t NUMBER_BASE_2 = 2;
const int32_t NUMBER_BASE_16 = 16;

const int32_t DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST = -1;
const int32_t DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED = -2;

static const unsigned char MINUS_SIGN = u8'-';
static const unsigned char PLUS_SIGN = u8'+';
static const unsigned char PREFIX_START = u8'0';
static const unsigned char PREFIX_BASE_2 = u8'b';
static const unsigned char PREFIX_BASE_16 = u8'x';
static const unsigned char PERIOD_SEPARATOR = u8'.';
static const unsigned char COMMA_SEPARATOR = u8',';
static const unsigned char EXPONENT_LOWER = u8'e';
static const unsigned char EXPONENT_UPPER = u8'E';

static const unsigned char* STRING_NAN_LOWER = u8"nan";
static const unsigned char* STRING_NAN_NORMAL = u8"NaN";
static const unsigned char* STRING_POSITIVE_INFINITY = u8"infinity";
static const unsigned char* STRING_NEGATIVE_INFINITY = u8"-infinity";


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateInvalidBaseError(int32_t base)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Invalid number base %d. Valid bases are %d through %d, or %d for auto-detect.",
        base,
        NUMBER_BASE_MIN,
        NUMBER_BASE_MAX,
        NUMBER_BASE_AUTO_DETECT);
}

static Error CreateAtLeastOneDigitRequiredError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument, u8"At least 1 digit is required in a number.");
}

static Error CreateInvalidCharacterError(unsigned char character, int32_t base)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Invalid character '%c' for a base %d number.",
        character,
        base);
}

static Error CreateNumberOverflowError(const unsigned char* str, size_t targetByteSize)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Number \"%s\" is out of range for a %zu-byte integer.",
        str,
        targetByteSize);
}

static Error CreateInvalidDecimalSeparatorError(unsigned char separator)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Invalid decimal separator '%c' in number string.",
        separator);
}

static Error CreateInvalidDecimalStringError(const unsigned char* str)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Invalid decimal number string \"%s\".",
        str);
}

static Error CreateDecimalBufferError(void)
{
    return Error_Construct1(ErrorCode_BufferTooSmall,
        u8"Passed in generic buffer is too small to fit the number string.");
}

static unsigned char ToLowerAscii(unsigned char value)
{
    if ((value >= u8'A') && (value <= u8'Z'))
    {
        return (unsigned char)(value - u8'A' + u8'a');
    }

    return value;
}

static bool EqualsIgnoreCaseAscii(const unsigned char* a, const unsigned char* b)
{
    size_t Index = 0;

    while ((a[Index] != 0) && (b[Index] != 0))
    {
        if (ToLowerAscii(a[Index]) != ToLowerAscii(b[Index]))
        {
            return false;
        }

        Index++;
    }

    return a[Index] == b[Index];
}

static int32_t GetDigitValue(unsigned char character)
{
    if ((character >= u8'0') && (character <= u8'9'))
    {
        return (int32_t)(character - u8'0');
    }

    character = ToLowerAscii(character);
    if ((character >= u8'a') && (character <= u8'f'))
    {
        return (int32_t)(character - u8'a') + 10;
    }

    return -1;
}

static bool IsValidBase(int32_t base)
{
    if (base == NUMBER_BASE_AUTO_DETECT)
    {
        return true;
    }

    return (base >= NUMBER_BASE_MIN) && (base <= NUMBER_BASE_MAX);
}

static const unsigned char* SkipOptionalSign(const unsigned char* str, bool* isNegative)
{
    *isNegative = false;

    if (*str == MINUS_SIGN)
    {
        *isNegative = true;
        return str + 1;
    }

    if (*str == PLUS_SIGN)
    {
        return str + 1;
    }

    return str;
}

static const unsigned char* SkipBasePrefix(const unsigned char* str, int32_t base)
{
    if (str[0] != PREFIX_START)
    {
        return str;
    }

    if ((base == NUMBER_BASE_16) && (ToLowerAscii(str[1]) == PREFIX_BASE_16))
    {
        return str + 2;
    }

    if ((base == NUMBER_BASE_2) && (ToLowerAscii(str[1]) == PREFIX_BASE_2))
    {
        return str + 2;
    }

    return str;
}

static int32_t DetectBase(const unsigned char* str, int32_t requestedBase)
{
    if (requestedBase != NUMBER_BASE_AUTO_DETECT)
    {
        return requestedBase;
    }

    if ((str[0] == PREFIX_START) && (ToLowerAscii(str[1]) == PREFIX_BASE_16))
    {
        return NUMBER_BASE_16;
    }

    if ((str[0] == PREFIX_START) && (ToLowerAscii(str[1]) == PREFIX_BASE_2))
    {
        return NUMBER_BASE_2;
    }

    return NUMBER_BASE_10;
}

static Error ParseUnsignedMagnitude(const unsigned char* digits, int32_t base, uint64_t maxValue, uint64_t* magnitude)
{
    uint64_t Value = 0;
    bool HasDigit = false;

    for (size_t Index = 0; digits[Index] != 0; Index++)
    {
        int32_t DigitValue = GetDigitValue(digits[Index]);
        if ((DigitValue < 0) || (DigitValue >= base))
        {
            return CreateInvalidCharacterError(digits[Index], base);
        }

        if (Value > (maxValue / (uint64_t)base))
        {
            return Error_Construct5(ErrorCode_ArgumentOutOfRange);
        }

        Value *= (uint64_t)base;
        if (Value > (maxValue - (uint64_t)DigitValue))
        {
            return Error_Construct5(ErrorCode_ArgumentOutOfRange);
        }

        Value += (uint64_t)DigitValue;
        HasDigit = true;
    }

    if (!HasDigit)
    {
        return CreateAtLeastOneDigitRequiredError();
    }

    *magnitude = Value;
    return Error_CreateSuccess();
}

static Error ParseIntegerCore(const unsigned char* str, int32_t base, uint64_t maxMagnitude, ParsedInteger* result)
{
    bool IsNegative = false;
    int32_t FinalBase = 0;
    const unsigned char* Digits = NULL;
    Error Result = Error_CreateSuccess();

    if (str == NULL)
    {
        return CreateNullArgumentError(u8"str");
    }

    if (!IsValidBase(base))
    {
        return CreateInvalidBaseError(base);
    }

    Digits = SkipOptionalSign(str, &IsNegative);
    FinalBase = DetectBase(Digits, base);
    Digits = SkipBasePrefix(Digits, FinalBase);

    Result = ParseUnsignedMagnitude(Digits, FinalBase, maxMagnitude, &result->Magnitude);
    if (Result.Code == ErrorCode_ArgumentOutOfRange)
    {
        Error_Deconstruct(&Result);
        return CreateNumberOverflowError(str, sizeof(uint64_t));
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    result->IsNegative = IsNegative;
    result->Base = FinalBase;
    return Error_CreateSuccess();
}

static bool AppendIntegerPrefix(GenericBuffer* buffer, int32_t base, bool includePrefix)
{
    if (!includePrefix)
    {
        return true;
    }

    if (base == NUMBER_BASE_16)
    {
        return GenericBuffer_AppendByte(buffer, PREFIX_START)
            && GenericBuffer_AppendByte(buffer, PREFIX_BASE_16);
    }

    if (base == NUMBER_BASE_2)
    {
        return GenericBuffer_AppendByte(buffer, PREFIX_START)
            && GenericBuffer_AppendByte(buffer, PREFIX_BASE_2);
    }

    return true;
}

static Error WriteUnsignedMagnitudeToBuffer(uint64_t magnitude, int32_t base, GenericBuffer* buffer)
{
    unsigned char Digits[INTEGER_TO_STRING_BUFFER_SIZE];
    size_t DigitCount = 0;

    do
    {
        uint64_t DigitValue = magnitude % (uint64_t)base;
        if (DigitValue <= 9U)
        {
            Digits[DigitCount] = (unsigned char)(u8'0' + DigitValue);
        }
        else
        {
            Digits[DigitCount] = (unsigned char)(u8'a' + (DigitValue - 10U));
        }

        magnitude /= (uint64_t)base;
        DigitCount++;
    } while (magnitude != 0U);

    while (DigitCount > 0U)
    {
        DigitCount--;
        if (!GenericBuffer_AppendByte(buffer, Digits[DigitCount]))
        {
            return CreateDecimalBufferError();
        }
    }

    return Error_CreateSuccess();
}

static Error WriteIntegerCore(uint64_t magnitude, bool isNegative, int32_t base, bool includePrefix, GenericBuffer* buffer)
{
    Error Result = Error_CreateSuccess();

    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }

    if (!IsValidBase(base) || (base == NUMBER_BASE_AUTO_DETECT))
    {
        return CreateInvalidBaseError(base);
    }

    if (isNegative && !GenericBuffer_AppendByte(buffer, MINUS_SIGN))
    {
        return CreateDecimalBufferError();
    }

    if (!AppendIntegerPrefix(buffer, base, includePrefix))
    {
        return CreateDecimalBufferError();
    }

    Result = WriteUnsignedMagnitudeToBuffer(magnitude, base, buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!GenericBuffer_NullTerminate(buffer))
    {
        return CreateDecimalBufferError();
    }

    return Error_CreateSuccess();
}

static Error BuildNormalizedDecimalString(const unsigned char* str, DecimalSeparator separator, char** normalizedOut)
{
    size_t Length = 0;
    char* Normalized = NULL;
    bool HasSeparator = false;
    bool HasExponent = false;
    bool HasDigit = false;

    if (str == NULL)
    {
        return CreateNullArgumentError(u8"str");
    }

    while (str[Length] != 0)
    {
        Length++;
    }

    Normalized = Memory_Allocate(Length + 1U);
    if (Normalized == NULL)
    {
        return Error_Construct5(ErrorCode_BufferTooSmall);
    }

    for (size_t Index = 0; Index < Length; Index++)
    {
        unsigned char Character = str[Index];
        bool IsExponentCharacter = (Character == EXPONENT_LOWER) || (Character == EXPONENT_UPPER);
        bool IsSignCharacter = (Character == PLUS_SIGN) || (Character == MINUS_SIGN);

        if ((Character >= u8'0') && (Character <= u8'9'))
        {
            Normalized[Index] = (char)Character;
            HasDigit = true;
            continue;
        }

        if (IsSignCharacter)
        {
            bool IsSignPositionValid = (Index == 0U)
                || (((str[Index - 1U] == EXPONENT_LOWER) || (str[Index - 1U] == EXPONENT_UPPER)) && (Index > 0U));
            if (!IsSignPositionValid)
            {
                Memory_Free(Normalized);
                return CreateInvalidDecimalStringError(str);
            }

            Normalized[Index] = (char)Character;
            continue;
        }

        if (IsExponentCharacter)
        {
            if (HasExponent || !HasDigit)
            {
                Memory_Free(Normalized);
                return CreateInvalidDecimalStringError(str);
            }

            HasExponent = true;
            Normalized[Index] = (char)EXPONENT_LOWER;
            continue;
        }

        if ((Character == PERIOD_SEPARATOR) || (Character == COMMA_SEPARATOR))
        {
            bool IsSeparatorAllowed = (separator == DecimalSeparator_Any)
                || ((separator == DecimalSeparator_Period) && (Character == PERIOD_SEPARATOR))
                || ((separator == DecimalSeparator_Comma) && (Character == COMMA_SEPARATOR));
            if (!IsSeparatorAllowed)
            {
                Memory_Free(Normalized);
                return CreateInvalidDecimalSeparatorError(Character);
            }
            if (HasSeparator || HasExponent)
            {
                Memory_Free(Normalized);
                return CreateInvalidDecimalStringError(str);
            }

            HasSeparator = true;
            Normalized[Index] = '.';
            continue;
        }

        Memory_Free(Normalized);
        return CreateInvalidDecimalStringError(str);
    }

    if (!HasDigit)
    {
        Memory_Free(Normalized);
        return CreateAtLeastOneDigitRequiredError();
    }

    Normalized[Length] = '\0';
    *normalizedOut = Normalized;
    return Error_CreateSuccess();
}

static bool TryParseSpecialDouble(const unsigned char* str, double* value)
{
    if (EqualsIgnoreCaseAscii(str, STRING_NAN_LOWER))
    {
        *value = NAN_DOUBLE;
        return true;
    }

    if (EqualsIgnoreCaseAscii(str, STRING_POSITIVE_INFINITY))
    {
        *value = INFINITY_POS_DOUBLE;
        return true;
    }

    if (EqualsIgnoreCaseAscii(str, STRING_NEGATIVE_INFINITY))
    {
        *value = INFINITY_NEG_DOUBLE;
        return true;
    }

    return false;
}

static void ReplaceDecimalSeparator(unsigned char* str, size_t count, DecimalSeparator separator)
{
    unsigned char DesiredSeparator = PERIOD_SEPARATOR;
    if (separator == DecimalSeparator_Comma)
    {
        DesiredSeparator = COMMA_SEPARATOR;
    }

    for (size_t Index = 0; Index < count; Index++)
    {
        if (str[Index] == PERIOD_SEPARATOR)
        {
            str[Index] = DesiredSeparator;
        }
    }
}

static void CreateDecimalPrintfFormat(DecimalFormatOptions options, char* format, size_t formatSize)
{
    char Conversion = 'f';

    if (options._isScientificNotation)
    {
        Conversion = options._isUpperCase ? 'E' : 'e';
        if (options._digitCountAfterSeparator >= 0)
        {
            snprintf(format, formatSize, "%%.%d%c", options._digitCountAfterSeparator, Conversion);
        }
        else
        {
            snprintf(format, formatSize, "%%%c", Conversion);
        }
        return;
    }

    if (options._digitCountAfterSeparator == DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST)
    {
        Conversion = options._isUpperCase ? 'G' : 'g';
        snprintf(format, formatSize, "%%%c", Conversion);
        return;
    }

    if (options._digitCountAfterSeparator == DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED)
    {
        snprintf(format, formatSize, "%%f");
        return;
    }

    snprintf(format, formatSize, "%%.%df", options._digitCountAfterSeparator);
}

static Error AppendFormattedDouble(double value, GenericBuffer* buffer, DecimalFormatOptions options)
{
    char Format[DECIMAL_FORMAT_BUFFER_SIZE];
    int RequiredCharacterCount = 0;
    size_t RequiredSize = 0;
    unsigned char* WriteStart = NULL;

    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }

    CreateDecimalPrintfFormat(options, Format, sizeof(Format));
    RequiredCharacterCount = snprintf(NULL, 0, Format, value);
    if (RequiredCharacterCount < 0)
    {
        return CreateInvalidDecimalStringError(u8"<format>");
    }

    RequiredSize = (size_t)RequiredCharacterCount;
    if (!GenericBuffer_TryPrepareForManualMutation(buffer, RequiredSize + 1U))
    {
        return CreateDecimalBufferError();
    }

    WriteStart = buffer->_data + buffer->_count;
    snprintf((char*)WriteStart, buffer->_capacity - buffer->_count, Format, value);
    ReplaceDecimalSeparator(WriteStart, RequiredSize, options._separator);
    buffer->_count += RequiredSize;

    if (!GenericBuffer_NullTerminate(buffer))
    {
        return CreateDecimalBufferError();
    }

    return Error_CreateSuccess();
}

static Error ParseDoubleCore(const unsigned char* str, DecimalSeparator separator, double* value)
{
    char* Normalized = NULL;
    char* ParseEnd = NULL;
    double ParsedValue = 0.0;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    if (TryParseSpecialDouble(str, value))
    {
        return Error_CreateSuccess();
    }

    Result = BuildNormalizedDecimalString(str, separator, &Normalized);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    errno = 0;
    ParsedValue = strtod(Normalized, &ParseEnd);
    if ((ParseEnd == Normalized) || (*ParseEnd != '\0') || (errno == ERANGE))
    {
        Memory_Free(Normalized);
        return CreateInvalidDecimalStringError(str);
    }

    Memory_Free(Normalized);
    *value = ParsedValue;
    return Error_CreateSuccess();
}


// Public functions.
#define DEFINE_SIGNED_FROM_STRING(functionName, typeName, minValue, maxValue) \
    Error functionName(const unsigned char* str, int32_t base, typeName* value) \
    { \
        ParsedInteger Parsed = { 0 }; \
        uint64_t MaxMagnitude = 0; \
        Error Result = Error_CreateSuccess(); \
        \
        if (value == NULL) \
        { \
            return CreateNullArgumentError(u8"value"); \
        } \
        \
        Result = ParseIntegerCore(str, base, (uint64_t)(maxValue), &Parsed); \
        if (Result.Code != ErrorCode_Success) \
        { \
            if ((Result.Code == ErrorCode_ArgumentOutOfRange) || (Result.Code == ErrorCode_BufferTooSmall)) \
            { \
                Error_Deconstruct(&Result); \
                return CreateNumberOverflowError(str, sizeof(typeName)); \
            } \
            return Result; \
        } \
        \
        MaxMagnitude = Parsed.IsNegative ? ((uint64_t)(maxValue) + 1U) : (uint64_t)(maxValue); \
        if (Parsed.Magnitude > MaxMagnitude) \
        { \
            return CreateNumberOverflowError(str, sizeof(typeName)); \
        } \
        \
        if (Parsed.IsNegative) \
        { \
            if (Parsed.Magnitude == ((uint64_t)(maxValue) + 1U)) \
            { \
                *value = (minValue); \
            } \
            else \
            { \
                *value = (typeName)(-(int64_t)Parsed.Magnitude); \
            } \
        } \
        else \
        { \
            *value = (typeName)Parsed.Magnitude; \
        } \
        \
        return Error_CreateSuccess(); \
    }

#define DEFINE_UNSIGNED_FROM_STRING(functionName, typeName, maxValue) \
    Error functionName(const unsigned char* str, int32_t base, typeName* value) \
    { \
        ParsedInteger Parsed = { 0 }; \
        Error Result = Error_CreateSuccess(); \
        \
        if (value == NULL) \
        { \
            return CreateNullArgumentError(u8"value"); \
        } \
        \
        Result = ParseIntegerCore(str, base, (uint64_t)(maxValue), &Parsed); \
        if (Result.Code != ErrorCode_Success) \
        { \
            if ((Result.Code == ErrorCode_ArgumentOutOfRange) || (Result.Code == ErrorCode_BufferTooSmall)) \
            { \
                Error_Deconstruct(&Result); \
                return CreateNumberOverflowError(str, sizeof(typeName)); \
            } \
            return Result; \
        } \
        \
        if (Parsed.IsNegative || (Parsed.Magnitude > (uint64_t)(maxValue))) \
        { \
            return CreateNumberOverflowError(str, sizeof(typeName)); \
        } \
        \
        *value = (typeName)Parsed.Magnitude; \
        return Error_CreateSuccess(); \
    }

#define DEFINE_SIGNED_TO_STRING(functionName, typeName) \
    Error functionName(typeName value, int32_t base, bool includePrefix, GenericBuffer* buffer) \
    { \
        bool IsNegative = value < 0; \
        uint64_t Magnitude = 0; \
        \
        if (IsNegative) \
        { \
            Magnitude = (uint64_t)(-(value + 1)) + 1U; \
        } \
        else \
        { \
            Magnitude = (uint64_t)value; \
        } \
        \
        return WriteIntegerCore(Magnitude, IsNegative, base, includePrefix, buffer); \
    }

#define DEFINE_UNSIGNED_TO_STRING(functionName, typeName) \
    Error functionName(typeName value, int32_t base, bool includePrefix, GenericBuffer* buffer) \
    { \
        return WriteIntegerCore((uint64_t)value, false, base, includePrefix, buffer); \
    }

DEFINE_SIGNED_FROM_STRING(Number_Int8FromString, int8_t, INT8_MIN, INT8_MAX)
DEFINE_UNSIGNED_FROM_STRING(Number_UInt8FromString, uint8_t, UINT8_MAX)
DEFINE_SIGNED_FROM_STRING(Number_Int16FromString, int16_t, INT16_MIN, INT16_MAX)
DEFINE_UNSIGNED_FROM_STRING(Number_UInt16FromString, uint16_t, UINT16_MAX)
DEFINE_SIGNED_FROM_STRING(Number_Int32FromString, int32_t, INT32_MIN, INT32_MAX)
DEFINE_UNSIGNED_FROM_STRING(Number_UInt32FromString, uint32_t, UINT32_MAX)
DEFINE_SIGNED_FROM_STRING(Number_Int64FromString, int64_t, INT64_MIN, INT64_MAX)
DEFINE_UNSIGNED_FROM_STRING(Number_UInt64FromString, uint64_t, UINT64_MAX)

DEFINE_SIGNED_TO_STRING(Number_Int8ToString, int8_t)
DEFINE_UNSIGNED_TO_STRING(Number_UInt8ToString, uint8_t)
DEFINE_SIGNED_TO_STRING(Number_Int16ToString, int16_t)
DEFINE_UNSIGNED_TO_STRING(Number_UInt16ToString, uint16_t)
DEFINE_SIGNED_TO_STRING(Number_Int32ToString, int32_t)
DEFINE_UNSIGNED_TO_STRING(Number_UInt32ToString, uint32_t)
DEFINE_SIGNED_TO_STRING(Number_Int64ToString, int64_t)
DEFINE_UNSIGNED_TO_STRING(Number_UInt64ToString, uint64_t)

#undef DEFINE_SIGNED_FROM_STRING
#undef DEFINE_UNSIGNED_FROM_STRING
#undef DEFINE_SIGNED_TO_STRING
#undef DEFINE_UNSIGNED_TO_STRING

Error Number_FloatFromString(const unsigned char* str, float* value, DecimalSeparator separator)
{
    double ParsedValue = 0.0;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = ParseDoubleCore(str, separator, &ParsedValue);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!Math_IsNaNDouble(ParsedValue) && !Math_IsInfinityDouble(ParsedValue))
    {
        if ((ParsedValue > FLT_MAX) || (ParsedValue < -FLT_MAX))
        {
            return CreateNumberOverflowError(str, sizeof(float));
        }
    }

    *value = (float)ParsedValue;
    return Error_CreateSuccess();
}

Error Number_FloatToString(float value, GenericBuffer* buffer, DecimalFormatOptions options)
{
    return AppendFormattedDouble((double)value, buffer, options);
}

Error Number_DoubleFromString(const unsigned char* str, double* value, DecimalSeparator separator)
{
    return ParseDoubleCore(str, separator, value);
}

Error Number_DoubleToString(double value, GenericBuffer* buffer, DecimalFormatOptions options)
{
    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }

    if (Math_IsNaNDouble(value))
    {
        if (!GenericBuffer_AppendString(buffer, STRING_NAN_NORMAL) || !GenericBuffer_NullTerminate(buffer))
        {
            return CreateDecimalBufferError();
        }
        return Error_CreateSuccess();
    }

    if (Math_IsInfinityPosDouble(value))
    {
        if (!GenericBuffer_AppendString(buffer, STRING_POSITIVE_INFINITY) || !GenericBuffer_NullTerminate(buffer))
        {
            return CreateDecimalBufferError();
        }
        return Error_CreateSuccess();
    }

    if (Math_IsInfinityNegDouble(value))
    {
        if (!GenericBuffer_AppendString(buffer, STRING_NEGATIVE_INFINITY) || !GenericBuffer_NullTerminate(buffer))
        {
            return CreateDecimalBufferError();
        }
        return Error_CreateSuccess();
    }

    return AppendFormattedDouble(value, buffer, options);
}

DecimalFormatOptions DecimalFormatOptions_CreateScientific(DecimalSeparator separator, bool isUpperCase)
{
    return (DecimalFormatOptions)
    {
        ._separator = separator,
        ._digitCountAfterSeparator = DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED,
        ._isUpperCase = isUpperCase,
        ._isScientificNotation = true,
    };
}

DecimalFormatOptions DecimalFormatOptions_CreateFixed(DecimalSeparator separator, int32_t digitCountAfterDecimal)
{
    if (digitCountAfterDecimal < 0)
    {
        digitCountAfterDecimal = 0;
    }

    return (DecimalFormatOptions)
    {
        ._separator = separator,
        ._digitCountAfterSeparator = digitCountAfterDecimal,
        ._isUpperCase = false,
        ._isScientificNotation = false,
    };
}

DecimalFormatOptions DecimalFormatOptions_CreateShortest(DecimalSeparator separator)
{
    return (DecimalFormatOptions)
    {
        ._separator = separator,
        ._digitCountAfterSeparator = DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST,
        ._isUpperCase = false,
        ._isScientificNotation = false,
    };
}

DecimalFormatOptions DecimalFormatOptions_CreateFull(DecimalSeparator separator)
{
    return (DecimalFormatOptions)
    {
        ._separator = separator,
        ._digitCountAfterSeparator = DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED,
        ._isUpperCase = false,
        ._isScientificNotation = false,
    };
}
