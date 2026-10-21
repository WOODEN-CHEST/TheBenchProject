#include <float.h>
#include <math.h>
#include "WorldCollisionInternal.h"
#include "World.h"
#include "raylib/raymath.h"

/*
 * The collision world's queries (include/WorldCollision.h): rays, casts, overlaps and contacts, over both trees and
 * the unbounded planes, through the narrowphase of source/CollisionGeometry.h.
 *
 * TARGETS come in three kinds: convex shapes (primitives and hulls, as convex views), planes (plane views, with their
 * one-sided rules) and meshes (their triangles, found through the mesh's BVH with the query's box taken into the mesh's
 * space, each as a triangle view with its active edges). A query's own shapes are convex (planes too for overlaps).
 *
 * CONVENTIONS. In every manifold A is the query's shape (or the object's own collider) and B the world's; the normal
 * points from A to B. A cast's result gives the hit surface's normal facing the moving shape (the narrowphase's normal
 * negated).
 */


// Types.
/* One of a query's shapes, placed in the world. */
typedef struct QueryShapeStruct
{
    const CollisionShape* Shape;
    uint32_t Index;
    bool IsPlane;
    CollisionConvex Convex;      /* placed at the query's pose (the start pose for casts) */
    CollisionConvex ConvexInPose; /* relative to the pose (rotating casts) */
    CollisionPlaneView Plane;
    Vector3 Min;
    Vector3 Max;
} QueryShape;

/* A query's pose and shapes. */
typedef struct QueryStruct
{
    QueryShape Shapes[WORLD_COLLISION_MAX_QUERY_SHAPES];
    uint32_t Count;
    CollisionAffine PoseToWorld;
    CollisionAffine WorldToPose;
    Vector3 Min;
    Vector3 Max;
} Query;

typedef struct RayQueryStruct
{
    WorldCollision* Self;
    const WorldCollisionFilter* Filter;
    Vector3 Origin;
    Vector3 Direction;
    float MaxDistance;
    bool IsAll;
    GenericBuffer* Hits;
    bool HasBest;
    WorldRayHit Best;
} RayQuery;

typedef struct TriangleContextStruct
{
    const CollisionMesh* Mesh;
    const CollisionAffine* MeshToWorld;
    bool IsTwoSided;
    /* What to do with each triangle view: return false to stop. */
    bool (*Visit)(void* context, const CollisionConvex* triangle, uint32_t triangleIndex);
    void* Context;
} TriangleContext;

typedef struct BoxCollectStruct
{
    GenericBuffer* Candidates;
} BoxCollect;


// Static functions: filters and infos.
static WorldCollisionFilter GetFilter(const WorldCollisionFilter* filter, WorldCollisionTriggerMode defaultMode)
{
    return (filter != NULL) ? *filter : WorldCollisionFilter_Create(COLLISION_LAYER_MASK_ALL, defaultMode);
}

void WorldCollisionInternal_FillTriangleInfo(const CollisionMesh* mesh, uint32_t triangle, WorldCollisionShapeInfo* info)
{
    const CollisionMeshTriangle* Source = &mesh->_triangles[triangle];
    info->HasTriangle = true;
    info->TriangleIndex = triangle;
    info->MeshPart = Source->_part;
    info->SourceTriangle = Source->_sourceIndex;
    info->MaterialIndex = (Source->_part < mesh->_partCount) ? mesh->_partMaterials[Source->_part] : -1;
}

/* The barycentric weights of @p point's nearest spot on a mesh triangle (world corners). */
static Vector3 GetTriangleWeights(const CollisionConvex* triangle, Vector3 point)
{
    Vector3 Weights;
    uint32_t Region = 0U;
    CollisionMath_GetClosestTrianglePoint(triangle->Vertices[0], triangle->Vertices[1], triangle->Vertices[2], point,
        &Weights, &Region);
    return Weights;
}

static void FillQueryShapeInfo(const QueryShape* shape, WorldCollisionShapeInfo* info)
{
    Memory_Zero(info, sizeof(*info));
    info->ShapeIndex = shape->Index;
    info->ShapeName = shape->Shape->Name;
    info->ShapeType = shape->Shape->Type;
    info->Layer = CollisionLayer_Prop;
    info->MaterialIndex = -1;
}


// Static functions: building queries.
static bool BuildQuery(const CollisionShape* shapes, uint32_t shapeCount, Vector3 position, Quaternion rotation,
    bool allowsPlanes, Query* outQuery)
{
    if ((shapes == NULL) || (shapeCount == 0U) || (shapeCount > WORLD_COLLISION_MAX_QUERY_SHAPES))
    {
        return false;
    }
    CollisionPose Pose = { .Position = position, .Rotation = QuaternionNormalize(rotation) };
    outQuery->PoseToWorld = CollisionPose_ToAffine(&Pose);
    CollisionAffine_TryInvert(&outQuery->PoseToWorld, &outQuery->WorldToPose);
    outQuery->Count = 0U;
    outQuery->Min = CollisionMath_Vector(FLT_MAX, FLT_MAX, FLT_MAX);
    outQuery->Max = CollisionMath_Vector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (uint32_t Index = 0U; Index < shapeCount; Index++)
    {
        const CollisionShape* Shape = &shapes[Index];
        QueryShape* Target = &outQuery->Shapes[outQuery->Count];
        Target->Shape = Shape;
        Target->Index = Index;
        Target->IsPlane = Shape->Type == CollisionShapeType_Plane;
        CollisionAffine Local = CollisionAffine_FromCenterRotation(Shape->Center, Shape->Rotation);
        CollisionAffine Placed = CollisionAffine_Multiply(&outQuery->PoseToWorld, &Local);
        if (Target->IsPlane)
        {
            // Overlaps only: a plane as a query shape (an unbounded one has no box: it reaches everything).
            if (!allowsPlanes || !Shape->IsBounded || !CollisionPlaneView_FromShape(Shape, &Placed, &Target->Plane))
            {
                return false;
            }
        }
        else
        {
            if (!CollisionConvex_FromShape(Shape, &Placed, &Target->Convex)
                || !CollisionConvex_FromShape(Shape, &Local, &Target->ConvexInPose))
            {
                return false; // a mesh, or a flattened shape
            }
        }
        Vector3 LocalMin;
        Vector3 LocalMax;
        CollisionShape_TryGetLocalBounds(Shape, &LocalMin, &LocalMax);
        CollisionAffine_TransformBounds(&Placed, LocalMin, LocalMax, &Target->Min, &Target->Max);
        outQuery->Min = CollisionMath_Min(outQuery->Min, Target->Min);
        outQuery->Max = CollisionMath_Max(outQuery->Max, Target->Max);
        outQuery->Count++;
    }
    return true;
}


// Static functions: candidates and meshes.
static bool CollectVisitor(void* context, int32_t proxy, uint64_t userValue)
{
    (void)proxy;
    BoxCollect* Collect = context;
    GenericBuffer_AddLast(Collect->Candidates, &userValue);
    return true;
}

static bool VisitTriangle(void* context, uint32_t triangle)
{
    TriangleContext* Triangles = context;
    CollisionConvex View;
    CollisionConvex_FromMeshTriangle(Triangles->Mesh, triangle, Triangles->MeshToWorld, Triangles->IsTwoSided, &View);
    return Triangles->Visit(Triangles->Context, &View, triangle);
}

/* Visits the triangles of a mesh shape whose boxes meet the world box [min, max]. */
static void ForEachTriangle(const WorldCollisionShape* target, Vector3 min, Vector3 max,
    bool (*visit)(void* context, const CollisionConvex* triangle, uint32_t triangleIndex), void* context)
{
    Vector3 LocalMin;
    Vector3 LocalMax;
    CollisionAffine_TransformBounds(&target->WorldToShape, min, max, &LocalMin, &LocalMax);
    TriangleContext Triangles = { .Mesh = target->Shape->Mesh, .MeshToWorld = &target->ShapeToWorld,
        .IsTwoSided = target->Shape->IsTwoSided, .Visit = visit, .Context = context };
    CollisionGeometry_QueryMesh(target->Shape->Mesh, LocalMin, LocalMax, VisitTriangle, &Triangles);
}


// Static functions: rays.
static void RecordRayHit(RayQuery* query, const WorldCollisionInstance* instance, const WorldCollisionShape* shape,
    const CollisionRayHit* hit)
{
    WorldRayHit Result;
    Memory_Zero(&Result, sizeof(Result));
    WorldCollisionInternal_FillShapeInfo(instance, shape, &Result.Shape);
    if (shape->Shape->Type == CollisionShapeType_Mesh)
    {
        WorldCollisionInternal_FillTriangleInfo(shape->Shape->Mesh, hit->TriangleIndex, &Result.Shape);
        Result.Shape.Barycentric = hit->Barycentric;
    }
    Result.Point = CollisionMath_AddScaled(query->Origin, query->Direction, hit->T);
    Result.Normal = hit->Normal;
    Result.Distance = hit->T;
    Result.Fraction = hit->T / query->MaxDistance;
    Result.IsStartInside = hit->IsStartInside;
    if (query->IsAll)
    {
        GenericBuffer_AddLast(query->Hits, &Result);
        return;
    }
    if (!query->HasBest || (Result.Distance < query->Best.Distance))
    {
        query->Best = Result;
        query->HasBest = true;
    }
}

/* Tests one shape; returns the hit distance (or @p maxDistance when it missed). */
static float TestRayShape(RayQuery* query, uint64_t key, float maxDistance)
{
    const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(query->Self,
        WorldCollisionInternal_GetKeySlot(key));
    if (!WorldCollisionInternal_PassesFilter(query->Self, Instance, query->Filter))
    {
        return maxDistance;
    }
    const WorldCollisionShape* Shape = WorldCollisionInternal_GetShape(Instance, WorldCollisionInternal_GetKeyShape(key));
    CollisionRayHit Hit;
    if (!Shape->IsValid || !CollisionGeometry_RaycastShape(Shape->Shape, &Shape->ShapeToWorld, &Shape->WorldToShape,
        query->Origin, query->Direction, query->IsAll ? query->MaxDistance : maxDistance, &Hit))
    {
        return maxDistance;
    }
    RecordRayHit(query, Instance, Shape, &Hit);
    return query->IsAll ? maxDistance : Hit.T;
}

static float RayVisitor(void* context, int32_t proxy, uint64_t userValue, float maxDistance)
{
    (void)proxy;
    float Distance = TestRayShape(context, userValue, maxDistance);
    // Never 0 (that would stop the walk): a hit at the origin keeps a tiny length, and nothing is nearer anyway.
    return fmaxf(Distance, 1e-30f);
}

static size_t RunRay(WorldCollision* self, Vector3 origin, Vector3 direction, float maxDistance,
    const WorldCollisionFilter* filter, bool isAll, GenericBuffer* hits, RayQuery* outQuery)
{
    float Length = CollisionMath_Length(direction);
    if (!(Length > 0.0f) || !(maxDistance > 0.0f) || !CollisionMath_IsFinite(origin))
    {
        return 0U;
    }
    WorldCollisionFilter Filter = GetFilter(filter, WorldCollisionTriggerMode_Ignore);
    *outQuery = (RayQuery){ .Self = self, .Filter = &Filter, .Origin = origin,
        .Direction = CollisionMath_Scale(direction, 1.0f / Length), .MaxDistance = maxDistance, .IsAll = isAll,
        .Hits = hits, .HasBest = false };
    size_t Before = (hits != NULL) ? hits->_count : 0U;
    CollisionTree_QueryRay(&self->_staticTree, origin, outQuery->Direction, maxDistance, RayVisitor, outQuery);
    float Remaining = outQuery->HasBest ? outQuery->Best.Distance : maxDistance;
    CollisionTree_QueryRay(&self->_movableTree, origin, outQuery->Direction, fmaxf(Remaining, 1e-30f), RayVisitor,
        outQuery);
    for (size_t Index = 0U; Index < self->_planes._count; Index++)
    {
        uint64_t Key = ((const uint64_t*)(const void*)self->_planes._data)[Index];
        TestRayShape(outQuery, Key, outQuery->HasBest ? outQuery->Best.Distance : maxDistance);
    }
    outQuery->Filter = NULL;
    return isAll ? (hits->_count - Before) : (outQuery->HasBest ? 1U : 0U);
}

static ComparisonResult CompareRayHits(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    const WorldRayHit* A = a._element;
    const WorldRayHit* B = b._element;
    if (A->Distance != B->Distance)
    {
        return (A->Distance < B->Distance) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    if (A->Shape.ObjectId != B->Shape.ObjectId)
    {
        return (A->Shape.ObjectId < B->Shape.ObjectId) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
    }
    return (A->Shape.ShapeIndex < B->Shape.ShapeIndex) ? ComparisonResult_ALessThanB
        : ((A->Shape.ShapeIndex > B->Shape.ShapeIndex) ? ComparisonResult_AGreaterThanB : ComparisonResult_AEqualsB);
}


// Static functions: contacts.
typedef struct ContactRunStruct
{
    WorldCollision* Self;
    const WorldCollisionInstance* Target;
    const WorldCollisionShape* TargetShape;
    const CollisionConvex* Convex;   /* A: the query's (or the object's) convex shape */
    WorldCollisionShapeInfo InfoA;
    const CollisionAffine* WorldToA;
    float MaxSeparation;
    bool IsSwapped;                  /* the narrowphase ran with the world's shape as A: flip the result */
    GenericBuffer* Out;              /* manifolds, or NULL to stop at the first contact */
    bool HasContact;
} ContactRun;

/* Writes a narrowphase contact as a manifold (A the query's, B the world's). */
static void EmitContact(ContactRun* run, const CollisionContact* contact, uint32_t triangle,
    const CollisionConvex* triangleView)
{
    run->HasContact = true;
    if (run->Out == NULL)
    {
        return;
    }
    WorldContactManifold Manifold;
    Memory_Zero(&Manifold, sizeof(Manifold));
    Manifold.ShapeA = run->InfoA;
    WorldCollisionInternal_FillShapeInfo(run->Target, run->TargetShape, &Manifold.ShapeB);
    Manifold.Normal = run->IsSwapped ? CollisionMath_Negate(contact->Normal) : contact->Normal;
    Manifold.Separation = contact->Separation;
    Manifold.PointCount = contact->PointCount;
    for (uint32_t Point = 0U; Point < contact->PointCount; Point++)
    {
        const CollisionContactPoint* Source = &contact->Points[Point];
        WorldContactPoint* Target = &Manifold.Points[Point];
        Target->PositionA = run->IsSwapped ? Source->PositionB : Source->PositionA;
        Target->PositionB = run->IsSwapped ? Source->PositionA : Source->PositionB;
        Target->Separation = Source->Separation;
        Target->FeatureId = Source->FeatureId;
        Target->LocalPositionA = CollisionAffine_TransformPoint(run->WorldToA, Target->PositionA);
        Target->LocalPositionB = CollisionAffine_TransformPoint(&run->Target->WorldToObject, Target->PositionB);
    }
    if (triangleView != NULL)
    {
        WorldCollisionInternal_FillTriangleInfo(run->TargetShape->Shape->Mesh, triangle, &Manifold.ShapeB);
        Manifold.ShapeB.Barycentric = GetTriangleWeights(triangleView, Manifold.Points[0].PositionB);
    }
    GenericBuffer_AddLast(run->Out, &Manifold);
}

static bool ContactTriangle(void* context, const CollisionConvex* triangle, uint32_t triangleIndex)
{
    ContactRun* Run = context;
    CollisionContact Contact;
    bool IsTouching = CollisionGeometry_Collide(Run->Convex, triangle, Run->MaxSeparation, &Contact);
    if (IsTouching)
    {
        EmitContact(Run, &Contact, triangleIndex, triangle);
    }
    return (Run->Out != NULL) || !Run->HasContact;
}

/* The contacts of the run's convex shape with one world shape (any kind). */
static void ContactWithShape(ContactRun* run, Vector3 min, Vector3 max)
{
    const WorldCollisionShape* Target = run->TargetShape;
    CollisionContact Contact;
    switch (Target->Shape->Type)
    {
        case CollisionShapeType_Plane:
        {
            CollisionPlaneView Plane;
            run->IsSwapped = true;
            if (CollisionPlaneView_FromShape(Target->Shape, &Target->ShapeToWorld, &Plane)
                && CollisionGeometry_CollidePlane(&Plane, run->Convex, run->MaxSeparation, &Contact))
            {
                EmitContact(run, &Contact, 0U, NULL);
            }
            run->IsSwapped = false;
            return;
        }
        case CollisionShapeType_Mesh:
            if (Target->Shape->Mesh != NULL)
            {
                Vector3 Margin = { run->MaxSeparation, run->MaxSeparation, run->MaxSeparation };
                ForEachTriangle(Target, CollisionMath_Subtract(min, Margin), CollisionMath_Add(max, Margin),
                    ContactTriangle, run);
            }
            return;
        case CollisionShapeType_Sphere:
        case CollisionShapeType_Capsule:
        case CollisionShapeType_Box:
        case CollisionShapeType_Cylinder:
        case CollisionShapeType_ConvexHull:
        {
            CollisionConvex Convex;
            if (CollisionConvex_FromShape(Target->Shape, &Target->ShapeToWorld, &Convex)
                && CollisionGeometry_Collide(run->Convex, &Convex, run->MaxSeparation, &Contact))
            {
                EmitContact(run, &Contact, 0U, NULL);
            }
            return;
        }
        case CollisionShapeType_Count:
            break;
    }
}

/* A plane query shape (overlaps) against one world shape: only convex targets (planes and meshes do not pair). */
static bool DoesPlaneTouch(const CollisionPlaneView* plane, const WorldCollisionShape* target, float maxSeparation)
{
    CollisionConvex Convex;
    CollisionContact Contact;
    return (target->Shape->Type != CollisionShapeType_Plane) && (target->Shape->Type != CollisionShapeType_Mesh)
        && CollisionConvex_FromShape(target->Shape, &target->ShapeToWorld, &Convex)
        && CollisionGeometry_CollidePlane(plane, &Convex, maxSeparation, &Contact);
}

/* Collects the candidate keys for [min, max] widened by @p margin into _candidates. */
static void Collect(WorldCollision* self, Vector3 min, Vector3 max, float margin)
{
    Vector3 Margin = { margin, margin, margin };
    WorldCollisionInternal_CollectCandidates(self, CollisionMath_Subtract(min, Margin), CollisionMath_Add(max, Margin));
}


// Static functions: casts.
typedef struct CastRunStruct
{
    WorldCollision* Self;
    const QueryShape* Shape;
    const CollisionPose* From;
    const CollisionPose* To;
    Vector3 Translation;
    bool IsTurning;
    const WorldCollisionInstance* Target;
    const WorldCollisionShape* TargetShape;
    bool HasBest;
    WorldCastHit Best;
} CastRun;

/* Keeps a narrowphase hit when it is earlier than the best (at fraction 0, the deeper penetration). */
static void ConsiderCastHit(CastRun* run, const CollisionCastHit* hit, uint32_t triangle,
    const CollisionConvex* triangleView)
{
    bool IsEarlier = !run->HasBest || (hit->Fraction < run->Best.Fraction)
        || ((hit->Fraction == run->Best.Fraction) && (hit->PenetrationDepth > run->Best.PenetrationDepth));
    if (!IsEarlier)
    {
        return;
    }
    WorldCastHit Result;
    Memory_Zero(&Result, sizeof(Result));
    WorldCollisionInternal_FillShapeInfo(run->Target, run->TargetShape, &Result.Shape);
    if (triangleView != NULL)
    {
        WorldCollisionInternal_FillTriangleInfo(run->TargetShape->Shape->Mesh, triangle, &Result.Shape);
        Result.Shape.Barycentric = GetTriangleWeights(triangleView, hit->PointB);
    }
    Result.CastShapeIndex = run->Shape->Index;
    Result.Fraction = hit->Fraction;
    Result.Distance = hit->Fraction * CollisionMath_Length(run->Translation);
    Result.Point = hit->PointB;
    Result.PointOnCast = hit->PointA;
    Result.Normal = CollisionMath_Negate(hit->Normal);
    Result.IsStartPenetrating = hit->IsStartPenetrating;
    Result.PenetrationDepth = hit->PenetrationDepth;
    run->Best = Result;
    run->HasBest = true;
}

static bool CastAgainstConvex(CastRun* run, const CollisionConvex* target, CollisionCastHit* outHit)
{
    return run->IsTurning ? CollisionGeometry_CastMotion(&run->Shape->ConvexInPose, run->From, run->To, target, outHit)
        : CollisionGeometry_Cast(&run->Shape->Convex, run->Translation, target, outHit);
}

static bool CastTriangle(void* context, const CollisionConvex* triangle, uint32_t triangleIndex)
{
    CastRun* Run = context;
    CollisionCastHit Hit;
    if (CastAgainstConvex(Run, triangle, &Hit))
    {
        ConsiderCastHit(Run, &Hit, triangleIndex, triangle);
    }
    return true;
}

static void CastAgainstShape(CastRun* run, Vector3 sweptMin, Vector3 sweptMax)
{
    const WorldCollisionShape* Target = run->TargetShape;
    CollisionCastHit Hit;
    switch (Target->Shape->Type)
    {
        case CollisionShapeType_Plane:
        {
            CollisionPlaneView Plane;
            if (!CollisionPlaneView_FromShape(Target->Shape, &Target->ShapeToWorld, &Plane))
            {
                return;
            }
            bool IsHit = run->IsTurning
                ? CollisionGeometry_CastMotionPlane(&run->Shape->ConvexInPose, run->From, run->To, &Plane, &Hit)
                : CollisionGeometry_CastPlane(&run->Shape->Convex, run->Translation, &Plane, &Hit);
            if (IsHit)
            {
                ConsiderCastHit(run, &Hit, 0U, NULL);
            }
            return;
        }
        case CollisionShapeType_Mesh:
            if (Target->Shape->Mesh != NULL)
            {
                ForEachTriangle(Target, sweptMin, sweptMax, CastTriangle, run);
            }
            return;
        case CollisionShapeType_Sphere:
        case CollisionShapeType_Capsule:
        case CollisionShapeType_Box:
        case CollisionShapeType_Cylinder:
        case CollisionShapeType_ConvexHull:
        {
            CollisionConvex Convex;
            if (CollisionConvex_FromShape(Target->Shape, &Target->ShapeToWorld, &Convex) && CastAgainstConvex(run,
                &Convex, &Hit))
            {
                ConsiderCastHit(run, &Hit, 0U, NULL);
            }
            return;
        }
        case CollisionShapeType_Count:
            break;
    }
}


// Internal functions (WorldCollisionInternal.h).
bool WorldCollisionInternal_PassesFilter(WorldCollision* self, const WorldCollisionInstance* instance,
    const WorldCollisionFilter* filter)
{
    if (!instance->IsUsed || !instance->IsActive)
    {
        return false;
    }
    if ((filter->LayerMask & COLLISION_LAYER_BIT(instance->Settings.Layer)) == 0U)
    {
        return false;
    }
    switch (filter->TriggerMode)
    {
        case WorldCollisionTriggerMode_Ignore:
            if (instance->Settings.IsTrigger)
            {
                return false;
            }
            break;
        case WorldCollisionTriggerMode_Only:
            if (!instance->Settings.IsTrigger)
            {
                return false;
            }
            break;
        case WorldCollisionTriggerMode_Include:
            break;
    }
    if (filter->IgnoredObjectId == 0U)
    {
        return true;
    }
    if (instance->ObjectId == filter->IgnoredObjectId)
    {
        return false;
    }
    if (filter->IsIgnoredSubtree)
    {
        // The live hierarchy, found by id: an object removed since the sync is in no subtree (its shapes stay until the
        // next sync), and the collider and object pointers the instance kept may be freed by then.
        WorldObject* Object = NULL;
        if (!World_TryGetObject(self->_world, instance->ObjectId, &Object))
        {
            return true;
        }
        for (const WorldObject* Ancestor = WorldObject_GetParent(Object); Ancestor != NULL;
            Ancestor = WorldObject_GetParent(Ancestor))
        {
            if (WorldObject_GetId(Ancestor) == filter->IgnoredObjectId)
            {
                return false;
            }
        }
    }
    return true;
}

void WorldCollisionInternal_FillShapeInfo(const WorldCollisionInstance* instance, const WorldCollisionShape* shape,
    WorldCollisionShapeInfo* outInfo)
{
    Memory_Zero(outInfo, sizeof(*outInfo));
    outInfo->ObjectId = instance->ObjectId;
    outInfo->ShapeIndex = shape->ShapeIndex;
    outInfo->ShapeName = shape->Shape->Name;
    outInfo->ShapeType = shape->Shape->Type;
    outInfo->Layer = instance->Settings.Layer;
    outInfo->IsTrigger = instance->Settings.IsTrigger;
    outInfo->MaterialIndex = -1;
}

void WorldCollisionInternal_CollectCandidates(WorldCollision* self, Vector3 min, Vector3 max)
{
    GenericBuffer_Clear(&self->_candidates);
    BoxCollect Collect = { .Candidates = &self->_candidates };
    CollisionTree_QueryBox(&self->_staticTree, min, max, CollectVisitor, &Collect);
    CollisionTree_QueryBox(&self->_movableTree, min, max, CollectVisitor, &Collect);
    GenericBuffer_AddLastRange(&self->_candidates, self->_planes._data, self->_planes._count);
}

bool WorldCollisionInternal_DoInstancesTouch(WorldCollision* self, const WorldCollisionInstance* a,
    const WorldCollisionInstance* b, float margin)
{
    for (uint32_t IndexA = 0U; IndexA < (uint32_t)a->Shapes._count; IndexA++)
    {
        const WorldCollisionShape* ShapeA = WorldCollisionInternal_GetShape(a, IndexA);
        if (!ShapeA->IsValid)
        {
            continue;
        }
        for (uint32_t IndexB = 0U; IndexB < (uint32_t)b->Shapes._count; IndexB++)
        {
            const WorldCollisionShape* ShapeB = WorldCollisionInternal_GetShape(b, IndexB);
            if (!ShapeB->IsValid)
            {
                continue;
            }
            // One side must be convex: run it as A against the other (any kind).
            bool IsConvexA = (ShapeA->Shape->Type != CollisionShapeType_Plane)
                && (ShapeA->Shape->Type != CollisionShapeType_Mesh);
            bool IsConvexB = (ShapeB->Shape->Type != CollisionShapeType_Plane)
                && (ShapeB->Shape->Type != CollisionShapeType_Mesh);
            if (!IsConvexA && !IsConvexB)
            {
                continue;
            }
            const WorldCollisionInstance* Probe = IsConvexA ? a : b;
            const WorldCollisionShape* ProbeShape = IsConvexA ? ShapeA : ShapeB;
            CollisionConvex Convex;
            if (!CollisionConvex_FromShape(ProbeShape->Shape, &ProbeShape->ShapeToWorld, &Convex))
            {
                continue;
            }
            ContactRun Run;
            Memory_Zero(&Run, sizeof(Run));
            Run.Self = self;
            Run.Target = IsConvexA ? b : a;
            Run.TargetShape = IsConvexA ? ShapeB : ShapeA;
            Run.Convex = &Convex;
            Run.WorldToA = &Probe->WorldToObject;
            Run.MaxSeparation = margin;
            Run.Out = NULL;
            ContactWithShape(&Run, ProbeShape->Min, ProbeShape->Max);
            if (Run.HasContact)
            {
                return true;
            }
        }
    }
    return false;
}


// Public functions: rays.
bool WorldCollision_Raycast(WorldCollision* self, Vector3 origin, Vector3 direction, float maxDistance,
    const WorldCollisionFilter* filter, WorldRayHit* outHit)
{
    RayQuery Probe;
    if (RunRay(self, origin, direction, maxDistance, filter, false, NULL, &Probe) == 0U)
    {
        return false;
    }
    *outHit = Probe.Best;
    return true;
}

size_t WorldCollision_RaycastAll(WorldCollision* self, Vector3 origin, Vector3 direction, float maxDistance,
    const WorldCollisionFilter* filter, GenericBuffer* outHits)
{
    size_t Before = outHits->_count;
    RayQuery Probe;
    size_t Count = RunRay(self, origin, direction, maxDistance, filter, true, outHits, &Probe);
    if (Count > 1U)
    {
        // Sort the appended range only: a temporary view over it.
        GenericBuffer View;
        GenericBuffer_CreateConstant(&View, (unsigned char*)outHits->_data + (Before * sizeof(WorldRayHit)), Count,
            sizeof(WorldRayHit), Count);
        WorldRayHit Scratch[2];
        GenericBuffer_SortAscending(&View, CompareRayHits, NULL, Scratch);
    }
    return Count;
}


// Public functions: casts.
bool WorldCollision_CastShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3 startPosition, Quaternion startRotation, Vector3 endPosition, Quaternion endRotation,
    const WorldCollisionFilter* filter, WorldCastHit* outHit)
{
    Query Start;
    if (!BuildQuery(shapes, shapeCount, startPosition, startRotation, false, &Start))
    {
        return false;
    }
    CollisionPose From = { .Position = startPosition, .Rotation = QuaternionNormalize(startRotation) };
    CollisionPose To = { .Position = endPosition, .Rotation = QuaternionNormalize(endRotation) };
    float Turn = CollisionPose_GetRotationAngle(&From, &To);
    bool IsTurning = Turn > 1e-6f;
    Vector3 Translation = CollisionMath_Subtract(endPosition, startPosition);

    // The swept box: the shapes at both ends; turning, everything within reach of the moving pose's origin.
    Vector3 SweptMin = CollisionMath_Min(Start.Min, CollisionMath_Add(Start.Min, Translation));
    Vector3 SweptMax = CollisionMath_Max(Start.Max, CollisionMath_Add(Start.Max, Translation));
    if (IsTurning)
    {
        float Reach = 0.0f;
        for (uint32_t Index = 0U; Index < Start.Count; Index++)
        {
            const CollisionConvex* InPose = &Start.Shapes[Index].ConvexInPose;
            Reach = fmaxf(Reach, CollisionMath_Length(InPose->Transform.Translation) + InPose->BoundingRadius);
        }
        Vector3 Radius = { Reach, Reach, Reach };
        SweptMin = CollisionMath_Min(SweptMin, CollisionMath_Subtract(CollisionMath_Min(startPosition, endPosition),
            Radius));
        SweptMax = CollisionMath_Max(SweptMax, CollisionMath_Add(CollisionMath_Max(startPosition, endPosition), Radius));
    }
    WorldCollisionFilter Filter = GetFilter(filter, WorldCollisionTriggerMode_Ignore);
    Collect(self, SweptMin, SweptMax, 0.0f);

    CastRun Run;
    Memory_Zero(&Run, sizeof(Run));
    Run.Self = self;
    Run.From = &From;
    Run.To = &To;
    Run.Translation = Translation;
    Run.IsTurning = IsTurning;
    for (size_t Candidate = 0U; Candidate < self->_candidates._count; Candidate++)
    {
        uint64_t Key = ((const uint64_t*)(const void*)self->_candidates._data)[Candidate];
        const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self,
            WorldCollisionInternal_GetKeySlot(Key));
        if (!WorldCollisionInternal_PassesFilter(self, Instance, &Filter))
        {
            continue;
        }
        const WorldCollisionShape* Target = WorldCollisionInternal_GetShape(Instance,
            WorldCollisionInternal_GetKeyShape(Key));
        if (!Target->IsValid)
        {
            continue;
        }
        Run.Target = Instance;
        Run.TargetShape = Target;
        for (uint32_t Index = 0U; Index < Start.Count; Index++)
        {
            Run.Shape = &Start.Shapes[Index];
            CastAgainstShape(&Run, SweptMin, SweptMax);
        }
    }
    if (!Run.HasBest)
    {
        return false;
    }
    *outHit = Run.Best;
    return true;
}

bool WorldCollision_SweepSphere(WorldCollision* self, Vector3 start, Vector3 end, float radius,
    const WorldCollisionFilter* filter, WorldCastHit* outHit)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Sphere);
    Shape.Radius = radius;
    return WorldCollision_CastShape(self, &Shape, 1U, start, QuaternionIdentity(), end, QuaternionIdentity(), filter,
        outHit);
}

bool WorldCollision_SweepCapsule(WorldCollision* self, Vector3 start, Vector3 end, float radius, float height,
    const WorldCollisionFilter* filter, WorldCastHit* outHit)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Capsule);
    Shape.Radius = radius;
    Shape.HalfHeight = fmaxf(0.0f, (height * 0.5f) - radius);
    return WorldCollision_CastShape(self, &Shape, 1U, start, QuaternionIdentity(), end, QuaternionIdentity(), filter,
        outHit);
}

bool WorldCollision_SweepBox(WorldCollision* self, Vector3 start, Vector3 end, Vector3 halfExtents, Quaternion rotation,
    const WorldCollisionFilter* filter, WorldCastHit* outHit)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = halfExtents;
    return WorldCollision_CastShape(self, &Shape, 1U, start, rotation, end, rotation, filter, outHit);
}


// Public functions: overlaps.
static ComparisonResult CompareIds(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    uint64_t A = *(const uint64_t*)a._element;
    uint64_t B = *(const uint64_t*)b._element;
    return (A < B) ? ComparisonResult_ALessThanB : ((A > B) ? ComparisonResult_AGreaterThanB : ComparisonResult_AEqualsB);
}

size_t WorldCollision_OverlapShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3 position, Quaternion rotation, const WorldCollisionFilter* filter, GenericBuffer* outObjectIds)
{
    Query Shapes;
    if (!BuildQuery(shapes, shapeCount, position, rotation, true, &Shapes))
    {
        return 0U;
    }
    WorldCollisionFilter Filter = GetFilter(filter, WorldCollisionTriggerMode_Include);
    Collect(self, Shapes.Min, Shapes.Max, 0.0f);
    size_t Before = outObjectIds->_count;
    for (size_t Candidate = 0U; Candidate < self->_candidates._count; Candidate++)
    {
        uint64_t Key = ((const uint64_t*)(const void*)self->_candidates._data)[Candidate];
        const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self,
            WorldCollisionInternal_GetKeySlot(Key));
        if (!WorldCollisionInternal_PassesFilter(self, Instance, &Filter))
        {
            continue;
        }
        const WorldCollisionShape* Target = WorldCollisionInternal_GetShape(Instance,
            WorldCollisionInternal_GetKeyShape(Key));
        bool IsTouching = false;
        for (uint32_t Index = 0U; Target->IsValid && (Index < Shapes.Count) && !IsTouching; Index++)
        {
            const QueryShape* Shape = &Shapes.Shapes[Index];
            if (Shape->IsPlane)
            {
                IsTouching = DoesPlaneTouch(&Shape->Plane, Target, 0.0f);
                continue;
            }
            ContactRun Run;
            Memory_Zero(&Run, sizeof(Run));
            Run.Self = self;
            Run.Target = Instance;
            Run.TargetShape = Target;
            Run.Convex = &Shape->Convex;
            Run.WorldToA = &Shapes.WorldToPose;
            Run.MaxSeparation = 0.0f;
            ContactWithShape(&Run, Shape->Min, Shape->Max);
            IsTouching = Run.HasContact;
        }
        if (IsTouching)
        {
            uint64_t Id = Instance->ObjectId;
            GenericBuffer_AddLast(outObjectIds, &Id);
        }
    }
    // Ascending and each once: sort the appended ids, then drop repeats (an object touched by several shapes).
    size_t Count = outObjectIds->_count - Before;
    if (Count == 0U)
    {
        return 0U;
    }
    uint64_t* Ids = (uint64_t*)(void*)outObjectIds->_data + Before;
    GenericBuffer View;
    GenericBuffer_CreateConstant(&View, (unsigned char*)(void*)Ids, Count, sizeof(uint64_t), Count);
    uint64_t Scratch[2];
    GenericBuffer_SortAscending(&View, CompareIds, NULL, Scratch);
    size_t Unique = 1U;
    for (size_t Index = 1U; Index < Count; Index++)
    {
        if (Ids[Index] != Ids[Unique - 1U])
        {
            Ids[Unique++] = Ids[Index];
        }
    }
    GenericBuffer_SetCount(outObjectIds, Before + Unique);
    return Unique;
}

size_t WorldCollision_OverlapSphere(WorldCollision* self, Vector3 center, float radius,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Sphere);
    Shape.Radius = radius;
    return WorldCollision_OverlapShape(self, &Shape, 1U, center, QuaternionIdentity(), filter, outObjectIds);
}

size_t WorldCollision_OverlapCapsule(WorldCollision* self, Vector3 center, float radius, float height,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Capsule);
    Shape.Radius = radius;
    Shape.HalfHeight = fmaxf(0.0f, (height * 0.5f) - radius);
    return WorldCollision_OverlapShape(self, &Shape, 1U, center, QuaternionIdentity(), filter, outObjectIds);
}

size_t WorldCollision_OverlapBox(WorldCollision* self, Vector3 center, Vector3 halfExtents, Quaternion rotation,
    const WorldCollisionFilter* filter, GenericBuffer* outObjectIds)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = halfExtents;
    return WorldCollision_OverlapShape(self, &Shape, 1U, center, rotation, filter, outObjectIds);
}


// Public functions: contacts.
size_t WorldCollision_CollideShape(WorldCollision* self, const CollisionShape* shapes, uint32_t shapeCount,
    Vector3 position, Quaternion rotation, float maxSeparation, const WorldCollisionFilter* filter,
    GenericBuffer* outManifolds)
{
    Query Shapes;
    if (!BuildQuery(shapes, shapeCount, position, rotation, false, &Shapes) || !(maxSeparation >= 0.0f))
    {
        return 0U;
    }
    WorldCollisionFilter Filter = GetFilter(filter, WorldCollisionTriggerMode_Ignore);
    Collect(self, Shapes.Min, Shapes.Max, maxSeparation);
    size_t Before = outManifolds->_count;
    for (size_t Candidate = 0U; Candidate < self->_candidates._count; Candidate++)
    {
        uint64_t Key = ((const uint64_t*)(const void*)self->_candidates._data)[Candidate];
        const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self,
            WorldCollisionInternal_GetKeySlot(Key));
        const WorldCollisionShape* Target = WorldCollisionInternal_GetShape(Instance,
            WorldCollisionInternal_GetKeyShape(Key));
        if (!WorldCollisionInternal_PassesFilter(self, Instance, &Filter) || !Target->IsValid)
        {
            continue;
        }
        for (uint32_t Index = 0U; Index < Shapes.Count; Index++)
        {
            const QueryShape* Shape = &Shapes.Shapes[Index];
            ContactRun Run;
            Memory_Zero(&Run, sizeof(Run));
            Run.Self = self;
            Run.Target = Instance;
            Run.TargetShape = Target;
            Run.Convex = &Shape->Convex;
            FillQueryShapeInfo(Shape, &Run.InfoA);
            Run.WorldToA = &Shapes.WorldToPose;
            Run.MaxSeparation = maxSeparation;
            Run.Out = outManifolds;
            ContactWithShape(&Run, Shape->Min, Shape->Max);
        }
    }
    return outManifolds->_count - Before;
}

size_t WorldCollision_CollideObject(WorldCollision* self, uint64_t objectId, float maxSeparation,
    const WorldCollisionFilter* filter, GenericBuffer* outManifolds)
{
    uint32_t Slot = WORLD_COLLISION_NO_SLOT;
    if (!(maxSeparation >= 0.0f) || !WorldCollisionInternal_FindSlot(self, objectId, &Slot))
    {
        return 0U;
    }
    const WorldCollisionInstance* Source = WorldCollisionInternal_GetInstance(self, Slot);
    if (!Source->IsUsed || !Source->IsActive)
    {
        return 0U;
    }
    WorldCollisionFilter Filter = GetFilter(filter, WorldCollisionTriggerMode_Ignore);
    Filter.IgnoredObjectId = objectId;
    size_t Before = outManifolds->_count;
    for (uint32_t Index = 0U; Index < (uint32_t)Source->Shapes._count; Index++)
    {
        const WorldCollisionShape* Own = WorldCollisionInternal_GetShape(Source, Index);
        bool IsConvex = (Own->Shape->Type != CollisionShapeType_Plane) && (Own->Shape->Type != CollisionShapeType_Mesh);
        CollisionConvex Convex;
        if (!Own->IsValid || !IsConvex || !CollisionConvex_FromShape(Own->Shape, &Own->ShapeToWorld, &Convex))
        {
            // TODO: an object whose own shape is a plane or a mesh against the convex shapes around it (T27 needs it
            // only for movers, which are convex).
            continue;
        }
        Collect(self, Own->Min, Own->Max, maxSeparation);
        for (size_t Candidate = 0U; Candidate < self->_candidates._count; Candidate++)
        {
            uint64_t Key = ((const uint64_t*)(const void*)self->_candidates._data)[Candidate];
            const WorldCollisionInstance* Instance = WorldCollisionInternal_GetInstance(self,
                WorldCollisionInternal_GetKeySlot(Key));
            const WorldCollisionShape* Target = WorldCollisionInternal_GetShape(Instance,
                WorldCollisionInternal_GetKeyShape(Key));
            if (!WorldCollisionInternal_PassesFilter(self, Instance, &Filter) || !Target->IsValid
                || !CollisionLayer_DoCollide(Source->Settings.Layer, Source->Settings.CollidesWith,
                    Instance->Settings.Layer, Instance->Settings.CollidesWith))
            {
                continue;
            }
            ContactRun Run;
            Memory_Zero(&Run, sizeof(Run));
            Run.Self = self;
            Run.Target = Instance;
            Run.TargetShape = Target;
            Run.Convex = &Convex;
            WorldCollisionInternal_FillShapeInfo(Source, Own, &Run.InfoA);
            Run.WorldToA = &Source->WorldToObject;
            Run.MaxSeparation = maxSeparation;
            Run.Out = outManifolds;
            ContactWithShape(&Run, Own->Min, Own->Max);
        }
    }
    return outManifolds->_count - Before;
}
