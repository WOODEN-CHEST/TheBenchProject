#include "WRFileStream.h"
#include "WRMemory.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>


// Macros.


// Types.


// Fields.
static const IOStreamVTable FILE_STREAM_VTABLE =
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
static FILE* GetHandle(FileStream* self)
{
    return self->_nativeHandle;
}

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"File stream argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateClosedStreamError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Cannot %s a closed file stream.",
        operationName);
}

static Error CreateOutOfRangeError(size_t position)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Cannot seek the file stream to position %zu because it exceeds the supported range.",
        position);
}

static Error CreateIOError(const unsigned char* operationName, int errorCode)
{
    return Error_Construct3(ErrorCode_IO,
        u8"Failed to %s the file stream. Native error code %d.",
        operationName,
        errorCode);
}

static Error FileStream_GetPosition(void* selfVoid, size_t* position)
{
    FileStream* self = selfVoid;
    long Position = 0;

    if (position == NULL)
    {
        return CreateNullArgumentError(u8"position");
    }
    if (self->_isClosed)
    {
        return CreateClosedStreamError(u8"get the position of");
    }

    errno = 0;
    Position = ftell(GetHandle(self));
    if (Position < 0)
    {
        return CreateIOError(u8"get the position of", errno);
    }

    *position = (size_t)Position;
    return Error_CreateSuccess();
}

static Error FileStream_SetPosition(void* selfVoid, size_t position)
{
    FileStream* self = selfVoid;

    if (self->_isClosed)
    {
        return CreateClosedStreamError(u8"set the position of");
    }
    if (position > (size_t)LONG_MAX)
    {
        return CreateOutOfRangeError(position);
    }

    errno = 0;
    if (fseek(GetHandle(self), (long)position, SEEK_SET) != 0)
    {
        return CreateIOError(u8"set the position of", errno);
    }

    return Error_CreateSuccess();
}

static Error FileStream_SetPositionSpecial(void* selfVoid, IOStreamSeekOrigin origin)
{
    FileStream* self = selfVoid;
    int NativeOrigin = 0;

    if (self->_isClosed)
    {
        return CreateClosedStreamError(u8"set the special position of");
    }

    switch (origin)
    {
        case IOStreamSeekOrigin_Start:
            NativeOrigin = SEEK_SET;
            break;

        case IOStreamSeekOrigin_End:
            NativeOrigin = SEEK_END;
            break;

        default:
            return Error_Construct3(ErrorCode_IllegalArgument,
                u8"Unsupported file stream seek origin %d.",
                (int)origin);
    }

    errno = 0;
    if (fseek(GetHandle(self), 0, NativeOrigin) != 0)
    {
        return CreateIOError(u8"set the special position of", errno);
    }

    return Error_CreateSuccess();
}

static Error FileStream_Flush(void* selfVoid)
{
    FileStream* self = selfVoid;

    if (self->_isClosed)
    {
        return CreateClosedStreamError(u8"flush");
    }
    if (!IOStream_IsWritable(&self->Base))
    {
        return Error_CreateSuccess();
    }

    errno = 0;
    if (fflush(GetHandle(self)) != 0)
    {
        return CreateIOError(u8"flush", errno);
    }

    return Error_CreateSuccess();
}

static Error FileStream_WriteByte(void* selfVoid, unsigned char byte)
{
    FileStream* self = selfVoid;
    int Result = 0;

    if (self->_isClosed)
    {
        return CreateClosedStreamError(u8"write to");
    }

    errno = 0;
    Result = fputc((int)byte, GetHandle(self));
    if (Result == EOF)
    {
        return CreateIOError(u8"write a byte to", ferror(GetHandle(self)) != 0 ? ferror(GetHandle(self)) : errno);
    }

    return Error_CreateSuccess();
}

static Error FileStream_Write(void* selfVoid, const unsigned char* buffer, size_t bufferSize)
{
    FileStream* self = selfVoid;
    size_t WrittenCount = 0;

    if ((buffer == NULL) && (bufferSize > 0))
    {
        return CreateNullArgumentError(u8"buffer");
    }
    if (self->_isClosed)
    {
        return CreateClosedStreamError(u8"write to");
    }
    if (bufferSize == 0)
    {
        return Error_CreateSuccess();
    }

    WrittenCount = fwrite(buffer, sizeof(unsigned char), bufferSize, GetHandle(self));
    if (WrittenCount < bufferSize)
    {
        return Error_Construct3(ErrorCode_IO,
            u8"Failed to write all bytes to the file stream. Wrote %zu bytes out of %zu.",
            WrittenCount,
            bufferSize);
    }

    return Error_CreateSuccess();
}

static Error FileStream_ReadByte(void* selfVoid, unsigned char* byte)
{
    FileStream* self = selfVoid;
    int Result = 0;

    if (byte == NULL)
    {
        return CreateNullArgumentError(u8"byte");
    }
    if (self->_isClosed)
    {
        return CreateClosedStreamError(u8"read from");
    }

    Result = fgetc(GetHandle(self));
    if (Result == EOF)
    {
        if (feof(GetHandle(self)) != 0)
        {
            return Error_Construct1(ErrorCode_IO, u8"Cannot read past the end of the file stream.");
        }

        return CreateIOError(u8"read a byte from", ferror(GetHandle(self)));
    }

    *byte = (unsigned char)Result;
    return Error_CreateSuccess();
}

static Error FileStream_Read(void* selfVoid, GenericBuffer* dest, size_t readSize)
{
    FileStream* self = selfVoid;
    size_t ReadCount = 0;
    unsigned char* Destination = NULL;

    if (dest == NULL)
    {
        return CreateNullArgumentError(u8"dest");
    }
    if (dest->_elementSize != sizeof(unsigned char))
    {
        return Error_Construct3(ErrorCode_IllegalArgument,
            u8"File stream reads require a byte buffer destination, got element size %zu.",
            dest->_elementSize);
    }
    if (self->_isClosed)
    {
        return CreateClosedStreamError(u8"read from");
    }
    if (readSize == 0)
    {
        return Error_CreateSuccess();
    }
    if (!GenericBuffer_TryPrepareForManualMutation(dest, readSize))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Destination buffer is too small to read %zu bytes from the file stream.",
            readSize);
    }

    Destination = dest->_data + dest->_count;
    ReadCount = fread(Destination, sizeof(unsigned char), readSize, GetHandle(self));
    dest->_count += ReadCount;

    if ((ReadCount < readSize) && (ferror(GetHandle(self)) != 0))
    {
        return CreateIOError(u8"read from", ferror(GetHandle(self)));
    }

    return Error_CreateSuccess();
}

static Error FileStream_Close(void* selfVoid)
{
    FileStream* self = selfVoid;
    Error Result = Error_CreateSuccess();

    if (self->_isClosed)
    {
        return Error_CreateSuccess();
    }

    if (self->_ownsHandle && IOStream_IsWritable(&self->Base))
    {
        Result = FileStream_Flush(self);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    if (self->_ownsHandle && (self->_nativeHandle != NULL))
    {
        errno = 0;
        if (fclose(GetHandle(self)) != 0)
        {
            return CreateIOError(u8"close", errno);
        }
    }

    self->_nativeHandle = NULL;
    self->_isClosed = true;
    return Error_CreateSuccess();
}

static bool FileStream_IsEOF(void* selfVoid)
{
    FileStream* self = selfVoid;

    if (self->_isClosed || (self->_nativeHandle == NULL))
    {
        return true;
    }

    return (feof(GetHandle(self)) != 0);
}

static Error FileStream_VTableDeconstruct(void* selfVoid)
{
    FileStream* self = selfVoid;
    Error Result = Error_CreateSuccess();

    Result = FileStream_Close(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Zero(self, sizeof(*self));
    return Error_CreateSuccess();
}

static IOStreamVTable CreateFileStreamVTable(FileStream* self)
{
    IOStreamVTable Result = FILE_STREAM_VTABLE;
    Result.Self = self;
    Result._getPosition = &FileStream_GetPosition;
    Result._setPosition = &FileStream_SetPosition;
    Result._setPositionSpecial = &FileStream_SetPositionSpecial;
    Result._flush = &FileStream_Flush;
    Result._writeByte = &FileStream_WriteByte;
    Result._write = &FileStream_Write;
    Result._readByte = &FileStream_ReadByte;
    Result._read = &FileStream_Read;
    Result._close = &FileStream_Close;
    Result._isEOF = &FileStream_IsEOF;
    Result._deconstruct = &FileStream_VTableDeconstruct;
    return Result;
}


// Public functions.
Error FileStream_ConstructFromHandle(FileStream* self, void* nativeHandle, IOStreamFlags flags, bool ownsHandle)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (nativeHandle == NULL)
    {
        return CreateNullArgumentError(u8"nativeHandle");
    }

    Memory_Zero(self, sizeof(*self));
    self->Base._type = IOStreamType_File;
    self->Base._flags = flags;
    self->Base._vtable = CreateFileStreamVTable(self);
    self->_nativeHandle = nativeHandle;
    self->_ownsHandle = ownsHandle;
    self->_isClosed = false;
    return Error_CreateSuccess();
}

Error FileStream_Deconstruct(FileStream* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    return FileStream_VTableDeconstruct(self);
}
