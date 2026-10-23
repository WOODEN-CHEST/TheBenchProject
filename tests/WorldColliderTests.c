#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TestWorld.h"
#include "CollisionLayer.h"
#include "CollisionShape.h"
#include "PropertyDescriptor.h"
#include "World.h"
#include "WorldColliderComponent.h"
#include "WorldModel.h"
#include "raylib/raymath.h"
#include "wr/WRGHDF.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for the float comparisons. */
#define TEST_EPSILON 1e-4f
/** Room for a definition built in a test. */
#define COLLIDER_TEST_JSON_CAPACITY 16384


// Types.
/* A refused definition: its "collision" value, the code and a fragment its message must hold. */
typedef struct RefusedCollisionStruct
{
    const char* Json;
    ErrorCode Code;
    const char* Fragment;
} RefusedCollision;


// Fields.
/* A cube's corners as a CPU mesh: 8 vertices, 12 triangles (counter-clockwise from outside). */
static const float CubeVertices[] =
{
    -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f,
    -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f
};
static const unsigned short CubeIndices[] =
{
    0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5
};


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static bool AreNamesEqual(const unsigned char* a, const char* b)
{
    return (a != NULL) && (b != NULL) && (strcmp((const char*)a, b) == 0);
}

static Error BuildWorldModelDefinition(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.WorldModel, &Bytes,
        (const unsigned char*)u8"test", &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}

/* A world model of one LOD ("cube_mesh") with @p collision as its "collision" value. */
static void FormatWorldModel(char* buffer, size_t capacity, const char* name, const char* collision)
{
    snprintf(buffer, capacity, "{\"name\": \"%s\", \"lods\": [{\"model\": \"cube_mesh\", \"max_distance\": 50}], "
        "\"collision\": %s}", name, collision);
}

static bool AddCubeModels(TestRun* run, TestAssetFixture* fixture)
{
    BoundingBox Box = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    return TestAssets_AddModel(run, fixture, "cube_mesh", Box, NULL)
        && TestAssets_AddMeshModel(run, fixture, "cube_triangles", CubeVertices, 8, CubeIndices, 12, MatrixIdentity(),
            NULL);
}

static WorldModel* LoadWorldModel(TestRun* run, TestAssetFixture* fixture, const char* name, AssetUserID* outUser)
{
    WorldModel* Loaded = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(fixture->Manager, outUser)))
    {
        return NULL;
    }
    TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldModel(fixture->Manager, (const unsigned char*)name, *outUser,
        &Loaded));
    return Loaded;
}


// Static functions: the collision section.
static void TheCollisionSectionReadsEveryKey(TestRun* run)
{
    TestAssetFixture Fixture;
    char Json[COLLIDER_TEST_JSON_CAPACITY];
    FormatWorldModel(Json, sizeof(Json), "door",
        "{\"default_set\": \"open\", \"layer\": \"world\", \"collides_with\": [\"character\", \"vehicle\"], "
        "\"is_trigger\": true, \"mass\": 25, \"sets\": ["
        "{\"name\": \"closed\", \"shapes\": ["
        "{\"type\": \"box\", \"center\": [0, 1, 0], \"rotation\": [0, 90, 0], \"half_extents\": [0.5, 1, 0.05], "
            "\"name\": \"panel\"},"
        "{\"type\": \"sphere\", \"center\": [0.4, 1, 0.1], \"radius\": 0.04, \"name\": \"handle\"},"
        "{\"type\": \"capsule\", \"radius\": 0.2, \"half_height\": 0.6},"
        "{\"type\": \"cylinder\", \"radius\": 0.3, \"half_height\": 0.25},"
        "{\"type\": \"plane\", \"half_extents\": [2, 3], \"depth\": 0.5},"
        "{\"type\": \"plane\", \"is_bounded\": false},"
        "{\"type\": \"convex_hull\", \"points\": [[-1, 0, -1], [1, 0, -1], [1, 0, 1], [-1, 0, 1], [0, 2, 0]]},"
        "{\"type\": \"convex_hull\", \"model\": \"cube_triangles\", \"max_vertices\": 16},"
        "{\"type\": \"mesh\", \"model\": \"cube_triangles\", \"is_two_sided\": false}]},"
        "{\"name\": \"open\", \"shapes\": [{\"type\": \"box\", \"half_extents\": [0.05, 1, 0.5]}]}]}");
    AssetUserID User = ASSET_USER_ID_INVALID;
    WorldModel* Door = (TestAssetFixture_Construct(run, &Fixture) && AddCubeModels(run, &Fixture)
        && TestAssets_AddWorldModel(run, &Fixture, Json)) ? LoadWorldModel(run, &Fixture, "door", &User) : NULL;
    const WorldModelCollision* Collision = (Door != NULL) ? WorldModel_GetCollision(Door) : NULL;
    if (TEST_CHECK(run, Collision != NULL))
    {
        TEST_CHECK(run, (WorldModelCollision_GetSetCount(Collision) == 2U)
            && (WorldModelCollision_GetDefaultSetIndex(Collision) == 1U));
        TEST_CHECK(run, (Collision->_layer == CollisionLayer_World) && Collision->_hasCollidesWith
            && Collision->_isTrigger && (Collision->_collidesWith == (COLLISION_LAYER_BIT(CollisionLayer_Character)
                | COLLISION_LAYER_BIT(CollisionLayer_Vehicle))));
        TEST_CHECK(run, IsNear(Collision->_mass, 25.0f));
        uint32_t Open = 0U;
        TEST_CHECK(run, WorldModelCollision_TryFindSet(Collision, (const unsigned char*)u8"open", &Open)
            && (Open == 1U));
        TEST_CHECK(run, !WorldModelCollision_TryFindSet(Collision, (const unsigned char*)u8"ajar", &Open));
        const CollisionSet* Closed = WorldModelCollision_GetSet(Collision, 0U);
        if (TEST_CHECK(run, (Closed != NULL) && (CollisionSet_GetShapeCount(Closed) == 9U)))
        {
            const CollisionShape* Panel = CollisionSet_GetShape(Closed, 0U);
            TEST_CHECK(run, (Panel->Type == CollisionShapeType_Box) && AreNamesEqual(Panel->Name, "panel")
                && IsNear(Panel->Center.y, 1.0f) && IsNear(Panel->Rotation.y, PI * 0.5f)
                && IsNear(Panel->HalfExtents.z, 0.05f));
            const CollisionShape* Handle = CollisionSet_GetShape(Closed, 1U);
            TEST_CHECK(run, (Handle->Type == CollisionShapeType_Sphere) && IsNear(Handle->Radius, 0.04f)
                && AreNamesEqual(Handle->Name, "handle"));
            TEST_CHECK(run, (CollisionSet_GetShape(Closed, 2U)->Type == CollisionShapeType_Capsule)
                && IsNear(CollisionSet_GetShape(Closed, 2U)->HalfHeight, 0.6f)
                && (CollisionSet_GetShape(Closed, 2U)->Name == NULL));
            TEST_CHECK(run, IsNear(CollisionSet_GetShape(Closed, 3U)->Radius, 0.3f));
            const CollisionShape* Floor = CollisionSet_GetShape(Closed, 4U);
            TEST_CHECK(run, Floor->IsBounded && IsNear(Floor->PlaneHalfExtents.x, 2.0f)
                && IsNear(Floor->PlaneHalfExtents.y, 3.0f) && IsNear(Floor->Depth, 0.5f));
            TEST_CHECK(run, !CollisionSet_GetShape(Closed, 5U)->IsBounded);
            const CollisionShape* Pyramid = CollisionSet_GetShape(Closed, 6U);
            TEST_CHECK(run, (Pyramid->Hull != NULL) && (Pyramid->Hull->_vertexCount == 5U)
                && (Pyramid->Hull->_faceCount == 5U));
            const CollisionShape* CubeHull = CollisionSet_GetShape(Closed, 7U);
            TEST_CHECK(run, (CubeHull->Hull != NULL) && (CubeHull->Hull->_vertexCount == 8U)
                && (CubeHull->Hull->_faceCount == 6U));
            const CollisionShape* CubeMesh = CollisionSet_GetShape(Closed, 8U);
            TEST_CHECK(run, (CubeMesh->Mesh != NULL) && (CubeMesh->Mesh->_triangleCount == 12U)
                && !CubeMesh->IsTwoSided);
        }
    }
    if (User != ASSET_USER_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TheBoundsFallbackIsABoxAroundLod0(TestRun* run)
{
    TestAssetFixture Fixture;
    BoundingBox Box = { .min = { -1.0f, 0.0f, -0.5f }, .max = { 1.0f, 2.0f, 0.5f } };
    Matrix Turned = MatrixMultiply(MatrixScale(2.0f, 2.0f, 2.0f), MatrixRotateY(PI * 0.5f));
    AssetUserID User = ASSET_USER_ID_INVALID;
    bool IsBuilt = TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddModelWithMaterials(run, &Fixture, "crate_mesh", Box, 0, Turned, NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"crate\", \"lods\": [{\"model\": \"crate_mesh\", "
            "\"max_distance\": 50}], \"collision\": \"bounds\"}");
    WorldModel* Crate = IsBuilt ? LoadWorldModel(run, &Fixture, "crate", &User) : NULL;
    const WorldModelCollision* Collision = (Crate != NULL) ? WorldModel_GetCollision(Crate) : NULL;
    if (TEST_CHECK(run, (Collision != NULL) && Collision->_isBoundsFallback
        && (WorldModelCollision_GetSetCount(Collision) == 1U)))
    {
        TEST_CHECK(run, IsNear(Collision->_mass, COLLISION_DEFAULT_MASS));
        const CollisionSet* Set = WorldModelCollision_GetSet(Collision, 0U);
        const CollisionShape* Shape = CollisionSet_GetShape(Set, 0U);
        TEST_CHECK(run, AreNamesEqual(CollisionSet_GetName(Set), "bounds") && (Shape->Type == CollisionShapeType_Box));
        // The box's half extents are the mesh's, scaled by 2; its centre is the mesh centre through the import.
        TEST_CHECK(run, IsNear(Shape->HalfExtents.x, 2.0f) && IsNear(Shape->HalfExtents.y, 2.0f)
            && IsNear(Shape->HalfExtents.z, 1.0f) && IsNear(Shape->Center.y, 2.0f));
        // The turn is the import's: the box's own X axis lies along the world's Z.
        Matrix Placed = CollisionShape_GetTransform(Shape);
        TEST_CHECK(run, IsNear(fabsf(Placed.m2), 1.0f));
        TEST_CHECK(run, (Collision->_layer == CollisionLayer_Prop) && !Collision->_hasCollidesWith);
    }
    if (User != ASSET_USER_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void EachCollisionRuleIsRefused(TestRun* run)
{
    static const RefusedCollision Cases[] =
    {
        { "3",
            ErrorCode_InvalidAssetDefinition, "collision must be \"bounds\"" },
        { "\"boundz\"",
            ErrorCode_InvalidAssetDefinition, "collision must be \"bounds\"" },
        { "{}",
            ErrorCode_InvalidAssetDefinition, "collision.sets must hold 1 to 32 sets" },
        { "{\"sets\": []}",
            ErrorCode_InvalidAssetDefinition, "collision.sets must hold 1 to 32 sets" },
        { "{\"sets\": [{\"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "collision.sets[0] needs a \"name\"" },
        { "{\"sets\": [{\"name\": \"Big\", \"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "is not a valid name" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}, {\"name\": \"a\", "
            "\"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "set names must be unique" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": []}]}",
            ErrorCode_InvalidAssetDefinition, "collision.sets[0].shapes must hold 1 to 128 shapes" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "needs a \"type\"" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"cone\"}]}]}",
            ErrorCode_InvalidAssetDefinition, "which is not a collision_shape_type" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\"}]}]}",
            ErrorCode_InvalidAssetDefinition, "collision.sets[0].shapes[0] needs a \"radius\"" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\", \"radius\": -1}]}]}",
            ErrorCode_InvalidAssetDefinition, "radius is -1; it must be above 0" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"capsule\", \"radius\": 1, \"half_height\": -1}]}]}",
            ErrorCode_InvalidAssetDefinition, "half_height is -1; it must be at least 0" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"cylinder\", \"radius\": 1, \"half_height\": 0}]}]}",
            ErrorCode_InvalidAssetDefinition, "half_height is 0; it must be above 0" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"box\", \"half_extents\": [1, 0, 1]}]}]}",
            ErrorCode_InvalidAssetDefinition, "half_extents must each be above 0" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"box\", \"radius\": 1, "
            "\"half_extents\": [1, 1, 1]}]}]}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"radius\"" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"convex_hull\"}]}]}",
            ErrorCode_InvalidAssetDefinition, "needs either \"points\" or \"model\"" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"convex_hull\", \"model\": \"cube_triangles\", "
            "\"points\": [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]]}]}]}",
            ErrorCode_InvalidAssetDefinition, "needs either \"points\" or \"model\"" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"convex_hull\", \"points\": [[0, 0, 0], [1, 0, 0], "
            "[0, 1, 0]]}]}]}",
            ErrorCode_InvalidAssetDefinition, "a hull needs 4 to 65536" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"convex_hull\", \"points\": [[0, 0, 0], [1, 0, 0], "
            "[0, 0, 1], [1, 0, 1]]}]}]}",
            ErrorCode_InvalidOperation, "span a volume" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"convex_hull\", \"model\": \"cube_triangles\", "
            "\"max_vertices\": 3}]}]}",
            ErrorCode_InvalidAssetDefinition, "max_vertices is 3; it must be 4 to 256" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"plane\"}]}]}",
            ErrorCode_InvalidAssetDefinition, "needs \"half_extents\": [x, z]" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"plane\", \"half_extents\": [1, 1], "
            "\"depth\": -2}]}]}",
            ErrorCode_InvalidAssetDefinition, "depth is -2; it must be at least 0" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"mesh\"}]}]}",
            ErrorCode_InvalidAssetDefinition, "needs a \"model\"" },
        { "{\"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\", \"radius\": 1, \"center\": [0, 1]}]}]}",
            ErrorCode_InvalidJSON, "collision.sets[0].shapes[0].center" },
        { "{\"sets\": [{\"name\": \"a\", \"size\": 3, \"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"size\"" },
        { "{\"layers\": 1, \"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"layers\"" },
        { "{\"default_set\": \"b\", \"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\", "
            "\"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "collision.default_set \"b\" names no set" },
        { "{\"layer\": \"ghost\", \"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "which is not a collision_layer" },
        { "{\"collides_with\": [\"world\", \"ghost\"], \"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": "
            "\"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "collision.collides_with[1] \"ghost\" is not a layer" },
        { "{\"mass\": 0, \"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "collision.mass is 0; it must be above 0 and at most 1e+06" },
        { "{\"mass\": 2e6, \"sets\": [{\"name\": \"a\", \"shapes\": [{\"type\": \"sphere\", \"radius\": 1}]}]}",
            ErrorCode_InvalidAssetDefinition, "collision.mass is 2e+06" },
    };
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    char Json[COLLIDER_TEST_JSON_CAPACITY];
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        FormatWorldModel(Json, sizeof(Json), "m", Cases[Index].Json);
        const char* const Fragments[] = { Cases[Index].Fragment, NULL };
        if (!TestText_CheckErrorMessage(run, BuildWorldModelDefinition(&Fixture, Json), Cases[Index].Code, Fragments))
        {
            printf("      (case %u: %s)\n", (unsigned)Index, Cases[Index].Json);
        }
    }

    // The limits: 33 sets, 129 shapes in a set.
    char Sets[COLLIDER_TEST_JSON_CAPACITY];
    size_t Length = (size_t)snprintf(Sets, sizeof(Sets), "{\"sets\": [");
    for (int Set = 0; Set < 33; Set++)
    {
        Length += (size_t)snprintf(Sets + Length, sizeof(Sets) - Length, "%s{\"name\": \"s%d\", \"shapes\": "
            "[{\"type\": \"sphere\", \"radius\": 1}]}", (Set == 0) ? "" : ", ", Set);
    }
    snprintf(Sets + Length, sizeof(Sets) - Length, "]}");
    FormatWorldModel(Json, sizeof(Json), "m", Sets);
    const char* const TooManySets[] = { "collision.sets must hold 1 to 32 sets", NULL };
    TestText_CheckErrorMessage(run, BuildWorldModelDefinition(&Fixture, Json), ErrorCode_InvalidAssetDefinition,
        TooManySets);
    Length = (size_t)snprintf(Sets, sizeof(Sets), "{\"sets\": [{\"name\": \"a\", \"shapes\": [");
    for (int Shape = 0; Shape < 129; Shape++)
    {
        Length += (size_t)snprintf(Sets + Length, sizeof(Sets) - Length, "%s{\"type\": \"sphere\", \"radius\": 1}",
            (Shape == 0) ? "" : ", ");
    }
    snprintf(Sets + Length, sizeof(Sets) - Length, "]}]}");
    FormatWorldModel(Json, sizeof(Json), "m", Sets);
    const char* const TooManyShapes[] = { "collision.sets[0].shapes must hold 1 to 128 shapes", NULL };
    TestText_CheckErrorMessage(run, BuildWorldModelDefinition(&Fixture, Json), ErrorCode_InvalidAssetDefinition,
        TooManyShapes);
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void AMissingCollisionModelFailsTheWorldModel(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Counters = { 0 };
    BoundingBox Box = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddModel(run, &Fixture, "cube_mesh", Box, &Counters)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"shop\", \"lods\": [{\"model\": \"cube_mesh\", "
            "\"max_distance\": 50}], \"collision\": {\"sets\": [{\"name\": \"walls\", \"shapes\": [{\"type\": "
            "\"mesh\", \"model\": \"no_such_model\"}]}]}}")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        WorldModel* Loaded = NULL;
        const char* const Fragments[] = { "World model \"shop\": collision set \"walls\", shape 0:", "no_such_model",
            NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"shop",
            User, &Loaded), ErrorCode_InvalidOperation, Fragments);
        TEST_CHECK(run, (Loaded == NULL) && (Counters.LoadCount == 1U) && (Counters.UnloadCount == 1U));
        // A model without vertices cannot give a mesh either.
        TEST_CHECK(run, TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"empty\", \"lods\": [{\"model\": "
            "\"cube_mesh\", \"max_distance\": 50}], \"collision\": {\"sets\": [{\"name\": \"walls\", \"shapes\": "
            "[{\"type\": \"mesh\", \"model\": \"cube_mesh\"}]}]}}"));
        TEST_CHECK_ERROR(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"empty", User,
            &Loaded), ErrorCode_InvalidAssetData);
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Static functions: the collider component.
static bool AddCollider(TestRun* run, World* world, WorldColliderComponent** outCollider)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    bool IsAdded = TEST_CHECK_SUCCESS(run, World_CreateObject(world, (const unsigned char*)u8"thing", &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_Collider,
            &Component));
    *outCollider = IsAdded ? WorldColliderComponent_FromComponent(Component) : NULL;
    return IsAdded && (*outCollider != NULL);
}

static void TheColliderStartsAtItsDefaults(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(WorldColliderComponent_GetPropertyTable()));
    TEST_CHECK_SUCCESS(run, WorldComponentTypeInfo_Check(WorldColliderComponent_GetTypeInfo()));
    World TestWorld;
    KeyRegistry Keys;
    WorldColliderComponent* Collider = NULL;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    if (AddCollider(run, &TestWorld, &Collider))
    {
        TEST_CHECK(run, WorldColliderComponent_IsEnabled(Collider) && !WorldColliderComponent_UsesOwnShape(Collider)
            && (WorldColliderComponent_GetCollisionSet(Collider) == NULL));
        TEST_CHECK(run, (WorldColliderComponent_GetLayer(Collider) == CollisionLayer_Prop)
            && (WorldColliderComponent_GetCollidesWith(Collider) == CollisionLayer_GetDefaultMask(CollisionLayer_Prop))
            && !WorldColliderComponent_IsTrigger(Collider));
        TEST_CHECK(run, (WorldColliderComponent_GetRotationMode(Collider) == WorldColliderRotationMode_Full)
            && Vector3Equals(WorldColliderComponent_GetScale(Collider), (Vector3){ 1.0f, 1.0f, 1.0f }));
        for (int Setting = 0; Setting < (int)WorldColliderSetting_Count; Setting++)
        {
            TEST_CHECK(run, !WorldColliderComponent_IsOverridden(Collider, (WorldColliderSetting)Setting));
        }
        CollisionShape Own = WorldColliderComponent_GetOwnShape(Collider);
        TEST_CHECK(run, (Own.Type == CollisionShapeType_Box) && IsNear(Own.HalfExtents.x, COLLISION_SHAPE_DEFAULT_SIZE)
            && Own.IsBounded && IsNear(Own.Depth, 0.0f));
        // T27: kinematic on a movable object (not pushable), 1 kg, stops when blocked, swept, no persisting contacts.
        TEST_CHECK(run, !WorldColliderComponent_IsPushable(Collider)
            && IsNear(WorldColliderComponent_GetMass(Collider), COLLISION_DEFAULT_MASS)
            && (WorldColliderComponent_GetWhenBlocked(Collider) == WorldColliderBlockedMode_Stop)
            && WorldColliderComponent_IsContinuous(Collider)
            && !WorldColliderComponent_ReportsPersistingContacts(Collider));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

static void TheColliderResolvesItsSettings(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    WorldColliderComponent* Collider = NULL;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    if (AddCollider(run, &TestWorld, &Collider))
    {
        // Without a world model's collision: prop, prop's mask, no trigger, no shapes.
        WorldColliderSettings Settings;
        WorldColliderComponent_ResolveSettings(Collider, NULL, &Settings);
        TEST_CHECK(run, !Settings.HasSet && !Settings.UsesOwnShape && (Settings.Layer == CollisionLayer_Prop)
            && (Settings.CollidesWith == CollisionLayer_GetDefaultMask(CollisionLayer_Prop)) && !Settings.IsTrigger);
        TEST_CHECK(run, IsNear(Settings.Mass, COLLISION_DEFAULT_MASS) && !Settings.IsPushable && Settings.IsContinuous
            && (Settings.WhenBlocked == WorldColliderBlockedMode_Stop) && !Settings.ReportsPersistingContacts);

        // A world model's defaults: its default set, layer, mask and trigger flag.
        CollisionSet Sets[2] =
        {
            { ._name = (const unsigned char*)u8"closed" }, { ._name = (const unsigned char*)u8"open" }
        };
        WorldModelCollision Collision = { ._sets = Sets, ._setCount = 2U, ._defaultSet = 1U,
            ._layer = CollisionLayer_World, ._hasCollidesWith = false, ._isTrigger = true, ._mass = 30.0f };
        WorldColliderComponent_ResolveSettings(Collider, &Collision, &Settings);
        TEST_CHECK(run, Settings.HasSet && (Settings.SetIndex == 1U) && (Settings.Layer == CollisionLayer_World)
            && (Settings.CollidesWith == CollisionLayer_GetDefaultMask(CollisionLayer_World)) && Settings.IsTrigger);
        TEST_CHECK(run, IsNear(Settings.Mass, 30.0f));
        Collision._hasCollidesWith = true;
        Collision._collidesWith = COLLISION_LAYER_BIT(CollisionLayer_Character);
        WorldColliderComponent_ResolveSettings(Collider, &Collision, &Settings);
        TEST_CHECK(run, Settings.CollidesWith == COLLISION_LAYER_BIT(CollisionLayer_Character));

        // The placement's overrides, each with its flag; a value kept while its flag is off changes nothing.
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollisionSet(Collider, (const unsigned char*)u8"closed"));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetLayer(Collider, CollisionLayer_Character));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollidesWith(Collider,
            COLLISION_LAYER_BIT(CollisionLayer_World)));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetIsTrigger(Collider, false));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(Collider, 4.0f));
        WorldColliderComponent_ResolveSettings(Collider, &Collision, &Settings);
        TEST_CHECK(run, (Settings.SetIndex == 1U) && (Settings.Layer == CollisionLayer_World) && Settings.IsTrigger);
        TEST_CHECK(run, IsNear(Settings.Mass, 30.0f));
        for (int Setting = 0; Setting < (int)WorldColliderSetting_Count; Setting++)
        {
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, (WorldColliderSetting)Setting,
                true));
        }
        WorldColliderComponent_ResolveSettings(Collider, &Collision, &Settings);
        TEST_CHECK(run, Settings.HasSet && (Settings.SetIndex == 0U) && (Settings.Layer == CollisionLayer_Character)
            && (Settings.CollidesWith == COLLISION_LAYER_BIT(CollisionLayer_World)) && !Settings.IsTrigger);
        TEST_CHECK(run, IsNear(Settings.Mass, 4.0f));

        // The motion rows pass through as they are.
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetPushable(Collider, true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetWhenBlocked(Collider, WorldColliderBlockedMode_PushThrough));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetContinuous(Collider, false));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetReportsPersistingContacts(Collider, true));
        WorldColliderComponent_ResolveSettings(Collider, &Collision, &Settings);
        TEST_CHECK(run, Settings.IsPushable && (Settings.WhenBlocked == WorldColliderBlockedMode_PushThrough)
            && !Settings.IsContinuous && Settings.ReportsPersistingContacts);

        // A set the world model lacks: no shapes, and worth a warning.
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollisionSet(Collider, (const unsigned char*)u8"ajar"));
        WorldColliderComponent_ResolveSettings(Collider, &Collision, &Settings);
        TEST_CHECK(run, !Settings.HasSet && Settings.IsSetMissing);

        // The own shape wins over the sets.
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
        WorldColliderComponent_ResolveSettings(Collider, &Collision, &Settings);
        TEST_CHECK(run, Settings.UsesOwnShape && !Settings.HasSet && !Settings.IsSetMissing);

        // The mask by layer follows the layer in effect.
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_CollidesWith,
            false));
        Collision._hasCollidesWith = false;
        WorldColliderComponent_ResolveSettings(Collider, &Collision, &Settings);
        TEST_CHECK(run, Settings.CollidesWith == CollisionLayer_GetDefaultMask(CollisionLayer_Character));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

static void TheColliderSettersCheckTheirValues(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    WorldColliderComponent* Collider = NULL;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    if (AddCollider(run, &TestWorld, &Collider))
    {
        uint32_t Revision = WorldComponent_GetRevision(&Collider->Base);
        CollisionShape Ball = CollisionShape_Create(CollisionShapeType_Sphere);
        Ball.Radius = 0.25f;
        Ball.Center = (Vector3){ 0.0f, 1.0f, 0.0f };
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Ball));
        CollisionShape Own = WorldColliderComponent_GetOwnShape(Collider);
        TEST_CHECK(run, (Own.Type == CollisionShapeType_Sphere) && IsNear(Own.Radius, 0.25f)
            && IsNear(Own.Center.y, 1.0f) && (WorldComponent_GetRevision(&Collider->Base) != Revision));

        CollisionShape Hull = CollisionShape_Create(CollisionShapeType_ConvexHull);
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetOwnShape(Collider, &Hull), ErrorCode_ArgumentOutOfRange);
        CollisionShape Bad = Ball;
        Bad.Radius = -1.0f;
        Bad.Center = (Vector3){ 5.0f, 5.0f, 5.0f };
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetOwnShape(Collider, &Bad), ErrorCode_ArgumentOutOfRange);
        Own = WorldColliderComponent_GetOwnShape(Collider);
        TEST_CHECK(run, IsNear(Own.Radius, 0.25f) && IsNear(Own.Center.x, 0.0f)); // all or nothing

        TEST_CHECK_ERROR(run, WorldColliderComponent_SetLayer(Collider, (CollisionLayer)99),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetCollidesWith(Collider, 1U << 20U),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetLocalTransform(Collider, (Vector3){ 0.0f, 0.0f, 0.0f },
            (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 1.0f }), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetRotationMode(Collider, (WorldColliderRotationMode)7),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_Count, true),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetMass(Collider, 0.0f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetMass(Collider, 2.0e6f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetMass(Collider, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, IsNear(WorldColliderComponent_GetMass(Collider), COLLISION_DEFAULT_MASS));
        TEST_CHECK_ERROR(run, WorldColliderComponent_SetWhenBlocked(Collider, (WorldColliderBlockedMode)5),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(Collider, COLLISION_MAX_MASS));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetLocalTransform(Collider, (Vector3){ 0.0f, 0.5f, 0.0f },
            (Vector3){ 0.0f, PI * 0.5f, 0.0f }, (Vector3){ 2.0f, 1.0f, 1.0f }));
        Matrix4Double Local = WorldColliderComponent_GetLocalTransform(Collider);
        Vector3Double Moved = Matrix4Double_TransformPoint(&Local, Vector3Double_Create(1.0, 0.0, 0.0));
        // Scale first (x 2), then the turn (X to -Z), then the offset.
        TEST_CHECK(run, IsNear((float)Moved.X, 0.0f) && IsNear((float)Moved.Y, 0.5f) && IsNear((float)Moved.Z, -2.0f));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

static void TheColliderSavesThroughItsTable(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    WorldColliderComponent* Collider = NULL;
    JSONObjectPool* Pool = NULL;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    if (AddCollider(run, &TestWorld, &Collider) && TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        CollisionShape Plane = CollisionShape_Create(CollisionShapeType_Plane);
        Plane.Rotation = (Vector3){ PI * 0.5f, 0.0f, 0.0f };
        Plane.PlaneHalfExtents = (Vector2){ 3.0f, 4.0f };
        Plane.Depth = 0.25f;
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Plane));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_CollidesWith,
            true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollidesWith(Collider,
            COLLISION_LAYER_BIT(CollisionLayer_Character) | COLLISION_LAYER_BIT(CollisionLayer_World)));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetRotationMode(Collider, WorldColliderRotationMode_YawOnly));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetPushable(Collider, true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_Mass, true));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(Collider, 12.5f));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetWhenBlocked(Collider, WorldColliderBlockedMode_PushThrough));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetContinuous(Collider, false));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetReportsPersistingContacts(Collider, true));
        const PropertyTable* Table = WorldColliderComponent_GetPropertyTable();
        JSONCompound* Compound = NULL;
        if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound)))
        {
            TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, Collider, Pool, Compound, NULL));
            JSONObjectValue Value;
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"shape_type",
                JSONValueType_String, &Value)) && Memory_IsEqual(Value.Value.String->_data, u8"plane", 5U));
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"collides_with",
                JSONValueType_Array, &Value)) && (JSONArray_GetElementCount(Value.Value.Array) == 2U));
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"rotation_mode",
                JSONValueType_String, &Value)) && Memory_IsEqual(Value.Value.String->_data, u8"yaw_only", 8U));
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"when_blocked",
                JSONValueType_String, &Value)) && Memory_IsEqual(Value.Value.String->_data, u8"push_through", 12U));
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"is_pushable",
                JSONValueType_Boolean, &Value)) && Value.Value.Boolean);
            WorldColliderComponent* Copy = NULL;
            if (AddCollider(run, &TestWorld, &Copy))
            {
                TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Compound, Copy, NULL));
                TEST_CHECK(run, Properties_Equals(Table, Collider, Copy));
                CollisionShape Read = WorldColliderComponent_GetOwnShape(Copy);
                TEST_CHECK(run, IsNear(Read.Rotation.x, PI * 0.5f) && IsNear(Read.PlaneHalfExtents.y, 4.0f));
            }
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
        }

        // GHDF: by wire ids, the angles in radians.
        GHDFObjectPool* BinaryPool = NULL;
        GHDFCompound* Binary = NULL;
        WorldColliderComponent* BinaryCopy = NULL;
        if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&BinaryPool))
            && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(BinaryPool, &Binary))
            && TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(Table, Collider, BinaryPool, Binary, NULL))
            && AddCollider(run, &TestWorld, &BinaryCopy))
        {
            TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(Table, Binary, BinaryCopy, NULL));
            TEST_CHECK(run, Properties_Equals(Table, Collider, BinaryCopy));
        }
        if (Binary != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(BinaryPool, Binary, true));
        }
        if (BinaryPool != NULL)
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(BinaryPool));
        }
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}


// Public functions.
void WorldColliderTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_collision_section_reads_every_key", TheCollisionSectionReadsEveryKey },
        { "the_bounds_fallback_is_a_box_around_lod_0", TheBoundsFallbackIsABoxAroundLod0 },
        { "each_collision_rule_is_refused", EachCollisionRuleIsRefused },
        { "a_missing_collision_model_fails_the_world_model", AMissingCollisionModelFailsTheWorldModel },
        { "the_collider_starts_at_its_defaults", TheColliderStartsAtItsDefaults },
        { "the_collider_resolves_its_settings", TheColliderResolvesItsSettings },
        { "the_collider_setters_check_their_values", TheColliderSettersCheckTheirValues },
        { "the_collider_saves_through_its_table", TheColliderSavesThroughItsTable },
    };
    TestRun_RunSuite(run, "WorldCollider", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
