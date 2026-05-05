#pragma once
#include <stddef.h>
#include <stdint.h>
#include "WRHash.h"
#include "WRMemory.h"
#include "WRCollection.h"
#include "WRError.h"
#include "WRCompile.h"


// Types.
typedef struct IMapStruct IMap;

typedef enum MapFlagsEnum
{
    MapFlags_None = 0,
    MapFlags_IsReadOnly = (1 << 0),
} MapFlags;

typedef bool (*MapKeyComparator)(IMap* map, const void* key1, const void* key2, void* userData);

typedef bool (*MapValueComparator)(IMap* map, const void* value1, const void* value2, void* userData);

typedef struct MapEntryViewStruct
{
    void* _key;
    void* _value;
} MapEntryView;

typedef struct MapVTableStruct
{
    void* Self;
    Error (*_getElement)(void* self, const void* key, void* outValue);
    Error (*_getPointerToElement)(void* self, const void* key, void** outValuePointer);
    Error (*_add)(void* self, const void* key, const void* value, bool* outWasAdded);
    Error (*_remove)(void* self, const void* key, bool* outWasRemoved);
    Error (*_clear)(void* self);
    Error (*_containsKey)(void* self, const void* key, bool* outContainsKey);
    Error (*_containsValue)(void* self, const void* value, bool* outContainsValue);
    size_t (*_getEntryCount)(void* self);
    Error (*_deconstruct)(void* self);
} MapVTable;

typedef struct IMapStruct
{
    size_t _valueSize;
    size_t _keySize;
    MapVTable _vtable;
    ICollection _entryCollection;
    ICollection _keyCollection;
    ICollection _valueCollection;
    MapFlags _flags;
} IMap;


// Functions.
static inline size_t IMap_GetKeySize(IMap* map);

static inline size_t IMap_GetValueSize(IMap* map);

static inline bool MapKeyComparator_Default(IMap* map, const void* key1, const void* key2, void* userData)
{
    UNUSED(userData);
    return Memory_IsEqual(key1, key2, IMap_GetKeySize(map));
}

static inline bool MapValueComparator_Default(IMap* map, const void* value1, const void* value2, void* userData)
{
    UNUSED(userData);
    return Memory_IsEqual(value1, value2, IMap_GetValueSize(map));
}

static inline size_t IMap_GetKeySize(IMap* map)
{
    return map->_keySize;
}

static inline size_t IMap_GetValueSize(IMap* map)
{
    return map->_valueSize;
}

static inline ICollection* IMap_AsEntryCollection(IMap* map)
{
    return &map->_entryCollection;
}

static inline ICollection* IMap_AsKeyCollection(IMap* map)
{
    return &map->_keyCollection;
}

static inline ICollection* IMap_AsValueCollection(IMap* map)
{
    return &map->_valueCollection;
}

static inline size_t IMap_GetEntryCount(IMap* map)
{
    return map->_vtable._getEntryCount(map->_vtable.Self);
}

static inline MapFlags IMap_GetFlags(IMap* map)
{
    return map->_flags;
}

static inline bool IMap_IsReadOnly(IMap* map)
{
    return (IMap_GetFlags(map) & MapFlags_IsReadOnly) != 0;
}

Error IMap_GetElement(IMap* self, const void* key, void* outValue);

Error IMap_GetPointerToElement(IMap* self, const void* key, void** outValue);

Error IMap_Add(IMap* self, const void* key, const void* value, bool* outWasAdded);

Error IMap_Remove(IMap* self, const void* key, bool* outWasRemoved);

Error IMap_Clear(IMap* self);

Error IMap_ContainsKey(IMap* self, const void* key, bool* outContainsKey);

Error IMap_ContainsValue(IMap* self, const void* value, bool* outContainsValue);

Error IMap_Deconstruct(IMap* self);
