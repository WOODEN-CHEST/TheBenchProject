#include "TestSuites.h"
#include "TestAssets.h"
#include "WorldRenderCache.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "WorldLightComponent.h"
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

/* Creates an object at @p position with @p scale and a model component naming @p assetName; returns the component
 * (world-owned), or NULL with a failed check. */
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
    if (!TEST_CHECK_SUCCESS(run, WorldModelComponent_SetModel(ModelComponent, (const unsigned char*)assetName)))
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
            TEST_CHECK(run, Models[0].HasOutline && !Models[0].OmitsPixelation && Models[0].CastsShadows
                && (Models[0].ShadowTier == WorldShadowTier_Both));
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

        // Another property changes the revision, but the name is the same: not retried.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasOutline(ModelComponent, false));
        if (CacheFixture_Sync(run, &Fixture))
        {
            Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
            TEST_CHECK(run, (Count == 1) && (Models[0].Asset == NULL) && !Models[0].HasOutline);
        }

        // Renaming changes the name, so the next sync resolves the new model.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetModel(ModelComponent, (const unsigned char*)u8"box"));
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
            && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetModel(WorldModelComponent_FromComponent(Component),
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
        || (AddModel(run, &Fixture.World, "box", Origin, One) == NULL) || !CacheFixture_Sync(run, &Fixture))
    {
        CacheFixture_Deconstruct(run, &Fixture);
        return;
    }

    // A second world whose first object has the same id (1) and revisions, but another model.
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
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetSpriteAnimation(Bound, (const unsigned char*)u8"walk"))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetSpriteAnimation(Empty, (const unsigned char*)u8"missing"))
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
        TEST_CHECK(run, !WorldRenderCache_HasCrispObjects(&Fixture.Cache));

        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOmitsPixelation(Bound, true));
        if (CacheFixture_Sync(run, &Fixture))
        {
            TEST_CHECK(run, WorldRenderCache_HasCrispObjects(&Fixture.Cache));
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
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetSpriteAnimation(Sprite, (const unsigned char*)u8"walk"))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOmitsPixelation(ModelComponent, true))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(OwnerOf(ModelComponent), false))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(WorldComponent_GetOwner(&Sprite->Base), false))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(WorldComponent_GetOwner(&Light->Base), false))
        && CacheFixture_Sync(run, &Fixture))
    {
        // The hidden model keeps its entry and its resolved model, but is not drawn (nor crisp).
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, (Count == 1) && (Models[0].Asset != NULL) && !Models[0].IsDrawn);
        TEST_CHECK(run, !WorldRenderCache_HasCrispObjects(&Fixture.Cache));
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
            TEST_CHECK(run, (Count == 1) && Models[0].IsDrawn);
            TEST_CHECK(run, WorldRenderCache_HasCrispObjects(&Fixture.Cache));
            WorldRenderCache_GetSprites(&Fixture.Cache, &Count);
            TEST_CHECK(run, Count == 1);
            WorldRenderCache_GetLights(&Fixture.Cache, &Count);
            TEST_CHECK(run, Count == 1);
        }
        TEST_CHECK(run, Fixture.BoxCounters.LoadCount == 1);
    }
    CacheFixture_Deconstruct(run, &Fixture);
}

static void EntriesCarryTheShadowSettings(TestRun* run)
{
    CacheFixture Fixture;
    WorldModelComponent* ModelComponent = NULL;
    if (CacheFixture_Construct(run, &Fixture)
        && ((ModelComponent = AddModel(run, &Fixture.World, "box", (Vector3){ 0.0f, 0.0f, 0.0f },
            (Vector3){ 1.0f, 1.0f, 1.0f })) != NULL)
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetCastsShadows(ModelComponent, false))
        && TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowTier(ModelComponent, WorldShadowTier_Near))
        && CacheFixture_Sync(run, &Fixture))
    {
        size_t Count = 0;
        const WorldRenderModel* Models = WorldRenderCache_GetModels(&Fixture.Cache, &Count);
        TEST_CHECK(run, (Count == 1) && Models[0].IsDrawn && !Models[0].CastsShadows
            && (Models[0].ShadowTier == WorldShadowTier_Near));
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
        { "entries_follow_components_across_adds_and_removals", EntriesFollowComponentsAcrossAddsAndRemovals },
        { "reused_id_with_another_model_is_resolved_again", ReusedIdWithAnotherModelIsResolvedAgain },
        { "another_world_is_resolved_from_scratch", AnotherWorldIsResolvedFromScratch },
        { "lists_drawable_sprites_and_every_light", ListsDrawableSpritesAndEveryLight },
        { "disabled_and_hidden_objects_are_not_drawn", DisabledAndHiddenObjectsAreNotDrawn },
        { "entries_carry_the_shadow_settings", EntriesCarryTheShadowSettings },
        { "reset_forgets_every_entry", ResetForgetsEveryEntry },
    };
    TestRun_RunSuite(run, "WorldRenderCache", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
