#include "WRStandardStream.h"
#include <stdio.h>



// Static functions.
static Error CreateNullArgumentError(void)
{
    return Error_Construct1(ErrorCode_IllegalArgument, u8"Standard stream self must not be null.");
}


// Public functions.
Error StandardStream_CreateFromStandardInput(StandardStream* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError();
    }

    return FileStream_ConstructFromHandle(&self->Base, stdin, IOStreamFlags_CanRead, false);
}

Error StandardStream_CreateFromStandardOutput(StandardStream* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError();
    }

    return FileStream_ConstructFromHandle(&self->Base, stdout, IOStreamFlags_CanWrite, false);
}

Error StandardStream_CreateFromStandardError(StandardStream* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError();
    }

    return FileStream_ConstructFromHandle(&self->Base, stderr, IOStreamFlags_CanWrite, false);
}

Error StandardStream_Deconstruct(StandardStream* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    return FileStream_Deconstruct(&self->Base);
}
