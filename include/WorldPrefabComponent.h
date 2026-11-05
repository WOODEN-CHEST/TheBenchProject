#pragma once
#include <stdbool.h>
#include "wr/WRError.h"
#include "DataValue.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"


/**
 * @file WorldPrefabComponent.h
 * @brief The prefab component: a copy of a prefab (Prefab.h) placed by its object, the copy's root.
 *
 * It only holds what a world file keeps of a copy: the prefab's name, the values of its properties and the overrides of its
 * objects. WorldPrefabs (the system) makes the prefab's objects as runtime children of the root, which world files never save, so
 * the file stays small and editing the prefab updates every copy.
 *
 * VALUES (values): property name -> value, each in the JSON form of what it targets ("#2e4a7a", 35, "garage"): a compound, whichever
 * format holds it. A property it does not name takes its default.
 *
 * OVERRIDES (object_overrides): a path of names -> a block, for one object of the copy:
 * { "is_removed": true }, the object and what lies under it are not made;
 * { "rotation": [0, 35, 0] }, any row of the object's table but its name, its parent and its data;
 * { "components": { "model": { "uses_variant": true, "variant": "red" } } }, any row of one of its components' tables.
 * A path through a nested copy names each level: "lamp/bulb" is the object "bulb" of the copy that the object "lamp" holds. A name
 * the prefab no longer has is warned about once and ignored.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Prefab, &component); reach it with WorldPrefabComponent_FromComponent.
 * Change it through the table or the setters here (which go through the table): every change advances the revision the system
 * watches.
 */


// Macros.
/** @brief The longest prefab name, in bytes. */
#define WORLD_PREFAB_COMPONENT_NAME_MAX_LENGTH ((size_t)128)
/** @brief The most bytes a copy's values may count (DataValue_GetByteSize). */
#define WORLD_PREFAB_COMPONENT_VALUES_MAX_BYTES ((size_t)65536)
/** @brief The most bytes a copy's overrides may count (DataValue_GetByteSize). */
#define WORLD_PREFAB_COMPONENT_OVERRIDES_MAX_BYTES ((size_t)1048576)
/** @brief What separates the names of an override's path. */
#define WORLD_PREFAB_PATH_SEPARATOR ((unsigned char)'/')
/** @brief The key of an override block's flag that removes the object. */
#define WORLD_PREFAB_OVERRIDE_IS_REMOVED_KEY ((const unsigned char*)u8"is_removed")
/** @brief The key of an override block's rows of the object's components. */
#define WORLD_PREFAB_OVERRIDE_COMPONENTS_KEY ((const unsigned char*)u8"components")


// Types.
/** @brief The prefab component. Underscore-prefixed fields are read-only: use the accessors and the table. */
typedef struct WorldPrefabComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Owned asset name of the prefab; NULL makes nothing. */
    unsigned char* _prefab;
    /** @brief Owned compound of the property values; NULL while empty. */
    DataValue* _values;
    /** @brief Owned compound of the object overrides, by path; NULL while empty. */
    DataValue* _objectOverrides;
} WorldPrefabComponent;


// Functions.
/**
 * @brief Returns the component's registry entry (WorldComponentType_Prefab, "prefab").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldPrefabComponent_GetTypeInfo(void);

/**
 * @brief Returns the component's property table ("prefab_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldPrefabComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component.
 * @param component The component, or NULL.
 * @returns The prefab component, or NULL when @p component is NULL or of another type.
 */
static inline WorldPrefabComponent* WorldPrefabComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Prefab)) ? (WorldPrefabComponent*)component : NULL;
}

/**
 * @brief Upcasts to the base.
 * @param self The component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldPrefabComponent_AsComponent(WorldPrefabComponent* self)
{
    return &self->Base;
}

/**
 * @brief The prefab's name.
 * @param self The component; must not be NULL.
 * @returns The name in full (borrowed, valid until it changes), or NULL for none.
 */
static inline const unsigned char* WorldPrefabComponent_GetPrefab(const WorldPrefabComponent* self)
{
    return self->_prefab;
}

/**
 * @brief Sets the prefab: the copy's objects are made anew by the system.
 * @param self The component; must not be NULL.
 * @param prefab The asset name, or NULL for none.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for a name that breaks its rule.
 */
Error WorldPrefabComponent_SetPrefab(WorldPrefabComponent* self, const unsigned char* prefab);

/**
 * @brief A property's value this copy sets.
 * @param self The component; must not be NULL.
 * @param name The property's name; must not be NULL.
 * @returns The value (borrowed, valid until the values change), or NULL when the copy leaves it at its default.
 */
const DataValue* WorldPrefabComponent_GetValue(const WorldPrefabComponent* self, const unsigned char* name);

/**
 * @brief Sets a property's value on this copy, in the JSON form of what it targets, or takes it back to its default.
 * @param self The component; must not be NULL.
 * @param name The property's name: a local name. Must not be NULL.
 * @param value The value, copied; NULL takes it back to its default.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments or a name that is no local name; the table's error for a value
 *          that breaks its rules.
 */
Error WorldPrefabComponent_SetValue(WorldPrefabComponent* self, const unsigned char* name, const DataValue* value);

/**
 * @brief An object's override block on this copy.
 * @param self The component; must not be NULL.
 * @param path The object's path of names; must not be NULL.
 * @returns The block, a compound (borrowed, valid until the overrides change), or NULL for none.
 */
const DataValue* WorldPrefabComponent_GetObjectOverride(const WorldPrefabComponent* self, const unsigned char* path);

/**
 * @brief Overrides a row of an object of this copy, or takes the override off.
 * @param self The component; must not be NULL.
 * @param path The object's path of names (WORLD_PREFAB_PATH_SEPARATOR between them); must not be NULL.
 * @param component The component's registry name ("model"), or NULL for the object's own table.
 * @param row The row's key; must not be NULL.
 * @param value The row's value in its JSON form, copied; NULL takes the override off. A block left with nothing in it goes.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; the table's error for overrides that break its rules.
 */
Error WorldPrefabComponent_SetObjectRow(WorldPrefabComponent* self, const unsigned char* path, const unsigned char* component,
    const unsigned char* row, const DataValue* value);

/**
 * @brief Removes an object of this copy, with what lies under it, or brings it back.
 * @param self The component; must not be NULL.
 * @param path The object's path of names; must not be NULL.
 * @param isRemoved Whether it is removed. False takes the flag off an object of this copy's prefab; through a nested copy it stays written,
 *        since that copy's own prefab may remove the object (WorldPrefabComponent_ClearObjectOverride takes it off).
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; the table's error for overrides that break its rules.
 */
Error WorldPrefabComponent_SetObjectRemoved(WorldPrefabComponent* self, const unsigned char* path, bool isRemoved);

/**
 * @brief Takes every override of an object of this copy off.
 * @param self The component; must not be NULL.
 * @param path The object's path of names; must not be NULL.
 * @returns Success (also when it had none); ErrorCode_IllegalArgument for NULL arguments.
 */
Error WorldPrefabComponent_ClearObjectOverride(WorldPrefabComponent* self, const unsigned char* path);
