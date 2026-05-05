#pragma once
#include "WRIO.h"


// Types.
typedef struct FileStreamStruct
{
    IOStream Base;
    void* _nativeHandle;
    bool _ownsHandle;
    bool _isClosed;
} FileStream;


// Functions.
static inline IOStream* FileStream_AsIOStream(FileStream* self)
{
    return &self->Base;
}

Error FileStream_ConstructFromHandle(FileStream* self, void* nativeHandle, IOStreamFlags flags, bool ownsHandle);

Error FileStream_Deconstruct(FileStream* self);
