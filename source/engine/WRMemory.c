#include "WRMemory.h"
#include <stdlib.h>
#include <memory.h>
#include <string.h>

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

bool Memory_IsEqual(void* regionA, void* regionB, size_t size)
{
    return !memcmp(regionA, regionB, size);
}

void Memory_Copy(void* source, void* destination, size_t size)
{
    memcpy(destination, source, size);
}

void Memory_Move(void* source, void* destination, size_t size)
{
    memmove(destination, source, size);
}

GenericBuffer GenericBuffer_CreateVariable(void* destination,
    size_t bufferSize,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback)
{
    return (GenericBuffer)
    {
        ._data = destination,
        ._capacity = bufferSize,
        ._elementSize = elementSize,
        ._count = elementCount,
        ._requestMoreSpaceCallback = callback,
        ._userData = userData
    };
}

GenericBuffer GenericBuffer_CreateConstant(void* destination, size_t bufferSize, size_t elementSize, size_t elementCount)
{
    return GenericBuffer_CreateVariable(destination, bufferSize, elementSize, elementCount, NULL, NULL);
}

bool GenericBuffer_EnsureCapacity(GenericBuffer* buffer, size_t capacity)
{
    if (buffer->_capacity < capacity)
    {
        bool WasMemoryAllocated = buffer->_requestMoreSpaceCallback && (*buffer->_requestMoreSpaceCallback)(buffer, capacity);
        if (!WasMemoryAllocated || (buffer->_capacity < capacity))
        {
            return false;
        }
    }
    return true;
}

bool GenericBuffer_Write(GenericBuffer* buffer, void* itemToWrite)
{
    if (!GenericBuffer_EnsureCapacity(buffer, buffer->_count + 1))
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
    if (!GenericBuffer_EnsureCapacity(buffer, buffer->_count + 1))
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
    GenericBuffer_EnsureCapacity(buffer, (buffer->_count + 1) * sizeof(ptr));

    size_t WriteOffset = buffer->_count * sizeof(ptr);
    void** WritePosition = (void**)((uintptr_t)buffer->_data + WriteOffset);
    *WritePosition = ptr;
    buffer->_count++;
    return true;
}

void GenericBuffer_TrackWrittenItems(GenericBuffer* buffer, size_t itemCount)
{
    buffer->_count += itemCount;
}