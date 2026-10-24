#include "TestSuites.h"
#include "TestAssets.h"
#include "TestTerrain.h"
#include "TestWorld.h"
#include "GameMath.h"
#include "WorldVisibility.h"
#include "WorldRenderCache.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "WorldTerrainComponent.h"
#include "Renderer.h"
#include "raylib/raymath.h"
#include "wr/WRVectorMath.h"
#include <math.h>
#include <string.h>


// Macros.
/** Tolerance for the float comparisons. */
#define TEST_EPSILON 1e-4f
/** The test camera's projection: the renderer's (60 degrees vertically at 16:9, raylib's cull distances). */
#define TEST_FOV_Y_DEGREES 60.0
#define TEST_ASPECT (16.0 / 9.0)
#define TEST_NEAR 0.05
#define TEST_FAR 4000.0
/** The draw distance scale of the list tests: models 30 m, sprites 7.5 m, decals 3 m, each with a 3 m fade band. */
#define TEST_SCALE 0.05f
/** How far the render-space test shifts its scene and its camera (20 km along X and Z), and how many lists and LOD states
 *  it compares. */
#define SHIFT_DISTANCE 20000.0
#define SHIFT_MAX_ENTRIES 32U
/** How close a shifted scene's fades must stay (its floats are rounded to 2 mm out there; a fade band is 3 m). */
#define SHIFT_FADE_TOLERANCE 2e-3f
/** A box model's bounding radius at scale 1 (a unit cube's half diagonal). */
#define BOX_RADIUS 0.8660254f
/** The test sprites' bounding radius: their frame is 1x1 pixels at 0.5 pixels per unit, so they are 2 m wide and tall
 *  (half the diagonal around their centre pivot). */
#define SPRITE_RADIUS 1.4142136f


// Types.
/* An asset fixture with a "box" model (unit cube bounds) and a "box" world model over it (600 m, the draw distance
 * models had before world models), three 2 m world sprites over a one-frame animation (1x1 pixels) without draw
 * distances of their own: "cutout", "smoke" (blended) and "decal" (fixed, a decal), a world built with them, a render
 * cache and the visibility lists. */
typedef struct VisibilityFixtureStruct
{
    TestAssetFixture Assets;
    World World;
    KeyRegistry Keys;
    WorldRenderCache Cache;
    WorldVisibility Visibility;
    AssetUserID ModelUser;
    bool IsWorldConstructed;
} VisibilityFixture;


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static bool VisibilityFixture_Construct(TestRun* run, VisibilityFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    BoundingBox UnitCube = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    if (!TestAssetFixture_Construct(run, &self->Assets)
        || !TestAssets_AddModel(run, &self->Assets, "box", UnitCube, NULL)
        || !TestAssets_AddWorldModelOf(run, &self->Assets, "box", "box")
        || !TestAssets_AddSpriteAnimation(run, &self->Assets, "walk_animation", NULL)
        || !TestAssets_AddWorldSprite(run, &self->Assets, "{\"name\": \"cutout\", \"sprite_animation\": "
            "\"walk_animation\", \"pixels_per_unit\": 0.5}")
        || !TestAssets_AddWorldSprite(run, &self->Assets, "{\"name\": \"smoke\", \"sprite_animation\": "
            "\"walk_animation\", \"pixels_per_unit\": 0.5, \"alpha_mode\": \"blend\"}")
        || !TestAssets_AddWorldSprite(run, &self->Assets, "{\"name\": \"decal\", \"sprite_animation\": "
            "\"walk_animation\", \"pixels_per_unit\": 0.5, \"orientation\": \"fixed\", \"is_decal\": true}")
        || !TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(self->Assets.Manager, &self->ModelUser))
        || !TestWorld_Construct(run, &self->World, &self->Keys, self->Assets.Manager, NULL))
    {
        return false;
    }
    self->IsWorldConstructed = true;
    WorldRenderCache_Construct(&self->Cache);
    WorldVisibility_Construct(&self->Visibility);
    return true;
}

static void VisibilityFixture_Deconstruct(TestRun* run, VisibilityFixture* self)
{
    if (self->IsWorldConstructed)
    {
        WorldVisibility_Deconstruct(&self->Visibility);
        WorldRenderCache_Deconstruct(&self->Cache);
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

/* Syncs the cache with the world and builds the lists for @p view. */
static bool VisibilityFixture_Build(TestRun* run, VisibilityFixture* self, const WorldVisibilityView* view)
{
    return TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&self->Cache, &self->World, Vector3Double_Zero(),
               self->Assets.Manager, self->ModelUser, NULL))
        && TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&self->Visibility, &self->Cache, view));
}

/* The renderer's camera at @p position looking along +Z, as its view-projection's frustum. */
static WorldFrustum MakeCameraFrustum(Vector3 position)
{
    Matrix View = MatrixLookAt(position, Vector3Add(position, (Vector3){ 0.0f, 0.0f, 1.0f }),
        (Vector3){ 0.0f, 1.0f, 0.0f });
    Matrix Projection = MatrixPerspective(TEST_FOV_Y_DEGREES * DEG2RAD, TEST_ASPECT, TEST_NEAR, TEST_FAR);
    return WorldVisibility_ExtractFrustum(MatrixMultiply(View, Projection));
}

/* The direction toward the test's shadow light (high in the sky, a little off vertical). */
static Vector3 TowardLight(void)
{
    return Vector3Normalize((Vector3){ 0.2f, 1.0f, 0.1f });
}

/* A shadow cascade as the renderer builds one: an orthographic box of @p size around @p focus, seen from 120 m along
 * the light (depth 1 .. 240 m). */
static Matrix MakeCascadeViewProjection(Vector3 focus, float size)
{
    Vector3 LightPosition = Vector3Add(focus, Vector3Scale(TowardLight(), 120.0f));
    Matrix View = MatrixLookAt(LightPosition, focus, (Vector3){ 0.0f, 1.0f, 0.0f });
    double Half = (double)size * 0.5;
    Matrix Projection = MatrixOrtho(-Half, Half, -Half, Half, 1.0, 240.0);
    return MatrixMultiply(View, Projection);
}

/* The list tests' view: the camera at @p cameraPosition looking along +Z, both cascades (32 and 160 m boxes around it)
 * running, no cube shadow, TEST_SCALE, no zoom. */
static WorldVisibilityView MakeView(Vector3 cameraPosition)
{
    return (WorldVisibilityView)
    {
        .CameraPosition = cameraPosition,
        .CameraFrustum = MakeCameraFrustum(cameraPosition),
        .IsCullingEnabled = true,
        .IsNearCascadeActive = true,
        .NearCascade = WorldVisibility_ExtractFrustum(MakeCascadeViewProjection(cameraPosition, 32.0f)),
        .IsFarCascadeActive = true,
        .FarCascade = WorldVisibility_ExtractFrustum(MakeCascadeViewProjection(cameraPosition, 160.0f)),
        .IsPointShadowActive = false,
        .PointShadowPosition = { 0.0f, 0.0f, 0.0f },
        .PointShadowReach = 0.0f,
        .DrawDistanceScale = TEST_SCALE,
        .ZoomFactor = 1.0f
    };
}

/* Creates an object at @p position with a model component naming the world model @p worldModel; returns the component
 * (world-owned), or NULL with a failed check. */
static WorldModelComponent* AddWorldModel(TestRun* run, World* world, const char* worldModel, Vector3 position)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, NULL, &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, GameMath_FromVector3(position)))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component)))
    {
        return NULL;
    }
    WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(Component);
    return TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)worldModel))
        ? ModelComponent : NULL;
}

/* Creates an object at @p position with a box model; returns the model component (world-owned), or NULL with a failed
 * check. */
static WorldModelComponent* AddBox(TestRun* run, World* world, Vector3 position)
{
    return AddWorldModel(run, world, "box", position);
}

/* Creates an object at @p position with a sprite showing the world sprite @p worldSprite; returns the sprite component,
 * or NULL with a failed check. */
static WorldSpriteComponent* AddSprite(TestRun* run, World* world, Vector3 position, const char* worldSprite)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, NULL, &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, GameMath_FromVector3(position)))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_Sprite,
            &Component)))
    {
        return NULL;
    }
    WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(Component);
    return TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)worldSprite))
        ? Sprite : NULL;
}

/* Sets an object's opacity, keeping the rest of its tint. */
static bool SetOpacity(TestRun* run, WorldObject* object, float opacity)
{
    RenderColor Tint = WorldObject_GetTint(object);
    Tint.Opacity = opacity;
    return TEST_CHECK_SUCCESS(run, WorldObject_SetTint(object, Tint));
}

/* The index of a model component in the cache (SIZE_MAX when absent). */
static size_t ModelIndex(const WorldRenderCache* cache, const WorldModelComponent* model)
{
    size_t Count = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(cache, &Count);
    for (size_t Index = 0; Index < Count; Index++)
    {
        if (Models[Index].Component == model)
        {
            return Index;
        }
    }
    return SIZE_MAX;
}

/* The index of a sprite component in the cache (SIZE_MAX when absent). */
static size_t SpriteIndex(const WorldRenderCache* cache, const WorldSpriteComponent* sprite)
{
    size_t Count = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(cache, &Count);
    for (size_t Index = 0; Index < Count; Index++)
    {
        if (Sprites[Index].Component == sprite)
        {
            return Index;
        }
    }
    return SIZE_MAX;
}

/* Whether a pass's model list holds the model. */
static bool HasModel(const VisibilityFixture* fixture, WorldVisibilityPass pass, const WorldModelComponent* model)
{
    size_t Index = ModelIndex(&fixture->Cache, model);
    size_t Count = 0;
    const size_t* List = WorldVisibility_GetModels(&fixture->Visibility, &fixture->Cache, pass, &Count);
    for (size_t Position = 0; Position < Count; Position++)
    {
        if (List[Position] == Index)
        {
            return true;
        }
    }
    return false;
}

/* Whether a pass's sprite list holds the sprite. */
static bool HasSprite(const VisibilityFixture* fixture, WorldVisibilityPass pass, const WorldSpriteComponent* sprite)
{
    size_t Index = SpriteIndex(&fixture->Cache, sprite);
    size_t Count = 0;
    const size_t* List = WorldVisibility_GetSprites(&fixture->Visibility, &fixture->Cache, pass, &Count);
    for (size_t Position = 0; Position < Count; Position++)
    {
        if (List[Position] == Index)
        {
            return true;
        }
    }
    return false;
}

/* Whether a list's indices are strictly ascending (the passes draw in the world's order). */
static bool IsAscending(const size_t* list, size_t count)
{
    for (size_t Position = 1; Position < count; Position++)
    {
        if (list[Position] <= list[Position - 1])
        {
            return false;
        }
    }
    return true;
}

static WorldObject* OwnerOfModel(const WorldModelComponent* model)
{
    return WorldComponent_GetOwner(&model->Base);
}

static WorldObject* OwnerOfSprite(const WorldSpriteComponent* sprite)
{
    return WorldComponent_GetOwner(&sprite->Base);
}


// Tests: frustums.
/* The camera's frustum from the renderer's view-projection: unit inward normals, the near and far planes where the
 * projection puts them, and spheres inside, outside each side, behind the camera and crossing a plane. */
static void FrustumPlanesFromAViewProjection(TestRun* run)
{
    WorldFrustum Frustum = MakeCameraFrustum((Vector3){ 0.0f, 0.0f, 0.0f });
    for (int Index = 0; Index < (int)WorldFrustumPlane_Count; Index++)
    {
        TEST_CHECK(run, IsNear(Vector3Length(Frustum.Planes[Index].Normal), 1.0f));
    }
    // The near plane lies 0.05 m ahead, its normal forward (+Z); the far plane 4 km ahead (float precision: metres).
    const WorldPlane* Near = &Frustum.Planes[WorldFrustumPlane_Near];
    TEST_CHECK(run, IsNear(Near->Normal.z, 1.0f) && IsNear(Near->Distance, -0.05f));
    const WorldPlane* Far = &Frustum.Planes[WorldFrustumPlane_Far];
    TEST_CHECK(run, IsNear(Far->Normal.z, -1.0f) && (fabsf(Far->Distance - 4000.0f) < 25.0f));

    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 0.0f, 0.0f, 10.0f }, 1.0f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 0.0f, 0.0f, -5.0f }, 1.0f));
    // The camera inside a sphere behind it: the sphere crosses the near plane.
    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 0.0f, 0.0f, -0.5f }, 1.0f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 0.0f, 0.0f, 4100.0f }, 10.0f));

    // At 10 m the sides lie at x = +-10 tan(h), tan(h) = 16/9 tan(30 degrees) = 1.0264, 13 m out is 1.91 m from the side
    // plane; the top at y = 10 tan(30 degrees) = 5.774, 7 m up is 1.06 m above it.
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 13.0f, 0.0f, 10.0f }, 1.0f));
    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 13.0f, 0.0f, 10.0f }, 2.5f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ -13.0f, 0.0f, 10.0f }, 1.0f));
    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ -13.0f, 0.0f, 10.0f }, 2.5f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 0.0f, 7.0f, 10.0f }, 1.0f));
    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 0.0f, 7.0f, 10.0f }, 1.2f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 0.0f, -7.0f, 10.0f }, 1.0f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ NAN, 0.0f, 10.0f }, 1.0f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Frustum, (Vector3){ 0.0f, 0.0f, 10.0f }, NAN));
}

/* A shadow cascade's orthographic box: inside, past each side by more and by less than the radius, beyond its far
 * plane, between the light and its near plane, and behind the light. */
static void CascadeBoxCullsItsCasters(TestRun* run)
{
    Vector3 Focus = { 5.0f, 0.0f, 5.0f };
    WorldFrustum Box = WorldVisibility_ExtractFrustum(MakeCascadeViewProjection(Focus, 32.0f));
    // The light view's axes, as MatrixLookAt makes them: Z toward the light, X = up x Z, Y = Z x X.
    Vector3 AxisZ = TowardLight();
    Vector3 AxisX = Vector3Normalize(Vector3CrossProduct((Vector3){ 0.0f, 1.0f, 0.0f }, AxisZ));
    Vector3 AxisY = Vector3CrossProduct(AxisZ, AxisX);

    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Box, Focus, 0.5f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisX, 17.0f)), 0.5f));
    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisX, 17.0f)), 1.5f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisX, -17.0f)), 0.5f));
    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisY, 15.4f)), 0.5f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisY, -16.6f)), 0.5f));
    // 241 m from the light along its view, past the far plane at 240 m.
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisZ, -121.0f)), 0.5f));
    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisZ, -121.0f)), 1.5f));
    // 0.5 m from the light, short of the near plane at 1 m; and above the light.
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisZ, 119.5f)), 0.25f));
    TEST_CHECK(run, WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisZ, 119.5f)), 0.6f));
    TEST_CHECK(run, !WorldVisibility_IsSphereInFrustum(&Box, Vector3Add(Focus, Vector3Scale(AxisZ, 130.0f)), 1.0f));
}


// Tests: draw distances and fades.
/* An entry's own draw distance takes the quality scale unless it is fixed; the zoom factor is exactly 1 at 60 degrees,
 * below 1 zoomed in (distances shrink, so far things stay), above 1 zoomed out, and 1 for an unusable field of view. */
static void OwnDrawDistancesAndTheZoomFactor(TestRun* run)
{
    TEST_CHECK(run, WorldVisibility_ScaleDrawDistance(100.0f, false, 0.5f) == 50.0f);
    TEST_CHECK(run, WorldVisibility_ScaleDrawDistance(100.0f, true, 0.5f) == 100.0f);
    TEST_CHECK(run, WorldVisibility_ScaleDrawDistance(100.0f, true, 4.0f) == 100.0f);
    TEST_CHECK(run, WorldVisibility_ScaleDrawDistance(100.0f, false, 0.0f) == 0.0f);
    TEST_CHECK(run, WorldVisibility_ScaleDrawDistance(100.0f, false, NAN) == 100.0f);
    TEST_CHECK(run, WorldVisibility_ScaleDrawDistance(100.0f, false, -2.0f) == 100.0f);
    TEST_CHECK(run, isinf(WorldVisibility_ScaleDrawDistance(INFINITY, false, 2.0f)));

    TEST_CHECK(run, WorldVisibility_GetZoomFactor(60.0f) == 1.0f);
    TEST_CHECK(run, IsNear(WorldVisibility_GetZoomFactor(30.0f), tanf(15.0f * DEG2RAD) / tanf(30.0f * DEG2RAD)));
    TEST_CHECK(run, IsNear(WorldVisibility_GetZoomFactor(15.0f), tanf(7.5f * DEG2RAD) / tanf(30.0f * DEG2RAD)));
    TEST_CHECK(run, WorldVisibility_GetZoomFactor(90.0f) > 1.0f);
    float Previous = 0.0f;
    for (float Fov = 5.0f; Fov < 175.0f; Fov += 5.0f)
    {
        float Factor = WorldVisibility_GetZoomFactor(Fov);
        TEST_CHECK(run, (Factor > Previous) && isfinite(Factor));
        Previous = Factor;
    }
    TEST_CHECK(run, (WorldVisibility_GetZoomFactor(0.0f) == 1.0f) && (WorldVisibility_GetZoomFactor(180.0f) == 1.0f)
        && (WorldVisibility_GetZoomFactor(-30.0f) == 1.0f) && (WorldVisibility_GetZoomFactor(NAN) == 1.0f)
        && (WorldVisibility_GetZoomFactor(INFINITY) == 1.0f));
}

/* 0 before the band, rising to 1 at the limit, monotonic in between; the band is a tenth, at least 3 m, at most all. */
static void DistanceFadeRisesOverTheBand(TestRun* run)
{
    TEST_CHECK(run, IsNear(WorldVisibility_GetFadeBand(600.0f), 60.0f));
    TEST_CHECK(run, IsNear(WorldVisibility_GetFadeBand(20.0f), 3.0f));
    TEST_CHECK(run, IsNear(WorldVisibility_GetFadeBand(2.0f), 2.0f));
    TEST_CHECK(run, WorldVisibility_GetFadeBand(0.0f) == 0.0f);

    TEST_CHECK(run, WorldVisibility_GetDistanceFade(0.0f, 600.0f) == 0.0f);
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(540.0f, 600.0f) == 0.0f);
    TEST_CHECK(run, IsNear(WorldVisibility_GetDistanceFade(570.0f, 600.0f), 0.5f));
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(599.9f, 600.0f) < 1.0f);
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(600.0f, 600.0f) == 1.0f);
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(700.0f, 600.0f) == 1.0f);
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(17.0f, 20.0f) == 0.0f);
    TEST_CHECK(run, IsNear(WorldVisibility_GetDistanceFade(18.5f, 20.0f), 0.5f));
    TEST_CHECK(run, IsNear(WorldVisibility_GetDistanceFade(1.0f, 2.0f), 0.5f));

    float Previous = 0.0f;
    bool IsMonotonic = true;
    bool IsInRange = true;
    for (int Step = 0; Step <= 1400; Step++)
    {
        float Fade = WorldVisibility_GetDistanceFade((float)Step * 0.5f, 600.0f);
        IsMonotonic = IsMonotonic && (Fade >= Previous);
        IsInRange = IsInRange && (Fade >= 0.0f) && (Fade <= 1.0f);
        Previous = Fade;
    }
    TEST_CHECK(run, IsMonotonic && IsInRange);

    // Degenerate input: nothing within a draw distance of 0 or less, a NaN distance counts as beyond, an infinite draw
    // distance fades nothing.
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(0.0f, 0.0f) == 1.0f);
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(0.0f, -5.0f) == 1.0f);
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(NAN, 600.0f) == 1.0f);
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(10.0f, NAN) == 1.0f);
    TEST_CHECK(run, WorldVisibility_GetDistanceFade(1e30f, INFINITY) == 0.0f);

    // The distance is to the nearest point of the bounding sphere, 0 inside it.
    Vector3 CameraPosition = { 1.0f, 2.0f, 3.0f };
    TEST_CHECK(run, IsNear(WorldVisibility_GetEntryDistance(CameraPosition, (Vector3){ 11.0f, 2.0f, 3.0f }, 4.0f), 6.0f));
    TEST_CHECK(run, WorldVisibility_GetEntryDistance(CameraPosition, (Vector3){ 11.0f, 2.0f, 3.0f }, 20.0f) == 0.0f);
}

/* The opacity's fade is 1 - opacity (clamped; a NaN is transparent), and a draw takes the larger of its two fades. */
static void OpacityFadeAndTheMaxRule(TestRun* run)
{
    TEST_CHECK(run, WorldVisibility_GetOpacityFade(1.0f) == 0.0f);
    TEST_CHECK(run, IsNear(WorldVisibility_GetOpacityFade(0.5f), 0.5f));
    TEST_CHECK(run, WorldVisibility_GetOpacityFade(0.0f) == 1.0f);
    TEST_CHECK(run, WorldVisibility_GetOpacityFade(1.5f) == 0.0f);
    TEST_CHECK(run, WorldVisibility_GetOpacityFade(-0.5f) == 1.0f);
    TEST_CHECK(run, WorldVisibility_GetOpacityFade(NAN) == 1.0f);

    TEST_CHECK(run, IsNear(WorldVisibility_CombineFades(0.2f, 0.5f), 0.5f));
    TEST_CHECK(run, IsNear(WorldVisibility_CombineFades(0.7f, 0.5f), 0.7f));
    TEST_CHECK(run, WorldVisibility_CombineFades(1.0f, 0.0f) == 1.0f);
    TEST_CHECK(run, WorldVisibility_CombineFades(0.0f, 0.0f) == 0.0f);
}


// Tests: the dither.
/* The C mirror of shaders/common/dither.glsl: every 4x4 block holds each threshold (k + 0.5) / 16 once and repeats; with
 * either pattern a fade of k/16 keeps 16 - k pixels of a block; a draw at fade p and an inverted one at 1 - p (T11's two
 * LODs mid cross-fade) split every pixel between them; half a fade is a checkerboard; the crisp overlay's pixel scale
 * lands every window pixel in its scene pixel's cell. */
static void DitherMatchesTheShaderFormula(TestRun* run)
{
    bool IsSeen[WORLD_VISIBILITY_DITHER_LEVELS] = { false };
    bool AreThresholdsDistinct = true;
    bool IsPeriodic = true;
    for (uint32_t Y = 0U; Y < WORLD_VISIBILITY_DITHER_SIZE; Y++)
    {
        for (uint32_t X = 0U; X < WORLD_VISIBILITY_DITHER_SIZE; X++)
        {
            float Threshold = WorldVisibility_GetDitherThreshold(X, Y);
            float Level = Threshold * (float)WORLD_VISIBILITY_DITHER_LEVELS - 0.5f;
            uint32_t K = (uint32_t)lroundf(Level);
            AreThresholdsDistinct = AreThresholdsDistinct && IsNear(Level, (float)K)
                && (K < WORLD_VISIBILITY_DITHER_LEVELS) && !IsSeen[K];
            if (K < WORLD_VISIBILITY_DITHER_LEVELS)
            {
                IsSeen[K] = true;
            }
            IsPeriodic = IsPeriodic && (WorldVisibility_GetDitherThreshold(X + 4U, Y) == Threshold)
                && (WorldVisibility_GetDitherThreshold(X, Y + 8U) == Threshold);
        }
    }
    TEST_CHECK(run, AreThresholdsDistinct && IsPeriodic);

    // A fade of k/16 keeps 16 - k pixels of every block; a LOD cross-fade at progress k/16 gives the incoming LOD k of
    // them and the outgoing one the other 16 - k.
    bool AreLevelsExact = true;
    for (uint32_t Level = 0U; Level <= WORLD_VISIBILITY_DITHER_LEVELS; Level++)
    {
        float Share = (float)Level / (float)WORLD_VISIBILITY_DITHER_LEVELS;
        uint32_t Kept = 0U;
        uint32_t KeptByOutgoing = 0U;
        uint32_t KeptByIncoming = 0U;
        for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
        {
            Kept += WorldVisibility_IsDitherDiscarded(Share, 0.0f, false, Cell % 4U, Cell / 4U) ? 0U : 1U;
            KeptByOutgoing += WorldVisibility_IsDitherDiscarded(0.0f, Share, false, Cell % 4U, Cell / 4U) ? 0U : 1U;
            KeptByIncoming += WorldVisibility_IsDitherDiscarded(0.0f, Share, true, Cell % 4U, Cell / 4U) ? 0U : 1U;
        }
        AreLevelsExact = AreLevelsExact && (Kept == WORLD_VISIBILITY_DITHER_LEVELS - Level)
            && (KeptByOutgoing == WORLD_VISIBILITY_DITHER_LEVELS - Level) && (KeptByIncoming == Level);
    }
    TEST_CHECK(run, AreLevelsExact);

    // Progress values on, between and at the pattern's own thresholds ((k + 0.5) / 16, where a tie could double-draw):
    // the two LODs always keep complementary pixels (tests/WorldLodTests.c checks many more, with the fade too).
    const float Progresses[] = { 0.0f, 1.0f / 32.0f, 0.03f, 0.3f, 0.5f, 0.5019608f, 0.53125f, 0.77f, 0.999f, 1.0f };
    bool IsPartition = true;
    for (size_t Index = 0U; Index < (sizeof(Progresses) / sizeof(Progresses[0])); Index++)
    {
        float Progress = Progresses[Index];
        for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
        {
            bool IsDrawnByOutgoing = !WorldVisibility_IsDitherDiscarded(0.0f, Progress, false, Cell % 4U, Cell / 4U);
            bool IsDrawnByIncoming = !WorldVisibility_IsDitherDiscarded(0.0f, Progress, true, Cell % 4U, Cell / 4U);
            IsPartition = IsPartition && (IsDrawnByOutgoing != IsDrawnByIncoming);
        }
    }
    TEST_CHECK(run, IsPartition);
    // A draw outside a cross-fade (progress 0, the outgoing side) keeps everything at fade 0 and nothing at fade 1; at
    // progress 1 the incoming LOD keeps everything and the outgoing one nothing.
    TEST_CHECK(run, !WorldVisibility_IsDitherDiscarded(0.0f, 0.0f, false, 2U, 3U)
        && WorldVisibility_IsDitherDiscarded(1.0f, 0.0f, false, 2U, 3U));
    TEST_CHECK(run, !WorldVisibility_IsDitherDiscarded(0.0f, 1.0f, true, 2U, 3U)
        && WorldVisibility_IsDitherDiscarded(0.0f, 1.0f, false, 2U, 3U)
        && WorldVisibility_IsDitherDiscarded(0.0f, 0.0f, true, 2U, 3U));

    bool IsCheckerboard = true;
    for (uint32_t Cell = 0U; Cell < WORLD_VISIBILITY_DITHER_LEVELS; Cell++)
    {
        uint32_t X = Cell % 4U;
        uint32_t Y = Cell / 4U;
        bool IsEvenCell = ((X + Y) % 2U) == 0U;
        IsCheckerboard = IsCheckerboard && (WorldVisibility_IsDitherDiscarded(0.5f, 0.0f, false, X, Y) == IsEvenCell);
    }
    TEST_CHECK(run, IsCheckerboard);

    // Pixel centres sit at index + 0.5: at scale 1 a cell is a pixel; at a 1600-pixel window over a 640-pixel scene
    // (the crisp overlay, 2.5 window pixels per scene pixel) every window pixel lands in the scene pixel the upscale
    // shows there.
    TEST_CHECK(run, (WorldVisibility_GetDitherCell(0.5f, 1.0f) == 0U)
        && (WorldVisibility_GetDitherCell(7.5f, 1.0f) == 7U));
    TEST_CHECK(run, (WorldVisibility_GetDitherCell(2.5f, 3.0f) == 0U)
        && (WorldVisibility_GetDitherCell(3.5f, 3.0f) == 1U));
    TEST_CHECK(run, WorldVisibility_GetDitherCell(7.5f, 0.5f) == 7U); // below 1 counts as 1
    float CrispScale = 1600.0f / 640.0f;
    bool IsOnTheSceneGrid = true;
    for (uint32_t WindowPixel = 0U; WindowPixel < 1600U; WindowPixel++)
    {
        uint32_t ScenePixel = (uint32_t)floorf(((float)WindowPixel + 0.5f) * 640.0f / 1600.0f);
        IsOnTheSceneGrid = IsOnTheSceneGrid
            && (WorldVisibility_GetDitherCell((float)WindowPixel + 0.5f, CrispScale) == ScenePixel);
    }
    TEST_CHECK(run, IsOnTheSceneGrid);
}


// Tests: the lists.
/* The camera's list holds what is in view, in the world's order; what is outside it, or hidden, is not. */
static void CameraListHoldsWhatIsInView(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    Vector3 CameraPosition = { 0.0f, 1.0f, 0.0f };
    WorldModelComponent* Ahead = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, 10.0f });
    WorldModelComponent* Behind = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -10.0f });
    WorldModelComponent* Aside = AddBox(run, &Fixture.World, (Vector3){ 20.0f, 1.0f, 2.0f });
    WorldModelComponent* Hidden = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, 5.0f });
    WorldModelComponent* AlsoAhead = AddBox(run, &Fixture.World, (Vector3){ -1.0f, 1.5f, 12.0f });
    WorldVisibilityView View = MakeView(CameraPosition);
    if ((Ahead != NULL) && (Behind != NULL) && (Aside != NULL) && (Hidden != NULL) && (AlsoAhead != NULL)
        && TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOfModel(Hidden), false))
        && VisibilityFixture_Build(run, &Fixture, &View))
    {
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, Ahead));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, AlsoAhead));
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_Camera, Behind));
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_Camera, Aside));
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_Camera, Hidden));
        size_t Count = 0;
        const size_t* List = WorldVisibility_GetModels(&Fixture.Visibility, &Fixture.Cache, WorldVisibilityPass_Camera,
            &Count);
        TEST_CHECK(run, (Count == 2) && IsAscending(List, Count));

        WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&Fixture.Visibility);
        TEST_CHECK(run, (Counts.DrawnModelCount == 2) && (Counts.OutsideViewCount == 2) && (Counts.FadedOutCount == 0)
            && (Counts.FadingCount == 0) && (Counts.DrawnSpriteCount == 0));
        // A hidden model has nothing to draw: fully faded.
        TEST_CHECK(run, WorldVisibility_GetModelFade(&Fixture.Visibility, ModelIndex(&Fixture.Cache, Hidden)).Combined
            == 1.0f);
        TEST_CHECK(run, WorldVisibility_GetModelFade(&Fixture.Visibility, ModelIndex(&Fixture.Cache, Ahead)).Combined
            == 0.0f);
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* A caster off screen still shadows what is on it: culled from the camera's list, it stays in the shadow lists of the
 * cascades whose box holds it; a model that does not cast is in none; the shadow tier routes a model to one cascade;
 * the cube shadow takes the casters within its light's reach. */
static void CastersBehindTheCameraStayInTheShadowLists(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    Vector3 CameraPosition = { 0.0f, 1.0f, 0.0f };
    WorldModelComponent* Behind = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -10.0f });
    WorldModelComponent* NonCaster = AddBox(run, &Fixture.World, (Vector3){ 3.0f, 1.0f, -5.0f });
    WorldModelComponent* NearOnly = AddBox(run, &Fixture.World, (Vector3){ -1.0f, 1.0f, -3.0f });
    WorldModelComponent* FarOnly = AddBox(run, &Fixture.World, (Vector3){ 1.0f, 1.0f, -3.0f });
    WorldModelComponent* PastTheNearBox = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -25.0f });
    WorldVisibilityView View = MakeView(CameraPosition);
    View.IsPointShadowActive = true;
    View.PointShadowPosition = (Vector3){ 0.0f, 1.0f, -10.0f };
    View.PointShadowReach = 5.0f;
    if ((Behind != NULL) && (NonCaster != NULL) && (NearOnly != NULL) && (FarOnly != NULL) && (PastTheNearBox != NULL)
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(NonCaster, WorldModelSetting_CastsShadows, true))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetCastsShadows(NonCaster, false))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(NearOnly, WorldModelSetting_ShadowTier, true))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowTier(NearOnly, WorldShadowTier_Near))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(FarOnly, WorldModelSetting_ShadowTier, true))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowTier(FarOnly, WorldShadowTier_Far))
        && VisibilityFixture_Build(run, &Fixture, &View))
    {
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_Camera, Behind));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_NearCascade, Behind));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_FarCascade, Behind));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_PointShadow, Behind));

        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_NearCascade, NonCaster)
            && !HasModel(&Fixture, WorldVisibilityPass_FarCascade, NonCaster)
            && !HasModel(&Fixture, WorldVisibilityPass_PointShadow, NonCaster));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_NearCascade, NearOnly)
            && !HasModel(&Fixture, WorldVisibilityPass_FarCascade, NearOnly));
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_NearCascade, FarOnly)
            && HasModel(&Fixture, WorldVisibilityPass_FarCascade, FarOnly));
        // 7 m from the lamp, beyond its 5 m reach plus its own radius.
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_PointShadow, NearOnly));
        // 25 m behind: outside the 32 m box around the camera, inside the 160 m one.
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_NearCascade, PastTheNearBox)
            && HasModel(&Fixture, WorldVisibilityPass_FarCascade, PastTheNearBox));

        // Without the shadow passes running, their lists stay empty.
        View.IsNearCascadeActive = false;
        View.IsFarCascadeActive = false;
        View.IsPointShadowActive = false;
        size_t Count = 1;
        if (TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&Fixture.Visibility, &Fixture.Cache, &View)))
        {
            WorldVisibility_GetModels(&Fixture.Visibility, &Fixture.Cache, WorldVisibilityPass_NearCascade, &Count);
            TEST_CHECK(run, Count == 0);
            WorldVisibility_GetModels(&Fixture.Visibility, &Fixture.Cache, WorldVisibilityPass_FarCascade, &Count);
            TEST_CHECK(run, Count == 0);
            WorldVisibility_GetModels(&Fixture.Visibility, &Fixture.Cache, WorldVisibilityPass_PointShadow, &Count);
            TEST_CHECK(run, Count == 0);
        }
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* Beyond its draw distance an entry is in no list, its shadow included; in the band it fades; the opacity fades it and
 * at 0 removes it; the counts follow. */
static void DrawDistanceAndOpacityFadeAndCull(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    // TEST_SCALE: a 30 m draw distance for models, fading from 27 m (distances to the nearest point of the sphere).
    Vector3 CameraPosition = { 0.0f, 1.0f, 0.0f };
    WorldModelComponent* Beyond = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -40.0f });
    WorldModelComponent* InTheBand = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, 29.0f });
    WorldModelComponent* Transparent = AddBox(run, &Fixture.World, (Vector3){ 2.0f, 1.0f, 10.0f });
    WorldModelComponent* Half = AddBox(run, &Fixture.World, (Vector3){ -2.0f, 1.0f, 10.0f });
    WorldVisibilityView View = MakeView(CameraPosition);
    if ((Beyond != NULL) && (InTheBand != NULL) && (Transparent != NULL) && (Half != NULL)
        && SetOpacity(run, OwnerOfModel(Transparent), 0.0f) && SetOpacity(run, OwnerOfModel(Half), 0.5f)
        && VisibilityFixture_Build(run, &Fixture, &View))
    {
        // 40 m behind: past the draw distance, so not even in the far cascade, whose 160 m box holds it.
        for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
        {
            TEST_CHECK(run, !HasModel(&Fixture, (WorldVisibilityPass)Pass, Beyond));
            TEST_CHECK(run, !HasModel(&Fixture, (WorldVisibilityPass)Pass, Transparent));
        }
        WorldVisibilityFade BeyondFade = WorldVisibility_GetModelFade(&Fixture.Visibility,
            ModelIndex(&Fixture.Cache, Beyond));
        TEST_CHECK(run, (BeyondFade.Distance == 1.0f) && (BeyondFade.Combined == 1.0f));

        // 29 m ahead, 28.13 m to its sphere: (28.134 - 27) / 3 of the way through the band.
        WorldVisibilityFade BandFade = WorldVisibility_GetModelFade(&Fixture.Visibility,
            ModelIndex(&Fixture.Cache, InTheBand));
        float Expected = (29.0f - BOX_RADIUS - 27.0f) / 3.0f;
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, InTheBand));
        TEST_CHECK(run, IsNear(BandFade.Distance, Expected) && IsNear(BandFade.Combined, Expected));

        // Half opacity: a fade of 1 - 127/255 (the tint's alpha byte), dithered in every pass that draws it.
        WorldVisibilityFade HalfFade = WorldVisibility_GetModelFade(&Fixture.Visibility,
            ModelIndex(&Fixture.Cache, Half));
        TEST_CHECK(run, (HalfFade.Distance == 0.0f) && IsNear(HalfFade.Combined, 1.0f - 127.0f / 255.0f));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, Half)
            && HasModel(&Fixture, WorldVisibilityPass_NearCascade, Half));
        WorldVisibilityFade TransparentFade = WorldVisibility_GetModelFade(&Fixture.Visibility,
            ModelIndex(&Fixture.Cache, Transparent));
        TEST_CHECK(run, (TransparentFade.Distance == 0.0f) && (TransparentFade.Combined == 1.0f));

        WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&Fixture.Visibility);
        TEST_CHECK(run, (Counts.DrawnModelCount == 2) && (Counts.FadedOutCount == 2) && (Counts.FadingCount == 2)
            && (Counts.OutsideViewCount == 0));
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* Each model fades at its own draw distance: its world model's, or its placement's while that overrides it, times the
 * placement's multiplier; the quality scale applies unless the world model or the placement keeps it fixed; zooming in
 * shrinks the distances, so far models stay. */
static void ModelsFadeAtTheirOwnDrawDistance(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture)
        || !TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"near_box\", \"lods\": [{\"model\": "
            "\"box\", \"max_distance\": 200}]}")
        || !TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"landmark\", \"lods\": [{\"model\": "
            "\"box\", \"max_distance\": 200}], \"is_draw_distance_fixed\": true}"))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    // TEST_SCALE (0.05): near_box is seen 10 m (fading from 7 m), the fixed landmark 200 m. Every box stands 20 m ahead
    // (and up to 5 m aside), 19.1 to 19.8 m from the camera to its sphere.
    Vector3 CameraPosition = { 0.0f, 1.0f, 0.0f };
    WorldModelComponent* Near = AddWorldModel(run, &Fixture.World, "near_box", (Vector3){ -5.0f, 1.0f, 20.0f });
    WorldModelComponent* Landmark = AddWorldModel(run, &Fixture.World, "landmark", (Vector3){ -3.0f, 1.0f, 20.0f });
    WorldModelComponent* OwnDistance = AddWorldModel(run, &Fixture.World, "near_box", (Vector3){ -1.0f, 1.0f, 20.0f });
    WorldModelComponent* Multiplied = AddWorldModel(run, &Fixture.World, "near_box", (Vector3){ 1.0f, 1.0f, 20.0f });
    WorldModelComponent* Unfixed = AddWorldModel(run, &Fixture.World, "landmark", (Vector3){ 3.0f, 1.0f, 20.0f });
    WorldModelComponent* Fixed = AddWorldModel(run, &Fixture.World, "near_box", (Vector3){ 5.0f, 1.0f, 20.0f });
    bool IsBuilt = (Near != NULL) && (Landmark != NULL) && (OwnDistance != NULL) && (Multiplied != NULL)
        && (Unfixed != NULL) && (Fixed != NULL)
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(OwnDistance, WorldModelSetting_DrawDistance, true))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistance(OwnDistance, 1000.0f)) // 50 m scaled
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistanceMultiplier(Multiplied, 4.0f)) // 40 m scaled
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Unfixed, WorldModelSetting_IsDrawDistanceFixed,
            true)) // its own "not fixed": 10 m scaled
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Fixed, WorldModelSetting_IsDrawDistanceFixed,
            true))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsDrawDistanceFixed(Fixed, true)); // 200 m
    WorldVisibilityView View = MakeView(CameraPosition);
    if (IsBuilt && VisibilityFixture_Build(run, &Fixture, &View))
    {
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_Camera, Near));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, Landmark));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, OwnDistance));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, Multiplied));
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_Camera, Unfixed));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, Fixed));
        TEST_CHECK(run, WorldVisibility_GetModelFade(&Fixture.Visibility, ModelIndex(&Fixture.Cache, Near)).Distance
            == 1.0f);
        TEST_CHECK(run, WorldVisibility_GetModelFade(&Fixture.Visibility, ModelIndex(&Fixture.Cache, Landmark)).Distance
            == 0.0f);
    }

    // Zoomed in 4 times: 19.8 m counts as 4.9 m, inside even the 10 m distances.
    View.ZoomFactor = 0.25f;
    if (IsBuilt && VisibilityFixture_Build(run, &Fixture, &View))
    {
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, Near));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, Unfixed));
        TEST_CHECK(run, WorldVisibility_GetModelFade(&Fixture.Visibility, ModelIndex(&Fixture.Cache, Near)).Distance
            == 0.0f);
        // A zoom of 0.45 puts near_box (5 m aside) 8.9 m away: in the fade band (7 to 10 m).
        View.ZoomFactor = 0.45f;
        if (VisibilityFixture_Build(run, &Fixture, &View))
        {
            float Expected = (((sqrtf(25.0f + 400.0f) - BOX_RADIUS) * 0.45f) - 7.0f) / 3.0f;
            TEST_CHECK(run, IsNear(WorldVisibility_GetModelFade(&Fixture.Visibility,
                ModelIndex(&Fixture.Cache, Near)).Distance, Expected));
        }
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* Sprites cull and fade like models; without a draw distance of their own (in their world sprites or their placements)
 * by their kind's (decals the shortest), around a sphere that holds their quad; only cutout sprites that are not decals
 * cast shadows. */
static void SpritesCullAndFadeByTheirKind(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    // TEST_SCALE: sprites 7.5 m (fading from 4.5 m), decals 3 m (fading from 0); a 2 m sprite's radius is sqrt(2).
    Vector3 CameraPosition = { 0.0f, 1.0f, 0.0f };
    WorldSpriteComponent* Ahead = AddSprite(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, 5.0f }, "cutout");
    WorldSpriteComponent* Behind = AddSprite(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -5.0f }, "cutout");
    WorldSpriteComponent* Fading = AddSprite(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, 7.0f }, "cutout");
    WorldSpriteComponent* Decal = AddSprite(run, &Fixture.World, (Vector3){ 0.0f, 0.01f, 2.0f }, "decal");
    WorldSpriteComponent* Smoke = AddSprite(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -3.0f }, "smoke");
    WorldSpriteComponent* Beyond = AddSprite(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, 20.0f }, "cutout");
    WorldVisibilityView View = MakeView(CameraPosition);
    float Radius = SPRITE_RADIUS;
    if ((Ahead != NULL) && (Behind != NULL) && (Fading != NULL) && (Decal != NULL) && (Smoke != NULL)
        && (Beyond != NULL) && SetOpacity(run, OwnerOfSprite(Smoke), 0.5f)
        && VisibilityFixture_Build(run, &Fixture, &View))
    {
        TEST_CHECK(run, HasSprite(&Fixture, WorldVisibilityPass_Camera, Ahead)
            && HasSprite(&Fixture, WorldVisibilityPass_NearCascade, Ahead));
        TEST_CHECK(run, !HasSprite(&Fixture, WorldVisibilityPass_Camera, Behind)
            && HasSprite(&Fixture, WorldVisibilityPass_NearCascade, Behind)
            && HasSprite(&Fixture, WorldVisibilityPass_FarCascade, Behind));
        WorldVisibilityFade FadingFade = WorldVisibility_GetSpriteFade(&Fixture.Visibility,
            SpriteIndex(&Fixture.Cache, Fading));
        TEST_CHECK(run, HasSprite(&Fixture, WorldVisibilityPass_Camera, Fading)
            && IsNear(FadingFade.Combined, (7.0f - Radius - 4.5f) / 3.0f));

        // The decal fades over its whole 3 m (its band is all of it), and casts no shadow.
        Vector3 DecalOffset = { 0.0f, 0.99f, 2.0f };
        WorldVisibilityFade DecalFade = WorldVisibility_GetSpriteFade(&Fixture.Visibility,
            SpriteIndex(&Fixture.Cache, Decal));
        TEST_CHECK(run, HasSprite(&Fixture, WorldVisibilityPass_Camera, Decal)
            && IsNear(DecalFade.Combined, (Vector3Length(DecalOffset) - Radius) / 3.0f));
        TEST_CHECK(run, !HasSprite(&Fixture, WorldVisibilityPass_NearCascade, Decal));
        // A blended sprite casts no shadow either; its opacity is its combined fade (the renderer draws it as alpha).
        TEST_CHECK(run, !HasSprite(&Fixture, WorldVisibilityPass_NearCascade, Smoke)
            && !HasSprite(&Fixture, WorldVisibilityPass_Camera, Smoke));
        WorldVisibilityFade SmokeFade = WorldVisibility_GetSpriteFade(&Fixture.Visibility,
            SpriteIndex(&Fixture.Cache, Smoke));
        TEST_CHECK(run, (SmokeFade.Distance == 0.0f) && IsNear(SmokeFade.Combined, 1.0f - 127.0f / 255.0f));
        for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
        {
            TEST_CHECK(run, !HasSprite(&Fixture, (WorldVisibilityPass)Pass, Beyond));
        }

        WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&Fixture.Visibility);
        TEST_CHECK(run, (Counts.DrawnSpriteCount == 3) && (Counts.OutsideViewCount == 2) && (Counts.FadedOutCount == 1)
            && (Counts.FadingCount == 2));
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* A sprite fades at its own draw distance, with the models' rules (G17): its world sprite's, or the placement's own while
 * it overrides it, times the placement's multiplier, then the quality scale unless it is fixed, against its distance
 * times the zoom factor. */
static void SpritesFadeAtTheirOwnDrawDistance(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture)
        || !TestAssets_AddWorldSprite(run, &Fixture.Assets, "{\"name\": \"sign\", \"sprite_animation\": "
            "\"walk_animation\", \"pixels_per_unit\": 0.5, \"draw_distance\": 12}")
        || !TestAssets_AddWorldSprite(run, &Fixture.Assets, "{\"name\": \"landmark\", \"sprite_animation\": "
            "\"walk_animation\", \"pixels_per_unit\": 0.5, \"draw_distance\": 12, \"is_draw_distance_fixed\": true}"))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    // All 11 m ahead, so their spheres' nearest points are 11 - sqrt(2) away; 12 m fades from 9 m (a 3 m band).
    Vector3 Ahead = { 0.0f, 1.0f, 11.0f };
    float Distance = 11.0f - SPRITE_RADIUS;
    WorldSpriteComponent* Sign = AddSprite(run, &Fixture.World, Ahead, "sign");
    WorldSpriteComponent* Farther = AddSprite(run, &Fixture.World, Ahead, "sign");
    WorldSpriteComponent* Shorter = AddSprite(run, &Fixture.World, Ahead, "sign");
    WorldSpriteComponent* Landmark = AddSprite(run, &Fixture.World, Ahead, "landmark");
    WorldVisibilityView View = MakeView((Vector3){ 0.0f, 1.0f, 0.0f });
    View.DrawDistanceScale = 1.0f;
    if ((Sign == NULL) || (Farther == NULL) || (Shorter == NULL) || (Landmark == NULL)
        || !TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Farther, WorldSpriteSetting_DrawDistance, true))
        || !TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistance(Farther, 30.0f))
        || !TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistanceMultiplier(Shorter, 0.5f))
        || !VisibilityFixture_Build(run, &Fixture, &View))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }

    float SignFade = WorldVisibility_GetSpriteFade(&Fixture.Visibility, SpriteIndex(&Fixture.Cache, Sign)).Combined;
    TEST_CHECK(run, IsNear(SignFade, (Distance - 9.0f) / 3.0f) && HasSprite(&Fixture, WorldVisibilityPass_Camera, Sign));
    TEST_CHECK(run, WorldVisibility_GetSpriteFade(&Fixture.Visibility, SpriteIndex(&Fixture.Cache, Farther)).Combined
        == 0.0f);
    TEST_CHECK(run, !HasSprite(&Fixture, WorldVisibilityPass_Camera, Shorter)); // 6 m
    TEST_CHECK(run, IsNear(WorldVisibility_GetSpriteFade(&Fixture.Visibility, SpriteIndex(&Fixture.Cache,
        Landmark)).Combined, SignFade));

    // Half the quality: 6 m for the sign, its own distance for the fixed landmark, 15 m for the overriding one.
    View.DrawDistanceScale = 0.5f;
    if (TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&Fixture.Visibility, &Fixture.Cache, &View)))
    {
        TEST_CHECK(run, !HasSprite(&Fixture, WorldVisibilityPass_Camera, Sign)
            && HasSprite(&Fixture, WorldVisibilityPass_Camera, Landmark)
            && HasSprite(&Fixture, WorldVisibilityPass_Camera, Farther));
        TEST_CHECK(run, IsNear(WorldVisibility_GetSpriteFade(&Fixture.Visibility, SpriteIndex(&Fixture.Cache,
            Landmark)).Combined, SignFade));
    }

    // Zoomed in twice: the 6 m sprite is seen again, at half its distance (fading from 3 m).
    View.DrawDistanceScale = 1.0f;
    View.ZoomFactor = 0.5f;
    if (TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&Fixture.Visibility, &Fixture.Cache, &View)))
    {
        TEST_CHECK(run, HasSprite(&Fixture, WorldVisibilityPass_Camera, Shorter)
            && IsNear(WorldVisibility_GetSpriteFade(&Fixture.Visibility, SpriteIndex(&Fixture.Cache, Shorter)).Combined,
                ((Distance * 0.5f) - 3.0f) / 3.0f));
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* With culling off (the A/B toggle) the camera and the cascades keep everything within its draw distance; the draw
 * distances and the opacity still apply. */
static void CullingOffKeepsEverythingWithinReach(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    Vector3 CameraPosition = { 0.0f, 1.0f, 0.0f };
    WorldModelComponent* Behind = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -10.0f });
    WorldModelComponent* FarAside = AddBox(run, &Fixture.World, (Vector3){ 25.0f, 1.0f, 0.0f });
    WorldModelComponent* Beyond = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -40.0f });
    WorldVisibilityView View = MakeView(CameraPosition);
    View.IsCullingEnabled = false;
    if ((Behind != NULL) && (FarAside != NULL) && (Beyond != NULL) && VisibilityFixture_Build(run, &Fixture, &View))
    {
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, Behind));
        TEST_CHECK(run, HasModel(&Fixture, WorldVisibilityPass_Camera, FarAside)
            && HasModel(&Fixture, WorldVisibilityPass_NearCascade, FarAside));
        TEST_CHECK(run, !HasModel(&Fixture, WorldVisibilityPass_Camera, Beyond)
            && !HasModel(&Fixture, WorldVisibilityPass_FarCascade, Beyond));
        WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&Fixture.Visibility);
        TEST_CHECK(run, (Counts.DrawnModelCount == 2) && (Counts.OutsideViewCount == 0) && (Counts.FadedOutCount == 1));
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* The crisp overlay runs only for crisp entries the camera sees. */
static void CrispObjectsCountOnlyInView(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    WorldModelComponent* Screen = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, -6.0f });
    WorldVisibilityView LookingAway = MakeView((Vector3){ 0.0f, 1.0f, 0.0f });
    WorldVisibilityView LookingAt = MakeView((Vector3){ 0.0f, 1.0f, -12.0f });
    if ((Screen != NULL)
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Screen, WorldModelSetting_OmitPixelation, true))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOmitsPixelation(Screen, true))
        && VisibilityFixture_Build(run, &Fixture, &LookingAway))
    {
        TEST_CHECK(run, !WorldVisibility_HasCrispObjects(&Fixture.Visibility));
        if (TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&Fixture.Visibility, &Fixture.Cache, &LookingAt)))
        {
            TEST_CHECK(run, WorldVisibility_HasCrispObjects(&Fixture.Visibility));
        }
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* The counts give the drawn models by the LOD they show (or head to: a fade that turned back counts under the LOD it
 * returns to) and how many cross-fade: models outside the view, hidden or faded out are not counted. */
static void DrawnModelsAreCountedByLod(TestRun* run)
{
    VisibilityFixture Fixture;
    BoundingBox UnitCube = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    if (!VisibilityFixture_Construct(run, &Fixture)
        || !TestAssets_AddModel(run, &Fixture.Assets, "box_lod1", UnitCube, NULL)
        || !TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"stepped\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 10}, {\"model\": \"box_lod1\", \"max_distance\": 30}], \"lod_hysteresis\": 0}"))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    Vector3 CameraPosition = { 0.0f, 1.0f, 0.0f };
    WorldModelComponent* Near = AddWorldModel(run, &Fixture.World, "stepped", (Vector3){ 0.0f, 1.0f, 5.0f });
    WorldModelComponent* AlsoNear = AddWorldModel(run, &Fixture.World, "stepped", (Vector3){ 1.0f, 1.0f, 6.0f });
    WorldModelComponent* Far = AddWorldModel(run, &Fixture.World, "stepped", (Vector3){ 0.0f, 1.0f, 20.0f });
    WorldModelComponent* Behind = AddWorldModel(run, &Fixture.World, "stepped", (Vector3){ 0.0f, 1.0f, -20.0f });
    WorldVisibilityView View = MakeView(CameraPosition);
    View.DrawDistanceScale = 1.0f;
    WorldRenderLodView LodView = { .CameraPosition = CameraPosition, .ZoomFactor = 1.0f, .DistanceScale = 1.0f,
        .DeltaSeconds = 0.1f };
    if ((Near != NULL) && (AlsoNear != NULL) && (Far != NULL) && (Behind != NULL)
        && TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Fixture.Cache, &Fixture.World, Vector3Double_Zero(),
            Fixture.Assets.Manager, Fixture.ModelUser, NULL)))
    {
        WorldRenderCache_UpdateLods(&Fixture.Cache, &LodView, NULL);
        if (TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&Fixture.Visibility, &Fixture.Cache, &View)))
        {
            WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&Fixture.Visibility);
            TEST_CHECK(run, (Counts.DrawnModelCount == 3U) && (Counts.DrawnModelsByLod[0] == 2U)
                && (Counts.DrawnModelsByLod[1] == 1U) && (Counts.DrawnModelsByLod[2] == 0U)
                && (Counts.LodFadingCount == 0U));
        }
        // The camera steps back 8 m: the near ones cross the 10 m switch and cross-fade to LOD 1.
        LodView.CameraPosition.z = -8.0f;
        View = MakeView(LodView.CameraPosition);
        View.DrawDistanceScale = 1.0f;
        WorldRenderCache_UpdateLods(&Fixture.Cache, &LodView, NULL);
        if (TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&Fixture.Visibility, &Fixture.Cache, &View)))
        {
            WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&Fixture.Visibility);
            TEST_CHECK(run, (Counts.DrawnModelCount == 3U) && (Counts.DrawnModelsByLod[0] == 0U)
                && (Counts.DrawnModelsByLod[1] == 3U) && (Counts.LodFadingCount == 2U));
        }
        // Back again within the fade (half a frame's worth): the near ones turn around and count under LOD 0 again.
        LodView.CameraPosition.z = 0.0f;
        LodView.DeltaSeconds = 0.05f;
        View = MakeView(LodView.CameraPosition);
        View.DrawDistanceScale = 1.0f;
        WorldRenderCache_UpdateLods(&Fixture.Cache, &LodView, NULL);
        if (TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&Fixture.Visibility, &Fixture.Cache, &View)))
        {
            WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&Fixture.Visibility);
            TEST_CHECK(run, (Counts.DrawnModelCount == 3U) && (Counts.DrawnModelsByLod[0] == 2U)
                && (Counts.DrawnModelsByLod[1] == 1U) && (Counts.LodFadingCount == 2U));
        }
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* What one render-space build gives, for comparing two: every pass's lists, every entry's fade, every model's LOD. */
typedef struct ShiftSnapshotStruct
{
    size_t ModelCounts[WorldVisibilityPass_Count];
    size_t Models[WorldVisibilityPass_Count][SHIFT_MAX_ENTRIES];
    size_t SpriteCounts[WorldVisibilityPass_Count];
    size_t Sprites[WorldVisibilityPass_Count][SHIFT_MAX_ENTRIES];
    size_t ModelCount;
    size_t SpriteCount;
    float ModelFades[SHIFT_MAX_ENTRIES];
    float SpriteFades[SHIFT_MAX_ENTRIES];
    WorldLodState Lods[SHIFT_MAX_ENTRIES];
} ShiftSnapshot;

/* One frame of the render-space test from scratch: the cache synced for the camera at @p origin, the LODs chosen
 * afresh, the lists built for the camera at render space's origin; then the results into @p outSnapshot. */
static bool BuildInRenderSpace(TestRun* run, VisibilityFixture* fixture, Vector3Double origin,
    ShiftSnapshot* outSnapshot)
{
    Memory_Zero(outSnapshot, sizeof(*outSnapshot));
    WorldRenderCache_Reset(&fixture->Cache);
    WorldVisibilityView View = MakeView((Vector3){ 0.0f, 0.0f, 0.0f });
    WorldRenderLodView LodView = { .CameraPosition = { 0.0f, 0.0f, 0.0f }, .ZoomFactor = 1.0f, .DistanceScale = 1.0f,
        .DeltaSeconds = 0.0f };
    if (!TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&fixture->Cache, &fixture->World, origin,
        fixture->Assets.Manager, fixture->ModelUser, NULL)))
    {
        return false;
    }
    WorldRenderCache_UpdateLods(&fixture->Cache, &LodView, NULL);
    if (!TEST_CHECK_SUCCESS(run, WorldVisibility_Build(&fixture->Visibility, &fixture->Cache, &View)))
    {
        return false;
    }
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&fixture->Cache, &outSnapshot->ModelCount);
    WorldRenderCache_GetSprites(&fixture->Cache, &outSnapshot->SpriteCount);
    if (!TEST_CHECK(run, (outSnapshot->ModelCount <= SHIFT_MAX_ENTRIES) && (outSnapshot->SpriteCount <= SHIFT_MAX_ENTRIES)))
    {
        return false;
    }
    for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
    {
        const size_t* List = WorldVisibility_GetModels(&fixture->Visibility, &fixture->Cache, (WorldVisibilityPass)Pass,
            &outSnapshot->ModelCounts[Pass]);
        Memory_Copy(List, outSnapshot->Models[Pass], outSnapshot->ModelCounts[Pass] * sizeof(size_t));
        List = WorldVisibility_GetSprites(&fixture->Visibility, &fixture->Cache, (WorldVisibilityPass)Pass,
            &outSnapshot->SpriteCounts[Pass]);
        Memory_Copy(List, outSnapshot->Sprites[Pass], outSnapshot->SpriteCounts[Pass] * sizeof(size_t));
    }
    for (size_t Index = 0U; Index < outSnapshot->ModelCount; Index++)
    {
        outSnapshot->ModelFades[Index] = WorldVisibility_GetModelFade(&fixture->Visibility, Index).Combined;
        outSnapshot->Lods[Index] = Models[Index].Lod;
    }
    for (size_t Index = 0U; Index < outSnapshot->SpriteCount; Index++)
    {
        outSnapshot->SpriteFades[Index] = WorldVisibility_GetSpriteFade(&fixture->Visibility, Index).Combined;
    }
    return true;
}

/* Whether two builds drew the same: the same lists and LODs, fades within the rounding of the shifted floats. */
static bool AreSnapshotsAlike(const ShiftSnapshot* a, const ShiftSnapshot* b)
{
    bool IsAlike = (a->ModelCount == b->ModelCount) && (a->SpriteCount == b->SpriteCount)
        && (memcmp(a->ModelCounts, b->ModelCounts, sizeof(a->ModelCounts)) == 0)
        && (memcmp(a->Models, b->Models, sizeof(a->Models)) == 0)
        && (memcmp(a->SpriteCounts, b->SpriteCounts, sizeof(a->SpriteCounts)) == 0)
        && (memcmp(a->Sprites, b->Sprites, sizeof(a->Sprites)) == 0);
    for (size_t Index = 0U; IsAlike && (Index < a->ModelCount); Index++)
    {
        IsAlike = (fabsf(a->ModelFades[Index] - b->ModelFades[Index]) < SHIFT_FADE_TOLERANCE)
            && (a->Lods[Index].CurrentLod == b->Lods[Index].CurrentLod)
            && (a->Lods[Index].TargetLod == b->Lods[Index].TargetLod);
    }
    for (size_t Index = 0U; IsAlike && (Index < a->SpriteCount); Index++)
    {
        IsAlike = fabsf(a->SpriteFades[Index] - b->SpriteFades[Index]) < SHIFT_FADE_TOLERANCE;
    }
    return IsAlike;
}

/* Render space (T32): a scene's lists, fades and LOD choices are the same with the camera at its place near the origin
 * and with the scene and the camera shifted together 20 km out (where the world's floats round to 2 mm): the cache syncs
 * for the camera's position, and the view and the cascades are in render space, around the camera at (0, 0, 0). A scene
 * left behind (the camera alone moved) is drawn nowhere. */
static void ListsAndLodsHoldTwentyKilometresOut(TestRun* run)
{
    VisibilityFixture Fixture;
    BoundingBox UnitCube = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    if (!VisibilityFixture_Construct(run, &Fixture)
        || !TestAssets_AddModel(run, &Fixture.Assets, "box_lod1", UnitCube, NULL)
        || !TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"stepped\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 10}, {\"model\": \"box_lod1\", \"max_distance\": 30}], \"lod_hysteresis\": 0, "
            "\"draw_distance\": 600}"))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    // A scene in front of a camera at (0.3, 1.6, -0.7) looking along +Z, at the list tests' quality scale (models drawn
    // to 30 m, sprites to 7.5 m, decals to 3 m): LOD models near and past their switch, boxes in view, beside it, behind
    // it, in their fade band and beyond it, the three sprites near and fading, and a box on a parent.
    Vector3Double CameraPosition = Vector3Double_Create(0.3, 1.6, -0.7);
    const Vector3 Stepped[] = { { 0.1f, 1.7f, 4.3f }, { 1.2f, 1.4f, 11.9f }, { -0.6f, 1.9f, 24.1f } };
    const Vector3 Boxes[] = { { 0.4f, 1.3f, 7.7f }, { 20.3f, 1.6f, 9.3f }, { 0.3f, 1.6f, -10.7f }, { 0.2f, 1.5f, 27.8f },
        { 0.3f, 1.6f, 40.1f } };
    bool IsBuilt = true;
    for (size_t Index = 0U; Index < (sizeof(Stepped) / sizeof(Stepped[0])); Index++)
    {
        IsBuilt = IsBuilt && (AddWorldModel(run, &Fixture.World, "stepped", Stepped[Index]) != NULL);
    }
    for (size_t Index = 0U; Index < (sizeof(Boxes) / sizeof(Boxes[0])); Index++)
    {
        IsBuilt = IsBuilt && (AddBox(run, &Fixture.World, Boxes[Index]) != NULL);
    }
    IsBuilt = IsBuilt && (AddSprite(run, &Fixture.World, (Vector3){ 1.3f, 1.1f, 3.3f }, "cutout") != NULL)
        && (AddSprite(run, &Fixture.World, (Vector3){ -0.7f, 2.1f, 5.3f }, "smoke") != NULL)
        && (AddSprite(run, &Fixture.World, (Vector3){ 0.3f, 0.01f, 2.3f }, "decal") != NULL)
        && (AddSprite(run, &Fixture.World, (Vector3){ 0.3f, 1.6f, 6.6f }, "cutout") != NULL);
    WorldObject* Parent = NULL;
    WorldModelComponent* Child = AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, 0.0f });
    IsBuilt = IsBuilt && (Child != NULL) && TEST_CHECK_SUCCESS(run, World_CreateObject(&Fixture.World, NULL, &Parent))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Parent, Vector3Double_Create(2.3, 0.3, 5.9)))
        && TEST_CHECK_SUCCESS(run, World_SetParent(&Fixture.World, WorldObject_GetId(OwnerOfModel(Child)),
            WorldObject_GetId(Parent), false));

    ShiftSnapshot AtTheOrigin;
    ShiftSnapshot Shifted;
    ShiftSnapshot LeftBehind;
    Vector3Double Shift = Vector3Double_Create(SHIFT_DISTANCE, 0.0, SHIFT_DISTANCE);
    if (IsBuilt && BuildInRenderSpace(run, &Fixture, CameraPosition, &AtTheOrigin)
        && BuildInRenderSpace(run, &Fixture, Vector3Double_Add(CameraPosition, Shift), &LeftBehind))
    {
        // The scene: drawn, with LOD 1 in use and entries fading, and none of it drawn by a camera that moved away
        // alone.
        TEST_CHECK(run, (AtTheOrigin.ModelCounts[WorldVisibilityPass_Camera] >= 5U)
            && (AtTheOrigin.SpriteCounts[WorldVisibilityPass_Camera] == 4U));
        bool HasLodOne = false;
        bool HasFadingModel = false;
        bool HasFadingSprite = false;
        for (size_t Index = 0U; Index < AtTheOrigin.ModelCount; Index++)
        {
            HasLodOne = HasLodOne || (AtTheOrigin.Lods[Index].IsStarted && (AtTheOrigin.Lods[Index].TargetLod == 1U));
            HasFadingModel = HasFadingModel
                || ((AtTheOrigin.ModelFades[Index] > 0.0f) && (AtTheOrigin.ModelFades[Index] < 1.0f));
        }
        for (size_t Index = 0U; Index < AtTheOrigin.SpriteCount; Index++)
        {
            HasFadingSprite = HasFadingSprite
                || ((AtTheOrigin.SpriteFades[Index] > 0.0f) && (AtTheOrigin.SpriteFades[Index] < 1.0f));
        }
        TEST_CHECK(run, HasLodOne && HasFadingModel && HasFadingSprite);
        TEST_CHECK(run, (LeftBehind.ModelCounts[WorldVisibilityPass_Camera] == 0U)
            && (LeftBehind.SpriteCounts[WorldVisibilityPass_Camera] == 0U));

        // Every root moved 20 km (in double, exactly), the camera with them.
        size_t ObjectCount = World_GetObjectCount(&Fixture.World);
        for (size_t Index = 0U; Index < ObjectCount; Index++)
        {
            WorldObject* Object = NULL;
            if (TEST_CHECK_SUCCESS(run, World_GetObjectByIndex(&Fixture.World, Index, &Object))
                && (WorldObject_GetParent(Object) == NULL))
            {
                Vector3Double Position = WorldObject_GetPosition(Object);
                TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, Vector3Double_Create(Position.X + Shift.X,
                    Position.Y, Position.Z + Shift.Z)));
            }
        }
        if (BuildInRenderSpace(run, &Fixture, Vector3Double_Add(CameraPosition, Shift), &Shifted))
        {
            TEST_CHECK(run, AreSnapshotsAlike(&AtTheOrigin, &Shifted));
        }
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}

/* A list is read against the cache it was built from: against a cache of another size it is empty, so no index can run
 * past the cache; an index the build did not cover reads as fully faded. */
static void ListsFollowTheirCache(TestRun* run)
{
    VisibilityFixture Fixture;
    if (!VisibilityFixture_Construct(run, &Fixture))
    {
        VisibilityFixture_Deconstruct(run, &Fixture);
        return;
    }
    WorldVisibilityView View = MakeView((Vector3){ 0.0f, 1.0f, 0.0f });
    if ((AddBox(run, &Fixture.World, (Vector3){ 0.0f, 1.0f, 10.0f }) != NULL)
        && VisibilityFixture_Build(run, &Fixture, &View))
    {
        WorldRenderCache Empty;
        WorldRenderCache_Construct(&Empty);
        size_t Count = 1;
        TEST_CHECK(run, (WorldVisibility_GetModels(&Fixture.Visibility, &Empty, WorldVisibilityPass_Camera, &Count)
            == NULL) && (Count == 0));
        WorldRenderCache_Deconstruct(&Empty);
        TEST_CHECK(run, (WorldVisibility_GetModels(&Fixture.Visibility, &Fixture.Cache, WorldVisibilityPass_Camera,
            &Count) != NULL) && (Count == 1));
        TEST_CHECK(run, (WorldVisibility_GetModels(&Fixture.Visibility, &Fixture.Cache, WorldVisibilityPass_Count,
            &Count) == NULL) && (Count == 0));
        TEST_CHECK(run, WorldVisibility_GetModelFade(&Fixture.Visibility, 5).Combined == 1.0f);
        TEST_CHECK(run, WorldVisibility_GetSpriteFade(&Fixture.Visibility, 0).Combined == 1.0f);
        TEST_CHECK_ERROR(run, WorldVisibility_Build(&Fixture.Visibility, NULL, &View), ErrorCode_IllegalArgument);
    }
    VisibilityFixture_Deconstruct(run, &Fixture);
}


// Tests: terrain chunks (B01).
/* Places the terrain @p name at @p position; returns its object (NULL with a failed check). */
static WorldObject* AddTerrainObject(TestRun* run, World* world, const char* name, Vector3 position)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, NULL, &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, GameMath_FromVector3(position)))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_Terrain,
            &Component))
        || !TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetTerrain(WorldTerrainComponent_FromComponent(Component),
            (const unsigned char*)name)))
    {
        return NULL;
    }
    return Object;
}

/* Whether a pass's chunk list holds the (one) chunk of @p object's terrain. */
static bool HasChunkOf(const VisibilityFixture* fixture, WorldVisibilityPass pass, const WorldObject* object)
{
    size_t ChunkCount = 0U;
    const WorldRenderTerrainChunk* Chunks = WorldRenderCache_GetTerrainChunks(&fixture->Cache, &ChunkCount);
    size_t Count = 0U;
    const size_t* List = WorldVisibility_GetTerrainChunks(&fixture->Visibility, &fixture->Cache, pass, &Count);
    for (size_t Position = 0U; Position < Count; Position++)
    {
        if (Chunks[List[Position]].ObjectId == WorldObject_GetId(object))
        {
            return true;
        }
    }
    return false;
}

/* Terrain chunks join the passes as models do: the camera's list in view, the cascades by their shadow tier, none
 * without casting, none beyond their own draw distance (fixed by default: the quality setting leaves it) or with their
 * object fully transparent; they are counted on their own, with their triangles. */
static void TerrainChunksJoinThePassesAsModelsDo(TestRun* run)
{
    VisibilityFixture Fixture;
    TestTerrainGrid Grid;
    // A 2 m patch of 1 m quads from its object's origin: one 8 m chunk of eight triangles.
    TestTerrain_BuildGrid(&Grid, 2U, 2U, 1.0f, 0.0f, 0.0f, NULL);
    TerrainMesh* Ground = NULL;
    bool IsReady = VisibilityFixture_Construct(run, &Fixture)
        && ((Ground = TestTerrain_CreateMesh(run, &Grid, 1U, 0U, 8.0f)) != NULL)
        && TestAssets_AddTerrain(run, &Fixture.Assets, "patch", Ground, NULL, NULL);
    WorldObject* InFront = IsReady ? AddTerrainObject(run, &Fixture.World, "patch", (Vector3){ 0.0f, 0.0f, 10.0f }) : NULL;
    WorldObject* Distant = IsReady ? AddTerrainObject(run, &Fixture.World, "patch", (Vector3){ 0.0f, 0.0f, 300.0f })
        : NULL;
    WorldObject* Behind = IsReady ? AddTerrainObject(run, &Fixture.World, "patch", (Vector3){ 0.0f, 0.0f, -10.0f })
        : NULL;
    WorldObject* NearOnly = IsReady ? AddTerrainObject(run, &Fixture.World, "patch", (Vector3){ -1.0f, 0.0f, -3.0f })
        : NULL;
    WorldObject* NonCaster = IsReady ? AddTerrainObject(run, &Fixture.World, "patch", (Vector3){ 3.0f, 0.0f, -5.0f })
        : NULL;
    WorldObject* Beyond = IsReady ? AddTerrainObject(run, &Fixture.World, "patch", (Vector3){ 0.0f, 0.0f, 4100.0f })
        : NULL;
    WorldObject* Clear = IsReady ? AddTerrainObject(run, &Fixture.World, "patch", (Vector3){ 2.0f, 0.0f, 12.0f }) : NULL;
    WorldVisibilityView View = MakeView((Vector3){ 0.0f, 1.0f, 0.0f });
    if ((InFront != NULL) && (Distant != NULL) && (Behind != NULL) && (NearOnly != NULL) && (NonCaster != NULL)
        && (Beyond != NULL) && (Clear != NULL) && SetOpacity(run, Clear, 0.0f))
    {
        WorldTerrainComponent* Near = WorldTerrainComponent_FromComponent(WorldObject_GetComponent(NearOnly,
            WorldComponentType_Terrain));
        WorldTerrainComponent* Unshadowed = WorldTerrainComponent_FromComponent(WorldObject_GetComponent(NonCaster,
            WorldComponentType_Terrain));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Near, WorldTerrainSetting_ShadowTier, true));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetShadowTier(Near, WorldShadowTier_Near));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Unshadowed, WorldTerrainSetting_CastsShadows, true));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetCastsShadows(Unshadowed, false));
        if (VisibilityFixture_Build(run, &Fixture, &View))
        {
            TEST_CHECK(run, HasChunkOf(&Fixture, WorldVisibilityPass_Camera, InFront)
                && HasChunkOf(&Fixture, WorldVisibilityPass_NearCascade, InFront)
                && HasChunkOf(&Fixture, WorldVisibilityPass_FarCascade, InFront));
            // 300 m out: past the models' 4 km scaled by the quality setting (200 m), drawn as its distance is fixed.
            TEST_CHECK(run, HasChunkOf(&Fixture, WorldVisibilityPass_Camera, Distant));
            TEST_CHECK(run, !HasChunkOf(&Fixture, WorldVisibilityPass_Camera, Behind)
                && HasChunkOf(&Fixture, WorldVisibilityPass_NearCascade, Behind)
                && HasChunkOf(&Fixture, WorldVisibilityPass_FarCascade, Behind));
            TEST_CHECK(run, HasChunkOf(&Fixture, WorldVisibilityPass_NearCascade, NearOnly)
                && !HasChunkOf(&Fixture, WorldVisibilityPass_FarCascade, NearOnly));
            TEST_CHECK(run, !HasChunkOf(&Fixture, WorldVisibilityPass_NearCascade, NonCaster)
                && !HasChunkOf(&Fixture, WorldVisibilityPass_FarCascade, NonCaster));
            for (int Pass = 0; Pass < (int)WorldVisibilityPass_Count; Pass++)
            {
                TEST_CHECK(run, !HasChunkOf(&Fixture, (WorldVisibilityPass)Pass, Beyond)
                    && !HasChunkOf(&Fixture, (WorldVisibilityPass)Pass, Clear));
            }
            // Counted apart from the models and sprites: the two in view, eight triangles each.
            WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&Fixture.Visibility);
            TEST_CHECK(run, (Counts.DrawnTerrainChunkCount == 2U) && (Counts.DrawnTerrainTriangleCount == 16U)
                && (Counts.DrawnModelCount == 0U) && (Counts.OutsideViewCount == 0U) && (Counts.FadedOutCount == 0U));
            size_t ChunkCount = 0U;
            WorldRenderCache_GetTerrainChunks(&Fixture.Cache, &ChunkCount);
            TEST_CHECK(run, (ChunkCount == 7U)
                && (WorldVisibility_GetTerrainChunkFade(&Fixture.Visibility, ChunkCount).Combined == 1.0f));
        }
    }
    TerrainMesh_Destroy(Ground);
    VisibilityFixture_Deconstruct(run, &Fixture);
    TestTerrain_FreeGrid(&Grid);
}

// Public functions.
void WorldVisibilityTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "frustum_planes_from_a_view_projection", FrustumPlanesFromAViewProjection },
        { "cascade_box_culls_its_casters", CascadeBoxCullsItsCasters },
        { "own_draw_distances_and_the_zoom_factor", OwnDrawDistancesAndTheZoomFactor },
        { "distance_fade_rises_over_the_band", DistanceFadeRisesOverTheBand },
        { "opacity_fade_and_the_max_rule", OpacityFadeAndTheMaxRule },
        { "dither_matches_the_shader_formula", DitherMatchesTheShaderFormula },
        { "camera_list_holds_what_is_in_view", CameraListHoldsWhatIsInView },
        { "casters_behind_the_camera_stay_in_the_shadow_lists", CastersBehindTheCameraStayInTheShadowLists },
        { "draw_distance_and_opacity_fade_and_cull", DrawDistanceAndOpacityFadeAndCull },
        { "models_fade_at_their_own_draw_distance", ModelsFadeAtTheirOwnDrawDistance },
        { "sprites_cull_and_fade_by_their_kind", SpritesCullAndFadeByTheirKind },
        { "sprites_fade_at_their_own_draw_distance", SpritesFadeAtTheirOwnDrawDistance },
        { "culling_off_keeps_everything_within_reach", CullingOffKeepsEverythingWithinReach },
        { "crisp_objects_count_only_in_view", CrispObjectsCountOnlyInView },
        { "lists_follow_their_cache", ListsFollowTheirCache },
        { "drawn_models_are_counted_by_lod", DrawnModelsAreCountedByLod },
        { "lists_and_lods_hold_twenty_kilometres_out", ListsAndLodsHoldTwentyKilometresOut },
        { "terrain_chunks_join_the_passes_as_models_do", TerrainChunksJoinThePassesAsModelsDo },
    };
    TestRun_RunSuite(run, "WorldVisibility", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
