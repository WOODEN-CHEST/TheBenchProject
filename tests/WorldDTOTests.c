#include "TestSuites.h"
#include "World.h"
#include "WorldDTO.h"
#include "WorldModelObject.h"
#include "WorldSpriteObject.h"
#include "WorldLight.h"
#include <math.h>
#include <string.h>


// Static functions.
static bool AreStringsEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    return strcmp((const char*)a, (const char*)b) == 0;
}

static bool AreVectorsEqual(Vector3 a, Vector3 b)
{
    return (a.x == b.x) && (a.y == b.y) && (a.z == b.z);
}

static bool AreColorsEqual(Color a, Color b)
{
    return (a.r == b.r) && (a.g == b.g) && (a.b == b.b) && (a.a == b.a);
}

/* Adds one object of every type with non-default values in every persisted field. */
static bool BuildSourceWorld(TestRun* run, World* world)
{
    if (!TEST_CHECK_SUCCESS(run, World_Construct1(world)))
    {
        return false;
    }
    bool IsBuilt = TEST_CHECK_SUCCESS(run, World_SetName(world, (const unsigned char*)u8"office"));

    WorldEnvironment* Environment = World_GetEnvironment(world);
    Environment->TimeOfDay = 0.3f;
    Environment->DayLengthSeconds = 90.0f;
    Environment->SunAngle = 0.2f;
    Environment->SkyTint = (Color){ 10, 20, 30, 255 };
    Environment->StarSeed = 42;
    Environment->FogStrength = 0.5f;
    Environment->IsBloomEnabled = false;
    Environment->LightPollution = 0.9f;
    Environment->LightPollutionColor = (Color){ 200, 100, 50, 255 };
    Environment->MilkyWayBrightness = 1.5f;
    Environment->StarTwinkle = 0.4f;

    WorldModelObject* ModelObject = NULL;
    IsBuilt = IsBuilt && TEST_CHECK_SUCCESS(run, WorldModelObject_Create((const unsigned char*)u8"desk",
        (const unsigned char*)u8"desk_model", &ModelObject));
    if (ModelObject != NULL)
    {
        WorldObject* Base = WorldModelObject_AsObject(ModelObject);
        ModelObject->HasOutline = false;
        ModelObject->OmitPixelation = true;
        ModelObject->ShadowTier = WorldShadowTier_Far;
        RenderColor Tint = RenderColor_White();
        Tint.Brightness = 0.5f;
        IsBuilt = IsBuilt && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Base, (Vector3){ 1.0f, 2.0f, 3.0f }))
            && TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Base, (Vector3){ 0.1f, 0.2f, 0.3f }))
            && TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Base, (Vector3){ 2.0f, 2.0f, 0.5f }))
            && TEST_CHECK_SUCCESS(run, WorldObject_SetTint(Base, Tint))
            && TEST_CHECK_SUCCESS(run, World_AddObject(world, Base));
    }

    WorldSpriteObject* Sprite = NULL;
    IsBuilt = IsBuilt && TEST_CHECK_SUCCESS(run, WorldSpriteObject_Create((const unsigned char*)u8"poster",
        (const unsigned char*)u8"poster_animation", &Sprite));
    if (Sprite != NULL)
    {
        Sprite->HasOutline = false;
        Sprite->OmitPixelation = true;
        IsBuilt = IsBuilt && TEST_CHECK_SUCCESS(run, World_AddObject(world, WorldSpriteObject_AsObject(Sprite)));
    }

    WorldLight* Light = NULL;
    IsBuilt = IsBuilt && TEST_CHECK_SUCCESS(run, WorldLight_Create((const unsigned char*)u8"lamp", &Light));
    if (Light != NULL)
    {
        Light->Color = (Color){ 255, 200, 100, 255 };
        Light->CastsShadows = true;
        IsBuilt = IsBuilt && TEST_CHECK_SUCCESS(run, WorldLight_SetIntensity(Light, 3.0f))
            && TEST_CHECK_SUCCESS(run, WorldLight_SetSize(Light, 7.0f))
            && TEST_CHECK_SUCCESS(run, World_AddObject(world, WorldLight_AsObject(Light)));
    }
    return IsBuilt;
}

static void RoundTripKeepsEveryPersistentField(TestRun* run)
{
    World Source;
    World Restored;
    WorldDTO Snapshot;
    bool IsSourceBuilt = BuildSourceWorld(run, &Source);
    bool IsSnapshotTaken = IsSourceBuilt && TEST_CHECK_SUCCESS(run, WorldDTO_FromWorld(&Source, &Snapshot));
    bool IsRestored = IsSnapshotTaken && TEST_CHECK_SUCCESS(run, World_Construct1(&Restored));
    if (IsRestored && TEST_CHECK_SUCCESS(run, WorldDTO_ApplyToWorld(&Snapshot, &Restored)))
    {
        TEST_CHECK(run, AreStringsEqual(World_GetName(&Restored), World_GetName(&Source)));
        TEST_CHECK(run, World_GetNextObjectId(&Restored) == World_GetNextObjectId(&Source));
        TEST_CHECK(run, World_GetEnvironment(&Restored)->TimeOfDay == 0.3f);
        TEST_CHECK(run, World_GetEnvironment(&Restored)->DayLengthSeconds == 90.0f);
        TEST_CHECK(run, World_GetEnvironment(&Restored)->SunAngle == 0.2f);
        TEST_CHECK(run, AreColorsEqual(World_GetEnvironment(&Restored)->SkyTint, (Color){ 10, 20, 30, 255 }));
        TEST_CHECK(run, World_GetEnvironment(&Restored)->StarSeed == 42);
        TEST_CHECK(run, World_GetEnvironment(&Restored)->FogStrength == 0.5f);
        TEST_CHECK(run, !World_GetEnvironment(&Restored)->IsBloomEnabled);
        TEST_CHECK(run, World_GetEnvironment(&Restored)->LightPollution == 0.9f);
        TEST_CHECK(run, AreColorsEqual(World_GetEnvironment(&Restored)->LightPollutionColor, (Color){ 200, 100, 50, 255 }));
        TEST_CHECK(run, World_GetEnvironment(&Restored)->MilkyWayBrightness == 1.5f);
        TEST_CHECK(run, World_GetEnvironment(&Restored)->StarTwinkle == 0.4f);

        if (TEST_CHECK(run, World_GetObjectCount(&Restored) == World_GetObjectCount(&Source)))
        {
            for (size_t Index = 0; Index < World_GetObjectCount(&Source); Index++)
            {
                WorldObject* Original = NULL;
                WorldObject* Copy = NULL;
                World_GetObjectByIndex(&Source, Index, &Original);
                World_GetObjectByIndex(&Restored, Index, &Copy);
                TEST_CHECK(run, WorldObject_GetType(Copy) == WorldObject_GetType(Original));
                TEST_CHECK(run, WorldObject_GetId(Copy) == WorldObject_GetId(Original));
                TEST_CHECK(run, AreStringsEqual(WorldObject_GetName(Copy), WorldObject_GetName(Original)));
                TEST_CHECK(run, AreVectorsEqual(WorldObject_GetPosition(Copy), WorldObject_GetPosition(Original)));
                TEST_CHECK(run, AreVectorsEqual(WorldObject_GetRotation(Copy), WorldObject_GetRotation(Original)));
                TEST_CHECK(run, AreVectorsEqual(WorldObject_GetScale(Copy), WorldObject_GetScale(Original)));
                TEST_CHECK(run, WorldObject_GetTint(Copy).Brightness == WorldObject_GetTint(Original).Brightness);
            }

            WorldObject* Copy = NULL;
            World_GetObjectByIndex(&Restored, 0, &Copy);
            const WorldModelObject* ModelCopy = (const WorldModelObject*)Copy;
            TEST_CHECK(run, AreStringsEqual(WorldModelObject_GetModelAssetName(ModelCopy),
                (const unsigned char*)u8"desk_model"));
            TEST_CHECK(run, !ModelCopy->HasOutline && ModelCopy->OmitPixelation);
            TEST_CHECK(run, ModelCopy->ShadowTier == WorldShadowTier_Far);

            World_GetObjectByIndex(&Restored, 1, &Copy);
            const WorldSpriteObject* SpriteCopy = (const WorldSpriteObject*)Copy;
            TEST_CHECK(run, AreStringsEqual(WorldSpriteObject_GetSpriteAnimationAssetName(SpriteCopy),
                (const unsigned char*)u8"poster_animation"));
            TEST_CHECK(run, !SpriteCopy->HasOutline && SpriteCopy->OmitPixelation);

            World_GetObjectByIndex(&Restored, 2, &Copy);
            const WorldLight* LightCopy = (const WorldLight*)Copy;
            TEST_CHECK(run, AreColorsEqual(LightCopy->Color, (Color){ 255, 200, 100, 255 }));
            TEST_CHECK(run, (WorldLight_GetIntensity(LightCopy) == 3.0f) && (WorldLight_GetSize(LightCopy) == 7.0f));
            TEST_CHECK(run, LightCopy->CastsShadows);
        }
    }

    if (IsRestored)
    {
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Restored));
    }
    if (IsSnapshotTaken)
    {
        TEST_CHECK_SUCCESS(run, WorldDTO_Deconstruct(&Snapshot));
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&Source));
}

static void BadEnvironmentRejectsTheWholeSnapshot(TestRun* run)
{
    World Source;
    World Target;
    WorldDTO Snapshot;
    bool IsSourceBuilt = BuildSourceWorld(run, &Source);
    bool IsSnapshotTaken = IsSourceBuilt && TEST_CHECK_SUCCESS(run, WorldDTO_FromWorld(&Source, &Snapshot));
    bool IsTargetBuilt = IsSnapshotTaken && TEST_CHECK_SUCCESS(run, World_Construct1(&Target));
    if (IsTargetBuilt)
    {
        // A NaN day length from a bad save would make the time of day NaN forever.
        Snapshot.Environment.DayLengthSeconds = NAN;
        TEST_CHECK_ERROR(run, WorldDTO_ApplyToWorld(&Snapshot, &Target), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, World_GetObjectCount(&Target) == 0);
        TEST_CHECK(run, World_GetName(&Target) == NULL);
        TEST_CHECK(run, isfinite(World_GetEnvironment(&Target)->DayLengthSeconds));
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Target));
    }
    if (IsSnapshotTaken)
    {
        TEST_CHECK_SUCCESS(run, WorldDTO_Deconstruct(&Snapshot));
    }
    TEST_CHECK_SUCCESS(run, World_Deconstruct(&Source));
}


// Public functions.
void WorldDTOTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "round_trip_keeps_every_persistent_field", RoundTripKeepsEveryPersistentField },
        { "bad_environment_rejects_the_whole_snapshot", BadEnvironmentRejectsTheWholeSnapshot },
    };
    TestRun_RunSuite(run, "WorldDTO", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
