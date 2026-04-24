#include "WRMemory.h"
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <stdatomic.h>


// Fields.
static atomic_size_t TotalAllocations = 0;
static atomic_size_t TotalReallocations = 0;
static atomic_size_t TotalFrees = 0;
static atomic_size_t CurrentAllocations = 0;


// Static functions.
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


// Functions.
void* Memory_Allocate(size_t size)
{
    void* Block = malloc(size);
    if (!Block)
    {
        abort(); // We give up LMAO.
    }

    atomic_fetch_add_explicit(&TotalAllocations, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&CurrentAllocations, 1, memory_order_relaxed);
    return Block;
}

void* Memory_Reallocate(void* ptr, size_t size)
{
    void* Block = realloc(ptr, size);
    if (!Block)
    {
        abort(); // We give up here too.
    }
    atomic_fetch_add_explicit(&TotalReallocations, 1, memory_order_relaxed);
    return Block;
}

void Memory_Free(void* ptr)
{
    free(ptr);
    atomic_fetch_add_explicit(&TotalFrees, 1, memory_order_relaxed);
    atomic_fetch_sub_explicit(&TotalAllocations, 1, memory_order_relaxed);
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
    return !memcmp(regionA, regionB, size);
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
    if ((buffer->_capacity < capacity) || !buffer->_data)
    {
        bool WasMemoryAllocated = buffer->_requestMoreSpaceCallback && (*buffer->_requestMoreSpaceCallback)(buffer, capacity);
        if (!WasMemoryAllocated || (buffer->_capacity < capacity))
        {
            return false;
        }
    }
    return true;
}

bool GenericBuffer_ReserveMoreCapacity(GenericBuffer* buffer, size_t requiredSize)
{
    return GenericBuffer_EnsureTotalCapacity(buffer, buffer->_count + requiredSize);
}


bool GenericBuffer_AddLast(GenericBuffer* buffer, void* item)
{
    return GenericBuffer_Insert(buffer, item, buffer->_count);
}

bool GenericBuffer_AddFirst(GenericBuffer* buffer, void* item)
{
    return GenericBuffer_Insert(buffer, item, 0);
}

bool GenericBuffer_Insert(GenericBuffer* buffer, void* item, size_t index)
{
    if (index > buffer->_count)
    {
        return false;
    }
    if (GenericBuffer_IsReadOnly(buffer))
    {
        return false;
    }
    if (!GenericBuffer_ReserveMoreCapacity(buffer, 1))
    {
        return false;
    }

    size_t MovedByteCount = (buffer->_count - index) * buffer->_elementSize;
    unsigned char* MoveSource = buffer->_data + (index * buffer->_elementSize);
    unsigned char* MoveDestination = MoveSource + buffer->_elementSize;
    
    Memory_Move(MoveSource, MoveDestination, MovedByteCount);

    Memory_Copy(item, MoveDestination, buffer->_elementSize);
    buffer->_count++;

    return true;
}

bool GenericBuffer_Replace(GenericBuffer* buffer, void* item, size_t index);















size_t GenericBuffer_GetCapacityRemaining(GenericBuffer* buffer)
{
    return buffer->_capacity - buffer->_count;
}

size_t Memory_GetTotalAllocationCount()
{
    return atomic_load(&TotalAllocations);
}

size_t Memory_GetTotalRellocationCount()
{
    return atomic_load(&TotalReallocations);
}

size_t Memory_GetTotalFreeCount()
{
    return atomic_load(&TotalFrees);
}

size_t Memory_GetCurrentAllocationCount()
{
    return atomic_load(&CurrentAllocations);
}