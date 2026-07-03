#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WRArrayList.h"
#include "WRCollection.h"
#include "WRError.h"
#include "WRMemory.h"
#include "WRRandom.h"


/**
 * RandomWeightedCollection module provides a collection of fixed-size items where each item has an
 * attached selection weight (a finite double >= 0.0), and random items can be picked from the
 * collection with a probability proportional to their weight. An item with weight 0.0 is a valid
 * member of the collection but is never picked.
 *
 * The collection is optimized for the "fill once, pick many times" usage pattern: picking uses the
 * alias method, so after an internal O(n) lookup table build it costs O(1) per pick with zero
 * allocations. Mutations (add / remove / set weight) only mark the table stale; it is lazily
 * rebuilt on the first pick that follows. Frequently alternating single mutations and picks
 * therefore degrades to O(n) per pick and should be avoided.
 *
 * The items (without their weights) can be enumerated in insertion order through the ICollection
 * view returned by RandomWeightedCollection_AsCollection; enumeration involves no randomness and
 * does not advance any generator.
 */


// Types.
/**
 * @brief A collection of fixed-size items with attached selection weights for weighted random picking.
 *
 * Construct with RandomWeightedCollection_Construct1 / Construct2, fill it with items and weights,
 * pick items with the GetRandom* functions using a caller-supplied Random, and release it with
 * RandomWeightedCollection_Deconstruct. Items are stored by value (elementSize bytes each) in
 * insertion order; weights live in a parallel array. Not thread-safe.
 */
typedef struct RandomWeightedCollectionStruct
{
    /** @brief Item storage, in insertion order. Its embedded list also provides the ICollection view. */
    ArrayList _items;
    /** @brief Parallel array of double weights; entry N is the weight of item N. */
    GenericBuffer _weights;
    /** @brief Alias-method probability table (double per item). Rebuilt lazily; valid only when _isAliasTableValid. */
    GenericBuffer _aliasProbabilities;
    /** @brief Alias-method alias index table (size_t per item). Rebuilt lazily; valid only when _isAliasTableValid. */
    GenericBuffer _aliasIndices;
    /** @brief Scratch index worklist reused by alias table rebuilds to avoid per-rebuild allocations. */
    GenericBuffer _aliasWorklist;
    /** @brief Cached sum of all weights. Maintained incrementally and recomputed exactly on each table rebuild. */
    double _totalWeight;
    /** @brief Whether the alias tables currently match the items and weights. Cleared by every mutation. */
    bool _isAliasTableValid;
} RandomWeightedCollection;


// Functions.
/**
 * @brief Constructs an empty weighted collection for items of the given size.
 *
 * The collection starts with zero items and zero capacity; no storage is allocated until items are
 * added. Must later be released with RandomWeightedCollection_Deconstruct.
 * @param self The collection to initialize. If NULL, the call is a no-op.
 * @param elementSize Size in bytes of each item; must be > 0.
 */
void RandomWeightedCollection_Construct1(RandomWeightedCollection* self, size_t elementSize);

/**
 * @brief Constructs an empty weighted collection and reserves capacity for the expected item count.
 *
 * Behaves like RandomWeightedCollection_Construct1 and then reserves item and weight storage for
 * at least initialCapacity items (the count stays zero). A failed reservation is ignored and
 * leaves a valid empty collection.
 * @param self The collection to initialize. If NULL, the call is a no-op.
 * @param elementSize Size in bytes of each item; must be > 0.
 * @param initialCapacity Number of items to reserve storage for up front. 0 reserves nothing.
 */
void RandomWeightedCollection_Construct2(RandomWeightedCollection* self, size_t elementSize, size_t initialCapacity);

/**
 * @brief Releases the collection's storage and resets it to a valid empty state.
 *
 * Frees the item, weight, and alias table storage. After the call the collection is a valid empty
 * collection with the same element size semantics as after construction, so it may be reused or
 * discarded. Safe to call more than once.
 * @param self The collection to release. If NULL, the call is a no-op.
 */
void RandomWeightedCollection_Deconstruct(RandomWeightedCollection* self);

/**
 * @brief Appends one item with the given selection weight.
 *
 * Copies elementSize bytes from @p item to the end of the collection and attaches @p weight to it.
 * Marks the pick table stale.
 * @param self The collection. Must not be NULL.
 * @param item Pointer to the item to copy in. Must not be NULL.
 * @param weight The item's selection weight. Must be finite and >= 0.0; 0.0 means "never picked".
 * @returns Success when the item was added. Raises ErrorCode_IllegalArgument if @p self or @p item
 *          is NULL; ErrorCode_ArgumentOutOfRange if @p weight is negative, NaN, or infinite;
 *          ErrorCode_InvalidOperation if storage could not be grown.
 */
Error RandomWeightedCollection_Add(RandomWeightedCollection* self, void* item, double weight);

/**
 * @brief Appends a run of items with their parallel selection weights.
 *
 * Copies @p count items from @p items and their weights from @p weights. All weights are validated
 * before anything is added, so on a weight validation error the collection is unchanged. Marks the
 * pick table stale.
 * @param self The collection. Must not be NULL.
 * @param items Pointer to the first of @p count contiguous items. May be NULL only when @p count is 0.
 * @param weights Pointer to the first of @p count weights; weights[N] belongs to items[N]. May be
 *        NULL only when @p count is 0.
 * @param count Number of items to add; 0 is a valid no-op.
 * @returns Success when all items were added. Raises ErrorCode_IllegalArgument if @p self is NULL,
 *          or if @p items or @p weights is NULL while @p count > 0; ErrorCode_ArgumentOutOfRange if
 *          any weight is negative, NaN, or infinite; ErrorCode_InvalidOperation if storage could
 *          not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomWeightedCollection_AddRange(RandomWeightedCollection* self, void* items, double* weights, size_t count);

/**
 * @brief Removes the item (and its weight) at the given index, shifting later items down by one.
 *
 * Later items keep their insertion order; their indices decrease by one. Marks the pick table stale.
 * @param self The collection. Must not be NULL.
 * @param index Position of the item to remove. Valid range is 0..count-1.
 * @returns Success on removal. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_IndexOutOfBounds if @p index is out of range.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomWeightedCollection_RemoveAt(RandomWeightedCollection* self, size_t index);

/**
 * @brief Removes all items and weights, leaving an empty collection.
 *
 * Capacity is retained for reuse. Marks the pick table stale.
 * @param self The collection. Must not be NULL.
 * @returns Success once the collection is empty. Raises ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomWeightedCollection_Clear(RandomWeightedCollection* self);

/**
 * @brief Replaces the entire contents of the collection with the given items and weights.
 *
 * This function's contract is an explicit replace: the current contents are cleared first, then
 * @p count items and their parallel weights are added. All weights are validated before anything
 * is modified, so on a weight validation error the collection is unchanged.
 * @param self The collection. Must not be NULL.
 * @param items Pointer to the first of @p count contiguous items. May be NULL only when @p count is 0.
 * @param weights Pointer to the first of @p count weights; weights[N] belongs to items[N]. May be
 *        NULL only when @p count is 0.
 * @param count Number of items the collection should contain afterwards; 0 empties the collection.
 * @returns Success when the contents were replaced. Raises ErrorCode_IllegalArgument if @p self is
 *          NULL, or if @p items or @p weights is NULL while @p count > 0;
 *          ErrorCode_ArgumentOutOfRange if any weight is negative, NaN, or infinite;
 *          ErrorCode_InvalidOperation if storage could not be grown.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomWeightedCollection_SetItems(RandomWeightedCollection* self, void* items, double* weights, size_t count);

/**
 * @brief Copies the item at the given index into a caller-provided buffer.
 *
 * @param self The collection. Must not be NULL.
 * @param index Position of the item to read. Valid range is 0..count-1.
 * @param outItem [out] Destination buffer of at least elementSize bytes. Must not be NULL.
 * @returns Success after writing @p outItem. Raises ErrorCode_IllegalArgument if @p self or
 *          @p outItem is NULL; ErrorCode_IndexOutOfBounds if @p index is out of range.
 */
Error RandomWeightedCollection_GetItem(RandomWeightedCollection* self, size_t index, void* outItem);

/**
 * @brief Obtains a pointer to the item at the given index inside the collection's storage.
 *
 * The pointer aliases live storage: it is owned by the collection and is invalidated by any
 * mutation of the collection.
 * @param self The collection. Must not be NULL.
 * @param index Position of the item to address. Valid range is 0..count-1.
 * @param outPointer [out] Receives the borrowed item pointer. Must not be NULL.
 * @returns Success after writing @p outPointer. Raises ErrorCode_IllegalArgument if @p self or
 *          @p outPointer is NULL; ErrorCode_IndexOutOfBounds if @p index is out of range.
 */
Error RandomWeightedCollection_GetPointerToItem(RandomWeightedCollection* self, size_t index, void** outPointer);

/**
 * @brief Reads the selection weight of the item at the given index.
 *
 * @param self The collection. Must not be NULL.
 * @param index Position of the item whose weight to read. Valid range is 0..count-1.
 * @param outWeight [out] Receives the item's weight. Must not be NULL.
 * @returns Success after writing @p outWeight. Raises ErrorCode_IllegalArgument if @p self or
 *          @p outWeight is NULL; ErrorCode_IndexOutOfBounds if @p index is out of range.
 */
Error RandomWeightedCollection_GetWeight(RandomWeightedCollection* self, size_t index, double* outWeight);

/**
 * @brief Replaces the selection weight of the item at the given index.
 *
 * Marks the pick table stale.
 * @param self The collection. Must not be NULL.
 * @param index Position of the item whose weight to replace. Valid range is 0..count-1.
 * @param weight The new selection weight. Must be finite and >= 0.0.
 * @returns Success on replacement. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p weight is negative, NaN, or infinite;
 *          ErrorCode_IndexOutOfBounds if @p index is out of range.
 */
Error RandomWeightedCollection_SetWeight(RandomWeightedCollection* self, size_t index, double weight);

/**
 * @brief Picks a random item index, with probability proportional to the item weights.
 *
 * Draws from the supplied generator and writes the index of the picked item. Items with weight 0.0
 * are never picked. Lazily rebuilds the internal pick table (O(n)) if the collection was mutated
 * since the last pick; otherwise the pick is O(1) and allocation-free.
 * @param self The collection. Must not be NULL.
 * @param rng The random number generator to draw from. Must not be NULL.
 * @param outIndex [out] Receives the picked item's index. Must not be NULL.
 * @returns Success after writing @p outIndex. Raises ErrorCode_IllegalArgument if @p self, @p rng,
 *          or @p outIndex is NULL; ErrorCode_InvalidOperation if the collection is empty, if the
 *          total weight is 0.0 (no pickable item), or if the total weight overflows to infinity.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomWeightedCollection_GetRandomIndex(RandomWeightedCollection* self, Random* rng, size_t* outIndex);

/**
 * @brief Picks a random item, with probability proportional to the item weights, and copies it out.
 *
 * Equivalent to RandomWeightedCollection_GetRandomIndex followed by RandomWeightedCollection_GetItem.
 * @param self The collection. Must not be NULL.
 * @param rng The random number generator to draw from. Must not be NULL.
 * @param outItem [out] Destination buffer of at least elementSize bytes. Must not be NULL.
 * @returns Success after writing @p outItem. See RandomWeightedCollection_GetRandomIndex for the
 *          error conditions.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomWeightedCollection_GetRandomItem(RandomWeightedCollection* self, Random* rng, void* outItem);

/**
 * @brief Picks a random item, with probability proportional to the item weights, by reference.
 *
 * Equivalent to RandomWeightedCollection_GetRandomIndex followed by
 * RandomWeightedCollection_GetPointerToItem. The pointer aliases live storage and is invalidated
 * by any mutation of the collection.
 * @param self The collection. Must not be NULL.
 * @param rng The random number generator to draw from. Must not be NULL.
 * @param outPointer [out] Receives the borrowed pointer to the picked item. Must not be NULL.
 * @returns Success after writing @p outPointer. See RandomWeightedCollection_GetRandomIndex for the
 *          error conditions.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error RandomWeightedCollection_GetRandomItemPointer(RandomWeightedCollection* self, Random* rng, void** outPointer);

/**
 * @brief Returns the number of items currently in the collection.
 *
 * @param self The collection. Must not be NULL.
 * @returns The item count; 0 for an empty collection.
 */
static inline size_t RandomWeightedCollection_GetElementCount(RandomWeightedCollection* self)
{
    return IList_GetElementCount(&self->_items._list);
}

/**
 * @brief Returns the size in bytes of each item stored in the collection.
 *
 * @param self The collection. Must not be NULL.
 * @returns The per-item byte size chosen at construction.
 */
static inline size_t RandomWeightedCollection_GetElementSize(RandomWeightedCollection* self)
{
    return IList_GetElementSize(&self->_items._list);
}

/**
 * @brief Returns the sum of all item weights.
 *
 * The value is maintained incrementally, so after very long mutation sequences it may carry tiny
 * floating-point rounding drift; it is recomputed exactly whenever the pick table is rebuilt.
 * @param self The collection. Must not be NULL.
 * @returns The total weight; 0.0 for an empty collection.
 */
static inline double RandomWeightedCollection_GetTotalWeight(RandomWeightedCollection* self)
{
    return self->_totalWeight;
}

/**
 * @brief Returns the collection viewed as an ICollection over its items.
 *
 * The view enumerates the items (not the weights) in insertion order; no randomness is involved
 * and no generator state is advanced. The returned pointer is owned by the collection and valid
 * for its lifetime; do not free it. Enumerators are invalidated by mutations, like for any list.
 * @param self The collection. Must not be NULL.
 * @returns A borrowed pointer to the collection's ICollection view.
 */
static inline ICollection* RandomWeightedCollection_AsCollection(RandomWeightedCollection* self)
{
    return IList_AsCollection(&self->_items._list);
}
