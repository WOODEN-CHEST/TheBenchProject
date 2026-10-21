#include <inttypes.h>
#include <math.h>
#include "WorldCollisionInternal.h"
#include "AssetManager.h"
#include "Logger.h"
#include "World.h"
#include "WorldInternal.h"
#include "WorldModelComponent.h"
#include "WorldTransform.h"
#include "raylib/raymath.h"

/*
 * The collision world (include/WorldCollision.h): lifetime, the sync, the debug list and the counts. Queries are in
 * source/WorldCollisionQuery.c, trigger pairs in source/WorldCollisionTrigger.c; the shared state in
 * source/WorldCollisionInternal.h.
 *
 * A SYNC walks the world's colliders in its own order (_order maps each to its instance's slot; re-matched by object
 * id whenever the world's component set changes, so an added, removed or replaced component is noticed) and for each:
 * out of the trees when it or an ancestor is disabled; its shapes rebuilt when the collider changed (its revision), its
 * world model's name changed, it moved between the static and the movable tree, or it was just (re)enabled; moved
 * (new transforms, boxes and proxies) when only its object's transform revision changed.
 */


// Macros.
/** The first capacities of the buffers. */
#define INITIAL_INSTANCES 16U
#define INITIAL_CANDIDATES 64U


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
        const WorldObject* Object = WorldComponent_GetOwner(&instance->Collider->Base);
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
    GenericBuffer_AllocateVariable(&Instance->Shapes, 1U, sizeof(WorldCollisionShape));
    return Slot;
}

/* Takes an instance's shapes out of the trees and the planes list. */
static void Deactivate(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot)
{
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

static void FreeSlot(WorldCollision* self, uint32_t slot)
{
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, slot);
    Deactivate(self, Instance, slot);
    Memory_Free(Instance->Shapes._data);
    Memory_Free(Instance->WorldModelName);
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

/* Matches the world's colliders to instances by object id after its component set changed: new colliders get slots,
 * a replaced component starts its instance afresh, instances whose collider is gone are freed. */
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
        WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Colliders[Index]);
        uint64_t ObjectId = WorldObject_GetId(WorldComponent_GetOwner(Colliders[Index]));
        uint32_t Slot = WORLD_COLLISION_NO_SLOT;
        bool IsKnown = WorldCollisionInternal_FindSlot(self, ObjectId, &Slot)
            && WorldCollisionInternal_GetInstance(self, Slot)->IsUsed;
        if (!IsKnown)
        {
            Slot = AllocateSlot(self);
            WorldCollisionInstance* Fresh = WorldCollisionInternal_GetInstance(self, Slot);
            Fresh->ObjectId = ObjectId;
            Fresh->Collider = Collider;
            Fresh->ColliderSerial = WorldComponent_GetSerial(Colliders[Index]);
        }
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        uint64_t Serial = WorldComponent_GetSerial(Colliders[Index]);
        if ((Instance->Collider != Collider) || (Instance->ColliderSerial != Serial))
        {
            // A replaced component (often at its predecessor's address): nothing the old one resolved or saw carries
            // over.
            Deactivate(self, Instance, Slot);
            Instance->Collider = Collider;
            Instance->ColliderSerial = Serial;
            Instance->HasSeen = false;
            Instance->HasWarnedSet = false;
            Instance->HasWarnedNoShapes = false;
            Instance->HasWarnedFlatShape = false;
        }
        Instance->IsMatched = true;
        GenericBuffer_AddLast(&self->_order, &Slot);
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

/* The collider's frame in the world: the object's world matrix by the rotation mode, then the collider's own
 * transform. yaw_only and none rebuild the frame from the world transform's decomposition (a mirror lives in its scale,
 * so the turn they keep is a proper rotation) with the object's scale along the turned (or the world's) axes. */
static Matrix GetColliderToWorld(const WorldColliderComponent* collider, const WorldObject* object, Matrix objectWorld)
{
    Matrix Frame = objectWorld;
    WorldColliderRotationMode Mode = WorldColliderComponent_GetRotationMode(collider);
    if (Mode != WorldColliderRotationMode_Full)
    {
        Vector3 Position;
        Vector3 Rotation;
        Vector3 Scale;
        WorldObject_GetWorldTransform(object, &Position, &Rotation, &Scale);
        float Yaw = 0.0f;
        if (Mode == WorldColliderRotationMode_YawOnly)
        {
            // The yaw of the turn's forward (local +Z), or of its right (local +X) when it points straight up or down.
            Matrix Turn = WorldTransform_Compose((Vector3){ 0.0f, 0.0f, 0.0f }, Rotation, (Vector3){ 1.0f, 1.0f, 1.0f });
            bool HasForward = ((Turn.m8 * Turn.m8) + (Turn.m10 * Turn.m10)) > 1e-12f;
            Yaw = HasForward ? atan2f(Turn.m8, Turn.m10) : atan2f(-Turn.m2, Turn.m0);
        }
        Frame = WorldTransform_Compose(Position, (Vector3){ 0.0f, Yaw, 0.0f }, Scale);
    }
    return MatrixMultiply(WorldColliderComponent_GetLocalTransform(collider), Frame);
}

/* A shape's transforms and box under the collider's frame; false when the frame flattens it. */
static bool PlaceShape(WorldCollisionShape* shape, Matrix colliderToWorld)
{
    Matrix ShapeToWorld = MatrixMultiply(CollisionShape_GetTransform(shape->Shape), colliderToWorld);
    shape->ShapeToWorld = CollisionAffine_FromMatrix(ShapeToWorld);
    bool IsValid = CollisionAffine_TryInvert(&shape->ShapeToWorld, &shape->WorldToShape);
    shape->IsUnbounded = (shape->Shape->Type == CollisionShapeType_Plane) && !shape->Shape->IsBounded;
    if (IsValid && !shape->IsUnbounded)
    {
        Vector3 LocalMin;
        Vector3 LocalMax;
        CollisionShape_TryGetLocalBounds(shape->Shape, &LocalMin, &LocalMax);
        CollisionAffine_TransformBounds(&shape->ShapeToWorld, LocalMin, LocalMax, &shape->Min, &shape->Max);
        IsValid = CollisionMath_IsFinite(shape->Min) && CollisionMath_IsFinite(shape->Max);
    }
    return IsValid;
}

static void UpdateObjectTransform(WorldCollisionInstance* instance, Matrix objectWorld)
{
    instance->ObjectToWorld = CollisionAffine_FromMatrix(objectWorld);
    if (!CollisionAffine_TryInvert(&instance->ObjectToWorld, &instance->WorldToObject))
    {
        instance->WorldToObject = CollisionAffine_Identity();
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
        Shape->Proxy = CollisionTree_Insert(Tree, Shape->Min, Shape->Max, Key);
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

/* Rebuilds an instance's shapes from its settings in effect and puts them into the right tree. */
static void Rebuild(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot, bool isStatic)
{
    Deactivate(self, instance, slot);
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
    Matrix ObjectWorld = WorldObject_GetWorldMatrix(Object);
    UpdateObjectTransform(instance, ObjectWorld);
    Matrix Frame = GetColliderToWorld(instance->Collider, Object, ObjectWorld);
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
        Shape.IsValid = PlaceShape(&Shape, Frame) && Shape.IsSizeValid;
        HasFlatShape = HasFlatShape || !Shape.IsValid;
        GenericBuffer_AddLast(&instance->Shapes, &Shape);
    }
    if (HasFlatShape && !instance->HasWarnedFlatShape)
    {
        instance->HasWarnedFlatShape = true;
        WarnAbout(self, instance, "has a collision shape that its transform flattens (a zero scale) or that has no "
            "size; it collides with nothing", NULL, "", NULL);
    }
    instance->IsStatic = isStatic;
    if (Count > 0U)
    {
        Activate(self, instance, slot);
    }
    self->_rebuildCount++;
}

/* Moves an instance's shapes with its object (the trees re-insert what left its box). */
static void Move(WorldCollision* self, WorldCollisionInstance* instance, uint32_t slot)
{
    const WorldObject* Object = WorldComponent_GetOwner(&instance->Collider->Base);
    Matrix ObjectWorld = WorldObject_GetWorldMatrix(Object);
    UpdateObjectTransform(instance, ObjectWorld);
    Matrix Frame = GetColliderToWorld(instance->Collider, Object, ObjectWorld);
    CollisionTree* Tree = instance->IsStatic ? &self->_staticTree : &self->_movableTree;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        bool WasValid = Shape->IsValid;
        bool IsValid = PlaceShape(Shape, Frame) && Shape->IsSizeValid;
        if (IsValid != WasValid)
        {
            // A shape became flat or unflattened: rebuild the whole instance (rare).
            Rebuild(self, instance, slot, instance->IsStatic);
            return;
        }
        if (IsValid && (Shape->Proxy != COLLISION_TREE_NULL))
        {
            CollisionTree_Move(Tree, Shape->Proxy, Shape->Min, Shape->Max);
        }
    }
    if (instance->IsStatic && self->_options.WarnsWhenStaticObjectsMove && !instance->HasWarnedStaticMove)
    {
        instance->HasWarnedStaticMove = true;
        WarnAbout(self, instance, "is static but moved; its collider was moved in the static tree (mark the object "
            "movable if it moves in play)", NULL, "", NULL);
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

/* Brings one instance in step with its collider and object. */
static void Refresh(WorldCollision* self, uint32_t slot)
{
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, slot);
    WorldColliderComponent* Collider = Instance->Collider;
    const WorldObject* Object = WorldComponent_GetOwner(&Collider->Base);
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
    bool NeedsRebuild = !Instance->HasSeen || IsRenamed || (ColliderRevision != Instance->SeenColliderRevision)
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
    Instance->SeenColliderRevision = ColliderRevision;
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
            WorldCollisionDebugShape Debug = { .Shape = Shape->Shape, .Transform = CollisionAffine_ToMatrix(
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
            Memory_Free(Instance->Shapes._data);
            Memory_Free(Instance->WorldModelName);
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
    World_UpdateTransforms(self->_world);
    if (!self->_hasComponentSetVersion || (World_GetComponentSetVersion(self->_world) != self->_componentSetVersion))
    {
        Rematch(self);
    }
    for (size_t Index = 0U; Index < self->_order._count; Index++)
    {
        Refresh(self, ((const uint32_t*)(const void*)self->_order._data)[Index]);
    }
    if (self->_isDebugListEnabled)
    {
        RebuildDebugList(self);
    }
    WorldCollisionInternal_UpdateTriggers(self);
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
    Stats.Tick = self->_tick;
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

const WorldCollisionDebugShape* WorldCollision_GetDebugShapes(const WorldCollision* self, size_t* outCount)
{
    *outCount = self->_debugShapes._count;
    return (self->_debugShapes._count > 0U)
        ? (const WorldCollisionDebugShape*)(const void*)self->_debugShapes._data : NULL;
}
