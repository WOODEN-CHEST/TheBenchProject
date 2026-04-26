#pragma once
#include <stdbool.h>
#include "WRError.h"
#include "stdint.h"
#include "WRMemory.h"



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
Error Number_Int8FromString(const unsigned char* str, int32_t base, int8_t* value);

Error Number_Int8ToString(int8_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_UInt8FromString(const unsigned char* str, int32_t base, uint8_t* value);

Error Number_UInt8ToString(uint8_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_Int16FromString(const unsigned char* str, int32_t base, int16_t* value);

Error Number_Int16ToString(int16_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_UInt16FromString(const unsigned char* str, int32_t base, uint16_t* value);

Error Number_UInt16ToString(uint16_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_Int32FromString(const unsigned char* str, int32_t base, int32_t* value);

Error Number_Int32ToString(int32_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_UInt32FromString(const unsigned char* str, int32_t base, uint32_t* value);

Error Number_UInt32ToString(uint32_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_Int64FromString(const unsigned char* str, int32_t base, int64_t* value);

Error Number_Int64ToString(int64_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_UInt64FromString(const unsigned char* str, int32_t base, uint64_t* value);

Error Number_UInt64ToString(uint64_t value, int32_t base, bool includePrefix, GenericBuffer* buffer);


Error Number_FloatFromString(const unsigned char* str,
    float* value,
    DecimalSeparator separator);

Error Number_FloatToString(float value,
    GenericBuffer* buffer,
    DecimalFormatOptions options);


Error Number_DoubleFromString(const unsigned char* str,
    double* value,
    DecimalSeparator separator);

Error Number_DoubleToString(double value,
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
