#include "World.h"
#include "WorldShared.h"
#include "wr/WRMemory.h"


// Macros.
/** Initial capacity (in objects) of a world's object buffer. */
#define INITIAL_OBJECT_CAPACITY 32


// Static functions.
/* Returns the storage index of the object with the given id, or SIZE_MAX if not present. */
static size_t FindObjectIndexById(World* self, uint64_t id)
{
    for (size_t Index = 0; Index < self->_objects._count; Index++)
    {
        WorldObject* Object = NULL;
        GenericBuffer_GetAt(&self->_objects, Index, &Object);
        if (WorldObject_GetId(Object) == id)
        {
            return Index;
        }
    }
    return SIZE_MAX;
}


// Public functions.
Error World_Construct(World* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_Construct: self must not be NULL.");
    }

    GenericBuffer_AllocateVariable(&self->_objects, INITIAL_OBJECT_CAPACITY, sizeof(WorldObject*));
    self->_nextId = 1;
    self->_name = NULL;
    WorldEnvironment_SetDefaults(&self->Environment);

    return Error_CreateSuccess();
}

Error World_Deconstruct(World* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_Deconstruct: self must not be NULL.");
    }

    for (size_t Index = 0; Index < self->_objects._count; Index++)
    {
        WorldObject* Object = NULL;
        GenericBuffer_GetAt(&self->_objects, Index, &Object);
        WorldObject_Destroy(Object);
    }

    Memory_Free(self->_objects._data);
    Memory_Free(self->_name);
    WorldEnvironment_Deconstruct(&self->Environment);

    return Error_CreateSuccess();
}

Error World_AddObject(World* self, WorldObject* object)
{
    if ((self == NULL) || (object == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_AddObject: self and object must not be NULL.");
    }
    if (WorldObject_GetId(object) != 0)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_AddObject: object already has an id.");
    }

    uint64_t NewId = self->_nextId;
    if (!GenericBuffer_AddLast(&self->_objects, &object))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_AddObject: failed to store the object.");
    }

    // Cannot fail: object is non-NULL and NewId is non-zero.
    WorldObject_SetId(object, NewId);
    self->_nextId++;
    return Error_CreateSuccess();
}

Error World_AddObjectWithId(World* self, WorldObject* object, uint64_t id)
{
    if ((self == NULL) || (object == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_AddObjectWithId: self and object must not be NULL.");
    }
    if (id == 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_AddObjectWithId: id must not be 0.");
    }
    if (WorldObject_GetId(object) != 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_AddObjectWithId: object already has an id.");
    }
    if (FindObjectIndexById(self, id) != SIZE_MAX)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_AddObjectWithId: id is already in use.");
    }

    if (!GenericBuffer_AddLast(&self->_objects, &object))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_AddObjectWithId: failed to store the object.");
    }

    WorldObject_SetId(object, id);
    if ((id != UINT64_MAX) && (id >= self->_nextId))
    {
        self->_nextId = id + 1;
    }
    return Error_CreateSuccess();
}

Error World_RemoveObjectById(World* self, uint64_t id)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_RemoveObjectById: self must not be NULL.");
    }
    if (id == 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_RemoveObjectById: id must not be 0.");
    }

    size_t Index = FindObjectIndexById(self, id);
    if (Index == SIZE_MAX)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "World_RemoveObjectById: no object with that id.");
    }

    WorldObject* Object = NULL;
    GenericBuffer_GetAt(&self->_objects, Index, &Object);
    WorldObject_Destroy(Object);
    GenericBuffer_RemoveAt(&self->_objects, Index);
    return Error_CreateSuccess();
}

bool World_TryGetObjectById(World* self, uint64_t id, WorldObject** outObject)
{
    if ((self == NULL) || (outObject == NULL))
    {
        return false;
    }

    size_t Index = FindObjectIndexById(self, id);
    if (Index == SIZE_MAX)
    {
        *outObject = NULL;
        return false;
    }

    GenericBuffer_GetAt(&self->_objects, Index, outObject);
    return true;
}

size_t World_GetObjectCount(World* self)
{
    if (self == NULL)
    {
        return 0;
    }
    return self->_objects._count;
}

Error World_GetObjectByIndex(World* self, size_t index, WorldObject** outObject)
{
    if ((self == NULL) || (outObject == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_GetObjectByIndex: self and outObject must not be NULL.");
    }
    if (index >= self->_objects._count)
    {
        return Error_Construct2(ErrorCode_IndexOutOfBounds, "World_GetObjectByIndex: index out of range.");
    }

    GenericBuffer_GetAt(&self->_objects, index, outObject);
    return Error_CreateSuccess();
}

void World_EnsureNextObjectIdAtLeast(World* self, uint64_t minimumNext)
{
    if (self == NULL)
    {
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

    unsigned char* Clone;
    WorldShared_CloneString(name, &Clone);
    Memory_Free(self->_name);
    self->_name = Clone;
    return Error_CreateSuccess();
}
