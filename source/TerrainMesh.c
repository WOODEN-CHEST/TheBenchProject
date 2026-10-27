#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "TerrainMeshInternal.h"
#include "CollisionShape.h"
#include "wr/WRMemory.h"

/*
 * Terrain meshes (include/TerrainMesh.h): the checks every source goes through, the copy onto the position grid, the
 * vertex groups (vertices with one position: one vertex for normals and edges, so imported hard edges, which are split
 * vertices, still join their neighbours), each group's triangles (the adjacency every neighbourhood walk uses), the
 * smooth normals, the active edges, the setters and what they mark, and the rebuild of what they marked. The chunks are
 * source/TerrainMeshChunks.c's.
 *
 * WHAT A CHANGE TOUCHES. A moved vertex changes the shape of the triangles around it: their normals, so the normals of
 * every vertex of theirs (the vertex's ring), and the activity of every edge of theirs, which the triangle across each
 * edge also stores. Every triangle around a vertex of the ring is therefore marked (its edges recomputed, its chunk
 * rebuilt), and every group of the ring gets its normal again. Opening or closing a hole does the same around the
 * hole's corners (holes count for neither normals nor edges). New weights, densities or a face material change only the
 * chunks that draw the vertex or the triangle.
 */


// Macros.
/** Room for a description in printable form. */
#define DESCRIPTION_CAPACITY ((size_t)160)
/** The smallest group table. */
#define MIN_TABLE_CAPACITY ((size_t)16)
/** The first capacity of the dirty lists. */
#define INITIAL_DIRTY_CAPACITY 16U
/** How far from 1 a squared length may be for an imported normal to count as unit already. */
#define UNIT_LENGTH_TOLERANCE 1e-6


// Static functions: helpers.
static Vector3 Subtract(Vector3 a, Vector3 b)
{
    return (Vector3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static Vector3 Cross(Vector3 a, Vector3 b)
{
    return (Vector3){ (a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x) };
}

/* @p v divided by its length, or straight up for a zero or non-finite one. */
static Vector3 NormalizeOrUp(Vector3 v)
{
    double Length = sqrt(((double)v.x * (double)v.x) + ((double)v.y * (double)v.y) + ((double)v.z * (double)v.z));
    if (!isfinite(Length) || !(Length > 0.0))
    {
        return (Vector3){ 0.0f, 1.0f, 0.0f };
    }
    return (Vector3){ (float)((double)v.x / Length), (float)((double)v.y / Length), (float)((double)v.z / Length) };
}

/* An imported normal as the mesh keeps it: as it is when it is unit already (so a mesh read back from its binary form
 * has its normals bit for bit), divided by its length otherwise. */
static Vector3 NormalizeImported(Vector3 v)
{
    double LengthSquared = ((double)v.x * (double)v.x) + ((double)v.y * (double)v.y) + ((double)v.z * (double)v.z);
    return (fabs(LengthSquared - 1.0) <= UNIT_LENGTH_TOLERANCE) ? v : NormalizeOrUp(v);
}

static size_t GetTableCapacity(size_t count)
{
    size_t Capacity = MIN_TABLE_CAPACITY;
    while (Capacity < (count * 2U))
    {
        Capacity *= 2U;
    }
    return Capacity;
}

static uint32_t HashPosition(Vector3 position)
{
    uint32_t Bits[3];
    Memory_Copy(&position.x, &Bits[0], sizeof(uint32_t));
    Memory_Copy(&position.y, &Bits[1], sizeof(uint32_t));
    Memory_Copy(&position.z, &Bits[2], sizeof(uint32_t));
    uint32_t Hash = 2166136261U;
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Hash = (Hash ^ Bits[Index]) * 16777619U;
        Hash ^= Hash >> 15U;
    }
    return Hash;
}

static bool ArePositionsEqual(Vector3 a, Vector3 b)
{
    return (a.x == b.x) && (a.y == b.y) && (a.z == b.z);
}

static bool IsHole(const TerrainTriangle* triangle)
{
    return (triangle->_flags & TERRAIN_TRIANGLE_FLAG_HOLE) != 0U;
}

/* A triangle's unit normal from its corners (straight up for one without an area). */
static Vector3 GetTriangleNormal(const TerrainMesh* self, const TerrainTriangle* triangle)
{
    Vector3 A = self->_positions[triangle->_indices[0]];
    return NormalizeOrUp(Cross(Subtract(self->_positions[triangle->_indices[1]], A),
        Subtract(self->_positions[triangle->_indices[2]], A)));
}

/* The longest edge of the triangle with these corners, in double. */
static double GetLongestEdge(Vector3 a, Vector3 b, Vector3 c)
{
    const Vector3 Corners[3] = { a, b, c };
    double Longest = 0.0;
    for (size_t Edge = 0U; Edge < 3U; Edge++)
    {
        Vector3 From = Corners[Edge];
        Vector3 To = Corners[(Edge + 1U) % 3U];
        double X = (double)To.x - (double)From.x;
        double Y = (double)To.y - (double)From.y;
        double Z = (double)To.z - (double)From.z;
        Longest = fmax(Longest, sqrt((X * X) + (Y * Y) + (Z * Z)));
    }
    return Longest;
}


// Static functions: the checks.
static Error CheckCounts(const TerrainMeshData* data, const unsigned char* description)
{
    if ((data->VertexCount == 0U) || (data->VertexCount > TERRAIN_MESH_MAX_VERTICES))
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "it has %" PRIu32 " vertices; a terrain "
            "has 1 to %" PRIu32 ".", data->VertexCount, TERRAIN_MESH_MAX_VERTICES);
    }
    if ((data->TriangleCount == 0U) || (data->TriangleCount > TERRAIN_MESH_MAX_TRIANGLES))
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "it has %" PRIu32 " triangles; a "
            "terrain has 1 to %" PRIu32 ".", data->TriangleCount, TERRAIN_MESH_MAX_TRIANGLES);
    }
    if ((data->MaterialCount == 0U) || (data->MaterialCount > TERRAIN_MAX_MATERIALS))
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "its palette holds %" PRIu32 " "
            "materials; a terrain has 1 to %u.", data->MaterialCount, TERRAIN_MAX_MATERIALS);
    }
    if (data->DensityChannelCount > TERRAIN_MAX_DENSITY_CHANNELS)
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "it has %" PRIu32 " density channels; "
            "a terrain has at most %u.", data->DensityChannelCount, TERRAIN_MAX_DENSITY_CHANNELS);
    }
    if (!isfinite(data->ChunkSize) || (data->ChunkSize < TERRAIN_MIN_CHUNK_SIZE)
        || (data->ChunkSize > TERRAIN_MAX_CHUNK_SIZE))
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "its chunk size is %g m; it must be %g "
            "to %g m.", (double)data->ChunkSize, (double)TERRAIN_MIN_CHUNK_SIZE, (double)TERRAIN_MAX_CHUNK_SIZE);
    }
    bool IsModeKnown = (data->NormalMode == TerrainNormalMode_Smooth) || (data->NormalMode == TerrainNormalMode_Imported);
    if (!IsModeKnown || ((data->NormalMode == TerrainNormalMode_Imported) && (data->Normals == NULL)))
    {
        return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "its normals are neither smooth nor "
            "given (imported normals need a normal per vertex).");
    }
    return Error_CreateSuccess();
}

static Error CheckVertices(const TerrainMeshData* data, const unsigned char* description)
{
    for (uint32_t Vertex = 0U; Vertex < data->VertexCount; Vertex++)
    {
        Vector3 Position = data->Positions[Vertex];
        if (!TerrainMeshInternal_IsInExtent(Position.x) || !TerrainMeshInternal_IsInExtent(Position.y)
            || !TerrainMeshInternal_IsInExtent(Position.z))
        {
            return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "vertex %" PRIu32 " is at (%g, %g, "
                "%g); positions must be finite and within %g m of the terrain's origin on every axis.", Vertex,
                (double)Position.x, (double)Position.y, (double)Position.z, TERRAIN_MESH_EXTENT);
        }
        if (data->NormalMode == TerrainNormalMode_Imported)
        {
            Vector3 Normal = data->Normals[Vertex];
            double LengthSquared = ((double)Normal.x * (double)Normal.x) + ((double)Normal.y * (double)Normal.y)
                + ((double)Normal.z * (double)Normal.z);
            if (!isfinite(LengthSquared) || !(LengthSquared > 1e-20))
            {
                return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "vertex %" PRIu32 "'s normal "
                    "has no direction (zero or not finite).", Vertex);
            }
        }
        for (uint32_t Entry = data->MaterialCount; (data->Weights != NULL) && (Entry < TERRAIN_MAX_MATERIALS); Entry++)
        {
            if (data->Weights[((size_t)Vertex * TERRAIN_MAX_MATERIALS) + Entry] != 0U)
            {
                return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "vertex %" PRIu32 " weighs "
                    "material %" PRIu32 ", past the palette's %" PRIu32 ".", Vertex, Entry, data->MaterialCount);
            }
        }
        for (uint32_t Channel = data->DensityChannelCount;
            (data->Densities != NULL) && (Channel < TERRAIN_MAX_DENSITY_CHANNELS); Channel++)
        {
            if (data->Densities[((size_t)Vertex * TERRAIN_MAX_DENSITY_CHANNELS) + Channel] != 0U)
            {
                return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "vertex %" PRIu32 " has a "
                    "density in channel %" PRIu32 ", past the terrain's %" PRIu32 " channels.", Vertex, Channel,
                    data->DensityChannelCount);
            }
        }
    }
    return Error_CreateSuccess();
}

static Error CheckTriangles(const TerrainMeshData* data, const unsigned char* description)
{
    for (uint32_t Triangle = 0U; Triangle < data->TriangleCount; Triangle++)
    {
        const uint32_t* Corners = &data->Indices[(size_t)Triangle * 3U];
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            if (Corners[Corner] >= data->VertexCount)
            {
                return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "triangle %" PRIu32 " names "
                    "vertex %" PRIu32 " of %" PRIu32 ".", Triangle, Corners[Corner], data->VertexCount);
            }
        }
        if ((Corners[0] == Corners[1]) || (Corners[1] == Corners[2]) || (Corners[2] == Corners[0]))
        {
            return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "triangle %" PRIu32 " uses one "
                "vertex twice.", Triangle);
        }
        uint8_t Flags = (data->TriangleFlags != NULL) ? data->TriangleFlags[Triangle] : 0U;
        if ((Flags & (uint8_t)~TERRAIN_TRIANGLE_FLAGS_KNOWN) != 0U)
        {
            return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "triangle %" PRIu32 " has unknown "
                "flags (0x%02x).", Triangle, (unsigned)Flags);
        }
        uint8_t FaceMaterial = (data->TriangleMaterials != NULL) ? data->TriangleMaterials[Triangle] : 0U;
        if (((Flags & TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL) != 0U) && ((uint32_t)FaceMaterial >= data->MaterialCount))
        {
            return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "triangle %" PRIu32 " takes "
                "material %u, past the palette's %" PRIu32 ".", Triangle, (unsigned)FaceMaterial, data->MaterialCount);
        }
    }
    return Error_CreateSuccess();
}

/* What the grid may have joined, after the copy: no triangle with two corners at one position, none with an edge past
 * the extent. */
static Error CheckSnappedTriangles(const TerrainMesh* self, const unsigned char* description)
{
    for (uint32_t Triangle = 0U; Triangle < self->_triangleCount; Triangle++)
    {
        const uint32_t* Corners = self->_triangles[Triangle]._indices;
        if ((self->_groups[Corners[0]] == self->_groups[Corners[1]])
            || (self->_groups[Corners[1]] == self->_groups[Corners[2]])
            || (self->_groups[Corners[2]] == self->_groups[Corners[0]]))
        {
            return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "triangle %" PRIu32 " has two "
                "corners at one position (on the %g m grid).", Triangle, TERRAIN_POSITION_QUANTUM);
        }
        if (GetLongestEdge(self->_positions[Corners[0]], self->_positions[Corners[1]], self->_positions[Corners[2]])
            > TERRAIN_MESH_EXTENT)
        {
            return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "triangle %" PRIu32 " has an edge "
                "longer than %g m.", Triangle, TERRAIN_MESH_EXTENT);
        }
    }
    return Error_CreateSuccess();
}


// Static functions: groups, normals and edges.
/* Each vertex's group (the first vertex with its position) and the groups' member lists. */
static void BuildGroups(TerrainMesh* self)
{
    size_t Capacity = GetTableCapacity(self->_vertexCount);
    uint32_t* Table = Memory_Allocate(Capacity * sizeof(uint32_t));
    for (size_t Slot = 0U; Slot < Capacity; Slot++)
    {
        Table[Slot] = TERRAIN_NONE;
    }
    for (uint32_t Vertex = 0U; Vertex < self->_vertexCount; Vertex++)
    {
        Vector3 Position = self->_positions[Vertex];
        size_t Slot = HashPosition(Position) & (Capacity - 1U);
        uint32_t Group = Vertex;
        while (Table[Slot] != TERRAIN_NONE)
        {
            if (ArePositionsEqual(self->_positions[Table[Slot]], Position))
            {
                Group = Table[Slot];
                break;
            }
            Slot = (Slot + 1U) & (Capacity - 1U);
        }
        if (Group == Vertex)
        {
            Table[Slot] = Vertex;
        }
        self->_groups[Vertex] = Group;
        self->_nextInGroup[Vertex] = TERRAIN_NONE;
        if (Group != Vertex)
        {
            self->_nextInGroup[Vertex] = self->_nextInGroup[Group];
            self->_nextInGroup[Group] = Vertex;
        }
    }
    Memory_Free(Table);
}

/* Every group's triangles, ascending (each triangle's corners lie in three groups: CheckSnappedTriangles). */
static void BuildGroupTriangles(TerrainMesh* self)
{
    uint32_t* Starts = self->_groupTriangleStarts;
    Memory_Zero(Starts, ((size_t)self->_vertexCount + 1U) * sizeof(uint32_t));
    for (uint32_t Triangle = 0U; Triangle < self->_triangleCount; Triangle++)
    {
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            Starts[self->_groups[self->_triangles[Triangle]._indices[Corner]] + 1U]++;
        }
    }
    for (uint32_t Group = 0U; Group < self->_vertexCount; Group++)
    {
        Starts[Group + 1U] += Starts[Group];
    }
    uint32_t* Cursor = Memory_Allocate(((size_t)self->_vertexCount + 1U) * sizeof(uint32_t));
    Memory_Copy(Starts, Cursor, ((size_t)self->_vertexCount + 1U) * sizeof(uint32_t));
    for (uint32_t Triangle = 0U; Triangle < self->_triangleCount; Triangle++)
    {
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            uint32_t Group = self->_groups[self->_triangles[Triangle]._indices[Corner]];
            self->_groupTriangles[Cursor[Group]++] = Triangle;
        }
    }
    Memory_Free(Cursor);
}

/* A group's smooth normal: the triangles around it that are not holes, each weighted by its area (the cross product of
 * two edges is twice the area along the normal), summed in double in ascending triangle order. */
static Vector3 ComputeGroupNormal(const TerrainMesh* self, uint32_t group)
{
    double Sum[3] = { 0.0, 0.0, 0.0 };
    for (uint32_t Entry = self->_groupTriangleStarts[group]; Entry < self->_groupTriangleStarts[group + 1U]; Entry++)
    {
        const TerrainTriangle* Triangle = &self->_triangles[self->_groupTriangles[Entry]];
        if (IsHole(Triangle))
        {
            continue;
        }
        Vector3 A = self->_positions[Triangle->_indices[0]];
        Vector3 B = self->_positions[Triangle->_indices[1]];
        Vector3 C = self->_positions[Triangle->_indices[2]];
        double Ab[3] = { (double)B.x - (double)A.x, (double)B.y - (double)A.y, (double)B.z - (double)A.z };
        double Ac[3] = { (double)C.x - (double)A.x, (double)C.y - (double)A.y, (double)C.z - (double)A.z };
        Sum[0] += (Ab[1] * Ac[2]) - (Ab[2] * Ac[1]);
        Sum[1] += (Ab[2] * Ac[0]) - (Ab[0] * Ac[2]);
        Sum[2] += (Ab[0] * Ac[1]) - (Ab[1] * Ac[0]);
    }
    double Length = sqrt((Sum[0] * Sum[0]) + (Sum[1] * Sum[1]) + (Sum[2] * Sum[2]));
    if (!isfinite(Length) || !(Length > 0.0))
    {
        return (Vector3){ 0.0f, 1.0f, 0.0f };
    }
    return (Vector3){ (float)(Sum[0] / Length), (float)(Sum[1] / Length), (float)(Sum[2] / Length) };
}

/* Gives every vertex of a group its smooth normal. */
static void UpdateGroupNormal(TerrainMesh* self, uint32_t group)
{
    Vector3 Normal = ComputeGroupNormal(self, group);
    for (uint32_t Vertex = group; Vertex != TERRAIN_NONE; Vertex = self->_nextInGroup[Vertex])
    {
        self->_normals[Vertex] = Normal;
    }
}

/* Whether @p triangle has a corner in @p group, and which. */
static bool TryFindCorner(const TerrainMesh* self, const TerrainTriangle* triangle, uint32_t group, uint32_t* outCorner)
{
    for (uint32_t Corner = 0U; Corner < 3U; Corner++)
    {
        if (self->_groups[triangle->_indices[Corner]] == group)
        {
            *outCorner = Corner;
            return true;
        }
    }
    return false;
}

/* Whether one edge of a triangle (from its corner @p edge to the next) is active: unless exactly one other triangle that
 * is not a hole shares it, wound consistently (the edge runs the other way in it), across a flat or concave fold. */
static bool IsEdgeActive(const TerrainMesh* self, uint32_t triangleIndex, uint32_t edge, Vector3 normal)
{
    const TerrainTriangle* Triangle = &self->_triangles[triangleIndex];
    uint32_t From = self->_groups[Triangle->_indices[edge]];
    uint32_t To = self->_groups[Triangle->_indices[(edge + 1U) % 3U]];
    uint32_t Neighbour = TERRAIN_NONE;
    uint32_t NeighbourFrom = 0U;
    uint32_t MatchCount = 0U;
    for (uint32_t Entry = self->_groupTriangleStarts[From]; Entry < self->_groupTriangleStarts[From + 1U]; Entry++)
    {
        uint32_t Other = self->_groupTriangles[Entry];
        const TerrainTriangle* Candidate = &self->_triangles[Other];
        uint32_t ToCorner = 0U;
        if ((Other == triangleIndex) || IsHole(Candidate) || !TryFindCorner(self, Candidate, To, &ToCorner))
        {
            continue;
        }
        MatchCount++;
        Neighbour = Other;
        NeighbourFrom = ToCorner;
    }
    if (MatchCount != 1U)
    {
        return true;
    }
    // Wound consistently, the neighbour runs from To back to From: its corner after To's is From's.
    const TerrainTriangle* Other = &self->_triangles[Neighbour];
    if (self->_groups[Other->_indices[(NeighbourFrom + 1U) % 3U]] != From)
    {
        return true;
    }
    Vector3 Opposite = self->_positions[Other->_indices[(NeighbourFrom + 2U) % 3U]];
    return CollisionMesh_IsSharedEdgeActive(normal, GetTriangleNormal(self, Other),
        self->_positions[Triangle->_indices[edge]], Opposite);
}

static uint8_t ComputeActiveEdges(const TerrainMesh* self, uint32_t triangleIndex)
{
    const TerrainTriangle* Triangle = &self->_triangles[triangleIndex];
    if (IsHole(Triangle))
    {
        return TERRAIN_ALL_EDGES_ACTIVE;
    }
    Vector3 Normal = GetTriangleNormal(self, Triangle);
    uint8_t Active = 0U;
    for (uint32_t Edge = 0U; Edge < 3U; Edge++)
    {
        if (IsEdgeActive(self, triangleIndex, Edge, Normal))
        {
            Active = (uint8_t)(Active | (1U << Edge));
        }
    }
    return Active;
}


// Static functions: marking changes.
static void MarkChunk(TerrainMesh* self, uint32_t chunk)
{
    if (self->_isChunkDirty[chunk] == 0U)
    {
        self->_isChunkDirty[chunk] = 1U;
        GenericBuffer_AddLast(&self->_dirtyChunks, &chunk);
    }
}

/* A triangle whose edges are recomputed and whose chunk is rebuilt. */
static void MarkTriangle(TerrainMesh* self, uint32_t triangle)
{
    if (self->_isTriangleDirty[triangle] == 0U)
    {
        self->_isTriangleDirty[triangle] = 1U;
        GenericBuffer_AddLast(&self->_dirtyTriangles, &triangle);
    }
    MarkChunk(self, self->_triangleChunks[triangle]);
}

/* A group whose smooth normal is recomputed, and every triangle around it (their chunks show the normal). */
static void MarkGroupAndItsTriangles(TerrainMesh* self, uint32_t group)
{
    if (self->_isGroupDirty[group] == 0U)
    {
        self->_isGroupDirty[group] = 1U;
        GenericBuffer_AddLast(&self->_dirtyGroups, &group);
    }
    for (uint32_t Entry = self->_groupTriangleStarts[group]; Entry < self->_groupTriangleStarts[group + 1U]; Entry++)
    {
        MarkTriangle(self, self->_groupTriangles[Entry]);
    }
}

/* What a change of shape at a group touches: the groups of its ring (itself and every corner of its triangles) and
 * every triangle around them (the file comment). */
static void MarkAroundGroup(TerrainMesh* self, uint32_t group)
{
    for (uint32_t Entry = self->_groupTriangleStarts[group]; Entry < self->_groupTriangleStarts[group + 1U]; Entry++)
    {
        const TerrainTriangle* Triangle = &self->_triangles[self->_groupTriangles[Entry]];
        for (uint32_t Corner = 0U; Corner < 3U; Corner++)
        {
            MarkGroupAndItsTriangles(self, self->_groups[Triangle->_indices[Corner]]);
        }
    }
}

/* The chunks that draw a vertex: those of the triangles around its group. */
static void MarkVertexChunks(TerrainMesh* self, uint32_t vertex)
{
    uint32_t Group = self->_groups[vertex];
    for (uint32_t Entry = self->_groupTriangleStarts[Group]; Entry < self->_groupTriangleStarts[Group + 1U]; Entry++)
    {
        MarkChunk(self, self->_triangleChunks[self->_groupTriangles[Entry]]);
    }
}

static Error CheckIndices(const TerrainMesh* self, const uint32_t* indices, size_t count, uint32_t limit,
    const char* function)
{
    if ((self == NULL) || ((indices == NULL) && (count > 0U)))
    {
        return Error_Construct4(ErrorCode_IllegalArgument, "%s: self and the indices must not be NULL.", function);
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (indices[Index] >= limit)
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "%s: index %" PRIu32 " is past the last (%" PRIu32
                ").", function, indices[Index], limit);
        }
    }
    return Error_CreateSuccess();
}


// Internal functions (TerrainMeshInternal.h).
Error TerrainMeshInternal_Fail(const unsigned char* description, ErrorCode code, const char* format, ...)
{
    char Text[TERRAIN_MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    char Name[DESCRIPTION_CAPACITY];
    size_t NameLength = 0U;
    const unsigned char* Source = (description != NULL) ? description : (const unsigned char*)u8"The terrain";
    for (; (Source[NameLength] != 0U) && ((NameLength + 1U) < sizeof(Name)); NameLength++)
    {
        unsigned char Character = Source[NameLength];
        Name[NameLength] = ((Character >= 0x20U) && (Character < 0x7FU)) ? (char)Character : '?';
    }
    Name[NameLength] = '\0';
    return Error_Construct4(code, "%s: %s", Name, Text);
}

float TerrainMeshInternal_Snap(double value)
{
    float Snapped = (float)(round(value / TERRAIN_POSITION_QUANTUM) * TERRAIN_POSITION_QUANTUM);
    return (Snapped == 0.0f) ? 0.0f : Snapped;
}

bool TerrainMeshInternal_IsInExtent(double value)
{
    return isfinite(value) && (fabs(value) <= TERRAIN_MESH_EXTENT);
}


// Public functions.
Error TerrainMesh_Create(const TerrainMeshData* data, const unsigned char* description, TerrainMesh** outMesh)
{
    if ((data == NULL) || (outMesh == NULL) || (data->Positions == NULL) || (data->Indices == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TerrainMesh_Create: data, its positions and indices, and outMesh must not be NULL.");
    }
    *outMesh = NULL;
    Error Result = CheckCounts(data, description);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckVertices(data, description);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckTriangles(data, description);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t Vertices = data->VertexCount;
    size_t Triangles = data->TriangleCount;
    TerrainMesh* Self = Memory_Allocate(sizeof(TerrainMesh));
    Memory_Zero(Self, sizeof(*Self));
    Self->_vertexCount = data->VertexCount;
    Self->_triangleCount = data->TriangleCount;
    Self->_materialCount = data->MaterialCount;
    Self->_densityChannelCount = data->DensityChannelCount;
    Self->_normalMode = data->NormalMode;
    Self->_chunkSize = data->ChunkSize;
    Self->_positions = Memory_Allocate(Vertices * sizeof(Vector3));
    Self->_normals = Memory_Allocate(Vertices * sizeof(Vector3));
    Self->_weights = Memory_Allocate(Vertices * TERRAIN_MAX_MATERIALS);
    Self->_densities = Memory_Allocate(Vertices * TERRAIN_MAX_DENSITY_CHANNELS);
    Self->_groups = Memory_Allocate(Vertices * sizeof(uint32_t));
    Self->_nextInGroup = Memory_Allocate(Vertices * sizeof(uint32_t));
    Self->_groupTriangleStarts = Memory_Allocate((Vertices + 1U) * sizeof(uint32_t));
    Self->_groupTriangles = Memory_Allocate(Triangles * 3U * sizeof(uint32_t));
    Self->_isGroupDirty = Memory_Allocate(Vertices);
    Self->_triangles = Memory_Allocate(Triangles * sizeof(TerrainTriangle));
    Self->_isTriangleDirty = Memory_Allocate(Triangles);
    Memory_Zero(Self->_isGroupDirty, Vertices);
    Memory_Zero(Self->_isTriangleDirty, Triangles);
    GenericBuffer_AllocateVariable(&Self->_dirtyGroups, INITIAL_DIRTY_CAPACITY, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Self->_dirtyTriangles, INITIAL_DIRTY_CAPACITY, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&Self->_dirtyChunks, INITIAL_DIRTY_CAPACITY, sizeof(uint32_t));

    for (size_t Vertex = 0U; Vertex < Vertices; Vertex++)
    {
        Vector3 Position = data->Positions[Vertex];
        Self->_positions[Vertex] = (Vector3){ TerrainMeshInternal_Snap(Position.x), TerrainMeshInternal_Snap(Position.y),
            TerrainMeshInternal_Snap(Position.z) };
        Self->_normals[Vertex] = (data->NormalMode == TerrainNormalMode_Imported)
            ? NormalizeImported(data->Normals[Vertex]) : (Vector3){ 0.0f, 1.0f, 0.0f };
    }
    if (data->Weights != NULL)
    {
        Memory_Copy(data->Weights, Self->_weights, Vertices * TERRAIN_MAX_MATERIALS);
    }
    else
    {
        Memory_Zero(Self->_weights, Vertices * TERRAIN_MAX_MATERIALS);
    }
    if (data->Densities != NULL)
    {
        Memory_Copy(data->Densities, Self->_densities, Vertices * TERRAIN_MAX_DENSITY_CHANNELS);
    }
    else
    {
        Memory_Zero(Self->_densities, Vertices * TERRAIN_MAX_DENSITY_CHANNELS);
    }
    for (size_t Triangle = 0U; Triangle < Triangles; Triangle++)
    {
        TerrainTriangle* Target = &Self->_triangles[Triangle];
        uint8_t Flags = (data->TriangleFlags != NULL) ? data->TriangleFlags[Triangle] : 0U;
        bool OverridesMaterial = (Flags & TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL) != 0U;
        *Target = (TerrainTriangle){ ._indices = { data->Indices[Triangle * 3U], data->Indices[(Triangle * 3U) + 1U],
            data->Indices[(Triangle * 3U) + 2U] }, ._flags = Flags,
            ._material = (OverridesMaterial && (data->TriangleMaterials != NULL)) ? data->TriangleMaterials[Triangle] : 0U,
            ._activeEdges = TERRAIN_ALL_EDGES_ACTIVE, ._reserved = 0U };
    }

    BuildGroups(Self);
    Result = CheckSnappedTriangles(Self, description);
    if (Result.Code != ErrorCode_Success)
    {
        TerrainMesh_Destroy(Self);
        return Result;
    }
    BuildGroupTriangles(Self);
    if (Self->_normalMode == TerrainNormalMode_Smooth)
    {
        for (uint32_t Vertex = 0U; Vertex < Self->_vertexCount; Vertex++)
        {
            if (Self->_groups[Vertex] == Vertex)
            {
                UpdateGroupNormal(Self, Vertex);
            }
        }
    }
    for (uint32_t Triangle = 0U; Triangle < Self->_triangleCount; Triangle++)
    {
        Self->_triangles[Triangle]._activeEdges = ComputeActiveEdges(Self, Triangle);
    }
    uint32_t ChunkCount = 0U;
    if (!TerrainMeshInternal_BuildChunks(Self, &ChunkCount))
    {
        double ChunkSize = (double)Self->_chunkSize;
        TerrainMesh_Destroy(Self);
        return TerrainMeshInternal_Fail(description, ErrorCode_InvalidAssetData, "its triangles fill %" PRIu32 " chunks "
            "of %g m; a terrain has at most %u (larger chunks hold more).", ChunkCount, ChunkSize,
            TERRAIN_MESH_MAX_CHUNKS);
    }
    *outMesh = Self;
    return Error_CreateSuccess();
}

void TerrainMesh_Destroy(TerrainMesh* self)
{
    if (self == NULL)
    {
        return;
    }
    TerrainMeshInternal_DestroyChunks(self);
    Memory_Free(self->_positions);
    Memory_Free(self->_normals);
    Memory_Free(self->_weights);
    Memory_Free(self->_densities);
    Memory_Free(self->_groups);
    Memory_Free(self->_nextInGroup);
    Memory_Free(self->_groupTriangleStarts);
    Memory_Free(self->_groupTriangles);
    Memory_Free(self->_isGroupDirty);
    Memory_Free(self->_triangles);
    Memory_Free(self->_isTriangleDirty);
    Memory_Free(self->_dirtyGroups._data);
    Memory_Free(self->_dirtyTriangles._data);
    Memory_Free(self->_dirtyChunks._data);
    Memory_Free(self);
}

Error TerrainMesh_SetVertexPositions(TerrainMesh* self, const uint32_t* vertices, const Vector3* positions,
    size_t count)
{
    Error Result = CheckIndices(self, vertices, count, (self != NULL) ? self->_vertexCount : 0U,
        "TerrainMesh_SetVertexPositions");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((positions == NULL) && (count > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TerrainMesh_SetVertexPositions: positions must not be NULL.");
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        Vector3 Position = positions[Index];
        if (!TerrainMeshInternal_IsInExtent(Position.x) || !TerrainMeshInternal_IsInExtent(Position.y)
            || !TerrainMeshInternal_IsInExtent(Position.z))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_SetVertexPositions: (%g, %g, %g) is not "
                "finite or not within %g m of the terrain's origin.", (double)Position.x, (double)Position.y,
                (double)Position.z, TERRAIN_MESH_EXTENT);
        }
    }

    // Applied, then checked around every moved vertex; on a refusal the old positions come back.
    Vector3* Old = Memory_Allocate(((count > 0U) ? count : 1U) * sizeof(Vector3));
    for (size_t Index = 0U; Index < count; Index++)
    {
        Old[Index] = self->_positions[vertices[Index]];
        Vector3 Position = positions[Index];
        self->_positions[vertices[Index]] = (Vector3){ TerrainMeshInternal_Snap(Position.x),
            TerrainMeshInternal_Snap(Position.y), TerrainMeshInternal_Snap(Position.z) };
    }
    for (size_t Index = 0U; (Index < count) && (Result.Code == ErrorCode_Success); Index++)
    {
        uint32_t Group = self->_groups[vertices[Index]];
        for (uint32_t Entry = self->_groupTriangleStarts[Group]; Entry < self->_groupTriangleStarts[Group + 1U]; Entry++)
        {
            uint32_t TriangleIndex = self->_groupTriangles[Entry];
            const uint32_t* Corners = self->_triangles[TriangleIndex]._indices;
            Vector3 A = self->_positions[Corners[0]];
            Vector3 B = self->_positions[Corners[1]];
            Vector3 C = self->_positions[Corners[2]];
            if (ArePositionsEqual(A, B) || ArePositionsEqual(B, C) || ArePositionsEqual(C, A))
            {
                Result = Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_SetVertexPositions: the move would "
                    "put two corners of triangle %" PRIu32 " at one position.", TriangleIndex);
                break;
            }
            if (GetLongestEdge(A, B, C) > TERRAIN_MESH_EXTENT)
            {
                Result = Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_SetVertexPositions: the move would "
                    "give triangle %" PRIu32 " an edge longer than %g m.", TriangleIndex, TERRAIN_MESH_EXTENT);
                break;
            }
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        for (size_t Index = count; Index > 0U; Index--)
        {
            self->_positions[vertices[Index - 1U]] = Old[Index - 1U];
        }
        Memory_Free(Old);
        return Result;
    }
    Memory_Free(Old);
    for (size_t Index = 0U; Index < count; Index++)
    {
        MarkAroundGroup(self, self->_groups[vertices[Index]]);
    }
    // TODO: the groups are kept as they were built (the editor's topology edits will group again): moving one
    // vertex of a group separates it from the others at that position (an imported hard edge's other side), and moving
    // a vertex onto another group's position does not join them, so the normals and edges there keep the old grouping.
    // Until then the editor moves every vertex at a position together and keeps positions apart.
    return Error_CreateSuccess();
}

Error TerrainMesh_SetVertexWeights(TerrainMesh* self, const uint32_t* vertices, const uint8_t* weights, size_t count)
{
    Error Result = CheckIndices(self, vertices, count, (self != NULL) ? self->_vertexCount : 0U,
        "TerrainMesh_SetVertexWeights");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((weights == NULL) && (count > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TerrainMesh_SetVertexWeights: weights must not be NULL.");
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        for (uint32_t Entry = self->_materialCount; Entry < TERRAIN_MAX_MATERIALS; Entry++)
        {
            if (weights[(Index * TERRAIN_MAX_MATERIALS) + Entry] != 0U)
            {
                return Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_SetVertexWeights: material %" PRIu32
                    " is past the palette's %" PRIu32 ".", Entry, self->_materialCount);
            }
        }
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        Memory_Copy(&weights[Index * TERRAIN_MAX_MATERIALS],
            &self->_weights[(size_t)vertices[Index] * TERRAIN_MAX_MATERIALS], TERRAIN_MAX_MATERIALS);
        MarkVertexChunks(self, vertices[Index]);
    }
    return Error_CreateSuccess();
}

Error TerrainMesh_SetVertexDensities(TerrainMesh* self, const uint32_t* vertices, const uint8_t* densities,
    size_t count)
{
    Error Result = CheckIndices(self, vertices, count, (self != NULL) ? self->_vertexCount : 0U,
        "TerrainMesh_SetVertexDensities");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((densities == NULL) && (count > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TerrainMesh_SetVertexDensities: densities must not be NULL.");
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        for (uint32_t Channel = self->_densityChannelCount; Channel < TERRAIN_MAX_DENSITY_CHANNELS; Channel++)
        {
            if (densities[(Index * TERRAIN_MAX_DENSITY_CHANNELS) + Channel] != 0U)
            {
                return Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_SetVertexDensities: channel %" PRIu32
                    " is past the terrain's %" PRIu32 ".", Channel, self->_densityChannelCount);
            }
        }
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        Memory_Copy(&densities[Index * TERRAIN_MAX_DENSITY_CHANNELS],
            &self->_densities[(size_t)vertices[Index] * TERRAIN_MAX_DENSITY_CHANNELS], TERRAIN_MAX_DENSITY_CHANNELS);
        MarkVertexChunks(self, vertices[Index]);
    }
    return Error_CreateSuccess();
}

Error TerrainMesh_SetTriangleFlags(TerrainMesh* self, const uint32_t* triangles, const uint8_t* flags,
    const uint8_t* materials, size_t count)
{
    Error Result = CheckIndices(self, triangles, count, (self != NULL) ? self->_triangleCount : 0U,
        "TerrainMesh_SetTriangleFlags");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (((flags == NULL) || (materials == NULL)) && (count > 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TerrainMesh_SetTriangleFlags: flags and materials must not be NULL.");
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        if ((flags[Index] & (uint8_t)~TERRAIN_TRIANGLE_FLAGS_KNOWN) != 0U)
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_SetTriangleFlags: 0x%02x holds unknown "
                "flags.", (unsigned)flags[Index]);
        }
        if (((flags[Index] & TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL) != 0U)
            && ((uint32_t)materials[Index] >= self->_materialCount))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange, "TerrainMesh_SetTriangleFlags: material %u is past the "
                "palette's %" PRIu32 ".", (unsigned)materials[Index], self->_materialCount);
        }
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        TerrainTriangle* Triangle = &self->_triangles[triangles[Index]];
        bool WasHole = IsHole(Triangle);
        Triangle->_flags = flags[Index];
        Triangle->_material = ((flags[Index] & TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL) != 0U) ? materials[Index] : 0U;
        MarkTriangle(self, triangles[Index]);
        if (WasHole != IsHole(Triangle))
        {
            for (uint32_t Corner = 0U; Corner < 3U; Corner++)
            {
                MarkGroupAndItsTriangles(self, self->_groups[Triangle->_indices[Corner]]);
            }
        }
    }
    return Error_CreateSuccess();
}

uint32_t TerrainMesh_RebuildDirtyChunks(TerrainMesh* self)
{
    const uint32_t* Groups = (const uint32_t*)(const void*)self->_dirtyGroups._data;
    for (size_t Index = 0U; Index < self->_dirtyGroups._count; Index++)
    {
        if (self->_normalMode == TerrainNormalMode_Smooth)
        {
            UpdateGroupNormal(self, Groups[Index]);
        }
        self->_isGroupDirty[Groups[Index]] = 0U;
    }
    const uint32_t* Triangles = (const uint32_t*)(const void*)self->_dirtyTriangles._data;
    for (size_t Index = 0U; Index < self->_dirtyTriangles._count; Index++)
    {
        self->_triangles[Triangles[Index]]._activeEdges = ComputeActiveEdges(self, Triangles[Index]);
        self->_isTriangleDirty[Triangles[Index]] = 0U;
    }
    const uint32_t* Chunks = (const uint32_t*)(const void*)self->_dirtyChunks._data;
    uint32_t Rebuilt = (uint32_t)self->_dirtyChunks._count;
    TerrainMeshInternal_RebuildChunks(self, Chunks, self->_dirtyChunks._count);
    for (size_t Index = 0U; Index < self->_dirtyChunks._count; Index++)
    {
        self->_isChunkDirty[Chunks[Index]] = 0U;
    }
    GenericBuffer_Clear(&self->_dirtyGroups);
    GenericBuffer_Clear(&self->_dirtyTriangles);
    GenericBuffer_Clear(&self->_dirtyChunks);
    return Rebuilt;
}

void TerrainMesh_GetCell(const TerrainMesh* self, Vector3Double point, int32_t* outCellX, int32_t* outCellZ)
{
    double Size = (double)self->_chunkSize;
    double X = floor(point.X / Size);
    double Z = floor(point.Z / Size);
    // Far outside the extent (a query, not a vertex) the cell is clamped; no triangle lies there.
    *outCellX = (int32_t)fmin(fmax(X, (double)INT32_MIN), (double)INT32_MAX);
    *outCellZ = (int32_t)fmin(fmax(Z, (double)INT32_MIN), (double)INT32_MAX);
}

bool TerrainMesh_TryFindChunk(const TerrainMesh* self, int32_t cellX, int32_t cellZ, uint32_t* outIndex)
{
    uint32_t Low = 0U;
    uint32_t High = self->_chunkCount;
    while (Low < High)
    {
        uint32_t Middle = Low + ((High - Low) / 2U);
        const TerrainChunk* Chunk = &self->_chunks[Middle];
        bool IsBefore = (Chunk->_cellZ < cellZ) || ((Chunk->_cellZ == cellZ) && (Chunk->_cellX < cellX));
        if (IsBefore)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    if ((Low < self->_chunkCount) && (self->_chunks[Low]._cellX == cellX) && (self->_chunks[Low]._cellZ == cellZ))
    {
        *outIndex = Low;
        return true;
    }
    return false;
}
