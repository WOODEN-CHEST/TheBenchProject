#include "WRIO.h"
#include <stddef.h>
#include <stdint.h>


// Macros.


// Types.


// Fields.
static const size_t IO_STREAM_READ_CHUNK_SIZE = 256;


// Static functions.
static const unsigned char* GetStreamTypeName(IOStreamType type)
{
    switch (type)
    {
        case IOStreamType_File:
            return u8"file";

        case IOStreamType_Memory:
            return u8"memory";

        case IOStreamType_Socket:
            return u8"socket";

        default:
            return u8"unknown";
    }
}

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"IO stream argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateMissingOperationError(IOStream* stream, const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidState,
        u8"Cannot perform \"%s\" on the %s stream because its vtable is incomplete.",
        operationName,
        GetStreamTypeName(stream->_type));
}

static Error CreateCapabilityError(IOStream* stream, const unsigned char* operationName, const unsigned char* capabilityName)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Cannot %s the %s stream because it is not %s.",
        operationName,
        GetStreamTypeName(stream->_type),
        capabilityName);
}

static Error ReadAllFromSeekable(IOStream* stream, GenericBuffer* outBuffer)
{
    size_t RemainingSize = 0;
    Error Result = IOStream_GetStreamSizeRemaining(stream, &RemainingSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_Read(stream, RemainingSize, outBuffer);
}

static Error ReadAllFromNonSeekable(IOStream* stream, GenericBuffer* outBuffer)
{
    while (true)
    {
        size_t PreviousCount = outBuffer->_count;
        Error Result = IOStream_Read(stream, IO_STREAM_READ_CHUNK_SIZE, outBuffer);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (outBuffer->_count == PreviousCount)
        {
            return Error_CreateSuccess();
        }
    }
}


// Public functions.
Error IOStream_SetPosition(IOStream* stream, size_t position)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (!IOStream_IsSeekable(stream))
    {
        return CreateCapabilityError(stream, u8"set the position of", u8"seekable");
    }
    if (stream->_vtable._setPosition == NULL)
    {
        return CreateMissingOperationError(stream, u8"set position");
    }

    return (*stream->_vtable._setPosition)(stream->_vtable.Self, position);
}

Error IOStream_SetPositionSpecial(IOStream* stream, IOStreamSeekOrigin origin)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (!IOStream_IsSeekable(stream))
    {
        return CreateCapabilityError(stream, u8"set the special position of", u8"seekable");
    }
    if (stream->_vtable._setPositionSpecial == NULL)
    {
        return CreateMissingOperationError(stream, u8"set special position");
    }

    return (*stream->_vtable._setPositionSpecial)(stream->_vtable.Self, origin);
}

Error IOStream_SetLength(IOStream* stream, size_t length)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (!IOStream_IsLengthSettable(stream))
    {
        return CreateCapabilityError(stream, u8"set the length of", u8"length-settable");
    }
    if (stream->_vtable._setLength == NULL)
    {
        return CreateMissingOperationError(stream, u8"set length");
    }

    return (*stream->_vtable._setLength)(stream->_vtable.Self, length);
}

Error IOStream_WriteByte(IOStream* stream, unsigned char byte)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (!IOStream_IsWritable(stream))
    {
        return CreateCapabilityError(stream, u8"write to", u8"writable");
    }
    if (stream->_vtable._writeByte == NULL)
    {
        return CreateMissingOperationError(stream, u8"write byte");
    }

    return (*stream->_vtable._writeByte)(stream->_vtable.Self, byte);
}

Error IOStream_Write(IOStream* stream, const unsigned char* data, size_t dataSize)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if ((data == NULL) && (dataSize > 0))
    {
        return CreateNullArgumentError(u8"data");
    }
    if (!IOStream_IsWritable(stream))
    {
        return CreateCapabilityError(stream, u8"write to", u8"writable");
    }
    if (stream->_vtable._write == NULL)
    {
        return CreateMissingOperationError(stream, u8"write");
    }

    return (*stream->_vtable._write)(stream->_vtable.Self, data, dataSize);
}

Error IOStream_ReadByte(IOStream* stream, unsigned char* byte)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (byte == NULL)
    {
        return CreateNullArgumentError(u8"byte");
    }
    if (!IOStream_IsReadable(stream))
    {
        return CreateCapabilityError(stream, u8"read from", u8"readable");
    }
    if (stream->_vtable._readByte == NULL)
    {
        return CreateMissingOperationError(stream, u8"read byte");
    }

    return (*stream->_vtable._readByte)(stream->_vtable.Self, byte);
}

Error IOStream_Read(IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (outBuffer == NULL)
    {
        return CreateNullArgumentError(u8"outBuffer");
    }
    if (!IOStream_IsReadable(stream))
    {
        return CreateCapabilityError(stream, u8"read from", u8"readable");
    }
    if (stream->_vtable._read == NULL)
    {
        return CreateMissingOperationError(stream, u8"read");
    }

    return (*stream->_vtable._read)(stream->_vtable.Self, outBuffer, bytesToRead);
}

Error IOStream_GetStreamSizeTotal(IOStream* stream, size_t* sizeBytes)
{
    size_t CurrentPosition = 0;
    Error Result = Error_CreateSuccess();

    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (sizeBytes == NULL)
    {
        return CreateNullArgumentError(u8"sizeBytes");
    }
    if (!IOStream_IsSeekable(stream))
    {
        return CreateCapabilityError(stream, u8"get the total size of", u8"seekable");
    }

    Result = IOStream_GetPosition(stream, &CurrentPosition);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IOStream_SetPositionSpecial(stream, IOStreamSeekOrigin_End);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IOStream_GetPosition(stream, sizeBytes);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_SetPosition(stream, CurrentPosition);
}

Error IOStream_GetStreamSizeRemaining(IOStream* stream, size_t* sizeBytes)
{
    size_t TotalSize = 0;
    size_t Position = 0;
    Error Result = Error_CreateSuccess();

    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (sizeBytes == NULL)
    {
        return CreateNullArgumentError(u8"sizeBytes");
    }
    if (!IOStream_IsSeekable(stream))
    {
        return CreateCapabilityError(stream, u8"get the remaining size of", u8"seekable");
    }

    Result = IOStream_GetStreamSizeTotal(stream, &TotalSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = IOStream_GetPosition(stream, &Position);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (Position > TotalSize)
    {
        return Error_Construct3(ErrorCode_InvalidState,
            u8"The %s stream position exceeds its total size.",
            GetStreamTypeName(stream->_type));
    }

    *sizeBytes = TotalSize - Position;
    return Error_CreateSuccess();
}

Error IOStream_Move(IOStream* stream, int64_t amount)
{
    size_t CurrentPosition = 0;
    size_t NewPosition = 0;
    Error Result = Error_CreateSuccess();

    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }

    Result = IOStream_GetPosition(stream, &CurrentPosition);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (amount < 0)
    {
        uint64_t Distance = (uint64_t)(-(amount + 1)) + 1U;
        if (Distance >= CurrentPosition)
        {
            NewPosition = 0;
        }
        else
        {
            NewPosition = CurrentPosition - (size_t)Distance;
        }
    }
    else
    {
        size_t Distance = (size_t)amount;
        if (Distance > (SIZE_MAX - CurrentPosition))
        {
            return Error_Construct3(ErrorCode_ArgumentOutOfRange,
                u8"Cannot move the %s stream because the target position would overflow.",
                GetStreamTypeName(stream->_type));
        }

        NewPosition = CurrentPosition + Distance;
    }

    return IOStream_SetPosition(stream, NewPosition);
}

Error IOStream_WriteString(IOStream* stream, const unsigned char* str)
{
    size_t Length = 0;

    if (str == NULL)
    {
        return CreateNullArgumentError(u8"str");
    }

    while (str[Length] != 0)
    {
        Length++;
    }

    return IOStream_Write(stream, str, Length);
}

Error IOStream_ReadAll(IOStream* stream, GenericBuffer* buffer)
{
    if (stream == NULL)
    {
        return CreateNullArgumentError(u8"stream");
    }
    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }
    if (!IOStream_IsReadable(stream))
    {
        return CreateCapabilityError(stream, u8"read all data from", u8"readable");
    }

    if (IOStream_IsSeekable(stream))
    {
        return ReadAllFromSeekable(stream, buffer);
    }

    return ReadAllFromNonSeekable(stream, buffer);
}

Error IOStream_Deconstruct(IOStream* stream)
{
    if ((stream == NULL) || (stream->_vtable._deconstruct == NULL))
    {
        return Error_CreateSuccess();
    }

    return (*stream->_vtable._deconstruct)(stream->_vtable.Self);
}
