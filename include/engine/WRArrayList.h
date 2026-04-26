#pragma once
#include "WRList.h"
#include "WRCollection.h"
#include "WRMemory.h"
#include "WRError.h"
#include <stddef.h>

// Types.
typedef struct ArrayListStruct
{
    GenericBuffer _selfContainedBuffer;
    GenericBuffer* _activeBuffer;
    IList _list;
    bool _isActiveBufferOwned;
} ArrayList;


// Functions.
void ArrayList_Construct1(ArrayList* self, size_t elementSize);

void ArrayList_Construct2(ArrayList* self, size_t elementSize, size_t initialCapacity);

void ArrayList_Construct3(ArrayList* self, GenericBuffer* bufferToWrap);

Error ArrayList_EnsureTotalCapacity(ArrayList* self, size_t totalCapacity);

Error ArrayList_ReserveMoreCapacity(ArrayList* self, size_t requiredExtraSize);

static inline GenericBuffer* ArrayList_GetActiveBuffer(ArrayList* self)
{
    return self->_activeBuffer;
}

void ArrayList_Deconstruct(ArrayList* self);
