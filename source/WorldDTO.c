#include "WorldDTO.h"
#include "WorldShared.h"
#include "World.h"
#include "WorldModelObject.h"
#include "WorldSpriteObject.h"
#include "WorldLight.h"
#include "wr/WRMemory.h"


// Macros.
/** Initial capacity (in object records) of a DTO's object buffer. */
#define INITIAL_OBJECT_CAPACITY 32


// Static functions.
/* Frees the owned strings of a single object DTO record (based on its type). */
static void FreeObjectDTO(WorldObjectDTO* record)
{
    Memory_Free(record->Name);
    record->Name = NULL;

    switch (record->Type)
    {
        case WorldObjectType_Model:
            Memory_Free(record->Data.Model.ModelAssetName);
            record->Data.Model.ModelAssetName = NULL;
            break;

        case WorldObjectType_Sprite:
            Memory_Free(record->Data.Sprite.SpriteAnimationAssetName);
            record->Data.Sprite.SpriteAnimationAssetName = NULL;
            break;

        case WorldObjectType_Light:
            break;
    }
}

/* Fills a DTO record (with cloned strings) from a live world object. */
static void FillObjectDTO(WorldObject* object, WorldObjectDTO* outRecord)
{
    outRecord->Type = WorldObject_GetType(object);
    outRecord->Id = WorldObject_GetId(object);
    WorldShared_CloneString(WorldObject_GetName(object), &outRecord->Name);
    outRecord->Position = WorldObject_GetPosition(object);
    outRecord->Rotation = WorldObject_GetRotation(object);
    outRecord->Scale = WorldObject_GetScale(object);
    outRecord->Tint = WorldObject_GetTint(object);

    switch (outRecord->Type)
    {
        case WorldObjectType_Model:
        {
            WorldModelObject* Model = (WorldModelObject*)object;
            WorldShared_CloneString(WorldModelObject_GetModelAssetName(Model),
                &outRecord->Data.Model.ModelAssetName);
            outRecord->Data.Model.HasOutline = Model->HasOutline;
            outRecord->Data.Model.OmitPixelation = Model->OmitPixelation;
            break;
        }

        case WorldObjectType_Sprite:
        {
            WorldSpriteObject* Sprite = (WorldSpriteObject*)object;
            WorldShared_CloneString(WorldSpriteObject_GetSpriteAnimationAssetName(Sprite),
                &outRecord->Data.Sprite.SpriteAnimationAssetName);
            outRecord->Data.Sprite.HasOutline = Sprite->HasOutline;
            outRecord->Data.Sprite.OmitPixelation = Sprite->OmitPixelation;
            break;
        }

        case WorldObjectType_Light:
        {
            WorldLight* Light = (WorldLight*)object;
            outRecord->Data.Light.Color = Light->Color;
            outRecord->Data.Light.Intensity = WorldLight_GetIntensity(Light);
            outRecord->Data.Light.Size = WorldLight_GetSize(Light);
            outRecord->Data.Light.CastsShadows = Light->CastsShadows;
            break;
        }
    }
}

/* Applies the DTO's base transform and tint onto a freshly created object, validating each value. */
static Error ApplyBaseFields(WorldObject* object, const WorldObjectDTO* record)
{
    Error Result = WorldObject_SetPosition(object, record->Position);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = WorldObject_SetRotation(object, record->Rotation);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = WorldObject_SetScale(object, record->Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return WorldObject_SetTint(object, record->Tint);
}

/* Creates a live object from a DTO record. On success *outObject is a new, unowned object (id 0). */
static Error BuildObjectFromDTO(const WorldObjectDTO* record, WorldObject** outObject)
{
    *outObject = NULL;
    WorldObject* Base = NULL;

    switch (record->Type)
    {
        case WorldObjectType_Model:
        {
            WorldModelObject* Model = NULL;
            Error Result = WorldModelObject_Create(record->Name, record->Data.Model.ModelAssetName, &Model);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            Model->HasOutline = record->Data.Model.HasOutline;
            Model->OmitPixelation = record->Data.Model.OmitPixelation;
            Base = WorldModelObject_AsObject(Model);
            break;
        }

        case WorldObjectType_Sprite:
        {
            WorldSpriteObject* Sprite = NULL;
            Error Result = WorldSpriteObject_Create(record->Name,
                record->Data.Sprite.SpriteAnimationAssetName, &Sprite);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            Sprite->HasOutline = record->Data.Sprite.HasOutline;
            Sprite->OmitPixelation = record->Data.Sprite.OmitPixelation;
            Base = WorldSpriteObject_AsObject(Sprite);
            break;
        }

        case WorldObjectType_Light:
        {
            WorldLight* Light = NULL;
            Error Result = WorldLight_Create(record->Name, &Light);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            Light->Color = record->Data.Light.Color;
            Light->CastsShadows = record->Data.Light.CastsShadows;

            Error IntensityResult = WorldLight_SetIntensity(Light, record->Data.Light.Intensity);
            if (IntensityResult.Code != ErrorCode_Success)
            {
                WorldObject_Destroy(WorldLight_AsObject(Light));
                return IntensityResult;
            }
            Error SizeResult = WorldLight_SetSize(Light, record->Data.Light.Size);
            if (SizeResult.Code != ErrorCode_Success)
            {
                WorldObject_Destroy(WorldLight_AsObject(Light));
                return SizeResult;
            }
            Base = WorldLight_AsObject(Light);
            break;
        }

        default:
            return Error_Construct2(ErrorCode_IllegalArgument, "BuildObjectFromDTO: unknown object type.");
    }

    Error BaseResult = ApplyBaseFields(Base, record);
    if (BaseResult.Code != ErrorCode_Success)
    {
        WorldObject_Destroy(Base);
        return BaseResult;
    }

    *outObject = Base;
    return Error_CreateSuccess();
}


// Public functions.
Error WorldDTO_Construct(WorldDTO* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldDTO_Construct: self must not be NULL.");
    }

    self->Name = NULL;
    self->NextObjectId = 1;
    WorldEnvironment_SetDefaults(&self->Environment);
    GenericBuffer_AllocateVariable(&self->_objects, INITIAL_OBJECT_CAPACITY, sizeof(WorldObjectDTO));
    return Error_CreateSuccess();
}

Error WorldDTO_Deconstruct(WorldDTO* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldDTO_Deconstruct: self must not be NULL.");
    }

    for (size_t Index = 0; Index < self->_objects._count; Index++)
    {
        WorldObjectDTO* Record = GenericBuffer_GetPointerToElement(&self->_objects, Index);
        FreeObjectDTO(Record);
    }

    Memory_Free(self->_objects._data);
    Memory_Free(self->Name);
    WorldEnvironment_Deconstruct(&self->Environment);
    return Error_CreateSuccess();
}

Error WorldDTO_FromWorld(World* world, WorldDTO* outDTO)
{
    if ((world == NULL) || (outDTO == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldDTO_FromWorld: world and outDTO must not be NULL.");
    }

    Error ConstructResult = WorldDTO_Construct(outDTO);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        return ConstructResult;
    }

    WorldShared_CloneString(World_GetName(world), &outDTO->Name);
    outDTO->NextObjectId = World_GetNextObjectId(world);
    outDTO->Environment = *World_GetEnvironment(world);

    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            WorldDTO_Deconstruct(outDTO);
            return GetResult;
        }

        WorldObjectDTO Record;
        FillObjectDTO(Object, &Record);
        if (!GenericBuffer_AddLast(&outDTO->_objects, &Record))
        {
            FreeObjectDTO(&Record);
            WorldDTO_Deconstruct(outDTO);
            return Error_Construct2(ErrorCode_InvalidOperation, "WorldDTO_FromWorld: failed to store an object record.");
        }
    }

    return Error_CreateSuccess();
}

Error WorldDTO_ApplyToWorld(const WorldDTO* dto, World* world)
{
    if ((dto == NULL) || (world == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldDTO_ApplyToWorld: dto and world must not be NULL.");
    }

    Error NameResult = World_SetName(world, dto->Name);
    if (NameResult.Code != ErrorCode_Success)
    {
        return NameResult;
    }
    *World_GetEnvironment(world) = dto->Environment;

    for (size_t Index = 0; Index < dto->_objects._count; Index++)
    {
        const WorldObjectDTO* Record = GenericBuffer_GetPointerToElement((GenericBuffer*)&dto->_objects, Index);

        WorldObject* Object = NULL;
        Error BuildResult = BuildObjectFromDTO(Record, &Object);
        if (BuildResult.Code != ErrorCode_Success)
        {
            return BuildResult;
        }

        Error AddResult = World_AddObjectWithId(world, Object, Record->Id);
        if (AddResult.Code != ErrorCode_Success)
        {
            WorldObject_Destroy(Object);
            return AddResult;
        }
    }

    World_EnsureNextObjectIdAtLeast(world, dto->NextObjectId);
    return Error_CreateSuccess();
}

size_t WorldDTO_GetObjectCount(const WorldDTO* self)
{
    if (self == NULL)
    {
        return 0;
    }
    return self->_objects._count;
}
