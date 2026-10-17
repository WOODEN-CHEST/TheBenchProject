#include "WorldSpriteObject.h"
#include "WorldShared.h"
#include "wr/WRMemory.h"


// Static functions.
static void WorldSpriteObject_Destroy(void* self)
{
    WorldSpriteObject* SpriteSelf = self;
    Error InstanceResult = SpriteAnimationInstance_Deconstruct(&SpriteSelf->_animationInstance);
    Error_Deconstruct(&InstanceResult); // best-effort; tears down the instance's embedded event
    Memory_Free(SpriteSelf->_spriteAnimationAssetName);
    WorldObject_Deconstruct(&SpriteSelf->Base);
    Memory_Free(SpriteSelf);
}


// Fields.
static const WorldObjectVTable SpriteObjectVTable =
{
    .Destroy = WorldSpriteObject_Destroy
};


// Public functions.
Error WorldSpriteObject_Create(const unsigned char* name, const unsigned char* spriteAnimationAssetName,
    WorldSpriteObject** outObject)
{
    if (outObject == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteObject_Create: outObject must not be NULL.");
    }
    *outObject = NULL;

    WorldSpriteObject* Object = Memory_Allocate(sizeof(WorldSpriteObject));

    Error BaseResult = WorldObject_Construct(&Object->Base, &SpriteObjectVTable, WorldObjectType_Sprite, name);
    if (BaseResult.Code != ErrorCode_Success)
    {
        Memory_Free(Object);
        return BaseResult;
    }

    // Start with an empty (no-source) animation instance; the backing animation is set later.
    Error InstanceResult = SpriteAnimationInstance_Construct1(&Object->_animationInstance);
    if (InstanceResult.Code != ErrorCode_Success)
    {
        WorldObject_Deconstruct(&Object->Base);
        Memory_Free(Object);
        return InstanceResult;
    }

    WorldShared_CloneString(spriteAnimationAssetName, &Object->_spriteAnimationAssetName);
    Object->HasOutline = true;
    Object->OmitPixelation = false;

    *outObject = Object;
    return Error_CreateSuccess();
}

Error WorldSpriteObject_SetAnimation(WorldSpriteObject* self, SpriteAnimation* animation)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteObject_SetAnimation: self must not be NULL.");
    }
    if (WorldObject_GetId(&self->Base) != 0)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "WorldSpriteObject_SetAnimation: the sprite belongs to a world, which binds its animation.");
    }
    return WorldSpriteObject_BindAnimation(self, animation);
}

Error WorldSpriteObject_SetSpriteAnimationAssetName(WorldSpriteObject* self,
    const unsigned char* spriteAnimationAssetName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldSpriteObject_SetSpriteAnimationAssetName: self must not be NULL.");
    }
    if (WorldObject_GetId(&self->Base) != 0)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "WorldSpriteObject_SetSpriteAnimationAssetName: the sprite belongs to a world; use "
            "World_SetSpriteAnimationAssetName.");
    }

    WorldSpriteObject_ReplaceAnimationAssetName(self, spriteAnimationAssetName);
    return Error_CreateSuccess();
}


// Internal functions (declared in WorldShared.h).
Error WorldSpriteObject_BindAnimation(WorldSpriteObject* self, SpriteAnimation* animation)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteObject_BindAnimation: self must not be NULL.");
    }

    // Tear down the current instance (and its event), then re-initialize into the same storage: backed by
    // @p animation (reset to its defaults) or empty when NULL.
    Error DeconstructResult = SpriteAnimationInstance_Deconstruct(&self->_animationInstance);
    if (DeconstructResult.Code != ErrorCode_Success)
    {
        return DeconstructResult;
    }
    if (animation == NULL)
    {
        return SpriteAnimationInstance_Construct1(&self->_animationInstance);
    }

    Error CreateResult = SpriteAnimation_CreateInstance(animation, &self->_animationInstance);
    if (CreateResult.Code != ErrorCode_Success)
    {
        // CreateInstance leaves the storage deconstructed on failure; keep a valid (empty) instance so later
        // calls and Destroy never work on a torn-down one.
        Error EmptyResult = SpriteAnimationInstance_Construct1(&self->_animationInstance);
        Error_Deconstruct(&EmptyResult);
    }
    return CreateResult;
}

void WorldSpriteObject_ReplaceAnimationAssetName(WorldSpriteObject* self, const unsigned char* name)
{
    unsigned char* Clone;
    WorldShared_CloneString(name, &Clone);
    Memory_Free(self->_spriteAnimationAssetName);
    self->_spriteAnimationAssetName = Clone;
}
