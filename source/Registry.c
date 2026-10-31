#include <inttypes.h>
#include "Registry.h"
#include "wr/WRCompile.h"
#include "wr/WRComparator.h"
#include "wr/WRHash.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** Elements per block. Blocks never move, so an active element keeps its address however many entries follow. */
#define REGISTRY_BLOCK_ELEMENT_COUNT ((size_t)64)
/** The name of the source NAMESPACED_KEY_ID_NONE in messages. */
#define REGISTRY_ENGINE_SOURCE_NAME "the engine"


// Types.
/* One index's record. */
typedef struct RegistrySlotStruct
{
    /* The entry's key id; kept while the entry is withdrawn, so registering the key again revives this slot. */
    uint32_t KeyId;
    /* The source that registered it last: a key id, NAMESPACED_KEY_ID_NONE for the engine. */
    uint32_t SourceKeyId;
    /* Whether the entry is registered now. */
    bool IsActive;
} RegistrySlot;


// Static functions: storage.
static HashCode HashKeyId(IMap* map, const void* key, const UserData* userData)
{
    UNUSED(map);
    UNUSED(userData);
    uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
    Memory_Copy(key, &KeyId, sizeof(KeyId));
    return Hash_UInt32(KeyId);
}

static RegistrySlot* GetSlots(const Registry* self)
{
    return (RegistrySlot*)self->_slots._data;
}

/* The element storage of @p index, an index of the registry. */
static unsigned char* GetElementAt(const Registry* self, uint32_t index)
{
    unsigned char* const* Blocks = (unsigned char* const*)self->_blocks._data;
    return Blocks[index / REGISTRY_BLOCK_ELEMENT_COUNT] + ((index % REGISTRY_BLOCK_ELEMENT_COUNT) * self->_elementSize);
}

static const char* GetRegistryName(const Registry* self)
{
    return (const char*)KeyRegistry_GetText(self->_keys, self->_keyId);
}

/* A source's name in messages: its key, or the engine. */
static const char* GetSourceName(const Registry* self, uint32_t sourceKeyId)
{
    const unsigned char* Text = KeyRegistry_GetText(self->_keys, sourceKeyId);
    return (Text != NULL) ? (const char*)Text : REGISTRY_ENGINE_SOURCE_NAME;
}

/* The index of @p keyId's slot, active or withdrawn; false when the key has none. ContainsKey first: a miss on
 * GetElement would build an error message. */
static bool FindSlot(Registry* self, uint32_t keyId, uint32_t* outIndex)
{
    IMap* Map = HashMap_AsMap(&self->_indexByKey);
    bool IsPresent = false;
    Error ContainsResult = IMap_ContainsKey(Map, &keyId, &IsPresent);
    if ((ContainsResult.Code != ErrorCode_Success) || !IsPresent)
    {
        Error_Deconstruct(&ContainsResult);
        return false;
    }
    Error GetResult = IMap_GetElement(Map, &keyId, outIndex);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return false;
    }
    return true;
}

/* Frees the last block and drops it from the block list. */
static void RemoveLastBlock(Registry* self)
{
    unsigned char* Block = NULL;
    GenericBuffer_GetLast(&self->_blocks, &Block);
    GenericBuffer_RemoveLast(&self->_blocks);
    Memory_Free(Block);
}

/* Adds a withdrawn slot for the new key @p keyId (and a block when the last one is full); nothing changes on
 * failure. */
static Error AddSlot(Registry* self, uint32_t keyId, uint32_t* outIndex)
{
    uint32_t Index = Registry_GetCount(self);
    bool IsBlockAdded = (Index % REGISTRY_BLOCK_ELEMENT_COUNT) == 0U;
    if (IsBlockAdded)
    {
        // The block size was checked against overflow at construction.
        unsigned char* Block = Memory_Allocate(self->_elementSize * REGISTRY_BLOCK_ELEMENT_COUNT);
        if (!GenericBuffer_AddLast(&self->_blocks, &Block))
        {
            Memory_Free(Block);
            return Error_Construct2(ErrorCode_BufferTooSmall, "Registry_Register: the entry could not be stored.");
        }
    }

    RegistrySlot Slot = { .KeyId = keyId, .SourceKeyId = NAMESPACED_KEY_ID_NONE, .IsActive = false };
    if (!GenericBuffer_AddLast(&self->_slots, &Slot))
    {
        if (IsBlockAdded)
        {
            RemoveLastBlock(self);
        }
        return Error_Construct2(ErrorCode_BufferTooSmall, "Registry_Register: the entry could not be stored.");
    }

    bool WasAdded = false;
    Error MapResult = IMap_Add(HashMap_AsMap(&self->_indexByKey), &keyId, &Index, &WasAdded);
    if (MapResult.Code != ErrorCode_Success)
    {
        GenericBuffer_RemoveLast(&self->_slots);
        if (IsBlockAdded)
        {
            RemoveLastBlock(self);
        }
        return MapResult;
    }
    *outIndex = Index;
    return Error_CreateSuccess();
}

/* Moves @p element into the storage of the withdrawn slot @p index and activates it. */
static void FillSlot(Registry* self, uint32_t index, const void* element, uint32_t sourceKeyId)
{
    Memory_Copy(element, GetElementAt(self, index), self->_elementSize);
    RegistrySlot* Slot = GetSlots(self) + index;
    Slot->SourceKeyId = sourceKeyId;
    Slot->IsActive = true;
    self->_activeCount++;
    self->_revision++;
}

/* Deconstructs the element of the active entry @p index and zeroes it. */
static void DeconstructElementAt(Registry* self, uint32_t index)
{
    unsigned char* Element = GetElementAt(self, index);
    if (self->_deconstructElement != NULL)
    {
        self->_deconstructElement(Element, &self->_deconstructElementUserData);
    }
    Memory_Zero(Element, self->_elementSize);
}

/* Withdraws the active entry @p index: its element is deconstructed, its slot kept. */
static void EmptySlot(Registry* self, uint32_t index)
{
    DeconstructElementAt(self, index);
    GetSlots(self)[index].IsActive = false;
    self->_activeCount--;
    self->_revision++;
}

/* Orders indices by their key's text (the registry is stored inline in the user data). */
static ComparisonResult CompareIndicesByKeyText(GenericBuffer* buffer, GenericBufferElementData a,
    GenericBufferElementData b, const UserData* userData)
{
    UNUSED(buffer);
    const Registry* Self = NULL;
    Memory_Copy(userData->Bytes, &Self, sizeof(Self));
    uint32_t First = 0U;
    uint32_t Second = 0U;
    Memory_Copy(a._element, &First, sizeof(First));
    Memory_Copy(b._element, &Second, sizeof(Second));
    const unsigned char* FirstText = Registry_GetKeyText(Self, First);
    const unsigned char* SecondText = Registry_GetKeyText(Self, Second);
    size_t Index = 0U;
    while ((FirstText[Index] == SecondText[Index]) && (FirstText[Index] != (unsigned char)0))
    {
        Index++;
    }
    return COMPARE_NUMBER(FirstText[Index], SecondText[Index]);
}


// Public functions: the registry.
Error Registry_Construct(Registry* self, const RegistryConstructOptions* options)
{
    if ((self == NULL) || (options == NULL) || (options->Keys == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Registry_Construct: self, options and options->Keys must not be NULL.");
    }
    if (KeyRegistry_GetText(options->Keys, options->KeyId) == NULL)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "Registry_Construct: the registry's key id %" PRIu32 " is not a key of its key registry.", options->KeyId);
    }
    size_t BlockBytes = 0U;
    if ((options->ElementSize == 0U)
        || !Memory_TryMultiplySize(options->ElementSize, REGISTRY_BLOCK_ELEMENT_COUNT, &BlockBytes))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "Registry_Construct: the element size must be at least 1 and a block of elements must fit in memory.");
    }
    if ((options->MaxEntryCount == 0U) || (options->MaxEntryCount > REGISTRY_MAX_ENTRY_COUNT))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "Registry_Construct: the maximum entry count must be 1 to %" PRIu32 ".", REGISTRY_MAX_ENTRY_COUNT);
    }

    HashMapConstructOptions MapOptions = HashMapConstructOptions_CreateDefault(sizeof(uint32_t), sizeof(uint32_t),
        HashKeyId);
    Error MapResult = HashMap_Construct1(&self->_indexByKey, MapOptions);
    if (MapResult.Code != ErrorCode_Success)
    {
        return MapResult;
    }
    GenericBuffer_AllocateVariable(&self->_slots, 0U, sizeof(RegistrySlot));
    GenericBuffer_AllocateVariable(&self->_blocks, 0U, sizeof(unsigned char*));
    self->_keys = options->Keys;
    self->_keyId = options->KeyId;
    self->_maxEntryCount = options->MaxEntryCount;
    self->_activeCount = 0U;
    self->_elementSize = options->ElementSize;
    self->_revision = 0U;
    self->_deconstructElement = options->DeconstructElement;
    self->_deconstructElementUserData = options->DeconstructElementUserData;
    return Error_CreateSuccess();
}

void Registry_Deconstruct(Registry* self)
{
    if (self == NULL)
    {
        return;
    }
    for (uint32_t Index = 0U; Index < Registry_GetCount(self); Index++)
    {
        if (GetSlots(self)[Index].IsActive)
        {
            DeconstructElementAt(self, Index);
        }
    }
    unsigned char* const* Blocks = (unsigned char* const*)self->_blocks._data;
    for (size_t Index = 0U; Index < self->_blocks._count; Index++)
    {
        Memory_Free(Blocks[Index]);
    }
    Memory_Free(self->_blocks._data);
    Memory_Free(self->_slots._data);
    Error MapResult = HashMap_Deconstruct(&self->_indexByKey);
    Error_Deconstruct(&MapResult);
    Memory_Zero(self, sizeof(*self));
}

Error Registry_Register(Registry* self, uint32_t keyId, const void* element, uint32_t sourceKeyId, uint32_t* outIndex)
{
    if ((self == NULL) || (element == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Registry_Register: self and element must not be NULL.");
    }
    if (KeyRegistry_GetText(self->_keys, keyId) == NULL)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "Registry %s: key id %" PRIu32 " is not a key of its key registry.", GetRegistryName(self), keyId);
    }
    if ((sourceKeyId != NAMESPACED_KEY_ID_NONE) && (KeyRegistry_GetText(self->_keys, sourceKeyId) == NULL))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "Registry %s: source key id %" PRIu32 " is not a key of its key registry.", GetRegistryName(self),
            sourceKeyId);
    }

    uint32_t Index = REGISTRY_INDEX_NONE;
    if (FindSlot(self, keyId, &Index))
    {
        const RegistrySlot* Slot = GetSlots(self) + Index;
        if (Slot->IsActive)
        {
            return Error_Construct4(ErrorCode_InvalidOperation,
                "Registry %s: %s is registered already (by %s), so %s cannot register it.", GetRegistryName(self),
                (const char*)KeyRegistry_GetText(self->_keys, keyId), GetSourceName(self, Slot->SourceKeyId),
                GetSourceName(self, sourceKeyId));
        }
    }
    else
    {
        if (Registry_GetCount(self) >= self->_maxEntryCount)
        {
            return Error_Construct4(ErrorCode_InvalidOperation,
                "Registry %s is full (%" PRIu32 " entries), so %s cannot register %s.", GetRegistryName(self),
                self->_maxEntryCount, GetSourceName(self, sourceKeyId),
                (const char*)KeyRegistry_GetText(self->_keys, keyId));
        }
        Error AddResult = AddSlot(self, keyId, &Index);
        if (AddResult.Code != ErrorCode_Success)
        {
            return AddResult;
        }
    }

    FillSlot(self, Index, element, sourceKeyId);
    if (outIndex != NULL)
    {
        *outIndex = Index;
    }
    return Error_CreateSuccess();
}

Error Registry_Withdraw(Registry* self, uint32_t index)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "Registry_Withdraw: self must not be NULL.");
    }
    if (index >= Registry_GetCount(self))
    {
        return Error_Construct4(ErrorCode_IndexOutOfBounds, "Registry %s: %" PRIu32 " is not an index of the registry.",
            GetRegistryName(self), index);
    }
    if (!GetSlots(self)[index].IsActive)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "Registry %s: %s is withdrawn already.",
            GetRegistryName(self), (const char*)Registry_GetKeyText(self, index));
    }
    EmptySlot(self, index);
    return Error_CreateSuccess();
}

uint32_t Registry_WithdrawSource(Registry* self, uint32_t sourceKeyId)
{
    if (self == NULL)
    {
        return 0U;
    }
    uint32_t WithdrawnCount = 0U;
    for (uint32_t Index = 0U; Index < Registry_GetCount(self); Index++)
    {
        const RegistrySlot* Slot = GetSlots(self) + Index;
        if (Slot->IsActive && (Slot->SourceKeyId == sourceKeyId))
        {
            EmptySlot(self, Index);
            WithdrawnCount++;
        }
    }
    return WithdrawnCount;
}

bool Registry_Find(Registry* self, uint32_t keyId, uint32_t* outIndex)
{
    if ((self == NULL) || (outIndex == NULL))
    {
        return false;
    }
    uint32_t Index = REGISTRY_INDEX_NONE;
    if (!FindSlot(self, keyId, &Index) || !GetSlots(self)[Index].IsActive)
    {
        return false;
    }
    *outIndex = Index;
    return true;
}

bool Registry_FindText(Registry* self, const unsigned char* key, uint32_t* outIndex)
{
    if ((self == NULL) || (key == NULL) || (outIndex == NULL))
    {
        return false;
    }
    uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
    return KeyRegistry_TryFind(self->_keys, key, &KeyId) && Registry_Find(self, KeyId, outIndex);
}

Error Registry_FindByPrefix(const Registry* self, const unsigned char* prefix, GenericBuffer* outIndices)
{
    if ((self == NULL) || (prefix == NULL) || (outIndices == NULL) || (outIndices->_elementSize != sizeof(uint32_t)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "Registry_FindByPrefix: self, prefix and outIndices (a buffer of uint32_t) must not be NULL.");
    }

    size_t PrefixLength = StringUTF8_GetByteLength(prefix);
    size_t Start = outIndices->_count;
    for (uint32_t Index = 0U; Index < Registry_GetCount(self); Index++)
    {
        if (!GetSlots(self)[Index].IsActive)
        {
            continue;
        }
        const unsigned char* Text = Registry_GetKeyText(self, Index);
        size_t Matched = 0U;
        while ((Matched < PrefixLength) && (Text[Matched] == prefix[Matched]))
        {
            Matched++;
        }
        if ((Matched == PrefixLength) && !GenericBuffer_AddLast(outIndices, &Index))
        {
            GenericBuffer_SetCount(outIndices, Start);
            return Error_Construct2(ErrorCode_BufferTooSmall, "Registry_FindByPrefix: outIndices could not grow.");
        }
    }

    // Sort only the appended indices, through a view over them.
    size_t Added = outIndices->_count - Start;
    if (Added < 2U)
    {
        return Error_CreateSuccess();
    }
    GenericBuffer View;
    GenericBuffer_CreateConstant(&View, (uint32_t*)outIndices->_data + Start, Added, sizeof(uint32_t), Added);
    UserData Context = UserData_CreateEmpty();
    Memory_Copy(&self, Context.Bytes, sizeof(self));
    uint32_t Scratch[2];
    GenericBuffer_SortAscending(&View, CompareIndicesByKeyText, &Context, Scratch);
    return Error_CreateSuccess();
}

bool Registry_IsActive(const Registry* self, uint32_t index)
{
    return (self != NULL) && (index < Registry_GetCount(self)) && GetSlots(self)[index].IsActive;
}

uint32_t Registry_GetKey(const Registry* self, uint32_t index)
{
    if ((self == NULL) || (index >= Registry_GetCount(self)))
    {
        return NAMESPACED_KEY_ID_NONE;
    }
    return GetSlots(self)[index].KeyId;
}

const unsigned char* Registry_GetKeyText(const Registry* self, uint32_t index)
{
    if ((self == NULL) || (index >= Registry_GetCount(self)))
    {
        return NULL;
    }
    return KeyRegistry_GetText(self->_keys, GetSlots(self)[index].KeyId);
}

uint32_t Registry_GetSource(const Registry* self, uint32_t index)
{
    if ((self == NULL) || (index >= Registry_GetCount(self)))
    {
        return NAMESPACED_KEY_ID_NONE;
    }
    return GetSlots(self)[index].SourceKeyId;
}

const void* Registry_Get(const Registry* self, uint32_t index)
{
    if (!Registry_IsActive(self, index))
    {
        return NULL;
    }
    return GetElementAt(self, index);
}


// Public functions: the root.
Error RegistryRoot_Construct(RegistryRoot* self, KeyRegistry* keys)
{
    if ((self == NULL) || (keys == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "RegistryRoot_Construct: self and keys must not be NULL.");
    }
    uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
    Error InternResult = KeyRegistry_Intern(keys, REGISTRY_ROOT_KEY, &KeyId);
    if (InternResult.Code != ErrorCode_Success)
    {
        return InternResult;
    }
    RegistryConstructOptions Options = RegistryConstructOptions_CreateDefault(keys, KeyId, sizeof(Registry*),
        REGISTRY_ROOT_MAX_ENTRY_COUNT);
    return Registry_Construct(&self->_registry, &Options);
}

void RegistryRoot_Deconstruct(RegistryRoot* self)
{
    if (self == NULL)
    {
        return;
    }
    Registry_Deconstruct(&self->_registry);
}

Error RegistryRoot_Add(RegistryRoot* self, Registry* registry)
{
    if ((self == NULL) || (registry == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "RegistryRoot_Add: self and registry must not be NULL.");
    }
    if (registry->_keys != self->_registry._keys)
    {
        return Error_Construct4(ErrorCode_IllegalArgument,
            "RegistryRoot_Add: registry %s borrows another key registry than the root.",
            (const char*)KeyRegistry_GetText(registry->_keys, registry->_keyId));
    }
    return Registry_Register(&self->_registry, registry->_keyId, &registry, NAMESPACED_KEY_ID_NONE, NULL);
}

Error RegistryRoot_Withdraw(RegistryRoot* self, const Registry* registry)
{
    if ((self == NULL) || (registry == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "RegistryRoot_Withdraw: self and registry must not be NULL.");
    }
    uint32_t Index = REGISTRY_INDEX_NONE;
    if (!Registry_Find(&self->_registry, registry->_keyId, &Index) || (RegistryRoot_Get(self, Index) != registry))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "RegistryRoot_Withdraw: the root does not list this registry under its key.");
    }
    return Registry_Withdraw(&self->_registry, Index);
}

Registry* RegistryRoot_Find(RegistryRoot* self, uint32_t keyId)
{
    uint32_t Index = REGISTRY_INDEX_NONE;
    if ((self == NULL) || !Registry_Find(&self->_registry, keyId, &Index))
    {
        return NULL;
    }
    return RegistryRoot_Get(self, Index);
}

Registry* RegistryRoot_FindText(RegistryRoot* self, const unsigned char* key)
{
    uint32_t Index = REGISTRY_INDEX_NONE;
    if ((self == NULL) || !Registry_FindText(&self->_registry, key, &Index))
    {
        return NULL;
    }
    return RegistryRoot_Get(self, Index);
}

Registry* RegistryRoot_Get(const RegistryRoot* self, uint32_t index)
{
    if (self == NULL)
    {
        return NULL;
    }
    const void* Element = Registry_Get(&self->_registry, index);
    if (Element == NULL)
    {
        return NULL;
    }
    Registry* Result = NULL;
    Memory_Copy(Element, &Result, sizeof(Result));
    return Result;
}
