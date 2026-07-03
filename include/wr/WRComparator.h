#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Compares two numeric values and yields the corresponding ComparisonResult.
 *
 * Function-like macro intended for use with any comparable scalar type (the
 * `>` and `==` operators are applied directly to the arguments).
 * @param a The left-hand operand.
 * @param b The right-hand operand.
 * @returns ComparisonResult_AGreaterThanB if `a > b`, ComparisonResult_AEqualsB
 *          if `a == b`, otherwise ComparisonResult_ALessThanB.
 * @note Each argument is evaluated more than once. Callers must not pass
 *       expressions with side effects (e.g. `i++`) as `a` or `b`.
 */
#define COMPARE_NUMBER(a, b) (a > b) ? ComparisonResult_AGreaterThanB : ((a == b) ? ComparisonResult_AEqualsB : ComparisonResult_ALessThanB)



// Types.
/**
 * @brief Result of a three-way comparison between two values `a` and `b`.
 *
 * Every comparator in this header returns one of these constants to describe
 * the ordering of its first argument relative to its second.
 */
typedef enum ComparisonResultEnum
{
    /** @brief `a` is ordered before `b` (a < b). Has the value -1. */
    ComparisonResult_ALessThanB = -1,
    /** @brief `a` and `b` are equal (a == b). Has the value 0. */
    ComparisonResult_AEqualsB = 0,
    /** @brief `a` is ordered after `b` (a > b). Has the value 1. */
    ComparisonResult_AGreaterThanB = 1
} ComparisonResult;


// Functions.
/**
 * @brief Compares two 8-bit signed integers.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareInt8(int8_t a, int8_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two 8-bit unsigned integers.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareUInt8(uint8_t a, uint8_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two 16-bit signed integers.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareInt16(int16_t a, int16_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two 16-bit unsigned integers.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareUInt16(uint16_t a, uint16_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two 32-bit signed integers.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareInt32(int32_t a, int32_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two 32-bit unsigned integers.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareUInt32(uint32_t a, uint32_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two 64-bit signed integers.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareInt64(int64_t a, int64_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two 64-bit unsigned integers.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareUInt64(uint64_t a, uint64_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two size_t values.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareSizeT(size_t a, size_t b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two single-precision floating-point values.
 *
 * Comparison uses the built-in floating-point relational operators; if either
 * operand is NaN the values compare as unordered and the result is
 * ComparisonResult_ALessThanB.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareFloat(float a, float b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two double-precision floating-point values.
 *
 * Comparison uses the built-in floating-point relational operators; if either
 * operand is NaN the values compare as unordered and the result is
 * ComparisonResult_ALessThanB.
 * @param a The left-hand value.
 * @param b The right-hand value.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
static inline ComparisonResult Comparator_CompareDouble(double a, double b)
{
    return COMPARE_NUMBER(a, b);
}

/**
 * @brief Compares two UTF-8 encoded strings in lexicographic order.
 *
 * Both arguments are treated as NUL-terminated UTF-8 strings and are ordered
 * by comparing their bytes (equivalently, their Unicode code points for valid
 * UTF-8) from the start; the first differing position determines the result.
 * If one string is a prefix of the other, the shorter string compares as less.
 * @param a The left-hand string.
 * @param b The right-hand string.
 * @returns Whether `a` is less than, equal to, or greater than `b`.
 */
ComparisonResult Comparator_CompareString(const unsigned char* a, const unsigned char* b);