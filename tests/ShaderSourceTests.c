#include "TestSuites.h"
#include "AssetManager.h"
#include "ShaderSource.h"
#include "wr/WRJSON.h"
#include <stdio.h>
#include <string.h>


// Macros.
/** Where the game's shipped assets live, relative to the repository root. */
#define SHIPPED_ASSET_ROOT "compile/out/asset"
/** The location every test's inline main text pretends to come from. */
#define TEST_MAIN_LOCATION "main/fragment"


// Types.
/* A manager with the standard asset types whose search root is the test's work directory (or the shipped assets).
 * Tests write include files under Root/shaders/ and expand inline main texts. */
typedef struct ShaderFixtureStruct
{
    AssetManager* Manager;
    StandardAssetTypes Types;
    JSONObjectPool* Pool;
    GenericBuffer Root;
    GenericBuffer Text;
    GenericBuffer SourceList;
} ShaderFixture;


// Static functions.
static bool ShaderFixture_Construct(TestRun* run, ShaderFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    GenericBuffer_AllocateVariable(&self->Root, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->Text, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->SourceList, 0U, sizeof(unsigned char));
    return TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &self->Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(self->Manager, &self->Types, &self->Pool));
}

/* A fixture whose only search root is a fresh work directory named @p name. */
static bool ShaderFixture_ConstructInWorkDirectory(TestRun* run, const char* name, ShaderFixture* self)
{
    return ShaderFixture_Construct(run, self)
        && TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, name, &self->Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(self->Manager, self->Root._data));
}

static void ShaderFixture_Deconstruct(TestRun* run, ShaderFixture* self)
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
    Memory_Free(self->Text._data);
    Memory_Free(self->SourceList._data);
}

/* Writes "<root>/shaders/<relativePath>". */
static bool ShaderFixture_Write(TestRun* run, ShaderFixture* self, const char* relativePath, const char* text)
{
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Path, (const unsigned char*)u8"shaders/");
    GenericBuffer_AppendString(&Path, (const unsigned char*)relativePath);
    GenericBuffer_NullTerminate(&Path);
    bool IsWritten = TEST_CHECK_SUCCESS(run, Test_WriteTextFile(self->Root._data, (const char*)Path._data, text));
    Memory_Free(Path._data);
    return IsWritten;
}

/* Expands @p length bytes of main text (from TEST_MAIN_LOCATION) into the fixture's Text and SourceList, which are
 * cleared first. */
static Error ShaderFixture_ExpandBytes(ShaderFixture* self, const char* text, size_t length)
{
    GenericBuffer_Clear(&self->Text);
    GenericBuffer_Clear(&self->SourceList);
    AssetLocation Main = { AssetLocationType_File, (const unsigned char*)TEST_MAIN_LOCATION };
    return ShaderSource_ExpandIncludes(self->Manager, self->Types.Shader, &Main, (const unsigned char*)text, length,
        &self->Text, &self->SourceList);
}

static Error ShaderFixture_Expand(ShaderFixture* self, const char* text)
{
    return ShaderFixture_ExpandBytes(self, text, strlen(text));
}

static bool TextEquals(const GenericBuffer* buffer, const char* expected)
{
    return (buffer->_count == (strlen(expected) + 1U)) && (strcmp((const char*)buffer->_data, expected) == 0);
}

/* Checks that @p buffer holds exactly @p expected (NUL-terminated), printing both texts when it does not. */
#define CHECK_TEXT(run, buffer, expected) \
    do \
    { \
        if (!TEST_CHECK((run), TextEquals((buffer), (expected)))) \
        { \
            printf("    expected:\n%s\n    actual (%zu bytes):\n%s\n", (expected), (buffer)->_count, \
                ((buffer)->_count > 0U) ? (const char*)(buffer)->_data : ""); \
        } \
    } \
    while (0)

static bool MessageContains(const Error* error, const char* text)
{
    return (error->Message != NULL) && (strstr((const char*)error->Message, text) != NULL);
}

/* Expands @p text, which must fail with ErrorCode_InvalidAssetData and a message containing @p first and @p second
 * (either may be NULL). */
static void ExpansionFails(TestRun* run, ShaderFixture* self, const char* text, const char* first, const char* second)
{
    Error Result = ShaderFixture_Expand(self, text);
    TEST_CHECK(run, Result.Code == ErrorCode_InvalidAssetData);
    if ((first != NULL) && !TEST_CHECK(run, MessageContains(&Result, first)))
    {
        printf("    message: %s\n", (Result.Message != NULL) ? (const char*)Result.Message : "(none)");
    }
    if ((second != NULL) && !TEST_CHECK(run, MessageContains(&Result, second)))
    {
        printf("    message: %s\n", (Result.Message != NULL) ? (const char*)Result.Message : "(none)");
    }
    Error_Deconstruct(&Result);
}

static bool BufferContains(const GenericBuffer* text, const char* needle)
{
    return (text->_count > 0U) && (strstr((const char*)text->_data, needle) != NULL);
}

static size_t CountOccurrences(const GenericBuffer* text, const char* needle)
{
    size_t Count = 0U;
    const char* Cursor = (const char*)text->_data;
    while ((Cursor != NULL) && ((Cursor = strstr(Cursor, needle)) != NULL))
    {
        Count++;
        Cursor += strlen(needle);
    }
    return Count;
}


// Tests.
static void PlainTextPassesThrough(TestRun* run)
{
    // Unknown directives, a commented-out include, CRLF line ends and a last line without a line break all pass through.
    static const char Plain[] =
        "#version 330\r\n"
        "#extension GL_ARB_explicit_attrib_location : enable\n"
        "#define STEPS 4\n"
        "#pragma optimize(on)\n"
        "// #include \"common/nothing\"\n"
        "#includes_nothing\n"
        "void main() {}";
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "plain_text", &Fixture)
        && TEST_CHECK_SUCCESS(run, ShaderFixture_Expand(&Fixture, Plain)))
    {
        CHECK_TEXT(run, &Fixture.Text, Plain);
        TEST_CHECK(run, Fixture.SourceList._count == 0U);
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void OneIncludeSitsBetweenLineDirectives(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "one_include", &Fixture)
        && ShaderFixture_Write(run, &Fixture, "common/a.glsl", "float aLine1;\nfloat aLine2;\n")
        && TEST_CHECK_SUCCESS(run, ShaderFixture_Expand(&Fixture,
            "#version 330\n// main line 2\n#include \"common/a\"\nfloat mainLine4;\n")))
    {
        CHECK_TEXT(run, &Fixture.Text,
            "#version 330\n// main line 2\n#line 1 1\nfloat aLine1;\nfloat aLine2;\n#line 4 0\nfloat mainLine4;\n");
        CHECK_TEXT(run, &Fixture.SourceList, "0 = \"main/fragment\", 1 = \"common/a\"");
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void NestedIncludesNumberTheirFiles(TestRun* run)
{
    // b has no final line break: the #line after it still starts a line of its own. The spaced directive and the
    // trailing comment are allowed forms.
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "nested_includes", &Fixture)
        && ShaderFixture_Write(run, &Fixture, "common/a.glsl", "float a1;\n  #  include   \"common/b\" // b\nfloat a3;\n")
        && ShaderFixture_Write(run, &Fixture, "common/b.glsl", "float b1;")
        && TEST_CHECK_SUCCESS(run, ShaderFixture_Expand(&Fixture, "#version 330\n#include \"common/a\"\nfloat main3;\n")))
    {
        CHECK_TEXT(run, &Fixture.Text,
            "#version 330\n#line 1 1\nfloat a1;\n#line 1 2\nfloat b1;\n#line 3 1\nfloat a3;\n#line 3 0\nfloat main3;\n");
        CHECK_TEXT(run, &Fixture.SourceList, "0 = \"main/fragment\", 1 = \"common/a\", 2 = \"common/b\"");
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void RepeatedIncludeIsSkipped(TestRun* run)
{
    // The second include of a (written with a leading '/'), b's include of a and b's include of the main file each
    // become an empty line.
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "repeated_include", &Fixture)
        && ShaderFixture_Write(run, &Fixture, "common/a.glsl", "float a1;\n")
        && ShaderFixture_Write(run, &Fixture, "common/b.glsl", "#include \"common/a\"\n#include \"main/fragment\"\nfloat b3;\n")
        && TEST_CHECK_SUCCESS(run, ShaderFixture_Expand(&Fixture,
            "#version 330\n#include \"common/a\"\n#include \"/common/a\"\n#include \"common/b\"\n")))
    {
        CHECK_TEXT(run, &Fixture.Text,
            "#version 330\n#line 1 1\nfloat a1;\n#line 3 0\n\n#line 1 2\n\n\nfloat b3;\n#line 5 0\n");
        TEST_CHECK(run, CountOccurrences(&Fixture.Text, "float a1;") == 1U);
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void IncludeDepthIsLimited(TestRun* run)
{
    // d1 .. d8 nest eight levels deep, which is allowed; d8 including d9 is a ninth level.
    ShaderFixture Fixture;
    bool IsWritten = ShaderFixture_ConstructInWorkDirectory(run, "include_depth", &Fixture)
        && ShaderFixture_Write(run, &Fixture, "common/d1.glsl", "#include \"common/d2\"\n")
        && ShaderFixture_Write(run, &Fixture, "common/d2.glsl", "#include \"common/d3\"\n")
        && ShaderFixture_Write(run, &Fixture, "common/d3.glsl", "#include \"common/d4\"\n")
        && ShaderFixture_Write(run, &Fixture, "common/d4.glsl", "#include \"common/d5\"\n")
        && ShaderFixture_Write(run, &Fixture, "common/d5.glsl", "#include \"common/d6\"\n")
        && ShaderFixture_Write(run, &Fixture, "common/d6.glsl", "#include \"common/d7\"\n")
        && ShaderFixture_Write(run, &Fixture, "common/d7.glsl", "#include \"common/d8\"\n")
        && ShaderFixture_Write(run, &Fixture, "common/d8.glsl", "float d8;\n")
        && ShaderFixture_Write(run, &Fixture, "common/d9.glsl", "float d9;\n");
    if (IsWritten && TEST_CHECK_SUCCESS(run, ShaderFixture_Expand(&Fixture, "#version 330\n#include \"common/d1\"\n")))
    {
        TEST_CHECK(run, CountOccurrences(&Fixture.Text, "float d8;") == 1U);
    }
    if (IsWritten && ShaderFixture_Write(run, &Fixture, "common/d8.glsl", "#include \"common/d9\"\n"))
    {
        ExpansionFails(run, &Fixture, "#version 330\n#include \"common/d1\"\n", "common/d9", "nested");
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void MissingIncludeNamesFileAndLine(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "missing_include", &Fixture)
        && ShaderFixture_Write(run, &Fixture, "common/a.glsl", "float a1;\n#include \"common/gone\"\n"))
    {
        ExpansionFails(run, &Fixture, "#version 330\n\n#include \"common/missing\"\n", "\"common/missing\" at line 3",
            TEST_MAIN_LOCATION);
        ExpansionFails(run, &Fixture, "#version 330\n#include \"common/a\"\n", "\"common/gone\" at line 2", "\"common/a\"");
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void VersionStaysFirst(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "version_first", &Fixture)
        && ShaderFixture_Write(run, &Fixture, "common/a.glsl", "float a1;\n")
        && ShaderFixture_Write(run, &Fixture, "common/versioned.glsl", "float v1;\n  #version 330\n"))
    {
        if (TEST_CHECK_SUCCESS(run, ShaderFixture_Expand(&Fixture, "#version 330 core\n#include \"common/a\"\n")))
        {
            TEST_CHECK(run, strncmp((const char*)Fixture.Text._data, "#version 330 core\n#line 1 1\n", 28U) == 0);
        }
        ExpansionFails(run, &Fixture, "// header\n#include \"common/a\"\n#version 330\n", "line 2", "#version");
        ExpansionFails(run, &Fixture, "#version 330\n#include \"common/versioned\"\n", "common/versioned", "line 2");
        // Without a #version line (GLSL 1.10), includes may come anywhere.
        TEST_CHECK_SUCCESS(run, ShaderFixture_Expand(&Fixture, "#include \"common/a\"\nvoid main() {}\n"));
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void MalformedIncludeIsRejected(TestRun* run)
{
    const char* const Malformed[] =
    {
        "#version 330\n#include common/a\n",
        "#version 330\n#include \"common/a\n",
        "#version 330\n#include \"common/a\" float x;\n",
        "#version 330\n#include \"\"\n",
        "#version 330\n#include \"/\"\n",
        "#version 330\n#include <common/a>\n",
        "#version 330\n#include\n"
    };
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "malformed_include", &Fixture)
        && ShaderFixture_Write(run, &Fixture, "common/a.glsl", "float a1;\n"))
    {
        for (size_t Index = 0U; Index < (sizeof(Malformed) / sizeof(Malformed[0])); Index++)
        {
            ExpansionFails(run, &Fixture, Malformed[Index], "Malformed #include at line 2", TEST_MAIN_LOCATION);
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void NulByteIsRejected(TestRun* run)
{
    static const char WithNul[] = "#version 330\nfloat a;\0float b;\n";
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "nul_byte", &Fixture))
    {
        Error Result = ShaderFixture_ExpandBytes(&Fixture, WithNul, sizeof(WithNul) - 1U);
        TEST_CHECK(run, Result.Code == ErrorCode_InvalidAssetData);
        TEST_CHECK(run, MessageContains(&Result, "NUL byte at line 2"));
        Error_Deconstruct(&Result);
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

static void FailureLeavesTheOutputAsItWas(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_ConstructInWorkDirectory(run, "failure_restores", &Fixture))
    {
        GenericBuffer_AppendString(&Fixture.Text, (const unsigned char*)u8"kept");
        GenericBuffer_NullTerminate(&Fixture.Text);
        AssetLocation Main = { AssetLocationType_File, (const unsigned char*)TEST_MAIN_LOCATION };
        static const char Text[] = "#version 330\nfloat a;\n#include \"common/missing\"\n";
        TEST_CHECK_ERROR(run, ShaderSource_ExpandIncludes(Fixture.Manager, Fixture.Types.Shader, &Main,
            (const unsigned char*)Text, sizeof(Text) - 1U, &Fixture.Text, &Fixture.SourceList), ErrorCode_InvalidAssetData);
        CHECK_TEXT(run, &Fixture.Text, "kept");
        TEST_CHECK(run, Fixture.SourceList._count == 0U);
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}

/* Expands a shipped shader stage the way the shader loader does. */
static bool ExpandShippedStage(TestRun* run, ShaderFixture* self, const char* location)
{
    AssetLocation Stage = { AssetLocationType_File, (const unsigned char*)location };
    IOStream* Stream = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_OpenResource(self->Manager, self->Types.Shader, &Stage, NULL, &Stream)))
    {
        return false;
    }
    GenericBuffer Raw;
    GenericBuffer_AllocateVariable(&Raw, 0U, sizeof(unsigned char));
    bool IsExpanded = TEST_CHECK_SUCCESS(run, IOStream_ReadAll(Stream, &Raw));
    TEST_CHECK_SUCCESS(run, AssetManager_CloseResource(self->Manager, Stream));
    GenericBuffer_Clear(&self->Text);
    GenericBuffer_Clear(&self->SourceList);
    IsExpanded = IsExpanded && TEST_CHECK_SUCCESS(run, ShaderSource_ExpandIncludes(self->Manager, self->Types.Shader,
        &Stage, Raw._data, Raw._count, &self->Text, &self->SourceList));
    Memory_Free(Raw._data);
    return IsExpanded;
}

static void ShippedSkyAndPbrShareOneAtmosphere(TestRun* run)
{
    ShaderFixture Fixture;
    if (ShaderFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, SHIPPED_ASSET_ROOT, &Fixture.Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Fixture.Root._data)))
    {
        const char* const Stages[] = { "sky/fragment", "pbr/fragment" };
        for (size_t Index = 0U; Index < (sizeof(Stages) / sizeof(Stages[0])); Index++)
        {
            if (ExpandShippedStage(run, &Fixture, Stages[Index]))
            {
                TEST_CHECK(run, strncmp((const char*)Fixture.Text._data, "#version 330\n", 13U) == 0);
                // Once from the shared file: the function, and its overload without the transmittance.
                TEST_CHECK(run, CountOccurrences(&Fixture.Text, "vec3 atmosphere(") == 2U);
                TEST_CHECK(run, CountOccurrences(&Fixture.Text, "vec2 raySphere(") == 1U);
                TEST_CHECK(run, CountOccurrences(&Fixture.Text, "const float PI ") == 1U);
                TEST_CHECK(run, BufferContains(&Fixture.SourceList, "\"common/atmosphere\""));
            }
        }
    }
    ShaderFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void ShaderSourceTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "plain_text_passes_through", PlainTextPassesThrough },
        { "one_include_sits_between_line_directives", OneIncludeSitsBetweenLineDirectives },
        { "nested_includes_number_their_files", NestedIncludesNumberTheirFiles },
        { "repeated_include_is_skipped", RepeatedIncludeIsSkipped },
        { "include_depth_is_limited", IncludeDepthIsLimited },
        { "missing_include_names_file_and_line", MissingIncludeNamesFileAndLine },
        { "version_stays_first", VersionStaysFirst },
        { "malformed_include_is_rejected", MalformedIncludeIsRejected },
        { "nul_byte_is_rejected", NulByteIsRejected },
        { "failure_leaves_the_output_as_it_was", FailureLeavesTheOutputAsItWas },
        { "shipped_sky_and_pbr_share_one_atmosphere", ShippedSkyAndPbrShareOneAtmosphere },
    };
    TestRun_RunSuite(run, "ShaderSource", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
