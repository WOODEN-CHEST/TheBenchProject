#include "WorldModelObject.h"
#include "WorldShared.h"
#include "wr/WRMemory.h"


// Static functions.
static void WorldModelObject_Destroy(void* self)
{
    WorldModelObject* ModelSelf = self;
    Memory_Free(ModelSelf->_modelAssetName);
    WorldObject_Deconstruct(&ModelSelf->Base);
    Memory_Free(ModelSelf);
}


// Fields.
static const WorldObjectVTable ModelObjectVTable =
{
    .Destroy = WorldModelObject_Destroy
};


// Public functions.
Error WorldModelObject_Create(const unsigned char* name, const unsigned char* modelAssetName,
    WorldModelObject** outObject)
{
    if (outObject == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldModelObject_Create: outObject must not be NULL.");
    }
    *outObject = NULL;

    WorldModelObject* Object = Memory_Allocate(sizeof(WorldModelObject));

    Error BaseResult = WorldObject_Construct(&Object->Base, &ModelObjectVTable, WorldObjectType_Model, name);
    if (BaseResult.Code != ErrorCode_Success)
    {
        Memory_Free(Object);
        return BaseResult;
    }

    WorldShared_CloneString(modelAssetName, &Object->_modelAssetName);
    Object->HasOutline = true;
    Object->OmitPixelation = false;
    Object->ShadowTier = WorldShadowTier_Both;

    *outObject = Object;
    return Error_CreateSuccess();
}

Error WorldModelObject_SetModelAssetName(WorldModelObject* self, const unsigned char* modelAssetName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldModelObject_SetModelAssetName: self must not be NULL.");
    }

    unsigned char* Clone;
    WorldShared_CloneString(modelAssetName, &Clone);
    Memory_Free(self->_modelAssetName);
    self->_modelAssetName = Clone;
    return Error_CreateSuccess();
}
