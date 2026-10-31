#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TestWorld.h"
#include "GameTexture.h"
#include "Logger.h"
#include "World.h"
#include "WorldModel.h"
#include "WorldModelComponent.h"
#include "WorldModelInstancesComponent.h"
#include "WorldObject.h"
#include "PropertyDescriptorInternal.h"
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
        && (a->CastsShadows == b->CastsShadows) && (a->HasShadowDistance == b->HasShadowDistance)
        && (a->ShadowDistance == b->ShadowDistance) && (a->IsShadowDistanceFixed == b->IsShadowDistanceFixed)
        && (a->IsShadowOnly == b->IsShadowOnly) && (a->HasTwoSidedShadows == b->HasTwoSidedShadows)
        && (a->HasShadowLod == b->HasShadowLod) && (a->ShadowLod == b->ShadowLod)
        && (a->DrawDistance == b->DrawDistance) && (a->IsDrawDistanceFixed == b->IsDrawDistanceFixed)
        && (a->Haze == b->Haze) && (a->OccludesSky == b->OccludesSky);
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
            TEST_CHECK(run, Settings->HasOutline && !Settings->OmitsPixelation && Settings->CastsShadows);
            // Casting as far as it is drawn, drawn itself, from its front faces, with the LODs the renderer picks.
            TEST_CHECK(run, !Settings->HasShadowDistance
                && (Settings->ShadowDistance == WORLD_MODEL_DEFAULT_SHADOW_DISTANCE) && !Settings->IsShadowDistanceFixed
                && !Settings->IsShadowOnly && !Settings->HasTwoSidedShadows && !Settings->HasShadowLod
                && (Settings->ShadowLod == 0U));
            TEST_CHECK(run, (Settings->DrawDistance == 25.0f) && !Settings->IsDrawDistanceFixed);
            TEST_CHECK(run, Settings->Haze == 1.0f); // the real air's haze
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

/* A full definition: three LODs, every setting, a variant with a texture and factors, and explicit bounds. */
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
        "  \"render\": { \"has_outline\": false, \"omit_pixelation\": true, \"casts_shadows\": false, "
        "\"has_shadow_distance\": true, \"shadow_distance\": 35, \"is_shadow_distance_fixed\": true, "
        "\"is_shadow_only\": true, \"has_two_sided_shadows\": true, \"has_shadow_lod\": true, \"shadow_lod\": 2, "
        "\"haze\": 0.25, \"occludes_sky\": false },\n"
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
            && !Settings->CastsShadows && (Settings->DrawDistance == 120.0f) && Settings->IsDrawDistanceFixed
            && (Settings->Haze == 0.25f) && !Settings->OccludesSky);
        TEST_CHECK(run, Settings->HasShadowDistance && (Settings->ShadowDistance == 35.0f)
            && Settings->IsShadowDistanceFixed && Settings->IsShadowOnly && Settings->HasTwoSidedShadows
            && Settings->HasShadowLod && (Settings->ShadowLod == 2U));
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
            "World model \"tbp:m\": lods[0]: \"max_distance\"" },
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
            ErrorCode_InvalidAssetDefinition, "Unknown key \"lodz\" in world model \"tbp:m\"; the keys allowed there are: "
                "name, "
            "lods, draw_distance" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"collision\": {}}",
            ErrorCode_InvalidAssetDefinition, "collision.sets must hold 1 to 32 sets" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"maxdistance\": 40}]}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"maxdistance\" in world model \"tbp:m\", lods[0]; the keys allowed there are: model, "
                "max_distance" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"\\u001b[31m\": 1}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"?[31m\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": 4}",
            ErrorCode_InvalidAssetDefinition, "render must be a JSON object" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"outline\": true}}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"outline\" in world model \"tbp:m\", render" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"shadow_tier\": "
            "\"middle\"}}", ErrorCode_InvalidAssetDefinition,
            "render.shadow_tier is \"middle\"; it must be \"both\", \"near\" or \"far\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"shadow_tier\": "
            "3}}", ErrorCode_InvalidAssetDefinition, "render.shadow_tier must be \"both\", \"near\" or \"far\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": "
            "{\"shadow_distance\": 0}}", ErrorCode_InvalidAssetDefinition,
            "render.shadow_distance is 0; it must be above 0" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": "
            "{\"shadow_distance\": \"far\"}}", ErrorCode_InvalidJSON, "render" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": "
            "{\"has_shadow_distance\": 1}}", ErrorCode_InvalidAssetDefinition,
            "render.has_shadow_distance must be true or false" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": "
            "{\"is_shadow_only\": \"yes\"}}", ErrorCode_InvalidAssetDefinition, "render.is_shadow_only must be true or false" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": "
            "{\"shadow_lod\": -1}}", ErrorCode_InvalidAssetDefinition, "render.shadow_lod is -1; it must be 0 or above" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": "
            "{\"shadow_lod\": 1.5}}", ErrorCode_InvalidAssetDefinition, "render.shadow_lod must be a whole number" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"has_outline\": "
            "\"yes\"}}", ErrorCode_InvalidAssetDefinition, "render.has_outline must be true or false" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"haze\": -1}}",
            ErrorCode_InvalidAssetDefinition, "render.haze is -1; it must be at least 0 and at most 1000" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"haze\": 1001}}",
            ErrorCode_InvalidAssetDefinition, "render.haze is 1001" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"render\": {\"haze\": "
            "\"none\"}}", ErrorCode_InvalidJSON, "render" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": {}}",
            ErrorCode_InvalidAssetDefinition, "variants must be an array" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{}]}",
            ErrorCode_InvalidAssetDefinition, "variants[0] needs a \"name\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"Dark Wood\"}]}", ErrorCode_InvalidAssetDefinition, "variants[0].name \"Dark Wood\" is not a valid "
                "variant name" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\"}, "
            "{\"name\": \"dark\"}]}", ErrorCode_InvalidAssetDefinition,
            "variants[1].name \"dark\" is used by variants[0] too" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"variants\": [{\"name\": "
            "\"dark\", "
            "\"look\": 1}]}", ErrorCode_InvalidAssetDefinition, "Unknown key \"look\" in world model \"tbp:m\", "
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
            "Unknown key \"texture_properties\" in world model \"tbp:m\", variants[0].material_overrides[0]" },
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
                "\"tbp:m\", "
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

/* The models a world model names are asset names: "mesh" and "tbp:mesh" are one model, "mymod:mesh" another; an
 * invalid name is refused naming the problem, and a variant's name is a local name, never namespaced. */
static void ReferencesAreAssetNames(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters BaseCounters = { 0 };
    TestAssetCounters ModCounters = { 0 };
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddModel(run, &Fixture, "mesh", Box, &BaseCounters)
        && TestAssets_AddModel(run, &Fixture, "mymod:mesh", Box, &ModCounters)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"short_ref\", \"lods\": [{\"model\": \"mesh\", "
            "\"max_distance\": 10}]}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"full_ref\", \"lods\": [{\"model\": \"tbp:mesh\", "
            "\"max_distance\": 10}]}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"mymod:mod_ref\", \"lods\": [{\"model\": "
            "\"mymod:mesh\", \"max_distance\": 10}]}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        WorldModel* Short = NULL;
        WorldModel* Full = NULL;
        WorldModel* Mod = NULL;
        TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"short_ref", User,
            &Short));
        TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"full_ref", User,
            &Full));
        TEST_CHECK(run, (Short != NULL) && (Full != NULL) && (BaseCounters.LoadCount == 1U)
            && (ModCounters.LoadCount == 0U));
        TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"mymod:mod_ref", User,
            &Mod));
        TEST_CHECK(run, (Mod != NULL) && (BaseCounters.LoadCount == 1U) && (ModCounters.LoadCount == 1U));
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    if (Fixture.Manager != NULL)
    {
        const char* const InvalidFragments[] = { "lods[0].model \"Mesh\" is not a valid asset name: uppercase", NULL };
        TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, "{\"name\": \"m\", \"lods\": [{\"model\": \"Mesh\", "
            "\"max_distance\": 10}]}"), ErrorCode_InvalidAssetDefinition, InvalidFragments);
        const char* const VariantFragments[] = { "variants[0].name \"tbp:dark\" is not a valid variant name", NULL };
        TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, "{\"name\": \"m\", \"lods\": [{\"model\": \"mesh\", "
            "\"max_distance\": 10}], \"variants\": [{\"name\": \"tbp:dark\"}]}"), ErrorCode_InvalidAssetDefinition,
            VariantFragments);
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
            "World model \"tbp:broken_lod\": LOD 1 (model \"tbp:missing\"): No definition named \"tbp:missing\"", NULL
        };
        TestText_CheckErrorMessage(run, AssetManager_LoadWorldModel(Fixture.Manager,
            (const unsigned char*)u8"broken_lod", User, &Loaded), ErrorCode_InvalidOperation, LodFragments);
        TEST_CHECK(run, (Loaded == NULL) && (MeshCounters.LoadCount == 1U) && (MeshCounters.UnloadCount == 1U));

        const char* const VariantFragments[] =
        {
            "World model \"tbp:broken_variant\": variant \"red\", slot 2: texture \"tbp:no_such_texture\"", NULL
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

/* The shadow settings' hand-written forms: a shadow_distance or a shadow_lod given without its has_ flag turns the flag
 * on; an explicit flag wins, and the value is then kept as given. */
static void HandWrittenShadowKeysTurnTheirFlagsOn(TestRun* run)
{
    TestAssetFixture Fixture;
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddModel(run, &Fixture, "box", Box, NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"shed\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"render\": {\"shadow_distance\": 25, \"shadow_lod\": 1}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"kept\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"render\": {\"has_shadow_distance\": false, \"shadow_distance\": 25, "
            "\"has_shadow_lod\": false, \"shadow_lod\": 1}}"))
    {
        WorldModel* Shed = LoadWorldModel(run, &Fixture, "shed", &User);
        WorldModel* Kept = LoadWorldModel(run, &Fixture, "kept", &User);
        if ((Shed != NULL) && (Kept != NULL))
        {
            const WorldModelSettings* Given = WorldModel_GetSettings(Shed);
            const WorldModelSettings* Off = WorldModel_GetSettings(Kept);
            TEST_CHECK(run, Given->HasShadowDistance && (Given->ShadowDistance == 25.0f) && Given->HasShadowLod
                && (Given->ShadowLod == 1U));
            TEST_CHECK(run, !Off->HasShadowDistance && (Off->ShadowDistance == 25.0f) && !Off->HasShadowLod
                && (Off->ShadowLod == 1U));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* An older definition's render.shadow_tier is read with one warning each: "near" as a 16 m shadow distance (or the
 * definition's own), "far" and "both" as nothing. */
static void AnOlderDefinitionsShadowTierIsReadAsTheShadowDistance(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, WORLD_MODEL_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "legacy_shadow_tier", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    TestAssetFixture Fixture;
    Memory_Zero(&Fixture, sizeof(Fixture));
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TEST_CHECK_SUCCESS(run, AssetManager_Construct1(&Log, &Fixture.Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Fixture.Manager, &Fixture.Types,
            &Fixture.Pool))
        && TestAssets_AddModel(run, &Fixture, "box", Box, NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"cup\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"render\": {\"shadow_tier\": \"near\"}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"vase\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"render\": {\"shadow_tier\": \"near\", \"shadow_distance\": 30}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"tower\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"render\": {\"shadow_tier\": \"far\"}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"crate\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"render\": {\"shadow_tier\": \"both\"}}"))
    {
        WorldModel* Cup = LoadWorldModel(run, &Fixture, "cup", &User);
        WorldModel* Vase = LoadWorldModel(run, &Fixture, "vase", &User);
        WorldModel* Tower = LoadWorldModel(run, &Fixture, "tower", &User);
        WorldModel* Crate = LoadWorldModel(run, &Fixture, "crate", &User);
        TEST_CHECK(run, (Cup != NULL) && WorldModel_GetSettings(Cup)->HasShadowDistance
            && (WorldModel_GetSettings(Cup)->ShadowDistance == WORLD_MODEL_NEAR_TIER_SHADOW_DISTANCE));
        TEST_CHECK(run, (Vase != NULL) && WorldModel_GetSettings(Vase)->HasShadowDistance
            && (WorldModel_GetSettings(Vase)->ShadowDistance == 30.0f));
        TEST_CHECK(run, (Tower != NULL) && !WorldModel_GetSettings(Tower)->HasShadowDistance);
        TEST_CHECK(run, (Crate != NULL) && !WorldModel_GetSettings(Crate)->HasShadowDistance
            && WorldModel_GetSettings(Crate)->CastsShadows);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "render.shadow_tier \"near\" is read as "
        "render.shadow_distance") == 2U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "World model \"tbp:cup\": render.shadow_tier \"near\" is read "
        "as render.shadow_distance 16") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "World model \"tbp:tower\": render.shadow_tier \"far\" is not "
        "read") == 1U);
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "World model \"tbp:crate\": render.shadow_tier \"both\" is what "
        "every model does") == 1U);
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
        [WorldModelSetting_CastsShadows] = "casts_shadows",
        [WorldModelSetting_DrawDistance] = "draw_distance",
        [WorldModelSetting_IsDrawDistanceFixed] = "is_draw_distance_fixed",
        [WorldModelSetting_LodDistances] = "lod_distances",
        [WorldModelSetting_Haze] = "haze",
        [WorldModelSetting_OccludesSky] = "occludes_sky",
        [WorldModelSetting_ShadowDistance] = "shadow_distance",
        [WorldModelSetting_IsShadowDistanceFixed] = "is_shadow_distance_fixed",
        [WorldModelSetting_IsShadowOnly] = "is_shadow_only",
        [WorldModelSetting_HasTwoSidedShadows] = "has_two_sided_shadows",
        [WorldModelSetting_ShadowLod] = "shadow_lod",
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
            .HasOutline = false, .OmitsPixelation = false, .CastsShadows = false, .HasShadowDistance = true,
            .ShadowDistance = 50.0f, .IsShadowDistanceFixed = true, .IsShadowOnly = false, .HasTwoSidedShadows = false,
            .HasShadowLod = false, .ShadowLod = 0U, .DrawDistance = 80.0f, .IsDrawDistanceFixed = true, .Haze = 0.5f,
            .OccludesSky = false
        };
        // The placement's own values, all different from the asset's; their flags stay off for now.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasOutline(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOmitsPixelation(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetCastsShadows(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistance(Placement, 12.0f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsShadowDistanceFixed(Placement, false));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsShadowOnly(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasTwoSidedShadows(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowLod(Placement, 3U));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistance(Placement, 30.0f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsDrawDistanceFixed(Placement, false));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHaze(Placement, 0.0f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOccludesSky(Placement, true));

        WorldModelSettings Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, AreSettingsEqual(&Resolved, &Asset));

        // Each flag alone takes exactly its own setting from the placement.
        for (int Setting = 0; Setting < (int)WorldModelSetting_Count; Setting++)
        {
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, (WorldModelSetting)Setting, true));
            Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
            TEST_CHECK(run, Resolved.HasOutline == (Setting == (int)WorldModelSetting_HasOutline));
            TEST_CHECK(run, Resolved.OmitsPixelation == (Setting == (int)WorldModelSetting_OmitPixelation));
            TEST_CHECK(run, Resolved.CastsShadows == (Setting == (int)WorldModelSetting_CastsShadows));
            TEST_CHECK(run, Resolved.HasShadowDistance
                && (Resolved.ShadowDistance == ((Setting == (int)WorldModelSetting_ShadowDistance) ? 12.0f : 50.0f)));
            TEST_CHECK(run, Resolved.IsShadowDistanceFixed
                == (Setting != (int)WorldModelSetting_IsShadowDistanceFixed));
            TEST_CHECK(run, Resolved.IsShadowOnly == (Setting == (int)WorldModelSetting_IsShadowOnly));
            TEST_CHECK(run, Resolved.HasTwoSidedShadows == (Setting == (int)WorldModelSetting_HasTwoSidedShadows));
            TEST_CHECK(run, (Resolved.HasShadowLod == (Setting == (int)WorldModelSetting_ShadowLod))
                && (Resolved.ShadowLod == ((Setting == (int)WorldModelSetting_ShadowLod) ? 3U : 0U)));
            TEST_CHECK(run, Resolved.DrawDistance
                == ((Setting == (int)WorldModelSetting_DrawDistance) ? 30.0f : 80.0f));
            TEST_CHECK(run, Resolved.IsDrawDistanceFixed == (Setting != (int)WorldModelSetting_IsDrawDistanceFixed));
            TEST_CHECK(run, Resolved.Haze == ((Setting == (int)WorldModelSetting_Haze) ? 0.0f : 0.5f));
            TEST_CHECK(run, Resolved.OccludesSky == (Setting == (int)WorldModelSetting_OccludesSky));
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

        // The rules: distances and multipliers above 0 and finite, the haze in [0, 1000], variant names of at most 128
        // bytes.
        TEST_CHECK_ERROR(run, WorldModelComponent_SetHaze(Placement, -0.5f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetHaze(Placement, 1001.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetHaze(Placement, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, WorldModelComponent_GetHaze(Placement) == 0.0f);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHaze(Placement, 1000.0f));
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
 * at the settings' defaults, 600 m and a multiplier of 1. */
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
            && WorldModelComponent_CastsShadows(Placement)
            && (WorldModelComponent_GetDrawDistance(Placement) == WORLD_MODEL_DEFAULT_DRAW_DISTANCE)
            && (WorldModelComponent_GetDrawDistanceMultiplier(Placement) == 1.0f)
            && !WorldModelComponent_IsDrawDistanceFixed(Placement) && (WorldModelComponent_GetHaze(Placement) == 1.0f)
            && WorldModelComponent_OccludesSky(Placement));
        // No LOD distances of its own, a LOD multiplier of 1, the LOD chosen by distance (forced LOD 0 kept).
        size_t LodDistanceCount = 1U;
        TEST_CHECK(run, (WorldModelComponent_GetLodDistances(Placement, &LodDistanceCount) == NULL)
            && (LodDistanceCount == 0U) && (WorldModelComponent_GetLodDistanceMultiplier(Placement) == 1.0f)
            && !WorldModelComponent_IsLodForced(Placement) && (WorldModelComponent_GetForcedLod(Placement) == 0U));
        // Its own shadow values: 600 m, a multiplier of 1, not fixed, drawn, front faces, shadow LOD 0 kept.
        TEST_CHECK(run, (WorldModelComponent_GetShadowDistance(Placement) == WORLD_MODEL_DEFAULT_SHADOW_DISTANCE)
            && (WorldModelComponent_GetShadowDistanceMultiplier(Placement) == 1.0f)
            && !WorldModelComponent_IsShadowDistanceFixed(Placement) && !WorldModelComponent_IsShadowOnly(Placement)
            && !WorldModelComponent_HasTwoSidedShadows(Placement) && (WorldModelComponent_GetShadowLod(Placement) == 0U));
        // The world model's settings, the shadow distance being the draw distance (it casts as far as it is drawn).
        WorldModelSettings Defaults = WorldModelSettings_CreateDefault();
        Defaults.DrawDistance = 250.0f;
        WorldModelSettings Resolved = WorldModelComponent_ResolveSettings(Placement, &Defaults);
        TEST_CHECK(run, !Resolved.HasShadowDistance && (Resolved.ShadowDistance == 250.0f));
        Resolved.ShadowDistance = Defaults.ShadowDistance;
        TEST_CHECK(run, AreSettingsEqual(&Resolved, &Defaults));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* The shadow distance in effect: the placement's own while it overrides it, else its world model's, else its draw
 * distance in effect (the draw distance's multiplier in it); then times the shadow multiplier, which also scales the draw
 * distance standing in for one; +infinity past the float range, 0 below it. */
static void TheShadowDistanceComesFromThePlacementTheWorldModelOrTheDrawDistance(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    WorldComponent* InstancesComponent = NULL;
    if (TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object),
            WorldComponentType_ModelInstances, &InstancesComponent)))
    {
        WorldModelComponent* Placement = WorldModelComponent_FromComponent(Component);
        WorldModelInstancesComponent* Instances = WorldModelInstancesComponent_FromComponent(InstancesComponent);
        WorldModelSettings Asset = WorldModelSettings_CreateDefault();
        Asset.DrawDistance = 80.0f;
        Asset.ShadowDistance = 999.0f; // kept unused while the asset has no shadow distance of its own
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistanceMultiplier(Placement, 2.0f));
        TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetDrawDistanceMultiplier(Instances, 2.0f));

        // Without one: the draw distance in effect (80 x 2), not the asset's kept value.
        WorldModelSettings Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, !Resolved.HasShadowDistance && (Resolved.ShadowDistance == 160.0f)
            && (Resolved.DrawDistance == 160.0f));
        // The shadow multiplier scales the draw distance that stands in for it.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistanceMultiplier(Placement, 0.25f));
        TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetShadowDistanceMultiplier(Instances, 0.25f));
        Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, !Resolved.HasShadowDistance && (Resolved.ShadowDistance == 40.0f));
        // The world model's own: its value times the shadow multiplier (the draw multiplier does not touch it).
        Asset.HasShadowDistance = true;
        Asset.ShadowDistance = 30.0f;
        Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, Resolved.HasShadowDistance && (Resolved.ShadowDistance == 7.5f));
        // The placement's own wins over both, times the shadow multiplier.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistance(Placement, 100.0f));
        TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetShadowDistance(Instances, 100.0f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_ShadowDistance, true));
        TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetOverridden(Instances, WorldModelSetting_ShadowDistance,
            true));
        Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, Resolved.HasShadowDistance && (Resolved.ShadowDistance == 25.0f));
        Asset.HasShadowDistance = false;
        Resolved = WorldModelComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, Resolved.HasShadowDistance && (Resolved.ShadowDistance == 25.0f));
        WorldModelSettings FromInstances = WorldModelInstancesComponent_ResolveSettings(Instances, &Asset);
        TEST_CHECK(run, FromInstances.HasShadowDistance && (FromInstances.ShadowDistance == 25.0f));

        // Past the float range +infinity (casting from everywhere, never a broken number); two tiny factors 0.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistance(Placement, 3e38f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistanceMultiplier(Placement, 1e10f));
        TEST_CHECK(run, isinf(WorldModelComponent_ResolveSettings(Placement, &Asset).ShadowDistance));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistance(Placement, 1e-30f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistanceMultiplier(Placement, 1e-30f));
        TEST_CHECK(run, WorldModelComponent_ResolveSettings(Placement, &Asset).ShadowDistance == 0.0f);

        // The rules: the distance and the multiplier above 0 and finite; nothing changes on a refusal.
        TEST_CHECK_ERROR(run, WorldModelComponent_SetShadowDistance(Placement, -1.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetShadowDistance(Placement, INFINITY), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelInstancesComponent_SetShadowDistance(Instances, 0.0f),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelInstancesComponent_SetShadowDistanceMultiplier(Instances, -2.0f),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, (WorldModelComponent_GetShadowDistance(Placement) == 1e-30f)
            && (WorldModelInstancesComponent_GetShadowDistance(Instances) == 100.0f)
            && (WorldModelInstancesComponent_GetShadowDistanceMultiplier(Instances) == 0.25f));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}


/* The LOD settings in effect: the world model's switch distances, or the placement's own list while its flag is on and it
 * has one distance per LOD (otherwise the world model's, and the call says so unless the LOD is forced, for the
 * renderer's warning), times the LOD multiplier; the world model's hysteresis and fade time; the forced LOD clamped to
 * the last one and kept as written. The rules refuse lists that do not rise, hold a distance that is not finite or not
 * above 0, or more than 8 distances, and a refused set changes nothing; JSON carries the list through the table. */
static void ThePlacementPicksItsLodSettings(TestRun* run)
{
    TestAssetFixture Fixture;
    BoundingBox Box = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    World TestWorld;
    KeyRegistry Keys;
    bool IsWorldBuilt = false;
    WorldModel* Loaded = NULL;
    WorldComponent* Component = NULL;
    WorldObject* Object = NULL;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddModel(run, &Fixture, "lamp_lod0", Box, NULL)
        && TestAssets_AddModel(run, &Fixture, "lamp_lod1", Box, NULL)
        && TestAssets_AddModel(run, &Fixture, "lamp_lod2", Box, NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"lamp\", \"lods\": [{\"model\": \"lamp_lod0\", "
            "\"max_distance\": 10}, {\"model\": \"lamp_lod1\", \"max_distance\": 30}, {\"model\": \"lamp_lod2\", "
            "\"max_distance\": 90}], \"lod_fade_seconds\": 0.5, \"lod_hysteresis\": 0.2}")
        && ((Loaded = LoadWorldModel(run, &Fixture, "lamp", &User)) != NULL)
        && (IsWorldBuilt = TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
        && TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component)))
    {
        WorldModelComponent* Placement = WorldModelComponent_FromComponent(Component);
        WorldModelLodSettings Settings;
        TEST_CHECK(run, WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings));
        TEST_CHECK(run, (Settings.LodCount == 3U) && (Settings.SwitchDistances[0] == 10.0f)
            && (Settings.SwitchDistances[1] == 30.0f) && (Settings.SwitchDistances[2] == 90.0f)
            && (Settings.SwitchDistances[3] == 0.0f) && (Settings.Hysteresis == 0.2f) && (Settings.FadeSeconds == 0.5f)
            && !Settings.IsLodForced && (Settings.ForcedLod == 0U));

        // The placement's own list is kept, unused, while its flag is off; on, it replaces the world model's.
        const float Own[] = { 5.0f, 12.0f, 40.0f };
        uint32_t Revision = WorldComponent_GetRevision(Component);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, Own, 3U));
        TEST_CHECK(run, WorldComponent_GetRevision(Component) == (Revision + 1U));
        TEST_CHECK(run, WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings)
            && (Settings.SwitchDistances[0] == 10.0f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_LodDistances, true));
        TEST_CHECK(run, WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings)
            && (Settings.SwitchDistances[0] == 5.0f) && (Settings.SwitchDistances[1] == 12.0f)
            && (Settings.SwitchDistances[2] == 40.0f));

        // The multiplier scales whichever distances are in effect.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistanceMultiplier(Placement, 2.0f));
        TEST_CHECK(run, WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings)
            && (Settings.SwitchDistances[0] == 10.0f) && (Settings.SwitchDistances[2] == 80.0f));

        // A list that does not fit the world model (it lost a LOD, or gained one): the world model's distances, and false.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, Own, 2U));
        TEST_CHECK(run, !WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings)
            && (Settings.SwitchDistances[0] == 20.0f) && (Settings.SwitchDistances[1] == 60.0f)
            && (Settings.SwitchDistances[2] == 180.0f));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, NULL, 0U));
        TEST_CHECK(run, !WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_LodDistances, false));
        TEST_CHECK(run, WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings));

        // A forced LOD, clamped to the last one in the settings and kept as written.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsLodForced(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetForcedLod(Placement, 1U));
        TEST_CHECK(run, WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings) && Settings.IsLodForced
            && (Settings.ForcedLod == 1U));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetForcedLod(Placement, 7U));
        TEST_CHECK(run, WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings)
            && (Settings.ForcedLod == 2U) && (WorldModelComponent_GetForcedLod(Placement) == 7U));

        // While the LOD is forced no distance matters, so an own list that does not fit is not reported; unforced, it is.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, WorldModelSetting_LodDistances, true));
        TEST_CHECK(run, WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings) && Settings.IsLodForced);
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsLodForced(Placement, false));
        TEST_CHECK(run, !WorldModelComponent_ResolveLodSettings(Placement, Loaded, &Settings));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsLodForced(Placement, true));

        // The rules; a refused set changes nothing.
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, Own, 3U));
        const float Flat[] = { 5.0f, 5.0f };
        const float Falling[] = { 5.0f, 3.0f };
        const float Zero[] = { 0.0f, 3.0f };
        const float NotFinite[] = { 2.0f, INFINITY };
        const float Missing[] = { NAN };
        const float Nine[] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f };
        const char* const RiseFragments[] = { "model_component.lod_distances", "rise strictly", NULL };
        const char* const AboveFragments[] = { "lod_distances (_lodDistances) must have every value finite and > 0",
            NULL };
        const char* const CountFragments[] = { "lod_distances", "hold at most 8 values", NULL };
        TestText_CheckErrorMessage(run, WorldModelComponent_SetLodDistances(Placement, Flat, 2U),
            ErrorCode_ArgumentOutOfRange, RiseFragments);
        TestText_CheckErrorMessage(run, WorldModelComponent_SetLodDistances(Placement, Falling, 2U),
            ErrorCode_ArgumentOutOfRange, RiseFragments);
        TestText_CheckErrorMessage(run, WorldModelComponent_SetLodDistances(Placement, Zero, 2U),
            ErrorCode_ArgumentOutOfRange, AboveFragments);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetLodDistances(Placement, NotFinite, 2U),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetLodDistances(Placement, Missing, 1U), ErrorCode_ArgumentOutOfRange);
        TestText_CheckErrorMessage(run, WorldModelComponent_SetLodDistances(Placement, Nine, 9U),
            ErrorCode_ArgumentOutOfRange, CountFragments);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetLodDistances(Placement, NULL, 2U), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetLodDistanceMultiplier(Placement, 0.0f),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelComponent_SetLodDistanceMultiplier(Placement, NAN), ErrorCode_ArgumentOutOfRange);
        size_t Count = 0U;
        const float* Kept = WorldModelComponent_GetLodDistances(Placement, &Count);
        TEST_CHECK(run, (Count == 3U) && (Kept[0] == 5.0f) && (Kept[2] == 40.0f)
            && (WorldModelComponent_GetLodDistanceMultiplier(Placement) == 2.0f));

        // JSON through the table: the list as an array of numbers, read back the same.
        JSONObjectPool* Pool = NULL;
        JSONCompound* Compound = NULL;
        if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool))
            && TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound)))
        {
            const PropertyTable* Table = WorldModelComponent_GetPropertyTable();
            TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, Placement, Pool, Compound, NULL));
            JSONObjectValue List;
            if (TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"lod_distances", JSONValueType_Array, &List)))
            {
                JSONObjectValue Last;
                TEST_CHECK(run, (JSONArray_GetElementCount(List.Value.Array) == 3U)
                    && TEST_CHECK_SUCCESS(run, JSONArray_Get(List.Value.Array, 2U, &Last))
                    && (Last.Type == JSONValueType_RealNumber) && (Last.Value.RealNumber == 40.0));
            }
            WorldObject* Other = NULL;
            WorldComponent* OtherComponent = NULL;
            if (TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &Other))
                && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Other),
                    WorldComponentType_Model, &OtherComponent)))
            {
                TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Compound, OtherComponent, NULL));
                TEST_CHECK(run, Properties_Equals(Table, Placement, OtherComponent));
            }
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
        }
        if (Pool != NULL)
        {
            TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
        }
    }
    if (IsWorldBuilt)
    {
        TestWorld_Deconstruct(run, &TestWorld, &Keys);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: the model instances component.
/* Two texts equal byte for byte (both NULL counts as equal). */
static bool AreTextsEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    return strcmp((const char*)a, (const char*)b) == 0;
}

/* The instances component's rows are the model component's, row for row (key, wire id, type, default, rules, flags,
 * enum, texts; only the field differs), but for omit_pixelation and its flag (copies are never crisp); then the copies,
 * wire id 22. */
static void TheInstancesComponentSharesTheModelRows(TestRun* run)
{
    const PropertyTable* ModelTable = WorldModelComponent_GetPropertyTable();
    const PropertyTable* Instances = WorldModelInstancesComponent_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(Instances));
    TEST_CHECK(run, (Instances->DescriptorCount == (ModelTable->DescriptorCount - 2U + 1U))
        && (PropertyTable_FindByKey(Instances, u8"omit_pixelation") == NULL)
        && (PropertyTable_FindByKey(Instances, u8"overrides_omit_pixelation") == NULL));
    for (size_t Row = 0U; Row < Instances->DescriptorCount; Row++)
    {
        const PropertyDescriptor* Own = &Instances->Descriptors[Row];
        if (strcmp((const char*)Own->Key, "instances") == 0)
        {
            TEST_CHECK(run, (Own->WireId == 22U) && (Own->Type == PropertyType_InstanceList)
                && (Own->Rules.MaxCount == WORLD_MODEL_INSTANCES_MAX_COUNT) && (Own->Rules.Maximum == WORLD_POSITION_LIMIT)
                && (Row == (Instances->DescriptorCount - 1U)));
            continue;
        }
        const PropertyDescriptor* Shared = PropertyTable_FindByKey(ModelTable, Own->Key);
        if (!TEST_CHECK(run, Shared != NULL))
        {
            printf("      %s is not a model component row\n", (const char*)Own->Key);
            continue;
        }
        bool IsSame = (Shared->WireId == Own->WireId) && (Shared->Type == Own->Type)
            && (Memory_IsEqual(&Shared->Rules, &Own->Rules, sizeof(PropertyRules))) && (Shared->Flags == Own->Flags)
            && (Shared->EnumTable == Own->EnumTable) && (Shared->SinceVersion == Own->SinceVersion)
            && AreTextsEqual(Shared->DisplayName, Own->DisplayName) && AreTextsEqual(Shared->Description, Own->Description)
            && AreTextsEqual(Shared->Category, Own->Category)
            && (strcmp((const char*)Shared->MemberName, (const char*)Own->MemberName) == 0)
            && (Shared->Size == Own->Size);
        bool IsSameDefault = PropertyInternal_AreDataEqual(Own->Type, &Shared->Default, &Own->Default);
        if (!TEST_CHECK(run, IsSame && IsSameDefault))
        {
            printf("      %s differs from the model component's row\n", (const char*)Own->Key);
        }
    }
}

/* The settings and the LOD settings in effect are the model component's for the same rows, through every flag and a
 * spread of values; copies are never crisp, even where the world model is; the crisp flag cannot be set. */
static void TheInstancesComponentResolvesLikeAModel(TestRun* run)
{
    TestAssetFixture Fixture;
    BoundingBox Box = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    World TestWorld;
    KeyRegistry Keys;
    bool IsWorldBuilt = false;
    WorldModel* Loaded = NULL;
    WorldObject* Object = NULL;
    WorldComponent* ModelComponent = NULL;
    WorldComponent* InstancesComponent = NULL;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddModel(run, &Fixture, "bush_lod0", Box, NULL)
        && TestAssets_AddModel(run, &Fixture, "bush_lod1", Box, NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"bush\", \"lods\": [{\"model\": \"bush_lod0\", "
            "\"max_distance\": 12}, {\"model\": \"bush_lod1\", \"max_distance\": 45}], \"lod_hysteresis\": 0.15, "
            "\"render\": {\"omit_pixelation\": true, \"shadow_distance\": 70, \"has_shadow_lod\": true}}")
        && ((Loaded = LoadWorldModel(run, &Fixture, "bush", &User)) != NULL)
        && (IsWorldBuilt = TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
        && TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object), WorldComponentType_Model,
            &ModelComponent))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object),
            WorldComponentType_ModelInstances, &InstancesComponent)))
    {
        WorldModelComponent* Placement = WorldModelComponent_FromComponent(ModelComponent);
        WorldModelInstancesComponent* Instances = WorldModelInstancesComponent_FromComponent(InstancesComponent);
        TEST_CHECK(run, (Instances != NULL) && (WorldModelInstancesComponent_FromComponent(ModelComponent) == NULL)
            && (WorldModelInstancesComponent_AsComponent(Instances) == InstancesComponent));
        const WorldModelSettings* Asset = WorldModel_GetSettings(Loaded);
        uint32_t Seed = 12345U;
        for (int Round = 0; Round < 64; Round++)
        {
            // The same values through both components' setters (the instances' as the table writes them).
            Seed = (Seed * 1664525U) + 1013904223U;
            bool Bits[8];
            for (int Bit = 0; Bit < 8; Bit++)
            {
                Bits[Bit] = ((Seed >> (Bit + 8)) & 1U) != 0U;
            }
            float Distance = 5.0f + (float)(Seed % 97U);
            float Multiplier = 0.5f + ((float)((Seed >> 4) % 7U) * 0.25f);
            float ShadowDistance = 2.0f + (float)((Seed >> 20) % 31U);
            uint32_t ShadowLod = (Seed >> 24) % 4U;
            const float Own[] = { 3.0f + (float)(Round % 5), 30.0f + (float)Round };
            size_t OwnCount = Bits[6] ? 2U : 1U;
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasOutline(Placement, Bits[0]));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetHasOutline(Instances, Bits[0]));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistance(Placement, ShadowDistance));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetShadowDistance(Instances, ShadowDistance));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowDistanceMultiplier(Placement, Multiplier));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetShadowDistanceMultiplier(Instances, Multiplier));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsShadowDistanceFixed(Placement, Bits[4]));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetIsShadowDistanceFixed(Instances, Bits[4]));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsShadowOnly(Placement, Bits[5]));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetIsShadowOnly(Instances, Bits[5]));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHasTwoSidedShadows(Placement, Bits[6] != Bits[0]));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetHasTwoSidedShadows(Instances, Bits[6] != Bits[0]));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetShadowLod(Placement, ShadowLod));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetShadowLod(Instances, ShadowLod));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetCastsShadows(Placement, Bits[1]));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetCastsShadows(Instances, Bits[1]));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistance(Placement, Distance));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetDrawDistance(Instances, Distance));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetDrawDistanceMultiplier(Placement, Multiplier));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetDrawDistanceMultiplier(Instances, Multiplier));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsDrawDistanceFixed(Placement, Bits[2]));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetIsDrawDistanceFixed(Instances, Bits[2]));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistances(Placement, Own, OwnCount));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetLodDistances(Instances, Own, OwnCount));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetLodDistanceMultiplier(Placement, Multiplier));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetLodDistanceMultiplier(Instances, Multiplier));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetIsLodForced(Placement, Bits[3]));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetIsLodForced(Instances, Bits[3]));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetForcedLod(Placement, (uint32_t)(Seed % 4U)));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetForcedLod(Instances, (uint32_t)(Seed % 4U)));
            float Haze = (float)((Seed >> 12) % 9U) * 0.25f;
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetHaze(Placement, Haze));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetHaze(Instances, Haze));
            TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOccludesSky(Placement, Bits[7]));
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetOccludesSky(Instances, Bits[7]));
            for (int Setting = 0; Setting < (int)WorldModelSetting_Count; Setting++)
            {
                if (Setting == (int)WorldModelSetting_OmitPixelation)
                {
                    continue;
                }
                bool IsOn = ((Seed >> (Setting + 1)) & 3U) != 0U;
                TEST_CHECK_SUCCESS(run, WorldModelComponent_SetOverridden(Placement, (WorldModelSetting)Setting, IsOn));
                TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetOverridden(Instances, (WorldModelSetting)Setting,
                    IsOn));
                TEST_CHECK(run, WorldModelInstancesComponent_IsOverridden(Instances, (WorldModelSetting)Setting) == IsOn);
            }
            WorldModelSettings FromModel = WorldModelComponent_ResolveSettings(Placement, Asset);
            WorldModelSettings FromInstances = WorldModelInstancesComponent_ResolveSettings(Instances, Asset);
            TEST_CHECK(run, FromModel.OmitsPixelation && !FromInstances.OmitsPixelation);
            FromModel.OmitsPixelation = false;
            TEST_CHECK(run, AreSettingsEqual(&FromModel, &FromInstances));
            WorldModelLodSettings ModelLods;
            WorldModelLodSettings InstanceLods;
            bool IsModelAsAsked = WorldModelComponent_ResolveLodSettings(Placement, Loaded, &ModelLods);
            bool IsInstancesAsAsked = WorldModelInstancesComponent_ResolveLodSettings(Instances, Loaded, &InstanceLods);
            TEST_CHECK(run, (IsModelAsAsked == IsInstancesAsAsked)
                && Memory_IsEqual(&ModelLods, &InstanceLods, sizeof(ModelLods)));
        }
        // The crisp flag is the model component's alone.
        TEST_CHECK_ERROR(run, WorldModelInstancesComponent_SetOverridden(Instances, WorldModelSetting_OmitPixelation,
            true), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldModelInstancesComponent_SetOverridden(Instances, WorldModelSetting_Count, true),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, !WorldModelInstancesComponent_IsOverridden(Instances, WorldModelSetting_OmitPixelation));
        TEST_CHECK_ERROR(run, WorldModelInstancesComponent_SetOverridden(NULL, WorldModelSetting_HasOutline, true),
            ErrorCode_IllegalArgument);
    }
    if (IsWorldBuilt)
    {
        TestWorld_Deconstruct(run, &TestWorld, &Keys);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* The copies: set as a copy, refused with nothing changed (naming the copy that breaks a rule), and their own revision
 * advances only when they may have changed (their setter, their row through the table, a read, a copy), so the renderer
 * sorts them again only then. */
static void TheInstancesRevisionFollowsTheCopies(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldObject* OtherObject = NULL;
    WorldComponent* Component = NULL;
    WorldComponent* OtherComponent = NULL;
    JSONObjectPool* Pool = NULL;
    if (TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object),
            WorldComponentType_ModelInstances, &Component))
        && TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, NULL, &OtherObject))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(OtherObject),
            WorldComponentType_ModelInstances, &OtherComponent))
        && TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        WorldModelInstancesComponent* Instances = WorldModelInstancesComponent_FromComponent(Component);
        const PropertyTable* Table = WorldModelInstancesComponent_GetPropertyTable();
        size_t Count = 1U;
        TEST_CHECK(run, (WorldModelInstancesComponent_GetInstances(Instances, &Count) == NULL) && (Count == 0U)
            && (WorldModelInstancesComponent_GetWorldModel(Instances) == NULL));
        PropertyInstanceEntry Copies[2] = { PropertyInstanceEntry_CreateDefault(), PropertyInstanceEntry_CreateDefault() };
        Copies[1].Position = Vector3Double_Create(31.5, 0.0, -2.0);
        Copies[1].Tint = (Color){ 100, 150, 200, 255 };

        uint32_t Revision = WorldModelInstancesComponent_GetInstancesRevision(Instances);
        TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetInstances(Instances, Copies, 2U));
        TEST_CHECK(run, WorldModelInstancesComponent_GetInstancesRevision(Instances) != Revision);
        const PropertyInstanceEntry* Held = WorldModelInstancesComponent_GetInstances(Instances, &Count);
        TEST_CHECK(run, (Count == 2U) && (Held != Copies) && (Held[1].Position.X == 31.5) && (Held[1].Tint.b == 200U));

        // Other rows leave the copies' revision alone.
        Revision = WorldModelInstancesComponent_GetInstancesRevision(Instances);
        uint32_t ComponentRevision = WorldComponent_GetRevision(Component);
        TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetWorldModel(Instances, (const unsigned char*)u8"bush"));
        TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetDrawDistance(Instances, 40.0f));
        TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetOverridden(Instances, WorldModelSetting_DrawDistance,
            true));
        TEST_CHECK(run, (WorldModelInstancesComponent_GetInstancesRevision(Instances) == Revision)
            && (WorldComponent_GetRevision(Component) == (ComponentRevision + 3U)));

        // A refused set changes nothing, and names the copy.
        PropertyInstanceEntry Broken[3] = { PropertyInstanceEntry_CreateDefault(), PropertyInstanceEntry_CreateDefault(),
            PropertyInstanceEntry_CreateDefault() };
        Broken[2].Position.Y = WORLD_POSITION_LIMIT * 2.0;
        const char* const Fragments[] = { "model_instances_component.instances", "instance 2", NULL };
        TestText_CheckErrorMessage(run, WorldModelInstancesComponent_SetInstances(Instances, Broken, 3U),
            ErrorCode_ArgumentOutOfRange, Fragments);
        TEST_CHECK_ERROR(run, WorldModelInstancesComponent_SetInstances(Instances, NULL, 2U), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldModelInstancesComponent_SetInstances(NULL, Copies, 2U), ErrorCode_IllegalArgument);
        WorldModelInstancesComponent_GetInstances(Instances, &Count);
        TEST_CHECK(run, (Count == 2U) && (WorldModelInstancesComponent_GetInstancesRevision(Instances) == Revision));

        // The row through the table, a JSON read and a copy each advance it.
        PropertyInstanceList* One = NULL;
        if (TEST_CHECK_SUCCESS(run, PropertyInstanceList_Create(Copies, 1U, &One)))
        {
            PropertyValue Value = PropertyValue_FromInstanceList(One);
            TEST_CHECK_SUCCESS(run, Properties_Set(Table, Instances, u8"instances", &Value));
            TEST_CHECK(run, WorldModelInstancesComponent_GetInstancesRevision(Instances) != Revision);
            PropertyInstanceList_Destroy(One);
        }
        WorldModelInstancesComponent* Other = WorldModelInstancesComponent_FromComponent(OtherComponent);
        Revision = WorldModelInstancesComponent_GetInstancesRevision(Other);
        TEST_CHECK_SUCCESS(run, Properties_CopyTo(Table, Instances, Other));
        TEST_CHECK(run, (WorldModelInstancesComponent_GetInstancesRevision(Other) != Revision)
            && Properties_Equals(Table, Instances, Other));
        JSONCompound* Compound = NULL;
        if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound)))
        {
            TEST_CHECK_SUCCESS(run, WorldModelInstancesComponent_SetInstances(Instances, Copies, 2U));
            TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, Instances, Pool, Compound, NULL));
            Revision = WorldModelInstancesComponent_GetInstancesRevision(Other);
            TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Compound, Other, NULL));
            TEST_CHECK(run, (WorldModelInstancesComponent_GetInstancesRevision(Other) != Revision)
                && Properties_Equals(Table, Instances, Other));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
        }
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}


// Tests: the shipped content.
/* The game's world models (compile/out/asset/world_models) are all read without a skipped file. */
/* The skinned figure of the socket tests: a root, a spine 1 m up, a head 2 m up, a hand 1 m to the side of the spine. */
static const TestSkeletonBone SocketFigureBones[4] =
{
    { .Name = "root", .Parent = -1, .Bind = { .translation = { 0.0f, 0.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "spine", .Parent = 0, .Bind = { .translation = { 0.0f, 1.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "head", .Parent = 1, .Bind = { .translation = { 0.0f, 2.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
    { .Name = "hand", .Parent = 1, .Bind = { .translation = { 1.0f, 1.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f } } },
};

/* A world model names its animation graph (a dependency), its sockets on bones and fixed ones, and what a
 * first-person camera rides and hides; bone names resolve against its skeleton when it loads. */
static void SocketsTheGraphAndTheFirstPersonBlockLoadWithTheSkeleton(TestRun* run)
{
    TestAssetFixture Fixture;
    BoundingBox Box = { .min = { -1.0f, -1.0f, -1.0f }, .max = { 1.0f, 1.0f, 1.0f } };
    // Raised 0.5 m by its import transform: bone sockets go through it, fixed ones do not.
    Matrix Raised = MatrixTranslate(0.0f, 0.5f, 0.0f);
    bool IsBuilt = TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddSkinnedModel(run, &Fixture, "figure_mesh", SocketFigureBones, 4, 0.2f, Raised, NULL)
        && TestAssets_AddSkinnedModel(run, &Fixture, "half_mesh", SocketFigureBones, 4, 0.2f,
            MatrixMultiply(MatrixScale(0.5f, 0.5f, 0.5f), MatrixRotateY(0.5f * PI)), NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"half_figure\", \"lods\": [{\"model\": \"half_mesh\", "
            "\"max_distance\": 50}], \"sockets\": {\"hand_r\": {\"bone\": \"hand\", \"position\": [0, -0.1, 0.05]}}}")
        && TestAssets_AddModel(run, &Fixture, "box", Box, NULL)
        && TestAssets_AddAnimationGraph(run, &Fixture, "{\"name\": \"figure_graph\", \"layers\": [{\"name\": \"body\", "
            "\"entry\": \"idle\", \"states\": {\"idle\": {\"clip\": \"idle\"}}}]}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"figure\", \"lods\": [{\"model\": \"figure_mesh\", "
            "\"max_distance\": 50}], \"animation_graph\": \"figure_graph\", \"sockets\": {\"hand_r\": {\"bone\": "
            "\"hand\", \"position\": [0, -0.1, 0.05]}, \"eyes\": {\"bone\": \"head\", \"position\": [0, 0.1, 0.12]}, "
            "\"seat\": {\"position\": [0, 0, 0.5], \"rotation\": [0, 180, 0]}}, \"first_person\": {\"eyes\": \"eyes\", "
            "\"hidden_bones\": [\"head\"]}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"bad_bone\", \"lods\": [{\"model\": \"figure_mesh\", "
            "\"max_distance\": 50}], \"sockets\": {\"tail\": {\"bone\": \"tail\"}}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"rigid_bone\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 50}], \"sockets\": {\"grip\": {\"bone\": \"hand\"}}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"rigid_fixed\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 50}], \"sockets\": {\"grip\": {\"position\": [0.1, 0.2, 0.3]}}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"bad_hidden\", \"lods\": [{\"model\": \"figure_mesh\", "
            "\"max_distance\": 50}], \"sockets\": {\"eyes\": {\"bone\": \"head\"}}, \"first_person\": {\"eyes\": "
            "\"eyes\", \"hidden_bones\": [\"skull\"]}}")
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"lost_graph\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 50}], \"animation_graph\": \"nowhere\"}");
    AssetUserID User = ASSET_USER_ID_INVALID;
    WorldModel* Figure = IsBuilt ? LoadWorldModel(run, &Fixture, "figure", &User) : NULL;
    if (Figure != NULL)
    {
        TEST_CHECK(run, (WorldModel_GetAnimationGraph(Figure) != NULL)
            && (strcmp((const char*)WorldModel_GetAnimationGraphName(Figure), "tbp:figure_graph") == 0));
        TEST_CHECK(run, WorldModel_GetSocketCount(Figure) == 3U);
        uint32_t Hand = 0U;
        uint32_t Seat = 0U;
        TEST_CHECK(run, WorldModel_TryFindSocket(Figure, (const unsigned char*)u8"hand_r", &Hand)
            && WorldModel_TryFindSocket(Figure, (const unsigned char*)u8"seat", &Seat)
            && !WorldModel_TryFindSocket(Figure, (const unsigned char*)u8"tail", NULL)
            && !WorldModel_TryFindSocket(Figure, NULL, NULL));
        const WorldModelSocket* HandSocket = WorldModel_GetSocket(Figure, Hand);
        TEST_CHECK(run, HandSocket->_hasBone && (HandSocket->_bone == 3U) && !WorldModel_GetSocket(Figure, Seat)->_hasBone);
        // In the bind pose: the hand's socket 10 cm below the hand and 5 cm in front, raised by the import transform.
        Matrix HandMatrix = WorldModel_GetSocketMatrix(Figure, Hand, NULL);
        TEST_CHECK(run, IsNear(HandMatrix.m12, 1.0f) && IsNear(HandMatrix.m13, 1.4f) && IsNear(HandMatrix.m14, 0.05f));
        // In a pose that lifts the hand 1 m (its bone matrix), the socket follows.
        Matrix Pose[4];
        for (int Index = 0; Index < 4; Index++)
        {
            Pose[Index] = MatrixTranslate(SocketFigureBones[Index].Bind.translation.x,
                SocketFigureBones[Index].Bind.translation.y, SocketFigureBones[Index].Bind.translation.z);
        }
        Pose[3] = MatrixTranslate(1.0f, 2.0f, 0.0f);
        HandMatrix = WorldModel_GetSocketMatrix(Figure, Hand, Pose);
        TEST_CHECK(run, IsNear(HandMatrix.m13, 2.4f));
        // The seat is fixed in the object's space, turned half round (its +Z faces -Z), the import transform aside.
        Matrix SeatMatrix = WorldModel_GetSocketMatrix(Figure, Seat, Pose);
        TEST_CHECK(run, IsNear(SeatMatrix.m12, 0.0f) && IsNear(SeatMatrix.m13, 0.0f) && IsNear(SeatMatrix.m14, 0.5f)
            && IsNear(SeatMatrix.m10, -1.0f));
        uint32_t Eyes = 0U;
        const uint32_t* Hidden = NULL;
        uint32_t HiddenCount = 0U;
        TEST_CHECK(run, WorldModel_GetFirstPerson(Figure, &Eyes, &Hidden, &HiddenCount)
            && (strcmp((const char*)WorldModel_GetSocket(Figure, Eyes)->_name, "eyes") == 0) && (HiddenCount == 1U)
            && (Hidden != NULL) && (Hidden[0] == 2U));
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    if (IsBuilt)
    {
        // An import at half scale, turned a quarter round Y (a model authored in other units and axes): the socket's
        // frame is rigid (unit axes, the turn kept) and its offset is in metres along the bone's turned axes, on the
        // bone's joint where the import puts it.
        AssetUserID Scaled = ASSET_USER_ID_INVALID;
        WorldModel* Half = LoadWorldModel(run, &Fixture, "half_figure", &Scaled);
        uint32_t Hand = 0U;
        if ((Half != NULL) && TEST_CHECK(run, WorldModel_TryFindSocket(Half, (const unsigned char*)u8"hand_r", &Hand)))
        {
            Matrix HandMatrix = WorldModel_GetSocketMatrix(Half, Hand, NULL);
            // The hand's joint (1, 1, 0) halved and turned: (0, 0.5, -0.5); the offset (0, -0.1, 0.05) turned: (0.05, -0.1,
            // 0).
            TEST_CHECK(run, IsNear(HandMatrix.m12, 0.05f) && IsNear(HandMatrix.m13, 0.4f) && IsNear(HandMatrix.m14, -0.5f));
            Vector3 AxisX = { HandMatrix.m0, HandMatrix.m1, HandMatrix.m2 };
            Vector3 AxisY = { HandMatrix.m4, HandMatrix.m5, HandMatrix.m6 };
            TEST_CHECK(run, IsNear(Vector3Length(AxisX), 1.0f) && IsNear(Vector3Length(AxisY), 1.0f)
                && IsNear(AxisX.z, -1.0f) && IsNear(AxisY.y, 1.0f));
        }
        if (Scaled != ASSET_USER_ID_INVALID)
        {
            TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, Scaled));
        }
        // A fixed socket needs no skeleton.
        AssetUserID Other = ASSET_USER_ID_INVALID;
        WorldModel* Rigid = LoadWorldModel(run, &Fixture, "rigid_fixed", &Other);
        TEST_CHECK(run, (Rigid != NULL) && (WorldModel_GetSocketCount(Rigid) == 1U)
            && !WorldModel_GetFirstPerson(Rigid, NULL, NULL, NULL) && (WorldModel_GetAnimationGraph(Rigid) == NULL));
        if (Other != ASSET_USER_ID_INVALID)
        {
            TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, Other));
        }
        // Bones the skeleton lacks, a bone socket without a skeleton and a graph that does not load fail the load.
        static const struct { const char* Name; const char* Fragment; } Failing[] =
        {
            { "bad_bone", "which its skeleton lacks" },
            { "rigid_bone", "none of its LOD models has a skeleton" },
            { "bad_hidden", "hidden_bones names \"skull\", which its skeleton lacks" },
            { "lost_graph", "animation graph \"tbp:nowhere\"" },
        };
        for (size_t Index = 0U; Index < (sizeof(Failing) / sizeof(Failing[0])); Index++)
        {
            AssetUserID Failed = ASSET_USER_ID_INVALID;
            WorldModel* Loaded = NULL;
            const char* Fragments[] = { Failing[Index].Fragment, NULL };
            if (TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &Failed)))
            {
                Error Result = AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)Failing[Index].Name,
                    Failed, &Loaded);
                bool IsFailed = Result.Code != ErrorCode_Success;
                TEST_CHECK(run, IsFailed);
                if (IsFailed)
                {
                    TestText_CheckErrorMessage(run, Result, Result.Code, Fragments);
                }
                else
                {
                    Error_Deconstruct(&Result);
                }
                TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, Failed));
            }
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void SocketAndFirstPersonRulesAreRefused(TestRun* run)
{
    static const RefusedDefinition Cases[] =
    {
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": []}",
            ErrorCode_InvalidAssetDefinition, "\"sockets\" must be a JSON object" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": {\"Hand\": {}}}",
            ErrorCode_InvalidAssetDefinition, "not a valid socket name" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": {\"hand\": 3}}",
            ErrorCode_InvalidAssetDefinition, "sockets.hand must be a JSON object" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": {\"hand\": "
            "{\"offset\": [0, 0, 0]}}}", ErrorCode_InvalidAssetDefinition, "Unknown key \"offset\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": {\"hand\": "
            "{\"bone\": \"\"}}}", ErrorCode_InvalidAssetDefinition, "sockets.hand.bone must be a bone's name" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": {\"hand\": "
            "{\"bone\": \"a_bone_name_longer_than_31_bytes\"}}}", ErrorCode_InvalidAssetDefinition,
            "sockets.hand.bone must be a bone's name" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": {\"hand\": "
            "{\"position\": [0, 0]}}}", ErrorCode_InvalidJSON, "sockets.hand.position" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"first_person\": {}}",
            ErrorCode_InvalidAssetDefinition, "first_person needs \"eyes\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"first_person\": "
            "{\"eyes\": \"eyes\"}}", ErrorCode_InvalidAssetDefinition, "which is not one of its sockets" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": {\"eyes\": {}}, "
            "\"first_person\": {\"eyes\": \"eyes\", \"hidden\": []}}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"hidden\"" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"sockets\": {\"eyes\": {}}, "
            "\"first_person\": {\"eyes\": \"eyes\", \"hidden_bones\": \"head\"}}", ErrorCode_InvalidAssetDefinition,
            "hidden_bones must be an array" },
        { "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], \"animation_graph\": "
            "\"Graph\"}", ErrorCode_InvalidAssetDefinition, "animation_graph \"Graph\" is not a valid asset name" },
    };
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            const char* Fragments[] = { Cases[Index].Fragment, NULL };
            if (!TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, Cases[Index].Json), Cases[Index].Code,
                Fragments))
            {
                printf("    (case %u)\n", (unsigned)Index);
            }
        }
        // 17 hidden bones are one too many.
        char Json[WORLD_MODEL_TEST_JSON_CAPACITY];
        size_t Length = (size_t)snprintf(Json, sizeof(Json), "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"sockets\": {\"eyes\": {}}, \"first_person\": {\"eyes\": \"eyes\", "
            "\"hidden_bones\": [");
        for (size_t Index = 0U; Index <= WORLD_MODEL_MAX_HIDDEN_BONES; Index++)
        {
            Length += (size_t)snprintf(Json + Length, sizeof(Json) - Length, "%s\"b%u\"", (Index > 0U) ? ", " : "",
                (unsigned)Index);
        }
        snprintf(Json + Length, sizeof(Json) - Length, "]}}");
        const char* TooMany[] = { "at most 16", NULL };
        TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, Json), ErrorCode_InvalidAssetDefinition, TooMany);
        // 33 sockets are one too many.
        Length = (size_t)snprintf(Json, sizeof(Json), "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"sockets\": {");
        for (size_t Index = 0U; Index <= WORLD_MODEL_MAX_SOCKETS; Index++)
        {
            Length += (size_t)snprintf(Json + Length, sizeof(Json) - Length, "%s\"s%u\": {}", (Index > 0U) ? ", " : "",
                (unsigned)Index);
        }
        snprintf(Json + Length, sizeof(Json) - Length, "}}");
        const char* TooManySockets[] = { "more than 32 sockets", NULL };
        TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, Json), ErrorCode_InvalidAssetDefinition,
            TooManySockets);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TheShippedWorldModelsAreRead(TestRun* run)
{
    static const char* const Names[] =
    {
        "test", "pbr_demo", "pbr_metal", "painted_cube", "lod_test", "test_door", "test_humanoid", "test_humanoid_short",
        "trash_can_lined"
    };
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
        { "references_are_asset_names", ReferencesAreAssetNames },
        { "a_missing_dependency_fails_the_load_and_holds_nothing", AMissingDependencyFailsTheLoadAndHoldsNothing },
        { "an_unused_variant_slot_is_logged_once", AnUnusedVariantSlotIsLoggedOnce },
        { "hand_written_shadow_keys_turn_their_flags_on", HandWrittenShadowKeysTurnTheirFlagsOn },
        { "an_older_definitions_shadow_tier_is_read_as_the_shadow_distance",
            AnOlderDefinitionsShadowTierIsReadAsTheShadowDistance },
        { "the_model_type_shares_the_factor_rules", TheModelTypeSharesTheFactorRules },
        { "variants_are_found_by_name_and_change_only_their_slots", VariantsAreFoundByNameAndChangeOnlyTheirSlots },
        { "every_override_is_a_flag_next_to_its_value", EveryOverrideIsAFlagNextToItsValue },
        { "the_flags_choose_between_the_world_model_and_the_placement",
            TheFlagsChooseBetweenTheWorldModelAndThePlacement },
        { "the_component_starts_at_its_defaults", TheComponentStartsAtItsDefaults },
        { "the_shadow_distance_comes_from_the_placement_the_world_model_or_the_draw_distance",
            TheShadowDistanceComesFromThePlacementTheWorldModelOrTheDrawDistance },
        { "the_placement_picks_its_lod_settings", ThePlacementPicksItsLodSettings },
        { "the_instances_component_shares_the_model_rows", TheInstancesComponentSharesTheModelRows },
        { "the_instances_component_resolves_like_a_model", TheInstancesComponentResolvesLikeAModel },
        { "the_instances_revision_follows_the_copies", TheInstancesRevisionFollowsTheCopies },
        { "sockets_the_graph_and_the_first_person_block_load_with_the_skeleton",
            SocketsTheGraphAndTheFirstPersonBlockLoadWithTheSkeleton },
        { "socket_and_first_person_rules_are_refused", SocketAndFirstPersonRulesAreRefused },
        { "the_shipped_world_models_are_read", TheShippedWorldModelsAreRead },
    };
    TestRun_RunSuite(run, "WorldModel", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
