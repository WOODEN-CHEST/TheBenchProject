#include "WRIO.h"
#include <stddef.h>
#include <stdint.h>
#include <WRMemory.h>
#include "WRError.h"
#include <stdio.h>
#include <errno.h>
#include <limits.h>


// Types.
typedef struct IOMemoryStreamDataStruct
{
    unsigned char* _buffer;
    size_t _count;
    size_t _capacity;
    size_t _position;
} IOMemoryStreamData;



// Functions.
IOStream IOStream_GetFromStandardInput()
{
    return (IOStream) { ._data = stdin };
}

IOStream IOStream_GetFromStandardOutput()
{
    return (IOStream) { ._data = stdout };
}

IOStream IOStream_GetFromStandardError()
{
    return (IOStream) { ._data = stderr };
}

Error IOSteam_GetPosition(ErrorMessagePool* errorPool, IOStream* stream, size_t* position)
{
    *position = 0;

    if (stream->_type == IOStreamType_Memory)
    {
        *position = ((IOMemoryStreamData*)stream->_data)->_position;
        return Error_CreateSuccess();
    }

    errno = 0;
    long Pos = ftell(stream->_data);
    if (Pos == -1)
    {
        return Error_Construct3(errorPool,
            ErrorCode_IO,
            u8"Failed to retrieve the stream's position, code %d.",
            errno);
    }
    *position = (size_t)Pos;
    return Error_CreateSuccess();
}

Error IOSteam_SetPosition(ErrorMessagePool* errorPool, IOStream* stream, size_t position)
{
    long ClampedSeekPos = (position > LONG_MAX) ? LONG_MAX : (long)position;
    int Result = fseek(stream->_data, ClampedSeekPos, SEEK_SET);
    if (Result)
    {
        return Error_Construct3(errorPool,
            ErrorCode_IO,
            u8"Failed to set the stream's position, code %d.",
            errno);
    }
    return Error_CreateSuccess();
}

Error IOSteam_SetPositionSpecial(ErrorMessagePool* errorPool, IOStream* stream, IOStreamSeekOrigin origin)
{
    int SeekOriginInt;
    if (origin == IOStreamSeekOrigin_Start)
    {
        SeekOriginInt = SEEK_SET;
    }
    else if (origin == IOStreamSeekOrigin_End)
    {
        SeekOriginInt = SEEK_END;
    }
    else
    {
        SeekOriginInt = SEEK_CUR;
    }

    int Result = fseek(stream->_data, 0, SeekOriginInt);
    if (Result)
    {
        return Error_Construct3(errorPool,
            ErrorCode_IO,
            u8"Failed to set the stream's special position, code %d.",
            errno);
    }
    return Error_CreateSuccess();
}

Error IOStream_GetStreamSize(ErrorMessagePool* errorPool, IOStream* stream, size_t* sizeBytes)
{
    long CurrentPosition = ftell(stream->_data);
    if (CurrentPosition == -1)
    {
        return Error_Construct3(errorPool,
            ErrorCode_IO,
            u8"Failed to get stream size because; couldn't retrieve stream's current position, code %d.",
            errno);
    }

    int SeekEndResult = fseek(stream->_data, 0, SEEK_END);

}

void IOStream_Flush(IOStream* stream);

void IOStream_WriteByte(IOStream* stream, unsigned char byte);

void IOStream_Write(IOStream* stream, const unsigned char* data, size_t dataSize);

void IOStream_WriteString(IOStream* stream, const unsigned char* str);

void IOStream_WriteFormattedString(IOStream* stream, const unsigned char* format, ...);

Error IOStream_ReadByte(ErrorMessagePool* errorPool, IOStream* stream, unsigned char* byte);

Error IOStream_Read(ErrorMessagePool* errorPool, IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer);

bool IOStream_IsEndOfFile(IOStream* stream);

void IOStream_Close(IOStream stream);