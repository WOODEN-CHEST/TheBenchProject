#include "TestSuites.h"
#include "TestAssets.h"
#include "TestTerrain.h"
#include "TestText.h"
#include "GameTexture.h"
#include "TerrainMaterial.h"
#include "wr/WRFileSystem.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Room for a path. */
#define MATERIAL_TEST_PATH_CAPACITY 256
/** 40 degrees in radians, as the default triplanar slope is stored. */
#define FORTY_DEGREES 0.6981317f


// Types.
/* An asset manager over a work directory (a search root) with the standard types, its definitions read. */
typedef struct MaterialPackStruct
{
    GenericBuffer Root;
    AssetManager* Manager;
    StandardAssetTypes Types;
    JSONObjectPool* Pool;
    AssetUserID User;
} MaterialPack;

/* A refused definition: its JSON, the error code and a fragment its message must hold. */
typedef struct RefusedMaterialStruct
{
    const char* Json;
    ErrorCode Code;
    const char* Fragment;
} RefusedMaterial;


// Static functions: helpers.
static bool CreatePack(TestRun* run, MaterialPack* pack, const char* name)
{
    Memory_Zero(pack, sizeof(*pack));
    GenericBuffer_AllocateVariable(&pack->Root, MATERIAL_TEST_PATH_CAPACITY, sizeof(unsigned char));
    return TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, name, &pack->Root));
}

static bool OpenPack(TestRun* run, MaterialPack* pack)
{
    size_t FailedCount = 1U;
    return TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &pack->Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(pack->Manager, &pack->Types, &pack->Pool))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(pack->Manager, pack->Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(pack->Manager, &FailedCount))
        && TEST_CHECK(run, FailedCount == 0U)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(pack->Manager, &pack->User));
}

static void ClosePack(TestRun* run, MaterialPack* pack)
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

static bool AreNamesEqual(const unsigned char* a, const char* b)
{
    return (a != NULL) && (strcmp((const char*)a, b) == 0);
}

static bool IsNear(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

/* Builds a terrain material definition from @p json without registering it and returns the constructor's result. */
static Error BuildDefinition(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.TerrainMaterial, &Bytes,
        (const unsigned char*)u8"test", &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}

/* Writes one image of @p width x @p height into the pack's textures/ with its texture definition ("<name>"), each texel
 * @p colour, or a greyscale (one channel) image of @p grey when @p isGrey. */
static bool WriteImage(TestRun* run, const MaterialPack* pack, const char* name, int32_t width, int32_t height,
    Color colour, bool isGrey, unsigned char grey, const char* colourSpace)
{
    char Relative[MATERIAL_TEST_PATH_CAPACITY];
    char Definition[MATERIAL_TEST_PATH_CAPACITY];
    snprintf(Relative, sizeof(Relative), "textures/%s.json", name);
    snprintf(Definition, sizeof(Definition), "{\"name\": \"%s\", \"location\": \"%s\"%s%s%s}", name, name,
        (colourSpace != NULL) ? ", \"color_space\": \"" : "", (colourSpace != NULL) ? colourSpace : "",
        (colourSpace != NULL) ? "\"" : "");
    if (!TEST_CHECK_SUCCESS(run, Test_WriteTextFile(pack->Root._data, Relative, Definition)))
    {
        return false;
    }
    Image Picture = GenImageColor(width, height, colour);
    if (isGrey)
    {
        ImageFormat(&Picture, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
        unsigned char* Bytes = Picture.data;
        for (size_t Index = 0U; Index < ((size_t)width * (size_t)height); Index++)
        {
            Bytes[Index] = grey;
        }
    }
    char Path[MATERIAL_TEST_PATH_CAPACITY];
    snprintf(Path, sizeof(Path), "%s/textures/%s.png", (const char*)pack->Root._data, name);
    bool IsWritten = ExportImage(Picture, Path);
    UnloadImage(Picture);
    return TEST_CHECK(run, IsWritten);
}

static void PutU32(uint8_t* at, uint32_t value)
{
    for (uint32_t Byte = 0U; Byte < 4U; Byte++)
    {
        at[Byte] = (uint8_t)(value >> (8U * Byte));
    }
}

/* Writes a well-formed 4 x 4 uncompressed RGBA DDS image (its header and all 64 bytes of pixels) with @p extension and
 * its texture definition: one raylib decodes. */
static bool WriteDdsImage(TestRun* run, const MaterialPack* pack, const char* name, const char* extension)
{
    char Relative[MATERIAL_TEST_PATH_CAPACITY];
    char Definition[MATERIAL_TEST_PATH_CAPACITY];
    snprintf(Relative, sizeof(Relative), "textures/%s.json", name);
    snprintf(Definition, sizeof(Definition), "{\"name\": \"%s\", \"location\": \"%s\"}", name, name);
    if (!TEST_CHECK_SUCCESS(run, Test_WriteTextFile(pack->Root._data, Relative, Definition)))
    {
        return false;
    }
    uint8_t File[128 + 64];
    memset(File, 0, sizeof(File));
    memcpy(File, "DDS ", 4U);
    PutU32(&File[4], 124U);
    PutU32(&File[8], 0x100FU);
    PutU32(&File[12], 4U);
    PutU32(&File[16], 4U);
    PutU32(&File[20], 16U);
    PutU32(&File[76], 32U);
    PutU32(&File[80], 0x41U);
    PutU32(&File[88], 32U);
    PutU32(&File[92], 0x00FF0000U);
    PutU32(&File[96], 0x0000FF00U);
    PutU32(&File[100], 0x000000FFU);
    PutU32(&File[104], 0xFF000000U);
    PutU32(&File[108], 0x1000U);
    memset(&File[128], 200, 64U);
    char Path[MATERIAL_TEST_PATH_CAPACITY];
    snprintf(Path, sizeof(Path), "%s/textures/%s.%s", (const char*)pack->Root._data, name, extension);
    return TEST_CHECK(run, SaveFileData(Path, File, (int)sizeof(File)));
}

static TerrainMaterial* LoadMaterial(TestRun* run, MaterialPack* pack, const char* name)
{
    TerrainMaterial* Loaded = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_LoadTerrainMaterial(pack->Manager, (const unsigned char*)name,
        pack->User, &Loaded)))
    {
        return NULL;
    }
    return Loaded;
}


// Tests: definitions.
/* A definition with only its albedo gets the documented defaults; the material has its albedo with a flat height in
 * its alpha, and neither a normal nor a roughness map. */
static void AMinimalDefinitionGetsTheDefaults(TestRun* run)
{
    MaterialPack Pack;
    TestTerrainMaterialFiles Files = { .Name = "moss", .AlbedoSize = 4, .Albedo = { 60, 90, 30, 255 } };
    TerrainMaterial* Loaded = (CreatePack(run, &Pack, "material_minimal")
        && TEST_CHECK_SUCCESS(run, TestTerrain_WriteMaterialFiles(Pack.Root._data, &Files)) && OpenPack(run, &Pack))
        ? LoadMaterial(run, &Pack, "moss") : NULL;
    if (Loaded != NULL)
    {
        const TerrainMaterialSettings* Settings = TerrainMaterial_GetSettings(Loaded);
        TEST_CHECK(run, AreNamesEqual(Settings->Albedo, "moss_albedo") && (Settings->Height == NULL)
            && (Settings->Normal == NULL) && (Settings->RoughnessAo == NULL));
        TEST_CHECK(run, (Settings->TileSize == 2.0f) && (Settings->AlbedoTint.r == 255U) && (Settings->AlbedoTint.g == 255U)
            && (Settings->AlbedoTint.b == 255U) && (Settings->Roughness == 0.9f) && (Settings->NormalStrength == 1.0f)
            && (Settings->HeightBlendStrength == 1.0f) && (Settings->Triplanar == TerrainTriplanarMode_Steep)
            && IsNear(Settings->TriplanarSlope, FORTY_DEGREES, 1e-6f) && (Settings->Puddles == 0.0f)
            && (Settings->Porosity == 0.5f));
        TEST_CHECK(run, AreNamesEqual(Settings->Surface, "tbp:ground"));
        int32_t Width = 0;
        int32_t Height = 0;
        const uint8_t* Albedo = TerrainMaterial_GetMap(Loaded, TerrainMaterialMapKind_AlbedoHeight, &Width, &Height);
        TEST_CHECK(run, (Albedo != NULL) && (Width == 4) && (Height == 4));
        TEST_CHECK(run, (Albedo != NULL) && (Albedo[0] == 60U) && (Albedo[1] == 90U) && (Albedo[2] == 30U)
            && (Albedo[3] == TERRAIN_MATERIAL_FLAT_HEIGHT) && (Albedo[(15U * 4U) + 3U] == TERRAIN_MATERIAL_FLAT_HEIGHT));
        TEST_CHECK(run, !TerrainMaterial_HasMap(Loaded, TerrainMaterialMapKind_Normal)
            && !TerrainMaterial_HasMap(Loaded, TerrainMaterialMapKind_RoughnessAo)
            && (TerrainMaterial_GetMap(Loaded, TerrainMaterialMapKind_Normal, &Width, &Height) == NULL) && (Width == 0)
            && (Height == 0));
        // The mean colour is the albedo's in linear light (times a white tint).
        Vector3 Mean = TerrainMaterial_GetMeanColor(Loaded);
        TEST_CHECK(run, IsNear(Mean.x, powf(60.0f / 255.0f, 2.2f), 1e-5f)
            && IsNear(Mean.y, powf(90.0f / 255.0f, 2.2f), 1e-5f) && IsNear(Mean.z, powf(30.0f / 255.0f, 2.2f), 1e-5f));
        // Without a height map the material is flat at mid-height.
        TEST_CHECK(run, IsNear(TerrainMaterial_SampleHeight(Loaded, 0.3f, 0.7f), 128.0f / 255.0f, 1e-6f));
    }
    ClosePack(run, &Pack);
}

/* Every key at once: each map read, every setting, and the surface key put into its full form. */
static void EveryKeyIsRead(TestRun* run)
{
    MaterialPack Pack;
    TestTerrainMaterialFiles Files =
    {
        .Name = "cobbles", .AlbedoSize = 8, .Albedo = { 10, 20, 30, 255 }, .HeightSize = 8, .NormalSize = 4,
        .RoughnessAoSize = 2,
        .Settings = "\"tile_size\": 3.5, \"albedo_tint\": \"#808040\", \"roughness\": 0.6, \"normal_strength\": 2.5, "
            "\"height_blend_strength\": 4, \"triplanar\": \"always\", \"triplanar_slope\": 55, \"surface\": \"stone\", "
            "\"puddles\": 1, \"porosity\": 0.25"
    };
    TestTerrainMaterialFiles Town =
    {
        .Name = "mud", .AlbedoSize = 2, .Albedo = { 90, 70, 50, 255 }, .Settings = "\"surface\": \"town:mud\", "
            "\"triplanar\": \"off\""
    };
    TerrainMaterial* Loaded = NULL;
    TerrainMaterial* Mud = NULL;
    if (CreatePack(run, &Pack, "material_full")
        && TEST_CHECK_SUCCESS(run, TestTerrain_WriteMaterialFiles(Pack.Root._data, &Files))
        && TEST_CHECK_SUCCESS(run, TestTerrain_WriteMaterialFiles(Pack.Root._data, &Town)) && OpenPack(run, &Pack))
    {
        Loaded = LoadMaterial(run, &Pack, "cobbles");
        Mud = LoadMaterial(run, &Pack, "mud");
    }
    if (Loaded != NULL)
    {
        const TerrainMaterialSettings* Settings = TerrainMaterial_GetSettings(Loaded);
        TEST_CHECK(run, AreNamesEqual(Settings->Albedo, "cobbles_albedo") && AreNamesEqual(Settings->Height,
            "cobbles_height") && AreNamesEqual(Settings->Normal, "cobbles_normal")
            && AreNamesEqual(Settings->RoughnessAo, "cobbles_roughness_ao"));
        TEST_CHECK(run, (Settings->TileSize == 3.5f) && (Settings->AlbedoTint.r == 128U) && (Settings->AlbedoTint.g == 128U)
            && (Settings->AlbedoTint.b == 64U) && (Settings->Roughness == 0.6f) && (Settings->NormalStrength == 2.5f)
            && (Settings->HeightBlendStrength == 4.0f) && (Settings->Triplanar == TerrainTriplanarMode_Always)
            && IsNear(Settings->TriplanarSlope, 55.0f * 0.017453292f, 1e-6f) && (Settings->Puddles == 1.0f)
            && (Settings->Porosity == 0.25f));
        TEST_CHECK(run, AreNamesEqual(Settings->Surface, "tbp:stone"));
        // The mean colour carries the tint (linear times linear).
        Vector3 Mean = TerrainMaterial_GetMeanColor(Loaded);
        TEST_CHECK(run, IsNear(Mean.x, powf(10.0f / 255.0f, 2.2f) * powf(128.0f / 255.0f, 2.2f), 1e-6f)
            && IsNear(Mean.z, powf(30.0f / 255.0f, 2.2f) * powf(64.0f / 255.0f, 2.2f), 1e-6f));
    }
    if (Mud != NULL)
    {
        TEST_CHECK(run, AreNamesEqual(TerrainMaterial_GetSettings(Mud)->Surface, "town:mud")
            && (TerrainMaterial_GetSettings(Mud)->Triplanar == TerrainTriplanarMode_Off));
    }
    ClosePack(run, &Pack);
}

/* Every rule of the definition refuses what it should, naming the material and the rule. */
static void EachRuleIsRefusedWithAClearMessage(TestRun* run)
{
    static const RefusedMaterial Cases[] =
    {
        { "{\"name\": \"m\"}", ErrorCode_InvalidAssetDefinition, "Terrain material \"m\": \"albedo\" must name the "
            "texture of its colour." },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"colour\": \"#ff0000\"}", ErrorCode_InvalidAssetDefinition,
            "\"colour\"" },
        { "{\"albedo\": \"a\"}", ErrorCode_InvalidAssetDefinition, "name" },
        { "{\"name\": \"m\", \"albedo\": \"Grass\"}", ErrorCode_Success, "albedo" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"tile_size\": 0}", ErrorCode_Success, "tile_size" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"tile_size\": -2}", ErrorCode_Success, "tile_size" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"tile_size\": 1000.5}", ErrorCode_Success, "tile_size" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"roughness\": 1.5}", ErrorCode_Success, "roughness" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"normal_strength\": 9}", ErrorCode_Success, "normal_strength" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"height_blend_strength\": -1}", ErrorCode_Success,
            "height_blend_strength" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"triplanar\": \"sometimes\"}", ErrorCode_Success, "triplanar" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"triplanar_slope\": 91}", ErrorCode_Success, "triplanar_slope" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"triplanar_slope\": -1}", ErrorCode_Success, "triplanar_slope" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"surface\": \"Bad Key\"}", ErrorCode_Success, "Terrain material \"m\"" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"surface\": \"\"}", ErrorCode_Success, "Terrain material \"m\"" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"puddles\": 2}", ErrorCode_Success, "puddles" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"porosity\": -0.1}", ErrorCode_Success, "porosity" },
        { "{\"name\": \"m\", \"albedo\": \"a\", \"height\": 3}", ErrorCode_Success, "height" },
    };
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        Error Result = BuildDefinition(&Fixture, Cases[Index].Json);
        // ErrorCode_Success above means "refused, whatever its code": the table's rules give their own codes.
        if (Cases[Index].Code == ErrorCode_Success)
        {
            bool IsRefused = Result.Code != ErrorCode_Success;
            bool IsNamed = IsRefused && (Result.Message != NULL)
                && (strstr((const char*)Result.Message, Cases[Index].Fragment) != NULL);
            if (!TEST_CHECK(run, IsRefused && IsNamed))
            {
                printf("        case %zu: %s -> %s\n", Index, Cases[Index].Json,
                    (Result.Message != NULL) ? (const char*)Result.Message : "(accepted)");
            }
            Error_Deconstruct(&Result);
            continue;
        }
        const char* const Fragments[] = { Cases[Index].Fragment, NULL };
        if (!TestText_CheckErrorMessage(run, Result, Cases[Index].Code, Fragments))
        {
            printf("        case %zu: %s\n", Index, Cases[Index].Json);
        }
    }
    // At the limits: a tile size of 1000, a slope of 90 degrees, both 0..1 values at their ends.
    TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, "{\"name\": \"m\", \"albedo\": \"a\", \"tile_size\": 1000, "
        "\"triplanar_slope\": 90, \"puddles\": 1, \"porosity\": 0, \"roughness\": 0, \"normal_strength\": 8, "
        "\"height_blend_strength\": 10}"));
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: loading.
/* The maps are packed as the texture arrays take them: the albedo with the height (the height map's red) in its alpha,
 * the normal map's red and green, the roughness and AO map's red and green; each at its own size. */
static void TheMapsArePackedAsTheArraysTakeThem(TestRun* run)
{
    MaterialPack Pack;
    TestTerrainMaterialFiles Files =
    {
        .Name = "packed", .AlbedoSize = 8, .Albedo = { 10, 20, 30, 255 }, .HeightSize = 8, .NormalSize = 4,
        .RoughnessAoSize = 2
    };
    TerrainMaterial* Loaded = (CreatePack(run, &Pack, "material_packed")
        && TEST_CHECK_SUCCESS(run, TestTerrain_WriteMaterialFiles(Pack.Root._data, &Files)) && OpenPack(run, &Pack))
        ? LoadMaterial(run, &Pack, "packed") : NULL;
    if (Loaded != NULL)
    {
        TEST_CHECK(run, (TerrainMaterial_GetMapTexelSize(TerrainMaterialMapKind_AlbedoHeight) == 4U)
            && (TerrainMaterial_GetMapTexelSize(TerrainMaterialMapKind_Normal) == 2U)
            && (TerrainMaterial_GetMapTexelSize(TerrainMaterialMapKind_RoughnessAo) == 2U));
        int32_t Width = 0;
        int32_t Height = 0;
        const uint8_t* Albedo = TerrainMaterial_GetMap(Loaded, TerrainMaterialMapKind_AlbedoHeight, &Width, &Height);
        TEST_CHECK(run, (Albedo != NULL) && (Width == 8) && (Height == 8));
        bool IsRamp = Albedo != NULL;
        for (int32_t Y = 0; IsRamp && (Y < 8); Y++)
        {
            for (int32_t X = 0; X < 8; X++)
            {
                const uint8_t* Texel = &Albedo[((Y * 8) + X) * 4];
                uint8_t Expected = (uint8_t)lroundf(255.0f * (float)X / 7.0f);
                IsRamp = IsRamp && (Texel[0] == 10U) && (Texel[1] == 20U) && (Texel[2] == 30U) && (Texel[3] == Expected);
            }
        }
        TEST_CHECK(run, IsRamp);
        const uint8_t* Normal = TerrainMaterial_GetMap(Loaded, TerrainMaterialMapKind_Normal, &Width, &Height);
        TEST_CHECK(run, (Normal != NULL) && (Width == 4) && (Height == 4) && (Normal[0] == 150U) && (Normal[1] == 110U)
            && (Normal[30] == 150U) && (Normal[31] == 110U));
        const uint8_t* Surface = TerrainMaterial_GetMap(Loaded, TerrainMaterialMapKind_RoughnessAo, &Width, &Height);
        TEST_CHECK(run, (Surface != NULL) && (Width == 2) && (Height == 2) && (Surface[0] == 200U) && (Surface[1] == 100U)
            && (Surface[6] == 200U) && (Surface[7] == 100U));
        TEST_CHECK(run, TerrainMaterial_HasMap(Loaded, TerrainMaterialMapKind_Normal)
            && TerrainMaterial_HasMap(Loaded, TerrainMaterialMapKind_RoughnessAo)
            && !TerrainMaterial_HasMap(Loaded, TerrainMaterialMapKind_Count));
    }
    ClosePack(run, &Pack);
}

/* The height goes into the albedo's map, so it must be the albedo's size: another size is refused naming both; a map
 * past the size limit is refused naming its size; a texture that is not defined, or not read from a file, fails the
 * load naming the slot and the texture. */
static void BadMapsFailTheLoadNamingWhatAndWhere(TestRun* run)
{
    MaterialPack Pack;
    TestTerrainMaterialFiles Mismatched =
    {
        .Name = "mismatched", .AlbedoSize = 4, .Albedo = { 1, 2, 3, 255 }, .HeightSize = 8
    };
    TestTerrainMaterialFiles Huge = { .Name = "huge", .AlbedoSize = TERRAIN_MATERIAL_MAX_SIDE + 1, .Albedo = WHITE };
    TestTerrainMaterialFiles Largest = { .Name = "largest", .AlbedoSize = TERRAIN_MATERIAL_MAX_SIDE, .Albedo = WHITE };
    bool IsReady = CreatePack(run, &Pack, "material_bad")
        && TEST_CHECK_SUCCESS(run, TestTerrain_WriteMaterialFiles(Pack.Root._data, &Mismatched))
        && TEST_CHECK_SUCCESS(run, TestTerrain_WriteMaterialFiles(Pack.Root._data, &Huge))
        && TEST_CHECK_SUCCESS(run, TestTerrain_WriteMaterialFiles(Pack.Root._data, &Largest))
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Pack.Root._data, "terrain_materials/lost.json",
            "{\"name\": \"lost\", \"albedo\": \"nothing\"}"))
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Pack.Root._data, "terrain_materials/coded.json",
            "{\"name\": \"coded\", \"albedo\": \"made_in_code\"}"))
        && OpenPack(run, &Pack)
        && TestAssets_AddTexture(run, &(TestAssetFixture){ .Manager = Pack.Manager, .Types = Pack.Types,
            .Pool = Pack.Pool }, "made_in_code", 7U, NULL);
    if (IsReady)
    {
        TerrainMaterial* Loaded = NULL;
        const char* const MismatchFragments[] = { "Terrain material \"mismatched\": its height \"mismatched_height\" is "
            "8 x 8 pixels and its albedo 4 x 4; they must be the same size", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTerrainMaterial(Pack.Manager, (const unsigned char*)u8"mismatched",
            Pack.User, &Loaded), ErrorCode_InvalidAssetData, MismatchFragments);
        const char* const HugeFragments[] = { "Terrain material \"huge\": its albedo \"huge_albedo\" could not be read: "
            "Texture \"huge_albedo\"", "is 1025 x 1025 pixels; at most 1024 per side are allowed.", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTerrainMaterial(Pack.Manager, (const unsigned char*)u8"huge",
            Pack.User, &Loaded), ErrorCode_InvalidAssetData, HugeFragments);
        const char* const LostFragments[] = { "Terrain material \"lost\": its albedo \"nothing\" could not be read: No "
            "texture named \"nothing\" is defined.", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTerrainMaterial(Pack.Manager, (const unsigned char*)u8"lost",
            Pack.User, &Loaded), ErrorCode_InvalidOperation, LostFragments);
        const char* const CodedFragments[] = { "its albedo \"made_in_code\" could not be read: Texture \"made_in_code\" is "
            "not read from an image file", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTerrainMaterial(Pack.Manager, (const unsigned char*)u8"coded",
            Pack.User, &Loaded), ErrorCode_InvalidOperation, CodedFragments);
        TEST_CHECK(run, Loaded == NULL);
        // At the limit it loads.
        Loaded = LoadMaterial(run, &Pack, "largest");
        int32_t Width = 0;
        TEST_CHECK(run, (Loaded != NULL) && (TerrainMaterial_GetMap(Loaded, TerrainMaterialMapKind_AlbedoHeight, &Width,
            NULL) != NULL) && (Width == TERRAIN_MATERIAL_MAX_SIDE));
    }
    ClosePack(run, &Pack);
}

/* A texture's pixels are read from its image file as 8-bit RGBA whatever the file holds (a greyscale image's grey in red,
 * green and blue, alpha 255 without one), with the definition's colour space, up to the caller's size; nothing is loaded
 * or held, and a DDS file is refused before it is decoded. */
static void TexturePixelsAreReadAsRgba(TestRun* run)
{
    MaterialPack Pack;
    bool IsReady = CreatePack(run, &Pack, "texture_pixels")
        && WriteImage(run, &Pack, "grey_one", 3, 2, BLACK, true, 77U, "linear")
        && WriteImage(run, &Pack, "colour_one", 2, 5, (Color){ 9, 8, 7, 200 }, false, 0U, NULL)
        && WriteDdsImage(run, &Pack, "dds_one", "dds") && WriteDdsImage(run, &Pack, "dds_loud", "DDS")
        && OpenPack(run, &Pack);
    if (IsReady)
    {
        TexturePixels Pixels;
        if (TEST_CHECK_SUCCESS(run, AssetManager_LoadTexturePixels(Pack.Manager, (const unsigned char*)u8"grey_one", 3,
            &Pixels)))
        {
            TEST_CHECK(run, (Pixels.Width == 3) && (Pixels.Height == 2) && !Pixels.IsSrgb && (Pixels.Pixels[0] == 77U)
                && (Pixels.Pixels[1] == 77U) && (Pixels.Pixels[2] == 77U) && (Pixels.Pixels[3] == 255U)
                && (Pixels.Pixels[23] == 255U));
            TexturePixels_Deconstruct(&Pixels);
            TEST_CHECK(run, (Pixels.Pixels == NULL) && (Pixels.Width == 0));
        }
        if (TEST_CHECK_SUCCESS(run, AssetManager_LoadTexturePixels(Pack.Manager, (const unsigned char*)u8"colour_one",
            TERRAIN_MATERIAL_MAX_SIDE, &Pixels)))
        {
            TEST_CHECK(run, (Pixels.Width == 2) && (Pixels.Height == 5) && Pixels.IsSrgb && (Pixels.Pixels[36] == 9U)
                && (Pixels.Pixels[37] == 8U) && (Pixels.Pixels[38] == 7U) && (Pixels.Pixels[39] == 200U));
            TexturePixels_Deconstruct(&Pixels);
        }
        // Reading pixels loads nothing: the texture asset is not held by anyone afterwards.
        TEST_CHECK_SUCCESS(run, AssetManager_ReleaseAllAssetsForUser(Pack.Manager, Pack.User));
        Error Released = AssetManager_ReleaseAsset(Pack.Manager, Pack.Types.Texture, (const unsigned char*)u8"grey_one",
            Pack.User);
        TEST_CHECK(run, Released.Code == ErrorCode_InvalidOperation);
        Error_Deconstruct(&Released);
        const char* const Fragments[] = { "No texture named \"absent\" is defined.", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTexturePixels(Pack.Manager, (const unsigned char*)u8"absent", 16,
            &Pixels), ErrorCode_InvalidOperation, Fragments);
        TEST_CHECK(run, (Pixels.Pixels == NULL) && (Pixels.Width == 0) && (Pixels.Height == 0));
        // Past the caller's size: refused, naming both (3 x 2 against 2); no size at all is not a limit.
        const char* const SizeFragments[] = { "Texture \"grey_one\"", "is 3 x 2 pixels; at most 2 per side are allowed.",
            NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTexturePixels(Pack.Manager, (const unsigned char*)u8"grey_one", 2,
            &Pixels), ErrorCode_InvalidAssetData, SizeFragments);
        TEST_CHECK(run, (Pixels.Pixels == NULL) && (Pixels.Width == 0));
        const char* const LimitFragments[] = { "maxSide is 0", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTexturePixels(Pack.Manager, (const unsigned char*)u8"grey_one", 0,
            &Pixels), ErrorCode_IllegalArgument, LimitFragments);
        // A DDS file is never decoded (raylib's reader trusts its header's sizes), even a well-formed one.
        const char* const DdsFragments[] = { "Texture \"dds_one\"", "is a DDS file, whose pixels are not read", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTexturePixels(Pack.Manager, (const unsigned char*)u8"dds_one", 16,
            &Pixels), ErrorCode_InvalidAssetData, DdsFragments);
        TEST_CHECK(run, (Pixels.Pixels == NULL) && (Pixels.Width == 0));
        // In any case of its extension (the file names resolve ignoring case, and raylib reads ".DDS" as DDS too).
        const char* const LoudFragments[] = { "Texture \"dds_loud\"", "is a DDS file", NULL };
        TestText_CheckErrorMessage(run, AssetManager_LoadTexturePixels(Pack.Manager, (const unsigned char*)u8"dds_loud", 16,
            &Pixels), ErrorCode_InvalidAssetData, LoudFragments);
    }
    ClosePack(run, &Pack);
}

/* The height is sampled as the GPU's base level: bilinear between texel centres, repeating at the edges, the same one
 * repeat away and for any coordinate. */
static void TheHeightIsSampledBilinearlyAndRepeats(TestRun* run)
{
    MaterialPack Pack;
    TestTerrainMaterialFiles Files = { .Name = "ramp", .AlbedoSize = 8, .Albedo = WHITE, .HeightSize = 8 };
    TerrainMaterial* Loaded = (CreatePack(run, &Pack, "material_sample")
        && TEST_CHECK_SUCCESS(run, TestTerrain_WriteMaterialFiles(Pack.Root._data, &Files)) && OpenPack(run, &Pack))
        ? LoadMaterial(run, &Pack, "ramp") : NULL;
    if (Loaded != NULL)
    {
        // Texel x holds round(255 x / 7) / 255; its centre is u = (x + 0.5) / 8.
        float Texel2 = (float)lroundf(255.0f * 2.0f / 7.0f) / 255.0f;
        float Texel3 = (float)lroundf(255.0f * 3.0f / 7.0f) / 255.0f;
        TEST_CHECK(run, IsNear(TerrainMaterial_SampleHeight(Loaded, 2.5f / 8.0f, 0.5f), Texel2, 1e-6f));
        TEST_CHECK(run, IsNear(TerrainMaterial_SampleHeight(Loaded, 3.0f / 8.0f, 0.1f), 0.5f * (Texel2 + Texel3), 1e-6f));
        // Between the last texel (1) and the first (0), across the edge: halfway at u = 0.
        TEST_CHECK(run, IsNear(TerrainMaterial_SampleHeight(Loaded, 0.0f, 0.3f), 0.5f, 1e-6f));
        TEST_CHECK(run, IsNear(TerrainMaterial_SampleHeight(Loaded, 1.0f, 0.3f), 0.5f, 1e-6f));
        // A repeat away, negative, and far out: the same.
        float Here = TerrainMaterial_SampleHeight(Loaded, 0.3125f, 0.25f);
        TEST_CHECK(run, IsNear(TerrainMaterial_SampleHeight(Loaded, 3.3125f, 0.25f), Here, 1e-5f)
            && IsNear(TerrainMaterial_SampleHeight(Loaded, -0.6875f, -3.75f), Here, 1e-5f)
            && IsNear(TerrainMaterial_SampleHeight(Loaded, 65536.3125f, 0.25f), Here, 1e-5f));
        // Rows are all the same ramp: v changes nothing; a non-finite coordinate is flat.
        TEST_CHECK(run, IsNear(TerrainMaterial_SampleHeight(Loaded, 0.3125f, 0.9f), Here, 1e-6f));
        TEST_CHECK(run, IsNear(TerrainMaterial_SampleHeight(Loaded, INFINITY, 0.0f), 128.0f / 255.0f, 1e-6f)
            && IsNear(TerrainMaterial_SampleHeight(Loaded, 0.5f, NAN), 128.0f / 255.0f, 1e-6f));
    }
    ClosePack(run, &Pack);
}

/* The settings' table passes its own check and reads back what it writes. */
static void TheSettingsTablePassesItsCheck(TestRun* run)
{
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(TerrainMaterialSettings_GetPropertyTable()));
    TerrainMaterialSettings Settings;
    if (TEST_CHECK_SUCCESS(run, TerrainMaterialSettings_SetDefaults(&Settings)))
    {
        TEST_CHECK(run, AreNamesEqual(Settings.Surface, "tbp:ground") && (Settings.Albedo == NULL));
        TerrainMaterialSettings_Deconstruct(&Settings);
        TEST_CHECK(run, Settings.Surface == NULL);
        TerrainMaterialSettings_Deconstruct(&Settings);
    }
}


// Public functions.
void TerrainMaterialTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_minimal_definition_gets_the_defaults", AMinimalDefinitionGetsTheDefaults },
        { "every_key_is_read", EveryKeyIsRead },
        { "each_rule_is_refused_with_a_clear_message", EachRuleIsRefusedWithAClearMessage },
        { "the_maps_are_packed_as_the_arrays_take_them", TheMapsArePackedAsTheArraysTakeThem },
        { "bad_maps_fail_the_load_naming_what_and_where", BadMapsFailTheLoadNamingWhatAndWhere },
        { "texture_pixels_are_read_as_rgba", TexturePixelsAreReadAsRgba },
        { "the_height_is_sampled_bilinearly_and_repeats", TheHeightIsSampledBilinearlyAndRepeats },
        { "the_settings_table_passes_its_check", TheSettingsTablePassesItsCheck },
    };
    TestRun_RunSuite(run, "TerrainMaterial", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
