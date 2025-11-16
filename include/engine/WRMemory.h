#pragma once
#include <stddef.h>
#include <stdint.h>


// Types.
typedef struct GenericBufferStruct GenericBuffer;

typedef bool (*GenericBufferAllocateCallback)(GenericBuffer* destination, size_t requestedCapacity);

struct GenericBufferStruct
{
    void* _data;
    size_t _capacity;
    size_t _count;
    size_t _elementSize;
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

bool GenericBuffer_EnsureCapacity(GenericBuffer* buffer, size_t requiredSize);

bool GenericBuffer_Write(GenericBuffer* buffer, void* itemToWrite);

bool GenericBuffer_WriteUChar(GenericBuffer* buffer, unsigned char character);

bool GenericBuffer_WriteString(GenericBuffer* buffer, const unsigned char* str);

bool GenericBuffer_WriteStringBySize(GenericBuffer* buffer, const unsigned char* str, size_t stringSize);

bool GenericBuffer_TryNullTerminate(GenericBuffer* buffer);

bool GenericBuffer_WriteVoidPtr(GenericBuffer* buffer, void* ptr);

void GenericBuffer_TrackWrittenItems(GenericBuffer* buffer, size_t itemCount);

void* Memory_Allocate(size_t size);

void* Memory_Reallocate(void* ptr, size_t size);

void Memory_Free(void* ptr);

void Memory_Set(void* ptr, int8_t value, size_t size);

void Memory_Zero(void* ptr, size_t size);

bool Memory_IsEqual(void* regionA, void* regionB, size_t size);

void Memory_Copy(void* source, void* destination, size_t size);

void Memory_Move(void* source, void* destination, size_t size);