#pragma once
#include "WRError.h"
#include "stdint.h"
#include "WRMemory.h"



/**
 * The WRNumber module provides functions for parsing and writing numbers, as well as other
 * random stuff related to numbers, such as number unions.
 * 
 * The integer parse functions all fail with the illegal argument error if one if these conditions is met:
 *  * The string is empty.
 *  * The contains characters that are not valid digits in the specified base. 
 *    This includes leading and trailing whitespace, so the strings containing the numbers must be trimmed.
 *  * The string contains more than one sign ('+' or '-') or the sign is not at the beginning of the string.
 *  * The passed in base is not between NUMBER_BASE_MIN and NUMBER_BASE_MAX, and not set to NUMBER_BASE_AUTO_DETECT.
 *  * The number stored in the string is too large to fit in the specified type. This includes trying to parse a negative
 *    number and store it in an unsigned number.
 * When parsing a number with the base being set to be auto-detected, the base will first be set to base10.
 * If the prefix 0x or 0X will be detected at the start of the string but after the (optionally present) sign, then the base will
 * be set to base16. Same applies for the prefix 0b or 0B; then the base will be set to base 2. If none of said prefixes are present
 * at the start of the string and a base 16 letter (a-f) is found in the string, the base is set to be base 16. Multiple base
 * prefixes are not allowed, the maximum number of them is 1.
 * If parsing a string fails, no output is written to the out integer value.
 * 
 * The integer ToString functions all fail if one of these conditions is met:
 *  * The passed in base is not between NUMBER_BASE_MIN and NUMBER_BASE_MAX,
 *    base NUMBER_BASE_AUTO_DETECT is invalid for writing a string (illegal argument error).
 *  * The passed in buffer runs out of capacity before the entire string,
 *    including the null terminator, could be written. (buffer too small error.)
 * If the includePrefix value is set to true, the prefix 0x or 0b will be written before the number
 * (but after the sign) if the base is 16 or 2 respectively. Other bases will not have a prefix written.
 * If the given buffer is too small to hold the resulting string version of the number, the buffer will contain the
 * incomplete data and shouldn't be read a string.
 * 
 * The float / double parse functions all fail with the illegal argument exception if one of these conditions is met:
 *  * The
 */


// Types.
typedef enum DecimalSeparatorEnum
{
    DecimalSeparator_Period,
    DecimalSeparator_Comma,
    DecimalSeparator_Any
} DecimalSeparator;

typedef union StandardIntegersUnion
{
    int8_t Int8;
    uint8_t UInt8;
    int16_t Int16;
    uint16_t UInt16;
    int32_t Int32;
    uint32_t UInt32;
    int64_t Int64;
    uint64_t UInt64;
} StandardIntegers;

typedef struct DecimalFormatOptionsStruct
{
    DecimalSeparator _separator;
    int32_t _digitCountAfterSeparator;
    bool _isUpperCase;
    bool _isScientificNotation;
} DecimalFormatOptions;



// Fields.
extern const int32_t NUMBER_BASE_MAX;
extern const int32_t NUMBER_BASE_MIN;
extern const int32_t NUMBER_BASE_AUTO_DETECT;
extern const int32_t NUMBER_BASE_10;
extern const int32_t NUMBER_BASE_2;
extern const int32_t NUMBER_BASE_16;

extern const int32_t DIGIT_COUNT_AFTER_SEPARATOR_SHORTEST;
extern const int32_t DIGIT_COUNT_AFTER_SEPARATOR_UNLIMITED;


// Functions.
Error Number_Int8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int8_t* value);

Error Number_Int8ToString(ErrorMessagePool* errorPool, int8_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_UInt8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint8_t* value);

Error Number_UInt8ToString(ErrorMessagePool* errorPool, uint8_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_Int16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int16_t* value);

Error Number_Int16ToString(ErrorMessagePool* errorPool, int16_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_UInt16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint16_t* value);

Error Number_UInt16ToString(ErrorMessagePool* errorPool, uint16_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_Int32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int32_t* value);

Error Number_Int32ToString(ErrorMessagePool* errorPool, int32_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_UInt32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint32_t* value);

Error Number_UInt32ToString(ErrorMessagePool* errorPool, uint32_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_Int64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int64_t* value);

Error Number_Int64ToString(ErrorMessagePool* errorPool, int64_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_UInt64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint64_t* value);

Error Number_UInt64ToString(ErrorMessagePool* errorPool, uint64_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_FloatFromString(ErrorMessagePool* errorPool,
    const unsigned char* str,
    float* value,
    DecimalSeparator separator);

Error Number_FloatToString(ErrorMessagePool* errorPool,
    float value,
    GenericBuffer* buffer,
    DecimalFormatOptions options);


Error Number_DoubleFromString(ErrorMessagePool* errorPool,
    const unsigned char* str,
    double* value,
    DecimalSeparator separator);

Error Number_DoubleToString(ErrorMessagePool* errorPool,
    double value,
    GenericBuffer* buffer,
    DecimalFormatOptions options);

/**
 * Creates decimal format options which can be used to write the number in scientific format.
 */
DecimalFormatOptions DecimalFormatOptions_CreateScientific(DecimalSeparator separator, bool isUpperCase);

/**
 * Creates decimal format options which can be used to write the number with a fixed number of digits after the decimal separator.
 * @param digitCountAfterDecimal The number of digits after the decimal separator.
 */
DecimalFormatOptions DecimalFormatOptions_CreateFixed(DecimalSeparator separator, int32_t digitCountAfterDecimal);

/**
 * Creates decimal format options which can be used to write the number with the lower number of digits after the decimal separator.
 */
DecimalFormatOptions DecimalFormatOptions_CreateShortest(DecimalSeparator separator);

/**
 * Creates decimal format options which can be used to write the full, non-trimmed number with all digits after the decimal separator.
 */
DecimalFormatOptions DecimalFormatOptions_CreateFull(DecimalSeparator separator);