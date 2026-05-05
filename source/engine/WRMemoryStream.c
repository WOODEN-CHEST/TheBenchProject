#include "WRMemoryStream.h"
#include "WRMemory.h"


// Macros.


// Types.


// Fields.
static const size_t MEMORY_STREAM_CAPACITY_DEFAULT = 512;
static const size_t MEMORY_STREAM_CAPACITY_GROWTH = 2;
static const IOStreamVTable MEMORY_STREAM_VTABLE =
{
    .Self = NULL,
    ._getPosition = NULL,
    ._setPosition = NULL,
    ._setPositionSpecial = NULL,
    ._setLength = NULL,
    ._flush = NULL,
    ._writeByte = NULL,
    ._write = NULL,
    ._readByte = NULL,
    ._read = NULL,
    ._close = NULL,
    ._isEOF = NULL,
    ._deconstruct = NULL,
};


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Memory stream argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateClosedStreamError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Cannot %s a closed memory stream.",
        operationName);
}

static Error CreateOverflowError(size_t position, size_t size)
{
    return Error_Construct3(ErrorCode_BufferTooLarge,
        u8"Cannot access %zu bytes at memory stream position %zu because the resulting size would overflow.",
        size,
        position);
}

static Error CreateByteBufferRequiredError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Memory stream argument \"%s\" must be a byte buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static bool MemoryStream_Allocate(GenericBuffer* destination, size_t requestedCapacity)
{
    size_t NewCapacity = 0;
    size_t NewSize = 0;

    if (requestedCapacity <= destination->_capacity)
    {
        return true;
    }
    if (GenericBuffer_IsFixedCapacity(destination))
    {
        return false;
    }

    NewCapacity = (destination->_capacity == 0) ? MEMORY_STREAM_CAPACITY_DEFAULT : destination->_capacity;
    while (NewCapacity < requestedCapacity)
    {
        NewCapacity *= MEMORY_STREAM_CAPACITY_GROWTH;
    }

    if (NewCapacity > (SIZE_MAX / destination->_elementSize))
    {
        return false;
    }

    NewSize = NewCapacity * destination->_elementSize;
    destination->_data = (destination->_data == NULL)
        ? Memory_Allocate(NewSize)
        : Memory_Reallocate(destination->_data, NewSize);
    destination->_capacity = NewCapacity;
    return true;
}

static Error MemoryStream_EnsureOpen(MemoryStream* self, const unsigned char* operationName)
{
    if (self->_isClosed)
    {
        return CreateClosedStreamError(operationName);
    }

    return Error_CreateSuccess();
}

static Error MemoryStream_EnsureWritableBytes(MemoryStream* self, size_t writeSize)
{
    size_t WriteEnd = 0;
    size_t AddedElementCount = 0;

    if (GenericBuffer_IsReadOnly(self->_buffer))
    {
        return Error_Construct1(ErrorCode_InvalidOperation, u8"Cannot write to a read-only memory stream buffer.");
    }
    if (writeSize > (SIZE_MAX - self->_position))
    {
        return CreateOverflowError(self->_position, writeSize);
    }

    WriteEnd = self->_position + writeSize;
    if (WriteEnd > self->_buffer->_count)
    {
        AddedElementCount = WriteEnd - self->_buffer->_count;
    }

    if (!GenericBuffer_TryPrepareForManualMutation(self->_buffer, AddedElementCount))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Memory stream buffer is too small to hold %zu bytes at position %zu.",
            writeSize,
            self->_position);
    }

    return Error_CreateSuccess();
}

static Error MemoryStream_GetPosition(void* selfVoid, size_t* position)
{
    MemoryStream* self = selfVoid;

    if (position == NULL)
    {
        return CreateNullArgumentError(u8"position");
    }

    *position = self->_position;
    return Error_CreateSuccess();
}

static Error MemoryStream_SetPosition(void* selfVoid, size_t position)
{
    MemoryStream* self = selfVoid;
    Error Result = MemoryStream_EnsureOpen(self, u8"set the position of");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_position = (position > self->_buffer->_count) ? self->_buffer->_count : position;
    return Error_CreateSuccess();
}

static Error MemoryStream_SetPositionSpecial(void* selfVoid, IOStreamSeekOrigin origin)
{
    MemoryStream* self = selfVoid;
    Error Result = MemoryStream_EnsureOpen(self, u8"set the special position of");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    switch (origin)
    {
        case IOStreamSeekOrigin_Start:
            self->_position = 0;
            return Error_CreateSuccess();

        case IOStreamSeekOrigin_End:
            self->_position = self->_buffer->_count;
            return Error_CreateSuccess();

        default:
            return Error_Construct3(ErrorCode_IllegalArgument,
                u8"Unsupported memory stream seek origin %d.",
                (int)origin);
    }
}

static Error MemoryStream_Flush(void* selfVoid)
{
    MemoryStream* self = selfVoid;
    return MemoryStream_EnsureOpen(self, u8"flush");
}

static Error MemoryStream_SetLengthVTable(void* selfVoid, size_t length)
{
    MemoryStream* self = selfVoid;
    return MemoryStream_SetLength(self, length);
}

static Error MemoryStream_WriteByte(void* selfVoid, unsigned char byte)
{
    MemoryStream* self = selfVoid;
    Error Result = MemoryStream_EnsureOpen(self, u8"write to");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = MemoryStream_EnsureWritableBytes(self, 1);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_buffer->_data[self->_position] = byte;
    self->_position++;
    if (self->_buffer->_count < self->_position)
    {
        self->_buffer->_count = self->_position;
    }

    return Error_CreateSuccess();
}

static Error MemoryStream_Write(void* selfVoid, const unsigned char* buffer, size_t bufferSize)
{
    MemoryStream* self = selfVoid;
    Error Result = MemoryStream_EnsureOpen(self, u8"write to");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((buffer == NULL) && (bufferSize > 0))
    {
        return CreateNullArgumentError(u8"buffer");
    }
    if (bufferSize == 0)
    {
        return Error_CreateSuccess();
    }

    Result = MemoryStream_EnsureWritableBytes(self, bufferSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Copy(buffer, self->_buffer->_data + self->_position, bufferSize);
    self->_position += bufferSize;
    if (self->_buffer->_count < self->_position)
    {
        self->_buffer->_count = self->_position;
    }

    return Error_CreateSuccess();
}

static Error MemoryStream_ReadByte(void* selfVoid, unsigned char* byte)
{
    MemoryStream* self = selfVoid;
    Error Result = MemoryStream_EnsureOpen(self, u8"read from");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (byte == NULL)
    {
        return CreateNullArgumentError(u8"byte");
    }
    if (self->_position >= self->_buffer->_count)
    {
        return Error_Construct1(ErrorCode_IO, u8"Cannot read past the end of the memory stream.");
    }

    *byte = self->_buffer->_data[self->_position];
    self->_position++;
    return Error_CreateSuccess();
}

static Error MemoryStream_Read(void* selfVoid, GenericBuffer* dest, size_t readSize)
{
    MemoryStream* self = selfVoid;
    size_t Available = 0;
    size_t ActualReadSize = 0;
    Error Result = MemoryStream_EnsureOpen(self, u8"read from");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (dest == NULL)
    {
        return CreateNullArgumentError(u8"dest");
    }
    if (dest->_elementSize != sizeof(unsigned char))
    {
        return CreateByteBufferRequiredError(u8"dest", dest->_elementSize);
    }
    if (readSize == 0)
    {
        return Error_CreateSuccess();
    }

    Available = self->_buffer->_count - self->_position;
    ActualReadSize = (readSize < Available) ? readSize : Available;
    if (ActualReadSize == 0)
    {
        return Error_CreateSuccess();
    }
    if (!GenericBuffer_TryPrepareForManualMutation(dest, ActualReadSize))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Destination buffer is too small to read %zu bytes from the memory stream.",
            ActualReadSize);
    }

    Memory_Copy(self->_buffer->_data + self->_position, dest->_data + dest->_count, ActualReadSize);
    dest->_count += ActualReadSize;
    self->_position += ActualReadSize;
    return Error_CreateSuccess();
}

static Error MemoryStream_Close(void* selfVoid)
{
    MemoryStream* self = selfVoid;
    self->_isClosed = true;
    return Error_CreateSuccess();
}

static bool MemoryStream_IsEOF(void* selfVoid)
{
    MemoryStream* self = selfVoid;
    return (self->_position >= self->_buffer->_count);
}

static Error MemoryStream_VTableDeconstruct(void* selfVoid)
{
    MemoryStream* self = selfVoid;

    Error Result = MemoryStream_Close(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (self->_ownsBuffer && (self->_selfContainedBuffer._data != NULL))
    {
        Memory_Free(self->_selfContainedBuffer._data);
    }

    Memory_Zero(self, sizeof(*self));
    return Error_CreateSuccess();
}

static IOStreamVTable CreateMemoryStreamVTable(MemoryStream* self)
{
    IOStreamVTable Result = MEMORY_STREAM_VTABLE;
    Result.Self = self;
    Result._getPosition = &MemoryStream_GetPosition;
    Result._setPosition = &MemoryStream_SetPosition;
    Result._setPositionSpecial = &MemoryStream_SetPositionSpecial;
    Result._setLength = &MemoryStream_SetLengthVTable;
    Result._flush = &MemoryStream_Flush;
    Result._writeByte = &MemoryStream_WriteByte;
    Result._write = &MemoryStream_Write;
    Result._readByte = &MemoryStream_ReadByte;
    Result._read = &MemoryStream_Read;
    Result._close = &MemoryStream_Close;
    Result._isEOF = &MemoryStream_IsEOF;
    Result._deconstruct = &MemoryStream_VTableDeconstruct;
    return Result;
}


// Public functions.
Error MemoryStream_Construct1(MemoryStream* self, IOStreamFlags flags)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));
    self->Base._type = IOStreamType_Memory;
    self->Base._flags = flags | IOStreamFlags_CanSeek | IOStreamFlags_CanSetLength;
    self->Base._vtable = CreateMemoryStreamVTable(self);
    GenericBuffer_CreateVariable(&self->_selfContainedBuffer,
        NULL,
        0,
        sizeof(unsigned char),
        0,
        self,
        &MemoryStream_Allocate);
    self->_buffer = &self->_selfContainedBuffer;
    self->_position = 0;
    self->_ownsBuffer = true;
    self->_isClosed = false;
    return Error_CreateSuccess();
}

Error MemoryStream_SetLength(MemoryStream* self, size_t length)
{
    Error Result = Error_CreateSuccess();
    size_t PreviousCount = 0;
    size_t AddedCount = 0;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = MemoryStream_EnsureOpen(self, u8"set the length of");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (GenericBuffer_IsReadOnly(self->_buffer))
    {
        return Error_Construct1(ErrorCode_InvalidOperation, u8"Cannot set the length of a read-only memory stream buffer.");
    }

    PreviousCount = self->_buffer->_count;
    if (length < PreviousCount)
    {
        self->_buffer->_count = length;
        if (self->_position > length)
        {
            self->_position = length;
        }

        return Error_CreateSuccess();
    }
    if (length == PreviousCount)
    {
        return Error_CreateSuccess();
    }

    AddedCount = length - PreviousCount;
    if (!GenericBuffer_TryPrepareForManualMutation(self->_buffer, AddedCount))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Memory stream buffer is too small to set its length to %zu bytes.",
            length);
    }

    Memory_Zero(self->_buffer->_data + PreviousCount, AddedCount);
    self->_buffer->_count = length;
    return Error_CreateSuccess();
}

Error MemoryStream_Construct2(MemoryStream* self, GenericBuffer* bufferToWrap, IOStreamFlags flags)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (bufferToWrap == NULL)
    {
        return CreateNullArgumentError(u8"bufferToWrap");
    }
    if (bufferToWrap->_elementSize != sizeof(unsigned char))
    {
        return CreateByteBufferRequiredError(u8"bufferToWrap", bufferToWrap->_elementSize);
    }
    if (GenericBuffer_IsReadOnly(bufferToWrap) && ((flags & IOStreamFlags_CanWrite) != 0))
    {
        return Error_Construct1(ErrorCode_InvalidOperation, u8"Cannot create a writable memory stream over a read-only buffer.");
    }

    Memory_Zero(self, sizeof(*self));
    self->Base._type = IOStreamType_Memory;
    self->Base._flags = flags | IOStreamFlags_CanSeek | IOStreamFlags_CanSetLength;
    self->Base._vtable = CreateMemoryStreamVTable(self);
    self->_buffer = bufferToWrap;
    self->_position = 0;
    self->_ownsBuffer = false;
    self->_isClosed = false;
    return Error_CreateSuccess();
}

void MemoryStream_Deconstruct(MemoryStream* self)
{
    if (self == NULL)
    {
        return;
    }

    MemoryStream_VTableDeconstruct(self);
}
