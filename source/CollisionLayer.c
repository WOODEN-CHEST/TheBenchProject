#include "CollisionLayer.h"

/*
 * Collision layers (include/CollisionLayer.h): their names and default masks.
 */


// Macros.
/** The layers that block each other by default: everything solid. */
#define SOLID_LAYERS (COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Prop) \
    | COLLISION_LAYER_BIT(CollisionLayer_Character) | COLLISION_LAYER_BIT(CollisionLayer_Vehicle) \
    | COLLISION_LAYER_BIT(CollisionLayer_Dynamic))
/** What a trigger volume sees by default: everything that moves or is placed. */
#define TRIGGER_SEEN_LAYERS (COLLISION_LAYER_BIT(CollisionLayer_Prop) | COLLISION_LAYER_BIT(CollisionLayer_Character) \
    | COLLISION_LAYER_BIT(CollisionLayer_Vehicle) | COLLISION_LAYER_BIT(CollisionLayer_Dynamic))


// Fields.
static const PropertyEnumEntry LayerEntries[] =
{
    { u8"world", CollisionLayer_World }, { u8"prop", CollisionLayer_Prop }, { u8"character", CollisionLayer_Character },
    { u8"vehicle", CollisionLayer_Vehicle }, { u8"dynamic", CollisionLayer_Dynamic },
    { u8"trigger", CollisionLayer_Trigger }, { u8"interaction", CollisionLayer_Interaction }
};

const PropertyEnumTable COLLISION_LAYER_ENUM =
{
    .Name = u8"collision_layer", .Entries = LayerEntries, .EntryCount = sizeof(LayerEntries) / sizeof(LayerEntries[0])
};


// Static functions.
static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Index = 0U;
    while ((a[Index] == b[Index]) && (a[Index] != (unsigned char)u8'\0'))
    {
        Index++;
    }
    return a[Index] == b[Index];
}


// Public functions.
uint32_t CollisionLayer_GetDefaultMask(CollisionLayer layer)
{
    switch (layer)
    {
        case CollisionLayer_World:
        case CollisionLayer_Prop:
        case CollisionLayer_Character:
        case CollisionLayer_Vehicle:
        case CollisionLayer_Dynamic:
            // Triggers see the solid layers, so the solid layers see triggers back (the rule is symmetric).
            return SOLID_LAYERS | ((layer != CollisionLayer_World) ? COLLISION_LAYER_BIT(CollisionLayer_Trigger) : 0U);
        case CollisionLayer_Trigger:
            return TRIGGER_SEEN_LAYERS;
        case CollisionLayer_Interaction:
            return 0U;
        case CollisionLayer_Count:
            break;
    }
    return 0U;
}

const unsigned char* CollisionLayer_GetName(CollisionLayer layer)
{
    for (size_t Index = 0U; Index < COLLISION_LAYER_ENUM.EntryCount; Index++)
    {
        if (LayerEntries[Index].Value == (int32_t)layer)
        {
            return LayerEntries[Index].Name;
        }
    }
    return NULL;
}

bool CollisionLayer_TryParse(const unsigned char* name, CollisionLayer* outLayer)
{
    if (name == NULL)
    {
        return false;
    }
    for (size_t Index = 0U; Index < COLLISION_LAYER_ENUM.EntryCount; Index++)
    {
        if (AreNamesEqual(LayerEntries[Index].Name, name))
        {
            *outLayer = (CollisionLayer)LayerEntries[Index].Value;
            return true;
        }
    }
    return false;
}
