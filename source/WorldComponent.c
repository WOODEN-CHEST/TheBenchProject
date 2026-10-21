#include "WorldComponent.h"
#include "WorldInternal.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "WorldLightComponent.h"
#include "WorldColliderComponent.h"


// Static functions.
/* [a-z][a-z0-9_]*: component type names, like property keys. */
static bool IsSnakeCase(const unsigned char* text)
{
    if ((text == NULL) || (text[0] < (unsigned char)'a') || (text[0] > (unsigned char)'z'))
    {
        return false;
    }
    for (size_t Index = 1; text[Index] != (unsigned char)0; Index++)
    {
        unsigned char Byte = text[Index];
        bool IsLetter = (Byte >= (unsigned char)'a') && (Byte <= (unsigned char)'z');
        bool IsDigit = (Byte >= (unsigned char)'0') && (Byte <= (unsigned char)'9');
        if (!IsLetter && !IsDigit && (Byte != (unsigned char)'_'))
        {
            return false;
        }
    }
    return true;
}

static Error InfoError(const WorldComponentTypeInfo* info, const char* problem)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "Component type %u (%s): %s.", (unsigned int)info->Type,
        (info->Name != NULL) ? (const char*)info->Name : "?", problem);
}


// Public functions.
const WorldComponentTypeInfo* WorldComponent_GetStandardTypeInfo(WorldComponentType type)
{
    switch (type)
    {
        case WorldComponentType_Model:
            return WorldModelComponent_GetTypeInfo();
        case WorldComponentType_Sprite:
            return WorldSpriteComponent_GetTypeInfo();
        case WorldComponentType_Light:
            return WorldLightComponent_GetTypeInfo();
        case WorldComponentType_Collider:
            return WorldColliderComponent_GetTypeInfo();
        case WorldComponentType_Count:
            break;
    }
    return NULL;
}

Error WorldComponentTypeInfo_Check(const WorldComponentTypeInfo* info)
{
    if (info == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldComponentTypeInfo_Check: info must not be NULL.");
    }
    if ((size_t)info->Type >= WORLD_MAX_COMPONENT_TYPES)
    {
        return InfoError(info, "the type is not below WORLD_MAX_COMPONENT_TYPES");
    }
    if (!IsSnakeCase(info->Name))
    {
        return InfoError(info, "the name is not snake_case");
    }
    if (info->Size < sizeof(WorldComponent))
    {
        return InfoError(info, "the size cannot hold the WorldComponent base");
    }
    if ((info->Construct == NULL) || (info->Deconstruct == NULL))
    {
        return InfoError(info, "Construct and Deconstruct are required");
    }
    if (info->Table == NULL)
    {
        return InfoError(info, "the property table is missing");
    }
    if (info->Table->StructSize != info->Size)
    {
        return InfoError(info, "the property table describes a struct of another size");
    }
    if (info->Table->AfterSet != WorldInternal_ComponentAfterSet)
    {
        return InfoError(info, "the property table's AfterSet is not WorldInternal_ComponentAfterSet");
    }
    return PropertyTable_Check(info->Table);
}
