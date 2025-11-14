#pragma once
#include <stddef.h>
#include <stdint.h>


// Types.
typedef struct GenericBufferStruct GenericBuffer;

typedef bool (*GenericBufferAllocateCallback)(GenericBuffer* destination);

struct GenericBufferStruct
{
    void* _buffer;
    size_t _bufferSize;
    size_t _elementSize;
    size_t _elementCount;
    void* _userData;
    GenericBufferAllocateCallback _requestMoreSpaceCallback;
};


// Functions.
GenericBuffer GenericBuffer_CreateVariable(void* destination,
    size_t size,
    size_t elementSize,
    size_t elementCount,
    void* userData,
    GenericBufferAllocateCallback callback);

GenericBuffer GenericBuffer_CreateConstant(void* destination, size_t bufferSize, size_t elementSize, size_t elementCount);

static inline bool GenericBuffer_Write(GenericBuffer* buffer, void* itemToWrite)
{
    size_t RequiredSize = (1 + buffer->_elementCount) * buffer->_elementSize;
    if (buffer->_bufferSize <= RequiredSize)
    {
        bool WasMemoryAllocated = buffer->_requestMoreSpaceCallback && (*buffer->_requestMoreSpaceCallback)(buffer);
        if (!WasMemoryAllocated || (buffer->_bufferSize <= RequiredSize))
        {
            return false;
        }
    }

    size_t WriteOffset = buffer->_elementCount * buffer->_elementSize;
    void* WritePosition = (void*)((uintptr_t)buffer->_buffer + WriteOffset);
    Memory_Copy(itemToWrite, WritePosition, buffer->_elementSize);
    return true;
}

static inline bool GenericBuffer_WriteUChar(GenericBuffer* buffer, unsigned char character)
{
    size_t RequiredSize = (1 + buffer->_elementCount);
    if (buffer->_bufferSize <= RequiredSize)
    {
        bool WasMemoryAllocated = buffer->_requestMoreSpaceCallback && (*buffer->_requestMoreSpaceCallback)(buffer);
        if (!WasMemoryAllocated || (buffer->_bufferSize <= RequiredSize))
        {
            return false;
        }
    }

    size_t WriteOffset = buffer->_elementCount;
    unsigned char* WritePosition = (unsigned char*)((uintptr_t)buffer->_buffer + WriteOffset);
    *WritePosition = character;
    return true;
}

void* Memory_Allocate(size_t size);

void* Memory_Reallocate(void* ptr, size_t size);

void Memory_Free(void* ptr);

void Memory_Set(void* ptr, int8_t value, size_t size);

void Memory_Zero(void* ptr, size_t size);

bool Memory_IsEqual(void* regionA, void* regionB, size_t size);

void Memory_Copy(void* source, void* destination, size_t size);