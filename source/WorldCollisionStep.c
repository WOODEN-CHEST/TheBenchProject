#include <float.h>
#include <math.h>
#include "WorldCollisionInternal.h"
#include "GameMath.h"
#include "World.h"
#include "raylib/raymath.h"


/*
 * The collision world's step (include/WorldCollision.h, "Moving and pushing"; the step's types in
 * source/WorldCollisionInternal.h). WorldCollision_Step:
 * 1. syncs the shapes (source/WorldCollision.c), then starts each instance's motion: from its pose at the end of the
 *    last step to its pose now; a new instance, a teleported one, a static one and every one while motions are not
 *    swept starts where it is. Each instance gets its OWNER for the step: itself when it is a pushable, else the
 *    nearest pushable up its object's chain (it carries it), else none;
 * 2. makes the BODIES: every instance that moves, and every pushable woken by something near it that moves, changed or
 *    is still pushed (a walk outward from those, so a crowd pressed together wakes as one); the colliders a woken
 *    pushable carries come along. Bodies that move widen their tree boxes to their step's motion;
 * 3. solves (source/WorldCollisionSolver.c);
 * 4. sets back the kinematic movers that are blocked and stop when blocked: the local transforms that changed since the
 *    last step along the mover's object's chain (a group moved by its parent stops as one), recorded at the last step's
 *    end; then syncs and solves again (at most WORLD_COLLISION_MAX_BLOCK_ROUNDS times);
 * 5. WRITES the pushes into the objects: a pushed object's position moves by its push less the push of the pushable it
 *    hangs under (that one moves it already), through its parent's inverse. Syncs again when it moved anything, so the
 *    instances stand where their objects ended up;
 * 6. finds the contacts and their events (source/WorldCollisionEvents.c) and the results;
 * 7. remembers the bodies' poses for the next step (nothing else moved; a static instance's last pose is the sync's),
 *    queues the motion events, records the stopping movers' chains, and updates the triggers.
 * The passes over every instance stop early at a static one, and an instance's result is emptied only when the step
 * writes it (an older one reads as empty), so colliders at rest cost little more than the sync.
 */


// Static functions: helpers.
static bool ArePosesEqual(const WorldCollisionPose* a, const WorldCollisionPose* b)
{
    return WorldCollisionInternal_ArePosesEqual(a, b);
}

static bool AreVectorsEqual(Vector3 a, Vector3 b)
{
    return (a.x == b.x) && (a.y == b.y) && (a.z == b.z);
}

static bool ArePositionsEqual(Vector3Double a, Vector3Double b)
{
    return (a.X == b.X) && (a.Y == b.Y) && (a.Z == b.Z);
}

static WorldObject* GetObjectOf(const WorldCollisionInstance* instance)
{
    return WorldComponent_GetOwner(WorldCollisionInternal_GetComponent(instance));
}

/* The box of an instance's valid, bounded shapes where it is; false when it has none. */
static bool GetInstanceBox(const WorldCollisionInstance* instance, BoundingBoxDouble* outBox)
{
    *outBox = WorldCollisionInternal_GetEmptyBox();
    bool HasBox = false;
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        const WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, Index);
        if (Shape->IsValid && !Shape->IsUnbounded)
        {
            *outBox = BoundingBoxDouble_Union(*outBox, Shape->Box);
            HasBox = true;
        }
    }
    return HasBox;
}

/* The box an instance's shapes cover over its step's motion: where it is now, where it was (moved back by the step's
 * translation), and for a turn everything within its reach of both positions. False without a bounded shape. */
static bool GetStepBox(const WorldCollisionInstance* instance, BoundingBoxDouble* outBox)
{
    if (!GetInstanceBox(instance, outBox))
    {
        return false;
    }
    Vector3Double Back = Vector3Double_Subtract(instance->LastPose.Position, instance->Pose.Position);
    BoundingBoxDouble Before = { .Min = Vector3Double_Add(outBox->Min, Back),
        .Max = Vector3Double_Add(outBox->Max, Back) };
    *outBox = BoundingBoxDouble_Union(*outBox, Before);
    if (WorldCollisionInternal_GetTurn(&instance->LastPose, &instance->Pose) > 0.0f)
    {
        BoundingBoxDouble Turned = BoundingBoxDouble_Expand(BoundingBoxDouble_FromCorners(instance->LastPose.Position,
            instance->Pose.Position), (double)instance->Reach);
        *outBox = BoundingBoxDouble_Union(*outBox, Turned);
    }
    return true;
}

static ComparisonResult CompareIds(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    uint64_t A = *(const uint64_t*)a._element;
    uint64_t B = *(const uint64_t*)b._element;
    return (A < B) ? ComparisonResult_ALessThanB : ((A > B) ? ComparisonResult_AGreaterThanB : ComparisonResult_AEqualsB);
}

static bool ContainsId(const GenericBuffer* sortedIds, uint64_t id)
{
    const uint64_t* Ids = (const uint64_t*)(const void*)sortedIds->_data;
    size_t Low = 0U;
    size_t High = sortedIds->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (Ids[Middle] == id)
        {
            return true;
        }
        if (Ids[Middle] < id)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return false;
}


// Static functions: motions and owners.
/* Whether the instance's object or an ancestor was teleported since the last step (_teleports sorted). */
static bool IsTeleported(const WorldCollision* self, const WorldCollisionInstance* instance)
{
    if (self->_teleports._count == 0U)
    {
        return false;
    }
    for (const WorldObject* Object = GetObjectOf(instance); Object != NULL; Object = WorldObject_GetParent(Object))
    {
        if (ContainsId(&self->_teleports, WorldObject_GetId(Object)))
        {
            return true;
        }
    }
    return false;
}

/* The slot of the nearest pushable instance strictly up an instance's object's chain, or WORLD_COLLISION_NO_SLOT. */
static uint32_t FindPushableAbove(const WorldCollision* self, const WorldCollisionInstance* instance)
{
    for (const WorldObject* Ancestor = WorldObject_GetParent(GetObjectOf(instance)); Ancestor != NULL;
        Ancestor = WorldObject_GetParent(Ancestor))
    {
        uint32_t Slot = WORLD_COLLISION_NO_SLOT;
        if (WorldCollisionInternal_FindSlot(self, WorldObject_GetId(Ancestor), &Slot)
            && WorldCollisionInternal_IsPushable(WorldCollisionInternal_GetInstance(self, Slot)))
        {
            return Slot;
        }
    }
    return WORLD_COLLISION_NO_SLOT;
}

/* Starts every movable instance's motion, finds its owner (and a pushable's pushable above), and lists those that hang
 * under a pushable (a static one starts where it is: the sync keeps its last pose, and nothing pushes it). */
static void BeginMotions(WorldCollision* self)
{
    if (self->_teleports._count > 1U)
    {
        uint64_t Scratch[2];
        GenericBuffer_SortAscending(&self->_teleports, CompareIds, NULL, Scratch);
    }
    GenericBuffer_Clear(&self->_carried);
    const uint32_t* Order = (const uint32_t*)(const void*)self->_order._data;
    for (size_t Index = 0U; Index < self->_order._count; Index++)
    {
        uint32_t Slot = Order[Index];
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        if (!Instance->IsActive || Instance->IsStatic)
        {
            continue;
        }
        Instance->IsStoppedThisStep = false;
        bool StartsHere = !Instance->HasLastPose || !self->_options.AreMotionsSwept || IsTeleported(self, Instance);
        if (StartsHere)
        {
            // No sweep: it starts where it is. A jump still wakes what is around it, to be pushed out.
            Instance->IsChanged = Instance->IsChanged || (Instance->HasLastPose
                && !ArePosesEqual(&Instance->LastPose, &Instance->Pose));
            Instance->LastPose = Instance->Pose;
            Instance->HasLastPose = true;
        }
        uint32_t Above = FindPushableAbove(self, Instance);
        bool IsPushable = WorldCollisionInternal_IsPushable(Instance);
        Instance->OwnerSlot = IsPushable ? Slot : Above;
        Instance->PushableAboveSlot = IsPushable ? Above : WORLD_COLLISION_NO_SLOT;
        if (Above != WORLD_COLLISION_NO_SLOT)
        {
            GenericBuffer_AddLast(&self->_carried, &Slot);
        }
    }
}


// Static functions: bodies.
/* Warns once about a pushed collider's planes and meshes: the solver pushes convex shapes only. */
static void WarnAboutPushedShapes(const WorldCollision* self, WorldCollisionInstance* instance)
{
    if (instance->HasWarnedPushableShape)
    {
        return;
    }
    for (uint32_t Index = 0U; Index < (uint32_t)instance->Shapes._count; Index++)
    {
        CollisionShapeType Type = WorldCollisionInternal_GetShape(instance, Index)->Shape->Type;
        if ((Type == CollisionShapeType_Plane) || (Type == CollisionShapeType_Mesh))
        {
            instance->HasWarnedPushableShape = true;
            WorldCollisionInternal_Warn(self, instance, "is pushed (pushable, or carried by a pushable) but has a plane "
                "or mesh shape; those take no part in pushing, only its spheres, capsules, boxes, cylinders and hulls do");
            return;
        }
    }
}

static uint32_t AddBody(WorldCollision* self, uint32_t slot);

/* The bodies of what hangs under a pushable: the colliders it carries (the instances it owns), and the pushables right
 * under it (each its own owner; woken with it, so its push is written back relative to this one's). */
static void AddCarriedBodies(WorldCollision* self, uint32_t ownerSlot)
{
    for (size_t Index = 0U; Index < self->_carried._count; Index++)
    {
        uint32_t Slot = ((const uint32_t*)(const void*)self->_carried._data)[Index];
        const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        uint32_t Above = (Instance->OwnerSlot == Slot) ? Instance->PushableAboveSlot : Instance->OwnerSlot;
        if ((Above == ownerSlot) && (Instance->Body == WORLD_COLLISION_NO_BODY))
        {
            AddBody(self, Slot);
        }
    }
}

/* Makes the body of the instance in @p slot, once: its owner's body first (for a pushable, the pushable above it), and
 * for a pushable the bodies of what hangs under it after. A body that moves or is pushed widens its tree boxes to its
 * step's motion. */
static uint32_t AddBody(WorldCollision* self, uint32_t slot)
{
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, slot);
    if (Instance->Body != WORLD_COLLISION_NO_BODY)
    {
        return Instance->Body;
    }
    bool IsPushable = WorldCollisionInternal_IsPushable(Instance);
    uint32_t Owner = WORLD_COLLISION_NO_BODY;
    if ((Instance->OwnerSlot != WORLD_COLLISION_NO_SLOT) && (Instance->OwnerSlot != slot))
    {
        Owner = AddBody(self, Instance->OwnerSlot);
    }
    uint32_t ParentPushable = WORLD_COLLISION_NO_BODY;
    if (IsPushable && (Instance->PushableAboveSlot != WORLD_COLLISION_NO_SLOT))
    {
        ParentPushable = AddBody(self, Instance->PushableAboveSlot);
    }
    if (Instance->Body != WORLD_COLLISION_NO_BODY)
    {
        return Instance->Body; // made on the way (a pushable carrying its owner's owner: not possible, but cheap to hold)
    }
    WorldCollisionBody Body;
    Memory_Zero(&Body, sizeof(Body));
    Body.Slot = slot;
    Body.Kind = IsPushable ? WorldCollisionBodyKind_Pushable
        : (Instance->IsStatic ? WorldCollisionBodyKind_Static : WorldCollisionBodyKind_Kinematic);
    Body.StepFrom = Instance->LastPose;
    Body.StepTo = Instance->Pose;
    Body.IsMoving = !ArePosesEqual(&Body.StepFrom, &Body.StepTo);
    Body.Turn = Body.IsMoving ? WorldCollisionInternal_GetTurn(&Body.StepFrom, &Body.StepTo) : 0.0f;
    bool IsPushed = IsPushable || (Owner != WORLD_COLLISION_NO_BODY);
    Body.IsContinuous = (IsPushed || Body.IsMoving) ? Instance->Settings.IsContinuous : true;
    Body.From = Body.StepFrom;
    Body.To = Body.StepTo;
    Body.PhaseTurn = QuaternionIdentity();
    Body.ClampFraction = 1.0f;
    Body.ParentPushable = ParentPushable;
    Body.Owner = Owner;
    Body.InverseMass = IsPushable ? (1.0f / Instance->Settings.Mass)
        : ((Owner != WORLD_COLLISION_NO_BODY) ? WorldCollisionInternal_GetBodyAt(self, Owner)->InverseMass : 0.0f);
    if (!GetStepBox(Instance, &Body.Swept))
    {
        Body.Swept = WorldCollisionInternal_GetEmptyBox();
    }
    uint32_t Index = (uint32_t)self->_bodies._count;
    if (IsPushable)
    {
        Body.Owner = Index;
    }
    GenericBuffer_AddLast(&self->_bodies, &Body);
    Instance->Body = Index;
    if ((Body.IsMoving || IsPushed) && !WorldCollisionInternal_IsBoxEmpty(Body.Swept))
    {
        WorldCollisionInternal_WidenProxies(self, Instance, Body.Swept);
    }
    if (IsPushed)
    {
        WarnAboutPushedShapes(self, Instance);
    }
    if (IsPushable)
    {
        AddCarriedBodies(self, slot);
    }
    return Index;
}

static void Enqueue(WorldCollision* self, uint32_t slot)
{
    GenericBuffer_AddLast(&self->_queue, &slot);
}

/* Wakes the pushables (with what they carry) near an instance's box, and queues them to wake what is near them. */
static void WakeNear(WorldCollision* self, uint32_t slot)
{
    const WorldCollisionInstance* Source = WorldCollisionInternal_GetInstance(self, slot);
    BoundingBoxDouble Box = WorldCollisionInternal_GetEmptyBox();
    bool HasBox = (Source->Body != WORLD_COLLISION_NO_BODY)
        ? !WorldCollisionInternal_IsBoxEmpty(WorldCollisionInternal_GetBodyAt(self, Source->Body)->Swept)
        : GetInstanceBox(Source, &Box);
    if (!HasBox)
    {
        return;
    }
    if (Source->Body != WORLD_COLLISION_NO_BODY)
    {
        Box = WorldCollisionInternal_GetBodyAt(self, Source->Body)->Swept;
    }
    Box = BoundingBoxDouble_Expand(Box, (double)(WORLD_COLLISION_SPECULATIVE_DISTANCE + WORLD_COLLISION_SKIN));
    WorldCollisionInternal_CollectCandidates(self, Box);
    for (size_t Candidate = 0U; Candidate < self->_candidates._count; Candidate++)
    {
        uint64_t Key = ((const uint64_t*)(const void*)self->_candidates._data)[Candidate];
        uint32_t OtherSlot = WorldCollisionInternal_GetKeySlot(Key);
        const WorldCollisionInstance* Other = WorldCollisionInternal_GetInstance(self, OtherSlot);
        if ((OtherSlot == slot) || !Other->IsUsed || !Other->IsActive || (Other->OwnerSlot == WORLD_COLLISION_NO_SLOT)
            || !WorldCollisionInternal_IsCandidateNear(Other, WorldCollisionInternal_GetShape(Other,
                WorldCollisionInternal_GetKeyShape(Key)), Box)
            || !WorldCollisionInternal_DoBlock(Source, Other)
            || (WorldCollisionInternal_GetInstance(self, Other->OwnerSlot)->Body != WORLD_COLLISION_NO_BODY))
        {
            continue;
        }
        // The pushable that owns it wakes, with everything it carries: each wakes what is near it in turn.
        size_t First = self->_bodies._count;
        AddBody(self, Other->OwnerSlot);
        for (size_t Index = First; Index < self->_bodies._count; Index++)
        {
            Enqueue(self, WorldCollisionInternal_GetBodyAt(self, (uint32_t)Index)->Slot);
        }
    }
}

/* Lets go of the bodies: their instances have none, and the tree boxes they widened fit their shapes again. */
static void ReleaseBodies(WorldCollision* self)
{
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self,
            WorldCollisionInternal_GetBodyAt(self, Index)->Slot);
        Instance->Body = WORLD_COLLISION_NO_BODY;
        if (Instance->IsWidened)
        {
            Instance->IsWidened = false;
            WorldCollisionInternal_RestoreProxy(self, Instance);
        }
    }
    GenericBuffer_Clear(&self->_bodies);
}

/* The step's bodies: what moves, the pushables still awake, and every pushable they wake. */
static void BuildBodies(WorldCollision* self)
{
    ReleaseBodies(self);
    GenericBuffer_Clear(&self->_queue);
    for (size_t Index = 0U; Index < self->_order._count; Index++)
    {
        uint32_t Slot = ((const uint32_t*)(const void*)self->_order._data)[Index];
        const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
        if (!Instance->IsActive || (Instance->IsStatic && !Instance->IsChanged))
        {
            continue;
        }
        bool IsMoving = !ArePosesEqual(&Instance->LastPose, &Instance->Pose);
        bool IsAwakePushable = Instance->IsAwake && (Instance->OwnerSlot == Slot);
        if (IsMoving || IsAwakePushable || ((Instance->OwnerSlot != WORLD_COLLISION_NO_SLOT) && Instance->IsChanged))
        {
            size_t First = self->_bodies._count;
            AddBody(self, Slot);
            for (size_t Made = First; Made < self->_bodies._count; Made++)
            {
                Enqueue(self, WorldCollisionInternal_GetBodyAt(self, (uint32_t)Made)->Slot);
            }
        }
        else if (Instance->IsChanged)
        {
            Enqueue(self, Slot); // a static or still kinematic shape changed: it wakes what it now touches
        }
    }
    for (size_t Index = 0U; Index < self->_queue._count; Index++)
    {
        WakeNear(self, ((const uint32_t*)(const void*)self->_queue._data)[Index]);
    }
}


// Static functions: blocked movers.
static uint64_t GetParentId(const WorldObject* object)
{
    const WorldObject* Parent = WorldObject_GetParent(object);
    return (Parent != NULL) ? WorldObject_GetId(Parent) : 0U;
}

/* Sets an instance's object's chain back to the local transforms recorded at the end of the last step (only those that
 * changed; WorldCollisionInternal_CanStopMover said there are some, in the same hierarchy). */
static void RevertChain(WorldCollision* self, const WorldCollisionInstance* instance)
{
    const WorldCollisionChainRecord* Records = (const WorldCollisionChainRecord*)(const void*)self->_chainRecords._data;
    for (uint32_t Index = 0U; Index < instance->ChainCount; Index++)
    {
        const WorldCollisionChainRecord* Record = &Records[instance->ChainStart + Index];
        WorldObject* Object = NULL;
        if (!World_TryGetObject(self->_world, Record->ObjectId, &Object))
        {
            continue;
        }
        Error Results[3] = { Error_CreateSuccess(), Error_CreateSuccess(), Error_CreateSuccess() };
        if (!ArePositionsEqual(WorldObject_GetPosition(Object), Record->Position))
        {
            Results[0] = WorldObject_SetPosition(Object, Record->Position);
        }
        if (!AreVectorsEqual(WorldObject_GetRotation(Object), Record->Rotation))
        {
            Results[1] = WorldObject_SetRotation(Object, Record->Rotation);
        }
        if (!AreVectorsEqual(WorldObject_GetScale(Object), Record->Scale))
        {
            Results[2] = WorldObject_SetScale(Object, Record->Scale);
        }
        for (size_t Result = 0U; Result < 3U; Result++)
        {
            Error_Deconstruct(&Results[Result]); // recorded values passed the same checks before
        }
    }
}

/* Sets back every blocked kinematic mover that stops when blocked; true when one was. */
static bool StopBlockedMovers(WorldCollision* self)
{
    bool HasStopped = false;
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Body->Slot);
        if (!Body->IsBlocked || !WorldCollisionInternal_CanStopMover(self, Instance))
        {
            continue;
        }
        RevertChain(self, Instance);
        Instance->IsStoppedThisStep = true;
        WorldMotionResult* Result = WorldCollisionInternal_TouchResult(self, Instance);
        Result->IsBlocked = true;
        Result->IsStopped = true;
        Result->BlockedById = Body->BlockedById;
        Result->Correction = GameMath_ToVector3Relative(Body->StepFrom.Position, Body->StepTo.Position);
        self->_stepStats.BlockedCount++; // counted here: once set back it often has no body left to count
        HasStopped = true;
    }
    return HasStopped;
}


// Static functions: the write-back and the results.
/* Moves every pushed object by its push (less the push of the pushable it hangs under, which moves it already), through
 * its parent's inverse. Returns whether it moved anything. */
static bool WriteBack(WorldCollision* self)
{
    bool HasWritten = false;
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        if (Body->Owner != Index)
        {
            continue;
        }
        Vector3 Push = Body->Accumulated;
        if (Body->ParentPushable != WORLD_COLLISION_NO_BODY)
        {
            Push = CollisionMath_Subtract(Push, WorldCollisionInternal_GetBodyAt(self, Body->ParentPushable)->Accumulated);
        }
        if ((Push.x == 0.0f) && (Push.y == 0.0f) && (Push.z == 0.0f))
        {
            continue;
        }
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Body->Slot);
        WorldObject* Object = GetObjectOf(Instance);
        const WorldObject* Parent = WorldObject_GetParent(Object);
        Vector3 Local = Push;
        if (Parent != NULL)
        {
            // Only the parent's axes turn a push (an offset): they round to floats as well near or far from the origin.
            Matrix4Double ParentWorld = WorldObject_GetWorldMatrix(Parent);
            CollisionAffine ParentToWorld = CollisionAffine_FromMatrix(GameMath_ToMatrix(&ParentWorld));
            CollisionAffine WorldToParent;
            if (!CollisionAffine_TryInvert(&ParentToWorld, &WorldToParent))
            {
                if (!Instance->HasWarnedWriteBack)
                {
                    Instance->HasWarnedWriteBack = true;
                    WorldCollisionInternal_Warn(self, Instance, "was pushed, but its parent's scale is zero on an axis, "
                        "so the push cannot be written into its position");
                }
                continue;
            }
            Local = CollisionAffine_TransformVector(&WorldToParent, Push);
        }
        // The push (millimetres to metres, in float) moves the double position: the same push near or far out.
        Error Result = WorldObject_SetPosition(Object, Vector3Double_Add(WorldObject_GetPosition(Object),
            GameMath_FromVector3(Local)));
        Error_Deconstruct(&Result);
        HasWritten = true;
    }
    return HasWritten;
}

/* The results of the bodies (velocities, pushes, blocked, crushed; whatever has no body did not move), and the step's
 * counts. */
static void FinishResults(WorldCollision* self)
{
    float InverseDelta = 1.0f / self->_deltaSeconds;
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Body->Slot);
        WorldMotionResult* Result = WorldCollisionInternal_TouchResult(self, Instance);
        Result->IsAwake = true;
        Result->Velocity = CollisionMath_Scale(GameMath_ToVector3Relative(Instance->Pose.Position,
            Instance->LastPose.Position), InverseDelta);
        self->_stepStats.MoverCount += Body->IsMoving ? 1U : 0U;
        if (Body->Owner != WORLD_COLLISION_NO_BODY)
        {
            const WorldCollisionBody* Owner = WorldCollisionInternal_GetBodyAt(self, Body->Owner);
            Result->Correction = Owner->Accumulated;
            if (Body->Owner == Index)
            {
                // Crushed is the pushable's (not repeated by each collider it carries).
                Result->IsCrushed = Owner->IsCrushed;
                Result->CrushDepth = Owner->CrushDepth;
                Result->CrushedById = Owner->CrushedById;
                self->_stepStats.AwakeCount++;
                self->_stepStats.CrushedCount += Owner->IsCrushed ? 1U : 0U;
            }
        }
        if (Body->IsBlocked && !Result->IsStopped)
        {
            Result->IsBlocked = true; // pushed through (or nothing to set back)
            Result->BlockedById = Body->BlockedById;
        }
        self->_stepStats.BlockedCount += (Result->IsBlocked && !Result->IsStopped) ? 1U : 0U;
    }
}

/* Records the chain of a kinematic mover that stops when blocked: its object's and its ancestors' local transforms. */
static void RecordChain(WorldCollision* self, WorldCollisionInstance* instance)
{
    instance->ChainStart = (uint32_t)self->_chainRecords._count;
    instance->ChainCount = 0U;
    bool IsStoppingMover = instance->IsActive && !instance->IsStatic && (instance->OwnerSlot == WORLD_COLLISION_NO_SLOT)
        && !instance->Settings.IsTrigger && (instance->Settings.WhenBlocked == WorldColliderBlockedMode_Stop);
    if (!IsStoppingMover)
    {
        return;
    }
    for (const WorldObject* Object = GetObjectOf(instance); Object != NULL; Object = WorldObject_GetParent(Object))
    {
        WorldCollisionChainRecord Record = { .ObjectId = WorldObject_GetId(Object), .ParentId = GetParentId(Object),
            .Position = WorldObject_GetPosition(Object), .Rotation = WorldObject_GetRotation(Object),
            .Scale = WorldObject_GetScale(Object) };
        GenericBuffer_AddLast(&self->_chainRecords, &Record);
        instance->ChainCount++;
    }
}

/* The step's end: every pose is where the next step starts (the bodies moved; nothing else did), the blocked and crushed
 * states that began or ended are queued as motion events, pushables left crushed stay awake, and the stopping movers'
 * chains are recorded. */
static void EndMotions(WorldCollision* self)
{
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self,
            WorldCollisionInternal_GetBodyAt(self, Index)->Slot);
        Instance->LastPose = Instance->Pose;
        Instance->HasLastPose = true;
    }
    ReleaseBodies(self);
    GenericBuffer_Clear(&self->_chainRecords);
    const uint32_t* Order = (const uint32_t*)(const void*)self->_order._data;
    for (size_t Index = 0U; Index < self->_order._count; Index++)
    {
        WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Order[Index]);
        Instance->IsChanged = false;
        if (Instance->IsStatic && !Instance->WasBlocked && !Instance->WasCrushed)
        {
            continue; // never blocked nor crushed, and no chain (a static one that moved in play is teleported)
        }
        WorldCollisionInternal_QueueMotionEvents(self, Instance);
        Instance->WasBlocked = WorldCollisionInternal_IsFlagged(self, Instance, false);
        Instance->WasCrushed = WorldCollisionInternal_IsFlagged(self, Instance, true);
        Instance->IsAwake = Instance->WasCrushed && WorldCollisionInternal_IsPushable(Instance);
        RecordChain(self, Instance);
    }
    GenericBuffer_Clear(&self->_teleports);
}


// Internal functions (WorldCollisionInternal.h).
bool WorldCollisionInternal_IsPushable(const WorldCollisionInstance* instance)
{
    // A pushable held on a socket of its parent (T30: a crate in someone's hands) is the holder's: it moves with the
    // hand, carried as any child of a pushable is, not pushed out of it. Instance data only: queries call this for
    // instances of objects removed since the sync.
    return instance->IsUsed && instance->IsActive && instance->Settings.IsPushable && !instance->IsStatic
        && !instance->Settings.IsTrigger && !instance->IsOnSocket;
}

bool WorldCollisionInternal_CanStopMover(const WorldCollision* self, const WorldCollisionInstance* instance)
{
    if ((instance->Settings.WhenBlocked != WorldColliderBlockedMode_Stop) || instance->IsStoppedThisStep)
    {
        return false;
    }
    bool IsChanged = false;
    const WorldCollisionChainRecord* Records = (const WorldCollisionChainRecord*)(const void*)self->_chainRecords._data;
    for (uint32_t Index = 0U; Index < instance->ChainCount; Index++)
    {
        const WorldCollisionChainRecord* Record = &Records[instance->ChainStart + Index];
        WorldObject* Object = NULL;
        if (!World_TryGetObject(self->_world, Record->ObjectId, &Object) || (GetParentId(Object) != Record->ParentId))
        {
            return false; // another hierarchy now: the recorded local transforms would put it somewhere else
        }
        IsChanged = IsChanged || !ArePositionsEqual(WorldObject_GetPosition(Object), Record->Position)
            || !AreVectorsEqual(WorldObject_GetRotation(Object), Record->Rotation)
            || !AreVectorsEqual(WorldObject_GetScale(Object), Record->Scale);
    }
    return IsChanged;
}

bool WorldCollisionInternal_DoBlock(const WorldCollisionInstance* a, const WorldCollisionInstance* b)
{
    bool IsOneOwner = (a->OwnerSlot != WORLD_COLLISION_NO_SLOT) && (a->OwnerSlot == b->OwnerSlot);
    return (a != b) && !a->Settings.IsTrigger && !b->Settings.IsTrigger && !IsOneOwner
        && CollisionLayer_DoCollide(a->Settings.Layer, a->Settings.CollidesWith, b->Settings.Layer,
            b->Settings.CollidesWith);
}

uint32_t WorldCollisionInternal_GetBody(WorldCollision* self, uint32_t slot)
{
    return AddBody(self, slot);
}


// Public functions.
void WorldCollision_Step(WorldCollision* self, float deltaSeconds)
{
    self->_tick++;
    self->_stepTick = self->_tick;
    self->_deltaSeconds = (deltaSeconds > 1e-6f) ? deltaSeconds : 1e-6f;
    Memory_Zero(&self->_stepStats, sizeof(self->_stepStats));
    WorldCollisionInternal_SyncShapes(self);
    BeginMotions(self);
    for (uint32_t Round = 0U; ; Round++)
    {
        BuildBodies(self);
        WorldCollisionInternal_Solve(self, Round < WORLD_COLLISION_MAX_BLOCK_ROUNDS);
        if ((Round >= WORLD_COLLISION_MAX_BLOCK_ROUNDS) || !StopBlockedMovers(self))
        {
            break;
        }
        // Set back: its instances follow it, and the step is solved again without its motion.
        self->_stepStats.ResolveCount++;
        WorldCollisionInternal_SyncShapes(self);
    }
    if (WriteBack(self))
    {
        WorldCollisionInternal_SyncShapes(self); // the pushed objects' instances follow them
    }
    FinishResults(self);
    WorldCollisionInternal_UpdateContacts(self);
    EndMotions(self);
    WorldCollisionInternal_RebuildDebugList(self);
    WorldCollisionInternal_UpdateTriggers(self);
}

void WorldCollision_Teleport(WorldCollision* self, uint64_t objectId)
{
    if (objectId != 0U)
    {
        GenericBuffer_AddLast(&self->_teleports, &objectId);
    }
}

bool WorldCollision_GetMotionResult(const WorldCollision* self, uint64_t objectId, WorldMotionResult* outResult)
{
    Memory_Zero(outResult, sizeof(*outResult));
    uint32_t Slot = WORLD_COLLISION_NO_SLOT;
    if (!WorldCollisionInternal_FindSlot(self, objectId, &Slot))
    {
        return false;
    }
    const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
    if (!Instance->IsUsed || !Instance->IsActive)
    {
        return false;
    }
    if (Instance->Result.Tick == self->_stepTick)
    {
        *outResult = Instance->Result;
    }
    outResult->Tick = self->_stepTick; // untouched by the last step: it did not move and touches nothing
    return true;
}
