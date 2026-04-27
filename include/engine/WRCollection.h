#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WRError.h"
#include "WRMemory.h"


// Types.
typedef enum EnumeratorFlagsEnum
{
    EnumeratorFlags_None = 0,
    EnumeratorFlags_CanReturnByReference = (1 << 0)
} EnumeratorFlags;

typedef struct CollectionEnumeratorVTableStruct
{
    void* Self;
    Error (*_hasNext)(void* self, bool* outHasNext);
    Error (*_nextByValue)(void* self, void* outEntryValue);
    Error (*_nextByReference)(void* self, void** outPointer);
    void (*_deconstruct)(void* self);
} CollectionEnumeratorVTable;

typedef struct CollectionEnumeratorStruct
{
    size_t _singleElementSize;
    EnumeratorFlags _flags;
    CollectionEnumeratorVTable _vtable;
} CollectionEnumerator;

typedef struct ICollectionVTable
{
    void* Self;
    CollectionEnumerator* (*_getEnumerator)(void* self);
} ICollectionVtable;

typedef struct ICollectionStruct
{
    ICollectionVtable _vtable;
} ICollection;


// Functions.
static inline CollectionEnumerator* ICollection_GetEnumerator(ICollection* self)
{
    return (*self->_vtable._getEnumerator)(self->_vtable.Self);
}

Error ICollection_WriteToBufferByValue(ICollection* self, GenericBuffer* buffer);

Error ICollection_WriteToBufferByReference(ICollection* self, GenericBuffer* buffer);


static inline Error CollectionEnumerator_HasNext(CollectionEnumerator* self, bool* outValue)
{
    return (*self->_vtable._hasNext)(self->_vtable.Self, outValue);
}

static inline size_t CollectionEnumerator_GetSingleElementSize(CollectionEnumerator* self)
{
    return self->_singleElementSize;
}


static inline Error CollectionEnumerator_NextByValue(CollectionEnumerator* self, void* outValue)
{
    return (*self->_vtable._nextByValue)(self->_vtable.Self, outValue);
}

Error CollectionEnumerator_NextByReference(CollectionEnumerator* self, void** outPointer);

static inline bool CollectionEnumerator_IsReferenceReturningSupported(CollectionEnumerator* self)
{
    return ((self ->_flags) & EnumeratorFlags_CanReturnByReference) != 0;
}

static inline void CollectionEnumerator_Deconstruct(CollectionEnumerator* self)
{
    (*self->_vtable._deconstruct)(self->_vtable.Self);
}