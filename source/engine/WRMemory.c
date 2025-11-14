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