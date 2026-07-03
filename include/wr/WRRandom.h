#pragma once
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Number of 64-bit words that make up the generator's internal state.
 */
#define RANDOM_STATE_LENGTH 4


// Types.
/**
 * @brief A pseudo-random number generator instance (xoshiro256** algorithm).
 *
 * Holds the generator's mutable internal state. Construct it with Random_Construct1
 * (entropy-seeded) or Random_Construct2 (from an explicit RandomState), draw values with the
 * Random_Next* functions, then release it with Random_Deconstruct1. Every draw advances and
 * mutates the state, so the same instance produces a different value on each call.
 *
 * This type is NOT thread-safe: a single Random must not be used concurrently from multiple
 * threads without external synchronization. The generator is fast but non-cryptographic and
 * must not be used to produce secrets, keys, or security tokens.
 */
typedef struct RandomStruct
{
    uint64_t _values[RANDOM_STATE_LENGTH];
} Random;

/**
 * @brief A snapshot of a generator's internal state, suitable for saving and restoring.
 *
 * Obtained from Random_GetState and accepted by Random_SetState / Random_Construct2 to
 * reproduce or resume a generation sequence. A state whose words are all zero is invalid for
 * xoshiro256**; the API replaces such a state with a deterministically derived non-zero state
 * rather than using it.
 */
typedef struct RandomStateStruct
{
    uint64_t Values[RANDOM_STATE_LENGTH];
} RandomState;


// Functions.
/**
 * @brief Initializes a generator with a state seeded from host entropy.
 *
 * Seeds the generator from a non-deterministic source derived from the current time and the
 * instance's address, then advances it a few steps so early output is well mixed. Because the
 * seed is entropy-based, consecutive constructions generally produce different sequences.
 * The object must later be released with Random_Deconstruct1.
 * @param self The generator to initialize. Must not be NULL.
 */
void Random_Construct1(Random* self);

/**
 * @brief Initializes a generator from an explicit state snapshot.
 *
 * Sets the generator's state from the supplied RandomState so it reproduces the sequence that
 * follows that state. If the provided state is all-zero (invalid for the algorithm), a
 * deterministically derived non-zero state is substituted instead. The object must later be
 * released with Random_Deconstruct1.
 * @param self The generator to initialize. Must not be NULL.
 * @param state The state to load (passed by value).
 */
void Random_Construct2(Random* self, RandomState state);

/**
 * @brief Releases a generator and clears its internal state.
 *
 * Zeroes the instance's state. The Random owns no external resources, so this performs no
 * deallocation; it simply leaves the object in a defined cleared state.
 * @param self The generator to release. Must not be NULL.
 */
void Random_Deconstruct1(Random* self);

/**
 * @brief Returns the next pseudo-random 32-bit signed integer.
 *
 * Draws a full-width value spanning the entire int32_t range [INT32_MIN, INT32_MAX]
 * (every value, including negatives, is possible). Advances the generator state.
 * @param self The generator to draw from. Must not be NULL.
 * @returns A pseudo-random int32_t across the full type range.
 */
int32_t Random_NextInt32(Random* self);

/**
 * @brief Returns a pseudo-random 32-bit integer in [0, limit).
 *
 * Produces a value uniformly distributed in the half-open range from 0 (inclusive) to limit
 * (exclusive), using rejection to avoid modulo bias. If limit is 0 or negative the range is
 * empty and 0 is returned. Advances the generator state.
 * @param self The generator to draw from. Must not be NULL.
 * @param limit Exclusive upper bound. Values <= 0 yield 0.
 * @returns A uniformly distributed value in [0, limit), or 0 when limit <= 0.
 */
int32_t Random_NextInt32InLimit(Random* self, int32_t limit);

/**
 * @brief Returns a pseudo-random 32-bit integer in [min, max).
 *
 * Produces a value uniformly distributed in the half-open range from min (inclusive) to max
 * (exclusive). If max <= min the range is empty and min is returned. Advances the generator
 * state. The caller must ensure the span (max - min) is representable as a positive 32-bit value.
 * @param self The generator to draw from. Must not be NULL.
 * @param min Inclusive lower bound.
 * @param max Exclusive upper bound; if max <= min, min is returned.
 * @returns A uniformly distributed value in [min, max), or min when max <= min.
 */
int32_t Random_NextInt32InRange(Random* self, int32_t min, int32_t max);

/**
 * @brief Returns the next pseudo-random 64-bit signed integer.
 *
 * Draws a full-width value spanning the entire int64_t range [INT64_MIN, INT64_MAX]
 * (every value, including negatives, is possible). Advances the generator state.
 * @param self The generator to draw from. Must not be NULL.
 * @returns A pseudo-random int64_t across the full type range.
 */
int64_t Random_NextInt64(Random* self);

/**
 * @brief Returns a pseudo-random 64-bit integer in [0, limit).
 *
 * Produces a value uniformly distributed in the half-open range from 0 (inclusive) to limit
 * (exclusive), using rejection to avoid modulo bias. If limit is 0 or negative the range is
 * empty and 0 is returned. Advances the generator state.
 * @param self The generator to draw from. Must not be NULL.
 * @param limit Exclusive upper bound. Values <= 0 yield 0.
 * @returns A uniformly distributed value in [0, limit), or 0 when limit <= 0.
 */
int64_t Random_NextInt64InLimit(Random* self, int64_t limit);

/**
 * @brief Returns a pseudo-random 64-bit integer in [min, max).
 *
 * Produces a value uniformly distributed in the half-open range from min (inclusive) to max
 * (exclusive). If max <= min the range is empty and min is returned. Advances the generator
 * state. The caller must ensure the span (max - min) is representable as a positive 64-bit value.
 * @param self The generator to draw from. Must not be NULL.
 * @param min Inclusive lower bound.
 * @param max Exclusive upper bound; if max <= min, min is returned.
 * @returns A uniformly distributed value in [min, max), or min when max <= min.
 */
int64_t Random_NextInt64InRange(Random* self, int64_t min, int64_t max);

/**
 * @brief Returns a pseudo-random single-precision float in [0.0f, 1.0f).
 *
 * Produces a value in the half-open unit interval (0.0 inclusive, 1.0 exclusive) built from the
 * generator's high mantissa bits. Advances the generator state.
 * @param self The generator to draw from. Must not be NULL.
 * @returns A float in [0.0f, 1.0f).
 */
float Random_NextSingle(Random* self);

/**
 * @brief Returns a pseudo-random double-precision float in [0.0, 1.0).
 *
 * Produces a value in the half-open unit interval (0.0 inclusive, 1.0 exclusive) built from the
 * generator's high mantissa bits. Advances the generator state.
 * @param self The generator to draw from. Must not be NULL.
 * @returns A double in [0.0, 1.0).
 */
double Random_NextDouble(Random* self);

/**
 * @brief Returns a pseudo-random boolean.
 *
 * Yields true or false with (approximately) equal probability. Advances the generator state.
 * @param self The generator to draw from. Must not be NULL.
 * @returns A pseudo-random true or false.
 */
bool Random_NextBool(Random* self);

/**
 * @brief Replaces the generator's internal state with a saved snapshot.
 *
 * Loads the supplied state so subsequent draws continue the sequence that follows it. If the
 * given state is all-zero (invalid for the algorithm), a deterministically derived non-zero
 * state is substituted instead.
 * @param self The generator to modify. Must not be NULL.
 * @param state The state to load (passed by value).
 */
void Random_SetState(Random* self, RandomState state);

/**
 * @brief Captures the generator's current internal state.
 *
 * Returns a snapshot that can be stored and later restored with Random_SetState or
 * Random_Construct2 to reproduce or resume the sequence. Does not modify the generator.
 * @param self The generator to read. Must not be NULL.
 * @returns A copy of the current generator state.
 */
RandomState Random_GetState(Random* self);

/**
 * @brief Returns the next raw 64-bit output word from the generator.
 *
 * Produces one unbiased full-width 64-bit value spanning the entire uint64_t range and advances
 * the generator state. This is the underlying primitive the other Random_Next* helpers build on.
 * @param self The generator to draw from. Must not be NULL.
 * @returns A pseudo-random uint64_t across the full type range.
 */
uint64_t Random_NextValue(Random* self);
