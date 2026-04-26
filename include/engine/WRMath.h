#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef union FloatBitsUnion
{
    float Float;
    uint32_t Int;
} FloatBits;

typedef union DoubleBitsUnion
{
    double Double;
    uint64_t Int;
} DoubleBits;


#define NAN_FLOAT ((FloatBits) { .Int = 0x7F800001 }.Float)
#define INFINITY_POS_FLOAT ((FloatBits) { .Int = 0x7F800000 }.Float)
#define INFINITY_NEG_FLOAT ((FloatBits) { .Int = 0xFF800000 }.Float)
#define MAX_FLOAT ((FloatBits) { .Int = 0x7F7FFFFF }.Float)
#define MIN_FLOAT ((FloatBits) { .Int = 0xFF7FFFFF }.Float)
#define PI_FLOAT 3.14159265358979323846f
#define TAU_FLOAT (PI_FLOAT * 2.0f)
#define E_FLOAT 2.71828182845904523536028f

#define NAN_DOUBLE ((DoubleBits) { .Int = 0x7FF0000000000001 }.Double)
#define INFINITY_POS_DOUBLE ((DoubleBits) { .Int = 0x7FF0000000000000 }.Double)
#define INFINITY_NEG_DOUBLE ((DoubleBits) { .Int = 0xFFF0000000000000 }.Double)
#define MAX_DOUBLE ((DoubleBits) { .Int = 0x7FEFFFFFFFFFFFFF }.Double)
#define MIN_DOUBLE ((DoubleBits) { .Int = 0xFFEFFFFFFFFFFFFF }.Double)
#define PI_DOUBLE 3.14159265358979323846
#define TAU_DOUBLE (PI_DOUBLE * 2.0)
#define E_DOUBLE 2.71828182845904523536028

#define FLOAT_BIT_MASK_SIGN 0x80000000
#define FLOAT_BIT_MASK_EXPONENT 0x7F800000
#define FLOAT_BIT_MASK_MANTISSA 0x007FFFFF

#define DOUBLE_BIT_MASK_SIGN 0x8000000000000000
#define DOUBLE_BIT_MASK_EXPONENT 0x7FF0000000000000
#define DOUBLE_BIT_MASK_MANTISSA 0x000FFFFFFFFFFFFF


// Types.
typedef enum RoundingTypeEnum
{
    RoundingType_ToZero,
    RoundingType_ToEven,
    RoundingType_AwayFromZero,
    RoundingType_ToNegativeInfinity,
    RoundingType_ToPositiveInfinity
} RoundingType;

typedef struct RoundingOptionsStruct
{
    RoundingType _type;
    uint32_t _digitCountAfterDecimal;
} RoundingOptions;


// Functions.
RoundingOptions RoundingOptions_CreateNormal(void);

RoundingOptions RoundingOptions_CreateWithDigitCount(uint32_t digitCount);

RoundingOptions RoundingOptions_CreateWithType(RoundingType type);

RoundingOptions RoundingOptions_CreateFull(RoundingType type, uint32_t digitCount);


/* Float. */
float Math_RemainderFloat(float x, float y);

float Math_PowFloat(float value, float exponent);

float Math_Log10Float(float value);

float Math_Log2Float(float value);

float Math_LogNaturalFloat(float value);

float Math_LogFloat(float value, float base);

float Math_SqrtFloat(float value);

float Math_CbrtFloat(float value);

float Math_NthRootFloat(float value, float root);

float Math_SinFloat(float value);

float Math_CosFloat(float value);

float Math_TanFloat(float value);

float Math_ASinFloat(float value);

float Math_ACosFloat(float value);

float Math_ATanFloat(float value);

float Math_ATan2Float(float x, float y);

float Math_SinHypFloat(float value);

float Math_CosHypFloat(float value);

float Math_TanHypFloat(float value);

float Math_ASinHypFloat(float value);

float Math_ACosHypFloat(float value);

float Math_ATanHypFloat(float value);

float Math_CeilFloat(float value);

float Math_FloorFloat(float value);

float Math_RoundFloat(float value, RoundingOptions options);

float Math_TruncateFloat(float value);

float Math_SplitNumberFloat(float number, float* integralPart);

static inline bool Math_IsNaNFloat(float value)
{
    FloatBits Bits = (FloatBits) { .Float = value };
    return ((Bits.Int & FLOAT_BIT_MASK_EXPONENT) == FLOAT_BIT_MASK_EXPONENT)
        && ((Bits.Int & FLOAT_BIT_MASK_MANTISSA) != 0);
}

static inline bool Math_IsInfinityFloat(float value)
{
    FloatBits Bits = (FloatBits) { .Float = value };
    return ((Bits.Int & FLOAT_BIT_MASK_EXPONENT) == FLOAT_BIT_MASK_EXPONENT)
        && ((Bits.Int & FLOAT_BIT_MASK_MANTISSA) == 0);
}

static inline bool Math_IsInfinityPosFloat(float value)
{
    FloatBits Bits = (FloatBits) { .Float = value };
    return Math_IsInfinityFloat(value) && ((Bits.Int & FLOAT_BIT_MASK_SIGN) == 0);
}

static inline bool Math_IsInfinityNegFloat(float value)
{
    FloatBits Bits = (FloatBits) { .Float = value };
    return Math_IsInfinityFloat(value) && ((Bits.Int & FLOAT_BIT_MASK_SIGN) != 0);
}

static inline float Math_MinFloat(float a, float b)
{
    return (a > b) ? b : a;
}

static inline float Math_MaxFloat(float a, float b)
{
    return (a < b) ? b : a;
}

static inline float Math_ClampFloat(float value, float min, float max)
{
    return Math_MaxFloat(min, Math_MinFloat(value, max));
}

static inline float Math_AbsFloat(float value)
{
    return (value < 0.0f) ? -value : value;
}

static inline int32_t Math_SignFloat(float value)
{
    if (value > 0.0f)
    {
        return 1;
    }
    if (value < 0.0f)
    {
        return -1;
    }
    return 0;
}

static inline float Math_LerpFloat(float min, float max, float amount)
{
    return min + ((max - min) * amount);
}

static inline float Math_DegToRadFloat(float deg)
{
    return deg / 180.0f * PI_FLOAT;
}

static inline float Math_RadToDegFloat(float rad)
{
    return rad / PI_FLOAT * 180.0f;
}

static inline bool Math_EqualsCloseFloat(float a, float b, float marginOfError)
{
    return Math_AbsFloat(a - b) <= marginOfError;
}

static inline float Math_NormalizeFloat(float value, float min, float max)
{
    return (value - min) / (max - min);
}

static inline float Math_MapFloat(float value, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + ((outMax - outMin) * Math_NormalizeFloat(value, inMin, inMax));
}


/* Double. */
double Math_RemainderDouble(double x, double y);

double Math_PowDouble(double value, double exponent);

double Math_Log10Double(double value);

double Math_Log2Double(double value);

double Math_LogNaturalDouble(double value);

double Math_LogDouble(double value, double base);

double Math_SqrtDouble(double value);

double Math_CbrtDouble(double value);

double Math_NthRootDouble(double value, double root);

double Math_SinDouble(double value);

double Math_CosDouble(double value);

double Math_TanDouble(double value);

double Math_ASinDouble(double value);

double Math_ACosDouble(double value);

double Math_ATanDouble(double value);

double Math_ATan2Double(double x, double y);

double Math_SinHypDouble(double value);

double Math_CosHypDouble(double value);

double Math_TanHypDouble(double value);

double Math_ASinHypDouble(double value);

double Math_ACosHypDouble(double value);

double Math_ATanHypDouble(double value);

double Math_CeilDouble(double value);

double Math_FloorDouble(double value);

double Math_RoundDouble(double value, RoundingOptions options);

double Math_TruncateDouble(double value);

double Math_SplitNumberDouble(double number, double* integralPart);

static inline bool Math_IsNaNDouble(double value)
{
    DoubleBits Bits = (DoubleBits) { .Double = value };
    return ((Bits.Int & DOUBLE_BIT_MASK_EXPONENT) == DOUBLE_BIT_MASK_EXPONENT)
        && ((Bits.Int & DOUBLE_BIT_MASK_MANTISSA) != 0);
}

static inline bool Math_IsInfinityDouble(double value)
{
    DoubleBits Bits = (DoubleBits) { .Double = value };
    return ((Bits.Int & DOUBLE_BIT_MASK_EXPONENT) == DOUBLE_BIT_MASK_EXPONENT)
        && ((Bits.Int & DOUBLE_BIT_MASK_MANTISSA) == 0);
}

static inline bool Math_IsInfinityPosDouble(double value)
{
    DoubleBits Bits = (DoubleBits) { .Double = value };
    return Math_IsInfinityDouble(value) && ((Bits.Int & DOUBLE_BIT_MASK_SIGN) == 0);
}

static inline bool Math_IsInfinityNegDouble(double value)
{
    DoubleBits Bits = (DoubleBits) { .Double = value };
    return Math_IsInfinityDouble(value) && ((Bits.Int & DOUBLE_BIT_MASK_SIGN) != 0);
}

static inline double Math_MinDouble(double a, double b)
{
    return (a > b) ? b : a;
}

static inline double Math_MaxDouble(double a, double b)
{
    return (a < b) ? b : a;
}

static inline double Math_ClampDouble(double value, double min, double max)
{
    return Math_MaxDouble(min, Math_MinDouble(value, max));
}

static inline double Math_AbsDouble(double value)
{
    return (value < 0.0) ? -value : value;
}

static inline int32_t Math_SignDouble(double value)
{
    if (value > 0.0)
    {
        return 1;
    }
    if (value < 0.0)
    {
        return -1;
    }
    return 0;
}

static inline double Math_LerpDouble(double min, double max, double amount)
{
    return min + ((max - min) * amount);
}

static inline double Math_DegToRadDouble(double deg)
{
    return deg / 180.0 * PI_DOUBLE;
}

static inline double Math_RadToDegDouble(double rad)
{
    return rad / PI_DOUBLE * 180.0;
}

static inline bool Math_EqualsCloseDouble(double a, double b, double marginOfError)
{
    return Math_AbsDouble(a - b) <= marginOfError;
}

static inline double Math_NormalizeDouble(double value, double min, double max)
{
    return (value - min) / (max - min);
}

static inline double Math_MapDouble(double value, double inMin, double inMax, double outMin, double outMax)
{
    return outMin + ((outMax - outMin) * Math_NormalizeDouble(value, inMin, inMax));
}


/* Int32. */
static inline int32_t Math_MinInt32(int32_t a, int32_t b)
{
    return (a > b) ? b : a;
}

static inline int32_t Math_MaxInt32(int32_t a, int32_t b)
{
    return (a < b) ? b : a;
}

static inline int32_t Math_ClampInt32(int32_t value, int32_t min, int32_t max)
{
    return Math_MaxInt32(min, Math_MinInt32(value, max));
}

static inline int32_t Math_AbsInt32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static inline int32_t Math_SignInt32(int32_t value)
{
    if (value > 0)
    {
        return 1;
    }
    if (value < 0)
    {
        return -1;
    }
    return 0;
}


/* Int64. */
static inline int64_t Math_MinInt64(int64_t a, int64_t b)
{
    return (a > b) ? b : a;
}

static inline int64_t Math_MaxInt64(int64_t a, int64_t b)
{
    return (a < b) ? b : a;
}

static inline int64_t Math_ClampInt64(int64_t value, int64_t min, int64_t max)
{
    return Math_MaxInt64(min, Math_MinInt64(value, max));
}

static inline int64_t Math_AbsInt64(int64_t value)
{
    return (value < 0) ? -value : value;
}

static inline int32_t Math_SignInt64(int64_t value)
{
    if (value > 0)
    {
        return 1;
    }
    if (value < 0)
    {
        return -1;
    }
    return 0;
}

/* size_t */
static inline size_t Math_MinSizeT(size_t a, size_t b)
{
    return (a > b) ? b : a;
}

static inline size_t Math_MaxSizeT(size_t a, size_t b)
{
    return (a < b) ? b : a;
}

static inline size_t Math_ClampSizeT(size_t value, size_t min, size_t max)
{
    return Math_MaxSizeT(min, Math_MinSizeT(value, max));
}
