#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector3, Quaternion and Matrix are raylib types in the queries' arguments and results (positions, rotations, the
// debug shapes' transforms); this include is unavoidable here.
#include "raylib/raylib.h"
#include "AssetManager.h"
#include "CollisionLayer.h"
#include "CollisionShape.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"

// Forward declarations: the collision world borrows a world (World.h) and a logger (Logger.h).
typedef struct WorldStruct World;
typedef struct LoggerStruct Logger;


/**
 * @file WorldCollision.h
 * @brief The collision world: every enabled collider of a world (WorldColliderComponent.h) in world space, kept in step
 *        with the world every tick, and the queries over it (T12).
 *
 * WHAT IT HOLDS. One instance per enabled collider of an effectively enabled object: its shapes in world space (the
 * object's world matrix, the collider's rotation mode and local transform, each shape's centre and rotation; any
 * affine transform, uneven scales and parents' shear included), its world bounds, its layer, mask and trigger flag
 * (the settings in effect, WorldColliderComponent_ResolveSettings). The shapes come from the object's world model's
 * collision sets (resolved by name under the collision world's own asset user, AGENTS.md rule 1) or from the
 * collider's own primitive.
 *
 * SYNC. WorldCollision_Sync, once a tick in the collision step (after the transform drivers): it brings the world
 * matrices up to date, then rebuilds only the colliders whose object moved (its transform revision: a parent moving
 * moves it too), whose collider changed (its revision), whose world model changed or whose component was added,
 * removed or replaced. Static colliders (the object and all its ancestors static) sit in one dynamic AABB tree,
 * movable ones in another with fattened bounds; unbounded planes in a list every query checks. A static object that
 * moves is re-inserted, with one warning per object unless the options turn that off (the editor).
 *
 * QUERIES. Each takes a filter (WorldCollisionFilter): the layers it sees, triggers ignored, included or only, and an
 * object to ignore, optionally with its subtree. Rays and casts give the nearest hit; overlaps the objects touched;
 * contacts every manifold up to a separation. Every result says which object and shape (index, part name, type),
 * which layer, and for meshes which triangle. None allocates per call beyond the caller's output buffers' growth.
 * Queries see the colliders as of the last sync: an object moved, changed or removed since is found as and where it was
 * until the next sync (only an ignored subtree is read from the live hierarchy). Moving something and querying again in
 * the same tick needs a sync in between.
 *
 * CONTINUOUS CASTS. WorldCollision_CastShape moves shapes from one pose to another and finds the first contact along
 * the way, whatever the speed: an exact time of impact for linear motion (the GJK ray cast), conservative advancement
 * when the pose turns (no contact is skipped, it stops a hair before touching). A shape that starts inside something
 * reports that at fraction 0 with the depth and the direction to push it out. A shape at rest against something, as
 * a cast left it (touching, or overlapping by at most 0.1 mm of rounding), is stopped only by a motion that goes
 * deeper: it slides along a floor and lifts off it freely.
 *
 * TRIGGER EVENTS. After each sync the pairs (trigger, other collider) that overlap are compared with the previous
 * tick's: an enter for each new pair, an exit for each that ended (also when either object is removed or disabled, or
 * stops qualifying). An exit needs WORLD_COLLISION_TRIGGER_EXIT_MARGIN of separation, so a collider resting on a
 * boundary does not flicker. Two triggers never pair, a static trigger never pairs with a static collider, and both
 * must collide by layer. Events queue in a deterministic order (by ids) until WorldCollision_ClearTriggerEvents: game
 * logic drains them at one point of the tick (architecture A4).
 *
 * THREADS AND LIFETIME. Not thread-safe. The collision world borrows its world, which must outlive it, and must be
 * synced on that world only. Pointers it returns (events, debug shapes, shape names in results) are valid until the
 * next WorldCollision_Sync.
 */


// Macros.
/** @brief An overlapping trigger pair ends only once the two are this far apart (metres): no flicker at a boundary. */
#define WORLD_COLLISION_TRIGGER_EXIT_MARGIN 0.01f
/** @brief How far a movable collider's tree box is fattened (metres): small moves cost no re-insertion. */
#define WORLD_COLLISION_MOVABLE_MARGIN 0.1f
/** @brief The most shapes one query may cast or test at once (a compound; a query places them on the stack). */
#define WORLD_COLLISION_MAX_QUERY_SHAPES 32U


// Types.
/**
 * @brief Options of WorldCollision_Create. Build with WorldCollisionOptions_CreateDefault.
 */
typedef struct WorldCollisionOptionsStruct
{
    /** @brief Whether moving a static object logs one warning per object (default true; the editor turns it off, as it
     *  moves static scenery all the time). */
    bool WarnsWhenStaticObjectsMove;
} WorldCollisionOptions;

/**
 * @brief What a query does with triggers.
 */
typedef enum WorldCollisionTriggerModeEnum
{
    /** @brief Triggers are skipped (the default of rays and casts: they never block). */
    WorldCollisionTriggerMode_Ignore = 0,
    /** @brief Triggers are found like the rest (the default of overlaps). */
    WorldCollisionTriggerMode_Include = 1,
    /** @brief Only triggers are found. */
    WorldCollisionTriggerMode_Only = 2
} WorldCollisionTriggerMode;

/**
 * @brief What a query sees. A plain value type; build with WorldCollisionFilter_Create.
 */
typedef struct WorldCollisionFilterStruct
{
    /** @brief The layers whose colliders the query sees (COLLISION_LAYER_BIT per layer; COLLISION_LAYER_MASK_ALL for
     *  every layer). */
    uint32_t LayerMask;
    /** @brief What it does with triggers. */
    WorldCollisionTriggerMode TriggerMode;
    /** @brief An object whose collider the query skips (0: none), e.g. the player's own. */
    uint64_t IgnoredObjectId;
    /** @brief Whether the ignored object's whole subtree is skipped (what a character carries). */
    bool IsIgnoredSubtree;
} WorldCollisionFilter;

/**
 * @brief Which shape of which object a result is about.
 */
typedef struct WorldCollisionShapeInfoStruct
{
    /** @brief The object (0 for a query's own shape). */
    uint64_t ObjectId;
    /** @brief The shape's index in its collider's set (0 for an own shape) or in the query's shapes. */
    uint32_t ShapeIndex;
    /** @brief The shape's part name ("handle"), or NULL. Borrowed: valid until the next sync. */
    const unsigned char* ShapeName;
    /** @brief The shape's type. */
    CollisionShapeType ShapeType;
    /** @brief The collider's layer in effect (prop for a query's own shape). */
    CollisionLayer Layer;
    /** @brief Whether the collider is a trigger. */
    bool IsTrigger;
    /** @brief Whether the result is on a mesh triangle (the fields below are valid). */
    bool HasTriangle;
    /** @brief The triangle's index in the mesh (CollisionMesh's order). */
    uint32_t TriangleIndex;
    /** @brief The model's mesh the triangle came from, and its index among that mesh's own triangles (with the
     *  barycentrics, the model's UVs at the hit: T19). */
    uint32_t MeshPart;
    uint32_t SourceTriangle;
    /** @brief The mesh part's material index, -1 for none. */
    int32_t MaterialIndex;
    /** @brief Ray hits on triangles: the weights of the triangle's corners (in the mesh's order) at the hit. */
    Vector3 Barycentric;
} WorldCollisionShapeInfo;

/**
 * @brief The nearest hit of a ray.
 */
typedef struct WorldRayHitStruct
{
    /** @brief The shape hit. */
    WorldCollisionShapeInfo Shape;
    /** @brief The hit point. */
    Vector3 Point;
    /** @brief The surface's unit normal there, facing the ray (against the ray when it started inside). */
    Vector3 Normal;
    /** @brief The distance from the origin, in metres. */
    float Distance;
    /** @brief Distance over the ray's length. */
    float Fraction;
    /** @brief The ray started inside a solid shape (Distance 0). */
    bool IsStartInside;
} WorldRayHit;

/**
 * @brief The first contact of a cast.
 */
typedef struct WorldCastHitStruct
{
    /** @brief The shape hit. */
    WorldCollisionShapeInfo Shape;
    /** @brief Which of the cast's shapes hit (an index into the shapes given). */
    uint32_t CastShapeIndex;
    /** @brief The share of the motion done at the contact, in [0, 1]. */
    float Fraction;
    /** @brief The distance the cast's position moved up to the contact, in metres. */
    float Distance;
    /** @brief The contact point on the surface hit, and on the cast shape. */
    Vector3 Point;
    Vector3 PointOnCast;
    /** @brief The surface hit's unit normal, facing the cast shape: the direction to slide along or push it out. */
    Vector3 Normal;
    /** @brief The cast shape was already inside at the start (Fraction 0): PenetrationDepth along Normal pushes it out.
     *  (An overlap of at most 0.1 mm, a shape at rest where a cast left it, counts only when the motion goes deeper.) */
    bool IsStartPenetrating;
    /** @brief With IsStartPenetrating: how deep it was, in metres. */
    float PenetrationDepth;
} WorldCastHit;

/**
 * @brief One point of a contact manifold.
 */
typedef struct WorldContactPointStruct
{
    /** @brief World positions on A's and on B's surface. */
    Vector3 PositionA;
    Vector3 PositionB;
    /** @brief The same points in A's space (the query's pose, or A's object's space) and in B's object's space: where the
     *  contact sits on each body, for matching and anchoring across ticks. */
    Vector3 LocalPositionA;
    Vector3 LocalPositionB;
    /** @brief Along the manifold's normal: above 0 apart, below 0 penetrating (metres). */
    float Separation;
    /** @brief Which features made the point (the same while the contact keeps its topology). */
    uint32_t FeatureId;
} WorldContactPoint;

/**
 * @brief A contact manifold between a shape (A) and a shape of the world (B).
 */
typedef struct WorldContactManifoldStruct
{
    /** @brief The query's shape, or the collider's of WorldCollision_CollideObject. */
    WorldCollisionShapeInfo ShapeA;
    /** @brief The world's shape touched. */
    WorldCollisionShapeInfo ShapeB;
    /** @brief The unit normal from A to B: moving A against it by the depth separates them. */
    Vector3 Normal;
    /** @brief The deepest point's separation (below 0: penetrating; the push-out is -Separation along -Normal for A). */
    float Separation;
    /** @brief 1 to 4 points. */
    uint32_t PointCount;
    WorldContactPoint Points[4];
} WorldContactManifold;

/**
 * @brief The kinds of trigger events. Saved by value (scripts and saves will see them): never renumber.
 */
typedef enum WorldTriggerEventKindEnum
{
    /** @brief A collider started overlapping a trigger. */
    WorldTriggerEventKind_Enter = 0,
    /** @brief It stopped (moved out, was removed, disabled, or stopped qualifying). */
    WorldTriggerEventKind_Exit = 1
} WorldTriggerEventKind;

/**
 * @brief A trigger event.
 */
typedef struct WorldTriggerEventStruct
{
    WorldTriggerEventKind Kind;
    /** @brief The trigger's object. */
    uint64_t TriggerId;
    /** @brief The other collider's object. */
    uint64_t OtherId;
    /** @brief The sync that found it (WorldCollisionStats.Tick). */
    uint64_t Tick;
} WorldTriggerEvent;

/**
 * @brief A shape to draw for debugging (WorldCollision_GetDebugShapes).
 */
typedef struct WorldCollisionDebugShapeStruct
{
    /** @brief The shape (its sizes, hull or mesh; borrowed until the next sync). */
    const CollisionShape* Shape;
    /** @brief From the shape's own space to the world. */
    Matrix Transform;
    /** @brief The collider's layer and trigger flag, and whether it is static. */
    CollisionLayer Layer;
    bool IsTrigger;
    bool IsStatic;
} WorldCollisionDebugShape;

/**
 * @brief Counts for diagnostics and the status line.
 */
typedef struct WorldCollisionStatsStruct
{
    /** @brief Active instances (enabled colliders with shapes), and how many of them are static and movable. */
    size_t InstanceCount;
    size_t StaticInstanceCount;
    size_t MovableInstanceCount;
    /** @brief Their shapes, the triangles of their mesh shapes, and their unbounded planes. */
    size_t ShapeCount;
    size_t TriangleCount;
    size_t PlaneCount;
    /** @brief The overlapping trigger pairs. */
    size_t TriggerPairCount;
    /** @brief How often an instance's shapes were rebuilt, and how often an instance moved, since construction. */
    uint64_t RebuildCount;
    uint64_t MoveCount;
    /** @brief The number of syncs so far. */
    uint64_t Tick;
} WorldCollisionStats;

/** @brief The collision world. Opaque; create with WorldCollision_Create. */
typedef struct WorldCollisionStruct WorldCollision;


// Functions: values.
/**
 * @brief Default options: moving a static object warns once per object.
 * @returns The options.
 */
static inline WorldCollisionOptions WorldCollisionOptions_CreateDefault(void)
{
    return (WorldCollisionOptions){ .WarnsWhenStaticObjectsMove = true };
}

/**
 * @brief A filter of the layers in @p layerMask and the trigger rule @p triggerMode, ignoring no object.
 * @param layerMask COLLISION_LAYER_BIT per layer seen.
 * @param triggerMode What to do with triggers.
 * @returns The filter.
 */
static inline WorldCollisionFilter WorldCollisionFilter_Create(uint32_t layerMask, WorldCollisionTriggerMode triggerMode)
{
    return (WorldCollisionFilter){ .LayerMask = layerMask, .TriggerMode = triggerMode, .IgnoredObjectId = 0U,
        .IsIgnoredSubtree = false };
}


// Functions: lifetime.
/**
 * @brief Creates a collision world for @p world. Nothing is in it until the first WorldCollision_Sync.
 * @param world The world (borrowed: it must outlive the collision world); must not be NULL.
 * @param assets The asset manager its world models load from (borrowed), or NULL: then only own shapes collide.
 * @param logger Logs warnings (once each: a missing world model or set, a static object moved, a flattened shape);
 *        borrowed, may be NULL.
 * @param options The options, or NULL for the defaults.
 * @param outCollision [out] Receives the collision world (owned: WorldCollision_Destroy). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL world or output.
 * @note May propagate errors from internal calls (registering the asset user); consult the documentation of called
 *       functions for the full set.
 */
Error WorldCollision_Create(World* world, AssetManager* assets, Logger* logger, const WorldCollisionOptions* options,
    WorldCollision** outCollision);

/**
 * @brief Destroys a collision world, releasing its world models (its asset user). The world is left untouched.
 * @param self The collision world; NULL does nothing.
 */
void WorldCollision_Destroy(WorldCollision* self);


// Functions: the tick.
/**
 * @brief Brings the collision world in step with its world: world matrices first (World_UpdateTransforms), then each
 *        collider that changed, then the trigger pairs (queuing their events).
 *
 * The tick's collision step (after the transform drivers, before the queued events are handled). Costs one pass over
 * the colliders plus the work for those that changed.
 * @param self The collision world; must not be NULL.
 */
void WorldCollision_Sync(WorldCollision* self);

/**
 * @brief Returns the trigger events queued since the last WorldCollision_ClearTriggerEvents, oldest first (within a
 *        sync by trigger id, then other id; enters before exits).
 * @param self The collision world; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The events (borrowed: valid until the next sync or clear), or NULL when there are none.
 */
const WorldTriggerEvent* WorldCollision_GetTriggerEvents(const WorldCollision* self, size_t* outCount);

/**
 * @brief Drops the queued trigger events (after game logic handled them).
 * @param self The collision world; must not be NULL.
 */
void WorldCollision_ClearTriggerEvents(WorldCollision* self);

/**
 * @brief Returns the counts.
 * @param self The collision world; must not be NULL.
 * @returns The counts as of the last sync.
 */
WorldCollisionStats WorldCollision_GetStats(const WorldCollision* self);


// Functions: queries.
/**
 * @brief The nearest hit of a ray.
 * @param self The collision world; must not be NULL.
 * @param origin Where the ray starts.
 * @param direction Its direction (any length but 0; it is normalised).
 * @param maxDistance How far it reaches, in metres (above 0).
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outHit [out] Receives the hit. Must not be NULL.
 * @returns true when the ray hit something.
 */
bool WorldCollision_Raycast(WorldCollision* self, Vector3 origin, Vector3 direction, float maxDistance,
    const WorldCollisionFilter* filter, WorldRayHit* outHit);

/**
 * @brief Every hit of a ray (one per shape it hits), nearest first.
 * @param self The collision world; must not be NULL.
 * @param origin Where the ray starts.
 * @param direction Its direction (normalised).
 * @param maxDistance How far it reaches.
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outHits [out] Receives WorldRayHit elements, appended (the caller clears it). Must not be NULL.
 * @returns How many hits were appended.
 */
size_t WorldCollision_RaycastAll(WorldCollision* self, Vector3 origin, Vector3 direction, float maxDistance,
    const WorldCollisionFilter* filter, GenericBuffer* outHits);

/**
 * @brief Moves shapes from one pose to another and finds their first contact on the way, whatever the speed.
 *
 * The shapes are given in the pose's own space (each with its centre and rotation); a pose is a position and a unit
 * rotation. When the rotation does not change the motion is linear and the time of impact exact; otherwise conservative
 * advancement finds the first time they come within a hair of touching (it never skips a contact). A shape already
 * inside something at the start is a hit at fraction 0 with its push-out. Meshes and planes cannot be cast (a mesh is
 * never moved): a query holding one finds nothing.
 * @param self The collision world; must not be NULL.
 * @param shapes The shapes (spheres, capsules, boxes, cylinders, hulls); must not be NULL.
 * @param shapeCount How many (1 to WORLD_COLLISION_MAX_QUERY_SHAPES).
 * @param startPosition The pose at the start.
 * @param startRotation (unit quaternion).
 * @param endPosition The pose at the end.
 * @param endRotation (unit quaternion).
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outHit [out] Receives the first contact. Must not be NULL.
 * @returns true when something was hit.
 */
bool WorldCollision_CastShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3 startPosition, Quaternion startRotation, Vector3 endPosition, Quaternion endRotation,
    const WorldCollisionFilter* filter, WorldCastHit* outHit);

/**
 * @brief Sweeps a sphere from @p start to @p end (WorldCollision_CastShape for one sphere).
 * @returns true when something was hit (see WorldCollision_CastShape).
 */
bool WorldCollision_SweepSphere(WorldCollision* self, Vector3 start, Vector3 end, float radius,
    const WorldCollisionFilter* filter, WorldCastHit* outHit);

/**
 * @brief Sweeps an upright capsule (a character) from @p start to @p end, given by its centre.
 * @param height The capsule's whole height, caps included (at least 2 x @p radius).
 * @returns true when something was hit (see WorldCollision_CastShape).
 */
bool WorldCollision_SweepCapsule(WorldCollision* self, Vector3 start, Vector3 end, float radius, float height,
    const WorldCollisionFilter* filter, WorldCastHit* outHit);

/**
 * @brief Sweeps a box of @p halfExtents turned by @p rotation from @p start to @p end (not turning on the way).
 * @returns true when something was hit (see WorldCollision_CastShape).
 */
bool WorldCollision_SweepBox(WorldCollision* self, Vector3 start, Vector3 end, Vector3 halfExtents, Quaternion rotation,
    const WorldCollisionFilter* filter, WorldCastHit* outHit);

/**
 * @brief The objects whose colliders overlap shapes at a pose (touching counts).
 * @param self The collision world; must not be NULL.
 * @param shapes The shapes, in the pose's space (any primitive or hull; a bounded plane finds the convex shapes on it,
 *        not planes or meshes; an unbounded plane or a mesh makes the query find nothing); must not be NULL.
 * @param shapeCount How many (1 to WORLD_COLLISION_MAX_QUERY_SHAPES).
 * @param position The pose's position.
 * @param rotation The pose's rotation (unit quaternion).
 * @param filter What it sees (NULL: every layer, triggers included).
 * @param outObjectIds [out] Receives uint64_t object ids, appended in ascending order, each once (the caller clears
 *        it). Must not be NULL.
 * @returns How many ids were appended.
 */
size_t WorldCollision_OverlapShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3 position, Quaternion rotation, const WorldCollisionFilter* filter, GenericBuffer* outObjectIds);

/** @brief WorldCollision_OverlapShape for one sphere. @returns How many ids were appended. */
size_t WorldCollision_OverlapSphere(WorldCollision* self, Vector3 center, float radius,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds);

/** @brief WorldCollision_OverlapShape for one upright capsule of @p height (caps included) around @p center.
 *  @returns How many ids were appended. */
size_t WorldCollision_OverlapCapsule(WorldCollision* self, Vector3 center, float radius, float height,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds);

/** @brief WorldCollision_OverlapShape for one box. @returns How many ids were appended. */
size_t WorldCollision_OverlapBox(WorldCollision* self, Vector3 center, Vector3 halfExtents, Quaternion rotation,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds);

/**
 * @brief Every contact manifold of shapes at a pose with the world, out to @p maxSeparation (negative separations are
 *        penetrations: each manifold gives its push-out).
 * @param self The collision world; must not be NULL.
 * @param shapes The shapes, in the pose's space (spheres, capsules, boxes, cylinders, hulls: a plane or a mesh makes the
 *        query find nothing); must not be NULL.
 * @param shapeCount How many (1 to WORLD_COLLISION_MAX_QUERY_SHAPES).
 * @param position The pose's position.
 * @param rotation The pose's rotation.
 * @param maxSeparation How far apart shapes may be and still give a manifold, in metres (at least 0; a small skin
 *        gives speculative contacts).
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outManifolds [out] Receives WorldContactManifold elements, appended (the caller clears it). Must not be NULL.
 * @returns How many manifolds were appended.
 */
size_t WorldCollision_CollideShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3 position, Quaternion rotation, float maxSeparation, const WorldCollisionFilter* filter,
    GenericBuffer* outManifolds);

/**
 * @brief Every contact manifold of an object's collider (as synced) with the rest of the world.
 * @param self The collision world; must not be NULL.
 * @param objectId The object; it is ignored by the query itself (with its subtree when the filter says so).
 * @param maxSeparation As for WorldCollision_CollideShape.
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outManifolds [out] Receives WorldContactManifold elements, appended. Must not be NULL.
 * @returns How many manifolds were appended (0 when the object has no active collider; an object's own plane and mesh
 *          shapes are skipped for now, so a collider made only of them gives none).
 */
size_t WorldCollision_CollideObject(WorldCollision* self, uint64_t objectId, float maxSeparation,
    const WorldCollisionFilter* filter, GenericBuffer* outManifolds);


// Functions: debugging.
/**
 * @brief Switches the debug list on or off: while on, each sync lists every active shape for drawing.
 * @param self The collision world; must not be NULL.
 * @param isEnabled Whether to keep the list.
 */
void WorldCollision_SetDebugListEnabled(WorldCollision* self, bool isEnabled);

/**
 * @brief Returns the debug list (empty while it is off), for a renderer that draws wireframes: read only.
 * @param self The collision world; must not be NULL.
 * @param outCount [out] Receives how many shapes. Must not be NULL.
 * @returns The shapes (borrowed: valid until the next sync), or NULL when there are none.
 */
const WorldCollisionDebugShape* WorldCollision_GetDebugShapes(const WorldCollision* self, size_t* outCount);
