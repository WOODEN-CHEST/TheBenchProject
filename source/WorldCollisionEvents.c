#include <float.h>
#include <math.h>
#include "WorldCollisionInternal.h"
#include "raylib/raymath.h"

/*
 * The step's contacts and their events (include/WorldCollision.h, "Contacts and their events"), after
 * source/WorldCollisionSolver.c left its last contacts at the final poses in _manifolds:
 * - CONTACTS: one per pair of shapes (and per triangle of a mesh), keyed by the objects (A the lower id), the shapes and
 *   the triangle. The step's own come from _manifolds; a pair neither of whose colliders took part in the step (nothing
 *   moved there) keeps its last contact, a step older. They are sorted by key and matched to the last step's: a
 *   contact's age goes on, and each point's goes on when a point of the same feature id was there. Each point gets its
 *   relative velocity from the two motions over the step and the solver's push on it (the rows of its key and feature,
 *   summed over the step's solves).
 * - OBJECT PAIRS in contact: a pair begins when one of its contacts touches; it stays in contact while it has contacts at
 *   all (they reach WORLD_COLLISION_CONTACT_END_MARGIN past touching); a begin event, a persist event every step for a
 *   pair that reports persisting contacts, an end event once it has none. Events copy their pair's contacts.
 * - RESULTS: each object's touching contacts and its most upward-facing one, the ground.
 * - MOTION EVENTS: blocked and crushed states that began or ended this step.
 */


// Static functions: keys.
static WorldCollisionContactKey GetKeyOfContact(const WorldContact* contact)
{
    const WorldCollisionShapeInfo* A = &contact->Manifold.ShapeA;
    const WorldCollisionShapeInfo* B = &contact->Manifold.ShapeB;
    uint32_t Triangle = A->HasTriangle ? A->TriangleIndex : (B->HasTriangle ? B->TriangleIndex
        : WORLD_COLLISION_NO_TRIANGLE);
    return (WorldCollisionContactKey){ .ObjectA = A->ObjectId, .ObjectB = B->ObjectId, .ShapeA = A->ShapeIndex,
        .ShapeB = B->ShapeIndex, .Triangle = Triangle };
}

static ComparisonResult ToComparison(int order)
{
    return (order < 0) ? ComparisonResult_ALessThanB : ((order > 0) ? ComparisonResult_AGreaterThanB
        : ComparisonResult_AEqualsB);
}

static ComparisonResult CompareContacts(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    WorldCollisionContactKey KeyA = GetKeyOfContact(a._element);
    WorldCollisionContactKey KeyB = GetKeyOfContact(b._element);
    return ToComparison(WorldCollisionInternal_CompareContactKeys(&KeyA, &KeyB));
}

static int ComparePushRecords(const WorldCollisionPush* a, const WorldCollisionPush* b)
{
    int Order = WorldCollisionInternal_CompareContactKeys(&a->Key, &b->Key);
    if (Order != 0)
    {
        return Order;
    }
    return (a->FeatureId < b->FeatureId) ? -1 : ((a->FeatureId > b->FeatureId) ? 1 : 0);
}

static ComparisonResult ComparePushes(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    return ToComparison(ComparePushRecords(a._element, b._element));
}

static ComparisonResult CompareStarts(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    const WorldCollisionStart* A = a._element;
    const WorldCollisionStart* B = b._element;
    return ToComparison(WorldCollisionInternal_CompareContactKeys(&A->Key, &B->Key));
}

static int CompareObjectPairs(uint64_t objectA, uint64_t objectB, uint64_t otherA, uint64_t otherB)
{
    if (objectA != otherA)
    {
        return (objectA < otherA) ? -1 : 1;
    }
    return (objectB < otherB) ? -1 : ((objectB > otherB) ? 1 : 0);
}

/* The first contact in a sorted list whose key is not below @p key. */
static size_t FindFirstContact(const GenericBuffer* contacts, const WorldCollisionContactKey* key)
{
    const WorldContact* Contacts = (const WorldContact*)(const void*)contacts->_data;
    size_t Low = 0U;
    size_t High = contacts->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        WorldCollisionContactKey Key = GetKeyOfContact(&Contacts[Middle]);
        if (WorldCollisionInternal_CompareContactKeys(&Key, key) < 0)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return Low;
}

/* The first contact of an object pair in a sorted list (by its key with the lowest shapes). */
static size_t FindFirstOfPair(const GenericBuffer* contacts, uint64_t objectA, uint64_t objectB)
{
    WorldCollisionContactKey Key = { .ObjectA = objectA, .ObjectB = objectB, .ShapeA = 0U, .ShapeB = 0U, .Triangle = 0U };
    return FindFirstContact(contacts, &Key);
}

static size_t FindPairEnd(const GenericBuffer* contacts, size_t first, uint64_t objectA, uint64_t objectB)
{
    const WorldContact* Contacts = (const WorldContact*)(const void*)contacts->_data;
    size_t End = first;
    while ((End < contacts->_count) && (Contacts[End].Manifold.ShapeA.ObjectId == objectA)
        && (Contacts[End].Manifold.ShapeB.ObjectId == objectB))
    {
        End++;
    }
    return End;
}


// Static functions: building contacts.
static const WorldCollisionInstance* FindActiveInstance(const WorldCollision* self, uint64_t objectId, uint32_t* outSlot)
{
    uint32_t Slot = WORLD_COLLISION_NO_SLOT;
    if (!WorldCollisionInternal_FindSlot(self, objectId, &Slot))
    {
        return NULL;
    }
    const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
    if (!Instance->IsUsed || !Instance->IsActive)
    {
        return NULL;
    }
    if (outSlot != NULL)
    {
        *outSlot = Slot;
    }
    return Instance;
}

/* The velocity of the material point at @p point, while its instance is at its pose now, under the motion from @p from
 * to @p to over the step (m/s). */
static Vector3 GetPointVelocity(const WorldCollision* self, const WorldCollisionInstance* instance,
    const CollisionPose* from, const CollisionPose* to, Vector3 point)
{
    bool IsTurning = (from->Rotation.x != to->Rotation.x) || (from->Rotation.y != to->Rotation.y)
        || (from->Rotation.z != to->Rotation.z) || (from->Rotation.w != to->Rotation.w);
    if (!IsTurning)
    {
        return CollisionMath_Scale(CollisionMath_Subtract(to->Position, from->Position), 1.0f / self->_deltaSeconds);
    }
    Vector3 Local = Vector3RotateByQuaternion(CollisionMath_Subtract(point, instance->Pose.Position),
        QuaternionInvert(instance->Pose.Rotation));
    Vector3 Before = CollisionMath_Add(from->Position, Vector3RotateByQuaternion(Local, from->Rotation));
    Vector3 After = CollisionMath_Add(to->Position, Vector3RotateByQuaternion(Local, to->Rotation));
    return CollisionMath_Scale(CollisionMath_Subtract(After, Before), 1.0f / self->_deltaSeconds);
}

/* The motion an instance's driver gave it this step (its body's, before pushes), or none without a body. */
static void GetDriverMotion(const WorldCollision* self, const WorldCollisionInstance* instance, CollisionPose* outFrom,
    CollisionPose* outTo)
{
    if (instance->Body == WORLD_COLLISION_NO_BODY)
    {
        *outFrom = instance->Pose;
        *outTo = instance->Pose;
        return;
    }
    const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, instance->Body);
    *outFrom = Body->StepFrom;
    *outTo = Body->StepTo;
}

/* The push recorded for a contact point (its key and feature), or 0; adds every push of the key to @p total once (when
 * @p total is not NULL). */
static float FindPush(const GenericBuffer* pushes, const WorldCollisionContactKey* key, uint32_t featureId)
{
    const WorldCollisionPush* Pushes = (const WorldCollisionPush*)(const void*)pushes->_data;
    WorldCollisionPush Probe = { .Key = *key, .FeatureId = featureId, .Push = 0.0f };
    size_t Low = 0U;
    size_t High = pushes->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        int Order = ComparePushRecords(&Pushes[Middle], &Probe);
        if (Order == 0)
        {
            return Pushes[Middle].Push;
        }
        if (Order < 0)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return 0.0f;
}

static float SumPushes(const GenericBuffer* pushes, const WorldCollisionContactKey* key)
{
    const WorldCollisionPush* Pushes = (const WorldCollisionPush*)(const void*)pushes->_data;
    WorldCollisionPush Probe = { .Key = *key, .FeatureId = 0U, .Push = 0.0f };
    size_t Low = 0U;
    size_t High = pushes->_count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (ComparePushRecords(&Pushes[Middle], &Probe) < 0)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    float Sum = 0.0f;
    for (size_t Index = Low; (Index < pushes->_count)
        && (WorldCollisionInternal_CompareContactKeys(&Pushes[Index].Key, key) == 0); Index++)
    {
        Sum += Pushes[Index].Push;
    }
    return Sum;
}

/* Sorts the step's push records and sums those of one key and feature into one. */
static void MergePushes(WorldCollision* self)
{
    if (self->_pushes._count < 2U)
    {
        return;
    }
    WorldCollisionPush Scratch[2];
    GenericBuffer_SortAscending(&self->_pushes, ComparePushes, NULL, Scratch);
    WorldCollisionPush* Pushes = (WorldCollisionPush*)(void*)self->_pushes._data;
    size_t Unique = 1U;
    for (size_t Index = 1U; Index < self->_pushes._count; Index++)
    {
        if (ComparePushRecords(&Pushes[Index], &Pushes[Unique - 1U]) == 0)
        {
            Pushes[Unique - 1U].Push += Pushes[Index].Push;
        }
        else
        {
            Pushes[Unique++] = Pushes[Index];
        }
    }
    GenericBuffer_SetCount(&self->_pushes, Unique);
}

/* Fills a contact's mesh triangle data on the side that is a mesh: its index, part, material and the barycentrics of
 * the contact's first point on it. */
static void FillContactTriangle(const WorldCollisionInstance* instance, uint32_t shapeIndex, uint32_t triangle,
    Vector3 point, WorldCollisionShapeInfo* info)
{
    const WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(instance, shapeIndex);
    const CollisionMesh* Triangles = Shape->Shape->Mesh;
    if ((Triangles == NULL) || (triangle >= Triangles->_triangleCount))
    {
        return;
    }
    WorldCollisionInternal_FillTriangleInfo(Triangles, triangle, info);
    CollisionConvex View;
    CollisionConvex_FromMeshTriangle(Triangles, triangle, &Shape->ShapeToWorld, Shape->Shape->IsTwoSided, &View);
    uint32_t Region = 0U;
    CollisionMath_GetClosestTrianglePoint(View.Vertices[0], View.Vertices[1], View.Vertices[2], point,
        &info->Barycentric, &Region);
}

/* A manifold of the solver's as a contact: A the lower object id, local positions, motions and pushes. */
static void AddStepContact(WorldCollision* self, const WorldCollisionManifold* manifold)
{
    bool IsSwapped = false;
    WorldCollisionContactKey Key = WorldCollisionInternal_GetContactKey(self, manifold->SlotA, manifold->ShapeA,
        manifold->SlotB, manifold->ShapeB, manifold->Triangle, &IsSwapped);
    uint32_t SlotA = IsSwapped ? manifold->SlotB : manifold->SlotA;
    uint32_t SlotB = IsSwapped ? manifold->SlotA : manifold->SlotB;
    const WorldCollisionInstance* InstanceA = WorldCollisionInternal_GetInstance(self, SlotA);
    const WorldCollisionInstance* InstanceB = WorldCollisionInternal_GetInstance(self, SlotB);
    WorldContact Contact;
    Memory_Zero(&Contact, sizeof(Contact));
    WorldContactManifold* Manifold = &Contact.Manifold;
    WorldCollisionInternal_FillShapeInfo(InstanceA, WorldCollisionInternal_GetShape(InstanceA, Key.ShapeA),
        &Manifold->ShapeA);
    WorldCollisionInternal_FillShapeInfo(InstanceB, WorldCollisionInternal_GetShape(InstanceB, Key.ShapeB),
        &Manifold->ShapeB);
    Manifold->Normal = IsSwapped ? CollisionMath_Negate(manifold->Contact.Normal) : manifold->Contact.Normal;
    Manifold->Separation = manifold->Contact.Separation;
    Manifold->PointCount = manifold->Contact.PointCount;
    CollisionPose DriverFromA;
    CollisionPose DriverToA;
    CollisionPose DriverFromB;
    CollisionPose DriverToB;
    GetDriverMotion(self, InstanceA, &DriverFromA, &DriverToA);
    GetDriverMotion(self, InstanceB, &DriverFromB, &DriverToB);
    for (uint32_t Point = 0U; Point < manifold->Contact.PointCount; Point++)
    {
        const CollisionContactPoint* Source = &manifold->Contact.Points[Point];
        WorldContactPoint* Target = &Manifold->Points[Point];
        WorldContactPointDetail* Detail = &Contact.Points[Point];
        Target->PositionA = IsSwapped ? Source->PositionB : Source->PositionA;
        Target->PositionB = IsSwapped ? Source->PositionA : Source->PositionB;
        Target->LocalPositionA = CollisionAffine_TransformPoint(&InstanceA->WorldToObject, Target->PositionA);
        Target->LocalPositionB = CollisionAffine_TransformPoint(&InstanceB->WorldToObject, Target->PositionB);
        Target->Separation = Source->Separation;
        Target->FeatureId = Source->FeatureId;
        // The relative velocity over the step's motions, and the closing before the pushes (the drivers' motions).
        Vector3 Relative = CollisionMath_Subtract(
            GetPointVelocity(self, InstanceB, &InstanceB->LastPose, &InstanceB->Pose, Target->PositionB),
            GetPointVelocity(self, InstanceA, &InstanceA->LastPose, &InstanceA->Pose, Target->PositionA));
        Detail->NormalSpeed = CollisionMath_Dot(Relative, Manifold->Normal);
        Detail->TangentVelocity = CollisionMath_AddScaled(Relative, Manifold->Normal, -Detail->NormalSpeed);
        Vector3 Driven = CollisionMath_Subtract(
            GetPointVelocity(self, InstanceB, &DriverFromB, &DriverToB, Target->PositionB),
            GetPointVelocity(self, InstanceA, &DriverFromA, &DriverToA, Target->PositionA));
        Detail->Push = FindPush(&self->_pushes, &Key, Source->FeatureId);
        Detail->PushSpeed = Detail->Push / self->_deltaSeconds;
        Detail->IsTouching = Source->Separation <= WORLD_COLLISION_CONTACT_DISTANCE;
        Contact.IsTouching = Contact.IsTouching || Detail->IsTouching;
        if (Detail->IsTouching)
        {
            Contact.ApproachSpeed = fmaxf(Contact.ApproachSpeed, -CollisionMath_Dot(Driven, Manifold->Normal));
        }
    }
    Contact.TotalPush = SumPushes(&self->_pushes, &Key);
    if (Key.Triangle != WORLD_COLLISION_NO_TRIANGLE)
    {
        bool IsMeshA = WorldCollisionInternal_GetShape(InstanceA, Key.ShapeA)->Shape->Type == CollisionShapeType_Mesh;
        FillContactTriangle(IsMeshA ? InstanceA : InstanceB, IsMeshA ? Key.ShapeA : Key.ShapeB, Key.Triangle,
            IsMeshA ? Manifold->Points[0].PositionA : Manifold->Points[0].PositionB,
            IsMeshA ? &Manifold->ShapeA : &Manifold->ShapeB);
    }
    GenericBuffer_AddLast(&self->_contacts, &Contact);
}

/* Whether an instance was pushed in this step (a body its owner moves): the solver found its pairs afresh. */
static bool IsPushedInStep(const WorldCollision* self, const WorldCollisionInstance* instance)
{
    return (instance->Body != WORLD_COLLISION_NO_BODY)
        && (WorldCollisionInternal_GetBodyAt(self, instance->Body)->Owner != WORLD_COLLISION_NO_BODY);
}

/* A lookup of active instances by object id that keeps its last answer (sorted contacts repeat their objects). */
typedef struct InstanceMemoStruct
{
    bool IsValid;
    uint64_t ObjectId;
    const WorldCollisionInstance* Instance;
} InstanceMemo;

static const WorldCollisionInstance* FindRemembered(const WorldCollision* self, InstanceMemo* memo, uint64_t objectId)
{
    if (!memo->IsValid || (memo->ObjectId != objectId))
    {
        memo->IsValid = true;
        memo->ObjectId = objectId;
        memo->Instance = FindActiveInstance(self, objectId, NULL);
    }
    return memo->Instance;
}

/* Whether a contact of the last step carries over unchanged: the solver did not find it afresh (neither collider was
 * pushed in this step: nothing moved near it), and both are still there (@p instanceA and @p instanceB, NULL when gone)
 * with their shapes. */
static bool IsCarriedOver(const WorldCollision* self, const WorldContact* contact,
    const WorldCollisionInstance* instanceA, const WorldCollisionInstance* instanceB)
{
    return (instanceA != NULL) && (instanceB != NULL) && !IsPushedInStep(self, instanceA)
        && !IsPushedInStep(self, instanceB) && !instanceA->IsChanged && !instanceB->IsChanged
        && (contact->Manifold.ShapeA.ShapeIndex < (uint32_t)instanceA->Shapes._count)
        && (contact->Manifold.ShapeB.ShapeIndex < (uint32_t)instanceB->Shapes._count)
        && WorldCollisionInternal_DoBlock(instanceA, instanceB);
}

/* Adds the last step's contacts that carry over, a step older: no motion and no push in this step. */
static void CarryOverContacts(WorldCollision* self)
{
    InstanceMemo MemoA = { .IsValid = false };
    InstanceMemo MemoB = { .IsValid = false };
    const WorldContact* Previous = (const WorldContact*)(const void*)self->_previousContacts._data;
    for (size_t Index = 0U; Index < self->_previousContacts._count; Index++)
    {
        const WorldContact* Contact = &Previous[Index];
        if (!IsCarriedOver(self, Contact, FindRemembered(self, &MemoA, Contact->Manifold.ShapeA.ObjectId),
            FindRemembered(self, &MemoB, Contact->Manifold.ShapeB.ObjectId)))
        {
            continue;
        }
        GenericBuffer_AddLast(&self->_contacts, (void*)Contact);
        WorldContact* Carried = &((WorldContact*)(void*)self->_contacts._data)[self->_contacts._count - 1U];
        for (uint32_t Point = 0U; Point < Carried->Manifold.PointCount; Point++)
        {
            Carried->Points[Point].NormalSpeed = 0.0f;
            Carried->Points[Point].TangentVelocity = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
            Carried->Points[Point].Push = 0.0f;
            Carried->Points[Point].PushSpeed = 0.0f;
        }
        Carried->ApproachSpeed = 0.0f;
        Carried->TotalPush = 0.0f;
    }
}

static bool AreContactsSorted(const GenericBuffer* contacts)
{
    const WorldContact* Contacts = (const WorldContact*)(const void*)contacts->_data;
    for (size_t Index = 1U; Index < contacts->_count; Index++)
    {
        WorldCollisionContactKey Before = GetKeyOfContact(&Contacts[Index - 1U]);
        WorldCollisionContactKey After = GetKeyOfContact(&Contacts[Index]);
        if (WorldCollisionInternal_CompareContactKeys(&Before, &After) > 0)
        {
            return false;
        }
    }
    return true;
}

/* Carries the age of a contact, and of each of its points, over from the last step's contact of the same key. Both
 * lists are sorted: @p cursor walks the last step's along with this step's. */
static void MatchAge(const WorldCollision* self, WorldContact* contact, size_t* cursor)
{
    const WorldContact* Previous = (const WorldContact*)(const void*)self->_previousContacts._data;
    WorldCollisionContactKey Key = GetKeyOfContact(contact);
    for (; *cursor < self->_previousContacts._count; (*cursor)++)
    {
        WorldCollisionContactKey BeforeKey = GetKeyOfContact(&Previous[*cursor]);
        int Order = WorldCollisionInternal_CompareContactKeys(&BeforeKey, &Key);
        if (Order > 0)
        {
            return; // a new contact
        }
        if (Order < 0)
        {
            continue;
        }
        const WorldContact* Before = &Previous[*cursor];
        contact->AgeTicks = Before->AgeTicks + 1U;
        for (uint32_t Point = 0U; Point < contact->Manifold.PointCount; Point++)
        {
            for (uint32_t Old = 0U; Old < Before->Manifold.PointCount; Old++)
            {
                if (Before->Manifold.Points[Old].FeatureId == contact->Manifold.Points[Point].FeatureId)
                {
                    contact->Points[Point].AgeTicks = Before->Points[Old].AgeTicks + 1U;
                    break;
                }
            }
        }
        return;
    }
}


// Static functions: events and results.
/* The share of the step at which a pair would first have touched along its drivers' motions (0 without a record of how
 * it started). */
static float GetTimeOfImpact(const WorldCollision* self, const WorldContact* contacts, size_t count)
{
    const WorldCollisionStart* Starts = (const WorldCollisionStart*)(const void*)self->_starts._data;
    float Earliest = 1.0f;
    bool IsFound = false;
    for (size_t Index = 0U; Index < count; Index++)
    {
        WorldCollisionContactKey Key = GetKeyOfContact(&contacts[Index]);
        size_t Low = 0U;
        size_t High = self->_starts._count;
        while (Low < High)
        {
            size_t Middle = Low + ((High - Low) / 2U);
            if (WorldCollisionInternal_CompareContactKeys(&Starts[Middle].Key, &Key) < 0)
            {
                Low = Middle + 1U;
            }
            else
            {
                High = Middle;
            }
        }
        for (size_t Start = Low; (Start < self->_starts._count)
            && (WorldCollisionInternal_CompareContactKeys(&Starts[Start].Key, &Key) == 0); Start++)
        {
            float Gap = fmaxf(0.0f, Starts[Start].Separation - WORLD_COLLISION_CONTACT_DISTANCE);
            float Time = (Starts[Start].Closing > 0.0f) ? fminf(1.0f, Gap / Starts[Start].Closing) : 0.0f;
            Earliest = fminf(Earliest, Time);
            IsFound = true;
        }
    }
    return IsFound ? Earliest : 0.0f;
}

static void QueueContactEvent(WorldCollision* self, WorldContactEventKind kind, const WorldCollisionContactPair* pair,
    const WorldContact* contacts, size_t count)
{
    WorldContactEvent Event =
    {
        .Kind = kind, .ObjectIdA = pair->ObjectA, .ObjectIdB = pair->ObjectB, .Tick = self->_tick,
        .AgeTicks = pair->AgeTicks, .ApproachSpeed = pair->ApproachSpeed, .TimeOfImpact = pair->TimeOfImpact,
        .FirstContact = (uint32_t)self->_eventContacts._count, .ContactCount = (uint32_t)count
    };
    if (count > 0U)
    {
        GenericBuffer_AddLastRange(&self->_eventContacts, (void*)contacts, count);
    }
    GenericBuffer_AddLast(&self->_contactEvents, &Event);
}

/* One side of a touching contact in its object's result: its count, and the ground if it faces up the most. */
static void AddToResult(WorldCollision* self, WorldCollisionInstance* instance, const WorldCollisionInstance* other,
    Vector3 pushOut, Vector3 otherPoint)
{
    WorldMotionResult* Result = WorldCollisionInternal_TouchResult(self, instance);
    Result->ContactCount++;
    if (!(pushOut.y > 0.0f) || (Result->HasGround && (pushOut.y <= Result->GroundNormal.y)))
    {
        return;
    }
    Result->HasGround = true;
    Result->GroundObjectId = other->ObjectId;
    Result->GroundNormal = pushOut;
    Result->GroundPoint = otherPoint;
    Result->GroundVelocity = GetPointVelocity(self, other, &other->LastPose, &other->Pose, otherPoint);
}

/* One side of a touching contact in its object's result, and in the result of the pushable that carries it. */
static void AddSideToResults(WorldCollision* self, uint32_t slot, const WorldCollisionInstance* other, Vector3 pushOut,
    Vector3 otherPoint)
{
    WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, slot);
    AddToResult(self, Instance, other, pushOut, otherPoint);
    if ((Instance->OwnerSlot != WORLD_COLLISION_NO_SLOT) && (Instance->OwnerSlot != slot))
    {
        AddToResult(self, WorldCollisionInternal_GetInstance(self, Instance->OwnerSlot), other, pushOut, otherPoint);
    }
}

/* A touching contact in both objects' results (its deepest point stands for it). */
static void AddContactToResults(WorldCollision* self, const WorldContact* contact, uint32_t slotA, uint32_t slotB)
{
    const WorldContactManifold* Manifold = &contact->Manifold;
    uint32_t Deepest = 0U;
    for (uint32_t Point = 1U; Point < Manifold->PointCount; Point++)
    {
        Deepest = (Manifold->Points[Point].Separation < Manifold->Points[Deepest].Separation) ? Point : Deepest;
    }
    const WorldContactPoint* Point = &Manifold->Points[Deepest];
    AddSideToResults(self, slotA, WorldCollisionInternal_GetInstance(self, slotB), CollisionMath_Negate(Manifold->Normal),
        Point->PositionB);
    AddSideToResults(self, slotB, WorldCollisionInternal_GetInstance(self, slotA), Manifold->Normal, Point->PositionA);
}

/* The object pairs in contact, a run of the sorted contacts each: the contacts' ages, the objects' results (counts and
 * grounds), the pairs and their events (begins and persists in pair order, then ends). */
static void UpdateContactPairs(WorldCollision* self)
{
    GenericBuffer_Clear(&self->_newContactPairs);
    WorldContact* Contacts = (WorldContact*)(void*)self->_contacts._data;
    const WorldCollisionContactPair* Previous = (const WorldCollisionContactPair*)(const void*)self->_contactPairs._data;
    size_t ContactCursor = 0U;
    size_t PairCursor = 0U;
    size_t First = 0U;
    while (First < self->_contacts._count)
    {
        uint64_t ObjectA = Contacts[First].Manifold.ShapeA.ObjectId;
        uint64_t ObjectB = Contacts[First].Manifold.ShapeB.ObjectId;
        size_t End = FindPairEnd(&self->_contacts, First, ObjectA, ObjectB);
        uint32_t SlotA = WORLD_COLLISION_NO_SLOT;
        uint32_t SlotB = WORLD_COLLISION_NO_SLOT;
        const WorldCollisionInstance* InstanceA = FindActiveInstance(self, ObjectA, &SlotA);
        const WorldCollisionInstance* InstanceB = FindActiveInstance(self, ObjectB, &SlotB);
        bool IsTouching = false;
        float ApproachSpeed = 0.0f;
        for (size_t Index = First; Index < End; Index++)
        {
            MatchAge(self, &Contacts[Index], &ContactCursor);
            IsTouching = IsTouching || Contacts[Index].IsTouching;
            ApproachSpeed = fmaxf(ApproachSpeed, Contacts[Index].ApproachSpeed);
            if (!Contacts[Index].IsTouching)
            {
                continue;
            }
            if ((InstanceA != NULL) && (InstanceB != NULL))
            {
                AddContactToResults(self, &Contacts[Index], SlotA, SlotB);
            }
            self->_stepStats.ContactCount++;
        }
        while ((PairCursor < self->_contactPairs._count) && (CompareObjectPairs(Previous[PairCursor].ObjectA,
            Previous[PairCursor].ObjectB, ObjectA, ObjectB) < 0))
        {
            PairCursor++;
        }
        const WorldCollisionContactPair* Before = ((PairCursor < self->_contactPairs._count) && (CompareObjectPairs(
            Previous[PairCursor].ObjectA, Previous[PairCursor].ObjectB, ObjectA, ObjectB) == 0)) ? &Previous[PairCursor]
            : NULL;
        if ((Before != NULL) || IsTouching)
        {
            WorldCollisionContactPair Pair;
            if (Before != NULL)
            {
                Pair = *Before;
                Pair.AgeTicks++;
            }
            else
            {
                Pair = (WorldCollisionContactPair){ .ObjectA = ObjectA, .ObjectB = ObjectB, .AgeTicks = 0U,
                    .ApproachSpeed = ApproachSpeed, .TimeOfImpact = GetTimeOfImpact(self, &Contacts[First],
                        End - First) };
            }
            Pair.ReportsPersisting = ((InstanceA != NULL) && InstanceA->Settings.ReportsPersistingContacts)
                || ((InstanceB != NULL) && InstanceB->Settings.ReportsPersistingContacts);
            GenericBuffer_AddLast(&self->_newContactPairs, &Pair);
            if (Before == NULL)
            {
                QueueContactEvent(self, WorldContactEventKind_Begin, &Pair, &Contacts[First], End - First);
            }
            else if (Pair.ReportsPersisting)
            {
                QueueContactEvent(self, WorldContactEventKind_Persist, &Pair, &Contacts[First], End - First);
            }
        }
        First = End;
    }
    const WorldCollisionContactPair* Next = (const WorldCollisionContactPair*)(const void*)self->_newContactPairs._data;
    size_t NextCursor = 0U;
    for (size_t Index = 0U; Index < self->_contactPairs._count; Index++)
    {
        const WorldCollisionContactPair* Pair = &Previous[Index];
        while ((NextCursor < self->_newContactPairs._count) && (CompareObjectPairs(Next[NextCursor].ObjectA,
            Next[NextCursor].ObjectB, Pair->ObjectA, Pair->ObjectB) < 0))
        {
            NextCursor++;
        }
        if ((NextCursor < self->_newContactPairs._count) && (CompareObjectPairs(Next[NextCursor].ObjectA,
            Next[NextCursor].ObjectB, Pair->ObjectA, Pair->ObjectB) == 0))
        {
            continue;
        }
        // Ended: the event carries the pair's last contacts.
        size_t Start = FindFirstOfPair(&self->_previousContacts, Pair->ObjectA, Pair->ObjectB);
        size_t Stop = FindPairEnd(&self->_previousContacts, Start, Pair->ObjectA, Pair->ObjectB);
        WorldCollisionContactPair Ended = *Pair;
        Ended.AgeTicks++;
        QueueContactEvent(self, WorldContactEventKind_End, &Ended,
            &((const WorldContact*)(const void*)self->_previousContacts._data)[Start], Stop - Start);
    }
    GenericBuffer Swap = self->_contactPairs;
    self->_contactPairs = self->_newContactPairs;
    self->_newContactPairs = Swap;
}

static void RebuildDebugContacts(WorldCollision* self)
{
    GenericBuffer_Clear(&self->_debugContacts);
    if (!self->_isDebugContactsEnabled)
    {
        return;
    }
    const WorldContact* Contacts = (const WorldContact*)(const void*)self->_contacts._data;
    for (size_t Index = 0U; Index < self->_contacts._count; Index++)
    {
        const WorldContact* Contact = &Contacts[Index];
        for (uint32_t Point = 0U; Point < Contact->Manifold.PointCount; Point++)
        {
            const WorldContactPoint* Source = &Contact->Manifold.Points[Point];
            WorldCollisionDebugContact Debug =
            {
                .Point = CollisionMath_Scale(CollisionMath_Add(Source->PositionA, Source->PositionB), 0.5f),
                .Normal = Contact->Manifold.Normal, .PushSpeed = Contact->Points[Point].PushSpeed,
                .IsTouching = Contact->Points[Point].IsTouching
            };
            GenericBuffer_AddLast(&self->_debugContacts, &Debug);
        }
    }
}


// Internal functions (WorldCollisionInternal.h).
WorldCollisionContactKey WorldCollisionInternal_GetContactKey(const WorldCollision* self, uint32_t slotA,
    uint32_t shapeA, uint32_t slotB, uint32_t shapeB, uint32_t triangle, bool* outIsSwapped)
{
    uint64_t ObjectA = WorldCollisionInternal_GetInstance(self, slotA)->ObjectId;
    uint64_t ObjectB = WorldCollisionInternal_GetInstance(self, slotB)->ObjectId;
    *outIsSwapped = ObjectA > ObjectB;
    return *outIsSwapped
        ? (WorldCollisionContactKey){ .ObjectA = ObjectB, .ObjectB = ObjectA, .ShapeA = shapeB, .ShapeB = shapeA,
            .Triangle = triangle }
        : (WorldCollisionContactKey){ .ObjectA = ObjectA, .ObjectB = ObjectB, .ShapeA = shapeA, .ShapeB = shapeB,
            .Triangle = triangle };
}

int WorldCollisionInternal_CompareContactKeys(const WorldCollisionContactKey* a, const WorldCollisionContactKey* b)
{
    int Order = CompareObjectPairs(a->ObjectA, a->ObjectB, b->ObjectA, b->ObjectB);
    if (Order != 0)
    {
        return Order;
    }
    uint32_t KeysA[3] = { a->ShapeA, a->ShapeB, a->Triangle };
    uint32_t KeysB[3] = { b->ShapeA, b->ShapeB, b->Triangle };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        if (KeysA[Index] != KeysB[Index])
        {
            return (KeysA[Index] < KeysB[Index]) ? -1 : 1;
        }
    }
    return 0;
}

void WorldCollisionInternal_UpdateContacts(WorldCollision* self)
{
    GenericBuffer Swap = self->_previousContacts;
    self->_previousContacts = self->_contacts;
    self->_contacts = Swap;
    GenericBuffer_Clear(&self->_contacts);
    MergePushes(self);
    if (self->_starts._count > 1U)
    {
        WorldCollisionStart Scratch[2];
        GenericBuffer_SortAscending(&self->_starts, CompareStarts, NULL, Scratch);
    }
    const WorldCollisionManifold* Manifolds = (const WorldCollisionManifold*)(const void*)self->_manifolds._data;
    for (size_t Index = 0U; Index < self->_manifolds._count; Index++)
    {
        AddStepContact(self, &Manifolds[Index]);
    }
    CarryOverContacts(self);
    if (!AreContactsSorted(&self->_contacts))
    {
        WorldContact Scratch[2];
        GenericBuffer_SortAscending(&self->_contacts, CompareContacts, NULL, Scratch);
    }
    UpdateContactPairs(self);
    RebuildDebugContacts(self);
    self->_stepStats.ContactPairCount = self->_contactPairs._count;
}

void WorldCollisionInternal_QueueMotionEvents(WorldCollision* self, const WorldCollisionInstance* instance)
{
    const WorldMotionResult* Result = &instance->Result;
    bool IsBlocked = WorldCollisionInternal_IsFlagged(self, instance, false);
    bool IsCrushed = WorldCollisionInternal_IsFlagged(self, instance, true);
    WorldMotionEvent Event = { .ObjectId = instance->ObjectId, .Tick = self->_tick };
    if (IsBlocked != instance->WasBlocked)
    {
        Event.Kind = IsBlocked ? WorldMotionEventKind_BlockedBegin : WorldMotionEventKind_BlockedEnd;
        Event.OtherId = IsBlocked ? Result->BlockedById : 0U;
        Event.IsStopped = IsBlocked && Result->IsStopped;
        Event.Depth = 0.0f;
        GenericBuffer_AddLast(&self->_motionEvents, &Event);
    }
    if (IsCrushed != instance->WasCrushed)
    {
        Event.Kind = IsCrushed ? WorldMotionEventKind_CrushedBegin : WorldMotionEventKind_CrushedEnd;
        Event.OtherId = IsCrushed ? Result->CrushedById : 0U;
        Event.IsStopped = false;
        Event.Depth = IsCrushed ? Result->CrushDepth : 0.0f;
        GenericBuffer_AddLast(&self->_motionEvents, &Event);
    }
}


// Public functions.
const WorldContact* WorldCollision_GetContacts(const WorldCollision* self, size_t* outCount)
{
    *outCount = self->_contacts._count;
    return (self->_contacts._count > 0U) ? (const WorldContact*)(const void*)self->_contacts._data : NULL;
}

const WorldContactEvent* WorldCollision_GetContactEvents(const WorldCollision* self, size_t* outCount)
{
    *outCount = self->_contactEvents._count;
    return (self->_contactEvents._count > 0U) ? (const WorldContactEvent*)(const void*)self->_contactEvents._data : NULL;
}

const WorldContact* WorldCollision_GetContactEventContacts(const WorldCollision* self, size_t* outCount)
{
    *outCount = self->_eventContacts._count;
    return (self->_eventContacts._count > 0U) ? (const WorldContact*)(const void*)self->_eventContacts._data : NULL;
}

void WorldCollision_ClearContactEvents(WorldCollision* self)
{
    GenericBuffer_Clear(&self->_contactEvents);
    GenericBuffer_Clear(&self->_eventContacts);
}

const WorldMotionEvent* WorldCollision_GetMotionEvents(const WorldCollision* self, size_t* outCount)
{
    *outCount = self->_motionEvents._count;
    return (self->_motionEvents._count > 0U) ? (const WorldMotionEvent*)(const void*)self->_motionEvents._data : NULL;
}

void WorldCollision_ClearMotionEvents(WorldCollision* self)
{
    GenericBuffer_Clear(&self->_motionEvents);
}
