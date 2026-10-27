#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldCollisionInternal.h"
#include "AssetManager.h"
#include "GameMath.h"
#include "Logger.h"
#include "World.h"
#include "WorldInternal.h"
#include "WorldModelComponent.h"
#include "WorldTerrainComponent.h"
#include "WorldTransform.h"
#include "raylib/raymath.h"

/*
 * The collision world (include/WorldCollision.h): lifetime, the sync, the debug list and the counts. Queries are in
 * source/WorldCollisionQuery.c, trigger pairs in source/WorldCollisionTrigger.c, the step in
 * source/WorldCollisionStep.c with source/WorldCollisionSolver.c and source/WorldCollisionEvents.c; the shared state in
 * source/WorldCollisionInternal.h.
 *
 * A SYNC walks the world's colliders, then its terrains, in its own order (_order maps each to its instance's
 * slot; re-matched by object id whenever the world's component set changes, so an added, removed or replaced component
 * is noticed) and for each: out of the trees when it or an ancestor is disabled; its shapes rebuilt when the collider
 * changed (its revision), its world model's name changed, it moved between the static and the movable tree, or it was
 * just (re)enabled; moved (new transforms, boxes and proxies) when only its object's transform revision changed. Either
 * way its motion pose follows: the rigid frame its shapes move with, each shape's place relative to it, and how
 * far they reach.
 *
 * A TERRAIN is an object's source when it has no collider (with both, the collider wins: warned once). Its
 * instance keeps a record per chunk of its terrain's mesh: the chunk's collision mesh (TerrainMesh.h,
 * TerrainMesh_CreateChunkCollisionMesh: the terrain's own active edges, so seams do not catch) in a one-sided mesh shape
 * at the chunk's centre, one shape per chunk in chunk order. The meshes are cut again when the terrain or its chunk
 * layout changes, and rebuilt one by one when a chunk's revision moves (the editor's changes); the shapes are placed
 * again when the object moves and rebuilt, the meshes kept, when a setting changes or the instance comes back. The
 * layer, the mask and "collides" are the terrain's settings in effect (WorldTerrainComponent_ResolveSettings).
 *
 * The placements come from the objects' double world matrices: each shape's transform is composed in double, its
 * axes rounded to floats and its translation kept in double (WorldCollisionPlacement), its box transformed in double.
 */


// Macros.
/** The first capacities of the buffers. */
#define INITIAL_INSTANCES 16U
#define INITIAL_CANDIDATES 64U
#define INITIAL_STEP_ITEMS 16U
/** A shape's place relative to its pose changed when an entry moved by more than this share of its size (at least 1):
 *  above the rounding of a turn's decomposition, far below any scale a creator sets. */
#define SHAPE_FRAME_TOLERANCE 1e-5f


// Static functions: messages.
static const char* GetObjectName(const WorldObject* object)
{
    const unsigned char* Name = WorldObject_GetName(object);
    return (Name != NULL) ? (const char*)Name : "";
}

/* Logs a warning about an instance's object (once: the caller keeps the flag): the problem's text before a quoted name
 * (left out when NULL) and after it, then the cause's message, if any. */
static void WarnAbout(const WorldCollision* self, const WorldCollisionInstance* instance, const char* before,
    const unsigned char* name, const char* after, Error* cause)
{
    if (self->_logger != NULL)
    {
        const WorldObject* Object = WorldComponent_GetOwner(WorldCollisionInternal_GetComponent(instance));
        bool HasCause = (cause != NULL) && (cause->Message != NULL);
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"WorldCollision: object %" PRIu64 u8" (\"%s\") %s%s%s%s%s%s%s", instance->ObjectId,
            GetObjectName(Object), before, (name != NULL) ? " \"" : "", (name != NULL) ? (const char*)name : "",
            (name != NULL) ? "\"" : "", after, HasCause ? ": " : ".", HasCause ? (const char*)cause->Message : "");
        Error_Deconstruct(&LogResult);
    }
    if (cause != NULL)
    {
        Error_Deconstruct(cause);
    }
}


// Static functions: slots.
static uint32_t AllocateSlot(WorldCollision* self)
{
    uint32_t Slot = self->_freeSlot;
    if (Slot != WORLD_COLLISION_NO_SLOT)
    {
        self->_freeSlot = WorldCollisionInternal_GetInstance(self, Slot)->NextFree;
    }
    else
    {
        WorldCollisionInstance Empty;
        Memory_Zero(&Empty, sizeof(Empty));
        GenericBuffer_AddLast(&self->_instances, &Empty);
        Slot = (uint32_t)(self->_instances._count - 1U);
    }
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
    Memory_Zero(Instance, sizeof(*Instance));
    Instance->IsUsed = true;
    Instance->NextFree = WORLD_COLLISION_NO_SLOT;
    Instance->OwnerSlot = WORLD_COLLISION_NO_SLOT;
    Instance->PushableAboveSlot = WORLD_COLLISION_NO_SLOT;
    Instance->Body = WORLD_COLLISION_NO_BODY;
    GenericBuffer_AllocateVariable(&Instance->Shapes, 1U, sizeof(WorldCollisionShape));
    return Slot;
}

/* Takes an instance's shapes out of the trees and the planes list. */
static void Deactivate(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot)
{
    // Out of the world, it has no motion to sweep: when it comes back it starts where it is then.
    instance->HasLastPose = false;
    if (!instance->IsActive)
    {
        return;
    }
    CollisionTree* Tree = instance->IsStatic ? &self->_staticTree : &self->_movableTree;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        if (Shape->Proxy != COLLISION_TREE_NULL)
        {
            CollisionTree_Remove(Tree, Shape->Proxy);
            Shape->Proxy = COLLISION_TREE_NULL;
        }
    }
    uint64_t* Planes = (uint64_t*)(void*)self->_planes._data;
    for (size_t Index = self->_planes._count; Index > 0U; Index--)
    {
        if (WorldCollisionInternal_GetKeySlot(Planes[Index - 1U]) == slot)
        {
            GenericBuffer_RemoveAt(&self->_planes, Index - 1U);
        }
    }
    instance->IsActive = false;
}

/* Releases a terrain instance's chunk records and their meshes, and the shapes that point into them (which must be out
 * of the trees). */
static void FreeTerrainChunks(WorldCollisionInstance* instance)
{
    GenericBuffer_Clear(&instance->Shapes);
    for (uint32_t Chunk = 0U; Chunk < instance->TerrainChunkCount; Chunk++)
    {
        CollisionMesh_Destroy(instance->TerrainChunks[Chunk].Mesh);
    }
    Memory_Free(instance->TerrainChunks);
    instance->TerrainChunks = NULL;
    instance->TerrainChunkCount = 0U;
}

static void FreeSlot(WorldCollision* self, uint32_t slot)
{
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, slot);
    // A collider that goes while blocked or crushed ends that state (a BlockedEnd or CrushedEnd motion event).
    WorldMotionEvent Ends[2] =
    {
        { .Kind = WorldMotionEventKind_BlockedEnd, .ObjectId = Instance->ObjectId, .Tick = self->_tick },
        { .Kind = WorldMotionEventKind_CrushedEnd, .ObjectId = Instance->ObjectId, .Tick = self->_tick }
    };
    if (Instance->WasBlocked)
    {
        GenericBuffer_AddLast(&self->_motionEvents, &Ends[0]);
    }
    if (Instance->WasCrushed)
    {
        GenericBuffer_AddLast(&self->_motionEvents, &Ends[1]);
    }
    Deactivate(self, Instance, slot);
    FreeTerrainChunks(Instance);
    Memory_Free(Instance->Shapes._data);
    Memory_Free(Instance->WorldModelName);
    Memory_Free(Instance->TerrainName);
    Memory_Free(Instance->OwnShape);
    Memory_Zero(Instance, sizeof(*Instance));
    Instance->NextFree = self->_freeSlot;
    self->_freeSlot = slot;
}

static ComparisonResult CompareLookups(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    const WorldCollisionLookup* A = a._element;
    const WorldCollisionLookup* B = b._element;
    return (A->ObjectId < B->ObjectId) ? ComparisonResult_ALessThanB
        : ((A->ObjectId > B->ObjectId) ? ComparisonResult_AGreaterThanB : ComparisonResult_AEqualsB);
}

/* Finds or makes the instance of a source (a collider, or a terrain: the other is NULL) by its object's id and appends
 * it to the order. A replaced component starts its instance afresh; a source of the other kind (a terrain whose object
 * gained a collider) gets a new instance. Returns the slot. */
static uint32_t MatchSource(WorldCollision* self, WorldComponent* component, WorldColliderComponent* collider,
    WorldTerrainComponent* terrain)
{
    uint64_t ObjectId = WorldObject_GetId(WorldComponent_GetOwner(component));
    uint64_t Serial = WorldComponent_GetSerial(component);
    uint32_t Slot = WORLD_COLLISION_NO_SLOT;
    bool IsKnown = WorldCollisionInternal_FindSlot(self, ObjectId, &Slot)
        && WorldCollisionInternal_GetInstance(self, Slot)->IsUsed;
    if (IsKnown && ((WorldCollisionInternal_GetInstance(self, Slot)->Collider != NULL) != (collider != NULL)))
    {
        FreeSlot(self, Slot);
        IsKnown = false;
    }
    if (!IsKnown)
    {
        Slot = AllocateSlot(self);
        WorldCollisionInstance* Fresh = WorldCollisionInternal_GetInstance(self, Slot);
        Fresh->ObjectId = ObjectId;
        Fresh->Collider = collider;
        Fresh->Terrain = terrain;
        Fresh->ComponentSerial = Serial;
    }
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
    if ((WorldCollisionInternal_GetComponent(Instance) != component) || (Instance->ComponentSerial != Serial))
    {
        // A replaced component (often at its predecessor's address): nothing the old one resolved or saw carries
        // over.
        Deactivate(self, Instance, Slot);
        Instance->Collider = collider;
        Instance->Terrain = terrain;
        Instance->ComponentSerial = Serial;
        Instance->HasSeen = false;
        Instance->HasWarnedSet = false;
        Instance->HasWarnedNoShapes = false;
        Instance->HasWarnedFlatShape = false;
        Instance->HasWarnedTerrainChunk = false;
        Instance->HasWarnedTerrainIgnored = false;
    }
    Instance->IsMatched = true;
    GenericBuffer_AddLast(&self->_order, &Slot);
    return Slot;
}

/* Matches the world's colliders, then its terrains, to instances by object id after its component set changed: new
 * sources get slots, a replaced component starts its instance afresh, instances whose source is gone are freed. A
 * terrain on an object with a collider is no source: the collider wins (warned once, on the collider's instance). */
static void Rematch(WorldCollision* self)
{
    size_t Count = 0U;
    WorldComponent* const* Colliders = World_GetComponents(self->_world, WorldComponentType_Collider, &Count);
    for (uint32_t Slot = 0U; Slot < (uint32_t)self->_instances._count; Slot++)
    {
        WorldCollisionInternal_GetInstance(self, Slot)->IsMatched = false;
    }
    GenericBuffer_Clear(&self->_order);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        uint32_t Slot = MatchSource(self, Colliders[Index], WorldColliderComponent_FromComponent(Colliders[Index]),
            NULL);
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        bool HasTerrain = WorldObject_GetComponent(WorldComponent_GetOwner(Colliders[Index]), WorldComponentType_Terrain)
            != NULL;
        if (HasTerrain && !Instance->HasWarnedTerrainIgnored)
        {
            Instance->HasWarnedTerrainIgnored = true;
            WarnAbout(self, Instance, "has a collider and a terrain: it collides as its collider says, and its terrain "
                "does not collide", NULL, "", NULL);
        }
    }
    size_t TerrainCount = 0U;
    WorldComponent* const* Terrains = World_GetComponents(self->_world, WorldComponentType_Terrain, &TerrainCount);
    for (size_t Index = 0U; Index < TerrainCount; Index++)
    {
        if (WorldObject_GetComponent(WorldComponent_GetOwner(Terrains[Index]), WorldComponentType_Collider) == NULL)
        {
            MatchSource(self, Terrains[Index], NULL, WorldTerrainComponent_FromComponent(Terrains[Index]));
        }
    }
    for (uint32_t Slot = 0U; Slot < (uint32_t)self->_instances._count; Slot++)
    {
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        if (Instance->IsUsed && !Instance->IsMatched)
        {
            FreeSlot(self, Slot);
        }
    }
    GenericBuffer_Clear(&self->_lookup);
    for (uint32_t Slot = 0U; Slot < (uint32_t)self->_instances._count; Slot++)
    {
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        if (Instance->IsUsed)
        {
            WorldCollisionLookup Entry = { .ObjectId = Instance->ObjectId, .Slot = Slot };
            GenericBuffer_AddLast(&self->_lookup, &Entry);
        }
    }
    WorldCollisionLookup Scratch[2];
    GenericBuffer_SortAscending(&self->_lookup, CompareLookups, NULL, Scratch);
    self->_componentSetVersion = World_GetComponentSetVersion(self->_world);
    self->_hasComponentSetVersion = true;
}


// Static functions: placements.
/* A double matrix as a placement: its axes rounded to floats, its translation kept. */
static WorldCollisionPlacement MakePlacement(const Matrix4Double* matrix)
{
    return (WorldCollisionPlacement)
    {
        .X = { (float)matrix->M[0], (float)matrix->M[1], (float)matrix->M[2] },
        .Y = { (float)matrix->M[4], (float)matrix->M[5], (float)matrix->M[6] },
        .Z = { (float)matrix->M[8], (float)matrix->M[9], (float)matrix->M[10] },
        .Translation = Matrix4Double_GetTranslation(matrix)
    };
}

/* A placement as a double matrix (the debug list's transforms). */
static Matrix4Double MakePlacementMatrix(const WorldCollisionPlacement* placement)
{
    Matrix4Double Result = Matrix4Double_Identity();
    const Vector3 Axes[3] = { placement->X, placement->Y, placement->Z };
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        Result.M[Axis * 4U] = (double)Axes[Axis].x;
        Result.M[(Axis * 4U) + 1U] = (double)Axes[Axis].y;
        Result.M[(Axis * 4U) + 2U] = (double)Axes[Axis].z;
    }
    Matrix4Double_SetTranslation(&Result, placement->Translation);
    return Result;
}

/* The inverse of a placement's axes (no translation); false (and the identity) when they are singular or nearly so. */
static bool TryInvertAxes(const WorldCollisionPlacement* placement, CollisionAffine* outInverse)
{
    CollisionAffine Axes = { .X = placement->X, .Y = placement->Y, .Z = placement->Z,
        .Translation = { 0.0f, 0.0f, 0.0f } };
    return CollisionAffine_TryInvert(&Axes, outInverse);
}


// Static functions: shapes.
/* The object and every ancestor static: the instance belongs in the static tree. */
static bool IsEffectivelyStatic(const WorldObject* object)
{
    for (const WorldObject* Current = object; Current != NULL; Current = WorldObject_GetParent(Current))
    {
        if (WorldObject_GetMobility(Current) != WorldMobility_Static)
        {
            return false;
        }
    }
    return true;
}

/* The frame an instance's shapes hang in, in the world, in double: for a collider the object's world matrix by the
 * rotation mode, then the collider's own transform; for a terrain (no collider) the object's world matrix. yaw_only and
 * none rebuild the frame from the world transform's decomposition (a mirror lives in its scale, so the turn they keep is
 * a proper rotation) with the object's scale along the turned (or the world's) axes. The frame's rigid part is the
 * motion's pose: the object's world position, turned as the shapes turn. */
static Matrix4Double GetInstanceFrame(const WorldCollisionInstance* instance, const WorldObject* object,
    const Matrix4Double* objectWorld, WorldCollisionPose* outPose)
{
    const WorldColliderComponent* Collider = instance->Collider;
    Matrix4Double Frame = *objectWorld;
    WorldColliderRotationMode Mode = (Collider != NULL) ? WorldColliderComponent_GetRotationMode(Collider)
        : WorldColliderRotationMode_Full;
    Vector3Double Position;
    Vector3 Rotation;
    Vector3 Scale;
    WorldObject_GetWorldTransform(object, &Position, &Rotation, &Scale);
    Vector3 Turn = Rotation;
    if (Mode != WorldColliderRotationMode_Full)
    {
        float Yaw = 0.0f;
        if (Mode == WorldColliderRotationMode_YawOnly)
        {
            // The yaw of the turn's forward (local +Z), or of its right (local +X) when it points straight up or down.
            Matrix4Double Turned = WorldTransform_Compose(Vector3Double_Zero(), Rotation,
                (Vector3){ 1.0f, 1.0f, 1.0f });
            bool HasForward = ((Turned.M[8] * Turned.M[8]) + (Turned.M[10] * Turned.M[10])) > 1e-12;
            Yaw = (float)(HasForward ? atan2(Turned.M[8], Turned.M[10]) : atan2(-Turned.M[2], Turned.M[0]));
        }
        Turn = (Vector3){ 0.0f, Yaw, 0.0f };
        Frame = WorldTransform_Compose(Position, Turn, Scale);
    }
    outPose->Position = Matrix4Double_GetTranslation(objectWorld);
    outPose->Rotation = QuaternionNormalize(QuaternionFromMatrix(MatrixRotateXYZ(Turn)));
    if (Collider == NULL)
    {
        return Frame;
    }
    Matrix4Double Local = WorldColliderComponent_GetLocalTransform(Collider);
    return Matrix4Double_Multiply(&Local, &Frame);
}

/* Whether a shape's place relative to its instance's pose changed beyond rounding. */
static bool IsShapeFrameChanged(const CollisionAffine* before, const CollisionAffine* after)
{
    Vector3 Before[4] = { before->X, before->Y, before->Z, before->Translation };
    Vector3 After[4] = { after->X, after->Y, after->Z, after->Translation };
    for (size_t Column = 0U; Column < 4U; Column++)
    {
        float Size = fmaxf(1.0f, CollisionMath_MaxAbsComponent(Before[Column]));
        if (CollisionMath_MaxAbsComponent(CollisionMath_Subtract(After[Column], Before[Column]))
            > (SHAPE_FRAME_TOLERANCE * Size))
        {
            return true;
        }
    }
    return false;
}

/* Places each shape relative to the instance's pose and finds how far the valid ones reach from its origin.
 * Returns whether a valid shape's place relative to the pose changed (a scale or a shear: no motion of the pose shows
 * it). The shapes are taken relative to the pose's position first, in double, so the place holds far out. */
static bool UpdatePoseShapes(WorldCollisionInstance* instance)
{
    CollisionPose Turn = { .Position = { 0.0f, 0.0f, 0.0f }, .Rotation = instance->Pose.Rotation };
    CollisionAffine TurnToWorld = CollisionPose_ToAffine(&Turn);
    CollisionAffine WorldToPose;
    CollisionAffine_TryInvert(&TurnToWorld, &WorldToPose);
    float Reach = 0.0f;
    bool IsReshaped = false;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        CollisionAffine Before = Shape->ShapeToPose;
        CollisionAffine ShapeToWorld = WorldCollisionInternal_ToRelative(&Shape->ShapeToWorld, instance->Pose.Position);
        Shape->ShapeToPose = CollisionAffine_Multiply(&WorldToPose, &ShapeToWorld);
        IsReshaped = IsReshaped || (Shape->IsValid && IsShapeFrameChanged(&Before, &Shape->ShapeToPose));
        Vector3 LocalMin;
        Vector3 LocalMax;
        if (!Shape->IsValid || !CollisionShape_TryGetLocalBounds(Shape->Shape, &LocalMin, &LocalMax))
        {
            continue; // an unbounded plane reaches everything: the sweeps treat it apart
        }
        for (uint32_t Corner = 0U; Corner < 8U; Corner++)
        {
            Vector3 Local = { ((Corner & 1U) != 0U) ? LocalMax.x : LocalMin.x, ((Corner & 2U) != 0U) ? LocalMax.y
                : LocalMin.y, ((Corner & 4U) != 0U) ? LocalMax.z : LocalMin.z };
            Reach = fmaxf(Reach, CollisionMath_Length(CollisionAffine_TransformPoint(&Shape->ShapeToPose, Local)));
        }
    }
    instance->Reach = Reach;
    return IsReshaped;
}

/* A shape's placement and box under the collider's frame (composed in double); false when the frame flattens it. */
static bool PlaceShape(WorldCollisionShape* shape, const Matrix4Double* colliderToWorld)
{
    Matrix4Double ShapeTransform = GameMath_FromMatrix(CollisionShape_GetTransform(shape->Shape));
    Matrix4Double ShapeToWorld = Matrix4Double_Multiply(&ShapeTransform, colliderToWorld);
    shape->ShapeToWorld = MakePlacement(&ShapeToWorld);
    bool IsValid = TryInvertAxes(&shape->ShapeToWorld, &shape->WorldToShapeAxes);
    shape->IsUnbounded = (shape->Shape->Type == CollisionShapeType_Plane) && !shape->Shape->IsBounded;
    if (IsValid && !shape->IsUnbounded)
    {
        Vector3 LocalMin;
        Vector3 LocalMax;
        CollisionShape_TryGetLocalBounds(shape->Shape, &LocalMin, &LocalMax);
        BoundingBoxDouble Local = { .Min = GameMath_FromVector3(LocalMin), .Max = GameMath_FromVector3(LocalMax) };
        shape->Box = BoundingBoxDouble_Transform(Local, &ShapeToWorld);
        IsValid = Vector3Double_IsFinite(shape->Box.Min) && Vector3Double_IsFinite(shape->Box.Max);
    }
    return IsValid;
}

static void UpdateObjectTransform(WorldCollisionInstance* instance, const Matrix4Double* objectWorld)
{
    instance->ObjectToWorld = MakePlacement(objectWorld);
    if (!TryInvertAxes(&instance->ObjectToWorld, &instance->WorldToObjectAxes))
    {
        instance->WorldToObjectAxes = CollisionAffine_Identity();
    }
}

/* Puts an instance's valid shapes into its tree (or the planes list). */
static void Activate(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot)
{
    CollisionTree* Tree = instance->IsStatic ? &self->_staticTree : &self->_movableTree;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        Shape->Proxy = COLLISION_TREE_NULL;
        if (!Shape->IsValid)
        {
            continue;
        }
        uint64_t Key = WorldCollisionInternal_MakeKey(slot, Index);
        if (Shape->IsUnbounded)
        {
            GenericBuffer_AddLast(&self->_planes, &Key);
            continue;
        }
        Shape->Proxy = CollisionTree_Insert(Tree, Shape->Box, Key);
    }
    instance->IsActive = true;
}

/* The shapes the settings in effect name: the collider's own, a set of the world model, or none. */
static const CollisionShape* GetSourceShapes(WorldCollisionInstance* instance, uint32_t* outCount)
{
    *outCount = 0U;
    if (instance->Settings.UsesOwnShape)
    {
        if (instance->OwnShape == NULL)
        {
            instance->OwnShape = Memory_Allocate(sizeof(CollisionShape));
        }
        *instance->OwnShape = WorldColliderComponent_GetOwnShape(instance->Collider);
        *outCount = 1U;
        return instance->OwnShape;
    }
    if (!instance->Settings.HasSet || (instance->WorldModel == NULL))
    {
        return NULL;
    }
    const CollisionSet* Set = WorldModelCollision_GetSet(WorldModel_GetCollision(instance->WorldModel),
        instance->Settings.SetIndex);
    if (Set == NULL)
    {
        return NULL;
    }
    *outCount = CollisionSet_GetShapeCount(Set);
    return CollisionSet_GetShape(Set, 0U);
}

/* The end of a rebuild: the tree the instance belongs in, its shapes' places relative to its pose, the step's view of it
 * (changed; a static one starts every step where it is, unowned) and its shapes into the tree. */
static void FinishRebuild(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot, bool isStatic,
    bool hasShapes)
{
    instance->IsStatic = isStatic;
    UpdatePoseShapes(instance);
    instance->IsChanged = true;
    instance->ChainCount = 0U; // a chain recorded before may name other objects now
    if (isStatic)
    {
        // A static collider never sweeps and nothing pushes it: it starts every step where it is, unowned.
        instance->LastPose = instance->Pose;
        instance->HasLastPose = true;
        instance->OwnerSlot = WORLD_COLLISION_NO_SLOT;
        instance->PushableAboveSlot = WORLD_COLLISION_NO_SLOT;
    }
    if (hasShapes)
    {
        Activate(self, instance, slot);
    }
    self->_rebuildCount++;
}

static void RebuildTerrainShapes(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot, bool isStatic);

/* Rebuilds an instance's shapes from its settings in effect and puts them into the right tree. Its motion goes on: the
 * next step sweeps the new shapes from where the last step left the instance. */
static void Rebuild(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot, bool isStatic)
{
    if (instance->Terrain != NULL)
    {
        RebuildTerrainShapes(self, instance, slot, isStatic);
        return;
    }
    bool HadLastPose = instance->HasLastPose;
    Deactivate(self, instance, slot);
    instance->HasLastPose = HadLastPose;
    GenericBuffer_Clear(&instance->Shapes);
    const WorldModelCollision* Collision = (instance->WorldModel != NULL) ? WorldModel_GetCollision(instance->WorldModel)
        : NULL;
    WorldColliderComponent_ResolveSettings(instance->Collider, Collision, &instance->Settings);
    if (instance->Settings.IsSetMissing && !instance->HasWarnedSet)
    {
        instance->HasWarnedSet = true;
        WarnAbout(self, instance, "has no collision: its world model has no collision set",
            WorldColliderComponent_GetCollisionSet(instance->Collider), "", NULL);
    }
    uint32_t Count = 0U;
    const CollisionShape* Shapes = GetSourceShapes(instance, &Count);
    if ((Count == 0U) && !instance->Settings.IsSetMissing && !instance->HasWarnedNoShapes && (instance->WorldModel != NULL))
    {
        instance->HasWarnedNoShapes = true;
        WarnAbout(self, instance, "has a collider but no shapes: it uses no own shape and its world model",
            instance->WorldModelName, " has no collision section", NULL);
    }
    const WorldObject* Object = WorldComponent_GetOwner(&instance->Collider->Base);
    Matrix4Double ObjectWorld = WorldObject_GetWorldMatrix(Object);
    UpdateObjectTransform(instance, &ObjectWorld);
    Matrix4Double Frame = GetInstanceFrame(instance, Object, &ObjectWorld, &instance->Pose);
    bool HasFlatShape = false;
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        WorldCollisionShape Shape;
        Memory_Zero(&Shape, sizeof(Shape));
        Shape.Shape = &Shapes[Index];
        Shape.ShapeIndex = Index;
        Shape.Proxy = COLLISION_TREE_NULL;
        Shape.IsSizeValid = true;
        if (instance->Settings.UsesOwnShape)
        {
            // An own shape's sizes come from the rows: a cylinder of half height 0 is flat.
            Error Check = CollisionShape_Validate(Shape.Shape);
            Shape.IsSizeValid = Check.Code == ErrorCode_Success;
            Error_Deconstruct(&Check);
        }
        Shape.IsValid = PlaceShape(&Shape, &Frame) && Shape.IsSizeValid;
        HasFlatShape = HasFlatShape || !Shape.IsValid;
        GenericBuffer_AddLast(&instance->Shapes, &Shape);
    }
    if (HasFlatShape && !instance->HasWarnedFlatShape)
    {
        instance->HasWarnedFlatShape = true;
        WarnAbout(self, instance, "has a collision shape that its transform flattens (a zero scale) or that has no "
            "size; it collides with nothing", NULL, "", NULL);
    }
    FinishRebuild(self, instance, slot, isStatic, Count > 0U);
}

/* Moves an instance's shapes with its object (the trees re-insert what left its box). */
static void Move(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot)
{
    const WorldObject* Object = WorldComponent_GetOwner(WorldCollisionInternal_GetComponent(instance));
    Matrix4Double ObjectWorld = WorldObject_GetWorldMatrix(Object);
    UpdateObjectTransform(instance, &ObjectWorld);
    Matrix4Double Frame = GetInstanceFrame(instance, Object, &ObjectWorld, &instance->Pose);
    CollisionTree* Tree = instance->IsStatic ? &self->_staticTree : &self->_movableTree;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        bool WasValid = Shape->IsValid;
        bool IsValid = PlaceShape(Shape, &Frame) && Shape->IsSizeValid;
        if (IsValid != WasValid)
        {
            // A shape became flat or unflattened: rebuild the whole instance (rare).
            Rebuild(self, instance, slot, instance->IsStatic);
            return;
        }
        if (IsValid && (Shape->Proxy != COLLISION_TREE_NULL))
        {
            CollisionTree_Move(Tree, Shape->Proxy, Shape->Box);
        }
    }
    // A shape whose place relative to the pose changed (a scale) moved in a way no motion of the pose shows: the
    // instance is changed, so the step wakes what is near it and pushes it out.
    if (UpdatePoseShapes(instance))
    {
        instance->IsChanged = true;
    }
    if (instance->IsStatic)
    {
        // A static object never moves in play: its move is a teleport, with no sweep.
        instance->IsChanged = true;
        instance->LastPose = instance->Pose;
        instance->HasLastPose = true;
        if (self->_options.WarnsWhenStaticObjectsMove && !instance->HasWarnedStaticMove)
        {
            instance->HasWarnedStaticMove = true;
            WarnAbout(self, instance, (instance->Terrain != NULL) ? "is static but moved; its terrain was moved in the "
                "static tree (mark the object movable if it moves in play)" : "is static but moved; its collider was "
                "moved in the static tree (mark the object movable if it moves in play)", NULL, "", NULL);
        }
    }
    self->_moveCount++;
}

/* Resolves an instance's world model for a new name (a failure is warned about once and not retried until the name
 * changes again). */
static void ResolveWorldModel(WorldCollision* self, WorldCollisionInstance* instance, const unsigned char* name)
{
    Memory_Free(instance->WorldModelName);
    instance->WorldModelName = WorldInternal_CloneString(name);
    instance->WorldModel = NULL;
    instance->HasWarnedNoShapes = false;
    instance->HasWarnedSet = false;
    if ((name == NULL) || (self->_assets == NULL))
    {
        return;
    }
    WorldModel* Asset = NULL;
    Error Result = AssetManager_LoadWorldModel(self->_assets, name, self->_assetUser, &Asset);
    if (Result.Code == ErrorCode_Success)
    {
        instance->WorldModel = Asset;
        return;
    }
    WarnAbout(self, instance, "has no collision from its world model", name, "", &Result);
}

// Static functions: terrains.
/* Resolves a terrain instance's terrain for a new name (a failure is warned about once and not retried until the name
 * changes again). The old terrain's chunk records go: they were cut from its mesh. */
static void ResolveTerrain(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot,
    const unsigned char* name)
{
    Deactivate(self, instance, slot);
    FreeTerrainChunks(instance);
    Memory_Free(instance->TerrainName);
    instance->TerrainName = WorldInternal_CloneString(name);
    instance->TerrainAsset = NULL;
    instance->HasWarnedTerrainChunk = false;
    if ((name == NULL) || (self->_assets == NULL))
    {
        return;
    }
    TerrainAsset* Asset = NULL;
    Error Result = AssetManager_LoadTerrain(self->_assets, name, self->_assetUser, &Asset);
    if (Result.Code == ErrorCode_Success)
    {
        instance->TerrainAsset = Asset;
        return;
    }
    WarnAbout(self, instance, "has no collision from its terrain", name, "", &Result);
}

/* Builds one chunk's record from the terrain's mesh: its collision mesh (none for a chunk of holes only; a chunk whose
 * mesh cannot be built has none either, warned once) in a one-sided mesh shape at the chunk's centre. */
static void BuildTerrainChunk(WorldCollision* self, WorldCollisionInstance* instance, const TerrainMesh* mesh,
    uint32_t chunk)
{
    WorldCollisionTerrainChunk* Record = &instance->TerrainChunks[chunk];
    const TerrainChunk* Source = TerrainMesh_GetChunk(mesh, chunk);
    CollisionMesh_Destroy(Record->Mesh);
    Record->Mesh = NULL;
    Error Result = TerrainMesh_CreateChunkCollisionMesh(mesh, chunk, &Record->Mesh);
    if ((Result.Code != ErrorCode_Success) && !instance->HasWarnedTerrainChunk)
    {
        instance->HasWarnedTerrainChunk = true;
        char Text[96];
        snprintf(Text, sizeof(Text), "has a terrain chunk (chunk %" PRIu32 ") without collision, which lets everything "
            "through", chunk);
        WarnAbout(self, instance, Text, NULL, "", &Result);
    }
    else
    {
        Error_Deconstruct(&Result);
    }
    Record->Shape = CollisionShape_Create(CollisionShapeType_Mesh);
    // On the position grid, within the extent: exact in float.
    Vector3Double Center = TerrainChunk_GetCenter(Source);
    Record->Shape.Center = (Vector3){ (float)Center.X, (float)Center.Y, (float)Center.Z };
    Record->Shape.Mesh = Record->Mesh;
    Record->Shape.IsTwoSided = false;
    Record->Revision = TerrainChunk_GetRevision(Source);
    self->_terrainChunkBuildCount++;
}

/* Cuts a terrain instance's chunk records afresh from its terrain's mesh, one per chunk (its old shapes, which point
 * into the old records, go first; its motion goes on). */
static void BuildTerrainChunks(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot,
    const TerrainMesh* mesh)
{
    bool HadLastPose = instance->HasLastPose;
    Deactivate(self, instance, slot);
    instance->HasLastPose = HadLastPose;
    FreeTerrainChunks(instance);
    uint32_t Count = TerrainMesh_GetChunkCount(mesh);
    instance->TerrainChunks = Memory_Allocate(((Count > 0U) ? (size_t)Count : 1U) * sizeof(WorldCollisionTerrainChunk));
    Memory_Zero(instance->TerrainChunks, ((Count > 0U) ? (size_t)Count : 1U) * sizeof(WorldCollisionTerrainChunk));
    instance->TerrainChunkCount = Count;
    for (uint32_t Chunk = 0U; Chunk < Count; Chunk++)
    {
        BuildTerrainChunk(self, instance, mesh, Chunk);
    }
    instance->SeenTerrainLayoutRevision = TerrainMesh_GetLayoutRevision(mesh);
    instance->SeenTerrainRevision = TerrainMesh_GetRevision(mesh);
}

/* Places a terrain instance's shapes afresh, one per chunk record (shape index = chunk), and puts them into the right
 * tree. Its motion goes on, as a collider's rebuild. */
static void RebuildTerrainShapes(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot, bool isStatic)
{
    bool HadLastPose = instance->HasLastPose;
    Deactivate(self, instance, slot);
    instance->HasLastPose = HadLastPose;
    GenericBuffer_Clear(&instance->Shapes);
    const WorldObject* Object = WorldComponent_GetOwner(&instance->Terrain->Base);
    Matrix4Double ObjectWorld = WorldObject_GetWorldMatrix(Object);
    UpdateObjectTransform(instance, &ObjectWorld);
    Matrix4Double Frame = GetInstanceFrame(instance, Object, &ObjectWorld, &instance->Pose);
    bool HasFlatShape = false;
    for (uint32_t Chunk = 0U; Chunk < instance->TerrainChunkCount; Chunk++)
    {
        WorldCollisionShape Shape;
        Memory_Zero(&Shape, sizeof(Shape));
        Shape.Shape = &instance->TerrainChunks[Chunk].Shape;
        Shape.ShapeIndex = Chunk;
        Shape.Proxy = COLLISION_TREE_NULL;
        Shape.IsSizeValid = instance->TerrainChunks[Chunk].Mesh != NULL;
        bool IsPlaced = PlaceShape(&Shape, &Frame);
        Shape.IsValid = IsPlaced && Shape.IsSizeValid;
        HasFlatShape = HasFlatShape || (Shape.IsSizeValid && !IsPlaced);
        GenericBuffer_AddLast(&instance->Shapes, &Shape);
    }
    if (HasFlatShape && !instance->HasWarnedFlatShape)
    {
        instance->HasWarnedFlatShape = true;
        WarnAbout(self, instance, "has a terrain that its transform flattens (a zero scale); it collides with nothing",
            NULL, "", NULL);
    }
    FinishRebuild(self, instance, slot, isStatic, instance->TerrainChunkCount > 0U);
}

/* Rebuilds the collision meshes of the chunks whose revision moved. With @p isPlacing (the instance is in the trees and
 * keeps its other shapes) each rebuilt chunk's shape is placed again and put back into its tree, and the instance is
 * changed (the step wakes what is near it); otherwise the caller rebuilds every shape. */
static void UpdateTerrainChunks(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot,
    const TerrainMesh* mesh, bool isPlacing)
{
    const WorldObject* Object = WorldComponent_GetOwner(&instance->Terrain->Base);
    Matrix4Double ObjectWorld = WorldObject_GetWorldMatrix(Object);
    WorldCollisionPose Pose;
    Matrix4Double Frame = GetInstanceFrame(instance, Object, &ObjectWorld, &Pose);
    CollisionTree* Tree = instance->IsStatic ? &self->_staticTree : &self->_movableTree;
    bool IsAnyRebuilt = false;
    for (uint32_t Chunk = 0U; Chunk < instance->TerrainChunkCount; Chunk++)
    {
        if (instance->TerrainChunks[Chunk].Revision == TerrainChunk_GetRevision(TerrainMesh_GetChunk(mesh, Chunk)))
        {
            continue;
        }
        WorldCollisionShape* Shape = isPlacing ? WorldCollisionInternal_GetShape(instance, Chunk) : NULL;
        if ((Shape != NULL) && (Shape->Proxy != COLLISION_TREE_NULL))
        {
            CollisionTree_Remove(Tree, Shape->Proxy);
            Shape->Proxy = COLLISION_TREE_NULL;
        }
        BuildTerrainChunk(self, instance, mesh, Chunk);
        IsAnyRebuilt = true;
        if (Shape != NULL)
        {
            Shape->IsSizeValid = instance->TerrainChunks[Chunk].Mesh != NULL;
            Shape->IsValid = PlaceShape(Shape, &Frame) && Shape->IsSizeValid;
            if (Shape->IsValid)
            {
                Shape->Proxy = CollisionTree_Insert(Tree, Shape->Box, WorldCollisionInternal_MakeKey(slot, Chunk));
            }
        }
    }
    instance->SeenTerrainRevision = TerrainMesh_GetRevision(mesh);
    if (IsAnyRebuilt && isPlacing)
    {
        UpdatePoseShapes(instance);
        instance->IsChanged = true;
    }
}

/* A terrain's settings in effect as the settings a collider's instance has: its layer and mask, never a trigger nor
 * pushed (a movable terrain is a kinematic mover that stops when blocked, as a collider by default). */
static void SetTerrainSettings(WorldCollisionInstance* instance, const TerrainSettings* settings)
{
    Memory_Zero(&instance->Settings, sizeof(instance->Settings));
    instance->Settings.Layer = settings->Collision.Layer;
    instance->Settings.CollidesWith = settings->Collision.CollidesWith;
    instance->Settings.Mass = COLLISION_DEFAULT_MASS;
    instance->Settings.WhenBlocked = WorldColliderBlockedMode_Stop;
    instance->Settings.IsContinuous = true;
}

/* Brings a terrain's instance in step with its component, its terrain's mesh and its object (see the file comment). */
static void RefreshTerrain(WorldCollision* self, uint32_t slot)
{
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, slot);
    WorldTerrainComponent* Terrain = Instance->Terrain;
    const WorldObject* Object = WorldComponent_GetOwner(&Terrain->Base);
    Instance->IsOnSocket = WorldObject_GetParentSocket(Object) != NULL;
    const unsigned char* Name = WorldTerrainComponent_GetTerrain(Terrain);
    if (!WorldInternal_AreStringsEqual(Name, Instance->TerrainName))
    {
        ResolveTerrain(self, Instance, slot, Name);
        Instance->HasSeen = false;
    }
    TerrainSettings Defaults = TerrainSettings_CreateDefault();
    TerrainSettings Settings = WorldTerrainComponent_ResolveSettings(Terrain, (Instance->TerrainAsset != NULL)
        ? TerrainAsset_GetSettings(Instance->TerrainAsset) : &Defaults);
    if ((Instance->TerrainAsset == NULL) || !Settings.Collision.IsEnabled || !WorldObject_IsEffectivelyEnabled(Object))
    {
        // Out of the trees; its chunk meshes stay for when it comes back.
        Deactivate(self, Instance, slot);
        Instance->HasSeen = false;
        return;
    }
    const TerrainMesh* Ground = TerrainAsset_GetMesh(Instance->TerrainAsset);
    uint32_t ComponentRevision = WorldComponent_GetRevision(&Terrain->Base);
    uint32_t TransformRevision = WorldObject_GetTransformRevision(Object);
    bool IsStatic = IsEffectivelyStatic(Object);
    bool IsCut = (Instance->TerrainChunks != NULL)
        && (Instance->SeenTerrainLayoutRevision == TerrainMesh_GetLayoutRevision(Ground));
    bool NeedsShapes = !IsCut || !Instance->HasSeen || (ComponentRevision != Instance->SeenComponentRevision)
        || (IsStatic != Instance->IsStatic);
    if (!IsCut)
    {
        BuildTerrainChunks(self, Instance, slot, Ground);
    }
    else if (TerrainMesh_GetRevision(Ground) != Instance->SeenTerrainRevision)
    {
        UpdateTerrainChunks(self, Instance, slot, Ground, !NeedsShapes && Instance->IsActive);
    }
    if (NeedsShapes)
    {
        SetTerrainSettings(Instance, &Settings);
        RebuildTerrainShapes(self, Instance, slot, IsStatic);
    }
    else if ((TransformRevision != Instance->SeenTransformRevision) && Instance->IsActive)
    {
        Move(self, Instance, slot);
    }
    Instance->HasSeen = true;
    Instance->SeenComponentRevision = ComponentRevision;
    Instance->SeenTransformRevision = TransformRevision;
}


/* Brings one instance in step with its source and object. */
static void Refresh(WorldCollision* self, uint32_t slot)
{
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, slot);
    if (Instance->Terrain != NULL)
    {
        RefreshTerrain(self, slot);
        return;
    }
    WorldColliderComponent* Collider = Instance->Collider;
    const WorldObject* Object = WorldComponent_GetOwner(&Collider->Base);
    Instance->IsOnSocket = WorldObject_GetParentSocket(Object) != NULL;
    if (!WorldColliderComponent_IsEnabled(Collider) || !WorldObject_IsEffectivelyEnabled(Object))
    {
        Deactivate(self, Instance, slot);
        Instance->HasSeen = false;
        return;
    }
    const WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(WorldObject_GetComponent(Object,
        WorldComponentType_Model));
    const unsigned char* ModelName = (ModelComponent != NULL) ? WorldModelComponent_GetWorldModel(ModelComponent) : NULL;
    bool IsRenamed = !WorldInternal_AreStringsEqual(ModelName, Instance->WorldModelName);
    if (IsRenamed)
    {
        ResolveWorldModel(self, Instance, ModelName);
    }
    uint32_t ColliderRevision = WorldComponent_GetRevision(&Collider->Base);
    uint32_t TransformRevision = WorldObject_GetTransformRevision(Object);
    bool IsStatic = IsEffectivelyStatic(Object);
    bool NeedsRebuild = !Instance->HasSeen || IsRenamed || (ColliderRevision != Instance->SeenComponentRevision)
        || (IsStatic != Instance->IsStatic);
    if (NeedsRebuild)
    {
        Rebuild(self, Instance, slot, IsStatic);
    }
    else if ((TransformRevision != Instance->SeenTransformRevision) && Instance->IsActive)
    {
        // An instance without shapes has nothing to move (its next rebuild places it afresh).
        Move(self, Instance, slot);
    }
    Instance->HasSeen = true;
    Instance->SeenComponentRevision = ColliderRevision;
    Instance->SeenTransformRevision = TransformRevision;
}

static void RebuildDebugList(WorldCollision* self)
{
    GenericBuffer_Clear(&self->_debugShapes);
    for (uint32_t Slot = 0U; Slot < (uint32_t)self->_instances._count; Slot++)
    {
        const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        if (!Instance->IsUsed || !Instance->IsActive)
        {
            continue;
        }
        for (uint32_t Index = 0U; Index < (uint32_t)Instance->Shapes._count; Index++)
        {
            const WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(Instance, Index);
            if (!Shape->IsValid)
            {
                continue;
            }
            WorldCollisionDebugShape Debug = { .Shape = Shape->Shape, .Transform = MakePlacementMatrix(
                &Shape->ShapeToWorld), .Layer = Instance->Settings.Layer, .IsTrigger = Instance->Settings.IsTrigger,
                .IsStatic = Instance->IsStatic };
            GenericBuffer_AddLast(&self->_debugShapes, &Debug);
        }
    }
}


// Internal functions (WorldCollisionInternal.h).
bool WorldCollisionInternal_FindSlot(const WorldCollision* self, uint64_t objectId, uint32_t* outSlot)
{
    const WorldCollisionLookup* Entries = (const WorldCollisionLookup*)(const void*)self->_lookup._data;
    size_t Low = 0U;
    size_t High = self->_lookup._count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (Entries[Middle].ObjectId < objectId)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    if ((Low < self->_lookup._count) && (Entries[Low].ObjectId == objectId))
    {
        *outSlot = Entries[Low].Slot;
        return true;
    }
    return false;
}


void WorldCollisionInternal_SyncShapes(WorldCollision* self)
{
    World_UpdateTransforms(self->_world);
    if (!self->_hasComponentSetVersion || (World_GetComponentSetVersion(self->_world) != self->_componentSetVersion))
    {
        Rematch(self);
    }
    for (size_t Index = 0U; Index < self->_order._count; Index++)
    {
        Refresh(self, ((const uint32_t*)(const void*)self->_order._data)[Index]);
    }
}

void WorldCollisionInternal_WidenProxies(WorldCollision* self, WorldCollisionInstance* instance, BoundingBoxDouble box)
{
    instance->IsWidened = true;
    CollisionTree* Tree = instance->IsStatic ? &self->_staticTree : &self->_movableTree;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        if (Shape->Proxy != COLLISION_TREE_NULL)
        {
            CollisionTree_Move(Tree, Shape->Proxy, BoundingBoxDouble_Union(box, Shape->Box));
        }
    }
}

void WorldCollisionInternal_WidenPushedProxies(WorldCollision* self, WorldCollisionInstance* instance, Vector3 shift)
{
    CollisionTree* Tree = instance->IsStatic ? &self->_staticTree : &self->_movableTree;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        if (Shape->Proxy == COLLISION_TREE_NULL)
        {
            continue;
        }
        Vector3Double Shift = GameMath_FromVector3(shift);
        BoundingBoxDouble Shifted = { .Min = Vector3Double_Add(Shape->Box.Min, Shift),
            .Max = Vector3Double_Add(Shape->Box.Max, Shift) };
        BoundingBoxDouble Fat = CollisionTree_GetBounds(Tree, Shape->Proxy);
        if (!BoundingBoxDouble_ContainsBox(Fat, Shifted))
        {
            instance->IsWidened = true;
            CollisionTree_Move(Tree, Shape->Proxy, BoundingBoxDouble_Union(Shifted, Fat));
        }
    }
}

void WorldCollisionInternal_RestoreProxy(WorldCollision* self, WorldCollisionInstance* instance)
{
    CollisionTree* Tree = instance->IsStatic ? &self->_staticTree : &self->_movableTree;
    double Slack = 2.0 * (double)Tree->Margin;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        if (Shape->Proxy == COLLISION_TREE_NULL)
        {
            continue;
        }
        // Refitted only when the fat box grew well past the shape's (a sweep widened it); moves inside it cost nothing.
        BoundingBoxDouble Fat = CollisionTree_GetBounds(Tree, Shape->Proxy);
        const BoundingBoxDouble* Box = &Shape->Box;
        bool IsLoose = ((Box->Min.X - Fat.Min.X) > Slack) || ((Box->Min.Y - Fat.Min.Y) > Slack)
            || ((Box->Min.Z - Fat.Min.Z) > Slack) || ((Fat.Max.X - Box->Max.X) > Slack)
            || ((Fat.Max.Y - Box->Max.Y) > Slack) || ((Fat.Max.Z - Box->Max.Z) > Slack);
        if (IsLoose)
        {
            CollisionTree_Refit(Tree, Shape->Proxy, Shape->Box);
        }
        else
        {
            CollisionTree_Move(Tree, Shape->Proxy, Shape->Box);
        }
    }
}

void WorldCollisionInternal_Warn(const WorldCollision* self, const WorldCollisionInstance* instance, const char* text)
{
    WarnAbout(self, instance, text, NULL, "", NULL);
}

WorldCollisionPose WorldCollisionInternal_InterpolatePose(const WorldCollisionPose* from, const WorldCollisionPose* to,
    float t)
{
    CollisionPose TurnFrom = { .Position = { 0.0f, 0.0f, 0.0f }, .Rotation = from->Rotation };
    CollisionPose TurnTo = { .Position = { 0.0f, 0.0f, 0.0f }, .Rotation = to->Rotation };
    CollisionPose Turned = CollisionPose_Interpolate(&TurnFrom, &TurnTo, t);
    return (WorldCollisionPose){ .Position = Vector3Double_Lerp(from->Position, to->Position, (double)t),
        .Rotation = Turned.Rotation };
}

float WorldCollisionInternal_GetTurn(const WorldCollisionPose* from, const WorldCollisionPose* to)
{
    CollisionPose TurnFrom = { .Position = { 0.0f, 0.0f, 0.0f }, .Rotation = from->Rotation };
    CollisionPose TurnTo = { .Position = { 0.0f, 0.0f, 0.0f }, .Rotation = to->Rotation };
    return CollisionPose_GetRotationAngle(&TurnFrom, &TurnTo);
}


// Public functions.
Error WorldCollision_Create(World* world, AssetManager* assets, Logger* logger, const WorldCollisionOptions* options,
    WorldCollision** outCollision)
{
    if ((world == NULL) || (outCollision == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldCollision_Create: world and outCollision must not be NULL.");
    }
    *outCollision = NULL;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (assets != NULL)
    {
        Error Result = AssetManager_GetNewUserID(assets, &User);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    WorldCollision* Self = Memory_Allocate(sizeof(WorldCollision));
    Memory_Zero(Self, sizeof(*Self));
    Self->_world = world;
    Self->_assets = assets;
    Self->_assetUser = User;
    Self->_logger = logger;
    Self->_options = (options != NULL) ? *options : WorldCollisionOptions_CreateDefault();
    Self->_freeSlot = WORLD_COLLISION_NO_SLOT;
    GenericBuffer_AllocateVariable(&Self->_instances, INITIAL_INSTANCES, sizeof(WorldCollisionInstance));
    GenericBuffer_AllocateVariable(&Self->_order, INITIAL_INSTANCES, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Self->_lookup, INITIAL_INSTANCES, sizeof(WorldCollisionLookup));
    GenericBuffer_AllocateVariable(&Self->_planes, 4U, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&Self->_triggerPairs, 8U, sizeof(WorldCollisionTriggerPair));
    GenericBuffer_AllocateVariable(&Self->_newTriggerPairs, 8U, sizeof(WorldCollisionTriggerPair));
    GenericBuffer_AllocateVariable(&Self->_triggerEvents, 8U, sizeof(WorldTriggerEvent));
    GenericBuffer_AllocateVariable(&Self->_candidates, INITIAL_CANDIDATES, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&Self->_debugShapes, 0U, sizeof(WorldCollisionDebugShape));
    GenericBuffer_AllocateVariable(&Self->_teleports, 4U, sizeof(uint64_t));
    GenericBuffer_AllocateVariable(&Self->_bodies, INITIAL_STEP_ITEMS, sizeof(WorldCollisionBody));
    GenericBuffer_AllocateVariable(&Self->_queue, INITIAL_STEP_ITEMS, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Self->_carried, INITIAL_STEP_ITEMS, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Self->_pairs, INITIAL_STEP_ITEMS, sizeof(WorldCollisionPair));
    GenericBuffer_AllocateVariable(&Self->_rows, INITIAL_STEP_ITEMS, sizeof(WorldCollisionRow));
    GenericBuffer_AllocateVariable(&Self->_rowScratch, INITIAL_STEP_ITEMS, sizeof(WorldCollisionRow));
    GenericBuffer_AllocateVariable(&Self->_sweepHits, INITIAL_STEP_ITEMS, sizeof(WorldCollisionSweepHit));
    GenericBuffer_AllocateVariable(&Self->_manifolds, INITIAL_STEP_ITEMS, sizeof(WorldCollisionManifold));
    GenericBuffer_AllocateVariable(&Self->_pushes, INITIAL_STEP_ITEMS, sizeof(WorldCollisionPush));
    GenericBuffer_AllocateVariable(&Self->_starts, INITIAL_STEP_ITEMS, sizeof(WorldCollisionStart));
    GenericBuffer_AllocateVariable(&Self->_contacts, INITIAL_STEP_ITEMS, sizeof(WorldContact));
    GenericBuffer_AllocateVariable(&Self->_previousContacts, INITIAL_STEP_ITEMS, sizeof(WorldContact));
    GenericBuffer_AllocateVariable(&Self->_contactPairs, INITIAL_STEP_ITEMS, sizeof(WorldCollisionContactPair));
    GenericBuffer_AllocateVariable(&Self->_newContactPairs, INITIAL_STEP_ITEMS, sizeof(WorldCollisionContactPair));
    GenericBuffer_AllocateVariable(&Self->_contactEvents, 8U, sizeof(WorldContactEvent));
    GenericBuffer_AllocateVariable(&Self->_eventContacts, 8U, sizeof(WorldContact));
    GenericBuffer_AllocateVariable(&Self->_motionEvents, 4U, sizeof(WorldMotionEvent));
    GenericBuffer_AllocateVariable(&Self->_chainRecords, INITIAL_STEP_ITEMS, sizeof(WorldCollisionChainRecord));
    GenericBuffer_AllocateVariable(&Self->_debugContacts, 0U, sizeof(WorldCollisionDebugContact));
    CollisionTree_Construct(&Self->_staticTree, 0.0f);
    CollisionTree_Construct(&Self->_movableTree, WORLD_COLLISION_MOVABLE_MARGIN);
    *outCollision = Self;
    return Error_CreateSuccess();
}

void WorldCollision_Destroy(WorldCollision* self)
{
    if (self == NULL)
    {
        return;
    }
    for (uint32_t Slot = 0U; Slot < (uint32_t)self->_instances._count; Slot++)
    {
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        if (Instance->IsUsed)
        {
            FreeTerrainChunks(Instance);
            Memory_Free(Instance->Shapes._data);
            Memory_Free(Instance->WorldModelName);
            Memory_Free(Instance->TerrainName);
            Memory_Free(Instance->OwnShape);
        }
    }
    CollisionTree_Deconstruct(&self->_staticTree);
    CollisionTree_Deconstruct(&self->_movableTree);
    Memory_Free(self->_instances._data);
    Memory_Free(self->_order._data);
    Memory_Free(self->_lookup._data);
    Memory_Free(self->_planes._data);
    Memory_Free(self->_triggerPairs._data);
    Memory_Free(self->_newTriggerPairs._data);
    Memory_Free(self->_triggerEvents._data);
    Memory_Free(self->_candidates._data);
    Memory_Free(self->_debugShapes._data);
    GenericBuffer* StepBuffers[] =
    {
        &self->_teleports, &self->_bodies, &self->_queue, &self->_carried, &self->_pairs, &self->_rows,
        &self->_rowScratch, &self->_sweepHits, &self->_manifolds, &self->_pushes, &self->_starts, &self->_contacts,
        &self->_previousContacts, &self->_contactPairs, &self->_newContactPairs, &self->_contactEvents,
        &self->_eventContacts, &self->_motionEvents, &self->_chainRecords, &self->_debugContacts
    };
    for (size_t Index = 0U; Index < (sizeof(StepBuffers) / sizeof(StepBuffers[0])); Index++)
    {
        Memory_Free(StepBuffers[Index]->_data);
    }
    if ((self->_assets != NULL) && (self->_assetUser != ASSET_USER_ID_INVALID))
    {
        Error Result = AssetManager_RetireUser(self->_assets, self->_assetUser);
        Error_Deconstruct(&Result);
    }
    Memory_Free(self);
}

void WorldCollision_Sync(WorldCollision* self)
{
    self->_tick++;
    WorldCollisionInternal_SyncShapes(self);
    if (self->_isDebugListEnabled)
    {
        RebuildDebugList(self);
    }
    WorldCollisionInternal_UpdateTriggers(self);
}

void WorldCollisionInternal_RebuildDebugList(WorldCollision* self)
{
    if (self->_isDebugListEnabled)
    {
        RebuildDebugList(self);
    }
}

const WorldTriggerEvent* WorldCollision_GetTriggerEvents(const WorldCollision* self, size_t* outCount)
{
    *outCount = self->_triggerEvents._count;
    return (self->_triggerEvents._count > 0U) ? (const WorldTriggerEvent*)(const void*)self->_triggerEvents._data : NULL;
}

void WorldCollision_ClearTriggerEvents(WorldCollision* self)
{
    GenericBuffer_Clear(&self->_triggerEvents);
}

WorldCollisionStats WorldCollision_GetStats(const WorldCollision* self)
{
    WorldCollisionStats Stats;
    Memory_Zero(&Stats, sizeof(Stats));
    for (uint32_t Slot = 0U; Slot < (uint32_t)self->_instances._count; Slot++)
    {
        const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        if (!Instance->IsUsed || !Instance->IsActive)
        {
            continue;
        }
        Stats.InstanceCount++;
        Stats.StaticInstanceCount += Instance->IsStatic ? 1U : 0U;
        Stats.MovableInstanceCount += Instance->IsStatic ? 0U : 1U;
        for (uint32_t Index = 0U; Index < (uint32_t)Instance->Shapes._count; Index++)
        {
            const WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(Instance, Index);
            if (!Shape->IsValid)
            {
                continue;
            }
            Stats.ShapeCount++;
            Stats.PlaneCount += Shape->IsUnbounded ? 1U : 0U;
            if ((Shape->Shape->Type == CollisionShapeType_Mesh) && (Shape->Shape->Mesh != NULL))
            {
                Stats.TriangleCount += Shape->Shape->Mesh->_triangleCount;
            }
        }
    }
    Stats.TriggerPairCount = self->_triggerPairs._count;
    Stats.RebuildCount = self->_rebuildCount;
    Stats.MoveCount = self->_moveCount;
    Stats.TerrainChunkBuildCount = self->_terrainChunkBuildCount;
    Stats.Tick = self->_tick;
    Stats.MoverCount = self->_stepStats.MoverCount;
    Stats.AwakeCount = self->_stepStats.AwakeCount;
    Stats.PairCount = self->_stepStats.PairCount;
    Stats.ContactCount = self->_stepStats.ContactCount;
    Stats.ContactPairCount = self->_stepStats.ContactPairCount;
    Stats.SolverIterations = self->_stepStats.SolverIterations;
    Stats.SubstepCount = self->_stepStats.SubstepCount;
    Stats.DeepestOverlap = self->_stepStats.DeepestOverlap;
    Stats.BlockedCount = self->_stepStats.BlockedCount;
    Stats.CrushedCount = self->_stepStats.CrushedCount;
    Stats.ResolveCount = self->_stepStats.ResolveCount;
    return Stats;
}

void WorldCollision_SetDebugListEnabled(WorldCollision* self, bool isEnabled)
{
    self->_isDebugListEnabled = isEnabled;
    if (!isEnabled)
    {
        GenericBuffer_Clear(&self->_debugShapes);
    }
}

void WorldCollision_SetDebugContactsEnabled(WorldCollision* self, bool isEnabled)
{
    self->_isDebugContactsEnabled = isEnabled;
    if (!isEnabled)
    {
        GenericBuffer_Clear(&self->_debugContacts);
    }
}

const WorldCollisionDebugContact* WorldCollision_GetDebugContacts(const WorldCollision* self, size_t* outCount)
{
    *outCount = self->_debugContacts._count;
    return (self->_debugContacts._count > 0U)
        ? (const WorldCollisionDebugContact*)(const void*)self->_debugContacts._data : NULL;
}

const WorldCollisionDebugShape* WorldCollision_GetDebugShapes(const WorldCollision* self, size_t* outCount)
{
    *outCount = self->_debugShapes._count;
    return (self->_debugShapes._count > 0U)
        ? (const WorldCollisionDebugShape*)(const void*)self->_debugShapes._data : NULL;
}
