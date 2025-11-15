#pragma once
#include <stdint.h>


// Fields.
extern const float NAN_FLOAT;
extern const float INFINITY_POS_FLOAT;
extern const float INFINITY_NEG_FLOAT;
extern const float MAX_FLOAT;
extern const float MIN_FLOAT;
extern const float EPSILON_FLOAT;
extern const float PI_FLOAT;
extern const float TAU_FLOAT;
extern const float E_FLOAT;

extern const double NAN_DOUBLE;
extern const double INFINITY_POS_DOUBLE;
extern const double INFINITY_NEG_DOUBLE;
extern const double MAX_DOUBLE;
extern const double MIN_DOUBLE;
extern const double EPSILON_DOUBLE;
extern const double PI_DOUBLE;
extern const double TAU_DOUBLE;
extern const double E_DOUBLE;


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

bool Math_IsNaNFloat(float value);

bool Math_IsInfinityFloat(float value);

bool Math_IsInfinityPosFloat(float value);

bool Math_IsInfinityNegFloat(float value);

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

bool Math_IsNaNDouble(double value);

bool Math_IsInfinityDouble(double value);

bool Math_IsInfinityPosDouble(double value);

bool Math_IsInfinityNegDouble(double value);

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