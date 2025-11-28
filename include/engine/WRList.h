#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"
#include "WRMemory.h"
#include "WRComparasionResult.h"
#include "WRCompile.h"


// Types.

/* Boolean properties describing a list. */
typedef enum WRListFlagsEnum
{
    WRListFlags_None = 0,
    WRListFlags_IsBufferWrapper = (1 << 0) /* Whether the list is a wrapper around a constant-capacity buffer. */
} WRListFlags;

/* A generic list of bytes. */
typedef struct WRListStruct
{
    uint8_t* _data; /* The elements stored in this list, as an array of bytes, may be null if the list has a capacity of 0. */
    size_t _count; /* The number of ELEMENTS in the list (not bytes!). */
    size_t _capacity; /* The lists capacity, in ELEMENTS (not bytes!) */
    size_t _elementSize; /* The size of a single element, in bytes. */
    ErrorMessagePool* ErrorPool; /* The error pool used when generating errors, may be modified at any time, may be null. */
    WRListFlags _flags; /* Various read-only flags which describe the list. */
} WRList;

/* Holds information about an element in a list. */
typedef struct WRListElementDataStruct
{
    void* _element; /* A pointer to the element. */
    size_t _elementIndex; /* The index of the element in the list. */
} WRListElementData;

typedef ComparisonResult (*WRListComparator)(WRList* self, WRListElementData a, WRListElementData b, void* userData);

typedef bool (*WRListPredicate)(WRList* self, WRListElementData element, void* userData);

typedef void (*WRListMapper)(WRList* self, WRListElementData sourceEl, void* destElement, void* userData);

typedef int64_t (*WRListIntExtractor)(WRList* self, WRListElementData sourceEl, void* userData);

typedef double (*WRListDoubleExtractor)(WRList* self, WRListElementData sourceEl, void* userData);


// Functions.

/* Constructors. */

/**
 * Constructs a new list with a capacity of 0, thus not allocating an array for the elements.
 * @param elementSize The size of a single element.
 * @param errorPool The error pool to use for this list, may be null.
 * @return Success if the list was constructed, Illegal argument error if the element size is 0.
 */
Error WRList_Construct1(WRList* self, size_t elementSize, ErrorMessagePool* errorPool);

/**
 * Constructs a new list with the given capacity.
 * If the capacity is 0 then no element array is allocated for this list.
 * @param elementSize The size of a single element.
 * @param initialCapacity The initial capacity of the list, in elements.
 * @param errorPool The error pool to use for this list, may be null.
 * @return Success if the list was constructed, Illegal argument error if the element size is 0.
 */
Error WRList_Construct2(WRList* self, size_t elementSize, size_t initialCapacity, ErrorMessagePool* errorPool);

/**
 * Constructs a new list which is a wrapper around a buffer.
 * While the buffer may or may not be of constant capacity, the list will treat it as constant-capacity.
 * The buffer pointer must remain valid throughout the entire lifetime of this list.
 * Mutating the wrapper buffer outside of the list (not via the list functions) may result in
 * broken behavior, it should not be done.
 * @param buffer The buffer to wrap.
 * @param count The number of elements already present in the buffer.
 * @param capacity The capacity of the buffer, in elements.
 * @param elementSize The size of a single element.
 * @param errorPool The error pool to use for this list, may be null.
 * @return Success if the buffer was wrapped, Illegal argument error if the element size is 0.
 */
Error WRList_WrapConstantBuffer(WRList* self, void* buffer, size_t count, size_t capacity, size_t elementSize, ErrorMessagePool* errorPool);

/**
 * Deconstructs the given list, freeing all memory associated with it.
 * This does not free the element array if the list is a wrapper around a buffer.
 * If the list is not a buffer wrapper and the element array is not allocated, then this function does not attempt to free it.
 */
void WRList_Deconstruct1(WRList* self);


/* Basic element manipulation. */

/**
 * Prepends an element to this list.
 * @param item The element to prepend.
 * @return Success if the element was prepended, buffer too small error if the list is of fixed size and has run out of space.
 */
Error WRList_AddFirst(WRList* self, void* item);

/**
 * Appends an element to this list.
 * @param  item The element to append.
 * @return Success if the element was appended, buffer too small error if the list is of fixed size and has run out of space.
 */
Error WRList_AddLast(WRList* self, void* item);

/**
 * Inserts an element at the given index.
 * @param item The element to insert.
 * @param index The index at which to insert the element.
 * @return Success if the element was inserted;
 * buffer too small error if the list is of fixed size and has run out of space;
 * index out of bounds error if the given index is greater than the list's element count.
 */
Error WRList_Insert(WRList* self, void* item, size_t index);

/**
 * Appends a range elements to this list.
 * This method is more efficient than calling AddLast repeatedly.
 * @param items The elements to append.
 * @param itemCount The number of items in the item array.
 * @return Success if the elements were appended, buffer too small error if the list is of fixed size and has run out of space.
 */
Error WRList_AppendRange(WRList* self, void* items, size_t itemCount);

/**
 * Prepends a range elements to this list.
 * This method is more efficient than calling AddFirst repeatedly.
 * @param items The elements to prepend.
 * @param itemCount The number of items in the item array.
 * @return Success if the elements were prepended, buffer too small error if the list is of fixed size and has run out of space.
 */
Error WRList_PrependRange(WRList* self, void* items, size_t itemCount);

/**
 * Inserts a range of elements starting at the given index.
 * This method is more efficient than calling Insert repeatedly.
 * @param items The elements to insert.
 * @param itemCount The number of items in the item array.
 * @param startIndex The index at which to insert the range of elements.
 * @return Success if the element was inserted;
 * buffer too small error if the list is of fixed size and has run out of space;
 * index out of bounds error if the given index is greater than the list's element count.
 */
Error WRList_InsertRange(WRList* self, void* items, size_t itemCount, size_t startIndex);

/**
 * Removes the first element of the list.
 * @return Success if the first element was removed, index out of bounds error if the list is empty.
 */
Error WRList_RemoveFirst(WRList* self);

/**
 * Removes the last element of the list.
 * @return Success if the last element was removed, index out of bounds error if the list is empty.
 */
Error WRList_RemoveLast(WRList* self);

/**
 * Removes an element at the given index.
 * @param index The index at which to remove the element.
 * @return Success if the first element was removed,
 * index out of bounds error if the index is >= the list's element count.
 */
Error WRList_RemoveAt(WRList* self, size_t index);

/**
 * Removes a range of elements.
 * This method is more efficient than calling RemoveAt repeatedly.
 * @param startIndexInclusive The start index at which to remove the elements, inclusive.
 * @param endIndexExclusive The end index at which to remove the elements, exclusive.
 * @return Success if the range was removed;
 * index out of bounds error if either start or end indices are out of bounds;
 * illegal argument error if start index is > than end index.
 */
Error WRList_RemoveRange(WRList* self, size_t startIndexInclusive, size_t endIndexExclusive);

/**
 * Replaces an element at the given index with a new element.
 * @param element The new element to replace the old one with.
 * @param index The index at which to replace the element.
 * @return Success if the element was replaced,
 * index out of bounds error if the index is >= the list's element count.
 */
Error WRList_Replace(WRList* self, void* element, size_t index);

/**
 * Clears the list.
 */
void WRList_Clear(WRList* self);

/**
 * Removes the first element in the list and returns it by value.
 * @param out The removed element, must not be null.
 * @return Success if the element was removed, index out of bounds error if the list is empty.
 */
Error WRList_PopFirst(WRList* self, void* out);

/**
 * Removes the last element in the list and returns it by value.
 * @param out The removed element, must not be null.
 * @return Success if the element was removed, index out of bounds error if the list is empty.
 */
Error WRList_PopLast(WRList* self, void* out);

/**
 * Removes the element at the given index in the list and returns it by value.
 * @param index The index at which to remove the element.
 * @param out The removed element, must not be null.
 * @return Success if the element was removed, 
 * index out of bounds error if the index is >= the list's element count.
 */
Error WRList_PopAt(WRList* self, size_t index, void* out);

/**
 * Swaps 2 elements in the list.
 * @param indexA The index of the first element.
 * @param indexB The index of the second element.
 * @return Success if the elements were swapped, index out of bounds error if either element is >= the list's element count.
 */
Error WRList_Swap(WRList* self, size_t indexA, size_t indexB);


/* Info retrieval. */

/**
 * Returns the first element of the list by value.
 * @param out The returned element.
 * @return Success if the element was returned, index out of bounds error if the list is empty.
 */
Error WRList_GetFirst(WRList* self, void* out);

/**
 * Returns the last element of the list by value.
 * @param out The returned element.
 * @return Success if the element was returned, index out of bounds error if the list is empty.
 */
Error WRList_GetLast(WRList* self, void* out);

/**
 * Returns the element at the given index by value.
 * @param out The returned element.
 * @return Success if the element was returned,
 * index out of bounds error if the index is >= the list's element count.
 */
Error WRList_GetAt(WRList* self, size_t index, void* out);

/**
 * Returns a pointer to the first element of the list.
 * @param out A pointer to the first element.
 * @return Success if the element was returned, index out of bounds error if the list is empty.
 */
Error WRList_GetPointerToFirst(WRList* self, void** out);

/**
 * Returns a pointer to the last element of the list.
 * @param out A pointer to the last element.
 * @return Success if the element was returned, index out of bounds error if the list is empty.
 */
Error WRList_GetPointerToLast(WRList* self, void** out);

/**
 * Returns a pointer to the element in the list at the given index.
 * @param index The index of the element to return.
 * @param out A pointer to the element at the index.
 * @return Success if the element was returned, index out of bounds error if the index is >= the list's element count.
 */
Error WRList_GetPointerToElement(WRList* self, size_t index, void** out);

/**
 * Determines whether the list contains an element which satisfied the given predicate.
 * @param predicate The predicate to use for testing the elements.
 * @param userData Optional user data supplied to the predicate, may be null.
 * @return true if the list contains an element which satisfies the predicate, false otherwise.
 */
bool WRList_Contains(WRList* self, WRListPredicate predicate, void* userData);

/**
 * Finds the first index of an element which satisfies the given predicate.
 * If no element is found, outIndex is set to 0.
 * @param predicate The predicate to use for testing the elements.
 * @param userData Optional user data supplied to the predicate, may be null.
 * @return true if an element which satisfies the predicate was found, false otherwise.
 */
bool WRList_FirstIndexOf(WRList* self, WRListPredicate predicate, void* userData, size_t* outIndex);

/**
 * Finds the last index of an element which satisfies the given predicate.
* If no element is found, outIndex is set to 0.
 * @param predicate The predicate to use for testing the elements.
 * @param userData Optional user data supplied to the predicate, may be null.
 * @return true if the an element which satisfies the predicate was found, false otherwise.
 */
bool WRList_LastIndexOf(WRList* self, WRListPredicate predicate, void* userData, size_t* outIndex);


/* Full list manipulation. */

/**
 * Sorts the list in ascending order.
 * @param comparator The comparator to use when comparing elements.
 * @param userData Optional user data supplied to the comparator, may be null.
 */
void WRList_SortAscending(WRList* self, WRListComparator comparator, void* userData);

/**
 * Sorts the list in descending order.
 * @param comparator The comparator to use when comparing elements.
 * @param userData Optional user data supplied to the comparator, may be null.
 */
void WRList_SortDescending(WRList* self, WRListComparator comparator, void* userData);

/**
 * Filters the list (in place), removing any elements which do not match the given predicate.
 * @param predicate The predicate to use when filtering elements.
 * @param userData Optional user data supplied to the predicate, may be null.
 */
void WRList_Filter(WRList* self, WRListPredicate predicate, void* userData);

/**
 * Maps this list's elements from one type to another by applying the given mapping function
 * and storing the resulting elements in the destination list.
 * The destination list is cleared before the mapping takes place.
 * @param destination The destination where the new elements will be stored.
 * @param mapper The function used to convert the source elements into destination elements.
 * @param destElementBuffer A temporary buffer where to store exactly 1 destination element
 * (used mid-operation while converting the element).
 * @param userData Optional user data supplied to the mapper, may be null.
 * @returns Success if the mapping completed;
 * buffer too small error if the destination list is of fixed size and is too small to fit all resulting elements;
 * index out of bounds error if there is an internal error with the mapping algorithm, should never happen but
 * should be checked anyway.
 */
Error WRList_Map(WRList* self, WRList* destination, WRListMapper mapper, void* destElementBuffer, void* userData);

/**
 * Maps this list's elements in place from one type to another by applying the given mapping function.
 * The resulting mapped elements must the same size as the list's elements.
 * @param mapper The function used to convert the source elements into destination elements.
 * @param destElementBuffer A temporary buffer where to store exactly 1 destination element
 * (used mid-operation while converting the element).
 * @param userData Optional user data supplied to the mapper, may be null.
 * @returns Success if the mapping completed, index out of bounds error if there is an internal error
 * with the mapping algorithm, should never happen but should be checked anyway.
 */
Error WRList_MapToSelf(WRList* self, WRListMapper mapper, void* destElementBuffer, void* userData);

/**
 * Returns a sum of the elements in this list as an integer.
 * This function returns a sum of 0 if the list is empty.
 * @param extractor A function which extracts an integer value from a list's element.
 * @param userData Optional user data supplied to the extractor, may be null.
 * @return Always success.
 */
int64_t WRList_SumInt(WRList* self, WRListIntExtractor extractor, void* userData);

/**
 * Returns a sum of the elements in this list as a double.
 * This function returns a sum of 0.0 if the list is empty.
 * @param extractor A function which extracts a double value from a list's element.
 * @param userData Optional user data supplied to the extractor, may be null.
 * @return Always success.
 */
double WRList_SumDouble(WRList* self, WRListDoubleExtractor extractor, void* userData);

/**
 * Returns the maximum integer value in this list.
 * This function fails if the list is empty.
 * @param extractor A function which extracts a integer value from a list's element.
 * @param outValue The resulting maximum value. Initialized to 0 if the function fails.
 * @param userData Optional user data supplied to the extractor, may be null.
 * @return Success if the maximum value was calculated, invalid operation error if the list is empty.
 */
Error WRList_MaxInt(WRList* self, WRListIntExtractor extractor, int64_t* outValue, void* userData);

/**
 * Returns the maximum double value in this list.
 * This function fails if the list is empty.
 * @param extractor A function which extracts a double value from a list's element.
 * @param outValue The resulting maximum value. Initialized to 0.0 if the function fails.
 * @param userData Optional user data supplied to the extractor, may be null.
 * @return Success if the maximum value was calculated, invalid operation error if the list is empty.
 */
Error WRList_MaxDouble(WRList* self, WRListDoubleExtractor extractor, double* outValue, void* userData);

/**
 * Returns the minimum integer value in this list.
 * This function fails if the list is empty.
 * @param extractor A function which extracts a integer value from a list's element.
 * @param outValue The resulting minimum value. Initialized to 0 if the function fails.
 * @param userData Optional user data supplied to the extractor, may be null.
 * @return Success if the minimum value was calculated, invalid operation error if the list is empty.
 */
Error WRList_MinInt(WRList* self, WRListIntExtractor extractor, int64_t* outValue, void* userData);

/**
 * Returns the minimum double value in this list.
 * This function fails if the list is empty.
 * @param extractor A function which extracts a double value from a list's element.
 * @param outValue The resulting minimum value. Initialized to 0.0 if the function fails.
 * @param userData Optional user data supplied to the extractor, may be null.
 * @return Success if the minimum value was calculated, invalid operation error if the list is empty.
 */
Error WRList_MinDouble(WRList* self, WRListDoubleExtractor extractor, double* outValue, void* userData);

/**
 * Counts the number of elements which pass the given predicate.
 * @param predicate The predicate to use when counting elements.
 * @param userData optional user data supplied to the predicate, may be null.
 * @return The number of elements passing the predicate.
 */
size_t WRList_CountWhere(WRList* self, WRListPredicate* predicate, void* userData);

/**
 * Reverses the elements of the list.
 */
void WRList_Reverse(WRList* self);


/* Technical. */

/**
 * Ensures that the list has at least the given capacity, in elements.
 * @param capacity The minimum capacity of the list, in elements.
 * @returns true if the list's capacity was ensured to be at least the given capacity,
 * false if the requested capacity is greater than the list's current capacity and the list is of fixed capacity.
 */
bool WRList_EnsureCapacity(WRList* self, size_t capacity);

/**
 * Reserves space for the given number of extra elements.
 * @param extraElementCount The number of extra elements.
 * @returns true if the list's capacity was ensured to have enough space for the given number of extra elements,
 * false if the requested capacity is greater than the list's current capacity and the list is of fixed capacity.
 */
bool WRList_ReserveSpace(WRList* self, size_t extraElementCount);

/**
 * Determines whether the list is of fixed capacity.
 * Typically a buffer wrapper will be of fixed-capacity, though this doesn't mean that all fixed-capacity
 * lists are buffer wrappers.
 */
bool WRList_IsFixedCapacity(WRList* self);

/**
 * Determines whether the list is a wrapper around a buffer.
 */
bool WRList_IsWrapperBuffer(WRList* self);

/**
 * Gets the number of extra elements this list can still store without having to resize or run out of capacity.
 */
size_t WRList_GetCapacityRemaining(WRList* self);


/* Buffers. */

/**
 * Creates a buffer which wraps the elements in this list.
 * The returned buffer is of constant capacity.
 */
GenericBuffer WRList_ToConstantBuffer(WRList* self);

/**
 * Creates a buffer which wraps the elements in this list.
 * The returned buffer is of dynamic size and can grow.
 * If the list is of constant-capacity, then this function will return
 * a constant capacity buffer, not a dynamic capacity one.
 */
GenericBuffer WRList_ToDynamicBuffer(WRList* self);


/* Comparators. */
#define WRLIST_COMPARE_FUNC(funcName, typeName)\
    static inline ComparisonResult WRList_Compare##funcName(WRList* self, WRListElementData a, WRListElementData b, void* userData)\
    {\
        UNUSED(self);\
        UNUSED(userData);\
        return Comparator_Compare##funcName(*((const typeName*)a._element), *((const typeName*)b._element));\
    }

WRLIST_COMPARE_FUNC(Int8, int8_t)

WRLIST_COMPARE_FUNC(UInt8, uint8_t)

WRLIST_COMPARE_FUNC(Int16, int16_t)

WRLIST_COMPARE_FUNC(UInt16, uint16_t)

WRLIST_COMPARE_FUNC(Int32, int32_t)

WRLIST_COMPARE_FUNC(UInt32, uint32_t)

WRLIST_COMPARE_FUNC(Int64, int64_t)

WRLIST_COMPARE_FUNC(UInt64, uint64_t)

WRLIST_COMPARE_FUNC(SizeT, size_t)

WRLIST_COMPARE_FUNC(Float, float)

WRLIST_COMPARE_FUNC(Double, double)

#undef WRLIST_COMPARE_FUNC

static inline ComparisonResult WList_CompareString(WRList* self, WRListElementData a, WRListElementData b, void* userData)
{
    UNUSED(self);
    UNUSED(userData);
    return Comparator_CompareString(a._element, b._element);
}


/* Extractors. */
#define WRLIST_EXTRACT_FUNC(funcName, typeName, targetType, targetTypeName)\
    static inline targetType WRList_Extract##targetTypeName##From##funcName(WRList* self, WRListElementData sourceEl, void* userData)\
    {\
        UNUSED(self);\
        UNUSED(userData);\
        return (targetType)(*((const typeName*)sourceEl._element));\
    }

#define WRLIST_EXTRACT_PAIR_FUNC(funcName, typeName)\
    WRLIST_EXTRACT_FUNC(funcName, typeName, int64_t, Int)\
    WRLIST_EXTRACT_FUNC(funcName, typeName, double, Double)

WRLIST_EXTRACT_PAIR_FUNC(Int8, int8_t)
WRLIST_EXTRACT_PAIR_FUNC(UInt8, uint8_t)
WRLIST_EXTRACT_PAIR_FUNC(Int16, int16_t)
WRLIST_EXTRACT_PAIR_FUNC(UInt16, uint16_t)
WRLIST_EXTRACT_PAIR_FUNC(Int32, int32_t)
WRLIST_EXTRACT_PAIR_FUNC(UInt32, uint32_t)
WRLIST_EXTRACT_PAIR_FUNC(Int64, int64_t)
WRLIST_EXTRACT_PAIR_FUNC(UInt64, uint64_t)
WRLIST_EXTRACT_PAIR_FUNC(SizeT, size_t)
WRLIST_EXTRACT_PAIR_FUNC(Float, float)
WRLIST_EXTRACT_PAIR_FUNC(Double, double)

#undef WRLIST_EXTRACT_PAIR_FUNC
#undef WRLIST_EXTRACT_FUNC