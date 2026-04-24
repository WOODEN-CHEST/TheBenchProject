#pragma once
#include <stdint.h>
#include <stddef.h>

#define COMPARE_NUMBER(a, b) (a > b) ? ComparisonResult_AGreaterThanB : ((a == b) ? ComparisonResult_AEqualsB : ComparisonResult_ALessThanB)



// Types.
typedef enum ComparisonResultEnum
{
    ComparisonResult_ALessThanB = -1,
    ComparisonResult_AEqualsB = 0,
    ComparisonResult_AGreaterThanB = 1
} ComparisonResult;


// Functions.
static inline ComparisonResult Comparator_CompareInt8(int8_t a, int8_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareUInt8(uint8_t a, uint8_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareInt16(int16_t a, int16_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareUInt16(uint16_t a, uint16_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareInt32(int32_t a, int32_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareUInt32(uint32_t a, uint32_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareInt64(int64_t a, int64_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareUInt64(uint64_t a, uint64_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareSizeT(size_t a, size_t b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareFloat(float a, float b)
{
    return COMPARE_NUMBER(a, b);
}

static inline ComparisonResult Comparator_CompareDouble(double a, double b)
{
    return COMPARE_NUMBER(a, b);
}

ComparisonResult Comparator_CompareString(const unsigned char* a, const unsigned char* b);