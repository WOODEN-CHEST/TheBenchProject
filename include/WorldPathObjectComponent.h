#pragma once
#include <stdbool.h>
#include "wr/WRError.h"
#include "PathObject.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"


/**
 * @file WorldPathObjectComponent.h
 * @brief The path object component: a fence, hedge, railing or row of lamps placed along a path, with every setting of its
 *        asset (PathObject.h) overridable here, so a floating fence needs no second asset.
 *
 * It reads the nearest path at or above its object (WorldPathComponent_FindForObject): a lone fence keeps its path and this
 * component on one object, a road's parts are children of the road. What it generates (WorldPathObjects.h, the system that
 * reads these) lies in its own object's space as if the path's nodes were given there, on runtime children of its object
 * that world files never save, so a child with no transform of its own lies exactly on its parent's path.
 *
 * The component only holds settings. Its properties are the rows of its table ("path_object_component"): the asset's name
 * (path_object) and the settings' rows (their flags and values), the slots' records through their own table
 * (PathObject_GetSlotPropertyTable), the models' through theirs (PathObject_GetModelPropertyTable). Change them by the table
 * (Properties_Set) or the name's setter; every change advances the revision the system watches.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_PathObject, &component); reach it with
 * WorldPathObjectComponent_FromComponent.
 */


// Types.
/** @brief The path object component. Underscore-prefixed fields are read-only: use the accessors and the table. */
typedef struct WorldPathObjectComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Owned asset name of the path object (at most PATH_OBJECT_NAME_MAX_LENGTH bytes); NULL generates nothing. */
    unsigned char* _pathObject;
    /** @brief This placement's settings: each applies while its flag is on (PathObject_ResolveSettings). */
    PathObjectSettings _settings;
} WorldPathObjectComponent;


// Functions.
/**
 * @brief Returns the component's registry entry (WorldComponentType_PathObject, "path_object").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldPathObjectComponent_GetTypeInfo(void);

/**
 * @brief Returns the component's property table ("path_object_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldPathObjectComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component.
 * @param component The component, or NULL.
 * @returns The path object component, or NULL when @p component is NULL or of another type.
 */
static inline WorldPathObjectComponent* WorldPathObjectComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_PathObject))
        ? (WorldPathObjectComponent*)component : NULL;
}

/**
 * @brief Upcasts to the base.
 * @param self The component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldPathObjectComponent_AsComponent(WorldPathObjectComponent* self)
{
    return &self->Base;
}

/**
 * @brief The asset's name.
 * @param self The component; must not be NULL.
 * @returns The name in full (borrowed, valid until it changes), or NULL for none.
 */
static inline const unsigned char* WorldPathObjectComponent_GetPathObject(const WorldPathObjectComponent* self)
{
    return self->_pathObject;
}

/**
 * @brief Names the asset. Advances the revision.
 * @param self The component; must not be NULL.
 * @param pathObject The asset's name as written (`fence` is kept as `tbp:fence`), or NULL for none.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for an invalid name, nothing
 *          changed.
 */
Error WorldPathObjectComponent_SetPathObject(WorldPathObjectComponent* self, const unsigned char* pathObject);

/**
 * @brief This placement's settings: what PathObject_ResolveSettings and PathObject_ResolveSlot take. Change them by the table.
 * @param self The component; must not be NULL.
 * @returns The settings (borrowed, valid until they change).
 */
static inline const PathObjectSettings* WorldPathObjectComponent_GetSettings(const WorldPathObjectComponent* self)
{
    return &self->_settings;
}
