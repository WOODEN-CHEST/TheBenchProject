#include "TestSuites.h"
#include "Config.h"
#include "GameCamera.h"
#include "WorldReflections.h"
#include "WorldRenderSpace.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stdlib.h>


// Macros.
/** The synthetic scenes' screen, the scene target's size at the game's pixelation. */
#define SCENE_WIDTH 640
#define SCENE_HEIGHT 360
/** The scenes' camera: 1.7 m above a floor, its field of view and planes as the game's. */
#define EYE_HEIGHT 1.7f
#define SCENE_FOV 60.0f
#define SCENE_NEAR 0.05
#define SCENE_FAR 4000.0


// Types.
/* What a synthetic scene holds: a floor at y = -EYE_HEIGHT (the camera at the origin), and optionally a wall across the
 * view at z = WallZ (as wide as the screen) and a small plate facing the camera at z = PlateZ around PlateCenter. */
typedef struct SceneShapeStruct
{
    bool HasWall;
    float WallZ;
    bool HasPlate;
    float PlateZ;
    Vector2 PlateCenter;
    float PlateHalfSize;
} SceneShape;

/* A scene's view and its depth as the GPU would store it (rows from the bottom). */
typedef struct SceneStruct
{
    WorldRenderView View;
    WorldReflectionsView Reflections;
    float* Depth;
} Scene;


// Static functions.
static float ReadDepth(const void* context, int32_t x, int32_t y)
{
    const Scene* Self = context;
    return Self->Depth[(size_t)y * (size_t)SCENE_WIDTH + (size_t)x];
}

/* The nearest of the shape's surfaces along a view ray from the origin, as its distance along the ray (+infinity: the
 * sky). */
static float CastShape(const SceneShape* shape, Vector3 direction)
{
    float Nearest = INFINITY;
    if (direction.y < 0.0f)
    {
        Nearest = -EYE_HEIGHT / direction.y;
    }
    if (shape->HasWall && (direction.z > 0.0f))
    {
        float T = shape->WallZ / direction.z;
        Nearest = fminf(Nearest, T);
    }
    if (shape->HasPlate && (direction.z > 0.0f))
    {
        float T = shape->PlateZ / direction.z;
        Vector3 Point = Vector3Scale(direction, T);
        if ((fabsf(Point.x - shape->PlateCenter.x) <= shape->PlateHalfSize)
            && (fabsf(Point.y - shape->PlateCenter.y) <= shape->PlateHalfSize))
        {
            Nearest = fminf(Nearest, T);
        }
    }
    return Nearest;
}

/* The depth a render-space point is stored with, through the view's projection. */
static float StoredDepth(const WorldRenderView* view, bool isReversed, Vector3 point)
{
    Vector4 Clip =
    {
        view->ViewProjection.m0 * point.x + view->ViewProjection.m4 * point.y + view->ViewProjection.m8 * point.z
            + view->ViewProjection.m12,
        view->ViewProjection.m1 * point.x + view->ViewProjection.m5 * point.y + view->ViewProjection.m9 * point.z
            + view->ViewProjection.m13,
        view->ViewProjection.m2 * point.x + view->ViewProjection.m6 * point.y + view->ViewProjection.m10 * point.z
            + view->ViewProjection.m14,
        view->ViewProjection.m3 * point.x + view->ViewProjection.m7 * point.y + view->ViewProjection.m11 * point.z
            + view->ViewProjection.m15
    };
    float Ndc = Clip.z / Clip.w;
    return isReversed ? Ndc : (Ndc * 0.5f + 0.5f);
}

/* Builds a scene: the camera at the origin with the given yaw (0: toward +Z) and pitch, every pixel's depth from its
 * view ray (sky: 0 reversed, 1 standard). */
static void Scene_Construct(Scene* self, const SceneShape* shape, float yaw, float pitch, bool isReversed)
{
    GameCamera View = GameCamera_Create(Vector3Double_Zero(), SCENE_FOV);
    View.Yaw = yaw;
    View.Pitch = pitch;
    self->View = WorldRenderSpace_BuildView(&View, (double)SCENE_WIDTH / (double)SCENE_HEIGHT, SCENE_NEAR, SCENE_FAR,
        isReversed);
    self->Reflections = (WorldReflectionsView)
    {
        .ViewProjection = self->View.ViewProjection, .Forward = GameCamera_GetForward(&View),
        .NearPlane = (float)SCENE_NEAR, .FarPlane = (float)SCENE_FAR, .IsDepthReversed = isReversed,
        .Width = SCENE_WIDTH, .Height = SCENE_HEIGHT
    };
    self->Depth = Memory_Allocate(sizeof(float) * (size_t)SCENE_WIDTH * (size_t)SCENE_HEIGHT);
    for (int32_t Y = 0; Y < SCENE_HEIGHT; Y++)
    {
        for (int32_t X = 0; X < SCENE_WIDTH; X++)
        {
            Vector3 ViewRay = WorldRenderSpace_GetSkyRay(&self->View.InvViewProjection, ((float)X + 0.5f) / SCENE_WIDTH,
                ((float)Y + 0.5f) / SCENE_HEIGHT);
            float T = CastShape(shape, ViewRay);
            self->Depth[(size_t)Y * SCENE_WIDTH + (size_t)X] = isinf(T) ? (isReversed ? 0.0f : 1.0f)
                : StoredDepth(&self->View, isReversed, Vector3Scale(ViewRay, T));
        }
    }
}

static void Scene_Deconstruct(Scene* self)
{
    Memory_Free(self->Depth);
    self->Depth = NULL;
}

/* Every pixel shows the sky. */
static void Scene_ClearToSky(Scene* self)
{
    for (size_t Index = 0U; Index < (size_t)SCENE_WIDTH * (size_t)SCENE_HEIGHT; Index++)
    {
        self->Depth[Index] = self->Reflections.IsDepthReversed ? 0.0f : 1.0f;
    }
}

/* A pixel shows a surface at a distance in front of the camera. */
static void Scene_SetDistance(Scene* self, int32_t x, int32_t y, float distance)
{
    Vector3 ViewRay = WorldRenderSpace_GetSkyRay(&self->View.InvViewProjection, ((float)x + 0.5f) / SCENE_WIDTH,
        ((float)y + 0.5f) / SCENE_HEIGHT);
    float Along = distance / Vector3DotProduct(ViewRay, self->Reflections.Forward);
    self->Depth[(size_t)y * SCENE_WIDTH + (size_t)x] = StoredDepth(&self->View, self->Reflections.IsDepthReversed,
        Vector3Scale(ViewRay, Along));
}

/* The render-space point a view ray through a screen position (0 to 1, from the bottom left; beyond for points off the
 * screen) reaches at a distance in front of the camera. */
static Vector3 Scene_PointAt(const Scene* self, float u, float v, float distance)
{
    Vector3 ViewRay = WorldRenderSpace_GetSkyRay(&self->View.InvViewProjection, u, v);
    return Vector3Scale(ViewRay, distance / Vector3DotProduct(ViewRay, self->Reflections.Forward));
}

/* The ray's facing as the march takes it (the same sum in the same order). */
static float FacingOf(const Scene* self, Vector3 direction)
{
    const Vector3* Forward = &self->Reflections.Forward;
    return direction.x * Forward->x + direction.y * Forward->y + direction.z * Forward->z;
}

/* An answer's confidence as the march gives it: its pixel's edge fade and the ray's facing fade. */
static float ExpectedConfidence(const WorldReflectionsAnswer* answer, float facing)
{
    return WorldReflections_GetEdgeFade(((float)answer->X + 0.5f) / (float)SCENE_WIDTH,
        ((float)answer->Y + 0.5f) / (float)SCENE_HEIGHT) * WorldReflections_GetFacingFade(facing);
}

/* The surface point a pixel shows, and the view ray's reflection off a floor (normal +Y) there. */
static bool FloorPixel(const Scene* self, const SceneShape* shape, int32_t x, int32_t y, Vector3* outPoint,
    Vector3* outReflected)
{
    Vector3 ViewRay = WorldRenderSpace_GetSkyRay(&self->View.InvViewProjection, ((float)x + 0.5f) / SCENE_WIDTH,
        ((float)y + 0.5f) / SCENE_HEIGHT);
    float T = CastShape(shape, ViewRay);
    if (isinf(T))
    {
        return false;
    }
    *outPoint = Vector3Scale(ViewRay, T);
    *outReflected = Vector3Reflect(ViewRay, (Vector3){ 0.0f, 1.0f, 0.0f });
    return true;
}

/* The pixel a render-space point projects to (x right, y up from the bottom). */
static void ProjectToPixel(const Scene* self, Vector3 point, float* outX, float* outY)
{
    const Matrix* M = &self->View.ViewProjection;
    float ClipX = M->m0 * point.x + M->m4 * point.y + M->m8 * point.z + M->m12;
    float ClipY = M->m1 * point.x + M->m5 * point.y + M->m9 * point.z + M->m13;
    float ClipW = M->m3 * point.x + M->m7 * point.y + M->m11 * point.z + M->m15;
    *outX = (ClipX / ClipW * 0.5f + 0.5f) * SCENE_WIDTH;
    *outY = (ClipY / ClipW * 0.5f + 0.5f) * SCENE_HEIGHT;
}

static bool IsNear(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

/* The screen share is 1 for smooth surfaces, 0 for rough ones and falls steadily between. */
static void TheScreenShareFollowsTheRoughness(TestRun* run)
{
    TEST_CHECK(run, (WorldReflections_GetScreenShare(0.0f) == 1.0f)
        && (WorldReflections_GetScreenShare(WORLD_REFLECTIONS_SHARP_ROUGHNESS) == 1.0f)
        && (WorldReflections_GetScreenShare(WORLD_REFLECTIONS_ROUGH_ROUGHNESS) == 0.0f)
        && (WorldReflections_GetScreenShare(1.0f) == 0.0f) && (WorldReflections_GetScreenShare(NAN) == 0.0f));
    float Middle = WorldReflections_GetScreenShare(0.5f * (WORLD_REFLECTIONS_SHARP_ROUGHNESS
        + WORLD_REFLECTIONS_ROUGH_ROUGHNESS));
    TEST_CHECK(run, IsNear(Middle, 0.5f, 1e-6f));
    // A smoothstep: a quarter of the way the share has fallen by 0.15625 (a straight line would give 0.25).
    float Quarter = WorldReflections_GetScreenShare(WORLD_REFLECTIONS_SHARP_ROUGHNESS
        + 0.25f * (WORLD_REFLECTIONS_ROUGH_ROUGHNESS - WORLD_REFLECTIONS_SHARP_ROUGHNESS));
    TEST_CHECK(run, IsNear(Quarter, 0.84375f, 1e-5f));
    float Last = 1.0f;
    bool IsFalling = true;
    for (int Step = 0; Step <= 100; Step++)
    {
        float Share = WorldReflections_GetScreenShare((float)Step / 100.0f);
        IsFalling = IsFalling && (Share <= Last) && (Share >= 0.0f) && (Share <= 1.0f);
        Last = Share;
    }
    TEST_CHECK(run, IsFalling);
    // The puddles' still water mirrors in full.
    TEST_CHECK(run, WorldReflections_GetScreenShare(0.045f) == 1.0f);
}

/* Every direction packs into [-1, 1]^2 and comes back to within a float's rounding; zero packs as +Z. */
static void DirectionsPackAndUnpack(TestRun* run)
{
    float WorstError = 0.0f;
    bool IsInRange = true;
    for (int Theta = 0; Theta <= 36; Theta++)
    {
        for (int Phi = 0; Phi < 72; Phi++)
        {
            float Polar = (float)Theta * (PI / 36.0f);
            float Azimuth = (float)Phi * (2.0f * PI / 72.0f);
            Vector3 Direction = { sinf(Polar) * cosf(Azimuth), sinf(Polar) * sinf(Azimuth), cosf(Polar) };
            Vector2 Packed = WorldReflections_EncodeDirection(Direction);
            IsInRange = IsInRange && (fabsf(Packed.x) <= 1.0f) && (fabsf(Packed.y) <= 1.0f);
            Vector3 Back = WorldReflections_DecodeDirection(Packed);
            WorstError = fmaxf(WorstError, Vector3Distance(Direction, Back));
        }
    }
    TEST_CHECK(run, IsInRange && (WorstError < 1e-5f));
    const Vector3 Axes[] =
    {
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
    };
    for (size_t Index = 0U; Index < (sizeof(Axes) / sizeof(Axes[0])); Index++)
    {
        TEST_CHECK(run, Vector3Distance(WorldReflections_DecodeDirection(WorldReflections_EncodeDirection(Axes[Index])),
            Axes[Index]) < 1e-6f);
    }
    Vector2 Zero = WorldReflections_EncodeDirection((Vector3){ 0.0f, 0.0f, 0.0f });
    TEST_CHECK(run, (Zero.x == 0.0f) && (Zero.y == 0.0f));
    // The packing itself, as the lit shaders write it and the pass reads it: the upper half as its x and y over the sum
    // of the components' sizes, the lower half folded over the diagonals, a zero component counting as positive.
    Vector2 Up = WorldReflections_EncodeDirection((Vector3){ 0.3f, -0.2f, 0.5f });
    Vector2 Down = WorldReflections_EncodeDirection((Vector3){ 0.0f, 0.0f, -1.0f });
    Vector2 Folded = WorldReflections_EncodeDirection((Vector3){ 0.0f, -0.6f, -0.8f });
    TEST_CHECK(run, IsNear(Up.x, 0.3f, 1e-6f) && IsNear(Up.y, -0.2f, 1e-6f));
    TEST_CHECK(run, (Down.x == 1.0f) && (Down.y == 1.0f));
    TEST_CHECK(run, IsNear(Folded.x, 4.0f / 7.0f, 1e-6f) && (Folded.y == -1.0f));
    // A length other than 1 packs as its unit direction.
    Vector2 Long = WorldReflections_EncodeDirection((Vector3){ 0.0f, 3.0f, -4.0f });
    TEST_CHECK(run, Vector3Distance(WorldReflections_DecodeDirection(Long), (Vector3){ 0.0f, 0.6f, -0.8f }) < 1e-6f);
}

/* Answers fade toward the screen's edges and for rays toward the camera. */
static void AnswersFadeAtTheEdgesAndTowardTheCamera(TestRun* run)
{
    TEST_CHECK(run, (WorldReflections_GetEdgeFade(0.5f, 0.5f) == 1.0f)
        && (WorldReflections_GetEdgeFade(0.0f, 0.5f) == 0.0f) && (WorldReflections_GetEdgeFade(0.5f, 1.0f) == 0.0f)
        && (WorldReflections_GetEdgeFade(-0.1f, 0.5f) == 0.0f) && (WorldReflections_GetEdgeFade(0.5f, 1.2f) == 0.0f));
    TEST_CHECK(run, IsNear(WorldReflections_GetEdgeFade(0.5f * WORLD_REFLECTIONS_EDGE_FADE, 0.5f), 0.5f, 1e-6f)
        && IsNear(WorldReflections_GetEdgeFade(0.5f, 1.0f - 0.25f * WORLD_REFLECTIONS_EDGE_FADE), 0.25f, 1e-6f)
        && (WorldReflections_GetEdgeFade(WORLD_REFLECTIONS_EDGE_FADE, WORLD_REFLECTIONS_EDGE_FADE) == 1.0f));
    TEST_CHECK(run, (WorldReflections_GetFacingFade(-1.0f) == 0.0f)
        && (WorldReflections_GetFacingFade(WORLD_REFLECTIONS_FACING_FADE_START) == 0.0f)
        && (WorldReflections_GetFacingFade(WORLD_REFLECTIONS_FACING_FADE_END) == 1.0f)
        && (WorldReflections_GetFacingFade(0.0f) == 1.0f) && (WorldReflections_GetFacingFade(1.0f) == 1.0f));
    TEST_CHECK(run, IsNear(WorldReflections_GetFacingFade(WORLD_REFLECTIONS_FACING_FADE_START
        + 0.25f * (WORLD_REFLECTIONS_FACING_FADE_END - WORLD_REFLECTIONS_FACING_FADE_START)), 0.15625f, 1e-5f));
    float Last = 0.0f;
    bool IsRising = true;
    for (int Step = 0; Step <= 100; Step++)
    {
        float Fade = WorldReflections_GetFacingFade(-1.0f + (float)Step / 50.0f);
        IsRising = IsRising && (Fade >= Last);
        Last = Fade;
    }
    TEST_CHECK(run, IsRising);
    TEST_CHECK(run, (WorldReflections_GetThickness(0.0f) == WORLD_REFLECTIONS_THICKNESS)
        && IsNear(WorldReflections_GetThickness(100.0f),
            WORLD_REFLECTIONS_THICKNESS + 100.0f * WORLD_REFLECTIONS_THICKNESS_SHARE, 1e-5f));
}

/* A stored depth gives the distance in front of the camera in either convention; the sky gives none. Standard depth
 * counts everything past the sky's threshold (shaders/common/depth.glsl: 0.9999, about 440 m with the game's planes) as
 * the sky, as every reader of it does. */
static void DistancesFromBothDepthConventions(TestRun* run)
{
    const float Distances[] = { 0.05f, 0.5f, 1.7f, 10.0f, 250.0f, 3900.0f };
    for (int Convention = 0; Convention < 2; Convention++)
    {
        bool IsReversed = (Convention == 0);
        SceneShape Shape = { 0 };
        Scene Probe;
        Scene_Construct(&Probe, &Shape, 0.0f, 0.0f, IsReversed);
        for (size_t Index = 0U; Index < (sizeof(Distances) / sizeof(Distances[0])); Index++)
        {
            float Depth = StoredDepth(&Probe.View, IsReversed, (Vector3){ 0.0f, 0.0f, Distances[Index] });
            float Back = WorldReflections_GetDistance(&Probe.Reflections, Depth);
            if (!IsReversed && (Distances[Index] > 440.0f))
            {
                TEST_CHECK(run, isinf(Back));
                continue;
            }
            // Standard depth keeps a float's resolution near its far plane only.
            float Tolerance = IsReversed ? (Distances[Index] * 1e-6f) : (Distances[Index] * Distances[Index] * 2e-4f);
            TEST_CHECK(run, IsNear(Back, Distances[Index], fmaxf(Tolerance, 1e-6f)));
        }
        TEST_CHECK(run, isinf(WorldReflections_GetDistance(&Probe.Reflections, IsReversed ? 0.0f : 1.0f)));
        Scene_Deconstruct(&Probe);
    }
}

/* A floor mirrors a wall in front of the camera: the ray from a floor pixel finds the wall where the mirror image lies,
 * in either depth convention, and the answer counts in full. */
static void AFloorMirrorsAWallInFront(TestRun* run)
{
    SceneShape Shape = { .HasWall = true, .WallZ = 12.0f };
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    for (int Convention = 0; Convention < 2; Convention++)
    {
        Scene Floor;
        Scene_Construct(&Floor, &Shape, 0.0f, -0.2f, Convention == 0);
        // Floor pixels across the lower half of the screen whose mirror image lies well inside it.
        const int32_t Pixels[][2] = { { 480, 100 }, { 320, 120 }, { 160, 130 }, { 400, 150 } };
        for (size_t Index = 0U; Index < (sizeof(Pixels) / sizeof(Pixels[0])); Index++)
        {
            Vector3 Point;
            Vector3 Reflected;
            if (!TEST_CHECK(run, FloorPixel(&Floor, &Shape, Pixels[Index][0], Pixels[Index][1], &Point, &Reflected)))
            {
                continue;
            }
            WorldReflectionsAnswer Answer = WorldReflections_March(&Floor.Reflections, &Settings, Point, Reflected, ReadDepth, &Floor);
            // Where the reflected ray meets the wall.
            Vector3 OnWall = Vector3Add(Point, Vector3Scale(Reflected, (Shape.WallZ - Point.z) / Reflected.z));
            float ExpectX = 0.0f;
            float ExpectY = 0.0f;
            ProjectToPixel(&Floor, OnWall, &ExpectX, &ExpectY);
            TEST_CHECK(run, Answer.IsFound && !Answer.IsSky);
            TEST_CHECK(run, IsNear((float)Answer.X + 0.5f, ExpectX, 1.5f) && IsNear((float)Answer.Y + 0.5f, ExpectY, 1.5f));
            TEST_CHECK(run, Answer.Confidence == 1.0f);
        }
        // Nearer the camera the mirror image climbs toward the screen's top: the answer fades there by its edge, and
        // past it there is none.
        Vector3 Point;
        Vector3 Reflected;
        if (TEST_CHECK(run, FloorPixel(&Floor, &Shape, 200, 70, &Point, &Reflected)))
        {
            WorldReflectionsAnswer Answer = WorldReflections_March(&Floor.Reflections, &Settings, Point, Reflected, ReadDepth, &Floor);
            float Fade = WorldReflections_GetEdgeFade(((float)Answer.X + 0.5f) / SCENE_WIDTH,
                ((float)Answer.Y + 0.5f) / SCENE_HEIGHT);
            TEST_CHECK(run, Answer.IsFound && (Fade < 1.0f) && (Answer.Confidence == Fade));
        }
        if (TEST_CHECK(run, FloorPixel(&Floor, &Shape, 320, 40, &Point, &Reflected)))
        {
            WorldReflectionsAnswer Answer = WorldReflections_March(&Floor.Reflections, &Settings, Point, Reflected, ReadDepth, &Floor);
            TEST_CHECK(run, !Answer.IsFound && (Answer.Confidence == 0.0f));
        }
        // At the wall's foot: the floor pixel just below the line where floor and wall meet mirrors the wall a few
        // pixels from itself (the march starts WORLD_REFLECTIONS_START_OFFSET pixels along).
        float FootX = 0.0f;
        float FootY = 0.0f;
        ProjectToPixel(&Floor, (Vector3){ 0.0f, -EYE_HEIGHT, Shape.WallZ }, &FootX, &FootY);
        if (TEST_CHECK(run, FloorPixel(&Floor, &Shape, (int32_t)FootX, (int32_t)floorf(FootY) - 1, &Point, &Reflected)))
        {
            WorldReflectionsAnswer Answer = WorldReflections_March(&Floor.Reflections, &Settings, Point, Reflected,
                ReadDepth, &Floor);
            Vector3 OnWall = Vector3Add(Point, Vector3Scale(Reflected, (Shape.WallZ - Point.z) / Reflected.z));
            float ExpectX = 0.0f;
            float ExpectY = 0.0f;
            ProjectToPixel(&Floor, OnWall, &ExpectX, &ExpectY);
            float StartX = 0.0f;
            float StartY = 0.0f;
            ProjectToPixel(&Floor, Point, &StartX, &StartY);
            TEST_CHECK(run, hypotf(ExpectX - StartX, ExpectY - StartY) < 4.0f);
            TEST_CHECK(run, Answer.IsFound && !Answer.IsSky && IsNear((float)Answer.X + 0.5f, ExpectX, 1.5f)
                && IsNear((float)Answer.Y + 0.5f, ExpectY, 1.5f));
        }
        Scene_Deconstruct(&Floor);
    }
}

/* Without a wall the floor's ray climbs into the sky: its vanishing point on the screen answers, unless something stands
 * in front of it there. */
static void ARayIntoTheSkyIsAnsweredByItsVanishingPoint(TestRun* run)
{
    SceneShape Shape = { 0 };
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    Scene Open;
    Scene_Construct(&Open, &Shape, 0.0f, 0.0f, true);
    Vector3 Point;
    Vector3 Reflected;
    if (TEST_CHECK(run, FloorPixel(&Open, &Shape, 320, 120, &Point, &Reflected)))
    {
        WorldReflectionsAnswer Answer = WorldReflections_March(&Open.Reflections, &Settings, Point, Reflected, ReadDepth, &Open);
        // The vanishing point: the direction itself, projected.
        float ExpectX = 0.0f;
        float ExpectY = 0.0f;
        ProjectToPixel(&Open, Vector3Scale(Reflected, 1.0e6f), &ExpectX, &ExpectY);
        TEST_CHECK(run, Answer.IsFound && Answer.IsSky);
        TEST_CHECK(run, IsNear((float)Answer.X + 0.5f, ExpectX, 1.0f) && IsNear((float)Answer.Y + 0.5f, ExpectY, 1.0f));
        TEST_CHECK(run, Answer.Confidence == 1.0f);
        // A small plate 50 m out in front of the vanishing point: the ray passes it hundreds of metres behind, and the
        // sky it goes to does not show there, so nothing answers.
        Vector3 TowardPlate = Vector3Scale(Reflected, 50.0f / Reflected.z);
        SceneShape Plated =
        {
            .HasPlate = true, .PlateZ = 50.0f, .PlateCenter = { TowardPlate.x, TowardPlate.y }, .PlateHalfSize = 0.5f
        };
        Scene Hidden;
        Scene_Construct(&Hidden, &Plated, 0.0f, 0.0f, true);
        TEST_CHECK(run, isfinite(WorldReflections_GetDistance(&Hidden.Reflections,
            ReadDepth(&Hidden, (int32_t)ExpectX, (int32_t)ExpectY))));
        WorldReflectionsAnswer Behind = WorldReflections_March(&Hidden.Reflections, &Settings, Point, Reflected, ReadDepth,
            &Hidden);
        TEST_CHECK(run, !Behind.IsFound && (Behind.Confidence == 0.0f));
        Scene_Deconstruct(&Hidden);
    }
    // Looking down steeply the floor's reflection climbs past the top of the screen: no answer.
    Scene Down;
    Scene_Construct(&Down, &Shape, 0.0f, -1.0f, true);
    if (TEST_CHECK(run, FloorPixel(&Down, &Shape, 320, 300, &Point, &Reflected)))
    {
        WorldReflectionsAnswer Answer = WorldReflections_March(&Down.Reflections, &Settings, Point, Reflected, ReadDepth, &Down);
        TEST_CHECK(run, !Answer.IsFound && (Answer.Confidence == 0.0f));
    }
    Scene_Deconstruct(&Down);
    Scene_Deconstruct(&Open);
}

/* A ray back toward the camera (a wall facing it, mirrored head on) runs to the near plane and finds nothing. */
static void ARayTowardTheCameraHasNoAnswer(TestRun* run)
{
    SceneShape Shape = { .HasWall = true, .WallZ = 6.0f };
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    Scene Facing;
    Scene_Construct(&Facing, &Shape, 0.0f, 0.0f, true);
    Vector3 ViewRay = WorldRenderSpace_GetSkyRay(&Facing.View.InvViewProjection, 0.55f, 0.6f);
    Vector3 Point = Vector3Scale(ViewRay, Shape.WallZ / ViewRay.z);
    Vector3 Reflected = Vector3Reflect(ViewRay, (Vector3){ 0.0f, 0.0f, -1.0f });
    WorldReflectionsAnswer Answer = WorldReflections_March(&Facing.Reflections, &Settings, Point, Reflected, ReadDepth, &Facing);
    TEST_CHECK(run, !Answer.IsFound || (Answer.Confidence == 0.0f));
    Scene_Deconstruct(&Facing);
}

/* A ray passing behind a thin plate (well behind it, from the camera) goes on and finds the wall; a plate the ray meets
 * stops it. */
static void AThinPlateDoesNotStopARayPassingBehindIt(TestRun* run)
{
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    SceneShape Shape = { .HasWall = true, .WallZ = 12.0f };
    Scene Probe;
    Scene_Construct(&Probe, &Shape, 0.0f, -0.2f, true);
    Vector3 Point;
    Vector3 Reflected;
    bool HasFloor = FloorPixel(&Probe, &Shape, 320, 130, &Point, &Reflected);
    Scene_Deconstruct(&Probe);
    if (!TEST_CHECK(run, HasFloor))
    {
        return;
    }
    // The ray's point 1 m in front of the wall, and the camera's line to it at a plate 2.5 m nearer: the ray passes
    // 2.5 m behind the plate there.
    Vector3 Behind = Vector3Add(Point, Vector3Scale(Reflected, (Shape.WallZ - 1.0f - Point.z) / Reflected.z));
    float PlateZ = Behind.z - 2.5f;
    Vector3 OnPlate = Vector3Scale(Behind, PlateZ / Behind.z);
    Shape.HasPlate = true;
    Shape.PlateZ = PlateZ;
    Shape.PlateCenter = (Vector2){ OnPlate.x, OnPlate.y };
    Shape.PlateHalfSize = 0.15f;
    Scene Plated;
    Scene_Construct(&Plated, &Shape, 0.0f, -0.2f, true);
    WorldReflectionsAnswer Answer = WorldReflections_March(&Plated.Reflections, &Settings, Point, Reflected, ReadDepth, &Plated);
    Vector3 OnWall = Vector3Add(Point, Vector3Scale(Reflected, (Shape.WallZ - Point.z) / Reflected.z));
    float ExpectX = 0.0f;
    float ExpectY = 0.0f;
    ProjectToPixel(&Plated, OnWall, &ExpectX, &ExpectY);
    TEST_CHECK(run, Answer.IsFound && !Answer.IsSky);
    TEST_CHECK(run, IsNear((float)Answer.X + 0.5f, ExpectX, 1.5f) && IsNear((float)Answer.Y + 0.5f, ExpectY, 1.5f));
    Scene_Deconstruct(&Plated);

    // A plate where the ray itself crosses (at the plate's distance): the ray hits it.
    Vector3 Crossing = Vector3Add(Point, Vector3Scale(Reflected, (Shape.WallZ - 4.0f - Point.z) / Reflected.z));
    Shape.PlateZ = Crossing.z;
    Shape.PlateCenter = (Vector2){ Crossing.x, Crossing.y };
    Shape.PlateHalfSize = 0.5f;
    Scene Met;
    Scene_Construct(&Met, &Shape, 0.0f, -0.2f, true);
    WorldReflectionsAnswer Hit = WorldReflections_March(&Met.Reflections, &Settings, Point, Reflected, ReadDepth,
        &Met);
    ProjectToPixel(&Met, Crossing, &ExpectX, &ExpectY);
    TEST_CHECK(run, Hit.IsFound && !Hit.IsSky);
    TEST_CHECK(run, IsNear((float)Hit.X + 0.5f, ExpectX, 1.5f) && IsNear((float)Hit.Y + 0.5f, ExpectY, 1.5f));
    Scene_Deconstruct(&Met);
}

/* What a walk of every pixel along the ray finds (the march without its blocks): the first pixel where the ray lies behind
 * the surface within the thickness. */
static WorldReflectionsAnswer WalkEveryPixel(const Scene* scene, Vector3 origin, Vector3 direction)
{
    WorldReflectionsAnswer None = { 0 };
    const Matrix* M = &scene->View.ViewProjection;
    Vector3 End = Vector3Add(origin, Vector3Scale(direction, WORLD_REFLECTIONS_RAY_LENGTH));
    float W0 = M->m3 * origin.x + M->m7 * origin.y + M->m11 * origin.z + M->m15;
    float W1 = M->m3 * End.x + M->m7 * End.y + M->m11 * End.z + M->m15;
    float X0 = 0.0f;
    float Y0 = 0.0f;
    float X1 = 0.0f;
    float Y1 = 0.0f;
    ProjectToPixel(scene, origin, &X0, &Y0);
    ProjectToPixel(scene, End, &X1, &Y1);
    float Dx = X1 - X0;
    float Dy = Y1 - Y0;
    float Pixels = sqrtf(Dx * Dx + Dy * Dy);
    for (float Walk = WORLD_REFLECTIONS_START_OFFSET; Walk < Pixels; Walk += 0.25f)
    {
        float Share = Walk / Pixels;
        float X = X0 + Dx * Share;
        float Y = Y0 + Dy * Share;
        if ((X < 0.0f) || (Y < 0.0f) || (X >= SCENE_WIDTH) || (Y >= SCENE_HEIGHT))
        {
            return None;
        }
        float RayDistance = 1.0f / (1.0f / W0 + (1.0f / W1 - 1.0f / W0) * Share);
        float Surface = WorldReflections_GetDistance(&scene->Reflections, ReadDepth(scene, (int32_t)X, (int32_t)Y));
        if ((RayDistance > Surface) && ((RayDistance - Surface) < WorldReflections_GetThickness(Surface)))
        {
            return (WorldReflectionsAnswer){ .IsFound = true, .X = (int32_t)X, .Y = (int32_t)Y };
        }
    }
    return None;
}

/* The blocks only skip what cannot stop the ray: over a floor, a wall and a thin plate, wherever a walk of every quarter
 * pixel finds a surface, the march finds it too (to within the pixel the two land in) or one before it along the ray (a
 * plate two pixels thick the walk's samples can still miss at its edge); and from the floor pixel that sees a thin
 * plate's mirror image it finds the plate wherever the plate stands, as a stride of two pixels would not. */
static void TheBlocksSkipOnlyWhatCannotStopTheRay(TestRun* run)
{
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    SceneShape Shape =
    {
        .HasWall = true, .WallZ = 14.0f, .HasPlate = true, .PlateZ = 9.0f, .PlateCenter = { 0.3f, 0.5f },
        .PlateHalfSize = 0.035f
    };
    Scene Thin;
    Scene_Construct(&Thin, &Shape, 0.0f, -0.15f, true);
    size_t Compared = 0U;
    size_t Agreeing = 0U;
    size_t OnThePlate = 0U;
    for (int32_t Y = 20; Y < 170; Y += 7)
    {
        for (int32_t X = 40; X < 600; X += 9)
        {
            Vector3 Point;
            Vector3 Reflected;
            if (!FloorPixel(&Thin, &Shape, X, Y, &Point, &Reflected))
            {
                continue;
            }
            WorldReflectionsAnswer Marched = WorldReflections_March(&Thin.Reflections, &Settings, Point, Reflected,
                ReadDepth, &Thin);
            WorldReflectionsAnswer Walked = WalkEveryPixel(&Thin, Point, Reflected);
            if (Marched.IsSky || !Walked.IsFound)
            {
                continue; // the sky's answer and rays off the screen are the other tests'
            }
            Compared++;
            float StartX = 0.0f;
            float StartY = 0.0f;
            ProjectToPixel(&Thin, Point, &StartX, &StartY);
            float MarchedAlong = hypotf((float)Marched.X + 0.5f - StartX, (float)Marched.Y + 0.5f - StartY);
            float WalkedAlong = hypotf((float)Walked.X + 0.5f - StartX, (float)Walked.Y + 0.5f - StartY);
            bool IsSame = Marched.IsFound && (((abs(Marched.X - Walked.X) <= 1) && (abs(Marched.Y - Walked.Y) <= 1))
                || (MarchedAlong < WalkedAlong));
            Agreeing += IsSame ? 1U : 0U;
            float Depth = ReadDepth(&Thin, Marched.X, Marched.Y);
            OnThePlate += (Marched.IsFound
                && (fabsf(WorldReflections_GetDistance(&Thin.Reflections, Depth) - Shape.PlateZ) < 0.5f)) ? 1U : 0U;
        }
    }
    TEST_CHECK(run, (Compared > 200U) && (Agreeing == Compared) && (OnThePlate > 0U));
    Scene_Deconstruct(&Thin);

    const Vector2 Centers[] = { { 0.3f, 0.5f }, { -1.1f, 0.2f }, { 1.4f, 1.1f }, { 0.0f, 1.6f } };
    for (size_t Index = 0U; Index < (sizeof(Centers) / sizeof(Centers[0])); Index++)
    {
        Shape.PlateCenter = Centers[Index];
        Scene_Construct(&Thin, &Shape, 0.0f, -0.15f, true);
        // The plate's centre mirrored in the floor, and the floor's point the camera sees it through.
        Vector3 Mirrored = { Centers[Index].x, -2.0f * EYE_HEIGHT - Centers[Index].y, Shape.PlateZ };
        Vector3 OnFloor = Vector3Scale(Mirrored, -EYE_HEIGHT / Mirrored.y);
        float FloorX = 0.0f;
        float FloorY = 0.0f;
        ProjectToPixel(&Thin, OnFloor, &FloorX, &FloorY);
        Vector3 Point;
        Vector3 Reflected;
        if (TEST_CHECK(run, FloorPixel(&Thin, &Shape, (int32_t)FloorX, (int32_t)FloorY, &Point, &Reflected)))
        {
            WorldReflectionsAnswer Answer = WorldReflections_March(&Thin.Reflections, &Settings, Point, Reflected,
                ReadDepth, &Thin);
            float Hit = Answer.IsFound
                ? WorldReflections_GetDistance(&Thin.Reflections, ReadDepth(&Thin, Answer.X, Answer.Y)) : 0.0f;
            TEST_CHECK(run, Answer.IsFound && !Answer.IsSky && (fabsf(Hit - Shape.PlateZ) < 0.5f));
        }
        Scene_Deconstruct(&Thin);
    }
}

/* A block's nearest surface: the largest depth reversed, the smallest standard; the sky's depth where it shows only the
 * sky; only its pixels on the screen. */
static void ABlocksNearestSurface(TestRun* run)
{
    SceneShape Shape = { .HasWall = true, .WallZ = 30.0f };
    for (int Convention = 0; Convention < 2; Convention++)
    {
        bool IsReversed = (Convention == 0);
        Scene Probe;
        Scene_Construct(&Probe, &Shape, 0.0f, 0.2f, IsReversed);
        bool IsRight = true;
        int32_t Columns = (SCENE_WIDTH + WORLD_REFLECTIONS_BLOCK_SIZE - 1) / WORLD_REFLECTIONS_BLOCK_SIZE;
        int32_t Rows = (SCENE_HEIGHT + WORLD_REFLECTIONS_BLOCK_SIZE - 1) / WORLD_REFLECTIONS_BLOCK_SIZE;
        for (int32_t BlockY = 0; BlockY < Rows; BlockY += 3)
        {
            for (int32_t BlockX = 0; BlockX < Columns; BlockX += 5)
            {
                float Expected = IsReversed ? 0.0f : 1.0f;
                for (int32_t Y = BlockY * WORLD_REFLECTIONS_BLOCK_SIZE;
                    (Y < (BlockY + 1) * WORLD_REFLECTIONS_BLOCK_SIZE) && (Y < SCENE_HEIGHT); Y++)
                {
                    for (int32_t X = BlockX * WORLD_REFLECTIONS_BLOCK_SIZE;
                        (X < (BlockX + 1) * WORLD_REFLECTIONS_BLOCK_SIZE) && (X < SCENE_WIDTH); X++)
                    {
                        float Depth = ReadDepth(&Probe, X, Y);
                        Expected = IsReversed ? fmaxf(Expected, Depth) : fminf(Expected, Depth);
                    }
                }
                IsRight = IsRight && (WorldReflections_GetBlockNearest(&Probe.Reflections, BlockX, BlockY, ReadDepth,
                    &Probe) == Expected);
            }
        }
        TEST_CHECK(run, IsRight);
        Scene_Deconstruct(&Probe);
        // Without the wall, looking up, the screen's top rows show the sky alone.
        SceneShape Open = { 0 };
        Scene_Construct(&Probe, &Open, 0.0f, 0.2f, IsReversed);
        TEST_CHECK(run, WorldReflections_GetBlockNearest(&Probe.Reflections, 0, Rows - 1, ReadDepth, &Probe)
            == (IsReversed ? 0.0f : 1.0f));
        // A single surface in a block's last row and column, or in its first, is the block's nearest.
        Scene_ClearToSky(&Probe);
        const int32_t Last = WORLD_REFLECTIONS_BLOCK_SIZE - 1;
        Scene_SetDistance(&Probe, 5 * WORLD_REFLECTIONS_BLOCK_SIZE + Last, 7 * WORLD_REFLECTIONS_BLOCK_SIZE + Last, 20.0f);
        Scene_SetDistance(&Probe, 9 * WORLD_REFLECTIONS_BLOCK_SIZE, 2 * WORLD_REFLECTIONS_BLOCK_SIZE, 30.0f);
        TEST_CHECK(run, IsNear(WorldReflections_GetDistance(&Probe.Reflections,
                WorldReflections_GetBlockNearest(&Probe.Reflections, 5, 7, ReadDepth, &Probe)), 20.0f, 0.5f)
            && IsNear(WorldReflections_GetDistance(&Probe.Reflections,
                WorldReflections_GetBlockNearest(&Probe.Reflections, 9, 2, ReadDepth, &Probe)), 30.0f, 0.5f));
        Scene_Deconstruct(&Probe);
    }
}

/* A ray that would visit more blocks than the quality allows has no answer. */
static void ARayPastItsBlocksHasNoAnswer(TestRun* run)
{
    SceneShape Shape = { .HasWall = true, .WallZ = 12.0f };
    Scene Floor;
    Scene_Construct(&Floor, &Shape, 0.0f, -0.2f, true);
    Vector3 Point;
    Vector3 Reflected;
    if (TEST_CHECK(run, FloorPixel(&Floor, &Shape, 320, 120, &Point, &Reflected)))
    {
        WorldReflectionsMarchSettings Full = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
        WorldReflectionsMarchSettings Short = { .MaxBlocks = 4, .ResolutionDivisor = 1 };
        TEST_CHECK(run, WorldReflections_March(&Floor.Reflections, &Full, Point, Reflected, ReadDepth, &Floor).IsFound);
        WorldReflectionsAnswer Cut = WorldReflections_March(&Floor.Reflections, &Short, Point, Reflected, ReadDepth,
            &Floor);
        TEST_CHECK(run, !Cut.IsFound && (Cut.Confidence == 0.0f));
    }
    Scene_Deconstruct(&Floor);
    // Nor does a ray into the sky that runs out of blocks before its vanishing point.
    SceneShape Open = { 0 };
    Scene Sky;
    Scene_Construct(&Sky, &Open, 0.0f, 0.0f, true);
    if (TEST_CHECK(run, FloorPixel(&Sky, &Open, 320, 120, &Point, &Reflected)))
    {
        WorldReflectionsMarchSettings Full = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
        WorldReflectionsMarchSettings Short = { .MaxBlocks = 4, .ResolutionDivisor = 1 };
        TEST_CHECK(run, WorldReflections_March(&Sky.Reflections, &Full, Point, Reflected, ReadDepth, &Sky).IsSky);
        WorldReflectionsAnswer Cut = WorldReflections_March(&Sky.Reflections, &Short, Point, Reflected, ReadDepth, &Sky);
        TEST_CHECK(run, !Cut.IsFound && (Cut.Confidence == 0.0f));
    }
    Scene_Deconstruct(&Sky);
}

/* A turned camera sees the same mirror images: from floor pixels all over the screen, looking aside and down at a wall,
 * the march finds the wall where the reflected ray meets it, and each answer counts by its pixel's edge fade and the
 * ray's facing (among them answers faded by the screen's left or right edge). */
static void ATurnedCameraSeesTheSameMirrorImages(TestRun* run)
{
    SceneShape Shape = { .HasWall = true, .WallZ = 12.0f };
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    const float Yaws[] = { 0.6f, -0.9f };
    size_t Checked = 0U;
    size_t Wrong = 0U;
    size_t BySides = 0U;
    for (size_t Turn = 0U; Turn < (sizeof(Yaws) / sizeof(Yaws[0])); Turn++)
    {
        Scene Turned;
        Scene_Construct(&Turned, &Shape, Yaws[Turn], -0.25f, true);
        for (int32_t Y = 10; Y < 180; Y += 17)
        {
            for (int32_t X = 8; X < SCENE_WIDTH; X += 21)
            {
                Vector3 Point;
                Vector3 Reflected;
                if (!FloorPixel(&Turned, &Shape, X, Y, &Point, &Reflected) || !IsNear(Point.y, -EYE_HEIGHT, 1e-3f)
                    || !(Reflected.z > 0.0f))
                {
                    continue; // the wall, or a reflection away from it
                }
                Vector3 OnWall = Vector3Add(Point, Vector3Scale(Reflected, (Shape.WallZ - Point.z) / Reflected.z));
                float ExpectX = 0.0f;
                float ExpectY = 0.0f;
                ProjectToPixel(&Turned, OnWall, &ExpectX, &ExpectY);
                if ((ExpectX < 2.0f) || (ExpectY < 2.0f) || (ExpectX > (float)SCENE_WIDTH - 2.0f)
                    || (ExpectY > (float)SCENE_HEIGHT - 2.0f))
                {
                    continue; // its image off the screen: the other tests'
                }
                WorldReflectionsAnswer Answer = WorldReflections_March(&Turned.Reflections, &Settings, Point, Reflected,
                    ReadDepth, &Turned);
                Checked++;
                bool IsRight = Answer.IsFound && !Answer.IsSky && IsNear((float)Answer.X + 0.5f, ExpectX, 1.5f)
                    && IsNear((float)Answer.Y + 0.5f, ExpectY, 1.5f)
                    && (Answer.Confidence == ExpectedConfidence(&Answer, FacingOf(&Turned, Reflected)));
                Wrong += IsRight ? 0U : 1U;
                float U = ((float)Answer.X + 0.5f) / (float)SCENE_WIDTH;
                float V = ((float)Answer.Y + 0.5f) / (float)SCENE_HEIGHT;
                float Side = fminf(U, 1.0f - U);
                BySides += (IsRight && (Side < WORLD_REFLECTIONS_EDGE_FADE) && (Side < fminf(V, 1.0f - V))) ? 1U : 0U;
            }
        }
        Scene_Deconstruct(&Turned);
    }
    TEST_CHECK(run, (Checked > 100U) && (Wrong == 0U) && (BySides > 0U));
}

/* A ray back toward the camera at a slant (from a wall down to the floor nearer the camera) finds the floor where it
 * meets it, its answer counting by the ray's facing: the march runs it to just short of the near plane. */
static void ARayTowardTheCameraFindsWhatItMeets(TestRun* run)
{
    SceneShape Shape = { .HasWall = true, .WallZ = 8.0f };
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    Scene Probe;
    Scene_Construct(&Probe, &Shape, 0.0f, -0.2f, true);
    Vector3 OnWallRay = WorldRenderSpace_GetSkyRay(&Probe.View.InvViewProjection, 0.15f, 0.7f);
    Vector3 Origin = Vector3Scale(OnWallRay, Shape.WallZ / OnWallRay.z);
    Vector3 OnFloorRay = WorldRenderSpace_GetSkyRay(&Probe.View.InvViewProjection, 0.85f, 0.42f);
    Vector3 Target = Vector3Scale(OnFloorRay, -EYE_HEIGHT / OnFloorRay.y);
    Vector3 Direction = Vector3Normalize(Vector3Subtract(Target, Origin));
    float Facing = FacingOf(&Probe, Direction);
    TEST_CHECK(run, (Facing > WORLD_REFLECTIONS_FACING_FADE_START) && (Facing < WORLD_REFLECTIONS_FACING_FADE_END));
    WorldReflectionsAnswer Answer = WorldReflections_March(&Probe.Reflections, &Settings, Origin, Direction, ReadDepth,
        &Probe);
    float ExpectX = 0.0f;
    float ExpectY = 0.0f;
    ProjectToPixel(&Probe, Target, &ExpectX, &ExpectY);
    TEST_CHECK(run, Answer.IsFound && !Answer.IsSky && IsNear((float)Answer.X + 0.5f, ExpectX, 1.5f)
        && IsNear((float)Answer.Y + 0.5f, ExpectY, 1.5f));
    TEST_CHECK(run, (Answer.Confidence > 0.0f) && (Answer.Confidence < 1.0f)
        && (Answer.Confidence == ExpectedConfidence(&Answer, Facing)));
    Scene_Deconstruct(&Probe);
}

/* A ray toward the camera ends just short of the near plane, not at infinity: where its end shows the sky, the sky is
 * still not its answer. */
static void ARayTowardTheCameraIsNotAnsweredByTheSky(TestRun* run)
{
    SceneShape Shape = { 0 };
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    Scene Up;
    Scene_Construct(&Up, &Shape, 0.0f, 0.3f, true);
    Scene_ClearToSky(&Up);
    Vector3 Origin = Scene_PointAt(&Up, 0.5f, 0.8f, 20.0f);
    Vector3 End = Scene_PointAt(&Up, 0.55f, 0.85f, (float)SCENE_NEAR * WORLD_REFLECTIONS_NEAR_MARGIN);
    Vector3 Direction = Vector3Normalize(Vector3Subtract(End, Origin));
    float EndX = 0.0f;
    float EndY = 0.0f;
    ProjectToPixel(&Up, End, &EndX, &EndY);
    TEST_CHECK(run, (EndX > 0.0f) && (EndX < (float)SCENE_WIDTH) && (EndY > 0.0f) && (EndY < (float)SCENE_HEIGHT)
        && (FacingOf(&Up, Direction) < 0.0f));
    WorldReflectionsAnswer Answer = WorldReflections_March(&Up.Reflections, &Settings, Origin, Direction, ReadDepth, &Up);
    TEST_CHECK(run, !Answer.IsFound && (Answer.Confidence == 0.0f));
    Scene_Deconstruct(&Up);
}

/* A ray into the sky whose vanishing point lies past an edge of the screen has no answer (past each edge in turn): the
 * march ends at the edge, whose sky is not the sky the ray goes to. With its vanishing point as far inside the edge the
 * sky there answers. */
static void ARayVanishingOffTheScreenHasNoAnswer(TestRun* run)
{
    SceneShape Shape = { 0 };
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    Scene Open;
    Scene_Construct(&Open, &Shape, 0.0f, 0.0f, true);
    Scene_ClearToSky(&Open);
    Vector3 Origin = Scene_PointAt(&Open, 0.5f, 0.5f, 5.0f);
    const float AcrossX = 40.0f / (float)SCENE_WIDTH;
    const float AcrossY = 40.0f / (float)SCENE_HEIGHT;
    const Vector2 Vanishing[][2] =
    {
        { { 1.0f + AcrossX, 0.55f }, { 1.0f - AcrossX, 0.55f } }, { { -AcrossX, 0.45f }, { AcrossX, 0.45f } },
        { { 0.55f, 1.0f + AcrossY }, { 0.55f, 1.0f - AcrossY } }, { { 0.45f, -AcrossY }, { 0.45f, AcrossY } }
    };
    for (size_t Index = 0U; Index < (sizeof(Vanishing) / sizeof(Vanishing[0])); Index++)
    {
        Vector3 Outward = WorldRenderSpace_GetSkyRay(&Open.View.InvViewProjection, Vanishing[Index][0].x,
            Vanishing[Index][0].y);
        WorldReflectionsAnswer Off = WorldReflections_March(&Open.Reflections, &Settings, Origin, Outward, ReadDepth,
            &Open);
        TEST_CHECK(run, !Off.IsFound && (Off.Confidence == 0.0f));
        Vector3 Inward = WorldRenderSpace_GetSkyRay(&Open.View.InvViewProjection, Vanishing[Index][1].x,
            Vanishing[Index][1].y);
        WorldReflectionsAnswer On = WorldReflections_March(&Open.Reflections, &Settings, Origin, Inward, ReadDepth,
            &Open);
        TEST_CHECK(run, On.IsFound && On.IsSky
            && IsNear((float)On.X + 0.5f, Vanishing[Index][1].x * (float)SCENE_WIDTH, 1.0f)
            && IsNear((float)On.Y + 0.5f, Vanishing[Index][1].y * (float)SCENE_HEIGHT, 1.0f));
    }
    Scene_Deconstruct(&Open);
}

/* A ray almost along the view (from a point above the eye, away from the camera) meets a wall wherever the wall stands:
 * across the pixel where it meets the wall its distance runs over several times the wall's thickness, and that pixel is
 * still its answer. */
static void ARayAlongTheViewMeetsAWallWhereverItStands(TestRun* run)
{
    WorldReflectionsMarchSettings Settings = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    const Vector3 Origin = { 0.3f, 1.0f, 5.0f };
    bool IsGrazing = true;
    for (int Step = 0; Step < 8; Step++)
    {
        SceneShape Shape = { .HasWall = true, .WallZ = 12.0f + 4.0f * (float)Step };
        Scene Probe;
        Scene_Construct(&Probe, &Shape, 0.0f, 0.0f, true);
        // Toward the screen point 10 pixels up and 3 across from the origin's: the ray creeps toward it.
        float OriginX = 0.0f;
        float OriginY = 0.0f;
        ProjectToPixel(&Probe, Origin, &OriginX, &OriginY);
        Vector3 Direction = WorldRenderSpace_GetSkyRay(&Probe.View.InvViewProjection, (OriginX + 3.0f) / SCENE_WIDTH,
            (OriginY + 10.0f) / SCENE_HEIGHT);
        Vector3 OnWall = Vector3Add(Origin, Vector3Scale(Direction, (Shape.WallZ - Origin.z) / Direction.z));
        float ExpectX = 0.0f;
        float ExpectY = 0.0f;
        ProjectToPixel(&Probe, OnWall, &ExpectX, &ExpectY);
        float NextX = 0.0f;
        float NextY = 0.0f;
        ProjectToPixel(&Probe, Vector3Add(OnWall, Direction), &NextX, &NextY);
        float MetresPerPixel = 1.0f / hypotf(NextX - ExpectX, NextY - ExpectY);
        IsGrazing = IsGrazing && (MetresPerPixel > 3.0f * WorldReflections_GetThickness(Shape.WallZ));
        WorldReflectionsAnswer Answer = WorldReflections_March(&Probe.Reflections, &Settings, Origin, Direction,
            ReadDepth, &Probe);
        TEST_CHECK(run, Answer.IsFound && !Answer.IsSky && IsNear((float)Answer.X + 0.5f, ExpectX, 1.5f)
            && IsNear((float)Answer.Y + 0.5f, ExpectY, 1.5f));
        Scene_Deconstruct(&Probe);
    }
    TEST_CHECK(run, IsGrazing);
}

/* The qualities: low at half the scene's resolution with fewer blocks, high at the scene's; off and anything else as
 * high (the pass does not run then). */
static void TheQualitiesSettings(TestRun* run)
{
    WorldReflectionsMarchSettings Low = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_LOW);
    WorldReflectionsMarchSettings High = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_HIGH);
    WorldReflectionsMarchSettings Off = WorldReflections_GetMarchSettings(CONFIG_REFLECTION_QUALITY_OFF);
    TEST_CHECK(run, (Low.MaxBlocks == WORLD_REFLECTIONS_LOW_BLOCKS) && (Low.ResolutionDivisor == 2)
        && (High.MaxBlocks == WORLD_REFLECTIONS_HIGH_BLOCKS) && (High.ResolutionDivisor == 1)
        && (Off.MaxBlocks == High.MaxBlocks) && (Off.ResolutionDivisor == High.ResolutionDivisor));
    TEST_CHECK(run, (Low.MaxBlocks <= WORLD_REFLECTIONS_MAX_BLOCKS) && (High.MaxBlocks <= WORLD_REFLECTIONS_MAX_BLOCKS)
        && (Low.MaxBlocks < High.MaxBlocks));
}


// Public functions.
void WorldReflectionsTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_screen_share_follows_the_roughness", TheScreenShareFollowsTheRoughness },
        { "directions_pack_and_unpack", DirectionsPackAndUnpack },
        { "answers_fade_at_the_edges_and_toward_the_camera", AnswersFadeAtTheEdgesAndTowardTheCamera },
        { "distances_from_both_depth_conventions", DistancesFromBothDepthConventions },
        { "a_floor_mirrors_a_wall_in_front", AFloorMirrorsAWallInFront },
        { "a_ray_into_the_sky_is_answered_by_its_vanishing_point", ARayIntoTheSkyIsAnsweredByItsVanishingPoint },
        { "a_ray_toward_the_camera_has_no_answer", ARayTowardTheCameraHasNoAnswer },
        { "a_thin_plate_does_not_stop_a_ray_passing_behind_it", AThinPlateDoesNotStopARayPassingBehindIt },
        { "the_blocks_skip_only_what_cannot_stop_the_ray", TheBlocksSkipOnlyWhatCannotStopTheRay },
        { "a_blocks_nearest_surface", ABlocksNearestSurface },
        { "a_ray_past_its_blocks_has_no_answer", ARayPastItsBlocksHasNoAnswer },
        { "a_turned_camera_sees_the_same_mirror_images", ATurnedCameraSeesTheSameMirrorImages },
        { "a_ray_toward_the_camera_finds_what_it_meets", ARayTowardTheCameraFindsWhatItMeets },
        { "a_ray_toward_the_camera_is_not_answered_by_the_sky", ARayTowardTheCameraIsNotAnsweredByTheSky },
        { "a_ray_vanishing_off_the_screen_has_no_answer", ARayVanishingOffTheScreenHasNoAnswer },
        { "a_ray_along_the_view_meets_a_wall_wherever_it_stands", ARayAlongTheViewMeetsAWallWhereverItStands },
        { "the_qualities_settings", TheQualitiesSettings },
    };
    TestRun_RunSuite(run, "WorldReflections", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
