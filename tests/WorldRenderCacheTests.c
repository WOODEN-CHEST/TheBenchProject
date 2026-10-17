#include "TestSuites.h"
#include "TestAssets.h"
#include "WorldRenderCache.h"
#include "World.h"
#include "WorldModelObject.h"
#include "WorldSpriteObject.h"
#include "WorldLight.h"
#include <math.h>


// Types.
/* An asset fixture with a "box" model (unit cube bounds) and a "walk" animation, a world built with it, and a
 * render cache whose models are held under their own user. */
typedef struct CacheFixtureStruct
{
    TestAssetFixture Assets;
    TestAssetCounters BoxCounters;
    World World;
    WorldRenderCache Cache;
    AssetUserID ModelUser;
    bool IsWorldConstructed;
} CacheFixture;


// Static functions.
static bool CacheFixture_Construct(TestRun* run, CacheFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    BoundingBox UnitCube = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    if (!TestAssetFixture_Construct(run, &self->Assets)
        || !TestAssets_AddModel(run, &self->Assets, "box", UnitCube, &self->BoxCounters)
        || !TestAssets_AddSpriteAnimation(run, &self->Assets, "walk", NULL)
        || !TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(self->Assets.Manager, &self->ModelUser))
        || !TEST_CHECK_SUCCESS(run, World_Construct2(&self->World, self->Assets.Manager, NULL)))
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
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&self->World));
    }
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

static bool CacheFixture_Sync(TestRun* run, CacheFixture* self)
{
    return TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&self->Cache, &self->World, self->Assets.Manager,
        self->ModelUser, NULL));
}

/* Adds a model object at @p position with @p scale; returns it (world-owned) or NULL with a failed check. */
static WorldModelObject* AddModel(TestRun* run, World* world, const char* assetName, Vector3 position, Vector3 scale)
{
    WorldModelObject* ModelObject = NULL;
    if (!TEST_CHECK_SUCCESS(run, WorldModelObject_Create(NULL, (const unsigned char*)assetName, &ModelObject)))
    {
        return NULL;
    }
    WorldObject* Base = WorldModelObject_AsObject(ModelObject);
    if (!TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Base, position))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Base, scale))
        || !TEST_CHECK_SUCCESS(run, World_AddObject(world, Base)))
    {
        WorldObject_Destroy(Base);
        return NULL;
    }
    return ModelObject;
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
            TEST_CHECK(run, Models[0].IsResolved);
        }
        TEST_CHECK(run, Fixture.BoxCounters.LoadCount == 1);

        // A second sync reuses the resolved entry (no new load, same asset).
        const GameModel* FirstAsset = (Count == 1) ? Models[0].Asset : NULL;
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
    WorldModelObject* ModelObject = NULL;
    if (CacheFixture_Construct(run, &Fixture)
        && ((ModelObject = AddModel(run, &Fixture.World, "missing", (Vector3){ 0.0f, 0.0f, 0.0f },
            (Vector3){ 1.0f, 1.0f, 1.0f })) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        if (TEST_CHECK(run, Count == 1))
        {
            TEST_CHECK(run, Models[0].IsResolved && (Models[0].Asset == NULL));
        }

        // Renaming advances the object's revision, so the next sync resolves the new model.
        TEST_CHECK_SUCCESS(run, WorldModelObject_SetModelAssetName(ModelObject, (const unsigned char*)u8"box"));
        if (CacheFixture_Sync(run, &Fixture))
        {
            Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && (Models[0].Asset != NULL));
            uint32_t Revision = WorldModelObject_GetAssetRevision(ModelObject);
            TEST_CHECK(run, (Count == 1) && (Models[0].AssetRevision == Revision));
        }
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

static void EntriesFollowObjectsAcrossAddsAndRemovals(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelObject* First = NULL;
    WorldModelObject* Second = NULL;
    Vector3 One = { 1.0f, 1.0f, 1.0f };
    if (CacheFixture_Construct(run, &Fixture)
        && ((First = AddModel(run, &Fixture.World, "box", (Vector3){ 0.0f, 0.0f, 0.0f }, One)) != NULL)
        && ((Second = AddModel(run, &Fixture.World, "missing", (Vector3){ 5.0f, 0.0f, 0.0f }, One)) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        uint64_t SecondId = WorldObject_GetId(WorldModelObject_AsObject(Second));
        WorldModelObject* Third = AddModel(run, &Fixture.World, "box", (Vector3){ 9.0f, 0.0f, 0.0f }, One);
        uint64_t FirstId = WorldObject_GetId(WorldModelObject_AsObject(First));
        TEST_CHECK_SUCCESS(run, World_RemoveObjectById(&Fixture.World, FirstId));

        if ((Third != NULL) && CacheFixture_Sync(run, &Fixture))
        {
            size_t Count = 0;
            const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            if (TEST_CHECK(run, Count == 2))
            {
                TEST_CHECK(run, (Models[0].ObjectId == SecondId) && (Models[0].Object == Second));
                TEST_CHECK(run, Models[0].IsResolved && (Models[0].Asset == NULL));
                TEST_CHECK(run, (Models[1].Object == Third) && (Models[1].Asset != NULL));
                TEST_CHECK(run, IsNear(Models[1].Center.x, 9.0f));
            }
        }
        // The box asset is held once under the model user, however many objects use it.
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
    WorldModelObject* Box = NULL;
    if (CacheFixture_Construct(run, &Fixture)
        && TestAssets_AddModel(run, &Fixture.Assets, "crate", BigCube, &CrateCounters)
        && ((Box = AddModel(run, &Fixture.World, "box", (Vector3){ 0.0f, 0.0f, 0.0f }, One)) != NULL)
        && CacheFixture_Sync(run, &Fixture))
    {
        // An editor undo or a re-applied save: the id comes back with another object, naming another model, at
        // revision 0 like every new object. The old entry must not be reused.
        uint64_t Id = WorldObject_GetId(WorldModelObject_AsObject(Box));
        TEST_CHECK_SUCCESS(run, World_RemoveObjectById(&Fixture.World, Id));
        WorldModelObject* Crate = NULL;
        if (TEST_CHECK_SUCCESS(run, WorldModelObject_Create(NULL, (const unsigned char*)u8"crate", &Crate)))
        {
            if (!TEST_CHECK_SUCCESS(run, World_AddObjectWithId(&Fixture.World, WorldModelObject_AsObject(Crate), Id)))
            {
                WorldObject_Destroy(WorldModelObject_AsObject(Crate));
            }
            else if (CacheFixture_Sync(run, &Fixture))
            {
                size_t Count = 0;
                const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
                if (TEST_CHECK(run, Count == 1))
                {
                    TEST_CHECK(run, Models[0].Object == Crate);
                    TEST_CHECK(run, IsNear(Models[0].Radius, sqrtf(75.0f)));
                }
                TEST_CHECK(run, CrateCounters.LoadCount == 1);
            }
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
        || (AddModel(run, &Fixture.World, "box", Origin, One) == NULL) || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }

    // A second world whose first object has the same id (1) and revision (0), but another model.
    World Other;
    if (TEST_CHECK_SUCCESS(run, World_Construct2(&Other, Fixture.Assets.Manager, NULL)))
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

static void ListsDrawableSpritesAndEveryLight(TestRun* run)
{
    CacheFixture Fixture;
    if (!CacheFixture_Construct(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteObject* Bound = NULL;
    WorldSpriteObject* Empty = NULL;
    WorldLight* Light = NULL;
    bool IsBuilt = TEST_CHECK_SUCCESS(run, WorldSpriteObject_Create(NULL, (const unsigned char*)u8"walk", &Bound))
        && TEST_CHECK_SUCCESS(run, World_AddObject(&Fixture.World, WorldSpriteObject_AsObject(Bound)))
        && TEST_CHECK_SUCCESS(run, WorldSpriteObject_Create(NULL, (const unsigned char*)u8"missing", &Empty))
        && TEST_CHECK_SUCCESS(run, World_AddObject(&Fixture.World, WorldSpriteObject_AsObject(Empty)))
        && TEST_CHECK_SUCCESS(run, WorldLight_Create(NULL, &Light))
        && TEST_CHECK_SUCCESS(run, WorldLight_SetIntensity(Light, 0.0f)) // dark lights are still listed
        && TEST_CHECK_SUCCESS(run, World_AddObject(&Fixture.World, WorldLight_AsObject(Light)));
    if (IsBuilt && CacheFixture_Sync(run, &Fixture))
    {
        size_t SpriteCount = 0;
        const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&Fixture.Cache, &SpriteCount);
        if (TEST_CHECK(run, SpriteCount == 1))
        {
            TEST_CHECK(run, Sprites[0].Object == Bound);
        }
        size_t LightCount = 0;
        const WorldRenderLight* Lights = WorldRenderCache_GetLights(&Fixture.Cache, &LightCount);
        TEST_CHECK(run, (LightCount == 1) && (Lights[0].Light == Light) && (Lights[0].Intensity == 0.0f));
        TEST_CHECK(run, !WorldRenderCache_HasCrispObjects(&Fixture.Cache));

        Bound->OmitPixelation = true;
        if (CacheFixture_Sync(run, &Fixture))
        {
            TEST_CHECK(run, WorldRenderCache_HasCrispObjects(&Fixture.Cache));
        }
    }
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
        { "entries_follow_objects_across_adds_and_removals", EntriesFollowObjectsAcrossAddsAndRemovals },
        { "reused_id_with_another_model_is_resolved_again", ReusedIdWithAnotherModelIsResolvedAgain },
        { "another_world_is_resolved_from_scratch", AnotherWorldIsResolvedFromScratch },
        { "lists_drawable_sprites_and_every_light", ListsDrawableSpritesAndEveryLight },
        { "reset_forgets_every_entry", ResetForgetsEveryEntry },
    };
    TestRun_RunSuite(run, "WorldRenderCache", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
