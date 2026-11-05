// The prefabs system: see WorldPrefabs.h. It follows the components, loads their prefabs, and makes and removes a copy's objects;
// WorldPrefabsApply.c works out the rows.

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldPrefabsInternal.h"
#include "AssetDefinitionCommon.h"
#include "DataMap.h"
#include "NamespacedKey.h"
#include "WorldInternal.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** FNV-1a's 64-bit basis and prime. */
#define PREFAB_ID_HASH_BASIS UINT64_C(0xCBF29CE484222325)
#define PREFAB_ID_HASH_PRIME UINT64_C(0x100000001B3)
/** MurmurHash3's finalizer, which spreads every input bit over the output. */
#define PREFAB_ID_MIX_SHIFT 33U
#define PREFAB_ID_MIX_FIRST UINT64_C(0xFF51AFD7ED558CCD)
#define PREFAB_ID_MIX_SECOND UINT64_C(0xC4CEB9FE1A85EC53)
/** How many salts an object tries before it gives up its id: a second one is already a one in 2^63. */
#define PREFAB_MAX_ID_SALTS 64U
/** The bytes of a number the hash takes, and of a warning's text. */
#define PREFAB_BYTE_MASK 0xFFU
#define PREFAB_BITS_PER_BYTE 8U
#define PREFAB_WARNING_CAPACITY ((size_t)512)
#define PREFAB_NAME_PRINT_CAPACITY ((size_t)129)
/** How many passes settle one update: nested copies reach no deeper than the hierarchy, and each pass reaches one level. */
#define WORLD_PREFABS_MAX_PASSES ((size_t)(WORLD_HIERARCHY_MAX_DEPTH + 2U))
/** The object table's rows a copy's object does not copy from its template. */
#define PARENT_ROW_KEY ((const unsigned char*)u8"parent")
#define SOCKET_ROW_KEY ((const unsigned char*)u8"parent_socket")


// Static functions: ids.
static uint64_t HashBytes(uint64_t hash, const unsigned char* bytes, size_t count)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        hash = (hash ^ (uint64_t)bytes[Index]) * PREFAB_ID_HASH_PRIME;
    }
    return hash;
}

/* In little-endian order whatever the machine's: an id is the same everywhere. */
static uint64_t HashNumber(uint64_t hash, uint64_t number)
{
    for (size_t Byte = 0U; Byte < sizeof(number); Byte++)
    {
        hash = (hash ^ ((number >> (Byte * PREFAB_BITS_PER_BYTE)) & PREFAB_BYTE_MASK)) * PREFAB_ID_HASH_PRIME;
    }
    return hash;
}


// Static functions: warnings.
static bool HasWarned(PrefabCopyState* state, uint64_t hash)
{
    const uint64_t* Warned = (const uint64_t*)state->Warned._data;
    for (size_t Index = 0U; Index < state->Warned._count; Index++)
    {
        if (Warned[Index] == hash)
        {
            return true;
        }
    }
    return false;
}


// Static functions: making objects.
static const WorldPrefabComponent* FindComponent(const WorldPrefabs* self, const PrefabCopyState* state)
{
    WorldComponent* Component = NULL;
    if (!World_TryGetComponent(self->_world, state->RootId, WorldComponentType_Prefab, &Component)
        || (WorldComponent_GetSerial(Component) != state->Serial))
    {
        return NULL;
    }
    return WorldPrefabComponent_FromComponent(Component);
}

static bool HoldsObjectId(const DataValue* value)
{
    if (value->Type == DataType_Object)
    {
        return true;
    }
    for (size_t Index = 0U; Index < DataValue_GetListCount(value); Index++)
    {
        if (HoldsObjectId(DataValue_GetListElement(value, Index)))
        {
            return true;
        }
    }
    for (size_t Index = 0U; Index < DataValue_GetCompoundCount(value); Index++)
    {
        if (HoldsObjectId(DataValue_GetCompoundEntryValue(value, Index)))
        {
            return true;
        }
    }
    return false;
}

/* What an object id in a template's data means in a copy: that object of the copy, by its name, made yet or not; none if it is no object of
 * the prefab. */
static uint64_t GetCopyObjectId(const PrefabCopyState* state, uint64_t templateId)
{
    uint32_t Index = 0U;
    if (!Prefab_TryFindObjectById(state->Asset, templateId, &Index))
    {
        return 0U;
    }
    return (state->ObjectIds[Index] != 0U) ? state->ObjectIds[Index]
        : WorldPrefabs_DeriveObjectId(state->RootId, Prefab_GetObjectName(state->Asset, Index), 0U);
}

/* A copy of a value whose object ids mean this copy's objects. */
static Error RemapObjectIds(const PrefabCopyState* state, const DataValue* value, DataValue* outValue)
{
    if (value->Type == DataType_Object)
    {
        *outValue = DataValue_FromObject(GetCopyObjectId(state, value->ObjectValue));
        return Error_CreateSuccess();
    }
    if (value->Type == DataType_List)
    {
        Error Result = DataValue_CreateList(DataValue_GetListElementType(value), outValue);
        for (size_t Index = 0U; (Index < DataValue_GetListCount(value)) && (Result.Code == ErrorCode_Success); Index++)
        {
            DataValue Element = DataValue_CreateTag();
            Result = RemapObjectIds(state, DataValue_GetListElement(value, Index), &Element);
            if (Result.Code == ErrorCode_Success)
            {
                Result = DataValue_ListAdd(outValue, &Element);
            }
            DataValue_Deconstruct(&Element);
        }
        return Result;
    }
    if (value->Type == DataType_Compound)
    {
        Error Result = DataValue_CreateCompound(outValue);
        for (size_t Index = 0U; (Index < DataValue_GetCompoundCount(value)) && (Result.Code == ErrorCode_Success); Index++)
        {
            DataValue Entry = DataValue_CreateTag();
            Result = RemapObjectIds(state, DataValue_GetCompoundEntryValue(value, Index), &Entry);
            if (Result.Code == ErrorCode_Success)
            {
                Result = DataValue_CompoundSet(outValue, DataValue_GetCompoundEntryName(value, Index), &Entry);
            }
            DataValue_Deconstruct(&Entry);
        }
        return Result;
    }
    *outValue = DataValue_Clone(value);
    return Error_CreateSuccess();
}

/* A data value in the world's terms: object ids through this copy, key ids, which mean nothing in another registry, through their text. */
static Error TranslateData(WorldPrefabs* self, const PrefabCopyState* state, const DataValue* value, DataValue* outValue)
{
    DataValue Remapped = DataValue_CreateTag();
    Error Result = HoldsObjectId(value) ? RemapObjectIds(state, value, &Remapped) : Error_CreateSuccess();
    const DataValue* Value = (Remapped.Type != DataType_Tag) ? &Remapped : value;
    if ((Result.Code == ErrorCode_Success) && DataValue_HoldsKey(Value))
    {
        JSONObjectPool* Pool = WorldPrefabsInternal_GetPool(self);
        JSONObjectValue Json = JSONObjectValue_CreateNull();
        Result = DataValue_WriteJSON(Value, Prefab_GetKeys(state->Asset), Pool, &Json);
        if (Result.Code == ErrorCode_Success)
        {
            Result = DataValue_ReadJSON(&Json, World_GetKeys(self->_world), NULL, outValue);
            Error ReturnResult = JSONObjectPool_ReturnValue(Pool, &Json);
            Error_Deconstruct(&ReturnResult);
        }
    }
    else if (Result.Code == ErrorCode_Success)
    {
        *outValue = DataValue_Clone(Value);
    }
    DataValue_Deconstruct(&Remapped);
    return Result;
}

/* Copies a template's tags and data into an object, from the prefab's registry to the world's. */
static void CopyData(WorldPrefabs* self, PrefabCopyState* state, const WorldObject* template, uint64_t id)
{
    const DataMap* Map = WorldObject_GetData(template);
    const KeyRegistry* PrefabKeys = Prefab_GetKeys(state->Asset);
    KeyRegistry* WorldKeys = World_GetKeys(self->_world);
    for (size_t Index = 0U; Index < DataMap_GetCount(Map); Index++)
    {
        const DataMapEntry* Entry = DataMap_GetEntry(Map, Index);
        uint32_t Key = NAMESPACED_KEY_ID_NONE;
        Error Result = KeyRegistry_Intern(WorldKeys, KeyRegistry_GetText(PrefabKeys, Entry->Key), &Key);
        if ((Result.Code == ErrorCode_Success) && (Entry->Value.Type == DataType_Tag))
        {
            Result = World_AddTag(self->_world, id, Key);
        }
        else if ((Result.Code == ErrorCode_Success) && !DataValue_HoldsKey(&Entry->Value) && !HoldsObjectId(&Entry->Value))
        {
            Result = World_SetData(self->_world, id, Key, &Entry->Value);
        }
        else if (Result.Code == ErrorCode_Success)
        {
            DataValue Translated = DataValue_CreateTag();
            Result = TranslateData(self, state, &Entry->Value, &Translated);
            if (Result.Code == ErrorCode_Success)
            {
                Result = World_SetData(self->_world, id, Key, &Translated);
            }
            DataValue_Deconstruct(&Translated);
        }
        if (Result.Code != ErrorCode_Success)
        {
            WorldPrefabsInternal_Warn(self, state, "leaves out some of its objects' data: %s",
                (Result.Message != NULL) ? (const char*)Result.Message : "");
            Error_Deconstruct(&Result);
        }
    }
}

/* Copies a template's rows into an object: all but where it hangs, which the world says, and its tags and data, which are a registry's. */
static void CopyObjectRows(const WorldObject* template, WorldObject* object)
{
    const PropertyTable* Table = WorldObject_GetPropertyTable();
    for (size_t Row = 0U; Row < Table->DescriptorCount; Row++)
    {
        const PropertyDescriptor* Descriptor = &Table->Descriptors[Row];
        if ((Descriptor->Type == PropertyType_DataMap) || WorldInternal_AreStringsEqual(Descriptor->Key, PARENT_ROW_KEY)
            || WorldInternal_AreStringsEqual(Descriptor->Key, SOCKET_ROW_KEY))
        {
            continue;
        }
        PropertyValue From;
        PropertyValue To;
        Error Result = Properties_Get(Table, template, Descriptor->Key, &From);
        if (Result.Code == ErrorCode_Success)
        {
            Result = Properties_Get(Table, object, Descriptor->Key, &To);
        }
        if ((Result.Code == ErrorCode_Success) && !PropertyValue_Equals(&From, &To))
        {
            Result = Properties_Set(Table, object, Descriptor->Key, &From);
        }
        Error_Deconstruct(&Result); // a template's rows passed the same rules
    }
}

/* Adds a template's components to an object, each a copy of its rows. */
static void CopyComponents(WorldPrefabs* self, PrefabCopyState* state, const WorldObject* template, WorldObject* object)
{
    uint64_t Id = WorldObject_GetId(object);
    for (size_t Type = 0U; Type < (size_t)WorldComponentType_Count; Type++)
    {
        WorldComponent* From = WorldObject_GetComponent(template, (WorldComponentType)Type);
        if (From == NULL)
        {
            continue;
        }
        const WorldComponentTypeInfo* Info = World_GetComponentTypeInfo(self->_world, (WorldComponentType)Type);
        WorldComponent* To = NULL;
        Error Result = (Info != NULL) ? World_AddComponent(self->_world, Id, (WorldComponentType)Type, &To)
            : Error_Construct2(ErrorCode_InvalidOperation, "the world has no such component type.");
        if (Result.Code == ErrorCode_Success)
        {
            Result = Properties_CopyTo(Info->Table, From, To);
        }
        if (Result.Code != ErrorCode_Success)
        {
            WorldPrefabsInternal_Warn(self, state, "leaves out a component: %s", (Result.Message != NULL) ? (const char*)Result.Message : "");
            Error_Deconstruct(&Result);
        }
    }
}

/* Makes the object of an index under its parent, the id free of any other's. False when it cannot: what lies under it is not made. */
static bool MakeObject(WorldPrefabs* self, PrefabCopyState* state, size_t index)
{
    if (self->_stats.ObjectCount >= self->_limits.MaxObjects)
    {
        WorldPrefabsInternal_Warn(self, state, "makes more objects than the world's copies may hold: the rest are left out.");
        state->IsShort = true;
        return false;
    }
    const unsigned char* Name = Prefab_GetObjectName(state->Asset, index);
    uint32_t ParentIndex = Prefab_GetObjectParent(state->Asset, index);
    uint64_t ParentId = (ParentIndex == PREFAB_NO_PARENT) ? state->RootId : state->ObjectIds[ParentIndex];
    if (ParentId == 0U)
    {
        return false;
    }
    uint64_t Id = 0U;
    WorldObject* Taken = NULL;
    for (uint32_t Salt = 0U; Salt < PREFAB_MAX_ID_SALTS; Salt++)
    {
        Id = WorldPrefabs_DeriveObjectId(state->RootId, Name, Salt);
        if (!World_TryGetObject(self->_world, Id, &Taken))
        {
            break;
        }
        if (Salt == 0U)
        {
            WorldPrefabsInternal_Warn(self, state, "has an object whose id another object holds: \"%s\" takes another.", (const char*)Name);
        }
        Id = 0U;
    }
    WorldObject* Object = NULL;
    Error Result = (Id != 0U) ? World_CreateDerivedObject(self->_world, Name, Id, &Object)
        : Error_Construct2(ErrorCode_InvalidOperation, "no id is free.");
    const WorldObject* Template = Prefab_GetObjectTemplate(state->Asset, index);
    if (Result.Code == ErrorCode_Success)
    {
        Result = World_SetParentSocket(self->_world, Id, ParentId, WorldObject_GetParentSocket(Template), false);
    }
    if (Result.Code != ErrorCode_Success)
    {
        WorldPrefabsInternal_Warn(self, state, "cannot make the object \"%s\": %s", (const char*)Name,
            (Result.Message != NULL) ? (const char*)Result.Message : "");
        Error_Deconstruct(&Result);
        if (Object != NULL)
        {
            Result = World_RemoveObject(self->_world, Id);
            Error_Deconstruct(&Result);
        }
        return false;
    }
    CopyObjectRows(Template, Object);
    CopyData(self, state, Template, Id);
    CopyComponents(self, state, Template, Object);
    state->ObjectIds[index] = Id;
    state->MadeCount++;
    self->_stats.ObjectCount++;
    return true;
}

/* Removes every object a copy holds; what lies under an object goes with it, so the prefab's root takes nearly all at once. */
static void RemoveMade(WorldPrefabs* self, PrefabCopyState* state)
{
    self->_stats.ObjectCount -= state->MadeCount;
    self->_stats.ByteCount -= state->HeldBytes + state->WrittenBytes;
    state->MadeCount = 0U;
    state->HeldBytes = 0U;
    state->WrittenBytes = 0U;
    state->IsShort = false;
    size_t Count = (state->Asset != NULL) ? Prefab_GetObjectCount(state->Asset) : 0U;
    for (size_t Index = 0U; (state->ObjectIds != NULL) && (Index < Count); Index++)
    {
        if (WorldPrefabsInternal_FindHeld(self, state->ObjectIds[Index]) != NULL)
        {
            Error Result = World_RemoveObject(self->_world, state->ObjectIds[Index]);
            Error_Deconstruct(&Result);
        }
    }
    Memory_Free(state->ObjectIds);
    state->ObjectIds = NULL;
    GenericBuffer_Clear(&state->Touched);
}

/* Removes what every copy holds in one go: one by one it would walk the world's lists once per copy. The copies' own books are RemoveMade's. */
static void RemoveEveryMade(WorldPrefabs* self)
{
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
    const PrefabCopyState* States = (const PrefabCopyState*)self->_states._data;
    for (size_t Index = 0U; Index < self->_states._count; Index++)
    {
        size_t Count = (States[Index].ObjectIds != NULL) ? Prefab_GetObjectCount(States[Index].Asset) : 0U;
        for (size_t Object = 0U; Object < Count; Object++)
        {
            if (WorldPrefabsInternal_FindHeld(self, States[Index].ObjectIds[Object]) != NULL)
            {
                GenericBuffer_AddLast(&Ids, &States[Index].ObjectIds[Object]);
            }
        }
    }
    Error Result = World_RemoveObjects(self->_world, (const uint64_t*)Ids._data, Ids._count);
    Error_Deconstruct(&Result);
    Memory_Free(Ids._data);
}

/* Makes what a copy's objects lack and removes what its overrides remove. */
static void SyncObjects(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component)
{
    size_t Count = Prefab_GetObjectCount(state->Asset);
    state->IsShort = false;
    if (state->ObjectIds == NULL)
    {
        // Past the budgets a copy costs nothing: they bound what a small file can ask for.
        size_t Bytes = Prefab_GetFileByteCount(state->Asset);
        if (self->_stats.ObjectCount >= self->_limits.MaxObjects)
        {
            WorldPrefabsInternal_Warn(self, state, "makes more objects than the world's copies may hold: the rest are left out.");
            state->IsShort = true;
            return;
        }
        if ((self->_stats.ByteCount + Bytes) > self->_limits.MaxBytes)
        {
            WorldPrefabsInternal_Warn(self, state, "makes nothing: its prefab's file is more than the world's copies may still hold.");
            state->IsShort = true;
            return;
        }
        state->ObjectIds = Memory_Allocate(Count * sizeof(uint64_t));
        Memory_Zero(state->ObjectIds, Count * sizeof(uint64_t));
        state->HeldBytes = Bytes;
        self->_stats.ByteCount += Bytes;
    }
    GenericBuffer_Clear(&self->_removed);
    GenericBuffer_EnsureTotalCapacity(&self->_removed, Count);
    Memory_Zero(self->_removed._data, Count * sizeof(bool));
    GenericBuffer_SetCount(&self->_removed, Count);
    WorldPrefabsApply_FindRemoved(self, state, component, (bool*)self->_removed._data);
    const bool* Removed = (const bool*)self->_removed._data;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        bool Exists = WorldPrefabsInternal_FindHeld(self, state->ObjectIds[Index]) != NULL;
        if (Removed[Index])
        {
            if (Exists)
            {
                Error Result = World_RemoveObject(self->_world, state->ObjectIds[Index]);
                Error_Deconstruct(&Result);
            }
            if (state->ObjectIds[Index] != 0U)
            {
                state->MadeCount--;
                self->_stats.ObjectCount--;
            }
            state->ObjectIds[Index] = 0U;
        }
        else if (!Exists)
        {
            // Removed from outside, or never made: the count follows what is held.
            if (state->ObjectIds[Index] != 0U)
            {
                state->MadeCount--;
                self->_stats.ObjectCount--;
                state->ObjectIds[Index] = 0U;
            }
            MakeObject(self, state, Index);
        }
    }
}

/* Whether a copy's prefab would hold itself, or nest too deep: walking up the copies its root lies in. A saved object ends the walk:
 * an ordinary child of a copy's root is no part of that copy. */
static bool CheckNesting(WorldPrefabs* self, PrefabCopyState* state, const unsigned char* name)
{
    WorldObject* Current = NULL;
    if (!World_TryGetObject(self->_world, state->RootId, &Current))
    {
        return false;
    }
    size_t Depth = 1U;
    while (!WorldObject_IsPersistent(Current) && (WorldObject_GetParent(Current) != NULL))
    {
        Current = WorldObject_GetParent(Current);
        const WorldPrefabComponent* Outer = WorldPrefabComponent_FromComponent(WorldObject_GetComponent(Current, WorldComponentType_Prefab));
        if (Outer == NULL)
        {
            continue;
        }
        Depth++;
        if (WorldInternal_AreStringsEqual(WorldPrefabComponent_GetPrefab(Outer), name))
        {
            WorldPrefabsInternal_Warn(self, state, "makes nothing: the prefab \"%s\" would hold itself.", (const char*)name);
            return false;
        }
    }
    if (Depth > WORLD_PREFABS_MAX_NESTING)
    {
        WorldPrefabsInternal_Warn(self, state, "makes nothing: copies nest at most %" PRIu64 " deep.", (uint64_t)WORLD_PREFABS_MAX_NESTING);
        return false;
    }
    return true;
}

/* Resolves the prefab a component names, when the name changed; a change makes the copy anew. */
static void ResolveAsset(WorldPrefabs* self, PrefabCopyState* state, const WorldPrefabComponent* component)
{
    const unsigned char* Name = WorldPrefabComponent_GetPrefab(component);
    if (state->IsResolved && WorldInternal_AreStringsEqual(Name, state->AssetName))
    {
        return;
    }
    RemoveMade(self, state);
    Memory_Free(state->AssetName);
    state->AssetName = WorldInternal_CloneString(Name);
    state->Asset = NULL;
    state->IsResolved = true;
    if ((Name == NULL) || !CheckNesting(self, state, Name))
    {
        return;
    }
    Prefab* Asset = NULL;
    Error Result = AssetManager_LoadPrefab(self->_assets, Name, self->_assetUser, &Asset);
    if (Result.Code != ErrorCode_Success)
    {
        WorldPrefabsInternal_Warn(self, state, "makes nothing: its prefab does not load: %s", (Result.Message != NULL) ? (const char*)Result.Message : "");
        Error_Deconstruct(&Result);
        return;
    }
    state->Asset = Asset;
}

/* One copy: built again when a row of it changed. */
static void RefreshCopy(WorldPrefabs* self, PrefabCopyState* state)
{
    const WorldPrefabComponent* Component = FindComponent(self, state);
    if (Component == NULL)
    {
        return; // it went during this update: the next pass drops its state
    }
    uint32_t Revision = Component->Base._revision;
    bool FindsRoom = state->IsShort && (self->_stats.ObjectCount < self->_limits.MaxObjects) && (self->_stats.ByteCount < self->_limits.MaxBytes);
    if (state->HasBuilt && (Revision == state->SeenRevision) && !FindsRoom)
    {
        return;
    }
    if (self->_hasWorked)
    {
        // One update's work is spent: the rest waits. An update that starts on a copy always does it, however large.
        size_t Cost = 1U;
        if (state->Asset != NULL)
        {
            Cost += Prefab_GetObjectCount(state->Asset);
        }
        if (Cost > self->_workLeft)
        {
            self->_hasPending = true;
            return;
        }
    }
    ResolveAsset(self, state, Component);
    state->HasBuilt = true;
    state->SeenRevision = Revision;
    WorldPrefabsInternal_SpendWork(self, 1U);
    if (state->Asset != NULL)
    {
        SyncObjects(self, state, Component);
    }
    if (state->ObjectIds != NULL)
    {
        // Every object looked at, and what is copied from the file to make them or to start each row from.
        WorldPrefabsInternal_SpendWork(self, Prefab_GetObjectCount(state->Asset) + (state->HeldBytes / WORLD_PREFABS_BYTES_PER_WORK));
        if (!WorldPrefabsApply_Run(self, state, Component))
        {
            state->HasBuilt = false; // comes back next update: what is made stays, and the name is known
            self->_hasPending = true;
        }
    }
    self->_hasWorked = true;
}


// Static functions: states.
static void DropState(WorldPrefabs* self, PrefabCopyState* state)
{
    RemoveMade(self, state);
    Memory_Free(state->AssetName);
    Memory_Free(state->Touched._data);
    Memory_Free(state->Warned._data);
}

static bool IsStateListed(GenericBuffer* buffer, GenericBufferElementData element, const UserData* userData)
{
    (void)buffer;
    (void)userData;
    return ((const PrefabCopyState*)element._element)->IsListed;
}

/* A state for each component; a component that went takes its objects along. The components and the states both stand in the order the
 * components were added, the order of their serials, so one walk along both matches them, whatever came and went. */
static void RematchStates(WorldPrefabs* self)
{
    size_t Count = 0U;
    WorldComponent* const* Components = World_GetComponents(self->_world, WorldComponentType_Prefab, &Count);
    PrefabCopyState* States = (PrefabCopyState*)self->_states._data;
    size_t Next = 0U;
    for (size_t Index = 0U; Index < self->_states._count; Index++)
    {
        States[Index].IsListed = (Next < Count) && (States[Index].Serial == WorldComponent_GetSerial(Components[Next]));
        Next += States[Index].IsListed ? 1U : 0U;
    }
    for (; Next < Count; Next++)
    {
        PrefabCopyState Fresh;
        Memory_Zero(&Fresh, sizeof(Fresh));
        Fresh.Serial = WorldComponent_GetSerial(Components[Next]);
        Fresh.RootId = WorldObject_GetId(WorldComponent_GetOwner(Components[Next]));
        Fresh.IsListed = true;
        GenericBuffer_AllocateVariable(&Fresh.Touched, 0U, sizeof(PrefabTouchedRow));
        GenericBuffer_AllocateVariable(&Fresh.Warned, 0U, sizeof(uint64_t));
        GenericBuffer_AddLast(&self->_states, &Fresh);
    }
    States = (PrefabCopyState*)self->_states._data;
    for (size_t Index = 0U; Index < self->_states._count; Index++)
    {
        if (!States[Index].IsListed)
        {
            DropState(self, &States[Index]);
        }
    }
    GenericBuffer_Filter(&self->_states, IsStateListed, NULL);
    self->_stats.CopyCount = Count;
}

/* The state of a copy's root. */
static const PrefabCopyState* FindRootState(const WorldPrefabs* self, uint64_t rootId)
{
    WorldComponent* Component = NULL;
    if (!World_TryGetComponent(self->_world, rootId, WorldComponentType_Prefab, &Component))
    {
        return NULL;
    }
    // The states stand in the order of their serials.
    uint64_t Serial = WorldComponent_GetSerial(Component);
    const PrefabCopyState* States = (const PrefabCopyState*)self->_states._data;
    size_t Low = 0U;
    size_t High = self->_states._count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (States[Middle].Serial < Serial)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return ((Low < self->_states._count) && (States[Low].Serial == Serial)) ? &States[Low] : NULL;
}


// Internal functions.
void WorldPrefabsInternal_Warn(WorldPrefabs* self, PrefabCopyState* state, const char* format, ...)
{
    if ((self->_logger == NULL) || (self->_warningCount >= self->_limits.MaxWarnings))
    {
        return;
    }
    char Text[PREFAB_WARNING_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        return;
    }
    uint64_t Hash = HashBytes(PREFAB_ID_HASH_BASIS, (const unsigned char*)Text, StringUTF8_GetByteLength((const unsigned char*)Text));
    if (HasWarned(state, Hash) || (state->Warned._count >= WORLD_PREFABS_MAX_WARNINGS))
    {
        return;
    }
    GenericBuffer_AddLast(&state->Warned, &Hash);
    WorldObject* Root = NULL;
    const unsigned char* Name = World_TryGetObject(self->_world, state->RootId, &Root) ? WorldObject_GetName(Root) : NULL;
    char Printable[PREFAB_WARNING_CAPACITY];
    char PrintableName[PREFAB_NAME_PRINT_CAPACITY];
    AssetJSON_CopyPrintable((const unsigned char*)Text, Printable, sizeof(Printable));
    AssetJSON_CopyPrintable((Name != NULL) ? Name : (const unsigned char*)u8"", PrintableName, sizeof(PrintableName));
    Error Result = Logger_LogFormatted(self->_logger, LogLevel_Warning, (const unsigned char*)u8"WorldPrefabs: object %" PRIu64 " (\"%s\") %s",
        state->RootId, PrintableName, Printable);
    Error_Deconstruct(&Result);
    self->_warningCount++;
    if (self->_warningCount == self->_limits.MaxWarnings)
    {
        Result = Logger_LogFormatted(self->_logger, LogLevel_Warning, (const unsigned char*)u8"WorldPrefabs: that is %zu warnings, so it stays quiet "
            "until the next rebuild.", self->_warningCount);
        Error_Deconstruct(&Result);
    }
}

WorldObject* WorldPrefabsInternal_FindHeld(const WorldPrefabs* self, uint64_t id)
{
    WorldObject* Found = NULL;
    return (World_TryGetObject(self->_world, id, &Found) && !WorldObject_IsPersistent(Found)) ? Found : NULL;
}

void WorldPrefabsInternal_SpendWork(WorldPrefabs* self, size_t work)
{
    self->_workLeft = (self->_workLeft > work) ? (self->_workLeft - work) : 0U;
}

JSONObjectPool* WorldPrefabsInternal_GetPool(WorldPrefabs* self)
{
    if (self->_pool == NULL)
    {
        Error Result = JSONObjectPool_Create(&self->_pool);
        Error_Deconstruct(&Result);
    }
    return self->_pool;
}


// Functions.
Error WorldPrefabs_Create(World* world, AssetManager* assets, Logger* logger, WorldPrefabs** outSystem)
{
    if ((world == NULL) || (assets == NULL) || (outSystem == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldPrefabs_Create: the world, the asset manager and the out parameter are required.");
    }
    WorldPrefabs* Self = Memory_Allocate(sizeof(WorldPrefabs));
    Memory_Zero(Self, sizeof(*Self));
    Self->_world = world;
    Self->_assets = assets;
    Self->_logger = logger;
    Self->_limits = WorldPrefabsLimits_CreateDefault();
    Error Result = AssetManager_GetNewUserID(assets, &Self->_assetUser);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Self);
        return Result;
    }
    GenericBuffer_AllocateVariable(&Self->_states, 0U, sizeof(PrefabCopyState));
    GenericBuffer_AllocateVariable(&Self->_removed, 0U, sizeof(bool));
    WorldPrefabsApply_ConstructScratch(Self);
    *outSystem = Self;
    return Error_CreateSuccess();
}

void WorldPrefabs_Destroy(WorldPrefabs* self)
{
    if (self == NULL)
    {
        return;
    }
    RemoveEveryMade(self);
    PrefabCopyState* States = (PrefabCopyState*)self->_states._data;
    for (size_t Index = 0U; Index < self->_states._count; Index++)
    {
        DropState(self, &States[Index]);
    }
    Memory_Free(self->_states._data);
    Memory_Free(self->_removed._data);
    WorldPrefabsApply_DeconstructScratch(self);
    if (self->_pool != NULL)
    {
        Error PoolResult = JSONObjectPool_Deconstruct(self->_pool);
        Error_Deconstruct(&PoolResult);
    }
    Error Result = AssetManager_RetireUser(self->_assets, self->_assetUser);
    Error_Deconstruct(&Result);
    Memory_Free(self);
}

void WorldPrefabs_Update(WorldPrefabs* self)
{
    uint64_t SetVersion = World_GetComponentSetVersion(self->_world);
    uint64_t Revision = World_GetComponentTypeRevision(self->_world, WorldComponentType_Prefab);
    if (self->_hasSeen && !self->_hasPending && (SetVersion == self->_seenSetVersion) && (Revision == self->_seenRevision))
    {
        return;
    }
    self->_workLeft = self->_limits.MaxUpdateWork;
    self->_hasWorked = false;
    self->_hasPending = false;
    bool IsStateListCurrent = self->_hasSeen && (SetVersion == self->_seenSetVersion);
    bool IsQuiet = false;
    for (size_t Pass = 0U; (Pass < WORLD_PREFABS_MAX_PASSES) && !IsQuiet; Pass++)
    {
        self->_stats.PassCount++;
        SetVersion = World_GetComponentSetVersion(self->_world);
        Revision = World_GetComponentTypeRevision(self->_world, WorldComponentType_Prefab);
        if (!IsStateListCurrent)
        {
            RematchStates(self);
        }
        // By the states, which nothing below adds to: a copy's build can remove or add components, which the next pass takes in. It goes on
        // from where the last update stopped, so copies that keep changing cannot starve those behind them.
        PrefabCopyState* States = (PrefabCopyState*)self->_states._data;
        size_t Count = self->_states._count;
        size_t First = (self->_nextState < Count) ? self->_nextState : 0U;
        for (size_t Step = 0U; Step < Count; Step++)
        {
            size_t Index = ((First + Step) < Count) ? (First + Step) : (First + Step - Count);
            bool WasPending = self->_hasPending;
            RefreshCopy(self, &States[Index]);
            if (!WasPending && self->_hasPending)
            {
                self->_nextState = Index;
            }
        }
        IsStateListCurrent = SetVersion == World_GetComponentSetVersion(self->_world);
        IsQuiet = IsStateListCurrent && (Revision == World_GetComponentTypeRevision(self->_world, WorldComponentType_Prefab));
    }
    self->_nextState = self->_hasPending ? self->_nextState : 0U;
    self->_hasPending = self->_hasPending || !IsQuiet;
    self->_hasSeen = IsStateListCurrent;
    self->_seenSetVersion = World_GetComponentSetVersion(self->_world);
    self->_seenRevision = World_GetComponentTypeRevision(self->_world, WorldComponentType_Prefab);
    if (self->_pool != NULL)
    {
        Error PoolResult = JSONObjectPool_Deconstruct(self->_pool);
        Error_Deconstruct(&PoolResult);
        self->_pool = NULL;
    }
}

bool WorldPrefabs_IsSettled(const WorldPrefabs* self)
{
    return !self->_hasPending;
}

void WorldPrefabs_Rebuild(WorldPrefabs* self)
{
    self->_warningCount = 0U;
    RemoveEveryMade(self);
    PrefabCopyState* States = (PrefabCopyState*)self->_states._data;
    for (size_t Index = 0U; Index < self->_states._count; Index++)
    {
        RemoveMade(self, &States[Index]);
        Memory_Free(States[Index].AssetName);
        States[Index].AssetName = NULL;
        States[Index].Asset = NULL;
        States[Index].IsResolved = false;
        States[Index].HasBuilt = false;
        GenericBuffer_Clear(&States[Index].Warned);
    }
    Error Result = AssetManager_ReleaseAllAssetsForUser(self->_assets, self->_assetUser);
    Error_Deconstruct(&Result);
    self->_hasSeen = false;
    WorldPrefabs_Update(self);
}

void WorldPrefabs_SetLimits(WorldPrefabs* self, const WorldPrefabsLimits* limits)
{
    self->_limits = *limits;
}

WorldPrefabsStats WorldPrefabs_GetStats(const WorldPrefabs* self)
{
    return self->_stats;
}

uint64_t WorldPrefabs_DeriveObjectId(uint64_t rootId, const unsigned char* name, uint32_t salt)
{
    uint64_t Hash = HashNumber(PREFAB_ID_HASH_BASIS, rootId);
    Hash = HashBytes(Hash, name, StringUTF8_GetByteLength(name));
    Hash = HashNumber(Hash, (uint64_t)salt);
    Hash ^= Hash >> PREFAB_ID_MIX_SHIFT;
    Hash *= PREFAB_ID_MIX_FIRST;
    Hash ^= Hash >> PREFAB_ID_MIX_SHIFT;
    Hash *= PREFAB_ID_MIX_SECOND;
    Hash ^= Hash >> PREFAB_ID_MIX_SHIFT;
    Hash |= WORLD_OBJECT_DERIVED_ID_FIRST;
    return (Hash > WORLD_OBJECT_ID_MAX) ? WORLD_OBJECT_ID_MAX : Hash;
}

bool WorldPrefabs_TryGetCopyRoot(World* world, uint64_t objectId, uint64_t* outRootId)
{
    WorldObject* Current = NULL;
    if ((world == NULL) || (outRootId == NULL) || !World_TryGetObject(world, objectId, &Current))
    {
        return false;
    }
    while (Current != NULL)
    {
        if (WorldObject_GetComponent(Current, WorldComponentType_Prefab) != NULL)
        {
            *outRootId = WorldObject_GetId(Current);
            return true;
        }
        if (WorldObject_IsPersistent(Current))
        {
            return false; // an ordinary object: it, and what it generates, belongs to no copy
        }
        Current = WorldObject_GetParent(Current);
    }
    return false;
}

bool WorldPrefabs_TryGetOutermostCopyRoot(World* world, uint64_t objectId, uint64_t* outRootId)
{
    uint64_t Root = 0U;
    if (!WorldPrefabs_TryGetCopyRoot(world, objectId, &Root) || (outRootId == NULL))
    {
        return false;
    }
    for (size_t Level = 0U; Level < WORLD_HIERARCHY_MAX_DEPTH; Level++)
    {
        WorldObject* Object = NULL;
        uint64_t Outer = 0U;
        if (!World_TryGetObject(world, Root, &Object) || WorldObject_IsPersistent(Object) || (WorldObject_GetParent(Object) == NULL)
            || !WorldPrefabs_TryGetCopyRoot(world, WorldObject_GetId(WorldObject_GetParent(Object)), &Outer))
        {
            break;
        }
        Root = Outer;
    }
    *outRootId = Root;
    return true;
}

bool WorldPrefabs_TryFindObject(const WorldPrefabs* self, uint64_t rootId, const unsigned char* path, uint64_t* outObjectId)
{
    if ((path == NULL) || (outObjectId == NULL))
    {
        return false;
    }
    uint64_t Root = rootId;
    const unsigned char* Rest = path;
    for (size_t Level = 0U; Level <= WORLD_PREFABS_MAX_NESTING; Level++)
    {
        const PrefabCopyState* State = FindRootState(self, Root);
        if ((State == NULL) || (State->Asset == NULL))
        {
            return false;
        }
        size_t Length = 0U;
        while ((Rest[Length] != 0U) && (Rest[Length] != WORLD_PREFAB_PATH_SEPARATOR))
        {
            Length++;
        }
        uint32_t Index = 0U;
        if ((State->ObjectIds == NULL) || !Prefab_TryFindObject(State->Asset, Rest, Length, &Index)
            || (WorldPrefabsInternal_FindHeld(self, State->ObjectIds[Index]) == NULL))
        {
            return false;
        }
        if (Rest[Length] == 0U)
        {
            *outObjectId = State->ObjectIds[Index];
            return true;
        }
        Root = State->ObjectIds[Index];
        Rest += Length + 1U;
    }
    return false;
}

bool WorldPrefabs_GetCopyObjects(const WorldPrefabs* self, uint64_t rootId, GenericBuffer* outIds)
{
    const PrefabCopyState* State = FindRootState(self, rootId);
    if ((State == NULL) || (outIds == NULL))
    {
        return false;
    }
    size_t Count = (State->ObjectIds != NULL) ? Prefab_GetObjectCount(State->Asset) : 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if (WorldPrefabsInternal_FindHeld(self, State->ObjectIds[Index]) != NULL)
        {
            GenericBuffer_AddLast(outIds, &State->ObjectIds[Index]);
        }
    }
    return true;
}

const Prefab* WorldPrefabs_GetPrefab(const WorldPrefabs* self, uint64_t rootId)
{
    const PrefabCopyState* State = FindRootState(self, rootId);
    return (State != NULL) ? State->Asset : NULL;
}
