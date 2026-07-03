#pragma once
#include "WRError.h"
#include "WRList.h"
#include "WRMemory.h"
#include "WRRandom.h"


/**
 * Shuffle module provides uniform random shuffling (Fisher-Yates) of element containers using a
 * caller-supplied random number generator. Every permutation of the elements is equally likely,
 * assuming a uniform generator. Shuffling an empty or single-element container is a successful
 * no-op. The generator's state is advanced once per performed swap.
 */


// Functions.
/**
 * @brief Uniformly shuffles the elements of a list in place.
 *
 * Reorders the list's elements into a uniformly random permutation using the supplied generator.
 * Elements are moved with by-value copies through the list interface, using an internal swap
 * scratch (stack storage for small elements, a single temporary allocation for large ones).
 * @param list The list to shuffle. Must not be NULL.
 * @param rng The random number generator driving the shuffle. Must not be NULL.
 * @returns Success when the list was shuffled (or had fewer than two elements). Raises
 *          ErrorCode_IllegalArgument if @p list or @p rng is NULL; ErrorCode_InvalidOperation if
 *          the list is read-only; ErrorCode_ArgumentOutOfRange if the element count exceeds the
 *          supported maximum.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Shuffle_List(IList* list, Random* rng);

/**
 * @brief Uniformly shuffles the elements of a generic buffer in place.
 *
 * Reorders the buffer's elements into a uniformly random permutation using the supplied generator.
 * The buffer's count and capacity are unchanged. Uses an internal swap scratch (stack storage for
 * small elements, a single temporary allocation for large ones).
 * @param buffer The buffer to shuffle. Must not be NULL.
 * @param rng The random number generator driving the shuffle. Must not be NULL.
 * @returns Success when the buffer was shuffled (or had fewer than two elements). Raises
 *          ErrorCode_IllegalArgument if @p buffer or @p rng is NULL; ErrorCode_InvalidOperation if
 *          the buffer cannot be mutated (it is read-only); ErrorCode_ArgumentOutOfRange if the
 *          element count exceeds the supported maximum.
 */
Error Shuffle_Buffer(GenericBuffer* buffer, Random* rng);
