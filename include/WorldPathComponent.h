#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRVectorMath.h"
#include "PathCurve.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"
#include "WorldObject.h"


/**
 * @file WorldPathComponent.h
 * @brief The path component: a line through nodes, any length, that bends. Fences, hedges, roads, kerbs, streams and rows
 *        of lamps read it; a closed one is also a region, an area for scattering, painting and scripts.
 *
 * The component holds raw geometry only: its nodes (relative to its object), whether it closes on itself, how its frames
 * find their up and how finely readers sample it by default. Nothing about the ground: each reader decides whether and how
 * it snaps to terrain (PathGround.h), so two generators on one path can differ, and only a node's section settings can say
 * otherwise for one stretch (a bridge). The maths is PathCurve.h, and WorldPathComponent_GetCurve hands out the curve,
 * rebuilt whenever a row that shapes it changes, however it was changed, so it is always valid and read-only code (the
 * renderer's debug view) never builds anything.
 *
 * READERS. A generator reads the nearest path at or above itself in the hierarchy (WorldPathComponent_FindForObject): on
 * its own object for a lone fence, on a parent for a road's parts. It rebuilds what it made when the path changes, never
 * only at load, by keeping a WorldPathWatch and asking it once a tick.
 *
 * DISTANCES are in the object's own space, as everything the curve says is: a scaled object scales them too, along with
 * whatever is generated in that space. The World* functions below give positions and directions in the world, in double.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Path, &component); reach it with
 * WorldPathComponent_FromComponent. Its properties are the rows of its table ("path_component"); a node's are the rows of
 * its own table ("path_node", WorldPathComponent_GetNodePropertyTable), so the editor can show one like any other row set.
 * Every change advances the component's revision (WorldComponent_GetRevision).
 */


// Macros.
/** @brief The most nodes a path holds. */
#define WORLD_PATH_MAX_NODES PATH_CURVE_MAX_NODES
/** @brief The largest width or scale factor a node may give. */
#define WORLD_PATH_NODE_MAX_FACTOR 1000000.0f
/** @brief The sample spacing a new path has, in metres. */
#define WORLD_PATH_DEFAULT_SAMPLE_SPACING 1.0f


// Types.
/**
 * @brief The path component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldPathComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief The nodes (owned; PathNode records, at most WORLD_PATH_MAX_NODES; NULL while it has none). */
    PropertyRecordList* _nodes;
    /** @brief Whether a segment runs from the last node back to the first, which also makes the path a region. */
    bool _isClosed;
    /** @brief How its frames find their up. */
    PathUpMode _upMode;
    /** @brief The most distance between two samples its readers take by default, in metres (above 0). */
    float _sampleSpacing;
    /** @brief The curve through the nodes; runtime, rebuilt whenever a row that shapes it changes. */
    PathCurve _curve;
} WorldPathComponent;

/**
 * @brief What a reader last saw of its path. Zero it (or WorldPathWatch_Create) to start; WorldPathWatch_Update then says
 *        what changed. A plain value type the reader keeps.
 */
typedef struct WorldPathWatchStruct
{
    /** @brief The component it saw (its serial, which no other component of the world ever has); 0 for none. */
    uint64_t _serial;
    /** @brief The component's revision it saw. */
    uint32_t _revision;
    /** @brief The transform revision of the path's object it saw. */
    uint32_t _transformRevision;
} WorldPathWatch;

/**
 * @brief What WorldPathWatch_Update found. A path found, lost or replaced by another component counts as both.
 */
typedef struct WorldPathChangeStruct
{
    /** @brief The path's rows changed: rebuild what was generated from it. */
    bool HasPathChanged;
    /** @brief The path's object moved in the world (itself or through a parent): what was generated moves with it when it
     *  lies under that object, but what was snapped to the ground needs doing again. */
    bool HasPlaceChanged;
} WorldPathChange;


// Functions: the registry.
/**
 * @brief Returns the path component's registry entry (WorldComponentType_Path, "path").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldPathComponent_GetTypeInfo(void);

/**
 * @brief Returns the path component's property table ("path_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldPathComponent_GetPropertyTable(void);

/**
 * @brief Returns the table of one node ("path_node"), the records of its nodes row: what the editor shows for a node.
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldPathComponent_GetNodePropertyTable(void);

/**
 * @brief Downcasts a component to a path component.
 * @param component The component, or NULL.
 * @returns The path component, or NULL when @p component is NULL or of another type.
 */
static inline WorldPathComponent* WorldPathComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Path)) ? (WorldPathComponent*)component : NULL;
}

/**
 * @brief Upcasts a path component to its base.
 * @param self The path component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldPathComponent_AsComponent(WorldPathComponent* self)
{
    return &self->Base;
}


// Functions: the nodes.
/**
 * @brief Returns the nodes.
 * @param self The path component; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The nodes (borrowed: valid until they change), NULL when it has none.
 */
static inline const PathNode* WorldPathComponent_GetNodes(const WorldPathComponent* self, size_t* outCount)
{
    *outCount = PropertyRecordList_GetCount(self->_nodes);
    return PropertyRecordList_GetRecords(self->_nodes);
}

/**
 * @brief Replaces every node, after checking them by the rows' rules. Advances the revision and rebuilds the curve.
 * @param self The path component; must not be NULL.
 * @param nodes The nodes (copied); may be NULL only when @p count is 0.
 * @param count How many, at most WORLD_PATH_MAX_NODES.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_ArgumentOutOfRange naming the node and the rule
 *          it breaks. Nothing changes on failure.
 */
Error WorldPathComponent_SetNodes(WorldPathComponent* self, const PathNode* nodes, size_t count);

/**
 * @brief Replaces one node.
 * @param self The path component; must not be NULL.
 * @param index The node, below the count.
 * @param node The new node (copied); must not be NULL.
 * @returns As WorldPathComponent_SetNodes; also ErrorCode_ArgumentOutOfRange for an index out of range.
 */
Error WorldPathComponent_SetNode(WorldPathComponent* self, size_t index, const PathNode* node);

/**
 * @brief Inserts a node before @p index; the nodes from there on move up.
 * @param self The path component; must not be NULL.
 * @param index Where, at most the count (the count appends).
 * @param node The new node (copied); must not be NULL.
 * @returns As WorldPathComponent_SetNodes; also ErrorCode_ArgumentOutOfRange for an index out of range.
 */
Error WorldPathComponent_InsertNode(WorldPathComponent* self, size_t index, const PathNode* node);

/**
 * @brief Removes one node; the nodes after it move down.
 * @param self The path component; must not be NULL.
 * @param index The node, below the count.
 * @returns As WorldPathComponent_SetNodes; also ErrorCode_ArgumentOutOfRange for an index out of range.
 */
Error WorldPathComponent_RemoveNode(WorldPathComponent* self, size_t index);


// Functions: the settings.
/** @brief Whether the path closes on itself. @param self The path component; must not be NULL. @returns true when closed. */
static inline bool WorldPathComponent_IsClosed(const WorldPathComponent* self)
{
    return self->_isClosed;
}

/**
 * @brief Sets whether the path closes on itself. Advances the revision and rebuilds the curve.
 * @param self The path component; must not be NULL.
 * @param isClosed Whether it closes.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self.
 */
Error WorldPathComponent_SetClosed(WorldPathComponent* self, bool isClosed);

/** @brief How its frames find their up. @param self The path component; must not be NULL. @returns The up mode. */
static inline PathUpMode WorldPathComponent_GetUpMode(const WorldPathComponent* self)
{
    return self->_upMode;
}

/**
 * @brief Sets how its frames find their up. Advances the revision and rebuilds the curve.
 * @param self The path component; must not be NULL.
 * @param upMode The up mode.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for an unknown mode.
 */
Error WorldPathComponent_SetUpMode(WorldPathComponent* self, PathUpMode upMode);

/** @brief The most distance between two samples its readers take by default. @param self The path component; must not be
 *  NULL. @returns The spacing, in metres. */
static inline float WorldPathComponent_GetSampleSpacing(const WorldPathComponent* self)
{
    return self->_sampleSpacing;
}

/**
 * @brief Sets the spacing its readers sample at by default. Advances the revision.
 * @param self The path component; must not be NULL.
 * @param spacing The spacing in metres, above 0.
 * @returns Success; ErrorCode_IllegalArgument for a NULL @p self; ErrorCode_ArgumentOutOfRange for one not above 0.
 */
Error WorldPathComponent_SetSampleSpacing(WorldPathComponent* self, float spacing);

/**
 * @brief The curve through the nodes, in the object's space: always up to date.
 * @param self The path component; must not be NULL.
 * @returns The curve (borrowed: valid while the component is; it changes whenever a row that shapes it does).
 */
static inline const PathCurve* WorldPathComponent_GetCurve(const WorldPathComponent* self)
{
    return &self->_curve;
}


// Functions: finding a path, and following it.
/**
 * @brief The nearest path at or above an object in the hierarchy: its own, or its parent's, and so on up.
 * @param object The object; NULL finds nothing.
 * @returns The path component, or NULL when neither it nor any ancestor has one.
 */
WorldPathComponent* WorldPathComponent_FindForObject(const WorldObject* object);

/**
 * @brief A watch that has seen no path yet.
 * @returns The watch.
 */
static inline WorldPathWatch WorldPathWatch_Create(void)
{
    return (WorldPathWatch){ ._serial = 0U, ._revision = 0U, ._transformRevision = 0U };
}

/**
 * @brief Says what changed since the last call, and remembers the path as it is now.
 *
 * A first call with a path says both changed. With NULL after a path (it was removed, or the reader moved out from under
 * it), both changed; with NULL again, neither.
 * @param self The watch; must not be NULL.
 * @param path The path as found now, or NULL.
 * @returns What changed.
 */
WorldPathChange WorldPathWatch_Update(WorldPathWatch* self, const WorldPathComponent* path);


// Functions: the world.
/**
 * @brief A position of the path's space in the world, in double.
 * @param self The path component; must not be NULL.
 * @param local The position in the object's space.
 * @returns The position in the world.
 */
Vector3Double WorldPathComponent_LocalToWorld(const WorldPathComponent* self, Vector3Double local);

/**
 * @brief A position in the world in the path's space. Exact far from the world's origin (the translation is taken off
 *        before the inverse is applied).
 * @param self The path component; must not be NULL.
 * @param world The position in the world.
 * @param outLocal [out] Receives the position in the object's space. Must not be NULL.
 * @returns true; false for an object scaled flat, which no point of the world can be brought back from.
 */
bool WorldPathComponent_WorldToLocal(const WorldPathComponent* self, Vector3Double world, Vector3Double* outLocal);

/**
 * @brief A point of the curve in the world: its position, and its frame turned (and kept unit and at right angles under
 *        an object's uneven scale). Its distance and values stay as they are.
 * @param self The path component; must not be NULL.
 * @param local The point, from the curve. Must not be NULL.
 * @param outWorld [out] Receives the point in the world; may be @p local itself. Must not be NULL.
 */
void WorldPathComponent_PointToWorld(const WorldPathComponent* self, const PathCurvePoint* local, PathCurvePoint* outWorld);

/**
 * @brief Reports whether a position in the world lies inside this path's region: the area its closed curve encloses in its
 *        object's X and Z, along its object's Y at any height.
 * @param self The path component; must not be NULL.
 * @param world The position in the world.
 * @returns true when inside; false outside, on an open path, and for an object scaled flat.
 */
bool WorldPathComponent_IsWorldPointInside(const WorldPathComponent* self, Vector3Double world);
