#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WRArrayList.h"
#include "WRCollection.h"
#include "WRError.h"
#include "WRMemory.h"
#include "WRRandom.h"


/**
 * RandomShuffledCollection module provides a "shuffle bag": a collection of fixed-size items that
 * is played back in shuffled sequences through RandomShuffledCollection_GetNext. The whole item
 * set is shuffled, then GetNext walks that sequence one item per call; when the sequence is
 * exhausted the collection reshuffles and playback continues, so every item is returned exactly
 * once per sequence (unlike independent random picks). An item may still appear twice in a row
 * across a sequence boundary.
 *
 * The collection owns an embedded Random which drives all shuffling. Shuffling permutes an
 * internal index array, never the items themselves, so items stay in insertion order: the
 * ICollection view returned by RandomShuffledCollection_AsCollection enumerates them
 * deterministically and advances no generator state.
 *
 * Any mutation (add / remove / set / clear) abandons the in-progress sequence; the next GetNext
 * call starts a freshly shuffled sequence over the updated items.
 */


// Types.
/**
 * @brief A collection of fixed-size items played back in endless shuffled sequences.
 *
 * Construct with RandomShuffledCollection_Construct1 / Construct2, fill it with items, draw items
 * with RandomShuffledCollection_GetNext, and release it with RandomShuffledCollection_Deconstruct.
 * Items are stored by value (elementSize bytes each) in insertion order. Not thread-safe.
 */
typedef struct RandomShuffledCollectionStruct
{
    /** @brief Item storage, in insertion order. Its embedded list also provides the ICollection view. */
    ArrayList _items;
    /** @brief The shuffled playback order: a permutation of the item indices (size_t per item). */
    GenericBuffer _playbackOrder;
    /** @brief Position within _playbackOrder of the next item GetNext returns. */
    size_t _cursor;
    /** @brief The owned random number generator that drives every shuffle. */
    Random _random;
    /** @brief Whether _playbackOrder matches the current items. Cleared by every mutation and by Reshuffle. */
    bool _isSequenceValid;
} RandomShuffledCollection;


// Functions.
/**
 * @brief Constructs an empty shuffled collection with an entropy-seeded generator.
 *
 * The collection starts with zero items and zero capacity; no storage is allocated until items are
 * added. The embedded generator is seeded from host entropy (see Random_Construct1), so playback
 * sequences differ between runs. Must later be released with RandomShuffledCollection_Deconstruct.
 * @param self The collection to initialize. If NULL, the call is a no-op.
 * @param elementSize Size in bytes of each item; must be > 0.
 */
void RandomShuffledCollection_Construct1(RandomShuffledCollection* self, size_t elementSize);

/**
 * @brief Constructs an empty shuffled collection using the supplied generator.
 *
 * Behaves like RandomShuffledCollection_Construct1 but copies @p rng (by value) as the embedded
 * generator, allowing deterministic playback sequences. The caller's Random instance is not
 * referenced afterwards. Must later be released with RandomShuffledCollection_Deconstruct.
 * @param self The collection to initialize. If NULL, the call is a no-op.
 * @param elementSize Size in bytes of each item; must be > 0.
 * @param rng The generator whose state the collection copies and continues from.
 */
void RandomShuffledCollection_Construct2(RandomShuffledCollection* self, size_t elementSize, Random rng);

/**
 * @brief Releases the collection's storage and resets it to a valid empty state.
 *
 * Frees the item and playback order storage and clears the embedded generator. After the call the
 * collection is a valid empty collection (with a cleared generator state), so it may be reused
 * after reseeding via RandomShuffledCollection_SetRandom, or discarded. Safe to call more than once.
 * @param self The collection to release. If NULL, the call is a no-op.
 */
void RandomShuffledCollection_Deconstruct(RandomShuffledCollection* self);

/**
 * @brief Replaces the embedded generator with a copy of the supplied one.
 *
 * The new generator drives all future shuffles. An in-progress playback sequence is unaffected
 * (its order was already drawn); call RandomShuffledCollection_Reshuffle afterwards to make the
 * new generator take effect on the very next GetNext.
 * @param self The collection. If NULL, the call is a no-op.
 * @param rng The generator whose state the collection copies and continues from.
 */
void RandomShuffledCollection_SetRandom(RandomShuffledCollection* self, Random rng);

/**
 * @brief Ensures the collection's storage can hold at least totalCapacity items without reallocating.
 *
 * Grows the item and playback order storage if needed; the item count is unchanged.
 * @param self The collection. Must not be NULL.
 * @param totalCapacity The minimum total item capacity required.
 * @returns Success when the capacity is ensured. Raises ErrorCode_IllegalArgument if @p self is
 *          NULL; ErrorCode_InvalidOperation if the capacity could not be ensured.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomShuffledCollection_EnsureTotalCapacity(RandomShuffledCollection* self, size_t totalCapacity);

/**
 * @brief Appends one item to the collection.
 *
 * Copies elementSize bytes from @p item to the end of the collection. Abandons the in-progress
 * playback sequence.
 * @param self The collection. Must not be NULL.
 * @param item Pointer to the item to copy in. Must not be NULL.
 * @returns Success when the item was added. Raises ErrorCode_IllegalArgument if @p self or @p item
 *          is NULL; ErrorCode_InvalidOperation if storage could not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomShuffledCollection_Add(RandomShuffledCollection* self, void* item);

/**
 * @brief Appends a run of items to the collection.
 *
 * Copies @p count items from @p items to the end of the collection. Abandons the in-progress
 * playback sequence.
 * @param self The collection. Must not be NULL.
 * @param items Pointer to the first of @p count contiguous items. May be NULL only when @p count is 0.
 * @param count Number of items to add; 0 is a valid no-op.
 * @returns Success when all items were added. Raises ErrorCode_IllegalArgument if @p self is NULL,
 *          or if @p items is NULL while @p count > 0; ErrorCode_InvalidOperation if storage could
 *          not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomShuffledCollection_AddRange(RandomShuffledCollection* self, void* items, size_t count);

/**
 * @brief Removes the item at the given index (in insertion order), shifting later items down by one.
 *
 * Abandons the in-progress playback sequence.
 * @param self The collection. Must not be NULL.
 * @param index Position of the item to remove. Valid range is 0..count-1.
 * @returns Success on removal. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_IndexOutOfBounds if @p index is out of range.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomShuffledCollection_RemoveAt(RandomShuffledCollection* self, size_t index);

/**
 * @brief Removes all items, leaving an empty collection.
 *
 * Capacity is retained for reuse. Abandons the in-progress playback sequence.
 * @param self The collection. Must not be NULL.
 * @returns Success once the collection is empty. Raises ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomShuffledCollection_Clear(RandomShuffledCollection* self);

/**
 * @brief Replaces the entire contents of the collection with the given items.
 *
 * This function's contract is an explicit replace: the current contents are cleared first, then
 * @p count items are added. Abandons the in-progress playback sequence.
 * @param self The collection. Must not be NULL.
 * @param items Pointer to the first of @p count contiguous items. May be NULL only when @p count is 0.
 * @param count Number of items the collection should contain afterwards; 0 empties the collection.
 * @returns Success when the contents were replaced. Raises ErrorCode_IllegalArgument if @p self is
 *          NULL, or if @p items is NULL while @p count > 0; ErrorCode_InvalidOperation if storage
 *          could not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomShuffledCollection_SetItems(RandomShuffledCollection* self, void* items, size_t count);

/**
 * @brief Copies the item at the given index (in insertion order) into a caller-provided buffer.
 *
 * @param self The collection. Must not be NULL.
 * @param index Position of the item to read. Valid range is 0..count-1.
 * @param outItem [out] Destination buffer of at least elementSize bytes. Must not be NULL.
 * @returns Success after writing @p outItem. Raises ErrorCode_IllegalArgument if @p self or
 *          @p outItem is NULL; ErrorCode_IndexOutOfBounds if @p index is out of range.
 */
Error RandomShuffledCollection_GetItem(RandomShuffledCollection* self, size_t index, void* outItem);

/**
 * @brief Returns the next item of the shuffled playback sequence.
 *
 * Copies the next item of the current sequence into @p outItem and advances the playback cursor.
 * When no valid sequence exists (first call, sequence exhausted, or the collection was mutated or
 * reshuffled), a new shuffled sequence over all items is started first; each item appears exactly
 * once per sequence. Starting a sequence costs O(n) (allocation-free once the playback storage has
 * grown to the item count); every other call is O(1).
 * @param self The collection. Must not be NULL.
 * @param outItem [out] Destination buffer of at least elementSize bytes. Must not be NULL.
 * @returns Success after writing @p outItem. Raises ErrorCode_IllegalArgument if @p self or
 *          @p outItem is NULL; ErrorCode_InvalidOperation if the collection is empty or the
 *          playback storage could not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomShuffledCollection_GetNext(RandomShuffledCollection* self, void* outItem);

/**
 * @brief Abandons the in-progress playback sequence.
 *
 * The next RandomShuffledCollection_GetNext call starts a freshly shuffled sequence over all
 * items, even if the current sequence was not exhausted.
 * @param self The collection. If NULL, the call is a no-op.
 */
void RandomShuffledCollection_Reshuffle(RandomShuffledCollection* self);

/**
 * @brief Returns the number of items currently in the collection.
 *
 * @param self The collection. Must not be NULL.
 * @returns The item count; 0 for an empty collection.
 */
static inline size_t RandomShuffledCollection_GetElementCount(RandomShuffledCollection* self)
{
    return IList_GetElementCount(&self->_items._list);
}

/**
 * @brief Returns the size in bytes of each item stored in the collection.
 *
 * @param self The collection. Must not be NULL.
 * @returns The per-item byte size chosen at construction.
 */
static inline size_t RandomShuffledCollection_GetElementSize(RandomShuffledCollection* self)
{
    return IList_GetElementSize(&self->_items._list);
}

/**
 * @brief Returns the collection viewed as an ICollection over its items.
 *
 * The view enumerates the items in insertion order; no randomness is involved, no generator state
 * is advanced, and the playback cursor is unaffected. The returned pointer is owned by the
 * collection and valid for its lifetime; do not free it. Enumerators are invalidated by mutations,
 * like for any list.
 * @param self The collection. Must not be NULL.
 * @returns A borrowed pointer to the collection's ICollection view.
 */
static inline ICollection* RandomShuffledCollection_AsCollection(RandomShuffledCollection* self)
{
    return IList_AsCollection(&self->_items._list);
}
