#pragma once
#include <stddef.h>
#include <stdint.h>
#include <WRMemory.h>
#include "WRError.h"
#include <stdint.h>



// Types.
typedef enum IOStreamTypeEnum
{
    IOStreamType_Unknown = 0,
    IOStreamType_File,
    IOStreamType_Memory,
} IOStreamType;

typedef enum IOStreamFlagsEnum
{
    IOStreamFlags_None = 0,
    IOStreamFlags_CanWrite = 1 << 0,
    IOStreamFlags_CanRead = 1 << 1,
    IOStreamFlags_CanSeek = 1 << 2
} IOStreamFlags;

typedef enum IOStreamSeekOriginEnum
{
    IOStreamSeekOrigin_Start,
    IOStreamSeekOrigin_End,
} IOStreamSeekOrigin;

typedef struct IOStreamStruct IOStream;

struct IOStreamStruct
{
    IOStreamType _type;
    IOStreamFlags _flags;
    void* _data;
    ErrorMessagePool* ErrorPool;
    Error (*_getPosition)(IOStream* stream, size_t* position);
    Error (*_setPosition)(IOStream* stream, size_t position);
    Error (*_setPositionSpecial)(IOStream* stream, IOStreamSeekOrigin origin);
    Error (*_flush)(IOStream* stream);
    Error (*_writeByte)(IOStream* stream, unsigned char byte);
    Error (*_write)(IOStream* stream, const unsigned char* buffer, size_t bufferSize);
    Error (*_readByte)(IOStream* stream, const unsigned char* byte);
    Error (*_read)(IOStream* stream, GenericBuffer* dest, size_t readSize);
    Error (*_close)(IOStream* stream);
    bool (*isEOF)(IOStream* stream);
};


// Functions.
static inline Error IOStream_GetPosition(IOStream* stream, size_t* position)
{
    return (*stream->_getPosition)(stream, position);
}

static inline Error IOStream_Flush(IOStream* stream)
{
    return (*stream->_flush)(stream);
}

static inline Error IOStream_Close(IOStream* stream)
{
    return (*stream->_close)(stream);
}

static inline bool IOStream_IsSeekable(IOStream* stream)
{
    return (stream->_flags & IOStreamFlags_CanSeek);
}

static inline bool IOStream_IsWritable(IOStream* stream)
{
    return (stream->_flags & IOStreamFlags_CanWrite);
}

static inline bool IOStream_IsReadable(IOStream* stream)
{
    return (stream->_flags & IOStreamFlags_CanRead);
}

static inline bool IOStream_IsEndOfStream(IOStream* stream)
{
    return (*stream->isEOF)(stream);
}


Error IOStream_SetPosition(IOStream* stream, size_t position);

Error IOStream_SetPositionSpecial(IOStream* stream, IOStreamSeekOrigin origin);

Error IOStream_WriteByte(IOStream* stream, unsigned char byte);

Error IOStream_Write(IOStream* stream, const unsigned char* data, size_t dataSize);

Error IOStream_ReadByte(IOStream* stream, unsigned char* byte);

Error IOStream_Read(IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer);


Error IOStream_CreateFileStream(ErrorMessagePool* errorPool, void* fileData, IOStreamFlags flags, IOStream* stream);

Error IOStream_CreateMemoryStream(ErrorMessagePool* errorPool, IOStreamFlags flags, IOStream* stream);

Error IOStream_CreateMemoryStreamWrapped(ErrorMessagePool* errorPool, GenericBuffer* bufferToWrap, IOStreamFlags flags, IOStream* stream);

void IOStream_CreateFromStandardInput(ErrorMessagePool* errorPool, IOStream* stream);

void IOStream_CreateFromStandardOutput(ErrorMessagePool* errorPool, IOStream* stream);

void IOStream_CreateFromStandardError(ErrorMessagePool* errorPool, IOStream* stream);

Error IOStream_GetStreamSize(IOStream* stream, size_t* sizeBytes);

Error IOStream_GetStreamSizeRemaining(IOStream* stream, size_t* sizeBytes);

Error IOStream_Move(IOStream* stream, int64_t amount);

Error IOStream_WriteString(IOStream* stream, const unsigned char* str);

Error IOStream_ReadAll(IOStream* stream, GenericBuffer* buffer);

void IOStream_Deconstruct(IOStream* stream);