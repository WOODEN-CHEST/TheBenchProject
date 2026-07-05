#include "WorldObject.h"
#include "WorldShared.h"
#include "wr/WRMemory.h"


// Public functions.
Error WorldObject_Construct(WorldObject* self, const WorldObjectVTable* vtable, WorldObjectType type,
    const unsigned char* name)
{
    if ((self == NULL) || (vtable == NULL) || (vtable->Destroy == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldObject_Construct: self, vtable and vtable->Destroy must not be NULL.");
    }

    self->VTable = vtable;
    self->Type = type;
    self->_id = 0;
    WorldShared_CloneString(name, &self->_name);
    self->_position = (Vector3){ .x = 0.0f, .y = 0.0f, .z = 0.0f };
    self->_rotation = (Vector3){ .x = 0.0f, .y = 0.0f, .z = 0.0f };
    self->_scale = (Vector3){ .x = 1.0f, .y = 1.0f, .z = 1.0f };
    self->_tint = RenderColor_White();

    return Error_CreateSuccess();
}

void WorldObject_Deconstruct(WorldObject* self)
{
    if (self == NULL)
    {
        return;
    }

    Memory_Free(self->_name);
    self->_name = NULL;
}

Error WorldObject_SetId(WorldObject* self, uint64_t id)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetId: self must not be NULL.");
    }
    if (id == 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetId: id must not be 0.");
    }

    self->_id = id;
    return Error_CreateSuccess();
}

Error WorldObject_SetName(WorldObject* self, const unsigned char* name)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetName: self must not be NULL.");
    }

    unsigned char* Clone;
    WorldShared_CloneString(name, &Clone);
    Memory_Free(self->_name);
    self->_name = Clone;
    return Error_CreateSuccess();
}

Error WorldObject_SetPosition(WorldObject* self, Vector3 position)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetPosition: self must not be NULL.");
    }
    if (!WorldShared_IsVector3Finite(position))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldObject_SetPosition: position components must be finite.");
    }

    self->_position = position;
    return Error_CreateSuccess();
}

Error WorldObject_SetRotation(WorldObject* self, Vector3 rotation)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetRotation: self must not be NULL.");
    }
    if (!WorldShared_IsVector3Finite(rotation))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldObject_SetRotation: rotation components must be finite.");
    }

    self->_rotation = rotation;
    return Error_CreateSuccess();
}

Error WorldObject_SetScale(WorldObject* self, Vector3 scale)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetScale: self must not be NULL.");
    }
    if (!WorldShared_IsVector3Finite(scale))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldObject_SetScale: scale components must be finite.");
    }

    self->_scale = scale;
    return Error_CreateSuccess();
}

Error WorldObject_SetTint(WorldObject* self, RenderColor tint)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldObject_SetTint: self must not be NULL.");
    }
    if (!WorldShared_IsFloatFinite(tint.Brightness) || !WorldShared_IsFloatFinite(tint.Opacity))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "WorldObject_SetTint: tint brightness and opacity must be finite.");
    }

    self->_tint = tint;
    return Error_CreateSuccess();
}
