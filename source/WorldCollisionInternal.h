#pragma once
#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include "CollisionGeometry.h"
#include "GameMath.h"
#include "WorldCollision.h"
#include "WorldCollisionTree.h"
#include "TerrainAsset.h"
#include "WorldColliderComponent.h"
#include "WorldModel.h"
#include "WorldObject.h"
#include "WorldTerrainComponent.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): the collision world's state, shared by its files: source/WorldCollision.c (lifetime, sync,
 * debug list, stats), source/WorldCollisionQuery.c (the queries), source/WorldCollisionTrigger.c (trigger pairs and
 * events), and since T27 source/WorldCollisionStep.c (the step: motions, who takes part, blocked movers, the write-back,
 * the results), source/WorldCollisionSolver.c (pairs, contacts at poses, the sweeps, the push-out solver) and
 * source/WorldCollisionEvents.c (contacts across steps, contact and motion events, the debug contacts).
 *
 * INSTANCES live in slots (one growable array with a free list), so a tree proxy can name its shape by (slot, shape
 * index) for as long as the instance lives. An object has at most one: its collider's, or (B01) its terrain
 * component's when it has no collider (a terrain instance holds one mesh shape per chunk, its shape index the chunk's).
 * Between changes of the world's component set, _order maps each collider of the world's dense collider array, then
 * each terrain of its terrain array, to its slot, so a sync is one walk in the world's order; _lookup (sorted by object
 * id) finds an object's instance for the queries. Every instance holds its shapes in world space (WorldCollisionShape:
 * the transform both ways, the box, the proxy).
 *
 * DOUBLES OUTSIDE, FLOATS INSIDE (T33, as Jolt's double-precision build). Everything that says where something is in
 * the world is a double: the shapes' and objects' placements (WorldCollisionPlacement: float axes, a double
 * translation), the motion poses (WorldCollisionPose: a double position), the trees' boxes and the step's swept boxes
 * (BoundingBoxDouble). The narrowphase (source/CollisionGeometry.h) stays in float: before each call the world layer
 * picks an ORIGIN near what it tests (a query's start, a pair's first body's pose) and hands it affines and poses
 * relative to it, subtracted in double and rounded once (WorldCollisionInternal_ToRelative, _ToRelativeInverse,
 * _ToRelativePose); what comes back is relative to that origin and becomes a world position when the origin is added
 * back in double. A step's manifold keeps the origin its points are relative to. Directions, normals, separations,
 * pushes and sizes stay floats: they mean the same anywhere.
 *
 * THE STEP (T27). Each instance keeps its motion frame (Pose: its object's world position and its turn by the rotation
 * mode, rigid) and where that was at the end of the last step (LastPose); each shape keeps its transform relative to the
 * pose (ShapeToPose), so it can be placed anywhere along a motion. A step gives every instance that takes part a BODY:
 * pushables that are awake (moved, pushed, or near something that moved), kinematic and static instances that pair with
 * them. A body's push moves its OWNER: a pushable owns itself and the non-pushable colliders under it (it carries them);
 * kinematic and static bodies have none. PAIRS of shapes (A convex and owned by a pushable whenever either side is) are
 * collected from the trees with the bodies' swept boxes; a solve turns their contacts into ROWS (one per contact point:
 * a normal, the separation after the motions without pushes, the push so far), and the projected Gauss-Seidel moves
 * the owners. The last solve's contacts at the final poses become the step's contacts (source/WorldCollisionEvents.c).
 */


// Macros.
/** A slot that names nothing. */
#define WORLD_COLLISION_NO_SLOT UINT32_MAX
/** A body that names nothing. */
#define WORLD_COLLISION_NO_BODY UINT32_MAX
/** A contact's triangle when its shapes have none. */
#define WORLD_COLLISION_NO_TRIANGLE UINT32_MAX


// Types.
/** An affine placement in the world: point p goes to X * p.x + Y * p.y + Z * p.z + Translation. The axes are floats
 *  (they mean the same anywhere); the translation is a double, so a far placement keeps a double's precision. */
typedef struct WorldCollisionPlacementStruct
{
    /** The images of the local X, Y and Z axes (the linear part's columns). */
    Vector3 X;
    Vector3 Y;
    Vector3 Z;
    /** Where the local origin goes, in the world. */
    Vector3Double Translation;
} WorldCollisionPlacement;

/** A rigid pose in the world: a position in double and a unit rotation. */
typedef struct WorldCollisionPoseStruct
{
    Vector3Double Position;
    Quaternion Rotation;
} WorldCollisionPose;

/** One shape of an instance, in world space. */
typedef struct WorldCollisionShapeStruct
{
    /** The shape (borrowed from the world model, or the instance's OwnShape block). */
    const CollisionShape* Shape;
    /** Its index in its set (0 for an own shape). */
    uint32_t ShapeIndex;
    /** Shape space to the world, and the inverse of its axes (no translation): WorldCollisionInternal_PlaceShape gives
     *  both ways relative to an origin. */
    WorldCollisionPlacement ShapeToWorld;
    CollisionAffine WorldToShapeAxes;
    /** Whether its sizes are valid: always for a world model's shapes (checked at load), by CollisionShape_Validate for
     *  an own shape (its rows allow a cylinder of half height 0, which is flat). */
    bool IsSizeValid;
    /** False when it has no valid size or the transform flattens it (a zero scale): it takes part in nothing. */
    bool IsValid;
    /** An unbounded plane: no box and no proxy (it is in the planes list). */
    bool IsUnbounded;
    /** The world box (fat for nothing: the tree fattens its own copy). */
    BoundingBoxDouble Box;
    /** Its proxy in the instance's tree, or COLLISION_TREE_NULL. */
    int32_t Proxy;
    /** Shape space to the instance's pose (T27): the shape placed at any pose P is P * ShapeToPose. */
    CollisionAffine ShapeToPose;
} WorldCollisionShape;

/** What a terrain instance keeps for one chunk of its terrain's mesh (B01): the chunk's mesh shape, placed at the
 *  chunk's centre in the terrain's space (exact in float: on the position grid, within the extent), one-sided; its mesh
 *  (owned; NULL for a chunk of holes only, or one whose mesh could not be built); and the chunk revision it was built
 *  from. */
typedef struct WorldCollisionTerrainChunkStruct
{
    CollisionShape Shape;
    CollisionMesh* Mesh;
    uint32_t Revision;
} WorldCollisionTerrainChunk;

/** One collision source of an object: its collider, or (B01) its terrain component when it has no collider. */
typedef struct WorldCollisionInstanceStruct
{
    /** Whether the slot holds an instance. */
    bool IsUsed;
    /** The next free slot while unused. */
    uint32_t NextFree;
    uint64_t ObjectId;
    /** The source: the collider, or the terrain component (one of them is NULL; borrowed: re-matched whenever the
     *  world's component set changes), and its serial (a replaced component often reuses its predecessor's memory: the
     *  serial tells them apart). */
    WorldColliderComponent* Collider;
    WorldTerrainComponent* Terrain;
    uint64_t ComponentSerial;
    /** What the shapes were built from: the source component's and the object's transform revision, valid while
     *  HasSeen. */
    bool HasSeen;
    uint32_t SeenComponentRevision;
    uint32_t SeenTransformRevision;
    /** The world model resolved: an owned copy of its name (NULL: none), and the asset (borrowed under the collision
     *  world's user; NULL when there is none or it failed, which is not retried until the name changes). */
    unsigned char* WorldModelName;
    const WorldModel* WorldModel;
    /** B01, a terrain instance: its terrain resolved as the world model is (an owned copy of the name and the asset);
     *  a record per chunk of its mesh (an owned block, which the shapes point into: replaced only with them) for the
     *  layout revision they were cut from; and the mesh's revision they were last compared with (TerrainMesh_GetRevision:
     *  while it is unchanged no chunk changed). */
    unsigned char* TerrainName;
    const TerrainAsset* TerrainAsset;
    WorldCollisionTerrainChunk* TerrainChunks;
    uint32_t TerrainChunkCount;
    uint32_t SeenTerrainLayoutRevision;
    uint32_t SeenTerrainRevision;
    /** Whether the shapes are in the trees, and in which. */
    bool IsActive;
    bool IsStatic;
    /** Whether its object hangs on a socket of its parent (T30: a crate in someone's hands), as of the last sync: a
     *  pushable held so is its holder's (WorldCollisionInternal_IsPushable). Kept here so queries never follow the
     *  collider pointer, which an object removed since the sync has freed. */
    bool IsOnSocket;
    /** T27, kept beside IsActive and IsStatic (the step's passes over every instance read little else of a static one):
     *  its shapes were rebuilt, or it was placed for the first time, since the last step (it wakes what is near it); a
     *  pushable that stays awake into the next step (left overlapping beyond the slop); whether it was blocked or crushed
     *  after the last step (the motion events' edges). */
    bool IsChanged;
    bool IsAwake;
    bool WasBlocked;
    bool WasCrushed;
    WorldColliderSettings Settings;
    /** The collider's own primitive, while Settings.UsesOwnShape: an owned block, allocated the first time, so its
     *  address survives the instances' buffer growing (its shape points at it); NULL before. */
    CollisionShape* OwnShape;
    /** WorldCollisionShape elements. */
    GenericBuffer Shapes;
    /** The object's world placement and the inverse of its axes (local contact positions). */
    WorldCollisionPlacement ObjectToWorld;
    CollisionAffine WorldToObjectAxes;
    /** Set while the component set is re-matched: whether a collider still names this instance. */
    bool IsMatched;
    /** Warnings given once per instance. */
    bool HasWarnedNoShapes;
    bool HasWarnedSet;
    bool HasWarnedStaticMove;
    bool HasWarnedFlatShape;
    bool HasWarnedPushableShape;
    bool HasWarnedWriteBack;
    /** B01: a terrain chunk whose collision mesh could not be built; on a collider's instance, a terrain component on
     *  the same object (the collider wins). */
    bool HasWarnedTerrainChunk;
    bool HasWarnedTerrainIgnored;

    /* T27: motion. */
    /** The motion frame now (rigid: the object's world position, its turn by the rotation mode), and at the end of the
     *  last step (valid while HasLastPose: from the instance's first placement; cleared when it leaves the trees). */
    WorldCollisionPose Pose;
    WorldCollisionPose LastPose;
    bool HasLastPose;
    /** How far its valid shapes reach from the pose's origin (a turn sweeps them within it), metres. */
    float Reach;
    /** The slot of the pushable instance that owns it this step (itself, or the nearest pushable up its object's chain),
     *  or WORLD_COLLISION_NO_SLOT (always for a static one); and a pushable's nearest pushable strict ancestor's slot
     *  (its push is written back relative to that one's; it is woken with it), or WORLD_COLLISION_NO_SLOT. */
    uint32_t OwnerSlot;
    uint32_t PushableAboveSlot;
    /** Its body in the running step, or WORLD_COLLISION_NO_BODY. */
    uint32_t Body;
    /** A kinematic mover set back this step (when_blocked stop): it takes part in no later block round. */
    bool IsStoppedThisStep;
    /** Its tree proxies were widened by this step (they go back to its shapes' boxes at the step's end). */
    bool IsWidened;
    /** A kinematic mover that stops when blocked: the local transforms along its object's chain at the end of the last
     *  step, ChainCount records from ChainStart in _chainRecords (0: none, and after a rebuild). */
    uint32_t ChainStart;
    uint32_t ChainCount;
    /** Its feedback, stamped with the step that filled it (WorldCollisionInternal_TouchResult; an older stamp reads as
     *  empty). */
    WorldMotionResult Result;
} WorldCollisionInstance;

/** An overlapping trigger pair. */
typedef struct WorldCollisionTriggerPairStruct
{
    uint64_t TriggerId;
    uint64_t OtherId;
} WorldCollisionTriggerPair;

/** An object's slot, for lookups by id. */
typedef struct WorldCollisionLookupStruct
{
    uint64_t ObjectId;
    uint32_t Slot;
} WorldCollisionLookup;

/** What a body is. */
typedef enum WorldCollisionBodyKindEnum
{
    WorldCollisionBodyKind_Static,
    WorldCollisionBodyKind_Kinematic,
    WorldCollisionBodyKind_Pushable
} WorldCollisionBodyKind;

/** One instance taking part in a step. */
typedef struct WorldCollisionBodyStruct
{
    uint32_t Slot;
    WorldCollisionBodyKind Kind;
    /** The body whose push moves this one: itself for a pushable, its pushable owner's for a collider that one carries,
     *  WORLD_COLLISION_NO_BODY for the rest. */
    uint32_t Owner;
    /** A pushable's nearest pushable strict ancestor's body (its push is written relative to that one's), or
     *  WORLD_COLLISION_NO_BODY. */
    uint32_t ParentPushable;
    /** 1 / mass of its owner (0 without one). */
    float InverseMass;
    /** Its own motion this step: the pose at the last step's end and the pose now (equal when it does not move). */
    WorldCollisionPose StepFrom;
    WorldCollisionPose StepTo;
    bool IsMoving;
    float Turn;
    /** Whether its motion is swept (its collider's is_continuous; a body that does not move counts as swept). */
    bool IsContinuous;
    /** The running solve's motion: From (its start, the pushes so far included) to To (its target, without the solve's
     *  push); Final = To + the owner's Correction. PhaseTurn is the turn from From to To (To's rotation times From's
     *  inverse), while IsPhaseTurning. */
    WorldCollisionPose From;
    WorldCollisionPose To;
    Quaternion PhaseTurn;
    float PhaseTurnAngle;
    bool IsPhaseTurning;
    /** The running solve's earliest unresolved sweep hit of an owner (1: none): its motion stops just before it. */
    float ClampFraction;
    /** Scratch of the blocked check: the body's island (union-find parent) and, at an island's root, its deepest
     *  violation. */
    uint32_t Island;
    float IslandDepth;
    /** Owners: the running solve's push, and the pushes of the step so far; the push at the start of the running
     *  solver iteration (its convergence test). */
    Vector3 Correction;
    Vector3 Accumulated;
    Vector3 IterationCorrection;
    /** The box of its shapes over the step's motion (with the reach of a turn), for pairs; inverted (Min above Max)
     *  for a body without a bounded shape. */
    BoundingBoxDouble Swept;
    /** Kinematic movers: blocked by a pushable (which, how deep), and whether it was set back (stop). */
    bool IsBlocked;
    uint64_t BlockedById;
    float BlockDepth;
    bool IsStopped;
    /** Pushables (owners): left overlapping beyond the slop (how deep, with what). */
    bool IsCrushed;
    float CrushDepth;
    uint64_t CrushedById;
} WorldCollisionBody;

/** A sweep's hit that no round resolved: the body whose motion hit, the other body, and when. */
typedef struct WorldCollisionSweepHitStruct
{
    uint32_t Body;
    uint32_t OtherBody;
    float Fraction;
} WorldCollisionSweepHit;

/** A pair of shapes the solver looks at: A is convex, and owned by a pushable whenever either side is. */
typedef struct WorldCollisionPairStruct
{
    uint32_t SlotA;
    uint32_t ShapeA;
    uint32_t SlotB;
    uint32_t ShapeB;
    uint32_t BodyA;
    uint32_t BodyB;
    /** Their objects (the sort key with the shapes). */
    uint64_t ObjectA;
    uint64_t ObjectB;
    /** Swept: contacts before the motion and sweeps (not when a side that moves is not continuous). */
    bool IsSwept;
} WorldCollisionPair;

/** One non-penetration constraint: a contact point of a pair of shapes (A's and B's, as their slots and shape indices). */
typedef struct WorldCollisionRowStruct
{
    uint32_t SlotA;
    uint32_t ShapeA;
    uint32_t SlotB;
    uint32_t ShapeB;
    /** The owners whose pushes it moves (WORLD_COLLISION_NO_BODY: none) and their inverse masses. */
    uint32_t OwnerA;
    uint32_t OwnerB;
    float InverseMassA;
    float InverseMassB;
    /** From A to B. */
    Vector3 Normal;
    /** The separation after the solve's motions, before any push. */
    float Base;
    /** The push so far (at least 0). */
    float Push;
    /** B's triangle (WORLD_COLLISION_NO_TRIANGLE: none) and the point's feature id. */
    uint32_t Triangle;
    uint32_t FeatureId;
} WorldCollisionRow;

/** A contact of a pair of shapes at the poses of a solve (narrowphase points, A to B in the pair's orientation). */
typedef struct WorldCollisionManifoldStruct
{
    uint32_t SlotA;
    uint32_t ShapeA;
    uint32_t SlotB;
    uint32_t ShapeB;
    uint32_t BodyA;
    uint32_t BodyB;
    /** B's triangle, or WORLD_COLLISION_NO_TRIANGLE. */
    uint32_t Triangle;
    /** The contact, its points relative to Origin (the narrowphase ran there). */
    CollisionContact Contact;
    Vector3Double Origin;
} WorldCollisionManifold;

/** The key of a contact across steps: the objects (A the lower id), their shapes and B's triangle... or A's: the
 *  triangle of whichever shape is a mesh. */
typedef struct WorldCollisionContactKeyStruct
{
    uint64_t ObjectA;
    uint64_t ObjectB;
    uint32_t ShapeA;
    uint32_t ShapeB;
    uint32_t Triangle;
} WorldCollisionContactKey;

/** The push a step put on one contact point (rows summed over its solves). */
typedef struct WorldCollisionPushStruct
{
    WorldCollisionContactKey Key;
    uint32_t FeatureId;
    float Push;
} WorldCollisionPush;

/** Where a pair of shapes started the step: the separation of its contact before the motion, and how fast its drivers'
 *  motions closed it along the normal (metres per step). */
typedef struct WorldCollisionStartStruct
{
    WorldCollisionContactKey Key;
    float Separation;
    float Closing;
} WorldCollisionStart;

/** An object pair in contact. */
typedef struct WorldCollisionContactPairStruct
{
    uint64_t ObjectA;
    uint64_t ObjectB;
    uint32_t AgeTicks;
    float ApproachSpeed;
    float TimeOfImpact;
    /** Whether either collider reports persisting contacts. */
    bool ReportsPersisting;
} WorldCollisionContactPair;

/** An object's local transform and its parent (0: none), recorded at the end of a step. */
typedef struct WorldCollisionChainRecordStruct
{
    uint64_t ObjectId;
    uint64_t ParentId;
    /** In double, as the object holds it: set back exactly. */
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
} WorldCollisionChainRecord;

struct WorldCollisionStruct
{
    World* _world;
    AssetManager* _assets;
    AssetUserID _assetUser;
    Logger* _logger;
    WorldCollisionOptions _options;
    /** WorldCollisionInstance slots and the free list's head. */
    GenericBuffer _instances;
    uint32_t _freeSlot;
    /** uint32_t: each collider's slot, in the world's collider order (valid for _componentSetVersion). */
    GenericBuffer _order;
    /** WorldCollisionLookup, sorted by object id. */
    GenericBuffer _lookup;
    bool _hasComponentSetVersion;
    uint64_t _componentSetVersion;
    CollisionTree _staticTree;
    CollisionTree _movableTree;
    /** uint64_t keys of the unbounded planes. */
    GenericBuffer _planes;
    /** WorldCollisionTriggerPair, sorted: this tick's and the next's. */
    GenericBuffer _triggerPairs;
    GenericBuffer _newTriggerPairs;
    /** WorldTriggerEvent, queued until cleared. */
    GenericBuffer _triggerEvents;
    /** uint64_t keys a query collects. */
    GenericBuffer _candidates;
    /** WorldCollisionDebugShape, while the list is enabled. */
    bool _isDebugListEnabled;
    GenericBuffer _debugShapes;
    uint64_t _tick;
    uint64_t _rebuildCount;
    uint64_t _moveCount;
    /** B01: how many terrain chunks' collision meshes were built. */
    uint64_t _terrainChunkBuildCount;

    /* T27: the step. */
    float _deltaSeconds;
    /** The tick of the last step (results stamped with another tick read as empty). */
    uint64_t _stepTick;
    /** uint64_t object ids teleported since the last step. */
    GenericBuffer _teleports;
    /** WorldCollisionBody of the running step, uint32_t slots waiting in the wake-up walk, and uint32_t slots of the
     *  instances that hang under a pushable in collider order: the colliders it carries (owned by it) and the pushables
     *  under it (woken with it). */
    GenericBuffer _bodies;
    GenericBuffer _queue;
    GenericBuffer _carried;
    /** WorldCollisionPair, sorted by objects and shapes; WorldCollisionRow of the running solve; WorldCollisionSweepHit
     *  of its sweeps. */
    GenericBuffer _pairs;
    GenericBuffer _rows;
    GenericBuffer _rowScratch;
    GenericBuffer _sweepHits;
    /** WorldCollisionManifold: the last solve's contacts at the final poses. */
    GenericBuffer _manifolds;
    /** WorldCollisionPush and WorldCollisionStart of the step, sorted at its end. */
    GenericBuffer _pushes;
    GenericBuffer _starts;
    /** WorldContact: this step's (sorted by key) and the last one's. */
    GenericBuffer _contacts;
    GenericBuffer _previousContacts;
    /** WorldCollisionContactPair: the object pairs in contact (sorted), and the next step's. */
    GenericBuffer _contactPairs;
    GenericBuffer _newContactPairs;
    /** Queued WorldContactEvent, the WorldContact copies they index, and queued WorldMotionEvent. */
    GenericBuffer _contactEvents;
    GenericBuffer _eventContacts;
    GenericBuffer _motionEvents;
    /** WorldCollisionChainRecord of the kinematic movers that stop. */
    GenericBuffer _chainRecords;
    /** WorldCollisionDebugContact, while enabled. */
    bool _isDebugContactsEnabled;
    GenericBuffer _debugContacts;
    /** The last step's counts (WorldCollisionStats' T27 part). */
    WorldCollisionStats _stepStats;
};


// Functions.
static inline uint64_t WorldCollisionInternal_MakeKey(uint32_t slot, uint32_t shape)
{
    return ((uint64_t)slot << 32U) | (uint64_t)shape;
}

static inline uint32_t WorldCollisionInternal_GetKeySlot(uint64_t key)
{
    return (uint32_t)(key >> 32U);
}

static inline uint32_t WorldCollisionInternal_GetKeyShape(uint64_t key)
{
    return (uint32_t)(key & 0xFFFFFFFFULL);
}

static inline WorldCollisionInstance* WorldCollisionInternal_GetInstance(const WorldCollision* self, uint32_t slot)
{
    return &((WorldCollisionInstance*)(void*)self->_instances._data)[slot];
}

static inline WorldCollisionShape* WorldCollisionInternal_GetShape(const WorldCollisionInstance* instance,
    uint32_t index)
{
    return &((WorldCollisionShape*)(void*)instance->Shapes._data)[index];
}

/* The component an instance stands for: its collider's, or its terrain's. */
static inline WorldComponent* WorldCollisionInternal_GetComponent(const WorldCollisionInstance* instance)
{
    return (instance->Collider != NULL) ? &instance->Collider->Base : &instance->Terrain->Base;
}

/* The slot of an object's instance (used or not); false when it has none. */
bool WorldCollisionInternal_FindSlot(const WorldCollision* self, uint64_t objectId, uint32_t* outSlot);

/* Whether a query with @p filter sees an instance (active, its layer in the mask, the trigger rule, not ignored). An
 * ignored subtree is read from the live hierarchy through the world, by id: queries may run between a removal and the
 * next sync, when the instance's collider and object may be gone. */
bool WorldCollisionInternal_PassesFilter(WorldCollision* self, const WorldCollisionInstance* instance,
    const WorldCollisionFilter* filter);

/* Whether a candidate the trees found (by fattened or widened boxes) really reaches @p box: an instance that takes part
 * in the running step always does (its tree boxes cover its motion), any other one by its shape's own box (an unbounded
 * plane reaches everything). */
static inline bool WorldCollisionInternal_IsCandidateNear(const WorldCollisionInstance* instance,
    const WorldCollisionShape* shape, BoundingBoxDouble box)
{
    if ((instance->Body != WORLD_COLLISION_NO_BODY) || shape->IsUnbounded)
    {
        return true;
    }
    return BoundingBoxDouble_Overlaps(shape->Box, box);
}


// Functions: doubles outside, floats inside (T33).
/* A placement relative to @p origin, for the narrowphase: its axes, and its translation less the origin, subtracted in
 * double and rounded once. */
static inline CollisionAffine WorldCollisionInternal_ToRelative(const WorldCollisionPlacement* placement,
    Vector3Double origin)
{
    return (CollisionAffine){ .X = placement->X, .Y = placement->Y, .Z = placement->Z,
        .Translation = GameMath_ToVector3Relative(placement->Translation, origin) };
}

/* The inverse of a placement relative to @p origin, from the inverse of its axes (@p inverseAxes, no translation): a
 * point relative to the origin goes to inverseAxes * (point - (translation - origin)). */
static inline CollisionAffine WorldCollisionInternal_ToRelativeInverse(const WorldCollisionPlacement* placement,
    const CollisionAffine* inverseAxes, Vector3Double origin)
{
    CollisionAffine Result = *inverseAxes;
    Result.Translation = CollisionMath_Negate(CollisionAffine_TransformVector(inverseAxes,
        GameMath_ToVector3Relative(placement->Translation, origin)));
    return Result;
}

/* A shape's synced placement both ways, relative to @p origin. */
static inline void WorldCollisionInternal_PlaceShapeRelative(const WorldCollisionShape* shape, Vector3Double origin,
    CollisionAffine* outToWorld, CollisionAffine* outFromWorld)
{
    *outToWorld = WorldCollisionInternal_ToRelative(&shape->ShapeToWorld, origin);
    *outFromWorld = WorldCollisionInternal_ToRelativeInverse(&shape->ShapeToWorld, &shape->WorldToShapeAxes, origin);
}

/* Where a world position lies in an object's space (its placement's inverse): the position less the object's
 * translation, subtracted in double, then turned by the inverse axes. */
static inline Vector3 WorldCollisionInternal_ToObjectSpace(const WorldCollisionInstance* instance, Vector3Double point)
{
    return CollisionAffine_TransformVector(&instance->WorldToObjectAxes, GameMath_ToVector3Relative(point,
        instance->ObjectToWorld.Translation));
}

/* A pose relative to @p origin, for the narrowphase. */
static inline CollisionPose WorldCollisionInternal_ToRelativePose(const WorldCollisionPose* pose, Vector3Double origin)
{
    return (CollisionPose){ .Position = GameMath_ToVector3Relative(pose->Position, origin),
        .Rotation = pose->Rotation };
}

static inline bool WorldCollisionInternal_ArePosesEqual(const WorldCollisionPose* a, const WorldCollisionPose* b)
{
    return (a->Position.X == b->Position.X) && (a->Position.Y == b->Position.Y) && (a->Position.Z == b->Position.Z)
        && (a->Rotation.x == b->Rotation.x) && (a->Rotation.y == b->Rotation.y) && (a->Rotation.z == b->Rotation.z)
        && (a->Rotation.w == b->Rotation.w);
}

/* A box relative to @p origin (floats, from the narrowphase's maths) as a world box. */
static inline BoundingBoxDouble WorldCollisionInternal_ToWorldBox(Vector3 min, Vector3 max, Vector3Double origin)
{
    return (BoundingBoxDouble){ .Min = Vector3Double_Add(origin, GameMath_FromVector3(min)),
        .Max = Vector3Double_Add(origin, GameMath_FromVector3(max)) };
}

/* A box with nothing in it (Min above Max): the start of a union. */
static inline BoundingBoxDouble WorldCollisionInternal_GetEmptyBox(void)
{
    return (BoundingBoxDouble){ .Min = { .X = DBL_MAX, .Y = DBL_MAX, .Z = DBL_MAX },
        .Max = { .X = -DBL_MAX, .Y = -DBL_MAX, .Z = -DBL_MAX } };
}

static inline bool WorldCollisionInternal_IsBoxEmpty(BoundingBoxDouble box)
{
    return !(box.Min.X <= box.Max.X);
}

/* The pose at the share @p t of the motion from @p from to @p to: the position lerped in double, the rotation as
 * CollisionPose_Interpolate turns it (a slerp along the shorter arc). */
WorldCollisionPose WorldCollisionInternal_InterpolatePose(const WorldCollisionPose* from, const WorldCollisionPose* to,
    float t);

/* The angle, in radians in [0, pi], the motion from @p from to @p to turns through. */
float WorldCollisionInternal_GetTurn(const WorldCollisionPose* from, const WorldCollisionPose* to);

/* Fills what a result says about an instance's shape (no triangle). */
void WorldCollisionInternal_FillShapeInfo(const WorldCollisionInstance* instance, const WorldCollisionShape* shape,
    WorldCollisionShapeInfo* outInfo);

/* Fills what a result says about a triangle of an instance's mesh shape (its index, model part, source triangle and
 * material; not the barycentrics). A terrain's part is its chunk and its source triangle the terrain's (B01). */
void WorldCollisionInternal_FillTriangleInfo(const WorldCollisionInstance* instance, const WorldCollisionShape* shape,
    uint32_t triangle, WorldCollisionShapeInfo* info);

/* Collects into _candidates the keys of every shape whose box overlaps @p box (both trees) and every unbounded
 * plane. */
void WorldCollisionInternal_CollectCandidates(WorldCollision* self, BoundingBoxDouble box);

/* The trigger step of a sync: this tick's pairs against the last tick's, their events queued. */
void WorldCollisionInternal_UpdateTriggers(WorldCollision* self);

/* Whether two instances' shapes overlap (touching counts), or come within @p margin of each other. */
bool WorldCollisionInternal_DoInstancesTouch(WorldCollision* self, const WorldCollisionInstance* a,
    const WorldCollisionInstance* b, float margin);


// Functions: the sync (source/WorldCollision.c).
/* The sync without the triggers: world matrices, the instances' match to the colliders, each instance in step with its
 * collider and object (poses included). */
void WorldCollisionInternal_SyncShapes(WorldCollision* self);

/* Rebuilds the debug list of shapes, if it is enabled. */
void WorldCollisionInternal_RebuildDebugList(WorldCollision* self);

/* Puts every valid shape's tree proxy back on its box (after a step widened them to swept boxes). */
void WorldCollisionInternal_RestoreProxy(WorldCollision* self, WorldCollisionInstance* instance);

/* Widens an instance's shape proxies to @p box (a step's swept box) so the trees find it on its way. */
void WorldCollisionInternal_WidenProxies(WorldCollision* self, WorldCollisionInstance* instance, BoundingBoxDouble box);

/* Widens the proxies of an instance's shapes that a push of @p shift (from where the sync placed them) takes past their
 * tree boxes, to cover where it takes them. */
void WorldCollisionInternal_WidenPushedProxies(WorldCollision* self, WorldCollisionInstance* instance, Vector3 shift);

/* Logs a warning about an instance's object (once: the caller keeps the flag). */
void WorldCollisionInternal_Warn(const WorldCollision* self, const WorldCollisionInstance* instance, const char* text);


// Functions: the step (source/WorldCollisionStep.c).
/* Whether an instance is a pushable this step: active, pushable, movable and not a trigger. */
bool WorldCollisionInternal_IsPushable(const WorldCollisionInstance* instance);

/* Whether two instances' colliders block each other: neither a trigger, the layers both ways, not one owner. */
bool WorldCollisionInternal_DoBlock(const WorldCollisionInstance* a, const WorldCollisionInstance* b);

/* Whether a blocked kinematic mover is set back: it stops when blocked, was not set back yet this step, and its object's
 * chain changed since the last step (in the same hierarchy: a reparented object cannot go back). */
bool WorldCollisionInternal_CanStopMover(const WorldCollision* self, const WorldCollisionInstance* instance);

/* The body of an instance in the running step, made when it has none (a static or still kinematic one met on the way, or
 * a sleeping pushable that is woken). */
uint32_t WorldCollisionInternal_GetBody(WorldCollision* self, uint32_t slot);

static inline WorldCollisionBody* WorldCollisionInternal_GetBodyAt(const WorldCollision* self, uint32_t body)
{
    return &((WorldCollisionBody*)(void*)self->_bodies._data)[body];
}

/* An instance's result for the running step: emptied the first time the step writes it (a step leaves untouched the
 * results of everything it did not reach, which read as empty since their stamp is older). */
static inline WorldMotionResult* WorldCollisionInternal_TouchResult(const WorldCollision* self,
    WorldCollisionInstance* instance)
{
    if (instance->Result.Tick != self->_stepTick)
    {
        Memory_Zero(&instance->Result, sizeof(instance->Result));
        instance->Result.Tick = self->_stepTick;
    }
    return &instance->Result;
}

/* Whether an instance's result is the last step's and says blocked (or, with @p isCrushed, crushed). */
static inline bool WorldCollisionInternal_IsFlagged(const WorldCollision* self, const WorldCollisionInstance* instance,
    bool isCrushed)
{
    return (instance->Result.Tick == self->_stepTick) && (isCrushed ? instance->Result.IsCrushed
        : instance->Result.IsBlocked);
}

/* The key of a contact between shape @p shapeA of the instance in @p slotA and @p shapeB of @p slotB on @p triangle (A the
 * lower object id), and whether the given A is the key's B. */
WorldCollisionContactKey WorldCollisionInternal_GetContactKey(const WorldCollision* self, uint32_t slotA,
    uint32_t shapeA, uint32_t slotB, uint32_t shapeB, uint32_t triangle, bool* outIsSwapped);

/* Orders contact keys: objects, then shapes, then triangles. */
int WorldCollisionInternal_CompareContactKeys(const WorldCollisionContactKey* a, const WorldCollisionContactKey* b);


// Functions: the solver (source/WorldCollisionSolver.c).
/* Solves the step's bodies: substeps of speculative contacts, the push-out and the sweeps, then the depenetration at the
 * final poses; leaves each owner's Accumulated push, the blocked and crushed flags, and _manifolds at the final poses.
 * With @p mayStopMovers it ends early once a blocked mover will be set back (the step solves again); without, it runs
 * through (the step's last round). */
void WorldCollisionInternal_Solve(WorldCollision* self, bool mayStopMovers);


// Functions: events (source/WorldCollisionEvents.c).
/* The step's contacts from _manifolds (and the unchanged ones carried over), matched to the last step's; their contact
 * events; the results' contact counts and grounds; the debug contacts. */
void WorldCollisionInternal_UpdateContacts(WorldCollision* self);

/* Queues an instance's blocked and crushed events of the step (the states that began or ended since the last). */
void WorldCollisionInternal_QueueMotionEvents(WorldCollision* self, const WorldCollisionInstance* instance);
