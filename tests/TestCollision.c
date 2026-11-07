#include "TestCollision.h"
#include <math.h>
#include <stdbool.h>
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** The corners of each slice: outer, middle and inner at the bottom, then the same at the top. */
#define SLICE_CORNERS 6U
/** The triangles of a slab (fans of four on the top and the bottom, two on each side) and of the two end caps. */
#define SLAB_TRIANGLES 12U
#define CAP_TRIANGLES 4U


// Static functions.
static float GetSliceAngle(uint32_t slabCount, float slice)
{
    return (slice - (0.5f * (float)slabCount)) * TEST_HEDGE_BEND_DEGREES * DEG2RAD;
}

/* The bottom of the centreline at @p angle, and the convex side's outward normal there. */
static void GetFrame(float angle, Vector3* outCenter, Vector3* outOutward)
{
    *outOutward = (Vector3){ sinf(angle), 0.0f, -cosf(angle) };
    *outCenter = (Vector3){ TEST_HEDGE_RADIUS * sinf(angle), 0.0f, TEST_HEDGE_RADIUS * (1.0f - cosf(angle)) };
}

/* The index of a slice's corner: @p corner 0 outer, 1 middle, 2 inner, at its bottom or its top. */
static unsigned short GetCorner(uint32_t slice, uint32_t corner, bool isTop)
{
    return (unsigned short)((slice * SLICE_CORNERS) + (isTop ? 3U : 0U) + corner);
}

static Vector3 GetVertex(const float* vertices, unsigned short index)
{
    const float* Vertex = &vertices[(size_t)index * 3U];
    return (Vector3){ Vertex[0], Vertex[1], Vertex[2] };
}

/* Appends a triangle wound so that its front faces @p outward. */
static void AddTriangle(unsigned short* indices, uint32_t* count, const float* vertices, unsigned short a,
    unsigned short b, unsigned short c, Vector3 outward)
{
    Vector3 A = GetVertex(vertices, a);
    Vector3 Normal = Vector3CrossProduct(Vector3Subtract(GetVertex(vertices, b), A),
        Vector3Subtract(GetVertex(vertices, c), A));
    bool IsTurned = Vector3DotProduct(Normal, outward) < 0.0f;
    indices[(*count)++] = a;
    indices[(*count)++] = IsTurned ? c : b;
    indices[(*count)++] = IsTurned ? b : c;
}


// Functions.
CollisionMesh* TestCollision_CreateHedge(TestRun* run, uint32_t slabCount)
{
    uint32_t SliceCount = slabCount + 1U;
    uint32_t VertexCount = SliceCount * SLICE_CORNERS;
    uint32_t TriangleCount = (slabCount * SLAB_TRIANGLES) + CAP_TRIANGLES;
    float* Vertices = Memory_Allocate((size_t)VertexCount * 3U * sizeof(float));
    unsigned short* Indices = Memory_Allocate((size_t)TriangleCount * 3U * sizeof(unsigned short));
    float Half = 0.5f * TEST_HEDGE_WIDTH;
    for (uint32_t Slice = 0U; Slice < SliceCount; Slice++)
    {
        Vector3 Center;
        Vector3 Outward;
        GetFrame(GetSliceAngle(slabCount, (float)Slice), &Center, &Outward);
        // The middle corner wanders across the slice as a cut diagonal does, never onto the outline.
        float Offsets[3] = { Half, 0.3f * cosf(1.3f * (float)Slice), -Half };
        for (uint32_t Level = 0U; Level < 2U; Level++)
        {
            for (uint32_t Which = 0U; Which < 3U; Which++)
            {
                Vector3 Point = Vector3Add(Center, Vector3Scale(Outward, Offsets[Which]));
                float* Vertex = &Vertices[(size_t)GetCorner(Slice, Which, Level == 1U) * 3U];
                Vertex[0] = Point.x;
                Vertex[1] = (Level == 1U) ? TEST_HEDGE_HEIGHT : 0.0f;
                Vertex[2] = Point.z;
            }
        }
    }

    uint32_t Count = 0U;
    for (uint32_t Slab = 0U; Slab < slabCount; Slab++)
    {
        Vector3 Center;
        Vector3 Outward;
        GetFrame(GetSliceAngle(slabCount, (float)Slab + 0.5f), &Center, &Outward);
        uint32_t First = Slab;
        uint32_t Next = Slab + 1U;
        for (uint32_t Level = 0U; Level < 2U; Level++)
        {
            bool IsTop = Level == 1U;
            Vector3 Face = { 0.0f, IsTop ? 1.0f : -1.0f, 0.0f };
            // The slab's part of each source triangle (on either side of the diagonal through the middle corners), a quad
            // fanned from its first corner: the outer part's second triangle has only inner edges and meets the outline
            // at the outer corner alone.
            unsigned short Outer = GetCorner(First, 0U, IsTop);
            unsigned short Middle = GetCorner(First, 1U, IsTop);
            AddTriangle(Indices, &Count, Vertices, Outer, GetCorner(Next, 0U, IsTop), GetCorner(Next, 1U, IsTop), Face);
            AddTriangle(Indices, &Count, Vertices, Outer, GetCorner(Next, 1U, IsTop), Middle, Face);
            AddTriangle(Indices, &Count, Vertices, Middle, GetCorner(Next, 1U, IsTop), GetCorner(Next, 2U, IsTop), Face);
            AddTriangle(Indices, &Count, Vertices, Middle, GetCorner(Next, 2U, IsTop), GetCorner(First, 2U, IsTop), Face);
        }
        // The convex side (the outer corners) and the concave one (the inner corners), a quad each.
        for (uint32_t Which = 0U; Which < 3U; Which += 2U)
        {
            Vector3 Face = (Which == 0U) ? Outward : Vector3Negate(Outward);
            AddTriangle(Indices, &Count, Vertices, GetCorner(First, Which, false), GetCorner(Next, Which, false),
                GetCorner(Next, Which, true), Face);
            AddTriangle(Indices, &Count, Vertices, GetCorner(First, Which, false), GetCorner(Next, Which, true),
                GetCorner(First, Which, true), Face);
        }
    }
    // The end caps, a quad each across the first and the last slice.
    for (uint32_t End = 0U; End < 2U; End++)
    {
        uint32_t Slice = (End == 0U) ? 0U : slabCount;
        float Angle = GetSliceAngle(slabCount, (float)Slice);
        Vector3 Along = { cosf(Angle), 0.0f, sinf(Angle) };
        Vector3 Face = (End == 0U) ? Vector3Negate(Along) : Along;
        AddTriangle(Indices, &Count, Vertices, GetCorner(Slice, 0U, false), GetCorner(Slice, 2U, false),
            GetCorner(Slice, 2U, true), Face);
        AddTriangle(Indices, &Count, Vertices, GetCorner(Slice, 0U, false), GetCorner(Slice, 2U, true),
            GetCorner(Slice, 0U, true), Face);
    }

    CollisionMeshPart Part = { .Vertices = Vertices, .VertexCount = VertexCount, .Indices = Indices,
        .TriangleCount = Count / 3U, .MaterialIndex = 0 };
    CollisionMesh* Built = NULL;
    bool IsBuilt = TEST_CHECK_SUCCESS(run, CollisionMesh_Create(&Part, 1U, MatrixIdentity(), &Built));
    Memory_Free(Vertices);
    Memory_Free(Indices);
    return IsBuilt ? Built : NULL;
}

void TestCollision_GetHedgeFrame(uint32_t slabCount, float along, Vector3* outCenter, Vector3* outOutward)
{
    GetFrame(GetSliceAngle(slabCount, along * (float)slabCount), outCenter, outOutward);
}
