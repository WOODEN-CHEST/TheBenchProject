#include "WRError.h"
#include "WRMemory.h"
#include <stdarg.h>
#include <stdio.h>


// Static functions.
static size_t GetMessageByteLength(const unsigned char* message)
{
    size_t Length = 0;

    if (message == NULL)
    {
        return 0;
    }

    while (message[Length] != u8'\0')
    {
        Length++;
    }

    return Length;
}

static unsigned char* DuplicateMessage(const unsigned char* message)
{
    size_t MessageLength = GetMessageByteLength(message);
    unsigned char* Copy = Memory_Allocate(MessageLength + 1);

    for (size_t Index = 0; Index < MessageLength; Index++)
    {
        Copy[Index] = message[Index];
    }

    Copy[MessageLength] = u8'\0';
    return Copy;
}

static unsigned char* FormatMessage(const char* format, va_list args)
{
    va_list ArgsCopy;
    va_copy(ArgsCopy, args);
    int RequiredByteCount = vsnprintf(NULL, 0, format, ArgsCopy);
    va_end(ArgsCopy);

    if (RequiredByteCount < 0)
    {
        return DuplicateMessage(u8"Failed to format the error message.");
    }

    size_t MessageLength = (size_t)RequiredByteCount;
    unsigned char* Message = Memory_Allocate(MessageLength + 1);
    int WriteResult = vsnprintf((char*)Message, MessageLength + 1, format, args);

    if (WriteResult < 0)
    {
        Memory_Free(Message);
        return DuplicateMessage(u8"Failed to format the error message.");
    }

    return Message;
}


// Public functions.
Error Error_CreateSuccess()
{
    return (Error){ .Code = ErrorCode_Success, .Message = NULL };
}

Error Error_Construct1(ErrorCode code, const unsigned char* message)
{
    if (message == NULL)
    {
        return (Error)
        {
            .Code = code,
            .Message = NULL,
        };
    }

    return (Error)
    {
        .Code = code,
        .Message = DuplicateMessage(message),
    };
}

Error Error_Construct2(ErrorCode code, const char* message)
{
    return Error_Construct1(code, (const unsigned char*)message);
}

Error Error_Construct3(ErrorCode code, const unsigned char* format, ...)
{
    if (format == NULL)
    {
        return (Error)
        {
            .Code = code,
            .Message = NULL,
        };
    }

    va_list Args;
    va_start(Args, format);
    unsigned char* Message = FormatMessage((const char*)format, Args);
    va_end(Args);

    return (Error)
    {
        .Code = code,
        .Message = Message,
    };
}

Error Error_Construct4(ErrorCode code, const char* format, ...)
{
    if (format == NULL)
    {
        return (Error)
        {
            .Code = code,
            .Message = NULL,
        };
    }

    va_list Args;
    va_start(Args, format);
    unsigned char* Message = FormatMessage(format, Args);
    va_end(Args);

    return (Error)
    {
        .Code = code,
        .Message = Message,
    };
}

Error Error_Construct5(ErrorCode code)
{
    return (Error){ .Code = code, .Message = NULL };
}

void Error_Deconstruct(Error* self)
{
    if (self == NULL)
    {
        return;
    }

    if (self->Message != NULL)
    {
        Memory_Free((void*)self->Message);
    }

    self->Code = ErrorCode_Success;
    self->Message = NULL;
}
