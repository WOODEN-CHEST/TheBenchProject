#include <float.h>
#include <math.h>
#include "CollisionGeometry.h"
#include "wr/WRMemory.h"

/*
 * Contact manifolds (source/CollisionGeometry.h).
 *
 * GJK (or EPA, for overlapping cores) gives the normal and one pair of closest points. A resting box needs more than
 * one point, so the manifold is built from the two shapes' SUPPORTING FACES along the normal (A's face toward B, B's
 * toward A): the face, edge or point of each shape most aligned with it. The face with three or more points is the
 * reference; the other one is clipped against its sides (planes through its edges along the normal, Sutherland-
 * Hodgman), and each clipped point is projected onto the reference's plane along the normal for its partner and its
 * separation. Two segments (capsules side by side) keep their overlap. Points farther apart than the asked separation
 * go; when nothing survives, or the faces miss the true deepest point (an edge against an edge), GJK's own point is
 * added. More than COLLISION_MAX_CONTACT_POINTS are cut to the deepest, the farthest from it and the two that span the
 * most area on either side (Jolt's pruning).
 *
 * FEATURE IDS: A's face id in bits 16-23, B's in bits 8-15, and in bits 0-7 the incident vertex a point came from or
 * 0x80 + the clipping plane that cut it (0xFF for GJK's own point). They match while the contact keeps its topology.
 */


// Macros.
/** Room for a clipped polygon: a face of COLLISION_MAX_FACE_POINTS clipped by as many planes. */
#define CLIP_CAPACITY ((COLLISION_MAX_FACE_POINTS * 2U) + 8U)
/** A capsule's segment lies along a face when its direction's cosine with the normal is below this (about 6 degrees). */
#define SEGMENT_SIDE_COSINE 0.1f
/** A cylinder offers its cap when the cap's normal is within 60 degrees of the direction, else its side. */
#define CYLINDER_CAP_COSINE 0.5f
/** A triangle offers its face when its normal is within about 78 degrees of the direction, else its best edge. */
#define TRIANGLE_FACE_COSINE 0.2f
/** A point on a triangle whose barycentric weight for a corner is at most this lies on the edge opposite it. */
#define TRIANGLE_BOUNDARY_WEIGHT 1e-5f
/** Two segments side by side give their overlap's two ends when their directions are within about 8 degrees; crossed
 *  ones touch at one point (GJK's). */
#define SEGMENT_PARALLEL_COSINE 0.99f
/** GJK's point joins the clipped points when they all lie this much shallower than it (metres). */
#define MANIFOLD_DEPTH_TOLERANCE 0.005f
/** A bounded plane's edge contact may not push more than 60 degrees toward its back. */
#define PLANE_BACK_COSINE -0.5f
/** The feature byte of GJK's own point. */
#define FEATURE_GJK_POINT 0xFFU
/** The feature byte of a point cut by a clipping plane (plus the plane's index). */
#define FEATURE_CLIPPED 0x80U


// Types.
typedef struct ContactFaceStruct
{
    Vector3 Points[COLLISION_MAX_FACE_POINTS];
    uint32_t Count;
    uint32_t Id;
} ContactFace;

typedef struct ClipPointStruct
{
    Vector3 Position;
    uint32_t Feature;
} ClipPoint;

typedef struct ManifoldPointsStruct
{
    CollisionContactPoint Points[CLIP_CAPACITY];
    uint32_t Count;
} ManifoldPoints;


// Static functions: supporting faces.
static void SetSinglePoint(ContactFace* face, Vector3 point, uint32_t id)
{
    face->Points[0] = point;
    face->Count = 1U;
    face->Id = id;
}

/* The rounding's offset along @p direction: the sphere's (under a similarity) or the ellipsoid's. */
static Vector3 GetRoundingOffset(const CollisionConvex* convex, Vector3 direction)
{
    if (convex->Radius <= 0.0f)
    {
        return CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    }
    if (convex->IsSimilarity)
    {
        return CollisionMath_Scale(direction, convex->CoreRadius);
    }
    Vector3 Local = CollisionAffine_TransposeTransformVector(&convex->Transform, direction);
    Vector3 Offset = CollisionMath_Scale(CollisionMath_NormalizeOr(Local, CollisionMath_Vector(0.0f, 1.0f, 0.0f)),
        convex->Radius);
    return CollisionAffine_TransformVector(&convex->Transform, Offset);
}

/* A local face normal (or any local covector) as a unit world normal: through the inverse's transpose. */
static Vector3 GetWorldNormal(const CollisionConvex* convex, Vector3 localNormal)
{
    return CollisionMath_NormalizeOr(CollisionAffine_TransposeTransformVector(&convex->Inverse, localNormal),
        localNormal);
}

static void GetSegmentFace(const CollisionConvex* convex, Vector3 direction, ContactFace* out)
{
    Vector3 Bottom = CollisionAffine_TransformPoint(&convex->Transform, CollisionMath_Vector(0.0f, -convex->HalfHeight,
        0.0f));
    Vector3 Top = CollisionAffine_TransformPoint(&convex->Transform, CollisionMath_Vector(0.0f, convex->HalfHeight,
        0.0f));
    Vector3 Offset = GetRoundingOffset(convex, direction);
    Vector3 Axis = CollisionMath_Subtract(Top, Bottom);
    float Length = CollisionMath_Length(Axis);
    if ((Length > COLLISION_MATH_EPSILON)
        && (fabsf(CollisionMath_Dot(Axis, direction)) < (SEGMENT_SIDE_COSINE * Length)))
    {
        out->Points[0] = CollisionMath_Add(Bottom, Offset);
        out->Points[1] = CollisionMath_Add(Top, Offset);
        out->Count = 2U;
        out->Id = 2U;
        return;
    }
    bool IsTop = CollisionMath_Dot(Axis, direction) >= 0.0f;
    SetSinglePoint(out, CollisionMath_Add(IsTop ? Top : Bottom, Offset), IsTop ? 1U : 0U);
}

static void GetBoxFace(const CollisionConvex* convex, Vector3 direction, ContactFace* out)
{
    Vector3 Q = CollisionAffine_TransformVector(&convex->Inverse, direction);
    const CollisionAffine* Inverse = &convex->Inverse;
    float Components[3] = { Q.x, Q.y, Q.z };
    Vector3 Rows[3] = {
        CollisionMath_Vector(Inverse->X.x, Inverse->Y.x, Inverse->Z.x),
        CollisionMath_Vector(Inverse->X.y, Inverse->Y.y, Inverse->Z.y),
        CollisionMath_Vector(Inverse->X.z, Inverse->Y.z, Inverse->Z.z) };
    uint32_t Axis = 0U;
    float Best = -FLT_MAX;
    for (uint32_t Candidate = 0U; Candidate < 3U; Candidate++)
    {
        float Length = CollisionMath_Length(Rows[Candidate]);
        float Score = (Length > 0.0f) ? (fabsf(Components[Candidate]) / Length) : 0.0f;
        if (Score > Best)
        {
            Best = Score;
            Axis = Candidate;
        }
    }
    float Sign = (Components[Axis] >= 0.0f) ? 1.0f : -1.0f;
    float Half[3] = { convex->HalfExtents.x, convex->HalfExtents.y, convex->HalfExtents.z };
    uint32_t U = (Axis + 1U) % 3U;
    uint32_t V = (Axis + 2U) % 3U;
    static const float Corners[4][2] = { { 1.0f, 1.0f }, { -1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };
    for (uint32_t Corner = 0U; Corner < 4U; Corner++)
    {
        float Local[3];
        Local[Axis] = Sign * Half[Axis];
        Local[U] = Corners[Corner][0] * Half[U];
        Local[V] = Corners[Corner][1] * Half[V];
        out->Points[Corner] = CollisionAffine_TransformPoint(&convex->Transform,
            CollisionMath_Vector(Local[0], Local[1], Local[2]));
    }
    out->Count = 4U;
    out->Id = (Axis * 2U) + ((Sign > 0.0f) ? 1U : 0U);
}

static void GetCylinderFace(const CollisionConvex* convex, Vector3 direction, ContactFace* out)
{
    Vector3 CapNormal = GetWorldNormal(convex, CollisionMath_Vector(0.0f, 1.0f, 0.0f));
    float Cosine = CollisionMath_Dot(CapNormal, direction);
    if (fabsf(Cosine) >= CYLINDER_CAP_COSINE)
    {
        float Y = (Cosine >= 0.0f) ? convex->HalfHeight : -convex->HalfHeight;
        for (uint32_t Point = 0U; Point < COLLISION_CYLINDER_CAP_POINTS; Point++)
        {
            float Angle = (2.0f * PI * (float)Point) / (float)COLLISION_CYLINDER_CAP_POINTS;
            out->Points[Point] = CollisionAffine_TransformPoint(&convex->Transform, CollisionMath_Vector(
                convex->CylinderRadius * cosf(Angle), Y, convex->CylinderRadius * sinf(Angle)));
        }
        out->Count = COLLISION_CYLINDER_CAP_POINTS;
        out->Id = (Cosine >= 0.0f) ? 1U : 0U;
        return;
    }
    Vector3 Local = CollisionAffine_TransposeTransformVector(&convex->Transform, direction);
    float Radial = sqrtf((Local.x * Local.x) + (Local.z * Local.z));
    float X = (Radial > COLLISION_MATH_EPSILON) ? ((Local.x / Radial) * convex->CylinderRadius) : convex->CylinderRadius;
    float Z = (Radial > COLLISION_MATH_EPSILON) ? ((Local.z / Radial) * convex->CylinderRadius) : 0.0f;
    out->Points[0] = CollisionAffine_TransformPoint(&convex->Transform, CollisionMath_Vector(X, -convex->HalfHeight, Z));
    out->Points[1] = CollisionAffine_TransformPoint(&convex->Transform, CollisionMath_Vector(X, convex->HalfHeight, Z));
    out->Count = 2U;
    out->Id = 2U;
}

static void GetHullFace(const CollisionConvex* convex, Vector3 direction, ContactFace* out)
{
    const CollisionHull* Hull = convex->Hull;
    uint32_t Best = 0U;
    float BestScore = -FLT_MAX;
    for (uint32_t Face = 0U; Face < Hull->_faceCount; Face++)
    {
        float Score = CollisionMath_Dot(GetWorldNormal(convex, Hull->_faces[Face]._normal), direction);
        if (Score > BestScore)
        {
            BestScore = Score;
            Best = Face;
        }
    }
    const CollisionHullFace* Face = &Hull->_faces[Best];
    uint32_t Count = Face->_vertexCount;
    uint32_t Kept = (Count > COLLISION_MAX_FACE_POINTS) ? COLLISION_MAX_FACE_POINTS : Count;
    for (uint32_t Point = 0U; Point < Kept; Point++)
    {
        // A face with more vertices than the buffer keeps an even spread of them (a convex polygon inside it).
        uint32_t Source = (uint32_t)(((uint64_t)Point * Count) / Kept);
        uint32_t Vertex = Hull->_faceVertexIndices[Face->_firstIndex + Source];
        out->Points[Point] = CollisionAffine_TransformPoint(&convex->Transform, Hull->_vertices[Vertex]);
    }
    out->Count = Kept;
    out->Id = Best & 0xFFU;
}

static void GetTriangleFace(const CollisionConvex* convex, Vector3 direction, ContactFace* out)
{
    if (fabsf(CollisionMath_Dot(convex->TriangleNormal, direction)) >= TRIANGLE_FACE_COSINE)
    {
        out->Points[0] = convex->Vertices[0];
        out->Points[1] = convex->Vertices[1];
        out->Points[2] = convex->Vertices[2];
        out->Count = 3U;
        out->Id = 0U;
        return;
    }
    uint32_t Best = 0U;
    float BestScore = -FLT_MAX;
    for (uint32_t Edge = 0U; Edge < 3U; Edge++)
    {
        Vector3 Middle = CollisionMath_Scale(CollisionMath_Add(convex->Vertices[Edge], convex->Vertices[(Edge + 1U) % 3U]),
            0.5f);
        float Score = CollisionMath_Dot(CollisionMath_Subtract(Middle, convex->Center), direction);
        if (Score > BestScore)
        {
            BestScore = Score;
            Best = Edge;
        }
    }
    out->Points[0] = convex->Vertices[Best];
    out->Points[1] = convex->Vertices[(Best + 1U) % 3U];
    out->Count = 2U;
    out->Id = 1U + Best;
}

/* The face, edge or point of @p convex most aligned with the unit @p direction, on its full surface. */
static void GetSupportingFace(const CollisionConvex* convex, Vector3 direction, ContactFace* out)
{
    switch (convex->Kind)
    {
        case CollisionConvexKind_Point:
            SetSinglePoint(out, CollisionConvex_SupportFull(convex, direction), 0U);
            return;
        case CollisionConvexKind_Segment:
            GetSegmentFace(convex, direction, out);
            return;
        case CollisionConvexKind_Box:
            GetBoxFace(convex, direction, out);
            return;
        case CollisionConvexKind_Cylinder:
            GetCylinderFace(convex, direction, out);
            return;
        case CollisionConvexKind_Hull:
            GetHullFace(convex, direction, out);
            return;
        case CollisionConvexKind_Triangle:
            GetTriangleFace(convex, direction, out);
            return;
    }
    SetSinglePoint(out, CollisionConvex_SupportFull(convex, direction), 0U);
}


// Static functions: clipping.
/* The reference face's normal (Newell's method, robust for any convex polygon), oriented along @p along. */
static Vector3 GetFaceNormal(const ContactFace* face, Vector3 along)
{
    Vector3 Normal = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    for (uint32_t Index = 0U; Index < face->Count; Index++)
    {
        Vector3 Current = face->Points[Index];
        Vector3 Next = face->Points[(Index + 1U) % face->Count];
        Normal.x += (Current.y - Next.y) * (Current.z + Next.z);
        Normal.y += (Current.z - Next.z) * (Current.x + Next.x);
        Normal.z += (Current.x - Next.x) * (Current.y + Next.y);
    }
    Normal = CollisionMath_NormalizeOr(Normal, along);
    return (CollisionMath_Dot(Normal, along) < 0.0f) ? CollisionMath_Negate(Normal) : Normal;
}

static Vector3 GetFaceCentroid(const ContactFace* face)
{
    Vector3 Sum = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    for (uint32_t Index = 0U; Index < face->Count; Index++)
    {
        Sum = CollisionMath_Add(Sum, face->Points[Index]);
    }
    return CollisionMath_Scale(Sum, 1.0f / (float)face->Count);
}

/* Clips a polygon (or a segment, count 2) to the inside of the plane through @p origin with inward normal @p normal. */
static uint32_t ClipAgainstPlane(const ClipPoint* input, uint32_t count, Vector3 origin, Vector3 normal,
    uint32_t planeFeature, ClipPoint* output)
{
    uint32_t Written = 0U;
    if (count == 2U)
    {
        float D0 = CollisionMath_Dot(normal, CollisionMath_Subtract(input[0].Position, origin));
        float D1 = CollisionMath_Dot(normal, CollisionMath_Subtract(input[1].Position, origin));
        if ((D0 < 0.0f) && (D1 < 0.0f))
        {
            return 0U;
        }
        for (uint32_t End = 0U; End < 2U; End++)
        {
            float Here = (End == 0U) ? D0 : D1;
            float There = (End == 0U) ? D1 : D0;
            if (Here >= 0.0f)
            {
                output[Written++] = input[End];
            }
            else
            {
                float T = Here / (Here - There);
                output[Written++] = (ClipPoint){ .Position = CollisionMath_Lerp(input[End].Position,
                    input[1U - End].Position, T), .Feature = planeFeature };
            }
        }
        return Written;
    }
    for (uint32_t Index = 0U; Index < count; Index++)
    {
        const ClipPoint* Current = &input[Index];
        const ClipPoint* Next = &input[(Index + 1U) % count];
        float DCurrent = CollisionMath_Dot(normal, CollisionMath_Subtract(Current->Position, origin));
        float DNext = CollisionMath_Dot(normal, CollisionMath_Subtract(Next->Position, origin));
        if (DCurrent >= 0.0f)
        {
            output[Written++] = *Current;
        }
        if (((DCurrent >= 0.0f) != (DNext >= 0.0f)) && (Written < CLIP_CAPACITY))
        {
            float T = DCurrent / (DCurrent - DNext);
            output[Written++] = (ClipPoint){ .Position = CollisionMath_Lerp(Current->Position, Next->Position, T),
                .Feature = planeFeature };
        }
        if (Written >= CLIP_CAPACITY)
        {
            break;
        }
    }
    return Written;
}

/* Clips @p incident against the sides of @p reference (projected along @p normal) and adds the surviving points with
 * their partners on the reference's plane. */
static void ClipFaces(const ContactFace* reference, const ContactFace* incident, Vector3 normal, bool isReferenceA,
    float maxSeparation, ManifoldPoints* points)
{
    ClipPoint BufferA[CLIP_CAPACITY];
    ClipPoint BufferB[CLIP_CAPACITY];
    uint32_t Count = incident->Count;
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        BufferA[Index] = (ClipPoint){ .Position = incident->Points[Index], .Feature = Index };
    }
    ClipPoint* Input = BufferA;
    ClipPoint* Output = BufferB;
    Vector3 Centroid = GetFaceCentroid(reference);
    for (uint32_t Edge = 0U; (Edge < reference->Count) && (Count > 0U); Edge++)
    {
        Vector3 From = reference->Points[Edge];
        Vector3 To = reference->Points[(Edge + 1U) % reference->Count];
        Vector3 Side = CollisionMath_Cross(CollisionMath_Subtract(To, From), normal);
        if (CollisionMath_LengthSquared(Side) <= COLLISION_MATH_EPSILON)
        {
            continue;
        }
        if (CollisionMath_Dot(Side, CollisionMath_Subtract(Centroid, From)) < 0.0f)
        {
            Side = CollisionMath_Negate(Side);
        }
        Count = ClipAgainstPlane(Input, Count, From, Side, FEATURE_CLIPPED + (Edge & 0x7FU), Output);
        ClipPoint* Swap = Input;
        Input = Output;
        Output = Swap;
    }

    Vector3 ReferenceNormal = GetFaceNormal(reference, isReferenceA ? normal : CollisionMath_Negate(normal));
    float Denominator = CollisionMath_Dot(ReferenceNormal, normal);
    if (fabsf(Denominator) <= COLLISION_MATH_EPSILON)
    {
        return;
    }
    Vector3 PlanePoint = reference->Points[0];
    for (uint32_t Index = 0U; (Index < Count) && (points->Count < CLIP_CAPACITY); Index++)
    {
        Vector3 Q = Input[Index].Position;
        float S = CollisionMath_Dot(ReferenceNormal, CollisionMath_Subtract(Q, PlanePoint)) / Denominator;
        Vector3 OnReference = CollisionMath_AddScaled(Q, normal, -S);
        float Separation = isReferenceA ? S : -S;
        if (Separation > maxSeparation)
        {
            continue;
        }
        uint32_t ReferenceId = reference->Id & 0xFFU;
        uint32_t IncidentId = incident->Id & 0xFFU;
        uint32_t FaceBits = isReferenceA ? ((ReferenceId << 16U) | (IncidentId << 8U))
            : ((IncidentId << 16U) | (ReferenceId << 8U));
        points->Points[points->Count++] = (CollisionContactPoint)
        {
            .PositionA = isReferenceA ? OnReference : Q, .PositionB = isReferenceA ? Q : OnReference,
            .Separation = Separation, .FeatureId = FaceBits | (Input[Index].Feature & 0xFFU)
        };
    }
}

/* Two segments side by side: their overlap along A's segment, when they are nearly parallel (crossed segments touch at
 * one point, which the caller takes from GJK). */
static void ClipSegments(const ContactFace* faceA, const ContactFace* faceB, Vector3 normal, float maxSeparation,
    ManifoldPoints* points)
{
    Vector3 A0 = faceA->Points[0];
    Vector3 A1 = faceA->Points[1];
    Vector3 DirectionA = CollisionMath_NormalizeOr(CollisionMath_Subtract(A1, A0), CollisionMath_Vector(0.0f, 0.0f, 0.0f));
    Vector3 DirectionB = CollisionMath_NormalizeOr(CollisionMath_Subtract(faceB->Points[1], faceB->Points[0]),
        CollisionMath_Vector(0.0f, 0.0f, 0.0f));
    if (fabsf(CollisionMath_Dot(DirectionA, DirectionB)) < SEGMENT_PARALLEL_COSINE)
    {
        return;
    }
    Vector3 Along = CollisionMath_Subtract(A1, A0);
    Along = CollisionMath_AddScaled(Along, normal, -CollisionMath_Dot(Along, normal));
    float LengthSquared = CollisionMath_LengthSquared(Along);
    if (LengthSquared <= COLLISION_MATH_EPSILON)
    {
        return;
    }
    float T0 = CollisionMath_Dot(CollisionMath_Subtract(faceB->Points[0], A0), Along) / LengthSquared;
    float T1 = CollisionMath_Dot(CollisionMath_Subtract(faceB->Points[1], A0), Along) / LengthSquared;
    float Low = fmaxf(0.0f, fminf(T0, T1));
    float High = fminf(1.0f, fmaxf(T0, T1));
    if ((Low > High) || (fabsf(T1 - T0) <= COLLISION_MATH_EPSILON))
    {
        return;
    }
    float Ends[2] = { Low, High };
    for (uint32_t End = 0U; End < 2U; End++)
    {
        float S = (Ends[End] - T0) / (T1 - T0);
        Vector3 OnB = CollisionMath_Lerp(faceB->Points[0], faceB->Points[1], S);
        float TA = CollisionMath_GetClosestSegmentParameter(A0, A1, OnB);
        Vector3 OnA = CollisionMath_Lerp(A0, A1, TA);
        float Separation = CollisionMath_Dot(CollisionMath_Subtract(OnB, OnA), normal);
        if (Separation <= maxSeparation)
        {
            points->Points[points->Count++] = (CollisionContactPoint){ .PositionA = OnA, .PositionB = OnB,
                .Separation = Separation, .FeatureId = ((faceA->Id & 0xFFU) << 16U) | ((faceB->Id & 0xFFU) << 8U) | End };
        }
    }
}

/* Keeps at most COLLISION_MAX_CONTACT_POINTS: the deepest, the farthest from it, and the two spanning the largest
 * areas on either side of their line. */
static void ReducePoints(ManifoldPoints* points, Vector3 normal, CollisionContact* out)
{
    out->PointCount = 0U;
    out->Separation = FLT_MAX;
    if (points->Count <= COLLISION_MAX_CONTACT_POINTS)
    {
        for (uint32_t Index = 0U; Index < points->Count; Index++)
        {
            out->Points[out->PointCount++] = points->Points[Index];
            out->Separation = fminf(out->Separation, points->Points[Index].Separation);
        }
        return;
    }
    const CollisionContactPoint* P = points->Points;
    uint32_t First = 0U;
    for (uint32_t Index = 1U; Index < points->Count; Index++)
    {
        First = (P[Index].Separation < P[First].Separation) ? Index : First;
    }
    uint32_t Second = First;
    float Farthest = -1.0f;
    for (uint32_t Index = 0U; Index < points->Count; Index++)
    {
        float Distance = CollisionMath_LengthSquared(CollisionMath_Subtract(P[Index].PositionB, P[First].PositionB));
        if (Distance > Farthest)
        {
            Farthest = Distance;
            Second = Index;
        }
    }
    uint32_t Third = First;
    uint32_t Fourth = First;
    float MostPositive = 0.0f;
    float MostNegative = 0.0f;
    Vector3 Line = CollisionMath_Subtract(P[Second].PositionB, P[First].PositionB);
    for (uint32_t Index = 0U; Index < points->Count; Index++)
    {
        float Area = CollisionMath_Dot(CollisionMath_Cross(Line, CollisionMath_Subtract(P[Index].PositionB,
            P[First].PositionB)), normal);
        if (Area > MostPositive)
        {
            MostPositive = Area;
            Third = Index;
        }
        if (Area < MostNegative)
        {
            MostNegative = Area;
            Fourth = Index;
        }
    }
    uint32_t Chosen[4] = { First, Second, Third, Fourth };
    for (uint32_t Slot = 0U; Slot < 4U; Slot++)
    {
        bool IsRepeated = false;
        for (uint32_t Earlier = 0U; Earlier < Slot; Earlier++)
        {
            IsRepeated = IsRepeated || (Chosen[Earlier] == Chosen[Slot]);
        }
        if (!IsRepeated)
        {
            out->Points[out->PointCount++] = P[Chosen[Slot]];
            out->Separation = fminf(out->Separation, P[Chosen[Slot]].Separation);
        }
    }
}

/* The manifold along @p normal around GJK's (or EPA's) point pair. */
static void BuildManifold(const CollisionConvex* a, const CollisionConvex* b, Vector3 normal, Vector3 pointA,
    Vector3 pointB, float separation, float maxSeparation, CollisionContact* out)
{
    ContactFace FaceA;
    ContactFace FaceB;
    GetSupportingFace(a, normal, &FaceA);
    GetSupportingFace(b, CollisionMath_Negate(normal), &FaceB);
    ManifoldPoints Points;
    Points.Count = 0U;
    if ((FaceA.Count >= 3U) && (FaceB.Count >= 2U))
    {
        ClipFaces(&FaceA, &FaceB, normal, true, maxSeparation, &Points);
    }
    else if ((FaceB.Count >= 3U) && (FaceA.Count >= 2U))
    {
        ClipFaces(&FaceB, &FaceA, normal, false, maxSeparation, &Points);
    }
    else if ((FaceA.Count == 2U) && (FaceB.Count == 2U))
    {
        ClipSegments(&FaceA, &FaceB, normal, maxSeparation, &Points);
    }
    float Shallowest = FLT_MAX;
    for (uint32_t Index = 0U; Index < Points.Count; Index++)
    {
        Shallowest = fminf(Shallowest, Points.Points[Index].Separation);
    }
    if ((Points.Count == 0U) || (Shallowest > (separation + MANIFOLD_DEPTH_TOLERANCE)))
    {
        Points.Points[Points.Count++] = (CollisionContactPoint){ .PositionA = pointA, .PositionB = pointB,
            .Separation = separation, .FeatureId = ((FaceA.Id & 0xFFU) << 16U) | ((FaceB.Id & 0xFFU) << 8U)
            | FEATURE_GJK_POINT };
    }
    out->Normal = normal;
    ReducePoints(&Points, normal, out);
}


// Static functions: triangles.
/* The feature of a triangle a point ON it lies on, from its barycentric weights (u, v, w) for (a, b, c): an edge where
 * one weight vanishes, a corner where two do, numbered as CollisionMath_GetClosestTrianglePoint's regions (1..3 the
 * edges ab, bc, ca, 4..6 the corners a, b, c), 0 inside. That function calls a point exactly on the boundary inside (its
 * tests tie there), and a contact's witness point on the triangle lies exactly on the edge it touches. */
static uint32_t GetBoundaryRegion(Vector3 weights)
{
    bool IsOnBc = weights.x <= TRIANGLE_BOUNDARY_WEIGHT;
    bool IsOnCa = weights.y <= TRIANGLE_BOUNDARY_WEIGHT;
    bool IsOnAb = weights.z <= TRIANGLE_BOUNDARY_WEIGHT;
    if (IsOnAb && IsOnCa)
    {
        return 4U;
    }
    if (IsOnAb && IsOnBc)
    {
        return 5U;
    }
    if (IsOnBc && IsOnCa)
    {
        return 6U;
    }
    if (IsOnAb)
    {
        return 1U;
    }
    if (IsOnBc)
    {
        return 2U;
    }
    return IsOnCa ? 3U : 0U;
}

/* Applies a triangle's sides and active edges to a contact's normal and points; false when there is no contact (the
 * other shape lies behind a one-sided triangle, or past the separation along the face normal). */
static bool AdjustForTriangle(const CollisionConvex* a, const CollisionConvex* b, float maxSeparation, Vector3* normal,
    Vector3* pointA, Vector3* pointB, float* separation)
{
    bool IsTriangleA = a->Kind == CollisionConvexKind_Triangle;
    const CollisionConvex* Triangle = IsTriangleA ? a : b;
    const CollisionConvex* Other = IsTriangleA ? b : a;
    const Vector3* V = Triangle->Vertices;
    Vector3 Face = Triangle->TriangleNormal;
    bool IsInFront = CollisionMath_Dot(CollisionMath_Subtract(Other->Center, V[0]), Face) >= 0.0f;
    if (Triangle->IsOneSided && !IsInFront)
    {
        return false;
    }
    Vector3 Facing = IsInFront ? Face : CollisionMath_Negate(Face);
    Vector3 Outward = IsTriangleA ? *normal : CollisionMath_Negate(*normal);
    Vector3 Weights;
    uint32_t Region = 0U;
    CollisionMath_GetClosestTrianglePoint(V[0], V[1], V[2], IsTriangleA ? *pointA : *pointB, &Weights, &Region);
    if (Region == 0U)
    {
        // Read as inside, the contact's feature would take the face normal and the face's plane distance: for a
        // capsule beside a mesh box's convex edge, a push-out of most of its height (a player jumping against a mesh
        // cube would be thrown up and down).
        Region = GetBoundaryRegion(Weights);
    }
    uint8_t Active = Triangle->ActiveEdges;
    bool UsesFace = false;
    switch (Region)
    {
        case 0U:
            UsesFace = true;
            break;
        case 1U:
        case 2U:
        case 3U:
            UsesFace = (Active & (1U << (Region - 1U))) == 0U;
            break;
        case 4U:
            UsesFace = (Active & 0x5U) == 0U;
            break;
        case 5U:
            UsesFace = (Active & 0x3U) == 0U;
            break;
        default:
            UsesFace = (Active & 0x6U) == 0U;
            break;
    }
    UsesFace = UsesFace || (Triangle->IsOneSided && (CollisionMath_Dot(Outward, Face) < 0.0f));
    if (!UsesFace)
    {
        return true;
    }
    Vector3 Deepest = CollisionConvex_SupportFull(Other, CollisionMath_Negate(Facing));
    float Separation = CollisionMath_Dot(CollisionMath_Subtract(Deepest, V[0]), Facing);
    if (Separation > maxSeparation)
    {
        return false;
    }
    Vector3 OnTriangle = CollisionMath_AddScaled(Deepest, Facing, -Separation);
    *normal = IsTriangleA ? Facing : CollisionMath_Negate(Facing);
    *pointA = IsTriangleA ? OnTriangle : Deepest;
    *pointB = IsTriangleA ? Deepest : OnTriangle;
    *separation = Separation;
    return true;
}


// Public functions.
bool CollisionGeometry_Collide(const CollisionConvex* a, const CollisionConvex* b, float maxSeparation,
    CollisionContact* outContact)
{
    Memory_Zero(outContact, sizeof(*outContact));
    CollisionDistance Distance;
    CollisionGeometry_GetDistance(a, b, &Distance);
    Vector3 Normal;
    Vector3 PointA;
    Vector3 PointB;
    float Separation = 0.0f;
    if (!Distance.AreCoresOverlapping)
    {
        if (Distance.Distance > maxSeparation)
        {
            return false;
        }
        Normal = Distance.Normal;
        PointA = Distance.PointA;
        PointB = Distance.PointB;
        Separation = Distance.Distance;
    }
    else
    {
        CollisionPenetration Penetration;
        CollisionGeometry_GetPenetration(a, b, &Penetration);
        Normal = Penetration.Normal;
        PointA = Penetration.PointA;
        PointB = Penetration.PointB;
        Separation = -Penetration.Depth;
    }
    bool HasTriangle = (a->Kind == CollisionConvexKind_Triangle) != (b->Kind == CollisionConvexKind_Triangle);
    if (HasTriangle && !AdjustForTriangle(a, b, maxSeparation, &Normal, &PointA, &PointB, &Separation))
    {
        return false;
    }
    BuildManifold(a, b, Normal, PointA, PointB, Separation, maxSeparation, outContact);
    return outContact->PointCount > 0U;
}

bool CollisionGeometry_CollidePlane(const CollisionPlaneView* plane, const CollisionConvex* b, float maxSeparation,
    CollisionContact* outContact)
{
    Memory_Zero(outContact, sizeof(*outContact));
    Vector3 Normal = plane->Normal;
    Vector3 Opposite = CollisionMath_Negate(Normal);
    if (plane->IsBounded)
    {
        if (CollisionMath_Dot(CollisionMath_Subtract(b->Center, plane->Origin), Normal) < -plane->BackDistance)
        {
            return false; // behind the slab: below a floor, the far side of a one-way plane
        }
        CollisionDistance Distance;
        CollisionGeometry_GetDistance(&plane->Slab, b, &Distance);
        if (!Distance.AreCoresOverlapping && (Distance.Distance > maxSeparation))
        {
            return false;
        }
    }
    ContactFace Face;
    GetSupportingFace(b, Opposite, &Face);
    ClipPoint BufferA[CLIP_CAPACITY];
    ClipPoint BufferB[CLIP_CAPACITY];
    uint32_t Count = Face.Count;
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        BufferA[Index] = (ClipPoint){ .Position = Face.Points[Index], .Feature = Index };
    }
    ClipPoint* Input = BufferA;
    ClipPoint* Output = BufferB;
    if (plane->IsBounded)
    {
        Vector3 Centroid = CollisionMath_Scale(CollisionMath_Add(CollisionMath_Add(plane->Corners[0], plane->Corners[1]),
            CollisionMath_Add(plane->Corners[2], plane->Corners[3])), 0.25f);
        for (uint32_t Edge = 0U; (Edge < 4U) && (Count > 0U); Edge++)
        {
            Vector3 From = plane->Corners[Edge];
            Vector3 Side = CollisionMath_Cross(CollisionMath_Subtract(plane->Corners[(Edge + 1U) % 4U], From), Normal);
            if (CollisionMath_Dot(Side, CollisionMath_Subtract(Centroid, From)) < 0.0f)
            {
                Side = CollisionMath_Negate(Side);
            }
            if (Count == 1U)
            {
                // A single point: inside or not.
                Count = (CollisionMath_Dot(Side, CollisionMath_Subtract(Input[0].Position, From)) >= 0.0f) ? 1U : 0U;
                continue;
            }
            Count = ClipAgainstPlane(Input, Count, From, Side, FEATURE_CLIPPED + Edge, Output);
            ClipPoint* Swap = Input;
            Input = Output;
            Output = Swap;
        }
    }
    ManifoldPoints Points;
    Points.Count = 0U;
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        Vector3 Q = Input[Index].Position;
        float Separation = CollisionMath_Dot(CollisionMath_Subtract(Q, plane->Origin), Normal);
        if (Separation <= maxSeparation)
        {
            Points.Points[Points.Count++] = (CollisionContactPoint){ .PositionA = CollisionMath_AddScaled(Q, Normal,
                -Separation), .PositionB = Q, .Separation = Separation,
                .FeatureId = ((Face.Id & 0xFFU) << 8U) | (Input[Index].Feature & 0xFFU) };
        }
    }
    if (!plane->IsBounded && (Points.Count == 0U))
    {
        Vector3 Deepest = CollisionConvex_SupportFull(b, Opposite);
        float Separation = CollisionMath_Dot(CollisionMath_Subtract(Deepest, plane->Origin), Normal);
        if (Separation > maxSeparation)
        {
            return false;
        }
        Points.Points[Points.Count++] = (CollisionContactPoint){ .PositionA = CollisionMath_AddScaled(Deepest, Normal,
            -Separation), .PositionB = Deepest, .Separation = Separation, .FeatureId = FEATURE_GJK_POINT };
    }
    if (Points.Count > 0U)
    {
        outContact->Normal = Normal;
        ReducePoints(&Points, Normal, outContact);
        return true;
    }
    if (!plane->IsBounded)
    {
        return false;
    }
    // Only the rectangle's edge region is touched: the slab's own contact, never pushing toward the back.
    if (!CollisionGeometry_Collide(&plane->Slab, b, maxSeparation, outContact))
    {
        return false;
    }
    if (CollisionMath_Dot(outContact->Normal, Normal) < PLANE_BACK_COSINE)
    {
        Memory_Zero(outContact, sizeof(*outContact));
        return false;
    }
    return true;
}
