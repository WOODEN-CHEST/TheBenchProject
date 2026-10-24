#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector3 and Quaternion are raylib types in the queries' arguments and results (directions, normals, rotations); this
// include is unavoidable here. Positions are WRVectorMath's doubles (T33).
#include "raylib/raylib.h"
#include "wr/WRVectorMath.h"
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
 * collider's own primitive. An object with a terrain component (B01) and no collider is a TERRAIN instance: one
 * one-sided triangle mesh shape per chunk of its terrain (the shape's index is the chunk's; a hit's MeshPart names the
 * chunk and its SourceTriangle the terrain's triangle) with the terrain's own active edges, so nothing bumps where
 * chunks meet; its layer, mask and enabled flag are the terrain's settings in effect
 * (WorldTerrainComponent_ResolveSettings). An object with both is a collider instance alone (one warning).
 *
 * SYNC. The first part of WorldCollision_Step, once a tick in the collision step (after the transform drivers), or
 * WorldCollision_Sync alone: it brings the world matrices up to date, then rebuilds only the colliders whose object
 * moved (its transform revision: a parent moving moves it too), whose collider changed (its revision), whose world model
 * changed or whose component was added, removed or replaced. A terrain instance rebuilds only the chunks whose
 * revision changed, and every chunk when the chunk layout changed. Static colliders (the object and all its ancestors
 * static) sit in one dynamic AABB tree, movable ones in another with fattened bounds; unbounded planes in a list every
 * query checks. A static object that moves is re-inserted, with one warning per object unless the options turn that off
 * (the editor).
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
 * MOVING AND PUSHING (T27). WorldCollision_Step is the tick's collision step: the sync above, then every collider that
 * moved since the last step is SWEPT from where it was to where its driver, script, animation or parent put it, turns
 * included, and one solver pushes everything that overlaps apart at once.
 * - Who gives way: a static object's collider never moves in play; a movable object's collider that is pushable
 *   (is_pushable) is pushed, pushables sharing a push by mass; any other movable collider is KINEMATIC (doors,
 *   elevators): it pushes and is never pushed. The colliders an object carries (its children, unless pushable
 *   themselves) move with its push; a pushable under it is pushed on its own. Pushes only translate (rotation from
 *   contacts is T28's), and only along each contact's normal, so a pushed collider slides along walls.
 * - Nothing passes through anything: contacts are taken before the motion out to how far each pair can close
 *   (speculative contacts), and every pushed motion longer than WORLD_COLLISION_SPECULATIVE_DISTANCE is checked
 *   afterwards with exact linear casts against everything around it, both sides moving; what still hits gets a new
 *   contact and the solver runs again, and a motion no round could resolve stops at its time of impact. Turns are
 *   covered by the contacts: a tick that turns anything more than WORLD_COLLISION_MAX_SUBSTEP_TURN is split into
 *   substeps, each with its own.
 * - What does not fit: a kinematic mover that would squeeze a pushable against something that does not give way is
 *   BLOCKED: with when_blocked stop it is set back where it was at the step's start (the local transforms that changed
 *   along its object's chain, so a group moved by its parent stops as one) and the step is solved again (at most
 *   WORLD_COLLISION_MAX_BLOCK_ROUNDS times; then the movers still blocked push through); with push_through it moves
 *   on. A pushable left overlapping beyond WORLD_COLLISION_SLOP is CRUSHED.
 * - The solver writes the pushes into the objects (a child through its parent's inverse): the one place a system other
 *   than an object's driver writes its transform. A driver reads the correction next tick.
 * - A TELEPORT (WorldCollision_Teleport: spawning, the editor, a script's "teleport to") skips the sweep: the object
 *   starts its next step where it is and is only pushed out. New colliders and static objects that moved are
 *   teleports too.
 * - Pushables that nothing moved near sleep: a step costs nothing for a pile at rest.
 *
 * CONTACTS AND THEIR EVENTS (T27). After each step every pair of colliders, one of them pushable, that lies within
 * WORLD_COLLISION_CONTACT_DISTANCE (touching) or a little farther (speculative) has a contact per pair of shapes
 * (WorldContact: T12's manifold at the final poses, plus each point's relative velocity, push, age and whether it
 * touches). Per OBJECT pair a contact event begins when they first touch, persists every tick while they stay in contact
 * (only when either collider reports persisting contacts) and ends when they are WORLD_COLLISION_CONTACT_END_MARGIN
 * beyond touching, or either is removed, disabled or stops qualifying. Blocked and crushed states give motion events
 * when they begin and end. WorldCollision_GetMotionResult gives each object's feedback for its driver: its push, its
 * contacts, its ground, blocked or crushed.
 *
 * DOUBLES OUTSIDE, FLOATS INSIDE (T33). Every position the collision world takes or gives is a double (Vector3Double:
 * query origins and poses, hit and contact points, grounds, the debug list's placements), so worlds hundreds of
 * kilometres across collide as they do at their origin: the skin, the rest gaps and the sweep thresholds (millimetres)
 * mean the same everywhere. Directions, normals, velocities, pushes, sizes and local positions stay floats. Inside, the
 * shapes' own maths runs in float relative to an origin near what it tests (a query's start, a pair's first shape), as
 * Jolt's double-precision build does. Shapes keep their vertices in their own space as floats: a triangle mesh loses
 * precision at its far end when it is very large (1 mm at 8 km from its object's origin). A terrain's chunks are shapes
 * of their own with their vertices relative to the chunk's centre, where its position grid is exact in float
 * (TerrainMesh.h), so a terrain is exact across its whole extent. A plane's point is its object's origin: a level
 * unbounded plane is exact anywhere, a tilted one moves by up to the float step at its distance from that origin
 * (agents/WorldCollision.md, "Known limits").
 *
 * THREADS AND LIFETIME. Not thread-safe. The collision world borrows its world, which must outlive it, and must be
 * synced on that world only. Pointers it returns (events, contacts, debug lists, shape names in results) are valid until
 * the next WorldCollision_Sync or WorldCollision_Step; the queued events until they are cleared.
 */


// Macros.
/** @brief An overlapping trigger pair ends only once the two are this far apart (metres): no flicker at a boundary. */
#define WORLD_COLLISION_TRIGGER_EXIT_MARGIN 0.01f
/** @brief How far a movable collider's tree box is fattened (metres): small moves cost no re-insertion. */
#define WORLD_COLLISION_MOVABLE_MARGIN 0.1f
/** @brief The most shapes one query may cast or test at once (a compound; a query places them on the stack). */
#define WORLD_COLLISION_MAX_QUERY_SHAPES 32U
/** @brief The gap the solver keeps between a pushed collider and what it touches (metres): resting shapes lie this far
 *  apart, so every sweep starts clear of them. */
#define WORLD_COLLISION_SKIN 0.001f
/** @brief The overlap the solver may leave without a report (metres): beyond it a pushable is crushed and a mover that
 *  caused it blocked. */
#define WORLD_COLLISION_SLOP 0.001f
/** @brief Contacts this far apart beyond how far a pair can close in a step join the solver before the motion (metres):
 *  room for the pushes passed on through a crowd. */
#define WORLD_COLLISION_SPECULATIVE_DISTANCE 0.02f
/** @brief Shapes at most this far apart touch (metres: the skin and a millimetre). */
#define WORLD_COLLISION_CONTACT_DISTANCE 0.002f
/** @brief An object pair in contact stays in contact until its shapes are this much farther apart than touching
 *  (metres): no flicker for a crate resting on a floor. */
#define WORLD_COLLISION_CONTACT_END_MARGIN 0.005f
/** @brief A step turning anything more than this (radians, 15 degrees) is split into substeps, so the solver's straight
 *  line approximations of turns stay close. */
#define WORLD_COLLISION_MAX_SUBSTEP_TURN 0.2617994f
/** @brief The most substeps of one step (a faster turn is solved in this many; the sweeps still catch every contact). */
#define WORLD_COLLISION_MAX_SUBSTEPS 8U
/** @brief The most solver iterations per solve (it stops earlier once every contact holds). */
#define WORLD_COLLISION_SOLVER_ITERATIONS 64U
/** @brief How many times one solve may add the contacts its sweeps found and run again before the motions that still
 *  hit are stopped at their time of impact. */
#define WORLD_COLLISION_SWEEP_ROUNDS 4U
/** @brief How many rounds of pushing out what still overlaps at the final poses a step may run. */
#define WORLD_COLLISION_DEPENETRATION_ROUNDS 4U
/** @brief How many times one step may set blocked movers back and solve again (movers pushing into each other through
 *  pushables stop one more each time); past it, the movers still blocked push through for that step. */
#define WORLD_COLLISION_MAX_BLOCK_ROUNDS 8U


// Types.
/**
 * @brief Options of WorldCollision_Create. Build with WorldCollisionOptions_CreateDefault.
 */
typedef struct WorldCollisionOptionsStruct
{
    /** @brief Whether moving a static object logs one warning per object (default true; the editor turns it off, as it
     *  moves static scenery all the time). */
    bool WarnsWhenStaticObjectsMove;
    /** @brief Whether WorldCollision_Step sweeps motions (default true). Off, every move is a teleport: things are only
     *  pushed out where they end up (the editor, which places objects rather than moving them). */
    bool AreMotionsSwept;
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
    /** @brief Whether pushable colliders are skipped, and the movable colliders a pushable carries (its children's, which
     *  its push moves; default false): a character's own movement sweeps past them and leaves them to the collision
     *  step, which pushes both by mass (T13). Kinematic and static colliders still count. */
    bool ArePushablesIgnored;
    /** @brief Whether the query collides as a collider on OwnLayer would (default false): it then also skips colliders
     *  whose mask lacks that layer, the other half of CollisionLayer_DoCollide's symmetric rule (a character's sweeps
     *  pass what does not collide with characters). Off, the query's LayerMask alone decides. */
    bool HasOwnLayer;
    /** @brief The layer the query collides as, while HasOwnLayer is on (a value that is not a layer: the query sees
     *  nothing). */
    CollisionLayer OwnLayer;
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
    /** @brief The hit point, in the world (double). */
    Vector3Double Point;
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
    /** @brief The contact point on the surface hit, and on the cast shape, in the world (double). */
    Vector3Double Point;
    Vector3Double PointOnCast;
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
    /** @brief World positions on A's and on B's surface (double). */
    Vector3Double PositionA;
    Vector3Double PositionB;
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
    /** @brief From the shape's own space to the world, in double (its translation far from the world's origin): draw it
     *  relative to a nearby origin (GameMath_ToMatrixRelative). */
    Matrix4Double Transform;
    /** @brief The collider's layer and trigger flag, and whether it is static. */
    CollisionLayer Layer;
    bool IsTrigger;
    bool IsStatic;
} WorldCollisionDebugShape;

/**
 * @brief Extra data of one contact point, matched to WorldContactManifold's point of the same index.
 */
typedef struct WorldContactPointDetailStruct
{
    /** @brief The relative velocity of B's point against A's along the normal, from the motions over the step (m/s;
     *  below 0 closing). */
    float NormalSpeed;
    /** @brief The rest of the relative velocity, across the normal (m/s): sliding. */
    Vector3 TangentVelocity;
    /** @brief How far the solver pushed the two apart at this point this step (metres, at least 0), and that as a speed
     *  over the step (m/s). Pushes on the pair the solver could not match to a point are only in WorldContact's
     *  TotalPush. */
    float Push;
    float PushSpeed;
    /** @brief For how many steps this point has existed (matched across steps by its feature id; 0 when new). */
    uint32_t AgeTicks;
    /** @brief Whether it touches (separation at most WORLD_COLLISION_CONTACT_DISTANCE) or is speculative (near). */
    bool IsTouching;
} WorldContactPointDetail;

/**
 * @brief One contact between a shape of one object and a shape of another, at the end of a step (T27).
 */
typedef struct WorldContactStruct
{
    /** @brief T12's manifold at the final poses: A the object with the lower id, the normal from A to B, the points'
     *  world and local positions, separations and feature ids, triangle data. */
    WorldContactManifold Manifold;
    /** @brief One per point of the manifold. */
    WorldContactPointDetail Points[4];
    /** @brief How fast the two closed along the normal before the solver pushed them (m/s, at least 0): the motions
     *  their drivers gave them. The impact speed of a new contact (impact sounds). */
    float ApproachSpeed;
    /** @brief Every push on this pair of shapes this step, matched to a point or not (metres). */
    float TotalPush;
    /** @brief For how many steps this pair of shapes has been in contact (0 when new). */
    uint32_t AgeTicks;
    /** @brief Whether any point touches; otherwise the shapes are near but apart (speculative). */
    bool IsTouching;
} WorldContact;

/**
 * @brief The kinds of contact events. Saved by value (scripts will see them): never renumber.
 */
typedef enum WorldContactEventKindEnum
{
    /** @brief Two objects started touching. */
    WorldContactEventKind_Begin = 0,
    /** @brief They are still in contact (only for colliders that report persisting contacts). */
    WorldContactEventKind_Persist = 1,
    /** @brief They stopped (moved apart, one was removed or disabled, or they stopped qualifying). */
    WorldContactEventKind_End = 2
} WorldContactEventKind;

/**
 * @brief A contact event between two objects. Its contacts are copies, valid until WorldCollision_ClearContactEvents.
 */
typedef struct WorldContactEventStruct
{
    WorldContactEventKind Kind;
    /** @brief The objects: A the lower id. */
    uint64_t ObjectIdA;
    uint64_t ObjectIdB;
    /** @brief The step that found it (WorldCollisionStats.Tick). */
    uint64_t Tick;
    /** @brief For how many steps the two have been in contact (0 at the begin). */
    uint32_t AgeTicks;
    /** @brief The impact: how fast the two closed when they began touching (m/s), and the share of that step at which
     *  they would have touched along their drivers' motions (0 when they started the step touching). */
    float ApproachSpeed;
    float TimeOfImpact;
    /** @brief The pair's contacts (one per pair of shapes): WorldCollision_GetContactEventContacts from FirstContact on,
     *  ContactCount of them. A begin or persist event has the step's contacts, an end event the last step's. */
    uint32_t FirstContact;
    uint32_t ContactCount;
} WorldContactEvent;

/**
 * @brief The kinds of motion events. Saved by value (scripts will see them): never renumber.
 */
typedef enum WorldMotionEventKindEnum
{
    /** @brief A kinematic mover could not push a pushable out of its way: it stopped (when_blocked stop) or moved on
     *  (push_through). */
    WorldMotionEventKind_BlockedBegin = 0,
    /** @brief It is no longer blocked. */
    WorldMotionEventKind_BlockedEnd = 1,
    /** @brief A pushable is left overlapping something beyond WORLD_COLLISION_SLOP (pinned between things that do not
     *  give way). */
    WorldMotionEventKind_CrushedBegin = 2,
    /** @brief It is no longer crushed. */
    WorldMotionEventKind_CrushedEnd = 3
} WorldMotionEventKind;

/**
 * @brief A motion event: a mover blocked or a pushable crushed, when the state begins and ends.
 */
typedef struct WorldMotionEventStruct
{
    WorldMotionEventKind Kind;
    /** @brief The mover (blocked) or the pushable (crushed). */
    uint64_t ObjectId;
    /** @brief The pushable that blocked the mover, or what the pushable is pressed into most deeply (0 at an end). */
    uint64_t OtherId;
    /** @brief The overlap left at the begin (metres). */
    float Depth;
    /** @brief A blocked mover that was set back (stop). */
    bool IsStopped;
    /** @brief The step that found it, or the sync that removed the collider (WorldCollisionStats.Tick then). */
    uint64_t Tick;
} WorldMotionEvent;

/**
 * @brief An object's feedback from the last step, for its driver (T13's character reads it).
 */
typedef struct WorldMotionResultStruct
{
    /** @brief The step it describes (WorldCollisionStats.Tick right after that step; syncs since then count on). */
    uint64_t Tick;
    /** @brief Whether the collider took part in the step's solve (moved, was pushed or is near what did). */
    bool IsAwake;
    /** @brief The collider's motion over the step, start pose to final pose, per second (m/s). */
    Vector3 Velocity;
    /** @brief Where the step put it against where its driver did (world metres): a pushable's push, or the motion a
     *  stopped mover lost. */
    Vector3 Correction;
    /** @brief Its touching contacts (a pushable's with those of the colliders it carries). */
    uint32_t ContactCount;
    /** @brief The most upward-facing of those, as the ground: its object, the normal out of the ground into the object
     *  (its Y above 0), the point, and the ground point's velocity (a moving platform). */
    bool HasGround;
    uint64_t GroundObjectId;
    Vector3 GroundNormal;
    /** @brief The ground's point, in the world (double). */
    Vector3Double GroundPoint;
    Vector3 GroundVelocity;
    /** @brief A kinematic mover that could not push a pushable out of its way, which one, and whether it was set back. */
    bool IsBlocked;
    uint64_t BlockedById;
    bool IsStopped;
    /** @brief A pushable left overlapping beyond the slop, by how much (metres) and with what (a pushable's only, not
     *  repeated by the colliders it carries). */
    bool IsCrushed;
    float CrushDepth;
    uint64_t CrushedById;
} WorldMotionResult;

/**
 * @brief A contact point to draw for debugging (WorldCollision_GetDebugContacts).
 */
typedef struct WorldCollisionDebugContactStruct
{
    /** @brief Midway between the point on A and the point on B, in the world (double). */
    Vector3Double Point;
    /** @brief The contact's unit normal, from A to B. */
    Vector3 Normal;
    /** @brief The point's push as a speed along the normal (m/s; 0 when nothing pushed). */
    float PushSpeed;
    /** @brief Whether it touches (else speculative). */
    bool IsTouching;
} WorldCollisionDebugContact;

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
    /** @brief How many terrain chunks' collision meshes were built since construction (B01: a chunk's again only when it
     *  changes). */
    uint64_t TerrainChunkBuildCount;
    /** @brief The number of syncs and steps so far. */
    uint64_t Tick;
    /** @brief The last step (T27): colliders that moved, pushables that took part, pairs of shapes the solver looked
     *  at, contacts (touching ones, and the object pairs in contact), solver iterations, substeps, the deepest overlap
     *  left (metres), blocked movers, crushed pushables, and how often the step was solved again for stopped movers. */
    size_t MoverCount;
    size_t AwakeCount;
    size_t PairCount;
    size_t ContactCount;
    size_t ContactPairCount;
    uint32_t SolverIterations;
    uint32_t SubstepCount;
    float DeepestOverlap;
    size_t BlockedCount;
    size_t CrushedCount;
    uint32_t ResolveCount;
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
    return (WorldCollisionOptions){ .WarnsWhenStaticObjectsMove = true, .AreMotionsSwept = true };
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
        .IsIgnoredSubtree = false, .ArePushablesIgnored = false, .HasOwnLayer = false,
        .OwnLayer = CollisionLayer_Prop };
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
 * Moves and pushes nothing: the game's tick runs WorldCollision_Step, which syncs first. A sync alone is for tools, the
 * editor and queries after a change in the middle of a tick; it leaves the motions alone (the next step sweeps from
 * where each collider was at the last step). Costs one pass over the colliders plus the work for those that changed.
 * @param self The collision world; must not be NULL.
 */
void WorldCollision_Sync(WorldCollision* self);

/**
 * @brief The tick's collision step (T27): syncs, sweeps every collider that moved since the last step, pushes apart
 *        what overlaps, writes the pushes into the objects, then finds the contacts, events and results and the
 *        trigger pairs.
 *
 * Replaces WorldCollision_Sync in the tick (after the transform drivers, before the queued events are handled). The
 * motions run from each collider's pose at the end of the last step (whatever syncs happened between) to its pose now;
 * a collider's first step, a teleport and a static object's move start where they are. Costs nothing for colliders
 * that did not move and are not near anything that did. Writes object transforms: pushed objects' positions, stopped
 * movers' local transforms.
 * @param self The collision world; must not be NULL.
 * @param deltaSeconds The tick's length in seconds (velocities and speeds in contacts and results); above 0.
 */
void WorldCollision_Step(WorldCollision* self, float deltaSeconds);

/**
 * @brief Makes an object and everything under it start its next step where it is then: no sweep of how it got there,
 *        only the push-out (spawning, the editor, a script's "teleport to"). Call it when moving the object, before
 *        the step.
 * @param self The collision world; must not be NULL.
 * @param objectId The object (an unknown id does nothing).
 */
void WorldCollision_Teleport(WorldCollision* self, uint64_t objectId);

/**
 * @brief Returns the last step's contacts, sorted by object ids, then shapes and triangles.
 * @param self The collision world; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The contacts (borrowed: valid until the next sync or step), or NULL when there are none.
 */
const WorldContact* WorldCollision_GetContacts(const WorldCollision* self, size_t* outCount);

/**
 * @brief Returns the contact events queued since the last WorldCollision_ClearContactEvents, oldest first (within a step
 *        by object ids; ends after begins and persists).
 * @param self The collision world; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The events (borrowed: valid until the next step or clear), or NULL when there are none.
 */
const WorldContactEvent* WorldCollision_GetContactEvents(const WorldCollision* self, size_t* outCount);

/**
 * @brief Returns the contacts the queued contact events index (WorldContactEvent.FirstContact, .ContactCount).
 * @param self The collision world; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The copies (borrowed: valid until the next step or clear), or NULL when there are none.
 */
const WorldContact* WorldCollision_GetContactEventContacts(const WorldCollision* self, size_t* outCount);

/**
 * @brief Drops the queued contact events and their contacts (after game logic handled them).
 * @param self The collision world; must not be NULL.
 */
void WorldCollision_ClearContactEvents(WorldCollision* self);

/**
 * @brief Returns the motion events (blocked, crushed) queued since the last WorldCollision_ClearMotionEvents, oldest
 *        first (within a step by object id).
 * @param self The collision world; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The events (borrowed: valid until the next step or clear), or NULL when there are none.
 */
const WorldMotionEvent* WorldCollision_GetMotionEvents(const WorldCollision* self, size_t* outCount);

/**
 * @brief Drops the queued motion events.
 * @param self The collision world; must not be NULL.
 */
void WorldCollision_ClearMotionEvents(WorldCollision* self);

/**
 * @brief An object's feedback from the last step: its push, its contacts and ground, blocked or crushed.
 * @param self The collision world; must not be NULL.
 * @param objectId The object.
 * @param outResult [out] Receives the result (zeroed when false). Must not be NULL.
 * @returns true when the object has an active collider.
 */
bool WorldCollision_GetMotionResult(const WorldCollision* self, uint64_t objectId, WorldMotionResult* outResult);

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
 * @param origin Where the ray starts, in the world (double: exact there however far from the world's origin).
 * @param direction Its direction (any length but 0; it is normalised).
 * @param maxDistance How far it reaches, in metres (above 0).
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outHit [out] Receives the hit. Must not be NULL.
 * @returns true when the ray hit something.
 */
bool WorldCollision_Raycast(WorldCollision* self, Vector3Double origin, Vector3 direction, float maxDistance,
    const WorldCollisionFilter* filter, WorldRayHit* outHit);

/**
 * @brief Every hit of a ray (one per shape it hits), nearest first.
 * @param self The collision world; must not be NULL.
 * @param origin Where the ray starts, in the world.
 * @param direction Its direction (normalised).
 * @param maxDistance How far it reaches.
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outHits [out] Receives WorldRayHit elements, appended (the caller clears it). Must not be NULL.
 * @returns How many hits were appended.
 */
size_t WorldCollision_RaycastAll(WorldCollision* self, Vector3Double origin, Vector3 direction, float maxDistance,
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
 * @param startPosition The pose at the start, in the world (double).
 * @param startRotation (unit quaternion).
 * @param endPosition The pose at the end, in the world (double).
 * @param endRotation (unit quaternion).
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outHit [out] Receives the first contact. Must not be NULL.
 * @returns true when something was hit.
 */
bool WorldCollision_CastShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3Double startPosition, Quaternion startRotation, Vector3Double endPosition, Quaternion endRotation,
    const WorldCollisionFilter* filter, WorldCastHit* outHit);

/**
 * @brief Sweeps a sphere from @p start to @p end (WorldCollision_CastShape for one sphere).
 * @returns true when something was hit (see WorldCollision_CastShape).
 */
bool WorldCollision_SweepSphere(WorldCollision* self, Vector3Double start, Vector3Double end, float radius,
    const WorldCollisionFilter* filter, WorldCastHit* outHit);

/**
 * @brief Sweeps an upright capsule (a character) from @p start to @p end, given by its centre.
 * @param height The capsule's whole height, caps included (at least 2 x @p radius).
 * @returns true when something was hit (see WorldCollision_CastShape).
 */
bool WorldCollision_SweepCapsule(WorldCollision* self, Vector3Double start, Vector3Double end, float radius,
    float height, const WorldCollisionFilter* filter, WorldCastHit* outHit);

/**
 * @brief Sweeps a box of @p halfExtents turned by @p rotation from @p start to @p end (not turning on the way).
 * @returns true when something was hit (see WorldCollision_CastShape).
 */
bool WorldCollision_SweepBox(WorldCollision* self, Vector3Double start, Vector3Double end, Vector3 halfExtents,
    Quaternion rotation, const WorldCollisionFilter* filter, WorldCastHit* outHit);

/**
 * @brief The objects whose colliders overlap shapes at a pose (touching counts).
 * @param self The collision world; must not be NULL.
 * @param shapes The shapes, in the pose's space (any primitive or hull; a bounded plane finds the convex shapes on it,
 *        not planes or meshes; an unbounded plane or a mesh makes the query find nothing); must not be NULL.
 * @param shapeCount How many (1 to WORLD_COLLISION_MAX_QUERY_SHAPES).
 * @param position The pose's position, in the world (double).
 * @param rotation The pose's rotation (unit quaternion).
 * @param filter What it sees (NULL: every layer, triggers included).
 * @param outObjectIds [out] Receives uint64_t object ids, appended in ascending order, each once (the caller clears
 *        it). Must not be NULL.
 * @returns How many ids were appended.
 */
size_t WorldCollision_OverlapShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3Double position, Quaternion rotation, const WorldCollisionFilter* filter, GenericBuffer* outObjectIds);

/** @brief WorldCollision_OverlapShape for one sphere. @returns How many ids were appended. */
size_t WorldCollision_OverlapSphere(WorldCollision* self, Vector3Double center, float radius,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds);

/** @brief WorldCollision_OverlapShape for one upright capsule of @p height (caps included) around @p center.
 *  @returns How many ids were appended. */
size_t WorldCollision_OverlapCapsule(WorldCollision* self, Vector3Double center, float radius, float height,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds);

/** @brief WorldCollision_OverlapShape for one box. @returns How many ids were appended. */
size_t WorldCollision_OverlapBox(WorldCollision* self, Vector3Double center, Vector3 halfExtents, Quaternion rotation,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds);

/**
 * @brief Every contact manifold of shapes at a pose with the world, out to @p maxSeparation (negative separations are
 *        penetrations: each manifold gives its push-out).
 * @param self The collision world; must not be NULL.
 * @param shapes The shapes, in the pose's space (spheres, capsules, boxes, cylinders, hulls: a plane or a mesh makes the
 *        query find nothing); must not be NULL.
 * @param shapeCount How many (1 to WORLD_COLLISION_MAX_QUERY_SHAPES).
 * @param position The pose's position, in the world (double).
 * @param rotation The pose's rotation.
 * @param maxSeparation How far apart shapes may be and still give a manifold, in metres (at least 0; a small skin
 *        gives speculative contacts).
 * @param filter What it sees (NULL: every layer, triggers ignored).
 * @param outManifolds [out] Receives WorldContactManifold elements, appended (the caller clears it). Must not be NULL.
 * @returns How many manifolds were appended.
 */
size_t WorldCollision_CollideShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3Double position, Quaternion rotation, float maxSeparation, const WorldCollisionFilter* filter,
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
 * @brief Switches the debug contact list on or off: while on, each step lists every contact point for drawing.
 * @param self The collision world; must not be NULL.
 * @param isEnabled Whether to keep the list.
 */
void WorldCollision_SetDebugContactsEnabled(WorldCollision* self, bool isEnabled);

/**
 * @brief Returns the debug contact list (empty while it is off): read only.
 * @param self The collision world; must not be NULL.
 * @param outCount [out] Receives how many points. Must not be NULL.
 * @returns The points (borrowed: valid until the next step or sync), or NULL when there are none.
 */
const WorldCollisionDebugContact* WorldCollision_GetDebugContacts(const WorldCollision* self, size_t* outCount);

/**
 * @brief Returns the debug list (empty while it is off), for a renderer that draws wireframes: read only.
 * @param self The collision world; must not be NULL.
 * @param outCount [out] Receives how many shapes. Must not be NULL.
 * @returns The shapes (borrowed: valid until the next sync), or NULL when there are none.
 */
const WorldCollisionDebugShape* WorldCollision_GetDebugShapes(const WorldCollision* self, size_t* outCount);
