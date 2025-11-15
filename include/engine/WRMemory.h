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

void* Memory_Allocate(size_t size);

void* Memory_Reallocate(void* ptr, size_t size);

void Memory_Free(void* ptr);

void Memory_Set(void* ptr, int8_t value, size_t size);

void Memory_Zero(void* ptr, size_t size);

bool Memory_IsEqual(void* regionA, void* regionB, size_t size);

void Memory_Copy(void* source, void* destination, size_t size);

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

static inline bool GenericBuffer_Write(GenericBuffer* buffer, void* itemToWrite)
{
    GenericBuffer_EnsureCapacity(buffer, (buffer->_elementCount + 1) * buffer->_elementSize);

    size_t WriteOffset = buffer->_elementCount * buffer->_elementSize;
    void* WritePosition = (void*)((uintptr_t)buffer->_buffer + WriteOffset);
    Memory_Copy(itemToWrite, WritePosition, buffer->_elementSize);
    buffer->_elementCount++;
    return true;
}

static inline bool GenericBuffer_WriteUChar(GenericBuffer* buffer, unsigned char character)
{
    GenericBuffer_EnsureCapacity(buffer, buffer->_elementCount + 1);

    size_t WriteOffset = buffer->_elementCount;
    unsigned char* WritePosition = (unsigned char*)((uintptr_t)buffer->_buffer + WriteOffset);
    *WritePosition = character;
    buffer->_elementCount++;
    return true;
}

static inline bool GenericBuffer_WriteString(GenericBuffer* buffer, const unsigned char* str)
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