#include "TestSuites.h"
#include "TestAssets.h"
#include "TestTerrain.h"
#include "TestWorld.h"
#include "CollisionLayer.h"
#include "Logger.h"
#include "TerrainAsset.h"
#include "World.h"
#include "WorldCharacterComponent.h"
#include "WorldCharacterInternal.h"
#include "WorldCharacters.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldObject.h"
#include "WorldTerrainComponent.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The game's tick (480 Hz). */
#define TEST_DELTA (1.0f / 480.0f)
#define TICKS_PER_SECOND 480U
/** Room for a path, and for the log text. */
#define TERRAIN_COLLISION_TEST_TEXT_CAPACITY 256
/** The ticks of the walk across the seams. */
#define SEAM_WALK_TICKS (20U * TICKS_PER_SECOND)


// Types.
/* A world with the fixture's assets, its collision world and its characters. */
typedef struct TerrainSceneStruct
{
    TestAssetFixture Fixture;
    World World;
    KeyRegistry Keys;
    WorldCollision* Collision;
    WorldCharacters* Characters;
    bool IsWorldBuilt;
} TerrainScene;


// Static functions: helpers.
static bool IsNear(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

/* A plane rising 1 m in 4 along X and falling 1 m in 8 along Z: exact on the position grid at whole metres. */
static float Tilt(float x, float z)
{
    return (0.25f * x) - (0.125f * z);
}

/* Flat for x below 0, then rising at 30 degrees (or 60) along X. */
static float Gentle(float x, float z)
{
    (void)z;
    return (x > 0.0f) ? (x * tanf(30.0f * DEG2RAD)) : 0.0f;
}

static float Steep(float x, float z)
{
    (void)z;
    return (x > 0.0f) ? (x * tanf(60.0f * DEG2RAD)) : 0.0f;
}

static uint32_t NextRandom(uint32_t* state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return *state >> 8U;
}

static float RandomRange(uint32_t* state, float minimum, float maximum)
{
    return minimum + ((maximum - minimum) * ((float)NextRandom(state) / (float)(1U << 24U)));
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Found = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, TERRAIN_COLLISION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, TERRAIN_COLLISION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
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


// Static functions: scenes.
static bool Scene_Construct(TestRun* run, TerrainScene* self, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    if (!TestAssetFixture_Construct(run, &self->Fixture))
    {
        return false;
    }
    self->IsWorldBuilt = TestWorld_Construct(run, &self->World, &self->Keys, self->Fixture.Manager, logger);
    return self->IsWorldBuilt
        && TEST_CHECK_SUCCESS(run, WorldCollision_Create(&self->World, self->Fixture.Manager, logger, NULL,
            &self->Collision))
        && TEST_CHECK_SUCCESS(run, WorldCharacters_Create(&self->World, self->Collision, NULL, &self->Characters));
}

static void Scene_Deconstruct(TestRun* run, TerrainScene* self)
{
    WorldCharacters_Destroy(self->Characters);
    WorldCollision_Destroy(self->Collision);
    if (self->IsWorldBuilt)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Fixture);
}

static WorldObject* GetObject(TerrainScene* scene, uint64_t id)
{
    WorldObject* Object = NULL;
    World_TryGetObject(&scene->World, id, &Object);
    return Object;
}

/* Registers the terrain @p name built from @p grid (one material, no channels) in @p chunkSize chunks; returns its mesh
 * (a copy of the asset's: the same chunks), or NULL. */
static TerrainMesh* AddTerrainAsset(TestRun* run, TerrainScene* scene, const char* name, const TestTerrainGrid* grid,
    float chunkSize)
{
    TerrainMesh* Ground = TestTerrain_CreateMesh(run, grid, 1U, 0U, chunkSize);
    if ((Ground != NULL) && !TestAssets_AddTerrain(run, &scene->Fixture, name, Ground, NULL, NULL))
    {
        TerrainMesh_Destroy(Ground);
        return NULL;
    }
    return Ground;
}

/* A static object at @p position of the scene (TestWorld_At: shifted in the far run) placing the terrain @p name. */
static uint64_t AddTerrainObject(TestRun* run, TerrainScene* scene, Vector3 position, const char* name)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"ground", &Object)))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Object);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(position)));
    TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Object, WorldMobility_Static));
    if (TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_Terrain, &Component)))
    {
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetTerrain(WorldTerrainComponent_FromComponent(Component),
            (const unsigned char*)name));
    }
    return Id;
}

static WorldTerrainComponent* GetTerrain(TerrainScene* scene, uint64_t id)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&scene->World, id, WorldComponentType_Terrain, &Component)
        ? WorldTerrainComponent_FromComponent(Component) : NULL;
}

/* The distance of a ray's nearest hit from @p origin of the scene, or -1 when it hits nothing. */
static float CastRay(TerrainScene* scene, Vector3 origin, Vector3 direction, const WorldCollisionFilter* filter,
    WorldRayHit* outHit)
{
    WorldRayHit Hit;
    if (!WorldCollision_Raycast(scene->Collision, TestWorld_At(origin), direction, 100.0f, filter, &Hit))
    {
        return -1.0f;
    }
    if (outHit != NULL)
    {
        *outHit = Hit;
    }
    return Hit.Distance;
}

/* The height of the nearest hit below (x, 30, z) of the scene, or -100 when nothing is there. */
static float GetHeightBelow(TerrainScene* scene, float x, float z)
{
    WorldRayHit Hit;
    return (CastRay(scene, (Vector3){ x, 30.0f, z }, (Vector3){ 0.0f, -1.0f, 0.0f }, NULL, &Hit) < 0.0f) ? -100.0f
        : TestWorld_ToScene(Hit.Point).y;
}

/* A character (the default settings) with its feet at @p feet of the scene; syncs the collision world first. */
static uint64_t AddCharacter(TestRun* run, TerrainScene* scene, Vector3 feet)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"walker", &Object)))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Object);
    if (TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_Character, &Component)))
    {
        WorldCollision_Sync(scene->Collision);
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(scene->Characters, Id, TestWorld_At(feet), 0.0f));
    }
    return Id;
}

static WorldCharacterComponent* GetCharacter(TerrainScene* scene, uint64_t id)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&scene->World, id, WorldComponentType_Character, &Component)
        ? WorldCharacterComponent_FromComponent(Component) : NULL;
}

static void Walk(TestRun* run, TerrainScene* scene, uint64_t id, Vector3 direction)
{
    CharacterInput Input = CharacterInput_CreateDefault();
    Input.MoveDirection = direction;
    TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(GetCharacter(scene, id), &Input));
}

static Vector3 GetFeet(TerrainScene* scene, uint64_t id)
{
    return TestWorld_ToScene(WorldObject_GetWorldPosition(GetObject(scene, id)));
}

static bool IsGrounded(TerrainScene* scene, uint64_t id)
{
    return WorldCharacterComponent_GetState(GetCharacter(scene, id))->IsGrounded;
}

/* One tick in the game's order: the characters (the drivers), the collision step, the world matrices. */
static void Tick(TerrainScene* scene)
{
    WorldCharacters_Update(scene->Characters, TEST_DELTA);
    WorldCollision_Step(scene->Collision, TEST_DELTA);
    World_UpdateTransforms(&scene->World);
}

static void TickFor(TerrainScene* scene, uint32_t ticks)
{
    for (uint32_t Index = 0U; Index < ticks; Index++)
    {
        Tick(scene);
    }
}


// Tests: queries.
/* A tilted 24 m field of 1 m quads in 8 m chunks, placed at (5, 2, -3): rays down hit it where the plane is, each hit
 * naming the terrain object, its chunk (the shape and the part) and the terrain's triangle, whose corners the hit's
 * weights rebuild; a ray passes through the hole, and through the field from below (it collides from its front only); a
 * capsule cast lands on it and names the same. */
static void RaysHitTheTerrainWhereItIsAndPassThroughAHole(TestRun* run)
{
    TerrainScene Scene;
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 24U, 24U, 1.0f, -12.0f, -12.0f, Tilt);
    Grid.Flags[TestTerrain_Triangle(&Grid, 15U, 15U)] = TERRAIN_TRIANGLE_FLAG_HOLE;
    TerrainMesh* Ground = NULL;
    uint64_t Id = 0U;
    if (Scene_Construct(run, &Scene, NULL) && ((Ground = AddTerrainAsset(run, &Scene, "field", &Grid, 8.0f)) != NULL))
    {
        Id = AddTerrainObject(run, &Scene, (Vector3){ 5.0f, 2.0f, -3.0f }, "field");
    }
    if (Id != 0U)
    {
        WorldCollision_Sync(Scene.Collision);
        WorldCollisionStats Stats = WorldCollision_GetStats(Scene.Collision);
        TEST_CHECK(run, (Stats.InstanceCount == 1U) && (Stats.StaticInstanceCount == 1U) && (Stats.ShapeCount == 16U)
            && (Stats.TriangleCount == (Grid.TriangleCount - 1U)) && (Stats.TerrainChunkBuildCount == 16U));
        Vector3 Up = Vector3Normalize((Vector3){ -0.25f, 1.0f, 0.125f });
        uint32_t State = 12345U;
        uint32_t Checked = 0U;
        for (uint32_t Shot = 0U; Shot < 300U; Shot++)
        {
            float X = RandomRange(&State, -11.9f, 11.9f);
            float Z = RandomRange(&State, -11.9f, 11.9f);
            float U = (X + 12.0f) - floorf(X + 12.0f);
            float V = (Z + 12.0f) - floorf(Z + 12.0f);
            uint32_t I = (uint32_t)floorf(X + 12.0f);
            uint32_t J = (uint32_t)floorf(Z + 12.0f);
            if ((fabsf(U + V - 1.0f) < 0.01f) || ((I == 15U) && (J == 15U)))
            {
                continue; // on a quad's diagonal (either triangle may answer), or in the holed quad
            }
            uint32_t Expected = TestTerrain_Triangle(&Grid, I, J) + (((U + V) > 1.0f) ? 1U : 0U);
            WorldRayHit Hit;
            float Distance = CastRay(&Scene, (Vector3){ 5.0f + X, 20.0f, -3.0f + Z }, (Vector3){ 0.0f, -1.0f, 0.0f },
                NULL, &Hit);
            float Height = 2.0f + Tilt(X, Z);
            uint32_t Chunk = TerrainMesh_GetTriangleChunk(Ground, Expected);
            bool IsRight = IsNear(Distance, 20.0f - Height, 1e-4f) && (Hit.Shape.ObjectId == Id)
                && Hit.Shape.HasTriangle && (Hit.Shape.ShapeType == CollisionShapeType_Mesh)
                && (Hit.Shape.Layer == CollisionLayer_World) && (Hit.Shape.SourceTriangle == Expected)
                && (Hit.Shape.ShapeIndex == Chunk) && (Hit.Shape.MeshPart == Chunk)
                && (Vector3DotProduct(Hit.Normal, Up) > 0.99999f);
            // The weights of the terrain triangle's corners at the hit give the hit point back.
            const TerrainTriangle* Triangle = TerrainMesh_GetTriangle(Ground, Expected);
            Vector3 Weights = Hit.Shape.Barycentric;
            Vector3 Rebuilt = { 5.0f, 2.0f, -3.0f };
            for (int Corner = 0; Corner < 3; Corner++)
            {
                float Weight = (Corner == 0) ? Weights.x : ((Corner == 1) ? Weights.y : Weights.z);
                Rebuilt = Vector3Add(Rebuilt, Vector3Scale(TerrainMesh_GetPosition(Ground, Triangle->_indices[Corner]),
                    Weight));
            }
            IsRight = IsRight && (Vector3Distance(Rebuilt, TestWorld_ToScene(Hit.Point)) < 1e-4f);
            if (!TEST_CHECK(run, IsRight))
            {
                printf("      (ray %u at %g, %g: distance %g, triangle %u of %u, chunk %u)\n", Shot, (double)X, (double)Z,
                    (double)Distance, Hit.Shape.SourceTriangle, Expected, Hit.Shape.ShapeIndex);
                break;
            }
            Checked++;
        }
        TEST_CHECK(run, Checked > 250U);
        // The hole (quad (15, 15)'s first triangle) lets a ray through, its other triangle does not; from below, the
        // field lets every ray through.
        TEST_CHECK(run, CastRay(&Scene, (Vector3){ 5.0f + 3.25f, 20.0f, -3.0f + 3.25f }, (Vector3){ 0.0f, -1.0f, 0.0f },
            NULL, NULL) < 0.0f);
        TEST_CHECK(run, IsNear(CastRay(&Scene, (Vector3){ 5.0f + 3.75f, 20.0f, -3.0f + 3.75f },
            (Vector3){ 0.0f, -1.0f, 0.0f }, NULL, NULL), 18.0f - Tilt(3.75f, 3.75f), 1e-4f));
        TEST_CHECK(run, CastRay(&Scene, (Vector3){ 5.3f, -20.0f, -3.4f }, (Vector3){ 0.0f, 1.0f, 0.0f }, NULL, NULL)
            < 0.0f);
        // A capsule cast down lands on it: the same chunk and terrain triangle as a ray down its axis would find.
        WorldCastHit Cast;
        Vector3 Start = { 5.0f + 4.4f, 10.0f, -3.0f - 6.3f };
        if (TEST_CHECK(run, WorldCollision_SweepCapsule(Scene.Collision, TestWorld_At(Start),
            TestWorld_At((Vector3){ Start.x, -10.0f, Start.z }), 0.3f, 1.8f, NULL, &Cast)))
        {
            uint32_t Triangle = Cast.Shape.SourceTriangle;
            TEST_CHECK(run, Cast.Shape.HasTriangle && (Cast.Shape.ObjectId == Id) && (Triangle < Grid.TriangleCount)
                && (Cast.Shape.ShapeIndex == TerrainMesh_GetTriangleChunk(Ground, Triangle))
                && (Cast.Shape.MeshPart == Cast.Shape.ShapeIndex)
                && (Vector3DotProduct(Cast.Normal, Up) > 0.9999f));
        }
    }
    TerrainMesh_Destroy(Ground);
    Scene_Deconstruct(run, &Scene);
    TestTerrain_FreeGrid(&Grid);
}

/* A terrain object turned a quarter about Y and scaled twice along X puts every chunk where it puts the terrain: rays hit
 * the tilted plane at the place and height the object's transform gives (its chunk centres turned and scaled with it). */
static void ATurnedAndScaledTerrainIsWhereItsObjectPutsIt(TestRun* run)
{
    TerrainScene Scene;
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 24U, 24U, 1.0f, -12.0f, -12.0f, Tilt);
    TerrainMesh* Ground = NULL;
    uint64_t Id = 0U;
    if (Scene_Construct(run, &Scene, NULL) && ((Ground = AddTerrainAsset(run, &Scene, "field", &Grid, 8.0f)) != NULL))
    {
        Id = AddTerrainObject(run, &Scene, (Vector3){ -4.0f, 1.0f, 6.0f }, "field");
    }
    WorldObject* Object = (Id != 0U) ? GetObject(&Scene, Id) : NULL;
    if ((Object != NULL) && TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Object, (Vector3){ 0.0f, PI / 2.0f, 0.0f }))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Object, (Vector3){ 2.0f, 1.0f, 1.0f })))
    {
        WorldCollision_Sync(Scene.Collision);
        uint32_t State = 777U;
        bool AreRight = true;
        for (uint32_t Shot = 0U; AreRight && (Shot < 100U); Shot++)
        {
            // A point of the terrain's space, and where the object puts it: scaled along X, then turned (+X to -Z).
            float X = RandomRange(&State, -11.0f, 11.0f);
            float Z = RandomRange(&State, -11.0f, 11.0f);
            Vector3 Placed = { -4.0f + Z, 1.0f + Tilt(X, Z), 6.0f - (2.0f * X) };
            AreRight = IsNear(GetHeightBelow(&Scene, Placed.x, Placed.z), Placed.y, 1e-3f);
            if (!AreRight)
            {
                printf("      (terrain point %g, %g: height %g below, %g expected)\n", (double)X, (double)Z,
                    (double)GetHeightBelow(&Scene, Placed.x, Placed.z), (double)Placed.y);
            }
        }
        TEST_CHECK(run, AreRight);
    }
    TerrainMesh_Destroy(Ground);
    Scene_Deconstruct(run, &Scene);
    TestTerrain_FreeGrid(&Grid);
}


// Tests: seams and slopes.
/* A flat 48 m field in 8 m chunks: a ball sunk 5 cm into it just past a seam touches the chunk beyond the seam with
 * the face's normal, never the seam edge's slanted one (each chunk's border edges have the activity the whole terrain
 * gives them); and a character walking across four seams goes exactly as it goes on a box floor, tick by tick. */
static void ACapsuleSlidesAcrossChunkSeamsWithoutABump(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 48U, 48U, 1.0f, -24.0f, -24.0f, NULL);
    static Vector3 OnBox[SEAM_WALK_TICKS];
    static Vector3 OnTerrain[SEAM_WALK_TICKS];
    for (int Floor = 0; Floor < 2; Floor++)
    {
        bool IsTerrain = Floor == 1;
        Vector3* Feet = IsTerrain ? OnTerrain : OnBox;
        TerrainScene Scene;
        TerrainMesh* Ground = NULL;
        bool IsBuilt = Scene_Construct(run, &Scene, NULL);
        if (IsBuilt && IsTerrain)
        {
            Ground = AddTerrainAsset(run, &Scene, "field", &Grid, 8.0f);
            IsBuilt = (Ground != NULL) && (AddTerrainObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, "field") != 0U);
        }
        else if (IsBuilt)
        {
            // The same floor as a box: its top at y = 0.
            WorldObject* Object = NULL;
            WorldComponent* Component = NULL;
            IsBuilt = TEST_CHECK_SUCCESS(run, World_CreateObject(&Scene.World, NULL, &Object))
                && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At((Vector3){ 0.0f, -0.5f, 0.0f })))
                && TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Object, WorldMobility_Static))
                && TEST_CHECK_SUCCESS(run, World_AddComponent(&Scene.World, WorldObject_GetId(Object),
                    WorldComponentType_Collider, &Component));
            if (IsBuilt)
            {
                CollisionShape Box = CollisionShape_Create(CollisionShapeType_Box);
                Box.HalfExtents = (Vector3){ 24.0f, 0.5f, 24.0f };
                WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
                TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Box));
                TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
            }
        }
        if (IsBuilt && IsTerrain)
        {
            WorldCollision_Sync(Scene.Collision);
            GenericBuffer Manifolds;
            GenericBuffer_AllocateVariable(&Manifolds, 4U, sizeof(WorldContactManifold));
            CollisionShape Ball = CollisionShape_Create(CollisionShapeType_Sphere);
            Ball.Radius = 0.3f;
            for (int Seam = -16; Seam <= 16; Seam += 8)
            {
                for (int Axis = 0; Axis < 2; Axis++)
                {
                    float Past = (float)Seam + 0.1f;
                    Vector3 Center = (Axis == 0) ? (Vector3){ Past, 0.25f, 0.5f } : (Vector3){ 0.5f, 0.25f, Past };
                    GenericBuffer_Clear(&Manifolds);
                    size_t Found = WorldCollision_CollideShape(Scene.Collision, &Ball, 1U, TestWorld_At(Center),
                        QuaternionIdentity(), 0.0f, NULL, &Manifolds);
                    const WorldContactManifold* Contacts = (const WorldContactManifold*)(const void*)Manifolds._data;
                    bool AreFlat = Found > 0U;
                    for (size_t Index = 0U; Index < Manifolds._count; Index++)
                    {
                        AreFlat = AreFlat && (Contacts[Index].Normal.y < -0.9999f)
                            && IsNear(Contacts[Index].Separation, -0.05f, 1e-4f);
                    }
                    if (!TEST_CHECK(run, AreFlat))
                    {
                        printf("      (seam %d along %s: %u contacts)\n", Seam, (Axis == 0) ? "x" : "z",
                            (unsigned int)Found);
                    }
                }
            }
            Memory_Free(Manifolds._data);
        }
        if (IsBuilt)
        {
            uint64_t Walker = AddCharacter(run, &Scene, (Vector3){ -22.5f, 0.0f, -22.37f });
            TickFor(&Scene, 20U);
            Walk(run, &Scene, Walker, Vector3Normalize((Vector3){ 1.0f, 0.0f, 0.8f }));
            bool IsAlwaysGrounded = true;
            for (uint32_t Index = 0U; Index < SEAM_WALK_TICKS; Index++)
            {
                Tick(&Scene);
                Feet[Index] = GetFeet(&Scene, Walker);
                IsAlwaysGrounded = IsAlwaysGrounded && IsGrounded(&Scene, Walker);
            }
            TEST_CHECK(run, IsAlwaysGrounded);
        }
        TerrainMesh_Destroy(Ground);
        Scene_Deconstruct(run, &Scene);
    }
    // The walk crossed four seams (x = -16 and -8, and z = -16 and -8): it went exactly as on the box.
    float Rest = CHARACTER_SKIN + CHARACTER_REST_GAP;
    float WorstStep = 0.0f;
    float WorstHeight = 0.0f;
    for (uint32_t Index = 0U; Index < SEAM_WALK_TICKS; Index++)
    {
        WorstStep = fmaxf(WorstStep, Vector3Distance(OnBox[Index], OnTerrain[Index]));
        WorstHeight = fmaxf(WorstHeight, fabsf(OnTerrain[Index].y - Rest));
    }
    TEST_CHECK(run, (OnTerrain[SEAM_WALK_TICKS - 1U].x > -7.0f) && (OnTerrain[SEAM_WALK_TICKS - 1U].z > -7.0f)
        && (WorstStep < 1e-4f) && (WorstHeight < 1e-4f));
    if ((WorstStep >= 1e-4f) || (WorstHeight >= 1e-4f))
    {
        printf("      (worst difference from the box %g m, worst height off the rest %g m; ended at %g, %g)\n",
            (double)WorstStep, (double)WorstHeight, (double)OnTerrain[SEAM_WALK_TICKS - 1U].x,
            (double)OnTerrain[SEAM_WALK_TICKS - 1U].z);
    }
    TestTerrain_FreeGrid(&Grid);
}

/* Walks a character from (-2, 0, 0) along +X for @p seconds onto a terrain that is flat below x = 0 and rises at
 * @p height's slope beyond; returns its feet and whether it ends on the ground. A drop at @p dropX (when above 0) is
 * made after the walk, onto the slope, with no input for three seconds. */
static Vector3 WalkOntoASlope(TestRun* run, TestTerrainHeightFunction height, uint32_t seconds, float dropX,
    bool* outIsGrounded)
{
    Vector3 Feet = { 0 };
    *outIsGrounded = false;
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 32U, 16U, 0.5f, -8.0f, -4.0f, height);
    TerrainScene Scene;
    TerrainMesh* Ground = NULL;
    if (Scene_Construct(run, &Scene, NULL) && ((Ground = AddTerrainAsset(run, &Scene, "slope", &Grid, 8.0f)) != NULL)
        && (AddTerrainObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, "slope") != 0U))
    {
        uint64_t Walker = AddCharacter(run, &Scene, (Vector3){ -2.0f, 0.0f, 0.0f });
        Walk(run, &Scene, Walker, (Vector3){ 1.0f, 0.0f, 0.0f });
        TickFor(&Scene, seconds * TICKS_PER_SECOND);
        if (dropX > 0.0f)
        {
            TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Walker,
                TestWorld_At((Vector3){ dropX, height(dropX, 0.0f) + 0.5f, 0.0f }), 0.0f));
            Walk(run, &Scene, Walker, (Vector3){ 0.0f, 0.0f, 0.0f });
            TickFor(&Scene, 3U * TICKS_PER_SECOND);
        }
        Feet = GetFeet(&Scene, Walker);
        *outIsGrounded = IsGrounded(&Scene, Walker);
    }
    TerrainMesh_Destroy(Ground);
    Scene_Deconstruct(run, &Scene);
    TestTerrain_FreeGrid(&Grid);
    return Feet;
}

/* The slope limit holds on terrain as on any mesh: a 30 degree slope is walked (the feet as high above it as the
 * round bottom puts them on a ramp), a 60 degree one is not, and dropped onto it a character slides down to the flat. */
static void ACharacterWalksAGentleSlopeAndSlidesOffASteepOne(TestRun* run)
{
    bool IsOnGround = false;
    Vector3 Feet = WalkOntoASlope(run, Gentle, 3U, 0.0f, &IsOnGround);
    float Angle = 30.0f * DEG2RAD;
    float Expected = (Feet.x * tanf(Angle)) + (CHARACTER_DEFAULT_RADIUS * ((1.0f / cosf(Angle)) - 1.0f));
    TEST_CHECK(run, IsOnGround && (Feet.x > 1.5f) && IsNear(Feet.y, Expected, 0.005f));

    Feet = WalkOntoASlope(run, Steep, 2U, 0.0f, &IsOnGround);
    TEST_CHECK(run, (Feet.x < 0.5f) && (Feet.y < 0.05f));
    Feet = WalkOntoASlope(run, Steep, 1U, 2.0f, &IsOnGround);
    TEST_CHECK(run, IsOnGround && (Feet.x < 0.5f) && (Feet.y < 0.01f));
}


// Tests: keeping in step.
/* A 32 m field in 8 m chunks: a vertex moved by the editor rebuilds its one chunk's collision mesh and nothing else; a
 * moved object only moves its shapes; a new layer, "collides" off and on again and a hole rebuild no other chunk; a new
 * terrain is cut afresh; a removed component leaves nothing. */
static void OnlyChangedChunksAreRebuilt(TestRun* run)
{
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 32U, 32U, 1.0f, -16.0f, -16.0f, NULL);
    TestTerrainGrid Small;
    TestTerrain_BuildGrid(&Small, 8U, 8U, 1.0f, -4.0f, -4.0f, NULL);
    TerrainScene Scene;
    TerrainMesh* Copy = NULL;
    TerrainMesh* SmallMesh = NULL;
    uint64_t Id = 0U;
    AssetUserID User = ASSET_USER_ID_INVALID;
    TerrainAsset* Shared = NULL;
    if (Scene_Construct(run, &Scene, NULL) && ((Copy = AddTerrainAsset(run, &Scene, "field", &Grid, 8.0f)) != NULL)
        && ((SmallMesh = AddTerrainAsset(run, &Scene, "patch", &Small, 8.0f)) != NULL)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Scene.Fixture.Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadTerrain(Scene.Fixture.Manager, (const unsigned char*)u8"field", User,
            &Shared)))
    {
        Id = AddTerrainObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, "field");
    }
    WorldTerrainComponent* Placement = (Id != 0U) ? GetTerrain(&Scene, Id) : NULL;
    if (Placement != NULL)
    {
        WorldCollision_Sync(Scene.Collision);
        WorldCollisionStats Before = WorldCollision_GetStats(Scene.Collision);
        TEST_CHECK(run, (Before.ShapeCount == 16U) && (Before.TerrainChunkBuildCount == 16U));
        WorldCollision_Sync(Scene.Collision);
        TEST_CHECK(run, WorldCollision_GetStats(Scene.Collision).TerrainChunkBuildCount == 16U);

        // The editor raises the vertex at (4, 4), inside the chunk of cell (0, 0): that chunk alone.
        TerrainMesh* Ground = TerrainAsset_GetMesh(Shared);
        uint32_t Vertex = TestTerrain_Vertex(&Grid, 20U, 20U);
        Vector3 Raised = { 4.0f, 1.0f, 4.0f };
        TEST_CHECK_SUCCESS(run, TerrainMesh_SetVertexPositions(Ground, &Vertex, &Raised, 1U));
        TEST_CHECK(run, TerrainMesh_RebuildDirtyChunks(Ground) == 1U);
        WorldCollision_Sync(Scene.Collision);
        WorldCollisionStats Stats = WorldCollision_GetStats(Scene.Collision);
        TEST_CHECK(run, (Stats.TerrainChunkBuildCount == 17U) && (Stats.RebuildCount == Before.RebuildCount)
            && (Stats.MoveCount == Before.MoveCount) && (Stats.ShapeCount == 16U));
        TEST_CHECK(run, IsNear(GetHeightBelow(&Scene, 4.0f, 4.0f), 1.0f, 1e-4f)
            && IsNear(GetHeightBelow(&Scene, 4.5f, 4.0f), 0.5f, 1e-4f)
            && IsNear(GetHeightBelow(&Scene, -12.0f, -12.0f), 0.0f, 1e-4f));

        // Moved: the shapes go with it, no mesh is built.
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(GetObject(&Scene, Id), TestWorld_At((Vector3){ 0.0f, 0.5f,
            0.0f })));
        WorldCollision_Sync(Scene.Collision);
        Stats = WorldCollision_GetStats(Scene.Collision);
        TEST_CHECK(run, (Stats.TerrainChunkBuildCount == 17U) && (Stats.MoveCount == (Before.MoveCount + 1U))
            && IsNear(GetHeightBelow(&Scene, -12.0f, -12.0f), 0.5f, 1e-4f)
            && IsNear(GetHeightBelow(&Scene, 4.0f, 4.0f), 1.5f, 1e-4f));

        // Its own layer: its shapes again (with the new layer), no mesh; a query that sees only the world layer misses.
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Placement, WorldTerrainSetting_CollisionLayer, true));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetCollisionLayer(Placement, CollisionLayer_Prop));
        WorldCollision_Sync(Scene.Collision);
        Stats = WorldCollision_GetStats(Scene.Collision);
        WorldRayHit Hit;
        WorldCollisionFilter WorldOnly = WorldCollisionFilter_Create(COLLISION_LAYER_BIT(CollisionLayer_World),
            WorldCollisionTriggerMode_Ignore);
        TEST_CHECK(run, (Stats.TerrainChunkBuildCount == 17U) && (Stats.RebuildCount == (Before.RebuildCount + 1U))
            && (CastRay(&Scene, (Vector3){ -12.0f, 5.0f, -12.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, NULL, &Hit) > 0.0f)
            && (Hit.Shape.Layer == CollisionLayer_Prop)
            && (CastRay(&Scene, (Vector3){ -12.0f, 5.0f, -12.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, &WorldOnly, NULL)
                < 0.0f));

        // "Collides" off: nothing; on again: back, without building a mesh.
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetOverridden(Placement, WorldTerrainSetting_IsCollisionEnabled,
            true));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetIsCollisionEnabled(Placement, false));
        WorldCollision_Sync(Scene.Collision);
        TEST_CHECK(run, (WorldCollision_GetStats(Scene.Collision).InstanceCount == 0U)
            && (GetHeightBelow(&Scene, -12.0f, -12.0f) == -100.0f));
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetIsCollisionEnabled(Placement, true));
        WorldCollision_Sync(Scene.Collision);
        TEST_CHECK(run, (WorldCollision_GetStats(Scene.Collision).TerrainChunkBuildCount == 17U)
            && IsNear(GetHeightBelow(&Scene, -12.0f, -12.0f), 0.5f, 1e-4f));

        // A hole in quad (2, 2)'s first triangle, inside the chunk of cell (-2, -2): that chunk again.
        uint32_t Triangle = TestTerrain_Triangle(&Grid, 2U, 2U);
        uint8_t Flag = TERRAIN_TRIANGLE_FLAG_HOLE;
        uint8_t FaceMaterial = 0U;
        TEST_CHECK_SUCCESS(run, TerrainMesh_SetTriangleFlags(Ground, &Triangle, &Flag, &FaceMaterial, 1U));
        TEST_CHECK(run, TerrainMesh_RebuildDirtyChunks(Ground) == 1U);
        WorldCollision_Sync(Scene.Collision);
        TEST_CHECK(run, (WorldCollision_GetStats(Scene.Collision).TerrainChunkBuildCount == 18U)
            && (GetHeightBelow(&Scene, -13.75f, -13.75f) == -100.0f)
            && IsNear(GetHeightBelow(&Scene, -13.25f, -13.25f), 0.5f, 1e-4f));

        // Another terrain: cut afresh (its four chunks); removed: nothing.
        TEST_CHECK_SUCCESS(run, WorldTerrainComponent_SetTerrain(Placement, (const unsigned char*)u8"patch"));
        WorldCollision_Sync(Scene.Collision);
        Stats = WorldCollision_GetStats(Scene.Collision);
        TEST_CHECK(run, (Stats.TerrainChunkBuildCount == 22U) && (Stats.ShapeCount == 4U)
            && (GetHeightBelow(&Scene, -12.0f, -12.0f) == -100.0f) && IsNear(GetHeightBelow(&Scene, -3.0f, 3.0f), 0.5f,
                1e-4f));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Scene.World, Id, WorldComponentType_Terrain));
        WorldCollision_Sync(Scene.Collision);
        TEST_CHECK(run, (WorldCollision_GetStats(Scene.Collision).InstanceCount == 0U)
            && (GetHeightBelow(&Scene, -3.0f, 3.0f) == -100.0f));
    }
    if (User != ASSET_USER_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Scene.Fixture.Manager, User));
    }
    TerrainMesh_Destroy(Copy);
    TerrainMesh_Destroy(SmallMesh);
    Scene_Deconstruct(run, &Scene);
    TestTerrain_FreeGrid(&Small);
    TestTerrain_FreeGrid(&Grid);
}

/* An object with a collider and a terrain collides as its collider says (warned once); without the collider its
 * terrain collides. A terrain that cannot be loaded, or that its object's scale flattens, is warned about once and
 * collides with nothing. */
static void AColliderWinsOverATerrainAndProblemsAreWarnedOnce(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, TERRAIN_COLLISION_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "terrain_collision_warnings", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    TestTerrainGrid Grid;
    TestTerrain_BuildGrid(&Grid, 8U, 8U, 1.0f, -4.0f, -4.0f, NULL);
    TerrainScene Scene;
    TerrainMesh* Ground = NULL;
    uint64_t Both = 0U;
    uint64_t Lost = 0U;
    if (Scene_Construct(run, &Scene, &Log) && ((Ground = AddTerrainAsset(run, &Scene, "field", &Grid, 8.0f)) != NULL))
    {
        Both = AddTerrainObject(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f }, "field");
        Lost = AddTerrainObject(run, &Scene, (Vector3){ 20.0f, 0.0f, 0.0f }, "nowhere");
    }
    WorldComponent* Component = NULL;
    bool IsRun = (Both != 0U) && (Lost != 0U)
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&Scene.World, Both, WorldComponentType_Collider, &Component));
    if (IsRun)
    {
        // The collider's own small ball at the object's origin, 2 m up.
        CollisionShape Ball = CollisionShape_Create(CollisionShapeType_Sphere);
        Ball.Radius = 0.5f;
        Ball.Center = (Vector3){ 0.0f, 2.0f, 0.0f };
        WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Ball));
        TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
        for (int Step = 0; Step < 3; Step++)
        {
            WorldCollision_Sync(Scene.Collision);
        }
        TEST_CHECK(run, IsNear(GetHeightBelow(&Scene, 0.0f, 0.0f), 2.5f, 1e-4f)
            && (GetHeightBelow(&Scene, 3.0f, 3.0f) == -100.0f) && (GetHeightBelow(&Scene, 21.0f, 1.0f) == -100.0f));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Scene.World, Both, WorldComponentType_Collider));
        WorldCollision_Sync(Scene.Collision);
        TEST_CHECK(run, IsNear(GetHeightBelow(&Scene, 0.0f, 0.0f), 0.0f, 1e-4f)
            && IsNear(GetHeightBelow(&Scene, 3.0f, 3.0f), 0.0f, 1e-4f));
        // Flattened by its scale: nothing collides (warned once); unflattened, it collides again.
        for (int Step = 0; Step < 3; Step++)
        {
            TEST_CHECK_SUCCESS(run, WorldObject_SetScale(GetObject(&Scene, Both), (Vector3){ 1.0f, (Step == 1) ? 1e-30f
                : 0.0f, 1.0f }));
            WorldCollision_Sync(Scene.Collision);
            TEST_CHECK(run, GetHeightBelow(&Scene, 3.0f, 3.0f) == -100.0f);
        }
        TEST_CHECK_SUCCESS(run, WorldObject_SetScale(GetObject(&Scene, Both), (Vector3){ 1.0f, 1.0f, 1.0f }));
        WorldCollision_Sync(Scene.Collision);
        TEST_CHECK(run, IsNear(GetHeightBelow(&Scene, 3.0f, 3.0f), 0.0f, 1e-4f));
    }
    TerrainMesh_Destroy(Ground);
    Scene_Deconstruct(run, &Scene);
    TestTerrain_FreeGrid(&Grid);
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    if (IsRun)
    {
        TEST_CHECK(run, CountInLatestLog(run, Directory._data, "has a collider and a terrain") == 1U);
        TEST_CHECK(run, CountInLatestLog(run, Directory._data, "has no collision from its terrain \"tbp:nowhere\"") == 1U);
        TEST_CHECK(run, CountInLatestLog(run, Directory._data, "has a terrain that its transform flattens") == 1U);
    }
    Memory_Free(Directory._data);
}


// Public functions.
void WorldTerrainCollisionTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "rays_hit_the_terrain_where_it_is_and_pass_through_a_hole", RaysHitTheTerrainWhereItIsAndPassThroughAHole },
        { "a_turned_and_scaled_terrain_is_where_its_object_puts_it", ATurnedAndScaledTerrainIsWhereItsObjectPutsIt },
        { "a_capsule_slides_across_chunk_seams_without_a_bump", ACapsuleSlidesAcrossChunkSeamsWithoutABump },
        { "a_character_walks_a_gentle_slope_and_slides_off_a_steep_one",
            ACharacterWalksAGentleSlopeAndSlidesOffASteepOne },
        { "only_changed_chunks_are_rebuilt", OnlyChangedChunksAreRebuilt },
        { "a_collider_wins_over_a_terrain_and_problems_are_warned_once",
            AColliderWinsOverATerrainAndProblemsAreWarnedOnce },
    };
    TestWorld_RunSuite(run, "WorldTerrainCollision", "WorldTerrainCollisionFar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
