#include "WRIO.h"
#include <stddef.h>
#include <stdint.h>
#include <WRMemory.h>
#include "WRError.h"
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include "WRMath.h"
#include "string.h"


// Types.
typedef struct IOMemoryStreamDataStruct
{
    GenericBuffer _selfContainedBuffer;
    GenericBuffer* _buffer;
    size_t _position;
    bool _isClosed;
} IOMemoryStreamData;


// Fields.
static const size_t MEM_STREAM_CAPACITY_DEFAULT = 512;
static const size_t MEM_STREAM_CAPACITY_GROWTH = 4;



// Static functions.
static Error CreateUnknownStreamTypeError(ErrorMessagePool* errorPool, IOStreamType type)
{
    return Error_Construct3(errorPool,
        ErrorCode_IO,
        u8"Cannot operate on IO stream because it has an unknown type of %d.",
        type);
}

static const unsigned char* GetStreamTypeName(IOStreamType type)
{
    switch (type)
    {
        case IOStreamType_File:
            return u8"file";
            break;

        case IOStreamType_Memory:
            return u8"memory";
            break;
        
        default:
            u8"invalid";
    }
}

static Error CreateBufferTooSmallError(ErrorMessagePool* errorPool, IOStreamType* streamType, const unsigned char* operationName)
{
    return Error_Construct3(errorPool,
        ErrorCode_BufferTooSmall,
        u8"Memory stream buffer is too small to perform the operation \"%s\" on the %s stream.",
        operationName, GetStreamTypeName(streamType));
}

static Error CreateInvalidSpecialPositionError(ErrorMessagePool* errorPool, IOStreamType streamType, IOStreamSeekOrigin origin)
{
    return Error_Construct3(errorPool,
        ErrorCode_IllegalArgument,
        u8"Invalid %s stream special position %d.",
        origin);
}

static void InitMemStreamVTable(IOStreamVTable* table)
{
    table->_getPosition = &MemoryStreamGetPosition;
    table->_setPosition = &MemoryStreamSetPosition;
    table->_setPositionSpecial = &MemoryStreamSetPositionSpecial;
    table->_flush = &MemoryStreamFlush;
    table->_writeByte = &MemoryStreamWriteByte;
    table->_write = &MemoryStreamWrite;
    table->_readByte = &MemoryStreamReadByte;
    table->_read = &MemoryStreamRead;
    table->_close = &MemoryStreamClose;
}

static void AssignStreamFields(IOStream* stream, ErrorMessagePool* errorPool, IOStreamType* type, IOStreamFlags flags, void* data)
{
    stream->_data = data;
    stream->ErrorPool = errorPool;
    stream->_type = type;
    stream->_flags = flags;
}

/* File stream. */
static Error FileStreamGetPosition(IOStream* stream, size_t* position)
{
    errno = 0;
    long Pos = ftell(stream->_data);
    if (Pos == -1)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IO,
            u8"Failed to retrieve the %s stream's position, code %d.",
            GetStreamTypeName(stream->_type), errno);
    }
    *position = (size_t)Pos;
    return Error_CreateSuccess();
}

static Error FileStreamSetPosition(IOStream* stream, size_t position)
{
    long ClampedSeekPos = (position > LONG_MAX) ? LONG_MAX : (long)position;
    int Result = fseek(stream->_data, ClampedSeekPos, SEEK_SET);
    if (Result)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IO,
            u8"Failed to set the %s stream's position, code %d.",
            GetStreamTypeName(stream->_type), errno);
    }
    return Error_CreateSuccess();
}

static Error FileStreamSetPositionSpecial(IOStream* stream, IOStreamSeekOrigin origin)
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
        return CreateInvalidSpecialPositionError(stream->ErrorPool, stream->_type, origin);
    }

    int Result = fseek(stream->_data, 0, SeekOriginInt);
    if (Result)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IO,
            u8"Failed to set the %s stream's special position, code %d.",
            GetStreamTypeName(stream->_type), errno);
    }
    return Error_CreateSuccess();
}

static Error FileStreamFlush(IOStream* stream)
{
    int Result = fflush(stream->_data);
    if (Result)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IO,
            u8"Failed to flush %s stream.",
            GetStreamTypeName(stream->_type));
    }
    return Error_CreateSuccess();
}

static Error FileStreamWriteByte(IOStream* stream, unsigned char byte)
{
    FILE* FileStream = stream->_data;
    int Result = fputc(byte, FileStream);
    if (Result)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IO,
            u8"Failed to write a byte to a %s stream. Error code %d.",
            GetStreamTypeName(stream->_type), ferror(FileStream));
    }
    return Error_CreateSuccess();
}

static Error FileStreamWrite(IOStream* stream, const unsigned char* data, size_t dataSize)
{
    size_t ObjectsWritten = fwrite(data, 1, dataSize, stream->_data);
    if (ObjectsWritten < dataSize)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IO,
            u8"Failed to write all bytes to a %s stream, only wrote %zu bytes out of %zu.",
            GetStreamTypeName(stream->_type), ObjectsWritten, dataSize);
    }
    return Error_CreateSuccess();
}

static Error FileStreamReadByte(IOStream* stream, unsigned char* byte)
{
    FILE* FileStream = stream->_data;
    int ByteValue = fgetc(FileStream);
    if (ByteValue == EOF)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IO,
            u8"Failed to read a byte from a %s stream. Error code %d.",
            GetStreamTypeName(stream->_type), ferror(FileStream));
    }

    *byte = (unsigned char)ByteValue;
    return Error_CreateSuccess();
}

static Error FileStreamRead(IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer)
{
    FILE* FileStream = stream->_data;
    clearerr(FileStream);

    if (!GenericBuffer_ReserveCapacity(outBuffer, bytesToRead))
    {
        return CreateBufferTooSmallError(stream->ErrorPool, stream->_type, u8"read bulk");
    }

    size_t ReadBytes = fread(outBuffer,
        ((char*)outBuffer->_data) + (outBuffer->_count),
        bytesToRead,
        FileStream);
    outBuffer->_count += ReadBytes;
    return Error_CreateSuccess();
}

static Error FileStreamClose(IOStream* stream)
{
    int Result = fclose(stream->_data);
    if (Result)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IO,
            u8"Failed to close %s stream.",
            GetStreamTypeName(stream->_type));
    }
    return Error_CreateSuccess();
}

static bool FIleStreamIsEOF(IOStream* stream)
{
    return feof(stream->_data);
}


/* Memory stream. */
static bool MemoryStreamAllocateCallback(GenericBuffer* destination, size_t requestedCapacity)
{
    if (requestedCapacity <= destination->_capacity)
    {
        return true;
    }
    if (destination->_flags & GenericBufferFlags_FixedCapacity)
    {
        return false;
    }

    size_t NewCapacity = destination->_capacity ? destination->_capacity : MEM_STREAM_CAPACITY_DEFAULT;
    while (NewCapacity < requestedCapacity)
    {
        NewCapacity *= MEM_STREAM_CAPACITY_GROWTH;
    }

    size_t NewSize = NewCapacity * destination->_elementSize;
    destination->_data = destination->_data ? Memory_Reallocate(destination->_data, NewSize) : Memory_Allocate(NewSize);
    destination->_capacity = requestedCapacity;
    return true;
}

static Error MemoryStreamGetPosition(ErrorMessagePool* errorPool, IOStream* stream, size_t* position)
{
    *position = ((IOMemoryStreamData*)stream->_data)->_position;
    return Error_CreateSuccess();
}

static Error MemoryStreamSetPosition(ErrorMessagePool* errorPool, IOStream* stream, size_t position)
{
    IOMemoryStreamData* MemData = stream->_data;
    MemData->_position = Math_MinSizeT(position, MemData->_buffer->_count);
    return Error_CreateSuccess();
}

static Error MemoryStreamSetPositionSpecial(ErrorMessagePool* errorPool, IOStream* stream, IOStreamSeekOrigin origin)
{
    IOMemoryStreamData* MemData = stream->_data;

    if (origin == IOStreamSeekOrigin_Start)
    {
        MemData->_position = 0;
    }
    else if (origin == IOStreamSeekOrigin_End)
    {
        MemData->_position = MemData->_buffer->_count;
    }
    else
    {
        return CreateInvalidSpecialPositionError(errorPool, stream->_type, origin);
    }
    return Error_CreateSuccess();
}

static Error MemoryStreamFlush(ErrorMessagePool* errorPool, IOStream* stream)
{
    return Error_CreateSuccess();
}

static Error MemoryStreamWriteByte(ErrorMessagePool* errorPool, IOStream* stream, unsigned char byte)
{
    IOMemoryStreamData* MemData = stream->_data;
    if ((MemData->_buffer->_count == MemData->_position) && !GenericBuffer_ReserveCapacity(MemData->_buffer, 1))
    {
        return CreateBufferTooSmallError(stream->ErrorPool, stream->_type, u8"write byte");
    }
    ((unsigned char*)MemData->_buffer)[MemData->_position] = byte;
    MemData->_position++;
    return Error_CreateSuccess(); 
}

static Error MemoryStreamWrite(ErrorMessagePool* errorPool, IOStream* stream, const unsigned char* data, size_t dataSize)
{
    IOMemoryStreamData* MemData = stream->_data;
    size_t BytesToReserve = dataSize - MemData->_buffer->_count + MemData->_position;
    if (!GenericBuffer_ReserveCapacity(MemData->_buffer, BytesToReserve))
    {
        return CreateBufferTooSmallError(stream->ErrorPool, stream->_type, u8"write bulk");
    }

    Memory_Copy(data, ((unsigned char*)MemData->_buffer->_data) + MemData->_position, dataSize);
    return Error_CreateSuccess();
}

static Error MemoryStreamReadByte(ErrorMessagePool* errorPool, IOStream* stream, unsigned char* byte)
{
    IOMemoryStreamData* MemData = stream->_data;
    if (MemData->_buffer->_count == 0)
    {
        return Error_Construct3(errorPool,
            ErrorCode_IO,
            u8"Cannot read a byte from a %s stream because it is empty.",
            GetStreamTypeName(stream->_type));
    }

    *byte = ((unsigned char*)MemData->_buffer->_data)[MemData->_position];
    MemData->_position++;
    return Error_CreateSuccess();
}

static Error MemoryStreamRead(ErrorMessagePool* errorPool, IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer)
{
    IOMemoryStreamData* MemData = stream->_data;
    if (MemData->_isClosed)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot read from closed %s stream.",
            GetStreamTypeName(stream->_type));
    }
    if (outBuffer->_elementSize != MemData->_buffer->_elementSize)
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_IllegalArgument,
            u8"The given out buffer for a %s stream read operation has an element size of %zu, expected %zu.",
            GetStreamTypeName(stream->_type), outBuffer->_elementSize, MemData->_buffer->_elementSize);
    }

    size_t MaxBytesToRead = Math_MinSizeT(MemData->_buffer->_count - MemData->_position, bytesToRead);
    if (MaxBytesToRead == 0)
    {
        return Error_CreateSuccess();
    }

    if (!GenericBuffer_ReserveCapacity(outBuffer, MaxBytesToRead))
    {
        return Error_Construct3(errorPool,
            ErrorCode_BufferTooSmall,
            u8"Destination buffer into which to read the %s stream is too small to hold the requested %zu bytes.",
            GetStreamTypeName(stream->_type), MaxBytesToRead);
    }
    Memory_Copy(((char*)MemData->_buffer->_data) + MemData->_position, outBuffer->_data, MaxBytesToRead);
    outBuffer->_count += MaxBytesToRead;
    MemData->_position += MaxBytesToRead;
    return Error_CreateSuccess();
}

static Error MemoryStreamClose(ErrorMessagePool* errorPool, IOStream* stream)
{
    IOMemoryStreamData* MemData = stream->_data;
    MemData->_isClosed = true;
    return Error_CreateSuccess();
}

static bool MemoryStreamIsEOF(IOStream* stream)
{
    IOMemoryStreamData* MemData = stream->_data;
    return MemData->_position == MemData->_buffer->_count;
}


/* ALl stream types. */
static Error ReadAllFromSeekable(IOStream* stream, GenericBuffer* outBuffer)
{
    size_t RemainingSize;
    Error ErrorResult = IOStream_GetStreamSizeRemaining(stream, &RemainingSize);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    return IOStream_Read(stream, RemainingSize, outBuffer);
}

static Error ReadAllFromNonSeekable(IOStream* stream, GenericBuffer* outBuffer)
{
    while (!IOStream_IsEndOfStream(stream))
    {
        unsigned char ReadByte;
        Error ErrorResult = IOStream_ReadByte(stream, &ReadByte);
        if (ErrorResult.Code != ErrorCode_Success)
        {
            return ErrorResult;
        }
        if (!GenericBuffer_WriteUChar(outBuffer, ReadByte))
        {
            return Error_Construct3(stream->ErrorPool,
                ErrorCode_BufferTooSmall,
                u8"Destination buffer into which to read the %s stream is too small to hold the next byte.",
                GetStreamTypeName(stream->_type));
        }
    }
    return Error_CreateSuccess();
}

// Functions.
Error IOStream_CreateFileStream(ErrorMessagePool* errorPool, void* fileData, IOStreamFlags flags, IOStream* stream)
{
    Memory_Zero(stream, sizeof(*stream));
    AssignStreamFields(stream, errorPool, IOStreamType_File, flags, fileData);

    IOStreamVTable* VTable = &stream->_vtable;
    VTable->_getPosition = &FileStreamGetPosition;
    VTable->_setPosition = &FileStreamSetPosition;
    VTable->_setPositionSpecial = &FileStreamSetPositionSpecial;
    VTable->_flush = &FileStreamFlush;
    VTable->_writeByte = &FileStreamWriteByte;
    VTable->_write = &FileStreamWrite;
    VTable->_readByte = &FileStreamReadByte;
    VTable->_read = &FileStreamRead;
    VTable->_close = &FileStreamClose;
}

Error IOStream_CreateMemoryStream(ErrorMessagePool* errorPool, IOStreamFlags flags, IOStream* stream)
{
    Memory_Zero(stream, sizeof(*stream));

    IOMemoryStreamData* MemStreamData = Memory_Allocate(sizeof(*MemStreamData));
    Memory_Zero(MemStreamData, sizeof(*MemStreamData));
    MemStreamData->_selfContainedBuffer = GenericBuffer_CreateVariable(
        NULL, 0, sizeof(unsigned char), 0, stream, &MemoryStreamAllocateCallback);
    MemStreamData->_buffer = &MemStreamData->_selfContainedBuffer;
    MemStreamData->_position = 0;

    AssignStreamFields(stream, errorPool, IOStreamType_Memory, flags, MemStreamData);
    InitMemStreamVTable(stream);
}

Error IOStream_CreateMemoryStreamWrapped(ErrorMessagePool* errorPool, GenericBuffer* bufferToWrap, IOStreamFlags flags, IOStream* stream)
{
    if (bufferToWrap->_elementSize != 1)
    {
        return Error_Construct3(errorPool,
            ErrorCode_IllegalArgument,
            u8"A memory stream which wraps a user-supplied buffer must have an element size of 1, got %zu instead.",
            bufferToWrap->_elementSize);
    }

    Memory_Zero(stream, sizeof(*stream));

    IOMemoryStreamData* MemStreamData = Memory_Allocate(sizeof(*MemStreamData));
    Memory_Zero(MemStreamData, sizeof(*MemStreamData));
    MemStreamData->_buffer = bufferToWrap;
    MemStreamData->_position = 0;

    AssignStreamFields(stream, errorPool, IOStreamType_Memory, flags, MemStreamData);
    InitMemStreamVTable(stream);
}

Error IOStream_CreateSocket(ErrorMessagePool* errorPool, void* data, IOStreamFlags flags, IOStreamVTable* vtable, IOStream* stream)
{
    Memory_Zero(stream, sizeof(stream));
    AssignStreamFields(stream, errorPool, IOStreamType_Socket, flags, data);
    stream->_vtable = *vtable;
}

Error IOStream_CreateCustom(ErrorMessagePool* errorPool, void* data, IOStreamFlags flags, IOStreamVTable* vtable, IOStream* stream)
{
    Memory_Zero(stream, sizeof(stream));
    AssignStreamFields(stream, errorPool, IOStreamType_Custom, flags, data);
    stream->_vtable = *vtable;
}

void IOStream_CreateFromStandardInput(ErrorMessagePool* errorPool, IOStream* stream)
{
    IOStream_CreateFileStream(errorPool,
        stdin,
        IOStreamFlags_CanRead,
        stream);
}

void IOStream_CreateFromStandardOutput(ErrorMessagePool* errorPool, IOStream* stream)
{
    IOStream_CreateFileStream(errorPool,
        stdout,
        IOStreamFlags_CanWrite,
        stream);
}

void IOStream_CreateFromStandardError(ErrorMessagePool* errorPool, IOStream* stream)
{
    IOStream_CreateFileStream(errorPool,
        stderr,
        IOStreamFlags_CanWrite,
        stream);
}

Error IOStream_SetPosition(IOStream* stream, size_t position)
{
    if (!IOStream_IsSeekable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot set %s stream position because the stream is not seekable.",
            GetStreamTypeName(stream->_type));
    }

    return (*stream->_vtable._setPosition)(stream, position);
}

Error IOStream_SetPositionSpecial(IOStream* stream, IOStreamSeekOrigin origin)
{
    if (!IOStream_IsSeekable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot set %s stream special position because the stream is not seekable.",
            GetStreamTypeName(stream->_type));
    }

    return (*stream->_vtable._setPositionSpecial)(stream, origin);
}

Error IOStream_WriteByte(IOStream* stream, unsigned char byte)
{
    if (!IOStream_IsWritable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot write a byte to a %s stream because the stream is not writable.",
            GetStreamTypeName(stream->_type));
    }

    return (*stream->_vtable._writeByte)(stream, byte);
}

Error IOStream_Write(IOStream* stream, const unsigned char* data, size_t dataSize)
{
    if (!IOStream_IsWritable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot bulk write to a %s stream because the stream is not writable.",
            GetStreamTypeName(stream->_type));
    }

    return (*stream->_vtable._write)(stream, data, dataSize);
}

Error IOStream_ReadByte(IOStream* stream, unsigned char* byte)
{
    if (!IOStream_IsReadable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot read a byte from a %s stream because the stream is not readable.",
            GetStreamTypeName(stream->_type));
    }

    return (*stream->_vtable._readByte)(stream, byte);
}

Error IOStream_Read(IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer)
{
    if (!IOStream_IsReadable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot bulk read from a %s stream because the stream is not readable.",
            GetStreamTypeName(stream->_type));
    }

    return (*stream->_vtable._read)(stream, outBuffer, bytesToRead);
}

Error IOStream_GetStreamSize(IOStream* stream, size_t* sizeBytes)
{
    if (!IOStream_IsSeekable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot get the size of a %s stream because it is not seekable.",
            GetStreamTypeName(stream->_type));
    }

    size_t CurPos;
    Error ErrorResult = IOStream_GetPosition(stream, &CurPos);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    Error ErrorResult = IOStream_SetPositionSpecial(stream, IOStreamSeekOrigin_End);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }
    
    Error ErrorResult = IOStream_GetPosition(stream, sizeBytes);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    return IOStream_SetPosition(stream, CurPos);
}

Error IOStream_GetStreamSizeRemaining(IOStream* stream, size_t* sizeBytes)
{
    if (!IOStream_IsSeekable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot get the remaining size of a %s stream because it is not seekable.",
            GetStreamTypeName(stream->_type));
    }

    size_t TotalSize;
    Error ErrorResult = IOStream_GetStreamSize(stream, &TotalSize);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }
    
    size_t Position;
    ErrorResult = IOStream_GetPosition(stream, &Position);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    *sizeBytes = TotalSize - Position;
    return Error_CreateSuccess();
}

Error IOStream_Move(IOStream* stream, int64_t amount)
{
    size_t CurPos;
    Error ErrorResult = IOStream_GetPosition(stream, &CurPos);
    if (ErrorResult.Code != ErrorCode_Success)
    {
        return ErrorResult;
    }

    size_t ClampedCurPos = CurPos > INT64_MAX ? INT64_MAX : CurPos;
    size_t NewPos = (size_t)Math_ClampInt64((int64_t)ClampedCurPos + amount, 0, INT64_MAX);
    return IOStream_SetPosition(stream, NewPos);
}

Error IOStream_WriteString(IOStream* stream, const unsigned char* str)
{
    IOStream_Write(stream, str, strlen(str));
}

Error IOStream_ReadAll(IOStream* stream, GenericBuffer* buffer)
{
    if (!IOStream_IsReadable(stream))
    {
        return Error_Construct3(stream->ErrorPool,
            ErrorCode_InvalidOperation,
            u8"Cannot read all data from a %s stream because it is not readable.",
            GetStreamTypeName(stream->_type));
    }

    if (IOStream_IsSeekable(stream))
    {
        return ReadAllFromSeekable(stream, buffer);
    }
    return ReadAllFromNonSeekable(stream ,buffer);
}

void IOStream_Deconstruct(IOStream* stream)
{
    if (stream->_type == IOStreamType_Memory)
    {
        IOMemoryStreamData* MemStreamData = stream->_data;
        if (MemStreamData->_selfContainedBuffer._data)
        {
            Memory_Free(MemStreamData->_selfContainedBuffer._data);
        }
    }
}