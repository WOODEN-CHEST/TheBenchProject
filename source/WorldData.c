// The world's side of object tags and data: the data functions of World.h (limits, the data revision), the tag index
// queries use (tag key id -> the ids of the objects carrying it, ascending), and the byte count.

#include <inttypes.h>
#include <stdio.h>
#include "World.h"
#include "WorldInternal.h"
#include "Logger.h"
#include "wr/WRCompile.h"
#include "wr/WRHash.h"
#include "wr/WRMemory.h"


// Macros.
/** The data limits, one bit each in World._loggedDataLimits. */
#define DATA_LIMIT_ENTRIES 1U
#define DATA_LIMIT_OBJECT_BYTES 2U
#define DATA_LIMIT_WORLD_BYTES 4U
/** The first capacity of a tag's member list. */
#define TAG_MEMBERS_FIRST_CAPACITY ((size_t)4)


// Types.
/* The objects carrying one tag: their ids, ascending. Stored by value in the tag index; an entry stays (with no ids)
 * when its last object loses the tag, so changing tags never removes index entries. */
typedef struct WorldTagMembersStruct
{
    uint64_t* Ids;
    size_t Count;
    size_t Capacity;
} WorldTagMembers;


// Static functions: the tag index.
static HashCode HashTagKey(IMap* map, const void* key, const UserData* userData)
{
    UNUSED(map);
    UNUSED(userData);
    uint32_t Tag = 0U;
    Memory_Copy(key, &Tag, sizeof(Tag));
    return Hash_UInt32(Tag);
}

/* The members of @p tag (a pointer into the index: valid until an entry is added), or NULL. */
static WorldTagMembers* FindMembers(World* world, uint32_t tag)
{
    IMap* Map = HashMap_AsMap(&world->_tagIndex);
    bool IsPresent = false;
    Error ContainsResult = IMap_ContainsKey(Map, &tag, &IsPresent);
    if ((ContainsResult.Code != ErrorCode_Success) || !IsPresent)
    {
        Error_Deconstruct(&ContainsResult);
        return NULL;
    }
    void* Members = NULL;
    Error GetResult = IMap_GetPointerToElement(Map, &tag, &Members);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return NULL;
    }
    return Members;
}

/* The index of @p id in @p members, or where it would go (*outIsFound false). */
static size_t FindIdIndex(const WorldTagMembers* members, uint64_t id, bool* outIsFound)
{
    size_t Low = 0U;
    size_t High = members->Count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (members->Ids[Middle] == id)
        {
            *outIsFound = true;
            return Middle;
        }
        if (members->Ids[Middle] < id)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    *outIsFound = false;
    return Low;
}

static void IndexTag(World* world, uint32_t tag, uint64_t id)
{
    WorldTagMembers* Members = FindMembers(world, tag);
    if (Members == NULL)
    {
        WorldTagMembers Empty = { .Ids = NULL, .Count = 0U, .Capacity = 0U };
        bool WasAdded = false;
        Error AddResult = IMap_Add(HashMap_AsMap(&world->_tagIndex), &tag, &Empty, &WasAdded);
        Error_Deconstruct(&AddResult); // Allocation failures abort; the arguments are valid.
        Members = FindMembers(world, tag);
        if (Members == NULL)
        {
            return;
        }
    }
    bool IsFound = false;
    size_t Index = FindIdIndex(Members, id, &IsFound);
    if (IsFound)
    {
        return;
    }
    if (Members->Count == Members->Capacity)
    {
        size_t Start = (Members->Capacity == 0U) ? TAG_MEMBERS_FIRST_CAPACITY : Members->Capacity;
        size_t Capacity = 0U;
        if (!Memory_TryGrowCapacity(Start, Members->Count + 1U, 2U, sizeof(uint64_t), &Capacity))
        {
            return; // Unreachable: there cannot be that many objects.
        }
        Members->Ids = Memory_Reallocate(Members->Ids, Capacity * sizeof(uint64_t));
        Members->Capacity = Capacity;
    }
    size_t After = Members->Count - Index;
    if (After > 0U)
    {
        Memory_Move(&Members->Ids[Index], &Members->Ids[Index + 1U], After * sizeof(uint64_t));
    }
    Members->Ids[Index] = id;
    Members->Count++;
}

/* Takes @p id out of a member list; the list's memory goes with its last id. */
static void RemoveMember(WorldTagMembers* members, uint64_t id)
{
    bool IsFound = false;
    size_t Index = FindIdIndex(members, id, &IsFound);
    if (!IsFound)
    {
        return;
    }
    size_t After = members->Count - Index - 1U;
    if (After > 0U)
    {
        Memory_Move(&members->Ids[Index + 1U], &members->Ids[Index], After * sizeof(uint64_t));
    }
    members->Count--;
    if (members->Count == 0U)
    {
        Memory_Free(members->Ids);
        members->Ids = NULL;
        members->Capacity = 0U;
    }
}

static void UnindexTag(World* world, uint32_t tag, uint64_t id)
{
    WorldTagMembers* Members = FindMembers(world, tag);
    if (Members != NULL)
    {
        RemoveMember(Members, id);
    }
}

/* Takes @p id out of every member list: after a table write the object's old tags are gone with its old map. */
static void UnindexEverywhere(World* world, uint64_t id)
{
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(IMap_AsEntryCollection(
        HashMap_AsMap(&world->_tagIndex)));
    if (Enumerator == NULL)
    {
        return;
    }
    bool HasNext = false;
    Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        MapEntryView Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            RemoveMember(Entry._value, id);
            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        }
    }
    Error_Deconstruct(&Result);
    CollectionEnumerator_Destroy(Enumerator);
}


// Static functions: limits and bookkeeping.
/* Logs a data limit the first time the world reaches it. */
static void LogLimitOnce(World* world, uint32_t limit, const char* message)
{
    bool WasLogged = (world->_loggedDataLimits & limit) != 0U;
    world->_loggedDataLimits |= limit;
    if (WasLogged || (world->_logger == NULL))
    {
        return;
    }
    Error LogResult = Logger_LogWarningFormatted(world->_logger, u8"World: %s Not logged again for this world.", message);
    Error_Deconstruct(&LogResult);
}

/* Refuses a change that would pass a limit (and logs that limit once). @p old is the key's current value, or NULL. */
static Error CheckLimits(World* world, const WorldObject* object, uint32_t key, const DataValue* old,
    const DataValue* value)
{
    size_t OldBytes = (old != NULL) ? DataMap_GetEntryByteSize(world->_keys, key, old) : 0U;
    size_t NewBytes = DataMap_GetEntryByteSize(world->_keys, key, value);
    size_t ObjectBytes = (DataMap_GetByteSize(object->_data) - OldBytes) + NewBytes;
    size_t WorldBytes = (world->_dataByteSize - OldBytes) + NewBytes;
    char Message[192];
    if ((old == NULL) && (DataMap_GetCount(object->_data) >= WORLD_OBJECT_DATA_MAX_ENTRIES))
    {
        snprintf(Message, sizeof(Message), "object %" PRIu64 " cannot hold more than 256 data entries (tags included);"
            " the change was refused.", object->_id);
        LogLimitOnce(world, DATA_LIMIT_ENTRIES, Message);
        return Error_Construct4(ErrorCode_InvalidOperation, "World_SetData: %s", Message);
    }
    if (ObjectBytes > WORLD_OBJECT_DATA_MAX_BYTES)
    {
        snprintf(Message, sizeof(Message), "the data of object %" PRIu64 " cannot count more than 65536 bytes; the "
            "change was refused.", object->_id);
        LogLimitOnce(world, DATA_LIMIT_OBJECT_BYTES, Message);
        return Error_Construct4(ErrorCode_InvalidOperation, "World_SetData: %s", Message);
    }
    if (WorldBytes > WORLD_DATA_MAX_BYTES)
    {
        snprintf(Message, sizeof(Message), "the data of all objects cannot count more than 16 MiB; a change to object "
            "%" PRIu64 " was refused.", object->_id);
        LogLimitOnce(world, DATA_LIMIT_WORLD_BYTES, Message);
        return Error_Construct4(ErrorCode_InvalidOperation, "World_SetData: %s", Message);
    }
    return Error_CreateSuccess();
}

/* Brings the world's byte count up to date with an object's data. */
static void CountObjectBytes(World* world, WorldObject* object)
{
    size_t Bytes = DataMap_GetByteSize(object->_data);
    world->_dataByteSize = (world->_dataByteSize - object->_countedDataBytes) + Bytes;
    object->_countedDataBytes = Bytes;
}

/* Keeps an object's data NULL while it is empty. */
static void ReleaseIfEmpty(WorldObject* object)
{
    if ((object->_data != NULL) && (DataMap_GetCount(object->_data) == 0U))
    {
        DataMap_Destroy(object->_data);
        object->_data = NULL;
    }
}


// Internal functions (declared in WorldInternal.h).
Error WorldInternal_ConstructData(World* world)
{
    HashMapConstructOptions Options = HashMapConstructOptions_CreateDefault(sizeof(uint32_t), sizeof(WorldTagMembers),
        HashTagKey);
    Error Result = HashMap_Construct1(&world->_tagIndex, Options);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    world->_dataByteSize = 0U;
    world->_loggedDataLimits = 0U;
    GenericBuffer_AllocateVariable(&world->_queryCandidates, 0U, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&world->_queryMatches, 0U, sizeof(WorldQueryMatch));
    return Error_CreateSuccess();
}

void WorldInternal_DeconstructData(World* world)
{
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(IMap_AsValueCollection(
        HashMap_AsMap(&world->_tagIndex)));
    if (Enumerator != NULL)
    {
        bool HasNext = false;
        Error Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        while ((Result.Code == ErrorCode_Success) && HasNext)
        {
            WorldTagMembers Members;
            Result = CollectionEnumerator_NextByValue(Enumerator, &Members);
            if (Result.Code == ErrorCode_Success)
            {
                Memory_Free(Members.Ids);
                Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
            }
        }
        Error_Deconstruct(&Result);
        CollectionEnumerator_Destroy(Enumerator);
    }
    Error MapResult = HashMap_Deconstruct(&world->_tagIndex);
    Error_Deconstruct(&MapResult);
    Memory_Free(world->_queryCandidates._data);
    Memory_Free(world->_queryMatches._data);
    world->_dataByteSize = 0U;
}

void WorldInternal_SyncObjectData(World* world, WorldObject* object)
{
    ReleaseIfEmpty(object);
    CountObjectBytes(world, object);
    if (object->_indexedTagCount > 0U)
    {
        UnindexEverywhere(world, object->_id);
    }
    object->_indexedTagCount = 0U;
    for (size_t Index = 0U; Index < DataMap_GetCount(object->_data); Index++)
    {
        const DataMapEntry* Entry = DataMap_GetEntry(object->_data, Index);
        if (Entry->Value.Type == DataType_Tag)
        {
            IndexTag(world, Entry->Key, object->_id);
            object->_indexedTagCount++;
        }
    }
    if (world->_dataByteSize > WORLD_DATA_MAX_BYTES)
    {
        LogLimitOnce(world, DATA_LIMIT_WORLD_BYTES, "the data of all objects counts more than 16 MiB after reading or "
            "copying an object: a world file this large must be refused.");
    }
}

void WorldInternal_ForgetObjectData(World* world, WorldObject* object)
{
    for (size_t Index = 0U; Index < DataMap_GetCount(object->_data); Index++)
    {
        const DataMapEntry* Entry = DataMap_GetEntry(object->_data, Index);
        if (Entry->Value.Type == DataType_Tag)
        {
            UnindexTag(world, Entry->Key, object->_id);
        }
    }
    object->_indexedTagCount = 0U;
    world->_dataByteSize -= object->_countedDataBytes;
    object->_countedDataBytes = 0U;
}

const uint64_t* WorldInternal_GetTagMembers(World* world, uint32_t tag, size_t* outCount)
{
    const WorldTagMembers* Members = FindMembers(world, tag);
    *outCount = (Members != NULL) ? Members->Count : 0U;
    return ((Members != NULL) && (Members->Count > 0U)) ? Members->Ids : NULL;
}


// Public functions.
Error World_SetData(World* self, uint64_t id, uint32_t key, const DataValue* value)
{
    if ((self == NULL) || (value == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_SetData: self and value must not be NULL.");
    }
    if (KeyRegistry_GetText(self->_keys, key) == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_SetData: the key is not an id of the world's key registry.");
    }
    WorldObject* Object = WorldInternal_FindObject(self, id);
    if (Object == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_SetData: no object with that id.");
    }
    Error Result = DataValue_Validate(value, self->_keys);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const DataValue* Old = DataMap_Find(Object->_data, key);
    if ((Old != NULL) && DataValue_Equals(Old, value))
    {
        return Error_CreateSuccess();
    }
    Result = CheckLimits(self, Object, key, Old, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // Read both before the map changes: value may point into it (another entry, or part of the old value), and adopting
    // the copy moves or frees what it points to.
    bool WasTag = (Old != NULL) && (Old->Type == DataType_Tag);
    bool IsTag = value->Type == DataType_Tag;
    if (Object->_data == NULL)
    {
        Result = DataMap_Create(self->_keys, &Object->_data);
    }
    DataValue Copy = DataValue_Clone(value);
    if (Result.Code == ErrorCode_Success)
    {
        Result = DataMap_Adopt(Object->_data, key, &Copy, NULL);
    }
    DataValue_Deconstruct(&Copy);
    if (Result.Code != ErrorCode_Success)
    {
        ReleaseIfEmpty(Object);
        return Result;
    }

    CountObjectBytes(self, Object);
    if (IsTag && !WasTag)
    {
        IndexTag(self, key, id);
        Object->_indexedTagCount++;
    }
    else if (!IsTag && WasTag)
    {
        UnindexTag(self, key, id);
        Object->_indexedTagCount--;
    }
    Object->_dataRevision++;
    return Error_CreateSuccess();
}

bool World_TryGetData(World* self, uint64_t id, uint32_t key, const DataValue** outValue)
{
    if (outValue == NULL)
    {
        return false;
    }
    WorldObject* Object = (self != NULL) ? WorldInternal_FindObject(self, id) : NULL;
    *outValue = (Object != NULL) ? DataMap_Find(Object->_data, key) : NULL;
    return *outValue != NULL;
}

Error World_RemoveData(World* self, uint64_t id, uint32_t key, bool* outWasRemoved)
{
    if (outWasRemoved != NULL)
    {
        *outWasRemoved = false;
    }
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_RemoveData: self must not be NULL.");
    }
    WorldObject* Object = WorldInternal_FindObject(self, id);
    if (Object == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_RemoveData: no object with that id.");
    }
    const DataValue* Old = DataMap_Find(Object->_data, key);
    if (Old == NULL)
    {
        return Error_CreateSuccess();
    }

    bool WasTag = Old->Type == DataType_Tag;
    DataMap_Remove(Object->_data, key);
    if (WasTag)
    {
        UnindexTag(self, key, id);
        Object->_indexedTagCount--;
    }
    ReleaseIfEmpty(Object);
    CountObjectBytes(self, Object);
    Object->_dataRevision++;
    if (outWasRemoved != NULL)
    {
        *outWasRemoved = true;
    }
    return Error_CreateSuccess();
}

Error World_AddTag(World* self, uint64_t id, uint32_t key)
{
    DataValue Tag = DataValue_CreateTag();
    return World_SetData(self, id, key, &Tag);
}

Error World_RemoveTag(World* self, uint64_t id, uint32_t key, bool* outWasRemoved)
{
    if (outWasRemoved != NULL)
    {
        *outWasRemoved = false;
    }
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_RemoveTag: self must not be NULL.");
    }
    WorldObject* Object = WorldInternal_FindObject(self, id);
    if (Object == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_RemoveTag: no object with that id.");
    }
    if (!DataMap_HasTag(Object->_data, key))
    {
        return Error_CreateSuccess();
    }
    return World_RemoveData(self, id, key, outWasRemoved);
}

bool World_HasTag(World* self, uint64_t id, uint32_t key)
{
    WorldObject* Object = (self != NULL) ? WorldInternal_FindObject(self, id) : NULL;
    return (Object != NULL) && DataMap_HasTag(Object->_data, key);
}
