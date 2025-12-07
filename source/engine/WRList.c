#include "WRList.h"
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"
#include "WRMemory.h"
#include "WRComparasionResult.h"
#include "math.h"
#include "limits.h"


// Fields.
static const size_t LIST_CAPACITY_DEFAULT = 1;
static const size_t LIST_CAPACITY_GROWTH = 2;


// Static functions.
static Error CreateOutOfCapacityError(WRList* self, const unsigned char* operationName)
{
    return Error_Construct3(self->ErrorPool,
        ErrorCode_BufferTooSmall,
        u8"Internal element array (which is not resizable) has ran out of capacity. Capacity is %zu element(s) "
        u8"(operation \"%s\").",
        self->_buffer->_capacity, operationName);
}

static Error CreateIndexOutOfRangeError(WRList* self, size_t index, const unsigned char* operationName)
{
    return Error_Construct3(self->ErrorPool,
        ErrorCode_IndexOutOfBounds,
        u8"Index %zu is out of range for a list of size %zu (operation \"%s\").",
        index, self->_buffer->_count, operationName);
}

static Error CreateEmptyListError(WRList* self, const unsigned char* operationName)
{
    return Error_Construct3(self->ErrorPool,
        ErrorCode_InvalidOperation,
        u8"At least 1 list element is required to perform the operation \"%s\".",
        operationName);
}

static bool AllocateCallback(GenericBuffer* buffer, size_t requestedCapacity)
{
    WRList* Self = (WRList*)buffer->_userData;
    return WRList_EnsureCapacity(Self, requestedCapacity);
}

static inline void* GetUncheckedElementPtr(WRList* self, size_t index)
{
    return self->_buffer->_data + (index * self->_buffer->_elementSize);
}

static inline WRListElementData GetListElementData(WRList* self, size_t index)
{
    return (WRListElementData) { ._element = GetUncheckedElementPtr(self, index), ._elementIndex = index };
}

static ComparisonResult FormatComparisonResult(ComparisonResult result, int step)
{
    if ((step > 0) || (result == ComparisonResult_AEqualsB))
    {
        return result;
    }

    if (result == ComparisonResult_AGreaterThanB)
    {
        return ComparisonResult_ALessThanB;
    }
    return ComparisonResult_AGreaterThanB;
}

static void SortList(WRList* self,
    int32_t order,
    WRListComparator comparator,
    void* userData,
    int64_t minIndex,
    int64_t maxIndex)
{
    if (minIndex >= maxIndex)
    {
        return;
    }

    int64_t LeftIndex = minIndex + 1;
    int64_t RightIndex = maxIndex;
    int64_t PivotIndex = minIndex;

    while (LeftIndex <= RightIndex)
    {
        WRListElementData LeftEl = GetListElementData(self, (size_t)LeftIndex);
        WRListElementData RightEl = GetListElementData(self, (size_t)RightIndex);
        WRListElementData PivotEl = GetListElementData(self, (size_t)PivotIndex);

        ComparisonResult CompResult = FormatComparisonResult((*comparator)(self, LeftEl, PivotEl, userData), order);
        if (CompResult == ComparisonResult_ALessThanB)
        {
            LeftIndex++;
            continue;
        }
        
        CompResult = FormatComparisonResult((*comparator)(self, RightEl, PivotEl, userData), order);
        if (CompResult == ComparisonResult_AGreaterThanB)
        {
            RightIndex--;
            continue;
        }

        WRList_Swap(self, (size_t)LeftIndex, (size_t)RightIndex);
        LeftIndex++;
        RightIndex--;
    }

    WRList_Swap(self, (size_t)RightIndex, (size_t)PivotIndex);

    SortList(self, order, comparator, userData, minIndex, RightIndex - 1);
    SortList(self, order, comparator, userData, RightIndex + 1, maxIndex);
}

static Error CreateInvalidElementSizeError(ErrorMessagePool* errorPool)
{
    ErrorCode Code = ErrorCode_IllegalArgument;
    if (errorPool)
    {
        return Error_Construct3(errorPool,
            Code,
            u8"An element size of at least 1 is required to construct a list.");
    }
    return Error_Construct5(Code);
}



// Functions.

/* Constructors. */
Error WRList_Construct1(WRList* self, size_t elementSize, ErrorMessagePool* errorPool)
{
    if (elementSize == 0)
    {
        return CreateInvalidElementSizeError(errorPool);
    }

    Memory_Zero(self, sizeof(*self));
    self->_buffer->_elementSize = elementSize;
    self->ErrorPool = errorPool;

    return Error_CreateSuccess();
}

Error WRList_Construct2(WRList* self, size_t elementSize, size_t initialCapacity, ErrorMessagePool* errorPool)
{
    if (elementSize == 0)
    {
        return CreateInvalidElementSizeError(errorPool);
    }

    Memory_Zero(self, sizeof(*self));

    void* AllocatedMemory = (initialCapacity == 0) ? NULL : Memory_Allocate(elementSize * initialCapacity);
    self->_selfContainedData = GenericBuffer_CreateVariable(AllocatedMemory,
        initialCapacity, elementSize, 0, self, &AllocateCallback);
    self->_buffer = &self->_selfContainedData;

    self->ErrorPool = errorPool;
    return Error_CreateSuccess();
}

Error WRList_WrapBuffer(WRList* self, GenericBuffer* buffer, ErrorMessagePool* errorPool)
{
    Memory_Zero(self, sizeof(*self));
    self->_buffer = buffer;
    self->_flags = WRListFlags_IsBufferWrapper;
    self->ErrorPool = errorPool;
    return Error_CreateSuccess();
}

void WRList_Deconstruct1(WRList* self)
{
    if (self->_buffer->_data && !WRList_IsWrapperBuffer(self))
    {
        Memory_Free(self->_buffer->_data);
    }
}


/* Basic element manipulation. */
Error WRList_AddFirst(WRList* self, void* item)
{
    return WRList_Insert(self, item, 0);
}

Error WRList_AddLast(WRList* self, void* item)
{
    return WRList_Insert(self, item, self->_buffer->_count);
}

Error WRList_Insert(WRList* self, void* item, size_t index)
{
    GenericBuffer* Buffer = self->_buffer;
    if (index > Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, index, u8"insert");
    }
    if (!WRList_EnsureCapacity(self, Buffer->_count + 1))
    {
        return CreateOutOfCapacityError(self, u8"insert");
    }

    size_t ElementSize = Buffer->_elementSize;
    char* ItemDestination = ((char*)Buffer->_data) + (index * ElementSize);
    char* MoveDestination = ItemDestination + ElementSize;
    Memory_Move(ItemDestination, MoveDestination, ElementSize * (Buffer->_count - index));
    Memory_Copy(item, ItemDestination, ElementSize);
    
    Buffer->_count++;

    return Error_CreateSuccess();
}

Error WRList_AppendRange(WRList* self, void* items, size_t itemCount)
{
    return WRList_InsertRange(self, items, itemCount, self->_buffer->_count);
}

Error WRList_PrependRange(WRList* self, void* items, size_t itemCount)
{
    return WRList_InsertRange(self, items, itemCount, 0);
}

Error WRList_InsertRange(WRList* self, void* items, size_t itemCount, size_t startIndex)
{
    GenericBuffer* Buffer = self->_buffer;
    if (startIndex > Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, startIndex, u8"insert range");
    }
    if (itemCount == 0)
    {
        return Error_CreateSuccess();
    }
    if (!WRList_EnsureCapacity(self, Buffer->_count + itemCount))
    {
        return CreateOutOfCapacityError(self, u8"insert range");
    }

    size_t ElementSize = Buffer->_elementSize;
    char* ItemDestination = ((char*)Buffer->_data) + (startIndex * ElementSize);
    size_t ShiftedItemCount = Buffer->_count - startIndex;
    size_t ShiftedByteCount = ShiftedItemCount * ElementSize;
    char* MoveDestination = ItemDestination + (itemCount * ElementSize);
    Memory_Move(ItemDestination, MoveDestination, ShiftedByteCount);

    Memory_Copy(items, ItemDestination, itemCount * ElementSize);
    Buffer->_count += itemCount;

    return Error_CreateSuccess();
}

Error WRList_RemoveFirst(WRList* self)
{
    return WRList_RemoveAt(self, 0);
}

Error WRList_RemoveLast(WRList* self)
{
    if (self->_buffer->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0, u8"remove last");
    }
    return WRList_RemoveAt(self, self->_buffer->_count - 1);
}

Error WRList_RemoveAt(WRList* self, size_t index)
{
    GenericBuffer* Buffer = self->_buffer;
    if (index >= Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, index, u8"remove at");
    }

    size_t ElementSize = Buffer->_elementSize;
    size_t StartIndex = index + 1;
    char* MoveSource = ((char*)Buffer->_data) + (StartIndex * ElementSize);
    char* MoveDest = MoveSource - ElementSize;
    Memory_Move(MoveSource, MoveDest, (Buffer->_count - index) * ElementSize);

    Buffer->_count--;
    return Error_CreateSuccess();
}

Error WRList_RemoveRange(WRList* self, size_t startIndexInclusive, size_t endIndexExclusive)
{
    const unsigned char* ErrorContext = u8"remove range";
    if (startIndexInclusive > endIndexExclusive)
    {
        return Error_Construct3(self->ErrorPool,
            ErrorCode_IllegalArgument,
            u8"Min index %zu is greater than max index %zu.",
            startIndexInclusive, endIndexExclusive);
    }

    GenericBuffer* Buffer = self->_buffer;
    if (startIndexInclusive >= Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, startIndexInclusive, ErrorContext);
    }
    if (endIndexExclusive >= Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, startIndexInclusive, ErrorContext);
    }
    if (startIndexInclusive == endIndexExclusive)
    {
        return Error_CreateSuccess();
    }

    size_t RemovedElementCount = endIndexExclusive - startIndexInclusive;
    size_t MovedElementCount = Buffer->_count - endIndexExclusive;
    size_t MovedByteCount = MovedElementCount * Buffer->_elementSize;
    uint8_t* Destination = GetUncheckedElementPtr(self, startIndexInclusive);
    uint8_t* MoveSource = GetUncheckedElementPtr(self, endIndexExclusive);

    Memory_Move(MoveSource, Destination, MovedByteCount);

    Buffer->_count -= RemovedElementCount;
    return Error_CreateSuccess();
}

Error WRList_Replace(WRList* self, void* element, size_t index)
{
    GenericBuffer* Buffer = self->_buffer;
    if (index >= Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, index,  u8"replace");
    }

    size_t ElementSize = Buffer->_elementSize;
    Memory_Copy(element, ((char*)Buffer->_data) + (index * ElementSize), ElementSize);
    return Error_CreateSuccess();
}

void WRList_Clear(WRList* self)
{
    self->_buffer->_count = 0;
}

Error WRList_PopFirst(WRList* self, void* out)
{
    return WRList_PopAt(self, 0, out);
}

Error WRList_PopLast(WRList* self, void* out)
{
    if (self->_buffer->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0,  u8"pop last");
    }

    return WRList_PopAt(self, self->_buffer->_count - 1, out);
}

Error WRList_PopAt(WRList* self, size_t index, void* out)
{
    if (index >= self->_buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, index,  u8"pop at");
    }

    Error Result = WRList_GetAt(self, index, out);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return WRList_RemoveAt(self, index);
}

Error WRList_Swap(WRList* self, size_t indexA, size_t indexB)
{
    GenericBuffer* Buffer = self->_buffer;
    const unsigned char* OperationName = u8"swap";
    if (Buffer->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0, OperationName);
    }
    if (indexA >= Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, indexA, OperationName);
    }
    if (indexB >= Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, indexA, OperationName);
    }

    char* PtrA = GetUncheckedElementPtr(self, indexA);
    char* PtrB = GetUncheckedElementPtr(self, indexB);

    for (size_t ByteIndex = 0; ByteIndex < Buffer->_elementSize; ByteIndex++)
    {
        char ByteA = PtrA[ByteIndex];
        char ByteB = PtrB[ByteIndex];
        PtrA[ByteIndex] = ByteB;
        PtrB[ByteIndex] = ByteA;
    }

    return Error_CreateSuccess();
}


/* Info retrieval. */
Error WRList_GetFirst(WRList* self, void* out)
{
    return WRList_GetAt(self, 0, out);
}

Error WRList_GetLast(WRList* self, void* out)
{
    if (self->_buffer->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0, u8"get last");
    }
    return WRList_GetAt(self, self->_buffer->_count - 1, out);
}

Error WRList_GetAt(WRList* self, size_t index, void* out)
{
    GenericBuffer* Buffer = self->_buffer;
    if (index >= Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, index,  u8"get at");
    }

    Memory_Copy(((char*)Buffer->_data) + (index * Buffer->_elementSize), out, Buffer->_elementSize);
    return Error_CreateSuccess();
}

Error WRList_GetPointerToFirst(WRList* self, void** out)
{
    return WRList_GetPointerToElement(self, 0, out);
}

Error WRList_GetPointerToLast(WRList* self, void** out)
{
    if (self->_buffer->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0, u8"get pointer to last");
    }
    return WRList_GetPointerToElement(self, self->_buffer->_count - 1, out);
}

Error WRList_GetPointerToElement(WRList* self, size_t index, void** out)
{
    GenericBuffer* Buffer = self->_buffer;
    if (index >= Buffer->_count)
    {
        return CreateIndexOutOfRangeError(self, index,  u8"get pointer to");
    }

    *out = ((char*)Buffer->_data) + (index * Buffer->_elementSize);
    return Error_CreateSuccess();
}

bool WRList_Contains(WRList* self, WRListPredicate predicate, void* userData)
{
    size_t ElementCount = self->_buffer->_count;
    for (size_t i = 0; i < ElementCount; i++)
    {
        if ((*predicate)(self, GetListElementData(self, i), userData))
        {
            return true;
        }
    }
    return false;
}

bool WRList_FirstIndexOf(WRList* self, WRListPredicate predicate, void* userData, size_t* outIndex)
{
    *outIndex = 0;
    size_t ElementCount = self->_buffer->_count;
    for (size_t i = 0; i < ElementCount; i++)
    {
        if ((*predicate)(self, GetListElementData(self, i), userData))
        {
            *outIndex = i;
            return true;
        }
    }
    return false;
}

bool WRList_LastIndexOf(WRList* self, WRListPredicate predicate, void* userData, size_t* outIndex)
{
    *outIndex = 0;
    for (size_t i = self->_buffer->_count; i > 0; i--)
    {
        size_t RealIndex = i - 1;
        if ((*predicate)(self, GetListElementData(self, RealIndex), userData))
        {
            *outIndex = RealIndex;
            return true;
        }
    }
    return false;
}


/* Full list manipulation. */
void WRList_SortAscending(WRList* self, WRListComparator comparator, void* userData)
{
    if (self->_buffer->_count == 0)
    {
        return;
    }
    SortList(self, 1, comparator, userData, 0, (int64_t)self->_buffer->_count - 1);
}

void WRList_SortDescending(WRList* self, WRListComparator comparator, void* userData)
{
    if (self->_buffer->_count == 0)
    {
        return;
    }
    SortList(self, -1, comparator, userData, 0, (int64_t)self->_buffer->_count - 1);
}

void WRList_Filter(WRList* self, WRListPredicate predicate, void* userData)
{
    size_t RemovedElementCount = 0;
    size_t StartingElementCount = self->_buffer->_count;
    for (size_t i = 0; i < StartingElementCount; i++)
    {
        if (!(*predicate)(self, GetListElementData(self, i), userData))
        {
            RemovedElementCount++;
            continue;
        }
        if (RemovedElementCount == 0)
        {
            continue;
        }

        void* MoveSource = GetUncheckedElementPtr(self, i);
        void* MoveDestination = GetUncheckedElementPtr(self, i - RemovedElementCount);
        Memory_Copy(MoveSource, MoveDestination, self->_buffer->_elementSize);
    }
    self->_buffer->_count -= RemovedElementCount;
}

Error WRList_Map(WRList* self, WRList* destination, WRListMapper mapper, void* destElementBuffer, void* userData)
{
    if (!WRList_EnsureCapacity(destination, self->_buffer->_count))
    {
        return CreateOutOfCapacityError(destination, u8"map");
    }

    WRList_Clear(destination);

    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        (*mapper)(self, GetListElementData(self, i), destElementBuffer, userData);
        Error InsertResult = WRList_Insert(destination, destElementBuffer, i);
        if (InsertResult.Code != ErrorCode_Success)
        {
            return InsertResult;
        }
    }

    return Error_CreateSuccess();
}

Error WRList_MapToSelf(WRList* self, WRListMapper mapper, void* destElementBuffer, void* userData)
{
    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        (*mapper)(self, GetListElementData(self, i), destElementBuffer, userData);
        Error ReplaceResult = WRList_Replace(self, destElementBuffer, i);
        if (ReplaceResult.Code != ErrorCode_Success)
        {
            return ReplaceResult;
        }
    }

    return Error_CreateSuccess();
}

int64_t WRList_SumInt(WRList* self, WRListIntExtractor extractor, void* userData)
{
    int64_t Sum = 0;
    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        Sum += (*extractor)(self, GetListElementData(self, i), userData);
    }

    return Sum;
}

double WRList_SumDouble(WRList* self, WRListDoubleExtractor extractor, void* userData)
{
    double Sum = 0.0;
    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        Sum += (*extractor)(self, GetListElementData(self, i), userData);
    }

    return Sum;
}

Error WRList_MaxInt(WRList* self, WRListIntExtractor extractor, int64_t* outValue, void* userData)
{
    *outValue = 0;
    if (self->_buffer->_count == 0)
    {
        return CreateEmptyListError(self, u8"max int");
    }

    int64_t MaxValue = INT64_MIN;
    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        int64_t Value = (*extractor)(self, GetListElementData(self, i), userData);
        if (Value > MaxValue)
        {
            MaxValue = Value;
        }
    }

    *outValue = MaxValue;
    return Error_CreateSuccess();
}

Error WRList_MaxDouble(WRList* self, WRListDoubleExtractor extractor, double* outValue, void* userData)
{
    *outValue = 0.0;
    if (self->_buffer->_count == 0)
    {
        return CreateEmptyListError(self, u8"max double");
    }

    double MaxValue = -INFINITY;
    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        double Value = (*extractor)(self, GetListElementData(self, i), userData);
        if (Value > MaxValue)
        {
            MaxValue = Value;
        }
    }

    *outValue = MaxValue;
    return Error_CreateSuccess();
}

Error WRList_MinInt(WRList* self, WRListIntExtractor extractor, int64_t* outValue, void* userData)
{
    *outValue = 0;
    if (self->_buffer->_count == 0)
    {
        return CreateEmptyListError(self, u8"min int");
    }

    int64_t MinValue = INT64_MAX;
    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        int64_t Value = (*extractor)(self, GetListElementData(self, i), userData);
        if (Value < MinValue)
        {
            MinValue = Value;
        }
    }

    *outValue = MinValue;
    return Error_CreateSuccess();
}

Error WRList_MinDouble(WRList* self, WRListDoubleExtractor extractor, double* outValue, void* userData)
{
    *outValue = 0.0;
    if (self->_buffer->_count == 0)
    {
        return CreateEmptyListError(self, u8"min double");
    }

    double MinValue = INFINITY;
    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        double Value = (*extractor)(self, GetListElementData(self, i), userData);
        if (Value < MinValue)
        {
            MinValue = Value;
        }
    }

    *outValue = MinValue;
    return Error_CreateSuccess();
}

size_t WRList_CountWhere(WRList* self, WRListPredicate predicate, void* userData)
{
    size_t PassCount = 0;
    for (size_t i = 0; i < self->_buffer->_count; i++)
    {
        if ((*predicate)(self, GetListElementData(self, i), userData))
        {
            PassCount++;
        }
    }
    return PassCount;
}

void WRList_Reverse(WRList* self)
{
    size_t ElementCount = self->_buffer->_count;
    for (size_t ElIndex = 0; ElIndex < (ElementCount / 2); ElIndex++)
    {
        size_t IndexA = ElIndex;
        size_t IndexB = ElementCount - 1 - ElIndex;
        WRList_Swap(self, IndexA, IndexB);
    }
}


/* Technical. */
bool WRList_EnsureCapacity(WRList* self, size_t capacity)
{
    GenericBuffer* Buffer = self->_buffer;
    if (Buffer->_capacity >= capacity)
    {
        return true;
    }
    if (WRList_IsFixedCapacity(self))
    {
        return false;
    }

    size_t NewCapacity = Buffer->_capacity ? Buffer->_capacity : LIST_CAPACITY_DEFAULT;
    while (NewCapacity < capacity)
    {
        NewCapacity *= LIST_CAPACITY_GROWTH;
    }

    size_t NewSize = NewCapacity * Buffer->_elementSize;
    Buffer->_data = Buffer->_data ? Memory_Reallocate(Buffer->_data, NewSize) : Memory_Allocate(NewSize);
    Buffer->_capacity = NewCapacity;
    return true;
}

bool WRList_ReserveSpace(WRList* self, size_t extraElementCount)
{
    return WRList_EnsureCapacity(self, self->_buffer + extraElementCount);
}

bool WRList_IsFixedCapacity(WRList* self)
{
    return self->_buffer->_flags & GenericBufferFlags_FixedCapacity;
}

bool WRList_IsWrapperBuffer(WRList* self)
{
    return self->_flags & WRListFlags_IsBufferWrapper;
}

size_t WRList_GetCapacityRemaining(WRList* self)
{
    return GenericBuffer_GetCapacityRemaining(self->_buffer);
}


/* Buffers. */
/**
 * Creates a buffer which wraps the elements in this list.
 */
GenericBuffer* WRList_ToGenericBuffer(WRList* self)
{
    return self->_buffer;
}