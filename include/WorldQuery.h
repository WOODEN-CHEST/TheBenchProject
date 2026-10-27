#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
// Vector3Double comes from WRVectorMath: the query's origin and box are positions, in double like the objects'.
#include "wr/WRVectorMath.h"
#include "DataValue.h"
#include "World.h"
#include "WorldComponent.h"


/**
 * @file WorldQuery.h
 * @brief Searches over a world's objects by their tags, data, components, flags and place: the engine side of
 *        selectors.
 *
 * A WorldQuery holds optional filters, combined with AND: tags (all of, any of, none of), data keys present, data
 * comparisons, component types present, enabled and visible only (effectively: a disabled or hidden ancestor counts),
 * within a distance of a point, inside a box (both by the object's world position, through its parents). It also
 * holds the order (object ids ascending, or nearest first) and a limit. World_Query appends the ids of the matching
 * objects. Start from WorldQuery_Create (no filter: every object) and set what is needed; the arrays are borrowed for
 * the call.
 *
 * SELECTORS. There is no string syntax here. A selector such as `@objects[tag=town:street_lamp, data.town:power>3,
 * distance<20]` belongs to the scripting language, which compiles it to a WorldQuery. Scripts' searches set
 * IsEnabledOnly, since a disabled object is skipped by every system; the editor leaves it off to find everything.
 *
 * DATA COMPARISONS. A comparison needs its key present on the object. Equal and NotEqual compare numbers by value
 * across int and float (3 equals 3.0) and every other type exactly (DataValue_Equals). Less, LessOrEqual, Greater and
 * GreaterOrEqual compare numbers (int and float, by value) and date_times; a value of another type never matches them.
 *
 * SPEED. The world keeps a tag index (tag -> object ids), so a query with an all-of or any-of tag visits only the
 * objects carrying those tags; other queries scan every object, which is fine for hundreds of objects.
 */


// Types.
/**
 * @brief How a data comparison compares the object's value with the query's.
 */
typedef enum WorldDataOperatorEnum
{
    /** @brief The value equals the query's (numbers by value across int and float). */
    WorldDataOperator_Equal,
    /** @brief The value differs from the query's. */
    WorldDataOperator_NotEqual,
    /** @brief The value is below the query's (numbers and date_times). */
    WorldDataOperator_Less,
    /** @brief The value is at most the query's (numbers and date_times). */
    WorldDataOperator_LessOrEqual,
    /** @brief The value is above the query's (numbers and date_times). */
    WorldDataOperator_Greater,
    /** @brief The value is at least the query's (numbers and date_times). */
    WorldDataOperator_GreaterOrEqual
} WorldDataOperator;

/**
 * @brief One data comparison: the object's value under Key, Operator, Value.
 */
typedef struct WorldDataComparisonStruct
{
    /** @brief The key (an id of the world's key registry); the object must hold it. */
    uint32_t Key;
    /** @brief How to compare. */
    WorldDataOperator Operator;
    /** @brief The value to compare with; borrowed (the caller owns it). An int, float or date_time for the ordering
     *  operators. */
    DataValue Value;
} WorldDataComparison;

/**
 * @brief A search over a world's objects: filters combined with AND, an order and a limit. A plain value type; every
 *        pointer is borrowed for the World_Query call. An array may be NULL only when its count is 0.
 */
typedef struct WorldQueryStruct
{
    /** @brief Tags the object must all carry. */
    const uint32_t* AllOfTags;
    /** @brief The number of AllOfTags. */
    size_t AllOfTagCount;
    /** @brief Tags of which the object must carry at least one (no filter when the count is 0). */
    const uint32_t* AnyOfTags;
    /** @brief The number of AnyOfTags. */
    size_t AnyOfTagCount;
    /** @brief Tags the object must not carry. */
    const uint32_t* NoneOfTags;
    /** @brief The number of NoneOfTags. */
    size_t NoneOfTagCount;
    /** @brief Data keys the object must hold, of any type (a tag included). */
    const uint32_t* DataKeys;
    /** @brief The number of DataKeys. */
    size_t DataKeyCount;
    /** @brief Data comparisons that must all hold. */
    const WorldDataComparison* Comparisons;
    /** @brief The number of Comparisons. */
    size_t ComparisonCount;
    /** @brief Component types the object must all have. */
    const WorldComponentType* ComponentTypes;
    /** @brief The number of ComponentTypes. */
    size_t ComponentTypeCount;
    /** @brief Only enabled objects whose ancestors are all enabled too (WorldObject_IsEffectivelyEnabled; scripts'
     *  searches set it). */
    bool IsEnabledOnly;
    /** @brief Only visible objects whose ancestors are all visible too (WorldObject_IsEffectivelyVisible). */
    bool IsVisibleOnly;
    /** @brief The point distances are measured from (finite), in double like the objects' positions. */
    Vector3Double Origin;
    /** @brief Whether MaximumDistance applies. */
    bool HasMaximumDistance;
    /** @brief The farthest an object's world position may be from Origin (finite, at least 0); compared in double. */
    float MaximumDistance;
    /** @brief Whether the box applies. */
    bool HasBox;
    /** @brief The box's lowest corner (finite, every component at most BoxMaximum's), in double. */
    Vector3Double BoxMinimum;
    /** @brief The box's highest corner, in double; world positions on the faces are inside. */
    Vector3Double BoxMaximum;
    /** @brief true: nearest to Origin first (ties by id); false: object ids ascending. */
    bool IsSortedByDistance;
    /** @brief The most ids to give, after ordering; 0 for no limit. */
    size_t Limit;
} WorldQuery;


// Functions.
/**
 * @brief Makes a query without any filter (it matches every object), ordered by id, without a limit.
 * @returns The query.
 */
static inline WorldQuery WorldQuery_Create(void)
{
    return (WorldQuery){ .AllOfTagCount = 0U };
}

/**
 * @brief Appends the ids of the objects that match a query, in its order and within its limit.
 *
 * Objects are placed by their world position (WorldObject_GetWorldPosition, through their parents). @p outIds is not
 * cleared.
 * @param self The world; must not be NULL.
 * @param query The query; must not be NULL.
 * @param outIds A buffer of uint64_t to append the ids to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL, @p outIds does not hold uint64_t, or the query
 *          is malformed (a NULL array with a count, a key id of 0, an ordering operator on a value that is neither a
 *          number nor a date_time, an invalid comparison value, a component type the world does not have, a
 *          non-finite origin, a negative distance, a box whose minimum passes its maximum); ErrorCode_BufferTooSmall
 *          if @p outIds could not grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error World_Query(World* self, const WorldQuery* query, GenericBuffer* outIds);

/**
 * @brief Reports whether one object matches a query's filters (its order and limit do not apply).
 *
 * For "does this object match the selector": a trigger checking who entered it, a script testing an object.
 * @param self The world; must not be NULL.
 * @param query The query; must not be NULL.
 * @param id The object's id.
 * @param outIsMatch [out] Receives whether the object exists and matches. Must not be NULL.
 * @returns Success; the errors of World_Query for a malformed query.
 */
Error World_DoesObjectMatch(World* self, const WorldQuery* query, uint64_t id, bool* outIsMatch);
