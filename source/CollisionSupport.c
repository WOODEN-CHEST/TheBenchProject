#include <float.h>
#include <math.h>
#include "CollisionGeometry.h"
#include "wr/WRMemory.h"

/*
 * Convex views (source/CollisionGeometry.h): building them from shapes and mesh triangles, preparing their derived
 * fields, and their support functions.
 */


// Static functions.
/* The farthest point of the local core along the local direction @p d (ties go to the positive side). */
static Vector3 SupportLocalCore(const CollisionConvex* self, Vector3 d)
{
    switch (self->Kind)
    {
        case CollisionConvexKind_Point:
            return CollisionMath_Vector(0.0f, 0.0f, 0.0f);
        case CollisionConvexKind_Segment:
            return CollisionMath_Vector(0.0f, (d.y >= 0.0f) ? self->HalfHeight : -self->HalfHeight, 0.0f);
        case CollisionConvexKind_Box:
            return CollisionMath_Vector((d.x >= 0.0f) ? self->HalfExtents.x : -self->HalfExtents.x,
                (d.y >= 0.0f) ? self->HalfExtents.y : -self->HalfExtents.y,
                (d.z >= 0.0f) ? self->HalfExtents.z : -self->HalfExtents.z);
        case CollisionConvexKind_Cylinder:
        {
            float Radial = sqrtf((d.x * d.x) + (d.z * d.z));
            float Y = (d.y >= 0.0f) ? self->HalfHeight : -self->HalfHeight;
            if (!(Radial > COLLISION_MATH_EPSILON))
            {
                return CollisionMath_Vector(0.0f, Y, 0.0f);
            }
            float Scale = self->CylinderRadius / Radial;
            return CollisionMath_Vector(d.x * Scale, Y, d.z * Scale);
        }
        case CollisionConvexKind_Hull:
        {
            const CollisionHull* Hull = self->Hull;
            uint32_t Best = 0U;
            float BestDot = -FLT_MAX;
            for (uint32_t Vertex = 0U; Vertex < Hull->_vertexCount; Vertex++)
            {
                float Dot = CollisionMath_Dot(Hull->_vertices[Vertex], d);
                if (Dot > BestDot)
                {
                    BestDot = Dot;
                    Best = Vertex;
                }
            }
            return Hull->_vertices[Best];
        }
        case CollisionConvexKind_Triangle:
            break;
    }
    return CollisionMath_Vector(0.0f, 0.0f, 0.0f);
}

/* The local shape's largest distance from its own origin. */
static float GetLocalBoundingRadius(const CollisionConvex* self)
{
    switch (self->Kind)
    {
        case CollisionConvexKind_Point:
            return self->Radius;
        case CollisionConvexKind_Segment:
            return self->HalfHeight + self->Radius;
        case CollisionConvexKind_Box:
            return CollisionMath_Length(self->HalfExtents);
        case CollisionConvexKind_Cylinder:
            return sqrtf((self->HalfHeight * self->HalfHeight) + (self->CylinderRadius * self->CylinderRadius));
        case CollisionConvexKind_Hull:
        {
            float Largest = 0.0f;
            for (uint32_t Vertex = 0U; Vertex < self->Hull->_vertexCount; Vertex++)
            {
                Largest = fmaxf(Largest, CollisionMath_LengthSquared(self->Hull->_vertices[Vertex]));
            }
            return sqrtf(Largest);
        }
        case CollisionConvexKind_Triangle:
            break;
    }
    return 0.0f;
}


// Public functions.
void CollisionConvex_Prepare(CollisionConvex* self)
{
    if (self->Kind == CollisionConvexKind_Triangle)
    {
        self->Transform = CollisionAffine_Identity();
        self->Inverse = CollisionAffine_Identity();
        self->IsSimilarity = true;
        self->CoreRadius = 0.0f;
        self->Radius = 0.0f;
        self->Center = CollisionMath_Scale(CollisionMath_Add(self->Vertices[0],
            CollisionMath_Add(self->Vertices[1], self->Vertices[2])), 1.0f / 3.0f);
        float Largest = 0.0f;
        for (size_t Corner = 0U; Corner < 3U; Corner++)
        {
            Largest = fmaxf(Largest, CollisionMath_LengthSquared(self->Vertices[Corner]));
        }
        self->BoundingRadius = sqrtf(Largest);
        self->TriangleNormal = CollisionMath_NormalizeOr(CollisionMath_Cross(
            CollisionMath_Subtract(self->Vertices[1], self->Vertices[0]),
            CollisionMath_Subtract(self->Vertices[2], self->Vertices[0])), CollisionMath_Vector(0.0f, 1.0f, 0.0f));
        self->IsValid = true;
        return;
    }
    self->IsValid = CollisionAffine_TryInvert(&self->Transform, &self->Inverse);
    float Scale = 1.0f;
    self->IsSimilarity = CollisionAffine_IsSimilarity(&self->Transform, &Scale);
    self->CoreRadius = self->IsSimilarity ? (self->Radius * Scale) : 0.0f;
    Vector3 LocalCenter = (self->Kind == CollisionConvexKind_Hull) ? self->Hull->_centroid
        : CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    self->Center = CollisionAffine_TransformPoint(&self->Transform, LocalCenter);
    self->BoundingRadius = GetLocalBoundingRadius(self) * CollisionAffine_GetMaxStretch(&self->Transform);
    self->TriangleNormal = CollisionMath_Vector(0.0f, 1.0f, 0.0f);
}

Vector3 CollisionConvex_SupportCore(const CollisionConvex* self, Vector3 direction)
{
    if (self->Kind == CollisionConvexKind_Triangle)
    {
        float Dot0 = CollisionMath_Dot(self->Vertices[0], direction);
        float Dot1 = CollisionMath_Dot(self->Vertices[1], direction);
        float Dot2 = CollisionMath_Dot(self->Vertices[2], direction);
        if ((Dot0 >= Dot1) && (Dot0 >= Dot2))
        {
            return self->Vertices[0];
        }
        return (Dot1 >= Dot2) ? self->Vertices[1] : self->Vertices[2];
    }
    Vector3 Local = CollisionAffine_TransposeTransformVector(&self->Transform, direction);
    Vector3 Point = SupportLocalCore(self, Local);
    if (!self->IsSimilarity && (self->Radius > 0.0f))
    {
        // An uneven scale: the rounding is an ellipsoid, part of the support.
        float Length = CollisionMath_Length(Local);
        if (Length > COLLISION_MATH_EPSILON)
        {
            Point = CollisionMath_AddScaled(Point, Local, self->Radius / Length);
        }
    }
    return CollisionAffine_TransformPoint(&self->Transform, Point);
}

Vector3 CollisionConvex_SupportFull(const CollisionConvex* self, Vector3 direction)
{
    Vector3 Core = CollisionConvex_SupportCore(self, direction);
    if (self->CoreRadius <= 0.0f)
    {
        return Core;
    }
    float Length = CollisionMath_Length(direction);
    return (Length > COLLISION_MATH_EPSILON) ? CollisionMath_AddScaled(Core, direction, self->CoreRadius / Length) : Core;
}

bool CollisionConvex_FromShape(const CollisionShape* shape, const CollisionAffine* shapeToWorld,
    CollisionConvex* outConvex)
{
    Memory_Zero(outConvex, sizeof(*outConvex));
    outConvex->Transform = *shapeToWorld;
    switch (shape->Type)
    {
        case CollisionShapeType_Sphere:
            outConvex->Kind = CollisionConvexKind_Point;
            outConvex->Radius = shape->Radius;
            break;
        case CollisionShapeType_Capsule:
            outConvex->Kind = CollisionConvexKind_Segment;
            outConvex->Radius = shape->Radius;
            outConvex->HalfHeight = shape->HalfHeight;
            break;
        case CollisionShapeType_Box:
            outConvex->Kind = CollisionConvexKind_Box;
            outConvex->HalfExtents = shape->HalfExtents;
            break;
        case CollisionShapeType_Cylinder:
            outConvex->Kind = CollisionConvexKind_Cylinder;
            outConvex->CylinderRadius = shape->Radius;
            outConvex->HalfHeight = shape->HalfHeight;
            break;
        case CollisionShapeType_ConvexHull:
            if (shape->Hull == NULL)
            {
                return false;
            }
            outConvex->Kind = CollisionConvexKind_Hull;
            outConvex->Hull = shape->Hull;
            break;
        case CollisionShapeType_Plane:
        case CollisionShapeType_Mesh:
        case CollisionShapeType_Count:
            return false;
    }
    CollisionConvex_Prepare(outConvex);
    return outConvex->IsValid;
}

void CollisionConvex_FromMeshTriangle(const CollisionMesh* mesh, uint32_t triangle, const CollisionAffine* meshToWorld,
    bool isTwoSided, CollisionConvex* outConvex)
{
    Memory_Zero(outConvex, sizeof(*outConvex));
    const CollisionMeshTriangle* Source = &mesh->_triangles[triangle];
    outConvex->Kind = CollisionConvexKind_Triangle;
    for (size_t Corner = 0U; Corner < 3U; Corner++)
    {
        outConvex->Vertices[Corner] = CollisionAffine_TransformPoint(meshToWorld, mesh->_vertices[Source->_indices[Corner]]);
    }
    if (CollisionAffine_GetDeterminant(meshToWorld) < 0.0f)
    {
        // A mirror turns the winding round: swap two corners so the front stays the front (and the edge bits follow).
        Vector3 Swap = outConvex->Vertices[1];
        outConvex->Vertices[1] = outConvex->Vertices[2];
        outConvex->Vertices[2] = Swap;
        uint8_t Bits = Source->_activeEdges;
        // Edges (0,1) (1,2) (2,0) become (0,2) (2,1) (1,0): bit 0 <- old bit 2, bit 1 <- old bit 1, bit 2 <- old bit 0.
        outConvex->ActiveEdges = (uint8_t)(((Bits >> 2U) & 1U) | (Bits & 2U) | ((Bits & 1U) << 2U));
    }
    else
    {
        outConvex->ActiveEdges = Source->_activeEdges;
    }
    outConvex->IsOneSided = !isTwoSided;
    CollisionConvex_Prepare(outConvex);
}

bool CollisionPlaneView_FromShape(const CollisionShape* shape, const CollisionAffine* shapeToWorld,
    CollisionPlaneView* outPlane)
{
    Memory_Zero(outPlane, sizeof(*outPlane));
    CollisionAffine Inverse;
    if (!CollisionAffine_TryInvert(shapeToWorld, &Inverse))
    {
        return false;
    }
    outPlane->Normal = CollisionMath_NormalizeOr(CollisionAffine_TransposeTransformVector(&Inverse,
        CollisionMath_Vector(0.0f, 1.0f, 0.0f)), CollisionMath_Vector(0.0f, 1.0f, 0.0f));
    outPlane->Origin = shapeToWorld->Translation;
    outPlane->IsBounded = shape->IsBounded;
    if (!shape->IsBounded)
    {
        return true;
    }
    float Hx = shape->PlaneHalfExtents.x;
    float Hz = shape->PlaneHalfExtents.y;
    CollisionAffine SlabOffset = CollisionAffine_Identity();
    SlabOffset.Translation = CollisionMath_Vector(0.0f, -shape->Depth * 0.5f, 0.0f);
    outPlane->Slab.Kind = CollisionConvexKind_Box;
    outPlane->Slab.Transform = CollisionAffine_Multiply(shapeToWorld, &SlabOffset);
    outPlane->Slab.HalfExtents = CollisionMath_Vector(Hx, shape->Depth * 0.5f, Hz);
    CollisionConvex_Prepare(&outPlane->Slab);
    outPlane->Corners[0] = CollisionAffine_TransformPoint(shapeToWorld, CollisionMath_Vector(-Hx, 0.0f, Hz));
    outPlane->Corners[1] = CollisionAffine_TransformPoint(shapeToWorld, CollisionMath_Vector(Hx, 0.0f, Hz));
    outPlane->Corners[2] = CollisionAffine_TransformPoint(shapeToWorld, CollisionMath_Vector(Hx, 0.0f, -Hz));
    outPlane->Corners[3] = CollisionAffine_TransformPoint(shapeToWorld, CollisionMath_Vector(-Hx, 0.0f, -Hz));
    Vector3 Back = CollisionAffine_TransformPoint(shapeToWorld, CollisionMath_Vector(0.0f, -shape->Depth, 0.0f));
    outPlane->BackDistance = fmaxf(0.0f, CollisionMath_Dot(outPlane->Normal, CollisionMath_Subtract(outPlane->Origin, Back)));
    return outPlane->Slab.IsValid;
}

bool CollisionConvex_IsBehindOneSided(const CollisionConvex* convex, Vector3 point)
{
    return (convex->Kind == CollisionConvexKind_Triangle) && convex->IsOneSided
        && (CollisionMath_Dot(CollisionMath_Subtract(point, convex->Vertices[0]), convex->TriangleNormal) < 0.0f);
}

void CollisionConvex_FaceOneSided(const CollisionConvex* target, CollisionCastHit* hit)
{
    if ((target->Kind == CollisionConvexKind_Triangle) && target->IsOneSided
        && (CollisionMath_Dot(hit->Normal, target->TriangleNormal) > 0.0f))
    {
        hit->Normal = CollisionMath_Negate(target->TriangleNormal);
    }
}
