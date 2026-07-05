#include "WorldLight.h"
#include "WorldShared.h"
#include "wr/WRMemory.h"


// Static functions.
static void WorldLight_Destroy(void* self)
{
    WorldLight* LightSelf = self;
    WorldObject_Deconstruct(&LightSelf->Base);
    Memory_Free(LightSelf);
}


// Fields.
static const WorldObjectVTable LightVTable =
{
    .Destroy = WorldLight_Destroy
};


// Public functions.
Error WorldLight_Create(const unsigned char* name, WorldLight** outLight)
{
    if (outLight == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldLight_Create: outLight must not be NULL.");
    }
    *outLight = NULL;

    WorldLight* Light = Memory_Allocate(sizeof(WorldLight));

    Error BaseResult = WorldObject_Construct(&Light->Base, &LightVTable, WorldObjectType_Light, name);
    if (BaseResult.Code != ErrorCode_Success)
    {
        Memory_Free(Light);
        return BaseResult;
    }

    Light->Color = WHITE;
    Light->_intensity = WORLD_LIGHT_INTENSITY_DEFAULT;
    Light->_size = WORLD_LIGHT_SIZE_DEFAULT;
    Light->CastsShadows = false;

    *outLight = Light;
    return Error_CreateSuccess();
}

Error WorldLight_SetIntensity(WorldLight* self, float intensity)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldLight_SetIntensity: self must not be NULL.");
    }
    if (!WorldShared_IsFloatFinite(intensity) || (intensity < WORLD_LIGHT_INTENSITY_MIN))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldLight_SetIntensity: intensity must be finite and non-negative.");
    }

    self->_intensity = intensity;
    return Error_CreateSuccess();
}

Error WorldLight_SetSize(WorldLight* self, float size)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldLight_SetSize: self must not be NULL.");
    }
    if (!WorldShared_IsFloatFinite(size) || (size < WORLD_LIGHT_SIZE_MIN))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldLight_SetSize: size must be finite and non-negative.");
    }

    self->_size = size;
    return Error_CreateSuccess();
}
