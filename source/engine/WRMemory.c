#include "WRMemory.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>


// Macros.
#define GENERIC_BUFFER_SORT_ASCENDING (1)
#define GENERIC_BUFFER_SORT_DESCENDING (-1)
#define DEFAULT_GROWABLE_BUFFER_CAPACITY_MULTIPLIER 2


// Types.
typedef struct GenericBufferSortContextStruct
{
    GenericBuffer* Buffer;
    GenericBufferComparator Comparator;
    void* UserData;
    int Direction;
} GenericBufferSortContext;


// Fields.
static atomic_size_t TotalAllocations = 0;
static atomic_size_t TotalReallocations = 0;
static atomic_size_t TotalFrees = 0;
static atomic_size_t CurrentAllocations = 0;


// Static functions.
static inline size_t GetMinimumAllocationSize(size_t size)
{
    return (size == 0) ? 1 : size;
}

static inline unsigned char* GenericBuffer_GetElementAddress(GenericBuffer* buffer, size_t index)
{
    return buffer->_data + (index * buffer->_elementSize);
}

static inline GenericBufferElementData CreateGenericBufferElementData(GenericBuffer* buffer, size_t index)
{
    return (GenericBufferElementData)
    {
        ._element = GenericBuffer_GetElementAddress(buffer, index),
        ._index = index,
    };
}

static inline bool GenericBuffer_CanModify(GenericBuffer* buffer)
{
    return !GenericBuffer_IsReadOnly(buffer);
}

static inline bool GenericBuffer_IsByteBuffer(GenericBuffer* buffer)
{
    return (buffer != NULL) && (buffer->_elementSize == sizeof(unsigned char));
}

static inline size_t GenericBuffer_GetStringLength(const unsigned char* str)
{
    if (str == NULL)
    {
        return 0;
    }

    return strlen((char*)str);
}

static inline bool GenericBuffer_IsIndexValid(GenericBuffer* buffer, size_t index)
{
    return (buffer != NULL) && (index < buffer->_count);
}

static bool GenericBuffer_DefaultAllocateVariableCallback(GenericBuffer* destination, size_t requestedCapacity)
{
    size_t AllocationSize = 0;

    if (destination == NULL)
    {
        return false;
    }
    if (destination->_elementSize == 0)
    {
        return false;
    }

    size_t NewCapacity = (destination->_capacity == 0) ? 1 : destination->_capacity;
    while (NewCapacity < requestedCapacity)
    {
        NewCapacity *= DEFAULT_GROWABLE_BUFFER_CAPACITY_MULTIPLIER;
    }
    if (NewCapacity > (SIZE_MAX / destination->_elementSize))
    {
        return false;
    }

    AllocationSize = NewCapacity * destination->_elementSize;
    destination->_data = Memory_Reallocate(destination->_data, AllocationSize);
    destination->_capacity = NewCapacity;
    return true;
}

static inline void CreateGenericBuffer(GenericBuffer* buffer,
    void* destination,
    size_t bufferCapacity,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback,
    GenericBufferFlags flags)
{
    buffer->_data = destination;
    buffer->_capacity = bufferCapacity;
    buffer->_elementSize = elementSize;
    buffer->_count = elementCount;
    buffer->_requestMoreSpaceCallback = callback;
    buffer->_userData = userData;
    buffer->_flags = flags;
}

static inline bool GenericBuffer_CopyElements(GenericBuffer* buffer, size_t destinationIndex, const void* items, size_t itemCount)
{
    size_t ByteCount = itemCount * buffer->_elementSize;

    if (ByteCount == 0)
    {
        return true;
    }

    Memory_Copy(items, GenericBuffer_GetElementAddress(buffer, destinationIndex), ByteCount);
    return true;
}

static inline void GenericBuffer_SwapElements(GenericBuffer* buffer, size_t indexA, size_t indexB, unsigned char* temp)
{
    unsigned char* ElementA = GenericBuffer_GetElementAddress(buffer, indexA);
    unsigned char* ElementB = GenericBuffer_GetElementAddress(buffer, indexB);

    Memory_Copy(ElementA, temp, buffer->_elementSize);
    Memory_Copy(ElementB, ElementA, buffer->_elementSize);
    Memory_Copy(temp, ElementB, buffer->_elementSize);
}

static inline bool GenericBuffer_ShouldSwapForSort(ComparisonResult result, int direction)
{
    return (((direction == GENERIC_BUFFER_SORT_ASCENDING) && (result == ComparisonResult_AGreaterThanB))
        || ((direction == GENERIC_BUFFER_SORT_DESCENDING) && (result == ComparisonResult_ALessThanB)));
}

static size_t GenericBuffer_Partition(GenericBufferSortContext* context,
    size_t lowIndex,
    size_t highIndex,
    unsigned char* pivotBuffer,
    unsigned char* swapBuffer)
{
    size_t PartitionIndex = lowIndex;

    Memory_Copy(GenericBuffer_GetElementAddress(context->Buffer, highIndex), pivotBuffer, context->Buffer->_elementSize);

    for (size_t CurrentIndex = lowIndex; CurrentIndex < highIndex; CurrentIndex++)
    {
        GenericBufferElementData CurrentElement = CreateGenericBufferElementData(context->Buffer, CurrentIndex);
        GenericBufferElementData PivotElement = (GenericBufferElementData)
        {
            ._element = pivotBuffer,
            ._index = highIndex,
        };
        ComparisonResult Result = context->Comparator(context->Buffer, CurrentElement, PivotElement, context->UserData);

        if (!GenericBuffer_ShouldSwapForSort(Result, context->Direction))
        {
            if (PartitionIndex != CurrentIndex)
            {
                GenericBuffer_SwapElements(context->Buffer, PartitionIndex, CurrentIndex, swapBuffer);
            }

            PartitionIndex++;
        }
    }

    if (PartitionIndex != highIndex)
    {
        GenericBuffer_SwapElements(context->Buffer, PartitionIndex, highIndex, swapBuffer);
    }

    return PartitionIndex;
}

static void GenericBuffer_QuickSort(GenericBufferSortContext* context,
    size_t lowIndex,
    size_t highIndex,
    unsigned char* pivotBuffer,
    unsigned char* swapBuffer)
{
    size_t PivotIndex = 0;

    if (lowIndex >= highIndex)
    {
        return;
    }

    PivotIndex = GenericBuffer_Partition(context, lowIndex, highIndex, pivotBuffer, swapBuffer);

    if (PivotIndex > lowIndex)
    {
        GenericBuffer_QuickSort(context, lowIndex, PivotIndex - 1, pivotBuffer, swapBuffer);
    }
    if (PivotIndex < highIndex)
    {
        GenericBuffer_QuickSort(context, PivotIndex + 1, highIndex, pivotBuffer, swapBuffer);
    }
}

static bool GenericBuffer_SortInternal(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData, int direction)
{
    GenericBufferSortContext Context;
    unsigned char* ScratchBuffer = NULL;
    unsigned char* PivotBuffer = NULL;
    unsigned char* SwapBuffer = NULL;

    if ((buffer == NULL) || (comparator == NULL))
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }
    if (buffer->_count < 2)
    {
        return true;
    }

    ScratchBuffer = Memory_Allocate(buffer->_elementSize * 2);
    PivotBuffer = ScratchBuffer;
    SwapBuffer = ScratchBuffer + buffer->_elementSize;
    Context = (GenericBufferSortContext)
    {
        .Buffer = buffer,
        .Comparator = comparator,
        .UserData = userData,
        .Direction = direction,
    };

    GenericBuffer_QuickSort(&Context, 0, buffer->_count - 1, PivotBuffer, SwapBuffer);

    Memory_Free(ScratchBuffer);
    return true;
}


// Public functions.
void* Memory_Allocate(size_t size)
{
    void* Block = malloc(GetMinimumAllocationSize(size));

    if (Block == NULL)
    {
        abort();
    }

    atomic_fetch_add_explicit(&TotalAllocations, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&CurrentAllocations, 1, memory_order_relaxed);
    return Block;
}

void* Memory_Reallocate(void* ptr, size_t size)
{
    void* Block = realloc(ptr, GetMinimumAllocationSize(size));

    if (Block == NULL)
    {
        abort();
    }

    atomic_fetch_add_explicit(&TotalReallocations, 1, memory_order_relaxed);

    if (ptr == NULL)
    {
        atomic_fetch_add_explicit(&TotalAllocations, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&CurrentAllocations, 1, memory_order_relaxed);
    }

    return Block;
}

void Memory_Free(void* ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    free(ptr);
    atomic_fetch_add_explicit(&TotalFrees, 1, memory_order_relaxed);
    atomic_fetch_sub_explicit(&CurrentAllocations, 1, memory_order_relaxed);
}

void Memory_Set(void* ptr, int8_t value, size_t size)
{
    memset(ptr, value, size);
}

void Memory_Zero(void* ptr, size_t size)
{
    memset(ptr, 0, size);
}

bool Memory_IsEqual(const void* regionA, const void* regionB, size_t size)
{
    return (memcmp(regionA, regionB, size) == 0);
}

void Memory_Copy(const void* source, void* destination, size_t size)
{
    memcpy(destination, source, size);
}

void Memory_Move(void* source, void* destination, size_t size)
{
    memmove(destination, source, size);
}

void GenericBuffer_CreateVariable(GenericBuffer* buffer,
    void* destination,
    size_t bufferCapacity,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback)
{
    CreateGenericBuffer(buffer,
        destination,
        bufferCapacity,
        elementSize,
        elementCount,
        userData,
        callback,
        GenericBufferFlags_None);
}

void GenericBuffer_AllocateVariable(GenericBuffer* buffer, size_t initialCapacity, size_t elementSize)
{
    void* Destination = NULL;

    if ((initialCapacity > 0) && (elementSize > 0))
    {
        Destination = Memory_Allocate(initialCapacity * elementSize);
    }

    CreateGenericBuffer(buffer,
        Destination,
        initialCapacity,
        elementSize,
        0,
        NULL,
        GenericBuffer_DefaultAllocateVariableCallback,
        GenericBufferFlags_None);
}

void GenericBuffer_CreateConstant(GenericBuffer* buffer,
    void* destination,
    size_t bufferCapacity,
    size_t elementSize,
    size_t elementCount)
{
    CreateGenericBuffer(buffer,
        destination,
        bufferCapacity,
        elementSize,
        elementCount,
        NULL,
        NULL,
        GenericBufferFlags_FixedCapacity);
}

void GenericBuffer_SetCallback(GenericBuffer* buffer, GenericBufferAllocateCallback callback, void* userData)
{
    buffer->_userData = userData;
    buffer->_requestMoreSpaceCallback = callback;
}

void GenericBuffer_ClearCallback(GenericBuffer* buffer)
{
    buffer->_requestMoreSpaceCallback = NULL;
    buffer->_userData = NULL;
}

bool GenericBuffer_EnsureTotalCapacity(GenericBuffer* buffer, size_t capacity)
{
    bool WasMemoryAllocated = false;

    if (buffer == NULL)
    {
        return false;
    }
    if (buffer->_capacity >= capacity)
    {
        return true;
    }
    if (GenericBuffer_IsFixedCapacity(buffer))
    {
        return false;
    }
    if (buffer->_requestMoreSpaceCallback == NULL)
    {
        return false;
    }

    WasMemoryAllocated = (*buffer->_requestMoreSpaceCallback)(buffer, capacity);
    if (!WasMemoryAllocated)
    {
        return false;
    }

    return (buffer->_capacity >= capacity);
}

bool GenericBuffer_ReserveMoreCapacity(GenericBuffer* buffer, size_t requiredSize)
{
    if (buffer == NULL)
    {
        return false;
    }

    return GenericBuffer_EnsureTotalCapacity(buffer, buffer->_count + requiredSize);
}

size_t GenericBuffer_GetCapacityRemaining(GenericBuffer* buffer)
{
    if (buffer == NULL)
    {
        return 0;
    }

    return buffer->_capacity - buffer->_count;
}

bool GenericBuffer_AddLast(GenericBuffer* buffer, void* item)
{
    if (buffer == NULL)
    {
        return false;
    }

    return GenericBuffer_Insert(buffer, item, buffer->_count);
}

bool GenericBuffer_AddFirst(GenericBuffer* buffer, void* item)
{
    return GenericBuffer_Insert(buffer, item, 0);
}

bool GenericBuffer_Insert(GenericBuffer* buffer, void* item, size_t index)
{
    size_t MovedByteCount = 0;
    unsigned char* MoveSource = NULL;
    unsigned char* MoveDestination = NULL;

    if ((buffer == NULL) || (item == NULL))
    {
        return false;
    }
    if (index > buffer->_count)
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }
    if (!GenericBuffer_ReserveMoreCapacity(buffer, 1))
    {
        return false;
    }

    MovedByteCount = (buffer->_count - index) * buffer->_elementSize;
    MoveSource = GenericBuffer_GetElementAddress(buffer, index);
    MoveDestination = MoveSource + buffer->_elementSize;

    Memory_Move(MoveSource, MoveDestination, MovedByteCount);
    Memory_Copy(item, MoveSource, buffer->_elementSize);
    buffer->_count++;
    return true;
}

bool GenericBuffer_Replace(GenericBuffer* buffer, void* item, size_t index)
{
    if ((buffer == NULL) || (item == NULL))
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }
    if (!GenericBuffer_IsIndexValid(buffer, index))
    {
        return false;
    }

    Memory_Copy(item, GenericBuffer_GetElementAddress(buffer, index), buffer->_elementSize);
    return true;
}

bool GenericBuffer_RemoveAt(GenericBuffer* buffer, size_t index)
{
    if ((buffer == NULL) || !GenericBuffer_IsIndexValid(buffer, index))
    {
        return false;
    }

    return GenericBuffer_RemoveRange(buffer, index, index + 1);
}

bool GenericBuffer_RemoveFirst(GenericBuffer* buffer)
{
    return GenericBuffer_RemoveAt(buffer, 0);
}

bool GenericBuffer_RemoveLast(GenericBuffer* buffer)
{
    if ((buffer == NULL) || (buffer->_count == 0))
    {
        return false;
    }

    return GenericBuffer_RemoveAt(buffer, buffer->_count - 1);
}

bool GenericBuffer_AddLastRange(GenericBuffer* buffer, void* items, size_t itemCount)
{
    if (buffer == NULL)
    {
        return false;
    }

    return GenericBuffer_InsertRange(buffer, items, itemCount, buffer->_count);
}

bool GenericBuffer_AddFirstRange(GenericBuffer* buffer, void* items, size_t itemCount)
{
    return GenericBuffer_InsertRange(buffer, items, itemCount, 0);
}

bool GenericBuffer_InsertRange(GenericBuffer* buffer, void* items, size_t itemCount, size_t index)
{
    size_t MovedByteCount = 0;
    unsigned char* MoveSource = NULL;
    unsigned char* MoveDestination = NULL;

    if (buffer == NULL)
    {
        return false;
    }
    if (itemCount == 0)
    {
        return true;
    }
    if (items == NULL)
    {
        return false;
    }
    if (index > buffer->_count)
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }
    if (!GenericBuffer_ReserveMoreCapacity(buffer, itemCount))
    {
        return false;
    }

    MovedByteCount = (buffer->_count - index) * buffer->_elementSize;
    MoveSource = GenericBuffer_GetElementAddress(buffer, index);
    MoveDestination = MoveSource + (itemCount * buffer->_elementSize);

    Memory_Move(MoveSource, MoveDestination, MovedByteCount);
    GenericBuffer_CopyElements(buffer, index, items, itemCount);
    buffer->_count += itemCount;
    return true;
}

bool GenericBuffer_ReplaceRange(GenericBuffer* buffer, void* items, size_t itemCount, size_t index)
{
    if (buffer == NULL)
    {
        return false;
    }
    if (itemCount == 0)
    {
        return true;
    }
    if (items == NULL)
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }
    if (index > buffer->_count)
    {
        return false;
    }
    if ((buffer->_count - index) < itemCount)
    {
        return false;
    }

    GenericBuffer_CopyElements(buffer, index, items, itemCount);
    return true;
}

bool GenericBuffer_RemoveRange(GenericBuffer* buffer, size_t startIndexInclusive, size_t endIndexExclusive)
{
    size_t ItemCountToRemove = 0;
    size_t MovedByteCount = 0;
    unsigned char* MoveSource = NULL;
    unsigned char* MoveDestination = NULL;

    if (buffer == NULL)
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }
    if (startIndexInclusive > endIndexExclusive)
    {
        return false;
    }
    if (endIndexExclusive > buffer->_count)
    {
        return false;
    }
    if (startIndexInclusive == endIndexExclusive)
    {
        return true;
    }

    ItemCountToRemove = endIndexExclusive - startIndexInclusive;
    MoveDestination = GenericBuffer_GetElementAddress(buffer, startIndexInclusive);
    MoveSource = GenericBuffer_GetElementAddress(buffer, endIndexExclusive);
    MovedByteCount = (buffer->_count - endIndexExclusive) * buffer->_elementSize;

    Memory_Move(MoveSource, MoveDestination, MovedByteCount);
    buffer->_count -= ItemCountToRemove;
    return true;
}

void* GenericBuffer_GetPointerToElement(GenericBuffer* buffer, size_t index)
{
    if (!GenericBuffer_IsIndexValid(buffer, index))
    {
        return NULL;
    }

    return GenericBuffer_GetElementAddress(buffer, index);
}

void* GenericBuffer_GetPointerToFirst(GenericBuffer* buffer)
{
    return GenericBuffer_GetPointerToElement(buffer, 0);
}

void* GenericBuffer_GetPointerToLast(GenericBuffer* buffer)
{
    if ((buffer == NULL) || (buffer->_count == 0))
    {
        return NULL;
    }

    return GenericBuffer_GetPointerToElement(buffer, buffer->_count - 1);
}

bool GenericBuffer_GetAt(GenericBuffer* buffer, size_t index, void* out)
{
    if ((buffer == NULL) || (out == NULL))
    {
        return false;
    }
    if (!GenericBuffer_IsIndexValid(buffer, index))
    {
        return false;
    }

    Memory_Copy(GenericBuffer_GetElementAddress(buffer, index), out, buffer->_elementSize);
    return true;
}

bool GenericBuffer_GetFirst(GenericBuffer* buffer, void* out)
{
    return GenericBuffer_GetAt(buffer, 0, out);
}

bool GenericBuffer_GetLast(GenericBuffer* buffer, void* out)
{
    if ((buffer == NULL) || (buffer->_count == 0))
    {
        return false;
    }

    return GenericBuffer_GetAt(buffer, buffer->_count - 1, out);
}

bool GenericBuffer_Clear(GenericBuffer* buffer)
{
    if (buffer == NULL)
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }

    buffer->_count = 0;
    return true;
}

bool GenericBuffer_Contains(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData)
{
    return (GenericBuffer_FirstIndexOf(buffer, predicate, userData) != GENERIC_BUFFER_INDEX_INVALID);
}

size_t GenericBuffer_FirstIndexOf(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData)
{
    if ((buffer == NULL) || (predicate == NULL))
    {
        return GENERIC_BUFFER_INDEX_INVALID;
    }

    for (size_t Index = 0; Index < buffer->_count; Index++)
    {
        GenericBufferElementData Element = CreateGenericBufferElementData(buffer, Index);

        if (predicate(buffer, Element, userData))
        {
            return Index;
        }
    }

    return GENERIC_BUFFER_INDEX_INVALID;
}

size_t GenericBuffer_LastIndexOf(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData)
{
    if ((buffer == NULL) || (predicate == NULL))
    {
        return GENERIC_BUFFER_INDEX_INVALID;
    }

    for (size_t Index = buffer->_count; Index > 0; Index--)
    {
        GenericBufferElementData Element = CreateGenericBufferElementData(buffer, Index - 1);

        if (predicate(buffer, Element, userData))
        {
            return Index - 1;
        }
    }

    return GENERIC_BUFFER_INDEX_INVALID;
}

bool GenericBuffer_Reverse(GenericBuffer* buffer)
{
    unsigned char* Temp = NULL;

    if ((buffer == NULL) || (buffer->_count < 2))
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }

    Temp = Memory_Allocate(buffer->_elementSize);

    for (size_t Index = 0; Index < (buffer->_count / 2); Index++)
    {
        size_t OppositeIndex = buffer->_count - Index - 1;

        GenericBuffer_SwapElements(buffer, Index, OppositeIndex, Temp);
    }

    Memory_Free(Temp);
    return true;
}

bool GenericBuffer_SortAscending(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData)
{
    return GenericBuffer_SortInternal(buffer, comparator, userData, GENERIC_BUFFER_SORT_ASCENDING);
}

bool GenericBuffer_SortDescending(GenericBuffer* buffer, GenericBufferComparator comparator, void* userData)
{
    return GenericBuffer_SortInternal(buffer, comparator, userData, GENERIC_BUFFER_SORT_DESCENDING);
}

bool GenericBuffer_Filter(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData)
{
    size_t WriteIndex = 0;

    if ((buffer == NULL) || (predicate == NULL))
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }

    for (size_t ReadIndex = 0; ReadIndex < buffer->_count; ReadIndex++)
    {
        GenericBufferElementData Element = CreateGenericBufferElementData(buffer, ReadIndex);

        if (!predicate(buffer, Element, userData))
        {
            continue;
        }

        if (WriteIndex != ReadIndex)
        {
            Memory_Move(Element._element, GenericBuffer_GetElementAddress(buffer, WriteIndex), buffer->_elementSize);
        }

        WriteIndex++;
    }

    buffer->_count = WriteIndex;
    return true;
}

bool GenericBuffer_Map(GenericBuffer* buffer, GenericBuffer* destination, GenericBufferMapper mapper, void* userData)
{
    size_t DestinationStartIndex = 0;
    size_t SourceCount = 0;

    if ((buffer == NULL) || (destination == NULL) || (mapper == NULL))
    {
        return false;
    }
    if (!GenericBuffer_CanModify(destination))
    {
        return false;
    }
    if (buffer == destination)
    {
        return false;
    }

    DestinationStartIndex = destination->_count;
    SourceCount = buffer->_count;
    if (DestinationStartIndex > (SIZE_MAX - SourceCount))
    {
        return false;
    }
    if (!GenericBuffer_EnsureTotalCapacity(destination, DestinationStartIndex + SourceCount))
    {
        return false;
    }

    for (size_t Index = 0; Index < SourceCount; Index++)
    {
        GenericBufferElementData SourceElement = CreateGenericBufferElementData(buffer, Index);
        void* DestinationElement = GenericBuffer_GetElementAddress(destination, DestinationStartIndex + Index);

        mapper(buffer, SourceElement, DestinationElement, userData);
    }

    destination->_count = DestinationStartIndex + SourceCount;
    return true;
}

bool GenericBuffer_SumInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue)
{
    int64_t Sum = 0;

    if ((buffer == NULL) || (extractor == NULL) || (outValue == NULL))
    {
        return false;
    }

    for (size_t Index = 0; Index < buffer->_count; Index++)
    {
        Sum += extractor(buffer, CreateGenericBufferElementData(buffer, Index), userData);
    }

    *outValue = Sum;
    return true;
}

bool GenericBuffer_SumDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue)
{
    double Sum = 0.0;

    if ((buffer == NULL) || (extractor == NULL) || (outValue == NULL))
    {
        return false;
    }

    for (size_t Index = 0; Index < buffer->_count; Index++)
    {
        Sum += extractor(buffer, CreateGenericBufferElementData(buffer, Index), userData);
    }

    *outValue = Sum;
    return true;
}

bool GenericBuffer_MaxInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue)
{
    int64_t MaxValue = 0;

    if ((buffer == NULL) || (extractor == NULL) || (outValue == NULL))
    {
        return false;
    }
    if (buffer->_count == 0)
    {
        return false;
    }

    MaxValue = extractor(buffer, CreateGenericBufferElementData(buffer, 0), userData);

    for (size_t Index = 1; Index < buffer->_count; Index++)
    {
        int64_t Value = extractor(buffer, CreateGenericBufferElementData(buffer, Index), userData);

        if (Value > MaxValue)
        {
            MaxValue = Value;
        }
    }

    *outValue = MaxValue;
    return true;
}

bool GenericBuffer_MaxDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue)
{
    double MaxValue = 0.0;

    if ((buffer == NULL) || (extractor == NULL) || (outValue == NULL))
    {
        return false;
    }
    if (buffer->_count == 0)
    {
        return false;
    }

    MaxValue = extractor(buffer, CreateGenericBufferElementData(buffer, 0), userData);

    for (size_t Index = 1; Index < buffer->_count; Index++)
    {
        double Value = extractor(buffer, CreateGenericBufferElementData(buffer, Index), userData);

        if (Value > MaxValue)
        {
            MaxValue = Value;
        }
    }

    *outValue = MaxValue;
    return true;
}

bool GenericBuffer_MinInt(GenericBuffer* buffer, GenericBufferIntExtractor extractor, void* userData, int64_t* outValue)
{
    int64_t MinValue = 0;

    if ((buffer == NULL) || (extractor == NULL) || (outValue == NULL))
    {
        return false;
    }
    if (buffer->_count == 0)
    {
        return false;
    }

    MinValue = extractor(buffer, CreateGenericBufferElementData(buffer, 0), userData);

    for (size_t Index = 1; Index < buffer->_count; Index++)
    {
        int64_t Value = extractor(buffer, CreateGenericBufferElementData(buffer, Index), userData);

        if (Value < MinValue)
        {
            MinValue = Value;
        }
    }

    *outValue = MinValue;
    return true;
}

bool GenericBuffer_MinDouble(GenericBuffer* buffer, GenericBufferDoubleExtractor extractor, void* userData, double* outValue)
{
    double MinValue = 0.0;

    if ((buffer == NULL) || (extractor == NULL) || (outValue == NULL))
    {
        return false;
    }
    if (buffer->_count == 0)
    {
        return false;
    }

    MinValue = extractor(buffer, CreateGenericBufferElementData(buffer, 0), userData);

    for (size_t Index = 1; Index < buffer->_count; Index++)
    {
        double Value = extractor(buffer, CreateGenericBufferElementData(buffer, Index), userData);

        if (Value < MinValue)
        {
            MinValue = Value;
        }
    }

    *outValue = MinValue;
    return true;
}

size_t GenericBuffer_CountWhere(GenericBuffer* buffer, GenericBufferPredicate predicate, void* userData)
{
    size_t MatchCount = 0;

    if ((buffer == NULL) || (predicate == NULL))
    {
        return 0;
    }

    for (size_t Index = 0; Index < buffer->_count; Index++)
    {
        if (predicate(buffer, CreateGenericBufferElementData(buffer, Index), userData))
        {
            MatchCount++;
        }
    }

    return MatchCount;
}

bool GenericBuffer_AppendByte(GenericBuffer* buffer, unsigned char byte)
{
    if (!GenericBuffer_IsByteBuffer(buffer))
    {
        return false;
    }

    return GenericBuffer_AddLast(buffer, &byte);
}

bool GenericBuffer_AppendRangeBytes(GenericBuffer* buffer, unsigned char* bytes, size_t size)
{
    if (!GenericBuffer_IsByteBuffer(buffer))
    {
        return false;
    }

    return GenericBuffer_AddLastRange(buffer, bytes, size);
}

bool GenericBuffer_AppendString(GenericBuffer* buffer, const unsigned char* str)
{
    size_t Length = 0;

    if (!GenericBuffer_IsByteBuffer(buffer))
    {
        return false;
    }
    if (str == NULL)
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }

    Length = GenericBuffer_GetStringLength(str);
    if (!GenericBuffer_ReserveMoreCapacity(buffer, Length))
    {
        return false;
    }

    GenericBuffer_CopyElements(buffer, buffer->_count, str, Length);
    buffer->_count += Length;
    return true;
}

bool GenericBuffer_NullTerminate(GenericBuffer* buffer)
{
    unsigned char NullByte = 0;

    if (!GenericBuffer_IsByteBuffer(buffer))
    {
        return false;
    }
    if ((buffer->_count > 0) && (*((unsigned char*)GenericBuffer_GetPointerToLast(buffer)) == 0))
    {
        return true;
    }

    return GenericBuffer_AddLast(buffer, &NullByte);
}

bool GenericBuffer_SetByte(GenericBuffer* buffer, size_t index, unsigned char byte)
{
    if (!GenericBuffer_IsByteBuffer(buffer))
    {
        return false;
    }

    return GenericBuffer_Replace(buffer, &byte, index);
}

bool GenericBuffer_TryPrepareForManualMutation(GenericBuffer* buffer, size_t addedElementCount)
{
    if (buffer == NULL)
    {
        return false;
    }
    if (!GenericBuffer_CanModify(buffer))
    {
        return false;
    }
    if (!GenericBuffer_ReserveMoreCapacity(buffer, addedElementCount))
    {
        return false;
    }

    return true;
}

size_t Memory_GetTotalAllocationCount(void)
{
    return atomic_load_explicit(&TotalAllocations, memory_order_relaxed);
}

size_t Memory_GetTotalReallocationCount(void)
{
    return atomic_load_explicit(&TotalReallocations, memory_order_relaxed);
}

size_t Memory_GetTotalFreeCount(void)
{
    return atomic_load_explicit(&TotalFrees, memory_order_relaxed);
}

size_t Memory_GetCurrentAllocationCount(void)
{
    return atomic_load_explicit(&CurrentAllocations, memory_order_relaxed);
}
