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


// Fields.
extern const int32_t NUMBER_BASE_MAX;
extern const int32_t NUMBER_BASE_MIN;
extern const int32_t NUMBER_BASE_AUTO_DETECT;
extern const int32_t NUMBER_BASE_10;
extern const int32_t NUMBER_BASE_2;
extern const int32_t NUMBER_BASE_16;


// Functions.
Error Number_Int8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int8_t* value);

void Number_Int8ToString(int8_t value, int32_t base, GenericBuffer buffer);


Error Number_UInt8FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint8_t* value);

void Number_UInt8ToString(uint8_t value, int32_t base, GenericBuffer buffer);


Error Number_Int16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int16_t* value);

void Number_Int16ToString(int16_t value, int32_t base, GenericBuffer buffer);


Error Number_UInt16FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint16_t* value);

void Number_UInt16ToString(uint16_t value, int32_t base, GenericBuffer buffer);


Error Number_Int32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int32_t* value);

void Number_Int32ToString(int32_t value, int32_t base, GenericBuffer buffer);


Error Number_UInt32FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint32_t* value);

void Number_UInt32ToString(uint32_t value, int32_t base, GenericBuffer buffer);

Error Number_Int64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, int64_t* value);

void Number_Int64ToString(int64_t value, int32_t base, GenericBuffer buffer);


Error Number_UInt64FromString(ErrorMessagePool* errorPool, const unsigned char* str, int32_t base, uint64_t* value);

void Number_UInt64ToString(uint64_t value, int32_t base, GenericBuffer buffer);


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