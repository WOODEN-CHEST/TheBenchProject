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
    ErrorCode Code = ErrorCode_BufferTooSmall;
    if (self->ErrorPool)
    {
        return Error_Construct3(self->ErrorPool,
            Code,
            u8"Internal element array (which is not resizable) has ran out of capacity. Capacity is %zu element(s) "
            u8"(operation \"%s\").",
            self->_capacity, operationName);
    }
    return Error_Construct5(Code);
}

static Error CreateIndexOutOfRangeError(WRList* self, size_t index, const unsigned char* operationName)
{
    ErrorCode Code = ErrorCode_IndexOutOfBounds;
    if (self->ErrorPool)
    {
        return Error_Construct3(self->ErrorPool,
            Code,
            u8"Index %zu is out of range for a list of size %zu (operation \"%s\").",
            index, self->_count, operationName);
    }
    return Error_Construct5(Code);
}

static Error CreateEmptyListError(WRList* self, const unsigned char* operationName)
{
    ErrorCode Code = ErrorCode_InvalidOperation;
    if (self->ErrorPool)
    {
        return Error_Construct3(self->ErrorPool,
            Code,
            u8"At least 1 list element is required to perform the operation \"%s\".",
            operationName);
    }
    return Error_Construct5(Code);
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



// Functions.

/* Constructors. */
Error WRList_Construct1(WRList* self, size_t elementSize, ErrorMessagePool* errorPool)
{
    Memory_Zero(self, sizeof(*self));
    self->_elementSize = elementSize;
    self->ErrorPool = errorPool;
}

Error WRList_Construct2(WRList* self, size_t elementSize, size_t initialCapacity, ErrorMessagePool* errorPool)
{
    Memory_Zero(self, sizeof(*self));
    self->_elementSize = elementSize;
    self->ErrorPool = errorPool;
    WRList_EnsureCapacity(self, initialCapacity);
}

void WRList_WrapConstantBuffer(WRList* self, void* buffer, size_t count, size_t capacity, size_t elementSize, ErrorMessagePool* errorPool)
{
    Memory_Zero(self, sizeof(*self));
    self->_data = buffer;
    self->_count = count;
    self->_capacity = capacity;
    self->_elementSize = elementSize;
    self->_flags = WRListFlags_IsBufferWrapper;
    self->ErrorPool = errorPool;
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
    if (index < self->_count)
    {
        uint8_t* MoveDestination = ItemDestination + self->_elementSize;
        Memory_Move(ItemDestination, MoveDestination, self->_elementSize);
    }
    Memory_Copy(item, ItemDestination, self->_elementSize);
    
    self->_count++;

    return Error_CreateSuccess();
}

Error WRList_AddRange(WRList* self, void** items, size_t itemCount)
{
    return WRList_InsertRange(self, items, itemCount, self->_count);
}

Error WRList_InsertRange(WRList* self, void** items, size_t itemCount, size_t startIndex)
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

    uint8_t* ItemDestination = self->_data + (self->_count * self->_elementSize);
    if (startIndex < self->_count)
    {
        uint8_t* MoveDestination = ItemDestination + (itemCount * self->_elementSize);
        Memory_Move(ItemDestination, MoveDestination, self->_elementSize * (self->_count - startIndex));
    }

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

    uint8_t* MoveSource = self->_data + (index * self->_elementSize);
    uint8_t* MoveDest = MoveSource - self->_elementSize;
    Memory_Move(MoveSource, MoveDest, self->_elementSize);
    self->_count--;
    return Error_CreateSuccess();
}

Error WRList_RemoveRange(WRList* self, size_t startIndesInclusive, size_t endIndexExclusive);

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

bool WRList_Contains(WRList* self, WRListPredicate* predicate, void* userData)
{
    for (size_t i = 0; i < self->_count; i++)
    {
        void* ElementPtr = self->_data + (i * self->_elementSize);
        if ((*predicate)(self, ElementPtr, userData))
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
        void* ElementPtr = self->_data + (i * self->_elementSize);
        if ((*predicate)(self, ElementPtr, userData))
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
    for (size_t i = self->_count; i > 0; i++)
    {
        size_t RealIndex = i - 1;
        if ((*predicate)(self, GetUncheckedElementPtr(self, RealIndex), userData))
        {
            *outIndex = RealIndex;
            return true;
        }
    }
    return false;
}


/* Full list manipulation. */
void WRList_SortAscending(WRList* self, WRListComparator comparator, void* userData);

void WRList_SortDescending(WRList* self, WRListComparator comparator, void* userData);

void WRList_Filter(WRList* self, WRListPredicate predicate, void* userData);

void WRList_Map(WRList* self, WRList* destination, WRListMapper mapper, void* destElementBuffer, void* userData);

void WRList_MapToSelf(WRList* self, WRListMapper mapper, void* destElementBuffer, void* userData);

Error WRList_SumInt(WRList* self, WRListIntExtractor extractor, int64_t* outValue, void* userData)
{
    *outValue = 0;
    if (self->_count == 0)
    {
        return CreateEmptyListError(self, u8"sum int");
    }

    int64_t Sum = 0;
    for (size_t i = 0; i < self->_count; i++)
    {
        Sum += (*extractor)(self, GetUncheckedElementPtr(self, i), userData);
    }

    *outValue = Sum;
    return Error_CreateSuccess();
}

Error WRList_SumDouble(WRList* self, WRListDoubleExtractor extractor, double* outValue, void* userData)
{
    *outValue = 0.0;
    if (self->_count == 0)
    {
        return CreateEmptyListError(self, u8"sum double");
    }

    double Sum = 0.0;
    for (size_t i = 0; i < self->_count; i++)
    {
        Sum += (*extractor)(self, GetUncheckedElementPtr(self, i), userData);
    }

    *outValue = Sum;
    return Error_CreateSuccess();
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
        int64_t Value = (*extractor)(self, GetUncheckedElementPtr(self, i), userData);
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
        double Value = (*extractor)(self, GetUncheckedElementPtr(self, i), userData);
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
        int64_t Value = (*extractor)(self, GetUncheckedElementPtr(self, i), userData);
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
        double Value = (*extractor)(self, GetUncheckedElementPtr(self, i), userData);
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
        if ((*predicate)(self, GetUncheckedElementPtr(self, i), userData))
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
        uint8_t* PtrA = self->_data + (IndexA * self->_elementSize);
        uint8_t* PtrB = self->_data + (IndexB * self->_elementSize);

        for (size_t ByteIndex = 0; ByteIndex < self->_elementSize; ByteIndex++)
        {
            uint8_t ByteA = PtrA[ByteIndex];
            uint8_t ByteB = PtrB[ByteIndex];
            PtrA[ByteIndex] = ByteB;
            PtrB[ByteIndex] = ByteA;
        }
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
    return self->_flags & WRListFlags_IsBufferWrapper;
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
    return GenericBuffer_CreateVariable(self->_data,
        self->_capacity,
        self->_elementSize,
        self->_count,
        self,
        &AllocateCallback);
}