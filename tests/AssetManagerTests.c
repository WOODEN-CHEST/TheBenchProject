#include "TestSuites.h"
#include "AssetManager.h"
#include "CharacterAsset.h"
#include "NamespacedKey.h"
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
static const AssetExtensionFilter ModelExtensionFilter = { ModelExtensions, sizeof(ModelExtensions) / sizeof(ModelExtensions[0]) };
static const AssetExtensionFilter TextureExtensionFilter = { TextureExtensions, sizeof(TextureExtensions) / sizeof(TextureExtensions[0]) };


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
        }
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

/* A shader definition's "defines": an array of at most 16 distinct names GLSL lets a shader define; anything else fails
 * the definition. */
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
        ResolvesToFile(run, &Fixture, Fixture.Types.Model, "ship", &ModelExtensionFilter, "ship.obj");
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
        FailsToResolve(run, &Fixture, Fixture.Types.SpriteSheet, "x", &TextureExtensionFilter, ErrorCode_InvalidAssetDefinition);
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
        FailsToResolve(run, &Fixture, Fixture.Types.SpriteSheet, "dir/b.c", &TextureExtensionFilter, ErrorCode_FileNotFound);
        ResolvesToFile(run, &Fixture, Fixture.Types.SpriteSheet, "other/b.c", &TextureExtensionFilter, "other/b.c.png");
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
        FailsToResolve(run, &Fixture, Fixture.Types.SpriteSheet, "photo", &TextureExtensionFilter, ErrorCode_InvalidAssetData);
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
        GameFont* LoadedFont = NULL;
        Error First = AssetManager_LoadFont(Fixture.Manager, (const unsigned char*)u8"missing_font", User, &LoadedFont);
        TEST_CHECK(run, First.Code == ErrorCode_FileNotFound);
        TEST_CHECK(run, !MessageContains(&First, "failed to load earlier"));
        Error_Deconstruct(&First);

        Error Cached = AssetManager_LoadFont(Fixture.Manager, (const unsigned char*)u8"missing_font", User, &LoadedFont);
        TEST_CHECK(run, Cached.Code == ErrorCode_FileNotFound);
        TEST_CHECK(run, MessageContains(&Cached, "failed to load earlier"));
        Error_Deconstruct(&Cached);

        TEST_CHECK_SUCCESS(run, AssetManager_ClearLoadFailures(Fixture.Manager));
        Error Retried = AssetManager_LoadFont(Fixture.Manager, (const unsigned char*)u8"missing_font", User, &LoadedFont);
        TEST_CHECK(run, Retried.Code == ErrorCode_FileNotFound);
        TEST_CHECK(run, !MessageContains(&Retried, "failed to load earlier"));
        Error_Deconstruct(&Retried);
        TEST_CHECK(run, LoadedFont == NULL);

        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

/* Asset names are namespaced keys with the namespace optional: the check, the problem's description, the full form. */
static void AssetNamesAreNamespacedKeys(TestRun* run)
{
    const char* const Valid[] = { "desk", "tbp:desk", "mymod:furniture/desk", "2d_sprite", "world_pbr.v2-final",
        "world.trash_can_lined.lod0", "0:0" };
    for (size_t Index = 0U; Index < (sizeof(Valid) / sizeof(Valid[0])); Index++)
    {
        TEST_CHECK(run, AssetManager_IsValidAssetName((const unsigned char*)Valid[Index])
            && (AssetManager_DescribeAssetNameProblem((const unsigned char*)Valid[Index]) == NULL));
    }
    const char* const Invalid[] = { "", "Bad Name", "Desk", "Town:x", "a:b:c", ":desk", "desk:", "caf\xC3\xA9",
        "desk ", "a\\b", "a/b:c" };
    for (size_t Index = 0U; Index < (sizeof(Invalid) / sizeof(Invalid[0])); Index++)
    {
        TEST_CHECK(run, !AssetManager_IsValidAssetName((const unsigned char*)Invalid[Index])
            && (AssetManager_DescribeAssetNameProblem((const unsigned char*)Invalid[Index]) != NULL));
    }
    TEST_CHECK(run, !AssetManager_IsValidAssetName(NULL) && (AssetManager_DescribeAssetNameProblem(NULL) != NULL));
    TEST_CHECK(run, strstr(AssetManager_DescribeAssetNameProblem((const unsigned char*)u8"Desk"), "uppercase") != NULL);

    // At most 128 bytes in full: a path of 124 bytes fits the default namespace and its colon, 125 does not.
    char Long[NAMESPACED_KEY_BUFFER_SIZE];
    memset(Long, 'a', sizeof(Long));
    Long[124] = '\0';
    TEST_CHECK(run, AssetManager_IsValidAssetName((const unsigned char*)Long));
    Long[124] = 'a';
    Long[125] = '\0';
    TEST_CHECK(run, !AssetManager_IsValidAssetName((const unsigned char*)Long));
    TEST_CHECK(run, strstr(AssetManager_DescribeAssetNameProblem((const unsigned char*)Long), "128 bytes") != NULL);

    // The full form: the default namespace added, a full name kept, nothing appended for an invalid name.
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer Full;
    GenericBuffer_CreateConstant(&Full, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    TEST_CHECK_SUCCESS(run, AssetManager_ParseAssetName((const unsigned char*)u8"desk", &Full));
    TEST_CHECK(run, strcmp((const char*)Storage, "tbp:desk") == 0);
    GenericBuffer_CreateConstant(&Full, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    TEST_CHECK_SUCCESS(run, AssetManager_ParseAssetName((const unsigned char*)u8"mymod:desk", &Full));
    TEST_CHECK(run, strcmp((const char*)Storage, "mymod:desk") == 0);
    GenericBuffer_CreateConstant(&Full, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    TEST_CHECK_ERROR(run, AssetManager_ParseAssetName((const unsigned char*)u8"Bad Name", &Full),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, Full._count == 0U);
    TEST_CHECK_ERROR(run, AssetManager_ParseAssetName(NULL, &Full), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, AssetManager_ParseAssetName((const unsigned char*)u8"desk", NULL), ErrorCode_IllegalArgument);
}

/* The mass of a loaded character, or -1 when none. */
static float GetMass(const CharacterAsset* character)
{
    return (character != NULL) ? CharacterAsset_GetSettings(character)->Mass : -1.0f;
}

/* "walker" and "tbp:walker" name one asset, "mymod:walker" another (characters load without a window). */
static void ShortAndFullNamesNameOneAsset(TestRun* run)
{
    AssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (AssetFixture_Construct(run, "short_and_full_names", &Fixture)
        && AssetFixture_Write(run, &Fixture, "characters/a.json", "{ \"name\": \"walker\", \"mass\": 70 }")
        && AssetFixture_Write(run, &Fixture, "characters/b.json", "{ \"name\": \"mymod:walker\", \"mass\": 20 }")
        && AssetFixture_Read(run, &Fixture, NULL)
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        AssetTypeID Type = Fixture.Types.Character;
        TEST_CHECK(run, AssetFixture_HasDefinition(&Fixture, Type, "walker")
            && AssetFixture_HasDefinition(&Fixture, Type, "tbp:walker")
            && AssetFixture_HasDefinition(&Fixture, Type, "mymod:walker")
            && !AssetFixture_HasDefinition(&Fixture, Type, "other:walker")
            && !AssetFixture_HasDefinition(&Fixture, Type, "Bad Name"));

        CharacterAsset* Short = NULL;
        CharacterAsset* Full = NULL;
        CharacterAsset* Mod = NULL;
        TEST_CHECK_SUCCESS(run, AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"walker", User, &Short));
        TEST_CHECK_SUCCESS(run, AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"tbp:walker", User,
            &Full));
        TEST_CHECK_SUCCESS(run, AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"mymod:walker", User,
            &Mod));
        TEST_CHECK(run, (Short != NULL) && (Short == Full) && (Mod != NULL) && (Mod != Short));
        TEST_CHECK(run, (GetMass(Short) == 70.0f) && (GetMass(Mod) == 20.0f));

        // One hold for the user, released by either spelling.
        TEST_CHECK_SUCCESS(run, AssetManager_ReleaseAsset(Fixture.Manager, Type, (const unsigned char*)u8"tbp:walker", User));
        TEST_CHECK_ERROR(run, AssetManager_ReleaseAsset(Fixture.Manager, Type, (const unsigned char*)u8"walker", User),
            ErrorCode_InvalidOperation);

        // An invalid name is refused, naming the problem.
        CharacterAsset* Bad = NULL;
        Error BadResult = AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"Walker", User, &Bad);
        TEST_CHECK(run, (BadResult.Code == ErrorCode_ArgumentOutOfRange) && (Bad == NULL)
            && MessageContains(&BadResult, "uppercase"));
        Error_Deconstruct(&BadResult);
        TEST_CHECK_ERROR(run, AssetManager_ReleaseAsset(Fixture.Manager, Type, (const unsigned char*)u8"a b", User),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, AssetManager_RemoveDefinition(Fixture.Manager, Type, (const unsigned char*)u8"a b"),
            ErrorCode_ArgumentOutOfRange);

        // Removing by the short name removes the full one.
        TEST_CHECK_SUCCESS(run, AssetManager_RemoveDefinition(Fixture.Manager, Type, (const unsigned char*)u8"walker"));
        TEST_CHECK(run, !AssetFixture_HasDefinition(&Fixture, Type, "tbp:walker"));
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

/* Appends every name of @p type starting with @p prefix as one comma-separated text ("tbp:a,tbp:b"). */
static void ListNames(TestRun* run, AssetFixture* fixture, AssetTypeID type, const char* prefix, char* out, size_t capacity)
{
    GenericBuffer Names;
    GenericBuffer Offsets;
    GenericBuffer_AllocateVariable(&Names, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&Offsets, 0U, sizeof(size_t));
    out[0] = '\0';
    if (TEST_CHECK_SUCCESS(run, AssetManager_GetDefinitionNames(fixture->Manager, type, (const unsigned char*)prefix,
        &Names, &Offsets)))
    {
        const size_t* Starts = (const size_t*)Offsets._data;
        for (size_t Index = 0U; Index < Offsets._count; Index++)
        {
            if (Index > 0U)
            {
                strncat(out, ",", capacity - strlen(out) - 1U);
            }
            strncat(out, (const char*)Names._data + Starts[Index], capacity - strlen(out) - 1U);
        }
    }
    Memory_Free(Names._data);
    Memory_Free(Offsets._data);
}

/* A definition's name in full: read or set in code without its namespace, it is registered, overridden and listed by
 * its full form, and both spellings in one root are a duplicate. */
static void DefinitionsGetTheirFullNames(TestRun* run)
{
    AssetFixture Fixture;
    GenericBuffer HighRoot;
    GenericBuffer_AllocateVariable(&HighRoot, ASSET_TEST_BUFFER_CAPACITY, sizeof(unsigned char));
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (AssetFixture_Construct(run, "full_names", &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "full_names_high", &HighRoot))
        && AssetFixture_Write(run, &Fixture, "characters/walker.json", "{ \"name\": \"walker\", \"mass\": 70 }")
        && AssetFixture_Write(run, &Fixture, "characters/dup_a.json", "{ \"name\": \"dup\" }")
        && AssetFixture_Write(run, &Fixture, "characters/dup_b.json", "{ \"name\": \"tbp:dup\" }")
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(HighRoot._data, "characters/walker.json",
            "{ \"name\": \"tbp:walker\", \"mass\": 90 }"))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_InsertSearchRoot(Fixture.Manager, 0U, HighRoot._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        size_t FailedCount = 0U;
        TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, &FailedCount));
        TEST_CHECK(run, (FailedCount == 1U) && AssetFixture_HasDefinition(&Fixture, Fixture.Types.Character, "dup"));

        // The higher root's "tbp:walker" overrides the lower root's "walker".
        CharacterAsset* Walker = NULL;
        TEST_CHECK_SUCCESS(run, AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"walker", User,
            &Walker));
        TEST_CHECK(run, GetMass(Walker) == 90.0f);

        // Read from bytes: the constructor's name is full already. Given in code without its namespace (a name
        // allocated as definitions allocate theirs), registration makes it full.
        GenericBuffer Bytes;
        GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
        GenericBuffer_AppendString(&Bytes, (const unsigned char*)u8"{ \"name\": \"coded\" }");
        AssetDefinition* Definition = NULL;
        if (TEST_CHECK_SUCCESS(run, AssetManager_BuildDefinition(Fixture.Manager, Fixture.Types.Character, &Bytes,
            (const unsigned char*)u8"test bytes", &Definition)))
        {
            TEST_CHECK(run, strcmp((const char*)Definition->Name, "tbp:coded") == 0);
            Memory_Free(Definition->Name);
            Definition->Name = Memory_Allocate(sizeof(u8"renamed"));
            Memory_Copy(u8"renamed", Definition->Name, sizeof(u8"renamed"));
            TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(Fixture.Manager, Definition));
        }
        Memory_Free(Bytes._data);
        char Listed[256];
        ListNames(run, &Fixture, Fixture.Types.Character, "tbp:", Listed, sizeof(Listed));
        TEST_CHECK(run, strcmp(Listed, "tbp:dup,tbp:renamed,tbp:walker") == 0);

        // A name that is not valid in full is refused at registration, and the definition is destroyed.
        Definition = NULL;
        GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
        GenericBuffer_AppendString(&Bytes, (const unsigned char*)u8"{ \"name\": \"other\" }");
        if (TEST_CHECK_SUCCESS(run, AssetManager_BuildDefinition(Fixture.Manager, Fixture.Types.Character, &Bytes,
            (const unsigned char*)u8"test bytes", &Definition)))
        {
            Memory_Free(Definition->Name);
            Definition->Name = Memory_Allocate(sizeof(u8"No Good"));
            Memory_Copy(u8"No Good", Definition->Name, sizeof(u8"No Good"));
            Error SetResult = AssetManager_SetDefinition(Fixture.Manager, Definition);
            TEST_CHECK(run, (SetResult.Code == ErrorCode_InvalidAssetDefinition) && MessageContains(&SetResult, "No Good"));
            Error_Deconstruct(&SetResult);
        }
        Memory_Free(Bytes._data);
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    Memory_Free(HighRoot._data);
    AssetFixture_Deconstruct(run, &Fixture);
}

/* A type's names by prefix: byte order, the buffers' earlier contents kept, other types left out. */
static void DefinitionNamesAreListedByPrefix(TestRun* run)
{
    AssetFixture Fixture;
    if (AssetFixture_Construct(run, "names_by_prefix", &Fixture)
        && AssetFixture_Write(run, &Fixture, "characters/1.json", "{ \"name\": \"tbp:b\" }")
        && AssetFixture_Write(run, &Fixture, "characters/2.json", "{ \"name\": \"a\" }")
        && AssetFixture_Write(run, &Fixture, "characters/3.json", "{ \"name\": \"mymod:z\" }")
        && AssetFixture_Write(run, &Fixture, "characters/4.json", "{ \"name\": \"mymod:a\" }")
        && AssetFixture_Write(run, &Fixture, "climates/5.json", "{ \"name\": \"tbp:a_climate\" }")
        && AssetFixture_Read(run, &Fixture, NULL))
    {
        AssetTypeID Type = Fixture.Types.Character;
        char Listed[256];
        ListNames(run, &Fixture, Type, "tbp:", Listed, sizeof(Listed));
        TEST_CHECK(run, strcmp(Listed, "tbp:a,tbp:b") == 0);
        ListNames(run, &Fixture, Type, "mymod:", Listed, sizeof(Listed));
        TEST_CHECK(run, strcmp(Listed, "mymod:a,mymod:z") == 0);
        ListNames(run, &Fixture, Type, "", Listed, sizeof(Listed));
        TEST_CHECK(run, strcmp(Listed, "mymod:a,mymod:z,tbp:a,tbp:b") == 0);
        ListNames(run, &Fixture, Type, "a", Listed, sizeof(Listed)); // prefixes are not read as names
        TEST_CHECK(run, strcmp(Listed, "") == 0);

        // Appended after what the buffers hold.
        GenericBuffer Names;
        GenericBuffer Offsets;
        GenericBuffer_AllocateVariable(&Names, 0U, sizeof(unsigned char));
        GenericBuffer_AllocateVariable(&Offsets, 0U, sizeof(size_t));
        size_t Marker = 99U;
        GenericBuffer_AppendString(&Names, (const unsigned char*)u8"x");
        GenericBuffer_AppendByte(&Names, (unsigned char)0);
        GenericBuffer_AddLast(&Offsets, &Marker);
        TEST_CHECK_SUCCESS(run, AssetManager_GetDefinitionNames(Fixture.Manager, Type, (const unsigned char*)u8"tbp:",
            &Names, &Offsets));
        const size_t* Starts = (const size_t*)Offsets._data;
        TEST_CHECK(run, (Offsets._count == 3U) && (Starts[0] == 99U) && (Starts[1] == 2U) && (Starts[2] == 8U)
            && (Names._count == 14U) && (memcmp(Names._data, "x\0tbp:a\0tbp:b\0", 14U) == 0));

        GenericBuffer Wrong;
        GenericBuffer_AllocateVariable(&Wrong, 0U, sizeof(uint32_t));
        TEST_CHECK_ERROR(run, AssetManager_GetDefinitionNames(Fixture.Manager, Type, (const unsigned char*)u8"", &Names,
            &Wrong), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, AssetManager_GetDefinitionNames(Fixture.Manager, Type, NULL, &Names, &Offsets),
            ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, AssetManager_GetDefinitionNames(Fixture.Manager, (AssetTypeID)9999U,
            (const unsigned char*)u8"", &Names, &Offsets), ErrorCode_IllegalArgument);
        TEST_CHECK(run, Offsets._count == 3U);
        Memory_Free(Wrong._data);
        Memory_Free(Names._data);
        Memory_Free(Offsets._data);
    }
    AssetFixture_Deconstruct(run, &Fixture);
}

/* A sprite animation's sheet frames name their sheet with "sprite_sheet", an asset name kept in full; "location" there
 * is refused, as is a missing or invalid name. Loading tries the sheet by its full name (before anything needs GL). */
static void SpriteSheetFramesNameTheirSheet(TestRun* run)
{
    AssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (AssetFixture_Construct(run, "sheet_frames", &Fixture)
        && AssetFixture_Write(run, &Fixture, "sprite_animations/good.json", "{ \"name\": \"good\", \"frames\": [ "
            "{ \"source\": \"spritesheet\", \"sprite_sheet\": \"no_sheet\", \"name\": \"image\" } ] }")
        && AssetFixture_Write(run, &Fixture, "sprite_animations/located.json", "{ \"name\": \"located\", \"frames\": [ "
            "{ \"source\": \"spritesheet\", \"sprite_sheet\": \"no_sheet\", \"location\": \"no_sheet\", "
            "\"name\": \"image\" } ] }")
        && AssetFixture_Write(run, &Fixture, "sprite_animations/unnamed.json", "{ \"name\": \"unnamed\", \"frames\": [ "
            "{ \"source\": \"spritesheet\", \"name\": \"image\" } ] }")
        && AssetFixture_Write(run, &Fixture, "sprite_animations/invalid.json", "{ \"name\": \"invalid\", \"frames\": [ "
            "{ \"source\": \"spritesheet\", \"sprite_sheet\": \"No Sheet\", \"name\": \"image\" } ] }")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        size_t FailedCount = 0U;
        if (AssetFixture_Read(run, &Fixture, &FailedCount))
        {
            AssetTypeID Type = Fixture.Types.SpriteAnimation;
            TEST_CHECK(run, (FailedCount == 3U) && AssetFixture_HasDefinition(&Fixture, Type, "good")
                && !AssetFixture_HasDefinition(&Fixture, Type, "located")
                && !AssetFixture_HasDefinition(&Fixture, Type, "unnamed")
                && !AssetFixture_HasDefinition(&Fixture, Type, "invalid"));
            void* Animation = NULL;
            Error LoadResult = AssetManager_LoadAssetSingle(Fixture.Manager, Type, (const unsigned char*)u8"good", User,
                &Animation);
            TEST_CHECK(run, (LoadResult.Code == ErrorCode_InvalidOperation) && (Animation == NULL)
                && MessageContains(&LoadResult, "No definition named \"tbp:no_sheet\""));
            Error_Deconstruct(&LoadResult);
        }
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
        { "asset_names_are_namespaced_keys", AssetNamesAreNamespacedKeys },
        { "short_and_full_names_name_one_asset", ShortAndFullNamesNameOneAsset },
        { "definitions_get_their_full_names", DefinitionsGetTheirFullNames },
        { "definition_names_are_listed_by_prefix", DefinitionNamesAreListedByPrefix },
        { "sprite_sheet_frames_name_their_sheet", SpriteSheetFramesNameTheirSheet },
    };
    TestRun_RunSuite(run, "AssetManager", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
