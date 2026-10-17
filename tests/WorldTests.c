#include "TestSuites.h"
#include "TestAssets.h"
#include "World.h"
#include "WorldModelObject.h"
#include "WorldSpriteObject.h"
#include "WorldLight.h"
#include "SpriteAnimation.h"
#include <string.h>


// Static functions.
/* Creates a sprite object with the given animation name and adds it to the world. Returns it (world-owned), or
 * NULL with a failed check. */
static WorldSpriteObject* AddSprite(TestRun* run, World* world, const char* animationName)
{
    WorldSpriteObject* Sprite = NULL;
    if (!TEST_CHECK_SUCCESS(run, WorldSpriteObject_Create((const unsigned char*)u8"sprite",
        (const unsigned char*)animationName, &Sprite)))
    {
        return NULL;
    }
    if (!TEST_CHECK_SUCCESS(run, World_AddObject(world, WorldSpriteObject_AsObject(Sprite))))
    {
        WorldObject_Destroy(WorldSpriteObject_AsObject(Sprite));
        return NULL;
    }
    return Sprite;
}

static bool IsBound(WorldSpriteObject* sprite)
{
    SpriteAnimationInstance* Instance = WorldSpriteObject_GetAnimationInstance(sprite);
    return SpriteAnimationInstance_HasSource(Instance) && (SpriteAnimationInstance_GetFrameCount(Instance) == 1);
}

static void ObjectSetVersionChangesOnAddAndRemove(TestRun* run)
{
    World TestWorld;
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(&TestWorld)))
    {
        return;
    }

    uint64_t Initial = World_GetObjectSetVersion(&TestWorld);
    WorldLight* Light = NULL;
    if (TEST_CHECK_SUCCESS(run, WorldLight_Create(NULL, &Light)))
    {
        if (TEST_CHECK_SUCCESS(run, World_AddObject(&TestWorld, WorldLight_AsObject(Light))))
        {
            uint64_t AfterAdd = World_GetObjectSetVersion(&TestWorld);
            TEST_CHECK(run, AfterAdd != Initial);

            // Changing an object's properties is not a change of the object set.
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(WorldLight_AsObject(Light), (Vector3){ 1.0f, 2.0f, 3.0f }));
            TEST_CHECK(run, World_GetObjectSetVersion(&TestWorld) == AfterAdd);

            TEST_CHECK_SUCCESS(run, World_RemoveObjectById(&TestWorld, WorldObject_GetId(WorldLight_AsObject(Light))));
            TEST_CHECK(run, World_GetObjectSetVersion(&TestWorld) != AfterAdd);
        }
        else
        {
            WorldObject_Destroy(WorldLight_AsObject(Light));
        }
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
}

static void WorldWithoutAssetsLeavesSpritesEmpty(TestRun* run)
{
    World TestWorld;
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(&TestWorld)))
    {
        return;
    }
    WorldSpriteObject* Sprite = AddSprite(run, &TestWorld, "walk");
    if (Sprite != NULL)
    {
        TEST_CHECK(run, !SpriteAnimationInstance_HasSource(WorldSpriteObject_GetAnimationInstance(Sprite)));
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
}

static void AddedSpriteIsBoundAndReleasedWithTheWorld(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Counters = { 0 };
    World TestWorld;
    if (!TestAssetFixture_Construct(run, &Fixture) || !TestAssets_AddSpriteAnimation(run, &Fixture, "walk", &Counters)
        || !TEST_CHECK_SUCCESS(run, World_Construct2(&TestWorld, Fixture.Manager, NULL)))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteObject* First = AddSprite(run, &TestWorld, "walk");
    WorldSpriteObject* Second = AddSprite(run, &TestWorld, "walk");
    if ((First != NULL) && (Second != NULL))
    {
        TEST_CHECK(run, IsBound(First));
        TEST_CHECK(run, IsBound(Second));
        TEST_CHECK(run, SpriteAnimationInstance_GetSource(WorldSpriteObject_GetAnimationInstance(First))
            == SpriteAnimationInstance_GetSource(WorldSpriteObject_GetAnimationInstance(Second)));
        TEST_CHECK(run, Counters.LoadCount == 1); // shared: one load under the world's user
    }

    // The world's user holds the animation until the world is destroyed; then it is released (and unloaded,
    // since nothing else holds it).
    TEST_CHECK(run, Counters.UnloadCount == 0);
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
    TEST_CHECK(run, Counters.UnloadCount == 1);
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void MissingAnimationLeavesSpriteEmptyWithoutFailingTheAdd(TestRun* run)
{
    TestAssetFixture Fixture;
    World TestWorld;
    if (!TestAssetFixture_Construct(run, &Fixture) || !TEST_CHECK_SUCCESS(run, World_Construct2(&TestWorld,
        Fixture.Manager, NULL)))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteObject* Sprite = AddSprite(run, &TestWorld, "missing");
    if (Sprite != NULL)
    {
        TEST_CHECK(run, WorldObject_GetId(WorldSpriteObject_AsObject(Sprite)) != 0);
        TEST_CHECK(run, !SpriteAnimationInstance_HasSource(WorldSpriteObject_GetAnimationInstance(Sprite)));
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void RenamingThroughTheWorldRebinds(TestRun* run)
{
    TestAssetFixture Fixture;
    World TestWorld;
    if (!TestAssetFixture_Construct(run, &Fixture) || !TestAssets_AddSpriteAnimation(run, &Fixture, "walk", NULL)
        || !TEST_CHECK_SUCCESS(run, World_Construct2(&TestWorld, Fixture.Manager, NULL)))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteObject* Sprite = AddSprite(run, &TestWorld, "missing");
    if (Sprite != NULL)
    {
        uint64_t Id = WorldObject_GetId(WorldSpriteObject_AsObject(Sprite));
        TEST_CHECK(run, !IsBound(Sprite));
        if (TEST_CHECK_SUCCESS(run, World_SetSpriteAnimationAssetName(&TestWorld, Id, (const unsigned char*)u8"walk")))
        {
            TEST_CHECK(run, IsBound(Sprite));
            TEST_CHECK(run, strcmp((const char*)WorldSpriteObject_GetSpriteAnimationAssetName(Sprite), "walk") == 0);
        }
        TEST_CHECK_SUCCESS(run, World_SetSpriteAnimationAssetName(&TestWorld, Id, NULL));
        TEST_CHECK(run, !SpriteAnimationInstance_HasSource(WorldSpriteObject_GetAnimationInstance(Sprite)));
        TEST_CHECK(run, WorldSpriteObject_GetSpriteAnimationAssetName(Sprite) == NULL);
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void SpriteSettersRefuseSpritesInAWorld(TestRun* run)
{
    World TestWorld;
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(&TestWorld)))
    {
        return;
    }

    // Before the add both setters work; afterwards the world owns the binding.
    WorldSpriteObject* Sprite = NULL;
    if (TEST_CHECK_SUCCESS(run, WorldSpriteObject_Create(NULL, NULL, &Sprite)))
    {
        TEST_CHECK_SUCCESS(run, WorldSpriteObject_SetSpriteAnimationAssetName(Sprite, (const unsigned char*)u8"a"));
        TEST_CHECK_SUCCESS(run, WorldSpriteObject_SetAnimation(Sprite, NULL));
        if (TEST_CHECK_SUCCESS(run, World_AddObject(&TestWorld, WorldSpriteObject_AsObject(Sprite))))
        {
            TEST_CHECK_ERROR(run, WorldSpriteObject_SetSpriteAnimationAssetName(Sprite, (const unsigned char*)u8"b"),
                ErrorCode_InvalidOperation);
            TEST_CHECK_ERROR(run, WorldSpriteObject_SetAnimation(Sprite, NULL), ErrorCode_InvalidOperation);
            TEST_CHECK(run, strcmp((const char*)WorldSpriteObject_GetSpriteAnimationAssetName(Sprite), "a") == 0);
        }
        else
        {
            WorldObject_Destroy(WorldSpriteObject_AsObject(Sprite));
        }
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
}

static void SpriteRenameValidatesItsTarget(TestRun* run)
{
    World TestWorld;
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(&TestWorld)))
    {
        return;
    }

    WorldModelObject* ModelObject = NULL;
    if (TEST_CHECK_SUCCESS(run, WorldModelObject_Create(NULL, NULL, &ModelObject)))
    {
        if (TEST_CHECK_SUCCESS(run, World_AddObject(&TestWorld, WorldModelObject_AsObject(ModelObject))))
        {
            uint64_t ModelId = WorldObject_GetId(WorldModelObject_AsObject(ModelObject));
            TEST_CHECK_ERROR(run, World_SetSpriteAnimationAssetName(&TestWorld, ModelId, NULL),
                ErrorCode_InvalidOperation);
        }
        else
        {
            WorldObject_Destroy(WorldModelObject_AsObject(ModelObject));
        }
    }
    TEST_CHECK_ERROR(run, World_SetSpriteAnimationAssetName(&TestWorld, 999, NULL), ErrorCode_InvalidOperation);
    TEST_CHECK_ERROR(run, World_SetSpriteAnimationAssetName(&TestWorld, 0, NULL), ErrorCode_IllegalArgument);
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&TestWorld));
}

static void ModelRenameAdvancesItsAssetRevision(TestRun* run)
{
    WorldModelObject* ModelObject = NULL;
    if (!TEST_CHECK_SUCCESS(run, WorldModelObject_Create(NULL, (const unsigned char*)u8"crate", &ModelObject)))
    {
        return;
    }
    uint32_t Initial = WorldModelObject_GetAssetRevision(ModelObject);
    TEST_CHECK_SUCCESS(run, WorldModelObject_SetModelAssetName(ModelObject, (const unsigned char*)u8"barrel"));
    TEST_CHECK(run, WorldModelObject_GetAssetRevision(ModelObject) != Initial);
    WorldObject_Destroy(WorldModelObject_AsObject(ModelObject));
}


// Public functions.
void WorldTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "object_set_version_changes_on_add_and_remove", ObjectSetVersionChangesOnAddAndRemove },
        { "world_without_assets_leaves_sprites_empty", WorldWithoutAssetsLeavesSpritesEmpty },
        { "added_sprite_is_bound_and_released_with_the_world", AddedSpriteIsBoundAndReleasedWithTheWorld },
        { "missing_animation_leaves_sprite_empty_without_failing_the_add",
            MissingAnimationLeavesSpriteEmptyWithoutFailingTheAdd },
        { "renaming_through_the_world_rebinds", RenamingThroughTheWorldRebinds },
        { "sprite_setters_refuse_sprites_in_a_world", SpriteSettersRefuseSpritesInAWorld },
        { "sprite_rename_validates_its_target", SpriteRenameValidatesItsTarget },
        { "model_rename_advances_its_asset_revision", ModelRenameAdvancesItsAssetRevision },
    };
    TestRun_RunSuite(run, "World", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
