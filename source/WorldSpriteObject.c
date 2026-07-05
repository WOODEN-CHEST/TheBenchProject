#include "WorldSpriteObject.h"
#include "WorldShared.h"
#include "wr/WRMemory.h"


// Static functions.
static void WorldSpriteObject_Destroy(void* self)
{
    WorldSpriteObject* SpriteSelf = self;
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

    WorldShared_CloneString(spriteAnimationAssetName, &Object->_spriteAnimationAssetName);
    Object->HasOutline = true;
    Object->OmitPixelation = false;

    *outObject = Object;
    return Error_CreateSuccess();
}

Error WorldSpriteObject_SetSpriteAnimationAssetName(WorldSpriteObject* self,
    const unsigned char* spriteAnimationAssetName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldSpriteObject_SetSpriteAnimationAssetName: self must not be NULL.");
    }

    unsigned char* Clone;
    WorldShared_CloneString(spriteAnimationAssetName, &Clone);
    Memory_Free(self->_spriteAnimationAssetName);
    self->_spriteAnimationAssetName = Clone;
    return Error_CreateSuccess();
}
