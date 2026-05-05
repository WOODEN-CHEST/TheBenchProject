#pragma once
#include "WRMemory.h"
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"


// Types.
typedef Error (*ObjectPoolObjectLifecycleCallback)(void* object, void* userData);

typedef struct ObjectPoolLifecycleStruct
{
    ObjectPoolObjectLifecycleCallback ConstructObject;
    ObjectPoolObjectLifecycleCallback ResetObject;
    ObjectPoolObjectLifecycleCallback DeconstructObject;
} ObjectPoolLifecycle;

typedef struct ObjectPoolStruct
{
    GenericBuffer _sections;
    size_t _elementSize;
    size_t _sectionCapacity; // Object count per section.
    size_t _nextSectionSearchIndex;
    ObjectPoolLifecycle _lifecycle;
    void* _userData;
} ObjectPool;


// Functions.
Error ObjectPool_Construct1(ObjectPool* self, size_t elementSize, size_t sectionCapacity);

Error ObjectPool_Construct2(ObjectPool* self,
    size_t elementSize,
    size_t sectionCapacity,
    ObjectPoolLifecycle lifecycle,
    void* userData);

Error ObjectPool_Deconstruct(ObjectPool* self);

static inline size_t ObjectPool_GetElementSize(ObjectPool* self)
{
    return self->_elementSize;
}

static inline size_t ObjectPool_GetSectionCapacity(ObjectPool* self)
{
    return self->_sectionCapacity;
}

Error ObjectPool_GetNewObject(ObjectPool* self, void** outObject);

/* Object pointer here must be the same as returned by GetNewObject. */
Error ObjectPool_DisposeObject(ObjectPool* self, void* object);

Error ObjectPool_Clear(ObjectPool* self);
