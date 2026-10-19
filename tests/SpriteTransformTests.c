#include "TestSuites.h"
#include "SpriteTransform.h"
#include "raylib/raymath.h"
#include <math.h>
#include <string.h>


// Macros.
/** Tolerance for the float comparisons. */
#define TEST_EPSILON 1e-4f


// Static functions.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static bool IsNearVector(Vector3 a, Vector3 b)
{
    return IsNear(a.x, b.x) && IsNear(a.y, b.y) && IsNear(a.z, b.z);
}

/* Where the matrix puts a point of the image (x, y in [0, 1]). */
static Vector3 Corner(Matrix matrix, float x, float y)
{
    return Vector3Transform((Vector3){ x, y, 0.0f }, matrix);
}

/* The image's X (width) and Y (height) axes under a sprite matrix: its first two columns. */
static Vector3 WidthAxis(Matrix matrix)
{
    return (Vector3){ matrix.m0, matrix.m1, matrix.m2 };
}

static Vector3 HeightAxis(Matrix matrix)
{
    return (Vector3){ matrix.m4, matrix.m5, matrix.m6 };
}

/* The sign of the matrix's 3x3 part: negative when the image is mirrored once. */
static float Handedness(Matrix matrix)
{
    return Vector3DotProduct(Vector3CrossProduct(WidthAxis(matrix), HeightAxis(matrix)),
        SpriteTransform_GetNormal(matrix));
}

/* A sprite with the world's axes as its rotation. */
static SpriteShape MakeShape(WorldSpriteOrientation orientation, Vector3 position, Vector2 size, Vector2 pivot)
{
    return (SpriteShape)
    {
        .Orientation = orientation, .Position = position,
        .AxisX = { 1.0f, 0.0f, 0.0f }, .AxisY = { 0.0f, 1.0f, 0.0f }, .AxisZ = { 0.0f, 0.0f, 1.0f },
        .Size = size, .Pivot = pivot, .IsFlippedX = false, .IsFlippedY = false
    };
}

/* The shape's axes from an Euler rotation, as the render cache takes them from the object. */
static void SetRotation(SpriteShape* shape, Vector3 rotation)
{
    Matrix Rotation = MatrixRotateXYZ(rotation);
    shape->AxisX = (Vector3){ Rotation.m0, Rotation.m1, Rotation.m2 };
    shape->AxisY = (Vector3){ Rotation.m4, Rotation.m5, Rotation.m6 };
    shape->AxisZ = (Vector3){ Rotation.m8, Rotation.m9, Rotation.m10 };
}


// Tests.
/* A billboard's normal points at the camera, its image leans its top toward the camera's up, its right is the viewer's
 * right (the image is not mirrored), and its axes carry its size. */
static void BillboardFacesTheCamera(TestRun* run)
{
    Vector3 Position = { 1.0f, 2.0f, 3.0f };
    SpriteShape Shape = MakeShape(WorldSpriteOrientation_Billboard, Position, (Vector2){ 2.0f, 3.0f },
        (Vector2){ 0.5f, 0.5f });
    Vector3 CameraPosition = { 1.0f, 5.0f, 7.0f }; // 3 up and 4 back: the direction to it is (0, 0.6, 0.8)
    SpriteViewer Viewer = SpriteViewer_FromPoint(CameraPosition, (Vector3){ 0.0f, 1.0f, 0.0f });
    Matrix Result = SpriteTransform_GetMatrix(&Shape, &Viewer);

    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), (Vector3){ 0.0f, 0.6f, 0.8f }));
    TEST_CHECK(run, IsNearVector(Vector3Normalize(HeightAxis(Result)), (Vector3){ 0.0f, 0.8f, -0.6f }));
    TEST_CHECK(run, IsNearVector(Vector3Normalize(WidthAxis(Result)), (Vector3){ 1.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, IsNear(Vector3Length(WidthAxis(Result)), 2.0f) && IsNear(Vector3Length(HeightAxis(Result)), 3.0f));
    TEST_CHECK(run, Handedness(Result) > 0.0f);

    // The image's right is the viewer's right: raylib's camera has right = forward x up.
    Matrix View = MatrixLookAt(CameraPosition, Position, (Vector3){ 0.0f, 1.0f, 0.0f });
    Vector3 CameraRight = { View.m0, View.m4, View.m8 };
    TEST_CHECK(run, Vector3DotProduct(Vector3Normalize(WidthAxis(Result)), CameraRight) > 0.99f);

    // Wherever the camera stands, the normal follows it.
    Vector3 Cameras[3] = { { -6.0f, 2.5f, 3.5f }, { 1.5f, -4.0f, -1.0f }, { 30.0f, 40.0f, -50.0f } };
    for (size_t Index = 0; Index < 3; Index++)
    {
        Viewer = SpriteViewer_FromPoint(Cameras[Index], (Vector3){ 0.0f, 1.0f, 0.0f });
        Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
        Vector3 Toward = Vector3Normalize(Vector3Subtract(Cameras[Index], Position));
        TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), Toward));
        TEST_CHECK(run, IsNear(Vector3DotProduct(HeightAxis(Result), Toward), 0.0f));
        TEST_CHECK(run, Vector3DotProduct(HeightAxis(Result), (Vector3){ 0.0f, 1.0f, 0.0f }) >= 0.0f);
    }

    // Straight above, the world's up gives no lean: another up stands in, and the matrix stays finite.
    Viewer = SpriteViewer_FromPoint((Vector3){ 1.0f, 12.0f, 3.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), (Vector3){ 0.0f, 1.0f, 0.0f }));
    TEST_CHECK(run, IsNear(Vector3Length(HeightAxis(Result)), 3.0f) && isfinite(Result.m12));

    // A camera at the pivot gives no direction: the sprite faces its object's +Z.
    Viewer = SpriteViewer_FromPoint(Position, (Vector3){ 0.0f, 1.0f, 0.0f });
    Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), (Vector3){ 0.0f, 0.0f, 1.0f }));
}

/* An axis billboard keeps its object's up axis and turns around it to face the camera; looking straight along the axis
 * it faces its object's +Z. */
static void AxisBillboardTurnsAroundItsAxis(TestRun* run)
{
    SpriteShape Shape = MakeShape(WorldSpriteOrientation_AxisBillboard, (Vector3){ 0.0f, 0.0f, 0.0f },
        (Vector2){ 2.0f, 4.0f }, (Vector2){ 0.5f, 0.0f });
    SpriteViewer Viewer = SpriteViewer_FromPoint((Vector3){ 5.0f, 10.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    Matrix Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(Vector3Normalize(HeightAxis(Result)), (Vector3){ 0.0f, 1.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), (Vector3){ 1.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, Handedness(Result) > 0.0f);
    // It stands on its pivot: the bottom centre is at the object's position, the top 4 units up the axis.
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.5f, 0.0f), (Vector3){ 0.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.5f, 1.0f), (Vector3){ 0.0f, 4.0f, 0.0f }));

    // A tilted object: the axis is its local Y, wherever that points.
    SetRotation(&Shape, (Vector3){ PI / 2.0f, 0.0f, 0.0f }); // local Y -> world +Z
    Viewer = SpriteViewer_FromPoint((Vector3){ 3.0f, -2.0f, 9.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(Vector3Normalize(HeightAxis(Result)), Shape.AxisY));
    TEST_CHECK(run, IsNear(Vector3DotProduct(SpriteTransform_GetNormal(Result), Shape.AxisY), 0.0f));
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), Vector3Normalize((Vector3){ 3.0f, -2.0f, 0.0f })));

    // Straight along the axis the camera gives no direction around it: the sprite faces its object's +Z.
    Shape = MakeShape(WorldSpriteOrientation_AxisBillboard, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector2){ 2.0f, 4.0f },
        (Vector2){ 0.5f, 0.0f });
    Viewer = SpriteViewer_FromPoint((Vector3){ 0.0f, 20.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f });
    Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), (Vector3){ 0.0f, 0.0f, 1.0f }));
}

/* A fixed sprite takes its object's rotation whatever looks at it. */
static void FixedUsesTheObjectsRotation(TestRun* run)
{
    Vector3 Position = { 4.0f, -1.0f, 2.0f };
    SpriteShape Shape = MakeShape(WorldSpriteOrientation_Fixed, Position, (Vector2){ 2.0f, 3.0f },
        (Vector2){ 0.0f, 0.0f });
    SetRotation(&Shape, (Vector3){ 0.3f, 1.1f, -0.4f });
    SpriteViewer Viewer = SpriteViewer_FromPoint((Vector3){ 50.0f, 50.0f, 50.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    Matrix Result = SpriteTransform_GetMatrix(&Shape, &Viewer);

    // What the model path does with the same rotation: scale, then rotate, then translate.
    Matrix ModelPath = MatrixMultiply(MatrixMultiply(MatrixScale(2.0f, 3.0f, 1.0f), MatrixRotateXYZ((Vector3){ 0.3f, 1.1f,
        -0.4f })), MatrixTranslate(Position.x, Position.y, Position.z));
    const float Points[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };
    for (size_t Index = 0; Index < 4; Index++)
    {
        TEST_CHECK(run, IsNearVector(Corner(Result, Points[Index][0], Points[Index][1]),
            Vector3Transform((Vector3){ Points[Index][0], Points[Index][1], 0.0f }, ModelPath)));
    }
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), Shape.AxisZ));

    Viewer = SpriteViewer_FromDirection((Vector3){ 0.0f, -1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f });
    Matrix FromLight = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, memcmp(&Result, &FromLight, sizeof(Matrix)) == 0);
}

/* The pivot is the point of the image at the object's position, and the flips mirror the image about it. */
static void PivotAndFlipsMoveAndMirrorTheCorners(TestRun* run)
{
    SpriteShape Shape = MakeShape(WorldSpriteOrientation_Fixed, (Vector3){ 10.0f, 0.0f, 0.0f }, (Vector2){ 2.0f, 4.0f },
        (Vector2){ 0.5f, 0.0f });
    SpriteViewer Viewer = SpriteViewer_FromPoint((Vector3){ 0.0f, 0.0f, 10.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    Matrix Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.0f, 0.0f), (Vector3){ 9.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 1.0f, 0.0f), (Vector3){ 11.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.0f, 1.0f), (Vector3){ 9.0f, 4.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 1.0f, 1.0f), (Vector3){ 11.0f, 4.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.5f, 0.0f), (Vector3){ 10.0f, 0.0f, 0.0f }));

    Shape.IsFlippedX = true;
    Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.0f, 0.0f), (Vector3){ 11.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 1.0f, 1.0f), (Vector3){ 9.0f, 4.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.5f, 0.0f), (Vector3){ 10.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, Handedness(Result) < 0.0f);
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(Result), (Vector3){ 0.0f, 0.0f, 1.0f }));

    // Mirrored about a pivot off the centre: the pivot's point stays, the rest swings across it.
    Shape.Pivot = (Vector2){ 0.25f, 0.5f };
    Shape.IsFlippedX = false;
    Shape.IsFlippedY = true;
    Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.25f, 0.5f), (Vector3){ 10.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.0f, 0.0f), (Vector3){ 9.5f, 2.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Corner(Result, 1.0f, 1.0f), (Vector3){ 11.5f, -2.0f, 0.0f }));
    Shape.IsFlippedX = true;
    Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(Corner(Result, 0.0f, 0.0f), (Vector3){ 10.5f, 2.0f, 0.0f }));
    TEST_CHECK(run, Handedness(Result) > 0.0f);

    // A pivot outside the image puts the image beside the object.
    Shape = MakeShape(WorldSpriteOrientation_Fixed, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector2){ 2.0f, 2.0f },
        (Vector2){ 1.5f, 0.5f });
    Result = SpriteTransform_GetMatrix(&Shape, &Viewer);
    TEST_CHECK(run, IsNearVector(Corner(Result, 1.0f, 0.5f), (Vector3){ -1.0f, 0.0f, 0.0f }));
}

/* In a shadow pass the light is the viewer: a billboard faces a directional light along its rays and a point light at
 * its position, and an axis billboard turns around its axis toward the light. The camera plays no part, so the shadow
 * does not turn when the camera moves. */
static void LightsTurnBillboardsTowardThemselves(TestRun* run)
{
    Vector3 TowardSun = { 0.6f, 0.8f, 0.0f };
    SpriteViewer Sun = SpriteViewer_FromDirection(TowardSun, (Vector3){ 0.0f, 1.0f, 0.0f });
    SpriteShape Near = MakeShape(WorldSpriteOrientation_Billboard, (Vector3){ 0.0f, 0.0f, 0.0f },
        (Vector2){ 1.0f, 1.0f }, (Vector2){ 0.5f, 0.0f });
    SpriteShape Far = Near;
    Far.Position = (Vector3){ 40.0f, 3.0f, -25.0f };
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(SpriteTransform_GetMatrix(&Near, &Sun)), TowardSun));
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(SpriteTransform_GetMatrix(&Far, &Sun)), TowardSun));

    SpriteShape Tree = Near;
    Tree.Orientation = WorldSpriteOrientation_AxisBillboard;
    Matrix TreeFromSun = SpriteTransform_GetMatrix(&Tree, &Sun);
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(TreeFromSun), (Vector3){ 1.0f, 0.0f, 0.0f }));
    TEST_CHECK(run, IsNearVector(Vector3Normalize(HeightAxis(TreeFromSun)), (Vector3){ 0.0f, 1.0f, 0.0f }));

    Vector3 Lamp = { 0.0f, 5.0f, 0.0f };
    SpriteViewer LampViewer = SpriteViewer_FromPoint(Lamp, (Vector3){ 0.0f, 1.0f, 0.0f });
    SpriteShape Sign = Near;
    Sign.Position = (Vector3){ 3.0f, 0.0f, 4.0f };
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetNormal(SpriteTransform_GetMatrix(&Sign, &LampViewer)),
        Vector3Normalize((Vector3){ -3.0f, 5.0f, -4.0f })));
}

/* The world size is the frame's pixels over the pixels per unit times the object's X and Y scale; the bounding radius
 * reaches the image's farthest corner from the pivot; the centre is the image's middle. */
static void SizeBoundsAndCentre(TestRun* run)
{
    Rectangle Frame = { 16.0f, 8.0f, 64.0f, 32.0f };
    Vector2 Size = SpriteTransform_GetWorldSize(Frame, 32.0f, (Vector3){ 1.0f, 1.0f, 1.0f });
    TEST_CHECK(run, IsNear(Size.x, 2.0f) && IsNear(Size.y, 1.0f));
    Size = SpriteTransform_GetWorldSize(Frame, 32.0f, (Vector3){ 2.0f, 0.5f, 9.0f });
    TEST_CHECK(run, IsNear(Size.x, 4.0f) && IsNear(Size.y, 0.5f));
    Size = SpriteTransform_GetWorldSize(Frame, 16.0f, (Vector3){ -1.0f, 1.0f, 1.0f });
    TEST_CHECK(run, IsNear(Size.x, -4.0f) && IsNear(Size.y, 2.0f));
    const float Unusable[4] = { 0.0f, -32.0f, NAN, INFINITY };
    for (size_t Index = 0; Index < 4; Index++)
    {
        Size = SpriteTransform_GetWorldSize(Frame, Unusable[Index], (Vector3){ 1.0f, 1.0f, 1.0f });
        TEST_CHECK(run, (Size.x == 0.0f) && (Size.y == 0.0f));
    }

    TEST_CHECK(run, IsNear(SpriteTransform_GetBoundingRadius((Vector2){ 2.0f, 4.0f }, (Vector2){ 0.5f, 0.0f }),
        sqrtf(17.0f)));
    TEST_CHECK(run, IsNear(SpriteTransform_GetBoundingRadius((Vector2){ -2.0f, 4.0f }, (Vector2){ 1.5f, 0.5f }),
        sqrtf(13.0f)));
    TEST_CHECK(run, IsNear(SpriteTransform_GetBoundingRadius((Vector2){ 2.0f, 2.0f }, (Vector2){ 0.5f, 0.5f }),
        sqrtf(2.0f)));

    SpriteShape Shape = MakeShape(WorldSpriteOrientation_Fixed, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector2){ 2.0f, 4.0f },
        (Vector2){ 0.5f, 0.0f });
    SpriteViewer Viewer = SpriteViewer_FromPoint((Vector3){ 0.0f, 0.0f, 10.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    TEST_CHECK(run, IsNearVector(SpriteTransform_GetCenter(SpriteTransform_GetMatrix(&Shape, &Viewer)),
        (Vector3){ 0.0f, 2.0f, 0.0f }));
}

/* A transparent list is drawn back to front: the farthest first, equal depths in index order. */
static void BlendedListSortsBackToFront(TestRun* run)
{
    GenericBuffer Order;
    GenericBuffer_AllocateVariable(&Order, 8, sizeof(SpriteDrawOrder));
    const float Depths[6] = { 5.0f, 20.0f, 5.0f, 1.0f, 20.0f, -3.0f };
    for (size_t Index = 0; Index < 6; Index++)
    {
        SpriteDrawOrder Entry = { .Index = Index, .Depth = Depths[Index] };
        TEST_CHECK(run, GenericBuffer_AddLast(&Order, &Entry));
    }
    if (TEST_CHECK(run, SpriteTransform_SortBackToFront(&Order)))
    {
        const size_t Expected[6] = { 1, 4, 0, 2, 3, 5 };
        const SpriteDrawOrder* Entries = (const SpriteDrawOrder*)Order._data;
        for (size_t Index = 0; Index < 6; Index++)
        {
            TEST_CHECK(run, Entries[Index].Index == Expected[Index]);
        }
    }

    // Short lists are sorted as they are; a buffer of anything else is refused.
    GenericBuffer_Clear(&Order);
    TEST_CHECK(run, SpriteTransform_SortBackToFront(&Order));
    GenericBuffer Wrong;
    GenericBuffer_AllocateVariable(&Wrong, 2, sizeof(float));
    TEST_CHECK(run, !SpriteTransform_SortBackToFront(&Wrong));
    Memory_Free(Wrong._data);
    Memory_Free(Order._data);
}


// Public functions.
void SpriteTransformTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "billboard_faces_the_camera", BillboardFacesTheCamera },
        { "axis_billboard_turns_around_its_axis", AxisBillboardTurnsAroundItsAxis },
        { "fixed_uses_the_objects_rotation", FixedUsesTheObjectsRotation },
        { "pivot_and_flips_move_and_mirror_the_corners", PivotAndFlipsMoveAndMirrorTheCorners },
        { "lights_turn_billboards_toward_themselves", LightsTurnBillboardsTowardThemselves },
        { "size_bounds_and_centre", SizeBoundsAndCentre },
        { "blended_list_sorts_back_to_front", BlendedListSortsBackToFront },
    };
    TestRun_RunSuite(run, "SpriteTransform", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
