#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "GameTexture.h"
#include "WaterMaterial.h"
#include "WorldModel.h"
#include "WorldWater.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for float comparisons of values near 1. */
#define WATER_TEST_EPSILON 1e-5f
/** 35 degrees in radians, as the second map's default turn is stored. */
#define THIRTY_FIVE_DEGREES 0.61086524f
/** The made-up GPU ids of the test textures. */
#define WAVES_ID 81U
#define RIPPLES_ID 82U
#define FOAM_ID 83U
#define MUD_ID 84U


// Types.
/* A refused definition: its JSON, the error code and a fragment its message must hold. */
typedef struct RefusedWaterStruct
{
    const char* Json;
    ErrorCode Code;
    const char* Fragment;
} RefusedWater;


// Fields.
/* A quad in the XZ plane (the water's surface) and a smaller one under it (a basin's floor), two triangles each. */
static const float SurfaceVertices[] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
static const float BasinVertices[] = { 0.0f, -0.5f, 0.0f, 1.0f, -0.5f, 0.0f, 1.0f, -0.5f, 1.0f, 0.0f, -0.5f, 1.0f };
static const float UpperVertices[] = { 0.0f, 0.5f, 0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 0.5f, 1.0f, 0.0f, 0.5f, 1.0f };
static const float QuadTexcoords[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
static const unsigned short QuadIndices[] = { 0, 2, 1, 0, 3, 2 };


// Static functions: helpers.
static bool IsNear(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static bool AreNamesEqual(const unsigned char* a, const char* b)
{
    return (a != NULL) && (strcmp((const char*)a, b) == 0);
}

/* Builds a definition of a type from @p json without registering it (a refused one returns its error). */
static Error BuildDefinitionOfType(TestAssetFixture* fixture, AssetTypeID type, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, type, &Bytes, (const unsigned char*)u8"test",
        &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}

/* Registers "basin": a model whose LOD has the floor on material slot 0 and the water's surface on slot 1. */
static bool AddBasinModel(TestRun* run, TestAssetFixture* fixture, const char* name)
{
    TestModelPart Parts[] =
    {
        { .Vertices = BasinVertices, .Texcoords = QuadTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 0 },
        { .Vertices = SurfaceVertices, .Texcoords = QuadTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 1 },
    };
    return TestAssets_AddPartsModel(run, fixture, name, Parts, 2, 2, MatrixIdentity(), NULL);
}

/* Registers a model as AddBasinModel's with a second, upper basin's surface on material slot 2. */
static bool AddTwinBasinModel(TestRun* run, TestAssetFixture* fixture, const char* name)
{
    TestModelPart Parts[] =
    {
        { .Vertices = BasinVertices, .Texcoords = QuadTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 0 },
        { .Vertices = SurfaceVertices, .Texcoords = QuadTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 1 },
        { .Vertices = UpperVertices, .Texcoords = QuadTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 2 },
    };
    return TestAssets_AddPartsModel(run, fixture, name, Parts, 3, 3, MatrixIdentity(), NULL);
}

/* Checks each case is refused with its code and a message holding its fragment; ErrorCode_Success as the code means
 * "refused, whatever its code" (the settings table's rules give their own codes), the fragment still required. */
static void CheckRefused(TestRun* run, TestAssetFixture* fixture, AssetTypeID type, const RefusedWater* cases,
    size_t count)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        Error Result = BuildDefinitionOfType(fixture, type, cases[Index].Json);
        if (cases[Index].Code == ErrorCode_Success)
        {
            bool IsRefused = Result.Code != ErrorCode_Success;
            bool IsNamed = IsRefused && (Result.Message != NULL)
                && (strstr((const char*)Result.Message, cases[Index].Fragment) != NULL);
            if (!TEST_CHECK(run, IsRefused && IsNamed))
            {
                printf("      (case %u: %s -> %s)\n", (unsigned int)Index, cases[Index].Json,
                    (Result.Message != NULL) ? (const char*)Result.Message : "(accepted)");
            }
            Error_Deconstruct(&Result);
            continue;
        }
        const char* const Fragments[] = { cases[Index].Fragment, NULL };
        if (!TestText_CheckErrorMessage(run, Result, cases[Index].Code, Fragments))
        {
            printf("      (case %u: %s)\n", (unsigned int)Index, cases[Index].Json);
        }
    }
}


// Static functions: the water material.
/* A definition with a name alone gets every default, and the table passes its own check. */
static void AMinimalWaterMaterialGetsTheDefaults(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(WaterMaterialSettings_GetPropertyTable()));
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    WaterMaterial* Loaded = NULL;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddWaterMaterial(run, &Fixture, "{\"name\": \"still\"}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadWaterMaterial(Fixture.Manager, (const unsigned char*)u8"still", User,
            &Loaded)))
    {
        const WaterMaterialSettings* Settings = WaterMaterial_GetSettings(Loaded);
        TEST_CHECK(run, (Settings->NormalA == NULL) && (Settings->NormalB == NULL) && (Settings->Foam == NULL));
        TEST_CHECK(run, (WaterMaterial_GetMap(Loaded, WaterMaterialMap_NormalA) == NULL)
            && (WaterMaterial_GetMap(Loaded, WaterMaterialMap_NormalB) == NULL)
            && (WaterMaterial_GetMap(Loaded, WaterMaterialMap_Foam) == NULL)
            && (WaterMaterial_GetMap(Loaded, WaterMaterialMap_Count) == NULL));
        TEST_CHECK(run, IsNear(Settings->NormalATileSize, 5.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->NormalASpeed, 1.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->NormalBTileSize, 1.7f, WATER_TEST_EPSILON)
            && IsNear(Settings->NormalBSpeed, 0.6f, WATER_TEST_EPSILON)
            && IsNear(Settings->NormalBTurn, THIRTY_FIVE_DEGREES, WATER_TEST_EPSILON)
            && IsNear(Settings->WaveStrength, 1.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->WindResponse, 1.0f, WATER_TEST_EPSILON));
        TEST_CHECK(run, (Settings->Flow == WaterFlow_Wind) && IsNear(Settings->FlowAngle, 0.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->FlowSpeed, 0.3f, WATER_TEST_EPSILON));
        TEST_CHECK(run, (Settings->ScatterColor.r == 26) && (Settings->ScatterColor.g == 50)
            && (Settings->ScatterColor.b == 56));
        TEST_CHECK(run, IsNear(Settings->Absorption.x, 0.45f, WATER_TEST_EPSILON)
            && IsNear(Settings->Absorption.y, 0.09f, WATER_TEST_EPSILON)
            && IsNear(Settings->Absorption.z, 0.06f, WATER_TEST_EPSILON));
        TEST_CHECK(run, IsNear(Settings->Roughness, 0.06f, WATER_TEST_EPSILON)
            && IsNear(Settings->Reflectance, 1.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->FoamTileSize, 2.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->FoamDepth, 0.3f, WATER_TEST_EPSILON)
            && IsNear(Settings->FoamStrength, 0.9f, WATER_TEST_EPSILON));
        TEST_CHECK(run, Settings->HasRipples && Settings->HasReflections);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Every key is read, the maps load as the material's dependencies (one shared texture loads once), and they unload
 * right after it. */
static void EveryKeyIsReadAndTheMapsAreDependencies(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Waves = { 0 };
    TestAssetCounters Ripples = { 0 };
    TestAssetCounters Foam = { 0 };
    AssetUserID User = ASSET_USER_ID_INVALID;
    WaterMaterial* Loaded = NULL;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddTexture(run, &Fixture, "waves", WAVES_ID, &Waves)
        && TestAssets_AddTexture(run, &Fixture, "ripples", RIPPLES_ID, &Ripples)
        && TestAssets_AddTexture(run, &Fixture, "spume", FOAM_ID, &Foam)
        && TestAssets_AddWaterMaterial(run, &Fixture, "{\"name\": \"river\", \"normal_a\": \"waves\", "
            "\"normal_a_tile_size\": 8, \"normal_a_speed\": 1.5, \"normal_b\": \"tbp:ripples\", \"normal_b_tile_size\": 3, "
            "\"normal_b_speed\": 0.25, \"normal_b_turn\": 90, \"wave_strength\": 2, \"wind_response\": 0.5, "
            "\"flow\": \"uv\", \"flow_angle\": 180, \"flow_speed\": 1.2, \"color\": \"#203040\", "
            "\"absorption\": [1, 2, 3], \"roughness\": 0.2, \"reflectance\": 1.5, \"foam\": \"spume\", "
            "\"foam_tile_size\": 4, \"foam_depth\": 0.5, \"foam_strength\": 0.75, \"has_ripples\": false, "
            "\"has_reflections\": false}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadWaterMaterial(Fixture.Manager, (const unsigned char*)u8"river", User,
            &Loaded)))
    {
        const WaterMaterialSettings* Settings = WaterMaterial_GetSettings(Loaded);
        TEST_CHECK(run, AreNamesEqual(Settings->NormalA, "tbp:waves") && AreNamesEqual(Settings->NormalB, "tbp:ripples")
            && AreNamesEqual(Settings->Foam, "tbp:spume"));
        TEST_CHECK(run, IsNear(Settings->NormalATileSize, 8.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->NormalASpeed, 1.5f, WATER_TEST_EPSILON)
            && IsNear(Settings->NormalBTileSize, 3.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->NormalBSpeed, 0.25f, WATER_TEST_EPSILON)
            && IsNear(Settings->NormalBTurn, 1.5707964f, WATER_TEST_EPSILON)
            && IsNear(Settings->WaveStrength, 2.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->WindResponse, 0.5f, WATER_TEST_EPSILON));
        TEST_CHECK(run, (Settings->Flow == WaterFlow_Uv) && IsNear(Settings->FlowAngle, 3.1415927f, WATER_TEST_EPSILON)
            && IsNear(Settings->FlowSpeed, 1.2f, WATER_TEST_EPSILON));
        TEST_CHECK(run, (Settings->ScatterColor.r == 0x20) && (Settings->ScatterColor.g == 0x30)
            && (Settings->ScatterColor.b == 0x40));
        TEST_CHECK(run, IsNear(Settings->Absorption.x, 1.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->Absorption.y, 2.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->Absorption.z, 3.0f, WATER_TEST_EPSILON));
        TEST_CHECK(run, IsNear(Settings->Roughness, 0.2f, WATER_TEST_EPSILON)
            && IsNear(Settings->Reflectance, 1.5f, WATER_TEST_EPSILON)
            && IsNear(Settings->FoamTileSize, 4.0f, WATER_TEST_EPSILON)
            && IsNear(Settings->FoamDepth, 0.5f, WATER_TEST_EPSILON)
            && IsNear(Settings->FoamStrength, 0.75f, WATER_TEST_EPSILON));
        TEST_CHECK(run, !Settings->HasRipples && !Settings->HasReflections);
        const GameTexture* MapA = WaterMaterial_GetMap(Loaded, WaterMaterialMap_NormalA);
        const GameTexture* MapB = WaterMaterial_GetMap(Loaded, WaterMaterialMap_NormalB);
        const GameTexture* MapFoam = WaterMaterial_GetMap(Loaded, WaterMaterialMap_Foam);
        TEST_CHECK(run, (MapA != NULL) && (GameTexture_GetRaylibTexture(MapA).id == WAVES_ID)
            && (MapB != NULL) && (GameTexture_GetRaylibTexture(MapB).id == RIPPLES_ID)
            && (MapFoam != NULL) && (GameTexture_GetRaylibTexture(MapFoam).id == FOAM_ID));
        TEST_CHECK(run, (Waves.LoadCount == 1U) && (Ripples.LoadCount == 1U) && (Foam.LoadCount == 1U));
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
        TEST_CHECK(run, (Waves.UnloadCount == 1U) && (Ripples.UnloadCount == 1U) && (Foam.UnloadCount == 1U));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Every rule is refused when the definition is read, naming the key: unknown keys, numbers out of their bounds, a flow
 * that is not one, a name that is not an asset name, a value of the wrong kind. */
static void EachWaterMaterialRuleIsRefused(TestRun* run)
{
    const RefusedWater Cases[] =
    {
        { "{\"name\": \"w\", \"waves\": 1}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"waves\" in water material \"tbp:w\"" },
        { "{\"name\": \"w\", \"normal_a_tile_size\": 0}", ErrorCode_Success, "normal_a_tile_size" },
        { "{\"name\": \"w\", \"normal_b_tile_size\": 20000}", ErrorCode_Success, "normal_b_tile_size" },
        { "{\"name\": \"w\", \"normal_a_speed\": -1}", ErrorCode_Success, "normal_a_speed" },
        { "{\"name\": \"w\", \"normal_b_speed\": 11}", ErrorCode_Success, "normal_b_speed" },
        { "{\"name\": \"w\", \"wave_strength\": 9}", ErrorCode_Success, "wave_strength" },
        { "{\"name\": \"w\", \"wind_response\": 2.5}", ErrorCode_Success, "wind_response" },
        { "{\"name\": \"w\", \"flow\": \"tide\"}", ErrorCode_Success, "flow" },
        { "{\"name\": \"w\", \"flow_speed\": 101}", ErrorCode_Success, "flow_speed" },
        { "{\"name\": \"w\", \"absorption\": [0.1, -0.1, 0.1]}", ErrorCode_Success, "absorption" },
        { "{\"name\": \"w\", \"absorption\": [0.1, 0.1]}", ErrorCode_Success, "absorption" },
        { "{\"name\": \"w\", \"roughness\": 1.5}", ErrorCode_Success, "roughness" },
        { "{\"name\": \"w\", \"reflectance\": 6}", ErrorCode_Success, "reflectance" },
        { "{\"name\": \"w\", \"foam_depth\": 21}", ErrorCode_Success, "foam_depth" },
        { "{\"name\": \"w\", \"foam_strength\": 1.5}", ErrorCode_Success, "foam_strength" },
        { "{\"name\": \"w\", \"foam_tile_size\": -2}", ErrorCode_Success, "foam_tile_size" },
        { "{\"name\": \"w\", \"normal_a\": \"Big Waves\"}", ErrorCode_Success, "normal_a" },
        { "{\"name\": \"w\", \"has_ripples\": 1}", ErrorCode_Success, "has_ripples" },
    };
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        CheckRefused(run, &Fixture, Fixture.Types.WaterMaterial, Cases, sizeof(Cases) / sizeof(Cases[0]));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A map that does not load (a missing texture, or a name another type holds) fails the material, naming the key and
 * the texture; the maps that had loaded are released. */
static void AMissingMapFailsTheMaterialNamingIt(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Waves = { 0 };
    AssetUserID User = ASSET_USER_ID_INVALID;
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddTexture(run, &Fixture, "waves", WAVES_ID, &Waves)
        && TestAssets_AddModel(run, &Fixture, "a_model", Box, NULL)
        && TestAssets_AddWaterMaterial(run, &Fixture, "{\"name\": \"lost\", \"normal_a\": \"waves\", \"normal_b\": "
            "\"no_such_ripples\"}")
        && TestAssets_AddWaterMaterial(run, &Fixture, "{\"name\": \"confused\", \"foam\": \"a_model\"}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        WaterMaterial* Loaded = NULL;
        const char* const Lost[] = { "Water material \"tbp:lost\": its normal_b \"tbp:no_such_ripples\"", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadWaterMaterial(Fixture.Manager, (const unsigned char*)u8"lost",
            User, &Loaded), ErrorCode_InvalidOperation, Lost);
        TEST_CHECK(run, (Waves.LoadCount == 1U) && (Waves.UnloadCount == 1U));
        const char* const Confused[] = { "Water material \"tbp:confused\": its foam \"tbp:a_model\"", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadWaterMaterial(Fixture.Manager,
            (const unsigned char*)u8"confused", User, &Loaded), ErrorCode_InvalidOperation, Confused);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Static functions: a world model's water.
/* The "water" section's rules and the variants' water materials are refused when the definition is read, naming the
 * place. */
static void EachWaterSectionRuleIsRefused(TestRun* run)
{
#define BASIN_LODS "{\"name\": \"m\", \"lods\": [{\"model\": \"basin\", \"max_distance\": 40}], "
    const RefusedWater Cases[] =
    {
        { BASIN_LODS "\"water\": {}}", ErrorCode_InvalidAssetDefinition, "\"water\" must be an array of water slots" },
        { BASIN_LODS "\"water\": [1]}", ErrorCode_InvalidAssetDefinition, "water[0] must be a JSON object" },
        { BASIN_LODS "\"water\": [{\"water_material\": \"pond\"}]}", ErrorCode_InvalidAssetDefinition,
            "water[0] needs a \"material_slot\"" },
        { BASIN_LODS "\"water\": [{\"material_slot\": -1, \"water_material\": \"pond\"}]}",
            ErrorCode_InvalidAssetDefinition, "water[0] needs a \"material_slot\"" },
        { BASIN_LODS "\"water\": [{\"material_slot\": 1}]}", ErrorCode_InvalidAssetDefinition,
            "water[0] needs a \"water_material\"" },
        { BASIN_LODS "\"water\": [{\"material_slot\": 1, \"water_material\": \"Pond Water\"}]}",
            ErrorCode_InvalidAssetDefinition, "water[0].water_material \"Pond Water\" is not a valid asset name" },
        { BASIN_LODS "\"water\": [{\"material_slot\": 1, \"water_material\": \"pond\", \"depth\": 2}]}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"depth\" in world model \"tbp:m\", water[0]" },
        { BASIN_LODS "\"water\": [{\"material_slot\": 1, \"water_material\": \"pond\"}, {\"material_slot\": 1, "
            "\"water_material\": \"mud\"}]}", ErrorCode_InvalidAssetDefinition,
            "water[1].material_slot 1 is water[0]'s too" },
        { BASIN_LODS "\"water\": [{\"material_slot\": 1, \"water_material\": \"pond\"}], \"surfaces\": [{\"name\": "
            "\"screen\", \"material_slot\": 1, \"resolution\": [64, 64]}]}", ErrorCode_InvalidAssetDefinition,
            "water[0].material_slot 1 shows a surface's picture too" },
        { BASIN_LODS "\"water\": [{\"material_slot\": 1, \"water_material\": \"pond\"}], \"variants\": [{\"name\": "
            "\"murky\", \"material_overrides\": [{\"slot\": 0, \"water_material\": \"mud\"}]}]}",
            ErrorCode_InvalidAssetDefinition, "variants[0].material_overrides[0] names a \"water_material\" for "
                "material slot 0, which the \"water\" section does not draw as water" },
        { BASIN_LODS "\"variants\": [{\"name\": \"murky\", \"material_overrides\": [{\"slot\": 1, \"water_material\": "
            "\"mud\"}]}]}", ErrorCode_InvalidAssetDefinition, "does not draw as water" },
        { BASIN_LODS "\"water\": [{\"material_slot\": 1, \"water_material\": \"pond\"}], \"variants\": [{\"name\": "
            "\"murky\", \"material_overrides\": [{\"slot\": 1, \"water_material\": \"mud\", \"tint\": \"#ff0000\"}]}]}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"tint\" in world model \"tbp:m\", "
                "variants[0].material_overrides[0] (material slot 1 is drawn as water" },
        { BASIN_LODS "\"water\": [{\"material_slot\": 1, \"water_material\": \"pond\"}], \"variants\": [{\"name\": "
            "\"murky\", \"material_overrides\": [{\"slot\": 1, \"water_material\": \"Mud!\"}]}]}",
            ErrorCode_InvalidAssetDefinition, "water_material \"Mud!\" is not a valid asset name" },
    };
#undef BASIN_LODS
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        CheckRefused(run, &Fixture, Fixture.Types.WorldModel, Cases, sizeof(Cases) / sizeof(Cases[0]));
        // The limit: WORLD_MODEL_MAX_WATERS slots pass, one more is refused.
        char Json[2048];
        for (size_t Count = WORLD_MODEL_MAX_WATERS; Count <= (WORLD_MODEL_MAX_WATERS + 1U); Count++)
        {
            int Length = snprintf(Json, sizeof(Json), "{\"name\": \"m\", \"lods\": [{\"model\": \"basin\", "
                "\"max_distance\": 40}], \"water\": [");
            for (size_t Index = 0U; Index < Count; Index++)
            {
                Length += snprintf(Json + Length, sizeof(Json) - (size_t)Length, "%s{\"material_slot\": %u, "
                    "\"water_material\": \"pond\"}", (Index > 0U) ? ", " : "", (unsigned int)Index);
            }
            snprintf(Json + Length, sizeof(Json) - (size_t)Length, "]}");
            Error Result = BuildDefinitionOfType(&Fixture, Fixture.Types.WorldModel, Json);
            if (Count == WORLD_MODEL_MAX_WATERS)
            {
                TEST_CHECK_SUCCESS(run, Result);
            }
            else
            {
                const char* const Fragments[] = { "at most 16 are allowed", NULL };
                TestText_CheckErrorMessage(run, Result, ErrorCode_InvalidAssetDefinition, Fragments);
            }
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A world model's water slots load their materials as dependencies; a slot is found by its material slot; a variant
 * swaps a water slot's material (and only where it says), every other variant and no variant keep the section's. */
static void WaterSlotsLoadTheirMaterialsAndVariantsSwapThem(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Waves = { 0 };
    TestAssetCounters Mud = { 0 };
    AssetUserID User = ASSET_USER_ID_INVALID;
    WorldModel* Fountain = NULL;
    if (TestAssetFixture_Construct(run, &Fixture)
        && AddBasinModel(run, &Fixture, "basin")
        && TestAssets_AddTexture(run, &Fixture, "waves", WAVES_ID, &Waves)
        && TestAssets_AddTexture(run, &Fixture, "mud_waves", MUD_ID, &Mud)
        && TestAssets_AddWaterMaterial(run, &Fixture, "{\"name\": \"clear\", \"normal_a\": \"waves\"}")
        && TestAssets_AddWaterMaterial(run, &Fixture, "{\"name\": \"murky\", \"normal_a\": \"mud_waves\", "
            "\"absorption\": [2, 2, 2]}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"fountain\", \"lods\": [{\"model\": \"basin\", "
            "\"max_distance\": 40}], \"water\": [{\"material_slot\": 1, \"water_material\": \"clear\"}], "
            "\"variants\": [{\"name\": \"murky\", \"material_overrides\": [{\"slot\": 1, \"water_material\": \"murky\"}, "
            "{\"slot\": 0, \"tint\": \"#808080\"}]}, {\"name\": \"painted\", \"material_overrides\": [{\"slot\": 0, "
            "\"tint\": \"#ff0000\"}]}]}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"fountain", User,
            &Fountain)))
    {
        TEST_CHECK(run, WorldModel_GetWaterCount(Fountain) == 1U);
        const WorldModelWater* Water = WorldModel_GetWater(Fountain, 0U);
        TEST_CHECK(run, (Water != NULL) && (WorldModel_GetWater(Fountain, 1U) == NULL));
        uint32_t Index = 99U;
        TEST_CHECK(run, WorldModel_TryFindWater(Fountain, 1, &Index) && (Index == 0U)
            && !WorldModel_TryFindWater(Fountain, 0, &Index) && !WorldModel_TryFindWater(Fountain, 2, NULL));
        if (Water != NULL)
        {
            TEST_CHECK(run, (WorldModelWater_GetMaterialSlot(Water) == 1)
                && AreNamesEqual(WorldModelWater_GetMaterialName(Water), "tbp:clear"));
            const WaterMaterial* Clear = WorldModelWater_GetMaterial(Water);
            size_t Murky = 0U;
            size_t Painted = 0U;
            TEST_CHECK(run, WorldModel_TryFindVariant(Fountain, (const unsigned char*)u8"murky", &Murky)
                && WorldModel_TryFindVariant(Fountain, (const unsigned char*)u8"painted", &Painted));
            const WaterMaterial* Swapped = WorldModel_GetWaterMaterial(Fountain, true, Murky, 0U);
            TEST_CHECK(run, (Clear != NULL) && (Swapped != NULL) && (Swapped != Clear)
                && IsNear(WaterMaterial_GetSettings(Swapped)->Absorption.x, 2.0f, WATER_TEST_EPSILON));
            TEST_CHECK(run, (WorldModel_GetWaterMaterial(Fountain, true, Painted, 0U) == Clear)
                && (WorldModel_GetWaterMaterial(Fountain, false, Murky, 0U) == Clear)
                && (WorldModel_GetWaterMaterial(Fountain, true, 99U, 0U) == Clear)
                && (WorldModel_GetWaterMaterial(Fountain, true, Murky, 1U) == NULL));
        }
        // Both materials and their maps are the world model's dependencies: loaded once, gone with it.
        TEST_CHECK(run, (Waves.LoadCount == 1U) && (Mud.LoadCount == 1U));
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
        TEST_CHECK(run, (Waves.UnloadCount == 1U) && (Mud.UnloadCount == 1U));
    }

    // Two water slots: a variant's swap takes its own slot alone, and an override of a water slot that names no water
    // material keeps the section's.
    WorldModel* Twin = NULL;
    AssetUserID TwinUser = ASSET_USER_ID_INVALID;
    if (TEST_CHECK(run, User != ASSET_USER_ID_INVALID)
        && AddTwinBasinModel(run, &Fixture, "twin_basin")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"twin_fountain\", \"lods\": [{\"model\": "
            "\"twin_basin\", \"max_distance\": 40}], \"water\": [{\"material_slot\": 1, \"water_material\": \"clear\"}, "
            "{\"material_slot\": 2, \"water_material\": \"clear\"}], \"variants\": [{\"name\": \"lower_murky\", "
            "\"material_overrides\": [{\"slot\": 1, \"water_material\": \"murky\"}, {\"slot\": 2}]}]}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &TwinUser))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"twin_fountain",
            TwinUser, &Twin)))
    {
        size_t LowerMurky = 0U;
        uint32_t Lower = 99U;
        uint32_t Upper = 99U;
        TEST_CHECK(run, (WorldModel_GetWaterCount(Twin) == 2U)
            && WorldModel_TryFindVariant(Twin, (const unsigned char*)u8"lower_murky", &LowerMurky)
            && WorldModel_TryFindWater(Twin, 1, &Lower) && WorldModel_TryFindWater(Twin, 2, &Upper));
        if ((Lower < 2U) && (Upper < 2U))
        {
            const WaterMaterial* Clear = WorldModelWater_GetMaterial(WorldModel_GetWater(Twin, Upper));
            const WaterMaterial* LowerSwapped = WorldModel_GetWaterMaterial(Twin, true, LowerMurky, Lower);
            TEST_CHECK(run, (Clear != NULL) && (LowerSwapped != NULL) && (LowerSwapped != Clear)
                && (WorldModel_GetWaterMaterial(Twin, true, LowerMurky, Upper) == Clear));
        }
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, TwinUser));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A water slot no mesh of LOD 0 uses, and a water material that does not load, fail the world model, naming the slot
 * and the material; what had loaded is released. */
static void ABrokenWaterSlotFailsTheWorldModel(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Waves = { 0 };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture)
        && AddBasinModel(run, &Fixture, "basin")
        && TestAssets_AddTexture(run, &Fixture, "waves", WAVES_ID, &Waves)
        && TestAssets_AddWaterMaterial(run, &Fixture, "{\"name\": \"clear\", \"normal_a\": \"waves\"}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"dry\", \"lods\": [{\"model\": \"basin\", "
            "\"max_distance\": 40}], \"water\": [{\"material_slot\": 4, \"water_material\": \"clear\"}]}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"lost\", \"lods\": [{\"model\": \"basin\", "
            "\"max_distance\": 40}], \"water\": [{\"material_slot\": 0, \"water_material\": \"clear\"}, "
            "{\"material_slot\": 1, \"water_material\": \"no_such_water\"}]}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"lost_variant\", \"lods\": [{\"model\": \"basin\", "
            "\"max_distance\": 40}], \"water\": [{\"material_slot\": 1, \"water_material\": \"clear\"}], \"variants\": "
            "[{\"name\": \"odd\", \"material_overrides\": [{\"slot\": 1, \"water_material\": \"no_such_water\"}]}]}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        WorldModel* Loaded = NULL;
        const char* const Dry[] = { "World model \"tbp:dry\": water[0]: no mesh of LOD 0 uses material slot 4", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"dry", User,
            &Loaded), ErrorCode_InvalidAssetData, Dry);
        const char* const Lost[] =
        {
            "World model \"tbp:lost\": water[1] (slot 1): water material \"tbp:no_such_water\"", NULL
        };
        TestText_CheckErrorMessage(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"lost",
            User, &Loaded), ErrorCode_InvalidOperation, Lost);
        // The first water slot's material had loaded before the second failed: released with the rest.
        TEST_CHECK(run, (Waves.LoadCount == 1U) && (Waves.UnloadCount == 1U));
        const char* const LostVariant[] =
        {
            "World model \"tbp:lost_variant\": variant \"odd\", slot 1: water material \"tbp:no_such_water\"", NULL
        };
        TestText_CheckErrorMessage(run, AssetManager_LoadWorldModel(Fixture.Manager,
            (const unsigned char*)u8"lost_variant", User, &Loaded), ErrorCode_InvalidOperation, LostVariant);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Static functions: the maths.
/* The drift: the flows run by the seconds, the air moves along the wind (gusts in); a change of wind changes the speed
 * from then on and never where the waves are; nothing moves for a time that is not positive and finite, and the air
 * not for a wind that is not. */
static void TheDriftFollowsTheWindWithoutJumps(TestRun* run)
{
    WorldWaterDrift Drift;
    WorldWaterDrift_Reset(&Drift);
    TEST_CHECK(run, (Drift.Seconds == 0.0) && (Drift.Wind.X == 0.0) && (Drift.Wind.Y == 0.0));
    WorldWaterDrift_Advance(&Drift, 0.0f, 4.0f, 2.0); // toward +Z
    TEST_CHECK(run, (Drift.Seconds == 2.0) && (fabs(Drift.Wind.X) < 1e-12) && (fabs(Drift.Wind.Y - 8.0) < 1e-12));
    WorldWaterDrift_Advance(&Drift, 1.5707964f, 3.0f, 1.0); // toward +X
    TEST_CHECK(run, (Drift.Seconds == 3.0) && (fabs(Drift.Wind.X - 3.0) < 1e-6) && (fabs(Drift.Wind.Y - 8.0) < 1e-6));
    WorldWaterDrift Before = Drift;
    WorldWaterDrift_Advance(&Drift, 0.0f, 4.0f, -1.0);
    WorldWaterDrift_Advance(&Drift, 0.0f, 4.0f, NAN);
    WorldWaterDrift_Advance(&Drift, 0.0f, 4.0f, INFINITY);
    TEST_CHECK(run, Memory_IsEqual(&Before, &Drift, sizeof(Drift)));
    WorldWaterDrift_Advance(&Drift, NAN, 4.0f, 1.0);
    WorldWaterDrift_Advance(&Drift, 0.0f, -4.0f, 1.0);
    TEST_CHECK(run, (Drift.Seconds == 5.0) && (Drift.Wind.X == Before.Wind.X) && (Drift.Wind.Y == Before.Wind.Y));

    // A sea under the wind: its map A drifts at 3 % of the air's travel times its response; a sudden turn of the wind
    // moves the pattern from where it was (the next frame's offset differs by one frame's drift, not by a jump).
    WaterMaterialSettings Settings;
    if (TEST_CHECK_SUCCESS(run, WaterMaterialSettings_SetDefaults(&Settings)))
    {
        Settings.WindResponse = 2.0f;
        WorldWaterDrift_Reset(&Drift);
        WorldWaterDrift_Advance(&Drift, 0.0f, 10.0f, 100.0);
        Vector2Double Travel = WorldWater_GetDrift(&Settings, &Drift, 1.0);
        TEST_CHECK(run, (fabs(Travel.X) < 1e-9) && (fabs(Travel.Y - (100.0 * 10.0 * 0.03 * 2.0)) < 1e-9));
        Vector2Double Slower = WorldWater_GetDrift(&Settings, &Drift, 0.25);
        TEST_CHECK(run, (fabs(Slower.X) < 1e-9) && (fabs(Slower.Y - (0.25 * Travel.Y)) < 1e-9));
        Vector3Double Origin = { 12.5, 3.0, -40.25 };
        WorldWaterFrame First;
        WorldWater_GetFrame(&Settings, &Drift, Origin, 10.0f, &First);
        WorldWaterDrift_Advance(&Drift, 3.1415927f, 10.0f, 1.0 / 60.0); // the wind swings round
        WorldWaterFrame Second;
        WorldWater_GetFrame(&Settings, &Drift, Origin, 10.0f, &Second);
        float Step = fabsf(Second.Maps[WaterMaterialMap_NormalA].Offset[1] - First.Maps[WaterMaterialMap_NormalA].Offset[1]);
        TEST_CHECK(run, IsNear(Step, (float)(10.0 / 60.0 * 0.03 * 2.0), 1e-5f)
            && IsNear(Second.Maps[WaterMaterialMap_NormalA].Offset[0], First.Maps[WaterMaterialMap_NormalA].Offset[0],
                1e-5f));
        WaterMaterialSettings_Deconstruct(&Settings);
    }
}

/* The fixed flow runs along its angle at its speed, the uv flow along +V; each map at its share of the flow. */
static void TheFixedAndUvFlowsRunAtTheirSpeed(TestRun* run)
{
    WaterMaterialSettings Settings;
    if (!TEST_CHECK_SUCCESS(run, WaterMaterialSettings_SetDefaults(&Settings)))
    {
        return;
    }
    WorldWaterDrift Drift;
    WorldWaterDrift_Reset(&Drift);
    WorldWaterDrift_Advance(&Drift, 0.0f, 50.0f, 10.0); // a gale: the fixed and uv flows ignore it
    Settings.Flow = WaterFlow_Fixed;
    Settings.FlowAngle = 1.5707964f; // toward +X
    Settings.FlowSpeed = 2.0f;
    Vector2Double Fixed = WorldWater_GetDrift(&Settings, &Drift, 0.5);
    TEST_CHECK(run, (fabs(Fixed.X - 10.0) < 1e-5) && (fabs(Fixed.Y) < 1e-5));
    Settings.Flow = WaterFlow_Uv;
    Vector2Double Uv = WorldWater_GetDrift(&Settings, &Drift, 0.5);
    TEST_CHECK(run, (Uv.X == 0.0) && (fabs(Uv.Y - 10.0) < 1e-9));
    TEST_CHECK(run, (WorldWater_GetDrift(&Settings, &Drift, NAN).Y == 0.0));
    Settings.Flow = (WaterFlow)7; // a corrupted value: no drift
    Vector2Double None = WorldWater_GetDrift(&Settings, &Drift, 1.0);
    TEST_CHECK(run, (None.X == 0.0) && (None.Y == 0.0));

    // Along a river the maps lie on the texture coordinates: the frame's origin plays no part, the offset is the drift
    // wrapped by the tile (as one minus it).
    Settings.Flow = WaterFlow_Uv;
    Settings.NormalATileSize = 4.0f;
    WorldWaterFrame Near;
    WorldWaterFrame Far;
    WorldWater_GetFrame(&Settings, &Drift, (Vector3Double){ 0.0, 0.0, 0.0 }, 0.0f, &Near);
    WorldWater_GetFrame(&Settings, &Drift, (Vector3Double){ 9.0e6, 0.0, -3.0e6 }, 0.0f, &Far);
    TEST_CHECK(run, Near.IsAlongUv && Memory_IsEqual(&Near, &Far, sizeof(Near)));
    // Map A drifted 20 (2 m/s x 10 s at a share of 1): 20 is 0 mod 4, so its offset is 0.
    TEST_CHECK(run, IsNear(Near.Maps[WaterMaterialMap_NormalA].Offset[1], 0.0f, 1e-5f)
        && IsNear(Near.Maps[WaterMaterialMap_NormalA].InverseTile, 0.25f, 1e-7f));
    WaterMaterialSettings_Deconstruct(&Settings);
}

/* The maps are anchored to the world: a point of the world reads the same coordinate (up to whole tiles) whatever the
 * frame's origin, 10 000 km out as near it; the second map turned by its turn; offsets wrapped into one tile. */
static void TheMapsAreAnchoredToTheWorldFarOut(TestRun* run)
{
    WaterMaterialSettings Settings;
    if (!TEST_CHECK_SUCCESS(run, WaterMaterialSettings_SetDefaults(&Settings)))
    {
        return;
    }
    WorldWaterDrift Drift;
    WorldWaterDrift_Reset(&Drift);
    // A place a hair below a tile's start wraps to the tile's start, never to the tile itself.
    WorldWaterFrame Edge;
    WorldWater_GetFrame(&Settings, &Drift, (Vector3Double){ -1.0e-20, 0.0, 0.0 }, 0.0f, &Edge);
    TEST_CHECK(run, Edge.Maps[WaterMaterialMap_NormalA].Offset[0] == 0.0f);
    WorldWaterDrift_Advance(&Drift, 0.7f, 6.0f, 3600.0);
    Settings.NormalASpeed = 1.7f; // the foam still drifts at the flow's own speed
    const Vector3Double Origins[] = { { 0.0, 0.0, 0.0 }, { 3.25, 0.0, -7.75 }, { 1.0e7, 0.0, -1.0e7 } };
    // The same world point, 2.5 m along x and -1.5 m along z from each origin, through each map.
    for (size_t OriginIndex = 0U; OriginIndex < (sizeof(Origins) / sizeof(Origins[0])); OriginIndex++)
    {
        WorldWaterFrame Frame;
        WorldWater_GetFrame(&Settings, &Drift, Origins[OriginIndex], 6.0f, &Frame);
        for (uint32_t Map = 0U; Map < (uint32_t)WaterMaterialMap_Count; Map++)
        {
            const WorldWaterMapFrame* MapFrame = &Frame.Maps[Map];
            double Tile = (Map == (uint32_t)WaterMaterialMap_NormalA) ? (double)Settings.NormalATileSize
                : ((Map == (uint32_t)WaterMaterialMap_NormalB) ? (double)Settings.NormalBTileSize
                : (double)Settings.FoamTileSize);
            TEST_CHECK(run, (MapFrame->Offset[0] >= 0.0f) && ((double)MapFrame->Offset[0] < (Tile + 1e-3))
                && (MapFrame->Offset[1] >= 0.0f) && ((double)MapFrame->Offset[1] < (Tile + 1e-3)));
            Vector2 Coordinate = WorldWater_GetMapCoordinate(MapFrame, (Vector2){ 2.5f, -1.5f });
            // The expected coordinate in double: the world point turned, less the drift, over the tile, mod 1.
            double Turn = (Map == (uint32_t)WaterMaterialMap_NormalB) ? (double)Settings.NormalBTurn : 0.0;
            double Cos = (double)cosf((float)Turn);
            double Sin = (double)sinf((float)Turn);
            double WorldX = Origins[OriginIndex].X + 2.5;
            double WorldZ = Origins[OriginIndex].Z - 1.5;
            double Share = (Map == (uint32_t)WaterMaterialMap_NormalA) ? (double)Settings.NormalASpeed
                : ((Map == (uint32_t)WaterMaterialMap_NormalB) ? (double)Settings.NormalBSpeed : 1.0);
            Vector2Double Travel = WorldWater_GetDrift(&Settings, &Drift, Share);
            double U = ((Cos * WorldX) + (Sin * WorldZ) - Travel.X) / Tile;
            double V = ((-Sin * WorldX) + (Cos * WorldZ) - Travel.Y) / Tile;
            double DeltaU = (double)Coordinate.x - U;
            double DeltaV = (double)Coordinate.y - V;
            DeltaU -= round(DeltaU);
            DeltaV -= round(DeltaV);
            if (!TEST_CHECK(run, (fabs(DeltaU) < 2e-6) && (fabs(DeltaV) < 2e-6)))
            {
                printf("      (origin %u, map %u: off by %.3g, %.3g of a tile)\n", (unsigned int)OriginIndex,
                    (unsigned int)Map, DeltaU, DeltaV);
            }
        }
    }
    WaterMaterialSettings_Deconstruct(&Settings);
}

/* The waves' strength follows the wind as wind-driven seas' slopes do: a third in still air, the set strength in a 5 m/s
 * breeze, about twice it in a 20 m/s gale; no change at a response of 0; never below 0; the wind clamped to a
 * hurricane's. */
static void TheWavesGrowWithTheWind(TestRun* run)
{
    float Calm = WorldWater_GetWaveStrength(1.0f, 1.0f, 0.0f);
    float Breeze = WorldWater_GetWaveStrength(1.0f, 1.0f, 5.0f);
    float Gale = WorldWater_GetWaveStrength(1.0f, 1.0f, 20.0f);
    TEST_CHECK(run, IsNear(Calm, (float)sqrt(0.003 / 0.0286), 1e-5f) && IsNear(Breeze, 1.0f, 1e-6f)
        && IsNear(Gale, (float)sqrt(0.1054 / 0.0286), 1e-4f) && (Gale > 1.9f) && (Gale < 2.0f));
    TEST_CHECK(run, IsNear(WorldWater_GetWaveStrength(2.0f, 0.0f, 0.0f), 2.0f, 1e-6f)
        && IsNear(WorldWater_GetWaveStrength(2.0f, 0.0f, 30.0f), 2.0f, 1e-6f));
    TEST_CHECK(run, IsNear(WorldWater_GetWaveStrength(1.0f, 0.5f, 0.0f), 0.5f * (1.0f + Calm), 1e-5f));
    TEST_CHECK(run, (WorldWater_GetWaveStrength(1.0f, 2.0f, 0.0f) == 0.0f)
        && (WorldWater_GetWaveStrength(0.0f, 1.0f, 20.0f) == 0.0f));
    TEST_CHECK(run, IsNear(WorldWater_GetWaveStrength(1.0f, 1.0f, 400.0f), WorldWater_GetWaveStrength(1.0f, 1.0f, 40.0f),
        1e-6f) && IsNear(WorldWater_GetWaveStrength(1.0f, 1.0f, NAN), Calm, 1e-6f)
        && IsNear(WorldWater_GetWaveStrength(1.0f, 1.0f, -3.0f), Calm, 1e-6f));
}

/* Under the surface: the bottom fades by exp(-absorption x path) per channel, all of it over an infinite path (nothing
 * under the water) whatever the absorption; foam shows where the water is shallower than its depth times the
 * pattern's value, softened at the edge, none past the foam depth, none without a foam depth or a strength. */
static void TheBottomFadesAndFoamLiesInTheShallows(TestRun* run)
{
    Vector3 Clear = WorldWater_GetTransmittance((Vector3){ 0.45f, 0.09f, 0.06f }, 2.0f);
    TEST_CHECK(run, IsNear(Clear.x, expf(-0.9f), 1e-6f) && IsNear(Clear.y, expf(-0.18f), 1e-6f)
        && IsNear(Clear.z, expf(-0.12f), 1e-6f));
    Vector3 Through = WorldWater_GetTransmittance((Vector3){ 0.0f, 0.0f, 0.0f }, 50.0f);
    Vector3 Open = WorldWater_GetTransmittance((Vector3){ 0.0f, 0.0f, 0.0f }, INFINITY);
    TEST_CHECK(run, (Through.x == 1.0f) && (Through.z == 1.0f) && (Open.x == 0.0f) && (Open.y == 0.0f)
        && (Open.z == 0.0f));
    Vector3 Above = WorldWater_GetTransmittance((Vector3){ 1.0f, 1.0f, 1.0f }, -3.0f);
    TEST_CHECK(run, Above.x == 1.0f);

    TEST_CHECK(run, IsNear(WorldWater_GetFoam(0.6f, 0.0f, 0.3f, 1.0f), 1.0f, 1e-6f)
        && IsNear(WorldWater_GetFoam(0.5f, 0.15f, 0.3f, 1.0f), 0.5f, 1e-6f)
        && (WorldWater_GetFoam(0.9f, 0.3f, 0.3f, 1.0f) == 0.0f)
        && (WorldWater_GetFoam(1.0f, 0.5f, 0.3f, 1.0f) == 0.0f));
    TEST_CHECK(run, IsNear(WorldWater_GetFoam(0.6f, 0.0f, 0.3f, 0.75f), 0.75f, 1e-6f)
        && (WorldWater_GetFoam(0.6f, 0.0f, 0.0f, 1.0f) == 0.0f) && (WorldWater_GetFoam(0.6f, 0.0f, 0.3f, 0.0f) == 0.0f)
        && (WorldWater_GetFoam(1.0f, INFINITY, 0.3f, 1.0f) == 0.0f)
        && (WorldWater_GetFoam(0.5f, NAN, 0.3f, 1.0f) == 0.0f));
    // Across its edge the foam rises smoothly with the pattern's value.
    float Previous = -1.0f;
    bool IsRising = true;
    for (int Step = 0; Step <= 20; Step++)
    {
        float Foam = WorldWater_GetFoam(0.4f + (0.01f * (float)Step), 0.15f, 0.3f, 1.0f);
        IsRising = IsRising && (Foam >= Previous);
        Previous = Foam;
    }
    TEST_CHECK(run, IsRising && (Previous == 1.0f));
}


// Public functions.
void WaterTests_Run(TestRun* run)
{
    const TestCase Cases[] =
    {
        { "a_minimal_water_material_gets_the_defaults", AMinimalWaterMaterialGetsTheDefaults },
        { "every_key_is_read_and_the_maps_are_dependencies", EveryKeyIsReadAndTheMapsAreDependencies },
        { "each_water_material_rule_is_refused", EachWaterMaterialRuleIsRefused },
        { "a_missing_map_fails_the_material_naming_it", AMissingMapFailsTheMaterialNamingIt },
        { "each_water_section_rule_is_refused", EachWaterSectionRuleIsRefused },
        { "water_slots_load_their_materials_and_variants_swap_them", WaterSlotsLoadTheirMaterialsAndVariantsSwapThem },
        { "a_broken_water_slot_fails_the_world_model", ABrokenWaterSlotFailsTheWorldModel },
        { "the_drift_follows_the_wind_without_jumps", TheDriftFollowsTheWindWithoutJumps },
        { "the_fixed_and_uv_flows_run_at_their_speed", TheFixedAndUvFlowsRunAtTheirSpeed },
        { "the_maps_are_anchored_to_the_world_far_out", TheMapsAreAnchoredToTheWorldFarOut },
        { "the_waves_grow_with_the_wind", TheWavesGrowWithTheWind },
        { "the_bottom_fades_and_foam_lies_in_the_shallows", TheBottomFadesAndFoamLiesInTheShallows },
    };
    TestRun_RunSuite(run, "Water", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
