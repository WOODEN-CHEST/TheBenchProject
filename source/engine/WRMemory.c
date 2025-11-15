#include "WRMemory.h"
#include <stdlib.h>
#include <memory.h>

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

GenericBuffer GenericBuffer_CreateVariable(void* destination,
    size_t bufferSize,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback)
{
    return (GenericBuffer)
    {
        ._buffer = destination,
        ._bufferSize = bufferSize,
        ._elementSize = elementSize,
        ._elementCount = elementCount,
        ._requestMoreSpaceCallback = callback,
        ._userData = userData
    };
}

GenericBuffer GenericBuffer_CreateConstant(void* destination, size_t bufferSize, size_t elementSize, size_t elementCount)
{
    return GenericBuffer_CreateVariable(destination, bufferSize, elementSize, elementCount, NULL, NULL);
}

static inline bool GenericBuffer_EnsureCapacity(GenericBuffer* buffer, size_t requiredSize)
{
    while (buffer->_bufferSize <= requiredSize)
    {
        size_t OldSize = buffer->_bufferSize;
        bool WasMemoryAllocated = buffer->_requestMoreSpaceCallback && (*buffer->_requestMoreSpaceCallback)(buffer);
        if (!WasMemoryAllocated || (buffer->_bufferSize <= OldSize))
        {
            return false;
        }
    }
    return true;
}

bool GenericBuffer_Write(GenericBuffer* buffer, void* itemToWrite)
{
    GenericBuffer_EnsureCapacity(buffer, (buffer->_elementCount + 1) * buffer->_elementSize);

    size_t WriteOffset = buffer->_elementCount * buffer->_elementSize;
    void* WritePosition = (void*)((uintptr_t)buffer->_buffer + WriteOffset);
    Memory_Copy(itemToWrite, WritePosition, buffer->_elementSize);
    buffer->_elementCount++;
    return true;
}

bool GenericBuffer_WriteUChar(GenericBuffer* buffer, unsigned char character)
{
    GenericBuffer_EnsureCapacity(buffer, buffer->_elementCount + 1);

    size_t WriteOffset = buffer->_elementCount;
    unsigned char* WritePosition = (unsigned char*)((uintptr_t)buffer->_buffer + WriteOffset);
    *WritePosition = character;
    buffer->_elementCount++;
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

static inline void GenericBuffer_TrackWrittenItems(GenericBuffer* buffer, size_t itemCount)
{
    buffer->_elementCount += itemCount;
}