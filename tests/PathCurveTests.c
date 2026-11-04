#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "PathCurve.h"
#include "TestFramework.h"
#include "TestSuites.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** How near a position must lie where a test computes it by other means, in metres. */
#define PATH_TEST_NEAR 1e-6
/** How near a direction's component must be, for what floats round to. */
#define PATH_TEST_DIRECTION_NEAR 1e-5f
/** The steps a dense walk along a path takes per metre. */
#define PATH_TEST_DENSE_STEPS 200.0


// Static functions: helpers.
static PathNode MakeNode(double x, double y, double z, PathNodeMode mode)
{
    PathNode Node = PathNode_CreateDefault();
    Node.Position = Vector3Double_Create(x, y, z);
    Node.Mode = mode;
    return Node;
}

static PathNode MakeBezier(double x, double y, double z, Vector3 inHandle, Vector3 outHandle)
{
    PathNode Node = MakeNode(x, y, z, PathNodeMode_Bezier);
    Node.InHandle = inHandle;
    Node.OutHandle = outHandle;
    return Node;
}

static bool Build(TestRun* run, PathCurve* curve, const PathNode* nodes, size_t count, bool isClosed, PathUpMode upMode)
{
    PathCurve_Construct(curve);
    return TEST_CHECK_SUCCESS(run, PathCurve_Build(curve, nodes, count, isClosed, upMode));
}

static bool IsNear(Vector3Double a, Vector3Double b, double margin)
{
    return (fabs(a.X - b.X) <= margin) && (fabs(a.Y - b.Y) <= margin) && (fabs(a.Z - b.Z) <= margin);
}

static bool IsDirectionNear(Vector3 a, Vector3 b, float margin)
{
    return (fabsf(a.x - b.x) <= margin) && (fabsf(a.y - b.y) <= margin) && (fabsf(a.z - b.z) <= margin);
}

static Vector3Double ToDouble(Vector3 value)
{
    return Vector3Double_Create((double)value.x, (double)value.y, (double)value.z);
}

static PathCurvePoint PointAt(TestRun* run, const PathCurve* curve, double distance)
{
    PathCurvePoint Point;
    Memory_Zero(&Point, sizeof(Point));
    TEST_CHECK(run, PathCurve_GetPointAt(curve, distance, &Point));
    return Point;
}

/* Every value of a point is finite and its frame unit, at right angles and right-handed. */
static bool IsFrameValid(const PathCurvePoint* point)
{
    Vector3Double T = ToDouble(point->Tangent);
    Vector3Double U = ToDouble(point->Up);
    Vector3Double R = ToDouble(point->Right);
    Vector3Double Cross = Vector3Double_Cross(T, U);
    return Vector3Double_IsFinite(point->Position) && (fabs(Vector3Double_Length(T) - 1.0) < 1e-5)
        && (fabs(Vector3Double_Length(U) - 1.0) < 1e-5) && (fabs(Vector3Double_Length(R) - 1.0) < 1e-5)
        && (fabs(Vector3Double_Dot(T, U)) < 1e-5) && (fabs(Vector3Double_Dot(T, R)) < 1e-5)
        && (fabs(Vector3Double_Dot(U, R)) < 1e-5) && IsNear(Cross, R, 1e-5);
}

/* The length of a dense walk of chords between two distances. */
static double GetChordSum(TestRun* run, const PathCurve* curve, double from, double to)
{
    size_t Steps = (size_t)fmax(ceil((to - from) * PATH_TEST_DENSE_STEPS), 8.0);
    double Sum = 0.0;
    Vector3Double Last = PointAt(run, curve, from).Position;
    for (size_t Step = 1U; Step <= Steps; Step++)
    {
        Vector3Double Here = PointAt(run, curve, from + ((to - from) * (double)Step / (double)Steps)).Position;
        Sum += Vector3Double_Distance(Last, Here);
        Last = Here;
    }
    return Sum;
}

/* A seeded generator of doubles in [0, 1). */
static double NextRandom(uint32_t* state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return (double)(*state >> 8) / (double)(1U << 24);
}

/* A path of @p count nodes wandering over 200 m, in random modes, with handles, rolls, widths and scales. */
static void MakeRandomNodes(uint32_t* state, PathNode* outNodes, size_t count)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        double X = (double)Index * 12.0 + (NextRandom(state) * 8.0);
        double Y = NextRandom(state) * 6.0;
        double Z = (NextRandom(state) - 0.5) * 40.0;
        double Pick = NextRandom(state);
        PathNodeMode Mode = (Pick < 0.5) ? PathNodeMode_Smooth : ((Pick < 0.75) ? PathNodeMode_Corner : PathNodeMode_Bezier);
        outNodes[Index] = MakeNode(X, Y, Z, Mode);
        outNodes[Index].InHandle = (Vector3){ (float)(NextRandom(state) * -6.0), (float)(NextRandom(state) - 0.5),
            (float)(NextRandom(state) * 6.0 - 3.0) };
        outNodes[Index].OutHandle = (Vector3){ (float)(NextRandom(state) * 6.0), (float)(NextRandom(state) - 0.5),
            (float)(NextRandom(state) * 6.0 - 3.0) };
        outNodes[Index].Roll = (float)((NextRandom(state) - 0.5) * 2.0);
        outNodes[Index].Width = (float)(NextRandom(state) * 3.0);
        outNodes[Index].Scale = (float)(NextRandom(state) * 2.0);
    }
}

/* Centripetal Catmull-Rom by the Barry-Goldman pyramid, independent of the module's Bezier form. */
static Vector3Double EvaluateBarryGoldman(Vector3Double p0, Vector3Double p1, Vector3Double p2, Vector3Double p3, double amount)
{
    double T0 = 0.0;
    double T1 = T0 + sqrt(Vector3Double_Distance(p0, p1));
    double T2 = T1 + sqrt(Vector3Double_Distance(p1, p2));
    double T3 = T2 + sqrt(Vector3Double_Distance(p2, p3));
    double T = T1 + ((T2 - T1) * amount);
    Vector3Double A1 = Vector3Double_Lerp(p0, p1, (T - T0) / (T1 - T0));
    Vector3Double A2 = Vector3Double_Lerp(p1, p2, (T - T1) / (T2 - T1));
    Vector3Double A3 = Vector3Double_Lerp(p2, p3, (T - T2) / (T3 - T2));
    Vector3Double B1 = Vector3Double_Lerp(A1, A2, (T - T0) / (T2 - T0));
    Vector3Double B2 = Vector3Double_Lerp(A2, A3, (T - T1) / (T3 - T1));
    return Vector3Double_Lerp(B1, B2, (T - T1) / (T2 - T1));
}

static Vector3Double EvaluateBezier(Vector3Double p0, Vector3Double p1, Vector3Double p2, Vector3Double p3, double amount)
{
    Vector3Double A = Vector3Double_Lerp(p0, p1, amount);
    Vector3Double B = Vector3Double_Lerp(p1, p2, amount);
    Vector3Double C = Vector3Double_Lerp(p2, p3, amount);
    return Vector3Double_Lerp(Vector3Double_Lerp(A, B, amount), Vector3Double_Lerp(B, C, amount), amount);
}

/* The curve lies within @p margin of a point computed by other means. */
static bool IsOnCurve(TestRun* run, const PathCurve* curve, Vector3Double point, double margin)
{
    PathCurvePoint Nearest;
    double Separation = 0.0;
    TEST_CHECK(run, PathCurve_FindNearest(curve, point, false, &Nearest, &Separation));
    return Separation <= margin;
}


// Static functions: test cases.
static void EmptyAndSingleNodeCurves(TestRun* run)
{
    PathCurve Curve;
    PathCurve_Construct(&Curve);
    PathCurvePoint Point;
    GenericBuffer Samples;
    GenericBuffer_AllocateVariable(&Samples, 0U, sizeof(double));
    PathSampleOptions Options = PathSampleOptions_Create(1.0);
    TEST_CHECK(run, (PathCurve_GetLength(&Curve) == 0.0) && (PathCurve_GetNodeCount(&Curve) == 0U)
        && (PathCurve_GetSectionCount(&Curve) == 0U) && !PathCurve_GetPointAt(&Curve, 0.0, &Point)
        && !PathCurve_FindNearest(&Curve, Vector3Double_Zero(), false, &Point, NULL)
        && !PathCurve_IsPointInside(&Curve, 0.0, 0.0) && !PathCurve_GetSectionPoint(&Curve, 0U, 0.0, &Point));
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count == 0U);
    TEST_CHECK_ERROR(run, PathCurve_WriteSectionSamples(&Curve, 0U, &Options, &Samples), ErrorCode_IllegalArgument);

    PathNode Node = MakeNode(1.5, 2.5, -3.5, PathNodeMode_Smooth);
    Node.Roll = 0.5f;
    Node.Width = 2.0f;
    Node.Scale = 3.0f;
    for (int IsClosed = 0; IsClosed < 2; IsClosed++)
    {
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, &Node, 1U, IsClosed != 0, PathUpMode_RotationMinimizing));
        // One section of no segments, so the section a point names always exists.
        TEST_CHECK(run, (PathCurve_GetLength(&Curve) == 0.0) && (PathCurve_GetNodeCount(&Curve) == 1U)
            && (PathCurve_GetSegmentCount(&Curve) == 0U) && (PathCurve_GetSectionCount(&Curve) == 1U));
        const PathCurveSection* Section = PathCurve_GetSection(&Curve, 0U);
        TEST_CHECK(run, (Section->FirstNode == 0U) && (Section->SegmentCount == 0U) && (Section->Length == 0.0));
        Point = PointAt(run, &Curve, 40.0);
        TEST_CHECK(run, IsNear(Point.Position, Node.Position, 0.0) && (Point.Distance == 0.0) && (Point.Width == 2.0f)
            && (Point.Scale == 3.0f) && (Point.Roll == 0.5f) && IsFrameValid(&Point) && (Point.Section == 0U));
        // Along +Z with a positive roll, the up leans to the right, which is -X.
        TEST_CHECK(run, (Point.Up.x < -0.4f) && (Point.Up.y > 0.8f) && (Point.Right.x < -0.8f) && (Point.Right.y < -0.4f));
        PathCurvePoint InSection;
        TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 0U, 3.0, &InSection) && IsNear(InSection.Position, Node.Position, 0.0)
            && IsFrameValid(&InSection));
        GenericBuffer_Clear(&Samples);
        TEST_CHECK_SUCCESS(run, PathCurve_WriteSectionSamples(&Curve, 0U, &Options, &Samples));
        TEST_CHECK(run, (Samples._count == 1U) && (((double*)Samples._data)[0] == 0.0));
        double Separation = 0.0;
        TEST_CHECK(run, PathCurve_FindNearest(&Curve, Vector3Double_Create(1.5, 5.5, -3.5), false, &Point, &Separation)
            && (fabs(Separation - 3.0) < 1e-12));
        TEST_CHECK(run, PathCurve_FindNearest(&Curve, Vector3Double_Create(1.5, 5.5, -3.5), true, &Point, &Separation)
            && (Separation == 0.0));
        GenericBuffer_Clear(&Samples);
        TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
        TEST_CHECK(run, (Samples._count == 1U) && (((double*)Samples._data)[0] == 0.0));
        TEST_CHECK(run, !PathCurve_IsPointInside(&Curve, 1.5, -3.5));
    }
    Memory_Free(Samples._data);
    PathCurve_Deconstruct(&Curve);
    PathCurve_Deconstruct(&Curve);
    PathCurve_Deconstruct(NULL);
}

static void CornerNodesMakeAPolyline(TestRun* run)
{
    PathNode Nodes[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 0.0, 5.0, PathNodeMode_Corner), MakeNode(10.0, 3.0, 5.0, PathNodeMode_Corner) };
    PathCurve Curve;
    if (!Build(run, &Curve, Nodes, 4U, false, PathUpMode_Upright))
    {
        return;
    }
    TEST_CHECK(run, (PathCurve_GetLength(&Curve) == 18.0) && (PathCurve_GetSegmentCount(&Curve) == 3U)
        && (PathCurve_GetNodeDistance(&Curve, 0U) == 0.0) && (PathCurve_GetNodeDistance(&Curve, 1U) == 10.0)
        && (PathCurve_GetNodeDistance(&Curve, 2U) == 15.0) && (PathCurve_GetNodeDistance(&Curve, 3U) == 18.0)
        && (PathCurve_GetNodeDistance(&Curve, 4U) == 0.0));
    PathCurvePoint Point = PointAt(run, &Curve, 4.0);
    TEST_CHECK(run, IsNear(Point.Position, Vector3Double_Create(4.0, 0.0, 0.0), 1e-12)
        && IsDirectionNear(Point.Tangent, (Vector3){ 1.0f, 0.0f, 0.0f }, 0.0f) && (Point.Segment == 0U)
        && (Point.Section == 0U));
    // +Y up and running along +X, the right is +Z: looking along +X from above, +Z is to the right.
    TEST_CHECK(run, IsDirectionNear(Point.Up, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f)
        && IsDirectionNear(Point.Right, (Vector3){ 0.0f, 0.0f, 1.0f }, 0.0f));

    // At a corner the point leaves it: the tangent of the segment after it; clamped past either end.
    Point = PointAt(run, &Curve, 10.0);
    TEST_CHECK(run, IsNear(Point.Position, Nodes[1].Position, 0.0) && (Point.Segment == 1U)
        && IsDirectionNear(Point.Tangent, (Vector3){ 0.0f, 0.0f, 1.0f }, 0.0f));
    Point = PointAt(run, &Curve, -5.0);
    TEST_CHECK(run, IsNear(Point.Position, Nodes[0].Position, 0.0) && (Point.Distance == 0.0));
    Point = PointAt(run, &Curve, 1000.0);
    TEST_CHECK(run, IsNear(Point.Position, Nodes[3].Position, 0.0) && (Point.Distance == 18.0)
        && IsDirectionNear(Point.Tangent, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f) && IsFrameValid(&Point));

    // Each corner starts a section, and a section's last point keeps the tangent it arrived with.
    TEST_CHECK(run, PathCurve_GetSectionCount(&Curve) == 3U);
    const PathCurveSection* Section = PathCurve_GetSection(&Curve, 0U);
    TEST_CHECK(run, (Section->FirstNode == 0U) && (Section->SegmentCount == 1U) && (Section->StartDistance == 0.0)
        && (Section->Length == 10.0));
    Section = PathCurve_GetSection(&Curve, 1U);
    TEST_CHECK(run, (Section->FirstNode == 1U) && (Section->StartDistance == 10.0) && (Section->Length == 5.0));
    PathCurvePoint End;
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 0U, 10.0, &End) && IsNear(End.Position, Nodes[1].Position, 0.0)
        && IsDirectionNear(End.Tangent, (Vector3){ 1.0f, 0.0f, 0.0f }, 0.0f) && (End.Section == 0U));
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 1U, 999.0, &End) && IsNear(End.Position, Nodes[2].Position, 0.0)
        && IsDirectionNear(End.Tangent, (Vector3){ 0.0f, 0.0f, 1.0f }, 0.0f) && (End.Distance == 15.0));
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 1U, -1.0, &End) && IsNear(End.Position, Nodes[1].Position, 0.0));
    TEST_CHECK(run, !PathCurve_GetSectionPoint(&Curve, 3U, 0.0, &End));
    PathCurve_Deconstruct(&Curve);
}

static void SmoothNodesFollowCentripetalCatmullRom(TestRun* run)
{
    // Unevenly spaced on purpose: where uniform Catmull-Rom makes a cusp or a loop, and chordal overshoots.
    PathNode Nodes[6] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(40.0, 2.0, 1.0, PathNodeMode_Smooth),
        MakeNode(40.5, 2.0, 1.2, PathNodeMode_Smooth), MakeNode(41.0, 2.5, 30.0, PathNodeMode_Smooth),
        MakeNode(5.0, 0.0, 31.0, PathNodeMode_Smooth), MakeNode(0.0, 1.0, 60.0, PathNodeMode_Smooth) };
    PathCurve Curve;
    if (!Build(run, &Curve, Nodes, 6U, false, PathUpMode_Upright))
    {
        return;
    }
    TEST_CHECK(run, PathCurve_GetSectionCount(&Curve) == 1U);
    for (uint32_t Node = 0U; Node < 6U; Node++)
    {
        PathCurvePoint Point = PointAt(run, &Curve, PathCurve_GetNodeDistance(&Curve, Node));
        TEST_CHECK(run, IsNear(Point.Position, Nodes[Node].Position, 1e-9));
    }
    // The inner segments against an independent evaluation; the ends reflect their neighbour.
    for (uint32_t Segment = 1U; Segment < 4U; Segment++)
    {
        for (int Step = 0; Step <= 16; Step++)
        {
            Vector3Double Expected = EvaluateBarryGoldman(Nodes[Segment - 1U].Position, Nodes[Segment].Position,
                Nodes[Segment + 1U].Position, Nodes[Segment + 2U].Position, (double)Step / 16.0);
            TEST_CHECK(run, IsOnCurve(run, &Curve, Expected, PATH_TEST_NEAR));
        }
    }
    Vector3Double Reflected = Vector3Double_Subtract(Vector3Double_Scale(Nodes[0].Position, 2.0), Nodes[1].Position);
    for (int Step = 0; Step <= 16; Step++)
    {
        Vector3Double Expected = EvaluateBarryGoldman(Reflected, Nodes[0].Position, Nodes[1].Position, Nodes[2].Position,
            (double)Step / 16.0);
        TEST_CHECK(run, IsOnCurve(run, &Curve, Expected, PATH_TEST_NEAR));
    }

    Vector3Double ReflectedLast = Vector3Double_Subtract(Vector3Double_Scale(Nodes[5].Position, 2.0), Nodes[4].Position);
    for (int Step = 0; Step <= 16; Step++)
    {
        Vector3Double Expected = EvaluateBarryGoldman(Nodes[3].Position, Nodes[4].Position, Nodes[5].Position, ReflectedLast,
            (double)Step / 16.0);
        TEST_CHECK(run, IsOnCurve(run, &Curve, Expected, PATH_TEST_NEAR));
    }

    // No kink at a node, and never a turn back: the tangent changes a little per step everywhere.
    double Length = PathCurve_GetLength(&Curve);
    Vector3 Last = PointAt(run, &Curve, 0.0).Tangent;
    for (double Distance = 0.01; Distance < Length; Distance += 0.01)
    {
        PathCurvePoint Point = PointAt(run, &Curve, Distance);
        double Cos = Vector3Double_Dot(ToDouble(Last), ToDouble(Point.Tangent));
        TEST_CHECK(run, IsFrameValid(&Point) && (Cos > 0.9));
        Last = Point.Tangent;
    }
    PathCurve_Deconstruct(&Curve);
}

/* A segment touching a corner is a straight line; its smooth neighbour bends away along it. */
static void SmoothNodesBendAwayFromCornersWithoutAKink(TestRun* run)
{
    PathNode Nodes[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 0.0, 0.0, PathNodeMode_Smooth),
        MakeNode(30.0, 0.0, 10.0, PathNodeMode_Smooth), MakeNode(30.0, 0.0, 30.0, PathNodeMode_Corner) };
    PathCurve Curve;
    if (!Build(run, &Curve, Nodes, 4U, false, PathUpMode_Upright))
    {
        return;
    }
    double B = PathCurve_GetNodeDistance(&Curve, 1U);
    double C = PathCurve_GetNodeDistance(&Curve, 2U);
    double End = PathCurve_GetLength(&Curve);
    TEST_CHECK(run, fabs(B - 20.0) < 1e-12);
    TEST_CHECK(run, fabs((End - C) - 20.0) < 1e-12);
    TEST_CHECK(run, IsNear(PointAt(run, &Curve, 7.0).Position, Vector3Double_Create(7.0, 0.0, 0.0), 1e-12));
    TEST_CHECK(run, IsNear(PointAt(run, &Curve, C + 5.0).Position, Vector3Double_Create(30.0, 0.0, 15.0), 1e-9));

    // The bend leaves B along +X and reaches C along +Z, and no direction jumps across either node.
    PathCurvePoint Leaving = PointAt(run, &Curve, B);
    TEST_CHECK(run, IsDirectionNear(Leaving.Tangent, (Vector3){ 1.0f, 0.0f, 0.0f }, PATH_TEST_DIRECTION_NEAR));
    PathCurvePoint Arriving = PointAt(run, &Curve, C - 1e-7);
    TEST_CHECK(run, IsDirectionNear(Arriving.Tangent, (Vector3){ 0.0f, 0.0f, 1.0f }, 1e-4f));
    TEST_CHECK(run, PointAt(run, &Curve, 0.5 * (B + C)).Position.Z > 0.5);

    // Exactly: the bend's first control point a third of its chord along the straight it leaves, its last a third of the
    // chord back along the straight it reaches.
    double Chord = sqrt((10.0 * 10.0) + (10.0 * 10.0));
    for (int Step = 0; Step <= 16; Step++)
    {
        Vector3Double Expected = EvaluateBezier(Nodes[1].Position, Vector3Double_Create(20.0 + (Chord / 3.0), 0.0, 0.0),
            Vector3Double_Create(30.0, 0.0, 10.0 - (Chord / 3.0)), Nodes[2].Position, (double)Step / 16.0);
        TEST_CHECK(run, IsOnCurve(run, &Curve, Expected, PATH_TEST_NEAR));
    }

    // Only corners start sections here.
    TEST_CHECK(run, PathCurve_GetSectionCount(&Curve) == 1U);

    // A smooth node between two corners has straights on both sides and so its one kink.
    PathNode Between[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(5.0, 0.0, 0.0, PathNodeMode_Smooth),
        MakeNode(5.0, 0.0, 5.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Between, 3U, false, PathUpMode_Upright));
    TEST_CHECK(run, (fabs(PathCurve_GetLength(&Curve) - 10.0) < 1e-12) && (PathCurve_GetSectionCount(&Curve) == 1U));
    TEST_CHECK(run, IsNear(PointAt(run, &Curve, 7.0).Position, Vector3Double_Create(5.0, 0.0, 2.0), 1e-12));
    TEST_CHECK(run, IsDirectionNear(PointAt(run, &Curve, 4.0).Tangent, (Vector3){ 1.0f, 0.0f, 0.0f }, 0.0f)
        && IsDirectionNear(PointAt(run, &Curve, 6.0).Tangent, (Vector3){ 0.0f, 0.0f, 1.0f }, 0.0f));
    PathCurve_Deconstruct(&Curve);
}

static void BezierNodesFollowTheirHandles(TestRun* run)
{
    Vector3 None = { 0.0f, 0.0f, 0.0f };
    PathNode Pair[2] = { MakeBezier(0.0, 0.0, 0.0, None, (Vector3){ 5.0f, 0.0f, 5.0f }),
        MakeBezier(20.0, 3.0, 0.0, (Vector3){ -5.0f, 0.0f, -5.0f }, None) };
    PathCurve Curve;
    if (!Build(run, &Curve, Pair, 2U, false, PathUpMode_Upright))
    {
        return;
    }
    for (int Step = 0; Step <= 20; Step++)
    {
        Vector3Double Expected = EvaluateBezier(Pair[0].Position, Vector3Double_Create(5.0, 0.0, 5.0),
            Vector3Double_Create(15.0, 3.0, -5.0), Pair[1].Position, (double)Step / 20.0);
        TEST_CHECK(run, IsOnCurve(run, &Curve, Expected, PATH_TEST_NEAR));
    }

    // A handle shapes the segment even next to a corner, which has none: one control point stays at the corner.
    PathNode FromCorner[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeBezier(20.0, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 8.0f }, None) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, FromCorner, 2U, false, PathUpMode_Upright));
    for (int Step = 0; Step <= 20; Step++)
    {
        Vector3Double Expected = EvaluateBezier(FromCorner[0].Position, FromCorner[0].Position,
            Vector3Double_Create(20.0, 0.0, 8.0), FromCorner[1].Position, (double)Step / 20.0);
        TEST_CHECK(run, IsOnCurve(run, &Curve, Expected, PATH_TEST_NEAR));
    }
    // Its first control point sits on its start, so the tangent there comes from the next one.
    PathCurvePoint Start = PointAt(run, &Curve, 0.0);
    double Across = sqrt((20.0 * 20.0) + (8.0 * 8.0));
    TEST_CHECK(run, IsDirectionNear(Start.Tangent, (Vector3){ (float)(20.0 / Across), 0.0f, (float)(8.0 / Across) }, 1e-3f));
    PathNode ToCorner[2] = { MakeBezier(0.0, 0.0, 0.0, None, (Vector3){ 0.0f, 0.0f, 8.0f }),
        MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, ToCorner, 2U, false, PathUpMode_Upright));
    for (int Step = 0; Step <= 20; Step++)
    {
        Vector3Double Expected = EvaluateBezier(ToCorner[0].Position, Vector3Double_Create(0.0, 0.0, 8.0),
            ToCorner[1].Position, ToCorner[1].Position, (double)Step / 20.0);
        TEST_CHECK(run, IsOnCurve(run, &Curve, Expected, PATH_TEST_NEAR));
    }

    // Handles are ignored on a smooth or corner node, however large.
    PathNode Ignored[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
    Ignored[0].OutHandle = (Vector3){ 100.0f, 100.0f, 100.0f };
    Ignored[1].InHandle = (Vector3){ -100.0f, 100.0f, 100.0f };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Ignored, 2U, false, PathUpMode_Upright));
    TEST_CHECK(run, PathCurve_GetLength(&Curve) == 10.0);

    // A smooth node beside a bezier one keeps its own tangent on both sides.
    PathNode Mixed[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth),
        MakeBezier(20.0, 0.0, 10.0, (Vector3){ -2.0f, 0.0f, -6.0f }, None) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Mixed, 3U, false, PathUpMode_Upright));
    double Node = PathCurve_GetNodeDistance(&Curve, 1U);
    TEST_CHECK(run, IsDirectionNear(PointAt(run, &Curve, Node - 1e-7).Tangent, PointAt(run, &Curve, Node + 1e-7).Tangent,
        1e-3f));

    // A cusp and a loop: no NaN, a unit tangent everywhere, and arc length still follows the curve.
    PathNode Cusp[2] = { MakeBezier(0.0, 0.0, 0.0, None, (Vector3){ 30.0f, 0.0f, 0.0f }),
        MakeBezier(0.0, 0.0, 0.0, (Vector3){ 30.0f, 0.0f, 0.0f }, None) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Cusp, 2U, false, PathUpMode_RotationMinimizing));
    TEST_CHECK(run, PathCurve_GetLength(&Curve) > 0.0);
    for (double Distance = 0.0; Distance <= PathCurve_GetLength(&Curve); Distance += 0.25)
    {
        PathCurvePoint Point = PointAt(run, &Curve, Distance);
        TEST_CHECK(run, IsFrameValid(&Point));
    }
    PathNode Loop[2] = { MakeBezier(0.0, 0.0, 0.0, None, (Vector3){ 20.0f, 0.0f, 30.0f }),
        MakeBezier(0.0, 0.0, 0.0, (Vector3){ -20.0f, 0.0f, 30.0f }, None) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Loop, 2U, false, PathUpMode_Upright));
    double LoopLength = PathCurve_GetLength(&Curve);
    TEST_CHECK(run, fabs(GetChordSum(run, &Curve, 0.0, LoopLength) - LoopLength) < 1e-3);
    PathCurve_Deconstruct(&Curve);
}

static void ArcLengthIsExact(TestRun* run)
{
    // A quarter circle of radius 100 as a Bezier (kappa = 0.5522847...): within 0.03 % of the true arc.
    float Kappa = 55.228475f;
    PathNode Quarter[2] = { MakeBezier(100.0, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, Kappa }),
        MakeBezier(0.0, 0.0, 100.0, (Vector3){ Kappa, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }) };
    PathCurve Curve;
    if (!Build(run, &Curve, Quarter, 2U, false, PathUpMode_Upright))
    {
        return;
    }
    double Length = PathCurve_GetLength(&Curve);
    TEST_CHECK(run, fabs(Length - (50.0 * PI)) < (50.0 * PI * 3e-4));
    TEST_CHECK(run, fabs(GetChordSum(run, &Curve, 0.0, Length) - Length) < 1e-6);
    // Equal distances are equal lengths of curve, however fast the parameter runs there.
    for (int Step = 0; Step < 50; Step++)
    {
        double From = (double)Step * Length / 50.0;
        double To = (double)(Step + 1) * Length / 50.0;
        TEST_CHECK(run, fabs(GetChordSum(run, &Curve, From, To) - (Length / 50.0)) < 1e-6);
    }
    // Random paths: the same, and a point's distance comes back through its nearest point.
    uint32_t State = 11U;
    for (int Path = 0; Path < 8; Path++)
    {
        PathNode Nodes[12];
        MakeRandomNodes(&State, Nodes, 12U);
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Nodes, 12U, (Path % 2) == 1, PathUpMode_Upright));
        Length = PathCurve_GetLength(&Curve);
        // Node to node: a walk across a corner would cut it, and that is the walk's error, not the length's.
        for (uint32_t Segment = 0U; Segment < PathCurve_GetSegmentCount(&Curve); Segment++)
        {
            double From = PathCurve_GetNodeDistance(&Curve, Segment);
            double To = ((Segment + 1U) < 12U) ? PathCurve_GetNodeDistance(&Curve, Segment + 1U) : Length;
            TEST_CHECK(run, fabs(GetChordSum(run, &Curve, From, To) - (To - From)) < ((To - From) * 1e-4));
        }
        for (int Step = 1; Step < 64; Step++)
        {
            double Distance = NextRandom(&State) * Length;
            PathCurvePoint Point = PointAt(run, &Curve, Distance);
            TEST_CHECK(run, (fabs(Point.Distance - Distance) < 1e-9) && IsFrameValid(&Point));
            TEST_CHECK(run, BoundingBoxDouble_ContainsPoint(PathCurve_GetBounds(&Curve), Point.Position));
            PathCurvePoint Nearest;
            double Separation = 1.0;
            TEST_CHECK(run, PathCurve_FindNearest(&Curve, Point.Position, false, &Nearest, &Separation)
                && (Separation < 1e-5));
            // A crossing of the path may answer with its other pass, but then at the same place.
            TEST_CHECK(run, IsNear(Nearest.Position, Point.Position, 1e-5));
        }
    }
    PathCurve_Deconstruct(&Curve);
}

static void ClosedPathsWrapAndMeet(TestRun* run)
{
    PathNode Nodes[4] = { MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(0.0, 0.0, 10.0, PathNodeMode_Smooth),
        MakeNode(-10.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(0.0, 0.0, -10.0, PathNodeMode_Smooth) };
    PathCurve Curve;
    if (!Build(run, &Curve, Nodes, 4U, true, PathUpMode_Upright))
    {
        return;
    }
    double Length = PathCurve_GetLength(&Curve);
    TEST_CHECK(run, (PathCurve_GetSegmentCount(&Curve) == 4U) && PathCurve_IsClosed(&Curve));
    // Each quarter is the spline through its neighbours, which wrap around the end.
    double ExpectedLength = 0.0;
    for (int Quarter = 0; Quarter < 4; Quarter++)
    {
        Vector3Double Last = Nodes[Quarter].Position;
        for (int Step = 1; Step <= 4000; Step++)
        {
            Vector3Double Here = EvaluateBarryGoldman(Nodes[(Quarter + 3) % 4].Position, Nodes[Quarter].Position,
                Nodes[(Quarter + 1) % 4].Position, Nodes[(Quarter + 2) % 4].Position, (double)Step / 4000.0);
            ExpectedLength += Vector3Double_Distance(Last, Here);
            Last = Here;
        }
    }
    TEST_CHECK(run, fabs(Length - ExpectedLength) < 1e-5);
    // Wraps both ways: the same point a whole number of lengths on, and no kink where it closes.
    for (double Distance = -100.0; Distance < 100.0; Distance += 7.3)
    {
        PathCurvePoint Point = PointAt(run, &Curve, Distance);
        PathCurvePoint Again = PointAt(run, &Curve, Distance + (3.0 * Length));
        TEST_CHECK(run, IsNear(Point.Position, Again.Position, 1e-8) && (Point.Distance >= 0.0) && (Point.Distance < Length));
    }
    TEST_CHECK(run, IsNear(PointAt(run, &Curve, Length).Position, Nodes[0].Position, 1e-12));
    TEST_CHECK(run, IsDirectionNear(PointAt(run, &Curve, Length - 1e-7).Tangent, PointAt(run, &Curve, 1e-7).Tangent, 1e-3f));

    // One loop from node 0 while nothing starts a section; its last point arrives back at node 0.
    TEST_CHECK(run, PathCurve_GetSectionCount(&Curve) == 1U);
    const PathCurveSection* Section = PathCurve_GetSection(&Curve, 0U);
    TEST_CHECK(run, (Section->FirstNode == 0U) && (Section->SegmentCount == 4U) && (Section->Length == Length));
    PathCurvePoint End;
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 0U, Length, &End) && IsNear(End.Position, Nodes[0].Position, 1e-9)
        && (End.Segment == 3U));

    // A start on node 2 alone: a whole loop from there, through the end of the list.
    Nodes[2].StartsSection = true;
    Nodes[2].SkipsSection = true;
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Nodes, 4U, true, PathUpMode_Upright));
    TEST_CHECK(run, PathCurve_GetSectionCount(&Curve) == 1U);
    Section = PathCurve_GetSection(&Curve, 0U);
    TEST_CHECK(run, (Section->FirstNode == 2U) && (Section->SegmentCount == 4U) && Section->IsSkipped
        && (fabs(Section->Length - Length) < 1e-9));
    double NodeTwo = PathCurve_GetNodeDistance(&Curve, 2U);
    TEST_CHECK(run, fabs(Section->StartDistance - NodeTwo) < 1e-12);
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 0U, 0.0, &End) && IsNear(End.Position, Nodes[2].Position, 1e-12));
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 0U, Section->Length, &End)
        && IsNear(End.Position, Nodes[2].Position, 1e-9) && (End.Segment == 1U));
    // Half way round from node 2 is node 0, and a point's distance there has wrapped.
    double ToNodeZero = Length - NodeTwo;
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 0U, ToNodeZero, &End) && IsNear(End.Position, Nodes[0].Position, 1e-9)
        && (End.Distance < 1e-9));
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 0U, ToNodeZero + 1.0, &End)
        && IsNear(End.Position, PointAt(run, &Curve, 1.0).Position, 1e-9) && (fabs(End.Distance - 1.0) < 1e-9));

    // A closed square of corners: four sections, the last ending back at node 0 with its own tangent.
    PathNode Square[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Square, 4U, true, PathUpMode_Upright));
    TEST_CHECK(run, (PathCurve_GetLength(&Curve) == 40.0) && (PathCurve_GetSectionCount(&Curve) == 4U));
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 3U, 10.0, &End) && IsNear(End.Position, Square[0].Position, 0.0)
        && IsDirectionNear(End.Tangent, (Vector3){ 0.0f, 0.0f, -1.0f }, 0.0f) && (End.Section == 3U));
    TEST_CHECK(run, IsNear(PointAt(run, &Curve, 35.0).Position, Vector3Double_Create(0.0, 0.0, 5.0), 1e-12));

    // Two closed nodes: there and back over one straight line, no NaN.
    PathNode Two[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Two, 2U, true, PathUpMode_RotationMinimizing));
    TEST_CHECK(run, fabs(PathCurve_GetLength(&Curve) - 20.0) < 1e-9);
    for (double Distance = 0.0; Distance < 20.0; Distance += 0.5)
    {
        PathCurvePoint Point = PointAt(run, &Curve, Distance);
        TEST_CHECK(run, IsFrameValid(&Point));
    }
    TEST_CHECK(run, !PathCurve_IsPointInside(&Curve, 5.0, 0.0));
    PathCurve_Deconstruct(&Curve);
}

static void SectionsHoldTheirFirstNodesSettings(TestRun* run)
{
    PathNode Nodes[6];
    for (int Index = 0; Index < 6; Index++)
    {
        Nodes[Index] = MakeNode(10.0 * (double)Index, 0.0, 0.0, PathNodeMode_Smooth);
    }
    Nodes[0].SkipsSection = false;
    Nodes[1].SkipsSection = true; // no start here: nothing reads it
    Nodes[2].StartsSection = true;
    Nodes[2].SkipsSection = true;
    Nodes[3].Mode = PathNodeMode_Corner;
    Nodes[3].OverridesSnapsToTerrain = true;
    Nodes[3].SnapsToTerrain = false;
    Nodes[5].StartsSection = true; // the last node of an open path starts nothing
    PathCurve Curve;
    if (!Build(run, &Curve, Nodes, 6U, false, PathUpMode_Upright))
    {
        return;
    }
    TEST_CHECK(run, PathCurve_GetSectionCount(&Curve) == 3U);
    const PathCurveSection* First = PathCurve_GetSection(&Curve, 0U);
    const PathCurveSection* Gate = PathCurve_GetSection(&Curve, 1U);
    const PathCurveSection* Bridge = PathCurve_GetSection(&Curve, 2U);
    TEST_CHECK(run, (First->FirstNode == 0U) && (First->SegmentCount == 2U) && !First->IsSkipped
        && !First->OverridesSnapsToTerrain);
    TEST_CHECK(run, (Gate->FirstNode == 2U) && (Gate->SegmentCount == 1U) && Gate->IsSkipped);
    TEST_CHECK(run, (Bridge->FirstNode == 3U) && (Bridge->SegmentCount == 2U) && !Bridge->IsSkipped
        && Bridge->OverridesSnapsToTerrain && !Bridge->SnapsToTerrain);
    TEST_CHECK(run, fabs((Gate->StartDistance + Gate->Length) - Bridge->StartDistance) < 1e-12);
    TEST_CHECK(run, fabs((Bridge->StartDistance + Bridge->Length) - PathCurve_GetLength(&Curve)) < 1e-12);
    TEST_CHECK(run, PathCurveSection_ResolveSnapsToTerrain(First, true) && !PathCurveSection_ResolveSnapsToTerrain(First, false)
        && !PathCurveSection_ResolveSnapsToTerrain(Bridge, true));

    // A point names its section; at a section's start it already lies in it.
    TEST_CHECK(run, (PointAt(run, &Curve, 15.0).Section == 0U) && (PointAt(run, &Curve, Gate->StartDistance).Section == 1U)
        && (PointAt(run, &Curve, Gate->StartDistance + 1.0).Section == 1U) && (PointAt(run, &Curve, 45.0).Section == 2U)
        && (PointAt(run, &Curve, 1000.0).Section == 2U));

    // Two corners at one place make a section of no length, which no point falls in.
    PathNode Doubled[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(5.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(5.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(5.0, 0.0, 5.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Doubled, 4U, false, PathUpMode_Upright));
    TEST_CHECK(run, (PathCurve_GetSectionCount(&Curve) == 3U) && (PathCurve_GetSection(&Curve, 1U)->Length == 0.0));
    TEST_CHECK(run, PointAt(run, &Curve, 5.0).Section == 2U);
    PathCurvePoint Point;
    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, 1U, 0.0, &Point) && IsFrameValid(&Point)
        && IsNear(Point.Position, Doubled[1].Position, 0.0));
    PathCurve_Deconstruct(&Curve);
}

/* A walk over a step of the curve: how far it leaves the straight line between the step's ends, and the smallest cosine of its
 * tangent's turn from the step's start. */
static void WalkStep(TestRun* run, const PathCurve* curve, double from, double to, double* outDeviation, double* outCos)
{
    PathCurvePoint First = PointAt(run, curve, from + 1e-6);
    Vector3Double Start = PointAt(run, curve, from).Position;
    Vector3Double End = PointAt(run, curve, to).Position;
    Vector3Double Along = Vector3Double_Subtract(End, Start);
    double Squared = Vector3Double_LengthSquared(Along);
    *outDeviation = 0.0;
    *outCos = 1.0;
    for (int Step = 1; Step < 48; Step++)
    {
        PathCurvePoint Here = PointAt(run, curve, from + (((to - from) * (double)Step) / 48.0));
        double Share = (Squared > 0.0)
            ? fmin(fmax(Vector3Double_Dot(Vector3Double_Subtract(Here.Position, Start), Along) / Squared, 0.0), 1.0) : 0.0;
        *outDeviation = fmax(*outDeviation, Vector3Double_Distance(Here.Position, Vector3Double_AddScaled(Start, Along, Share)));
        *outCos = fmin(*outCos, Vector3Double_Dot(ToDouble(First.Tangent), ToDouble(Here.Tangent)));
    }
}

/* The samples ascend, hold every node, and no two neighbours are further apart than the options allow. */
static void CheckSamples(TestRun* run, const PathCurve* curve, const GenericBuffer* samples,
    const PathSampleOptions* options)
{
    const double* Distances = (const double*)samples->_data;
    size_t Count = samples->_count;
    TEST_CHECK(run, (Count >= 1U) && (Distances[0] == 0.0) && (Distances[Count - 1U] == PathCurve_GetLength(curve)));
    size_t NextNode = 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        while ((NextNode < PathCurve_GetNodeCount(curve))
            && (PathCurve_GetNodeDistance(curve, (uint32_t)NextNode) <= Distances[Index]))
        {
            TEST_CHECK(run, PathCurve_GetNodeDistance(curve, (uint32_t)NextNode) == Distances[Index]);
            NextNode++;
        }
        if (Index == 0U)
        {
            continue;
        }
        double From = Distances[Index - 1U];
        double To = Distances[Index];
        TEST_CHECK(run, (To > From) && ((To - From) <= (options->Spacing + 1e-9)));
        // Within a segment: the curve stays near the chord and the tangent turns little.
        PathCurvePoint A = PointAt(run, curve, From + 1e-6);
        PathCurvePoint B = PointAt(run, curve, To - 1e-6);
        bool IsAtTheFloor = (To - From) < (options->Spacing / 4000.0); // a cusp: halving stops there
        if ((A.Segment == B.Segment) && !IsAtTheFloor)
        {
            // By a walk across the step, not only at the points the writer looks at: within a little of the limits, since the
            // writer reads a step's middle and quarters and a curve's worst may lie just beside them.
            double Deviation = 0.0;
            double Cos = 1.0;
            WalkStep(run, curve, From, To, &Deviation, &Cos);
            TEST_CHECK(run, Deviation <= (options->MaxDeviation * 1.1));
            TEST_CHECK(run, Cos >= cos((double)options->MaxTurn * 1.5));
            TEST_CHECK(run, Vector3Double_Dot(ToDouble(A.Tangent), ToDouble(B.Tangent))
                >= (cos((double)options->MaxTurn) - 1e-3));
        }
    }
    TEST_CHECK(run, NextNode == PathCurve_GetNodeCount(curve));
}

static void SamplesFollowSpacingAndCurvature(TestRun* run)
{
    GenericBuffer Samples;
    GenericBuffer_AllocateVariable(&Samples, 0U, sizeof(double));
    PathCurve Curve;
    PathNode Straight[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(100.0, 0.0, 0.0, PathNodeMode_Corner) };
    if (!Build(run, &Curve, Straight, 2U, false, PathUpMode_Upright))
    {
        Memory_Free(Samples._data);
        return;
    }
    // A straight line takes the spacing and nothing more, whatever the deviation and the turn say.
    PathSampleOptions Options = PathSampleOptions_Create(3.0);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count == 35U);
    CheckSamples(run, &Curve, &Samples, &Options);
    GenericBuffer_Clear(&Samples);
    Options.Spacing = 1000.0;
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, (Samples._count == 2U) && (((double*)Samples._data)[1] == 100.0));
    // A writer appends: what the buffer held stays, and a second call goes on after it.
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, (Samples._count == 4U) && (((double*)Samples._data)[1] == 100.0) && (((double*)Samples._data)[2] == 0.0));

    // A curve takes more than its spacing asks where it bends; a gentler one, fewer.
    PathNode Tight[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(1.0, 0.0, 1.0, PathNodeMode_Smooth),
        MakeNode(2.0, 0.0, 0.0, PathNodeMode_Smooth) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Tight, 3U, false, PathUpMode_Upright));
    Options = PathSampleOptions_Create(10.0);
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    size_t TightCount = Samples._count;
    TEST_CHECK(run, TightCount > 12U);
    CheckSamples(run, &Curve, &Samples, &Options);
    PathNode Wide[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(100.0, 0.0, 100.0, PathNodeMode_Smooth),
        MakeNode(200.0, 0.0, 0.0, PathNodeMode_Smooth) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Wide, 3U, false, PathUpMode_Upright));
    Options.Spacing = 1000.0;
    Options.MaxDeviation = 1000.0;
    Options.MaxTurn = 3.2f;
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count == 3U); // nothing asks for more than the nodes

    // A symmetric S: its ends parallel and its middle on its chord, which alone would pass for straight, however gentle.
    PathNode S[2] = { MakeBezier(0.0, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 10.0f, 0.0f, 0.0f }),
        MakeBezier(30.0, 0.0, 10.0, (Vector3){ -10.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, S, 2U, false, PathUpMode_Upright));
    Options = PathSampleOptions_Create(1000.0);
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count > 8U);
    CheckSamples(run, &Curve, &Samples, &Options);
    // So gentle that no tangent turns as far as the limit: the deviation alone must catch it, at the quarters.
    PathNode Gentle[2] = { MakeBezier(0.0, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 100.0f, 0.0f, 0.0f }),
        MakeBezier(300.0, 0.0, 6.0, (Vector3){ -100.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Gentle, 2U, false, PathUpMode_Upright));
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count > 8U);
    CheckSamples(run, &Curve, &Samples, &Options);
    // So small that the curve never leaves its chord by a centimetre: the turn across its halves must catch it.
    PathNode Tiny2[2] = { MakeBezier(0.0, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.1f, 0.0f, 0.0f }),
        MakeBezier(0.3, 0.0, 0.05, (Vector3){ -0.1f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Tiny2, 2U, false, PathUpMode_Upright));
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count > 2U);
    CheckSamples(run, &Curve, &Samples, &Options);
    // Bent in its first quarter alone: within the limit at its middle and its last quarter, past it only there.
    PathNode Lopsided[2] = { MakeBezier(0.0, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.5f, 0.0f, 0.035f }),
        MakeBezier(1.5, 0.0, 0.0, (Vector3){ -0.5f, 0.0f, -0.01167f }, (Vector3){ 0.0f, 0.0f, 0.0f }) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Lopsided, 2U, false, PathUpMode_Upright));
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count > 2U);
    CheckSamples(run, &Curve, &Samples, &Options);
    // A wavy line of smooth nodes at a spacing longer than its nodes' gaps.
    PathNode Wavy[9];
    for (int Index = 0; Index < 9; Index++)
    {
        Wavy[Index] = MakeNode(10.0 * (double)Index, 0.0, ((Index % 2) != 0) ? 5.0 : 0.0, PathNodeMode_Smooth);
    }
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Wavy, 9U, false, PathUpMode_Upright));
    for (int Spacing = 0; Spacing < 3; Spacing++)
    {
        double Spacings[3] = { 5.0, 12.0, 100.0 };
        Options = PathSampleOptions_Create(Spacings[Spacing]);
        GenericBuffer_Clear(&Samples);
        TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
        CheckSamples(run, &Curve, &Samples, &Options);
    }
    // A tangent turns back on itself at the most: a limit past that asks for no more than one of a half turn does.
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Wide, 3U, false, PathUpMode_Upright));
    Options = PathSampleOptions_Create(1000.0);
    Options.MaxDeviation = 1000.0;
    for (int Turn = 0; Turn < 3; Turn++)
    {
        float Turns[3] = { PI, 3.2f, 6.28f };
        Options.MaxTurn = Turns[Turn];
        GenericBuffer_Clear(&Samples);
        TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
        TEST_CHECK(run, Samples._count == 3U);
    }

    // Segments too short to move the running distance: strictly rising, and a section still ends on exactly its length.
    PathNode Tiny[5] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(1.0e7, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(1.0e7, 1.0e-20, 0.0, PathNodeMode_Corner), MakeNode(1.0e7, 2.0e-20, 0.0, PathNodeMode_Smooth),
        MakeNode(1.0e7, 3.0e-20, 0.0, PathNodeMode_Smooth) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Tiny, 5U, false, PathUpMode_Upright));
    Options = PathSampleOptions_Create(5.0e6);
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    const double* Rising = (const double*)Samples._data;
    TEST_CHECK(run, (Samples._count == 3U) && (Rising[0] == 0.0) && (Rising[2] == 1.0e7));
    for (uint32_t Index = 0U; Index < PathCurve_GetSectionCount(&Curve); Index++)
    {
        GenericBuffer_Clear(&Samples);
        TEST_CHECK_SUCCESS(run, PathCurve_WriteSectionSamples(&Curve, Index, &Options, &Samples));
        const double* Local = (const double*)Samples._data;
        TEST_CHECK(run, (Samples._count >= 1U) && (Local[0] == 0.0)
            && (Local[Samples._count - 1U] == PathCurve_GetSection(&Curve, Index)->Length));
        for (size_t Sample = 1U; Sample < Samples._count; Sample++)
        {
            TEST_CHECK(run, Local[Sample] > Local[Sample - 1U]);
        }
    }

    // Random paths of every mode, both closures and both up modes.
    uint32_t State = 5U;
    for (int Path = 0; Path < 12; Path++)
    {
        PathNode Nodes[10];
        MakeRandomNodes(&State, Nodes, 10U);
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Nodes, 10U, (Path % 2) == 1,
            ((Path % 3) == 0) ? PathUpMode_RotationMinimizing : PathUpMode_Upright));
        Options = PathSampleOptions_Create(2.0);
        GenericBuffer_Clear(&Samples);
        TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
        CheckSamples(run, &Curve, &Samples, &Options);

        // Every section from 0 to exactly its length, the same places as the path's own distances.
        for (uint32_t Index = 0U; Index < PathCurve_GetSectionCount(&Curve); Index++)
        {
            const PathCurveSection* Section = PathCurve_GetSection(&Curve, Index);
            GenericBuffer_Clear(&Samples);
            TEST_CHECK_SUCCESS(run, PathCurve_WriteSectionSamples(&Curve, Index, &Options, &Samples));
            const double* Local = (const double*)Samples._data;
            TEST_CHECK(run, (Samples._count >= 1U) && (Local[0] == 0.0) && (Local[Samples._count - 1U] == Section->Length));
            for (size_t Sample = 0U; Sample < Samples._count; Sample++)
            {
                PathCurvePoint Point;
                TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, Index, Local[Sample], &Point));
                TEST_CHECK(run, (Point.Section == Index) || (Section->Length == 0.0));
                TEST_CHECK(run, (Sample == 0U) || (Local[Sample] > Local[Sample - 1U]));
                TEST_CHECK(run, IsNear(Point.Position, PointAt(run, &Curve, Section->StartDistance + Local[Sample]).Position,
                    1e-7));
            }
        }
    }

    // Refused: wrong arguments, and more samples than the limit, before any memory is taken for them.
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Straight, 2U, false, PathUpMode_Upright));
    GenericBuffer Wrong;
    GenericBuffer_AllocateVariable(&Wrong, 0U, sizeof(float));
    Options = PathSampleOptions_Create(1.0);
    TEST_CHECK_ERROR(run, PathCurve_WriteSamples(&Curve, &Options, &Wrong), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, PathCurve_WriteSamples(&Curve, NULL, &Samples), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, PathCurve_WriteSamples(&Curve, &Options, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, PathCurve_WriteSectionSamples(&Curve, 9U, &Options, &Samples), ErrorCode_IllegalArgument);
    PathSampleOptions Broken[4] = { PathSampleOptions_Create(0.0), Options, Options, PathSampleOptions_Create((double)NAN) };
    Broken[1].MaxDeviation = 0.0;
    Broken[2].MaxTurn = -1.0f;
    for (size_t Index = 0U; Index < 4U; Index++)
    {
        TEST_CHECK_ERROR(run, PathCurve_WriteSamples(&Curve, &Broken[Index], &Samples), ErrorCode_IllegalArgument);
    }
    Memory_Free(Wrong._data);
    PathNode Long[2] = { MakeNode(-1e7, 0.0, 0.0, PathNodeMode_Corner), MakeNode(1e7, 0.0, 0.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Long, 2U, false, PathUpMode_Upright));
    Options = PathSampleOptions_Create(0.01);
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_ERROR(run, PathCurve_WriteSamples(&Curve, &Options, &Samples), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, Samples._count <= 1U);
    Options = PathSampleOptions_Create(20.0);
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count == 1000001U);
    // The limit is per call: what the buffer already holds does not count.
    size_t Held = Samples._count;
    TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    TEST_CHECK(run, Samples._count == (2U * Held));
    // Exactly as many steps as the limit, plus the first sample: one too many, and never more than the limit written.
    Options = PathSampleOptions_Create(2e7 / (double)PATH_CURVE_MAX_SAMPLES);
    GenericBuffer_Clear(&Samples);
    TEST_CHECK_ERROR(run, PathCurve_WriteSamples(&Curve, &Options, &Samples), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, Samples._count == PATH_CURVE_MAX_SAMPLES);
    PathCurve_Deconstruct(&Curve);
    Memory_Free(Samples._data);
}

static void NearestPointBeatsEveryOtherPoint(TestRun* run)
{
    uint32_t State = 21U;
    PathCurve Curve;
    PathCurve_Construct(&Curve);
    for (int Path = 0; Path < 8; Path++)
    {
        PathNode Nodes[10];
        MakeRandomNodes(&State, Nodes, 10U);
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Nodes, 10U, (Path % 2) == 1, PathUpMode_Upright));
        double Length = PathCurve_GetLength(&Curve);
        for (int Query = 0; Query < 40; Query++)
        {
            Vector3Double Point = Vector3Double_Create((NextRandom(&State) * 160.0) - 20.0, (NextRandom(&State) * 40.0) - 20.0,
                (NextRandom(&State) * 120.0) - 60.0);
            for (int IgnoresHeight = 0; IgnoresHeight < 2; IgnoresHeight++)
            {
                PathCurvePoint Nearest;
                double Separation = -1.0;
                if (!TEST_CHECK(run, PathCurve_FindNearest(&Curve, Point, IgnoresHeight != 0, &Nearest, &Separation)))
                {
                    continue;
                }
                // Its separation is really to its point, and its point is the one its distance names.
                Vector3Double Toward = Vector3Double_Subtract(Nearest.Position, Point);
                if (IgnoresHeight != 0)
                {
                    Toward.Y = 0.0;
                }
                TEST_CHECK(run, fabs(Vector3Double_Length(Toward) - Separation) < 1e-9);
                TEST_CHECK(run, IsNear(PointAt(run, &Curve, Nearest.Distance).Position, Nearest.Position, 1e-6));
                TEST_CHECK(run, IsFrameValid(&Nearest));
                // No point of a dense walk comes closer than it does.
                double Best = INFINITY;
                for (double Walk = 0.0; Walk <= Length; Walk += 0.25)
                {
                    Vector3Double Toward2 = Vector3Double_Subtract(PointAt(run, &Curve, Walk).Position, Point);
                    if (IgnoresHeight != 0)
                    {
                        Toward2.Y = 0.0;
                    }
                    Best = fmin(Best, Vector3Double_Length(Toward2));
                }
                TEST_CHECK(run, Separation <= (Best + 1e-6));
                TEST_CHECK(run, Separation >= (Best - 0.2)); // the walk's steps are 0.25 long
            }
        }
    }
    // Two smooth nodes make a cubic along their chord. Against the line's own closed form, over enough points that a rounding
    // cannot hide: the best chord must always count, whatever its squared distance rounds to.
    PathNode Along[2] = { MakeNode(-0.60428753685394465, 10.195484390816581, -4.1091232272246003, PathNodeMode_Smooth),
        MakeNode(2.2744293755236855, -8.2904285223944463, 17.426619559861148, PathNodeMode_Smooth) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Along, 2U, false, PathUpMode_Upright));
    Vector3Double AlongChord = Vector3Double_Subtract(Along[1].Position, Along[0].Position);
    double AlongSquared = Vector3Double_LengthSquared(AlongChord);
    for (int Query = 0; Query < 4000; Query++)
    {
        Vector3Double Point = (Query == 0) ? Vector3Double_Create(-4.288505, 7.166511, 12.155042)
            : Vector3Double_Create((NextRandom(&State) * 60.0) - 30.0, (NextRandom(&State) * 60.0) - 30.0,
                (NextRandom(&State) * 60.0) - 30.0);
        double Share = fmin(fmax(Vector3Double_Dot(Vector3Double_Subtract(Point, Along[0].Position), AlongChord) / AlongSquared,
            0.0), 1.0);
        double Expected = Vector3Double_Distance(Point, Vector3Double_AddScaled(Along[0].Position, AlongChord, Share));
        PathCurvePoint Nearest;
        double Separation = 0.0;
        TEST_CHECK(run, PathCurve_FindNearest(&Curve, Point, false, &Nearest, &Separation)
            && (fabs(Separation - Expected) < 1e-6));
    }

    // Wild cubics that cross themselves and double back, so a point has several passes near it: the nearest of them answers.
    for (int Wild = 0; Wild < 800; Wild++)
    {
        double Size = ((Wild % 3) == 0) ? 1.0 : (((Wild % 3) == 1) ? 30.0 : 1000.0);
        Vector3 Out = { (float)((NextRandom(&State) - 0.5) * 3.0 * Size), (float)((NextRandom(&State) - 0.5) * Size),
            (float)((NextRandom(&State) - 0.5) * 3.0 * Size) };
        Vector3 In = { (float)((NextRandom(&State) - 0.5) * 3.0 * Size), (float)((NextRandom(&State) - 0.5) * Size),
            (float)((NextRandom(&State) - 0.5) * 3.0 * Size) };
        PathNode Pair[2] = { MakeBezier(0.0, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 0.0f }, Out),
            MakeBezier((NextRandom(&State) - 0.5) * Size, (NextRandom(&State) - 0.5) * Size * 0.2, (NextRandom(&State) - 0.5) * Size,
                In, (Vector3){ 0.0f, 0.0f, 0.0f }) };
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Pair, 2U, false, PathUpMode_Upright));
        Vector3Double P1 = ToDouble(Out);
        Vector3Double P2 = Vector3Double_Add(Pair[1].Position, ToDouble(In));
        for (int Query = 0; Query < 10; Query++)
        {
            PathCurvePoint On = PointAt(run, &Curve, NextRandom(&State) * PathCurve_GetLength(&Curve));
            Vector3Double Point = Vector3Double_Create(On.Position.X + ((NextRandom(&State) - 0.5) * 0.2 * Size),
                On.Position.Y + ((NextRandom(&State) - 0.5) * 0.2 * Size), On.Position.Z + ((NextRandom(&State) - 0.5) * 0.2 * Size));
            PathCurvePoint Nearest;
            double Separation = 0.0;
            TEST_CHECK(run, PathCurve_FindNearest(&Curve, Point, false, &Nearest, &Separation));
            double Best = INFINITY;
            for (int Step = 0; Step <= 2000; Step++)
            {
                Vector3Double Here = EvaluateBezier(Pair[0].Position, P1, P2, Pair[1].Position, (double)Step / 2000.0);
                Best = fmin(Best, Vector3Double_Distance(Here, Point));
            }
            TEST_CHECK(run, Separation <= (Best + (1e-5 * Size)));
        }
    }

    // On a tie the first along the path answers: the middle of a closed square.
    PathNode Square[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Square, 4U, true, PathUpMode_Upright));
    PathCurvePoint Nearest;
    double Separation = 0.0;
    TEST_CHECK(run, PathCurve_FindNearest(&Curve, Vector3Double_Create(5.0, 0.0, 5.0), false, &Nearest, &Separation)
        && (Nearest.Segment == 0U) && (fabs(Nearest.Distance - 5.0) < 1e-12) && (fabs(Separation - 5.0) < 1e-12));
    // A diamond's segments reach past its middle in their boxes, so no box can settle the tie: still the first.
    PathNode Diamond[4] = { MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner),
        MakeNode(-10.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, -10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Diamond, 4U, true, PathUpMode_Upright));
    TEST_CHECK(run, PathCurve_FindNearest(&Curve, Vector3Double_Zero(), false, &Nearest, &Separation) && (Nearest.Segment == 0U));
    // Past the ends of an open path, its end.
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Square, 2U, false, PathUpMode_Upright));
    TEST_CHECK(run, PathCurve_FindNearest(&Curve, Vector3Double_Create(25.0, 0.0, 0.0), false, &Nearest, NULL)
        && (Nearest.Distance == 10.0) && IsNear(Nearest.Position, Square[1].Position, 0.0));
    TEST_CHECK(run, !PathCurve_FindNearest(&Curve, Vector3Double_Create((double)NAN, 0.0, 0.0), false, &Nearest, NULL));
    PathCurve_Deconstruct(&Curve);
}

static void UprightFramesStayUpright(TestRun* run)
{
    uint32_t State = 31U;
    PathCurve Curve;
    PathCurve_Construct(&Curve);
    PathNode Nodes[12];
    MakeRandomNodes(&State, Nodes, 12U);
    for (int Index = 0; Index < 12; Index++)
    {
        Nodes[Index].Roll = 0.0f;
    }
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Nodes, 12U, false, PathUpMode_Upright));
    for (double Distance = 0.0; Distance < PathCurve_GetLength(&Curve); Distance += 0.5)
    {
        PathCurvePoint Point = PointAt(run, &Curve, Distance);
        TEST_CHECK(run, IsFrameValid(&Point) && (fabsf(Point.Right.y) < 1e-6f) && (Point.Up.y >= 0.0f));
    }

    // The roll turns the frame about the tangent, up toward the right.
    PathNode Banked[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner) };
    Banked[0].Roll = PI / 2.0f;
    Banked[1].Roll = PI / 2.0f;
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Banked, 2U, false, PathUpMode_Upright));
    PathCurvePoint Point = PointAt(run, &Curve, 5.0);
    TEST_CHECK(run, IsFrameValid(&Point) && (fabsf(Point.Roll - (PI / 2.0f)) < 1e-6f)
        && IsDirectionNear(Point.Up, (Vector3){ 0.0f, 0.0f, 1.0f }, PATH_TEST_DIRECTION_NEAR)
        && IsDirectionNear(Point.Right, (Vector3){ 0.0f, -1.0f, 0.0f }, PATH_TEST_DIRECTION_NEAR)
        && IsDirectionNear(Point.Tangent, (Vector3){ 1.0f, 0.0f, 0.0f }, PATH_TEST_DIRECTION_NEAR));

    // A run straight up keeps the right of what came before it, and of what comes after when it starts the path.
    PathNode Climb[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 10.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 10.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Climb, 4U, false, PathUpMode_Upright));
    Point = PointAt(run, &Curve, 15.0);
    TEST_CHECK(run, IsFrameValid(&Point) && IsDirectionNear(Point.Tangent, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f)
        && IsDirectionNear(Point.Right, (Vector3){ 0.0f, 0.0f, 1.0f }, 0.0f));
    PathNode Starts[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(0.0, 10.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 10.0, 0.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Starts, 3U, false, PathUpMode_Upright));
    Point = PointAt(run, &Curve, 5.0);
    TEST_CHECK(run, IsFrameValid(&Point) && IsDirectionNear(Point.Right, (Vector3){ 0.0f, 0.0f, 1.0f }, 0.0f));
    // After a curve the same: the right of the curve's end, not of its start, so the frame does not jump onto the run.
    PathNode Bending[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth),
        MakeNode(20.0, 0.0, 10.0, PathNodeMode_Smooth), MakeNode(20.0, 10.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Bending, 4U, false, PathUpMode_Upright));
    double Foot = PathCurve_GetNodeDistance(&Curve, 2U);
    PathCurvePoint Before = PointAt(run, &Curve, Foot - 1e-3);
    PathCurvePoint OnRun = PointAt(run, &Curve, Foot + 5.0);
    PathCurvePoint AtFoot = PointAt(run, &Curve, Foot);
    TEST_CHECK(run, IsFrameValid(&Before) && IsFrameValid(&OnRun) && IsFrameValid(&AtFoot));
    TEST_CHECK(run, IsDirectionNear(Before.Right, OnRun.Right, 1e-3f) && IsDirectionNear(AtFoot.Right, OnRun.Right, 1e-3f));
    // A bezier lead-in that turns a quarter in plan: the same.
    PathNode Quarter[3] = { MakeBezier(0.0, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 5.0f, 0.0f, 0.0f }),
        MakeBezier(10.0, 0.0, 10.0, (Vector3){ 0.0f, 0.0f, -5.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }),
        MakeNode(10.0, 10.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Quarter, 3U, false, PathUpMode_Upright));
    Foot = PathCurve_GetNodeDistance(&Curve, 1U);
    TEST_CHECK(run, IsDirectionNear(PointAt(run, &Curve, Foot - 1e-3).Right, PointAt(run, &Curve, Foot + 5.0).Right, 5e-3f));
    // A curve that ends straight up: the limit of its right, which it reaches from just before the end.
    PathNode Rising[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth),
        MakeBezier(20.0, 10.0, 0.0, (Vector3){ 0.0f, -5.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Rising, 3U, false, PathUpMode_Upright));
    double Top = PathCurve_GetLength(&Curve);
    PathCurvePoint NearTop = PointAt(run, &Curve, Top - 1e-3);
    PathCurvePoint AtTop = PointAt(run, &Curve, Top);
    TEST_CHECK(run, IsFrameValid(&AtTop) && IsDirectionNear(NearTop.Right, AtTop.Right, 5e-3f)
        && IsDirectionNear(AtTop.Right, (Vector3){ 0.0f, 0.0f, 1.0f }, 5e-3f));

    // Nothing but vertical: a frame all the same.
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Starts, 2U, false, PathUpMode_Upright));
    Point = PointAt(run, &Curve, 5.0);
    TEST_CHECK(run, IsFrameValid(&Point));
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Starts, 2U, false, PathUpMode_RotationMinimizing));
    Point = PointAt(run, &Curve, 5.0);
    TEST_CHECK(run, IsFrameValid(&Point));
    PathCurve_Deconstruct(&Curve);
}

/* The turn about the tangent between two neighbouring frames, after taking out the smallest rotation between tangents. */
static double GetFrameTwist(const PathCurvePoint* from, const PathCurvePoint* to)
{
    Vector3Double T0 = ToDouble(from->Tangent);
    Vector3Double T1 = ToDouble(to->Tangent);
    Vector3Double R0 = ToDouble(from->Right);
    Vector3Double Axis = Vector3Double_Cross(T0, T1);
    double Sin = Vector3Double_Length(Axis);
    double Cos = Vector3Double_Dot(T0, T1);
    Vector3Double Carried = R0;
    if (Sin > 1e-12)
    {
        Carried = Vector3Double_RotateByAxisAngle(R0, Vector3Double_Scale(Axis, 1.0 / Sin), atan2(Sin, Cos));
    }
    Vector3Double R1 = ToDouble(to->Right);
    return atan2(Vector3Double_Dot(Vector3Double_Cross(Carried, R1), T1), Vector3Double_Dot(Carried, R1));
}

static void RotationMinimizingFramesDoNotTwist(TestRun* run)
{
    // A helix of radius 10 climbing 5 m a turn: upright frames keep Right level, these turn with the curve instead.
    PathNode Helix[33];
    for (int Index = 0; Index < 33; Index++)
    {
        double Angle = (double)Index * (PI / 4.0);
        Helix[Index] = MakeNode(10.0 * cos(Angle), (double)Index * 5.0 / 8.0, 10.0 * sin(Angle), PathNodeMode_Smooth);
    }
    PathCurve Curve;
    if (!Build(run, &Curve, Helix, 33U, false, PathUpMode_RotationMinimizing))
    {
        return;
    }
    double Step = 0.05;
    double Length = PathCurve_GetLength(&Curve);
    PathCurvePoint Last = PointAt(run, &Curve, 0.0);
    double TotalTwist = 0.0;
    TEST_CHECK(run, fabsf(Last.Right.y) < 1e-6f); // starts upright
    for (double Distance = Step; Distance < Length; Distance += Step)
    {
        PathCurvePoint Point = PointAt(run, &Curve, Distance);
        TEST_CHECK(run, IsFrameValid(&Point));
        double Twist = GetFrameTwist(&Last, &Point);
        TotalTwist += Twist;
        TEST_CHECK(run, fabs(Twist) < 2e-3);
        Last = Point;
    }
    TEST_CHECK(run, fabs(TotalTwist) < 0.02); // over 12 turns, against 2 pi a turn for a frame that spins

    // It really differs from upright there: after one turn the right has climbed out of level.
    PathCurvePoint Mid = PointAt(run, &Curve, Length * 0.5);
    TEST_CHECK(run, fabsf(Mid.Right.y) > 1e-3f);

    // Across a corner the frame takes the smallest rotation: no twist about either tangent.
    PathNode Bent[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 10.0, 0.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Bent, 3U, false, PathUpMode_RotationMinimizing));
    PathCurvePoint Before = PointAt(run, &Curve, 9.0);
    PathCurvePoint After = PointAt(run, &Curve, 11.0);
    TEST_CHECK(run, IsFrameValid(&Before) && IsFrameValid(&After) && (fabs(GetFrameTwist(&Before, &After)) < 1e-5));
    // Right is +Z along +X, and turning from +X up to +Y about +Z leaves it there; Up turns from +Y to -X.
    TEST_CHECK(run, IsDirectionNear(After.Right, (Vector3){ 0.0f, 0.0f, 1.0f }, PATH_TEST_DIRECTION_NEAR)
        && IsDirectionNear(After.Up, (Vector3){ -1.0f, 0.0f, 0.0f }, PATH_TEST_DIRECTION_NEAR));
    // Doubling straight back turns about the up, so the up stays up.
    PathNode Back[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Back, 3U, false, PathUpMode_RotationMinimizing));
    After = PointAt(run, &Curve, 15.0);
    TEST_CHECK(run, IsFrameValid(&After) && IsDirectionNear(After.Up, (Vector3){ 0.0f, 1.0f, 0.0f }, PATH_TEST_DIRECTION_NEAR));

    // A closed loop out of plane has a leftover twist. The loop's own holonomy comes from a walk of smallest rotations here,
    // independent of the module's table: its frames must make up for exactly that, evenly along the length.
    PathNode Loop[8];
    for (int Index = 0; Index < 8; Index++)
    {
        double Angle = (double)Index * (PI / 4.0);
        Loop[Index] = MakeNode(20.0 * cos(Angle), 12.0 * (sin(Angle) + (0.6 * sin(2.0 * Angle))), 20.0 * sin(Angle),
            PathNodeMode_Smooth);
    }
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Loop, 8U, true, PathUpMode_RotationMinimizing));
    Length = PathCurve_GetLength(&Curve);
    PathCurvePoint Start = PointAt(run, &Curve, 0.0);
    PathCurvePoint End = PointAt(run, &Curve, Length - 1e-6);
    TEST_CHECK(run, fabs(GetFrameTwist(&End, &Start)) < 1e-3);
    // Closing at a corner, the frame turns across that kink as across any other: no twist about the path there.
    PathNode Tilted[4] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 5.0, 0.0, PathNodeMode_Corner),
        MakeNode(20.0, 0.0, 20.0, PathNodeMode_Corner), MakeNode(-5.0, 10.0, 20.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Tilted, 4U, true, PathUpMode_RotationMinimizing));
    double TiltedLength = PathCurve_GetLength(&Curve);
    PathCurvePoint Arriving = PointAt(run, &Curve, TiltedLength - 1e-6);
    PathCurvePoint Leaving = PointAt(run, &Curve, 0.0);
    TEST_CHECK(run, fabs(GetFrameTwist(&Arriving, &Leaving)) < 1e-3);
    // And across a kink inside it, up to the even share of the loop's twist.
    Arriving = PointAt(run, &Curve, PathCurve_GetNodeDistance(&Curve, 1U) - 1e-6);
    Leaving = PointAt(run, &Curve, PathCurve_GetNodeDistance(&Curve, 1U));
    TEST_CHECK(run, fabs(GetFrameTwist(&Arriving, &Leaving)) < 1e-3);
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Loop, 8U, true, PathUpMode_RotationMinimizing));
    Vector3Double Carried = ToDouble(Start.Right);
    Vector3Double CarriedTangent = ToDouble(Start.Tangent);
    Last = Start;
    double Twisted = 0.0;
    double Largest = 0.0;
    double Smallest = INFINITY;
    int Steps = (int)(Length / Step);
    for (int Index = 1; Index <= Steps; Index++)
    {
        PathCurvePoint Point = PointAt(run, &Curve, (Length * (double)Index) / (double)Steps);
        TEST_CHECK(run, IsFrameValid(&Point));
        double Twist = GetFrameTwist(&Last, &Point);
        Twisted += Twist;
        Largest = fmax(Largest, Twist);
        Smallest = fmin(Smallest, Twist);
        Vector3Double NextTangent = ToDouble(Point.Tangent);
        Vector3Double Axis = Vector3Double_Cross(CarriedTangent, NextTangent);
        double Sin = Vector3Double_Length(Axis);
        if (Sin > 1e-12)
        {
            Carried = Vector3Double_RotateByAxisAngle(Carried, Vector3Double_Scale(Axis, 1.0 / Sin),
                atan2(Sin, Vector3Double_Dot(CarriedTangent, NextTangent)));
        }
        CarriedTangent = NextTangent;
        Last = Point;
    }
    double Holonomy = atan2(Vector3Double_Dot(Vector3Double_Cross(Carried, ToDouble(Start.Right)), CarriedTangent),
        Vector3Double_Dot(Carried, ToDouble(Start.Right)));
    TEST_CHECK(run, fabs(Holonomy) > 0.05); // or the loop would not test this
    TEST_CHECK(run, fabs(Twisted - Holonomy) < 0.01);
    double Share = Twisted / (double)Steps;
    TEST_CHECK(run, (fabs(Largest - Share) < 1e-3) && (fabs(Smallest - Share) < 1e-3));
    PathCurve_Deconstruct(&Curve);
}

static void ValuesRunSmoothlyWithoutOvershoot(TestRun* run)
{
    PathNode Nodes[5];
    for (int Index = 0; Index < 5; Index++)
    {
        Nodes[Index] = MakeNode(10.0 * (double)Index, 0.0, 0.0, PathNodeMode_Corner);
        Nodes[Index].StartsSection = false;
    }
    float Widths[5] = { 1.0f, 1.0f, 5.0f, 5.0f, 2.0f };
    float Scales[5] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
    float Rolls[5] = { 0.0f, 0.5f, -0.5f, 0.25f, 0.25f };
    for (int Index = 0; Index < 5; Index++)
    {
        Nodes[Index].Width = Widths[Index];
        Nodes[Index].Scale = Scales[Index];
        Nodes[Index].Roll = Rolls[Index];
    }
    PathCurve Curve;
    if (!Build(run, &Curve, Nodes, 5U, false, PathUpMode_Upright))
    {
        return;
    }
    // Exactly the node's at a node.
    for (int Index = 0; Index < 5; Index++)
    {
        PathCurvePoint Point = PointAt(run, &Curve, 10.0 * (double)Index);
        TEST_CHECK(run, (Point.Width == Widths[Index]) && (Point.Scale == Scales[Index]) && (Point.Roll == Rolls[Index]));
    }
    // A steady ramp stays one: evenly spaced nodes rising evenly give a straight line of values.
    for (double Distance = 0.0; Distance <= 40.0; Distance += 0.7)
    {
        TEST_CHECK(run, fabsf(PointAt(run, &Curve, Distance).Scale - (float)(1.0 + (Distance / 10.0))) < 1e-5f);
    }
    // Between nodes never past either neighbour's value, and a flat stretch stays flat.
    for (double Distance = 0.0; Distance < 40.0; Distance += 0.05)
    {
        PathCurvePoint Point = PointAt(run, &Curve, Distance);
        uint32_t Segment = Point.Segment;
        float WidthLow = fminf(Widths[Segment], Widths[Segment + 1U]);
        float WidthHigh = fmaxf(Widths[Segment], Widths[Segment + 1U]);
        float RollLow = fminf(Rolls[Segment], Rolls[Segment + 1U]);
        float RollHigh = fmaxf(Rolls[Segment], Rolls[Segment + 1U]);
        TEST_CHECK(run, (Point.Width >= WidthLow) && (Point.Width <= WidthHigh) && (Point.Roll >= RollLow)
            && (Point.Roll <= RollHigh));
    }
    TEST_CHECK(run, PointAt(run, &Curve, 5.0).Width == 1.0f);
    TEST_CHECK(run, PointAt(run, &Curve, 25.0).Width == 5.0f);
    // A peak is rounded, reached only at its node: its slope is 0, or the value would climb past it just after the node
    // and sit on the clamp.
    TEST_CHECK(run, PointAt(run, &Curve, 10.5).Roll < 0.4999f);
    // And no corner in the values at a node inside a steady rise: the slope matches on both sides.
    PathNode Rise[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth),
        MakeNode(30.0, 0.0, 0.0, PathNodeMode_Smooth) };
    Rise[0].Width = 1.0f;
    Rise[1].Width = 2.0f;
    Rise[2].Width = 7.0f;
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Rise, 3U, false, PathUpMode_Upright));
    double Node = PathCurve_GetNodeDistance(&Curve, 1U);
    double Before = (double)(PointAt(run, &Curve, Node).Width - PointAt(run, &Curve, Node - 0.01).Width) / 0.01;
    double After = (double)(PointAt(run, &Curve, Node + 0.01).Width - PointAt(run, &Curve, Node).Width) / 0.01;
    TEST_CHECK(run, fabs(Before - After) < 0.02);
    // Steffen's slope there: secants 0.1 over 10 m and 0.25 over 20 m, weighted by the other side's length, 0.15 between them.
    TEST_CHECK(run, fabs(After - 0.15) < 0.01);

    // Two nodes: a straight line of values, and on a closed path they run on through its end.
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Rise, 2U, false, PathUpMode_Upright));
    TEST_CHECK(run, fabsf(PointAt(run, &Curve, 2.5).Width - 1.25f) < 1e-5f);
    PathNode Ring[4] = { MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(0.0, 0.0, 10.0, PathNodeMode_Smooth),
        MakeNode(-10.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(0.0, 0.0, -10.0, PathNodeMode_Smooth) };
    Ring[0].Scale = 2.0f;
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Ring, 4U, true, PathUpMode_Upright));
    double Length = PathCurve_GetLength(&Curve);
    TEST_CHECK(run, fabsf(PointAt(run, &Curve, 0.01).Scale - PointAt(run, &Curve, Length - 0.01).Scale) < 1e-3f);
    // Node 0 of a closed path has neighbours on both sides like any other: its slope comes from both secants.
    Ring[0].Scale = 2.0f;
    Ring[1].Scale = 3.0f;
    Ring[2].Scale = 2.0f;
    Ring[3].Scale = 1.9f;
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Ring, 4U, true, PathUpMode_Upright));
    Length = PathCurve_GetLength(&Curve);
    double Quarter = Length / 4.0;
    double ExpectedSlope = 2.0 * fmin(fmin(0.1 / Quarter, 1.0 / Quarter), 0.5 * ((0.1 + 1.0) / (2.0 * Quarter)));
    double Slope = (double)(PointAt(run, &Curve, 0.05).Scale - PointAt(run, &Curve, 0.0).Scale) / 0.05;
    TEST_CHECK(run, fabs(Slope - ExpectedSlope) < (ExpectedSlope * 0.1));
    PathCurve_Deconstruct(&Curve);
}

static void DegenerateAndHostileInput(TestRun* run)
{
    PathCurve Curve;
    PathCurve_Construct(&Curve);
    GenericBuffer Samples;
    GenericBuffer_AllocateVariable(&Samples, 0U, sizeof(double));
    PathSampleOptions Options = PathSampleOptions_Create(1.0);

    // Every node at one place, in every mode: length 0, and every answer finite.
    for (int Mode = 0; Mode < 3; Mode++)
    {
        for (int UpMode = 0; UpMode < 2; UpMode++)
        {
            for (int IsClosed = 0; IsClosed < 2; IsClosed++)
            {
                PathNode Nodes[3] = { MakeNode(4.0, 5.0, 6.0, (PathNodeMode)Mode), MakeNode(4.0, 5.0, 6.0, (PathNodeMode)Mode),
                    MakeNode(4.0, 5.0, 6.0, (PathNodeMode)Mode) };
                TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Nodes, 3U, IsClosed != 0, (PathUpMode)UpMode));
                TEST_CHECK(run, PathCurve_GetLength(&Curve) == 0.0);
                PathCurvePoint Point = PointAt(run, &Curve, 3.0);
                TEST_CHECK(run, IsFrameValid(&Point) && IsNear(Point.Position, Nodes[0].Position, 0.0));
                PathCurvePoint Nearest;
                TEST_CHECK(run, PathCurve_FindNearest(&Curve, Vector3Double_Zero(), false, &Nearest, NULL)
                    && IsFrameValid(&Nearest));
                GenericBuffer_Clear(&Samples);
                TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
                TEST_CHECK(run, Samples._count == 1U);
                TEST_CHECK(run, !PathCurve_IsPointInside(&Curve, 4.0, 6.0));
                for (uint32_t Section = 0U; Section < PathCurve_GetSectionCount(&Curve); Section++)
                {
                    PathCurvePoint Inside;
                    TEST_CHECK(run, PathCurve_GetSectionPoint(&Curve, Section, 0.0, &Inside) && IsFrameValid(&Inside));
                }
            }
        }
    }

    // A pair of nodes at one place inside a path: its tangents, sections and samples stay sound around them.
    PathNode Doubled[5] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth),
        MakeNode(10.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(20.0, 0.0, 5.0, PathNodeMode_Smooth),
        MakeNode(20.0, 0.0, 5.0, PathNodeMode_Smooth) };
    for (int UpMode = 0; UpMode < 2; UpMode++)
    {
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Doubled, 5U, false, (PathUpMode)UpMode));
        for (double Distance = 0.0; Distance <= PathCurve_GetLength(&Curve); Distance += 0.1)
        {
            PathCurvePoint Point = PointAt(run, &Curve, Distance);
            TEST_CHECK(run, IsFrameValid(&Point));
        }
        // The path's end is on the last segment with a length, with the tangent it arrived with.
        PathCurvePoint End = PointAt(run, &Curve, PathCurve_GetLength(&Curve));
        TEST_CHECK(run, IsFrameValid(&End) && IsNear(End.Position, Doubled[4].Position, 1e-9) && (End.Segment == 2U));
        GenericBuffer_Clear(&Samples);
        TEST_CHECK_SUCCESS(run, PathCurve_WriteSamples(&Curve, &Options, &Samples));
    }

    // The limits of what a file may hold: answers stay finite and the table stays bounded.
    float Huge = 1.0e7f;
    PathNode Far[3] = { MakeBezier(-1e7, -1e7, -1e7, (Vector3){ Huge, Huge, Huge }, (Vector3){ Huge, -Huge, Huge }),
        MakeBezier(1e7, 1e7, 1e7, (Vector3){ -Huge, Huge, -Huge }, (Vector3){ Huge, Huge, Huge }),
        MakeNode(1e7, -1e7, 1e7, PathNodeMode_Corner) };
    Far[0].Roll = 1e30f;
    Far[1].Width = 1e30f;
    for (int UpMode = 0; UpMode < 2; UpMode++)
    {
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Far, 3U, true, (PathUpMode)UpMode));
        double Length = PathCurve_GetLength(&Curve);
        TEST_CHECK(run, isfinite(Length) && (Length > 1e7));
        for (double Distance = 0.0; Distance <= Length; Distance += Length / 97.0)
        {
            PathCurvePoint Point = PointAt(run, &Curve, Distance);
            TEST_CHECK(run, IsFrameValid(&Point) && isfinite(Point.Roll) && isfinite(Point.Width));
        }
    }

    // Nodes a hair apart with different values: a slope over no length would be infinite, and it is not.
    PathNode Hair[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(1.0e-300, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(5.0e-324, 0.0, 1.0, PathNodeMode_Corner) };
    Hair[1].Width = 5.0f;
    Hair[1].Roll = 3.0e38f;
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Hair, 3U, false, PathUpMode_Upright));
    for (double Distance = 0.0; Distance <= PathCurve_GetLength(&Curve); Distance += 0.125)
    {
        PathCurvePoint Point = PointAt(run, &Curve, Distance);
        TEST_CHECK(run, IsFrameValid(&Point) && isfinite(Point.Width) && isfinite(Point.Roll));
    }

    // Far from the object's origin a retracted handle's first steps cancel in a position: not in the tangent's secant.
    for (int Distant = 0; Distant < 3; Distant++)
    {
        double Place = (Distant == 0) ? 0.0 : ((Distant == 1) ? 1.0e5 : 1.0e7);
        PathNode Back[2] = { MakeBezier(Place, 0.0, 0.0, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }),
            MakeBezier(Place + 10.0, 0.0, 10.0, (Vector3){ -10.0f, 0.0f, -10.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }) };
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Back, 2U, false, PathUpMode_Upright));
        TEST_CHECK(run, IsDirectionNear(PointAt(run, &Curve, 0.0).Tangent, (Vector3){ 0.7071068f, 0.0f, 0.7071068f }, 1e-4f));
    }

    // Past what any world holds, and where squares would overflow: refused, finite or not.
    PathNode Enormous = MakeNode(1.0e154, 0.0, 0.0, PathNodeMode_Smooth);
    TEST_CHECK_ERROR(run, PathCurve_Build(&Curve, &Enormous, 1U, false, PathUpMode_Upright), ErrorCode_IllegalArgument);
    Enormous = MakeNode(PATH_CURVE_COORDINATE_LIMIT, 0.0, 0.0, PathNodeMode_Smooth);
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, &Enormous, 1U, false, PathUpMode_Upright));
    Enormous = MakeNode(PATH_CURVE_COORDINATE_LIMIT * 2.0, 0.0, 0.0, PathNodeMode_Smooth);
    TEST_CHECK_ERROR(run, PathCurve_Build(&Curve, &Enormous, 1U, false, PathUpMode_Upright), ErrorCode_IllegalArgument);
    Enormous = MakeNode(0.0, 0.0, 0.0, PathNodeMode_Bezier);
    Enormous.InHandle.y = 1.0e30f;
    TEST_CHECK_ERROR(run, PathCurve_Build(&Curve, &Enormous, 1U, false, PathUpMode_Upright), ErrorCode_IllegalArgument);

    // Refused, leaving the curve empty.
    PathNode Good = MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth);
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Doubled, 5U, false, PathUpMode_Upright));
    TEST_CHECK_ERROR(run, PathCurve_Build(NULL, &Good, 1U, false, PathUpMode_Upright), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, PathCurve_Build(&Curve, NULL, 1U, false, PathUpMode_Upright), ErrorCode_IllegalArgument);
    TEST_CHECK(run, PathCurve_GetNodeCount(&Curve) == 0U);
    TEST_CHECK_ERROR(run, PathCurve_Build(&Curve, &Good, 1U, false, (PathUpMode)9), ErrorCode_IllegalArgument);
    PathNode* Many = Memory_Allocate((PATH_CURVE_MAX_NODES + 1U) * sizeof(PathNode));
    for (size_t Index = 0U; Index <= PATH_CURVE_MAX_NODES; Index++)
    {
        Many[Index] = MakeNode((double)Index, 0.0, 0.0, PathNodeMode_Corner);
    }
    TEST_CHECK_ERROR(run, PathCurve_Build(&Curve, Many, PATH_CURVE_MAX_NODES + 1U, false, PathUpMode_Upright),
        ErrorCode_IllegalArgument);
    TEST_CHECK(run, PathCurve_GetNodeCount(&Curve) == 0U);
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Many, PATH_CURVE_MAX_NODES, false, PathUpMode_Upright));
    TEST_CHECK(run, PathCurve_GetNodeCount(&Curve) == PATH_CURVE_MAX_NODES);
    Memory_Free(Many);
    for (int Breaker = 0; Breaker < 7; Breaker++)
    {
        PathNode Broken[2] = { Good, Good };
        switch (Breaker)
        {
            case 0: Broken[1].Position.X = (double)NAN; break;
            case 1: Broken[1].Position.Z = (double)INFINITY; break;
            case 2: Broken[1].InHandle.y = NAN; break;
            case 3: Broken[1].OutHandle.z = INFINITY; break;
            case 4: Broken[1].Roll = NAN; break;
            case 5: Broken[1].Width = INFINITY; break;
            default: Broken[1].Mode = (PathNodeMode)7; break;
        }
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, &Good, 1U, false, PathUpMode_Upright));
        TEST_CHECK_ERROR(run, PathCurve_Build(&Curve, Broken, 2U, false, PathUpMode_Upright), ErrorCode_IllegalArgument);
        TEST_CHECK(run, (PathCurve_GetNodeCount(&Curve) == 0U) && (PathCurve_GetLength(&Curve) == 0.0));
    }
    PathCurvePoint Unused;
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Doubled, 5U, false, PathUpMode_Upright));
    TEST_CHECK(run, !PathCurve_GetPointAt(&Curve, (double)NAN, &Unused) && !PathCurve_GetSectionPoint(&Curve, 0U,
        (double)INFINITY, &Unused));
    PathCurve_Deconstruct(&Curve);
    Memory_Free(Samples._data);
}

static void RegionsAreTheAreasClosedPathsEnclose(TestRun* run)
{
    // An L of corners, wound one way and the other.
    PathNode Shape[6] = { MakeNode(0.0, 3.0, 0.0, PathNodeMode_Corner), MakeNode(20.0, 9.0, 0.0, PathNodeMode_Corner),
        MakeNode(20.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(10.0, 7.0, 10.0, PathNodeMode_Corner),
        MakeNode(10.0, 2.0, 20.0, PathNodeMode_Corner), MakeNode(0.0, 1.0, 20.0, PathNodeMode_Corner) };
    PathCurve Curve;
    if (!Build(run, &Curve, Shape, 6U, true, PathUpMode_Upright))
    {
        return;
    }
    for (int Direction = 0; Direction < 2; Direction++)
    {
        TEST_CHECK(run, PathCurve_IsPointInside(&Curve, 5.0, 5.0) && PathCurve_IsPointInside(&Curve, 15.0, 5.0)
            && PathCurve_IsPointInside(&Curve, 5.0, 15.0));
        TEST_CHECK(run, !PathCurve_IsPointInside(&Curve, 15.0, 15.0) && !PathCurve_IsPointInside(&Curve, -1.0, 5.0)
            && !PathCurve_IsPointInside(&Curve, 25.0, 5.0) && !PathCurve_IsPointInside(&Curve, 5.0, -1.0)
            && !PathCurve_IsPointInside(&Curve, 5.0, 25.0));
        PathNode Reversed[6];
        for (int Index = 0; Index < 6; Index++)
        {
            Reversed[Index] = Shape[5 - Index];
        }
        TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Reversed, 6U, true, PathUpMode_Upright));
    }
    uint32_t Count = 0U;
    const Vector2Double* Points = PathCurve_GetRegionPoints(&Curve, &Count);
    TEST_CHECK(run, (Points != NULL) && (Count == 6U));

    // The same nodes open: no area at all.
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Shape, 6U, false, PathUpMode_Upright));
    TEST_CHECK(run, !PathCurve_IsPointInside(&Curve, 5.0, 5.0) && (PathCurve_GetRegionPoints(&Curve, &Count) == NULL)
        && (Count == 0U));
    TEST_CHECK(run, !PathCurve_IsPointInside(&Curve, (double)NAN, 5.0));

    // A smooth ring: its polygon follows the curve, not the nodes' chords.
    PathNode Ring[6];
    for (int Index = 0; Index < 6; Index++)
    {
        double Angle = (double)Index * (PI / 3.0);
        Ring[Index] = MakeNode(100.0 * cos(Angle), 0.0, 100.0 * sin(Angle), PathNodeMode_Smooth);
    }
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Ring, 6U, true, PathUpMode_Upright));
    Points = PathCurve_GetRegionPoints(&Curve, &Count);
    TEST_CHECK(run, Count > 24U);
    double Area = 0.0;
    for (uint32_t Index = 0U; Index < Count; Index++)
    {
        const Vector2Double* From = &Points[Index];
        const Vector2Double* To = &Points[(Index + 1U) % Count];
        Area += (From->X * To->Y) - (To->X * From->Y);
        TEST_CHECK(run, IsOnCurve(run, &Curve, Vector3Double_Create(From->X, 0.0, From->Y), 1e-6));
    }
    TEST_CHECK(run, fabs((0.5 * fabs(Area)) - (PI * 100.0 * 100.0)) < (PI * 100.0 * 100.0 * 0.03));
    // Outside the hexagon of the nodes' chords but inside the curve.
    TEST_CHECK(run, PathCurve_IsPointInside(&Curve, 92.0 * cos(PI / 6.0), 92.0 * sin(PI / 6.0)));
    TEST_CHECK(run, !PathCurve_IsPointInside(&Curve, 110.0 * cos(PI / 6.0), 110.0 * sin(PI / 6.0)));

    // Against a dense polygon of its own curve: the same answer away from the outline.
    uint32_t State = 41U;
    double Length = PathCurve_GetLength(&Curve);
    for (int Query = 0; Query < 400; Query++)
    {
        double X = (NextRandom(&State) * 260.0) - 130.0;
        double Z = (NextRandom(&State) * 260.0) - 130.0;
        int Crossings = 0;
        Vector3Double From = PointAt(run, &Curve, 0.0).Position;
        for (int Step = 1; Step <= 720; Step++)
        {
            Vector3Double To = PointAt(run, &Curve, (double)Step * Length / 720.0).Position;
            if (((From.Z > Z) != (To.Z > Z)) && (X < (((To.X - From.X) * (Z - From.Z) / (To.Z - From.Z)) + From.X)))
            {
                Crossings++;
            }
            From = To;
        }
        PathCurvePoint Nearest;
        double Separation = 0.0;
        TEST_CHECK(run, PathCurve_FindNearest(&Curve, Vector3Double_Create(X, 0.0, Z), true, &Nearest, &Separation));
        if (Separation > 0.1)
        {
            TEST_CHECK(run, PathCurve_IsPointInside(&Curve, X, Z) == ((Crossings % 2) == 1));
        }
    }

    // A figure of eight: both lobes inside, whichever way each winds. And two turns round: the overlap too, by non-zero.
    PathNode Eight[4] = { MakeNode(-10.0, 0.0, -10.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner),
        MakeNode(10.0, 0.0, -10.0, PathNodeMode_Corner), MakeNode(-10.0, 0.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Eight, 4U, true, PathUpMode_Upright));
    TEST_CHECK(run, PathCurve_IsPointInside(&Curve, 7.0, 0.0) && PathCurve_IsPointInside(&Curve, -7.0, 0.0)
        && !PathCurve_IsPointInside(&Curve, 0.0, 7.0) && !PathCurve_IsPointInside(&Curve, 0.0, -7.0));
    PathNode Twice[8] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner),
        MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(10.0, 0.0, 10.0, PathNodeMode_Corner), MakeNode(0.0, 0.0, 10.0, PathNodeMode_Corner) };
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Twice, 8U, true, PathUpMode_Upright));
    TEST_CHECK(run, PathCurve_IsPointInside(&Curve, 5.0, 5.0));
    PathCurve_Deconstruct(&Curve);
}

static void BuildingAgainReplacesEverything(TestRun* run)
{
    PathNode First[3] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Corner), MakeNode(10.0, 0.0, 0.0, PathNodeMode_Corner),
        MakeNode(20.0, 0.0, 0.0, PathNodeMode_Corner) };
    PathNode Second[2] = { MakeNode(0.0, 0.0, 0.0, PathNodeMode_Smooth), MakeNode(0.0, 0.0, 5.0, PathNodeMode_Smooth) };
    PathCurve Curve;
    if (!Build(run, &Curve, First, 3U, true, PathUpMode_RotationMinimizing))
    {
        return;
    }
    TEST_CHECK(run, PathCurve_GetSectionCount(&Curve) == 3U);
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, Second, 2U, false, PathUpMode_Upright));
    TEST_CHECK(run, (fabs(PathCurve_GetLength(&Curve) - 5.0) < 1e-12) && (PathCurve_GetNodeCount(&Curve) == 2U)
        && !PathCurve_IsClosed(&Curve) && (PathCurve_GetSectionCount(&Curve) == 1U));
    TEST_CHECK_SUCCESS(run, PathCurve_Build(&Curve, NULL, 0U, false, PathUpMode_Upright));
    TEST_CHECK(run, (PathCurve_GetNodeCount(&Curve) == 0U) && (PathCurve_GetLength(&Curve) == 0.0));
    PathCurve_Deconstruct(&Curve);
}


// Public functions.
void PathCurveTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "empty_and_single_node_curves", EmptyAndSingleNodeCurves },
        { "corner_nodes_make_a_polyline", CornerNodesMakeAPolyline },
        { "smooth_nodes_follow_centripetal_catmull_rom", SmoothNodesFollowCentripetalCatmullRom },
        { "smooth_nodes_bend_away_from_corners_without_a_kink", SmoothNodesBendAwayFromCornersWithoutAKink },
        { "bezier_nodes_follow_their_handles", BezierNodesFollowTheirHandles },
        { "arc_length_is_exact", ArcLengthIsExact },
        { "closed_paths_wrap_and_meet", ClosedPathsWrapAndMeet },
        { "sections_hold_their_first_nodes_settings", SectionsHoldTheirFirstNodesSettings },
        { "samples_follow_spacing_and_curvature", SamplesFollowSpacingAndCurvature },
        { "nearest_point_beats_every_other_point", NearestPointBeatsEveryOtherPoint },
        { "upright_frames_stay_upright", UprightFramesStayUpright },
        { "rotation_minimizing_frames_do_not_twist", RotationMinimizingFramesDoNotTwist },
        { "values_run_smoothly_without_overshoot", ValuesRunSmoothlyWithoutOvershoot },
        { "degenerate_and_hostile_input", DegenerateAndHostileInput },
        { "regions_are_the_areas_closed_paths_enclose", RegionsAreTheAreasClosedPathsEnclose },
        { "building_again_replaces_everything", BuildingAgainReplacesEverything },
    };
    TestRun_RunSuite(run, "PathCurve", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
