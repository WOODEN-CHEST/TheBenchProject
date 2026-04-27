#pragma once
#include "WRIO.h"


// Types.
typedef struct MemoryStreamStruct
{
    IOStream Base;
    GenericBuffer _selfContainedBuffer;
    GenericBuffer* _buffer;
    size_t _position;
    bool _ownsBuffer;
    bool _isClosed;
} MemoryStream;


// Functions.
static inline IOStream* MemoryStream_AsIOStream(MemoryStream* self)
{
    return &self->Base;
}

static inline GenericBuffer* MemoryStream_GetBuffer(MemoryStream* self)
{
    return self->_buffer;
}

Error MemoryStream_Construct1(MemoryStream* self, IOStreamFlags flags);

Error MemoryStream_Construct2(MemoryStream* self, GenericBuffer* bufferToWrap, IOStreamFlags flags);

Error MemoryStream_SetLength(MemoryStream* self, size_t length);

void MemoryStream_Deconstruct(MemoryStream* self);
