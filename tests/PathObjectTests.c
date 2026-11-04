#include "TestSuites.h"
#include "TestAssets.h"
#include "AssetManager.h"
#include "PathObject.h"
#include "PathObjectPlacement.h"
#include "WorldPathObjectComponent.h"
#include "WorldModelInstancesComponent.h"
#include "WorldTransform.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
#define NEAR(a, b, tolerance) (fabs((double)(a) - (double)(b)) <= (double)(tolerance))
/** How close a copy's corner must land, in metres. */
#define COPY_TOLERANCE 1.0e-3


// Static functions: helpers.
static PathObjectPieceEnd MakeEnd(double x, double y, double z)
{
    return (PathObjectPieceEnd){ .Position = Vector3Double_Create(x, y, z), .Tangent = { 1.0f, 0.0f, 0.0f }, .Up = { 0.0f, 1.0f,
        0.0f },
        .Right = { 0.0f, 0.0f, 1.0f }, .Scale = 1.0f, .Width = 1.0f };
}

/* A mesh's measure: a box of a length along X from @p x, 1 high and 0.4 wide. */
static PathObjectMeasure MakeMeasure(float x, float length)
{
    return (PathObjectMeasure){ .Min = { x, 0.0f, -0.2f }, .Max = { x + length, 1.0f, 0.2f } };
}

/* A mesh's point through a copy. */
static Vector3Double ThroughCopy(const PropertyInstanceEntry* copy, Vector3 point)
{
    Matrix4Double Placement = WorldTransform_ComposeSheared(copy->Position, copy->Rotation, copy->Scale, copy->Shear);
    return Matrix4Double_TransformPoint(&Placement, Vector3Double_Create((double)point.x, (double)point.y, (double)point.z));
}

static bool IsVectorNear(Vector3Double a, double x, double y, double z, double tolerance)
{
    return NEAR(a.X, x, tolerance) && NEAR(a.Y, y, tolerance) && NEAR(a.Z, z, tolerance);
}

static PathObjectCopyInput MakeCopyInput(const PathObjectSlotRecord* slot, PathObjectPieceEnd start, PathObjectPieceEnd end,
    PathObjectMeasure measure)
{
    PathObjectCopyInput Input;
    Memory_Zero(&Input, sizeof(Input));
    Input.Start = start;
    Input.End = end;
    Input.Measure = measure;
    Input.Slot = slot;
    Input.Tint = (Color){ 255, 255, 255, 255 };
    for (size_t Index = 0U; Index < 5U; Index++)
    {
        Input.Jitter[Index] = 0.5f; // no jitter: the middle of its range
    }
    return Input;
}

/* The mesh of a box along X from @p fromX of length @p length: twelve triangles with flat normals, unindexed. */
static void MakeBoxMesh(float fromX, float length, float* outPositions, float* outNormals, float* outTexcoords)
{
    const float X0 = fromX;
    const float X1 = fromX + length;
    const float Corners[8][3] =
    {
        { X0, 0.0f, -0.2f }, { X1, 0.0f, -0.2f }, { X1, 1.0f, -0.2f }, { X0, 1.0f, -0.2f },
        { X0, 0.0f, 0.2f }, { X1, 0.0f, 0.2f }, { X1, 1.0f, 0.2f }, { X0, 1.0f, 0.2f }
    };
    const int Faces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 }, { 3, 7, 6, 2 }, { 0, 1, 5, 4 } };
    const float Normals[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    const int Fan[6] = { 0, 1, 2, 0, 2, 3 };
    size_t Written = 0U;
    for (size_t Face = 0U; Face < 6U; Face++)
    {
        for (size_t Corner = 0U; Corner < 6U; Corner++)
        {
            const float* Point = Corners[Faces[Face][Fan[Corner]]];
            for (size_t Axis = 0U; Axis < 3U; Axis++)
            {
                outPositions[(Written * 3U) + Axis] = Point[Axis];
                outNormals[(Written * 3U) + Axis] = Normals[Face][Axis];
            }
            outTexcoords[Written * 2U] = (Point[0] - X0) / length;
            outTexcoords[(Written * 2U) + 1U] = Point[1];
            Written++;
        }
    }
}

/* Slices of a straight piece: from @p meshFrom to @p meshTo in @p count steps, on a line from the origin along +X in the world.
 */
static void MakeLineSlices(PathObjectBendSlice* outSlices, size_t count, float meshFrom, float meshTo, float worldLength)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        float Share = (count > 1U) ? ((float)Index / (float)(count - 1U)) : 0.0f;
        outSlices[Index] = (PathObjectBendSlice){ .MeshX = meshFrom + (Share * (meshTo - meshFrom)),
            .Position = { Share * worldLength, 0.0f, 0.0f }, .AxisX = { worldLength / (meshTo - meshFrom), 0.0f, 0.0f },
            .AxisY = { 0.0f, 1.0f, 0.0f }, .AxisZ = { 0.0f, 0.0f, 1.0f } };
    }
}

/* Slices of a quarter circle of radius @p radius in X and Z, the mesh's X along it, its Z outward. */
static void MakeArcSlices(PathObjectBendSlice* outSlices, size_t count, float meshLength, float radius)
{
    float Sweep = 1.5707963f;
    float Stretch = (radius * Sweep) / meshLength;
    for (size_t Index = 0U; Index < count; Index++)
    {
        float Share = (float)Index / (float)(count - 1U);
        float Angle = Share * Sweep;
        Vector3 Tangent = { -sinf(Angle), 0.0f, cosf(Angle) };
        Vector3 Up = { 0.0f, 1.0f, 0.0f };
        Vector3 Right = Vector3CrossProduct(Tangent, Up);
        outSlices[Index] = (PathObjectBendSlice){ .MeshX = Share * meshLength,
            .Position = { radius * cosf(Angle), 0.0f, radius * sinf(Angle) }, .AxisX = Vector3Scale(Tangent, Stretch),
                .AxisY = Up,
            .AxisZ = Right };
    }
}


// Tests: the tables and what is in effect.
static void TablesPass(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(PathObject_GetModelPropertyTable()));
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(PathObject_GetSlotPropertyTable()));
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(PathObject_GetPropertyTable()));
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(WorldPathObjectComponent_GetPropertyTable()));
    TEST_CHECK_SUCCESS(run, WorldComponentTypeInfo_Check(WorldPathObjectComponent_GetTypeInfo()));
    TEST_CHECK(run, WorldComponent_GetStandardTypeInfo(WorldComponentType_PathObject) == WorldPathObjectComponent_GetTypeInfo());
    // Every row of the slot table has display text: the editor's inspector shows them.
    const PropertyTable* Tables[3] = { PathObject_GetModelPropertyTable(), PathObject_GetSlotPropertyTable(),
        PathObject_GetPropertyTable() };
    for (size_t Table = 0U; Table < 3U; Table++)
    {
        for (size_t Row = 0U; Row < Tables[Table]->DescriptorCount; Row++)
        {
            const PropertyDescriptor* Held = &Tables[Table]->Descriptors[Row];
            TEST_CHECK(run, (Held->DisplayName != NULL) && (Held->Description != NULL) && (Held->Category != NULL));
        }
    }
    // The render rows are the model instances component's, key for key and type for type: a child takes them by key.
    const PropertyTable* Instances = WorldModelInstancesComponent_GetPropertyTable();
    size_t Shared = 0U;
    for (size_t Row = 0U; Row < Instances->DescriptorCount; Row++)
    {
        const PropertyDescriptor* Own = &Instances->Descriptors[Row];
        const PropertyDescriptor* Theirs = PropertyTable_FindByKey(PathObject_GetSlotPropertyTable(), Own->Key);
        if (Theirs != NULL)
        {
            Shared++;
            TEST_CHECK(run, Theirs->Type == Own->Type);
        }
    }
    TEST_CHECK(run, Shared == 31U);
}

static void WhatIsInEffectRunsDefaultsAssetPlacement(TestRun* run)
{
    PathObjectSettings Defaults = PathObjectSettings_CreateDefault();
    TEST_CHECK(run, (Defaults.Fit == PathObjectFit_Stretch) && Defaults.SnapsToTerrain && Defaults.FitsEachSection
        && (Defaults.SeparatorEvery == 1U) && !Defaults.OverridesFit && (Defaults.Models == NULL));
    PathObjectSettings Asset = PathObjectSettings_CreateDefault();
    PathObjectSettings Placement = PathObjectSettings_CreateDefault();
    Asset.OverridesFit = true;
    Asset.Fit = PathObjectFit_Fixed;
    Asset.OverridesGap = true;
    Asset.Gap = 7.0f;
    // A value without its flag is not in effect.
    Asset.Align = PathObjectAlign_Around;
    Placement.Rounding = PathObjectRounding_Down;
    Placement.OverridesGap = true;
    Placement.Gap = 9.0f;
    PathObjectSettings AssetOnly = PathObject_ResolveSettings(&Asset, NULL);
    TEST_CHECK(run,
        (AssetOnly.Fit == PathObjectFit_Fixed) && (AssetOnly.Gap == 7.0f) && (AssetOnly.Align == PathObjectAlign_Start));
    PathObjectSettings Both = PathObject_ResolveSettings(&Asset, &Placement);
    TEST_CHECK(run, (Both.Fit == PathObjectFit_Fixed) && (Both.Gap == 9.0f) && (Both.Rounding == PathObjectRounding_Nearest));
    TEST_CHECK(run, PathObjectSettings_IsSlotUsed(&Both, PathObjectSlot_Middle) && !PathObjectSettings_IsSlotUsed(&Both,
        PathObjectSlot_EndCap));
    Placement.OverridesHasEndCap = true;
    Placement.HasEndCap = true;
    Both = PathObject_ResolveSettings(&Asset, &Placement);
    TEST_CHECK(run, PathObjectSettings_IsSlotUsed(&Both, PathObjectSlot_EndCap) && !PathObjectSettings_IsSlotUsed(&Both,
        PathObjectSlot_Count));
    PathGroundSettings Ground = PathObjectSettings_GetGround(&Both);
    TEST_CHECK(run, (Ground.Target == PathGroundTarget_Terrain) && (Ground.SearchHeight == PATH_GROUND_DEFAULT_SEARCH_HEIGHT));
}

static void ASlotsDefaultsDependOnItsKind(TestRun* run)
{
    PathObjectSlotRecord Middle = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    PathObjectSlotRecord Post = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
    PathObjectSlotRecord Single = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Single);
    TEST_CHECK(run, Middle.Stretches && (Middle.Slope == PathObjectSlope_Shear));
    TEST_CHECK(run, !Post.Stretches && (Post.Slope == PathObjectSlope_Step));
    TEST_CHECK(run, Single.Stretches && (Single.Slope == PathObjectSlope_Step));
    TEST_CHECK(run, (Middle.Collision == PathObjectCollision_Model) && (Middle.Curve == PathObjectCurve_Rigid)
        && (Middle._drawDistanceMultiplier == 1.0f));
}

/* A settings struct whose slots list holds the given records. */
static PathObjectSettings MakeSlotSettings(TestRun* run, const PathObjectSlotRecord* records, size_t count)
{
    PathObjectSettings Settings = PathObjectSettings_CreateDefault();
    TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetSlotPropertyTable(), records, count, &Settings.Slots));
    return Settings;
}

static void ASlotResolvesSettingBySetting(TestRun* run)
{
    PathObjectSlotRecord AssetRecord = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
    AssetRecord.OverridesLength = true;
    AssetRecord.Length = 0.25f;
    AssetRecord.OverridesSlope = true;
    AssetRecord.Slope = PathObjectSlope_Tilt;
    AssetRecord._overridesHasOutline = true;
    AssetRecord._hasOutline = false;
    AssetRecord._drawDistanceMultiplier = 2.0f;
    PathObjectSlotRecord PlacementRecord = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
    PlacementRecord.OverridesSlope = true;
    PlacementRecord.Slope = PathObjectSlope_Shear;
    PlacementRecord.EntryDepthEnd = 9.0f; // no flag: not in effect
    PlacementRecord._drawDistanceMultiplier = 1.5f;
    PathObjectSettings Asset = MakeSlotSettings(run, &AssetRecord, 1U);
    PathObjectSettings Placement = MakeSlotSettings(run, &PlacementRecord, 1U);

    PathObjectSlotRecord Resolved = PathObject_ResolveSlot(PathObjectSlot_Separator, &Asset, &Placement);
    TEST_CHECK(run, (Resolved.Length == 0.25f) && Resolved.OverridesLength && (Resolved.Slope == PathObjectSlope_Shear));
    TEST_CHECK(run, (Resolved.EntryDepthEnd == 0.0f) && Resolved._overridesHasOutline && !Resolved._hasOutline);
    TEST_CHECK(run, Resolved._drawDistanceMultiplier == 3.0f);
    // Another slot: its own defaults, nothing from either record.
    PathObjectSlotRecord Other = PathObject_ResolveSlot(PathObjectSlot_Middle, &Asset, &Placement);
    TEST_CHECK(run, !Other.OverridesLength && (Other.Slope == PathObjectSlope_Shear) && Other.Stretches
        && (Other._drawDistanceMultiplier == 1.0f));
    PropertyRecordList_Destroy(Asset.Slots);
    PropertyRecordList_Destroy(Placement.Slots);
}

/* Of several records of one slot in a list, the last counts. */
static void TheLastRecordOfASlotCounts(TestRun* run)
{
    PathObjectSlotRecord Records[2] = { PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle),
        PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle) };
    Records[0].OverridesLength = true;
    Records[0].Length = 1.0f;
    Records[1].OverridesLength = true;
    Records[1].Length = 2.0f;
    PathObjectSettings Placement = MakeSlotSettings(run, Records, 2U);
    PathObjectSlotRecord Resolved = PathObject_ResolveSlot(PathObjectSlot_Middle, NULL, &Placement);
    TEST_CHECK(run, Resolved.OverridesLength && (Resolved.Length == 2.0f));
    PropertyRecordList_Destroy(Placement.Slots);
}

/* The settings' ground rows are what the ground is looked for with. */
static void TheGroundSettingsComeFromTheRows(TestRun* run)
{
    PathObjectSettings Settings = PathObjectSettings_CreateDefault();
    Settings.GroundTarget = PathGroundTarget_Layers;
    Settings.GroundLayers = 5U;
    Settings.GroundSearchHeight = 7.0f;
    Settings.GroundSearchDepth = 9.0f;
    Settings.GroundOffset = -0.25f;
    PathGroundSettings Ground = PathObjectSettings_GetGround(&Settings);
    TEST_CHECK(run, (Ground.Target == PathGroundTarget_Layers) && (Ground.LayerMask == 5U) && (Ground.SearchHeight == 7.0f)
        && (Ground.SearchDepth == 9.0f) && (Ground.Offset == -0.25f));
}

static void APlacementsModelsReplaceOnlyTheSlotsTheyName(TestRun* run)
{
    PathObjectModelRecord Records[3];
    const unsigned char* Names[3] = { u8"tbp:a", u8"tbp:b", u8"tbp:c" };
    const PathObjectSlot Slots[3] = { PathObjectSlot_Middle, PathObjectSlot_Separator, PathObjectSlot_Middle };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Records[Index] = (PathObjectModelRecord){ .Slot = Slots[Index], .WorldModel = (unsigned char*)Names[Index],
            .Weight = 1.0f,
            .Tint = { 255, 255, 255, 255 } };
    }
    PathObjectSettings Asset = PathObjectSettings_CreateDefault();
    TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetModelPropertyTable(), Records, 3U, &Asset.Models));
    PathObjectModelRecord Own = { .Slot = PathObjectSlot_Separator, .WorldModel = (unsigned char*)u8"tbp:mine", .Weight = 1.0f,
        .Tint = { 255, 255, 255, 255 } };
    PathObjectSettings Placement = PathObjectSettings_CreateDefault();
    TEST_CHECK_SUCCESS(run, PropertyRecordList_Create(PathObject_GetModelPropertyTable(), &Own, 1U, &Placement.Models));

    const PathObjectModelRecord* Found[4];
    TEST_CHECK(run, PathObject_GetSlotModels(PathObjectSlot_Middle, &Asset, &Placement, Found, 4U) == 2U);
    TEST_CHECK(run, strcmp((const char*)Found[1]->WorldModel, "tbp:c") == 0);
    // Its flag off, its list says nothing.
    TEST_CHECK(run, PathObject_GetSlotModels(PathObjectSlot_Separator, &Asset, &Placement, Found, 4U) == 1U);
    TEST_CHECK(run, strcmp((const char*)Found[0]->WorldModel, "tbp:b") == 0);
    Placement.OverridesModels = true;
    TEST_CHECK(run, PathObject_GetSlotModels(PathObjectSlot_Separator, &Asset, &Placement, Found, 4U) == 1U);
    TEST_CHECK(run, strcmp((const char*)Found[0]->WorldModel, "tbp:mine") == 0);
    // counts past the capacity
    TEST_CHECK(run, PathObject_GetSlotModels(PathObjectSlot_Middle, &Asset, &Placement, Found, 1U) == 2U);
    TEST_CHECK(run, PathObject_GetSlotModels(PathObjectSlot_Corner, &Asset, &Placement, Found, 4U) == 0U);
    PropertyRecordList_Destroy(Asset.Models);
    PropertyRecordList_Destroy(Placement.Models);
}


// Tests: the asset.
/* Builds a definition from JSON through the real type; the error it gives back, when it does. */
static Error BuildDefinition(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Raw;
    GenericBuffer_AllocateVariable(&Raw, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Raw, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.PathObject, &Raw, (const unsigned char*)u8"test",
        &Definition);
    Memory_Free(Raw._data);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetManager_SetDefinition(fixture->Manager, Definition);
    }
    return Result;
}

static void ADefinitionsWrittenSettingsAreInEffect(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    TestAssets_AddModel(run, &Fixture, "panel", (BoundingBox){ { 0.0f, 0.0f, -0.1f }, { 2.0f, 1.0f, 0.1f } }, NULL);
    TestAssets_AddModel(run, &Fixture, "post", (BoundingBox){ { -0.1f, 0.0f, -0.1f }, { 0.1f, 1.0f, 0.1f } }, NULL);
    TestAssets_AddWorldModelOf(run, &Fixture, "panel", "panel");
    TestAssets_AddWorldModelOf(run, &Fixture, "post", "post");
    TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, "{ \"name\": \"fence\", \"fit\": \"fixed\", \"overrides_align\": false, "
        "\"align\": \"evenly\", \"has_separator\": true, \"models\": [ { \"slot\": \"middle\", \"world_model\": \"panel\" }, "
        "{ \"slot\": \"separator\", \"world_model\": \"post\", \"weight\": 2 } ], \"slots\": [ { \"slot\": \"separator\", "
        "\"length\": 0.5, \"curve\": \"bent\", \"overrides_slope\": false, \"slope\": \"tilt\" } ] }"));
    AssetUserID User;
    TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User));
    PathObject* Asset = NULL;
    if (TEST_CHECK_SUCCESS(run, AssetManager_LoadPathObject(Fixture.Manager, (const unsigned char*)u8"fence", User, &Asset)))
    {
        const PathObjectSettings* Settings = PathObject_GetSettings(Asset);
        PathObjectSettings InEffect = PathObject_ResolveSettings(Settings, NULL);
        // Written: its flag was put in. A flag written false stays: the value is kept, out of effect.
        TEST_CHECK(run, Settings->OverridesFit && (InEffect.Fit == PathObjectFit_Fixed) && InEffect.HasSeparator);
        TEST_CHECK(run, !Settings->OverridesAlign && (Settings->Align == PathObjectAlign_Evenly)
            && (InEffect.Align == PathObjectAlign_Start));
        PathObjectSlotRecord Separator = PathObject_ResolveSlot(PathObjectSlot_Separator, Settings, NULL);
        TEST_CHECK(run, Separator.OverridesLength && (Separator.Length == 0.5f) && (Separator.Curve == PathObjectCurve_Bent)
            && (Separator.Slope == PathObjectSlope_Step));
        const PathObjectModelRecord* Found[2];
        TEST_CHECK(run, PathObject_GetSlotModels(PathObjectSlot_Separator, Settings, NULL, Found, 2U) == 1U);
        TEST_CHECK(run, (Found[0]->Weight == 2.0f) && (strcmp((const char*)Found[0]->WorldModel, "tbp:post") == 0));
        TEST_CHECK_SUCCESS(run, AssetManager_ReleaseAllAssetsForUser(Fixture.Manager, User));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void ADefinitionIsCheckedWhenItIsRead(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    const char* Refused[] =
    {
        // No middle.
        "{ \"name\": \"a\" }",
        "{ \"name\": \"b\", \"models\": [ { \"slot\": \"separator\", \"world_model\": \"m\" } ] }",
        // A flag on with no model for it.
        "{ \"name\": \"c\", \"has_end_cap\": true, \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\" } ] }",
        // An unknown key at the root and in each kind of record.
        "{ \"name\": \"d\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\" } ], \"fits\": \"stretch\" }",
        "{ \"name\": \"e\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\", \"size\": 1 } ] }",
        "{ \"name\": \"f\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\" } ], \"slots\": [ { \"slot\": "
            "\"middle\", \"curvy\": 1 } ] }",
        // A rule of a row: an enum's name, a negative length, a stretch above 1, a weight of 0.
        "{ \"name\": \"g\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\" } ], \"fit\": \"tight\" }",
        "{ \"name\": \"h\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\" } ], \"slots\": [ { \"slot\": "
            "\"middle\", \"length\": -1 } ] }",
        "{ \"name\": \"i\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\" } ], \"minimum_stretch\": 1.5 }",
        "{ \"name\": \"j\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\", \"weight\": 0 } ] }",
        // A model with no name, and a slot twice.
        "{ \"name\": \"k\", \"models\": [ { \"slot\": \"middle\" } ] }",
        "{ \"name\": \"l\", \"models\": [ { \"slot\": \"middle\", \"world_model\": \"m\" } ], \"slots\": [ { \"slot\": "
            "\"middle\" }, { \"slot\": \"middle\" } ] }",
    };
    for (size_t Index = 0U; Index < (sizeof(Refused) / sizeof(Refused[0])); Index++)
    {
        Error Result = BuildDefinition(&Fixture, Refused[Index]);
        TEST_CHECK(run, Result.Code != ErrorCode_Success);
        Error_Deconstruct(&Result);
    }
    // A world model that does not load fails the asset, naming it.
    TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, "{ \"name\": \"z\", \"models\": [ { \"slot\": \"middle\", \"world_model\": "
        "\"missing\" } ] }"));
    AssetUserID User;
    TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User));
    PathObject* Asset = NULL;
    Error Result = AssetManager_LoadPathObject(Fixture.Manager, (const unsigned char*)u8"z", User, &Asset);
    TEST_CHECK(run, (Result.Code != ErrorCode_Success) && (Result.Message != NULL) && (strstr((const char*)Result.Message,
        "missing") != NULL));
    Error_Deconstruct(&Result);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: measuring.
static void AMeshIsMeasuredUnderItsImportTransform(TestRun* run)
{
    const float Positions[9] = { 1.0f, 0.0f, 0.0f, 3.0f, 2.0f, 0.5f, 2.0f, 1.0f, -0.5f };
    PathObjectMeasure Measure = PathObjectMeasure_CreateEmpty();
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    TEST_CHECK(run, PathObjectMeasure_GetLength(&Measure, &Slot) == 0.0f); // holding nothing
    TEST_CHECK(run, PathObjectMeasure_GetBodyCenter(&Measure, &Slot) == 0.0f);
    PathObjectMeasure_AddVertices(&Measure, Positions, 3U, MatrixTranslate(10.0f, 0.0f, 0.0f));
    TEST_CHECK(run, NEAR(Measure.Min.x, 11.0, 1.0e-6) && NEAR(Measure.Max.x, 13.0, 1.0e-6) && NEAR(Measure.Max.y, 2.0, 1.0e-6));
    TEST_CHECK(run, NEAR(PathObjectMeasure_GetLength(&Measure, &Slot), 2.0, 1.0e-6));
    TEST_CHECK(run, NEAR(PathObjectMeasure_GetBodyCenter(&Measure, &Slot), 12.0, 1.0e-6));
    // Entry depth: the mesh reaches that much past its length.
    Slot.EntryDepthStart = 0.25f;
    Slot.EntryDepthEnd = 0.5f;
    TEST_CHECK(run, NEAR(PathObjectMeasure_GetLength(&Measure, &Slot), 1.25, 1.0e-6));
    TEST_CHECK(run, NEAR(PathObjectMeasure_GetBodyCenter(&Measure, &Slot), 11.875, 1.0e-6));
    // A length the setting gives: the mesh keeps its body's middle.
    Slot.OverridesLength = true;
    Slot.Length = 0.0f;
    TEST_CHECK(run, PathObjectMeasure_GetLength(&Measure, &Slot) == 0.0f);
    Slot.EntryDepthStart = 5.0f; // more than the mesh is long: never below 0
    Slot.EntryDepthEnd = 5.0f;
    Slot.OverridesLength = false;
    TEST_CHECK(run, PathObjectMeasure_GetLength(&Measure, &Slot) == 0.0f);
    // A box where no vertices are at hand: its corners, through a turn.
    PathObjectMeasure Box = PathObjectMeasure_CreateEmpty();
    PathObjectMeasure_AddBox(&Box, (BoundingBox){ { -1.0f, 0.0f, -2.0f }, { 1.0f, 1.0f, 2.0f } }, MatrixRotateY(1.5707963f));
    TEST_CHECK(run, NEAR(Box.Max.x, 2.0, 1.0e-5) && NEAR(Box.Min.x, -2.0, 1.0e-5));
}


// Tests: rigid copies.
static void ACopyLandsOnItsEnds(TestRun* run)
{
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    PathObjectMeasure Measure = MakeMeasure(3.0f, 2.0f);
    PathObjectPieceEnd Start = MakeEnd(10.0, 1.0, 4.0);
    PathObjectPieceEnd End = MakeEnd(13.0, 2.5, 8.0); // climbing
    const Vector3 MeshStart = { 3.0f, 0.0f, 0.0f };
    const Vector3 MeshEnd = { 5.0f, 0.0f, 0.0f };
    PathObjectSlope Slopes[2] = { PathObjectSlope_Shear, PathObjectSlope_Tilt };
    for (size_t Index = 0U; Index < 2U; Index++)
    {
        Slot.Slope = Slopes[Index];
        PathObjectCopyInput Input = MakeCopyInput(&Slot, Start, End, Measure);
        PropertyInstanceEntry Copy;
        TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
        // Whatever the slope's rule, the mesh's two ends land on the piece's.
        TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, MeshStart), 10.0, 1.0, 4.0, COPY_TOLERANCE));
        TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, MeshEnd), 13.0, 2.5, 8.0, COPY_TOLERANCE));
        // Turned round, they swap.
        Input.IsTurned = true;
        TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
        TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, MeshEnd), 10.0, 1.0, 4.0, COPY_TOLERANCE));
        TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, MeshStart), 13.0, 2.5, 8.0, COPY_TOLERANCE));
    }
    // Shear keeps its verticals vertical: the mesh's top stands straight over its foot, at the world's Y.
    Slot.Slope = PathObjectSlope_Shear;
    PathObjectCopyInput Input = MakeCopyInput(&Slot, Start, End, Measure);
    PropertyInstanceEntry Copy;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 3.0f, 1.0f, 0.0f }), 10.0, 2.0, 4.0, COPY_TOLERANCE));
    TEST_CHECK(run, Copy.Shear != 0.0f);
    // Tilt turns them with the slope: square to its length, no shear.
    Slot.Slope = PathObjectSlope_Tilt;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, Copy.Shear == 0.0f);
    Vector3Double Top = ThroughCopy(&Copy, (Vector3){ 3.0f, 1.0f, 0.0f });
    Vector3Double Rise = Vector3Double_Subtract(Top, Start.Position);
    Vector3Double Along = Vector3Double_Subtract(End.Position, Start.Position);
    TEST_CHECK(run, NEAR(Vector3Double_Dot(Rise, Along), 0.0, COPY_TOLERANCE) && NEAR(Vector3Double_Length(Rise), 1.0,
        COPY_TOLERANCE));
    // Across it, a turned copy's +Z swaps sides, as a half turn does: nothing mirrors.
    Input.IsTurned = false;
    Vector3Double Right = ThroughCopy(&Copy, (Vector3){ 3.0f, 0.0f, 0.1f });
    Input.IsTurned = true;
    PropertyInstanceEntry Turned;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Turned));
    Vector3Double TurnedRight = ThroughCopy(&Turned, (Vector3){ 5.0f, 0.0f, 0.1f });
    TEST_CHECK(run, NEAR(Vector3Double_Dot(Vector3Double_Subtract(Right, Start.Position), Vector3Double_Subtract(TurnedRight,
        Start.Position)),
        -0.01, COPY_TOLERANCE));
}

static void ALevelPieceTakesTheHeightItsAnchorSays(TestRun* run)
{
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
    Slot.Slope = PathObjectSlope_Step;
    PathObjectMeasure Measure = MakeMeasure(0.0f, 2.0f);
    PathObjectPieceEnd Start = MakeEnd(0.0, 1.0, 0.0);
    PathObjectPieceEnd End = MakeEnd(2.0, 3.0, 0.0);
    struct { PathObjectStepAnchor Anchor; double Height; } Cases[3] =
    {
        { PathObjectStepAnchor_Middle, 2.0 }, { PathObjectStepAnchor_Higher, 3.0 }, { PathObjectStepAnchor_Lower, 1.0 }
    };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Slot.StepAnchor = Cases[Index].Anchor;
        PathObjectCopyInput Input = MakeCopyInput(&Slot, Start, End, Measure);
        PropertyInstanceEntry Copy;
        TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
        // Level: both ends of its foot at the height; its length is the chord's across.
        TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 0.0f, 0.0f, 0.0f }), 0.0, Cases[Index].Height, 0.0,
            COPY_TOLERANCE));
        TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 2.0f, 0.0f, 0.0f }), 2.0, Cases[Index].Height, 0.0,
            COPY_TOLERANCE));
    }
}

static void AChordNearlyAlongTheUpTiltsInsteadOfShearing(TestRun* run)
{
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    PathObjectMeasure Measure = MakeMeasure(0.0f, 1.0f);
    PathObjectPieceEnd Start = MakeEnd(0.0, 0.0, 0.0);
    PathObjectPieceEnd End = MakeEnd(0.01, 1.0, 0.0);
    PathObjectCopyInput Input = MakeCopyInput(&Slot, Start, End, Measure);
    PropertyInstanceEntry Copy;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, Copy.Shear == 0.0f);
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 1.0f, 0.0f, 0.0f }), 0.01, 1.0, 0.0, COPY_TOLERANCE));
    // A piece with no length: nothing flat, its own size, along the curve's tangent.
    Slot.OverridesLength = true;
    Slot.Length = 0.0f;
    PathObjectCopyInput Point = MakeCopyInput(&Slot, Start, Start, MakeMeasure(-0.1f, 0.2f));
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Point, &Copy));
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 0.1f, 0.5f, 0.0f }), 0.1, 0.5, 0.0, COPY_TOLERANCE));
    // Anything not finite is refused, and the copy left alone.
    PathObjectPieceEnd Broken = Start;
    Broken.Position.X = NAN;
    Point = MakeCopyInput(&Slot, Broken, Start, Measure);
    PropertyInstanceEntry Kept = PropertyInstanceEntry_CreateDefault();
    Kept.Shear = 3.0f;
    TEST_CHECK(run, !PathObjectPlacement_MakeCopy(&Point, &Kept) && (Kept.Shear == 3.0f));
}

static void OffsetsAndTheFramesScaleApply(TestRun* run)
{
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    Slot.Slope = PathObjectSlope_Tilt;
    PathObjectMeasure Measure = MakeMeasure(0.0f, 2.0f);
    PathObjectPieceEnd Start = MakeEnd(0.0, 0.0, 0.0);
    PathObjectPieceEnd End = MakeEnd(4.0, 0.0, 0.0);
    Start.Scale = 3.0f;
    End.Scale = 1.0f;
    Start.Width = 0.5f;
    End.Width = 0.5f;
    PathObjectCopyInput Input = MakeCopyInput(&Slot, Start, End, Measure);
    PropertyInstanceEntry Copy;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    // Its height is the scale, its width the scale times the width; its length the chord's.
    TEST_CHECK(run, NEAR(Copy.Scale.x, 2.0, 1.0e-6) && NEAR(Copy.Scale.y, 2.0, 1.0e-6) && NEAR(Copy.Scale.z, 1.0, 1.0e-6));
    Slot.PositionOffset = (Vector3){ 1.0f, 2.0f, 3.0f };
    Input = MakeCopyInput(&Slot, MakeEnd(0.0, 0.0, 0.0), MakeEnd(2.0, 0.0, 0.0), Measure);
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 0.0f, 0.0f, 0.0f }), 1.0, 2.0, 3.0, COPY_TOLERANCE));
    // A rotation offset turns it on top of its frame, about the body's middle's place.
    Slot.PositionOffset = (Vector3){ 0.0f, 0.0f, 0.0f };
    Slot.RotationOffset = (Vector3){ 0.0f, 1.5707963f, 0.0f };
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, NEAR(Copy.Rotation.y, 1.5707963, 1.0e-5));
    Slot.RotationOffset = (Vector3){ 0.0f, 0.0f, 0.0f };
    // Jitter: off its place by at most its setting, by the piece's random values; the size too.
    Slot.PositionJitter = (Vector3){ 1.0f, 0.0f, 0.0f };
    Slot.ScaleJitter = 0.5f;
    Input.Jitter[0] = 1.0f;
    Input.Jitter[4] = 1.0f;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 0.0f, 0.0f, 0.0f }), 1.0, 0.0, 0.0, COPY_TOLERANCE));
    TEST_CHECK(run, NEAR(Copy.Scale.y, 1.5, 1.0e-6));
    // Aligned to the world: the object's axes, whatever the chord, its place the chord's middle.
    Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    Slot.AlignedTo = PathObjectAlignedTo_World;
    Input = MakeCopyInput(&Slot, MakeEnd(2.0, 0.0, 2.0), MakeEnd(4.0, 0.0, 6.0), MakeMeasure(-0.5f, 1.0f));
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 0.5f, 0.0f, 0.0f }), 3.5, 0.0, 4.0, COPY_TOLERANCE));
}


// Tests: bending.
/* What lies along a tilted piece's up lies square to its chord, not straight up. */
static void ATiltedPiecesOffsetLiesSquareToIt(TestRun* run)
{
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    Slot.Slope = PathObjectSlope_Tilt;
    PathObjectCopyInput Input = MakeCopyInput(&Slot, MakeEnd(0.0, 0.0, 0.0), MakeEnd(3.0, 4.0, 0.0), MakeMeasure(0.0f, 5.0f));
    PropertyInstanceEntry Plain;
    PropertyInstanceEntry Lifted;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Plain));
    Slot.PositionOffset = (Vector3){ 0.0f, 1.0f, 0.0f };
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Lifted));
    Vector3Double Moved = Vector3Double_Subtract(Lifted.Position, Plain.Position);
    TEST_CHECK(run, IsVectorNear(Moved, -0.8, 0.6, 0.0, COPY_TOLERANCE));
}

/* A rotation offset and a yaw jitter turn a piece through its body's middle, wherever the mesh's origin lies. */
static void ATurnGoesThroughThePiecesMiddle(TestRun* run)
{
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Middle);
    PathObjectCopyInput Input = MakeCopyInput(&Slot, MakeEnd(0.0, 0.0, 0.0), MakeEnd(2.0, 0.0, 0.0), MakeMeasure(0.0f, 2.0f));
    const Vector3 BodyMiddle = { 1.0f, 0.0f, 0.0f };
    PropertyInstanceEntry Copy;
    Slot.RotationOffset = (Vector3){ 0.0f, 1.5707963f, 0.0f };
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, BodyMiddle), 1.0, 0.0, 0.0, COPY_TOLERANCE));
    Slot.RotationOffset = (Vector3){ 0.0f, 0.0f, 0.0f };
    Slot.YawJitter = 1.5707963f;
    Input.Jitter[3] = 1.0f;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, BodyMiddle), 1.0, 0.0, 0.0, COPY_TOLERANCE));
    // And on a mesh off its origin, turned round and sheared up a slope, the middle still lies on the chord's.
    Slot.YawJitter = 0.0f;
    Slot.Slope = PathObjectSlope_Shear;
    Input = MakeCopyInput(&Slot, MakeEnd(0.0, 0.0, 0.0), MakeEnd(2.0, 1.0, 0.0), MakeMeasure(3.0f, 2.0f));
    Input.IsTurned = true;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, IsVectorNear(ThroughCopy(&Copy, (Vector3){ 4.0f, 0.0f, 0.0f }), 1.0, 0.5, 0.0, COPY_TOLERANCE));
}

/* A piece that keeps its length has it on a slope too: a post stays as thick. Otherwise its X spans the chord's run. */
static void APieceThatKeepsItsLengthHasItOnASlope(TestRun* run)
{
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
    PathObjectCopyInput Input = MakeCopyInput(&Slot, MakeEnd(0.0, 0.0, 0.0), MakeEnd(0.2, 0.2, 0.0), MakeMeasure(-0.1f, 0.2f));
    PropertyInstanceEntry Copy;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, NEAR(Copy.Scale.x, 1.0, 1.0e-5));
    Input.KeepsLength = true;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, NEAR(Copy.Scale.x, 1.0, 1.0e-5));
    Input = MakeCopyInput(&Slot, MakeEnd(0.0, 0.0, 0.0), MakeEnd(0.1, 0.1, 0.0), MakeMeasure(-0.1f, 0.2f));
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, NEAR(Copy.Scale.x, 0.5, 1.0e-5));
    Input.KeepsLength = true;
    Input.Start.Scale = 2.0f;
    Input.End.Scale = 2.0f;
    TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
    TEST_CHECK(run, NEAR(Copy.Scale.x, 2.0, 1.0e-5));
}

/* A yaw jitter turns a piece about its up, by the jitter's value: none at the middle of its range. */
static void AYawJitterTurnsAPieceAboutItsUp(TestRun* run)
{
    PathObjectSlotRecord Slot = PathObjectSlotRecord_CreateDefault(PathObjectSlot_Separator);
    Slot.YawJitter = 0.5f;
    PathObjectCopyInput Input = MakeCopyInput(&Slot, MakeEnd(0.0, 0.0, 0.0), MakeEnd(2.0, 0.0, 0.0), MakeMeasure(0.0f, 2.0f));
    const float Values[3] = { 0.0f, 0.5f, 1.0f };
    const double Turns[3] = { -0.5, 0.0, 0.5 };
    for (size_t Index = 0U; Index < 3U; Index++)
    {
        Input.Jitter[3] = Values[Index];
        PropertyInstanceEntry Copy;
        TEST_CHECK(run, PathObjectPlacement_MakeCopy(&Input, &Copy));
        Vector3Double Along = Vector3Double_Subtract(ThroughCopy(&Copy, (Vector3){ 1.0f, 0.0f, 0.0f }), ThroughCopy(&Copy,
            (Vector3){ 0.0f, 0.0f, 0.0f }));
        // About +Y a positive turn takes +X toward -Z.
        TEST_CHECK(run, IsVectorNear(Along, cos(Turns[Index]), 0.0, -sin(Turns[Index]), COPY_TOLERANCE));
    }
}

/* Between two slices a point goes by the lerp of their axes as well as of their places. */
static void BetweenSlicesTheAxesLerpToo(TestRun* run)
{
    const float Positions[9] = { 1.0f, 1.0f, 1.0f, 1.25f, 1.0f, 1.0f, 1.0f, 1.25f, 1.0f };
    const float Normals[9] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    PathObjectSourceMesh Source = { .Positions = Positions, .Normals = Normals, .VertexCount = 3U,
        .Transform = MatrixIdentity() };
    PathObjectBendSlice Slices[2] =
    {
        { .MeshX = 0.0f, .Position = { 0.0f, 0.0f, 0.0f }, .AxisX = { 1.0f, 0.0f, 0.0f }, .AxisY = { 0.0f, 1.0f, 0.0f },
            .AxisZ = { 0.0f, 0.0f, 1.0f } },
        { .MeshX = 2.0f, .Position = { 10.0f, 2.0f, 0.0f }, .AxisX = { 1.0f, 0.0f, 0.0f }, .AxisY = { 0.0f, 3.0f, 0.0f },
            .AxisZ = { 0.0f, 0.0f, 5.0f } }
    };
    PathObjectBentMesh Bent;
    PathObjectBentMesh_Construct(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 2U, -1.0e30f, 1.0e30f, 1000U));
    if (TEST_CHECK(run, PathObjectBentMesh_GetVertexCount(&Bent) == 3U))
    {
        // At mesh X 1, half way: the place (5, 1, 0), the axes Y (0, 2, 0) and Z (0, 0, 3).
        const float* First = (const float*)Bent.Positions._data;
        TEST_CHECK(run, NEAR(First[0], 5.0, COPY_TOLERANCE) && NEAR(First[1], 3.0, COPY_TOLERANCE) && NEAR(First[2], 3.0,
            COPY_TOLERANCE));
    }
    PathObjectBentMesh_Deconstruct(&Bent);
}

/* An import transform that flattens a mesh has no inverse for its normals: they come out finite all the same. */
static void AFlatteningImportTransformLeavesNormalsFinite(TestRun* run)
{
    const float Positions[9] = { 0.5f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f, 1.0f };
    const float Normals[9] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
    PathObjectSourceMesh Source = { .Positions = Positions, .Normals = Normals, .VertexCount = 3U, .Transform = MatrixScale(1.0f,
        0.0f, 1.0f) };
    PathObjectBendSlice Slices[2] =
    {
        { .MeshX = 0.0f, .Position = { 0.0f, 0.0f, 0.0f }, .AxisX = { 1.0f, 0.0f, 0.0f }, .AxisY = { 0.0f, 1.0f, 0.0f },
            .AxisZ = { 0.0f, 0.0f, 1.0f } },
        { .MeshX = 2.0f, .Position = { 2.0f, 0.0f, 0.0f }, .AxisX = { 1.0f, 0.0f, 0.0f }, .AxisY = { 0.0f, 1.0f, 0.0f },
            .AxisZ = { 0.0f, 0.0f, 1.0f } }
    };
    PathObjectBentMesh Bent;
    PathObjectBentMesh_Construct(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 2U, -1.0e30f, 1.0e30f, 1000U));
    const float* Normal = (const float*)Bent.Normals._data;
    TEST_CHECK(run,
        (PathObjectBentMesh_GetVertexCount(&Bent) == 3U) && isfinite(Normal[0]) && isfinite(Normal[1]) && isfinite(Normal[2]));
    PathObjectBentMesh_Deconstruct(&Bent);
}

/* A normal stays square to its surface under a frame that shears it. */
static void ANormalStaysSquareUnderAShearedFrame(TestRun* run)
{
    const float Positions[9] = { 0.5f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f, 1.0f };
    const float Normals[9] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    PathObjectSourceMesh Source = { .Positions = Positions, .Normals = Normals, .VertexCount = 3U,
        .Transform = MatrixIdentity() };
    PathObjectBendSlice Slices[2] =
    {
        { .MeshX = 0.0f, .Position = { 0.0f, 0.0f, 0.0f }, .AxisX = { 1.0f, 1.0f, 0.0f }, .AxisY = { 0.0f, 1.0f, 0.0f },
            .AxisZ = { 0.0f, 0.0f, 1.0f } },
        { .MeshX = 2.0f, .Position = { 2.0f, 2.0f, 0.0f }, .AxisX = { 1.0f, 1.0f, 0.0f }, .AxisY = { 0.0f, 1.0f, 0.0f },
            .AxisZ = { 0.0f, 0.0f, 1.0f } }
    };
    PathObjectBentMesh Bent;
    PathObjectBentMesh_Construct(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 2U, -1.0e30f, 1.0e30f, 1000U));
    if (TEST_CHECK(run, PathObjectBentMesh_GetVertexCount(&Bent) == 3U))
    {
        const float* Normal = (const float*)Bent.Normals._data;
        TEST_CHECK(run, NEAR(Normal[0], -0.70710678, COPY_TOLERANCE) && NEAR(Normal[1], 0.70710678,
            COPY_TOLERANCE) && NEAR(Normal[2], 0.0, COPY_TOLERANCE));
    }
    PathObjectBentMesh_Deconstruct(&Bent);
}

static void ASlicedBoxKeepsItsSurface(TestRun* run)
{
    float Positions[108];
    float Normals[108];
    float Texcoords[72];
    MakeBoxMesh(0.0f, 2.0f, Positions, Normals, Texcoords);
    PathObjectSourceMesh Source = { .Positions = Positions, .Normals = Normals, .Texcoords = Texcoords, .VertexCount = 36U,
        .Transform = MatrixIdentity() };
    PathObjectBendSlice Slices[5];
    MakeLineSlices(Slices, 5U, 0.0f, 2.0f, 4.0f);
    PathObjectBentMesh Bent;
    PathObjectBentMesh_Construct(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 5U, -1.0e30f, 1.0e30f, 100000U));
    // Stretched along a line, the box is a box twice as long: its surface area, and no corner off it.
    const float* Places = (const float*)Bent.Positions._data;
    size_t Count = PathObjectBentMesh_GetVertexCount(&Bent);
    TEST_CHECK(run, ((Count % 3U) == 0U) && (Count > 36U));
    double Area = 0.0;
    for (size_t Triangle = 0U; (Triangle + 2U) < Count; Triangle += 3U)
    {
        const float* A = &Places[Triangle * 3U];
        const float* B = &Places[(Triangle * 3U) + 3U];
        const float* C = &Places[(Triangle * 3U) + 6U];
        Vector3 Ab = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
        Vector3 Ac = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
        Area += 0.5 * (double)Vector3Length(Vector3CrossProduct(Ab, Ac));
    }
    // Two sides 4 x 0.4, two 4 x 1, two ends 0.4 x 1.
    TEST_CHECK(run, NEAR(Area, (2.0 * 4.0 * 0.4) + (2.0 * 4.0 * 1.0) + (2.0 * 0.4 * 1.0), 1.0e-3));
    TEST_CHECK(run, NEAR(Bent.BoxMin.x, 0.0, 1.0e-5) && NEAR(Bent.BoxMax.x, 4.0, 1.0e-5) && NEAR(Bent.BoxMax.y, 1.0, 1.0e-5)
        && NEAR(Bent.BoxMax.z, 0.2, 1.0e-5));
    // Every triangle lies inside one slab: none crosses a slice's plane.
    for (size_t Triangle = 0U; (Triangle + 2U) < Count; Triangle += 3U)
    {
        double Min = 1.0e30;
        double Max = -1.0e30;
        for (size_t Corner = 0U; Corner < 3U; Corner++)
        {
            double X = (double)Places[(Triangle + Corner) * 3U];
            Min = fmin(Min, X);
            Max = fmax(Max, X);
        }
        TEST_CHECK(run, ((Max - Min) < 1.0e-4) || (floor((Min + 1.0e-5) / 1.0) == floor((Max - 1.0e-5) / 1.0)));
    }
    // What the source has comes along, split corners lerped; what it lacks stays out.
    TEST_CHECK(run, (Bent.Texcoords._count == (Count * 2U)) && (Bent.Tangents._count == 0U) && (Bent.Colors._count == 0U)
        && (Bent.Normals._count == (Count * 3U)));
    const float* Uv = (const float*)Bent.Texcoords._data;
    for (size_t Vertex = 0U; Vertex < Count; Vertex++)
    {
        // The texture coordinate that said "share of the length" says where it landed, in a quarter of 4 m.
        double Expected = (double)Places[Vertex * 3U] / 4.0;
        TEST_CHECK(run, NEAR(Uv[Vertex * 2U], Expected, 1.0e-4));
    }
    PathObjectBentMesh_Deconstruct(&Bent);
}

static void ABentBoxFollowsItsSlices(TestRun* run)
{
    float Positions[108];
    float Normals[108];
    float Texcoords[72];
    MakeBoxMesh(0.0f, 2.0f, Positions, Normals, Texcoords);
    PathObjectSourceMesh Source = { .Positions = Positions, .Normals = Normals, .Texcoords = Texcoords, .VertexCount = 36U,
        .Transform = MatrixIdentity() };
    enum { SliceCount = 33 };
    PathObjectBendSlice Slices[SliceCount];
    float Radius = 10.0f;
    MakeArcSlices(Slices, SliceCount, 2.0f, Radius);
    PathObjectBentMesh Bent;
    PathObjectBentMesh_Construct(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, SliceCount, -1.0e30f, 1.0e30f, 100000U));
    const float* Places = (const float*)Bent.Positions._data;
    const float* Turned = (const float*)Bent.Normals._data;
    size_t Count = PathObjectBentMesh_GetVertexCount(&Bent);
    TEST_CHECK(run, Count > 1000U);
    for (size_t Vertex = 0U; Vertex < Count; Vertex++)
    {
        const float* P = &Places[Vertex * 3U];
        const float* N = &Turned[Vertex * 3U];
        // Every point lies within the box's width of the circle, with its up kept.
        double Off = sqrt(((double)P[0] * (double)P[0]) + ((double)P[2] * (double)P[2])) - (double)Radius;
        TEST_CHECK(run, (Off >= -0.21) && (Off <= 0.21) && (P[1] >= -1.0e-5f) && (P[1] <= 1.00001f));
        // Normals stay unit, and a side's stays radial: its Z-facing faces point away from or toward the centre.
        double Length = sqrt(((double)N[0] * (double)N[0]) + ((double)N[1] * (double)N[1]) + ((double)N[2] * (double)N[2]));
        TEST_CHECK(run, NEAR(Length, 1.0, 1.0e-3));
        if (fabs((double)N[1]) < 0.5)
        {
            double Radial = (((double)P[0] * (double)N[0]) + ((double)P[2] * (double)N[2])) / sqrt(((double)P[0] * (double)P[0])
                + ((double)P[2] * (double)P[2]));
            double Along = fabs((fabs(Radial) - 1.0));
            // A side's normal is radial; an end cap's lies along the tangent, its radial part 0.
            TEST_CHECK(run, (Along < 0.02) || (fabs(Radial) < 0.02));
        }
    }
    // Winding stays what it was: a triangle's normal agrees with its corners' normals.
    for (size_t Triangle = 0U; (Triangle + 2U) < Count; Triangle += 3U)
    {
        const float* A = &Places[Triangle * 3U];
        const float* B = &Places[(Triangle * 3U) + 3U];
        const float* C = &Places[(Triangle * 3U) + 6U];
        Vector3 Face = Vector3CrossProduct((Vector3){ B[0] - A[0], B[1] - A[1], B[2] - A[2] },
            (Vector3){ C[0] - A[0], C[1] - A[1], C[2] - A[2] });
        const float* N = &Turned[Triangle * 3U];
        if (Vector3Length(Face) > 1.0e-5f)
        {
            TEST_CHECK(run, ((Face.x * N[0]) + (Face.y * N[1]) + (Face.z * N[2])) > 0.0f);
        }
    }
    PathObjectBentMesh_Deconstruct(&Bent);
}

static void WhatReachesPastTheSlicesCarriesOnStraight(TestRun* run)
{
    float Positions[108];
    float Normals[108];
    float Texcoords[72];
    MakeBoxMesh(-0.5f, 3.0f, Positions, Normals, Texcoords); // from -0.5 to 2.5: entry depths past the body from 0 to 2
    PathObjectSourceMesh Source = { .Positions = Positions, .Normals = Normals, .Texcoords = Texcoords, .VertexCount = 36U,
        .Transform = MatrixIdentity() };
    PathObjectBendSlice Slices[2];
    MakeLineSlices(Slices, 2U, 0.0f, 2.0f, 2.0f);
    PathObjectBentMesh Bent;
    PathObjectBentMesh_Construct(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 2U, -1.0e30f, 1.0e30f, 100000U));
    TEST_CHECK(run, NEAR(Bent.BoxMin.x, -0.5, 1.0e-5) && NEAR(Bent.BoxMax.x, 2.5, 1.0e-5));
    // Cut at the planes: what lies outside is gone, nothing is added to fill it.
    PathObjectBentMesh_Clear(&Bent);
    TEST_CHECK(run, PathObjectBentMesh_GetVertexCount(&Bent) == 0U);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 2U, 0.25f, 1.5f, 100000U));
    TEST_CHECK(run, NEAR(Bent.BoxMin.x, 0.25, 1.0e-5) && NEAR(Bent.BoxMax.x, 1.5, 1.0e-5));
    // A cut past the whole mesh leaves nothing.
    PathObjectBentMesh_Clear(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 2U, 10.0f, 20.0f, 100000U));
    TEST_CHECK(run, PathObjectBentMesh_GetVertexCount(&Bent) == 0U);
    // The import transform comes first.
    Source.Transform = MatrixTranslate(0.5f, 0.0f, 0.0f);
    PathObjectBentMesh_Clear(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 2U, -1.0e30f, 1.0e30f, 100000U));
    TEST_CHECK(run, NEAR(Bent.BoxMin.x, 0.0, 1.0e-5) && NEAR(Bent.BoxMax.x, 3.0, 1.0e-5));
    PathObjectBentMesh_Deconstruct(&Bent);
}

static void AnIndexedMeshBendsAsAnUnindexedOne(TestRun* run)
{
    // A quad of two triangles with colours and tangents, indexed.
    const float Positions[12] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    const float Normals[12] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
    const float Tangents[16] = { 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f };
    const unsigned char Colors[16] = { 255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255 };
    const unsigned short Indices[6] = { 0, 1, 2, 0, 2, 3 };
    PathObjectSourceMesh Source = { .Positions = Positions, .Normals = Normals, .Tangents = Tangents, .Colors = Colors,
        .VertexCount = 4U, .Indices = Indices, .TriangleCount = 2U, .Transform = MatrixIdentity() };
    PathObjectBendSlice Slices[3];
    MakeLineSlices(Slices, 3U, 0.0f, 1.0f, 1.0f);
    PathObjectBentMesh Bent;
    PathObjectBentMesh_Construct(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 3U, -1.0e30f, 1.0e30f, 100000U));
    size_t Count = PathObjectBentMesh_GetVertexCount(&Bent);
    TEST_CHECK(run, (Count >= 12U) && (Bent.Tangents._count == (Count * 4U)) && (Bent.Colors._count == (Count * 4U)));
    const float* Tangent = (const float*)Bent.Tangents._data;
    const unsigned char* Colors8 = (const unsigned char*)Bent.Colors._data;
    const float* Places = (const float*)Bent.Positions._data;
    for (size_t Vertex = 0U; Vertex < Count; Vertex++)
    {
        TEST_CHECK(run, NEAR(Tangent[Vertex * 4U], 1.0, 1.0e-5) && (Tangent[(Vertex * 4U) + 3U] == 1.0f));
        // The colour runs from red at the foot to green at the top, whichever triangle a corner came from.
        double Expected = (double)Places[(Vertex * 3U) + 1U] * 255.0;
        TEST_CHECK(run, NEAR(Colors8[(Vertex * 4U) + 1U], Expected, 1.0) && NEAR(Colors8[Vertex * 4U], 255.0 - Expected, 1.0));
    }
    // An index past the vertices is skipped, not read.
    const unsigned short Hostile[6] = { 0, 1, 9999, 0, 2, 3 };
    Source.Indices = Hostile;
    PathObjectBentMesh_Clear(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 3U, -1.0e30f, 1.0e30f, 100000U));
    TEST_CHECK(run, PathObjectBentMesh_GetVertexCount(&Bent) < Count);
    // The limit stops it, keeping what fit; a slice count out of range is refused.
    Source.Indices = Indices;
    PathObjectBentMesh_Clear(&Bent);
    Error Limited = PathObjectBend_AppendMesh(&Bent, &Source, Slices, 3U, -1.0e30f, 1.0e30f, 6U);
    TEST_CHECK(run, (Limited.Code == ErrorCode_ArgumentOutOfRange) && (PathObjectBentMesh_GetVertexCount(&Bent) <= 6U));
    Error_Deconstruct(&Limited);
    TEST_CHECK_ERROR(run, PathObjectBend_AppendMesh(&Bent, &Source, Slices, 0U, 0.0f, 1.0f, 6U), ErrorCode_IllegalArgument);
    PathObjectBentMesh_Deconstruct(&Bent);
}

static void ASectionOfOneSliceStillPlacesTheMesh(TestRun* run)
{
    float Positions[108];
    float Normals[108];
    float Texcoords[72];
    MakeBoxMesh(0.0f, 1.0f, Positions, Normals, Texcoords);
    PathObjectSourceMesh Source = { .Positions = Positions, .Normals = Normals, .VertexCount = 36U,
        .Transform = MatrixIdentity() };
    PathObjectBendSlice Slice = { .MeshX = 0.5f, .Position = { 5.0f, 0.0f, 0.0f }, .AxisX = { 2.0f, 0.0f, 0.0f },
        .AxisY = { 0.0f, 1.0f, 0.0f }, .AxisZ = { 0.0f, 0.0f, 1.0f } };
    PathObjectBentMesh Bent;
    PathObjectBentMesh_Construct(&Bent);
    TEST_CHECK_SUCCESS(run, PathObjectBend_AppendMesh(&Bent, &Source, &Slice, 1U, -1.0e30f, 1.0e30f, 100000U));
    TEST_CHECK(run, NEAR(Bent.BoxMin.x, 4.0, 1.0e-5) && NEAR(Bent.BoxMax.x, 6.0, 1.0e-5));
    PathObjectBentMesh_Deconstruct(&Bent);
}


// Tests: copies' shear.
static void AShearLeansACopyWithoutMovingItsFoot(TestRun* run)
{
    Matrix4Double Plain = WorldTransform_Compose(Vector3Double_Create(1.0, 2.0, 3.0), (Vector3){ 0.1f, 0.2f, 0.3f },
        (Vector3){ 2.0f, 3.0f, 4.0f });
    Matrix4Double Same = WorldTransform_ComposeSheared(Vector3Double_Create(1.0, 2.0, 3.0), (Vector3){ 0.1f, 0.2f, 0.3f },
        (Vector3){ 2.0f, 3.0f, 4.0f }, 0.0f);
    TEST_CHECK(run, memcmp(&Plain, &Same, sizeof(Plain)) == 0);
    Matrix4Double Level = WorldTransform_ComposeSheared(Vector3Double_Create(0.0, 0.0, 0.0), (Vector3){ 0.0f, 0.0f, 0.0f },
        (Vector3){ 2.0f, 1.0f, 1.0f }, 0.5f);
    // After the scale: X of 1 lands at 2 along, and rises 0.5 of those 2. A point on Y does not move along.
    TEST_CHECK(run, IsVectorNear(Matrix4Double_TransformPoint(&Level, Vector3Double_Create(1.0, 0.0, 0.0)), 2.0, 1.0, 0.0,
        1.0e-9));
    TEST_CHECK(run, IsVectorNear(Matrix4Double_TransformPoint(&Level, Vector3Double_Create(0.0, 1.0, 0.0)), 0.0, 1.0, 0.0,
        1.0e-9));
    TEST_CHECK(run, IsVectorNear(Matrix4Double_TransformPoint(&Level, Vector3Double_Create(1.0, 1.0, 1.0)), 2.0, 2.0, 1.0,
        1.0e-9));
    // Turned, the rise goes along the turned Y.
    Matrix4Double Turned = WorldTransform_ComposeSheared(Vector3Double_Create(0.0, 0.0, 0.0), (Vector3){ 0.0f, 0.0f, 1.5707963f },
        (Vector3){ 1.0f, 1.0f, 1.0f }, 1.0f);
    TEST_CHECK(run, IsVectorNear(Matrix4Double_TransformPoint(&Turned, Vector3Double_Create(1.0, 0.0, 0.0)), -1.0, 1.0, 0.0,
        1.0e-5));
    // A Y scale of 0 leans along the turned axis all the same.
    Matrix4Double Flat = WorldTransform_ComposeSheared(Vector3Double_Create(0.0, 0.0, 0.0), (Vector3){ 0.0f, 0.0f, 0.0f },
        (Vector3){ 1.0f, 0.0f, 1.0f }, 1.0f);
    TEST_CHECK(run, IsVectorNear(Matrix4Double_TransformPoint(&Flat, Vector3Double_Create(1.0, 0.0, 0.0)), 1.0, 1.0, 0.0,
        1.0e-9));
}


// Public functions.
void PathObjectTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "tables_pass", TablesPass },
        { "what_is_in_effect_runs_defaults_asset_placement", WhatIsInEffectRunsDefaultsAssetPlacement },
        { "a_slots_defaults_depend_on_its_kind", ASlotsDefaultsDependOnItsKind },
        { "a_slot_resolves_setting_by_setting", ASlotResolvesSettingBySetting },
        { "the_last_record_of_a_slot_counts", TheLastRecordOfASlotCounts },
        { "the_ground_settings_come_from_the_rows", TheGroundSettingsComeFromTheRows },
        { "a_placements_models_replace_only_the_slots_they_name", APlacementsModelsReplaceOnlyTheSlotsTheyName },
        { "a_definitions_written_settings_are_in_effect", ADefinitionsWrittenSettingsAreInEffect },
        { "a_definition_is_checked_when_it_is_read", ADefinitionIsCheckedWhenItIsRead },
        { "a_mesh_is_measured_under_its_import_transform", AMeshIsMeasuredUnderItsImportTransform },
        { "a_copy_lands_on_its_ends", ACopyLandsOnItsEnds },
        { "a_level_piece_takes_the_height_its_anchor_says", ALevelPieceTakesTheHeightItsAnchorSays },
        { "a_chord_nearly_along_the_up_tilts_instead_of_shearing", AChordNearlyAlongTheUpTiltsInsteadOfShearing },
        { "offsets_and_the_frames_scale_apply", OffsetsAndTheFramesScaleApply },
        { "a_tilted_pieces_offset_lies_square_to_it", ATiltedPiecesOffsetLiesSquareToIt },
        { "a_turn_goes_through_the_pieces_middle", ATurnGoesThroughThePiecesMiddle },
        { "a_piece_that_keeps_its_length_has_it_on_a_slope", APieceThatKeepsItsLengthHasItOnASlope },
        { "a_yaw_jitter_turns_a_piece_about_its_up", AYawJitterTurnsAPieceAboutItsUp },
        { "between_slices_the_axes_lerp_too", BetweenSlicesTheAxesLerpToo },
        { "a_flattening_import_transform_leaves_normals_finite", AFlatteningImportTransformLeavesNormalsFinite },
        { "a_normal_stays_square_under_a_sheared_frame", ANormalStaysSquareUnderAShearedFrame },
        { "a_sliced_box_keeps_its_surface", ASlicedBoxKeepsItsSurface },
        { "a_bent_box_follows_its_slices", ABentBoxFollowsItsSlices },
        { "what_reaches_past_the_slices_carries_on_straight", WhatReachesPastTheSlicesCarriesOnStraight },
        { "an_indexed_mesh_bends_as_an_unindexed_one", AnIndexedMeshBendsAsAnUnindexedOne },
        { "a_section_of_one_slice_still_places_the_mesh", ASectionOfOneSliceStillPlacesTheMesh },
        { "a_shear_leans_a_copy_without_moving_its_foot", AShearLeansACopyWithoutMovingItsFoot },
    };
    TestRun_RunSuite(run, "PathObject", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
