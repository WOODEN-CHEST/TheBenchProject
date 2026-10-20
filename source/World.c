#include "World.h"
#include "WorldInternal.h"
#include "wr/WRCompile.h"
#include "wr/WRHash.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include <inttypes.h>


// Macros.
/** Initial capacity (in objects) of a world's object buffer and id map. */
#define INITIAL_OBJECT_CAPACITY 32
/** Initial capacity (in components) of each per-type component array. */
#define INITIAL_COMPONENT_CAPACITY 16


// Static functions: the id map.
static HashCode HashObjectId(IMap* map, const void* key, const UserData* userData)
{
    UNUSED(map);
    UNUSED(userData);
    uint64_t Id = 0;
    Memory_Copy(key, &Id, sizeof(Id));
    return Hash_UInt64(Id);
}

/* The object with @p id, or NULL. ContainsKey first: a miss on GetElement would build an error message. */
static WorldObject* FindObject(World* self, uint64_t id)
{
    if (id == 0U)
    {
        return NULL;
    }
    IMap* Map = HashMap_AsMap(&self->_objectsById);
    bool IsPresent = false;
    Error ContainsResult = IMap_ContainsKey(Map, &id, &IsPresent);
    if ((ContainsResult.Code != ErrorCode_Success) || !IsPresent)
    {
        Error_Deconstruct(&ContainsResult);
        return NULL;
    }

    WorldObject* Object = NULL;
    Error GetResult = IMap_GetElement(Map, &id, &Object);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return NULL;
    }
    return Object;
}


// Static functions: components.
static bool IsWorldType(const World* self, WorldComponentType type)
{
    return ((size_t)type < self->_componentTypeCount) && (self->_componentTypes[type] != NULL);
}

/* Removes @p pointer from @p buffer (a buffer of pointers), keeping the order of the rest. */
static void RemovePointer(GenericBuffer* buffer, const void* pointer)
{
    for (size_t Index = 0; Index < buffer->_count; Index++)
    {
        const void* Candidate = NULL;
        GenericBuffer_GetAt(buffer, Index, &Candidate);
        if (Candidate == pointer)
        {
            GenericBuffer_RemoveAt(buffer, Index);
            return;
        }
    }
}

/* Whether the object carries a transform driver other than a component of @p exceptType. */
static bool HasOtherDriver(const World* self, const WorldObject* object, WorldComponentType exceptType)
{
    for (size_t Type = 0; Type < self->_componentTypeCount; Type++)
    {
        if ((Type != (size_t)exceptType) && (object->_components[Type] != NULL)
            && self->_componentTypes[Type]->IsTransformDriver)
        {
            return true;
        }
    }
    return false;
}

/* Detaches a component from its object and the world's array, then destroys it. */
static void DestroyComponent(World* self, WorldObject* object, WorldComponentType type)
{
    WorldComponent* Component = object->_components[type];
    const WorldComponentTypeInfo* Info = self->_componentTypes[type];
    if (Info->OnDetach != NULL)
    {
        Info->OnDetach(self, object, Component);
    }
    RemovePointer(&self->_components[type], Component);
    object->_components[type] = NULL;
    self->_componentSetVersion++;

    Info->Deconstruct(Component);
    Memory_Free(Component);
}

/* Detaches every component of the object (its types' OnDetach hooks) and adds their types' bits to @p ioTypes. They
 * stay in the per-type arrays and in the object's slots until DestroyDetachedComponents. */
static void DetachComponents(World* self, WorldObject* object, uint32_t* ioTypes)
{
    for (size_t Type = 0; Type < self->_componentTypeCount; Type++)
    {
        WorldComponent* Component = object->_components[Type];
        if (Component == NULL)
        {
            continue;
        }
        const WorldComponentTypeInfo* Info = self->_componentTypes[Type];
        if (Info->OnDetach != NULL)
        {
            Info->OnDetach(self, object, Component);
        }
        *ioTypes |= (uint32_t)1U << Type;
    }
}

/* Destroys every component of the object, which DetachComponents detached and which no array holds any more. */
static void DestroyDetachedComponents(World* self, WorldObject* object)
{
    for (size_t Type = 0; Type < self->_componentTypeCount; Type++)
    {
        WorldComponent* Component = object->_components[Type];
        if (Component == NULL)
        {
            continue;
        }
        object->_components[Type] = NULL;
        self->_componentTypes[Type]->Deconstruct(Component);
        Memory_Free(Component);
    }
}

/* GenericBuffer_Filter predicates: keep what is not being removed. */
static bool IsObjectKept(GenericBuffer* buffer, GenericBufferElementData element, const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    const WorldObject* Object = NULL;
    Memory_Copy(element._element, &Object, sizeof(Object));
    return !Object->_isBeingRemoved;
}

static bool IsComponentKept(GenericBuffer* buffer, GenericBufferElementData element, const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    const WorldComponent* Component = NULL;
    Memory_Copy(element._element, &Component, sizeof(Component));
    return !Component->_owner->_isBeingRemoved;
}


// Static functions: objects.
/* Constructs an object with @p id and stores it in the list and the id map. The caller checked the id is free. */
static Error StoreNewObject(World* self, const unsigned char* name, uint64_t id, WorldObject** outObject)
{
    WorldObject* Object = Memory_Allocate(sizeof(WorldObject));
    Error ConstructResult = WorldObject_Construct(Object, self, id, name);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        Memory_Free(Object);
        return ConstructResult;
    }

    if (!GenericBuffer_AddLast(&self->_objects, &Object))
    {
        WorldObject_Deconstruct(Object);
        Memory_Free(Object);
        return Error_Construct2(ErrorCode_InvalidOperation, "World: failed to store the object.");
    }
    bool WasAdded = false;
    Error MapResult = IMap_Add(HashMap_AsMap(&self->_objectsById), &id, &Object, &WasAdded);
    if ((MapResult.Code != ErrorCode_Success) || !WasAdded)
    {
        Error_Deconstruct(&MapResult);
        GenericBuffer_RemoveLast(&self->_objects);
        WorldObject_Deconstruct(Object);
        Memory_Free(Object);
        return Error_Construct2(ErrorCode_InvalidOperation, "World: failed to index the object.");
    }

    self->_objectSetVersion++;
    *outObject = Object;
    return Error_CreateSuccess();
}

/* The shared body of the constructors: @p types are the world's component types (entry i of type i). */
static Error Construct(World* self, KeyRegistry* keys, AssetManager* assets, Logger* logger,
    const WorldComponentTypeInfo* const* types, size_t typeCount)
{
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (assets != NULL)
    {
        Error UserResult = AssetManager_GetNewUserID(assets, &User);
        if (UserResult.Code != ErrorCode_Success)
        {
            return UserResult;
        }
    }

    HashMapConstructOptions Options = HashMapConstructOptions_CreateDefault(sizeof(uint64_t), sizeof(WorldObject*),
        HashObjectId);
    Error MapResult = HashMap_Construct1(&self->_objectsById, Options);
    if (MapResult.Code == ErrorCode_Success)
    {
        MapResult = WorldInternal_ConstructData(self);
        if (MapResult.Code != ErrorCode_Success)
        {
            Error DeconstructResult = HashMap_Deconstruct(&self->_objectsById);
            Error_Deconstruct(&DeconstructResult);
        }
    }
    if (MapResult.Code != ErrorCode_Success)
    {
        if (assets != NULL)
        {
            Error RetireResult = AssetManager_RetireUser(assets, User);
            Error_Deconstruct(&RetireResult);
        }
        return MapResult;
    }

    GenericBuffer_AllocateVariable(&self->_objects, INITIAL_OBJECT_CAPACITY, sizeof(WorldObject*));
    Memory_Zero(self->_componentTypes, sizeof(self->_componentTypes));
    Memory_Zero(self->_components, sizeof(self->_components));
    self->_componentTypeCount = typeCount;
    for (size_t Type = 0; Type < typeCount; Type++)
    {
        self->_componentTypes[Type] = types[Type];
        GenericBuffer_AllocateVariable(&self->_components[Type], INITIAL_COMPONENT_CAPACITY, sizeof(WorldComponent*));
    }
    self->_nextId = 1;
    self->_objectSetVersion = 0;
    self->_componentSetVersion = 0;
    self->_dirtyTransformCount = 0U;
    self->_worldMatrixRecomputeCount = 0U;
    self->_refusedParentCount = 0U;
    self->_hasLoggedRefusedParent = false;
    GenericBuffer_AllocateVariable(&self->_removalScratch, 0U, sizeof(WorldObject*));
    self->_name = NULL;
    self->_keys = keys;
    self->_assets = assets;
    self->_logger = logger;
    self->_assetUser = User;
    WorldEnvironment_SetDefaults(&self->Environment);
    return Error_CreateSuccess();
}


// Internal functions (declared in WorldInternal.h).
WorldObject* WorldInternal_FindObject(World* world, uint64_t id)
{
    return FindObject(world, id);
}

void WorldInternal_ComponentAfterSet(void* instance, const PropertyDescriptor* descriptor)
{
    WorldComponent* Component = instance;
    Component->_revision++;

    WorldObject* Owner = Component->_owner;
    if ((Owner == NULL) || (Owner->_world == NULL))
    {
        return;
    }
    World* OwnerWorld = Owner->_world;
    const WorldComponentTypeInfo* Info = World_GetComponentTypeInfo(OwnerWorld, Component->_type);
    if ((Info != NULL) && (Info->OnPropertySet != NULL))
    {
        Info->OnPropertySet(OwnerWorld, Owner, Component, descriptor);
    }
}

Error WorldInternal_ConstructWithTypes(World* self, KeyRegistry* keys, AssetManager* assets, Logger* logger,
    const WorldComponentTypeInfo* const* types, size_t typeCount)
{
    if ((self == NULL) || (keys == NULL) || (types == NULL) || (typeCount > WORLD_MAX_COMPONENT_TYPES))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldInternal_ConstructWithTypes: self, keys and types must not be NULL, and at most "
            "WORLD_MAX_COMPONENT_TYPES types fit.");
    }
    for (size_t Type = 0; Type < typeCount; Type++)
    {
        // The standard types keep their own slots (their downcasts check the slot), the test types come after them.
        bool IsStandardSlot = Type < (size_t)WorldComponentType_Count;
        if ((types[Type] == NULL) || ((size_t)types[Type]->Type != Type)
            || (IsStandardSlot && (types[Type] != WorldComponent_GetStandardTypeInfo((WorldComponentType)Type))))
        {
            return Error_Construct2(ErrorCode_IllegalArgument,
                "WorldInternal_ConstructWithTypes: entry i must be the registry entry of type i, and the standard "
                "types must come first, as their own entries.");
        }
        Error CheckResult = WorldComponentTypeInfo_Check(types[Type]);
        if (CheckResult.Code != ErrorCode_Success)
        {
            return CheckResult;
        }
    }
    return Construct(self, keys, assets, logger, types, typeCount);
}


// Public functions: lifecycle.
Error World_Construct1(World* self, KeyRegistry* keys)
{
    return World_Construct2(self, keys, NULL, NULL);
}

Error World_Construct2(World* self, KeyRegistry* keys, AssetManager* assets, Logger* logger)
{
    if ((self == NULL) || (keys == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_Construct2: self and keys must not be NULL.");
    }

    const WorldComponentTypeInfo* Types[WorldComponentType_Count];
    for (size_t Type = 0; Type < (size_t)WorldComponentType_Count; Type++)
    {
        Types[Type] = WorldComponent_GetStandardTypeInfo((WorldComponentType)Type);
    }
    return Construct(self, keys, assets, logger, Types, (size_t)WorldComponentType_Count);
}

Error World_Deconstruct(World* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_Deconstruct: self must not be NULL.");
    }

    // Objects (and their components) first: a sprite's playback points into an animation the world's user holds.
    // Every detach hook runs while every object is still alive and linked (each root's subtree children first), then
    // everything is destroyed. The component arrays and the links are freed whole, so nothing is kept up to date.
    WorldObject* const* Objects = (WorldObject* const*)self->_objects._data;
    for (size_t Index = 0U; Index < self->_objects._count; Index++)
    {
        if (Objects[Index]->_parent != NULL)
        {
            continue;
        }
        for (WorldObject* Current = WorldInternal_GetFirstChildrenFirst(Objects[Index]); Current != NULL;
            Current = WorldInternal_GetNextChildrenFirst(Current, Objects[Index]))
        {
            uint32_t Types = 0U;
            DetachComponents(self, Current, &Types);
        }
    }
    for (size_t Index = self->_objects._count; Index > 0; Index--)
    {
        WorldObject* Object = Objects[Index - 1];
        DestroyDetachedComponents(self, Object);
        WorldObject_Deconstruct(Object);
        Memory_Free(Object);
    }

    Memory_Free(self->_objects._data);
    Memory_Free(self->_removalScratch._data);
    for (size_t Type = 0; Type < self->_componentTypeCount; Type++)
    {
        Memory_Free(self->_components[Type]._data);
    }
    WorldInternal_DeconstructData(self);
    Error MapResult = HashMap_Deconstruct(&self->_objectsById);
    Error_Deconstruct(&MapResult);
    Memory_Free(self->_name);
    self->_name = NULL;
    WorldEnvironment_Deconstruct(&self->Environment);

    if (self->_assets != NULL)
    {
        return AssetManager_RetireUser(self->_assets, self->_assetUser);
    }
    return Error_CreateSuccess();
}


// Public functions: objects.
Error World_CreateObject(World* self, const unsigned char* name, WorldObject** outObject)
{
    if ((self == NULL) || (outObject == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_CreateObject: self and outObject must not be NULL.");
    }
    *outObject = NULL;
    uint64_t Id = self->_nextId;
    if ((Id == 0) || (FindObject(self, Id) != NULL))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_CreateObject: no free object id is left.");
    }

    Error Result = StoreNewObject(self, name, Id, outObject);
    if (Result.Code == ErrorCode_Success)
    {
        self->_nextId = (Id == WORLD_OBJECT_ID_MAX) ? 0U : (Id + 1U); // 0: every id is used
    }
    return Result;
}

Error World_CreateObjectWithId(World* self, const unsigned char* name, uint64_t id, WorldObject** outObject)
{
    if ((self == NULL) || (outObject == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_CreateObjectWithId: self and outObject must not be NULL.");
    }
    *outObject = NULL;
    if (id == 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_CreateObjectWithId: id must not be 0.");
    }
    if (id > WORLD_OBJECT_ID_MAX)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "World_CreateObjectWithId: id must be at most %" PRIu64 " (WORLD_OBJECT_ID_MAX).", WORLD_OBJECT_ID_MAX);
    }
    if (FindObject(self, id) != NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_CreateObjectWithId: id is already in use.");
    }

    Error Result = StoreNewObject(self, name, id, outObject);
    if ((Result.Code == ErrorCode_Success) && (self->_nextId != 0) && (id >= self->_nextId))
    {
        self->_nextId = (id == WORLD_OBJECT_ID_MAX) ? 0U : (id + 1U); // 0: every id is used
    }
    return Result;
}

Error World_RemoveObject(World* self, uint64_t id)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_RemoveObject: self must not be NULL.");
    }
    if (id == 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_RemoveObject: id must not be 0.");
    }
    WorldObject* Object = FindObject(self, id);
    if (Object == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_RemoveObject: no object with that id.");
    }

    // The whole subtree, children first, is listed before anything changes.
    GenericBuffer* Removed = &self->_removalScratch;
    GenericBuffer_Clear(Removed);
    if (!WorldInternal_CollectSubtree(Object, Removed))
    {
        GenericBuffer_Clear(Removed);
        return Error_Construct2(ErrorCode_InvalidOperation, "World_RemoveObject: the subtree could not be listed.");
    }

    // Children first, every component is detached while the whole hierarchy is still in place (its hooks see the
    // object where it is). Then the subtree is unlinked, and the component arrays and the object list drop it in one
    // pass each (keeping the others' order).
    WorldObject* const* Objects = (WorldObject* const*)Removed->_data;
    size_t RemovedCount = Removed->_count;
    uint32_t Types = 0U;
    for (size_t Index = 0U; Index < RemovedCount; Index++)
    {
        DetachComponents(self, Objects[Index], &Types);
    }
    WorldInternal_DetachFromParent(Object);
    for (size_t Index = 0U; Index < RemovedCount; Index++)
    {
        Objects[Index]->_isBeingRemoved = true;
    }
    for (size_t Type = 0U; Type < self->_componentTypeCount; Type++)
    {
        if ((Types & ((uint32_t)1U << Type)) != 0U)
        {
            GenericBuffer_Filter(&self->_components[Type], IsComponentKept, NULL);
        }
    }
    if (Types != 0U)
    {
        self->_componentSetVersion++;
    }
    GenericBuffer_Filter(&self->_objects, IsObjectKept, NULL);

    for (size_t Index = 0U; Index < RemovedCount; Index++)
    {
        WorldObject* Doomed = Objects[Index];
        DestroyDetachedComponents(self, Doomed);
        WorldInternal_ForgetObjectData(self, Doomed);
        uint64_t DoomedId = Doomed->_id;
        bool WasRemoved = false;
        Error MapResult = IMap_Remove(HashMap_AsMap(&self->_objectsById), &DoomedId, &WasRemoved);
        Error_Deconstruct(&MapResult);
        if (Doomed->_isWorldMatrixDirty)
        {
            self->_dirtyTransformCount--;
        }
        WorldObject_Deconstruct(Doomed);
        Memory_Free(Doomed);
    }
    GenericBuffer_Clear(Removed);
    self->_objectSetVersion++;
    return Error_CreateSuccess();
}

bool World_TryGetObject(World* self, uint64_t id, WorldObject** outObject)
{
    if ((self == NULL) || (outObject == NULL))
    {
        return false;
    }
    *outObject = (id != 0) ? FindObject(self, id) : NULL;
    return *outObject != NULL;
}

size_t World_GetObjectCount(const World* self)
{
    if (self == NULL)
    {
        return 0;
    }
    return self->_objects._count;
}

Error World_GetObjectByIndex(const World* self, size_t index, WorldObject** outObject)
{
    if ((self == NULL) || (outObject == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_GetObjectByIndex: self and outObject must not be NULL.");
    }
    if (index >= self->_objects._count)
    {
        return Error_Construct2(ErrorCode_IndexOutOfBounds, "World_GetObjectByIndex: index out of range.");
    }

    *outObject = ((WorldObject* const*)self->_objects._data)[index];
    return Error_CreateSuccess();
}


// Public functions: components.
Error World_AddComponent(World* self, uint64_t id, WorldComponentType type, WorldComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_AddComponent: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;
    if ((id == 0) || !IsWorldType(self, type))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_AddComponent: id must not be 0 and type must be one of the world's component types.");
    }
    WorldObject* Object = FindObject(self, id);
    if (Object == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_AddComponent: no object with that id.");
    }
    const WorldComponentTypeInfo* Info = self->_componentTypes[type];
    if (Object->_components[type] != NULL)
    {
        return Error_Construct4(ErrorCode_InvalidOperation,
            "World_AddComponent: the object already has a %s component.", (const char*)Info->Name);
    }
    if (Info->IsTransformDriver && HasOtherDriver(self, Object, type))
    {
        return Error_Construct4(ErrorCode_InvalidOperation,
            "World_AddComponent: the object already has a component that drives its transform; a %s component cannot "
            "drive it too.", (const char*)Info->Name);
    }

    WorldComponent* Component = Memory_Allocate(Info->Size);
    Error ConstructResult = Info->Construct(Component);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        Memory_Free(Component);
        return ConstructResult;
    }
    // Construct zeroed the base with the rest; the world sets it.
    Component->_type = type;
    Component->_owner = Object;
    Component->_revision = 0;
    if (!GenericBuffer_AddLast(&self->_components[type], &Component))
    {
        Info->Deconstruct(Component);
        Memory_Free(Component);
        return Error_Construct2(ErrorCode_InvalidOperation, "World_AddComponent: failed to store the component.");
    }
    Object->_components[type] = Component;
    self->_componentSetVersion++;

    if (Info->IsTransformDriver && (Object->_mobility != WorldMobility_Movable))
    {
        Error MobilityResult = WorldObject_SetMobility(Object, WorldMobility_Movable);
        Error_Deconstruct(&MobilityResult); // cannot fail: the object exists and the value is valid
    }
    if (Info->OnAttach != NULL)
    {
        Info->OnAttach(self, Object, Component);
    }
    *outComponent = Component;
    return Error_CreateSuccess();
}

Error World_RemoveComponent(World* self, uint64_t id, WorldComponentType type)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_RemoveComponent: self must not be NULL.");
    }
    if ((id == 0) || !IsWorldType(self, type))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World_RemoveComponent: id must not be 0 and type must be one of the world's component types.");
    }
    WorldObject* Object = FindObject(self, id);
    if (Object == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_RemoveComponent: no object with that id.");
    }
    if (Object->_components[type] == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "World_RemoveComponent: the object has no %s component.",
            (const char*)self->_componentTypes[type]->Name);
    }

    DestroyComponent(self, Object, type);
    return Error_CreateSuccess();
}

bool World_TryGetComponent(World* self, uint64_t id, WorldComponentType type, WorldComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return false;
    }
    *outComponent = NULL;
    WorldObject* Object = NULL;
    if (!IsWorldType(self, type) || !World_TryGetObject(self, id, &Object))
    {
        return false;
    }
    *outComponent = Object->_components[type];
    return *outComponent != NULL;
}

WorldComponent* const* World_GetComponents(const World* self, WorldComponentType type, size_t* outCount)
{
    if (outCount != NULL)
    {
        *outCount = 0;
    }
    if ((self == NULL) || (outCount == NULL) || !IsWorldType(self, type) || (self->_components[type]._count == 0))
    {
        return NULL;
    }
    *outCount = self->_components[type]._count;
    return (WorldComponent* const*)self->_components[type]._data;
}

const WorldComponentTypeInfo* World_GetComponentTypeInfo(const World* self, WorldComponentType type)
{
    if ((self == NULL) || !IsWorldType(self, type))
    {
        return NULL;
    }
    return self->_componentTypes[type];
}

bool World_TryFindComponentType(const World* self, const unsigned char* name, WorldComponentType* outType)
{
    if ((self == NULL) || (name == NULL) || (outType == NULL))
    {
        return false;
    }
    for (size_t Type = 0; Type < self->_componentTypeCount; Type++)
    {
        if (WorldInternal_AreStringsEqual(self->_componentTypes[Type]->Name, name))
        {
            *outType = (WorldComponentType)Type;
            return true;
        }
    }
    return false;
}


// Public functions: world properties.
void World_EnsureNextObjectIdAtLeast(World* self, uint64_t minimumNext)
{
    if ((self == NULL) || (self->_nextId == 0))
    {
        return;
    }
    if (minimumNext > WORLD_OBJECT_ID_MAX)
    {
        self->_nextId = 0U; // No id is that large: every id is used.
        return;
    }
    if (minimumNext > self->_nextId)
    {
        self->_nextId = minimumNext;
    }
}

Error World_SetName(World* self, const unsigned char* name)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_SetName: self must not be NULL.");
    }
    if ((name != NULL) && ((StringUTF8_GetByteLength(name) > WORLD_NAME_MAX_LENGTH) || !StringUTF8_IsEncodingValid(name)))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "A world name must be valid UTF-8 of at most %zu bytes.", WORLD_NAME_MAX_LENGTH);
    }

    unsigned char* Clone = WorldInternal_CloneString(name);
    Memory_Free(self->_name);
    self->_name = Clone;
    return Error_CreateSuccess();
}
