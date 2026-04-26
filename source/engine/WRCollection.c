#include "WRCollection.h"


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Collection enumerator argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateReferenceEnumerationUnsupportedError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"This collection enumerator does not support returning elements by reference.");
}


// Public functions.
Error CollectionEnumerator_NextByReference(CollectionEnumerator* self, void** outPointer)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outPointer == NULL)
    {
        return CreateNullArgumentError(u8"outPointer");
    }
    if (!CollectionEnumerator_IsReferenceReturningSupported(self))
    {
        *outPointer = NULL;
        return CreateReferenceEnumerationUnsupportedError();
    }

    return (*self->_vtable._nextByReference)(self->_vtable.Self, outPointer);
}
