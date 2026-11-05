#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRJSON.h"
#include "AssetManager.h"
#include "DataValue.h"
#include "NamespacedKey.h"
#include "PropertyDescriptor.h"
#include "World.h"
#include "WorldObject.h"


/**
 * @file Prefab.h
 * @brief The prefab asset type (JSON in prefabs/): a group of objects built once and placed many times, and the properties a copy
 *        sets. references/asset_structure.md, "Prefabs", has the format; WorldPrefabs.h is what makes copies of one.
 *
 * THE OBJECTS are in the world file's own format, read by the world file's own reader into a template world that is never shown:
 * every table, codec and rule a world's objects have. They have one root, and each has a name of its own inside the prefab, a local
 * name: that name addresses it, and a copy's object takes its id from it. Tags and data live in a key registry of the prefab's own,
 * because an asset may serve worlds of other registries.
 *
 * THE PROPERTIES are data the editor builds a copy's options panel from, not a static table: each a type (PrefabPropertyType), a
 * default, a display name, a category, a description and a range. Each writes to targets: rows of the objects it names, by object
 * name or tag, or a property of a nested copy. An enum property's options are each a list of values to set: the one way a prefab has
 * variants. No expressions: a window_count can drive a path object's count, and logic waits for scripting.
 *
 * THE VALUES are plain values in the JSON form of what they target (Properties_SetFromDataValue): each target's row reads the value
 * through its own reader, so one colour reaches a tint and a light's colour, and everything is checked when the file is read: a
 * default or an option's value through every row it reaches.
 *
 * Immutable once loaded: everything here is read only, and a copy holds the prefab under its system's asset user.
 */


// Macros.
/** @brief The most objects a prefab holds. */
#define PREFAB_MAX_OBJECTS ((size_t)16384)
/** @brief The most properties a prefab declares. */
#define PREFAB_MAX_PROPERTIES ((size_t)256)
/** @brief The most options an enum property has. */
#define PREFAB_MAX_OPTIONS ((size_t)256)
/** @brief The most targets of a property or an option. */
#define PREFAB_MAX_TARGETS ((size_t)1024)
/** @brief The most objects every target of a prefab reaches together: a tag can reach many, so a hostile file's targets could
 *  otherwise ask for any amount of memory and of work per copy. */
#define PREFAB_MAX_TARGET_OBJECTS ((size_t)65536)
/** @brief How many objects all of a prefab's tag targets may look through together: each looks through every object. */
#define PREFAB_MAX_TAG_SCAN ((size_t)4194304)
/** @brief The most bytes one value may count (DataValue_GetByteSize). */
#define PREFAB_VALUE_MAX_BYTES ((size_t)65536)
/** @brief The longest display name, category or description, in bytes. */
#define PREFAB_TEXT_MAX_LENGTH ((size_t)1024)
/** @brief An object's parent when it is the prefab's root. */
#define PREFAB_NO_PARENT UINT32_MAX
/** @brief A target's component when it writes the object's own table. */
#define PREFAB_NO_COMPONENT (-1)


// Types.
/** @brief The type of a declared property: what the editor shows and what a value must be. Saved in files by name. */
typedef enum PrefabPropertyTypeEnum
{
    /** @brief true or false. */
    PrefabPropertyType_Bool = 0,
    /** @brief A whole number. */
    PrefabPropertyType_Int = 1,
    /** @brief A number. */
    PrefabPropertyType_Float = 2,
    /** @brief A colour, as a row reads one ("#rrggbb"). */
    PrefabPropertyType_Color = 3,
    /** @brief Three numbers. */
    PrefabPropertyType_Vector = 4,
    /** @brief Two numbers. */
    PrefabPropertyType_Vector2 = 5,
    /** @brief A number of degrees. */
    PrefabPropertyType_Angle = 6,
    /** @brief The name of one of its options: a dropdown, each option a list of values to set. */
    PrefabPropertyType_Enum = 7,
    /** @brief An asset's name (a model to use). */
    PrefabPropertyType_Asset = 8,
    /** @brief A text. */
    PrefabPropertyType_String = 9
} PrefabPropertyType;

/** @brief What a target writes of a vector row: all of it, or a number into one or every axis, leaving the rest. Saved by name. */
typedef enum PrefabAxisEnum
{
    PrefabAxis_None = 0,
    PrefabAxis_X = 1,
    PrefabAxis_Y = 2,
    PrefabAxis_Z = 3,
    PrefabAxis_All = 4
} PrefabAxis;

/** @brief A target as a file writes it: a record of the "prefab_target" table. */
typedef struct PrefabTargetRecordStruct
{
    /** @brief The object it reaches by name, or NULL: then Tag says. Exactly one of the two is set. */
    unsigned char* Object;
    /** @brief The tag whose objects it reaches (a namespaced key's text), or NULL. */
    unsigned char* Tag;
    /** @brief The component whose table holds Row, by its registry name; NULL: the object's own table. */
    unsigned char* Component;
    /** @brief The row it writes, or NULL: then Property says. Exactly one of the two is set. */
    unsigned char* Row;
    /** @brief The property of the nested copy on its objects that it writes, or NULL. */
    unsigned char* Property;
    /** @brief What it writes of a vector row. */
    PrefabAxis Axis;
    /** @brief An enum option's value to set; owned. NULL in a property's targets, which write the property's value. */
    DataValue* Value;
} PrefabTargetRecord;

/** @brief A target resolved against the prefab's objects. Read only. */
typedef struct PrefabTargetStruct
{
    /** @brief The file's record. */
    const PrefabTargetRecord* Record;
    /** @brief The indices of the objects it reaches, ObjectCount of them, at least one. */
    const uint32_t* Objects;
    size_t ObjectCount;
    /** @brief The component type holding Row, or PREFAB_NO_COMPONENT for the object's own table. */
    int32_t ComponentType;
    /** @brief The table holding Row; NULL for a nested copy's property. */
    const PropertyTable* Table;
    /** @brief The row it writes; NULL for a nested copy's property. */
    const PropertyDescriptor* Row;
} PrefabTarget;

/** @brief An enum property's option. Read only. */
typedef struct PrefabOptionStruct
{
    /** @brief Its name: what a value holds. */
    const unsigned char* Name;
    /** @brief What the editor shows, or NULL: its name. */
    const unsigned char* DisplayName;
    /** @brief The values it sets. */
    const PrefabTarget* Targets;
    size_t TargetCount;
} PrefabOption;

/** @brief A declared property. Read only. */
typedef struct PrefabPropertyStruct
{
    /** @brief Its name: a local name, what a copy's values use. */
    const unsigned char* Name;
    /** @brief Its type. */
    PrefabPropertyType Type;
    /** @brief What the editor shows, each may be NULL. */
    const unsigned char* DisplayName;
    const unsigned char* Category;
    const unsigned char* Description;
    /** @brief An asset property's asset type ("world_model"), or NULL for any: what the editor offers. */
    const unsigned char* AssetType;
    /** @brief Its default: never NULL. */
    const DataValue* Default;
    /** @brief The smallest and largest number allowed, for Int, Float, Angle and each axis of the vectors. */
    bool HasMinimum;
    double Minimum;
    bool HasMaximum;
    double Maximum;
    /** @brief What it writes, unless it is an enum, whose options write. */
    const PrefabTarget* Targets;
    size_t TargetCount;
    /** @brief An enum's options. */
    const PrefabOption* Options;
    size_t OptionCount;
} PrefabProperty;


// Functions: the file.
/**
 * @brief Reads a prefab from its parsed file: every rule checked, unknown keys refused everywhere, the file as hostile.
 * @param root The file's root compound (its "name" is not looked at); must not be NULL.
 * @param fileByteCount How many bytes the file has: what a copy counts as holding (WorldPrefabsLimits.MaxBytes).
 * @param pool The pool holding its tree; must not be NULL.
 * @param manager Knows the asset types an asset property may name; must not be NULL.
 * @param outPrefab [out] Receives the prefab; the caller owns it (Prefab_Destroy). NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_InvalidJSON naming the problem and where it is.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error Prefab_Read(JSONCompound* root, size_t fileByteCount, JSONObjectPool* pool, AssetManager* manager, Prefab** outPrefab);

/**
 * @brief Releases a prefab.
 * @param self The prefab, or NULL.
 */
void Prefab_Destroy(Prefab* self);

/** @brief The table of a target's record ("prefab_target"), for the editor. @returns The table (static). */
const PropertyTable* Prefab_GetTargetPropertyTable(void);

/** @brief The table of a property's plain rows ("prefab_property"), for the editor. @returns The table (static). */
const PropertyTable* Prefab_GetPropertyPropertyTable(void);


// Functions: the objects.
/** @brief How many objects it holds: at least one. @param self The prefab; must not be NULL. @returns The count. */
size_t Prefab_GetObjectCount(const Prefab* self);

/** @brief How many bytes its file has. @param self The prefab; must not be NULL. @returns The count. */
size_t Prefab_GetFileByteCount(const Prefab* self);

/**
 * @brief An object's name. Objects come parents first, the root at index 0, children in the order of the file.
 * @param self The prefab; must not be NULL.
 * @param index The object's index, below the count.
 * @returns The name (borrowed).
 */
const unsigned char* Prefab_GetObjectName(const Prefab* self, size_t index);

/**
 * @brief An object's parent.
 * @param self The prefab; must not be NULL.
 * @param index The object's index, below the count.
 * @returns The parent's index, always below @p index; PREFAB_NO_PARENT for the root.
 */
uint32_t Prefab_GetObjectParent(const Prefab* self, size_t index);

/**
 * @brief An object's template: its rows and components are what a copy's object starts from. Its tags and data use the prefab's own
 *        registry (Prefab_GetKeys).
 * @param self The prefab; must not be NULL.
 * @param index The object's index, below the count.
 * @returns The object (borrowed); read it, never change it.
 */
const WorldObject* Prefab_GetObjectTemplate(const Prefab* self, size_t index);

/**
 * @brief Finds an object by its name.
 * @param self The prefab; must not be NULL.
 * @param name The name's bytes; need not be NUL-terminated. May be NULL when @p byteCount is 0.
 * @param byteCount The number of bytes.
 * @param outIndex [out] Receives its index when found. Must not be NULL.
 * @returns Whether it has one of that name.
 */
bool Prefab_TryFindObject(const Prefab* self, const unsigned char* name, size_t byteCount, uint32_t* outIndex);

/**
 * @brief Finds an object by the id its file gives it, as a `$object` value in the file's data names it.
 * @param self The prefab; must not be NULL.
 * @param id The id in the file.
 * @param outIndex [out] Receives its index. Must not be NULL.
 * @returns Whether the file has such an object.
 */
bool Prefab_TryFindObjectById(const Prefab* self, uint64_t id, uint32_t* outIndex);

/** @brief The registry its templates' tags and data use. @param self The prefab; must not be NULL. @returns The registry. */
const KeyRegistry* Prefab_GetKeys(const Prefab* self);


// Functions: the properties.
/** @brief How many properties it declares. @param self The prefab; must not be NULL. @returns The count. */
size_t Prefab_GetPropertyCount(const Prefab* self);

/**
 * @brief A property, in the order the file declares them.
 * @param self The prefab; must not be NULL.
 * @param index The property's index, below the count.
 * @returns The property (borrowed).
 */
const PrefabProperty* Prefab_GetProperty(const Prefab* self, size_t index);

/**
 * @brief Finds a property by its name.
 * @param self The prefab; must not be NULL.
 * @param name The name; must not be NULL.
 * @returns The property (borrowed), or NULL.
 */
const PrefabProperty* Prefab_FindProperty(const Prefab* self, const unsigned char* name);

/**
 * @brief Finds an enum property's option by its name.
 * @param property The property; must not be NULL.
 * @param name The name; must not be NULL.
 * @returns The option (borrowed), or NULL.
 */
const PrefabOption* PrefabProperty_FindOption(const PrefabProperty* property, const unsigned char* name);

/**
 * @brief Checks a value against a property's type and range. A value that passes may still be refused by a row it reaches.
 * @param property The property; must not be NULL.
 * @param value The value; must not be NULL.
 * @param pool Lends the JSON a colour is read through; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange naming what is wrong.
 */
Error PrefabProperty_CheckValue(const PrefabProperty* property, const DataValue* value, JSONObjectPool* pool);
