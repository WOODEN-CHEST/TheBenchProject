#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"
#include "WRMemory.h"
#include "WRComparasionResult.h"

// Types.
typedef enum WRListFlagsEnum
{
    WRListFlags_None = 0,
    WRListFlags_IsBufferWrapper = (1 << 0)
} WRListFlags;

typedef struct WRListStruct
{
    uint8_t* _data;
    size_t _count;
    size_t _capacity;
    size_t _elementSize;
    ErrorMessagePool* ErrorPool;
    WRListFlags _flags;
} WRList;

typedef struct WRListElementDataStruct
{
    void* _element;
    size_t _elementIndex;
} WRListElementData;

typedef ComparisonResult (*WRListComparator)(WRList* self, WRListElementData a, WRListElementData b, void* userData);

typedef bool (*WRListPredicate)(WRList* self, void* element, void* userData);

typedef void (*WRListMapper)(WRList* self, WRListElementData sourceEl, void* destElement, void* userData);

typedef int64_t (*WRListIntExtractor)(WRList* self, WRListElementData* sourceEl);

typedef double (*WRListDoubleExtractor)(WRList* self, WRListElementData* sourceEl);


// Functions.
/* Constructors. */
Error WRList_Construct1(WRList* self, size_t elementSize, ErrorMessagePool* errorPool);

Error WRList_Construct2(WRList* self, size_t elementSize, size_t initialCapacity, ErrorMessagePool* errorPool);

void WRList_WrapConstantBuffer(WRList* self, void* buffer, size_t count, size_t capacity, size_t elementSize, ErrorMessagePool* errorPool);

void WRList_Deconstruct1(WRList* self);


/* Basic element manipulation. */
Error WRList_AddFirst(WRList* self, void* item);

Error WRList_AddLast(WRList* self, void* item);

Error WRList_Insert(WRList* self, void* item, size_t index);

Error WRList_AddRange(WRList* self, void** items, size_t itemCount);

Error WRList_InsertRange(WRList* self, void** items, size_t itemCount, size_t startIndex);

Error WRList_RemoveFirst(WRList* self);

Error WRList_RemoveLast(WRList* self);

Error WRList_RemoveAt(WRList* self, size_t index);

Error WRList_RemoveRange(WRList* self, size_t startIndesInclusive, size_t endIndexExclusive);

Error WRList_Replace(WRList* self, void* element, size_t index);

void WRList_Clear(WRList* self);

Error WRList_PopFirst(WRList* self, void* out);

Error WRList_PopLast(WRList* self, void* out);

Error WRList_PopAt(WRList* self, size_t index, void* out);


/* Info retrieval. */
Error WRList_GetFirst(WRList* self, void* out);

Error WRList_GetLast(WRList* self, void* out);

Error WRList_GetAt(WRList* self, size_t index, void* out);

Error WRList_GetPointerToFirst(WRList* self, void** out);

Error WRList_GetPointerToLast(WRList* self, void** out);

Error WRList_GetPointerToElement(WRList* self, size_t index, void** out);

bool WRList_Contains(WRList* self, WRListPredicate* predicate, void* userData);

bool WRList_FirstIndexOf(WRList* self, WRListPredicate predicate, void* userData, size_t* outIndex);

bool WRList_LastIndexOf(WRList* self, WRListPredicate predicate, void* userData, size_t* outIndex);


/* Full list manipulation. */
void WRList_SortAscending(WRList* self, WRListComparator comparator, void* userData);

void WRList_SortDescending(WRList* self, WRListComparator comparator, void* userData);

void WRList_Filter(WRList* self, WRListPredicate predicate, void* userData);

void WRList_Map(WRList* self, WRList* destination, WRListMapper mapper, void* destElementBuffer, void* userData);

void WRList_MapToSelf(WRList* self, WRListMapper mapper, void* destElementBuffer, void* userData);

int64_t WRList_SumInt(WRList* self, WRListIntExtractor extractor);

double WRList_SumDouble(WRList* self, WRListDoubleExtractor extractor);

int64_t WRList_MaxInt(WRList* self, WRListIntExtractor extractor);

double WRList_MaxDouble(WRList* self, WRListDoubleExtractor extractor);

int64_t WRList_MinInt(WRList* self, WRListIntExtractor extractor);

double WRList_MinDouble(WRList* self, WRListDoubleExtractor extractor);

size_t WRList_CountWhere(WRList* self, WRListPredicate* predicate);

void WRList_Reverse(WRList* self);


/* Technical. */
bool WRList_EnsureCapacity(WRList* self, size_t capacity);

bool WRList_ReserveSpace(WRList* self, size_t extraElementCount);

bool WRList_IsFixedCapacity(WRList* self);

bool WRList_IsWrapperBuffer(WRList* self);

size_t WRList_GetCapacityRemaining(WRList* self);


/* Buffers. */
GenericBuffer WRList_ToConstantBuffer(WRList* self);

GenericBuffer WRList_ToDynamicBuffer(WRList* self);