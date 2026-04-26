#include "WRMath.h"
#include <stdint.h>
#include <math.h>


// Macros.


#define SUFFIX_FLOAT f
#define SUFFIX_DOUBLE
#define WRITE_DECIMAL(value, suffix) value##suffix

// Well...
#define DECIMAL_ROUND_IMPLEMENTATION(typeName, funcTypeName, suffix, roundingThreshold, marginOfErrorScale)\
    static const typeName RoundingThresholds##funcTypeName[] =\
    {\
        roundingThreshold,\
        roundingThreshold / WRITE_DECIMAL(10.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(100.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(1'000.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(10'000.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(100'000.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(1'000'000.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(10'000'000.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(100'000'000.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(1'000'000'000.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(10'000'000'000.0, suffix),\
        roundingThreshold / WRITE_DECIMAL(100'000'000'000.0, suffix),\
    };\
    \
    static inline typeName GetRoundingThreshold##funcTypeName(uint32_t digitsAfterDecimal)\
    {\
        if ((size_t)digitsAfterDecimal >= (sizeof(RoundingThresholds##funcTypeName) / sizeof(RoundingThresholds##funcTypeName[0])))\
        {\
            return roundingThreshold / Math_Pow##funcTypeName(WRITE_DECIMAL(10.0, suffix), (typeName)digitsAfterDecimal);\
        }\
        return RoundingThresholds##funcTypeName[digitsAfterDecimal];\
    }\
    \
    static bool IsMagnitudeIncreasedOnMidpoint##funcTypeName(typeName number, typeName threshold, RoundingType type)\
    {\
        if (type == RoundingType_AwayFromZero)\
        {\
            return true;\
        }\
        else if (type == RoundingType_ToZero)\
        {\
            return false;\
        }\
        else if (type == RoundingType_ToEven)\
        {\
            return Math_Remainder##funcTypeName(Math_Abs##funcTypeName(number),\
                WRITE_DECIMAL(2.0, suffix) * threshold / roundingThreshold) >= WRITE_DECIMAL(1.0, suffix);\
        }\
        else if (type == RoundingType_ToNegativeInfinity)\
        {\
            return (number > WRITE_DECIMAL(0.0, suffix)) ? false : true;\
        }\
        else if (type == RoundingType_ToPositiveInfinity)\
        {\
            return (number > WRITE_DECIMAL(0.0, suffix)) ? true : false;\
        }\
        return false;\
    }\
    \
    typeName Math_Round##funcTypeName(typeName value, RoundingOptions options)\
    {\
        if (Math_IsNaN##funcTypeName(value) || Math_IsInfinity##funcTypeName(value))\
        {\
            return value;\
        }\
        \
        typeName Threshold = GetRoundingThreshold##funcTypeName(options._digitCountAfterDecimal);\
        typeName Denominator = Threshold * (WRITE_DECIMAL(1.0, suffix) / roundingThreshold);\
        typeName OriginalRelevantFraction = Math_Remainder##funcTypeName(Math_Abs##funcTypeName(value), Denominator);\
        \
        bool IsMagnitudeIncreased;\
        typeName MarginOfError = marginOfErrorScale * Threshold;\
        if (Math_Abs##funcTypeName(OriginalRelevantFraction - Threshold) <= MarginOfError)\
        {\
            IsMagnitudeIncreased = IsMagnitudeIncreasedOnMidpoint##funcTypeName(value, Threshold, options._type);\
        }\
        else if (OriginalRelevantFraction > Threshold)\
        {\
            IsMagnitudeIncreased = true;\
        }\
        else\
        {\
            IsMagnitudeIncreased = false;\
        }\
        \
        typeName ValueToAdd = (IsMagnitudeIncreased ? (Threshold - OriginalRelevantFraction) : (-OriginalRelevantFraction));\
        if (value < WRITE_DECIMAL(0.0, suffix))\
        {\
            ValueToAdd *= WRITE_DECIMAL(-1.0, suffix);\
        }\
        return value + ValueToAdd;\
    }


// Types.


// Fields.


// Static functions.


// Public functions.
RoundingOptions RoundingOptions_CreateNormal(void)
{
    return (RoundingOptions)
    {
        ._type = RoundingType_ToEven,
        ._digitCountAfterDecimal = 0,
    };
}

RoundingOptions RoundingOptions_CreateWithDigitCount(uint32_t digitCount)
{
    return (RoundingOptions)
    {
        ._type = RoundingType_ToEven,
        ._digitCountAfterDecimal = digitCount,
    };
}

RoundingOptions RoundingOptions_CreateWithType(RoundingType type)
{
    return (RoundingOptions)
    {
        ._type = type,
        ._digitCountAfterDecimal = 0,
    };
}

RoundingOptions RoundingOptions_CreateFull(RoundingType type, uint32_t digitCount)
{
    return (RoundingOptions)
    {
        ._type = type,
        ._digitCountAfterDecimal = digitCount,
    };
}

DECIMAL_ROUND_IMPLEMENTATION(double, Double, SUFFIX_DOUBLE, 0.5, 0.0000000001)
DECIMAL_ROUND_IMPLEMENTATION(float, Float, SUFFIX_FLOAT, 0.5f, 0.00001f)
#undef DECIMAL_ROUND_IMPLEMENTATION

float Math_RemainderFloat(float x, float y)
{
    return fmodf(x, y);
}

float Math_PowFloat(float value, float exponent)
{
    return powf(value, exponent);
}

float Math_Log10Float(float value)
{
    return log10f(value);
}

float Math_Log2Float(float value)
{
    return log2f(value);
}

float Math_LogNaturalFloat(float value)
{
    return logf(value);
}

float Math_LogFloat(float value, float base)
{
    return log10f(value) / log10f(base);
}

float Math_SqrtFloat(float value)
{
    return sqrtf(value);
}

float Math_CbrtFloat(float value)
{
    return cbrtf(value);
}

float Math_NthRootFloat(float value, float root)
{
    return powf(value, 1.0f / root);
}

float Math_SinFloat(float value)
{
    return sinf(value);
}

float Math_CosFloat(float value)
{
    return cosf(value);
}

float Math_TanFloat(float value)
{
    return tanf(value);
}

float Math_ASinFloat(float value)
{
    return asinf(value);
}

float Math_ACosFloat(float value)
{
    return acosf(value);
}

float Math_ATanFloat(float value)
{
    return atanf(value);
}

float Math_ATan2Float(float x, float y)
{
    return atan2f(y, x);
}

float Math_SinHypFloat(float value)
{
    return sinhf(value);
}

float Math_CosHypFloat(float value)
{
    return coshf(value);
}

float Math_TanHypFloat(float value)
{
    return tanhf(value);
}

float Math_ASinHypFloat(float value)
{
    return asinhf(value);
}

float Math_ACosHypFloat(float value)
{
    return acoshf(value);
}

float Math_ATanHypFloat(float value)
{
    return atanhf(value);
}

float Math_CeilFloat(float value)
{
    return ceilf(value);
}

float Math_FloorFloat(float value)
{
    return floorf(value);
}

float Math_TruncateFloat(float value)
{
    return truncf(value);
}

float Math_SplitNumberFloat(float number, float* integralPart)
{
    return modff(number, integralPart);
}

double Math_PowDouble(double value, double exponent)
{
    return pow(value, exponent);
}

double Math_Log10Double(double value)
{
    return log10(value);
}

double Math_Log2Double(double value)
{
    return log2(value);
}

double Math_LogNaturalDouble(double value)
{
    return log(value);
}

double Math_LogDouble(double value, double base)
{
    return log(value) / log(base);
}

double Math_SqrtDouble(double value)
{
    return sqrt(value);
}

double Math_CbrtDouble(double value)
{
    return cbrt(value);
}

double Math_NthRootDouble(double value, double root)
{
    return pow(value, 1.0 / root);
}

double Math_SinDouble(double value)
{
    return sin(value);
}

double Math_CosDouble(double value)
{
    return cos(value);
}

double Math_TanDouble(double value)
{
    return tan(value);
}

double Math_ASinDouble(double value)
{
    return asin(value);
}

double Math_ACosDouble(double value)
{
    return acos(value);
}

double Math_ATanDouble(double value)
{
    return atan(value);
}

double Math_ATan2Double(double x, double y)
{
    return atan2(y, x);
}

double Math_SinHypDouble(double value)
{
    return sinh(value);
}

double Math_CosHypDouble(double value)
{
    return cosh(value);
}

double Math_TanHypDouble(double value)
{
    return tanh(value);
}

double Math_ASinHypDouble(double value)
{
    return asinh(value);
}

double Math_ACosHypDouble(double value)
{
    return acosh(value);
}

double Math_ATanHypDouble(double value)
{
    return atanh(value);
}

double Math_CeilDouble(double value)
{
    return ceil(value);
}

double Math_FloorDouble(double value)
{
    return floor(value);
}

double Math_TruncateDouble(double value)
{
    return trunc(value);
}

double Math_SplitNumberDouble(double number, double* integralPart)
{
    return modf(number, integralPart);
}

double Math_RemainderDouble(double x, double y)
{
    return fmod(x, y);
}
