#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "CollisionShape.h"
#include "PathGround.h"
#include "PathObject.h"
#include "PathObjectLayout.h"
#include "PathObjectPlacement.h"
#include "WorldColliderComponent.h"
#include "WorldGeneratedModel.h"
#include "WorldModel.h"
#include "WorldObject.h"
#include "WorldPathComponent.h"
#include "WorldPathObjectComponent.h"
#include "WorldPathObjects.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/) of the path objects system: what its files share. WorldPathObjects.c follows the components and runs
 * the rebuilds, WorldPathObjectsRigid.c stands pieces on the ground and places the rigid ones, WorldPathObjectsBent.c bends the
 * others, and WorldPathObjectsChildren.c writes what came out on the runtime children.
 */


// Macros.
/** The most models one slot draws from: a list of more is cut off, with a warning. */
#define WORLD_PATH_OBJECTS_MAX_SLOT_MODELS 64U
/** The most copies one piece makes: both sides. */
#define WORLD_PATH_OBJECTS_MAX_PIECE_SIDES 2U
/** How much wider than itself a mitre makes what crosses a sharp corner, at most. */
#define WORLD_PATH_OBJECTS_MAX_CORNER_STRETCH 4.0
/** How near a corner's distance a point counts as at it, in metres. */
#define WORLD_PATH_OBJECTS_CORNER_TOLERANCE 1.0e-6


// Types.
/* What a child holds. */
typedef enum PathObjectChildKindEnum
{
    /* A slot's and model's rigid copies and its placed collision shapes. */
    PathObjectChildKind_Copies,
    /* One stretch of a slot's and model's bent pieces. */
    PathObjectChildKind_Part
} PathObjectChildKind;

/* A runtime child of a path object's object. */
typedef struct PathObjectChildRecordStruct
{
    uint64_t ObjectId;
    PathObjectChildKind Kind;
    PathObjectSlot Slot;
    uint32_t Model;
    uint32_t Part;
    /* Marked while a rebuild uses it; what stays unmarked goes. */
    bool IsUsed;
} PathObjectChildRecord;

/* What the system remembers of one component. */
typedef struct PathObjectStateStruct
{
    uint64_t Serial;
    uint64_t ObjectId;
    WorldPathWatch Watch;
    /* Marked while the states are matched to the components; what stays unmarked goes. */
    bool IsListed;
    /* What the last rebuild saw. */
    bool HasBuilt;
    uint32_t SeenRevision;
    uint32_t SeenTransformRevision;
    uint64_t SeenGroundRevision;
    RenderColor SeenTint;
    /* Whether anything was snapped, to terrain alone or to more, and the area its points cover in the world. */
    bool HasSnapped;
    bool IsTerrainOnly;
    BoundingBoxDouble GroundArea;
    /* The asset resolved: an owned copy of its name (NULL: none) and the asset (borrowed under the system's user; NULL when it
     * failed, which is not retried until the name changes). */
    unsigned char* AssetName;
    const PathObject* Asset;
    /* PathObjectChildRecord. */
    GenericBuffer Children;
    /* What it holds now. */
    size_t PieceCount;
    size_t CopyCount;
    size_t ShapeCount;
    size_t GeneratedModelCount;
    size_t TriangleCount;
    size_t VertexCount;
    /* What was warned about, once each. */
    bool HasWarnedAsset;
    bool HasWarnedLimit;
    bool HasWarnedModel;
    bool HasWarnedZeroMiddle;
    bool HasWarnedBudget;
    bool HasWarnedShapes;
    bool HasWarnedCopies;
    bool HasWarnedChildren;
    bool HasWarnedGround;
    bool HasWarnedPlace;
} PathObjectState;

/* A world model a slot can pick, resolved for this rebuild. */
typedef struct PathObjectSlotModelStruct
{
    const WorldModel* Asset;
    const PathObjectModelRecord* Record;
    PathObjectMeasure Measure;
    float Length;
    /* The last part made of its bent pieces, in this rebuild: parts come in rising order along the path. */
    bool HasParts;
    uint32_t LastPart;
} PathObjectSlotModel;

/* A slot's settings and models in effect for this rebuild. */
typedef struct PathObjectSlotBuildStruct
{
    PathObjectSlotRecord Settings;
    PathObjectSlotModel Models[WORLD_PATH_OBJECTS_MAX_SLOT_MODELS];
    PathObjectLayoutModel LayoutModels[WORLD_PATH_OBJECTS_MAX_SLOT_MODELS];
    uint32_t ModelCount;
    bool IsUsed;
} PathObjectSlotBuild;

/* One stretch of bent pieces being built: its place and each LOD's meshes (a builder per source mesh). */
typedef struct PathObjectBentPartStruct
{
    PathObjectSlot Slot;
    uint32_t Model;
    uint32_t Part;
    /* The stretch's place in the object's space: what its vertices are relative to. */
    Vector3Double Center;
    /* PathObjectBentMesh[], per LOD: one per mesh of that LOD's model. */
    PathObjectBentMesh* Meshes[WORLD_MODEL_MAX_LODS];
    uint32_t MeshCounts[WORLD_MODEL_MAX_LODS];
} PathObjectBentPart;

/* One slice of a bent piece before it is made relative to its part: in the object's space, in double. */
typedef struct PathObjectBentPlaceStruct
{
    Vector3Double Position;
    float MeshX;
    Vector3 AxisX;
    Vector3 AxisY;
    Vector3 AxisZ;
} PathObjectBentPlace;

/* A point of a bent piece: its distance from the span's start and where it lies. */
typedef struct PathObjectBentEndStruct
{
    double Distance;
    PathObjectPieceEnd End;
} PathObjectBentEnd;

/* What every slice of one bent copy shares. */
typedef struct PathObjectBentPieceInfoStruct
{
    const PathObjectPiece* Piece;
    const PathObjectSpan* Span;
    const PathObjectSlotRecord* Settings;
    const PathObjectSlotModel* Model;
    bool IsTurned;
    /* Its side: which of the piece's copies, and its sign. */
    uint32_t SideIndex;
    float SideSign;
    double PieceLength;
    /* The part of the mesh's X it shows, and the piece's length over it. */
    float KeptStart;
    float KeptLength;
    float Stretch;
    float SizeJitter;
    /* Its offset and jitter: along it, up and to its right. */
    Vector3 Offset;
} PathObjectBentPieceInfo;

/* A world model's measure, kept for the system's life (a world model does not change while it is held). */
typedef struct PathObjectMeasuredModelStruct
{
    const WorldModel* Asset;
    PathObjectMeasure Measure;
} PathObjectMeasuredModel;

/* One rebuild's data. */
typedef struct PathObjectRebuildStruct
{
    WorldPathObjects* System;
    PathObjectState* State;
    WorldPathObjectComponent* Component;
    WorldObject* Owner;
    const PathCurve* Curve;
    PathObjectSettings Settings;
    PathObjectSlotBuild* Slots;
    /* The object's world matrix and the inverse of its axes, for the ground. */
    Matrix4Double ToWorld;
    Matrix4Double ToLocalAxes;
    bool HasInverse;
    PathGroundSettings Ground;
    bool HasAnyChange;
    /* What it may still make, and how much more it may look at. */
    size_t VertexBudget;
    size_t ShapeBudget;
    size_t BentWorkLeft;
    uint64_t GroundWorkLeft;
    /* What one look for the ground costs: a ray, and the path object's own shapes it may pass over. */
    uint64_t GroundCost;
} PathObjectRebuild;

/* A section's sample distances (double): the pieces of a section share them. A slot has one spacing in a rebuild. */
typedef struct PathObjectSampleCacheStruct
{
    GenericBuffer Samples;
    uint32_t Section;
} PathObjectSampleCache;

/* Where two sections of a span's run meet. What stands off to a side turns the corner by its mitre, so the two sides meet
 * there. */
typedef struct PathObjectSpanCornerStruct
{
    /* From the span's start. */
    double Distance;
    /* The two rights' middle, as long as makes the two sides meet: a unit long where the path runs straight on. */
    Vector3 Mitre;
    /* The tangents it arrives and leaves with. */
    Vector3 Arriving;
    Vector3 Leaving;
    /* The node's width factor. */
    float Width;
} PathObjectSpanCorner;

/* Where the build of one slot's and model's copies stands. */
typedef struct PathObjectCopyListStruct
{
    /* PropertyInstanceEntry */
    GenericBuffer Copies;
    /* Placed shapes: CollisionShape and Matrix4Double, in step. */
    GenericBuffer Shapes;
    GenericBuffer Placements;
} PathObjectCopyList;

struct WorldPathObjectsStruct
{
    World* _world;
    WorldCollision* _collision;
    AssetManager* _assets;
    AssetUserID _assetUser;
    Logger* _logger;
    /* PathObjectState, in the world's component order, and where the next search for one starts. */
    GenericBuffer _states;
    size_t _stateCursor;
    uint64_t _componentSetVersion;
    bool _hasComponentSetVersion;
    /* PathObjectMeasuredModel. */
    GenericBuffer _measures;
    /* A layout, kept so its buffers are reused. */
    PathObjectLayout _layout;
    /* A cache of a section's samples per slot: a slot has one spacing, and pieces of two slots take turns along a section. */
    PathObjectSampleCache _sampleCaches[PathObjectSlot_Count];
    /* The corners (PathObjectSpanCorner, by their distances) of the span they were made for. */
    GenericBuffer _corners;
    const PathObjectSpan* _cornersSpan;
    /* Scratch of a rebuild: doubles (a bent piece's slice distances), its PathObjectBentEnd and PathObjectBentPlace, and
     * PathObjectBentPart. */
    GenericBuffer _distances;
    GenericBuffer _ends;
    GenericBuffer _places;
    GenericBuffer _parts;
    /* Scratch: the slot builds, which are large (a heap block, one rebuild at a time). */
    PathObjectSlotBuild* _slotBuilds;
    /* The copies of each slot and model: an array of WORLD_PATH_OBJECTS_MAX_SLOT_MODELS * slots lists, whose buffers are
     * reused. */
    PathObjectCopyList* _copyLists;
    WorldPathObjectsStats _stats;
    /* What every path object may make together; the vertices they hold (the pieces and the shapes are in the stats). */
    WorldPathObjectsLimits _limits;
    size_t _totalVertices;
};


// Functions: shared small things.
/** One warning about a path object's object. */
void WorldPathObjectsInternal_Warn(const WorldPathObjects* self, const WorldObject* object, const char* format, ...);

/** The list of a slot's and model's copies. */
static inline PathObjectCopyList* WorldPathObjectsInternal_GetCopyList(WorldPathObjects* self, PathObjectSlot slot,
    uint32_t model)
{
    return &self->_copyLists[((size_t)slot * WORLD_PATH_OBJECTS_MAX_SLOT_MODELS) + model];
}

/** Whether two tints are the same. */
static inline bool WorldPathObjectsInternal_AreTintsEqual(RenderColor a, RenderColor b)
{
    return (a.Tint.r == b.Tint.r) && (a.Tint.g == b.Tint.g) && (a.Tint.b == b.Tint.b) && (a.Brightness == b.Brightness)
        && (a.Opacity == b.Opacity);
}


// Functions: the ground and rigid pieces (WorldPathObjectsRigid.c).
/** A point along a span, off to its side and on the ground, with its frame there. False when the span has no such point. */
bool WorldPathObjectsRigid_MakeEnd(PathObjectRebuild* rebuild, const PathObjectSpan* span, double distance, float sideSign,
    float sideOffset, PathObjectPieceEnd* outEnd);

/** Fills the ends of a rigid piece: both on the curve, or a joint's around its node along the way it arrives. */
bool WorldPathObjectsRigid_MakePieceEnds(PathObjectRebuild* rebuild, const PathObjectPiece* piece, const PathObjectSpan* span,
    const PathObjectSlotBuild* slot, float sideSign, PathObjectPieceEnd* outStart, PathObjectPieceEnd* outEnd);

/** Whether a piece is turned end for end by its slot's flip. */
bool WorldPathObjectsRigid_IsPieceFlipped(const PathObjectRebuild* rebuild, const PathObjectPiece* piece,
    const PathObjectSpan* span,
    const PathObjectSlotRecord* slot);

/** The sides a piece stands on: their signs, +1 right and -1 left, 0 on the path. Returns how many. */
uint32_t WorldPathObjectsRigid_GetPieceSides(const PathObjectPiece* piece, const PathObjectSlotRecord* slot,
    float outSigns[WORLD_PATH_OBJECTS_MAX_PIECE_SIDES]);

/** The corner a span has at a distance, or NULL. */
const PathObjectSpanCorner* WorldPathObjectsRigid_FindCorner(PathObjectRebuild* rebuild, const PathObjectSpan* span,
    double distance);

/** Whether a point at a distance along a span, @p side metres off to its right (times the node's width), lies past where the two
 *  sides of a corner cross: inside a corner a side's two legs run over each other. Such a point has no place on the side's
 * line. */
bool WorldPathObjectsRigid_IsPastInnerCorner(PathObjectRebuild* rebuild, const PathObjectSpan* span, double distance,
    double side);

/** A piece's random value in [0, 1) for one of its side's jitter channels. */
float WorldPathObjectsRigid_GetJitter(const PathObjectRebuild* rebuild, const PathObjectPiece* piece, const PathObjectSpan* span,
    uint32_t side, uint32_t channel);

/** Appends the collision a copy has by its slot: the world model's set under the copy's matrix, or one box around it. */
void WorldPathObjectsRigid_AddCopyCollision(PathObjectRebuild* rebuild, PathObjectSlot slot, uint32_t modelIndex,
    const PropertyInstanceEntry* copy, const PathObjectSlotRecord* settings);

/** A rigid copy of a piece on one of its sides, into its list. */
void WorldPathObjectsRigid_AddCopy(PathObjectRebuild* rebuild, const PathObjectPiece* piece, const PathObjectSpan* span,
    const PathObjectSlotBuild* slot, uint32_t sideIndex, float sideSign);


// Functions: bent pieces (WorldPathObjectsBent.c).
/** One bent copy of a piece on one of its sides, into its parts and, for collision that is not its mesh, its list. */
void WorldPathObjectsBent_AddCopy(PathObjectRebuild* rebuild, const PathObjectPiece* piece, const PathObjectSpan* span,
    const PathObjectSlotBuild* slot, uint32_t sideIndex, float sideSign);

/** Whether a part holds nothing at any LOD: every triangle of its model lay outside what its stretches kept. */
bool WorldPathObjectsBent_IsPartEmpty(const PathObjectBentPart* part);

/** What one part came to: a generated model from its builders' arrays, which it takes over. */
WorldGeneratedModel* WorldPathObjectsBent_MakeGeneratedModel(const PathObjectRebuild* rebuild, const PathObjectBentPart* part);

/** Frees the parts being built. */
void WorldPathObjectsBent_FreeParts(WorldPathObjects* self);


// Functions: the runtime children (WorldPathObjectsChildren.c).
/** The child of a slot's and model's copies: its copies and placed shapes, written only when they differ. */
void WorldPathObjectsChildren_WriteCopies(PathObjectRebuild* rebuild, PathObjectSlot slot, uint32_t modelIndex);

/** The child of a part: its place, its rows and its generated model, written only when they differ. */
void WorldPathObjectsChildren_WritePart(PathObjectRebuild* rebuild, const PathObjectBentPart* part);

/** Marks every child unused; those a rebuild does not use go at its end. */
void WorldPathObjectsChildren_MarkUnused(PathObjectState* state);

/** Removes the children no rebuild used. */
void WorldPathObjectsChildren_RemoveUnused(WorldPathObjects* self, PathObjectState* state);
