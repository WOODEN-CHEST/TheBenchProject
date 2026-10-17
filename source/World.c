#include <inttypes.h>
#include "World.h"
#include "WorldShared.h"
#include "WorldSpriteObject.h"
#include "Logger.h"
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

/* Logs (once, now) that a sprite's animation could not be bound, and releases the error. */
static void ReportSpriteBindFailure(World* self, WorldSpriteObject* sprite, const unsigned char* assetName,
    Error* error)
{
    if (self->_logger != NULL)
    {
        const unsigned char* ObjectName = WorldObject_GetName(WorldSpriteObject_AsObject(sprite));
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"World: sprite object %" PRIu64 u8" (\"%s\") has no animation: \"%s\" could not "
            u8"be loaded (%s).",
            WorldObject_GetId(WorldSpriteObject_AsObject(sprite)),
            (ObjectName != NULL) ? (const char*)ObjectName : "",
            (const char*)assetName,
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Backs a sprite's playback with the animation its asset name refers to, loaded under the world's asset user, or
 * empties it when the world has no asset manager, the sprite has no name, or the load fails. A failed load is
 * a recoverable content problem: it is logged once, here, and the sprite stays empty (it draws nothing). */
static void BindSpriteAnimation(World* self, WorldSpriteObject* sprite)
{
    SpriteAnimation* Animation = NULL;
    const unsigned char* AssetName = WorldSpriteObject_GetSpriteAnimationAssetName(sprite);
    if ((self->_assets != NULL) && (AssetName != NULL))
    {
        Error LoadResult = AssetManager_LoadSpriteAnimation(self->_assets, AssetName, self->_assetUser, &Animation);
        if (LoadResult.Code != ErrorCode_Success)
        {
            ReportSpriteBindFailure(self, sprite, AssetName, &LoadResult);
            Animation = NULL;
        }
    }

    Error BindResult = WorldSpriteObject_BindAnimation(sprite, Animation);
    if (BindResult.Code != ErrorCode_Success)
    {
        const unsigned char* ReportedName = (AssetName != NULL) ? AssetName : (const unsigned char*)u8"";
        ReportSpriteBindFailure(self, sprite, ReportedName, &BindResult);
    }
}

/* Work every successful insertion shares: the object-set version changes and a sprite gets its animation. */
static void OnObjectAdded(World* self, WorldObject* object)
{
    self->_objectSetVersion++;
    if (WorldObject_GetType(object) == WorldObjectType_Sprite)
    {
        BindSpriteAnimation(self, (WorldSpriteObject*)object);
    }
}


// Public functions.
Error World_Construct1(World* self)
{
    return World_Construct2(self, NULL, NULL);
}

Error World_Construct2(World* self, AssetManager* assets, Logger* logger)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_Construct2: self must not be NULL.");
    }

    AssetUserID User = ASSET_USER_ID_INVALID;
    if (assets != NULL)
    {
        Error UserResult = AssetManager_GetNewUserID(assets, &User);
        if (UserResult.Code != ErrorCode_Success)
        {
            return UserResult;
        }
    }

    GenericBuffer_AllocateVariable(&self->_objects, INITIAL_OBJECT_CAPACITY, sizeof(WorldObject*));
    self->_nextId = 1;
    self->_objectSetVersion = 0;
    self->_name = NULL;
    self->_assets = assets;
    self->_logger = logger;
    self->_assetUser = User;
    WorldEnvironment_SetDefaults(&self->Environment);

    return Error_CreateSuccess();
}

Error World_Deconstruct(World* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_Deconstruct: self must not be NULL.");
    }

    // Objects first: a sprite's playback instance points into an animation held by the world's asset user.
    for (size_t Index = 0; Index < self->_objects._count; Index++)
    {
        WorldObject* Object = NULL;
        GenericBuffer_GetAt(&self->_objects, Index, &Object);
        WorldObject_Destroy(Object);
    }

    Memory_Free(self->_objects._data);
    Memory_Free(self->_name);
    WorldEnvironment_Deconstruct(&self->Environment);

    if (self->_assets != NULL)
    {
        return AssetManager_RetireUser(self->_assets, self->_assetUser);
    }
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
    OnObjectAdded(self, object);
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
    OnObjectAdded(self, object);
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
    self->_objectSetVersion++;
    return Error_CreateSuccess();
}

Error World_SetSpriteAnimationAssetName(World* self, uint64_t id, const unsigned char* assetName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_SetSpriteAnimationAssetName: self must not be NULL.");
    }
    if (id == 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World_SetSpriteAnimationAssetName: id must not be 0.");
    }

    WorldObject* Object = NULL;
    if (!World_TryGetObjectById(self, id, &Object))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "World_SetSpriteAnimationAssetName: no object with that id.");
    }
    if (WorldObject_GetType(Object) != WorldObjectType_Sprite)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "World_SetSpriteAnimationAssetName: the object is not a sprite object.");
    }

    // TODO: The previous animation stays held under the world's user until the world is destroyed (holds are
    // not counted, so releasing it here could unload it under another sprite). If long-lived edited worlds
    // pile up unused animations, add a release pass that swaps to a fresh user holding only bound animations.
    WorldSpriteObject* Sprite = (WorldSpriteObject*)Object;
    WorldSpriteObject_ReplaceAnimationAssetName(Sprite, assetName);
    BindSpriteAnimation(self, Sprite);
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
