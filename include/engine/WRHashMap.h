#pragma once
#include "WRMap.h"


// Types.
typedef HashCode (*HashMapKeyHashFunction)(IMap* map, const void* key, void* userData);

typedef struct HashMapStruct
{
    IMap _map;
    HashMapKeyHashFunction _keyHashFunction;
    void* _keyHashFunctionUserData;
    MapKeyComparator _keyComparator;
    void* _keyComparatorUserData;
    MapValueComparator _valueComparator;
    void* _valueComparatorUserData;
    GenericBuffer _dataBuffer;
    bool _isActiveBufferOwned;
    size_t _entryCount;
    size_t _tombstoneCount;
    size_t _capacity;
} HashMap;

typedef struct HashMapConstructOptionsStruct
{
    size_t KeySize;
    size_t ValueSize;
    HashMapKeyHashFunction KeyHashFunction;
    void* KeyHashFunctionUserData;
    MapKeyComparator KeyComparator;
    void* KeyComparatorUserData;
    MapValueComparator ValueComparator;
    void* ValueComparatorUserData;
    size_t InitialCapacity;
} HashMapConstructOptions;


// Functions.
static inline HashMapConstructOptions HashMapConstructOptions_CreateDefault(size_t keySize, 
    size_t valueSize,
    HashMapKeyHashFunction keyHashFunction)
{
    return (HashMapConstructOptions) {
        .KeySize = keySize,
        .ValueSize = valueSize,
        .KeyHashFunction = keyHashFunction,
        .KeyHashFunctionUserData = NULL,
        .KeyComparator = NULL,
        .KeyComparatorUserData = NULL,
        .ValueComparator = NULL,
        .ValueComparatorUserData = NULL,
        .InitialCapacity = 0,
    };
}

static inline IMap* HashMap_AsMap(HashMap* self)
{
    return &self->_map;
}

Error HashMap_Construct1(HashMap* self, HashMapConstructOptions options);

Error HashMap_Deconstruct(HashMap* self);
