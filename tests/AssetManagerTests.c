#include "TestSuites.h"
#include "AssetManager.h"
#include "wr/WRIO.h"
#include "wr/WRJSON.h"
#include "wr/WRString.h"
#include <string.h>


// Macros.
/** Initial capacity (bytes) of path and content buffers; they grow as needed. */
#define ASSET_TEST_BUFFER_CAPACITY ((size_t)256)


// Types.
/* A manager with the standard asset types whose single search root is the test's own work directory. Tests
 * write their definition and resource files under Root, then call AssetFixture_Read. */
typedef struct AssetFixtureStruct
{
    AssetManager* Manager;
    StandardAssetTypes Types;
    JSONObjectPool* Pool;
    GenericBuffer Root;
} AssetFixture;


// Fields.
/* Extension filters matching what the loaders accept (models and textures), named once for the tests. */
static const unsigned char* const ModelExtensions[] =
{
    (const unsigned char*)u8"obj", (const unsigned char*)u8"gltf", (const unsigned char*)u8"glb",
    (const unsigned char*)u8"iqm", (const unsigned char*)u8"vox", (const unsigned char*)u8"m3d"
};
static const unsigned char* const TextureExtensions[] =
{
    (const unsigned char*)u8"png", (const unsigned char*)u8"bmp", (const unsigned char*)u8"gif",
    (const unsigned char*)u8"qoi", (const unsigned char*)u8"dds"
};
static const AssetExtensionFilter ModelFilter = { ModelExtensions, sizeof(ModelExtensions) / sizeof(ModelExtensions[0]) };
static const AssetExtensionFilter TextureFilter = { TextureExtensions, sizeof(TextureExtensions) / sizeof(TextureExtensions[0]) };


// Static functions.
static bool AssetFixture_Construct(TestRun* run, const char* name, AssetFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->Root, ASSET_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    return TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, name, &self->Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &self->Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(self->Manager, &self->Types, &self->Pool));
}

static void AssetFixture_Deconstruct(TestRun* run, AssetFixture* self)
{
    if (self->Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(self->Manager));
    }
    if (self->Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(self->Pool));
    }
    Memory_Free(self->Root._data);
}

/* Writes a file under the fixture's root (a definition or a resource). */
static bool AssetFixture_Write(TestRun* run, AssetFixture* self, const char* relativePath, const char* text)
{
    return TEST_CHECK_SUCCESS(run, Test_WriteTextFile(self->Root._data, relativePath, text));
}

/* Adds the fixture's root as the only search root and reads its definitions. */
static bool AssetFixture_Read(TestRun* run, AssetFixture* self, size_t* outFailedFileCount)
{
    return TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(self->Manager, self->Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(self->Manager, outFailedFileCount));
}

static bool AssetFixture_HasDefinition(AssetFixture* self, AssetTypeID type, const char* name)
{
    bool Exists = false;
    Error Result = AssetManager_HasDefinition(self->Manager, type, (const unsigned char*)name, &Exists);
    Error_Deconstruct(&Result);
    return Exists;
}

/* Resolves a File location and compares the resolved file's contents with @p expectedText. Each fixture
 * resource contains its own file name, so the contents identify which file the resolver picked. */
static bool ResolvesToFile(TestRun* run, AssetFixture* self, AssetTypeID type, const char* location,
    const AssetExtensionFilter* filter, const char* expectedText)
{
    AssetLocation Location = { AssetLocationType_File, (const unsigned char*)location };
    IOStream* Stream = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_OpenResource(self->Manager, type, &Location, filter, &Stream)))
    {
        return false;
    }

    GenericBuffer Contents;
    GenericBuffer_AllocateVariable(&Contents, ASSET_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    bool IsMatch = TEST_CHECK_SUCCESS(run, IOStream_ReadAll(Stream, &Contents))
        && TEST_CHECK(run, GenericBuffer_NullTerminate(&Contents));
    if (IsMatch)
    {
        IsMatch = TEST_CHECK(run, strcmp((const char*)Contents._data, expectedText) == 0);
    }
    Memory_Free(Contents._data);
    TEST_CHECK_SUCCESS(run, AssetManager_CloseResource(self->Manager, Stream));
    return IsMatch;
}

/* Resolves a File location that must fail with @p expectedCode. */
static void FailsToResolve(TestRun* run, AssetFixture* self, AssetTypeID type, const char* location,
    const AssetExtensionFilter* filter, ErrorCode expectedCode)
{
    AssetLocation Location = { AssetLocationType_File, (const unsigned char*)location };
    IOStream* Stream = NULL;
    TEST_CHECK_ERROR(run, AssetManager_OpenResource(self->Manager, type, &Location, filter, &Stream), expectedCode);
    TEST_CHECK(run, Stream == NULL);
}

static bool MessageContains(const Error* error, const char* text)
{
    return (error->Message != NULL) && (strstr((const char*)error->Message, text) != NULL);
}

static void BadDefinitionFileIsSkippedAndCounted(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "bad_file_skipped", &Fixture)
        && AssetFixture_Write(run, &Fixture, "fonts/broken/broken.json", "{ not json")
        && AssetFixture_Write(run, &Fixture, "shaders/good/good.json",
            "{ \"name\": \"good\", \"fragment_location\": \"good/fragment\" }"))
    {
        size_t FailedCount = 0;
        if (AssetFixture_Read(run, &Fixture, &FailedCount))
        {
            TEST_CHECK(run, FailedCount == 1);
            TEST_CHECK(run, AssetFixture_HasDefinition(&Fixture, Fixture.Types.Shader, "good"));
        }
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

static void InvalidNameIsRejected(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "invalid_name", &Fixture)
        && AssetFixture_Write(run, &Fixture, "shaders/bad/bad.json",
            "{ \"name\": \"Bad Name\", \"fragment_location\": \"bad/fragment\" }")
        && AssetFixture_Write(run, &Fixture, "shaders/empty/empty.json",
            "{ \"name\": \"\", \"fragment_location\": \"empty/fragment\" }"))
    {
        size_t FailedCount = 0;
        if (AssetFixture_Read(run, &Fixture, &FailedCount))
        {
            TEST_CHECK(run, FailedCount == 2);
            TEST_CHECK(run, !AssetFixture_HasDefinition(&Fixture, Fixture.Types.Shader, "Bad Name"));
            TEST_CHECK(run, AssetManager_IsValidAssetName((const unsigned char*)u8"world_pbr.v2-final"));
            TEST_CHECK(run, !AssetManager_IsValidAssetName((const unsigned char*)u8"2d_sprite"));
        }
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

/* A shader definition's "defines" (B03): an array of at most 16 distinct names GLSL lets a shader define; anything else
 * fails the definition. */
static void ShaderDefinitionsCheckTheirDefines(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "shader_defines", &Fixture)
        && AssetFixture_Write(run, &Fixture, "shaders/ok/ok.json",
            "{ \"name\": \"ok_defines\", \"fragment_location\": \"ok/fragment\", \"defines\": [\"A\", \"B_2\"] }")
        && AssetFixture_Write(run, &Fixture, "shaders/empty/empty.json",
            "{ \"name\": \"empty_defines\", \"fragment_location\": \"empty/fragment\", \"defines\": [] }")
        && AssetFixture_Write(run, &Fixture, "shaders/sixteen/sixteen.json",
            "{ \"name\": \"sixteen_defines\", \"fragment_location\": \"sixteen/fragment\", \"defines\": [\"A0\", \"A1\", "
            "\"A2\", \"A3\", \"A4\", \"A5\", \"A6\", \"A7\", \"A8\", \"A9\", \"A10\", \"A11\", \"A12\", \"A13\", \"A14\", "
            "\"A15\"] }")
        && AssetFixture_Write(run, &Fixture, "shaders/not_array/not_array.json",
            "{ \"name\": \"not_array\", \"fragment_location\": \"not_array/fragment\", \"defines\": \"A\" }")
        && AssetFixture_Write(run, &Fixture, "shaders/not_string/not_string.json",
            "{ \"name\": \"not_string\", \"fragment_location\": \"not_string/fragment\", \"defines\": [\"A\", 1] }")
        && AssetFixture_Write(run, &Fixture, "shaders/reserved/reserved.json",
            "{ \"name\": \"reserved\", \"fragment_location\": \"reserved/fragment\", \"defines\": [\"GL_X\"] }")
        && AssetFixture_Write(run, &Fixture, "shaders/twice/twice.json",
            "{ \"name\": \"twice\", \"fragment_location\": \"twice/fragment\", \"defines\": [\"A\", \"B\", \"A\"] }")
        && AssetFixture_Write(run, &Fixture, "shaders/seventeen/seventeen.json",
            "{ \"name\": \"seventeen\", \"fragment_location\": \"seventeen/fragment\", \"defines\": [\"A0\", \"A1\", "
            "\"A2\", \"A3\", \"A4\", \"A5\", \"A6\", \"A7\", \"A8\", \"A9\", \"A10\", \"A11\", \"A12\", \"A13\", \"A14\", "
            "\"A15\", \"A16\"] }"))
    {
        size_t FailedCount = 0;
        if (AssetFixture_Read(run, &Fixture, &FailedCount))
        {
            TEST_CHECK(run, FailedCount == 5);
            TEST_CHECK(run, AssetFixture_HasDefinition(&Fixture, Fixture.Types.Shader, "ok_defines"));
            TEST_CHECK(run, AssetFixture_HasDefinition(&Fixture, Fixture.Types.Shader, "empty_defines"));
            TEST_CHECK(run, AssetFixture_HasDefinition(&Fixture, Fixture.Types.Shader, "sixteen_defines"));
            TEST_CHECK(run, !AssetFixture_HasDefinition(&Fixture, Fixture.Types.Shader, "twice"));
            TEST_CHECK(run, !AssetFixture_HasDefinition(&Fixture, Fixture.Types.Shader, "seventeen"));
        }
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

static void SameRootDuplicateIsAnError(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "same_root_duplicate", &Fixture)
        && AssetFixture_Write(run, &Fixture, "shaders/a/a.json",
            "{ \"name\": \"duplicated\", \"fragment_location\": \"a/fragment\" }")
        && AssetFixture_Write(run, &Fixture, "shaders/b/b.json",
            "{ \"name\": \"duplicated\", \"fragment_location\": \"b/fragment\" }"))
    {
        size_t FailedCount = 0;
        if (AssetFixture_Read(run, &Fixture, &FailedCount))
        {
            // One of the two is registered (directory order decides which); the other is a counted failure.
            TEST_CHECK(run, FailedCount == 1);
            TEST_CHECK(run, AssetFixture_HasDefinition(&Fixture, Fixture.Types.Shader, "duplicated"));
        }
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

static void ModelLocationIgnoresMaterialLibrary(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "model_ignores_mtl", &Fixture)
        && AssetFixture_Write(run, &Fixture, "models/ship.obj", "ship.obj")
        && AssetFixture_Write(run, &Fixture, "models/ship.mtl", "ship.mtl")
        && AssetFixture_Read(run, &Fixture, NULL))
    {
        ResolvesToFile(run, &Fixture, Fixture.Types.Model, "ship", &ModelFilter, "ship.obj");
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

static void AmbiguousLocationIsAnError(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "ambiguous_location", &Fixture)
        && AssetFixture_Write(run, &Fixture, "sprite_sheets/x.png", "x.png")
        && AssetFixture_Write(run, &Fixture, "sprite_sheets/x.bmp", "x.bmp")
        && AssetFixture_Read(run, &Fixture, NULL))
    {
        FailsToResolve(run, &Fixture, Fixture.Types.SpriteSheet, "x", &TextureFilter, ErrorCode_InvalidAssetDefinition);
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

static void LocationMatchesTheWholeLastName(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "whole_last_name", &Fixture)
        && AssetFixture_Write(run, &Fixture, "sprite_sheets/dir/b.png", "dir/b.png")
        && AssetFixture_Write(run, &Fixture, "sprite_sheets/other/b.c.png", "other/b.c.png")
        && AssetFixture_Read(run, &Fixture, NULL))
    {
        // "dir/b.c" means a file named "b.c" plus an extension, never "b.png".
        FailsToResolve(run, &Fixture, Fixture.Types.SpriteSheet, "dir/b.c", &TextureFilter, ErrorCode_FileNotFound);
        ResolvesToFile(run, &Fixture, Fixture.Types.SpriteSheet, "other/b.c", &TextureFilter, "other/b.c.png");
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

static void UnsupportedExtensionIsReported(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "unsupported_extension", &Fixture)
        && AssetFixture_Write(run, &Fixture, "sprite_sheets/photo.jpg", "photo.jpg")
        && AssetFixture_Read(run, &Fixture, NULL))
    {
        FailsToResolve(run, &Fixture, Fixture.Types.SpriteSheet, "photo", &TextureFilter, ErrorCode_InvalidAssetData);
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

static void FailedLoadIsCachedUntilCleared(TestRun* run)
{
    AssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (AssetFixture_Construct(run, "failed_load_cached", &Fixture)
        && AssetFixture_Write(run, &Fixture, "fonts/missing/missing.json",
            "{ \"name\": \"missing_font\", \"location\": \"missing/nothing\" }")
        && AssetFixture_Read(run, &Fixture, NULL)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        // The font file does not exist, so loading fails while resolving the file (before any GL call).
        GameFont* Font = NULL;
        Error First = AssetManager_LoadFont(Fixture.Manager, (const unsigned char*)u8"missing_font", User, &Font);
        TEST_CHECK(run, First.Code == ErrorCode_FileNotFound);
        TEST_CHECK(run, !MessageContains(&First, "failed to load earlier"));
        Error_Deconstruct(&First);

        Error Cached = AssetManager_LoadFont(Fixture.Manager, (const unsigned char*)u8"missing_font", User, &Font);
        TEST_CHECK(run, Cached.Code == ErrorCode_FileNotFound);
        TEST_CHECK(run, MessageContains(&Cached, "failed to load earlier"));
        Error_Deconstruct(&Cached);

        TEST_CHECK_SUCCESS(run, AssetManager_ClearLoadFailures(Fixture.Manager));
        Error Retried = AssetManager_LoadFont(Fixture.Manager, (const unsigned char*)u8"missing_font", User, &Font);
        TEST_CHECK(run, Retried.Code == ErrorCode_FileNotFound);
        TEST_CHECK(run, !MessageContains(&Retried, "failed to load earlier"));
        Error_Deconstruct(&Retried);
        TEST_CHECK(run, Font == NULL);

        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    AssetFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void AssetManagerTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "bad_definition_file_is_skipped_and_counted", BadDefinitionFileIsSkippedAndCounted },
        { "invalid_name_is_rejected", InvalidNameIsRejected },
        { "shader_definitions_check_their_defines", ShaderDefinitionsCheckTheirDefines },
        { "same_root_duplicate_is_an_error", SameRootDuplicateIsAnError },
        { "model_location_ignores_material_library", ModelLocationIgnoresMaterialLibrary },
        { "ambiguous_location_is_an_error", AmbiguousLocationIsAnError },
        { "location_matches_the_whole_last_name", LocationMatchesTheWholeLastName },
        { "unsupported_extension_is_reported", UnsupportedExtensionIsReported },
        { "failed_load_is_cached_until_cleared", FailedLoadIsCachedUntilCleared },
    };
    TestRun_RunSuite(run, "AssetManager", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
