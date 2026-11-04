#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector3 comes from raylib: a point's tangent and frame are directions, floats like every direction in the world.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
// Vector3Double and BoundingBoxDouble come from WRVectorMath: where a path runs is a position, in double like the objects'.
#include "wr/WRVectorMath.h"


/**
 * @file PathCurve.h
 * @brief A line through nodes as pure maths: its shape, its length, its frames, its sections, samples along it, the
 *        nearest point on it, and the area a closed one encloses. No world, no GL: every generator that reads a path
 *        (fences, sweeps, terrain effects, roads, ponds) goes through this.
 *
 * NODES. A path is a list of PathNode values. Between two neighbouring nodes lies one segment:
 * - A SMOOTH node lies on the curve and the curve has no kink there. Between two smooth nodes the curve is a centripetal
 *   Catmull-Rom spline (which never loops or cusps, whatever the spacing of the nodes), held as a cubic Bezier.
 * - A CORNER node is sharp, and a segment that touches a corner is a straight line. A smooth node next to a corner bends
 *   away along that line, with no kink: straight, bend, straight is corner, smooth, smooth, corner.
 * - A BEZIER node shapes its segments by its two handles (offsets from the node). A handle wins over a corner's
 *   straightness: a segment between a corner and a bezier node is a curve shaped by the one handle it has.
 *
 * DISTANCE. Everything along a path is measured by its arc length, in the units of the space the nodes are given in (for a
 * path component, its object's: a scaled object scales the distances too). A point at a distance is solved exactly (to
 * about a micrometre), not read from a table of samples. On a closed path a distance wraps; on an open one it clamps.
 *
 * FRAMES. A point carries a tangent, an up and a right, all unit and at right angles (Right = Tangent x Up: to the right
 * looking along the path, seen from above with +Y up). PathUpMode says how the up is found; the node's roll then turns the
 * frame about the tangent, and a positive roll leans the up toward the right (a bank into a right-hand bend).
 *
 * VALUES. A node's roll, width and scale reach the points between nodes through a monotone cubic over the distance: as
 * smooth as the curve, and never past either neighbour's value.
 *
 * SECTIONS. Node 0 of an open path, every corner node and every node with StartsSection start a section, which runs to the
 * next start. The settings of a section are its first node's. A closed path's last section runs through the end of the
 * list to the first start, and a closed path with no start at all is one loop from node 0. What a section's settings mean
 * (a skipped section, a section's terrain snapping) is for whatever reads the path: the path only holds them.
 *
 * REGIONS. A closed path also encloses an area: the polygon of its points in the path's own X and Z, inside by non-zero
 * winding (so a loop drawn twice round is still inside). Height does not count: a region is a prism along Y.
 *
 * USE. Construct, build from nodes, read, build again whenever the nodes change, deconstruct. A build allocates one block;
 * reading never allocates or writes, so readers need no synchronisation with each other. Not thread-safe against a build.
 */


// Macros.
/** @brief The most nodes a curve takes. */
#define PATH_CURVE_MAX_NODES ((size_t)16384)
/** @brief The most samples one call writes: a limit on what a very long path with a very short spacing asks for. */
#define PATH_CURVE_MAX_SAMPLES ((size_t)1048576)
/** @brief The largest coordinate of a node's position or handle PathCurve_Build takes, in metres: past what any world holds,
 *         and far below where squared lengths could overflow. */
#define PATH_CURVE_COORDINATE_LIMIT 1.0e12
/** @brief PathSampleOptions_Create's default: how far the curve may leave the straight line between two samples, in
 *         metres. */
#define PATH_SAMPLE_DEFAULT_MAX_DEVIATION 0.01
/** @brief PathSampleOptions_Create's default: how far the tangent may turn between two samples, in radians (10 degrees). */
#define PATH_SAMPLE_DEFAULT_MAX_TURN 0.17453292f


// Types.
/**
 * @brief How the curve passes through a node. Saved in files: values are explicit and never renumbered.
 */
typedef enum PathNodeModeEnum
{
    /** @brief The curve passes through the node with no kink (centripetal Catmull-Rom). */
    PathNodeMode_Smooth = 0,
    /** @brief Sharp, and the segments touching it are straight. */
    PathNodeMode_Corner = 1,
    /** @brief The curve is shaped by the node's in and out handles. */
    PathNodeMode_Bezier = 2
} PathNodeMode;

/**
 * @brief How a path's frames find their up. Saved in files: values are explicit and never renumbered.
 */
typedef enum PathUpModeEnum
{
    /** @brief The up stays as near +Y as the tangent allows (the path's own +Y: a component's object's). Right for
     *  anything standing on the ground. A run straight up or down keeps the right of what came before it. */
    PathUpMode_Upright = 0,
    /** @brief The frame turns as little as it can from one point to the next, starting upright: for paths that climb
     *  steeply or loop, where upright would spin. A closed loop's leftover twist is spread along its length, so its ends
     *  meet. */
    PathUpMode_RotationMinimizing = 1
} PathUpMode;

/**
 * @brief One node of a path. A plain value type; PathNode_CreateDefault gives the defaults.
 */
typedef struct PathNodeStruct
{
    /** @brief Where it lies, in metres, relative to the path's space (a component's object). */
    Vector3Double Position;
    /** @brief How the curve passes through it. */
    PathNodeMode Mode;
    /** @brief A bezier node's handle toward the node before it, as an offset from the node; kept but unused otherwise. */
    Vector3 InHandle;
    /** @brief A bezier node's handle toward the node after it, as an offset from the node; kept but unused otherwise. */
    Vector3 OutHandle;
    /** @brief How far the frame turns about the tangent, in radians (degrees in files); positive leans the up to the
     *  right. */
    float Roll;
    /** @brief How wide what follows the path is across it, as a factor (1 is as its own settings say); at least 0. */
    float Width;
    /** @brief How large what follows the path is in every direction, as a factor (1 is as its own settings say); at least
     *  0. */
    float Scale;
    /** @brief Whether a section starts here (a corner node always starts one). */
    bool StartsSection;
    /** @brief Whether the section this node starts is skipped: a gate's opening, a gap in a wall. Only a node that starts
     *  a section gives it. */
    bool SkipsSection;
    /** @brief Whether the section this node starts sets its own terrain snapping, over its reader's. */
    bool OverridesSnapsToTerrain;
    /** @brief The section's terrain snapping while OverridesSnapsToTerrain is on: false for a bridge. */
    bool SnapsToTerrain;
} PathNode;

/**
 * @brief A point on a path: where it is, its frame and its values there. A plain value type.
 */
typedef struct PathCurvePointStruct
{
    /** @brief The position, in the nodes' space. */
    Vector3Double Position;
    /** @brief The unit tangent, along the path's direction. */
    Vector3 Tangent;
    /** @brief The unit up, after the roll. */
    Vector3 Up;
    /** @brief The unit right, after the roll: Tangent x Up. */
    Vector3 Right;
    /** @brief The roll here, in radians. */
    float Roll;
    /** @brief The width factor here. */
    float Width;
    /** @brief The scale factor here. */
    float Scale;
    /** @brief The distance along the path from node 0 (a closed path's wrapped into its length). */
    double Distance;
    /** @brief The segment it lies on: the index of the node it leaves. */
    uint32_t Segment;
    /** @brief The section it lies in. */
    uint32_t Section;
} PathCurvePoint;

/**
 * @brief One section of a path. A plain value type.
 */
typedef struct PathCurveSectionStruct
{
    /** @brief The node that starts it, whose settings it has. */
    uint32_t FirstNode;
    /** @brief How many segments it has (it reaches node FirstNode + SegmentCount, modulo the node count on a closed
     *  path). */
    uint32_t SegmentCount;
    /** @brief Where it starts, as a distance from node 0. */
    double StartDistance;
    /** @brief Its length. StartDistance + Length passes the path's length on a closed path's last section. */
    double Length;
    /** @brief The first node's SkipsSection. */
    bool IsSkipped;
    /** @brief The first node's OverridesSnapsToTerrain. */
    bool OverridesSnapsToTerrain;
    /** @brief The first node's SnapsToTerrain. */
    bool SnapsToTerrain;
} PathCurveSection;

/**
 * @brief How finely PathCurve_WriteSamples samples. Build with PathSampleOptions_Create.
 */
typedef struct PathSampleOptionsStruct
{
    /** @brief The most distance between two samples (above 0). */
    double Spacing;
    /** @brief How far the curve may leave the straight line between two samples (above 0). */
    double MaxDeviation;
    /** @brief How far the tangent may turn between two samples, in radians (above 0; pi or more asks for nothing). */
    float MaxTurn;
} PathSampleOptions;

/** @brief A segment's shape (private to source/PathCurve.c). */
typedef struct PathCurveSegmentStruct PathCurveSegment;
/** @brief A node's values and their slopes (private to source/PathCurve.c). */
typedef struct PathCurveNodeValuesStruct PathCurveNodeValues;

/**
 * @brief A built curve. Read-only outside this module: use the functions. Construct with PathCurve_Construct, build with
 *        PathCurve_Build, release with PathCurve_Deconstruct.
 */
typedef struct PathCurveStruct
{
    /** @brief The one allocation everything below points into; NULL while empty. */
    void* _block;
    /** @brief The segments. */
    const PathCurveSegment* _segments;
    /** @brief Every node's values. */
    const PathCurveNodeValues* _nodeValues;
    /** @brief The arc-length table: each cubic segment's distances at its even parameter steps. */
    const double* _tableDistances;
    /** @brief The rotation-minimizing frames' rights over the same table; NULL in the upright mode. */
    const Vector3* _tableRights;
    /** @brief The sections. */
    const PathCurveSection* _sections;
    /** @brief A closed path's polygon, as X and Z (Y here is the path's Z); NULL on an open one. */
    const Vector2Double* _regionPoints;
    /** @brief The number of nodes given. */
    uint32_t _nodeCount;
    /** @brief The number of segments. */
    uint32_t _segmentCount;
    /** @brief The number of sections. */
    uint32_t _sectionCount;
    /** @brief The number of polygon points. */
    uint32_t _regionPointCount;
    /** @brief Whether the path closes on itself. */
    bool _isClosed;
    /** @brief The whole length. */
    double _length;
    /** @brief A box around the whole curve (its control points'). */
    BoundingBoxDouble _bounds;
} PathCurve;


// Functions: nodes and options.
/**
 * @brief A node with the defaults: at the origin, smooth, no handles, no roll, width and scale 1, starting no section.
 * @returns The node.
 */
static inline PathNode PathNode_CreateDefault(void)
{
    return (PathNode)
    {
        .Position = { 0.0, 0.0, 0.0 }, .Mode = PathNodeMode_Smooth, .InHandle = { 0.0f, 0.0f, 0.0f },
        .OutHandle = { 0.0f, 0.0f, 0.0f }, .Roll = 0.0f, .Width = 1.0f, .Scale = 1.0f, .StartsSection = false,
        .SkipsSection = false, .OverridesSnapsToTerrain = false, .SnapsToTerrain = true
    };
}

/**
 * @brief Sampling options at a spacing, with the default deviation and turn.
 * @param spacing The most distance between two samples (above 0).
 * @returns The options.
 */
static inline PathSampleOptions PathSampleOptions_Create(double spacing)
{
    return (PathSampleOptions){ .Spacing = spacing, .MaxDeviation = PATH_SAMPLE_DEFAULT_MAX_DEVIATION,
        .MaxTurn = PATH_SAMPLE_DEFAULT_MAX_TURN };
}

/**
 * @brief A section's terrain snapping in effect.
 * @param section The section; must not be NULL.
 * @param readerDefault What its reader does where a section does not say.
 * @returns The section's own while it overrides, otherwise @p readerDefault.
 */
static inline bool PathCurveSection_ResolveSnapsToTerrain(const PathCurveSection* section, bool readerDefault)
{
    return section->OverridesSnapsToTerrain ? section->SnapsToTerrain : readerDefault;
}


// Functions: lifetime.
/**
 * @brief Constructs an empty curve (no nodes, length 0).
 * @param self The curve; must not be NULL.
 */
void PathCurve_Construct(PathCurve* self);

/**
 * @brief Releases what a curve holds. Safe to repeat; the curve is empty and usable afterwards.
 * @param self The curve, or NULL.
 */
void PathCurve_Deconstruct(PathCurve* self);

/**
 * @brief Builds the curve from nodes, replacing what it held. The nodes are not kept.
 *
 * O(nodes). One closed node, or none, makes a curve with no segments: a point of length 0, or nothing. Nodes at one place
 * make zero-length segments, which every function here takes in stride (no NaN reaches a result).
 * @param self The curve; must not be NULL.
 * @param nodes The nodes; may be NULL only when @p nodeCount is 0.
 * @param nodeCount The number of nodes, at most PATH_CURVE_MAX_NODES.
 * @param isClosed Whether a segment runs from the last node back to the first.
 * @param upMode How its frames find their up.
 * @returns Success; ErrorCode_IllegalArgument (the curve left empty) for NULL arguments, too many nodes, an unknown mode or
 *          up mode, a value that is not finite, or a coordinate past PATH_CURVE_COORDINATE_LIMIT.
 */
Error PathCurve_Build(PathCurve* self, const PathNode* nodes, size_t nodeCount, bool isClosed, PathUpMode upMode);


// Functions: reading.
/** @brief The curve's whole length (0 for an empty one). @param self The curve; must not be NULL. @returns The length. */
static inline double PathCurve_GetLength(const PathCurve* self)
{
    return self->_length;
}

/** @brief How many nodes it was built from. @param self The curve; must not be NULL. @returns The count. */
static inline uint32_t PathCurve_GetNodeCount(const PathCurve* self)
{
    return self->_nodeCount;
}

/** @brief How many segments it has. @param self The curve; must not be NULL. @returns The count. */
static inline uint32_t PathCurve_GetSegmentCount(const PathCurve* self)
{
    return self->_segmentCount;
}

/** @brief Whether the path closes on itself. @param self The curve; must not be NULL. @returns true when closed. */
static inline bool PathCurve_IsClosed(const PathCurve* self)
{
    return self->_isClosed;
}

/** @brief A box around the whole curve, in the nodes' space; meaningless while it has no nodes. @param self The curve;
 *  must not be NULL. @returns The box. */
static inline BoundingBoxDouble PathCurve_GetBounds(const PathCurve* self)
{
    return self->_bounds;
}

/**
 * @brief The distance along the path of a node.
 * @param self The curve; must not be NULL.
 * @param nodeIndex The node, below the node count.
 * @returns Its distance from node 0 (0 for a node out of range).
 */
double PathCurve_GetNodeDistance(const PathCurve* self, uint32_t nodeIndex);

/**
 * @brief The point at a distance along the path. At a node between two segments it is the point leaving the node: on a
 *        corner, the tangent of the segment after it. To end a run at a corner with the tangent it arrived with, use
 *        PathCurve_GetSectionPoint.
 *
 * O(log nodes), with a few evaluations of the segment.
 * @param self The curve; must not be NULL.
 * @param distance The distance from node 0; clamped to the length on an open path, wrapped on a closed one. Must be finite.
 * @param outPoint [out] Receives the point. Must not be NULL.
 * @returns true; false for a curve with no nodes (or a non-finite distance), leaving @p outPoint alone.
 */
bool PathCurve_GetPointAt(const PathCurve* self, double distance, PathCurvePoint* outPoint);

/**
 * @brief The nearest point on the path to a point.
 *
 * O(table entries) at worst, with segments far from the best so far skipped by their boxes; a cubic's every pass near the
 * point is looked at. On ties the first along the path wins.
 * @param self The curve; must not be NULL.
 * @param point The point, in the nodes' space.
 * @param ignoresHeight Whether distances are measured in X and Z alone (how far is this tree from the road, in plan).
 * @param outPoint [out] Receives the nearest point. Must not be NULL.
 * @param outSeparation [out] Receives the distance to it, by the same measure; may be NULL.
 * @returns true; false for a curve with no nodes (or a non-finite point), leaving the outputs alone.
 */
bool PathCurve_FindNearest(const PathCurve* self, Vector3Double point, bool ignoresHeight, PathCurvePoint* outPoint,
    double* outSeparation);


// Functions: sections.
/** @brief How many sections the path has: at least 1 with any node (one node is a section of no segments), 0 without. @param
 *  self The curve; must not be NULL. @returns The count. */
static inline uint32_t PathCurve_GetSectionCount(const PathCurve* self)
{
    return self->_sectionCount;
}

/**
 * @brief A section of the path.
 * @param self The curve; must not be NULL.
 * @param index The section, below the section count; in order of their first nodes.
 * @returns The section (borrowed: valid until the next build).
 */
static inline const PathCurveSection* PathCurve_GetSection(const PathCurve* self, uint32_t index)
{
    return &self->_sections[index];
}

/**
 * @brief The point at a distance along one section. Unlike PathCurve_GetPointAt it stays inside the section: its last
 *        point has the tangent the section arrives with, even at a corner.
 * @param self The curve; must not be NULL.
 * @param sectionIndex The section, below the section count.
 * @param localDistance The distance from the section's start, clamped to its length. Must be finite.
 * @param outPoint [out] Receives the point. Must not be NULL.
 * @returns true; false for a section out of range (or a non-finite distance), leaving @p outPoint alone.
 */
bool PathCurve_GetSectionPoint(const PathCurve* self, uint32_t sectionIndex, double localDistance,
    PathCurvePoint* outPoint);


// Functions: samples.
/**
 * @brief Appends distances along the whole path to sample it at: 0, every node, the length, and between them at most the
 *        spacing apart, closer where the curve bends (by the options' deviation and turn), ascending. Doubles, each a
 *        distance PathCurve_GetPointAt takes. Straight segments get the spacing and nothing more. A closed path's last
 *        sample is its length, back at its start. A curve with no segment appends one 0 when it has a node, else
 *        nothing. The distances strictly rise (a segment too short to move them adds none).
 * @param self The curve; must not be NULL.
 * @param options How finely. Must not be NULL.
 * @param outDistances The buffer of doubles to append to; not cleared. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments, a buffer of another element size or options not above 0;
 *          ErrorCode_ArgumentOutOfRange when this would pass PATH_CURVE_MAX_SAMPLES, ErrorCode_BufferTooSmall when the
 *          buffer cannot grow (some samples may have been appended: the caller clears them).
 */
Error PathCurve_WriteSamples(const PathCurve* self, const PathSampleOptions* options, GenericBuffer* outDistances);

/**
 * @brief The same for one section: distances from the section's start, each a local distance PathCurve_GetSectionPoint
 *        takes, from 0 to exactly its length.
 * @param self The curve; must not be NULL.
 * @param sectionIndex The section, below the section count.
 * @param options How finely. Must not be NULL.
 * @param outDistances The buffer of doubles to append to; not cleared. Must not be NULL.
 * @returns As PathCurve_WriteSamples; also ErrorCode_IllegalArgument for a section out of range.
 */
Error PathCurve_WriteSectionSamples(const PathCurve* self, uint32_t sectionIndex, const PathSampleOptions* options,
    GenericBuffer* outDistances);


// Functions: regions.
/**
 * @brief Reports whether a point in the path's X and Z lies inside the area a closed path encloses, by non-zero winding.
 *
 * O(polygon points), after a test against its box. The polygon holds the curve to about a centimetre (more on a very long
 * segment); a point exactly on it may go either way.
 * @param self The curve; must not be NULL.
 * @param x The point's X, in the nodes' space.
 * @param z The point's Z, in the nodes' space.
 * @returns true when inside; false outside, and always on an open path.
 */
bool PathCurve_IsPointInside(const PathCurve* self, double x, double z);

/**
 * @brief A closed path's polygon, as the region tests see it.
 * @param self The curve; must not be NULL.
 * @param outCount [out] Receives how many points. Must not be NULL.
 * @returns The points, X in X and the path's Z in Y (borrowed: valid until the next build); NULL on an open path.
 */
static inline const Vector2Double* PathCurve_GetRegionPoints(const PathCurve* self, uint32_t* outCount)
{
    *outCount = self->_regionPointCount;
    return self->_regionPoints;
}
