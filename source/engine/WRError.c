#include "WRError.h"
#include "WRMemory.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>


// Fields.
static const size_t ERROR_POOL_CAPACITY_DEFAULT = 4;
static const size_t ERROR_POOL_CAPACITY_GROWTH = 2;


// Static functions.
static void EnsureErrorPoolCapacity(ErrorMessagePool* self, size_t capacity)
{
    if (self->_capacity >= capacity)
    {
        return;
    }

    size_t NewCapacity = self->_capacity == 0 ? ERROR_POOL_CAPACITY_DEFAULT : self->_capacity;
    while (NewCapacity < capacity)
    {
        NewCapacity *= ERROR_POOL_CAPACITY_GROWTH;
    }

    size_t NewSize = NewCapacity * MAX_ERROR_MESSAGE_BUFFER_LENGTH;
    self->_messages = self->_messages ? Memory_Reallocate(self->_messages, NewSize) : Memory_Allocate(NewSize);
    self->_capacity = NewCapacity;
}


// Functions.
Error Error_CreateSuccess()
{
    return (Error) { .Code = ErrorCode_Success, .Message = NULL };
}

Error Error_Construct(ErrorMessagePool* pool, ErrorCode code, const unsigned char* message)
{
    Error CreatedError;
    CreatedError.Code = code;

    if (message)
    {
        unsigned char* MessageBuffer = ErrorMessagePool_GetNextMessage(pool);
        size_t Index;
        for (Index = 0; (message[Index] != '\0') && (Index < MAX_ERROR_MESSAGE_BUFFER_LENGTH - 1); Index++)
        {
            MessageBuffer[Index] = message[Index];
        }
        MessageBuffer[Index] = '\0';
        CreatedError.Message = MessageBuffer;
    }
    else
    {
        CreatedError.Message = NULL;
    }

    return CreatedError;
}

Error Error_Construct2(ErrorMessagePool* pool, ErrorCode code, char* message)
{
    return Error_Construct(pool, code, (const unsigned char*)message);
}

Error Error_Construct3(ErrorMessagePool* pool, ErrorCode code, const unsigned char* format, ...)
{
    unsigned char* Message = ErrorMessagePool_GetNextMessage(pool);
    va_list Args;
    va_start(Args, format);
    vsnprintf((char*)Message, MAX_ERROR_MESSAGE_BUFFER_LENGTH, (const char*)format, Args);
    va_end(Args);

    return (Error)
    {
        .Code = code,
        .Message = Message
    };
}

Error Error_Construct4(ErrorMessagePool* pool, ErrorCode code, char* format, ...)
{
    unsigned char* Message = ErrorMessagePool_GetNextMessage(pool);
    va_list Args;
    va_start(Args, format);
    vsnprintf((char*)Message, MAX_ERROR_MESSAGE_BUFFER_LENGTH, format, Args);
    va_end(Args);

    return (Error)
    {
        .Code = code,
        .Message = Message
    };
}

void ErrorMessagePool_Construct1(ErrorMessagePool* self)
{
    Memory_Zero(self, sizeof(*self));
    EnsureErrorPoolCapacity(self, ERROR_POOL_CAPACITY_DEFAULT);
}

void ErrorMessagePool_Deconstruct1(ErrorMessagePool* self)
{
    if (self->_messages)
    {
        Memory_Free(self->_messages);
    }
    Memory_Zero(self, sizeof(*self));
}

void ErrorMessagePool_Clear(ErrorMessagePool* self)
{
    self->_count = 0;
}

unsigned char* ErrorMessagePool_GetNextMessage(ErrorMessagePool* self)
{
    EnsureErrorPoolCapacity(self, self->_count + 1);
    size_t Index = self->_count;
    self->_count++;
    return &self->_messages[Index];
}