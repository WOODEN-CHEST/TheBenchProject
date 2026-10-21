#include <float.h>
#include <math.h>
#include "CollisionGeometry.h"
#include "wr/WRMemory.h"

/*
 * Rays against shapes and meshes (source/CollisionGeometry.h). Each test runs in the shape's own space: the ray goes
 * through the inverse transform (its parameter T is the same in both spaces, so the direction need not be unit), the
 * closed form of the local shape gives T and a local normal, and the normal comes back through the inverse's transpose.
 * A ray that starts inside a solid shape hits at T 0. Planes are hit from the front only (and inside their slab);
 * triangles from both sides or the front only. Meshes walk their BVH nearest-first.
 */


// Macros.
/** A ray hits a triangle within this share of its edges beyond them (barycentric weights down to -this): two triangles
 *  sharing an edge then leave no crack along it for rays to slip through. */
#define TRIANGLE_EDGE_EPSILON 1e-5f
/** Ray directions below this length (in the shape's space) count as zero. */
#define RAY_EPSILON 1e-12f
/** The BVH walk's stack: a node's sibling per level (meshes are at most about 20 levels deep). */
#define MESH_STACK_CAPACITY 64U


// Types.
typedef struct LocalHitStruct
{
    float T;
    Vector3 Normal;
    bool IsStartInside;
} LocalHit;


// Static functions: local shapes.
static bool HitSphere(Vector3 o, Vector3 d, Vector3 center, float radius, float maxT, LocalHit* hit)
{
    Vector3 Offset = CollisionMath_Subtract(o, center);
    float A = CollisionMath_Dot(d, d);
    float B = CollisionMath_Dot(Offset, d);
    float C = CollisionMath_Dot(Offset, Offset) - (radius * radius);
    if (C <= 0.0f)
    {
        *hit = (LocalHit){ .T = 0.0f, .IsStartInside = true };
        return true;
    }
    float Discriminant = (B * B) - (A * C);
    if ((Discriminant < 0.0f) || (A <= RAY_EPSILON))
    {
        return false;
    }
    float T = (-B - sqrtf(Discriminant)) / A;
    if ((T < 0.0f) || (T > maxT))
    {
        return false;
    }
    Vector3 Point = CollisionMath_AddScaled(Offset, d, T);
    *hit = (LocalHit){ .T = T, .Normal = CollisionMath_NormalizeOr(Point, CollisionMath_Vector(0.0f, 1.0f, 0.0f)),
        .IsStartInside = false };
    return true;
}

/* The side of an infinite cylinder of @p radius along Y, between heights -halfHeight and halfHeight. */
static bool HitCylinderSide(Vector3 o, Vector3 d, float radius, float halfHeight, float maxT, LocalHit* hit)
{
    float A = (d.x * d.x) + (d.z * d.z);
    if (A <= RAY_EPSILON)
    {
        return false;
    }
    float B = (o.x * d.x) + (o.z * d.z);
    float C = (o.x * o.x) + (o.z * o.z) - (radius * radius);
    float Discriminant = (B * B) - (A * C);
    if (Discriminant < 0.0f)
    {
        return false;
    }
    float T = (-B - sqrtf(Discriminant)) / A;
    if ((T < 0.0f) || (T > maxT))
    {
        return false;
    }
    float Y = o.y + (d.y * T);
    if (fabsf(Y) > halfHeight)
    {
        return false;
    }
    *hit = (LocalHit){ .T = T, .Normal = CollisionMath_NormalizeOr(CollisionMath_Vector(o.x + (d.x * T), 0.0f,
        o.z + (d.z * T)), CollisionMath_Vector(1.0f, 0.0f, 0.0f)), .IsStartInside = false };
    return true;
}

static void KeepNearer(bool isHit, const LocalHit* candidate, bool* hasBest, LocalHit* best)
{
    if (isHit && (!*hasBest || (candidate->T < best->T)))
    {
        *best = *candidate;
        *hasBest = true;
    }
}

static bool HitCapsule(Vector3 o, Vector3 d, float radius, float halfHeight, float maxT, LocalHit* hit)
{
    float Y = fmaxf(-halfHeight, fminf(halfHeight, o.y));
    if (CollisionMath_LengthSquared(CollisionMath_Subtract(o, CollisionMath_Vector(0.0f, Y, 0.0f))) <= (radius * radius))
    {
        *hit = (LocalHit){ .T = 0.0f, .IsStartInside = true };
        return true;
    }
    bool HasBest = false;
    LocalHit Candidate;
    KeepNearer(HitCylinderSide(o, d, radius, halfHeight, maxT, &Candidate), &Candidate, &HasBest, hit);
    KeepNearer(HitSphere(o, d, CollisionMath_Vector(0.0f, halfHeight, 0.0f), radius, maxT, &Candidate), &Candidate,
        &HasBest, hit);
    KeepNearer(HitSphere(o, d, CollisionMath_Vector(0.0f, -halfHeight, 0.0f), radius, maxT, &Candidate), &Candidate,
        &HasBest, hit);
    return HasBest;
}

/* A box of @p half around @p center (slab method); @p isBottomRefused refuses an entry through the -Y face (a plane's
 * slab seen from behind). */
static bool HitBox(Vector3 o, Vector3 d, Vector3 center, Vector3 half, bool isBottomRefused, float maxT, LocalHit* hit)
{
    Vector3 Offset = CollisionMath_Subtract(o, center);
    float Origin[3] = { Offset.x, Offset.y, Offset.z };
    float Direction[3] = { d.x, d.y, d.z };
    float Half[3] = { half.x, half.y, half.z };
    if ((fabsf(Origin[0]) <= Half[0]) && (fabsf(Origin[1]) <= Half[1]) && (fabsf(Origin[2]) <= Half[2]))
    {
        *hit = (LocalHit){ .T = 0.0f, .IsStartInside = true };
        return true;
    }
    float Enter = 0.0f;
    float Exit = maxT;
    int32_t EnterAxis = -1;
    float EnterSign = 1.0f;
    for (int32_t Axis = 0; Axis < 3; Axis++)
    {
        if (fabsf(Direction[Axis]) <= RAY_EPSILON)
        {
            if (fabsf(Origin[Axis]) > Half[Axis])
            {
                return false;
            }
            continue;
        }
        float Inverse = 1.0f / Direction[Axis];
        float Near = (-Half[Axis] - Origin[Axis]) * Inverse;
        float Far = (Half[Axis] - Origin[Axis]) * Inverse;
        float Sign = -1.0f; // the face at Near: the -h face when the ray runs toward +
        if (Near > Far)
        {
            float Swap = Near;
            Near = Far;
            Far = Swap;
            Sign = 1.0f;
        }
        if (Near > Enter)
        {
            Enter = Near;
            EnterAxis = Axis;
            EnterSign = Sign;
        }
        Exit = fminf(Exit, Far);
        if (Enter > Exit)
        {
            return false;
        }
    }
    if (EnterAxis < 0)
    {
        return false;
    }
    if (isBottomRefused && (EnterAxis == 1) && (EnterSign < 0.0f))
    {
        return false;
    }
    Vector3 Normal = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    if (EnterAxis == 0)
    {
        Normal.x = EnterSign;
    }
    else if (EnterAxis == 1)
    {
        Normal.y = EnterSign;
    }
    else
    {
        Normal.z = EnterSign;
    }
    *hit = (LocalHit){ .T = Enter, .Normal = Normal, .IsStartInside = false };
    return true;
}

static bool HitCylinder(Vector3 o, Vector3 d, float radius, float halfHeight, float maxT, LocalHit* hit)
{
    if ((((o.x * o.x) + (o.z * o.z)) <= (radius * radius)) && (fabsf(o.y) <= halfHeight))
    {
        *hit = (LocalHit){ .T = 0.0f, .IsStartInside = true };
        return true;
    }
    bool HasBest = false;
    LocalHit Candidate;
    KeepNearer(HitCylinderSide(o, d, radius, halfHeight, maxT, &Candidate), &Candidate, &HasBest, hit);
    if (fabsf(d.y) > RAY_EPSILON)
    {
        for (int32_t Cap = -1; Cap <= 1; Cap += 2)
        {
            float Height = (float)Cap * halfHeight;
            if (((o.y - Height) * (float)Cap) < 0.0f)
            {
                continue; // the cap faces away from the ray's origin
            }
            float T = (Height - o.y) / d.y;
            float X = o.x + (d.x * T);
            float Z = o.z + (d.z * T);
            if ((T >= 0.0f) && (T <= maxT) && (((X * X) + (Z * Z)) <= (radius * radius)))
            {
                Candidate = (LocalHit){ .T = T, .Normal = CollisionMath_Vector(0.0f, (float)Cap, 0.0f),
                    .IsStartInside = false };
                KeepNearer(true, &Candidate, &HasBest, hit);
            }
        }
    }
    return HasBest;
}

static bool HitHull(Vector3 o, Vector3 d, const CollisionHull* hull, float maxT, LocalHit* hit)
{
    float Enter = 0.0f;
    float Exit = maxT;
    int32_t EnterFace = -1;
    bool IsInside = true;
    for (uint32_t Face = 0U; Face < hull->_faceCount; Face++)
    {
        const CollisionHullFace* Plane = &hull->_faces[Face];
        float Denominator = CollisionMath_Dot(Plane->_normal, d);
        float Distance = CollisionMath_Dot(Plane->_normal, o) - Plane->_offset;
        IsInside = IsInside && (Distance <= 0.0f);
        if (fabsf(Denominator) <= RAY_EPSILON)
        {
            if (Distance > 0.0f)
            {
                return false;
            }
            continue;
        }
        float T = -Distance / Denominator;
        if (Denominator < 0.0f)
        {
            if (T > Enter)
            {
                Enter = T;
                EnterFace = (int32_t)Face;
            }
        }
        else
        {
            Exit = fminf(Exit, T);
        }
        if (Enter > Exit)
        {
            return false;
        }
    }
    if (IsInside)
    {
        *hit = (LocalHit){ .T = 0.0f, .IsStartInside = true };
        return true;
    }
    if (EnterFace < 0)
    {
        return false;
    }
    *hit = (LocalHit){ .T = Enter, .Normal = hull->_faces[EnterFace]._normal, .IsStartInside = false };
    return true;
}

static bool HitPlane(Vector3 o, Vector3 d, const CollisionShape* shape, float maxT, LocalHit* hit)
{
    if (!shape->IsBounded)
    {
        if (o.y <= 0.0f)
        {
            *hit = (LocalHit){ .T = 0.0f, .IsStartInside = true };
            return true;
        }
        if (d.y >= 0.0f)
        {
            return false;
        }
        float T = -o.y / d.y;
        if (T > maxT)
        {
            return false;
        }
        *hit = (LocalHit){ .T = T, .Normal = CollisionMath_Vector(0.0f, 1.0f, 0.0f), .IsStartInside = false };
        return true;
    }
    float Hx = shape->PlaneHalfExtents.x;
    float Hz = shape->PlaneHalfExtents.y;
    if (shape->Depth > 0.0f)
    {
        float HalfDepth = shape->Depth * 0.5f;
        return HitBox(o, d, CollisionMath_Vector(0.0f, -HalfDepth, 0.0f), CollisionMath_Vector(Hx, HalfDepth, Hz), true,
            maxT, hit);
    }
    if ((o.y < 0.0f) || (d.y >= 0.0f))
    {
        return false; // a flat rectangle is hit from the front only
    }
    float T = -o.y / d.y;
    float X = o.x + (d.x * T);
    float Z = o.z + (d.z * T);
    if ((T > maxT) || (fabsf(X) > Hx) || (fabsf(Z) > Hz))
    {
        return false;
    }
    *hit = (LocalHit){ .T = T, .Normal = CollisionMath_Vector(0.0f, 1.0f, 0.0f), .IsStartInside = false };
    return true;
}


// Static functions: meshes.
static bool HitBounds(Vector3 o, Vector3 inverseDirection, Vector3 min, Vector3 max, float maxT)
{
    float T1 = (min.x - o.x) * inverseDirection.x;
    float T2 = (max.x - o.x) * inverseDirection.x;
    float Near = fminf(T1, T2);
    float Far = fmaxf(T1, T2);
    T1 = (min.y - o.y) * inverseDirection.y;
    T2 = (max.y - o.y) * inverseDirection.y;
    Near = fmaxf(Near, fminf(T1, T2));
    Far = fminf(Far, fmaxf(T1, T2));
    T1 = (min.z - o.z) * inverseDirection.z;
    T2 = (max.z - o.z) * inverseDirection.z;
    Near = fmaxf(Near, fminf(T1, T2));
    Far = fminf(Far, fmaxf(T1, T2));
    return (Far >= fmaxf(Near, 0.0f)) && (Near <= maxT);
}

static float SafeInverse(float value)
{
    return (fabsf(value) > RAY_EPSILON) ? (1.0f / value) : copysignf(FLT_MAX, value);
}

static bool OverlapsBounds(Vector3 minA, Vector3 maxA, Vector3 minB, Vector3 maxB)
{
    return (minA.x <= maxB.x) && (maxA.x >= minB.x) && (minA.y <= maxB.y) && (maxA.y >= minB.y)
        && (minA.z <= maxB.z) && (maxA.z >= minB.z);
}


// Public functions.
bool CollisionGeometry_RaycastTriangle(Vector3 origin, Vector3 direction, Vector3 a, Vector3 b, Vector3 c,
    bool isTwoSided, float maxT, CollisionRayHit* outHit)
{
    Vector3 Edge1 = CollisionMath_Subtract(b, a);
    Vector3 Edge2 = CollisionMath_Subtract(c, a);
    Vector3 P = CollisionMath_Cross(direction, Edge2);
    float Determinant = CollisionMath_Dot(Edge1, P);
    float Scale = CollisionMath_Length(Edge1) * CollisionMath_Length(Edge2) * CollisionMath_Length(direction);
    float Epsilon = 1e-9f * Scale;
    if (isTwoSided ? (fabsf(Determinant) <= Epsilon) : (Determinant <= Epsilon))
    {
        return false;
    }
    float InverseDeterminant = 1.0f / Determinant;
    Vector3 S = CollisionMath_Subtract(origin, a);
    float U = CollisionMath_Dot(S, P) * InverseDeterminant;
    if ((U < -TRIANGLE_EDGE_EPSILON) || (U > (1.0f + TRIANGLE_EDGE_EPSILON)))
    {
        return false;
    }
    Vector3 Q = CollisionMath_Cross(S, Edge1);
    float V = CollisionMath_Dot(direction, Q) * InverseDeterminant;
    if ((V < -TRIANGLE_EDGE_EPSILON) || ((U + V) > (1.0f + TRIANGLE_EDGE_EPSILON)))
    {
        return false;
    }
    float T = CollisionMath_Dot(Edge2, Q) * InverseDeterminant;
    if ((T < 0.0f) || (T > maxT))
    {
        return false;
    }
    Vector3 Normal = CollisionMath_NormalizeOr(CollisionMath_Cross(Edge1, Edge2), CollisionMath_Vector(0.0f, 1.0f, 0.0f));
    if (CollisionMath_Dot(Normal, direction) > 0.0f)
    {
        Normal = CollisionMath_Negate(Normal);
    }
    // The weights clamped into the triangle (a hit within the edge tolerance lies on the edge).
    U = fmaxf(U, 0.0f);
    V = fmaxf(V, 0.0f);
    float Sum = U + V;
    if (Sum > 1.0f)
    {
        U /= Sum;
        V /= Sum;
    }
    *outHit = (CollisionRayHit){ .T = T, .Normal = Normal, .IsStartInside = false, .TriangleIndex = 0U,
        .Barycentric = CollisionMath_Vector(1.0f - U - V, U, V) };
    return true;
}

bool CollisionGeometry_RaycastMesh(const CollisionMesh* mesh, Vector3 origin, Vector3 direction, float maxT,
    bool isTwoSided, CollisionRayHit* outHit)
{
    Vector3 InverseDirection = CollisionMath_Vector(SafeInverse(direction.x), SafeInverse(direction.y),
        SafeInverse(direction.z));
    uint32_t Stack[MESH_STACK_CAPACITY];
    size_t StackCount = 0U;
    Stack[StackCount++] = 0U;
    float Best = maxT;
    bool IsHit = false;
    while (StackCount > 0U)
    {
        const CollisionMeshNode* Node = &mesh->_nodes[Stack[--StackCount]];
        if (!HitBounds(origin, InverseDirection, Node->_min, Node->_max, Best))
        {
            continue;
        }
        if (Node->_count == 0U)
        {
            if ((StackCount + 2U) <= MESH_STACK_CAPACITY)
            {
                Stack[StackCount++] = Node->_start;
                Stack[StackCount++] = (uint32_t)((Node - mesh->_nodes) + 1);
            }
            continue;
        }
        for (uint32_t Index = Node->_start; Index < (Node->_start + Node->_count); Index++)
        {
            const CollisionMeshTriangle* Triangle = &mesh->_triangles[Index];
            CollisionRayHit Hit;
            if (CollisionGeometry_RaycastTriangle(origin, direction, mesh->_vertices[Triangle->_indices[0]],
                mesh->_vertices[Triangle->_indices[1]], mesh->_vertices[Triangle->_indices[2]], isTwoSided, Best, &Hit))
            {
                Hit.TriangleIndex = Index;
                *outHit = Hit;
                Best = Hit.T;
                IsHit = true;
            }
        }
    }
    return IsHit;
}

void CollisionGeometry_QueryMesh(const CollisionMesh* mesh, Vector3 min, Vector3 max, CollisionTriangleVisitor visit,
    void* context)
{
    uint32_t Stack[MESH_STACK_CAPACITY];
    size_t StackCount = 0U;
    Stack[StackCount++] = 0U;
    while (StackCount > 0U)
    {
        const CollisionMeshNode* Node = &mesh->_nodes[Stack[--StackCount]];
        if (!OverlapsBounds(Node->_min, Node->_max, min, max))
        {
            continue;
        }
        if (Node->_count == 0U)
        {
            if ((StackCount + 2U) <= MESH_STACK_CAPACITY)
            {
                Stack[StackCount++] = Node->_start;
                Stack[StackCount++] = (uint32_t)((Node - mesh->_nodes) + 1);
            }
            continue;
        }
        for (uint32_t Index = Node->_start; Index < (Node->_start + Node->_count); Index++)
        {
            const CollisionMeshTriangle* Triangle = &mesh->_triangles[Index];
            Vector3 A = mesh->_vertices[Triangle->_indices[0]];
            Vector3 B = mesh->_vertices[Triangle->_indices[1]];
            Vector3 C = mesh->_vertices[Triangle->_indices[2]];
            Vector3 TriangleMin = CollisionMath_Min(A, CollisionMath_Min(B, C));
            Vector3 TriangleMax = CollisionMath_Max(A, CollisionMath_Max(B, C));
            if (OverlapsBounds(TriangleMin, TriangleMax, min, max) && !visit(context, Index))
            {
                return;
            }
        }
    }
}

bool CollisionGeometry_RaycastShape(const CollisionShape* shape, const CollisionAffine* shapeToWorld,
    const CollisionAffine* worldToShape, Vector3 origin, Vector3 direction, float maxT, CollisionRayHit* outHit)
{
    (void)shapeToWorld;
    Memory_Zero(outHit, sizeof(*outHit));
    Vector3 O = CollisionAffine_TransformPoint(worldToShape, origin);
    Vector3 D = CollisionAffine_TransformVector(worldToShape, direction);
    LocalHit Hit;
    bool IsHit = false;
    switch (shape->Type)
    {
        case CollisionShapeType_Sphere:
            IsHit = HitSphere(O, D, CollisionMath_Vector(0.0f, 0.0f, 0.0f), shape->Radius, maxT, &Hit);
            break;
        case CollisionShapeType_Capsule:
            IsHit = HitCapsule(O, D, shape->Radius, shape->HalfHeight, maxT, &Hit);
            break;
        case CollisionShapeType_Box:
            IsHit = HitBox(O, D, CollisionMath_Vector(0.0f, 0.0f, 0.0f), shape->HalfExtents, false, maxT, &Hit);
            break;
        case CollisionShapeType_Cylinder:
            IsHit = HitCylinder(O, D, shape->Radius, shape->HalfHeight, maxT, &Hit);
            break;
        case CollisionShapeType_ConvexHull:
            IsHit = (shape->Hull != NULL) && HitHull(O, D, shape->Hull, maxT, &Hit);
            break;
        case CollisionShapeType_Plane:
            IsHit = HitPlane(O, D, shape, maxT, &Hit);
            break;
        case CollisionShapeType_Mesh:
        {
            CollisionRayHit MeshHit;
            if ((shape->Mesh == NULL) || !CollisionGeometry_RaycastMesh(shape->Mesh, O, D, maxT, shape->IsTwoSided,
                &MeshHit))
            {
                return false;
            }
            Hit = (LocalHit){ .T = MeshHit.T, .Normal = MeshHit.Normal, .IsStartInside = false };
            outHit->TriangleIndex = MeshHit.TriangleIndex;
            outHit->Barycentric = MeshHit.Barycentric;
            IsHit = true;
            break;
        }
        case CollisionShapeType_Count:
            break;
    }
    if (!IsHit)
    {
        return false;
    }
    outHit->T = Hit.T;
    outHit->IsStartInside = Hit.IsStartInside;
    Vector3 Facing = CollisionMath_NormalizeOr(CollisionMath_Negate(direction), CollisionMath_Vector(0.0f, 1.0f, 0.0f));
    outHit->Normal = Hit.IsStartInside ? Facing
        : CollisionMath_NormalizeOr(CollisionAffine_TransposeTransformVector(worldToShape, Hit.Normal), Facing);
    return true;
}
