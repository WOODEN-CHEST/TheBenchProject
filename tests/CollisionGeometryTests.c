#include "TestSuites.h"
#include "CollisionGeometry.h"
#include "CollisionMath.h"
#include "CollisionShape.h"
#include "wr/WRMemory.h"
#include <float.h>
#include <math.h>
#include <stdint.h>


// Macros.
/** Tolerances: distances in metres, and looser ones for iterative answers (EPA, casts). */
#define NEAR_EPSILON 1e-4f
#define LOOSE_EPSILON 2e-3f
/** The game's tick, for the tunnelling cases. */
#define TICK_SECONDS (1.0f / 480.0f)


// Static functions: helpers.
static bool IsNear(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static bool IsNearVector(Vector3 a, Vector3 b, float tolerance)
{
    return IsNear(a.x, b.x, tolerance) && IsNear(a.y, b.y, tolerance) && IsNear(a.z, b.z, tolerance);
}

static Vector3 V3(float x, float y, float z)
{
    return CollisionMath_Vector(x, y, z);
}

/* A transform: Euler XYZ rotation (degrees), per-axis scale, then the position. */
static CollisionAffine MakeTransform(Vector3 position, Vector3 degrees, Vector3 scale)
{
    Vector3 Radians = CollisionMath_Scale(degrees, DEG2RAD);
    Matrix Composed = MatrixMultiply(MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixRotateXYZ(Radians)),
        MatrixTranslate(position.x, position.y, position.z));
    return CollisionAffine_FromMatrix(Composed);
}

static CollisionAffine At(Vector3 position)
{
    return MakeTransform(position, V3(0.0f, 0.0f, 0.0f), V3(1.0f, 1.0f, 1.0f));
}

static CollisionConvex MakeConvex(const CollisionShape* shape, CollisionAffine transform)
{
    CollisionConvex Convex;
    CollisionConvex_FromShape(shape, &transform, &Convex);
    return Convex;
}

static CollisionConvex Sphere(Vector3 center, float radius)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Sphere);
    Shape.Radius = radius;
    return MakeConvex(&Shape, At(center));
}

static CollisionConvex Box(Vector3 center, Vector3 half, Vector3 degrees)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = half;
    return MakeConvex(&Shape, MakeTransform(center, degrees, V3(1.0f, 1.0f, 1.0f)));
}

static CollisionConvex Capsule(Vector3 center, float radius, float halfHeight, Vector3 degrees)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Capsule);
    Shape.Radius = radius;
    Shape.HalfHeight = halfHeight;
    return MakeConvex(&Shape, MakeTransform(center, degrees, V3(1.0f, 1.0f, 1.0f)));
}

static CollisionConvex Cylinder(Vector3 center, float radius, float halfHeight, Vector3 degrees)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Cylinder);
    Shape.Radius = radius;
    Shape.HalfHeight = halfHeight;
    return MakeConvex(&Shape, MakeTransform(center, degrees, V3(1.0f, 1.0f, 1.0f)));
}

static CollisionHull* MakeCubeHull(TestRun* run, float half)
{
    Vector3 Points[8];
    for (uint32_t Corner = 0U; Corner < 8U; Corner++)
    {
        Points[Corner] = V3(((Corner & 1U) != 0U) ? half : -half, ((Corner & 2U) != 0U) ? half : -half,
            ((Corner & 4U) != 0U) ? half : -half);
    }
    CollisionHull* Hull = NULL;
    TEST_CHECK_SUCCESS(run, CollisionHull_Create(Points, 8U, COLLISION_HULL_DEFAULT_MAX_VERTICES, &Hull));
    return Hull;
}

static CollisionPlaneView MakePlane(Vector3 center, Vector3 degrees, bool isBounded, Vector2 half, float depth)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Plane);
    Shape.IsBounded = isBounded;
    Shape.PlaneHalfExtents = half;
    Shape.Depth = depth;
    CollisionAffine Placement = MakeTransform(center, degrees, V3(1.0f, 1.0f, 1.0f));
    CollisionPlaneView Plane;
    CollisionPlaneView_FromShape(&Shape, &Placement, &Plane);
    return Plane;
}

/* A flat square of two triangles, 2 x 2 m in XZ at height 0, facing up, its diagonal shared. */
static CollisionMesh* MakeQuadMesh(TestRun* run)
{
    static const float Vertices[] = { -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f };
    static const unsigned short Indices[] = { 0U, 2U, 1U, 0U, 3U, 2U };
    CollisionMeshPart Part = { .Vertices = Vertices, .VertexCount = 4U, .Indices = Indices, .TriangleCount = 2U,
        .MaterialIndex = 0 };
    CollisionMesh* Built = NULL;
    TEST_CHECK_SUCCESS(run, CollisionMesh_Create(&Part, 1U, MatrixIdentity(), &Built));
    return Built;
}


// Static functions: maths.
static void AffineTransformsInvertAndCompose(TestRun* run)
{
    CollisionAffine Placed = MakeTransform(V3(1.0f, -2.0f, 3.0f), V3(30.0f, 45.0f, -60.0f), V3(2.0f, 0.5f, 3.0f));
    CollisionAffine Inverse;
    TEST_CHECK(run, CollisionAffine_TryInvert(&Placed, &Inverse));
    CollisionAffine Identity = CollisionAffine_Multiply(&Placed, &Inverse);
    Vector3 Point = V3(0.3f, -0.7f, 1.9f);
    TEST_CHECK(run, IsNearVector(CollisionAffine_TransformPoint(&Identity, Point), Point, NEAR_EPSILON));
    float Scale = 0.0f;
    TEST_CHECK(run, !CollisionAffine_IsSimilarity(&Placed, &Scale));
    CollisionAffine Similar = MakeTransform(V3(0.0f, 0.0f, 0.0f), V3(10.0f, 20.0f, 30.0f), V3(2.0f, 2.0f, 2.0f));
    TEST_CHECK(run, CollisionAffine_IsSimilarity(&Similar, &Scale) && IsNear(Scale, 2.0f, NEAR_EPSILON));
    CollisionAffine Flat = MakeTransform(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), V3(1.0f, 0.0f, 1.0f));
    TEST_CHECK(run, !CollisionAffine_TryInvert(&Flat, &Inverse));

    // The matrix round trip is exact for raylib's own composition.
    Matrix Composed = CollisionAffine_ToMatrix(&Placed);
    TEST_CHECK(run, IsNearVector(Vector3Transform(Point, Composed), CollisionAffine_TransformPoint(&Placed, Point),
        NEAR_EPSILON));
}

static void ClosestPointsOnSegmentsAndTriangles(TestRun* run)
{
    float S = 0.0f;
    float T = 0.0f;
    CollisionMath_GetClosestSegmentParameters(V3(0.0f, 0.0f, 0.0f), V3(2.0f, 0.0f, 0.0f), V3(1.0f, 1.0f, -1.0f),
        V3(1.0f, 1.0f, 1.0f), &S, &T);
    TEST_CHECK(run, IsNear(S, 0.5f, NEAR_EPSILON) && IsNear(T, 0.5f, NEAR_EPSILON));

    Vector3 Weights;
    uint32_t Region = 99U;
    Vector3 A = V3(0.0f, 0.0f, 0.0f);
    Vector3 B = V3(1.0f, 0.0f, 0.0f);
    Vector3 C = V3(0.0f, 0.0f, 1.0f);
    Vector3 Face = CollisionMath_GetClosestTrianglePoint(A, B, C, V3(0.25f, 3.0f, 0.25f), &Weights, &Region);
    TEST_CHECK(run, (Region == 0U) && IsNearVector(Face, V3(0.25f, 0.0f, 0.25f), NEAR_EPSILON));
    TEST_CHECK(run, IsNear(Weights.x + Weights.y + Weights.z, 1.0f, NEAR_EPSILON));
    CollisionMath_GetClosestTrianglePoint(A, B, C, V3(-1.0f, 0.0f, -1.0f), &Weights, &Region);
    TEST_CHECK(run, Region == 4U);
    CollisionMath_GetClosestTrianglePoint(A, B, C, V3(1.0f, 0.0f, 1.0f), &Weights, &Region);
    TEST_CHECK(run, Region == 2U);
    CollisionMath_GetClosestTrianglePoint(A, B, C, V3(0.5f, 0.0f, -2.0f), &Weights, &Region);
    TEST_CHECK(run, Region == 1U);
}

static void PosesTurnAtAConstantRate(TestRun* run)
{
    CollisionPose From = CollisionPose_Identity();
    CollisionPose To = { .Position = V3(4.0f, 0.0f, 0.0f), .Rotation = QuaternionFromAxisAngle(V3(0.0f, 1.0f, 0.0f),
        PI * 0.5f) };
    TEST_CHECK(run, IsNear(CollisionPose_GetRotationAngle(&From, &To), PI * 0.5f, NEAR_EPSILON));
    CollisionPose Middle = CollisionPose_Interpolate(&From, &To, 0.5f);
    TEST_CHECK(run, IsNearVector(Middle.Position, V3(2.0f, 0.0f, 0.0f), NEAR_EPSILON));
    TEST_CHECK(run, IsNear(CollisionPose_GetRotationAngle(&From, &Middle), PI * 0.25f, NEAR_EPSILON));
    CollisionPose Quarter = CollisionPose_Interpolate(&From, &To, 0.25f);
    TEST_CHECK(run, IsNear(CollisionPose_GetRotationAngle(&From, &Quarter), PI * 0.125f, NEAR_EPSILON));
}


// Static functions: supports, distances and penetrations.
static void SupportsFollowEveryTransform(TestRun* run)
{
    // A sphere scaled 2 x 1 x 1 is an ellipsoid: its support along X reaches twice the radius.
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Sphere);
    Shape.Radius = 1.0f;
    CollisionConvex Ellipsoid = MakeConvex(&Shape, MakeTransform(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f),
        V3(2.0f, 1.0f, 1.0f)));
    TEST_CHECK(run, !Ellipsoid.IsSimilarity && (Ellipsoid.CoreRadius == 0.0f));
    TEST_CHECK(run, IsNearVector(CollisionConvex_SupportFull(&Ellipsoid, V3(1.0f, 0.0f, 0.0f)), V3(2.0f, 0.0f, 0.0f),
        NEAR_EPSILON));
    TEST_CHECK(run, IsNearVector(CollisionConvex_SupportFull(&Ellipsoid, V3(0.0f, 1.0f, 0.0f)), V3(0.0f, 1.0f, 0.0f),
        NEAR_EPSILON));

    // A uniformly scaled sphere keeps its rounding apart as a core radius.
    CollisionConvex Scaled = MakeConvex(&Shape, MakeTransform(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f),
        V3(3.0f, 3.0f, 3.0f)));
    TEST_CHECK(run, Scaled.IsSimilarity && IsNear(Scaled.CoreRadius, 3.0f, NEAR_EPSILON));

    CollisionConvex Turned = Box(V3(0.0f, 0.0f, 0.0f), V3(1.0f, 0.5f, 0.25f), V3(0.0f, 90.0f, 0.0f));
    Vector3 Corner = CollisionConvex_SupportFull(&Turned, V3(1.0f, 1.0f, 1.0f));
    TEST_CHECK(run, IsNearVector(Corner, V3(0.25f, 0.5f, 1.0f), NEAR_EPSILON));

    CollisionConvex Can = Cylinder(V3(0.0f, 0.0f, 0.0f), 0.5f, 1.0f, V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, IsNearVector(CollisionConvex_SupportFull(&Can, V3(1.0f, 1.0f, 0.0f)), V3(0.5f, 1.0f, 0.0f),
        NEAR_EPSILON));
}

static void GjkFindsTheExactDistance(TestRun* run)
{
    CollisionDistance Distance;
    CollisionConvex A = Sphere(V3(0.0f, 0.0f, 0.0f), 1.0f);
    CollisionConvex B = Sphere(V3(5.0f, 0.0f, 0.0f), 2.0f);
    CollisionGeometry_GetDistance(&A, &B, &Distance);
    TEST_CHECK(run, !Distance.AreCoresOverlapping && IsNear(Distance.Distance, 2.0f, NEAR_EPSILON));
    TEST_CHECK(run, IsNearVector(Distance.Normal, V3(1.0f, 0.0f, 0.0f), NEAR_EPSILON));
    TEST_CHECK(run, IsNearVector(Distance.PointA, V3(1.0f, 0.0f, 0.0f), NEAR_EPSILON));
    TEST_CHECK(run, IsNearVector(Distance.PointB, V3(3.0f, 0.0f, 0.0f), NEAR_EPSILON));

    // A sphere above a box's face, edge and corner.
    CollisionConvex Cube = Box(V3(0.0f, 0.0f, 0.0f), V3(1.0f, 1.0f, 1.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Above = Sphere(V3(0.2f, 3.0f, -0.4f), 0.5f);
    CollisionGeometry_GetDistance(&Cube, &Above, &Distance);
    TEST_CHECK(run, IsNear(Distance.Distance, 1.5f, NEAR_EPSILON) && IsNearVector(Distance.Normal, V3(0.0f, 1.0f, 0.0f),
        NEAR_EPSILON));
    CollisionConvex OffCorner = Sphere(V3(2.0f, 2.0f, 2.0f), 0.5f);
    CollisionGeometry_GetDistance(&Cube, &OffCorner, &Distance);
    TEST_CHECK(run, IsNear(Distance.Distance, sqrtf(3.0f) - 0.5f, NEAR_EPSILON));

    // A shallow overlap of rounded shapes is exact without EPA (the cores stay apart).
    CollisionConvex Close = Sphere(V3(1.8f, 0.0f, 0.0f), 1.0f);
    CollisionGeometry_GetDistance(&A, &Close, &Distance);
    TEST_CHECK(run, !Distance.AreCoresOverlapping && IsNear(Distance.Distance, -0.2f, NEAR_EPSILON));

    // Capsules crossing at right angles, 1 m apart between their axes.
    CollisionConvex Upright = Capsule(V3(0.0f, 0.0f, 0.0f), 0.25f, 1.0f, V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Lying = Capsule(V3(1.0f, 0.0f, 0.0f), 0.25f, 1.0f, V3(90.0f, 0.0f, 0.0f));
    CollisionGeometry_GetDistance(&Upright, &Lying, &Distance);
    TEST_CHECK(run, IsNear(Distance.Distance, 0.5f, NEAR_EPSILON));

    // Two boxes, one turned 45 degrees: its edge faces the other's face.
    CollisionConvex Turned = Box(V3(3.0f, 0.0f, 0.0f), V3(1.0f, 1.0f, 1.0f), V3(0.0f, 45.0f, 0.0f));
    CollisionGeometry_GetDistance(&Cube, &Turned, &Distance);
    TEST_CHECK(run, IsNear(Distance.Distance, 2.0f - sqrtf(2.0f), NEAR_EPSILON));

    // A cylinder and a hull cube behave like their shapes.
    CollisionHull* Hull = MakeCubeHull(run, 1.0f);
    if (Hull != NULL)
    {
        CollisionShape HullShape = CollisionShape_Create(CollisionShapeType_ConvexHull);
        HullShape.Hull = Hull;
        CollisionConvex HullCube = MakeConvex(&HullShape, At(V3(0.0f, 0.0f, 0.0f)));
        CollisionGeometry_GetDistance(&HullCube, &Above, &Distance);
        TEST_CHECK(run, IsNear(Distance.Distance, 1.5f, NEAR_EPSILON));
        CollisionHull_Destroy(Hull);
    }
    CollisionConvex Can = Cylinder(V3(0.0f, 0.0f, 3.0f), 0.5f, 1.0f, V3(0.0f, 0.0f, 0.0f));
    CollisionGeometry_GetDistance(&Cube, &Can, &Distance);
    TEST_CHECK(run, IsNear(Distance.Distance, 1.5f, NEAR_EPSILON));
}

static void EpaFindsThePenetration(TestRun* run)
{
    CollisionPenetration Penetration;
    CollisionConvex Cube = Box(V3(0.0f, 0.0f, 0.0f), V3(1.0f, 1.0f, 1.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Sunk = Box(V3(0.0f, 1.7f, 0.0f), V3(0.5f, 1.0f, 0.5f), V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_GetPenetration(&Cube, &Sunk, &Penetration));
    TEST_CHECK(run, IsNear(Penetration.Depth, 0.3f, LOOSE_EPSILON));
    TEST_CHECK(run, IsNearVector(Penetration.Normal, V3(0.0f, 1.0f, 0.0f), LOOSE_EPSILON));

    // Deep: a sphere whose core is inside the box.
    CollisionConvex Inside = Sphere(V3(0.6f, 0.0f, 0.0f), 0.5f);
    CollisionDistance Distance;
    CollisionGeometry_GetDistance(&Cube, &Inside, &Distance);
    TEST_CHECK(run, Distance.AreCoresOverlapping);
    TEST_CHECK(run, CollisionGeometry_GetPenetration(&Cube, &Inside, &Penetration));
    TEST_CHECK(run, IsNear(Penetration.Depth, 0.9f, LOOSE_EPSILON));
    TEST_CHECK(run, IsNearVector(Penetration.Normal, V3(1.0f, 0.0f, 0.0f), LOOSE_EPSILON));

    // Spheres with coincident centres still get a depth.
    CollisionConvex Same = Sphere(V3(0.0f, 0.0f, 0.0f), 1.0f);
    CollisionConvex Other = Sphere(V3(0.0f, 0.0f, 0.0f), 0.5f);
    TEST_CHECK(run, CollisionGeometry_GetPenetration(&Same, &Other, &Penetration));
    TEST_CHECK(run, IsNear(Penetration.Depth, 1.5f, LOOSE_EPSILON * 5.0f));
}


// Static functions: manifolds.
static void ABoxOnABoxTouchesAtFourCorners(TestRun* run)
{
    CollisionContact Contact;
    CollisionConvex Floor = Box(V3(0.0f, -1.0f, 0.0f), V3(5.0f, 1.0f, 5.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Crate = Box(V3(0.3f, 0.51f, -0.2f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &Crate, 0.05f, &Contact));
    TEST_CHECK(run, Contact.PointCount == 4U);
    TEST_CHECK(run, IsNearVector(Contact.Normal, V3(0.0f, 1.0f, 0.0f), NEAR_EPSILON));
    bool AreSeparationsRight = true;
    for (uint32_t Point = 0U; Point < Contact.PointCount; Point++)
    {
        AreSeparationsRight = AreSeparationsRight && IsNear(Contact.Points[Point].Separation, 0.01f, NEAR_EPSILON)
            && IsNear(Contact.Points[Point].PositionA.y, 0.0f, NEAR_EPSILON)
            && IsNear(Contact.Points[Point].PositionB.y, 0.01f, NEAR_EPSILON);
    }
    TEST_CHECK(run, AreSeparationsRight);

    // Sunk a little: the same four corners, penetrating.
    CollisionConvex Sunk = Box(V3(0.3f, 0.48f, -0.2f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &Sunk, 0.05f, &Contact));
    TEST_CHECK(run, (Contact.PointCount == 4U) && IsNear(Contact.Separation, -0.02f, LOOSE_EPSILON));

    // Turned 30 degrees about Y: still four points (the turned face inside the floor's).
    CollisionConvex Turned = Box(V3(0.0f, 0.505f, 0.0f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 30.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &Turned, 0.05f, &Contact) && (Contact.PointCount == 4U));

    // A crate on a smaller box: the crossing of two squares, cut down to four points.
    CollisionConvex Post = Box(V3(0.0f, -0.5f, 0.0f), V3(0.4f, 0.5f, 0.4f), V3(0.0f, 45.0f, 0.0f));
    CollisionConvex OnPost = Box(V3(0.0f, 0.505f, 0.0f), V3(0.4f, 0.5f, 0.4f), V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Post, &OnPost, 0.05f, &Contact) && (Contact.PointCount == 4U));

    // Farther apart than asked: no contact.
    CollisionConvex Far = Box(V3(0.0f, 1.0f, 0.0f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, !CollisionGeometry_Collide(&Floor, &Far, 0.05f, &Contact));
}

static void RoundShapesTouchAtTheirPoints(TestRun* run)
{
    CollisionContact Contact;
    CollisionConvex Floor = Box(V3(0.0f, -1.0f, 0.0f), V3(5.0f, 1.0f, 5.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Ball = Sphere(V3(1.0f, 0.5f, 1.0f), 0.5f);
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &Ball, 0.05f, &Contact) && (Contact.PointCount == 1U));
    TEST_CHECK(run, IsNearVector(Contact.Points[0].PositionB, V3(1.0f, 0.0f, 1.0f), NEAR_EPSILON));

    CollisionConvex LyingCapsule = Capsule(V3(0.0f, 0.3f, 0.0f), 0.3f, 1.0f, V3(0.0f, 0.0f, 90.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &LyingCapsule, 0.05f, &Contact) && (Contact.PointCount == 2U));
    bool AreEndsBelow = true;
    for (uint32_t Point = 0U; Point < Contact.PointCount; Point++)
    {
        AreEndsBelow = AreEndsBelow && IsNear(Contact.Points[Point].PositionB.y, 0.0f, NEAR_EPSILON)
            && IsNear(fabsf(Contact.Points[Point].PositionB.x), 1.0f, NEAR_EPSILON);
    }
    TEST_CHECK(run, AreEndsBelow);

    CollisionConvex StandingCan = Cylinder(V3(0.0f, 1.001f, 0.0f), 0.5f, 1.0f, V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &StandingCan, 0.05f, &Contact) && (Contact.PointCount == 4U));
    CollisionConvex LyingCan = Cylinder(V3(0.0f, 0.501f, 0.0f), 0.5f, 1.0f, V3(90.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &LyingCan, 0.05f, &Contact) && (Contact.PointCount == 2U));

    // Two capsules side by side touch along their overlap.
    CollisionConvex Left = Capsule(V3(0.0f, 0.0f, 0.0f), 0.25f, 1.0f, V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Right = Capsule(V3(0.49f, 0.5f, 0.0f), 0.25f, 1.0f, V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Left, &Right, 0.05f, &Contact) && (Contact.PointCount == 2U));
}

static void FeatureIdsHoldAcrossASmallMove(TestRun* run)
{
    CollisionContact First;
    CollisionContact Second;
    CollisionConvex Floor = Box(V3(0.0f, -1.0f, 0.0f), V3(5.0f, 1.0f, 5.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Crate = Box(V3(0.0f, 0.505f, 0.0f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 10.0f, 0.0f));
    CollisionConvex Moved = Box(V3(0.02f, 0.504f, 0.01f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 10.5f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &Crate, 0.05f, &First));
    TEST_CHECK(run, CollisionGeometry_Collide(&Floor, &Moved, 0.05f, &Second));
    uint32_t Matched = 0U;
    for (uint32_t A = 0U; A < First.PointCount; A++)
    {
        for (uint32_t B = 0U; B < Second.PointCount; B++)
        {
            Matched += (First.Points[A].FeatureId == Second.Points[B].FeatureId) ? 1U : 0U;
        }
    }
    TEST_CHECK(run, (First.PointCount == 4U) && (Matched == 4U));
}


// Static functions: rays.
static void RaysHitEveryShape(TestRun* run)
{
    CollisionRayHit Hit;
    CollisionAffine Identity = CollisionAffine_Identity();
    CollisionShape SphereShape = CollisionShape_Create(CollisionShapeType_Sphere);
    SphereShape.Radius = 1.0f;
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&SphereShape, &Identity, &Identity, V3(-5.0f, 0.0f, 0.0f),
        V3(1.0f, 0.0f, 0.0f), 10.0f, &Hit));
    TEST_CHECK(run, IsNear(Hit.T, 4.0f, NEAR_EPSILON) && IsNearVector(Hit.Normal, V3(-1.0f, 0.0f, 0.0f), NEAR_EPSILON));
    TEST_CHECK(run, !CollisionGeometry_RaycastShape(&SphereShape, &Identity, &Identity, V3(-5.0f, 0.0f, 0.0f),
        V3(1.0f, 0.0f, 0.0f), 3.9f, &Hit));
    TEST_CHECK(run, !CollisionGeometry_RaycastShape(&SphereShape, &Identity, &Identity, V3(-5.0f, 1.01f, 0.0f),
        V3(1.0f, 0.0f, 0.0f), 10.0f, &Hit));
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&SphereShape, &Identity, &Identity, V3(0.2f, 0.0f, 0.0f),
        V3(1.0f, 0.0f, 0.0f), 10.0f, &Hit) && Hit.IsStartInside && (Hit.T == 0.0f));

    // An unevenly scaled sphere (an ellipsoid 2 m along X) is hit at its true surface.
    CollisionAffine Stretched = MakeTransform(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), V3(2.0f, 1.0f, 1.0f));
    CollisionAffine Unstretched;
    CollisionAffine_TryInvert(&Stretched, &Unstretched);
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&SphereShape, &Stretched, &Unstretched, V3(-5.0f, 0.0f, 0.0f),
        V3(1.0f, 0.0f, 0.0f), 10.0f, &Hit) && IsNear(Hit.T, 3.0f, NEAR_EPSILON));

    CollisionShape BoxShape = CollisionShape_Create(CollisionShapeType_Box);
    BoxShape.HalfExtents = V3(1.0f, 2.0f, 3.0f);
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&BoxShape, &Identity, &Identity, V3(0.5f, 10.0f, 0.5f),
        V3(0.0f, -1.0f, 0.0f), 20.0f, &Hit));
    TEST_CHECK(run, IsNear(Hit.T, 8.0f, NEAR_EPSILON) && IsNearVector(Hit.Normal, V3(0.0f, 1.0f, 0.0f), NEAR_EPSILON));

    CollisionShape CapsuleShape = CollisionShape_Create(CollisionShapeType_Capsule);
    CapsuleShape.Radius = 0.5f;
    CapsuleShape.HalfHeight = 1.0f;
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&CapsuleShape, &Identity, &Identity, V3(0.0f, 5.0f, 0.0f),
        V3(0.0f, -1.0f, 0.0f), 10.0f, &Hit) && IsNear(Hit.T, 3.5f, NEAR_EPSILON));
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&CapsuleShape, &Identity, &Identity, V3(-5.0f, 0.3f, 0.0f),
        V3(1.0f, 0.0f, 0.0f), 10.0f, &Hit) && IsNear(Hit.T, 4.5f, NEAR_EPSILON));

    CollisionShape CylinderShape = CollisionShape_Create(CollisionShapeType_Cylinder);
    CylinderShape.Radius = 0.5f;
    CylinderShape.HalfHeight = 1.0f;
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&CylinderShape, &Identity, &Identity, V3(0.2f, 5.0f, 0.0f),
        V3(0.0f, -1.0f, 0.0f), 10.0f, &Hit) && IsNear(Hit.T, 4.0f, NEAR_EPSILON)
        && IsNearVector(Hit.Normal, V3(0.0f, 1.0f, 0.0f), NEAR_EPSILON));
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&CylinderShape, &Identity, &Identity, V3(-5.0f, 0.9f, 0.0f),
        V3(1.0f, 0.0f, 0.0f), 10.0f, &Hit) && IsNear(Hit.T, 4.5f, NEAR_EPSILON));

    CollisionHull* Hull = MakeCubeHull(run, 1.0f);
    if (Hull != NULL)
    {
        CollisionShape HullShape = CollisionShape_Create(CollisionShapeType_ConvexHull);
        HullShape.Hull = Hull;
        TEST_CHECK(run, CollisionGeometry_RaycastShape(&HullShape, &Identity, &Identity, V3(0.0f, 0.0f, -4.0f),
            V3(0.0f, 0.0f, 2.0f), 10.0f, &Hit) && IsNear(Hit.T, 1.5f, NEAR_EPSILON)
            && IsNearVector(Hit.Normal, V3(0.0f, 0.0f, -1.0f), NEAR_EPSILON));
        CollisionHull_Destroy(Hull);
    }
}

static void PlanesAreHitFromTheFront(TestRun* run)
{
    CollisionRayHit Hit;
    CollisionAffine Identity = CollisionAffine_Identity();
    CollisionShape Plane = CollisionShape_Create(CollisionShapeType_Plane);
    Plane.PlaneHalfExtents = (Vector2){ 1.0f, 1.0f };
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&Plane, &Identity, &Identity, V3(0.5f, 2.0f, 0.5f),
        V3(0.0f, -1.0f, 0.0f), 10.0f, &Hit) && IsNear(Hit.T, 2.0f, NEAR_EPSILON));
    TEST_CHECK(run, !CollisionGeometry_RaycastShape(&Plane, &Identity, &Identity, V3(0.5f, -2.0f, 0.5f),
        V3(0.0f, 1.0f, 0.0f), 10.0f, &Hit));
    TEST_CHECK(run, !CollisionGeometry_RaycastShape(&Plane, &Identity, &Identity, V3(1.5f, 2.0f, 0.5f),
        V3(0.0f, -1.0f, 0.0f), 10.0f, &Hit));

    Plane.IsBounded = false;
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&Plane, &Identity, &Identity, V3(50.0f, 2.0f, 50.0f),
        V3(0.0f, -1.0f, 0.0f), 10.0f, &Hit) && IsNear(Hit.T, 2.0f, NEAR_EPSILON));
    TEST_CHECK(run, CollisionGeometry_RaycastShape(&Plane, &Identity, &Identity, V3(0.0f, -3.0f, 0.0f),
        V3(0.0f, 1.0f, 0.0f), 10.0f, &Hit) && Hit.IsStartInside);

    // Contacts: within the rectangle the plane pushes to its front; behind its back face it is not touched.
    CollisionContact Contact;
    CollisionPlaneView Floor = MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), true, (Vector2){ 2.0f, 2.0f }, 0.0f);
    CollisionConvex Crate = Box(V3(0.0f, 0.4f, 0.0f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_CollidePlane(&Floor, &Crate, 0.05f, &Contact) && (Contact.PointCount == 4U));
    TEST_CHECK(run, IsNear(Contact.Separation, -0.1f, NEAR_EPSILON)
        && IsNearVector(Contact.Normal, V3(0.0f, 1.0f, 0.0f), NEAR_EPSILON));
    CollisionConvex Below = Box(V3(0.0f, -0.3f, 0.0f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, !CollisionGeometry_CollidePlane(&Floor, &Below, 0.05f, &Contact));
    CollisionPlaneView Thick = MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), true, (Vector2){ 2.0f, 2.0f }, 1.0f);
    TEST_CHECK(run, CollisionGeometry_CollidePlane(&Thick, &Below, 0.05f, &Contact)
        && IsNear(Contact.Separation, -0.8f, NEAR_EPSILON));

    // An infinite plane pushes up whatever lies behind it.
    CollisionPlaneView Ground = MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), false, (Vector2){ 1.0f, 1.0f }, 0.0f);
    CollisionConvex Buried = Sphere(V3(30.0f, -4.0f, 0.0f), 0.5f);
    TEST_CHECK(run, CollisionGeometry_CollidePlane(&Ground, &Buried, 0.05f, &Contact)
        && IsNear(Contact.Separation, -4.5f, NEAR_EPSILON));
}

static void TrianglesGiveBarycentrics(TestRun* run)
{
    CollisionRayHit Hit;
    Vector3 A = V3(0.0f, 0.0f, 0.0f);
    Vector3 B = V3(1.0f, 0.0f, 0.0f);
    Vector3 C = V3(0.0f, 0.0f, -1.0f); // counter-clockwise seen from +Y
    TEST_CHECK(run, CollisionGeometry_RaycastTriangle(V3(0.25f, 1.0f, -0.5f), V3(0.0f, -1.0f, 0.0f), A, B, C, false,
        10.0f, &Hit));
    TEST_CHECK(run, IsNear(Hit.T, 1.0f, NEAR_EPSILON) && IsNearVector(Hit.Barycentric, V3(0.25f, 0.25f, 0.5f),
        NEAR_EPSILON));
    TEST_CHECK(run, !CollisionGeometry_RaycastTriangle(V3(0.25f, -1.0f, -0.5f), V3(0.0f, 1.0f, 0.0f), A, B, C, false,
        10.0f, &Hit));
    TEST_CHECK(run, CollisionGeometry_RaycastTriangle(V3(0.25f, -1.0f, -0.5f), V3(0.0f, 1.0f, 0.0f), A, B, C, true,
        10.0f, &Hit) && IsNearVector(Hit.Normal, V3(0.0f, -1.0f, 0.0f), NEAR_EPSILON));
}


// Static functions: casts.
static void CastsFindTheTimeOfImpact(TestRun* run)
{
    CollisionCastHit Hit;
    CollisionConvex Wall = Box(V3(5.0f, 0.0f, 0.0f), V3(0.5f, 2.0f, 2.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Ball = Sphere(V3(0.0f, 0.0f, 0.0f), 0.5f);
    TEST_CHECK(run, CollisionGeometry_Cast(&Ball, V3(10.0f, 0.0f, 0.0f), &Wall, &Hit));
    TEST_CHECK(run, IsNear(Hit.Fraction, 0.4f, NEAR_EPSILON) && !Hit.IsStartPenetrating);
    TEST_CHECK(run, IsNearVector(Hit.Normal, V3(1.0f, 0.0f, 0.0f), NEAR_EPSILON));
    TEST_CHECK(run, IsNearVector(Hit.PointB, V3(4.5f, 0.0f, 0.0f), LOOSE_EPSILON));
    TEST_CHECK(run, !CollisionGeometry_Cast(&Ball, V3(3.0f, 0.0f, 0.0f), &Wall, &Hit));
    TEST_CHECK(run, !CollisionGeometry_Cast(&Ball, V3(0.0f, 10.0f, 0.0f), &Wall, &Hit));

    // Starting inside: reported at once with the push-out.
    CollisionConvex Stuck = Sphere(V3(4.2f, 0.0f, 0.0f), 0.5f);
    TEST_CHECK(run, CollisionGeometry_Cast(&Stuck, V3(1.0f, 0.0f, 0.0f), &Wall, &Hit) && Hit.IsStartPenetrating);
    TEST_CHECK(run, IsNear(Hit.PenetrationDepth, 0.2f, LOOSE_EPSILON));

    // A box sweeping diagonally hits the wall's face with its corner.
    CollisionConvex Crate = Box(V3(0.0f, 0.0f, 0.0f), V3(0.5f, 0.5f, 0.5f), V3(0.0f, 45.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Cast(&Crate, V3(8.0f, 0.0f, 0.0f), &Wall, &Hit));
    TEST_CHECK(run, IsNear(Hit.Fraction * 8.0f, 4.5f - sqrtf(0.5f), LOOSE_EPSILON));

    // A capsule against a capsule.
    CollisionConvex Post = Capsule(V3(3.0f, 0.0f, 0.0f), 0.25f, 1.0f, V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Mover = Capsule(V3(0.0f, 0.0f, 0.0f), 0.25f, 1.0f, V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_Cast(&Mover, V3(6.0f, 0.0f, 0.0f), &Post, &Hit)
        && IsNear(Hit.Fraction * 6.0f, 2.5f, LOOSE_EPSILON));
}

static void FastThinCasesDoNotTunnel(TestRun* run)
{
    // A 1 cm wall and a ball at 1000 m/s: one 480 Hz tick moves it 2.08 m, far more than the wall is thick.
    CollisionCastHit Hit;
    CollisionConvex Sheet = Box(V3(1.0f, 0.0f, 0.0f), V3(0.005f, 1.0f, 1.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Bullet = Sphere(V3(0.0f, 0.0f, 0.0f), 0.01f);
    Vector3 Step = V3(1000.0f * TICK_SECONDS, 0.0f, 0.0f);
    TEST_CHECK(run, CollisionGeometry_Cast(&Bullet, Step, &Sheet, &Hit));
    TEST_CHECK(run, IsNear(Hit.Fraction * Step.x, 0.985f, LOOSE_EPSILON));

    // A flat rectangle (a one-way floor) stops a fast fall from above and lets a fall from below through.
    CollisionPlaneView Floor = MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), true, (Vector2){ 2.0f, 2.0f }, 0.0f);
    CollisionConvex Falling = Sphere(V3(0.0f, 1.0f, 0.0f), 0.25f);
    TEST_CHECK(run, CollisionGeometry_CastPlane(&Falling, V3(0.0f, -100.0f, 0.0f), &Floor, &Hit)
        && IsNear(Hit.Fraction * 100.0f, 0.75f, LOOSE_EPSILON));
    CollisionConvex Rising = Sphere(V3(0.0f, -1.0f, 0.0f), 0.25f);
    TEST_CHECK(run, !CollisionGeometry_CastPlane(&Rising, V3(0.0f, 100.0f, 0.0f), &Floor, &Hit));

    // The infinite plane: stopped at any speed.
    CollisionPlaneView Ground = MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), false, (Vector2){ 1.0f, 1.0f }, 0.0f);
    TEST_CHECK(run, CollisionGeometry_CastPlane(&Falling, V3(0.0f, -5000.0f, 0.0f), &Ground, &Hit)
        && IsNear(Hit.Fraction * 5000.0f, 0.75f, LOOSE_EPSILON));
}

static void RotatingCastsHitMidTurn(TestRun* run)
{
    // A 4 m bar turning a quarter about its centre: at the start it points along X, at the end along Z. A ball sits
    // on the way, at 45 degrees: both end poses are clear of it, the turn is not.
    CollisionShape BarShape = CollisionShape_Create(CollisionShapeType_Box);
    BarShape.HalfExtents = V3(2.0f, 0.1f, 0.1f);
    CollisionAffine InPose = CollisionAffine_Identity();
    CollisionConvex Bar = MakeConvex(&BarShape, InPose);
    CollisionConvex Ball = Sphere(V3(1.2f, 0.0f, 1.2f), 0.2f);
    CollisionPose From = CollisionPose_Identity();
    CollisionPose To = { .Position = V3(0.0f, 0.0f, 0.0f), .Rotation = QuaternionFromAxisAngle(V3(0.0f, 1.0f, 0.0f),
        -PI * 0.5f) };
    CollisionPose AtEnd = To;
    CollisionCastHit Hit;
    CollisionDistance Distance;
    CollisionGeometry_GetDistance(&Bar, &Ball, &Distance);
    TEST_CHECK(run, Distance.Distance > 0.5f);
    CollisionConvex EndBar = Bar;
    CollisionAffine EndPose = CollisionPose_ToAffine(&AtEnd);
    EndBar.Transform = EndPose;
    CollisionConvex_Prepare(&EndBar);
    CollisionGeometry_GetDistance(&EndBar, &Ball, &Distance);
    TEST_CHECK(run, Distance.Distance > 0.5f);
    TEST_CHECK(run, CollisionGeometry_CastMotion(&Bar, &From, &To, &Ball, &Hit));
    TEST_CHECK(run, (Hit.Fraction > 0.1f) && (Hit.Fraction < 0.6f) && !Hit.IsStartPenetrating);

    // The same turn but the ball well away: no hit.
    CollisionConvex Away = Sphere(V3(-3.0f, 0.0f, -3.0f), 0.2f);
    TEST_CHECK(run, !CollisionGeometry_CastMotion(&Bar, &From, &To, &Away, &Hit));

    // A turning blade (40 degrees a tick) against a ball in its arc: a fan does not skip over it.
    CollisionPose BladeTo = { .Position = V3(0.0f, 0.0f, 0.0f), .Rotation = QuaternionFromAxisAngle(V3(0.0f, 1.0f, 0.0f),
        -40.0f * DEG2RAD) };
    CollisionConvex InArc = Sphere(V3(1.5f * cosf(20.0f * DEG2RAD), 0.0f, 1.5f * sinf(20.0f * DEG2RAD)), 0.05f);
    TEST_CHECK(run, CollisionGeometry_CastMotion(&Bar, &From, &BladeTo, &InArc, &Hit));
    TEST_CHECK(run, (Hit.Fraction > 0.2f) && (Hit.Fraction < 0.6f));
}


// Static functions: hulls and meshes.
static void HullsHaveTheirFaces(TestRun* run)
{
    // A cube's corners, with interior points and repeats: 8 vertices, 6 square faces, 12 edges.
    Vector3 Points[20];
    size_t Count = 0U;
    for (uint32_t Corner = 0U; Corner < 8U; Corner++)
    {
        Points[Count++] = V3(((Corner & 1U) != 0U) ? 1.0f : -1.0f, ((Corner & 2U) != 0U) ? 1.0f : -1.0f,
            ((Corner & 4U) != 0U) ? 1.0f : -1.0f);
    }
    Points[Count++] = V3(0.0f, 0.0f, 0.0f);
    Points[Count++] = V3(0.5f, -0.2f, 0.3f);
    Points[Count++] = V3(1.0f, 1.0f, 1.0f);
    Points[Count++] = V3(1.0f, 0.0f, 0.0f); // on a face
    CollisionHull* Hull = NULL;
    TEST_CHECK_SUCCESS(run, CollisionHull_Create(Points, Count, COLLISION_HULL_DEFAULT_MAX_VERTICES, &Hull));
    if (Hull != NULL)
    {
        TEST_CHECK(run, (Hull->_vertexCount == 8U) && (Hull->_faceCount == 6U) && (Hull->_edgeCount == 12U));
        bool AreSquares = true;
        for (uint32_t Face = 0U; Face < Hull->_faceCount; Face++)
        {
            AreSquares = AreSquares && (Hull->_faces[Face]._vertexCount == 4U)
                && IsNear(Hull->_faces[Face]._offset, 1.0f, NEAR_EPSILON);
        }
        TEST_CHECK(run, AreSquares);
        CollisionHull_Destroy(Hull);
    }

    // Too few points, and flat points, are refused.
    TEST_CHECK_ERROR(run, CollisionHull_Create(Points, 3U, COLLISION_HULL_DEFAULT_MAX_VERTICES, &Hull),
        ErrorCode_ArgumentOutOfRange);
    Vector3 Flat[5] = { V3(0.0f, 0.0f, 0.0f), V3(1.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 1.0f), V3(1.0f, 0.0f, 1.0f),
        V3(0.5f, 0.0f, 0.5f) };
    TEST_CHECK_ERROR(run, CollisionHull_Create(Flat, 5U, COLLISION_HULL_DEFAULT_MAX_VERTICES, &Hull),
        ErrorCode_InvalidOperation);
    TEST_CHECK(run, Hull == NULL);
}

static void FineHullsAreReducedKeepingTheirExtremes(TestRun* run)
{
    // 600 points on a sphere of radius 2: a fine hull, reduced to 32 vertices.
    enum { POINT_COUNT = 600 };
    Vector3* Points = Memory_Allocate(POINT_COUNT * sizeof(Vector3));
    const float GoldenAngle = PI * (3.0f - sqrtf(5.0f));
    for (int32_t Index = 0; Index < POINT_COUNT; Index++)
    {
        float Y = 1.0f - ((2.0f * ((float)Index + 0.5f)) / (float)POINT_COUNT);
        float Ring = sqrtf(1.0f - (Y * Y));
        Points[Index] = CollisionMath_Scale(V3(Ring * cosf(GoldenAngle * (float)Index), Y,
            Ring * sinf(GoldenAngle * (float)Index)), 2.0f);
    }
    CollisionHull* Full = NULL;
    TEST_CHECK_SUCCESS(run, CollisionHull_Create(Points, POINT_COUNT, COLLISION_HULL_MAX_VERTICES, &Full));
    CollisionHull* Reduced = NULL;
    TEST_CHECK_SUCCESS(run, CollisionHull_Create(Points, POINT_COUNT, 32U, &Reduced));
    if ((Full != NULL) && (Reduced != NULL))
    {
        TEST_CHECK(run, Full->_vertexCount > 32U);
        TEST_CHECK(run, (Reduced->_vertexCount <= 32U) && (Reduced->_vertexCount >= 20U));
        // Every reduced vertex is one of the points (an extreme), and the extent stays close to the sphere's.
        bool AreOriginal = true;
        for (uint32_t Vertex = 0U; Vertex < Reduced->_vertexCount; Vertex++)
        {
            AreOriginal = AreOriginal && IsNear(CollisionMath_Length(Reduced->_vertices[Vertex]), 2.0f, NEAR_EPSILON);
        }
        TEST_CHECK(run, AreOriginal);
        TEST_CHECK(run, (Reduced->_boundsMax.x > 1.8f) && (Reduced->_boundsMin.y < -1.8f));
    }
    CollisionHull_Destroy(Full);
    CollisionHull_Destroy(Reduced);
    Memory_Free(Points);
}

static void MeshesMarkTheirSeamsInactive(TestRun* run)
{
    CollisionMesh* Quad = MakeQuadMesh(run);
    if (Quad == NULL)
    {
        return;
    }
    TEST_CHECK(run, (Quad->_vertexCount == 4U) && (Quad->_triangleCount == 2U));
    uint32_t InactiveEdges = 0U;
    for (uint32_t Triangle = 0U; Triangle < Quad->_triangleCount; Triangle++)
    {
        for (uint32_t Edge = 0U; Edge < 3U; Edge++)
        {
            InactiveEdges += ((Quad->_triangles[Triangle]._activeEdges & (1U << Edge)) == 0U) ? 1U : 0U;
        }
    }
    TEST_CHECK(run, InactiveEdges == 2U); // the diagonal, seen from both triangles

    // A ball resting on the diagonal takes the face normal, not the edge's.
    CollisionAffine Identity = CollisionAffine_Identity();
    CollisionConvex Ball = Sphere(V3(0.0f, 0.24f, 0.0f), 0.25f);
    for (uint32_t Triangle = 0U; Triangle < Quad->_triangleCount; Triangle++)
    {
        CollisionConvex Face;
        CollisionConvex_FromMeshTriangle(Quad, Triangle, &Identity, true, &Face);
        CollisionContact Contact;
        TEST_CHECK(run, CollisionGeometry_Collide(&Face, &Ball, 0.05f, &Contact));
        TEST_CHECK(run, IsNearVector(Contact.Normal, V3(0.0f, 1.0f, 0.0f), NEAR_EPSILON)
            && IsNear(Contact.Separation, -0.01f, NEAR_EPSILON));
    }

    // One-sided: the ball below is not touched.
    CollisionConvex Under = Sphere(V3(0.3f, -0.2f, 0.3f), 0.25f);
    CollisionConvex FrontOnly;
    CollisionConvex_FromMeshTriangle(Quad, 0U, &Identity, false, &FrontOnly);
    CollisionContact Contact;
    TEST_CHECK(run, !CollisionGeometry_Collide(&FrontOnly, &Under, 0.05f, &Contact));

    // Degenerate input: a triangle with no area is skipped, a mesh of only those is refused.
    static const float Line[] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f };
    CollisionMeshPart Part = { .Vertices = Line, .VertexCount = 3U, .Indices = NULL, .TriangleCount = 0U,
        .MaterialIndex = -1 };
    CollisionMesh* Empty = NULL;
    TEST_CHECK_ERROR(run, CollisionMesh_Create(&Part, 1U, MatrixIdentity(), &Empty), ErrorCode_InvalidAssetData);
    CollisionMesh_Destroy(Quad);
}

static void MeshQueriesMatchBruteForce(TestRun* run)
{
    // A bumpy 30 x 30 grid of quads.
    enum { SIDE = 31, VERTICES = SIDE * SIDE, TRIANGLES = (SIDE - 1) * (SIDE - 1) * 2 };
    float* Vertices = Memory_Allocate(VERTICES * 3U * sizeof(float));
    unsigned short* Indices = Memory_Allocate(TRIANGLES * 3U * sizeof(unsigned short));
    for (int32_t Z = 0; Z < SIDE; Z++)
    {
        for (int32_t X = 0; X < SIDE; X++)
        {
            int32_t Index = (Z * SIDE) + X;
            Vertices[(Index * 3)] = (float)X;
            Vertices[(Index * 3) + 1] = 0.5f * sinf((float)X * 0.7f) * cosf((float)Z * 0.4f);
            Vertices[(Index * 3) + 2] = (float)Z;
        }
    }
    int32_t Written = 0;
    for (int32_t Z = 0; Z < (SIDE - 1); Z++)
    {
        for (int32_t X = 0; X < (SIDE - 1); X++)
        {
            unsigned short A = (unsigned short)((Z * SIDE) + X);
            unsigned short B = (unsigned short)(A + 1);
            unsigned short C = (unsigned short)(A + SIDE);
            unsigned short D = (unsigned short)(C + 1);
            unsigned short Quad[6] = { A, C, B, B, C, D };
            for (int32_t Corner = 0; Corner < 6; Corner++)
            {
                Indices[Written++] = Quad[Corner];
            }
        }
    }
    CollisionMeshPart Part = { .Vertices = Vertices, .VertexCount = VERTICES, .Indices = Indices,
        .TriangleCount = TRIANGLES, .MaterialIndex = 3 };
    CollisionMesh* Grid = NULL;
    TEST_CHECK_SUCCESS(run, CollisionMesh_Create(&Part, 1U, MatrixIdentity(), &Grid));
    if (Grid != NULL)
    {
        TEST_CHECK(run, (Grid->_triangleCount == TRIANGLES) && (Grid->_partMaterials[0] == 3));
        // Rays straight down hit at the surface found by brute force.
        bool AreSame = true;
        for (int32_t Sample = 0; Sample < 50; Sample++)
        {
            Vector3 Origin = V3(0.37f + ((float)Sample * 0.57f), 5.0f, 0.71f + ((float)Sample * 0.53f));
            Vector3 Down = V3(0.0f, -1.0f, 0.0f);
            CollisionRayHit Fast;
            bool IsFast = CollisionGeometry_RaycastMesh(Grid, Origin, Down, 20.0f, true, &Fast);
            float Best = 20.0f;
            bool IsSlow = false;
            for (uint32_t Triangle = 0U; Triangle < Grid->_triangleCount; Triangle++)
            {
                const CollisionMeshTriangle* T = &Grid->_triangles[Triangle];
                CollisionRayHit Slow;
                if (CollisionGeometry_RaycastTriangle(Origin, Down, Grid->_vertices[T->_indices[0]],
                    Grid->_vertices[T->_indices[1]], Grid->_vertices[T->_indices[2]], true, Best, &Slow))
                {
                    Best = Slow.T;
                    IsSlow = true;
                }
            }
            AreSame = AreSame && (IsFast == IsSlow) && (!IsFast || IsNear(Fast.T, Best, NEAR_EPSILON));
        }
        TEST_CHECK(run, AreSame);
        // The interior seams of the bumpy surface are not all active (flat-ish rows are inactive).
        uint32_t Inactive = 0U;
        for (uint32_t Triangle = 0U; Triangle < Grid->_triangleCount; Triangle++)
        {
            for (uint32_t Edge = 0U; Edge < 3U; Edge++)
            {
                Inactive += ((Grid->_triangles[Triangle]._activeEdges & (1U << Edge)) == 0U) ? 1U : 0U;
            }
        }
        TEST_CHECK(run, Inactive > 0U);
        CollisionMesh_Destroy(Grid);
    }
    Memory_Free(Vertices);
    Memory_Free(Indices);
}


// Static functions: seeded random shapes.
/* A small LCG, so the cases are the same on every run and platform. */
static float NextRandom(uint32_t* state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return (float)(*state >> 8U) / 16777216.0f;
}

static float RandomBetween(uint32_t* state, float low, float high)
{
    return low + ((high - low) * NextRandom(state));
}

static Vector3 RandomVector(uint32_t* state, float extent)
{
    return V3(RandomBetween(state, -extent, extent), RandomBetween(state, -extent, extent),
        RandomBetween(state, -extent, extent));
}

/* A random primitive or hull at a random pose (the hull is the cube's, shared). */
static CollisionConvex RandomConvex(uint32_t* state, Vector3 center, const CollisionHull* hull)
{
    uint32_t Kind = (uint32_t)(NextRandom(state) * 5.0f);
    Vector3 Degrees = RandomVector(state, 180.0f);
    switch (Kind)
    {
        case 0U:
            return Sphere(center, RandomBetween(state, 0.2f, 0.8f));
        case 1U:
            return Box(center, V3(RandomBetween(state, 0.1f, 0.8f), RandomBetween(state, 0.1f, 0.8f),
                RandomBetween(state, 0.1f, 0.8f)), Degrees);
        case 2U:
            return Capsule(center, RandomBetween(state, 0.1f, 0.5f), RandomBetween(state, 0.0f, 0.8f), Degrees);
        case 3U:
            return Cylinder(center, RandomBetween(state, 0.1f, 0.6f), RandomBetween(state, 0.1f, 0.8f), Degrees);
        default:
        {
            CollisionShape Shape = CollisionShape_Create(CollisionShapeType_ConvexHull);
            Shape.Hull = hull;
            float Size = RandomBetween(state, 0.3f, 1.2f);
            return MakeConvex(&Shape, MakeTransform(center, Degrees, V3(Size, Size * 0.7f, Size * 1.3f)));
        }
    }
}

/* A copy of @p convex moved by @p offset. */
static CollisionConvex Moved(const CollisionConvex* convex, Vector3 offset)
{
    CollisionConvex Result = *convex;
    Result.Transform.Translation = CollisionMath_Add(Result.Transform.Translation, offset);
    CollisionConvex_Prepare(&Result);
    return Result;
}

static bool IsOverlapping(const CollisionConvex* a, const CollisionConvex* b)
{
    CollisionDistance Distance;
    CollisionGeometry_GetDistance(a, b, &Distance);
    return Distance.AreCoresOverlapping || (Distance.Distance <= 0.0f);
}

static void RandomDistancesAndPushOutsAgree(TestRun* run)
{
    CollisionHull* Hull = MakeCubeHull(run, 0.5f);
    if (Hull == NULL)
    {
        return;
    }
    uint32_t State = 12345U;
    uint32_t Separated = 0U;
    uint32_t Overlapped = 0U;
    bool ArePointsOnSurfaces = true;
    bool DoPushOutsSeparate = true;
    for (uint32_t Case = 0U; Case < 400U; Case++)
    {
        CollisionConvex A = RandomConvex(&State, V3(0.0f, 0.0f, 0.0f), Hull);
        CollisionConvex B = RandomConvex(&State, RandomVector(&State, 1.6f), Hull);
        CollisionDistance Distance;
        CollisionGeometry_GetDistance(&A, &B, &Distance);
        if (!Distance.AreCoresOverlapping && (Distance.Distance > 0.0f))
        {
            Separated++;
            // The closest points lie on the surfaces, along the normal, the distance apart.
            float ReachA = CollisionMath_Dot(CollisionConvex_SupportFull(&A, Distance.Normal), Distance.Normal);
            float ReachB = CollisionMath_Dot(CollisionConvex_SupportFull(&B, CollisionMath_Negate(Distance.Normal)),
                Distance.Normal);
            ArePointsOnSurfaces = ArePointsOnSurfaces
                && IsNear(CollisionMath_Dot(Distance.PointA, Distance.Normal), ReachA, LOOSE_EPSILON)
                && IsNear(CollisionMath_Dot(Distance.PointB, Distance.Normal), ReachB, LOOSE_EPSILON)
                && IsNear(CollisionMath_Length(CollisionMath_Subtract(Distance.PointB, Distance.PointA)),
                    Distance.Distance, LOOSE_EPSILON);
            continue;
        }
        Overlapped++;
        // Moving B along the push-out by its depth (and a little) separates them.
        CollisionPenetration Penetration;
        bool HasDepth = Distance.AreCoresOverlapping ? CollisionGeometry_GetPenetration(&A, &B, &Penetration) : true;
        float Depth = Distance.AreCoresOverlapping ? Penetration.Depth : -Distance.Distance;
        Vector3 Normal = Distance.AreCoresOverlapping ? Penetration.Normal : Distance.Normal;
        if (!HasDepth)
        {
            continue;
        }
        CollisionConvex Out = Moved(&B, CollisionMath_Scale(Normal, Depth + 0.01f));
        DoPushOutsSeparate = DoPushOutsSeparate && !IsOverlapping(&A, &Out);
    }
    TEST_CHECK(run, (Separated > 50U) && (Overlapped > 50U));
    TEST_CHECK(run, ArePointsOnSurfaces);
    TEST_CHECK(run, DoPushOutsSeparate);
    CollisionHull_Destroy(Hull);
}

static void RandomCastsNeverMissAContact(TestRun* run)
{
    CollisionHull* Hull = MakeCubeHull(run, 0.5f);
    if (Hull == NULL)
    {
        return;
    }
    uint32_t State = 777U;
    uint32_t Hits = 0U;
    bool AreHitsTouching = true;
    bool AreMissesClear = true;
    bool AreLinearHitsClearBefore = true;
    for (uint32_t Case = 0U; Case < 300U; Case++)
    {
        CollisionConvex Target = RandomConvex(&State, V3(0.0f, 0.0f, 0.0f), Hull);
        Vector3 Start = CollisionMath_Scale(CollisionMath_NormalizeOr(RandomVector(&State, 1.0f),
            V3(1.0f, 0.0f, 0.0f)), 4.0f);
        CollisionConvex Mover = RandomConvex(&State, Start, Hull);
        if (IsOverlapping(&Mover, &Target))
        {
            continue;
        }
        // Aim through a point near the target, sometimes past it; fast (up to 12 m in one cast).
        Vector3 Aim = RandomVector(&State, 1.2f);
        Vector3 Motion = CollisionMath_Scale(CollisionMath_Subtract(Aim, Start), RandomBetween(&State, 0.5f, 3.0f));
        CollisionCastHit Hit;
        if (CollisionGeometry_Cast(&Mover, Motion, &Target, &Hit))
        {
            Hits++;
            CollisionConvex AtHit = Moved(&Mover, CollisionMath_Scale(Motion, Hit.Fraction));
            CollisionDistance Distance;
            CollisionGeometry_GetDistance(&AtHit, &Target, &Distance);
            float Gap = Distance.Distance;
            if (Distance.AreCoresOverlapping)
            {
                CollisionPenetration Penetration;
                CollisionGeometry_GetPenetration(&AtHit, &Target, &Penetration);
                Gap = -Penetration.Depth;
            }
            // Touching at the time of impact, within the cast's tolerance either side.
            AreHitsTouching = AreHitsTouching && (fabsf(Gap) < (COLLISION_TOI_TARGET + (COLLISION_CAST_TOLERANCE * 4.0f)));
            for (uint32_t Step = 0U; Step < 64U; Step++)
            {
                float T = (Hit.Fraction * (float)Step) / 64.0f;
                CollisionConvex Before = Moved(&Mover, CollisionMath_Scale(Motion, T));
                AreLinearHitsClearBefore = AreLinearHitsClearBefore && !IsOverlapping(&Before, &Target);
            }
            continue;
        }
        // No hit: no sample along the way overlaps (fine steps catch what a coarse step would tunnel through).
        for (uint32_t Step = 0U; Step <= 512U; Step++)
        {
            CollisionConvex Along = Moved(&Mover, CollisionMath_Scale(Motion, (float)Step / 512.0f));
            AreMissesClear = AreMissesClear && !IsOverlapping(&Along, &Target);
        }
    }
    TEST_CHECK(run, Hits > 50U);
    TEST_CHECK(run, AreHitsTouching);
    TEST_CHECK(run, AreLinearHitsClearBefore);
    TEST_CHECK(run, AreMissesClear);
    CollisionHull_Destroy(Hull);
}

static void RandomTurningCastsNeverMissAContact(TestRun* run)
{
    CollisionHull* Hull = MakeCubeHull(run, 0.5f);
    if (Hull == NULL)
    {
        return;
    }
    uint32_t State = 4242U;
    uint32_t Hits = 0U;
    bool AreHitsClearBefore = true;
    bool AreMissesClear = true;
    for (uint32_t Case = 0U; Case < 400U; Case++)
    {
        CollisionConvex Target = RandomConvex(&State, RandomVector(&State, 1.5f), Hull);
        // The moving shape sits off its pose's origin, so turning sweeps it round.
        CollisionConvex InPose = RandomConvex(&State, V3(RandomBetween(&State, 0.0f, 1.5f), 0.0f, 0.0f), Hull);
        CollisionPose From = { .Position = RandomVector(&State, 0.5f), .Rotation = QuaternionIdentity() };
        Vector3 Axis = CollisionMath_NormalizeOr(RandomVector(&State, 1.0f), V3(0.0f, 1.0f, 0.0f));
        CollisionPose To = { .Position = CollisionMath_Add(From.Position, RandomVector(&State, 1.0f)),
            .Rotation = QuaternionFromAxisAngle(Axis, RandomBetween(&State, 0.5f, 3.0f)) };
        CollisionConvex AtStart;
        {
            CollisionAffine Pose = CollisionPose_ToAffine(&From);
            AtStart = InPose;
            AtStart.Transform = CollisionAffine_Multiply(&Pose, &InPose.Transform);
            CollisionConvex_Prepare(&AtStart);
        }
        if (IsOverlapping(&AtStart, &Target))
        {
            continue;
        }
        CollisionCastHit Hit;
        bool IsHit = CollisionGeometry_CastMotion(&InPose, &From, &To, &Target, &Hit);
        float End = IsHit ? Hit.Fraction : 1.0f;
        Hits += IsHit ? 1U : 0U;
        for (uint32_t Step = 0U; Step <= 256U; Step++)
        {
            float T = (End * (float)Step) / 256.0f;
            CollisionPose Pose = CollisionPose_Interpolate(&From, &To, T);
            CollisionAffine Placed = CollisionPose_ToAffine(&Pose);
            CollisionConvex Along = InPose;
            Along.Transform = CollisionAffine_Multiply(&Placed, &InPose.Transform);
            CollisionConvex_Prepare(&Along);
            bool IsClear = !IsOverlapping(&Along, &Target);
            if (IsHit)
            {
                AreHitsClearBefore = AreHitsClearBefore && IsClear;
            }
            else
            {
                AreMissesClear = AreMissesClear && IsClear;
            }
        }
    }
    TEST_CHECK(run, Hits > 30U);
    TEST_CHECK(run, AreHitsClearBefore);
    TEST_CHECK(run, AreMissesClear);
    CollisionHull_Destroy(Hull);
}


// Public functions.
// Static functions: the review's cases (2026-10-21), each once a defect.
/* A 2 m plank tilted a hair over a 10 m floor box: GJK's direction to a large face near the origin once tilted in float
 * and read a resting plank 0.4 mm inside as 0.6 mm apart. */
static void RestingBoxesReadTheirTrueDistance(TestRun* run)
{
    CollisionConvex Floor = Box(V3(0.0f, -1.0f, 0.0f), V3(5.0f, 1.0f, 5.0f), V3(0.0f, 0.0f, 0.0f));
    uint32_t State = 4242U;
    uint32_t Wrong = 0U;
    for (uint32_t Case = 0U; Case < 400U; Case++)
    {
        Vector3 Degrees = V3(RandomBetween(&State, -0.2f, 0.2f), RandomBetween(&State, 0.0f, 360.0f),
            RandomBetween(&State, -0.2f, 0.2f));
        CollisionConvex Plank = Box(V3(0.0f, 0.0f, 0.0f), V3(1.0f, 0.05f, 0.3f), Degrees);
        float Lowest = CollisionConvex_SupportFull(&Plank, V3(0.0f, -1.0f, 0.0f)).y;
        float Gap = RandomBetween(&State, -0.0015f, 0.0015f);
        Plank = Moved(&Plank, V3(RandomBetween(&State, -3.0f, 3.0f), Gap - Lowest, RandomBetween(&State, -3.0f, 3.0f)));
        CollisionDistance Distance;
        CollisionGeometry_GetDistance(&Plank, &Floor, &Distance);
        bool IsRight = (Gap > 1e-4f) ? (!Distance.AreCoresOverlapping && IsNear(Distance.Distance, Gap, 1e-4f))
            : ((Gap < -1e-4f) ? (Distance.AreCoresOverlapping || (Distance.Distance <= 0.0f)) : true);
        Wrong += IsRight ? 0U : 1U;
    }
    TEST_CHECK(run, Wrong == 0U);
}

/* A shape left where a cast stopped (within rounding of touching) moves away and along freely and is stopped only going
 * deeper, by the linear and the turning cast alike; once every follow-up was blocked at fraction 0. */
static void CastsFromRestMoveAwayFreely(TestRun* run)
{
    CollisionConvex Floor = Box(V3(0.0f, -1.0f, 0.0f), V3(5.0f, 1.0f, 5.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Shapes[3] =
    {
        Sphere(V3(0.3f, 2.0f, -0.2f), 0.4f), Capsule(V3(-0.4f, 2.0f, 0.1f), 0.3f, 0.5f, V3(20.0f, 0.0f, 70.0f)),
        Box(V3(0.1f, 2.0f, 0.4f), V3(0.4f, 0.3f, 0.2f), V3(0.0f, 33.0f, 0.0f))
    };
    for (uint32_t Index = 0U; Index < 3U; Index++)
    {
        CollisionCastHit Hit;
        if (!TEST_CHECK(run, CollisionGeometry_Cast(&Shapes[Index], V3(0.0f, -5.0f, 0.0f), &Floor, &Hit)))
        {
            continue;
        }
        CollisionConvex Resting = Moved(&Shapes[Index], V3(0.0f, -5.0f * Hit.Fraction, 0.0f));
        CollisionCastHit Follow;
        TEST_CHECK(run, !CollisionGeometry_Cast(&Resting, V3(1.0f, 0.0f, 0.0f), &Floor, &Follow));
        TEST_CHECK(run, !CollisionGeometry_Cast(&Resting, V3(0.0f, 1.0f, 0.0f), &Floor, &Follow));
        TEST_CHECK(run, CollisionGeometry_Cast(&Resting, V3(0.3f, -1.0f, 0.0f), &Floor, &Follow)
            && IsNear(Follow.Fraction, 0.0f, 1e-3f) && IsNearVector(Follow.Normal, V3(0.0f, -1.0f, 0.0f), 1e-3f));
        CollisionPose From = CollisionPose_Identity();
        CollisionPose Sideways = { .Position = V3(1.0f, 0.0f, 0.0f), .Rotation = QuaternionIdentity() };
        CollisionPose Up = { .Position = V3(0.0f, 1.0f, 0.0f), .Rotation = QuaternionIdentity() };
        TEST_CHECK(run, !CollisionGeometry_CastMotion(&Resting, &From, &Sideways, &Floor, &Follow));
        TEST_CHECK(run, !CollisionGeometry_CastMotion(&Resting, &From, &Up, &Floor, &Follow));
    }
}

/* A plank resting a hair above a floor tips 45 degrees: its corner digs in at once, also against every kind of plane
 * (the turning plane cast once dropped the turn there); yawing on the floor digs nothing (a turn about the normal). */
static void TurningCastsNearAFloorKeepTheirTurn(TestRun* run)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = V3(1.0f, 0.1f, 1.0f);
    CollisionAffine InPose = At(V3(0.0f, 0.0f, 0.0f));
    CollisionConvex Plank = MakeConvex(&Shape, InPose);
    CollisionPose From = { .Position = V3(0.0f, 0.1f + 5e-4f, 0.0f), .Rotation = QuaternionIdentity() };
    CollisionPose Tipped = { .Position = V3(0.0f, 0.1f + 1.5e-3f, 0.0f),
        .Rotation = QuaternionFromAxisAngle(V3(0.0f, 0.0f, 1.0f), PI * 0.25f) };
    CollisionPose Yawed = { .Position = From.Position, .Rotation = QuaternionFromAxisAngle(V3(0.0f, 1.0f, 0.0f), PI) };
    CollisionPlaneView Planes[3] =
    {
        MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), false, (Vector2){ 1.0f, 1.0f }, 0.0f),
        MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), true, (Vector2){ 5.0f, 5.0f }, 0.0f),
        MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), true, (Vector2){ 5.0f, 5.0f }, 0.5f)
    };
    CollisionCastHit Hit;
    for (uint32_t Index = 0U; Index < 3U; Index++)
    {
        TEST_CHECK(run, CollisionGeometry_CastMotionPlane(&Plank, &From, &Tipped, &Planes[Index], &Hit)
            && (Hit.Fraction < 0.01f) && (Hit.Normal.y < -0.99f));
        TEST_CHECK(run, !CollisionGeometry_CastMotionPlane(&Plank, &From, &Yawed, &Planes[Index], &Hit));
    }
    CollisionConvex Floor = Box(V3(0.0f, -1.0f, 0.0f), V3(5.0f, 1.0f, 5.0f), V3(0.0f, 0.0f, 0.0f));
    TEST_CHECK(run, CollisionGeometry_CastMotion(&Plank, &From, &Tipped, &Floor, &Hit) && (Hit.Fraction < 0.01f));
    TEST_CHECK(run, !CollisionGeometry_CastMotion(&Plank, &From, &Yawed, &Floor, &Hit));
}

/* A capsule spinning about its own axis near a wall moves nothing toward it (once false hits a few millimetres off). */
static void SpinningInPlaceHitsNothing(TestRun* run)
{
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Capsule);
    Shape.Radius = 0.3f;
    Shape.HalfHeight = 0.6f;
    CollisionConvex Spinner = MakeConvex(&Shape, At(V3(0.0f, 0.0f, 0.0f)));
    CollisionConvex Wall = Box(V3(1.0f + 0.3f + 0.002f, 0.0f, 0.0f), V3(1.0f, 2.0f, 2.0f), V3(0.0f, 0.0f, 0.0f));
    CollisionPose From = CollisionPose_Identity();
    CollisionPose To = { .Position = V3(0.0f, 0.0f, 0.0f), .Rotation = QuaternionFromAxisAngle(V3(0.0f, 1.0f, 0.0f),
        PI * 0.9f) };
    CollisionCastHit Hit;
    TEST_CHECK(run, !CollisionGeometry_CastMotion(&Spinner, &From, &To, &Wall, &Hit));
}

/* A one-sided triangle is passed from behind by both casts (as by rays and contacts), and hit from the front. */
static void OneSidedTrianglesArePassedFromBehind(TestRun* run)
{
    CollisionMesh* Quad = MakeQuadMesh(run);
    if (Quad == NULL)
    {
        return;
    }
    CollisionAffine Identity = CollisionAffine_Identity();
    CollisionConvex Triangle;
    CollisionConvex_FromMeshTriangle(Quad, 0U, &Identity, false, &Triangle);
    CollisionConvex Below = Sphere(V3(0.3f, -1.0f, -0.4f), 0.2f);
    CollisionConvex Above = Sphere(V3(0.3f, 1.0f, -0.4f), 0.2f);
    CollisionCastHit Hit;
    TEST_CHECK(run, !CollisionGeometry_Cast(&Below, V3(0.0f, 2.0f, 0.0f), &Triangle, &Hit));
    TEST_CHECK(run, CollisionGeometry_Cast(&Above, V3(0.0f, -2.0f, 0.0f), &Triangle, &Hit)
        && IsNear(Hit.Fraction, 0.4f, 1e-3f) && IsNearVector(Hit.Normal, V3(0.0f, -1.0f, 0.0f), 1e-3f));
    CollisionPose From = CollisionPose_Identity();
    CollisionPose Up = { .Position = V3(0.0f, 2.0f, 0.0f), .Rotation = QuaternionFromAxisAngle(V3(1.0f, 0.0f, 0.0f), 0.3f) };
    TEST_CHECK(run, !CollisionGeometry_CastMotion(&Below, &From, &Up, &Triangle, &Hit));
    CollisionMesh_Destroy(Quad);
}

/* A thin ellipsoid's reduced hull keeps its thickness (plain directions once picked its rim and lost 83% of it). */
static void ThinHullsKeepTheirThickness(TestRun* run)
{
    Vector3 Points[600];
    const float GoldenAngle = PI * (3.0f - sqrtf(5.0f));
    float Top = 0.0f;
    for (uint32_t Index = 0U; Index < 600U; Index++)
    {
        float Y = 1.0f - ((2.0f * ((float)Index + 0.5f)) / 600.0f);
        float Ring = sqrtf(fmaxf(0.0f, 1.0f - (Y * Y)));
        Points[Index] = V3(Ring * cosf(GoldenAngle * (float)Index), Y * 0.03f, Ring * sinf(GoldenAngle * (float)Index));
        Top = fmaxf(Top, Points[Index].y);
    }
    CollisionHull* Hull = NULL;
    if (TEST_CHECK_SUCCESS(run, CollisionHull_Create(Points, 600U, 64U, &Hull)))
    {
        TEST_CHECK(run, (Hull->_vertexCount <= 64U) && IsNear(Hull->_boundsMax.y, Top, Top * 0.01f)
            && IsNear(Hull->_boundsMin.y, -Top, Top * 0.01f));
    }
    CollisionHull_Destroy(Hull);
    // A sphere's reduced hull still wraps every point closely, below as above (a pass that stopped early once picked
    // only directions near the top: a dome on a cone).
    for (uint32_t Index = 0U; Index < 600U; Index++)
    {
        Points[Index].y /= 0.03f;
    }
    if (TEST_CHECK_SUCCESS(run, CollisionHull_Create(Points, 600U, 64U, &Hull)))
    {
        float WorstOutside = 0.0f;
        for (uint32_t Index = 0U; Index < 600U; Index++)
        {
            float Outside = -FLT_MAX;
            for (uint32_t Face = 0U; Face < Hull->_faceCount; Face++)
            {
                Outside = fmaxf(Outside, CollisionMath_Dot(Hull->_faces[Face]._normal, Points[Index])
                    - Hull->_faces[Face]._offset);
            }
            WorstOutside = fmaxf(WorstOutside, Outside);
        }
        TEST_CHECK(run, (Hull->_vertexCount == 64U) && (WorstOutside < 0.06f));
    }
    CollisionHull_Destroy(Hull);
    // The most points a hull takes builds in a moment (the faces' extraction was once quadratic: two minutes).
    Vector3* Many = Memory_Allocate(16384U * sizeof(Vector3));
    for (uint32_t Index = 0U; Index < 16384U; Index++)
    {
        float Y = 1.0f - ((2.0f * ((float)Index + 0.5f)) / 16384.0f);
        float Ring = sqrtf(fmaxf(0.0f, 1.0f - (Y * Y)));
        Many[Index] = V3(Ring * cosf(GoldenAngle * (float)Index), Y, Ring * sinf(GoldenAngle * (float)Index));
    }
    TEST_CHECK_SUCCESS(run, CollisionHull_Create(Many, 16384U, COLLISION_HULL_DEFAULT_MAX_VERTICES, &Hull));
    CollisionHull_Destroy(Hull);
    Memory_Free(Many);
}

/* Capsules crossed at an angle touch at one point; lying side by side, along their overlap (two). Crossed ones once got
 * two points where their surfaces were apart. */
static void CrossedCapsulesTouchAtOnePoint(TestRun* run)
{
    CollisionConvex Lower = Capsule(V3(0.0f, 0.0f, 0.0f), 0.1f, 1.0f, V3(0.0f, 0.0f, 90.0f));
    static const float Angles[] = { 10.0f, 30.0f, 60.0f, 85.0f };
    for (uint32_t Index = 0U; Index < 4U; Index++)
    {
        CollisionConvex Upper = Capsule(V3(0.0f, 0.195f, 0.0f), 0.1f, 1.0f, V3(0.0f, Angles[Index], 90.0f));
        CollisionContact Contact;
        TEST_CHECK(run, CollisionGeometry_Collide(&Lower, &Upper, 0.0f, &Contact) && (Contact.PointCount == 1U)
            && IsNear(Contact.Separation, -0.005f, 1e-4f) && IsNearVector(Contact.Points[0].PositionA, V3(0.0f, 0.1f, 0.0f),
                1e-3f));
    }
    CollisionConvex Beside = Capsule(V3(0.0f, 0.195f, 0.0f), 0.1f, 1.0f, V3(0.0f, 0.0f, 90.0f));
    CollisionContact Contact;
    TEST_CHECK(run, CollisionGeometry_Collide(&Lower, &Beside, 0.0f, &Contact) && (Contact.PointCount == 2U));
}

/* Rays at a gently bumpy grid's shared edges and corners never slip between its triangles. */
static void RaysDoNotSlipBetweenMeshTriangles(TestRun* run)
{
    enum { Side = 21 };
    float Vertices[Side * Side * 3];
    unsigned short Indices[(Side - 1) * (Side - 1) * 6];
    for (int Z = 0; Z < Side; Z++)
    {
        for (int X = 0; X < Side; X++)
        {
            float* Vertex = &Vertices[((Z * Side) + X) * 3];
            Vertex[0] = (float)X - 10.0f;
            Vertex[1] = 0.05f * sinf((float)X * 0.7f) * cosf((float)Z * 0.9f);
            Vertex[2] = (float)Z - 10.0f;
        }
    }
    uint32_t Count = 0U;
    for (int Z = 0; Z < (Side - 1); Z++)
    {
        for (int X = 0; X < (Side - 1); X++)
        {
            unsigned short Corner = (unsigned short)((Z * Side) + X);
            unsigned short Quad[6] = { Corner, (unsigned short)(Corner + Side), (unsigned short)(Corner + Side + 1),
                Corner, (unsigned short)(Corner + Side + 1), (unsigned short)(Corner + 1) };
            for (uint32_t Entry = 0U; Entry < 6U; Entry++)
            {
                Indices[Count++] = Quad[Entry];
            }
        }
    }
    CollisionMeshPart Part = { .Vertices = Vertices, .VertexCount = Side * Side, .Indices = Indices,
        .TriangleCount = (Side - 1) * (Side - 1) * 2, .MaterialIndex = 0 };
    CollisionMesh* Grid = NULL;
    if (!TEST_CHECK_SUCCESS(run, CollisionMesh_Create(&Part, 1U, MatrixIdentity(), &Grid)))
    {
        return;
    }
    uint32_t Misses = 0U;
    for (int Z = 1; Z < (Side - 1); Z++)
    {
        for (int X = 1; X < (Side - 1); X++)
        {
            // A corner, the middle of a grid edge and the middle of a diagonal, from slightly tilted directions.
            Vector3 Targets[3] = { V3((float)X - 10.0f, 0.0f, (float)Z - 10.0f), V3((float)X - 9.5f, 0.0f,
                (float)Z - 10.0f), V3((float)X - 9.5f, 0.0f, (float)Z - 9.5f) };
            for (uint32_t Target = 0U; Target < 3U; Target++)
            {
                Vector3 Origin = CollisionMath_Add(Targets[Target], V3(0.013f, 2.0f, -0.007f));
                CollisionRayHit Hit;
                bool IsHit = CollisionGeometry_RaycastMesh(Grid, Origin, CollisionMath_Subtract(Targets[Target], Origin),
                    10.0f, false, &Hit);
                Misses += IsHit ? 0U : 1U;
            }
        }
    }
    TEST_CHECK(run, Misses == 0U);
    CollisionMesh_Destroy(Grid);
}

/* Random casts: the hit's normal faces the motion and agrees with the gap's direction a millimetre before the contact,
 * the two points coincide on their surfaces, and at the contact touching shapes have no depth. A sliver simplex once
 * placed points millimetres off, EPA once read touching ellipsoids as 4 mm deep, and normals once pointed backwards. */
static void RandomCastHitsAreWhereTheShapesMeet(TestRun* run)
{
    CollisionHull* Hull = MakeCubeHull(run, 0.5f);
    uint32_t State = 9001U;
    uint32_t Hits = 0U;
    uint32_t Problems = 0U;
    for (uint32_t Case = 0U; (Case < 3000U) && (Hull != NULL); Case++)
    {
        CollisionConvex Target = RandomConvex(&State, V3(0.0f, 0.0f, 0.0f), Hull);
        Vector3 Start = CollisionMath_Scale(CollisionMath_NormalizeOr(RandomVector(&State, 1.0f), V3(1.0f, 0.0f, 0.0f)),
            4.0f);
        CollisionConvex Mover = RandomConvex(&State, Start, Hull);
        Vector3 Motion = CollisionMath_Scale(CollisionMath_Subtract(RandomVector(&State, 1.2f), Start),
            RandomBetween(&State, 0.5f, 3.0f));
        CollisionCastHit Hit;
        if (IsOverlapping(&Mover, &Target) || !CollisionGeometry_Cast(&Mover, Motion, &Target, &Hit))
        {
            continue;
        }
        Hits++;
        float Length = CollisionMath_Length(Motion);
        CollisionConvex Before = Moved(&Mover, CollisionMath_Scale(Motion, Hit.Fraction - (0.001f / Length)));
        CollisionDistance Reference;
        CollisionGeometry_GetDistance(&Before, &Target, &Reference);
        bool IsNormalRight = (CollisionMath_Dot(Hit.Normal, Motion) > 0.0f) && (Reference.AreCoresOverlapping
            || (CollisionMath_Dot(Hit.Normal, Reference.Normal) > cosf(25.0f * DEG2RAD)));
        bool ArePointsRight = CollisionMath_Length(CollisionMath_Subtract(Hit.PointA, Hit.PointB)) <= 1e-3f;
        CollisionConvex AtHit = Moved(&Mover, CollisionMath_Scale(Motion, Hit.Fraction));
        CollisionPenetration Penetration;
        bool IsDepthRight = !IsOverlapping(&AtHit, &Target) || !CollisionGeometry_GetPenetration(&AtHit, &Target,
            &Penetration) || (Penetration.Depth <= 2e-4f);
        Problems += (IsNormalRight && ArePointsRight && IsDepthRight) ? 0U : 1U;
    }
    TEST_CHECK(run, (Hits > 500U) && (Problems == 0U));
    CollisionHull_Destroy(Hull);
}

/* A tilted box dropped onto a bounded floor plane, turning a little: a turning cast's step once landed in the overlap and
 * kept a placeholder normal, and the plane took it for a hit from behind (84 of 2000 passed straight through). */
static void TurningDropsLandOnBoundedFloors(TestRun* run)
{
    CollisionPlaneView Floor = MakePlane(V3(0.0f, 0.0f, 0.0f), V3(0.0f, 0.0f, 0.0f), true, (Vector2){ 5.0f, 5.0f }, 0.5f);
    uint32_t State = 77U;
    uint32_t Passed = 0U;
    for (uint32_t Case = 0U; Case < 400U; Case++)
    {
        CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
        Shape.HalfExtents = V3(RandomBetween(&State, 0.2f, 0.8f), RandomBetween(&State, 0.1f, 0.4f),
            RandomBetween(&State, 0.2f, 0.8f));
        CollisionConvex Falling = MakeConvex(&Shape, At(V3(0.0f, 0.0f, 0.0f)));
        Vector3 Axis = CollisionMath_NormalizeOr(RandomVector(&State, 1.0f), V3(1.0f, 0.0f, 0.0f));
        CollisionPose From = { .Position = V3(RandomBetween(&State, -3.0f, 3.0f), 1.5f, RandomBetween(&State, -3.0f, 3.0f)),
            .Rotation = QuaternionFromAxisAngle(Axis, RandomBetween(&State, -0.3f, 0.3f)) };
        CollisionPose To = { .Position = CollisionMath_Add(From.Position, V3(0.0f, -1.5f, 0.0f)),
            .Rotation = QuaternionFromAxisAngle(Axis, RandomBetween(&State, -0.3f, 0.3f)) };
        CollisionCastHit Hit;
        bool IsHit = CollisionGeometry_CastMotionPlane(&Falling, &From, &To, &Floor, &Hit);
        Passed += (IsHit && (Hit.Normal.y < -0.5f)) ? 0U : 1U;
    }
    TEST_CHECK(run, Passed == 0U);
}


void CollisionGeometryTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "affine_transforms_invert_and_compose", AffineTransformsInvertAndCompose },
        { "closest_points_on_segments_and_triangles", ClosestPointsOnSegmentsAndTriangles },
        { "poses_turn_at_a_constant_rate", PosesTurnAtAConstantRate },
        { "supports_follow_every_transform", SupportsFollowEveryTransform },
        { "gjk_finds_the_exact_distance", GjkFindsTheExactDistance },
        { "epa_finds_the_penetration", EpaFindsThePenetration },
        { "a_box_on_a_box_touches_at_four_corners", ABoxOnABoxTouchesAtFourCorners },
        { "round_shapes_touch_at_their_points", RoundShapesTouchAtTheirPoints },
        { "feature_ids_hold_across_a_small_move", FeatureIdsHoldAcrossASmallMove },
        { "rays_hit_every_shape", RaysHitEveryShape },
        { "planes_are_hit_from_the_front", PlanesAreHitFromTheFront },
        { "triangles_give_barycentrics", TrianglesGiveBarycentrics },
        { "casts_find_the_time_of_impact", CastsFindTheTimeOfImpact },
        { "fast_thin_cases_do_not_tunnel", FastThinCasesDoNotTunnel },
        { "rotating_casts_hit_mid_turn", RotatingCastsHitMidTurn },
        { "hulls_have_their_faces", HullsHaveTheirFaces },
        { "fine_hulls_are_reduced_keeping_their_extremes", FineHullsAreReducedKeepingTheirExtremes },
        { "meshes_mark_their_seams_inactive", MeshesMarkTheirSeamsInactive },
        { "mesh_queries_match_brute_force", MeshQueriesMatchBruteForce },
        { "random_distances_and_push_outs_agree", RandomDistancesAndPushOutsAgree },
        { "random_casts_never_miss_a_contact", RandomCastsNeverMissAContact },
        { "random_turning_casts_never_miss_a_contact", RandomTurningCastsNeverMissAContact },
        { "resting_boxes_read_their_true_distance", RestingBoxesReadTheirTrueDistance },
        { "casts_from_rest_move_away_freely", CastsFromRestMoveAwayFreely },
        { "turning_casts_near_a_floor_keep_their_turn", TurningCastsNearAFloorKeepTheirTurn },
        { "spinning_in_place_hits_nothing", SpinningInPlaceHitsNothing },
        { "one_sided_triangles_are_passed_from_behind", OneSidedTrianglesArePassedFromBehind },
        { "thin_hulls_keep_their_thickness", ThinHullsKeepTheirThickness },
        { "crossed_capsules_touch_at_one_point", CrossedCapsulesTouchAtOnePoint },
        { "rays_do_not_slip_between_mesh_triangles", RaysDoNotSlipBetweenMeshTriangles },
        { "random_cast_hits_are_where_the_shapes_meet", RandomCastHitsAreWhereTheShapesMeet },
        { "turning_drops_land_on_bounded_floors", TurningDropsLandOnBoundedFloors },
    };
    TestRun_RunSuite(run, "CollisionGeometry", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
