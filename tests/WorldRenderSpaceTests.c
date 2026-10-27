#include "TestSuites.h"
#include "GameCamera.h"
#include "GameMath.h"
#include "WorldRenderSpace.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRVectorMath.h"
#include <math.h>
#include <string.h>


/**
 * Render space (T32, source/WorldRenderSpace.h): the renderer's view with the camera at the origin, the sky shader's ray
 * and the shadow cascades snapped to their texels. The float pipeline the GPU runs (raylib's DrawMesh matrix products,
 * the shaders' matrix-vector products) is replayed in C and measured against a double-precision reference camera, 50 km
 * out and near the origin; each test also replays the world-space maths the renderer had before, to show it fails the
 * same check far out (so the check can see the problem).
 */


// Macros.
/** Pi in double (raymath's PI is a float). */
#define DOUBLE_PI 3.14159265358979323846
/** The scene the probe ran at: 640 x 360 scene pixels, a 60-degree vertical field of view. */
#define SCENE_WIDTH 640.0
#define SCENE_HEIGHT 360.0
#define SCENE_FOV_Y 60.0f
/** How close to the reference the render-space pipeline must put a point or a ray, in scene pixels. */
#define PIXEL_TOLERANCE 0.01
/** How far off the old world-space pipeline must be at 50 km, at least, for the checks to show they see its error. */
#define OLD_POINT_ERROR_MINIMUM 0.1
#define OLD_RAY_ERROR_MINIMUM 1.0
/** How close the render-space pipeline must match the old one near the origin (float rounding only), in pixels. */
#define ORIGIN_PIXEL_TOLERANCE 1e-3
/** Fifty kilometres, and ten thousand, in metres. */
#define FAR_DISTANCE 50000.0
#define VERY_FAR_DISTANCE 1e7
/** The shadow cascade the snapping tests use: the renderer's near one (32 m, 2048 texels: 1.6 cm a texel). */
#define CASCADE_SIZE 32.0f
#define CASCADE_LIGHT_DISTANCE 120.0f
#define CASCADE_NEAR 1.0f
#define CASCADE_FAR 240.0f
#define CASCADE_MAP_SIZE 2048
/** How far the camera moves each frame in the snapping tests (1 mm), and how many frames (24 cm in all). */
#define CASCADE_STEP 0.001
#define CASCADE_STEP_COUNT 240
/** How far from a whole number a texel coordinate's change may be, in texels. */
#define TEXEL_TOLERANCE 1e-3
/** The turning-light test: how many frames, and how far the light turns each (a 24-minute day's sun at 60 frames a
 *  second), and how close the anchored grid's motion must stay to a centred box's, in texels a frame. */
#define TURN_FRAME_COUNT 600
#define TURN_PER_FRAME ((2.0 * DOUBLE_PI / 1440.0) / 60.0)
#define TURN_TOLERANCE 2e-3
/** The grid-step test: how far the camera moves each frame (3 mm, so the centre steps every few frames) and how many
 *  frames, before its 28 km jump. */
#define GRID_STEP 0.003
#define GRID_FRAME_COUNT 30
/** The reversed projection's round trip (T39): how close a point must come back through the inverse, as a share of its
 *  distance, and how close the closed-form inverse must match MatrixInvert's, as a share of each element. */
#define REVERSED_ROUND_TRIP_TOLERANCE 1e-6
#define REVERSED_INVERSE_TOLERANCE 1e-6


// Types.
/* The camera as a double-precision reference: its position and MatrixLookAt's axes (right, up, back) computed in double
 * from the same angles, and the projection's focal length and aspect. */
typedef struct ReferenceViewStruct
{
    Vector3Double Position;
    Vector3Double Right;
    Vector3Double Up;
    Vector3Double Back;
    double Focal;
    double Aspect;
} ReferenceView;

/* A pixel position on the scene target, x from the left, y from the bottom. */
typedef struct PixelStruct
{
    double X;
    double Y;
} Pixel;


// Static functions: the reference.
static GameCamera MakeCamera(Vector3Double position)
{
    GameCamera Viewer = GameCamera_Create(position, SCENE_FOV_Y);
    Viewer.Yaw = 0.6f;
    Viewer.Pitch = -0.15f;
    return Viewer;
}

static ReferenceView MakeReference(const GameCamera* camera)
{
    double Yaw = (double)camera->Yaw;
    double Pitch = (double)camera->Pitch;
    Vector3Double Forward = Vector3Double_Create(sin(Yaw) * cos(Pitch), sin(Pitch), cos(Yaw) * cos(Pitch));
    Vector3Double Back = Vector3Double_Negate(Vector3Double_NormalizeOr(Forward, Vector3Double_Create(0.0, 0.0, 1.0)));
    Vector3Double Right = Vector3Double_NormalizeOr(Vector3Double_Cross(Vector3Double_Create(0.0, 1.0, 0.0), Back),
        Vector3Double_Create(1.0, 0.0, 0.0));
    return (ReferenceView)
    {
        .Position = camera->Position,
        .Right = Right,
        .Up = Vector3Double_Cross(Back, Right),
        .Back = Back,
        .Focal = 1.0 / tan((double)camera->FovY * 0.5 * (DOUBLE_PI / 180.0)),
        .Aspect = SCENE_WIDTH / SCENE_HEIGHT
    };
}

/* Where the exact camera sees a world point on the target. */
static Pixel ReferencePixel(const ReferenceView* view, Vector3Double point)
{
    Vector3Double Offset = Vector3Double_Subtract(point, view->Position);
    double Depth = -Vector3Double_Dot(Offset, view->Back);
    double X = (view->Focal / view->Aspect) * Vector3Double_Dot(Offset, view->Right) / Depth;
    double Y = view->Focal * Vector3Double_Dot(Offset, view->Up) / Depth;
    return (Pixel){ .X = (X + 1.0) * 0.5 * SCENE_WIDTH, .Y = (Y + 1.0) * 0.5 * SCENE_HEIGHT };
}

/* The exact camera's ray through a pixel's centre. */
static Vector3Double ReferenceRay(const ReferenceView* view, double pixelX, double pixelY)
{
    double X = (((pixelX + 0.5) / SCENE_WIDTH) * 2.0 - 1.0) * view->Aspect / view->Focal;
    double Y = (((pixelY + 0.5) / SCENE_HEIGHT) * 2.0 - 1.0) / view->Focal;
    Vector3Double Direction = Vector3Double_Add(Vector3Double_Add(Vector3Double_Scale(view->Right, X),
        Vector3Double_Scale(view->Up, Y)), Vector3Double_Negate(view->Back));
    return Vector3Double_NormalizeOr(Direction, view->Back);
}

/* The angle between two unit rays, in scene pixels (a pixel spans the field of view over the height). */
static double RayErrorInPixels(Vector3Double a, Vector3Double b)
{
    double Angle = atan2(Vector3Double_Length(Vector3Double_Cross(a, b)), Vector3Double_Dot(a, b));
    return Angle / (((double)SCENE_FOV_Y * (DOUBLE_PI / 180.0)) / SCENE_HEIGHT);
}

static double PixelDistance(Pixel a, Pixel b)
{
    return hypot(a.X - b.X, a.Y - b.Y);
}


// Static functions: the float pipeline the GPU runs.
/* A model drawn as DrawMesh draws it: mvp = model x view x projection (raymath's order, in float), then the vertex
 * stage's mvp * vertex, the perspective divide and the viewport. */
static Pixel DrawnPixel(Matrix model, Matrix view, Matrix projection, Vector3 vertex)
{
    Matrix Mvp = MatrixMultiply(MatrixMultiply(model, view), projection);
    float X = (Mvp.m0 * vertex.x) + (Mvp.m4 * vertex.y) + (Mvp.m8 * vertex.z) + Mvp.m12;
    float Y = (Mvp.m1 * vertex.x) + (Mvp.m5 * vertex.y) + (Mvp.m9 * vertex.z) + Mvp.m13;
    float W = (Mvp.m3 * vertex.x) + (Mvp.m7 * vertex.y) + (Mvp.m11 * vertex.z) + Mvp.m15;
    return (Pixel){ .X = ((double)(X / W) + 1.0) * 0.5 * SCENE_WIDTH, .Y = ((double)(Y / W) + 1.0) * 0.5 * SCENE_HEIGHT };
}

/* The view the renderer had before render space: raylib's camera at the camera's float position. */
static Matrix WorldSpaceView(const GameCamera* camera)
{
    Vector3 Position = GameMath_ToVector3(camera->Position);
    Vector3 Forward = GameCamera_GetForward(camera);
    return MatrixLookAt(Position, Vector3Add(Position, Forward), GameCamera_GetUp(camera));
}

/* The old sky shader's ray: the far plane's point through the world-space inverse view-projection, minus the camera's
 * float position. */
static Vector3Double WorldSpaceSkyRay(const GameCamera* camera, Matrix projection, double pixelX, double pixelY)
{
    Matrix Inverse = MatrixInvert(MatrixMultiply(WorldSpaceView(camera), projection));
    float ClipX = (float)(((pixelX + 0.5) / SCENE_WIDTH) * 2.0 - 1.0);
    float ClipY = (float)(((pixelY + 0.5) / SCENE_HEIGHT) * 2.0 - 1.0);
    float X = (Inverse.m0 * ClipX) + (Inverse.m4 * ClipY) + Inverse.m8 + Inverse.m12;
    float Y = (Inverse.m1 * ClipX) + (Inverse.m5 * ClipY) + Inverse.m9 + Inverse.m13;
    float Z = (Inverse.m2 * ClipX) + (Inverse.m6 * ClipY) + Inverse.m10 + Inverse.m14;
    float W = (Inverse.m3 * ClipX) + (Inverse.m7 * ClipY) + Inverse.m11 + Inverse.m15;
    Vector3 Eye = GameMath_ToVector3(camera->Position);
    Vector3 Direction = Vector3Normalize(Vector3Subtract((Vector3){ X / W, Y / W, Z / W }, Eye));
    return GameMath_FromVector3(Direction);
}

/* A placed object for the probe: a turned world matrix in double at @p position. */
static Matrix4Double MakeWorldMatrix(Vector3Double position)
{
    Matrix4Double World = Matrix4Double_FromRotationXYZ(Vector3Double_Create(0.3, 1.1, -0.2));
    Matrix4Double_SetTranslation(&World, position);
    return World;
}

/* The probe's points: 0.6 m, 3 m and 30 m in front of the camera, a little off its axis. */
static Vector3Double ProbePoint(const ReferenceView* view, size_t index)
{
    static const double Offsets[3][3] = { { 0.6, 0.1, 0.05 }, { 3.0, -0.7, 0.4 }, { 30.0, 8.0, -4.0 } };
    Vector3Double Point = Vector3Double_AddScaled(view->Position, Vector3Double_Negate(view->Back), Offsets[index][0]);
    Point = Vector3Double_AddScaled(Point, view->Right, Offsets[index][1]);
    return Vector3Double_AddScaled(Point, view->Up, Offsets[index][2]);
}

static WorldRenderView BuildView(const GameCamera* camera)
{
    return WorldRenderSpace_BuildView(camera, SCENE_WIDTH / SCENE_HEIGHT, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR,
        false);
}

/* The camera's view with reversed depth (T39), as the renderer builds it where the GPU has clip control. */
static WorldRenderView BuildReversedView(const GameCamera* camera)
{
    return WorldRenderSpace_BuildView(camera, SCENE_WIDTH / SCENE_HEIGHT, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR,
        true);
}

/* A matrix times (x, y, z, w), in float as a shader's product. */
static Vector4 TransformVector4(Matrix m, Vector4 v)
{
    return (Vector4)
    {
        (m.m0 * v.x) + (m.m4 * v.y) + (m.m8 * v.z) + (m.m12 * v.w),
        (m.m1 * v.x) + (m.m5 * v.y) + (m.m9 * v.z) + (m.m13 * v.w),
        (m.m2 * v.x) + (m.m6 * v.y) + (m.m10 * v.z) + (m.m14 * v.w),
        (m.m3 * v.x) + (m.m7 * v.y) + (m.m11 * v.z) + (m.m15 * v.w)
    };
}

/* A view-space point's depth through a projection as the GPU stores it with clip control's 0-to-1 depth: clip z over
 * clip w, in float. */
static float ClipDepth(Matrix projection, Vector3 viewPoint)
{
    Vector4 Clip = TransformVector4(projection, (Vector4){ viewPoint.x, viewPoint.y, viewPoint.z, 1.0f });
    return Clip.z / Clip.w;
}


// Static functions: the cascades.
static WorldRenderCascadeShape MakeCascadeShape(void)
{
    return (WorldRenderCascadeShape)
    {
        .Size = CASCADE_SIZE,
        .LightDistance = CASCADE_LIGHT_DISTANCE,
        .NearPlane = CASCADE_NEAR,
        .FarPlane = CASCADE_FAR,
        .MapSize = CASCADE_MAP_SIZE
    };
}

/* A render-space point's coordinates on the shadow map, in texels, as the lit shaders find them: lightVP * position,
 * the divide, then [-1, 1] to [0, MapSize]. */
static Pixel TexelOf(Matrix viewProjection, Vector3 position)
{
    const Matrix* M = &viewProjection;
    float X = (M->m0 * position.x) + (M->m4 * position.y) + (M->m8 * position.z) + M->m12;
    float Y = (M->m1 * position.x) + (M->m5 * position.y) + (M->m9 * position.z) + M->m13;
    float W = (M->m3 * position.x) + (M->m7 * position.y) + (M->m11 * position.z) + M->m15;
    return (Pixel)
    {
        .X = ((double)(X / W) * 0.5 + 0.5) * (double)CASCADE_MAP_SIZE,
        .Y = ((double)(Y / W) * 0.5 + 0.5) * (double)CASCADE_MAP_SIZE
    };
}

/* The cascade the renderer had before the snapping: the same box centred on the camera (with the same up as the
 * snapped one: +Z for a light near straight up). */
static Matrix UnsnappedViewProjection(Vector3 towardLight)
{
    Vector3 Up = (fabsf(towardLight.y) > 0.99f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
    Matrix View = MatrixLookAt(Vector3Scale(towardLight, CASCADE_LIGHT_DISTANCE), (Vector3){ 0.0f, 0.0f, 0.0f }, Up);
    double Half = (double)CASCADE_SIZE * 0.5;
    return MatrixMultiply(View, MatrixOrtho(-Half, Half, -Half, Half, CASCADE_NEAR, CASCADE_FAR));
}

/* How far a value is from the nearest whole number. */
static double FromWhole(double value)
{
    return fabs(value - round(value));
}

/* How far a texel coordinate moved between two frames as an edge in the world sees it: the map's contents moving by
 * whole texels (a snapped centre stepping) move no edge. */
static double EdgeMotion(Pixel previous, Pixel current)
{
    return hypot(FromWhole(current.X - previous.X), FromWhole(current.Y - previous.Y));
}

/* The turning-light test's light: 40 degrees up, its azimuth turning a fixed angle a frame. */
static Vector3 TurningLight(int frame)
{
    double Elevation = 0.7;
    double Azimuth = 0.4 + (TURN_PER_FRAME * (double)frame);
    return GameMath_ToVector3(Vector3Double_Create(cos(Elevation) * sin(Azimuth), sin(Elevation),
        cos(Elevation) * cos(Azimuth)));
}


// Tests.
/* The render camera sits at the origin and only turns: the view is a pure rotation, the same bit for bit wherever the
 * camera is, and it is the matrix BeginMode3D loads for the render camera; the origin is the camera's position. */
static void TheViewIsARotationWhereverTheCameraIs(TestRun* run)
{
    GameCamera Near = MakeCamera(Vector3Double_Create(1.25, 1.7, -3.5));
    GameCamera Far = MakeCamera(Vector3Double_Create(FAR_DISTANCE * 0.6, 1.7, -FAR_DISTANCE * 0.8));
    WorldRenderView NearView = BuildView(&Near);
    WorldRenderView FarView = BuildView(&Far);
    TEST_CHECK(run, (FarView.Origin.X == Far.Position.X) && (FarView.Origin.Y == Far.Position.Y)
        && (FarView.Origin.Z == Far.Position.Z));
    TEST_CHECK(run, (FarView.Camera.position.x == 0.0f) && (FarView.Camera.position.y == 0.0f)
        && (FarView.Camera.position.z == 0.0f));
    TEST_CHECK(run, (FarView.View.m12 == 0.0f) && (FarView.View.m13 == 0.0f) && (FarView.View.m14 == 0.0f));
    Matrix Loaded = MatrixLookAt(FarView.Camera.position, FarView.Camera.target, FarView.Camera.up);
    TEST_CHECK(run, memcmp(&Loaded, &FarView.View, sizeof(Matrix)) == 0);
    TEST_CHECK(run, memcmp(&NearView.View, &FarView.View, sizeof(Matrix)) == 0);
    TEST_CHECK(run, memcmp(&NearView.ViewProjection, &FarView.ViewProjection, sizeof(Matrix)) == 0);
    TEST_CHECK(run, memcmp(&NearView.InvViewProjection, &FarView.InvViewProjection, sizeof(Matrix)) == 0);
}

/* The probe as a test: 50 km from the origin, points 0.6 m, 3 m and 30 m from the camera land within a hundredth of a
 * scene pixel of where the exact camera sees them, through the draw path (the world matrix relative to the origin, in
 * double, then raylib's float products). The world-space floats the renderer used before miss by more. */
static void PointsLandWithinAHundredthOfAPixelFiftyKilometresOut(TestRun* run)
{
    GameCamera Viewer = MakeCamera(Vector3Double_Create(FAR_DISTANCE * 0.6, 1.7, -FAR_DISTANCE * 0.8));
    WorldRenderView View = BuildView(&Viewer);
    ReferenceView Reference = MakeReference(&Viewer);
    Vector3 Vertex = { 0.05f, -0.02f, 0.03f };
    double WorstOld = 0.0;
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Matrix4Double World = MakeWorldMatrix(ProbePoint(&Reference, Index));
        Pixel Expected = ReferencePixel(&Reference, Matrix4Double_TransformPoint(&World, GameMath_FromVector3(Vertex)));
        Pixel Drawn = DrawnPixel(GameMath_ToMatrixRelative(&World, View.Origin), View.View, View.Projection, Vertex);
        TEST_CHECK(run, PixelDistance(Drawn, Expected) < PIXEL_TOLERANCE);
        Pixel Old = DrawnPixel(GameMath_ToMatrix(&World), WorldSpaceView(&Viewer), View.Projection, Vertex);
        WorstOld = fmax(WorstOld, PixelDistance(Old, Expected));
    }
    TEST_CHECK(run, WorstOld > OLD_POINT_ERROR_MINIMUM);
}

/* Near the origin render space changes nothing but rounding: the same points land where the world-space pipeline put
 * them, within a thousandth of a pixel. */
static void NearTheOriginPointsLandWhereTheyDid(TestRun* run)
{
    GameCamera Viewer = MakeCamera(Vector3Double_Create(1.25, 1.7, -3.5));
    WorldRenderView View = BuildView(&Viewer);
    ReferenceView Reference = MakeReference(&Viewer);
    Vector3 Vertex = { 0.05f, -0.02f, 0.03f };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Matrix4Double World = MakeWorldMatrix(ProbePoint(&Reference, Index));
        Pixel Drawn = DrawnPixel(GameMath_ToMatrixRelative(&World, View.Origin), View.View, View.Projection, Vertex);
        Pixel Old = DrawnPixel(GameMath_ToMatrix(&World), WorldSpaceView(&Viewer), View.Projection, Vertex);
        TEST_CHECK(run, PixelDistance(Drawn, Old) < ORIGIN_PIXEL_TOLERANCE);
    }
}

/* The sky's ray (the C mirror of the shader's) at every corner pixel is the exact camera's ray within a hundredth of a
 * pixel, at the origin, 1 km and 50 km out: it comes from the rotation alone. The old world-space ray misses by more
 * than a pixel 50 km out. */
static void SkyRaysAreExactAtAnyDistance(TestRun* run)
{
    const double Distances[] = { 0.0, 1000.0, FAR_DISTANCE };
    const double Corners[4][2] =
    {
        { 0.0, 0.0 }, { SCENE_WIDTH - 1.0, 0.0 }, { 0.0, SCENE_HEIGHT - 1.0 }, { SCENE_WIDTH - 1.0, SCENE_HEIGHT - 1.0 }
    };
    for (size_t Distance = 0U; Distance < (sizeof(Distances) / sizeof(Distances[0])); Distance++)
    {
        double Far = Distances[Distance];
        GameCamera Viewer = MakeCamera(Vector3Double_Create(Far * 0.6, 1.7, -Far * 0.8));
        WorldRenderView View = BuildView(&Viewer);
        ReferenceView Reference = MakeReference(&Viewer);
        double WorstOld = 0.0;
        for (size_t Corner = 0U; Corner < 4U; Corner++)
        {
            double X = Corners[Corner][0];
            double Y = Corners[Corner][1];
            Vector3 Mirror = WorldRenderSpace_GetSkyRay(&View.InvViewProjection, (float)((X + 0.5) / SCENE_WIDTH),
                (float)((Y + 0.5) / SCENE_HEIGHT));
            Vector3Double Expected = ReferenceRay(&Reference, X, Y);
            TEST_CHECK(run, RayErrorInPixels(GameMath_FromVector3(Mirror), Expected) < PIXEL_TOLERANCE);
            WorstOld = fmax(WorstOld, RayErrorInPixels(WorldSpaceSkyRay(&Viewer, View.Projection, X, Y), Expected));
        }
        if (Distances[Distance] == FAR_DISTANCE)
        {
            TEST_CHECK(run, WorstOld > OLD_RAY_ERROR_MINIMUM);
        }
    }
}

/* The reversed infinite projection (T39): depth is 1 at the near plane, a half at twice its distance, and falls as 1
 * over the distance toward 0, which no finite distance reaches (10 000 km: 5e-9, a normal float); points 1 m apart at
 * 20 km and 1 km apart at 10 000 km keep distinct float depths, nearer greater; x and y are MatrixPerspective's, so a
 * point lands within a thousandth of a pixel of where the standard projection puts it. */
static void ReversedDepthFallsFromTheNearPlaneToInfinity(TestRun* run)
{
    double FovY = (double)SCENE_FOV_Y * (DOUBLE_PI / 180.0);
    double Aspect = SCENE_WIDTH / SCENE_HEIGHT;
    Matrix Reversed = WorldRenderSpace_GetReversedProjection(FovY, Aspect, RL_CULL_DISTANCE_NEAR, NULL);
    Matrix Standard = MatrixPerspective(FovY, Aspect, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    float Near = (float)RL_CULL_DISTANCE_NEAR;
    TEST_CHECK(run, ClipDepth(Reversed, (Vector3){ 0.0f, 0.0f, -Near }) == 1.0f);
    TEST_CHECK(run, fabsf(ClipDepth(Reversed, (Vector3){ 0.0f, 0.0f, -2.0f * Near }) - 0.5f) < 1e-7f);
    float Farthest = ClipDepth(Reversed, (Vector3){ 0.0f, 0.0f, -1.0e7f });
    TEST_CHECK(run, (Farthest > 0.0f) && (fabsf(Farthest - 5.0e-9f) < 1e-14f) && isnormal(Farthest));
    TEST_CHECK(run, ClipDepth(Reversed, (Vector3){ 600.0f, -200.0f, -20000.0f })
        > ClipDepth(Reversed, (Vector3){ 600.0f, -200.0f, -20001.0f }));
    TEST_CHECK(run, ClipDepth(Reversed, (Vector3){ 0.0f, 0.0f, -1.0e7f })
        > ClipDepth(Reversed, (Vector3){ 0.0f, 0.0f, -1.0e7f - 1000.0f }));
    const Vector3 Points[] = { { 0.02f, 0.01f, -0.06f }, { 1.3f, -0.4f, -3.0f }, { 500.0f, 200.0f, -2000.0f },
        { -9000.0f, 3000.0f, -30000.0f } };
    for (size_t Index = 0U; Index < (sizeof(Points) / sizeof(Points[0])); Index++)
    {
        Pixel FromReversed = DrawnPixel(MatrixIdentity(), MatrixIdentity(), Reversed, Points[Index]);
        Pixel FromStandard = DrawnPixel(MatrixIdentity(), MatrixIdentity(), Standard, Points[Index]);
        TEST_CHECK(run, PixelDistance(FromReversed, FromStandard) < ORIGIN_PIXEL_TOLERANCE);
    }
}

/* The reversed projection's closed-form inverse takes a pixel's stored depth back to the point it came from, as the AO
 * and the outline passes reconstruct positions (common/depth's depthToNdc), within a millionth of the distance from the
 * near plane to 10 000 km, and matches MatrixInvert's. */
static void ReversedDepthComesBackThroughItsInverse(TestRun* run)
{
    double FovY = (double)SCENE_FOV_Y * (DOUBLE_PI / 180.0);
    Matrix Inverse;
    Matrix Reversed = WorldRenderSpace_GetReversedProjection(FovY, SCENE_WIDTH / SCENE_HEIGHT, RL_CULL_DISTANCE_NEAR,
        &Inverse);
    const float Distances[] = { 0.05f, 1.0f, 20000.0f, 1.0e7f };
    for (size_t Index = 0U; Index < (sizeof(Distances) / sizeof(Distances[0])); Index++)
    {
        float Distance = Distances[Index];
        Vector3 Point = { 0.31f * Distance, -0.12f * Distance, -Distance };
        Vector4 Clip = TransformVector4(Reversed, (Vector4){ Point.x, Point.y, Point.z, 1.0f });
        Vector4 Ndc = { Clip.x / Clip.w, Clip.y / Clip.w, Clip.z / Clip.w, 1.0f };
        Vector4 Back = TransformVector4(Inverse, Ndc);
        Vector3 Restored = { Back.x / Back.w, Back.y / Back.w, Back.z / Back.w };
        TEST_CHECK(run, Vector3Distance(Restored, Point) <= (float)(REVERSED_ROUND_TRIP_TOLERANCE * (double)Distance));
    }
    Matrix Numeric = MatrixInvert(Reversed);
    const float* A = &Inverse.m0;
    const float* B = &Numeric.m0;
    bool IsSame = true;
    for (size_t Index = 0U; Index < 16U; Index++)
    {
        IsSame = IsSame && (fabs((double)A[Index] - (double)B[Index])
            <= (REVERSED_INVERSE_TOLERANCE * fmax(1.0, fabs((double)B[Index]))));
    }
    TEST_CHECK(run, IsSame);
}

/* With reversed depth (T39) the view carries the reversed projection, its closed-form inverse and their products with the
 * rotation, and the sky's rays (the C mirror of the shader's, whose clip depth 1 is now the near plane's) are the exact
 * camera's within a hundredth of a pixel at every corner, at the origin and 50 km out. */
static void ReversedViewsKeepTheSkyRaysExact(TestRun* run)
{
    const double Distances[] = { 0.0, FAR_DISTANCE };
    const double Corners[4][2] =
    {
        { 0.0, 0.0 }, { SCENE_WIDTH - 1.0, 0.0 }, { 0.0, SCENE_HEIGHT - 1.0 }, { SCENE_WIDTH - 1.0, SCENE_HEIGHT - 1.0 }
    };
    for (size_t Distance = 0U; Distance < (sizeof(Distances) / sizeof(Distances[0])); Distance++)
    {
        double Far = Distances[Distance];
        GameCamera Viewer = MakeCamera(Vector3Double_Create(Far * 0.6, 1.7, -Far * 0.8));
        WorldRenderView View = BuildReversedView(&Viewer);
        Matrix Inverse;
        Matrix Projection = WorldRenderSpace_GetReversedProjection((double)View.Camera.fovy * DEG2RAD,
            SCENE_WIDTH / SCENE_HEIGHT, RL_CULL_DISTANCE_NEAR, &Inverse);
        Matrix ViewProjection = MatrixMultiply(View.View, Projection);
        TEST_CHECK(run, (memcmp(&View.Projection, &Projection, sizeof(Matrix)) == 0)
            && (memcmp(&View.InvProjection, &Inverse, sizeof(Matrix)) == 0)
            && (memcmp(&View.ViewProjection, &ViewProjection, sizeof(Matrix)) == 0));
        ReferenceView Reference = MakeReference(&Viewer);
        for (size_t Corner = 0U; Corner < 4U; Corner++)
        {
            double X = Corners[Corner][0];
            double Y = Corners[Corner][1];
            Vector3 Mirror = WorldRenderSpace_GetSkyRay(&View.InvViewProjection, (float)((X + 0.5) / SCENE_WIDTH),
                (float)((Y + 0.5) / SCENE_HEIGHT));
            TEST_CHECK(run, RayErrorInPixels(GameMath_FromVector3(Mirror), ReferenceRay(&Reference, X, Y))
                < PIXEL_TOLERANCE);
        }
    }
}

/* The cascades' texel grid stands still in the world while the light does: as the camera moves 1 mm a frame, a fixed
 * point keeps its shadow-map texel coordinates until the box's centre steps, and then they change by whole texels; the
 * centre (the anchor each frame passes on) stays within half a texel of the camera along the light's right and up. Near
 * the origin and 10 000 km out, for a light at an angle and one near straight up (the light's view takes +Z as its up
 * there). The cascade the renderer had before (centred on the camera) moves the point by fractions of a texel every
 * frame. */
static void CascadeTexelsStandStillAsTheCameraMoves(TestRun* run)
{
    WorldRenderCascadeShape Shape = MakeCascadeShape();
    double Texel = (double)CASCADE_SIZE / (double)CASCADE_MAP_SIZE;
    const Vector3 Lights[2] =
    {
        Vector3Normalize((Vector3){ 0.3f, 0.8f, -0.4f }), Vector3Normalize((Vector3){ 0.05f, 0.998f, 0.03f })
    };
    Vector3Double Motion = Vector3Double_NormalizeOr(Vector3Double_Create(1.0, 0.2, 0.5), Vector3Double_Zero());
    const Vector3Double Starts[2] =
    {
        { .X = 1.25, .Y = 1.7, .Z = -3.5 }, { .X = VERY_FAR_DISTANCE * 0.6, .Y = 12.0, .Z = -VERY_FAR_DISTANCE * 0.8 }
    };
    for (size_t Light = 0U; Light < 2U; Light++)
    {
        Vector3 TowardLight = Lights[Light];
        Matrix Unsnapped = UnsnappedViewProjection(TowardLight);
        for (size_t Start = 0U; Start < 2U; Start++)
        {
            Vector3Double Point = Vector3Double_Add(Starts[Start], Vector3Double_Create(3.0, -1.5, 2.0));
            Vector3Double Anchor = Starts[Start];
            Pixel Previous = { 0 };
            Pixel PreviousUnsnapped = { 0 };
            Vector3Double PreviousCenter = Vector3Double_Zero();
            size_t StepCount = 0U;
            size_t StillCount = 0U;
            size_t CrawlCount = 0U;
            bool AreChangesWhole = true;
            bool IsStillWhenNotStepping = true;
            bool IsCenterNear = true;
            for (int Frame = 0; Frame <= CASCADE_STEP_COUNT; Frame++)
            {
                Vector3Double Origin = Vector3Double_AddScaled(Starts[Start], Motion, (double)Frame * CASCADE_STEP);
                WorldRenderCascade Cascade = WorldRenderSpace_GetCascade(Origin, Anchor, TowardLight, &Shape);
                Anchor = Cascade.Anchor;
                Vector3 Relative = GameMath_ToVector3Relative(Point, Origin);
                Pixel Current = TexelOf(Cascade.ViewProjection, Relative);
                Pixel CurrentUnsnapped = TexelOf(Unsnapped, Relative);

                // The light's right and up are the view's first two rows.
                Vector3 Right = { Cascade.View.m0, Cascade.View.m4, Cascade.View.m8 };
                Vector3 Up = { Cascade.View.m1, Cascade.View.m5, Cascade.View.m9 };
                IsCenterNear = IsCenterNear
                    && (fabs((double)Vector3DotProduct(Cascade.Center, Right)) <= (Texel * 0.5 + 1e-6))
                    && (fabs((double)Vector3DotProduct(Cascade.Center, Up)) <= (Texel * 0.5 + 1e-6));
                if (Frame > 0)
                {
                    double ChangeX = Current.X - Previous.X;
                    double ChangeY = Current.Y - Previous.Y;
                    bool HasStepped = Vector3Double_Distance(Anchor, PreviousCenter) > (Texel * 0.25);
                    if (HasStepped)
                    {
                        StepCount++;
                        AreChangesWhole = AreChangesWhole && (FromWhole(ChangeX) < TEXEL_TOLERANCE)
                            && (FromWhole(ChangeY) < TEXEL_TOLERANCE)
                            && ((round(ChangeX) != 0.0) || (round(ChangeY) != 0.0));
                    }
                    else
                    {
                        StillCount++;
                        IsStillWhenNotStepping = IsStillWhenNotStepping && (fabs(ChangeX) < TEXEL_TOLERANCE)
                            && (fabs(ChangeY) < TEXEL_TOLERANCE);
                    }
                    CrawlCount += (EdgeMotion(PreviousUnsnapped, CurrentUnsnapped) > 0.01) ? 1U : 0U;
                }
                Previous = Current;
                PreviousUnsnapped = CurrentUnsnapped;
                PreviousCenter = Anchor;
            }
            TEST_CHECK(run, AreChangesWhole && IsStillWhenNotStepping && IsCenterNear);
            TEST_CHECK(run, (StepCount > 0U) && (StillCount > StepCount));
            TEST_CHECK(run, CrawlCount > (size_t)(CASCADE_STEP_COUNT / 2));
        }
    }
}

/* The cascades' texel grid turns about the camera while the light turns: with the camera still and the light turning at
 * a 24-minute day's pace, a point 4 m from the camera moves on the shadow map exactly as under a box centred on the
 * camera, near the origin, 28 km out and 10 000 km out, when each frame passes its anchor on. A grid anchored at the
 * world's origin instead turns about that origin and moves the point by the camera's distance from it times the angle:
 * edges far out jump by a fraction of a texel every frame. */
static void CascadeGridTurnsAboutTheCamera(TestRun* run)
{
    WorldRenderCascadeShape Shape = MakeCascadeShape();
    const Vector3Double Cameras[3] =
    {
        { .X = 1.25, .Y = 1.7, .Z = -3.5 }, { .X = 20000.0, .Y = 2.5, .Z = 19993.0 },
        { .X = VERY_FAR_DISTANCE * 0.6, .Y = 12.0, .Z = -VERY_FAR_DISTANCE * 0.8 }
    };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Vector3Double Still = Cameras[Index];
        Vector3 Relative = GameMath_ToVector3Relative(Vector3Double_Add(Still, Vector3Double_Create(2.0, -2.0, 3.0)),
            Still);
        Vector3Double Anchor = Still;
        Pixel Previous[3];
        Memory_Zero(Previous, sizeof(Previous));
        double Anchored = 0.0;
        double WorldAnchored = 0.0;
        double Centred = 0.0;
        for (int Frame = 0; Frame <= TURN_FRAME_COUNT; Frame++)
        {
            Vector3 TowardLight = TurningLight(Frame);
            WorldRenderCascade Cascade = WorldRenderSpace_GetCascade(Still, Anchor, TowardLight, &Shape);
            Anchor = Cascade.Anchor;
            WorldRenderCascade AtTheWorldsOrigin = WorldRenderSpace_GetCascade(Still, Vector3Double_Zero(), TowardLight,
                &Shape);
            Pixel Current[3] =
            {
                TexelOf(Cascade.ViewProjection, Relative), TexelOf(AtTheWorldsOrigin.ViewProjection, Relative),
                TexelOf(UnsnappedViewProjection(TowardLight), Relative)
            };
            if (Frame > 0)
            {
                Anchored += EdgeMotion(Previous[0], Current[0]);
                WorldAnchored += EdgeMotion(Previous[1], Current[1]);
                Centred += hypot(Current[2].X - Previous[2].X, Current[2].Y - Previous[2].Y);
            }
            Memory_Copy(Current, Previous, sizeof(Previous));
        }
        Anchored /= (double)TURN_FRAME_COUNT;
        WorldAnchored /= (double)TURN_FRAME_COUNT;
        Centred /= (double)TURN_FRAME_COUNT;
        TEST_CHECK(run, (Centred > 0.005) && (fabs(Anchored - Centred) < TURN_TOLERANCE));
        if (Index > 0U)
        {
            TEST_CHECK(run, WorldAnchored > (10.0 * Centred));
        }
    }
}

/* The renderer's step (WorldRenderSpace_StepCascade): a zeroed grid starts at its first frame's origin; then each frame
 * is WorldRenderSpace_GetCascade from the centre the grid kept, bit for bit, while the camera moves 3 mm a frame (the
 * centre stepping now and then); after a 28 km jump (as F11 makes) the kept centre is within half a texel of the camera
 * along the light's right and up and at its depth along the light, so the grid turns about the camera from there. */
static void CascadeGridFollowsTheCamera(TestRun* run)
{
    WorldRenderCascadeShape Shape = MakeCascadeShape();
    double Texel = (double)CASCADE_SIZE / (double)CASCADE_MAP_SIZE;
    Vector3 TowardLight = Vector3Normalize((Vector3){ 0.3f, 0.8f, -0.4f });
    Vector3Double Start = Vector3Double_Create(1.25, 1.7, -3.5);
    WorldRenderCascadeGrid Grid;
    Memory_Zero(&Grid, sizeof(Grid));
    WorldRenderCascade First = WorldRenderSpace_StepCascade(&Grid, Start, TowardLight, &Shape);
    WorldRenderCascade FromStart = WorldRenderSpace_GetCascade(Start, Start, TowardLight, &Shape);
    TEST_CHECK(run, Grid.HasAnchor && (memcmp(&First.ViewProjection, &FromStart.ViewProjection, sizeof(Matrix)) == 0)
        && (Vector3Double_Distance(Grid.Anchor, Start) < 1e-9));

    Vector3Double Motion = Vector3Double_NormalizeOr(Vector3Double_Create(1.0, 0.2, 0.5), Vector3Double_Zero());
    Vector3Double Anchor = Grid.Anchor;
    bool IsChained = true;
    size_t StepCount = 0U;
    for (int Frame = 1; Frame <= GRID_FRAME_COUNT; Frame++)
    {
        Vector3Double Origin = Vector3Double_AddScaled(Start, Motion, (double)Frame * GRID_STEP);
        WorldRenderCascade Stepped = WorldRenderSpace_StepCascade(&Grid, Origin, TowardLight, &Shape);
        WorldRenderCascade Chained = WorldRenderSpace_GetCascade(Origin, Anchor, TowardLight, &Shape);
        IsChained = IsChained && (memcmp(&Stepped.ViewProjection, &Chained.ViewProjection, sizeof(Matrix)) == 0)
            && (memcmp(&Grid.Anchor, &Chained.Anchor, sizeof(Vector3Double)) == 0);
        StepCount += (Vector3Double_Distance(Chained.Anchor, Anchor) > (Texel * 0.25)) ? 1U : 0U;
        Anchor = Chained.Anchor;
    }
    TEST_CHECK(run, IsChained && (StepCount > 0U));

    Vector3Double Far = Vector3Double_Create(20000.0, 2.5, 19993.0);
    WorldRenderCascade Jumped = WorldRenderSpace_StepCascade(&Grid, Far, TowardLight, &Shape);
    Vector3Double FromCamera = Vector3Double_Subtract(Grid.Anchor, Far);
    // The light's right, up and toward-the-light axes are the view's three rows.
    Vector3Double Right = Vector3Double_Create(Jumped.View.m0, Jumped.View.m4, Jumped.View.m8);
    Vector3Double Up = Vector3Double_Create(Jumped.View.m1, Jumped.View.m5, Jumped.View.m9);
    Vector3Double Toward = Vector3Double_Create(Jumped.View.m2, Jumped.View.m6, Jumped.View.m10);
    TEST_CHECK(run, (fabs(Vector3Double_Dot(FromCamera, Right)) <= (Texel * 0.5 + 1e-6))
        && (fabs(Vector3Double_Dot(FromCamera, Up)) <= (Texel * 0.5 + 1e-6))
        && (fabs(Vector3Double_Dot(FromCamera, Toward)) < 1e-6));
}


// Public functions.
void WorldRenderSpaceTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_view_is_a_rotation_wherever_the_camera_is", TheViewIsARotationWhereverTheCameraIs },
        { "points_land_within_a_hundredth_of_a_pixel_fifty_kilometres_out",
            PointsLandWithinAHundredthOfAPixelFiftyKilometresOut },
        { "near_the_origin_points_land_where_they_did", NearTheOriginPointsLandWhereTheyDid },
        { "sky_rays_are_exact_at_any_distance", SkyRaysAreExactAtAnyDistance },
        { "reversed_depth_falls_from_the_near_plane_to_infinity", ReversedDepthFallsFromTheNearPlaneToInfinity },
        { "reversed_depth_comes_back_through_its_inverse", ReversedDepthComesBackThroughItsInverse },
        { "reversed_views_keep_the_sky_rays_exact", ReversedViewsKeepTheSkyRaysExact },
        { "cascade_texels_stand_still_as_the_camera_moves", CascadeTexelsStandStillAsTheCameraMoves },
        { "cascade_grid_turns_about_the_camera", CascadeGridTurnsAboutTheCamera },
        { "cascade_grid_follows_the_camera", CascadeGridFollowsTheCamera },
    };
    TestRun_RunSuite(run, "WorldRenderSpace", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
