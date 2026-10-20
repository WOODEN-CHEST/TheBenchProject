#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TestWorld.h"
#include "GameTexture.h"
#include "Logger.h"
#include "World.h"
#include "WorldModel.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for the float comparisons. */
#define TEST_EPSILON 1e-4f
/** Room for a path, and for a definition built in a test. */
#define WORLD_MODEL_TEST_TEXT_CAPACITY 256
#define WORLD_MODEL_TEST_JSON_CAPACITY 8192
/** The made-up GPU ids of the test textures. */
#define DARK_ALBEDO_ID 71U
#define GLOW_ID 72U


// Types.
/* A refused definition: its JSON, the error code and a fragment its message must hold. */
typedef struct RefusedDefinitionStruct
{
    const char* Json;
    ErrorCode Code;
    const char* Fragment;
} RefusedDefinition;


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

/* Builds a definition of @p type from @p json without registering it and returns the constructor's result (the
 * definition, if one was built, is destroyed). */
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

/* Builds a world model definition from @p json without registering it (see BuildDefinitionOfType). */
static Error BuildDefinition(TestAssetFixture* fixture, const char* json)
{
    return BuildDefinitionOfType(fixture, fixture->Types.WorldModel, json);
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Found = 0;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, WORLD_MODEL_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_MODEL_TEST_TEXT_CAPACITY, sizeof(unsigned char));
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

/* Loads the world model @p name under a new user; *outUser receives the user (retire it to release the hold). */
static WorldModel* LoadWorldModel(TestRun* run, TestAssetFixture* fixture, const char* name, AssetUserID* outUser)
{
    WorldModel* Loaded = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(fixture->Manager, outUser))
        || !TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldModel(fixture->Manager, (const unsigned char*)name, *outUser,
            &Loaded)))
    {
        return NULL;
    }
    return Loaded;
}

/* A material's maps filled with values a variant can be seen to change (and some it must leave). */
static void FillMaps(MaterialMap* maps)
{
    Memory_Zero(maps, sizeof(MaterialMap) * (size_t)(MATERIAL_MAP_BRDF + 1));
    for (int Index = 0; Index <= (int)MATERIAL_MAP_BRDF; Index++)
    {
        maps[Index].texture.id = (unsigned int)(Index + 1);
        maps[Index].color = (Color){ 10, 20, 30, 255 };
        maps[Index].value = 0.25f;
    }
}

static bool AreColorsEqual(Color a, Color b)
{
    return (a.r == b.r) && (a.g == b.g) && (a.b == b.b) && (a.a == b.a);
}

/* Field by field (the struct has padding, which memcmp would compare too). */
static bool AreSettingsEqual(const WorldModelSettings* a, const WorldModelSettings* b)
{
    return (a->HasOutline == b->HasOutline) && (a->OmitsPixelation == b->OmitsPixelation)
        && (a->ShadowTier == b->ShadowTier) && (a->CastsShadows == b->CastsShadows)
        && (a->DrawDistance == b->DrawDistance) && (a->IsDrawDistanceFixed == b->IsDrawDistanceFixed);
}


// Tests: definitions.
/* A definition with one LOD gets the documented defaults; its bounds are LOD 0's mesh bounds moved by its import
 * transform. */
static void AMinimalDefinitionGetsTheDefaults(TestRun* run)
{
    TestAssetFixture Fixture;
    BoundingBox Box = { .min = { -0.5f, 0.0f, -0.5f }, .max = { 0.5f, 2.0f, 0.5f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddModelWithMaterials(run, &Fixture, "post", Box, 1, MatrixTranslate(0.0f, 0.0f, 3.0f), NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"post\", \"lods\": [{\"model\": \"post\", "
            "\"max_distance\": 25}]}"))
    {
        WorldModel* Loaded = LoadWorldModel(run, &Fixture, "post", &User);
        if (Loaded != NULL)
        {
            const WorldModelSettings* Settings = WorldModel_GetSettings(Loaded);
            TEST_CHECK(run, (WorldModel_GetLodCount(Loaded) == 1U) && (WorldModel_GetLodModel(Loaded, 0U) != NULL)
                && (WorldModel_GetLodModel(Loaded, 1U) == NULL) && (WorldModel_GetLodMaxDistance(Loaded, 0U) == 25.0f)
                && (WorldModel_GetLodMaxDistance(Loaded, 1U) == 0.0f));
            TEST_CHECK(run, Settings->HasOutline && !Settings->OmitsPixelation
                && (Settings->ShadowTier == WorldShadowTier_Both) && Settings->CastsShadows);
            TEST_CHECK(run, (Settings->DrawDistance == 25.0f) && !Settings->IsDrawDistanceFixed);
            TEST_CHECK(run, (WorldModel_GetLodFadeSeconds(Loaded) == WORLD_MODEL_DEFAULT_LOD_FADE_SECONDS)
                && (WorldModel_GetLodHysteresis(Loaded) == WORLD_MODEL_DEFAULT_LOD_HYSTERESIS));
            // The sphere around LOD 0's mesh bounds, in its mesh space, with its import transform to the object's.
            Vector3 Center = WorldModel_GetBoundsCenter(Loaded);
            Matrix BoundsTransform = WorldModel_GetBoundsTransform(Loaded);
            TEST_CHECK(run, IsNear(Center.x, 0.0f) && IsNear(Center.y, 1.0f) && IsNear(Center.z, 0.0f));
            TEST_CHECK(run, IsNear(WorldModel_GetBoundsRadius(Loaded), sqrtf(0.25f + 1.0f + 0.25f)));
            TEST_CHECK(run, (BoundsTransform.m14 == 3.0f) && (BoundsTransform.m0 == 1.0f));
            TEST_CHECK(run, (WorldModel_GetVariantCount(Loaded) == 0U)
                && (WorldModel_GetVariantName(Loaded, 0U) == NULL));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* The task's example: three LODs, every setting, a variant with a texture and factors, and explicit bounds. */
static void AFullDefinitionReadsEveryKey(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters TextureCounters = { 0 };
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    const char* Json =
        "{\n"
        "  \"name\": \"office_desk\",\n"
        "  \"lods\": [\n"
        "    { \"model\": \"office_desk_lod0\", \"max_distance\": 15 },\n"
        "    { \"model\": \"office_desk_lod1\", \"max_distance\": 40 },\n"
        "    { \"model\": \"office_desk_lod2\", \"max_distance\": \"90\" }\n"
        "  ],\n"
        "  \"draw_distance\": 120,\n"
        "  \"is_draw_distance_fixed\": true,\n"
        "  \"lod_fade_seconds\": 0.5,\n"
        "  \"lod_hysteresis\": 0.1,\n"
        "  \"render\": { \"has_outline\": false, \"omit_pixelation\": true, \"shadow_tier\": \"near\", "
        "\"casts_shadows\": false },\n"
        "  \"variants\": [\n"
        "    { \"name\": \"dark_wood\", \"material_overrides\": [ { \"slot\": 0, \"albedo\": \"dark_albedo\", "
        "\"tint\": \"#d0c0b0\" } ] },\n"
        "    { \"name\": \"glowing\", \"material_overrides\": [ { \"slot\": 1, \"emissive\": \"glow\", \"metallic\": "
            "1, "
        "\"roughness\": 0.25 } ] }\n"
        "  ],\n"
        "  \"bounds\": { \"center\": [0, 0.4, 0], \"radius\": 1.1 }\n"
        "}\n";
    bool IsBuilt = TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddModel(run, &Fixture, "office_desk_lod0", Box, NULL)
        && TestAssets_AddModel(run, &Fixture, "office_desk_lod1", Box, NULL)
        && TestAssets_AddModel(run, &Fixture, "office_desk_lod2", Box, NULL)
        && TestAssets_AddTexture(run, &Fixture, "dark_albedo", DARK_ALBEDO_ID, &TextureCounters)
        && TestAssets_AddTexture(run, &Fixture, "glow", GLOW_ID, NULL)
        && TestAssets_AddWorldModel(run, &Fixture, Json);
    WorldModel* Loaded = IsBuilt ? LoadWorldModel(run, &Fixture, "office_desk", &User) : NULL;
    if (Loaded != NULL)
    {
        TEST_CHECK(run, (WorldModel_GetLodCount(Loaded) == 3U) && (WorldModel_GetLodMaxDistance(Loaded, 0U) == 15.0f)
            && (WorldModel_GetLodMaxDistance(Loaded, 1U) == 40.0f)
            && (WorldModel_GetLodMaxDistance(Loaded, 2U) == 90.0f));
        TEST_CHECK(run, (WorldModel_GetLodModel(Loaded, 0U) != WorldModel_GetLodModel(Loaded, 1U))
            && (WorldModel_GetLodModel(Loaded, 2U) != NULL));
        const WorldModelSettings* Settings = WorldModel_GetSettings(Loaded);
        TEST_CHECK(run, !Settings->HasOutline && Settings->OmitsPixelation
            && (Settings->ShadowTier == WorldShadowTier_Near)
            && !Settings->CastsShadows && (Settings->DrawDistance == 120.0f) && Settings->IsDrawDistanceFixed);
        TEST_CHECK(run, (WorldModel_GetLodFadeSeconds(Loaded) == 0.5f)
            && (WorldModel_GetLodHysteresis(Loaded) == 0.1f));
        Vector3 Center = WorldModel_GetBoundsCenter(Loaded);
        Matrix Identity = MatrixIdentity();
        Matrix BoundsTransform = WorldModel_GetBoundsTransform(Loaded);
        TEST_CHECK(run, (Center.x == 0.0f) && (Center.y == 0.4f) && (Center.z == 0.0f)
            && (WorldModel_GetBoundsRadius(Loaded) == 1.1f)
            && (memcmp(&BoundsTransform, &Identity, sizeof(Matrix)) == 0)); // authored in the object's own space
        TEST_CHECK(run, (WorldModel_GetVariantCount(Loaded) == 2U)
            && (strcmp((const char*)WorldModel_GetVariantName(Loaded, 0U), "dark_wood") == 0)
            && (strcmp((const char*)WorldModel_GetVariantName(Loaded, 1U), "glowing") == 0));
        TEST_CHECK(run, TextureCounters.LoadCount == 1U);
    }
    if (User != ASSET_USER_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
        TEST_CHECK(run, TextureCounters.UnloadCount == 1U);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Every rule of the definition refuses what it should, with a message that says where and why. */
static void EachRuleIsRefusedWithAClearMessage(TestRun* run)
{
    static const RefusedDefinition Cases[] =
    {
        { "{\"name\": \"m\"}", ErrorCode_InvalidAssetDefinition, "\"lods\" must hold 1 to 8 LODs" },
        { "{\"name\": \"m\", \"lods\": []}", ErrorCode_InvalidAssetDefinition, "it holds 0" },
        { "{\"name\": \"m\", \"lods\": {}}", ErrorCode_InvalidAssetDefinition, "lods must be an array" },
        { "{\"name\": \"m\", \"lods\": [3]}", ErrorCode_InvalidAssetDefinition, "lods[0] must be a JSON object" },
        { "{\"name\": \"m\", \"lods\": [{\"max_distance\": 5}]}", ErrorCode_InvalidAssetDefinition,
            "lods[0] needs a \"model\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"Box\", \"max_distance\": 5}]}", ErrorCode_InvalidAssetDefinition,
            "lods[0].model \"Box\" is not a valid asset name" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": 7, \"max_distance\": 5}]}", ErrorCode_InvalidAssetDefinition,
            "lods[0].model must be a string" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\"}]}", ErrorCode_InvalidAssetDefinition,
            "lods[0] needs a \"max_distance\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 0}]}", ErrorCode_InvalidAssetDefinition,
            "lods[0].max_distance is 0; it must be above 0" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": -4}]}",
            ErrorCode_InvalidAssetDefinition, "it must be above 0" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": \"far\"}]}", ErrorCode_InvalidJSON,
            "World model \"m\": lods[0]: \"max_distance\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 1e40}]}", ErrorCode_InvalidJSON,
            "beyond the float range" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"a\", \"max_distance\": 10}, {\"model\": \"b\", "
            "\"max_distance\": 10}]}",
            ErrorCode_InvalidAssetDefinition, "lods[1].max_distance (10) must be above lods[0].max_distance (10)" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"a\", \"max_distance\": 1}, {\"model\": \"a\", \"max_distance\": "
            "2},"
            "{\"model\": \"a\", \"max_distance\": 3}, {\"model\": \"a\", \"max_distance\": 4}, {\"model\": \"a\", "
            "\"max_distance\": 5}, {\"model\": \"a\", \"max_distance\": 6}, {\"model\": \"a\", \"max_distance\": 7}, "
            "{\"model\": \"a\", \"max_distance\": 8}, {\"model\": \"a\", \"max_distance\": 9}]}",
            ErrorCode_InvalidAssetDefinition, "it holds 9" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"draw_distance\": 30}",
            ErrorCode_InvalidAssetDefinition, "draw_distance (30) must be at least the last LOD's max_distance (40)" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"is_draw_distance_fixed\": 2}",
            ErrorCode_InvalidAssetDefinition, "is_draw_distance_fixed must be true or false" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"lod_fade_seconds\": -1}",
            ErrorCode_InvalidAssetDefinition, "lod_fade_seconds is -1; it must be at least 0" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"lod_hysteresis\": 1}",
            ErrorCode_InvalidAssetDefinition, "lod_hysteresis is 1; it must be at least 0 and below 1" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"lod_hysteresis\": -0.5}",
            ErrorCode_InvalidAssetDefinition, "lod_hysteresis is -0.5" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"lodz\": 1}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"lodz\" in world model \"m\"; the keys allowed there are: "
                "name, "
            "lods, draw_distance" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"collision\": {}}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"collision\" in world model \"m\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"maxdistance\": 40}]}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"maxdistance\" in world model \"m\", lods[0]; the keys allowed there are: model, "
                "max_distance" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"\\u001b[31m\": 1}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"?[31m\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": 4}",
            ErrorCode_InvalidAssetDefinition, "render must be a JSON object" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"outline\": true}}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"outline\" in world model \"m\", render" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"shadow_tier\": "
            "\"middle\"}}", ErrorCode_InvalidAssetDefinition,
            "render.shadow_tier is \"middle\"; it must be \"both\", \"near\" or \"far\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"has_outline\": "
            "\"yes\"}}", ErrorCode_InvalidAssetDefinition, "render.has_outline must be true or false" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": {}}",
            ErrorCode_InvalidAssetDefinition, "variants must be an array" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{}]}",
            ErrorCode_InvalidAssetDefinition, "variants[0] needs a \"name\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"Dark Wood\"}]}", ErrorCode_InvalidAssetDefinition, "variants[0].name \"Dark Wood\" is not a valid "
                "asset name" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\"}, "
            "{\"name\": \"dark\"}]}", ErrorCode_InvalidAssetDefinition,
            "variants[1].name \"dark\" is used by variants[0] too" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"look\": 1}]}", ErrorCode_InvalidAssetDefinition, "Unknown key \"look\" in world model \"m\", "
                "variants[0]" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"tint\": \"#ff0000\"}]}]}", ErrorCode_InvalidAssetDefinition,
            "variants[0].material_overrides[0]: A material override requires a \"slot\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"slot\": -1}]}]}", ErrorCode_InvalidAssetDefinition, "requires a \"slot\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"slot\": 0}, {\"slot\": 0}]}]}", ErrorCode_InvalidAssetDefinition,
            "variants[0].material_overrides[1] overrides material slot 0 a second time" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"slot\": 0, \"albedo\": \"Dark/Albedo\"}]}]}", ErrorCode_InvalidAssetDefinition,
            "albedo \"Dark/Albedo\" is not a valid asset name" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"slot\": 0, \"metallic\": 1.5}]}]}", ErrorCode_InvalidAssetDefinition,
            "\"metallic\" is 1.5; it must be in [0, 1]" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"slot\": 0, \"roughness\": -0.5}]}]}", ErrorCode_InvalidAssetDefinition,
            "\"roughness\" is -0.5; it must be in [0, 1]" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"slot\": 0, \"emissive_intensity\": -2}]}]}", ErrorCode_InvalidAssetDefinition,
            "\"emissive_intensity\" is -2; it must be at least 0" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"slot\": 0, \"tint\": \"#zzzzzz\"}]}]}", ErrorCode_InvalidJSON, "\"tint\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"material_overrides\": [{\"slot\": 0, \"texture_properties\": {}}]}]}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"texture_properties\" in world model \"m\", variants[0].material_overrides[0]" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"bounds\": {\"center\": [0, 0, "
            "0]}}",
            ErrorCode_InvalidAssetDefinition, "bounds needs a \"radius\" of at least 0 metres" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"bounds\": {\"center\": [0, 0, "
            "0], "
            "\"radius\": -1}}", ErrorCode_InvalidAssetDefinition, "bounds needs a \"radius\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"bounds\": {\"radius\": 1}}",
            ErrorCode_InvalidAssetDefinition, "bounds needs a \"center\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"bounds\": {\"center\": [0, 0], "
            "\"radius\": 1}}", ErrorCode_InvalidJSON, "bounds.center" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"bounds\": {\"center\": [0, 0, "
            "0], "
            "\"radius\": 1, \"size\": 2}}", ErrorCode_InvalidAssetDefinition, "Unknown key \"size\" in world model "
                "\"m\", "
            "bounds" },
    };
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            const char* const Fragments[] = { Cases[Index].Fragment, NULL };
            if (!TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, Cases[Index].Json), Cases[Index].Code,
                Fragments))
            {
                printf("      (case %u: %s)\n", (unsigned int)Index, Cases[Index].Json);
            }
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* The limits on variants: at most WORLD_MODEL_MAX_VARIANTS, each with at most WORLD_MODEL_MAX_VARIANT_OVERRIDES
 * overrides, and names of at most WORLD_MODEL_VARIANT_NAME_MAX_LENGTH bytes; exactly at the limits is fine. */
static void VariantLimitsAreEnforced(TestRun* run)
{
    TestAssetFixture Fixture;
    char* Json = Memory_Allocate(WORLD_MODEL_TEST_JSON_CAPACITY);
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        Memory_Free(Json);
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }
    for (size_t Count = WORLD_MODEL_MAX_VARIANTS; Count <= (WORLD_MODEL_MAX_VARIANTS + 1U); Count++)
    {
        int Length = snprintf(Json, WORLD_MODEL_TEST_JSON_CAPACITY,
            "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [");
        for (size_t Index = 0U; Index < Count; Index++)
        {
            Length += snprintf(Json + Length, WORLD_MODEL_TEST_JSON_CAPACITY - (size_t)Length, "%s{\"name\": \"v%u\"}",
                (Index > 0U) ? ", " : "", (unsigned int)Index);
        }
        snprintf(Json + Length, WORLD_MODEL_TEST_JSON_CAPACITY - (size_t)Length, "]}");
        Error Result = BuildDefinition(&Fixture, Json);
        if (Count == WORLD_MODEL_MAX_VARIANTS)
        {
            TEST_CHECK_SUCCESS(run, Result);
        }
        else
        {
            const char* const Fragments[] = { "at most 64 are allowed", NULL };
            TestText_CheckErrorMessage(run, Result, ErrorCode_InvalidAssetDefinition, Fragments);
        }
    }
    for (size_t Count = WORLD_MODEL_MAX_VARIANT_OVERRIDES; Count <= (WORLD_MODEL_MAX_VARIANT_OVERRIDES + 1U); Count++)
    {
        int Length = snprintf(Json, WORLD_MODEL_TEST_JSON_CAPACITY, "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"variants\": [{\"name\": \"v\", \"material_overrides\": [");
        for (size_t Index = 0U; Index < Count; Index++)
        {
            Length += snprintf(Json + Length, WORLD_MODEL_TEST_JSON_CAPACITY - (size_t)Length, "%s{\"slot\": %u}",
                (Index > 0U) ? ", " : "", (unsigned int)Index);
        }
        snprintf(Json + Length, WORLD_MODEL_TEST_JSON_CAPACITY - (size_t)Length, "]}]}");
        Error Result = BuildDefinition(&Fixture, Json);
        if (Count == WORLD_MODEL_MAX_VARIANT_OVERRIDES)
        {
            TEST_CHECK_SUCCESS(run, Result);
        }
        else
        {
            const char* const Fragments[] = { "has 33 material overrides; at most 32 are allowed", NULL };
            TestText_CheckErrorMessage(run, Result, ErrorCode_InvalidAssetDefinition, Fragments);
        }
    }
    for (size_t Length = WORLD_MODEL_VARIANT_NAME_MAX_LENGTH; Length <= (WORLD_MODEL_VARIANT_NAME_MAX_LENGTH + 1U);
        Length++)
    {
        char Name[WORLD_MODEL_VARIANT_NAME_MAX_LENGTH + 2U];
        Memory_Set(Name, 'v', Length);
        Name[Length] = '\0';
        snprintf(Json, WORLD_MODEL_TEST_JSON_CAPACITY, "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"variants\": [{\"name\": \"%s\"}]}", Name);
        Error Result = BuildDefinition(&Fixture, Json);
        if (Length == WORLD_MODEL_VARIANT_NAME_MAX_LENGTH)
        {
            TEST_CHECK_SUCCESS(run, Result);
        }
        else
        {
            const char* const Fragments[] = { "variants[0].name is longer than 128 bytes", NULL };
            TestText_CheckErrorMessage(run, Result, ErrorCode_InvalidAssetDefinition, Fragments);
        }
    }
    Memory_Free(Json);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: loading.
/* Holding a world model holds its LOD models and its variants' textures (as its dependencies): they load once, and
 * unload when the last world model holding them does. */
static void LoadingHoldsTheDependenciesUntilTheLastHolderGoes(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Near = { 0 };
    TestAssetCounters Far = { 0 };
    TestAssetCounters PaintCounters = { 0 };
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    AssetUserID First = ASSET_USER_ID_INVALID;
    AssetUserID Second = ASSET_USER_ID_INVALID;
    bool IsBuilt = TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddModel(run, &Fixture, "near_mesh", Box, &Near)
        && TestAssets_AddModel(run, &Fixture, "far_mesh", Box, &Far)
        && TestAssets_AddTexture(run, &Fixture, "paint", 9U, &PaintCounters)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"lamp\", \"lods\": [{\"model\": \"near_mesh\", "
            "\"max_distance\": 10}, {\"model\": \"far_mesh\", \"max_distance\": 50}], \"variants\": [{\"name\": "
                "\"red\", "
            "\"material_overrides\": [{\"slot\": 0, \"albedo\": \"paint\"}]}]}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"lamp_far\", \"lods\": [{\"model\": \"far_mesh\", "
            "\"max_distance\": 50}]}");
    WorldModel* Lamp = IsBuilt ? LoadWorldModel(run, &Fixture, "lamp", &First) : NULL;
    WorldModel* LampFar = (Lamp != NULL) ? LoadWorldModel(run, &Fixture, "lamp_far", &Second) : NULL;
    if (LampFar != NULL)
    {
        TEST_CHECK(run, (Near.LoadCount == 1U) && (Far.LoadCount == 1U) && (PaintCounters.LoadCount == 1U));
        TEST_CHECK(run, WorldModel_GetLodModel(Lamp, 1U) == WorldModel_GetLodModel(LampFar, 0U)); // one shared asset

        // The first world model goes: its own mesh and texture with it; the shared one stays for the second.
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, First));
        First = ASSET_USER_ID_INVALID;
        TEST_CHECK(run, (Near.UnloadCount == 1U) && (PaintCounters.UnloadCount == 1U) && (Far.UnloadCount == 0U));
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, Second));
        Second = ASSET_USER_ID_INVALID;
        TEST_CHECK(run, Far.UnloadCount == 1U);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A dependency that cannot load fails the world model, names it, and leaves nothing held (the LODs that loaded before
 * it are released). */
static void AMissingDependencyFailsTheLoadAndHoldsNothing(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters MeshCounters = { 0 };
    TestAssetCounters PaintCounters = { 0 };
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddModel(run, &Fixture, "mesh", Box, &MeshCounters)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"broken_lod\", \"lods\": [{\"model\": \"mesh\", "
            "\"max_distance\": 10}, {\"model\": \"missing\", \"max_distance\": 20}]}")
        && TestAssets_AddTexture(run, &Fixture, "paint", 5U, &PaintCounters)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"broken_variant\", \"lods\": [{\"model\": \"mesh\", "
            "\"max_distance\": 10}], \"variants\": [{\"name\": \"blue\", \"material_overrides\": [{\"slot\": 0, "
            "\"albedo\": \"paint\"}]}, {\"name\": \"red\", \"material_overrides\": [{\"slot\": 2, "
            "\"normal\": \"no_such_texture\"}]}]}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        WorldModel* Loaded = NULL;
        const char* const LodFragments[] =
        {
            "World model \"broken_lod\": LOD 1 (model \"missing\"): No definition named \"missing\"", NULL
        };
        TestText_CheckErrorMessage(run, AssetManager_LoadWorldModel(Fixture.Manager,
            (const unsigned char*)u8"broken_lod", User, &Loaded), ErrorCode_InvalidOperation, LodFragments);
        TEST_CHECK(run, (Loaded == NULL) && (MeshCounters.LoadCount == 1U) && (MeshCounters.UnloadCount == 1U));

        const char* const VariantFragments[] =
        {
            "World model \"broken_variant\": variant \"red\", slot 2: texture \"no_such_texture\"", NULL
        };
        TestText_CheckErrorMessage(run, AssetManager_LoadWorldModel(Fixture.Manager,
            (const unsigned char*)u8"broken_variant", User, &Loaded), ErrorCode_InvalidOperation, VariantFragments);
        TEST_CHECK(run, (Loaded == NULL) && (MeshCounters.LoadCount == 2U) && (MeshCounters.UnloadCount == 2U));
        // The first variant's texture had loaded before the second failed: released with the rest.
        TEST_CHECK(run, (PaintCounters.LoadCount == 1U) && (PaintCounters.UnloadCount == 1U));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


/* A variant that overrides a material slot no LOD model has is logged once, when the world model loads (it changes
 * nothing); slots the models have are not. */
static void AnUnusedVariantSlotIsLoggedOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, WORLD_MODEL_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "unused_variant_slot", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    // The fixture's manager, but with the logger.
    TestAssetFixture Fixture;
    Memory_Zero(&Fixture, sizeof(Fixture));
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TEST_CHECK_SUCCESS(run, AssetManager_Construct1(&Log, &Fixture.Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Fixture.Manager, &Fixture.Types,
            &Fixture.Pool))
        && TestAssets_AddModelWithMaterials(run, &Fixture, "two_materials", Box, 2, MatrixIdentity(), NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"cabinet\", \"lods\": [{\"model\": "
            "\"two_materials\", \"max_distance\": 10}], \"variants\": [{\"name\": \"glass\", \"material_overrides\": "
            "[{\"slot\": 1, \"roughness\": 0.1}, {\"slot\": 3, \"roughness\": 0.2}]}]}"))
    {
        TEST_CHECK(run, LoadWorldModel(run, &Fixture, "cabinet", &User) != NULL);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "variant \"glass\" overrides material slot 3, but its LOD "
        "models have 2 material(s)") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "overrides material slot 1") == 0U);
    Memory_Free(Directory._data);
}

/* The model type's material_overrides read the factors with the same parser, so they keep to the same rules. */
static void TheModelTypeSharesTheFactorRules(TestRun* run)
{
    static const RefusedDefinition Cases[] =
    {
        { "{\"name\": \"m\", \"location\": \"m\", \"material_overrides\": [{\"slot\": 0, \"metallic\": 1.2}]}",
            ErrorCode_InvalidAssetDefinition, "\"metallic\" is 1.2; it must be in [0, 1]" },
        { "{\"name\": \"m\", \"location\": \"m\", \"material_overrides\": [{\"slot\": 0, "
            "\"emissive_intensity\": -1}]}", ErrorCode_InvalidAssetDefinition, "it must be at least 0" },
        { "{\"name\": \"m\", \"location\": \"m\", \"material_overrides\": [{\"tint\": \"#ffffff\"}]}",
            ErrorCode_InvalidAssetDefinition, "requires a \"slot\"" },
        { "{\"name\": \"m\", \"location\": \"m\", \"material_overrides\": [{\"slot\": 0, \"roughness\": "
            "\"rough\"}]}", ErrorCode_InvalidJSON, "\"roughness\"" },
    };
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            const char* const Fragments[] = { Cases[Index].Fragment, NULL };
            TestText_CheckErrorMessage(run, BuildDefinitionOfType(&Fixture, Fixture.Types.Model, Cases[Index].Json),
                Cases[Index].Code, Fragments);
        }
        TEST_CHECK_SUCCESS(run, BuildDefinitionOfType(&Fixture, Fixture.Types.Model, "{\"name\": \"m\", \"location\": "
            "\"m\", \"material_overrides\": [{\"slot\": 0, \"metallic\": 1, \"roughness\": 0, "
            "\"emissive_intensity\": 4}]}"));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: variants.
/* A variant is found by its name; applied to one material's maps it changes only the slot it overrides: its textures,
 * then its factors (an emissive map alone shows at white x 1). */
static void VariantsAreFoundByNameAndChangeOnlyTheirSlots(TestRun* run)
{
    TestAssetFixture Fixture;
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    bool IsBuilt = TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddModel(run, &Fixture, "mesh", Box, NULL)
        && TestAssets_AddTexture(run, &Fixture, "dark_albedo", DARK_ALBEDO_ID, NULL)
        && TestAssets_AddTexture(run, &Fixture, "glow", GLOW_ID, NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"desk\", \"lods\": [{\"model\": \"mesh\", "
            "\"max_distance\": 10}], \"variants\": ["
            "{\"name\": \"dark_wood\", \"material_overrides\": [{\"slot\": 0, \"albedo\": \"dark_albedo\", "
            "\"tint\": \"#d0c0b0\", \"roughness\": 0.75}]},"
            "{\"name\": \"glowing\", \"material_overrides\": [{\"slot\": 1, \"emissive\": \"glow\", \"metallic\": "
                "1}]}]}");
    WorldModel* Loaded = IsBuilt ? LoadWorldModel(run, &Fixture, "desk", &User) : NULL;
    if (Loaded != NULL)
    {
        size_t Index = 99U;
        TEST_CHECK(run, WorldModel_TryFindVariant(Loaded, (const unsigned char*)u8"glowing", &Index) && (Index == 1U));
        TEST_CHECK(run, WorldModel_TryFindVariant(Loaded, (const unsigned char*)u8"dark_wood", &Index)
            && (Index == 0U));
        TEST_CHECK(run, !WorldModel_TryFindVariant(Loaded, (const unsigned char*)u8"pink", &Index) && (Index == 0U));
        TEST_CHECK(run, !WorldModel_TryFindVariant(Loaded, NULL, &Index));

        MaterialMap Maps[MATERIAL_MAP_BRDF + 1];
        MaterialMap Untouched[MATERIAL_MAP_BRDF + 1];
        FillMaps(Untouched);

        // dark_wood on material 0: the albedo texture, the tint and the roughness; nothing else.
        FillMaps(Maps);
        WorldModel_ApplyVariant(Loaded, 0U, 0, Maps);
        TEST_CHECK(run, (Maps[MATERIAL_MAP_ALBEDO].texture.id == DARK_ALBEDO_ID)
            && AreColorsEqual(Maps[MATERIAL_MAP_ALBEDO].color, (Color){ 0xd0, 0xc0, 0xb0, 0xff })
            && (Maps[MATERIAL_MAP_ROUGHNESS].value == 0.75f));
        TEST_CHECK(run, (memcmp(&Maps[MATERIAL_MAP_NORMAL], &Untouched[MATERIAL_MAP_NORMAL], sizeof(MaterialMap)) == 0)
            && (memcmp(&Maps[MATERIAL_MAP_METALNESS], &Untouched[MATERIAL_MAP_METALNESS], sizeof(MaterialMap)) == 0)
            && (memcmp(&Maps[MATERIAL_MAP_EMISSION], &Untouched[MATERIAL_MAP_EMISSION], sizeof(MaterialMap)) == 0));

        // dark_wood on material 1 (a slot it does not override), and a variant past the last: nothing changes.
        FillMaps(Maps);
        WorldModel_ApplyVariant(Loaded, 0U, 1, Maps);
        WorldModel_ApplyVariant(Loaded, 7U, 0, Maps);
        TEST_CHECK(run, memcmp(Maps, Untouched, sizeof(Maps)) == 0);

        // glowing on material 1: the emissive map without a colour or intensity of its own shows at white x 1.
        FillMaps(Maps);
        WorldModel_ApplyVariant(Loaded, 1U, 1, Maps);
        TEST_CHECK(run, (Maps[MATERIAL_MAP_EMISSION].texture.id == GLOW_ID)
            && AreColorsEqual(Maps[MATERIAL_MAP_EMISSION].color, WHITE) && (Maps[MATERIAL_MAP_EMISSION].value == 1.0f)
            && (Maps[MATERIAL_MAP_METALNESS].value == 1.0f)
            && (Maps[MATERIAL_MAP_METALNESS].texture.id == Untouched[MATERIAL_MAP_METALNESS].texture.id));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: the model component's overrides.
/* Every setting a placement can override is a flag row "overrides_<key>" (a Bool, off by default) right before its
 * value row, and the typed accessors reach the same fields as the table. */
static void EveryOverrideIsAFlagNextToItsValue(TestRun* run)
{
    static const char* const ValueKeys[WorldModelSetting_Count] =
    {
        [WorldModelSetting_HasOutline] = "has_outline",
        [WorldModelSetting_OmitPixelation] = "omit_pixelation",
        [WorldModelSetting_ShadowTier] = "shadow_tier",
        [WorldModelSetting_CastsShadows] = "casts_shadows",
        [WorldModelSetting_DrawDistance] = "draw_distance",
        [WorldModelSetting_IsDrawDistanceFixed] = "is_draw_distance_fixed",
    };
    const PropertyTable* Table = WorldModelComponent_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(Table));
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component)))
    {
        WorldModelComponent* ModelComponent = WorldModelComponent_FromComponent(Component);
        for (int Setting = 0; Setting < (int)WorldModelSetting_Count; Setting++)
        {
            char FlagKey[64];
            snprintf(FlagKey, sizeof(FlagKey), "overrides_%s", ValueKeys[Setting]);
            const PropertyDescriptor* Flag = PropertyTable_FindByKey(Table, (const unsigned char*)FlagKey);
            const PropertyDescriptor* Value = PropertyTable_FindByKey(Table, (const unsigned char*)ValueKeys[Setting]);
            if (!TEST_CHECK(run, (Flag != NULL) && (Value != NULL)))
            {
                continue;
            }
            TEST_CHECK(run, (Flag->Type == PropertyType_Bool) && !Flag->Default.BoolValue && ((Flag + 1) == Value));

            // The typed flag accessors and the table reach the same field, and both advance the revision.
            TEST_CHECK(run, !WorldModelComponent_IsOverridden(ModelComponent, (WorldModelSetting)Setting));
            uint32_t Revision = WorldComponent_GetRevision(Component);
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(ModelComponent, (WorldModelSetting)Setting,
                true));
            PropertyValue Read = { 0 };
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, Properties_Get(Table, ModelComponent, Flag->Key, &Read))
                && Read.Data.BoolValue && (WorldComponent_GetRevision(Component) == (Revision + 1U)));
            PropertyValue Off = PropertyValue_FromBool(false);
            TEST_CHECK_SUCCESS(run, Properties_Set(Table, ModelComponent, Flag->Key, &Off));
            TEST_CHECK(run, !WorldModelComponent_IsOverridden(ModelComponent, (WorldModelSetting)Setting)
                && (WorldComponent_GetRevision(Component) == (Revision + 2U)));
        }
        TEST_CHECK_ERROR(run, WorldModelComponent_SetOverridden(ModelComponent, WorldModelSetting_Count, true),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, !WorldModelComponent_IsOverridden(ModelComponent, (WorldModelSetting)42));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* The settings in effect: the world model's while every flag is off, the placement's own value for each flag that is on
 * (each alone), the values kept while their flags are off (switching back restores them), and the multiplier on
 * whichever draw distance is in effect. */
static void TheFlagsChooseBetweenTheWorldModelAndThePlacement(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component)))
    {
        WorldModelComponent* Placement = WorldModelComponent_FromComponent(Component);
        WorldModelSettings Asset =
        {
            .HasOutline = false, .OmitsPixelation = false, .ShadowTier = WorldShadowTier_Far, .CastsShadows = false,
            .DrawDistance = 80.0f, .IsDrawDistanceFixed = true
        };
        // The placement's own values, all different from the asset's; their flags stay off for now.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasOutline(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOmitsPixelation(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowTier(Placement, WorldShadowTier_Near));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetCastsShadows(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistance(Placement, 30.0f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsDrawDistanceFixed(Placement, false));

        WorldModelSettings Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, AreSettingsEqual(&Resolved, &Asset));

        // Each flag alone takes exactly its own setting from the placement.
        for (int Setting = 0; Setting < (int)WorldModelSetting_Count; Setting++)
        {
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, (WorldModelSetting)Setting, true));
            Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
            TEST_CHECK(run, Resolved.HasOutline == (Setting == (int)WorldModelSetting_HasOutline));
            TEST_CHECK(run, Resolved.OmitsPixelation == (Setting == (int)WorldModelSetting_OmitPixelation));
            TEST_CHECK(run, Resolved.ShadowTier
                == ((Setting == (int)WorldModelSetting_ShadowTier) ? WorldShadowTier_Near : WorldShadowTier_Far));
            TEST_CHECK(run, Resolved.CastsShadows == (Setting == (int)WorldModelSetting_CastsShadows));
            TEST_CHECK(run, Resolved.DrawDistance
                == ((Setting == (int)WorldModelSetting_DrawDistance) ? 30.0f : 80.0f));
            TEST_CHECK(run, Resolved.IsDrawDistanceFixed == (Setting != (int)WorldModelSetting_IsDrawDistanceFixed));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, (WorldModelSetting)Setting, false));
        }

        // The multiplier scales the distance in effect, the asset's or the placement's.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistanceMultiplier(Placement, 2.5f));
        TEST_CHECK(run, WorldModelComponent_ResolveSettings(Placement, &Asset).DrawDistance == 200.0f);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_DrawDistance, true));
        TEST_CHECK(run, WorldModelComponent_ResolveSettings(Placement, &Asset).DrawDistance == 75.0f);

        // Off and on again: the placement's value was kept.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_DrawDistance, false));
        TEST_CHECK(run, WorldModelComponent_GetDrawDistance(Placement) == 30.0f);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_DrawDistance, true));
        TEST_CHECK(run, WorldModelComponent_ResolveSettings(Placement, &Asset).DrawDistance == 75.0f);

        // A product past the float range is +infinity: seen from everywhere, never a broken number.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistance(Placement, 3e38f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistanceMultiplier(Placement, 1e10f));
        TEST_CHECK(run, isinf(WorldModelComponent_ResolveSettings(Placement, &Asset).DrawDistance));

        // The rules: distances and multipliers above 0 and finite, variant names of at most 128 bytes.
        TEST_CHECK_ERROR(run, WorldModelComponent_SetDrawDistance(Placement, 0.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetDrawDistance(Placement, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetDrawDistanceMultiplier(Placement, -1.0f),
            ErrorCode_ArgumentOutOfRange);
        char LongName[WORLD_MODEL_VARIANT_NAME_MAX_LENGTH + 2U];
        Memory_Set(LongName, 'v', WORLD_MODEL_VARIANT_NAME_MAX_LENGTH + 1U);
        LongName[WORLD_MODEL_VARIANT_NAME_MAX_LENGTH + 1U] = '\0';
        TEST_CHECK_ERROR(run, WorldModelComponent_SetVariant(Placement, (const unsigned char*)LongName),
            ErrorCode_ArgumentOutOfRange);
        LongName[WORLD_MODEL_VARIANT_NAME_MAX_LENGTH] = '\0';
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetVariant(Placement, (const unsigned char*)LongName));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* The component starts at its documented defaults: no world model, no variant, no override, the placement's own values
 * at the old component defaults, 600 m and a multiplier of 1. */
static void TheComponentStartsAtItsDefaults(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component)))
    {
        WorldModelComponent* Placement = WorldModelComponent_FromComponent(Component);
        TEST_CHECK(run, (WorldModelComponent_GetWorldModel(Placement) == NULL)
            && !WorldModelComponent_UsesVariant(Placement)
            && (WorldModelComponent_GetVariant(Placement) == NULL));
        for (int Setting = 0; Setting < (int)WorldModelSetting_Count; Setting++)
        {
            TEST_CHECK(run, !WorldModelComponent_IsOverridden(Placement, (WorldModelSetting)Setting));
        }
        TEST_CHECK(run, WorldModelComponent_HasOutline(Placement) && !WorldModelComponent_OmitsPixelation(Placement)
            && (WorldModelComponent_GetShadowTier(Placement) == WorldShadowTier_Both)
            && WorldModelComponent_CastsShadows(Placement)
            && (WorldModelComponent_GetDrawDistance(Placement) == WORLD_MODEL_DEFAULT_DRAW_DISTANCE)
            && (WorldModelComponent_GetDrawDistanceMultiplier(Placement) == 1.0f)
            && !WorldModelComponent_IsDrawDistanceFixed(Placement));
        WorldModelSettings Defaults = WorldModelSettings_CreateDefault();
        WorldModelSettings Resolved = WorldModelComponent_ResolveSettings(Placement, &Defaults);
        TEST_CHECK(run, AreSettingsEqual(&Resolved, &Defaults));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}


// Tests: the shipped content.
/* The game's world models (compile/out/asset/world_models) are all read without a skipped file. */
static void TheShippedWorldModelsAreRead(TestRun* run)
{
    static const char* const Names[] = { "test", "pbr_demo", "pbr_metal", "painted_cube" };
    TestAssetFixture Fixture;
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, WORLD_MODEL_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TestAssetFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Root._data)))
    {
        size_t FailedCount = 1U;
        TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, &FailedCount));
        TEST_CHECK(run, FailedCount == 0U);
        for (size_t Index = 0U; Index < (sizeof(Names) / sizeof(Names[0])); Index++)
        {
            bool HasDefinition = false;
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, AssetManager_HasDefinition(Fixture.Manager,
                Fixture.Types.WorldModel, (const unsigned char*)Names[Index], &HasDefinition)) && HasDefinition);
        }
    }
    Memory_Free(Root._data);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void WorldModelTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_minimal_definition_gets_the_defaults", AMinimalDefinitionGetsTheDefaults },
        { "a_full_definition_reads_every_key", AFullDefinitionReadsEveryKey },
        { "each_rule_is_refused_with_a_clear_message", EachRuleIsRefusedWithAClearMessage },
        { "variant_limits_are_enforced", VariantLimitsAreEnforced },
        { "loading_holds_the_dependencies_until_the_last_holder_goes",
            LoadingHoldsTheDependenciesUntilTheLastHolderGoes },
        { "a_missing_dependency_fails_the_load_and_holds_nothing", AMissingDependencyFailsTheLoadAndHoldsNothing },
        { "an_unused_variant_slot_is_logged_once", AnUnusedVariantSlotIsLoggedOnce },
        { "the_model_type_shares_the_factor_rules", TheModelTypeSharesTheFactorRules },
        { "variants_are_found_by_name_and_change_only_their_slots", VariantsAreFoundByNameAndChangeOnlyTheirSlots },
        { "every_override_is_a_flag_next_to_its_value", EveryOverrideIsAFlagNextToItsValue },
        { "the_flags_choose_between_the_world_model_and_the_placement",
            TheFlagsChooseBetweenTheWorldModelAndThePlacement },
        { "the_component_starts_at_its_defaults", TheComponentStartsAtItsDefaults },
        { "the_shipped_world_models_are_read", TheShippedWorldModelsAreRead },
    };
    TestRun_RunSuite(run, "WorldModel", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
