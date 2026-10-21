#include <float.h>
#include <math.h>
#include "WorldCollisionInternal.h"
#include "raylib/raymath.h"

/*
 * The collision world's push-out solver (include/WorldCollision.h, "Moving and pushing"; the step's types in
 * source/WorldCollisionInternal.h), run by WorldCollision_Step over the bodies source/WorldCollisionStep.c made.
 *
 * A SOLVE runs over one stretch of the step's motions: each body moves from From to To (a substep of its own motion, or
 * no motion at all while pushing out what overlaps at the end), and each owner (a pushable) gets one push on top, a
 * translation. It:
 * 1. finds the pairs' contacts at the From poses, out to how far each pair can close in the stretch plus a margin;
 * 2. turns every contact point into a ROW: the separation the motions would leave (the two material points carried by
 *    their bodies' motions, measured along the contact's normal) must be at least WORLD_COLLISION_SKIN, and the owners'
 *    pushes along the normal change it;
 * 3. solves the rows by projected Gauss-Seidel: each row pushes its owners apart by its violation, shared by inverse
 *    mass, its accumulated push clamped at 0 (a push never pulls), sweeping forward and backward in turn until no pushed
 *    body moves by more than WORLD_COLLISION_SKIN / 100 in an iteration. Every solve starts from no push (no warm
 *    starting: last step's pushes, re-applied row by row, threw a light pushable pinned by a heavy one around). Rows
 *    against what neither moves nor gives way (static colliders, kinematic ones standing still) close every iteration,
 *    so what cannot fit is left between pushables and movers, never in a wall. Only the normal part of a motion is
 *    touched, so pushed things slide;
 * 4. SWEEPS every continuous pushed motion with its push: exact casts of each pushed shape against everything near it,
 *    the other side moving too, wherever the two move in straight lines relative to each other (a side whose motion is
 *    not continuous stands where it ends: it jumps there); a pushed body that turns is swept along its straight path at
 *    its final turn against what stands still (a push through a wall is caught). A hit becomes a row at the time of
 *    impact and the rows are solved again; after WORLD_COLLISION_SWEEP_ROUNDS, a motion that still hits stops just
 *    before its time of impact, or blocks the kinematic mover it ran into. TURNS are the substeps' job: at most
 *    WORLD_COLLISION_MAX_SUBSTEP_TURN each, with contacts taken before each out to how far the turn can reach, so
 *    whatever a turn would hit is pushed ahead of it (a cast through a turn would see the pushed body's straight path
 *    cut the turning one, which no push can mend). Only motions longer than WORLD_COLLISION_SPECULATIVE_DISTANCE in
 *    the solve are swept: a shorter one stays within the reach of the solve's contacts, which hold it; a pair is swept
 *    by a side that sweeps (once: by the lower when both do);
 * 5. finds what did not fit: the ISLANDS of pushed bodies joined by rows; an island with a row still violated by more
 *    than the slop could not make room, so every kinematic mover whose row presses into it (pushes) is blocked;
 * 6. adds the pushes to the owners' accumulated ones and widens their tree boxes, so later searches find them.
 *
 * THE STEP'S SOLVE: substeps (more than one when a turn passes WORLD_COLLISION_MAX_SUBSTEP_TURN), each a solve over its
 * share of the motions, with the pairs found once over the whole step; then solves without motion at the final poses,
 * each with the pairs and contacts found there, while something still overlaps by more than WORLD_COLLISION_SLOP (at
 * most WORLD_COLLISION_DEPENETRATION_ROUNDS). The contacts found last at the final poses are the step's (_manifolds);
 * the blocked and crushed flags come from them and from the sweeps. A solve that may stop movers ends early once one
 * that stops when blocked is blocked and can be set back: the step sets it back and solves again.
 */


// Macros.
/** Gauss-Seidel stops once no pushed body moves by more than this in an iteration (metres: a hundredth of the skin).
 *  The bodies, not the rows: the rows of one face (a box resting on a box has four) trade their shares of a push
 *  without moving anything. */
#define SOLVER_CONVERGED_PUSH 1e-5f
/** The Gauss-Seidel's relaxation: each row's correction is scaled by it (1 is plain Gauss-Seidel). */
#define SOLVER_RELAXATION 1.0f
/** A turn below this (radians) is no turn: the motion is linear. */
#define TURN_EPSILON 1e-7f
/** The feature id of a row a sweep made (it matches no manifold point). */
#define CAST_FEATURE_ID UINT32_MAX
/** A depenetration round that leaves the deepest overlap at more than this share of what it was stops the rounds (the
 *  separations are negative: deeper is lower). */
#define DEPENETRATION_PROGRESS 0.9f


// Types.
/* The contacts of one pair against a mesh's triangles. */
typedef struct MeshContactRunStruct
{
    WorldCollision* Self;
    const CollisionConvex* A;
    const CollisionMesh* Mesh;
    const CollisionAffine* MeshToWorld;
    bool IsTwoSided;
    float MaxSeparation;
    WorldCollisionManifold Template;
} MeshContactRun;

/* A sweep against a mesh's triangles: the earliest hit. */
typedef struct MeshCastRunStruct
{
    const CollisionConvex* A;
    Vector3 Relative;
    const CollisionMesh* Mesh;
    const CollisionAffine* MeshToWorld;
    bool IsTwoSided;
    bool HasHit;
    CollisionCastHit Hit;
    uint32_t Triangle;
} MeshCastRun;


// Static functions: poses and boxes.
static bool ArePosesEqual(const CollisionPose* a, const CollisionPose* b)
{
    return (a->Position.x == b->Position.x) && (a->Position.y == b->Position.y) && (a->Position.z == b->Position.z)
        && (a->Rotation.x == b->Rotation.x) && (a->Rotation.y == b->Rotation.y) && (a->Rotation.z == b->Rotation.z)
        && (a->Rotation.w == b->Rotation.w);
}

static bool IsConvexShape(const CollisionShape* shape)
{
    return (shape->Type != CollisionShapeType_Plane) && (shape->Type != CollisionShapeType_Mesh);
}

/* A body's pose at the share @p t of its step's motion (exactly its ends at 0 and 1). */
static CollisionPose GetStepPose(const WorldCollisionBody* body, float t)
{
    if (!body->IsMoving || (t <= 0.0f))
    {
        return body->StepFrom;
    }
    return (t >= 1.0f) ? body->StepTo : CollisionPose_Interpolate(&body->StepFrom, &body->StepTo, t);
}

/* A body's pose at the end of the running solve: its target with its owner's push. */
static CollisionPose GetFinalPose(const WorldCollision* self, const WorldCollisionBody* body)
{
    CollisionPose Pose = body->To;
    if (body->Owner != WORLD_COLLISION_NO_BODY)
    {
        Pose.Position = CollisionMath_Add(Pose.Position, WorldCollisionInternal_GetBodyAt(self, body->Owner)->Correction);
    }
    return Pose;
}

/* The running solve's push on a body: its owner's correction (none without an owner). */
static Vector3 GetBodyCorrection(const WorldCollision* self, const WorldCollisionBody* body)
{
    return (body->Owner != WORLD_COLLISION_NO_BODY) ? WorldCollisionInternal_GetBodyAt(self, body->Owner)->Correction
        : CollisionMath_Vector(0.0f, 0.0f, 0.0f);
}

/* Where a material point that lies at @p point while its body is at From goes with the body's motion to To (no push). */
static Vector3 MovePoint(const WorldCollisionBody* body, Vector3 point)
{
    Vector3 Offset = CollisionMath_Subtract(point, body->From.Position);
    if (body->IsPhaseTurning)
    {
        Offset = Vector3RotateByQuaternion(Offset, body->PhaseTurn);
    }
    return CollisionMath_Add(body->To.Position, Offset);
}

/* Where the material point that lies at @p point while its body is at pose @p at lies once the body is at pose
 * @p to. */
static Vector3 CarryPoint(const CollisionPose* at, const CollisionPose* to, Vector3 point)
{
    Vector3 Local = Vector3RotateByQuaternion(CollisionMath_Subtract(point, at->Position), QuaternionInvert(at->Rotation));
    return CollisionMath_Add(to->Position, Vector3RotateByQuaternion(Local, to->Rotation));
}

/* A shape of an instance placed at @p pose, one way (the synced placement itself while the pose is the instance's own). */
static CollisionAffine PlaceShapeToWorld(const WorldCollisionInstance* instance, const WorldCollisionShape* shape,
    const CollisionPose* pose)
{
    if (ArePosesEqual(pose, &instance->Pose))
    {
        return shape->ShapeToWorld;
    }
    CollisionAffine PoseToWorld = CollisionPose_ToAffine(pose);
    return CollisionAffine_Multiply(&PoseToWorld, &shape->ShapeToPose);
}

/* A shape of an instance placed at @p pose (the synced placement itself while the pose is the instance's own). */
static void PlaceShape(const WorldCollisionInstance* instance, const WorldCollisionShape* shape,
    const CollisionPose* pose, CollisionAffine* outToWorld, CollisionAffine* outFromWorld)
{
    if (ArePosesEqual(pose, &instance->Pose))
    {
        *outToWorld = shape->ShapeToWorld;
        *outFromWorld = shape->WorldToShape;
        return;
    }
    CollisionAffine PoseToWorld = CollisionPose_ToAffine(pose);
    *outToWorld = CollisionAffine_Multiply(&PoseToWorld, &shape->ShapeToPose);
    if (!CollisionAffine_TryInvert(outToWorld, outFromWorld))
    {
        *outFromWorld = shape->WorldToShape;
    }
}

/* The world box of a shape under @p toWorld; false for an unbounded plane. */
static bool GetPlacedBox(const WorldCollisionShape* shape, const CollisionAffine* toWorld, Vector3* outMin,
    Vector3* outMax)
{
    Vector3 LocalMin;
    Vector3 LocalMax;
    if (!CollisionShape_TryGetLocalBounds(shape->Shape, &LocalMin, &LocalMax))
    {
        return false;
    }
    CollisionAffine_TransformBounds(toWorld, LocalMin, LocalMax, outMin, outMax);
    return true;
}

static void ExpandBox(Vector3* min, Vector3* max, float margin)
{
    Vector3 Margin = { margin, margin, margin };
    *min = CollisionMath_Subtract(*min, Margin);
    *max = CollisionMath_Add(*max, Margin);
}

/* The box of a body's valid, bounded shapes at @p pose; false when it has none. */
static bool GetBodyBox(const WorldCollision* self, const WorldCollisionBody* body, const CollisionPose* pose,
    Vector3* outMin, Vector3* outMax)
{
    const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, body->Slot);
    *outMin = CollisionMath_Vector(FLT_MAX, FLT_MAX, FLT_MAX);
    *outMax = CollisionMath_Vector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool HasBox = false;
    for (uint32_t Index = 0U; Index < (uint32_t)Instance->Shapes._count; Index++)
    {
        const WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(Instance, Index);
        Vector3 Min;
        Vector3 Max;
        if (!Shape->IsValid)
        {
            continue;
        }
        CollisionAffine ToWorld = PlaceShapeToWorld(Instance, Shape, pose);
        if (GetPlacedBox(Shape, &ToWorld, &Min, &Max))
        {
            *outMin = CollisionMath_Min(*outMin, Min);
            *outMax = CollisionMath_Max(*outMax, Max);
            HasBox = true;
        }
    }
    return HasBox;
}

/* The box a body's shapes cover moving from @p from to @p to (with the reach of a turn around both positions); false
 * when it has no bounded shape. */
static bool GetMotionBox(const WorldCollision* self, const WorldCollisionBody* body, const CollisionPose* from,
    const CollisionPose* to, Vector3* outMin, Vector3* outMax)
{
    Vector3 EndMin;
    Vector3 EndMax;
    if (!GetBodyBox(self, body, from, outMin, outMax) || !GetBodyBox(self, body, to, &EndMin, &EndMax))
    {
        return false;
    }
    *outMin = CollisionMath_Min(*outMin, EndMin);
    *outMax = CollisionMath_Max(*outMax, EndMax);
    if (CollisionPose_GetRotationAngle(from, to) > TURN_EPSILON)
    {
        float Reach = WorldCollisionInternal_GetInstance(self, body->Slot)->Reach;
        Vector3 Radius = { Reach, Reach, Reach };
        *outMin = CollisionMath_Min(*outMin, CollisionMath_Subtract(CollisionMath_Min(from->Position, to->Position),
            Radius));
        *outMax = CollisionMath_Max(*outMax, CollisionMath_Add(CollisionMath_Max(from->Position, to->Position), Radius));
    }
    return true;
}


// Static functions: the solve's poses.
/* Every body's motion for a solve over the share [t0, t1] of its step's motion, its owner's pushes so far added. */
static void SetPhasePoses(WorldCollision* self, float t0, float t1)
{
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        Body->From = GetStepPose(Body, t0);
        Body->To = GetStepPose(Body, t1);
        if (Body->Owner != WORLD_COLLISION_NO_BODY)
        {
            Vector3 Pushed = WorldCollisionInternal_GetBodyAt(self, Body->Owner)->Accumulated;
            Body->From.Position = CollisionMath_Add(Body->From.Position, Pushed);
            Body->To.Position = CollisionMath_Add(Body->To.Position, Pushed);
        }
        Body->PhaseTurnAngle = CollisionPose_GetRotationAngle(&Body->From, &Body->To);
        Body->IsPhaseTurning = Body->PhaseTurnAngle > TURN_EPSILON;
        Body->PhaseTurn = Body->IsPhaseTurning
            ? QuaternionMultiply(Body->To.Rotation, QuaternionInvert(Body->From.Rotation)) : QuaternionIdentity();
    }
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        Body->Correction = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
        Body->ClampFraction = 1.0f;
    }
}

/* How far a pair's bodies can close over the running solve (metres): their relative translation and their turns times
 * how far their shapes reach. */
static float GetClosingBound(const WorldCollision* self, const WorldCollisionBody* a, const WorldCollisionBody* b)
{
    Vector3 MotionA = CollisionMath_Subtract(a->To.Position, a->From.Position);
    Vector3 MotionB = CollisionMath_Subtract(b->To.Position, b->From.Position);
    float Bound = CollisionMath_Length(CollisionMath_Subtract(MotionA, MotionB));
    Bound += a->PhaseTurnAngle * WorldCollisionInternal_GetInstance(self, a->Slot)->Reach;
    Bound += b->PhaseTurnAngle * WorldCollisionInternal_GetInstance(self, b->Slot)->Reach;
    return Bound;
}


// Static functions: pairs.
static ComparisonResult ComparePairs(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    const WorldCollisionPair* A = a._element;
    const WorldCollisionPair* B = b._element;
    uint64_t KeysA[4] = { A->ObjectA, A->ObjectB, A->ShapeA, A->ShapeB };
    uint64_t KeysB[4] = { B->ObjectA, B->ObjectB, B->ShapeA, B->ShapeB };
    for (size_t Index = 0U; Index < 4U; Index++)
    {
        if (KeysA[Index] != KeysB[Index])
        {
            return (KeysA[Index] < KeysB[Index]) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
        }
    }
    return ComparisonResult_AEqualsB;
}

static void AddPair(WorldCollision* self, uint32_t slotA, uint32_t shapeA, uint32_t slotB, uint32_t shapeB)
{
    WorldCollisionPair Pair =
    {
        .SlotA = slotA, .ShapeA = shapeA, .SlotB = slotB, .ShapeB = shapeB,
        .BodyA = WorldCollisionInternal_GetBody(self, slotA), .BodyB = WorldCollisionInternal_GetBody(self, slotB),
        .ObjectA = WorldCollisionInternal_GetInstance(self, slotA)->ObjectId,
        .ObjectB = WorldCollisionInternal_GetInstance(self, slotB)->ObjectId
    };
    GenericBuffer_AddLast(&self->_pairs, &Pair);
}

/* The pairs of one pushed body with the shapes in _candidates near the box [min, max]. A is its convex shape, unless the
 * other is pushed too, convex and of the lower object id: then the other is A (each pair is found from both sides
 * alike). */
static void AddCandidatePairs(WorldCollision* self, uint32_t slot, Vector3 min, Vector3 max)
{
    const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, slot);
    for (size_t Candidate = 0U; Candidate < self->_candidates._count; Candidate++)
    {
        uint64_t Key = ((const uint64_t*)(const void*)self->_candidates._data)[Candidate];
        uint32_t OtherSlot = WorldCollisionInternal_GetKeySlot(Key);
        uint32_t OtherShape = WorldCollisionInternal_GetKeyShape(Key);
        const WorldCollisionInstance* Other = WorldCollisionInternal_GetInstance(self, OtherSlot);
        if ((OtherSlot == slot) || !Other->IsUsed || !Other->IsActive || !WorldCollisionInternal_DoBlock(Instance, Other)
            || !WorldCollisionInternal_GetShape(Other, OtherShape)->IsValid
            || !WorldCollisionInternal_IsCandidateNear(Other, WorldCollisionInternal_GetShape(Other, OtherShape), min,
                max))
        {
            continue;
        }
        const CollisionShape* Target = WorldCollisionInternal_GetShape(Other, OtherShape)->Shape;
        bool IsOtherPushed = Other->OwnerSlot != WORLD_COLLISION_NO_SLOT;
        if (IsOtherPushed && !IsConvexShape(Target))
        {
            continue; // a pushed collider's planes and meshes take no part
        }
        bool IsOtherA = IsOtherPushed && (Other->ObjectId < Instance->ObjectId);
        for (uint32_t Shape = 0U; Shape < (uint32_t)Instance->Shapes._count; Shape++)
        {
            const WorldCollisionShape* Own = WorldCollisionInternal_GetShape(Instance, Shape);
            if (!Own->IsValid || !IsConvexShape(Own->Shape))
            {
                continue; // a pushed collider's planes and meshes take no part (warned when it is made a body)
            }
            if (IsOtherA)
            {
                AddPair(self, OtherSlot, OtherShape, slot, Shape);
            }
            else
            {
                AddPair(self, slot, Shape, OtherSlot, OtherShape);
            }
        }
    }
}

/* Collects the pairs of every pushed body (one an owner pushes) with what lies within its box: the box over its step's
 * motion, or (@p isAtFinalPoses) the box at its From pose, the final one; widened by @p margin. Bodies the pairs meet
 * are made on the way (a woken pushable is collected too). Sorted, each pair once. */
static void CollectPairs(WorldCollision* self, bool isAtFinalPoses, float margin)
{
    GenericBuffer_Clear(&self->_pairs);
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        Vector3 Min = Body->SweptMin;
        Vector3 Max = Body->SweptMax;
        if ((Body->Owner == WORLD_COLLISION_NO_BODY) || (isAtFinalPoses && !GetBodyBox(self, Body, &Body->From, &Min,
            &Max)) || (Min.x > Max.x))
        {
            continue;
        }
        uint32_t Slot = Body->Slot;
        ExpandBox(&Min, &Max, margin);
        WorldCollisionInternal_CollectCandidates(self, Min, Max);
        AddCandidatePairs(self, Slot, Min, Max);
    }
    if (self->_pairs._count < 2U)
    {
        return;
    }
    WorldCollisionPair Scratch[2];
    GenericBuffer_SortAscending(&self->_pairs, ComparePairs, NULL, Scratch);
    WorldCollisionPair* Pairs = (WorldCollisionPair*)(void*)self->_pairs._data;
    size_t Unique = 1U;
    for (size_t Index = 1U; Index < self->_pairs._count; Index++)
    {
        const WorldCollisionPair* Last = &Pairs[Unique - 1U];
        bool IsRepeat = (Pairs[Index].ObjectA == Last->ObjectA) && (Pairs[Index].ObjectB == Last->ObjectB)
            && (Pairs[Index].ShapeA == Last->ShapeA) && (Pairs[Index].ShapeB == Last->ShapeB);
        if (!IsRepeat)
        {
            Pairs[Unique++] = Pairs[Index];
        }
    }
    GenericBuffer_SetCount(&self->_pairs, Unique);
}


// Static functions: contacts at poses.
static void AddManifold(WorldCollision* self, WorldCollisionManifold* manifold)
{
    GenericBuffer_AddLast(&self->_manifolds, manifold);
}

static bool CollideTriangle(void* context, uint32_t triangle)
{
    MeshContactRun* Run = context;
    CollisionConvex View;
    CollisionConvex_FromMeshTriangle(Run->Mesh, triangle, Run->MeshToWorld, Run->IsTwoSided, &View);
    if (CollisionGeometry_Collide(Run->A, &View, Run->MaxSeparation, &Run->Template.Contact))
    {
        Run->Template.Triangle = triangle;
        AddManifold(Run->Self, &Run->Template);
    }
    return true;
}

/* A pair's contacts at its bodies' From poses, out to @p maxSeparation, appended to _manifolds (one per triangle of a
 * mesh B). */
static void CollidePair(WorldCollision* self, const WorldCollisionPair* pair, float maxSeparation)
{
    const WorldCollisionInstance* InstanceA = WorldCollisionInternal_GetInstance(self, pair->SlotA);
    const WorldCollisionInstance* InstanceB = WorldCollisionInternal_GetInstance(self, pair->SlotB);
    const WorldCollisionShape* ShapeA = WorldCollisionInternal_GetShape(InstanceA, pair->ShapeA);
    const WorldCollisionShape* ShapeB = WorldCollisionInternal_GetShape(InstanceB, pair->ShapeB);
    CollisionAffine AToWorld;
    CollisionAffine WorldToA;
    CollisionAffine BToWorld;
    CollisionAffine WorldToB;
    PlaceShape(InstanceA, ShapeA, &WorldCollisionInternal_GetBodyAt(self, pair->BodyA)->From, &AToWorld, &WorldToA);
    PlaceShape(InstanceB, ShapeB, &WorldCollisionInternal_GetBodyAt(self, pair->BodyB)->From, &BToWorld, &WorldToB);
    CollisionConvex A;
    if (!CollisionConvex_FromShape(ShapeA->Shape, &AToWorld, &A))
    {
        return;
    }
    WorldCollisionManifold Manifold;
    Memory_Zero(&Manifold, sizeof(Manifold));
    Manifold.SlotA = pair->SlotA;
    Manifold.ShapeA = pair->ShapeA;
    Manifold.SlotB = pair->SlotB;
    Manifold.ShapeB = pair->ShapeB;
    Manifold.BodyA = pair->BodyA;
    Manifold.BodyB = pair->BodyB;
    Manifold.Triangle = WORLD_COLLISION_NO_TRIANGLE;
    switch (ShapeB->Shape->Type)
    {
        case CollisionShapeType_Plane:
        {
            CollisionPlaneView Plane;
            CollisionContact Contact;
            if (!CollisionPlaneView_FromShape(ShapeB->Shape, &BToWorld, &Plane)
                || !CollisionGeometry_CollidePlane(&Plane, &A, maxSeparation, &Contact))
            {
                return;
            }
            // The plane's contact runs from the plane to A: turned around, A to B.
            Manifold.Contact = Contact;
            Manifold.Contact.Normal = CollisionMath_Negate(Contact.Normal);
            for (uint32_t Point = 0U; Point < Contact.PointCount; Point++)
            {
                Manifold.Contact.Points[Point].PositionA = Contact.Points[Point].PositionB;
                Manifold.Contact.Points[Point].PositionB = Contact.Points[Point].PositionA;
            }
            AddManifold(self, &Manifold);
            return;
        }
        case CollisionShapeType_Mesh:
        {
            Vector3 Min;
            Vector3 Max;
            Vector3 LocalMin;
            Vector3 LocalMax;
            if ((ShapeB->Shape->Mesh == NULL) || !GetPlacedBox(ShapeA, &AToWorld, &Min, &Max))
            {
                return;
            }
            ExpandBox(&Min, &Max, maxSeparation);
            CollisionAffine_TransformBounds(&WorldToB, Min, Max, &LocalMin, &LocalMax);
            MeshContactRun Run = { .Self = self, .A = &A, .Mesh = ShapeB->Shape->Mesh, .MeshToWorld = &BToWorld,
                .IsTwoSided = ShapeB->Shape->IsTwoSided, .MaxSeparation = maxSeparation, .Template = Manifold };
            CollisionGeometry_QueryMesh(ShapeB->Shape->Mesh, LocalMin, LocalMax, CollideTriangle, &Run);
            return;
        }
        case CollisionShapeType_Sphere:
        case CollisionShapeType_Capsule:
        case CollisionShapeType_Box:
        case CollisionShapeType_Cylinder:
        case CollisionShapeType_ConvexHull:
        {
            CollisionConvex B;
            if (CollisionConvex_FromShape(ShapeB->Shape, &BToWorld, &B)
                && CollisionGeometry_Collide(&A, &B, maxSeparation, &Manifold.Contact))
            {
                AddManifold(self, &Manifold);
            }
            return;
        }
        case CollisionShapeType_Count:
            break;
    }
}

/* The contacts of every pair at the bodies' From poses, out to @p margin plus (with @p addsClosing) how far the pair can
 * close in the solve; appended to _manifolds. Returns the deepest separation found (FLT_MAX for none). */
static float ComputeManifolds(WorldCollision* self, bool addsClosing, float margin)
{
    size_t First = self->_manifolds._count;
    for (size_t Index = 0U; Index < self->_pairs._count; Index++)
    {
        const WorldCollisionPair* Pair = &((const WorldCollisionPair*)(const void*)self->_pairs._data)[Index];
        float MaxSeparation = margin;
        if (addsClosing)
        {
            MaxSeparation += GetClosingBound(self, WorldCollisionInternal_GetBodyAt(self, Pair->BodyA),
                WorldCollisionInternal_GetBodyAt(self, Pair->BodyB));
        }
        CollidePair(self, Pair, MaxSeparation);
    }
    float Deepest = FLT_MAX;
    const WorldCollisionManifold* Manifolds = (const WorldCollisionManifold*)(const void*)self->_manifolds._data;
    for (size_t Index = First; Index < self->_manifolds._count; Index++)
    {
        Deepest = fminf(Deepest, Manifolds[Index].Contact.Separation);
    }
    return Deepest;
}


// Static functions: rows.
static void AddRow(WorldCollision* self, WorldCollisionRow* row)
{
    GenericBuffer_AddLast(&self->_rows, row);
}

/* A row's owners and their inverse masses from its bodies; false when no push could move it. */
static bool SetRowOwners(const WorldCollision* self, uint32_t bodyA, uint32_t bodyB, WorldCollisionRow* row)
{
    row->OwnerA = WorldCollisionInternal_GetBodyAt(self, bodyA)->Owner;
    row->OwnerB = WorldCollisionInternal_GetBodyAt(self, bodyB)->Owner;
    if (row->OwnerA == row->OwnerB)
    {
        return false; // none, or one body (never paired: the colliders one pushable carries do not block each other)
    }
    row->InverseMassA = (row->OwnerA != WORLD_COLLISION_NO_BODY)
        ? WorldCollisionInternal_GetBodyAt(self, row->OwnerA)->InverseMass : 0.0f;
    row->InverseMassB = (row->OwnerB != WORLD_COLLISION_NO_BODY)
        ? WorldCollisionInternal_GetBodyAt(self, row->OwnerB)->InverseMass : 0.0f;
    return (row->InverseMassA + row->InverseMassB) > 0.0f;
}

/* Turns the manifolds into rows: each point's separation after both bodies' motions over the solve, before pushes. */
static void AddManifoldRows(WorldCollision* self)
{
    for (size_t Index = 0U; Index < self->_manifolds._count; Index++)
    {
        const WorldCollisionManifold* Manifold =
            &((const WorldCollisionManifold*)(const void*)self->_manifolds._data)[Index];
        const WorldCollisionBody* BodyA = WorldCollisionInternal_GetBodyAt(self, Manifold->BodyA);
        const WorldCollisionBody* BodyB = WorldCollisionInternal_GetBodyAt(self, Manifold->BodyB);
        WorldCollisionRow Row;
        Memory_Zero(&Row, sizeof(Row));
        if (!SetRowOwners(self, Manifold->BodyA, Manifold->BodyB, &Row))
        {
            continue;
        }
        Row.SlotA = Manifold->SlotA;
        Row.ShapeA = Manifold->ShapeA;
        Row.SlotB = Manifold->SlotB;
        Row.ShapeB = Manifold->ShapeB;
        Row.Triangle = Manifold->Triangle;
        Row.Normal = Manifold->Contact.Normal;
        for (uint32_t Point = 0U; Point < Manifold->Contact.PointCount; Point++)
        {
            const CollisionContactPoint* Source = &Manifold->Contact.Points[Point];
            Row.Base = CollisionMath_Dot(CollisionMath_Subtract(MovePoint(BodyB, Source->PositionB),
                MovePoint(BodyA, Source->PositionA)), Row.Normal);
            Row.FeatureId = Source->FeatureId;
            AddRow(self, &Row);
        }
    }
}

/* Remembers where each manifold of the step's first solve started: its separation and how far its bodies' motions
 * would close it over the whole step (the solve's share times @p shares). */
static void RecordStarts(WorldCollision* self, float shares)
{
    for (size_t Index = 0U; Index < self->_manifolds._count; Index++)
    {
        const WorldCollisionManifold* Manifold =
            &((const WorldCollisionManifold*)(const void*)self->_manifolds._data)[Index];
        const WorldCollisionBody* BodyA = WorldCollisionInternal_GetBodyAt(self, Manifold->BodyA);
        const WorldCollisionBody* BodyB = WorldCollisionInternal_GetBodyAt(self, Manifold->BodyB);
        float Closing = 0.0f;
        for (uint32_t Point = 0U; Point < Manifold->Contact.PointCount; Point++)
        {
            const CollisionContactPoint* Source = &Manifold->Contact.Points[Point];
            Vector3 MotionA = CollisionMath_Subtract(MovePoint(BodyA, Source->PositionA), Source->PositionA);
            Vector3 MotionB = CollisionMath_Subtract(MovePoint(BodyB, Source->PositionB), Source->PositionB);
            Closing = fmaxf(Closing, -CollisionMath_Dot(CollisionMath_Subtract(MotionB, MotionA),
                Manifold->Contact.Normal));
        }
        bool IsSwapped = false;
        WorldCollisionStart Start =
        {
            .Key = WorldCollisionInternal_GetContactKey(self, Manifold->SlotA, Manifold->ShapeA, Manifold->SlotB,
                Manifold->ShapeB, Manifold->Triangle, &IsSwapped),
            .Separation = Manifold->Contact.Separation, .Closing = Closing * shares
        };
        GenericBuffer_AddLast(&self->_starts, &Start);
    }
}


// Static functions: the projected Gauss-Seidel.
/* Whether a row holds a pushed body against something that neither moves nor gives way (an ANCHOR: a static collider, a
 * kinematic one standing still). */
static bool IsAnchorRow(const WorldCollision* self, const WorldCollisionRow* row)
{
    if ((row->OwnerA != WORLD_COLLISION_NO_BODY) && (row->OwnerB != WORLD_COLLISION_NO_BODY))
    {
        return false;
    }
    uint32_t Slot = (row->OwnerA == WORLD_COLLISION_NO_BODY) ? row->SlotA : row->SlotB;
    uint32_t Body = WorldCollisionInternal_GetInstance(self, Slot)->Body;
    return (Body != WORLD_COLLISION_NO_BODY) && !WorldCollisionInternal_GetBodyAt(self, Body)->IsMoving;
}

/* Moves the anchor rows after the others, keeping each part's order; returns how many come first. */
static size_t PartitionRows(WorldCollision* self)
{
    GenericBuffer_Clear(&self->_rowScratch);
    WorldCollisionRow* Rows = (WorldCollisionRow*)(void*)self->_rows._data;
    size_t Free = 0U;
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        if (IsAnchorRow(self, &Rows[Index]))
        {
            GenericBuffer_AddLast(&self->_rowScratch, &Rows[Index]);
        }
        else
        {
            Rows[Free++] = Rows[Index];
        }
    }
    Memory_Copy(self->_rowScratch._data, &Rows[Free], self->_rowScratch._count * sizeof(WorldCollisionRow));
    return Free;
}

/* One row's projected step: its push moves by its violation shared by inverse mass, the total kept at 0 or above. */
static void SolveRow(WorldCollision* self, WorldCollisionRow* row)
{
    WorldCollisionBody* OwnerA = (row->OwnerA != WORLD_COLLISION_NO_BODY)
        ? WorldCollisionInternal_GetBodyAt(self, row->OwnerA) : NULL;
    WorldCollisionBody* OwnerB = (row->OwnerB != WORLD_COLLISION_NO_BODY)
        ? WorldCollisionInternal_GetBodyAt(self, row->OwnerB) : NULL;
    Vector3 PushA = (OwnerA != NULL) ? OwnerA->Correction : CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    Vector3 PushB = (OwnerB != NULL) ? OwnerB->Correction : CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    float Separation = row->Base + CollisionMath_Dot(CollisionMath_Subtract(PushB, PushA), row->Normal);
    float Delta = (SOLVER_RELAXATION * (WORLD_COLLISION_SKIN - Separation)) / (row->InverseMassA + row->InverseMassB);
    float Push = fmaxf(0.0f, row->Push + Delta);
    Delta = Push - row->Push;
    if (Delta == 0.0f)
    {
        return;
    }
    row->Push = Push;
    if (OwnerA != NULL)
    {
        OwnerA->Correction = CollisionMath_AddScaled(OwnerA->Correction, row->Normal, -row->InverseMassA * Delta);
    }
    if (OwnerB != NULL)
    {
        OwnerB->Correction = CollisionMath_AddScaled(OwnerB->Correction, row->Normal, row->InverseMassB * Delta);
    }
}

/* How far the pushed bodies moved in the iteration just run (the largest), and the start of the next. */
static float MeasureIteration(WorldCollision* self)
{
    float Largest = 0.0f;
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        if (Body->Owner != Index)
        {
            continue;
        }
        Largest = fmaxf(Largest, CollisionMath_MaxAbsComponent(CollisionMath_Subtract(Body->Correction,
            Body->IterationCorrection)));
        Body->IterationCorrection = Body->Correction;
    }
    return Largest;
}

/* Solves the rows; returns the iterations used. */
static uint32_t RunGaussSeidel(WorldCollision* self)
{
    size_t Count = self->_rows._count;
    if (Count == 0U)
    {
        return 0U;
    }
    size_t Free = PartitionRows(self);
    WorldCollisionRow* Rows = (WorldCollisionRow*)(void*)self->_rows._data;
    MeasureIteration(self);
    for (uint32_t Iteration = 0U; Iteration < WORLD_COLLISION_SOLVER_ITERATIONS; Iteration++)
    {
        // Forward and backward in turn: a push passes along a chain (a wall, a row of crates) in one sweep either way.
        // The anchor rows come last every time: they close, and what cannot fit stays among the others.
        bool IsForward = (Iteration % 2U) == 0U;
        for (size_t Step = 0U; Step < Free; Step++)
        {
            SolveRow(self, &Rows[IsForward ? Step : (Free - 1U - Step)]);
        }
        for (size_t Index = Free; Index < Count; Index++)
        {
            SolveRow(self, &Rows[Index]);
        }
        if (MeasureIteration(self) < SOLVER_CONVERGED_PUSH)
        {
            return Iteration + 1U;
        }
    }
    return WORLD_COLLISION_SOLVER_ITERATIONS;
}

/* Widens the tree boxes of every body its owner pushed where the push takes it past them, so later searches find it
 * there: its shapes' boxes where the step's motion put them, moved by the pushes so far (pushes only translate). */
static void WidenPushed(WorldCollision* self)
{
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        if (Body->Owner == WORLD_COLLISION_NO_BODY)
        {
            continue;
        }
        const WorldCollisionBody* Owner = WorldCollisionInternal_GetBodyAt(self, Body->Owner);
        Vector3 Shift = CollisionMath_Add(Owner->Accumulated, Owner->Correction);
        if ((Shift.x == 0.0f) && (Shift.y == 0.0f) && (Shift.z == 0.0f))
        {
            continue;
        }
        WorldCollisionInternal_WidenPushedProxies(self, WorldCollisionInternal_GetInstance(self, Body->Slot), Shift);
    }
}


// Static functions: blocked and crushed.
static uint64_t GetOwnerObjectId(const WorldCollision* self, const WorldCollisionBody* body)
{
    uint32_t Owner = (body->Owner != WORLD_COLLISION_NO_BODY) ? body->Owner : WORLD_COLLISION_NO_BODY;
    const WorldCollisionBody* Source = (Owner != WORLD_COLLISION_NO_BODY) ? WorldCollisionInternal_GetBodyAt(self, Owner)
        : body;
    return WorldCollisionInternal_GetInstance(self, Source->Slot)->ObjectId;
}

static void MarkCrushed(WorldCollision* self, uint32_t owner, float depth, uint64_t byId)
{
    WorldCollisionBody* Owner = WorldCollisionInternal_GetBodyAt(self, owner);
    if (!Owner->IsCrushed || (depth > Owner->CrushDepth))
    {
        Owner->CrushDepth = depth;
        Owner->CrushedById = byId;
    }
    Owner->IsCrushed = true;
}

/* A moving kinematic body pressing into a pushed one by @p depth is blocked by it. */
static void MarkBlocked(WorldCollision* self, WorldCollisionBody* mover, const WorldCollisionBody* pushed, float depth)
{
    if ((mover->Owner != WORLD_COLLISION_NO_BODY) || !mover->IsMoving || (pushed->Owner == WORLD_COLLISION_NO_BODY))
    {
        return;
    }
    if (!mover->IsBlocked || (depth > mover->BlockDepth))
    {
        mover->BlockDepth = depth;
        mover->BlockedById = GetOwnerObjectId(self, pushed);
    }
    mover->IsBlocked = true;
}

/* The overlaps left at the final poses: pushed bodies crushed, kinematic movers that press into them blocked. */
static void FindBlockedAndCrushed(WorldCollision* self)
{
    float Deepest = 0.0f;
    for (size_t Index = 0U; Index < self->_manifolds._count; Index++)
    {
        const WorldCollisionManifold* Manifold =
            &((const WorldCollisionManifold*)(const void*)self->_manifolds._data)[Index];
        float Depth = -Manifold->Contact.Separation;
        if (!(Depth > WORLD_COLLISION_SLOP))
        {
            continue;
        }
        Deepest = fmaxf(Deepest, Depth);
        WorldCollisionBody* BodyA = WorldCollisionInternal_GetBodyAt(self, Manifold->BodyA);
        WorldCollisionBody* BodyB = WorldCollisionInternal_GetBodyAt(self, Manifold->BodyB);
        if (BodyA->Owner != WORLD_COLLISION_NO_BODY)
        {
            MarkCrushed(self, BodyA->Owner, Depth, GetOwnerObjectId(self, BodyB));
        }
        if (BodyB->Owner != WORLD_COLLISION_NO_BODY)
        {
            MarkCrushed(self, BodyB->Owner, Depth, GetOwnerObjectId(self, BodyA));
        }
        MarkBlocked(self, BodyA, BodyB, Depth);
        MarkBlocked(self, BodyB, BodyA, Depth);
    }
    self->_stepStats.DeepestOverlap = Deepest;
}


// Static functions: the sweeps.
static void ConsiderTriangleHit(MeshCastRun* run, const CollisionCastHit* hit, uint32_t triangle)
{
    if (!run->HasHit || (hit->Fraction < run->Hit.Fraction))
    {
        run->Hit = *hit;
        run->Triangle = triangle;
        run->HasHit = true;
    }
}

static bool CastTriangle(void* context, uint32_t triangle)
{
    MeshCastRun* Run = context;
    CollisionConvex View;
    CollisionConvex_FromMeshTriangle(Run->Mesh, triangle, Run->MeshToWorld, Run->IsTwoSided, &View);
    CollisionCastHit Hit;
    if (CollisionGeometry_Cast(Run->A, Run->Relative, &View, &Hit))
    {
        ConsiderTriangleHit(Run, &Hit, triangle);
    }
    return true;
}

/* A linear sweep of the convex view @p a by @p relative against a placed shape of any kind (B's triangles near A's path
 * for a mesh: the earliest hit, and its triangle). */
static bool CastAgainstPlaced(const CollisionConvex* a, const WorldCollisionShape* aShape, Vector3 relative,
    const WorldCollisionShape* b, const CollisionAffine* bToWorld, const CollisionAffine* worldToB,
    CollisionCastHit* outHit, uint32_t* outTriangle)
{
    *outTriangle = WORLD_COLLISION_NO_TRIANGLE;
    switch (b->Shape->Type)
    {
        case CollisionShapeType_Plane:
        {
            CollisionPlaneView Plane;
            return CollisionPlaneView_FromShape(b->Shape, bToWorld, &Plane)
                && CollisionGeometry_CastPlane(a, relative, &Plane, outHit);
        }
        case CollisionShapeType_Mesh:
        {
            Vector3 Min;
            Vector3 Max;
            Vector3 LocalMin;
            Vector3 LocalMax;
            if ((b->Shape->Mesh == NULL) || !GetPlacedBox(aShape, &a->Transform, &Min, &Max))
            {
                return false;
            }
            Min = CollisionMath_Min(Min, CollisionMath_Add(Min, relative));
            Max = CollisionMath_Max(Max, CollisionMath_Add(Max, relative));
            ExpandBox(&Min, &Max, WORLD_COLLISION_SKIN);
            CollisionAffine_TransformBounds(worldToB, Min, Max, &LocalMin, &LocalMax);
            MeshCastRun Run = { .A = a, .Relative = relative, .Mesh = b->Shape->Mesh, .MeshToWorld = bToWorld,
                .IsTwoSided = b->Shape->IsTwoSided, .HasHit = false };
            CollisionGeometry_QueryMesh(b->Shape->Mesh, LocalMin, LocalMax, CastTriangle, &Run);
            *outHit = Run.Hit;
            *outTriangle = Run.Triangle;
            return Run.HasHit;
        }
        case CollisionShapeType_Sphere:
        case CollisionShapeType_Capsule:
        case CollisionShapeType_Box:
        case CollisionShapeType_Cylinder:
        case CollisionShapeType_ConvexHull:
        {
            CollisionConvex B;
            return CollisionConvex_FromShape(b->Shape, bToWorld, &B) && CollisionGeometry_Cast(a, relative, &B, outHit);
        }
        case CollisionShapeType_Count:
            break;
    }
    return false;
}

/* The first contact of shape @p shapeA of body @p bodyA, moving from its From pose to its final one, with shape
 * @p shapeB of body @p bodyB:
 * - neither turning: the exact cast of A's motion relative to B's (B where it starts);
 * - B standing still and A turning: A's straight path, its shape turned as at the end (a push through a wall is caught;
 *   the turn itself is the substeps' and their contacts');
 * - B moving and either turning: none (the substeps' contacts push apart what the turns reach).
 * A B whose motion is not continuous stands where it ends (its final pose): it jumps there, and A must not pass it.
 * With where the contact's material points are at the end of the motions without the solve's push (where its row
 * measures), and B's triangle. */
static bool CastShapes(WorldCollision* self, const WorldCollisionBody* bodyA, uint32_t shapeA,
    const WorldCollisionBody* bodyB, uint32_t shapeB, CollisionCastHit* outHit, uint32_t* outTriangle,
    Vector3* outEndA, Vector3* outEndB)
{
    const WorldCollisionInstance* InstanceA = WorldCollisionInternal_GetInstance(self, bodyA->Slot);
    const WorldCollisionInstance* InstanceB = WorldCollisionInternal_GetInstance(self, bodyB->Slot);
    const WorldCollisionShape* ShapeA = WorldCollisionInternal_GetShape(InstanceA, shapeA);
    const WorldCollisionShape* ShapeB = WorldCollisionInternal_GetShape(InstanceB, shapeB);
    CollisionPose FromA = bodyA->From;
    CollisionPose ToA = GetFinalPose(self, bodyA);
    CollisionPose ToB = GetFinalPose(self, bodyB);
    CollisionPose FromB = bodyB->IsContinuous ? bodyB->From : ToB;
    bool IsTurning = (CollisionPose_GetRotationAngle(&FromA, &ToA) > TURN_EPSILON)
        || (CollisionPose_GetRotationAngle(&FromB, &ToB) > TURN_EPSILON);
    if (IsTurning && !ArePosesEqual(&FromB, &ToB))
    {
        return false;
    }
    CollisionPose StartA = FromA;
    StartA.Rotation = ToA.Rotation;
    Vector3 Relative = CollisionMath_Subtract(CollisionMath_Subtract(ToA.Position, FromA.Position),
        CollisionMath_Subtract(ToB.Position, FromB.Position));
    CollisionAffine AToWorld;
    CollisionAffine WorldToA;
    CollisionAffine BToWorld;
    CollisionAffine WorldToB;
    CollisionConvex A;
    PlaceShape(InstanceA, ShapeA, &StartA, &AToWorld, &WorldToA);
    PlaceShape(InstanceB, ShapeB, &FromB, &BToWorld, &WorldToB);
    if (!CollisionConvex_FromShape(ShapeA->Shape, &AToWorld, &A)
        || !CastAgainstPlaced(&A, ShapeA, Relative, ShapeB, &BToWorld, &WorldToB, outHit, outTriangle))
    {
        return false;
    }
    // A's point at the impact lies at A's start moved by its share of the relative motion (B where it starts); both go
    // on with their own motions, without the push, to where the row measures them.
    CollisionPose ImpactA = StartA;
    ImpactA.Position = CollisionMath_AddScaled(StartA.Position, Relative, outHit->Fraction);
    *outEndA = CarryPoint(&ImpactA, &bodyA->To, outHit->PointA);
    *outEndB = bodyB->IsContinuous ? MovePoint(bodyB, outHit->PointB)
        : CollisionMath_Subtract(outHit->PointB, GetBodyCorrection(self, bodyB)); // B at its final pose, less its push
    return true;
}

/* Whether the solve already has a row from a sweep of these two shapes (and triangle). */
static bool HasCastRow(const WorldCollision* self, const WorldCollisionRow* row)
{
    const WorldCollisionRow* Rows = (const WorldCollisionRow*)(const void*)self->_rows._data;
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        const WorldCollisionRow* Other = &Rows[Index];
        if ((Other->FeatureId == CAST_FEATURE_ID) && (Other->SlotA == row->SlotA) && (Other->ShapeA == row->ShapeA)
            && (Other->SlotB == row->SlotB) && (Other->ShapeB == row->ShapeB) && (Other->Triangle == row->Triangle))
        {
            return true;
        }
    }
    return false;
}

/* A row from a sweep's hit: the contact's normal at the time of impact, and its two material points where the motions
 * without the push take them (@p endA, @p endB). Once per pair of shapes in a solve: a hit the row did not mend is
 * left to the stop at the time of impact. Returns whether a row was added. */
static bool AddCastRow(WorldCollision* self, uint32_t bodyA, uint32_t shapeA, uint32_t bodyB, uint32_t shapeB,
    uint32_t triangle, const CollisionCastHit* hit, Vector3 endA, Vector3 endB)
{
    WorldCollisionRow Row;
    Memory_Zero(&Row, sizeof(Row));
    if (!SetRowOwners(self, bodyA, bodyB, &Row))
    {
        return false;
    }
    Row.SlotA = WorldCollisionInternal_GetBodyAt(self, bodyA)->Slot;
    Row.ShapeA = shapeA;
    Row.SlotB = WorldCollisionInternal_GetBodyAt(self, bodyB)->Slot;
    Row.ShapeB = shapeB;
    Row.Triangle = triangle;
    Row.FeatureId = CAST_FEATURE_ID;
    Row.Normal = hit->Normal;
    Row.Base = CollisionMath_Dot(CollisionMath_Subtract(endB, endA), Row.Normal);
    if (HasCastRow(self, &Row))
    {
        return false;
    }
    AddRow(self, &Row);
    return true;
}

/* Whether a body sweeps its own motion over the solve (From to its final pose): a pushed, continuous one whose shapes
 * travel farther than the speculative distance. A shorter motion stays where the solve's contacts reach: they hold it,
 * and it is too short to pass through anything they missed. */
static bool IsSweptItself(const WorldCollision* self, const WorldCollisionBody* body)
{
    if ((body->Owner == WORLD_COLLISION_NO_BODY) || !body->IsContinuous)
    {
        return false;
    }
    CollisionPose Final = GetFinalPose(self, body);
    float Turn = CollisionPose_GetRotationAngle(&body->From, &Final);
    float Reach = CollisionMath_Length(CollisionMath_Subtract(Final.Position, body->From.Position))
        + (Turn * WorldCollisionInternal_GetInstance(self, body->Slot)->Reach);
    return Reach > WORLD_COLLISION_SPECULATIVE_DISTANCE;
}

/* Whether the sweep of body @p body tests body @p other: every other body that does not sweep itself, and a pair of
 * bodies that both do once (from the lower). */
static bool ShouldSweepAgainst(const WorldCollision* self, uint32_t body, uint32_t other)
{
    return (other > body) || !IsSweptItself(self, WorldCollisionInternal_GetBodyAt(self, other));
}

/* Sweeps one body's motion over the solve (From to its final pose; it sweeps itself) against what lies near it; each hit
 * is listed in _sweepHits and (with @p addsRows) becomes a row, once per pair of shapes. Returns how many rows it added
 * (with @p addsRows) or hits it found. */
static size_t SweepBody(WorldCollision* self, uint32_t bodyIndex, bool addsRows)
{
    const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, bodyIndex);
    CollisionPose From = Body->From;
    CollisionPose Final = GetFinalPose(self, Body);
    uint32_t Slot = Body->Slot;
    const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self, Slot);
    Vector3 Min;
    Vector3 Max;
    if (!GetMotionBox(self, Body, &From, &Final, &Min, &Max))
    {
        return 0U;
    }
    ExpandBox(&Min, &Max, WORLD_COLLISION_SKIN);
    WorldCollisionInternal_CollectCandidates(self, Min, Max);
    size_t Hits = 0U;
    for (size_t Candidate = 0U; Candidate < self->_candidates._count; Candidate++)
    {
        uint64_t Key = ((const uint64_t*)(const void*)self->_candidates._data)[Candidate];
        uint32_t OtherSlot = WorldCollisionInternal_GetKeySlot(Key);
        uint32_t OtherShape = WorldCollisionInternal_GetKeyShape(Key);
        const WorldCollisionInstance* Other = WorldCollisionInternal_GetInstance(self, OtherSlot);
        if ((OtherSlot == Slot) || !Other->IsUsed || !Other->IsActive || !WorldCollisionInternal_DoBlock(Instance, Other)
            || !WorldCollisionInternal_GetShape(Other, OtherShape)->IsValid
            || !WorldCollisionInternal_IsCandidateNear(Other, WorldCollisionInternal_GetShape(Other, OtherShape), Min,
                Max)
            || ((Other->OwnerSlot != WORLD_COLLISION_NO_SLOT)
                && !IsConvexShape(WorldCollisionInternal_GetShape(Other, OtherShape)->Shape)))
        {
            continue;
        }
        uint32_t OtherBody = WorldCollisionInternal_GetBody(self, OtherSlot);
        if (!ShouldSweepAgainst(self, bodyIndex, OtherBody))
        {
            continue;
        }
        for (uint32_t Shape = 0U; Shape < (uint32_t)Instance->Shapes._count; Shape++)
        {
            const WorldCollisionShape* Own = WorldCollisionInternal_GetShape(Instance, Shape);
            CollisionCastHit Hit;
            uint32_t Triangle = WORLD_COLLISION_NO_TRIANGLE;
            Vector3 EndA;
            Vector3 EndB;
            if (!Own->IsValid || !IsConvexShape(Own->Shape)
                || !CastShapes(self, WorldCollisionInternal_GetBodyAt(self, bodyIndex), Shape,
                    WorldCollisionInternal_GetBodyAt(self, OtherBody), OtherShape, &Hit, &Triangle, &EndA, &EndB)
                || Hit.IsStartPenetrating)
            {
                continue; // no hit, or an overlap at the start: that one is the contacts' to push out
            }
            WorldCollisionSweepHit Record = { .Body = bodyIndex, .OtherBody = OtherBody, .Fraction = Hit.Fraction };
            GenericBuffer_AddLast(&self->_sweepHits, &Record);
            Hits += (!addsRows || AddCastRow(self, bodyIndex, Shape, OtherBody, OtherShape, Triangle, &Hit, EndA, EndB))
                ? 1U : 0U;
        }
    }
    return Hits;
}

/* Sweeps every body that sweeps itself; returns how many rows it added (with @p addsRows) or hits it found. */
static size_t Sweep(WorldCollision* self, bool addsRows)
{
    GenericBuffer_Clear(&self->_sweepHits);
    size_t Hits = 0U;
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        if (IsSweptItself(self, WorldCollisionInternal_GetBodyAt(self, Index)))
        {
            Hits += SweepBody(self, Index, addsRows);
        }
    }
    return Hits;
}

/* An owner's push stops its motion just before @p fraction of the way. */
static void LimitOwner(WorldCollision* self, uint32_t owner, float fraction)
{
    WorldCollisionBody* Owner = WorldCollisionInternal_GetBodyAt(self, owner);
    Owner->ClampFraction = fminf(Owner->ClampFraction, fraction);
}

/* What the last sweep still hit: a kinematic mover in the way is blocked; any other hit stops the pushed motions (both,
 * for two pushed bodies) just before their time of impact. */
static void StopUnresolved(WorldCollision* self)
{
    const WorldCollisionSweepHit* Hits = (const WorldCollisionSweepHit*)(const void*)self->_sweepHits._data;
    for (size_t Index = 0U; Index < self->_sweepHits._count; Index++)
    {
        WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Hits[Index].Body);
        WorldCollisionBody* Other = WorldCollisionInternal_GetBodyAt(self, Hits[Index].OtherBody);
        if ((Other->Owner == WORLD_COLLISION_NO_BODY) && Other->IsMoving)
        {
            Other->IsBlocked = true;
            Other->BlockDepth = fmaxf(Other->BlockDepth, WORLD_COLLISION_SLOP);
            Other->BlockedById = WorldCollisionInternal_GetInstance(self,
                WorldCollisionInternal_GetBodyAt(self, Body->Owner)->Slot)->ObjectId;
            continue;
        }
        LimitOwner(self, Body->Owner, Hits[Index].Fraction);
        if (Other->Owner != WORLD_COLLISION_NO_BODY)
        {
            LimitOwner(self, Other->Owner, Hits[Index].Fraction);
        }
    }
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionBody* Owner = WorldCollisionInternal_GetBodyAt(self, Index);
        if (Owner->ClampFraction >= 1.0f)
        {
            continue;
        }
        // Back along its pushed motion to the skin before the time of impact.
        Vector3 Motion = CollisionMath_Subtract(CollisionMath_Add(Owner->To.Position, Owner->Correction),
            Owner->From.Position);
        float Length = CollisionMath_Length(Motion);
        float Fraction = (Length > WORLD_COLLISION_SKIN) ? fmaxf(0.0f, Owner->ClampFraction - (WORLD_COLLISION_SKIN
            / Length)) : 0.0f;
        Vector3 Stopped = CollisionMath_AddScaled(Owner->From.Position, Motion, Fraction);
        Owner->Correction = CollisionMath_Subtract(Stopped, Owner->To.Position);
    }
}

static uint32_t FindIsland(WorldCollision* self, uint32_t body)
{
    uint32_t Root = body;
    while (WorldCollisionInternal_GetBodyAt(self, Root)->Island != Root)
    {
        Root = WorldCollisionInternal_GetBodyAt(self, Root)->Island;
    }
    while (body != Root)
    {
        WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, body);
        uint32_t Next = Body->Island;
        Body->Island = Root;
        body = Next;
    }
    return Root;
}

/* The island of a row: its pushed owner's (either, once the islands are joined). */
static uint32_t GetRowIsland(WorldCollision* self, const WorldCollisionRow* row)
{
    return FindIsland(self, (row->OwnerA != WORLD_COLLISION_NO_BODY) ? row->OwnerA : row->OwnerB);
}

/* How far a row is still violated: the skin less its separation with the pushes so far (above 0: too close). */
static float GetRowViolation(const WorldCollision* self, const WorldCollisionRow* row)
{
    Vector3 PushA = (row->OwnerA != WORLD_COLLISION_NO_BODY)
        ? WorldCollisionInternal_GetBodyAt(self, row->OwnerA)->Correction : CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    Vector3 PushB = (row->OwnerB != WORLD_COLLISION_NO_BODY)
        ? WorldCollisionInternal_GetBodyAt(self, row->OwnerB)->Correction : CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    return WORLD_COLLISION_SKIN - (row->Base + CollisionMath_Dot(CollisionMath_Subtract(PushB, PushA), row->Normal));
}

/* What did not fit: the pushed bodies joined by rows form islands; an island holding a row still violated by more than
 * the slop could not make room, so every kinematic mover whose row presses into it (the row pushes, or is violated
 * itself) is blocked (by the island's deepest violation, by the pushed body its row touches). A mover touching the
 * island while it moves away, or along it, is not. */
static void FindBlockedIslands(WorldCollision* self)
{
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        Body->Island = Index;
        Body->IslandDepth = 0.0f;
    }
    const WorldCollisionRow* Rows = (const WorldCollisionRow*)(const void*)self->_rows._data;
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        if ((Rows[Index].OwnerA != WORLD_COLLISION_NO_BODY) && (Rows[Index].OwnerB != WORLD_COLLISION_NO_BODY))
        {
            uint32_t RootA = FindIsland(self, Rows[Index].OwnerA);
            uint32_t RootB = FindIsland(self, Rows[Index].OwnerB);
            WorldCollisionInternal_GetBodyAt(self, (RootA > RootB) ? RootA : RootB)->Island = (RootA > RootB) ? RootB
                : RootA;
        }
    }
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        WorldCollisionBody* Island = WorldCollisionInternal_GetBodyAt(self, GetRowIsland(self, &Rows[Index]));
        Island->IslandDepth = fmaxf(Island->IslandDepth, GetRowViolation(self, &Rows[Index]));
    }
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        const WorldCollisionRow* Row = &Rows[Index];
        if ((Row->OwnerA != WORLD_COLLISION_NO_BODY) && (Row->OwnerB != WORLD_COLLISION_NO_BODY))
        {
            continue;
        }
        float Depth = WorldCollisionInternal_GetBodyAt(self, GetRowIsland(self, Row))->IslandDepth;
        bool IsPressing = (Row->Push > 0.0f) || (GetRowViolation(self, Row) > WORLD_COLLISION_SLOP);
        if (!(Depth > WORLD_COLLISION_SLOP) || !IsPressing)
        {
            continue;
        }
        bool IsMoverA = Row->OwnerA == WORLD_COLLISION_NO_BODY;
        uint32_t Mover = WorldCollisionInternal_GetInstance(self, IsMoverA ? Row->SlotA : Row->SlotB)->Body;
        uint32_t Pushed = WorldCollisionInternal_GetInstance(self, IsMoverA ? Row->SlotB : Row->SlotA)->Body;
        if ((Mover != WORLD_COLLISION_NO_BODY) && (Pushed != WORLD_COLLISION_NO_BODY))
        {
            MarkBlocked(self, WorldCollisionInternal_GetBodyAt(self, Mover), WorldCollisionInternal_GetBodyAt(self,
                Pushed), Depth);
        }
    }
}

/* Whether a blocked kinematic mover will be set back (it stops when blocked and can be: the step solves again without
 * its motion, so the rest of this solve can be skipped). */
static bool HasStoppingMoverBlocked(const WorldCollision* self)
{
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        if (Body->IsBlocked && WorldCollisionInternal_CanStopMover(self,
            WorldCollisionInternal_GetInstance(self, Body->Slot)))
        {
            return true;
        }
    }
    return false;
}

/* Solves the rows, sweeps the pushed motions and adds rows for what they hit, a few rounds, and stops what still hits. */
static void SolveAndSweep(WorldCollision* self)
{
    self->_stepStats.SolverIterations += RunGaussSeidel(self);
    WidenPushed(self);
    size_t Added = 0U;
    for (uint32_t Round = 0U; Round < WORLD_COLLISION_SWEEP_ROUNDS; Round++)
    {
        Added = Sweep(self, true);
        if (Added == 0U)
        {
            break; // nothing new to solve: the hits listed (if any) already have their rows, and still hit
        }
        self->_stepStats.SolverIterations += RunGaussSeidel(self);
        WidenPushed(self);
    }
    if (Added > 0U)
    {
        Sweep(self, false); // the last round's rows were solved: what still hits now
    }
    if (self->_sweepHits._count > 0U)
    {
        StopUnresolved(self);
    }
    FindBlockedIslands(self);
}

/* Adds the solve's pushes to the owners' and remembers each row's push for the contacts, in metres (how far it moved
 * the two apart: the row's push times both inverse masses). */
static void Accumulate(WorldCollision* self)
{
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        if (Body->Owner == Index)
        {
            Body->Accumulated = CollisionMath_Add(Body->Accumulated, Body->Correction);
        }
    }
    const WorldCollisionRow* Rows = (const WorldCollisionRow*)(const void*)self->_rows._data;
    for (size_t Index = 0U; Index < self->_rows._count; Index++)
    {
        if (!(Rows[Index].Push > 0.0f))
        {
            continue;
        }
        bool IsSwapped = false;
        WorldCollisionPush Record =
        {
            .Key = WorldCollisionInternal_GetContactKey(self, Rows[Index].SlotA, Rows[Index].ShapeA, Rows[Index].SlotB,
                Rows[Index].ShapeB, Rows[Index].Triangle, &IsSwapped),
            .FeatureId = Rows[Index].FeatureId,
            .Push = Rows[Index].Push * (Rows[Index].InverseMassA + Rows[Index].InverseMassB)
        };
        GenericBuffer_AddLast(&self->_pushes, &Record);
    }
}


// Static functions: the step's solve.
/* How many substeps the step needs: enough that no body turns more than WORLD_COLLISION_MAX_SUBSTEP_TURN in one. */
static uint32_t GetSubstepCount(const WorldCollision* self)
{
    float Turn = 0.0f;
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        Turn = fmaxf(Turn, WorldCollisionInternal_GetBodyAt(self, Index)->Turn);
    }
    float Count = ceilf(Turn / WORLD_COLLISION_MAX_SUBSTEP_TURN);
    return (Count <= 1.0f) ? 1U : ((Count >= (float)WORLD_COLLISION_MAX_SUBSTEPS) ? WORLD_COLLISION_MAX_SUBSTEPS
        : (uint32_t)Count);
}

/* Whether the step's pairs still hold every pair at the final poses, out to the contact band: no body was made since
 * they were collected (@p pairBodyCount bodies then), and every pushed body's pushes stay within what they were
 * collected for. */
static bool ArePairsStillValid(const WorldCollision* self, size_t pairBodyCount)
{
    if (self->_bodies._count != pairBodyCount)
    {
        return false;
    }
    float Reach = (WORLD_COLLISION_SPECULATIVE_DISTANCE + WORLD_COLLISION_SKIN)
        - (WORLD_COLLISION_CONTACT_DISTANCE + WORLD_COLLISION_CONTACT_END_MARGIN);
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        const WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        if ((Body->Owner == Index) && (CollisionMath_Length(Body->Accumulated) > Reach))
        {
            return false;
        }
    }
    return true;
}

/* The pairs and contacts at the final poses, out to the contact band (the step's pairs while they stay valid, else
 * found again there; @p pairBodyCount: the bodies when the step's pairs were collected, 0 to collect anyway). Returns
 * the deepest separation. */
static float FindFinalContacts(WorldCollision* self, size_t pairBodyCount)
{
    SetPhasePoses(self, 1.0f, 1.0f);
    if ((pairBodyCount == 0U) || !ArePairsStillValid(self, pairBodyCount))
    {
        CollectPairs(self, true, WORLD_COLLISION_CONTACT_DISTANCE + WORLD_COLLISION_CONTACT_END_MARGIN);
    }
    GenericBuffer_Clear(&self->_manifolds);
    return ComputeManifolds(self, false, WORLD_COLLISION_CONTACT_DISTANCE + WORLD_COLLISION_CONTACT_END_MARGIN);
}


// Internal functions (WorldCollisionInternal.h).
void WorldCollisionInternal_Solve(WorldCollision* self, bool mayStopMovers)
{
    GenericBuffer_Clear(&self->_manifolds);
    GenericBuffer_Clear(&self->_pushes);
    GenericBuffer_Clear(&self->_starts);
    for (uint32_t Index = 0U; Index < (uint32_t)self->_bodies._count; Index++)
    {
        WorldCollisionBody* Body = WorldCollisionInternal_GetBodyAt(self, Index);
        Body->Accumulated = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
        Body->IsBlocked = false;
        Body->BlockDepth = 0.0f;
        Body->IsCrushed = false;
        Body->CrushDepth = 0.0f;
    }

    // The motions, in substeps, with the pairs found over the whole step.
    uint32_t Substeps = GetSubstepCount(self);
    self->_stepStats.SubstepCount = Substeps;
    CollectPairs(self, false, WORLD_COLLISION_SPECULATIVE_DISTANCE + WORLD_COLLISION_SKIN);
    size_t PairBodyCount = self->_bodies._count;
    self->_stepStats.PairCount = self->_pairs._count;
    for (uint32_t Substep = 0U; Substep < Substeps; Substep++)
    {
        SetPhasePoses(self, (float)Substep / (float)Substeps, (float)(Substep + 1U) / (float)Substeps);
        GenericBuffer_Clear(&self->_manifolds);
        GenericBuffer_Clear(&self->_rows);
        ComputeManifolds(self, true, WORLD_COLLISION_SPECULATIVE_DISTANCE + WORLD_COLLISION_SKIN);
        if (Substep == 0U)
        {
            RecordStarts(self, (float)Substeps);
        }
        AddManifoldRows(self);
        SolveAndSweep(self);
        Accumulate(self);
        if (mayStopMovers && HasStoppingMoverBlocked(self))
        {
            return; // set back and solved again: nothing of this solve stays
        }
    }

    // What still overlaps at the final poses is pushed out, a few rounds while they make progress (a pushable pinned
    // between things that do not give way stays as deep); the last contacts found are the step's.
    float Deepest = FindFinalContacts(self, PairBodyCount);
    for (uint32_t Round = 0U; (Round < WORLD_COLLISION_DEPENETRATION_ROUNDS) && (Deepest < -WORLD_COLLISION_SLOP); Round++)
    {
        GenericBuffer_Clear(&self->_rows);
        AddManifoldRows(self);
        SolveAndSweep(self);
        Accumulate(self);
        float Before = Deepest;
        Deepest = FindFinalContacts(self, 0U);
        if ((mayStopMovers && HasStoppingMoverBlocked(self)) || (Deepest <= (Before * DEPENETRATION_PROGRESS)))
        {
            break;
        }
    }
    FindBlockedAndCrushed(self);
}
