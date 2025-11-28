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
        self->_capacity, operationName);
}

static Error CreateIndexOutOfRangeError(WRList* self, size_t index, const unsigned char* operationName)
{
    return Error_Construct3(self->ErrorPool,
        ErrorCode_IndexOutOfBounds,
        u8"Index %zu is out of range for a list of size %zu (operation \"%s\").",
        index, self->_count, operationName);
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
    if (WRList_EnsureCapacity(Self, requestedCapacity))
    {
        buffer->_capacity = Self->_capacity;
        return true;
    }
    return false;
}

static inline void* GetUncheckedElementPtr(WRList* self, size_t index)
{
    return self->_data + (index * self->_elementSize);
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
        WRListElementData LeftEl = GetListElementData(self, LeftIndex);
        WRListElementData RightEl = GetListElementData(self, RightIndex);
        WRListElementData PivotEl = GetListElementData(self, PivotIndex);

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

        WRList_Swap(self, LeftIndex, RightIndex);
        LeftIndex++;
        RightIndex--;
    }

    WRList_Swap(self, RightIndex, PivotIndex);

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
    self->_elementSize = elementSize;
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
    self->_elementSize = elementSize;
    self->ErrorPool = errorPool;
    WRList_EnsureCapacity(self, initialCapacity);

    return Error_CreateSuccess();
}

Error WRList_WrapConstantBuffer(WRList* self, void* buffer, size_t count, size_t capacity, size_t elementSize, ErrorMessagePool* errorPool)
{
    if (elementSize == 0)
    {
        return CreateInvalidElementSizeError(errorPool);
    }

    Memory_Zero(self, sizeof(*self));
    self->_data = buffer;
    self->_count = count;
    self->_capacity = capacity;
    self->_elementSize = elementSize;
    self->_flags = WRListFlags_IsBufferWrapper;
    self->ErrorPool = errorPool;

    return Error_CreateSuccess();
}

void WRList_Deconstruct1(WRList* self)
{
    if (self->_data && !WRList_IsWrapperBuffer(self))
    {
        Memory_Free(self->_data);
    }
}


/* Basic element manipulation. */
Error WRList_AddFirst(WRList* self, void* item)
{
    return WRList_Insert(self, item, 0);
}

Error WRList_AddLast(WRList* self, void* item)
{
    return WRList_Insert(self, item, self->_count);
}

Error WRList_Insert(WRList* self, void* item, size_t index)
{
    if (index > self->_count)
    {
        return CreateIndexOutOfRangeError(self, index, u8"insert");
    }
    if (!WRList_EnsureCapacity(self, self->_count + 1))
    {
        return CreateOutOfCapacityError(self, u8"insert");
    }

    uint8_t* ItemDestination = self->_data + (index * self->_elementSize);
    uint8_t* MoveDestination = ItemDestination + self->_elementSize;
    Memory_Move(ItemDestination, MoveDestination, self->_elementSize * (self->_count - index));
    Memory_Copy(item, ItemDestination, self->_elementSize);
    
    self->_count++;

    return Error_CreateSuccess();
}

Error WRList_AppendRange(WRList* self, void* items, size_t itemCount)
{
    return WRList_InsertRange(self, items, itemCount, self->_count);
}

Error WRList_PrependRange(WRList* self, void* items, size_t itemCount)
{
    return WRList_InsertRange(self, items, itemCount, 0);
}

Error WRList_InsertRange(WRList* self, void* items, size_t itemCount, size_t startIndex)
{
    if (startIndex > self->_count)
    {
        return CreateIndexOutOfRangeError(self, startIndex, u8"insert range");
    }
    if (itemCount == 0)
    {
        return Error_CreateSuccess();
    }
    if (!WRList_EnsureCapacity(self, self->_count + itemCount))
    {
        return CreateOutOfCapacityError(self, u8"insert range");
    }

    uint8_t* ItemDestination = self->_data + (startIndex * self->_elementSize);
    size_t ShiftedItemCount = self->_count - startIndex;
    size_t ShiftedByteCount = ShiftedItemCount * self->_elementSize;
    uint8_t* MoveDestination = ItemDestination + (itemCount * self->_elementSize);
    Memory_Move(ItemDestination, MoveDestination, ShiftedByteCount);

    Memory_Copy(items, ItemDestination, itemCount * self->_elementSize);
    self->_count += itemCount;

    return Error_CreateSuccess();
}

Error WRList_RemoveFirst(WRList* self)
{
    return WRList_RemoveAt(self, 0);
}

Error WRList_RemoveLast(WRList* self)
{
    if (self->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0, u8"remove last");
    }
    return WRList_RemoveAt(self, self->_count - 1);
}

Error WRList_RemoveAt(WRList* self, size_t index)
{
    if (index >= self->_count)
    {
        return CreateIndexOutOfRangeError(self, index, u8"remove at");
    }

    size_t StartIndex = index + 1;
    uint8_t* MoveSource = self->_data + (StartIndex * self->_elementSize);
    uint8_t* MoveDest = MoveSource - self->_elementSize;
    Memory_Move(MoveSource, MoveDest, (self->_count - index) * self->_elementSize);

    self->_count--;
    return Error_CreateSuccess();
}

Error WRList_RemoveRange(WRList* self, size_t startIndexInclusive, size_t endIndexExclusive)
{
    const unsigned char* ErrorContext = u8"remove range";
    if (startIndexInclusive > endIndexExclusive)
    {
        return Error_Construct3(self->ErrorPool,
            ErrorCode_IllegalArgument,
            u8"Min index %zu is greater than max index %zu",
            startIndexInclusive, endIndexExclusive);
    }
    if (startIndexInclusive >= self->_count)
    {
        return CreateIndexOutOfRangeError(self, startIndexInclusive, ErrorContext);
    }
    if (endIndexExclusive >= self->_count)
    {
        return CreateIndexOutOfRangeError(self, startIndexInclusive, ErrorContext);
    }
    if (startIndexInclusive == endIndexExclusive)
    {
        return Error_CreateSuccess();
    }

    size_t RemovedElementCount = endIndexExclusive - startIndexInclusive;
    size_t MovedElementCount = self->_count - endIndexExclusive;
    size_t MovedByteCount = MovedElementCount * self->_elementSize;
    uint8_t* Destination = GetUncheckedElementPtr(self, startIndexInclusive);
    uint8_t* MoveSource = GetUncheckedElementPtr(self, endIndexExclusive);

    Memory_Move(MoveSource, Destination, MovedByteCount);

    self->_count -= RemovedElementCount;
    return Error_CreateSuccess();
}

Error WRList_Replace(WRList* self, void* element, size_t index)
{
    if (index >= self->_count)
    {
        return CreateIndexOutOfRangeError(self, index,  u8"replace");
    }

    Memory_Copy(element, self->_data + (index * self->_elementSize), self->_elementSize);
    return Error_CreateSuccess();
}

void WRList_Clear(WRList* self)
{
    self->_count = 0;
}

Error WRList_PopFirst(WRList* self, void* out)
{
    return WRList_PopAt(self, 0, out);
}

Error WRList_PopLast(WRList* self, void* out)
{
    if (self->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0,  u8"pop last");
    }

    return WRList_PopAt(self, self->_count - 1, out);
}

Error WRList_PopAt(WRList* self, size_t index, void* out)
{
    if (index >= self->_count)
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
    const unsigned char* OperationName = u8"swap";
    if (self->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0, OperationName);
    }
    if (indexA >= self->_count)
    {
        return CreateIndexOutOfRangeError(self, indexA, OperationName);
    }
    if (indexB >= self->_count)
    {
        return CreateIndexOutOfRangeError(self, indexA, OperationName);
    }

    uint8_t* PtrA = GetUncheckedElementPtr(self, indexA);
    uint8_t* PtrB = GetUncheckedElementPtr(self, indexB);

    for (size_t ByteIndex = 0; ByteIndex < self->_elementSize; ByteIndex++)
    {
        uint8_t ByteA = PtrA[ByteIndex];
        uint8_t ByteB = PtrB[ByteIndex];
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
    if (self->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0, u8"get last");
    }
    return WRList_GetAt(self, self->_count - 1, out);
}

Error WRList_GetAt(WRList* self, size_t index, void* out)
{
    if (index >= self->_count)
    {
        return CreateIndexOutOfRangeError(self, index,  u8"get at");
    }

    Memory_Copy(self->_data + (index * self->_elementSize), out, self->_elementSize);
    return Error_CreateSuccess();
}

Error WRList_GetPointerToFirst(WRList* self, void** out)
{
    return WRList_GetPointerToElement(self, 0, out);
}

Error WRList_GetPointerToLast(WRList* self, void** out)
{
    if (self->_count == 0)
    {
        return CreateIndexOutOfRangeError(self, 0, u8"get pointer to last");
    }
    return WRList_GetPointerToElement(self, self->_count - 1, out);
}

Error WRList_GetPointerToElement(WRList* self, size_t index, void** out)
{
    if (index >= self->_count)
    {
        return CreateIndexOutOfRangeError(self, index,  u8"get pointer to");
    }

    *out = self->_data + (index * self->_elementSize);
    return Error_CreateSuccess();
}

bool WRList_Contains(WRList* self, WRListPredicate predicate, void* userData)
{
    for (size_t i = 0; i < self->_count; i++)
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
    for (size_t i = 0; i < self->_count; i++)
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
    for (size_t i = self->_count; i > 0; i--)
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
    if (self->_count == 0)
    {
        return;
    }
    SortList(self, 1, comparator, userData, 0, (int64_t)self->_count - 1);
}

void WRList_SortDescending(WRList* self, WRListComparator comparator, void* userData)
{
    if (self->_count == 0)
    {
        return;
    }
    SortList(self, -1, comparator, userData, 0, (int64_t)self->_count - 1);
}

void WRList_Filter(WRList* self, WRListPredicate predicate, void* userData)
{
    size_t RemovedElementCount = 0;
    for (size_t i = 0; i < self->_count; i++)
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
        Memory_Copy(MoveSource, MoveDestination, self->_elementSize);
    }
    self->_count -= RemovedElementCount;
}

Error WRList_Map(WRList* self, WRList* destination, WRListMapper mapper, void* destElementBuffer, void* userData)
{
    if (!WRList_EnsureCapacity(destination, self->_count))
    {
        return CreateOutOfCapacityError(destination, u8"map");
    }

    WRList_Clear(destination);

    for (size_t i = 0; i < self->_count; i++)
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
    for (size_t i = 0; i < self->_count; i++)
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
    for (size_t i = 0; i < self->_count; i++)
    {
        Sum += (*extractor)(self, GetListElementData(self, i), userData);
    }

    return Sum;
}

double WRList_SumDouble(WRList* self, WRListDoubleExtractor extractor, void* userData)
{
    double Sum = 0.0;
    for (size_t i = 0; i < self->_count; i++)
    {
        Sum += (*extractor)(self, GetListElementData(self, i), userData);
    }

    return Sum;
}

Error WRList_MaxInt(WRList* self, WRListIntExtractor extractor, int64_t* outValue, void* userData)
{
    *outValue = 0;
    if (self->_count == 0)
    {
        return CreateEmptyListError(self, u8"max int");
    }

    int64_t MaxValue = INT64_MIN;
    for (size_t i = 0; i < self->_count; i++)
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
    if (self->_count == 0)
    {
        return CreateEmptyListError(self, u8"max double");
    }

    double MaxValue = -INFINITY;
    for (size_t i = 0; i < self->_count; i++)
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
    if (self->_count == 0)
    {
        return CreateEmptyListError(self, u8"min int");
    }

    int64_t MinValue = INT64_MAX;
    for (size_t i = 0; i < self->_count; i++)
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
    if (self->_count == 0)
    {
        return CreateEmptyListError(self, u8"min double");
    }

    double MinValue = INFINITY;
    for (size_t i = 0; i < self->_count; i++)
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

size_t WRList_CountWhere(WRList* self, WRListPredicate* predicate, void* userData)
{
    size_t PassCount = 0;
    for (size_t i = 0; i < self->_count; i++)
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
    for (size_t ElIndex = 0; ElIndex < self->_count / 2; ElIndex++)
    {
        size_t IndexA = ElIndex;
        size_t IndexB = self->_count - 1 - ElIndex;
        WRList_Swap(self, IndexA, IndexB);
    }
}


/* Technical. */
bool WRList_EnsureCapacity(WRList* self, size_t capacity)
{
    if (self->_capacity >= capacity)
    {
        return true;
    }
    if (WRList_IsFixedCapacity(self))
    {
        return false;
    }

    size_t NewCapacity = self->_capacity ? self->_capacity : LIST_CAPACITY_DEFAULT;
    while (NewCapacity < capacity)
    {
        NewCapacity *= LIST_CAPACITY_GROWTH;
    }

    size_t NewSize = NewCapacity * self->_elementSize;
    self->_data = self->_data ? Memory_Reallocate(self->_data, NewSize) : Memory_Allocate(NewSize);
    self->_capacity = NewCapacity;
    return true;
}

bool WRList_ReserveSpace(WRList* self, size_t extraElementCount)
{
    return WRList_EnsureCapacity(self, self->_count + extraElementCount);
}

bool WRList_IsFixedCapacity(WRList* self)
{
    return WRList_IsWrapperBuffer(self);
}

bool WRList_IsWrapperBuffer(WRList* self)
{
    return self->_flags & WRListFlags_IsBufferWrapper;
}

size_t WRList_GetCapacityRemaining(WRList* self)
{
    return self->_capacity - self->_count;
}


/* Buffers. */
GenericBuffer WRList_ToConstantBuffer(WRList* self)
{
    return GenericBuffer_CreateConstant(self->_data, self->_capacity, self->_elementSize, self->_count);
}

GenericBuffer WRList_ToDynamicBuffer(WRList* self)
{
    if (WRList_IsFixedCapacity(self))
    {
        return WRList_ToConstantBuffer(self);
    }

    return GenericBuffer_CreateVariable(self->_data,
        self->_capacity,
        self->_elementSize,
        self->_count,
        self,
        &AllocateCallback);
}