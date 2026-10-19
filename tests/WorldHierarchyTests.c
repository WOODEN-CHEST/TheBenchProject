#include "TestSuites.h"
#include "TestText.h"
#include "TestWorld.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldComponent.h"
#include "WorldInternal.h"
#include "WorldTransform.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "wr/WRCompile.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Initial capacity of path and text buffers; they grow as needed. */
#define HIERARCHY_TEST_TEXT_CAPACITY ((size_t)256)
/** How close two matrices must be, relative to their largest entry. */
#define MATRIX_TOLERANCE 2e-5f
/** The most detach hooks the recording type keeps. */
#define MAX_RECORDED_DETACHES 16U


// Types.
/* A test-only component type whose OnDetach records the order objects lose their components in. */
typedef struct RecorderComponentStruct
{
    WorldComponent Base;
    float Unused;
} RecorderComponent;


// Fields.
static const PropertyDescriptor RecorderProperties[] =
{
    { .Key = u8"unused", .WireId = 1U, .Type = PropertyType_Float, PROPERTY_MEMBER(RecorderComponent, Unused),
        .Default.FloatValue = 0.0f, .Flags = PROPERTY_FLAGS_SETTING, .SinceVersion = 1U },
};

static const PropertyTable RecorderTable =
{
    .Name = u8"test_recorder", .Version = 1U, .StructSize = sizeof(RecorderComponent),
    .Descriptors = RecorderProperties, .DescriptorCount = sizeof(RecorderProperties) / sizeof(RecorderProperties[0]),
    .RetiredWireIds = NULL, .RetiredWireIdCount = 0U, .AfterSet = WorldInternal_ComponentAfterSet, .Migrate = NULL
};

/* The ids of the objects whose recorder component was detached, in order, with where each hook saw its object and
 * whether it saw it shown (test-only state, reset by each user). */
static uint64_t RecordedDetaches[MAX_RECORDED_DETACHES];
static Vector3 RecordedPositions[MAX_RECORDED_DETACHES];
static bool RecordedVisibilities[MAX_RECORDED_DETACHES];
static size_t RecordedDetachCount = 0U;


// Static functions: the recording component type.
static Error ConstructRecorder(WorldComponent* component)
{
    return Properties_Construct(&RecorderTable, component);
}

static void DeconstructRecorder(WorldComponent* component)
{
    Properties_Deconstruct(&RecorderTable, component);
}

static void RecordDetach(World* world, WorldObject* object, WorldComponent* component)
{
    UNUSED(world);
    UNUSED(component);
    if (RecordedDetachCount < MAX_RECORDED_DETACHES)
    {
        RecordedDetaches[RecordedDetachCount] = WorldObject_GetId(object);
        RecordedPositions[RecordedDetachCount] = WorldObject_GetWorldPosition(object);
        RecordedVisibilities[RecordedDetachCount] = WorldObject_IsEffectivelyVisible(object);
    }
    RecordedDetachCount++;
}

static const WorldComponentTypeInfo RecorderType =
{
    .Type = (WorldComponentType)WorldComponentType_Count, .Name = u8"test_recorder", .Size = sizeof(RecorderComponent),
    .Table = &RecorderTable, .IsTransformDriver = false, .Construct = ConstructRecorder,
    .Deconstruct = DeconstructRecorder, .OnDetach = RecordDetach
};


// Static functions: helpers.
/* Creates an object under @p parentId (0: a root); returns it, or NULL with a failed check. */
static WorldObject* CreateObject(TestRun* run, World* world, const char* name, uint64_t parentId)
{
    WorldObject* Object = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, (const unsigned char*)name, &Object)))
    {
        return NULL;
    }
    if ((parentId != 0U)
        && !TEST_CHECK_SUCCESS(run, World_SetParent(world, WorldObject_GetId(Object), parentId, false)))
    {
        return NULL;
    }
    return Object;
}

static uint64_t IdOf(const WorldObject* object)
{
    return (object != NULL) ? WorldObject_GetId(object) : 0U;
}

/* Sets an object's whole local transform; returns whether every setter succeeded. */
static bool Place(TestRun* run, WorldObject* object, Vector3 position, Vector3 rotation, Vector3 scale)
{
    return (object != NULL) && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(object, position))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(object, rotation))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetScale(object, scale));
}

static Matrix LocalOf(const WorldObject* object)
{
    return WorldTransform_Compose(WorldObject_GetPosition(object), WorldObject_GetRotation(object),
        WorldObject_GetScale(object));
}

/* Whether two affine matrices agree within MATRIX_TOLERANCE of their largest entry (at least 1). */
static bool AreMatricesNear(Matrix a, Matrix b)
{
    const float A[12] = { a.m0, a.m1, a.m2, a.m4, a.m5, a.m6, a.m8, a.m9, a.m10, a.m12, a.m13, a.m14 };
    const float B[12] = { b.m0, b.m1, b.m2, b.m4, b.m5, b.m6, b.m8, b.m9, b.m10, b.m12, b.m13, b.m14 };
    float Largest = 1.0f;
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        Largest = fmaxf(Largest, fmaxf(fabsf(A[Index]), fabsf(B[Index])));
    }
    for (size_t Index = 0U; Index < 12U; Index++)
    {
        if (!(fabsf(A[Index] - B[Index]) <= (MATRIX_TOLERANCE * Largest)))
        {
            return false;
        }
    }
    return true;
}

static bool AreMatricesEqual(Matrix a, Matrix b)
{
    return Memory_IsEqual(&a, &b, sizeof(Matrix));
}

/* Checks that @p parent's children are @p expected, in order, through every way of reading them. */
static void CheckChildren(TestRun* run, World* world, const WorldObject* parent, const uint64_t* expected, size_t count)
{
    GenericBuffer Ids;
    GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
    if (TEST_CHECK_SUCCESS(run, World_GetChildren(world, IdOf(parent), &Ids)) && TEST_CHECK(run, Ids._count == count))
    {
        TEST_CHECK(run, (count == 0U) || Memory_IsEqual(Ids._data, expected, count * sizeof(uint64_t)));
    }
    Memory_Free(Ids._data);

    TEST_CHECK(run, WorldObject_GetChildCount(parent) == count);
    const WorldObject* Child = WorldObject_GetFirstChild(parent);
    const WorldObject* Previous = NULL;
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (!TEST_CHECK(run, Child != NULL))
        {
            return;
        }
        TEST_CHECK(run, (IdOf(Child) == expected[Index]) && (WorldObject_GetParent(Child) == parent)
            && (WorldObject_GetParentId(Child) == IdOf(parent)) && (Child->_previousSibling == Previous));
        Previous = Child;
        Child = WorldObject_GetNextSibling(Child);
    }
    TEST_CHECK(run, (Child == NULL) && (parent->_lastChild == Previous));
}

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Count = 0;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, HIERARCHY_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, HIERARCHY_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, Path_Append(directory, (const unsigned char*)u8"logs/latest.log", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text)))
    {
        for (const char* Found = strstr((const char*)Text._data, needle); Found != NULL;
            Found = strstr(Found + 1, needle))
        {
            Count++;
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    return Count;
}

/* Reads @p text (a JSON object) into @p object through the object table. */
static Error ReadObjectJSON(TestRun* run, WorldObject* object, const char* text)
{
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return Error_Construct2(ErrorCode_InvalidState, "the test's JSON pool could not be created");
    }
    Error Result = Error_Construct2(ErrorCode_InvalidJSON, "the test's JSON did not parse");
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, text, &Value))
    {
        Error_Deconstruct(&Result);
        Result = Properties_ReadJSON(WorldObject_GetPropertyTable(), Value.Value.Compound, object, NULL);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    return Result;
}


// Static functions: parents.
static void SetParentRefusesUnknownIdsSelfAndCycles(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* A = CreateObject(run, &TestWorld, "a", 0U);
    WorldObject* B = CreateObject(run, &TestWorld, "b", 0U);
    WorldObject* C = CreateObject(run, &TestWorld, "c", 0U);
    if ((A != NULL) && (B != NULL) && (C != NULL))
    {
        uint64_t IdA = IdOf(A);
        uint64_t IdB = IdOf(B);
        uint64_t IdC = IdOf(C);
        TEST_CHECK_ERROR(run, World_SetParent(NULL, IdA, IdB, false), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, World_SetParent(&TestWorld, 0U, IdB, false), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, World_SetParent(&TestWorld, 99U, IdB, false), ErrorCode_InvalidOperation);
        const char* const Unknown[] = { "no object has the id 99", NULL };
        TestText_CheckErrorMessage(run, World_SetParent(&TestWorld, IdA, 99U, false), ErrorCode_InvalidOperation,
            Unknown);
        const char* const Self[] = { "cannot be its own parent", NULL };
        TestText_CheckErrorMessage(run, World_SetParent(&TestWorld, IdA, IdA, true), ErrorCode_InvalidOperation, Self);

        // b under a, then a under b would be a cycle; so would a under c once c is under b.
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdB, IdA, false));
        uint32_t RevisionA = WorldObject_GetRevision(A);
        const char* const Cycle[] = { "a cycle", NULL };
        TestText_CheckErrorMessage(run, World_SetParent(&TestWorld, IdA, IdB, false), ErrorCode_InvalidOperation,
            Cycle);
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdC, IdB, true));
        TEST_CHECK_ERROR(run, World_SetParent(&TestWorld, IdA, IdC, true), ErrorCode_InvalidOperation);
        TEST_CHECK(run, (WorldObject_GetParent(A) == NULL) && (WorldObject_GetRevision(A) == RevisionA));
        TEST_CHECK(run, (WorldObject_GetParent(B) == A) && (WorldObject_GetParent(C) == B));

        uint64_t ParentId = 7U;
        TEST_CHECK_SUCCESS(run, World_GetParent(&TestWorld, IdC, &ParentId));
        TEST_CHECK(run, ParentId == IdB);
        TEST_CHECK_SUCCESS(run, World_GetParent(&TestWorld, IdA, &ParentId));
        TEST_CHECK(run, ParentId == 0U);
        TEST_CHECK_ERROR(run, World_GetParent(&TestWorld, 99U, &ParentId), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, World_GetParent(&TestWorld, 0U, &ParentId), ErrorCode_IllegalArgument);

        // The parent it already has changes nothing; 0 makes it a root.
        uint32_t RevisionC = WorldObject_GetRevision(C);
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdC, IdB, false));
        TEST_CHECK(run, WorldObject_GetRevision(C) == RevisionC);
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdC, 0U, false));
        TEST_CHECK(run, (WorldObject_GetParent(C) == NULL) && (WorldObject_GetParentId(C) == 0U)
            && (WorldObject_GetRevision(C) != RevisionC) && (WorldObject_GetChildCount(B) == 0U));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void HierarchiesHaveAtMost64Levels(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    uint64_t Chain[WORLD_HIERARCHY_MAX_DEPTH];
    bool IsBuilt = true;
    for (size_t Level = 0U; (Level < WORLD_HIERARCHY_MAX_DEPTH) && IsBuilt; Level++)
    {
        WorldObject* Object = CreateObject(run, &TestWorld, "link", (Level > 0U) ? Chain[Level - 1U] : 0U);
        IsBuilt = Object != NULL;
        Chain[Level] = IdOf(Object);
    }
    WorldObject* Extra = CreateObject(run, &TestWorld, "extra", 0U);
    WorldObject* Leaf = CreateObject(run, &TestWorld, "leaf", 0U);
    if (IsBuilt && (Extra != NULL) && (Leaf != NULL))
    {
        // 64 levels exist; a 65th is refused.
        const char* const TooDeep[] = { "more than 64 levels", NULL };
        TestText_CheckErrorMessage(run, World_SetParent(&TestWorld, IdOf(Extra), Chain[63], false),
            ErrorCode_InvalidOperation, TooDeep);
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Extra), Chain[62], false));

        // A subtree of two levels counts both: under level 63 its leaf would be level 65.
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Extra), 0U, false));
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Leaf), IdOf(Extra), false));
        TEST_CHECK_ERROR(run, World_SetParent(&TestWorld, IdOf(Extra), Chain[62], false), ErrorCode_InvalidOperation);
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Extra), Chain[61], false));

        // The deepest object's world matrix still reads, and the whole chain is removed with its root.
        TEST_CHECK(run, AreMatricesNear(WorldObject_GetWorldMatrix(Leaf), MatrixIdentity()));
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, Chain[0]));
        TEST_CHECK(run, World_GetObjectCount(&TestWorld) == 0U);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}


// Static functions: world matrices.
static void WorldMatricesMultiplyDownThreeLevels(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* A = CreateObject(run, &TestWorld, "a", 0U);
    WorldObject* B = CreateObject(run, &TestWorld, "b", IdOf(A));
    WorldObject* C = CreateObject(run, &TestWorld, "c", IdOf(B));
    if (Place(run, A, (Vector3){ 1.0f, 2.0f, 3.0f }, (Vector3){ 0.0f, 0.5f, 0.0f }, (Vector3){ 2.0f, 2.0f, 2.0f })
        && Place(run, B, (Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.3f, 0.0f, 0.0f }, (Vector3){ 1.0f, 0.5f, 1.0f })
        && Place(run, C, (Vector3){ 0.0f, 1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.7f }, (Vector3){ 1.0f, 1.0f, 1.0f }))
    {
        Matrix WorldA = LocalOf(A);
        Matrix WorldB = MatrixMultiply(LocalOf(B), WorldA);
        Matrix WorldC = MatrixMultiply(LocalOf(C), WorldB);
        // Computed on the fly while dirty, then cached: the same products either way.
        for (size_t Pass = 0U; Pass < 2U; Pass++)
        {
            Matrix Read = MatrixIdentity();
            TEST_CHECK(run, AreMatricesEqual(WorldObject_GetWorldMatrix(A), WorldA));
            TEST_CHECK(run, AreMatricesEqual(WorldObject_GetWorldMatrix(B), WorldB));
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_GetWorldMatrix(&TestWorld, IdOf(C), &Read))
                && AreMatricesEqual(Read, WorldC));
            World_UpdateTransforms(&TestWorld);
        }

        // c sits at b's local point (0, 1, 0); a point of c lands where the matrices put it.
        Vector3 Position = Vector3Zero();
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_GetWorldPosition(&TestWorld, IdOf(C), &Position))
            && (Vector3Distance(Position, Vector3Transform((Vector3){ 0.0f, 1.0f, 0.0f }, WorldB)) < 1e-5f));
        TEST_CHECK(run, Vector3Distance(WorldObject_GetWorldPosition(A), (Vector3){ 1.0f, 2.0f, 3.0f }) == 0.0f);

        // A root's world rotation is its own; a child's turns with its parent (a uniform scale keeps it exact).
        Vector3 Rotation = Vector3Zero();
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_GetWorldRotation(&TestWorld, IdOf(A), &Rotation))
            && (Rotation.y == 0.5f) && (Rotation.x == 0.0f));
        Vector3 WorldPosition;
        Vector3 WorldRotation;
        Vector3 WorldScale;
        WorldObject_GetWorldTransform(B, &WorldPosition, &WorldRotation, &WorldScale);
        Matrix Expected = MatrixMultiply(MatrixRotateXYZ((Vector3){ 0.3f, 0.0f, 0.0f }),
            MatrixRotateXYZ((Vector3){ 0.0f, 0.5f, 0.0f }));
        TEST_CHECK(run, AreMatricesNear(MatrixRotateXYZ(WorldRotation), Expected));
        TEST_CHECK(run, Vector3Distance(WorldScale, (Vector3){ 2.0f, 1.0f, 2.0f }) < 1e-5f);
        TEST_CHECK_ERROR(run, World_GetWorldMatrix(&TestWorld, 99U, &Expected), ErrorCode_InvalidOperation);
        TEST_CHECK_ERROR(run, World_GetWorldPosition(&TestWorld, IdOf(A), NULL), ErrorCode_IllegalArgument);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void ReparentingKeepsTheWorldPlacementWhenAsked(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* First = CreateObject(run, &TestWorld, "first", 0U);
    WorldObject* Second = CreateObject(run, &TestWorld, "second", 0U);
    WorldObject* Mover = CreateObject(run, &TestWorld, "mover", IdOf(First));
    WorldObject* Rider = CreateObject(run, &TestWorld, "rider", IdOf(Mover));
    if (Place(run, First, (Vector3){ 5.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, (Vector3){ 2.0f, 2.0f, 2.0f })
        && Place(run, Second, (Vector3){ -3.0f, 4.0f, 1.0f }, (Vector3){ 0.4f, -0.2f, 0.9f },
            (Vector3){ 0.5f, 0.5f, 0.5f })
        && Place(run, Mover, (Vector3){ 1.0f, 2.0f, 3.0f }, (Vector3){ 0.3f, 0.2f, 0.1f }, (Vector3){ 1.0f, 2.0f, 3.0f })
        && Place(run, Rider, (Vector3){ 0.0f, 1.0f, 0.0f }, Vector3Zero(), (Vector3){ 1.0f, 1.0f, 1.0f }))
    {
        Matrix MoverBefore = WorldObject_GetWorldMatrix(Mover);
        Matrix RiderBefore = WorldObject_GetWorldMatrix(Rider);
        Vector3 RiderLocal = WorldObject_GetPosition(Rider);

        // Under the second parent, then as a root: the mover (and the rider on it) stay where they are.
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Mover), IdOf(Second), true));
        TEST_CHECK(run, (WorldObject_GetParent(Mover) == Second) && (WorldObject_GetParent(Rider) == Mover));
        TEST_CHECK(run, AreMatricesNear(WorldObject_GetWorldMatrix(Mover), MoverBefore));
        TEST_CHECK(run, AreMatricesNear(WorldObject_GetWorldMatrix(Rider), RiderBefore));
        TEST_CHECK(run, Vector3Distance(WorldObject_GetPosition(Rider), RiderLocal) == 0.0f); // its local is untouched
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK(run, AreMatricesNear(WorldObject_GetWorldMatrix(Rider), RiderBefore));

        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Mover), 0U, true));
        TEST_CHECK(run, AreMatricesNear(WorldObject_GetWorldMatrix(Mover), MoverBefore));
        Vector3 Position;
        Vector3 Rotation;
        Vector3 Scale;
        WorldTransform_Decompose(MoverBefore, (Vector3){ 1.0f, 1.0f, 1.0f }, &Position, &Rotation, &Scale);
        TEST_CHECK(run, (Vector3Distance(WorldObject_GetPosition(Mover), Position) < 1e-4f)
            && (Vector3Distance(WorldObject_GetScale(Mover), Scale) < 1e-4f)
            && AreMatricesNear(MatrixRotateXYZ(WorldObject_GetRotation(Mover)), MatrixRotateXYZ(Rotation)));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void ReparentingWithoutKeepingKeepsTheLocalValues(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* Parent = CreateObject(run, &TestWorld, "parent", 0U);
    WorldObject* Flat = CreateObject(run, &TestWorld, "flat", 0U);
    WorldObject* Child = CreateObject(run, &TestWorld, "child", 0U);
    if (Place(run, Parent, (Vector3){ 10.0f, 0.0f, -2.0f }, (Vector3){ 0.0f, 1.5f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f })
        && Place(run, Flat, Vector3Zero(), Vector3Zero(), (Vector3){ 1.0f, 0.0f, 1.0f })
        && Place(run, Child, (Vector3){ 1.0f, 2.0f, 3.0f }, (Vector3){ 0.1f, 0.2f, 0.3f }, (Vector3){ 1.0f, 1.0f, 1.0f }))
    {
        Matrix Local = LocalOf(Child);
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Child), IdOf(Parent), false));
        TEST_CHECK(run, AreMatricesEqual(LocalOf(Child), Local));
        TEST_CHECK(run, AreMatricesEqual(WorldObject_GetWorldMatrix(Child), MatrixMultiply(Local, LocalOf(Parent))));

        // Keeping the place under a parent flattened to nothing is impossible: refused, nothing changes.
        uint32_t Revision = WorldObject_GetRevision(Child);
        const char* const Flattened[] = { "cannot be inverted", NULL };
        TestText_CheckErrorMessage(run, World_SetParent(&TestWorld, IdOf(Child), IdOf(Flat), true),
            ErrorCode_InvalidOperation, Flattened);
        TEST_CHECK(run, (WorldObject_GetParent(Child) == Parent) && (WorldObject_GetRevision(Child) == Revision)
            && AreMatricesEqual(LocalOf(Child), Local));
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Child), IdOf(Flat), false));
        TEST_CHECK(run, WorldObject_GetWorldMatrix(Child).m13 == 0.0f); // flattened onto the parent's plane
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void MovingAParentRecomputesOnlyItsSubtree(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* A = CreateObject(run, &TestWorld, "a", 0U);
    WorldObject* B = CreateObject(run, &TestWorld, "b", IdOf(A));
    WorldObject* C = CreateObject(run, &TestWorld, "c", IdOf(A));
    WorldObject* D = CreateObject(run, &TestWorld, "d", IdOf(B));
    WorldObject* E = CreateObject(run, &TestWorld, "e", 0U);
    if ((A != NULL) && (B != NULL) && (C != NULL) && (D != NULL) && (E != NULL))
    {
        World_UpdateTransforms(&TestWorld);
        uint64_t Count = WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld);
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK(run, WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld) == Count); // nothing dirty

        WorldObject* All[] = { A, B, C, D, E };
        uint32_t Revisions[5];
        Matrix Before[5];
        for (size_t Index = 0U; Index < 5U; Index++)
        {
            Revisions[Index] = WorldObject_GetTransformRevision(All[Index]);
            Before[Index] = WorldObject_GetWorldMatrix(All[Index]);
        }

        // Moving a moves its whole subtree (a, b, c, d) and nothing else.
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(A, (Vector3){ 0.0f, 3.0f, 0.0f }));
        for (size_t Index = 0U; Index < 5U; Index++)
        {
            bool IsMoved = Index < 4U;
            TEST_CHECK(run, (WorldObject_GetTransformRevision(All[Index]) != Revisions[Index]) == IsMoved);
            TEST_CHECK(run, All[Index]->_isWorldMatrixDirty == IsMoved);
        }
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK(run, WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld) == (Count + 4U));
        TEST_CHECK(run, (WorldObject_GetWorldMatrix(D).m13 == 3.0f) && (WorldObject_GetWorldMatrix(C).m13 == 3.0f));
        TEST_CHECK(run, AreMatricesEqual(WorldObject_GetWorldMatrix(E), Before[4]));
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK(run, WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld) == (Count + 4U));

        // Turning b moves b and d; a table write to c's scale moves c alone.
        TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(B, (Vector3){ 0.0f, 0.0f, 1.0f }));
        PropertyValue Scale = PropertyValue_FromVector3((Vector3){ 2.0f, 2.0f, 2.0f });
        TEST_CHECK_SUCCESS(run, Properties_Set(WorldObject_GetPropertyTable(), C, u8"scale", &Scale));
        TEST_CHECK(run, !A->_isWorldMatrixDirty && B->_isWorldMatrixDirty && C->_isWorldMatrixDirty
            && D->_isWorldMatrixDirty && !E->_isWorldMatrixDirty);
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK(run, WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld) == (Count + 7U));

        // Other properties do not move anything.
        uint32_t Revision = WorldObject_GetTransformRevision(A);
        TEST_CHECK_SUCCESS(run, WorldObject_SetName(A, (const unsigned char*)u8"renamed"));
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(A, false));
        TEST_CHECK(run, (WorldObject_GetTransformRevision(A) == Revision) && !A->_isWorldMatrixDirty);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void ReadsComputeDirtyMatricesWithoutStoringThem(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* Parent = CreateObject(run, &TestWorld, "parent", 0U);
    WorldObject* Child = CreateObject(run, &TestWorld, "child", IdOf(Parent));
    if ((Child != NULL) && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Child, (Vector3){ 1.0f, 0.0f, 0.0f })))
    {
        World_UpdateTransforms(&TestWorld);
        uint64_t Count = WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld);
        TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Parent, (Vector3){ 0.0f, PI / 2.0f, 0.0f }));

        Matrix Expected = MatrixMultiply(LocalOf(Child), LocalOf(Parent));
        Matrix Read = MatrixIdentity();
        TEST_CHECK(run, AreMatricesEqual(WorldObject_GetWorldMatrix(Child), Expected));
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_GetWorldMatrix(&TestWorld, IdOf(Child), &Read))
            && AreMatricesEqual(Read, Expected));
        // Turned a quarter about Y, the parent takes the child's +X to -Z.
        TEST_CHECK(run, Vector3Distance(WorldObject_GetWorldPosition(Child), (Vector3){ 0.0f, 0.0f, -1.0f }) < 1e-6f);
        TEST_CHECK(run, Child->_isWorldMatrixDirty && Parent->_isWorldMatrixDirty
            && (WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld) == Count));

        World_UpdateTransforms(&TestWorld);
        TEST_CHECK(run, !Child->_isWorldMatrixDirty && AreMatricesEqual(Child->_worldMatrix, Expected)
            && (WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld) == (Count + 2U)));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}


// Static functions: children and removal.
static void ChildrenKeepTheOrderTheyWereAttachedIn(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* Parent = CreateObject(run, &TestWorld, "parent", 0U);
    WorldObject* First = CreateObject(run, &TestWorld, "first", IdOf(Parent));
    WorldObject* Second = CreateObject(run, &TestWorld, "second", IdOf(Parent));
    WorldObject* Third = CreateObject(run, &TestWorld, "third", IdOf(Parent));
    if ((First != NULL) && (Second != NULL) && (Third != NULL))
    {
        uint64_t Expected[3] = { IdOf(First), IdOf(Second), IdOf(Third) };
        CheckChildren(run, &TestWorld, Parent, Expected, 3U);

        // Detached and attached again, the first comes last; a removed child leaves its neighbours linked.
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(First), 0U, false));
        uint64_t WithoutFirst[2] = { IdOf(Second), IdOf(Third) };
        CheckChildren(run, &TestWorld, Parent, WithoutFirst, 2U);
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(First), IdOf(Parent), false));
        uint64_t Reordered[3] = { IdOf(Second), IdOf(Third), IdOf(First) };
        CheckChildren(run, &TestWorld, Parent, Reordered, 3U);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, IdOf(Third)));
        uint64_t AfterRemoval[2] = { Reordered[0], Reordered[2] };
        CheckChildren(run, &TestWorld, Parent, AfterRemoval, 2U);

        // World_GetChildren appends; it refuses a buffer of another element size and an unknown object.
        GenericBuffer Ids;
        GenericBuffer_AllocateVariable(&Ids, 0U, sizeof(uint64_t));
        TEST_CHECK_SUCCESS(run, World_GetChildren(&TestWorld, IdOf(Parent), &Ids));
        TEST_CHECK_SUCCESS(run, World_GetChildren(&TestWorld, IdOf(Parent), &Ids));
        TEST_CHECK(run, Ids._count == 4U);
        TEST_CHECK_ERROR(run, World_GetChildren(&TestWorld, 99U, &Ids), ErrorCode_InvalidOperation);
        Memory_Free(Ids._data);
        GenericBuffer Bytes;
        GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
        TEST_CHECK_ERROR(run, World_GetChildren(&TestWorld, IdOf(Parent), &Bytes), ErrorCode_IllegalArgument);
        Memory_Free(Bytes._data);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

/* Checks that the world's components of @p type belong to the objects @p expectedIds, in that order. */
static void CheckComponentOwners(TestRun* run, const World* world, WorldComponentType type, const uint64_t* expectedIds,
    size_t expectedCount)
{
    size_t Count = 0;
    WorldComponent* const* Components = World_GetComponents(world, type, &Count);
    if (!TEST_CHECK(run, Count == expectedCount))
    {
        return;
    }
    for (size_t Index = 0; Index < Count; Index++)
    {
        const WorldObject* Owner = WorldComponent_GetOwner(Components[Index]);
        TEST_CHECK(run, (WorldObject_GetId(Owner) == expectedIds[Index])
            && (WorldObject_GetComponent(Owner, type) == Components[Index]));
    }
}

static void RemovingAnObjectRemovesItsSubtree(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    // root -> middle -> leaf, root -> side; other roots interleaved, so the arrays mix both.
    WorldObject* Root = CreateObject(run, &TestWorld, "root", 0U);
    WorldObject* Before = CreateObject(run, &TestWorld, "before", 0U);
    WorldObject* Middle = CreateObject(run, &TestWorld, "middle", IdOf(Root));
    WorldObject* Leaf = CreateObject(run, &TestWorld, "leaf", IdOf(Middle));
    WorldObject* After = CreateObject(run, &TestWorld, "after", 0U);
    WorldObject* Side = CreateObject(run, &TestWorld, "side", IdOf(Root));
    WorldComponent* Component = NULL;
    WorldObject* WithModels[] = { Root, Before, Leaf, After, Side };
    WorldObject* WithLights[] = { Middle, Before, Leaf, After };
    bool IsBuilt = (Root != NULL) && (Before != NULL) && (Middle != NULL) && (Leaf != NULL) && (After != NULL)
        && (Side != NULL);
    for (size_t Index = 0U; IsBuilt && (Index < 5U); Index++)
    {
        IsBuilt = TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, IdOf(WithModels[Index]),
            WorldComponentType_Model, &Component));
    }
    for (size_t Index = 0U; IsBuilt && (Index < 4U); Index++)
    {
        IsBuilt = TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, IdOf(WithLights[Index]),
            WorldComponentType_Light, &Component));
    }
    if (IsBuilt)
    {
        uint64_t IdRoot = IdOf(Root);
        uint64_t IdMiddle = IdOf(Middle);
        uint64_t IdLeaf = IdOf(Leaf);
        uint64_t IdSide = IdOf(Side);
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Root, (Vector3){ 1.0f, 0.0f, 0.0f })); // four dirty
        uint64_t Count = WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld);
        uint64_t ObjectVersion = World_GetObjectSetVersion(&TestWorld);
        uint64_t ComponentVersion = World_GetComponentSetVersion(&TestWorld);

        // The middle goes with its leaf; the root keeps its side, the arrays keep the others in order.
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, IdMiddle));
        WorldObject* Found = NULL;
        TEST_CHECK(run, !World_TryGetObject(&TestWorld, IdMiddle, &Found) && !World_TryGetObject(&TestWorld, IdLeaf, &Found));
        TEST_CHECK(run, World_GetObjectCount(&TestWorld) == 4U);
        TEST_CHECK(run, (World_GetObjectSetVersion(&TestWorld) != ObjectVersion)
            && (World_GetComponentSetVersion(&TestWorld) != ComponentVersion));
        uint64_t Models[] = { IdRoot, IdOf(Before), IdOf(After), IdSide };
        uint64_t Lights[] = { IdOf(Before), IdOf(After) };
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Model, Models, 4U);
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Light, Lights, 2U);
        uint64_t Children[] = { IdSide };
        CheckChildren(run, &TestWorld, Root, Children, 1U);
        WorldObject* AtIndex = NULL;
        TEST_CHECK(run, TEST_CHECK_SUCCESS(run, World_GetObjectByIndex(&TestWorld, 2U, &AtIndex)) && (AtIndex == After));

        // The removed objects were dirty: only the root and its side are left to compute.
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK(run, (WorldInternal_GetWorldMatrixRecomputeCount(&TestWorld) == (Count + 2U))
            && (TestWorld._dirtyTransformCount == 0U));

        // The root goes with its side.
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, IdRoot));
        TEST_CHECK(run, (World_GetObjectCount(&TestWorld) == 2U) && !World_TryGetObject(&TestWorld, IdSide, &Found));
        uint64_t LeftModels[] = { IdOf(Before), IdOf(After) };
        CheckComponentOwners(run, &TestWorld, WorldComponentType_Model, LeftModels, 2U);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

/* Constructs a key registry and a world holding the standard types plus the recording type (release with
 * TestWorld_Deconstruct). */
static bool ConstructRecorderWorld(TestRun* run, World* world, KeyRegistry* keys)
{
    const WorldComponentTypeInfo* Types[WorldComponentType_Count + 1] =
    {
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Model),
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Sprite),
        WorldComponent_GetStandardTypeInfo(WorldComponentType_Light),
        &RecorderType,
    };
    if (!TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(keys)))
    {
        return false;
    }
    if (!TEST_CHECK_SUCCESS(run, WorldInternal_ConstructWithTypes(world, keys, NULL, NULL, Types,
        sizeof(Types) / sizeof(Types[0]))))
    {
        KeyRegistry_Deconstruct(keys);
        return false;
    }
    RecordedDetachCount = 0U;
    return true;
}

/* Gives @p object the recording component. */
static bool AddRecorder(TestRun* run, World* world, const WorldObject* object)
{
    WorldComponent* Component = NULL;
    return (object != NULL) && TEST_CHECK_SUCCESS(run, World_AddComponent(world, IdOf(object),
        (WorldComponentType)WorldComponentType_Count, &Component));
}

static void RemovalDetachesComponentsChildrenFirst(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!ConstructRecorderWorld(run, &TestWorld, &TestKeys))
    {
        return;
    }
    // root -> a -> b, root -> c: removed b, a, c, then the root.
    WorldObject* Root = CreateObject(run, &TestWorld, "root", 0U);
    WorldObject* A = CreateObject(run, &TestWorld, "a", IdOf(Root));
    WorldObject* B = CreateObject(run, &TestWorld, "b", IdOf(A));
    WorldObject* C = CreateObject(run, &TestWorld, "c", IdOf(Root));
    WorldObject* Ordered[] = { B, A, C, Root };
    bool IsBuilt = (Root != NULL) && (A != NULL) && (B != NULL) && (C != NULL);
    for (size_t Index = 0U; IsBuilt && (Index < 4U); Index++)
    {
        IsBuilt = AddRecorder(run, &TestWorld, Ordered[Index]);
    }
    if (IsBuilt)
    {
        uint64_t Expected[4] = { IdOf(B), IdOf(A), IdOf(C), IdOf(Root) };
        RecordedDetachCount = 0U;
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, IdOf(Root)));
        TEST_CHECK(run, (RecordedDetachCount == 4U) && Memory_IsEqual(RecordedDetaches, Expected, sizeof(Expected)));
        size_t Count = 0U;
        TEST_CHECK(run, (World_GetComponents(&TestWorld, (WorldComponentType)WorldComponentType_Count, &Count) == NULL)
            && (Count == 0U));
    }
    RecordedDetachCount = 0U;
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}


/* A detach hook sees its object where it is: on a removal the subtree is unlinked only after every hook ran, and when
 * the world is destroyed every hook runs before any object goes (a child made before its parent included). */
static void DetachHooksSeeTheHierarchyInPlace(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!ConstructRecorderWorld(run, &TestWorld, &TestKeys))
    {
        return;
    }
    WorldObject* Cup = CreateObject(run, &TestWorld, "cup", 0U); // made before its parent: destroyed after it
    WorldObject* Lamp = CreateObject(run, &TestWorld, "lamp", 0U);
    WorldObject* Desk = CreateObject(run, &TestWorld, "desk", 0U);
    bool IsBuilt = (Lamp != NULL) && (Desk != NULL) && (Cup != NULL)
        && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Desk, (Vector3){ 10.0f, 0.0f, 0.0f }))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(Desk, false))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Lamp, (Vector3){ 0.5f, 0.0f, 0.0f }))
        && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Cup, (Vector3){ 0.25f, 0.0f, 0.0f }))
        && TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Lamp), IdOf(Desk), false))
        && TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Cup), IdOf(Desk), false))
        && AddRecorder(run, &TestWorld, Lamp) && AddRecorder(run, &TestWorld, Desk) && AddRecorder(run, &TestWorld, Cup);
    if (IsBuilt)
    {
        World_UpdateTransforms(&TestWorld);
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&TestWorld, IdOf(Lamp)));
        TEST_CHECK(run, (RecordedDetachCount == 1U) && (fabsf(RecordedPositions[0].x - 10.5f) < 1e-6f)
            && !RecordedVisibilities[0]);
    }
    // The cup was made before the desk it sits on: destroying the world still runs its hook first, with the desk alive.
    RecordedDetachCount = 0U;
    uint64_t IdCup = IdOf(Cup);
    uint64_t IdDesk = IdOf(Desk);
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
    if (IsBuilt)
    {
        TEST_CHECK(run, (RecordedDetachCount == 2U) && (RecordedDetaches[0] == IdCup) && (RecordedDetaches[1] == IdDesk)
            && (fabsf(RecordedPositions[0].x - 10.25f) < 1e-6f) && !RecordedVisibilities[0]);
    }
    RecordedDetachCount = 0U;
}

/* A child's world scale keeps the signs its chain of scales gives it, as a root's own scale would: what billboards and
 * axis billboards use (they drop the turn that a moved sign would need). Keeping the place on reparenting keeps them. */
static void WorldScalesKeepTheSignsOfTheirChain(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* Plain = CreateObject(run, &TestWorld, "plain", 0U);
    WorldObject* Room = CreateObject(run, &TestWorld, "mirrored room", 0U);
    WorldObject* Upside = CreateObject(run, &TestWorld, "upside down", IdOf(Plain));
    WorldObject* Turned = CreateObject(run, &TestWorld, "turned half", IdOf(Plain));
    WorldObject* Sign = CreateObject(run, &TestWorld, "sign", IdOf(Room));
    if (Place(run, Room, Vector3Zero(), Vector3Zero(), (Vector3){ 1.0f, 1.0f, -1.0f })
        && Place(run, Upside, Vector3Zero(), Vector3Zero(), (Vector3){ 1.0f, -1.0f, 1.0f })
        && Place(run, Turned, Vector3Zero(), Vector3Zero(), (Vector3){ -1.0f, -1.0f, 1.0f }) && (Sign != NULL))
    {
        const WorldObject* Objects[] = { Upside, Turned, Sign };
        const Vector3 Expected[] = { { 1.0f, -1.0f, 1.0f }, { -1.0f, -1.0f, 1.0f }, { 1.0f, 1.0f, -1.0f } };
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            Vector3 Position;
            Vector3 Rotation;
            Vector3 Scale;
            WorldObject_GetWorldTransform(Objects[Index], &Position, &Rotation, &Scale);
            TEST_CHECK(run, (Vector3Distance(Scale, Expected[Index]) < 1e-6f) && (Vector3Length(Rotation) < 1e-6f));
        }

        // Out of the mirrored room into a plain one, keeping its place: the sign carries the mirror itself now. Then into
        // the room again: the room's mirror is taken back out of it.
        Matrix Before = WorldObject_GetWorldMatrix(Sign);
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Sign), IdOf(Plain), true));
        TEST_CHECK(run, (Vector3Distance(WorldObject_GetScale(Sign), (Vector3){ 1.0f, 1.0f, -1.0f }) < 1e-6f)
            && (Vector3Length(WorldObject_GetRotation(Sign)) < 1e-6f)
            && AreMatricesNear(WorldObject_GetWorldMatrix(Sign), Before));
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Sign), IdOf(Room), true));
        TEST_CHECK(run, (Vector3Distance(WorldObject_GetScale(Sign), (Vector3){ 1.0f, 1.0f, 1.0f }) < 1e-6f)
            && (Vector3Length(WorldObject_GetRotation(Sign)) < 1e-6f));
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Upside), IdOf(Room), true));
        TEST_CHECK(run, Vector3Distance(WorldObject_GetScale(Upside), (Vector3){ 1.0f, -1.0f, -1.0f }) < 1e-6f);
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}


// Static functions: flags and the table.
static void EffectiveFlagsFollowTheAncestors(TestRun* run)
{
    World TestWorld;
    KeyRegistry TestKeys;
    if (!TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, NULL))
    {
        return;
    }
    WorldObject* A = CreateObject(run, &TestWorld, "a", 0U);
    WorldObject* B = CreateObject(run, &TestWorld, "b", IdOf(A));
    WorldObject* C = CreateObject(run, &TestWorld, "c", IdOf(B));
    if ((A != NULL) && (B != NULL) && (C != NULL))
    {
        TEST_CHECK(run, WorldObject_IsEffectivelyEnabled(C) && WorldObject_IsEffectivelyVisible(C));

        // A disabled root disables the subtree; the children's own flags stay.
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(A, false));
        TEST_CHECK(run, !WorldObject_IsEffectivelyEnabled(B) && !WorldObject_IsEffectivelyEnabled(C)
            && WorldObject_IsEnabled(C) && WorldObject_IsEffectivelyVisible(C));
        TEST_CHECK(run, !World_IsEffectivelyEnabled(&TestWorld, IdOf(C)) && World_IsEffectivelyVisible(&TestWorld, IdOf(C)));

        // A hidden middle hides what is under it, not what is above it.
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(B, false));
        TEST_CHECK(run, WorldObject_IsEffectivelyVisible(A) && !WorldObject_IsEffectivelyVisible(B)
            && !WorldObject_IsEffectivelyVisible(C));
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(A, true));
        TEST_CHECK(run, WorldObject_IsEffectivelyEnabled(C) && !WorldObject_IsEffectivelyVisible(C));

        // Out from under it, the leaf shows again.
        TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(C), IdOf(A), true));
        TEST_CHECK(run, WorldObject_IsEffectivelyVisible(C));
        TEST_CHECK(run, !World_IsEffectivelyEnabled(&TestWorld, 99U) && !World_IsEffectivelyVisible(&TestWorld, 0U));
    }
    TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
}

static void TheParentRowIsReadOnlyAndReadsAttachOrAreRefused(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, HIERARCHY_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    if (!TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "hierarchy_refused_parent", &Directory))
        || !TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data)))
    {
        Memory_Free(Directory._data);
        return;
    }
    World TestWorld;
    KeyRegistry TestKeys;
    if (TestWorld_Construct(run, &TestWorld, &TestKeys, NULL, &Log))
    {
        const PropertyTable* Table = WorldObject_GetPropertyTable();
        WorldObject* Desk = CreateObject(run, &TestWorld, "desk", 0U);
        WorldObject* Cup = CreateObject(run, &TestWorld, "cup", 0U);
        WorldObject* Saucer = CreateObject(run, &TestWorld, "saucer", 0U);
        char Text[128];
        if ((Desk != NULL) && (Cup != NULL) && (Saucer != NULL)
            && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Desk, (Vector3){ 2.0f, 0.0f, 0.0f })))
        {
            // Read through the table; set only through the world.
            PropertyValue Value = PropertyValue_FromObjectId(IdOf(Desk));
            TEST_CHECK_ERROR(run, Properties_Set(Table, Cup, u8"parent", &Value), ErrorCode_InvalidOperation);
            TEST_CHECK(run, WorldObject_GetParent(Cup) == NULL);

            // A read that names a parent attaches the object to it, its position now relative to the parent.
            snprintf(Text, sizeof(Text), "{\"parent\": %llu, \"position\": [0, 1, 0]}", (unsigned long long)IdOf(Desk));
            TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, Cup, Text));
            TEST_CHECK(run, (WorldObject_GetParent(Cup) == Desk)
                && (Vector3Distance(WorldObject_GetWorldPosition(Cup), (Vector3){ 2.0f, 1.0f, 0.0f }) < 1e-6f));
            TEST_CHECK_SUCCESS(run, Properties_Get(Table, Cup, u8"parent", &Value));
            TEST_CHECK(run, Value.Data.ObjectIdValue == IdOf(Desk));
            TEST_CHECK(run, World_GetRefusedParentCount(&TestWorld) == 0U);

            // A missing parent and a cycle are refused: the object keeps its parent; the first refusal is logged.
            TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, Cup, "{\"parent\": 999, \"position\": [0, 2, 0]}"));
            TEST_CHECK(run, (WorldObject_GetParent(Cup) == Desk) && (WorldObject_GetParentId(Cup) == IdOf(Desk))
                && (WorldObject_GetPosition(Cup).y == 2.0f) && (World_GetRefusedParentCount(&TestWorld) == 1U));
            snprintf(Text, sizeof(Text), "{\"parent\": %llu}", (unsigned long long)IdOf(Cup));
            TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, Desk, Text));
            TEST_CHECK(run, (WorldObject_GetParent(Desk) == NULL) && (World_GetRefusedParentCount(&TestWorld) == 2U));

            // A read without a parent gives the default: a root.
            TEST_CHECK_SUCCESS(run, ReadObjectJSON(run, Cup, "{\"name\": \"loose cup\"}"));
            TEST_CHECK(run, (WorldObject_GetParent(Cup) == NULL) && (WorldObject_GetChildCount(Desk) == 0U));

            // A copy brings the parent along: the copy becomes the original's sibling.
            TEST_CHECK_SUCCESS(run, World_SetParent(&TestWorld, IdOf(Cup), IdOf(Desk), false));
            TEST_CHECK_SUCCESS(run, Properties_CopyTo(Table, Cup, Saucer));
            uint64_t Siblings[] = { IdOf(Cup), IdOf(Saucer) };
            CheckChildren(run, &TestWorld, Desk, Siblings, 2U);
            TEST_CHECK(run, AreMatricesEqual(WorldObject_GetWorldMatrix(Saucer), WorldObject_GetWorldMatrix(Cup)));
        }
        TestWorld_Deconstruct(run, &TestWorld, &TestKeys);
    }
    TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
    TEST_CHECK(run, CountInLatestLog(run, Directory._data, "which was refused") == 1U);
    Memory_Free(Directory._data);
}


// Public functions.
void WorldHierarchyTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "set_parent_refuses_unknown_ids_self_and_cycles", SetParentRefusesUnknownIdsSelfAndCycles },
        { "hierarchies_have_at_most_64_levels", HierarchiesHaveAtMost64Levels },
        { "world_matrices_multiply_down_three_levels", WorldMatricesMultiplyDownThreeLevels },
        { "reparenting_keeps_the_world_placement_when_asked", ReparentingKeepsTheWorldPlacementWhenAsked },
        { "reparenting_without_keeping_keeps_the_local_values", ReparentingWithoutKeepingKeepsTheLocalValues },
        { "moving_a_parent_recomputes_only_its_subtree", MovingAParentRecomputesOnlyItsSubtree },
        { "reads_compute_dirty_matrices_without_storing_them", ReadsComputeDirtyMatricesWithoutStoringThem },
        { "children_keep_the_order_they_were_attached_in", ChildrenKeepTheOrderTheyWereAttachedIn },
        { "removing_an_object_removes_its_subtree", RemovingAnObjectRemovesItsSubtree },
        { "removal_detaches_components_children_first", RemovalDetachesComponentsChildrenFirst },
        { "detach_hooks_see_the_hierarchy_in_place", DetachHooksSeeTheHierarchyInPlace },
        { "world_scales_keep_the_signs_of_their_chain", WorldScalesKeepTheSignsOfTheirChain },
        { "effective_flags_follow_the_ancestors", EffectiveFlagsFollowTheAncestors },
        { "the_parent_row_is_read_only_and_reads_attach_or_are_refused",
            TheParentRowIsReadOnlyAndReadsAttachOrAreRefused },
    };
    TestRun_RunSuite(run, "WorldHierarchy", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
