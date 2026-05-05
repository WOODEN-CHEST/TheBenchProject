#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRMemory.h"
#include "WRError.h"


// Types.
typedef enum IOStreamTypeEnum
{
    IOStreamType_Unknown = 0,
    IOStreamType_File,
    IOStreamType_Memory,
    IOStreamType_Socket,
} IOStreamType;

typedef enum IOStreamFlagsEnum
{
    IOStreamFlags_None = 0,
    IOStreamFlags_CanWrite = (1 << 0),
    IOStreamFlags_CanRead = (1 << 1),
    IOStreamFlags_CanSeek = (1 << 2),
    IOStreamFlags_CanSetLength = (1 << 3),
} IOStreamFlags;

typedef enum IOStreamSeekOriginEnum
{
    IOStreamSeekOrigin_Start,
    IOStreamSeekOrigin_End,
} IOStreamSeekOrigin;

typedef struct IOStreamStruct IOStream;

typedef struct IOStreamVTableStruct
{
    void* Self;
    Error (*_getPosition)(void* self, size_t* position);
    Error (*_setPosition)(void* self, size_t position);
    Error (*_setPositionSpecial)(void* self, IOStreamSeekOrigin origin);
    Error (*_setLength)(void* self, size_t length);
    Error (*_flush)(void* self);
    Error (*_writeByte)(void* self, unsigned char byte);
    Error (*_write)(void* self, const unsigned char* buffer, size_t bufferSize);
    Error (*_readByte)(void* self, unsigned char* byte);
    Error (*_read)(void* self, GenericBuffer* dest, size_t readSize);
    Error (*_close)(void* self);
    bool (*_isEOF)(void* self);
    Error (*_deconstruct)(void* self);
} IOStreamVTable;

struct IOStreamStruct
{
    IOStreamType _type;
    IOStreamFlags _flags;
    IOStreamVTable _vtable;
};


// Functions.
static inline Error IOStream_GetPosition(IOStream* stream, size_t* position)
{
    return (*stream->_vtable._getPosition)(stream->_vtable.Self, position);
}

static inline Error IOStream_Flush(IOStream* stream)
{
    return (*stream->_vtable._flush)(stream->_vtable.Self);
}

static inline Error IOStream_Close(IOStream* stream)
{
    return (*stream->_vtable._close)(stream->_vtable.Self);
}

static inline bool IOStream_IsSeekable(IOStream* stream)
{
    return ((stream->_flags & IOStreamFlags_CanSeek) != 0);
}

static inline bool IOStream_IsWritable(IOStream* stream)
{
    return ((stream->_flags & IOStreamFlags_CanWrite) != 0);
}

static inline bool IOStream_IsReadable(IOStream* stream)
{
    return ((stream->_flags & IOStreamFlags_CanRead) != 0);
}

static inline bool IOStream_IsLengthSettable(IOStream* stream)
{
    return ((stream->_flags & IOStreamFlags_CanSetLength) != 0);
}

static inline bool IOStream_IsEndOfStream(IOStream* stream)
{
    return (*stream->_vtable._isEOF)(stream->_vtable.Self);
}

Error IOStream_SetPosition(IOStream* stream, size_t position);

Error IOStream_SetPositionSpecial(IOStream* stream, IOStreamSeekOrigin origin);

Error IOStream_SetLength(IOStream* stream, size_t length);

Error IOStream_WriteByte(IOStream* stream, unsigned char byte);

Error IOStream_Write(IOStream* stream, const unsigned char* data, size_t dataSize);

Error IOStream_ReadByte(IOStream* stream, unsigned char* byte);

Error IOStream_Read(IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer);

Error IOStream_GetStreamSizeTotal(IOStream* stream, size_t* sizeBytes);

Error IOStream_GetStreamSizeRemaining(IOStream* stream, size_t* sizeBytes);

Error IOStream_Move(IOStream* stream, int64_t amount);

Error IOStream_WriteString(IOStream* stream, const unsigned char* str);

Error IOStream_ReadAll(IOStream* stream, GenericBuffer* buffer);

Error IOStream_Deconstruct(IOStream* stream);
