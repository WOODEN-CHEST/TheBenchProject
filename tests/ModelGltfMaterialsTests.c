#include "TestSuites.h"
#include "ModelGltfMaterials.h"
#include "wr/WRFileSystem.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for the float comparisons (the values pass through cgltf's float parse). */
#define TEST_EPSILON 1e-6f


// Static functions: helpers.
/* Five materials: glTF's defaults (no pbrMetallicRoughness block, no textures), every factor, the emission strength
 * extension, an occlusion texture's strength, and textures raylib skips (a normal map without the block). */
static const char* const MaterialsGltf =
    "{\"asset\": {\"version\": \"2.0\"}, \"extensionsUsed\": [\"KHR_materials_emissive_strength\"], \"materials\": ["
    "{\"name\": \"plain\"},"
    "{\"name\": \"factors\", \"pbrMetallicRoughness\": {\"baseColorFactor\": [0.5, 0.25, 0.125, 0.75], "
        "\"metallicFactor\": 0.0, \"roughnessFactor\": 0.85}, \"emissiveFactor\": [1.0, 0.5, 0.0]},"
    "{\"name\": \"strong\", \"pbrMetallicRoughness\": {}, \"emissiveFactor\": [1, 1, 1], \"extensions\": "
        "{\"KHR_materials_emissive_strength\": {\"emissiveStrength\": 7.5}}},"
    "{\"name\": \"occluded\", \"pbrMetallicRoughness\": {}, \"occlusionTexture\": {\"index\": 0, \"strength\": 0.25}},"
    "{\"name\": \"skipped\", \"normalTexture\": {\"index\": 0}}"
    "], \"textures\": [{\"source\": 0}], \"images\": [{\"uri\": \"not_loaded.png\"}]}";

static bool IsNear(float a, float b)
{
    return fabsf(a - b) < TEST_EPSILON;
}

static Error ReadText(const char* text, ModelGltfMaterials* outMaterials)
{
    return ModelGltfMaterials_Read(text, strlen(text), "test file", NULL, outMaterials);
}

/* A binary glTF (.glb) holding @p json as its only chunk, padded with spaces as the format asks. */
static void WriteGlb(const char* json, GenericBuffer* out)
{
    uint32_t JsonLength = (uint32_t)strlen(json);
    uint32_t PaddedLength = (JsonLength + 3U) & ~3U;
    // The header (magic, version, length) and the JSON chunk's (length, type), little-endian.
    const uint32_t Header[5] = { 0x46546C67U, 2U, 12U + 8U + PaddedLength, PaddedLength, 0x4E4F534AU };
    GenericBuffer_AllocateVariable(out, 0U, sizeof(unsigned char));
    for (size_t Word = 0U; Word < 5U; Word++)
    {
        for (uint32_t Shift = 0U; Shift < 32U; Shift += 8U)
        {
            GenericBuffer_AppendByte(out, (unsigned char)((Header[Word] >> Shift) & 0xFFU));
        }
    }
    for (uint32_t Index = 0U; Index < JsonLength; Index++)
    {
        GenericBuffer_AppendByte(out, (unsigned char)json[Index]);
    }
    for (uint32_t Pad = JsonLength; Pad < PaddedLength; Pad++)
    {
        GenericBuffer_AppendByte(out, (unsigned char)' ');
    }
}

/* Checks the five materials of MaterialsGltf. */
static void CheckMaterials(TestRun* run, const ModelGltfMaterials* materials)
{
    if (!TEST_CHECK(run, (materials->Count == 5U) && (materials->Materials != NULL)))
    {
        return;
    }
    const ModelGltfMaterial* Plain = &materials->Materials[0];
    TEST_CHECK(run, IsNear(Plain->BaseColor[0], 1.0f) && IsNear(Plain->BaseColor[3], 1.0f) && IsNear(Plain->Metallic, 1.0f)
        && IsNear(Plain->Roughness, 1.0f) && IsNear(Plain->Emissive[0], 0.0f) && IsNear(Plain->EmissiveStrength, 1.0f)
        && IsNear(Plain->OcclusionStrength, 1.0f) && !Plain->HasSkippedTextures);
    const ModelGltfMaterial* Factors = &materials->Materials[1];
    TEST_CHECK(run, IsNear(Factors->BaseColor[0], 0.5f) && IsNear(Factors->BaseColor[1], 0.25f)
        && IsNear(Factors->BaseColor[2], 0.125f) && IsNear(Factors->BaseColor[3], 0.75f) && IsNear(Factors->Metallic, 0.0f)
        && IsNear(Factors->Roughness, 0.85f) && IsNear(Factors->Emissive[0], 1.0f) && IsNear(Factors->Emissive[1], 0.5f)
        && IsNear(Factors->Emissive[2], 0.0f) && IsNear(Factors->EmissiveStrength, 1.0f));
    TEST_CHECK(run, IsNear(materials->Materials[2].EmissiveStrength, 7.5f)
        && IsNear(materials->Materials[2].Emissive[2], 1.0f));
    TEST_CHECK(run, IsNear(materials->Materials[3].OcclusionStrength, 0.25f) && !materials->Materials[3].HasSkippedTextures);
    TEST_CHECK(run, materials->Materials[4].HasSkippedTextures);
}


// Tests.
static void MaterialsAreReadWithGltfsDefaults(TestRun* run)
{
    ModelGltfMaterials Materials;
    if (TEST_CHECK_SUCCESS(run, ReadText(MaterialsGltf, &Materials)))
    {
        CheckMaterials(run, &Materials);
    }
    ModelGltfMaterials_Deconstruct(&Materials);
    TEST_CHECK(run, (Materials.Materials == NULL) && (Materials.Count == 0U));

    // A binary file reads the same.
    GenericBuffer Glb;
    WriteGlb(MaterialsGltf, &Glb);
    if (TEST_CHECK_SUCCESS(run, ModelGltfMaterials_Read(Glb._data, Glb._count, "test file", NULL, &Materials)))
    {
        CheckMaterials(run, &Materials);
    }
    ModelGltfMaterials_Deconstruct(&Materials);
    Memory_Free(Glb._data);

    // A file without materials has none.
    if (TEST_CHECK_SUCCESS(run, ReadText("{\"asset\": {\"version\": \"2.0\"}}", &Materials)))
    {
        TEST_CHECK(run, (Materials.Count == 0U) && (Materials.Materials == NULL));
    }
    ModelGltfMaterials_Deconstruct(&Materials);
}

static void ValuesOutsideGltfsRangesAreRefused(TestRun* run)
{
    static const struct { const char* Material; const char* Named; } Cases[] =
    {
        { "\"pbrMetallicRoughness\": {\"baseColorFactor\": [1.5, 0, 0, 1]}", "baseColorFactor" },
        { "\"pbrMetallicRoughness\": {\"metallicFactor\": -0.1}", "metallicFactor" },
        { "\"pbrMetallicRoughness\": {\"roughnessFactor\": 2}", "roughnessFactor" },
        { "\"emissiveFactor\": [0, -1, 0]", "emissiveFactor" },
        { "\"occlusionTexture\": {\"index\": 0, \"strength\": 1.5}", "occlusionTexture.strength" },
        { "\"extensions\": {\"KHR_materials_emissive_strength\": {\"emissiveStrength\": -1}}", "emissiveStrength" },
        { "\"extensions\": {\"KHR_materials_emissive_strength\": {\"emissiveStrength\": 1e39}}", "emissiveStrength" },
    };
    char Text[512];
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        snprintf(Text, sizeof(Text), "{\"asset\": {\"version\": \"2.0\"}, \"materials\": [{}, {\"name\": \"bad\", %s}], "
            "\"textures\": [{\"source\": 0}], \"images\": [{\"uri\": \"x.png\"}]}", Cases[Index].Material);
        ModelGltfMaterials Materials;
        Error Result = ReadText(Text, &Materials);
        TEST_CHECK(run, Result.Code == ErrorCode_InvalidAssetData);
        TEST_CHECK(run, (Result.Message != NULL) && (strstr((const char*)Result.Message, "test file: material 1 (\"bad\")") != NULL)
            && (strstr((const char*)Result.Message, Cases[Index].Named) != NULL));
        TEST_CHECK(run, (Materials.Materials == NULL) && (Materials.Count == 0U));
        Error_Deconstruct(&Result);
    }

    // Not glTF 2.0: nothing, no JSON, glTF 1, a cut binary header.
    static const char* const NotGltf[] = { "", "not json", "{\"asset\": {\"version\": \"1.0\"}}", "glTF\x02" };
    for (size_t Index = 0U; Index < (sizeof(NotGltf) / sizeof(NotGltf[0])); Index++)
    {
        ModelGltfMaterials Materials;
        TEST_CHECK_ERROR(run, ReadText(NotGltf[Index], &Materials), ErrorCode_InvalidAssetData);
        TEST_CHECK(run, Materials.Materials == NULL);
    }
}

static void ColoursAreEncodedAsTheRendererDecodesThem(TestRun* run)
{
    // Every byte the renderer linearizes (to the power of the gamma) comes back as itself.
    bool IsRoundTrip = true;
    for (int Byte = 0; Byte <= 255; Byte++)
    {
        float Linear = powf((float)Byte / 255.0f, MODEL_GLTF_COLOR_GAMMA);
        float Channels[3] = { Linear, Linear, Linear };
        Color Encoded = ModelGltfMaterials_ToColor(Channels, 1.0f);
        IsRoundTrip = IsRoundTrip && (Encoded.r == Byte) && (Encoded.g == Byte) && (Encoded.b == Byte) && (Encoded.a == 255);
    }
    TEST_CHECK(run, IsRoundTrip);
    // A mid grey in linear light is a light grey in sRGB; alpha is linear; out-of-range values (and NaN) are clamped.
    const float Grey[3] = { 0.2140f, 0.0f, 1.0f };
    Color Mid = ModelGltfMaterials_ToColor(Grey, 0.5f);
    TEST_CHECK(run, (Mid.r == 127) && (Mid.g == 0) && (Mid.b == 255) && (Mid.a == 128));
    const float Wild[3] = { 1.5f, -1.0f, NAN };
    Color Clamped = ModelGltfMaterials_ToColor(Wild, 2.0f);
    TEST_CHECK(run, (Clamped.r == 255) && (Clamped.g == 0) && (Clamped.b == 0) && (Clamped.a == 255));
}

static void AFileIsReadFromDisk(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, 0U, sizeof(unsigned char));
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    ModelGltfMaterials Materials;
    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "model_gltf_materials", &Directory))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, (const unsigned char*)u8"materials.gltf", &Path))
        && TEST_CHECK_SUCCESS(run, FileSystem_WriteAllText(Path._data, (const unsigned char*)MaterialsGltf)))
    {
        if (TEST_CHECK_SUCCESS(run, ModelGltfMaterials_ReadFile(Path._data, "test file", &Materials)))
        {
            CheckMaterials(run, &Materials);
        }
        ModelGltfMaterials_Deconstruct(&Materials);
    }
    GenericBuffer_Clear(&Path);
    if (TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, (const unsigned char*)u8"missing.glb", &Path)))
    {
        Error Missing = ModelGltfMaterials_ReadFile(Path._data, "test file", &Materials);
        TEST_CHECK(run, Missing.Code != ErrorCode_Success);
        TEST_CHECK(run, Materials.Materials == NULL);
        Error_Deconstruct(&Missing);
    }
    Memory_Free(Path._data);
    Memory_Free(Directory._data);
}


// Public functions.
void ModelGltfMaterialsTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "materials_are_read_with_gltfs_defaults", MaterialsAreReadWithGltfsDefaults },
        { "values_outside_gltfs_ranges_are_refused", ValuesOutsideGltfsRangesAreRefused },
        { "colours_are_encoded_as_the_renderer_decodes_them", ColoursAreEncodedAsTheRendererDecodesThem },
        { "a_file_is_read_from_disk", AFileIsReadFromDisk },
    };
    TestRun_RunSuite(run, "ModelGltfMaterials", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
