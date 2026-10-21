#include <float.h>
#include <math.h>
#include <stdint.h>
#include "CollisionShape.h"
#include "CollisionMath.h"
#include "GameModel.h"
#include "wr/WRMemory.h"

/*
 * Convex hulls (include/CollisionShape.h): quickhull in double precision, then the triangles merged into polygon faces
 * where they are coplanar, and the result compacted into one allocation.
 *
 * QUICKHULL. A tetrahedron from the extreme points starts the hull; every other point goes into the outside set of the
 * face it lies farthest above. Then, while a face has outside points: its farthest point (the eye) is added: the faces
 * it sees (above them by more than the tolerance) go, their boundary (the horizon, one cycle of edges) is joined to the
 * eye by new faces, and the orphaned outside points go to the new faces they lie above (the rest are inside). An eye
 * whose visible faces do not have one clean horizon (numerical noise on near-coplanar input) is dropped as inside: it
 * lies within the tolerance of the hull.
 *
 * TOLERANCE. Points within 3 x DBL_EPSILON x (the sum of the largest coordinates per axis) of a face count as on it
 * (John Lloyd's QuickHull3D bound), so exact input (a model's cube) gives exact faces.
 *
 * FACES. Adjacent triangles whose normals agree within COPLANAR_COSINE and whose vertices lie within the merge distance
 * of the group's plane become one polygon face (a cube has 6 faces, not 12): the contact manifolds clip against whole
 * faces.
 */


// Macros.
/** A face index or point index that names nothing. */
#define NONE UINT32_MAX
/** The tolerance's factor on the coordinates' size (QuickHull3D). */
#define TOLERANCE_FACTOR (3.0 * DBL_EPSILON)
/** Two triangles merge into one face when their normals' cosine is above this (about 0.06 degrees apart). */
#define COPLANAR_COSINE 0.9999995
/** ...and their vertices lie within this share of the hull's size of the group's plane. */
#define COPLANAR_DISTANCE_SHARE 1e-6


// Types.
typedef struct HullFaceStruct
{
    uint32_t Vertices[3];  // point indices, counter-clockwise seen from outside
    uint32_t Neighbors[3]; // the face across edge i (Vertices[i] -> Vertices[(i + 1) % 3])
    double Normal[3];
    double Offset;
    uint32_t OutsideHead;  // the first point of its outside set, NONE when empty
    bool IsAlive;
    bool IsVisible;        // scratch for one step
} HullFace;

typedef struct HorizonEdgeStruct
{
    uint32_t From;
    uint32_t To;
    uint32_t OuterFace;    // the face that stays, across the edge
    uint32_t Next;         // the next edge of the cycle (an index into the horizon)
} HorizonEdge;

typedef struct HullBuilderStruct
{
    const double* Points;  // three per point
    uint32_t PointCount;
    double Tolerance;
    GenericBuffer Faces;   // HullFace
    uint32_t* NextOutside; // per point: the next point of the same outside set
    GenericBuffer Visible; // uint32_t face indices
    GenericBuffer Horizon; // HorizonEdge
    GenericBuffer Orphans; // uint32_t point indices
    GenericBuffer Pending; // uint32_t faces that may have outside points
} HullBuilder;


// Static functions: vectors in double.
static void Sub3(const double* a, const double* b, double* out)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static double Dot3(const double* a, const double* b)
{
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

static void Cross3(const double* a, const double* b, double* out)
{
    double X = (a[1] * b[2]) - (a[2] * b[1]);
    double Y = (a[2] * b[0]) - (a[0] * b[2]);
    double Z = (a[0] * b[1]) - (a[1] * b[0]);
    out[0] = X;
    out[1] = Y;
    out[2] = Z;
}

static double Length3(const double* a)
{
    return sqrt(Dot3(a, a));
}

static const double* PointAt(const HullBuilder* builder, uint32_t index)
{
    return &builder->Points[(size_t)index * 3U];
}

static HullFace* FaceAt(HullBuilder* builder, uint32_t index)
{
    return GenericBuffer_GetPointerToElement(&builder->Faces, index);
}

static double DistanceAbove(const HullBuilder* builder, const HullFace* face, uint32_t point)
{
    return Dot3(face->Normal, PointAt(builder, point)) - face->Offset;
}


// Static functions: building.
/* Appends the face (a, b, c) with its plane; neighbours unset. */
static uint32_t AddFace(HullBuilder* builder, uint32_t a, uint32_t b, uint32_t c)
{
    HullFace Face;
    Memory_Zero(&Face, sizeof(Face));
    Face.Vertices[0] = a;
    Face.Vertices[1] = b;
    Face.Vertices[2] = c;
    Face.Neighbors[0] = NONE;
    Face.Neighbors[1] = NONE;
    Face.Neighbors[2] = NONE;
    double Ab[3];
    double Ac[3];
    Sub3(PointAt(builder, b), PointAt(builder, a), Ab);
    Sub3(PointAt(builder, c), PointAt(builder, a), Ac);
    Cross3(Ab, Ac, Face.Normal);
    double Length = Length3(Face.Normal);
    if (Length > 0.0)
    {
        Face.Normal[0] /= Length;
        Face.Normal[1] /= Length;
        Face.Normal[2] /= Length;
    }
    Face.Offset = Dot3(Face.Normal, PointAt(builder, a));
    Face.OutsideHead = NONE;
    Face.IsAlive = true;
    GenericBuffer_AddLast(&builder->Faces, &Face);
    return (uint32_t)(builder->Faces._count - 1U);
}

static void AddOutside(HullBuilder* builder, uint32_t faceIndex, uint32_t point)
{
    HullFace* Face = FaceAt(builder, faceIndex);
    builder->NextOutside[point] = Face->OutsideHead;
    Face->OutsideHead = point;
}

/* Assigns @p point to the face of @p faces (indices) it lies farthest above; false when it lies above none. */
static bool AssignToFaces(HullBuilder* builder, const uint32_t* faces, size_t faceCount, uint32_t point)
{
    double Best = builder->Tolerance;
    uint32_t BestFace = NONE;
    for (size_t Index = 0U; Index < faceCount; Index++)
    {
        double Distance = DistanceAbove(builder, FaceAt(builder, faces[Index]), point);
        if (Distance > Best)
        {
            Best = Distance;
            BestFace = faces[Index];
        }
    }
    if (BestFace == NONE)
    {
        return false;
    }
    AddOutside(builder, BestFace, point);
    return true;
}

/* The tolerance and the four points of the starting tetrahedron; false when the points span no volume. */
static bool FindTetrahedron(HullBuilder* builder, uint32_t outVertices[4])
{
    uint32_t MinIndex[3] = { 0U, 0U, 0U };
    uint32_t MaxIndex[3] = { 0U, 0U, 0U };
    double Largest[3] = { 0.0, 0.0, 0.0 };
    for (uint32_t Point = 0U; Point < builder->PointCount; Point++)
    {
        const double* P = PointAt(builder, Point);
        for (size_t Axis = 0U; Axis < 3U; Axis++)
        {
            MinIndex[Axis] = (P[Axis] < PointAt(builder, MinIndex[Axis])[Axis]) ? Point : MinIndex[Axis];
            MaxIndex[Axis] = (P[Axis] > PointAt(builder, MaxIndex[Axis])[Axis]) ? Point : MaxIndex[Axis];
            Largest[Axis] = fmax(Largest[Axis], fabs(P[Axis]));
        }
    }
    builder->Tolerance = TOLERANCE_FACTOR * (Largest[0] + Largest[1] + Largest[2]);

    size_t Axis = 0U;
    double Spread = -1.0;
    for (size_t Candidate = 0U; Candidate < 3U; Candidate++)
    {
        double Extent = PointAt(builder, MaxIndex[Candidate])[Candidate] - PointAt(builder, MinIndex[Candidate])[Candidate];
        if (Extent > Spread)
        {
            Spread = Extent;
            Axis = Candidate;
        }
    }
    if (!(Spread > builder->Tolerance))
    {
        return false;
    }
    uint32_t A = MinIndex[Axis];
    uint32_t B = MaxIndex[Axis];

    // The point farthest from the line AB.
    double Direction[3];
    Sub3(PointAt(builder, B), PointAt(builder, A), Direction);
    double Best = 0.0;
    uint32_t C = NONE;
    for (uint32_t Point = 0U; Point < builder->PointCount; Point++)
    {
        double Offset[3];
        double Cross[3];
        Sub3(PointAt(builder, Point), PointAt(builder, A), Offset);
        Cross3(Direction, Offset, Cross);
        double Distance = Length3(Cross) / Length3(Direction);
        if (Distance > Best)
        {
            Best = Distance;
            C = Point;
        }
    }
    if ((C == NONE) || !(Best > builder->Tolerance))
    {
        return false;
    }

    // The point farthest from the plane ABC.
    double Ac[3];
    double Normal[3];
    Sub3(PointAt(builder, C), PointAt(builder, A), Ac);
    Cross3(Direction, Ac, Normal);
    double NormalLength = Length3(Normal);
    Best = 0.0;
    uint32_t D = NONE;
    for (uint32_t Point = 0U; Point < builder->PointCount; Point++)
    {
        double Offset[3];
        Sub3(PointAt(builder, Point), PointAt(builder, A), Offset);
        double Distance = fabs(Dot3(Normal, Offset)) / NormalLength;
        if (Distance > Best)
        {
            Best = Distance;
            D = Point;
        }
    }
    if ((D == NONE) || !(Best > builder->Tolerance))
    {
        return false;
    }
    outVertices[0] = A;
    outVertices[1] = B;
    outVertices[2] = C;
    outVertices[3] = D;
    return true;
}

/* The edge of @p face from @p from to @p to, or NONE. */
static uint32_t FindEdge(const HullFace* face, uint32_t from, uint32_t to)
{
    for (uint32_t Edge = 0U; Edge < 3U; Edge++)
    {
        if ((face->Vertices[Edge] == from) && (face->Vertices[(Edge + 1U) % 3U] == to))
        {
            return Edge;
        }
    }
    return NONE;
}

/* The starting tetrahedron: four outward faces, linked, with every other point in an outside set. */
static void BuildTetrahedron(HullBuilder* builder, const uint32_t vertices[4])
{
    static const uint32_t FaceCorners[4][3] = { { 0U, 1U, 2U }, { 0U, 3U, 1U }, { 1U, 3U, 2U }, { 2U, 3U, 0U } };
    double Centroid[3] = { 0.0, 0.0, 0.0 };
    for (size_t Corner = 0U; Corner < 4U; Corner++)
    {
        const double* P = PointAt(builder, vertices[Corner]);
        Centroid[0] += P[0] * 0.25;
        Centroid[1] += P[1] * 0.25;
        Centroid[2] += P[2] * 0.25;
    }
    uint32_t Faces[4];
    for (size_t Face = 0U; Face < 4U; Face++)
    {
        uint32_t A = vertices[FaceCorners[Face][0]];
        uint32_t B = vertices[FaceCorners[Face][1]];
        uint32_t C = vertices[FaceCorners[Face][2]];
        Faces[Face] = AddFace(builder, A, B, C);
        HullFace* Built = FaceAt(builder, Faces[Face]);
        if ((Dot3(Built->Normal, Centroid) - Built->Offset) > 0.0)
        {
            // Wound inwards: swap two corners, which turns the normal round.
            Built->Vertices[1] = C;
            Built->Vertices[2] = B;
            Built->Normal[0] = -Built->Normal[0];
            Built->Normal[1] = -Built->Normal[1];
            Built->Normal[2] = -Built->Normal[2];
            Built->Offset = -Built->Offset;
        }
    }
    for (size_t Face = 0U; Face < 4U; Face++)
    {
        HullFace* Built = FaceAt(builder, Faces[Face]);
        for (uint32_t Edge = 0U; Edge < 3U; Edge++)
        {
            uint32_t From = Built->Vertices[Edge];
            uint32_t To = Built->Vertices[(Edge + 1U) % 3U];
            for (size_t Other = 0U; Other < 4U; Other++)
            {
                if ((Other != Face) && (FindEdge(FaceAt(builder, Faces[Other]), To, From) != NONE))
                {
                    Built->Neighbors[Edge] = Faces[Other];
                }
            }
        }
    }
    for (uint32_t Point = 0U; Point < builder->PointCount; Point++)
    {
        if ((Point != vertices[0]) && (Point != vertices[1]) && (Point != vertices[2]) && (Point != vertices[3]))
        {
            AssignToFaces(builder, Faces, 4U, Point);
        }
    }
    for (size_t Face = 0U; Face < 4U; Face++)
    {
        GenericBuffer_AddLast(&builder->Pending, &Faces[Face]);
    }
}

/* Takes the farthest point out of a face's outside set. */
static uint32_t TakeEye(HullBuilder* builder, uint32_t faceIndex)
{
    HullFace* Face = FaceAt(builder, faceIndex);
    uint32_t Best = NONE;
    uint32_t BestPrevious = NONE;
    double BestDistance = -DBL_MAX;
    uint32_t Previous = NONE;
    for (uint32_t Point = Face->OutsideHead; Point != NONE; Point = builder->NextOutside[Point])
    {
        double Distance = DistanceAbove(builder, Face, Point);
        if (Distance > BestDistance)
        {
            BestDistance = Distance;
            Best = Point;
            BestPrevious = Previous;
        }
        Previous = Point;
    }
    if (BestPrevious == NONE)
    {
        Face->OutsideHead = builder->NextOutside[Best];
    }
    else
    {
        builder->NextOutside[BestPrevious] = builder->NextOutside[Best];
    }
    return Best;
}

/* Marks the faces the eye sees, flooding from @p start. */
static void FindVisibleFaces(HullBuilder* builder, uint32_t start, uint32_t eye)
{
    GenericBuffer_Clear(&builder->Visible);
    FaceAt(builder, start)->IsVisible = true;
    GenericBuffer_AddLast(&builder->Visible, &start);
    for (size_t Index = 0U; Index < builder->Visible._count; Index++)
    {
        uint32_t FaceIndex = ((const uint32_t*)(void*)builder->Visible._data)[Index];
        for (uint32_t Edge = 0U; Edge < 3U; Edge++)
        {
            uint32_t NeighborIndex = FaceAt(builder, FaceIndex)->Neighbors[Edge];
            HullFace* Neighbor = FaceAt(builder, NeighborIndex);
            if (!Neighbor->IsVisible && (DistanceAbove(builder, Neighbor, eye) > builder->Tolerance))
            {
                Neighbor->IsVisible = true;
                GenericBuffer_AddLast(&builder->Visible, &NeighborIndex);
            }
        }
    }
}

/* The horizon: every edge between a visible and a hidden face, chained into one cycle. False when the edges do not form
 * exactly one cycle (the eye is then dropped). */
static bool FindHorizon(HullBuilder* builder)
{
    GenericBuffer_Clear(&builder->Horizon);
    const uint32_t* Visible = (const uint32_t*)(void*)builder->Visible._data;
    for (size_t Index = 0U; Index < builder->Visible._count; Index++)
    {
        const HullFace* Face = FaceAt(builder, Visible[Index]);
        for (uint32_t Edge = 0U; Edge < 3U; Edge++)
        {
            if (!FaceAt(builder, Face->Neighbors[Edge])->IsVisible)
            {
                HorizonEdge Horizon = { .From = Face->Vertices[Edge], .To = Face->Vertices[(Edge + 1U) % 3U],
                    .OuterFace = Face->Neighbors[Edge], .Next = NONE };
                GenericBuffer_AddLast(&builder->Horizon, &Horizon);
            }
        }
    }
    HorizonEdge* Edges = (HorizonEdge*)(void*)builder->Horizon._data;
    size_t Count = builder->Horizon._count;
    if (Count < 3U)
    {
        return false;
    }
    for (size_t Index = 0U; Index < Count; Index++)
    {
        for (size_t Other = 0U; Other < Count; Other++)
        {
            if (Edges[Other].From == Edges[Index].To)
            {
                if (Edges[Index].Next != NONE)
                {
                    return false; // two edges leave one vertex: not a simple cycle
                }
                Edges[Index].Next = (uint32_t)Other;
            }
        }
        if (Edges[Index].Next == NONE)
        {
            return false;
        }
    }
    // One cycle through every edge.
    size_t Steps = 0U;
    uint32_t Current = 0U;
    do
    {
        Current = Edges[Current].Next;
        Steps++;
    } while ((Current != 0U) && (Steps <= Count));
    return Steps == Count;
}

static void ClearVisible(HullBuilder* builder)
{
    const uint32_t* Visible = (const uint32_t*)(void*)builder->Visible._data;
    for (size_t Index = 0U; Index < builder->Visible._count; Index++)
    {
        FaceAt(builder, Visible[Index])->IsVisible = false;
    }
}

/* Adds the eye: new faces from the horizon to it, the visible faces gone, their outside points given to the new
 * faces. */
static void AddEye(HullBuilder* builder, uint32_t eye)
{
    // The orphans: every outside point of the faces that go.
    GenericBuffer_Clear(&builder->Orphans);
    const uint32_t* Visible = (const uint32_t*)(void*)builder->Visible._data;
    for (size_t Index = 0U; Index < builder->Visible._count; Index++)
    {
        HullFace* Face = FaceAt(builder, Visible[Index]);
        for (uint32_t Point = Face->OutsideHead; Point != NONE; Point = builder->NextOutside[Point])
        {
            GenericBuffer_AddLast(&builder->Orphans, &Point);
        }
        Face->OutsideHead = NONE;
        Face->IsAlive = false;
        Face->IsVisible = false;
    }

    // One new face per horizon edge, in the cycle's order, so each one's neighbours are the faces before and after.
    size_t Count = builder->Horizon._count;
    size_t FirstNew = builder->Faces._count;
    uint32_t Current = 0U;
    for (size_t Step = 0U; Step < Count; Step++)
    {
        const HorizonEdge* Edge = &((const HorizonEdge*)(void*)builder->Horizon._data)[Current];
        uint32_t NewFace = AddFace(builder, Edge->From, Edge->To, eye);
        HullFace* Outer = FaceAt(builder, Edge->OuterFace);
        Outer->Neighbors[FindEdge(Outer, Edge->To, Edge->From)] = NewFace;
        FaceAt(builder, NewFace)->Neighbors[0] = Edge->OuterFace;
        Current = Edge->Next;
    }
    for (size_t Step = 0U; Step < Count; Step++)
    {
        HullFace* Face = FaceAt(builder, (uint32_t)(FirstNew + Step));
        Face->Neighbors[1] = (uint32_t)(FirstNew + ((Step + 1U) % Count));
        Face->Neighbors[2] = (uint32_t)(FirstNew + ((Step + Count - 1U) % Count));
    }

    // The orphans go to the new faces they lie above; the others are inside now.
    for (size_t Index = 0U; Index < builder->Orphans._count; Index++)
    {
        uint32_t Point = ((const uint32_t*)(void*)builder->Orphans._data)[Index];
        double Best = builder->Tolerance;
        uint32_t BestFace = NONE;
        for (size_t Step = 0U; Step < Count; Step++)
        {
            uint32_t FaceIndex = (uint32_t)(FirstNew + Step);
            double Distance = DistanceAbove(builder, FaceAt(builder, FaceIndex), Point);
            if (Distance > Best)
            {
                Best = Distance;
                BestFace = FaceIndex;
            }
        }
        if (BestFace != NONE)
        {
            AddOutside(builder, BestFace, Point);
        }
    }
    for (size_t Step = 0U; Step < Count; Step++)
    {
        uint32_t FaceIndex = (uint32_t)(FirstNew + Step);
        if (FaceAt(builder, FaceIndex)->OutsideHead != NONE)
        {
            GenericBuffer_AddLast(&builder->Pending, &FaceIndex);
        }
    }
}

static void RunQuickhull(HullBuilder* builder)
{
    while (builder->Pending._count > 0U)
    {
        uint32_t FaceIndex = 0U;
        GenericBuffer_GetLast(&builder->Pending, &FaceIndex);
        HullFace* Face = FaceAt(builder, FaceIndex);
        if (!Face->IsAlive || (Face->OutsideHead == NONE))
        {
            GenericBuffer_RemoveLast(&builder->Pending);
            continue;
        }
        uint32_t Eye = TakeEye(builder, FaceIndex);
        FindVisibleFaces(builder, FaceIndex, Eye);
        if (!FindHorizon(builder))
        {
            ClearVisible(builder); // numerical noise: the eye lies within the tolerance of the hull
            continue;
        }
        AddEye(builder, Eye);
    }
}


// Static functions: faces and the result.
typedef struct HullExtractStruct
{
    uint32_t* Group;          // per face: its group's representative (a face index), NONE for dead faces
    uint32_t* VertexRemap;    // per point: its index in the result, NONE when unused
    GenericBuffer FaceLoops;  // uint32_t: each group's boundary loop, loop after loop (point indices)
    GenericBuffer LoopStarts; // uint32_t: per group, its first entry in FaceLoops, then a final end
    GenericBuffer LoopFaces;  // uint32_t: per group, the representative face
} HullExtract;

static uint32_t FindGroup(uint32_t* groups, uint32_t face)
{
    while (groups[face] != face)
    {
        groups[face] = groups[groups[face]];
        face = groups[face];
    }
    return face;
}

/* Merges coplanar neighbours into groups (union-find on the face indices). */
static void GroupCoplanarFaces(HullBuilder* builder, HullExtract* extract, double mergeDistance)
{
    size_t FaceCount = builder->Faces._count;
    for (uint32_t Face = 0U; Face < FaceCount; Face++)
    {
        extract->Group[Face] = FaceAt(builder, Face)->IsAlive ? Face : NONE;
    }
    for (uint32_t Face = 0U; Face < FaceCount; Face++)
    {
        const HullFace* Built = FaceAt(builder, Face);
        if (!Built->IsAlive)
        {
            continue;
        }
        for (uint32_t Edge = 0U; Edge < 3U; Edge++)
        {
            uint32_t Neighbor = Built->Neighbors[Edge];
            uint32_t RootA = FindGroup(extract->Group, Face);
            uint32_t RootB = FindGroup(extract->Group, Neighbor);
            if ((Neighbor < Face) || (RootA == RootB))
            {
                continue;
            }
            const HullFace* Representative = FaceAt(builder, RootA);
            const HullFace* Other = FaceAt(builder, Neighbor);
            if (Dot3(Representative->Normal, Other->Normal) < COPLANAR_COSINE)
            {
                continue;
            }
            bool IsFlat = true;
            for (uint32_t Corner = 0U; Corner < 3U; Corner++)
            {
                IsFlat = IsFlat && (fabs(DistanceAbove(builder, Representative, Other->Vertices[Corner]))
                    <= mergeDistance);
            }
            if (IsFlat)
            {
                extract->Group[RootB] = RootA;
            }
        }
    }
}

static ComparisonResult CompareEdgeStarts(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    uint32_t A = ((const uint32_t*)a._element)[0];
    uint32_t B = ((const uint32_t*)b._element)[0];
    return (A < B) ? ComparisonResult_ALessThanB : ((A > B) ? ComparisonResult_AGreaterThanB : ComparisonResult_AEqualsB);
}

/* The edge leaving @p vertex among @p count edges sorted by their start, or @p count when none does. */
static size_t FindEdgeFrom(const uint32_t (*pairs)[2], size_t count, uint32_t vertex)
{
    size_t Low = 0U;
    size_t High = count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if (pairs[Middle][0] < vertex)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return ((Low < count) && (pairs[Low][0] == vertex)) ? Low : count;
}

/* Each group's boundary as one loop (counter-clockwise from outside). False when a group's boundary is not one loop.
 * The faces are bucketed by group once (scanning every face for every group was quadratic: two minutes for the 65536
 * points a hull may take), and each boundary is chained through its edges sorted by their start. */
static bool BuildFaceLoops(HullBuilder* builder, HullExtract* extract)
{
    uint32_t FaceCount = (uint32_t)builder->Faces._count;
    // One block: each face's group (NONE for a dead face), each group's first member (plus an end), the members by
    // group, and the fill's cursors.
    uint32_t* Roots = Memory_Allocate((((size_t)FaceCount * 4U) + 1U) * sizeof(uint32_t));
    uint32_t* Offsets = Roots + FaceCount;
    uint32_t* Members = Offsets + FaceCount + 1U;
    uint32_t* Cursors = Members + FaceCount;
    Memory_Zero(Offsets, ((size_t)FaceCount + 1U) * sizeof(uint32_t));
    for (uint32_t Face = 0U; Face < FaceCount; Face++)
    {
        Roots[Face] = (extract->Group[Face] == NONE) ? NONE : FindGroup(extract->Group, Face);
        if (Roots[Face] != NONE)
        {
            Offsets[Roots[Face] + 1U]++;
        }
    }
    for (uint32_t Face = 0U; Face < FaceCount; Face++)
    {
        Offsets[Face + 1U] += Offsets[Face];
    }
    Memory_Copy(Offsets, Cursors, (size_t)FaceCount * sizeof(uint32_t));
    for (uint32_t Face = 0U; Face < FaceCount; Face++)
    {
        if (Roots[Face] != NONE)
        {
            Members[Cursors[Roots[Face]]++] = Face;
        }
    }

    GenericBuffer Edges; // pairs (from, to) of the current group's boundary
    GenericBuffer_AllocateVariable(&Edges, 32U, sizeof(uint32_t) * 2U);
    bool IsValid = true;
    for (uint32_t Root = 0U; (Root < FaceCount) && IsValid; Root++)
    {
        if (Roots[Root] != Root)
        {
            continue; // dead, or not its group's representative
        }
        GenericBuffer_Clear(&Edges);
        for (uint32_t Member = Offsets[Root]; Member < Offsets[Root + 1U]; Member++)
        {
            const HullFace* Built = FaceAt(builder, Members[Member]);
            for (uint32_t Edge = 0U; Edge < 3U; Edge++)
            {
                if (Roots[Built->Neighbors[Edge]] != Root)
                {
                    uint32_t Pair[2] = { Built->Vertices[Edge], Built->Vertices[(Edge + 1U) % 3U] };
                    GenericBuffer_AddLast(&Edges, Pair);
                }
            }
        }
        uint32_t Scratch[4];
        GenericBuffer_SortAscending(&Edges, CompareEdgeStarts, NULL, Scratch);
        const uint32_t (*Pairs)[2] = (const uint32_t (*)[2])(void*)Edges._data;
        size_t EdgeCount = Edges._count;
        IsValid = EdgeCount >= 3U;
        for (size_t Edge = 1U; (Edge < EdgeCount) && IsValid; Edge++)
        {
            IsValid = Pairs[Edge][0] != Pairs[Edge - 1U][0]; // two edges leaving one vertex: not one simple loop
        }
        if (!IsValid)
        {
            break;
        }
        // Chain the boundary edges from the first one.
        uint32_t Start = (uint32_t)extract->FaceLoops._count;
        GenericBuffer_AddLast(&extract->LoopStarts, &Start);
        GenericBuffer_AddLast(&extract->LoopFaces, &Root);
        uint32_t First = Pairs[0][0];
        uint32_t Current = First;
        for (size_t Step = 0U; Step < EdgeCount; Step++)
        {
            GenericBuffer_AddLast(&extract->FaceLoops, &Current);
            size_t Found = FindEdgeFrom(Pairs, EdgeCount, Current);
            if (Found >= EdgeCount)
            {
                IsValid = false; // no edge leaves the vertex
                break;
            }
            Current = Pairs[Found][1];
            if ((Current == First) && ((Step + 1U) < EdgeCount))
            {
                IsValid = false; // back at the start early: the boundary is more than one loop
                break;
            }
        }
        IsValid = IsValid && (Current == First);
    }
    uint32_t End = (uint32_t)extract->FaceLoops._count;
    GenericBuffer_AddLast(&extract->LoopStarts, &End);
    Memory_Free(Edges._data);
    Memory_Free(Roots);
    return IsValid;
}

/* Writes the result block: the struct, then the vertices, faces, face vertex indices and edges. */
static Error WriteHull(HullBuilder* builder, HullExtract* extract, CollisionHull** outHull)
{
    // The vertices the faces use, in point order.
    uint32_t VertexCount = 0U;
    const uint32_t* Loops = (const uint32_t*)(void*)extract->FaceLoops._data;
    size_t LoopEntryCount = extract->FaceLoops._count;
    for (uint32_t Point = 0U; Point < builder->PointCount; Point++)
    {
        extract->VertexRemap[Point] = NONE;
    }
    for (size_t Entry = 0U; Entry < LoopEntryCount; Entry++)
    {
        if (extract->VertexRemap[Loops[Entry]] == NONE)
        {
            extract->VertexRemap[Loops[Entry]] = 0U;
        }
    }
    for (uint32_t Point = 0U; Point < builder->PointCount; Point++)
    {
        if (extract->VertexRemap[Point] != NONE)
        {
            extract->VertexRemap[Point] = VertexCount++;
        }
    }
    uint32_t FaceCount = (uint32_t)extract->LoopFaces._count;
    uint32_t EdgeCount = (uint32_t)(LoopEntryCount / 2U); // each edge borders two faces

    size_t Offset = sizeof(CollisionHull);
    size_t VerticesOffset = Offset;
    Offset += (size_t)VertexCount * sizeof(Vector3);
    size_t FacesOffset = Offset;
    Offset += (size_t)FaceCount * sizeof(CollisionHullFace);
    size_t IndicesOffset = Offset;
    Offset += LoopEntryCount * sizeof(uint32_t);
    size_t EdgesOffset = Offset;
    Offset += (size_t)EdgeCount * 2U * sizeof(uint32_t);

    unsigned char* Block = Memory_Allocate(Offset);
    Memory_Zero(Block, Offset);
    CollisionHull* Hull = (CollisionHull*)(void*)Block;
    Vector3* Vertices = (Vector3*)(void*)(Block + VerticesOffset);
    CollisionHullFace* Faces = (CollisionHullFace*)(void*)(Block + FacesOffset);
    uint32_t* Indices = (uint32_t*)(void*)(Block + IndicesOffset);
    uint32_t* EdgeIndices = (uint32_t*)(void*)(Block + EdgesOffset);

    Vector3 Min = CollisionMath_Vector(FLT_MAX, FLT_MAX, FLT_MAX);
    Vector3 Max = CollisionMath_Vector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    Vector3 Sum = CollisionMath_Vector(0.0f, 0.0f, 0.0f);
    for (uint32_t Point = 0U; Point < builder->PointCount; Point++)
    {
        uint32_t Target = extract->VertexRemap[Point];
        if (Target == NONE)
        {
            continue;
        }
        const double* P = PointAt(builder, Point);
        Vector3 Vertex = CollisionMath_Vector((float)P[0], (float)P[1], (float)P[2]);
        Vertices[Target] = Vertex;
        Min = CollisionMath_Min(Min, Vertex);
        Max = CollisionMath_Max(Max, Vertex);
        Sum = CollisionMath_Add(Sum, Vertex);
    }

    const uint32_t* Starts = (const uint32_t*)(void*)extract->LoopStarts._data;
    const uint32_t* Representatives = (const uint32_t*)(void*)extract->LoopFaces._data;
    uint32_t EdgeWritten = 0U;
    for (uint32_t Face = 0U; Face < FaceCount; Face++)
    {
        const HullFace* Representative = FaceAt(builder, Representatives[Face]);
        Vector3 Normal = CollisionMath_Vector((float)Representative->Normal[0], (float)Representative->Normal[1],
            (float)Representative->Normal[2]);
        float PlaneOffset = -FLT_MAX;
        for (uint32_t Entry = Starts[Face]; Entry < Starts[Face + 1U]; Entry++)
        {
            uint32_t Vertex = extract->VertexRemap[Loops[Entry]];
            Indices[Entry] = Vertex;
            PlaneOffset = fmaxf(PlaneOffset, CollisionMath_Dot(Normal, Vertices[Vertex]));
            uint32_t NextEntry = ((Entry + 1U) < Starts[Face + 1U]) ? (Entry + 1U) : Starts[Face];
            uint32_t NextVertex = extract->VertexRemap[Loops[NextEntry]];
            if ((Vertex < NextVertex) && (EdgeWritten < EdgeCount))
            {
                EdgeIndices[(EdgeWritten * 2U)] = Vertex;
                EdgeIndices[(EdgeWritten * 2U) + 1U] = NextVertex;
                EdgeWritten++;
            }
        }
        // The plane through the face's outermost vertex, so every vertex lies on it or below.
        Faces[Face] = (CollisionHullFace){ ._normal = Normal, ._offset = PlaneOffset, ._firstIndex = Starts[Face],
            ._vertexCount = Starts[Face + 1U] - Starts[Face] };
    }

    Hull->_vertices = Vertices;
    Hull->_vertexCount = VertexCount;
    Hull->_faces = Faces;
    Hull->_faceCount = FaceCount;
    Hull->_faceVertexIndices = Indices;
    Hull->_faceVertexIndexCount = (uint32_t)LoopEntryCount;
    Hull->_edgeVertexIndices = EdgeIndices;
    Hull->_edgeCount = EdgeWritten;
    Hull->_boundsMin = Min;
    Hull->_boundsMax = Max;
    Hull->_centroid = CollisionMath_Scale(Sum, 1.0f / (float)VertexCount);
    *outHull = Hull;
    return Error_CreateSuccess();
}

/* Groups the faces and writes the result; if a group's boundary is not one loop, falls back to triangle faces. */
static Error ExtractHull(HullBuilder* builder, CollisionHull** outHull)
{
    size_t FaceCount = builder->Faces._count;
    HullExtract Extract;
    Extract.Group = Memory_Allocate(FaceCount * sizeof(uint32_t));
    Extract.VertexRemap = Memory_Allocate((size_t)builder->PointCount * sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Extract.FaceLoops, FaceCount * 3U, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Extract.LoopStarts, FaceCount + 1U, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Extract.LoopFaces, FaceCount, sizeof(uint32_t));

    double Size = 0.0;
    for (size_t Face = 0U; Face < FaceCount; Face++)
    {
        const HullFace* Built = FaceAt(builder, (uint32_t)Face);
        if (Built->IsAlive)
        {
            Size = fmax(Size, fabs(Built->Offset));
        }
    }
    double MergeDistance = fmax(builder->Tolerance, COPLANAR_DISTANCE_SHARE * Size);

    GroupCoplanarFaces(builder, &Extract, MergeDistance);
    if (!BuildFaceLoops(builder, &Extract))
    {
        // Every triangle its own face (always one loop each).
        GenericBuffer_Clear(&Extract.FaceLoops);
        GenericBuffer_Clear(&Extract.LoopStarts);
        GenericBuffer_Clear(&Extract.LoopFaces);
        for (uint32_t Face = 0U; Face < FaceCount; Face++)
        {
            Extract.Group[Face] = FaceAt(builder, Face)->IsAlive ? Face : NONE;
        }
        BuildFaceLoops(builder, &Extract);
    }
    Error Result = WriteHull(builder, &Extract, outHull);

    Memory_Free(Extract.Group);
    Memory_Free(Extract.VertexRemap);
    Memory_Free(Extract.FaceLoops._data);
    Memory_Free(Extract.LoopStarts._data);
    Memory_Free(Extract.LoopFaces._data);
    return Result;
}

/* Builds the hull of points given in double (three per point), with no vertex limit. */
static Error BuildHull(const double* points, uint32_t count, CollisionHull** outHull)
{
    HullBuilder Builder;
    Memory_Zero(&Builder, sizeof(Builder));
    Builder.Points = points;
    Builder.PointCount = count;
    uint32_t Tetrahedron[4];
    if (!FindTetrahedron(&Builder, Tetrahedron))
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "A convex hull needs points that span a volume; these lie on a plane, a line or a point.");
    }
    GenericBuffer_AllocateVariable(&Builder.Faces, (size_t)count * 2U, sizeof(HullFace));
    GenericBuffer_AllocateVariable(&Builder.Visible, 32U, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Builder.Horizon, 32U, sizeof(HorizonEdge));
    GenericBuffer_AllocateVariable(&Builder.Orphans, 64U, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Builder.Pending, 64U, sizeof(uint32_t));
    Builder.NextOutside = Memory_Allocate((size_t)count * sizeof(uint32_t));
    for (uint32_t Point = 0U; Point < count; Point++)
    {
        Builder.NextOutside[Point] = NONE;
    }

    BuildTetrahedron(&Builder, Tetrahedron);
    RunQuickhull(&Builder);
    Error Result = ExtractHull(&Builder, outHull);

    Memory_Free(Builder.Faces._data);
    Memory_Free(Builder.Visible._data);
    Memory_Free(Builder.Horizon._data);
    Memory_Free(Builder.Orphans._data);
    Memory_Free(Builder.Pending._data);
    Memory_Free(Builder.NextOutside);
    return Result;
}

/* Adds the full hull's vertex farthest along @p axis to the @p chosen picks in @p points, unless it is one already. */
static void PickFarthestVertex(const CollisionHull* full, Vector3 axis, bool* isChosen, double* points,
    uint32_t* chosen)
{
    uint32_t Best = 0U;
    float BestDot = -FLT_MAX;
    for (uint32_t Vertex = 0U; Vertex < full->_vertexCount; Vertex++)
    {
        float Dot = CollisionMath_Dot(full->_vertices[Vertex], axis);
        if (Dot > BestDot)
        {
            BestDot = Dot;
            Best = Vertex;
        }
    }
    if (!isChosen[Best])
    {
        isChosen[Best] = true;
        points[(*chosen * 3U)] = full->_vertices[Best].x;
        points[(*chosen * 3U) + 1U] = full->_vertices[Best].y;
        points[(*chosen * 3U) + 2U] = full->_vertices[Best].z;
        (*chosen)++;
    }
}

/* A hull with at most @p maxVertices vertices: the hull's extremes along its six axis directions first, then its
 * vertices farthest along directions spread over the whole sphere and scaled by the inverse of the hull's half extents
 * (which spreads the picks evenly over a thin hull: plain directions pick its rim and lose its thickness), and the hull
 * of those. The first pass has exactly as many directions as picks are left, so it always covers the whole sphere;
 * denser passes, turned, fill what repeated picks left. */
static Error ReduceHull(const CollisionHull* full, uint32_t maxVertices, CollisionHull** outHull)
{
    double* Points = Memory_Allocate((size_t)maxVertices * 3U * sizeof(double));
    bool* IsChosen = Memory_Allocate((size_t)full->_vertexCount * sizeof(bool));
    Memory_Zero(IsChosen, (size_t)full->_vertexCount * sizeof(bool));
    uint32_t Chosen = 0U;
    Vector3 Half = CollisionMath_Scale(CollisionMath_Subtract(full->_boundsMax, full->_boundsMin), 0.5f);
    float Floor = 1e-6f * fmaxf(Half.x, fmaxf(Half.y, Half.z));
    Vector3 Inverse = CollisionMath_Vector(1.0f / fmaxf(Half.x, Floor), 1.0f / fmaxf(Half.y, Floor),
        1.0f / fmaxf(Half.z, Floor));
    const float GoldenAngle = PI * (3.0f - sqrtf(5.0f));
    for (uint32_t Extreme = 0U; (Extreme < 6U) && (Chosen < maxVertices); Extreme++)
    {
        float Sign = ((Extreme & 1U) != 0U) ? -1.0f : 1.0f;
        Vector3 Axis = CollisionMath_Vector(((Extreme / 2U) == 0U) ? Sign : 0.0f, ((Extreme / 2U) == 1U) ? Sign : 0.0f,
            ((Extreme / 2U) == 2U) ? Sign : 0.0f);
        PickFarthestVertex(full, Axis, IsChosen, Points, &Chosen);
    }
    uint32_t DirectionCount = maxVertices - Chosen;
    float Twist = 0.0f;
    for (uint32_t Pass = 0U; (Pass < 4U) && (Chosen < maxVertices); Pass++)
    {
        for (uint32_t Direction = 0U; (Direction < DirectionCount) && (Chosen < maxVertices); Direction++)
        {
            float Y = 1.0f - ((2.0f * ((float)Direction + 0.5f)) / (float)DirectionCount);
            float Ring = sqrtf(fmaxf(0.0f, 1.0f - (Y * Y)));
            float Angle = (GoldenAngle * (float)Direction) + Twist;
            Vector3 Axis = CollisionMath_Vector(Ring * cosf(Angle) * Inverse.x, Y * Inverse.y,
                Ring * sinf(Angle) * Inverse.z);
            PickFarthestVertex(full, Axis, IsChosen, Points, &Chosen);
        }
        DirectionCount *= 2U;
        Twist += GoldenAngle * 0.25f;
    }
    Error Result = BuildHull(Points, Chosen, outHull);
    Memory_Free(Points);
    Memory_Free(IsChosen);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct3(ErrorCode_InvalidOperation,
            u8"A convex hull of %u vertices could not be reduced to %u; allow more vertices.", full->_vertexCount,
            maxVertices);
    }
    return Result;
}


// Public functions.
Error CollisionHull_Create(const Vector3* points, size_t count, uint32_t maxVertices, CollisionHull** outHull)
{
    *outHull = NULL;
    if ((count < 4U) || (count > COLLISION_HULL_MAX_INPUT_POINTS))
    {
        return Error_Construct3(ErrorCode_ArgumentOutOfRange,
            u8"A convex hull needs 4 to %u points; %llu were given.", (unsigned)COLLISION_HULL_MAX_INPUT_POINTS,
            (unsigned long long)count);
    }
    if ((maxVertices < COLLISION_HULL_MIN_VERTICES) || (maxVertices > COLLISION_HULL_MAX_VERTICES))
    {
        return Error_Construct3(ErrorCode_ArgumentOutOfRange, u8"A convex hull's max_vertices must be %u to %u.",
            COLLISION_HULL_MIN_VERTICES, COLLISION_HULL_MAX_VERTICES);
    }
    double* Points = Memory_Allocate(count * 3U * sizeof(double));
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (!CollisionMath_IsFinite(points[Index]))
        {
            Memory_Free(Points);
            return Error_Construct2(ErrorCode_ArgumentOutOfRange, "A convex hull's points must be finite.");
        }
        Points[(Index * 3U)] = points[Index].x;
        Points[(Index * 3U) + 1U] = points[Index].y;
        Points[(Index * 3U) + 2U] = points[Index].z;
    }
    CollisionHull* Full = NULL;
    Error Result = BuildHull(Points, (uint32_t)count, &Full);
    Memory_Free(Points);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Full->_vertexCount <= maxVertices)
    {
        *outHull = Full;
        return Result;
    }
    Result = ReduceHull(Full, maxVertices, outHull);
    CollisionHull_Destroy(Full);
    return Result;
}

Error CollisionHull_CreateFromModel(const GameModel* model, uint32_t maxVertices, CollisionHull** outHull)
{
    *outHull = NULL;
    const Model* RayModel = &model->_rayModel;
    size_t Count = 0U;
    for (int Part = 0; Part < RayModel->meshCount; Part++)
    {
        if ((RayModel->meshes[Part].vertices != NULL) && (RayModel->meshes[Part].vertexCount > 0))
        {
            Count += (size_t)RayModel->meshes[Part].vertexCount;
        }
    }
    if ((Count == 0U) || (Count > COLLISION_HULL_MAX_INPUT_POINTS))
    {
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"A convex hull built from a model needs 4 to %u vertices; the model has %llu.",
            (unsigned)COLLISION_HULL_MAX_INPUT_POINTS, (unsigned long long)Count);
    }
    Vector3* Points = Memory_Allocate(Count * sizeof(Vector3));
    size_t Written = 0U;
    for (int Part = 0; Part < RayModel->meshCount; Part++)
    {
        const Mesh* Source = &RayModel->meshes[Part];
        if (Source->vertices == NULL)
        {
            continue;
        }
        for (int Vertex = 0; Vertex < Source->vertexCount; Vertex++)
        {
            const float* P = &Source->vertices[(size_t)Vertex * 3U];
            Points[Written++] = Vector3Transform(CollisionMath_Vector(P[0], P[1], P[2]), RayModel->transform);
        }
    }
    Error Result = CollisionHull_Create(Points, Written, maxVertices, outHull);
    Memory_Free(Points);
    return Result;
}

void CollisionHull_Destroy(CollisionHull* self)
{
    Memory_Free(self);
}
