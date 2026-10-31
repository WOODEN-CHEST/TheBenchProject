#include "TestSuites.h"
#include "TestAssets.h"
#include "AssetManager.h"
#include "AssetDefinitionCommon.h"
#include "GameTexture.h"
#include "wr/WRJSON.h"
#include <string.h>


// Macros.
/** Where the game's shipped assets live, relative to the repository root. */
#define SHIPPED_ASSET_ROOT "compile/out/asset"


// Static functions.
/* Parses @p json and reads its "texture_properties" with AssetJSON_ReadTextureProperties. */
static Error ReadProperties(TestAssetFixture* fixture, const char* json, bool areMipmapsSupported,
    TextureProperties* outProperties)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    JSONObjectValue Root;
    JSONCompound* RootCompound = NULL;
    Error Result = AssetJSON_DeserializeRoot(fixture->Pool, &Bytes, (const unsigned char*)u8"test", &Root, &RootCompound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadTextureProperties(RootCompound, (const unsigned char*)u8"texture_properties",
            areMipmapsSupported, outProperties);
    }
    Error ReturnResult = JSONObjectPool_ReturnValue(fixture->Pool, &Root);
    Error_Deconstruct(&ReturnResult);
    Memory_Free(Bytes._data);
    return Result;
}

/* Parses @p json and reads its "texture_properties" with AssetJSON_ReadTexturePropertiesWithDefaults (mipmaps
 * supported). */
static Error ReadPropertiesWithDefaults(TestAssetFixture* fixture, const char* json, const TextureProperties* defaults,
    TextureProperties* outProperties, bool* outIsPresent)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    JSONObjectValue Root;
    JSONCompound* RootCompound = NULL;
    Error Result = AssetJSON_DeserializeRoot(fixture->Pool, &Bytes, (const unsigned char*)u8"test", &Root, &RootCompound);
    if (Result.Code == ErrorCode_Success)
    {
        Result = AssetJSON_ReadTexturePropertiesWithDefaults(RootCompound, (const unsigned char*)u8"texture_properties",
            true, defaults, outProperties, outIsPresent);
    }
    Error ReturnResult = JSONObjectPool_ReturnValue(fixture->Pool, &Root);
    Error_Deconstruct(&ReturnResult);
    Memory_Free(Bytes._data);
    return Result;
}

static bool ArePropertiesEqual(const TextureProperties* properties, int filter, int wrap, bool areMipmapsEnabled)
{
    return (properties->Filter == filter) && (properties->Wrap == wrap)
        && (properties->AreMipmapsEnabled == areMipmapsEnabled);
}

/* Builds (and destroys) a definition of @p type from @p json without registering it. */
static Error BuildDefinition(TestAssetFixture* fixture, AssetTypeID type, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, type, &Bytes, (const unsigned char*)u8"test", &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}

static bool MessageContains(const Error* error, const char* text)
{
    return (error->Message != NULL) && (strstr((const char*)error->Message, text) != NULL);
}

/* A fixture whose only search root is a fresh work directory; *outRoot receives its path (the caller frees it). */
static bool ConstructWithWorkRoot(TestRun* run, const char* name, TestAssetFixture* fixture, GenericBuffer* outRoot)
{
    GenericBuffer_AllocateVariable(outRoot, 0U, sizeof(unsigned char));
    return TestAssetFixture_Construct(run, fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, name, outRoot));
}


// Tests.
static void TexturePropertiesHaveTodaysDefaults(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TextureProperties Properties;
        if (TEST_CHECK_SUCCESS(run, ReadProperties(&Fixture, "{}", true, &Properties)))
        {
            TEST_CHECK(run, Properties.Filter == TEXTURE_FILTER_BILINEAR);
            TEST_CHECK(run, Properties.Wrap == TEXTURE_WRAP_REPEAT);
            TEST_CHECK(run, !Properties.AreMipmapsEnabled);
        }
        if (TEST_CHECK_SUCCESS(run, ReadProperties(&Fixture, "{\"texture_properties\": {}}", false, &Properties)))
        {
            TEST_CHECK(run, (Properties.Filter == TEXTURE_FILTER_BILINEAR) && (Properties.Wrap == TEXTURE_WRAP_REPEAT));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TexturePropertiesReadEveryValue(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TextureProperties Properties;
        if (TEST_CHECK_SUCCESS(run, ReadProperties(&Fixture, "{\"texture_properties\": "
            "{\"filtering\": \"trilinear\", \"wrap\": \"mirror_repeat\", \"mipmaps\": true}}", true, &Properties)))
        {
            TEST_CHECK(run, Properties.Filter == TEXTURE_FILTER_TRILINEAR);
            TEST_CHECK(run, Properties.Wrap == TEXTURE_WRAP_MIRROR_REPEAT);
            TEST_CHECK(run, Properties.AreMipmapsEnabled);
        }
        if (TEST_CHECK_SUCCESS(run, ReadProperties(&Fixture, "{\"texture_properties\": "
            "{\"filtering\": \"point\", \"wrap\": \"clamp\", \"mipmaps\": \"true\"}}", true, &Properties)))
        {
            TEST_CHECK(run, (Properties.Filter == TEXTURE_FILTER_POINT) && (Properties.Wrap == TEXTURE_WRAP_CLAMP));
            TEST_CHECK(run, Properties.AreMipmapsEnabled);
        }
        if (TEST_CHECK_SUCCESS(run, ReadProperties(&Fixture, "{\"texture_properties\": {\"wrap\": \"mirror_clamp\"}}",
            false, &Properties)))
        {
            TEST_CHECK(run, Properties.Wrap == TEXTURE_WRAP_MIRROR_CLAMP);
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TexturePropertiesRejectBadValues(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TextureProperties Properties;
        const char* const Bad[] =
        {
            "{\"texture_properties\": {\"wrap\": \"tile\"}}",
            "{\"texture_properties\": {\"wrap\": 1}}",
            "{\"texture_properties\": {\"filtering\": \"cubic\"}}",
            "{\"texture_properties\": {\"filtering\": \"trilinear\"}}",
            "{\"texture_properties\": {\"filtering\": \"trilinear\", \"mipmaps\": false}}",
            "{\"texture_properties\": \"bilinear\"}"
        };
        for (size_t Index = 0U; Index < (sizeof(Bad) / sizeof(Bad[0])); Index++)
        {
            TEST_CHECK_ERROR(run, ReadProperties(&Fixture, Bad[Index], true, &Properties), ErrorCode_InvalidAssetDefinition);
        }
        TEST_CHECK_ERROR(run, ReadProperties(&Fixture, "{\"texture_properties\": {\"mipmaps\": \"often\"}}", true,
            &Properties), ErrorCode_InvalidJSON);

        // Without mipmap support trilinear is rejected even with "mipmaps": true, which such a loader ignores.
        Error Trilinear = ReadProperties(&Fixture, "{\"texture_properties\": {\"filtering\": \"trilinear\", "
            "\"mipmaps\": true}}", false, &Properties);
        TEST_CHECK(run, Trilinear.Code == ErrorCode_InvalidAssetDefinition);
        TEST_CHECK(run, MessageContains(&Trilinear, "\"point\" or \"bilinear\""));
        Error_Deconstruct(&Trilinear);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TexturePropertiesTakeTheCallersDefaultsForLeftOutKeys(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    // The model maps' defaults: smooth at every distance.
    const TextureProperties Smooth = { .Filter = TEXTURE_FILTER_TRILINEAR, .Wrap = TEXTURE_WRAP_REPEAT,
        .AreMipmapsEnabled = true };
    TextureProperties Properties;
    bool IsPresent = true;
    if (TEST_CHECK_SUCCESS(run, ReadPropertiesWithDefaults(&Fixture, "{}", &Smooth, &Properties, &IsPresent)))
    {
        TEST_CHECK(run, !IsPresent && ArePropertiesEqual(&Properties, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT, true));
    }
    if (TEST_CHECK_SUCCESS(run, ReadPropertiesWithDefaults(&Fixture, "{\"texture_properties\": {}}", &Smooth,
        &Properties, &IsPresent)))
    {
        TEST_CHECK(run, IsPresent && ArePropertiesEqual(&Properties, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT, true));
    }
    // One key given, the others the defaults'.
    if (TEST_CHECK_SUCCESS(run, ReadPropertiesWithDefaults(&Fixture, "{\"texture_properties\": {\"filtering\": "
        "\"point\"}}", &Smooth, &Properties, NULL)))
    {
        TEST_CHECK(run, ArePropertiesEqual(&Properties, TEXTURE_FILTER_POINT, TEXTURE_WRAP_REPEAT, true));
    }
    if (TEST_CHECK_SUCCESS(run, ReadPropertiesWithDefaults(&Fixture, "{\"texture_properties\": {\"wrap\": \"clamp\"}}",
        &Smooth, &Properties, NULL)))
    {
        TEST_CHECK(run, ArePropertiesEqual(&Properties, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_CLAMP, true));
    }
    // Mipmaps turned off: a default of trilinear (which needs them) becomes bilinear; trilinear written out is refused.
    if (TEST_CHECK_SUCCESS(run, ReadPropertiesWithDefaults(&Fixture, "{\"texture_properties\": {\"mipmaps\": false}}",
        &Smooth, &Properties, NULL)))
    {
        TEST_CHECK(run, ArePropertiesEqual(&Properties, TEXTURE_FILTER_BILINEAR, TEXTURE_WRAP_REPEAT, false));
    }
    TEST_CHECK_ERROR(run, ReadPropertiesWithDefaults(&Fixture, "{\"texture_properties\": {\"filtering\": \"trilinear\", "
        "\"mipmaps\": false}}", &Smooth, &Properties, NULL), ErrorCode_InvalidAssetDefinition);
    // Other defaults are kept as they are; the defaults may be the output itself (an override over its model's).
    const TextureProperties Crisp = { .Filter = TEXTURE_FILTER_POINT, .Wrap = TEXTURE_WRAP_CLAMP, .AreMipmapsEnabled = false };
    if (TEST_CHECK_SUCCESS(run, ReadPropertiesWithDefaults(&Fixture, "{\"texture_properties\": {\"mipmaps\": true}}",
        &Crisp, &Properties, NULL)))
    {
        TEST_CHECK(run, ArePropertiesEqual(&Properties, TEXTURE_FILTER_POINT, TEXTURE_WRAP_CLAMP, true));
    }
    Properties = Smooth;
    if (TEST_CHECK_SUCCESS(run, ReadPropertiesWithDefaults(&Fixture, "{\"texture_properties\": {\"wrap\": "
        "\"mirror_repeat\"}}", &Properties, &Properties, NULL)))
    {
        TEST_CHECK(run, ArePropertiesEqual(&Properties, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_MIRROR_REPEAT, true));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void ModelDefinitionsReadTheirTextureProperties(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    // A model's own texture_properties, and an override's over them (its keys left out are the model's).
    const char* const Good[] =
    {
        "{\"name\": \"m\", \"location\": \"m\", \"texture_properties\": {\"filtering\": \"bilinear\"}}",
        "{\"name\": \"m\", \"location\": \"m\", \"texture_properties\": {\"mipmaps\": false}}",
        "{\"name\": \"m\", \"location\": \"m\", \"texture_properties\": {\"mipmaps\": false}, \"material_overrides\": "
            "[{\"slot\": 1, \"texture_properties\": {\"wrap\": \"clamp\"}}]}",
        "{\"name\": \"m\", \"location\": \"m\", \"material_overrides\": [{\"slot\": 1, \"texture_properties\": "
            "{\"filtering\": \"point\"}}]}"
    };
    for (size_t Index = 0U; Index < (sizeof(Good) / sizeof(Good[0])); Index++)
    {
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, Fixture.Types.Model, Good[Index]));
    }
    // Trilinear without mipmaps, at the root or in an override that turns the model's mipmaps off.
    const char* const Bad[] =
    {
        "{\"name\": \"m\", \"location\": \"m\", \"texture_properties\": {\"filtering\": \"trilinear\", \"mipmaps\": false}}",
        "{\"name\": \"m\", \"location\": \"m\", \"texture_properties\": \"smooth\"}",
        "{\"name\": \"m\", \"location\": \"m\", \"material_overrides\": [{\"slot\": 1, \"texture_properties\": "
            "{\"filtering\": \"trilinear\", \"mipmaps\": false}}]}"
    };
    for (size_t Index = 0U; Index < (sizeof(Bad) / sizeof(Bad[0])); Index++)
    {
        TEST_CHECK_ERROR(run, BuildDefinition(&Fixture, Fixture.Types.Model, Bad[Index]), ErrorCode_InvalidAssetDefinition);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TextureIsAStandardType(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        AssetTypeID ById = ASSET_TYPE_ID_INVALID;
        const unsigned char* Directory = NULL;
        TEST_CHECK(run, Fixture.Types.Texture != ASSET_TYPE_ID_INVALID);
        TEST_CHECK_SUCCESS(run, AssetManager_GetAssetTypeByName(Fixture.Manager, (const unsigned char*)u8"texture", &ById));
        TEST_CHECK(run, ById == Fixture.Types.Texture);
        if (TEST_CHECK_SUCCESS(run, AssetManager_GetAssetTypeDirectoryName(Fixture.Manager, Fixture.Types.Texture, &Directory)))
        {
            TEST_CHECK(run, strcmp((const char*)Directory, "textures") == 0);
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TextureDefinitionsValidateTheirKeys(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        AssetTypeID TextureType = Fixture.Types.Texture;
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, TextureType, "{\"name\": \"t\", \"location\": \"sky/t\"}"));
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, TextureType, "{\"name\": \"t\", \"location\": \"sky/t\", "
            "\"texture_properties\": {\"filtering\": \"trilinear\", \"wrap\": \"repeat\", \"mipmaps\": true}, "
            "\"color_space\": \"srgb\"}"));
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, TextureType, "{\"name\": \"t\", \"location\": "
            "{\"type\": \"file\", \"value\": \"masks/t\"}, \"color_space\": \"linear\"}"));

        const char* const Bad[] =
        {
            "{\"name\": \"t\", \"location\": \"sky/t\", \"color_space\": \"rgb\"}",
            "{\"name\": \"t\", \"location\": \"sky/t\", \"color_space\": true}",
            "{\"name\": \"t\", \"location\": \"sky/t\", \"texture_properties\": {\"wrap\": \"tile\"}}",
            "{\"name\": \"t\", \"location\": \"sky/t\", \"texture_properties\": {\"filtering\": \"trilinear\"}}",
            "{\"name\": \"t\", \"location\": {\"type\": \"reference\", \"value\": \"generated\"}}",
            "{\"name\": \"t\"}",
            "{\"location\": \"sky/t\"}"
        };
        for (size_t Index = 0U; Index < (sizeof(Bad) / sizeof(Bad[0])); Index++)
        {
            TEST_CHECK_ERROR(run, BuildDefinition(&Fixture, TextureType, Bad[Index]), ErrorCode_InvalidAssetDefinition);
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TextureNamesAreValidated(TestRun* run)
{
    TestAssetFixture Fixture;
    GenericBuffer Root;
    if (ConstructWithWorkRoot(run, "texture_names", &Fixture, &Root)
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Root._data, "textures/good/good.json",
            "{\"name\": \"good_texture\", \"location\": \"good/good\"}"))
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Root._data, "textures/bad/bad.json",
            "{\"name\": \"Bad Texture\", \"location\": \"bad/bad\"}"))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Root._data)))
    {
        size_t FailedCount = 0U;
        bool HasGood = false;
        TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, &FailedCount));
        TEST_CHECK(run, FailedCount == 1U);
        TEST_CHECK_SUCCESS(run, AssetManager_HasDefinition(Fixture.Manager, Fixture.Types.Texture,
            (const unsigned char*)u8"good_texture", &HasGood));
        TEST_CHECK(run, HasGood);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    Memory_Free(Root._data);
}

static void SpriteSheetsAndFontsRejectTrilinear(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        // They ignore "wrap" and "mipmaps" (still validated), but cannot honour trilinear filtering.
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, Fixture.Types.SpriteSheet, "{\"name\": \"s\", "
            "\"images\": [{\"name\": \"a\", \"location\": \"a\"}], "
            "\"texture_properties\": {\"filtering\": \"point\", \"wrap\": \"clamp\", \"mipmaps\": true}}"));
        TEST_CHECK_ERROR(run, BuildDefinition(&Fixture, Fixture.Types.SpriteSheet, "{\"name\": \"s\", "
            "\"images\": [{\"name\": \"a\", \"location\": \"a\"}], "
            "\"texture_properties\": {\"filtering\": \"trilinear\", \"mipmaps\": true}}"), ErrorCode_InvalidAssetDefinition);
        TEST_CHECK_ERROR(run, BuildDefinition(&Fixture, Fixture.Types.Font, "{\"name\": \"f\", \"location\": \"f/f\", "
            "\"texture_properties\": {\"filtering\": \"trilinear\", \"mipmaps\": true}}"), ErrorCode_InvalidAssetDefinition);
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, Fixture.Types.Model, "{\"name\": \"m\", \"location\": \"m/m\", "
            "\"material_overrides\": [{\"slot\": 0, \"texture_properties\": {\"filtering\": \"trilinear\", "
            "\"wrap\": \"clamp\", \"mipmaps\": true}}]}"));
        // Model maps have mipmaps unless they turn them off, so trilinear alone is fine there.
        TEST_CHECK_SUCCESS(run, BuildDefinition(&Fixture, Fixture.Types.Model, "{\"name\": \"m\", \"location\": \"m/m\", "
            "\"material_overrides\": [{\"slot\": 0, \"texture_properties\": {\"filtering\": \"trilinear\"}}]}"));
        TEST_CHECK_ERROR(run, BuildDefinition(&Fixture, Fixture.Types.Model, "{\"name\": \"m\", \"location\": \"m/m\", "
            "\"material_overrides\": [{\"slot\": 0, \"texture_properties\": {\"filtering\": \"trilinear\", "
            "\"mipmaps\": false}}]}"), ErrorCode_InvalidAssetDefinition);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void OversizedTextureFailsBeforeUpload(TestRun* run)
{
    TestAssetFixture Fixture;
    GenericBuffer Root;
    GenericBuffer ImagePath;
    GenericBuffer_AllocateVariable(&ImagePath, 0U, sizeof(unsigned char));
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (ConstructWithWorkRoot(run, "oversized_texture", &Fixture, &Root)
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Root._data, "textures/wide.json",
            "{\"name\": \"too_wide\", \"location\": \"wide\"}"))
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Root._data, "textures/absent.json",
            "{\"name\": \"absent\", \"location\": \"nothing_here\"}")))
    {
        // An 8193 x 1 image: decoding it needs no GL context, and the size check comes before the upload. raylib's
        // file messages are muted meanwhile (LOG_INFO is its default level).
        SetTraceLogLevel(LOG_WARNING);
        GenericBuffer_AppendString(&ImagePath, Root._data);
        GenericBuffer_AppendString(&ImagePath, (const unsigned char*)u8"/textures/wide.png");
        GenericBuffer_NullTerminate(&ImagePath);
        Image Wide = GenImageColor(8193, 1, WHITE);
        bool IsExported = TEST_CHECK(run, ExportImage(Wide, (const char*)ImagePath._data));
        UnloadImage(Wide);

        if (IsExported && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Root._data))
            && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, NULL))
            && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
        {
            GameTexture* Loaded = NULL;
            Error Result = AssetManager_LoadTexture(Fixture.Manager, (const unsigned char*)u8"too_wide", User, &Loaded);
            TEST_CHECK(run, Result.Code == ErrorCode_InvalidAssetData);
            TEST_CHECK(run, MessageContains(&Result, "8193 x 1 pixels; at most 8192"));
            TEST_CHECK(run, Loaded == NULL);
            Error_Deconstruct(&Result);
            TEST_CHECK_ERROR(run, AssetManager_LoadTexture(Fixture.Manager, (const unsigned char*)u8"absent", User,
                &Loaded), ErrorCode_FileNotFound);
            TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
        }
        SetTraceLogLevel(LOG_INFO);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    Memory_Free(Root._data);
    Memory_Free(ImagePath._data);
}

static void ShippedMilkyWayDefinitionResolves(TestRun* run)
{
    TestAssetFixture Fixture;
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, 0U, sizeof(unsigned char));
    if (TestAssetFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, NULL)))
    {
        bool HasMilkyWay = false;
        TEST_CHECK_SUCCESS(run, AssetManager_HasDefinition(Fixture.Manager, Fixture.Types.Texture,
            (const unsigned char*)u8"sky_milky_way", &HasMilkyWay));
        TEST_CHECK(run, HasMilkyWay);

        // Its placeholder image resolves to exactly one file the texture loader can read.
        AssetLocation Location = { AssetLocationType_File, (const unsigned char*)u8"sky/placeholder_milky_way" };
        IOStream* Stream = NULL;
        if (TEST_CHECK_SUCCESS(run, AssetManager_OpenResource(Fixture.Manager, Fixture.Types.Texture, &Location,
            &ASSET_EXTENSIONS_TEXTURE, &Stream)))
        {
            TEST_CHECK_SUCCESS(run, AssetManager_CloseResource(Fixture.Manager, Stream));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    Memory_Free(Root._data);
}


/* The moon's definition ships, and its placeholder image resolves to exactly one file the texture loader can read. */
static void ShippedMoonDefinitionResolves(TestRun* run)
{
    TestAssetFixture Fixture;
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, 0U, sizeof(unsigned char));
    if (TestAssetFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, NULL)))
    {
        bool HasMoon = false;
        TEST_CHECK_SUCCESS(run, AssetManager_HasDefinition(Fixture.Manager, Fixture.Types.Texture,
            (const unsigned char*)u8"sky_moon", &HasMoon));
        TEST_CHECK(run, HasMoon);

        AssetLocation Location = { AssetLocationType_File, (const unsigned char*)u8"sky/placeholder_moon" };
        IOStream* Stream = NULL;
        if (TEST_CHECK_SUCCESS(run, AssetManager_OpenResource(Fixture.Manager, Fixture.Types.Texture, &Location,
            &ASSET_EXTENSIONS_TEXTURE, &Stream)))
        {
            TEST_CHECK_SUCCESS(run, AssetManager_CloseResource(Fixture.Manager, Stream));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
    Memory_Free(Root._data);
}


// Public functions.
void TextureTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "texture_properties_have_todays_defaults", TexturePropertiesHaveTodaysDefaults },
        { "texture_properties_read_every_value", TexturePropertiesReadEveryValue },
        { "texture_properties_reject_bad_values", TexturePropertiesRejectBadValues },
        { "texture_properties_take_the_callers_defaults_for_left_out_keys",
            TexturePropertiesTakeTheCallersDefaultsForLeftOutKeys },
        { "model_definitions_read_their_texture_properties", ModelDefinitionsReadTheirTextureProperties },
        { "texture_is_a_standard_type", TextureIsAStandardType },
        { "texture_definitions_validate_their_keys", TextureDefinitionsValidateTheirKeys },
        { "texture_names_are_validated", TextureNamesAreValidated },
        { "sprite_sheets_and_fonts_reject_trilinear", SpriteSheetsAndFontsRejectTrilinear },
        { "oversized_texture_fails_before_upload", OversizedTextureFailsBeforeUpload },
        { "shipped_milky_way_definition_resolves", ShippedMilkyWayDefinitionResolves },
        { "shipped_moon_definition_resolves", ShippedMoonDefinitionResolves },
    };
    TestRun_RunSuite(run, "Texture", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
