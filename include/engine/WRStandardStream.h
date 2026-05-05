#pragma once
#include "WRFileStream.h"


// Types.
typedef struct StandardStreamStruct
{
    FileStream Base;
} StandardStream;


// Functions.
static inline IOStream* StandardStream_AsIOStream(StandardStream* self)
{
    return FileStream_AsIOStream(&self->Base);
}

Error StandardStream_CreateFromStandardInput(StandardStream* self);

Error StandardStream_CreateFromStandardOutput(StandardStream* self);

Error StandardStream_CreateFromStandardError(StandardStream* self);

Error StandardStream_Deconstruct(StandardStream* self);
