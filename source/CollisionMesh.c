#include <float.h>
#include <math.h>
#include <stdint.h>
#include "CollisionShape.h"
#include "CollisionMath.h"
#include "GameModel.h"
#include "wr/WRMemory.h"

/*
 * Collision meshes (include/CollisionShape.h): the parts' triangles transformed, their vertices welded (equal
 * positions become one vertex, so neighbouring triangles share edges), degenerate triangles skipped, each edge marked
 * active or not, and a bounding volume hierarchy over the triangles, all in one allocation.
 *
 * ACTIVE EDGES. A shape sliding over a mesh touches the edges between triangles; taking such an edge's own normal would
 * make it catch on a flat floor's seams (a "ghost" collision). An edge shared by two triangles is inactive when the two
 * lie flat (normals within FLAT_EDGE_COSINE) or fold concave; contacts on inactive edges then take the face normal.
 * Border edges, convex folds and edges shared by more than two triangles stay active.
 *
 * BVH. Median splits along the longest axis of the triangles' centres, at most LEAF_TRIANGLES per leaf, depth-first
 * with the first child next to its parent, built with an explicit stack (no recursion).
 */


// Macros.
/** A welded vertex or triangle that names nothing. */
#define NONE UINT32_MAX
/** The most triangles a BVH leaf holds. */
#define LEAF_TRIANGLES 4U
/** Two triangles lie flat across their edge when their normals' cosine is above this (5 degrees apart). */
#define FLAT_EDGE_COSINE 0.9961947f
/** A triangle is degenerate when the sine of its angle at the first corner is below this. */
#define DEGENERATE_SINE 1e-6f
/** The deepest a BVH can be: a median split halves the triangles, so log2 of the most triangles plus room. */
#define MAX_BVH_DEPTH 64U


// Types.
typedef struct WeldEntryStruct
{
    uint32_t Bits[3];
    uint32_t Index;
} WeldEntry;

typedef struct BuildTriangleStruct
{
    uint32_t Indices[3];
    uint32_t Part;
    uint32_t SourceIndex;
    Vector3 Centroid;
    Vector3 Min;
    Vector3 Max;
} BuildTriangle;

typedef struct EdgeEntryStruct
{
    uint32_t Low;      // the lower vertex index (NONE: empty slot)
    uint32_t High;
    uint32_t Triangle; // the first triangle with the edge
    uint32_t Edge;     // its edge slot there
    uint32_t Count;    // how many triangles share it
    uint32_t SecondTriangle;
    uint32_t SecondEdge;
} EdgeEntry;

typedef struct BuildRangeStruct
{
    uint32_t Node;
    uint32_t First;
    uint32_t Count;
} BuildRange;


// Static functions: welding.
static uint32_t GetFloatBits(float value)
{
    // +0 and -0 are one position.
    if (value == 0.0f)
    {
        value = 0.0f;
    }
    uint32_t Bits = 0U;
    Memory_Copy(&value, &Bits, sizeof(Bits));
    return Bits;
}

static uint32_t HashBits(const uint32_t bits[3])
{
    uint32_t Hash = 2166136261U;
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Hash = (Hash ^ bits[Index]) * 16777619U;
        Hash ^= Hash >> 15U;
    }
    return Hash;
}

static size_t GetTableCapacity(size_t count)
{
    size_t Capacity = 16U;
    while (Capacity < (count * 2U))
    {
        Capacity *= 2U;
    }
    return Capacity;
}

/* The welded index of a position, adding it when new. */
static uint32_t Weld(WeldEntry* table, size_t capacity, Vector3* vertices, uint32_t* vertexCount, Vector3 position)
{
    uint32_t Bits[3] = { GetFloatBits(position.x), GetFloatBits(position.y), GetFloatBits(position.z) };
    size_t Slot = HashBits(Bits) & (capacity - 1U);
    while (table[Slot].Index != NONE)
    {
        if ((table[Slot].Bits[0] == Bits[0]) && (table[Slot].Bits[1] == Bits[1]) && (table[Slot].Bits[2] == Bits[2]))
        {
            return table[Slot].Index;
        }
        Slot = (Slot + 1U) & (capacity - 1U);
    }
    uint32_t Index = (*vertexCount)++;
    table[Slot] = (WeldEntry){ .Bits = { Bits[0], Bits[1], Bits[2] }, .Index = Index };
    vertices[Index] = (Vector3){ .x = Bits[0] == 0U ? 0.0f : position.x, .y = Bits[1] == 0U ? 0.0f : position.y,
        .z = Bits[2] == 0U ? 0.0f : position.z };
    return Index;
}


// Static functions: triangles.
static bool IsDegenerate(Vector3 a, Vector3 b, Vector3 c)
{
    Vector3 Ab = CollisionMath_Subtract(b, a);
    Vector3 Ac = CollisionMath_Subtract(c, a);
    float CrossSquared = CollisionMath_LengthSquared(CollisionMath_Cross(Ab, Ac));
    float Scale = CollisionMath_LengthSquared(Ab) * CollisionMath_LengthSquared(Ac);
    return !(CrossSquared > (DEGENERATE_SINE * DEGENERATE_SINE * Scale)) || !(Scale > 0.0f);
}

static Vector3 GetNormal(const Vector3* vertices, const uint32_t indices[3])
{
    Vector3 A = vertices[indices[0]];
    Vector3 Cross = CollisionMath_Cross(CollisionMath_Subtract(vertices[indices[1]], A),
        CollisionMath_Subtract(vertices[indices[2]], A));
    return CollisionMath_NormalizeOr(Cross, CollisionMath_Vector(0.0f, 1.0f, 0.0f));
}

/* Counts the parts' triangles and checks their indices; false (with *outError) for a bad part. */
static bool CountTriangles(const CollisionMeshPart* parts, size_t partCount, size_t* outTriangles,
    size_t* outVertices, Error* outError)
{
    size_t Triangles = 0U;
    size_t Vertices = 0U;
    for (size_t Part = 0U; Part < partCount; Part++)
    {
        const CollisionMeshPart* Source = &parts[Part];
        if ((Source->Vertices == NULL) || (Source->VertexCount == 0U))
        {
            continue;
        }
        size_t Count = (Source->Indices != NULL) ? Source->TriangleCount : (Source->VertexCount / 3U);
        if (Source->Indices != NULL)
        {
            for (size_t Index = 0U; Index < (Count * 3U); Index++)
            {
                if (Source->Indices[Index] >= Source->VertexCount)
                {
                    *outError = Error_Construct3(ErrorCode_InvalidAssetData,
                        u8"A collision mesh's part %llu names vertex %u of %u.", (unsigned long long)Part,
                        (unsigned)Source->Indices[Index], Source->VertexCount);
                    return false;
                }
            }
        }
        Triangles += Count;
        Vertices += Source->VertexCount;
        if (Triangles > COLLISION_MESH_MAX_TRIANGLES)
        {
            *outError = Error_Construct3(ErrorCode_InvalidAssetData,
                u8"A collision mesh may have at most %u triangles.", COLLISION_MESH_MAX_TRIANGLES);
            return false;
        }
    }
    *outTriangles = Triangles;
    *outVertices = Vertices;
    return true;
}

/* Welds the parts' vertices and collects the triangles that have an area. */
static bool CollectTriangles(const CollisionMeshPart* parts, size_t partCount, Matrix transform, Vector3* vertices,
    uint32_t* outVertexCount, BuildTriangle* triangles, uint32_t* outTriangleCount, size_t vertexCapacity)
{
    size_t Capacity = GetTableCapacity(vertexCapacity);
    WeldEntry* Table = Memory_Allocate(Capacity * sizeof(WeldEntry));
    for (size_t Slot = 0U; Slot < Capacity; Slot++)
    {
        Table[Slot].Index = NONE;
    }
    uint32_t* Remap = Memory_Allocate(vertexCapacity * sizeof(uint32_t));
    uint32_t VertexCount = 0U;
    uint32_t TriangleCount = 0U;
    bool IsFinite = true;
    for (size_t Part = 0U; (Part < partCount) && IsFinite; Part++)
    {
        const CollisionMeshPart* Source = &parts[Part];
        if ((Source->Vertices == NULL) || (Source->VertexCount == 0U))
        {
            continue;
        }
        for (uint32_t Vertex = 0U; Vertex < Source->VertexCount; Vertex++)
        {
            const float* P = &Source->Vertices[(size_t)Vertex * 3U];
            Vector3 Position = Vector3Transform(CollisionMath_Vector(P[0], P[1], P[2]), transform);
            if (!CollisionMath_IsFinite(Position))
            {
                IsFinite = false;
                break;
            }
            Remap[Vertex] = Weld(Table, Capacity, vertices, &VertexCount, Position);
        }
        uint32_t Count = (Source->Indices != NULL) ? Source->TriangleCount : (Source->VertexCount / 3U);
        for (uint32_t Triangle = 0U; (Triangle < Count) && IsFinite; Triangle++)
        {
            uint32_t Corners[3];
            for (uint32_t Corner = 0U; Corner < 3U; Corner++)
            {
                uint32_t Local = (Source->Indices != NULL) ? Source->Indices[(Triangle * 3U) + Corner]
                    : ((Triangle * 3U) + Corner);
                Corners[Corner] = Remap[Local];
            }
            if ((Corners[0] == Corners[1]) || (Corners[1] == Corners[2]) || (Corners[2] == Corners[0])
                || IsDegenerate(vertices[Corners[0]], vertices[Corners[1]], vertices[Corners[2]]))
            {
                continue;
            }
            BuildTriangle* Built = &triangles[TriangleCount++];
            Built->Indices[0] = Corners[0];
            Built->Indices[1] = Corners[1];
            Built->Indices[2] = Corners[2];
            Built->Part = (uint32_t)Part;
            Built->SourceIndex = Triangle;
            Built->Min = CollisionMath_Min(vertices[Corners[0]], CollisionMath_Min(vertices[Corners[1]],
                vertices[Corners[2]]));
            Built->Max = CollisionMath_Max(vertices[Corners[0]], CollisionMath_Max(vertices[Corners[1]],
                vertices[Corners[2]]));
            Built->Centroid = CollisionMath_Scale(CollisionMath_Add(vertices[Corners[0]],
                CollisionMath_Add(vertices[Corners[1]], vertices[Corners[2]])), 1.0f / 3.0f);
        }
    }
    Memory_Free(Table);
    Memory_Free(Remap);
    *outVertexCount = VertexCount;
    *outTriangleCount = TriangleCount;
    return IsFinite;
}


// Static functions: active edges.
static uint32_t HashEdge(uint32_t low, uint32_t high)
{
    uint32_t Bits[3] = { low, high, 0x9E3779B9U };
    return HashBits(Bits);
}

/* Records that @p triangle's edge @p edge (its corners low/high in either order) exists. */
static void AddEdge(EdgeEntry* table, size_t capacity, uint32_t a, uint32_t b, uint32_t triangle, uint32_t edge)
{
    uint32_t Low = (a < b) ? a : b;
    uint32_t High = (a < b) ? b : a;
    size_t Slot = HashEdge(Low, High) & (capacity - 1U);
    while (table[Slot].Low != NONE)
    {
        if ((table[Slot].Low == Low) && (table[Slot].High == High))
        {
            if (table[Slot].Count == 1U)
            {
                table[Slot].SecondTriangle = triangle;
                table[Slot].SecondEdge = edge;
            }
            table[Slot].Count++;
            return;
        }
        Slot = (Slot + 1U) & (capacity - 1U);
    }
    table[Slot] = (EdgeEntry){ .Low = Low, .High = High, .Triangle = triangle, .Edge = edge, .Count = 1U,
        .SecondTriangle = NONE, .SecondEdge = NONE };
}

/* Whether the edge between two triangles is active: not flat, and convex seen from the first one's front. */
static bool IsEdgeActive(const Vector3* vertices, const BuildTriangle* first, uint32_t firstEdge,
    const BuildTriangle* second, uint32_t secondEdge)
{
    // Wound consistently, the shared edge runs opposite ways in the two triangles.
    if (first->Indices[firstEdge] != second->Indices[(secondEdge + 1U) % 3U])
    {
        return true;
    }
    Vector3 FirstNormal = GetNormal(vertices, first->Indices);
    Vector3 SecondNormal = GetNormal(vertices, second->Indices);
    if (CollisionMath_Dot(FirstNormal, SecondNormal) > FLAT_EDGE_COSINE)
    {
        return false;
    }
    Vector3 Opposite = vertices[second->Indices[(secondEdge + 2U) % 3U]];
    Vector3 OnEdge = vertices[first->Indices[firstEdge]];
    return CollisionMath_Dot(FirstNormal, CollisionMath_Subtract(Opposite, OnEdge)) < 0.0f;
}

/* Each triangle's active edge bits. */
static void FindActiveEdges(const Vector3* vertices, const BuildTriangle* triangles, uint32_t triangleCount,
    uint8_t* outActive)
{
    size_t Capacity = GetTableCapacity((size_t)triangleCount * 3U);
    EdgeEntry* Table = Memory_Allocate(Capacity * sizeof(EdgeEntry));
    for (size_t Slot = 0U; Slot < Capacity; Slot++)
    {
        Table[Slot].Low = NONE;
    }
    for (uint32_t Triangle = 0U; Triangle < triangleCount; Triangle++)
    {
        outActive[Triangle] = 0x7U;
        for (uint32_t Edge = 0U; Edge < 3U; Edge++)
        {
            AddEdge(Table, Capacity, triangles[Triangle].Indices[Edge], triangles[Triangle].Indices[(Edge + 1U) % 3U],
                Triangle, Edge);
        }
    }
    for (size_t Slot = 0U; Slot < Capacity; Slot++)
    {
        const EdgeEntry* Entry = &Table[Slot];
        if ((Entry->Low == NONE) || (Entry->Count != 2U))
        {
            continue;
        }
        const BuildTriangle* First = &triangles[Entry->Triangle];
        const BuildTriangle* Second = &triangles[Entry->SecondTriangle];
        if (!IsEdgeActive(vertices, First, Entry->Edge, Second, Entry->SecondEdge))
        {
            outActive[Entry->Triangle] = (uint8_t)(outActive[Entry->Triangle] & ~(1U << Entry->Edge));
            outActive[Entry->SecondTriangle] = (uint8_t)(outActive[Entry->SecondTriangle] & ~(1U << Entry->SecondEdge));
        }
    }
    Memory_Free(Table);
}


// Static functions: the BVH.
static float GetAxis(Vector3 v, uint32_t axis)
{
    return (axis == 0U) ? v.x : ((axis == 1U) ? v.y : v.z);
}

/* Partially orders order[first .. first + count) by the triangles' centres along @p axis so the median sits at its
 * place (quickselect, iterative). */
static void SelectMedian(const BuildTriangle* triangles, uint32_t* order, uint32_t first, uint32_t count,
    uint32_t axis)
{
    uint32_t Low = first;
    uint32_t High = first + count - 1U;
    uint32_t Median = first + (count / 2U);
    while (Low < High)
    {
        float Pivot = GetAxis(triangles[order[(Low + High) / 2U]].Centroid, axis);
        uint32_t I = Low;
        uint32_t J = High;
        while (I <= J)
        {
            while (GetAxis(triangles[order[I]].Centroid, axis) < Pivot)
            {
                I++;
            }
            while (GetAxis(triangles[order[J]].Centroid, axis) > Pivot)
            {
                J--;
            }
            if (I <= J)
            {
                uint32_t Swap = order[I];
                order[I] = order[J];
                order[J] = Swap;
                I++;
                if (J == 0U)
                {
                    break;
                }
                J--;
            }
        }
        if (Median <= J)
        {
            High = J;
        }
        else if (Median >= I)
        {
            Low = I;
        }
        else
        {
            break;
        }
    }
}

/* The number of nodes the splitting rule makes for @p count triangles: a leaf for at most LEAF_TRIANGLES, otherwise a
 * node over the two halves (count / 2 and the rest). */
static uint32_t CountNodes(uint32_t count)
{
    uint32_t Pending[MAX_BVH_DEPTH];
    size_t PendingCount = 0U;
    uint32_t Nodes = 0U;
    Pending[PendingCount++] = count;
    while (PendingCount > 0U)
    {
        uint32_t Count = Pending[--PendingCount];
        Nodes++;
        if (Count > LEAF_TRIANGLES)
        {
            Pending[PendingCount++] = Count / 2U;
            Pending[PendingCount++] = Count - (Count / 2U);
        }
    }
    return Nodes;
}

/* Builds the nodes over order[] (depth-first: a node's first child is the next node, its second child comes after the
 * first child's whole subtree, CountNodes of it). A median split halves the triangles, so a mesh of at most
 * COLLISION_MESH_MAX_TRIANGLES is at most about 20 levels deep and the stacks (one pending sibling per level) stay far
 * below MAX_BVH_DEPTH. */
static void BuildNodes(const BuildTriangle* triangles, uint32_t* order, uint32_t triangleCount,
    CollisionMeshNode* nodes)
{
    BuildRange Stack[MAX_BVH_DEPTH];
    size_t StackCount = 0U;
    Stack[StackCount++] = (BuildRange){ .Node = 0U, .First = 0U, .Count = triangleCount };
    while (StackCount > 0U)
    {
        BuildRange Range = Stack[--StackCount];
        Vector3 Min = CollisionMath_Vector(FLT_MAX, FLT_MAX, FLT_MAX);
        Vector3 Max = CollisionMath_Vector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        Vector3 CentroidMin = Min;
        Vector3 CentroidMax = Max;
        for (uint32_t Index = Range.First; Index < (Range.First + Range.Count); Index++)
        {
            const BuildTriangle* Triangle = &triangles[order[Index]];
            Min = CollisionMath_Min(Min, Triangle->Min);
            Max = CollisionMath_Max(Max, Triangle->Max);
            CentroidMin = CollisionMath_Min(CentroidMin, Triangle->Centroid);
            CentroidMax = CollisionMath_Max(CentroidMax, Triangle->Centroid);
        }
        CollisionMeshNode* Node = &nodes[Range.Node];
        Node->_min = Min;
        Node->_max = Max;
        if (Range.Count <= LEAF_TRIANGLES)
        {
            Node->_start = Range.First;
            Node->_count = Range.Count;
            continue;
        }
        Vector3 Spread = CollisionMath_Subtract(CentroidMax, CentroidMin);
        uint32_t Axis = (Spread.x >= Spread.y) ? ((Spread.x >= Spread.z) ? 0U : 2U) : ((Spread.y >= Spread.z) ? 1U : 2U);
        SelectMedian(triangles, order, Range.First, Range.Count, Axis);
        uint32_t LeftCount = Range.Count / 2U;
        uint32_t LeftNode = Range.Node + 1U;
        uint32_t RightNode = LeftNode + CountNodes(LeftCount);
        Node->_start = RightNode;
        Node->_count = 0U;
        Stack[StackCount++] = (BuildRange){ .Node = RightNode, .First = Range.First + LeftCount,
            .Count = Range.Count - LeftCount };
        Stack[StackCount++] = (BuildRange){ .Node = LeftNode, .First = Range.First, .Count = LeftCount };
    }
}


// Public functions.
Error CollisionMesh_Create(const CollisionMeshPart* parts, size_t partCount, Matrix transform, CollisionMesh** outMesh)
{
    *outMesh = NULL;
    size_t TriangleCapacity = 0U;
    size_t VertexCapacity = 0U;
    Error Result = Error_CreateSuccess();
    if (!CountTriangles(parts, partCount, &TriangleCapacity, &VertexCapacity, &Result))
    {
        return Result;
    }
    if (TriangleCapacity == 0U)
    {
        return Error_Construct2(ErrorCode_InvalidAssetData, "A collision mesh needs at least one triangle.");
    }
    Vector3* Vertices = Memory_Allocate(VertexCapacity * sizeof(Vector3));
    BuildTriangle* Triangles = Memory_Allocate(TriangleCapacity * sizeof(BuildTriangle));
    uint32_t VertexCount = 0U;
    uint32_t TriangleCount = 0U;
    bool IsFinite = CollectTriangles(parts, partCount, transform, Vertices, &VertexCount, Triangles, &TriangleCount,
        VertexCapacity);
    if (!IsFinite || (TriangleCount == 0U))
    {
        Memory_Free(Vertices);
        Memory_Free(Triangles);
        return Error_Construct2(ErrorCode_InvalidAssetData, IsFinite
            ? "A collision mesh needs at least one triangle with an area."
            : "A collision mesh's vertices must be finite.");
    }

    uint8_t* Active = Memory_Allocate(TriangleCount);
    FindActiveEdges(Vertices, Triangles, TriangleCount, Active);
    uint32_t* Order = Memory_Allocate((size_t)TriangleCount * sizeof(uint32_t));
    for (uint32_t Index = 0U; Index < TriangleCount; Index++)
    {
        Order[Index] = Index;
    }
    uint32_t NodeCapacity = CountNodes(TriangleCount);

    size_t Offset = sizeof(CollisionMesh);
    size_t VerticesOffset = Offset;
    Offset += (size_t)VertexCount * sizeof(Vector3);
    size_t TrianglesOffset = Offset;
    Offset += (size_t)TriangleCount * sizeof(CollisionMeshTriangle);
    size_t NodesOffset = Offset;
    Offset += (size_t)NodeCapacity * sizeof(CollisionMeshNode);
    size_t PartsOffset = Offset;
    Offset += partCount * sizeof(int32_t);
    unsigned char* Block = Memory_Allocate(Offset);
    Memory_Zero(Block, Offset);
    CollisionMesh* Built = (CollisionMesh*)(void*)Block;
    Vector3* OutVertices = (Vector3*)(void*)(Block + VerticesOffset);
    CollisionMeshTriangle* OutTriangles = (CollisionMeshTriangle*)(void*)(Block + TrianglesOffset);
    CollisionMeshNode* OutNodes = (CollisionMeshNode*)(void*)(Block + NodesOffset);
    int32_t* OutParts = (int32_t*)(void*)(Block + PartsOffset);

    BuildNodes(Triangles, Order, TriangleCount, OutNodes);
    Memory_Copy(Vertices, OutVertices, (size_t)VertexCount * sizeof(Vector3));
    Vector3 Min = CollisionMath_Vector(FLT_MAX, FLT_MAX, FLT_MAX);
    Vector3 Max = CollisionMath_Vector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (uint32_t Vertex = 0U; Vertex < VertexCount; Vertex++)
    {
        Min = CollisionMath_Min(Min, Vertices[Vertex]);
        Max = CollisionMath_Max(Max, Vertices[Vertex]);
    }
    for (uint32_t Index = 0U; Index < TriangleCount; Index++)
    {
        const BuildTriangle* Source = &Triangles[Order[Index]];
        OutTriangles[Index] = (CollisionMeshTriangle){ ._indices = { Source->Indices[0], Source->Indices[1],
            Source->Indices[2] }, ._part = Source->Part, ._sourceIndex = Source->SourceIndex,
            ._activeEdges = Active[Order[Index]] };
    }
    for (size_t Part = 0U; Part < partCount; Part++)
    {
        OutParts[Part] = parts[Part].MaterialIndex;
    }

    Built->_vertices = OutVertices;
    Built->_vertexCount = VertexCount;
    Built->_triangles = OutTriangles;
    Built->_triangleCount = TriangleCount;
    Built->_nodes = OutNodes;
    Built->_nodeCount = NodeCapacity;
    Built->_partMaterials = OutParts;
    Built->_partCount = (uint32_t)partCount;
    Built->_boundsMin = Min;
    Built->_boundsMax = Max;

    Memory_Free(Vertices);
    Memory_Free(Triangles);
    Memory_Free(Active);
    Memory_Free(Order);
    *outMesh = Built;
    return Error_CreateSuccess();
}

Error CollisionMesh_CreateFromModel(const GameModel* model, CollisionMesh** outMesh)
{
    *outMesh = NULL;
    const Model* RayModel = &model->_rayModel;
    if ((RayModel->meshCount <= 0) || (RayModel->meshes == NULL))
    {
        return Error_Construct2(ErrorCode_InvalidAssetData, "A collision mesh's model has no meshes.");
    }
    size_t PartCount = (size_t)RayModel->meshCount;
    CollisionMeshPart* Parts = Memory_Allocate(PartCount * sizeof(CollisionMeshPart));
    for (size_t Part = 0U; Part < PartCount; Part++)
    {
        const Mesh* Source = &RayModel->meshes[Part];
        Parts[Part] = (CollisionMeshPart)
        {
            .Vertices = Source->vertices, .VertexCount = (Source->vertexCount > 0) ? (uint32_t)Source->vertexCount : 0U,
            .Indices = Source->indices,
            .TriangleCount = (Source->triangleCount > 0) ? (uint32_t)Source->triangleCount : 0U,
            .MaterialIndex = (RayModel->meshMaterial != NULL) ? RayModel->meshMaterial[Part] : -1
        };
    }
    Error Result = CollisionMesh_Create(Parts, PartCount, RayModel->transform, outMesh);
    Memory_Free(Parts);
    return Result;
}

void CollisionMesh_Destroy(CollisionMesh* self)
{
    Memory_Free(self);
}
