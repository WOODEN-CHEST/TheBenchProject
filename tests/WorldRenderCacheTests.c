#include "TestSuites.h"
#include "TestAssets.h"
#include "TestWorld.h"
#include "Logger.h"
#include "WorldRenderCache.h"
#include "WorldTransform.h"
#include "SpriteTransform.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "WorldLightComponent.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <string.h>


// Macros.
/** Room for a work directory's path and a log file's text. */
#define CACHE_TEST_TEXT_CAPACITY 256


// Types.
/* An asset fixture with a "box" model (unit cube bounds) and a "box" world model over it, a "walk" world sprite with the
 * default settings over a one-frame animation, a world built with it, and a render cache whose world models are held
 * under their own user. */
typedef struct CacheFixtureStruct
{
    TestAssetFixture Assets;
    TestAssetCounters BoxCounters;
    World World;
    WorldRenderCache Cache;
    AssetUserID ModelUser;
    KeyRegistry Keys;
    bool IsWorldConstructed;
} CacheFixture;


// Static functions.
static bool CacheFixture_Construct(TestRun* run, CacheFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    BoundingBox UnitCube = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    if (!TestAssetFixture_Construct(run, &self->Assets)
        || !TestAssets_AddModel(run, &self->Assets, "box", UnitCube, &self->BoxCounters)
        || !TestAssets_AddWorldModelOf(run, &self->Assets, "box", "box")
        || !TestAssets_AddSpriteAnimation(run, &self->Assets, "walk_animation", NULL)
        || !TestAssets_AddWorldSpriteOf(run, &self->Assets, "walk", "walk_animation")
        || !TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(self->Assets.Manager, &self->ModelUser))
        || !TestWorld_Construct(run, &self->World, &self->Keys, self->Assets.Manager, NULL))
    {
        return false;
    }
    self->IsWorldConstructed = true;
    WorldRenderCache_Construct(&self->Cache);
    return true;
}

static void CacheFixture_Deconstruct(TestRun* run, CacheFixture* self)
{
    if (self->IsWorldConstructed)
    {
        WorldRenderCache_Deconstruct(&self->Cache);
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

static bool CacheFixture_Sync(TestRun* run, CacheFixture* self)
{
    return TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&self->Cache, &self->World, self->Assets.Manager,
        self->ModelUser, NULL));
}

/* Creates an object at @p position with @p scale and a model component naming the world model @p assetName; returns the
 * component (world-owned), or NULL with a failed check. */
static WorldModelComponent* AddModel(TestRun* run, World* world, const char* assetName, Vector3 position, Vector3 scale)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, NULL, &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, position))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, scale))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component)))
    {
        return NULL;
    }
    WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(Component);
    if (!TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)assetName)))
    {
        return NULL;
    }
    return ModelComponent;
}

/* The object that carries @p component. */
static WorldObject* OwnerOf(const WorldModelComponent* component)
{
    return WorldComponent_GetOwner(&component->Base);
}

static uint64_t IdOf(const WorldModelComponent* component)
{
    return WorldObject_GetId(OwnerOf(component));
}

static bool IsNear(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static void ResolvesModelsAndComputesWorldBounds(TestRun* run)
{
    CacheFixture Fixture;
    if (CacheFixture_Construct(run, &Fixture)
        && (AddModel(run, &Fixture.World, "box", (Vector3){ 10.0f, 0.0f, 0.0f }, (Vector3){ 2.0f, 4.0f, 2.0f }) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        if (TEST_CHECK(run, Count == 1) && TEST_CHECK(run, Models[0].Asset != NULL))
        {
            // Unit cube: local radius = |(0.5, 0.5, 0.5)|; the largest axis scale (4) scales it conservatively.
            TEST_CHECK(run, IsNear(Models[0].Center.x, 10.0f) && IsNear(Models[0].Center.y, 0.0f));
            TEST_CHECK(run, IsNear(Models[0].Radius, 4.0f * sqrtf(0.75f)));
            TEST_CHECK(run, Models[0].IsResolved && Models[0].IsDrawn);
            // Before any LOD update the entry holds no LOD yet: it reads as LOD 0, not fading.
            TEST_CHECK(run, !Models[0].Lod.IsStarted && (Models[0].Lod.CurrentLod == 0U)
                && !WorldLodState_IsFading(&Models[0].Lod));
            TEST_CHECK(run, Models[0].HasOutline && !Models[0].OmitsPixelation && Models[0].CastsShadows
                && (Models[0].ShadowTier == WorldShadowTier_Both));
            TEST_CHECK(run, (Models[0].DrawDistance == 600.0f) && !Models[0].IsDrawDistanceFixed
                && !Models[0].HasVariant);
        }
        TEST_CHECK(run, Fixture.BoxCounters.LoadCount == 1);

        // A second sync reuses the resolved entry (no new load, same asset).
        const WorldModel* FirstAsset = (Count == 1) ? Models[0].Asset : NULL;
        if (CacheFixture_Sync(run, &Fixture))
        {
            Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && (Models[0].Asset == FirstAsset));
        }
        TEST_CHECK(run, Fixture.BoxCounters.LoadCount == 1);
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

static void FailedModelStaysUnresolvedUntilRenamed(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelComponent* ModelComponent = NULL;
    if (CacheFixture_Construct(run, &Fixture)
        && ((ModelComponent = AddModel(run, &Fixture.World, "missing", (Vector3){ 0.0f, 0.0f, 0.0f },
            (Vector3){ 1.0f, 1.0f, 1.0f })) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        if (TEST_CHECK(run, Count == 1))
        {
            TEST_CHECK(run, Models[0].IsResolved && (Models[0].Asset == NULL) && !Models[0].IsDrawn);
        }

        // Another property changes the revision, but the name is the same: not retried. The settings still resolve
        // (against the defaults while there is no world model).
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(ModelComponent, WorldModelSetting_HasOutline, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasOutline(ModelComponent, false));
        if (CacheFixture_Sync(run, &Fixture))
        {
            Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && (Models[0].Asset == NULL) && !Models[0].HasOutline);
        }

        // Renaming changes the name, so the next sync resolves the new model.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)u8"box"));
        if (CacheFixture_Sync(run, &Fixture))
        {
            Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && (Models[0].Asset != NULL) && Models[0].IsDrawn);
            TEST_CHECK(run, (Count == 1) && (Models[0].Revision == WorldComponent_GetRevision(&ModelComponent->Base)));
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

static void EntriesFollowComponentsAcrossAddsAndRemovals(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelComponent* First = NULL;
    WorldModelComponent* Second = NULL;
    Vector3 One = { 1.0f, 1.0f, 1.0f };
    if (CacheFixture_Construct(run, &Fixture)
        && ((First = AddModel(run, &Fixture.World, "box", (Vector3){ 0.0f, 0.0f, 0.0f }, One)) != NULL)
        && ((Second = AddModel(run, &Fixture.World, "missing", (Vector3){ 5.0f, 0.0f, 0.0f }, One)) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        uint64_t SecondId = IdOf(Second);
        WorldModelComponent* Third = AddModel(run, &Fixture.World, "box", (Vector3){ 9.0f, 0.0f, 0.0f }, One);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&Fixture.World, IdOf(First)));

        if ((Third != NULL) && CacheFixture_Sync(run, &Fixture))
        {
            size_t Count = 0;
            const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            if (TEST_CHECK(run, Count == 2))
            {
                TEST_CHECK(run, (Models[0].ObjectId == SecondId) && (Models[0].Component == Second));
                TEST_CHECK(run, Models[0].IsResolved && (Models[0].Asset == NULL));
                TEST_CHECK(run, (Models[1].Component == Third) && (Models[1].Asset != NULL));
                TEST_CHECK(run, IsNear(Models[1].Center.x, 9.0f));
            }
        }

        // Removing only the component (the object stays) drops its entry too.
        if ((Third != NULL)
            && TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Fixture.World, IdOf(Third), WorldComponentType_Model))
            && CacheFixture_Sync(run, &Fixture))
        {
            size_t Count = 0;
            const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && (Models[0].Component == Second));
        }
        // The box asset is held once under the model user, however many components use it.
        TEST_CHECK(run, Fixture.BoxCounters.LoadCount == 1);
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

static void ReusedIdWithAnotherModelIsResolvedAgain(TestRun* run)
{
    CacheFixture Fixture;
    TestAssetCounters CrateCounters = { 0 };
    BoundingBox BigCube = { .min = { -5.0f, -5.0f, -5.0f }, .max = { 5.0f, 5.0f, 5.0f } };
    Vector3 One = { 1.0f, 1.0f, 1.0f };
    WorldModelComponent* Box = NULL;
    if (CacheFixture_Construct(run, &Fixture)
        && TestAssets_AddModel(run, &Fixture.Assets, "crate", BigCube, &CrateCounters)
        && TestAssets_AddWorldModelOf(run, &Fixture.Assets, "crate", "crate")
        && ((Box = AddModel(run, &Fixture.World, "box", (Vector3){ 0.0f, 0.0f, 0.0f }, One)) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        // An editor undo or a re-applied save: the id comes back with another object, naming another model, at
        // revision 0 like every new component. The old entry must not be reused.
        uint64_t Id = IdOf(Box);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&Fixture.World, Id));
        WorldObject* Object = NULL;
        WorldComponent* Component = NULL;
        if (TEST_CHECK_SUCCESS(run, World_CreateObjectWithId(&Fixture.World, NULL, Id, &Object))
            && TEST_CHECK_SUCCESS(run, World_AddComponent(&Fixture.World, Id, WorldComponentType_Model, &Component))
            && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component),
                (const unsigned char*)u8"crate"))
            && CacheFixture_Sync(run, &Fixture))
        {
            size_t Count = 0;
            const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            if (TEST_CHECK(run, Count == 1))
            {
                TEST_CHECK(run, Models[0].Component == WorldModelComponent_FromComponent(Component));
                TEST_CHECK(run, IsNear(Models[0].Radius, sqrtf(75.0f)));
            }
            TEST_CHECK(run, CrateCounters.LoadCount == 1);
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

static void AnotherWorldIsResolvedFromScratch(TestRun* run)
{
    CacheFixture Fixture;
    BoundingBox BigCube = { .min = { -5.0f, -5.0f, -5.0f }, .max = { 5.0f, 5.0f, 5.0f } };
    Vector3 Origin = { 0.0f, 0.0f, 0.0f };
    Vector3 One = { 1.0f, 1.0f, 1.0f };
    if (!CacheFixture_Construct(run, &Fixture) || !TestAssets_AddModel(run, &Fixture.Assets, "crate", BigCube, NULL)
        || !TestAssets_AddWorldModelOf(run, &Fixture.Assets, "crate", "crate")
        || (AddModel(run, &Fixture.World, "box", Origin, One) == NULL) || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }

    // A second world whose first object has the same id (1) and revisions, but another model.
    World Other;
    if (TEST_CHECK_SUCCESS(run, World_Construct2(&Other, &Fixture.Keys, Fixture.Assets.Manager, NULL)))
    {
        if ((AddModel(run, &Other, "crate", Origin, One) != NULL)
            && TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Fixture.Cache, &Other, Fixture.Assets.Manager,
                Fixture.ModelUser, NULL)))
        {
            size_t Count = 0;
            const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && IsNear(Models[0].Radius, sqrtf(75.0f)));
        }
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Other));
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* Creates an object with a component of @p type; returns the component (world-owned), or NULL with a failed check. */
static WorldComponent* AddComponentObject(TestRun* run, World* world, WorldComponentType type)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, NULL, &Object))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), type, &Component)))
    {
        return NULL;
    }
    return Component;
}

static void ListsDrawableSpritesAndEveryLight(TestRun* run)
{
    CacheFixture Fixture;
    if (!CacheFixture_Construct(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteComponent* Bound = WorldSpriteComponent_FromComponent(AddComponentObject(run, &Fixture.World,
        WorldComponentType_Sprite));
    WorldSpriteComponent* Empty = WorldSpriteComponent_FromComponent(AddComponentObject(run, &Fixture.World,
        WorldComponentType_Sprite));
    WorldLightComponent* Light = WorldLightComponent_FromComponent(AddComponentObject(run, &Fixture.World,
        WorldComponentType_Light));
    bool IsBuilt = (Bound != NULL) && (Empty != NULL) && (Light != NULL)
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Bound, (const unsigned char*)u8"walk"))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Empty, (const unsigned char*)u8"missing"))
        && TEST_CHECK_SUCCESS(run, WorldLightComponent_SetIntensity(Light, 0.0f)); // dark lights are still listed
    if (IsBuilt && CacheFixture_Sync(run, &Fixture))
    {
        size_t SpriteCount = 0;
        const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &SpriteCount);
        if (TEST_CHECK(run, SpriteCount == 1))
        {
            TEST_CHECK(run, (Sprites[0].Component == Bound) && Sprites[0].HasOutline && !Sprites[0].OmitsPixelation);
        }
        size_t LightCount = 0;
        const WorldRenderLight* Lights = WorldRenderCache_GetLights(&Fixture.Cache, &LightCount);
        TEST_CHECK(run, (LightCount == 1) && (Lights[0].Component == Light) && (Lights[0].Intensity == 0.0f));

        // The placement's own crisp setting applies once its flag is on.
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOmitsPixelation(Bound, true));
        if (CacheFixture_Sync(run, &Fixture))
        {
            Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &SpriteCount);
            TEST_CHECK(run, (SpriteCount == 1) && !Sprites[0].OmitsPixelation);
        }
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Bound, WorldSpriteSetting_OmitPixelation, true));
        if (CacheFixture_Sync(run, &Fixture))
        {
            Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &SpriteCount);
            TEST_CHECK(run, (SpriteCount == 1) && Sprites[0].OmitsPixelation);
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

static void DisabledAndHiddenObjectsAreNotDrawn(TestRun* run)
{
    CacheFixture Fixture;
    Vector3 One = { 1.0f, 1.0f, 1.0f };
    WorldModelComponent* ModelComponent = NULL;
    if (!CacheFixture_Construct(run, &Fixture)
        || ((ModelComponent = AddModel(run, &Fixture.World, "box", (Vector3){ 0.0f, 0.0f, 0.0f }, One)) == NULL))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(AddComponentObject(run, &Fixture.World,
        WorldComponentType_Sprite));
    WorldLightComponent* Light = WorldLightComponent_FromComponent(AddComponentObject(run, &Fixture.World,
        WorldComponentType_Light));
    if ((Sprite != NULL) && (Light != NULL)
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"walk"))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(ModelComponent, WorldModelSetting_OmitPixelation,
            true))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOmitsPixelation(ModelComponent, true))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(ModelComponent), false))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(WorldComponent_GetOwner(&Sprite->Base), false))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(WorldComponent_GetOwner(&Light->Base), false))
        && CacheFixture_Sync(run, &Fixture))
    {
        // The hidden model keeps its entry, its resolved model and its settings, but is not drawn.
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, (Count == 1) && (Models[0].Asset != NULL) && !Models[0].IsDrawn && Models[0].OmitsPixelation);
        WorldRenderCache_GetSprites(&Fixture.Cache, &Count);
        TEST_CHECK(run, Count == 0);
        WorldRenderCache_GetLights(&Fixture.Cache, &Count);
        TEST_CHECK(run, Count == 0);

        // Shown again: drawn without loading anything again.
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(ModelComponent), true));
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(WorldComponent_GetOwner(&Sprite->Base), true));
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(WorldComponent_GetOwner(&Light->Base), true));
        if (CacheFixture_Sync(run, &Fixture))
        {
            Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && Models[0].IsDrawn && Models[0].OmitsPixelation);
            WorldRenderCache_GetSprites(&Fixture.Cache, &Count);
            TEST_CHECK(run, Count == 1);
            WorldRenderCache_GetLights(&Fixture.Cache, &Count);
            TEST_CHECK(run, Count == 1);
        }
        TEST_CHECK(run, Fixture.BoxCounters.LoadCount == 1);
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* An entry carries the settings in effect: the world model's while the placement overrides nothing, the placement's own
 * while their flags are on, and the draw distance in effect times the multiplier. */
static void EntriesCarryTheSettingsInEffect(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelComponent* ModelComponent = NULL;
    if (!CacheFixture_Construct(run, &Fixture)
        || !TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"dark_box\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 20}], \"draw_distance\": 50, \"is_draw_distance_fixed\": true, \"render\": {"
            "\"has_outline\": false, \"omit_pixelation\": true, \"shadow_tier\": \"far\", \"casts_shadows\": false}}")
        || ((ModelComponent = AddModel(run, &Fixture.World, "dark_box", Vector3Zero(), (Vector3){ 1.0f, 1.0f, 1.0f }))
            == NULL)
        || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    size_t Count = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
    TEST_CHECK(run, (Count == 1) && Models[0].IsDrawn && !Models[0].HasOutline && Models[0].OmitsPixelation
        && (Models[0].ShadowTier == WorldShadowTier_Far) && !Models[0].CastsShadows && (Models[0].DrawDistance == 50.0f)
        && Models[0].IsDrawDistanceFixed);

    // The placement's own values (kept from before) apply once their flags are on; the multiplier always does.
    for (int Setting = 0; Setting < (int)WorldModelSetting_Count; Setting++)
    {
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(ModelComponent, (WorldModelSetting)Setting, true));
    }
    TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowTier(ModelComponent, WorldShadowTier_Near));
    TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistance(ModelComponent, 30.0f));
    TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistanceMultiplier(ModelComponent, 2.0f));
    if (CacheFixture_Sync(run, &Fixture))
    {
        Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, (Count == 1) && Models[0].HasOutline && !Models[0].OmitsPixelation
            && (Models[0].ShadowTier == WorldShadowTier_Near) && Models[0].CastsShadows
            && (Models[0].DrawDistance == 60.0f) && !Models[0].IsDrawDistanceFixed);
    }

    // Overrides off again: the world model's settings, its distance times the multiplier.
    for (int Setting = 0; Setting < (int)WorldModelSetting_Count; Setting++)
    {
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(ModelComponent, (WorldModelSetting)Setting, false));
    }
    if (CacheFixture_Sync(run, &Fixture))
    {
        Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, (Count == 1) && !Models[0].HasOutline && (Models[0].ShadowTier == WorldShadowTier_Far)
            && (Models[0].DrawDistance == 100.0f) && Models[0].IsDrawDistanceFixed);
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* Changing an object's world model swaps what it draws and keeps the object: the component's revision advances and the
 * next sync resolves the new world model (the old one stays held until PrepareWorld swaps the user). */
static void SwappingTheWorldModelKeepsTheObject(TestRun* run)
{
    CacheFixture Fixture;
    BoundingBox BigCube = { .min = { -5.0f, -5.0f, -5.0f }, .max = { 5.0f, 5.0f, 5.0f } };
    WorldModelComponent* ModelComponent = NULL;
    if (!CacheFixture_Construct(run, &Fixture) || !TestAssets_AddModel(run, &Fixture.Assets, "crate", BigCube, NULL)
        || !TestAssets_AddWorldModelOf(run, &Fixture.Assets, "crate", "crate")
        || ((ModelComponent = AddModel(run, &Fixture.World, "box", Vector3Zero(), (Vector3){ 1.0f, 1.0f, 1.0f }))
            == NULL)
        || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    size_t Count = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
    const WorldModel* Box = (Count == 1) ? Models[0].Asset : NULL;
    uint64_t Id = IdOf(ModelComponent);
    uint32_t Revision = WorldComponent_GetRevision(&ModelComponent->Base);

    TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)u8"crate"));
    TEST_CHECK(run, WorldComponent_GetRevision(&ModelComponent->Base) != Revision);
    if (CacheFixture_Sync(run, &Fixture))
    {
        Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, (Count == 1) && (Models[0].Asset != NULL) && (Models[0].Asset != Box)
            && (Models[0].ObjectId == Id) && IsNear(Models[0].Radius, sqrtf(75.0f)));
        TEST_CHECK(run, (Count == 1) && (Models[0].Revision == WorldComponent_GetRevision(&ModelComponent->Base)));
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Found = 0;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, CACHE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, CACHE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* At = strstr((const char*)Text._data, needle); At != NULL; At = strstr(At + 1, needle))
        {
            Found++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Found;
}

/* A variant is resolved to its index with the world model and again when uses_variant or the name changes; an unknown
 * name draws the model's own materials and is logged once, not again on unrelated changes. */
static void VariantsResolveByNameAndUnknownOnesWarnOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, CACHE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "cache_variants", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }

    CacheFixture Fixture;
    WorldModelComponent* ModelComponent = NULL;
    if (CacheFixture_Construct(run, &Fixture)
        && TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"painted\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 9}], \"variants\": [{\"name\": \"red\"}, {\"name\": \"blue\"}]}")
        && ((ModelComponent = AddModel(run, &Fixture.World, "painted", Vector3Zero(), (Vector3){ 1.0f, 1.0f, 1.0f }))
            != NULL)
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetVariant(ModelComponent, (const unsigned char*)u8"blue"))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetUsesVariant(ModelComponent, true)))
    {
        const char* Steps[] = { "blue", "green", "green", "red", "red" };
        const bool UsesVariant[] = { true, true, true, true, false };
        const bool HasVariant[] = { true, false, false, true, false };
        const size_t Indices[] = { 1U, 0U, 0U, 0U, 0U };
        for (size_t Step = 0U; Step < (sizeof(Steps) / sizeof(Steps[0])); Step++)
        {
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetVariant(ModelComponent, (const unsigned char*)Steps[Step]));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetUsesVariant(ModelComponent, UsesVariant[Step]));
            // An unrelated change (the third step) advances the revision without asking for another variant.
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasOutline(ModelComponent, (Step % 2U) == 0U));
            if (TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Fixture.Cache, &Fixture.World, Fixture.Assets.Manager,
                Fixture.ModelUser, &Log)))
            {
                size_t Count = 0;
                const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
                TEST_CHECK(run, (Count == 1) && (Models[0].HasVariant == HasVariant[Step])
                    && (!HasVariant[Step] || (Models[0].VariantIndex == Indices[Step])));
            }
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "it has no variant \"green\"") == 1U);
    Memory_Free(Directory._data);
}

/* A component that replaces another on the same object, with the same world model, gets the variant it asks for, even
 * when its revision happens to equal the old component's (revisions of two components are not comparable). */
static void AReplacedComponentGetsItsOwnVariant(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelComponent* First = NULL;
    if (!CacheFixture_Construct(run, &Fixture)
        || !TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"painted\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 9}], \"variants\": [{\"name\": \"red\"}, {\"name\": \"blue\"}]}")
        || ((First = AddModel(run, &Fixture.World, "painted", Vector3Zero(), (Vector3){ 1.0f, 1.0f, 1.0f }))
            == NULL)
        || !TEST_CHECK_SUCCESS(run, WorldModelComponent_SetVariant(First, (const unsigned char*)u8"red"))
        || !TEST_CHECK_SUCCESS(run, WorldModelComponent_SetUsesVariant(First, true))
        || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    uint64_t Id = IdOf(First);
    uint32_t FirstRevision = WorldComponent_GetRevision(&First->Base);
    // Two replacements, each with as many changes as the first (so the same revision): another variant, then none.
    const char* Variants[] = { "blue", "red" };
    const bool UsesVariant[] = { true, false };
    for (size_t Step = 0U; Step < 2U; Step++)
    {
        WorldComponent* Component = NULL;
        if (!TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Fixture.World, Id, WorldComponentType_Model))
            || !TEST_CHECK_SUCCESS(run, World_AddComponent(&Fixture.World, Id, WorldComponentType_Model, &Component)))
        {
            break;
        }
        WorldModelComponent* Replacement = WorldModelComponent_FromComponent(Component);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(Replacement, (const unsigned char*)u8"painted"));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetVariant(Replacement, (const unsigned char*)Variants[Step]));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetUsesVariant(Replacement, UsesVariant[Step]));
        TEST_CHECK(run, WorldComponent_GetRevision(Component) == FirstRevision);
        if (CacheFixture_Sync(run, &Fixture))
        {
            size_t Count = 0;
            const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && (Models[0].Component == Replacement)
                && (Models[0].HasVariant == UsesVariant[Step])
                && (!UsesVariant[Step] || (Models[0].VariantIndex == 1U)));
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* Without "bounds", the sphere is LOD 0's mesh sphere through its import transform composed with the world matrix:
 * the drawn model's own sphere, as tight as before world models even when an uneven import scale meets an uneven
 * placement scale (taking the two stretches apart would multiply them up). */
static void DerivedBoundsComposeTheImportWithThePlacement(TestRun* run)
{
    CacheFixture Fixture;
    BoundingBox UnitCube = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    Matrix Import = MatrixMultiply(MatrixScale(3.0f, 0.2f, 1.0f), MatrixRotateY(PI / 4.0f));
    Vector3 Position = { 2.0f, 1.0f, -3.0f };
    Vector3 Scale = { 1.0f, 5.0f, 1.0f };
    if (CacheFixture_Construct(run, &Fixture)
        && TestAssets_AddModelWithMaterials(run, &Fixture.Assets, "plank", UnitCube, 1, Import, NULL)
        && TestAssets_AddWorldModelOf(run, &Fixture.Assets, "plank", "plank")
        && (AddModel(run, &Fixture.World, "plank", Position, Scale) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        Matrix Placement = MatrixMultiply(MatrixScale(Scale.x, Scale.y, Scale.z),
            MatrixTranslate(Position.x, Position.y, Position.z));
        Matrix Composed = MatrixMultiply(Import, Placement);
        float Expected = sqrtf(0.75f) * WorldTransform_GetMaxScale(Composed);
        float Separate = sqrtf(0.75f) * WorldTransform_GetMaxScale(Import) * WorldTransform_GetMaxScale(Placement);
        if (TEST_CHECK(run, Count == 1))
        {
            TEST_CHECK(run, IsNear(Models[0].Radius, Expected) && (Models[0].Radius < (0.5f * Separate)));
            TEST_CHECK(run, IsNear(Models[0].Center.x, 2.0f) && IsNear(Models[0].Center.y, 1.0f)
                && IsNear(Models[0].Center.z, -3.0f));
            Matrix LodTransform = WorldRenderModel_GetLodTransform(&Models[0], 0U);
            TEST_CHECK(run, memcmp(&LodTransform, &Composed, sizeof(Matrix)) == 0);
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* The bounding sphere is the world model's: its "bounds" in the object's own space, or LOD 0's mesh bounds moved by
 * LOD 0's import transform. Each LOD is drawn with its own import transform inside the object's world matrix. */
static void BoundsAndTheDrawnLodComeFromTheWorldModel(TestRun* run)
{
    CacheFixture Fixture;
    BoundingBox UnitCube = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    WorldModelComponent* Explicit = NULL;
    WorldModelComponent* Imported = NULL;
    Vector3 Position = { 10.0f, 0.0f, 0.0f };
    Vector3 Scale = { 2.0f, 2.0f, 2.0f };
    if (!CacheFixture_Construct(run, &Fixture)
        || !TestAssets_AddModelWithMaterials(run, &Fixture.Assets, "lifted", UnitCube, 1,
            MatrixTranslate(0.0f, 3.0f, 0.0f), NULL)
        || !TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"tall\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 9}], \"bounds\": {\"center\": [0, 1, 0], \"radius\": 2}}")
        || !TestAssets_AddWorldModel(run, &Fixture.Assets, "{\"name\": \"lifted\", \"lods\": [{\"model\": "
            "\"lifted\", \"max_distance\": 9}, {\"model\": \"box\", \"max_distance\": 20}]}")
        || ((Explicit = AddModel(run, &Fixture.World, "tall", Position, Scale)) == NULL)
        || ((Imported = AddModel(run, &Fixture.World, "lifted", Position, Scale)) == NULL)
        || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    size_t Count = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
    if (TEST_CHECK(run, Count == 2))
    {
        TEST_CHECK(run, IsNear(Models[0].Center.x, 10.0f) && IsNear(Models[0].Center.y, 2.0f)
            && IsNear(Models[0].Radius, 4.0f));
        // LOD 0 is lifted 3 m by its import transform: its sphere and its draw transform both are, scaled by 2. LOD 1
        // (the box) has none: drawn at the object's own place. Past the last LOD the world matrix alone.
        TEST_CHECK(run, IsNear(Models[1].Center.y, 6.0f) && IsNear(Models[1].Radius, 2.0f * sqrtf(0.75f)));
        Matrix Lifted = WorldRenderModel_GetLodTransform(&Models[1], 0U);
        Matrix Plain = WorldRenderModel_GetLodTransform(&Models[1], 1U);
        Matrix Past = WorldRenderModel_GetLodTransform(&Models[1], 2U);
        TEST_CHECK(run, (WorldRenderModel_GetLodModel(&Models[1], 0U) == WorldModel_GetLodModel(Models[1].Asset, 0U))
            && IsNear(Lifted.m13, 6.0f) && IsNear(Lifted.m12, 10.0f));
        TEST_CHECK(run, IsNear(Plain.m13, 0.0f) && IsNear(Plain.m12, 10.0f) && IsNear(Plain.m0, 2.0f)
            && (WorldRenderModel_GetLodModel(&Models[1], 2U) == NULL)
            && (memcmp(&Past, &Models[1].WorldMatrix, sizeof(Matrix)) == 0));
        TEST_CHECK(run, Models[1].DrawDistance == 20.0f);
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* A sprite entry carries its quad (the world size from the frame's pixels, the pixels per unit and the object's scale;
 * the object's axes; the pivot and flips), the fixed matrix, its bounds and its drawing settings, all as they are in
 * effect: the world sprite's, with the placement's own where it overrides them. A decal needs the fixed orientation, and
 * only cutout sprites that are not decals are drawn crisp. */
static void SpriteEntriesCarryTheirQuadAndSettings(TestRun* run)
{
    CacheFixture Fixture;
    if (!CacheFixture_Construct(run, &Fixture)
        || !TestAssets_AddWorldSprite(run, &Fixture.Assets, "{\"name\": \"sign\", \"sprite_animation\": "
            "\"walk_animation\", \"orientation\": \"fixed\", \"pivot\": [0.5, 0], \"pixels_per_unit\": 0.5, "
            "\"alpha_mode\": \"blend\", \"is_decal\": true, \"render\": {\"omit_pixelation\": true}}"))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteComponent* Sign = WorldSpriteComponent_FromComponent(AddComponentObject(run, &Fixture.World,
        WorldComponentType_Sprite));
    WorldSpriteComponent* Puff = WorldSpriteComponent_FromComponent(AddComponentObject(run, &Fixture.World,
        WorldComponentType_Sprite));
    bool IsBuilt = (Sign != NULL) && (Puff != NULL)
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sign, (const unsigned char*)u8"sign"))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Puff, (const unsigned char*)u8"walk"))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetScale(WorldComponent_GetOwner(&Sign->Base),
            (Vector3){ 2.0f, 3.0f, 9.0f }))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(WorldComponent_GetOwner(&Sign->Base),
            (Vector3){ 1.0f, 2.0f, 3.0f }))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(WorldComponent_GetOwner(&Sign->Base),
            (Vector3){ 0.0f, PI / 2.0f, 0.0f }))
        // The sign's own look comes from its world sprite; this placement mirrors it.
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Sign, WorldSpriteSetting_FlipX, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetFlippedX(Sign, true))
        // The puff's default look, changed by this placement: unlit and bright, a "decal" billboard (not drawn as one),
        // crisp. A value without its flag changes nothing.
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Puff, WorldSpriteSetting_IsLit, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetLit(Puff, false))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Puff, WorldSpriteSetting_Emission, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetEmission(Puff, 4.0f))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Puff, WorldSpriteSetting_IsDecal, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDecal(Puff, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Puff, WorldSpriteSetting_OmitPixelation, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOmitsPixelation(Puff, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetPixelsPerUnit(Puff, 4.0f));
    size_t Count = 0;
    const WorldRenderSprite* Sprites = NULL;
    if (IsBuilt && CacheFixture_Sync(run, &Fixture))
    {
        Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &Count);
    }
    if (TEST_CHECK(run, (Sprites != NULL) && (Count == 2)))
    {
        // The test frame is 1x1 pixels without a texture: 1 / 0.5 pixels per unit times the X and Y scale.
        const WorldRenderSprite* Entry = &Sprites[0];
        TEST_CHECK(run, IsNear(Entry->Shape.Size.x, 4.0f) && IsNear(Entry->Shape.Size.y, 6.0f));
        TEST_CHECK(run, (Entry->Shape.Orientation == WorldSpriteOrientation_Fixed) && Entry->Shape.IsFlippedX
            && !Entry->Shape.IsFlippedY && (Entry->Shape.Pivot.y == 0.0f));
        TEST_CHECK(run, IsNear(Entry->Shape.Position.x, 1.0f) && IsNear(Entry->Shape.Position.z, 3.0f));
        TEST_CHECK(run, IsNear(Entry->Shape.AxisZ.x, 1.0f) && IsNear(Entry->Shape.AxisY.y, 1.0f)); // turned 90 degrees
        TEST_CHECK(run, (Entry->UvRect[2] == 0.0f) && (Entry->UvRect[3] == 0.0f)); // no texture size: no area
        SpriteViewer AnyViewer = SpriteViewer_FromPoint((Vector3){ 9.0f, 9.0f, 9.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
        Matrix Expected = SpriteTransform_GetMatrix(&Entry->Shape, &AnyViewer);
        TEST_CHECK(run, memcmp(&Expected, &Entry->FixedTransform, sizeof(Matrix)) == 0);
        TEST_CHECK(run, IsNear(Entry->BoundsRadius, SpriteTransform_GetBoundingRadius(Entry->Shape.Size,
            Entry->Shape.Pivot)));
        TEST_CHECK(run, Entry->IsDecal && (Entry->AlphaMode == WorldSpriteAlphaMode_Blend) && !Entry->OmitsPixelation
            && Entry->IsLit && Entry->CastsShadows && IsNear(Entry->DecalOffset, 0.01f));
        // No draw distance anywhere: a decal's.
        TEST_CHECK(run, (Entry->DrawDistance == 60.0f) && !Entry->IsDrawDistanceFixed);

        Entry = &Sprites[1];
        // The cutout billboard is drawn crisp; the blended decal above is not.
        TEST_CHECK(run, !Entry->IsDecal && !Entry->IsLit && (Entry->Emission == 4.0f) && Entry->OmitsPixelation);
        TEST_CHECK(run, IsNear(Entry->Shape.Size.x, 1.0f / 32.0f) && (Entry->Shape.Pivot.x == 0.5f));
        TEST_CHECK(run, Entry->DrawDistance == 150.0f);
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* A sprite entry's draw distance is the one in effect (the world sprite's, or the placement's own while it overrides
 * it, times the multiplier) and so is the fixed flag; swapping the world sprite swaps the settings and keeps the
 * placement's overrides. */
static void SpriteEntriesCarryTheirDrawDistanceAndFollowASwap(TestRun* run)
{
    CacheFixture Fixture;
    WorldSpriteComponent* Sprite = NULL;
    if (!CacheFixture_Construct(run, &Fixture)
        || !TestAssets_AddWorldSprite(run, &Fixture.Assets, "{\"name\": \"landmark\", \"sprite_animation\": "
            "\"walk_animation\", \"pixels_per_unit\": 8, \"draw_distance\": 40, \"is_draw_distance_fixed\": true}")
        || ((Sprite = WorldSpriteComponent_FromComponent(AddComponentObject(run, &Fixture.World,
            WorldComponentType_Sprite))) == NULL)
        || !TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"landmark")))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }

    size_t Count = 0;
    const WorldRenderSprite* Sprites = NULL;
    if (CacheFixture_Sync(run, &Fixture) && ((Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &Count)) != NULL)
        && TEST_CHECK(run, Count == 1))
    {
        TEST_CHECK(run, (Sprites[0].DrawDistance == 40.0f) && Sprites[0].IsDrawDistanceFixed
            && IsNear(Sprites[0].Shape.Size.x, 1.0f / 8.0f));
    }

    // The multiplier scales the world sprite's distance; the placement's own replaces it while its flag is on.
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistanceMultiplier(Sprite, 2.0f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistance(Sprite, 10.0f));
    if (CacheFixture_Sync(run, &Fixture) && ((Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &Count)) != NULL))
    {
        TEST_CHECK(run, (Count == 1) && (Sprites[0].DrawDistance == 80.0f));
    }
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Sprite, WorldSpriteSetting_DrawDistance, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Sprite, WorldSpriteSetting_IsDrawDistanceFixed, true));
    if (CacheFixture_Sync(run, &Fixture) && ((Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &Count)) != NULL))
    {
        TEST_CHECK(run, (Count == 1) && (Sprites[0].DrawDistance == 20.0f) && !Sprites[0].IsDrawDistanceFixed);
    }

    // Another world sprite: its look, the placement's overrides still on top.
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"walk"));
    if (CacheFixture_Sync(run, &Fixture) && ((Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &Count)) != NULL))
    {
        TEST_CHECK(run, (Count == 1) && IsNear(Sprites[0].Shape.Size.x, 1.0f / 32.0f)
            && (Sprites[0].DrawDistance == 20.0f) && !Sprites[0].IsDrawDistanceFixed);
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* Creates an object under @p parent with a component of @p type; returns the component, or NULL with a failed check. */
static WorldComponent* AddChildComponent(TestRun* run, World* world, const WorldObject* parent, WorldComponentType type,
    Vector3 position)
{
    WorldComponent* Component = AddComponentObject(run, world, type);
    if ((Component == NULL)
        || !TEST_CHECK_SUCCESS(run, World_SetParent(world, WorldObject_GetId(WorldComponent_GetOwner(Component)),
            WorldObject_GetId(parent), false))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(WorldComponent_GetOwner(Component), position)))
    {
        return NULL;
    }
    return Component;
}

/* A parent turned a quarter about Y and scaled 2 places its children: a model, a light and a sprite. The cache reads
 * their world placements, also while they are dirty (no World_UpdateTransforms) and after the parent moves. */
static void ChildrenAreDrawnWhereTheirParentsPutThem(TestRun* run)
{
    CacheFixture Fixture;
    WorldObject* Parent = NULL;
    if (!CacheFixture_Construct(run, &Fixture)
        || !TEST_CHECK_SUCCESS(run, World_CreateObject(&Fixture.World, NULL, &Parent))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Parent, (Vector3){ 10.0f, 0.0f, 0.0f }))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Parent, (Vector3){ 0.0f, PI / 2.0f, 0.0f }))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Parent, (Vector3){ 2.0f, 2.0f, 2.0f })))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(AddChildComponent(run, &Fixture.World,
        Parent, WorldComponentType_Model, (Vector3){ 1.0f, 0.0f, 0.0f }));
    WorldLightComponent* Light = WorldLightComponent_FromComponent(AddChildComponent(run, &Fixture.World, Parent,
        WorldComponentType_Light, (Vector3){ 0.0f, 1.0f, 0.0f }));
    WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(AddChildComponent(run, &Fixture.World, Parent,
        WorldComponentType_Sprite, (Vector3){ 0.0f, 0.0f, 1.0f }));
    if ((ModelComponent != NULL) && (Light != NULL) && (Sprite != NULL)
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)u8"box"))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"walk"))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Sprite, WorldSpriteSetting_Orientation, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOrientation(Sprite, WorldSpriteOrientation_Fixed)))
    {
        for (size_t Pass = 0U; (Pass < 2U) && CacheFixture_Sync(run, &Fixture); Pass++)
        {
            // The turn takes local +X to -Z and +Z to +X; the scale doubles the distances and the sizes.
            float Shift = (Pass == 0U) ? 0.0f : 5.0f;
            size_t Count = 0;
            const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && Models[0].IsDrawn && IsNear(Models[0].Center.x, 10.0f)
                && IsNear(Models[0].Center.y, Shift) && IsNear(Models[0].Center.z, -2.0f)
                && IsNear(Models[0].Radius, 2.0f * sqrtf(0.75f)));
            Matrix LodTransform = (Count == 1) ? WorldRenderModel_GetLodTransform(&Models[0], 0U) : (Matrix){ 0 };
            TEST_CHECK(run, (Count == 1) && (memcmp(&LodTransform, &(Matrix){ 0 }, sizeof(Matrix)) != 0)
                && IsNear(LodTransform.m12, 10.0f) && IsNear(LodTransform.m14, -2.0f));
            const WorldRenderLight* Lights = WorldRenderCache_GetLights(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && IsNear(Lights[0].Position.x, 10.0f)
                && IsNear(Lights[0].Position.y, 2.0f + Shift) && IsNear(Lights[0].Position.z, 0.0f));
            const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && IsNear(Sprites[0].Shape.Position.x, 12.0f)
                && IsNear(Sprites[0].Shape.Position.y, Shift) && IsNear(Sprites[0].Shape.AxisZ.x, 1.0f)
                && IsNear(Sprites[0].Shape.AxisX.z, -1.0f) && IsNear(Sprites[0].Shape.Size.x, 2.0f / 32.0f));

            // Then the parent rises; the next sync finds the children with it (the matrices cached this time).
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Parent, (Vector3){ 10.0f, 5.0f, 0.0f }));
            World_UpdateTransforms(&Fixture.World);
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

static void HiddenOrDisabledParentsHideTheirSubtree(TestRun* run)
{
    CacheFixture Fixture;
    WorldObject* Parent = NULL;
    if (!CacheFixture_Construct(run, &Fixture) || !TEST_CHECK_SUCCESS(run, World_CreateObject(&Fixture.World, NULL,
        &Parent)))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(AddChildComponent(run, &Fixture.World,
        Parent, WorldComponentType_Model, Vector3Zero()));
    WorldSpriteComponent* Sprite = WorldSpriteComponent_FromComponent(AddChildComponent(run, &Fixture.World, Parent,
        WorldComponentType_Sprite, Vector3Zero()));
    bool IsBuilt = (ModelComponent != NULL) && (Sprite != NULL)
        && (AddChildComponent(run, &Fixture.World, Parent, WorldComponentType_Light, Vector3Zero()) != NULL)
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)u8"box"))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Sprite, (const unsigned char*)u8"walk"));
    // Shown, hidden by the parent, disabled by the parent, shown again.
    const bool Visible[] = { true, false, true, true };
    const bool Enabled[] = { true, true, false, true };
    for (size_t Step = 0U; IsBuilt && (Step < 4U); Step++)
    {
        bool IsShown = Visible[Step] && Enabled[Step];
        if (!TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(Parent, Visible[Step]))
            || !TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Parent, Enabled[Step]))
            || !CacheFixture_Sync(run, &Fixture))
        {
            break;
        }
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, (Count == 1) && (Models[0].IsDrawn == IsShown));
        WorldRenderCache_GetSprites(&Fixture.Cache, &Count);
        TEST_CHECK(run, Count == (IsShown ? 1U : 0U));
        WorldRenderCache_GetLights(&Fixture.Cache, &Count);
        TEST_CHECK(run, Count == (IsShown ? 1U : 0U));
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* A child turned 45 degrees inside a parent stretched 4 times along X is sheared: its longest axis is about 2.9 long,
 * but the matrix stretches some direction 4 times, and the bounding sphere must hold that. */
static void BoundsStayConservativeUnderShear(TestRun* run)
{
    CacheFixture Fixture;
    WorldObject* Parent = NULL;
    WorldModelComponent* ModelComponent = NULL;
    if (CacheFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, World_CreateObject(&Fixture.World, NULL, &Parent))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Parent, (Vector3){ 4.0f, 1.0f, 1.0f }))
        && ((ModelComponent = WorldModelComponent_FromComponent(AddChildComponent(run, &Fixture.World, Parent,
            WorldComponentType_Model, Vector3Zero()))) != NULL)
        && TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(OwnerOf(ModelComponent), (Vector3){ 0.0f, PI / 4.0f, 0.0f }))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(ModelComponent, (const unsigned char*)u8"box"))
        && CacheFixture_Sync(run, &Fixture))
    {
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, (Count == 1) && (Models[0].Radius >= ((4.0f * sqrtf(0.75f)) - 1e-4f)));
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

/* Adds the "steps" world model: three LODs switching at 10 and 20 m (40 m for the last, its draw distance), a 0.4 s
 * cross-fade and a hysteresis of 0.1, over three code-built box models. */
static bool AddSteps(TestRun* run, CacheFixture* fixture)
{
    BoundingBox UnitCube = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    return TestAssets_AddModel(run, &fixture->Assets, "step_lod1", UnitCube, NULL)
        && TestAssets_AddModel(run, &fixture->Assets, "step_lod2", UnitCube, NULL)
        && TestAssets_AddWorldModel(run, &fixture->Assets, "{\"name\": \"steps\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 10}, {\"model\": \"step_lod1\", \"max_distance\": 20}, {\"model\": \"step_lod2\", "
            "\"max_distance\": 40}], \"lod_fade_seconds\": 0.4, \"lod_hysteresis\": 0.1}");
}

/* Runs one frame's LOD update with the camera at @p camera. */
static void UpdateLods(CacheFixture* fixture, Vector3 camera, float zoom, float scale, float deltaSeconds, Logger* logger)
{
    WorldRenderLodView View = { .CameraPosition = camera, .ZoomFactor = zoom, .DistanceScale = scale,
        .DeltaSeconds = deltaSeconds };
    WorldRenderCache_UpdateLods(&fixture->Cache, &View, logger);
}

/* The entry of a model component (NULL when absent). */
static const WorldRenderModel* EntryOf(const CacheFixture* fixture, const WorldModelComponent* component)
{
    size_t Count = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&fixture->Cache, &Count);
    for (size_t Index = 0; Index < Count; Index++)
    {
        if (Models[Index].Component == component)
        {
            return &Models[Index];
        }
    }
    return NULL;
}

/* Whether an entry shows @p lod, not fading. */
static bool ShowsLod(const CacheFixture* fixture, const WorldModelComponent* component, size_t lod)
{
    const WorldRenderModel* Entry = EntryOf(fixture, component);
    return (Entry != NULL) && Entry->Lod.IsStarted && !WorldLodState_IsFading(&Entry->Lod)
        && (Entry->Lod.CurrentLod == lod);
}

/* Each drawn entry's LOD follows the camera's distance to its bounding sphere's centre: the first update takes it at
 * once, a change cross-fades over the world model's fade time by the frames' seconds (the shadow LOD at once, the
 * G-buffer's at half), the hysteresis holds an object at a boundary, and an entry carried over a re-match keeps its
 * state. */
static void LodsFollowTheDistanceAndCrossFade(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelComponent* Near = NULL;
    WorldModelComponent* Middle = NULL;
    WorldModelComponent* Far = NULL;
    WorldModelComponent* Farthest = NULL;
    Vector3 Unit = { 1.0f, 1.0f, 1.0f };
    if (!CacheFixture_Construct(run, &Fixture) || !AddSteps(run, &Fixture)
        || ((Near = AddModel(run, &Fixture.World, "steps", (Vector3){ 0.0f, 0.0f, 5.0f }, Unit)) == NULL)
        || ((Middle = AddModel(run, &Fixture.World, "steps", (Vector3){ 0.0f, 0.0f, 15.0f }, Unit)) == NULL)
        || ((Far = AddModel(run, &Fixture.World, "steps", (Vector3){ 0.0f, 0.0f, 30.0f }, Unit)) == NULL)
        || ((Farthest = AddModel(run, &Fixture.World, "steps", (Vector3){ 0.0f, 0.0f, 100.0f }, Unit)) == NULL)
        || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    Vector3 Eye = { 0.0f, 0.0f, 0.0f };
    UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, NULL);
    TEST_CHECK(run, ShowsLod(&Fixture, Near, 0U) && ShowsLod(&Fixture, Middle, 1U) && ShowsLod(&Fixture, Far, 2U)
        && ShowsLod(&Fixture, Farthest, 2U));

    // 6 m back: the near box is 11 m away, past 10 x 1.1, and cross-fades to LOD 1; the middle one (21 m) stays within
    // its band (below 22 m).
    Eye.z = -6.0f;
    UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, NULL);
    const WorldRenderModel* Entry = EntryOf(&Fixture, Near);
    TEST_CHECK(run, (Entry != NULL) && WorldLodState_IsFading(&Entry->Lod) && (Entry->Lod.CurrentLod == 0U)
        && (Entry->Lod.TargetLod == 1U) && IsNear(Entry->Lod.Progress, 0.25f)
        && (WorldLodState_GetShadowLod(&Entry->Lod) == 1U) && (WorldLodState_GetSolidLod(&Entry->Lod) == 0U));
    TEST_CHECK(run, ShowsLod(&Fixture, Middle, 1U));

    // A component added meanwhile re-matches the entries; the fading one keeps its fade.
    TEST_CHECK(run, AddModel(run, &Fixture.World, "box", (Vector3){ 50.0f, 0.0f, 0.0f }, Unit) != NULL);
    if (CacheFixture_Sync(run, &Fixture))
    {
        Entry = EntryOf(&Fixture, Near);
        TEST_CHECK(run, (Entry != NULL) && WorldLodState_IsFading(&Entry->Lod) && IsNear(Entry->Lod.Progress, 0.25f));
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, NULL);
        Entry = EntryOf(&Fixture, Near);
        TEST_CHECK(run, (Entry != NULL) && IsNear(Entry->Lod.Progress, 0.5f)
            && (WorldLodState_GetSolidLod(&Entry->Lod) == 1U));
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, NULL);
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, NULL);
        TEST_CHECK(run, ShowsLod(&Fixture, Near, 1U));
    }

    // Standing at the band's edge while the camera wobbles: no switch back.
    const float Wobble[] = { -5.2f, -4.9f, -5.3f, -4.95f };
    bool IsSteady = true;
    for (size_t Index = 0U; Index < (sizeof(Wobble) / sizeof(Wobble[0])); Index++)
    {
        Eye.z = Wobble[Index]; // 10.2, 9.9, 10.3, 9.95 m: all above 10 x 0.9
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, NULL);
        IsSteady = IsSteady && ShowsLod(&Fixture, Near, 1U);
    }
    TEST_CHECK(run, IsSteady);
    CacheFixture_Deconstruct(run, &Fixture);
}

/* The switch distances in effect move with the zoom (a narrower view picks finer LODs), the player's lod_distance_scale,
 * the placement's multiplier, its own list (while it fits the world model: otherwise the world model's distances, with
 * one warning when that starts) and its forced LOD, which still cross-fades when it changes. */
static void ThePlacementAndThePlayerMoveTheSwitches(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, CACHE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "cache_lods", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    CacheFixture Fixture;
    WorldModelComponent* Plain = NULL;
    WorldModelComponent* Placement = NULL;
    Vector3 Unit = { 1.0f, 1.0f, 1.0f };
    Vector3 Eye = { 0.0f, 0.0f, 0.0f };
    if (CacheFixture_Construct(run, &Fixture) && AddSteps(run, &Fixture)
        && ((Plain = AddModel(run, &Fixture.World, "steps", (Vector3){ 0.0f, 0.0f, 15.0f }, Unit)) != NULL)
        && ((Placement = AddModel(run, &Fixture.World, "steps", (Vector3){ 1.0f, 0.0f, 15.0f }, Unit)) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        // Each case starts afresh (the entries hidden for a frame), so the hysteresis does not hold the old LOD.
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.0f, &Log);
        TEST_CHECK(run, ShowsLod(&Fixture, Plain, 1U));
        UpdateLods(&Fixture, Eye, 0.65f, 1.0f, 0.0f, &Log); // zoomed in a little: 9.75 m, the hysteresis holds LOD 1
        TEST_CHECK(run, ShowsLod(&Fixture, Plain, 1U));
        UpdateLods(&Fixture, Eye, 0.25f, 1.0f, 0.0f, &Log); // 3.75 m: below 10 x 0.9
        const WorldRenderModel* Entry = EntryOf(&Fixture, Plain);
        TEST_CHECK(run, (Entry != NULL) && (WorldLodState_GetDestinationLod(&Entry->Lod) == 0U));
        UpdateLods(&Fixture, Eye, 1.0f, 0.5f, 0.0f, &Log); // half the quality: switches at 5 and 10 m
        Entry = EntryOf(&Fixture, Plain);
        TEST_CHECK(run, (Entry != NULL) && (WorldLodState_GetDestinationLod(&Entry->Lod) == 2U));

        // The placement's multiplier keeps its LODs twice as far: 15 m is LOD 0 (below 20 m).
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(Placement), false));
        CacheFixture_Sync(run, &Fixture);
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.0f, &Log);
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(Placement), true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistanceMultiplier(Placement, 2.0f));
        CacheFixture_Sync(run, &Fixture);
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.0f, &Log);
        TEST_CHECK(run, ShowsLod(&Fixture, Placement, 0U));

        // Its own list (3, 6, 50 m, times 2): 15 m is LOD 2, at once (the hidden frame started it afresh).
        const float Own[] = { 3.0f, 6.0f, 50.0f };
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, Own, 3U));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_LodDistances, true));
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(Placement), false));
        CacheFixture_Sync(run, &Fixture);
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.0f, &Log);
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(Placement), true));
        CacheFixture_Sync(run, &Fixture);
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.0f, &Log);
        TEST_CHECK(run, ShowsLod(&Fixture, Placement, 2U));

        // A list that no longer fits: the world model's distances (times 2: LOD 0 at 15 m, cross-fading from LOD 2),
        // warned about once over several frames; fitting again and failing again warns again.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, Own, 2U));
        CacheFixture_Sync(run, &Fixture);
        for (int Frame = 0; Frame < 3; Frame++)
        {
            UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, &Log);
        }
        Entry = EntryOf(&Fixture, Placement);
        TEST_CHECK(run, (Entry != NULL) && (WorldLodState_GetDestinationLod(&Entry->Lod) == 0U)
            && Entry->IsLodListRefused);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, Own, 3U));
        CacheFixture_Sync(run, &Fixture);
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, &Log);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, Own, 1U));
        CacheFixture_Sync(run, &Fixture);
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, &Log);

        // A forced LOD, and a change of it, cross-fade.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_LodDistances, false));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsLodForced(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetForcedLod(Placement, 2U));
        CacheFixture_Sync(run, &Fixture);
        for (int Frame = 0; Frame < 5; Frame++)
        {
            UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, &Log);
        }
        TEST_CHECK(run, ShowsLod(&Fixture, Placement, 2U));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetForcedLod(Placement, 1U));
        CacheFixture_Sync(run, &Fixture);
        UpdateLods(&Fixture, Eye, 1.0f, 1.0f, 0.1f, &Log);
        Entry = EntryOf(&Fixture, Placement);
        TEST_CHECK(run, (Entry != NULL) && WorldLodState_IsFading(&Entry->Lod) && (Entry->Lod.CurrentLod == 2U)
            && (Entry->Lod.TargetLod == 1U));
    }
    CacheFixture_Deconstruct(run, &Fixture);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "uses its world model \"steps\"'s LOD distances: its own "
        "lod_distances holds 2, the world model has 3 LODs.") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "lod_distances holds 1,") == 1U);
    Memory_Free(Directory._data);
}

/* An object that is not drawn (hidden, or under a disabled parent) forgets its LOD: drawn again, it appears at the LOD
 * its distance picks, at once. A world model resolved again (a rename) starts afresh too. */
static void AnObjectDrawnAgainAppearsAtItsLod(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelComponent* Component = NULL;
    Vector3 Unit = { 1.0f, 1.0f, 1.0f };
    if (!CacheFixture_Construct(run, &Fixture) || !AddSteps(run, &Fixture)
        || ((Component = AddModel(run, &Fixture.World, "steps", (Vector3){ 0.0f, 0.0f, 5.0f }, Unit)) == NULL)
        || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f, 0.1f, NULL);
    TEST_CHECK(run, ShowsLod(&Fixture, Component, 0U));

    TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(Component), false));
    CacheFixture_Sync(run, &Fixture);
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, -30.0f }, 1.0f, 1.0f, 0.1f, NULL);
    const WorldRenderModel* Entry = EntryOf(&Fixture, Component);
    TEST_CHECK(run, (Entry != NULL) && !Entry->IsDrawn && !Entry->Lod.IsStarted);
    TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(Component), true));
    CacheFixture_Sync(run, &Fixture);
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, -30.0f }, 1.0f, 1.0f, 0.1f, NULL);
    TEST_CHECK(run, ShowsLod(&Fixture, Component, 2U)); // 35 m: no fade from LOD 0

    // Renamed to a one-LOD world model and back: each resolution starts afresh, at its LOD.
    TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(Component, (const unsigned char*)u8"box"));
    CacheFixture_Sync(run, &Fixture);
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, -30.0f }, 1.0f, 1.0f, 0.1f, NULL);
    TEST_CHECK(run, ShowsLod(&Fixture, Component, 0U));
    TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(Component, (const unsigned char*)u8"steps"));
    CacheFixture_Sync(run, &Fixture);
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, -30.0f }, 1.0f, 1.0f, 0.1f, NULL);
    TEST_CHECK(run, ShowsLod(&Fixture, Component, 2U));
    CacheFixture_Deconstruct(run, &Fixture);
}

/* A camera that turns back within a fade turns the fade around: the same two LODs keep their pixels while the share
 * runs back, the shadows draw the LOD it heads back to, and the hysteresis measures from that LOD, so standing inside
 * the band keeps it heading there. */
static void ACameraTurningBackTurnsTheFadeAround(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelComponent* Near = NULL;
    Vector3 Unit = { 1.0f, 1.0f, 1.0f };
    if (!CacheFixture_Construct(run, &Fixture) || !AddSteps(run, &Fixture)
        || ((Near = AddModel(run, &Fixture.World, "steps", (Vector3){ 0.0f, 0.0f, 5.0f }, Unit)) == NULL)
        || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f, 0.1f, NULL);
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, -6.0f }, 1.0f, 1.0f, 0.1f, NULL); // 11 m: a quarter of the way to LOD 1

    // Back at 5 m within the fade: it runs back toward LOD 0 from where it was (0.05 s of the 0.4 s fade).
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f, 0.05f, NULL);
    const WorldRenderModel* Entry = EntryOf(&Fixture, Near);
    TEST_CHECK(run, (Entry != NULL) && WorldLodState_IsFading(&Entry->Lod) && Entry->Lod.IsReturning
        && (Entry->Lod.CurrentLod == 0U) && (Entry->Lod.TargetLod == 1U) && IsNear(Entry->Lod.Progress, 0.125f)
        && (WorldLodState_GetDestinationLod(&Entry->Lod) == 0U) && (WorldLodState_GetShadowLod(&Entry->Lod) == 0U));

    // At 10 m, inside the band (9 to 11 m): measured from LOD 0, it keeps heading back, and then arrives.
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, -5.0f }, 1.0f, 1.0f, 0.02f, NULL);
    Entry = EntryOf(&Fixture, Near);
    TEST_CHECK(run, (Entry != NULL) && Entry->Lod.IsReturning && (WorldLodState_GetDestinationLod(&Entry->Lod) == 0U)
        && IsNear(Entry->Lod.Progress, 0.075f));
    UpdateLods(&Fixture, (Vector3){ 0.0f, 0.0f, -5.0f }, 1.0f, 1.0f, 0.1f, NULL);
    TEST_CHECK(run, ShowsLod(&Fixture, Near, 0U));
    CacheFixture_Deconstruct(run, &Fixture);
}

static void ResetForgetsEveryEntry(TestRun* run)
{
    CacheFixture Fixture;
    if (CacheFixture_Construct(run, &Fixture)
        && (AddModel(run, &Fixture.World, "box", (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        WorldRenderCache_Reset(&Fixture.Cache);
        size_t Count = 1;
        WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, Count == 0);
        if (CacheFixture_Sync(run, &Fixture))
        {
            const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && (Models[0].Asset != NULL));
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void WorldRenderCacheTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "resolves_models_and_computes_world_bounds", ResolvesModelsAndComputesWorldBounds },
        { "failed_model_stays_unresolved_until_renamed", FailedModelStaysUnresolvedUntilRenamed },
        { "entries_follow_components_across_adds_and_removals", EntriesFollowComponentsAcrossAddsAndRemovals },
        { "reused_id_with_another_model_is_resolved_again", ReusedIdWithAnotherModelIsResolvedAgain },
        { "another_world_is_resolved_from_scratch", AnotherWorldIsResolvedFromScratch },
        { "lists_drawable_sprites_and_every_light", ListsDrawableSpritesAndEveryLight },
        { "disabled_and_hidden_objects_are_not_drawn", DisabledAndHiddenObjectsAreNotDrawn },
        { "entries_carry_the_settings_in_effect", EntriesCarryTheSettingsInEffect },
        { "swapping_the_world_model_keeps_the_object", SwappingTheWorldModelKeepsTheObject },
        { "variants_resolve_by_name_and_unknown_ones_warn_once", VariantsResolveByNameAndUnknownOnesWarnOnce },
        { "bounds_and_the_drawn_lod_come_from_the_world_model", BoundsAndTheDrawnLodComeFromTheWorldModel },
        { "a_replaced_component_gets_its_own_variant", AReplacedComponentGetsItsOwnVariant },
        { "derived_bounds_compose_the_import_with_the_placement", DerivedBoundsComposeTheImportWithThePlacement },
        { "sprite_entries_carry_their_quad_and_settings", SpriteEntriesCarryTheirQuadAndSettings },
        { "sprite_entries_carry_their_draw_distance_and_follow_a_swap",
            SpriteEntriesCarryTheirDrawDistanceAndFollowASwap },
        { "reset_forgets_every_entry", ResetForgetsEveryEntry },
        { "children_are_drawn_where_their_parents_put_them", ChildrenAreDrawnWhereTheirParentsPutThem },
        { "hidden_or_disabled_parents_hide_their_subtree", HiddenOrDisabledParentsHideTheirSubtree },
        { "bounds_stay_conservative_under_shear", BoundsStayConservativeUnderShear },
        { "lods_follow_the_distance_and_cross_fade", LodsFollowTheDistanceAndCrossFade },
        { "the_placement_and_the_player_move_the_switches", ThePlacementAndThePlayerMoveTheSwitches },
        { "an_object_drawn_again_appears_at_its_lod", AnObjectDrawnAgainAppearsAtItsLod },
        { "a_camera_turning_back_turns_the_fade_around", ACameraTurningBackTurnsTheFadeAround },
    };
    TestRun_RunSuite(run, "WorldRenderCache", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
