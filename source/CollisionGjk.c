#include <float.h>
#include <math.h>
#include "CollisionGeometry.h"
#include "wr/WRMemory.h"

/*
 * GJK distance, EPA penetration and the GJK ray cast (source/CollisionGeometry.h).
 *
 * GJK works on the Minkowski difference D = A - B: its point nearest the origin is the closest points' difference,
 * found by adding support points toward the origin to a simplex and reducing it to the face, edge or vertex nearest
 * the origin (Johnson's sub-algorithm, by Voronoi regions; Ericson, Real-Time Collision Detection 9.5). EPA grows a
 * polytope inside D from GJK's last simplex until its face nearest the origin lies on D's boundary: that face's normal
 * and distance are the penetration's. The ray cast (van den Bergen 2004) casts the origin along the motion into B - A,
 * advancing along each support plane it is in front of.
 */


// Macros.
/** The most vertices and faces of the EPA polytope. */
#define EPA_MAX_VERTICES 192U
#define EPA_MAX_FACES 384U
/** Below this squared length a vector counts as zero. */
#define TINY_SQUARED 1e-18f
/** A tetrahedron flatter than this share of its edges' cube is treated as flat. */
#define FLAT_VOLUME_SHARE 1e-9f
/** A face sees a new EPA vertex when the vertex lies this far above it (metres). */
#define EPA_VISIBLE_DISTANCE 1e-7f
/** Below this distance between the cores (metres) the direction between their closest points comes too close to the
 *  coordinates' rounding to trust alone (its sign first): it is checked against the last simplex face's normal. */
#define NOISY_DISTANCE 1e-3f
/** A cast's normal and points come from a distance query this far (metres) before the contact, along the motion: at
 *  the contact itself the direction between shapes without rounding (boxes, hulls) is lost in the rounding of floats. */
#define CAST_NORMAL_BACKOFF (2.0f * COLLISION_CAST_TOLERANCE)


// Types.
typedef struct EpaFaceStruct
{
    uint32_t Vertices[3];
    Vector3 Normal;
    float Distance;
    bool IsAlive;
} EpaFace;

typedef struct EpaPolytopeStruct
{
    CollisionSimplexVertex Vertices[EPA_MAX_VERTICES];
    uint32_t VertexCount;
    EpaFace Faces[EPA_MAX_FACES];
    uint32_t FaceCount;
} EpaPolytope;

typedef struct EpaEdgeStruct
{
    uint32_t From;
    uint32_t To;
} EpaEdge;


// Static functions: supports.
static void SupportPair(const CollisionConvex* a, const CollisionConvex* b, Vector3 direction, bool isFull,
    CollisionSimplexVertex* out)
{
    Vector3 Opposite = CollisionMath_Negate(direction);
    out->A = isFull ? CollisionConvex_SupportFull(a, direction) : CollisionConvex_SupportCore(a, direction);
    out->B = isFull ? CollisionConvex_SupportFull(b, Opposite) : CollisionConvex_SupportCore(b, Opposite);
    out->W = CollisionMath_Subtract(out->A, out->B);
}


// Static functions: the simplex.
static void KeepVertices(CollisionSimplex* simplex, uint32_t first, uint32_t second, uint32_t third, uint32_t count,
    const float* weights)
{
    CollisionSimplexVertex Kept[3] = { simplex->Vertices[first], simplex->Vertices[second], simplex->Vertices[third] };
    for (uint32_t Index = 0U; Index < count; Index++)
    {
        simplex->Vertices[Index] = Kept[Index];
        simplex->Weights[Index] = weights[Index];
    }
    simplex->Count = count;
}

/* The sub-simplex of a triangle (vertices i, j, k of the simplex) nearest the origin; writes the closest point and
 * returns its squared distance. Only computes when @p apply is false. */
static float SolveTriangle(CollisionSimplex* simplex, uint32_t i, uint32_t j, uint32_t k, bool apply,
    Vector3* outClosest)
{
    Vector3 Weights;
    uint32_t Region = 0U;
    Vector3 Closest = CollisionMath_GetClosestTrianglePoint(simplex->Vertices[i].W, simplex->Vertices[j].W,
        simplex->Vertices[k].W, CollisionMath_Vector(0.0f, 0.0f, 0.0f), &Weights, &Region);
    *outClosest = Closest;
    if (apply)
    {
        float Pair[3];
        switch (Region)
        {
            case 0U:
                Pair[0] = Weights.x;
                Pair[1] = Weights.y;
                Pair[2] = Weights.z;
                KeepVertices(simplex, i, j, k, 3U, Pair);
                break;
            case 1U:
                Pair[0] = Weights.x;
                Pair[1] = Weights.y;
                KeepVertices(simplex, i, j, k, 2U, Pair);
                break;
            case 2U:
                Pair[0] = Weights.y;
                Pair[1] = Weights.z;
                KeepVertices(simplex, j, k, i, 2U, Pair);
                break;
            case 3U:
                Pair[0] = Weights.z;
                Pair[1] = Weights.x;
                KeepVertices(simplex, k, i, j, 2U, Pair);
                break;
            case 4U:
                Pair[0] = 1.0f;
                KeepVertices(simplex, i, j, k, 1U, Pair);
                break;
            case 5U:
                Pair[0] = 1.0f;
                KeepVertices(simplex, j, k, i, 1U, Pair);
                break;
            default:
                Pair[0] = 1.0f;
                KeepVertices(simplex, k, i, j, 1U, Pair);
                break;
        }
    }
    return CollisionMath_LengthSquared(Closest);
}

/* Reduces the simplex to its feature nearest the origin and writes that point. False when the origin lies inside the
 * tetrahedron (the shapes overlap). */
static bool SolveSimplex(CollisionSimplex* simplex, Vector3* outClosest)
{
    switch (simplex->Count)
    {
        case 1U:
            simplex->Weights[0] = 1.0f;
            *outClosest = simplex->Vertices[0].W;
            return true;
        case 2U:
        {
            Vector3 W0 = simplex->Vertices[0].W;
            Vector3 Edge = CollisionMath_Subtract(simplex->Vertices[1].W, W0);
            float LengthSquared = CollisionMath_LengthSquared(Edge);
            float T = (LengthSquared > TINY_SQUARED) ? (-CollisionMath_Dot(W0, Edge) / LengthSquared) : 0.0f;
            float One = 1.0f;
            if (T <= 0.0f)
            {
                KeepVertices(simplex, 0U, 1U, 1U, 1U, &One);
                *outClosest = simplex->Vertices[0].W;
                return true;
            }
            if (T >= 1.0f)
            {
                KeepVertices(simplex, 1U, 0U, 0U, 1U, &One);
                *outClosest = simplex->Vertices[0].W;
                return true;
            }
            simplex->Weights[0] = 1.0f - T;
            simplex->Weights[1] = T;
            *outClosest = CollisionMath_AddScaled(W0, Edge, T);
            return true;
        }
        case 3U:
            SolveTriangle(simplex, 0U, 1U, 2U, true, outClosest);
            return true;
        default:
            break;
    }

    // A tetrahedron. The origin is inside when its barycentric weights (sub-volumes over the volume) are all positive;
    // a flat one has no inside. Otherwise the nearest of the faces the origin lies outside of.
    static const uint32_t Faces[4][4] = { { 0U, 1U, 2U, 3U }, { 0U, 3U, 1U, 2U }, { 0U, 2U, 3U, 1U }, { 1U, 3U, 2U, 0U } };
    const CollisionSimplexVertex* V = simplex->Vertices;
    Vector3 E1 = CollisionMath_Subtract(V[1].W, V[0].W);
    Vector3 E2 = CollisionMath_Subtract(V[2].W, V[0].W);
    Vector3 E3 = CollisionMath_Subtract(V[3].W, V[0].W);
    float Volume = CollisionMath_Dot(E1, CollisionMath_Cross(E2, E3));
    float Scale = fmaxf(CollisionMath_LengthSquared(E1), fmaxf(CollisionMath_LengthSquared(E2),
        CollisionMath_LengthSquared(E3)));
    bool IsFlat = !(fabsf(Volume) > (FLAT_VOLUME_SHARE * Scale * sqrtf(Scale)));
    if (!IsFlat)
    {
        Vector3 P0 = CollisionMath_Negate(V[0].W); // the origin relative to vertex 0
        float W1 = CollisionMath_Dot(P0, CollisionMath_Cross(E2, E3)) / Volume;
        float W2 = CollisionMath_Dot(E1, CollisionMath_Cross(P0, E3)) / Volume;
        float W3 = CollisionMath_Dot(E1, CollisionMath_Cross(E2, P0)) / Volume;
        float W0 = 1.0f - W1 - W2 - W3;
        if ((W0 > 0.0f) && (W1 > 0.0f) && (W2 > 0.0f) && (W3 > 0.0f))
        {
            // Inside: the weights place the origin (a cast's point of impact) in the tetrahedron.
            simplex->Weights[0] = W0;
            simplex->Weights[1] = W1;
            simplex->Weights[2] = W2;
            simplex->Weights[3] = W3;
            return false;
        }
    }
    float Best = FLT_MAX;
    int32_t BestFace = -1;
    for (uint32_t Face = 0U; Face < 4U; Face++)
    {
        Vector3 A = V[Faces[Face][0]].W;
        Vector3 Normal = CollisionMath_Cross(CollisionMath_Subtract(V[Faces[Face][1]].W, A),
            CollisionMath_Subtract(V[Faces[Face][2]].W, A));
        if (CollisionMath_Dot(Normal, CollisionMath_Subtract(V[Faces[Face][3]].W, A)) > 0.0f)
        {
            Normal = CollisionMath_Negate(Normal);
        }
        bool IsOutside = CollisionMath_Dot(Normal, CollisionMath_Negate(A)) > 0.0f;
        if (!IsOutside && !IsFlat)
        {
            continue;
        }
        Vector3 Closest;
        float Distance = SolveTriangle(simplex, Faces[Face][0], Faces[Face][1], Faces[Face][2], false, &Closest);
        if (Distance < Best)
        {
            Best = Distance;
            BestFace = (int32_t)Face;
        }
    }
    if (BestFace < 0)
    {
        // Numerically inside no face's outer side yet not inside either: the nearest face.
        for (uint32_t Face = 0U; Face < 4U; Face++)
        {
            Vector3 Closest;
            float Distance = SolveTriangle(simplex, Faces[Face][0], Faces[Face][1], Faces[Face][2], false, &Closest);
            if (Distance < Best)
            {
                Best = Distance;
                BestFace = (int32_t)Face;
            }
        }
    }
    SolveTriangle(simplex, Faces[BestFace][0], Faces[BestFace][1], Faces[BestFace][2], true, outClosest);
    return true;
}

static void GetSimplexPoints(const CollisionSimplex* simplex, Vector3* outA, Vector3* outB)
{
    Vector3 A = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    Vector3 B = A;
    for (uint32_t Index = 0U; Index < simplex->Count; Index++)
    {
        A = CollisionMath_AddScaled(A, simplex->Vertices[Index].A, simplex->Weights[Index]);
        B = CollisionMath_AddScaled(B, simplex->Vertices[Index].B, simplex->Weights[Index]);
    }
    *outA = A;
    *outB = B;
}

/* GJK on the cores or the full shapes. True when they overlap (or come within COLLISION_GJK_OVERLAP_DISTANCE); the
 * simplex holds the last vertices either way, *outClosest the nearest point of the difference to the origin, and
 * *outDistance the distance to report. It keeps the best simplex seen and stops when a step no longer brings the point
 * closer: with smooth shapes (cylinders, ellipsoids) float noise can otherwise make it cycle between nearly equal
 * simplices. Every support also bounds the distance from below (the difference lies beyond the support plane), and
 * an exit before convergence reports that bound, never more: a gap is never overstated, so a contact is not missed. */
static bool RunGjk(const CollisionConvex* a, const CollisionConvex* b, bool isFull, CollisionSimplex* simplex,
    Vector3* outClosest, float* outDistance)
{
    simplex->Count = 0U;
    *outDistance = 0.0f;
    Vector3 V = CollisionMath_Subtract(a->Center, b->Center);
    if (CollisionMath_LengthSquared(V) <= TINY_SQUARED)
    {
        V = CollisionMath_Vector(1.0f, 0.0f, 0.0f);
    }
    CollisionSimplex Best;
    Best.Count = 0U;
    Vector3 BestV = V;
    float BestSquared = FLT_MAX;
    float LowerBound = 0.0f;
    bool IsConverged = false;
    for (uint32_t Iteration = 0U; Iteration < COLLISION_GJK_MAX_ITERATIONS; Iteration++)
    {
        CollisionSimplexVertex W;
        SupportPair(a, b, CollisionMath_Negate(V), isFull, &W);
        float Squared = CollisionMath_LengthSquared(V);
        float Projection = CollisionMath_Dot(V, W.W);
        if (Squared > TINY_SQUARED)
        {
            LowerBound = fmaxf(LowerBound, Projection / sqrtf(Squared));
        }
        if ((simplex->Count > 0U) && ((Squared - Projection) <= (COLLISION_GJK_RELATIVE_TOLERANCE * Squared)))
        {
            IsConverged = true;
            break; // no closer support: converged
        }
        bool IsDuplicate = false;
        for (uint32_t Index = 0U; Index < simplex->Count; Index++)
        {
            IsDuplicate = IsDuplicate || (CollisionMath_LengthSquared(CollisionMath_Subtract(simplex->Vertices[Index].W,
                W.W)) <= TINY_SQUARED);
        }
        if (IsDuplicate)
        {
            break;
        }
        // The support along -V still lies on V's side of the origin: the plane through the origin across V separates
        // the difference from it, so the shapes are apart whatever the simplex solve says next.
        bool IsSeparated = CollisionMath_Dot(V, W.W) > 0.0f;
        simplex->Vertices[simplex->Count++] = W;
        if (!SolveSimplex(simplex, &V))
        {
            if (IsSeparated && (Best.Count > 0U))
            {
                *simplex = Best;
                *outClosest = BestV;
                *outDistance = fminf(sqrtf(BestSquared), LowerBound);
                return false;
            }
            *outClosest = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
            return true;
        }
        float NewSquared = CollisionMath_LengthSquared(V);
        if (NewSquared <= (COLLISION_GJK_OVERLAP_DISTANCE * COLLISION_GJK_OVERLAP_DISTANCE))
        {
            *outClosest = V;
            return true;
        }
        if (NewSquared >= BestSquared)
        {
            // No progress: go back to the best simplex.
            *simplex = Best;
            V = BestV;
            break;
        }
        Best = *simplex;
        BestV = V;
        BestSquared = NewSquared;
    }
    *outClosest = V;
    float Length = CollisionMath_Length(V);
    *outDistance = IsConverged ? Length : fminf(Length, LowerBound);
    return false;
}


/* The direction from A to B along which the cores lie farthest apart, among @p estimate and the last simplex face's
 * normal, each both ways. For convex shapes the gap along any direction is at most their distance, and the separating
 * direction reaches it: so the largest gap marks it, even where the closest points' own direction is lost. */
static Vector3 PickSeparatingNormal(const CollisionConvex* a, const CollisionConvex* b, const CollisionSimplex* simplex,
    Vector3 estimate)
{
    Vector3 Candidates[4];
    uint32_t Count = 0U;
    Candidates[Count++] = estimate;
    Candidates[Count++] = CollisionMath_Negate(estimate);
    if (simplex->Count == 3U)
    {
        Vector3 Across = CollisionMath_NormalizeOr(CollisionMath_Cross(
            CollisionMath_Subtract(simplex->Vertices[1].W, simplex->Vertices[0].W),
            CollisionMath_Subtract(simplex->Vertices[2].W, simplex->Vertices[0].W)), estimate);
        Candidates[Count++] = Across;
        Candidates[Count++] = CollisionMath_Negate(Across);
    }
    Vector3 Best = estimate;
    float BestGap = -FLT_MAX;
    for (uint32_t Candidate = 0U; Candidate < Count; Candidate++)
    {
        Vector3 Normal = Candidates[Candidate];
        float Gap = CollisionMath_Dot(CollisionMath_Subtract(CollisionConvex_SupportCore(b, CollisionMath_Negate(Normal)),
            CollisionConvex_SupportCore(a, Normal)), Normal);
        if (Gap > BestGap)
        {
            BestGap = Gap;
            Best = Normal;
        }
    }
    return Best;
}

/* @p source moved by @p offset. */
static void TranslateConvex(const CollisionConvex* source, Vector3 offset, CollisionConvex* outMoved)
{
    *outMoved = *source;
    if (source->Kind == CollisionConvexKind_Triangle)
    {
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            outMoved->Vertices[Corner] = CollisionMath_Add(source->Vertices[Corner], offset);
        }
    }
    else
    {
        outMoved->Transform.Translation = CollisionMath_Add(source->Transform.Translation, offset);
    }
    CollisionConvex_Prepare(outMoved);
}


// Static functions: EPA.
static bool AddEpaFace(EpaPolytope* polytope, uint32_t i, uint32_t j, uint32_t k)
{
    if (polytope->FaceCount >= EPA_MAX_FACES)
    {
        return false;
    }
    Vector3 A = polytope->Vertices[i].W;
    Vector3 Normal = CollisionMath_Cross(CollisionMath_Subtract(polytope->Vertices[j].W, A),
        CollisionMath_Subtract(polytope->Vertices[k].W, A));
    float Length = CollisionMath_Length(Normal);
    if (!(Length > 1e-12f))
    {
        return false;
    }
    Normal = CollisionMath_Scale(Normal, 1.0f / Length);
    EpaFace* Face = &polytope->Faces[polytope->FaceCount++];
    Face->Vertices[0] = i;
    Face->Vertices[1] = j;
    Face->Vertices[2] = k;
    Face->Normal = Normal;
    Face->Distance = CollisionMath_Dot(Normal, A);
    Face->IsAlive = true;
    return true;
}

/* Adds face (i, j, k) wound so its normal points away from @p inside. */
static bool AddOutwardFace(EpaPolytope* polytope, uint32_t i, uint32_t j, uint32_t k, Vector3 inside)
{
    Vector3 A = polytope->Vertices[i].W;
    Vector3 Normal = CollisionMath_Cross(CollisionMath_Subtract(polytope->Vertices[j].W, A),
        CollisionMath_Subtract(polytope->Vertices[k].W, A));
    return (CollisionMath_Dot(Normal, CollisionMath_Subtract(inside, A)) > 0.0f) ? AddEpaFace(polytope, i, k, j)
        : AddEpaFace(polytope, i, j, k);
}

static bool IsFarFrom(const EpaPolytope* polytope, Vector3 point)
{
    for (uint32_t Index = 0U; Index < polytope->VertexCount; Index++)
    {
        if (CollisionMath_LengthSquared(CollisionMath_Subtract(polytope->Vertices[Index].W, point)) <= 1e-12f)
        {
            return false;
        }
    }
    return true;
}

/* Grows GJK's simplex into a polytope with volume around the origin: a tetrahedron, or a double pyramid on a triangle
 * through the origin. False when the difference is flat there (the shapes only touch). */
static bool BuildInitialPolytope(const CollisionConvex* a, const CollisionConvex* b, const CollisionSimplex* simplex,
    bool isFull, EpaPolytope* polytope)
{
    polytope->VertexCount = simplex->Count;
    polytope->FaceCount = 0U;
    for (uint32_t Index = 0U; Index < simplex->Count; Index++)
    {
        polytope->Vertices[Index] = simplex->Vertices[Index];
    }
    static const Vector3 Axes[6] = { { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };
    for (uint32_t Axis = 0U; (Axis < 6U) && (polytope->VertexCount < 2U); Axis++)
    {
        CollisionSimplexVertex W;
        SupportPair(a, b, Axes[Axis], isFull, &W);
        if (IsFarFrom(polytope, W.W) || (polytope->VertexCount == 0U))
        {
            polytope->Vertices[polytope->VertexCount++] = W;
        }
    }
    if (polytope->VertexCount == 2U)
    {
        Vector3 Direction = CollisionMath_NormalizeOr(CollisionMath_Subtract(polytope->Vertices[1].W,
            polytope->Vertices[0].W), CollisionMath_Vector(1.0f, 0.0f, 0.0f));
        Vector3 U;
        Vector3 V;
        CollisionMath_GetBasis(Direction, &U, &V);
        for (uint32_t Step = 0U; (Step < 6U) && (polytope->VertexCount < 3U); Step++)
        {
            float Angle = (float)Step * (PI / 3.0f);
            Vector3 Around = CollisionMath_Add(CollisionMath_Scale(U, cosf(Angle)), CollisionMath_Scale(V, sinf(Angle)));
            CollisionSimplexVertex W;
            SupportPair(a, b, Around, isFull, &W);
            Vector3 Cross = CollisionMath_Cross(CollisionMath_Subtract(polytope->Vertices[1].W, polytope->Vertices[0].W),
                CollisionMath_Subtract(W.W, polytope->Vertices[0].W));
            if (CollisionMath_LengthSquared(Cross) > 1e-14f)
            {
                polytope->Vertices[polytope->VertexCount++] = W;
            }
        }
    }
    if (polytope->VertexCount < 3U)
    {
        return false;
    }
    if (polytope->VertexCount == 3U)
    {
        Vector3 W0 = polytope->Vertices[0].W;
        Vector3 Normal = CollisionMath_NormalizeOr(CollisionMath_Cross(CollisionMath_Subtract(polytope->Vertices[1].W, W0),
            CollisionMath_Subtract(polytope->Vertices[2].W, W0)), CollisionMath_Vector(0.0f, 1.0f, 0.0f));
        CollisionSimplexVertex Up;
        CollisionSimplexVertex Down;
        SupportPair(a, b, Normal, isFull, &Up);
        SupportPair(a, b, CollisionMath_Negate(Normal), isFull, &Down);
        float UpHeight = CollisionMath_Dot(Normal, CollisionMath_Subtract(Up.W, W0));
        float DownHeight = CollisionMath_Dot(Normal, CollisionMath_Subtract(Down.W, W0));
        bool HasUp = UpHeight > 1e-7f;
        bool HasDown = DownHeight < -1e-7f;
        if (!HasUp && !HasDown)
        {
            return false;
        }
        Vector3 Inside = CollisionMath_Scale(CollisionMath_Add(W0, CollisionMath_Add(polytope->Vertices[1].W,
            polytope->Vertices[2].W)), 1.0f / 3.0f);
        if (HasUp && HasDown)
        {
            polytope->Vertices[3] = Up;
            polytope->Vertices[4] = Down;
            polytope->VertexCount = 5U;
            bool IsBuilt = true;
            for (uint32_t Edge = 0U; Edge < 3U; Edge++)
            {
                uint32_t From = Edge;
                uint32_t To = (Edge + 1U) % 3U;
                IsBuilt = IsBuilt && AddOutwardFace(polytope, From, To, 3U, Inside)
                    && AddOutwardFace(polytope, From, To, 4U, Inside);
            }
            return IsBuilt;
        }
        polytope->Vertices[3] = HasUp ? Up : Down;
        polytope->VertexCount = 4U;
    }
    Vector3 Centroid = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    for (uint32_t Index = 0U; Index < 4U; Index++)
    {
        Centroid = CollisionMath_AddScaled(Centroid, polytope->Vertices[Index].W, 0.25f);
    }
    return AddOutwardFace(polytope, 0U, 1U, 2U, Centroid) && AddOutwardFace(polytope, 0U, 3U, 1U, Centroid)
        && AddOutwardFace(polytope, 0U, 2U, 3U, Centroid) && AddOutwardFace(polytope, 1U, 3U, 2U, Centroid);
}

/* Adds an edge to the horizon list, or removes its reverse when present (an edge between two visible faces). */
static void ToggleEdge(EpaEdge* edges, uint32_t* edgeCount, uint32_t from, uint32_t to)
{
    for (uint32_t Index = 0U; Index < *edgeCount; Index++)
    {
        if ((edges[Index].From == to) && (edges[Index].To == from))
        {
            edges[Index] = edges[--(*edgeCount)];
            return;
        }
    }
    edges[(*edgeCount)++] = (EpaEdge){ .From = from, .To = to };
}

static const EpaFace* FindClosestFace(const EpaPolytope* polytope)
{
    const EpaFace* Best = NULL;
    for (uint32_t Index = 0U; Index < polytope->FaceCount; Index++)
    {
        const EpaFace* Face = &polytope->Faces[Index];
        if (Face->IsAlive && ((Best == NULL) || (Face->Distance < Best->Distance)))
        {
            Best = Face;
        }
    }
    return Best;
}

/* Expands the polytope until its closest face lies on the difference's boundary; returns that face, and the normal of
 * every face it examined whose support lay nearest (a direction the depth along which is known, for the caller's
 * check). */
static EpaFace RunEpa(const CollisionConvex* a, const CollisionConvex* b, bool isFull, EpaPolytope* polytope,
    Vector3* outExaminedNormal)
{
    EpaEdge Edges[EPA_MAX_FACES * 3U];
    EpaFace Closest = *FindClosestFace(polytope);
    *outExaminedNormal = Closest.Normal;
    float NearestSupport = FLT_MAX;
    for (uint32_t Iteration = 0U; Iteration < COLLISION_EPA_MAX_ITERATIONS; Iteration++)
    {
        const EpaFace* Face = FindClosestFace(polytope);
        if (Face == NULL)
        {
            break;
        }
        Closest = *Face;
        CollisionSimplexVertex W;
        SupportPair(a, b, Closest.Normal, isFull, &W);
        float Support = CollisionMath_Dot(W.W, Closest.Normal);
        if (Support < NearestSupport)
        {
            NearestSupport = Support;
            *outExaminedNormal = Closest.Normal;
        }
        if (((CollisionMath_Dot(W.W, Closest.Normal) - Closest.Distance) < COLLISION_EPA_TOLERANCE)
            || (polytope->VertexCount >= EPA_MAX_VERTICES))
        {
            break;
        }
        uint32_t NewVertex = polytope->VertexCount++;
        polytope->Vertices[NewVertex] = W;
        uint32_t EdgeCount = 0U;
        uint32_t Seen = 0U;
        for (uint32_t Index = 0U; Index < polytope->FaceCount; Index++)
        {
            EpaFace* Other = &polytope->Faces[Index];
            if (!Other->IsAlive || (CollisionMath_Dot(Other->Normal, CollisionMath_Subtract(W.W,
                polytope->Vertices[Other->Vertices[0]].W)) <= EPA_VISIBLE_DISTANCE))
            {
                continue;
            }
            Other->IsAlive = false;
            Seen++;
            for (uint32_t Edge = 0U; Edge < 3U; Edge++)
            {
                ToggleEdge(Edges, &EdgeCount, Other->Vertices[Edge], Other->Vertices[(Edge + 1U) % 3U]);
            }
        }
        if (Seen == 0U)
        {
            break;
        }
        bool IsBuilt = true;
        for (uint32_t Edge = 0U; (Edge < EdgeCount) && IsBuilt; Edge++)
        {
            IsBuilt = AddEpaFace(polytope, Edges[Edge].From, Edges[Edge].To, NewVertex);
        }
        if (!IsBuilt)
        {
            break; // out of faces or a sliver: the best face so far stands
        }
    }
    return Closest;
}


// Static functions: cores without a volume.
/* Whether a shape's core has a volume (its Minkowski difference with anything does too): not a point, a segment or a
 * triangle. */
static bool HasVolume(const CollisionConvex* convex)
{
    switch (convex->Kind)
    {
        case CollisionConvexKind_Point:
        case CollisionConvexKind_Segment:
        case CollisionConvexKind_Triangle:
            return false;
        case CollisionConvexKind_Box:
        case CollisionConvexKind_Cylinder:
        case CollisionConvexKind_Hull:
            return true;
    }
    return true;
}

static Vector3 GetSegmentDirection(const CollisionConvex* convex)
{
    if (convex->Kind != CollisionConvexKind_Segment)
    {
        return CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    }
    return CollisionMath_NormalizeOr(convex->Transform.Y, CollisionMath_Vector(0.0f, 0.0f, 0.0f));
}

/* @p v without its part along the unit @p axis, normalised; @p fallback when nothing is left. */
static Vector3 GetPerpendicular(Vector3 v, Vector3 axis, Vector3 fallback)
{
    return CollisionMath_NormalizeOr(CollisionMath_AddScaled(v, axis, -CollisionMath_Dot(v, axis)), fallback);
}

/* Rounded shapes whose cores (points, segments, triangles) meet: the cores have no volume to grow a polytope in, but
 * where they touch the depth is the radii, along the triangle's face, across both segments, or between the centres. */
static bool GetCorePenetration(const CollisionConvex* a, const CollisionConvex* b, CollisionPenetration* outPenetration)
{
    Vector3 Between = CollisionMath_Subtract(b->Center, a->Center);
    Vector3 Up = CollisionMath_Vector(0.0f, 1.0f, 0.0f);
    Vector3 Normal = CollisionMath_NormalizeOr(Between, Up);
    if (a->Kind == CollisionConvexKind_Triangle)
    {
        Normal = (CollisionMath_Dot(a->TriangleNormal, CollisionMath_Subtract(b->Center, a->Vertices[0])) >= 0.0f)
            ? a->TriangleNormal : CollisionMath_Negate(a->TriangleNormal);
    }
    else if (b->Kind == CollisionConvexKind_Triangle)
    {
        Normal = (CollisionMath_Dot(b->TriangleNormal, CollisionMath_Subtract(a->Center, b->Vertices[0])) >= 0.0f)
            ? CollisionMath_Negate(b->TriangleNormal) : b->TriangleNormal;
    }
    else
    {
        Vector3 DirectionA = GetSegmentDirection(a);
        Vector3 DirectionB = GetSegmentDirection(b);
        bool IsSegmentA = CollisionMath_LengthSquared(DirectionA) > 0.0f;
        bool IsSegmentB = CollisionMath_LengthSquared(DirectionB) > 0.0f;
        Vector3 Basis;
        Vector3 Other;
        CollisionMath_GetBasis(IsSegmentA ? DirectionA : (IsSegmentB ? DirectionB : Up), &Basis, &Other);
        if (IsSegmentA && IsSegmentB)
        {
            Vector3 Across = CollisionMath_Cross(DirectionA, DirectionB);
            Normal = (CollisionMath_LengthSquared(Across) > 1e-6f) ? CollisionMath_NormalizeOr(Across, Basis)
                : GetPerpendicular(Normal, DirectionA, Basis);
            Normal = (CollisionMath_Dot(Normal, Between) < 0.0f) ? CollisionMath_Negate(Normal) : Normal;
        }
        else if (IsSegmentA || IsSegmentB)
        {
            Normal = GetPerpendicular(Normal, IsSegmentA ? DirectionA : DirectionB, Basis);
        }
    }
    Vector3 DeepestA = CollisionConvex_SupportFull(a, Normal);
    Vector3 DeepestB = CollisionConvex_SupportFull(b, CollisionMath_Negate(Normal));
    outPenetration->Normal = Normal;
    outPenetration->Depth = fmaxf(0.0f, CollisionMath_Dot(CollisionMath_Subtract(DeepestA, DeepestB), Normal));
    outPenetration->PointA = DeepestA;
    outPenetration->PointB = DeepestB;
    return true;
}


// Public functions.
void CollisionGeometry_GetDistance(const CollisionConvex* a, const CollisionConvex* b, CollisionDistance* outDistance)
{
    Memory_Zero(outDistance, sizeof(*outDistance));
    Vector3 Closest;
    float CoreDistance = 0.0f;
    if (RunGjk(a, b, false, &outDistance->Simplex, &Closest, &CoreDistance))
    {
        outDistance->AreCoresOverlapping = true;
        outDistance->Distance = -(a->CoreRadius + b->CoreRadius);
        outDistance->Normal = CollisionMath_Vector(0.0f, 1.0f, 0.0f);
        return;
    }
    Vector3 PointA;
    Vector3 PointB;
    GetSimplexPoints(&outDistance->Simplex, &PointA, &PointB);
    Vector3 Normal = CollisionMath_NormalizeOr(CollisionMath_Negate(Closest), CollisionMath_Vector(0.0f, 1.0f, 0.0f));
    if (CoreDistance < NOISY_DISTANCE)
    {
        Normal = PickSeparatingNormal(a, b, &outDistance->Simplex, Normal);
    }
    outDistance->Normal = Normal;
    outDistance->PointA = CollisionMath_AddScaled(PointA, Normal, a->CoreRadius);
    outDistance->PointB = CollisionMath_AddScaled(PointB, Normal, -b->CoreRadius);
    outDistance->Distance = CoreDistance - a->CoreRadius - b->CoreRadius;
}

bool CollisionGeometry_GetPenetration(const CollisionConvex* a, const CollisionConvex* b,
    CollisionPenetration* outPenetration)
{
    Memory_Zero(outPenetration, sizeof(*outPenetration));
    if ((a->CoreRadius > 0.0f) || (b->CoreRadius > 0.0f))
    {
        // Rounded shapes whose cores are apart overlap by their roundings only: exactly the distance's, along its
        // normal (the deepest points are its points).
        CollisionDistance Cores;
        CollisionGeometry_GetDistance(a, b, &Cores);
        if (!Cores.AreCoresOverlapping)
        {
            outPenetration->Normal = Cores.Normal;
            outPenetration->Depth = fmaxf(0.0f, -Cores.Distance);
            outPenetration->PointA = Cores.PointA;
            outPenetration->PointB = Cores.PointB;
            return Cores.Distance <= 0.0f;
        }
        if (!HasVolume(a) && !HasVolume(b))
        {
            return GetCorePenetration(a, b, outPenetration);
        }
    }
    // With a rounded shape, EPA runs on the cores (one of which has a volume) and the radii are added back: exact for
    // a sphere or capsule in a box. Without one, on the full shapes.
    bool UsesCores = (a->CoreRadius > 0.0f) || (b->CoreRadius > 0.0f);
    CollisionSimplex Simplex;
    Vector3 Closest;
    float Unused = 0.0f;
    bool IsOverlapping = RunGjk(a, b, !UsesCores, &Simplex, &Closest, &Unused);
    Vector3 PointA;
    Vector3 PointB;
    GetSimplexPoints(&Simplex, &PointA, &PointB);
    Vector3 Fallback = CollisionMath_NormalizeOr(CollisionMath_Subtract(b->Center, a->Center),
        CollisionMath_Vector(0.0f, 1.0f, 0.0f));
    outPenetration->Normal = CollisionMath_NormalizeOr(CollisionMath_Negate(Closest), Fallback);
    outPenetration->PointA = PointA;
    outPenetration->PointB = PointB;
    if (!IsOverlapping)
    {
        return false;
    }
    EpaPolytope Polytope;
    if (!BuildInitialPolytope(a, b, &Simplex, !UsesCores, &Polytope))
    {
        return false; // the difference is flat here: the shapes touch
    }
    Vector3 ExaminedNormal;
    EpaFace Face = RunEpa(a, b, !UsesCores, &Polytope, &ExaminedNormal);
    Vector3 Projection = CollisionMath_Scale(Face.Normal, Face.Distance);
    Vector3 Weights;
    uint32_t Region = 0U;
    const CollisionSimplexVertex* V0 = &Polytope.Vertices[Face.Vertices[0]];
    const CollisionSimplexVertex* V1 = &Polytope.Vertices[Face.Vertices[1]];
    const CollisionSimplexVertex* V2 = &Polytope.Vertices[Face.Vertices[2]];
    CollisionMath_GetClosestTrianglePoint(V0->W, V1->W, V2->W, Projection, &Weights, &Region);
    outPenetration->Normal = Face.Normal;
    outPenetration->Depth = fmaxf(0.0f, Face.Distance) + a->CoreRadius + b->CoreRadius;
    outPenetration->PointA = CollisionMath_AddScaled(CollisionMath_Add(CollisionMath_Add(CollisionMath_Scale(V0->A,
        Weights.x), CollisionMath_Scale(V1->A, Weights.y)), CollisionMath_Scale(V2->A, Weights.z)), Face.Normal,
        a->CoreRadius);
    outPenetration->PointB = CollisionMath_AddScaled(CollisionMath_Add(CollisionMath_Add(CollisionMath_Scale(V0->B,
        Weights.x), CollisionMath_Scale(V1->B, Weights.y)), CollisionMath_Scale(V2->B, Weights.z)), Face.Normal,
        -b->CoreRadius);

    // Every direction bounds the depth from above (moving B that far along it separates the shapes). When the origin
    // lies within a hair of the difference's boundary (shapes that only touch), the expansion around that nearly flat
    // face can lose it and end on a face millimetres away: check the directions it may have lost (the nearest support
    // it examined, and GJK's own: its simplex passed by the origin) and keep the smallest depth.
    Vector3 Candidates[5];
    uint32_t CandidateCount = 0U;
    Candidates[CandidateCount++] = ExaminedNormal;
    if (CollisionMath_LengthSquared(Closest) > 0.0f)
    {
        Vector3 Direction = CollisionMath_NormalizeOr(Closest, ExaminedNormal);
        Candidates[CandidateCount++] = Direction;
        Candidates[CandidateCount++] = CollisionMath_Negate(Direction);
    }
    if (Simplex.Count == 3U)
    {
        Vector3 Across = CollisionMath_NormalizeOr(CollisionMath_Cross(
            CollisionMath_Subtract(Simplex.Vertices[1].W, Simplex.Vertices[0].W),
            CollisionMath_Subtract(Simplex.Vertices[2].W, Simplex.Vertices[0].W)), ExaminedNormal);
        Candidates[CandidateCount++] = Across;
        Candidates[CandidateCount++] = CollisionMath_Negate(Across);
    }
    for (uint32_t Candidate = 0U; Candidate < CandidateCount; Candidate++)
    {
        Vector3 Normal = Candidates[Candidate];
        Vector3 DeepestA = CollisionConvex_SupportFull(a, Normal);
        Vector3 DeepestB = CollisionConvex_SupportFull(b, CollisionMath_Negate(Normal));
        float Depth = CollisionMath_Dot(CollisionMath_Subtract(DeepestA, DeepestB), Normal);
        if (Depth < outPenetration->Depth)
        {
            outPenetration->Normal = Normal;
            outPenetration->Depth = fmaxf(0.0f, Depth);
            outPenetration->PointA = DeepestA;
            outPenetration->PointB = DeepestB;
        }
    }
    return true;
}

bool CollisionGeometry_Cast(const CollisionConvex* a, Vector3 translation, const CollisionConvex* b,
    CollisionCastHit* outHit)
{
    Memory_Zero(outHit, sizeof(*outHit));
    if (CollisionConvex_IsBehindOneSided(b, a->Center))
    {
        return false;
    }
    CollisionDistance Start;
    CollisionGeometry_GetDistance(a, b, &Start);
    if (Start.AreCoresOverlapping || (Start.Distance <= 0.0f))
    {
        outHit->IsStartPenetrating = true;
        if (Start.AreCoresOverlapping)
        {
            CollisionPenetration Penetration;
            CollisionGeometry_GetPenetration(a, b, &Penetration);
            outHit->Normal = Penetration.Normal;
            outHit->PenetrationDepth = Penetration.Depth;
            outHit->PointA = Penetration.PointA;
            outHit->PointB = Penetration.PointB;
        }
        else
        {
            outHit->Normal = Start.Normal;
            outHit->PenetrationDepth = -Start.Distance;
            outHit->PointA = Start.PointA;
            outHit->PointB = Start.PointB;
        }
        CollisionConvex_FaceOneSided(b, outHit);
        bool IsDeeper = CollisionMath_Dot(translation, outHit->Normal)
            > (COLLISION_GJK_RELATIVE_TOLERANCE * CollisionMath_Length(translation));
        if ((outHit->PenetrationDepth <= COLLISION_SHALLOW_START_DEPTH) && !IsDeeper)
        {
            Memory_Zero(outHit, sizeof(*outHit));
            return false; // resting where the last cast left it, and moving away or along: free
        }
        return true;
    }
    if (CollisionMath_LengthSquared(translation) <= TINY_SQUARED)
    {
        return false;
    }

    // Cast the origin along the motion into C = B - A (cores), stopping within the radii's sum of it.
    float Radius = a->CoreRadius + b->CoreRadius;
    float Lambda = 0.0f;
    Vector3 X = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    Vector3 Normal = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    CollisionSimplex Simplex;
    Simplex.Count = 0U;
    Vector3 V = CollisionMath_Subtract(X, CollisionMath_Subtract(b->Center, a->Center));
    bool IsHit = false;
    bool IsStalled = true; // until the loop ends by a hit or a sure miss
    float PreviousLength = FLT_MAX;
    uint32_t StallCount = 0U;
    for (uint32_t Iteration = 0U; Iteration < COLLISION_GJK_MAX_ITERATIONS; Iteration++)
    {
        float Length = CollisionMath_Length(V);
        if (Length <= (Radius + COLLISION_CAST_TOLERANCE))
        {
            IsHit = true;
            IsStalled = false;
            break;
        }
        // The support of C along V: B's farthest along V less A's farthest against it.
        CollisionSimplexVertex P;
        P.A = CollisionConvex_SupportCore(a, CollisionMath_Negate(V));
        P.B = CollisionConvex_SupportCore(b, V);
        P.W = CollisionMath_Subtract(P.B, P.A);
        float VW = CollisionMath_Dot(V, CollisionMath_Subtract(X, P.W));
        bool HasAdvanced = false;
        if (VW > (Radius * Length))
        {
            float VR = CollisionMath_Dot(V, translation);
            if (VR >= 0.0f)
            {
                return false; // in front of a support plane and not closing on it: a sure miss
            }
            Lambda -= (VW - (Radius * Length)) / VR;
            if (Lambda > 1.0f)
            {
                return false; // the motion ends before the support plane: a sure miss
            }
            X = CollisionMath_Scale(translation, Lambda);
            Normal = V;
            HasAdvanced = true;
        }
        // Float noise with smooth shapes can stop the simplex short of X without a step forward: a stall.
        StallCount = (!HasAdvanced && (Length >= (PreviousLength * (1.0f - 1e-4f)))) ? (StallCount + 1U) : 0U;
        if (StallCount >= 3U)
        {
            break;
        }
        PreviousLength = Length;
        // The simplex's point nearest X: solve on the points shifted by -X.
        if (Simplex.Count < 4U)
        {
            Simplex.Vertices[Simplex.Count++] = P;
        }
        for (uint32_t Index = 0U; Index < Simplex.Count; Index++)
        {
            Simplex.Vertices[Index].W = CollisionMath_Subtract(Simplex.Vertices[Index].W, X);
        }
        Vector3 Closest;
        bool IsOutside = SolveSimplex(&Simplex, &Closest);
        for (uint32_t Index = 0U; Index < Simplex.Count; Index++)
        {
            Simplex.Vertices[Index].W = CollisionMath_Add(Simplex.Vertices[Index].W, X);
        }
        if (!IsOutside)
        {
            IsHit = true; // X lies inside the simplex, so inside C
            IsStalled = false;
            break;
        }
        V = CollisionMath_Negate(Closest);
    }
    if (IsStalled)
    {
        // Not converged: conservative advancement over the same motion never skips a contact.
        CollisionPose From = CollisionPose_Identity();
        CollisionPose To = { .Position = translation, .Rotation = QuaternionIdentity() };
        return CollisionGeometry_CastMotion(a, &From, &To, b, outHit);
    }
    if (!IsHit || (Simplex.Count == 0U))
    {
        return false;
    }
    // A start already within the tolerance: its own distance query gives the normal and the points, and it is a hit
    // only when the motion closes on B along that normal (moving away or along a surface it rests on is free).
    float Length = CollisionMath_Length(translation);
    if (Lambda == 0.0f)
    {
        if (CollisionMath_Dot(translation, Start.Normal) <= (COLLISION_GJK_RELATIVE_TOLERANCE * Length))
        {
            return false;
        }
        outHit->Normal = Start.Normal;
        outHit->PointA = Start.PointA;
        outHit->PointB = Start.PointB;
        CollisionConvex_FaceOneSided(b, outHit);
        return true;
    }
    // Otherwise from a distance query a hair before the contact: the last advance's direction is older, and at the
    // contact the direction between the cores is lost in rounding.
    Vector3 Shift = CollisionMath_Scale(translation, CAST_NORMAL_BACKOFF / Length);
    CollisionConvex Before;
    TranslateConvex(a, CollisionMath_Subtract(CollisionMath_Scale(translation, Lambda), Shift), &Before);
    CollisionDistance Near;
    CollisionGeometry_GetDistance(&Before, b, &Near);
    if (!Near.AreCoresOverlapping && (CollisionMath_Dot(Near.Normal, translation) > 0.0f))
    {
        outHit->Normal = Near.Normal;
        outHit->PointA = CollisionMath_Add(Near.PointA, Shift);
        outHit->PointB = Near.PointB;
    }
    else
    {
        Vector3 Fallback = CollisionMath_NormalizeOr(translation, CollisionMath_Vector(0.0f, 1.0f, 0.0f));
        Vector3 HitNormal = CollisionMath_NormalizeOr(CollisionMath_Negate(Normal), Fallback);
        Vector3 PointA;
        Vector3 PointB;
        GetSimplexPoints(&Simplex, &PointA, &PointB);
        outHit->Normal = HitNormal;
        outHit->PointA = CollisionMath_AddScaled(CollisionMath_AddScaled(PointA, translation, Lambda), HitNormal,
            a->CoreRadius);
        outHit->PointB = CollisionMath_AddScaled(PointB, HitNormal, -b->CoreRadius);
    }
    outHit->Fraction = Lambda;
    CollisionConvex_FaceOneSided(b, outHit);
    return true;
}
