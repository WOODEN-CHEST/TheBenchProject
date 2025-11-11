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
    void* _userData;
    GenericBufferAllocateCallback _requestMoreSpaceCallback;
};


// Functions.
GenericBuffer GenericBuffer_CreatVariable(void* destination,
    size_t size,
    void* userData,
    GenericBufferAllocateCallback callback);

GenericBuffer GenericBuffer_CreateConstant(void* destination, size_t size);

void* Memory_Allocate(size_t size);

void* Memory_Reallocate(void* ptr, size_t size);

void Memory_Free(void* ptr);

void Memory_Set(void* ptr, int8_t value, size_t size);

void Memory_Zero(void* ptr, size_t size);

bool Memory_IsEqual(void* regionA, void* regionB, size_t size);

void Memory_Copy(void* source, void* destination, size_t size);