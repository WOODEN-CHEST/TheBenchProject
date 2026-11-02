#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TestWorld.h"
#include "GameInput.h"
#include "GameMath.h"
#include "Logger.h"
#include "ProgramTime.h"
#include "PropertyDescriptor.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"
#include "World.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldFile.h"
#include "WorldModel.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "WorldRenderCache.h"
#include "WorldRendererInternal.h"
#include "WorldScreenComponent.h"
#include "WorldScreenHost.h"
#include "WorldScreens.h"
#include "raylib/raymath.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for texture coordinates and distances found by rays. */
#define UV_EPSILON 1e-4f
/** Room for a path, and for a definition built in a test. */
#define SCREEN_TEST_TEXT_CAPACITY 256
/** The test screen's picture. */
#define TEST_PICTURE_WIDTH 320
#define TEST_PICTURE_HEIGHT 240
/** The curved screen: a cylinder's section around Y of this radius, these many columns over this angle (degrees). */
#define CURVE_RADIUS 2.0f
#define CURVE_COLUMNS 8
#define CURVE_HALF_ANGLE 30.0f
#define CURVE_HALF_HEIGHT 0.5f
/** The fixed tick the host's tests advance by, in seconds. */
#define HOST_TICK_SECONDS (1.0 / 60.0)
/** A made-up GPU id for the pictures handed to the renderer. */
#define PICTURE_TEXTURE_ID 91U
#define OTHER_TEXTURE_ID 92U


// Fields: the test models.
/* A bezel (slot 0) at z = 0 over [-1, 1] x [-1, 1], and in front of it a screen (slot 1) at z = 0.01 over
 * [-0.5, 0.5] x [-0.4, 0.4], both facing +Z; the screen's texture coordinates as raylib keeps them (v down: (0, 0) at
 * its top-left corner). */
static const float BezelVertices[] = { -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f };
static const float BezelTexcoords[] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float ScreenVertices[] = { -0.5f, -0.4f, 0.01f, 0.5f, -0.4f, 0.01f, 0.5f, 0.4f, 0.01f, -0.5f, 0.4f, 0.01f };
static const float ScreenTexcoords[] = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f };
/* The same screen whose coordinates run past the picture: a rounded border of a tenth on every side. */
static const float BorderedTexcoords[] = { -0.1f, 1.1f, 1.1f, 1.1f, 1.1f, -0.1f, -0.1f, -0.1f };
static const unsigned short QuadIndices[] = { 0U, 1U, 2U, 0U, 2U, 3U };
static const float NotFinite[] = { 0.0f, 1.0f, NAN, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f };

static const char* const ScreenBoxJson =
    "{\"name\": \"screen_box\", \"lods\": [{\"model\": \"screen_box\", \"max_distance\": 40}], \"collision\": \"bounds\", "
    "\"surfaces\": [{\"name\": \"screen\", \"material_slot\": 1, \"resolution\": [320, 240]}]}";


// Types.
/* A refused definition: its JSON, the error code and a fragment its message must hold. */
typedef struct RefusedDefinitionStruct
{
    const char* Json;
    ErrorCode Code;
    const char* Fragment;
} RefusedDefinition;

/* A widget that records what reaches it: the presses, the keys and the typed text, and whether it handles keys. */
typedef struct HostProbeStruct
{
    Widget Base;
    bool IsHandlingKeys;
    bool IsHandlingText;
    int PressCount;
    int KeyCount;
    int TextCount;
    GameKey LastKey;
    uint32_t LastCodepoint;
} HostProbe;

/* A screen with the probe type, the tick's input and a host. */
typedef struct HostFixtureStruct
{
    KeyRegistry Keys;
    GameInput Input;
    GameInputSample Sample;
    UIWidgetFactory Factory;
    UIScreen Screen;
    uint64_t ProbeType;
    WorldScreenHost Host;
    double Time;
    bool IsScreenConstructed;
    bool IsReady;
} HostFixture;


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) < UV_EPSILON;
}

/* Builds a world model definition from @p json without registering it, and returns the constructor's result. */
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

/* Counts the occurrences of @p needle in "<directory>/logs/latest.log". */
static size_t CountInLatestLog(TestRun* run, const unsigned char* directory, const char* needle)
{
    size_t Found = 0U;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, SCREEN_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, SCREEN_TEST_TEXT_CAPACITY, sizeof(unsigned char));
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

/* Registers the bezel-and-screen model under @p name with the import transform @p transform and the screen's texture
 * coordinates @p screenTexcoords (NULL: none). */
static bool AddScreenModel(TestRun* run, TestAssetFixture* fixture, const char* name, Matrix transform,
    const float* screenTexcoords)
{
    TestModelPart Parts[] =
    {
        { .Vertices = BezelVertices, .Texcoords = BezelTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 0 },
        { .Vertices = ScreenVertices, .Texcoords = screenTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 1 },
    };
    return TestAssets_AddPartsModel(run, fixture, name, Parts, 2, 2, transform, NULL);
}

/* The curved screen's vertices and texture coordinates: CURVE_COLUMNS + 1 columns of two (bottom, top), the section
 * bulging toward +Z (its middle at z = 0), u along the arc and v down. */
static void BuildCurve(float* outVertices, float* outTexcoords, unsigned short* outIndices)
{
    for (int Column = 0; Column <= CURVE_COLUMNS; Column++)
    {
        float Share = (float)Column / (float)CURVE_COLUMNS;
        float Angle = (-CURVE_HALF_ANGLE + (Share * 2.0f * CURVE_HALF_ANGLE)) * DEG2RAD;
        for (int Row = 0; Row < 2; Row++)
        {
            int Vertex = (Column * 2) + Row;
            outVertices[(Vertex * 3) + 0] = CURVE_RADIUS * sinf(Angle);
            outVertices[(Vertex * 3) + 1] = (Row == 0) ? -CURVE_HALF_HEIGHT : CURVE_HALF_HEIGHT;
            outVertices[(Vertex * 3) + 2] = (CURVE_RADIUS * cosf(Angle)) - CURVE_RADIUS;
            outTexcoords[(Vertex * 2) + 0] = Share;
            outTexcoords[(Vertex * 2) + 1] = (Row == 0) ? 1.0f : 0.0f;
        }
    }
    for (int Column = 0; Column < CURVE_COLUMNS; Column++)
    {
        unsigned short Bottom = (unsigned short)(Column * 2);
        unsigned short* Quad = &outIndices[Column * 6];
        // Counter-clockwise seen from the front (+Z): bottom-left, bottom-right, top-right; bottom-left, top-right, top-left.
        Quad[0] = Bottom;
        Quad[1] = (unsigned short)(Bottom + 2U);
        Quad[2] = (unsigned short)(Bottom + 3U);
        Quad[3] = Bottom;
        Quad[4] = (unsigned short)(Bottom + 3U);
        Quad[5] = (unsigned short)(Bottom + 1U);
    }
}

/* Loads the world model @p name under a new user (retire it to release the hold). */
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

/* The screen box's world model and surface, loaded in a fresh fixture; *outUser holds it. */
static const WorldModelSurface* LoadScreenBox(TestRun* run, TestAssetFixture* fixture, AssetUserID* outUser)
{
    if (!TestAssetFixture_Construct(run, fixture) || !AddScreenModel(run, fixture, "screen_box", MatrixIdentity(),
        ScreenTexcoords) || !TestAssets_AddWorldModel(run, fixture, ScreenBoxJson))
    {
        return NULL;
    }
    const WorldModel* Loaded = LoadWorldModel(run, fixture, "screen_box", outUser);
    return ((Loaded != NULL) && TEST_CHECK(run, WorldModel_GetSurfaceCount(Loaded) == 1U))
        ? WorldModel_GetSurface(Loaded, 0U) : NULL;
}

/* The screen box's local point (x, y) on the screen's plane, and the coordinates it should have. */
static Vector2 ExpectedUv(float x, float y)
{
    return (Vector2){ x + 0.5f, (0.4f - y) / 0.8f };
}


// Static functions: the surfaces of a world model.
/* A world model's surfaces: their keys, the defaults, and LOD 0's triangles of each slot (only those) with their
 * corners' texture coordinates, after LOD 0's import transform. */
static void SurfacesAreReadAndBuiltFromLodZerosSlot(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    const WorldModelSurface* Surface = LoadScreenBox(run, &Fixture, &User);
    if (Surface != NULL)
    {
        TEST_CHECK(run, strcmp((const char*)WorldModelSurface_GetName(Surface), "screen") == 0);
        TEST_CHECK(run, WorldModelSurface_GetMaterialSlot(Surface) == 1);
        TEST_CHECK(run, (WorldModelSurface_GetWidth(Surface) == TEST_PICTURE_WIDTH)
            && (WorldModelSurface_GetHeight(Surface) == TEST_PICTURE_HEIGHT));
        TEST_CHECK(run, WorldModelSurface_GetKind(Surface) == WorldModelSurfaceKind_Screen);
        TEST_CHECK(run, WorldModelSurface_GetEmission(Surface) == WORLD_MODEL_DEFAULT_SURFACE_EMISSION);
        const CollisionMesh* Triangles = WorldModelSurface_GetMesh(Surface);
        // Only the screen's two triangles: the bezel (slot 0) is not part of it.
        TEST_CHECK(run, (Triangles != NULL) && (Triangles->_triangleCount == 2U));
        TEST_CHECK(run, (Triangles != NULL) && IsNear(Triangles->_boundsMin.x, -0.5f) && IsNear(Triangles->_boundsMax.y, 0.4f)
            && IsNear(Triangles->_boundsMin.z, 0.01f));
        // Each corner of each triangle carries its vertex's coordinates.
        for (uint32_t Triangle = 0U; (Triangles != NULL) && (Triangle < Triangles->_triangleCount); Triangle++)
        {
            for (uint32_t Corner = 0U; Corner < 3U; Corner++)
            {
                Vector3 Weights = { (Corner == 0U) ? 1.0f : 0.0f, (Corner == 1U) ? 1.0f : 0.0f,
                    (Corner == 2U) ? 1.0f : 0.0f };
                Vector2 Uv = { -1.0f, -1.0f };
                TEST_CHECK(run, WorldModelSurface_GetUv(Surface, Triangle, Weights, &Uv));
                Vector3 Vertex = Triangles->_vertices[Triangles->_triangles[Triangle]._indices[Corner]];
                Vector2 Expected = ExpectedUv(Vertex.x, Vertex.y);
                TEST_CHECK(run, IsNear(Uv.x, Expected.x) && IsNear(Uv.y, Expected.y));
            }
        }
        Vector2 Uv = { 5.0f, 5.0f };
        TEST_CHECK(run, !WorldModelSurface_GetUv(Surface, 2U, (Vector3){ 1.0f, 0.0f, 0.0f }, &Uv) && (Uv.x == 0.0f)
            && (Uv.y == 0.0f));
        uint32_t Index = 7U;
        WorldModel* Loaded = NULL;
        TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldModel(Fixture.Manager, (const unsigned char*)u8"screen_box", User,
            &Loaded));
        TEST_CHECK(run, (Loaded != NULL) && WorldModel_TryFindSurface(Loaded, (const unsigned char*)u8"screen", &Index)
            && (Index == 0U));
        TEST_CHECK(run, (Loaded != NULL) && !WorldModel_TryFindSurface(Loaded, (const unsigned char*)u8"screens", NULL)
            && !WorldModel_TryFindSurface(Loaded, (const unsigned char*)u8"scr", NULL)
            && !WorldModel_TryFindSurface(Loaded, NULL, NULL));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);

    // LOD 0's import transform places the triangles in the object's space; a paper takes no emission; two surfaces keep
    // their own triangles.
    if (TestAssetFixture_Construct(run, &Fixture)
        && AddScreenModel(run, &Fixture, "big_screen", MatrixScale(2.0f, 3.0f, 1.0f), ScreenTexcoords)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"big_screen\", \"lods\": [{\"model\": \"big_screen\", "
            "\"max_distance\": 40}], \"surfaces\": [{\"name\": \"glass\", \"material_slot\": 1, \"resolution\": [16, 2048], "
            "\"emission\": 0.5}, {\"name\": \"page\", \"material_slot\": 0, \"resolution\": [2048, 16], \"kind\": "
            "\"paper\"}]}"))
    {
        const WorldModel* Loaded = LoadWorldModel(run, &Fixture, "big_screen", &User);
        if ((Loaded != NULL) && TEST_CHECK(run, WorldModel_GetSurfaceCount(Loaded) == 2U))
        {
            const WorldModelSurface* Glass = WorldModel_GetSurface(Loaded, 0U);
            const WorldModelSurface* Page = WorldModel_GetSurface(Loaded, 1U);
            TEST_CHECK(run, WorldModel_GetSurface(Loaded, 2U) == NULL);
            uint32_t PageIndex = 0U;
            TEST_CHECK(run, WorldModel_TryFindSurface(Loaded, (const unsigned char*)u8"page", &PageIndex)
                && (PageIndex == 1U));
            TEST_CHECK(run, IsNear(WorldModelSurface_GetMesh(Glass)->_boundsMax.x, 1.0f)
                && IsNear(WorldModelSurface_GetMesh(Glass)->_boundsMax.y, 1.2f));
            TEST_CHECK(run, WorldModelSurface_GetEmission(Glass) == 0.5f);
            TEST_CHECK(run, (WorldModelSurface_GetKind(Page) == WorldModelSurfaceKind_Paper)
                && (WorldModelSurface_GetEmission(Page) == 0.0f));
            TEST_CHECK(run, IsNear(WorldModelSurface_GetMesh(Page)->_boundsMax.x, 2.0f)
                && (WorldModelSurface_GetWidth(Page) == 2048) && (WorldModelSurface_GetHeight(Page) == 16));
            // Each surface reads its own coordinates: the glass's (scaled), and the page's (the bezel's, all 0).
            Matrix4Double Identity = Matrix4Double_Identity();
            WorldModelSurfaceHit Hit;
            TEST_CHECK(run, WorldModelSurface_Raycast(Glass, &Identity, Vector3Double_Create(0.5, -0.3, 2.0),
                (Vector3){ 0.0f, 0.0f, -1.0f }, 5.0f, &Hit) && IsNear(Hit.Uv.x, 0.75f) && IsNear(Hit.Uv.y, 0.625f));
            TEST_CHECK(run, WorldModelSurface_Raycast(Page, &Identity, Vector3Double_Create(1.8, 2.5, 2.0),
                (Vector3){ 0.0f, 0.0f, -1.0f }, 5.0f, &Hit) && IsNear(Hit.Uv.x, 0.0f) && IsNear(Hit.Uv.y, 0.0f));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    TEST_CHECK(run, strcmp((const char*)WorldModelSurfaceKind_GetName(WorldModelSurfaceKind_Paper), "paper") == 0);
    TEST_CHECK(run, WorldModelSurfaceKind_GetName(WorldModelSurfaceKind_Count) == NULL);
}

/* Every rule of the "surfaces" section is refused when the definition is read, with a message naming the place. */
static void SurfaceRulesAreRefusedWithClearMessages(TestRun* run)
{
#define WM "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", \"max_distance\": 40}], "
    static const RefusedDefinition Cases[] =
    {
        { WM "\"surfaces\": {}}", ErrorCode_InvalidAssetDefinition, "\"surfaces\" must be an array of surfaces" },
        { WM "\"surfaces\": [3]}", ErrorCode_InvalidAssetDefinition, "surfaces[0] must be a JSON object" },
        { WM "\"surfaces\": [{\"material_slot\": 1, \"resolution\": [64, 64]}]}", ErrorCode_InvalidAssetDefinition,
            "surfaces[0] needs a \"name\"" },
        { WM "\"surfaces\": [{\"name\": 4, \"material_slot\": 1, \"resolution\": [64, 64]}]}",
            ErrorCode_InvalidAssetDefinition, "surfaces[0].name must be a string" },
        { WM "\"surfaces\": [{\"name\": \"Screen\", \"material_slot\": 1, \"resolution\": [64, 64]}]}",
            ErrorCode_InvalidAssetDefinition, "surfaces[0].name \"Screen\" is not a valid surface name" },
        { WM "\"surfaces\": [{\"name\": \"a12345678901234567890123456789012345678901234567890123456789012345\", "
            "\"material_slot\": 1, \"resolution\": [64, 64]}]}", ErrorCode_InvalidAssetDefinition, "at most 64 bytes" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64]}, {\"name\": \"s\", "
            "\"material_slot\": 2, \"resolution\": [64, 64]}]}", ErrorCode_InvalidAssetDefinition,
            "surfaces[1].name \"s\" is used by surfaces[0] too" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"resolution\": [64, 64]}]}", ErrorCode_InvalidAssetDefinition,
            "surfaces[0] needs a \"material_slot\"" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": -1, \"resolution\": [64, 64]}]}",
            ErrorCode_InvalidAssetDefinition, "a whole number from 0 to 2147483647" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 2147483648, \"resolution\": [64, 64]}]}",
            ErrorCode_InvalidAssetDefinition, "a whole number from 0 to 2147483647" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1.5, \"resolution\": [64, 64]}]}",
            ErrorCode_InvalidAssetDefinition, "surfaces[0] needs a \"material_slot\"" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64]}, {\"name\": \"t\", "
            "\"material_slot\": 1, \"resolution\": [64, 64]}]}", ErrorCode_InvalidAssetDefinition,
            "surfaces[1].material_slot 1 is surfaces[0]'s too" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1}]}", ErrorCode_InvalidAssetDefinition,
            "surfaces[0] needs a \"resolution\": [width, height] of its picture, each 16 to 2048 pixels" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [15, 64]}]}",
            ErrorCode_InvalidAssetDefinition, "each 16 to 2048 pixels" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 2049]}]}",
            ErrorCode_InvalidAssetDefinition, "each 16 to 2048 pixels" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [2049, 64]}]}",
            ErrorCode_InvalidAssetDefinition, "each 16 to 2048 pixels" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 15]}]}",
            ErrorCode_InvalidAssetDefinition, "each 16 to 2048 pixels" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": \"big\"}]}",
            ErrorCode_InvalidAssetDefinition, "surfaces[0] needs a \"resolution\"" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64], \"kind\": \"tv\"}]}",
            ErrorCode_InvalidAssetDefinition, "surfaces[0].kind is \"tv\"; it must be \"screen\"" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64], \"kind\": 1}]}",
            ErrorCode_InvalidAssetDefinition, "surfaces[0].kind is" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64], \"kind\": \"paper\", "
            "\"emission\": 1}]}", ErrorCode_InvalidAssetDefinition,
            "surfaces[0].emission is a screen's; a paper is lit like the surface it is on" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64], \"emission\": -0.5}]}",
            ErrorCode_InvalidAssetDefinition, "surfaces[0].emission is -0.5; it must be at least 0 and at most 1000" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64], \"emission\": 1001}]}",
            ErrorCode_InvalidAssetDefinition, "at most 1000" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64], \"emission\": \"x\"}]}",
            ErrorCode_InvalidAssetDefinition, "surfaces[0].emission must be a number" },
        { WM "\"surfaces\": [{\"name\": \"s\", \"material_slot\": 1, \"resolution\": [64, 64], \"slot\": 1}]}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"slot\" in world model \"tbp:m\", surfaces[0]; the keys "
                "allowed there are: name, material_slot, resolution, kind, emission" },
        { WM "\"surfaces\": [{\"name\": \"a\", \"material_slot\": 0, \"resolution\": [64, 64]}, {\"name\": \"b\", "
            "\"material_slot\": 1, \"resolution\": [64, 64]}, {\"name\": \"c\", \"material_slot\": 2, \"resolution\": "
            "[64, 64]}, {\"name\": \"d\", \"material_slot\": 3, \"resolution\": [64, 64]}, {\"name\": \"e\", "
            "\"material_slot\": 4, \"resolution\": [64, 64]}, {\"name\": \"f\", \"material_slot\": 5, \"resolution\": "
            "[64, 64]}, {\"name\": \"g\", \"material_slot\": 6, \"resolution\": [64, 64]}, {\"name\": \"h\", "
            "\"material_slot\": 7, \"resolution\": [64, 64]}, {\"name\": \"i\", \"material_slot\": 8, \"resolution\": "
            "[64, 64]}, {\"name\": \"j\", \"material_slot\": 9, \"resolution\": [64, 64]}, {\"name\": \"k\", "
            "\"material_slot\": 10, \"resolution\": [64, 64]}, {\"name\": \"l\", \"material_slot\": 11, \"resolution\": "
            "[64, 64]}, {\"name\": \"m\", \"material_slot\": 12, \"resolution\": [64, 64]}, {\"name\": \"n\", "
            "\"material_slot\": 13, \"resolution\": [64, 64]}, {\"name\": \"o\", \"material_slot\": 14, \"resolution\": "
            "[64, 64]}, {\"name\": \"p\", \"material_slot\": 15, \"resolution\": [64, 64]}, {\"name\": \"q\", "
            "\"material_slot\": 16, \"resolution\": [64, 64]}]}", ErrorCode_InvalidAssetDefinition,
            "\"surfaces\" holds 17 surfaces; at most 16 are allowed" },
    };
#undef WM
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            const char* const Fragments[] = { Cases[Index].Fragment, NULL };
            if (!TestText_CheckErrorMessage(run, BuildWorldModelDefinition(&Fixture, Cases[Index].Json), Cases[Index].Code,
                Fragments))
            {
                printf("      (case %u: %s)\n", (unsigned int)Index, Cases[Index].Json);
            }
        }
        // At the limits: 16 x 16 and 2048 x 2048 pixels, slot 0, emission 0 and 1000, an empty list.
        TEST_CHECK_SUCCESS(run, BuildWorldModelDefinition(&Fixture, "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"surfaces\": [{\"name\": \"a\", \"material_slot\": 0, \"resolution\": [16, 16], "
            "\"emission\": 0}, {\"name\": \"b\", \"material_slot\": 1, \"resolution\": {\"x\": 2048, \"y\": 2048}, "
            "\"emission\": 1000}]}"));
        TEST_CHECK_SUCCESS(run, BuildWorldModelDefinition(&Fixture, "{\"name\": \"m\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"surfaces\": []}"));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A surface needs LOD 0's meshes of its slot, each with finite texture coordinates: otherwise the world model does not
 * load, naming the surface (and holds nothing). */
static void SurfacesNeedTheirSlotsMeshesWithTextureCoordinates(TestRun* run)
{
    TestAssetFixture Fixture;
    TestModelPart NoTexcoords[] =
    {
        { .Vertices = ScreenVertices, .Texcoords = NULL, .VertexCount = 4, .Indices = QuadIndices, .TriangleCount = 2,
            .Material = 1 },
    };
    TestModelPart Flat[] =
    {
        { .Vertices = BezelVertices, .Texcoords = BezelTexcoords, .VertexCount = 3, .Indices = NULL,
            .TriangleCount = 0, .Material = 1 },
    };
    static const float FlatVertices[] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f };
    Flat[0].Vertices = FlatVertices;
    struct { const char* Model; const char* Fragment; } Cases[] =
    {
        { "no_slot", "surface \"screen\": no mesh of LOD 0 uses material slot 1" },
        { "no_texcoords", "surface \"screen\": LOD 0's mesh 0 (material slot 1) has no texture coordinates" },
        { "nan_texcoords", "surface \"screen\": LOD 0's mesh 1 has texture coordinates that are not finite numbers" },
        { "flat", "surface \"screen\": its triangles: A collision mesh needs at least one triangle with an area" },
    };
    TestModelPart BezelOnly[] =
    {
        { .Vertices = BezelVertices, .Texcoords = BezelTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 0 },
    };
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddPartsModel(run, &Fixture, "no_slot", BezelOnly, 1, 2, MatrixIdentity(), NULL)
        && TestAssets_AddPartsModel(run, &Fixture, "no_texcoords", NoTexcoords, 1, 2, MatrixIdentity(), NULL)
        && AddScreenModel(run, &Fixture, "nan_texcoords", MatrixIdentity(), NotFinite)
        && TestAssets_AddPartsModel(run, &Fixture, "flat", Flat, 1, 2, MatrixIdentity(), NULL))
    {
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            char Json[SCREEN_TEST_TEXT_CAPACITY * 2];
            snprintf(Json, sizeof(Json), "{\"name\": \"wm_%s\", \"lods\": [{\"model\": \"%s\", \"max_distance\": 40}], "
                "\"surfaces\": [{\"name\": \"screen\", \"material_slot\": 1, \"resolution\": [64, 64]}]}",
                Cases[Index].Model, Cases[Index].Model);
            char Name[SCREEN_TEST_TEXT_CAPACITY];
            snprintf(Name, sizeof(Name), "wm_%s", Cases[Index].Model);
            WorldModel* Loaded = NULL;
            AssetUserID User = ASSET_USER_ID_INVALID;
            const char* const Fragments[] = { Cases[Index].Fragment, NULL };
            if (TestAssets_AddWorldModel(run, &Fixture, Json)
                && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User))
                && !TestText_CheckErrorMessage(run, AssetManager_LoadWorldModel(Fixture.Manager,
                    (const unsigned char*)Name, User, &Loaded), ErrorCode_InvalidAssetData, Fragments))
            {
                printf("      (case %s)\n", Cases[Index].Model);
            }
            TEST_CHECK(run, Loaded == NULL);
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A ray meets the screen box's surface where it points, from the front, with the texture coordinates of that point; the
 * picture's position is the coordinates as they are (v down: the UI's top-left convention). */
static void TheHitGivesTheSurfacePosition(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    const WorldModelSurface* Surface = LoadScreenBox(run, &Fixture, &User);
    if (Surface != NULL)
    {
        Matrix4Double Identity = Matrix4Double_Identity();
        const Vector2 Points[] = { { 0.0f, 0.0f }, { -0.45f, 0.35f }, { 0.3f, -0.2f }, { 0.49f, -0.39f } };
        for (size_t Index = 0U; Index < (sizeof(Points) / sizeof(Points[0])); Index++)
        {
            WorldModelSurfaceHit Hit;
            Vector3Double Origin = Vector3Double_Create((double)Points[Index].x, (double)Points[Index].y, 3.0);
            bool IsHit = WorldModelSurface_Raycast(Surface, &Identity, Origin, (Vector3){ 0.0f, 0.0f, -2.0f }, 10.0f,
                &Hit);
            Vector2 Expected = ExpectedUv(Points[Index].x, Points[Index].y);
            TEST_CHECK(run, IsHit && IsNear(Hit.Distance, 2.99f) && Hit.IsOnPicture);
            TEST_CHECK(run, IsHit && IsNear(Hit.Uv.x, Expected.x) && IsNear(Hit.Uv.y, Expected.y));
            TEST_CHECK(run, IsHit && IsNear(Hit.Barycentric.x + Hit.Barycentric.y + Hit.Barycentric.z, 1.0f));
            // The hit's triangle and barycentrics give its coordinates again.
            Vector2 Again = { -1.0f, -1.0f };
            TEST_CHECK(run, IsHit && WorldModelSurface_GetUv(Surface, Hit.Triangle, Hit.Barycentric, &Again)
                && IsNear(Again.x, Hit.Uv.x) && IsNear(Again.y, Hit.Uv.y));
        }
        // The top-left corner is (0, 0), the bottom-right (1, 1).
        WorldModelSurfaceHit Corner;
        TEST_CHECK(run, WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(-0.4999, 0.3999, 1.0),
            (Vector3){ 0.0f, 0.0f, -1.0f }, 5.0f, &Corner) && (Corner.Uv.x < 0.001f) && (Corner.Uv.y < 0.001f));
        TEST_CHECK(run, WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(0.4999, -0.3999, 1.0),
            (Vector3){ 0.0f, 0.0f, -1.0f }, 5.0f, &Corner) && (Corner.Uv.x > 0.999f) && (Corner.Uv.y > 0.999f));
        // A slanted ray meets the same point as a straight one.
        WorldModelSurfaceHit Slanted;
        TEST_CHECK(run, WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(-1.0, 0.5, 1.01),
            (Vector3){ 1.25f, -0.5f, -1.0f }, 5.0f, &Slanted) && IsNear(Slanted.Uv.x, ExpectedUv(0.25f, 0.0f).x)
            && IsNear(Slanted.Uv.y, ExpectedUv(0.25f, 0.0f).y));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Rays that miss the slot's triangles meet nothing: on the bezel (another slot), beside it, from behind (one-sided),
 * short of it, along it, and with no direction. */
static void RaysThatMissTheSlotsTrianglesMeetNothing(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    const WorldModelSurface* Surface = LoadScreenBox(run, &Fixture, &User);
    if (Surface != NULL)
    {
        Matrix4Double Identity = Matrix4Double_Identity();
        WorldModelSurfaceHit Hit;
        Vector3 Back = { 0.0f, 0.0f, -1.0f };
        TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(0.8, 0.0, 2.0), Back, 10.0f,
            &Hit));
        TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(0.0, 0.45, 2.0), Back,
            10.0f, &Hit));
        TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(0.0, 0.0, -2.0),
            (Vector3){ 0.0f, 0.0f, 1.0f }, 10.0f, &Hit));
        TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(0.0, 0.0, 2.0), Back, 1.5f,
            &Hit));
        TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(-2.0, 0.0, 0.01),
            (Vector3){ 1.0f, 0.0f, 0.0f }, 10.0f, &Hit));
        TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(0.0, 0.0, 2.0),
            (Vector3){ 0.0f, 0.0f, 0.0f }, 10.0f, &Hit));
        TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create(0.0, 0.0, 2.0), Back, 0.0f,
            &Hit));
        // A placement squashed flat cannot be inverted: nothing is met.
        Matrix4Double Flat = Matrix4Double_FromScale(Vector3Double_Create(1.0, 1.0, 0.0));
        TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Flat, Vector3Double_Create(0.0, 0.0, 2.0), Back, 10.0f,
            &Hit));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A placement turned, scaled unevenly and moved (also mirrored: from the side its slot faces) is met where its triangles
 * lie, with the coordinates of the local point; and as exactly 10 000 km from the world's origin. */
static void ARotatedAndScaledPlacementIsMetWhereItsTrianglesLie(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    const WorldModelSurface* Surface = LoadScreenBox(run, &Fixture, &User);
    if (Surface != NULL)
    {
        const Vector3Double Places[] =
        {
            { 10.0, 5.0, -3.0 }, { 1.0e7 + 0.37, -2.0e6, 7.3e6 }
        };
        const Vector3 Scales[] = { { 2.0f, 3.0f, 0.5f }, { -1.5f, 1.0f, 1.0f } };
        for (size_t Place = 0U; Place < (sizeof(Places) / sizeof(Places[0])); Place++)
        {
            for (size_t Scale = 0U; Scale < (sizeof(Scales) / sizeof(Scales[0])); Scale++)
            {
                Matrix Local = MatrixMultiply(MatrixMultiply(MatrixScale(Scales[Scale].x, Scales[Scale].y,
                    Scales[Scale].z), MatrixRotateXYZ((Vector3){ 0.3f, 1.2f, -0.4f })), MatrixIdentity());
                Matrix4Double Placement = GameMath_FromMatrix(Local);
                Matrix4Double_SetTranslation(&Placement, Places[Place]);
                // A point of the screen, and the direction it faces in the world (the normal's transform).
                Vector3 LocalPoint = { 0.2f, -0.1f, 0.01f };
                Matrix Normal = MatrixTranspose(MatrixInvert(Local));
                Vector3 Facing = Vector3Normalize(Vector3Transform((Vector3){ 0.0f, 0.0f, 1.0f }, Normal));
                Vector3Double Point = Matrix4Double_TransformPoint(&Placement, GameMath_FromVector3(LocalPoint));
                Vector3Double Origin = Vector3Double_AddScaled(Point, GameMath_FromVector3(Facing), 2.0);
                WorldModelSurfaceHit Hit;
                bool IsHit = WorldModelSurface_Raycast(Surface, &Placement, Origin, Vector3Negate(Facing), 5.0f, &Hit);
                Vector2 Expected = ExpectedUv(LocalPoint.x, LocalPoint.y);
                TEST_CHECK(run, IsHit && IsNear(Hit.Distance, 2.0f) && Hit.IsOnPicture);
                TEST_CHECK(run, IsHit && IsNear(Hit.Uv.x, Expected.x) && IsNear(Hit.Uv.y, Expected.y));
                if (!IsHit || !IsNear(Hit.Uv.x, Expected.x) || !IsNear(Hit.Uv.y, Expected.y))
                {
                    printf("      (place %u, scale %u)\n", (unsigned int)Place, (unsigned int)Scale);
                }
                // From behind the placement's face: nothing.
                Vector3Double Behind = Vector3Double_AddScaled(Point, GameMath_FromVector3(Facing), -2.0);
                TEST_CHECK(run, !WorldModelSurface_Raycast(Surface, &Placement, Behind, Facing, 5.0f, &Hit));
            }
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A curved surface (a cylinder's section in facets) is met on the facet the ray reaches, with the coordinates of that
 * point of the facet, close to the arc's own. */
static void ACurvedSurfaceIsMetOnItsFacets(TestRun* run)
{
    float Vertices[(CURVE_COLUMNS + 1) * 2 * 3];
    float Texcoords[(CURVE_COLUMNS + 1) * 2 * 2];
    unsigned short Indices[CURVE_COLUMNS * 6];
    BuildCurve(Vertices, Texcoords, Indices);
    TestModelPart Part =
    {
        .Vertices = Vertices, .Texcoords = Texcoords, .VertexCount = (CURVE_COLUMNS + 1) * 2, .Indices = Indices,
        .TriangleCount = CURVE_COLUMNS * 2, .Material = 0
    };
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddPartsModel(run, &Fixture, "curve", &Part, 1, 1, MatrixIdentity(), NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"curve\", \"lods\": [{\"model\": \"curve\", "
            "\"max_distance\": 40}], \"surfaces\": [{\"name\": \"screen\", \"material_slot\": 0, \"resolution\": "
            "[256, 128]}]}"))
    {
        const WorldModel* Loaded = LoadWorldModel(run, &Fixture, "curve", &User);
        const WorldModelSurface* Surface = (Loaded != NULL) ? WorldModel_GetSurface(Loaded, 0U) : NULL;
        TEST_CHECK(run, (Surface != NULL) && (WorldModelSurface_GetMesh(Surface)->_triangleCount == (CURVE_COLUMNS * 2)));
        Matrix4Double Identity = Matrix4Double_Identity();
        const float Xs[] = { -0.9f, -0.37f, 0.0f, 0.21f, 0.8f };
        for (size_t Index = 0U; (Surface != NULL) && (Index < (sizeof(Xs) / sizeof(Xs[0]))); Index++)
        {
            float X = Xs[Index];
            float Y = 0.25f;
            WorldModelSurfaceHit Hit;
            bool IsHit = WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create((double)X, (double)Y, 5.0),
                (Vector3){ 0.0f, 0.0f, -1.0f }, 10.0f, &Hit);
            // The facet's own answer: the column the x falls in, and the share of the chord there.
            float Expected = 0.0f;
            for (int Column = 0; Column < CURVE_COLUMNS; Column++)
            {
                float Left = Vertices[(Column * 2) * 3];
                float Right = Vertices[((Column + 1) * 2) * 3];
                if ((X >= Left) && (X <= Right))
                {
                    float Share = (X - Left) / (Right - Left);
                    Expected = ((float)Column + Share) / (float)CURVE_COLUMNS;
                }
            }
            float ArcU = ((asinf(X / CURVE_RADIUS) * RAD2DEG) + CURVE_HALF_ANGLE) / (2.0f * CURVE_HALF_ANGLE);
            TEST_CHECK(run, IsHit && IsNear(Hit.Uv.x, Expected) && IsNear(Hit.Uv.y, 0.25f) && Hit.IsOnPicture);
            TEST_CHECK(run, IsHit && (fabsf(Hit.Uv.x - ArcU) < 0.01f));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A slot spread over two meshes, another slot's mesh between them: each triangle takes the coordinates of the mesh it
 * came from. */
static void ASlotOverSeveralMeshesKeepsEachMeshsCoordinates(TestRun* run)
{
    static const float LeftVertices[] = { -0.5f, -0.4f, 0.01f, 0.0f, -0.4f, 0.01f, 0.0f, 0.4f, 0.01f, -0.5f, 0.4f, 0.01f };
    static const float LeftTexcoords[] = { 0.0f, 1.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f };
    static const float RightVertices[] = { 0.0f, -0.4f, 0.01f, 0.5f, -0.4f, 0.01f, 0.5f, 0.4f, 0.01f, 0.0f, 0.4f, 0.01f };
    static const float RightTexcoords[] = { 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.0f };
    TestModelPart Parts[] =
    {
        { .Vertices = LeftVertices, .Texcoords = LeftTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 1 },
        { .Vertices = BezelVertices, .Texcoords = BezelTexcoords, .VertexCount = 4, .Indices = QuadIndices,
            .TriangleCount = 2, .Material = 0 },
        { .Vertices = RightVertices, .Texcoords = RightTexcoords, .VertexCount = 4, .Indices = NULL, .TriangleCount = 0,
            .Material = 1 },
    };
    // The right half unindexed: every three vertices a triangle (its first three only, as raylib reads an OBJ).
    static const float RightUnindexed[] = { 0.0f, -0.4f, 0.01f, 0.5f, -0.4f, 0.01f, 0.5f, 0.4f, 0.01f, 0.0f, -0.4f, 0.01f,
        0.5f, 0.4f, 0.01f, 0.0f, 0.4f, 0.01f };
    static const float RightUnindexedTexcoords[] = { 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f, 1.0f, 0.0f, 0.5f,
        0.0f };
    Parts[2].Vertices = RightUnindexed;
    Parts[2].Texcoords = RightUnindexedTexcoords;
    Parts[2].VertexCount = 6;
    (void)RightVertices;
    (void)RightTexcoords;
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddPartsModel(run, &Fixture, "split", Parts, 3, 2, MatrixIdentity(), NULL)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"split\", \"lods\": [{\"model\": \"split\", "
            "\"max_distance\": 40}], \"surfaces\": [{\"name\": \"screen\", \"material_slot\": 1, \"resolution\": "
            "[64, 64]}]}"))
    {
        const WorldModel* Loaded = LoadWorldModel(run, &Fixture, "split", &User);
        const WorldModelSurface* Surface = (Loaded != NULL) ? WorldModel_GetSurface(Loaded, 0U) : NULL;
        TEST_CHECK(run, (Surface != NULL) && (WorldModelSurface_GetMesh(Surface)->_triangleCount == 4U));
        Matrix4Double Identity = Matrix4Double_Identity();
        const Vector2 Points[] = { { -0.3f, 0.1f }, { -0.05f, -0.35f }, { 0.2f, 0.3f }, { 0.45f, -0.1f } };
        for (size_t Index = 0U; (Surface != NULL) && (Index < (sizeof(Points) / sizeof(Points[0]))); Index++)
        {
            WorldModelSurfaceHit Hit;
            bool IsHit = WorldModelSurface_Raycast(Surface, &Identity, Vector3Double_Create((double)Points[Index].x,
                (double)Points[Index].y, 2.0), (Vector3){ 0.0f, 0.0f, -1.0f }, 5.0f, &Hit);
            Vector2 Expected = ExpectedUv(Points[Index].x, Points[Index].y);
            TEST_CHECK(run, IsHit && IsNear(Hit.Uv.x, Expected.x) && IsNear(Hit.Uv.y, Expected.y));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Coordinates beyond [0, 1] (a slot that runs past its picture) are met but not on the picture. */
static void ABorderBeyondThePictureIsOffIt(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture)
        && AddScreenModel(run, &Fixture, "bordered", MatrixIdentity(), BorderedTexcoords)
        && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"bordered\", \"lods\": [{\"model\": \"bordered\", "
            "\"max_distance\": 40}], \"surfaces\": [{\"name\": \"screen\", \"material_slot\": 1, \"resolution\": "
            "[64, 64]}]}"))
    {
        const WorldModel* Loaded = LoadWorldModel(run, &Fixture, "bordered", &User);
        const WorldModelSurface* Surface = (Loaded != NULL) ? WorldModel_GetSurface(Loaded, 0U) : NULL;
        Matrix4Double Identity = Matrix4Double_Identity();
        WorldModelSurfaceHit Hit;
        Vector3 Back = { 0.0f, 0.0f, -1.0f };
        TEST_CHECK(run, (Surface != NULL) && WorldModelSurface_Raycast(Surface, &Identity,
            Vector3Double_Create(-0.47, 0.0, 1.0), Back, 5.0f, &Hit) && !Hit.IsOnPicture && (Hit.Uv.x < 0.0f));
        TEST_CHECK(run, (Surface != NULL) && WorldModelSurface_Raycast(Surface, &Identity,
            Vector3Double_Create(0.0, -0.39, 1.0), Back, 5.0f, &Hit) && !Hit.IsOnPicture && (Hit.Uv.y > 1.0f));
        TEST_CHECK(run, (Surface != NULL) && WorldModelSurface_Raycast(Surface, &Identity,
            Vector3Double_Create(0.0, 0.0, 1.0), Back, 5.0f, &Hit) && Hit.IsOnPicture && IsNear(Hit.Uv.x, 0.5f));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Static functions: the screen component.
/* The component's defaults, its setters' rules (the table's), and its table's own check. */
static void TheScreenComponentStartsAtItsDefaultsAndChecksItsValues(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(WorldScreenComponent_GetPropertyTable()));
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, (const unsigned char*)u8"tv", &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object), WorldComponentType_Screen,
            &Component)))
    {
        WorldScreenComponent* Screen = WorldScreenComponent_FromComponent(Component);
        TEST_CHECK(run, (Screen != NULL) && (WorldScreenComponent_FromComponent(NULL) == NULL));
        TEST_CHECK(run, strcmp((const char*)WorldScreenComponent_GetSurface(Screen), "screen") == 0);
        TEST_CHECK(run, WorldScreenComponent_IsPowered(Screen) && (WorldScreenComponent_GetBrightness(Screen) == 1.0f));
        uint32_t Revision = WorldComponent_GetRevision(Component);
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetSurface(Screen, (const unsigned char*)u8"glass.front"));
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetIsPowered(Screen, false));
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetBrightness(Screen, 0.0f));
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetBrightness(Screen, WORLD_SCREEN_COMPONENT_MAX_BRIGHTNESS));
        TEST_CHECK(run, WorldComponent_GetRevision(Component) == (Revision + 4U));
        TEST_CHECK(run, (strcmp((const char*)WorldScreenComponent_GetSurface(Screen), "glass.front") == 0)
            && !WorldScreenComponent_IsPowered(Screen));
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetBrightness(Screen, -0.01f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetBrightness(Screen, 1000.5f), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetBrightness(Screen, NAN), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, WorldScreenComponent_GetBrightness(Screen) == WORLD_SCREEN_COMPONENT_MAX_BRIGHTNESS);
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetSurface(Screen, NULL), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetSurface(Screen, (const unsigned char*)u8"Glass"),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetSurface(Screen, (const unsigned char*)
            u8"a1234567890123456789012345678901234567890123456789012345678901234"), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, strcmp((const char*)WorldScreenComponent_GetSurface(Screen), "glass.front") == 0);
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetIsPowered(NULL, true), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetBrightness(NULL, 1.0f), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldScreenComponent_SetSurface(NULL, (const unsigned char*)u8"a"),
            ErrorCode_IllegalArgument);
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* A screen component is saved and read back with the world (JSON and GHDF), and a file names it "screen". */
static void TheScreenComponentIsSavedWithTheWorld(TestRun* run)
{
    World Source;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &Source, &Keys, NULL, NULL))
    {
        return;
    }
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    GenericBuffer Json;
    GenericBuffer_AllocateVariable(&Json, SCREEN_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Ghdf;
    GenericBuffer_AllocateVariable(&Ghdf, SCREEN_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, World_CreateObject(&Source, (const unsigned char*)u8"tv", &Object))
        && TEST_CHECK_SUCCESS(run, World_AddComponent(&Source, WorldObject_GetId(Object), WorldComponentType_Screen,
            &Component))
        && TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetSurface(WorldScreenComponent_FromComponent(Component),
            (const unsigned char*)u8"glass"))
        && TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetIsPowered(WorldScreenComponent_FromComponent(Component), false))
        && TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetBrightness(WorldScreenComponent_FromComponent(Component), 2.5f))
        && TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Source, &Json))
        && TEST_CHECK_SUCCESS(run, WorldFile_WriteGHDF(&Source, &Ghdf)))
    {
        TEST_CHECK(run, GenericBuffer_NullTerminate(&Json) && (strstr((const char*)Json._data, "\"screen\": {") != NULL)
            && (strstr((const char*)Json._data, "\"surface\": \"glass\"") != NULL));
        for (int Format = 0; Format < 2; Format++)
        {
            World Loaded;
            WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
            Error Result = (Format == 0) ? WorldFile_ReadJSON(Json._data, Json._count - 1U, &Options, &Loaded)
                : WorldFile_ReadGHDF(Ghdf._data, Ghdf._count, &Options, &Loaded);
            if (!TEST_CHECK_SUCCESS(run, Result))
            {
                continue;
            }
            WorldComponent* Read = NULL;
            TEST_CHECK(run, World_TryGetComponent(&Loaded, WorldObject_GetId(Object), WorldComponentType_Screen, &Read));
            const WorldScreenComponent* Screen = WorldScreenComponent_FromComponent(Read);
            TEST_CHECK(run, (Screen != NULL) && (strcmp((const char*)WorldScreenComponent_GetSurface(Screen), "glass") == 0)
                && !WorldScreenComponent_IsPowered(Screen) && (WorldScreenComponent_GetBrightness(Screen) == 2.5f));
            TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
        }
    }
    Memory_Free(Json._data);
    Memory_Free(Ghdf._data);
    TestWorld_Deconstruct(run, &Source, &Keys);
}


// Static functions: the screens.
/* A world with the screen box's world model, its screens and its collision world. */
typedef struct ScreensFixtureStruct
{
    TestAssetFixture Assets;
    World World;
    KeyRegistry Keys;
    WorldScreens* Screens;
    WorldCollision* Collision;
    bool IsWorldConstructed;
} ScreensFixture;

static bool ScreensFixture_Construct(TestRun* run, ScreensFixture* self, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    BoundingBox Box = { .min = { -0.5f, -0.5f, -0.5f }, .max = { 0.5f, 0.5f, 0.5f } };
    if (!TestAssetFixture_Construct(run, &self->Assets)
        || !AddScreenModel(run, &self->Assets, "screen_box", MatrixIdentity(), ScreenTexcoords)
        || !TestAssets_AddWorldModel(run, &self->Assets, ScreenBoxJson)
        || !TestAssets_AddModel(run, &self->Assets, "box", Box, NULL)
        || !TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"box\", \"lods\": [{\"model\": \"box\", "
            "\"max_distance\": 40}], \"collision\": \"bounds\"}")
        || !TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"two_surfaces\", \"lods\": [{\"model\": "
            "\"screen_box\", \"max_distance\": 40}], \"collision\": \"bounds\", \"surfaces\": [{\"name\": \"frame\", "
            "\"material_slot\": 0, \"resolution\": [64, 64], \"kind\": \"paper\"}, {\"name\": \"screen\", "
            "\"material_slot\": 1, \"resolution\": [320, 240]}]}")
        || !TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"cased_box\", \"lods\": [{\"model\": "
            "\"screen_box\", \"max_distance\": 40}], \"collision\": {\"sets\": [{\"name\": \"case\", \"shapes\": "
            "[{\"type\": \"box\", \"half_extents\": [1, 1, 0.2]}]}]}, \"surfaces\": [{\"name\": \"screen\", "
            "\"material_slot\": 1, \"resolution\": [320, 240]}]}")
        || !AddScreenModel(run, &self->Assets, "bordered_box", MatrixIdentity(), BorderedTexcoords)
        || !TestAssets_AddWorldModel(run, &self->Assets, "{\"name\": \"bordered_box\", \"lods\": [{\"model\": "
            "\"bordered_box\", \"max_distance\": 40}], \"collision\": \"bounds\", \"surfaces\": [{\"name\": "
            "\"screen\", \"material_slot\": 1, \"resolution\": [64, 64]}]}"))
    {
        return false;
    }
    self->IsWorldConstructed = TestWorld_Construct(run, &self->World, &self->Keys, self->Assets.Manager, logger);
    return self->IsWorldConstructed
        && TEST_CHECK_SUCCESS(run, WorldScreens_Create(&self->World, self->Assets.Manager, logger, &self->Screens))
        && TEST_CHECK_SUCCESS(run, WorldCollision_Create(&self->World, self->Assets.Manager, NULL, NULL,
            &self->Collision));
}

static void ScreensFixture_Deconstruct(TestRun* run, ScreensFixture* self)
{
    WorldCollision_Destroy(self->Collision);
    WorldScreens_Destroy(self->Screens);
    if (self->IsWorldConstructed)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
    TestAssetFixture_Deconstruct(run, &self->Assets);
}

/* Adds an object at @p position (the scene's: through TestWorld_At) with a model of @p worldModel, a collider, and when
 * asked a screen component; returns its id (0 on failure). */
static uint64_t AddObject(TestRun* run, ScreensFixture* self, const char* worldModel, Vector3 position, bool hasScreen)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&self->World, (const unsigned char*)u8"thing", &Object))
        || !TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(position)))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(&self->World, WorldObject_GetId(Object), WorldComponentType_Model,
            &Component))
        || !TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component),
            (const unsigned char*)worldModel))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(&self->World, WorldObject_GetId(Object),
            WorldComponentType_Collider, &Component)))
    {
        return 0U;
    }
    if (hasScreen && !TEST_CHECK_SUCCESS(run, World_AddComponent(&self->World, WorldObject_GetId(Object),
        WorldComponentType_Screen, &Component)))
    {
        return 0U;
    }
    return WorldObject_GetId(Object);
}

/* Brings the world's transforms, the screens and the colliders up to date. */
static void ScreensFixture_Sync(ScreensFixture* self)
{
    World_UpdateTransforms(&self->World);
    WorldScreens_Sync(self->Screens);
    WorldCollision_Sync(self->Collision);
}

/* A ray from the scene's @p from toward -Z, out to 10 m. */
static bool PointFrom(ScreensFixture* self, Vector3 from, bool isOcclusionTested, WorldScreenHit* outHit)
{
    return WorldScreens_Raycast(self->Screens, TestWorld_At(from), (Vector3){ 0.0f, 0.0f, -1.0f }, 10.0f,
        isOcclusionTested ? self->Collision : NULL, NULL, outHit);
}

/* Each screen resolves its object's world model and the surface it names; one that cannot show anything (no model
 * component, a world model that does not load, a surface its world model lacks) points at nothing, logged once. */
static void ScreensResolveTheSurfacesTheyName(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, SCREEN_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    bool HasLog = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "screens_resolve", &Directory))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data));
    ScreensFixture Fixture;
    if (HasLog && ScreensFixture_Construct(run, &Fixture, &Log))
    {
        uint64_t Good = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.0f, 0.0f, 0.0f }, true);
        uint64_t Misnamed = AddObject(run, &Fixture, "screen_box", (Vector3){ 5.0f, 0.0f, 0.0f }, true);
        uint64_t Missing = AddObject(run, &Fixture, "no_such_model", (Vector3){ 10.0f, 0.0f, 0.0f }, true);
        WorldObject* Bare = NULL;
        WorldComponent* Component = NULL;
        TEST_CHECK_SUCCESS(run, World_CreateObject(&Fixture.World, (const unsigned char*)u8"bare", &Bare));
        TEST_CHECK_SUCCESS(run, World_AddComponent(&Fixture.World, WorldObject_GetId(Bare), WorldComponentType_Screen,
            &Component));
        WorldComponent* Screen = NULL;
        TEST_CHECK(run, World_TryGetComponent(&Fixture.World, Misnamed, WorldComponentType_Screen, &Screen));
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetSurface(WorldScreenComponent_FromComponent(Screen),
            (const unsigned char*)u8"scren"));
        for (int Tick = 0; Tick < 3; Tick++)
        {
            ScreensFixture_Sync(&Fixture);
        }
        WorldScreenInfo Info;
        TEST_CHECK(run, WorldScreens_TryGetScreen(Fixture.Screens, Good, &Info) && (Info.SurfaceIndex == 0U)
            && (WorldModelSurface_GetWidth(Info.Surface) == TEST_PICTURE_WIDTH) && (Info.WorldModel != NULL));
        TEST_CHECK(run, !WorldScreens_TryGetScreen(Fixture.Screens, Misnamed, &Info));
        TEST_CHECK(run, !WorldScreens_TryGetScreen(Fixture.Screens, Missing, &Info));
        TEST_CHECK(run, !WorldScreens_TryGetScreen(Fixture.Screens, WorldObject_GetId(Bare), &Info));
        TEST_CHECK(run, !WorldScreens_TryGetScreen(Fixture.Screens, 9999U, &Info));
        // Another screen joining resolves only itself (the failures are not logged again), here on its world model's
        // second surface.
        uint64_t Second = AddObject(run, &Fixture, "two_surfaces", (Vector3){ 15.0f, 0.0f, 0.0f }, true);
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, WorldScreens_TryGetScreen(Fixture.Screens, Second, &Info) && (Info.SurfaceIndex == 1U)
            && (Info.Surface == WorldModel_GetSurface(Info.WorldModel, 1U)));
        // A change the screen does not care about logs nothing again; naming its surface right resolves it.
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetBrightness(WorldScreenComponent_FromComponent(Screen), 0.5f));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetSurface(WorldScreenComponent_FromComponent(Screen),
            (const unsigned char*)u8"screen"));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, WorldScreens_TryGetScreen(Fixture.Screens, Misnamed, &Info));
    }
    if (HasLog)
    {
        ScreensFixture_Deconstruct(run, &Fixture);
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
        TEST_CHECK(run, CountInLatestLog(run, Directory._data, "its world model has no surface named scren") == 1U);
        TEST_CHECK(run, CountInLatestLog(run, Directory._data, "its world model did not load: tbp:no_such_model") == 1U);
        TEST_CHECK(run, CountInLatestLog(run, Directory._data, "(\"bare\") cannot be pointed at: it has no model "
            "component naming a world model") == 1U);
    }
    Memory_Free(Directory._data);
}

/* The ray points at the nearest screen it meets from the front, where on its picture, with the point and the distance;
 * a disabled or hidden screen is not pointed at. */
static void TheRayPointsAtTheNearestScreenOnItsPicture(TestRun* run)
{
    ScreensFixture Fixture;
    if (ScreensFixture_Construct(run, &Fixture, NULL))
    {
        // The far screen first: the nearest is met whatever the order.
        uint64_t Far = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.1f, 0.0f, -2.0f }, true);
        uint64_t Near = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.0f, 0.0f, 0.0f }, true);
        ScreensFixture_Sync(&Fixture);
        WorldScreenHit Hit;
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.25f, 0.2f, 3.0f }, false, &Hit) && (Hit.ObjectId == Near)
            && Hit.IsOnPicture && IsNear(Hit.Uv.x, 0.75f) && IsNear(Hit.Uv.y, 0.25f) && IsNear(Hit.Distance, 2.99f)
            && (Hit.SurfaceIndex == 0U) && (Hit.Surface != NULL));
        Vector3 Point = TestWorld_ToScene(Hit.Point);
        TEST_CHECK(run, IsNear(Point.x, 0.25f) && IsNear(Point.y, 0.2f) && IsNear(Point.z, 0.01f));
        // Past the near screen's edge the far one is met, through the near one's bezel (no screen there).
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.55f, 0.0f, 3.0f }, false, &Hit) && (Hit.ObjectId == Far)
            && IsNear(Hit.Uv.x, 0.95f));
        // Disabled or hidden, the near screen is passed.
        WorldObject* Object = NULL;
        TEST_CHECK(run, World_TryGetObject(&Fixture.World, Near, &Object));
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, false));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.25f, 0.2f, 3.0f }, false, &Hit) && (Hit.ObjectId == Far));
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(Object, true));
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(Object, false));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.25f, 0.2f, 3.0f }, false, &Hit) && (Hit.ObjectId == Far));
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(Object, true));
        ScreensFixture_Sync(&Fixture);
        // Out of reach, beside both, or with no screens left: nothing.
        TEST_CHECK(run, !WorldScreens_Raycast(Fixture.Screens, TestWorld_At((Vector3){ 0.0f, 0.0f, 3.0f }),
            (Vector3){ 0.0f, 0.0f, -1.0f }, 2.0f, NULL, NULL, &Hit));
        TEST_CHECK(run, !PointFrom(&Fixture, (Vector3){ 3.0f, 0.0f, 3.0f }, false, &Hit));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Fixture.World, Near, WorldComponentType_Screen));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&Fixture.World, Far));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, !PointFrom(&Fixture, (Vector3){ 0.25f, 0.2f, 3.0f }, false, &Hit));
        // A screen on its world model's second surface; a border beyond a picture is met, off it.
        uint64_t Second = AddObject(run, &Fixture, "two_surfaces", (Vector3){ 10.0f, 0.0f, 0.0f }, true);
        uint64_t Bordered = AddObject(run, &Fixture, "bordered_box", (Vector3){ 20.0f, 0.0f, 0.0f }, true);
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 10.0f, 0.0f, 3.0f }, false, &Hit) && (Hit.ObjectId == Second)
            && (Hit.SurfaceIndex == 1U) && (strcmp((const char*)WorldModelSurface_GetName(Hit.Surface), "screen") == 0));
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 19.53f, 0.0f, 3.0f }, false, &Hit) && (Hit.ObjectId == Bordered)
            && !Hit.IsOnPicture && (Hit.Uv.x < 0.0f));
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 20.0f, 0.0f, 3.0f }, false, &Hit) && (Hit.ObjectId == Bordered)
            && Hit.IsOnPicture);
    }
    ScreensFixture_Deconstruct(run, &Fixture);
}

/* A collider of another object in front of a screen hides it; the screen's own colliders (its box around the glass) and
 * what lies behind it do not; the filter's ignored object (the one pointing) never does. */
static void OtherObjectsCollidersHideAScreenItsOwnDoNot(TestRun* run)
{
    ScreensFixture Fixture;
    if (ScreensFixture_Construct(run, &Fixture, NULL))
    {
        uint64_t Screen = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.0f, 0.0f, 0.0f }, true);
        uint64_t Wall = AddObject(run, &Fixture, "box", (Vector3){ 0.0f, 0.0f, -1.0f }, false);
        ScreensFixture_Sync(&Fixture);
        WorldScreenHit Hit;
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, true, &Hit) && (Hit.ObjectId == Screen));
        uint64_t CrateId = AddObject(run, &Fixture, "box", (Vector3){ 0.0f, 0.0f, 1.5f }, false);
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, !PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, true, &Hit));
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, false, &Hit) && (Hit.ObjectId == Screen));
        // Within a centimetre in front of the glass (a frame flush with it) a collider hides nothing; 3 cm in front it does.
        WorldObject* Crate = NULL;
        TEST_CHECK(run, World_TryGetObject(&Fixture.World, CrateId, &Crate));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Crate, TestWorld_At((Vector3){ 0.0f, 0.0f, 0.01f - 0.5f + 0.005f })));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, true, &Hit) && (Hit.ObjectId == Screen));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Crate, TestWorld_At((Vector3){ 0.0f, 0.0f, 0.01f - 0.5f + 0.03f })));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, !PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, true, &Hit));
        TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Crate, TestWorld_At((Vector3){ 0.0f, 0.0f, 1.5f })));
        ScreensFixture_Sync(&Fixture);
        // Pointing from inside the crate, as the crate (its own subtree ignored): the screen is seen.
        WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_MASK_ALL,
            WorldCollisionTriggerMode_Ignore);
        Filter.IgnoredObjectId = CrateId;
        Filter.IsIgnoredSubtree = true;
        TEST_CHECK(run, WorldScreens_Raycast(Fixture.Screens, TestWorld_At((Vector3){ 0.0f, 0.0f, 3.0f }),
            (Vector3){ 0.0f, 0.0f, -1.0f }, 10.0f, Fixture.Collision, &Filter, &Hit) && (Hit.ObjectId == Screen));
        // A filter that sees none of the crate's layer: the crate hides nothing.
        Filter = WorldCollisionFilter_Create(COLLISION_LAYER_BIT(CollisionLayer_World), WorldCollisionTriggerMode_Ignore);
        TEST_CHECK(run, WorldScreens_Raycast(Fixture.Screens, TestWorld_At((Vector3){ 0.0f, 0.0f, 3.0f }),
            (Vector3){ 0.0f, 0.0f, -1.0f }, 10.0f, Fixture.Collision, &Filter, &Hit) && (Hit.ObjectId == Screen));
        TEST_CHECK(run, Wall != 0U);
        // A screen whose own box encloses its glass (20 cm in front of it) is pointed at through the box.
        uint64_t Cased = AddObject(run, &Fixture, "cased_box", (Vector3){ 5.0f, 0.0f, 0.0f }, true);
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 5.0f, 0.0f, 3.0f }, true, &Hit) && (Hit.ObjectId == Cased));
    }
    ScreensFixture_Deconstruct(run, &Fixture);
}

/* Between syncs a screen whose object or component the world removed is not pointed at (nothing removed is read), and
 * neither is a component added in its place, until the next sync. */
static void AScreenRemovedBetweenSyncsIsNotPointedAt(TestRun* run)
{
    ScreensFixture Fixture;
    if (ScreensFixture_Construct(run, &Fixture, NULL))
    {
        uint64_t Near = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.0f, 0.0f, 0.0f }, true);
        uint64_t Far = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.1f, 0.0f, -2.0f }, true);
        ScreensFixture_Sync(&Fixture);
        WorldScreenHit Hit;
        WorldScreenInfo Info;
        Vector3 From = { 0.25f, 0.2f, 3.0f };
        TEST_CHECK(run, PointFrom(&Fixture, From, false, &Hit) && (Hit.ObjectId == Near));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Fixture.World, Near, WorldComponentType_Screen));
        TEST_CHECK(run, PointFrom(&Fixture, From, false, &Hit) && (Hit.ObjectId == Far));
        TEST_CHECK(run, !WorldScreens_TryGetScreen(Fixture.Screens, Near, &Info));
        WorldComponent* Component = NULL;
        TEST_CHECK_SUCCESS(run, World_AddComponent(&Fixture.World, Near, WorldComponentType_Screen, &Component));
        TEST_CHECK(run, PointFrom(&Fixture, From, false, &Hit) && (Hit.ObjectId == Far));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, PointFrom(&Fixture, From, false, &Hit) && (Hit.ObjectId == Near));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&Fixture.World, Near));
        TEST_CHECK_SUCCESS(run, World_RemoveObject(&Fixture.World, Far));
        TEST_CHECK(run, !PointFrom(&Fixture, From, false, &Hit));
        TEST_CHECK(run, !WorldScreens_TryGetScreen(Fixture.Screens, Far, &Info));
    }
    ScreensFixture_Deconstruct(run, &Fixture);
}

/* The screens follow their components: a surface renamed, a world model swapped, a component replaced. */
static void ScreensFollowTheirComponents(TestRun* run)
{
    ScreensFixture Fixture;
    if (ScreensFixture_Construct(run, &Fixture, NULL))
    {
        uint64_t Id = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.0f, 0.0f, 0.0f }, true);
        ScreensFixture_Sync(&Fixture);
        WorldScreenHit Hit;
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, false, &Hit) && (Hit.ObjectId == Id));
        WorldComponent* ModelComponent = NULL;
        TEST_CHECK(run, World_TryGetComponent(&Fixture.World, Id, WorldComponentType_Model, &ModelComponent));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(ModelComponent),
            (const unsigned char*)u8"box"));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, !PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, false, &Hit));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(ModelComponent),
            (const unsigned char*)u8"screen_box"));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, false, &Hit) && (Hit.ObjectId == Id));
        // Replaced by a component naming a surface the world model lacks, then removed.
        WorldComponent* Screen = NULL;
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Fixture.World, Id, WorldComponentType_Screen));
        TEST_CHECK_SUCCESS(run, World_AddComponent(&Fixture.World, Id, WorldComponentType_Screen, &Screen));
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetSurface(WorldScreenComponent_FromComponent(Screen),
            (const unsigned char*)u8"other"));
        ScreensFixture_Sync(&Fixture);
        TEST_CHECK(run, !PointFrom(&Fixture, (Vector3){ 0.0f, 0.0f, 3.0f }, false, &Hit));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Fixture.World, Id, WorldComponentType_Model));
        ScreensFixture_Sync(&Fixture);
        WorldScreenInfo Info;
        TEST_CHECK(run, !WorldScreens_TryGetScreen(Fixture.Screens, Id, &Info));
    }
    ScreensFixture_Deconstruct(run, &Fixture);
    TEST_CHECK_ERROR(run, WorldScreens_Create(NULL, NULL, NULL, NULL), ErrorCode_IllegalArgument);
}


// Static functions: the host.
static Error HostProbe_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    HostProbe* Probe = self;
    if (args->Type == UIMouseInputType_ButtonPress)
    {
        Probe->PressCount++;
    }
    return Error_CreateSuccess();
}

static Error HostProbe_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    HostProbe* Probe = self;
    if (args->Type == UIKeyboardInputType_Text)
    {
        Probe->TextCount++;
        Probe->LastCodepoint = args->Codepoint;
    }
    else if (args->Type == UIKeyboardInputType_KeyPress)
    {
        Probe->KeyCount++;
        Probe->LastKey = args->Key;
    }
    *outIsHandled = (args->Type == UIKeyboardInputType_Text) ? Probe->IsHandlingText
        : (Probe->IsHandlingKeys && (args->Key != GameKey_Escape));
    return Error_CreateSuccess();
}

static const WidgetVTable HostProbeVTable =
{
    .OnMouseInput = HostProbe_OnMouseInput,
    .OnKeyboardInput = HostProbe_OnKeyboardInput
};

static bool HostFixture_Construct(TestRun* run, HostFixture* self, uint64_t objectId)
{
    Memory_Zero(self, sizeof(*self));
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"HostProbe", .StructSize = sizeof(HostProbe), .VTable = &HostProbeVTable, .IsFocusable = true
    };
    bool IsBuilt = TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&self->Keys))
        && TEST_CHECK_SUCCESS(run, GameInput_Construct(&self->Input, &self->Keys, NULL, NULL))
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_Construct(&self->Factory))
        && TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&self->Factory, &Descriptor, &self->ProbeType));
    self->IsScreenConstructed = IsBuilt && TEST_CHECK_SUCCESS(run, UIScreen_Construct(&self->Screen, &self->Factory,
        NULL));
    GameInputSample_Clear(&self->Sample);
    self->Sample.WindowSize = (Vector2){ 1280.0f, 720.0f };
    self->Sample.IsCursorCaptured = true;
    WorldScreenHost_Construct(&self->Host, objectId);
    self->IsReady = self->IsScreenConstructed;
    return self->IsReady;
}

static void HostFixture_Deconstruct(TestRun* run, HostFixture* self)
{
    WorldScreenHost_Deconstruct(&self->Host);
    if (self->IsScreenConstructed)
    {
        TEST_CHECK_SUCCESS(run, UIScreen_Deconstruct(&self->Screen));
    }
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_Deconstruct(&self->Factory));
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&self->Input));
    KeyRegistry_Deconstruct(&self->Keys);
}

/* A probe over the box (screen-relative), initialized and added as a root. */
static HostProbe* AddProbe(TestRun* run, HostFixture* self, Rectangle box)
{
    Widget* Created = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&self->Factory, &self->Screen, self->ProbeType, NULL,
        &Created)))
    {
        return NULL;
    }
    TEST_CHECK_SUCCESS(run, Widget_SetPosition(Created, (Vector2){ box.x, box.y }));
    TEST_CHECK_SUCCESS(run, Widget_SetSize(Created, (Vector2){ box.width, box.height }));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    if (!TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&self->Screen, Created)))
    {
        return NULL;
    }
    return (HostProbe*)Created;
}

/* Begins a tick with the sample (the host's update follows); its presses become holds and its releases end after. */
static void BeginTick(HostFixture* self)
{
    GameInput_BeginTick(&self->Input, &self->Sample);
}

static void EndTick(HostFixture* self)
{
    for (int32_t Index = 0; Index < (int32_t)GameKey_Count; Index++)
    {
        uint8_t State = self->Sample.KeyStates[Index];
        self->Sample.KeyStates[Index] = ((State & GAME_INPUT_KEY_STATE_RELEASED) != 0U) ? 0U
            : (uint8_t)(State & GAME_INPUT_KEY_STATE_DOWN);
    }
    self->Sample.TextCodepointCount = 0U;
}

/* One tick: the sample in, the host's update with @p pointed, then the tick's end. */
static void HostTick(TestRun* run, HostFixture* self, const WorldScreenHit* pointed)
{
    BeginTick(self);
    self->Time += HOST_TICK_SECONDS;
    TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&self->Host, &self->Screen,
        ProgramTime_Create(self->Time, HOST_TICK_SECONDS), &self->Input, pointed));
    EndTick(self);
}

static void Press(HostFixture* self, GameKey key)
{
    self->Sample.KeyStates[key] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_PRESSED;
}

/* A hit on the hosted screen at @p uv. */
static WorldScreenHit HitAt(uint64_t objectId, Vector2 uv, bool isOnPicture)
{
    return (WorldScreenHit){ .ObjectId = objectId, .Surface = NULL, .SurfaceIndex = 0U, .Uv = uv,
        .IsOnPicture = isOnPicture, .Point = Vector3Double_Zero(), .Distance = 1.0f };
}

/* The pointer is where the player points on the hosted screen's picture, the UI's own position as it is; elsewhere (off
 * the picture, at another screen, nowhere, under UI above that claimed the pointer) there is none. */
static void ThePointerIsWhereThePlayerPointsOnThePicture(TestRun* run)
{
    HostFixture Fixture;
    if (HostFixture_Construct(run, &Fixture, 7U))
    {
        WorldScreenHit OnIt = HitAt(7U, (Vector2){ 0.3f, 0.6f }, true);
        HostTick(run, &Fixture, &OnIt);
        TEST_CHECK(run, WorldScreenHost_IsPointed(&Fixture.Host) && (WorldScreenHost_GetObjectId(&Fixture.Host) == 7U));
        TEST_CHECK(run, IsNear(WorldScreenHost_GetPointer(&Fixture.Host).x, 0.3f)
            && IsNear(WorldScreenHost_GetPointer(&Fixture.Host).y, 0.6f));
        TEST_CHECK(run, UIScreen_IsPointerPresent(&Fixture.Screen) && IsNear(UIScreen_GetPointerPosition(&Fixture.Screen).y,
            0.6f));
        WorldScreenHit OffIt = HitAt(7U, (Vector2){ 1.05f, 0.5f }, false);
        HostTick(run, &Fixture, &OffIt);
        TEST_CHECK(run, !WorldScreenHost_IsPointed(&Fixture.Host) && !UIScreen_IsPointerPresent(&Fixture.Screen));
        WorldScreenHit Other = HitAt(8U, (Vector2){ 0.5f, 0.5f }, true);
        HostTick(run, &Fixture, &Other);
        TEST_CHECK(run, !WorldScreenHost_IsPointed(&Fixture.Host));
        HostTick(run, &Fixture, NULL);
        TEST_CHECK(run, !WorldScreenHost_IsPointed(&Fixture.Host));
        BeginTick(&Fixture);
        GameInput_ConsumePointer(&Fixture.Input);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.0, 0.1),
            &Fixture.Input, &OnIt));
        EndTick(&Fixture);
        TEST_CHECK(run, !WorldScreenHost_IsPointed(&Fixture.Host));
        // Without input the screen gets none (no pointer) and keeps its focus.
        Press(&Fixture, GameKey_E);
        HostTick(run, &Fixture, &OnIt);
        TEST_CHECK(run, WorldScreenHost_IsPointed(&Fixture.Host) && WorldScreenHost_IsFocused(&Fixture.Host));
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.0, 0.1), NULL,
            &OnIt));
        TEST_CHECK(run, !WorldScreenHost_IsPointed(&Fixture.Host) && WorldScreenHost_IsFocused(&Fixture.Host)
            && !UIScreen_IsPointerPresent(&Fixture.Screen));
    }
    HostFixture_Deconstruct(run, &Fixture);
    // A host of no screen is never pointed at.
    if (HostFixture_Construct(run, &Fixture, 0U))
    {
        WorldScreenHit Zero = HitAt(0U, (Vector2){ 0.5f, 0.5f }, true);
        HostTick(run, &Fixture, &Zero);
        TEST_CHECK(run, !WorldScreenHost_IsPointed(&Fixture.Host));
    }
    HostFixture_Deconstruct(run, &Fixture);
}

/* The use action or a click on the picture gives the screen the keyboard. The press of use is consumed with the tick's
 * text (it neither acts in the game nor types); the click reaches the UI under the pointer, and a press on the picture is
 * the screen's even where no widget is. Nothing focuses a screen the player does not point at. */
static void UseOrAClickOnThePictureFocusesTheScreen(TestRun* run)
{
    HostFixture Fixture;
    if (HostFixture_Construct(run, &Fixture, 7U))
    {
        HostProbe* Probe = AddProbe(run, &Fixture, (Rectangle){ 0.0f, 0.0f, 0.5f, 1.0f });
        uint64_t ProbeId = (Probe != NULL) ? Widget_GetId(&Probe->Base) : 0U;
        WorldScreenHit Away = HitAt(8U, (Vector2){ 0.2f, 0.5f }, true);
        Press(&Fixture, GameKey_E);
        HostTick(run, &Fixture, &Away);
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Fixture.Host));
        // A click elsewhere is the game's.
        Press(&Fixture, GameKey_MouseLeft);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(0.9, 0.1),
            &Fixture.Input, &Away));
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Fixture.Host) && GameInput_IsKeyPressed(&Fixture.Input,
            GameKey_MouseLeft));
        EndTick(&Fixture);
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &Away);
        // Use: focused, the press and the text consumed.
        WorldScreenHit OnProbe = HitAt(7U, (Vector2){ 0.2f, 0.5f }, true);
        Press(&Fixture, GameKey_E);
        Fixture.Sample.TextCodepoints[0] = 'e';
        Fixture.Sample.TextCodepointCount = 1U;
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.0, 0.1),
            &Fixture.Input, &OnProbe));
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host));
        TEST_CHECK(run, !GameInput_IsActionPressed(&Fixture.Input, GameAction_Use)
            && (GameInput_GetTextCodepointCount(&Fixture.Input) == 0U));
        EndTick(&Fixture);
        TEST_CHECK(run, (Probe != NULL) && (Probe->TextCount == 0));
        WorldScreenHost_Release(&Fixture.Host);
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Fixture.Host));

        // A click on the probe: focused, the press reaches the probe and the game never sees it.
        Press(&Fixture, GameKey_MouseLeft);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.1, 0.1),
            &Fixture.Input, &OnProbe));
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host) && !GameInput_IsKeyPressed(&Fixture.Input,
            GameKey_MouseLeft));
        EndTick(&Fixture);
        Widget* Resolved = NULL;
        TEST_CHECK(run, UIScreen_TryGetWidgetById(&Fixture.Screen, ProbeId, &Resolved) && (Probe->PressCount == 1)
            && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == ProbeId));
        WorldScreenHost_Release(&Fixture.Host);
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnProbe);

        // Let go of, the probe keeps the UI's focus; pointed at but not used, the keys and the text are the game's.
        int KeysBefore = Probe->KeyCount;
        Press(&Fixture, GameKey_W);
        Fixture.Sample.TextCodepoints[0] = 'w';
        Fixture.Sample.TextCodepointCount = 1U;
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.15, 0.1),
            &Fixture.Input, &OnProbe));
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Fixture.Host)
            && GameInput_IsActionDown(&Fixture.Input, GameAction_MoveForward)
            && (GameInput_GetTextCodepointCount(&Fixture.Input) == 1U));
        EndTick(&Fixture);
        TEST_CHECK(run, (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == ProbeId) && (Probe->KeyCount == KeysBefore)
            && (Probe->TextCount == 0));
        Fixture.Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnProbe);
        // With the UI's focus on the probe, the press of use that starts using the screen neither reaches it as a key
        // nor types its letter.
        Press(&Fixture, GameKey_E);
        Fixture.Sample.TextCodepoints[0] = 'e';
        Fixture.Sample.TextCodepointCount = 1U;
        HostTick(run, &Fixture, &OnProbe);
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == ProbeId)
            && (Probe->KeyCount == KeysBefore) && (Probe->TextCount == 0));
        // Held on, its key's repeats type nothing; let go of, the keys type again.
        for (int Tick = 0; Tick < 2; Tick++)
        {
            Fixture.Sample.KeyStates[GameKey_E] = GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_REPEATED;
            Fixture.Sample.TextCodepoints[0] = 'e';
            Fixture.Sample.TextCodepointCount = 1U;
            HostTick(run, &Fixture, &OnProbe);
        }
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host) && (Probe->TextCount == 0));
        Fixture.Sample.KeyStates[GameKey_E] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnProbe);
        Fixture.Sample.TextCodepoints[0] = 'x';
        Fixture.Sample.TextCodepointCount = 1U;
        HostTick(run, &Fixture, &OnProbe);
        TEST_CHECK(run, (Probe->TextCount == 1) && (Probe->LastCodepoint == 'x'));
        WorldScreenHost_Release(&Fixture.Host);

        // A click on the picture beside every widget: focused, and still the screen's.
        WorldScreenHit Beside = HitAt(7U, (Vector2){ 0.8f, 0.5f }, true);
        Press(&Fixture, GameKey_MouseLeft);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.2, 0.1),
            &Fixture.Input, &Beside));
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host) && !GameInput_IsKeyPressed(&Fixture.Input,
            GameKey_MouseLeft));
        EndTick(&Fixture);
        TEST_CHECK(run, Probe->PressCount == 1);
        // A right press on the picture is the screen's too (but does not focus).
        WorldScreenHost_Release(&Fixture.Host);
        Press(&Fixture, GameKey_MouseRight);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.3, 0.1),
            &Fixture.Input, &Beside));
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Fixture.Host) && !GameInput_IsKeyPressed(&Fixture.Input,
            GameKey_MouseRight));
        EndTick(&Fixture);
    }
    HostFixture_Deconstruct(run, &Fixture);
}

/* While focused the UI reads every key and the text first, then the host takes the whole keyboard and the text, so the
 * game below sees no key (the movement actions included); unfocused, the keys are the game's. */
static void AFocusedScreenTakesTheWholeKeyboardAfterItsUi(TestRun* run)
{
    HostFixture Fixture;
    if (HostFixture_Construct(run, &Fixture, 7U))
    {
        HostProbe* Probe = AddProbe(run, &Fixture, (Rectangle){ 0.0f, 0.0f, 1.0f, 1.0f });
        WorldScreenHit OnIt = HitAt(7U, (Vector2){ 0.5f, 0.5f }, true);
        // Unfocused: W walks.
        Press(&Fixture, GameKey_W);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.0, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, GameInput_IsActionDown(&Fixture.Input, GameAction_MoveForward));
        EndTick(&Fixture);
        // Focused by a click in the tick W is let go: the release is the screen's too.
        Fixture.Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_RELEASED;
        Press(&Fixture, GameKey_MouseLeft);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.1, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host) && (Probe != NULL)
            && !GameInput_IsKeyReleased(&Fixture.Input, GameKey_W));
        EndTick(&Fixture);
        // Then, the probe focused in the UI: the keys and the text reach it, then nothing else sees them.
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_RELEASED;
        Press(&Fixture, GameKey_W);
        Press(&Fixture, GameKey_LeftShift);
        Fixture.Sample.TextCodepoints[0] = 'W';
        Fixture.Sample.TextCodepointCount = 1U;
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.2, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, (Probe->KeyCount >= 1) && (Probe->TextCount == 1) && (Probe->LastCodepoint == 'W'));
        TEST_CHECK(run, !GameInput_IsActionDown(&Fixture.Input, GameAction_MoveForward)
            && !GameInput_IsActionDown(&Fixture.Input, GameAction_Sprint)
            && !GameInput_IsKeyDown(&Fixture.Input, GameKey_W) && (GameInput_GetTextCodepointCount(&Fixture.Input) == 0U));
        EndTick(&Fixture);
        // Held on, they stay taken; a mouse button held since before is not the keyboard's.
        Fixture.Sample.KeyStates[GameKey_MouseRight] = GAME_INPUT_KEY_STATE_DOWN;
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.3, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, !GameInput_IsKeyDown(&Fixture.Input, GameKey_W) && WorldScreenHost_IsFocused(&Fixture.Host)
            && GameInput_IsKeyDown(&Fixture.Input, GameKey_MouseRight));
        EndTick(&Fixture);
        // Let go of, they stay taken: nothing below sees W's release.
        Fixture.Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_RELEASED;
        Fixture.Sample.KeyStates[GameKey_MouseRight] = 0U;
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.35, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, !GameInput_IsKeyReleased(&Fixture.Input, GameKey_W)
            && !GameInput_IsActionReleased(&Fixture.Input, GameAction_MoveForward));
        EndTick(&Fixture);
        // The mouse still turns the view: its movement is not the keyboard's.
        Fixture.Sample.MouseDelta = (Vector2){ 12.0f, -3.0f };
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.4, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, GameInput_GetMouseDelta(&Fixture.Input).x == 12.0f);
        EndTick(&Fixture);
    }
    HostFixture_Deconstruct(run, &Fixture);
}

/* An Escape the UI leaves unused lets go of the screen and is consumed (no pause); one the UI uses (clearing its focus)
 * does not. Pointing away lets go too, and the keys are the game's again. */
static void EscapeAndPointingAwayLetGo(TestRun* run)
{
    HostFixture Fixture;
    if (HostFixture_Construct(run, &Fixture, 7U))
    {
        HostProbe* Probe = AddProbe(run, &Fixture, (Rectangle){ 0.0f, 0.0f, 1.0f, 1.0f });
        WorldScreenHit OnIt = HitAt(7U, (Vector2){ 0.5f, 0.5f }, true);
        // Focused by a click, the probe has the UI's focus: the first Escape is the UI's (it clears that focus).
        Press(&Fixture, GameKey_MouseLeft);
        HostTick(run, &Fixture, &OnIt);
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnIt);
        TEST_CHECK(run, (Probe != NULL) && WorldScreenHost_IsFocused(&Fixture.Host)
            && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Widget_GetId(&Probe->Base)));
        Press(&Fixture, GameKey_Escape);
        HostTick(run, &Fixture, &OnIt);
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host)
            && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID));
        Fixture.Sample.KeyStates[GameKey_Escape] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnIt);
        // With nothing for the UI to close, Escape lets go, and the game does not see it as pause.
        Press(&Fixture, GameKey_Escape);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.0, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Fixture.Host) && !GameInput_IsActionPressed(&Fixture.Input,
            GameAction_Pause) && !GameInput_IsActionPressed(&Fixture.Input, GameAction_UIBack));
        EndTick(&Fixture);
        Fixture.Sample.KeyStates[GameKey_Escape] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnIt);
        // Unfocused, Escape is the game's.
        Press(&Fixture, GameKey_Escape);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.1, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, GameInput_IsActionPressed(&Fixture.Input, GameAction_Pause));
        EndTick(&Fixture);
        Fixture.Sample.KeyStates[GameKey_Escape] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnIt);
        // Focused by use, then pointing away: let go at once, and W walks again.
        Press(&Fixture, GameKey_E);
        HostTick(run, &Fixture, &OnIt);
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host));
        Fixture.Sample.KeyStates[GameKey_E] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnIt);
        WorldScreenHit OffIt = HitAt(7U, (Vector2){ -0.2f, 0.5f }, false);
        Press(&Fixture, GameKey_W);
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.2, 0.1),
            &Fixture.Input, &OffIt));
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Fixture.Host) && GameInput_IsActionDown(&Fixture.Input,
            GameAction_MoveForward));
        EndTick(&Fixture);
        Fixture.Sample.KeyStates[GameKey_W] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OffIt);
        // With pause on a key of its own, that key lets go too (its letter taken by no widget), and does not pause the
        // game.
        GameActionOverride PauseOnP = { .OverridesDefaultBindings = true,
            .DefaultBindings = { { .Key = GameKey_P, .Modifiers = GAME_KEY_MODIFIER_NONE } }, .DefaultBindingCount = 1U };
        TEST_CHECK_SUCCESS(run, GameInput_OverrideAction(&Fixture.Input, GameAction_Pause, &PauseOnP,
            NAMESPACED_KEY_ID_NONE));
        Press(&Fixture, GameKey_E);
        HostTick(run, &Fixture, &OnIt);
        Fixture.Sample.KeyStates[GameKey_E] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnIt);
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host) && (UIScreen_GetFocusedWidgetId(&Fixture.Screen)
            == WIDGET_ID_INVALID));
        Press(&Fixture, GameKey_P);
        Fixture.Sample.TextCodepoints[0] = 'p';
        Fixture.Sample.TextCodepointCount = 1U;
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.3, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, !WorldScreenHost_IsFocused(&Fixture.Host) && !GameInput_IsActionPressed(&Fixture.Input,
            GameAction_Pause));
        EndTick(&Fixture);
        Fixture.Sample.KeyStates[GameKey_P] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnIt);
        // Typed into a widget that takes text, that key is typing: the screen keeps the keyboard, the game no pause.
        Probe->IsHandlingText = true;
        Press(&Fixture, GameKey_MouseLeft);
        HostTick(run, &Fixture, &OnIt);
        Fixture.Sample.KeyStates[GameKey_MouseLeft] = GAME_INPUT_KEY_STATE_RELEASED;
        HostTick(run, &Fixture, &OnIt);
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host)
            && (UIScreen_GetFocusedWidgetId(&Fixture.Screen) == Widget_GetId(&Probe->Base)));
        int TextBefore = Probe->TextCount;
        Press(&Fixture, GameKey_P);
        Fixture.Sample.TextCodepoints[0] = 'p';
        Fixture.Sample.TextCodepointCount = 1U;
        BeginTick(&Fixture);
        TEST_CHECK_SUCCESS(run, WorldScreenHost_Update(&Fixture.Host, &Fixture.Screen, ProgramTime_Create(1.4, 0.1),
            &Fixture.Input, &OnIt));
        TEST_CHECK(run, WorldScreenHost_IsFocused(&Fixture.Host) && !GameInput_IsActionPressed(&Fixture.Input,
            GameAction_Pause) && (Probe->TextCount == (TextBefore + 1)) && (Probe->LastCodepoint == 'p'));
        EndTick(&Fixture);
    }
    HostFixture_Deconstruct(run, &Fixture);
}


// Static functions: the renderer's side.
/* The render cache takes each model entry's screen: the surface it names (the world model's), whether it is on and its
 * glow (the surface's emission times the brightness); a surface the world model lacks is logged once however the
 * component changes otherwise, and a removed component takes the surface away. */
static void TheRenderCacheTakesTheScreensSurface(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, SCREEN_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    Logger Log;
    bool HasLog = TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "screens_cache", &Directory))
        && TEST_CHECK_SUCCESS(run, Logger_Construct(&Log, Directory._data));
    ScreensFixture Fixture;
    WorldRenderCache Cache;
    WorldRenderCache_Construct(&Cache);
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (HasLog && ScreensFixture_Construct(run, &Fixture, NULL)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Assets.Manager, &User)))
    {
        uint64_t Id = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.0f, 0.0f, 0.0f }, true);
        uint64_t Plain = AddObject(run, &Fixture, "screen_box", (Vector3){ 3.0f, 0.0f, 0.0f }, false);
        WorldComponent* Component = NULL;
        TEST_CHECK(run, World_TryGetComponent(&Fixture.World, Id, WorldComponentType_Screen, &Component));
        WorldScreenComponent* Screen = WorldScreenComponent_FromComponent(Component);
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetBrightness(Screen, 4.0f));
        size_t Count = 0U;
        const WorldRenderModel* Models = NULL;
        if (TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Cache, &Fixture.World, Vector3Double_Zero(),
            Fixture.Assets.Manager, User, &Log)))
        {
            Models = WorldRenderCache_GetModels(&Cache, &Count);
            TEST_CHECK(run, (Count == 2U) && (Models[0].ObjectId == Id) && (Models[1].ObjectId == Plain));
            TEST_CHECK(run, (Count == 2U) && (Models[0].Surface != NULL) && (Models[1].Surface == NULL));
            TEST_CHECK(run, (Count == 2U) && Models[0].IsSurfacePowered
                && IsNear(Models[0].SurfaceEmission, 4.0f * WORLD_MODEL_DEFAULT_SURFACE_EMISSION));
        }
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetIsPowered(Screen, false));
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetSurface(Screen, (const unsigned char*)u8"gone"));
        for (int Frame = 0; Frame < 3; Frame++)
        {
            TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetBrightness(Screen, (float)Frame));
            TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Cache, &Fixture.World, Vector3Double_Zero(),
                Fixture.Assets.Manager, User, &Log));
        }
        Models = WorldRenderCache_GetModels(&Cache, &Count);
        TEST_CHECK(run, (Count == 2U) && (Models[0].Surface == NULL));
        TEST_CHECK_SUCCESS(run, WorldScreenComponent_SetSurface(Screen, (const unsigned char*)u8"screen"));
        TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Cache, &Fixture.World, Vector3Double_Zero(), Fixture.Assets.Manager,
            User, &Log));
        Models = WorldRenderCache_GetModels(&Cache, &Count);
        TEST_CHECK(run, (Count == 2U) && (Models[0].Surface != NULL) && !Models[0].IsSurfacePowered
            && IsNear(Models[0].SurfaceEmission, 2.0f * WORLD_MODEL_DEFAULT_SURFACE_EMISSION));
        // Its object's world model swapped: the surface of that name in the new one.
        WorldComponent* ModelComponent = NULL;
        TEST_CHECK(run, World_TryGetComponent(&Fixture.World, Id, WorldComponentType_Model, &ModelComponent));
        TEST_CHECK_SUCCESS(run, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(ModelComponent),
            (const unsigned char*)u8"two_surfaces"));
        TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Cache, &Fixture.World, Vector3Double_Zero(), Fixture.Assets.Manager,
            User, &Log));
        Models = WorldRenderCache_GetModels(&Cache, &Count);
        TEST_CHECK(run, (Count == 2U) && (Models[0].Asset != NULL)
            && (Models[0].Surface == WorldModel_GetSurface(Models[0].Asset, 1U)));
        TEST_CHECK_SUCCESS(run, World_RemoveComponent(&Fixture.World, Id, WorldComponentType_Screen));
        TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Cache, &Fixture.World, Vector3Double_Zero(), Fixture.Assets.Manager,
            User, &Log));
        Models = WorldRenderCache_GetModels(&Cache, &Count);
        TEST_CHECK(run, (Count == 2U) && (Models[0].Surface == NULL));
    }
    WorldRenderCache_Deconstruct(&Cache);
    if (HasLog)
    {
        ScreensFixture_Deconstruct(run, &Fixture);
        TEST_CHECK_SUCCESS(run, Logger_Deconstruct(&Log));
        TEST_CHECK(run, CountInLatestLog(run, Directory._data, "shows no picture: world model \"tbp:screen_box\" has no "
            "surface \"gone\"") == 1U);
    }
    Memory_Free(Directory._data);
}

/* The renderer's pictures: held per object and surface (a second one replaces the first), put into the surface's
 * material copy (a screen's as its glowing emissive map while it is on, a paper's as its albedo map; without a picture
 * or off, a screen gives off no light), and forgotten after the render. */
static void ThePictureGoesIntoTheSlotsMaterialCopyForOneRender(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    const WorldModelSurface* Surface = LoadScreenBox(run, &Fixture, &User);
    WorldRenderer* Renderer = Memory_Allocate(sizeof(WorldRenderer));
    Memory_Zero(Renderer, sizeof(*Renderer));
    GenericBuffer_AllocateVariable(&Renderer->_surfaces._bindings, 1U, sizeof(WorldRendererSurfaceBinding));
    WorldRenderCache_Construct(&Renderer->_cache);
    if (Surface != NULL)
    {
        Texture2D Picture = { .id = PICTURE_TEXTURE_ID, .width = 320, .height = 240, .mipmaps = 1, .format = 7 };
        Texture2D Other = Picture;
        Other.id = OTHER_TEXTURE_ID;
        TEST_CHECK_ERROR(run, WorldRenderer_SetSurfaceTexture(NULL, 7U, (const unsigned char*)u8"screen", Picture, true),
            ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldRenderer_SetSurfaceTexture(Renderer, 0U, (const unsigned char*)u8"screen", Picture,
            true), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldRenderer_SetSurfaceTexture(Renderer, 7U, NULL, Picture, true),
            ErrorCode_IllegalArgument);
        Texture2D None = Picture;
        None.id = 0U;
        TEST_CHECK_ERROR(run, WorldRenderer_SetSurfaceTexture(Renderer, 7U, (const unsigned char*)u8"screen", None, true),
            ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, WorldRenderer_SetSurfaceTexture(Renderer, 7U, (const unsigned char*)
            u8"a12345678901234567890123456789012345678901234567890123456789012345", Picture, true),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Renderer->_surfaces._bindings._count == 0U);
        TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, 7U, (const unsigned char*)u8"screen", Other,
            false));
        TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, 7U, (const unsigned char*)u8"screen", Picture,
            true));
        TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, 8U, (const unsigned char*)u8"screen", Other,
            false));
        TEST_CHECK(run, Renderer->_surfaces._bindings._count == 2U);

        WorldRenderModel Entry;
        Memory_Zero(&Entry, sizeof(Entry));
        Entry.ObjectId = 7U;
        Entry.Surface = Surface;
        Entry.IsSurfacePowered = true;
        Entry.SurfaceEmission = 0.25f;
        MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
        Memory_Zero(Maps, sizeof(Maps));
        Maps[MATERIAL_MAP_EMISSION].texture.id = 55U;
        Maps[MATERIAL_MAP_EMISSION].color = (Color){ 10, 20, 30, 255 };
        Maps[MATERIAL_MAP_EMISSION].value = 3.0f;
        Maps[MATERIAL_MAP_ALBEDO].texture.id = 56U;
        Material Copy = { .maps = Maps };
        WorldRendererMapOrientation Orientation = WorldRendererSurfaces_Apply(Renderer, &Entry, &Copy);
        TEST_CHECK(run, (Maps[MATERIAL_MAP_EMISSION].texture.id == PICTURE_TEXTURE_ID)
            && (Maps[MATERIAL_MAP_EMISSION].value == 0.25f) && (Maps[MATERIAL_MAP_EMISSION].color.r == 255)
            && (Maps[MATERIAL_MAP_EMISSION].color.b == 255));
        TEST_CHECK(run, Orientation.IsEmissiveBottomUp && !Orientation.IsAlbedoBottomUp
            && (Maps[MATERIAL_MAP_ALBEDO].texture.id == 56U));
        // Off: no glow at all, the material's own emission gone too.
        Entry.IsSurfacePowered = false;
        Maps[MATERIAL_MAP_EMISSION].texture.id = 55U;
        Orientation = WorldRendererSurfaces_Apply(Renderer, &Entry, &Copy);
        TEST_CHECK(run, (Maps[MATERIAL_MAP_EMISSION].texture.id == 0U) && (Maps[MATERIAL_MAP_EMISSION].value == 0.0f)
            && (Maps[MATERIAL_MAP_EMISSION].color.r == 0) && !Orientation.IsEmissiveBottomUp);
        // Another object's picture is not this one's.
        Entry.IsSurfacePowered = true;
        Entry.ObjectId = 9U;
        Maps[MATERIAL_MAP_EMISSION].texture.id = 55U;
        Orientation = WorldRendererSurfaces_Apply(Renderer, &Entry, &Copy);
        TEST_CHECK(run, (Maps[MATERIAL_MAP_EMISSION].texture.id == 0U) && !Orientation.IsEmissiveBottomUp);
        // Object 8's, as an image (not bottom up).
        Entry.ObjectId = 8U;
        Orientation = WorldRendererSurfaces_Apply(Renderer, &Entry, &Copy);
        TEST_CHECK(run, (Maps[MATERIAL_MAP_EMISSION].texture.id == OTHER_TEXTURE_ID) && !Orientation.IsEmissiveBottomUp);
        // After the render: forgotten, the screen shows nothing.
        WorldRendererSurfaces_ForgetTextures(Renderer);
        Entry.ObjectId = 7U;
        Maps[MATERIAL_MAP_EMISSION].texture.id = 55U;
        Orientation = WorldRendererSurfaces_Apply(Renderer, &Entry, &Copy);
        TEST_CHECK(run, (Maps[MATERIAL_MAP_EMISSION].texture.id == 0U) && (Renderer->_surfaces._bindings._count == 0U));
        // A picture for a longer name is not this surface's.
        TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, 7U, (const unsigned char*)u8"screens", Picture,
            true));
        Maps[MATERIAL_MAP_EMISSION].texture.id = 55U;
        Orientation = WorldRendererSurfaces_Apply(Renderer, &Entry, &Copy);
        TEST_CHECK(run, (Maps[MATERIAL_MAP_EMISSION].texture.id == 0U) && !Orientation.IsEmissiveBottomUp);
        WorldRendererSurfaces_ForgetTextures(Renderer);
        // A render that cannot run (no world) forgets them too.
        TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, 7U, (const unsigned char*)u8"screen", Picture,
            true));
        TEST_CHECK_ERROR(run, WorldRenderer_RenderToTarget(Renderer, NULL, NULL, 0.0, (RenderTexture2D){ 0 }),
            ErrorCode_IllegalArgument);
        TEST_CHECK(run, Renderer->_surfaces._bindings._count == 0U);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);

    // A paper: its picture is the albedo map, lit; without one its material stays as it is.
    if (TestAssetFixture_Construct(run, &Fixture) && AddScreenModel(run, &Fixture, "page", MatrixIdentity(),
        ScreenTexcoords) && TestAssets_AddWorldModel(run, &Fixture, "{\"name\": \"page\", \"lods\": [{\"model\": "
        "\"page\", \"max_distance\": 40}], \"surfaces\": [{\"name\": \"page\", \"material_slot\": 1, \"resolution\": "
        "[64, 64], \"kind\": \"paper\"}]}"))
    {
        const WorldModel* Loaded = LoadWorldModel(run, &Fixture, "page", &User);
        WorldRenderModel Entry;
        Memory_Zero(&Entry, sizeof(Entry));
        Entry.ObjectId = 3U;
        Entry.Surface = (Loaded != NULL) ? WorldModel_GetSurface(Loaded, 0U) : NULL;
        Entry.IsSurfacePowered = true;
        MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
        Memory_Zero(Maps, sizeof(Maps));
        Maps[MATERIAL_MAP_ALBEDO].texture.id = 56U;
        Maps[MATERIAL_MAP_EMISSION].value = 0.5f;
        Material Copy = { .maps = Maps };
        if (Entry.Surface != NULL)
        {
            WorldRendererMapOrientation Orientation = WorldRendererSurfaces_Apply(Renderer, &Entry, &Copy);
            TEST_CHECK(run, (Maps[MATERIAL_MAP_ALBEDO].texture.id == 56U) && !Orientation.IsAlbedoBottomUp
                && (Maps[MATERIAL_MAP_EMISSION].value == 0.5f));
            Texture2D Picture = { .id = PICTURE_TEXTURE_ID, .width = 64, .height = 64, .mipmaps = 1, .format = 7 };
            TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, 3U, (const unsigned char*)u8"page",
                Picture, true));
            Orientation = WorldRendererSurfaces_Apply(Renderer, &Entry, &Copy);
            TEST_CHECK(run, (Maps[MATERIAL_MAP_ALBEDO].texture.id == PICTURE_TEXTURE_ID) && Orientation.IsAlbedoBottomUp
                && !Orientation.IsEmissiveBottomUp && (Maps[MATERIAL_MAP_EMISSION].value == 0.5f));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    WorldRenderCache_Deconstruct(&Renderer->_cache);
    Memory_Free(Renderer->_surfaces._bindings._data);
    Memory_Free(Renderer);
}

/* The per-draw material copy takes the picture on the surface's slot only (the bezel's draw keeps its own maps, the
 * model's materials are never written), and the LOD tinting's glow comes after it (on a LOD past the first the glow
 * shows). The LOD's model is built here, two slots with maps of their own (the test assets' models have none). */
static void ThePictureReachesOnlyItsSlotsDrawsBeforeTheLodGlow(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    const WorldModelSurface* Surface = LoadScreenBox(run, &Fixture, &User);
    WorldRenderer* Renderer = Memory_Allocate(sizeof(WorldRenderer));
    Memory_Zero(Renderer, sizeof(*Renderer));
    GenericBuffer_AllocateVariable(&Renderer->_surfaces._bindings, 1U, sizeof(WorldRendererSurfaceBinding));
    if ((Surface != NULL) && TEST_CHECK(run, WorldModelSurface_GetMaterialSlot(Surface) == 1))
    {
        Texture2D Picture = { .id = PICTURE_TEXTURE_ID, .width = 320, .height = 240, .mipmaps = 1, .format = 7 };
        TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, 7U, (const unsigned char*)u8"screen", Picture,
            true));
        WorldRenderModel Entry;
        Memory_Zero(&Entry, sizeof(Entry));
        Entry.ObjectId = 7U;
        Entry.Surface = Surface;
        Entry.IsSurfacePowered = true;
        Entry.SurfaceEmission = 0.25f;
        Entry.Tint = WHITE;
        MaterialMap ModelMaps[2][WORLD_RENDERER_MATERIAL_MAP_COUNT];
        Memory_Zero(ModelMaps, sizeof(ModelMaps));
        ModelMaps[0][MATERIAL_MAP_EMISSION].texture.id = 55U;
        ModelMaps[1][MATERIAL_MAP_EMISSION].texture.id = 56U;
        Material ModelMaterials[2] = { { .maps = ModelMaps[0] }, { .maps = ModelMaps[1] } };
        int MeshMaterials[2] = { 0, 1 };
        Model Lod = { .meshCount = 2, .materialCount = 2, .materials = ModelMaterials, .meshMaterial = MeshMaterials };
        for (int MeshIndex = 0; MeshIndex < 2; MeshIndex++)
        {
            bool IsScreen = (MeshIndex == 1);
            MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
            WorldRendererGeometry_PrepareMaterial(Renderer, &Entry, Lod, MeshIndex, 0U, NULL, NULL, Maps);
            TEST_CHECK(run, Maps[MATERIAL_MAP_EMISSION].texture.id == (IsScreen ? PICTURE_TEXTURE_ID : 55U));
            TEST_CHECK(run, !IsScreen || (Maps[MATERIAL_MAP_EMISSION].value == 0.25f));
            Renderer->_isLodTintingEnabled = true;
            WorldRendererGeometry_PrepareMaterial(Renderer, &Entry, Lod, MeshIndex, 1U, NULL, NULL, Maps);
            TEST_CHECK(run, Maps[MATERIAL_MAP_EMISSION].texture.id == 0U);
            Renderer->_isLodTintingEnabled = false;
        }
        TEST_CHECK(run, (ModelMaps[0][MATERIAL_MAP_EMISSION].texture.id == 55U)
            && (ModelMaps[1][MATERIAL_MAP_EMISSION].texture.id == 56U));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    Memory_Free(Renderer->_surfaces._bindings._data);
    Memory_Free(Renderer);
}

/* The renderer's status counts the drawn screens and those showing a picture handed over for them. */
static void TheStatusCountsTheDrawnScreensAndTheirPictures(TestRun* run)
{
    ScreensFixture Fixture;
    WorldRenderer* Renderer = Memory_Allocate(sizeof(WorldRenderer));
    Memory_Zero(Renderer, sizeof(*Renderer));
    GenericBuffer_AllocateVariable(&Renderer->_surfaces._bindings, 1U, sizeof(WorldRendererSurfaceBinding));
    WorldRenderCache_Construct(&Renderer->_cache);
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (ScreensFixture_Construct(run, &Fixture, NULL)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Assets.Manager, &User)))
    {
        uint64_t Shown = AddObject(run, &Fixture, "screen_box", (Vector3){ 0.0f, 0.0f, 0.0f }, true);
        uint64_t Hidden = AddObject(run, &Fixture, "screen_box", (Vector3){ 3.0f, 0.0f, 0.0f }, true);
        TEST_CHECK(run, AddObject(run, &Fixture, "screen_box", (Vector3){ 6.0f, 0.0f, 0.0f }, false) != 0U);
        WorldObject* Object = NULL;
        TEST_CHECK(run, World_TryGetObject(&Fixture.World, Hidden, &Object));
        TEST_CHECK_SUCCESS(run, WorldObject_SetVisible(Object, false));
        World_UpdateTransforms(&Fixture.World);
        Texture2D Picture = { .id = PICTURE_TEXTURE_ID, .width = 320, .height = 240, .mipmaps = 1, .format = 7 };
        uint32_t Screens = 9U;
        uint32_t Pictures = 9U;
        if (TEST_CHECK_SUCCESS(run, WorldRenderCache_Sync(&Renderer->_cache, &Fixture.World, Vector3Double_Zero(),
            Fixture.Assets.Manager, User, NULL)))
        {
            WorldRendererSurfaces_Count(Renderer, &Screens, &Pictures);
            TEST_CHECK(run, (Screens == 1U) && (Pictures == 0U));
            TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, Hidden, (const unsigned char*)u8"screen",
                Picture, true));
            WorldRendererSurfaces_Count(Renderer, &Screens, &Pictures);
            TEST_CHECK(run, (Screens == 1U) && (Pictures == 0U));
            TEST_CHECK_SUCCESS(run, WorldRenderer_SetSurfaceTexture(Renderer, Shown, (const unsigned char*)u8"screen",
                Picture, true));
            WorldRendererSurfaces_Count(Renderer, &Screens, &Pictures);
            TEST_CHECK(run, (Screens == 1U) && (Pictures == 1U));
        }
    }
    ScreensFixture_Deconstruct(run, &Fixture);
    WorldRenderCache_Deconstruct(&Renderer->_cache);
    Memory_Free(Renderer->_surfaces._bindings._data);
    Memory_Free(Renderer);
}


// Public functions.
void WorldScreenTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "surfaces_are_read_and_built_from_lod_zeros_slot", SurfacesAreReadAndBuiltFromLodZerosSlot },
        { "surface_rules_are_refused_with_clear_messages", SurfaceRulesAreRefusedWithClearMessages },
        { "surfaces_need_their_slots_meshes_with_texture_coordinates",
            SurfacesNeedTheirSlotsMeshesWithTextureCoordinates },
        { "the_hit_gives_the_surface_position", TheHitGivesTheSurfacePosition },
        { "rays_that_miss_the_slots_triangles_meet_nothing", RaysThatMissTheSlotsTrianglesMeetNothing },
        { "a_rotated_and_scaled_placement_is_met_where_its_triangles_lie",
            ARotatedAndScaledPlacementIsMetWhereItsTrianglesLie },
        { "a_curved_surface_is_met_on_its_facets", ACurvedSurfaceIsMetOnItsFacets },
        { "a_border_beyond_the_picture_is_off_it", ABorderBeyondThePictureIsOffIt },
        { "a_slot_over_several_meshes_keeps_each_meshs_coordinates", ASlotOverSeveralMeshesKeepsEachMeshsCoordinates },
        { "the_screen_component_starts_at_its_defaults_and_checks_its_values",
            TheScreenComponentStartsAtItsDefaultsAndChecksItsValues },
        { "the_screen_component_is_saved_with_the_world", TheScreenComponentIsSavedWithTheWorld },
        { "the_pointer_is_where_the_player_points_on_the_picture", ThePointerIsWhereThePlayerPointsOnThePicture },
        { "use_or_a_click_on_the_picture_focuses_the_screen", UseOrAClickOnThePictureFocusesTheScreen },
        { "a_focused_screen_takes_the_whole_keyboard_after_its_ui", AFocusedScreenTakesTheWholeKeyboardAfterItsUi },
        { "escape_and_pointing_away_let_go", EscapeAndPointingAwayLetGo },
        { "the_render_cache_takes_the_screens_surface", TheRenderCacheTakesTheScreensSurface },
        { "the_picture_goes_into_the_slots_material_copy_for_one_render",
            ThePictureGoesIntoTheSlotsMaterialCopyForOneRender },
        { "the_picture_reaches_only_its_slots_draws_before_the_lod_glow",
            ThePictureReachesOnlyItsSlotsDrawsBeforeTheLodGlow },
        { "the_status_counts_the_drawn_screens_and_their_pictures", TheStatusCountsTheDrawnScreensAndTheirPictures },
    };
    TestRun_RunSuite(run, "WorldScreen", Cases, sizeof(Cases) / sizeof(Cases[0]));
    static const TestCase WorldCases[] =
    {
        { "screens_resolve_the_surfaces_they_name", ScreensResolveTheSurfacesTheyName },
        { "the_ray_points_at_the_nearest_screen_on_its_picture", TheRayPointsAtTheNearestScreenOnItsPicture },
        { "other_objects_colliders_hide_a_screen_its_own_do_not", OtherObjectsCollidersHideAScreenItsOwnDoNot },
        { "screens_follow_their_components", ScreensFollowTheirComponents },
        { "a_screen_removed_between_syncs_is_not_pointed_at", AScreenRemovedBetweenSyncsIsNotPointedAt },
    };
    TestWorld_RunSuite(run, "WorldScreens", "WorldScreensFar", WorldCases, sizeof(WorldCases) / sizeof(WorldCases[0]));
}
