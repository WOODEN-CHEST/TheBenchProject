#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Type-punning union providing access to the raw IEEE-754 bit pattern of a 32-bit float.
 *
 * Writing one member and reading the other reinterprets the same 4 bytes, allowing inspection or
 * construction of a float from its sign/exponent/mantissa bits.
 */
typedef union FloatBitsUnion
{
    float Float;    /**< The value interpreted as a 32-bit IEEE-754 single-precision float. */
    uint32_t Int;   /**< The same 4 bytes interpreted as an unsigned 32-bit integer (the raw bit pattern). */
} FloatBits;

/**
 * @brief Type-punning union providing access to the raw IEEE-754 bit pattern of a 64-bit double.
 *
 * Writing one member and reading the other reinterprets the same 8 bytes, allowing inspection or
 * construction of a double from its sign/exponent/mantissa bits.
 */
typedef union DoubleBitsUnion
{
    double Double;  /**< The value interpreted as a 64-bit IEEE-754 double-precision float. */
    uint64_t Int;   /**< The same 8 bytes interpreted as an unsigned 64-bit integer (the raw bit pattern). */
} DoubleBits;


/** @brief A quiet not-a-number (NaN) value of type float, built from a fixed IEEE-754 bit pattern. */
#define NAN_FLOAT ((FloatBits) { .Int = 0x7F800001 }.Float)
/** @brief Positive infinity of type float (IEEE-754 +inf). */
#define INFINITY_POS_FLOAT ((FloatBits) { .Int = 0x7F800000 }.Float)
/** @brief Negative infinity of type float (IEEE-754 -inf). */
#define INFINITY_NEG_FLOAT ((FloatBits) { .Int = 0xFF800000 }.Float)
/** @brief The largest finite positive float (equivalent to FLT_MAX). */
#define MAX_FLOAT ((FloatBits) { .Int = 0x7F7FFFFF }.Float)
/** @brief The most negative finite float (the negative of FLT_MAX), i.e. the smallest representable finite float, NOT the smallest positive value. */
#define MIN_FLOAT ((FloatBits) { .Int = 0xFF7FFFFF }.Float)
/** @brief The mathematical constant pi as a float. */
#define PI_FLOAT 3.14159265358979323846f
/** @brief The mathematical constant tau (2*pi) as a float. */
#define TAU_FLOAT (PI_FLOAT * 2.0f)
/** @brief Euler's number e as a float. */
#define E_FLOAT 2.71828182845904523536028f

/** @brief A quiet not-a-number (NaN) value of type double, built from a fixed IEEE-754 bit pattern. */
#define NAN_DOUBLE ((DoubleBits) { .Int = 0x7FF0000000000001 }.Double)
/** @brief Positive infinity of type double (IEEE-754 +inf). */
#define INFINITY_POS_DOUBLE ((DoubleBits) { .Int = 0x7FF0000000000000 }.Double)
/** @brief Negative infinity of type double (IEEE-754 -inf). */
#define INFINITY_NEG_DOUBLE ((DoubleBits) { .Int = 0xFFF0000000000000 }.Double)
/** @brief The largest finite positive double (equivalent to DBL_MAX). */
#define MAX_DOUBLE ((DoubleBits) { .Int = 0x7FEFFFFFFFFFFFFF }.Double)
/** @brief The most negative finite double (the negative of DBL_MAX), i.e. the smallest representable finite double, NOT the smallest positive value. */
#define MIN_DOUBLE ((DoubleBits) { .Int = 0xFFEFFFFFFFFFFFFF }.Double)
/** @brief The mathematical constant pi as a double. */
#define PI_DOUBLE 3.14159265358979323846
/** @brief The mathematical constant tau (2*pi) as a double. */
#define TAU_DOUBLE (PI_DOUBLE * 2.0)
/** @brief Euler's number e as a double. */
#define E_DOUBLE 2.71828182845904523536028

/** @brief Bit mask selecting the sign bit of a 32-bit float's raw integer representation. */
#define FLOAT_BIT_MASK_SIGN 0x80000000
/** @brief Bit mask selecting the 8 exponent bits of a 32-bit float's raw integer representation. */
#define FLOAT_BIT_MASK_EXPONENT 0x7F800000
/** @brief Bit mask selecting the 23 mantissa (fraction) bits of a 32-bit float's raw integer representation. */
#define FLOAT_BIT_MASK_MANTISSA 0x007FFFFF

/** @brief Bit mask selecting the sign bit of a 64-bit double's raw integer representation. */
#define DOUBLE_BIT_MASK_SIGN 0x8000000000000000
/** @brief Bit mask selecting the 11 exponent bits of a 64-bit double's raw integer representation. */
#define DOUBLE_BIT_MASK_EXPONENT 0x7FF0000000000000
/** @brief Bit mask selecting the 52 mantissa (fraction) bits of a 64-bit double's raw integer representation. */
#define DOUBLE_BIT_MASK_MANTISSA 0x000FFFFFFFFFFFFF


// Types.
/**
 * @brief Tie-breaking / direction policy that controls how a value exactly on (or near) a rounding
 *        midpoint is rounded.
 *
 * Used by RoundingOptions and the Math_Round* functions to decide, at the rounding boundary, whether
 * the result's magnitude is increased or kept.
 */
typedef enum RoundingTypeEnum
{
    RoundingType_ToZero,              /**< Truncate toward zero: the fractional remainder is always dropped. */
    RoundingType_ToEven,             /**< Round half to even ("banker's rounding"): exact midpoints go to the nearest even last digit. */
    RoundingType_AwayFromZero,       /**< Round half away from zero: midpoints always increase magnitude. */
    RoundingType_ToNegativeInfinity, /**< Round toward negative infinity (floor): midpoints go to the lower value. */
    RoundingType_ToPositiveInfinity  /**< Round toward positive infinity (ceil): midpoints go to the higher value. */
} RoundingType;

/**
 * @brief Configuration describing how Math_Round* should round: the tie-breaking policy and the number
 *        of fractional digits to keep.
 *
 * Construct with the RoundingOptions_Create* helpers rather than initializing fields directly.
 */
typedef struct RoundingOptionsStruct
{
    RoundingType _type;                  /**< The rounding/tie-breaking policy to apply. */
    uint32_t _digitCountAfterDecimal;    /**< Number of digits to retain after the decimal point (0 = round to a whole number). */
} RoundingOptions;


// Functions.
/**
 * @brief Creates default rounding options: round-half-to-even to a whole number.
 *
 * Equivalent to RoundingOptions_CreateFull(RoundingType_ToEven, 0).
 * @returns A RoundingOptions value with _type == RoundingType_ToEven and _digitCountAfterDecimal == 0.
 */
RoundingOptions RoundingOptions_CreateNormal(void);

/**
 * @brief Creates rounding options that keep a given number of fractional digits, using round-half-to-even.
 * @param digitCount Number of digits to retain after the decimal point.
 * @returns A RoundingOptions value with _type == RoundingType_ToEven and _digitCountAfterDecimal == digitCount.
 */
RoundingOptions RoundingOptions_CreateWithDigitCount(uint32_t digitCount);

/**
 * @brief Creates rounding options with a given tie-breaking policy, rounding to a whole number.
 * @param type The rounding/tie-breaking policy to use.
 * @returns A RoundingOptions value with the given _type and _digitCountAfterDecimal == 0.
 */
RoundingOptions RoundingOptions_CreateWithType(RoundingType type);

/**
 * @brief Creates rounding options specifying both the tie-breaking policy and the fractional digit count.
 * @param type The rounding/tie-breaking policy to use.
 * @param digitCount Number of digits to retain after the decimal point.
 * @returns A RoundingOptions value with the given _type and _digitCountAfterDecimal.
 */
RoundingOptions RoundingOptions_CreateFull(RoundingType type, uint32_t digitCount);


/* Float. */
/**
 * @brief Computes the floating-point remainder of x divided by y (single precision).
 *
 * Returns x - n*y where n is x/y truncated toward zero, so the result has the same sign as x and a
 * magnitude less than |y|. Wraps the C library fmodf.
 * @param x The dividend.
 * @param y The divisor.
 * @returns The remainder of x/y. NaN if y is zero or either argument is NaN/infinite (per fmodf).
 */
float Math_RemainderFloat(float x, float y);

/**
 * @brief Raises value to the given exponent (single precision).
 *
 * Wraps the C library powf; special cases (e.g. negative base with non-integer exponent yielding NaN,
 * 0 to a negative power yielding infinity) follow IEEE-754 / powf semantics.
 * @param value The base.
 * @param exponent The power to raise the base to.
 * @returns value raised to exponent.
 */
float Math_PowFloat(float value, float exponent);

/**
 * @brief Computes the base-10 logarithm of value (single precision).
 * @param value The input; the domain is value > 0.
 * @returns log10(value); -infinity at 0 and NaN for negative input (per log10f).
 */
float Math_Log10Float(float value);

/**
 * @brief Computes the base-2 logarithm of value (single precision).
 * @param value The input; the domain is value > 0.
 * @returns log2(value); -infinity at 0 and NaN for negative input (per log2f).
 */
float Math_Log2Float(float value);

/**
 * @brief Computes the natural (base-e) logarithm of value (single precision).
 * @param value The input; the domain is value > 0.
 * @returns ln(value); -infinity at 0 and NaN for negative input (per logf).
 */
float Math_LogNaturalFloat(float value);

/**
 * @brief Computes the logarithm of value in an arbitrary base (single precision).
 *
 * Implemented as log10(value) / log10(base); both arguments must be positive for a meaningful result.
 * @param value The input whose logarithm is taken; the domain is value > 0.
 * @param base The logarithm base; should be > 0 and != 1 (base == 1 yields a division by zero -> infinity/NaN).
 * @returns The base-`base` logarithm of value. Edge values follow from the underlying log10f division.
 */
float Math_LogFloat(float value, float base);

/**
 * @brief Computes the square root of value (single precision).
 * @param value The input; the domain is value >= 0.
 * @returns sqrt(value); -0 for -0, and NaN for negative input (per sqrtf).
 */
float Math_SqrtFloat(float value);

/**
 * @brief Computes the real cube root of value (single precision).
 *
 * Defined for all finite inputs, including negatives (the result keeps the sign of value).
 * @param value The input.
 * @returns The cube root of value.
 */
float Math_CbrtFloat(float value);

/**
 * @brief Computes the real `root`-th root of value (single precision).
 *
 * Implemented as powf(value, 1/root). Because it goes through powf, a negative value with a
 * non-integer effective exponent yields NaN, and root == 0 produces an infinite exponent.
 * @param value The radicand.
 * @param root The degree of the root; must be non-zero.
 * @returns value raised to the power 1/root.
 */
float Math_NthRootFloat(float value, float root);

/**
 * @brief Computes the sine of an angle (single precision).
 * @param value The angle in radians.
 * @returns sin(value), in the range [-1, 1]; NaN if value is infinite (per sinf).
 */
float Math_SinFloat(float value);

/**
 * @brief Computes the cosine of an angle (single precision).
 * @param value The angle in radians.
 * @returns cos(value), in the range [-1, 1]; NaN if value is infinite (per cosf).
 */
float Math_CosFloat(float value);

/**
 * @brief Computes the tangent of an angle (single precision).
 * @param value The angle in radians.
 * @returns tan(value); NaN if value is infinite (per tanf).
 */
float Math_TanFloat(float value);

/**
 * @brief Computes the arcsine (inverse sine) of value (single precision).
 * @param value The input; the domain is [-1, 1].
 * @returns The angle in radians in [-pi/2, pi/2]; NaN if |value| > 1 (per asinf).
 */
float Math_ASinFloat(float value);

/**
 * @brief Computes the arccosine (inverse cosine) of value (single precision).
 * @param value The input; the domain is [-1, 1].
 * @returns The angle in radians in [0, pi]; NaN if |value| > 1 (per acosf).
 */
float Math_ACosFloat(float value);

/**
 * @brief Computes the arctangent (inverse tangent) of value (single precision).
 * @param value The input.
 * @returns The angle in radians in [-pi/2, pi/2].
 */
float Math_ATanFloat(float value);

/**
 * @brief Computes the angle of the 2D vector (x, y) from the positive x-axis (single precision).
 *
 * Returns the principal value of the arctangent of y/x using the signs of both arguments to select the
 * correct quadrant. Note the parameter order is (x, y): internally this calls atan2f(y, x).
 * @param x The x-coordinate (horizontal component).
 * @param y The y-coordinate (vertical component).
 * @returns The angle in radians in [-pi, pi].
 */
float Math_ATan2Float(float x, float y);

/**
 * @brief Computes the hyperbolic sine of value (single precision).
 * @param value The input.
 * @returns sinh(value); may overflow to +/-infinity for large-magnitude input (per sinhf).
 */
float Math_SinHypFloat(float value);

/**
 * @brief Computes the hyperbolic cosine of value (single precision).
 * @param value The input.
 * @returns cosh(value), always >= 1; may overflow to +infinity for large-magnitude input (per coshf).
 */
float Math_CosHypFloat(float value);

/**
 * @brief Computes the hyperbolic tangent of value (single precision).
 * @param value The input.
 * @returns tanh(value), in the range (-1, 1).
 */
float Math_TanHypFloat(float value);

/**
 * @brief Computes the inverse hyperbolic sine of value (single precision).
 * @param value The input.
 * @returns asinh(value).
 */
float Math_ASinHypFloat(float value);

/**
 * @brief Computes the inverse hyperbolic cosine of value (single precision).
 * @param value The input; the domain is value >= 1.
 * @returns acosh(value), always >= 0; NaN if value < 1 (per acoshf).
 */
float Math_ACosHypFloat(float value);

/**
 * @brief Computes the inverse hyperbolic tangent of value (single precision).
 * @param value The input; the domain is (-1, 1).
 * @returns atanh(value); +/-infinity at value == +/-1 and NaN for |value| > 1 (per atanhf).
 */
float Math_ATanHypFloat(float value);

/**
 * @brief Rounds value up to the nearest integer (single precision).
 * @param value The input.
 * @returns The smallest integral float not less than value (ceiling). NaN/infinity are returned unchanged.
 */
float Math_CeilFloat(float value);

/**
 * @brief Rounds value down to the nearest integer (single precision).
 * @param value The input.
 * @returns The largest integral float not greater than value (floor). NaN/infinity are returned unchanged.
 */
float Math_FloorFloat(float value);

/**
 * @brief Rounds value according to the supplied rounding options (single precision).
 *
 * Rounds to options._digitCountAfterDecimal fractional digits using the tie-breaking policy in
 * options._type (see RoundingType). A value detected to lie on a rounding midpoint (within a small
 * type-scaled margin of error) is resolved per the policy; otherwise it is rounded to the nearer step.
 * @param value The value to round.
 * @param options The rounding policy and fractional-digit count to apply.
 * @returns The rounded value. NaN and infinite inputs are returned unchanged.
 */
float Math_RoundFloat(float value, RoundingOptions options);

/**
 * @brief Truncates value toward zero, discarding the fractional part (single precision).
 * @param value The input.
 * @returns The integral part of value with the same sign. NaN/infinity are returned unchanged.
 */
float Math_TruncateFloat(float value);

/**
 * @brief Splits value into its integral and fractional parts (single precision).
 *
 * Both parts carry the same sign as number. Wraps the C library modff.
 * @param number The value to split.
 * @param integralPart [out] Receives the integral part of number (truncated toward zero); must not be NULL.
 * @returns The signed fractional part of number, in the range (-1, 1).
 */
float Math_SplitNumberFloat(float number, float* integralPart);

/**
 * @brief Tests whether value is a not-a-number (NaN) (single precision).
 *
 * Inspects the raw bit pattern (all exponent bits set and a non-zero mantissa), so it correctly reports
 * both quiet and signaling NaNs without relying on floating-point comparison.
 * @param value The value to test.
 * @returns true if value is any NaN; false otherwise.
 */
static inline bool Math_IsNaNFloat(float value)
{
    FloatBits Bits = (FloatBits) { .Float = value };
    return ((Bits.Int & FLOAT_BIT_MASK_EXPONENT) == FLOAT_BIT_MASK_EXPONENT)
        && ((Bits.Int & FLOAT_BIT_MASK_MANTISSA) != 0);
}

/**
 * @brief Tests whether value is positive or negative infinity (single precision).
 * @param value The value to test.
 * @returns true if value is +infinity or -infinity; false for finite values and NaN.
 */
static inline bool Math_IsInfinityFloat(float value)
{
    FloatBits Bits = (FloatBits) { .Float = value };
    return ((Bits.Int & FLOAT_BIT_MASK_EXPONENT) == FLOAT_BIT_MASK_EXPONENT)
        && ((Bits.Int & FLOAT_BIT_MASK_MANTISSA) == 0);
}

/**
 * @brief Tests whether value is positive infinity specifically (single precision).
 * @param value The value to test.
 * @returns true only if value is +infinity; false otherwise (including -infinity and NaN).
 */
static inline bool Math_IsInfinityPosFloat(float value)
{
    FloatBits Bits = (FloatBits) { .Float = value };
    return Math_IsInfinityFloat(value) && ((Bits.Int & FLOAT_BIT_MASK_SIGN) == 0);
}

/**
 * @brief Tests whether value is negative infinity specifically (single precision).
 * @param value The value to test.
 * @returns true only if value is -infinity; false otherwise (including +infinity and NaN).
 */
static inline bool Math_IsInfinityNegFloat(float value)
{
    FloatBits Bits = (FloatBits) { .Float = value };
    return Math_IsInfinityFloat(value) && ((Bits.Int & FLOAT_BIT_MASK_SIGN) != 0);
}

/**
 * @brief Returns the smaller of two floats (single precision).
 *
 * If the arguments are equal, b is returned. NaN handling is not special-cased; comparison with NaN
 * is unordered, so a NaN argument may be returned depending on operand position.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is smaller.
 */
static inline float Math_MinFloat(float a, float b)
{
    return (a > b) ? b : a;
}

/**
 * @brief Returns the larger of two floats (single precision).
 *
 * If the arguments are equal, b is returned. NaN handling is not special-cased; comparison with NaN
 * is unordered, so a NaN argument may be returned depending on operand position.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is larger.
 */
static inline float Math_MaxFloat(float a, float b)
{
    return (a < b) ? b : a;
}

/**
 * @brief Clamps value into the inclusive range [min, max] (single precision).
 *
 * The caller is responsible for passing min <= max; if min > max the result is min.
 * @param value The value to clamp.
 * @param min The lower bound (inclusive).
 * @param max The upper bound (inclusive).
 * @returns value if it lies within [min, max]; otherwise the nearer bound.
 */
static inline float Math_ClampFloat(float value, float min, float max)
{
    return Math_MaxFloat(min, Math_MinFloat(value, max));
}

/**
 * @brief Returns the absolute value of value (single precision).
 * @param value The input.
 * @returns |value|. Negative zero becomes positive zero.
 */
static inline float Math_AbsFloat(float value)
{
    return (value < 0.0f) ? -value : value;
}

/**
 * @brief Returns the sign of value as an integer (single precision).
 * @param value The input.
 * @returns 1 if value > 0, -1 if value < 0, and 0 if value is zero (also 0 for NaN, since the comparisons fail).
 */
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

/**
 * @brief Linearly interpolates between min and max by amount (single precision).
 *
 * Computes min + (max - min) * amount. amount is not clamped: 0 yields min, 1 yields max, and values
 * outside [0, 1] extrapolate beyond the endpoints.
 * @param min The value returned at amount == 0.
 * @param max The value returned at amount == 1.
 * @param amount The interpolation factor (typically in [0, 1], but not restricted).
 * @returns The interpolated value.
 */
static inline float Math_LerpFloat(float min, float max, float amount)
{
    return min + ((max - min) * amount);
}

/**
 * @brief Converts an angle from degrees to radians (single precision).
 * @param deg The angle in degrees.
 * @returns The equivalent angle in radians (deg * pi / 180).
 */
static inline float Math_DegToRadFloat(float deg)
{
    return deg / 180.0f * PI_FLOAT;
}

/**
 * @brief Converts an angle from radians to degrees (single precision).
 * @param rad The angle in radians.
 * @returns The equivalent angle in degrees (rad * 180 / pi).
 */
static inline float Math_RadToDegFloat(float rad)
{
    return rad / PI_FLOAT * 180.0f;
}

/**
 * @brief Tests whether two floats are equal within a given tolerance (single precision).
 *
 * Uses an absolute-difference comparison: |a - b| <= marginOfError.
 * @param a The first value.
 * @param b The second value.
 * @param marginOfError The maximum allowed absolute difference (should be >= 0).
 * @returns true if a and b differ by at most marginOfError; false otherwise.
 */
static inline bool Math_EqualsCloseFloat(float a, float b, float marginOfError)
{
    return Math_AbsFloat(a - b) <= marginOfError;
}

/**
 * @brief Maps value from the range [min, max] to a normalized [0, 1] position (single precision).
 *
 * Computes (value - min) / (max - min). The result is not clamped, so inputs outside [min, max] map
 * outside [0, 1]. min == max yields a division by zero.
 * @param value The value to normalize.
 * @param min The value mapping to 0.
 * @param max The value mapping to 1; must differ from min.
 * @returns The normalized position of value within [min, max].
 */
static inline float Math_NormalizeFloat(float value, float min, float max)
{
    return (value - min) / (max - min);
}

/**
 * @brief Re-maps value from an input range to an output range (single precision).
 *
 * Linearly maps value from [inMin, inMax] onto [outMin, outMax]. The result is not clamped to the
 * output range, and inMin == inMax yields a division by zero.
 * @param value The value to map.
 * @param inMin The lower bound of the input range.
 * @param inMax The upper bound of the input range; must differ from inMin.
 * @param outMin The output value corresponding to inMin.
 * @param outMax The output value corresponding to inMax.
 * @returns value re-mapped into the output range.
 */
static inline float Math_MapFloat(float value, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + ((outMax - outMin) * Math_NormalizeFloat(value, inMin, inMax));
}


/* Double. */
/**
 * @brief Computes the floating-point remainder of x divided by y (double precision).
 *
 * Returns x - n*y where n is x/y truncated toward zero, so the result has the same sign as x and a
 * magnitude less than |y|. Wraps the C library fmod.
 * @param x The dividend.
 * @param y The divisor.
 * @returns The remainder of x/y. NaN if y is zero or either argument is NaN/infinite (per fmod).
 */
double Math_RemainderDouble(double x, double y);

/**
 * @brief Raises value to the given exponent (double precision).
 *
 * Wraps the C library pow; special cases (e.g. negative base with non-integer exponent yielding NaN,
 * 0 to a negative power yielding infinity) follow IEEE-754 / pow semantics.
 * @param value The base.
 * @param exponent The power to raise the base to.
 * @returns value raised to exponent.
 */
double Math_PowDouble(double value, double exponent);

/**
 * @brief Computes the base-10 logarithm of value (double precision).
 * @param value The input; the domain is value > 0.
 * @returns log10(value); -infinity at 0 and NaN for negative input (per log10).
 */
double Math_Log10Double(double value);

/**
 * @brief Computes the base-2 logarithm of value (double precision).
 * @param value The input; the domain is value > 0.
 * @returns log2(value); -infinity at 0 and NaN for negative input (per log2).
 */
double Math_Log2Double(double value);

/**
 * @brief Computes the natural (base-e) logarithm of value (double precision).
 * @param value The input; the domain is value > 0.
 * @returns ln(value); -infinity at 0 and NaN for negative input (per log).
 */
double Math_LogNaturalDouble(double value);

/**
 * @brief Computes the logarithm of value in an arbitrary base (double precision).
 *
 * Implemented as ln(value) / ln(base); both arguments must be positive for a meaningful result.
 * @param value The input whose logarithm is taken; the domain is value > 0.
 * @param base The logarithm base; should be > 0 and != 1 (base == 1 yields a division by zero -> infinity/NaN).
 * @returns The base-`base` logarithm of value. Edge values follow from the underlying log division.
 */
double Math_LogDouble(double value, double base);

/**
 * @brief Computes the square root of value (double precision).
 * @param value The input; the domain is value >= 0.
 * @returns sqrt(value); -0 for -0, and NaN for negative input (per sqrt).
 */
double Math_SqrtDouble(double value);

/**
 * @brief Computes the real cube root of value (double precision).
 *
 * Defined for all finite inputs, including negatives (the result keeps the sign of value).
 * @param value The input.
 * @returns The cube root of value.
 */
double Math_CbrtDouble(double value);

/**
 * @brief Computes the real `root`-th root of value (double precision).
 *
 * Implemented as pow(value, 1/root). Because it goes through pow, a negative value with a non-integer
 * effective exponent yields NaN, and root == 0 produces an infinite exponent.
 * @param value The radicand.
 * @param root The degree of the root; must be non-zero.
 * @returns value raised to the power 1/root.
 */
double Math_NthRootDouble(double value, double root);

/**
 * @brief Computes the sine of an angle (double precision).
 * @param value The angle in radians.
 * @returns sin(value), in the range [-1, 1]; NaN if value is infinite (per sin).
 */
double Math_SinDouble(double value);

/**
 * @brief Computes the cosine of an angle (double precision).
 * @param value The angle in radians.
 * @returns cos(value), in the range [-1, 1]; NaN if value is infinite (per cos).
 */
double Math_CosDouble(double value);

/**
 * @brief Computes the tangent of an angle (double precision).
 * @param value The angle in radians.
 * @returns tan(value); NaN if value is infinite (per tan).
 */
double Math_TanDouble(double value);

/**
 * @brief Computes the arcsine (inverse sine) of value (double precision).
 * @param value The input; the domain is [-1, 1].
 * @returns The angle in radians in [-pi/2, pi/2]; NaN if |value| > 1 (per asin).
 */
double Math_ASinDouble(double value);

/**
 * @brief Computes the arccosine (inverse cosine) of value (double precision).
 * @param value The input; the domain is [-1, 1].
 * @returns The angle in radians in [0, pi]; NaN if |value| > 1 (per acos).
 */
double Math_ACosDouble(double value);

/**
 * @brief Computes the arctangent (inverse tangent) of value (double precision).
 * @param value The input.
 * @returns The angle in radians in [-pi/2, pi/2].
 */
double Math_ATanDouble(double value);

/**
 * @brief Computes the angle of the 2D vector (x, y) from the positive x-axis (double precision).
 *
 * Returns the principal value of the arctangent of y/x using the signs of both arguments to select the
 * correct quadrant. Note the parameter order is (x, y): internally this calls atan2(y, x).
 * @param x The x-coordinate (horizontal component).
 * @param y The y-coordinate (vertical component).
 * @returns The angle in radians in [-pi, pi].
 */
double Math_ATan2Double(double x, double y);

/**
 * @brief Computes the hyperbolic sine of value (double precision).
 * @param value The input.
 * @returns sinh(value); may overflow to +/-infinity for large-magnitude input (per sinh).
 */
double Math_SinHypDouble(double value);

/**
 * @brief Computes the hyperbolic cosine of value (double precision).
 * @param value The input.
 * @returns cosh(value), always >= 1; may overflow to +infinity for large-magnitude input (per cosh).
 */
double Math_CosHypDouble(double value);

/**
 * @brief Computes the hyperbolic tangent of value (double precision).
 * @param value The input.
 * @returns tanh(value), in the range (-1, 1).
 */
double Math_TanHypDouble(double value);

/**
 * @brief Computes the inverse hyperbolic sine of value (double precision).
 * @param value The input.
 * @returns asinh(value).
 */
double Math_ASinHypDouble(double value);

/**
 * @brief Computes the inverse hyperbolic cosine of value (double precision).
 * @param value The input; the domain is value >= 1.
 * @returns acosh(value), always >= 0; NaN if value < 1 (per acosh).
 */
double Math_ACosHypDouble(double value);

/**
 * @brief Computes the inverse hyperbolic tangent of value (double precision).
 * @param value The input; the domain is (-1, 1).
 * @returns atanh(value); +/-infinity at value == +/-1 and NaN for |value| > 1 (per atanh).
 */
double Math_ATanHypDouble(double value);

/**
 * @brief Rounds value up to the nearest integer (double precision).
 * @param value The input.
 * @returns The smallest integral double not less than value (ceiling). NaN/infinity are returned unchanged.
 */
double Math_CeilDouble(double value);

/**
 * @brief Rounds value down to the nearest integer (double precision).
 * @param value The input.
 * @returns The largest integral double not greater than value (floor). NaN/infinity are returned unchanged.
 */
double Math_FloorDouble(double value);

/**
 * @brief Rounds value according to the supplied rounding options (double precision).
 *
 * Rounds to options._digitCountAfterDecimal fractional digits using the tie-breaking policy in
 * options._type (see RoundingType). A value detected to lie on a rounding midpoint (within a small
 * type-scaled margin of error) is resolved per the policy; otherwise it is rounded to the nearer step.
 * @param value The value to round.
 * @param options The rounding policy and fractional-digit count to apply.
 * @returns The rounded value. NaN and infinite inputs are returned unchanged.
 */
double Math_RoundDouble(double value, RoundingOptions options);

/**
 * @brief Truncates value toward zero, discarding the fractional part (double precision).
 * @param value The input.
 * @returns The integral part of value with the same sign. NaN/infinity are returned unchanged.
 */
double Math_TruncateDouble(double value);

/**
 * @brief Splits value into its integral and fractional parts (double precision).
 *
 * Both parts carry the same sign as number. Wraps the C library modf.
 * @param number The value to split.
 * @param integralPart [out] Receives the integral part of number (truncated toward zero); must not be NULL.
 * @returns The signed fractional part of number, in the range (-1, 1).
 */
double Math_SplitNumberDouble(double number, double* integralPart);

/**
 * @brief Tests whether value is a not-a-number (NaN) (double precision).
 *
 * Inspects the raw bit pattern (all exponent bits set and a non-zero mantissa), so it correctly reports
 * both quiet and signaling NaNs without relying on floating-point comparison.
 * @param value The value to test.
 * @returns true if value is any NaN; false otherwise.
 */
static inline bool Math_IsNaNDouble(double value)
{
    DoubleBits Bits = (DoubleBits) { .Double = value };
    return ((Bits.Int & DOUBLE_BIT_MASK_EXPONENT) == DOUBLE_BIT_MASK_EXPONENT)
        && ((Bits.Int & DOUBLE_BIT_MASK_MANTISSA) != 0);
}

/**
 * @brief Tests whether value is positive or negative infinity (double precision).
 * @param value The value to test.
 * @returns true if value is +infinity or -infinity; false for finite values and NaN.
 */
static inline bool Math_IsInfinityDouble(double value)
{
    DoubleBits Bits = (DoubleBits) { .Double = value };
    return ((Bits.Int & DOUBLE_BIT_MASK_EXPONENT) == DOUBLE_BIT_MASK_EXPONENT)
        && ((Bits.Int & DOUBLE_BIT_MASK_MANTISSA) == 0);
}

/**
 * @brief Tests whether value is positive infinity specifically (double precision).
 * @param value The value to test.
 * @returns true only if value is +infinity; false otherwise (including -infinity and NaN).
 */
static inline bool Math_IsInfinityPosDouble(double value)
{
    DoubleBits Bits = (DoubleBits) { .Double = value };
    return Math_IsInfinityDouble(value) && ((Bits.Int & DOUBLE_BIT_MASK_SIGN) == 0);
}

/**
 * @brief Tests whether value is negative infinity specifically (double precision).
 * @param value The value to test.
 * @returns true only if value is -infinity; false otherwise (including +infinity and NaN).
 */
static inline bool Math_IsInfinityNegDouble(double value)
{
    DoubleBits Bits = (DoubleBits) { .Double = value };
    return Math_IsInfinityDouble(value) && ((Bits.Int & DOUBLE_BIT_MASK_SIGN) != 0);
}

/**
 * @brief Returns the smaller of two doubles (double precision).
 *
 * If the arguments are equal, b is returned. NaN handling is not special-cased; comparison with NaN
 * is unordered, so a NaN argument may be returned depending on operand position.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is smaller.
 */
static inline double Math_MinDouble(double a, double b)
{
    return (a > b) ? b : a;
}

/**
 * @brief Returns the larger of two doubles (double precision).
 *
 * If the arguments are equal, b is returned. NaN handling is not special-cased; comparison with NaN
 * is unordered, so a NaN argument may be returned depending on operand position.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is larger.
 */
static inline double Math_MaxDouble(double a, double b)
{
    return (a < b) ? b : a;
}

/**
 * @brief Clamps value into the inclusive range [min, max] (double precision).
 *
 * The caller is responsible for passing min <= max; if min > max the result is min.
 * @param value The value to clamp.
 * @param min The lower bound (inclusive).
 * @param max The upper bound (inclusive).
 * @returns value if it lies within [min, max]; otherwise the nearer bound.
 */
static inline double Math_ClampDouble(double value, double min, double max)
{
    return Math_MaxDouble(min, Math_MinDouble(value, max));
}

/**
 * @brief Returns the absolute value of value (double precision).
 * @param value The input.
 * @returns |value|. Negative zero becomes positive zero.
 */
static inline double Math_AbsDouble(double value)
{
    return (value < 0.0) ? -value : value;
}

/**
 * @brief Returns the sign of value as an integer (double precision).
 * @param value The input.
 * @returns 1 if value > 0, -1 if value < 0, and 0 if value is zero (also 0 for NaN, since the comparisons fail).
 */
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

/**
 * @brief Linearly interpolates between min and max by amount (double precision).
 *
 * Computes min + (max - min) * amount. amount is not clamped: 0 yields min, 1 yields max, and values
 * outside [0, 1] extrapolate beyond the endpoints.
 * @param min The value returned at amount == 0.
 * @param max The value returned at amount == 1.
 * @param amount The interpolation factor (typically in [0, 1], but not restricted).
 * @returns The interpolated value.
 */
static inline double Math_LerpDouble(double min, double max, double amount)
{
    return min + ((max - min) * amount);
}

/**
 * @brief Converts an angle from degrees to radians (double precision).
 * @param deg The angle in degrees.
 * @returns The equivalent angle in radians (deg * pi / 180).
 */
static inline double Math_DegToRadDouble(double deg)
{
    return deg / 180.0 * PI_DOUBLE;
}

/**
 * @brief Converts an angle from radians to degrees (double precision).
 * @param rad The angle in radians.
 * @returns The equivalent angle in degrees (rad * 180 / pi).
 */
static inline double Math_RadToDegDouble(double rad)
{
    return rad / PI_DOUBLE * 180.0;
}

/**
 * @brief Tests whether two doubles are equal within a given tolerance (double precision).
 *
 * Uses an absolute-difference comparison: |a - b| <= marginOfError.
 * @param a The first value.
 * @param b The second value.
 * @param marginOfError The maximum allowed absolute difference (should be >= 0).
 * @returns true if a and b differ by at most marginOfError; false otherwise.
 */
static inline bool Math_EqualsCloseDouble(double a, double b, double marginOfError)
{
    return Math_AbsDouble(a - b) <= marginOfError;
}

/**
 * @brief Maps value from the range [min, max] to a normalized [0, 1] position (double precision).
 *
 * Computes (value - min) / (max - min). The result is not clamped, so inputs outside [min, max] map
 * outside [0, 1]. min == max yields a division by zero.
 * @param value The value to normalize.
 * @param min The value mapping to 0.
 * @param max The value mapping to 1; must differ from min.
 * @returns The normalized position of value within [min, max].
 */
static inline double Math_NormalizeDouble(double value, double min, double max)
{
    return (value - min) / (max - min);
}

/**
 * @brief Re-maps value from an input range to an output range (double precision).
 *
 * Linearly maps value from [inMin, inMax] onto [outMin, outMax]. The result is not clamped to the
 * output range, and inMin == inMax yields a division by zero.
 * @param value The value to map.
 * @param inMin The lower bound of the input range.
 * @param inMax The upper bound of the input range; must differ from inMin.
 * @param outMin The output value corresponding to inMin.
 * @param outMax The output value corresponding to inMax.
 * @returns value re-mapped into the output range.
 */
static inline double Math_MapDouble(double value, double inMin, double inMax, double outMin, double outMax)
{
    return outMin + ((outMax - outMin) * Math_NormalizeDouble(value, inMin, inMax));
}


/* Int32. */
/**
 * @brief Returns the smaller of two 32-bit signed integers.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is smaller (b if they are equal).
 */
static inline int32_t Math_MinInt32(int32_t a, int32_t b)
{
    return (a > b) ? b : a;
}

/**
 * @brief Returns the larger of two 32-bit signed integers.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is larger (b if they are equal).
 */
static inline int32_t Math_MaxInt32(int32_t a, int32_t b)
{
    return (a < b) ? b : a;
}

/**
 * @brief Clamps a 32-bit signed integer into the inclusive range [min, max].
 *
 * The caller is responsible for passing min <= max; if min > max the result is min.
 * @param value The value to clamp.
 * @param min The lower bound (inclusive).
 * @param max The upper bound (inclusive).
 * @returns value if it lies within [min, max]; otherwise the nearer bound.
 */
static inline int32_t Math_ClampInt32(int32_t value, int32_t min, int32_t max)
{
    return Math_MaxInt32(min, Math_MinInt32(value, max));
}

/**
 * @brief Returns the absolute value of a 32-bit signed integer.
 * @param value The input.
 * @returns |value|.
 * @note Passing INT32_MIN overflows: its negation is not representable, so the result is undefined for that input.
 */
static inline int32_t Math_AbsInt32(int32_t value)
{
    return (value < 0) ? -value : value;
}

/**
 * @brief Returns the sign of a 32-bit signed integer.
 * @param value The input.
 * @returns 1 if value > 0, -1 if value < 0, and 0 if value == 0.
 */
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
/**
 * @brief Returns the smaller of two 64-bit signed integers.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is smaller (b if they are equal).
 */
static inline int64_t Math_MinInt64(int64_t a, int64_t b)
{
    return (a > b) ? b : a;
}

/**
 * @brief Returns the larger of two 64-bit signed integers.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is larger (b if they are equal).
 */
static inline int64_t Math_MaxInt64(int64_t a, int64_t b)
{
    return (a < b) ? b : a;
}

/**
 * @brief Clamps a 64-bit signed integer into the inclusive range [min, max].
 *
 * The caller is responsible for passing min <= max; if min > max the result is min.
 * @param value The value to clamp.
 * @param min The lower bound (inclusive).
 * @param max The upper bound (inclusive).
 * @returns value if it lies within [min, max]; otherwise the nearer bound.
 */
static inline int64_t Math_ClampInt64(int64_t value, int64_t min, int64_t max)
{
    return Math_MaxInt64(min, Math_MinInt64(value, max));
}

/**
 * @brief Returns the absolute value of a 64-bit signed integer.
 * @param value The input.
 * @returns |value|.
 * @note Passing INT64_MIN overflows: its negation is not representable, so the result is undefined for that input.
 */
static inline int64_t Math_AbsInt64(int64_t value)
{
    return (value < 0) ? -value : value;
}

/**
 * @brief Returns the sign of a 64-bit signed integer.
 * @param value The input.
 * @returns 1 if value > 0, -1 if value < 0, and 0 if value == 0 (returned as a 32-bit int).
 */
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
/**
 * @brief Returns the smaller of two size_t (unsigned) values.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is smaller (b if they are equal).
 */
static inline size_t Math_MinSizeT(size_t a, size_t b)
{
    return (a > b) ? b : a;
}

/**
 * @brief Returns the larger of two size_t (unsigned) values.
 * @param a The first value.
 * @param b The second value.
 * @returns Whichever of a or b is larger (b if they are equal).
 */
static inline size_t Math_MaxSizeT(size_t a, size_t b)
{
    return (a < b) ? b : a;
}

/**
 * @brief Clamps a size_t (unsigned) value into the inclusive range [min, max].
 *
 * The caller is responsible for passing min <= max; if min > max the result is min.
 * @param value The value to clamp.
 * @param min The lower bound (inclusive).
 * @param max The upper bound (inclusive).
 * @returns value if it lies within [min, max]; otherwise the nearer bound.
 */
static inline size_t Math_ClampSizeT(size_t value, size_t min, size_t max)
{
    return Math_MaxSizeT(min, Math_MinSizeT(value, max));
}
