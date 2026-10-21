#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector2, Vector3 and Color come from raylib and RenderColor from the renderer: property values carry them by value.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRError.h"
#include "wr/WRJSON.h"
#include "wr/WRGHDF.h"

// Forward declarations: the read options borrow a logger for warnings (Logger.h) and a key registry for data maps
// (NamespacedKey.h); a DataMap property holds a map (DataMap.h).
typedef struct LoggerStruct Logger;
typedef struct KeyRegistryStruct KeyRegistry;
typedef struct DataMapStruct DataMap;


/**
 * @file PropertyDescriptor.h
 * @brief Property tables: one static table per data type describes every property of the type (key, binary id,
 *        type, default, rules, flags), and generic functions driven by the table do the rest.
 *
 * A data type that holds settings or saved state (the world environment, later world objects and their components,
 * the game clock, weather) declares a PropertyTable next to its struct: one PropertyDescriptor per field, written
 * once. Everything else comes from the table, so a new property is one table row instead of six hand-written places:
 * - defaults: Properties_Construct (zero the struct, then write every default; owned strings are cloned) and
 *   Properties_Deconstruct (free the owned strings);
 * - validation: Properties_Validate (every rule; the error names the table, the key and the rule) and
 *   Properties_CheckValue (one value, for hand-written setters, so the rules live in the table only);
 * - deep copy and comparison: Properties_CopyTo and Properties_Equals;
 * - JSON and GHDF: Properties_WriteJSON / Properties_ReadJSON and Properties_WriteGHDF / Properties_ReadGHDF. Reads
 *   are all or nothing: they read into a scratch copy, validate it, then commit;
 * - access by key for the editor and scripts: Properties_Get / Properties_Set (validated).
 * PropertyTable_Check verifies a table itself (unique keys and ids, fields inside the struct, defaults that pass their
 * own rules); a test runs it for every table in the game. The other functions trust their table (they copy each field
 * by its descriptor's offset and size), so every table must pass that check.
 *
 * Tables write fields by offset, including `_`-prefixed (read-only) ones: this module is the one place allowed to do
 * that. Hand-written setters stay for C callers and validate through Properties_CheckValue.
 *
 * Values in files: JSON keys are the descriptors' snake_case keys; GHDF entry ids are their wire ids, which are never
 * reused (a removed property's id goes into the table's retired list). Per type:
 *
 * | Type        | In memory                   | JSON                                   | GHDF                          |
 * |-------------|-----------------------------|----------------------------------------|-------------------------------|
 * | Bool        | bool                        | true / false                           | Boolean                       |
 * | Int32       | int32_t                     | integer                                | smallest signed type (*)      |
 * | UInt32      | uint32_t                    | integer                                | smallest unsigned type (*)    |
 * | UInt64      | uint64_t                    | integer (a string above INT64_MAX)     | smallest unsigned type (*)    |
 * | Float       | float                       | number                                 | Float                         |
 * | Double      | double                      | number                                 | Double                        |
 * | Angle       | float, radians              | number, DEGREES                        | Float, radians                |
 * | Vector2     | Vector2                     | [x, y]                                 | Float array of 2              |
 * | Vector3     | Vector3                     | [x, y, z]                              | Float array of 3              |
 * | AngleVector3 | Vector3, radians          | [x, y, z], DEGREES                     | Float array of 3, radians     |
 * | Color       | Color                       | "#rrggbb" or "#rrggbbaa"               | UInt8 array of 4 (RGBA)       |
 * | RenderColor | RenderColor                 | "#rrggbb" or {tint, brightness, opacity} | Compound {1: UInt8[3] tint, 2: Float brightness, 3: Float opacity} |
 * | String      | unsigned char*, owned       | string, or null                        | String (absent when NULL)     |
 * | AssetName   | unsigned char*, owned       | string, or null                        | String (absent when NULL)     |
 * | Enum        | integer of 1, 2 or 4 bytes  | the entry's name, e.g. "near"          | EncodedInteger (the value)    |
 * | ObjectId    | uint64_t, 0 = none          | integer (a string above INT64_MAX)     | smallest unsigned type (*)    |
 * | Date        | int64_t day number          | "yyyy-mm-dd", e.g. "2026-10-18"        | EncodedInteger (the day number) |
 * | DataMap     | DataMap*, owned, NULL while empty | two keys: TagsKey ["key", ...] and Key {"key": value} | Compound array of entries |
 * | FloatList   | PropertyFloatList*, owned, NULL while empty | [a, b, ...]                  | Float array                   |
 * | EnumSet     | uint32_t mask (bit N: the entry of value N) | ["name", ...] (table order)    | smallest unsigned type (*)    |
 *
 * (*) Written in the smallest GHDF integer type that holds the value (a parent id of 12 is one UInt8 byte; GameGHDF.h);
 * read from any GHDF integer type, fixed width or encoded, whose value fits the property's type.
 *
 * JSON readers accept every form the GameJSON parsers accept (colors also as arrays or {r, g, b, a} compounds,
 * vectors also as {x, y, z} compounds, numbers also as numeric strings). A RenderColor's tint alpha is not part of
 * its value (RenderColor ignores it): it is not written, reads set it to 255 and comparisons skip it. A Date is a
 * day number of the game calendar (GameCalendar.h: days since 0001-01-01), and its JSON text is only ever the
 * "yyyy-mm-dd" form. A DataMap (tags and typed custom data, DataMap.h) is written as two JSON keys, its tags under the
 * descriptor's TagsKey and its values under its Key; reading one needs PropertyReadOptions.Keys, the program's key
 * registry, to intern the keys. A FloatList (a world model placement's LOD distances) is a list of finite floats: an
 * empty list is NULL, written [] in JSON and as an empty Float array in GHDF when defaults are written. An EnumSet (a
 * collider's layer mask) is a set of an enum table's entries, whose values are their bit indices (0 to 31): JSON
 * names them in an array, GHDF stores the mask.
 *
 * Not thread-safe: tables are immutable static data, but instances are plain structs with no locking.
 */


// Macros.
/**
 * @brief Fills a descriptor's Offset, Size and MemberName from a struct member, e.g.
 *        `PROPERTY_MEMBER(WorldEnvironment, TimeOfDay)`.
 * @param structType The struct type the table describes.
 * @param member The member's name in that struct.
 */
#define PROPERTY_MEMBER(structType, member) \
    .Offset = offsetof(structType, member), .Size = sizeof(((structType*)NULL)->member), \
    .MemberName = (const unsigned char*)#member

/** @brief Rules with no bounds (any finite value for floats; any value for the other numeric types). */
#define PROPERTY_RULES_NONE { .HasMinimum = false }
/** @brief Rules for a number of at least @p minimum (inclusive). */
#define PROPERTY_RULES_AT_LEAST(minimum) { .HasMinimum = true, .Minimum = (minimum) }
/** @brief Rules for a number strictly above @p minimum (exclusive; for values where the minimum itself breaks the
 *         maths, such as a zero length). */
#define PROPERTY_RULES_ABOVE(minimum) { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = (minimum) }
/** @brief Rules for a number in [@p minimum, @p maximum] (both inclusive). */
#define PROPERTY_RULES_BETWEEN(minimum, maximum) \
    { .HasMinimum = true, .Minimum = (minimum), .HasMaximum = true, .Maximum = (maximum) }
/** @brief Rules for a number in [@p minimum, @p maximum): the maximum itself excluded, for values that wrap there
 *         (seconds of a day). */
#define PROPERTY_RULES_HALF_OPEN(minimum, maximum) \
    { .HasMinimum = true, .Minimum = (minimum), .HasMaximum = true, .IsMaximumExclusive = true, .Maximum = (maximum) }
/** @brief Rules for a String or AssetName: at most @p maxLength bytes (without the terminator), and whether NULL is
 *         allowed (a string that may be NULL must default to NULL). */
#define PROPERTY_RULES_STRING(maxLength, isNullAllowed) { .MaxLength = (maxLength), .IsNullAllowed = (isNullAllowed) }
/** @brief Rules for a DataMap: at most @p maxCount entries counting at most @p maxBytes bytes (DataMap_GetByteSize). */
#define PROPERTY_RULES_DATA_MAP(maxCount, maxBytes) { .MaxLength = (maxBytes), .MaxCount = (maxCount) }
/** @brief Rules for a FloatList: at most @p maxCount finite values (at most PROPERTY_FLOAT_LIST_MAX_COUNT). */
#define PROPERTY_RULES_FLOAT_LIST(maxCount) { .MaxCount = (maxCount) }
/** @brief Rules for a FloatList of at most @p maxCount values, each above @p minimum (exclusive) and above the one
 *         before it (switch distances, thresholds). */
#define PROPERTY_RULES_INCREASING_FLOAT_LIST_ABOVE(maxCount, minimum) \
    { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = (minimum), .MaxCount = (maxCount), \
        .IsStrictlyIncreasing = true }

/** @brief The most values any FloatList may hold, whatever its rules say (a row's Rules.MaxCount must not pass it), so a
 *         list's size never overflows and no single list can take much memory. */
#define PROPERTY_FLOAT_LIST_MAX_COUNT ((size_t)4096)

/** @brief The flags of a creator setting: saved, shown in the editor's inspector and reachable from scripts. */
#define PROPERTY_FLAGS_SETTING ((uint32_t)(PropertyFlags_Persistent | PropertyFlags_Editable | PropertyFlags_Script))

/**
 * @brief The key prefix of an override flag.
 *
 * A setting a placement can override (a world model's draw distance, say) is two rows: the flag `overrides_<key>` (a
 * Bool, false by default: the asset's value applies) and the value `<key>` next to it, which is kept, and saved, while
 * the flag is off, so switching it back on restores it. No sentinel values and no "inherit" states (the owner's rule,
 * G17). PropertyTable_Check requires every row whose key starts with the prefix to be a Bool that names another row of
 * its table; the editor pairs them by the names.
 */
#define PROPERTY_OVERRIDE_FLAG_PREFIX u8"overrides_"


// Types.
/**
 * @brief The type of a property: how its field is stored in the struct, which rules apply and how it is written to
 *        JSON and GHDF (see the table in the file comment).
 */
typedef enum PropertyTypeEnum
{
    /** @brief A bool. */
    PropertyType_Bool,
    /** @brief An int32_t; Minimum/Maximum rules must be whole numbers within its range. */
    PropertyType_Int32,
    /** @brief A uint32_t; Minimum/Maximum rules must be whole numbers within its range. */
    PropertyType_UInt32,
    /** @brief A uint64_t (seeds, counters); every value is valid, so it takes no Minimum/Maximum rules. */
    PropertyType_UInt64,
    /** @brief A float; always finite, plus optional bounds. */
    PropertyType_Float,
    /** @brief A double, for values that need more precision than a float (the game clock's seconds); always finite,
     *  plus optional bounds (compared in double precision). */
    PropertyType_Double,
    /** @brief An angle: a float in radians in memory and in GHDF, written in degrees in JSON. Always finite, plus
     *  optional bounds (in radians). */
    PropertyType_Angle,
    /** @brief A Vector2; every component finite and within the optional bounds. */
    PropertyType_Vector2,
    /** @brief A Vector3; every component finite and within the optional bounds. */
    PropertyType_Vector3,
    /** @brief Three angles (Euler rotations): a Vector3 in radians in memory and in GHDF, written in degrees in JSON.
     *  Every component finite and within the optional bounds (in radians). */
    PropertyType_AngleVector3,
    /** @brief A raylib Color (RGBA, all four channels are data). */
    PropertyType_Color,
    /** @brief A RenderColor: tint RGB plus brightness and opacity, both in [0, 1] (the tint's alpha is ignored). */
    PropertyType_RenderColor,
    /** @brief An owned, NUL-terminated UTF-8 string (`unsigned char*`), at most Rules.MaxLength bytes; NULL only if
     *  Rules.IsNullAllowed. */
    PropertyType_String,
    /** @brief An owned asset name (`unsigned char*`) matching AssetManager_IsValidAssetName, at most Rules.MaxLength
     *  bytes; NULL only if Rules.IsNullAllowed. The editor will offer a picker. */
    PropertyType_AssetName,
    /** @brief An enum stored as an integer of 1, 2 (both unsigned) or 4 bytes (int32); its value must be an entry of
     *  the descriptor's EnumTable, and JSON writes the entry's name. */
    PropertyType_Enum,
    /** @brief A world object id (uint64_t), 0 = none; every value is valid. */
    PropertyType_ObjectId,
    /** @brief A calendar date: an int64_t day number of the game calendar (GameCalendar.h: days since 0001-01-01),
     *  which must lie in the calendar (years 1 to 9999). Takes no Minimum/Maximum rules. */
    PropertyType_Date,
    /** @brief Tags and typed custom data: an owned `DataMap*` (DataMap.h), NULL while empty, which is its only default.
     *  Rules.MaxCount and Rules.MaxLength bound its entries and bytes; the descriptor's TagsKey names the JSON key of
     *  its tags. */
    PropertyType_DataMap,
    /** @brief A list of floats: an owned `PropertyFloatList*`, NULL while empty, which is its only default. Every value
     *  is finite and within the optional bounds; Rules.MaxCount (required) bounds the count, Rules.IsStrictlyIncreasing
     *  makes each value rise above the one before it. */
    PropertyType_FloatList,
    /** @brief A set of an enum's entries: a uint32_t mask, bit N set for the entry of value N. The descriptor's
     *  EnumTable (required) gives the names, its values 0 to 31; a set bit must name an entry. Takes no other rules. */
    PropertyType_EnumSet
} PropertyType;

/**
 * @brief Bit flags of a descriptor (combine with |; stored in PropertyDescriptor.Flags).
 */
typedef enum PropertyFlagsEnum
{
    /** @brief No flags: a runtime-only property that is not saved, shown or scripted. */
    PropertyFlags_None = 0,
    /** @brief Saved: written and read by the JSON and GHDF functions (the others are skipped). */
    PropertyFlags_Persistent = 1 << 0,
    /** @brief Shown in the editor's inspector. */
    PropertyFlags_Editable = 1 << 1,
    /** @brief Visible but not settable through Properties_Set (ids, derived values). Reads from files still fill it. */
    PropertyFlags_ReadOnly = 1 << 2,
    /** @brief Reachable from scripts: the scripting language's bindings read the flag, the type and the description
     *  for type checking and autocomplete. */
    PropertyFlags_Script = 1 << 3
} PropertyFlags;

/**
 * @brief A list of floats: the value of a PropertyType_FloatList property, one heap block holding its count and its
 *        values.
 *
 * An empty list is NULL (there is no empty block). Create one with PropertyFloatList_Create and release it with
 * PropertyFloatList_Destroy; the instance whose field holds a list owns it, and Properties_Set copies the list it is
 * given. Immutable once created: read it through PropertyFloatList_GetCount and PropertyFloatList_GetValues.
 */
typedef struct PropertyFloatListStruct
{
    /** @brief The number of values: 1 to PROPERTY_FLOAT_LIST_MAX_COUNT. */
    size_t _count;
    /** @brief The values, _count of them. */
    float _values[];
} PropertyFloatList;

/**
 * @brief The data of a property value; the active member is selected by the property's PropertyType.
 */
typedef union PropertyDataUnion
{
    /** @brief PropertyType_Bool. */
    bool BoolValue;
    /** @brief PropertyType_Int32. */
    int32_t Int32Value;
    /** @brief PropertyType_UInt32. */
    uint32_t UInt32Value;
    /** @brief PropertyType_UInt64. */
    uint64_t UInt64Value;
    /** @brief PropertyType_Float, and PropertyType_Angle in radians. */
    float FloatValue;
    /** @brief PropertyType_Double. */
    double DoubleValue;
    /** @brief PropertyType_Vector2. */
    Vector2 Vector2Value;
    /** @brief PropertyType_Vector3, and PropertyType_AngleVector3 in radians. */
    Vector3 Vector3Value;
    /** @brief PropertyType_Color. */
    Color ColorValue;
    /** @brief PropertyType_RenderColor. */
    RenderColor RenderColorValue;
    /** @brief PropertyType_String and PropertyType_AssetName: a NUL-terminated UTF-8 string or NULL. Borrowed: in a
     *  default it is a static string the instance gets a clone of; from Properties_Get it points into the instance
     *  and stays valid until that property changes or the instance is deconstructed. */
    const unsigned char* StringValue;
    /** @brief PropertyType_Enum: the entry's value. */
    int32_t EnumValue;
    /** @brief PropertyType_ObjectId (0 = none). */
    uint64_t ObjectIdValue;
    /** @brief PropertyType_Date: the day number (days since 0001-01-01). */
    int64_t DateValue;
    /** @brief PropertyType_DataMap: the map, or NULL for an empty one. Borrowed: in a default it must be NULL; from
     *  Properties_Get it points into the instance and stays valid until that property changes; Properties_Set copies
     *  it. */
    const DataMap* DataMapValue;
    /** @brief PropertyType_FloatList: the list, or NULL for an empty one. Borrowed, as DataMapValue: NULL in a default,
     *  the instance's own from Properties_Get, copied by Properties_Set. */
    const PropertyFloatList* FloatListValue;
    /** @brief PropertyType_EnumSet: the mask (bit N: the entry of value N). */
    uint32_t EnumSetValue;
} PropertyData;

/**
 * @brief A typed property value: the type tag and the data it selects. A plain value type; build one with the
 *        PropertyValue_From* helpers. String data is borrowed (see PropertyData.StringValue).
 */
typedef struct PropertyValueStruct
{
    /** @brief The kind of value; selects the active member of Data. */
    PropertyType Type;
    /** @brief The value. */
    PropertyData Data;
} PropertyValue;

/**
 * @brief The rules a property's value must follow, beyond its type's own (floats finite, render colors in range,
 *        strings valid UTF-8, asset names valid, enums in their table).
 *
 * Build with the PROPERTY_RULES_* macros. Bounds apply to Int32, UInt32, Float, Double, Angle (radians), to every
 * component of Vector2, Vector3 and AngleVector3 (radians) and to every value of a FloatList; the string rules to String
 * and AssetName; MaxCount and MaxLength (bytes) to a DataMap; MaxCount and IsStrictlyIncreasing to a FloatList.
 * PropertyTable_Check rejects rules that do not fit the type.
 */
typedef struct PropertyRulesStruct
{
    /** @brief Whether Minimum applies. */
    bool HasMinimum;
    /** @brief When true the value must be > Minimum, otherwise >= Minimum. */
    bool IsMinimumExclusive;
    /** @brief The lower bound (finite; within the float range for the float types; a whole number in range for the
     *  integer types). */
    double Minimum;
    /** @brief Whether Maximum applies. */
    bool HasMaximum;
    /** @brief When true the value must be < Maximum, otherwise <= Maximum. */
    bool IsMaximumExclusive;
    /** @brief The upper bound (finite, not below Minimum; a whole number in range for the integer types). */
    double Maximum;
    /** @brief Strings: the most bytes allowed, without the terminator. Required (> 0) for String and AssetName, so
     *  content from other players cannot grow a save without bound. DataMap: the most bytes its entries may count
     *  (DataMap_GetByteSize); required too. */
    size_t MaxLength;
    /** @brief DataMap: the most entries, tags included; FloatList: the most values (at most
     *  PROPERTY_FLOAT_LIST_MAX_COUNT). Required (> 0) for both; must be 0 for every other type. */
    size_t MaxCount;
    /** @brief Strings: whether NULL is a valid value. Such a property must default to NULL (GHDF has no null, so NULL
     *  is written as an absent entry, which reads back as the default). */
    bool IsNullAllowed;
    /** @brief FloatList only: whether every value must be above the one before it (strictly increasing). */
    bool IsStrictlyIncreasing;
} PropertyRules;

/**
 * @brief One named value of an enum property.
 */
typedef struct PropertyEnumEntryStruct
{
    /** @brief The name written in JSON: snake_case ([a-z][a-z0-9_]*), unique within its table. */
    const unsigned char* Name;
    /** @brief The C enum value; also the value saved in GHDF, so persisted enums give their constants explicit values
     *  and never renumber them. Unique within its table and within the field's storage (0..255 for 1 byte, 0..65535
     *  for 2 bytes). */
    int32_t Value;
} PropertyEnumEntry;

/**
 * @brief The values of an enum property type, shared by every descriptor of that enum.
 */
typedef struct PropertyEnumTableStruct
{
    /** @brief The enum's name for messages and tooling, e.g. u8"shadow_tier". */
    const unsigned char* Name;
    /** @brief The entries; must not be NULL. */
    const PropertyEnumEntry* Entries;
    /** @brief The number of entries; at least 1. */
    size_t EntryCount;
} PropertyEnumTable;

/**
 * @brief Describes one property of a data type: one field of its struct.
 *
 * Declared in a static array next to the struct, typically as
 * `{ .Key = u8"light_pollution", .WireId = 26, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldEnvironment,
 * LightPollution), .Default.FloatValue = 0.35f, .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags =
 * PROPERTY_FLAGS_SETTING, ... }`. Every string is static UTF-8 that outlives the table.
 */
typedef struct PropertyDescriptorStruct
{
    /** @brief The key: snake_case ([a-z][a-z0-9_]*), unique within its table, the JSON key and the name scripts and
     *  Properties_Get/Set use. */
    const unsigned char* Key;
    /** @brief The GHDF entry id: not 0, unique within its table and never reused (a removed property's id goes into
     *  the table's RetiredWireIds). */
    uint16_t WireId;
    /** @brief The value type. */
    PropertyType Type;
    /** @brief The field's offset in the struct (offsetof; set with PROPERTY_MEMBER). */
    size_t Offset;
    /** @brief The field's size (sizeof; set with PROPERTY_MEMBER); PropertyTable_Check compares it with the type's. */
    size_t Size;
    /** @brief The C member's name, for messages that C callers recognise (set with PROPERTY_MEMBER). */
    const unsigned char* MemberName;
    /** @brief The default value (the member of the type); it must pass the rules. A String or AssetName default is a
     *  static string, cloned into each instance. */
    PropertyData Default;
    /** @brief The rules beyond the type's own. */
    PropertyRules Rules;
    /** @brief The enum's values; required for PropertyType_Enum and PropertyType_EnumSet (whose entries' values are bit
     *  indices, 0 to 31), NULL for every other type. */
    const PropertyEnumTable* EnumTable;
    /** @brief PropertyFlags bits. */
    uint32_t Flags;
    /** @brief The inspector's label, e.g. u8"Light pollution"; may be NULL. */
    const unsigned char* DisplayName;
    /** @brief One or two sentences for the inspector's tooltip and the scripting autocomplete; may be NULL. */
    const unsigned char* Description;
    /** @brief The inspector's group, e.g. u8"Night sky"; may be NULL. */
    const unsigned char* Category;
    /** @brief The table Version that added the property, in [1, the table's Version]. */
    uint32_t SinceVersion;
    /** @brief PropertyType_DataMap only (NULL for every other type): the JSON key the map's tags are written under,
     *  next to Key, which holds its values; snake_case and unique among the table's keys, e.g. u8"tags". */
    const unsigned char* TagsKey;
} PropertyDescriptor;

/**
 * @brief Called after a property of an instance changed through this module.
 *
 * Objects use it to advance a revision number, so caches notice the change.
 * @param instance The changed instance.
 * @param descriptor The property that changed after Properties_Set; NULL after Properties_ReadJSON,
 *        Properties_ReadGHDF or Properties_CopyTo, which may have changed any property.
 */
typedef void (*PropertyAfterSetFunction)(void* instance, const PropertyDescriptor* descriptor);

/**
 * @brief Upgrades JSON data written by an older table version, before it is read.
 *
 * Runs on the raw compound (rename keys, convert values, drop removed keys); the read then sees the current format.
 * @param fromVersion The version the data was written with; lower than the table's Version.
 * @param compound The data, changed in place.
 * @param pool The pool owning @p compound, for any new values; may be NULL if the read options gave none.
 * @returns Success, or an error that fails the read.
 */
typedef Error (*PropertyMigrateFunction)(uint32_t fromVersion, JSONCompound* compound, JSONObjectPool* pool);

/**
 * @brief The property table of one data type: its descriptors plus what the table needs as a whole.
 *
 * A static constant next to the struct, handed out by the type (e.g. WorldEnvironment_GetPropertyTable). Immutable.
 */
typedef struct PropertyTableStruct
{
    /** @brief The table's name, snake_case, e.g. u8"world_environment"; messages start with it. */
    const unsigned char* Name;
    /** @brief The current format version of the type's data, at least 1. Raise it with a Migrate step when a change
     *  needs old data converted. */
    uint32_t Version;
    /** @brief sizeof the struct; every field must lie inside it. */
    size_t StructSize;
    /** @brief The descriptors, in display order (the inspector's and the JSON writer's order); must not be NULL. */
    const PropertyDescriptor* Descriptors;
    /** @brief The number of descriptors; at least 1. */
    size_t DescriptorCount;
    /** @brief Wire ids of removed properties, never to be used again; may be NULL when RetiredWireIdCount is 0. */
    const uint16_t* RetiredWireIds;
    /** @brief The number of retired wire ids. */
    size_t RetiredWireIdCount;
    /** @brief Optional hook called after changes (see PropertyAfterSetFunction); may be NULL. */
    PropertyAfterSetFunction AfterSet;
    /** @brief Optional JSON migration for data older than Version (see PropertyMigrateFunction); may be NULL. */
    PropertyMigrateFunction Migrate;
} PropertyTable;

/**
 * @brief What a JSON read does with keys its table does not know (and with keys of runtime-only properties).
 */
typedef enum PropertyUnknownKeyModeEnum
{
    /** @brief Fail the read (strict: editor-made files, tests). */
    PropertyUnknownKeyMode_Error,
    /** @brief Log one warning per read naming the unknown keys, and read the rest. */
    PropertyUnknownKeyMode_Warn,
    /** @brief Skip them silently (data from a newer game version). */
    PropertyUnknownKeyMode_Ignore
} PropertyUnknownKeyMode;

/**
 * @brief Options of Properties_ReadJSON and Properties_ReadGHDF. Build with PropertyReadOptions_CreateDefault.
 */
typedef struct PropertyReadOptionsStruct
{
    /** @brief JSON only: what unknown keys do (GHDF always skips unknown ids, for forward compatibility). */
    PropertyUnknownKeyMode UnknownKeyMode;
    /** @brief Borrowed logger for PropertyUnknownKeyMode_Warn; with NULL, Warn behaves like Ignore. */
    Logger* Logger;
    /** @brief The table version the data was written with; 0 means the current version. JSON data from an older
     *  version runs the table's Migrate first. */
    uint32_t DataVersion;
    /** @brief Borrowed pool owning the JSON data, handed to Migrate; may be NULL when no migration needs one. */
    JSONObjectPool* Pool;
    /** @brief Borrowed key registry the keys of DataMap properties are interned in; required to read a DataMap that
     *  is not empty (the read fails without it). */
    KeyRegistry* Keys;
    /** @brief The namespace JSON keys written without one get (the story or pack id; NamespacedKey.h); NULL means
     *  NAMESPACED_KEY_DEFAULT_NAMESPACE. GHDF always holds full keys. */
    const unsigned char* DefaultNamespace;
} PropertyReadOptions;

/**
 * @brief Options of Properties_WriteJSON and Properties_WriteGHDF. Build with PropertyWriteOptions_CreateDefault.
 */
typedef struct PropertyWriteOptionsStruct
{
    /** @brief When false (the default) values equal to their default are left out, so authored files stay short and
     *  pick up changed defaults; when true every persistent property is written (saves that must keep their exact
     *  state). A NULL string is never written to GHDF either way (absent reads back as the NULL default). */
    bool AreDefaultsWritten;
} PropertyWriteOptions;


// Functions: values.
/**
 * @brief Default read options: unknown JSON keys fail the read, no logger, current data version, no pool, no key
 *        registry (set Keys to read data maps), the default namespace.
 * @returns The options.
 */
static inline PropertyReadOptions PropertyReadOptions_CreateDefault(void)
{
    return (PropertyReadOptions)
    {
        .UnknownKeyMode = PropertyUnknownKeyMode_Error,
        .Logger = NULL,
        .DataVersion = 0U,
        .Pool = NULL,
        .Keys = NULL,
        .DefaultNamespace = NULL
    };
}

/**
 * @brief Default write options: values equal to their defaults are left out.
 * @returns The options.
 */
static inline PropertyWriteOptions PropertyWriteOptions_CreateDefault(void)
{
    return (PropertyWriteOptions){ .AreDefaultsWritten = false };
}

/** @brief Makes a Bool value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromBool(bool value)
{
    return (PropertyValue){ .Type = PropertyType_Bool, .Data.BoolValue = value };
}

/** @brief Makes an Int32 value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromInt32(int32_t value)
{
    return (PropertyValue){ .Type = PropertyType_Int32, .Data.Int32Value = value };
}

/** @brief Makes a UInt32 value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromUInt32(uint32_t value)
{
    return (PropertyValue){ .Type = PropertyType_UInt32, .Data.UInt32Value = value };
}

/** @brief Makes a UInt64 value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromUInt64(uint64_t value)
{
    return (PropertyValue){ .Type = PropertyType_UInt64, .Data.UInt64Value = value };
}

/** @brief Makes a Float value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromFloat(float value)
{
    return (PropertyValue){ .Type = PropertyType_Float, .Data.FloatValue = value };
}

/** @brief Makes a Double value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromDouble(double value)
{
    return (PropertyValue){ .Type = PropertyType_Double, .Data.DoubleValue = value };
}

/** @brief Makes an Angle value. @param radians The angle in radians. @returns The typed value. */
static inline PropertyValue PropertyValue_FromAngle(float radians)
{
    return (PropertyValue){ .Type = PropertyType_Angle, .Data.FloatValue = radians };
}

/** @brief Makes a Vector2 value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromVector2(Vector2 value)
{
    return (PropertyValue){ .Type = PropertyType_Vector2, .Data.Vector2Value = value };
}

/** @brief Makes a Vector3 value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromVector3(Vector3 value)
{
    return (PropertyValue){ .Type = PropertyType_Vector3, .Data.Vector3Value = value };
}

/** @brief Makes an AngleVector3 value. @param radians The angles in radians. @returns The typed value. */
static inline PropertyValue PropertyValue_FromAngleVector3(Vector3 radians)
{
    return (PropertyValue){ .Type = PropertyType_AngleVector3, .Data.Vector3Value = radians };
}

/** @brief Makes a Color value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromColor(Color value)
{
    return (PropertyValue){ .Type = PropertyType_Color, .Data.ColorValue = value };
}

/** @brief Makes a RenderColor value. @param value The value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromRenderColor(RenderColor value)
{
    return (PropertyValue){ .Type = PropertyType_RenderColor, .Data.RenderColorValue = value };
}

/** @brief Makes a String value. @param value The string (borrowed) or NULL. @returns The typed value. */
static inline PropertyValue PropertyValue_FromString(const unsigned char* value)
{
    return (PropertyValue){ .Type = PropertyType_String, .Data.StringValue = value };
}

/** @brief Makes an AssetName value. @param value The name (borrowed) or NULL. @returns The typed value. */
static inline PropertyValue PropertyValue_FromAssetName(const unsigned char* value)
{
    return (PropertyValue){ .Type = PropertyType_AssetName, .Data.StringValue = value };
}

/** @brief Makes an Enum value. @param value The entry's value. @returns The typed value. */
static inline PropertyValue PropertyValue_FromEnum(int32_t value)
{
    return (PropertyValue){ .Type = PropertyType_Enum, .Data.EnumValue = value };
}

/** @brief Makes an ObjectId value. @param value The object id (0 = none). @returns The typed value. */
static inline PropertyValue PropertyValue_FromObjectId(uint64_t value)
{
    return (PropertyValue){ .Type = PropertyType_ObjectId, .Data.ObjectIdValue = value };
}

/** @brief Makes a Date value. @param dayNumber The day number (days since 0001-01-01). @returns The typed value. */
static inline PropertyValue PropertyValue_FromDate(int64_t dayNumber)
{
    return (PropertyValue){ .Type = PropertyType_Date, .Data.DateValue = dayNumber };
}

/** @brief Makes a DataMap value. @param map The map (borrowed; Properties_Set copies it), or NULL for an empty one.
 *  @returns The typed value. */
static inline PropertyValue PropertyValue_FromDataMap(const DataMap* map)
{
    return (PropertyValue){ .Type = PropertyType_DataMap, .Data.DataMapValue = map };
}

/** @brief Makes a FloatList value. @param list The list (borrowed; Properties_Set copies it), or NULL for an empty one.
 *  @returns The typed value. */
static inline PropertyValue PropertyValue_FromFloatList(const PropertyFloatList* list)
{
    return (PropertyValue){ .Type = PropertyType_FloatList, .Data.FloatListValue = list };
}

/** @brief Makes an EnumSet value. @param mask The set (bit N: the enum entry of value N). @returns The typed value. */
static inline PropertyValue PropertyValue_FromEnumSet(uint32_t mask)
{
    return (PropertyValue){ .Type = PropertyType_EnumSet, .Data.EnumSetValue = mask };
}

/**
 * @brief Returns a property type's name for messages and tooling, e.g. u8"float" or u8"render_color".
 * @param type The type.
 * @returns A static string; u8"unknown" for a value outside PropertyType.
 */
const unsigned char* PropertyType_GetName(PropertyType type);


// Functions: float lists.
/**
 * @brief Creates a float list holding a copy of @p count values (one allocation).
 *
 * The values are copied as they are; whether they suit a property (finite, bounded, rising) is that property's rules'
 * business (Properties_CheckValue).
 * @param values The values; may be NULL only when @p count is 0.
 * @param count The number of values, 0 to PROPERTY_FLOAT_LIST_MAX_COUNT; 0 gives the empty list, NULL.
 * @param outList [out] Receives the new list (the caller owns it: PropertyFloatList_Destroy), or NULL for an empty one.
 *        Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p outList is NULL, or @p values is NULL with a count above 0;
 *          ErrorCode_ArgumentOutOfRange for a count above PROPERTY_FLOAT_LIST_MAX_COUNT (*outList is NULL then).
 */
Error PropertyFloatList_Create(const float* values, size_t count, PropertyFloatList** outList);

/**
 * @brief Releases a float list.
 * @param list The list, or NULL (nothing happens).
 */
void PropertyFloatList_Destroy(PropertyFloatList* list);

/**
 * @brief Copies a float list.
 * @param list The list to copy; NULL (the empty list) gives NULL.
 * @returns The copy, owned by the caller (PropertyFloatList_Destroy).
 */
PropertyFloatList* PropertyFloatList_Clone(const PropertyFloatList* list);

/**
 * @brief Reports whether two float lists hold the same values in the same order (compared by value: 0 equals -0).
 * @param a The first list; NULL is the empty list.
 * @param b The second list; NULL is the empty list.
 * @returns true when they are equal.
 */
bool PropertyFloatList_Equals(const PropertyFloatList* a, const PropertyFloatList* b);

/**
 * @brief Returns how many values a float list holds.
 * @param list The list; NULL is the empty list.
 * @returns The count (0 for NULL).
 */
static inline size_t PropertyFloatList_GetCount(const PropertyFloatList* list)
{
    return (list != NULL) ? list->_count : 0U;
}

/**
 * @brief Returns a float list's values.
 * @param list The list; NULL is the empty list.
 * @returns The values (borrowed: valid while the list is), PropertyFloatList_GetCount of them; NULL for NULL.
 */
static inline const float* PropertyFloatList_GetValues(const PropertyFloatList* list)
{
    return (list != NULL) ? list->_values : NULL;
}


// Functions: tables.
/**
 * @brief Checks a table against every rule a table must follow.
 *
 * Checked: a valid name and a version of at least 1; at least one descriptor; every key snake_case and unique; every
 * wire id non-zero, unique and not retired; retired ids non-zero and unique; every field inside StructSize with the
 * size its type needs (Enum: 1, 2 or 4 bytes), and no two fields overlapping; rules that fit the type (bounds only on
 * bounded numeric types and float lists, finite and ordered, whole and in range for integers; MaxLength on every string;
 * MaxCount and MaxLength on every DataMap; MaxCount, at most PROPERTY_FLOAT_LIST_MAX_COUNT, on every FloatList, and
 * IsStrictlyIncreasing only there); a TagsKey exactly on DataMaps, snake_case and unique among the keys; an enum table
 * exactly on enums, with snake_case unique names and unique values that fit the field; only known flag bits;
 * SinceVersion in [1, Version]; override flags (PROPERTY_OVERRIDE_FLAG_PREFIX) that are Bools naming another row; and
 * every default passing its own rules (a DataMap's and a FloatList's is NULL). Run it from a test for every table in the
 * game.
 * @param table The table; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p table is NULL or breaks a rule (the message names the table, the
 *          key and the rule).
 */
Error PropertyTable_Check(const PropertyTable* table);

/**
 * @brief Finds a table's descriptor by key.
 *
 * A linear scan over the descriptors (tables are small); resolve once and keep the pointer when calling often.
 * @param table The table; must not be NULL.
 * @param key The key; NULL finds nothing.
 * @returns The descriptor (inside @p table), or NULL when the table has no such key.
 */
const PropertyDescriptor* PropertyTable_FindByKey(const PropertyTable* table, const unsigned char* key);


// Functions: instances.
/**
 * @brief Constructs an instance: zeroes its StructSize bytes, then writes every descriptor's default (strings
 *        cloned).
 *
 * Call it on new memory only: whatever the instance owned before is not released. Pair it with
 * Properties_Deconstruct. Fields the table does not describe are left zero, for the type's own constructor.
 * @param table The table; must not be NULL.
 * @param instance The struct to construct (at least StructSize bytes); must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL.
 */
Error Properties_Construct(const PropertyTable* table, void* instance);

/**
 * @brief Releases what an instance owns (its String, AssetName, DataMap and FloatList fields, which become NULL).
 *
 * Other fields are untouched. Safe on a constructed instance, also more than once; NULL arguments do nothing.
 * @param table The table, or NULL.
 * @param instance The instance, or NULL.
 */
void Properties_Deconstruct(const PropertyTable* table, void* instance);

/**
 * @brief Checks every described field of an instance against its type and rules.
 *
 * Use it on data from outside the program before using it; the readers already do. The first broken property fails,
 * e.g. "world_environment.light_pollution (LightPollution) must be finite and in [0, 1]."
 * @param table The table; must not be NULL.
 * @param instance The instance; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_ArgumentOutOfRange naming the table,
 *          the key, the C member and the rule of the first broken property.
 */
Error Properties_Validate(const PropertyTable* table, const void* instance);

/**
 * @brief Checks one value against a property's type and rules, without writing anything.
 *
 * Hand-written setters call this so their rules live in the table only.
 * @param table The table @p descriptor belongs to (for the message); must not be NULL.
 * @param descriptor The property; must not be NULL.
 * @param value The value; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL or @p value has another type than the property;
 *          ErrorCode_ArgumentOutOfRange naming the table, the key, the C member and the broken rule.
 */
Error Properties_CheckValue(const PropertyTable* table, const PropertyDescriptor* descriptor, const PropertyValue* value);

/**
 * @brief Deep-copies every described field of one instance into another (strings, data maps and float lists cloned,
 *        the destination's old ones released), then calls the table's AfterSet with a NULL descriptor on the
 *        destination.
 *
 * Fields the table does not describe are untouched. Copying an instance onto itself does nothing.
 * @param table The table; must not be NULL.
 * @param source The instance to copy; must not be NULL.
 * @param destination A constructed instance to overwrite; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL.
 */
Error Properties_CopyTo(const PropertyTable* table, const void* source, void* destination);

/**
 * @brief Reports whether two instances hold the same value in every described field.
 *
 * Floats compare by value (0 equals -0), strings by bytes (two NULLs are equal), a RenderColor without its tint's
 * alpha. Fields the table does not describe are not compared.
 * @param table The table; must not be NULL.
 * @param a The first instance; must not be NULL.
 * @param b The second instance; must not be NULL.
 * @returns true if every described field is equal; false otherwise or if an argument is NULL.
 */
bool Properties_Equals(const PropertyTable* table, const void* a, const void* b);

/**
 * @brief Reads one property of an instance by key.
 * @param table The table; must not be NULL.
 * @param instance The instance; must not be NULL.
 * @param key The property's key; must not be NULL.
 * @param outValue [out] Receives the typed value; a string points into the instance (valid until that property
 *        changes or the instance is deconstructed). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL or the table has no such key.
 */
Error Properties_Get(const PropertyTable* table, const void* instance, const unsigned char* key, PropertyValue* outValue);

/**
 * @brief Writes one property of an instance by key, after checking the value; then calls the table's AfterSet.
 *
 * A string value is cloned (the instance's old string is released). Nothing changes on failure.
 * @param table The table; must not be NULL.
 * @param instance The instance; must not be NULL.
 * @param key The property's key; must not be NULL.
 * @param value The new value, of the property's type; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL, the table has no such key or the value has
 *          another type; ErrorCode_InvalidOperation if the property is ReadOnly; ErrorCode_ArgumentOutOfRange if the
 *          value breaks a rule (see Properties_CheckValue).
 */
Error Properties_Set(const PropertyTable* table, void* instance, const unsigned char* key, const PropertyValue* value);


// Functions: JSON.
/**
 * @brief Finds the persistent property a JSON key belongs to: the descriptor whose Key it is, or the data map whose
 *        TagsKey it is.
 *
 * Exactly the keys Properties_ReadJSON reads; any other key (also the key of a property that is not persistent) is one
 * it calls unknown. A loader that reads many instances can use it to report unknown keys its own way (once per name
 * instead of once per read). A linear scan over the descriptors.
 * @param table The table; must not be NULL.
 * @param key The JSON key; NULL finds nothing.
 * @returns The descriptor (inside @p table), or NULL when the reader would call the key unknown.
 */
const PropertyDescriptor* PropertyTable_FindByJSONKey(const PropertyTable* table, const unsigned char* key);

/**
 * @brief Writes an instance's persistent properties into a JSON compound, in table order.
 *
 * Validates the instance first, so invalid data is never written. By default values equal to their default are left
 * out (see PropertyWriteOptions). Keys already in @p compound with a property's key are overwritten in place; other keys
 * stay. WRJSON compounds keep insertion order, so the new keys come out in table order, after the keys already there.
 * @param table The table; must not be NULL.
 * @param instance The instance; must not be NULL.
 * @param pool The pool owning @p compound, which every written value is borrowed from; must not be NULL.
 * @param compound The compound to write into; must not be NULL.
 * @param options The options, or NULL for PropertyWriteOptions_CreateDefault.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; the validation error
 *          (ErrorCode_ArgumentOutOfRange) for an invalid instance. On a failure after validation the compound may
 *          hold some of the properties: discard it.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error Properties_WriteJSON(const PropertyTable* table, const void* instance, JSONObjectPool* pool, JSONCompound* compound,
    const PropertyWriteOptions* options);

/**
 * @brief Reads an instance's persistent properties from a JSON compound: all or nothing.
 *
 * Every persistent property is taken from its key, or gets its default when the key is absent; properties that are
 * not persistent keep their current values. A DataMap is read from its two keys (its TagsKey and its Key) with the
 * options' Keys and DefaultNamespace. The read works on a scratch copy: after every key parsed and the whole
 * result validated it is committed into @p instance (and the table's AfterSet called with a NULL descriptor). On any
 * failure @p instance is unchanged, and the options' Keys forget the keys the read interned (KeyRegistry_TruncateTo).
 * Unknown keys follow the options. When the options' DataVersion is older than the table's Version, the table's
 * Migrate runs first and changes @p compound in place.
 * @param table The table; must not be NULL.
 * @param compound The data; must not be NULL.
 * @param instance A constructed instance to fill; must not be NULL.
 * @param options The options, or NULL for PropertyReadOptions_CreateDefault.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_InvalidJSON for a value of the wrong
 *          shape, a value breaking its rules or an unknown key in Error mode (the message names the table and the
 *          key); a Migrate error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error Properties_ReadJSON(const PropertyTable* table, JSONCompound* compound, void* instance,
    const PropertyReadOptions* options);


// Functions: GHDF.
/**
 * @brief Writes an instance's persistent properties into a GHDF compound, keyed by their wire ids.
 *
 * Validates the instance first. By default values equal to their default are left out (see PropertyWriteOptions;
 * saves that must match the old world encoder write them). A NULL string is never written. Entries already in
 * @p compound under a property's wire id are overwritten; other entries stay.
 * @param table The table; must not be NULL.
 * @param instance The instance; must not be NULL.
 * @param pool The pool every written string, array and compound is borrowed from; must not be NULL.
 * @param compound The compound to write into; must not be NULL.
 * @param options The options, or NULL for PropertyWriteOptions_CreateDefault.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; the validation error
 *          (ErrorCode_ArgumentOutOfRange) for an invalid instance; ErrorCode_EncodeError if a string could not be
 *          built. On a failure after validation the compound may hold some of the properties: discard it.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error Properties_WriteGHDF(const PropertyTable* table, const void* instance, GHDFObjectPool* pool, GHDFCompound* compound,
    const PropertyWriteOptions* options);

/**
 * @brief Reads an instance's persistent properties from a GHDF compound: all or nothing.
 *
 * Like Properties_ReadJSON: absent wire ids give the defaults, non-persistent properties keep their values, the
 * result is validated before it is committed and AfterSet is called with a NULL descriptor, and a failure leaves the
 * instance and the options' Keys as they were. Entries with ids the table does not know (or has retired) are skipped,
 * for forward compatibility. The options' UnknownKeyMode, Logger and Pool are JSON-only, and there is no GHDF
 * migration yet, so DataVersion is not used either; the parameter is there for when one is needed.
 * @param table The table; must not be NULL.
 * @param compound The data; must not be NULL.
 * @param instance A constructed instance to fill; must not be NULL.
 * @param options The options, or NULL for PropertyReadOptions_CreateDefault.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_Deserialize for an entry of the wrong
 *          type or shape, or a value breaking its rules (the message names the table and the key).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error Properties_ReadGHDF(const PropertyTable* table, GHDFCompound* compound, void* instance,
    const PropertyReadOptions* options);
