#include "WRMemory.h"
#include <stdlib.h>
#include <memory.h>
#include <string.h>


// Static functions.
static inline GenericBuffer CreateGenericBuffer(void* destination,
    size_t bufferCapacity,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback,
    GenericBufferFlags flags)
{
    return (GenericBuffer)
    {
        ._data = destination,
        ._capacity = bufferCapacity,
        ._elementSize = elementSize,
        ._count = elementCount,
        ._requestMoreSpaceCallback = callback,
        ._userData = userData,
        ._flags = flags
    };
}
// Functions.
void* Memory_Allocate(size_t size)
{
    void* Block = malloc(size);
    if (!Block)
    {
        abort(); // We give up LMAO.
    }
    return Block;
}

void* Memory_Reallocate(void* ptr, size_t size)
{
    void* Block = realloc(ptr, size);
    if (!Block)
    {
        abort(); // We give up here too.
    }
    return Block;
}

void Memory_Free(void* ptr)
{
    free(ptr);
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

GenericBuffer GenericBuffer_CreateVariable(void* destination,
    size_t bufferCapacity,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback)
{
    return CreateGenericBuffer(destination,
        bufferCapacity,
        elementSize,
        elementCount,
        userData,
        callback,
        GenericBufferFlags_None);
}

GenericBuffer GenericBuffer_CreateConstant(void* destination, size_t bufferCapacity, size_t elementSize, size_t elementCount)
{
    return CreateGenericBuffer(destination,
        bufferCapacity,
        elementSize,
        elementCount,
        NULL,
        NULL,
        GenericBufferFlags_FixedCapacity);
}

GenericBuffer GenericBuffer_CreateVariableIncomplete(void* destination, size_t bufferCapacity, size_t elementSize, size_t elementCount)
{
    return CreateGenericBuffer(destination,
        bufferCapacity,
        elementSize,
        elementCount,
        NULL,
        NULL,
        GenericBufferFlags_None);
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

bool GenericBuffer_EnsureCapacity(GenericBuffer* buffer, size_t capacity)
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

bool GenericBuffer_ReserveCapacity(GenericBuffer* buffer, size_t requiredSize)
{
    return GenericBuffer_EnsureCapacity(buffer, buffer->_count + requiredSize);
}

bool GenericBuffer_Write(GenericBuffer* buffer, void* itemToWrite)
{
    if (!GenericBuffer_ReserveCapacity(buffer, 1))
    {
        return false;
    }

    size_t WriteOffset = buffer->_count * buffer->_elementSize;
    void* WritePosition = (void*)((uintptr_t)buffer->_data + WriteOffset);
    Memory_Copy(itemToWrite, WritePosition, buffer->_elementSize);
    buffer->_count++;
    return true;
}

bool GenericBuffer_WriteUChar(GenericBuffer* buffer, unsigned char character)
{
    if (!GenericBuffer_ReserveCapacity(buffer, 1))
    {
        return false;
    }

    size_t WriteOffset = buffer->_count;
    unsigned char* WritePosition = (unsigned char*)((uintptr_t)buffer->_data + WriteOffset);
    *WritePosition = character;
    buffer->_count++;
    return true;
}

bool GenericBuffer_WriteString(GenericBuffer* buffer, const unsigned char* str)
{
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        if (!GenericBuffer_WriteUChar(buffer, str[i]))
        {
            return false;
        }
    }
    return true;
}

bool GenericBuffer_WriteStringBySize(GenericBuffer* buffer, const unsigned char* str, size_t stringSize)
{
    if (stringSize == 0)
    {
        return true;
    }
    if (!GenericBuffer_ReserveCapacity(buffer, stringSize))
    {
        return false;
    }

    size_t WriteOffset = buffer->_count;
    unsigned char* WritePosition = (unsigned char*)((uintptr_t)buffer->_data + WriteOffset);
    Memory_Copy(str, WritePosition, stringSize);
    buffer->_count += stringSize;
    return true;
}

bool GenericBuffer_TryNullTerminate(GenericBuffer* buffer)
{
    if ((buffer->_capacity == 0) || (buffer->_count > buffer->_capacity))
    {
        return false;
    }
    else if (buffer->_capacity == buffer->_count)
    {
        ((unsigned char*)buffer->_data)[buffer->_capacity - 1] = '\0';
    }
    else
    {
        ((unsigned char*)buffer->_data)[buffer->_count] = '\0';
        buffer->_count++; 
    }
    return true;
}

bool GenericBuffer_WriteVoidPtr(GenericBuffer* buffer, void* ptr)
{
    if (!GenericBuffer_ReserveCapacity(buffer, 1))
    {
        return false;
    }

    size_t WriteOffset = buffer->_count * sizeof(ptr);
    void** WritePosition = (void**)((uintptr_t)buffer->_data + WriteOffset);
    *WritePosition = ptr;
    buffer->_count++;
    return true;
}

bool GenericBuffer_WriteSizeT(GenericBuffer* buffer, size_t value)
{
    if (!GenericBuffer_ReserveCapacity(buffer, 1))
    {
        return false;
    }

    size_t WriteOffset = buffer->_count * sizeof(value);
    size_t* WritePosition = (size_t*)((uintptr_t)buffer->_data + WriteOffset);
    *WritePosition = value;
    buffer->_count++;
    return true;
}

void GenericBuffer_Clear(GenericBuffer* buffer)
{
    buffer->_count = 0;
}

size_t GenericBuffer_GetCapacityRemaining(GenericBuffer* buffer)
{
    return buffer->_capacity - buffer->_count;
}