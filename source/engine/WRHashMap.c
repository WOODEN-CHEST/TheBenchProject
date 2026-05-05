#include "WRHashMap.h"


// Macros.
#define HASH_MAP_INDEX_INVALID (~((size_t)0))
#define HASH_MAP_MINIMUM_CAPACITY ((size_t)8)


// Types.
typedef enum HashMapBucketStateEnum
{
    HashMapBucketState_Empty = 0,
    HashMapBucketState_Occupied = 1,
    HashMapBucketState_Tombstone = 2,
} HashMapBucketState;

typedef struct HashMapBucketMetadataStruct
{
    HashCode Hash;
    uint8_t State;
} HashMapBucketMetadata;

typedef enum HashMapCollectionKindEnum
{
    HashMapCollectionKind_Entry,
    HashMapCollectionKind_Key,
    HashMapCollectionKind_Value,
} HashMapCollectionKind;

typedef struct HashMapEnumeratorStruct
{
    CollectionEnumerator Base;
    HashMap* _hashMap;
    size_t _currentIndex;
    HashMapCollectionKind _kind;
    MapEntryView _currentEntryView;
} HashMapEnumerator;

typedef struct HashMapFindSlotResultStruct
{
    bool WasFound;
    bool CanInsert;
    size_t FoundIndex;
    size_t InsertionIndex;
} HashMapFindSlotResult;


// Static functions.
static size_t HashMap_MapGetEntryCount(void* self);

static Error HashMap_MapGetElement(void* self, const void* key, void* outValue);

static Error HashMap_MapGetPointerToElement(void* self, const void* key, void** outValuePointer);

static Error HashMap_MapAdd(void* self, const void* key, const void* value, bool* outWasAdded);

static Error HashMap_MapRemove(void* self, const void* key, bool* outWasRemoved);

static Error HashMap_MapClear(void* self);

static Error HashMap_MapContainsKey(void* self, const void* key, bool* outContainsKey);

static Error HashMap_MapContainsValue(void* self, const void* value, bool* outContainsValue);

static Error HashMap_MapDeconstruct(void* self);

static CollectionEnumerator* HashMap_EntryCollectionGetEnumerator(void* self);

static CollectionEnumerator* HashMap_KeyCollectionGetEnumerator(void* self);

static CollectionEnumerator* HashMap_ValueCollectionGetEnumerator(void* self);

static Error HashMapEnumerator_HasNext(void* self, bool* outHasNext);

static Error HashMapEnumerator_NextByValue(void* self, void* outEntryValue);

static Error HashMapEnumerator_NextByReference(void* self, void** outPointer);

static void HashMapEnumerator_Deconstruct(void* self);

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Hash map argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateInvalidOptionError(const unsigned char* optionName, const unsigned char* message)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Hash map option \"%s\" is invalid: %s.",
        optionName,
        message);
}

static Error CreateCapacityError(size_t requestedCapacity)
{
    return Error_Construct3(ErrorCode_BufferTooLarge,
        u8"Could not allocate hash map storage for %zu buckets.",
        requestedCapacity);
}

static Error CreateKeyNotFoundError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The specified map key was not found.");
}

static Error CreateEnumerationCompletedError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"The collection enumerator has no more elements.");
}

static bool TryMultiplySize(size_t a, size_t b, size_t* outProduct)
{
    if (outProduct == NULL)
    {
        return false;
    }
    if ((a > 0) && (b > (SIZE_MAX / a)))
    {
        *outProduct = 0;
        return false;
    }

    *outProduct = a * b;
    return true;
}

static bool TryAddSize(size_t a, size_t b, size_t* outSum)
{
    if (outSum == NULL)
    {
        return false;
    }
    if (b > (SIZE_MAX - a))
    {
        *outSum = 0;
        return false;
    }

    *outSum = a + b;
    return true;
}

static void InitializeEmptyBuffer(GenericBuffer* buffer)
{
    if (buffer == NULL)
    {
        return;
    }

    buffer->_data = NULL;
    buffer->_capacity = 0;
    buffer->_count = 0;
    buffer->_elementSize = sizeof(unsigned char);
    buffer->_userData = NULL;
    buffer->_requestMoreSpaceCallback = NULL;
    buffer->_flags = GenericBufferFlags_None;
}

static bool HashMap_HasStorage(HashMap* self)
{
    return (self != NULL) && (self->_capacity > 0) && (self->_dataBuffer._data != NULL);
}

static size_t HashMap_GetMetadataByteCountForCapacity(size_t capacity)
{
    return capacity * sizeof(HashMapBucketMetadata);
}

static size_t HashMap_GetKeyByteCountForCapacity(HashMap* self, size_t capacity)
{
    return capacity * IMap_GetKeySize(HashMap_AsMap(self));
}

static bool HashMap_TryGetStorageByteCount(HashMap* self, size_t capacity, size_t* outByteCount)
{
    size_t MetadataBytes = 0;
    size_t KeyBytes = 0;
    size_t ValueBytes = 0;
    size_t Intermediate = 0;

    if ((self == NULL) || (outByteCount == NULL))
    {
        return false;
    }
    if (!TryMultiplySize(capacity, sizeof(HashMapBucketMetadata), &MetadataBytes))
    {
        return false;
    }
    if (!TryMultiplySize(capacity, IMap_GetKeySize(HashMap_AsMap(self)), &KeyBytes))
    {
        return false;
    }
    if (!TryMultiplySize(capacity, IMap_GetValueSize(HashMap_AsMap(self)), &ValueBytes))
    {
        return false;
    }
    if (!TryAddSize(MetadataBytes, KeyBytes, &Intermediate))
    {
        return false;
    }
    if (!TryAddSize(Intermediate, ValueBytes, outByteCount))
    {
        return false;
    }

    return true;
}

static HashMapBucketMetadata* HashMap_GetBucketsFromBlock(unsigned char* block)
{
    return (HashMapBucketMetadata*)block;
}

static unsigned char* HashMap_GetKeysFromBlock(HashMap* self, unsigned char* block, size_t capacity)
{
    UNUSED(self);
    return block + HashMap_GetMetadataByteCountForCapacity(capacity);
}

static unsigned char* HashMap_GetValuesFromBlock(HashMap* self, unsigned char* block, size_t capacity)
{
    return HashMap_GetKeysFromBlock(self, block, capacity) + HashMap_GetKeyByteCountForCapacity(self, capacity);
}

static HashMapBucketMetadata* HashMap_GetBuckets(HashMap* self)
{
    return HashMap_GetBucketsFromBlock(self->_dataBuffer._data);
}

static unsigned char* HashMap_GetKeyBlock(HashMap* self)
{
    return HashMap_GetKeysFromBlock(self, self->_dataBuffer._data, self->_capacity);
}

static unsigned char* HashMap_GetValueBlock(HashMap* self)
{
    return HashMap_GetValuesFromBlock(self, self->_dataBuffer._data, self->_capacity);
}

static unsigned char* HashMap_GetKeyPointerFromBlock(HashMap* self, unsigned char* keyBlock, size_t index)
{
    return keyBlock + (index * IMap_GetKeySize(HashMap_AsMap(self)));
}

static unsigned char* HashMap_GetValuePointerFromBlock(HashMap* self, unsigned char* valueBlock, size_t index)
{
    return valueBlock + (index * IMap_GetValueSize(HashMap_AsMap(self)));
}

static unsigned char* HashMap_GetKeyPointerAt(HashMap* self, size_t index)
{
    return HashMap_GetKeyPointerFromBlock(self, HashMap_GetKeyBlock(self), index);
}

static unsigned char* HashMap_GetValuePointerAt(HashMap* self, size_t index)
{
    return HashMap_GetValuePointerFromBlock(self, HashMap_GetValueBlock(self), index);
}

static bool HashMap_KeysAreEqual(HashMap* self, const void* storedKey, const void* requestedKey)
{
    return self->_keyComparator(HashMap_AsMap(self),
        storedKey,
        requestedKey,
        self->_keyComparatorUserData);
}

static bool HashMap_ValuesAreEqual(HashMap* self, const void* storedValue, const void* requestedValue)
{
    return self->_valueComparator(HashMap_AsMap(self),
        storedValue,
        requestedValue,
        self->_valueComparatorUserData);
}

static bool HashMap_IsBucketOccupied(HashMapBucketMetadata bucket)
{
    return bucket.State == HashMapBucketState_Occupied;
}

static HashMapFindSlotResult HashMap_FindSlotInStorage(HashMap* self,
    size_t capacity,
    HashMapBucketMetadata* buckets,
    unsigned char* keyBlock,
    HashCode hash,
    const void* key)
{
    HashMapFindSlotResult Result =
    {
        .WasFound = false,
        .CanInsert = false,
        .FoundIndex = HASH_MAP_INDEX_INVALID,
        .InsertionIndex = HASH_MAP_INDEX_INVALID,
    };
    size_t FirstTombstoneIndex = HASH_MAP_INDEX_INVALID;
    size_t Index = 0;

    if ((self == NULL) || (capacity == 0) || (buckets == NULL) || (keyBlock == NULL))
    {
        return Result;
    }

    Index = (size_t)(hash % (HashCode)capacity);
    for (size_t ProbeIndex = 0; ProbeIndex < capacity; ProbeIndex++)
    {
        HashMapBucketMetadata Bucket = buckets[Index];

        if (Bucket.State == HashMapBucketState_Empty)
        {
            Result.CanInsert = true;
            Result.InsertionIndex = (FirstTombstoneIndex != HASH_MAP_INDEX_INVALID) ? FirstTombstoneIndex : Index;
            return Result;
        }
        if (Bucket.State == HashMapBucketState_Tombstone)
        {
            if (FirstTombstoneIndex == HASH_MAP_INDEX_INVALID)
            {
                FirstTombstoneIndex = Index;
            }
        }
        else if ((Bucket.Hash == hash)
            && HashMap_KeysAreEqual(self, HashMap_GetKeyPointerFromBlock(self, keyBlock, Index), key))
        {
            Result.WasFound = true;
            Result.CanInsert = true;
            Result.FoundIndex = Index;
            Result.InsertionIndex = Index;
            return Result;
        }

        Index++;
        if (Index == capacity)
        {
            Index = 0;
        }
    }

    if (FirstTombstoneIndex != HASH_MAP_INDEX_INVALID)
    {
        Result.CanInsert = true;
        Result.InsertionIndex = FirstTombstoneIndex;
    }

    return Result;
}

static HashMapFindSlotResult HashMap_FindSlot(HashMap* self, HashCode hash, const void* key)
{
    return HashMap_FindSlotInStorage(self,
        self->_capacity,
        HashMap_GetBuckets(self),
        HashMap_GetKeyBlock(self),
        hash,
        key);
}

static bool HashMap_TryFindNextOccupiedIndex(HashMap* self, size_t startIndex, size_t* outIndex)
{
    HashMapBucketMetadata* Buckets = NULL;

    if (outIndex == NULL)
    {
        return false;
    }

    *outIndex = HASH_MAP_INDEX_INVALID;
    if (!HashMap_HasStorage(self))
    {
        return false;
    }

    Buckets = HashMap_GetBuckets(self);
    for (size_t Index = startIndex; Index < self->_capacity; Index++)
    {
        if (HashMap_IsBucketOccupied(Buckets[Index]))
        {
            *outIndex = Index;
            return true;
        }
    }

    return false;
}

static bool HashMap_ShouldGrowForInsertion(HashMap* self)
{
    size_t OccupiedAndTombstones = 0;
    size_t GrowthThreshold = 0;

    if (self == NULL)
    {
        return false;
    }
    if (self->_capacity == 0)
    {
        return true;
    }

    OccupiedAndTombstones = self->_entryCount + self->_tombstoneCount;
    GrowthThreshold = self->_capacity - (self->_capacity / 4);
    return OccupiedAndTombstones >= GrowthThreshold;
}

static bool HashMap_ShouldRebuildForTombstones(HashMap* self)
{
    if ((self == NULL) || (self->_capacity == 0))
    {
        return false;
    }

    return self->_tombstoneCount > (self->_capacity / 4);
}

static Error HashMap_NormalizeCapacity(size_t requestedCapacity, size_t* outCapacity)
{
    size_t Capacity = HASH_MAP_MINIMUM_CAPACITY;

    if (outCapacity == NULL)
    {
        return CreateNullArgumentError(u8"outCapacity");
    }

    if (requestedCapacity <= HASH_MAP_MINIMUM_CAPACITY)
    {
        *outCapacity = HASH_MAP_MINIMUM_CAPACITY;
        return Error_CreateSuccess();
    }

    while (Capacity < requestedCapacity)
    {
        if (Capacity > (SIZE_MAX / 2))
        {
            return CreateCapacityError(requestedCapacity);
        }

        Capacity *= 2;
    }

    *outCapacity = Capacity;
    return Error_CreateSuccess();
}

static Error HashMap_CalculateGrowthCapacity(HashMap* self, size_t minimumLiveEntries, size_t* outCapacity)
{
    size_t Capacity = 0;
    Error Result = Error_CreateSuccess();

    if ((self == NULL) || (outCapacity == NULL))
    {
        return CreateNullArgumentError(u8"outCapacity");
    }

    Result = HashMap_NormalizeCapacity((self->_capacity == 0) ? HASH_MAP_MINIMUM_CAPACITY : self->_capacity, &Capacity);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    while (minimumLiveEntries >= (Capacity - (Capacity / 4)))
    {
        if (Capacity > (SIZE_MAX / 2))
        {
            return CreateCapacityError(minimumLiveEntries);
        }

        Capacity *= 2;
    }

    *outCapacity = Capacity;
    return Error_CreateSuccess();
}

static void HashMap_AdoptStorage(HashMap* self, unsigned char* storage, size_t capacity, size_t byteCount)
{
    self->_dataBuffer._data = storage;
    self->_dataBuffer._capacity = byteCount;
    self->_dataBuffer._count = byteCount;
    self->_dataBuffer._elementSize = sizeof(unsigned char);
    self->_dataBuffer._userData = NULL;
    self->_dataBuffer._requestMoreSpaceCallback = NULL;
    self->_dataBuffer._flags = GenericBufferFlags_None;
    self->_capacity = capacity;
    self->_isActiveBufferOwned = (storage != NULL);
}

static Error HashMap_RebuildStorage(HashMap* self, size_t requestedCapacity)
{
    unsigned char* NewStorage = NULL;
    size_t NewCapacity = 0;
    size_t NewByteCount = 0;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = HashMap_NormalizeCapacity(requestedCapacity, &NewCapacity);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HashMap_TryGetStorageByteCount(self, NewCapacity, &NewByteCount))
    {
        return CreateCapacityError(NewCapacity);
    }

    NewStorage = Memory_Allocate(NewByteCount);
    Memory_Zero(NewStorage, NewByteCount);

    if (HashMap_HasStorage(self))
    {
        unsigned char* OldStorage = self->_dataBuffer._data;
        HashMapBucketMetadata* OldBuckets = HashMap_GetBuckets(self);
        unsigned char* OldKeys = HashMap_GetKeyBlock(self);
        unsigned char* OldValues = HashMap_GetValueBlock(self);
        size_t OldCapacity = self->_capacity;
        HashMapBucketMetadata* NewBuckets = HashMap_GetBucketsFromBlock(NewStorage);
        unsigned char* NewKeys = HashMap_GetKeysFromBlock(self, NewStorage, NewCapacity);
        unsigned char* NewValues = HashMap_GetValuesFromBlock(self, NewStorage, NewCapacity);

        for (size_t Index = 0; Index < OldCapacity; Index++)
        {
            HashMapBucketMetadata OldBucket = OldBuckets[Index];

            if (!HashMap_IsBucketOccupied(OldBucket))
            {
                continue;
            }

            HashMapFindSlotResult SlotResult = HashMap_FindSlotInStorage(self,
                NewCapacity,
                NewBuckets,
                NewKeys,
                OldBucket.Hash,
                HashMap_GetKeyPointerFromBlock(self, OldKeys, Index));

            if (!SlotResult.CanInsert)
            {
                Memory_Free(NewStorage);
                return CreateCapacityError(NewCapacity);
            }

            NewBuckets[SlotResult.InsertionIndex].Hash = OldBucket.Hash;
            NewBuckets[SlotResult.InsertionIndex].State = HashMapBucketState_Occupied;
            Memory_Copy(HashMap_GetKeyPointerFromBlock(self, OldKeys, Index),
                HashMap_GetKeyPointerFromBlock(self, NewKeys, SlotResult.InsertionIndex),
                IMap_GetKeySize(HashMap_AsMap(self)));
            Memory_Copy(HashMap_GetValuePointerFromBlock(self, OldValues, Index),
                HashMap_GetValuePointerFromBlock(self, NewValues, SlotResult.InsertionIndex),
                IMap_GetValueSize(HashMap_AsMap(self)));
        }

        Memory_Free(OldStorage);
    }

    HashMap_AdoptStorage(self, NewStorage, NewCapacity, NewByteCount);
    self->_tombstoneCount = 0;
    return Error_CreateSuccess();
}

static Error HashMap_EnsureInsertCapacity(HashMap* self)
{
    size_t RequiredLiveCount = 0;
    size_t GrowthCapacity = 0;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    if (HashMap_ShouldGrowForInsertion(self))
    {
        RequiredLiveCount = self->_entryCount + 1;
        Result = HashMap_CalculateGrowthCapacity(self, RequiredLiveCount, &GrowthCapacity);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        return HashMap_RebuildStorage(self, GrowthCapacity);
    }
    if (HashMap_ShouldRebuildForTombstones(self))
    {
        return HashMap_RebuildStorage(self, self->_capacity);
    }

    return Error_CreateSuccess();
}

static void InitializeInterfaces(HashMap* self)
{
    static const MapVTable MapVTableTemplate =
    {
        .Self = NULL,
        ._getElement = HashMap_MapGetElement,
        ._getPointerToElement = HashMap_MapGetPointerToElement,
        ._add = HashMap_MapAdd,
        ._remove = HashMap_MapRemove,
        ._clear = HashMap_MapClear,
        ._containsKey = HashMap_MapContainsKey,
        ._containsValue = HashMap_MapContainsValue,
        ._getEntryCount = HashMap_MapGetEntryCount,
        ._deconstruct = HashMap_MapDeconstruct,
    };
    static const ICollectionVtable EntryCollectionTemplate =
    {
        .Self = NULL,
        ._getEnumerator = HashMap_EntryCollectionGetEnumerator,
    };
    static const ICollectionVtable KeyCollectionTemplate =
    {
        .Self = NULL,
        ._getEnumerator = HashMap_KeyCollectionGetEnumerator,
    };
    static const ICollectionVtable ValueCollectionTemplate =
    {
        .Self = NULL,
        ._getEnumerator = HashMap_ValueCollectionGetEnumerator,
    };

    if (self == NULL)
    {
        return;
    }

    self->_map._keySize = self->_map._keySize;
    self->_map._valueSize = self->_map._valueSize;
    self->_map._flags = MapFlags_None;
    self->_map._vtable = MapVTableTemplate;
    self->_map._vtable.Self = self;
    self->_map._entryCollection._vtable = EntryCollectionTemplate;
    self->_map._entryCollection._vtable.Self = self;
    self->_map._keyCollection._vtable = KeyCollectionTemplate;
    self->_map._keyCollection._vtable.Self = self;
    self->_map._valueCollection._vtable = ValueCollectionTemplate;
    self->_map._valueCollection._vtable.Self = self;
}

static void InitializeEmptyHashMap(HashMap* self)
{
    if (self == NULL)
    {
        return;
    }

    self->_map._keySize = 0;
    self->_map._valueSize = 0;
    self->_keyHashFunction = NULL;
    self->_keyHashFunctionUserData = NULL;
    self->_keyComparator = MapKeyComparator_Default;
    self->_keyComparatorUserData = NULL;
    self->_valueComparator = MapValueComparator_Default;
    self->_valueComparatorUserData = NULL;
    InitializeEmptyBuffer(&self->_dataBuffer);
    self->_isActiveBufferOwned = false;
    self->_entryCount = 0;
    self->_tombstoneCount = 0;
    self->_capacity = 0;
    InitializeInterfaces(self);
}

static Error ValidateHashMap(HashMap* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Error_CreateSuccess();
}

static Error ValidateHashMapOutput(HashMap* self, void* output, const unsigned char* outputName)
{
    Error Result = ValidateHashMap(self);

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

static Error ValidateEnumeratorAndOutput(HashMapEnumerator* self, void* outValue)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outValue == NULL)
    {
        return CreateNullArgumentError(u8"outValue");
    }

    return Error_CreateSuccess();
}

static Error ValidateEnumeratorPointerOutput(HashMapEnumerator* self, void** outPointer)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outPointer == NULL)
    {
        return CreateNullArgumentError(u8"outPointer");
    }

    return Error_CreateSuccess();
}

static size_t HashMap_MapGetEntryCount(void* self)
{
    HashMap* HashMapSelf = self;

    if (HashMapSelf == NULL)
    {
        return 0;
    }

    return HashMapSelf->_entryCount;
}

static Error HashMap_MapGetElement(void* self, const void* key, void* outValue)
{
    HashMap* HashMapSelf = self;
    HashCode Hash = 0;
    HashMapFindSlotResult SlotResult;
    Error Result = ValidateHashMapOutput(HashMapSelf, outValue, u8"outValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (key == NULL)
    {
        return CreateNullArgumentError(u8"key");
    }
    if (!HashMap_HasStorage(HashMapSelf))
    {
        return CreateKeyNotFoundError();
    }

    Hash = HashMapSelf->_keyHashFunction(HashMap_AsMap(HashMapSelf), key, HashMapSelf->_keyHashFunctionUserData);
    SlotResult = HashMap_FindSlot(HashMapSelf, Hash, key);
    if (!SlotResult.WasFound)
    {
        return CreateKeyNotFoundError();
    }

    Memory_Copy(HashMap_GetValuePointerAt(HashMapSelf, SlotResult.FoundIndex),
        outValue,
        IMap_GetValueSize(HashMap_AsMap(HashMapSelf)));
    return Error_CreateSuccess();
}

static Error HashMap_MapGetPointerToElement(void* self, const void* key, void** outValuePointer)
{
    HashMap* HashMapSelf = self;
    HashCode Hash = 0;
    HashMapFindSlotResult SlotResult;
    Error Result = ValidateHashMapOutput(HashMapSelf, outValuePointer, u8"outValuePointer");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (key == NULL)
    {
        return CreateNullArgumentError(u8"key");
    }

    *outValuePointer = NULL;
    if (!HashMap_HasStorage(HashMapSelf))
    {
        return CreateKeyNotFoundError();
    }

    Hash = HashMapSelf->_keyHashFunction(HashMap_AsMap(HashMapSelf), key, HashMapSelf->_keyHashFunctionUserData);
    SlotResult = HashMap_FindSlot(HashMapSelf, Hash, key);
    if (!SlotResult.WasFound)
    {
        return CreateKeyNotFoundError();
    }

    *outValuePointer = HashMap_GetValuePointerAt(HashMapSelf, SlotResult.FoundIndex);
    return Error_CreateSuccess();
}

static Error HashMap_MapAdd(void* self, const void* key, const void* value, bool* outWasAdded)
{
    HashMap* HashMapSelf = self;
    HashCode Hash = 0;
    HashMapFindSlotResult SlotResult;
    Error Result = ValidateHashMapOutput(HashMapSelf, outWasAdded, u8"outWasAdded");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (key == NULL)
    {
        return CreateNullArgumentError(u8"key");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = HashMap_EnsureInsertCapacity(HashMapSelf);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Hash = HashMapSelf->_keyHashFunction(HashMap_AsMap(HashMapSelf), key, HashMapSelf->_keyHashFunctionUserData);
    SlotResult = HashMap_FindSlot(HashMapSelf, Hash, key);
    if (!SlotResult.CanInsert)
    {
        return CreateCapacityError(HashMapSelf->_capacity);
    }

    if (SlotResult.WasFound)
    {
        Memory_Copy(value,
            HashMap_GetValuePointerAt(HashMapSelf, SlotResult.FoundIndex),
            IMap_GetValueSize(HashMap_AsMap(HashMapSelf)));
        *outWasAdded = false;
        return Error_CreateSuccess();
    }

    if (HashMap_GetBuckets(HashMapSelf)[SlotResult.InsertionIndex].State == HashMapBucketState_Tombstone)
    {
        HashMapSelf->_tombstoneCount--;
    }

    HashMap_GetBuckets(HashMapSelf)[SlotResult.InsertionIndex].Hash = Hash;
    HashMap_GetBuckets(HashMapSelf)[SlotResult.InsertionIndex].State = HashMapBucketState_Occupied;
    Memory_Copy(key,
        HashMap_GetKeyPointerAt(HashMapSelf, SlotResult.InsertionIndex),
        IMap_GetKeySize(HashMap_AsMap(HashMapSelf)));
    Memory_Copy(value,
        HashMap_GetValuePointerAt(HashMapSelf, SlotResult.InsertionIndex),
        IMap_GetValueSize(HashMap_AsMap(HashMapSelf)));
    HashMapSelf->_entryCount++;
    *outWasAdded = true;
    return Error_CreateSuccess();
}

static Error HashMap_MapRemove(void* self, const void* key, bool* outWasRemoved)
{
    HashMap* HashMapSelf = self;
    HashCode Hash = 0;
    HashMapFindSlotResult SlotResult;
    Error Result = ValidateHashMapOutput(HashMapSelf, outWasRemoved, u8"outWasRemoved");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (key == NULL)
    {
        return CreateNullArgumentError(u8"key");
    }

    *outWasRemoved = false;
    if (!HashMap_HasStorage(HashMapSelf))
    {
        return Error_CreateSuccess();
    }

    Hash = HashMapSelf->_keyHashFunction(HashMap_AsMap(HashMapSelf), key, HashMapSelf->_keyHashFunctionUserData);
    SlotResult = HashMap_FindSlot(HashMapSelf, Hash, key);
    if (!SlotResult.WasFound)
    {
        return Error_CreateSuccess();
    }

    HashMap_GetBuckets(HashMapSelf)[SlotResult.FoundIndex].State = HashMapBucketState_Tombstone;
    HashMapSelf->_entryCount--;
    HashMapSelf->_tombstoneCount++;
    *outWasRemoved = true;

    if (HashMap_ShouldRebuildForTombstones(HashMapSelf) && (HashMapSelf->_entryCount > 0))
    {
        Result = HashMap_RebuildStorage(HashMapSelf, HashMapSelf->_capacity);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

static Error HashMap_MapClear(void* self)
{
    HashMap* HashMapSelf = self;
    Error Result = ValidateHashMap(HashMapSelf);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HashMap_HasStorage(HashMapSelf))
    {
        return Error_CreateSuccess();
    }

    Memory_Zero(HashMap_GetBuckets(HashMapSelf),
        HashMap_GetMetadataByteCountForCapacity(HashMapSelf->_capacity));
    HashMapSelf->_entryCount = 0;
    HashMapSelf->_tombstoneCount = 0;
    return Error_CreateSuccess();
}

static Error HashMap_MapContainsKey(void* self, const void* key, bool* outContainsKey)
{
    HashMap* HashMapSelf = self;
    HashCode Hash = 0;
    HashMapFindSlotResult SlotResult;
    Error Result = ValidateHashMapOutput(HashMapSelf, outContainsKey, u8"outContainsKey");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (key == NULL)
    {
        return CreateNullArgumentError(u8"key");
    }

    *outContainsKey = false;
    if (!HashMap_HasStorage(HashMapSelf))
    {
        return Error_CreateSuccess();
    }

    Hash = HashMapSelf->_keyHashFunction(HashMap_AsMap(HashMapSelf), key, HashMapSelf->_keyHashFunctionUserData);
    SlotResult = HashMap_FindSlot(HashMapSelf, Hash, key);
    *outContainsKey = SlotResult.WasFound;
    return Error_CreateSuccess();
}

static Error HashMap_MapContainsValue(void* self, const void* value, bool* outContainsValue)
{
    HashMap* HashMapSelf = self;
    Error Result = ValidateHashMapOutput(HashMapSelf, outContainsValue, u8"outContainsValue");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    *outContainsValue = false;
    if (!HashMap_HasStorage(HashMapSelf))
    {
        return Error_CreateSuccess();
    }

    for (size_t Index = 0; Index < HashMapSelf->_capacity; Index++)
    {
        if (!HashMap_IsBucketOccupied(HashMap_GetBuckets(HashMapSelf)[Index]))
        {
            continue;
        }
        if (HashMap_ValuesAreEqual(HashMapSelf, HashMap_GetValuePointerAt(HashMapSelf, Index), value))
        {
            *outContainsValue = true;
            return Error_CreateSuccess();
        }
    }

    return Error_CreateSuccess();
}

static Error HashMap_MapDeconstruct(void* self)
{
    return HashMap_Deconstruct(self);
}

static CollectionEnumerator* HashMap_CreateEnumerator(HashMap* self, HashMapCollectionKind kind)
{
    static const CollectionEnumeratorVTable EnumeratorTemplate =
    {
        .Self = NULL,
        ._hasNext = HashMapEnumerator_HasNext,
        ._nextByValue = HashMapEnumerator_NextByValue,
        ._nextByReference = HashMapEnumerator_NextByReference,
        ._deconstruct = HashMapEnumerator_Deconstruct,
    };
    HashMapEnumerator* Enumerator = NULL;

    if (self == NULL)
    {
        return NULL;
    }

    Enumerator = Memory_Allocate(sizeof(*Enumerator));
    Enumerator->Base._singleElementSize = sizeof(MapEntryView);
    if (kind == HashMapCollectionKind_Key)
    {
        Enumerator->Base._singleElementSize = IMap_GetKeySize(HashMap_AsMap(self));
    }
    else if (kind == HashMapCollectionKind_Value)
    {
        Enumerator->Base._singleElementSize = IMap_GetValueSize(HashMap_AsMap(self));
    }

    Enumerator->Base._flags = EnumeratorFlags_CanReturnByReference;
    Enumerator->Base._vtable = EnumeratorTemplate;
    Enumerator->Base._vtable.Self = Enumerator;
    Enumerator->_hashMap = self;
    Enumerator->_currentIndex = 0;
    Enumerator->_kind = kind;
    Enumerator->_currentEntryView = (MapEntryView){ ._key = NULL, ._value = NULL };
    return &Enumerator->Base;
}

static CollectionEnumerator* HashMap_EntryCollectionGetEnumerator(void* self)
{
    return HashMap_CreateEnumerator(self, HashMapCollectionKind_Entry);
}

static CollectionEnumerator* HashMap_KeyCollectionGetEnumerator(void* self)
{
    return HashMap_CreateEnumerator(self, HashMapCollectionKind_Key);
}

static CollectionEnumerator* HashMap_ValueCollectionGetEnumerator(void* self)
{
    return HashMap_CreateEnumerator(self, HashMapCollectionKind_Value);
}

static Error HashMapEnumerator_HasNext(void* self, bool* outHasNext)
{
    HashMapEnumerator* EnumeratorSelf = self;
    size_t NextIndex = HASH_MAP_INDEX_INVALID;

    if (EnumeratorSelf == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outHasNext == NULL)
    {
        return CreateNullArgumentError(u8"outHasNext");
    }

    *outHasNext = HashMap_TryFindNextOccupiedIndex(EnumeratorSelf->_hashMap, EnumeratorSelf->_currentIndex, &NextIndex);
    return Error_CreateSuccess();
}

static Error HashMapEnumerator_NextByValue(void* self, void* outEntryValue)
{
    HashMapEnumerator* EnumeratorSelf = self;
    size_t NextIndex = HASH_MAP_INDEX_INVALID;
    Error Result = ValidateEnumeratorAndOutput(EnumeratorSelf, outEntryValue);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HashMap_TryFindNextOccupiedIndex(EnumeratorSelf->_hashMap, EnumeratorSelf->_currentIndex, &NextIndex))
    {
        return CreateEnumerationCompletedError();
    }

    if (EnumeratorSelf->_kind == HashMapCollectionKind_Entry)
    {
        MapEntryView EntryView =
        {
            ._key = HashMap_GetKeyPointerAt(EnumeratorSelf->_hashMap, NextIndex),
            ._value = HashMap_GetValuePointerAt(EnumeratorSelf->_hashMap, NextIndex),
        };
        Memory_Copy(&EntryView, outEntryValue, sizeof(EntryView));
    }
    else if (EnumeratorSelf->_kind == HashMapCollectionKind_Key)
    {
        Memory_Copy(HashMap_GetKeyPointerAt(EnumeratorSelf->_hashMap, NextIndex),
            outEntryValue,
            IMap_GetKeySize(HashMap_AsMap(EnumeratorSelf->_hashMap)));
    }
    else
    {
        Memory_Copy(HashMap_GetValuePointerAt(EnumeratorSelf->_hashMap, NextIndex),
            outEntryValue,
            IMap_GetValueSize(HashMap_AsMap(EnumeratorSelf->_hashMap)));
    }

    EnumeratorSelf->_currentIndex = NextIndex + 1;
    return Error_CreateSuccess();
}

static Error HashMapEnumerator_NextByReference(void* self, void** outPointer)
{
    HashMapEnumerator* EnumeratorSelf = self;
    size_t NextIndex = HASH_MAP_INDEX_INVALID;
    Error Result = ValidateEnumeratorPointerOutput(EnumeratorSelf, outPointer);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!HashMap_TryFindNextOccupiedIndex(EnumeratorSelf->_hashMap, EnumeratorSelf->_currentIndex, &NextIndex))
    {
        *outPointer = NULL;
        return CreateEnumerationCompletedError();
    }

    if (EnumeratorSelf->_kind == HashMapCollectionKind_Entry)
    {
        EnumeratorSelf->_currentEntryView = (MapEntryView)
        {
            ._key = HashMap_GetKeyPointerAt(EnumeratorSelf->_hashMap, NextIndex),
            ._value = HashMap_GetValuePointerAt(EnumeratorSelf->_hashMap, NextIndex),
        };
        *outPointer = &EnumeratorSelf->_currentEntryView;
    }
    else if (EnumeratorSelf->_kind == HashMapCollectionKind_Key)
    {
        *outPointer = HashMap_GetKeyPointerAt(EnumeratorSelf->_hashMap, NextIndex);
    }
    else
    {
        *outPointer = HashMap_GetValuePointerAt(EnumeratorSelf->_hashMap, NextIndex);
    }

    EnumeratorSelf->_currentIndex = NextIndex + 1;
    return Error_CreateSuccess();
}

static void HashMapEnumerator_Deconstruct(void* self)
{
    HashMapEnumerator* EnumeratorSelf = self;

    if (EnumeratorSelf == NULL)
    {
        return;
    }

    Memory_Free(EnumeratorSelf);
}


// Public functions.
Error HashMap_Construct1(HashMap* self, HashMapConstructOptions options)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (options.KeySize == 0)
    {
        return CreateInvalidOptionError(u8"KeySize", u8"must be greater than zero");
    }
    if (options.ValueSize == 0)
    {
        return CreateInvalidOptionError(u8"ValueSize", u8"must be greater than zero");
    }
    if (options.KeyHashFunction == NULL)
    {
        return CreateInvalidOptionError(u8"KeyHashFunction", u8"must not be null");
    }

    InitializeEmptyHashMap(self);
    self->_map._keySize = options.KeySize;
    self->_map._valueSize = options.ValueSize;
    self->_keyHashFunction = options.KeyHashFunction;
    self->_keyHashFunctionUserData = options.KeyHashFunctionUserData;
    self->_keyComparator = (options.KeyComparator == NULL) ? MapKeyComparator_Default : options.KeyComparator;
    self->_keyComparatorUserData = options.KeyComparatorUserData;
    self->_valueComparator = (options.ValueComparator == NULL) ? MapValueComparator_Default : options.ValueComparator;
    self->_valueComparatorUserData = options.ValueComparatorUserData;
    InitializeInterfaces(self);

    if (options.InitialCapacity > 0)
    {
        Result = HashMap_RebuildStorage(self, options.InitialCapacity);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}

Error HashMap_Deconstruct(HashMap* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    if (self->_isActiveBufferOwned && (self->_dataBuffer._data != NULL))
    {
        Memory_Free(self->_dataBuffer._data);
    }

    InitializeEmptyHashMap(self);
    return Error_CreateSuccess();
}
