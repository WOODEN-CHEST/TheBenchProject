#pragma once
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

typedef struct DecimaFormatOptionsStruct
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

Error Number_Int8ToString(ErrorMessagePool* errorPool, int8_t value, int32_t base, GenericBuffer buffer);


Error Number_UInt8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint8_t* value);

Error Number_UInt8ToString(ErrorMessagePool* errorPool, uint8_t value, int32_t base, GenericBuffer buffer);


Error Number_Int16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int16_t* value);

Error Number_Int16ToString(ErrorMessagePool* errorPool, int16_t value, int32_t base, GenericBuffer buffer);


Error Number_UInt16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint16_t* value);

Error Number_UInt16ToString(ErrorMessagePool* errorPool, uint16_t value, int32_t base, GenericBuffer buffer);


Error Number_Int32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int32_t* value);

Error Number_Int32ToString(ErrorMessagePool* errorPool, int32_t value, int32_t base, GenericBuffer buffer);


Error Number_UInt32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint32_t* value);

Error Number_UInt32ToString(ErrorMessagePool* errorPool, uint32_t value, int32_t base, GenericBuffer buffer);

Error Number_Int64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int64_t* value);

Error Number_Int64ToString(ErrorMessagePool* errorPool, int64_t value, int32_t base, GenericBuffer buffer);


Error Number_UInt64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint64_t* value);

Error Number_UInt64ToString(ErrorMessagePool* errorPool, uint64_t value, int32_t base, GenericBuffer buffer);


Error Number_FloatFromString(ErrorMessagePool* errorPool,
    const unsigned char* str,
    float* value,
    DecimalSeparator separator);

Error Number_FloatToString(ErrorMessagePool* errorPool,
    float value,
    GenericBuffer buffer,
    DecimalFormatOptions options);


Error Number_DoubleFromString(ErrorMessagePool* errorPool,
    const unsigned char* str,
    double* value,
    DecimalSeparator separator);

Error Number_DoubleToString(ErrorMessagePool* errorPool,
    double value,
    GenericBuffer buffer,
    DecimalFormatOptions options);