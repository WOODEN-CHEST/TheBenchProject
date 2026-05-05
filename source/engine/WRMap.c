#include "WRMap.h"


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Map argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateReadOnlyError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The map is read-only.");
}

static Error ValidateMap(IMap* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Error_CreateSuccess();
}

static Error ValidateWritableMap(IMap* self)
{
    Error Result = ValidateMap(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (IMap_IsReadOnly(self))
    {
        return CreateReadOnlyError();
    }

    return Error_CreateSuccess();
}

static Error ValidateMapAndKey(IMap* self, const void* key)
{
    Error Result = ValidateMap(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (key == NULL)
    {
        return CreateNullArgumentError(u8"key");
    }

    return Error_CreateSuccess();
}

static Error ValidateMapAndValue(IMap* self, const void* value)
{
    Error Result = ValidateMap(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    return Error_CreateSuccess();
}

static Error ValidateMapAndOutput(IMap* self, void* output, const unsigned char* outputName)
{
    Error Result = ValidateMap(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (output == NULL)
    {
        return CreateNullArgumentError(outputName);
    }

    return Error_CreateSuccess();
}


// Public functions.
Error IMap_GetElement(IMap* self, const void* key, void* outValue)
{
    Error Result = ValidateMapAndKey(self, key);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateMapAndOutput(self, outValue, u8"outValue");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._getElement(self->_vtable.Self, key, outValue);
}

Error IMap_GetPointerToElement(IMap* self, const void* key, void** outValue)
{
    Error Result = ValidateMapAndKey(self, key);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateMapAndOutput(self, outValue, u8"outValue");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._getPointerToElement(self->_vtable.Self, key, outValue);
}

Error IMap_Add(IMap* self, const void* key, const void* value, bool* outWasAdded)
{
    Error Result = ValidateMapAndKey(self, key);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateMapAndValue(self, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateMapAndOutput(self, outWasAdded, u8"outWasAdded");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateWritableMap(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._add(self->_vtable.Self, key, value, outWasAdded);
}

Error IMap_Remove(IMap* self, const void* key, bool* outWasRemoved)
{
    Error Result = ValidateMapAndKey(self, key);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateMapAndOutput(self, outWasRemoved, u8"outWasRemoved");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateWritableMap(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._remove(self->_vtable.Self, key, outWasRemoved);
}

Error IMap_Clear(IMap* self)
{
    Error Result = ValidateWritableMap(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._clear(self->_vtable.Self);
}

Error IMap_ContainsKey(IMap* self, const void* key, bool* outContainsKey)
{
    Error Result = ValidateMapAndKey(self, key);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateMapAndOutput(self, outContainsKey, u8"outContainsKey");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._containsKey(self->_vtable.Self, key, outContainsKey);
}

Error IMap_ContainsValue(IMap* self, const void* value, bool* outContainsValue)
{
    Error Result = ValidateMapAndValue(self, value);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateMapAndOutput(self, outContainsValue, u8"outContainsValue");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._containsValue(self->_vtable.Self, value, outContainsValue);
}

Error IMap_Deconstruct(IMap* self)
{
    Error Result = ValidateMap(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return self->_vtable._deconstruct(self->_vtable.Self);
}
