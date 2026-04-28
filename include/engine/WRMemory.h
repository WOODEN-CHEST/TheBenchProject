#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRComparator.h"


#define GENERIC_BUFFER_INDEX_INVALID (~((size_t)0))


// Types.
typedef struct GenericBufferStruct GenericBuffer;

typedef bool (*GenericBufferAllocateCallback)(GenericBuffer* destination, size_t requestedCapacity);

typedef enum GenericBufferFlagsEnum
{
    GenericBufferFlags_None = 0,
    GenericBufferFlags_FixedCapacity = (1 << 0),
    GenericBufferFlags_ReadOnly = (1 << 1),
} GenericBufferFlags;

struct GenericBufferStruct
{
    unsigned char* _data;
    size_t _capacity;
    size_t _count;
    size_t _elementSize;
    void* _userData;
    GenericBufferAllocateCallback _requestMoreSpaceCallback;
    GenericBufferFlags _flags;
};

typedef struct GenericBufferElementDataStruct
{
    void* _element;
    size_t _index;
} GenericBufferElementData;

typedef bool (*GenericBufferPredicate)(GenericBuffer* buffer, GenericBufferElementData element, void* userData);

typedef ComparisonResult (*GenericBufferComparator)(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b, void* userData);

typedef void (*GenericBufferMapper)(GenericBuffer* buffer, GenericBufferElementData sourceElement, void* destinationElement, void* userData);

typedef int64_t (*GenericBufferIntExtractor)(GenericBuffer* buffer, GenericBufferElementData sourceElement, void* userData);

typedef double (*GenericBufferDoubleExtractor)(GenericBuffer* buffer, GenericBufferElementData sourceElement, void* userData);


// Functions.
void GenericBuffer_CreateVariable(GenericBuffer* buffer, 
    void* destination,
    size_t bufferCapacity,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback);

void GenericBuffer_AllocateVariable(GenericBuffer* buffer, 
     size_t initialCapacity,
     size_t elementSize);

void GenericBuffer_CreateConstant(GenericBuffer* buffer, void* destination, size_t bufferCapacity, size_t elementSize, size_t elementCount);

void GenericBuffer_SetCallback(GenericBuffer* buffer, GenericBufferAllocateCallback callback, void* userData);

void GenericBuffer_ClearCallback(GenericBuffer* buffer);

bool GenericBuffer_EnsureTotalCapacity(GenericBuffer* buffer, size_t requiredSize);

bool GenericBuffer_ReserveMoreCapacity(GenericBuffer* buffer, size_t requiredSize);

size_t GenericBuffer_GetCapacityRemaining(GenericBuffer* buffer);

static inline bool GenericBuffer_IsReadOnly(GenericBuffer* buffer)
{
    return buffer->_flags & GenericBufferFlags_ReadOnly;
}

static inline bool GenericBuffer_IsFixedCapacity(GenericBuffer* buffer)
{
    return buffer->_flags & GenericBufferFlags_FixedCapacity;
}



bool GenericBuffer_AddLast(GenericBuffer* buffer, void* item);

bool GenericBuffer_AddFirst(GenericBuffer* buffer, void* item);

bool GenericBuffer_Insert(GenericBuffer* buffer, void* item, size_t index);

bool GenericBuffer_Replace(GenericBuffer* buffer, void* item, size_t index);

bool GenericBuffer_RemoveAt(GenericBuffer* buffer, size_t index);

bool GenericBuffer_RemoveFirst(GenericBuffer* buffer);

bool GenericBuffer_RemoveLast(GenericBuffer* buffer);

bool GenericBuffer_AddLastRange(GenericBuffer* buffer, void* items, size_t itemCount);

bool GenericBuffer_AddFirstRange(GenericBuffer* buffer, void* items, size_t itemCount);

bool GenericBuffer_InsertRange(GenericBuffer* buffer, void* items, size_t itemCount, size_t index);

bool GenericBuffer_ReplaceRange(GenericBuffer* buffer, void* items, size_t itemCount, size_t index);

bool GenericBuffer_RemoveRange(GenericBuffer* buffer, size_t startIndexInclusive, size_t endIndexExclusive);


void* GenericBuffer_GetPointerToElement(GenericBuffer* buffer, size_t index);

void* GenericBuffer_GetPointerToFirst(GenericBuffer* buffer);

void* GenericBuffer_GetPointerToLast(GenericBuffer* buffer);

bool GenericBuffer_GetAt(GenericBuffer* buffer, size_t index, void* out);

bool GenericBuffer_GetFirst(GenericBuffer* buffer, void* out);

bool GenericBuffer_GetLast(GenericBuffer* buffer, void* out);

bool GenericBuffer_Clear(GenericBuffer* buffer);


bool GenericBuffer_Contains(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);

size_t GenericBuffer_FirstIndexOf(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);

size_t GenericBuffer_LastIndexOf(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);

bool GenericBuffer_Reverse(GenericBuffer* buffer);


bool GenericBuffer_SortAscending(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData);

bool GenericBuffer_SortDescending(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData);

bool GenericBuffer_Filter(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);

bool GenericBuffer_Map(GenericBuffer* buffer, GenericBuffer* destination, GenericBufferMapper mapper, void* userData);

bool GenericBuffer_SumInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue);

bool GenericBuffer_SumDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue);

bool GenericBuffer_MaxInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue);

bool GenericBuffer_MaxDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue);

bool GenericBuffer_MinInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue);

bool GenericBuffer_MinDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue);

size_t GenericBuffer_CountWhere(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData);


bool GenericBuffer_AppendByte(GenericBuffer* buffer, unsigned char byte);

bool GenericBuffer_AppendRangeBytes(GenericBuffer* buffer, unsigned char* bytes, size_t size);

bool GenericBuffer_AppendString(GenericBuffer* buffer, const unsigned char* str);

bool GenericBuffer_NullTerminate(GenericBuffer* buffer);

bool GenericBuffer_SetByte(GenericBuffer* buffer, size_t index, unsigned char byte);

bool GenericBuffer_TryPrepareForManualMutation(GenericBuffer* buffer, size_t addedElementCount);



void* Memory_Allocate(size_t size);

void* Memory_Reallocate(void* ptr, size_t size);

void Memory_Free(void* ptr);

void Memory_Set(void* ptr, int8_t value, size_t size);

void Memory_Zero(void* ptr, size_t size);

bool Memory_IsEqual(const void* regionA, const void* regionB, size_t size);

void Memory_Copy(const void* source, void* destination, size_t size);

void Memory_Move(void* source, void* destination, size_t size);

size_t Memory_GetTotalAllocationCount(void);

size_t Memory_GetTotalReallocationCount(void);

size_t Memory_GetTotalFreeCount(void);

size_t Memory_GetCurrentAllocationCount(void);
