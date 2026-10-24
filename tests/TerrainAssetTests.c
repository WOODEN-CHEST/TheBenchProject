#include "TestSuites.h"
#include "TestAssets.h"
#include "TestTerrain.h"
#include "TestText.h"
#include "TestWorld.h"
#include "CollisionLayer.h"
#include "TerrainAsset.h"
#include "World.h"
#include "WorldFile.h"
#include "WorldObject.h"
#include "WorldTerrainComponent.h"
#include "wr/WRFileSystem.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Room for a path. */
#define TERRAIN_ASSET_TEST_PATH_CAPACITY 256
/** The glTF component types the files use. */
#define GLTF_FLOAT 5126U
#define GLTF_UNSIGNED_BYTE 5121U


// Types.
/* A refused definition: its JSON, the error code and a fragment its message must hold. */
typedef struct RefusedDefinitionStruct
{
    const char* Json;
    ErrorCode Code;
    const char* Fragment;
} RefusedDefinition;

/* An asset manager over a work directory (a search root), with the standard types and their definitions read. */
typedef struct TerrainPackStruct
{
    GenericBuffer Root;
    AssetManager* Manager;
    StandardAssetTypes Types;
    JSONObjectPool* Pool;
    AssetUserID User;
} TerrainPack;


// Static functions: helpers.
static float Slope(float x, float z)
{
    return (0.1f * x) - (0.05f * z);
}

static bool AreNamesEqual(const unsigned char* a, const char* b)
{
    return (a != NULL) && (strcmp((const char*)a, b) == 0);
}

/* Field by field (the structs have padding). */
static bool AreSettingsEqual(const TerrainSettings* a, const TerrainSettings* b)
{
    return (a->Render.HasOutline == b->Render.HasOutline) && (a->Render.ShadowTier == b->Render.ShadowTier)
        && (a->Render.CastsShadows == b->Render.CastsShadows) && (a->Render.DrawDistance == b->Render.DrawDistance)
        && (a->Render.IsDrawDistanceFixed == b->Render.IsDrawDistanceFixed)
        && (a->Collision.IsEnabled == b->Collision.IsEnabled) && (a->Collision.Layer == b->Collision.Layer)
        && (a->Collision.HasCollidesWith == b->Collision.HasCollidesWith)
        && (!a->Collision.HasCollidesWith || (a->Collision.CollidesWith == b->Collision.CollidesWith));
}

/* Builds a terrain definition from @p json without registering it and returns the constructor's result (the
 * definition, if one was built, is destroyed). */
static Error BuildDefinition(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.Terrain, &Bytes,
        (const unsigned char*)u8"test", &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}

/* Writes @p bytes to "<root>/<relativePath>" (its directory must exist). */
static bool WriteBytes(TestRun* run, const GenericBuffer* root, const char* relativePath, const GenericBuffer* bytes)
{
    char Path[TERRAIN_ASSET_TEST_PATH_CAPACITY];
    snprintf(Path, sizeof(Path), "%s/%s", (const char*)root->_data, relativePath);
    return TEST_CHECK_SUCCESS(run, FileSystem_WriteAllBytes((const unsigned char*)Path, bytes->_data, bytes->_count));
}

/* Creates the work directory @p name and writes the definitions (relative path, then JSON, NULL-terminated). */
static bool CreatePackFiles(TestRun* run, TerrainPack* pack, const char* name, const char* const* definitions)
{
    Memory_Zero(pack, sizeof(*pack));
    GenericBuffer_AllocateVariable(&pack->Root, TERRAIN_ASSET_TEST_PATH_CAPACITY, sizeof(unsigned char));
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, name, &pack->Root)))
    {
        return false;
    }
    for (size_t Index = 0U; definitions[Index] != NULL; Index += 2U)
    {
        if (!TEST_CHECK_SUCCESS(run, Test_WriteTextFile(pack->Root._data, definitions[Index], definitions[Index + 1U])))
        {
            return false;
        }
    }
    return true;
}

/* Makes the pack's manager over its root once its files are written, and a user to load with. */
static bool OpenPack(TestRun* run, TerrainPack* pack)
{
    size_t FailedCount = 1U;
    return TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &pack->Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(pack->Manager, &pack->Types, &pack->Pool))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(pack->Manager, pack->Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(pack->Manager, &FailedCount))
        && TEST_CHECK(run, FailedCount == 0U)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(pack->Manager, &pack->User));
}

static void ClosePack(TestRun* run, TerrainPack* pack)
{
    if (pack->Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(pack->Manager));
    }
    if (pack->Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(pack->Pool));
    }
    Memory_Free(pack->Root._data);
}

static TerrainAsset* LoadTerrain(TestRun* run, TerrainPack* pack, const char* name)
{
    TerrainAsset* Loaded = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_LoadTerrain(pack->Manager, (const unsigned char*)name, pack->User,
        &Loaded)))
    {
        return NULL;
    }
    return Loaded;
}

/* A .glb of a grid: one primitive per slot (the triangles shared out by @p counts), and the attributes given. */
static void WriteGridGlb(const TestTerrainGrid* grid, const char* const* slots, const uint32_t* counts,
    size_t slotCount, const TestGltfAttribute* attributes, size_t attributeCount, GenericBuffer* outBytes)
{
    TestGltfMesh File =
    {
        .Positions = grid->Positions, .Normals = grid->Normals, .HasNormals = true, .VertexCount = grid->VertexCount,
        .Attributes = attributes, .AttributeCount = attributeCount, .Indices = grid->Indices,
        .TriangleCount = grid->TriangleCount, .MaterialNames = slots, .PrimitiveTriangleCounts = counts,
        .PrimitiveModes = NULL, .PrimitiveCount = slotCount, .NodeTranslation = { 0.0f, 0.0f, 0.0f }, .NodeScale = 1.0f
    };
    TestTerrain_WriteGlb(&File, outBytes);
}

/* Adds an object with a terrain component to @p world. */
static WorldTerrainComponent* AddTerrainObject(TestRun* run, World* world)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, NULL, &Object))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_Terrain,
            &Component)))
    {
        return NULL;
    }
    return WorldTerrainComponent_FromComponent(Component);
}


// Tests: definitions.
/* A definition of a .glb with one palette material gets the documented defaults, and the file loads as its mesh. */
static void AMinimalDefinitionGetsTheDefaults(TestRun* run)
{
    static const char* const Files[] =
    {
        "terrains/town/town.json", "{\"name\": \"town\", \"source\": \"model\", \"model\": \"town/town\", "
            "\"materials\": [\"grass\"]}",
        NULL
    };
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 4U, 4U, 2.0f, -4.0f, -4.0f, Slope);
    const char* Slots[1] = { "blend" };
    uint32_t Counts[1] = { Grid.TriangleCount };
    GenericBuffer Glb;
    GenericBuffer_AllocateVariable(&Glb, 0U, sizeof(unsigned char));
    WriteGridGlb(&Grid, Slots, Counts, 1U, NULL, 0U, &Glb);
    TerrainPack Pack;
    TerrainAsset* Loaded = (CreatePackFiles(run, &Pack, "minimal", Files)
        && WriteBytes(run, &Pack.Root, "terrains/town/town.glb", &Glb) && OpenPack(run, &Pack))
        ? LoadTerrain(run, &Pack, "town") : NULL;
    if (Loaded != NULL)
    {
        const TerrainMesh* Terrain = TerrainAsset_GetMesh(Loaded);
        TEST_CHECK(run, (TerrainMesh_GetVertexCount(Terrain) == Grid.VertexCount)
            && (TerrainMesh_GetTriangleCount(Terrain) == Grid.TriangleCount));
        TEST_CHECK(run, (TerrainMesh_GetChunkSize(Terrain) == TERRAIN_DEFAULT_CHUNK_SIZE)
            && (TerrainMesh_GetNormalMode(Terrain) == TerrainNormalMode_Smooth));
        TEST_CHECK(run, (TerrainAsset_GetMaterialCount(Loaded) == 1U) && (TerrainMesh_GetMaterialCount(Terrain) == 1U)
            && AreNamesEqual(TerrainAsset_GetMaterialName(Loaded, 0U), "grass")
            && (TerrainAsset_GetMaterialName(Loaded, 1U) == NULL));
        TEST_CHECK(run, (TerrainAsset_GetDensityChannelCount(Loaded) == 0U)
            && (TerrainAsset_GetDensityChannelName(Loaded, 0U) == NULL));
        // Nothing maps the weights: every one is 0, which the shaders show as the first material.
        TEST_CHECK(run, TerrainMesh_GetWeights(Terrain, 0U)[0] == 0U);
        const TerrainSettings* Settings = TerrainAsset_GetSettings(Loaded);
        TerrainSettings Defaults = TerrainSettings_CreateDefault();
        TEST_CHECK(run, AreSettingsEqual(Settings, &Defaults));
        TEST_CHECK(run, Settings->Render.HasOutline && (Settings->Render.ShadowTier == WorldShadowTier_Both)
            && Settings->Render.CastsShadows && (Settings->Render.DrawDistance == TERRAIN_DEFAULT_DRAW_DISTANCE)
            && Settings->Render.IsDrawDistanceFixed);
        TEST_CHECK(run, Settings->Collision.IsEnabled && (Settings->Collision.Layer == CollisionLayer_World)
            && !Settings->Collision.HasCollidesWith);
    }
    ClosePack(run, &Pack);
    Memory_Free(Glb._data);
    TestTerrain_FreeGrid(&Grid);
}

/* Every key at once: the mappings feed the weights and densities from their attributes, the blend slot is renamed,
 * the file's normals are kept, faces take a palette material or become holes, and the blocks set every default. */
static void AFullDefinitionReadsEveryKey(TestRun* run)
{
    static const char* const Files[] =
    {
        "terrains/valley.json",
        "{\n"
        "  \"name\": \"valley\",\n"
        "  \"source\": \"model\",\n"
        "  \"model\": \"valley\",\n"
        "  \"format\": \"glb\",\n"
        "  \"materials\": [\"grass\", \"dirt\", \"rock\"],\n"
        "  \"density_channels\": [\"trees\", \"flowers\"],\n"
        "  \"weights\": [{\"attribute\": \"COLOR_0\", \"r\": \"dirt\", \"g\": \"grass\"}],\n"
        "  \"densities\": [{\"attribute\": \"_DENSITY\", \"b\": \"trees\", \"r\": \"flowers\"}],\n"
        "  \"normals\": \"imported\",\n"
        "  \"blend_slot\": \"paint\",\n"
        "  \"chunk_size\": 16,\n"
        "  \"render\": {\"has_outline\": false, \"shadow_tier\": \"near\", \"casts_shadows\": false, "
            "\"draw_distance\": 900, \"is_draw_distance_fixed\": false},\n"
        "  \"collision\": {\"is_enabled\": false, \"layer\": \"prop\", \"collides_with\": [\"world\", \"character\"]}\n"
        "}\n",
        NULL
    };
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 6U, 4U, 3.0f, 0.0f, 0.0f, Slope);
    float* Colours = Memory_Allocate(Grid.VertexCount * 4U * sizeof(float));
    float* Densities = Memory_Allocate(Grid.VertexCount * 3U * sizeof(float));
    for (uint32_t Vertex = 0U; Vertex < Grid.VertexCount; Vertex++)
    {
        Colours[(Vertex * 4U) + 0U] = 1.0f;
        Colours[(Vertex * 4U) + 1U] = 0.2f;
        Colours[(Vertex * 4U) + 2U] = 0.6f; // mapped to nothing
        Colours[(Vertex * 4U) + 3U] = 1.0f;
        Densities[(Vertex * 3U) + 0U] = 0.4f;
        Densities[(Vertex * 3U) + 1U] = 1.0f; // mapped to nothing
        Densities[(Vertex * 3U) + 2U] = 0.8f;
    }
    TestGltfAttribute Attributes[2] =
    {
        { .Name = "COLOR_0", .ChannelCount = 4U, .ComponentType = GLTF_UNSIGNED_BYTE, .Values = Colours },
        { .Name = "_DENSITY", .ChannelCount = 3U, .ComponentType = GLTF_FLOAT, .Values = Densities },
    };
    // 48 triangles: 40 painted, 4 of rock, 4 holes.
    const char* Slots[3] = { "paint", "rock", "hole" };
    uint32_t Counts[3] = { 40U, 4U, 4U };
    GenericBuffer Glb;
    GenericBuffer_AllocateVariable(&Glb, 0U, sizeof(unsigned char));
    WriteGridGlb(&Grid, Slots, Counts, 3U, Attributes, 2U, &Glb);
    TerrainPack Pack;
    TerrainAsset* Loaded = (CreatePackFiles(run, &Pack, "full", Files)
        && WriteBytes(run, &Pack.Root, "terrains/valley.glb", &Glb) && OpenPack(run, &Pack))
        ? LoadTerrain(run, &Pack, "valley") : NULL;
    if (Loaded != NULL)
    {
        const TerrainMesh* Terrain = TerrainAsset_GetMesh(Loaded);
        TEST_CHECK(run, (TerrainMesh_GetChunkSize(Terrain) == 16.0f)
            && (TerrainMesh_GetNormalMode(Terrain) == TerrainNormalMode_Imported));
        TEST_CHECK(run, (TerrainAsset_GetMaterialCount(Loaded) == 3U)
            && AreNamesEqual(TerrainAsset_GetMaterialName(Loaded, 2U), "rock")
            && (TerrainAsset_GetDensityChannelCount(Loaded) == 2U)
            && AreNamesEqual(TerrainAsset_GetDensityChannelName(Loaded, 0U), "trees")
            && AreNamesEqual(TerrainAsset_GetDensityChannelName(Loaded, 1U), "flowers"));
        // Each channel feeds what its mapping names; the channels mapped to nothing are dropped.
        const uint8_t* Weights = TerrainMesh_GetWeights(Terrain, 7U);
        const uint8_t* VertexDensities = TerrainMesh_GetDensities(Terrain, 7U);
        TEST_CHECK(run, (Weights[0] == 51U) && (Weights[1] == 255U) && (Weights[2] == 0U));
        TEST_CHECK(run, (VertexDensities[0] == 204U) && (VertexDensities[1] == 102U));
        // The painted slot, the palette's slot and the holes, in the file's order.
        TEST_CHECK(run, TerrainMesh_GetTriangle(Terrain, 0U)->_flags == 0U);
        TEST_CHECK(run, (TerrainMesh_GetTriangle(Terrain, 40U)->_flags == TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL)
            && (TerrainMesh_GetTriangle(Terrain, 40U)->_material == 2U));
        TEST_CHECK(run, TerrainMesh_GetTriangle(Terrain, 47U)->_flags == TERRAIN_TRIANGLE_FLAG_HOLE);
        const TerrainSettings* Settings = TerrainAsset_GetSettings(Loaded);
        TEST_CHECK(run, !Settings->Render.HasOutline && (Settings->Render.ShadowTier == WorldShadowTier_Near)
            && !Settings->Render.CastsShadows && (Settings->Render.DrawDistance == 900.0f)
            && !Settings->Render.IsDrawDistanceFixed);
        TEST_CHECK(run, !Settings->Collision.IsEnabled && (Settings->Collision.Layer == CollisionLayer_Prop)
            && Settings->Collision.HasCollidesWith
            && (Settings->Collision.CollidesWith
                == (COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Character))));
    }
    ClosePack(run, &Pack);
    Memory_Free(Glb._data);
    Memory_Free(Densities);
    Memory_Free(Colours);
    TestTerrain_FreeGrid(&Grid);
}

/* Every rule of the definition refuses what it should, with a message that says where and why. */
static void EachRuleIsRefusedWithAClearMessage(TestRun* run)
{
#define MODEL_HEAD "{\"name\": \"t\", \"source\": \"model\", \"model\": \"t\", "
#define DATA_HEAD "{\"name\": \"t\", \"source\": \"data\", \"data\": \"t\", "
    static const RefusedDefinition Cases[] =
    {
        // The source and its location.
        { "{\"name\": \"t\", \"model\": \"t\", \"materials\": [\"g\"]}", ErrorCode_InvalidAssetDefinition,
            "Terrain \"t\": \"source\" must be \"model\" (a .glb imported when it loads) or \"data\"" },
        { "{\"name\": \"t\", \"source\": \"mesh\", \"model\": \"t\", \"materials\": [\"g\"]}",
            ErrorCode_InvalidAssetDefinition, "\"source\" must be \"model\"" },
        { "{\"name\": \"t\", \"source\": 3, \"model\": \"t\", \"materials\": [\"g\"]}",
            ErrorCode_InvalidAssetDefinition, "\"source\" must be \"model\"" },
        { "{\"name\": \"t\", \"source\": \"model\", \"materials\": [\"g\"]}", ErrorCode_InvalidAssetDefinition,
            "Missing required location \"model\"" },
        { "{\"name\": \"t\", \"source\": \"data\", \"model\": \"t\", \"materials\": [\"g\"]}",
            ErrorCode_InvalidAssetDefinition, "\"model\" is read only with \"source\": \"model\"" },
        { DATA_HEAD "\"materials\": [\"g\"], \"normals\": \"smooth\"}", ErrorCode_InvalidAssetDefinition,
            "\"normals\" is read only with \"source\": \"model\"" },
        { DATA_HEAD "\"materials\": [\"g\"], \"blend_slot\": \"paint\"}", ErrorCode_InvalidAssetDefinition,
            "\"blend_slot\" is read only with \"source\": \"model\"" },
        { MODEL_HEAD "\"data\": \"t\", \"materials\": [\"g\"]}", ErrorCode_InvalidAssetDefinition,
            "\"data\" is read only with \"source\": \"data\"" },
        { MODEL_HEAD "\"format\": \"obj\", \"materials\": [\"g\"]}", ErrorCode_InvalidAssetDefinition,
            "Unsupported terrain format \"obj\"" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"size\": 3}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"size\" in terrain \"t\";" },
        // The palette and the channels.
        { MODEL_HEAD "\"weights\": []}", ErrorCode_InvalidAssetDefinition,
            "\"materials\" must name 1 to 16 terrain materials: the palette" },
        { MODEL_HEAD "\"materials\": []}", ErrorCode_InvalidAssetDefinition, "\"materials\" must name 1 to 16" },
        { MODEL_HEAD "\"materials\": \"g\"}", ErrorCode_InvalidAssetDefinition, "\"materials\" must be an array." },
        { MODEL_HEAD "\"materials\": [\"a\", \"b\", \"c\", \"d\", \"e\", \"f\", \"g\", \"h\", \"i\", \"j\", \"k\", "
            "\"l\", \"m\", \"n\", \"o\", \"p\", \"q\"]}", ErrorCode_InvalidAssetDefinition,
            "\"materials\" holds 17 names; at most 16 are allowed." },
        { MODEL_HEAD "\"materials\": [3]}", ErrorCode_InvalidAssetDefinition, "materials[0] must be a string." },
        { MODEL_HEAD "\"materials\": [\"g\", \"Grass\"]}", ErrorCode_InvalidAssetDefinition,
            "materials[1] is \"Grass\"; names are asset names" },
        { MODEL_HEAD "\"materials\": [\"g\", \"h\", \"g\"]}", ErrorCode_InvalidAssetDefinition,
            "materials[0] and materials[2] are both \"g\"." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"density_channels\": [\"Trees\"]}", ErrorCode_InvalidAssetDefinition,
            "density_channels[0] is \"Trees\"; names are [a-z][a-z0-9_]*" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"density_channels\": [\"tree.s\"]}", ErrorCode_InvalidAssetDefinition,
            "density_channels[0] is \"tree.s\"" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"density_channels\": [\"a\", \"b\", \"c\", \"d\", \"e\", \"f\", \"g\", "
            "\"h\", \"i\"]}", ErrorCode_InvalidAssetDefinition, "\"density_channels\" holds 9 names; at most 8" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"density_channels\": [\"a\", \"a\"]}", ErrorCode_InvalidAssetDefinition,
            "density_channels[0] and density_channels[1] are both \"a\"." },
        // The mappings.
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": {}}", ErrorCode_InvalidAssetDefinition,
            "\"weights\" must be an array." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": [{\"attribute\": \"COLOR_0\", \"r\": \"g\"}, "
            "{\"attribute\": \"COLOR_1\", \"g\": \"g\"}]}", ErrorCode_InvalidAssetDefinition,
            "\"weights\" holds 2 mappings; it can have one per palette material at most (1)." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": [7]}", ErrorCode_InvalidAssetDefinition,
            "weights[0] must be a JSON object." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": [{\"attribute\": \"COLOR_0\", \"r\": \"g\", \"x\": 1}]}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"x\" in terrain \"t\", weights[0];" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": [{\"r\": \"g\"}]}", ErrorCode_InvalidAssetDefinition,
            "weights[0] needs an \"attribute\": the name of a colour or custom attribute of the file" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": [{\"attribute\": \"\", \"r\": \"g\"}]}",
            ErrorCode_InvalidAssetDefinition, "weights[0] needs an \"attribute\"" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": [{\"attribute\": \"COLOR_0\", \"r\": \"sand\"}]}",
            ErrorCode_InvalidAssetDefinition, "weights[0].r is \"sand\", which is not one of its \"materials\"." },
        { MODEL_HEAD "\"materials\": [\"g\", \"h\"], \"weights\": [{\"attribute\": \"COLOR_0\", \"r\": \"g\", "
            "\"b\": \"g\"}]}", ErrorCode_InvalidAssetDefinition,
            "weights[0].b feeds \"g\", which another channel feeds already." },
        { MODEL_HEAD "\"materials\": [\"g\", \"h\"], \"weights\": [{\"attribute\": \"COLOR_0\", \"r\": \"g\"}, "
            "{\"attribute\": \"COLOR_1\", \"a\": \"g\"}]}", ErrorCode_InvalidAssetDefinition,
            "weights[1].a feeds \"g\", which another channel feeds already." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": [{\"attribute\": \"COLOR_0\"}]}",
            ErrorCode_InvalidAssetDefinition, "weights[0] maps none of its channels (\"r\", \"g\", \"b\", \"a\")." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"weights\": [{\"attribute\": \"COLOR_0\", \"r\": 0}]}",
            ErrorCode_InvalidAssetDefinition, "weights[0].r must be a name." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"density_channels\": [\"trees\"], \"densities\": [{\"attribute\": "
            "\"_D\", \"r\": \"g\"}]}", ErrorCode_InvalidAssetDefinition,
            "densities[0].r is \"g\", which is not one of its \"density_channels\"." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"densities\": [{\"attribute\": \"_D\", \"r\": \"trees\"}]}",
            ErrorCode_InvalidAssetDefinition,
            "\"densities\" holds 1 mappings; it can have one per density channel at most (0)." },
        // The model's options.
        { MODEL_HEAD "\"materials\": [\"g\"], \"normals\": \"flat\"}", ErrorCode_InvalidAssetDefinition,
            "\"normals\" must be \"smooth\" (computed from the welded mesh) or \"imported\"" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"normals\": 1}", ErrorCode_InvalidAssetDefinition,
            "\"normals\" must be \"smooth\" or \"imported\"." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"blend_slot\": \"hole\"}", ErrorCode_InvalidAssetDefinition,
            "\"blend_slot\" must be 1 to 128 bytes of UTF-8, and neither \"hole\" nor a palette material." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"blend_slot\": \"g\"}", ErrorCode_InvalidAssetDefinition,
            "\"blend_slot\" must be 1 to 128 bytes" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"blend_slot\": \"\"}", ErrorCode_InvalidAssetDefinition,
            "\"blend_slot\" must be 1 to 128 bytes" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"blend_slot\": false}", ErrorCode_InvalidAssetDefinition,
            "\"blend_slot\" must be a material slot's name." },
        // A palette material named as a slot the import reads first could never be a face's.
        { MODEL_HEAD "\"materials\": [\"g\", \"hole\"]}", ErrorCode_InvalidAssetDefinition,
            "materials[1] is \"hole\", the name of the material slot that makes holes: a face could never take that "
            "material. Rename the material." },
        { MODEL_HEAD "\"materials\": [\"blend\", \"g\"]}", ErrorCode_InvalidAssetDefinition,
            "materials[0] is \"blend\", the name of the material slot that uses the painted weights: a face could never "
            "take that material. Rename the material or set another \"blend_slot\"." },
        // The chunk size.
        { MODEL_HEAD "\"materials\": [\"g\"], \"chunk_size\": 4}", ErrorCode_InvalidAssetDefinition,
            "\"chunk_size\" is 4; it must be 8 to 256 metres." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"chunk_size\": 300}", ErrorCode_InvalidAssetDefinition,
            "\"chunk_size\" is 300; it must be 8 to 256 metres." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"chunk_size\": true}", ErrorCode_InvalidAssetDefinition,
            "\"chunk_size\" must be a number of metres." },
        // The render block.
        { MODEL_HEAD "\"materials\": [\"g\"], \"render\": []}", ErrorCode_InvalidAssetDefinition,
            "\"render\" must be a JSON object." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"render\": {\"lod\": 1}}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"lod\" in terrain \"t\", render;" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"render\": {\"has_outline\": \"yes\"}}",
            ErrorCode_InvalidAssetDefinition, "render.has_outline must be true or false." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"render\": {\"shadow_tier\": \"middle\"}}",
            ErrorCode_InvalidAssetDefinition, "render.shadow_tier is \"middle\"; it must be one of the" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"render\": {\"draw_distance\": 0}}", ErrorCode_InvalidAssetDefinition,
            "render.draw_distance is 0; it must be above 0." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"render\": {\"draw_distance\": -5}}", ErrorCode_InvalidAssetDefinition,
            "render.draw_distance is -5; it must be above 0." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"render\": {\"draw_distance\": [1]}}",
            ErrorCode_InvalidAssetDefinition, "render.draw_distance must be a number of metres." },
        // The collision block.
        { MODEL_HEAD "\"materials\": [\"g\"], \"collision\": 1}", ErrorCode_InvalidAssetDefinition,
            "\"collision\" must be a JSON object." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"collision\": {\"mask\": []}}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"mask\" in terrain \"t\", collision;" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"collision\": {\"is_enabled\": 1}}", ErrorCode_InvalidAssetDefinition,
            "collision.is_enabled must be true or false." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"collision\": {\"layer\": \"sky\"}}", ErrorCode_InvalidAssetDefinition,
            "collision.layer is \"sky\"; it must be one of the" },
        { MODEL_HEAD "\"materials\": [\"g\"], \"collision\": {\"collides_with\": \"world\"}}",
            ErrorCode_InvalidAssetDefinition, "\"collides_with\" must be an array." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"collision\": {\"collides_with\": [\"world\", \"air\"]}}",
            ErrorCode_InvalidAssetDefinition, "collision.collides_with[1] must be a layer's name, each named once." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"collision\": {\"collides_with\": [\"prop\", \"prop\"]}}",
            ErrorCode_InvalidAssetDefinition, "collision.collides_with[1] must be a layer's name, each named once." },
        { MODEL_HEAD "\"materials\": [\"g\"], \"collision\": {\"collides_with\": [5]}}",
            ErrorCode_InvalidAssetDefinition, "collision.collides_with[0] must be a layer's name" },
    };
#undef MODEL_HEAD
#undef DATA_HEAD
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
        // Exactly at the limits is fine: 16 materials, 8 channels, the chunk sizes' ends, the mask left empty.
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, "{\"name\": \"t\", \"source\": \"model\", \"model\": \"t\", "
            "\"materials\": [\"a\", \"b\", \"c\", \"d\", \"e\", \"f\", \"g\", \"h\", \"i\", \"j\", \"k\", \"l\", \"m\", "
            "\"n\", \"o\", \"p\"], \"density_channels\": [\"a\", \"b\", \"c\", \"d\", \"e\", \"f\", \"g\", \"h\"], "
            "\"chunk_size\": 8, \"collision\": {\"collides_with\": []}}"));
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, "{\"name\": \"t\", \"source\": \"data\", \"data\": \"t\", "
            "\"materials\": [\"a\"], \"chunk_size\": 256}"));
        // "blend" is a palette name like any other once the painted faces use another slot, and the binary form has no
        // slots at all.
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, "{\"name\": \"t\", \"source\": \"model\", \"model\": \"t\", "
            "\"materials\": [\"blend\", \"g\"], \"blend_slot\": \"paint\"}"));
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, "{\"name\": \"t\", \"source\": \"data\", \"data\": \"t\", "
            "\"materials\": [\"blend\", \"hole\"]}"));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: loading.
/* Source "data": the binary form a saved mesh wrote loads back as the same mesh; data whose palette or channels do not
 * match the definition's is refused, naming both counts. */
static void TheBinaryFormLoadsTheSavedMesh(TestRun* run)
{
    static const char* const Files[] =
    {
        "terrains/hills.json", "{\"name\": \"hills\", \"source\": \"data\", \"data\": \"hills\", \"materials\": "
            "[\"grass\", \"dirt\"], \"density_channels\": [\"trees\"], \"chunk_size\": 16}",
        "terrains/short.json", "{\"name\": \"short\", \"source\": \"data\", \"data\": \"hills\", \"materials\": "
            "[\"grass\"], \"density_channels\": [\"trees\"], \"chunk_size\": 16}",
        NULL
    };
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 10U, 6U, 4.0f, -20.0f, 5.0f, Slope);
    for (uint32_t Vertex = 0U; Vertex < Grid.VertexCount; Vertex++)
    {
        Grid.Weights[(Vertex * TERRAIN_MAX_MATERIALS) + 1U] = (uint8_t)(Vertex * 3U);
        Grid.Densities[Vertex * TERRAIN_MAX_DENSITY_CHANNELS] = (uint8_t)(Vertex * 5U);
    }
    Grid.Flags[3] = TERRAIN_TRIANGLE_FLAG_HOLE;
    TerrainMesh* Saved = TestTerrain_CreateMesh(run, &Grid, 2U, 1U, 16.0f);
    TerrainPack Pack;
    char Path[TERRAIN_ASSET_TEST_PATH_CAPACITY];
    bool IsReady = (Saved != NULL) && CreatePackFiles(run, &Pack, "binary", Files);
    if (IsReady)
    {
        snprintf(Path, sizeof(Path), "%s/terrains/hills.ghdf", (const char*)Pack.Root._data);
        IsReady = TEST_CHECK_SUCCESS(run, TerrainMesh_SaveGHDF(Saved, (const unsigned char*)Path))
            && OpenPack(run, &Pack);
    }
    TerrainAsset* Loaded = IsReady ? LoadTerrain(run, &Pack, "hills") : NULL;
    if (Loaded != NULL)
    {
        TEST_CHECK(run, TestTerrain_AreMeshesEqual(run, Saved, TerrainAsset_GetMesh(Loaded)));
        TEST_CHECK(run, (TerrainAsset_GetMaterialCount(Loaded) == 2U)
            && AreNamesEqual(TerrainAsset_GetMaterialName(Loaded, 1U), "dirt")
            && AreNamesEqual(TerrainAsset_GetDensityChannelName(Loaded, 0U), "trees"));
        TerrainAsset* Short = NULL;
        const char* const Fragments[] = { "Terrain \"short\": its data holds 2 materials and 1 density channels; the "
            "definition names 1 and 1.", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTerrain(Pack.Manager, (const unsigned char*)u8"short",
            Pack.User, &Short), ErrorCode_InvalidAssetData, Fragments);
        TEST_CHECK(run, Short == NULL);
    }
    if (Saved != NULL)
    {
        ClosePack(run, &Pack);
    }
    TerrainMesh_Destroy(Saved);
    TestTerrain_FreeGrid(&Grid);
}

/* A file that is no glTF fails the load with the terrain's name, and so does a file the definition's mapping does not
 * fit (an attribute the file does not have); the failure holds nothing. */
static void ABrokenFileFailsTheLoadWithTheTerrainsName(TestRun* run)
{
    static const char* const Files[] =
    {
        "terrains/junk.json", "{\"name\": \"junk\", \"source\": \"model\", \"model\": \"junk\", \"materials\": "
            "[\"grass\"]}",
        "terrains/unmapped.json", "{\"name\": \"unmapped\", \"source\": \"model\", \"model\": \"unmapped\", "
            "\"materials\": [\"grass\"], \"weights\": [{\"attribute\": \"COLOR_3\", \"r\": \"grass\"}]}",
        "terrains/missing.json", "{\"name\": \"missing\", \"source\": \"model\", \"model\": \"nowhere\", "
            "\"materials\": [\"grass\"]}",
        NULL
    };
    GenericBuffer Junk;
    GenericBuffer_AllocateVariable(&Junk, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Junk, (const unsigned char*)u8"this is no glTF file");
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 2U, 2U, 1.0f, 0.0f, 0.0f, NULL);
    const char* Slots[1] = { "blend" };
    uint32_t Counts[1] = { Grid.TriangleCount };
    GenericBuffer Glb;
    GenericBuffer_AllocateVariable(&Glb, 0U, sizeof(unsigned char));
    WriteGridGlb(&Grid, Slots, Counts, 1U, NULL, 0U, &Glb);
    TerrainPack Pack;
    if (CreatePackFiles(run, &Pack, "broken", Files) && WriteBytes(run, &Pack.Root, "terrains/junk.glb", &Junk)
        && WriteBytes(run, &Pack.Root, "terrains/unmapped.glb", &Glb) && OpenPack(run, &Pack))
    {
        TerrainAsset* Loaded = NULL;
        const char* const JunkFragments[] = { "Terrain \"junk\"", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTerrain(Pack.Manager, (const unsigned char*)u8"junk", Pack.User,
            &Loaded), ErrorCode_InvalidAssetData, JunkFragments);
        const char* const UnmappedFragments[] = { "Terrain \"unmapped\"", "COLOR_3", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTerrain(Pack.Manager, (const unsigned char*)u8"unmapped",
            Pack.User, &Loaded), ErrorCode_InvalidAssetData, UnmappedFragments);
        TEST_CHECK_ERROR(run, AssetManager_LoadTerrain(Pack.Manager, (const unsigned char*)u8"missing", Pack.User,
            &Loaded), ErrorCode_FileNotFound);
        TEST_CHECK(run, Loaded == NULL);
    }
    ClosePack(run, &Pack);
    Memory_Free(Glb._data);
    Memory_Free(Junk._data);
    TestTerrain_FreeGrid(&Grid);
}


// Tests: the terrain component's overrides.
/* The component starts at its documented defaults: no terrain, no override, the placement's own values at the asset's
 * defaults, a multiplier of 1; resolved over the defaults, the settings are the defaults with the mask resolved. */
static void TheComponentStartsAtItsDefaults(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, WorldComponentTypeInfo_Check(WorldTerrainComponent_GetTypeInfo()));
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldTerrainComponent* Placement = AddTerrainObject(run, &TestWorld);
    if (Placement != NULL)
    {
        TEST_CHECK(run, WorldTerrainComponent_GetTerrain(Placement) == NULL);
        for (int Setting = 0; Setting < (int)WorldTerrainSetting_Count; Setting++)
        {
            TEST_CHECK(run, !WorldTerrainComponent_IsOverridden(Placement, (WorldTerrainSetting)Setting));
        }
        TEST_CHECK(run, WorldTerrainComponent_HasOutline(Placement)
            && (WorldTerrainComponent_GetShadowTier(Placement) == WorldShadowTier_Both)
            && WorldTerrainComponent_CastsShadows(Placement)
            && (WorldTerrainComponent_GetDrawDistance(Placement) == TERRAIN_DEFAULT_DRAW_DISTANCE)
            && (WorldTerrainComponent_GetDrawDistanceMultiplier(Placement) == 1.0f)
            && WorldTerrainComponent_IsDrawDistanceFixed(Placement));
        TEST_CHECK(run, WorldTerrainComponent_IsCollisionEnabled(Placement)
            && (WorldTerrainComponent_GetCollisionLayer(Placement) == CollisionLayer_World)
            && (WorldTerrainComponent_GetCollidesWith(Placement) == CollisionLayer_GetDefaultMask(CollisionLayer_World)));
        TerrainSettings Defaults = TerrainSettings_CreateDefault();
        TerrainSettings Resolved = WorldTerrainComponent_ResolveSettings(Placement, &Defaults);
        TEST_CHECK(run, Resolved.Collision.HasCollidesWith
            && (Resolved.Collision.CollidesWith == CollisionLayer_GetDefaultMask(CollisionLayer_World)));
        Resolved.Collision.HasCollidesWith = false;
        TEST_CHECK(run, AreSettingsEqual(&Resolved, &Defaults));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* Every setting a placement can override is a flag row "overrides_<key>" (a Bool, off by default) right before its
 * value row, and the typed accessors reach the same fields as the table. */
static void EveryOverrideIsAFlagNextToItsValue(TestRun* run)
{
    static const char* const ValueKeys[WorldTerrainSetting_Count] =
    {
        [WorldTerrainSetting_HasOutline] = "has_outline",
        [WorldTerrainSetting_ShadowTier] = "shadow_tier",
        [WorldTerrainSetting_CastsShadows] = "casts_shadows",
        [WorldTerrainSetting_DrawDistance] = "draw_distance",
        [WorldTerrainSetting_IsDrawDistanceFixed] = "is_draw_distance_fixed",
        [WorldTerrainSetting_IsCollisionEnabled] = "is_collision_enabled",
        [WorldTerrainSetting_CollisionLayer] = "collision_layer",
        [WorldTerrainSetting_CollidesWith] = "collides_with",
    };
    const PropertyTable* Table = WorldTerrainComponent_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(Table));
    TEST_CHECK(run, (Table->DescriptorCount > 0U) && AreNamesEqual(Table->Descriptors[0].Key, "terrain"));
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldTerrainComponent* Placement = AddTerrainObject(run, &TestWorld);
    if (Placement != NULL)
    {
        WorldComponent* Component = WorldTerrainComponent_AsComponent(Placement);
        for (int Setting = 0; Setting < (int)WorldTerrainSetting_Count; Setting++)
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
            uint32_t Revision = WorldComponent_GetRevision(Component);
            TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Placement, (WorldTerrainSetting)Setting, true));
            for (int Other = 0; Other < (int)WorldTerrainSetting_Count; Other++)
            {
                TEST_CHECK(run, WorldTerrainComponent_IsOverridden(Placement, (WorldTerrainSetting)Other)
                    == (Other == Setting));
            }
            PropertyValue Read = { 0 };
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, Properties_Get(Table, Placement, Flag->Key, &Read))
                && Read.Data.BoolValue && (WorldComponent_GetRevision(Component) == (Revision + 1U)));
            PropertyValue Off = PropertyValue_FromBool(false);
            TEST_CHECK_SUCCESS(run, Properties_Set(Table, Placement, Flag->Key, &Off));
            TEST_CHECK(run, !WorldTerrainComponent_IsOverridden(Placement, (WorldTerrainSetting)Setting)
                && (WorldComponent_GetRevision(Component) == (Revision + 2U)));
        }
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetOverridden(Placement, WorldTerrainSetting_Count, true),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, !WorldTerrainComponent_IsOverridden(Placement, (WorldTerrainSetting)42));

        // Every setter advances the revision; the value setters check the table's rules and change nothing when refused.
        uint32_t Revision = WorldComponent_GetRevision(Component);
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetTerrain(Placement, (const unsigned char*)u8"town"));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetHasOutline(Placement, false));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetShadowTier(Placement, WorldShadowTier_Far));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetCastsShadows(Placement, false));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetDrawDistance(Placement, 1500.0f));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetDrawDistanceMultiplier(Placement, 0.5f));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetIsDrawDistanceFixed(Placement, false));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetIsCollisionEnabled(Placement, false));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetCollisionLayer(Placement, CollisionLayer_Prop));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetCollidesWith(Placement,
            COLLISION_LAYER_BIT(CollisionLayer_Prop)));
        TEST_CHECK(run, WorldComponent_GetRevision(Component) == (Revision + 10U));
        TEST_CHECK(run, AreNamesEqual(WorldTerrainComponent_GetTerrain(Placement), "town")
            && !WorldTerrainComponent_HasOutline(Placement)
            && (WorldTerrainComponent_GetShadowTier(Placement) == WorldShadowTier_Far)
            && !WorldTerrainComponent_CastsShadows(Placement)
            && (WorldTerrainComponent_GetDrawDistance(Placement) == 1500.0f)
            && (WorldTerrainComponent_GetDrawDistanceMultiplier(Placement) == 0.5f)
            && !WorldTerrainComponent_IsDrawDistanceFixed(Placement)
            && !WorldTerrainComponent_IsCollisionEnabled(Placement)
            && (WorldTerrainComponent_GetCollisionLayer(Placement) == CollisionLayer_Prop)
            && (WorldTerrainComponent_GetCollidesWith(Placement) == COLLISION_LAYER_BIT(CollisionLayer_Prop)));

        Revision = WorldComponent_GetRevision(Component);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetTerrain(Placement, (const unsigned char*)u8"Town"),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetShadowTier(Placement, (WorldShadowTier)9),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetDrawDistance(Placement, 0.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetDrawDistance(Placement, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetDrawDistanceMultiplier(Placement, -1.0f),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetDrawDistanceMultiplier(Placement, INFINITY),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetCollisionLayer(Placement, (CollisionLayer)40),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetCollidesWith(Placement, (uint32_t)1U << 30U),
            ErrorCode_ArgumentOutOfRange);
        char LongName[WORLD_TERRAIN_COMPONENT_NAME_MAX_LENGTH + 2U];
        Memory_Set(LongName, 't', WORLD_TERRAIN_COMPONENT_NAME_MAX_LENGTH + 1U);
        LongName[WORLD_TERRAIN_COMPONENT_NAME_MAX_LENGTH + 1U] = '\0';
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetTerrain(Placement, (const unsigned char*)LongName),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, (WorldComponent_GetRevision(Component) == Revision)
            && AreNamesEqual(WorldTerrainComponent_GetTerrain(Placement), "town")
            && (WorldTerrainComponent_GetDrawDistance(Placement) == 1500.0f)
            && (WorldTerrainComponent_GetCollisionLayer(Placement) == CollisionLayer_Prop));
        LongName[WORLD_TERRAIN_COMPONENT_NAME_MAX_LENGTH] = '\0';
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetTerrain(Placement, (const unsigned char*)LongName));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetTerrain(Placement, NULL));
        TEST_CHECK(run, WorldTerrainComponent_GetTerrain(Placement) == NULL);

        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetTerrain(NULL, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetOverridden(NULL, WorldTerrainSetting_HasOutline, true),
            ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldTerrainComponent_SetCollidesWith(NULL, 0U), ErrorCode_IllegalArgument);
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* The settings in effect: the terrain's while every flag is off, the placement's own value for each flag that is on
 * (each alone), the multiplier on whichever draw distance is in effect, and the mask: the placement's, else the
 * terrain's, else the default mask of the layer in effect (so a placement that only moves the layer gets that layer's
 * default when the terrain gives no mask). */
static void TheFlagsChooseBetweenTheTerrainAndThePlacement(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldTerrainComponent* Placement = AddTerrainObject(run, &TestWorld);
    if (Placement != NULL)
    {
        const uint32_t AssetMask = COLLISION_LAYER_BIT(CollisionLayer_Character);
        const uint32_t OwnMask = COLLISION_LAYER_BIT(CollisionLayer_Vehicle);
        TerrainSettings Asset =
        {
            .Render = { .HasOutline = false, .ShadowTier = WorldShadowTier_Far, .CastsShadows = false,
                .DrawDistance = 800.0f, .IsDrawDistanceFixed = true },
            .Collision = { .IsEnabled = false, .Layer = CollisionLayer_Dynamic, .HasCollidesWith = true,
                .CollidesWith = AssetMask }
        };
        // The placement's own values, all different from the asset's; their flags stay off for now.
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetHasOutline(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetShadowTier(Placement, WorldShadowTier_Near));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetCastsShadows(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetDrawDistance(Placement, 300.0f));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetIsDrawDistanceFixed(Placement, false));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetIsCollisionEnabled(Placement, true));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetCollisionLayer(Placement, CollisionLayer_Prop));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetCollidesWith(Placement, OwnMask));

        TerrainSettings Resolved = WorldTerrainComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, AreSettingsEqual(&Resolved, &Asset));

        // Each flag alone takes exactly its own setting from the placement.
        for (int Setting = 0; Setting < (int)WorldTerrainSetting_Count; Setting++)
        {
            TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Placement, (WorldTerrainSetting)Setting, true));
            Resolved = WorldTerrainComponent_ResolveSettings(Placement, &Asset);
            TEST_CHECK(run, Resolved.Render.HasOutline == (Setting == (int)WorldTerrainSetting_HasOutline));
            TEST_CHECK(run, Resolved.Render.ShadowTier
                == ((Setting == (int)WorldTerrainSetting_ShadowTier) ? WorldShadowTier_Near : WorldShadowTier_Far));
            TEST_CHECK(run, Resolved.Render.CastsShadows == (Setting == (int)WorldTerrainSetting_CastsShadows));
            TEST_CHECK(run, Resolved.Render.DrawDistance
                == ((Setting == (int)WorldTerrainSetting_DrawDistance) ? 300.0f : 800.0f));
            TEST_CHECK(run, Resolved.Render.IsDrawDistanceFixed
                == (Setting != (int)WorldTerrainSetting_IsDrawDistanceFixed));
            TEST_CHECK(run, Resolved.Collision.IsEnabled == (Setting == (int)WorldTerrainSetting_IsCollisionEnabled));
            TEST_CHECK(run, Resolved.Collision.Layer
                == ((Setting == (int)WorldTerrainSetting_CollisionLayer) ? CollisionLayer_Prop : CollisionLayer_Dynamic));
            TEST_CHECK(run, Resolved.Collision.HasCollidesWith && (Resolved.Collision.CollidesWith
                == ((Setting == (int)WorldTerrainSetting_CollidesWith) ? OwnMask : AssetMask)));
            TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Placement, (WorldTerrainSetting)Setting, false));
        }

        // The multiplier scales the distance in effect, the asset's or the placement's; past the float range it is
        // +infinity.
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetDrawDistanceMultiplier(Placement, 2.5f));
        TEST_CHECK(run, WorldTerrainComponent_ResolveSettings(Placement, &Asset).Render.DrawDistance == 2000.0f);
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Placement, WorldTerrainSetting_DrawDistance, true));
        TEST_CHECK(run, WorldTerrainComponent_ResolveSettings(Placement, &Asset).Render.DrawDistance == 750.0f);
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetDrawDistance(Placement, 3e38f));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetDrawDistanceMultiplier(Placement, 1e10f));
        TEST_CHECK(run, isinf(WorldTerrainComponent_ResolveSettings(Placement, &Asset).Render.DrawDistance));

        // A terrain without a mask: the default mask of the layer in effect, the asset's layer or the placement's (the
        // world layer's leaves out triggers, the prop layer's does not).
        TEST_CHECK(run, CollisionLayer_GetDefaultMask(CollisionLayer_Prop)
            != CollisionLayer_GetDefaultMask(CollisionLayer_World));
        Asset.Collision.Layer = CollisionLayer_World;
        Asset.Collision.HasCollidesWith = false;
        Asset.Collision.CollidesWith = 0U;
        Resolved = WorldTerrainComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, Resolved.Collision.HasCollidesWith
            && (Resolved.Collision.CollidesWith == CollisionLayer_GetDefaultMask(CollisionLayer_World)));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Placement, WorldTerrainSetting_CollisionLayer, true));
        Resolved = WorldTerrainComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, (Resolved.Collision.Layer == CollisionLayer_Prop)
            && (Resolved.Collision.CollidesWith == CollisionLayer_GetDefaultMask(CollisionLayer_Prop)));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Placement, WorldTerrainSetting_CollidesWith, true));
        TEST_CHECK(run, WorldTerrainComponent_ResolveSettings(Placement, &Asset).Collision.CollidesWith == OwnMask);
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* A terrain object in a world file: the component is "terrain" under "components", its rows by key, and it comes back
 * the same from JSON and GHDF. */
static void ATerrainObjectIsReadFromAWorldFile(TestRun* run)
{
    static const char* const Text =
        "{\"format_version\": 1, \"objects\": [{\"id\": 4, \"name\": \"ground\", \"position\": [10.5, 0, -3], "
        "\"components\": {\"terrain\": {\"terrain\": \"town\", \"overrides_draw_distance\": true, "
        "\"draw_distance\": 1200, \"draw_distance_multiplier\": 1.5, \"overrides_collision_layer\": true, "
        "\"collision_layer\": \"prop\", \"overrides_collides_with\": true, \"collides_with\": [\"character\"]}}}]}";
    KeyRegistry Keys;
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys)))
    {
        return;
    }
    WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
    World Loaded;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, WorldFile_ReadJSON((const unsigned char*)Text, strlen(Text), &Options, &Loaded)))
    {
        WorldComponent* Component = NULL;
        if (TEST_CHECK(run, World_TryGetComponent(&Loaded, 4U, WorldComponentType_Terrain, &Component)))
        {
            WorldTerrainComponent* Placement = WorldTerrainComponent_FromComponent(Component);
            TEST_CHECK(run, AreNamesEqual(WorldTerrainComponent_GetTerrain(Placement), "town")
                && WorldTerrainComponent_IsOverridden(Placement, WorldTerrainSetting_DrawDistance)
                && (WorldTerrainComponent_GetDrawDistance(Placement) == 1200.0f)
                && (WorldTerrainComponent_GetDrawDistanceMultiplier(Placement) == 1.5f)
                && (WorldTerrainComponent_GetCollisionLayer(Placement) == CollisionLayer_Prop)
                && (WorldTerrainComponent_GetCollidesWith(Placement) == COLLISION_LAYER_BIT(CollisionLayer_Character))
                && !WorldTerrainComponent_IsOverridden(Placement, WorldTerrainSetting_HasOutline));
        }
        World Again;
        if (TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Loaded, &Bytes))
            && TEST_CHECK_SUCCESS(run, WorldFile_ReadGHDF(Bytes._data, Bytes._count, &Options, &Again)))
        {
            WorldComponent* Read = NULL;
            TEST_CHECK(run, World_TryGetComponent(&Again, 4U, WorldComponentType_Terrain, &Read)
                && Properties_Equals(WorldTerrainComponent_GetPropertyTable(), Component, Read));
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Again));
        }
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
    }
    Memory_Free(Bytes._data);
    KeyRegistry_Deconstruct(&Keys);
}


// Tests: the shipped content.
/* The test world's placeholder terrain (asset_source/scripts/placeholder_terrain.py) loads through the real type: every
 * vertex welded back from its slots' primitives, its triangles (none dropped), the cave mouth's holes, the dirt patch's
 * face material, painted weights that sum to one, densities, chunks of 32 m, and no chunk left without a surface. */
static void TheShippedTerrainLoads(TestRun* run)
{
    TerrainPack Pack;
    Memory_Zero(&Pack, sizeof(Pack));
    GenericBuffer_AllocateVariable(&Pack.Root, TERRAIN_ASSET_TEST_PATH_CAPACITY, sizeof(unsigned char));
    TerrainAsset* Loaded = (TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Pack.Root))
        && OpenPack(run, &Pack)) ? LoadTerrain(run, &Pack, "placeholder_terrain") : NULL;
    if (Loaded != NULL)
    {
        const TerrainMesh* Terrain = TerrainAsset_GetMesh(Loaded);
        // The generator's 31337 grid points less the 19 x 19 inside the slab's open square, which no face uses.
        TEST_CHECK(run, (TerrainMesh_GetVertexCount(Terrain) == 30976U)
            && (TerrainMesh_GetTriangleCount(Terrain) == 61174U) && (TerrainAsset_GetMaterialCount(Loaded) == 3U)
            && (TerrainAsset_GetDensityChannelCount(Loaded) == 1U)
            && (TerrainMesh_GetChunkSize(Terrain) == TERRAIN_DEFAULT_CHUNK_SIZE)
            && (TerrainMesh_GetChunkCount(Terrain) == 256U));
        uint32_t Holes = 0U;
        uint32_t Patch = 0U;
        for (uint32_t Triangle = 0U; Triangle < TerrainMesh_GetTriangleCount(Terrain); Triangle++)
        {
            const TerrainTriangle* Entry = TerrainMesh_GetTriangle(Terrain, Triangle);
            Holes += ((Entry->_flags & TERRAIN_TRIANGLE_FLAG_HOLE) != 0U) ? 1U : 0U;
            Patch += (((Entry->_flags & TERRAIN_TRIANGLE_FLAG_OVERRIDES_MATERIAL) != 0U) && (Entry->_material == 1U))
                ? 1U : 0U;
        }
        TEST_CHECK(run, (Holes == 6U) && (Patch == 72U));
        bool AreWeightsWhole = true;
        uint32_t Wooded = 0U;
        for (uint32_t Vertex = 0U; Vertex < TerrainMesh_GetVertexCount(Terrain); Vertex++)
        {
            const uint8_t* Weights = TerrainMesh_GetWeights(Terrain, Vertex);
            int Sum = (int)Weights[0] + (int)Weights[1] + (int)Weights[2];
            AreWeightsWhole = AreWeightsWhole && (Sum >= 253) && (Sum <= 257);
            Wooded += (TerrainMesh_GetDensities(Terrain, Vertex)[0] > 0U) ? 1U : 0U;
        }
        TEST_CHECK(run, AreWeightsWhole && (Wooded > 1000U) && (Wooded < TerrainMesh_GetVertexCount(Terrain)));
        bool AreSurfaced = true;
        for (uint32_t Chunk = 0U; Chunk < TerrainMesh_GetChunkCount(Terrain); Chunk++)
        {
            const TerrainChunk* Entry = TerrainMesh_GetChunk(Terrain, Chunk);
            AreSurfaced = AreSurfaced && (TerrainChunk_GetSurfaceTriangleCount(Entry) > 0U);
        }
        TEST_CHECK(run, AreSurfaced);
    }
    ClosePack(run, &Pack);
}

// Public functions.
void TerrainAssetTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_minimal_definition_gets_the_defaults", AMinimalDefinitionGetsTheDefaults },
        { "a_full_definition_reads_every_key", AFullDefinitionReadsEveryKey },
        { "each_rule_is_refused_with_a_clear_message", EachRuleIsRefusedWithAClearMessage },
        { "the_binary_form_loads_the_saved_mesh", TheBinaryFormLoadsTheSavedMesh },
        { "a_broken_file_fails_the_load_with_the_terrains_name", ABrokenFileFailsTheLoadWithTheTerrainsName },
        { "the_component_starts_at_its_defaults", TheComponentStartsAtItsDefaults },
        { "every_override_is_a_flag_next_to_its_value", EveryOverrideIsAFlagNextToItsValue },
        { "the_flags_choose_between_the_terrain_and_the_placement", TheFlagsChooseBetweenTheTerrainAndThePlacement },
        { "a_terrain_object_is_read_from_a_world_file", ATerrainObjectIsReadFromAWorldFile },
        { "the_shipped_terrain_loads", TheShippedTerrainLoads },
    };
    TestRun_RunSuite(run, "TerrainAsset", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
